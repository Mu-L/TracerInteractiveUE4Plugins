// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneCaptureComponent.h"
#include "SceneCaptureComponentCube.generated.h"

class FSceneInterface;

/**
 *	Used to capture a 'snapshot' of the scene from a 6 planes and feed it to a render target.
 */
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), ClassGroup=Rendering, editinlinenew, meta = (BlueprintSpawnableComponent))
class ENGINE_API USceneCaptureComponentCube : public USceneCaptureComponent
{
	GENERATED_UCLASS_BODY()

	/** Temporary render target that can be used by the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
	class UTextureRenderTargetCube* TextureTarget;

	/** Preserve the rotation of the actor when updating the capture. The default behavior is to capture the cube aligned to the world axis system.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
	bool bCaptureRotation;

	/**
		Omni-directional Stereo Capture

		If vr.ODSCapture is enabled and both left, right and ODS render targets are set,
		we'll ignore the texture target and instead do an omni-directional stereo capture.
		Warped cube maps will be rendered into the left and right texture targets using the 
		IPD property for stereo offset. We will then reconstruct a stereo lat-long with the left
		eye stacked on top of the right eye in the ODS target.
		See: https://developers.google.com/vr/jump/rendering-ods-content.pdf
	*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SceneCapture|Omni-directional Stereo Capture")
	class UTextureRenderTargetCube* TextureTargetLeft;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SceneCapture|Omni-directional Stereo Capture")
	class UTextureRenderTargetCube* TextureTargetRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SceneCapture|Omni-directional Stereo Capture")
	class UTextureRenderTarget2D* TextureTargetODS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SceneCapture|Omni-directional Stereo Capture", meta = (DisplayName = "Interpupillary Distance (cm)", UIMin = "0.0", UIMax = "16.0", ClampMin = "0.0", ClampMax = "16.0"))
	float IPD;

public:
	//~ Begin UActorComponent Interface
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnRegister() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	//~ Begin UObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	/** Render the scene to the texture the next time the main view is rendered. */
	void CaptureSceneDeferred();

	/** 
	 * Render the scene to the texture target immediately.  
	 * This should not be used if bCaptureEveryFrame is enabled, or the scene capture will render redundantly. 
	 */
	UFUNCTION(BlueprintCallable,Category = "Rendering|SceneCapture")
	void CaptureScene();

	// For backwards compatibility
	void UpdateContent() { CaptureSceneDeferred(); }

	void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

#if WITH_EDITORONLY_DATA
	void UpdateDrawFrustum();

	/** The frustum component used to show visually where the camera field of view is */
	class UDrawFrustumComponent* DrawFrustum;
#endif
};
