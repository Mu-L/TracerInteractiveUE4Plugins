// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShadowSetup.cpp: Dynamic shadow setup implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "HAL/IConsoleManager.h"
#include "EngineDefines.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "ConvexVolume.h"
#include "SceneTypes.h"
#include "SceneInterface.h"
#include "RendererInterface.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "ScenePrivateBase.h"
#include "PostProcess/SceneRenderTargets.h"
#include "Math/GenericOctree.h"
#include "LightSceneInfo.h"
#include "ShadowRendering.h"
#include "TextureLayout.h"
#include "SceneRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "LightPropagationVolumeSettings.h"
#include "CapsuleShadowRendering.h"
#include "Async/ParallelFor.h"

static float GMinScreenRadiusForShadowCaster = 0.01f;
static FAutoConsoleVariableRef CVarMinScreenRadiusForShadowCaster(
	TEXT("r.Shadow.RadiusThreshold"),
	GMinScreenRadiusForShadowCaster,
	TEXT("Cull shadow casters if they are too small, value is the minimal screen space bounding sphere radius"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static float GMinScreenRadiusForShadowCasterRSM = 0.06f;
static FAutoConsoleVariableRef CVarMinScreenRadiusForShadowCasterRSM(
	TEXT("r.Shadow.RadiusThresholdRSM"),
	GMinScreenRadiusForShadowCasterRSM,
	TEXT("Cull shadow casters in the RSM if they are too small, values is the minimal screen space bounding sphere radius\n")
	TEXT("(default 0.06)")
	);

int32 GCacheWholeSceneShadows = 1;
FAutoConsoleVariableRef CVarCacheWholeSceneShadows(
	TEXT("r.Shadow.CacheWholeSceneShadows"),
	GCacheWholeSceneShadows,
	TEXT("When enabled, movable point and spot light whole scene shadow depths from static primitives will be cached as an optimization."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GMaxNumPointShadowCacheUpdatesPerFrame = -1;
FAutoConsoleVariableRef CVarMaxNumPointShadowCacheUpdatePerFrame(
	TEXT("r.Shadow.MaxNumPointShadowCacheUpdatesPerFrame"),
	GMaxNumPointShadowCacheUpdatesPerFrame,
	TEXT("Maximum number of point light shadow cache updates allowed per frame."
		"Only affect updates caused by resolution change. -1 means no limit."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GMaxNumSpotShadowCacheUpdatesPerFrame = -1;
FAutoConsoleVariableRef CVarMaxNumSpotShadowCacheUpdatePerFrame(
	TEXT("r.Shadow.MaxNumSpotShadowCacheUpdatesPerFrame"),
	GMaxNumSpotShadowCacheUpdatesPerFrame,
	TEXT("Maximum number of spot light shadow cache updates allowed per frame."
		"Only affect updates caused by resolution change. -1 means no limit."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GWholeSceneShadowCacheMb = 150;
FAutoConsoleVariableRef CVarWholeSceneShadowCacheMb(
	TEXT("r.Shadow.WholeSceneShadowCacheMb"),
	GWholeSceneShadowCacheMb,
	TEXT("Amount of memory that can be spent caching whole scene shadows.  ShadowMap allocations in a single frame can cause this to be exceeded."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GCachedShadowsCastFromMovablePrimitives = 1;
FAutoConsoleVariableRef CVarCachedWholeSceneShadowsCastFromMovablePrimitives(
	TEXT("r.Shadow.CachedShadowsCastFromMovablePrimitives"),
	GCachedShadowsCastFromMovablePrimitives,
	TEXT("Whether movable primitives should cast a shadow from cached whole scene shadows (movable point and spot lights).\n")
	TEXT("Disabling this can be used to remove the copy of the cached shadowmap."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

/** Can be used to visualize preshadow frustums when the shadowfrustums show flag is enabled. */
static TAutoConsoleVariable<int32> CVarDrawPreshadowFrustum(
	TEXT("r.Shadow.DrawPreshadowFrustums"),
	0,
	TEXT("visualize preshadow frustums when the shadowfrustums show flag is enabled"),
	ECVF_RenderThreadSafe
	);

/** Whether to allow preshadows (static world casting on character), can be disabled for debugging. */
static TAutoConsoleVariable<int32> CVarAllowPreshadows(
	TEXT("r.Shadow.Preshadows"),
	1,
	TEXT("Whether to allow preshadows (static world casting on character)"),
	ECVF_RenderThreadSafe
	);

/** Whether to allow per object shadows (character casting on world), can be disabled for debugging. */
static TAutoConsoleVariable<int32> CVarAllowPerObjectShadows(
	TEXT("r.Shadow.PerObject"),
	1,
	TEXT("Whether to render per object shadows (character casting on world)\n")
	TEXT("0: off\n")
	TEXT("1: on (default)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarShadowFadeExponent(
	TEXT("r.Shadow.FadeExponent"),
	0.25f,
	TEXT("Controls the rate at which shadows are faded out"),
	ECVF_RenderThreadSafe);

static int32 GShadowLightViewConvexHullCull = 1;
static FAutoConsoleVariableRef CVarShadowLightViewConvexHullCull(
	TEXT("r.Shadow.LightViewConvexHullCull"),
	GShadowLightViewConvexHullCull,
	TEXT("Enables culling of shadow casters that do not intersect the convex hull of the light origin and view frustum."),
	ECVF_RenderThreadSafe);

/**
 * Whether preshadows can be cached as an optimization.  
 * Disabling the caching through this setting is useful when debugging.
 */
static TAutoConsoleVariable<int32> CVarCachePreshadows(
	TEXT("r.Shadow.CachePreshadow"),
	1,
	TEXT("Whether preshadows can be cached as an optimization"),
	ECVF_RenderThreadSafe
	);

/**
 * NOTE: This flag is intended to be kept only as long as deemed neccessary to be sure that no artifacts were introduced.
 *       This allows a quick hot-fix to disable the change if need be.
 */
static TAutoConsoleVariable<int32> CVarResolutionScaleZeroDisablesSm(
	TEXT("r.Shadow.ResolutionScaleZeroDisablesSm"),
	1,
	TEXT("DEPRECATED: If 1 (default) then setting Shadow Resolution Scale to zero disables shadow maps for the light."),
	ECVF_RenderThreadSafe
);


bool ShouldUseCachePreshadows()
{
	return CVarCachePreshadows.GetValueOnRenderThread() != 0;
}

int32 GPreshadowsForceLowestLOD = 0;
FAutoConsoleVariableRef CVarPreshadowsForceLowestLOD(
	TEXT("r.Shadow.PreshadowsForceLowestDetailLevel"),
	GPreshadowsForceLowestLOD,
	TEXT("When enabled, static meshes render their lowest detail level into preshadow depth maps.  Disabled by default as it causes artifacts with poor quality LODs (tree billboard)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

/**
 * This value specifies how much bounds will be expanded when rendering a cached preshadow (0.15 = 15% larger).
 * Larger values result in more cache hits, but lower resolution and pull more objects into the depth pass.
 */
static TAutoConsoleVariable<float> CVarPreshadowExpandFraction(
	TEXT("r.Shadow.PreshadowExpand"),
	0.15f,
	TEXT("How much bounds will be expanded when rendering a cached preshadow (0.15 = 15% larger)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarPreShadowResolutionFactor(
	TEXT("r.Shadow.PreShadowResolutionFactor"),
	0.5f,
	TEXT("Mulitplier for preshadow resolution"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowTexelsPerPixel(
	TEXT("r.Shadow.TexelsPerPixel"),
	1.27324f,
	TEXT("The ratio of subject pixels to shadow texels for per-object shadows"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowTexelsPerPixelPointlight(
	TEXT("r.Shadow.TexelsPerPixelPointlight"),
	1.27324f,
	TEXT("The ratio of subject pixels to shadow texels for point lights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowTexelsPerPixelSpotlight(
	TEXT("r.Shadow.TexelsPerPixelSpotlight"),
	2.0f * 1.27324f,
	TEXT("The ratio of subject pixels to shadow texels for spotlights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowTexelsPerPixelRectlight(
	TEXT("r.Shadow.TexelsPerPixelRectlight"),
	1.27324f,
	TEXT("The ratio of subject pixels to shadow texels for rect lights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarPreShadowFadeResolution(
	TEXT("r.Shadow.PreShadowFadeResolution"),
	16,
	TEXT("Resolution in texels below which preshadows are faded out"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowFadeResolution(
	TEXT("r.Shadow.FadeResolution"),
	64,
	TEXT("Resolution in texels below which shadows are faded out"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMinShadowResolution(
	TEXT("r.Shadow.MinResolution"),
	32,
	TEXT("Minimum dimensions (in texels) allowed for rendering shadow subject depths"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMinPreShadowResolution(
	TEXT("r.Shadow.MinPreShadowResolution"),
	8,
	TEXT("Minimum dimensions (in texels) allowed for rendering preshadow depths"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUseConservativeShadowBounds(
	TEXT("r.Shadow.ConservativeBounds"),
	0,
	TEXT("Whether to use safe and conservative shadow frustum creation that wastes some shadowmap space"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarParallelGatherShadowPrimitives(
	TEXT("r.ParallelGatherShadowPrimitives"),
	1,  
	TEXT("Toggles parallel Gather shadow primitives. 0 = off; 1 = on"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarParallelGatherNumPrimitivesPerPacket(
	TEXT("r.ParallelGatherNumPrimitivesPerPacket"),
	256,  
	TEXT("Number of primitives per packet.  Only used when r.Shadow.UseOctreeForCulling is disabled."),
	ECVF_RenderThreadSafe
	);

int32 GUseOctreeForShadowCulling = 1;
FAutoConsoleVariableRef CVarUseOctreeForShadowCulling(
	TEXT("r.Shadow.UseOctreeForCulling"),
	GUseOctreeForShadowCulling,
	TEXT("Whether to use the primitive octree for shadow subject culling.  The octree culls large groups of primitives at a time, but introduces cache misses walking the data structure."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

CSV_DECLARE_CATEGORY_EXTERN(LightCount);

#if !UE_BUILD_SHIPPING
// read and written on the render thread
bool GDumpShadowSetup = false;
void DumpShadowDumpSetup()
{
	ENQUEUE_RENDER_COMMAND(DumpShadowDumpSetup)(
		[](FRHICommandList& RHICmdList)
		{
			GDumpShadowSetup = true;
		});
}

FAutoConsoleCommand CmdDumpShadowDumpSetup(
	TEXT("r.DumpShadows"),
	TEXT("Dump shadow setup (for developer only, only for non shiping build)"),
	FConsoleCommandDelegate::CreateStatic(DumpShadowDumpSetup)
	);
#endif // !UE_BUILD_SHIPPING

/** Whether to round the shadow map up to power of two on mobile platform. */
static TAutoConsoleVariable<int32> CVarMobileShadowmapRoundUpToPowerOfTwo(
	TEXT("r.Mobile.ShadowmapRoundUpToPowerOfTwo"),
	0,
	TEXT("Round the shadow map up to power of two on mobile platform, in case there is any compatibility issue.\n")
	TEXT(" 0: Disable (Default)\n")
	TEXT(" 1: Enabled"),
	ECVF_RenderThreadSafe
);

/**
 * Helper function to determine fade alpha value for shadows based on resolution. In the below ASCII art (1) is
 * the MinShadowResolution and (2) is the ShadowFadeResolution. Alpha will be 0 below the min resolution and 1
 * above the fade resolution. In between it is going to be an exponential curve with the values between (1) and (2)
 * being normalized in the 0..1 range.
 *
 *  
 *  |    /-------
 *  |  /
 *  |/
 *  1-----2-------
 *
 * @param	MaxUnclampedResolution		Requested resolution, unclamped so it can be below min
 * @param	ShadowFadeResolution		Resolution at which fade begins
 * @param	MinShadowResolution			Minimum resolution of shadow
 *
 * @return	fade value between 0 and 1
 */
float CalculateShadowFadeAlpha(const float MaxUnclampedResolution, const uint32 ShadowFadeResolution, const uint32 MinShadowResolution)
{
	// NB: MaxUnclampedResolution < 0 will return FadeAlpha = 0.0f. 

	float FadeAlpha = 0.0f;
	// Shadow size is above fading resolution.
	if (MaxUnclampedResolution > ShadowFadeResolution)
	{
		FadeAlpha = 1.0f;
	}
	// Shadow size is below fading resolution but above min resolution.
	else if (MaxUnclampedResolution > MinShadowResolution)
	{
		const float Exponent = CVarShadowFadeExponent.GetValueOnRenderThread();
		
		// Use the limit case ShadowFadeResolution = MinShadowResolution
		// to gracefully handle this case.
		if (MinShadowResolution >= ShadowFadeResolution)
		{
			const float SizeRatio = (float)(MaxUnclampedResolution - MinShadowResolution);
			FadeAlpha = 1.0f - FMath::Pow(SizeRatio, Exponent);
		} 
		else
		{
			const float InverseRange = 1.0f / (ShadowFadeResolution - MinShadowResolution);
			const float FirstFadeValue = FMath::Pow(InverseRange, Exponent);
			const float SizeRatio = (float)(MaxUnclampedResolution - MinShadowResolution) * InverseRange;
			// Rescale the fade alpha to reduce the change between no fading and the first value, which reduces popping with small ShadowFadeExponent's
			FadeAlpha = (FMath::Pow(SizeRatio, Exponent) - FirstFadeValue) / (1.0f - FirstFadeValue);
		}
	}
	return FadeAlpha;
}

typedef TArray<FVector,TInlineAllocator<8> > FBoundingBoxVertexArray;

/** Stores the indices for an edge of a bounding volume. */
struct FBoxEdge
{
	uint16 FirstEdgeIndex;
	uint16 SecondEdgeIndex;
	FBoxEdge(uint16 InFirst, uint16 InSecond) :
		FirstEdgeIndex(InFirst),
		SecondEdgeIndex(InSecond)
	{}
};

typedef TArray<FBoxEdge,TInlineAllocator<12> > FBoundingBoxEdgeArray;

/**
 * Creates an array of vertices and edges for a bounding box.
 * @param Box - The bounding box
 * @param OutVertices - Upon return, the array will contain the vertices of the bounding box.
 * @param OutEdges - Upon return, will contain indices of the edges of the bounding box.
 */
static void GetBoundingBoxVertices(const FBox& Box,FBoundingBoxVertexArray& OutVertices, FBoundingBoxEdgeArray& OutEdges)
{
	OutVertices.Empty(8);
	OutVertices.AddUninitialized(8);
	for(int32 X = 0;X < 2;X++)
	{
		for(int32 Y = 0;Y < 2;Y++)
		{
			for(int32 Z = 0;Z < 2;Z++)
			{
				OutVertices[X * 4 + Y * 2 + Z] = FVector(
					X ? Box.Min.X : Box.Max.X,
					Y ? Box.Min.Y : Box.Max.Y,
					Z ? Box.Min.Z : Box.Max.Z
					);
			}
		}
	}

	OutEdges.Empty(12);
	OutEdges.AddUninitialized(12);
	for(uint16 X = 0;X < 2;X++)
	{
		uint16 BaseIndex = X * 4;
		OutEdges[X * 4 + 0] = FBoxEdge(BaseIndex, BaseIndex + 1);
		OutEdges[X * 4 + 1] = FBoxEdge(BaseIndex + 1, BaseIndex + 3);
		OutEdges[X * 4 + 2] = FBoxEdge(BaseIndex + 3, BaseIndex + 2);
		OutEdges[X * 4 + 3] = FBoxEdge(BaseIndex + 2, BaseIndex);
	}
	for(uint16 XEdge = 0;XEdge < 4;XEdge++)
	{
		OutEdges[8 + XEdge] = FBoxEdge(XEdge, XEdge + 4);
	}
}

/**
 * Computes the transform contains a set of bounding box vertices and minimizes the pre-transform volume inside the post-transform clip space.
 * @param ZAxis - The Z axis of the transform.
 * @param Points - The points that represent the bounding volume.
 * @param Edges - The edges of the bounding volume.
 * @param OutAspectRatio - Upon successful return, contains the aspect ratio of the AABB; the ratio of width:height.
 * @param OutTransform - Upon successful return, contains the transform.
 * @return true if it successfully found a non-zero area projection of the bounding points.
 */
static bool GetBestShadowTransform(const FVector& ZAxis,const FBoundingBoxVertexArray& Points, const FBoundingBoxEdgeArray& Edges, float& OutAspectRatio, FMatrix& OutTransform)
{
	// Find the axis parallel to the edge between any two boundary points with the smallest projection of the bounds onto the axis.
	FVector XAxis(0,0,0);
	FVector YAxis(0,0,0);
	FVector Translation(0,0,0);
	float BestProjectedExtent = FLT_MAX;
	bool bValidProjection = false;

	// Cache unaliased pointers to point and edge data
	const FVector* RESTRICT PointsPtr = Points.GetData();
	const FBoxEdge* RESTRICT EdgesPtr = Edges.GetData();

	const int32 NumPoints = Points.Num();
	const int32 NumEdges = Edges.Num();

	// We're always dealing with box geometry here, so we can hint the compiler
	UE_ASSUME( NumPoints == 8 );
	UE_ASSUME( NumEdges == 12 );

	for(int32 EdgeIndex = 0;EdgeIndex < NumEdges; ++EdgeIndex)
	{
		const FVector Point = PointsPtr[EdgesPtr[EdgeIndex].FirstEdgeIndex];
		const FVector OtherPoint = PointsPtr[EdgesPtr[EdgeIndex].SecondEdgeIndex];
		const FVector PointDelta = OtherPoint - Point;
		const FVector TrialXAxis = (PointDelta - ZAxis * (PointDelta | ZAxis)).GetSafeNormal();
		const FVector TrialYAxis = (ZAxis ^ TrialXAxis).GetSafeNormal();

		// Calculate the size of the projection of the bounds onto this axis and an axis orthogonal to it and the Z axis.
		float MinProjectedX = FLT_MAX;
		float MaxProjectedX = -FLT_MAX;
		float MinProjectedY = FLT_MAX;
		float MaxProjectedY = -FLT_MAX;
		for(int32 ProjectedPointIndex = 0;ProjectedPointIndex < NumPoints; ++ProjectedPointIndex)
		{
			const float ProjectedX = PointsPtr[ProjectedPointIndex] | TrialXAxis;
			MinProjectedX = FMath::Min(MinProjectedX,ProjectedX);
			MaxProjectedX = FMath::Max(MaxProjectedX,ProjectedX);
			const float ProjectedY = PointsPtr[ProjectedPointIndex] | TrialYAxis;
			MinProjectedY = FMath::Min(MinProjectedY,ProjectedY);
			MaxProjectedY = FMath::Max(MaxProjectedY,ProjectedY);
		}

		float ProjectedExtentX;
		float ProjectedExtentY;
		if (CVarUseConservativeShadowBounds.GetValueOnRenderThread() != 0)
		{
			ProjectedExtentX = 2 * FMath::Max(FMath::Abs(MaxProjectedX), FMath::Abs(MinProjectedX));
			ProjectedExtentY = 2 * FMath::Max(FMath::Abs(MaxProjectedY), FMath::Abs(MinProjectedY));
		}
		else
		{
			ProjectedExtentX = MaxProjectedX - MinProjectedX;
			ProjectedExtentY = MaxProjectedY - MinProjectedY;
		}

		const float ProjectedExtent = ProjectedExtentX * ProjectedExtentY;
		if(ProjectedExtent < BestProjectedExtent - .05f 
			// Only allow projections with non-zero area
			&& ProjectedExtent > DELTA)
		{
			bValidProjection = true;
			BestProjectedExtent = ProjectedExtent;
			XAxis = TrialXAxis * 2.0f / ProjectedExtentX;
			YAxis = TrialYAxis * 2.0f / ProjectedExtentY;

			// Translating in post-transform clip space can cause the corners of the world space bounds to be outside of the transform generated by this function
			// This usually manifests in cinematics where the character's head is near the top of the bounds
			if (CVarUseConservativeShadowBounds.GetValueOnRenderThread() == 0)
			{
				Translation.X = (MinProjectedX + MaxProjectedX) * 0.5f;
				Translation.Y = (MinProjectedY + MaxProjectedY) * 0.5f;
			}

			if(ProjectedExtentY > ProjectedExtentX)
			{
				// Always make the X axis the largest one.
				Exchange(XAxis,YAxis);
				Exchange(Translation.X,Translation.Y);
				XAxis *= -1.0f;
				Translation.X *= -1.0f;
				OutAspectRatio = ProjectedExtentY / ProjectedExtentX;
			}
			else
			{
				OutAspectRatio = ProjectedExtentX / ProjectedExtentY;
			}
		}
	}

	// Only create the shadow if the projected extent of the given points has a non-zero area.
	if(bValidProjection && BestProjectedExtent > DELTA)
	{
		OutTransform = FBasisVectorMatrix(XAxis,YAxis,ZAxis,FVector(0,0,0)) * FTranslationMatrix(Translation);
		return true;
	}
	else
	{
		return false;
	}
}

FProjectedShadowInfo::FProjectedShadowInfo()
	: ShadowDepthView(NULL)
	, CacheMode(SDCM_Uncached)
	, DependentView(0)
	, ShadowId(INDEX_NONE)
	, PreShadowTranslation(0, 0, 0)
	, MaxSubjectZ(0)
	, MinSubjectZ(0)
	, ShadowBounds(0)
	, X(0)
	, Y(0)
	, ResolutionX(0)
	, ResolutionY(0)
	, BorderSize(0)
	, MaxScreenPercent(1.0f)
	, bAllocated(false)
	, bRendered(false)
	, bAllocatedInPreshadowCache(false)
	, bDepthsCached(false)
	, bDirectionalLight(false)
	, bOnePassPointLightShadow(false)
	, bWholeSceneShadow(false)
	, bReflectiveShadowmap(false)
	, bTranslucentShadow(false)
	, bRayTracedDistanceField(false)
	, bCapsuleShadow(false)
	, bPreShadow(false)
	, bSelfShadowOnly(false)
	, bPerObjectOpaqueShadow(false)
	, bTransmission(false)
	, bHairStrandsDeepShadow(false)
	, PerObjectShadowFadeStart(WORLD_MAX)
	, InvPerObjectShadowFadeLength(0.0f)
	, LightSceneInfo(0)
	, ParentSceneInfo(0)
	, NumDynamicSubjectMeshElements(0)
	, NumSubjectMeshCommandBuildRequestElements(0)
	, ShaderDepthBias(0.0f)
	, ShaderSlopeDepthBias(0.0f)
{
}

/** Shadow border needs to be wide enough to prevent the shadow filtering from picking up content in other shadowmaps in the atlas. */
const static uint32 SHADOW_BORDER = 4; 

bool FProjectedShadowInfo::SetupPerObjectProjection(
	FLightSceneInfo* InLightSceneInfo,
	const FPrimitiveSceneInfo* InParentSceneInfo,
	const FPerObjectProjectedShadowInitializer& Initializer,
	bool bInPreShadow,
	uint32 InResolutionX,
	uint32 MaxShadowResolutionY,
	uint32 InBorderSize,
	float InMaxScreenPercent,
	bool bInTranslucentShadow)
{
	check(InParentSceneInfo);

	LightSceneInfo = InLightSceneInfo;
	LightSceneInfoCompact = InLightSceneInfo;
	ParentSceneInfo = InParentSceneInfo;
	PreShadowTranslation = Initializer.PreShadowTranslation;
	ShadowBounds = FSphere(Initializer.SubjectBounds.Origin - Initializer.PreShadowTranslation, Initializer.SubjectBounds.SphereRadius);
	ResolutionX = InResolutionX;
	BorderSize = InBorderSize;
	MaxScreenPercent = InMaxScreenPercent;
	bDirectionalLight = InLightSceneInfo->Proxy->GetLightType() == LightType_Directional;
	const ERHIFeatureLevel::Type FeatureLevel = LightSceneInfo->Scene->GetFeatureLevel();
	bCapsuleShadow = InParentSceneInfo->Proxy->CastsCapsuleDirectShadow() && !bInPreShadow && SupportsCapsuleDirectShadows(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel]);
	bTranslucentShadow = bInTranslucentShadow;
	bPreShadow = bInPreShadow;
	bSelfShadowOnly = InParentSceneInfo->Proxy->CastsSelfShadowOnly();
	bTransmission = InLightSceneInfo->Proxy->Transmission();
	bHairStrandsDeepShadow = InLightSceneInfo->Proxy->CastsHairStrandsDeepShadow();

	check(!bRayTracedDistanceField);

	const FMatrix WorldToLightScaled = Initializer.WorldToLight * FScaleMatrix(Initializer.Scales);
	
	// Create an array of the extreme vertices of the subject's bounds.
	FBoundingBoxVertexArray BoundsPoints;
	FBoundingBoxEdgeArray BoundsEdges;
	GetBoundingBoxVertices(Initializer.SubjectBounds.GetBox(),BoundsPoints,BoundsEdges);

	// Project the bounding box vertices.
	FBoundingBoxVertexArray ProjectedBoundsPoints;
	for (int32 PointIndex = 0; PointIndex < BoundsPoints.Num(); PointIndex++)
	{
		const FVector TransformedBoundsPoint = WorldToLightScaled.TransformPosition(BoundsPoints[PointIndex]);
		const float TransformedBoundsPointW = Dot4(FVector4(0, 0, TransformedBoundsPoint | Initializer.FaceDirection,1), Initializer.WAxis);
		if (TransformedBoundsPointW >= DELTA)
		{
			ProjectedBoundsPoints.Add(TransformedBoundsPoint / TransformedBoundsPointW);
		}
		else
		{
			//ProjectedBoundsPoints.Add(FVector(FLT_MAX, FLT_MAX, FLT_MAX));
			return false;
		}
	}

	// Compute the transform from light-space to shadow-space.
	FMatrix LightToShadow;
	float AspectRatio;
	
	// if this is a valid transform (can be false if the object is around the light)
	bool bRet = false;

	if (GetBestShadowTransform(Initializer.FaceDirection.GetSafeNormal(), ProjectedBoundsPoints, BoundsEdges, AspectRatio, LightToShadow))
	{
		bRet = true;
		const FMatrix WorldToShadow = WorldToLightScaled * LightToShadow;

		const FBox ShadowSubjectBounds = Initializer.SubjectBounds.GetBox().TransformBy(WorldToShadow);

		MinSubjectZ = FMath::Max(Initializer.MinLightW, ShadowSubjectBounds.Min.Z);
		float MaxReceiverZ = FMath::Min(MinSubjectZ + Initializer.MaxDistanceToCastInLightW, (float)HALF_WORLD_MAX);
		// Max can end up smaller than min due to the clamp to HALF_WORLD_MAX above
		MaxReceiverZ = FMath::Max(MaxReceiverZ, MinSubjectZ + 1);
		MaxSubjectZ = FMath::Max(ShadowSubjectBounds.Max.Z, MinSubjectZ + 1);

		const FMatrix SubjectMatrix = WorldToShadow * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
		const float MaxSubjectAndReceiverDepth = Initializer.SubjectBounds.GetBox().TransformBy(SubjectMatrix).Max.Z;

		float MaxSubjectDepth;

		if (bPreShadow)
		{
			const FMatrix PreSubjectMatrix = WorldToShadow * FShadowProjectionMatrix(Initializer.MinLightW, MaxSubjectZ, Initializer.WAxis);
			// Preshadow frustum bounds go from the light to the furthest extent of the object in light space
			SubjectAndReceiverMatrix = PreSubjectMatrix;
			ReceiverMatrix = SubjectMatrix;
			MaxSubjectDepth = bDirectionalLight ? MaxSubjectAndReceiverDepth : Initializer.SubjectBounds.GetBox().TransformBy(PreSubjectMatrix).Max.Z;
		}
		else
		{
			const FMatrix PostSubjectMatrix = WorldToShadow * FShadowProjectionMatrix(MinSubjectZ, MaxReceiverZ, Initializer.WAxis);
			SubjectAndReceiverMatrix = SubjectMatrix;
			ReceiverMatrix = PostSubjectMatrix;
			MaxSubjectDepth = MaxSubjectAndReceiverDepth;

			if (bDirectionalLight)
			{
				// No room to fade out if the end of receiver range is inside the subject range, it will just clip.
				if (MaxSubjectZ < MaxReceiverZ)
				{
					float ShadowSubjectRange = MaxSubjectZ - MinSubjectZ;
					float FadeLength = FMath::Min(ShadowSubjectRange, MaxReceiverZ - MaxSubjectZ);
					//Initializer.MaxDistanceToCastInLightW / 16.0f;
					PerObjectShadowFadeStart = (MaxReceiverZ - MinSubjectZ - FadeLength) / ShadowSubjectRange;
					InvPerObjectShadowFadeLength = ShadowSubjectRange / FMath::Max(0.000001f, FadeLength);
				}
			}
		}

		InvMaxSubjectDepth = 1.0f / MaxSubjectDepth;

		MinPreSubjectZ = Initializer.MinLightW;

		ResolutionY = FMath::Clamp<uint32>(FMath::TruncToInt(InResolutionX / AspectRatio), 1, MaxShadowResolutionY);

		if (ResolutionX == 0 || ResolutionY == 0)
		{
			bRet = false;
		}
		else
		{
			// Store the view matrix
			// Reorder the vectors to match the main view, since ShadowViewMatrix will be used to override the main view's view matrix during shadow depth rendering
			ShadowViewMatrix = Initializer.WorldToLight *
				FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));

			GetViewFrustumBounds(CasterFrustum, SubjectAndReceiverMatrix, true);

			InvReceiverMatrix = ReceiverMatrix.InverseFast();
			GetViewFrustumBounds(ReceiverFrustum, ReceiverMatrix, true);
			UpdateShaderDepthBias();
		}
	}

	return bRet;
}

void FProjectedShadowInfo::SetupWholeSceneProjection(
	FLightSceneInfo* InLightSceneInfo,
	FViewInfo* InDependentView,
	const FWholeSceneProjectedShadowInitializer& Initializer,
	uint32 InResolutionX,
	uint32 InResolutionY,
	uint32 InBorderSize,
	bool bInReflectiveShadowMap)
{	
	LightSceneInfo = InLightSceneInfo;
	LightSceneInfoCompact = InLightSceneInfo;
	DependentView = InDependentView;
	PreShadowTranslation = Initializer.PreShadowTranslation;
	CascadeSettings = Initializer.CascadeSettings;
	ResolutionX = InResolutionX;
	ResolutionY = InResolutionY;
	bDirectionalLight = InLightSceneInfo->Proxy->GetLightType() == LightType_Directional;
	bOnePassPointLightShadow = Initializer.bOnePassPointLightShadow;
	bRayTracedDistanceField = Initializer.bRayTracedDistanceField;
	bWholeSceneShadow = true;
	bTransmission = InLightSceneInfo->Proxy->Transmission();
	bHairStrandsDeepShadow = InLightSceneInfo->Proxy->CastsHairStrandsDeepShadow();
	bReflectiveShadowmap = bInReflectiveShadowMap; 
	BorderSize = InBorderSize;

	FVector	XAxis, YAxis;
	Initializer.FaceDirection.FindBestAxisVectors(XAxis,YAxis);
	const FMatrix WorldToLightScaled = Initializer.WorldToLight * FScaleMatrix(Initializer.Scales);
	const FMatrix WorldToFace = WorldToLightScaled * FBasisVectorMatrix(-XAxis,YAxis,Initializer.FaceDirection.GetSafeNormal(),FVector::ZeroVector);

	MaxSubjectZ = WorldToFace.TransformPosition(Initializer.SubjectBounds.Origin).Z + Initializer.SubjectBounds.SphereRadius;
	MinSubjectZ = FMath::Max(MaxSubjectZ - Initializer.SubjectBounds.SphereRadius * 2,Initializer.MinLightW);

	if(bInReflectiveShadowMap)
	{
		check(!bOnePassPointLightShadow);
		check(!CascadeSettings.ShadowSplitIndex);

		// Quantise the RSM in shadow texel space
		static bool bQuantize = true;
		if ( bQuantize )
		{
			// Transform the shadow's position into shadowmap space
			const FVector TransformedPosition = WorldToFace.TransformPosition(-PreShadowTranslation);

			// Largest amount that the shadowmap will be downsampled to during sampling
			// We need to take this into account when snapping to get a stable result
			// This corresponds to the maximum kernel filter size used by subsurface shadows in ShadowProjectionPixelShader.usf
			static int32 MaxDownsampleFactor = 4;
			// Determine the distance necessary to snap the shadow's position to the nearest texel
			const float SnapX = FMath::Fmod(TransformedPosition.X, 2.0f * MaxDownsampleFactor / InResolutionX);
			const float SnapY = FMath::Fmod(TransformedPosition.Y, 2.0f * MaxDownsampleFactor / InResolutionY);
			// Snap the shadow's position and transform it back into world space
			// This snapping prevents sub-texel camera movements which removes view dependent aliasing from the final shadow result
			// This only maintains stable shadows under camera translation and rotation
			const FVector SnappedWorldPosition = WorldToFace.InverseFast().TransformPosition(TransformedPosition - FVector(SnapX, SnapY, 0.0f));
			PreShadowTranslation = -SnappedWorldPosition;
		}

		ShadowBounds = FSphere(-PreShadowTranslation, Initializer.SubjectBounds.SphereRadius);

		GetViewFrustumBounds(CasterFrustum, SubjectAndReceiverMatrix, true);
	}
	else
	{
		if(bDirectionalLight)
		{
			// Limit how small the depth range can be for smaller cascades
			// This is needed for shadow modes like subsurface shadows which need depth information outside of the smaller cascade depth range
			//@todo - expose this value to the ini
			const float DepthRangeClamp = 5000;
			MaxSubjectZ = FMath::Max(MaxSubjectZ, DepthRangeClamp);
			MinSubjectZ = FMath::Min(MinSubjectZ, -DepthRangeClamp);

			// Transform the shadow's position into shadowmap space
			const FVector TransformedPosition = WorldToFace.TransformPosition(-PreShadowTranslation);

			// Largest amount that the shadowmap will be downsampled to during sampling
			// We need to take this into account when snapping to get a stable result
			// This corresponds to the maximum kernel filter size used by subsurface shadows in ShadowProjectionPixelShader.usf
			const int32 MaxDownsampleFactor = 4;
			// Determine the distance necessary to snap the shadow's position to the nearest texel
			const float SnapX = FMath::Fmod(TransformedPosition.X, 2.0f * MaxDownsampleFactor / InResolutionX);
			const float SnapY = FMath::Fmod(TransformedPosition.Y, 2.0f * MaxDownsampleFactor / InResolutionY);
			// Snap the shadow's position and transform it back into world space
			// This snapping prevents sub-texel camera movements which removes view dependent aliasing from the final shadow result
			// This only maintains stable shadows under camera translation and rotation
			const FVector SnappedWorldPosition = WorldToFace.InverseFast().TransformPosition(TransformedPosition - FVector(SnapX, SnapY, 0.0f));
			PreShadowTranslation = -SnappedWorldPosition;
		}

		if (CascadeSettings.ShadowSplitIndex >= 0 && bDirectionalLight)
		{
			checkSlow(InDependentView);

			ShadowBounds = InLightSceneInfo->Proxy->GetShadowSplitBounds(
				*InDependentView, 
				bRayTracedDistanceField ? INDEX_NONE : CascadeSettings.ShadowSplitIndex, 
				InLightSceneInfo->IsPrecomputedLightingValid(), 
				0);
		}
		else
		{
			ShadowBounds = FSphere(-Initializer.PreShadowTranslation, Initializer.SubjectBounds.SphereRadius);
		}

		// Any meshes between the light and the subject can cast shadows, also any meshes inside the subject region
		const FMatrix CasterMatrix = WorldToFace * FShadowProjectionMatrix(Initializer.MinLightW, MaxSubjectZ, Initializer.WAxis);
		GetViewFrustumBounds(CasterFrustum, CasterMatrix, true);
	}

	checkf(MaxSubjectZ > MinSubjectZ, TEXT("MaxSubjectZ %f MinSubjectZ %f SubjectBounds.SphereRadius %f"), MaxSubjectZ, MinSubjectZ, Initializer.SubjectBounds.SphereRadius);

	MinPreSubjectZ = Initializer.MinLightW;

	SubjectAndReceiverMatrix = WorldToFace * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
	// For CSM the subject is the same as the receiver (i.e., the cascade bounds)
	ReceiverMatrix = SubjectAndReceiverMatrix;

	float MaxSubjectDepth = SubjectAndReceiverMatrix.TransformPosition(
		Initializer.SubjectBounds.Origin
		+ WorldToLightScaled.InverseFast().TransformVector(Initializer.FaceDirection) * Initializer.SubjectBounds.SphereRadius
		).Z;

	if (bOnePassPointLightShadow)
	{
		MaxSubjectDepth = Initializer.SubjectBounds.SphereRadius;
	}

	InvMaxSubjectDepth = 1.0f / MaxSubjectDepth;

	// Store the view matrix
	// Reorder the vectors to match the main view, since ShadowViewMatrix will be used to override the main view's view matrix during shadow depth rendering
	ShadowViewMatrix = Initializer.WorldToLight * 
		FMatrix(
		FPlane(0,	0,	1,	0),
		FPlane(1,	0,	0,	0),
		FPlane(0,	1,	0,	0),
		FPlane(0,	0,	0,	1));

	InvReceiverMatrix = ReceiverMatrix.InverseFast();

	GetViewFrustumBounds(ReceiverFrustum, ReceiverMatrix, true);

	UpdateShaderDepthBias();
}

void FProjectedShadowInfo::AddCachedMeshDrawCommandsForPass(
	int32 PrimitiveIndex,
	const FPrimitiveSceneInfo* InPrimitiveSceneInfo,
	const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance,
	const FStaticMeshBatch& StaticMesh,
	const FScene* Scene,
	EMeshPass::Type PassType,
	FMeshCommandOneFrameArray& VisibleMeshCommands,
	TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests,
	int32& NumMeshCommandBuildRequestElements)
{
	const EShadingPath ShadingPath = Scene->GetShadingPath();
	const bool bUseCachedMeshCommand = UseCachedMeshDrawCommands()
		&& !!(FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands)
		&& StaticMeshRelevance.bSupportsCachingMeshDrawCommands;

	if (bUseCachedMeshCommand)
	{
		const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(PassType);
		if (StaticMeshCommandInfoIndex >= 0)
		{
			const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = InPrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];
			const FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];
			const FMeshDrawCommand* MeshDrawCommand = CachedMeshDrawCommand.StateBucketId >= 0
					? &Scene->CachedMeshDrawCommandStateBuckets[PassType].GetByElementId(CachedMeshDrawCommand.StateBucketId).Key
					: &SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

			FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

			NewVisibleMeshDrawCommand.Setup(
				MeshDrawCommand,
				PrimitiveIndex,
				PrimitiveIndex,
				CachedMeshDrawCommand.StateBucketId,
				CachedMeshDrawCommand.MeshFillMode,
				CachedMeshDrawCommand.MeshCullMode,
				CachedMeshDrawCommand.SortKey);

			VisibleMeshCommands.Add(NewVisibleMeshDrawCommand);
		}
	}
	else
	{
		NumMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
		MeshCommandBuildRequests.Add(&StaticMesh);
	}
}

struct FAddSubjectPrimitiveOverflowedIndices
{
	TArray<uint16> MDCIndices;
	TArray<uint16> MeshIndices;
};

struct FFinalizeAddSubjectPrimitiveContext
{
	const uint16* OverflowedMDCIndices;
	const uint16* OverflowedMeshIndices;
};

struct FAddSubjectPrimitiveResult
{
	union
	{
		uint64 Qword;
		struct
		{
			uint32 bCopyCachedMeshDrawCommand : 1;
			uint32 bRequestMeshCommandBuild : 1;
			uint32 bOverflowed : 1;
			uint32 bDynamicSubjectPrimitive : 1;
			uint32 bTranslucentSubjectPrimitive : 1;
			uint32 bNeedUniformBufferUpdate : 1;
			uint32 bNeedUpdateStaticMeshes : 1;
			uint32 bNeedPrimitiveFadingStateUpdate : 1;
			uint32 bFadingIn : 1;
			uint32 bAddOnRenderThread : 1;
			uint32 bRecordShadowSubjectsForMobile : 1;

			union
			{
				uint16 MDCOrMeshIndices[2];
				struct 
				{
					uint16 NumMDCIndices;
					uint16 NumMeshIndices;
				};
			};
		};
	};

	void AcceptMDC(int32 NumAcceptedStaticMeshes, int32 MDCIdx, FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer)
	{
		check(NumAcceptedStaticMeshes >= 0 && MDCIdx < MAX_uint16);
		if (NumAcceptedStaticMeshes < 2)
		{
			MDCOrMeshIndices[NumAcceptedStaticMeshes] = uint16(MDCIdx + 1);
			if (bRequestMeshCommandBuild)
			{
				const uint16 Tmp = MDCOrMeshIndices[1];
				MDCOrMeshIndices[1] = MDCOrMeshIndices[0];
				MDCOrMeshIndices[0] = Tmp;
			}
		}
		else
		{
			if (NumAcceptedStaticMeshes == 2)
			{
				HandleOverflow(OverflowBuffer);
			}
			check(bOverflowed);
			OverflowBuffer.MDCIndices.Add(MDCIdx);
			++NumMDCIndices;
		}
		bCopyCachedMeshDrawCommand = true;
	}

	void AcceptMesh(int32 NumAcceptedStaticMeshes, int32 MeshIdx, FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer)
	{
		check(NumAcceptedStaticMeshes >= 0 && MeshIdx < MAX_uint16);
		if (NumAcceptedStaticMeshes < 2)
		{
			MDCOrMeshIndices[NumAcceptedStaticMeshes] = uint16(MeshIdx + 1);
		}
		else
		{
			if (NumAcceptedStaticMeshes == 2)
			{
				HandleOverflow(OverflowBuffer);
			}
			check(bOverflowed);
			OverflowBuffer.MeshIndices.Add(MeshIdx);
			++NumMeshIndices;
		}
		bRequestMeshCommandBuild = true;
	}

	int32 GetMDCIndices(FFinalizeAddSubjectPrimitiveContext& Context, const uint16*& OutMDCIndices, int32& OutIdxBias) const
	{
		int32 NumMDCs;
		OutIdxBias = -1;
		if (bOverflowed)
		{
			OutMDCIndices = Context.OverflowedMDCIndices;
			NumMDCs = NumMDCIndices;
			check(NumMDCs > 0);
			Context.OverflowedMDCIndices += NumMDCs;
			OutIdxBias = 0;
		}
		else
		{
			OutMDCIndices = MDCOrMeshIndices;
			NumMDCs = !MDCOrMeshIndices[1] ? 1 : (!bRequestMeshCommandBuild ? 2 : 1);
		}
		return NumMDCs;
	}

	int32 GetMeshIndices(FFinalizeAddSubjectPrimitiveContext& Context, const uint16*& OutMeshIndices, int32& OutIdxBias) const
	{
		int32 NumMeshes;
		OutIdxBias = -1;
		if (bOverflowed)
		{
			OutMeshIndices = Context.OverflowedMeshIndices;
			NumMeshes = NumMeshIndices;
			check(NumMeshes > 0);
			Context.OverflowedMeshIndices += NumMeshes;
			OutIdxBias = 0;
		}
		else if (!bCopyCachedMeshDrawCommand)
		{
			OutMeshIndices = MDCOrMeshIndices;
			NumMeshes = !MDCOrMeshIndices[1] ? 1 : 2;
		}
		else
		{
			OutMeshIndices = &MDCOrMeshIndices[1];
			NumMeshes = 1;
		}
		return NumMeshes;
	}

private:
	void HandleOverflow(FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer)
	{
		if (bCopyCachedMeshDrawCommand && !bRequestMeshCommandBuild)
		{
			OverflowBuffer.MDCIndices.Add(MDCOrMeshIndices[0] - 1);
			OverflowBuffer.MDCIndices.Add(MDCOrMeshIndices[1] - 1);
			NumMDCIndices = 2;
			NumMeshIndices = 0;
		}
		else if (bCopyCachedMeshDrawCommand)
		{
			OverflowBuffer.MDCIndices.Add(MDCOrMeshIndices[0] - 1);
			OverflowBuffer.MeshIndices.Add(MDCOrMeshIndices[1] - 1);
			NumMDCIndices = 1;
			NumMeshIndices = 1;
		}
		else
		{
			check(bRequestMeshCommandBuild);
			OverflowBuffer.MeshIndices.Add(MDCOrMeshIndices[0] - 1);
			OverflowBuffer.MeshIndices.Add(MDCOrMeshIndices[1] - 1);
			NumMDCIndices = 0;
			NumMeshIndices = 2;
		}
		bOverflowed = true;
	}
};

static_assert(sizeof(FAddSubjectPrimitiveResult) == 8, "Unexpected size for FAddSubjectPrimitiveResult");

struct FAddSubjectPrimitiveOp
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo;
	FAddSubjectPrimitiveResult Result;
};

struct FAddSubjectPrimitiveStats
{
	int32 NumCachedMDCCopies;
	int32 NumMDCBuildRequests;
	int32 NumDynamicSubs;
	int32 NumTranslucentSubs;
	int32 NumDeferredPrimitives;

	FAddSubjectPrimitiveStats()
		: NumCachedMDCCopies(0)
		, NumMDCBuildRequests(0)
		, NumDynamicSubs(0)
		, NumTranslucentSubs(0)
		, NumDeferredPrimitives(0)
	{}

	void InterlockedAdd(const FAddSubjectPrimitiveStats& Other)
	{
		if (Other.NumCachedMDCCopies > 0)
		{
			FPlatformAtomics::InterlockedAdd(&NumCachedMDCCopies, Other.NumCachedMDCCopies);
		}
		if (Other.NumMDCBuildRequests > 0)
		{
			FPlatformAtomics::InterlockedAdd(&NumMDCBuildRequests, Other.NumMDCBuildRequests);
		}
		if (Other.NumDynamicSubs > 0)
		{
			FPlatformAtomics::InterlockedAdd(&NumDynamicSubs, Other.NumDynamicSubs);
		}
		if (Other.NumTranslucentSubs > 0)
		{
			FPlatformAtomics::InterlockedAdd(&NumTranslucentSubs, Other.NumTranslucentSubs);
		}
		if (Other.NumDeferredPrimitives > 0)
		{
			FPlatformAtomics::InterlockedAdd(&NumDeferredPrimitives, Other.NumDeferredPrimitives);
		}
	}
};

void FProjectedShadowInfo::AddCachedMeshDrawCommands_AnyThread(
	const FScene* Scene,
	const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance,
	int32 StaticMeshIdx,
	int32& NumAcceptedStaticMeshes,
	FAddSubjectPrimitiveResult& OutResult,
	FAddSubjectPrimitiveStats& OutStats,
	FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const
{
	const EMeshPass::Type PassType = EMeshPass::CSMShadowDepth;
	const EShadingPath ShadingPath = Scene->GetShadingPath();
	const bool bUseCachedMeshCommand = UseCachedMeshDrawCommands_AnyThread()
		&& !!(FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands)
		&& StaticMeshRelevance.bSupportsCachingMeshDrawCommands;

	if (bUseCachedMeshCommand)
	{
		const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(PassType);
		if (StaticMeshCommandInfoIndex >= 0)
		{
			++OutStats.NumCachedMDCCopies;
			OutResult.AcceptMDC(NumAcceptedStaticMeshes++, StaticMeshCommandInfoIndex, OverflowBuffer);
		}
	}
	else
	{
		++OutStats.NumMDCBuildRequests;
		OutResult.AcceptMesh(NumAcceptedStaticMeshes++, StaticMeshIdx, OverflowBuffer);
	}
}

bool FProjectedShadowInfo::ShouldDrawStaticMeshes(FViewInfo& InCurrentView, FPrimitiveSceneInfo* InPrimitiveSceneInfo)
{
	bool WholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();
	bool bDrawingStaticMeshes = false;
	int32 PrimitiveId = InPrimitiveSceneInfo->GetIndex();

	{
		const int32 ForcedLOD = (InCurrentView.Family->EngineShowFlags.LOD) ? (GetCVarForceLODShadow() != -1 ? GetCVarForceLODShadow() : GetCVarForceLOD()) : -1;
		const FLODMask* VisibilePrimitiveLODMask = nullptr;

		if (InCurrentView.PrimitivesLODMask[PrimitiveId].ContainsLOD(MAX_int8)) // only calculate it if it's not set
		{
			FLODMask ViewLODToRender;
			float MeshScreenSizeSquared = 0;
			const int8 CurFirstLODIdx = InPrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();

			const FBoxSphereBounds& Bounds = InPrimitiveSceneInfo->Proxy->GetBounds();
			const float LODScale = InCurrentView.LODDistanceFactor * GetCachedScalabilityCVars().StaticMeshLODDistanceScale;
			ViewLODToRender = ComputeLODForMeshes(InPrimitiveSceneInfo->StaticMeshRelevances, InCurrentView, Bounds.Origin, Bounds.SphereRadius, ForcedLOD, MeshScreenSizeSquared, CurFirstLODIdx, LODScale);

			InCurrentView.PrimitivesLODMask[PrimitiveId] = ViewLODToRender;
		}

		VisibilePrimitiveLODMask = &InCurrentView.PrimitivesLODMask[PrimitiveId];
		check(VisibilePrimitiveLODMask != nullptr);

		FLODMask ShadowLODToRender = *VisibilePrimitiveLODMask;

		// Use lowest LOD for PreShadow
		if (bReflectiveShadowmap || (bPreShadow && GPreshadowsForceLowestLOD))
		{
			int8 LODToRenderScan = -MAX_int8;
			FLODMask LODToRender;

			for (int32 Index = 0; Index < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); Index++)
			{
				LODToRenderScan = FMath::Max<int8>(InPrimitiveSceneInfo->StaticMeshRelevances[Index].LODIndex, LODToRenderScan);
			}
			if (LODToRenderScan != -MAX_int8)
			{
				ShadowLODToRender.SetLOD(LODToRenderScan);
			}
		}

		if (CascadeSettings.bFarShadowCascade)
		{
			extern ENGINE_API int32 GFarShadowStaticMeshLODBias;
			int8 LODToRenderScan = ShadowLODToRender.DitheredLODIndices[0] + GFarShadowStaticMeshLODBias;

			for (int32 Index = InPrimitiveSceneInfo->StaticMeshRelevances.Num() - 1; Index >= 0; Index--)
			{
				if (LODToRenderScan == InPrimitiveSceneInfo->StaticMeshRelevances[Index].LODIndex)
				{
					ShadowLODToRender.SetLOD(LODToRenderScan);
					break;
				}
			}
		}

		if (WholeSceneDirectionalShadow)
		{
			// Don't cache if it requires per view per mesh state for distance cull fade.
			const bool bIsPrimitiveDistanceCullFading = InCurrentView.PotentiallyFadingPrimitiveMap[InPrimitiveSceneInfo->GetIndex()];
			const bool bCanCache = !bIsPrimitiveDistanceCullFading && !InPrimitiveSceneInfo->NeedsUpdateStaticMeshes();

			for (int32 MeshIndex = 0; MeshIndex < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
			{
				const FStaticMeshBatchRelevance& StaticMeshRelevance = InPrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				const FStaticMeshBatch& StaticMesh = InPrimitiveSceneInfo->StaticMeshes[MeshIndex];

				if ((StaticMeshRelevance.CastShadow || (bSelfShadowOnly && StaticMeshRelevance.bUseForDepthPass)) && ShadowLODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
				{
					if (GetShadowDepthType() == CSMShadowDepthType && bCanCache)
					{
						AddCachedMeshDrawCommandsForPass(
							PrimitiveId,
							InPrimitiveSceneInfo,
							StaticMeshRelevance,
							StaticMesh,
							InPrimitiveSceneInfo->Scene,
							EMeshPass::CSMShadowDepth,
							ShadowDepthPassVisibleCommands,
							SubjectMeshCommandBuildRequests,
							NumSubjectMeshCommandBuildRequestElements);
					}
					else
					{
						NumSubjectMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
						SubjectMeshCommandBuildRequests.Add(&StaticMesh);
					}

					bDrawingStaticMeshes = true;
				}
			}
		}
		else
		{
			for (int32 MeshIndex = 0; MeshIndex < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
			{
				const FStaticMeshBatchRelevance& StaticMeshRelevance = InPrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				const FStaticMeshBatch& StaticMesh = InPrimitiveSceneInfo->StaticMeshes[MeshIndex];

				if ((StaticMeshRelevance.CastShadow || (bSelfShadowOnly && StaticMeshRelevance.bUseForDepthPass)) && ShadowLODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
				{
					NumSubjectMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
					SubjectMeshCommandBuildRequests.Add(&StaticMesh);

					bDrawingStaticMeshes = true;
				}
			}
		}
	}

	return bDrawingStaticMeshes;
}

bool FProjectedShadowInfo::ShouldDrawStaticMeshes_AnyThread(
	FViewInfo& CurrentView,
	const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact,
	bool bMayBeFading,
	bool bNeedUpdateStaticMeshes,
	FAddSubjectPrimitiveResult& OutResult,
	FAddSubjectPrimitiveStats& OutStats,
	FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const
{
	bool bDrawingStaticMeshes = false;
	const bool WholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();
	const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneInfoCompact.PrimitiveSceneInfo;
	const FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfoCompact.Proxy;
	const int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();

	{
		const int32 ForcedLOD = CurrentView.Family->EngineShowFlags.LOD ? (GetCVarForceLODShadow_AnyThread() != -1 ? GetCVarForceLODShadow_AnyThread() : GetCVarForceLOD_AnyThread()) : -1;
		const FLODMask* VisibilePrimitiveLODMask = nullptr;

		if (CurrentView.PrimitivesLODMask[PrimitiveId].ContainsLOD(MAX_int8)) // only calculate it if it's not set
		{
			FLODMask ViewLODToRender;
			float MeshScreenSizeSquared = 0;
			const int8 CurFirstLODIdx = Proxy->GetCurrentFirstLODIdx_RenderThread();

			const FBoxSphereBounds& Bounds = PrimitiveSceneInfoCompact.Bounds;
			const float LODScale = CurrentView.LODDistanceFactor * GetCachedScalabilityCVars().StaticMeshLODDistanceScale;
			ViewLODToRender = ComputeLODForMeshes(PrimitiveSceneInfo->StaticMeshRelevances, CurrentView, Bounds.Origin, Bounds.SphereRadius, ForcedLOD, MeshScreenSizeSquared, CurFirstLODIdx, LODScale);

			CurrentView.PrimitivesLODMask[PrimitiveId] = ViewLODToRender;
		}

		VisibilePrimitiveLODMask = &CurrentView.PrimitivesLODMask[PrimitiveId];
		check(VisibilePrimitiveLODMask != nullptr);

		FLODMask ShadowLODToRender = *VisibilePrimitiveLODMask;

		// Use lowest LOD for PreShadow
		if (bReflectiveShadowmap || (bPreShadow && GPreshadowsForceLowestLOD))
		{
			int8 LODToRenderScan = -MAX_int8;
			FLODMask LODToRender;

			for (int32 Index = 0; Index < PrimitiveSceneInfo->StaticMeshRelevances.Num(); Index++)
			{
				LODToRenderScan = FMath::Max<int8>(PrimitiveSceneInfo->StaticMeshRelevances[Index].LODIndex, LODToRenderScan);
			}
			if (LODToRenderScan != -MAX_int8)
			{
				ShadowLODToRender.SetLOD(LODToRenderScan);
			}
		}

		if (CascadeSettings.bFarShadowCascade)
		{
			extern ENGINE_API int32 GFarShadowStaticMeshLODBias;
			int8 LODToRenderScan = ShadowLODToRender.DitheredLODIndices[0] + GFarShadowStaticMeshLODBias;

			for (int32 Index = PrimitiveSceneInfo->StaticMeshRelevances.Num() - 1; Index >= 0; Index--)
			{
				if (LODToRenderScan == PrimitiveSceneInfo->StaticMeshRelevances[Index].LODIndex)
				{
					ShadowLODToRender.SetLOD(LODToRenderScan);
					break;
				}
			}
		}

		if (WholeSceneDirectionalShadow)
		{
			// Don't cache if it requires per view per mesh state for distance cull fade.
			const bool bCanCache = !bMayBeFading && !bNeedUpdateStaticMeshes;
			int32 NumAcceptedStaticMeshes = 0;

			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
			{
				const FStaticMeshBatchRelevance& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];

				if ((StaticMeshRelevance.CastShadow || (bSelfShadowOnly && StaticMeshRelevance.bUseForDepthPass)) && ShadowLODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
				{
					if (bCanCache && GetShadowDepthType() == CSMShadowDepthType)
					{
						AddCachedMeshDrawCommands_AnyThread(PrimitiveSceneInfo->Scene, StaticMeshRelevance, MeshIndex, NumAcceptedStaticMeshes, OutResult, OutStats, OverflowBuffer);
					}
					else
					{
						++OutStats.NumMDCBuildRequests;
						OutResult.AcceptMesh(NumAcceptedStaticMeshes++, MeshIndex, OverflowBuffer);
					}

					bDrawingStaticMeshes = true;
				}
			}
		}
		else
		{
			int32 NumAcceptedStaticMeshes = 0;

			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
			{
				const FStaticMeshBatchRelevance& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];

				if ((StaticMeshRelevance.CastShadow || (bSelfShadowOnly && StaticMeshRelevance.bUseForDepthPass)) && ShadowLODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
				{
					check(MeshIndex < MAX_uint16);
					++OutStats.NumMDCBuildRequests;
					OutResult.AcceptMesh(NumAcceptedStaticMeshes++, MeshIndex, OverflowBuffer);

					bDrawingStaticMeshes = true;
				}
			}
		}
	}

	return bDrawingStaticMeshes;
}

void FProjectedShadowInfo::AddSubjectPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, TArray<FViewInfo>* ViewArray, ERHIFeatureLevel::Type FeatureLevel, bool bRecordShadowSubjectsForMobileShading)
{
	// Ray traced shadows use the GPU managed distance field object buffers, no CPU culling should be used
	check(!bRayTracedDistanceField);

	if (!ReceiverPrimitives.Contains(PrimitiveSceneInfo)
		// Far cascade only casts from primitives marked for it
		&& (!CascadeSettings.bFarShadowCascade || PrimitiveSceneInfo->Proxy->CastsFarShadow()))
	{
		const FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;

		TArray<FViewInfo*, TInlineAllocator<1> > Views;
		const bool bWholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();

		if (bWholeSceneDirectionalShadow)
		{
			Views.Add(DependentView);
		}
		else
		{
			checkf(ViewArray,
				TEXT("bWholeSceneShadow=%d, CascadeSettings.ShadowSplitIndex=%d, bDirectionalLight=%s"),
				bWholeSceneShadow ? TEXT("true") : TEXT("false"),
				CascadeSettings.ShadowSplitIndex,
				bDirectionalLight ? TEXT("true") : TEXT("false"));

			for (int32 ViewIndex = 0; ViewIndex < ViewArray->Num(); ViewIndex++)
			{
				Views.Add(&(*ViewArray)[ViewIndex]);
			}
		}

		bool bOpaque = false;
		bool bTranslucentRelevance = false;
		bool bShadowRelevance = false;

		uint32 ViewMask = 0;
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();

		for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
		{
			FViewInfo& CurrentView = *Views[ViewIndex];
			FPrimitiveViewRelevance& ViewRelevance = CurrentView.PrimitiveViewRelevanceMap[PrimitiveId];

			if (!ViewRelevance.bInitializedThisFrame)
			{
				if( CurrentView.IsPerspectiveProjection() )
				{
					// Compute the distance between the view and the primitive.
					float DistanceSquared = (Proxy->GetBounds().Origin - CurrentView.ShadowViewMatrices.GetViewOrigin()).SizeSquared();

					bool bIsDistanceCulled = CurrentView.IsDistanceCulled(
						DistanceSquared,
						Proxy->GetMinDrawDistance(),
						Proxy->GetMaxDrawDistance(),
						PrimitiveSceneInfo
						);
					if( bIsDistanceCulled )
					{
						continue;
					}
				}

				// Respect HLOD visibility which can hide child LOD primitives
				if (CurrentView.ViewState &&
					CurrentView.ViewState->HLODVisibilityState.IsValidPrimitiveIndex(PrimitiveId) &&
					CurrentView.ViewState->HLODVisibilityState.IsNodeForcedHidden(PrimitiveId))
				{
					continue;
				}

				if ((CurrentView.ShowOnlyPrimitives.IsSet() &&
					!CurrentView.ShowOnlyPrimitives->Contains(PrimitiveSceneInfo->Proxy->GetPrimitiveComponentId())) ||
					CurrentView.HiddenPrimitives.Contains(PrimitiveSceneInfo->Proxy->GetPrimitiveComponentId()))
				{
					continue;
				}

				// Compute the subject primitive's view relevance since it wasn't cached
				// Update the main view's PrimitiveViewRelevanceMap
				ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(&CurrentView);

				ViewMask |= (1 << ViewIndex);
			}

			bOpaque |= ViewRelevance.bOpaque || ViewRelevance.bMasked;
			bTranslucentRelevance |= ViewRelevance.HasTranslucency() && !ViewRelevance.bMasked;
			bShadowRelevance |= ViewRelevance.bShadowRelevance;
		}

		if (bShadowRelevance)
		{
			// Update the primitive component's last render time. Allows the component to update when using bCastWhenHidden.
			const float CurrentWorldTime = Views[0]->Family->CurrentWorldTime;
			PrimitiveSceneInfo->UpdateComponentLastRenderTime(CurrentWorldTime, /*bUpdateLastRenderTimeOnScreen=*/false);

			if (PrimitiveSceneInfo->NeedsUniformBufferUpdate())
			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					// Main view visible primitives are processed on parallel tasks, updating uniform buffer them here will cause a race condition.
					check(!Views[ViewIndex]->PrimitiveVisibilityMap[PrimitiveSceneInfo->GetIndex()]);
				}

				PrimitiveSceneInfo->ConditionalUpdateUniformBuffer(FRHICommandListExecutor::GetImmediateCommandList());
			}

			if (PrimitiveSceneInfo->NeedsUpdateStaticMeshes())
			{
				// Need to defer to next InitViews, as main view visible primitives are processed on parallel tasks and calling 
				// CacheMeshDrawCommands may resize CachedDrawLists/CachedMeshDrawCommandStateBuckets causing a crash.
				PrimitiveSceneInfo->BeginDeferredUpdateStaticMeshesWithoutVisibilityCheck();
			}
		}

		if (bOpaque && bShadowRelevance)
		{
			const FBoxSphereBounds& Bounds = Proxy->GetBounds();
			bool bDrawingStaticMeshes = false;

			if (PrimitiveSceneInfo->StaticMeshes.Num() > 0)
			{
				for (int32 ViewIndex = 0, ViewCount = Views.Num(); ViewIndex < ViewCount; ViewIndex++)
				{
					FViewInfo& CurrentView = *Views[ViewIndex];

					const float DistanceSquared = ( Bounds.Origin - CurrentView.ShadowViewMatrices.GetViewOrigin() ).SizeSquared();

					if (bWholeSceneShadow)
					{
						const float LODScaleSquared = FMath::Square(CurrentView.LODDistanceFactor);
						const bool bDrawShadowDepth = FMath::Square(Bounds.SphereRadius) > FMath::Square(GMinScreenRadiusForShadowCaster) * DistanceSquared * LODScaleSquared;
						if( !bDrawShadowDepth )
						{
							// cull object if it's too small to be considered as shadow caster
							continue;
						}
					}

					// Update visibility for meshes which weren't visible in the main views or were visible with static relevance
					if (!CurrentView.PrimitiveVisibilityMap[PrimitiveId] || CurrentView.PrimitiveViewRelevanceMap[PrimitiveId].bStaticRelevance)
					{
						bDrawingStaticMeshes |= ShouldDrawStaticMeshes(CurrentView, PrimitiveSceneInfo);						
					}
				}
			}

			if (bDrawingStaticMeshes)
			{
				if (bRecordShadowSubjectsForMobileShading)
				{
					DependentView->VisibleLightInfos[GetLightSceneInfo().Id].MobileCSMSubjectPrimitives.AddSubjectPrimitive(PrimitiveSceneInfo, PrimitiveId);
				}
			}
			else
			{
				// Add the primitive to the subject primitive list.
				DynamicSubjectPrimitives.Add(PrimitiveSceneInfo);

				if (bRecordShadowSubjectsForMobileShading)
				{
					DependentView->VisibleLightInfos[GetLightSceneInfo().Id].MobileCSMSubjectPrimitives.AddSubjectPrimitive(PrimitiveSceneInfo, PrimitiveId);
				}
			}
		}

		// Add translucent shadow casting primitives to SubjectTranslucentPrimitives
		if (bTranslucentRelevance && bShadowRelevance)
		{
			SubjectTranslucentPrimitives.Add(PrimitiveSceneInfo);
		}
	}
}

uint64 FProjectedShadowInfo::AddSubjectPrimitive_AnyThread(
	const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact,
	TArray<FViewInfo>* ViewArray,
	ERHIFeatureLevel::Type FeatureLevel,
	FAddSubjectPrimitiveStats& OutStats,
	FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const
{
	// Ray traced shadows use the GPU managed distance field object buffers, no CPU culling should be used
	check(!bRayTracedDistanceField);

	FAddSubjectPrimitiveResult Result;
	Result.Qword = 0;

	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		// record shadow casters if CSM culling is enabled for the light's mobility type and the culling mode requires the list of casters.
		static auto* CVarMobileEnableStaticAndCSMShadowReceivers = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableStaticAndCSMShadowReceivers"));
		static auto* CVarMobileEnableMovableLightCSMShaderCulling = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableMovableLightCSMShaderCulling"));
		static auto* CVarMobileCSMShaderCullingMethod = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.Shadow.CSMShaderCullingMethod"));
		const uint32 MobileCSMCullingMode = CVarMobileCSMShaderCullingMethod->GetValueOnAnyThread() & 0xF;
		const bool bRecordShadowSubjectsForMobile =
			(MobileCSMCullingMode == 2 || MobileCSMCullingMode == 3)
			&& ((CVarMobileEnableMovableLightCSMShaderCulling->GetValueOnAnyThread() && GetLightSceneInfo().Proxy->IsMovable() && GetLightSceneInfo().ShouldRenderViewIndependentWholeSceneShadows())
				|| (CVarMobileEnableStaticAndCSMShadowReceivers->GetValueOnAnyThread() && GetLightSceneInfo().Proxy->UseCSMForDynamicObjects()));

		if (bRecordShadowSubjectsForMobile)
		{
			Result.bAddOnRenderThread = true;
			Result.bRecordShadowSubjectsForMobile = true;
			++OutStats.NumDeferredPrimitives;
			return Result.Qword;
		}
	}

	FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneInfoCompact.PrimitiveSceneInfo;

	if (!ReceiverPrimitives.Contains(PrimitiveSceneInfo)
		// Far cascade only casts from primitives marked for it
		&& (!CascadeSettings.bFarShadowCascade || PrimitiveSceneInfoCompact.Proxy->CastsFarShadow()))
	{
		FViewInfo* CurrentView;
		const bool bWholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();

		if (bWholeSceneDirectionalShadow)
		{
			CurrentView = DependentView;
		}
		else
		{
			checkf(ViewArray,
				TEXT("bWholeSceneShadow=%d, CascadeSettings.ShadowSplitIndex=%d, bDirectionalLight=%s"),
				bWholeSceneShadow ? TEXT("true") : TEXT("false"),
				CascadeSettings.ShadowSplitIndex,
				bDirectionalLight ? TEXT("true") : TEXT("false"));

			if (ViewArray->Num() > 1)
			{
				Result.bAddOnRenderThread = true;
				++OutStats.NumDeferredPrimitives;
				return Result.Qword;
			}

			CurrentView = &(*ViewArray)[0];
		}

		bool bOpaque = false;
		bool bTranslucentRelevance = false;
		bool bShadowRelevance = false;
		bool bStaticRelevance = false;
		bool bMayBeFading = false;
		bool bNeedUpdateStaticMeshes = false;

		const int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		FPrimitiveViewRelevance& ViewRelevance = CurrentView->PrimitiveViewRelevanceMap[PrimitiveId];

		if (!ViewRelevance.bInitializedThisFrame)
		{
			if (CurrentView->IsPerspectiveProjection())
			{
				bool bFadingIn;
				// Compute the distance between the view and the primitive.
				const float DistanceSquared = (PrimitiveSceneInfoCompact.Bounds.Origin - CurrentView->ShadowViewMatrices.GetViewOrigin()).SizeSquared();

				if (CurrentView->IsDistanceCulled_AnyThread(
					DistanceSquared,
					PrimitiveSceneInfoCompact.MinDrawDistance,
					PrimitiveSceneInfoCompact.MaxDrawDistance,
					PrimitiveSceneInfo,
					bMayBeFading,
					bFadingIn))
				{
					return 0;
				}

				if (bMayBeFading)
				{
					Result.bNeedPrimitiveFadingStateUpdate = true;
					Result.bFadingIn = bFadingIn;
				}
			}

			// Respect HLOD visibility which can hide child LOD primitives
			if (CurrentView->ViewState &&
				CurrentView->ViewState->HLODVisibilityState.IsValidPrimitiveIndex(PrimitiveId) &&
				CurrentView->ViewState->HLODVisibilityState.IsNodeForcedHidden(PrimitiveId))
			{
				return 0;
			}

			if ((CurrentView->ShowOnlyPrimitives.IsSet() &&
				!CurrentView->ShowOnlyPrimitives->Contains(PrimitiveSceneInfoCompact.Proxy->GetPrimitiveComponentId())) ||
				CurrentView->HiddenPrimitives.Contains(PrimitiveSceneInfoCompact.Proxy->GetPrimitiveComponentId()))
			{
				return 0;
			}

			// Compute the subject primitive's view relevance since it wasn't cached
			// Update the main view's PrimitiveViewRelevanceMap
			ViewRelevance = PrimitiveSceneInfoCompact.Proxy->GetViewRelevance(CurrentView);
		}

		bOpaque = ViewRelevance.bOpaque || ViewRelevance.bMasked;
		bTranslucentRelevance = ViewRelevance.HasTranslucency() && !ViewRelevance.bMasked;
		bShadowRelevance = ViewRelevance.bShadowRelevance;
		bStaticRelevance = ViewRelevance.bStaticRelevance;

		if (!bShadowRelevance)
		{
			return 0;
		}

		// Update the primitive component's last render time. Allows the component to update when using bCastWhenHidden.
		const float CurrentWorldTime = CurrentView->Family->CurrentWorldTime;
		PrimitiveSceneInfo->UpdateComponentLastRenderTime(CurrentWorldTime, /*bUpdateLastRenderTimeOnScreen=*/false);

		if (PrimitiveSceneInfo->NeedsUniformBufferUpdate())
		{
			// Main view visible primitives are processed on parallel tasks, updating uniform buffer them here will cause a race condition.
			check(!CurrentView->PrimitiveVisibilityMap[PrimitiveId]);
			Result.bNeedUniformBufferUpdate = true;
		}

		if (PrimitiveSceneInfo->NeedsUpdateStaticMeshes())
		{
			// Need to defer to next InitViews, as main view visible primitives are processed on parallel tasks and calling 
			// CacheMeshDrawCommands may resize CachedDrawLists/CachedMeshDrawCommandStateBuckets causing a crash.
			Result.bNeedUpdateStaticMeshes = true;
			bNeedUpdateStaticMeshes = true;
		}

		if (bOpaque)
		{
			bool bDrawingStaticMeshes = false;

			if (PrimitiveSceneInfo->StaticMeshes.Num() > 0)
			{
				if (bWholeSceneShadow)
				{
					const FBoxSphereBounds& Bounds = PrimitiveSceneInfoCompact.Bounds;
					const float DistanceSquared = (Bounds.Origin - CurrentView->ShadowViewMatrices.GetViewOrigin()).SizeSquared();
					const float LODScaleSquared = FMath::Square(CurrentView->LODDistanceFactor);
					const bool bDrawShadowDepth = FMath::Square(Bounds.SphereRadius) > FMath::Square(GMinScreenRadiusForShadowCaster) * DistanceSquared * LODScaleSquared;
					if (!bDrawShadowDepth)
					{
						// cull object if it's too small to be considered as shadow caster
						return 0;
					}
				}

				// Update visibility for meshes which weren't visible in the main views or were visible with static relevance
				if (bStaticRelevance || !CurrentView->PrimitiveVisibilityMap[PrimitiveId])
				{
					bDrawingStaticMeshes |= ShouldDrawStaticMeshes_AnyThread(
						*CurrentView,
						PrimitiveSceneInfoCompact,
						bMayBeFading,
						bNeedUpdateStaticMeshes,
						Result,
						OutStats,
						OverflowBuffer);
				}
			}

			if (!bDrawingStaticMeshes)
			{
				Result.bDynamicSubjectPrimitive = true;
				++OutStats.NumDynamicSubs;
			}
		}

		if (bTranslucentRelevance)
		{
			Result.bTranslucentSubjectPrimitive = true;
			++OutStats.NumTranslucentSubs;
		}
	}

	return Result.Qword;
}

void FProjectedShadowInfo::PresizeSubjectPrimitiveArrays(const FAddSubjectPrimitiveStats& Stats)
{
	ShadowDepthPassVisibleCommands.Reserve(ShadowDepthPassVisibleCommands.Num() + Stats.NumDeferredPrimitives * 2 + Stats.NumCachedMDCCopies);
	SubjectMeshCommandBuildRequests.Reserve(SubjectMeshCommandBuildRequests.Num() + Stats.NumMDCBuildRequests);
	DynamicSubjectPrimitives.Reserve(DynamicSubjectPrimitives.Num() + Stats.NumDeferredPrimitives + Stats.NumDynamicSubs);
	SubjectTranslucentPrimitives.Reserve(SubjectTranslucentPrimitives.Num() + Stats.NumTranslucentSubs);
}

void FProjectedShadowInfo::FinalizeAddSubjectPrimitive(
	const FAddSubjectPrimitiveOp& Op,
	TArray<FViewInfo>* ViewArray,
	ERHIFeatureLevel::Type FeatureLevel,
	FFinalizeAddSubjectPrimitiveContext& Context)
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = Op.PrimitiveSceneInfo;
	const FAddSubjectPrimitiveResult& Result = Op.Result;

	if (Result.bAddOnRenderThread)
	{
		AddSubjectPrimitive(PrimitiveSceneInfo, ViewArray, FeatureLevel, Result.bRecordShadowSubjectsForMobile);
		return;
	}

	if (Result.bNeedPrimitiveFadingStateUpdate)
	{
		FViewInfo& View = IsWholeSceneDirectionalShadow() ? *DependentView : (*ViewArray)[0];
		if (View.UpdatePrimitiveFadingState(PrimitiveSceneInfo, Result.bFadingIn))
		{
			if (Result.bOverflowed)
			{
				Context.OverflowedMDCIndices += Result.NumMDCIndices;
				Context.OverflowedMeshIndices += Result.NumMeshIndices;
			}
			return;
		}
	}

	if (Result.bCopyCachedMeshDrawCommand)
	{
		check(!Result.bDynamicSubjectPrimitive);
		const uint16* MDCIndices;
		int32 IdxBias;
		int32 NumMDCs = Result.GetMDCIndices(Context, MDCIndices, IdxBias);

		for (int32 Idx = 0; Idx < NumMDCs; ++Idx)
		{
			const int32 CmdIdx = (int32)MDCIndices[Idx] + IdxBias;
			const FCachedMeshDrawCommandInfo& CmdInfo = PrimitiveSceneInfo->StaticMeshCommandInfos[CmdIdx];
			const FScene* Scene = PrimitiveSceneInfo->Scene;
			const FMeshDrawCommand* CachedCmd = CmdInfo.StateBucketId >= 0 ?
				&Scene->CachedMeshDrawCommandStateBuckets[EMeshPass::CSMShadowDepth].GetByElementId(CmdInfo.StateBucketId).Key :
				&Scene->CachedDrawLists[EMeshPass::CSMShadowDepth].MeshDrawCommands[CmdInfo.CommandIndex];
			const int32 PrimIdx = PrimitiveSceneInfo->GetIndex();

			FVisibleMeshDrawCommand& VisibleCmd = ShadowDepthPassVisibleCommands[ShadowDepthPassVisibleCommands.AddUninitialized()];
			VisibleCmd.Setup(CachedCmd, PrimIdx, PrimIdx, CmdInfo.StateBucketId, CmdInfo.MeshFillMode, CmdInfo.MeshCullMode, CmdInfo.SortKey);
		}
	}

	if (Result.bRequestMeshCommandBuild)
	{
		check(!Result.bDynamicSubjectPrimitive);
		const uint16* MeshIndices;
		int32 IdxBias;
		int32 NumMeshes = Result.GetMeshIndices(Context, MeshIndices, IdxBias);

		for (int32 Idx = 0; Idx < NumMeshes; ++Idx)
		{
			const int32 MeshIdx = (int32)MeshIndices[Idx] + IdxBias;
			const FStaticMeshBatchRelevance& MeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIdx];
			const FStaticMeshBatch& MeshBatch = PrimitiveSceneInfo->StaticMeshes[MeshIdx];

			NumSubjectMeshCommandBuildRequestElements += MeshRelevance.NumElements;
			SubjectMeshCommandBuildRequests.Add(&MeshBatch);
		}
	}

	if (Result.bDynamicSubjectPrimitive)
	{
		DynamicSubjectPrimitives.Add(PrimitiveSceneInfo);
	}

	if (Result.bTranslucentSubjectPrimitive)
	{
		SubjectTranslucentPrimitives.Add(PrimitiveSceneInfo);
	}

	if (Result.bNeedUniformBufferUpdate)
	{
		PrimitiveSceneInfo->ConditionalUpdateUniformBuffer(FRHICommandListExecutor::GetImmediateCommandList());
	}

	if (Result.bNeedUpdateStaticMeshes)
	{
		PrimitiveSceneInfo->BeginDeferredUpdateStaticMeshesWithoutVisibilityCheck();
	}
}

bool FProjectedShadowInfo::HasSubjectPrims() const
{
	return DynamicSubjectPrimitives.Num() > 0
		|| ShadowDepthPass.HasAnyDraw()
		|| SubjectMeshCommandBuildRequests.Num() > 0;
}

void FProjectedShadowInfo::AddReceiverPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
	// Add the primitive to the receiver primitive list.
	ReceiverPrimitives.Add(PrimitiveSceneInfo);
}

void FProjectedShadowInfo::SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupMeshDrawCommandsForShadowDepth);

	FShadowDepthPassMeshProcessor* MeshPassProcessor = new(FMemStack::Get()) FShadowDepthPassMeshProcessor(
		Renderer.Scene,
		ShadowDepthView,
		ShadowDepthView->ViewUniformBuffer,
		PassUniformBuffer,
		GetShadowDepthType(),
		nullptr);

	if (Renderer.ShouldDumpMeshDrawCommandInstancingStats())
	{
		FString PassNameForStats;
		GetShadowTypeNameForDrawEvent(PassNameForStats);
		ShadowDepthPass.SetDumpInstancingStats(TEXT("ShadowDepth ") + PassNameForStats);
	}
	
	const uint32 InstanceFactor = !GetShadowDepthType().bOnePassPointLightShadow || RHISupportsGeometryShaders(Renderer.Scene->GetShaderPlatform()) ? 1 : 6;

	ShadowDepthPass.DispatchPassSetup(
		Renderer.Scene,
		*ShadowDepthView,
		EMeshPass::Num,
		FExclusiveDepthStencil::DepthNop_StencilNop,
		MeshPassProcessor,
		DynamicSubjectMeshElements,
		nullptr,
		NumDynamicSubjectMeshElements * InstanceFactor,
		SubjectMeshCommandBuildRequests,
		NumSubjectMeshCommandBuildRequestElements * InstanceFactor,
		ShadowDepthPassVisibleCommands);

	Renderer.DispatchedShadowDepthPasses.Add(&ShadowDepthPass);
}

void FProjectedShadowInfo::SetupMeshDrawCommandsForProjectionStenciling(FSceneRenderer& Renderer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupMeshDrawCommandsForShadowDepth);

	const EShadingPath ShadingPath = FSceneInterface::GetShadingPath(Renderer.FeatureLevel);
	static const auto EnableModulatedSelfShadowCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.EnableModulatedSelfShadow"));
	const bool bMobileModulatedShadowsAllowSelfShadow = !bSelfShadowOnly && (ShadingPath == EShadingPath::Mobile && !EnableModulatedSelfShadowCVar->GetValueOnRenderThread() && LightSceneInfo->Proxy && LightSceneInfo->Proxy->CastsModulatedShadows());
	if (bPreShadow || bSelfShadowOnly || bMobileModulatedShadowsAllowSelfShadow )
	{
		ProjectionStencilingPasses.Empty(Renderer.Views.Num());

		for (int32 ViewIndex = 0; ViewIndex < Renderer.Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Renderer.Views[ViewIndex];
			ProjectionStencilingPasses.Add(FShadowMeshDrawCommandPass());
			FShadowMeshDrawCommandPass& ProjectionStencilingPass = ProjectionStencilingPasses[ViewIndex];

			FDynamicPassMeshDrawListContext ProjectionStencilingContext(DynamicMeshDrawCommandStorage, ProjectionStencilingPass.VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, NeedsShaderInitialisation);

			FMeshPassProcessorRenderState DrawRenderState;
			DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());

			if (bMobileModulatedShadowsAllowSelfShadow)
			{
				checkf(bPreShadow == false, TEXT("The mobile renderer does not support preshadows."));

				DrawRenderState.SetDepthStencilState(
					TStaticDepthStencilState<
					false, CF_DepthNearOrEqual,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					0xff, 0xff
					>::GetRHI());
				DrawRenderState.SetStencilRef(0);
			}
			else
			{
				// Set stencil to one.
				DrawRenderState.SetDepthStencilState(
					TStaticDepthStencilState<
					false, CF_DepthNearOrEqual,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xff, 0xff
					>::GetRHI());

				DrawRenderState.SetStencilRef(1);
			}

			FDepthPassMeshProcessor DepthPassMeshProcessor(
				Renderer.Scene,
				&View,
				DrawRenderState,
				false,
				DDM_AllOccluders,
				false,
				false,
				&ProjectionStencilingContext);

			// Pre-shadows mask by receiver elements, self-shadow mask by subject elements.
			// Note that self-shadow pre-shadows still mask by receiver elements.
			const PrimitiveArrayType& MaskPrimitives = bPreShadow ? ReceiverPrimitives : DynamicSubjectPrimitives;

			for (int32 PrimitiveIndex = 0, PrimitiveCount = MaskPrimitives.Num(); PrimitiveIndex < PrimitiveCount; PrimitiveIndex++)
			{
				const FPrimitiveSceneInfo* ReceiverPrimitiveSceneInfo = MaskPrimitives[PrimitiveIndex];

				if (View.PrimitiveVisibilityMap[ReceiverPrimitiveSceneInfo->GetIndex()])
				{
					const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[ReceiverPrimitiveSceneInfo->GetIndex()];

					if (ViewRelevance.bRenderInMainPass && ViewRelevance.bStaticRelevance)
					{
						for (int32 StaticMeshIdx = 0; StaticMeshIdx < ReceiverPrimitiveSceneInfo->StaticMeshes.Num(); StaticMeshIdx++)
						{
							const FStaticMeshBatch& StaticMesh = ReceiverPrimitiveSceneInfo->StaticMeshes[StaticMeshIdx];

							if (View.StaticMeshVisibilityMap[StaticMesh.Id])
							{
								const uint64 DefaultBatchElementMask = ~0ul;
								DepthPassMeshProcessor.AddMeshBatch(StaticMesh, DefaultBatchElementMask, StaticMesh.PrimitiveSceneInfo->Proxy);
							}
						}
					}

					if (ViewRelevance.bRenderInMainPass && ViewRelevance.bDynamicRelevance)
					{
						const FInt32Range MeshBatchRange = View.GetDynamicMeshElementRange(ReceiverPrimitiveSceneInfo->GetIndex());

						for (int32 MeshBatchIndex = MeshBatchRange.GetLowerBoundValue(); MeshBatchIndex < MeshBatchRange.GetUpperBoundValue(); ++MeshBatchIndex)
						{
							const FMeshBatchAndRelevance& MeshAndRelevance = View.DynamicMeshElements[MeshBatchIndex];
							const uint64 BatchElementMask = ~0ull;

							DepthPassMeshProcessor.AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
						}
					}
				}
			}

			if (bSelfShadowOnly && !bPreShadow && !bMobileModulatedShadowsAllowSelfShadow)
			{
				for (int32 MeshBatchIndex = 0; MeshBatchIndex < SubjectMeshCommandBuildRequests.Num(); ++MeshBatchIndex)
				{
					const FStaticMeshBatch& StaticMesh = *SubjectMeshCommandBuildRequests[MeshBatchIndex];
					const uint64 DefaultBatchElementMask = ~0ul;
					DepthPassMeshProcessor.AddMeshBatch(StaticMesh, DefaultBatchElementMask, StaticMesh.PrimitiveSceneInfo->Proxy);
				}
			}

			ApplyViewOverridesToMeshDrawCommands(View, ProjectionStencilingPass.VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, GraphicsMinimalPipelineStateSet, NeedsShaderInitialisation);

			// If instanced stereo is enabled, we need to render each view of the stereo pair using the instanced stereo transform to avoid bias issues.
			// TODO: Support instanced stereo properly in the projection stenciling pass.
			const uint32 InstanceFactor = View.bIsInstancedStereoEnabled && !View.bIsMultiViewEnabled && IStereoRendering::IsStereoEyeView(View) ? 2 : 1;
			SortAndMergeDynamicPassMeshDrawCommands(Renderer.FeatureLevel, ProjectionStencilingPass.VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, ProjectionStencilingPass.PrimitiveIdVertexBuffer, InstanceFactor);
		}
	}
}

void FProjectedShadowInfo::GatherDynamicMeshElements(FSceneRenderer& Renderer, FVisibleLightInfo& VisibleLightInfo, TArray<const FSceneView*>& ReusedViewsArray,
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Shadow_GatherDynamicMeshElements);

	check(ShadowDepthView && IsInRenderingThread());

	if (DynamicSubjectPrimitives.Num() > 0 || ReceiverPrimitives.Num() > 0 || SubjectTranslucentPrimitives.Num() > 0)
	{
		// Backup properties of the view that we will override
		FMatrix OriginalViewMatrix = ShadowDepthView->ViewMatrices.GetViewMatrix();

		// Override the view matrix so that billboarding primitives will be aligned to the light
		ShadowDepthView->ViewMatrices.HackOverrideViewMatrixForShadows(ShadowViewMatrix);

		ReusedViewsArray[0] = ShadowDepthView;

		if (bPreShadow && GPreshadowsForceLowestLOD)
		{
			ShadowDepthView->DrawDynamicFlags = EDrawDynamicFlags::ForceLowestLOD;
		}

		if (CascadeSettings.bFarShadowCascade)
		{
			(int32&)ShadowDepthView->DrawDynamicFlags |= (int32)EDrawDynamicFlags::FarShadowCascade;
		}

		if (IsWholeSceneDirectionalShadow())
		{
			ShadowDepthView->SetPreShadowTranslation(FVector(0, 0, 0));
			ShadowDepthView->SetDynamicMeshElementsShadowCullFrustum(&CascadeSettings.ShadowBoundsAccurate);
			GatherDynamicMeshElementsArray(ShadowDepthView, Renderer, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer, 
				DynamicSubjectPrimitives, ReusedViewsArray, DynamicSubjectMeshElements, NumDynamicSubjectMeshElements);
			ShadowDepthView->SetPreShadowTranslation(PreShadowTranslation);
		}
		else
		{
			ShadowDepthView->SetPreShadowTranslation(PreShadowTranslation);
			ShadowDepthView->SetDynamicMeshElementsShadowCullFrustum(&CasterFrustum);
			GatherDynamicMeshElementsArray(ShadowDepthView, Renderer, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer, 
				DynamicSubjectPrimitives, ReusedViewsArray, DynamicSubjectMeshElements, NumDynamicSubjectMeshElements);
		}

		ShadowDepthView->DrawDynamicFlags = EDrawDynamicFlags::None;

		int32 NumDynamicSubjectTranslucentMeshElements = 0;
		ShadowDepthView->SetDynamicMeshElementsShadowCullFrustum(&CasterFrustum);
		GatherDynamicMeshElementsArray(ShadowDepthView, Renderer, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer, 
			SubjectTranslucentPrimitives, ReusedViewsArray, DynamicSubjectTranslucentMeshElements, NumDynamicSubjectTranslucentMeshElements);

		Renderer.MeshCollector.ProcessTasks();
    }

	
	// Create a pass uniform buffer so we can build mesh commands now in InitDynamicShadows.  This will be updated with the correct contents just before the actual pass.
	const EShadingPath ShadingPath = FSceneInterface::GetShadingPath(Renderer.FeatureLevel);
	FRHIUniformBuffer* PassUniformBuffer = nullptr;
	if (ShadingPath == EShadingPath::Deferred)
	{
		FShadowDepthPassUniformParameters ShadowDepthParameters;
		ShadowDepthPassUniformBuffer = TUniformBufferRef<FShadowDepthPassUniformParameters>::CreateUniformBufferImmediate(ShadowDepthParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
		PassUniformBuffer = ShadowDepthPassUniformBuffer;
	}
	else if (ShadingPath == EShadingPath::Mobile)
	{
		FMobileShadowDepthPassUniformParameters ShadowDepthParameters;
		MobileShadowDepthPassUniformBuffer = TUniformBufferRef<FMobileShadowDepthPassUniformParameters>::CreateUniformBufferImmediate(ShadowDepthParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);
		PassUniformBuffer = MobileShadowDepthPassUniformBuffer;
	}

	SetupMeshDrawCommandsForShadowDepth(Renderer, PassUniformBuffer);
	SetupMeshDrawCommandsForProjectionStenciling(Renderer);
}

void FProjectedShadowInfo::GatherDynamicMeshElementsArray(
	FViewInfo* FoundView,
	FSceneRenderer& Renderer, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
	FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
	FGlobalDynamicReadBuffer& DynamicReadBuffer,
	const PrimitiveArrayType& PrimitiveArray, 
	const TArray<const FSceneView*>& ReusedViewsArray,
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator>& OutDynamicMeshElements,
	int32& OutNumDynamicSubjectMeshElements)
{
	// Simple elements not supported in shadow passes
	FSimpleElementCollector DynamicSubjectSimpleElements;

	Renderer.MeshCollector.ClearViewMeshArrays();
	Renderer.MeshCollector.AddViewMeshArrays(
		FoundView, 
		&OutDynamicMeshElements, 
		&DynamicSubjectSimpleElements, 
		&FoundView->DynamicPrimitiveShaderData, 
		Renderer.ViewFamily.GetFeatureLevel(),
		&DynamicIndexBuffer,
		&DynamicVertexBuffer,
		&DynamicReadBuffer
		);

	const uint32 PrimitiveCount = PrimitiveArray.Num();

	for (uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
	{
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveArray[PrimitiveIndex];
		const FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy;

		// Lookup the primitive's cached view relevance
		FPrimitiveViewRelevance ViewRelevance = FoundView->PrimitiveViewRelevanceMap[PrimitiveSceneInfo->GetIndex()];

		if (!ViewRelevance.bInitializedThisFrame)
		{
			// Compute the subject primitive's view relevance since it wasn't cached
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(FoundView);
		}

		// Only draw if the subject primitive is shadow relevant.
		if (ViewRelevance.bShadowRelevance && ViewRelevance.bDynamicRelevance)
		{
			Renderer.MeshCollector.SetPrimitive(PrimitiveSceneInfo->Proxy, PrimitiveSceneInfo->DefaultDynamicHitProxyId);

			PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(ReusedViewsArray, Renderer.ViewFamily, 0x1, Renderer.MeshCollector);
		}
	}

	OutNumDynamicSubjectMeshElements = Renderer.MeshCollector.GetMeshElementCount(0);
}

/** 
 * @param View view to check visibility in
 * @return true if this shadow info has any subject prims visible in the view
 */
bool FProjectedShadowInfo::SubjectsVisible(const FViewInfo& View) const
{
	checkSlow(!IsWholeSceneDirectionalShadow());
	for(int32 PrimitiveIndex = 0;PrimitiveIndex < DynamicSubjectPrimitives.Num();PrimitiveIndex++)
	{
		const FPrimitiveSceneInfo* SubjectPrimitiveSceneInfo = DynamicSubjectPrimitives[PrimitiveIndex];
		if(View.PrimitiveVisibilityMap[SubjectPrimitiveSceneInfo->GetIndex()])
		{
			return true;
		}
	}
	return false;
}

/** 
 * Clears arrays allocated with the scene rendering allocator.
 * Cached preshadows are reused across frames so scene rendering allocations will be invalid.
 */
void FProjectedShadowInfo::ClearTransientArrays()
{
	NumDynamicSubjectMeshElements = 0;
	NumSubjectMeshCommandBuildRequestElements = 0;

	SubjectTranslucentPrimitives.Empty();
	DynamicSubjectPrimitives.Empty();
	ReceiverPrimitives.Empty();
	DynamicSubjectMeshElements.Empty();
	DynamicSubjectTranslucentMeshElements.Empty();

	ShadowDepthPassVisibleCommands.Empty();
	ShadowDepthPass.WaitForTasksAndEmpty();

	SubjectMeshCommandBuildRequests.Empty();

	ProjectionStencilingPasses.Reset();

	DynamicMeshDrawCommandStorage.MeshDrawCommands.Empty();
	GraphicsMinimalPipelineStateSet.Empty();
}

/** Returns a cached preshadow matching the input criteria if one exists. */
TRefCountPtr<FProjectedShadowInfo> FSceneRenderer::GetCachedPreshadow(
	const FLightPrimitiveInteraction* InParentInteraction, 
	const FProjectedShadowInitializer& Initializer,
	const FBoxSphereBounds& Bounds,
	uint32 InResolutionX)
{
	if (ShouldUseCachePreshadows() && !Views[0].bIsSceneCapture)
	{
		const FPrimitiveSceneInfo* PrimitiveInfo = InParentInteraction->GetPrimitiveSceneInfo();
		const FLightSceneInfo* LightInfo = InParentInteraction->GetLight();
		const FSphere QueryBounds(Bounds.Origin, Bounds.SphereRadius);

		for (int32 ShadowIndex = 0; ShadowIndex < Scene->CachedPreshadows.Num(); ShadowIndex++)
		{
			TRefCountPtr<FProjectedShadowInfo> CachedShadow = Scene->CachedPreshadows[ShadowIndex];
			// Only reuse a cached preshadow if it was created for the same primitive and light
			if (CachedShadow->GetParentSceneInfo() == PrimitiveInfo
				&& &CachedShadow->GetLightSceneInfo() == LightInfo
				// Only reuse if it contains the bounds being queried, with some tolerance
				&& QueryBounds.IsInside(CachedShadow->ShadowBounds, CachedShadow->ShadowBounds.W * .04f)
				// Only reuse if the resolution matches
				&& CachedShadow->ResolutionX == InResolutionX
				&& CachedShadow->bAllocated)
			{
				// Reset any allocations using the scene rendering allocator, 
				// Since those will point to freed memory now that we are using the shadow on a different frame than it was created on.
				CachedShadow->ClearTransientArrays();
				return CachedShadow;
			}
		}
	}
	// No matching cached preshadow was found
	return NULL;
}

struct FComparePreshadows
{
	FORCEINLINE bool operator()(const TRefCountPtr<FProjectedShadowInfo>& A, const TRefCountPtr<FProjectedShadowInfo>& B) const
	{
		if (B->ResolutionX * B->ResolutionY < A->ResolutionX * A->ResolutionY)
		{
			return true;
		}

		return false;
	}
};

/** Removes stale shadows and attempts to add new preshadows to the cache. */
void FSceneRenderer::UpdatePreshadowCache(FSceneRenderTargets& SceneContext)
{
	if (ShouldUseCachePreshadows() && !Views[0].bIsSceneCapture)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdatePreshadowCache);
		if (Scene->PreshadowCacheLayout.GetSizeX() == 0)
		{
			// Initialize the texture layout if necessary
			const FIntPoint PreshadowCacheBufferSize = SceneContext.GetPreShadowCacheTextureResolution();
			Scene->PreshadowCacheLayout = FTextureLayout(1, 1, PreshadowCacheBufferSize.X, PreshadowCacheBufferSize.Y, false, ETextureLayoutAspectRatio::None, false);
		}

		// Iterate through the cached preshadows, removing those that are not going to be rendered this frame
		for (int32 CachedShadowIndex = Scene->CachedPreshadows.Num() - 1; CachedShadowIndex >= 0; CachedShadowIndex--)
		{
			TRefCountPtr<FProjectedShadowInfo> CachedShadow = Scene->CachedPreshadows[CachedShadowIndex];
			bool bShadowBeingRenderedThisFrame = false;

			for (int32 LightIndex = 0; LightIndex < VisibleLightInfos.Num() && !bShadowBeingRenderedThisFrame; LightIndex++)
			{
				bShadowBeingRenderedThisFrame = VisibleLightInfos[LightIndex].ProjectedPreShadows.Find(CachedShadow) != INDEX_NONE;
			}

			if (!bShadowBeingRenderedThisFrame)
			{
				// Must succeed, since we added it to the layout earlier
				verify(Scene->PreshadowCacheLayout.RemoveElement(
					CachedShadow->X,
					CachedShadow->Y,
					CachedShadow->ResolutionX + CachedShadow->BorderSize * 2,
					CachedShadow->ResolutionY + CachedShadow->BorderSize * 2));
				Scene->CachedPreshadows.RemoveAt(CachedShadowIndex);
			}
		}

		TArray<TRefCountPtr<FProjectedShadowInfo>, SceneRenderingAllocator> UncachedPreShadows;

		// Gather a list of preshadows that can be cached
		for (int32 LightIndex = 0; LightIndex < VisibleLightInfos.Num(); LightIndex++)
		{
			for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfos[LightIndex].ProjectedPreShadows.Num(); ShadowIndex++)
			{
				TRefCountPtr<FProjectedShadowInfo> CurrentShadow = VisibleLightInfos[LightIndex].ProjectedPreShadows[ShadowIndex];
				checkSlow(CurrentShadow->bPreShadow);

				if (!CurrentShadow->bAllocatedInPreshadowCache)
				{
					UncachedPreShadows.Add(CurrentShadow);
				}
			}
		}

		// Sort them from largest to smallest, based on the assumption that larger preshadows will have more objects in their depth only pass
		UncachedPreShadows.Sort(FComparePreshadows());

		for (int32 ShadowIndex = 0; ShadowIndex < UncachedPreShadows.Num(); ShadowIndex++)
		{
			TRefCountPtr<FProjectedShadowInfo> CurrentShadow = UncachedPreShadows[ShadowIndex];

			// Try to find space for the preshadow in the texture layout
			if (Scene->PreshadowCacheLayout.AddElement(
				CurrentShadow->X,
				CurrentShadow->Y,
				CurrentShadow->ResolutionX + CurrentShadow->BorderSize * 2,
				CurrentShadow->ResolutionY + CurrentShadow->BorderSize * 2))
			{
				// Mark the preshadow as existing in the cache
				// It must now use the preshadow cache render target to render and read its depths instead of the usual shadow depth buffers
				CurrentShadow->bAllocatedInPreshadowCache = true;
				// Indicate that the shadow's X and Y have been initialized
				CurrentShadow->bAllocated = true;
				Scene->CachedPreshadows.Add(CurrentShadow);
			}
		}
	}
}

bool ShouldCreateObjectShadowForStationaryLight(const FLightSceneInfo* LightSceneInfo, const FPrimitiveSceneProxy* PrimitiveSceneProxy, bool bInteractionShadowMapped) 
{
	const bool bCreateObjectShadowForStationaryLight = 
		LightSceneInfo->bCreatePerObjectShadowsForDynamicObjects
		&& LightSceneInfo->IsPrecomputedLightingValid()
		&& LightSceneInfo->Proxy->GetShadowMapChannel() != INDEX_NONE
		// Create a per-object shadow if the object does not want static lighting and needs to integrate with the static shadowing of a stationary light
		// Or if the object wants static lighting but does not have a built shadowmap (Eg has been moved in the editor)
		&& (!PrimitiveSceneProxy->HasStaticLighting() || !bInteractionShadowMapped);

	return bCreateObjectShadowForStationaryLight;
}

void FSceneRenderer::SetupInteractionShadows(
	FRHICommandListImmediate& RHICmdList,
	FLightPrimitiveInteraction* Interaction, 
	FVisibleLightInfo& VisibleLightInfo, 
	bool bStaticSceneOnly,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& PreShadows)
{
	// too high on hit count to leave on
	// SCOPE_CYCLE_COUNTER(STAT_SetupInteractionShadows);

	FPrimitiveSceneInfo* PrimitiveSceneInfo = Interaction->GetPrimitiveSceneInfo();
	FLightSceneProxy* LightProxy = Interaction->GetLight()->Proxy;
	extern bool GUseTranslucencyShadowDepths;

	bool bShadowHandledByParent = false;

	if (PrimitiveSceneInfo->LightingAttachmentRoot.IsValid())
	{
		FAttachmentGroupSceneInfo& AttachmentGroup = Scene->AttachmentGroups.FindChecked(PrimitiveSceneInfo->LightingAttachmentRoot);
		bShadowHandledByParent = AttachmentGroup.ParentSceneInfo && AttachmentGroup.ParentSceneInfo->Proxy->LightAttachmentsAsGroup();
	}

	// Shadowing for primitives with a shadow parent will be handled by that shadow parent
	if (!bShadowHandledByParent)
	{
		const bool bCreateTranslucentObjectShadow = GUseTranslucencyShadowDepths && Interaction->HasTranslucentObjectShadow();
		const bool bCreateInsetObjectShadow = Interaction->HasInsetObjectShadow();
		const bool bCreateObjectShadowForStationaryLight = ShouldCreateObjectShadowForStationaryLight(Interaction->GetLight(), PrimitiveSceneInfo->Proxy, Interaction->IsShadowMapped());

		if (Interaction->HasShadow() 
			// TODO: Handle inset shadows, especially when an object is only casting a self-shadow.
			// Only render shadows from objects that use static lighting during a reflection capture, since the reflection capture doesn't update at runtime
			&& (!bStaticSceneOnly || PrimitiveSceneInfo->Proxy->HasStaticLighting())
			&& (bCreateTranslucentObjectShadow || bCreateInsetObjectShadow || bCreateObjectShadowForStationaryLight))
		{
			// Create projected shadow infos
			CreatePerObjectProjectedShadow(RHICmdList, Interaction, bCreateTranslucentObjectShadow, bCreateInsetObjectShadow || bCreateObjectShadowForStationaryLight, ViewDependentWholeSceneShadows, PreShadows);
		}
	}
}

void FSceneRenderer::CreatePerObjectProjectedShadow(
	FRHICommandListImmediate& RHICmdList,
	FLightPrimitiveInteraction* Interaction, 
	bool bCreateTranslucentObjectShadow, 
	bool bCreateOpaqueObjectShadow,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows)
{
	check(bCreateOpaqueObjectShadow || bCreateTranslucentObjectShadow);
	FPrimitiveSceneInfo* PrimitiveSceneInfo = Interaction->GetPrimitiveSceneInfo();
	const int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();

	FLightSceneInfo* LightSceneInfo = Interaction->GetLight();
	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

	// Check if the shadow is visible in any of the views.
	bool bShadowIsPotentiallyVisibleNextFrame = false;
	bool bOpaqueShadowIsVisibleThisFrame = false;
	bool bSubjectIsVisible = false;
	bool bOpaque = false;
	bool bTranslucentRelevance = false;
	bool bTranslucentShadowIsVisibleThisFrame = false;
	int32 NumBufferedFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(FeatureLevel);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		// Lookup the primitive's cached view relevance
		FPrimitiveViewRelevance ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

		if (!ViewRelevance.bInitializedThisFrame)
		{
			// Compute the subject primitive's view relevance since it wasn't cached
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(&View);
		}

		// Check if the subject primitive is shadow relevant.
		const bool bPrimitiveIsShadowRelevant = ViewRelevance.bShadowRelevance;

		const FSceneViewState::FProjectedShadowKey OpaqueKey(PrimitiveSceneInfo->PrimitiveComponentId, LightSceneInfo->Proxy->GetLightComponent(), INDEX_NONE, false);

		// Check if the shadow and preshadow are occluded.
		const bool bOpaqueShadowIsOccluded = 
			!bCreateOpaqueObjectShadow ||
			(
				!View.bIgnoreExistingQueries &&	View.State &&
				((FSceneViewState*)View.State)->IsShadowOccluded(RHICmdList, OpaqueKey, NumBufferedFrames)
			);

		const FSceneViewState::FProjectedShadowKey TranslucentKey(PrimitiveSceneInfo->PrimitiveComponentId, LightSceneInfo->Proxy->GetLightComponent(), INDEX_NONE, true);

		const bool bTranslucentShadowIsOccluded = 
			!bCreateTranslucentObjectShadow ||
			(
				!View.bIgnoreExistingQueries && View.State &&
				((FSceneViewState*)View.State)->IsShadowOccluded(RHICmdList, TranslucentKey, NumBufferedFrames)
			);

		// if subject doesn't render in the main pass, it's never considered visible
		// (in this case, there will be no need to generate any preshadows for the subject)
		if (PrimitiveSceneInfo->Proxy->ShouldRenderInMainPass())
		{
			const bool bSubjectIsVisibleInThisView = View.PrimitiveVisibilityMap[PrimitiveSceneInfo->GetIndex()];
			bSubjectIsVisible |= bSubjectIsVisibleInThisView;
		}

		// The shadow is visible if it is view relevant and unoccluded.
		bOpaqueShadowIsVisibleThisFrame |= (bPrimitiveIsShadowRelevant && !bOpaqueShadowIsOccluded);
		bTranslucentShadowIsVisibleThisFrame |= (bPrimitiveIsShadowRelevant && !bTranslucentShadowIsOccluded);
		bShadowIsPotentiallyVisibleNextFrame |= bPrimitiveIsShadowRelevant;
		bOpaque |= ViewRelevance.bOpaque;
		bTranslucentRelevance |= ViewRelevance.HasTranslucency();
	}

	if (!bOpaqueShadowIsVisibleThisFrame && !bTranslucentShadowIsVisibleThisFrame && !bShadowIsPotentiallyVisibleNextFrame)
	{
		// Don't setup the shadow info for shadows which don't need to be rendered or occlusion tested.
		return;
	}

	TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowGroupPrimitives;
	PrimitiveSceneInfo->GatherLightingAttachmentGroupPrimitives(ShadowGroupPrimitives);

#if ENABLE_NAN_DIAGNOSTIC
	// allow for silent failure: only possible if NaN checking is enabled.  
	if (ShadowGroupPrimitives.Num() == 0)
	{
		return;
	}
#endif

	// Compute the composite bounds of this group of shadow primitives.
	FBoxSphereBounds OriginalBounds = ShadowGroupPrimitives[0]->Proxy->GetBounds();

	if (!ensureMsgf(OriginalBounds.ContainsNaN() == false, TEXT("OriginalBound contains NaN : %s"), *OriginalBounds.ToString()))
	{
		// fix up OriginalBounds. This is going to cause flickers
		OriginalBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(1.f), 1.f);
	}

	for (int32 ChildIndex = 1; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
	{
		const FPrimitiveSceneInfo* ShadowChild = ShadowGroupPrimitives[ChildIndex];
		if (ShadowChild->Proxy->CastsDynamicShadow())
		{
			FBoxSphereBounds ChildBound = ShadowChild->Proxy->GetBounds();
			OriginalBounds = OriginalBounds + ChildBound;

			if (!ensureMsgf(OriginalBounds.ContainsNaN() == false, TEXT("Child %s contains NaN : %s"), *ShadowChild->Proxy->GetOwnerName().ToString(), *ChildBound.ToString()))
			{
				// fix up OriginalBounds. This is going to cause flickers
				OriginalBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(1.f), 1.f);
			}
		}
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	
	// Shadowing constants.
	
	const uint32 MaxShadowResolutionSetting = GetCachedScalabilityCVars().MaxShadowResolution;
	const FIntPoint ShadowBufferResolution = SceneContext.GetShadowDepthTextureResolution();
	const uint32 MaxShadowResolution = FMath::Min<int32>(MaxShadowResolutionSetting, ShadowBufferResolution.X) - SHADOW_BORDER * 2;
	const uint32 MaxShadowResolutionY = FMath::Min<int32>(MaxShadowResolutionSetting, ShadowBufferResolution.Y) - SHADOW_BORDER * 2;
	const uint32 MinShadowResolution     = FMath::Max<int32>(0, CVarMinShadowResolution.GetValueOnRenderThread());
	const uint32 ShadowFadeResolution    = FMath::Max<int32>(0, CVarShadowFadeResolution.GetValueOnRenderThread());
	const uint32 MinPreShadowResolution  = FMath::Max<int32>(0, CVarMinPreShadowResolution.GetValueOnRenderThread());
	const uint32 PreShadowFadeResolution = FMath::Max<int32>(0, CVarPreShadowFadeResolution.GetValueOnRenderThread());
	
	// Compute the maximum resolution required for the shadow by any view. Also keep track of the unclamped resolution for fading.
	uint32 MaxDesiredResolution = 0;
	float MaxScreenPercent = 0;
	TArray<float, TInlineAllocator<2> > ResolutionFadeAlphas;
	TArray<float, TInlineAllocator<2> > ResolutionPreShadowFadeAlphas;
	float MaxResolutionFadeAlpha = 0;
	float MaxResolutionPreShadowFadeAlpha = 0;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		// Determine the size of the subject's bounding sphere in this view.
		const FVector ShadowViewOrigin = View.ViewMatrices.GetViewOrigin();
		float ShadowViewDistFromBounds = (OriginalBounds.Origin - ShadowViewOrigin).Size();
		const float ScreenRadius = View.ShadowViewMatrices.GetScreenScale() *
			OriginalBounds.SphereRadius /
			FMath::Max(ShadowViewDistFromBounds, 1.0f);
		// Early catch for invalid CalculateShadowFadeAlpha()
		ensureMsgf(ScreenRadius >= 0.0f, TEXT("View.ShadowViewMatrices.ScreenScale %f, OriginalBounds.SphereRadius %f, ShadowViewDistFromBounds %f"), View.ShadowViewMatrices.GetScreenScale(), OriginalBounds.SphereRadius, ShadowViewDistFromBounds);

		const float ScreenPercent = FMath::Max(
			1.0f / 2.0f * View.ShadowViewMatrices.GetProjectionScale().X,
			1.0f / 2.0f * View.ShadowViewMatrices.GetProjectionScale().Y
			) *
			OriginalBounds.SphereRadius /
			FMath::Max(ShadowViewDistFromBounds, 1.0f);

		MaxScreenPercent = FMath::Max(MaxScreenPercent, ScreenPercent);

		// Determine the amount of shadow buffer resolution needed for this view.
		const float UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixel.GetValueOnRenderThread();

		// Calculate fading based on resolution
		// Compute FadeAlpha before ShadowResolutionScale contribution (artists want to modify the softness of the shadow, not change the fade ranges)
		const float ViewSpecificAlpha = CalculateShadowFadeAlpha(UnclampedResolution, ShadowFadeResolution, MinShadowResolution) * LightSceneInfo->Proxy->GetShadowAmount();
		MaxResolutionFadeAlpha = FMath::Max(MaxResolutionFadeAlpha, ViewSpecificAlpha);
		ResolutionFadeAlphas.Add(ViewSpecificAlpha);

		const float ViewSpecificPreShadowAlpha = CalculateShadowFadeAlpha(UnclampedResolution * CVarPreShadowResolutionFactor.GetValueOnRenderThread(), PreShadowFadeResolution, MinPreShadowResolution) * LightSceneInfo->Proxy->GetShadowAmount();
		MaxResolutionPreShadowFadeAlpha = FMath::Max(MaxResolutionPreShadowFadeAlpha, ViewSpecificPreShadowAlpha);
		ResolutionPreShadowFadeAlphas.Add(ViewSpecificPreShadowAlpha);

		const float ShadowResolutionScale = LightSceneInfo->Proxy->GetShadowResolutionScale();

		float ClampedResolution = UnclampedResolution;

		if (ShadowResolutionScale > 1.0f)
		{
			// Apply ShadowResolutionScale before the MaxShadowResolution clamp if raising the resolution
			ClampedResolution *= ShadowResolutionScale;
		}

		ClampedResolution = FMath::Min<float>(ClampedResolution, MaxShadowResolution);

		if (ShadowResolutionScale <= 1.0f)
		{
			// Apply ShadowResolutionScale after the MaxShadowResolution clamp if lowering the resolution
			// Artists want to modify the softness of the shadow with ShadowResolutionScale
			ClampedResolution *= ShadowResolutionScale;
		}

		MaxDesiredResolution = FMath::Max(
			MaxDesiredResolution,
			FMath::Max<uint32>(
				ClampedResolution,
				FMath::Min<int32>(MinShadowResolution, ShadowBufferResolution.X - SHADOW_BORDER * 2)
				)
			);
	}

	FBoxSphereBounds Bounds = OriginalBounds;

	const bool bRenderPreShadow = 
		CVarAllowPreshadows.GetValueOnRenderThread() 
		&& LightSceneInfo->Proxy->HasStaticShadowing()
		// Preshadow only affects the subject's pixels
		&& bSubjectIsVisible 
		// Only objects with dynamic lighting should create a preshadow
		// Unless we're in the editor and need to preview an object without built lighting
		&& (!PrimitiveSceneInfo->Proxy->HasStaticLighting() || !Interaction->IsShadowMapped())
		// Disable preshadows from directional lights for primitives that use single sample shadowing, the shadow factor will be written into the precomputed shadow mask in the GBuffer instead
		&& !(PrimitiveSceneInfo->Proxy->UseSingleSampleShadowFromStationaryLights() && LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
		&& Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5;

	if (bRenderPreShadow && ShouldUseCachePreshadows())
	{
		float PreshadowExpandFraction = FMath::Max(CVarPreshadowExpandFraction.GetValueOnRenderThread(), 0.0f);

		// If we're creating a preshadow, expand the bounds somewhat so that the preshadow will be cached more often as the shadow caster moves around.
		//@todo - only expand the preshadow bounds for this, not the per object shadow.
		Bounds.SphereRadius += (Bounds.BoxExtent * PreshadowExpandFraction).Size();
		Bounds.BoxExtent *= PreshadowExpandFraction + 1.0f;
	}

	// Compute the projected shadow initializer for this primitive-light pair.
	FPerObjectProjectedShadowInitializer ShadowInitializer;

	if ((MaxResolutionFadeAlpha > 1.0f / 256.0f || (bRenderPreShadow && MaxResolutionPreShadowFadeAlpha > 1.0f / 256.0f))
		&& LightSceneInfo->Proxy->GetPerObjectProjectedShadowInitializer(Bounds, ShadowInitializer))
	{
		const float MaxFadeAlpha = MaxResolutionFadeAlpha;

		// Only create a shadow from this object if it hasn't completely faded away
		if (CVarAllowPerObjectShadows.GetValueOnRenderThread() && MaxFadeAlpha > 1.0f / 256.0f)
		{
			// Round down to the nearest power of two so that resolution changes are always doubling or halving the resolution, which increases filtering stability
			// Use the max resolution if the desired resolution is larger than that
			const int32 SizeX = MaxDesiredResolution >= MaxShadowResolution ? MaxShadowResolution : (1 << (FMath::CeilLogTwo(MaxDesiredResolution) - 1));

			if (bOpaque && bCreateOpaqueObjectShadow && (bOpaqueShadowIsVisibleThisFrame || bShadowIsPotentiallyVisibleNextFrame))
			{
				// Create a projected shadow for this interaction's shadow.
				FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(),1,16) FProjectedShadowInfo;

				if(ProjectedShadowInfo->SetupPerObjectProjection(
					LightSceneInfo,
					PrimitiveSceneInfo,
					ShadowInitializer,
					false,					// no preshadow
					SizeX,
					MaxShadowResolutionY,
					SHADOW_BORDER,
					MaxScreenPercent,
					false))					// no translucent shadow
				{
					ProjectedShadowInfo->bPerObjectOpaqueShadow = true;
					ProjectedShadowInfo->FadeAlphas = ResolutionFadeAlphas;
					VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);

					if (bOpaqueShadowIsVisibleThisFrame)
					{
						VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);

						for (int32 ChildIndex = 0, ChildCount = ShadowGroupPrimitives.Num(); ChildIndex < ChildCount; ChildIndex++)
						{
							FPrimitiveSceneInfo* ShadowChild = ShadowGroupPrimitives[ChildIndex];
							ProjectedShadowInfo->AddSubjectPrimitive(ShadowChild, &Views, FeatureLevel, false);
						}
					}
					else if (bShadowIsPotentiallyVisibleNextFrame)
					{
						VisibleLightInfo.OccludedPerObjectShadows.Add(ProjectedShadowInfo);
					}
				}
			}

			if (bTranslucentRelevance
				&& Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5
				&& bCreateTranslucentObjectShadow 
				&& (bTranslucentShadowIsVisibleThisFrame || bShadowIsPotentiallyVisibleNextFrame))
			{
				// Create a projected shadow for this interaction's shadow.
				FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(),1,16) FProjectedShadowInfo;

				if(ProjectedShadowInfo->SetupPerObjectProjection(
					LightSceneInfo,
					PrimitiveSceneInfo,
					ShadowInitializer,
					false,					// no preshadow
					// Size was computed for the full res opaque shadow, convert to downsampled translucent shadow size with proper clamping
					FMath::Clamp<int32>(SizeX / SceneContext.GetTranslucentShadowDownsampleFactor(), 1, SceneContext.GetTranslucentShadowDepthTextureResolution().X - SHADOW_BORDER * 2),
					FMath::Clamp<int32>(MaxShadowResolutionY / SceneContext.GetTranslucentShadowDownsampleFactor(), 1, SceneContext.GetTranslucentShadowDepthTextureResolution().Y - SHADOW_BORDER * 2),
					SHADOW_BORDER,
					MaxScreenPercent,
					true))					// translucent shadow
				{
					ProjectedShadowInfo->FadeAlphas = ResolutionFadeAlphas,
					VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);

					if (bTranslucentShadowIsVisibleThisFrame)
					{
						VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);

						for (int32 ChildIndex = 0, ChildCount = ShadowGroupPrimitives.Num(); ChildIndex < ChildCount; ChildIndex++)
						{
							FPrimitiveSceneInfo* ShadowChild = ShadowGroupPrimitives[ChildIndex];
							ProjectedShadowInfo->AddSubjectPrimitive(ShadowChild, &Views, FeatureLevel, false);
						}
					}
					else if (bShadowIsPotentiallyVisibleNextFrame)
					{
						VisibleLightInfo.OccludedPerObjectShadows.Add(ProjectedShadowInfo);
					}
				}
			}
		}

		const float MaxPreFadeAlpha = MaxResolutionPreShadowFadeAlpha;

		// If the subject is visible in at least one view, create a preshadow for static primitives shadowing the subject.
		if (MaxPreFadeAlpha > 1.0f / 256.0f 
			&& bRenderPreShadow
			&& bOpaque)
		{
			// Round down to the nearest power of two so that resolution changes are always doubling or halving the resolution, which increases filtering stability.
			int32 PreshadowSizeX = 1 << (FMath::CeilLogTwo(FMath::TruncToInt(MaxDesiredResolution * CVarPreShadowResolutionFactor.GetValueOnRenderThread())) - 1);

			const FIntPoint PreshadowCacheResolution = SceneContext.GetPreShadowCacheTextureResolution();
			checkSlow(PreshadowSizeX <= PreshadowCacheResolution.X);
			bool bIsOutsideWholeSceneShadow = true;

			for (int32 i = 0; i < ViewDependentWholeSceneShadows.Num(); i++)
			{
				const FProjectedShadowInfo* WholeSceneShadow = ViewDependentWholeSceneShadows[i];
				const FVector2D DistanceFadeValues = WholeSceneShadow->GetLightSceneInfo().Proxy->GetDirectionalLightDistanceFadeParameters(Scene->GetFeatureLevel(), WholeSceneShadow->GetLightSceneInfo().IsPrecomputedLightingValid(), WholeSceneShadow->DependentView->MaxShadowCascades);
				const float DistanceFromShadowCenterSquared = (WholeSceneShadow->ShadowBounds.Center - Bounds.Origin).SizeSquared();
				//@todo - if view dependent whole scene shadows are ever supported in splitscreen, 
				// We can only disable the preshadow at this point if it is inside a whole scene shadow for all views
				const float DistanceFromViewSquared = ((FVector)WholeSceneShadow->DependentView->ShadowViewMatrices.GetViewOrigin() - Bounds.Origin).SizeSquared();
				// Mark the preshadow as inside the whole scene shadow if its bounding sphere is inside the near fade distance
				if (DistanceFromShadowCenterSquared < FMath::Square(FMath::Max(WholeSceneShadow->ShadowBounds.W - Bounds.SphereRadius, 0.0f))
					//@todo - why is this extra threshold required?
					&& DistanceFromViewSquared < FMath::Square(FMath::Max(DistanceFadeValues.X - 200.0f - Bounds.SphereRadius, 0.0f)))
				{
					bIsOutsideWholeSceneShadow = false;
					break;
				}
			}

			// Only create opaque preshadows when part of the caster is outside the whole scene shadow.
			if (bIsOutsideWholeSceneShadow)
			{
				// Try to reuse a preshadow from the cache
				TRefCountPtr<FProjectedShadowInfo> ProjectedPreShadowInfo = GetCachedPreshadow(Interaction, ShadowInitializer, OriginalBounds, PreshadowSizeX);

				bool bOk = true;

				if(!ProjectedPreShadowInfo)
				{
					// Create a new projected shadow for this interaction's preshadow
					// Not using the scene rendering mem stack because this shadow info may need to persist for multiple frames if it gets cached
					ProjectedPreShadowInfo = new FProjectedShadowInfo;

					bOk = ProjectedPreShadowInfo->SetupPerObjectProjection(
						LightSceneInfo,
						PrimitiveSceneInfo,
						ShadowInitializer,
						true,				// preshadow
						PreshadowSizeX,
						FMath::TruncToInt(MaxShadowResolutionY * CVarPreShadowResolutionFactor.GetValueOnRenderThread()),
						SHADOW_BORDER,
						MaxScreenPercent,
						false				// not translucent shadow
						);
				}

				if (bOk)
				{

					// Update fade alpha on the cached preshadow
					ProjectedPreShadowInfo->FadeAlphas = ResolutionPreShadowFadeAlphas;

					VisibleLightInfo.AllProjectedShadows.Add(ProjectedPreShadowInfo);
					VisibleLightInfo.ProjectedPreShadows.Add(ProjectedPreShadowInfo);

					// Only add to OutPreShadows if the preshadow doesn't already have depths cached, 
					// Since OutPreShadows is used to generate information only used when rendering the shadow depths.
					if (!ProjectedPreShadowInfo->bDepthsCached && ProjectedPreShadowInfo->CasterFrustum.PermutedPlanes.Num())
					{
						OutPreShadows.Add(ProjectedPreShadowInfo);
					}

					for (int32 ChildIndex = 0; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
					{
						FPrimitiveSceneInfo* ShadowChild = ShadowGroupPrimitives[ChildIndex];
						bool bChildIsVisibleInAnyView = false;
						for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							const FViewInfo& View = Views[ViewIndex];
							if (View.PrimitiveVisibilityMap[ShadowChild->GetIndex()])
							{
								bChildIsVisibleInAnyView = true;
								break;
							}
						}
						if (bChildIsVisibleInAnyView)
						{
							ProjectedPreShadowInfo->AddReceiverPrimitive(ShadowChild);
						}
					}
				}
			}
		}
	}
}

static bool CanFallbackToOldShadowMapCache(const FShadowMapRenderTargetsRefCounted& CachedShadowMap, const FIntPoint& MaxShadowResolution)
{
	return CachedShadowMap.IsValid()
		&& CachedShadowMap.GetSize().X <= MaxShadowResolution.X
		&& CachedShadowMap.GetSize().Y <= MaxShadowResolution.Y;
}

void ComputeWholeSceneShadowCacheModes(
	const FLightSceneInfo* LightSceneInfo,
	bool bCubeShadowMap,
	float RealTime,
	float ActualDesiredResolution,
	const FIntPoint& MaxShadowResolution,
	FScene* Scene,
	FWholeSceneProjectedShadowInitializer& InOutProjectedShadowInitializer,
	FIntPoint& InOutShadowMapSize,
	uint32& InOutNumPointShadowCachesUpdatedThisFrame,
	uint32& InOutNumSpotShadowCachesUpdatedThisFrame,
	int32& OutNumShadowMaps, 
	EShadowDepthCacheMode* OutCacheModes)
{
	// Strategy:
	// - Try to fallback if over budget. Budget is defined as number of updates currently
	// - Only allow fallback for updates caused by resolution changes
	// - Always render if cache doesn't exist or has been released
	uint32* NumCachesUpdatedThisFrame = nullptr;
	uint32 MaxCacheUpdatesAllowed = 0;

	switch (LightSceneInfo->Proxy->GetLightType())
	{
	case LightType_Point:
	case LightType_Rect:
		NumCachesUpdatedThisFrame = &InOutNumPointShadowCachesUpdatedThisFrame;
		MaxCacheUpdatesAllowed = static_cast<uint32>(GMaxNumPointShadowCacheUpdatesPerFrame);
		break;
	case LightType_Spot:
		NumCachesUpdatedThisFrame = &InOutNumSpotShadowCachesUpdatedThisFrame;
		MaxCacheUpdatesAllowed = static_cast<uint32>(GMaxNumSpotShadowCacheUpdatesPerFrame);
		break;
	default:
		checkf(false, TEXT("Directional light isn't handled here"));
		break;
	}

	if (GCacheWholeSceneShadows 
		&& (!bCubeShadowMap || RHISupportsGeometryShaders(GShaderPlatformForFeatureLevel[Scene->GetFeatureLevel()]) || RHISupportsVertexShaderLayer(GShaderPlatformForFeatureLevel[Scene->GetFeatureLevel()])))
	{
		FCachedShadowMapData* CachedShadowMapData = Scene->CachedShadowMaps.Find(LightSceneInfo->Id);

		if (CachedShadowMapData)
		{
			if (InOutProjectedShadowInitializer.IsCachedShadowValid(CachedShadowMapData->Initializer))
			{
				if (CachedShadowMapData->ShadowMap.IsValid() && CachedShadowMapData->ShadowMap.GetSize() == InOutShadowMapSize)
				{
					OutNumShadowMaps = 1;
					OutCacheModes[0] = SDCM_MovablePrimitivesOnly;
				}
				else
				{
					int64 CachedShadowMapsSize = Scene->GetCachedWholeSceneShadowMapsSize();

					if (CachedShadowMapsSize < static_cast<int64>(GWholeSceneShadowCacheMb) * 1024 * 1024)
					{
						OutNumShadowMaps = 2;
						// Note: ShadowMap with static primitives rendered first so movable shadowmap can composite
						OutCacheModes[0] = SDCM_StaticPrimitivesOnly;
						OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
						++*NumCachesUpdatedThisFrame;
						
						// Check if update is caused by resolution change
						if (CanFallbackToOldShadowMapCache(CachedShadowMapData->ShadowMap, MaxShadowResolution))
						{
							FIntPoint ExistingShadowMapSize = CachedShadowMapData->ShadowMap.GetSize();
							bool bOverBudget = *NumCachesUpdatedThisFrame > MaxCacheUpdatesAllowed;
							bool bRejectedByGuardBand = false;

							// Only allow shrinking if actual desired resolution has dropped enough.
							// This creates a guard band and hence avoid thrashing
							if (!bOverBudget
								&& (InOutShadowMapSize.X < ExistingShadowMapSize.X
								|| InOutShadowMapSize.Y < ExistingShadowMapSize.Y))
							{
								FVector2D VecNewSize = static_cast<FVector2D>(InOutShadowMapSize);
								FVector2D VecExistingSize = static_cast<FVector2D>(ExistingShadowMapSize);
								FVector2D VecDesiredSize(ActualDesiredResolution, ActualDesiredResolution);
#if DO_CHECK
								checkf(ExistingShadowMapSize.X > 0 && ExistingShadowMapSize.Y > 0,
									TEXT("%d, %d"), ExistingShadowMapSize.X, ExistingShadowMapSize.Y);
#endif
								FVector2D DropRatio = (VecExistingSize - VecDesiredSize) / (VecExistingSize - VecNewSize);
								float MaxDropRatio = FMath::Max(
									InOutShadowMapSize.X < ExistingShadowMapSize.X ? DropRatio.X : 0.f,
									InOutShadowMapSize.Y < ExistingShadowMapSize.Y ? DropRatio.Y : 0.f);

								// MaxDropRatio <= 0 can happen when max shadow map resolution is lowered (for example,
								// by changing quality settings). In that case, just let it happen.
								bRejectedByGuardBand = MaxDropRatio > 0.f && MaxDropRatio < 0.5f;
							}

							if (bOverBudget || bRejectedByGuardBand)
							{
								// Fallback to existing shadow cache
								InOutShadowMapSize = CachedShadowMapData->ShadowMap.GetSize();
								InOutProjectedShadowInitializer = CachedShadowMapData->Initializer;
								OutNumShadowMaps = 1;
								OutCacheModes[0] = SDCM_MovablePrimitivesOnly;
								--*NumCachesUpdatedThisFrame;
							}
						}
					}
					else
					{
						OutNumShadowMaps = 1;
						OutCacheModes[0] = SDCM_Uncached;
						CachedShadowMapData->ShadowMap.Release();
					}
				}
			}
			else
			{
				OutNumShadowMaps = 1;
				OutCacheModes[0] = SDCM_Uncached;
				CachedShadowMapData->ShadowMap.Release();
			}

			CachedShadowMapData->Initializer = InOutProjectedShadowInitializer;
			CachedShadowMapData->LastUsedTime = RealTime;
		}
		else
		{
			int64 CachedShadowMapsSize = Scene->GetCachedWholeSceneShadowMapsSize();

			if (CachedShadowMapsSize < GWholeSceneShadowCacheMb * 1024 * 1024)
			{
				OutNumShadowMaps = 2;
				// Note: ShadowMap with static primitives rendered first so movable shadowmap can composite
				OutCacheModes[0] = SDCM_StaticPrimitivesOnly;
				OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
				++*NumCachesUpdatedThisFrame;
				Scene->CachedShadowMaps.Add(LightSceneInfo->Id, FCachedShadowMapData(InOutProjectedShadowInitializer, RealTime));
			}
			else
			{
				OutNumShadowMaps = 1;
				OutCacheModes[0] = SDCM_Uncached;
			}
		}
	}
	else
	{
		OutNumShadowMaps = 1;
		OutCacheModes[0] = SDCM_Uncached;
		Scene->CachedShadowMaps.Remove(LightSceneInfo->Id);
	}

	if (OutNumShadowMaps > 0)
	{
		int32 NumOcclusionQueryableShadows = 0;

		for (int32 i = 0; i < OutNumShadowMaps; i++)
		{
			NumOcclusionQueryableShadows += IsShadowCacheModeOcclusionQueryable(OutCacheModes[i]);
		}

		// Verify only one of the shadows will be occlusion queried, since they are all for the same light bounds
		check(NumOcclusionQueryableShadows == 1);
	}
}

typedef TArray<FConvexVolume, TInlineAllocator<8>> FLightViewFrustumConvexHulls;

// Builds a shadow convex hull based on frustum and and (point/spot) light position
// The 'near' plane isn't present in the frustum convex volume (because near = infinite far plane)
// Based on: https://udn.unrealengine.com/questions/410475/improved-culling-of-shadow-casters-for-spotlights.html
void BuildLightViewFrustumConvexHull(const FVector& LightOrigin, const FConvexVolume& Frustum, FConvexVolume& ConvexHull)
{
	// This function assumes that there are 5 planes, which is the case with an infinite projection matrix
	// If this isn't the case, we should really know about it, so assert.
	const int32 EdgeCount = 12;
	const int32 PlaneCount = 5;
	check(Frustum.Planes.Num() == PlaneCount);

	enum EFrustumPlanes
	{
		FLeft,
		FRight,
		FTop,
		FBottom,
		FFar
	};

	const EFrustumPlanes Edges[EdgeCount][2] =
	{
		{ FFar  , FLeft },{ FFar  , FRight  },
		{ FFar  , FTop }, { FFar  , FBottom },
		{ FLeft , FTop }, { FLeft , FBottom },
		{ FRight, FTop }, { FRight, FBottom }
	};

	float Distance[PlaneCount];
	bool  Visible[PlaneCount];

	for (int32 PlaneIndex = 0; PlaneIndex < PlaneCount; ++PlaneIndex)
	{
		const FPlane& Plane = Frustum.Planes[PlaneIndex];
		float Dist = Plane.PlaneDot(LightOrigin);
		bool bVisible = Dist < 0.0f;

		Distance[PlaneIndex] = Dist;
		Visible[PlaneIndex] = bVisible;

		if (bVisible)
		{
			ConvexHull.Planes.Add(Plane);
		}
	}

	for (int32 EdgeIndex = 0; EdgeIndex < EdgeCount; ++EdgeIndex)
	{
		EFrustumPlanes I1 = Edges[EdgeIndex][0];
		EFrustumPlanes I2 = Edges[EdgeIndex][1];

		// Silhouette edge
		if (Visible[I1] != Visible[I2])
		{
			// Add plane that passes through edge and light origin
			FPlane Plane = Frustum.Planes[I1] * Distance[I2] - Frustum.Planes[I2] * Distance[I1];
			if (Visible[I2])
			{
				Plane = Plane.Flip();
			}
			ConvexHull.Planes.Add(Plane);
		}
	}

	ConvexHull.Init();
}

void BuildLightViewFrustumConvexHulls(const FVector& LightOrigin, const TArray<FViewInfo>& Views, FLightViewFrustumConvexHulls& ConvexHulls)
{
	if (GShadowLightViewConvexHullCull == 0)
	{
		return;
	}


	ConvexHulls.Reserve(Views.Num());
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		// for now only support perspective projection as ortho camera shadows are broken anyway
		FViewInfo const& View = Views[ViewIndex];
		if (View.IsPerspectiveProjection())
		{
			FConvexVolume ConvexHull;
			BuildLightViewFrustumConvexHull(LightOrigin, View.ViewFrustum, ConvexHull);
			ConvexHulls.Add(ConvexHull);
		}
	}
}

bool IntersectsConvexHulls(FLightViewFrustumConvexHulls const& ConvexHulls, FBoxSphereBounds const& Bounds)
{
	if (ConvexHulls.Num() == 0)
	{
		return true;
	}

	for (int32 Index = 0; Index < ConvexHulls.Num(); ++Index)
	{
		FConvexVolume const& Hull = ConvexHulls[Index];
		if (Hull.IntersectBox(Bounds.Origin, Bounds.BoxExtent))
		{
			return true;
		}
	}

	return false;
}

/**  Creates a projected shadow for all primitives affected by a light.  If the light doesn't support whole-scene shadows, it returns false.
 * @param LightSceneInfo - The light to create a shadow for.
 * @return true if a whole scene shadow was created
 */
void FSceneRenderer::CreateWholeSceneProjectedShadow(
	FLightSceneInfo* LightSceneInfo,
	uint32& InOutNumPointShadowCachesUpdatedThisFrame,
	uint32& InOutNumSpotShadowCachesUpdatedThisFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_CreateWholeSceneProjectedShadow);
	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

	// early out if shadow resoluion scale is zero
	if (CVarResolutionScaleZeroDisablesSm.GetValueOnRenderThread() != 0 && LightSceneInfo->Proxy->GetShadowResolutionScale() <= 0.0f)
	{
		return;
	}

	// Try to create a whole-scene projected shadow initializer for the light.
	TArray<FWholeSceneProjectedShadowInitializer, TInlineAllocator<6> > ProjectedShadowInitializers;
	if (LightSceneInfo->Proxy->GetWholeSceneProjectedShadowInitializer(ViewFamily, ProjectedShadowInitializers))
	{
		FSceneRenderTargets& SceneContext_ConstantsOnly = FSceneRenderTargets::Get_FrameConstantsOnly();

		checkSlow(ProjectedShadowInitializers.Num() > 0);

		// Shadow resolution constants.
		const uint32 ShadowBorder = ProjectedShadowInitializers[0].bOnePassPointLightShadow ? 0 : SHADOW_BORDER;
		const uint32 EffectiveDoubleShadowBorder = ShadowBorder * 2;
		const uint32 MinShadowResolution = FMath::Max<int32>(0, CVarMinShadowResolution.GetValueOnRenderThread());
		const int32 MaxShadowResolutionSetting = GetCachedScalabilityCVars().MaxShadowResolution;
		const FIntPoint ShadowBufferResolution = SceneContext_ConstantsOnly.GetShadowDepthTextureResolution();
		const uint32 MaxShadowResolution = FMath::Min(MaxShadowResolutionSetting, ShadowBufferResolution.X) - EffectiveDoubleShadowBorder;
		const uint32 MaxShadowResolutionY = FMath::Min(MaxShadowResolutionSetting, ShadowBufferResolution.Y) - EffectiveDoubleShadowBorder;
		const uint32 ShadowFadeResolution = FMath::Max<int32>(0, CVarShadowFadeResolution.GetValueOnRenderThread());

		// Compute the maximum resolution required for the shadow by any view. Also keep track of the unclamped resolution for fading.
		float MaxDesiredResolution = 0;
		TArray<float, TInlineAllocator<2> > FadeAlphas;
		float MaxFadeAlpha = 0;
		bool bStaticSceneOnly = false;
		bool bAnyViewIsSceneCapture = false;

		for(int32 ViewIndex = 0, ViewCount = Views.Num(); ViewIndex < ViewCount; ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];

			const float ScreenRadius = LightSceneInfo->Proxy->GetEffectiveScreenRadius(View.ShadowViewMatrices);

			// Determine the amount of shadow buffer resolution needed for this view.
			float UnclampedResolution = 1.0f;

			switch (LightSceneInfo->Proxy->GetLightType())
			{
			case LightType_Point:
				UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelPointlight.GetValueOnRenderThread();
				break;
			case LightType_Spot:
				UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelSpotlight.GetValueOnRenderThread();
				break;
			case LightType_Rect:
				UnclampedResolution = ScreenRadius * CVarShadowTexelsPerPixelRectlight.GetValueOnRenderThread();
				break;
			default:
				// directional lights are not handled here
				checkf(false, TEXT("Unexpected LightType %d appears in CreateWholeSceneProjectedShadow %s"),
					(int32)LightSceneInfo->Proxy->GetLightType(),
					*LightSceneInfo->Proxy->GetComponentName().ToString());
			}

			// Compute FadeAlpha before ShadowResolutionScale contribution (artists want to modify the softness of the shadow, not change the fade ranges)
			const float FadeAlpha = CalculateShadowFadeAlpha( UnclampedResolution, ShadowFadeResolution, MinShadowResolution ) * LightSceneInfo->Proxy->GetShadowAmount();
			MaxFadeAlpha = FMath::Max(MaxFadeAlpha, FadeAlpha);
			FadeAlphas.Add(FadeAlpha);

			const float ShadowResolutionScale = LightSceneInfo->Proxy->GetShadowResolutionScale();

			float ClampedResolution = UnclampedResolution;

			if (ShadowResolutionScale > 1.0f)
			{
				// Apply ShadowResolutionScale before the MaxShadowResolution clamp if raising the resolution
				ClampedResolution *= ShadowResolutionScale;
			}

			ClampedResolution = FMath::Min<float>(ClampedResolution, MaxShadowResolution);

			if (ShadowResolutionScale <= 1.0f)
			{
				// Apply ShadowResolutionScale after the MaxShadowResolution clamp if lowering the resolution
				// Artists want to modify the softness of the shadow with ShadowResolutionScale
				ClampedResolution *= ShadowResolutionScale;
			}

			MaxDesiredResolution = FMath::Max(
				MaxDesiredResolution,
				FMath::Max<float>(
					ClampedResolution,
					FMath::Min<float>(MinShadowResolution, ShadowBufferResolution.X - EffectiveDoubleShadowBorder)
					)
				);

			bStaticSceneOnly = bStaticSceneOnly || View.bStaticSceneOnly;
			bAnyViewIsSceneCapture = bAnyViewIsSceneCapture || View.bIsSceneCapture;
		}

		if (MaxFadeAlpha > 1.0f / 256.0f)
		{
			Scene->FlushAsyncLightPrimitiveInteractionCreation();

			for (int32 ShadowIndex = 0, ShadowCount = ProjectedShadowInitializers.Num(); ShadowIndex < ShadowCount; ShadowIndex++)
			{
				FWholeSceneProjectedShadowInitializer& ProjectedShadowInitializer = ProjectedShadowInitializers[ShadowIndex];

				// Round down to the nearest power of two so that resolution changes are always doubling or halving the resolution, which increases filtering stability
				// Use the max resolution if the desired resolution is larger than that
				// FMath::CeilLogTwo(MaxDesiredResolution + 1.0f) instead of FMath::CeilLogTwo(MaxDesiredResolution) because FMath::CeilLogTwo takes
				// an uint32 as argument and this causes MaxDesiredResolution get truncated. For example, if MaxDesiredResolution is 256.1f,
				// FMath::CeilLogTwo returns 8 but the next line of code expects a 9 to work correctly
				int32 RoundedDesiredResolution = FMath::Max<int32>((1 << (FMath::CeilLogTwo(MaxDesiredResolution + 1.0f) - 1)) - ShadowBorder * 2, 1);
				int32 SizeX = MaxDesiredResolution >= MaxShadowResolution ? MaxShadowResolution : RoundedDesiredResolution;
				int32 SizeY = MaxDesiredResolution >= MaxShadowResolutionY ? MaxShadowResolutionY : RoundedDesiredResolution;

				if (ProjectedShadowInitializer.bOnePassPointLightShadow)
				{
					// Round to a resolution that is supported for one pass point light shadows
					SizeX = SizeY = SceneContext_ConstantsOnly.GetCubeShadowDepthZResolution(SceneContext_ConstantsOnly.GetCubeShadowDepthZIndex(MaxDesiredResolution));
				}

				int32 NumShadowMaps = 1;
				EShadowDepthCacheMode CacheMode[2] = { SDCM_Uncached, SDCM_Uncached };

				if (!bAnyViewIsSceneCapture && !ProjectedShadowInitializer.bRayTracedDistanceField)
				{
					FIntPoint ShadowMapSize(SizeX + ShadowBorder * 2, SizeY + ShadowBorder * 2);

					ComputeWholeSceneShadowCacheModes(
						LightSceneInfo,
						ProjectedShadowInitializer.bOnePassPointLightShadow,
						ViewFamily.CurrentRealTime,
						MaxDesiredResolution,
						FIntPoint(MaxShadowResolution, MaxShadowResolutionY),
						Scene,
						// Below are in-out or out parameters. They can change
						ProjectedShadowInitializer,
						ShadowMapSize,
						InOutNumPointShadowCachesUpdatedThisFrame,
						InOutNumSpotShadowCachesUpdatedThisFrame,
						NumShadowMaps,
						CacheMode);

					SizeX = ShadowMapSize.X - ShadowBorder * 2;
					SizeY = ShadowMapSize.Y - ShadowBorder * 2;
				}

				for (int32 CacheModeIndex = 0; CacheModeIndex < NumShadowMaps; CacheModeIndex++)
				{
					// Create the projected shadow info.
					FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(), 1, 16) FProjectedShadowInfo;

					ProjectedShadowInfo->SetupWholeSceneProjection(
						LightSceneInfo,
						NULL,
						ProjectedShadowInitializer,
						SizeX,
						SizeY,
						ShadowBorder,
						false	// no RSM
						);

					ProjectedShadowInfo->CacheMode = CacheMode[CacheModeIndex];
					ProjectedShadowInfo->FadeAlphas = FadeAlphas;

					VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);

					if (ProjectedShadowInitializer.bOnePassPointLightShadow)
					{
						const static FVector CubeDirections[6] =
						{
							FVector(-1, 0, 0),
							FVector(1, 0, 0),
							FVector(0, -1, 0),
							FVector(0, 1, 0),
							FVector(0, 0, -1),
							FVector(0, 0, 1)
						};

						const static FVector UpVectors[6] =
						{
							FVector(0, 1, 0),
							FVector(0, 1, 0),
							FVector(0, 0, -1),
							FVector(0, 0, 1),
							FVector(0, 1, 0),
							FVector(0, 1, 0)
						};

						const FLightSceneProxy& LightProxy = *(ProjectedShadowInfo->GetLightSceneInfo().Proxy);

						const FMatrix FaceProjection = FPerspectiveMatrix(PI / 4.0f, 1, 1, 1, LightProxy.GetRadius());

						// Light projection and bounding volume is set up relative to the light position
						// the view pre-translation (relative to light) is added later, when rendering & sampling.
						const FVector LightPosition = ProjectedShadowInitializer.WorldToLight.GetOrigin();

						ProjectedShadowInfo->OnePassShadowViewMatrices.Empty(6);
						ProjectedShadowInfo->OnePassShadowViewProjectionMatrices.Empty(6);
						const FMatrix ScaleMatrix = FScaleMatrix(FVector(1, -1, 1));

						// fill in the caster frustum with the far plane from every face
						ProjectedShadowInfo->CasterFrustum.Planes.Empty();
						for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
						{
							// Create a view projection matrix for each cube face
							const FMatrix WorldToLightMatrix = FLookFromMatrix(LightPosition, CubeDirections[FaceIndex], UpVectors[FaceIndex]) * ScaleMatrix;
							ProjectedShadowInfo->OnePassShadowViewMatrices.Add(WorldToLightMatrix);
							const FMatrix ShadowViewProjectionMatrix = WorldToLightMatrix * FaceProjection;
							ProjectedShadowInfo->OnePassShadowViewProjectionMatrices.Add(ShadowViewProjectionMatrix);
							// Add plane representing cube face to bounding volume
							ProjectedShadowInfo->CasterFrustum.Planes.Add(FPlane(CubeDirections[FaceIndex], LightProxy.GetRadius()));
						}
						ProjectedShadowInfo->CasterFrustum.Init();
					}

					// Ray traced shadows use the GPU managed distance field object buffers, no CPU culling should be used
					if (!ProjectedShadowInfo->bRayTracedDistanceField)
					{
						// Build light-view convex hulls for shadow caster culling
						FLightViewFrustumConvexHulls LightViewFrustumConvexHulls;
						if (CacheMode[CacheModeIndex] != SDCM_StaticPrimitivesOnly)
						{
							FVector const& LightOrigin = LightSceneInfo->Proxy->GetOrigin();
							BuildLightViewFrustumConvexHulls(LightOrigin, Views, LightViewFrustumConvexHulls);
						}

						bool bCastCachedShadowFromMovablePrimitives = GCachedShadowsCastFromMovablePrimitives || LightSceneInfo->Proxy->GetForceCachedShadowsForMovablePrimitives();
						if (CacheMode[CacheModeIndex] != SDCM_StaticPrimitivesOnly 
							&& (CacheMode[CacheModeIndex] != SDCM_MovablePrimitivesOnly || bCastCachedShadowFromMovablePrimitives))
						{
							// Add all the shadow casting primitives affected by the light to the shadow's subject primitive list.
							for (FLightPrimitiveInteraction* Interaction = LightSceneInfo->GetDynamicInteractionOftenMovingPrimitiveList(false);
								Interaction;
								Interaction = Interaction->GetNextPrimitive())
							{
								if (Interaction->HasShadow()
									// If the primitive only wants to cast a self shadow don't include it in whole scene shadows.
									&& !Interaction->CastsSelfShadowOnly()
									&& (!bStaticSceneOnly || Interaction->GetPrimitiveSceneInfo()->Proxy->HasStaticLighting()))
								{
									FBoxSphereBounds const& Bounds = Interaction->GetPrimitiveSceneInfo()->Proxy->GetBounds();
									if (IntersectsConvexHulls(LightViewFrustumConvexHulls, Bounds))
									{
										ProjectedShadowInfo->AddSubjectPrimitive(Interaction->GetPrimitiveSceneInfo(), &Views, FeatureLevel, false);
									}
								}
							}
						}
						
						if (CacheMode[CacheModeIndex] != SDCM_MovablePrimitivesOnly)
						{
							// Add all the shadow casting primitives affected by the light to the shadow's subject primitive list.
							for (FLightPrimitiveInteraction* Interaction = LightSceneInfo->GetDynamicInteractionStaticPrimitiveList(false);
								Interaction;
								Interaction = Interaction->GetNextPrimitive())
							{
								if (Interaction->HasShadow()
									// If the primitive only wants to cast a self shadow don't include it in whole scene shadows.
									&& !Interaction->CastsSelfShadowOnly()
									&& (!bStaticSceneOnly || Interaction->GetPrimitiveSceneInfo()->Proxy->HasStaticLighting()))
								{
									FBoxSphereBounds const& Bounds = Interaction->GetPrimitiveSceneInfo()->Proxy->GetBounds();
									if (IntersectsConvexHulls(LightViewFrustumConvexHulls, Bounds))
									{
										ProjectedShadowInfo->AddSubjectPrimitive(Interaction->GetPrimitiveSceneInfo(), &Views, FeatureLevel, false);
									}
								}
							}
						}
					}

					bool bRenderShadow = true;
					
					if (CacheMode[CacheModeIndex] == SDCM_StaticPrimitivesOnly)
					{
						const bool bHasStaticPrimitives = ProjectedShadowInfo->HasSubjectPrims();
						bRenderShadow = bHasStaticPrimitives;
						FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
						CachedShadowMapData.bCachedShadowMapHasPrimitives = bHasStaticPrimitives;
					}

					if (bRenderShadow)
					{
						VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
					}
				}
			}
		}
	}
}

void FSceneRenderer::InitProjectedShadowVisibility(FRHICommandListImmediate& RHICmdList)
{
	SCOPE_CYCLE_COUNTER(STAT_InitProjectedShadowVisibility);
	int32 NumBufferedFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(FeatureLevel);

	// Initialize the views' ProjectedShadowVisibilityMaps and remove shadows without subjects.
	for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights);LightIt;++LightIt)
	{
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];

		// Allocate the light's projected shadow visibility and view relevance maps for this view.
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];
			VisibleLightViewInfo.ProjectedShadowVisibilityMap.Init(false,VisibleLightInfo.AllProjectedShadows.Num());
			VisibleLightViewInfo.ProjectedShadowViewRelevanceMap.Empty(VisibleLightInfo.AllProjectedShadows.Num());
			VisibleLightViewInfo.ProjectedShadowViewRelevanceMap.AddZeroed(VisibleLightInfo.AllProjectedShadows.Num());
		}

		for( int32 ShadowIndex=0; ShadowIndex<VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++ )
		{
			FProjectedShadowInfo& ProjectedShadowInfo = *VisibleLightInfo.AllProjectedShadows[ShadowIndex];

			// Assign the shadow its id.
			ProjectedShadowInfo.ShadowId = ShadowIndex;

			for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];

				if (ProjectedShadowInfo.DependentView && ProjectedShadowInfo.DependentView != &View)
				{
					// The view dependent projected shadow is valid for this view if it's the
					// right eye and the projected shadow is being rendered for the left eye.
					const bool bIsValidForView = IStereoRendering::IsASecondaryView(View)
						&& IStereoRendering::IsAPrimaryView(*ProjectedShadowInfo.DependentView)
						&& ProjectedShadowInfo.FadeAlphas.IsValidIndex(ViewIndex)
						&& ProjectedShadowInfo.FadeAlphas[ViewIndex] == 1.0f;

					if (!bIsValidForView)
					{
						continue;
					}
				}

				FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];

				if(VisibleLightViewInfo.bInViewFrustum)
				{
					// Compute the subject primitive's view relevance.  Note that the view won't necessarily have it cached,
					// since the primitive might not be visible.
					FPrimitiveViewRelevance ViewRelevance;
					if(ProjectedShadowInfo.GetParentSceneInfo())
					{
						ViewRelevance = ProjectedShadowInfo.GetParentSceneInfo()->Proxy->GetViewRelevance(&View);
					}
					else
					{
						ViewRelevance.bDrawRelevance = ViewRelevance.bStaticRelevance = ViewRelevance.bDynamicRelevance = ViewRelevance.bShadowRelevance = true;
					}							
					VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowIndex] = ViewRelevance;

					// Check if the subject primitive's shadow is view relevant.
					const bool bPrimitiveIsShadowRelevant = ViewRelevance.bShadowRelevance;

					bool bShadowIsOccluded = false;

					if (!View.bIgnoreExistingQueries && View.State)
					{
						// Check if the shadow is occluded.
						bShadowIsOccluded =
							((FSceneViewState*)View.State)->IsShadowOccluded(
							RHICmdList,
							FSceneViewState::FProjectedShadowKey(ProjectedShadowInfo),
							NumBufferedFrames
							);
					}

					// The shadow is visible if it is view relevant and unoccluded.
					if(bPrimitiveIsShadowRelevant && !bShadowIsOccluded)
					{
						VisibleLightViewInfo.ProjectedShadowVisibilityMap[ShadowIndex] = true;
					}

					// Draw the shadow frustum.
					if(bPrimitiveIsShadowRelevant && !bShadowIsOccluded && !ProjectedShadowInfo.bReflectiveShadowmap)  
					{
						bool bDrawPreshadowFrustum = CVarDrawPreshadowFrustum.GetValueOnRenderThread() != 0;

						if ((ViewFamily.EngineShowFlags.ShadowFrustums)
							&& ((bDrawPreshadowFrustum && ProjectedShadowInfo.bPreShadow) || (!bDrawPreshadowFrustum && !ProjectedShadowInfo.bPreShadow)))
						{
							FViewElementPDI ShadowFrustumPDI(&Views[ViewIndex], nullptr, &Views[ViewIndex].DynamicPrimitiveShaderData);
							
							if(ProjectedShadowInfo.IsWholeSceneDirectionalShadow())
							{
								// Get split color
								FColor Color = FColor::White;
								switch(ProjectedShadowInfo.CascadeSettings.ShadowSplitIndex)
								{
									case 0: Color = FColor::Red; break;
									case 1: Color = FColor::Yellow; break;
									case 2: Color = FColor::Green; break;
									case 3: Color = FColor::Blue; break;
								}

								const FMatrix ViewMatrix = View.ViewMatrices.GetViewMatrix();
								const FMatrix ProjectionMatrix = View.ViewMatrices.GetProjectionMatrix();
								const FVector4 ViewOrigin = View.ViewMatrices.GetViewOrigin();

								float AspectRatio = ProjectionMatrix.M[1][1] / ProjectionMatrix.M[0][0];
								float ActualFOV = (ViewOrigin.W > 0.0f) ? FMath::Atan(1.0f / ProjectionMatrix.M[0][0]) : PI/4.0f;

								float Near = ProjectedShadowInfo.CascadeSettings.SplitNear;
								float Mid = ProjectedShadowInfo.CascadeSettings.FadePlaneOffset;
								float Far = ProjectedShadowInfo.CascadeSettings.SplitFar;

								// Camera Subfrustum
								DrawFrustumWireframe(&ShadowFrustumPDI, (ViewMatrix * FPerspectiveMatrix(ActualFOV, AspectRatio, 1.0f, Near, Mid)).Inverse(), Color, 0);
								DrawFrustumWireframe(&ShadowFrustumPDI, (ViewMatrix * FPerspectiveMatrix(ActualFOV, AspectRatio, 1.0f, Mid, Far)).Inverse(), FColor::White, 0);

								// Subfrustum Sphere Bounds
								//DrawWireSphere(&ShadowFrustumPDI, FTransform(ProjectedShadowInfo.ShadowBounds.Center), Color, ProjectedShadowInfo.ShadowBounds.W, 40, 0);

								// Shadow Map Projection Bounds
								DrawFrustumWireframe(&ShadowFrustumPDI, ProjectedShadowInfo.SubjectAndReceiverMatrix.Inverse() * FTranslationMatrix(-ProjectedShadowInfo.PreShadowTranslation), Color, 0);
							}
							else
							{
								ProjectedShadowInfo.RenderFrustumWireframe(&ShadowFrustumPDI);
							}
						}
					}
				}
			}
		}
	}

#if !UE_BUILD_SHIPPING
	if(GDumpShadowSetup)
	{
		GDumpShadowSetup = false;

		UE_LOG(LogRenderer, Display, TEXT("Dump Shadow Setup:"));

		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			UE_LOG(LogRenderer, Display, TEXT(" View  %d/%d"), ViewIndex, Views.Num());

			uint32 LightIndex = 0;
			for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt, ++LightIndex)
			{
				FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];
				FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];

				UE_LOG(LogRenderer, Display, TEXT("  Light %d/%d:"), LightIndex, Scene->Lights.Num());

				for( int32 ShadowIndex = 0, ShadowCount = VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex < ShadowCount; ShadowIndex++ )
				{
					FProjectedShadowInfo& ProjectedShadowInfo = *VisibleLightInfo.AllProjectedShadows[ShadowIndex];

					if(VisibleLightViewInfo.bInViewFrustum)
					{
						UE_LOG(LogRenderer, Display, TEXT("   Shadow %d/%d: ShadowId=%d"),  ShadowIndex, ShadowCount, ProjectedShadowInfo.ShadowId);
						UE_LOG(LogRenderer, Display, TEXT("    WholeSceneDir=%d SplitIndex=%d near=%f far=%f"),
							ProjectedShadowInfo.IsWholeSceneDirectionalShadow(),
							ProjectedShadowInfo.CascadeSettings.ShadowSplitIndex,
							ProjectedShadowInfo.CascadeSettings.SplitNear,
							ProjectedShadowInfo.CascadeSettings.SplitFar);
						UE_LOG(LogRenderer, Display, TEXT("    bDistField=%d bFarShadows=%d Bounds=%f,%f,%f,%f"),
							ProjectedShadowInfo.bRayTracedDistanceField,
							ProjectedShadowInfo.CascadeSettings.bFarShadowCascade,
							ProjectedShadowInfo.ShadowBounds.Center.X,
							ProjectedShadowInfo.ShadowBounds.Center.Y,
							ProjectedShadowInfo.ShadowBounds.Center.Z,
							ProjectedShadowInfo.ShadowBounds.W);
						UE_LOG(LogRenderer, Display, TEXT("    SplitFadeRegion=%f .. %f FadePlaneOffset=%f FadePlaneLength=%f"),
							ProjectedShadowInfo.CascadeSettings.SplitNearFadeRegion,
							ProjectedShadowInfo.CascadeSettings.SplitFarFadeRegion,
							ProjectedShadowInfo.CascadeSettings.FadePlaneOffset,
							ProjectedShadowInfo.CascadeSettings.FadePlaneLength);			
					}
				}
			}
		}
	}
