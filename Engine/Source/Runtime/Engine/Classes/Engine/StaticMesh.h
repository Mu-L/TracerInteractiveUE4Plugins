// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "UObject/ScriptMacros.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "RenderCommandFence.h"
#include "Templates/ScopedPointer.h"
#include "Components.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Engine/MeshMerging.h"
#include "Engine/StreamableRenderAsset.h"
#include "Templates/UniquePtr.h"
#include "StaticMeshResources.h"
#include "PerPlatformProperties.h"

#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshAttributeArray.h"
#include "RenderAssetUpdate.h"

#include "StaticMesh.generated.h"

class FSpeedTreeWind;
class UAssetUserData;
class UMaterialInterface;
class UNavCollisionBase;
class FStaticMeshUpdate;
struct FMeshDescriptionBulkData;
struct FStaticMeshLODResources;


/*-----------------------------------------------------------------------------
	Legacy mesh optimization settings.
-----------------------------------------------------------------------------*/

/** Optimization settings used to simplify mesh LODs. */
UENUM()
enum ENormalMode
{
	NM_PreserveSmoothingGroups,
	NM_RecalculateNormals,
	NM_RecalculateNormalsSmooth,
	NM_RecalculateNormalsHard,
	TEMP_BROKEN,
	ENormalMode_MAX,
};

UENUM()
enum EImportanceLevel
{
	IL_Off,
	IL_Lowest,
	IL_Low,
	IL_Normal,
	IL_High,
	IL_Highest,
	TEMP_BROKEN2,
	EImportanceLevel_MAX,
};

/** Enum specifying the reduction type to use when simplifying static meshes. */
UENUM()
enum EOptimizationType
{
	OT_NumOfTriangles,
	OT_MaxDeviation,
	OT_MAX,
};

/** Old optimization settings. */
USTRUCT()
struct FStaticMeshOptimizationSettings
{
	GENERATED_USTRUCT_BODY()

	/** The method to use when optimizing the skeletal mesh LOD */
	UPROPERTY()
	TEnumAsByte<enum EOptimizationType> ReductionMethod;

	/** If ReductionMethod equals SMOT_NumOfTriangles this value is the ratio of triangles [0-1] to remove from the mesh */
	UPROPERTY()
	float NumOfTrianglesPercentage;

	/**If ReductionMethod equals SMOT_MaxDeviation this value is the maximum deviation from the base mesh as a percentage of the bounding sphere. */
	UPROPERTY()
	float MaxDeviationPercentage;

	/** The welding threshold distance. Vertices under this distance will be welded. */
	UPROPERTY()
	float WeldingThreshold;

	/** Whether Normal smoothing groups should be preserved. If false then NormalsThreshold is used **/
	UPROPERTY()
	bool bRecalcNormals;

	/** If the angle between two triangles are above this value, the normals will not be
	smooth over the edge between those two triangles. Set in degrees. This is only used when PreserveNormals is set to false*/
	UPROPERTY()
	float NormalsThreshold;

	/** How important the shape of the geometry is (EImportanceLevel). */
	UPROPERTY()
	uint8 SilhouetteImportance;

	/** How important texture density is (EImportanceLevel). */
	UPROPERTY()
	uint8 TextureImportance;

	/** How important shading quality is. */
	UPROPERTY()
	uint8 ShadingImportance;


	FStaticMeshOptimizationSettings()
	: ReductionMethod( OT_MaxDeviation )
	, NumOfTrianglesPercentage( 1.0f )
	, MaxDeviationPercentage( 0.0f )
	, WeldingThreshold( 0.1f )
	, bRecalcNormals( true )
	, NormalsThreshold( 60.0f )
	, SilhouetteImportance( IL_Normal )
	, TextureImportance( IL_Normal )
	, ShadingImportance( IL_Normal )
	{
	}

	/** Serialization for FStaticMeshOptimizationSettings. */
	inline friend FArchive& operator<<( FArchive& Ar, FStaticMeshOptimizationSettings& Settings )
	{
		Ar << Settings.ReductionMethod;
		Ar << Settings.MaxDeviationPercentage;
		Ar << Settings.NumOfTrianglesPercentage;
		Ar << Settings.SilhouetteImportance;
		Ar << Settings.TextureImportance;
		Ar << Settings.ShadingImportance;
		Ar << Settings.bRecalcNormals;
		Ar << Settings.NormalsThreshold;
		Ar << Settings.WeldingThreshold;

		return Ar;
	}

};

/*-----------------------------------------------------------------------------
	UStaticMesh
-----------------------------------------------------------------------------*/

/**
 * Source model from which a renderable static mesh is built.
 */
USTRUCT()
struct FStaticMeshSourceModel
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITOR
	/**
	 * Imported raw mesh data. Optional for all but the first LOD.
	 *
	 * This is a member for legacy assets only.
	 * If it is non-empty, this means that it has been de-serialized from the asset, and
	 * the asset hence pre-dates MeshDescription.
	 */
	class FRawMeshBulkData* RawMeshBulkData;
	
	/*
	 * The staticmesh owner of this source model. We need the SM to be able to convert between MeshDesription and RawMesh.
	 * RawMesh use int32 material index and MeshDescription use FName material slot name.
	 * This memeber is fill in the PostLoad of the static mesh.
	 * TODO: Remove this member when FRawMesh will be remove.
	 */
	class UStaticMesh* StaticMeshOwner;
	/*
	 * Accessor to Load and save the raw mesh or the mesh description depending on the editor settings.
	 * Temporary until we deprecate the RawMesh.
	 */
	ENGINE_API bool IsRawMeshEmpty() const;
	ENGINE_API void LoadRawMesh(struct FRawMesh& OutRawMesh) const;
	ENGINE_API void SaveRawMesh(struct FRawMesh& InRawMesh, bool bConvertToMeshdescription = true);

