// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Spatial/MeshAABBTree3.h"
#include "Spatial/FastWinding.h"

#include "Generators/MarchingCubes.h"

/**
 * Use marching cubes to remesh a triangle mesh to a solid surface
 * Uses fast winding number to decide what is inside vs outside
 */
template<typename TriangleMeshType>
class TImplicitSolidify
{
public:

	TImplicitSolidify(TriangleMeshType* Source = nullptr, TMeshAABBTree3<TriangleMeshType>* SourceSpatial = nullptr, TFastWindingTree<TriangleMeshType>* SourceWinding = nullptr)
		: Source(Source), SourceSpatial(SourceSpatial), SourceWinding(SourceWinding)
	{
	}

	virtual ~TImplicitSolidify()
	{
	}

	///
	/// Inputs
	///

	TriangleMeshType* Source = nullptr;
	TMeshAABBTree3<TriangleMeshType>* SourceSpatial = nullptr;
	TFastWindingTree<TriangleMeshType>* SourceWinding = nullptr;

	/** Inside/outside winding number threshold */
	double WindingThreshold = .5;

	/** How much to extend bounds considered by marching cubes outside the original surface bounds */
	double ExtendBounds = 1;

	/** What to do if the surface extends outside the marching cubes bounds -- if true, puts a solid surface at the boundary */
	bool bSolidAtBoundaries = true;

	/** How many binary search steps to do when placing surface at boundary */
	int SurfaceSearchSteps = 4;

	/** size of the cells used when meshing the output (marching cubes' cube size) */
	double MeshCellSize = 1.0;

	/**
	 * Set cell size to hit the target voxel count along the max dimension of the bounds
	 */
	void SetCellSizeAndExtendBounds(FAxisAlignedBox3d Bounds, double ExtendBoundsIn, int TargetOutputVoxelCount)
	{
		ExtendBounds = ExtendBoundsIn;
		MeshCellSize = (Bounds.MaxDim() + ExtendBounds * 2.0) / double(TargetOutputVoxelCount);
	}

	/** if this function returns true, we should abort calculation */
	TFunction<bool(void)> CancelF = []()
	{
		return false;
	};
	
protected:

	FMarchingCubes MarchingCubes;

public:

	/**
	 * @return true if input parameters are valid
	 */
	bool Validate()
	{
		bool bValidMeshAndSpatial = Source != nullptr && SourceSpatial != nullptr && SourceSpatial->IsValid();
		bool bValidWinding = SourceWinding != nullptr;
		bool bValidParams = SurfaceSearchSteps >= 0 && MeshCellSize > 0;
		return bValidMeshAndSpatial && bValidWinding && bValidParams;
	}

	/**
	 * @param bReuseComputed If true, will attempt to reuse previously-computed AABB trees and SDFs where possible
	 */
	const FMeshShapeGenerator& Generate()
	{
		MarchingCubes.Reset();
		if (!ensure(Validate()))
		{
			// give up and return and empty result on invalid parameters
			return MarchingCubes;
		}

		FAxisAlignedBox3d InternalBounds = SourceSpatial->GetBoundingBox();
		InternalBounds.Expand(ExtendBounds);
		
		MarchingCubes.CubeSize = MeshCellSize;

		MarchingCubes.Bounds = InternalBounds;
		// expand marching cubes bounds beyond the 'internal' bounds to ensure we sample outside the bounds, if solid-at-boundaries is requested
		if (bSolidAtBoundaries)
		{
			MarchingCubes.Bounds.Expand(MeshCellSize * .1);
		}

		MarchingCubes.RootMode = ERootfindingModes::Bisection;
		MarchingCubes.RootModeSteps = SurfaceSearchSteps;
		MarchingCubes.IsoValue = WindingThreshold;
		MarchingCubes.CancelF = CancelF;

		if (bSolidAtBoundaries)
		{
			MarchingCubes.Implicit = [this, InternalBounds](const FVector3d& Pos)
			{
				return InternalBounds.Contains(Pos) ? SourceWinding->FastWindingNumber(Pos) : -(WindingThreshold + 1);
			};
		}
		else
		{
			MarchingCubes.Implicit = [this](const FVector3d& Pos)
			{
				return SourceWinding->FastWindingNumber(Pos);
			};
		}

		TArray<FVector3d> MCSeeds;
		for ( int32 VertIdx : Source->VertexIndicesItr() )
		{
			MCSeeds.Add(Source->GetVertex(VertIdx));
		}
		MarchingCubes.GenerateContinuation(MCSeeds);

		return MarchingCubes;
	}
};
