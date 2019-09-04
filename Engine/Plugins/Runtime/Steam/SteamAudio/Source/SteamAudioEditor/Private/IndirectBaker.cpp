//
// Copyright (C) Valve Corporation. All rights reserved.
//

#include "IndirectBaker.h"
#include "TickableNotification.h"
#include "PhononSourceComponent.h"
#include "PhononProbeVolume.h"
#include "PhononScene.h"
#include "SteamAudioSettings.h"
#include "SteamAudioEditorModule.h"
#include "PhononReverb.h"
#include "SteamAudioEnvironment.h"

#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelEditorViewport.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Components/AudioComponent.h"

namespace SteamAudio
{
	std::atomic<bool> GIsBaking(false);

	static TSharedPtr<FTickableNotification> GBakeTickable = MakeShareable(new FTickableNotification());
	static int32 GCurrentProbeVolume = 0;
	static int32 GNumProbeVolumes = 0;
	static int32 GCurrentBakeTask = 0;
	static int32 GNumBakeTasks = 0;

	static void BakeProgressCallback(float Progress)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("BakeProgress"), FText::AsPercent(Progress));
		Arguments.Add(TEXT("CurrentProbeVolume"), FText::AsNumber(GCurrentProbeVolume));
		Arguments.Add(TEXT("NumProbeVolumes"), FText::AsNumber(GNumProbeVolumes));
		Arguments.Add(TEXT("NumBakeTasks"), FText::AsNumber(GNumBakeTasks));
		Arguments.Add(TEXT("CurrentBakeTask"), FText::AsNumber(GCurrentBakeTask));
		GBakeTickable->SetDisplayText(FText::Format(NSLOCTEXT("SteamAudio", "BakeProgressFmt", "Baking {CurrentBakeTask}/{NumBakeTasks} sources \n {CurrentProbeVolume}/{NumProbeVolumes} probe volumes ({BakeProgress} complete)"), Arguments));
	}

	static void CancelBake()
	{
		iplCancelBake();
		GIsBaking.store(false);
	}

	/**
	 * Bakes propagation for all sources in PhononSourceComponents. Bakes reverb if BakeReverb is set. Performs baking across all probe volumes 
	 * in the scene. Runs baking in an async task so that UI remains responsive.
	 */
	void Bake(const TArray<UPhononSourceComponent*> PhononSourceComponents, const bool BakeReverb, FBakedSourceUpdated BakedSourceUpdated)
	{
		GIsBaking.store(true);

		GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "Baking", "Baking..."));
		GBakeTickable->CreateNotificationWithCancel(FSimpleDelegate::CreateStatic(CancelBake));

		auto World = GEditor->GetLevelViewportClients()[0]->GetWorld();
		check(World);

		GNumBakeTasks = BakeReverb ? PhononSourceComponents.Num() + 1 : PhononSourceComponents.Num();
		GCurrentBakeTask = 1;

		// Get all probe volumes (cannot do this in the async task - not on game thread)
		TArray<AActor*> PhononProbeVolumes;
		UGameplayStatics::GetAllActorsOfClass(World, APhononProbeVolume::StaticClass(), PhononProbeVolumes);

		Async(EAsyncExecution::Thread, [=]()
		{
			// Ensure we have at least one probe
			bool AtLeastOneProbe = false;

			for (auto PhononProbeVolumeActor : PhononProbeVolumes)
			{
				auto PhononProbeVolume = Cast<APhononProbeVolume>(PhononProbeVolumeActor);
				if (PhononProbeVolume->NumProbes > 0)
				{
					AtLeastOneProbe = true;
					break;
				}
			}

			if (!AtLeastOneProbe)
			{
				UE_LOG(LogSteamAudioEditor, Error, TEXT("Ensure at least one Phonon Probe Volume with probes exists."));
				GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "BakeFailed_NoProbes", "Bake failed. Create at least one Phonon Probe Volume that has probes."));
				GBakeTickable->DestroyNotification(SNotificationItem::CS_Fail);
				GIsBaking.store(false);
				return;
			}

			IPLBakingSettings BakingSettings;
			BakingSettings.bakeParametric = IPL_FALSE;
			BakingSettings.bakeConvolution = IPL_TRUE;

			IPLSimulationSettings SimulationSettings;
			SimulationSettings.sceneType = IPL_SCENETYPE_PHONON;
			SimulationSettings.irDuration = GetDefault<USteamAudioSettings>()->IndirectImpulseResponseDuration;
			SimulationSettings.ambisonicsOrder = GetDefault<USteamAudioSettings>()->IndirectImpulseResponseOrder;
			SimulationSettings.maxConvolutionSources = 1024; // FIXME
			SimulationSettings.numBounces = GetDefault<USteamAudioSettings>()->BakedBounces;
			SimulationSettings.numRays = GetDefault<USteamAudioSettings>()->BakedRays;
			SimulationSettings.numDiffuseSamples = GetDefault<USteamAudioSettings>()->BakedSecondaryRays;

			IPLhandle ComputeDevice = nullptr;
			IPLhandle PhononScene = nullptr;
			IPLhandle PhononEnvironment = nullptr;
			FPhononSceneInfo PhononSceneInfo;

			GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "LoadingScene", "Loading scene..."));

			// Load the scene
			if (!LoadSceneFromDisk(World, ComputeDevice, SimulationSettings, &PhononScene, PhononSceneInfo))
			{
				// If we can't find the scene, then presumably they haven't generated probes either, so just exit
				UE_LOG(LogSteamAudioEditor, Error, TEXT("Unable to create Phonon environment: .phononscene not found. Be sure to export the scene."));
				GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "BakeFailed_NoScene", "Bake failed. Export scene first."));
				GBakeTickable->DestroyNotification(SNotificationItem::CS_Fail);
				GIsBaking.store(false);
				return;
			}

			iplCreateEnvironment(SteamAudio::GlobalContext, ComputeDevice, SimulationSettings, PhononScene, nullptr, &PhononEnvironment);

			if (BakeReverb)
			{
				GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "Baking", "Baking..."));
				GNumProbeVolumes = PhononProbeVolumes.Num();
				GCurrentProbeVolume = 1;

				for (auto PhononProbeVolumeActor : PhononProbeVolumes)
				{
					auto PhononProbeVolume = Cast<APhononProbeVolume>(PhononProbeVolumeActor);

					IPLhandle ProbeBox = nullptr;
					PhononProbeVolume->LoadProbeBoxFromDisk(&ProbeBox);

					IPLBakedDataIdentifier ReverbIdentifier;
					ReverbIdentifier.identifier = 0;
					ReverbIdentifier.type = IPL_BAKEDDATATYPE_REVERB;

					iplDeleteBakedDataByIdentifier(ProbeBox, ReverbIdentifier);
					iplBakeReverb(PhononEnvironment, ProbeBox, BakingSettings, BakeProgressCallback);

					if (!GIsBaking.load())
					{
						iplDestroyProbeBox(&ProbeBox);
						break;
					}

					FBakedDataInfo BakedDataInfo;
					BakedDataInfo.Name = "__reverb__";
					BakedDataInfo.Size = iplGetBakedDataSizeByIdentifier(ProbeBox, ReverbIdentifier);

					auto ExistingInfo = PhononProbeVolume->BakedDataInfo.FindByPredicate([=](const FBakedDataInfo& InfoItem)
					{
						return InfoItem.Name == BakedDataInfo.Name;
					});

					if (ExistingInfo)
					{
						ExistingInfo->Size = BakedDataInfo.Size;
					}
					else
					{
						PhononProbeVolume->BakedDataInfo.Add(BakedDataInfo);
						PhononProbeVolume->BakedDataInfo.Sort();
					}

					PhononProbeVolume->UpdateProbeData(ProbeBox);
					iplDestroyProbeBox(&ProbeBox);
					++GCurrentProbeVolume;
				}

				if (!GIsBaking.load())
				{
					iplDestroyEnvironment(&PhononEnvironment);
					iplDestroyScene(&PhononScene);

					GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "BakeCancelled", "Bake cancelled."));
					GBakeTickable->DestroyNotification(SNotificationItem::CS_Fail);
					return;
				}

				BakedSourceUpdated.ExecuteIfBound("__reverb__");

				++GCurrentBakeTask;
			}

			// IN PROGRESS
			FIdentifierMap BakedIdentifierMap;
			LoadBakedIdentifierMapFromDisk(World, BakedIdentifierMap);

			for (auto PhononSourceComponent : PhononSourceComponents)
			{
				// Set the User ID on the audio component
				UAudioComponent* AudioComponent = Cast<UAudioComponent>(PhononSourceComponent->GetOwner()->GetComponentByClass(UAudioComponent::StaticClass()));
				
				if (AudioComponent == nullptr)
				{
					UE_LOG(LogSteamAudioEditor, Warning, TEXT("Actor containing the Phonon source \"%s\" has no Audio Component. It will be skipped."), *(PhononSourceComponent->UniqueIdentifier.ToString()));
				}
				else
				{
					AudioComponent->AudioComponentUserID = PhononSourceComponent->UniqueIdentifier;
					FString SourceString = AudioComponent->GetAudioComponentUserID().ToString().ToLower();
					if (!BakedIdentifierMap.ContainsKey(SourceString))
					{
						BakedIdentifierMap.Add(SourceString);
					}

					IPLBakedDataIdentifier SourceIdentifier;
					SourceIdentifier.type = IPL_BAKEDDATATYPE_STATICSOURCE;
					SourceIdentifier.identifier = BakedIdentifierMap.Get(SourceString);

					GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "Baking...", "Baking..."));
					GNumProbeVolumes = PhononProbeVolumes.Num();
					GCurrentProbeVolume = 1;

					for (auto PhononProbeVolumeActor : PhononProbeVolumes)
					{
						auto PhononProbeVolume = Cast<APhononProbeVolume>(PhononProbeVolumeActor);

						IPLhandle ProbeBox = nullptr;
						PhononProbeVolume->LoadProbeBoxFromDisk(&ProbeBox);

						IPLSphere SourceInfluence;
						SourceInfluence.radius = PhononSourceComponent->BakingRadius * SteamAudio::SCALEFACTOR;
						SourceInfluence.center = SteamAudio::UnrealToPhononIPLVector3(PhononSourceComponent->GetComponentLocation());

						iplDeleteBakedDataByIdentifier(ProbeBox, SourceIdentifier);
						iplBakePropagation(PhononEnvironment, ProbeBox, SourceInfluence, SourceIdentifier, BakingSettings, BakeProgressCallback);

						if (!GIsBaking.load())
						{
							iplDestroyProbeBox(&ProbeBox);
							break;
						}

						FBakedDataInfo BakedDataInfo;
						BakedDataInfo.Name = PhononSourceComponent->UniqueIdentifier;
						BakedDataInfo.Size = iplGetBakedDataSizeByIdentifier(ProbeBox, SourceIdentifier);

						auto ExistingInfo = PhononProbeVolume->BakedDataInfo.FindByPredicate([=](const FBakedDataInfo& InfoItem)
						{
							return InfoItem.Name == BakedDataInfo.Name;
						});

						if (ExistingInfo)
						{
							ExistingInfo->Size = BakedDataInfo.Size;
						}
						else
						{
							PhononProbeVolume->BakedDataInfo.Add(BakedDataInfo);
							PhononProbeVolume->BakedDataInfo.Sort();
						}

						PhononProbeVolume->UpdateProbeData(ProbeBox);
						iplDestroyProbeBox(&ProbeBox);
						++GCurrentProbeVolume;
					}

					if (!GIsBaking.load())
					{
						break;
					}

					BakedSourceUpdated.ExecuteIfBound(PhononSourceComponent->UniqueIdentifier);

					++GCurrentBakeTask;
				}
			}

			SaveBakedIdentifierMapToDisk(World, BakedIdentifierMap);

			iplDestroyEnvironment(&PhononEnvironment);
			iplDestroyScene(&PhononScene);

			if (!GIsBaking.load())
			{
				GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "BakeCancelled", "Bake cancelled."));
				GBakeTickable->DestroyNotification(SNotificationItem::CS_Fail);
			}
			else
			{
				GBakeTickable->SetDisplayText(NSLOCTEXT("SteamAudio", "BakePropagationComplete", "Bake propagation complete."));
				GBakeTickable->DestroyNotification();
				GIsBaking.store(false);
			}
		});
	}
}