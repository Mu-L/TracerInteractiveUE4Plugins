// Copyright Epic Games, Inc. All Rights Reserved.

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
enum class ERuntimeVirtualTextureMaterialType : uint8;

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
LANDSCAPE_API extern UMaterialInterface* GLandscapeDirtyMaterial;
#endif


/** The uniform shader parameters for a landscape draw call. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeUniformShaderParameters, LANDSCAPE_API)
SHADER_PARAMETER(int32, ComponentBaseX)
SHADER_PARAMETER(int32, ComponentBaseY)
SHADER_PARAMETER(int32, SubsectionSizeVerts)
SHADER_PARAMETER(int32, NumSubsections)
SHADER_PARAMETER(int32, LastLOD)
SHADER_PARAMETER(FVector4, HeightmapUVScaleBias)
SHADER_PARAMETER(FVector4, WeightmapUVScaleBias)
SHADER_PARAMETER(FVector4, LandscapeLightmapScaleBias)
SHADER_PARAMETER(FVector4, SubsectionSizeVertsLayerUVPan)
SHADER_PARAMETER(FVector4, SubsectionOffsetParams)
SHADER_PARAMETER(FVector4, LightmapSubsectionOffsetParams)
	SHADER_PARAMETER(FVector4, BlendableLayerMask)
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

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeSectionLODUniformParameters, )
	SHADER_PARAMETER(FIntPoint, Min)
	SHADER_PARAMETER(FIntPoint, Size)
	SHADER_PARAMETER_SRV(Buffer<float>, SectionLOD)
	SHADER_PARAMETER_SRV(Buffer<float>, SectionLODBias)
	SHADER_PARAMETER_SRV(Buffer<float>, SectionTessellationFalloffC)
	SHADER_PARAMETER_SRV(Buffer<float>, SectionTessellationFalloffK)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeFixedGridUniformShaderParameters, LANDSCAPE_API)
	SHADER_PARAMETER(FVector4, LodValues)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/* Data needed for the landscape vertex factory to set the render state for an individual batch element */
struct FLandscapeBatchElementParams
{
#if RHI_RAYTRACING
	FRHIUniformBuffer* LandscapeVertexFactoryMVFUniformBuffer;
#endif
	const TUniformBuffer<FLandscapeUniformShaderParameters>* LandscapeUniformShaderParametersResource;
	const TArray<TUniformBuffer<FLandscapeFixedGridUniformShaderParameters>>* FixedGridUniformShaderParameters;
	const FLandscapeComponentSceneProxy* SceneProxy;
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
	DECLARE_TYPE_LAYOUT(FLandscapeVertexFactoryPixelShaderParameters, NonVirtual);
public:
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;

	
	
};

/** vertex factory for VTF-heightmap terrain  */
class LANDSCAPE_API FLandscapeVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeVertexFactory);

public:

	FLandscapeVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual ~FLandscapeVertexFactory()
	{
		// can only be destroyed from the render thread
		ReleaseResource();
	}

	struct FDataType
	{
		/** The stream to read the vertex position from. */
		FVertexStreamComponent PositionComponent;
	};

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

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

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};


/** Vertex factory for fixed grid runtime virtual texture lod  */
class LANDSCAPE_API FLandscapeFixedGridVertexFactory : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeFixedGridVertexFactory);

public:
	FLandscapeFixedGridVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
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
class LANDSCAPE_API FLandscapeSharedBuffers : public FRefCountedObject
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
public:
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

	UTexture2D*				HeightmapTexture; // PC : Heightmap, Mobile : Weightmap

