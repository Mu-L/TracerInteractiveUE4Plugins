// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PreviewMesh.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicSubmesh3.h"
#include "EdgeLoop.h"
#include "PolyEditPreviewMesh.generated.h"

/**
 * UPolyEditPreviewMesh is a variant of UPreviewMesh intended for use as a 'live preview' of
 * a mesh creation/editing operation. The class supports initializing the preview mesh in various
 * ways, generally as a submesh of a base mesh.
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UPolyEditPreviewMesh : public UPreviewMesh
{
	GENERATED_BODY()
public:

	//
	// "Static Type" is just a static mesh
	//

	void InitializeStaticType(const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
		const FTransform3d* MeshTransform = nullptr);
	void UpdateStaticType(TFunctionRef<void(FDynamicMesh3&)> UpdateMeshFunc, bool bFullRecalculate);
	void MakeStaticTypeTargetMesh(FDynamicMesh3& TargetMesh);

	//
	// "Extrude Type" duplicates the input faces, offsets them using FExtrudeMesh, and stitches them together
	//

	void InitializeExtrudeType(const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
		const FVector3d& TransformedOffsetDirection,
		const FTransform3d* MeshTransform = nullptr,
		bool bDeleteExtrudeBaseFaces = true);
	void InitializeExtrudeType(FDynamicMesh3&& BaseMesh,
		const FVector3d& TransformedOffsetDirection,
		const FTransform3d* MeshTransform = nullptr,
		bool bDeleteExtrudeBaseFaces = true);
	/** Update extrude-type preview mesh by moving existing offset vertices */
	void UpdateExtrudeType(double NewOffset, bool bUseNormalDirection = false);
	/** Update extrude-type preview mesh using external function. if bFullRecalculate, mesh is re-initialized w/ initial extrusion patch SourceMesh before calling function */
	void UpdateExtrudeType(TFunctionRef<void(FDynamicMesh3&)> UpdateMeshFunc, bool bFullRecalculate);
	/** Make a hit-target mesh that is an infinite extrusion along extrude direction. If bUseNormalDirection, use per-vertex normals instead */
	void MakeExtrudeTypeHitTargetMesh(FDynamicMesh3& TargetMesh, bool bUseNormalDirection = false);


	//
	// "Inset Type" duplicates the input faces and insets them using FInsetMeshRegion
	//

	void InitializeInsetType(const FDynamicMesh3* SourceMesh, const TArray<int32>& Triangles,
		const FTransform3d* MeshTransform = nullptr);
	void UpdateInsetType(double NewOffset);
	void MakeInsetTypeTargetMesh(FDynamicMesh3& TargetMesh);

	const FDynamicMesh3& GetInitialPatchMesh() const { return InitialEditPatch; }

	// must be non-const because query functions are non-const
	FDynamicMeshAABBTree3& GetInitialPatchMeshSpatial() { return InitialEditPatchBVTree; }

protected:
	TUniquePtr<FDynamicSubmesh3> ActiveSubmesh;
	FDynamicMesh3 InitialEditPatch;
	FDynamicMeshAABBTree3 InitialEditPatchBVTree;

	TArray<int32> EditVertices;
	TArray<FVector3d> InitialPositions;
	TArray<FVector3d> InitialNormals;

	FVector3d InputDirection;

	FTransform3d MeshTransform;
	bool bHaveMeshTransform;
};