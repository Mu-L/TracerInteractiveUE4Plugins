// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/LightComponent.h"
#include "PointLightComponent.generated.h"

class FLightSceneProxy;

/**
 * A light component which emits light from a single point equally in all directions.
 */
UCLASS(Blueprintable, ClassGroup=(Lights,Common), hidecategories=(Object, LightShafts), editinlinenew, meta=(BlueprintSpawnableComponent))
class ENGINE_API UPointLightComponent : public ULightComponent
{
	GENERATED_UCLASS_BODY()

	/** 
	 * Units used for the intensity. 
	 * The peak luminous intensity is measured in candelas,
	 * while the luminous power is measured in lumens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, meta=(DisplayName="Intensity Units", EditCondition="bUseInverseSquaredFalloff"))
	ELightUnits IntensityUnits;

	UPROPERTY()
	float Radius_DEPRECATED;

	/**
	 * Bounds the light's visible influence.  
	 * This clamping of the light's influence is not physically correct but very important for performance, larger lights cost more.
	 */
	UPROPERTY(interp, BlueprintReadOnly, Category=Light, meta=(UIMin = "8.0", UIMax = "16384.0", SliderExponent = "5.0"))
	float AttenuationRadius;

	/** 
	 * Whether to use physically based inverse squared distance falloff, where AttenuationRadius is only clamping the light's contribution.  
	 * Disabling inverse squared falloff can be useful when placing fill lights (don't want a super bright spot near the light).
	 * When enabled, the light's Intensity is in units of lumens, where 1700 lumens is a 100W lightbulb.
	 * When disabled, the light's Intensity is a brightness scale.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, AdvancedDisplay)
	uint32 bUseInverseSquaredFalloff:1;

	/**
	 * Controls the radial falloff of the light when UseInverseSquaredFalloff is disabled. 
	 * 2 is almost linear and very unrealistic and around 8 it looks reasonable.
	 * With large exponents, the light has contribution to only a small area of its influence radius but still costs the same as low exponents.
	 */
	UPROPERTY(interp, BlueprintReadOnly, Category=Light, AdvancedDisplay, meta=(UIMin = "2.0", UIMax = "16.0"))
	float LightFalloffExponent;

	/** 
	 * Radius of light source shape.
	 * Note that light sources shapes which intersect shadow casting geometry can cause shadowing artifacts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light)
	float SourceRadius;

	/**
	* Soft radius of light source shape.
	* Note that light sources shapes which intersect shadow casting geometry can cause shadowing artifacts.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Light)
	float SoftSourceRadius;

	/** 
	 * Length of light source shape.
	 * Note that light sources shapes which intersect shadow casting geometry can cause shadowing artifacts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light)
	float SourceLength;

	/** The Lightmass settings for this object. */
	UPROPERTY(EditAnywhere, Category=Light, meta=(ShowOnlyInnerProperties))
	struct FLightmassPointLightSettings LightmassSettings;

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetAttenuationRadius(float NewRadius);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetLightFalloffExponent(float NewLightFalloffExponent);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetSourceRadius(float bNewValue);

	UFUNCTION(BlueprintCallable, Category = "Rendering|Lighting")
	void SetSoftSourceRadius(float bNewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetSourceLength(float NewValue);

	UFUNCTION(BlueprintPure, Category="Rendering|Lighting")
	static float GetUnitsConversionFactor(ELightUnits SrcUnits, ELightUnits TargetUnits, float CosHalfConeAngle = -1);

protected:
	//~ Begin UActorComponent Interface
	virtual void SendRenderTransform_Concurrent() override;
	//~ End UActorComponent Interface

public:

	virtual float ComputeLightBrightness() const override;

	//~ Begin ULightComponent Interface.
	virtual bool AffectsBounds(const FBoxSphereBounds& InBounds) const override;
	virtual FVector4 GetLightPosition() const override;
	virtual FBox GetBoundingBox() const override;
	virtual FSphere GetBoundingSphere() const override;
	virtual ELightComponentType GetLightType() const override;
	virtual FLightmassLightSettings GetLightmassSettings() const override
	{
		return LightmassSettings;
	}

	virtual float GetUniformPenumbraSize() const override;

	virtual FLightSceneProxy* CreateSceneProxy() const override;

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual bool CanEditChange(const UProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	/** 
	 * This is called when property is modified by InterpPropertyTracks
	 *
	 * @param PropertyThatChanged	Property that changed
	 */
	virtual void PostInterpChange(UProperty* PropertyThatChanged) override;

private:

	/** Pushes the value of radius to the rendering thread. */
	void PushRadiusToRenderThread();
};



