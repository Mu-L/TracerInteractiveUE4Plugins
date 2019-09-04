// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DisplayClusterPawn.generated.h"


class UCameraComponent;
class USphereComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterSceneComponentSyncParent;

/**
 * VR root. This pawn represents VR hierarchy in the game.
 */
UCLASS()
class DISPLAYCLUSTER_API ADisplayClusterPawn
	: public APawn
{
	GENERATED_BODY()

public:
	ADisplayClusterPawn(const FObjectInitializer& ObjectInitializer);

	inline USphereComponent* GetCollisionComponent() const
	{ return CollisionComponent; }

	inline UDisplayClusterSceneComponent* GetCollisionOffsetComponent() const
	{ return CollisionOffsetComponent; }

	inline UCameraComponent* GetCameraComponent() const
	{ return CameraComponent; }

public:
	/** Scene component. Specifies translation (DisplayCluster hierarchy navigation) direction. */
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	USceneComponent* TranslationDirection;

	/** Scene component. Specifies rotation center (DisplayCluster hierarchy rotation). */
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	USceneComponent* RotationAround;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// APawn
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DisplayCluster")
	UCameraComponent* CameraComponent;

	/** Collision component */
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USphereComponent* CollisionComponent;

	/** Used as 'second' root for any childs (whole hierarchy offset) */
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UDisplayClusterSceneComponent* CollisionOffsetComponent;

private:
	UPROPERTY()
	UDisplayClusterSceneComponentSyncParent* DisplayClusterSyncRoot;
	
	UPROPERTY()
	UDisplayClusterSceneComponentSyncParent* DisplayClusterSyncCollisionOffset;

	bool bIsCluster;
};
