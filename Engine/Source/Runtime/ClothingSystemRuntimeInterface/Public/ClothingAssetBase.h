// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "ClothingAssetBase.generated.h"

class USkeletalMesh;

// An interface object for any clothing asset the engine can use. Any
// clothing asset concrete object should derive from this.
UCLASS(Abstract)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothingAssetBase : public UObject
{
	GENERATED_BODY()

public:

	/** Binds a clothing asset submesh to a skeletal mesh section
	* @param InSkelMesh Skel mesh to bind to
	* @param InSectionIndex Section in the skel mesh to replace
	* @param InSubmeshIdx Submesh in this asset to replace section with
	* @param InAssetLodIndex Internal clothing LOD to use
	*/
	virtual bool BindToSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex, const int32 InSectionIndex, const int32 InAssetLodIndex)
	PURE_VIRTUAL(UClothingAssetBase::BindToSkeletalMesh, return false;);

	/**
	* Unbinds this clothing asset from the provided skeletal mesh, will remove all LODs
	* @param InSkelMesh skeletal mesh to remove from
	*/
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh)
	PURE_VIRTUAL(UClothingAssetBase::UnbindFromSkeletalMesh, );

	/**
	* Unbinds this clothing asset from the provided skeletal mesh
	* @param InSkelMesh skeletal mesh to remove from
	* @param InMeshLodIndex Mesh LOD to remove this asset from (could still be bound to other LODs)
	*/
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex)
	PURE_VIRTUAL(UClothingAssetBase::UnbindFromSkeletalMesh,);

	/** 
	 * Messages to the clothing asset that the bones in the parent mesh have
	 * possibly changed, which could invalidate the bone indices stored in the LOD
	 * data.
	 * @param InSkelMesh - The mesh to use to remap the bones
	 */
	virtual void RefreshBoneMapping(USkeletalMesh* InSkelMesh)
	PURE_VIRTUAL(UClothingAssetBase::RefreshBoneMapping, );

	/**
	 * Called on the clothing asset when the base data (physical mesh etc.) has
	 * changed, so any intermediate generated data can be regenerated.
	 */
	virtual void InvalidateCachedData()
	PURE_VIRTUAL(UClothingAssetBase::InvalidateCachedData(), );

	/** Check the validity of a LOD index */
	virtual bool IsValidLod(int32 InLodIndex) const
	PURE_VIRTUAL(UClothingAssetBase::IsValidLod(), return false;);

	/** Get the number of LODs defined in the clothing asset */
	virtual int32 GetNumLods() const
	PURE_VIRTUAL(UClothingAssetBase::GetNumLods(), return 0;);

	/** Add a new LOD class instance. */
	virtual int32 AddNewLod()
	PURE_VIRTUAL(UClothingAssetBase::AddNewLod(), return INDEX_NONE;);

	/** Builds self collision data */
	virtual void BuildSelfCollisionData()
	PURE_VIRTUAL(UClothingAssetBase::BuildSelfCollisionData(), );

	/**
	*	Builds the LOD transition data
	*	When we transition between LODs we skin the incoming mesh to the outgoing mesh
	*	in exactly the same way the render mesh is skinned to create a smooth swap
	*/
	virtual void BuildLodTransitionData()
	PURE_VIRTUAL(UClothingAssetBase::BuildLodTransitionData(), );

	/** Called after all cloth assets sharing the same simulation are added or loaded */
	virtual void PostUpdateAllAssets()
	PURE_VIRTUAL(UClothingAssetBase::PostUpdateAllAssets(), );

	/** Get the guid identifying this asset */
	const FGuid& GetAssetGuid() const
	{
		return AssetGuid;
	}

	// If this asset was imported from a file, this will be the original path
	UPROPERTY(VisibleAnywhere, Category = Import)
	FString ImportedFilePath;

protected:

	/** The asset factory should have access, as it will assign the asset guid when building assets */
	friend class UClothingAssetFactory;

	/** Guid to identify this asset. Will be embedded into chunks that are created using this asset */
	UPROPERTY()
	FGuid AssetGuid;
};
