// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Containers/ArrayView.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "RenderResource.h"
#include "Templates/ScopedPointer.h"
#include "UniformBuffer.h"
#include "GlobalDistanceFieldParameters.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "BatchedElements.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "ScenePrivateBase.h"
#include "PrimitiveSceneInfo.h"
#include "GlobalShader.h"
#include "PrimitiveViewRelevance.h"
#include "DistortionRendering.h"
#include "CustomDepthRendering.h"
#include "HeightfieldLighting.h"
#include "GlobalDistanceFieldParameters.h"
#include "Templates/UniquePtr.h"

class FScene;
class FSceneViewState;
class FViewInfo;
struct FILCUpdatePrimTaskData;

template<typename ShaderMetaType> class TShaderMap;

// Forward declarations.
class FPostprocessContext;
struct FILCUpdatePrimTaskData;

DECLARE_STATS_GROUP(TEXT("Command List Markers"), STATGROUP_CommandListMarkers, STATCAT_Advanced);


/** Mobile only. Information used to determine whether static meshes will be rendered with CSM shaders or not. */
class FMobileCSMVisibilityInfo
{
public:
	/** true if there are any primitives affected by CSM subjects */
	uint32 bMobileDynamicCSMInUse : 1;

	/** Visibility lists for static meshes that will use expensive CSM shaders. */
	FSceneBitArray MobilePrimitiveCSMReceiverVisibilityMap;
	FSceneBitArray MobileCSMStaticMeshVisibilityMap;
	TArray<uint64, SceneRenderingAllocator> MobileCSMStaticBatchVisibility;

	/** Visibility lists for static meshes that will use the non CSM shaders. */
	FSceneBitArray MobileNonCSMStaticMeshVisibilityMap;
	TArray<uint64, SceneRenderingAllocator> MobileNonCSMStaticBatchVisibility;

	/** Initialization constructor. */
	FMobileCSMVisibilityInfo() : bMobileDynamicCSMInUse(false)
	{}
};

/** Stores a list of CSM shadow casters. Used by mobile renderer for culling primitives receiving static + CSM shadows. */
class FMobileCSMSubjectPrimitives
{
public:
	/** Adds a subject primitive */
	void AddSubjectPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo, int32 PrimitiveId)
	{
		checkSlow(PrimitiveSceneInfo->GetIndex() == PrimitiveId);
		const int32 PrimitiveIndex = PrimitiveSceneInfo->GetIndex();
		if (!ShadowSubjectPrimitivesEncountered[PrimitiveId])
		{
			ShadowSubjectPrimitives.Add(PrimitiveSceneInfo);
			ShadowSubjectPrimitivesEncountered[PrimitiveId] = true;
		}
	}

	/** Returns the list of subject primitives */
	const TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator>& GetShadowSubjectPrimitives() const
	{
		return ShadowSubjectPrimitives;
	}

	/** Used to initialize the ShadowSubjectPrimitivesEncountered bit array
	  * to prevent shadow primitives being added more than once. */
	void InitShadowSubjectPrimitives(int32 PrimitiveCount)
	{
		ShadowSubjectPrimitivesEncountered.Init(false, PrimitiveCount);
	}

protected:
	/** List of this light's shadow subject primitives. */
	FSceneBitArray ShadowSubjectPrimitivesEncountered;
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowSubjectPrimitives;
};

/** Information about a visible light which is specific to the view it's visible in. */
class FVisibleLightViewInfo
{
public:

	/** The dynamic primitives which are both visible and affected by this light. */
	TArray<FPrimitiveSceneInfo*,SceneRenderingAllocator> VisibleDynamicLitPrimitives;
	
	/** Whether each shadow in the corresponding FVisibleLightInfo::AllProjectedShadows array is visible. */
	FSceneBitArray ProjectedShadowVisibilityMap;

	/** The view relevance of each shadow in the corresponding FVisibleLightInfo::AllProjectedShadows array. */
	TArray<FPrimitiveViewRelevance,SceneRenderingAllocator> ProjectedShadowViewRelevanceMap;

	/** true if this light in the view frustum (dir/sky lights always are). */
	uint32 bInViewFrustum : 1;

	/** List of CSM shadow casters. Used by mobile renderer for culling primitives receiving static + CSM shadows */
	FMobileCSMSubjectPrimitives MobileCSMSubjectPrimitives;

	/** Initialization constructor. */
	FVisibleLightViewInfo()
	:	bInViewFrustum(false)
	{}
};

/** Information about a visible light which isn't view-specific. */
class FVisibleLightInfo
{
public:

	/** Projected shadows allocated on the scene rendering mem stack. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> MemStackProjectedShadows;

	/** All visible projected shadows, output of shadow setup.  Not all of these will be rendered. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> AllProjectedShadows;

	/** Shadows to project for each feature that needs special handling. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> ShadowsToProject;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> CapsuleShadowsToProject;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> RSMsToProject;

	/** All visible projected preshdows.  These are not allocated on the mem stack so they are refcounted. */
	TArray<TRefCountPtr<FProjectedShadowInfo>,SceneRenderingAllocator> ProjectedPreShadows;

	/** A list of per-object shadows that were occluded. We need to track these so we can issue occlusion queries for them. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> OccludedPerObjectShadows;
};

// enum instead of bool to get better visibility when we pass around multiple bools, also allows for easier extensions
namespace ETranslucencyPass
{
	enum Type
	{
		TPT_StandardTranslucency,
		TPT_TranslucencyAfterDOF,

		/** Drawing all translucency, regardless of separate or standard.  Used when drawing translucency outside of the main renderer, eg FRendererModule::DrawTile. */
		TPT_AllTranslucency,
		TPT_MAX
	};
};

// Stores the primitive count of each translucency pass (redundant, could be computed after sorting but this way we touch less memory)
struct FTranslucenyPrimCount
{
private:
	uint32 Count[ETranslucencyPass::TPT_MAX];
	bool UseSceneColorCopyPerPass[ETranslucencyPass::TPT_MAX];
	bool DisableOffscreenRenderingPerPass[ETranslucencyPass::TPT_MAX];

public:
	// constructor
	FTranslucenyPrimCount()
	{
		for(uint32 i = 0; i < ETranslucencyPass::TPT_MAX; ++i)
		{
			Count[i] = 0;
			UseSceneColorCopyPerPass[i] = false;
			DisableOffscreenRenderingPerPass[i] = false;
		}
	}

	// interface similar to TArray but here we only store the count of Prims per pass
	void Append(const FTranslucenyPrimCount& InSrc)
	{
		for(uint32 i = 0; i < ETranslucencyPass::TPT_MAX; ++i)
		{
			Count[i] += InSrc.Count[i];
			UseSceneColorCopyPerPass[i] |= InSrc.UseSceneColorCopyPerPass[i];
			DisableOffscreenRenderingPerPass[i] |= InSrc.DisableOffscreenRenderingPerPass[i];
		}
	}

	// interface similar to TArray but here we only store the count of Prims per pass
	void Add(ETranslucencyPass::Type InPass, bool bUseSceneColorCopy, bool bDisableOffscreenRendering)
	{
		++Count[InPass];
		UseSceneColorCopyPerPass[InPass] |= bUseSceneColorCopy;
		DisableOffscreenRenderingPerPass[InPass] |= bDisableOffscreenRendering;
	}

	// @return range in SortedPrims[] after sorting
	FInt32Range GetPassRange(ETranslucencyPass::Type InPass) const
	{
		checkSlow(InPass < ETranslucencyPass::TPT_MAX);

		// can be optimized (if needed)

		// inclusive
		int32 Start = 0;

		uint32 i = 0;

		for(; i < (uint32)InPass; ++i)
		{
			Start += Count[i];
		}

		// exclusive
		int32 End = Start + Count[i];
		
		return FInt32Range(Start, End);
	}

	int32 Num(ETranslucencyPass::Type InPass) const
	{
		return Count[InPass];
	}

	bool UseSceneColorCopy(ETranslucencyPass::Type InPass) const
	{
		return UseSceneColorCopyPerPass[InPass];
	}

	bool DisableOffscreenRendering(ETranslucencyPass::Type InPass) const
	{
		return DisableOffscreenRenderingPerPass[InPass];
	}
};


/** 
* Set of sorted scene prims  
*/
template <class TKey>
class FSortedPrimSet
{
public:
	// contains a scene prim and its sort key
	struct FSortedPrim
	{
		// Default constructor
		FSortedPrim() {}

		FSortedPrim(FPrimitiveSceneInfo* InPrimitiveSceneInfo, const TKey InSortKey)
			:	PrimitiveSceneInfo(InPrimitiveSceneInfo)
			,	SortKey(InSortKey)
		{
		}

		FORCEINLINE bool operator<( const FSortedPrim& rhs ) const
		{
			return SortKey < rhs.SortKey;
		}