#endif // !UE_BUILD_SHIPPING
}

void FSceneRenderer::GatherShadowDynamicMeshElements(FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	TArray<const FSceneView*> ReusedViewsArray;
	ReusedViewsArray.AddZeroed(1);

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.ShadowMapAtlases.Num(); AtlasIndex++)
	{
		FSortedShadowMapAtlas& Atlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases[AtlasIndex];

		for (int32 ShadowIndex = 0; ShadowIndex < Atlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = Atlas.Shadows[ShadowIndex];
			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[ProjectedShadowInfo->GetLightSceneInfo().Id];
			ProjectedShadowInfo->GatherDynamicMeshElements(*this, VisibleLightInfo, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
		}
	}

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.RSMAtlases.Num(); AtlasIndex++)
	{
		FSortedShadowMapAtlas& Atlas = SortedShadowsForShadowDepthPass.RSMAtlases[AtlasIndex];

		for (int32 ShadowIndex = 0; ShadowIndex < Atlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = Atlas.Shadows[ShadowIndex];
			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[ProjectedShadowInfo->GetLightSceneInfo().Id];
			ProjectedShadowInfo->GatherDynamicMeshElements(*this, VisibleLightInfo, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
		}
	}

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.ShadowMapCubemaps.Num(); AtlasIndex++)
	{
		FSortedShadowMapAtlas& Atlas = SortedShadowsForShadowDepthPass.ShadowMapCubemaps[AtlasIndex];

		for (int32 ShadowIndex = 0; ShadowIndex < Atlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = Atlas.Shadows[ShadowIndex];
			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[ProjectedShadowInfo->GetLightSceneInfo().Id];
			ProjectedShadowInfo->GatherDynamicMeshElements(*this, VisibleLightInfo, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
		}
	}

	for (int32 ShadowIndex = 0; ShadowIndex < SortedShadowsForShadowDepthPass.PreshadowCache.Shadows.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = SortedShadowsForShadowDepthPass.PreshadowCache.Shadows[ShadowIndex];
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[ProjectedShadowInfo->GetLightSceneInfo().Id];
		ProjectedShadowInfo->GatherDynamicMeshElements(*this, VisibleLightInfo, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
	}

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases.Num(); AtlasIndex++)
	{
		FSortedShadowMapAtlas& Atlas = SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases[AtlasIndex];

		for (int32 ShadowIndex = 0; ShadowIndex < Atlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = Atlas.Shadows[ShadowIndex];
			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[ProjectedShadowInfo->GetLightSceneInfo().Id];
			ProjectedShadowInfo->GatherDynamicMeshElements(*this, VisibleLightInfo, ReusedViewsArray, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);
		}
	}
}

