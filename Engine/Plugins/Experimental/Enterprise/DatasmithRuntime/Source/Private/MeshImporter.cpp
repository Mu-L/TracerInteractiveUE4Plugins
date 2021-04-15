// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "DatasmithRuntimeUtils.h"
#include "LogCategory.h"
#include "MaterialImportUtils.h"

#include "DatasmithImportOptions.h"
#include "DatasmithMeshUObject.h"
#include "DatasmithNativeTranslator.h"
#include "DatasmithPayload.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMeshHelper.h"

#include "Algo/Find.h"
#include "Algo/Sort.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/ScopeLock.h"
#include "StaticMeshAttributes.h"
#include "UObject/GarbageCollection.h"

#if WITH_EDITOR
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#endif

namespace DatasmithRuntime
{
	bool FSceneImporter::ProcessMeshData(FAssetData& MeshData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMeshData);

		// Clear PendingDelete flag if it is set. Something is wrong. Better safe than sorry
		if (MeshData.HasState(EAssetState::PendingDelete))
		{
			MeshData.ClearState(EAssetState::PendingDelete);
			UE_LOG(LogDatasmithRuntime, Warning, TEXT("A mesh marked for deletion is actually used by the scene"));
		}

		if (MeshData.HasState(EAssetState::Processed))
		{
			return true;
		}

		TSharedPtr< IDatasmithMeshElement > MeshElement = StaticCastSharedPtr< IDatasmithMeshElement >(Elements[MeshData.ElementId]);

