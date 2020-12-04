// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/TextureStreamingTypes.h"
#include "Components/PrimitiveComponent.h"
#include "PerPlatformProperties.h"
#include "LandscapePhysicalMaterial.h"
#include "LandscapeWeightmapUsage.h"
#include "Engine/StreamableRenderAsset.h"

#include "LandscapeComponent.generated.h"

#define LANDSCAPE_LOD_STREAMING_USE_TOKEN (!WITH_EDITORONLY_DATA && USE_BULKDATA_STREAMING_TOKEN)

class ALandscape;
class ALandscapeProxy;
class FLightingBuildOptions;
class FMaterialUpdateContext;
class FMeshMapBuildData;
class FPrimitiveSceneProxy;
class ITargetPlatform;
class ULandscapeComponent;
class ULandscapeGrassType;
class ULandscapeHeightfieldCollisionComponent;
class ULandscapeInfo;
class ULandscapeLayerInfoObject;
class ULightComponent;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture2D;
struct FConvexVolume;
struct FEngineShowFlags;
struct FLandscapeEditDataInterface;
struct FLandscapeTextureDataInfo;
struct FStaticLightingPrimitiveInfo;

struct FLandscapeEditDataInterface;
struct FLandscapeMobileRenderData;

//
// FLandscapeEditToolRenderData
//
USTRUCT()
struct FLandscapeEditToolRenderData
{
public:
	GENERATED_USTRUCT_BODY()

	enum SelectionType
	{
		ST_NONE = 0,
		ST_COMPONENT = 1,
		ST_REGION = 2,
		// = 4...
	};

	FLandscapeEditToolRenderData()
		: ToolMaterial(NULL),
		GizmoMaterial(NULL),
		SelectedType(ST_NONE),
		DebugChannelR(INDEX_NONE),
		DebugChannelG(INDEX_NONE),
		DebugChannelB(INDEX_NONE),
		DataTexture(NULL),
		LayerContributionTexture(NULL),
		DirtyTexture(NULL)
	{}

	// Material used to render the tool.
	UPROPERTY(NonTransactional)
	UMaterialInterface* ToolMaterial;

	// Material used to render the gizmo selection region...
	UPROPERTY(NonTransactional)
	UMaterialInterface* GizmoMaterial;

	// Component is selected
	UPROPERTY(NonTransactional)
	int32 SelectedType;

	UPROPERTY(NonTransactional)
	int32 DebugChannelR;

	UPROPERTY(NonTransactional)
	int32 DebugChannelG;

	UPROPERTY(NonTransactional)
	int32 DebugChannelB;

	UPROPERTY(NonTransactional)
	UTexture2D* DataTexture; // Data texture other than height/weight

	UPROPERTY(NonTransactional)
	UTexture2D* LayerContributionTexture; // Data texture used to represent layer contribution

	UPROPERTY(NonTransactional)
	UTexture2D* DirtyTexture; // Data texture used to represent layer blend dirtied area

#if WITH_EDITOR
	void UpdateDebugColorMaterial(const ULandscapeComponent* const Component);
	void UpdateSelectionMaterial(int32 InSelectedType, const ULandscapeComponent* const Component);
#endif
};

class FLandscapeComponentDerivedData
{
	/** The compressed Landscape component data for mobile rendering. Serialized to disk. 
	    On device, freed once it has been decompressed. */
	TArray<uint8> CompressedLandscapeData;

#if LANDSCAPE_LOD_STREAMING_USE_TOKEN
	TArray<FBulkDataStreamingToken> StreamingLODDataArray;
#else
	TArray<FByteBulkData> StreamingLODDataArray;
#endif
	
	/** Cached render data. Only valid on device. */
	TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe > CachedRenderData;

	FString CachedLODDataFileName;

	friend class ULandscapeLODStreamingProxy;

public:
	/** Returns true if there is any valid platform data */
	bool HasValidPlatformData() const
	{
		return CompressedLandscapeData.Num() != 0;
	}

	/** Returns true if there is any valid platform data */
	bool HasValidRuntimeData() const
	{
		return CompressedLandscapeData.Num() != 0 || CachedRenderData.IsValid();
	}

	/** Returns the size of the platform data if there is any. */
	int32 GetPlatformDataSize() const
	{
		int32 Result = CompressedLandscapeData.Num();
		for (int32 Idx = 0; Idx < StreamingLODDataArray.Num(); ++Idx)
		{
			Result += (int32)StreamingLODDataArray[Idx].GetBulkDataSize();
		}
		return Result;
	}

	/** Initializes the compressed data from an uncompressed source. */
	void InitializeFromUncompressedData(const TArray<uint8>& UncompressedData, const TArray<TArray<uint8>>& StreamingLODs);

	/** Decompresses data if necessary and returns the render data object. 
     *  On device, this frees the compressed data and keeps a reference to the render data. */
	TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> GetRenderData();

	/** Constructs a key string for the DDC that uniquely identifies a the Landscape component's derived data. */
	static FString GetDDCKeyString(const FGuid& StateId);

	/** Loads the platform data from DDC */
	bool LoadFromDDC(const FGuid& StateId, UObject* Component);

	/** Saves the compressed platform data to the DDC */
	void SaveToDDC(const FGuid& StateId, UObject* Component);

	/* Serializer */
	void Serialize(FArchive& Ar, UObject* Owner);
};