#endif // #if WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/**
	 * Mesh description unpacked from bulk data.
	 *
	 * If this is valid, this means the mesh description has either been unpacked from the bulk data stored in the asset,
	 * or one has been generated by the build tools (or converted from legacy RawMesh).
	 */
	TUniquePtr<FMeshDescription> MeshDescription;

	/**
	 * Bulk data containing mesh description. LOD0 must be valid, but autogenerated lower LODs may be invalid.
	 *
	 * New assets store their source data here instead of in the RawMeshBulkData.
	 * If this is invalid, either the LOD is autogenerated (for LOD1+), or the asset is a legacy asset whose
	 * data is in the RawMeshBulkData.
	 */
	TUniquePtr<FMeshDescriptionBulkData> MeshDescriptionBulkData;
#endif

	/** Settings applied when building the mesh. */
	UPROPERTY(EditAnywhere, Category=BuildSettings)
	FMeshBuildSettings BuildSettings;

	/** Reduction settings to apply when building render data. */
	UPROPERTY(EditAnywhere, Category=ReductionSettings)
	FMeshReductionSettings ReductionSettings; 

	UPROPERTY()
	float LODDistance_DEPRECATED;

	/** 
	 * ScreenSize to display this LOD.
	 * The screen size is based around the projected diameter of the bounding
	 * sphere of the model. i.e. 0.5 means half the screen's maximum dimension.
	 */
	UPROPERTY(EditAnywhere, Category=ReductionSettings)
	FPerPlatformFloat ScreenSize;

	/** The file path that was used to import this LOD. */
	UPROPERTY(VisibleAnywhere, Category = StaticMeshSourceModel, AdvancedDisplay)
	FString SourceImportFilename;

#if WITH_EDITORONLY_DATA
	/** Weather this LOD was imported in the same file as the base mesh. */
	UPROPERTY()
	bool bImportWithBaseMesh;
#endif

	/** Default constructor. */
	ENGINE_API FStaticMeshSourceModel();

	/** Destructor. */
	ENGINE_API ~FStaticMeshSourceModel();

#if WITH_EDITOR
	/** Serializes bulk data. */
	void SerializeBulkData(FArchive& Ar, UObject* Owner);
#endif
};

// Make FStaticMeshSourceModel non-assignable
template<> struct TStructOpsTypeTraits<FStaticMeshSourceModel> : public TStructOpsTypeTraitsBase2<FStaticMeshSourceModel> { enum { WithCopy = false }; };


/**
 * Per-section settings.
 */
USTRUCT()
struct FMeshSectionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Index in to the Materials array on UStaticMesh. */
	UPROPERTY()
	int32 MaterialIndex;

	/** If true, collision is enabled for this section. */
	UPROPERTY()
	bool bEnableCollision;

	/** If true, this section will cast shadows. */
	UPROPERTY()
	bool bCastShadow;

	/** Default values. */
	FMeshSectionInfo()
		: MaterialIndex(0)
		, bEnableCollision(true)
		, bCastShadow(true)
	{
	}

	/** Default values with an explicit material index. */
	explicit FMeshSectionInfo(int32 InMaterialIndex)
		: MaterialIndex(InMaterialIndex)
		, bEnableCollision(true)
		, bCastShadow(true)
	{
	}
};

/** Comparison for mesh section info. */
bool operator==(const FMeshSectionInfo& A, const FMeshSectionInfo& B);
bool operator!=(const FMeshSectionInfo& A, const FMeshSectionInfo& B);

/**
 * Map containing per-section settings for each section of each LOD.
 */
USTRUCT()
struct FMeshSectionInfoMap
{
	GENERATED_USTRUCT_BODY()

	/** Maps an LOD+Section to the material it should render with. */
	UPROPERTY()
	TMap<uint32,FMeshSectionInfo> Map;

	/** Serialize. */
	void Serialize(FArchive& Ar);

	/** Clears all entries in the map resetting everything to default. */
	ENGINE_API void Clear();

	/** Get the number of section for a LOD. */
	ENGINE_API int32 GetSectionNumber(int32 LODIndex) const;

	/** Return true if the section exist, false otherwise. */
	ENGINE_API bool IsValidSection(int32 LODIndex, int32 SectionIndex) const;

	/** Gets per-section settings for the specified LOD + section. */
	ENGINE_API FMeshSectionInfo Get(int32 LODIndex, int32 SectionIndex) const;

	/** Sets per-section settings for the specified LOD + section. */
	ENGINE_API void Set(int32 LODIndex, int32 SectionIndex, FMeshSectionInfo Info);

	/** Resets per-section settings for the specified LOD + section to defaults. */
	ENGINE_API void Remove(int32 LODIndex, int32 SectionIndex);

	/** Copies per-section settings from the specified section info map. */
	ENGINE_API void CopyFrom(const FMeshSectionInfoMap& Other);

	/** Returns true if any section of the specified LOD has collision enabled. */
	bool AnySectionHasCollision(int32 LodIndex) const;
};

USTRUCT()
struct FAssetEditorOrbitCameraPosition
{
	GENERATED_USTRUCT_BODY()

	FAssetEditorOrbitCameraPosition()
		: bIsSet(false)
		, CamOrbitPoint(ForceInitToZero)
		, CamOrbitZoom(ForceInitToZero)
		, CamOrbitRotation(ForceInitToZero)
	{
	}

	FAssetEditorOrbitCameraPosition(const FVector& InCamOrbitPoint, const FVector& InCamOrbitZoom, const FRotator& InCamOrbitRotation)
		: bIsSet(true)
		, CamOrbitPoint(InCamOrbitPoint)
		, CamOrbitZoom(InCamOrbitZoom)
		, CamOrbitRotation(InCamOrbitRotation)
	{
	}

	/** Whether or not this has been set to a valid value */
	UPROPERTY()
	bool bIsSet;

	/** The position to orbit the camera around */
	UPROPERTY()
	FVector	CamOrbitPoint;

	/** The distance of the camera from the orbit point */
	UPROPERTY()
	FVector CamOrbitZoom;

