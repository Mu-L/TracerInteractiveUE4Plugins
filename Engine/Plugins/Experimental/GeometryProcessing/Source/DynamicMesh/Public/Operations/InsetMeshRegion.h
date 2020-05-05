// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshRegionBoundaryLoops.h"


class FDynamicMesh3;
class FDynamicMeshChangeTracker;
class FMeshNormals;

/**
 * FInsetMeshRegion implements local inset of a mesh region.
 * The selected triangles are separated and then stitched back together, creating
 * an new strip of triangles around their border (s). The boundary loop vertices
 * are inset by creating an offset line for each boundary loop edge, and then
 * finding closest-points between the sequential edge pairs. 
 *
 * Complex input regions are handled, eg it can be multiple disconnected components, donut-shaped, etc
 *
 * Each quad of the border loop is assigned it's own normal and UVs (ie each is a separate UV-island)
 */
class DYNAMICMESH_API FInsetMeshRegion
{
public:

	//
	// Inputs
	//

	/** The mesh that we are modifying */
	FDynamicMesh3* Mesh;

	/** The triangle region we are modifying */
	TArray<int32> Triangles;

	/** Inset by this distance */
	double InsetDistance = 1.0;

	/** quads on the stitch loop are planar-projected and scaled by this amount */
	float UVScaleFactor = 1.0f;

	/** If set, change tracker will be updated based on edit */
	TUniquePtr<FDynamicMeshChangeTracker> ChangeTracker;

	//
	// Outputs
	//

	/**
	 * Inset information for a single connected component
	 */
	struct FInsetInfo
	{
		/** Set of triangles for this region */
		TArray<int32> InitialTriangles;
		/** Initial loops on the mesh */
		TArray<FEdgeLoop> BaseLoops;
		/** Inset loops on the mesh */
		TArray<FEdgeLoop> InsetLoops;

		/** Lists of triangle-strip "tubes" that connect each loop-pair */
		TArray<TArray<int>> StitchTriangles;
		/** List of group ids / polygon ids on each triangle-strip "tube" */
		TArray<TArray<int>> StitchPolygonIDs;
	};

	/**
	 * List of Inset regions/components
	 */
	TArray<FInsetInfo> InsetRegions;

	/**
	 * List of all triangles created/modified by this operation
	 */
	TArray<int32> AllModifiedTriangles;


public:
	FInsetMeshRegion(FDynamicMesh3* mesh);

	virtual ~FInsetMeshRegion() {}


	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		// @todo calculate MeshBoundaryLoops and make sure it is valid

		// is there any reason we couldn't do this??

		return EOperationValidationResult::Ok;
	}


	/**
	 * Apply the Inset operation to the input mesh.
	 * @return true if the algorithm succeeds
	 */
	virtual bool Apply();


protected:

	virtual bool ApplyInset(FInsetInfo& Region, FMeshNormals* UseNormals = nullptr);
};