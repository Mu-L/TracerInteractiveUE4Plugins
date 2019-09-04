// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/MemStack.h"
#include "LODCluster.h"
#include "Engine/EngineTypes.h"

#include "Engine/DeveloperSettings.h"
#include "HierarchicalLOD.generated.h"

class AHierarchicalLODVolume;
class ALODActor;

/*=============================================================================
	HierarchicalLOD.h: Hierarchical LOD definition.
=============================================================================*/

class AActor;
class AHierarchicalLODVolume;
class ALODActor;
class UHierarchicalLODSetup;
class ULevel;
class UWorld;

UCLASS(config = Engine, meta = (DisplayName = "Hierarchical LOD"), defaultconfig)
class UNREALED_API UHierarchicalLODSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:
	/** If enabled will force the project set HLOD level settings to be used across all levels in the project when Building Clusters */
	UPROPERTY(EditAnywhere, config, Category = HLODSystem)
	bool bForceSettingsInAllMaps;

	/** When set in combination with */
	UPROPERTY(EditAnywhere, config, Category = HLODSystem, meta=(editcondition="bForceSettingsInAllMaps"))
	TSoftClassPtr<UHierarchicalLODSetup> DefaultSetup;
	
	UPROPERTY(config, EditAnywhere, Category = HLODSystem, AdvancedDisplay, meta = (DisplayName = "Directories containing maps used for building HLOD data through the commandlet", RelativeToGameContentDir))
	TArray<FDirectoryPath> DirectoriesForHLODCommandlet;

	UPROPERTY(config, EditAnywhere, Category = HLODSystem, AdvancedDisplay, meta = (DisplayName = "Map UAssets used for building HLOD data through the ", RelativeToGameContentDir, LongPackageName))
	TArray<FFilePath> MapsToBuild;

	UPROPERTY(EditAnywhere, config, Category = HLODSystem, meta = (DisplayName = "Invalidate HLOD Clusters on changes to the Sub Actors"))
	bool bInvalidateHLODClusters;

	UPROPERTY(EditAnywhere, config, Category = HLODSystem, meta = (DisplayName = "Delete (out-dated) HLOD Assets on Save", editcondition = "bInvalidateHLODClusters"))
	bool bDeleteHLODAssets;
	
	/** Base material used for creating a Constant Material Instance as the Proxy Material */
	UPROPERTY(EditAnywhere, config, Category = HLODSystem)
	TSoftObjectPtr<class UMaterialInterface> BaseMaterial;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

/**
 *
 *	This is Hierarchical LOD builder
 *
 * This builds list of clusters and make sure it's sorted in the order of lower cost to high and merge clusters
 **/
struct UNREALED_API FHierarchicalLODBuilder
{
	FHierarchicalLODBuilder(UWorld* InWorld);

	/** DO NOT USE. This constructor is for internal usage only for hot-reload purposes. */
	FHierarchicalLODBuilder();

	/**
	* Build, Builds the clusters and spawn LODActors with their merged Static Meshes
	*/
	void Build();
	
	/**
	* PreviewBuild, Builds the clusters and spawns LODActors but without actually creating/merging new StaticMeshes
	*/
	void PreviewBuild();

	/**
	* Clear all the HLODs and the ALODActors that were created for them
	*/
	void ClearHLODs();

	/**
	* Clear only the ALODActorsPreview 
	*/
	void ClearPreviewBuild();

	/** Builds the LOD meshes for all LODActors inside of the World's Levels */
	void BuildMeshesForLODActors(bool bForceAll);

	/** Saves HLOD meshes for actors in all the World's levels */
	void SaveMeshesForActors();

	/** Get the list of mesh packages to save for a given level */
	void GetMeshesPackagesToSave(ULevel* InLevel, TSet<UPackage*>& InHLODPackagesToSave, const FString& PreviousLevelName = "");

