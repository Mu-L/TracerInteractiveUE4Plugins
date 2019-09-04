// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRender.h: New terrain rendering
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "Templates/RefCounting.h"
#include "Containers/ArrayView.h"
#include "ShaderParameters.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "VertexFactory.h"
#include "MaterialShared.h"
#include "LandscapeProxy.h"
#include "RendererInterface.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "Engine/MapBuildDataRegistry.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"

// This defines the number of border blocks to surround terrain by when generating lightmaps
#define TERRAIN_PATCH_EXPAND_SCALAR	1

#define LANDSCAPE_LOD_LEVELS 8
#define LANDSCAPE_MAX_SUBSECTION_NUM 2

class FLandscapeComponentSceneProxy;
enum class ERuntimeVirtualTextureMaterialType;

#if WITH_EDITOR
namespace ELandscapeViewMode
{
	enum Type
	{
		Invalid = -1,
		/** Color only */
		Normal = 0,
		EditLayer,
		/** Layer debug only */
		DebugLayer,
		LayerDensity,
		LayerUsage,
		LOD,
		WireframeOnTop,
		LayerContribution
	};
}

extern LANDSCAPE_API int32 GLandscapeViewMode;

namespace ELandscapeEditRenderMode
{
	enum Type
	{
		None = 0x0,
		Gizmo = 0x1,
		SelectRegion = 0x2,
		SelectComponent = 0x4,
		Select = SelectRegion | SelectComponent,
		Mask = 0x8,
		InvertedMask = 0x10, // Should not be overlapped with other bits 
		BitMaskForMask = Mask | InvertedMask,

	};
}

LANDSCAPE_API extern bool GLandscapeEditModeActive;
LANDSCAPE_API extern int32 GLandscapeEditRenderMode;
LANDSCAPE_API extern UMaterialInterface* GLayerDebugColorMaterial;
LANDSCAPE_API extern UMaterialInterface* GSelectionColorMaterial;
LANDSCAPE_API extern UMaterialInterface* GSelectionRegionMaterial;
LANDSCAPE_API extern UMaterialInterface* GMaskRegionMaterial;
LANDSCAPE_API extern UMaterialInterface* GColorMaskRegionMaterial;
LANDSCAPE_API extern UTexture2D* GLandscapeBlackTexture;
LANDSCAPE_API extern UMaterialInterface* GLandscapeLayerUsageMaterial;
#endif


/** The uniform shader parameters for a landscape draw call. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeUniformShaderParameters, LANDSCAPE_API)
/** vertex shader parameters */
SHADER_PARAMETER(FVector4, HeightmapUVScaleBias)
SHADER_PARAMETER(FVector4, WeightmapUVScaleBias)
SHADER_PARAMETER(FVector4, LandscapeLightmapScaleBias)
SHADER_PARAMETER(FVector4, SubsectionSizeVertsLayerUVPan)
SHADER_PARAMETER(FVector4, SubsectionOffsetParams)
SHADER_PARAMETER(FVector4, LightmapSubsectionOffsetParams)
SHADER_PARAMETER(FMatrix, LocalToWorldNoScaling)
SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)
SHADER_PARAMETER_TEXTURE(Texture2D, NormalmapTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, NormalmapTextureSampler)
SHADER_PARAMETER_TEXTURE(Texture2D, XYOffsetmapTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, XYOffsetmapTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeVertexFactoryMVFParameters, LANDSCAPE_API)
	SHADER_PARAMETER(FIntPoint, SubXY)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FLandscapeVertexFactoryMVFParameters> FLandscapeVertexFactoryMVFUniformBufferRef;

/* Data needed for the landscape vertex factory to set the render state for an individual batch element */
struct FLandscapeBatchElementParams
{
#if RHI_RAYTRACING
	FLandscapeVertexFactoryMVFUniformBufferRef LandscapeVertexFactoryMVFUniformBuffer;
#endif
	const TUniformBuffer<FLandscapeUniformShaderParameters>* LandscapeUniformShaderParametersResource;
	const FMatrix* LocalToWorldNoScalingPtr;

	// LOD calculation-related params
	const FLandscapeComponentSceneProxy* SceneProxy;
	int32 SubX;
	int32 SubY;
	int32 CurrentLOD;
};

class FLandscapeElementParamArray : public FOneFrameResource
{
public:
	TArray<FLandscapeBatchElementParams, SceneRenderingAllocator> ElementParams;
};

