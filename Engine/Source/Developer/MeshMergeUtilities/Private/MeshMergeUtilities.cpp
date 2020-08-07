// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMergeUtilities.h"

#include "Engine/MapBuildDataRegistry.h"
#include "Engine/MeshMerging.h"

#include "MaterialOptions.h"
#include "IMaterialBakingModule.h"

#include "Misc/PackageName.h"
#include "MaterialUtilities.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/ShapeComponent.h"

#include "SkeletalMeshTypes.h"
#include "SkeletalRenderPublic.h"

#include "UObject/UObjectBaseUtility.h"
#include "UObject/Package.h"
#include "Materials/Material.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "MeshMergeData.h"
#include "IHierarchicalLODUtilities.h"
#include "Engine/MeshMergeCullingVolume.h"

#include "Landscape.h"
#include "LandscapeProxy.h"

#include "Editor.h"
#include "ProxyGenerationProcessor.h"
#include "Editor/EditorPerProjectUserSettings.h"

#include "ProxyMaterialUtilities.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshUtilities.h"
#include "ImageUtils.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"

#include "ProxyGenerationProcessor.h"
#include "IMaterialBakingAdapter.h"
#include "StaticMeshComponentAdapter.h"
#include "SkeletalMeshAdapter.h"
#include "StaticMeshAdapter.h"
#include "MeshMergeEditorExtensions.h"

#include "MeshMergeDataTracker.h"

#include "Misc/FileHelper.h"
#include "MeshMergeHelpers.h"
#include "Settings/EditorExperimentalSettings.h"
#include "MaterialBakingStructures.h"
#include "Async/ParallelFor.h"
#include "ScopedTransaction.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/LODActor.h"
#include "HierarchicalLODVolume.h"
#include "Engine/Selection.h"
#include "MaterialBakingHelpers.h"
#include "IMeshMergeExtension.h"

#include "RawMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

#include "Async/Future.h"
#include "Async/Async.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "MeshMergeUtils"

DEFINE_LOG_CATEGORY(LogMeshMerging);

FMeshMergeUtilities::FMeshMergeUtilities()
{
	Processor = new FProxyGenerationProcessor(this);

	// Add callback for registering editor extensions with Skeletal/Static mesh editor
	ModuleLoadedDelegateHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&FMeshMergeEditorExtensions::OnModulesChanged);
}

FMeshMergeUtilities::~FMeshMergeUtilities()
{
	FModuleManager::Get().OnModulesChanged().Remove(ModuleLoadedDelegateHandle);
	FMeshMergeEditorExtensions::RemoveExtenders();
}

void FMeshMergeUtilities::BakeMaterialsForComponent(TArray<TWeakObjectPtr<UObject>>& OptionObjects, IMaterialBakingAdapter* Adapter) const
{
	// Try and find material (merge) options from provided set of objects
	TWeakObjectPtr<UObject>* MaterialOptionsObject = OptionObjects.FindByPredicate([](TWeakObjectPtr<UObject> Object)
	{
		return Cast<UMaterialOptions>(Object.Get()) != nullptr;
	});

	TWeakObjectPtr<UObject>* MaterialMergeOptionsObject = OptionObjects.FindByPredicate([](TWeakObjectPtr<UObject> Object)
	{
		return Cast<UMaterialMergeOptions>(Object.Get()) != nullptr;
	});

	UMaterialOptions* MaterialOptions = MaterialOptionsObject ? Cast<UMaterialOptions>(MaterialOptionsObject->Get()) : nullptr;
	checkf(MaterialOptions, TEXT("No valid material options found"));


	UMaterialMergeOptions* MaterialMergeOptions  = MaterialMergeOptionsObject ? Cast<UMaterialMergeOptions>(MaterialMergeOptionsObject->Get()) : nullptr;

	// Mesh / LOD index	
	TMap<uint32, FMeshDescription> RawMeshLODs;

	// Unique set of sections in mesh
	TArray<FSectionInfo> UniqueSections;

	TArray<FSectionInfo> Sections;

	int32 NumLODs = Adapter->GetNumberOfLODs();

	// LOD index, <original section index, unique section index>
	TArray<TMap<int32, int32>> UniqueSectionIndexPerLOD;
	UniqueSectionIndexPerLOD.AddDefaulted(NumLODs);

	// Retrieve raw mesh data and unique sections
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		// Reset section for reuse
		Sections.SetNum(0, false);

		// Extract raw mesh data 
		const bool bProcessedLOD = MaterialOptions->LODIndices.Contains(LODIndex);
		if (bProcessedLOD)
		{
			FMeshDescription& RawMesh = RawMeshLODs.Add(LODIndex);
			FStaticMeshAttributes(RawMesh).Register();
			Adapter->RetrieveRawMeshData(LODIndex, RawMesh, MaterialOptions->bUseMeshData);
		}

		// Extract sections for given LOD index from the mesh 
		Adapter->RetrieveMeshSections(LODIndex, Sections);

		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			FSectionInfo& Section = Sections[SectionIndex];
			Section.bProcessed = bProcessedLOD;
			const int32 UniqueIndex = UniqueSections.AddUnique(Section);
			UniqueSectionIndexPerLOD[LODIndex].Emplace(SectionIndex, UniqueIndex);
		}
	}

	TArray<UMaterialInterface*> UniqueMaterials;
	TMap<UMaterialInterface*, int32> MaterialIndices;
	TMultiMap<uint32, uint32> SectionToMaterialMap;
	// Populate list of unique materials and store section mappings
	for (int32 SectionIndex = 0; SectionIndex < UniqueSections.Num(); ++SectionIndex)
	{
		FSectionInfo& Section = UniqueSections[SectionIndex];
		const int32 UniqueIndex = UniqueMaterials.AddUnique(Section.Material);
		SectionToMaterialMap.Add(UniqueIndex, SectionIndex);
	}

	TArray<bool> bMaterialUsesVertexData;
	DetermineMaterialVertexDataUsage(bMaterialUsesVertexData, UniqueMaterials, MaterialOptions);

	TArray<FMeshData> GlobalMeshSettings;
	TArray<FMaterialData> GlobalMaterialSettings;
	TArray<TMap<uint32, uint32>> OutputMaterialsMap;
	OutputMaterialsMap.AddDefaulted(NumLODs);

	for (int32 MaterialIndex = 0; MaterialIndex < UniqueMaterials.Num(); ++MaterialIndex)
	{
		UMaterialInterface* Material = UniqueMaterials[MaterialIndex];
		const bool bDoesMaterialUseVertexData = bMaterialUsesVertexData[MaterialIndex];
		// Retrieve all sections using this material 
		TArray<uint32> SectionIndices;
		SectionToMaterialMap.MultiFind(MaterialIndex, SectionIndices);

		if (MaterialOptions->bUseMeshData)
		{
			for (const int32 LODIndex : MaterialOptions->LODIndices)
			{
				FMeshData MeshSettings;
				MeshSettings.RawMeshDescription = nullptr;

				// Add material indices used for rendering out material
				for (const auto& Pair : UniqueSectionIndexPerLOD[LODIndex])
				{
					if (SectionIndices.Contains(Pair.Value))
					{
						MeshSettings.MaterialIndices.Add(Pair.Key);
					}
				}

				if (MeshSettings.MaterialIndices.Num())
				{
					// Retrieve raw mesh
					MeshSettings.RawMeshDescription = RawMeshLODs.Find(LODIndex);
					
					//Should not be using mesh data if there is no mesh
					check(MeshSettings.RawMeshDescription);

					MeshSettings.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
					const bool bUseVertexColor = FStaticMeshOperations::HasVertexColor(*(MeshSettings.RawMeshDescription));
					if (MaterialOptions->bUseSpecificUVIndex)
					{
						MeshSettings.TextureCoordinateIndex = MaterialOptions->TextureCoordinateIndex;
					}
					// if you use vertex color, we can't rely on overlapping UV channel, so use light map UV to unwrap UVs
					else if (bUseVertexColor)
					{
						MeshSettings.TextureCoordinateIndex = Adapter->LightmapUVIndex();
					}
					else
					{
						MeshSettings.TextureCoordinateIndex = 0;
					}
					
					Adapter->ApplySettings(LODIndex, MeshSettings);
					
					// In case part of the UVs is not within the 0-1 range try to use the lightmap UVs
					const bool bNeedsUniqueUVs = FMeshMergeHelpers::CheckWrappingUVs(*(MeshSettings.RawMeshDescription), MeshSettings.TextureCoordinateIndex);
					const int32 LightMapUVIndex = Adapter->LightmapUVIndex();
					
					TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = MeshSettings.RawMeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
					if (bNeedsUniqueUVs && MeshSettings.TextureCoordinateIndex != LightMapUVIndex && VertexInstanceUVs.GetNumElements() > 0 && VertexInstanceUVs.GetNumIndices() > LightMapUVIndex)
					{
						MeshSettings.TextureCoordinateIndex = LightMapUVIndex;
					}

					FMaterialData MaterialSettings;
					MaterialSettings.Material = Material;					

					// Add all user defined properties for baking out
					for (const FPropertyEntry& Entry : MaterialOptions->Properties)
					{
						if (!Entry.bUseConstantValue && Entry.Property != MP_MAX)
						{
							int32 NumTextureCoordinates;
							bool bUsesVertexData;
							Material->AnalyzeMaterialProperty(Entry.Property, NumTextureCoordinates, bUsesVertexData);

							MaterialSettings.PropertySizes.Add(Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
						}
					}

					// For each original material index add an entry to the corresponding LOD and bake output index 
					for (int32 Index : MeshSettings.MaterialIndices)
					{
						OutputMaterialsMap[LODIndex].Emplace(Index, GlobalMeshSettings.Num());
					}

					GlobalMeshSettings.Add(MeshSettings);
					GlobalMaterialSettings.Add(MaterialSettings);
				}
			}
		}
		else
		{
			// If we are not using the mesh data we aren't doing anything special, just bake out uv range
			FMeshData MeshSettings;
			for (int32 LODIndex : MaterialOptions->LODIndices)
			{
				for (const auto& Pair : UniqueSectionIndexPerLOD[LODIndex])
				{
					if (SectionIndices.Contains(Pair.Value))
					{
						MeshSettings.MaterialIndices.Add(Pair.Key);
					}
				}
			}

			if (MeshSettings.MaterialIndices.Num())
			{
				MeshSettings.RawMeshDescription = nullptr;
				MeshSettings.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
				MeshSettings.TextureCoordinateIndex = 0;

				FMaterialData MaterialSettings;
				MaterialSettings.Material = Material;

				// Add all user defined properties for baking out
				for (const FPropertyEntry& Entry : MaterialOptions->Properties)
				{
					if (!Entry.bUseConstantValue && Material->IsPropertyActive(Entry.Property) && Entry.Property != MP_MAX)
					{
						MaterialSettings.PropertySizes.Add(Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
					}
				}

				for (int32 LODIndex : MaterialOptions->LODIndices)
				{
					for (const auto& Pair : UniqueSectionIndexPerLOD[LODIndex])
					{
						if (SectionIndices.Contains(Pair.Value))
						{
							/// For each original material index add an entry to the corresponding LOD and bake output index 
							OutputMaterialsMap[LODIndex].Emplace(Pair.Key, GlobalMeshSettings.Num());
						}
					}
				}

				GlobalMeshSettings.Add(MeshSettings);
				GlobalMaterialSettings.Add(MaterialSettings);
			}
		}
	}

	TArray<FMeshData*> MeshSettingPtrs;
	for (int32 SettingsIndex = 0; SettingsIndex < GlobalMeshSettings.Num(); ++SettingsIndex)
	{
		MeshSettingPtrs.Add(&GlobalMeshSettings[SettingsIndex]);
	}

	TArray<FMaterialData*> MaterialSettingPtrs;
	for (int32 SettingsIndex = 0; SettingsIndex < GlobalMaterialSettings.Num(); ++SettingsIndex)
	{
		MaterialSettingPtrs.Add(&GlobalMaterialSettings[SettingsIndex]);
	}

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
	Module.BakeMaterials(MaterialSettingPtrs, MeshSettingPtrs, BakeOutputs);

	// Append constant properties which did not require baking out
	TArray<FColor> ConstantData;
	FIntPoint ConstantSize(1, 1);
	for (const FPropertyEntry& Entry : MaterialOptions->Properties)
	{
		if (Entry.bUseConstantValue && Entry.Property != MP_MAX)
		{
			ConstantData.SetNum(1, false);
			ConstantData[0] = FColor(Entry.ConstantValue * 255.0f, Entry.ConstantValue * 255.0f, Entry.ConstantValue * 255.0f);
			for (FBakeOutput& Ouput : BakeOutputs)
			{
				Ouput.PropertyData.Add(Entry.Property, ConstantData);
				Ouput.PropertySizes.Add(Entry.Property, ConstantSize);
			}
		}
	}

	TArray<UMaterialInterface*> NewMaterials;

	FString PackageName = Adapter->GetBaseName();

	const FGuid NameGuid = FGuid::NewGuid();
	for (int32 OutputIndex = 0; OutputIndex < BakeOutputs.Num(); ++OutputIndex)
	{
		// Create merged material asset
		FString MaterialAssetName = TEXT("M_") + FPackageName::GetShortName(PackageName) + TEXT("_") + MaterialSettingPtrs[OutputIndex]->Material->GetName() + TEXT("_") + NameGuid.ToString();
		FString MaterialPackageName = FPackageName::GetLongPackagePath(PackageName) + TEXT("/") + MaterialAssetName;

		FBakeOutput& Output = BakeOutputs[OutputIndex];
		// Optimize output 
		for (auto DataPair : Output.PropertyData)
		{
			FMaterialUtilities::OptimizeSampleArray(DataPair.Value, Output.PropertySizes[DataPair.Key]);
		}

		UMaterialInterface* Material = nullptr;

		if (Adapter->GetOuter())
		{
			Material = FMaterialUtilities::CreateProxyMaterialAndTextures(Adapter->GetOuter(), MaterialAssetName, Output, *MeshSettingPtrs[OutputIndex], *MaterialSettingPtrs[OutputIndex], MaterialOptions);
		}
		else
		{
			Material = FMaterialUtilities::CreateProxyMaterialAndTextures(MaterialPackageName, MaterialAssetName, Output, *MeshSettingPtrs[OutputIndex], *MaterialSettingPtrs[OutputIndex], MaterialOptions);
		}

		
		NewMaterials.Add(Material);
	}

	// Retrieve material indices which were not baked out and should still be part of the final asset
	TArray<int32> NonReplaceMaterialIndices;
	for (int32 MaterialIndex = 0; MaterialIndex < NewMaterials.Num(); ++MaterialIndex)
	{
		TArray<uint32> SectionIndices;
		SectionToMaterialMap.MultiFind(MaterialIndex, SectionIndices);

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			const bool bProcessedLOD = MaterialOptions->LODIndices.Contains(LODIndex);
			if (!bProcessedLOD)
			{
				for (const auto& Pair : UniqueSectionIndexPerLOD[LODIndex])
				{
					NonReplaceMaterialIndices.AddUnique(Adapter->GetMaterialIndex(LODIndex, Pair.Key));
				}
			}
		}
	}

	// Remap all baked out materials to their new material indices
	TMap<uint32, uint32> NewMaterialRemap;
	for (int32 LODIndex : MaterialOptions->LODIndices)
	{
		// Key == original section index, Value == unique material index
		for (const auto& Pair : OutputMaterialsMap[LODIndex])
		{
			int32 SetIndex = Adapter->GetMaterialIndex(LODIndex, Pair.Key);
			if (!NonReplaceMaterialIndices.Contains(SetIndex))
			{
				//TODO (Bug), need to pass the material data MaterialSlotName and ImportedMaterialSlotName. We loose all this data when baking material on skeletalmesh
				Adapter->SetMaterial(SetIndex, NewMaterials[Pair.Value]);
			}
			else
			{
				// Check if this material was  processed and a new entry already exists
				if (uint32* ExistingIndex = NewMaterialRemap.Find(Pair.Value))
				{
					Adapter->RemapMaterialIndex(LODIndex, Pair.Key, *ExistingIndex);
				}
				else
				{
					// Add new material
					const int32 NewMaterialIndex = Adapter->AddMaterial(NewMaterials[Pair.Value]);
					NewMaterialRemap.Add(Pair.Value, NewMaterialIndex);
					Adapter->RemapMaterialIndex(LODIndex, Pair.Key, NewMaterialIndex);
				}
			}
		}
	}

	Adapter->UpdateUVChannelData();
	GlobalMeshSettings.Empty();
}

void FMeshMergeUtilities::BakeMaterialsForComponent(USkeletalMeshComponent* SkeletalMeshComponent) const
{
	// Retrieve settings object
	UMaterialOptions* MaterialOptions = DuplicateObject(GetMutableDefault<UMaterialOptions>(), GetTransientPackage());
	UAssetBakeOptions* AssetOptions = GetMutableDefault<UAssetBakeOptions>();
	UMaterialMergeOptions* MergeOptions = GetMutableDefault<UMaterialMergeOptions>();
	TArray<TWeakObjectPtr<UObject>> Objects{ MergeOptions, AssetOptions, MaterialOptions };

	const int32 NumLODs = SkeletalMeshComponent->SkeletalMesh->GetLODNum();
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
	if (!Module.SetupMaterialBakeSettings(Objects, NumLODs))
	{
		return;
	}

	// Bake out materials for skeletal mesh
	FSkeletalMeshComponentAdapter Adapter(SkeletalMeshComponent);
	BakeMaterialsForComponent(Objects, &Adapter);
	SkeletalMeshComponent->MarkRenderStateDirty();
}

void FMeshMergeUtilities::BakeMaterialsForComponent(UStaticMeshComponent* StaticMeshComponent) const
{
	// Retrieve settings object
	UMaterialOptions* MaterialOptions = DuplicateObject(GetMutableDefault<UMaterialOptions>(), GetTransientPackage());
	UAssetBakeOptions* AssetOptions = GetMutableDefault<UAssetBakeOptions>();
	UMaterialMergeOptions* MergeOptions = GetMutableDefault<UMaterialMergeOptions>();
	TArray<TWeakObjectPtr<UObject>> Objects{ MergeOptions, AssetOptions, MaterialOptions };

	const int32 NumLODs = StaticMeshComponent->GetStaticMesh()->GetNumLODs();
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
	if (!Module.SetupMaterialBakeSettings(Objects, NumLODs))
	{
		return;
	}

	// Bake out materials for static mesh component
	FStaticMeshComponentAdapter Adapter(StaticMeshComponent);
	BakeMaterialsForComponent(Objects, &Adapter);
	StaticMeshComponent->MarkRenderStateDirty();
}

