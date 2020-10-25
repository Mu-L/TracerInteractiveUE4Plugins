// Copyright Epic Games, Inc. All Rights Reserved.
#include "Sound/SoundSubmix.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Sound/SoundSubmixSend.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Async/Async.h"
#endif // WITH_EDITOR

static int32 ClearBrokenSubmixAssetsCVar = 0;
FAutoConsoleVariableRef CVarFixUpBrokenSubmixAssets(
	TEXT("au.submix.clearbrokensubmixassets"),
	ClearBrokenSubmixAssetsCVar,
	TEXT("If fixed, will verify that we don't have a submix list a child submix that doesn't have it as it's parent, or vice versa.\n")
	TEXT("0: Disable, >0: Enable"),
	ECVF_Default);

USoundSubmixWithParentBase::USoundSubmixWithParentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ParentSubmix(nullptr)
{}

USoundSubmixBase::USoundSubmixBase(const FObjectInitializer& ObjectInitializer)
#if WITH_EDITORONLY_DATA
	: SoundSubmixGraph(nullptr)
#endif // WITH_EDITORONLY_DATA
{}

USoundSubmix::USoundSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bMuteWhenBackgrounded(0)
	, AmbisonicsPluginSettings(nullptr)
	, EnvelopeFollowerAttackTime(10)
	, EnvelopeFollowerReleaseTime(500)
	, OutputVolume(1.0f)
{
}


UEndpointSubmix::UEndpointSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EndpointType(IAudioEndpointFactory::GetTypeNameForDefaultEndpoint())
{

}

USoundfieldSubmix::USoundfieldSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SoundfieldEncodingFormat(ISoundfieldFactory::GetFormatNameForInheritedEncoding())
{}

USoundfieldEndpointSubmix::USoundfieldEndpointSubmix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SoundfieldEndpointType(ISoundfieldEndpointFactory::DefaultSoundfieldEndpointName())
{}

void USoundSubmix::StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* DesiredAudioDevice = ThisWorld->GetAudioDeviceRaw();

	StartRecordingOutput(DesiredAudioDevice, ExpectedDuration);
}

void USoundSubmix::StartRecordingOutput(FAudioDevice* InDevice, float ExpectedDuration)
{
	if (InDevice)
	{
		InDevice->StartRecording(this, ExpectedDuration);
	}
}

void USoundSubmix::StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	FAudioDevice* DesiredAudioDevice = ThisWorld->GetAudioDeviceRaw();

	StopRecordingOutput(DesiredAudioDevice, ExportType, Name, Path, ExistingSoundWaveToOverwrite);
}

void USoundSubmix::StopRecordingOutput(FAudioDevice* InDevice, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite /*= nullptr*/)
{
	if (InDevice)
	{
		float SampleRate;
		float ChannelCount;

		Audio::AlignedFloatBuffer& RecordedBuffer = InDevice->StopRecording(this, ChannelCount, SampleRate);

		// This occurs when Stop Recording Output is called when Start Recording Output was not called.
		if (RecordedBuffer.Num() == 0)
		{
			return;
		}

		// Pack output data into DSPSampleBuffer and record it out!
		RecordingData.Reset(new Audio::FAudioRecordingData());

		RecordingData->InputBuffer = Audio::TSampleBuffer<int16>(RecordedBuffer, ChannelCount, SampleRate);

		switch (ExportType)
		{
			case EAudioRecordingExportType::SoundWave:
			{
				// If we're using the editor, we can write out a USoundWave to the content directory. Otherwise, we just generate a USoundWave without writing it to disk.
				if (GIsEditor)
				{
					RecordingData->Writer.BeginWriteToSoundWave(Name, RecordingData->InputBuffer, Path, [this](const USoundWave* Result)
					{
						if (OnSubmixRecordedFileDone.IsBound())
						{
							OnSubmixRecordedFileDone.Broadcast(Result);
						}
					});
				}
				else
				{
					RecordingData->Writer.BeginGeneratingSoundWaveFromBuffer(RecordingData->InputBuffer, nullptr, [this](const USoundWave* Result)
					{
						if (OnSubmixRecordedFileDone.IsBound())
						{
							OnSubmixRecordedFileDone.Broadcast(Result);
						}
					});
				}
			}
			break;
			
			case EAudioRecordingExportType::WavFile:
			{
				RecordingData->Writer.BeginWriteToWavFile(RecordingData->InputBuffer, Name, Path, [this]()
				{
					if (OnSubmixRecordedFileDone.IsBound())
					{
						OnSubmixRecordedFileDone.Broadcast(nullptr);
					}
				});
			}
			break;

			default:
			break;
		}
	}
}

