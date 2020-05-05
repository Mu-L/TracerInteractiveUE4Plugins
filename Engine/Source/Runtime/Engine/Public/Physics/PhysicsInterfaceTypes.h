// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineGlobals.h"
#include "Engine/EngineTypes.h"
#include "PhysicsInterfaceDeclares.h"
#include "PhysicsEngine/BodySetupEnums.h"
#include "PhysicsInterfaceTypesCore.h"

#if WITH_CHAOS
#include "Chaos/Serializable.h"

namespace Chaos
{
	class FImplicitObjectUnion;

	class FImplicitObject;

	class FTriangleMeshImplicitObject;
}
#endif

class UPhysicalMaterialMask;
class UMaterialInterface;

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

struct FPhysicalMaterialMaskParams
{
	/** Physical materials mask */
	UPhysicalMaterialMask* PhysicalMaterialMask;

	/** Pointer to material which contains the physical material map */
	UMaterialInterface* PhysicalMaterialMap;
};

struct FGeometryAddParams
{
	bool bDoubleSided;
	FBodyCollisionData CollisionData;
	ECollisionTraceFlag CollisionTraceType;
	FVector Scale;
	UPhysicalMaterial* SimpleMaterial;
	TArrayView<UPhysicalMaterial*> ComplexMaterials;
#if WITH_CHAOS
	TArrayView<FPhysicalMaterialMaskParams> ComplexMaterialMasks;
#endif
	FTransform LocalTransform;
	FTransform WorldTransform;
	FKAggregateGeom* Geometry;
	// FPhysicsInterfaceTriMesh - Per implementation
#if WITH_PHYSX
	TArrayView<physx::PxTriangleMesh*> TriMeshes;
#endif
#if WITH_CHAOS
	TArrayView<TSharedPtr<Chaos::FTriangleMeshImplicitObject, ESPMode::ThreadSafe>> ChaosTriMeshes;
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