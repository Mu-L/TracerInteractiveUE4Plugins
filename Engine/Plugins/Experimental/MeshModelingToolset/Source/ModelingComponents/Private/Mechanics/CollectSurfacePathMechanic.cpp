// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/CollectSurfacePathMechanic.h"
#include "ToolSceneQueriesUtil.h"
#include "MeshQueries.h"
#include "SceneManagement.h"
#include "MeshTransforms.h"
#include "Distance/DistLine3Ray3.h"
#include "Util/ColorConstants.h"
#include "MeshNormals.h"



UCollectSurfacePathMechanic::UCollectSurfacePathMechanic()
{
	SpatialSnapPointsFunc = [this](FVector3d A, FVector3d B) { return A.DistanceSquared(B) < (ConstantSnapDistance * ConstantSnapDistance); };

	PathColor = LinearColors::VideoRed3f();
	PreviewColor = LinearColors::Orange3f();
	PathCompleteColor = LinearColors::LightGreen3f();

	PathDrawer.LineColor = PathColor;
	PathDrawer.LineThickness = 4.0f;
	PathDrawer.PointSize = 8.0f;
	PathDrawer.bDepthTested = false;
}


void UCollectSurfacePathMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);
}

void UCollectSurfacePathMechanic::Shutdown()
{
	UInteractionMechanic::Shutdown();


}

void UCollectSurfacePathMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bDrawPath == true)
	{
		PathDrawer.BeginFrame(RenderAPI);

		if (HitPath.Num() > 0)
		{
			const FLinearColor& DrawPathColor = (bCurrentPreviewWillComplete || bGeometricCloseOcurred) ? PathCompleteColor : PathColor;
			int32 NumPoints = HitPath.Num() - 1;
			for (int32 k = 0; k < NumPoints; ++k)
			{
				PathDrawer.DrawLine(HitPath[k].Origin, HitPath[k + 1].Origin, DrawPathColor);
			}

			const FLinearColor& DrawPreviewColor = (bCurrentPreviewWillComplete || bGeometricCloseOcurred) ? PathCompleteColor : PreviewColor;
			PathDrawer.DrawLine(HitPath[NumPoints].Origin, PreviewPathPoint.Origin, DrawPreviewColor);
		}

		PathDrawer.DrawPoint(PreviewPathPoint.Origin, PreviewColor, PathDrawer.PointSize, PathDrawer.bDepthTested);

		PathDrawer.EndFrame();
	}
}


void UCollectSurfacePathMechanic::InitializeMeshSurface(FDynamicMesh3&& TargetSurfaceMesh)
{
	TargetSurface = MoveTemp(TargetSurfaceMesh);
	TargetSurfaceAABB.SetMesh(&TargetSurface);
}

void UCollectSurfacePathMechanic::InitializePlaneSurface(const FFrame3d& TargetPlaneIn)
{
	TargetPlane = TargetPlaneIn;
	bHaveTargetPlane = true;
}


void UCollectSurfacePathMechanic::SetFixedNumPointsMode(int32 NumPoints)
{
	check(NumPoints > 0 && NumPoints < 100);
	DoneMode = ECollectSurfacePathDoneMode::FixedNumPoints;
	FixedPointTargetCount = NumPoints;
}

void UCollectSurfacePathMechanic::SetCloseWithLambdaMode()
{
	check(IsDoneFunc);
	DoneMode = ECollectSurfacePathDoneMode::ExternalLambda;
}

void UCollectSurfacePathMechanic::SetDrawClosedLoopMode()
{
	DoneMode = ECollectSurfacePathDoneMode::SnapCloseLoop;
}

void UCollectSurfacePathMechanic::SetDoubleClickOrCloseLoopMode()
{
	DoneMode = ECollectSurfacePathDoneMode::SnapDoubleClickOrCloseLoop;
}


bool UCollectSurfacePathMechanic::IsHitByRay(const FRay3d& Ray, FFrame3d& HitPoint)
{
	return RayToPathPoint(Ray, HitPoint, false);
}


bool UCollectSurfacePathMechanic::UpdatePreviewPoint(const FRay3d& Ray)
{
	FFrame3d PreviewPoint;
	if ( RayToPathPoint(Ray, PreviewPoint, true ) == false)
	{
		return false;
	}

	PreviewPathPoint = PreviewPoint;

	bCurrentPreviewWillComplete = CheckGeometricClosure(PreviewPathPoint);

	return true;
}


bool UCollectSurfacePathMechanic::TryAddPointFromRay(const FRay3d& Ray)
{
	FFrame3d NewPoint;
	if (RayToPathPoint(Ray, NewPoint, true) == false)
	{
		return false;
	}

	if (CheckGeometricClosure(NewPoint))
	{
		bGeometricCloseOcurred = true;
	}
	else
	{
		HitPath.Add(NewPoint);
	}

	bCurrentPreviewWillComplete = false;
	return true;
}


