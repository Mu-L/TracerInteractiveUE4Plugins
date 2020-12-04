// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Particles/ParticlePerfStats.h"
#include "NiagaraParameterCollection.h"
#include "UObject/GCObject.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraSystemSimulation.h"
#include "NiagaraSystemInstance.h"
#include "GlobalDistanceFieldParameters.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponentPool.h"
#include "NiagaraEffectType.h"
#include "NiagaraScalabilityManager.h"

#include "NiagaraWorldManager.generated.h"

class UWorld;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;
class UNiagaraComponentPool;
struct FNiagaraScalabilityState;

class FNiagaraViewDataMgr : public FRenderResource
{
public:
	FNiagaraViewDataMgr();

	static void Init();
	static void Shutdown();

	void PostOpaqueRender(FPostOpaqueRenderParameters& Params)
	{
		SceneDepthTexture = Params.DepthTexture;
		ViewUniformBuffer = Params.ViewUniformBuffer;
		SceneNormalTexture = Params.NormalTexture;
		SceneVelocityTexture = Params.VelocityTexture;
		SceneTexturesUniformParams = Params.SceneTexturesUniformParams;
	}

	FRHITexture2D* GetSceneDepthTexture() { return SceneDepthTexture; }
	FRHITexture2D* GetSceneNormalTexture() { return SceneNormalTexture; }
	FRHITexture2D* GetSceneVelocityTexture() { return SceneVelocityTexture; }
	FRHIUniformBuffer* GetViewUniformBuffer() { return ViewUniformBuffer; }
	TUniformBufferRef<FSceneTextureUniformParameters> GetSceneTextureUniformParameters() { return SceneTexturesUniformParams; }

	virtual void InitDynamicRHI() override;

	virtual void ReleaseDynamicRHI() override;

private:
	FRHITexture2D* SceneDepthTexture = nullptr;
	FRHITexture2D* SceneNormalTexture = nullptr;
	FRHITexture2D* SceneVelocityTexture = nullptr;
	FRHIUniformBuffer* ViewUniformBuffer = nullptr;

	TUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformParams;
	FPostOpaqueRenderDelegate PostOpaqueDelegate;
	FDelegateHandle PostOpaqueDelegateHandle;
};

extern TGlobalResource<FNiagaraViewDataMgr> GNiagaraViewDataManager;

USTRUCT()
struct FNiagaraWorldManagerTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	//~ FTickFunction Interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed)  override;
	//~ FTickFunction Interface

	FNiagaraWorldManager* Owner;
};

template<>
struct TStructOpsTypeTraits<FNiagaraWorldManagerTickFunction> : public TStructOpsTypeTraitsBase2<FNiagaraWorldManagerTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

/**
* Manager class for any data relating to a particular world.
*/
class FNiagaraWorldManager : public FGCObject
{
public:
	
	FNiagaraWorldManager();
	~FNiagaraWorldManager();

	void Init(UWorld* InWorld);

	static NIAGARA_API FNiagaraWorldManager* Get(const UWorld* World);
	static void OnStartup();
	static void OnShutdown();

	// Gamethread callback to cleanup references to the given batcher before it gets deleted on the renderthread.
	static void OnBatcherDestroyed(class NiagaraEmitterInstanceBatcher* InBatcher);

	static void DestroyAllSystemSimulations(class UNiagaraSystem* System);

	//~ GCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~ GCObject Interface
	
	UNiagaraParameterCollectionInstance* GetParameterCollection(UNiagaraParameterCollection* Collection);
	void CleanupParameterCollections();
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> GetSystemSimulation(ETickingGroup TickGroup, UNiagaraSystem* System);
	void DestroySystemSimulation(UNiagaraSystem* System);
	void DestroySystemInstance(TUniquePtr<FNiagaraSystemInstance>& InPtr);	

	void MarkSimulationForPostActorWork(FNiagaraSystemSimulation* SystemSimulation);

	void Tick(ETickingGroup TickGroup, float DeltaSeconds, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	/** Called after all actor tick groups are complete. */
	void PostActorTick(float DeltaSeconds);

	void OnWorldCleanup(bool bSessionEnded, bool bCleanupResources);

	void PreGarbageCollect();
	void PostReachabilityAnalysis();
	void PostGarbageCollect();
	void PreGarbageCollectBeginDestroy();
	
	FORCEINLINE FNDI_SkeletalMesh_GeneratedData& GetSkeletalMeshGeneratedData() { return SkeletalMeshGeneratedData; }

	NIAGARA_API bool CachedPlayerViewLocationsValid() const { return bCachedPlayerViewLocationsValid; }
	NIAGARA_API TArrayView<const FVector> GetCachedPlayerViewLocations() const { check(bCachedPlayerViewLocationsValid); return MakeArrayView(CachedPlayerViewLocations); }

	UNiagaraComponentPool* GetComponentPool() { return ComponentPool; }

	void UpdateScalabilityManagers(bool bNewSpawnsOnly);

	// Dump details about what's inside the world manager
	void DumpDetails(FOutputDevice& Ar);
	
	UWorld* GetWorld();
	FORCEINLINE UWorld* GetWorld()const { return World; }

	//Various helper functions for scalability culling.
	
	void RegisterWithScalabilityManager(UNiagaraComponent* Component);
	void UnregisterWithScalabilityManager(UNiagaraComponent* Component);

	/** Should we cull an instance of this system at the passed location before it's even been spawned? */
	NIAGARA_API bool ShouldPreCull(UNiagaraSystem* System, UNiagaraComponent* Component);
	NIAGARA_API bool ShouldPreCull(UNiagaraSystem* System, FVector Location);

	void CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, UNiagaraComponent* Component, bool bIsPreCull, FNiagaraScalabilityState& OutState);
	void CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, FVector Location, bool bIsPreCull, FNiagaraScalabilityState& OutState);

	/*FORCEINLINE_DEBUGGABLE*/ void SortedSignificanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, float Significance, int32& EffectTypeInstCount, int32& SystemInstCount, FNiagaraScalabilityState& OutState);

