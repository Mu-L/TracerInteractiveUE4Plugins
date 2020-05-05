// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FXSystemPrivate.h: Internal effects system interface.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "FXSystem.h"
#include "VectorField.h"
#include "ParticleSortingGPU.h"
#include "GPUSortManager.h"

class FCanvas;
class FGlobalDistanceFieldParameterData;
class FParticleSimulationGPU;
class FParticleSimulationResources;
class UVectorFieldComponent;
struct FGPUSpriteEmitterInfo;
struct FParticleEmitterInstance;

/*-----------------------------------------------------------------------------
	Forward declarations.
-----------------------------------------------------------------------------*/

/** An individual particle simulation taking place on the GPU. */
class FParticleSimulationGPU;
/** Resources used for particle simulation. */
class FParticleSimulationResources;

namespace EParticleSimulatePhase
{
	enum Type
	{
		/** The main simulation pass is for standard particles. */
		Main,
		CollisionDistanceField,
		/** The collision pass is used by these that collide against the scene depth buffer. */
		CollisionDepthBuffer,

		/**********************************************************************/

		/** The first simulation phase that is run each frame. */
		First = Main,
		/** The final simulation phase that is run each frame. */
		Last = CollisionDepthBuffer
	};
};

enum EParticleCollisionShaderMode
{
	PCM_None,
	PCM_DepthBuffer,
	PCM_DistanceField
};

/** Helper function to determine whether the given particle collision shader mode is supported on the given shader platform */
inline bool IsParticleCollisionModeSupported(EShaderPlatform InPlatform, EParticleCollisionShaderMode InCollisionShaderMode, bool bForCaching = false)
{
	switch (InCollisionShaderMode)
	{
	case PCM_None:
		return IsFeatureLevelSupported(InPlatform, ERHIFeatureLevel::ES3_1);
	case PCM_DepthBuffer:
		// we only need to check for simple forward if we're NOT currently attempting to cache the shader
		// since SF is a runtime change, we need to compile the shader regardless, because we could be switching to deferred at any time
		return (IsFeatureLevelSupported(InPlatform, ERHIFeatureLevel::SM5))
			&& (bForCaching || !IsSimpleForwardShadingEnabled(InPlatform));
	case PCM_DistanceField:
		return IsFeatureLevelSupported(InPlatform, ERHIFeatureLevel::SM5);
	}
	check(0);
	return IsFeatureLevelSupported(InPlatform, ERHIFeatureLevel::SM5);
}


inline EParticleSimulatePhase::Type GetLastParticleSimulationPhase(EShaderPlatform InPlatform)
{
	return (IsParticleCollisionModeSupported(InPlatform, PCM_DepthBuffer) ? EParticleSimulatePhase::Last : EParticleSimulatePhase::Main);
}

/*-----------------------------------------------------------------------------
Injecting particles in to the GPU for simulation.
-----------------------------------------------------------------------------*/

/**
* Data passed to the GPU to inject a new particle in to the simulation.
*/
struct FNewParticle
{
	/** The initial position of the particle. */
	FVector Position;
	/** The relative time of the particle. */
	float RelativeTime;
	/** The initial velocity of the particle. */
	FVector Velocity;
	/** The time scale for the particle. */
	float TimeScale;
	/** Initial size of the particle. */
	FVector2D Size;
	/** Initial rotation of the particle. */
	float Rotation;
	/** Relative rotation rate of the particle. */
	float RelativeRotationRate;
	/** Coefficient of drag. */
	float DragCoefficient;
	/** Per-particle vector field scale. */
	float VectorFieldScale;
	/** Resilience for collision. */
	union
	{
		float Resilience;
		int32 AllocatedTileIndex;
	} ResilienceAndTileIndex;
	/** Random selection of orbit attributes. */
	float RandomOrbit;
	/** The offset at which to inject the new particle. */
	FVector2D Offset;
};

/*-----------------------------------------------------------------------------
	FX system declaration.
-----------------------------------------------------------------------------*/

/**
 * FX system.
 */
class FFXSystem : public FFXSystemInterface
{
public:

	/** Default constructoer. */
	FFXSystem(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, FGPUSortManager* InGPUSortManager);

	/** Destructor. */
	virtual ~FFXSystem();