/** Pixel shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryPixelShaderParameters : public FVertexFactoryShaderParameters
{
public:
	virtual void Bind(const class FShaderParameterMap& ParameterMap) {}
	virtual void Serialize(FArchive& Ar) {}

	virtual void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const override;

	virtual uint32 GetSize() const override
	{
		return sizeof(*this);
	}
};

/** vertex factory for VTF-heightmap terrain  */
class FLandscapeVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeVertexFactory);

public:

	FLandscapeVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual ~FLandscapeVertexFactory()
	{
		// can only be destroyed from the render thread
		ReleaseResource();
	}

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	struct FDataType
	{
		/** The stream to read the vertex position from. */
		FVertexStreamComponent PositionComponent;
	};

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material, const FShaderType* ShaderType)
	{
		// only compile landscape materials for landscape vertex factory
		// The special engine materials must be compiled for the landscape vertex factory because they are used with it for wireframe, etc.
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4) &&
			(Material->IsUsedWithLandscape() || Material->IsSpecialEngineMaterial());
	}

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment);

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FLandscapeVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;

	static bool SupportsTessellationShaders() { return true; }

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		Data = InData;
		UpdateRHI();
	}

	virtual uint64 GetStaticBatchElementVisibility(const FSceneView& InView, const FMeshBatch* InBatch, const void* InViewCustomData = nullptr) const override;

	/** stream component data bound to this vertex factory */
	FDataType Data;
};


/** vertex factory for VTF-heightmap terrain  */
class FLandscapeXYOffsetVertexFactory : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeXYOffsetVertexFactory);

public:
	FLandscapeXYOffsetVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	virtual ~FLandscapeXYOffsetVertexFactory() {}

	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment);
};


/** Vertex factory for fixed grid runtime virtual texture lod  */
class FLandscapeFixedGridVertexFactory : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeFixedGridVertexFactory);

public:
	FLandscapeFixedGridVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment);
	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);
};


struct FLandscapeVertex
{
	float VertexX;
	float VertexY;
	float SubX;
	float SubY;
};

//
// FLandscapeVertexBuffer
//
class FLandscapeVertexBuffer : public FVertexBuffer
{
	ERHIFeatureLevel::Type FeatureLevel;
	int32 NumVertices;
	int32 SubsectionSizeVerts;
	int32 NumSubsections;
public:

	/** Constructor. */
	FLandscapeVertexBuffer(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumVertices, int32 InSubsectionSizeVerts, int32 InNumSubsections)
		: FeatureLevel(InFeatureLevel)
		, NumVertices(InNumVertices)
		, SubsectionSizeVerts(InSubsectionSizeVerts)
		, NumSubsections(InNumSubsections)
	{
		InitResource();
	}

	/** Destructor. */
	virtual ~FLandscapeVertexBuffer()
	{
		ReleaseResource();
	}

	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override;
};


//
// FLandscapeSharedAdjacencyIndexBuffer
//
class FLandscapeSharedAdjacencyIndexBuffer
{
public:
	FLandscapeSharedAdjacencyIndexBuffer(class FLandscapeSharedBuffers* SharedBuffer);
	virtual ~FLandscapeSharedAdjacencyIndexBuffer();

	TArray<FIndexBuffer*> IndexBuffers; // For tessellation
};

//
// FLandscapeSharedBuffers
//
class FLandscapeSharedBuffers : public FRefCountedObject
{
public:
	struct FLandscapeIndexRanges
	{
		int32 MinIndex[LANDSCAPE_MAX_SUBSECTION_NUM][LANDSCAPE_MAX_SUBSECTION_NUM];
		int32 MaxIndex[LANDSCAPE_MAX_SUBSECTION_NUM][LANDSCAPE_MAX_SUBSECTION_NUM];
		int32 MinIndexFull;
		int32 MaxIndexFull;
	};

	int32 NumVertices;
	int32 SharedBuffersKey;
	int32 NumIndexBuffers;
	int32 SubsectionSizeVerts;
	int32 NumSubsections;