		//
		FPrimitiveSceneInfo* PrimitiveSceneInfo;
		//
		TKey SortKey;
	};

	/**
	* Sort any primitives that were added to the set back-to-front
	*/
	void SortPrimitives()
	{
		Prims.Sort();
	}

	/** 
	* @return number of prims to render
	*/
	int32 NumPrims() const
	{
		return Prims.Num();
	}

	/** list of primitives, sorted after calling Sort() */
	TArray<FSortedPrim, SceneRenderingAllocator> Prims;
};

template <> struct TIsPODType<FSortedPrimSet<uint32>::FSortedPrim> { enum { Value = true }; };

class FMeshDecalPrimSet : public FSortedPrimSet<uint32>
{
public:
	typedef FSortedPrimSet<uint32>::FSortedPrim KeyType;

	static KeyType GenerateKey(FPrimitiveSceneInfo* PrimitiveSceneInfo, int16 InSortPriority)
	{
		return KeyType(PrimitiveSceneInfo, (uint32)(InSortPriority - SHRT_MIN));
	}
};

/** 
* Set of sorted translucent scene prims  
*/
class FTranslucentPrimSet
{
public:
	/** contains a scene prim and its sort key */
	struct FTranslucentSortedPrim
	{
		/** Default constructor. */
		FTranslucentSortedPrim() {}

		// @param InPass (first we sort by this)
		// @param InSortPriority SHRT_MIN .. SHRT_MAX (then we sort by this)
		// @param InSortKey from UPrimitiveComponent::TranslucencySortPriority e.g. SortByDistance/SortAlongAxis (then by this)
		FTranslucentSortedPrim(FPrimitiveSceneInfo* InPrimitiveSceneInfo, ETranslucencyPass::Type InPass, int16 InSortPriority, float InSortKey)
			:	PrimitiveSceneInfo(InPrimitiveSceneInfo)
			,	SortKey(InSortKey)
		{
			SetSortOrder(InPass, InSortPriority);
		}

		void SetSortOrder(ETranslucencyPass::Type InPass, int16 InSortPriority)
		{
			uint32 UpperShort = (uint32)InPass;
			// 0 .. 0xffff
			int32 SortPriorityWithoutSign = (int32)InSortPriority - (int32)SHRT_MIN;
			uint32 LowerShort = SortPriorityWithoutSign;

			check(LowerShort <= 0xffff);

			// top 8 bits are currently unused
			SortOrder = (UpperShort << 16) | LowerShort;
		}

		//
		FPrimitiveSceneInfo* PrimitiveSceneInfo;
		// single 32bit sort order containing Pass and SortPriority (first we sort by this)
		uint32 SortOrder;
		// from UPrimitiveComponent::TranslucencySortPriority (then by this)
		float SortKey;
	};

	/** 
	* Iterate over the sorted list of prims and draw them
	* @param View - current view used to draw items
	* @param PhaseSortedPrimitives - array with the primitives we want to draw
	* @param TranslucenyPassType
	*/
	void DrawPrimitives(FRHICommandListImmediate& RHICmdList, const class FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, class FDeferredShadingSceneRenderer& Renderer, ETranslucencyPass::Type TranslucenyPassType) const;

	/**
	* Iterate over the sorted list of prims and draw them
	* @param View - current view used to draw items
	* @param PhaseSortedPrimitives - array with the primitives we want to draw
	* @param TranslucenyPassType
	* @param FirstPrimIdx, range of elements to render (included), index into SortedPrims[] after sorting
	* @param LastPrimIdx, range of elements to render (included), index into SortedPrims[] after sorting
	*/
	void DrawPrimitivesParallel(FRHICommandList& RHICmdList, const class FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, class FDeferredShadingSceneRenderer& Renderer, ETranslucencyPass::Type TranslucenyPassType, int32 FirstPrimIdx, int32 LastPrimIdx) const;

	/**
	* Draw a single primitive...this is used when we are rendering in parallel and we need to handlke a translucent shadow
	* @param View - current view used to draw items
	* @param PhaseSortedPrimitives - array with the primitives we want to draw
	* @param TranslucenyPassType
	* @param PrimIdx in SortedPrims[]
	*/
	void DrawAPrimitive(FRHICommandList& RHICmdList, const class FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, class FDeferredShadingSceneRenderer& Renderer, ETranslucencyPass::Type TranslucenyPassType, int32 PrimIdx) const;

	/** 
	* Draw all the primitives in this set for the mobile pipeline. 
	*/
	template <class TDrawingPolicyFactory>
	void DrawPrimitivesForMobile(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, typename TDrawingPolicyFactory::ContextType& DrawingContext) const;

	/**
	* Insert a primitive to the translucency rendering list[s]
	*/
	
	static void PlaceScenePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo, const FPrimitiveViewRelevance& ViewRelevance,
		FTranslucentSortedPrim* InArrayStart, int32& InOutArrayNum, FTranslucenyPrimCount& OutCount);

	/**
	* Sort any primitives that were added to the set back-to-front
	*/
	void SortPrimitives();

	/** 
	* @return number of prims to render
	*/
	int32 NumPrims() const
	{
		return SortedPrims.Num();
	}

	/**
	* Adds primitives originally created with PlaceScenePrimitive
	*/
	void AppendScenePrimitives(FTranslucentSortedPrim* Elements, int32 Num, const FTranslucenyPrimCount& TranslucentPrimitiveCountPerPass);

	// belongs to SortedPrims
	FTranslucenyPrimCount SortedPrimsNum;

private:

	/** sortkey compare class */
	struct FCompareFTranslucentSortedPrim
	{
		FORCEINLINE bool operator()( const FTranslucentSortedPrim& A, const FTranslucentSortedPrim& B ) const
		{
			// If priorities are equal sort normally from back to front
			// otherwise lower sort priorities should render first
			return ( A.SortOrder == B.SortOrder ) ? ( B.SortKey < A.SortKey ) : ( A.SortOrder < B.SortOrder );
		}
	};

	/** list of translucent primitives, sorted after calling Sort() */
	TArray<FTranslucentSortedPrim,SceneRenderingAllocator> SortedPrims;


	/** Renders a single primitive for the deferred shading pipeline. */
	void RenderPrimitive(FRHICommandList& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, FPrimitiveSceneInfo* PrimitiveSceneInfo, const FPrimitiveViewRelevance& ViewRelevance, const FProjectedShadowInfo* TranslucentSelfShadow, ETranslucencyPass::Type TranslucenyPassType) const;
};

template <> struct TIsPODType<FTranslucentPrimSet::FTranslucentSortedPrim> { enum { Value = true }; };

/** A batched occlusion primitive. */
struct FOcclusionPrimitive
{
	FVector Center;
	FVector Extent;
};

/**
 * Combines consecutive primitives which use the same occlusion query into a single DrawIndexedPrimitive call.
 */
class FOcclusionQueryBatcher
{
public:

	/** The maximum number of consecutive previously occluded primitives which will be combined into a single occlusion query. */
	enum { OccludedPrimitiveQueryBatchSize = 16 };

	/** Initialization constructor. */
	FOcclusionQueryBatcher(class FSceneViewState* ViewState, uint32 InMaxBatchedPrimitives);

	/** Destructor. */
	~FOcclusionQueryBatcher();

	/** @returns True if the batcher has any outstanding batches, otherwise false. */
	bool HasBatches(void) const { return (NumBatchedPrimitives > 0); }

	/** Renders the current batch and resets the batch state. */
	void Flush(FRHICommandList& RHICmdList);

	/**
	 * Batches a primitive's occlusion query for rendering.
	 * @param Bounds - The primitive's bounds.
	 */
	FRenderQueryRHIParamRef BatchPrimitive(const FVector& BoundsOrigin, const FVector& BoundsBoxExtent);
	inline int32 GetNumBatchOcclusionQueries() const
	{
		return BatchOcclusionQueries.Num();
	}

private:

	struct FOcclusionBatch
	{
		FRenderQueryRHIRef Query;
		FGlobalDynamicVertexBuffer::FAllocation VertexAllocation;
	};

	/** The pending batches. */
	TArray<FOcclusionBatch,SceneRenderingAllocator> BatchOcclusionQueries;

	/** The batch new primitives are being added to. */
	FOcclusionBatch* CurrentBatchOcclusionQuery;

	/** The maximum number of primitives in a batch. */
	const uint32 MaxBatchedPrimitives;

	/** The number of primitives in the current batch. */
	uint32 NumBatchedPrimitives;

	/** The pool to allocate occlusion queries from. */
	class FRenderQueryPool* OcclusionQueryPool;
};

class FHZBOcclusionTester : public FRenderResource
{
public:
					FHZBOcclusionTester();
					~FHZBOcclusionTester() {}