	static const FName Name;
	virtual FFXSystemInterface* GetInterface(const FName& InName) override;

	// Begin FFXSystemInterface.
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void Suspend() override;
	virtual void Resume() override;
#endif // #if WITH_EDITOR
	virtual void DrawDebug(FCanvas* Canvas) override;
	virtual void AddVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	virtual void RemoveVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	virtual void UpdateVectorField(UVectorFieldComponent* VectorFieldComponent) override;
	FParticleEmitterInstance* CreateGPUSpriteEmitterInstance(FGPUSpriteEmitterInfo& EmitterInfo);
	virtual void PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate) override;
	virtual void PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate) override;
	virtual bool UsesGlobalDistanceField() const override;
	virtual bool UsesDepthBuffer() const override;
	virtual bool RequiresEarlyViewUniformBuffer() const override;
	virtual void PreRender(FRHICommandListImmediate& RHICmdList, const FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData, bool bAllowGPUParticleUpdate) override;
	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList, 
		FRHIUniformBuffer* ViewUniformBuffer,
		const FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		bool bAllowGPUParticleUpdate) override;
	// End FFXSystemInterface.

	/*--------------------------------------------------------------------------
		Internal interface for GPU simulation.
	--------------------------------------------------------------------------*/
	/**
	 * Retrieve feature level that this FXSystem was created for
	 */
	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/**
	 * Retrieve shaderplatform that this FXSystem was created for
	 */
	EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }

	/**
	 * Add a new GPU simulation to the system.
	 * @param Simulation The GPU simulation to add.
	 */
	void AddGPUSimulation(FParticleSimulationGPU* Simulation);

	/**
	 * Remove an existing GPU simulation to the system.
	 * @param Simulation The GPU simulation to remove.
	 */
	void RemoveGPUSimulation(FParticleSimulationGPU* Simulation);

	/**
	 * Retrieve GPU particle rendering resources.
	 */
	FParticleSimulationResources* GetParticleSimulationResources()
	{
		return ParticleSimulationResources;
	}

	/** 
	 * Register work for GPU sorting (using the GPUSortManager). 
	 * The initial keys and values are generated in the GenerateSortKeys() callback.
	 *
	 * @param Simulation The simulation to be sorted.
	 * @param ViewOrigin The origin of the view from which to sort.
	 * @param bIsTranslucent Whether this is for sorting translucent particles or opaque particles, affect when the data is required for the rendering.
	 * @param OutInfo The bindings for this GPU sort task, if success. 
	 * @returns true if the work was registered, or false it GPU sorting is not available or impossible.
	 */
	bool AddSortedGPUSimulation(FParticleSimulationGPU* Simulation, const FVector& ViewOrigin, bool bIsTranslucent, FGPUSortManager::FAllocationInfo& OutInfo);

	void PrepareGPUSimulation(FRHICommandListImmediate& RHICmdList);
	void FinalizeGPUSimulation(FRHICommandListImmediate& RHICmdList);

	/** Get the shared SortManager, used in the rendering loop to call FGPUSortManager::OnPreRender() and FGPUSortManager::OnPostRenderOpaque() */
	virtual FGPUSortManager* GetGPUSortManager() const override;

