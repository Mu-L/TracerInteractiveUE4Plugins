// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ChaosSolver.h"
#include "Chaos/ChaosSolverActor.h"
#include "HAL/ThreadSafeBool.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "ChaosCollisionEventFilter.h"
#include "ChaosBreakingEventFilter.h"
#include "ChaosTrailingEventFilter.h"
#include "ChaosBlueprint.generated.h"



/**
* Called when new destruction events are available for collisions. Collision listening must be enabled to get callbacks on this delegate.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaosCollisionEvents, const TArray<FChaosCollisionEventData>&, CollisionEvents);

/**
* Called when new destruction events are available for breaks. Breaking listening must be enabled to get callbacks on this delegate.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaosBreakingEvents, const TArray<FChaosBreakingEventData>&, BreakingEvents);

/**
* Called when new trailing events are available for breaks. Trailing listening must be enabled to get callbacks on this delegate.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaosTrailingEvents, const TArray<FChaosTrailingEventData>&, TrailingEvents);


/** Object allowing for retrieving Chaos Destruction data. */
UCLASS(ClassGroup = (Chaos, Common), hidecategories = (Object, ActorComponent, Physics, Rendering, Mobility, LOD), ShowCategories = Trigger, meta = (BlueprintSpawnableComponent))
class GEOMETRYCOLLECTIONENGINE_API UChaosDestructionListener : public USceneComponent
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface
	
	//~ Begin UActorComponent interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//~ End UActorComponent interface

	// Whether or not collision event listening is enabled
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision Events")
	uint8 bIsCollisionEventListeningEnabled : 1;

	// Whether or not collision event listening is enabled
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breaking Events")
	uint8 bIsBreakingEventListeningEnabled : 1;

	// Whether or not trailing event listening is enabled
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trailing Events")
	uint8 bIsTrailingEventListeningEnabled : 1;

	// The settings to use for collision event listening
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision Events", meta = (EditCondition = "bIsCollisionEventListeningEnabled"))
	FChaosCollisionEventRequestSettings CollisionEventRequestSettings;

	// The settings to use for breaking event listening
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breaking Events", meta = (EditCondition = "bIsBreakingEventListeningEnabled"))
	FChaosBreakingEventRequestSettings BreakingEventRequestSettings;

	// The settings to use for trailing event listening
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trailing Events", meta = (EditCondition = "bIsTrailingEventListeningEnabled"))
	FChaosTrailingEventRequestSettings TrailingEventRequestSettings;

	// Which chaos solver actors we're using. If empty, this listener will fallback to the "world" solver.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Solvers")
	TSet<AChaosSolverActor*> ChaosSolverActors;

	// Which chaos solver actors we're using. If empty, this listener will fallback to the "world" solver.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GeometryCollections")
	TSet<AGeometryCollectionActor*> GeometryCollectionActors; // Using TSet automatically blocks if user tries to add same actor twice

	// Dynamically adds a chaos solver to the listener
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void AddChaosSolverActor(AChaosSolverActor* ChaosSolverActor);
	
	// Dynamically removes a chaos solver from the listener
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void RemoveChaosSolverActor(AChaosSolverActor* ChaosSolverActor);

	// Dynamically adds a chaos solver to the listener
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void AddGeometryCollectionActor(AGeometryCollectionActor* GeometryCollectionActor);

	// Dynamically removes a chaos solver from the listener
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void RemoveGeometryCollectionActor(AGeometryCollectionActor* GeometryCollectionActor);

	// Sets collision event request settings dynamically
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetCollisionEventRequestSettings(const FChaosCollisionEventRequestSettings& InSettings);

	// Sets breaking event request settings dynamically
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetBreakingEventRequestSettings(const FChaosBreakingEventRequestSettings& InSettings);

	// Sets trailing event request settings dynamically
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetTrailingEventRequestSettings(const FChaosTrailingEventRequestSettings& InSettings);

	// Enables or disables collision event listening
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetCollisionEventEnabled(bool bIsEnabled);

	// Enables or disables breaking event listening
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetBreakingEventEnabled(bool bIsEnabled);

	// Enables or disables breaking event listening
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SetTrailingEventEnabled(bool bIsEnabled);

	// Returns if the destruction listener is listening to any events
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	bool IsEventListening() const;

	/** Called when new collision events are available. */
	UPROPERTY(BlueprintAssignable)
	FOnChaosCollisionEvents OnCollisionEvents;

	/** Called when new breaking events are available. */
	UPROPERTY(BlueprintAssignable)
	FOnChaosBreakingEvents OnBreakingEvents;

	/** Called when new trailing events are available. */
	UPROPERTY(BlueprintAssignable)
	FOnChaosTrailingEvents OnTrailingEvents;

	// Sorts collision events according to the given sort method	
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SortCollisionEvents(UPARAM(ref) TArray<FChaosCollisionEventData>& CollisionEvents, EChaosCollisionSortMethod SortMethod);

	// Sorts breaking events according to the given sort method	
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SortBreakingEvents(UPARAM(ref) TArray<FChaosBreakingEventData>& BreakingEvents, EChaosBreakingSortMethod SortMethod);

	// Sorts trailing events according to the given sort method	
	UFUNCTION(BlueprintCallable, Category = "Destruction Listener", meta = (WorldContext = "WorldContextObject"))
	void SortTrailingEvents(UPARAM(ref) TArray<FChaosTrailingEventData>& TrailingEvents, EChaosTrailingSortMethod SortMethod);

