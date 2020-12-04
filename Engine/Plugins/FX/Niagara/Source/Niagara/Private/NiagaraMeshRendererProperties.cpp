// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererMeshes.h"
#include "Engine/StaticMesh.h"
#include "NiagaraConstants.h"
#include "NiagaraBoundsCalculatorHelper.h"
#include "NiagaraCustomVersion.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SWidget.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/STextBlock.h"
#endif


#define LOCTEXT_NAMESPACE "UNiagaraMeshRendererProperties"


TArray<TWeakObjectPtr<UNiagaraMeshRendererProperties>> UNiagaraMeshRendererProperties::MeshRendererPropertiesToDeferredInit;

FNiagaraMeshMaterialOverride::FNiagaraMeshMaterialOverride()
	: ExplicitMat(nullptr)
{
	FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
	UserParamBinding.Parameter.SetType(MaterialDef);
}

bool FNiagaraMeshMaterialOverride::SerializeFromMismatchedTag(const struct FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	// We have to handle the fact that UNiagaraMeshRendererProperties OverrideMaterials just used to be an array of UMaterialInterfaces
	if (Tag.Type == NAME_ObjectProperty)
	{
		Slot << ExplicitMat;
		return true;
	}

	return false;
}


UNiagaraMeshRendererProperties::UNiagaraMeshRendererProperties()
	: ParticleMesh(nullptr)
	, SortMode(ENiagaraSortMode::None)
	, bOverrideMaterials(false)
	, bSortOnlyWhenTranslucent(true)
	, SubImageSize(1.0f, 1.0f)
	, bSubImageBlend(false)
	, FacingMode(ENiagaraMeshFacingMode::Default)
	, bLockedAxisEnable(false)
	, LockedAxis(0.0f, 0.0f, 1.0f)
	, LockedAxisSpace(ENiagaraMeshLockedAxisSpace::Simulation)
{
	AttributeBindings.Reserve(15);
	AttributeBindings.Add(&PositionBinding);
	AttributeBindings.Add(&ColorBinding);
	AttributeBindings.Add(&VelocityBinding);
	AttributeBindings.Add(&MeshOrientationBinding);
	AttributeBindings.Add(&ScaleBinding);
	AttributeBindings.Add(&SubImageIndexBinding);
	AttributeBindings.Add(&DynamicMaterialBinding);
	AttributeBindings.Add(&DynamicMaterial1Binding);
	AttributeBindings.Add(&DynamicMaterial2Binding);
	AttributeBindings.Add(&DynamicMaterial3Binding);
	AttributeBindings.Add(&MaterialRandomBinding);
	AttributeBindings.Add(&CustomSortingBinding);
	AttributeBindings.Add(&NormalizedAgeBinding);
	AttributeBindings.Add(&CameraOffsetBinding);
	AttributeBindings.Add(&RendererVisibilityTagBinding);
}

FNiagaraRenderer* UNiagaraMeshRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent)
{
	if (ParticleMesh)
	{
		FNiagaraRenderer* NewRenderer = new FNiagaraRendererMeshes(FeatureLevel, this, Emitter);
		NewRenderer->Initialize(this, Emitter, InComponent);
		return NewRenderer;
	}

	return nullptr;
}

FNiagaraBoundsCalculator* UNiagaraMeshRendererProperties::CreateBoundsCalculator()
{
	if (ParticleMesh)
	{
		FBox LocalBounds = ParticleMesh->GetBounds().GetBox();
		FVector MeshOffset(ForceInitToZero);
		ENiagaraBoundsMeshOffsetTransform MeshOffsetTransform = ENiagaraBoundsMeshOffsetTransform::None;
		if (PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Mesh)
		{
			// Offset the local bounds
			LocalBounds = LocalBounds.ShiftBy(PivotOffset);
		}
		else
		{
			// Offset is in either system-local or world space, and we need to decide how to transform it, if at all
			MeshOffset = PivotOffset;

			if (PivotOffsetSpace != ENiagaraMeshPivotOffsetSpace::Simulation)
			{
				bool bLocalSpace = false;
				if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(GetOuter()))
				{
					bLocalSpace = Emitter->bLocalSpace;
				}

				if (bLocalSpace && PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::World)
				{
					MeshOffsetTransform = ENiagaraBoundsMeshOffsetTransform::WorldToLocal;
				}
				else if (!bLocalSpace && PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Local)
				{
					MeshOffsetTransform = ENiagaraBoundsMeshOffsetTransform::LocalToWorld;
				}
			}			
		}

		// Take the bounding center into account with the extents, as it may not be at the origin
		const FVector Extents = LocalBounds.Max.GetAbs().ComponentMax(LocalBounds.Min.GetAbs());
		FNiagaraBoundsCalculatorHelper<false, true, false>* BoundsCalculator
			= new FNiagaraBoundsCalculatorHelper<false, true, false>(Extents, MeshOffset, MeshOffsetTransform);
		return BoundsCalculator;
	}

	return nullptr;

}

void UNiagaraMeshRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			MeshRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		InitBindings();
	}
}

void UNiagaraMeshRendererProperties::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
	const int32 NiagaraVersion = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	if (Ar.IsLoading() && (NiagaraVersion < FNiagaraCustomVersion::DisableSortingByDefault))
	{
		SortMode = ENiagaraSortMode::ViewDistance;
	}
	Super::Serialize(Ar);
}

/** The bindings depend on variables that are created during the NiagaraModule startup. However, the CDO's are build prior to this being initialized, so we defer setting these values until later.*/
void UNiagaraMeshRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNiagaraMeshRendererProperties* CDO = CastChecked<UNiagaraMeshRendererProperties>(UNiagaraMeshRendererProperties::StaticClass()->GetDefaultObject());
	CDO->InitBindings();

	for (TWeakObjectPtr<UNiagaraMeshRendererProperties>& WeakMeshRendererProperties : MeshRendererPropertiesToDeferredInit)
	{
		if (WeakMeshRendererProperties.Get())
		{
			WeakMeshRendererProperties->InitBindings();
		}
	}
}

void UNiagaraMeshRendererProperties::InitBindings()
{
	if (!PositionBinding.IsValid())
	{
		PositionBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		ColorBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COLOR);
		VelocityBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VELOCITY);
		SubImageIndexBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SUB_IMAGE_INDEX);
		DynamicMaterialBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		DynamicMaterial1Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		DynamicMaterial2Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		DynamicMaterial3Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		MeshOrientationBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MESH_ORIENTATION);
		ScaleBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SCALE);
		MaterialRandomBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
		NormalizedAgeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		CameraOffsetBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_CAMERA_OFFSET);
		RendererVisibilityTagBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VISIBILITY_TAG);

		//Default custom sorting to age
		CustomSortingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
	}
}

void UNiagaraMeshRendererProperties::CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData)
{
	// Initialize layout
	RendererLayoutWithCustomSorting.Initialize(ENiagaraMeshVFLayout::Num);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, PositionBinding, ENiagaraMeshVFLayout::Position);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, VelocityBinding, ENiagaraMeshVFLayout::Velocity);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, ColorBinding, ENiagaraMeshVFLayout::Color);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, ScaleBinding, ENiagaraMeshVFLayout::Scale);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, MeshOrientationBinding, ENiagaraMeshVFLayout::Transform);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, MaterialRandomBinding, ENiagaraMeshVFLayout::MaterialRandom);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, NormalizedAgeBinding, ENiagaraMeshVFLayout::NormalizedAge);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, CustomSortingBinding, ENiagaraMeshVFLayout::CustomSorting);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, SubImageIndexBinding, ENiagaraMeshVFLayout::SubImage);
	RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, CameraOffsetBinding, ENiagaraMeshVFLayout::CameraOffset);
	MaterialParamValidMask  = RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterialBinding, ENiagaraMeshVFLayout::DynamicParam0) ? 0x1 : 0;
	MaterialParamValidMask |= RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial1Binding, ENiagaraMeshVFLayout::DynamicParam1) ? 0x2 : 0;
	MaterialParamValidMask |= RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial2Binding, ENiagaraMeshVFLayout::DynamicParam2) ? 0x4 : 0;
	MaterialParamValidMask |= RendererLayoutWithCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial3Binding, ENiagaraMeshVFLayout::DynamicParam3) ? 0x8 : 0;
	RendererLayoutWithCustomSorting.Finalize();

	RendererLayoutWithoutCustomSorting.Initialize(ENiagaraMeshVFLayout::Num);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, PositionBinding, ENiagaraMeshVFLayout::Position);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, VelocityBinding, ENiagaraMeshVFLayout::Velocity);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, ColorBinding, ENiagaraMeshVFLayout::Color);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, ScaleBinding, ENiagaraMeshVFLayout::Scale);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, MeshOrientationBinding, ENiagaraMeshVFLayout::Transform);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, MaterialRandomBinding, ENiagaraMeshVFLayout::MaterialRandom);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, NormalizedAgeBinding, ENiagaraMeshVFLayout::NormalizedAge);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, SubImageIndexBinding, ENiagaraMeshVFLayout::SubImage);
	RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, CameraOffsetBinding, ENiagaraMeshVFLayout::CameraOffset);
	MaterialParamValidMask =  RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterialBinding, ENiagaraMeshVFLayout::DynamicParam0) ? 0x1 : 0;
	MaterialParamValidMask |= RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial1Binding, ENiagaraMeshVFLayout::DynamicParam1) ? 0x2 : 0;
	MaterialParamValidMask |= RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial2Binding, ENiagaraMeshVFLayout::DynamicParam2) ? 0x4 : 0;
	MaterialParamValidMask |= RendererLayoutWithoutCustomSorting.SetVariableFromBinding(CompiledData, DynamicMaterial3Binding, ENiagaraMeshVFLayout::DynamicParam3) ? 0x8 : 0;
	RendererLayoutWithoutCustomSorting.Finalize();
}

void UNiagaraMeshRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	if (ParticleMesh && ParticleMesh->RenderData)
	{
		const FStaticMeshLODResources& LODModel = ParticleMesh->RenderData->LODResources[0];
		if (bOverrideMaterials)
		{
			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
				UMaterialInterface* ParticleMeshMaterial = ParticleMesh->GetMaterial(Section.MaterialIndex);

				if (Section.MaterialIndex >= 0 && OverrideMaterials.Num() > Section.MaterialIndex)
				{
					bool bSet = false;
					
					// UserParamBinding, if mapped to a real value, always wins. Otherwise, use the ExplictMat if it is set. Finally, fall
					// back to the particle mesh material. This allows the user to effectively optionally bind to a Material binding
					// and still have good defaults if it isn't set to anything.
					if (InEmitter != nullptr && OverrideMaterials[Section.MaterialIndex].UserParamBinding.Parameter.IsValid() && InEmitter->FindBinding(OverrideMaterials[Section.MaterialIndex].UserParamBinding, OutMaterials))
					{
						bSet = true;
					}
					else if (OverrideMaterials[Section.MaterialIndex].ExplicitMat != nullptr)
					{
						bSet = true;
						OutMaterials.Add(OverrideMaterials[Section.MaterialIndex].ExplicitMat);
					}

					if (!bSet)
					{
						OutMaterials.Add(ParticleMeshMaterial);
					}
				}
				else
				{
					OutMaterials.Add(ParticleMeshMaterial);
				}
			}
		}
		else
		{
			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
				UMaterialInterface* ParticleMeshMaterial = ParticleMesh->GetMaterial(Section.MaterialIndex);
				OutMaterials.Add(ParticleMeshMaterial);
			}
		}
	}
}

uint32 UNiagaraMeshRendererProperties::GetNumIndicesPerInstance() const
{
	// TODO: Add proper support for multiple mesh sections for GPU mesh particles.
	//return ParticleMesh ? ParticleMesh->RenderData->LODResources[0].Sections[0].NumTriangles * 3 : 0;
	return ParticleMesh && ParticleMesh->RenderData ? ParticleMesh->RenderData->LODResources[0].IndexBuffer.GetNumIndices() : 0;
}

void UNiagaraMeshRendererProperties::GetIndexInfoPerSection(int32 LODIndex, TArray<TPair<int32, int32>>& IndexInfoPerSection) const
{
	check(ParticleMesh && ParticleMesh->RenderData && ParticleMesh->RenderData->LODResources.IsValidIndex(LODIndex));

	if (ParticleMesh && ParticleMesh->RenderData->LODResources.IsValidIndex(LODIndex))
	{
		const FStaticMeshLODResources& MeshLod = ParticleMesh->RenderData->LODResources[LODIndex];
		const int32 SectionCount = MeshLod.Sections.Num();
		IndexInfoPerSection.SetNum(SectionCount);

		for (int32 SectionIdx = 0; SectionIdx < SectionCount; ++SectionIdx)
		{
			IndexInfoPerSection[SectionIdx].Key = MeshLod.Sections[SectionIdx].NumTriangles * 3;
			IndexInfoPerSection[SectionIdx].Value = MeshLod.Sections[SectionIdx].FirstIndex;
		}
	}
}


void UNiagaraMeshRendererProperties::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	if (GIsEditor && (ParticleMesh != nullptr))
	{
		ParticleMesh->ConditionalPostLoad();
		ParticleMesh->GetOnMeshChanged().AddUObject(this, &UNiagaraMeshRendererProperties::OnMeshChanged);
		ParticleMesh->OnPostMeshBuild().AddUObject(this, &UNiagaraMeshRendererProperties::OnMeshPostBuild);
	}
#endif
	PostLoadBindings(ENiagaraRendererSourceDataMode::Particles);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraMeshRendererProperties::IsMaterialValidForRenderer(UMaterial* Material, FText& InvalidMessage)
{
	if (Material->bUsedWithNiagaraMeshParticles == false)
	{
		InvalidMessage = NSLOCTEXT("NiagaraMeshRendererProperties", "InvalidMaterialMessage", "The material isn't marked as \"Used with Niagara Mesh particles\"");
		return false;
	}
	return true;
}

void UNiagaraMeshRendererProperties::FixMaterial(UMaterial* Material)
{
	Material->Modify();
	Material->bUsedWithNiagaraMeshParticles = true;
	Material->ForceRecompileForRendering();
}