private:

	/**
	 * Generate all the initial keys and values for a GPUSortManager sort batch.
	 * Sort batches are created when GPU sort tasks are registered in AddSortedGPUSimulation().
	 * Each sort task defines constraints about when the initial sort data can generated and
	 * and when the sorted results are needed (see EGPUSortFlags for details).
	 * Currently, for Cascade, all the sort tasks have the EGPUSortFlags::KeyGenAfterPostRenderOpaque flag
	 * and so the callback registered in GPUSortManager->Register() only has the EGPUSortFlags::KeyGenAfterPostRenderOpaque usage.
	 * This garanties that GenerateSortKeys() only gets called after PostRenderOpaque(), which is a constraint required because
	 * cascade renders the GPU emitters after they have been ticked in PostRenderOpaque.
	 * Note that this callback must only initialize the content for the elements that relates to the tasks it has registered in this batch.
	 *
	 * @param RHICmdList The command list used to initiate the keys and values on GPU.
	 * @param BatchId The GPUSortManager batch id (regrouping several similar sort tasks).
	 * @param NumElementsInBatch The number of elements grouped in the batch (each element maps to a sort task)
	 * @param Flags Details about the key precision (see EGPUSortFlags::AnyKeyPrecision) and the keygen location (see EGPUSortFlags::AnyKeyGenLocation).
	 * @param KeysUAV The UAV that holds all the initial keys used to sort the values (being the particle indices here). 
	 * @param ValuesUAV The UAV that holds the initial values (particle indices) to be sorted accordingly to the keys.
	 */
	void GenerateSortKeys(FRHICommandListImmediate& RHICmdList, int32 BatchId, int32 NumElementsInBatch, EGPUSortFlags Flags, FRHIUnorderedAccessView* KeysUAV, FRHIUnorderedAccessView* ValuesUAV);

	/*--------------------------------------------------------------------------
		Private interface for GPU simulations.
	--------------------------------------------------------------------------*/

	/**
	 * Initializes GPU simulation for this system.
	 */
	void InitGPUSimulation();

	/**
	 * Destroys any resources allocated for GPU simulation for this system.
	 */
	virtual void DestroyGPUSimulation();

	/**
	 * Initializes GPU resources.
	 */
	void InitGPUResources();

	/**
	 * Releases GPU resources.
	 */
	void ReleaseGPUResources();

	/**
	 * Prepares GPU particles for simulation and rendering in the next frame.
	 */
	void AdvanceGPUParticleFrame(bool bAllowGPUParticleUpdate);

	bool UsesGlobalDistanceFieldInternal() const;
	bool UsesDepthBufferInternal() const;
	bool RequiresEarlyViewUniformBufferInternal() const;

	/**
	* Updates resources used in a multi-GPU context
	*/
	void UpdateMultiGPUResources(FRHICommandListImmediate& RHICmdList);

	/**
	 * Update particles simulated on the GPU.
	 * @param Phase				Which emitters are being simulated.
	 * @param CollisionView		View to be used for collision checks.
	 */
	void SimulateGPUParticles(
		FRHICommandListImmediate& RHICmdList,
		EParticleSimulatePhase::Type Phase,
		FRHIUniformBuffer* ViewUniformBuffer,
		const FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData,
		const FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer
		);

	/**
	 * Visualizes the current state of GPU particles.
	 * @param Canvas The canvas on which to draw the visualization.
	 */
	void VisualizeGPUParticles(FCanvas* Canvas);

private:

	template<typename TVectorFieldUniformParametersType>
	void SimulateGPUParticles_Internal(
		FRHICommandListImmediate& RHICmdList,
		EParticleSimulatePhase::Type Phase,
		FRHIUniformBuffer* ViewUniformBuffer,
		const FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData,
		FRHITexture2D* SceneDepthTexture,
		FRHITexture2D* GBufferATexture
	);

	/*-------------------------------------------------------------------------
		GPU simulation state.
	-------------------------------------------------------------------------*/

	/** List of all vector field instances. */
	FVectorFieldInstanceList VectorFields;
	/** List of all active GPU simulations. */
	TSparseArray<FParticleSimulationGPU*> GPUSimulations;
	/** Particle render resources. */
	FParticleSimulationResources* ParticleSimulationResources;
	/** Feature level of this effects system */
	ERHIFeatureLevel::Type FeatureLevel;
	/** Shader platform that will be rendering this effects system */
	EShaderPlatform ShaderPlatform;

	/** The shared GPUSortManager, used to register GPU sort tasks in order to generate sorted particle indices per emitter. */
	TRefCountPtr<FGPUSortManager> GPUSortManager;
	/** All sort tasks registered in AddSortedGPUSimulation(). Holds all the data required in GenerateSortKeys(). */
	TArray<FParticleSimulationSortInfo> SimulationsToSort;

	/** Previous frame new particles for multi-gpu simulation*/
	TArray<FNewParticle> LastFrameNewParticles;
#if WITH_EDITOR
	/** true if the system has been suspended. */
	bool bSuspended;
#endif // #if WITH_EDITOR

#if WITH_MGPU
	EParticleSimulatePhase::Type PhaseToWaitForTemporalEffect = EParticleSimulatePhase::First;
	EParticleSimulatePhase::Type PhaseToBroadcastTemporalEffect = EParticleSimulatePhase::Last;
#endif
};