	/** The rotation to apply around the orbit point */
	UPROPERTY()
	FRotator CamOrbitRotation;
};

#if WITH_EDITOR
/** delegate type for pre mesh build events */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPreMeshBuild, class UStaticMesh*);
/** delegate type for pre mesh build events */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostMeshBuild, class UStaticMesh*);
#endif

//~ Begin Material Interface for UStaticMesh - contains a material and other stuff
USTRUCT(BlueprintType)
struct FStaticMaterial
{
	GENERATED_USTRUCT_BODY()

		FStaticMaterial()
		: MaterialInterface(NULL)
		, MaterialSlotName(NAME_None)
#if WITH_EDITORONLY_DATA
		, ImportedMaterialSlotName(NAME_None)
#endif //WITH_EDITORONLY_DATA
	{

	}

	FStaticMaterial(class UMaterialInterface* InMaterialInterface
		, FName InMaterialSlotName = NAME_None
#if WITH_EDITORONLY_DATA
		, FName InImportedMaterialSlotName = NAME_None)
#else
		)
#endif
		: MaterialInterface(InMaterialInterface)
		, MaterialSlotName(InMaterialSlotName)
#if WITH_EDITORONLY_DATA
		, ImportedMaterialSlotName(InImportedMaterialSlotName)
#endif //WITH_EDITORONLY_DATA
	{
		//If not specified add some valid material slot name
		if (MaterialInterface && MaterialSlotName == NAME_None)
		{
			MaterialSlotName = MaterialInterface->GetFName();
		}
#if WITH_EDITORONLY_DATA
		if (ImportedMaterialSlotName == NAME_None)
		{
			ImportedMaterialSlotName = MaterialSlotName;
		}
#endif
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticMaterial& Elem);

	ENGINE_API friend bool operator==(const FStaticMaterial& LHS, const FStaticMaterial& RHS);
	ENGINE_API friend bool operator==(const FStaticMaterial& LHS, const UMaterialInterface& RHS);
	ENGINE_API friend bool operator==(const UMaterialInterface& LHS, const FStaticMaterial& RHS);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = StaticMesh)
	class UMaterialInterface* MaterialInterface;

	/*This name should be use by the gameplay to avoid error if the skeletal mesh Materials array topology change*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = StaticMesh)
	FName MaterialSlotName;

	/*This name should be use when we re-import a skeletal mesh so we can order the Materials array like it should be*/
	UPROPERTY(VisibleAnywhere, Category = StaticMesh)
	FName ImportedMaterialSlotName;

	/** Data used for texture streaming relative to each UV channels. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = StaticMesh)
	FMeshUVChannelInfo			UVChannelData;
};


enum EImportStaticMeshVersion
{
	// Before any version changes were made
	BeforeImportStaticMeshVersionWasAdded,
	// Remove the material re-order workflow
	RemoveStaticMeshSkinxxWorkflow,
	StaticMeshVersionPlusOne,
	LastVersion = StaticMeshVersionPlusOne - 1
};

USTRUCT()
struct FMaterialRemapIndex
{
	GENERATED_USTRUCT_BODY()

	FMaterialRemapIndex()
	{
		ImportVersionKey = 0;
	}

	FMaterialRemapIndex(uint32 VersionKey, TArray<int32> RemapArray)
	: ImportVersionKey(VersionKey)
	, MaterialRemap(RemapArray)
	{
	}

	UPROPERTY()
	uint32 ImportVersionKey;

	UPROPERTY()
	TArray<int32> MaterialRemap;
};

struct FStaticMeshDescriptionConstAttributeGetter
{
	ENGINE_API FStaticMeshDescriptionConstAttributeGetter(const FMeshDescription* InMeshDescription)
		: MeshDescription(InMeshDescription)
	{}

	const FMeshDescription* MeshDescription;

	ENGINE_API TVertexAttributesConstRef<FVector> GetPositions() const { return MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }

	ENGINE_API TVertexInstanceAttributesConstRef<FVector> GetNormals() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }

	ENGINE_API TVertexInstanceAttributesConstRef<FVector> GetTangents() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }

	ENGINE_API TVertexInstanceAttributesConstRef<float> GetBinormalSigns() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }

	ENGINE_API TVertexInstanceAttributesConstRef<FVector4> GetColors() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }

	ENGINE_API TVertexInstanceAttributesConstRef<FVector2D> GetUVs() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }

	ENGINE_API TEdgeAttributesConstRef<bool> GetEdgeHardnesses() const { return MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }

	ENGINE_API TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }

	ENGINE_API TPolygonGroupAttributesConstRef<FName> GetPolygonGroupImportedMaterialSlotNames() { return MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
};

struct FStaticMeshDescriptionAttributeGetter
{
	ENGINE_API FStaticMeshDescriptionAttributeGetter(FMeshDescription* InMeshDescription)
		: MeshDescription(InMeshDescription)
	{}
	
	FMeshDescription* MeshDescription;

	ENGINE_API TVertexAttributesRef<FVector> GetPositions() const { return MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }
	ENGINE_API TVertexAttributesConstRef<FVector> GetPositionsConst() const { return MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }

	ENGINE_API TVertexInstanceAttributesRef<FVector> GetNormals() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }
	ENGINE_API TVertexInstanceAttributesConstRef<FVector> GetNormalsConst() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }

	ENGINE_API TVertexInstanceAttributesRef<FVector> GetTangents() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }
	ENGINE_API TVertexInstanceAttributesConstRef<FVector> GetTangentsConst() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }

	ENGINE_API TVertexInstanceAttributesRef<float> GetBinormalSigns() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }
	ENGINE_API TVertexInstanceAttributesConstRef<float> GetBinormalSignsConst() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }

	ENGINE_API TVertexInstanceAttributesRef<FVector4> GetColors() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }
	ENGINE_API TVertexInstanceAttributesConstRef<FVector4> GetColorsConst() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }

	ENGINE_API TVertexInstanceAttributesRef<FVector2D> GetUVs() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }
	ENGINE_API TVertexInstanceAttributesConstRef<FVector2D> GetUVsConst() const { return MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }

	ENGINE_API TEdgeAttributesRef<bool> GetEdgeHardnesses() const { return MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }
	ENGINE_API TEdgeAttributesConstRef<bool> GetEdgeHardnessesConst() const { return MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }
	
	ENGINE_API TEdgeAttributesRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }
	ENGINE_API TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnessesConst() const { return MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }

	ENGINE_API TPolygonGroupAttributesRef<FName> GetPolygonGroupImportedMaterialSlotNames() { return MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
	ENGINE_API TPolygonGroupAttributesConstRef<FName> GetPolygonGroupImportedMaterialSlotNamesConst() { return MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
};


/**
 * A StaticMesh is a piece of geometry that consists of a static set of polygons.
 * Static Meshes can be translated, rotated, and scaled, but they cannot have their vertices animated in any way. As such, they are more efficient
 * to render than other types of geometry such as USkeletalMesh, and they are often the basic building block of levels created in the engine.
 *
 * @see https://docs.unrealengine.com/latest/INT/Engine/Content/Types/StaticMeshes/
 * @see AStaticMeshActor, UStaticMeshComponent
 */
