// Copyright Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/RemeshMeshOp.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshAttributeSet.h"
#include "Remesher.h"
#include "QueueRemesher.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"
#include "MeshNormals.h"
#include "NormalFlowRemesher.h"

namespace
{
	TUniquePtr<FRemesher> RemesherFactory(ERemeshType Type, FDynamicMesh3* TargetMesh)
	{
		switch(Type)
		{
		case ERemeshType::Standard:
			return MakeUnique<FQueueRemesher>(TargetMesh);
		case ERemeshType::FullPass:
			return MakeUnique<FRemesher>(TargetMesh);
		case ERemeshType::NormalFlow:
			return MakeUnique<FNormalFlowRemesher>(TargetMesh);
		default:
			check(!"Encountered unexpected Remesh Type");
			return nullptr;
		}
	}

}

void FRemeshMeshOp::SetTransform(const FTransform& Transform)
{
	ResultTransform = (FTransform3d)Transform;
}

void FRemeshMeshOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	bool bDiscardAttributesImmediately = bDiscardAttributes && !bPreserveSharpEdges;
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributesImmediately);

	if (Progress->Cancelled())
	{
		return;
	}

	FDynamicMesh3* TargetMesh = ResultMesh.Get();

	TUniquePtr<FRemesher> Remesher = RemesherFactory(RemeshType, TargetMesh);

	Remesher->bEnableSplits = bSplits;
	Remesher->bEnableFlips = bFlips;
	Remesher->bEnableCollapses = bCollapses;

	Remesher->SetTargetEdgeLength(TargetEdgeLength);

	Remesher->ProjectionMode = (bReproject) ? 
		FRemesher::ETargetProjectionMode::AfterRefinement : FRemesher::ETargetProjectionMode::NoProjection;

	Remesher->bEnableSmoothing = (SmoothingStrength > 0);
	Remesher->SmoothSpeedT = SmoothingStrength;
	// convert smooth type from UI enum to (currently 1:1) FRemesher enum
	Remesher->SmoothType = FRemesher::ESmoothTypes::Uniform;
	if (!bDiscardAttributes)
	{
		switch (SmoothingType)
		{
		case ERemeshSmoothingType::Uniform:
			Remesher->SmoothType = FRemesher::ESmoothTypes::Uniform;
			Remesher->FlipMetric = FRemesher::EFlipMetric::OptimalValence;
			break;
		case ERemeshSmoothingType::Cotangent:
			Remesher->SmoothType = FRemesher::ESmoothTypes::Cotan;
			Remesher->FlipMetric = FRemesher::EFlipMetric::MinEdgeLength;
			break;
		case ERemeshSmoothingType::MeanValue:
			Remesher->SmoothType = FRemesher::ESmoothTypes::MeanValue;
			Remesher->FlipMetric = FRemesher::EFlipMetric::MinEdgeLength;
			break;
		default:
			ensure(false);
		}
	}
	bool bIsUniformSmooth = (Remesher->SmoothType == FRemesher::ESmoothTypes::Uniform);

	Remesher->bPreventNormalFlips = bPreventNormalFlips;

	Remesher->DEBUG_CHECK_LEVEL = 0;

	FMeshConstraints constraints;
	FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(constraints, *TargetMesh,
														 MeshBoundaryConstraint,
														 GroupBoundaryConstraint,
														 MaterialBoundaryConstraint,
														 true, !bPreserveSharpEdges);

	Remesher->SetExternalConstraints(MoveTemp(constraints));

	if (ProjectionTarget == nullptr)
	{
		check(ProjectionTargetSpatial == nullptr);
		ProjectionTarget = OriginalMesh.Get();
		ProjectionTargetSpatial = OriginalMeshSpatial.Get();
	}

	FMeshProjectionTarget ProjTarget(ProjectionTarget, ProjectionTargetSpatial);
	Remesher->SetProjectionTarget(&ProjTarget);

	Remesher->Progress = Progress;

	if (bDiscardAttributes && !bDiscardAttributesImmediately)
	{
		TargetMesh->DiscardAttributes();
	}

	if (RemeshType == ERemeshType::FullPass)
	{
		// Run a fixed number of iterations
		for (int k = 0; k < RemeshIterations; ++k)
		{
			// If we are not uniform smoothing, then flips seem to often make things worse.
			// Possibly this is because without the tangential flow, we won't get to the nice tris.
			// In this case we are better off basically not flipping, and just letting collapses resolve things
			// regular-valence polygons - things stay "stuck". 
			// @todo try implementing edge-length flip criteria instead of valence-flip
			if (bIsUniformSmooth == false)
			{
				bool bUseFlipsThisPass = (k % 2 == 0 && k < RemeshIterations / 2);
				Remesher->bEnableFlips = bUseFlipsThisPass && bFlips;
			}

			Remesher->BasicRemeshPass();
		}
	}
	else if (RemeshType == ERemeshType::Standard || RemeshType == ERemeshType::NormalFlow)
	{
		// Run to convergence
		Remesher->BasicRemeshPass();
	}
	else 
	{
		check(!"Encountered unexpected Remesh Type");
	}

	if (!TargetMesh->HasAttributes())
	{
		FMeshNormals::QuickComputeVertexNormals(*TargetMesh);
	}
	else
	{
		FMeshNormals::QuickRecomputeOverlayNormals(*TargetMesh);
	}

}