typedef TArray<FAddSubjectPrimitiveOp> FShadowSubjectPrimitives;
typedef TArray<FAddSubjectPrimitiveStats, TInlineAllocator<4, SceneRenderingAllocator>> FPerShadowGatherStats;
typedef TArray<FAddSubjectPrimitiveOverflowedIndices, SceneRenderingAllocator> FPerShadowOverflowedIndices;

struct FGatherShadowPrimitivesPacket
{
	// Inputs
	const FScene* Scene;
	TArray<FViewInfo>& Views;
	const FScenePrimitiveOctree::FNodeIndex NodeIndex;
	int32 StartPrimitiveIndex;
	int32 NumPrimitives;
	const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& PreShadows;
	const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows;
	ERHIFeatureLevel::Type FeatureLevel;
	bool bStaticSceneOnly;

	// Scratch
	FPerShadowGatherStats ViewDependentWholeSceneShadowStats;
	FPerShadowOverflowedIndices PreShadowOverflowedIndices;
	FPerShadowOverflowedIndices ViewDependentWholeSceneShadowOverflowedIndices;
	TArray<FShadowSubjectPrimitives, SceneRenderingAllocator> PreShadowSubjectPrimitives;
	TArray<FShadowSubjectPrimitives, SceneRenderingAllocator> ViewDependentWholeSceneShadowSubjectPrimitives;

