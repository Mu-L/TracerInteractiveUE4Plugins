// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/HLODProxy.h"
#include "GameFramework/WorldSettings.h"

#if WITH_EDITOR
#include "Misc/ConfigCacheIni.h"
#include "Serialization/ArchiveObjectCrc32.h"
#endif
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "LevelUtils.h"
#include "Engine/LevelStreaming.h"
#include "Math/UnrealMathUtility.h"

#if WITH_EDITOR

void UHLODProxy::SetMap(const UWorld* InMap)
{
	// Level should only be set once
	check(OwningMap.IsNull());

	OwningMap = InMap;
}

TSoftObjectPtr<UWorld> UHLODProxy::GetMap() const
{
	return OwningMap;
}

void UHLODProxy::AddMesh(ALODActor* InLODActor, UStaticMesh* InStaticMesh, const FName& InKey)
{
	InLODActor->Proxy = this;
	FHLODProxyMesh NewProxyMesh(InLODActor, InStaticMesh, InKey);
	ProxyMeshes.AddUnique(NewProxyMesh);
}

void UHLODProxy::Clean()
{
	// The level we reference must be loaded to clean this package
	check(OwningMap.IsNull() || OwningMap.ToSoftObjectPath().ResolveObject() != nullptr);

	// Remove all entries that have invalid actors
	ProxyMeshes.RemoveAll([](const FHLODProxyMesh& InProxyMesh)
	{ 
		TLazyObjectPtr<ALODActor> LODActor = InProxyMesh.GetLODActor();

		// Invalid actor means that it has been deleted so we shouldnt hold onto its data
		if(!LODActor.IsValid())
		{
			return true;
		}
		else if(LODActor.Get()->Proxy == nullptr)
		{
			// No proxy means we are also invalid
			return true;
		}
		else if(!LODActor.Get()->Proxy->ContainsDataForActor(LODActor.Get()))
		{
			// actor and proxy are valid, but key differs (unbuilt)
			return true;
		}

		return false;
	});
}

const AActor* UHLODProxy::FindFirstActor(const ALODActor* LODActor)
{
	auto RecursiveFindFirstActor = [&](const ALODActor* InLODActor)
	{
		const AActor* FirstActor = InLODActor->SubActors.IsValidIndex(0) ? InLODActor->SubActors[0] : nullptr;
		while (FirstActor != nullptr && FirstActor->IsA<ALODActor>())
		{
			const ALODActor* SubLODActor = Cast<ALODActor>(FirstActor);
			if (SubLODActor)
			{
				FirstActor = SubLODActor->SubActors.IsValidIndex(0) ? SubLODActor->SubActors[0] : nullptr; 
			}
			else
			{
				// Unable to find a valid actor
				FirstActor = nullptr;
			}
		}
		return FirstActor;
	};

	// Retrieve first 'valid' AActor (non ALODActor)
	const AActor* FirstValidActor = nullptr;

	for (int32 Index = 0; Index < LODActor->SubActors.Num(); ++Index)
	{
		const AActor* SubActor = LODActor->SubActors[Index];

		if (const ALODActor* SubLODActor = Cast<ALODActor>(SubActor))
		{
			SubActor = RecursiveFindFirstActor(SubLODActor);
		}

		if (SubActor != nullptr)
		{
			FirstValidActor = SubActor;
			break;
		}
	}

	return FirstValidActor;
}

void UHLODProxy::ExtractStaticMeshComponentsFromLODActor(const ALODActor* LODActor, TArray<UStaticMeshComponent*>& InOutComponents)
{
	check(LODActor);

	for (const AActor* ChildActor : LODActor->SubActors)
	{
		if(ChildActor)
		{
			TArray<UStaticMeshComponent*> ChildComponents;
			if (ChildActor->IsA<ALODActor>())
			{
				ExtractStaticMeshComponentsFromLODActor(Cast<ALODActor>(ChildActor), ChildComponents);
			}
			else
			{
				ChildActor->GetComponents<UStaticMeshComponent>(ChildComponents);
			}

			InOutComponents.Append(ChildComponents);
		}
	}
}

