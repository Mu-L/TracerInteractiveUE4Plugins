// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in Fortnite-Dev-Physics stream
struct CORE_API FExternalPhysicsCustomObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Format change, Removed Convex Hulls From Triangle Mesh Implicit Object
		RemovedConvexHullsFromTriangleMeshImplicitObject,

		// Add serialization for particle bounds
		SerializeParticleBounds,

		// Add BV serialization for evolution
		SerializeEvolutionBV,

		// Allow evolution to swap acceleration structures
		SerializeEvolutionGenericAcceleration,

		//Global elements have bounds so we can skip them
		GlobalElementsHaveBounds,

		//SpatialIdx serialized
		SpatialIdxSerialized,

		//Save out heightfield data
		HeightfieldData,

		//Save out multiple acceleration structures
		SerializeMultiStructures,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FExternalPhysicsCustomObjectVersion() {}
};