	// Outputs
	FPerShadowGatherStats& GlobalStats;

	FGatherShadowPrimitivesPacket(
		const FScene* InScene,
		TArray<FViewInfo>& InViews,
		const FScenePrimitiveOctree::FNodeIndex InNodeIndex,
		int32 InStartPrimitiveIndex,
		int32 InNumPrimitives,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& InPreShadows,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& InViewDependentWholeSceneShadows,
		ERHIFeatureLevel::Type InFeatureLevel,
		bool bInStaticSceneOnly,
		FPerShadowGatherStats& OutGlobalStats) 
		: Scene(InScene)
		, Views(InViews)
		, NodeIndex(InNodeIndex)
		, StartPrimitiveIndex(InStartPrimitiveIndex)
		, NumPrimitives(InNumPrimitives)
		, PreShadows(InPreShadows)
		, ViewDependentWholeSceneShadows(InViewDependentWholeSceneShadows)
		, FeatureLevel(InFeatureLevel)
		, bStaticSceneOnly(bInStaticSceneOnly)
		, GlobalStats(OutGlobalStats)
	{
		const int32 NumPreShadows = PreShadows.Num();
		const int32 NumVDWSShadows = ViewDependentWholeSceneShadows.Num();

		check(GlobalStats.Num() == NumVDWSShadows);
		ViewDependentWholeSceneShadowStats.Empty(NumVDWSShadows);
		ViewDependentWholeSceneShadowStats.AddDefaulted(NumVDWSShadows);

		PreShadowOverflowedIndices.Empty(NumPreShadows);
		PreShadowOverflowedIndices.AddDefaulted(NumPreShadows);

		ViewDependentWholeSceneShadowOverflowedIndices.Empty(NumVDWSShadows);
		ViewDependentWholeSceneShadowOverflowedIndices.AddDefaulted(NumVDWSShadows);

		PreShadowSubjectPrimitives.Empty(NumPreShadows);
		PreShadowSubjectPrimitives.AddDefaulted(NumPreShadows);

		ViewDependentWholeSceneShadowSubjectPrimitives.Empty(NumVDWSShadows);
		ViewDependentWholeSceneShadowSubjectPrimitives.AddDefaulted(NumVDWSShadows);
	}