	FLandscapeVertexFactory* VertexFactory;
	FLandscapeVertexFactory* FixedGridVertexFactory;
	FLandscapeVertexBuffer* VertexBuffer;
	FIndexBuffer** IndexBuffers;
	FLandscapeIndexRanges* IndexRanges;
	FLandscapeSharedAdjacencyIndexBuffer* AdjacencyIndexBuffers;
	FOccluderIndexArraySP OccluderIndicesSP;
	bool bUse32BitIndices;
#if WITH_EDITOR
	FIndexBuffer* GrassIndexBuffer;
	TArray<int32, TInlineAllocator<8>> GrassIndexMipOffsets;
#endif

#if RHI_RAYTRACING
	TArray<FIndexBuffer*> ZeroOffsetIndexBuffers;
#endif

	FLandscapeSharedBuffers(int32 SharedBuffersKey, int32 SubsectionSizeQuads, int32 NumSubsections, ERHIFeatureLevel::Type FeatureLevel, bool bRequiresAdjacencyInformation, int32 NumOcclusionVertices);

	template <typename INDEX_TYPE>
	void CreateIndexBuffers(ERHIFeatureLevel::Type InFeatureLevel, bool bRequiresAdjacencyInformation);

	void CreateOccluderIndexBuffer(int32 NumOcclderVertices);
	
#if WITH_EDITOR
	template <typename INDEX_TYPE>
	void CreateGrassIndexBuffer();
#endif

	virtual ~FLandscapeSharedBuffers();
};

//
// FLandscapeNeighborInfo
//
class FLandscapeNeighborInfo
{
protected:
	static const int8 NEIGHBOR_COUNT = 4;

	// Key to uniquely identify the landscape to find the correct render proxy map
	class FLandscapeKey
	{
		const UWorld* World;
		const FGuid Guid;
	public:
		FLandscapeKey(const UWorld* InWorld, const FGuid& InGuid)
			: World(InWorld)
			, Guid(InGuid)
		{}

		friend inline uint32 GetTypeHash(const FLandscapeKey& InLandscapeKey)
		{
			return HashCombine(GetTypeHash(InLandscapeKey.World), GetTypeHash(InLandscapeKey.Guid));
		}

		friend bool operator==(const FLandscapeKey& A, const FLandscapeKey& B)
		{
			return A.World == B.World && A.Guid == B.Guid;
		}
	};

	const FLandscapeNeighborInfo* GetNeighbor(int32 Index) const
	{
		if (Index < NEIGHBOR_COUNT)
		{
			return Neighbors[Index];
		}

		return nullptr;
	}

	virtual const ULandscapeComponent* GetLandscapeComponent() const { return nullptr; }

	// Map of currently registered landscape proxies, used to register with our neighbors
	static TMap<FLandscapeKey, TMap<FIntPoint, const FLandscapeNeighborInfo*> > SharedSceneProxyMap;

	// For neighbor lookup
	FLandscapeKey			LandscapeKey;
	FIntPoint				ComponentBase;

	// Pointer to our neighbor's scene proxies in NWES order (nullptr if there is currently no neighbor)
	mutable const FLandscapeNeighborInfo* Neighbors[NEIGHBOR_COUNT];

	
	// Data we need to be able to access about our neighbor
	UTexture2D*				HeightmapTexture; // PC : Heightmap, Mobile : Weightmap
	int8					ForcedLOD;
	int8					LODBias;
	bool					bRegistered;
	int32					PrimitiveCustomDataIndex;

	friend class FLandscapeComponentSceneProxy;

public:
	FLandscapeNeighborInfo(const UWorld* InWorld, const FGuid& InGuid, const FIntPoint& InComponentBase, UTexture2D* InHeightmapTexture, int8 InForcedLOD, int8 InLODBias)
	: LandscapeKey(InWorld, InGuid)
	, ComponentBase(InComponentBase)
	, HeightmapTexture(InHeightmapTexture)
	, ForcedLOD(InForcedLOD)
	, LODBias(InLODBias)
	, bRegistered(false)
	, PrimitiveCustomDataIndex(INDEX_NONE)
	{
		//       -Y       
		//    - - 0 - -   
		//    |       |   
		// -X 1   P   2 +X
		//    |       |   
		//    - - 3 - -   
		//       +Y       

		Neighbors[0] = nullptr;
		Neighbors[1] = nullptr;
		Neighbors[2] = nullptr;
		Neighbors[3] = nullptr;
	}

	void RegisterNeighbors();
	void UnregisterNeighbors();
};

//
// FLandscapeMeshProxySceneProxy
//
class FLandscapeMeshProxySceneProxy final : public FStaticMeshSceneProxy
{
	TArray<FLandscapeNeighborInfo> ProxyNeighborInfos;
public:
	SIZE_T GetTypeHash() const override;