void FMeshMergeUtilities::BakeMaterialsForMesh(UStaticMesh* StaticMesh) const
{
	// Retrieve settings object
	UMaterialOptions* MaterialOptions = DuplicateObject(GetMutableDefault<UMaterialOptions>(), GetTransientPackage());
	UAssetBakeOptions* AssetOptions = GetMutableDefault<UAssetBakeOptions>();
	UMaterialMergeOptions* MergeOptions = GetMutableDefault<UMaterialMergeOptions>();
	TArray<TWeakObjectPtr<UObject>> Objects{ MergeOptions, AssetOptions, MaterialOptions };

	const int32 NumLODs = StaticMesh->GetNumLODs();
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
	if (!Module.SetupMaterialBakeSettings(Objects, NumLODs))
	{
		return;
	}

	// Bake out materials for static mesh asset
	FStaticMeshAdapter Adapter(StaticMesh);
	BakeMaterialsForComponent(Objects, &Adapter);
}

void FMeshMergeUtilities::DetermineMaterialVertexDataUsage(TArray<bool>& InOutMaterialUsesVertexData, const TArray<UMaterialInterface*>& UniqueMaterials, const UMaterialOptions* MaterialOptions) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DetermineMaterialVertexDataUsage);

	InOutMaterialUsesVertexData.SetNum(UniqueMaterials.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < UniqueMaterials.Num(); ++MaterialIndex)
	{
		UMaterialInterface* Material = UniqueMaterials[MaterialIndex];
		for (const FPropertyEntry& Entry : MaterialOptions->Properties)
		{
			// Don't have to check a property if the result is going to be constant anyway
			if (!Entry.bUseConstantValue && Entry.Property != MP_MAX)
			{
				int32 NumTextureCoordinates;
				bool bUsesVertexData;
				Material->AnalyzeMaterialProperty(Entry.Property, NumTextureCoordinates, bUsesVertexData);

				if (bUsesVertexData || NumTextureCoordinates > 1)
				{
					InOutMaterialUsesVertexData[MaterialIndex] = true;
					break;
				}
			}
		}
	}
}

void FMeshMergeUtilities::ConvertOutputToFlatMaterials(const TArray<FBakeOutput>& BakeOutputs, const TArray<FMaterialData>& MaterialData, TArray<FFlattenMaterial> &FlattenedMaterials) const
{
	for (int32 OutputIndex = 0; OutputIndex < BakeOutputs.Num(); ++OutputIndex)
	{
		const FBakeOutput& Output = BakeOutputs[OutputIndex];
		const FMaterialData& MaterialInfo = MaterialData[OutputIndex];

		FFlattenMaterial Material;		

		for (TPair<EMaterialProperty, FIntPoint> SizePair : Output.PropertySizes)
		{
			EFlattenMaterialProperties OldProperty = NewToOldProperty(SizePair.Key);
			Material.SetPropertySize(OldProperty, SizePair.Value);
			Material.GetPropertySamples(OldProperty).Append(Output.PropertyData[SizePair.Key]);
		}

		Material.bDitheredLODTransition = MaterialInfo.Material->IsDitheredLODTransition();
		Material.BlendMode = BLEND_Opaque;
		Material.bTwoSided = MaterialInfo.Material->IsTwoSided();
		Material.EmissiveScale = Output.EmissiveScale;

		FlattenedMaterials.Add(Material);
	}
}

void FMeshMergeUtilities::TransferOutputToFlatMaterials(const TArray<FMaterialData>& InMaterialData, TArray<FBakeOutput>& InOutBakeOutputs, TArray<FFlattenMaterial> &OutFlattenedMaterials) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::TransferOutputToFlatMaterials)

	OutFlattenedMaterials.SetNum(InOutBakeOutputs.Num());

	for (int32 OutputIndex = 0; OutputIndex < InOutBakeOutputs.Num(); ++OutputIndex)
	{
		FBakeOutput& Output = InOutBakeOutputs[OutputIndex];
		const FMaterialData& MaterialInfo = InMaterialData[OutputIndex];

		FFlattenMaterial& Material = OutFlattenedMaterials[OutputIndex];

		for (TPair<EMaterialProperty, FIntPoint> SizePair : Output.PropertySizes)
		{
			EFlattenMaterialProperties OldProperty = NewToOldProperty(SizePair.Key);
			Material.SetPropertySize(OldProperty, SizePair.Value);
			Material.GetPropertySamples(OldProperty) = MoveTemp(Output.PropertyData[SizePair.Key]);
		}

		Material.bDitheredLODTransition = MaterialInfo.Material->IsDitheredLODTransition();
		Material.BlendMode = BLEND_Opaque;
		Material.bTwoSided = MaterialInfo.Material->IsTwoSided();
		Material.EmissiveScale = Output.EmissiveScale;
	}
}

EFlattenMaterialProperties FMeshMergeUtilities::NewToOldProperty(int32 NewProperty) const
{
	const EFlattenMaterialProperties Remap[MP_Refraction] =
	{
		EFlattenMaterialProperties::Emissive,
		EFlattenMaterialProperties::Opacity,
		EFlattenMaterialProperties::OpacityMask,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::Diffuse,
		EFlattenMaterialProperties::Metallic,
		EFlattenMaterialProperties::Specular,
		EFlattenMaterialProperties::Roughness,
		EFlattenMaterialProperties::Anisotropy,
		EFlattenMaterialProperties::Normal,
		EFlattenMaterialProperties::Tangent,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::AmbientOcclusion
	};
	
	return Remap[NewProperty];
}

UMaterialOptions* FMeshMergeUtilities::PopulateMaterialOptions(const FMaterialProxySettings& MaterialSettings) const
{
	UMaterialOptions* MaterialOptions = DuplicateObject(GetMutableDefault<UMaterialOptions>(), GetTransientPackage());
	MaterialOptions->Properties.Empty();	
	MaterialOptions->TextureSize = MaterialSettings.TextureSize;
	
	const bool bCustomSizes = MaterialSettings.TextureSizingType == TextureSizingType_UseManualOverrideTextureSize;

	FPropertyEntry Property;
	PopulatePropertyEntry(MaterialSettings, MP_BaseColor, Property);
	MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_Specular, Property);
	if (MaterialSettings.bSpecularMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_Roughness, Property);
	if (MaterialSettings.bRoughnessMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_Anisotropy, Property);
	if (MaterialSettings.bAnisotropyMap)
	{
		MaterialOptions->Properties.Add(Property);
	}

	PopulatePropertyEntry(MaterialSettings, MP_Metallic, Property);
	if (MaterialSettings.bMetallicMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_Normal, Property);
	if (MaterialSettings.bNormalMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_Tangent, Property);
	if (MaterialSettings.bTangentMap)
	{
		MaterialOptions->Properties.Add(Property);
	}

	PopulatePropertyEntry(MaterialSettings, MP_Opacity, Property);
	if (MaterialSettings.bOpacityMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_OpacityMask, Property);
	if (MaterialSettings.bOpacityMaskMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_EmissiveColor, Property);
	if (MaterialSettings.bEmissiveMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry(MaterialSettings, MP_AmbientOcclusion, Property);
	if (MaterialSettings.bAmbientOcclusionMap)
		MaterialOptions->Properties.Add(Property);

	return MaterialOptions;
}

void FMeshMergeUtilities::PopulatePropertyEntry(const FMaterialProxySettings& MaterialSettings, EMaterialProperty MaterialProperty, FPropertyEntry& InOutPropertyEntry) const
{
	InOutPropertyEntry.Property = MaterialProperty;
	switch (MaterialSettings.TextureSizingType)
	{	
		/** Set property output size to unique per-property user set sizes */
		case TextureSizingType_UseManualOverrideTextureSize:
		{
			InOutPropertyEntry.bUseCustomSize = true;
			InOutPropertyEntry.CustomSize = [MaterialSettings, MaterialProperty]() -> FIntPoint
			{
				switch (MaterialProperty)
				{
					case MP_BaseColor: return MaterialSettings.DiffuseTextureSize;
					case MP_Specular: return MaterialSettings.SpecularTextureSize;
					case MP_Roughness: return MaterialSettings.RoughnessTextureSize;
					case MP_Anisotropy: return MaterialSettings.AnisotropyTextureSize;
					case MP_Metallic: return MaterialSettings.MetallicTextureSize;
					case MP_Normal: return MaterialSettings.NormalTextureSize;
					case MP_Tangent: return MaterialSettings.TangentTextureSize;
					case MP_Opacity: return MaterialSettings.OpacityTextureSize;
					case MP_OpacityMask: return MaterialSettings.OpacityMaskTextureSize;
					case MP_EmissiveColor: return MaterialSettings.EmissiveTextureSize;
					case MP_AmbientOcclusion: return MaterialSettings.AmbientOcclusionTextureSize;
					default:
					{
						checkf(false, TEXT("Invalid Material Property"));
						return FIntPoint();
					}	
				}
			}();

			break;
		}
		/** Set property output size to biased values off the TextureSize value (Normal at fullres, Diffuse at halfres, and anything else at quarter res */
		case TextureSizingType_UseAutomaticBiasedSizes:
		{
			const FIntPoint FullRes = MaterialSettings.TextureSize;
			const FIntPoint HalfRes = FIntPoint(FMath::Max(8, FullRes.X >> 1), FMath::Max(8, FullRes.Y >> 1));
			const FIntPoint QuarterRes = FIntPoint(FMath::Max(4, FullRes.X >> 2), FMath::Max(4, FullRes.Y >> 2));

			InOutPropertyEntry.bUseCustomSize = true;
			InOutPropertyEntry.CustomSize = [FullRes, HalfRes, QuarterRes, MaterialSettings, MaterialProperty]() -> FIntPoint
			{
				switch (MaterialProperty)
				{
				case MP_Normal: return FullRes;
				case MP_Tangent: return HalfRes;
				case MP_BaseColor: return HalfRes;
				case MP_Specular: return QuarterRes;
				case MP_Roughness: return QuarterRes;
				case MP_Anisotropy: return QuarterRes;
				case MP_Metallic: return QuarterRes;				
				case MP_Opacity: return QuarterRes;
				case MP_OpacityMask: return QuarterRes;
				case MP_EmissiveColor: return QuarterRes;
				case MP_AmbientOcclusion: return QuarterRes;
				default:
				{
					checkf(false, TEXT("Invalid Material Property"));
					return FIntPoint();
				}
				}
			}();

			break;
		}
 		/** Set all sizes to TextureSize */
		case TextureSizingType_UseSingleTextureSize:
		case TextureSizingType_UseSimplygonAutomaticSizing:
		{
			InOutPropertyEntry.bUseCustomSize = false;
			InOutPropertyEntry.CustomSize = MaterialSettings.TextureSize;
			break;
		}
	}
	/** Check whether or not a constant value should be used for this property */
	InOutPropertyEntry.bUseConstantValue = [MaterialSettings, MaterialProperty]() -> bool
	{
		switch (MaterialProperty)
		{
			case MP_BaseColor: return false;
			case MP_Normal: return !MaterialSettings.bNormalMap;
			case MP_Tangent: return !MaterialSettings.bTangentMap;
			case MP_Specular: return !MaterialSettings.bSpecularMap;
			case MP_Roughness: return !MaterialSettings.bRoughnessMap;
			case MP_Anisotropy: return !MaterialSettings.bAnisotropyMap;
			case MP_Metallic: return !MaterialSettings.bMetallicMap;
			case MP_Opacity: return !MaterialSettings.bOpacityMap;
			case MP_OpacityMask: return !MaterialSettings.bOpacityMaskMap;
			case MP_EmissiveColor: return !MaterialSettings.bEmissiveMap;
			case MP_AmbientOcclusion: return !MaterialSettings.bAmbientOcclusionMap;
			default:
			{
				checkf(false, TEXT("Invalid Material Property"));
				return false;
			}
		}
	}();
	/** Set the value if a constant value should be used for this property */
	InOutPropertyEntry.ConstantValue = [MaterialSettings, MaterialProperty]() -> float
	{
		switch (MaterialProperty)
		{
			case MP_BaseColor: return 1.0f;
			case MP_Normal: return 1.0f;
			case MP_Tangent: return 1.0f;
			case MP_Specular: return MaterialSettings.SpecularConstant;
			case MP_Roughness: return MaterialSettings.RoughnessConstant;
			case MP_Anisotropy: return MaterialSettings.AnisotropyConstant;
			case MP_Metallic: return MaterialSettings.MetallicConstant;
			case MP_Opacity: return MaterialSettings.OpacityConstant;
			case MP_OpacityMask: return MaterialSettings.OpacityMaskConstant;
			case MP_EmissiveColor: return 0.0f;
			case MP_AmbientOcclusion: return MaterialSettings.AmbientOcclusionConstant;
			default:
			{
				checkf(false, TEXT("Invalid Material Property"));
				return 1.0f;
			}
		}
	}();
}

void FMeshMergeUtilities::CopyTextureRect(const FColor* Src, const FIntPoint& SrcSize, FColor* Dst, const FIntPoint& DstSize, const FIntPoint& DstPos, bool bCopyOnlyMaskedPixels) const
{
	const int32 RowLength = SrcSize.X * sizeof(FColor);
	FColor* RowDst = Dst + DstSize.X*DstPos.Y;
	const FColor* RowSrc = Src;
	if(bCopyOnlyMaskedPixels)
	{
		for (int32 RowIdx = 0; RowIdx < SrcSize.Y; ++RowIdx)
		{
			for (int32 ColIdx = 0; ColIdx < SrcSize.X; ++ColIdx)
			{
				if(RowSrc[ColIdx] != FColor::Magenta)
				{
					RowDst[DstPos.X + ColIdx] = RowSrc[ColIdx];
				}
			}

			RowDst += DstSize.X;
			RowSrc += SrcSize.X;
		}
	}
	else
	{
		for (int32 RowIdx = 0; RowIdx < SrcSize.Y; ++RowIdx)
		{
			FMemory::Memcpy(RowDst + DstPos.X, RowSrc, RowLength);
			RowDst += DstSize.X;
			RowSrc += SrcSize.X;
		}
	}
}

void FMeshMergeUtilities::SetTextureRect(const FColor& ColorValue, const FIntPoint& SrcSize, FColor* Dst, const FIntPoint& DstSize, const FIntPoint& DstPos) const
{
	FColor* RowDst = Dst + DstSize.X*DstPos.Y;

	for (int32 RowIdx = 0; RowIdx < SrcSize.Y; ++RowIdx)
	{
		for (int32 ColIdx = 0; ColIdx < SrcSize.X; ++ColIdx)
		{
			RowDst[DstPos.X + ColIdx] = ColorValue;
		}

		RowDst += DstSize.X;
	}
}

FIntPoint FMeshMergeUtilities::ConditionalImageResize(const FIntPoint& SrcSize, const FIntPoint& DesiredSize, TArray<FColor>& InOutImage, bool bLinearSpace) const
{
	const int32 NumDesiredSamples = DesiredSize.X*DesiredSize.Y;
	if (InOutImage.Num() && InOutImage.Num() != NumDesiredSamples)
	{
		check(InOutImage.Num() == SrcSize.X*SrcSize.Y);
		TArray<FColor> OutImage;
		if (NumDesiredSamples > 0)
		{
			FImageUtils::ImageResize(SrcSize.X, SrcSize.Y, InOutImage, DesiredSize.X, DesiredSize.Y, OutImage, bLinearSpace);
		}
		Exchange(InOutImage, OutImage);
		return DesiredSize;
	}

	return SrcSize;
}