/* Used to uniquely reference a landscape vertex in a component. */
struct FLandscapeVertexRef
{
	FLandscapeVertexRef(int16 InX, int16 InY, int8 InSubX, int8 InSubY)
		: X(InX)
		, Y(InY)
		, SubX(InSubX)
		, SubY(InSubY)
	{}

	uint32 X : 8;
	uint32 Y : 8;
	uint32 SubX : 8;
	uint32 SubY : 8;

	/** Helper to provide a standard ordering for vertex arrays. */
	static int32 GetVertexIndex(FLandscapeVertexRef Vert, int32 SubsectionCount, int32 SubsectionVerts)
	{
		return (Vert.SubY * SubsectionVerts + Vert.Y) * SubsectionVerts * SubsectionCount + Vert.SubX * SubsectionVerts + Vert.X;
	}
};

/** Stores information about which weightmap texture and channel each layer is stored */
USTRUCT()
struct FWeightmapLayerAllocationInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	ULandscapeLayerInfoObject* LayerInfo;

	UPROPERTY()
	uint8 WeightmapTextureIndex;

	UPROPERTY()
	uint8 WeightmapTextureChannel;

	FWeightmapLayerAllocationInfo()
		: LayerInfo(nullptr)
		, WeightmapTextureIndex(0)
		, WeightmapTextureChannel(0)
	{
	}


	FWeightmapLayerAllocationInfo(ULandscapeLayerInfoObject* InLayerInfo)
		:	LayerInfo(InLayerInfo)
		,	WeightmapTextureIndex(255)	// Indicates an invalid allocation
		,	WeightmapTextureChannel(255)
	{
	}
	
	FName GetLayerName() const;

	uint32 GetHash() const;

	void Free()
	{
		WeightmapTextureChannel = 255;
		WeightmapTextureIndex = 255;
	}

	bool IsAllocated() const { return (WeightmapTextureChannel != 255 && WeightmapTextureIndex != 255); }
};

struct FLandscapeComponentGrassData
{
#if WITH_EDITORONLY_DATA
	// Variables used to detect when grass data needs to be regenerated:

	// Guid per material instance in the hierarchy between the assigned landscape material (instance) and the root UMaterial
	// used to detect changes to material instance parameters or the root material that could affect the grass maps
	TArray<FGuid, TInlineAllocator<2>> MaterialStateIds;
	// cached component rotation when material world-position-offset is used,
	// as this will affect the direction of world-position-offset deformation (included in the HeightData below)
	FQuat RotationForWPO;
#endif

	TArray<uint16> HeightData;
#if WITH_EDITORONLY_DATA
	// Height data for LODs 1+, keyed on LOD index
	TMap<int32, TArray<uint16>> HeightMipData;

	// Grass data was updated but not saved yet
	bool bIsDirty;
#endif
	TMap<ULandscapeGrassType*, TArray<uint8>> WeightData;

	FLandscapeComponentGrassData()
#if WITH_EDITORONLY_DATA
		: bIsDirty(false) 
#endif
	{}

#if WITH_EDITOR
	FLandscapeComponentGrassData(ULandscapeComponent* Component);
#endif

	bool HasData()
	{
		return HeightData.Num() > 0 ||
#if WITH_EDITORONLY_DATA
			HeightMipData.Num() > 0 ||
#endif
			WeightData.Num() > 0;
	}

	SIZE_T GetAllocatedSize() const;

	// Check whether we can discard any data not needed with current scalability settings
	void ConditionalDiscardDataOnLoad();

	friend FArchive& operator<<(FArchive& Ar, FLandscapeComponentGrassData& Data);
};

USTRUCT(NotBlueprintable)
struct FLandscapeComponentMaterialOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = LandscapeComponent, meta=(UIMin=0, UIMax=8, ClampMin=0, ClampMax=8))
	FPerPlatformInt LODIndex;

	UPROPERTY(EditAnywhere, Category = LandscapeComponent)
	UMaterialInterface* Material;
};

USTRUCT(NotBlueprintable)
struct FWeightmapData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<UTexture2D*> Textures;
	
	UPROPERTY()
	TArray<FWeightmapLayerAllocationInfo> LayerAllocations;

	UPROPERTY(Transient)
	TArray<ULandscapeWeightmapUsage*> TextureUsages;
};

USTRUCT(NotBlueprintable)
struct FHeightmapData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UTexture2D* Texture;
};

USTRUCT(NotBlueprintable)
struct FLandscapeLayerComponentData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FHeightmapData HeightmapData;

	UPROPERTY()
	FWeightmapData WeightmapData;

	bool IsInitialized() const { return HeightmapData.Texture != nullptr || WeightmapData.Textures.Num() > 0;  }
};

#if WITH_EDITOR
enum ELandscapeComponentUpdateFlag : uint32
{
	// Will call UpdateCollisionHeightData, UpdateCacheBounds, UpdateComponentToWorld on Component
	Component_Update_Heightmap_Collision = 1 << 0,
	// Will call UdateCollisionLayerData on Component
	Component_Update_Weightmap_Collision = 1 << 1,
	// Will call RecreateCollision on Component
	Component_Update_Recreate_Collision = 1 << 2,
	// Will update Component clients: Navigation data, Foliage, Grass, etc.
	Component_Update_Client = 1 << 3,
	// Will update Component clients while editing
	Component_Update_Client_Editing = 1 << 4,
	// Will compute component approximated bounds
	Component_Update_Approximated_Bounds = 1 << 5
};

