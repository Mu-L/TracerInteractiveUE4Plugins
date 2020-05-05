// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshConversionOptions.h"

#include "MeshTangents.h"

#include "SimpleDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;

/** internal FPrimitiveSceneProxy defined in SimpleDynamicMeshSceneProxy.h */
class FSimpleDynamicMeshSceneProxy;



/** 
 * USimpleDynamicMeshComponent is a mesh component similar to UProceduralMeshComponent,
 * except it bases the renderable geometry off an internal FDynamicMesh3 instance.
 * 
 * There is some support for undo/redo on the component (@todo is this the right place?)
 * 
 * This component draws wireframe-on-shaded when Wireframe is enabled, or when bExplicitShowWireframe = true
 *
 */
UCLASS(hidecategories = (LOD, Physics, Collision), editinlinenew, ClassGroup = Rendering)
class MODELINGCOMPONENTS_API USimpleDynamicMeshComponent : public UBaseDynamicMeshComponent
{
	GENERATED_UCLASS_BODY()


public:
	/** How should Tangents be calculated/handled */
	UPROPERTY()
	EDynamicMeshTangentCalcType TangentsType = EDynamicMeshTangentCalcType::NoTangents;

public:
	/**
	 * initialize the internal mesh from a MeshDescription
	 */
	void InitializeMesh(FMeshDescription* MeshDescription);

	/**
	 * @return pointer to internal mesh
	 */
	FDynamicMesh3* GetMesh() { return Mesh.Get(); }

	/**
	 * @return pointer to internal mesh
	 */
	const FDynamicMesh3* GetMesh() const { return Mesh.Get(); }

	/**
	 * @return the current internal mesh, which is replaced with an empty mesh
	 */
	TUniquePtr<FDynamicMesh3> ExtractMesh(bool bNotifyUpdate);

	/**
	 * @return pointer to internal tangents object. 
	 * @warning calling this with TangentsType = AutoCalculated will result in possibly-expensive Tangents calculation
	 */
	FMeshTangentsf* GetTangents();




	/**
	 * Write the internal mesh to a MeshDescription
	 * @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	 * @param ConversionOptions struct of additional options for the conversion
	 */
	void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions);

	/**
	* Write the internal mesh to a MeshDescription with default conversion options
	* @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	*/
	void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology)
	{
		FConversionToMeshDescriptionOptions ConversionOptions;
		Bake(MeshDescription, bHaveModifiedTopology, ConversionOptions);
	}


	//
	// change tracking/etc
	//

	/**
	 * Call this if you update the mesh via GetMesh(). This will destroy the existing RenderProxy and create a new one.
	 * @todo should provide a function that calls a lambda to modify the mesh, and only return const mesh pointer
	 */
	virtual void NotifyMeshUpdated() override;

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex colors (or triangle color function).
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyColorsUpdated();

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex positions.
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyPositionsUpdated(bool bNormals = false, bool bColors = false, bool bUVs = false);

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex uvs.
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyUVsUpdated();

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated secondary triangle sorting.
	 * This function will update the existing buffers if possible, without rebuilding entire RenderProxy.
	 */
	void FastNotifySecondaryTrianglesChanged();


	/**
	 * Apply a vertex deformation change to the internal mesh
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override;

	/**
	 * Apply a general mesh change to the internal mesh
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override;

	/**
	* Apply a general mesh replacement change to the internal mesh
	*/
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) override;


	/**
	 * This delegate fires when a FCommandChange is applied to this component, so that
	 * parent objects know the mesh has changed.
	 */
	FSimpleMulticastDelegate OnMeshChanged;


	/**
	 * if true, we always show the wireframe on top of the shaded mesh, even when not in wireframe mode
	 */
	UPROPERTY()
	bool bExplicitShowWireframe = false;

	/**
	 * @return true if wireframe rendering pass is enabled
	 */
	virtual bool EnableWireframeRenderPass() const override { return bExplicitShowWireframe; }


	/**
	 * If this function is set, we will use these colors instead of vertex colors
	 */
	TFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc = nullptr;


	/**
	 * If Secondary triangle buffers are enabled, then we will filter triangles that pass the given predicate
	 * function into a second index buffer. These triangles will be drawn with the Secondary render material
	 * that is set in the BaseDynamicMeshComponent. Calling this function invalidates the SceneProxy.
	 */
	virtual void EnableSecondaryTriangleBuffers(TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc);

	/**
	 * Disable secondary triangle buffers. This invalidates the SceneProxy.
	 */
	virtual void DisableSecondaryTriangleBuffers();


public:

	// do not use this
	UPROPERTY()
	bool bDrawOnTop = false;

	// do not use this
	void SetDrawOnTop(bool bSet);


protected:
	/**
	 * This is called to tell our RenderProxy about modifications to the material set.
	 * We need to pass this on for things like material validation in the Editor.
	 */
	virtual void NotifyMaterialSetUpdated();

private:

	FSimpleDynamicMeshSceneProxy* GetCurrentSceneProxy() { return (FSimpleDynamicMeshSceneProxy*)SceneProxy; }

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	TUniquePtr<FDynamicMesh3> Mesh;
	void InitializeNewMesh();

	// local-space bounding of Mesh
	FAxisAlignedBox3d LocalBounds;

	bool bTangentsValid = false;
	FMeshTangentsf Tangents;
	
	FColor GetTriangleColor(const FDynamicMesh3* Mesh, int TriangleID);

	TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc = nullptr;


	//friend class FCustomMeshSceneProxy;
};