void UHLODProxy::ExtractComponents(const ALODActor* LODActor, TArray<UPrimitiveComponent*>& InOutComponents)
{
	check(LODActor);

	for (const AActor* Actor : LODActor->SubActors)
	{
		if(Actor)
		{
			TArray<UStaticMeshComponent*> Components;

			if (Actor->IsA<ALODActor>())
			{
				ExtractStaticMeshComponentsFromLODActor(Cast<ALODActor>(Actor), Components);
			}
			else
			{
				Actor->GetComponents<UStaticMeshComponent>(Components);
			}

			Components.RemoveAll([&](UStaticMeshComponent* Val)
			{
				return Val->GetStaticMesh() == nullptr || !Val->ShouldGenerateAutoLOD(LODActor->LODLevel - 1);
			});

			InOutComponents.Append(Components);
		}
	}
}

uint32 UHLODProxy::GetCRC(UMaterialInterface* InMaterialInterface, uint32 InCRC)
{
	TArray<uint8> KeyBuffer;

	UMaterialInterface* MaterialInterface = InMaterialInterface;
	while(MaterialInterface)
	{
		// Walk material parent chain for instances with known states (we cant support MIDs directly as they are always changing)
		if(UMaterialInstance* MI = Cast<UMaterialInstance>(MaterialInterface))
		{
			if(UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI))
			{
				KeyBuffer.Append((uint8*)&MIC->ParameterStateId, sizeof(FGuid));
			}
			MaterialInterface = MI->Parent;
		}
		else if(UMaterial* Material = Cast<UMaterial>(MaterialInterface))
		{
			KeyBuffer.Append((uint8*)&Material->StateId, sizeof(FGuid));
			MaterialInterface = nullptr;
		}
	}

	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

uint32 UHLODProxy::GetCRC(UTexture* InTexture, uint32 InCRC)
{
	// Default to just the path name if we don't have render data
	 if (InTexture->GetRunningPlatformData() != nullptr)
     {
         FTexturePlatformData* PlatformData = *InTexture->GetRunningPlatformData();
         if (PlatformData != nullptr)
         {
             return FCrc::StrCrc32(*PlatformData->DerivedDataKey, InCRC);
         }
     }
 
     // Default to just the path name if we don't have render data
     return FCrc::StrCrc32(*InTexture->GetPathName(), InCRC);
}