UCLASS(hidecategories=Object, customconstructor, MinimalAPI, BlueprintType, config=Engine)
class UStaticMesh : public UStreamableRenderAsset, public IInterface_CollisionDataProvider, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	/** Notification when bounds changed */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnExtendedBoundsChanged, const FBoxSphereBounds&);

	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnMeshChanged);
#endif

	/** Pointer to the data used to render this static mesh. */
	TUniquePtr<class FStaticMeshRenderData> RenderData;

	/** Pointer to the occluder data used to rasterize this static mesh for software occlusion. */
	TUniquePtr<class FStaticMeshOccluderData> OccluderData;

#if WITH_EDITORONLY_DATA
	static const float MinimumAutoLODPixelError;

	/** Imported raw mesh bulk data. */
	UE_DEPRECATED(4.24, "Please do not access this member directly; use UStaticMesh::GetSourceModel(LOD) or UStaticMesh::GetSourceModels().")
	UPROPERTY()
	TArray<FStaticMeshSourceModel> SourceModels;

	/** Map of LOD+Section index to per-section info. */
	UE_DEPRECATED(4.24, "Please do not access this member directly; use UStaticMesh::GetSectionInfoMap().")
	UPROPERTY()
	FMeshSectionInfoMap SectionInfoMap;

	/**
	 * We need the OriginalSectionInfoMap to be able to build mesh in a non destructive way. Reduce has to play with SectionInfoMap in case some sections disappear.
	 * This member will be update in the following situation
	 * 1. After a static mesh import/reimport
	 * 2. Postload, if the OriginalSectionInfoMap is empty, we will fill it with the current SectionInfoMap
	 *
	 * We do not update it when the user shuffle section in the staticmesh editor because the OriginalSectionInfoMap must always be in sync with the saved rawMesh bulk data.
	 */
	UE_DEPRECATED(4.24, "Please do not access this member directly; use UStaticMesh::GetOriginalSectionInfoMap().")
	UPROPERTY()
	FMeshSectionInfoMap OriginalSectionInfoMap;

	/** The LOD group to which this mesh belongs. */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category=LodSettings)
	FName LODGroup;

	/**
	 * If non-negative, specify the maximum number of streamed LODs. Only has effect if
	 * mesh LOD streaming is enabled for the target platform.
	 */
	UPROPERTY()
	FPerPlatformInt NumStreamedLODs;

	/* The last import version */
	UPROPERTY()
	int32 ImportVersion;

	UPROPERTY()
	TArray<FMaterialRemapIndex> MaterialRemapIndexPerImportVersion;
	
	/* The lightmap UV generation version used during the last derived data build */
	UPROPERTY()
	int32 LightmapUVVersion;

	/** If true, the screen sizees at which LODs swap are computed automatically. */
	UPROPERTY()
	uint8 bAutoComputeLODScreenSize : 1;

	/**
	* If true on post load we need to calculate Display Factors from the
	* loaded LOD distances.
	*/
	uint8 bRequiresLODDistanceConversion : 1;

	/**
	 * If true on post load we need to calculate resolution independent Display Factors from the
	 * loaded LOD screen sizes.
	 */
	uint8 bRequiresLODScreenSizeConversion : 1;

	/** Materials used by this static mesh. Individual sections index in to this array. */
	UPROPERTY()
	TArray<UMaterialInterface*> Materials_DEPRECATED;