private:

	// Updates the scene component transform settings
	void UpdateTransformSettings();

	// Retrieves data from solvers
	void GetDataFromSolvers();

	// Retrieve data from GeometryCollectionPhysicsObjects
	void GetDataFromGeometryCollectionPhysicsObjects();

	void UpdateSolvers();
	void UpdateGeometryCollectionPhysicsObjects();

protected:

	// Task state enumeration used to track if there is an async processing task in-flight
	enum class ETaskState
	{
		// If there is no filtering and sorting task in-flight
		NoTask = 0,

		// If there is a task in-flight
		Processing,

		// If there was a task in flight but it is now finished with results ready for broadcast
		Finished
	};
	FThreadSafeCounter TaskState;

#if INCLUDE_CHAOS
	// The raw data arrays derived from the solvers
	Chaos::FPBDRigidsSolver::FCollisionDataArray RawCollisionDataArray;
	Chaos::FPBDRigidsSolver::FBreakingDataArray RawBreakingDataArray;
	TArray<Chaos::TTrailingData<float, 3>> RawTrailingDataArray;
#endif

	FTransform ChaosComponentTransform;

	FThreadSafeBool bChanged;

	float LastCollisionDataTimeStamp;
	float LastBreakingDataTimeStamp;
	float LastTrailingDataTimeStamp;

#if INCLUDE_CHAOS
	// The list of rigid body solvers, used to retrieve destruction events
	TSet<Chaos::FPBDRigidsSolver*> Solvers;

	// The list of GeometryCollectionPhysicsObjects, used to retrieve destruction events
	TArray<const FGeometryCollectionPhysicsObject*> GeometryCollectionPhysicsObjects;

	TSharedPtr<FChaosCollisionEventFilter> ChaosCollisionFilter;
	TSharedPtr<FChaosBreakingEventFilter> ChaosBreakingFilter;
	TSharedPtr<FChaosTrailingEventFilter> ChaosTrailingFilter;

public:
	void SetCollisionFilter(TSharedPtr<FChaosCollisionEventFilter> InCollisionFilter)
	{ 
		ChaosCollisionFilter = InCollisionFilter;
	}

	void SetBreakingFilter(TSharedPtr < FChaosBreakingEventFilter> InBreakingFilter)
	{
		ChaosBreakingFilter = InBreakingFilter;
	}

	void SetTrailingFilter(TSharedPtr < FChaosTrailingEventFilter> InTrailingFilter)
	{
		ChaosTrailingFilter = InTrailingFilter;
	}
#endif
};
