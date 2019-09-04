// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineGlobals.h"
#include "Engine/EngineTypes.h"
#include "PhysicsInterfaceDeclares.h"
#include "PhysicsEngine/BodySetupEnums.h"
#include "PhysicsInterfaceTypesCore.h"


// Defines for enabling hitch repeating (see ScopedSQHitchRepeater.h)
#if !UE_BUILD_SHIPPING
#define DETECT_SQ_HITCHES 1
#endif

#ifndef DETECT_SQ_HITCHES
#define DETECT_SQ_HITCHES 0
#endif

struct FKAggregateGeom;

namespace physx
{
	class PxShape;
	class PxTriangleMesh;
}

struct FGeometryAddParams
{
	bool bDoubleSided;
	FBodyCollisionData CollisionData;
	ECollisionTraceFlag CollisionTraceType;
	FVector Scale;
	UPhysicalMaterial* SimpleMaterial;
	TArrayView<UPhysicalMaterial*> ComplexMaterials;
	FTransform LocalTransform;
	FKAggregateGeom* Geometry;
	// FPhysicsInterfaceTriMesh - Per implementation
#if WITH_PHYSX
	TArrayView<physx::PxTriangleMesh*> TriMeshes;
#endif
};

namespace PhysicsInterfaceTypes
{
	enum class ELimitAxis : uint8
	{
		X,
		Y,
		Z,
		Twist,
		Swing1,
		Swing2
	};

	enum class EDriveType : uint8
	{
		X,
		Y,
		Z,
		Swing,
		Twist,
		Slerp
	};

	/**
	* Default number of inlined elements used in FInlineShapeArray.
	* Increase if for instance character meshes use more than this number of physics bodies and are involved in many queries.
	*/
	enum { NumInlinedPxShapeElements = 32 };

	/** Array that is intended for use when fetching shapes from a rigid body. */
	typedef TArray<FPhysicsShapeHandle, TInlineAllocator<NumInlinedPxShapeElements>> FInlineShapeArray;
}