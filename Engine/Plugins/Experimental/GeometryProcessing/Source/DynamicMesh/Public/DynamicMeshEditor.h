// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshEditor

#pragma once

#include "DynamicMesh3.h"
#include "EdgeLoop.h"




/**
 * FMeshIndexMappings stores a set of integer IndexMaps for a mesh
 * This is a convenient object to have, to avoid passing around large numbers of separate maps.
 * The individual maps are not necessarily all filled by every operation.
 */
struct DYNAMICMESH_API FMeshIndexMappings
{
protected:
	FIndexMapi VertexMap;
	FIndexMapi TriangleMap;
	FIndexMapi GroupMap;

	TArray<FIndexMapi> UVMaps;
	TArray<FIndexMapi> NormalMaps;

public:
	/** Size internal arrays-of-maps to be suitable for this Mesh */
	void Initialize(FDynamicMesh3* Mesh);

	/** @return the value used to indicate "invalid" in the mapping */
	int InvalidID() { return VertexMap.GetInvalidID(); }

	void Reset()
	{
		VertexMap.Reset();
		TriangleMap.Reset();
		GroupMap.Reset();
		for (FIndexMapi& UVMap : UVMaps)
		{
			UVMap.Reset();
		}
		for (FIndexMapi& NormalMap : NormalMaps)
		{
			NormalMap.Reset();
		}
	}

	FIndexMapi& GetVertexMap() { return VertexMap; }
	inline void SetVertex(int FromID, int ToID) { VertexMap.Add(FromID, ToID); }
	inline int GetNewVertex(int FromID) const { return VertexMap.GetTo(FromID); }
	inline bool ContainsVertex(int FromID) const { return VertexMap.ContainsFrom(FromID); }

	FIndexMapi& GetTriangleMap() { return TriangleMap; }
	void SetTriangle(int FromID, int ToID) { TriangleMap.Add(FromID, ToID); }
	int GetNewTriangle(int FromID) const { return TriangleMap.GetTo(FromID); }
	inline bool ContainsTriangle(int FromID) const { return TriangleMap.ContainsFrom(FromID); }

	FIndexMapi& GetGroupMap() { return GroupMap; }
	void SetGroup(int FromID, int ToID) { GroupMap.Add(FromID, ToID); }
	int GetNewGroup(int FromID) const { return GroupMap.GetTo(FromID); }
	inline bool ContainsGroup(int FromID) const { return GroupMap.ContainsFrom(FromID); }

	FIndexMapi& GetUVMap(int UVLayer) { return UVMaps[UVLayer]; }
	void SetUV(int UVLayer, int FromID, int ToID) { UVMaps[UVLayer].Add(FromID, ToID); }
	int GetNewUV(int UVLayer, int FromID) const { return UVMaps[UVLayer].GetTo(FromID); }
	inline bool ContainsUV(int UVLayer, int FromID) const { return UVMaps[UVLayer].ContainsFrom(FromID); }

	FIndexMapi& GetNormalMap(int NormalLayer) { return NormalMaps[NormalLayer]; }
	void SetNormal(int NormalLayer, int FromID, int ToID) { NormalMaps[NormalLayer].Add(FromID, ToID); }
	int GetNewNormal(int NormalLayer, int FromID) const { return NormalMaps[NormalLayer].GetTo(FromID); }
	inline bool ContainsNormal(int NormalLayer, int FromID) const { return NormalMaps[NormalLayer].ContainsFrom(FromID); }

};


/**
 * FDynamicMeshEditResult is used to return information about new mesh elements
 * created by mesh changes, primarily in FDynamicMeshEditor
 */
struct DYNAMICMESH_API FDynamicMeshEditResult
{
	/** New vertices created by an edit */
	TArray<int> NewVertices;

	/** New triangles created by an edit. Note that this list may be empty if the operation created quads or polygons */
	TArray<int> NewTriangles;
	/** New quads created by an edit, where each quad is a pair of triangle IDs */
	TArray<FIndex2i> NewQuads;
	/** New polygons created by an edit, where each polygon is a list of triangle IDs */
	TArray<TArray<int>> NewPolygons;

	/** New triangle groups created by an edit */
	TArray<int> NewGroups;