const TArray<FNiagaraVariable>& UNiagaraMeshRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_POSITION);
		Attrs.Add(SYS_PARAM_PARTICLES_VELOCITY);
		Attrs.Add(SYS_PARAM_PARTICLES_COLOR);
		Attrs.Add(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		Attrs.Add(SYS_PARAM_PARTICLES_SCALE);
		Attrs.Add(SYS_PARAM_PARTICLES_MESH_ORIENTATION);
		Attrs.Add(SYS_PARAM_PARTICLES_SUB_IMAGE_INDEX);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
	}

	return Attrs;
}

void UNiagaraMeshRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> ThumbnailWidget = SNullWidget::NullWidget;
	int32 ThumbnailSize = 32;
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	for (UMaterialInterface* Material : Materials)
	{
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(Material, ThumbnailSize, ThumbnailSize, InThumbnailPool));
		if (AssetThumbnail)
		{
			ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();
		}
		OutWidgets.Add(ThumbnailWidget);
	}

	if (Materials.Num() == 0)
	{
		TSharedRef<SWidget> SpriteWidget = SNew(SImage)
			.Image(FSlateIconFinder::FindIconBrushForClass(GetClass()));
		OutWidgets.Add(SpriteWidget);
	}
}

void UNiagaraMeshRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	if (Materials.Num() > 0)
	{
		GetRendererWidgets(InEmitter, OutWidgets, InThumbnailPool);
	}
	else
	{
		TSharedRef<SWidget> MeshTooltip = SNew(STextBlock)
			.Text(LOCTEXT("MeshRendererNoMat", "Mesh Renderer (No Material Set)"));
		OutWidgets.Add(MeshTooltip);
	}
}


void UNiagaraMeshRendererProperties::GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);
}

void UNiagaraMeshRendererProperties::BeginDestroy()
{
	Super::BeginDestroy();
#if WITH_EDITOR
	if (GIsEditor && (ParticleMesh != nullptr))
	{
		ParticleMesh->GetOnMeshChanged().RemoveAll(this);
		ParticleMesh->OnPostMeshBuild().RemoveAll(this);
	}
#endif
}

void UNiagaraMeshRendererProperties::PreEditChange(class FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	static FName ParticleMeshName(TEXT("ParticleMesh"));
	if ((PropertyThatWillChange != nullptr) && (PropertyThatWillChange->GetFName() == FName(ParticleMeshName)))
	{
		if (ParticleMesh != nullptr)
		{
			ParticleMesh->GetOnMeshChanged().RemoveAll(this);
			ParticleMesh->OnPostMeshBuild().RemoveAll(this);
		}
	}
}

void UNiagaraMeshRendererProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	SubImageSize.X = FMath::Max<float>(SubImageSize.X, 1.f);
	SubImageSize.Y = FMath::Max<float>(SubImageSize.Y, 1.f);

	static FName ParticleMeshName(TEXT("ParticleMesh"));

	if (ParticleMesh)
	{
		const bool IsRedirect = PropertyChangedEvent.ChangeType == EPropertyChangeType::Redirected;
		if (IsRedirect)
		{
			// Do this in case the redirected property is not ParticleMesh (we have no way of knowing b/c the property is nullptr)
			ParticleMesh->GetOnMeshChanged().RemoveAll(this);
			ParticleMesh->OnPostMeshBuild().RemoveAll(this);
		}
		if (IsRedirect || (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == ParticleMeshName))
		{
			// We only need to check material usage as we will invalidate any renderers later on
			CheckMaterialUsage();
			ParticleMesh->GetOnMeshChanged().AddUObject(this, &UNiagaraMeshRendererProperties::OnMeshChanged);
			ParticleMesh->OnPostMeshBuild().AddUObject(this, &UNiagaraMeshRendererProperties::OnMeshPostBuild);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNiagaraMeshRendererProperties::OnMeshChanged()
{
	FNiagaraSystemUpdateContext ReregisterContext;

	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(GetOuter());
	if (Emitter != nullptr)
	{
		ReregisterContext.Add(Emitter, true);
	}

	CheckMaterialUsage();
}

void UNiagaraMeshRendererProperties::OnMeshPostBuild(UStaticMesh*)
{
	OnMeshChanged();
}

void UNiagaraMeshRendererProperties::CheckMaterialUsage()
{
	if (ParticleMesh && ParticleMesh->RenderData)
	{
		const FStaticMeshLODResources& LODModel = ParticleMesh->RenderData->LODResources[0];
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			UMaterialInterface *Material = ParticleMesh->GetMaterial(Section.MaterialIndex);
			if (Material)
			{
				FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
				Material->CheckMaterialUsage(MATUSAGE_NiagaraMeshParticles);
			}
		}
	}
}

#endif // WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE