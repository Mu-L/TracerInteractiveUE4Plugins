// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "UObject/ObjectMacros.h"
#include "ClothingSystemRuntimeTypes.generated.h"

// Data produced by a clothing simulation
struct FClothSimulData
{
	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
	}

	// Positions of the simulation mesh particles
	TArray<FVector> Positions;

	// Normals at the simulation mesh particles
	TArray<FVector> Normals;

	// Transform applied per position/normal element when loaded
	FTransform Transform;

	// Transform relative to the component to update clothing root transform when not ticking clothing but rendering a component
	FTransform ComponentRelativeTransform;
};

enum class EClothingTeleportMode : uint8
{
	// No teleport, simulate as normal
	None = 0,
	// Teleport the simulation, causing no intertial effects but keep the sim mesh shape
	Teleport,
	// Teleport the simulation, causing no intertial effects and reset the sim mesh shape
	TeleportAndReset
};

/** Data for a single sphere primitive in the clothing simulation. This can either be a 
 *  sphere on its own, or part of a capsule referenced by the indices in FClothCollisionPrim_Capsule
 */
USTRUCT()
struct FClothCollisionPrim_Sphere
{
	GENERATED_BODY()

	UPROPERTY()
	int32 BoneIndex;

	UPROPERTY()
	float Radius;

	UPROPERTY()
	FVector LocalPosition;
};

/** Data for a single connected sphere primitive. This should be configured after all spheres have
 *  been processed as they are really just indexing the existing spheres
 */
USTRUCT()
struct FClothCollisionPrim_SphereConnection
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SphereIndices[2];
};

/**
 *	Data for a single convex element
 *	A convex is a collection of planes, in which the clothing will attempt to stay outside of the
 *	shape created by the planes combined.
 */
USTRUCT()
struct FClothCollisionPrim_Convex
{
	GENERATED_BODY()

	FClothCollisionPrim_Convex()
		: BoneIndex(INDEX_NONE)
	{

	}

	UPROPERTY()
	TArray<FPlane> Planes;

	UPROPERTY()
	int32 BoneIndex;
};

USTRUCT()
struct CLOTHINGSYSTEMRUNTIMEINTERFACE_API FClothCollisionData
{
	GENERATED_BODY()

	void Reset();

	void Append(const FClothCollisionData& InOther);

	// Sphere data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_Sphere> Spheres;

	// Capsule data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_SphereConnection> SphereConnections;

	// Convex Data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_Convex> Convexes;
};
