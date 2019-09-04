// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshUtilities.h"
#include "SkeletalMeshTools.h"
#include "Engine/SkeletalMesh.h"
#include "IAnimationBlueprintEditor.h"
#include "IAnimationBlueprintEditorModule.h"
#include "IAnimationEditor.h"
#include "IAnimationEditorModule.h"
#include "ISkeletalMeshEditor.h"
#include "ISkeletalMeshEditorModule.h"
#include "ISkeletonEditor.h"
#include "ISkeletonEditorModule.h"
#include "Engine/StaticMesh.h"

class FMeshUtilities : public IMeshUtilities
{
public:
	UE_DEPRECATED(4.17, "Use functionality in new MeshReduction Module")
	virtual IMeshReduction* GetStaticMeshReductionInterface() override;
	
	UE_DEPRECATED(4.17, "Use functionality in new MeshReduction Module")
	virtual IMeshReduction* GetSkeletalMeshReductionInterface() override;
	
	UE_DEPRECATED(4.17, "Use functionality in new MeshReduction Module")
	virtual IMeshMerging* GetMeshMergingInterface() override;
	
	UE_DEPRECATED(4.17, "Use functionality in new MeshMergeUtilities Module")
	virtual void MergeActors(
		const TArray<AActor*>& SourceActors,
		const FMeshMergingSettings& InSettings,
		UPackage* InOuter,
		const FString& InBasePackageName,
		TArray<UObject*>& OutAssetsToSync,
		FVector& OutMergedActorLocation,
		bool bSilent = false) const override;

	UE_DEPRECATED(4.17, "Use functionality in new MeshMergeUtilities Module")
	virtual void MergeStaticMeshComponents(
		const TArray<UStaticMeshComponent*>& ComponentsToMerge,
		UWorld* World,
		const FMeshMergingSettings& InSettings,
		UPackage* InOuter,
		const FString& InBasePackageName,
		TArray<UObject*>& OutAssetsToSync,
		FVector& OutMergedActorLocation,
		const float ScreenSize,
		bool bSilent = false) const override;

	UE_DEPRECATED(4.17, "Use functionality in new MeshMergeUtilities Module")
	virtual void CreateProxyMesh(const TArray<AActor*>& InActors, const struct FMeshProxySettings& InMeshProxySettings, UPackage* InOuter, const FString& InProxyBasePackageName, const FGuid InGuid, FCreateProxyDelegate InProxyCreatedDelegate, const bool bAllowAsync,
	const float ScreenAreaSize = 1.0f) override;

	UE_DEPRECATED(4.17, "Function is removed, use functionality in new MeshMergeUtilities Module")
	virtual void FlattenMaterialsWithMeshData(TArray<UMaterialInterface*>& InMaterials, TArray<FRawMeshExt>& InSourceMeshes, TMap<FMeshIdAndLOD, TArray<int32>>& InMaterialIndexMap, TArray<bool>& InMeshShouldBakeVertexData, const FMaterialProxySettings &InMaterialProxySettings, TArray<FFlattenMaterial> &OutFlattenedMaterials) const override;

private:
	/** Cached version string. */
	FString VersionString;
	/** True if NvTriStrip is being used for tri order optimization. */
	bool bUsingNvTriStrip;
	/** True if we disable triangle order optimization.  For debugging purposes only */
	bool bDisableTriangleOrderOptimization;

	// IMeshUtilities interface.
	virtual const FString& GetVersionString() const override
	{
		return VersionString;
	}

	virtual bool BuildStaticMesh(
		FStaticMeshRenderData& OutRenderData,
		UStaticMesh* StaticMesh,
		const FStaticMeshLODGroup& LODGroup
		) override;

	virtual void BuildStaticMeshVertexAndIndexBuffers(
		TArray<FStaticMeshBuildVertex>& OutVertices,
		TArray<TArray<uint32> >& OutPerSectionIndices,
		TArray<int32>& OutWedgeMap,
		const FRawMesh& RawMesh,
		const FOverlappingCorners& OverlappingCorners,
		const TMap<uint32, uint32>& MaterialToSectionMapping,
		float ComparisonThreshold,
		FVector BuildScale,
		int32 ImportVersion
		) override;

	virtual bool GenerateStaticMeshLODs(UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup) override;

	virtual void GenerateSignedDistanceFieldVolumeData(
		FString MeshName,
		const FStaticMeshLODResources& LODModel,
		class FQueuedThreadPool& ThreadPool,
		const TArray<EBlendMode>& MaterialBlendModes,
		const FBoxSphereBounds& Bounds,
		float DistanceFieldResolutionScale,
		bool bGenerateAsIfTwoSided,
		FDistanceFieldVolumeData& OutData) override;
	
