// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraRendererRibbons.h"
#include "NiagaraConstants.h"
#include "NiagaraBoundsCalculatorHelper.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SWidget.h"
#include "Styling/SlateBrush.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/STextBlock.h"
#endif



#define LOCTEXT_NAMESPACE "UNiagaraRibbonRendererProperties"

TArray<TWeakObjectPtr<UNiagaraRibbonRendererProperties>> UNiagaraRibbonRendererProperties::RibbonRendererPropertiesToDeferredInit;

UNiagaraRibbonRendererProperties::UNiagaraRibbonRendererProperties()
	: Material(nullptr)
	, FacingMode(ENiagaraRibbonFacingMode::Screen)
	, UV0TilingDistance(0.0f)
	, UV0Scale(FVector2D(1.0f, 1.0f))
	, UV0AgeOffsetMode(ENiagaraRibbonAgeOffsetMode::Scale)
	, UV1TilingDistance(0.0f)
	, UV1Scale(FVector2D(1.0f, 1.0f))
	, UV1AgeOffsetMode(ENiagaraRibbonAgeOffsetMode::Scale)
	, CurveTension(0.f)
	, TessellationMode(ENiagaraRibbonTessellationMode::Automatic)
	, TessellationFactor(16)
	, bUseConstantFactor(false)
	, TessellationAngle(15)
	, bScreenSpaceTessellation(true)
{
	FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
	MaterialUserParamBinding.Parameter.SetType(MaterialDef);

	AttributeBindings.Reserve(14);
	AttributeBindings.Add(&PositionBinding);
	AttributeBindings.Add(&ColorBinding);
	AttributeBindings.Add(&VelocityBinding);
	AttributeBindings.Add(&NormalizedAgeBinding);
	AttributeBindings.Add(&RibbonTwistBinding);
	AttributeBindings.Add(&RibbonWidthBinding);
	AttributeBindings.Add(&RibbonFacingBinding);
	AttributeBindings.Add(&RibbonIdBinding);
	AttributeBindings.Add(&RibbonLinkOrderBinding);
	AttributeBindings.Add(&MaterialRandomBinding);
	AttributeBindings.Add(&DynamicMaterialBinding);
	AttributeBindings.Add(&DynamicMaterial1Binding);
	AttributeBindings.Add(&DynamicMaterial2Binding);
	AttributeBindings.Add(&DynamicMaterial3Binding);
}

FNiagaraRenderer* UNiagaraRibbonRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter)
{
	FNiagaraRenderer* NewRenderer = new FNiagaraRendererRibbons(FeatureLevel, this, Emitter);
	NewRenderer->Initialize(this, Emitter);
	return NewRenderer;
}


void UNiagaraRibbonRendererProperties::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA

	if (MaterialUserParamBinding.Parameter.GetType().GetClass() != UMaterialInterface::StaticClass())
	{
		FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
		MaterialUserParamBinding.Parameter.SetType(MaterialDef);
	}
#endif
}

FNiagaraBoundsCalculator* UNiagaraRibbonRendererProperties::CreateBoundsCalculator()
{
	return new FNiagaraBoundsCalculatorHelper<false, false, true>();
}

void UNiagaraRibbonRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{

	bool bSet = false;
	if (InEmitter != nullptr && MaterialUserParamBinding.Parameter.IsValid() && InEmitter->FindBinding(MaterialUserParamBinding, OutMaterials))
	{
		bSet = true;
	}

	if (!bSet)
	{
		OutMaterials.Add(Material);
	}
}

void UNiagaraRibbonRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			RibbonRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		InitBindings();
	}
}