		// If mesh file does not exist, add scene's resource path if valid
		if (!FPaths::FileExists(MeshElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
		{
			MeshElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), MeshElement->GetFile()) );
		}

		// File not found, nothing to do
		if (!FPaths::FileExists(MeshElement->GetFile()))
		{
			MeshData.Object.Reset();
			MeshData.SetState(EAssetState::Processed | EAssetState::Completed);
			return false;
		}

		const int32 MaterialSlotCount = MeshElement->GetMaterialSlotCount();

		UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>();

		// Check if geometry has not changed
		if (StaticMesh)
		{
			// Force recreation of the static mesh if the mesh's file has changed
			DirectLink::FElementHash NewResourceHash = GetTypeHash(MeshElement->GetFileHash());
			if (MeshData.ResourceHash != NewResourceHash)
			{
				FAssetRegistry::UnregisterAssetData(StaticMesh, SceneKey, MeshData.ElementId);
				StaticMesh = nullptr;
				MeshData.Object.Reset();
			}
		}

		bool bUsingStaticMeshFromCache = false;

		if (StaticMesh == nullptr)
		{
			MeshData.Hash = GetTypeHash(MeshElement->CalculateElementHash(true));
			MeshData.ResourceHash = GetTypeHash(MeshElement->GetFileHash());

			if (UObject* AssetPtr = FAssetRegistry::FindObjectFromHash(MeshData.Hash))
			{
				StaticMesh = Cast<UStaticMesh>(AssetPtr);
				check(StaticMesh);

				MeshData.Object = TWeakObjectPtr<UObject>(StaticMesh);

				bUsingStaticMeshFromCache = true;
			}
			else
			{
#ifdef ASSET_DEBUG
				FString MeshName = TEXT("S_") + FString(MeshElement->GetName()) + TEXT("_LU_") + FString::FromInt(MeshData.ElementId);
				MeshName = FDatasmithUtils::SanitizeObjectName(MeshName);
				UPackage* Package = CreatePackage(*FPaths::Combine( TEXT("/DatasmithContent/Meshes"), MeshName));
				StaticMesh = NewObject< UStaticMesh >(Package, *MeshName, RF_Public);
#else
				StaticMesh = NewObject< UStaticMesh >(GetTransientPackage());
#endif
				check(StaticMesh);

				MeshData.Object = TWeakObjectPtr< UObject >(StaticMesh);

				// Add the creation of the mesh to the queue
				FActionTaskFunction TaskFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
				{
					OnGoingTasks.Emplace( Async(
#if WITH_EDITOR
						EAsyncExecution::LargeThreadPool,
#else
						EAsyncExecution::ThreadPool,
#endif
						[this, ElementId = Referencer.GetId()]() -> bool
						{
							return this->CreateStaticMesh(ElementId);
						},
						[this]()->void
						{
							this->ActionCounter.Increment();
						}
					));

					return EActionResult::Succeeded;
				};

				AddToQueue(EQueueTask::MeshQueue, { TaskFunc, {EDataType::Mesh, MeshData.ElementId, 0 } });
				TasksToComplete |=  EWorkerTask::MeshCreate;

				MeshElementSet.Add(MeshData.ElementId);
			}
		}

		FActionTaskFunction AssignMaterialFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->AssignMaterial(Referencer, Cast<UMaterialInstanceDynamic>(Object));
		};

		TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;
		StaticMaterials.SetNum(MaterialSlotCount);

		for (int32 Index = 0; Index < MaterialSlotCount; Index++)
		{
			FStaticMaterial& StaticMaterial = StaticMaterials[Index];

			if (!bUsingStaticMeshFromCache)
			{
				StaticMaterial.MaterialSlotName = NAME_None;
				StaticMaterial.MaterialInterface = nullptr;

				// Done to remove an assert from an 'ensure' in UStaticMesh::GetUVChannelData
				StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.f);
			}

			if (const IDatasmithMaterialIDElement* MaterialIDElement = MeshElement->GetMaterialSlotAt(Index).Get())
			{
				const FString MaterialPathName(MaterialIDElement->GetName());

				if (!MaterialPathName.StartsWith(TEXT("/")))
				{
					if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialPathName))
					{
						FAssetData& MaterialData = AssetDataList[*MaterialElementIdPtr];

						ProcessMaterialData(MaterialData);

						AddToQueue(EQueueTask::NonAsyncQueue, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Mesh, MeshData.ElementId, (uint16)Index } });
						TasksToComplete |= EWorkerTask::MaterialAssign;
					}
				}
				else
				{
					StaticMaterial.MaterialInterface = Cast<UMaterialInterface>(FSoftObjectPath(MaterialPathName).TryLoad());
				}

				if (!bUsingStaticMeshFromCache)
				{
					StaticMaterial.MaterialSlotName = *FString::Printf(TEXT("%d"), MaterialIDElement->GetId());
				}
			}
		}

		// Create BodySetup in game thread to avoid allocating during a garbage collect later on
		if (StaticMesh->BodySetup == nullptr)
		{
			StaticMesh->CreateBodySetup();
		}

		MeshData.SetState(EAssetState::Processed);

		FAssetRegistry::RegisterAssetData(StaticMesh, SceneKey, MeshData);

		return true;
	}

	bool FSceneImporter::ProcessMeshActorData(FActorData& ActorData, IDatasmithMeshActorElement* MeshActorElement)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMeshActorData);

		if (ActorData.HasState(EAssetState::Processed))
		{
			return true;
		}

		// Invalid reference to a mesh. Abort creation of component
		if (FCString::Strlen(MeshActorElement->GetStaticMeshPathName()) == 0)
		{
			ActorData.SetState(EAssetState::Processed);
			return false;
		}

		FActionTaskFunction CreateComponentFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->CreateMeshComponent(Referencer.GetId(), Cast<UStaticMesh>(Object));
		};

		FString StaticMeshPathName(MeshActorElement->GetStaticMeshPathName());
		UStaticMesh* StaticMesh = nullptr;

		if (!StaticMeshPathName.StartsWith(TEXT("/")))
		{
			if (FSceneGraphId* MeshElementIdPtr = AssetElementMapping.Find(MeshPrefix + StaticMeshPathName))
			{
				FAssetData& MeshData = AssetDataList[*MeshElementIdPtr];

				if (!ProcessMeshData(MeshData))
				{
					return false;
				}

				AddToQueue(EQueueTask::NonAsyncQueue, { CreateComponentFunc, *MeshElementIdPtr, { EDataType::Actor, ActorData.ElementId, 0 } });
				TasksToComplete |= EWorkerTask::MeshComponentCreate;

				ActorData.AssetId = *MeshElementIdPtr;

				StaticMesh = MeshData.GetObject<UStaticMesh>();
			}
		}
		else
		{
			StaticMesh = Cast<UStaticMesh>(FSoftObjectPath(StaticMeshPathName).TryLoad());
		}

		// The referenced static mesh was not found. Abort creation of component
		if (StaticMesh == nullptr)
		{
			return false;
		}

		if (MeshActorElement->GetMaterialOverridesCount() > 0)
		{
			FActionTaskFunction AssignMaterialFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
			{
				return this->AssignMaterial(Referencer, Cast<UMaterialInstanceDynamic>(Object));
			};

			TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;

			TMap<FString, int32> SlotMapping;
			SlotMapping.Reserve(StaticMaterials.Num());

			for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
			{
				const FStaticMaterial& StaticMaterial = StaticMaterials[Index];

				if (StaticMaterial.MaterialSlotName != NAME_None)
				{
					SlotMapping.Add(StaticMaterial.MaterialSlotName.ToString(), Index);
				}
			}

			// #ue_datasmithruntime: Missing code to handle the case where a MaterialID's name is an asset's path

			// All the materials of the static mesh are overridden by one single material
			// Note: for that case, we assume the actor has only one override
			if (MeshActorElement->GetMaterialOverride(0)->GetId() == -1)
			{
				TSharedPtr<const IDatasmithMaterialIDElement> MaterialIDElement = MeshActorElement->GetMaterialOverride(0);

				if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
				{
					ProcessMaterialData(AssetDataList[*MaterialElementIdPtr]);

					for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
					{
						AddToQueue(EQueueTask::NonAsyncQueue, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Actor, ActorData.ElementId, (uint16)Index } });
					}

					TasksToComplete |= EWorkerTask::MaterialAssign;
				}
			}
			else
			{
				for (int32 Index = 0; Index < MeshActorElement->GetMaterialOverridesCount(); ++Index)
				{
					TSharedPtr<const IDatasmithMaterialIDElement> MaterialIDElement = MeshActorElement->GetMaterialOverride(Index);

					FString MaterialSlotName = FString::Printf(TEXT("%d"), MaterialIDElement->GetId());

					if (StaticMaterials.Num() == 0 || SlotMapping.Contains(MaterialSlotName))
					{
						if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
						{
							ProcessMaterialData(AssetDataList[*MaterialElementIdPtr]);

							// If staticmesh has no material assigned, material assignment will be queued later when the mesh component is created
							if (StaticMaterials.Num() > 0)
							{
								const int32 MaterialIndex = SlotMapping[MaterialSlotName];

								AddToQueue(EQueueTask::NonAsyncQueue, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Actor, ActorData.ElementId, (uint16)MaterialIndex } });
								TasksToComplete |= EWorkerTask::MaterialAssign;
							}
						}
					}
				}
			}
		}

		ActorData.SetState(EAssetState::Processed);

		return true;
	}

	bool FSceneImporter::CreateStaticMesh(FSceneGraphId ElementId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateStaticMesh);

		TSharedRef< IDatasmithMeshElement > MeshElement = StaticCastSharedPtr< IDatasmithMeshElement >(Elements[ElementId]).ToSharedRef();

		TFunction<bool()> MaterialRequiresAdjacency;
		MaterialRequiresAdjacency = [this, MeshElementPtr = &MeshElement.Get()]() -> bool
		{
			for (int32 Index = 0; Index < MeshElementPtr->GetMaterialSlotCount(); Index++)
			{
				if (const IDatasmithMaterialIDElement* MaterialIDElement = MeshElementPtr->GetMaterialSlotAt(Index).Get())
				{
					// #ue_datasmithruntime: Missing code to handle the case where a MaterialID's name is an asset's path
					if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
					{
						if (AssetDataList[*MaterialElementIdPtr].Requirements & EMaterialRequirements::RequiresAdjacency)
						{
							return true;
						}
					}
				}
			}

			return false;
		};

		FAssetData& MeshData = AssetDataList[ElementId];

		UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>();
		if (StaticMesh == nullptr)
		{
			ensure(false);
			return false;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FDatasmithRuntimeModel::CreateStaticMesh);

		FDatasmithMeshElementPayload MeshPayload;
		{
			FDatasmithNativeTranslator NativeTranslator;

			// Prevent GC from running while loading meshes.
			// FDatasmithNativeTranslator::LoadStaticMesh is creating UDatasmithMesh objects
			FGCScopeGuard GCGuard;

			if (!NativeTranslator.LoadStaticMesh(MeshElement, MeshPayload))
			{
				// #ueent_datasmithruntime: TODO : Update FAssetFactory
				ActionCounter.Add(MeshData.Referencers.Num());
				MeshData.Object.Reset();
				MeshData.AddState(EAssetState::Completed);

				UE_LOG(LogDatasmithRuntime, Warning, TEXT("CreateStaticMesh: Loading file %s failed. Mesh element %s has not been imported"), MeshElement->GetFile(), MeshElement->GetLabel());
				
				return true;
			}
		}

		TArray< FMeshDescription >& MeshDescriptions = MeshPayload.LodMeshes;

		// Empty mesh?
		if (MeshDescriptions.Num() == 0)
		{
			ActionCounter.Add(MeshData.Referencers.Num());
			FAssetRegistry::UnregisteredAssetsData(StaticMesh, SceneKey, [](FAssetData& AssetData) -> void
				{
					AssetData.AddState(EAssetState::Completed);
					AssetData.Object.Reset();
				});

			UE_LOG(LogDatasmithRuntime, Warning, TEXT("CreateStaticMesh: %s does not have a mesh description"), MeshElement->GetLabel());

			return true;
		}

		// #ue_datasmithruntime: Cleanup mesh descriptions
		//FDatasmithStaticMeshImporter::CleanupMeshDescriptions(MeshDescriptions);

		//#ue_datasmithruntime: Implement task to build better lightmap sizes - See Dataprep operation
		int32 MinLightmapSize = FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue(EDatasmithImportLightmapMin::LIGHTMAP_64);
		int32 MaxLightmapSize = FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue(EDatasmithImportLightmapMax::LIGHTMAP_512);

		// 4. Collisions
		ProcessCollision(StaticMesh, MeshPayload);

		// Extracted from FDatasmithStaticMeshImporter::SetupStaticMesh