	FLandscapeMeshProxySceneProxy(UStaticMeshComponent* InComponent, const FGuid& InGuid, const TArray<FIntPoint>& InProxyComponentBases, int8 InProxyLOD);
	virtual ~FLandscapeMeshProxySceneProxy();
	virtual void CreateRenderThreadResources() override;
	virtual void OnLevelAddedToWorld() override;
};


//
// FLandscapeComponentSceneProxy
//
class FLandscapeComponentSceneProxy : public FPrimitiveSceneProxy, public FLandscapeNeighborInfo
{
	friend class FLandscapeSharedBuffers;

	SIZE_T GetTypeHash() const override;
	class FLandscapeLCI final : public FLightCacheInterface
	{
	public:
		/** Initialization constructor. */
		FLandscapeLCI(const ULandscapeComponent* InComponent)
			: FLightCacheInterface()
		{
			const FMeshMapBuildData* MapBuildData = InComponent->GetMeshMapBuildData();

			if (MapBuildData)
			{
				SetLightMap(MapBuildData->LightMap);
				SetShadowMap(MapBuildData->ShadowMap);
				SetResourceCluster(MapBuildData->ResourceCluster);
				IrrelevantLights = MapBuildData->IrrelevantLights;
			}
		}

		// FLightCacheInterface
		virtual FLightInteraction GetInteraction(const FLightSceneProxy* LightSceneProxy) const override;

	private:
		TArray<FGuid> IrrelevantLights;
	};

public:
	static const int8 MAX_SUBSECTION_COUNT = 2*2;

	// NOTE: CustomData is added in a FMemStack of the render thread, so no destructor will be called on any of the elements
	struct FViewCustomDataSubSectionLOD
	{
		FViewCustomDataSubSectionLOD()
			: StaticBatchElementIndexToRender(INDEX_NONE)
			, fBatchElementCurrentLOD(-1.0f)
			, BatchElementCurrentLOD(INDEX_NONE)
			, ScreenSizeSquared(-1.0f)
			, ShaderCurrentNeighborLOD(FVector4(-1.0f, -1.0f, -1.0f, -1.0f))
		{}

		int8 StaticBatchElementIndexToRender;
		float fBatchElementCurrentLOD;
		int8 BatchElementCurrentLOD;
		float ScreenSizeSquared;
		FVector4 ShaderCurrentNeighborLOD;
	};

	// NOTE: CustomData is added in a FMemStack of the render thread, so no destructor will be called on any of the elements
	struct FViewCustomDataLOD
	{
		FViewCustomDataLOD()
			: StaticMeshBatchLOD(INDEX_NONE)
			, UseCombinedMeshBatch(true)
			, IsShadowOnly(false)
			, ComponentScreenSize(0.0f)
			, ShaderCurrentLOD(ForceInitToZero)
			, LodBias(ForceInitToZero)
			, LodTessellationParams(ForceInitToZero)
		{}

		int8 StaticMeshBatchLOD;
		bool UseCombinedMeshBatch;
		bool IsShadowOnly;
		float ComponentScreenSize;
		TStaticArray<FViewCustomDataSubSectionLOD, MAX_SUBSECTION_COUNT> SubSections; // We always have at least 1 subsections

		// Shaders pre calculated params
		FVector4 ShaderCurrentLOD;
		FVector4 LodBias;
		FVector4 LodTessellationParams;
	};

#if RHI_RAYTRACING
	struct FLandscapeSectionRayTracingState
	{
		int8 CurrentLOD;
		FVector4 LODBias;
		FVector4 SectionLOD;
		FVector4 CurrentNeighborLOD;
		uint32 ReferencedTextureRHIHash;

		FRayTracingGeometry Geometry;
		FRWBuffer RayTracingDynamicVertexBuffer;
		FLandscapeVertexFactoryMVFUniformBufferRef UniformBuffer;

		FLandscapeSectionRayTracingState() 
			: CurrentLOD(-1)
			, LODBias(-1000.0f, -1000.0f, -1000.0f, -1000.0f)
			, SectionLOD(-1000.0f, -1000.0f, -1000.0f, -1000.0f)
			, CurrentNeighborLOD(-1000.0f, -1000.0f, -1000.0f, -1000.0f)
			, ReferencedTextureRHIHash(0) {}
	};

