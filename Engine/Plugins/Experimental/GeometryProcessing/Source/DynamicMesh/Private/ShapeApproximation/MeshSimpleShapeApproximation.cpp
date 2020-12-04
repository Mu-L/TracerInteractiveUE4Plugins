// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShapeApproximation/MeshSimpleShapeApproximation.h"
#include "Async/ParallelFor.h"

#include "MinVolumeSphere3.h"
#include "MinVolumeBox3.h"
#include "FitCapsule3.h"
//#include "DynamicMeshAABBTree3.h"

#include "ShapeApproximation/ShapeDetection3.h"
#include "MeshQueries.h"
#include "Operations/MeshConvexHull.h"
#include "Operations/MeshProjectionHull.h"



void FMeshSimpleShapeApproximation::DetectAndCacheSimpleShapeType(const FDynamicMesh3* SourceMesh, FSourceMeshCache& CacheOut)
{
	if (UE::Geometry::IsBoxMesh(*SourceMesh, CacheOut.DetectedBox))
	{
		CacheOut.DetectedType = EDetectedSimpleShapeType::Box;
	}
	else if (UE::Geometry::IsSphereMesh(*SourceMesh, CacheOut.DetectedSphere))
	{
		CacheOut.DetectedType = EDetectedSimpleShapeType::Sphere;
	}
	else if (UE::Geometry::IsCapsuleMesh(*SourceMesh, CacheOut.DetectedCapsule))
	{
		CacheOut.DetectedType = EDetectedSimpleShapeType::Capsule;
	}
}




void FMeshSimpleShapeApproximation::InitializeSourceMeshes(const TArray<const FDynamicMesh3*>& InputMeshSet)
{
	SourceMeshes = InputMeshSet;
	SourceMeshCaches.Reset();
	SourceMeshCaches.SetNum(SourceMeshes.Num());

	ParallelFor(SourceMeshes.Num(), [&](int32 k) {
		DetectAndCacheSimpleShapeType( SourceMeshes[k], SourceMeshCaches[k]);
	});

}







bool FMeshSimpleShapeApproximation::GetDetectedSimpleShape(
	const FSourceMeshCache& Cache,
	FSimpleShapeSet3d& ShapeSetOut,
	FCriticalSection& ShapeSetLock)
{
	if (Cache.DetectedType == EDetectedSimpleShapeType::Sphere && bDetectSpheres)
	{
		ShapeSetLock.Lock();
		ShapeSetOut.Spheres.Add(Cache.DetectedSphere);
		ShapeSetLock.Unlock();
		return true;
	}
	else if (Cache.DetectedType == EDetectedSimpleShapeType::Box && bDetectBoxes)
	{
		ShapeSetLock.Lock();
		ShapeSetOut.Boxes.Add(Cache.DetectedBox);
		ShapeSetLock.Unlock();
		return true;
	}
	else if (Cache.DetectedType == EDetectedSimpleShapeType::Capsule && bDetectCapsules)
	{
		ShapeSetLock.Lock();
		ShapeSetOut.Capsules.Add(Cache.DetectedCapsule);
		ShapeSetLock.Unlock();
		return true;
	}

	return false;
}





struct FSimpleShapeFitsResult
{
	bool bHaveSphere = false;
	FSphere3d Sphere;

	bool bHaveBox = false;
	FOrientedBox3d Box;

	bool bHaveCapsule = false;
	FCapsule3d Capsule;

	bool bHaveConvex = false;
	FDynamicMesh3 Convex;
};