void USoundSubmix::StartEnvelopeFollowing(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	if (UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw())
		{
			StartEnvelopeFollowing(AudioDevice);
		}
	}
}

void USoundSubmix::StartEnvelopeFollowing(FAudioDevice* InAudioDevice)
{
	if (InAudioDevice)
	{
		InAudioDevice->StartEnvelopeFollowing(this);
	}
}

void USoundSubmix::StopEnvelopeFollowing(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	if (UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw())
		{
			StopEnvelopeFollowing(AudioDevice);
		}
	}
}

void USoundSubmix::StopEnvelopeFollowing(FAudioDevice* InAudioDevice)
{
	if (InAudioDevice)
	{
		InAudioDevice->StopEnvelopeFollowing(this);
	}
}

void USoundSubmix::AddEnvelopeFollowerDelegate(const UObject* WorldContextObject, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP)
{
	if (!GEngine)
	{
		return;
	}

	// Find device for this specific audio recording thing.
	if (UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw())
		{
			AudioDevice->AddEnvelopeFollowerDelegate(this, OnSubmixEnvelopeBP);
		}
	}
}

void USoundSubmix::SetSubmixOutputVolume(const UObject* WorldContextObject, float InOutputVolume)
{
	if (!GEngine)
	{
		return;
	}

	if (UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (FAudioDevice* AudioDevice = ThisWorld->GetAudioDeviceRaw())
		{
			AudioDevice->SetSubmixOutputVolume(this, InOutputVolume);
		}
	}
}
#if WITH_EDITOR
void USoundSubmix::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_OutputVolume(TEXT("OutputVolume"));

		if (PropertyChangedEvent.Property->GetFName() == NAME_OutputVolume)
		{
			FAudioDeviceManager* AudioDeviceManager = (GEngine ? GEngine->GetAudioDeviceManager() : nullptr);
			if (AudioDeviceManager)
			{
				AudioDeviceManager->UpdateSubmix(this);
			}
		}

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundSubmix, SubmixEffectChain))
		{
			// Force the properties to be initialized for this SoundSubmix on all active audio devices
			if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
			{
				AudioDeviceManager->RegisterSoundSubmix(this);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FString USoundSubmixBase::GetDesc()
{
	return FString(TEXT("Sound Submix"));
}

void USoundSubmixBase::BeginDestroy()
{
	Super::BeginDestroy();

	// Use the main/default audio device for storing and retrieving sound class properties
	FAudioDeviceManager* AudioDeviceManager = (GEngine ? GEngine->GetAudioDeviceManager() : nullptr);

	// Force the properties to be initialized for this SoundClass on all active audio devices
	if (AudioDeviceManager)
	{
		AudioDeviceManager->UnregisterSoundSubmix(this);
	}
}

void USoundSubmixBase::PostLoad()
{
	Super::PostLoad();

	if (ClearBrokenSubmixAssetsCVar)
	{
		for (int32 ChildIndex = ChildSubmixes.Num() - 1; ChildIndex >= 0; ChildIndex--)
		{
			USoundSubmixBase* ChildSubmix = ChildSubmixes[ChildIndex];

			if (!ChildSubmix)
			{
				continue;
			}

			if (USoundSubmixWithParentBase* CastedChildSubmix = Cast<USoundSubmixWithParentBase>(ChildSubmix))
			{
				if (!ensure(CastedChildSubmix->ParentSubmix == this))
				{
					UE_LOG(LogAudio, Warning, TEXT("Submix had a child submix that didn't explicitly mark this submix as a parent!"));
					ChildSubmixes.RemoveAtSwap(ChildIndex);
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Submix had a child submix that doesn't have an output!"));
				ChildSubmixes.RemoveAtSwap(ChildIndex);
			}
		}
	}

	// Use the main/default audio device for storing and retrieving sound class properties
	FAudioDeviceManager* AudioDeviceManager = (GEngine ? GEngine->GetAudioDeviceManager() : nullptr);

	// Force the properties to be initialized for this SoundClass on all active audio devices
	if (AudioDeviceManager)
	{
		AudioDeviceManager->RegisterSoundSubmix(this);
	}
}

#if WITH_EDITOR

void USoundSubmixBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		ChildSubmixes.Reset();
	}
}

void USoundSubmixBase::PreEditChange(FProperty* PropertyAboutToChange)
{
	static FName NAME_ChildSubmixes(TEXT("ChildSubmixes"));

	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == NAME_ChildSubmixes)
	{
		// Take a copy of the current state of child classes
		BackupChildSubmixes = ChildSubmixes;
	}
}

void USoundSubmixBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Whether or not we need to reinit the submix. Not all properties require reinitialization.
	bool bReinitSubmix = true;

	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_ChildSubmixes(TEXT("ChildSubmixes"));
		static const FName NAME_ParentSubmix(TEXT("ParentSubmix"));
		static const FName NAME_OutputVolume(TEXT("OutputVolume"));

		if (PropertyChangedEvent.Property->GetFName() == NAME_ChildSubmixes)
		{
			// Find child that was changed/added
			for (int32 ChildIndex = 0; ChildIndex < ChildSubmixes.Num(); ChildIndex++)
			{
				if (ChildSubmixes[ChildIndex] != nullptr && !BackupChildSubmixes.Contains(ChildSubmixes[ChildIndex]))
				{
					if (ChildSubmixes[ChildIndex]->RecurseCheckChild(this))
					{
						// Contains cycle so revert to old layout - launch notification to inform user
						FNotificationInfo Info(NSLOCTEXT("Engine", "UnableToChangeSoundSubmixChildDueToInfiniteLoopNotification", "Could not change SoundSubmix child as it would create a loop"));
						Info.ExpireDuration = 5.0f;
						Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
						FSlateNotificationManager::Get().AddNotification(Info);

						// Revert to the child submixes
						ChildSubmixes = BackupChildSubmixes;
					}
					else if (USoundSubmixWithParentBase* SubmixWithParent = CastChecked<USoundSubmixWithParentBase>(ChildSubmixes[ChildIndex]))
					{
						// Update parentage
						SubmixWithParent->SetParentSubmix(this);
					}
					break;
				}
			}

			// Update old child's parent if it has been removed
			for (int32 ChildIndex = 0; ChildIndex < BackupChildSubmixes.Num(); ChildIndex++)
			{
				if (BackupChildSubmixes[ChildIndex] != nullptr && !ChildSubmixes.Contains(BackupChildSubmixes[ChildIndex]))
				{
					BackupChildSubmixes[ChildIndex]->Modify();
					if (USoundSubmixWithParentBase* SubmixWithParent = Cast<USoundSubmixWithParentBase>(BackupChildSubmixes[ChildIndex]))
					{
						SubmixWithParent->ParentSubmix = nullptr;
					}
				}
			}
				}
			}

	if (GEngine)
	{
		// Force the properties to be initialized for this SoundSubmix on all active audio devices
		if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
		{
			AudioDeviceManager->RegisterSoundSubmix(this);
		}
	}

	BackupChildSubmixes.Reset();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TArray<USoundSubmixBase*> USoundSubmixBase::BackupChildSubmixes;

bool USoundSubmixBase::RecurseCheckChild(const USoundSubmixBase* ChildSoundSubmix) const
{
	for (int32 Index = 0; Index < ChildSubmixes.Num(); Index++)
	{
		if (ChildSubmixes[Index])
		{
			if (ChildSubmixes[Index] == ChildSoundSubmix)
			{
				return true;
			}

			if (ChildSubmixes[Index]->RecurseCheckChild(ChildSoundSubmix))
			{
				return true;
			}
		}
	}

	return false;
}

void USoundSubmixWithParentBase::SetParentSubmix(USoundSubmixBase* InParentSubmix)
{
	if (ParentSubmix != InParentSubmix)
	{
		if (ParentSubmix)
		{
			ParentSubmix->Modify();
			ParentSubmix->ChildSubmixes.Remove(this);
		}

		Modify();
		ParentSubmix = InParentSubmix;
		if (ParentSubmix)
		{
			ParentSubmix->ChildSubmixes.AddUnique(this);
		}
	}
}

void USoundSubmixWithParentBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!GEngine)
	{
		return;
	}

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName ChangedPropName = PropertyChangedEvent.Property->GetFName();

		if (ChangedPropName == GET_MEMBER_NAME_CHECKED(USoundSubmixWithParentBase, ParentSubmix))
		{
			// Add this sound class to the parent class if it's not already added
			if (ParentSubmix)
			{
				bool bIsChildSubmix = false;
				for (int32 i = 0; i < ParentSubmix->ChildSubmixes.Num(); ++i)
				{
					USoundSubmixBase* ChildSubmix = ParentSubmix->ChildSubmixes[i];
					if (ChildSubmix && ChildSubmix == this)
					{
						bIsChildSubmix = true;
						break;
					}
				}

				if (!bIsChildSubmix)
				{
					ParentSubmix->Modify();
					ParentSubmix->ChildSubmixes.AddUnique(this);
				}
			}

			Modify();

			// Force the properties to be initialized for this SoundSubmix on all active audio devices
			if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
			{
				AudioDeviceManager->RegisterSoundSubmix(this);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void USoundSubmixWithParentBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		SetParentSubmix(nullptr);
	}

	Super::PostDuplicate(DuplicateMode);
}

void USoundSubmixBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	USoundSubmixBase* This = CastChecked<USoundSubmixBase>(InThis);

	Collector.AddReferencedObject(This->SoundSubmixGraph, This);

	for (USoundSubmixBase* Backup : This->BackupChildSubmixes)
	{
		Collector.AddReferencedObject(Backup);
	}

	Super::AddReferencedObjects(InThis, Collector);
}
#endif // WITH_EDITOR

ISoundfieldFactory* USoundfieldSubmix::GetSoundfieldFactoryForSubmix() const
{
	// If this isn't called in the game thread, a ParentSubmix could get destroyed while we are recursing through the submix graph.
	ensure(IsInGameThread());

	FName SoundfieldFormat = GetSubmixFormat();
	check(SoundfieldFormat != ISoundfieldFactory::GetFormatNameForInheritedEncoding());

	return ISoundfieldFactory::Get(SoundfieldFormat);
}

const USoundfieldEncodingSettingsBase* USoundfieldSubmix::GetSoundfieldEncodingSettings() const
{
	return GetEncodingSettings();
}

TArray<USoundfieldEffectBase *> USoundfieldSubmix::GetSoundfieldProcessors() const
{
	return SoundfieldEffectChain;
}

FName USoundfieldSubmix::GetSubmixFormat() const
{
	USoundfieldSubmix* ParentSoundfieldSubmix = Cast<USoundfieldSubmix>(ParentSubmix);

	if (!ParentSoundfieldSubmix || SoundfieldEncodingFormat != ISoundfieldFactory::GetFormatNameForInheritedEncoding())
	{
		if (SoundfieldEncodingFormat == ISoundfieldFactory::GetFormatNameForInheritedEncoding())
		{
			return ISoundfieldFactory::GetFormatNameForNoEncoding();
		}
		else
		{
			return SoundfieldEncodingFormat;
		}

	}
	else if(ParentSoundfieldSubmix)
	{
		// If this submix matches the format of whatever submix it's plugged into, 
		// Recurse into the submix graph to find it.
		return ParentSoundfieldSubmix->GetSubmixFormat();
	}
	else
	{
		return ISoundfieldFactory::GetFormatNameForNoEncoding();
		}
}

const USoundfieldEncodingSettingsBase* USoundfieldSubmix::GetEncodingSettings() const
{
	FName SubmixFormatName = GetSubmixFormat();

	USoundfieldSubmix* ParentSoundfieldSubmix = Cast<USoundfieldSubmix>(ParentSubmix);

	if (EncodingSettings)
	{
		return EncodingSettings;
	}
	else if (ParentSoundfieldSubmix && SoundfieldEncodingFormat == ISoundfieldFactory::GetFormatNameForInheritedEncoding())
	{
		// If this submix matches the format of whatever it's plugged into,
		// Recurse into the submix graph to match it's settings.
		return ParentSoundfieldSubmix->GetEncodingSettings();
	}
	else if (ISoundfieldFactory* Factory = ISoundfieldFactory::Get(SubmixFormatName))
	{
		// If we don't have any encoding settings, use the default.
		return Factory->GetDefaultEncodingSettings();
	}
	else
	{
		// If we don't have anything, exit.
		return nullptr;
	}
}