	TStaticArray<FLandscapeSectionRayTracingState, MAX_SUBSECTION_COUNT> SectionRayTracingStates;
#endif

protected:
	int8						MaxLOD;		// Maximum LOD level, user override possible
	bool						UseTessellationComponentScreenSizeFalloff:1;	// Tell if we should apply a Tessellation falloff
	bool						bRequiresAdjacencyInformation:1;
	int8						NumWeightmapLayerAllocations;
	uint8						StaticLightingLOD;
	float						WeightmapSubsectionOffset;
	TArray<float>				LODScreenRatioSquared;		// Table of valid screen size -> LOD index
	int32						FirstLOD;	// First LOD we have batch elements for
	int32						LastLOD;	// Last LOD we have batch elements for
	int32						FirstVirtualTextureLOD;
	int32						LastVirtualTextureLOD;
	float						ComponentMaxExtend; 		// The max extend value in any axis
	float						ComponentSquaredScreenSizeToUseSubSections; // Size at which we start to draw in sub lod if LOD are different per sub section
	float						MinValidLOD;							// Min LOD Taking into account LODBias
	float						MaxValidLOD;							// Max LOD Taking into account LODBias
	float						TessellationComponentSquaredScreenSize;	// Screen size of the component at which we start to apply tessellation
	float						TessellationComponentScreenSizeFalloff;	// Min Component screen size before we start applying the tessellation falloff

	/** 
	 * Number of subsections within the component in each dimension, this can be 1 or 2.
	 * Subsections exist to improve the speed at which LOD transitions can take place over distance.
	 */
	int32						NumSubsections;
	/** Number of unique heights in the subsection. */
	int32						SubsectionSizeQuads;
	/** Number of heightmap heights in the subsection. This includes the duplicate row at the end. */
	int32						SubsectionSizeVerts;
	/** Size of the component in unique heights. */
	int32						ComponentSizeQuads;
	/** 
	 * ComponentSizeQuads + 1.
	 * Note: in the case of multiple subsections, this is not very useful, as there will be an internal duplicate row of heights in addition to the row at the end.
	 */
	int32						ComponentSizeVerts;
	float						StaticLightingResolution;
	/** Address of the component within the parent Landscape in unique height texels. */
	FIntPoint					SectionBase;

	const ULandscapeComponent* LandscapeComponent;

	FMatrix						LocalToWorldNoScaling;

	TArray<FVector>				SubSectionScreenSizeTestingPosition;	// Precomputed sub section testing position for screen size calculation

	// Storage for static draw list batch params
	TArray<FLandscapeBatchElementParams> StaticBatchParamArray;

#if WITH_EDITOR
	// Precomputed grass rendering MeshBatch and per-LOD params
	FMeshBatch                           GrassMeshBatch;
	TArray<FLandscapeBatchElementParams> GrassBatchParams;
#endif

	FVector4 WeightmapScaleBias;
	TArray<UTexture2D*> WeightmapTextures;

	UTexture2D* VisibilityWeightmapTexture;
	int32 VisibilityWeightmapChannel;

#if WITH_EDITOR
	TArray<FLinearColor> LayerColors;
#endif
	UTexture2D* NormalmapTexture; // PC : Heightmap, Mobile : Weightmap
	UTexture2D* BaseColorForGITexture;
	FVector4 HeightmapScaleBias;
	float HeightmapSubsectionOffsetU;
	float HeightmapSubsectionOffsetV;

	UTexture2D* XYOffsetmapTexture;

	uint32						SharedBuffersKey;
	FLandscapeSharedBuffers*	SharedBuffers;
	FLandscapeVertexFactory*	VertexFactory;
	FLandscapeVertexFactory*	FixedGridVertexFactory;

	/** All available materials for non mobile, including LOD Material, Tessellation generated materials*/
	TArray<UMaterialInterface*> AvailableMaterials;

	/** A cache to know if the material stored in AvailableMaterials[X] has tessellation enabled */
	TBitArray<> MaterialHasTessellationEnabled;

	// FLightCacheInterface
	TUniquePtr<FLandscapeLCI> ComponentLightInfo;

	/** Mapping between LOD and Material Index*/
	TArray<int8> LODIndexToMaterialIndex;
	
	/** Mapping between Material Index to associated generated disabled Tessellation Material*/
	TArray<int8> MaterialIndexToDisabledTessellationMaterial;
	