enum ELandscapeLayerUpdateMode : uint32
{ 
	// No Update
	Update_None = 0,
	// Update types
	Update_Heightmap_All = 1 << 0,
	Update_Heightmap_Editing = 1 << 1,
	Update_Heightmap_Editing_NoCollision = 1 << 2,
	Update_Weightmap_All = 1 << 3,
	Update_Weightmap_Editing = 1 << 4,
	Update_Weightmap_Editing_NoCollision = 1 << 5,
	// Combinations
	Update_All = Update_Weightmap_All | Update_Heightmap_All,
	Update_All_Editing = Update_Weightmap_Editing | Update_Heightmap_Editing,
	Update_All_Editing_NoCollision = Update_Weightmap_Editing_NoCollision | Update_Heightmap_Editing_NoCollision,
	// In cases where we couldn't update the clients right away this flag will be set in RegenerateLayersContent
	Update_Client_Deferred = 1 << 6,
	// Update landscape component clients while editing
	Update_Client_Editing = 1 << 7
};

static const uint32 DefaultSplineHash = 0xFFFFFFFF;

#endif

UENUM()
enum ELandscapeClearMode
{
	Clear_Weightmap = 1 << 0 UMETA(DisplayName = "Paint"),
	Clear_Heightmap = 1 << 1 UMETA(DisplayName = "Sculpt"),
	Clear_All = Clear_Weightmap | Clear_Heightmap UMETA(DisplayName = "All")
};

UCLASS(MinimalAPI)
class ULandscapeLODStreamingProxy : public UStreamableRenderAsset
{
	GENERATED_UCLASS_BODY()

	//~ Begin UStreamableRenderAsset Interface
	virtual LANDSCAPE_API int32 CalcCumulativeLODSize(int32 NumLODs) const final override;
	virtual LANDSCAPE_API FIoFilenameHash GetMipIoFilenameHash(const int32 MipIndex) const  final override;
	virtual LANDSCAPE_API bool HasPendingRenderResourceInitialization() const final override;
	virtual bool StreamOut(int32 NewMipCount) final override;
	virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;
	virtual EStreamableRenderAssetType GetRenderAssetType() const final override { return EStreamableRenderAssetType::LandscapeMeshMobile; }
	//~ End UStreamableRenderAsset Interface

	LANDSCAPE_API bool GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const;


	LANDSCAPE_API TArray<float> GetLODScreenSizeArray() const;
	LANDSCAPE_API TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> GetRenderData() const;

	typedef typename TChooseClass<LANDSCAPE_LOD_STREAMING_USE_TOKEN, FBulkDataStreamingToken, FByteBulkData>::Result BulkDataType;
	LANDSCAPE_API BulkDataType& GetStreamingLODBulkData(int32 LODIdx) const;

	static LANDSCAPE_API void CancelAllPendingStreamingActions();

	void ClearStreamingResourceState();
	void InitResourceStateForMobileStreaming();

private:

	ULandscapeComponent* LandscapeComponent = nullptr;
};

UCLASS(hidecategories=(Display, Attachment, Physics, Debug, Collision, Movement, Rendering, PrimitiveComponent, Object, Transform, Mobility, VirtualTexture), showcategories=("Rendering|Material"), MinimalAPI, Within=LandscapeProxy)
class ULandscapeComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()
	
	/** X offset from global components grid origin (in quads) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LandscapeComponent)
	int32 SectionBaseX;

	/** Y offset from global components grid origin (in quads) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LandscapeComponent)
	int32 SectionBaseY;

	/** Total number of quads for this component, has to be >0 */
	UPROPERTY()
	int32 ComponentSizeQuads;

	/** Number of quads for a subsection of the component. SubsectionSizeQuads+1 must be a power of two. */
	UPROPERTY()
	int32 SubsectionSizeQuads;

	/** Number of subsections in X or Y axis */
	UPROPERTY()
	int32 NumSubsections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LandscapeComponent)
	UMaterialInterface* OverrideMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LandscapeComponent, AdvancedDisplay)
	UMaterialInterface* OverrideHoleMaterial;

	UPROPERTY(EditAnywhere, Category = LandscapeComponent)
	TArray<FLandscapeComponentMaterialOverride> OverrideMaterials;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UMaterialInstanceConstant* MaterialInstance_DEPRECATED;
#endif

	UPROPERTY(TextExportTransient)
	TArray<UMaterialInstanceConstant*> MaterialInstances;

	UPROPERTY(Transient, TextExportTransient)
	TArray<UMaterialInstanceDynamic*> MaterialInstancesDynamic;

	/** Mapping between LOD and Material Index*/
	UPROPERTY(TextExportTransient)
	TArray<int8> LODIndexToMaterialIndex;

	/** Mapping between Material Index to associated generated disabled Tessellation Material*/
	UPROPERTY(TextExportTransient)
	TArray<int8> MaterialIndexToDisabledTessellationMaterial;

	/** XYOffsetmap texture reference */
	UPROPERTY(TextExportTransient)
	UTexture2D* XYOffsetmapTexture;

	/** UV offset to component's weightmap data from component local coordinates*/
	UPROPERTY()
	FVector4 WeightmapScaleBias;

	/** U or V offset into the weightmap for the first subsection, in texture UV space */
	UPROPERTY()
	float WeightmapSubsectionOffset;

	/** UV offset to Heightmap data from component local coordinates */
	UPROPERTY()
	FVector4 HeightmapScaleBias;

	/** Cached local-space bounding box, created at heightmap update time */
	UPROPERTY()
	FBox CachedLocalBox;

	/** Reference to associated collision component */
	UPROPERTY()
	TLazyObjectPtr<ULandscapeHeightfieldCollisionComponent> CollisionComponent;