static void ComputeSimpleShapeFits(const FDynamicMesh3& Mesh,
	bool bSphere, bool bBox, bool bCapsule, bool bConvex, FSimpleShapeFitsResult& FitResult)
{
	TArray<int32> ToLinear, FromLinear;
	if (bSphere || bBox || bCapsule)
	{
		FromLinear.SetNum(Mesh.VertexCount());
		int32 LinearIndex = 0;
		for (int32 vid : Mesh.VertexIndicesItr())
		{
			FromLinear[LinearIndex++] = vid;
		}
	}

	FitResult.bHaveBox = false;
	if (bBox)
	{
		FMinVolumeBox3d MinBoxCalc;
		bool bMinBoxOK = MinBoxCalc.Solve(FromLinear.Num(),
			[&](int32 Index) { return Mesh.GetVertex(FromLinear[Index]); });
		if (bMinBoxOK && MinBoxCalc.IsSolutionAvailable())
		{
			FitResult.bHaveBox = true;
			MinBoxCalc.GetResult(FitResult.Box);
		}
	}

	FitResult.bHaveSphere = false;
	if (bSphere)
	{
		FMinVolumeSphere3d MinSphereCalc;
		bool bMinSphereOK = MinSphereCalc.Solve(FromLinear.Num(),
			[&](int32 Index) { return Mesh.GetVertex(FromLinear[Index]); });
		if (bMinSphereOK && MinSphereCalc.IsSolutionAvailable())
		{
			FitResult.bHaveSphere = true;
			MinSphereCalc.GetResult(FitResult.Sphere);
		}
	}

	FitResult.bHaveCapsule = false;
	if (bCapsule)
	{
		FitResult.bHaveCapsule = TFitCapsule3<double>::Solve(FromLinear.Num(),
			[&](int32 Index) { return Mesh.GetVertex(FromLinear[Index]); }, FitResult.Capsule);
	}

	// todo: once we have computed convex we can use it to compute box
	FitResult.bHaveConvex = false;
	if (bConvex)
	{
		FMeshConvexHull Hull(&Mesh);
		if (Hull.Compute())
		{
			FitResult.bHaveConvex = true;
			FitResult.Convex = MoveTemp(Hull.ConvexHull);
		}
	}
}





void FMeshSimpleShapeApproximation::Generate_AlignedBoxes(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		FAxisAlignedBox3d Bounds = SourceMeshes[idx]->GetBounds();

		FBoxShape3d NewBox;
		NewBox.Box = FOrientedBox3d(Bounds);

		GeometryLock.Lock();
		ShapeSetOut.Boxes.Add(NewBox);
		GeometryLock.Unlock();
	});
}



void FMeshSimpleShapeApproximation::Generate_OrientedBoxes(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& SourceMesh = *SourceMeshes[idx];
		FSimpleShapeFitsResult FitResult;
		ComputeSimpleShapeFits(SourceMesh, false, true, false, false, FitResult);

		if (FitResult.bHaveBox)
		{
			GeometryLock.Lock();
			ShapeSetOut.Boxes.Add(FBoxShape3d(FitResult.Box));
			GeometryLock.Unlock();
		}
	});
}

void FMeshSimpleShapeApproximation::Generate_MinimalSpheres(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& SourceMesh = *SourceMeshes[idx];
		FSimpleShapeFitsResult FitResult;
		ComputeSimpleShapeFits(SourceMesh, true, false, false, false, FitResult);

		if (FitResult.bHaveSphere)
		{
			GeometryLock.Lock();
			ShapeSetOut.Spheres.Add(FSphereShape3d(FitResult.Sphere));
			GeometryLock.Unlock();
		}
	});
}

void FMeshSimpleShapeApproximation::Generate_Capsules(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& SourceMesh = *SourceMeshes[idx];
		FSimpleShapeFitsResult FitResult;
		ComputeSimpleShapeFits(SourceMesh, false, false, true, false, FitResult);

		if (FitResult.bHaveCapsule)
		{
			GeometryLock.Lock();
			ShapeSetOut.Capsules.Add(FCapsuleShape3d(FitResult.Capsule));
			GeometryLock.Unlock();
		}
	});
}



void FMeshSimpleShapeApproximation::Generate_ConvexHulls(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& SourceMesh = *SourceMeshes[idx];
		FMeshConvexHull Hull(&SourceMesh);

		Hull.bPostSimplify = bSimplifyHulls;
		Hull.MaxTargetFaceCount = HullTargetFaceCount;

		if (Hull.Compute())
		{
			FConvexShape3d NewConvex;
			NewConvex.Mesh = MoveTemp(Hull.ConvexHull);
			GeometryLock.Lock();
			ShapeSetOut.Convexes.Add(NewConvex);
			GeometryLock.Unlock();
		}
	});
}