	/** Mapping between Material Index to Static Mesh Batch */
	TArray<int8> MaterialIndexToStaticMeshBatchLOD;

	/** Material Relevance for each material in AvailableMaterials */
	TArray<FMaterialRelevance> MaterialRelevances;

	// Reference counted vertex and index buffer shared among all landscape scene proxies of the same component size
	// Key is the component size and number of subsections.
	static TMap<uint32, FLandscapeSharedBuffers*> SharedBuffersMap;

#if WITH_EDITORONLY_DATA
	FLandscapeEditToolRenderData EditToolRenderData;
#endif

#if WITH_EDITORONLY_DATA
	ELandscapeLODFalloff::Type LODFalloff_DEPRECATED;
#endif

	// data used in editor or visualisers
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	int32 CollisionMipLevel;
	int32 SimpleCollisionMipLevel;

	FCollisionResponseContainer CollisionResponse;
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** LightMap resolution used for VMI_LightmapDensity */
	int32 LightMapResolution;
#endif

	TUniformBuffer<FLandscapeUniformShaderParameters> LandscapeUniformShaderParameters;

	// Cached versions of these
	FMatrix					WorldToLocal;

protected:
	virtual ~FLandscapeComponentSceneProxy();
	
	virtual const ULandscapeComponent* GetLandscapeComponent() const { return LandscapeComponent; }
	FORCEINLINE void ComputeTessellationFalloffShaderValues(const FViewCustomDataLOD& InLODData, const FMatrix& InViewProjectionMatrix, float& OutC, float& OutK) const;
	bool CanUseMeshBatchForShadowCascade(int8 InLODIndex, float InShadowMapTextureResolution, float InShadowMapCascadeSize) const;
	FORCEINLINE int32 ConvertBatchElementLODToBatchElementIndex(int8 InBatchElementLOD, bool InUseCombinedMeshBatch) const;
	float GetNeighborLOD(const FSceneView& InView, float InBatchElementCurrentLOD, int8 InNeighborIndex, int8 InSubSectionX, int8 InSubSectionY, int8 InCurrentSubSectionIndex) const;
	void CalculateBatchElementLOD(const FSceneView& InView, float InMeshScreenSizeSquared, float InViewLODScale, FViewCustomDataLOD& InOutLODData, bool InForceCombined) const;
	void CalculateLODFromScreenSize(const FSceneView& InView, float InMeshScreenSizeSquared, float InViewLODScale, int32 InSubSectionIndex, FViewCustomDataLOD& InOutLODData) const;
	FORCEINLINE void ComputeStaticBatchIndexToRender(FViewCustomDataLOD& OutLODData, int32 InSubSectionIndex);
	int8 GetLODFromScreenSize(float InScreenSizeSquared, float InViewLODScale) const;
	FORCEINLINE_DEBUGGABLE float ComputeBatchElementCurrentLOD(int32 InSelectedLODIndex, float InComponentScreenSize, float InViewLODScale) const;
	
	FORCEINLINE void GetShaderCurrentNeighborLOD(const FSceneView& InView, float InBatchElementCurrentLOD, int8 InSubSectionX, int8 InSubSectionY, int8 InCurrentSubSectionIndex, FVector4& OutShaderCurrentNeighborLOD) const;
	FORCEINLINE FVector4 GetShaderLODBias() const;
	FORCEINLINE FVector4 GetShaderLODValues(int8 BatchElementCurrentLOD) const;

	bool GetMeshElement(bool UseSeperateBatchForShadow, bool ShadowOnly, bool HasTessellation, int8 InLODIndex, UMaterialInterface* InMaterialInterface, FMeshBatch& OutMeshBatch, TArray<FLandscapeBatchElementParams>& OutStaticBatchParamArray) const;
	bool GetMeshElementForVirtualTexture(int32 InLodIndex, ERuntimeVirtualTextureMaterialType MaterialType, UMaterialInterface* InMaterialInterface, FMeshBatch& OutMeshBatch, TArray<FLandscapeBatchElementParams>& OutStaticBatchParamArray) const;
	void BuildDynamicMeshElement(const FViewCustomDataLOD* InPrimitiveCustomData, bool InToolMesh, bool InHasTessellation, bool InDisableTessellation, FMeshBatch& OutMeshBatch, TArray<FLandscapeBatchElementParams, SceneRenderingAllocator>& OutStaticBatchParamArray) const;