	// FRenderResource interface
					virtual void	InitDynamicRHI() override;
					virtual void	ReleaseDynamicRHI() override;
	
	uint32			GetNum() const { return Primitives.Num(); }

	uint32			AddBounds( const FVector& BoundsOrigin, const FVector& BoundsExtent );
	void			Submit(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	void			MapResults(FRHICommandListImmediate& RHICmdList);
	void			UnmapResults(FRHICommandListImmediate& RHICmdList);
	bool			IsVisible( uint32 Index ) const;

	bool IsValidFrame(uint32 FrameNumber) const;

	void SetValidFrameNumber(uint32 FrameNumber);

private:
	enum { SizeX = 256 };
	enum { SizeY = 256 };
	enum { FrameNumberMask = 0x7fffffff };
	enum { InvalidFrameNumber = 0xffffffff };

	TArray< FOcclusionPrimitive, SceneRenderingAllocator >	Primitives;

	TRefCountPtr<IPooledRenderTarget>	ResultsTextureCPU;
	const uint8*						ResultsBuffer;


	bool IsInvalidFrame() const;

	// set ValidFrameNumber to a number that cannot be set by SetValidFrameNumber so IsValidFrame will return false for any frame number
	void SetInvalidFrameNumber();

	uint32 ValidFrameNumber;
};

DECLARE_STATS_GROUP(TEXT("Parallel Command List Markers"), STATGROUP_ParallelCommandListMarkers, STATCAT_Advanced);

class FParallelCommandListSet
{
public:
	const FViewInfo& View;
	const FSceneRenderer* SceneRenderer;
	FDrawingPolicyRenderState DrawRenderState;
	FRHICommandListImmediate& ParentCmdList;
	const FRHIGPUMask GPUMask; // Copy of the Parent GPUMask at creation (since it could change).
	FSceneRenderTargets* Snapshot;
	TStatId	ExecuteStat;
	int32 Width;
	int32 NumAlloc;
	int32 MinDrawsPerCommandList;
	// see r.RHICmdBalanceParallelLists
	bool bBalanceCommands;
	// see r.RHICmdSpewParallelListBalance
	bool bSpewBalance;
	bool bBalanceCommandsWithLastFrame;
public:
	TArray<FRHICommandList*,SceneRenderingAllocator> CommandLists;
	TArray<FGraphEventRef,SceneRenderingAllocator> Events;
	// number of draws in this commandlist if known, -1 if not known. Overestimates are better than nothing.
	TArray<int32,SceneRenderingAllocator> NumDrawsIfKnown;
protected:
	//this must be called by deriving classes virtual destructor because it calls the virtual SetStateOnCommandList.
	//C++ will not do dynamic dispatch of virtual calls from destructors so we can't call it in the base class.
	void Dispatch(bool bHighPriority = false);
	FRHICommandList* AllocCommandList();
	bool bParallelExecute;
	bool bCreateSceneContext;
public:
	FParallelCommandListSet(
		TStatId InExecuteStat, 
		const FViewInfo& InView, 
		const FSceneRenderer* InSceneRenderer, 
		FRHICommandListImmediate& InParentCmdList, 
		bool bInParallelExecute, 
		bool bInCreateSceneContext, 
		const FDrawingPolicyRenderState& InDrawRenderState);

	virtual ~FParallelCommandListSet();
	int32 NumParallelCommandLists() const
	{
		return CommandLists.Num();
	}
	FRHICommandList* NewParallelCommandList();
	FORCEINLINE FGraphEventArray* GetPrereqs()
	{
		return nullptr;
	}
	void AddParallelCommandList(FRHICommandList* CmdList, FGraphEventRef& CompletionEvent, int32 InNumDrawsIfKnown = -1);	

	virtual void SetStateOnCommandList(FRHICommandList& CmdList)
	{
	}
	static void WaitForTasks();
private:
	void WaitForTasksInternal();
};

enum EVolumeUpdateType
{
	VUT_MeshDistanceFields = 1,
	VUT_Heightfields = 2,
	VUT_All = VUT_MeshDistanceFields | VUT_Heightfields
};

class FVolumeUpdateRegion
{
public:

	FVolumeUpdateRegion() :
		UpdateType(VUT_All)
	{}

	/** World space bounds. */
	FBox Bounds;

	/** Number of texels in each dimension to update. */
	FIntVector CellsSize;

	EVolumeUpdateType UpdateType;
};

class FGlobalDistanceFieldClipmap
{
public:
	/** World space bounds. */
	FBox Bounds;

	/** Offset applied to UVs so that only new or dirty areas of the volume texture have to be updated. */
	FVector ScrollOffset;

	/** Regions in the volume texture to update. */
	TArray<FVolumeUpdateRegion, TInlineAllocator<3> > UpdateRegions;

	/** Volume texture for this clipmap. */
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
};

class FGlobalDistanceFieldInfo
{
public:

	bool bInitialized;
	TArray<FGlobalDistanceFieldClipmap> MostlyStaticClipmaps;
	TArray<FGlobalDistanceFieldClipmap> Clipmaps;
	FGlobalDistanceFieldParameterData ParameterData;

	void UpdateParameterData(float MaxOcclusionDistance);

	FGlobalDistanceFieldInfo() :
		bInitialized(false)
	{}
};

const int32 GMaxForwardShadowCascades = 4;

#define FORWARD_GLOBAL_LIGHT_DATA_UNIFORM_BUFFER_MEMBER_TABLE \
	UNIFORM_MEMBER(uint32,NumLocalLights) \
	UNIFORM_MEMBER(uint32, NumReflectionCaptures) \
	UNIFORM_MEMBER(uint32, HasDirectionalLight) \
	UNIFORM_MEMBER(uint32, NumGridCells) \
	UNIFORM_MEMBER(FIntVector, CulledGridSize) \
	UNIFORM_MEMBER(uint32, MaxCulledLightsPerCell) \
	UNIFORM_MEMBER(uint32, LightGridPixelSizeShift) \
	UNIFORM_MEMBER(FVector, LightGridZParams) \
	UNIFORM_MEMBER(FVector, DirectionalLightDirection) \
	UNIFORM_MEMBER(FVector, DirectionalLightColor) \
	UNIFORM_MEMBER(float, DirectionalLightVolumetricScatteringIntensity) \
	UNIFORM_MEMBER(uint32, DirectionalLightShadowMapChannelMask) \
	UNIFORM_MEMBER(FVector2D, DirectionalLightDistanceFadeMAD) \
	UNIFORM_MEMBER(uint32, NumDirectionalLightCascades) \
	UNIFORM_MEMBER(FVector4, CascadeEndDepths) \
	UNIFORM_MEMBER_ARRAY(FMatrix, DirectionalLightWorldToShadowMatrix, [GMaxForwardShadowCascades]) \
	UNIFORM_MEMBER_ARRAY(FVector4, DirectionalLightShadowmapMinMax, [GMaxForwardShadowCascades]) \
	UNIFORM_MEMBER(FVector4, DirectionalLightShadowmapAtlasBufferSize) \
	UNIFORM_MEMBER(float, DirectionalLightDepthBias) \
	UNIFORM_MEMBER(uint32, DirectionalLightUseStaticShadowing) \
	UNIFORM_MEMBER(FVector4, DirectionalLightStaticShadowBufferSize) \
	UNIFORM_MEMBER(FMatrix, DirectionalLightWorldToStaticShadow) \
	UNIFORM_MEMBER_TEXTURE(Texture2D, DirectionalLightShadowmapAtlas) \
	UNIFORM_MEMBER_SAMPLER(SamplerState, ShadowmapSampler) \
	UNIFORM_MEMBER_TEXTURE(Texture2D, DirectionalLightStaticShadowmap) \
	UNIFORM_MEMBER_SAMPLER(SamplerState, StaticShadowmapSampler) \
	UNIFORM_MEMBER_SRV(StrongTypedBuffer<float4>, ForwardLocalLightBuffer) \
	UNIFORM_MEMBER_SRV(StrongTypedBuffer<uint>, NumCulledLightsGrid) \
	UNIFORM_MEMBER_SRV(StrongTypedBuffer<uint>, CulledLightDataGrid) 

BEGIN_UNIFORM_BUFFER_STRUCT_WITH_CONSTRUCTOR(FForwardLightData,)
	FORWARD_GLOBAL_LIGHT_DATA_UNIFORM_BUFFER_MEMBER_TABLE
END_UNIFORM_BUFFER_STRUCT(FForwardLightData)

class FForwardLightingViewResources
{
public:
	FForwardLightData ForwardLightData;
	TUniformBufferRef<FForwardLightData> ForwardLightDataUniformBuffer;
	FDynamicReadBuffer ForwardLocalLightBuffer;
	FRWBuffer NumCulledLightsGrid;
	FRWBuffer CulledLightDataGrid;