void USoundfieldSubmix::SanitizeLinks()
{
	bool bShouldRefreshGraph = false;

	// Iterate through children and check encoding formats.
	for (int32 Index = ChildSubmixes.Num() - 1; Index >= 0; Index--)
	{
		if (!SubmixUtils::AreSubmixFormatsCompatible(ChildSubmixes[Index], this))
		{
			CastChecked<USoundSubmixWithParentBase>(ChildSubmixes[Index])->ParentSubmix = nullptr;
			ChildSubmixes[Index]->Modify();
			ChildSubmixes.RemoveAtSwap(Index);
			bShouldRefreshGraph = true;
		}
	}

	// If this submix is now incompatible with the parent submix, disconnect it.
	if (!SubmixUtils::AreSubmixFormatsCompatible(this, ParentSubmix))
	{
		ParentSubmix->ChildSubmixes.RemoveSwap(this);
		ParentSubmix->Modify();
		ParentSubmix = nullptr;
		bShouldRefreshGraph = true;
	}

	if (bShouldRefreshGraph)
	{
#if WITH_EDITOR
		SubmixUtils::RefreshEditorForSubmix(this);
#endif
	}
}

#if WITH_EDITOR

void USoundfieldSubmix::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Whether to clean up now invalid links between submix and refresh the submix graph editor.
	bool bShouldSanitizeLinks = false;

	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_SoundfieldFormat(TEXT("SoundfieldEncodingFormat"));

		if (PropertyChangedEvent.Property->GetFName() == NAME_SoundfieldFormat)
		{
			bShouldSanitizeLinks = true;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bShouldSanitizeLinks)
	{
		SanitizeLinks();
	}
}

#endif // WITH_EDITOR

IAudioEndpointFactory* UEndpointSubmix::GetAudioEndpointForSubmix() const
{
	return IAudioEndpointFactory::Get(EndpointType);
}

const UAudioEndpointSettingsBase* UEndpointSubmix::GetEndpointSettings() const
{
	return EndpointSettings;
}

ISoundfieldEndpointFactory* USoundfieldEndpointSubmix::GetSoundfieldEndpointForSubmix() const
{
	return ISoundfieldEndpointFactory::Get(SoundfieldEndpointType);
}

const USoundfieldEndpointSettingsBase* USoundfieldEndpointSubmix::GetEndpointSettings() const
{
	return EndpointSettings;
}

const USoundfieldEncodingSettingsBase* USoundfieldEndpointSubmix::GetEncodingSettings() const
{
	return EncodingSettings;
}

TArray<USoundfieldEffectBase*> USoundfieldEndpointSubmix::GetSoundfieldProcessors() const
{
	return SoundfieldEffectChain;
}

void USoundfieldEndpointSubmix::SanitizeLinks()
{
	bool bShouldRefreshEditor = false;

	// Iterate through children and check encoding formats.
	for (int32 Index = ChildSubmixes.Num() - 1; Index >= 0; Index--)
	{
		if (!SubmixUtils::AreSubmixFormatsCompatible(ChildSubmixes[Index], this))
		{
			CastChecked<USoundSubmixWithParentBase>(ChildSubmixes[Index])->ParentSubmix = nullptr;
			ChildSubmixes[Index]->Modify();
			ChildSubmixes.RemoveAtSwap(Index);

			bShouldRefreshEditor = true;
		}
	}
	
	if (bShouldRefreshEditor)
	{
#if WITH_EDITOR
		SubmixUtils::RefreshEditorForSubmix(this);
#endif
	}
}

#if WITH_EDITOR