void FMeshMergeUtilities::MergeFlattenedMaterials(TArray<struct FFlattenMaterial>& InMaterialList, int32 InGutter, FFlattenMaterial& OutMergedMaterial, TArray<FUVOffsetScalePair>& OutUVTransforms) const
{
	OutUVTransforms.Reserve(InMaterialList.Num());

	// Fill output UV transforms with invalid values
	for (auto Material : InMaterialList)
	{

		// Invalid UV transform
		FUVOffsetScalePair UVTransform;
		UVTransform.Key = FVector2D::ZeroVector;
		UVTransform.Value = FVector2D::ZeroVector;
		OutUVTransforms.Add(UVTransform);
	}

	const int32 AtlasGridSize = FMath::CeilToInt(FMath::Sqrt(InMaterialList.Num()));
	OutMergedMaterial.EmissiveScale = FlattenEmissivescale(InMaterialList);

	for (int32 PropertyIndex = 0; PropertyIndex < (int32)EFlattenMaterialProperties::NumFlattenMaterialProperties; ++PropertyIndex)
	{
		const EFlattenMaterialProperties Property = (EFlattenMaterialProperties)PropertyIndex;
		if (OutMergedMaterial.ShouldGenerateDataForProperty(Property))
		{
			const FIntPoint AtlasTextureSize = OutMergedMaterial.GetPropertySize(Property);
			const FIntPoint ExportTextureSize = AtlasTextureSize / AtlasGridSize;
			const int32 AtlasNumSamples = AtlasTextureSize.X*AtlasTextureSize.Y;
			check(OutMergedMaterial.GetPropertySize(Property) == AtlasTextureSize);
			TArray<FColor>& Samples = OutMergedMaterial.GetPropertySamples(Property);
			Samples.SetNumUninitialized(AtlasNumSamples);

			// Fill with magenta (as we will be box blurring this later)
			for(FColor& SampleColor : Samples)
			{
				SampleColor = FColor(255, 0, 255);
			}
		}
	}

	int32 AtlasRowIdx = 0;
	int32 AtlasColIdx = 0;
	FIntPoint Gutter(InGutter, InGutter);
	FIntPoint DoubleGutter(InGutter * 2, InGutter * 2);
	FIntPoint GlobalAtlasTargetPos = Gutter;

	bool bSamplesWritten[(uint32)EFlattenMaterialProperties::NumFlattenMaterialProperties];
	FMemory::Memset(bSamplesWritten, 0);

	// Used to calculate UV transforms
	const FIntPoint GlobalAtlasTextureSize = OutMergedMaterial.GetPropertySize(EFlattenMaterialProperties::Diffuse);
	const FIntPoint GlobalExportTextureSize = (GlobalAtlasTextureSize / AtlasGridSize) - DoubleGutter;
	const FIntPoint GlobalExportEntrySize = (GlobalAtlasTextureSize / AtlasGridSize);

	// Flatten all materials and merge them into one material using texture atlases
	for (int32 MatIdx = 0; MatIdx < InMaterialList.Num(); ++MatIdx)
	{
		FFlattenMaterial& FlatMaterial = InMaterialList[MatIdx];
		OutMergedMaterial.bTwoSided |= FlatMaterial.bTwoSided;
		OutMergedMaterial.bDitheredLODTransition = FlatMaterial.bDitheredLODTransition;

		for (int32 PropertyIndex = 0; PropertyIndex < (int32)EFlattenMaterialProperties::NumFlattenMaterialProperties; ++PropertyIndex)
		{
			const EFlattenMaterialProperties Property = (EFlattenMaterialProperties)PropertyIndex;
			const FIntPoint PropertyTextureSize = OutMergedMaterial.GetPropertySize(Property);
			const int32 NumPropertySamples = PropertyTextureSize.X*PropertyTextureSize.Y;

			const FIntPoint PropertyAtlasTextureSize = (PropertyTextureSize / AtlasGridSize) - DoubleGutter;
			const FIntPoint PropertyAtlasEntrySize = (PropertyTextureSize / AtlasGridSize);
			const FIntPoint AtlasTargetPos((AtlasColIdx * PropertyAtlasEntrySize.X) + InGutter, (AtlasRowIdx * PropertyAtlasEntrySize.Y) + InGutter);
			
			if (OutMergedMaterial.ShouldGenerateDataForProperty(Property) && FlatMaterial.DoesPropertyContainData(Property))
			{
				TArray<FColor>& SourceSamples = FlatMaterial.GetPropertySamples(Property);
				TArray<FColor>& TargetSamples = OutMergedMaterial.GetPropertySamples(Property);
				if (FlatMaterial.IsPropertyConstant(Property))
				{
					SetTextureRect(SourceSamples[0], PropertyAtlasTextureSize, TargetSamples.GetData(), PropertyTextureSize, AtlasTargetPos);
				}
				else
				{
					FIntPoint PropertySize = FlatMaterial.GetPropertySize(Property);
					PropertySize = ConditionalImageResize(PropertySize, PropertyAtlasTextureSize, SourceSamples, false);
					CopyTextureRect(SourceSamples.GetData(), PropertyAtlasTextureSize, TargetSamples.GetData(), PropertyTextureSize, AtlasTargetPos);
					FlatMaterial.SetPropertySize(Property, PropertySize);
				}

				bSamplesWritten[PropertyIndex] |= true;
			}
		}

		check(OutUVTransforms.IsValidIndex(MatIdx));

		// Offset
		OutUVTransforms[MatIdx].Key = FVector2D(
			(float)GlobalAtlasTargetPos.X / GlobalAtlasTextureSize.X,
			(float)GlobalAtlasTargetPos.Y / GlobalAtlasTextureSize.Y);

		// Scale
		OutUVTransforms[MatIdx].Value = FVector2D(
			(float)GlobalExportTextureSize.X / GlobalAtlasTextureSize.X,
			(float)GlobalExportTextureSize.Y / GlobalAtlasTextureSize.Y);

		AtlasColIdx++;
		if (AtlasColIdx >= AtlasGridSize)
		{
			AtlasColIdx = 0;
			AtlasRowIdx++;
		}

		GlobalAtlasTargetPos = FIntPoint((AtlasColIdx * GlobalExportEntrySize.X) + InGutter, (AtlasRowIdx * GlobalExportEntrySize.Y) + InGutter);
	}

	// Check if some properties weren't populated with data (which means we can empty them out)
	for (int32 PropertyIndex = 0; PropertyIndex < (int32)EFlattenMaterialProperties::NumFlattenMaterialProperties; ++PropertyIndex)
	{
		EFlattenMaterialProperties Property = (EFlattenMaterialProperties)PropertyIndex;
		if (!bSamplesWritten[PropertyIndex])
		{	
			OutMergedMaterial.GetPropertySamples(Property).Empty();
			OutMergedMaterial.SetPropertySize(Property, FIntPoint(0, 0));
		}
		else
		{
			// Smear borders
			const FIntPoint PropertySize = OutMergedMaterial.GetPropertySize(Property);
			FMaterialBakingHelpers::PerformUVBorderSmear(OutMergedMaterial.GetPropertySamples(Property), PropertySize.X, PropertySize.Y);
		}
	}

}

void FMeshMergeUtilities::FlattenBinnedMaterials(TArray<struct FFlattenMaterial>& InMaterialList, const TArray<FBox2D>& InMaterialBoxes, int32 InGutter, bool bCopyOnlyMaskedPixels, FFlattenMaterial& OutMergedMaterial, TArray<FUVOffsetScalePair>& OutUVTransforms) const
{
	OutUVTransforms.AddZeroed(InMaterialList.Num());
	// Flatten emissive scale across all incoming materials
	OutMergedMaterial.EmissiveScale = FlattenEmissivescale(InMaterialList);

	// Merge all material properties
	for (int32 Index = 0; Index < (int32)EFlattenMaterialProperties::NumFlattenMaterialProperties; ++Index)
	{
		const EFlattenMaterialProperties Property = (EFlattenMaterialProperties)Index;
		const FIntPoint& OutTextureSize = OutMergedMaterial.GetPropertySize(Property);
		if (OutTextureSize != FIntPoint::ZeroValue)
		{
			TArray<FColor>& OutSamples = OutMergedMaterial.GetPropertySamples(Property);
			OutSamples.Reserve(OutTextureSize.X * OutTextureSize.Y);
			OutSamples.SetNumUninitialized(OutTextureSize.X * OutTextureSize.Y);

			// Fill with magenta (as we will be box blurring this later)
			for(FColor& SampleColor : OutSamples)
			{
				SampleColor = FColor(255, 0, 255);
			}

			FVector2D Gutter2D((float)InGutter, (float)InGutter);
			bool bMaterialsWritten = false;
			for (int32 MaterialIndex = 0; MaterialIndex < InMaterialList.Num(); ++MaterialIndex)
			{
				// Determine output size and offset
				FFlattenMaterial& FlatMaterial = InMaterialList[MaterialIndex];
				OutMergedMaterial.bDitheredLODTransition |= FlatMaterial.bDitheredLODTransition;
				OutMergedMaterial.bTwoSided |= FlatMaterial.bTwoSided;

				if (FlatMaterial.DoesPropertyContainData(Property))
				{
					const FBox2D MaterialBox = InMaterialBoxes[MaterialIndex];
					const FIntPoint& InputSize = FlatMaterial.GetPropertySize(Property);
					TArray<FColor>& InputSamples = FlatMaterial.GetPropertySamples(Property);

					// Resize material to match output (area) size
					FIntPoint OutputSize = FIntPoint((OutTextureSize.X * MaterialBox.GetSize().X) - (InGutter * 2), (OutTextureSize.Y * MaterialBox.GetSize().Y) - (InGutter * 2));
					ConditionalImageResize(InputSize, OutputSize, InputSamples, false);

					// Copy material data to the merged 'atlas' texture
					FIntPoint OutputPosition = FIntPoint((OutTextureSize.X * MaterialBox.Min.X) + InGutter, (OutTextureSize.Y * MaterialBox.Min.Y) + InGutter);
					CopyTextureRect(InputSamples.GetData(), OutputSize, OutSamples.GetData(), OutTextureSize, OutputPosition, bCopyOnlyMaskedPixels);

					// Set the UV tranforms only once
					if (Index == 0)
					{
						FUVOffsetScalePair& UVTransform = OutUVTransforms[MaterialIndex];
						UVTransform.Key = MaterialBox.Min + (Gutter2D / FVector2D(OutTextureSize));
						UVTransform.Value = MaterialBox.GetSize() - ((Gutter2D * 2.0f) / FVector2D(OutTextureSize));
					}

					bMaterialsWritten = true;
				}
			}

			if (!bMaterialsWritten)
			{
				OutSamples.Empty();
				OutMergedMaterial.SetPropertySize(Property, FIntPoint(0, 0));
			}
			else
			{
				// Smear borders
				const FIntPoint PropertySize = OutMergedMaterial.GetPropertySize(Property);
				FMaterialBakingHelpers::PerformUVBorderSmear(OutSamples, PropertySize.X, PropertySize.Y);
			}
		}
	}
}


float FMeshMergeUtilities::FlattenEmissivescale(TArray<struct FFlattenMaterial>& InMaterialList) const
{
	// Find maximum emissive scaling value across materials
	float MaxScale = 0.0f;
	for (const FFlattenMaterial& Material : InMaterialList)
	{
		MaxScale = FMath::Max(MaxScale, Material.EmissiveScale);
	}
	
	// Renormalize samples 
	const float Multiplier = 1.0f / MaxScale;
	const int32 NumThreads = [&]()
	{
		return FPlatformProcess::SupportsMultithreading() ? FPlatformMisc::NumberOfCores() : 1;
	}();

	const int32 MaterialsPerThread = FMath::CeilToInt((float)InMaterialList.Num() / (float)NumThreads);
	ParallelFor(NumThreads, [&InMaterialList, MaterialsPerThread, Multiplier, MaxScale]
	(int32 Index)
	{
		int32 StartIndex = FMath::CeilToInt((Index)* MaterialsPerThread);
		const int32 EndIndex = FMath::Min(FMath::CeilToInt((Index + 1) * MaterialsPerThread), InMaterialList.Num());

		for (; StartIndex < EndIndex; ++StartIndex)
		{
			FFlattenMaterial& Material = InMaterialList[StartIndex];
			if (Material.EmissiveScale != MaxScale)
			{
				for (FColor& Sample : Material.GetPropertySamples(EFlattenMaterialProperties::Emissive))
				{
					if (Sample != FColor::Magenta)
					{
						Sample.R = Sample.R * Multiplier;
						Sample.G = Sample.G * Multiplier;
						Sample.B = Sample.B * Multiplier;
						Sample.A = Sample.A * Multiplier;
					}
				}
			}
		}
	}, NumThreads == 1);

	return MaxScale;
}

void FMeshMergeUtilities::CreateProxyMesh(const TArray<AActor*>& InActors, const struct FMeshProxySettings& InMeshProxySettings, UPackage* InOuter, const FString& InProxyBasePackageName, const FGuid InGuid, const FCreateProxyDelegate& InProxyCreatedDelegate, const bool bAllowAsync, const float ScreenSize) const
{
	UMaterial* BaseMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/BaseFlattenMaterial.BaseFlattenMaterial"), NULL, LOAD_None, NULL);
	check(BaseMaterial);
	CreateProxyMesh(InActors, InMeshProxySettings, BaseMaterial, InOuter, InProxyBasePackageName, InGuid, InProxyCreatedDelegate, bAllowAsync, ScreenSize);
}

void FMeshMergeUtilities::CreateProxyMesh(const TArray<UStaticMeshComponent*>& InStaticMeshComps, const struct FMeshProxySettings& InMeshProxySettings, UPackage* InOuter, const FString& InProxyBasePackageName, const FGuid InGuid, const FCreateProxyDelegate& InProxyCreatedDelegate, const bool bAllowAsync, const float ScreenSize) const
{
	UMaterial* BaseMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/BaseFlattenMaterial.BaseFlattenMaterial"), NULL, LOAD_None, NULL);
	check(BaseMaterial);
	CreateProxyMesh(InStaticMeshComps, InMeshProxySettings, BaseMaterial, InOuter, InProxyBasePackageName, InGuid, InProxyCreatedDelegate, bAllowAsync, ScreenSize);
}

void FMeshMergeUtilities::CreateProxyMesh(const TArray<AActor*>& InActors, const struct FMeshProxySettings& InMeshProxySettings, UMaterialInterface* InBaseMaterial, UPackage* InOuter, const FString& InProxyBasePackageName, const FGuid InGuid, const FCreateProxyDelegate& InProxyCreatedDelegate, const bool bAllowAsync /*= false*/, const float ScreenSize /*= 1.0f*/) const
{
	// No actors given as input
	if (InActors.Num() == 0)
	{
		UE_LOG(LogMeshMerging, Log, TEXT("No actors specified to generate a proxy mesh for"));
		return;
	}

	// Collect components to merge
	TArray<UStaticMeshComponent*> ComponentsToMerge;
	for (AActor* Actor : InActors)
	{
		TInlineComponentArray<UStaticMeshComponent*> Components;
		Actor->GetComponents<UStaticMeshComponent>(Components);
		ComponentsToMerge.Append(Components);
	}

	CreateProxyMesh(ComponentsToMerge, InMeshProxySettings, InBaseMaterial, InOuter, InProxyBasePackageName, InGuid, InProxyCreatedDelegate, bAllowAsync, ScreenSize);
}