	/** clear this data structure */
	void Reset()
	{
		NewVertices.Reset();
		NewTriangles.Reset();
		NewQuads.Reset();
		NewPolygons.Reset();
		NewGroups.Reset();
	}

	/** Flatten the triangle/quad/polygon lists into a single list of all triangles */
	void GetAllTriangles(TArray<int>& TrianglesOut) const;
};




/**
 * FDynamicMeshEditor implements low-level mesh editing operations. These operations
 * can be used to construct higher-level operations. For example an Extrude operation
 * could be implemented via DuplicateTriangles() and StitchLoopMinimal().
 */
class DYNAMICMESH_API FDynamicMeshEditor
{
public:
	/** The mesh we will be editing */
	FDynamicMesh3* Mesh;

	FDynamicMeshEditor(FDynamicMesh3* MeshIn)
	{
		Mesh = MeshIn;
	}


	//////////////////////////////////////////////////////////////////////////
	// Create and Remove Triangle Functions
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Stitch together two loops of vertices with a quad-strip of triangles.
	 * Loops must be oriented (ordered) correctly for your use case.
	 * @param Loop1 first loop of sequential vertices
	 * @param Loop2 second loop of sequential vertices
	 * @param ResultOut lists of newly created triangles/vertices/etc
	 * @return true if operation suceeded. If a failure occurs, any added triangles are removed via RemoveTriangles
	 */
	bool StitchVertexLoopsMinimal(const TArray<int>& VertexLoop1, const TArray<int>& VertexLoop2, FDynamicMeshEditResult& ResultOut);


	/**
	 * Duplicate triangles of a mesh. This duplicates the current groups and also any attributes existing on the triangles.
	 * @param Triangles the triangles to duplicate
	 * @param IndexMaps returned mappings from old to new triangles/vertices/etc (you may initialize to optimize memory usage, etc)
	 * @param ResultOut lists of newly created triangles/vertices/etc
	 */
	void DuplicateTriangles(const TArray<int>& Triangles, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut);


	/**
	 * Pair of associated edge loops.
	 */
	struct FLoopPairSet
	{
		FEdgeLoop LoopA;
		FEdgeLoop LoopB;
	};

	/**
	 * Finds boundary loops of connected components of a set of triangles, and duplicates the vertices
	 * along the boundary, such that the triangles become disconnected.
	 * @param Triangles set of triangles
	 * @param LoopSetOut set of boundary loops. LoopA is original loop which remains with "outer" triangles, and LoopB is new boundary loop of triangle set
	 * @return true on success
	 */
	bool DisconnectTriangles(const TArray<int>& Triangles, TArray<FLoopPairSet>& LoopSetOut);


	/**
	 * Remove a list of triangles from the mesh, and optionally any vertices that are now orphaned
	 * @param Triangles the triangles to remove
	 * @param bRemoveIsolatedVerts if true, remove vertices that end up with no triangles
	 * @return true if all removes succeeded
	 */
	bool RemoveTriangles(const TArray<int>& Triangles, bool bRemoveIsolatedVerts);


	//////////////////////////////////////////////////////////////////////////
	// Normal utility functions
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Reverse the orientation of the given triangles, and optionally flip relevant normals
	 * @param Triangles the triangles to modify
	 * @param bInvertNormals if ture we call InvertTriangleNormals()
	 */
	void ReverseTriangleOrientations(const TArray<int>& Triangles, bool bInvertNormals);

	/**
	 * Flip the normals of the given triangles. This includes their vertex normals, if they
	 * exist, as well as any per-triangle attribute normals. @todo currently creates full-mesh bit arrays, could be more efficient on subsets
	 * @param Triangles the triangles to modify
	 */
	void InvertTriangleNormals(const TArray<int>& Triangles);


	/**
	 * Calculate and set the per-triangle normals of the two input quads.
	 * Average of the two face normals is used unless the quad is planar
	 * @param QuadTris pair of triangle IDs. If second ID is invalid, it is ignored
	 * @param bIsPlanar if the quad is known to be planar, operation is more efficient
	 * @return the normal vector that was set
	 */
	FVector3f ComputeAndSetQuadNormal(const FIndex2i& QuadTris, bool bIsPlanar = false);