private:
#if WITH_EDITORONLY_DATA
	/** Unique ID for this component, used for caching during distributed lighting */
	UPROPERTY()
	FGuid LightingGuid;

	UPROPERTY()
	TMap<FGuid, FLandscapeLayerComponentData> LayersData;

	/** Compoment's Data for Editing Layer */
	FGuid LandscapeEditingLayer;
	mutable FGuid CachedEditingLayer;
	mutable FLandscapeLayerComponentData* CachedEditingLayerData;
		
	// Final layer data
	UPROPERTY(Transient)
	TArray<ULandscapeWeightmapUsage*> WeightmapTexturesUsage;

	UPROPERTY(Transient)
	uint32 LayerUpdateFlagPerMode;

	/** Dirtied collision height region when painting (only used by Landscape Layer System) */
	FIntRect LayerDirtyCollisionHeightData;
#endif // WITH_EDITORONLY_DATA

	/** Heightmap texture reference */
	UPROPERTY(TextExportTransient)
	UTexture2D* HeightmapTexture;

	/** List of layers, and the weightmap and channel they are stored */
	UPROPERTY()
	TArray<FWeightmapLayerAllocationInfo> WeightmapLayerAllocations;

	/** Weightmap texture reference */
	UPROPERTY(TextExportTransient)
	TArray<UTexture2D*> WeightmapTextures;

	/** Used to interface the component to the LOD streamer. */
	UPROPERTY()
	ULandscapeLODStreamingProxy* LODStreamingProxy;

public:

	/** Uniquely identifies this component's built map data. */
	UPROPERTY()
	FGuid MapBuildDataId;

	/**	Legacy irrelevant lights */
	UPROPERTY()
	TArray<FGuid> IrrelevantLights_DEPRECATED;

	/** Heightfield mipmap used to generate collision */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	int32 CollisionMipLevel;

	/** Heightfield mipmap used to generate simple collision */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	int32 SimpleCollisionMipLevel;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the negative Z axis, positive value increases bound size */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	float NegativeZBoundsExtension;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the positive Z axis, positive value increases bound size */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	float PositiveZBoundsExtension;

	/** StaticLightingResolution overriding per component, default value 0 means no overriding */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent, meta=(ClampMax = 4096))
	float StaticLightingResolution;

	/** Forced LOD level to use when rendering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LandscapeComponent)
	int32 ForcedLOD;

	/** LOD level Bias to use when rendering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LandscapeComponent)
	int32 LODBias;

	UPROPERTY()
	FGuid StateId;

	/** The Material Guid that used when baking, to detect material recompilations */
	UPROPERTY()
	FGuid BakedTextureMaterialGuid;

	/** Pre-baked Base Color texture for use by distance field GI */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = BakedTextures)
	UTexture2D* GIBakedBaseColorTexture;

#if WITH_EDITORONLY_DATA
	/** LOD level Bias to use when lighting buidling via lightmass, -1 Means automatic LOD calculation based on ForcedLOD + LODBias */
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	int32 LightingLODBias;

	// List of layers allowed to be painted on this component
	UPROPERTY(EditAnywhere, Category=LandscapeComponent)
	TArray<ULandscapeLayerInfoObject*> LayerWhitelist;

	/** Pointer to data shared with the render thread, used by the editor tools */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FLandscapeEditToolRenderData EditToolRenderData;

	/** Hash of source for mobile generated data. Used determine if we need to re-generate mobile pixel data. */
	UPROPERTY(DuplicateTransient)
	FGuid MobileDataSourceHash;

	/** Represent the chosen material for each LOD */
	UPROPERTY(DuplicateTransient)
	TMap<UMaterialInterface*, int8> MaterialPerLOD;

	/** Represents hash of last weightmap usage update */
	uint32 WeightmapsHash;

	UPROPERTY()
	uint32 SplineHash;

	/** Represents hash for last PhysicalMaterialTask */
	UPROPERTY()
	uint32 PhysicalMaterialHash;
#endif

	/** For mobile */
	UPROPERTY()
	uint8 MobileBlendableLayerMask;

	UPROPERTY(NonPIEDuplicateTransient)
	UMaterialInterface* MobileMaterialInterface_DEPRECATED;

	/** Material interfaces used for mobile */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UMaterialInterface*> MobileMaterialInterfaces;

	/** Generated weightmap textures used for mobile. The first entry is also used for the normal map. 
	  * Serialized only when cooking or loading cooked builds. */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UTexture2D*> MobileWeightmapTextures;

#if WITH_EDITORONLY_DATA
	/** Layer allocations used by mobile. Cached value here used only in the editor for usage visualization. */
	TArray<FWeightmapLayerAllocationInfo> MobileWeightmapLayerAllocations;

	/** The editor needs to save out the combination MIC we'll use for mobile, 
	  because we cannot generate it at runtime for standalone PIE games */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UMaterialInstanceConstant*> MobileCombinationMaterialInstances;

	UPROPERTY(NonPIEDuplicateTransient)
	UMaterialInstanceConstant* MobileCombinationMaterialInstance_DEPRECATED;