protected:

	virtual const ULandscapeComponent* GetLandscapeComponent() const { return nullptr; }

	// Map of currently registered landscape proxies, used to register with our neighbors
	static TMap<FLandscapeKey, TMap<FIntPoint, const FLandscapeNeighborInfo*> > SharedSceneProxyMap;

	// For neighbor lookup
	FLandscapeKey			LandscapeKey;
	FIntPoint				ComponentBase;

	// Pointer to our neighbor's scene proxies in NWES order (nullptr if there is currently no neighbor)
	mutable const FLandscapeNeighborInfo* Neighbors[NEIGHBOR_COUNT];

	
	// Data we need to be able to access about our neighbor
	int8					ForcedLOD;
	int8					LODBias;
	bool					bRegistered;

	friend class FLandscapeComponentSceneProxy;

public:
	FLandscapeNeighborInfo(const UWorld* InWorld, const FGuid& InGuid, const FIntPoint& InComponentBase, UTexture2D* InHeightmapTexture, int8 InForcedLOD, int8 InLODBias)
	: HeightmapTexture(InHeightmapTexture)
	, LandscapeKey(InWorld, InGuid)
	, ComponentBase(InComponentBase)
	, ForcedLOD(InForcedLOD)
	, LODBias(InLODBias)
	, bRegistered(false)
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

	void RegisterNeighbors(FLandscapeComponentSceneProxy* SceneProxy = nullptr);
	void UnregisterNeighbors(FLandscapeComponentSceneProxy* SceneProxy = nullptr);
};


class FNullLandscapeRenderSystemResources : public FRenderResource
{
public:

	FVertexBufferRHIRef SectionLODBuffer;
	FShaderResourceViewRHIRef SectionLODSRV;
	TUniformBufferRef<FLandscapeSectionLODUniformParameters> UniformBuffer;

	virtual void InitRHI() override
	{
		TResourceArray<float> ResourceBuffer;
		ResourceBuffer.Add(0.0f);
		FRHIResourceCreateInfo CreateInfo(&ResourceBuffer);
		SectionLODBuffer = RHICreateVertexBuffer(ResourceBuffer.GetResourceDataSize(), BUF_ShaderResource | BUF_Static, CreateInfo);
		SectionLODSRV = RHICreateShaderResourceView(SectionLODBuffer, sizeof(float), PF_R32_FLOAT);

		FLandscapeSectionLODUniformParameters Parameters;
		Parameters.Size = FIntPoint(1, 1);
		Parameters.SectionLOD = SectionLODSRV;
		Parameters.SectionLODBias = SectionLODSRV;
		Parameters.SectionTessellationFalloffC = SectionLODSRV;
		Parameters.SectionTessellationFalloffK = SectionLODSRV;
		UniformBuffer = TUniformBufferRef<FLandscapeSectionLODUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);
	}

	virtual void ReleaseRHI() override
	{
		SectionLODBuffer.SafeRelease();
		SectionLODSRV.SafeRelease();
		UniformBuffer.SafeRelease();
	}
};

extern TGlobalResource<FNullLandscapeRenderSystemResources> GNullLandscapeRenderSystemResources;

extern RENDERER_API TAutoConsoleVariable<float> CVarStaticMeshLODDistanceScale;

struct FLandscapeRenderSystem
{
	struct LODSettingsComponent
	{
		float LOD0ScreenSizeSquared;
		float LOD1ScreenSizeSquared;
		float LODOnePlusDistributionScalarSquared;
		float LastLODScreenSizeSquared;
		int8 LastLODIndex;
		int8 ForcedLOD;
		int8 DrawCollisionPawnLOD;
		int8 DrawCollisionVisibilityLOD;
	};