	void AnyThreadTask()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GatherShadowPrimitivesPacket);

		if (NodeIndex != INDEX_NONE)
		{
			// Check all the primitives in this octree node.
			for (const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact : Scene->PrimitiveOctree.GetElementsForNode(NodeIndex))
			{
				if (PrimitiveSceneInfoCompact.PrimitiveFlagsCompact.bCastDynamicShadow)
				{
					FilterPrimitiveForShadows(PrimitiveSceneInfoCompact);
				}
			}
		}
		else
		{
			check(NumPrimitives > 0);

			// Check primitives in this packet's range
			for (int32 PrimitiveIndex = StartPrimitiveIndex; PrimitiveIndex < StartPrimitiveIndex + NumPrimitives; PrimitiveIndex++)
			{
				const FPrimitiveFlagsCompact PrimitiveFlagsCompact = Scene->PrimitiveFlagsCompact[PrimitiveIndex];

				if (PrimitiveFlagsCompact.bCastDynamicShadow)
				{
					FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
					const FPrimitiveSceneInfoCompact PrimitiveSceneInfoCompact(PrimitiveSceneInfo);

					FilterPrimitiveForShadows(PrimitiveSceneInfoCompact);
				}
			}
		}

		const int32 NumStatsToMerge = ViewDependentWholeSceneShadowStats.Num();
		for (int32 StatIdx = 0; StatIdx < NumStatsToMerge; ++StatIdx)
		{
			GlobalStats[StatIdx].InterlockedAdd(ViewDependentWholeSceneShadowStats[StatIdx]);
		}
	}

	bool DoesPrimitiveCastInsetShadow(const FPrimitiveSceneInfo* PrimitiveSceneInfo, const FPrimitiveSceneProxy* PrimitiveProxy) const
	{
		// If light attachment root is valid, we're in a group and need to get the flag from the root.
		if (PrimitiveSceneInfo->LightingAttachmentRoot.IsValid())
		{
			const FAttachmentGroupSceneInfo& AttachmentGroup = PrimitiveSceneInfo->Scene->AttachmentGroups.FindChecked(PrimitiveSceneInfo->LightingAttachmentRoot);
			return AttachmentGroup.ParentSceneInfo && AttachmentGroup.ParentSceneInfo->Proxy->CastsInsetShadow();
		}
		else
		{
			return PrimitiveProxy->CastsInsetShadow();
		}
	}

	void FilterPrimitiveForShadows(const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact)
	{
		const FPrimitiveFlagsCompact& PrimitiveFlagsCompact = PrimitiveSceneInfoCompact.PrimitiveFlagsCompact;
		const FBoxSphereBounds& PrimitiveBounds = PrimitiveSceneInfoCompact.Bounds;
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneInfoCompact.PrimitiveSceneInfo;
		const FPrimitiveSceneProxy* PrimitiveProxy = PrimitiveSceneInfoCompact.Proxy;

		// Check if the primitive is a subject for any of the preshadows.
		// Only allow preshadows from lightmapped primitives that cast both dynamic and static shadows.
		if (PreShadows.Num() && PrimitiveFlagsCompact.bCastStaticShadow && PrimitiveFlagsCompact.bStaticLighting)
		{
			for (int32 ShadowIndex = 0, Num = PreShadows.Num(); ShadowIndex < Num; ShadowIndex++)
			{
				FProjectedShadowInfo* RESTRICT ProjectedShadowInfo = PreShadows[ShadowIndex];

				// Note: Culling based on the primitive's bounds BEFORE dereferencing PrimitiveSceneInfo / PrimitiveProxy
				// Check if this primitive is in the shadow's frustum.
				bool bInFrustum = ProjectedShadowInfo->CasterFrustum.IntersectBox(PrimitiveBounds.Origin, ProjectedShadowInfo->PreShadowTranslation, PrimitiveBounds.BoxExtent);

				if (bInFrustum && ProjectedShadowInfo->GetLightSceneInfoCompact().AffectsPrimitive(PrimitiveBounds, PrimitiveProxy))
				{
					FAddSubjectPrimitiveResult Result;
					FAddSubjectPrimitiveStats UnusedStats;
					Result.Qword = ProjectedShadowInfo->AddSubjectPrimitive_AnyThread(PrimitiveSceneInfoCompact, &Views, FeatureLevel, UnusedStats, PreShadowOverflowedIndices[ShadowIndex]);

					if (!!Result.Qword)
					{
						FShadowSubjectPrimitives& SubjectPrimitives = PreShadowSubjectPrimitives[ShadowIndex];
						FAddSubjectPrimitiveOp& Op = SubjectPrimitives[SubjectPrimitives.AddUninitialized()];
						Op.PrimitiveSceneInfo = PrimitiveSceneInfo;
						Op.Result.Qword = Result.Qword;
					}
				}
			}
		}

		for (int32 ShadowIndex = 0, Num = ViewDependentWholeSceneShadows.Num();ShadowIndex < Num;ShadowIndex++)
		{
			const FProjectedShadowInfo* RESTRICT ProjectedShadowInfo = ViewDependentWholeSceneShadows[ShadowIndex];
			const FLightSceneInfo& RESTRICT LightSceneInfo = ProjectedShadowInfo->GetLightSceneInfo();
			const FLightSceneProxy& RESTRICT LightProxy = *LightSceneInfo.Proxy;

			const FVector LightDirection = LightProxy.GetDirection();
			const FVector PrimitiveToShadowCenter = ProjectedShadowInfo->ShadowBounds.Center - PrimitiveBounds.Origin;
			// Project the primitive's bounds origin onto the light vector
			const float ProjectedDistanceFromShadowOriginAlongLightDir = PrimitiveToShadowCenter | LightDirection;
			// Calculate the primitive's squared distance to the cylinder's axis
			const float PrimitiveDistanceFromCylinderAxisSq = (-LightDirection * ProjectedDistanceFromShadowOriginAlongLightDir + PrimitiveToShadowCenter).SizeSquared();
			const float CombinedRadiusSq = FMath::Square(ProjectedShadowInfo->ShadowBounds.W + PrimitiveBounds.SphereRadius);

			// Note: Culling based on the primitive's bounds BEFORE dereferencing PrimitiveSceneInfo / PrimitiveProxy

			// Check if this primitive is in the shadow's cylinder
			if (PrimitiveDistanceFromCylinderAxisSq < CombinedRadiusSq
				// If the primitive is further along the cone axis than the shadow bounds origin, 
				// Check if the primitive is inside the spherical cap of the cascade's bounds
				&& !(ProjectedDistanceFromShadowOriginAlongLightDir < 0 && PrimitiveToShadowCenter.SizeSquared() > CombinedRadiusSq)
				// Test against the convex hull containing the extruded shadow bounds
				&& ProjectedShadowInfo->CascadeSettings.ShadowBoundsAccurate.IntersectBox(PrimitiveBounds.Origin, PrimitiveBounds.BoxExtent))
			{
				// Distance culling for RSMs
				const float MinScreenRadiusForShadowCaster = ProjectedShadowInfo->bReflectiveShadowmap ? GMinScreenRadiusForShadowCasterRSM : GMinScreenRadiusForShadowCaster;

				bool bScreenSpaceSizeCulled = false;
				check(ProjectedShadowInfo->DependentView);

				{
					const float DistanceSquared = (PrimitiveBounds.Origin - ProjectedShadowInfo->DependentView->ShadowViewMatrices.GetViewOrigin()).SizeSquared();
					const float LODScaleSquared = FMath::Square(ProjectedShadowInfo->DependentView->LODDistanceFactor);
					bScreenSpaceSizeCulled = FMath::Square(PrimitiveBounds.SphereRadius) < FMath::Square(MinScreenRadiusForShadowCaster) * DistanceSquared * LODScaleSquared;
				}

				if (!bScreenSpaceSizeCulled
					&& ProjectedShadowInfo->GetLightSceneInfoCompact().AffectsPrimitive(PrimitiveBounds, PrimitiveProxy)
					// Include all primitives for movable lights, but only statically shadowed primitives from a light with static shadowing,
					// Since lights with static shadowing still create per-object shadows for primitives without static shadowing.
					&& (!LightProxy.HasStaticLighting() || (!LightSceneInfo.IsPrecomputedLightingValid() || LightProxy.UseCSMForDynamicObjects()))
					// Only render primitives into a reflective shadowmap that are supposed to affect indirect lighting
					&& !(ProjectedShadowInfo->bReflectiveShadowmap && !PrimitiveProxy->AffectsDynamicIndirectLighting())
					// Exclude primitives that will create their own per-object shadow, except when rendering RSMs
					&& (!DoesPrimitiveCastInsetShadow(PrimitiveSceneInfo, PrimitiveProxy) || ProjectedShadowInfo->bReflectiveShadowmap)
					// Exclude primitives that will create a per-object shadow from a stationary light
					&& !ShouldCreateObjectShadowForStationaryLight(&LightSceneInfo, PrimitiveProxy, true)
					// Only render shadows from objects that use static lighting during a reflection capture, since the reflection capture doesn't update at runtime
					&& (!bStaticSceneOnly || PrimitiveProxy->HasStaticLighting())
					// Render dynamic lit objects if CSMForDynamicObjects is enabled.
					&& (!LightProxy.UseCSMForDynamicObjects() || !PrimitiveProxy->HasStaticLighting()))
				{
					FAddSubjectPrimitiveResult Result;
					Result.Qword = ProjectedShadowInfo->AddSubjectPrimitive_AnyThread(
						PrimitiveSceneInfoCompact,
						nullptr,
						FeatureLevel,
						ViewDependentWholeSceneShadowStats[ShadowIndex],
						ViewDependentWholeSceneShadowOverflowedIndices[ShadowIndex]);

					if (!!Result.Qword)
					{
						FShadowSubjectPrimitives& SubjectPrimitives = ViewDependentWholeSceneShadowSubjectPrimitives[ShadowIndex];
						if (!SubjectPrimitives.Num())
						{
							SubjectPrimitives.Reserve(16);
						}
						FAddSubjectPrimitiveOp& Op = SubjectPrimitives[SubjectPrimitives.AddUninitialized()];
						Op.PrimitiveSceneInfo = PrimitiveSceneInfo;
						Op.Result.Qword = Result.Qword;
					}
				}
			}
		}
	}

	void RenderThreadFinalize()
	{
		for (int32 ShadowIndex = 0; ShadowIndex < PreShadowSubjectPrimitives.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = PreShadows[ShadowIndex];
			const FShadowSubjectPrimitives& SubjectPrimitives = PreShadowSubjectPrimitives[ShadowIndex];
			const FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer = PreShadowOverflowedIndices[ShadowIndex];
			FFinalizeAddSubjectPrimitiveContext Context;
			Context.OverflowedMDCIndices = OverflowBuffer.MDCIndices.GetData();
			Context.OverflowedMeshIndices = OverflowBuffer.MeshIndices.GetData();

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < SubjectPrimitives.Num(); PrimitiveIndex++)
			{
				ProjectedShadowInfo->FinalizeAddSubjectPrimitive(SubjectPrimitives[PrimitiveIndex], &Views, FeatureLevel, Context);
			}
		}

		for (int32 ShadowIndex = 0; ShadowIndex < ViewDependentWholeSceneShadowSubjectPrimitives.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = ViewDependentWholeSceneShadows[ShadowIndex];
			const FShadowSubjectPrimitives& SubjectPrimitives = ViewDependentWholeSceneShadowSubjectPrimitives[ShadowIndex];
			const FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer = ViewDependentWholeSceneShadowOverflowedIndices[ShadowIndex];
			FFinalizeAddSubjectPrimitiveContext Context;
			Context.OverflowedMDCIndices = OverflowBuffer.MDCIndices.GetData();
			Context.OverflowedMeshIndices = OverflowBuffer.MeshIndices.GetData();

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < SubjectPrimitives.Num(); PrimitiveIndex++)
			{
				ProjectedShadowInfo->FinalizeAddSubjectPrimitive(SubjectPrimitives[PrimitiveIndex], nullptr, FeatureLevel, Context);
			}
		}
	}
};

