// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TransformTypes.h"

class UMaterialInterface;
class UInteractiveToolManager;
class IToolsContextAssetAPI;
struct FMeshDescription;
class FDynamicMesh3;
class AActor;

/**
 * Utility functions for generating Assets in InteractiveTool implementations
 */
namespace AssetGenerationUtil
{
	/**
	 * Creates a new StaticMesh actor/component, with a new mesh asset stored at the given PackagePath created via the AssetAPI
	 * @param AssetAPI pointer to context asset API that will be used to create new asset
	 * @param TargetWorld world that Actor will be created in
	 * @param Mesh geometry for the mesh
	 * @param Transform transformation for the new actor
	 * @param ObjectName name of the new asset
	 * @param PackagePath path of the new asset
	 * @param Material optional single material to set on actor
	 * @return new mesh actor
	 */
	MODELINGCOMPONENTS_API AActor* GenerateStaticMeshActor(
		IToolsContextAssetAPI* AssetAPI,
		UWorld* TargetWorld,
		const FDynamicMesh3* Mesh,
		const FTransform3d& Transform,
		FString ObjectName,
		UMaterialInterface* Material = nullptr
	);

	/**
	* Creates a new StaticMesh actor/component, with a new mesh asset stored at the given PackagePath created via the AssetAPI
	* @param AssetAPI pointer to context asset API that will be used to create new asset
	* @param TargetWorld world that Actor will be created in
	* @param Mesh geometry for the mesh
	* @param Transform transformation for the new actor
	* @param ObjectName name of the new asset
	* @param PackagePath path of the new asset
	* @param Materials materials to set on generated actor
	* @return new mesh actor
	*/
	MODELINGCOMPONENTS_API AActor* GenerateStaticMeshActor(
		IToolsContextAssetAPI* AssetAPI,
		UWorld* TargetWorld,
		const FDynamicMesh3* Mesh,
		const FTransform3d& Transform,
		FString ObjectName,
		const TArrayView<UMaterialInterface*>& Materials
	);

}