#if WITH_EDITOR
		StaticMesh->SetNumSourceModels(MeshDescriptions.Num());
#endif

		for (int32 LodIndex = 0; LodIndex < MeshDescriptions.Num(); ++LodIndex)
		{
			FMeshDescription& MeshDescription = MeshDescriptions[LodIndex];

			// If the number of polygon groups in the MeshDescription is greater than the number of static materials in the static mesh
			// Add the missing polygon groups.
			FStaticMeshAttributes Attributes(MeshDescription);
			TPolygonGroupAttributesConstRef<FName> MaterialSlotNameAttribute = Attributes.GetPolygonGroupMaterialSlotNames();
			if (MeshDescription.PolygonGroups().Num() > StaticMesh->StaticMaterials.Num())
			{
				TArray<FStaticMaterial>& StaticMaterials = StaticMesh->StaticMaterials;
				int32 LastIndex = StaticMaterials.Num();

				StaticMaterials.SetNum(MeshDescription.PolygonGroups().Num());

				TSet<FName> ExistingSlotNames;
				ExistingSlotNames.Reserve(LastIndex);
				for (int32 Index = 0; Index < LastIndex; ++Index)
				{
					ExistingSlotNames.Add(StaticMaterials[Index].MaterialSlotName);
				}

				for (FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
				{
					const FName& MaterialSlotName = MaterialSlotNameAttribute[PolygonGroupID];
					if (!ExistingSlotNames.Contains(MaterialSlotName))
					{
						StaticMaterials[LastIndex].MaterialSlotName = MaterialSlotName;
						StaticMaterials[LastIndex].MaterialInterface = nullptr;
						++LastIndex;
					}
				}
			}

			// UV Channels
			int32 SourceIndex = 0;
			int32 DestinationIndex = 1;
			bool bUseImportedLightmap = false;
			bool bGenerateLightmapUVs = true /* Default value for StaticMeshImportOptions.bGenerateLightmapUVs*/;
			const int32 FirstOpenUVChannel = GetNextOpenUVChannel(MeshDescription);

			// if a custom lightmap coordinate index was imported, disable lightmap generation
			if (DatasmithMeshHelper::HasUVData(MeshDescription, MeshElement->GetLightmapCoordinateIndex()))
			{
				bUseImportedLightmap = true;
				bGenerateLightmapUVs = false;
				DestinationIndex = MeshElement->GetLightmapCoordinateIndex();
			}
			else
			{
				if (MeshElement->GetLightmapCoordinateIndex() >= 0)
				{
					UE_LOG(LogDatasmithRuntime, Error, TEXT("CreateStaticMesh: The lightmap coordinate index '%d' used for the mesh '%s' is invalid"), MeshElement->GetLightmapCoordinateIndex(), MeshElement->GetLabel());
				}

				DestinationIndex = FirstOpenUVChannel;
			}

			// Set the source lightmap index to the imported mesh data lightmap source if any, otherwise use the first open channel.
			if (DatasmithMeshHelper::HasUVData(MeshDescription, MeshElement->GetLightmapSourceUV()))
			{
				SourceIndex = MeshElement->GetLightmapSourceUV();
			}
			else
			{
				//If the lightmap source index was not set, we set it to the first open UV channel as it will be generated.
				//Also, it's okay to set both the source and the destination to be the same index as they are for different containers.
				SourceIndex = FirstOpenUVChannel;
			}

			if (bGenerateLightmapUVs)
			{
				if (!FMath::IsWithin<int32>(SourceIndex, 0, MAX_MESH_TEXTURE_COORDS_MD))
				{
					UE_LOG(LogDatasmithRuntime, Error, TEXT("CreateStaticMesh: Lightmap generation error for mesh %s: Specified source, %d, is invalid"), MeshElement->GetLabel(), MeshElement->GetLightmapSourceUV());
					bGenerateLightmapUVs = false;
				}
				else if (!FMath::IsWithin<int32>(DestinationIndex, 0, MAX_MESH_TEXTURE_COORDS_MD))
				{
					UE_LOG(LogDatasmithRuntime, Error, TEXT("CreateStaticMesh: Lightmap generation error for mesh %s: Cannot find an available destination channel."), MeshElement->GetLabel());
					bGenerateLightmapUVs = false;
				}

				if (!bGenerateLightmapUVs)
				{
					UE_LOG(LogDatasmithRuntime, Error, TEXT("CreateStaticMesh: Lightmap UVs for mesh %s won't be generated."), MeshElement->GetLabel());
				}
			}

			// We should always have some UV data in channel 0 because it is used in the mesh tangent calculation during the build.
			if (!DatasmithMeshHelper::HasUVData(MeshDescription, 0))
			{
				DatasmithMeshHelper::CreateDefaultUVs(MeshDescription);
			}

			if (bGenerateLightmapUVs && !DatasmithMeshHelper::HasUVData(MeshDescription, SourceIndex))
			{
				//If no UV data exist at the source index we generate unwrapped UVs.
				//Do this before calling DatasmithMeshHelper::CreateDefaultUVs() as the UVs may be unwrapped at channel 0.
				//UUVGenerationFlattenMapping::GenerateUVs(MeshDescription, SourceIndex, true);
				// #ue_datasmithruntime: Find runtime code to unwrap UVs
				// For the time being, just copy channel 0 to SourceIndex
				{
					TMeshAttributesRef<FVertexInstanceID, FVector2D> UVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
					if (UVs.GetNumIndices() <= SourceIndex)
					{
						UVs.SetNumIndices(SourceIndex + 1);
					}

					for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
					{
						UVs.Set(VertexInstanceID, SourceIndex, UVs.Get(VertexInstanceID, 0));
					}
				}
			}

			FVector BuildScale3D(1.0f, 1.0f, 1.0f);
#if WITH_EDITOR
			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LodIndex).BuildSettings;

			BuildSettings.bUseMikkTSpace = true;
			BuildSettings.bRecomputeNormals = ShouldRecomputeNormals(MeshDescription, MeshData.Requirements);
			BuildSettings.bRecomputeTangents = ShouldRecomputeTangents(MeshDescription, MeshData.Requirements);
			BuildSettings.bRemoveDegenerates = true /* Default value of StaticMeshImportOptions.bRemoveDegenerates */;
			BuildSettings.bUseHighPrecisionTangentBasis = true;
			BuildSettings.bUseFullPrecisionUVs = true;
			BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;
			BuildSettings.SrcLightmapIndex = SourceIndex;
			BuildSettings.DstLightmapIndex = DestinationIndex;
			BuildSettings.MinLightmapResolution = MinLightmapSize;
			BuildScale3D = BuildSettings.BuildScale3D;

			// Don't build adjacency buffer for meshes with over 500 000 triangles because it's too slow
			BuildSettings.bBuildAdjacencyBuffer = MeshDescription.Polygons().Num() < 500000 ? MaterialRequiresAdjacency() : false;
#endif
			if (DatasmithMeshHelper::IsMeshValid(MeshDescription, BuildScale3D))
			{
				if (bGenerateLightmapUVs && DatasmithMeshHelper::RequireUVChannel(MeshDescription, DestinationIndex))
				{
					GenerateLightmapUVResolution(MeshDescription, SourceIndex, MinLightmapSize);
				}
			}
		}

		TArray<const FMeshDescription*> MeshDescriptionPointers;
		for (FMeshDescription& MeshDescription : MeshDescriptions)
		{
			MeshDescriptionPointers.Add(&MeshDescription);
		}

		// #ue_datasmithruntime: Multi-threading issue with BodySetup::CreatePhysicsMeshes.
		static bool bEnableCollision = false;

		{
			FGCScopeGuard GCGuard;

			// Do not mark the package dirty since MarkPackageDirty is not thread safe
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bUseHashAsGuid = true;
			Params.bMarkPackageDirty = false;
			Params.bBuildSimpleCollision = bEnableCollision;
			// Do not commit since we only need the render data and commit is slow
			Params.bCommitMeshDescription = false;
			StaticMesh->BuildFromMeshDescriptions(MeshDescriptionPointers, Params);
		}

		// Free up memory
		MeshDescriptions.Empty();