#if DEBUG_SCALABILITY_STATE
	void DumpScalabilityState();
#endif

	template<typename TAction>
	void ForAllSystemSimulations(TAction Func);

	template<typename TAction>
	static void ForAllWorldManagers(TAction Func);

	static void PrimePoolForAllWorlds(UNiagaraSystem* System);
	void PrimePoolForAllSystems();
	void PrimePool(UNiagaraSystem* System);
private:
	// Callback function registered with global world delegates to instantiate world manager when a game world is created
	static void OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS);

	// Callback function registered with global world delegates to cleanup world manager contents
	static void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	// Callback function registered with global world delegates to cleanup world manager when a game world is destroyed
	static void OnPreWorldFinishDestroy(UWorld* World);

	// Called when the world begins to be torn down for example by level streaming.
	static void OnWorldBeginTearDown(UWorld* World);

	// Callback for when a world is ticked.
	static void TickWorld(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	// Callback to handle any pre GC processing needed.
	static void OnPreGarbageCollect();

	// Callback post reachability
	static void OnPostReachabilityAnalysis();

	// Callback to handle any post GC processing needed.
	static void OnPostGarbageCollect();

	// Callback to handle any pre GC processing needed.
	static void OnPreGarbageCollectBeginDestroy();
		
	// Gamethread callback to cleanup references to the given batcher before it gets deleted on the renderthread.
	void OnBatcherDestroyed_Internal(NiagaraEmitterInstanceBatcher* InBatcher);

	FORCEINLINE_DEBUGGABLE bool CanPreCull(UNiagaraEffectType* EffectType);

	FORCEINLINE_DEBUGGABLE void DistanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FVector Location, FNiagaraScalabilityState& OutState);
	FORCEINLINE_DEBUGGABLE void DistanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraComponent* Component, FNiagaraScalabilityState& OutState);
	FORCEINLINE_DEBUGGABLE void VisibilityCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraComponent* Component, FNiagaraScalabilityState& OutState);
	FORCEINLINE_DEBUGGABLE void InstanceCountCull(UNiagaraEffectType* EffectType, UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FNiagaraScalabilityState& OutState);

	
	static FDelegateHandle OnWorldInitHandle;
	static FDelegateHandle OnWorldCleanupHandle;
	static FDelegateHandle OnPreWorldFinishDestroyHandle;
	static FDelegateHandle OnWorldBeginTearDownHandle;
	static FDelegateHandle TickWorldHandle;
	static FDelegateHandle PreGCHandle;
	static FDelegateHandle PostReachabilityAnalysisHandle;
	static FDelegateHandle PostGCHandle;
	static FDelegateHandle PreGCBeginDestroyHandle;

	static TMap<class UWorld*, class FNiagaraWorldManager*> WorldManagers;

	UWorld* World;

	FNiagaraWorldManagerTickFunction TickFunctions[NiagaraNumTickGroups];

	TMap<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> ParameterCollections;

	TMap<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>> SystemSimulations[NiagaraNumTickGroups];

	TArray<TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>> SimulationsWithPostActorWork;

	int32 CachedEffectsQuality;

	bool bCachedPlayerViewLocationsValid = false;
	TArray<FVector, TInlineAllocator<8> > CachedPlayerViewLocations;

	UNiagaraComponentPool* ComponentPool;
	bool bPoolIsPrimed = false;

	/** Generated data used by data interfaces */
	FNDI_SkeletalMesh_GeneratedData SkeletalMeshGeneratedData;

	/** Instances that have been queued for deletion this frame, serviced in PostActorTick */
	TArray<TUniquePtr<FNiagaraSystemInstance>> DeferredDeletionQueue;

	UPROPERTY(transient)
	TMap<UNiagaraEffectType*, FNiagaraScalabilityManager> ScalabilityManagers;

	/** True if the app has focus. We prevent some culling if the app doesn't have focus as it can interefre. */
	bool bAppHasFocus;
};


template<typename TAction>
void FNiagaraWorldManager::ForAllSystemSimulations(TAction Func)
{
	for (int TG = 0; TG < NiagaraNumTickGroups; ++TG)
	{
		for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SimPair : SystemSimulations[TG])
		{
			Func(SimPair.Value.Get());
		}
	}
}

template<typename TAction>
void FNiagaraWorldManager::ForAllWorldManagers(TAction Func)
{
	for (auto& Pair : WorldManagers)
	{
		if (Pair.Value)
		{
			Func(*Pair.Value);
		}
	}
}