	void Release()
	{
		ForwardLightDataUniformBuffer.SafeRelease();
		ForwardLocalLightBuffer.Release();
		NumCulledLightsGrid.Release();
		CulledLightDataGrid.Release();
	}
};

class FForwardLightingCullingResources
{
public:
	FRWBuffer NextCulledLightLink;
	FRWBuffer StartOffsetGrid;
	FRWBuffer CulledLightLinks;
	FRWBuffer NextCulledLightData;

	void Release()
	{
		NextCulledLightLink.Release();
		StartOffsetGrid.Release();
		CulledLightLinks.Release();
		NextCulledLightData.Release();
	}
};

BEGIN_UNIFORM_BUFFER_STRUCT_WITH_CONSTRUCTOR(FVolumetricFogGlobalData,) 
	UNIFORM_MEMBER(FIntVector, GridSizeInt)
	UNIFORM_MEMBER(FVector, GridSize)
	UNIFORM_MEMBER(uint32, GridPixelSizeShift)
	UNIFORM_MEMBER(FVector, GridZParams)
	UNIFORM_MEMBER(FVector2D, SVPosToVolumeUV)
	UNIFORM_MEMBER(FIntPoint, FogGridToPixelXY)
	UNIFORM_MEMBER(float, MaxDistance)
	UNIFORM_MEMBER(FVector, HeightFogInscatteringColor)
	UNIFORM_MEMBER(FVector, HeightFogDirectionalLightInscatteringColor)
END_UNIFORM_BUFFER_STRUCT(FVolumetricFogGlobalData)

class FVolumetricFogViewResources
{
public:
	TUniformBufferRef<FVolumetricFogGlobalData> VolumetricFogGlobalData;
	TRefCountPtr<IPooledRenderTarget> IntegratedLightScattering;

	FVolumetricFogViewResources()
	{}

	void Release()
	{
		IntegratedLightScattering = NULL;
	}
};

class FVolumetricPrimSet
{
public:

	/**
	* Adds a new primitives to the list of distortion prims
	* @param PrimitiveSceneProxies - primitive info to add.
	*/
	void Append(FPrimitiveSceneProxy** PrimitiveSceneProxies, int32 NumProxies)
	{
		Prims.Append(PrimitiveSceneProxies, NumProxies);
	}

	/** 
	* @return number of prims to render
	*/
	int32 NumPrims() const
	{
		return Prims.Num();
	}

	/** 
	* @return a prim currently set to render
	*/
	const FPrimitiveSceneProxy* GetPrim(int32 i)const
	{
		check(i>=0 && i<NumPrims());
		return Prims[i];
	}

private:
	/** list of distortion prims added from the scene */
	TArray<FPrimitiveSceneProxy*, SceneRenderingAllocator> Prims;
};

static const int32 GMaxNumReflectionCaptures = 341;

/** Per-reflection capture data needed by the shader. */
BEGIN_UNIFORM_BUFFER_STRUCT(FReflectionCaptureShaderData,)
	UNIFORM_MEMBER_ARRAY(FVector4,PositionAndRadius,[GMaxNumReflectionCaptures])
	// R is brightness, G is array index, B is shape
	UNIFORM_MEMBER_ARRAY(FVector4,CaptureProperties,[GMaxNumReflectionCaptures])
	UNIFORM_MEMBER_ARRAY(FVector4,CaptureOffsetAndAverageBrightness,[GMaxNumReflectionCaptures])
	// Stores the box transform for a box shape, other data is packed for other shapes
	UNIFORM_MEMBER_ARRAY(FMatrix,BoxTransform,[GMaxNumReflectionCaptures])
	UNIFORM_MEMBER_ARRAY(FVector4,BoxScales,[GMaxNumReflectionCaptures])
END_UNIFORM_BUFFER_STRUCT(FReflectionCaptureShaderData)

// Structure in charge of storing all information about TAA's history.
struct FTemporalAAHistory
{
	// Number of render target in the history.
	static constexpr uint32 kRenderTargetCount = 2;

	// Render targets holding's pixel history.
	//  scene color's RGBA are in RT[0].
	TRefCountPtr<IPooledRenderTarget> RT[kRenderTargetCount];

	// Reference size of RT. Might be different than RT's actual size to handle down res.
	FIntPoint ReferenceBufferSize;

	// Viewport coordinate of the history in RT according to ReferenceBufferSize.
	FIntRect ViewportRect;

	// Scene color's PreExposure.
	float SceneColorPreExposure;


	void SafeRelease()
	{
		for (uint32 i = 0; i < kRenderTargetCount; i++)
		{
			RT[i].SafeRelease();
		}
	}

	bool IsValid() const
	{
		return RT[0].IsValid();
	}
};

// Structure that hold all information related to previous frame.
struct FPreviousViewInfo
{
	// View matrices.
	FViewMatrices ViewMatrices;

	// Temporal AA result of last frame
	FTemporalAAHistory TemporalAAHistory;

	// Temporal AA history for diaphragm DOF.
	FTemporalAAHistory DOFPreGatherHistory;
	FTemporalAAHistory DOFPostGatherForegroundHistory;
	FTemporalAAHistory DOFPostGatherBackgroundHistory;

	// Scene color input for SSR, that can be different from TemporalAAHistory.RT[0] if there is a SSR
	// input post process material.
	TRefCountPtr<IPooledRenderTarget> CustomSSRInput;


	void SafeRelease()
	{
		TemporalAAHistory.SafeRelease();
		DOFPreGatherHistory.SafeRelease();
		DOFPostGatherForegroundHistory.SafeRelease();
		DOFPostGatherBackgroundHistory.SafeRelease();
		CustomSSRInput.SafeRelease();
	}
};

/** A FSceneView with additional state used by the scene renderer. */
class FViewInfo : public FSceneView
{
public:

	/* Final position of the view in the final render target (in pixels), potentially scaled by ScreenPercentage */
	FIntRect ViewRect;

	/** 
	 * The view's state, or NULL if no state exists.
	 * This should be used internally to the renderer module to avoid having to cast View.State to an FSceneViewState*
	 */
	FSceneViewState* ViewState;

	/** Cached view uniform shader parameters, to allow recreating the view uniform buffer without having to fill out the entire struct. */
	TUniquePtr<FViewUniformShaderParameters> CachedViewUniformShaderParameters;

	/** A map from primitive ID to a boolean visibility value. */
	FSceneBitArray PrimitiveVisibilityMap;

	/** Bit set when a primitive is known to be unoccluded. */
	FSceneBitArray PrimitiveDefinitelyUnoccludedMap;

	/** A map from primitive ID to a boolean is fading value. */
	FSceneBitArray PotentiallyFadingPrimitiveMap;

	/** Primitive fade uniform buffers, indexed by packed primitive index. */
	TArray<FUniformBufferRHIParamRef,SceneRenderingAllocator> PrimitiveFadeUniformBuffers;

	/** A map from primitive ID to the primitive's view relevance. */
	TArray<FPrimitiveViewRelevance,SceneRenderingAllocator> PrimitiveViewRelevanceMap;

	/** A map from static mesh ID to a boolean visibility value. */
	FSceneBitArray StaticMeshVisibilityMap;

	/** A map from static mesh ID to a boolean occluder value. */
	FSceneBitArray StaticMeshOccluderMap;

	/** A map from static mesh ID to a boolean velocity visibility value. */
	FSceneBitArray StaticMeshVelocityMap;

	/** A map from static mesh ID to a boolean shadow depth visibility value. */
	FSceneBitArray StaticMeshShadowDepthMap;

	/** A map from static mesh ID to a boolean dithered LOD fade out value. */
	FSceneBitArray StaticMeshFadeOutDitheredLODMap;

	/** A map from static mesh ID to a boolean dithered LOD fade in value. */
	FSceneBitArray StaticMeshFadeInDitheredLODMap;

#if WITH_EDITOR
	/** A map from static mesh ID to editor selection visibility (whether or not it is selected AND should be drawn).  */
	FSceneBitArray StaticMeshEditorSelectionMap;
#endif

	/** Will only contain relevant primitives for view and/or shadow */
	TArray<FLODMask, SceneRenderingAllocator> PrimitivesLODMask;

	/** Used to know which shadow casting primitive were already init (lazy init)  */
	FSceneBitArray InitializedShadowCastingPrimitive;

	/** An array of batch element visibility masks, valid only for meshes
	 set visible in either StaticMeshVisibilityMap or StaticMeshShadowDepthMap. */
	TArray<uint64,SceneRenderingAllocator> StaticMeshBatchVisibility;

	/** The dynamic primitives visible in this view. */
	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> VisibleDynamicPrimitives;

	/** The dynamic editor primitives visible in this view. */
	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> VisibleEditorPrimitives;