#if WITH_EDITORONLY_DATA
		StaticMesh->ClearMeshDescriptions();
#endif

		check(StaticMesh->RenderData && StaticMesh->RenderData->IsInitialized());

		MeshData.ClearState(EAssetState::Building);
		FAssetRegistry::SetObjectCompletion(StaticMesh, true);

		return true;
	}

	EActionResult::Type FSceneImporter::CreateMeshComponent(FSceneGraphId ActorId, UStaticMesh* StaticMesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateMeshComponent);

		if (StaticMesh == nullptr)
		{
			ActionCounter.Increment();
			return EActionResult::Succeeded;
		}

		FActorData& ActorData = ActorDataList[ActorId];

		// Component has been removed, no action needed
		if (ActorData.ElementId == INDEX_NONE)
		{
			return EActionResult::Succeeded;
		}

		UStaticMeshComponent* MeshComponent = ActorData.GetObject<UStaticMeshComponent>();

		if (MeshComponent == nullptr)
		{
			MeshComponent = NewObject< UStaticMeshComponent >(RootComponent->GetOwner(), NAME_None);

			ActorData.Object = TWeakObjectPtr<UObject>(MeshComponent);

			MeshComponent->SetMobility(EComponentMobility::Movable);

			MeshComponent->AttachToComponent(RootComponent.Get(), FAttachmentTransformRules::KeepRelativeTransform);
			MeshComponent->RegisterComponentWithWorld(RootComponent->GetOwner()->GetWorld());
		}
		else
		{
			MeshComponent->MarkRenderStateDirty();
		}

		// #ueent_datasmithruntime: Enable collision after mesh component has been displayed. Can this be multi-threaded?
		MeshComponent->bAlwaysCreatePhysicsState = false;
		MeshComponent->BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);

		MeshComponent->SetStaticMesh(StaticMesh);