void FSceneRenderer::GatherShadowPrimitives(
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& PreShadows,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	bool bStaticSceneOnly
	)
{
	SCOPE_CYCLE_COUNTER(STAT_GatherShadowPrimitivesTime);

	if (PreShadows.Num() || ViewDependentWholeSceneShadows.Num())
	{
		TArray<FGatherShadowPrimitivesPacket*,SceneRenderingAllocator> Packets;
		FPerShadowGatherStats GatherStats;

		GatherStats.AddDefaulted(ViewDependentWholeSceneShadows.Num());

		if (GUseOctreeForShadowCulling)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_ShadowSceneOctreeTraversal);

			Packets.Reserve(100);

			// Find primitives that are in a shadow frustum in the octree.
			Scene->PrimitiveOctree.FindNodesWithPredicate([&PreShadows, &ViewDependentWholeSceneShadows](const FBoxCenterAndExtent& NodeBounds)
			{
				// Check that the child node is in the frustum for at least one shadow.

				// Check for subjects of preshadows.
				for (int32 ShadowIndex = 0, Num = PreShadows.Num(); ShadowIndex < Num; ShadowIndex++)
				{
					FProjectedShadowInfo* ProjectedShadowInfo = PreShadows[ShadowIndex];

					check(ProjectedShadowInfo->CasterFrustum.PermutedPlanes.Num());
					// Check if this primitive is in the shadow's frustum.
					if (ProjectedShadowInfo->CasterFrustum.IntersectBox(
						NodeBounds.Center + ProjectedShadowInfo->PreShadowTranslation,
						NodeBounds.Extent
					))
					return true;
				}

				for (int32 ShadowIndex = 0, Num = ViewDependentWholeSceneShadows.Num(); ShadowIndex < Num; ShadowIndex++)
				{
					FProjectedShadowInfo* ProjectedShadowInfo = ViewDependentWholeSceneShadows[ShadowIndex];

					//check(ProjectedShadowInfo->CasterFrustum.PermutedPlanes.Num());
					// Check if this primitive is in the shadow's frustum.
					if (ProjectedShadowInfo->CasterFrustum.IntersectBox(
						NodeBounds.Center + ProjectedShadowInfo->PreShadowTranslation,
						NodeBounds.Extent
					))
					return true;
				}

				// If the child node was in the frustum of at least one preshadow, push it on
				// the iterator's pending node stack.
				return false;
			},
			[this, &PreShadows, &ViewDependentWholeSceneShadows, bStaticSceneOnly, &GatherStats, &Packets](FScenePrimitiveOctree::FNodeIndex NodeIndex)
			{
				if (Scene->PrimitiveOctree.GetElementsForNode(NodeIndex).Num() > 0)
				{
					FGatherShadowPrimitivesPacket* Packet = new(FMemStack::Get()) FGatherShadowPrimitivesPacket(
						Scene,
						Views,
						NodeIndex,
						0,
						0,
						PreShadows,
						ViewDependentWholeSceneShadows,
						FeatureLevel,
						bStaticSceneOnly,
						GatherStats);
					Packets.Add(Packet);
				}
			});
		}
		else
		{
			const int32 PacketSize = CVarParallelGatherNumPrimitivesPerPacket.GetValueOnRenderThread();
			const int32 NumPackets = FMath::DivideAndRoundUp(Scene->Primitives.Num(), PacketSize);
			
			Packets.Reserve(NumPackets);

			for (int32 PacketIndex = 0; PacketIndex < NumPackets; PacketIndex++)
			{
				const int32 StartPrimitiveIndex = PacketIndex * PacketSize;
				const int32 NumPrimitives = FMath::Min(PacketSize, Scene->Primitives.Num() - StartPrimitiveIndex);
				FGatherShadowPrimitivesPacket* Packet = new(FMemStack::Get()) FGatherShadowPrimitivesPacket(
					Scene,
					Views,
					INDEX_NONE,
					StartPrimitiveIndex,
					NumPrimitives,
					PreShadows,
					ViewDependentWholeSceneShadows,
					FeatureLevel,
					bStaticSceneOnly,
					GatherStats);
				Packets.Add(Packet);
			}
		}
			
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FilterPrimitivesForShadows);

			ParallelFor(Packets.Num(), 
				[&Packets](int32 Index)
				{
					Packets[Index]->AnyThreadTask();
				},
				!(FApp::ShouldUseThreadingForPerformance() && CVarParallelGatherShadowPrimitives.GetValueOnRenderThread() > 0)
			);
		}

		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderThreadFinalize);

			for (int32 ShadowIdx = 0, Num = ViewDependentWholeSceneShadows.Num(); ShadowIdx < Num; ++ShadowIdx)
			{
				ViewDependentWholeSceneShadows[ShadowIdx]->PresizeSubjectPrimitiveArrays(GatherStats[ShadowIdx]);
			}

			for (int32 PacketIndex = 0; PacketIndex < Packets.Num(); PacketIndex++)
			{
				FGatherShadowPrimitivesPacket* Packet = Packets[PacketIndex];
				Packet->RenderThreadFinalize();
				// Class was allocated on the memstack which does not call destructors
				Packet->~FGatherShadowPrimitivesPacket();
			}
		}
	}
}