void FMeshMergeUtilities::CreateProxyMesh(const TArray<UStaticMeshComponent*>& InComponentsToMerge, const struct FMeshProxySettings& InMeshProxySettings, UMaterialInterface* InBaseMaterial,
	UPackage* InOuter, const FString& InProxyBasePackageName, const FGuid InGuid, const FCreateProxyDelegate& InProxyCreatedDelegate, const bool bAllowAsync, const float ScreenSize) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::CreateProxyMesh)

	// The MeshReductionInterface manages the choice mesh reduction plugins, Unreal native vs third party (e.g. Simplygon)
	IMeshReductionModule& ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionModule>("MeshReductionInterface");

	// Error/warning checking for input
	if (ReductionModule.GetMeshMergingInterface() == nullptr)
	{
		UE_LOG(LogMeshMerging, Log, TEXT("No automatic mesh merging module available"));
		return;
	}

	// Check that the delegate has a func-ptr bound to it
	if (!InProxyCreatedDelegate.IsBound())
	{
		UE_LOG(LogMeshMerging, Log, TEXT("Invalid (unbound) delegate for returning generated proxy mesh"));
		return;
	}

	TArray<UStaticMeshComponent*> ComponentsToMerge = InComponentsToMerge;

	// Remove invalid components
	ComponentsToMerge.RemoveAll([](UStaticMeshComponent* Val) { return Val->GetStaticMesh() == nullptr; });

	// No actors given as input
	if (ComponentsToMerge.Num() == 0)
	{
		UE_LOG(LogMeshMerging, Log, TEXT("No static mesh specified to generate a proxy mesh for"));
		
		TArray<UObject*> OutAssetsToSync;
		InProxyCreatedDelegate.ExecuteIfBound(InGuid, OutAssetsToSync);

		return;
	}

	// Base asset name for a new assets
	// In case outer is null ProxyBasePackageName has to be long package name
	if (InOuter == nullptr && FPackageName::IsShortPackageName(InProxyBasePackageName))
	{
		UE_LOG(LogMeshMerging, Warning, TEXT("Invalid long package name: '%s'."), *InProxyBasePackageName);
		return;
	}

	FScopedSlowTask SlowTask(100.f, (LOCTEXT("CreateProxyMesh_CreateMesh", "Creating Mesh Proxy")));
	SlowTask.MakeDialog();

	TArray<FRawMeshExt> SourceMeshes;
	TMap<FMeshIdAndLOD, TArray<int32>> GlobalMaterialMap;
	static const int32 ProxyMeshTargetLODLevel = 0;

	FBoxSphereBounds EstimatedBounds(ForceInitToZero);
	for (const UStaticMeshComponent* StaticMeshComponent : ComponentsToMerge)
	{
		EstimatedBounds = EstimatedBounds + StaticMeshComponent->Bounds;
	}

	static const float FOVRad = FMath::DegreesToRadians(45.0f);

	static const FMatrix ProjectionMatrix = FPerspectiveMatrix(FOVRad, 1920, 1080, 0.01f);
	FHierarchicalLODUtilitiesModule& HLODModule = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = HLODModule.GetUtilities();
	float EstimatedDistance = Utilities->CalculateDrawDistanceFromScreenSize(EstimatedBounds.SphereRadius, ScreenSize, ProjectionMatrix);

	SlowTask.EnterProgressFrame(5.0f, LOCTEXT("CreateProxyMesh_CollectingMeshes", "Collecting Input Static Meshes"));

	// Mesh / LOD index	
	TMap<uint32, FMeshDescription*> RawMeshLODs;

	// Mesh index, <original section index, unique section index>
	TMultiMap<uint32, TPair<uint32, uint32>> MeshSectionToUniqueSection;

	// Unique set of sections in mesh
	TArray<FSectionInfo> UniqueSections;
	TMultiMap<uint32, uint32> SectionToMesh;

	// Copies of mesh data
	TArray<FMeshDescription*> MeshDescriptionData;
	MeshDescriptionData.SetNum(ComponentsToMerge.Num());

	TArray<const UStaticMeshComponent*> ImposterMeshComponents;
	ImposterMeshComponents.SetNum(ComponentsToMerge.Num());

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	StaticMeshComponents.SetNum(ComponentsToMerge.Num());

	TAtomic<int32>  SummedLightmapPixels(0);
	TAtomic<uint32> ImposterMeshComponentsIndex(0);
	TAtomic<uint32> StaticMeshComponentsIndex(0);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::MeshGathering);

		TArray<TArray<FSectionInfo>> GlobalSections;
		GlobalSections.SetNum(ComponentsToMerge.Num());

		ParallelFor(
			ComponentsToMerge.Num(),
			[
				&ComponentsToMerge,
				&ImposterMeshComponents,
				&StaticMeshComponents,
				&ImposterMeshComponentsIndex,
				&StaticMeshComponentsIndex,
				&Utilities,
				&EstimatedDistance,
				&InMeshProxySettings,
				&MeshDescriptionData,
				&GlobalSections,
				&SummedLightmapPixels
			](uint32 Index)
			{
				UStaticMeshComponent* StaticMeshComponent = ComponentsToMerge[Index];

				int32 NumInstances = 1;
				if (StaticMeshComponent->bUseMaxLODAsImposter)
				{
					ImposterMeshComponents[ImposterMeshComponentsIndex++] = StaticMeshComponent;
				}
				else
				{
					const int32 MeshIndex = StaticMeshComponentsIndex++;
					StaticMeshComponents[MeshIndex] = StaticMeshComponent;

					const int32 ScreenSizeBasedLODLevel = Utilities->GetLODLevelForScreenSize(StaticMeshComponent, Utilities->CalculateScreenSizeFromDrawDistance(StaticMeshComponent->Bounds.SphereRadius, ProjectionMatrix, EstimatedDistance));
					const int32 LODIndex = InMeshProxySettings.bCalculateCorrectLODModel ? ScreenSizeBasedLODLevel : 0;
					static const bool bPropagateVertexColours = true;

					// Retrieve mesh data in FMeshDescription form
					MeshDescriptionData[MeshIndex] = new FMeshDescription();
					FMeshDescription& MeshDescription = *MeshDescriptionData[MeshIndex];
					FStaticMeshAttributes(MeshDescription).Register();
					FMeshMergeHelpers::RetrieveMesh(StaticMeshComponent, LODIndex, MeshDescription, bPropagateVertexColours);

					TArray<FSectionInfo>& Sections = GlobalSections[MeshIndex];

					// Extract sections for given LOD index from the mesh 
					FMeshMergeHelpers::ExtractSections(StaticMeshComponent, LODIndex, Sections);

					// If the component is an ISMC then we need to duplicate the vertex data
					if(StaticMeshComponent->IsA<UInstancedStaticMeshComponent>())
					{
						const UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(StaticMeshComponent);
						FMeshMergeHelpers::ExpandInstances(InstancedStaticMeshComponent, MeshDescription, Sections);
						NumInstances = InstancedStaticMeshComponent->PerInstanceSMData.Num();
					}
				}	

				int32 LightMapWidth, LightMapHeight;
				StaticMeshComponent->GetLightMapResolution(LightMapWidth, LightMapHeight);
				// Make sure we at least have some lightmap space allocated in case the static mesh is set up with invalid input
				SummedLightmapPixels += FMath::Max(16, LightMapHeight * LightMapWidth * NumInstances);
			},
			EParallelForFlags::Unbalanced
		);

		ImposterMeshComponents.SetNum(ImposterMeshComponentsIndex);
		StaticMeshComponents.SetNum(StaticMeshComponentsIndex);
		MeshDescriptionData.SetNum(StaticMeshComponentsIndex);

		for (uint32 MeshIndex = 0; MeshIndex < StaticMeshComponentsIndex; ++MeshIndex)
		{
			TArray<FSectionInfo>& Sections = GlobalSections[MeshIndex];

			for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
			{
				FSectionInfo& Section = Sections[SectionIndex];

				const int32 UniqueIndex = UniqueSections.AddUnique(Section);
				MeshSectionToUniqueSection.Add(MeshIndex, TPair<uint32, uint32>(SectionIndex, UniqueIndex));
				SectionToMesh.Add(UniqueIndex, MeshIndex);
			}
		}
	}

	TArray<UMaterialInterface*> UniqueMaterials;
	//Unique material index to unique section index
	TMultiMap<uint32, uint32> MaterialToSectionMap;
	for (int32 SectionIndex = 0; SectionIndex < UniqueSections.Num(); ++SectionIndex)
	{
		FSectionInfo& Section = UniqueSections[SectionIndex];
		const int32 UniqueIndex = UniqueMaterials.AddUnique(Section.Material);
		MaterialToSectionMap.Add(UniqueIndex, SectionIndex);
	}

	TArray<FMeshData> GlobalMeshSettings;
	TArray<FMaterialData> GlobalMaterialSettings;

	UMaterialOptions* Options = PopulateMaterialOptions(InMeshProxySettings.MaterialSettings);
	TArray<EMaterialProperty> MaterialProperties;
	for (const FPropertyEntry& Entry : Options->Properties)
	{
		if (Entry.Property != MP_MAX)
		{
			MaterialProperties.Add(Entry.Property);
		}
	}

	// Mesh index / ( Mesh relative section index / output index )	
	TMultiMap<uint32, TPair<uint32, uint32>> OutputMaterialsMap;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::MaterialAnalysisAndUVGathering);

		TArray<TFunction<void ()>> Lambdas;
		for (int32 MaterialIndex = 0; MaterialIndex < UniqueMaterials.Num(); ++MaterialIndex)
		{
			UMaterialInterface* Material = UniqueMaterials[MaterialIndex];

			//Unique section indices
			TArray<uint32> SectionIndices;
			MaterialToSectionMap.MultiFind(MaterialIndex, SectionIndices);

			// Check whether or not this material requires mesh data
			int32 NumTexCoords = 0;
			bool bUseVertexData = false;
			FMaterialUtilities::AnalyzeMaterial(Material, MaterialProperties, NumTexCoords, bUseVertexData);

			FMaterialData MaterialSettings;
			MaterialSettings.Material = Material;

			for (const FPropertyEntry& Entry : Options->Properties)
			{
				if (!Entry.bUseConstantValue && Material->IsPropertyActive(Entry.Property) && Entry.Property != MP_MAX)
				{
					MaterialSettings.PropertySizes.Add(Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : Options->TextureSize);
				}
			}

			if (bUseVertexData || NumTexCoords != 0)
			{
				for (uint32 SectionIndex : SectionIndices)
				{
					TArray<uint32> MeshIndices;
					SectionToMesh.MultiFind(SectionIndex, MeshIndices);

					for (const uint32 MeshIndex : MeshIndices)
					{
						// Retrieve mesh description
						const UStaticMeshComponent* StaticMeshComponent = StaticMeshComponents[MeshIndex];
						FMeshDescription* MeshDescription = MeshDescriptionData[MeshIndex];

						FMeshData MeshSettings;
						MeshSettings.RawMeshDescription = MeshDescription;

						TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

						// If we already have lightmap uvs generated and they are valid, we can reuse those instead of having to generate new ones
						const int32 LightMapCoordinateIndex = StaticMeshComponent->GetStaticMesh()->LightMapCoordinateIndex;
						if (InMeshProxySettings.bReuseMeshLightmapUVs &&
							LightMapCoordinateIndex > 0 &&
							VertexInstanceUVs.GetNumElements() > 0 &&
							VertexInstanceUVs.GetNumIndices() > LightMapCoordinateIndex)
						{
							MeshSettings.CustomTextureCoordinates.Reset(VertexInstanceUVs.GetNumElements());
							for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
							{
								MeshSettings.CustomTextureCoordinates.Add(VertexInstanceUVs.Get(VertexInstanceID, LightMapCoordinateIndex));
							}
							ScaleTextureCoordinatesToBox(FBox2D(FVector2D::ZeroVector, FVector2D(1, 1)), MeshSettings.CustomTextureCoordinates);
						}
						else
						{
							// Accumulate slow running tasks to process them in parallel once the arrays
							// are finished being resized.
							Lambdas.Emplace(
								[this, GlobalMeshSettingsIndex = GlobalMeshSettings.Num(), &GlobalMeshSettings, MeshDescription, Options]()
								{
									FMeshData& MeshSettings = GlobalMeshSettings[GlobalMeshSettingsIndex];
									// Generate unique UVs for mesh (should only be done if needed)
									FStaticMeshOperations::GenerateUniqueUVsForStaticMesh(*MeshDescription, Options->TextureSize.GetMax(), false, MeshSettings.CustomTextureCoordinates);
									ScaleTextureCoordinatesToBox(FBox2D(FVector2D::ZeroVector, FVector2D(1, 1)), MeshSettings.CustomTextureCoordinates);
									MeshSettings.TextureCoordinateBox = FBox2D(MeshSettings.CustomTextureCoordinates);
								}
							);
						}
						
						MeshSettings.TextureCoordinateBox = FBox2D(MeshSettings.CustomTextureCoordinates);

						// Section index is a unique one so we need to map it to the mesh's equivalent(s)
						TArray<TPair<uint32, uint32>> SectionToUniqueSectionIndices;
						MeshSectionToUniqueSection.MultiFind(MeshIndex, SectionToUniqueSectionIndices);
						for (const TPair<uint32, uint32> IndexPair : SectionToUniqueSectionIndices)
						{
							if (IndexPair.Value == SectionIndex)
							{
								MeshSettings.MaterialIndices.Add(IndexPair.Key);
								OutputMaterialsMap.Add(MeshIndex, TPair<uint32, uint32>(IndexPair.Key, GlobalMeshSettings.Num()));
							}
						}

						// Retrieve lightmap for usage of lightmap data
						if (StaticMeshComponent->LODData.IsValidIndex(0))
						{
							const FStaticMeshComponentLODInfo& ComponentLODInfo = StaticMeshComponent->LODData[0];
							const FMeshMapBuildData* MeshMapBuildData = StaticMeshComponent->GetMeshMapBuildData(ComponentLODInfo);
							if (MeshMapBuildData)
							{
								MeshSettings.LightMap = MeshMapBuildData->LightMap;
								MeshSettings.LightMapIndex = StaticMeshComponent->GetStaticMesh()->LightMapCoordinateIndex;
							}
						}

						GlobalMeshSettings.Add(MeshSettings);
						GlobalMaterialSettings.Add(MaterialSettings);
					}
				}
			}
			else
			{
				// Add simple bake entry 
				FMeshData MeshSettings;
				MeshSettings.RawMeshDescription = nullptr;
				MeshSettings.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
				MeshSettings.TextureCoordinateIndex = 0;

				// For each original material index add an entry to the corresponding LOD and bake output index 
				for (uint32 SectionIndex : SectionIndices)
				{
					TArray<uint32> MeshIndices;
					SectionToMesh.MultiFind(SectionIndex, MeshIndices);

					for (uint32 MeshIndex : MeshIndices)
					{
						TArray<TPair<uint32, uint32>> SectionToUniqueSectionIndices;
						MeshSectionToUniqueSection.MultiFind(MeshIndex, SectionToUniqueSectionIndices);
						for (const TPair<uint32, uint32> IndexPair : SectionToUniqueSectionIndices)
						{
							if (IndexPair.Value == SectionIndex)
							{
								OutputMaterialsMap.Add(MeshIndex, TPair<uint32, uint32>(IndexPair.Key, GlobalMeshSettings.Num()));
							}
						}
					}
				}
				
				GlobalMeshSettings.Add(MeshSettings);
				GlobalMaterialSettings.Add(MaterialSettings);
			}
		}

		if (Lambdas.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GenerateUVs);
			ParallelFor(
				Lambdas.Num(),
				[&Lambdas](uint32 Index)
				{
					Lambdas[Index]();
				},
				EParallelForFlags::Unbalanced
			);
		}
	}

	TArray<FFlattenMaterial> FlattenedMaterials;
	IMaterialBakingModule& MaterialBakingModule = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");

	auto MaterialFlattenLambda =
		[this, &Options, &GlobalMeshSettings, &GlobalMaterialSettings, &MeshDescriptionData, &OutputMaterialsMap, &MaterialBakingModule](TArray<FFlattenMaterial>& FlattenedMaterialArray)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MaterialFlatten)

		TArray<FMeshData*> MeshSettingPtrs;
		for (int32 SettingsIndex = 0; SettingsIndex < GlobalMeshSettings.Num(); ++SettingsIndex)
		{
			MeshSettingPtrs.Add(&GlobalMeshSettings[SettingsIndex]);
		}

		TArray<FMaterialData*> MaterialSettingPtrs;
		for (int32 SettingsIndex = 0; SettingsIndex < GlobalMaterialSettings.Num(); ++SettingsIndex)
		{
			MaterialSettingPtrs.Add(&GlobalMaterialSettings[SettingsIndex]);
		}

		// This scope ensures BakeOutputs is never used after TransferOutputToFlatMaterials
		{
			TArray<FBakeOutput> BakeOutputs;
			MaterialBakingModule.BakeMaterials(MaterialSettingPtrs, MeshSettingPtrs, BakeOutputs);

			// Append constant properties ?
			TArray<FColor> ConstantData;
			FIntPoint ConstantSize(1, 1);
			for (const FPropertyEntry& Entry : Options->Properties)
			{
				if (Entry.bUseConstantValue && Entry.Property != MP_MAX)
				{
					ConstantData.SetNum(1, false);
					ConstantData[0] = FColor(Entry.ConstantValue * 255.0f, Entry.ConstantValue * 255.0f, Entry.ConstantValue * 255.0f);
					for (FBakeOutput& Output : BakeOutputs)
					{
						Output.PropertyData.Add(Entry.Property, ConstantData);
						Output.PropertySizes.Add(Entry.Property, ConstantSize);
					}
				}
			}

			TransferOutputToFlatMaterials(GlobalMaterialSettings, BakeOutputs, FlattenedMaterialArray);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RemapBakedMaterials)

			// Now have the baked out material data, need to have a map or actually remap the raw mesh data to baked material indices
			for (int32 MeshIndex = 0; MeshIndex < MeshDescriptionData.Num(); ++MeshIndex)
			{
				FMeshDescription& MeshDescription = *MeshDescriptionData[MeshIndex];

				TArray<TPair<uint32, uint32>> SectionAndOutputIndices;
				OutputMaterialsMap.MultiFind(MeshIndex, SectionAndOutputIndices);
				TArray<int32> Remap;
				// Reorder loops
				for (const TPair<uint32, uint32>& IndexPair : SectionAndOutputIndices)
				{
					const int32 SectionIndex = IndexPair.Key;
					const int32 NewIndex = IndexPair.Value;

					if (Remap.Num() < (SectionIndex + 1))
					{
						Remap.SetNum(SectionIndex + 1);
					}

					Remap[SectionIndex] = NewIndex;
				}
			
				TMap<FPolygonGroupID, FPolygonGroupID> RemapPolygonGroup;
				for (const FPolygonGroupID& PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
				{
					checkf(Remap.IsValidIndex(PolygonGroupID.GetValue()), TEXT("Missing material bake output index entry for mesh(section)"));
					int32 RemapID = Remap[PolygonGroupID.GetValue()];
					RemapPolygonGroup.Add(PolygonGroupID, FPolygonGroupID(RemapID));
				}
				MeshDescription.RemapPolygonGroups(RemapPolygonGroup);
			}
		}
	};

	// Landscape culling.  NB these are temporary copies of the culling data and should be deleted after use.
	TArray<FMeshDescription*> CullingRawMeshes;
	if (InMeshProxySettings.bUseLandscapeCulling)
	{
		SlowTask.EnterProgressFrame(5.0f, LOCTEXT("CreateProxyMesh_LandscapeCulling", "Applying Landscape Culling"));
		UWorld* InWorld = ComponentsToMerge[0]->GetWorld();
		FMeshMergeHelpers::RetrieveCullingLandscapeAndVolumes(InWorld, EstimatedBounds, InMeshProxySettings.LandscapeCullingPrecision, CullingRawMeshes);
	}

	// Allocate merge complete data
	FMergeCompleteData* Data = new FMergeCompleteData();
	Data->InOuter = InOuter;
	Data->InProxySettings = InMeshProxySettings;
	Data->ProxyBasePackageName = InProxyBasePackageName;
	Data->CallbackDelegate = InProxyCreatedDelegate;
	Data->ImposterComponents = ImposterMeshComponents;
	Data->StaticMeshComponents = StaticMeshComponents;
	Data->BaseMaterial = InBaseMaterial;

	// Lightmap resolution
	if (InMeshProxySettings.bComputeLightMapResolution)
	{
		Data->InProxySettings.LightMapResolution = FMath::CeilToInt(FMath::Sqrt(SummedLightmapPixels));
	}

	// Add this proxy job to map	
	Processor->AddProxyJob(InGuid, Data);

	// We are only using LOD level 0 (ProxyMeshTargetLODLevel)
	TArray<FMeshMergeData> MergeDataEntries;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MergeDataPreparation)

		for (int32 Index = 0; Index < MeshDescriptionData.Num(); ++Index)
		{
			FMeshMergeData MergeData;
			MergeData.SourceStaticMesh = StaticMeshComponents[Index]->GetStaticMesh();
			MergeData.RawMesh = MeshDescriptionData[Index];
			MergeData.bIsClippingMesh = false;

			FMeshMergeHelpers::CalculateTextureCoordinateBoundsForRawMesh(*MergeData.RawMesh, MergeData.TexCoordBounds);

			FMeshData* MeshData = GlobalMeshSettings.FindByPredicate([&](const FMeshData& Entry)
			{
				return Entry.RawMeshDescription == MergeData.RawMesh && (Entry.CustomTextureCoordinates.Num() || Entry.TextureCoordinateIndex != 0);
			});

			if (MeshData)
			{
				if (MeshData->CustomTextureCoordinates.Num())
				{
					MergeData.NewUVs = MeshData->CustomTextureCoordinates;
				}
				else
				{
					TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshData->RawMeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
					MergeData.NewUVs.Reset(MeshData->RawMeshDescription->VertexInstances().Num());
					for (const FVertexInstanceID VertexInstanceID : MeshData->RawMeshDescription->VertexInstances().GetElementIDs())
					{
						MergeData.NewUVs.Add(VertexInstanceUVs.Get(VertexInstanceID, MeshData->TextureCoordinateIndex));
					}
				}
				MergeData.TexCoordBounds[0] = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
			}
			MergeDataEntries.Add(MergeData);
		}
	}

	// Populate landscape clipping geometry
	for (FMeshDescription* RawMesh : CullingRawMeshes)
	{
		FMeshMergeData ClipData;
		ClipData.bIsClippingMesh = true;
		ClipData.RawMesh = RawMesh;
		MergeDataEntries.Add(ClipData);
	}

	SlowTask.EnterProgressFrame(50.0f, LOCTEXT("CreateProxyMesh_GenerateProxy", "Generating Proxy Mesh"));

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProxyGeneration)

		// Choose Simplygon Swarm (if available) or local proxy lod method
		if (ReductionModule.GetDistributedMeshMergingInterface() != nullptr && GetDefault<UEditorPerProjectUserSettings>()->bUseSimplygonSwarm && bAllowAsync)
		{
			MaterialFlattenLambda(FlattenedMaterials);

			ReductionModule.GetDistributedMeshMergingInterface()->ProxyLOD(MergeDataEntries, Data->InProxySettings, FlattenedMaterials, InGuid);
		}
		else
		{
			IMeshMerging* MeshMerging = ReductionModule.GetMeshMergingInterface();

			// Register the Material Flattening code if parallel execution is supported, otherwise directly run it.

			if (MeshMerging->bSupportsParallelMaterialBake())
			{
				MeshMerging->BakeMaterialsDelegate.BindLambda(MaterialFlattenLambda);
			}
			else
			{
				MaterialFlattenLambda(FlattenedMaterials);
			}

			MeshMerging->ProxyLOD(MergeDataEntries, Data->InProxySettings, FlattenedMaterials, InGuid);


			Processor->Tick(0); // make sure caller gets merging results
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Cleanup)

	// Clean up the CullingRawMeshes
	ParallelFor(CullingRawMeshes.Num(),
		[&CullingRawMeshes](int32 Index)
		{
			delete CullingRawMeshes[Index];
		}
	);

	// Clean up the MeshDescriptionData
	ParallelFor(
		MeshDescriptionData.Num(),
		[&MeshDescriptionData](int32 Index)
		{
			delete MeshDescriptionData[Index];
		}
	);
}

