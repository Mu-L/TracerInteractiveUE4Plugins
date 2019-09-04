// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosEventListenerComponent.h"
#include "PhysicsPublic.h"
#include "ChaosNotifyHandlerInterface.h"
#include "ChaosGameplayEventDispatcher.generated.h"

struct FBodyInstance;

USTRUCT(BlueprintType)
struct CHAOSSOLVERENGINE_API FChaosBreakEvent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	UPrimitiveComponent* Component;

	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector Location;

	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector Velocity;

	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	FVector AngularVelocity;

	UPROPERTY(BlueprintReadOnly, Category = "Break Event")
	float Mass;
};


typedef TFunction<void(const FChaosBreakEvent&)> FOnBreakEventCallback;

/** UStruct wrapper so we can store the TFunction in a TMap */
USTRUCT()
struct CHAOSSOLVERENGINE_API FBreakEventCallbackWrapper
{
	GENERATED_BODY()

public:
	FOnBreakEventCallback BreakEventCallback;
};

/** UStruct wrapper so we can store the TSet in a TMap */
USTRUCT()
struct FChaosHandlerSet
{
	GENERATED_BODY()

	bool bLegacyComponentNotify;
		
	/** These should be IChaosNotifyHandlerInterface refs, but we can't store those here */
	UPROPERTY()
	TSet<UObject*> ChaosHandlers;
};

struct FChaosPendingCollisionNotify
{
	FChaosPhysicsCollisionInfo CollisionInfo;
	TSet<UObject*> NotifyRecipients;
};


UCLASS()
class CHAOSSOLVERENGINE_API UChaosGameplayEventDispatcher : public UChaosEventListenerComponent
{
	GENERATED_BODY()

public:

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

private:

	// contains the set of properties that uniquely identifies a reported collision
	// Note that order matters, { Body0, Body1 } is not the same as { Body1, Body0 }
	struct FUniqueContactPairKey
	{
		const void* Body0;
		const void* Body1;

		friend bool operator==(const FUniqueContactPairKey& Lhs, const FUniqueContactPairKey& Rhs) 
		{ 
			return Lhs.Body0 == Rhs.Body0 && Lhs.Body1 == Rhs.Body1;
		}

		friend inline uint32 GetTypeHash(FUniqueContactPairKey const& P)
		{
			return (PTRINT)P.Body0 ^ ((PTRINT)P.Body1 << 18);
		}
	};

	FCollisionNotifyInfo& GetPendingCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry);
	/** Key is the unique pair, value is index into PendingNotifies array */
	TMap<FUniqueContactPairKey, int32> ContactPairToPendingNotifyMap;

	FChaosPendingCollisionNotify& GetPendingChaosCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry);
	/** Key is the unique pair, value is index into PendingChaosCollisionNotifies array */
	TMap<FUniqueContactPairKey, int32> ContactPairToPendingChaosNotifyMap;

	/** Holds the list of pending Chaos notifies that are to be processed */
	TArray<FChaosPendingCollisionNotify> PendingChaosCollisionNotifies;

	/** Holds the list of pending legacy notifies that are to be processed */
	TArray<FCollisionNotifyInfo> PendingCollisionNotifies;


public:
	/** 
	 * Use to subscribe to collision events. 
	 * @param ComponentToListenTo	The component whose collisions will be reported
	 * @param ObjectToNotify		The object that will receive the notifications. Should be a PrimitiveComponent or implement IChaosNotifyHandlerInterface, or both.
	 */
	void RegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify);
	void UnRegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify);

	void RegisterForBreakEvents(UPrimitiveComponent* Component, FOnBreakEventCallback InFunc);
	void UnRegisterForBreakEvents(UPrimitiveComponent* Component);

private:

 	UPROPERTY()
 	TMap<UPrimitiveComponent*, FChaosHandlerSet> CollisionEventRegistrations;

	UPROPERTY()
	TMap<UPrimitiveComponent*, FBreakEventCallbackWrapper> BreakEventRegistrations;

	float LastCollisionDataTime = -1.f;
	float LastBreakingDataTime = -1.f;

	void DispatchPendingCollisionNotifies();
};