static bool NeedsUnatlasedCSMDepthsWorkaround(ERHIFeatureLevel::Type FeatureLevel)
{
	// UE-42131: Excluding mobile from this, mobile renderer relies on the depth texture border.
	return GRHINeedsUnatlasedCSMDepthsWorkaround && (FeatureLevel >= ERHIFeatureLevel::SM5);
}

void FSceneRenderer::AddViewDependentWholeSceneShadowsForView(
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfos, 
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfosThatNeedCulling,
	FVisibleLightInfo& VisibleLightInfo, 
	FLightSceneInfo& LightSceneInfo)
{
	SCOPE_CYCLE_COUNTER(STAT_AddViewDependentWholeSceneShadowsForView);

	// Allow each view to create a whole scene view dependent shadow
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		const float LightShadowAmount = LightSceneInfo.Proxy->GetShadowAmount();
		TArray<float, TInlineAllocator<2> > FadeAlphas;
		FadeAlphas.Init(0.0f, Views.Num());
		FadeAlphas[ViewIndex] = LightShadowAmount;

		if (IStereoRendering::IsAPrimaryView(View))
		{
			for (int FadeAlphaIndex = ViewIndex + 1; FadeAlphaIndex < Views.Num(); FadeAlphaIndex++)
			{
				if (Views.IsValidIndex(FadeAlphaIndex)
					&& IStereoRendering::IsASecondaryView(Views[FadeAlphaIndex]))
				{
					FadeAlphas[FadeAlphaIndex] = LightShadowAmount;
				}
				else if (IStereoRendering::IsAPrimaryView(Views[FadeAlphaIndex]))
				{
					break;
				}
			}
		}		
		
		// If rendering in stereo mode we render shadow depths only for the left eye, but project for both eyes!
		if (IStereoRendering::IsAPrimaryView(View))
		{
			const bool bExtraDistanceFieldCascade = LightSceneInfo.ShouldRenderLightViewIndependent()
				&& LightSceneInfo.Proxy->ShouldCreateRayTracedCascade(View.GetFeatureLevel(), LightSceneInfo.IsPrecomputedLightingValid(), View.MaxShadowCascades);

			const int32 ProjectionCount = LightSceneInfo.Proxy->GetNumViewDependentWholeSceneShadows(View, LightSceneInfo.IsPrecomputedLightingValid()) + (bExtraDistanceFieldCascade?1:0);

			checkSlow(INDEX_NONE == -1);

			FSceneRenderTargets& SceneContext_ConstantsOnly = FSceneRenderTargets::Get_FrameConstantsOnly();


			// todo: this code can be simplified by computing all the distances in one place - avoiding some redundant work and complexity
			for (int32 Index = 0; Index < ProjectionCount; Index++)
			{
				FWholeSceneProjectedShadowInitializer ProjectedShadowInitializer;

				int32 LocalIndex = Index;

				// Indexing like this puts the ray traced shadow cascade last (might not be needed)
				if(bExtraDistanceFieldCascade && LocalIndex + 1 == ProjectionCount)
				{
					LocalIndex = INDEX_NONE;
				}

				if (LightSceneInfo.Proxy->GetViewDependentWholeSceneProjectedShadowInitializer(View, LocalIndex, LightSceneInfo.IsPrecomputedLightingValid(), ProjectedShadowInitializer))
				{
					const FIntPoint ShadowBufferResolution(
					FMath::Clamp(GetCachedScalabilityCVars().MaxCSMShadowResolution, 1, (int32)GMaxShadowDepthBufferSizeX),
					FMath::Clamp(GetCachedScalabilityCVars().MaxCSMShadowResolution, 1, (int32)GMaxShadowDepthBufferSizeY));

					// Create the projected shadow info.
					FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(), 1, 16) FProjectedShadowInfo;

					uint32 ShadowBorder = NeedsUnatlasedCSMDepthsWorkaround(FeatureLevel) ? 0 : SHADOW_BORDER;

					ProjectedShadowInfo->SetupWholeSceneProjection(
						&LightSceneInfo,
						&View,
						ProjectedShadowInitializer,
						ShadowBufferResolution.X - ShadowBorder * 2,
						ShadowBufferResolution.Y - ShadowBorder * 2,
						ShadowBorder,
						false	// no RSM
						);

					ProjectedShadowInfo->FadeAlphas = FadeAlphas;

					FVisibleLightInfo& LightViewInfo = VisibleLightInfos[LightSceneInfo.Id];
					VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);
					VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
					ShadowInfos.Add(ProjectedShadowInfo);

					// Ray traced shadows use the GPU managed distance field object buffers, no CPU culling needed
					if (!ProjectedShadowInfo->bRayTracedDistanceField)
					{
						ShadowInfosThatNeedCulling.Add(ProjectedShadowInfo);
					}
				}
			}

			FSceneViewState* ViewState = (FSceneViewState*)View.State;
			if (ViewState)
			{
				FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());
				
				float LPVIntensity = 0.f;

				if (LightPropagationVolume && LightPropagationVolume->bEnabled)
				{
					const FLightPropagationVolumeSettings& LPVSettings = View.FinalPostProcessSettings.BlendableManager.GetSingleFinalDataConst<FLightPropagationVolumeSettings>();
					LPVIntensity = LPVSettings.LPVIntensity;
				}

				if (LPVIntensity > 0)
				{
					// Generate the RSM shadow info
					FWholeSceneProjectedShadowInitializer ProjectedShadowInitializer;
					FLightPropagationVolume& Lpv = *LightPropagationVolume;

					if (LightSceneInfo.Proxy->GetViewDependentRsmWholeSceneProjectedShadowInitializer(View, Lpv.GetBoundingBox(), ProjectedShadowInitializer))
					{
						// moved out from the FProjectedShadowInfo constructor
						ProjectedShadowInitializer.CascadeSettings.ShadowSplitIndex = 0;

						const int32 ShadowBufferResolution = SceneContext_ConstantsOnly.GetReflectiveShadowMapResolution();

						// Create the projected shadow info.
						FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(), 1, 16) FProjectedShadowInfo;

						ProjectedShadowInfo->SetupWholeSceneProjection(
							&LightSceneInfo,
							&View,
							ProjectedShadowInitializer,
							ShadowBufferResolution,
							ShadowBufferResolution,
							0,
							true);		// RSM

						FVisibleLightInfo& LightViewInfo = VisibleLightInfos[LightSceneInfo.Id];
						VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);
						VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
						ShadowInfos.Add(ProjectedShadowInfo); // or separate list?

						// Ray traced shadows use the GPU managed distance field object buffers, no CPU culling needed
						if (!ProjectedShadowInfo->bRayTracedDistanceField)
						{
							ShadowInfosThatNeedCulling.Add(ProjectedShadowInfo);
						}
					}
				}
			}
		}
	}
}

void FSceneRenderer::AllocateShadowDepthTargets(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Sort visible shadows based on their allocation needs
	// 2d shadowmaps for this frame only that can be atlased across lights
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> Shadows;
	// 2d shadowmaps that will persist across frames, can't be atlased
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> CachedSpotlightShadows;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> TranslucentShadows;
	// 2d shadowmaps that persist across frames
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> CachedPreShadows;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> RSMShadows;
	// Cubemaps, can't be atlased
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> WholeScenePointShadows;

	const bool bMobile = FeatureLevel < ERHIFeatureLevel::SM5;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> MobileWholeSceneDirectionalShadows;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> MobileDynamicSpotlightShadows;

	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

		// All cascades for a light need to be in the same texture
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator> WholeSceneDirectionalShadows;

		for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.AllProjectedShadows[ShadowIndex];

			// Check that the shadow is visible in at least one view before rendering it.
			bool bShadowIsVisible = false;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];

				if (ProjectedShadowInfo->DependentView && ProjectedShadowInfo->DependentView != &View)
				{
					continue;
				}

				const FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightSceneInfo->Id];
				const FPrimitiveViewRelevance ViewRelevance = VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowIndex];
				const bool bHasViewRelevance = (ProjectedShadowInfo->bTranslucentShadow && ViewRelevance.HasTranslucency()) 
					|| (!ProjectedShadowInfo->bTranslucentShadow && ViewRelevance.bOpaque);

				bShadowIsVisible |= bHasViewRelevance && VisibleLightViewInfo.ProjectedShadowVisibilityMap[ShadowIndex];
			}

			if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly && !ProjectedShadowInfo->HasSubjectPrims())
			{
				FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);

				// A shadowmap for movable primitives when there are no movable primitives would normally read directly from the cached shadowmap
				// However if the cached shadowmap also had no primitives then we need to skip rendering the shadow entirely
				if (!CachedShadowMapData.bCachedShadowMapHasPrimitives)
				{
					bShadowIsVisible = false;
				}
			}

			if (IsForwardShadingEnabled(ShaderPlatform)
				&& ProjectedShadowInfo->GetLightSceneInfo().GetDynamicShadowMapChannel() == -1)
			{
				// With forward shading, dynamic shadows are projected into channels of the light attenuation texture based on their assigned DynamicShadowMapChannel
				bShadowIsVisible = false;
			}

			if (bShadowIsVisible)
			{
				// Visible shadow stats
				if (ProjectedShadowInfo->bReflectiveShadowmap)
				{
					INC_DWORD_STAT(STAT_ReflectiveShadowMaps);
				}
				else if (ProjectedShadowInfo->bWholeSceneShadow)
				{
					INC_DWORD_STAT(STAT_WholeSceneShadows);

					if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly)
					{
						INC_DWORD_STAT(STAT_CachedWholeSceneShadows);
					}
				}
				else if (ProjectedShadowInfo->bPreShadow)
				{
					INC_DWORD_STAT(STAT_PreShadows);
				}
				else
				{
					INC_DWORD_STAT(STAT_PerObjectShadows);
				}

				bool bNeedsProjection = ProjectedShadowInfo->CacheMode != SDCM_StaticPrimitivesOnly
					// Mobile rendering only projects opaque per object shadows.
					&& (FeatureLevel >= ERHIFeatureLevel::SM5 || ProjectedShadowInfo->bPerObjectOpaqueShadow);

				if (bNeedsProjection)
				{
					if (ProjectedShadowInfo->bReflectiveShadowmap)
					{
						VisibleLightInfo.RSMsToProject.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->bCapsuleShadow)
					{
						VisibleLightInfo.CapsuleShadowsToProject.Add(ProjectedShadowInfo);
					}
					else
					{
						VisibleLightInfo.ShadowsToProject.Add(ProjectedShadowInfo);
					}
				}

				const bool bNeedsShadowmapSetup = !ProjectedShadowInfo->bCapsuleShadow && !ProjectedShadowInfo->bRayTracedDistanceField;

				if (bNeedsShadowmapSetup)
				{
					if (ProjectedShadowInfo->bReflectiveShadowmap)
					{
						check(ProjectedShadowInfo->bWholeSceneShadow);
						RSMShadows.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->bPreShadow && ProjectedShadowInfo->bAllocatedInPreshadowCache)
					{
						CachedPreShadows.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->bDirectionalLight && ProjectedShadowInfo->bWholeSceneShadow)
					{
						WholeSceneDirectionalShadows.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->bOnePassPointLightShadow)
					{
						WholeScenePointShadows.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->bTranslucentShadow)
					{
						TranslucentShadows.Add(ProjectedShadowInfo);
					}
					else if (ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly)
					{
						check(ProjectedShadowInfo->bWholeSceneShadow);
						CachedSpotlightShadows.Add(ProjectedShadowInfo);
					}
					else if (bMobile && ProjectedShadowInfo->bWholeSceneShadow)
					{
						MobileDynamicSpotlightShadows.Add(ProjectedShadowInfo);
					}
					else
					{
						Shadows.Add(ProjectedShadowInfo);
					}
				}
			}
		}

		// Sort cascades, this is needed for blending between cascades to work
		VisibleLightInfo.ShadowsToProject.Sort(FCompareFProjectedShadowInfoBySplitIndex());
		VisibleLightInfo.RSMsToProject.Sort(FCompareFProjectedShadowInfoBySplitIndex());

		if (!bMobile)
		{
			AllocateCSMDepthTargets(RHICmdList, WholeSceneDirectionalShadows);
		}
		else
		{
			//Only one directional light could cast csm on mobile, so we could delay allocation for it and see if we could combine any spotlight shadow with it.
			if (WholeSceneDirectionalShadows.Num() > 0)
			{
				MobileWholeSceneDirectionalShadows.Append(WholeSceneDirectionalShadows);
			}
		}
	}

	if (bMobile)
	{
		// AllocateMobileCSMAndSpotLightShadowDepthTargets would only allocate a single large render target for all shadows, so if the requirement exceeds the MaxTextureSize, the rest of the shadows will not get space for rendering
		// So we sort spotlight shadows and append them at the last to make sure csm will get space in any case.
		MobileDynamicSpotlightShadows.Sort(FCompareFProjectedShadowInfoByResolution());

		//Limit the number of spotlights shadow for performance reason
		static const auto MobileMaxVisibleMovableSpotLightsShadowCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.MaxVisibleMovableSpotLightsShadow"));
		if (MobileMaxVisibleMovableSpotLightsShadowCVar)
		{
			int32 MobileMaxVisibleMovableSpotLightsShadow = MobileMaxVisibleMovableSpotLightsShadowCVar->GetValueOnRenderThread();
			MobileDynamicSpotlightShadows.RemoveAt(MobileMaxVisibleMovableSpotLightsShadow, FMath::Max(MobileDynamicSpotlightShadows.Num() - MobileMaxVisibleMovableSpotLightsShadow, 0), false);
		}

		MobileWholeSceneDirectionalShadows.Append(MobileDynamicSpotlightShadows);
		MobileDynamicSpotlightShadows.Empty();
	}

	if (CachedPreShadows.Num() > 0)
	{
		if (!Scene->PreShadowCacheDepthZ)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneContext.GetPreShadowCacheTextureResolution(), PF_ShadowDepth, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Scene->PreShadowCacheDepthZ, TEXT("PreShadowCacheDepthZ"), ERenderTargetTransience::NonTransient);
		}

		SortedShadowsForShadowDepthPass.PreshadowCache.RenderTargets.DepthTarget = Scene->PreShadowCacheDepthZ;

		for (int32 ShadowIndex = 0; ShadowIndex < CachedPreShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = CachedPreShadows[ShadowIndex];
			ProjectedShadowInfo->RenderTargets.DepthTarget = Scene->PreShadowCacheDepthZ.GetReference();

			// Note: adding preshadows whose depths are cached so that GatherDynamicMeshElements
			// will still happen, which is necessary for preshadow receiver stenciling
			ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
			SortedShadowsForShadowDepthPass.PreshadowCache.Shadows.Add(ProjectedShadowInfo);
		}
	}

	AllocateOnePassPointLightDepthTargets(RHICmdList, WholeScenePointShadows);
	AllocateRSMDepthTargets(RHICmdList, RSMShadows);
	AllocateCachedSpotlightShadowDepthTargets(RHICmdList, CachedSpotlightShadows);
	AllocatePerObjectShadowDepthTargets(RHICmdList, Shadows);
	AllocateTranslucentShadowDepthTargets(RHICmdList, TranslucentShadows);
	AllocateMobileCSMAndSpotLightShadowDepthTargets(RHICmdList, MobileWholeSceneDirectionalShadows);

	// Update translucent shadow map uniform buffers.
	for (int32 TranslucentShadowIndex = 0; TranslucentShadowIndex < TranslucentShadows.Num(); ++TranslucentShadowIndex)
	{
		FProjectedShadowInfo* ShadowInfo = TranslucentShadows[TranslucentShadowIndex];
		const int32 PrimitiveIndex = ShadowInfo->GetParentSceneInfo()->GetIndex();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& View = Views[ViewIndex];
			FUniformBufferRHIRef* UniformBufferPtr = View.TranslucentSelfShadowUniformBufferMap.Find(PrimitiveIndex);

			if (UniformBufferPtr)
			{
				FTranslucentSelfShadowUniformParameters Parameters;
				SetupTranslucentSelfShadowUniformParameters(ShadowInfo, Parameters);
				RHIUpdateUniformBuffer(*UniformBufferPtr, &Parameters);
			}
		}
	}

	// Remove cache entries that haven't been used in a while
	for (TMap<int32, FCachedShadowMapData>::TIterator CachedShadowMapIt(Scene->CachedShadowMaps); CachedShadowMapIt; ++CachedShadowMapIt)
	{
		FCachedShadowMapData& ShadowMapData = CachedShadowMapIt.Value();

		if (ShadowMapData.ShadowMap.IsValid() && ViewFamily.CurrentRealTime - ShadowMapData.LastUsedTime > 2.0f)
		{
			ShadowMapData.ShadowMap.Release();
		}
	}

	SET_MEMORY_STAT(STAT_CachedShadowmapMemory, Scene->GetCachedWholeSceneShadowMapsSize());
	SET_MEMORY_STAT(STAT_ShadowmapAtlasMemory, SortedShadowsForShadowDepthPass.ComputeMemorySize());
}