uint32 UHLODProxy::GetCRC(UStaticMesh* InStaticMesh, uint32 InCRC)
{
	TArray<uint8> KeyBuffer;

	// Default to just the path name if we don't have render data
	FString DerivedDataKey = InStaticMesh->RenderData.IsValid() ? InStaticMesh->RenderData->DerivedDataKey : InStaticMesh->GetPathName();
	KeyBuffer.Append((uint8*)DerivedDataKey.GetCharArray().GetData(), DerivedDataKey.GetCharArray().Num() * DerivedDataKey.GetCharArray().GetTypeSize());
	KeyBuffer.Append((uint8*)&InStaticMesh->LightMapCoordinateIndex, sizeof(int32));
	if(InStaticMesh->BodySetup)
	{
		// Incorporate physics data
		KeyBuffer.Append((uint8*)&InStaticMesh->BodySetup->BodySetupGuid, sizeof(FGuid));;
	}
	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

uint32 UHLODProxy::GetCRC(UStaticMeshComponent* InComponent, uint32 InCRC, const FTransform& TransformComponents)
{
	TArray<uint8> KeyBuffer;

	FVector  ComponentLocation = InComponent->GetComponentLocation();
	FRotator ComponentRotation = InComponent->GetComponentRotation();
	FVector  ComponentScale = InComponent->GetComponentScale();

	ComponentLocation = TransformComponents.TransformPosition(ComponentLocation);
	ComponentRotation = TransformComponents.TransformRotation(ComponentRotation.Quaternion()).Rotator();

	// Include transform - round sufficiently to ensure stability
	FIntVector Location(ComponentLocation / THRESH_POINTS_ARE_NEAR);
	KeyBuffer.Append((uint8*)&Location, sizeof(Location));
	FIntVector Rotation(ComponentRotation.GetNormalized().Vector() / THRESH_POINTS_ARE_NEAR);
	KeyBuffer.Append((uint8*)&Rotation, sizeof(Rotation));
	FIntVector Scale(ComponentScale / THRESH_POINTS_ARE_NEAR);
	KeyBuffer.Append((uint8*)&Scale, sizeof(Scale));	

	// Include other relevant properties
	KeyBuffer.Append((uint8*)&InComponent->ForcedLodModel, sizeof(int32));
	bool bUseMaxLODAsImposter = InComponent->bUseMaxLODAsImposter;
	KeyBuffer.Append((uint8*)&bUseMaxLODAsImposter, sizeof(bool));
	bool bCastShadow = InComponent->CastShadow;
	KeyBuffer.Append((uint8*)&bCastShadow, sizeof(bool));
	bool bCastStaticShadow = InComponent->bCastStaticShadow;
	KeyBuffer.Append((uint8*)&bCastStaticShadow, sizeof(bool));
	bool bCastDynamicShadow = InComponent->bCastDynamicShadow;
	KeyBuffer.Append((uint8*)&bCastDynamicShadow, sizeof(bool));
	bool bCastFarShadow = InComponent->bCastFarShadow;
	KeyBuffer.Append((uint8*)&bCastFarShadow, sizeof(bool));
	int32 Width, Height;
	InComponent->GetLightMapResolution(Width, Height);
	KeyBuffer.Append((uint8*)&Width, sizeof(int32));
	KeyBuffer.Append((uint8*)&Height, sizeof(int32));
	
	// incorporate vertex colors
	for(FStaticMeshComponentLODInfo& LODInfo : InComponent->LODData)
	{
		if(LODInfo.OverrideVertexColors)
		{
			KeyBuffer.Append((uint8*)LODInfo.OverrideVertexColors->GetVertexData(), LODInfo.OverrideVertexColors->GetNumVertices() * LODInfo.OverrideVertexColors->GetStride());
		}
	}

	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

// Key that forms the basis of the HLOD proxy key. Bump this key (i.e. generate a new GUID) when you want to force a rebuild of ALL HLOD proxies
#define HLOD_PROXY_BASE_KEY		TEXT("76927B120C6645ACB9200E7FB8896AC3")

FName UHLODProxy::GenerateKeyForActor(const ALODActor* LODActor, bool bMustUndoLevelTransform)
{
	FString Key = HLOD_PROXY_BASE_KEY;

	// Base us off the unique object ID
	{
		FUniqueObjectGuid ObjectID = FUniqueObjectGuid::GetOrCreateIDForObject(LODActor);
		Key += TEXT("_");
		Key += ObjectID.GetGuid().ToString(EGuidFormats::Digits);
	}

	// Accumulate a bunch of settings into a CRC
	{
		uint32 CRC = 0;

		// Get the HLOD settings CRC
		{
			TArray<FHierarchicalSimplification>& BuildLODLevelSettings = LODActor->GetLevel()->GetWorldSettings()->GetHierarchicalLODSetup();
			if(BuildLODLevelSettings.IsValidIndex(LODActor->LODLevel - 1))
			{
				FHierarchicalSimplification& BuildLODLevelSetting = BuildLODLevelSettings[LODActor->LODLevel - 1];
				CRC = FCrc::MemCrc32(&BuildLODLevelSetting, sizeof(FHierarchicalSimplification), CRC);
			}
		}

		// screen size + override
		{
			if(LODActor->bOverrideScreenSize)
			{
				CRC = FCrc::MemCrc32(&LODActor->ScreenSize, sizeof(float), CRC);
			}
		}

		// material merge settings override
		{
			if (LODActor->bOverrideMaterialMergeSettings)
			{
				CRC = FCrc::MemCrc32(&LODActor->MaterialSettings, sizeof(FMaterialProxySettings), CRC);
			}
		}

		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// get the base material CRC
	{
		UMaterialInterface* BaseMaterial = LODActor->GetLevel()->GetWorldSettings()->GetHierarchicalLODBaseMaterial();
		uint32 CRC = GetCRC(BaseMaterial);
		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// We get the CRC of the first actor name and various static mesh components
	{
		uint32 CRC = 0;
		const AActor* FirstActor = FindFirstActor(LODActor);
		if(FirstActor)
		{
			CRC = FCrc::StrCrc32(*FirstActor->GetName(), CRC);
		}

		TArray<UPrimitiveComponent*> Components;
		ExtractComponents(LODActor, Components);
		
		// Components can be offset by their streaming level transform. Undo that transform to have the same signature
		// when computing CRC for a sub level or a persistent level.
		FTransform TransformComponents = FTransform::Identity;
		if (bMustUndoLevelTransform)
		{
			ULevelStreaming* SteamingLevel = FLevelUtils::FindStreamingLevel(LODActor->GetLevel());
			if (SteamingLevel)
			{
				TransformComponents = SteamingLevel->LevelTransform.Inverse();
			}
		}

		// We get the CRC of each component and combine them
		for(UPrimitiveComponent* Component : Components)
		{
			if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				// CRC component
				CRC = GetCRC(StaticMeshComponent, CRC, TransformComponents);

				if(UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
				{
					// CRC static mesh
					CRC = GetCRC(StaticMesh, CRC);

					// CRC materials
					const int32 NumMaterials = StaticMeshComponent->GetNumMaterials();
					for(int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
					{
						UMaterialInterface* MaterialInterface = StaticMeshComponent->GetMaterial(MaterialIndex);
						if (MaterialInterface)
						{
							CRC = GetCRC(MaterialInterface, CRC);

							TArray<UTexture*> Textures;
							MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
							for (UTexture* Texture : Textures)
							{
								CRC = GetCRC(Texture, CRC);
							}
						}
					}
				}
			}
		}
		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// Mesh reduction method
	{
		// NOTE: This mimics code in the editor only FMeshReductionManagerModule::StartupModule(). If that changes then this should too
		FString HLODMeshReductionModuleName;
		GConfig->GetString(TEXT("/Script/Engine.ProxyLODMeshSimplificationSettings"), TEXT("r.ProxyLODMeshReductionModule"), HLODMeshReductionModuleName, GEngineIni);
		// If nothing was requested, default to simplygon for mesh merging reduction
		if (HLODMeshReductionModuleName.IsEmpty())
		{
			HLODMeshReductionModuleName = TEXT("SimplygonMeshReduction");
		}

		Key += TEXT("_");
		Key += HLODMeshReductionModuleName;
	}

	return FName(*Key);
}

#endif // #if WITH_EDITOR

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

bool UHLODProxy::ContainsDataForActor(const ALODActor* InLODActor) const
{
#if WITH_EDITOR
	FName Key;

	// Only re-generate the key in non-PIE worlds
	if(InLODActor->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		Key = InLODActor->GetKey();
	}
	else
	{
		Key = GenerateKeyForActor(InLODActor);
	}
#else
	FName Key = InLODActor->GetKey();
#endif

	if(Key == NAME_None)
	{
		return false;
	}

	for(const FHLODProxyMesh& ProxyMesh : ProxyMeshes)
	{
		if(ProxyMesh.GetKey() == Key)
		{
			return true;
		}
	}

	return false;
}

#endif