// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Camera modifier that provides support for code-based oscillating camera shakes.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Camera/CameraTypes.h"
#include "Camera/CameraModifier.h"
#include "CameraModifier_CameraShake.generated.h"

class UCameraShakeBase;
class UCameraShakeSourceComponent;

USTRUCT()
struct FPooledCameraShakes
{
	GENERATED_BODY()
		
	UPROPERTY()
	TArray<UCameraShakeBase*> PooledShakes;
};

USTRUCT()
struct FActiveCameraShakeInfo
{
	GENERATED_BODY()

	FActiveCameraShakeInfo() : ShakeInstance(nullptr), ShakeSource(nullptr) {}

	UPROPERTY()
	UCameraShakeBase* ShakeInstance;

	UPROPERTY()
	TWeakObjectPtr<const UCameraShakeSourceComponent> ShakeSource;
};

struct FAddCameraShakeParams
{
	float Scale;
	ECameraShakePlaySpace PlaySpace;
	FRotator UserPlaySpaceRot;
	const UCameraShakeSourceComponent* SourceComponent;

	FAddCameraShakeParams()
		: Scale(1.f), PlaySpace(ECameraShakePlaySpace::CameraLocal), UserPlaySpaceRot(FRotator::ZeroRotator), SourceComponent(nullptr)
	{}
	FAddCameraShakeParams(float InScale, ECameraShakePlaySpace InPlaySpace = ECameraShakePlaySpace::CameraLocal, FRotator InUserPlaySpaceRot = FRotator::ZeroRotator, const UCameraShakeSourceComponent* InSourceComponent = nullptr)
		: Scale(InScale), PlaySpace(InPlaySpace), UserPlaySpaceRot(InUserPlaySpaceRot), SourceComponent(InSourceComponent)
	{}
};

//~=============================================================================
/**
 * A UCameraModifier_CameraShake is a camera modifier that can apply a UCameraShakeBase to 
 * the owning camera.
 */
UCLASS(config=Camera)
class ENGINE_API UCameraModifier_CameraShake : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCameraModifier_CameraShake(const FObjectInitializer& ObjectInitializer);

	/** 
	 * Adds a new active screen shake to be applied. 
	 * @param NewShake - The class of camera shake to instantiate.
	 * @param Params - The parameters for the new camera shake.
	 */
	virtual UCameraShakeBase* AddCameraShake(TSubclassOf<UCameraShakeBase> NewShake, const FAddCameraShakeParams& Params);

	/** 
	 * Adds a new active screen shake to be applied. 
	 * @param NewShake - The class of camera shake to instantiate.
	 * @param Scale - The scalar intensity to play the shake.
	 * @param PlaySpace - Which coordinate system to play the shake in.
	 * @param UserPlaySpaceRot - Coordinate system to play shake when PlaySpace == CAPS_UserDefined.
	 */
	UE_DEPRECATED(4.25, "Please use the new AddCameraShake method that takes a parameter struct.")
	virtual UCameraShakeBase* AddCameraShake(TSubclassOf<UCameraShakeBase> NewShake, float Scale, ECameraShakePlaySpace PlaySpace= ECameraShakePlaySpace::CameraLocal, FRotator UserPlaySpaceRot = FRotator::ZeroRotator)
	{
		return AddCameraShake(NewShake, FAddCameraShakeParams(Scale, PlaySpace, UserPlaySpaceRot));
	}

	/**
	 * Returns a list of currently active camera shakes.
	 * @param ActiveCameraShakes - The array to fill up with shake information.
	 */
	virtual void GetActiveCameraShakes(TArray<FActiveCameraShakeInfo>& ActiveCameraShakes) const;
	
	/**
	 * Stops and removes the camera shake of the given class from the camera.
	 * @param Shake - the camera shake class to remove.
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveCameraShake(UCameraShakeBase* ShakeInst, bool bImmediately = true);

	/**
	 * Stops and removes all camera shakes of the given class from the camera. 
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakesOfClass(TSubclassOf<UCameraShakeBase> ShakeClass, bool bImmediately = true);

	/**
	 * Stops and removes all camera shakes originating from the given source.
	 * @param SourceComponent   The shake source.
	 * @param bImmediately      If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakesFromSource(const UCameraShakeSourceComponent* SourceComponent, bool bImmediately = true);

	/**
	 * Stops and removes all camera shakes of the given class originating from the given source.
	 * @param ShakeClasss       The camera shake class to remove.
	 * @param SourceComponent   The shake source.
	 * @param bImmediately      If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakesOfClassFromSource(TSubclassOf<UCameraShakeBase> ShakeClass, const UCameraShakeSourceComponent* SourceComponent, bool bImmediately = true);

	/** 
	 * Stops and removes all camera shakes from the camera. 
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakes(bool bImmediately = true);
	
	//~ Begin UCameraModifer Interface
	virtual bool ModifyCamera(float DeltaTime, struct FMinimalViewInfo& InOutPOV) override;
	//~ End UCameraModifer Interface

protected:

	/** List of active CameraShake instances */
	UPROPERTY()
	TArray<FActiveCameraShakeInfo> ActiveShakes;

	UPROPERTY()
	TMap<TSubclassOf<UCameraShakeBase>, FPooledCameraShakes> ExpiredPooledShakesMap;

	void SaveShakeInExpiredPool(UCameraShakeBase* ShakeInst);
	UCameraShakeBase* ReclaimShakeFromExpiredPool(TSubclassOf<UCameraShakeBase> CameraShakeClass);

	/** Scaling factor applied to all camera shakes in when in splitscreen mode. Normally used to reduce shaking, since shakes feel more intense in a smaller viewport. */
	UPROPERTY(EditAnywhere, Category = CameraModifier_CameraShake)
	float SplitScreenShakeScale;
};