#endif // #if WITH_EDITORONLY_DATA

	/** Minimum LOD to use for rendering.  This is the default setting for the mesh and can be overridden by component settings. */
	UPROPERTY()
	FPerPlatformInt MinLOD;

	/** Bias multiplier for Light Propagation Volume lighting */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=StaticMesh, meta=(UIMin = "0.0", UIMax = "3.0"))
	float LpvBiasMultiplier;

	UPROPERTY()
	TArray<FStaticMaterial> StaticMaterials;

	UPROPERTY()
	float LightmapUVDensity;

	UPROPERTY(EditAnywhere, Category=StaticMesh, meta=(ClampMax = 4096, ToolTip="The light map resolution", FixedIncrement="4.0"))
	int32 LightMapResolution;

	/** The light map coordinate index */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=StaticMesh, meta=(ToolTip="The light map coordinate index", UIMin = "0", UIMax = "3"))
	int32 LightMapCoordinateIndex;

	/** Useful for reducing self shadowing from distance field methods when using world position offset to animate the mesh's vertices. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	float DistanceFieldSelfShadowBias;

	// Physics data.
	UPROPERTY(EditAnywhere, transient, duplicatetransient, Instanced, Category = StaticMesh)
	class UBodySetup* BodySetup;

	/** 
	 *	Specifies which mesh LOD to use for complex (per-poly) collision. 
	 *	Sometimes it can be desirable to use a lower poly representation for collision to reduce memory usage, improve performance and behaviour.
	 *	Collision representation does not change based on distance to camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh, meta=(DisplayName="LOD For Collision"))
	int32 LODForCollision;

	/** 
	 * Whether to generate a distance field for this mesh, which can be used by DistanceField Indirect Shadows.
	 * This is ignored if the project's 'Generate Mesh Distance Fields' setting is enabled.
	 */
	UPROPERTY(EditAnywhere, Category=StaticMesh)
	uint8 bGenerateMeshDistanceField : 1;

	/** If true, strips unwanted complex collision data aka kDOP tree when cooking for consoles.
		On the Playstation 3 data of this mesh will be stored in video memory. */
	UPROPERTY()
	uint8 bStripComplexCollisionForConsole_DEPRECATED:1;

	/** If true, mesh will have NavCollision property with additional data for navmesh generation and usage.
	    Set to false for distant meshes (always outside navigation bounds) to save memory on collision data. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Navigation)
	uint8 bHasNavigationData:1;

	/**	
		Mesh supports uniformly distributed sampling in constant time.
		Memory cost is 8 bytes per triangle.
		Example usage is uniform spawning of particles.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bSupportUniformlyDistributedSampling : 1;

protected:
	/** Tracks whether InitResources has been called, and rendering resources are initialized. */
	uint8 bRenderingResourcesInitialized:1;

public:
	/** 
	 *	If true, will keep geometry data CPU-accessible in cooked builds, rather than uploading to GPU memory and releasing it from CPU memory.
	 *	This is required if you wish to access StaticMesh geometry data on the CPU at runtime in cooked builds (e.g. to convert StaticMesh to ProceduralMeshComponent)
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bAllowCPUAccess:1;

	/**
	 * If true, a GPU buffer containing required data for uniform mesh surface sampling will be created at load time.
	 * It is created from the cpu data so bSupportUniformlyDistributedSampling is also required to be true.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bSupportGpuUniformlyDistributedSampling : 1;

	/** A fence which is used to keep track of the rendering thread releasing the static mesh resources. */
	FRenderCommandFence ReleaseResourcesFence;

#if WITH_EDITORONLY_DATA
	/** Importing data and options used for this mesh */
	UPROPERTY(EditAnywhere, Instanced, Category=ImportSettings)
	class UAssetImportData* AssetImportData;

	/** Path to the resource used to construct this static mesh */
	UPROPERTY()
	FString SourceFilePath_DEPRECATED;

	/** Date/Time-stamp of the file from the last import */
	UPROPERTY()
	FString SourceFileTimestamp_DEPRECATED;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, AdvancedDisplay, Category=StaticMesh)
	class UThumbnailInfo* ThumbnailInfo;

	/** The stored camera position to use as a default for the static mesh editor */
	UPROPERTY()
	FAssetEditorOrbitCameraPosition EditorCameraPosition;

	/** If the user has modified collision in any way or has custom collision imported. Used for determining if to auto generate collision on import */
	UPROPERTY(EditAnywhere, Category = Collision)
	bool bCustomizedCollision;

	/** 
	 *	Specifies which mesh LOD to use as occluder geometry for software occlusion
	 *  Set to -1 to not use this mesh as occluder 
	 */
	UPROPERTY(EditAnywhere, Category=StaticMesh, AdvancedDisplay, meta=(DisplayName="LOD For Occluder Mesh"))
	int32 LODForOccluderMesh;

#endif // WITH_EDITORONLY_DATA

	/** Unique ID for tracking/caching this mesh during distributed lighting */
	FGuid LightingGuid;

	/**
	 *	Array of named socket locations, set up in editor and used as a shortcut instead of specifying
	 *	everything explicitly to AttachComponent in the StaticMeshComponent.
	 */
	UPROPERTY()
	TArray<class UStaticMeshSocket*> Sockets;

	/** Data that is only available if this static mesh is an imported SpeedTree */
	TSharedPtr<class FSpeedTreeWind> SpeedTreeWind;

	/** Bound extension values in the positive direction of XYZ, positive value increases bound size */
	UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = StaticMesh)
	FVector PositiveBoundsExtension;
	/** Bound extension values in the negative direction of XYZ, positive value increases bound size */
	UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = StaticMesh)
	FVector NegativeBoundsExtension;
	/** Original mesh bounds extended with Positive/NegativeBoundsExtension */
	UPROPERTY()
	FBoxSphereBounds ExtendedBounds;

#if WITH_EDITOR
	FOnExtendedBoundsChanged OnExtendedBoundsChanged;
	FOnMeshChanged OnMeshChanged;

	/** This transient guid is use by the automation framework to modify the DDC key to force a build. */
	FGuid BuildCacheAutomationTestGuid;
#endif

protected:
	/**
	 * Index of an element to ignore while gathering streaming texture factors.
	 * This is useful to disregard automatically generated vertex data which breaks texture factor heuristics.
	 */
	UPROPERTY()
	int32 ElementToIgnoreForTexFactor;

	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = StaticMesh)
	TArray<UAssetUserData*> AssetUserData;

	TRefCountPtr<FRenderAssetUpdate> PendingUpdate;

	friend struct FStaticMeshUpdateContext;
	friend class FStaticMeshUpdate;

public:
	/** The editable mesh representation of this static mesh */
	// @todo: Maybe we don't want this visible in the details panel in the end; for now, this might aid debugging.
	UPROPERTY(Instanced, VisibleAnywhere, Category = EditableMesh)
	class UObject* EditableMesh;

	UPROPERTY(EditAnywhere, Category = Collision)
	class UStaticMesh* ComplexCollisionMesh;
	/**
	 * Registers the mesh attributes required by the mesh description for a static mesh.
	 */
	ENGINE_API static void RegisterMeshAttributes( FMeshDescription& MeshDescription );

