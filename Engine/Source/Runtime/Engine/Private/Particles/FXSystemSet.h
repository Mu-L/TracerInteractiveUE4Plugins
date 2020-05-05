// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FXSystemSet.h: Internal redirector to several fx systems.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "FXSystem.h"
#include "Templates/RefCounting.h"

class FGPUSortManager;

/**
 * FX system.
 */
class FFXSystemSet : public FFXSystemInterface
{
public:

	FFXSystemSet(FGPUSortManager* InGPUSortManager);

	TArray<FFXSystemInterface*> FXSystems;

	virtual FFXSystemInterface* GetInterface(const FName& InName) override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void Suspend() override;
	virtual void Resume() override;
#endif // #if WITH_EDITOR

	virtual void DrawDebug(FCanvas* Canvas) override;
	virtual void AddVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	virtual void RemoveVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	virtual void UpdateVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	virtual void PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate) override;
	virtual void PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate) override;
	virtual bool UsesGlobalDistanceField() const override;
	virtual bool UsesDepthBuffer() const override;
	virtual bool RequiresEarlyViewUniformBuffer() const override;
	virtual void PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData, bool bAllowGPUParticleSceneUpdate) override;
	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList, 
		FRHIUniformBuffer* ViewUniformBuffer,
		const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		bool bAllowGPUParticleUpdate) override;

	virtual void OnDestroy() override;
	virtual void DestroyGPUSimulation() override;

	/** Get the shared SortManager, used in the rendering loop to call FGPUSortManager::OnPreRender() and FGPUSortManager::OnPostRenderOpaque() */
	virtual FGPUSortManager* GetGPUSortManager() const override;

protected:

	/** By making the destructor protected, an instance must be destroyed via FFXSystemInterface::Destroy. */
	virtual ~FFXSystemSet();

	// We keep a reference to the GPUSortManager only for GetGPUSortManager().
	TRefCountPtr<FGPUSortManager> GPUSortManager;
};