	static int8 GetLODFromScreenSize(LODSettingsComponent LODSettings, float InScreenSizeSquared, float InViewLODScale, float& OutFractionalLOD)
	{
		float ScreenSizeSquared = InScreenSizeSquared / InViewLODScale;
		
		if (ScreenSizeSquared <= LODSettings.LastLODScreenSizeSquared)
		{
			OutFractionalLOD = LODSettings.LastLODIndex;
			return LODSettings.LastLODIndex;
		}
		else if (ScreenSizeSquared > LODSettings.LOD1ScreenSizeSquared)
		{
			OutFractionalLOD = (LODSettings.LOD0ScreenSizeSquared - FMath::Min(ScreenSizeSquared, LODSettings.LOD0ScreenSizeSquared)) / (LODSettings.LOD0ScreenSizeSquared - LODSettings.LOD1ScreenSizeSquared);
			return 0;
		}
		else
		{
			// No longer linear fraction, but worth the cache misses
			OutFractionalLOD = 1 + FMath::LogX(LODSettings.LODOnePlusDistributionScalarSquared, LODSettings.LOD1ScreenSizeSquared / ScreenSizeSquared);
			return (int8)OutFractionalLOD;
		}
	}

	int32 NumRegisteredEntities;
	int32 NumEntitiesWithTessellation;

	FIntPoint Min;
	FIntPoint Size;

	struct SystemTessellationFalloffSettings // Global settings on the render system, not as a component of an entity
	{
		bool UseTessellationComponentScreenSizeFalloff;
		float TessellationComponentSquaredScreenSize;
		float TessellationComponentScreenSizeFalloff;
	} TessellationFalloffSettings;

	TArray<LODSettingsComponent> SectionLODSettings;
	TResourceArray<float> SectionLODValues;
	TResourceArray<float> SectionLODBiases;
	TResourceArray<float> SectionTessellationFalloffC;
	TResourceArray<float> SectionTessellationFalloffK;
	TArray<FVector4> SectionOriginAndRadius;
	TArray<FLandscapeComponentSceneProxy*> SceneProxies;
	TArray<uint8> SectionCurrentFirstLODIndices;

	FVertexBufferRHIRef SectionLODBuffer;
	FShaderResourceViewRHIRef SectionLODSRV;
	FVertexBufferRHIRef SectionLODBiasBuffer;
	FShaderResourceViewRHIRef SectionLODBiasSRV;
	FVertexBufferRHIRef SectionTessellationFalloffCBuffer;
	FShaderResourceViewRHIRef SectionTessellationFalloffCSRV;
	FVertexBufferRHIRef SectionTessellationFalloffKBuffer;
	FShaderResourceViewRHIRef SectionTessellationFalloffKSRV;

	TUniformBufferRef<FLandscapeSectionLODUniformParameters> UniformBuffer;

	FCriticalSection CachedValuesCS;
	TMap<const FSceneView*, TResourceArray<float>> CachedSectionLODValues;
	TMap<const FSceneView*, TResourceArray<float>> CachedSectionTessellationFalloffC;
	TMap<const FSceneView*, TResourceArray<float>> CachedSectionTessellationFalloffK;
	const FSceneView* CachedView;

	TMap<const FSceneView*, FGraphEventRef> PerViewParametersTasks;
	FGraphEventRef FetchHeightmapLODBiasesEventRef;

	struct FComputeSectionPerViewParametersTask
	{
		FLandscapeRenderSystem& RenderSystem;
		const FSceneView* ViewPtrAsIdentifier;
		int32 ViewLODOverride;
		float ViewLODDistanceFactor;
		bool ViewEngineShowFlagCollisionPawn;
		bool ViewEngineShowFlagCollisionVisibility;
		FVector ViewOrigin;
		FMatrix ViewProjectionMatrix;