	virtual void DownSampleDistanceFieldVolumeData(class FDistanceFieldVolumeData& DistanceFieldData, float Divider) override;

	virtual void RecomputeTangentsAndNormalsForRawMesh(bool bRecomputeTangents, bool bRecomputeNormals, const FMeshBuildSettings& InBuildSettings, FRawMesh &OutRawMesh) const override;
	virtual void RecomputeTangentsAndNormalsForRawMesh(bool bRecomputeTangents, bool bRecomputeNormals, const FMeshBuildSettings& InBuildSettings, const FOverlappingCorners& InOverlappingCorners, FRawMesh &OutRawMesh) const override;

	virtual bool GenerateUniqueUVsForStaticMesh(const FRawMesh& RawMesh, int32 TextureResolution, TArray<FVector2D>& OutTexCoords) const override;
	virtual bool GenerateUniqueUVsForStaticMesh(const FRawMesh& RawMesh, int32 TextureResolution, bool bMergeIdenticalMaterials, TArray<FVector2D>& OutTexCoords) const override;

	virtual bool BuildSkeletalMesh(FSkeletalMeshLODModel& LODModel, const FReferenceSkeleton& RefSkeleton, const TArray<SkeletalMeshImportData::FVertInfluence>& Influences, const TArray<SkeletalMeshImportData::FMeshWedge>& Wedges, const TArray<SkeletalMeshImportData::FMeshFace>& Faces, const TArray<FVector>& Points, const TArray<int32>& PointToOriginalMap, const MeshBuildOptions& BuildOptions = MeshBuildOptions(), TArray<FText> * OutWarningMessages = NULL, TArray<FName> * OutWarningNames = NULL) override;
	bool BuildSkeletalMesh_Legacy(FSkeletalMeshLODModel& LODModel, const FReferenceSkeleton& RefSkeleton, const TArray<SkeletalMeshImportData::FVertInfluence>& Influences, const TArray<SkeletalMeshImportData::FMeshWedge>& Wedges, const TArray<SkeletalMeshImportData::FMeshFace>& Faces, const TArray<FVector>& Points, const TArray<int32>& PointToOriginalMap, const FOverlappingThresholds& OverlappingThresholds, bool bComputeNormals = true, bool bComputeTangents = true, TArray<FText> * OutWarningMessages = NULL, TArray<FName> * OutWarningNames = NULL);

	virtual void CacheOptimizeIndexBuffer(TArray<uint16>& Indices) override;
	virtual void CacheOptimizeIndexBuffer(TArray<uint32>& Indices) override;
	void CacheOptimizeVertexAndIndexBuffer(TArray<FStaticMeshBuildVertex>& Vertices, TArray<TArray<uint32> >& PerSectionIndices, TArray<int32>& WedgeMap);

	virtual void BuildSkeletalAdjacencyIndexBuffer(
		const TArray<FSoftSkinVertex>& VertexBuffer,
		const uint32 TexCoordCount,
		const TArray<uint32>& Indices,
		TArray<uint32>& OutPnAenIndices
		) override;

	virtual void CalcBoneVertInfos(USkeletalMesh* SkeletalMesh, TArray<FBoneVertInfo>& Infos, bool bOnlyDominant) override;

	/** 
	 * Convert a set of mesh components in their current pose to a static mesh. 
	 * @param	InMeshComponents		The mesh components we want to convert
	 * @param	InRootTransform			The transform of the root of the mesh we want to output
	 * @param	InPackageName			The package name to create the static mesh in. If this is empty then a dialog will be displayed to pick the mesh.
	 * @return a new static mesh (specified by the user)
	 */
	virtual UStaticMesh* ConvertMeshesToStaticMesh(const TArray<UMeshComponent*>& InMeshComponents, const FTransform& InRootTransform = FTransform::Identity, const FString& InPackageName = FString()) override;

	/**
	* Builds a renderable skeletal mesh LOD model. Note that the array of chunks
	* will be destroyed during this process!
	* @param LODModel				Upon return contains a renderable skeletal mesh LOD model.
	* @param RefSkeleton			The reference skeleton associated with the model.
	* @param Chunks				Skinned mesh chunks from which to build the renderable model.
	* @param PointToOriginalMap	Maps a vertex's RawPointIdx to its index at import time.
	*/
	void BuildSkeletalModelFromChunks(FSkeletalMeshLODModel& LODModel, const FReferenceSkeleton& RefSkeleton, TArray<FSkinnedMeshChunk*>& Chunks, const TArray<int32>& PointToOriginalMap);