void FMeshSimpleShapeApproximation::Generate_ProjectedHulls(FSimpleShapeSet3d& ShapeSetOut, EProjectedHullAxisMode AxisMode)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& Mesh = *SourceMeshes[idx];

		FFrame3d ProjectionPlane(FVector3d::Zero(), FVector3d::UnitY());
		if (AxisMode == EProjectedHullAxisMode::SmallestBoxDimension)
		{
			FAxisAlignedBox3d Bounds = Mesh.GetBounds();
			int32 AxisIndex = Bounds.Diagonal().MinAbsElementIndex();
			check(Bounds.Diagonal().MinAbsElement() == Bounds.Diagonal()[AxisIndex]);
			ProjectionPlane = FFrame3d(FVector3d::Zero(), FVector3d::MakeUnit(AxisIndex));
		}
		else if (AxisMode == EProjectedHullAxisMode::SmallestVolume)
		{
			FMeshProjectionHull HullX(&Mesh);
			HullX.ProjectionFrame = FFrame3d(FVector3d::Zero(), FVector3d::UnitX());
			HullX.MinThickness = FMathd::Max(MinDimension, 0);
			bool bHaveX = HullX.Compute();
			FMeshProjectionHull HullY(&Mesh);
			HullY.ProjectionFrame = FFrame3d(FVector3d::Zero(), FVector3d::UnitY());
			HullY.MinThickness = FMathd::Max(MinDimension, 0);
			bool bHaveY = HullY.Compute();
			FMeshProjectionHull HullZ(&Mesh);
			HullZ.ProjectionFrame = FFrame3d(FVector3d::Zero(), FVector3d::UnitZ());
			HullZ.MinThickness = FMathd::Max(MinDimension, 0);
			bool bHaveZ = HullZ.Compute();
			int32 MinIdx = FMathd::Min3Index(
				(bHaveX) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(HullX.ConvexHull3D).X : TNumericLimits<double>::Max(),
				(bHaveY) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(HullY.ConvexHull3D).X : TNumericLimits<double>::Max(),
				(bHaveZ) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(HullZ.ConvexHull3D).X : TNumericLimits<double>::Max());
			ProjectionPlane = (MinIdx == 0) ? HullX.ProjectionFrame : ((MinIdx == 1) ? HullY.ProjectionFrame : HullZ.ProjectionFrame);
		}
		else
		{
			ProjectionPlane = FFrame3d(FVector3d::Zero(), FVector3d::MakeUnit((int32)AxisMode));
		}

		FMeshProjectionHull Hull(&Mesh);
		Hull.ProjectionFrame = ProjectionPlane;
		Hull.MinThickness = FMathd::Max(MinDimension, 0);
		Hull.bSimplifyPolygon = bSimplifyHulls;
		Hull.MinEdgeLength = HullSimplifyTolerance;
		Hull.DeviationTolerance = HullSimplifyTolerance;

		if (Hull.Compute())
		{
			FConvexShape3d NewConvex;
			NewConvex.Mesh = MoveTemp(Hull.ConvexHull3D);
			GeometryLock.Lock();
			ShapeSetOut.Convexes.Add(NewConvex);
			GeometryLock.Unlock();
		}
	});
}




void FMeshSimpleShapeApproximation::Generate_MinVolume(FSimpleShapeSet3d& ShapeSetOut)
{
	FCriticalSection GeometryLock;
	ParallelFor(SourceMeshes.Num(), [&](int32 idx)
	{
		if (GetDetectedSimpleShape(SourceMeshCaches[idx], ShapeSetOut, GeometryLock))
		{
			return;
		}

		const FDynamicMesh3& SourceMesh = *SourceMeshes[idx];

		FOrientedBox3d AlignedBox = FOrientedBox3d(SourceMesh.GetBounds());

		FSimpleShapeFitsResult FitResult;
		ComputeSimpleShapeFits(SourceMesh, true, true, true, false, FitResult);

		double Volumes[4];
		Volumes[0] = AlignedBox.Volume();
		Volumes[1] = (FitResult.bHaveBox) ? FitResult.Box.Volume() : TNumericLimits<double>::Max();
		Volumes[2] = (FitResult.bHaveSphere) ? FitResult.Sphere.Volume() : TNumericLimits<double>::Max();
		Volumes[3] = (FitResult.bHaveCapsule) ? FitResult.Capsule.Volume() : TNumericLimits<double>::Max();

		int32 MinVolIndex = 0;
		for (int32 k = 1; k < 4; ++k)
		{
			if (Volumes[k] < Volumes[MinVolIndex])
			{
				MinVolIndex = k;
			}
		}

		if (Volumes[MinVolIndex] < TNumericLimits<double>::Max())
		{
			GeometryLock.Lock();
			switch (MinVolIndex)
			{
			case 0:
				ShapeSetOut.Boxes.Add(FBoxShape3d(AlignedBox));
				break;
			case 1:
				ShapeSetOut.Boxes.Add(FBoxShape3d(FitResult.Box));
				break;
			case 2:
				ShapeSetOut.Spheres.Add(FSphereShape3d(FitResult.Sphere));
				break;
			case 3:
				ShapeSetOut.Capsules.Add(FCapsuleShape3d(FitResult.Capsule));
				break;
			}
			GeometryLock.Unlock();
		}
	});
}