#if WITH_EDITORONLY_DATA
	/*
	 * Return the MeshDescription associate to the LODIndex. The mesh description can be created on the fly if it was null
	 * and there is a FRawMesh data for this LODIndex.
	 */
	ENGINE_API FMeshDescription* GetMeshDescription(int32 LodIndex) const;
	ENGINE_API bool IsMeshDescriptionValid(int32 LodIndex) const;
	ENGINE_API FMeshDescription* CreateMeshDescription(int32 LodIndex);
	ENGINE_API FMeshDescription* CreateMeshDescription(int32 LodIndex, FMeshDescription MeshDescription);
	ENGINE_API void CommitMeshDescription(int32 LodIndex);
	ENGINE_API void ClearMeshDescription(int32 LodIndex);
	ENGINE_API void ClearMeshDescriptions();

	UE_DEPRECATED(4.22, "Please use GetMeshDescription().")
	FMeshDescription* GetOriginalMeshDescription(int32 LodIndex) const { return GetMeshDescription(LodIndex); }
	
	UE_DEPRECATED(4.22, "Please use CreateMeshDescription().")
	FMeshDescription* CreateOriginalMeshDescription(int32 LodIndex) { return CreateMeshDescription(LodIndex); }

	UE_DEPRECATED(4.22, "Please use CommitMeshDescription().")
	void CommitOriginalMeshDescription(int32 LodIndex) { CommitMeshDescription(LodIndex); }

	UE_DEPRECATED(4.22, "Please use ClearMeshDescription().")
	void ClearOriginalMeshDescription(int32 LodIndex) { ClearMeshDescription(LodIndex); }

	/**
	 * Internal function use to make sure all imported material slot name are unique and non empty.
	 */
	void FixupMaterialSlotName();

	/**
	 * Adds an empty UV channel at the end of the existing channels on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @return true if a UV channel was added.
	 */
	ENGINE_API bool AddUVChannel(int32 LODIndex);

	/**
	 * Inserts an empty UV channel at the specified channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to insert the UV channel.
	 * @return true if a UV channel was added.
	 */
	ENGINE_API bool InsertUVChannel(int32 LODIndex, int32 UVChannelIndex);

	/**
	 * Removes the UV channel at the specified channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to remove the UV channel.
	 * @return true if the UV channel was removed.
	 */
	ENGINE_API bool RemoveUVChannel(int32 LODIndex, int32 UVChannelIndex);

	/**
	 * Sets the texture coordinates at the specified UV channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to remove the UV channel.
	 * @param	TexCoords			The texture coordinates to set on the UV channel.
	 * @return true if the UV channel could be set.
	 */
	ENGINE_API bool SetUVChannel(int32 LODIndex, int32 UVChannelIndex, const TMap<FVertexInstanceID, FVector2D>& TexCoords);

#endif

	/**
	 * Returns the number of UV channels for the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @return the number of UV channels.
	 */
	ENGINE_API int32 GetNumUVChannels(int32 LODIndex);

	/** Pre-build navigation collision */
	UPROPERTY(VisibleAnywhere, transient, duplicatetransient, Instanced, Category = Navigation)
	UNavCollisionBase* NavCollision;