	virtual void FindOverlappingCorners(FOverlappingCorners& OutOverlappingCorners, const TArray<FVector>& InVertices, const TArray<uint32>& InIndices, float ComparisonThreshold) const override;

	void FindOverlappingCorners(FOverlappingCorners& OutOverlappingCorners, FRawMesh const& RawMesh, float ComparisonThreshold) const;
	// IModuleInterface interface.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void ExtractMeshDataForGeometryCache(FRawMesh& RawMesh, const FMeshBuildSettings& BuildSettings, TArray<FStaticMeshBuildVertex>& OutVertices, TArray<TArray<uint32> >& OutPerSectionIndices, int32 ImportVersion);

	virtual void CalculateTextureCoordinateBoundsForSkeletalMesh(const FSkeletalMeshLODModel& LODModel, TArray<FBox2D>& OutBounds) const override;

	virtual bool GenerateUniqueUVsForSkeletalMesh(const FSkeletalMeshLODModel& LODModel, int32 TextureResolution, TArray<FVector2D>& OutTexCoords) const override;

	virtual bool RemoveBonesFromMesh(USkeletalMesh* SkeletalMesh, int32 LODIndex, const TArray<FName>* BoneNamesToRemove) const override;

	virtual void CalculateTangents(const TArray<FVector>& InVertices, const TArray<uint32>& InIndices, const TArray<FVector2D>& InUVs, const TArray<uint32>& InSmoothingGroupIndices, const uint32 InTangentOptions, TArray<FVector>& OutTangentX, TArray<FVector>& OutTangentY, TArray<FVector>& OutNormals) const override;
	virtual void CalculateNormals(const TArray<FVector>& InVertices, const TArray<uint32>& InIndices, const TArray<FVector2D>& InUVs, const TArray<uint32>& InSmoothingGroupIndices, const uint32 InTangentOptions, TArray<FVector>& OutNormals) const override;
	virtual void CalculateOverlappingCorners(const TArray<FVector>& InVertices, const TArray<uint32>& InIndices, bool bIgnoreDegenerateTriangles, FOverlappingCorners& OutOverlappingCorners) const override;

	virtual void GenerateRuntimeSkinWeightData(const FSkeletalMeshLODModel* ImportedModel, const TArray<FRawSkinWeight>& InRawSkinWeights, FRuntimeSkinWeightProfileData& InOutSkinWeightOverrideData) const override;

	// Need to call some members from this class, (which is internal to this module)
	friend class FStaticMeshUtilityBuilder;

protected:
	void AddAnimationBlueprintEditorToolbarExtender();

	void RemoveAnimationBlueprintEditorToolbarExtender();

	TSharedRef<FExtender> GetAnimationBlueprintEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<IAnimationBlueprintEditor> InAnimationBlueprintEditor);

	void AddAnimationEditorToolbarExtender();

	void RemoveAnimationEditorToolbarExtender();

	TSharedRef<FExtender> GetAnimationEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<IAnimationEditor> InAnimationEditor);

	void AddSkeletalMeshEditorToolbarExtender();

	void RemoveSkeletalMeshEditorToolbarExtender();

	TSharedRef<FExtender> GetSkeletalMeshEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<ISkeletalMeshEditor> InSkeletalMeshEditor);

	void AddSkeletonEditorToolbarExtender();

	void RemoveSkeletonEditorToolbarExtender();

	TSharedRef<FExtender> GetSkeletonEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<ISkeletonEditor> InSkeletonEditor);

	void HandleAddSkeletalMeshActionExtenderToToolbar(FToolBarBuilder& ParentToolbarBuilder, UMeshComponent* MeshComponent);

	void AddLevelViewportMenuExtender();

	void RemoveLevelViewportMenuExtender();

	TSharedRef<FExtender> GetLevelViewportContextMenuExtender(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> InActors);

	void ConvertActorMeshesToStaticMesh(const TArray<AActor*> InActors);

	FDelegateHandle ModuleLoadedDelegateHandle;
	FDelegateHandle LevelViewportExtenderHandle;
	FDelegateHandle AnimationBlueprintEditorExtenderHandle;
	FDelegateHandle AnimationEditorExtenderHandle;
	FDelegateHandle SkeletalMeshEditorExtenderHandle;
	FDelegateHandle SkeletonEditorExtenderHandle;
};

DECLARE_LOG_CATEGORY_EXTERN(LogMeshUtilities, Verbose, All);