#endif

public:
	/** Platform Data where don't support texture sampling in vertex buffer */
	FLandscapeComponentDerivedData PlatformData;

	/** Grass data for generation **/
	TSharedRef<FLandscapeComponentGrassData, ESPMode::ThreadSafe> GrassData;
	TArray<FBox> ActiveExcludedBoxes;
	uint32 ChangeTag;

#if WITH_EDITOR
	/** Physical material update task */
	FLandscapePhysicalMaterialRenderTask PhysicalMaterialTask;
	uint32 CalculatePhysicalMaterialTaskHash() const;
#endif

	//~ Begin UObject Interface.	
	virtual void PostInitProperties() override;	
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual void BeginDestroy() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual void PostLoad() override;
	virtual void PostEditUndo() override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface

	LANDSCAPE_API void UpdateEditToolRenderData();

	/** Fix up component layers, weightmaps
	 */
	LANDSCAPE_API void FixupWeightmaps();

	// Update layer whitelist to include the currently painted layers
	LANDSCAPE_API void UpdateLayerWhitelistFromPaintedLayers();
	
	//~ Begin UPrimitiveComponent Interface.
	virtual bool GetLightMapResolution( int32& Width, int32& Height ) const override;
	virtual int32 GetStaticLightMapResolution() const override;
	virtual void GetLightAndShadowMapMemoryUsage( int32& LightMapMemoryUsage, int32& ShadowMapMemoryUsage ) const override;
	virtual void GetStaticLightingInfo(FStaticLightingPrimitiveInfo& OutPrimitiveInfo,const TArray<ULightComponent*>& InRelevantLights,const FLightingBuildOptions& Options) override;
	virtual void AddMapBuildDataGUIDs(TSet<FGuid>& InGUIDs) const override;
#endif
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual ELightMapInteractionType GetStaticLightingType() const override { return LMIT_Texture;	}
	virtual void GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const override;
	virtual bool IsPrecomputedLightingValid() const override;

	virtual TArray<URuntimeVirtualTexture*> const& GetRuntimeVirtualTextures() const override;
	virtual ERuntimeVirtualTextureMainPassType GetVirtualTextureRenderPassType() const override;

	LANDSCAPE_API UTexture2D* GetHeightmap(bool InReturnEditingHeightmap = false) const;
	LANDSCAPE_API TArray<UTexture2D*>& GetWeightmapTextures(bool InReturnEditingWeightmap = false);
	LANDSCAPE_API const TArray<UTexture2D*>& GetWeightmapTextures(bool InReturnEditingWeightmap = false) const;

	LANDSCAPE_API TArray<FWeightmapLayerAllocationInfo>& GetWeightmapLayerAllocations(bool InReturnEditingWeightmap = false);
	LANDSCAPE_API const TArray<FWeightmapLayerAllocationInfo>& GetWeightmapLayerAllocations(bool InReturnEditingWeightmap = false) const;
	LANDSCAPE_API TArray<FWeightmapLayerAllocationInfo>& GetWeightmapLayerAllocations(const FGuid& InLayerGuid);
	LANDSCAPE_API const TArray<FWeightmapLayerAllocationInfo>& GetWeightmapLayerAllocations(const FGuid& InLayerGuid) const;

#if WITH_EDITOR
	LANDSCAPE_API uint32 ComputeLayerHash() const;

	LANDSCAPE_API void SetHeightmap(UTexture2D* NewHeightmap);

	LANDSCAPE_API void SetWeightmapTextures(const TArray<UTexture2D*>& InNewWeightmapTextures, bool InApplyToEditingWeightmap = false);

	LANDSCAPE_API void SetWeightmapLayerAllocations(const TArray<FWeightmapLayerAllocationInfo>& InNewWeightmapLayerAllocations);
	LANDSCAPE_API void SetWeightmapTexturesUsage(const TArray<ULandscapeWeightmapUsage*>& InNewWeightmapTexturesUsage, bool InApplyToEditingWeightmap = false);
	LANDSCAPE_API TArray<ULandscapeWeightmapUsage*>& GetWeightmapTexturesUsage(bool InReturnEditingWeightmap = false);
	LANDSCAPE_API const TArray<ULandscapeWeightmapUsage*>& GetWeightmapTexturesUsage(bool InReturnEditingWeightmap = false) const;

	LANDSCAPE_API bool HasLayersData() const;
	LANDSCAPE_API const FLandscapeLayerComponentData* GetLayerData(const FGuid& InLayerGuid) const;
	LANDSCAPE_API FLandscapeLayerComponentData* GetLayerData(const FGuid& InLayerGuid);
	LANDSCAPE_API void AddLayerData(const FGuid& InLayerGuid, const FLandscapeLayerComponentData& InData);
	LANDSCAPE_API void AddDefaultLayerData(const FGuid& InLayerGuid, const TArray<ULandscapeComponent*>& InComponentsUsingHeightmap, TMap<UTexture2D*, UTexture2D*>& InOutCreatedHeightmapTextures);
	LANDSCAPE_API void RemoveLayerData(const FGuid& InLayerGuid);
	LANDSCAPE_API void ForEachLayer(TFunctionRef<void(const FGuid&, struct FLandscapeLayerComponentData&)> Fn);

	LANDSCAPE_API void SetEditingLayer(const FGuid& InEditingLayer);
	FLandscapeLayerComponentData* GetEditingLayer();
	const FLandscapeLayerComponentData* GetEditingLayer() const;
	FGuid GetEditingLayerGUID() const;

	void CopyFinalLayerIntoEditingLayer(FLandscapeEditDataInterface& DataInterface, TSet<UTexture2D*>& ProcessedHeightmaps);