	/** List of visible primitives with dirty precomputed lighting buffers */
	TArray<FPrimitiveSceneInfo*,SceneRenderingAllocator> DirtyPrecomputedLightingBufferPrimitives;

	/** View dependent global distance field clipmap info. */
	FGlobalDistanceFieldInfo GlobalDistanceFieldInfo;

	/** Set of translucent prims for this view */
	FTranslucentPrimSet TranslucentPrimSet;

	/** Set of distortion prims for this view */
	FDistortionPrimSet DistortionPrimSet;
	
	/** Set of mesh decal prims for this view */
	FMeshDecalPrimSet MeshDecalPrimSet;
	
	/** Set of CustomDepth prims for this view */
	FCustomDepthPrimSet CustomDepthSet;

	/** Primitives with a volumetric material. */
	FVolumetricPrimSet VolumetricPrimSet;

	/** A map from light ID to a boolean visibility value. */
	TArray<FVisibleLightViewInfo,SceneRenderingAllocator> VisibleLightInfos;

	/** The view's batched elements. */
	FBatchedElements BatchedViewElements;

	/** The view's batched elements, above all other elements, for gizmos that should never be occluded. */
	FBatchedElements TopBatchedViewElements;

	/** The view's mesh elements. */
	TIndirectArray<FMeshBatch> ViewMeshElements;

	/** The view's mesh elements for the foreground (editor gizmos and primitives )*/
	TIndirectArray<FMeshBatch> TopViewMeshElements;

	/** The dynamic resources used by the view elements. */
	TArray<FDynamicPrimitiveResource*> DynamicResources;

	/** Gathered in initviews from all the primitives with dynamic view relevance, used in each mesh pass. */
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicMeshElements;

	// [PrimitiveIndex] = end index index in DynamicMeshElements[], to support GetDynamicMeshElementRange()
	TArray<uint32,SceneRenderingAllocator> DynamicMeshEndIndices;

	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicEditorMeshElements;

	FSimpleElementCollector SimpleElementCollector;

	FSimpleElementCollector EditorSimpleElementCollector;

	// Used by mobile renderer to determine whether static meshes will be rendered with CSM shaders or not.
	FMobileCSMVisibilityInfo MobileCSMVisibilityInfo;

	// Primitive CustomData
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> PrimitivesWithCustomData;	// Size == Amount of Primitive With Custom Data
	FSceneBitArray UpdatedPrimitivesWithCustomData;
	TArray<FMemStackBase, SceneRenderingAllocator> PrimitiveCustomDataMemStack; // Size == 1 global stack + 1 per visibility thread (if multithread)

	/** Parameters for exponential height fog. */
	FVector4 ExponentialFogParameters;
	FVector ExponentialFogColor;
	float FogMaxOpacity;
	FVector4 ExponentialFogParameters3;
	FVector2D SinCosInscatteringColorCubemapRotation;

	UTexture* FogInscatteringColorCubemap;
	FVector FogInscatteringTextureParameters;

	/** Parameters for directional inscattering of exponential height fog. */
	bool bUseDirectionalInscattering;
	float DirectionalInscatteringExponent;
	float DirectionalInscatteringStartDistance;
	FVector InscatteringLightDirection;
	FLinearColor DirectionalInscatteringColor;

	/** Translucency lighting volume properties. */
	FVector TranslucencyLightingVolumeMin[TVC_MAX];
	float TranslucencyVolumeVoxelSize[TVC_MAX];
	FVector TranslucencyLightingVolumeSize[TVC_MAX];

	/** Temporal jitter at the pixel scale. */
	FVector2D TemporalJitterPixels;

	/** true if all PrimitiveVisibilityMap's bits are set to false. */
	uint32 bHasNoVisiblePrimitive : 1;

	/** true if the view has at least one mesh with a translucent material. */
	uint32 bHasTranslucentViewMeshElements : 1;
	/** Indicates whether previous frame transforms were reset this frame for any reason. */
	uint32 bPrevTransformsReset : 1;
	/** Whether we should ignore queries from last frame (useful to ignoring occlusions on the first frame after a large camera movement). */
	uint32 bIgnoreExistingQueries : 1;
	/** Whether we should submit new queries this frame. (used to disable occlusion queries completely. */
	uint32 bDisableQuerySubmissions : 1;
	/** Whether we should disable distance-based fade transitions for this frame (usually after a large camera movement.) */
	uint32 bDisableDistanceBasedFadeTransitions : 1;
	/** Whether the view has any materials that use the global distance field. */
	uint32 bUsesGlobalDistanceField : 1;
	uint32 bUsesLightingChannels : 1;
	uint32 bTranslucentSurfaceLighting : 1;
	/** Whether the view has any materials that read from scene depth. */
	uint32 bUsesSceneDepth : 1;
	/** 
	 * true if the scene has at least one decal. Used to disable stencil operations in the mobile base pass when the scene has no decals.
	 * TODO: Right now decal visibility is computed right before rendering them. Ideally it should be done in InitViews and this flag should be replaced with list of visible decals  
	 */
	uint32 bSceneHasDecals : 1;
	/** Bitmask of all shading models used by primitives in this view */
	uint16 ShadingModelMaskInView;

	// Previous frame view info to use for this view.
	FPreviousViewInfo PrevViewInfo;

	/** The GPU nodes on which to render this view. */
	FRHIGPUMask GPUMask;

	/** An intermediate number of visible static meshes.  Doesn't account for occlusion until after FinishOcclusionQueries is called. */
	int32 NumVisibleStaticMeshElements;

	/** Frame's exposure. Always > 0. */
	float PreExposure;

	/** Mip bias to apply in material's samplers. */
	float MaterialTextureMipBias;

	/** Precomputed visibility data, the bits are indexed by VisibilityId of a primitive component. */
	const uint8* PrecomputedVisibilityData;

	FOcclusionQueryBatcher IndividualOcclusionQueries;
	FOcclusionQueryBatcher GroupedOcclusionQueries;

	// Hierarchical Z Buffer
	TRefCountPtr<IPooledRenderTarget> HZB;

	int32 NumBoxReflectionCaptures;
	int32 NumSphereReflectionCaptures;
	float FurthestReflectionCaptureDistance;
	TUniformBufferRef<FReflectionCaptureShaderData> ReflectionCaptureUniformBuffer;

	/** Used when there is no view state, buffers reallocate every frame. */
	TUniquePtr<FForwardLightingViewResources> ForwardLightingResourcesStorage;

	FVolumetricFogViewResources VolumetricFogResources;

	// Size of the HZB's mipmap 0
	// NOTE: the mipmap 0 is downsampled version of the depth buffer
	FIntPoint HZBMipmap0Size;

	/** Used by occlusion for percent unoccluded calculations. */
	float OneOverNumPossiblePixels;

	// Mobile gets one light-shaft, this light-shaft.
	FVector4 LightShaftCenter; 
	FLinearColor LightShaftColorMask;
	FLinearColor LightShaftColorApply;
	bool bLightShaftUse;

	FHeightfieldLightingViewInfo HeightfieldLightingViewInfo;

	TShaderMap<FGlobalShaderType>* ShaderMap;

	bool bIsSnapshot;

	// Optional stencil dithering optimization during prepasses
	bool bAllowStencilDither;

	/** Custom visibility query for view */
	ICustomVisibilityQuery* CustomVisibilityQuery;

	TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> IndirectShadowPrimitives;

	/** 
	 * Initialization constructor. Passes all parameters to FSceneView constructor
	 */
	FViewInfo(const FSceneViewInitOptions& InitOptions);

	/** 
	* Initialization constructor. 
	* @param InView - copy to init with
	*/
	explicit FViewInfo(const FSceneView* InView);

	/** 
	* Destructor. 
	*/
	~FViewInfo();

#if DO_CHECK
	/** Verifies all the assertions made on members. */
	bool VerifyMembersChecks() const;
#endif

	/** Returns the size of view rect after primary upscale ( == only with secondary screen percentage). */
	FIntPoint GetSecondaryViewRectSize() const;

	/** Returns whether the view requires a secondary upscale. */
	bool RequiresSecondaryUpscale() const
	{
		return UnscaledViewRect.Size() != GetSecondaryViewRectSize();
	}

	/** Creates ViewUniformShaderParameters given a set of view transforms. */
	void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices,
		FBox* OutTranslucentCascadeBoundsArray, 
		int32 NumTranslucentCascades,
		FViewUniformShaderParameters& ViewUniformShaderParameters) const;