void FSceneRenderer::AllocatePerObjectShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& Shadows)
{
	if (Shadows.Num() > 0)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		const FIntPoint ShadowBufferResolution = SceneContext.GetShadowDepthTextureResolution();

		int32 OriginalNumAtlases = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Num();

		FTextureLayout CurrentShadowLayout(1, 1, ShadowBufferResolution.X, ShadowBufferResolution.Y, false, ETextureLayoutAspectRatio::None, false);
		FPooledRenderTargetDesc ShadowMapDesc2D = FPooledRenderTargetDesc::Create2DDesc(ShadowBufferResolution, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false);
		ShadowMapDesc2D.Flags |= GFastVRamConfig.ShadowPerObject;

		// Sort the projected shadows by resolution.
		Shadows.Sort(FCompareFProjectedShadowInfoByResolution());

		for (int32 ShadowIndex = 0; ShadowIndex < Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = Shadows[ShadowIndex];

			// Atlased shadows need a border
			check(ProjectedShadowInfo->BorderSize != 0);
			check(!ProjectedShadowInfo->bAllocated);

			if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly && !ProjectedShadowInfo->HasSubjectPrims())
			{
				FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
				ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
				ProjectedShadowInfo->bAllocated = true;
				// Skip the shadow depth pass since there are no movable primitives to composite, project from the cached shadowmap directly which contains static primitive depths
				ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(CachedShadowMapData.ShadowMap);
			}
			else
			{
				if (SortedShadowsForShadowDepthPass.ShadowMapAtlases.Num() == OriginalNumAtlases)
				{
					// Start with an empty atlas for per-object shadows (don't allow packing object shadows into the CSM atlas atm)
					SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();
				}

				if (CurrentShadowLayout.AddElement(
					ProjectedShadowInfo->X,
					ProjectedShadowInfo->Y,
					ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
					ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
					)
				{
					ProjectedShadowInfo->bAllocated = true;
				}
				else
				{
					CurrentShadowLayout = FTextureLayout(1, 1, ShadowBufferResolution.X, ShadowBufferResolution.Y, false, ETextureLayoutAspectRatio::None, false);
					SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();

					if (CurrentShadowLayout.AddElement(
						ProjectedShadowInfo->X,
						ProjectedShadowInfo->Y,
						ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
						ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
						)
					{
						ProjectedShadowInfo->bAllocated = true;
					}
				}

				check(ProjectedShadowInfo->bAllocated);

				FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last();

				if (!ShadowMapAtlas.RenderTargets.IsValid() || GFastVRamConfig.bDirty)
				{
					GRenderTargetPool.FindFreeElement(RHICmdList, ShadowMapDesc2D, ShadowMapAtlas.RenderTargets.DepthTarget, TEXT("ShadowDepthAtlas"), ERenderTargetTransience::NonTransient);
				}

				ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapAtlas.RenderTargets);
				ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
				ShadowMapAtlas.Shadows.Add(ProjectedShadowInfo);
			}
		}
	}
}

void FSceneRenderer::AllocateCachedSpotlightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& CachedSpotlightShadows)
{
	for (int32 ShadowIndex = 0; ShadowIndex < CachedSpotlightShadows.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = CachedSpotlightShadows[ShadowIndex];
		SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();
		FSortedShadowMapAtlas& ShadowMap = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last();

		FIntPoint ShadowResolution(ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2, ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2);
		FPooledRenderTargetDesc ShadowMapDesc2D = FPooledRenderTargetDesc::Create2DDesc(ShadowResolution, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false, 1, false);
		GRenderTargetPool.FindFreeElement(RHICmdList, ShadowMapDesc2D, ShadowMap.RenderTargets.DepthTarget, TEXT("CachedShadowDepthMap"), ERenderTargetTransience::NonTransient);

		check(ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly);
		FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
		CachedShadowMapData.ShadowMap = ShadowMap.RenderTargets;

		ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
		ProjectedShadowInfo->bAllocated = true;
		ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMap.RenderTargets);

		ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
		ShadowMap.Shadows.Add(ProjectedShadowInfo);
	}
}

/**
* Helper function to get the name of a CSM rendertarget, keeping the pointers around (this is required by the rendertarget pool)
* @param ShadowMapIndex - the index of the shadow map cascade
*/
const TCHAR* GetCSMRenderTargetName(int32 ShadowMapIndex)
{
	// Render target names require string pointers not to be released, so we cache them in a static array and grow as necessary
	static TArray<FString*> ShadowmapNames;
	while (ShadowmapNames.Num() < ShadowMapIndex + 1)
	{
		if (ShadowMapIndex == 0)
		{
			ShadowmapNames.Add(new FString(TEXT("WholeSceneShadowmap")));
		}
		else
		{
			ShadowmapNames.Add(new FString(FString::Printf(TEXT("WholeSceneShadowmap%d"), ShadowmapNames.Num())));
		}
	}
	return **ShadowmapNames[ShadowMapIndex];
}

struct FLayoutAndAssignedShadows
{
	FLayoutAndAssignedShadows(int32 MaxTextureSize) :
		TextureLayout(1, 1, MaxTextureSize, MaxTextureSize, false, ETextureLayoutAspectRatio::None, false)
	{}

	FTextureLayout TextureLayout;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> Shadows;
};

void FSceneRenderer::AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeSceneDirectionalShadows)
{
	if (WholeSceneDirectionalShadows.Num() > 0)
	{
		const bool bAllowAtlasing = !NeedsUnatlasedCSMDepthsWorkaround(FeatureLevel);

		const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);
		TArray<FLayoutAndAssignedShadows, SceneRenderingAllocator> Layouts;
		Layouts.Add(FLayoutAndAssignedShadows(MaxTextureSize));

		for (int32 ShadowIndex = 0; ShadowIndex < WholeSceneDirectionalShadows.Num(); ShadowIndex++)
		{
			if (!bAllowAtlasing && ShadowIndex > 0)
			{
				Layouts.Add(FLayoutAndAssignedShadows(MaxTextureSize));
			}

			FProjectedShadowInfo* ProjectedShadowInfo = WholeSceneDirectionalShadows[ShadowIndex];

			// Atlased shadows need a border
			check(!bAllowAtlasing || ProjectedShadowInfo->BorderSize != 0);
			check(!ProjectedShadowInfo->bAllocated);

			if (Layouts.Last().TextureLayout.AddElement(
				ProjectedShadowInfo->X,
				ProjectedShadowInfo->Y,
				ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
				ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
				)
			{
				ProjectedShadowInfo->bAllocated = true;
				Layouts.Last().Shadows.Add(ProjectedShadowInfo);
			}
		}

		for (int32 LayoutIndex = 0; LayoutIndex < Layouts.Num(); LayoutIndex++)
		{
			const FLayoutAndAssignedShadows& CurrentLayout = Layouts[LayoutIndex];

			SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();
			FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last();

			FIntPoint WholeSceneAtlasSize(CurrentLayout.TextureLayout.GetSizeX(), CurrentLayout.TextureLayout.GetSizeY());
			FPooledRenderTargetDesc WholeSceneShadowMapDesc2D(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			WholeSceneShadowMapDesc2D.Flags |= GFastVRamConfig.ShadowCSM;
			GRenderTargetPool.FindFreeElement(RHICmdList, WholeSceneShadowMapDesc2D, ShadowMapAtlas.RenderTargets.DepthTarget, GetCSMRenderTargetName(LayoutIndex));

			for (int32 ShadowIndex = 0; ShadowIndex < CurrentLayout.Shadows.Num(); ShadowIndex++)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = CurrentLayout.Shadows[ShadowIndex];

				if (ProjectedShadowInfo->bAllocated)
				{
					ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapAtlas.RenderTargets);
					ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
					ShadowMapAtlas.Shadows.Add(ProjectedShadowInfo);
				}
			}
		}
	}
}

void FSceneRenderer::AllocateRSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& RSMShadows)
{
	if (RSMShadows.Num() > 0
		&& FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);
		FTextureLayout ShadowLayout(1, 1, MaxTextureSize, MaxTextureSize, false, ETextureLayoutAspectRatio::None, false);

		for (int32 ShadowIndex = 0; ShadowIndex < RSMShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = RSMShadows[ShadowIndex];

			check(ProjectedShadowInfo->BorderSize == 0);
			check(!ProjectedShadowInfo->bAllocated);

			if (ShadowLayout.AddElement(
				ProjectedShadowInfo->X,
				ProjectedShadowInfo->Y,
				ProjectedShadowInfo->ResolutionX,
				ProjectedShadowInfo->ResolutionY)
				)
			{
				ProjectedShadowInfo->bAllocated = true;
			}
		}

		SortedShadowsForShadowDepthPass.RSMAtlases.AddDefaulted();
		FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.RSMAtlases.Last();
		ShadowMapAtlas.RenderTargets.ColorTargets.Empty(2);
		ShadowMapAtlas.RenderTargets.ColorTargets.AddDefaulted(2);

		FIntPoint WholeSceneAtlasSize(ShadowLayout.GetSizeX(), ShadowLayout.GetSizeY());

		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_R8G8B8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ShadowMapAtlas.RenderTargets.ColorTargets[0], TEXT("RSMNormal"), ERenderTargetTransience::NonTransient );
		}

		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_FloatR11G11B10, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ShadowMapAtlas.RenderTargets.ColorTargets[1], TEXT("RSMDiffuse"), ERenderTargetTransience::NonTransient);
		}

		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ShadowMapAtlas.RenderTargets.DepthTarget, TEXT("RSMDepth"), ERenderTargetTransience::NonTransient);
		}

		for (int32 ShadowIndex = 0; ShadowIndex < RSMShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = RSMShadows[ShadowIndex];

			if (ProjectedShadowInfo->bAllocated)
			{
				ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapAtlas.RenderTargets);
				ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
				ShadowMapAtlas.Shadows.Add(ProjectedShadowInfo);
			}
		}
	}
}

void FSceneRenderer::AllocateOnePassPointLightDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeScenePointShadows)
{
	if (FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		for (int32 ShadowIndex = 0; ShadowIndex < WholeScenePointShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = WholeScenePointShadows[ShadowIndex];
			check(ProjectedShadowInfo->BorderSize == 0);

			if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly && !ProjectedShadowInfo->HasSubjectPrims())
			{
				FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
				ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
				ProjectedShadowInfo->bAllocated = true;
				// Skip the shadow depth pass since there are no movable primitives to composite, project from the cached shadowmap directly which contains static primitive depths
				check(CachedShadowMapData.ShadowMap.IsValid());
				ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(CachedShadowMapData.ShadowMap);
			}
			else
			{
				SortedShadowsForShadowDepthPass.ShadowMapCubemaps.AddDefaulted();
				FSortedShadowMapAtlas& ShadowMapCubemap = SortedShadowsForShadowDepthPass.ShadowMapCubemaps.Last();

				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateCubemapDesc(ProjectedShadowInfo->ResolutionX, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false, 1, 1, false));
				Desc.Flags |= GFastVRamConfig.ShadowPointLight;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ShadowMapCubemap.RenderTargets.DepthTarget, TEXT("CubeShadowDepthZ"), ERenderTargetTransience::NonTransient );

				if (ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly)
				{
					FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
					CachedShadowMapData.ShadowMap = ShadowMapCubemap.RenderTargets;
				}

				ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
				ProjectedShadowInfo->bAllocated = true;
				ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapCubemap.RenderTargets);

				ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
				ShadowMapCubemap.Shadows.Add(ProjectedShadowInfo);
			}
		}
	}
}

// for easier use of "VisualizeTexture"
TCHAR* const GetTranslucencyShadowTransmissionName(uint32 Id)
{
	// (TCHAR*) for non VisualStudio
	switch(Id)
	{
		case 0: return (TCHAR*)TEXT("TranslucencyShadowTransmission0");
		case 1: return (TCHAR*)TEXT("TranslucencyShadowTransmission1");

		default:
			check(0);
	}
	return (TCHAR*)TEXT("InvalidName");
}

void FSceneRenderer::AllocateTranslucentShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& TranslucentShadows)
{
	if (TranslucentShadows.Num() > 0 && FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		const FIntPoint TranslucentShadowBufferResolution = SceneContext.GetTranslucentShadowDepthTextureResolution();

		// Start with an empty atlas for per-object shadows (don't allow packing object shadows into the CSM atlas atm)
		SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases.AddDefaulted();

		FTextureLayout CurrentShadowLayout(1, 1, TranslucentShadowBufferResolution.X, TranslucentShadowBufferResolution.Y, false, ETextureLayoutAspectRatio::None, false);

		// Sort the projected shadows by resolution.
		TranslucentShadows.Sort(FCompareFProjectedShadowInfoByResolution());

		for (int32 ShadowIndex = 0; ShadowIndex < TranslucentShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = TranslucentShadows[ShadowIndex];

			check(ProjectedShadowInfo->BorderSize != 0);
			check(!ProjectedShadowInfo->bAllocated);

			if (CurrentShadowLayout.AddElement(
				ProjectedShadowInfo->X,
				ProjectedShadowInfo->Y,
				ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
				ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
				)
			{
				ProjectedShadowInfo->bAllocated = true;
			}
			else
			{
				CurrentShadowLayout = FTextureLayout(1, 1, TranslucentShadowBufferResolution.X, TranslucentShadowBufferResolution.Y, false, ETextureLayoutAspectRatio::None, false);
				SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases.AddDefaulted();

				if (CurrentShadowLayout.AddElement(
					ProjectedShadowInfo->X,
					ProjectedShadowInfo->Y,
					ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
					ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
					)
				{
					ProjectedShadowInfo->bAllocated = true;
				}
			}

			check(ProjectedShadowInfo->bAllocated);

			FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases.Last();

			if (ShadowMapAtlas.RenderTargets.ColorTargets.Num() == 0)
			{
				ShadowMapAtlas.RenderTargets.ColorTargets.Empty(NumTranslucencyShadowSurfaces);
				ShadowMapAtlas.RenderTargets.ColorTargets.AddDefaulted(NumTranslucencyShadowSurfaces);

				for (int32 SurfaceIndex = 0; SurfaceIndex < NumTranslucencyShadowSurfaces; SurfaceIndex++)
				{
					// Using PF_FloatRGBA because Fourier coefficients used by Fourier opacity maps have a large range and can be negative
					FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(TranslucentShadowBufferResolution, PF_FloatRGBA, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
					GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ShadowMapAtlas.RenderTargets.ColorTargets[SurfaceIndex], GetTranslucencyShadowTransmissionName(SurfaceIndex), ERenderTargetTransience::NonTransient);
				}
			}

			ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapAtlas.RenderTargets);
			ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
			ShadowMapAtlas.Shadows.Add(ProjectedShadowInfo);
		}
	}
}

void FSceneRenderer::InitDynamicShadows(FRHICommandListImmediate& RHICmdList, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	SCOPE_CYCLE_COUNTER(STAT_DynamicShadowSetupTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(InitViews_Shadows);
	SCOPED_NAMED_EVENT(FSceneRenderer_InitDynamicShadows, FColor::Magenta);

	const bool bMobile = FeatureLevel < ERHIFeatureLevel::SM5;

	bool bStaticSceneOnly = false;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		bStaticSceneOnly = bStaticSceneOnly || View.bStaticSceneOnly;
	}

	const bool bProjectEnablePointLightShadows = Scene->ReadOnlyCVARCache.bEnablePointLightShadows && !bMobile; // Point light shadow is unsupported on mobile for now.
	const bool bProjectEnableMovableDirectionLightShadows = !bMobile || Scene->ReadOnlyCVARCache.bMobileAllowMovableDirectionalLights;
	const bool bProjectEnableMovableSpotLightShadows = !bMobile || Scene->ReadOnlyCVARCache.bMobileEnableMovableSpotlightsShadow;

	uint32 NumPointShadowCachesUpdatedThisFrame = 0;
	uint32 NumSpotShadowCachesUpdatedThisFrame = 0;

	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> PreShadows;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> ViewDependentWholeSceneShadows;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> ViewDependentWholeSceneShadowsThatNeedCulling;
	{
		SCOPE_CYCLE_COUNTER(STAT_InitDynamicShadowsTime);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ShadowInitDynamic);

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			FScopeCycleCounter Context(LightSceneInfo->Proxy->GetStatId());

			FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

			const FLightOcclusionType OcclusionType = GetLightOcclusionType(LightSceneInfoCompact);
			if (OcclusionType != FLightOcclusionType::Shadowmap)
				continue;

			// Only consider lights that may have shadows.
			if ((LightSceneInfoCompact.bCastStaticShadow || LightSceneInfoCompact.bCastDynamicShadow) && GetShadowQuality() > 0)
			{
				// see if the light is visible in any view
				bool bIsVisibleInAnyView = false;

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					// View frustums are only checked when lights have visible primitives or have modulated shadows,
					// so we don't need to check for that again here
					bIsVisibleInAnyView = LightSceneInfo->ShouldRenderLight(Views[ViewIndex]);

					if (bIsVisibleInAnyView) 
					{
						break;
					}
				}

				if (bIsVisibleInAnyView)
				{
					static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
					const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);
					const bool bPointLightShadow = LightSceneInfoCompact.LightType == LightType_Point || LightSceneInfoCompact.LightType == LightType_Rect;
					const bool bDirectionalLightShadow = LightSceneInfoCompact.LightType == LightType_Directional;
					const bool bSpotLightShadow = LightSceneInfoCompact.LightType == LightType_Spot;

					// Only create whole scene shadows for lights that don't precompute shadowing (movable lights)
					const bool bShouldCreateShadowForMovableLight = 
						LightSceneInfoCompact.bCastDynamicShadow
						&& (!LightSceneInfo->Proxy->HasStaticShadowing() || !bAllowStaticLighting);

					const bool bCreateShadowForMovableLight = 
						bShouldCreateShadowForMovableLight
						&& (!bPointLightShadow || bProjectEnablePointLightShadows)
						&& (!bDirectionalLightShadow || bProjectEnableMovableDirectionLightShadows)
						&& (!bSpotLightShadow || bProjectEnableMovableSpotLightShadows);

					// Also create a whole scene shadow for lights with precomputed shadows that are unbuilt
					const bool bShouldCreateShadowToPreviewStaticLight =
						LightSceneInfo->Proxy->HasStaticShadowing()
						&& LightSceneInfoCompact.bCastStaticShadow
						&& !LightSceneInfo->IsPrecomputedLightingValid();						

					const bool bCreateShadowToPreviewStaticLight = 
						bShouldCreateShadowToPreviewStaticLight						
						&& (!bPointLightShadow || bProjectEnablePointLightShadows)
						// Stationary point light and spot light shadow are unsupported on mobile
						&& (!bMobile || bDirectionalLightShadow);

					// Create a whole scene shadow for lights that want static shadowing but didn't get assigned to a valid shadowmap channel due to overlap
					const bool bShouldCreateShadowForOverflowStaticShadowing =
						LightSceneInfo->Proxy->HasStaticShadowing()
						&& !LightSceneInfo->Proxy->HasStaticLighting()
						&& LightSceneInfoCompact.bCastStaticShadow
						&& LightSceneInfo->IsPrecomputedLightingValid()
						&& LightSceneInfo->Proxy->GetShadowMapChannel() == INDEX_NONE;

					const bool bCreateShadowForOverflowStaticShadowing =
						bShouldCreateShadowForOverflowStaticShadowing
						&& (!bPointLightShadow || bProjectEnablePointLightShadows)
						// Stationary point light and spot light shadow are unsupported on mobile
						&& (!bMobile || bDirectionalLightShadow);

					const bool bPointLightWholeSceneShadow = (bShouldCreateShadowForMovableLight || bShouldCreateShadowForOverflowStaticShadowing || bShouldCreateShadowToPreviewStaticLight) && bPointLightShadow;
					if (bPointLightWholeSceneShadow)
					{						
						UsedWholeScenePointLightNames.Add(LightSceneInfoCompact.LightSceneInfo->Proxy->GetComponentName());
					}

					if (bCreateShadowForMovableLight || bCreateShadowToPreviewStaticLight || bCreateShadowForOverflowStaticShadowing)
					{
						// Try to create a whole scene projected shadow.
						CreateWholeSceneProjectedShadow(LightSceneInfo, NumPointShadowCachesUpdatedThisFrame, NumSpotShadowCachesUpdatedThisFrame);
					}

					// Allow movable and stationary lights to create CSM, or static lights that are unbuilt
					if ((!LightSceneInfo->Proxy->HasStaticLighting() && LightSceneInfoCompact.bCastDynamicShadow) || bCreateShadowToPreviewStaticLight)
					{
						static_assert(UE_ARRAY_COUNT(Scene->MobileDirectionalLights) == 3, "All array entries for MobileDirectionalLights must be checked");
						if( !bMobile ||
							((LightSceneInfo->Proxy->UseCSMForDynamicObjects() || LightSceneInfo->Proxy->IsMovable()) 
								// Mobile uses the scene's MobileDirectionalLights only for whole scene shadows.
								&& (LightSceneInfo == Scene->MobileDirectionalLights[0] || LightSceneInfo == Scene->MobileDirectionalLights[1] || LightSceneInfo == Scene->MobileDirectionalLights[2])))
						{
							AddViewDependentWholeSceneShadowsForView(ViewDependentWholeSceneShadows, ViewDependentWholeSceneShadowsThatNeedCulling, VisibleLightInfo, *LightSceneInfo);
						}

						if( !bMobile || (LightSceneInfo->Proxy->CastsModulatedShadows() && !LightSceneInfo->Proxy->UseCSMForDynamicObjects()))
						{
							Scene->FlushAsyncLightPrimitiveInteractionCreation();

							const TArray<FLightPrimitiveInteraction*>* InteractionShadowPrimitives = LightSceneInfo->GetInteractionShadowPrimitives(false);

							if (InteractionShadowPrimitives)
							{
								const int32 NumPrims = InteractionShadowPrimitives->Num();
								for (int32 Idx = 0; Idx < NumPrims; ++Idx)
								{
									SetupInteractionShadows(RHICmdList, (*InteractionShadowPrimitives)[Idx], VisibleLightInfo, bStaticSceneOnly, ViewDependentWholeSceneShadows, PreShadows);
								}
							}
							else
							{
								// Look for individual primitives with a dynamic shadow.
								for (FLightPrimitiveInteraction* Interaction = LightSceneInfo->GetDynamicInteractionOftenMovingPrimitiveList(false);
									Interaction;
									Interaction = Interaction->GetNextPrimitive()
									)
								{
									SetupInteractionShadows(RHICmdList, Interaction, VisibleLightInfo, bStaticSceneOnly, ViewDependentWholeSceneShadows, PreShadows);
								}

								for (FLightPrimitiveInteraction* Interaction = LightSceneInfo->GetDynamicInteractionStaticPrimitiveList(false);
									Interaction;
									Interaction = Interaction->GetNextPrimitive()
									)
								{
									SetupInteractionShadows(RHICmdList, Interaction, VisibleLightInfo, bStaticSceneOnly, ViewDependentWholeSceneShadows, PreShadows);
								}
							}
						}
					}
				}
			}
		}

		CSV_CUSTOM_STAT(LightCount, UpdatedShadowMaps, float(NumPointShadowCachesUpdatedThisFrame + NumSpotShadowCachesUpdatedThisFrame), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT_GLOBAL(ShadowCacheUsageMB, (float(Scene->GetCachedWholeSceneShadowMapsSize()) / 1024) / 1024, ECsvCustomStatOp::Set);

		// Calculate visibility of the projected shadows.
		InitProjectedShadowVisibility(RHICmdList);
	}

	// Clear old preshadows and attempt to add new ones to the cache
	UpdatePreshadowCache(FSceneRenderTargets::Get(RHICmdList));

	// Gathers the list of primitives used to draw various shadow types
	GatherShadowPrimitives(PreShadows, ViewDependentWholeSceneShadowsThatNeedCulling, bStaticSceneOnly);

	AllocateShadowDepthTargets(RHICmdList);

	// Generate mesh element arrays from shadow primitive arrays
	GatherShadowDynamicMeshElements(DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);

}

void FSceneRenderer::AllocateMobileCSMAndSpotLightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& MobileCSMAndSpotLightShadows)
{
	if (MobileCSMAndSpotLightShadows.Num() > 0)
	{
		const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);
		FLayoutAndAssignedShadows MobileCSMAndSpotLightShadowLayout(MaxTextureSize);

		for (int32 ShadowIndex = 0; ShadowIndex < MobileCSMAndSpotLightShadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = MobileCSMAndSpotLightShadows[ShadowIndex];

			// Atlased shadows need a border
			checkSlow(ProjectedShadowInfo->BorderSize != 0);
			checkSlow(!ProjectedShadowInfo->bAllocated);

			if (MobileCSMAndSpotLightShadowLayout.TextureLayout.AddElement(
				ProjectedShadowInfo->X,
				ProjectedShadowInfo->Y,
				ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
				ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2)
				)
			{
				ProjectedShadowInfo->bAllocated = true;
				MobileCSMAndSpotLightShadowLayout.Shadows.Add(ProjectedShadowInfo);
			}
		}

		if (MobileCSMAndSpotLightShadowLayout.TextureLayout.GetSizeX() > 0)
		{
			SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();
			FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last();

			FIntPoint WholeSceneAtlasSize(MobileCSMAndSpotLightShadowLayout.TextureLayout.GetSizeX(), MobileCSMAndSpotLightShadowLayout.TextureLayout.GetSizeY());

			if (CVarMobileShadowmapRoundUpToPowerOfTwo.GetValueOnRenderThread() != 0)
			{
				WholeSceneAtlasSize.X = 1 << FMath::CeilLogTwo(WholeSceneAtlasSize.X);
				WholeSceneAtlasSize.Y = 1 << FMath::CeilLogTwo(WholeSceneAtlasSize.Y);
			}

			bool bResolutionChanged = Scene->MobileWholeSceneShadowAtlasSize != WholeSceneAtlasSize;

			Scene->MobileWholeSceneShadowAtlasSize = WholeSceneAtlasSize;

			FPooledRenderTargetDesc WholeSceneShadowMapDesc2D(FPooledRenderTargetDesc::Create2DDesc(WholeSceneAtlasSize, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			WholeSceneShadowMapDesc2D.Flags |= GFastVRamConfig.ShadowCSM;
			GRenderTargetPool.FindFreeElement(RHICmdList, WholeSceneShadowMapDesc2D, ShadowMapAtlas.RenderTargets.DepthTarget, TEXT("MobileCSMAndSpotLightShadowmap"));

			for (int32 ShadowIndex = 0; ShadowIndex < MobileCSMAndSpotLightShadowLayout.Shadows.Num(); ShadowIndex++)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = MobileCSMAndSpotLightShadowLayout.Shadows[ShadowIndex];

				if (ProjectedShadowInfo->bAllocated)
				{
					ProjectedShadowInfo->RenderTargets.CopyReferencesFromRenderTargets(ShadowMapAtlas.RenderTargets);
					ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
					ShadowMapAtlas.Shadows.Add(ProjectedShadowInfo);

					if (bResolutionChanged)
					{
						FLightSceneInfo* LightSceneInfo = (FLightSceneInfo*)(&ProjectedShadowInfo->GetLightSceneInfo());
						LightSceneInfo->Proxy->SetMobileMovablePointLightUniformBufferNeedsUpdate(true);
					}
				}
			}
		}
	}
}