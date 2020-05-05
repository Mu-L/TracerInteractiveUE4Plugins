// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "HAL/ThreadSafeBool.h"
#include "PhysicsEngine/BodySetup.h"

namespace physx
{
	class PxCooking;
	class PxTriangleMesh;
	class PxConvexMesh;
	class PxHeightField;
	class PxFoundation;
	struct PxCookingParams;
}

class IPhysXCookingModule;

#if WITH_PHYSX
/** Helper for physics cooking */
struct ENGINE_API FPhysXCookHelper
{
	FPhysXCookHelper(IPhysXCookingModule* InPhysXCookingModule);

	/** Cooks based on CookInfo and saves the results into the output data */
	bool CreatePhysicsMeshes_Concurrent();

	/** Cooks based on CookInfo and saves the results into the output data. Calls back into the delegate on the game thread when done */
	void CreatePhysicsMeshesAsync_Concurrent(FSimpleDelegateGraphTask::FDelegate FinishDelegate);

	static bool HasSomethingToCook(const FCookBodySetupInfo& InCookInfo)
	{
		return InCookInfo.bCookTriMesh || InCookInfo.bCookNonMirroredConvex || InCookInfo.bCookMirroredConvex;
	}

	void Abort() { bShouldAbort.AtomicSet(true); }

	FCookBodySetupInfo CookInfo;	//Use this with UBodySetup::GetCookInfo (must be called on game thread). If you already have the info just override it manually

	//output
	TArray<physx::PxConvexMesh*> OutNonMirroredConvexMeshes;
	TArray<physx::PxConvexMesh*> OutMirroredConvexMeshes;
	TArray<physx::PxTriangleMesh*> OutTriangleMeshes;
	FBodySetupUVInfo OutUVInfo;
private:
	void CreateConvexElements_Concurrent(const TArray<TArray<FVector>>& Elements, TArray<physx::PxConvexMesh*>& OutConvexMeshes, bool bFlipped);

	IPhysXCookingModule* PhysXCookingModule;
	FThreadSafeBool bShouldAbort;	
};
#endif //WITH_PHYSX