	float GetComponentScreenSize(const class FSceneView* View, const FVector& Origin,  float MaxExtend, float ElementRadius) const;

public:
	// constructor
	FLandscapeComponentSceneProxy(ULandscapeComponent* InComponent);

	// FPrimitiveSceneProxy interface.
	virtual void ApplyWorldOffset(FVector InOffset) override;
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual int32 CollectOccluderElements(FOccluderElementsCollector& Collector) const override;
	virtual uint32 GetMemoryFootprint() const override { return(sizeof(*this) + GetAllocatedSize()); }
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;
	virtual void OnTransformChanged() override;
	virtual void CreateRenderThreadResources() override;
	virtual void OnLevelAddedToWorld() override;
	virtual void* InitViewCustomData(const FSceneView& InView, float InViewLODScale, FMemStackBase& InCustomDataMemStack, bool InIsStaticRelevant, bool InIsShadowOnly, const FLODMask* InVisiblePrimitiveLODMask = nullptr, float InMeshScreenSizeSquared = -1.0f) override;
	void PostInitViewCustomData(const FSceneView& InView, void* InViewCustomData) const;
	virtual bool IsUsingCustomLODRules() const override;
	virtual FLODMask GetCustomLOD(const FSceneView& InView, float InViewLODScale, int32 InForcedLODLevel, float& OutScreenSizeSquared) const override;
	virtual bool IsUsingCustomWholeSceneShadowLODRules() const override;
	virtual FLODMask GetCustomWholeSceneShadowLOD(const FSceneView& InView, float InViewLODScale, int32 InForcedLODLevel, const struct FLODMask& InVisibilePrimitiveLODMask, float InShadowMapTextureResolution, float InShadowMapCascadeSize, int8 InShadowCascadeId, bool InHasSelfShadow) const override;
	
	friend class ULandscapeComponent;
	friend class FLandscapeVertexFactoryVertexShaderParameters;
	friend class FLandscapeXYOffsetVertexFactoryVertexShaderParameters;
	friend class FLandscapeVertexFactoryPixelShaderParameters;
	friend struct FLandscapeBatchElementParams;
	friend class FLandscapeVertexFactoryMobileVertexShaderParameters;
	friend class FLandscapeVertexFactoryMobilePixelShaderParameters;
	friend class FLandscapeFixedGridVertexFactoryVertexShaderParameters;

	// FLandscapeComponentSceneProxy interface.
	uint64 GetStaticBatchElementVisibility(const FSceneView& InView, const FMeshBatch* InBatch, const void* InViewCustomData) const;
#if WITH_EDITOR
	const FMeshBatch& GetGrassMeshBatch() const { return GrassMeshBatch; }
#endif

	// FLandcapeSceneProxy
	void ChangeTessellationComponentScreenSize_RenderThread(float InTessellationComponentScreenSize);
	void ChangeComponentScreenSizeToUseSubSections_RenderThread(float InComponentScreenSizeToUseSubSections);
	void ChangeUseTessellationComponentScreenSizeFalloff_RenderThread(bool InUseTessellationComponentScreenSizeFalloff);
	void ChangeTessellationComponentScreenSizeFalloff_RenderThread(float InTessellationComponentScreenSizeFalloff);

	virtual bool HeightfieldHasPendingStreaming() const override;

	virtual void GetHeightfieldRepresentation(UTexture2D*& OutHeightmapTexture, UTexture2D*& OutDiffuseColorTexture, UTexture2D*& OutVisibilityTexture, FHeightfieldComponentDescription& OutDescription) override;

	virtual void GetLCIs(FLCIArray& LCIs) override;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	virtual int32 GetLightMapResolution() const override { return LightMapResolution; }
#endif

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override final;
	virtual bool IsRayTracingRelevant() const override { return true; }
#endif
};