		FComputeSectionPerViewParametersTask(FLandscapeRenderSystem& InRenderSystem, const FSceneView* InView);

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FComputeSectionPerViewParametersTask, STATGROUP_TaskGraphTasks);
		}

		ENamedThreads::Type GetDesiredThread()
		{
			return ENamedThreads::AnyNormalThreadNormalTask;
		}

		static ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::TrackSubsequents;
		}

		void AnyThreadTask()
		{
			RenderSystem.ComputeSectionPerViewParameters(
				ViewPtrAsIdentifier, ViewLODOverride, ViewLODDistanceFactor, 
				ViewEngineShowFlagCollisionPawn, ViewEngineShowFlagCollisionVisibility, 
				ViewOrigin, ViewProjectionMatrix);
		}

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			AnyThreadTask();
		}
	};

	struct FGetSectionLODBiasesTask
	{
		FLandscapeRenderSystem& RenderSystem;

		FGetSectionLODBiasesTask(FLandscapeRenderSystem& InRenderSystem)
			: RenderSystem(InRenderSystem)
		{
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FGetSectionLODBiasesTask, STATGROUP_TaskGraphTasks);
		}

		ENamedThreads::Type GetDesiredThread()
		{
			return ENamedThreads::AnyNormalThreadNormalTask;
		}

		static ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::TrackSubsequents;
		}

		void AnyThreadTask()
		{
			RenderSystem.FetchHeightmapLODBiases();
		}

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			AnyThreadTask();
		}
	};

	FLandscapeRenderSystem()
		: NumRegisteredEntities(0)
		, NumEntitiesWithTessellation(0)
		, Min(MAX_int32, MAX_int32)
		, Size(EForceInit::ForceInitToZero)
		, CachedView(nullptr)
	{
		SectionLODValues.SetAllowCPUAccess(true);
		SectionLODBiases.SetAllowCPUAccess(true);
		SectionTessellationFalloffC.SetAllowCPUAccess(true);
		SectionTessellationFalloffK.SetAllowCPUAccess(true);
	}

	void RegisterEntity(FLandscapeComponentSceneProxy* SceneProxy);

	void UnregisterEntity(FLandscapeComponentSceneProxy* SceneProxy);

	int32 GetComponentLinearIndex(FIntPoint ComponentBase)
	{
		return (ComponentBase.Y - Min.Y) * Size.X + ComponentBase.X - Min.X;
	}
	void ResizeAndMoveTo(FIntPoint NewMin, FIntPoint NewSize);

	void SetSectionLODSettings(FIntPoint ComponentBase, LODSettingsComponent LODSettings)
	{
		SectionLODSettings[GetComponentLinearIndex(ComponentBase)] = LODSettings;
	}

	void SetSectionOriginAndRadius(FIntPoint ComponentBase, FVector4 OriginAndRadius)
	{
		SectionOriginAndRadius[GetComponentLinearIndex(ComponentBase)] = OriginAndRadius;
	}

	void SetSceneProxy(FIntPoint ComponentBase, FLandscapeComponentSceneProxy* SceneProxy)
	{
		SceneProxies[GetComponentLinearIndex(ComponentBase)] = SceneProxy;
	}

	float GetSectionLODValue(FIntPoint ComponentBase)
	{
		return SectionLODValues[GetComponentLinearIndex(ComponentBase)];
	}

	float GetSectionLODBias(FIntPoint ComponentBase)
	{
		return SectionLODBiases[GetComponentLinearIndex(ComponentBase)];
	}

	void ComputeSectionPerViewParameters(
		const FSceneView* ViewPtrAsIdentifier,
		int32 ViewLODOverride,
		float ViewLODDistanceFactor,
		bool bDrawCollisionPawn,
		bool bDrawCollisionCollision,
		FVector ViewOrigin,
		FMatrix ViewProjectionMarix);

	void PrepareView(const FSceneView* View);

	void BeginRenderView(const FSceneView* View);

	void BeginFrame();

	void FetchHeightmapLODBiases();

	void RecreateBuffers(const FSceneView* InView = nullptr);

	void EndFrame();
};

extern TMap<FLandscapeNeighborInfo::FLandscapeKey, FLandscapeRenderSystem*> LandscapeRenderSystems;

//
// FLandscapeMeshProxySceneProxy
//
class FLandscapeMeshProxySceneProxy final : public FStaticMeshSceneProxy
{
	TArray<FLandscapeNeighborInfo> ProxyNeighborInfos;
public:
	SIZE_T GetTypeHash() const override;