/** The bindings depend on variables that are created during the NiagaraModule startup. However, the CDO's are build prior to this being initialized, so we defer setting these values until later.*/
void UNiagaraRibbonRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNiagaraRibbonRendererProperties* CDO = CastChecked<UNiagaraRibbonRendererProperties>(UNiagaraRibbonRendererProperties::StaticClass()->GetDefaultObject());
	CDO->InitBindings();

	for (TWeakObjectPtr<UNiagaraRibbonRendererProperties>& WeakRibbonRendererProperties : RibbonRendererPropertiesToDeferredInit)
	{
		if (WeakRibbonRendererProperties.Get())
		{
			WeakRibbonRendererProperties->InitBindings();
		}
	}
}

void UNiagaraRibbonRendererProperties::InitBindings()
{
	if (PositionBinding.BoundVariable.GetName() == NAME_None)
	{
		PositionBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		ColorBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COLOR);
		VelocityBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VELOCITY);
		DynamicMaterialBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		DynamicMaterial1Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		DynamicMaterial2Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		DynamicMaterial3Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		NormalizedAgeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		RibbonTwistBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONTWIST);
		RibbonWidthBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONWIDTH);
		RibbonFacingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONFACING);
		RibbonIdBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONID);
		RibbonLinkOrderBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONLINKORDER);
		MaterialRandomBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraRibbonRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationAngle))
	{
		if (TessellationAngle > 0.f && TessellationAngle < 1.f)
		{
			TessellationAngle = 1.f;
		}
	}
}

const TArray<FNiagaraVariable>& UNiagaraRibbonRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_POSITION);
		Attrs.Add(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		Attrs.Add(SYS_PARAM_PARTICLES_COLOR);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONID);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONTWIST);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONWIDTH);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONFACING);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONLINKORDER);
	}

	return Attrs;
}


void UNiagaraRibbonRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> ThumbnailWidget = SNullWidget::NullWidget;
	int32 ThumbnailSize = 32;
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	for (UMaterialInterface* PreviewedMaterial : Materials)
	{
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(PreviewedMaterial, ThumbnailSize, ThumbnailSize, InThumbnailPool));
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

void UNiagaraRibbonRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	if (Materials.Num() > 0)
	{
		GetRendererWidgets(InEmitter, OutWidgets, InThumbnailPool);
	}
	else
	{
		TSharedRef<SWidget> RibbonTooltip = SNew(STextBlock)
			.Text(LOCTEXT("RibbonRendererNoMat", "Ribbon Renderer (No Material Set)"));
		OutWidgets.Add(RibbonTooltip);
	}
}


void UNiagaraRibbonRendererProperties::GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);
}


bool UNiagaraRibbonRendererProperties::IsMaterialValidForRenderer(UMaterial* InMaterial, FText& InvalidMessage)
{
	if (InMaterial->bUsedWithNiagaraRibbons == false)
	{
		InvalidMessage = NSLOCTEXT("NiagaraRibbonRendererProperties", "InvalidMaterialMessage", "The material isn't marked as \"Used with Niagara ribbons\"");
		return false;
	}
	return true;
}

void UNiagaraRibbonRendererProperties::FixMaterial(UMaterial* InMaterial)
{
	InMaterial->Modify();
	InMaterial->bUsedWithNiagaraRibbons = true;
	InMaterial->ForceRecompileForRendering();
}

bool UNiagaraRibbonRendererProperties::CanEditChange(const FProperty* InProperty) const
{

	if (InProperty->HasMetaData(TEXT("Category")) && InProperty->GetMetaData(TEXT("Category")).Contains("Tessellation"))
	{
		FName PropertyName = InProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, CurveTension))
		{
			return TessellationMode != ENiagaraRibbonTessellationMode::Disabled;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationFactor))
		{
			return TessellationMode == ENiagaraRibbonTessellationMode::Custom;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationMode))
		{
			return Super::CanEditChange(InProperty);
		}
		return TessellationMode == ENiagaraRibbonTessellationMode::Custom;
	}
	return Super::CanEditChange(InProperty);
}

#endif // WITH_EDITORONLY_DATA
#undef LOCTEXT_NAMESPACE