	/** Recreates ViewUniformShaderParameters, taking the view transform from the View Matrices */
	inline void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		FBox* OutTranslucentCascadeBoundsArray,
		int32 NumTranslucentCascades,
		FViewUniformShaderParameters& ViewUniformShaderParameters) const
	{
		SetupUniformBufferParameters(SceneContext,
			ViewMatrices,
			PrevViewInfo.ViewMatrices,
			OutTranslucentCascadeBoundsArray,
			NumTranslucentCascades,
			ViewUniformShaderParameters);
	}

	void SetupDefaultGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
	void SetupGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
	void SetupVolumetricFogUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;

	/** Initializes the RHI resources used by this view. */
	void InitRHIResources();

	/** Determines distance culling and fades if the state changes */
	bool IsDistanceCulled(float DistanceSquared, float MaxDrawDistance, float MinDrawDistance, const FPrimitiveSceneInfo* PrimitiveSceneInfo);

	/** Gets the eye adaptation render target for this view. Same as GetEyeAdaptationRT */
	IPooledRenderTarget* GetEyeAdaptation(FRHICommandList& RHICmdList) const;

	IPooledRenderTarget* GetEyeAdaptation() const
	{
		return GetEyeAdaptationRT();
	}

	/** Gets one of two eye adaptation render target for this view.
	* NB: will return null in the case that the internal view state pointer
	* (for the left eye in the stereo case) is null.
	*/
	IPooledRenderTarget* GetEyeAdaptationRT(FRHICommandList& RHICmdList) const;
	IPooledRenderTarget* GetEyeAdaptationRT() const;
	IPooledRenderTarget* GetLastEyeAdaptationRT(FRHICommandList& RHICmdList) const;

	/**Swap the order of the two eye adaptation targets in the double buffer system */
	void SwapEyeAdaptationRTs(FRHICommandList& RHICmdList) const;

	/** Tells if the eyeadaptation texture exists without attempting to allocate it. */
	bool HasValidEyeAdaptation() const;

	/** Informs sceneinfo that eyedaptation has queued commands to compute it at least once and that it can be used */
	void SetValidEyeAdaptation() const;

	/** Get the last valid exposure value for eye adapation. */
	float GetLastEyeAdaptationExposure() const;

	/** Informs sceneinfo that tonemapping LUT has queued commands to compute it at least once */
	void SetValidTonemappingLUT() const;

	/** Gets the tonemapping LUT texture, previously computed by the CombineLUTS post process,
	* for stereo rendering, this will force the post-processing to use the same texture for both eyes*/
	const FTextureRHIRef* GetTonemappingLUTTexture() const;

	/** Gets the rendertarget that will be populated by CombineLUTS post process 
	* for stereo rendering, this will force the post-processing to use the same render target for both eyes*/
	FSceneRenderTargetItem* GetTonemappingLUTRenderTarget(FRHICommandList& RHICmdList, const int32 LUTSize, const bool bUseVolumeLUT, const bool bNeedUAV) const;
	


	/** Instanced stereo and multi-view only need to render the left eye. */
	bool ShouldRenderView() const 
	{
		if (bHasNoVisiblePrimitive)
		{
			return false;
		}
		else if (!bIsInstancedStereoEnabled && !bIsMobileMultiViewEnabled)
		{
			return true;
		}
		else if (bIsInstancedStereoEnabled && StereoPass != eSSP_RIGHT_EYE)
		{
			return true;
		}
		else if (bIsMobileMultiViewEnabled && StereoPass != eSSP_RIGHT_EYE && Family && Family->Views.Num() > 1)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	inline FVector GetPrevViewDirection() const { return PrevViewInfo.ViewMatrices.GetViewMatrix().GetColumn(2); }

	/** Create a snapshot of this view info on the scene allocator. */
	FViewInfo* CreateSnapshot() const;

	/** Destroy all snapshots before we wipe the scene allocator. */
	static void DestroyAllSnapshots();
	
	// Get the range in DynamicMeshElements[] for a given PrimitiveIndex
	// @return range (start is inclusive, end is exclusive)
	FInt32Range GetDynamicMeshElementRange(uint32 PrimitiveIndex) const
	{
		// inclusive
		int32 Start = (PrimitiveIndex == 0) ? 0 : DynamicMeshEndIndices[PrimitiveIndex - 1];
		// exclusive
		int32 AfterEnd = DynamicMeshEndIndices[PrimitiveIndex];
		
		return FInt32Range(Start, AfterEnd);
	}

	/** Set the custom data associated with a primitive scene info.	*/
	void SetCustomData(const FPrimitiveSceneInfo* InPrimitiveSceneInfo, void* InCustomData);

	/** Custom Data Memstack functions.	*/
	FORCEINLINE FMemStackBase& GetCustomDataGlobalMemStack() { return PrimitiveCustomDataMemStack[0]; }
	FORCEINLINE FMemStackBase& AllocateCustomDataMemStack() { return *new(PrimitiveCustomDataMemStack) FMemStackBase(0); }

private:
	// Cache of TEXTUREGROUP_World to create view's samplers on render thread.
	// may not have a valid value if FViewInfo is created on the render thread.
	ESamplerFilter WorldTextureGroupSamplerFilter;
	bool bIsValidWorldTextureGroupSamplerFilter;

	FSceneViewState* GetEffectiveViewState() const;

	/** Initialization that is common to the constructors. */
	void Init();

	/** Calculates bounding boxes for the translucency lighting volume cascades. */
	void CalcTranslucencyLightingVolumeBounds(FBox* InOutCascadeBoundsArray, int32 NumCascades) const;

	/** Sets the sky SH irradiance map coefficients. */
	void SetupSkyIrradianceEnvironmentMapConstants(FVector4* OutSkyIrradianceEnvironmentMap) const;
};


/**
 * Masks indicating for which views a primitive needs to have a certain operation on.
 * One entry per primitive in the scene.
 */
typedef TArray<uint8, SceneRenderingAllocator> FPrimitiveViewMasks;

class FShadowMapRenderTargetsRefCounted
{
public:
	TArray<TRefCountPtr<IPooledRenderTarget>, SceneRenderingAllocator> ColorTargets;
	TRefCountPtr<IPooledRenderTarget> DepthTarget;

	bool IsValid() const
	{
		if (DepthTarget)
		{
			return true;
		}
		else 
		{
			return ColorTargets.Num() > 0;
		}
	}

	FIntPoint GetSize() const
	{
		const FPooledRenderTargetDesc* Desc = NULL;

		if (DepthTarget)
		{
			Desc = &DepthTarget->GetDesc();
		}
		else 
		{
			check(ColorTargets.Num() > 0);
			Desc = &ColorTargets[0]->GetDesc();
		}

		return Desc->Extent;
	}

	int64 ComputeMemorySize() const
	{
		int64 MemorySize = 0;

		for (int32 i = 0; i < ColorTargets.Num(); i++)
		{
			MemorySize += ColorTargets[i]->ComputeMemorySize();
		}

		if (DepthTarget)
		{
			MemorySize += DepthTarget->ComputeMemorySize();
		}

		return MemorySize;
	}

	void Release()
	{
		for (int32 i = 0; i < ColorTargets.Num(); i++)
		{
			ColorTargets[i] = NULL;
		}

		ColorTargets.Empty();

		DepthTarget = NULL;
	}
};

struct FSortedShadowMapAtlas
{
	FShadowMapRenderTargetsRefCounted RenderTargets;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> Shadows;
};

struct FSortedShadowMaps
{
	/** Visible shadows sorted by their shadow depth map render target. */
	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> ShadowMapAtlases;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> RSMAtlases;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> ShadowMapCubemaps;

	FSortedShadowMapAtlas PreshadowCache;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> TranslucencyShadowMapAtlases;

	void Release();

	int64 ComputeMemorySize() const
	{
		int64 MemorySize = 0;

		for (int i = 0; i < ShadowMapAtlases.Num(); i++)
		{
			MemorySize += ShadowMapAtlases[i].RenderTargets.ComputeMemorySize();
		}

		for (int i = 0; i < RSMAtlases.Num(); i++)
		{
			MemorySize += RSMAtlases[i].RenderTargets.ComputeMemorySize();
		}

		for (int i = 0; i < ShadowMapCubemaps.Num(); i++)
		{
			MemorySize += ShadowMapCubemaps[i].RenderTargets.ComputeMemorySize();
		}

		MemorySize += PreshadowCache.RenderTargets.ComputeMemorySize();

		for (int i = 0; i < TranslucencyShadowMapAtlases.Num(); i++)
		{
			MemorySize += TranslucencyShadowMapAtlases[i].RenderTargets.ComputeMemorySize();
		}

		return MemorySize;
	}
};

/**
 * Used as the scope for scene rendering functions.
 * It is initialized in the game thread by FSceneViewFamily::BeginRender, and then passed to the rendering thread.
 * The rendering thread calls Render(), and deletes the scene renderer when it returns.
 */
class FSceneRenderer
{
public:

	/** The scene being rendered. */
	FScene* Scene;

	/** The view family being rendered.  This references the Views array. */
	FSceneViewFamily ViewFamily;