bool FMeshMergeUtilities::IsValidBaseMaterial(const UMaterialInterface* InBaseMaterial, bool bShowToaster) const
{
	if (InBaseMaterial != nullptr)
	{
		TArray<FGuid> ParameterIds;
		TArray<FString> MissingParameters;
		auto NameCheckLambda = [&MissingParameters](const TArray<FMaterialParameterInfo>& InCheck, const TArray<FName>& InRequired)
		{
			for (const FName& Name : InRequired)
			{
				if (!InCheck.ContainsByPredicate([Name](const FMaterialParameterInfo& ParamInfo) { return (ParamInfo.Name == Name); }))
				{
					MissingParameters.Add(Name.ToString());
				}
			}
		};

		TArray<FMaterialParameterInfo> TextureParameterInfos;
		TArray<FName> RequiredTextureNames = { TEXT("DiffuseTexture"), TEXT("NormalTexture"), TEXT("PackedTexture"), TEXT("MetallicTexture"), TEXT("SpecularTexture"), TEXT("RoughnessTexture"), TEXT("EmissiveTexture"), TEXT("OpacityTexture"), TEXT("OpacityMaskTexture"), TEXT("AmbientOcclusionTexture") };
		InBaseMaterial->GetAllTextureParameterInfo(TextureParameterInfos, ParameterIds);
		NameCheckLambda(TextureParameterInfos, RequiredTextureNames);

		TArray<FMaterialParameterInfo> ScalarParameterInfos;
		TArray<FName> RequiredScalarNames = { TEXT("MetallicConst"), TEXT("SpecularConst"), TEXT("RoughnessConst"), TEXT("OpacityConst"), TEXT("OpacityMaskConst"), TEXT("AmbientOcclusionConst"), TEXT("EmissiveScale") };
		InBaseMaterial->GetAllScalarParameterInfo(ScalarParameterInfos, ParameterIds);
		NameCheckLambda(ScalarParameterInfos, RequiredScalarNames);

		TArray<FMaterialParameterInfo> VectorParameterInfos;
		TArray<FName> RequiredVectorNames = { TEXT("DiffuseConst"), TEXT("EmissiveConst") };
		InBaseMaterial->GetAllVectorParameterInfo(VectorParameterInfos, ParameterIds);
		NameCheckLambda(VectorParameterInfos, RequiredVectorNames);

		TArray<FMaterialParameterInfo> StaticSwitchParameterInfos;
		TArray<FName> RequiredSwitchNames = { TEXT("UseDiffuse"), TEXT("PackMetallic"), TEXT("PackSpecular"), TEXT("PackRoughness"),TEXT("UseMetallic"), TEXT("UseSpecular"), TEXT("UseRoughness"), TEXT("UseEmissive"), TEXT("UseOpacity"), TEXT("UseOpacityMask"), TEXT("UseAmbientOcclusion") };
		InBaseMaterial->GetAllStaticSwitchParameterInfo(StaticSwitchParameterInfos, ParameterIds);
		NameCheckLambda(StaticSwitchParameterInfos, RequiredSwitchNames);

		if (MissingParameters.Num() > 0)
		{
			FString MissingNamesString;
			for (const FString& Name : MissingParameters)
			{
				if (!MissingNamesString.IsEmpty())
				{
					MissingNamesString += ", ";
					MissingNamesString += Name;
				}
				else
				{
					MissingNamesString += Name;
				}
			}
#if WITH_EDITOR
			if (bShowToaster)
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("MaterialName"), FText::FromString(InBaseMaterial->GetName()));
				FText ErrorMessage = FText::Format(LOCTEXT("UHierarchicalLODSettings_PostEditChangeProperty", "Material {MaterialName} is missing required Material Parameters (check log for details)"), Arguments);
				FNotificationInfo Info(ErrorMessage);
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}

			UE_LOG(LogMeshMerging, Error, TEXT("Material %s is missing required Material Parameters %s, resetting to default."), *InBaseMaterial->GetName(), *MissingNamesString);
#endif // WITH_EDITOR

			return false;
		}
		else
		{
			return true;
		}
	}

	return false;
}

void FMeshMergeUtilities::RegisterExtension(IMeshMergeExtension* InExtension)
{
	MeshMergeExtensions.Add(InExtension);
}

void FMeshMergeUtilities::UnregisterExtension(IMeshMergeExtension* InExtension)
{
	MeshMergeExtensions.Remove(InExtension);
}

bool RetrieveRawMeshData(FMeshMergeDataTracker& DataTracker
	, const int32 ComponentIndex
	, const int32 LODIndex
	, UStaticMeshComponent* Component
	, const bool bPropagateMeshData
	, TArray<FSectionInfo>& Sections
	, FStaticMeshComponentAdapter& Adapter
	, const bool bMergeMaterialData
	, const FMeshMergingSettings& InSettings)
{
	// Retrieve raw mesh data
	FMeshDescription& RawMesh = DataTracker.AddAndRetrieveRawMesh(ComponentIndex, LODIndex, Component->GetStaticMesh());
	Adapter.RetrieveRawMeshData(LODIndex, RawMesh, bPropagateMeshData);

	// Reset section for reuse
	Sections.SetNum(0, false);

	// Extract sections for given LOD index from the mesh 
	Adapter.RetrieveMeshSections(LODIndex, Sections);

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FSectionInfo& Section = Sections[SectionIndex];
		// Unique section index for remapping
		const int32 UniqueIndex = DataTracker.AddSection(Section);

		// Store of original to unique section index entry for this component + LOD index
		DataTracker.AddSectionRemapping(ComponentIndex, LODIndex, SectionIndex, UniqueIndex);
		DataTracker.AddMaterialSlotName(Section.Material, Section.MaterialSlotName);

		if (!bMergeMaterialData)
		{
			FStaticMeshOperations::SwapPolygonPolygonGroup(RawMesh, UniqueIndex, Section.StartIndex, Section.EndIndex, false);
		}
	}
	
	//Compact the PolygonGroupID to make sure it follow the section index
	FElementIDRemappings RemapInformation;
	RawMesh.Compact(RemapInformation);

	// If the component is an ISMC then we need to duplicate the vertex data
	if (Component->IsA<UInstancedStaticMeshComponent>())
	{
		const UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component);
		FMeshMergeHelpers::ExpandInstances(InstancedStaticMeshComponent, RawMesh, Sections);
	}

	if (InSettings.bUseLandscapeCulling)
	{
		FMeshMergeHelpers::CullTrianglesFromVolumesAndUnderLandscapes(Component->GetWorld(), Adapter.GetBounds(), RawMesh);
	}

	// If the valid became invalid during retrieval remove it again
	const bool bValidMesh = RawMesh.VertexInstances().Num() > 0;
	if (!bValidMesh)
	{
		DataTracker.RemoveRawMesh(ComponentIndex, LODIndex);
	}
	else if (Component->GetStaticMesh() != nullptr)
	{
		// If the mesh is valid at this point, record the lightmap UV so we have a record for use later
		DataTracker.AddLightmapChannelRecord(ComponentIndex, LODIndex, Component->GetStaticMesh()->LightMapCoordinateIndex);
	}
	return bValidMesh;
}

void FMeshMergeUtilities::MergeComponentsToStaticMesh(const TArray<UPrimitiveComponent*>& ComponentsToMerge, UWorld* World, const FMeshMergingSettings& InSettings, UMaterialInterface* InBaseMaterial, UPackage* InOuter, const FString& InBasePackageName, TArray<UObject*>& OutAssetsToSync, FVector& OutMergedActorLocation, const float ScreenSize, bool bSilent /*= false*/) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::MergeComponentsToStaticMesh);

	// Use first mesh for naming and pivot
	bool bFirstMesh = true;
	FString MergedAssetPackageName;
	FVector MergedAssetPivot;
	
	TArray<UStaticMeshComponent*> StaticMeshComponentsToMerge;
	TArray<const UStaticMeshComponent*> ImposterComponents;

	for (int32 MeshId = 0; MeshId < ComponentsToMerge.Num(); ++MeshId)
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentsToMerge[MeshId]);
		if (MeshComponent)
		{
			if(MeshComponent->bUseMaxLODAsImposter && InSettings.bIncludeImposters)
			{
				ImposterComponents.Add(MeshComponent);
			}
			else
			{
				StaticMeshComponentsToMerge.Add(MeshComponent);
			}

			// Save the pivot and asset package name of the first mesh, will later be used for creating merged mesh asset 
			if (bFirstMesh)
			{
				// Mesh component pivot point
				MergedAssetPivot = InSettings.bPivotPointAtZero ? FVector::ZeroVector : MeshComponent->GetComponentTransform().GetLocation();

				// Source mesh asset package name
				MergedAssetPackageName = MeshComponent->GetStaticMesh()->GetOutermost()->GetName();

				bFirstMesh = false;
			}
		}
	}

	// Nothing to do if no StaticMeshComponents
	if (StaticMeshComponentsToMerge.Num() == 0 && ImposterComponents.Num() == 0)
	{
		return;
	}

	FMeshMergeDataTracker DataTracker;

	const bool bMergeAllLODs = InSettings.LODSelectionType == EMeshLODSelectionType::AllLODs;
	const bool bMergeMaterialData = InSettings.bMergeMaterials && InSettings.LODSelectionType != EMeshLODSelectionType::AllLODs;
	const bool bPropagateMeshData = InSettings.bBakeVertexDataToMesh || (bMergeMaterialData && InSettings.bUseVertexDataForBakingMaterial);

	TArray<FStaticMeshComponentAdapter> Adapters;

	TArray<FSectionInfo> Sections;
	if (bMergeAllLODs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RetrieveRawMeshData);
		for (int32 ComponentIndex = 0; ComponentIndex < StaticMeshComponentsToMerge.Num(); ++ComponentIndex)
		{
			UStaticMeshComponent* Component = StaticMeshComponentsToMerge[ComponentIndex];
			Adapters.Add(FStaticMeshComponentAdapter(Component));
			FStaticMeshComponentAdapter& Adapter = Adapters.Last();
			
			if (InSettings.bComputedLightMapResolution)
			{
				int32 LightMapHeight, LightMapWidth;
				if (Component->GetLightMapResolution(LightMapWidth, LightMapHeight))
				{
					DataTracker.AddLightMapPixels(LightMapWidth * LightMapHeight);
				}
			}			
						
			const int32 NumLODs = [&]()
			{
				const int32 NumberOfLODsAvailable = Adapter.GetNumberOfLODs();
				if (Component->bUseMaxLODAsImposter)
				{
					return InSettings.bIncludeImposters ? NumberOfLODsAvailable : NumberOfLODsAvailable - 1;
				}

				return NumberOfLODsAvailable;
			}();

			for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
			{
				if (!RetrieveRawMeshData(DataTracker
					, ComponentIndex
					, LODIndex
					, Component
					, bPropagateMeshData
					, Sections
					, Adapter
					, false
					, InSettings))
				{
					//If the rawmesh was not retrieve properly break the loop
					break;
				}
				DataTracker.AddLODIndex(LODIndex);
			}
		}
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RetrieveRawMeshData);

		// Retrieve HLOD module for calculating LOD index from screen size
		FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
		IHierarchicalLODUtilities* Utilities = Module.GetUtilities();

		// Adding LOD 0 for merged mesh output
		DataTracker.AddLODIndex(0);

		// Retrieve mesh and section data for each component
		for (int32 ComponentIndex = 0; ComponentIndex < StaticMeshComponentsToMerge.Num(); ++ComponentIndex)
		{
			// Create material merge adapter for this component
			UStaticMeshComponent* Component = StaticMeshComponentsToMerge[ComponentIndex];
			Adapters.Add(FStaticMeshComponentAdapter(Component));
			FStaticMeshComponentAdapter& Adapter = Adapters.Last();

			// Determine LOD to use for merging, either user specified or calculated index and ensure we clamp to the maximum LOD index for this adapter 
			const int32 LODIndex = [&]()
			{
				int32 LowestDetailLOD = Adapter.GetNumberOfLODs() - 1;
				if (Component->bUseMaxLODAsImposter && !InSettings.bIncludeImposters)
				{
					LowestDetailLOD = FMath::Max(0, LowestDetailLOD - 1);
				}

				switch (InSettings.LODSelectionType)
				{
				case EMeshLODSelectionType::SpecificLOD:
					return FMath::Min(LowestDetailLOD, InSettings.SpecificLOD);

				case EMeshLODSelectionType::CalculateLOD:
					return FMath::Min(LowestDetailLOD, Utilities->GetLODLevelForScreenSize(Component, FMath::Clamp(ScreenSize, 0.0f, 1.0f)));

				case EMeshLODSelectionType::LowestDetailLOD:
				default:
					return LowestDetailLOD;
				}
			}();

			RetrieveRawMeshData(DataTracker
				, ComponentIndex
				, LODIndex
				, Component
				, bPropagateMeshData
				, Sections
				, Adapter
				, bMergeMaterialData
				, InSettings);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProcessRawMeshes);
		DataTracker.ProcessRawMeshes();
	}

	// Retrieve physics data
	UBodySetup* BodySetupSource = nullptr;
	TArray<FKAggregateGeom> PhysicsGeometry;
	if (InSettings.bMergePhysicsData)
	{
		ExtractPhysicsDataFromComponents(ComponentsToMerge, PhysicsGeometry, BodySetupSource);
	}

	// Find all unique materials and remap section to unique materials
	TArray<UMaterialInterface*> UniqueMaterials;
	TMap<UMaterialInterface*, int32> MaterialIndices;
	TMap<UMaterialInterface*, UMaterialInterface*> CollapsedMaterialMap;

	for (int32 SectionIndex = 0; SectionIndex < DataTracker.NumberOfUniqueSections(); ++SectionIndex)
	{
		// Unique index for material
		UMaterialInterface* MaterialInterface = DataTracker.GetMaterialForSectionIndex(SectionIndex);
		int32 UniqueIndex = UniqueMaterials.IndexOfByPredicate([&InSettings, MaterialInterface](const UMaterialInterface* InMaterialInterface)
		{
			// Perform an optional custom comparison if we are trying to collapse material instances
			if(InSettings.bMergeEquivalentMaterials)
			{
				return FMaterialKey(MaterialInterface) == FMaterialKey(InMaterialInterface);
			}
			return MaterialInterface == InMaterialInterface;
		});

		if(UniqueIndex == INDEX_NONE)
		{
			UniqueIndex = UniqueMaterials.Add(MaterialInterface);
		}

		// Update map to 'collapsed' materials
		CollapsedMaterialMap.Add(MaterialInterface, UniqueMaterials[UniqueIndex]);
	}

	// For each unique material calculate how 'important' they are
	TArray<float> MaterialImportanceValues;
	FMaterialUtilities::DetermineMaterialImportance(UniqueMaterials, MaterialImportanceValues);

	TMultiMap< FMeshLODKey, MaterialRemapPair > OutputMaterialsMap;

	// The UV channel to use for the flattened material
	int32 MergedMatUVChannel = INDEX_NONE;
	UMaterialInterface* MergedMaterial = nullptr;

	// If the user wants to merge materials into a single one
	if (bMergeMaterialData && UniqueMaterials.Num() != 0)
	{
		UMaterialOptions* MaterialOptions = PopulateMaterialOptions(InSettings.MaterialSettings);
		// Check each material to see if the shader actually uses vertex data and collect flags
		TArray<bool> bMaterialUsesVertexData;
		DetermineMaterialVertexDataUsage(bMaterialUsesVertexData, UniqueMaterials, MaterialOptions);

		TArray<FMeshData> GlobalMeshSettings;
		TArray<FMaterialData> GlobalMaterialSettings;
		TArray<float> SectionMaterialImportanceValues;

		TMap<EMaterialProperty, FIntPoint> PropertySizes;
		for (const FPropertyEntry& Entry : MaterialOptions->Properties)
		{
			if (!Entry.bUseConstantValue && Entry.Property != MP_MAX)
			{
				PropertySizes.Add(Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
			}
		}

		TMap<UMaterialInterface*, int32> MaterialToDefaultMeshData;

		// If we are generating a single LOD and want to merge materials we can utilize texture space better by generating unique UVs
		// for the merged mesh and baking out materials using those UVs
		const bool bGloballyRemapUVs = !bMergeAllLODs && !InSettings.bReuseMeshLightmapUVs;

		typedef TTuple<UStaticMesh*, int32> FMeshLODTuple;
		typedef TFuture<TArray<FVector2D>> FUVComputeFuture;
		TMap<FMeshLODTuple, FUVComputeFuture> MeshLODsTextureCoordinates;
		TMap<int32, FMeshLODTuple> MeshDataAwaitingResults;

		for (TConstRawMeshIterator RawMeshIterator = DataTracker.GetConstRawMeshIterator(); RawMeshIterator; ++RawMeshIterator)
		{
			const FMeshLODKey& Key = RawMeshIterator.Key();
			const FMeshDescription& RawMesh = RawMeshIterator.Value();
			const bool bRequiresUniqueUVs = DataTracker.DoesMeshLODRequireUniqueUVs(Key);
			UStaticMeshComponent* Component = StaticMeshComponentsToMerge[Key.GetMeshIndex()];

			// Retrieve all sections and materials for key
			TArray<SectionRemapPair> SectionRemapPairs;
			DataTracker.GetMappingsForMeshLOD(Key, SectionRemapPairs);

			// Contains unique materials used for this key, and the accompanying section index which point to the material
			TMap<UMaterialInterface*, TArray<int32>> MaterialAndSectionIndices;

			for (const SectionRemapPair& RemapPair : SectionRemapPairs)
			{
				const int32 UniqueIndex = RemapPair.Value;
				const int32 SectionIndex = RemapPair.Key;
				TArray<int32>& SectionIndices = MaterialAndSectionIndices.FindOrAdd(CollapsedMaterialMap.FindChecked(DataTracker.GetMaterialForSectionIndex(UniqueIndex)));
				SectionIndices.Add(SectionIndex);
			}

			for (TPair<UMaterialInterface*, TArray<int32>>& MaterialSectionIndexPair : MaterialAndSectionIndices)
			{
				UMaterialInterface* Material = MaterialSectionIndexPair.Key;
				const int32 MaterialIndex = UniqueMaterials.IndexOfByKey(Material);
				const TArray<int32>& SectionIndices = MaterialSectionIndexPair.Value;
				const bool bDoesMaterialUseVertexData = bMaterialUsesVertexData[MaterialIndex];

				FMaterialData MaterialData;
				MaterialData.Material = CollapsedMaterialMap.FindChecked(Material);
				MaterialData.PropertySizes = PropertySizes;

				FMeshData MeshData;
				MeshData.Mesh = Key.GetMesh();
				MeshData.VertexColorHash = Key.GetVertexColorHash();
				MeshData.bMirrored = Component->GetComponentTransform().GetDeterminant() < 0.0f;
				int32 MeshDataIndex = 0;
				
				if (InSettings.bCreateMergedMaterial || bGloballyRemapUVs || (InSettings.bUseVertexDataForBakingMaterial && (bDoesMaterialUseVertexData || bRequiresUniqueUVs)))
				{
					FMeshDescription* RawMeshDescription = DataTracker.GetRawMeshPtr(Key);
					MeshData.RawMeshDescription = RawMeshDescription;

					// if it has vertex color/*WedgetColors.Num()*/, it should also use light map UV index
					// we can't do this for all meshes, but only for the mesh that has vertex color.
					if (bRequiresUniqueUVs || MeshData.RawMeshDescription->VertexInstances().Num() > 0)
					{
						// Check if there are lightmap uvs available?
						const int32 LightMapUVIndex = StaticMeshComponentsToMerge[Key.GetMeshIndex()]->GetStaticMesh()->LightMapCoordinateIndex;

						TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshData.RawMeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
						if (InSettings.bReuseMeshLightmapUVs && VertexInstanceUVs.GetNumElements() > 0 && VertexInstanceUVs.GetNumIndices() > LightMapUVIndex)
						{
							MeshData.TextureCoordinateIndex = LightMapUVIndex;
						}
						else
						{
							// Verify if we started an async task to generate UVs for this static mesh & LOD
							FMeshLODTuple Tuple(Key.GetMesh(), Key.GetLODIndex());
							if (!MeshLODsTextureCoordinates.Find(Tuple))
							{
								// No job found yet, fire an async task
								MeshLODsTextureCoordinates.Add(Tuple, Async(EAsyncExecution::Thread, [RawMeshDescription, MaterialOptions, this]()
								{
									TArray<FVector2D> UniqueTextureCoordinates;
									FStaticMeshOperations::GenerateUniqueUVsForStaticMesh(*RawMeshDescription, MaterialOptions->TextureSize.GetMax(), false, UniqueTextureCoordinates);
									ScaleTextureCoordinatesToBox(FBox2D(FVector2D::ZeroVector, FVector2D(1, 1)), UniqueTextureCoordinates);
									return UniqueTextureCoordinates;
								}));
							}
							// Keep track of the fact that this mesh is waiting for the UV computation to finish
							MeshDataAwaitingResults.Add(MeshDataIndex, Tuple);
						}
					}

					MeshData.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
					MeshData.MaterialIndices = SectionIndices;
					MeshDataIndex = GlobalMeshSettings.Num();

					Adapters[Key.GetMeshIndex()].ApplySettings(Key.GetLODIndex(), MeshData);

					int32 ExistingMeshDataIndex = INDEX_NONE;

					auto MaterialsAreEquivalent = [&InSettings](const UMaterialInterface* Material0, const UMaterialInterface* Material1)
					{
						if (InSettings.bMergeEquivalentMaterials)
						{
							return FMaterialKey(Material0) == FMaterialKey(Material1);
						}
						else
						{
							return Material0 == Material1;
						}
					};

					// Find any existing materials
					for (int32 GlobalMaterialSettingsIndex = 0; GlobalMaterialSettingsIndex < GlobalMaterialSettings.Num(); ++GlobalMaterialSettingsIndex)
					{
						const FMaterialData& ExistingMaterialData = GlobalMaterialSettings[GlobalMaterialSettingsIndex];
						// Compare materials (note this assumes property sizes match!)
						if (MaterialsAreEquivalent(ExistingMaterialData.Material, MaterialData.Material))
						{
							// materials match, so check the corresponding mesh data
							const FMeshData& ExistingMeshData = GlobalMeshSettings[GlobalMaterialSettingsIndex];
							bool bMatchesMesh = (ExistingMeshData.Mesh == MeshData.Mesh &&
								ExistingMeshData.MaterialIndices == MeshData.MaterialIndices &&
								ExistingMeshData.bMirrored == MeshData.bMirrored &&
								ExistingMeshData.VertexColorHash == MeshData.VertexColorHash);
							if (bMatchesMesh)
							{
								MeshDataIndex = ExistingMeshDataIndex = GlobalMaterialSettingsIndex;
								break;
							}
						}
					}

					if (ExistingMeshDataIndex == INDEX_NONE)
					{
						GlobalMeshSettings.Add(MeshData);
						GlobalMaterialSettings.Add(MaterialData);
						SectionMaterialImportanceValues.Add(MaterialImportanceValues[MaterialIndex]);
					}
				}
				else
				{
					MeshData.RawMeshDescription = nullptr;
					MeshData.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));

					// This prevents baking out the same material multiple times, which would be wasteful when it does not use vertex data anyway
					const bool bPreviouslyAdded = MaterialToDefaultMeshData.Contains(Material);
					int32& DefaultMeshDataIndex = MaterialToDefaultMeshData.FindOrAdd(Material);

					if (!bPreviouslyAdded)
					{
						DefaultMeshDataIndex = GlobalMeshSettings.Num();
						GlobalMeshSettings.Add(MeshData);
						GlobalMaterialSettings.Add(MaterialData);
						SectionMaterialImportanceValues.Add(MaterialImportanceValues[MaterialIndex]);
					}

					MeshDataIndex = DefaultMeshDataIndex;
				}

				for (const uint32& OriginalSectionIndex : SectionIndices)
				{
					OutputMaterialsMap.Add(Key, MaterialRemapPair(OriginalSectionIndex, MeshDataIndex));
				}
			}
		}

		// Fetch results from the async UV computation tasks
		for (auto MeshData : MeshDataAwaitingResults)
		{
			GlobalMeshSettings[MeshData.Key].CustomTextureCoordinates = MeshLODsTextureCoordinates[MeshData.Value].Get();
		}

		TArray<FMeshData*> MeshSettingPtrs;
		for (int32 SettingsIndex = 0; SettingsIndex < GlobalMeshSettings.Num(); ++SettingsIndex)
		{
			MeshSettingPtrs.Add(&GlobalMeshSettings[SettingsIndex]);
		}

		TArray<FMaterialData*> MaterialSettingPtrs;
		for (int32 SettingsIndex = 0; SettingsIndex < GlobalMaterialSettings.Num(); ++SettingsIndex)
		{
			MaterialSettingPtrs.Add(&GlobalMaterialSettings[SettingsIndex]);
		}

		if(bGloballyRemapUVs)
		{
			TArray<FMeshDescription> MergedRawMeshes;
			CreateMergedRawMeshes(DataTracker, InSettings, StaticMeshComponentsToMerge, UniqueMaterials, CollapsedMaterialMap, OutputMaterialsMap, false, false, MergedAssetPivot, MergedRawMeshes);

			// Create texture coords for the merged mesh
			TArray<FVector2D> GlobalTextureCoordinates;
			FStaticMeshOperations::GenerateUniqueUVsForStaticMesh(MergedRawMeshes[0], MaterialOptions->TextureSize.GetMax(), true, GlobalTextureCoordinates);
			ScaleTextureCoordinatesToBox(FBox2D(FVector2D::ZeroVector, FVector2D(1, 1)), GlobalTextureCoordinates);

			// copy UVs back to the un-merged mesh's custom texture coords
			// iterate the raw meshes in the same way as when we combined the mesh above in CreateMergedRawMeshes()
			int32 GlobalUVIndex = 0;
			for (TConstRawMeshIterator RawMeshIterator = DataTracker.GetConstRawMeshIterator(); RawMeshIterator; ++RawMeshIterator)
			{
				const FMeshLODKey& Key = RawMeshIterator.Key();
				const FMeshDescription& RawMesh = RawMeshIterator.Value();

				// Build a local array for this raw mesh
				TArray<FVector2D> UniqueTextureCoordinates;
				UniqueTextureCoordinates.SetNumUninitialized(RawMesh.VertexInstances().Num());
				for(FVector2D& UniqueTextureCoordinate : UniqueTextureCoordinates)
				{
					UniqueTextureCoordinate = GlobalTextureCoordinates[GlobalUVIndex++];
				}

				// copy to mesh data
				for(FMeshData& MeshData : GlobalMeshSettings)
				{
					if(MeshData.RawMeshDescription == &RawMesh)
					{
						MeshData.CustomTextureCoordinates = UniqueTextureCoordinates;
					}
				}
			}

			// Dont smear borders as we will copy back non-pink pixels
			for(FMaterialData& MaterialData : GlobalMaterialSettings)
			{
				MaterialData.bPerformBorderSmear = false;
			}
		}

		TArray<FFlattenMaterial> FlattenedMaterials;
		// This scope ensures BakeOutputs is never used after TransferOutputToFlatMaterials
		{
			TArray<FBakeOutput> BakeOutputs;
			IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
			Module.BakeMaterials(MaterialSettingPtrs, MeshSettingPtrs, BakeOutputs);

			// Append constant properties ?
			TArray<FColor> ConstantData;
			FIntPoint ConstantSize(1, 1);
			for (const FPropertyEntry& Entry : MaterialOptions->Properties)
			{
				if (Entry.bUseConstantValue && Entry.Property != MP_MAX)
				{
					ConstantData.SetNum(1, false);
					ConstantData[0] = FLinearColor(Entry.ConstantValue, Entry.ConstantValue, Entry.ConstantValue).ToFColor(true);
					for (FBakeOutput& Output : BakeOutputs)
					{
						Output.PropertyData.Add(Entry.Property, ConstantData);
						Output.PropertySizes.Add(Entry.Property, ConstantSize);
					}
				}
			}

			TransferOutputToFlatMaterials(GlobalMaterialSettings, BakeOutputs, FlattenedMaterials);
		}

		if(!bGloballyRemapUVs)
		{
			// Try to optimize materials where possible	
			for (FFlattenMaterial& InMaterial : FlattenedMaterials)
			{
				FMaterialUtilities::OptimizeFlattenMaterial(InMaterial);
			}
		}

		FFlattenMaterial OutMaterial;
		for (const FPropertyEntry& Entry : MaterialOptions->Properties)
		{
			if (Entry.Property != MP_MAX)
			{
				EFlattenMaterialProperties OldProperty = NewToOldProperty(Entry.Property);
				OutMaterial.SetPropertySize(OldProperty, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
			}
		}

		TArray<FUVOffsetScalePair> UVTransforms;
		if(bGloballyRemapUVs)
		{
			// If we have globally remapped UVs we copy non-pink pixels over the dest texture rather than 
			// copying sub-charts
			TArray<FBox2D> MaterialBoxes;
			MaterialBoxes.SetNumUninitialized(GlobalMaterialSettings.Num());
			for(FBox2D& Box2D : MaterialBoxes)
			{
				Box2D = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));
			}

			FlattenBinnedMaterials(FlattenedMaterials, MaterialBoxes, 0, true, OutMaterial, UVTransforms);
		}
		else
		{
			/** Reweighting */
			float TotalValue = 0.0f;
			for (const float& Value : SectionMaterialImportanceValues)
			{
				TotalValue += Value;
			}

			float Multiplier = 1.0f / TotalValue;

			for (float& Value : SectionMaterialImportanceValues)
			{
				Value *= Multiplier;
			}
			/** End reweighting */

			if (InSettings.bUseTextureBinning)
			{
				TArray<FBox2D> MaterialBoxes;
				FMaterialUtilities::GeneratedBinnedTextureSquares(FVector2D(1.0f, 1.0f), SectionMaterialImportanceValues, MaterialBoxes);
				FlattenBinnedMaterials(FlattenedMaterials, MaterialBoxes, InSettings.GutterSize, false, OutMaterial, UVTransforms);
			}
			else
			{
				MergeFlattenedMaterials(FlattenedMaterials, InSettings.GutterSize, OutMaterial, UVTransforms);
			}
		}

		// Compute UV channel to use for the merged material
		if(InSettings.bCreateMergedMaterial)
		{
			for(TConstRawMeshIterator Iterator = DataTracker.GetConstRawMeshIterator(); Iterator; ++Iterator)
			{
				const FMeshDescription& RawMesh = Iterator.Value();

				if(RawMesh.Vertices().Num())
				{
					const TVertexInstanceAttributesRef<const FVector2D> VertexInstanceUVs = RawMesh.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
					MergedMatUVChannel = FMath::Max(MergedMatUVChannel, VertexInstanceUVs.GetNumIndices());
				}
			}
		}

		// Adjust UVs
		for (int32 ComponentIndex = 0; ComponentIndex < StaticMeshComponentsToMerge.Num(); ++ComponentIndex)
		{
			TArray<uint32> ProcessedMaterials;
			for (TPair<FMeshLODKey, MaterialRemapPair>& MappingPair : OutputMaterialsMap)
			{
				if (MappingPair.Key.GetMeshIndex() == ComponentIndex && !ProcessedMaterials.Contains(MappingPair.Value.Key))
				{
					// Retrieve raw mesh data for this component and lod pair
					FMeshDescription* RawMesh = DataTracker.GetRawMeshPtr(MappingPair.Key);

					FMeshData& MeshData = GlobalMeshSettings[MappingPair.Value.Value];
					const FUVOffsetScalePair& UVTransform = UVTransforms[MappingPair.Value.Value];

					const uint32 MaterialIndex = MappingPair.Value.Key;
					ProcessedMaterials.Add(MaterialIndex);
					if (RawMesh->Vertices().Num())
					{
						TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = RawMesh->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
						int32 NumUVChannel = FMath::Min(VertexInstanceUVs.GetNumIndices(), (int32)MAX_MESH_TEXTURE_COORDS);
						for (int32 UVChannelIdx = 0; UVChannelIdx < NumUVChannel; ++UVChannelIdx)
						{
							int32 VertexIndex = 0;
							for (FVertexInstanceID VertexInstanceID : RawMesh->VertexInstances().GetElementIDs())
							{
								FVector2D UV = VertexInstanceUVs.Get(VertexInstanceID, UVChannelIdx);
								if (UVChannelIdx == 0 && !InSettings.bCreateMergedMaterial)
								{
									if (MeshData.CustomTextureCoordinates.Num())
									{
										UV = MeshData.CustomTextureCoordinates[VertexIndex];
									}
									else if (MeshData.TextureCoordinateIndex != 0)
									{
										check(MeshData.TextureCoordinateIndex < NumUVChannel);
										UV = VertexInstanceUVs.Get(VertexInstanceID, MeshData.TextureCoordinateIndex);
									}
								}

								const TArray<FPolygonID>& Polygons = RawMesh->GetVertexInstanceConnectedPolygons(VertexInstanceID);
								for (FPolygonID PolygonID : Polygons)
								{
									FPolygonGroupID PolygonGroupID = RawMesh->GetPolygonPolygonGroup(PolygonID);
									if (PolygonGroupID.GetValue() == MaterialIndex)
									{
										if (UVTransform.Value != FVector2D::ZeroVector)
										{
											VertexInstanceUVs.Set(VertexInstanceID, UVChannelIdx, UV * UVTransform.Value + UVTransform.Key);
											break;
										}
									}
								}
								VertexIndex++;
							}
						}

						if (InSettings.bCreateMergedMaterial && MeshData.CustomTextureCoordinates.Num() > 0)
						{
							VertexInstanceUVs.SetNumIndices(MergedMatUVChannel + 1);

							int32 VertexIndex = 0;
							for(FVertexInstanceID VertexInstanceID : RawMesh->VertexInstances().GetElementIDs())
							{
								FVector2D UV = MeshData.CustomTextureCoordinates[VertexIndex];
								VertexInstanceUVs.Set(VertexInstanceID, MergedMatUVChannel, UV * UVTransform.Value + UVTransform.Key);
								VertexIndex++;
							}
						}
					}
				}
			}
		}

		for (TRawMeshIterator Iterator = DataTracker.GetRawMeshIterator(); Iterator; ++Iterator)
		{
			FMeshDescription& RawMesh = Iterator.Value();
			// Reset material indexes
			TMap<FPolygonGroupID, FPolygonGroupID> RemapPolygonGroups;
			for (FPolygonGroupID PolygonGroupID : RawMesh.PolygonGroups().GetElementIDs())
			{
				RemapPolygonGroups.Add(PolygonGroupID, FPolygonGroupID(0));
			}
			RawMesh.RemapPolygonGroups(RemapPolygonGroups);
		}

		OutMaterial.UVChannel = MergedMatUVChannel;

		MergedMaterial = CreateProxyMaterial(InBasePackageName, MergedAssetPackageName, InBaseMaterial, InOuter, InSettings, OutMaterial, OutAssetsToSync);
		
		if (MergedMaterial)
		{
			if (!InSettings.bCreateMergedMaterial)
			{
				UniqueMaterials.Empty(1);
				UniqueMaterials.Add(MergedMaterial);

				FSectionInfo NewSection;
				NewSection.Material = MergedMaterial;
				NewSection.EnabledProperties.Add(GET_MEMBER_NAME_CHECKED(FStaticMeshSection, bCastShadow));
				DataTracker.AddBakedMaterialSection(NewSection);
			}

			for (IMeshMergeExtension* Extension : MeshMergeExtensions)
			{
				Extension->OnCreatedProxyMaterial(StaticMeshComponentsToMerge, MergedMaterial);
			}
		}
	}

	TArray<FMeshDescription> MergedRawMeshes;
	CreateMergedRawMeshes(DataTracker, InSettings, StaticMeshComponentsToMerge, UniqueMaterials, CollapsedMaterialMap, OutputMaterialsMap, bMergeAllLODs, bMergeMaterialData && !InSettings.bCreateMergedMaterial, MergedAssetPivot, MergedRawMeshes);

	// Populate mesh section map
	FMeshSectionInfoMap SectionInfoMap;	
	for (TConstLODIndexIterator Iterator = DataTracker.GetLODIndexIterator(); Iterator; ++Iterator)
	{
		const int32 LODIndex = *Iterator;
		TArray<uint32> UniqueMaterialIndices;
		const FMeshDescription& TargetRawMesh = MergedRawMeshes[LODIndex];
		uint32 MaterialIndex = 0;
		for (FPolygonGroupID PolygonGroupID : TargetRawMesh.PolygonGroups().GetElementIDs())
		{
			//Skip empty group
			if (TargetRawMesh.GetPolygonGroupPolygons(PolygonGroupID).Num() > 0)
			{
				if (PolygonGroupID.GetValue() < DataTracker.NumberOfUniqueSections())
				{
					UniqueMaterialIndices.AddUnique(PolygonGroupID.GetValue());
				}
				else
				{
					UniqueMaterialIndices.AddUnique(MaterialIndex);
				}
				MaterialIndex++;
			}
		}
		UniqueMaterialIndices.Sort();
		for (int32 Index = 0; Index < UniqueMaterialIndices.Num(); ++Index)
		{
			const int32 SectionIndex = UniqueMaterialIndices[Index];
			const FSectionInfo& StoredSectionInfo = DataTracker.GetSection(SectionIndex);
			FMeshSectionInfo SectionInfo;
			SectionInfo.bCastShadow = StoredSectionInfo.EnabledProperties.Contains(GET_MEMBER_NAME_CHECKED(FMeshSectionInfo, bCastShadow));
			SectionInfo.bEnableCollision = StoredSectionInfo.EnabledProperties.Contains(GET_MEMBER_NAME_CHECKED(FMeshSectionInfo, bEnableCollision));
			SectionInfo.MaterialIndex = UniqueMaterials.IndexOfByKey(StoredSectionInfo.Material);
			SectionInfoMap.Set(LODIndex, Index, SectionInfo);
		}
	}

	if(InSettings.bCreateMergedMaterial)
	{
		OutputMaterialsMap.Reset();
	}

	// Transform physics primitives to merged mesh pivot
	if (InSettings.bMergePhysicsData && !MergedAssetPivot.IsZero())
	{
		FTransform PivotTM(-MergedAssetPivot);
		for (FKAggregateGeom& Geometry : PhysicsGeometry)
		{
			FMeshMergeHelpers::TransformPhysicsGeometry(PivotTM, false, Geometry);
		}
	}

	// Compute target lightmap channel for each LOD, by looking at the first empty UV channel
	const int32 LightMapUVChannel = [&]()
	{
		if (InSettings.bGenerateLightMapUV)
		{
			const int32 TempChannel = DataTracker.GetAvailableLightMapUVChannel();
			if (TempChannel != INDEX_NONE)
			{
				return TempChannel;
			}
			else
			{
				// Output warning message
				UE_LOG(LogMeshMerging, Log, TEXT("Failed to find available lightmap uv channel"));
				
			}
		}

		return 0;
	}();		

	//
	//Create merged mesh asset
	//
	{
		FString AssetName;
		FString PackageName;
		if (InBasePackageName.IsEmpty())
		{
			AssetName = TEXT("SM_MERGED_") + FPackageName::GetShortName(MergedAssetPackageName);
			PackageName = FPackageName::GetLongPackagePath(MergedAssetPackageName) + TEXT("/") + AssetName;
		}
		else
		{
			AssetName = FPackageName::GetShortName(InBasePackageName);
			PackageName = InBasePackageName;
		}

		UPackage* Package = InOuter;
		if (Package == nullptr)
		{
			Package = CreatePackage(NULL, *PackageName);
			check(Package);
			Package->FullyLoad();
			Package->Modify();
		}

		// Check that an asset of a different class does not already exist
		{
			UObject* ExistingObject = StaticFindObject( nullptr, Package, *AssetName);
			if(ExistingObject && !ExistingObject->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
			{
				// Change name of merged static mesh to avoid name collision
				UPackage* ParentPackage = CreatePackage(nullptr, *FPaths::GetPath(Package->GetPathName()));
				ParentPackage->FullyLoad();

				AssetName = MakeUniqueObjectName( ParentPackage, UStaticMesh::StaticClass(), *AssetName).ToString();
				Package = CreatePackage(NULL, *(ParentPackage->GetPathName() / AssetName ));
				check(Package);
				Package->FullyLoad();
				Package->Modify();

				// Let user know name of merged static mesh has changed
				UE_LOG(LogMeshMerging, Warning,
					TEXT("Cannot create %s %s.%s\n")
					TEXT("An object with the same fully qualified name but a different class already exists.\n")
					TEXT("\tExisting Object: %s\n")
					TEXT("The merged mesh will be named %s.%s"),
					*UStaticMesh::StaticClass()->GetName(), *ExistingObject->GetOutermost()->GetPathName(),	*ExistingObject->GetName(),
					*ExistingObject->GetFullName(), *Package->GetPathName(), *AssetName);
			}
		}

		FStaticMeshComponentRecreateRenderStateContext RecreateRenderStateContext(FindObject<UStaticMesh>(Package, *AssetName));

		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
		StaticMesh->InitResources();

		FString OutputPath = StaticMesh->GetPathName();

		// make sure it has a new lighting guid
		StaticMesh->LightingGuid = FGuid::NewGuid();
		if (InSettings.bGenerateLightMapUV)
		{
			StaticMesh->LightMapResolution = InSettings.TargetLightMapResolution;
			StaticMesh->LightMapCoordinateIndex = LightMapUVChannel;
		}

		const bool bContainsImposters = ImposterComponents.Num() > 0;
		TArray<UMaterialInterface*> ImposterMaterials;
		FBox ImposterBounds(EForceInit::ForceInit);
		for (int32 LODIndex = 0; LODIndex < MergedRawMeshes.Num(); ++LODIndex)
		{
			FMeshDescription& MergedMeshLOD = MergedRawMeshes[LODIndex];
			if (MergedMeshLOD.Vertices().Num() > 0 || bContainsImposters)
			{
				FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();

				// Don't allow the engine to recalculate normals
				SrcModel.BuildSettings.bRecomputeNormals = false;
				SrcModel.BuildSettings.bRecomputeTangents = false;
				SrcModel.BuildSettings.bRemoveDegenerates = false;
				SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
				SrcModel.BuildSettings.bUseFullPrecisionUVs = false;
				SrcModel.BuildSettings.bGenerateLightmapUVs = InSettings.bGenerateLightMapUV;
				SrcModel.BuildSettings.MinLightmapResolution = InSettings.bComputedLightMapResolution ? DataTracker.GetLightMapDimension() : InSettings.TargetLightMapResolution;
				SrcModel.BuildSettings.SrcLightmapIndex = 0;
				SrcModel.BuildSettings.DstLightmapIndex = LightMapUVChannel;
				if(!InSettings.bAllowDistanceField)
				{
					SrcModel.BuildSettings.DistanceFieldResolutionScale = 0.0f;
				}

				if (bContainsImposters)
				{
					// Merge imposter meshes to rawmesh
					FMeshMergeHelpers::MergeImpostersToRawMesh(ImposterComponents, MergedMeshLOD, MergedAssetPivot, UniqueMaterials.Num(), ImposterMaterials);					

					const FTransform PivotTransform = FTransform(MergedAssetPivot);
					for (const UStaticMeshComponent* Component : ImposterComponents)
					{
						if (Component->GetStaticMesh())
						{
							ImposterBounds += Component->GetStaticMesh()->GetBoundingBox().TransformBy(Component->GetComponentToWorld().GetRelativeTransform(PivotTransform));
						}
					}
				}

				FMeshDescription* MeshDescription = StaticMesh->CreateMeshDescription(LODIndex, MergedMeshLOD);
				StaticMesh->CommitMeshDescription(LODIndex);
			}
		}
		
		auto IsMaterialImportedNameUnique = [&StaticMesh](FName ImportedMaterialSlotName)
		{
			for (const FStaticMaterial& StaticMaterial : StaticMesh->StaticMaterials)
			{
#if WITH_EDITOR
				if (StaticMaterial.ImportedMaterialSlotName == ImportedMaterialSlotName)
#else
				if (StaticMaterial.MaterialSlotName == ImportedMaterialSlotName)
#endif
				{
					return false;
				}
			}
			return true;
		};
		

		for (UMaterialInterface* Material : UniqueMaterials)
		{
			if (Material && (!Material->IsAsset() && InOuter != GetTransientPackage()))
			{
				Material = nullptr; // do not save non-asset materials
			}
			//Make sure we have unique slot name here
			FName MaterialSlotName = DataTracker.GetMaterialSlotName(Material);
			int32 Counter = 1;
			while (!IsMaterialImportedNameUnique(MaterialSlotName))
			{
				MaterialSlotName = *(DataTracker.GetMaterialSlotName(Material).ToString() + TEXT("_") + FString::FromInt(Counter++));
			}

			StaticMesh->StaticMaterials.Add(FStaticMaterial(Material, MaterialSlotName));
		}

		for(UMaterialInterface* ImposterMaterial : ImposterMaterials)
		{
			//Make sure we have unique slot name here
			FName MaterialSlotName = ImposterMaterial->GetFName();
			int32 Counter = 1;
			while (!IsMaterialImportedNameUnique(MaterialSlotName))
			{
				MaterialSlotName = *(ImposterMaterial->GetName() + TEXT("_") + FString::FromInt(Counter++));
			}
			StaticMesh->StaticMaterials.Add(FStaticMaterial(ImposterMaterial, MaterialSlotName));
		}

		if (InSettings.bMergePhysicsData)
		{
			StaticMesh->CreateBodySetup();
			if (BodySetupSource)
			{
				StaticMesh->BodySetup->CopyBodyPropertiesFrom(BodySetupSource);
			}

			StaticMesh->BodySetup->AggGeom = FKAggregateGeom();
			// Copy collision from the source meshes
			for (const FKAggregateGeom& Geom : PhysicsGeometry)
			{
				StaticMesh->BodySetup->AddCollisionFrom(Geom);
			}

			// Bake rotation into verts of convex hulls, so they scale correctly after rotation
			for (FKConvexElem& ConvexElem : StaticMesh->BodySetup->AggGeom.ConvexElems)
			{
				ConvexElem.BakeTransformToVerts();
			}
		}

		StaticMesh->GetSectionInfoMap().CopyFrom(SectionInfoMap);
		StaticMesh->GetOriginalSectionInfoMap().CopyFrom(SectionInfoMap);

		//Set the Imported version before calling the build
		StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
		StaticMesh->LightMapResolution = InSettings.bComputedLightMapResolution ? DataTracker.GetLightMapDimension() : InSettings.TargetLightMapResolution;

#if WITH_EDITOR
		//If we are running the automation test
		if (GIsAutomationTesting)
		{
			StaticMesh->BuildCacheAutomationTestGuid = FGuid::NewGuid();
		}
#endif
		StaticMesh->Build(bSilent);

		if (ImposterBounds.IsValid)
		{
			const FBox StaticMeshBox = StaticMesh->GetBoundingBox();
			const FBox CombinedBox = StaticMeshBox + ImposterBounds;
			StaticMesh->PositiveBoundsExtension = (CombinedBox.Max - StaticMeshBox.Max);
			StaticMesh->NegativeBoundsExtension = (StaticMeshBox.Min - CombinedBox.Min);
			StaticMesh->CalculateExtendedBounds();
		}		

		StaticMesh->PostEditChange();

		if (InSettings.bCreateMergedMaterial && MergedMaterial)
		{
			//Make sure we have unique slot name here
			FName MaterialSlotName = MergedMaterial->GetFName();
			int32 Counter = 1;
			while (!IsMaterialImportedNameUnique(MaterialSlotName))
			{
				MaterialSlotName = *(MergedMaterial->GetName() + TEXT("_") + FString::FromInt(Counter++));
			}
			StaticMesh->StaticMaterials.Add(FStaticMaterial(MergedMaterial, MaterialSlotName));
			StaticMesh->UpdateUVChannelData(false);
		}

		OutAssetsToSync.Add(StaticMesh);
		OutMergedActorLocation = MergedAssetPivot;
	}
}