	/** 
	 * @param	bInForce	Whether to force the recalculation of this actor's build flag. If this is false then the cached flag is used an only recalculated every so often.
	 * @return whether a build is needed (i.e. any LOD actors are dirty) 
	 */
	bool NeedsBuild(bool bInForce = false) const;

	/**
	* Build a single LOD Actor's mesh
	*
	* @param LODActor - LODActor to build mesh for
	* @param LODLevel - LODLevel to build the mesh for
	*/
	void BuildMeshForLODActor(ALODActor* LODActor, const uint32 LODLevel);

private:
	/**
	* Builds the clusters (HLODs) for InLevel, and will create the new/merged StaticMeshes if bCreateMeshes is true
	*
	* @param InLevel - Level for which the HLODs are currently being build
	* @param bCreateMeshes - Whether or not to create/merge the StaticMeshes (false if builing preview only)
	*/
	void BuildClusters(ULevel* InLevel, const bool bCreateMeshes);

	/** Generates a single cluster for the ULevel (across all HLOD levels) */
	void GenerateAsSingleCluster(const int32 NumHLODLevels, ULevel* InLevel, const bool bCreateMeshes);

	/**
	* Initializes the clusters, creating one for each actor within the level eligble for HLOD-ing
	*
	* @param InLevel - Level for which the HLODs are currently being build
	* @param LODIdx - LOD index we are building
	* @param CullCost - Test variable for tweaking HighestCost
	*/
	void InitializeClusters(ULevel* InLevel, const int32 LODIdx, float CullCost, bool const bPreviewBuild, bool const bVolumesOnly);

	/**
	* Merges clusters and builds actors for resulting (valid) clusters
	*
	* @param InLevel - Level for which the HLODs are currently being build
	* @param LODIdx - LOD index we are building, used for determining which StaticMesh LOD to use
	* @param HighestCost - Allowed HighestCost for this LOD level
	* @param MinNumActors - Minimum number of actors for this LOD level
	* @param bCreateMeshes - Whether or not to create/merge the StaticMeshes (false if builing preview only)
	*/
	void MergeClustersAndBuildActors(ULevel* InLevel, const int32 LODIdx, float HighestCost, int32 MinNumActors, const bool bCreateMeshes);

	/**
	* Finds the minimal spanning tree MST for the clusters by sorting on their cost ( Lower == better )
	*/
	void FindMST();

	/* Retrieves HierarchicalLODVolumes and creates a cluster for each individual one
	*
	* @param InLevel - Level for which the HLODs are currently being build
	*/
	void HandleHLODVolumes(ULevel* InLevel);

	/**
	* Determine whether or not this actor is eligble for HLOD creation
	*
	* @param Actor - Actor to test
	* @return bool
	*/
	bool ShouldGenerateCluster(AActor* Actor, const bool bPreviewBuild, const int32 HLODLevelIndex);
	
	/**
	* Deletes LOD actors from the world	
	*
	* @param InLevel - Level to delete the actors from
	* @param bPreviewOnly - Only delete preview actors
	* @return void
	*/
	void DeleteLODActors(ULevel* InLevel);

	/** Array of LOD Clusters - this is only valid within scope since mem stack allocator */
	TArray<FLODCluster, TMemStackAllocator<>> Clusters;

	/** Owning world HLODs are created for */
	UWorld*	World;

	/** Array of LOD clusters created for the HierachicalLODVolumes found within the level */
	TMap<AHierarchicalLODVolume*, FLODCluster> HLODVolumeClusters;	
	TMap<ALODActor*, AHierarchicalLODVolume*> HLODVolumeActors;

	const UHierarchicalLODSettings* HLODSettings;

	/** LOD Actors per HLOD level */
	TArray<TArray<ALODActor*>> LODLevelLODActors;

	/** Valid Static Mesh actors in level (populated during initialize clusters) */
	TArray<AActor*> ValidStaticMeshActorsInLevel;
	/** Actors which were rejected from the previous HLOD level(s) */
	TArray<AActor*> RejectedActorsInLevel;
};