	FLandscapeMeshProxySceneProxy(UStaticMeshComponent* InComponent, const FGuid& InGuid, const TArray<FIntPoint>& InProxyComponentBases, int8 InProxyLOD);
	virtual void CreateRenderThreadResources() override;
	virtual void DestroyRenderThreadResources() override;
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

#if RHI_RAYTRACING
	struct FLandscapeSectionRayTracingState
	{
		int8 CurrentLOD;
		float FractionalLOD;
		float HeightmapLODBias;
		uint32 ReferencedTextureRHIHash;

		FRayTracingGeometry Geometry;
		FRWBuffer RayTracingDynamicVertexBuffer;
		FLandscapeVertexFactoryMVFUniformBufferRef UniformBuffer;

		FLandscapeSectionRayTracingState() 
			: CurrentLOD(-1)
			, FractionalLOD(-1000.0f)
			, HeightmapLODBias(-1000.0f)
			, ReferencedTextureRHIHash(0) {}
	};

	TStaticArray<FLandscapeSectionRayTracingState, MAX_SUBSECTION_COUNT> SectionRayTracingStates;
#endif

	friend FLandscapeRenderSystem;

	// Reference counted vertex and index buffer shared among all landscape scene proxies of the same component size
	// Key is the component size and number of subsections.
	// Also being reused by GPULightmass currently to save mem
	static LANDSCAPE_API TMap<uint32, FLandscapeSharedBuffers*> SharedBuffersMap;

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

	FLandscapeRenderSystem::LODSettingsComponent LODSettings;

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

	uint8 BlendableLayerMask;

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

	TArray< TUniformBuffer<FLandscapeFixedGridUniformShaderParameters> > LandscapeFixedGridUniformShaderParameters;

	// Cached versions of these
	FMatrix					WorldToLocal;

protected:
	virtual ~FLandscapeComponentSceneProxy();
	
	virtual const ULandscapeComponent* GetLandscapeComponent() const { return LandscapeComponent; }
	int8 GetLODFromScreenSize(float InScreenSizeSquared, float InViewLODScale) const;

	bool GetMeshElementForVirtualTexture(int32 InLodIndex, ERuntimeVirtualTextureMaterialType MaterialType, UMaterialInterface* InMaterialInterface, FMeshBatch& OutMeshBatch, TArray<FLandscapeBatchElementParams>& OutStaticBatchParamArray) const;
	template<class ArrayType> bool GetStaticMeshElement(int32 LODIndex, bool bForToolMesh, bool bForcedLOD, FMeshBatch& MeshBatch, ArrayType& OutStaticBatchParamArray) const;
	
	virtual void ApplyMeshElementModifier(FMeshBatchElement& InOutMeshElement, int32 InLodIndex) const {}

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
	virtual void DestroyRenderThreadResources() override;
	virtual void OnLevelAddedToWorld() override;
	
	friend class ULandscapeComponent;
	friend class FLandscapeVertexFactoryVertexShaderParameters;
	friend class FLandscapeXYOffsetVertexFactoryVertexShaderParameters;
	friend class FLandscapeVertexFactoryPixelShaderParameters;
	friend struct FLandscapeBatchElementParams;
	friend class FLandscapeVertexFactoryMobileVertexShaderParameters;
	friend class FLandscapeVertexFactoryMobilePixelShaderParameters;
	friend class FLandscapeFixedGridVertexFactoryVertexShaderParameters;
	friend class FLandscapeFixedGridVertexFactoryMobileVertexShaderParameters;

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

	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
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
	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
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
	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		if (ParameterInfo.Name == FName(TEXT("bInverted")))
		{
			*OutValue = bInverted;
			return true;
		}
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
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
	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
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

		for (int32 i = 0; i < UE_ARRAY_COUNT(ColorNames) && i < LayerColors.Num(); i++)
		{
			if (ParameterInfo.Name == ColorNames[i])
			{
				*OutValue = LayerColors[i];
				return true;
			}
		}
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
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
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
};