void FMeshMergeUtilities::ExtractImposterToRawMesh(const UStaticMeshComponent* InImposterComponent, FMeshDescription& InImposterMesh) const
{
	check(InImposterComponent->bUseMaxLODAsImposter);
	FMeshMergeHelpers::ExtractImposterToRawMesh(InImposterComponent, InImposterMesh);
}

void FMeshMergeUtilities::CreateMergedRawMeshes(FMeshMergeDataTracker& InDataTracker, const FMeshMergingSettings& InSettings, const TArray<UStaticMeshComponent*>& InStaticMeshComponentsToMerge, const TArray<UMaterialInterface*>& InUniqueMaterials, const TMap<UMaterialInterface*, UMaterialInterface*>& InCollapsedMaterialMap, const TMultiMap<FMeshLODKey, MaterialRemapPair>& InOutputMaterialsMap, bool bInMergeAllLODs, bool bInMergeMaterialData, const FVector& InMergedAssetPivot, TArray<FMeshDescription>& OutMergedRawMeshes) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshMergeUtilities::CreateMergedRawMeshes)

	if (bInMergeAllLODs)
	{
		OutMergedRawMeshes.AddDefaulted(InDataTracker.GetNumLODsForMergedMesh());
		for (TConstLODIndexIterator Iterator = InDataTracker.GetLODIndexIterator(); Iterator; ++Iterator)
		{
			// Find meshes for each lod
			const int32 LODIndex = *Iterator;
			FMeshDescription& MergedMesh = OutMergedRawMeshes[LODIndex];
			FStaticMeshAttributes(MergedMesh).Register();

			for (int32 ComponentIndex = 0; ComponentIndex < InStaticMeshComponentsToMerge.Num(); ++ComponentIndex)
			{
				int32 RetrievedLODIndex = LODIndex;
				FMeshDescription* RawMeshPtr = InDataTracker.TryFindRawMeshForLOD(ComponentIndex, RetrievedLODIndex);


				if (RawMeshPtr != nullptr)
				{
					InDataTracker.AddComponentToWedgeMapping(ComponentIndex, LODIndex, MergedMesh.VertexInstances().Num());

					FStaticMeshOperations::FAppendSettings AppendSettings;

					AppendSettings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda([&bInMergeMaterialData, &InDataTracker, &InOutputMaterialsMap, &ComponentIndex, &LODIndex](const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroups)
					{
						TPolygonGroupAttributesConstRef<FName> SourceImportedMaterialSlotNames = SourceMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
						TPolygonGroupAttributesRef<FName> TargetImportedMaterialSlotNames = TargetMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
						//Copy the polygon group
						if (bInMergeMaterialData)
						{
							FPolygonGroupID PolygonGroupID(0);
							if (!TargetMesh.PolygonGroups().IsValid(PolygonGroupID))
							{
								TargetMesh.CreatePolygonGroupWithID(PolygonGroupID);
								TargetImportedMaterialSlotNames[PolygonGroupID] = SourceMesh.PolygonGroups().IsValid(PolygonGroupID) ? SourceImportedMaterialSlotNames[PolygonGroupID] : FName(TEXT("DefaultMaterialName"));
							}
							for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
							{
								RemapPolygonGroups.Add(SourcePolygonGroupID, PolygonGroupID);
							}
						}
						else
						{
							TArray<SectionRemapPair> SectionMappings;
							InDataTracker.GetMappingsForMeshLOD(FMeshLODKey(ComponentIndex, LODIndex), SectionMappings);
							for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
							{
								// First map from original section index to unique material index
								int32 UniqueIndex = INDEX_NONE;
								// then map to the output material map, if any
								if (InOutputMaterialsMap.Num() > 0)
								{
									TArray<MaterialRemapPair> MaterialMappings;
									InOutputMaterialsMap.MultiFind(FMeshLODKey(ComponentIndex, LODIndex), MaterialMappings);
									for (MaterialRemapPair& Pair : MaterialMappings)
									{
										if (Pair.Key == SourcePolygonGroupID.GetValue())
										{
											UniqueIndex = Pair.Value;
											break;
										}
									}

									// Note that at this point UniqueIndex is NOT a material index, but a unique section index!
								}
								
								if(UniqueIndex == INDEX_NONE)
								{
									UniqueIndex = SourcePolygonGroupID.GetValue();
								}
								FPolygonGroupID TargetPolygonGroupID(UniqueIndex);
								if (!TargetMesh.PolygonGroups().IsValid(TargetPolygonGroupID))
								{
									while (TargetMesh.PolygonGroups().Num() <= UniqueIndex)
									{
										TargetPolygonGroupID = TargetMesh.CreatePolygonGroup();
									}
									check(TargetPolygonGroupID.GetValue() == UniqueIndex);
									TargetImportedMaterialSlotNames[TargetPolygonGroupID] = SourceImportedMaterialSlotNames[SourcePolygonGroupID];
								}
								RemapPolygonGroups.Add(SourcePolygonGroupID, TargetPolygonGroupID);
							}
						}
					});
					AppendSettings.bMergeVertexColor = InSettings.bBakeVertexDataToMesh;
					AppendSettings.MergedAssetPivot = InMergedAssetPivot;
					FStaticMeshOperations::AppendMeshDescription(*RawMeshPtr, MergedMesh, AppendSettings);
				}
			}

			//Cleanup the empty material to avoid empty section later
			TArray<FPolygonGroupID> PolygonGroupToRemove;
			for (FPolygonGroupID PolygonGroupID : MergedMesh.PolygonGroups().GetElementIDs())
			{
				if (MergedMesh.GetPolygonGroupPolygons(PolygonGroupID).Num() < 1)
				{
					PolygonGroupToRemove.Add(PolygonGroupID);
					
				}
			}
			for (FPolygonGroupID PolygonGroupID : PolygonGroupToRemove)
			{
				MergedMesh.DeletePolygonGroup(PolygonGroupID);
			}
		}
	}
	else
	{	
		FMeshDescription& MergedMesh = OutMergedRawMeshes.AddDefaulted_GetRef();
		FStaticMeshAttributes(MergedMesh).Register();

		for (int32 ComponentIndex = 0; ComponentIndex < InStaticMeshComponentsToMerge.Num(); ++ComponentIndex)
		{
			int32 LODIndex = 0;
		
			FMeshDescription* RawMeshPtr = InDataTracker.FindRawMeshAndLODIndex(ComponentIndex, LODIndex);

			if (RawMeshPtr != nullptr)
			{
				FMeshDescription& RawMesh = *RawMeshPtr;

				const int32 TargetLODIndex = 0;
				InDataTracker.AddComponentToWedgeMapping(ComponentIndex, TargetLODIndex, MergedMesh.VertexInstances().Num());

				FStaticMeshOperations::FAppendSettings AppendSettings;

				AppendSettings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda([&bInMergeMaterialData, &InDataTracker, &InOutputMaterialsMap, &ComponentIndex, &LODIndex](const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroups)
				{
					TPolygonGroupAttributesConstRef<FName> SourceImportedMaterialSlotNames = SourceMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
					TPolygonGroupAttributesRef<FName> TargetImportedMaterialSlotNames = TargetMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
					//Copy the polygon group
					if (bInMergeMaterialData)
					{
						FPolygonGroupID PolygonGroupID(0);
						if (!TargetMesh.PolygonGroups().IsValid(PolygonGroupID))
						{
							TargetMesh.CreatePolygonGroupWithID(PolygonGroupID);
							TargetImportedMaterialSlotNames[PolygonGroupID] = SourceMesh.PolygonGroups().IsValid(PolygonGroupID) ? SourceImportedMaterialSlotNames[PolygonGroupID] : FName(TEXT("DefaultMaterialName"));
						}
						for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
						{
							RemapPolygonGroups.Add(SourcePolygonGroupID, PolygonGroupID);
						}
					}
					else
					{
						TArray<SectionRemapPair> SectionMappings;
						InDataTracker.GetMappingsForMeshLOD(FMeshLODKey(ComponentIndex, LODIndex), SectionMappings);
						for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
						{
							// First map from original section index to unique material index
							int32 UniqueIndex = INDEX_NONE;
							// then map to the output material map, if any
							if (InOutputMaterialsMap.Num() > 0)
							{
								TArray<MaterialRemapPair> MaterialMappings;
								InOutputMaterialsMap.MultiFind(FMeshLODKey(ComponentIndex, LODIndex), MaterialMappings);
								for (MaterialRemapPair& Pair : MaterialMappings)
								{
									if (Pair.Key == SourcePolygonGroupID.GetValue())
									{
										UniqueIndex = Pair.Value;
										break;
									}
								}

								// Note that at this point UniqueIndex is NOT a material index, but a unique section index!
							}
							
							//Fallback
							if(UniqueIndex == INDEX_NONE)
							{
								UniqueIndex = SourcePolygonGroupID.GetValue();
							}

							FPolygonGroupID TargetPolygonGroupID(UniqueIndex);
							if (!TargetMesh.PolygonGroups().IsValid(TargetPolygonGroupID))
							{
								while (TargetMesh.PolygonGroups().Num() <= UniqueIndex)
								{
									TargetPolygonGroupID = TargetMesh.CreatePolygonGroup();
								}
								check(TargetPolygonGroupID.GetValue() == UniqueIndex);
								TargetImportedMaterialSlotNames[TargetPolygonGroupID] = SourceImportedMaterialSlotNames[SourcePolygonGroupID];
							}
							RemapPolygonGroups.Add(SourcePolygonGroupID, TargetPolygonGroupID);
						}
					}
				});
				AppendSettings.bMergeVertexColor = InSettings.bBakeVertexDataToMesh;
				AppendSettings.MergedAssetPivot = InMergedAssetPivot;
				FStaticMeshOperations::AppendMeshDescription(*RawMeshPtr, MergedMesh, AppendSettings);
			}
		}
	}

	for (IMeshMergeExtension* Extension : MeshMergeExtensions)
	{
		Extension->OnCreatedMergedRawMeshes(InStaticMeshComponentsToMerge, InDataTracker, OutMergedRawMeshes);
	}
}