	/** The views being rendered. */
	TArray<FViewInfo> Views;

	FMeshElementCollector MeshCollector;

	/** Information about the visible lights. */
	TArray<FVisibleLightInfo,SceneRenderingAllocator> VisibleLightInfos;

	FSortedShadowMaps SortedShadowsForShadowDepthPass;

	/** If a freeze request has been made */
	bool bHasRequestedToggleFreeze;

	/** True if precomputed visibility was used when rendering the scene. */
	bool bUsedPrecomputedVisibility;

	/** Lights added if wholescenepointlight shadow would have been rendered (ignoring r.SupportPointLightWholeSceneShadows). Used for warning about unsupported features. */	
	TArray<FName, SceneRenderingAllocator> UsedWholeScenePointLightNames;

	/** Feature level being rendered */
	ERHIFeatureLevel::Type FeatureLevel;
	
	/** 
	 * The width in pixels of the stereo view family being rendered. This may be different than FamilySizeX if
	 * we're using adaptive resolution stereo rendering. In that case, FamilySizeX represents the maximum size of 
	 * the family to ensure the backing render targets don't change between frames as the view size varies.
	 */
	uint32 InstancedStereoWidth;

	/** Only used if we are going to delay the deletion of the scene renderer until later. */
	FMemMark* RootMark;

public:

	FSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);
	virtual ~FSceneRenderer();

	// FSceneRenderer interface

	virtual void Render(FRHICommandListImmediate& RHICmdList) = 0;
	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) {}

	/** Creates a scene renderer based on the current feature level. */
	static FSceneRenderer* CreateSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer);

	/** Setups FViewInfo::ViewRect according to ViewFamilly's ScreenPercentageInterface. */
	void PrepareViewRectsForRendering();

	bool DoOcclusionQueries(ERHIFeatureLevel::Type InFeatureLevel) const;
	/** Issues occlusion queries. */
	void BeginOcclusionTests(FRHICommandListImmediate& RHICmdList, bool bRenderQueries);

	// fences to make sure the rhi thread has digested the occlusion query renders before we attempt to read them back async
	static FGraphEventRef OcclusionSubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];
	/** Fences occlusion queries. */
	void FenceOcclusionTests(FRHICommandListImmediate& RHICmdList);
	/** Waits for the occlusion fence. */
	void WaitOcclusionTests(FRHICommandListImmediate& RHICmdList);

	/** bound shader state for occlusion test prims */
	static FGlobalBoundShaderState OcclusionTestBoundShaderState;
	
	/**
	* Whether or not to composite editor objects onto the scene as a post processing step
	*
	* @param View The view to test against
	*
	* @return true if compositing is needed
	*/
	static bool ShouldCompositeEditorPrimitives(const FViewInfo& View);

	/** the last thing we do with a scene renderer, lots of cleanup related to the threading **/
	static void WaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, bool bWaitForTasks = true);
	static void DelayWaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer);
	
	/** Apply the ResolutionFraction on ViewSize, taking into account renderer's requirements. */
	static FIntPoint ApplyResolutionFraction(
		const FSceneViewFamily& ViewFamily, const FIntPoint& UnscaledViewSize, float ResolutionFraction);

	/** Quantize the ViewRect.Min according to various renderer's downscale requirements. */
	static FIntPoint QuantizeViewRectMin(const FIntPoint& ViewRectMin);

	/** Get the desired buffer size from the view family's ResolutionFraction upperbound.
	 * Can be called on game thread or render thread. 
	 */
	static FIntPoint GetDesiredInternalBufferSize(const FSceneViewFamily& ViewFamily);

	/** Exposes renderer's privilege to fork view family's screen percentage interface. */
	static ISceneViewFamilyScreenPercentage* ForkScreenPercentageInterface(
		const ISceneViewFamilyScreenPercentage* ScreenPercentageInterface, FSceneViewFamily& ForkedViewFamily)
	{
		return ScreenPercentageInterface->Fork_GameThread(ForkedViewFamily);
	}

protected:

	/** Size of the family. */
	FIntPoint FamilySize;
	
	// Shared functionality between all scene renderers

	void InitDynamicShadows(FRHICommandListImmediate& RHICmdList);

	bool RenderShadowProjections(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, bool bProjectingForForwardShading, bool bMobileModulatedProjections);

	/** Finds a matching cached preshadow, if one exists. */
	TRefCountPtr<FProjectedShadowInfo> GetCachedPreshadow(
		const FLightPrimitiveInteraction* InParentInteraction,
		const FProjectedShadowInitializer& Initializer,
		const FBoxSphereBounds& Bounds,
		uint32 InResolutionX);

	/** Creates a per object projected shadow for the given interaction. */
	void CreatePerObjectProjectedShadow(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		bool bCreateTranslucentObjectShadow,
		bool bCreateInsetObjectShadow,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows);

	/** Creates shadows for the given interaction. */
	void SetupInteractionShadows(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		FVisibleLightInfo& VisibleLightInfo,
		bool bStaticSceneOnly,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& PreShadows);

	/** Generates FProjectedShadowInfos for all wholesceneshadows on the given light.*/
	void AddViewDependentWholeSceneShadowsForView(
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfos, 
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfosThatNeedCulling, 
		FVisibleLightInfo& VisibleLightInfo, 
		FLightSceneInfo& LightSceneInfo);

	void AllocateShadowDepthTargets(FRHICommandListImmediate& RHICmdList);
	
	void AllocatePerObjectShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& Shadows);

	void AllocateCachedSpotlightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& CachedShadows);

	void AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeSceneDirectionalShadows);

	void AllocateRSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& RSMShadows);

	void AllocateOnePassPointLightDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeScenePointShadows);

	void AllocateTranslucentShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& TranslucentShadows);

	/**
	* Used by RenderLights to figure out if projected shadows need to be rendered to the attenuation buffer.
	* Or to render a given shadowdepth map for forward rendering.
	*
	* @param LightSceneInfo Represents the current light
	* @return true if anything needs to be rendered
	*/
	bool CheckForProjectedShadows(const FLightSceneInfo* LightSceneInfo) const;

	/** Gathers the list of primitives used to draw various shadow types */
	void GatherShadowPrimitives(
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& PreShadows,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		bool bReflectionCaptureScene);

	void RenderShadowDepthMaps(FRHICommandListImmediate& RHICmdList);
	void RenderShadowDepthMapAtlases(FRHICommandListImmediate& RHICmdList);

	/**
	* Creates a projected shadow for all primitives affected by a light.
	* @param LightSceneInfo - The light to create a shadow for.
	*/
	void CreateWholeSceneProjectedShadow(FLightSceneInfo* LightSceneInfo, uint32& NumPointShadowCachesUpdatedThisFrame, uint32& NumSpotShadowCachesUpdatedThisFrame);

	/** Updates the preshadow cache, allocating new preshadows that can fit and evicting old ones. */
	void UpdatePreshadowCache(FSceneRenderTargets& SceneContext);

	/** Gets a readable light name for use with a draw event. */
	static void GetLightNameForDrawEvent(const FLightSceneProxy* LightProxy, FString& LightNameWithLevel);

	/** Gathers simple lights from visible primtives in the passed in views. */
	static void GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& SimpleLights);

	/** Calculates projected shadow visibility. */
	void InitProjectedShadowVisibility(FRHICommandListImmediate& RHICmdList);	

	/** Gathers dynamic mesh elements for all shadows. */
	void GatherShadowDynamicMeshElements();

	/** Performs once per frame setup prior to visibility determination. */
	void PreVisibilityFrameSetup(FRHICommandListImmediate& RHICmdList);

	/** Computes which primitives are visible and relevant for each view. */
	void ComputeViewVisibility(FRHICommandListImmediate& RHICmdList);

	/** Performs once per frame setup after to visibility determination. */
	void PostVisibilityFrameSetup(FILCUpdatePrimTaskData& OutILCTaskData);

	void GatherDynamicMeshElements(
		TArray<FViewInfo>& InViews, 
		const FScene* InScene, 
		const FSceneViewFamily& InViewFamily, 
		const FPrimitiveViewMasks& HasDynamicMeshElementsMasks, 
		const FPrimitiveViewMasks& HasDynamicEditorMeshElementsMasks, 
		const FPrimitiveViewMasks& HasViewCustomDataMasks,
		FMeshElementCollector& Collector);

	/** Initialized the fog constants for each view. */
	void InitFogConstants();

	/** Returns whether there are translucent primitives to be rendered. */
	bool ShouldRenderTranslucency(ETranslucencyPass::Type TranslucencyPass) const;

	/** TODO: REMOVE if no longer needed: Copies scene color to the viewport's render target after applying gamma correction. */
	void GammaCorrectToViewportRenderTarget(FRHICommandList& RHICmdList, const FViewInfo* View, float OverrideGamma);

	/** Updates state for the end of the frame. */
	void RenderFinish(FRHICommandListImmediate& RHICmdList);

	void RenderCustomDepthPassAtLocation(FRHICommandListImmediate& RHICmdList, int32 Location);
	void RenderCustomDepthPass(FRHICommandListImmediate& RHICmdList);

	void OnStartFrame(FRHICommandListImmediate& RHICmdList);

	/** Renders the scene's distortion */
	void RenderDistortion(FRHICommandListImmediate& RHICmdList);
	void RenderDistortionES2(FRHICommandListImmediate& RHICmdList);

	/** Returns the scene color texture multi-view is targeting. */	
	FTextureRHIParamRef GetMultiViewSceneColor(const FSceneRenderTargets& SceneContext) const;

	/** Composites the monoscopic far field view into the stereo views. */
	void CompositeMonoscopicFarField(FRHICommandListImmediate& RHICmdList);

	/** Renders a depth mask into the monoscopic far field view to ensure we only render visible pixels. */
	void RenderMonoscopicFarFieldMask(FRHICommandListImmediate& RHICmdList);

	static int32 GetRefractionQuality(const FSceneViewFamily& ViewFamily);

	void UpdatePrimitivePrecomputedLightingBuffers();
	void ClearPrimitiveSingleFramePrecomputedLightingBuffers();

	void RenderPlanarReflection(class FPlanarReflectionSceneProxy* ReflectionSceneProxy);

	void ResolveSceneColor(FRHICommandList& RHICmdList);