void USoundfieldEndpointSubmix::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName NAME_SoundfieldFormat(TEXT("SoundfieldEndpointType"));

		if (PropertyChangedEvent.Property->GetFName() == NAME_SoundfieldFormat)
		{
			// Add this sound class to the parent class if it's not already added
			SanitizeLinks();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

ENGINE_API bool SubmixUtils::AreSubmixFormatsCompatible(const USoundSubmixBase* ChildSubmix, const USoundSubmixBase* ParentSubmix)
{
	const USoundfieldSubmix* ChildSoundfieldSubmix = Cast<const USoundfieldSubmix>(ChildSubmix);

	// If both the child and parent are soundfield submixes, ensure that their formats are compatible.
	{
		const USoundfieldSubmix* ParentSoundfieldSubmix = Cast<const USoundfieldSubmix>(ParentSubmix);

		if (ChildSoundfieldSubmix && ParentSoundfieldSubmix)
		{
			ISoundfieldFactory* ChildSoundfieldFactory = ChildSoundfieldSubmix->GetSoundfieldFactoryForSubmix();
			ISoundfieldFactory* ParentSoundfieldFactory = ParentSoundfieldSubmix->GetSoundfieldFactoryForSubmix();

			if (ChildSoundfieldFactory && ParentSoundfieldFactory)
			{
				return ChildSoundfieldFactory->CanTranscodeToSoundfieldFormat(ParentSoundfieldFactory->GetSoundfieldFormatName(), *(ParentSoundfieldSubmix->GetSoundfieldEncodingSettings()->GetProxy()))
					|| ParentSoundfieldFactory->CanTranscodeFromSoundfieldFormat(ChildSoundfieldFactory->GetSoundfieldFormatName(), *(ChildSoundfieldSubmix->GetSoundfieldEncodingSettings()->GetProxy()));
			}
			else
			{
				return true;
			}
		}
	}

	// If the child is a soundfield submix and the parent is a soundfield endpoint submix, ensure that they have compatible formats.
	{
		const USoundfieldEndpointSubmix* ParentSoundfieldEndpointSubmix = Cast<const USoundfieldEndpointSubmix>(ParentSubmix);
		
		if (ChildSoundfieldSubmix && ParentSoundfieldEndpointSubmix)
		{
			ISoundfieldFactory* ChildSoundfieldFactory = ChildSoundfieldSubmix->GetSoundfieldFactoryForSubmix();
			ISoundfieldFactory* ParentSoundfieldFactory = ParentSoundfieldEndpointSubmix->GetSoundfieldEndpointForSubmix();

			if (ChildSoundfieldFactory && ParentSoundfieldFactory)
			{
				return ChildSoundfieldFactory->CanTranscodeToSoundfieldFormat(ParentSoundfieldFactory->GetSoundfieldFormatName(),  *(ParentSoundfieldEndpointSubmix->GetEncodingSettings()->GetProxy()))
					|| ParentSoundfieldFactory->CanTranscodeFromSoundfieldFormat(ChildSoundfieldFactory->GetSoundfieldFormatName(), *(ChildSoundfieldSubmix->GetSoundfieldEncodingSettings()->GetProxy()));
			}
			else
			{
				return true;
			}
		}
	}

	// Otherwise, these submixes are compatible.
	return true;
}

#if WITH_EDITOR

ENGINE_API void SubmixUtils::RefreshEditorForSubmix(const USoundSubmixBase* InSubmix)
{
	if (!GEditor || !InSubmix)
	{
		return;
	}

	TWeakObjectPtr<USoundSubmixBase> WeakSubmix = TWeakObjectPtr<USoundSubmixBase>(const_cast<USoundSubmixBase*>(InSubmix));

	// Since we may be in the middle of a PostEditProperty call,
	// Dispatch a command to close and reopen the editor window next tick.
	AsyncTask(ENamedThreads::GameThread, [WeakSubmix]
	{
			if (WeakSubmix.IsValid())
			{
				UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				TArray<IAssetEditorInstance*> SubmixEditors = EditorSubsystem->FindEditorsForAsset(WeakSubmix.Get());
				for (IAssetEditorInstance* Editor : SubmixEditors)
				{
					Editor->CloseWindow();
				}

				EditorSubsystem->OpenEditorForAsset(WeakSubmix.Get());
			}
	});
}

#endif // WITH_EDITOR