void FMeshMergeUtilities::MergeComponentsToInstances(const TArray<UPrimitiveComponent*>& ComponentsToMerge, UWorld* World, ULevel* Level, const FMeshInstancingSettings& InSettings, bool bActuallyMerge /*= true*/, FText* OutResultsText /*= nullptr*/) const
{
	auto HasInstanceVertexColors = [](UStaticMeshComponent* StaticMeshComponent)
	{
		for (const FStaticMeshComponentLODInfo& CurrentLODInfo : StaticMeshComponent->LODData)
		{
			if(CurrentLODInfo.OverrideVertexColors != nullptr || CurrentLODInfo.PaintedVertices.Num() > 0)
			{
				return true;
			}
		}

		return false;
	};

	// Gather valid components
	TArray<UStaticMeshComponent*> ValidComponents;
	for(UPrimitiveComponent* ComponentToMerge : ComponentsToMerge)
	{
		if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ComponentToMerge))
		{
			// Dont harvest from 'destination' actors
			if(StaticMeshComponent->GetOwner()->GetClass() != InSettings.ActorClassToUse.Get())
			{
				if( !InSettings.bSkipMeshesWithVertexColors || !HasInstanceVertexColors(StaticMeshComponent))
				{
					ValidComponents.Add(StaticMeshComponent);
				}
			}
		}
	}

	if(OutResultsText != nullptr)
	{
		*OutResultsText = LOCTEXT("InstanceMergePredictedResultsNone", "The current settings will not result in any instanced meshes being created");
	}

	if(ValidComponents.Num() > 0)
	{
		/** Helper struct representing a spawned ISMC */
		struct FComponentEntry
		{
			FComponentEntry(UStaticMeshComponent* InComponent)
			{
				StaticMesh = InComponent->GetStaticMesh();
				InComponent->GetUsedMaterials(Materials);
				bReverseCulling = InComponent->GetComponentTransform().ToMatrixWithScale().Determinant() < 0.0f;
				CollisionProfileName = InComponent->GetCollisionProfileName();
				CollisionEnabled = InComponent->GetCollisionEnabled();
				OriginalComponents.Add(InComponent);
			}

			bool operator==(const FComponentEntry& InOther) const
			{
				return 
					StaticMesh == InOther.StaticMesh &&
					Materials == InOther.Materials &&
					bReverseCulling == InOther.bReverseCulling && 
					CollisionProfileName == InOther.CollisionProfileName &&
					CollisionEnabled == InOther.CollisionEnabled;
			}

			UStaticMesh* StaticMesh;

			TArray<UMaterialInterface*> Materials;

			TArray<UStaticMeshComponent*> OriginalComponents;

			FName CollisionProfileName;

			bool bReverseCulling;

			ECollisionEnabled::Type CollisionEnabled;
		};

		/** Helper struct representing a spawned ISMC-containing actor */
		struct FActorEntry
		{
			FActorEntry(UStaticMeshComponent* InComponent, ULevel* InLevel)
				: MergedActor(nullptr)
			{
				// intersect with HLOD volumes if we have a level
				if(InLevel)
				{
					for (AActor* Actor : InLevel->Actors)
					{
						if (AHierarchicalLODVolume* HierarchicalLODVolume = Cast<AHierarchicalLODVolume>(Actor))
						{
							FBox BoundingBox = InComponent->Bounds.GetBox();
							FBox VolumeBox = HierarchicalLODVolume->GetComponentsBoundingBox(true);

							if (VolumeBox.IsInside(BoundingBox) || (HierarchicalLODVolume->bIncludeOverlappingActors && VolumeBox.Intersect(BoundingBox)))
							{
								HLODVolume = HierarchicalLODVolume;
								break;
							}
						}
					}
				}
			}

			bool operator==(const FActorEntry& InOther) const
			{
				return HLODVolume == InOther.HLODVolume;
			}

			AActor* MergedActor;
			AHierarchicalLODVolume* HLODVolume;
			TArray<FComponentEntry> ComponentEntries;
		};

		// Gather a list of components to merge
		TArray<FActorEntry> ActorEntries;
		for(UStaticMeshComponent* StaticMeshComponent : ValidComponents)
		{
			int32 ActorEntryIndex = ActorEntries.AddUnique(FActorEntry(StaticMeshComponent, InSettings.bUseHLODVolumes ? Level : nullptr));
			FActorEntry& ActorEntry = ActorEntries[ActorEntryIndex];

			FComponentEntry ComponentEntry(StaticMeshComponent);

			if(FComponentEntry* ExistingComponentEntry = ActorEntry.ComponentEntries.FindByKey(ComponentEntry))
			{
				ExistingComponentEntry->OriginalComponents.Add(StaticMeshComponent);
			}
			else
			{
				ActorEntry.ComponentEntries.Add(ComponentEntry);
			}
		}

		// Filter by component count
		for(FActorEntry& ActorEntry : ActorEntries)
		{
			ActorEntry.ComponentEntries = ActorEntry.ComponentEntries.FilterByPredicate([&InSettings](const FComponentEntry& InEntry)
			{
				return InEntry.OriginalComponents.Num() >= InSettings.InstanceReplacementThreshold;
			});
		}

		// Remove any empty actor entries
		ActorEntries.RemoveAll([](const FActorEntry& ActorEntry){ return ActorEntry.ComponentEntries.Num() == 0; });

		int32 TotalComponentCount = 0;
		TArray<AActor*> ActorsToCleanUp;
		for(FActorEntry& ActorEntry : ActorEntries)
		{
			for(const FComponentEntry& ComponentEntry : ActorEntry.ComponentEntries)
			{
				TotalComponentCount++;
				for(UStaticMeshComponent* OriginalComponent : ComponentEntry.OriginalComponents)
				{
					if(AActor* OriginalActor = OriginalComponent->GetOwner())
					{
						ActorsToCleanUp.AddUnique(OriginalActor);
					}
				}
			}
		}

		if(ActorEntries.Num() > 0)
		{
			if(OutResultsText != nullptr)
			{
				*OutResultsText = FText::Format(LOCTEXT("InstanceMergePredictedResults", "The current settings will result in {0} instanced static mesh components ({1} actors will be replaced)"), FText::AsNumber(TotalComponentCount), FText::AsNumber(ActorsToCleanUp.Num()));
			}
			
			if(bActuallyMerge)
			{
				// Create our actors
				const FScopedTransaction Transaction(LOCTEXT("PlaceInstancedActors", "Place Instanced Actor(s)"));
				Level->Modify();

 				FActorSpawnParameters Params;
 				Params.OverrideLevel = Level;

				// We now have the set of component data we want to apply
				for(FActorEntry& ActorEntry : ActorEntries)
				{
					ActorEntry.MergedActor = World->SpawnActor<AActor>(InSettings.ActorClassToUse.Get(), Params);

					for(const FComponentEntry& ComponentEntry : ActorEntry.ComponentEntries)
					{
						UInstancedStaticMeshComponent* NewComponent = nullptr;

						NewComponent = (UInstancedStaticMeshComponent*)ActorEntry.MergedActor->FindComponentByClass(InSettings.ISMComponentToUse.Get());

						if (NewComponent && NewComponent->PerInstanceSMData.Num() > 0)
						{
							NewComponent = nullptr;
						}

						if (NewComponent == nullptr)
						{
							NewComponent = NewObject<UInstancedStaticMeshComponent>(ActorEntry.MergedActor, InSettings.ISMComponentToUse.Get());
						
							if (ActorEntry.MergedActor->GetRootComponent())
							{
								// Attach to root if we already have one
								NewComponent->AttachToComponent(ActorEntry.MergedActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
							}
							else
							{
								// Make a new root if we dont have a root already
								ActorEntry.MergedActor->SetRootComponent(NewComponent);
							}

							// Take 'instanced' ownership so it persists with this actor
							ActorEntry.MergedActor->RemoveOwnedComponent(NewComponent);
							NewComponent->CreationMethod = EComponentCreationMethod::Instance;
							ActorEntry.MergedActor->AddOwnedComponent(NewComponent);

						}

						NewComponent->SetStaticMesh(ComponentEntry.StaticMesh);
						for(int32 MaterialIndex = 0; MaterialIndex < ComponentEntry.Materials.Num(); ++MaterialIndex)
						{
							NewComponent->SetMaterial(MaterialIndex, ComponentEntry.Materials[MaterialIndex]);
						}
						NewComponent->SetReverseCulling(ComponentEntry.bReverseCulling);
						NewComponent->SetCollisionProfileName(ComponentEntry.CollisionProfileName);
						NewComponent->SetCollisionEnabled(ComponentEntry.CollisionEnabled);
						NewComponent->SetMobility(EComponentMobility::Static);
						for(UStaticMeshComponent* OriginalComponent : ComponentEntry.OriginalComponents)
						{
							NewComponent->AddInstance(OriginalComponent->GetComponentTransform());
						}

						NewComponent->RegisterComponent();
					}

					World->UpdateCullDistanceVolumes(ActorEntry.MergedActor);
				}

				// Now clean up our original actors
				for(AActor* ActorToCleanUp : ActorsToCleanUp)
				{
					if(InSettings.MeshReplacementMethod == EMeshInstancingReplacementMethod::RemoveOriginalActors)
					{
						ActorToCleanUp->Destroy();
					}
					else if(InSettings.MeshReplacementMethod == EMeshInstancingReplacementMethod::KeepOriginalActorsAsEditorOnly)
					{
						ActorToCleanUp->Modify();
						ActorToCleanUp->bIsEditorOnlyActor = true;
						ActorToCleanUp->SetHidden(true);
						ActorToCleanUp->bHiddenEd = true;
						ActorToCleanUp->SetIsTemporarilyHiddenInEditor(true);
					}
				}

				// pop a toast allowing selection
				auto SelectActorsLambda = [ActorEntries]()
				{ 
					GEditor->GetSelectedActors()->Modify();
					GEditor->GetSelectedActors()->BeginBatchSelectOperation();
					GEditor->SelectNone(false, true, false);

					for(const FActorEntry& ActorEntry : ActorEntries)
					{
						GEditor->SelectActor(ActorEntry.MergedActor, true, false, true);
					}

					GEditor->GetSelectedActors()->EndBatchSelectOperation();
				};

				FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("CreatedInstancedActorsMessage", "Created {0} Instanced Actor(s)"), FText::AsNumber(ActorEntries.Num())));
				NotificationInfo.Hyperlink = FSimpleDelegate::CreateLambda(SelectActorsLambda);
				NotificationInfo.HyperlinkText = LOCTEXT("SelectActorsHyperlink", "Select Actors");
				NotificationInfo.ExpireDuration = 5.0f;

				FSlateNotificationManager::Get().AddNotification(NotificationInfo);
			}
		}
	}
}

UMaterialInterface* FMeshMergeUtilities::CreateProxyMaterial(const FString &InBasePackageName, FString MergedAssetPackageName, UMaterialInterface* InBaseMaterial, UPackage* InOuter, const FMeshMergingSettings &InSettings, FFlattenMaterial OutMaterial, TArray<UObject *>& OutAssetsToSync) const
{
	// Create merged material asset
	FString MaterialAssetName;
	FString MaterialPackageName;
	if (InBasePackageName.IsEmpty())
	{
		MaterialAssetName = FPackageName::GetShortName(MergedAssetPackageName);
		MaterialPackageName = FPackageName::GetLongPackagePath(MergedAssetPackageName) + TEXT("/");
	}
	else
	{
		MaterialAssetName = FPackageName::GetShortName(InBasePackageName);
		MaterialPackageName = FPackageName::GetLongPackagePath(InBasePackageName) + TEXT("/");
	}

	UPackage* MaterialPackage = InOuter;
	if (MaterialPackage == nullptr)
	{
		MaterialPackage = CreatePackage(nullptr, *(MaterialPackageName + MaterialAssetName));
		check(MaterialPackage);
		MaterialPackage->FullyLoad();
		MaterialPackage->Modify();
	}

	UMaterialInstanceConstant* MergedMaterial = ProxyMaterialUtilities::CreateProxyMaterialInstance(MaterialPackage, InSettings.MaterialSettings, InBaseMaterial, OutMaterial, MaterialPackageName, MaterialAssetName, OutAssetsToSync);
	// Set material static lighting usage flag if project has static lighting enabled
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);
	if (bAllowStaticLighting)
	{
		MergedMaterial->CheckMaterialUsage(MATUSAGE_StaticLighting);
	}

	return MergedMaterial;
}

void FMeshMergeUtilities::ExtractPhysicsDataFromComponents(const TArray<UPrimitiveComponent*>& ComponentsToMerge, TArray<FKAggregateGeom>& InOutPhysicsGeometry, UBodySetup*& OutBodySetupSource) const
{
	InOutPhysicsGeometry.AddDefaulted(ComponentsToMerge.Num());
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentsToMerge.Num(); ++ComponentIndex)
	{
		UPrimitiveComponent* PrimComp = ComponentsToMerge[ComponentIndex];
		UBodySetup* BodySetup = nullptr;
		FTransform ComponentToWorld = FTransform::Identity;

		if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimComp))
		{
			UStaticMesh* SrcMesh = StaticMeshComp->GetStaticMesh();
			if (SrcMesh)
			{
				BodySetup = SrcMesh->BodySetup;
			}
			ComponentToWorld = StaticMeshComp->GetComponentToWorld();
		}
		else if (UShapeComponent* ShapeComp = Cast<UShapeComponent>(PrimComp))
		{
			BodySetup = ShapeComp->GetBodySetup();
			ComponentToWorld = ShapeComp->GetComponentToWorld();
		}

		USplineMeshComponent* SplineMeshComponent = Cast<USplineMeshComponent>(PrimComp);
		FMeshMergeHelpers::ExtractPhysicsGeometry(BodySetup, ComponentToWorld, SplineMeshComponent != nullptr, InOutPhysicsGeometry[ComponentIndex]);
		if (SplineMeshComponent)
		{
			FMeshMergeHelpers::PropagateSplineDeformationToPhysicsGeometry(SplineMeshComponent, InOutPhysicsGeometry[ComponentIndex]);
		}

		// We will use first valid BodySetup as a source of physics settings
		if (OutBodySetupSource == nullptr)
		{
			OutBodySetupSource = BodySetup;
		}
	}
}

void FMeshMergeUtilities::ScaleTextureCoordinatesToBox(const FBox2D& Box, TArray<FVector2D>& InOutTextureCoordinates) const
{
	const FBox2D CoordinateBox(InOutTextureCoordinates);
	const FVector2D CoordinateRange = CoordinateBox.GetSize();
	const FVector2D Offset = CoordinateBox.Min + Box.Min;
	const FVector2D Scale = Box.GetSize() / CoordinateRange;
	for (FVector2D& Coordinate : InOutTextureCoordinates)
	{
		Coordinate = (Coordinate - Offset) * Scale;
	}

}

#undef LOCTEXT_NAMESPACE // "MeshMergeUtils"