class FLandscapeDebugMaterialRenderProxy : public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const UTexture2D* RedTexture;
	const UTexture2D* GreenTexture;
	const UTexture2D* BlueTexture;
	const FLinearColor R;
	const FLinearColor G;
	const FLinearColor B;

	/** Initialization constructor. */
	FLandscapeDebugMaterialRenderProxy(const FMaterialRenderProxy* InParent, const UTexture2D* TexR, const UTexture2D* TexG, const UTexture2D* TexB,
		const FLinearColor& InR, const FLinearColor& InG, const FLinearColor& InB) :
		Parent(InParent),
		RedTexture(TexR),
		GreenTexture(TexG),
		BlueTexture(TexB),
		R(InR),
		G(InG),
		B(InB)
	{}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}

	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("Landscape_RedMask")))
		{
			*OutValue = R;
			return true;
		}
		else if (ParameterInfo.Name == FName(TEXT("Landscape_GreenMask")))
		{
			*OutValue = G;
			return true;
		}
		else if (ParameterInfo.Name == FName(TEXT("Landscape_BlueMask")))
		{
			*OutValue = B;
			return true;
		}
		else
		{
			return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
		}
	}
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		// NOTE: These should be returning black textures when NULL. The material will
		// use a white texture if they are.
		if (ParameterInfo.Name == FName(TEXT("Landscape_RedTexture")))
		{
			*OutValue = RedTexture;
			return true;
		}
		else if (ParameterInfo.Name == FName(TEXT("Landscape_GreenTexture")))
		{
			*OutValue = GreenTexture;
			return true;
		}
		else if (ParameterInfo.Name == FName(TEXT("Landscape_BlueTexture")))
		{
			*OutValue = BlueTexture;
			return true;
		}
		else
		{
			return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
		}
	}
};

class FLandscapeSelectMaterialRenderProxy : public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const UTexture2D* SelectTexture;

	/** Initialization constructor. */
	FLandscapeSelectMaterialRenderProxy(const FMaterialRenderProxy* InParent, const UTexture2D* InTexture) :
		Parent(InParent),
		SelectTexture(InTexture)
	{}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}
	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("HighlightColor")))
		{
			*OutValue = FLinearColor(1.f, 0.5f, 0.5f);
			return true;
		}
		else
		{
			return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
		}
	}
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("SelectedData")))
		{
			*OutValue = SelectTexture;
			return true;
		}
		else
		{
			return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
		}
	}
};

class FLandscapeMaskMaterialRenderProxy : public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const UTexture2D* SelectTexture;
	const bool bInverted;

	/** Initialization constructor. */
	FLandscapeMaskMaterialRenderProxy(const FMaterialRenderProxy* InParent, const UTexture2D* InTexture, const bool InbInverted) :
		Parent(InParent),
		SelectTexture(InTexture),
		bInverted(InbInverted)
	{}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}
	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("bInverted")))
		{
			*OutValue = bInverted;
			return true;
		}
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("SelectedData")))
		{
			*OutValue = SelectTexture;
			return true;
		}
		else
		{
			return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
		}
	}
};

class FLandscapeLayerUsageRenderProxy : public FMaterialRenderProxy
{
	const FMaterialRenderProxy* const Parent;

	int32 ComponentSizeVerts;
	TArray<FLinearColor> LayerColors;
	float Rotation;
public:
	FLandscapeLayerUsageRenderProxy(const FMaterialRenderProxy* InParent, int32 InComponentSizeVerts, const TArray<FLinearColor>& InLayerColors, float InRotation)
	: Parent(InParent)
	, ComponentSizeVerts(InComponentSizeVerts)
	, LayerColors(InLayerColors)
	, Rotation(InRotation)
	{}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}
	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		static FName ColorNames[] =
		{
			FName(TEXT("Color0")),
			FName(TEXT("Color1")),
			FName(TEXT("Color2")),
			FName(TEXT("Color3")),
			FName(TEXT("Color4")),
			FName(TEXT("Color5")),
			FName(TEXT("Color6")),
			FName(TEXT("Color7")),
			FName(TEXT("Color8")),
			FName(TEXT("Color9")),
			FName(TEXT("Color10")),
			FName(TEXT("Color11")),
			FName(TEXT("Color12")),
			FName(TEXT("Color13")),
			FName(TEXT("Color14")),
			FName(TEXT("Color15"))
		};

		for (int32 i = 0; i < ARRAY_COUNT(ColorNames) && i < LayerColors.Num(); i++)
		{
			if (ParameterInfo.Name == ColorNames[i])
			{
				*OutValue = LayerColors[i];
				return true;
			}
		}
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("Rotation")))
		{
			*OutValue = Rotation;
			return true;
		}
		if (ParameterInfo.Name == FName(TEXT("NumStripes")))
		{
			*OutValue = LayerColors.Num();
			return true;
		}
		if (ParameterInfo.Name == FName(TEXT("ComponentSizeVerts")))
		{
			*OutValue = ComponentSizeVerts;
			return true;
		}		
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
};