#ifdef ASSET_DEBUG
		StaticMesh->ClearFlags(RF_Public);
#endif

		MeshComponent->SetRelativeTransform(ActorData.WorldTransform);

		// Allocate memory or not for override materials
		TArray< UMaterialInterface* >& OverrideMaterials = MeshComponent->OverrideMaterials;
		IDatasmithMeshActorElement* MeshActorElement = static_cast<IDatasmithMeshActorElement*>(Elements[ActorData.ElementId].Get());

		// There are override materials, make sure the slots are allocated
		if (MeshActorElement->GetMaterialOverridesCount() > 0)
		{
			// Update override materials if mesh element has less materials assigned than static mesh
			if (StaticMesh->StaticMaterials.Num() > OverrideMaterials.Num())
			{
				FActionTaskFunction AssignMaterialFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
				{
					return this->AssignMaterial(Referencer, Cast<UMaterialInstanceDynamic>(Object));
				};

				TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;

				if (MeshActorElement->GetMaterialOverride(0)->GetId() == -1)
				{
					TSharedPtr<const IDatasmithMaterialIDElement> MaterialIDElement = MeshActorElement->GetMaterialOverride(0);

					if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
					{
						for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
						{
							AddToQueue(EQueueTask::NonAsyncQueue, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Actor, ActorData.ElementId, (uint16)Index } });
						}

						TasksToComplete |= EWorkerTask::MaterialAssign;
					}
				}
				else
				{
					TMap<FString, int32> SlotMapping;
					SlotMapping.Reserve(StaticMaterials.Num());

					for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
					{
						const FStaticMaterial& StaticMaterial = StaticMaterials[Index];

						if (StaticMaterial.MaterialSlotName != NAME_None)
						{
							SlotMapping.Add(StaticMaterial.MaterialSlotName.ToString(), Index);
						}
					}

					for (int32 Index = 0; Index < MeshActorElement->GetMaterialOverridesCount(); ++Index)
					{
						TSharedPtr<const IDatasmithMaterialIDElement> MaterialIDElement = MeshActorElement->GetMaterialOverride(Index);

						FString MaterialSlotName = FString::Printf(TEXT("%d"), MaterialIDElement->GetId());

						if (SlotMapping.Contains(MaterialSlotName))
						{
							if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
							{
								const int32 MaterialIndex = SlotMapping[MaterialSlotName];

								AddToQueue(EQueueTask::NonAsyncQueue, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Actor, ActorData.ElementId, (uint16)MaterialIndex } });
								TasksToComplete |= EWorkerTask::MaterialAssign;
							}
						}
					}
				}
			}

			OverrideMaterials.SetNum(StaticMesh->StaticMaterials.Num());
			for (int32 Index = 0; Index < OverrideMaterials.Num(); ++Index)
			{
				OverrideMaterials[Index] = nullptr;
			}
		}
		// No override material, discard the array if necessary
		else if (OverrideMaterials.Num() > 0)
		{
			OverrideMaterials.Empty();
		}

		if (MeshActorElement->GetTagsCount() > 0)
		{
			MeshComponent->ComponentTags.Reserve(MeshActorElement->GetTagsCount());
			for (int32 Index = 0; Index < MeshActorElement->GetTagsCount(); ++Index)
			{
				MeshComponent->ComponentTags.Add(MeshActorElement->GetTag(Index));
			}
		}

		ActorData.AddState(EAssetState::Completed);

		// Update counters
		ActionCounter.Increment();

		return EActionResult::Succeeded;
	}

	EActionResult::Type FSceneImporter::AssignMaterial(const FReferencer& Referencer, UMaterialInstanceDynamic* Material)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::AssignMaterial);

		if (Material == nullptr)
		{
			// #ue_dsruntime: Log message material not assigned
			ActionCounter.Increment();
			return EActionResult::Failed;
		}

		if (Referencer.Type == (uint8)EDataType::Mesh)
		{
			FAssetData& MeshData = AssetDataList[Referencer.GetId()];

			if (!MeshData.HasState(EAssetState::Completed))
			{
				return EActionResult::Retry;
			}

			// Static mesh can be null if creation failed
			if (UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>())
			{
				TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;

				if (!StaticMaterials.IsValidIndex(Referencer.Slot))
				{
					ensure(false);
					ActionCounter.Increment();
					return EActionResult::Failed;
				}

				StaticMaterials[Referencer.Slot].MaterialInterface = Material;

#ifdef ASSET_DEBUG
				Material->ClearFlags(RF_Public);
#endif
				// Mark dependent mesh components' render state as dirty
				for (FReferencer& ActorReferencer : MeshData.Referencers)
				{
					FActorData& ActorData = ActorDataList[ActorReferencer.GetId()];

					if (UActorComponent* ActorComponent = ActorData.GetObject<UActorComponent>())
					{
						ActorComponent->MarkRenderStateDirty();
					}
				}
			}
		}
		else if (Referencer.Type == (uint8)EDataType::Actor)
		{
			FActorData& ActorData = ActorDataList[Referencer.GetId()];

			const TCHAR* ActorLabel = Elements[ActorData.ElementId]->GetLabel();

			if (!ActorData.HasState(EAssetState::Completed))
			{
				return EActionResult::Retry;
			}

			// Static mesh can be null if creation failed
			if (UStaticMeshComponent* MeshComponent = ActorData.GetObject<UStaticMeshComponent>())
			{
				if ((int32)Referencer.Slot >= MeshComponent->GetNumMaterials())
				{
					ensure(false);
					ActionCounter.Increment();
					return EActionResult::Failed;
				}

				MeshComponent->SetMaterial(Referencer.Slot, Material);

				// Force rebuilding of render data for mesh component
				MeshComponent->MarkRenderStateDirty();
#ifdef ASSET_DEBUG
				Material->ClearFlags(RF_Public);
#endif
			}
			else
			{
				ensure(false);
				ActionCounter.Increment();
				return EActionResult::Failed;
			}
		}
		else
		{
			ensure(false);
			ActionCounter.Increment();
			return EActionResult::Failed;
		}

		ActionCounter.Increment();

		return EActionResult::Succeeded;
	}
}
