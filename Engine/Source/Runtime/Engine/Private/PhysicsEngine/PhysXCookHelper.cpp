// Copyright Epic Games, Inc. All Rights Reserved.


#include "PhysXCookHelper.h"
#include "PhysXSupport.h"
#include "IPhysXCookingModule.h"

#if WITH_PHYSX

#include "IPhysXCooking.h"

FPhysXCookHelper::FPhysXCookHelper(IPhysXCookingModule* InPhysXCookingModule)
	: PhysXCookingModule(InPhysXCookingModule)
{
}

bool FPhysXCookHelper::CreatePhysicsMeshes_Concurrent()
{
	bool bSuccess = true;

	CreateConvexElements_Concurrent(CookInfo.NonMirroredConvexVertices, OutNonMirroredConvexMeshes, false);
	CreateConvexElements_Concurrent(CookInfo.MirroredConvexVertices, OutMirroredConvexMeshes, true);

	if (CookInfo.bCookTriMesh && !CookInfo.bTriMeshError)
	{
		OutTriangleMeshes.AddZeroed();
		const bool bError = !PhysXCookingModule->GetPhysXCooking()->CreateTriMesh(FPlatformProperties::GetPhysicsFormat(), CookInfo.TriMeshCookFlags, CookInfo.TriangleMeshDesc.Vertices, CookInfo.TriangleMeshDesc.Indices, CookInfo.TriangleMeshDesc.MaterialIndices, CookInfo.TriangleMeshDesc.bFlipNormals, OutTriangleMeshes[0]);
		if (bError)
		{
			bSuccess = false;
			UE_LOG(LogPhysics, Warning, TEXT("Failed to cook TriMesh: %s."), *CookInfo.OuterDebugName);
		}
		else if (CookInfo.bSupportUVFromHitResults)
		{
			OutUVInfo.FillFromTriMesh(CookInfo.TriangleMeshDesc);
		}
	}

    return bSuccess;
}

void FPhysXCookHelper::CreatePhysicsMeshesAsync_Concurrent(FSimpleDelegateGraphTask::FDelegate FinishDelegate)
{
	CreatePhysicsMeshes_Concurrent();
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(FinishDelegate, GET_STATID(STAT_PhysXCooking), nullptr, ENamedThreads::GameThread);
}

void FPhysXCookHelper::CreateConvexElements_Concurrent(const TArray<TArray<FVector>>& Elements, TArray<PxConvexMesh*>& OutConvexMeshes, bool bFlipped)
{
	OutMirroredConvexMeshes.Reserve(Elements.Num());
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
	{
		OutConvexMeshes.AddZeroed();
		const EPhysXCookingResult Result = PhysXCookingModule->GetPhysXCooking()->CreateConvex(FPlatformProperties::GetPhysicsFormat(), CookInfo.ConvexCookFlags, Elements[ElementIndex], OutConvexMeshes.Last());
		switch (Result)
		{
		case EPhysXCookingResult::Succeeded:
			break;
		case EPhysXCookingResult::Failed:
			UE_LOG(LogPhysics, Warning, TEXT("Failed to cook convex: %s %d (FlipX:%d). The remaining elements will not get cooked."), *CookInfo.OuterDebugName, ElementIndex, bFlipped ? 1 : 0);
			break;
		case EPhysXCookingResult::SucceededWithInflation:
			if (!CookInfo.bConvexDeformableMesh)
			{
				UE_LOG(LogPhysics, Warning, TEXT("Cook convex: %s %d (FlipX:%d) failed but succeeded with inflation.  The mesh should be looked at."), *CookInfo.OuterDebugName, ElementIndex, bFlipped ? 1 : 0);
			}
			else
			{
				UE_LOG(LogPhysics, Log, TEXT("Cook convex: %s %d (FlipX:%d) required inflation. You may wish to adjust the mesh so this is not necessary."), *CookInfo.OuterDebugName, ElementIndex, bFlipped ? 1 : 0);
			}
			break;
		default:
			check(false);
		}
	}
}

#endif //WITH_PHYSX