private:
	void ComputeFamilySize();
};

/**
 * Renderer that implements simple forward shading and associated features.
 */
class FMobileSceneRenderer : public FSceneRenderer
{
public:

	FMobileSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);

	// FSceneRenderer interface

	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

	bool RenderInverseOpacity(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	void RenderMobileBasePassDynamicData(FRHICommandList& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, EBlendMode BlendMode, bool bWireFrame, int32 FirstElement = 0, int32 AfterLastElement = MAX_int32);

protected:
	/** Finds the visible dynamic shadows for each view. */
	void InitDynamicShadows(FRHICommandListImmediate& RHICmdList);

	/** Build visibility lists on CSM receivers and non-csm receivers. */
	void BuildCSMVisibilityState(FLightSceneInfo* LightSceneInfo);

	void InitViews(FRHICommandListImmediate& RHICmdList);

	/** Renders the opaque base pass for mobile. */
	void RenderMobileBasePass(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	void RenderMobileEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState);
	void RenderMobileBasePassViewParallel(const FViewInfo& View, FRHICommandListImmediate& ParentCmdList, TArray<FViewInfo>& Views, const FDrawingPolicyRenderState& DrawRenderState);

	/** Render modulated shadow projections in to the scene, loops over any unrendered shadows until all are processed.*/
	void RenderModulatedShadowProjections(FRHICommandListImmediate& RHICmdList);

	/** Makes a copy of scene alpha so PC can emulate ES2 framebuffer fetch. */
	void CopySceneAlpha(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Resolves scene depth in case hardware does not support reading depth in the shader */
	void ConditionalResolveSceneDepth(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues occlusion queries */
	void RenderOcclusion(FRHICommandListImmediate& RHICmdList);
	
	/** Computes how many queries will be issued this frame */
	int32 ComputeNumOcclusionQueriesToBatch() const;

	/** Renders decals. */
	void RenderDecals(FRHICommandListImmediate& RHICmdList);

	/** Renders the base pass for translucency. */
	void RenderTranslucency(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	/** Perform upscaling when post process is not used. */
	void BasicPostProcess(FRHICommandListImmediate& RHICmdList, FViewInfo &View, bool bDoUpscale, bool bDoEditorPrimitives);

	/** Creates uniform buffers with the mobile directional light parameters, for each lighting channel. Called by InitViews */
	void CreateDirectionalLightUniformBuffers(FSceneView& SceneView);

	/** Copy scene color from the mobile multi-view render targat array to side by side stereo scene color */
	void CopyMobileMultiViewSceneColor(FRHICommandListImmediate& RHICmdList);

	/** Gather information about post-processing pass, which can be used by render for optimizations. Called by InitViews */
	void UpdatePostProcessUsageFlags();

	/** Render inverse opacity for the dynamic meshes. */
	bool RenderInverseOpacityDynamic(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState);

	/** Will update the view custom data. */
	void PostInitViewCustomData();
	
private:

	bool bModulatedShadowsInUse;
	bool bPostProcessUsesDepthTexture;
};

// The noise textures need to be set in Slate too.
RENDERER_API void UpdateNoiseTextureParameters(FViewUniformShaderParameters& ViewUniformShaderParameters);

inline FTextureRHIParamRef OrBlack2DIfNull(FTextureRHIParamRef Tex)
{
	FTextureRHIParamRef Result = Tex ? Tex : GBlackTexture->TextureRHI.GetReference();
	check(Result);
	return Result;
}

inline FTextureRHIParamRef OrBlack3DIfNull(FTextureRHIParamRef Tex)
{
	// we fall back to 2D which are unbound es2 parameters
	return OrBlack2DIfNull(Tex ? Tex : GBlackVolumeTexture->TextureRHI.GetReference());
}

inline FTextureRHIParamRef OrBlack3DUintIfNull(FTextureRHIParamRef Tex)
{
	// we fall back to 2D which are unbound es2 parameters
	return OrBlack2DIfNull(Tex ? Tex : GBlackUintVolumeTexture->TextureRHI.GetReference());
}

inline void SetBlack2DIfNull(FTextureRHIParamRef& Tex)
{
	if (!Tex)
	{
		Tex = GBlackTexture->TextureRHI.GetReference();
		check(Tex);
	}
}

inline void SetBlack3DIfNull(FTextureRHIParamRef& Tex)
{
	if (!Tex)
	{
		Tex = GBlackVolumeTexture->TextureRHI.GetReference();
		// we fall back to 2D which are unbound es2 parameters
		SetBlack2DIfNull(Tex);
	}
}

extern TAutoConsoleVariable<int32> CVarTransientResourceAliasing_Buffers;

FORCEINLINE bool IsTransientResourceBufferAliasingEnabled()
{
	return (GSupportsTransientResourceAliasing && CVarTransientResourceAliasing_Buffers.GetValueOnRenderThread() != 0);
}

struct FFastVramConfig
{
	FFastVramConfig();
	void Update();
	void OnCVarUpdated();
	void OnSceneRenderTargetsAllocated();

	uint32 GBufferA;
	uint32 GBufferB;
	uint32 GBufferC;
	uint32 GBufferD;
	uint32 GBufferE;
	uint32 GBufferVelocity;
	uint32 HZB;
	uint32 SceneDepth;
	uint32 SceneColor;
	uint32 LPV;
	uint32 BokehDOF;
	uint32 CircleDOF;
	uint32 CombineLUTs;
	uint32 Downsample;
	uint32 EyeAdaptation;
	uint32 Histogram;
	uint32 HistogramReduce;
	uint32 VelocityFlat;
	uint32 VelocityMax;
	uint32 MotionBlur;
	uint32 Tonemap;
	uint32 Upscale;
	uint32 DistanceFieldNormal;
	uint32 DistanceFieldAOHistory;
	uint32 DistanceFieldAOBentNormal;
	uint32 DistanceFieldAODownsampledBentNormal;
	uint32 DistanceFieldShadows;
	uint32 DistanceFieldIrradiance;
	uint32 DistanceFieldAOConfidence;
	uint32 Distortion;
	uint32 ScreenSpaceShadowMask;
	uint32 VolumetricFog;
	uint32 SeparateTranslucency;
	uint32 LightAccumulation;
	uint32 LightAttenuation;
	uint32 ScreenSpaceAO;
	uint32 SSR;
	uint32 DBufferA;
	uint32 DBufferB;
	uint32 DBufferC;
	uint32 DBufferMask;
	uint32 DOFSetup;
	uint32 DOFReduce;
	uint32 DOFPostfilter;

	uint32 CustomDepth;
	uint32 ShadowPointLight;
	uint32 ShadowPerObject;
	uint32 ShadowCSM;

	// Buffers
	uint32 DistanceFieldCulledObjectBuffers;
	uint32 DistanceFieldTileIntersectionResources;
	uint32 DistanceFieldAOScreenGridResources;
	uint32 ForwardLightingCullingResources;
	bool bDirty;

private:
	bool UpdateTextureFlagFromCVar(TAutoConsoleVariable<int32>& CVar, uint32& InOutValue);
	bool UpdateBufferFlagFromCVar(TAutoConsoleVariable<int32>& CVar, uint32& InOutValue);
};

extern FFastVramConfig GFastVRamConfig;