	/**
	 * Create and set new shared per-triangle normals for a pair of triangles that share one edge (ie a quad)
	 * @param QuadTris pair of triangle IDs. If second ID is invalid, it is ignored
	 * @param Normal normal vector to set
	 */
	void SetQuadNormals(const FIndex2i& QuadTris, const FVector3f& Normal);

	/**
	 * Create and set new shared per-triangle normals for a list of triangles
	 * @param Triangles list of triangle IDs
	 * @param Normal normal vector to set
	 */
	void SetTriangleNormals(const TArray<int>& Triangles, const FVector3f& Normal);


	//////////////////////////////////////////////////////////////////////////
	// UV utility functions
	//////////////////////////////////////////////////////////////////////////


	/**
	 * Project the two triangles of the quad onto a plane defined by the normal and use that to create/set new shared per-triangle UVs.
	 * UVs are translated so that their bbox min-corner is at origin, and scaled by given scale factor
	 * @param QuadTris pair of triangle IDs. If second ID is invalid, it is ignored
	 * @param ProjectFrame vertices are projected into XY axes of this frame
	 * @param UVScaleFactor UVs are scaled
	 * @param UVLayerIndex which UV layer to operate on (must exist)
	 */
	void SetQuadUVsFromProjection(const FIndex2i& QuadTris, const FFrame3f& ProjectFrame, float UVScaleFactor = 1.0f, int UVLayerIndex = 0);



	//////////////////////////////////////////////////////////////////////////
	// mesh element copying / duplication
	//////////////////////////////////////////////////////////////////////////


	/**
	 * Find "new" vertex for input vertex under Index mapping, or create new if missing
	 * @param VertexID the source vertex we want a copy of
	 * @param IndexMaps source/destination mapping of already-duplicated vertices
	 * @param ResultOut newly-created vertices are stored here
	 * @return index of duplicate vertex
	 */	
	int FindOrCreateDuplicateVertex(int VertexID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut);

	/**
	 * Find "new" group for input group under Index mapping, or create new if missing
	 * @param TriangleID the source triangle whose group we want a copy of
	 * @param IndexMaps source/destination mapping of already-duplicated groups
	 * @param ResultOut newly-created groups are stored here
	 * @return index of duplicate group
	 */
	int FindOrCreateDuplicateGroup(int TriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut);

	/**
	 * Find "new" UV for input UV element under Index mapping, or create new if missing
	 * @param ElementID the source UV we want a duplicate of
	 * @param UVLayerIndex which UV layer to consider
	 * @param IndexMaps source/destination mapping of already-duplicated UVs
	 * @return index of duplicate UV in given UV layer
	 */
	int FindOrCreateDuplicateUV(int ElementID, int UVLayerIndex, FMeshIndexMappings& IndexMaps);

	/**
	 * Find "new" normal for input normal element under Index mapping, or create new if missing
	 * @param ElementID the source normal we want a duplicate of
	 * @param NormalLayerIndex which normal layer to consider
	 * @param IndexMaps source/destination mapping of already-duplicated normals
	 * @return index of duplicate normal in given normal layer
	 */
	int FindOrCreateDuplicateNormal(int ElementID, int NormalLayerIndex, FMeshIndexMappings& IndexMaps);


	/**
	 * Copy all attribute-layer values from one triangle to another, using the IndexMaps to track and re-use shared attribute values.
	 * @param FromTriangleID source triangle
	 * @param ToTriangleID destination triangle
	 * @param IndexMaps mappings passed to FindOrCreateDuplicateX functions to track already-created attributes
	 * @param ResultOut information about new attributes is stored here (@todo populate this, at time of writing there are no attribute fields)
	 */
	void CopyAttributes(int FromTriangleID, int ToTriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut);




	void AppendMesh(const FDynamicMesh3* AppendMesh, FMeshIndexMappings& IndexMapsOut, FDynamicMeshEditResult& ResultOut,
		TFunction<FVector3d(int, const FVector3d&)> PositionTransform = nullptr,
		TFunction<FVector3f(int, const FVector3f&)> NormalTransform = nullptr);

};