public:
	/**
	 * Default constructor
	 */
	ENGINE_API UStaticMesh(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UObject Interface.
#if WITH_EDITOR
	ENGINE_API virtual void PreEditChange(UProperty* PropertyAboutToChange) override;
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API virtual void PostEditUndo() override;
	ENGINE_API virtual void GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const override;
	ENGINE_API void SetLODGroup(FName NewGroup, bool bRebuildImmediately = true);
	ENGINE_API void BroadcastNavCollisionChange();

	FOnExtendedBoundsChanged& GetOnExtendedBoundsChanged() { return OnExtendedBoundsChanged; }
	FOnMeshChanged& GetOnMeshChanged() { return OnMeshChanged; }

	//SourceModels API
	ENGINE_API FStaticMeshSourceModel& AddSourceModel();
	ENGINE_API void SetNumSourceModels(int32 Num);
	ENGINE_API void RemoveSourceModel(int32 Index);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ENGINE_API TArray<FStaticMeshSourceModel>& GetSourceModels() { return SourceModels; }
	ENGINE_API const TArray<FStaticMeshSourceModel>& GetSourceModels() const { return SourceModels; }
	ENGINE_API FStaticMeshSourceModel& GetSourceModel(int32 Index) { return SourceModels[Index]; }
	ENGINE_API const FStaticMeshSourceModel& GetSourceModel(int32 Index) const { return SourceModels[Index]; }
	ENGINE_API int32 GetNumSourceModels() const { return SourceModels.Num(); }
	ENGINE_API bool IsSourceModelValid(int32 Index) const { return SourceModels.IsValidIndex(Index); }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ENGINE_API FMeshSectionInfoMap& GetSectionInfoMap() { return SectionInfoMap; }
	ENGINE_API const FMeshSectionInfoMap& GetSectionInfoMap() const { return SectionInfoMap; }
	ENGINE_API FMeshSectionInfoMap& GetOriginalSectionInfoMap() { return OriginalSectionInfoMap; }
	ENGINE_API const FMeshSectionInfoMap& GetOriginalSectionInfoMap() const { return OriginalSectionInfoMap; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/*
	 * Verify that a specific LOD using a material needing the adjacency buffer have the build option set to create the adjacency buffer.
	 *
	 * LODIndex: The LOD to fix
	 * bPreviewMode: If true the the function will not fix the build option. It will also change the return behavior, return true if the LOD need adjacency buffer, false otherwise
	 * bPromptUser: if true a dialog will ask the user if he agree changing the build option to allow adjacency buffer
	 * OutUserCancel: if the value is not null and the bPromptUser is true, the prompt dialog will have a cancel button and the result will be put in the parameter.
	 *
	 * The function will return true if any LOD build settings option is fix to add adjacency option. It will return false if no action was done. In case bPreviewMode is true it return true if the LOD need adjacency buffer, false otherwise.
	 */
	ENGINE_API bool FixLODRequiresAdjacencyInformation(const int32 LODIndex, const bool bPreviewMode = false, bool bPromptUser = false, bool* OutUserCancel = nullptr);
	
#endif // WITH_EDITOR

	ENGINE_API virtual void Serialize(FArchive& Ar) override;
	ENGINE_API virtual void PostInitProperties() override;
	ENGINE_API virtual void PostLoad() override;
	virtual bool IsPostLoadThreadSafe() const override;
	ENGINE_API virtual void BeginDestroy() override;
	ENGINE_API virtual bool IsReadyForFinishDestroy() override;
	ENGINE_API virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	ENGINE_API virtual FString GetDesc() override;
	ENGINE_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	ENGINE_API virtual bool CanBeClusterRoot() const override;
	//~ End UObject Interface.

	//~ Begin UStreamableRenderAsset Interface
	virtual int32 GetLODGroupForStreaming() const final override;
	virtual int32 GetNumMipsForStreaming() const final override;
	virtual int32 GetNumNonStreamingMips() const final override;
	virtual int32 CalcNumOptionalMips() const final override;
	virtual int32 CalcCumulativeLODSize(int32 NumLODs) const final override;
	virtual bool GetMipDataFilename(const int32 MipIndex, FString& BulkDataFilename) const final override;
	virtual bool IsReadyForStreaming() const final override;
	virtual int32 GetNumResidentMips() const final override;
	virtual int32 GetNumRequestedMips() const final override;
	virtual bool CancelPendingMipChangeRequest() final override;
	virtual bool HasPendingUpdate() const final override;
	virtual bool IsPendingUpdateLocked() const final override;
	virtual bool StreamOut(int32 NewMipCount) final override;
	virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;
	virtual bool UpdateStreamingStatus(bool bWaitForMipFading = false) final override;
	//~ End UStreamableRenderAsset Interface

	void LinkStreaming();
	void UnlinkStreaming();

	/**
	* Cancels any pending static mesh streaming actions if possible.
	* Returns when no more async loading requests are in flight.
	*/
	ENGINE_API static void CancelAllPendingStreamingActions();

	/**
	 * Rebuilds renderable data for this static mesh.
	 * @param bSilent - If true will not popup a progress dialog.
	 */
	ENGINE_API void Build(bool bSilent = false, TArray<FText>* OutErrors = nullptr);

	/**
	 * Initialize the static mesh's render resources.
	 */
	ENGINE_API virtual void InitResources();

	/**
	 * Releases the static mesh's render resources.
	 */
	ENGINE_API virtual void ReleaseResources();

	/**
	 * Update missing material UV channel data used for texture streaming. 
	 *
	 * @param bRebuildAll		If true, rebuild everything and not only missing data.
	 */
	ENGINE_API void UpdateUVChannelData(bool bRebuildAll);

	/**
	 * Returns the material bounding box. Computed from all lod-section using the material index.
	 *
	 * @param MaterialIndex			Material Index to look at
	 * @param TransformMatrix		Matrix to be applied to the position before computing the bounds
	 *
	 * @return false if some parameters are invalid
	 */
	ENGINE_API FBox GetMaterialBox(int32 MaterialIndex, const FTransform& Transform) const;

	/**
	 * Returns the UV channel data for a given material index. Used by the texture streamer.
	 * This data applies to all lod-section using the same material.
	 *
	 * @param MaterialIndex		the material index for which to get the data for.
	 * @return the data, or null if none exists.
	 */
	ENGINE_API const FMeshUVChannelInfo* GetUVChannelData(int32 MaterialIndex) const;

	/**
	 * Returns the number of vertices for the specified LOD.
	 */
	ENGINE_API int32 GetNumVertices(int32 LODIndex) const;

	/**
	 * Returns the number of LODs used by the mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh", meta=(ScriptName="GetNumLods"))
	ENGINE_API int32 GetNumLODs() const;

	/**
	 * Returns true if the mesh has data that can be rendered.
	 */
	ENGINE_API bool HasValidRenderData(bool bCheckLODForVerts = true, int32 LODIndex = INDEX_NONE) const;

	/**
	 * Returns the number of bounds of the mesh.
	 *
	 * @return	The bounding box represented as box origin with extents and also a sphere that encapsulates that box
	 */
	UFUNCTION( BlueprintPure, Category="StaticMesh" )
	ENGINE_API FBoxSphereBounds GetBounds() const;

	/** Returns the bounding box, in local space including bounds extension(s), of the StaticMesh asset */
	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	ENGINE_API FBox GetBoundingBox() const;

	/** Returns number of Sections that this StaticMesh has, in the supplied LOD (LOD 0 is the highest) */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API int32 GetNumSections(int32 InLOD) const;

	/**
	 * Gets a Material given a Material Index and an LOD number
	 *
	 * @return Requested material
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API UMaterialInterface* GetMaterial(int32 MaterialIndex) const;

	/**
	* Gets a Material index given a slot name
	*
	* @return Requested material
	*/
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API int32 GetMaterialIndex(FName MaterialSlotName) const;

	ENGINE_API int32 GetMaterialIndexFromImportedMaterialSlotName(FName ImportedMaterialSlotName) const;

	/**
	 * Returns the render data to use for exporting the specified LOD. This method should always
	 * be called when exporting a static mesh.
	 */
	ENGINE_API const FStaticMeshLODResources& GetLODForExport(int32 LODIndex) const;

	/**
	 * Static: Processes the specified static mesh for light map UV problems
	 *
	 * @param	InStaticMesh					Static mesh to process
	 * @param	InOutAssetsWithMissingUVSets	Array of assets that we found with missing UV sets
	 * @param	InOutAssetsWithBadUVSets		Array of assets that we found with bad UV sets
	 * @param	InOutAssetsWithValidUVSets		Array of assets that we found with valid UV sets
	 * @param	bInVerbose						If true, log the items as they are found
	 */
	ENGINE_API static void CheckLightMapUVs( UStaticMesh* InStaticMesh, TArray< FString >& InOutAssetsWithMissingUVSets, TArray< FString >& InOutAssetsWithBadUVSets, TArray< FString >& InOutAssetsWithValidUVSets, bool bInVerbose = true );

	//~ Begin Interface_CollisionDataProvider Interface
	ENGINE_API virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	ENGINE_API virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override
	{
		return true;
	}
	ENGINE_API virtual void GetMeshId(FString& OutMeshId) override;
	//~ End Interface_CollisionDataProvider Interface

	/** Return the number of sections of the StaticMesh with collision enabled */
	int32 GetNumSectionsWithCollision() const;

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface


	/**
	 * Create BodySetup for this staticmesh if it doesn't have one
	 */
	ENGINE_API void CreateBodySetup();

	/**
	 * Calculates navigation collision for caching
	 */
	ENGINE_API void CreateNavCollision(const bool bIsUpdate = false);

	FORCEINLINE const UNavCollisionBase* GetNavCollision() const { return NavCollision; }

	/** Configures this SM as bHasNavigationData = false and clears stored NavCollision */
	ENGINE_API void MarkAsNotHavingNavigationData();

	const FGuid& GetLightingGuid() const
	{
#if WITH_EDITORONLY_DATA
		return LightingGuid;
#else
		static const FGuid NullGuid( 0, 0, 0, 0 );
		return NullGuid;
#endif // WITH_EDITORONLY_DATA
	}

	void SetLightingGuid()
	{
#if WITH_EDITORONLY_DATA
		LightingGuid = FGuid::NewGuid();
#endif // WITH_EDITORONLY_DATA
	}

	/**
	 *	Add a socket object in this StaticMesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void AddSocket(UStaticMeshSocket* Socket);

	/**
	 *	Find a socket object in this StaticMesh by name.
	 *	Entering NAME_None will return NULL. If there are multiple sockets with the same name, will return the first one.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API class UStaticMeshSocket* FindSocket(FName InSocketName) const;

	/**
	 *	Remove a socket object in this StaticMesh by providing it's pointer. Use FindSocket() if needed.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void RemoveSocket(UStaticMeshSocket* Socket);

	/**
	 * Returns vertex color data by position.
	 * For matching to reimported meshes that may have changed or copying vertex paint data from mesh to mesh.
	 *
	 *	@param	VertexColorData		(out)A map of vertex position data and its color. The method fills this map.
	 */
	ENGINE_API void GetVertexColorData(TMap<FVector, FColor>& VertexColorData);

	/**
	 * Sets vertex color data by position.
	 * Map of vertex color data by position is matched to the vertex position in the mesh
	 * and nearest matching vertex color is used.
	 *
	 *	@param	VertexColorData		A map of vertex position data and color.
	 */
	ENGINE_API void SetVertexColorData(const TMap<FVector, FColor>& VertexColorData);

	/** Removes all vertex colors from this mesh and rebuilds it (Editor only */
	ENGINE_API void RemoveVertexColors();

	/** Make sure the Lightmap UV point on a valid UVChannel */
	ENGINE_API void EnforceLightmapRestrictions();

	/** Calculates the extended bounds */
	ENGINE_API void CalculateExtendedBounds();

	inline bool AreRenderingResourcesInitialized() const { return bRenderingResourcesInitialized; }

#if WITH_EDITOR

	/**
	 * Sets a Material given a Material Index
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void SetMaterial(int32 MaterialIndex, UMaterialInterface* NewMaterial);

	/**
	 * Returns true if LODs of this static mesh may share texture lightmaps.
	 */
	bool CanLODsShareStaticLighting() const;

	/**
	 * Retrieves the names of all LOD groups.
	 */
	ENGINE_API static void GetLODGroups(TArray<FName>& OutLODGroups);

	/**
	 * Retrieves the localized display names of all LOD groups.
	 */
	ENGINE_API static void GetLODGroupsDisplayNames(TArray<FText>& OutLODGroupsDisplayNames);

	ENGINE_API void GenerateLodsInPackage();

	ENGINE_API virtual void PostDuplicate(bool bDuplicateForPIE) override;

	/** Get multicast delegate broadcast prior to mesh building */
	FOnPreMeshBuild& OnPreMeshBuild() { return PreMeshBuild; }

	/** Get multicast delegate broadcast after mesh building */
	FOnPostMeshBuild& OnPostMeshBuild() { return PostMeshBuild; }
	

	/* Return true if the reduction settings are setup to reduce a LOD*/
	ENGINE_API bool IsReductionActive(int32 LODIndex) const;

	/* Get a copy of the reduction settings for a specified LOD index. */
	ENGINE_API struct FMeshReductionSettings GetReductionSettings(int32 LODIndex) const;

private:
	/**
	 * Converts legacy LODDistance in the source models to Display Factor
	 */
	void ConvertLegacyLODDistance();

	/**
	 * Converts legacy LOD screen area in the source models to resolution-independent screen size
	 */
	void ConvertLegacyLODScreenArea();

	/**
	 * Fixes up static meshes that were imported with sections that had zero triangles.
	 */
	void FixupZeroTriangleSections();

	/**
	* Return mesh data key. The key is the ddc filename for the mesh data
	*/
	bool GetMeshDataKey(int32 LodIndex, FString& OutKey) const;

	/**
	* Caches mesh data.
	*/
	void CacheMeshData();

public:
	/**
	 * Caches derived renderable data.
	 */
	ENGINE_API void CacheDerivedData();

private:

	FOnPreMeshBuild PreMeshBuild;
	FOnPostMeshBuild PostMeshBuild;

	/**
	 * Fixes up the material when it was converted to the new staticmesh build process
	 */
	bool bCleanUpRedundantMaterialPostLoad;
#endif // #if WITH_EDITOR
};