bool UCollectSurfacePathMechanic::PopLastPoint()
{
	if (HitPath.Num() > 0)
	{
		HitPath.RemoveAt(HitPath.Num() - 1);
		return true;
	}
	return false;
}


bool UCollectSurfacePathMechanic::RayToPathPoint(const FRay3d& Ray, FFrame3d& PointOut, bool bEnableSnapping)
{
	bool bHaveHit = false;
	FFrame3d NearestHitFrame;
	double NearestHitDistSqr = TNumericLimits<double>::Max();

	if (TargetSurface.TriangleCount() > 0)
	{
		int32 HitTri = TargetSurfaceAABB.FindNearestHitTriangle(Ray);
		if (HitTri != FDynamicMesh3::InvalidID)
		{
			FIntrRay3Triangle3d Hit = TMeshQueries<FDynamicMesh3>::RayTriangleIntersection(TargetSurface, HitTri, Ray);
			NearestHitFrame = TargetSurface.GetTriFrame(HitTri);
			NearestHitFrame.Origin = Hit.Triangle.BarycentricPoint(Hit.TriangleBaryCoords);
			NearestHitDistSqr = Ray.Project(NearestHitFrame.Origin);
			bHaveHit = true;
		}
	}

	if (bHaveTargetPlane)
	{
		FFrame3d PlaneHit(TargetPlane);
		if (TargetPlane.RayPlaneIntersection(Ray.Origin, Ray.Direction, 2, PlaneHit.Origin))
		{
			double HitDistSqr = Ray.Project(PlaneHit.Origin);
			if (HitDistSqr < NearestHitDistSqr)
			{
				NearestHitDistSqr = HitDistSqr;
				NearestHitFrame = PlaneHit;
				bHaveHit = true;
			}
		}
	}

	if (bHaveHit == false)
	{
		return false;
	}

	PointOut = NearestHitFrame;

	// try snapping to close if we are in loop mode
	bool bHaveSnapped = false;
	if (DoneMode == ECollectSurfacePathDoneMode::SnapCloseLoop && HitPath.Num() > 2)
	{
		if (SpatialSnapPointsFunc(PointOut.Origin, HitPath.Last().Origin))
		{
			PointOut = HitPath.Last();
			bHaveSnapped = true;
		}
	}

	// try snapping to other things, if we haven't yet
	if (bEnableSnapping && bHaveSnapped == false)
	{
		if (bSnapToTargetMeshVertices && TargetSurface.TriangleCount() > 0)
		{
			double NearDistSqr;
			int32 NearestVID = TargetSurfaceAABB.FindNearestVertex(PointOut.Origin, NearDistSqr);
			if (NearestVID != FDynamicMesh3::InvalidID)
			{
				FVector3d NearestVertexPos = TargetSurface.GetVertex(NearestVID);
				if (SpatialSnapPointsFunc(PointOut.Origin, NearestVertexPos))
				{
					PointOut.Origin = NearestVertexPos;
					PointOut.AlignAxis(2, FMeshNormals::ComputeVertexNormal(TargetSurface, NearestVID));
				}
			}
		}
	}

	return true;
}


bool UCollectSurfacePathMechanic::IsDone() const
{
	if (DoneMode == ECollectSurfacePathDoneMode::FixedNumPoints)
	{
		return HitPath.Num() >= FixedPointTargetCount;
	}
	else if (DoneMode == ECollectSurfacePathDoneMode::ExternalLambda)
	{
		return IsDoneFunc();
	}
	else if (DoneMode == ECollectSurfacePathDoneMode::SnapCloseLoop || DoneMode == ECollectSurfacePathDoneMode::SnapDoubleClick || DoneMode == ECollectSurfacePathDoneMode::SnapDoubleClickOrCloseLoop)
	{
		return bGeometricCloseOcurred;
	}
	ensure(false);
	return false;
}


bool UCollectSurfacePathMechanic::CheckGeometricClosure(const FFrame3d& Point)
{
	if (HitPath.Num() == 0)
	{
		return false;
	}

	if (DoneMode == ECollectSurfacePathDoneMode::SnapCloseLoop || DoneMode == ECollectSurfacePathDoneMode::SnapDoubleClickOrCloseLoop)
	{
		if (HitPath.Num() > 2)
		{
			const FFrame3d& FirstPoint = HitPath[0];
			if (SpatialSnapPointsFunc(Point.Origin, FirstPoint.Origin))
			{
				bLoopWasClosed = true;		// We finished by clicking on the first point
				return true;
			}
		}
	}

	if (DoneMode == ECollectSurfacePathDoneMode::SnapDoubleClick || DoneMode == ECollectSurfacePathDoneMode::SnapDoubleClickOrCloseLoop)
	{
		if (HitPath.Num() > 1)
		{
			const FFrame3d& LastPoint = HitPath[HitPath.Num() - 1];
			if (SpatialSnapPointsFunc(Point.Origin, LastPoint.Origin))
			{
				return true;
			}
		}
	}

	return false;
}
