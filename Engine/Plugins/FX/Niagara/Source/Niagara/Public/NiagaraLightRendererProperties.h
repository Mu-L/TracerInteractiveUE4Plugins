// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraLightRendererProperties.generated.h"

class FNiagaraEmitterInstance;
class SWidget;

UCLASS(editinlinenew, MinimalAPI, meta = (DisplayName = "Light Renderer"))
class UNiagaraLightRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNiagaraLightRendererProperties();

	//UObject Interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//~ UNiagaraRendererProperties interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override { return nullptr; }
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return (InSimTarget == ENiagaraSimTarget::CPUSim); };
#if WITH_EDITORONLY_DATA
	virtual bool IsMaterialValidForRenderer(UMaterial* Material, FText& InvalidMessage) override;
	virtual void FixMaterial(UMaterial* Material) override;
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const override;
#endif // WITH_EDITORONLY_DATA
	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) override;
	//UNiagaraRendererProperties Interface END

	/** Whether to use physically based inverse squared falloff from the light.  If unchecked, the value from the LightExponent binding will be used instead. */
	UPROPERTY(EditAnywhere, Category = "Light Rendering")
	uint32 bUseInverseSquaredFalloff : 1;

	/**
	 * Whether lights from this renderer should affect translucency.
	 * Use with caution - if enabled, create only a few particle lights at most, and the smaller they are, the less they will cost.
	 */
	UPROPERTY(EditAnywhere, Category = "Light Rendering")
	uint32 bAffectsTranslucency : 1;

	/** A factor used to scale each particle light radius */
	UPROPERTY(EditAnywhere, Category = "Light Rendering", meta = (UIMin = "0"))
	float RadiusScale;

	/** A static color shift applied to each rendered light */
	UPROPERTY(EditAnywhere, Category = "Light Rendering")
	FVector ColorAdd;

	/** Which attribute should we use to check if light rendering should be enabled for a particle? This can be used to control the spawn-rate on a per-particle basis. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding LightRenderingEnabledBinding;

	/** Which attribute should we use for the light's exponent when inverse squared falloff is disabled? */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings", meta = (EditCondition = "!bUseInverseSquaredFalloff"))
	FNiagaraVariableAttributeBinding LightExponentBinding;

	/** Which attribute should we use for position when generating lights?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Which attribute should we use for light color when generating lights?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding ColorBinding;

	/** Which attribute should we use for light radius when generating lights?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RadiusBinding;

	/** Which attribute should we use for the intensity of the volumetric scattering from this light? This scales the light's intensity and color. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding VolumetricScatteringBinding;

	FNiagaraDataSetAccessor<FVector> PositionDataSetAccessor;
	FNiagaraDataSetAccessor<FLinearColor> ColorDataSetAccessor;
	FNiagaraDataSetAccessor<float> RadiusDataSetAccessor;
	FNiagaraDataSetAccessor<float> ExponentDataSetAccessor;
	FNiagaraDataSetAccessor<float> ScatteringDataSetAccessor;
	FNiagaraDataSetAccessor<FNiagaraBool> EnabledDataSetAccessor;

private:
	static TArray<TWeakObjectPtr<UNiagaraLightRendererProperties>> LightRendererPropertiesToDeferredInit;
};