#endif 

#if WITH_EDITOR
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual void PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel) override;
#endif
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) override;
#endif
	virtual void PropagateLightingScenarioChange() override;
	//~ End UActorComponent Interface.

	/** Gets the landscape info object for this landscape */
	LANDSCAPE_API ULandscapeInfo* GetLandscapeInfo() const;

#if WITH_EDITOR

	/** Deletes a layer from this component, removing all its data */
	LANDSCAPE_API void DeleteLayer(ULandscapeLayerInfoObject* LayerInfo, FLandscapeEditDataInterface& LandscapeEdit);

	/** Fills a layer to 100% on this component, adding it if needed and removing other layers that get painted away */
	LANDSCAPE_API void FillLayer(ULandscapeLayerInfoObject* LayerInfo, FLandscapeEditDataInterface& LandscapeEdit);

	/** Replaces one layerinfo on this component with another */
	LANDSCAPE_API void ReplaceLayer(ULandscapeLayerInfoObject* FromLayerInfo, ULandscapeLayerInfoObject* ToLayerInfo, FLandscapeEditDataInterface& LandscapeEdit);

	// true if the component's landscape material supports grass
	bool MaterialHasGrass() const;

	/** Creates and destroys cooked grass data stored in the map */
	void RenderGrassMap();
	void RemoveGrassMap();

	/* Could a grassmap currently be generated, disregarding whether our textures are streamed in? */
	bool CanRenderGrassMap() const;

	/* Are the textures we need to render a grassmap currently streamed in? */
	bool AreTexturesStreamedForGrassMapRender() const;

	/* Is the grassmap data outdated, eg by a material */
	bool IsGrassMapOutdated() const;

	/** Renders the heightmap of this component (including material world-position-offset) at the specified LOD */
	TArray<uint16> RenderWPOHeightmap(int32 LOD);

	/* Serialize all hashes/guids that record the current state of this component */
	void SerializeStateHashes(FArchive& Ar);

	// Generates mobile platform data for this component
	void GenerateMobileWeightmapLayerAllocations();
	void GeneratePlatformVertexData(const ITargetPlatform* TargetPlatform);
	void GeneratePlatformPixelData();

	/** Generate mobile data if it's missing or outdated */
	void CheckGenerateLandscapePlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform);
#endif

	LANDSCAPE_API int32 GetMaterialInstanceCount(bool InDynamic = true) const;
	LANDSCAPE_API class UMaterialInstance* GetMaterialInstance(int32 InIndex, bool InDynamic = true) const;

	/** Gets the landscape material instance dynamic for this component */
	UFUNCTION(BlueprintCallable, Category = "Landscape|Runtime|Material")
	class UMaterialInstanceDynamic* GetMaterialInstanceDynamic(int32 InIndex) const;

	/** Gets the landscape paint layer weight value at the given position using LandscapeLayerInfo . Returns 0 in case it fails. */
	UFUNCTION(BlueprintCallable, Category = "Landscape|Editor")
	LANDSCAPE_API float EditorGetPaintLayerWeightAtLocation(const FVector& InLocation, ULandscapeLayerInfoObject* PaintLayer);

	/** Gets the landscape paint layer weight value at the given position using layer name. Returns 0 in case it fails. */
	UFUNCTION(BlueprintCallable, Category = "Landscape|Editor")
	LANDSCAPE_API float EditorGetPaintLayerWeightByNameAtLocation(const FVector& InLocation, const FName InPaintLayerName);
		
	/** Get the landscape actor associated with this component. */
	LANDSCAPE_API ALandscape* GetLandscapeActor() const;

	/** Get the level in which the owning actor resides */
	ULevel* GetLevel() const;

#if WITH_EDITOR
	/** Returns all generated textures and material instances used by this component. */
	LANDSCAPE_API void GetGeneratedTexturesAndMaterialInstances(TArray<UObject*>& OutTexturesAndMaterials) const;
#endif

	/** Gets the landscape proxy actor which owns this component */
	LANDSCAPE_API ALandscapeProxy* GetLandscapeProxy() const;

	/** @return Component section base as FIntPoint */
	LANDSCAPE_API FIntPoint GetSectionBase() const
	{
		return FIntPoint(SectionBaseX, SectionBaseY);
	}

	/** @param InSectionBase new section base for a component */
	LANDSCAPE_API void SetSectionBase(FIntPoint InSectionBase)
	{
		SectionBaseX = InSectionBase.X;
		SectionBaseY = InSectionBase.Y;
	}

	/** @todo document */
	const FGuid& GetLightingGuid() const
	{
#if WITH_EDITORONLY_DATA
		return LightingGuid;
#else
		static const FGuid NullGuid( 0, 0, 0, 0 );
		return NullGuid;
#endif // WITH_EDITORONLY_DATA
	}

	/** @todo document */
	void SetLightingGuid()
	{
#if WITH_EDITORONLY_DATA
		LightingGuid = FGuid::NewGuid();
#endif // WITH_EDITORONLY_DATA
	}

	FGuid GetMapBuildDataId() const
	{
		return MapBuildDataId;
	}

	LANDSCAPE_API const FMeshMapBuildData* GetMeshMapBuildData() const;

#if WITH_EDITOR
	/** Initialize the landscape component */
	LANDSCAPE_API void Init(int32 InBaseX,int32 InBaseY,int32 InComponentSizeQuads, int32 InNumSubsections,int32 InSubsectionSizeQuads);

	/**
	 * Recalculate cached bounds using height values.
	 */
	LANDSCAPE_API void UpdateCachedBounds(bool bInApproximateBounds = false);

	/**
	 * Update the MaterialInstance parameters to match the layer and weightmaps for this component
	 * Creates the MaterialInstance if it doesn't exist.
	 */
	LANDSCAPE_API void UpdateMaterialInstances();
	LANDSCAPE_API void UpdateMaterialInstances(FMaterialUpdateContext& InOutMaterialContext, TArray<class FComponentRecreateRenderStateContext>& InOutRecreateRenderStateContext);

	// Internal implementation of UpdateMaterialInstances, not safe to call directly
	void UpdateMaterialInstances_Internal(FMaterialUpdateContext& Context);

	/** Helper function for UpdateMaterialInstance to get Material without set parameters */
	UMaterialInstanceConstant* GetCombinationMaterial(FMaterialUpdateContext* InMaterialUpdateContext, const TArray<FWeightmapLayerAllocationInfo>& Allocations, int8 InLODIndex, bool bMobile = false) const;
	/**
	 * Generate mipmaps for height and tangent data.
	 * @param HeightmapTextureMipData - array of pointers to the locked mip data.
	 *           This should only include the mips that are generated directly from this component's data
	 *           ie where each subsection has at least 2 vertices.
	* @param ComponentX1 - region of texture to update in component space, MAX_int32 meant end of X component in ALandscape::Import()
	* @param ComponentY1 - region of texture to update in component space, MAX_int32 meant end of Y component in ALandscape::Import()
	* @param ComponentX2 (optional) - region of texture to update in component space
	* @param ComponentY2 (optional) - region of texture to update in component space
	* @param TextureDataInfo - FLandscapeTextureDataInfo pointer, to notify of the mip data region updated.
	 */
	void GenerateHeightmapMips(TArray<FColor*>& HeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FLandscapeTextureDataInfo* TextureDataInfo=nullptr);

	/**
	 * Generate empty mipmaps for weightmap
	 */
	LANDSCAPE_API static void CreateEmptyTextureMips(UTexture2D* Texture, bool bClear = false);

	/**
	 * Generate mipmaps for weightmap
	 * Assumes all weightmaps are unique to this component.
	 * @param WeightmapTextureBaseMipData: array of pointers to each of the weightmaps' base locked mip data.
	 */
	template<typename DataType>

	/** @todo document */
	static void GenerateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, DataType* BaseMipData);

	/** @todo document */
	static void GenerateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, FColor* BaseMipData);

	/**
	 * Update mipmaps for existing weightmap texture
	 */
	template<typename DataType>

	/** @todo document */
	static void UpdateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, TArray<DataType*>& WeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FLandscapeTextureDataInfo* TextureDataInfo=nullptr);

	/** @todo document */
	LANDSCAPE_API static void UpdateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, TArray<FColor*>& WeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FLandscapeTextureDataInfo* TextureDataInfo=nullptr);

	/** @todo document */
	static void UpdateDataMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* Texture, TArray<uint8*>& TextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FLandscapeTextureDataInfo* TextureDataInfo=nullptr);

	/**
	 * Create or updates collision component height data
	 * @param HeightmapTextureMipData: heightmap data
	 * @param ComponentX1, ComponentY1, ComponentX2, ComponentY2: region to update
	 * @param bUpdateBounds: Whether to update bounds from render component.
	 * @param XYOffsetTextureMipData: xy-offset map data
	 * @returns True if CollisionComponent was created in this update.
	 */
	void UpdateCollisionHeightData(const FColor* HeightmapTextureMipData, const FColor* SimpleCollisionHeightmapTextureData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, bool bUpdateBounds=false, const FColor* XYOffsetTextureMipData=nullptr, bool bInUpdateHeightfieldRegion=true);

	/**
	 * Deletes Collision Component
	 */
	void DestroyCollisionData();

	/** Updates collision component height data for the entire component, locking and unlocking heightmap textures
	 */
	void UpdateCollisionData(bool bInUpdateHeightfieldRegion = true);

	/** Cumulates component's dirtied collision region that will need to be updated (used by Layer System)*/
	void UpdateDirtyCollisionHeightData(FIntRect Region);

	/** Clears component's dirtied collision region (used by Layer System)*/
	void ClearDirtyCollisionHeightData();

	/**
	 * Update collision component dominant layer data
	 * @param WeightmapTextureMipData: weightmap data
	 * @param ComponentX1, ComponentY1, ComponentX2, ComponentY2: region to update
	 * @param Whether to update bounds from render component.
	 */
	void UpdateCollisionLayerData(const FColor* const* WeightmapTextureMipData, const FColor* const* const SimpleCollisionWeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32);

	/**
	 * Update collision component dominant layer data for the whole component, locking and unlocking the weightmap textures.
	 */
	LANDSCAPE_API void UpdateCollisionLayerData();

	/** Update physical material render tasks. */
	void UpdatePhysicalMaterialTasks();
	/** Update collision component physical materials from render task results. */
	void UpdateCollisionPhysicalMaterialData(TArray<UPhysicalMaterial*> const& InPhysicalMaterials, TArray<uint8> const& InMaterialIds);

	/**
	 * Create weightmaps for this component for the layers specified in the WeightmapLayerAllocations array
	 */
	LANDSCAPE_API void ReallocateWeightmaps(FLandscapeEditDataInterface* DataInterface = nullptr, bool InCanUseEditingWeightmap = true, bool InSaveToTransactionBuffer = true, bool InInitPlatformDataAsync = false, bool InForceReallocate = false, ALandscapeProxy* InTargetProxy = nullptr, TArray<UTexture2D*>* OutNewCreatedTextures = nullptr);

	/** Returns the component's LandscapeMaterial, or the Component's OverrideLandscapeMaterial if set */
	LANDSCAPE_API UMaterialInterface* GetLandscapeMaterial(int8 InLODIndex = INDEX_NONE) const;

	/** Returns the components's LandscapeHoleMaterial, or the Component's OverrideLandscapeHoleMaterial if set */
	LANDSCAPE_API UMaterialInterface* GetLandscapeHoleMaterial() const;

	/** Returns true if the component has a valid LandscapeHoleMaterial */
	LANDSCAPE_API bool IsLandscapeHoleMaterialValid() const;

	/** Returns true if this component has visibility painted */
	LANDSCAPE_API bool ComponentHasVisibilityPainted() const;

	/**
	 * Generate a key for a component's layer allocations to use with MaterialInstanceConstantMap.
	 */
	static FString GetLayerAllocationKey(const TArray<FWeightmapLayerAllocationInfo>& Allocations, UMaterialInterface* LandscapeMaterial, bool bMobile = false);

	/** @todo document */
	void GetLayerDebugColorKey(int32& R, int32& G, int32& B) const;

	/** @todo document */
	void RemoveInvalidWeightmaps();

	/** @todo document */
	virtual void ExportCustomProperties(FOutputDevice& Out, uint32 Indent) override;

	/** @todo document */
	virtual void ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn) override;

	/** @todo document */
	LANDSCAPE_API void InitHeightmapData(TArray<FColor>& Heights, bool bUpdateCollision);

	/** @todo document */
	LANDSCAPE_API void InitWeightmapData(TArray<ULandscapeLayerInfoObject*>& LayerInfos, TArray<TArray<uint8> >& Weights);

	/** @todo document */
	LANDSCAPE_API float GetLayerWeightAtLocation( const FVector& InLocation, ULandscapeLayerInfoObject* LayerInfo, TArray<uint8>* LayerCache = NULL, bool bUseEditingWeightmap = false);

	/** Extends passed region with this component section size */
	LANDSCAPE_API void GetComponentExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const;

	/** Updates navigation properties to match landscape's master switch */
	void UpdateNavigationRelevance();

	/** Updates the reject navmesh underneath flag in the collision component */
	void UpdateRejectNavmeshUnderneath();
	
	/** Updates the values of component-level properties exposed by the Landscape Actor */
	LANDSCAPE_API void UpdatedSharedPropertiesFromActor();

	LANDSCAPE_API bool IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag InFlag, uint32 InModeMask) const;
	LANDSCAPE_API void ClearUpdateFlagsForModes(uint32 InModeMask);
	LANDSCAPE_API void RequestWeightmapUpdate(bool bUpdateAll = false, bool bUpdateCollision = true);
	LANDSCAPE_API void RequestHeightmapUpdate(bool bUpdateAll = false, bool bUpdateCollision = true);
	LANDSCAPE_API void RequestEditingClientUpdate();
	LANDSCAPE_API void RequestDeferredClientUpdate();
	LANDSCAPE_API uint32 GetLayerUpdateFlagPerMode() const { return LayerUpdateFlagPerMode; }
	LANDSCAPE_API uint32 ComputeWeightmapsHash();
#endif

	friend class FLandscapeComponentSceneProxy;
	friend struct FLandscapeComponentDataInterface;
	friend class ULandscapeLODStreamingProxy;

	void SetLOD(bool bForced, int32 InLODValue);

protected:

#if WITH_EDITOR
	void RecreateCollisionComponent(bool bUseSimpleCollision);
	void UpdateCollisionHeightBuffer(int32 InComponentX1, int32 InComponentY1, int32 InComponentX2, int32 InComponentY2, int32 InCollisionMipLevel, int32 InHeightmapSizeU, int32 InHeightmapSizeV,
		const FColor* const InHeightmapTextureMipData, uint16* CollisionHeightData, uint16* GrassHeightData,
		const FColor* const InXYOffsetTextureMipData, uint16* CollisionXYOffsetData);
	void UpdateDominantLayerBuffer(int32 InComponentX1, int32 InComponentY1, int32 InComponentX2, int32 InComponentY2, int32 InCollisionMipLevel, int32 InWeightmapSizeU, int32 InDataLayerIdx, const TArray<uint8*>& InCollisionDataPtrs, const TArray<ULandscapeLayerInfoObject*>& InLayerInfos, uint8* DominantLayerData);
#endif

	/** Whether the component type supports static lighting. */
	virtual bool SupportsStaticLighting() const override
	{
		return true;
	}
};
