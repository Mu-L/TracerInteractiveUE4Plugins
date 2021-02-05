// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRender.cpp: New terrain rendering
=============================================================================*/

#include "LandscapeRender.h"
#include "LightMap.h"
#include "ShadowMap.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapePrivate.h"
#include "LandscapeMeshProxyComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ShaderParameterUtils.h"
#include "TessellationRendering.h"
#include "LandscapeEdit.h"
#include "Engine/LevelStreaming.h"
#include "LevelUtils.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Engine/ShadowMapTexture2D.h"
#include "EngineGlobals.h"
#include "EngineModule.h"
#include "UnrealEngine.h"
#include "LandscapeLight.h"
#include "Algo/Find.h"
#include "Engine/StaticMesh.h"
#include "LandscapeInfo.h"
#include "LandscapeDataAccess.h"
#include "DrawDebugHelpers.h"
#include "PrimitiveSceneInfo.h"
#include "SceneView.h"
#include "Runtime/Renderer/Private/SceneCore.h"
#include "LandscapeProxy.h"
#include "HAL/LowLevelMemTracker.h"
#include "MeshMaterialShader.h"
#include "VT/RuntimeVirtualTexture.h"
#include "RayTracingInstance.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeUniformShaderParameters, "LandscapeParameters");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeFixedGridUniformShaderParameters, "LandscapeFixedGrid");
IMPLEMENT_TYPE_LAYOUT(FLandscapeVertexFactoryPixelShaderParameters);

int32 GLandscapeMeshLODBias = 0;
FAutoConsoleVariableRef CVarLandscapeMeshLODBias(
	TEXT("r.LandscapeLODBias"),
	GLandscapeMeshLODBias,
	TEXT("LOD bias for landscape/terrain meshes."),
	ECVF_Scalability
);

#if !UE_BUILD_SHIPPING
static void OnLODDistributionScaleChanged(IConsoleVariable* CVar)
{
	for (auto* LandscapeComponent : TObjectRange<ULandscapeComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
	{
		LandscapeComponent->MarkRenderStateDirty();
	}
}
#endif

float GLandscapeLOD0DistributionScale = 1.f;
FAutoConsoleVariableRef CVarLandscapeLOD0DistributionScale(
	TEXT("r.LandscapeLOD0DistributionScale"),
	GLandscapeLOD0DistributionScale,
	TEXT("Multiplier for the landscape LOD0DistributionSetting property"),
#if !UE_BUILD_SHIPPING
	FConsoleVariableDelegate::CreateStatic(&OnLODDistributionScaleChanged),
#endif
	ECVF_Scalability
);

float GLandscapeLODDistributionScale = 1.f;
FAutoConsoleVariableRef CVarLandscapeLODDistributionScale(
	TEXT("r.LandscapeLODDistributionScale"),
	GLandscapeLODDistributionScale,
	TEXT("Multiplier for the landscape LODDistributionSetting property"),
#if !UE_BUILD_SHIPPING
	FConsoleVariableDelegate::CreateStatic(&OnLODDistributionScaleChanged),
#endif
	ECVF_Scalability
);

float GShadowMapWorldUnitsToTexelFactor = -1.0f;
static FAutoConsoleVariableRef CVarShadowMapWorldUnitsToTexelFactor(
	TEXT("Landscape.ShadowMapWorldUnitsToTexelFactor"),
	GShadowMapWorldUnitsToTexelFactor,
	TEXT("Used to specify tolerance factor for mesh size related to cascade shadow resolution")
);

int32 GAllowLandscapeShadows = 1;
static FAutoConsoleVariableRef CVarAllowLandscapeShadows(
	TEXT("r.AllowLandscapeShadows"),
	GAllowLandscapeShadows,
	TEXT("Allow Landscape Shadows")
);

#if WITH_EDITOR
extern TAutoConsoleVariable<int32> CVarLandscapeShowDirty;
#endif

#if !UE_BUILD_SHIPPING
int32 GVarDumpLandscapeLODsCurrentFrame = 0;
bool GVarDumpLandscapeLODs = false;

static void OnDumpLandscapeLODs(const TArray< FString >& Args)
{
	if (Args.Num() >= 1)
	{
		GVarDumpLandscapeLODs = FCString::Atoi(*Args[0]) == 0 ? false : true;
	}

	// Add some buffer to be able to correctly catch the frame during the rendering
	GVarDumpLandscapeLODsCurrentFrame = GVarDumpLandscapeLODs ? GFrameNumberRenderThread + 3 : INDEX_NONE;
}

static FAutoConsoleCommand CVarDumpLandscapeLODs(
	TEXT("Landscape.DumpLODs"),
	TEXT("Will dump the current status of LOD value and current texture streaming status"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&OnDumpLandscapeLODs)
);
#endif

#if WITH_EDITOR
LANDSCAPE_API int32 GLandscapeViewMode = ELandscapeViewMode::Normal;
FAutoConsoleVariableRef CVarLandscapeDebugViewMode(
	TEXT("Landscape.DebugViewMode"),
	GLandscapeViewMode,
	TEXT("Change the view mode of the landscape rendering. Valid Input: 0 = Normal, 2 = DebugLayer, 3 = LayerDensity, 4 = LayerUsage, 5 = LOD Distribution, 6 = WireframeOnTop, 7 = LayerContribution"),
	ECVF_Cheat
);
#endif

#if RHI_RAYTRACING
static TAutoConsoleVariable<int32> CVarRayTracingLandscape(
	TEXT("r.RayTracing.Geometry.Landscape"),
	1,
	TEXT("Include landscapes in ray tracing effects (default = 1 (landscape enabled in ray tracing))"));

int32 GLandscapeRayTracingGeometryLODsThatUpdateEveryFrame = 0;
static FAutoConsoleVariableRef CVarLandscapeRayTracingGeometryLODsThatUpdateEveryFrame(
	TEXT("r.RayTracing.Geometry.Landscape.LODsUpdateEveryFrame"),
	GLandscapeRayTracingGeometryLODsThatUpdateEveryFrame,
	TEXT("If on, LODs that are lower than the specified level will be updated every frame, which can be used to workaround some artifacts caused by texture streaming if you're using WorldPositionOffset on the landscape")
);

int32 GLandscapeRayTracingGeometryDetectTextureStreaming = 1;
static FAutoConsoleVariableRef CVarLandscapeRayTracingGeometryDetectTextureStreaming(
	TEXT("r.RayTracing.Geometry.Landscape.DetectTextureStreaming"),
	GLandscapeRayTracingGeometryDetectTextureStreaming,
	TEXT("If on, update ray tracing geometry when texture streaming state changes. Useful when WorldPositionOffset is used in the landscape material")
);
#endif

/*------------------------------------------------------------------------------
Forsyth algorithm for cache optimizing index buffers.
------------------------------------------------------------------------------*/

// Forsyth algorithm to optimize post-transformed vertex cache
namespace
{
	// code for computing vertex score was taken, as much as possible
	// directly from the original publication.
	float ComputeVertexCacheScore(int32 CachePosition, uint32 VertexCacheSize)
	{
		const float FindVertexScoreCacheDecayPower = 1.5f;
		const float FindVertexScoreLastTriScore = 0.75f;

		float Score = 0.0f;
		if (CachePosition < 0)
		{
			// Vertex is not in FIFO cache - no score.
		}
		else
		{
			if (CachePosition < 3)
			{
				// This vertex was used in the last triangle,
				// so it has a fixed score, whichever of the three
				// it's in. Otherwise, you can get very different
				// answers depending on whether you add
				// the triangle 1,2,3 or 3,1,2 - which is silly.
				Score = FindVertexScoreLastTriScore;
			}
			else
			{
				check(CachePosition < (int32)VertexCacheSize);
				// Points for being high in the cache.
				const float Scaler = 1.0f / (VertexCacheSize - 3);
				Score = 1.0f - (CachePosition - 3) * Scaler;
				Score = FMath::Pow(Score, FindVertexScoreCacheDecayPower);
			}
		}

		return Score;
	}

	float ComputeVertexValenceScore(uint32 numActiveFaces)
	{
		const float FindVertexScoreValenceBoostScale = 2.0f;
		const float FindVertexScoreValenceBoostPower = 0.5f;

		float Score = 0.f;

		// Bonus points for having a low number of tris still to
		// use the vert, so we get rid of lone verts quickly.
		float ValenceBoost = FMath::Pow(float(numActiveFaces), -FindVertexScoreValenceBoostPower);
		Score += FindVertexScoreValenceBoostScale * ValenceBoost;

		return Score;
	}

	const uint32 MaxVertexCacheSize = 64;
	const uint32 MaxPrecomputedVertexValenceScores = 64;
	float VertexCacheScores[MaxVertexCacheSize + 1][MaxVertexCacheSize];
	float VertexValenceScores[MaxPrecomputedVertexValenceScores];
	bool bVertexScoresComputed = false; //ComputeVertexScores();

	bool ComputeVertexScores()
	{
		for (uint32 CacheSize = 0; CacheSize <= MaxVertexCacheSize; ++CacheSize)
		{
			for (uint32 CachePos = 0; CachePos < CacheSize; ++CachePos)
			{
				VertexCacheScores[CacheSize][CachePos] = ComputeVertexCacheScore(CachePos, CacheSize);
			}
		}

		for (uint32 Valence = 0; Valence < MaxPrecomputedVertexValenceScores; ++Valence)
		{
			VertexValenceScores[Valence] = ComputeVertexValenceScore(Valence);
		}

		return true;
	}

	inline float FindVertexCacheScore(uint32 CachePosition, uint32 MaxSizeVertexCache)
	{
		return VertexCacheScores[MaxSizeVertexCache][CachePosition];
	}

	inline float FindVertexValenceScore(uint32 NumActiveTris)
	{
		return VertexValenceScores[NumActiveTris];
	}

	float FindVertexScore(uint32 NumActiveFaces, uint32 CachePosition, uint32 VertexCacheSize)
	{
		check(bVertexScoresComputed);

		if (NumActiveFaces == 0)
		{
			// No tri needs this vertex!
			return -1.0f;
		}

		float Score = 0.f;
		if (CachePosition < VertexCacheSize)
		{
			Score += VertexCacheScores[VertexCacheSize][CachePosition];
		}

		if (NumActiveFaces < MaxPrecomputedVertexValenceScores)
		{
			Score += VertexValenceScores[NumActiveFaces];
		}
		else
		{
			Score += ComputeVertexValenceScore(NumActiveFaces);
		}

		return Score;
	}

	struct OptimizeVertexData
	{
		float  Score;
		uint32  ActiveFaceListStart;
		uint32  ActiveFaceListSize;
		uint32  CachePos0;
		uint32  CachePos1;
		OptimizeVertexData() : Score(0.f), ActiveFaceListStart(0), ActiveFaceListSize(0), CachePos0(0), CachePos1(0) { }
	};

	//-----------------------------------------------------------------------------
	//  OptimizeFaces
	//-----------------------------------------------------------------------------
	//  Parameters:
	//      InIndexList
	//          input index list
	//      OutIndexList
	//          a pointer to a preallocated buffer the same size as indexList to
	//          hold the optimized index list
	//      LRUCacheSize
	//          the size of the simulated post-transform cache (max:64)
	//-----------------------------------------------------------------------------

	template <typename INDEX_TYPE>
	void OptimizeFaces(const TArray<INDEX_TYPE>& InIndexList, TArray<INDEX_TYPE>& OutIndexList, uint16 LRUCacheSize)
	{
		uint32 VertexCount = 0;
		const uint32 IndexCount = InIndexList.Num();

		// compute face count per vertex
		for (uint32 i = 0; i < IndexCount; ++i)
		{
			uint32 Index = InIndexList[i];
			VertexCount = FMath::Max(Index, VertexCount);
		}
		VertexCount++;

		TArray<OptimizeVertexData> VertexDataList;
		VertexDataList.Empty(VertexCount);
		for (uint32 i = 0; i < VertexCount; i++)
		{
			VertexDataList.Add(OptimizeVertexData());
		}

		OutIndexList.Empty(IndexCount);
		OutIndexList.AddZeroed(IndexCount);

		// compute face count per vertex
		for (uint32 i = 0; i < IndexCount; ++i)
		{
			uint32 Index = InIndexList[i];
			OptimizeVertexData& VertexData = VertexDataList[Index];
			VertexData.ActiveFaceListSize++;
		}

		TArray<uint32> ActiveFaceList;

		const uint32 EvictedCacheIndex = TNumericLimits<uint32>::Max();

		{
			// allocate face list per vertex
			uint32 CurActiveFaceListPos = 0;
			for (uint32 i = 0; i < VertexCount; ++i)
			{
				OptimizeVertexData& VertexData = VertexDataList[i];
				VertexData.CachePos0 = EvictedCacheIndex;
				VertexData.CachePos1 = EvictedCacheIndex;
				VertexData.ActiveFaceListStart = CurActiveFaceListPos;
				CurActiveFaceListPos += VertexData.ActiveFaceListSize;
				VertexData.Score = FindVertexScore(VertexData.ActiveFaceListSize, VertexData.CachePos0, LRUCacheSize);
				VertexData.ActiveFaceListSize = 0;
			}
			ActiveFaceList.Empty(CurActiveFaceListPos);
			ActiveFaceList.AddZeroed(CurActiveFaceListPos);
		}

		// fill out face list per vertex
		for (uint32 i = 0; i < IndexCount; i += 3)
		{
			for (uint32 j = 0; j < 3; ++j)
			{
				uint32 Index = InIndexList[i + j];
				OptimizeVertexData& VertexData = VertexDataList[Index];
				ActiveFaceList[VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize] = i;
				VertexData.ActiveFaceListSize++;
			}
		}

		TArray<uint8> ProcessedFaceList;
		ProcessedFaceList.Empty(IndexCount);
		ProcessedFaceList.AddZeroed(IndexCount);

		uint32 VertexCacheBuffer[(MaxVertexCacheSize + 3) * 2];
		uint32* Cache0 = VertexCacheBuffer;
		uint32* Cache1 = VertexCacheBuffer + (MaxVertexCacheSize + 3);
		uint32 EntriesInCache0 = 0;

		uint32 BestFace = 0;
		float BestScore = -1.f;

		const float MaxValenceScore = FindVertexScore(1, EvictedCacheIndex, LRUCacheSize) * 3.f;

		for (uint32 i = 0; i < IndexCount; i += 3)
		{
			if (BestScore < 0.f)
			{
				// no verts in the cache are used by any unprocessed faces so
				// search all unprocessed faces for a new starting point
				for (uint32 j = 0; j < IndexCount; j += 3)
				{
					if (ProcessedFaceList[j] == 0)
					{
						uint32 Face = j;
						float FaceScore = 0.f;
						for (uint32 k = 0; k < 3; ++k)
						{
							uint32 Index = InIndexList[Face + k];
							OptimizeVertexData& VertexData = VertexDataList[Index];
							check(VertexData.ActiveFaceListSize > 0);
							check(VertexData.CachePos0 >= LRUCacheSize);
							FaceScore += VertexData.Score;
						}

						if (FaceScore > BestScore)
						{
							BestScore = FaceScore;
							BestFace = Face;

							check(BestScore <= MaxValenceScore);
							if (BestScore >= MaxValenceScore)
							{
								break;
							}
						}
					}
				}
				check(BestScore >= 0.f);
			}

			ProcessedFaceList[BestFace] = 1;
			uint32 EntriesInCache1 = 0;

			// add bestFace to LRU cache and to newIndexList
			for (uint32 V = 0; V < 3; ++V)
			{
				INDEX_TYPE Index = InIndexList[BestFace + V];
				OutIndexList[i + V] = Index;

				OptimizeVertexData& VertexData = VertexDataList[Index];

				if (VertexData.CachePos1 >= EntriesInCache1)
				{
					VertexData.CachePos1 = EntriesInCache1;
					Cache1[EntriesInCache1++] = Index;

					if (VertexData.ActiveFaceListSize == 1)
					{
						--VertexData.ActiveFaceListSize;
						continue;
					}
				}

				check(VertexData.ActiveFaceListSize > 0);
				uint32 FindIndex;
				for (FindIndex = VertexData.ActiveFaceListStart; FindIndex < VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize; FindIndex++)
				{
					if (ActiveFaceList[FindIndex] == BestFace)
					{
						break;
					}
				}
				check(FindIndex != VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize);

				if (FindIndex != VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize - 1)
				{
					uint32 SwapTemp = ActiveFaceList[FindIndex];
					ActiveFaceList[FindIndex] = ActiveFaceList[VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize - 1];
					ActiveFaceList[VertexData.ActiveFaceListStart + VertexData.ActiveFaceListSize - 1] = SwapTemp;
				}

				--VertexData.ActiveFaceListSize;
				VertexData.Score = FindVertexScore(VertexData.ActiveFaceListSize, VertexData.CachePos1, LRUCacheSize);

			}

			// move the rest of the old verts in the cache down and compute their new scores
			for (uint32 C0 = 0; C0 < EntriesInCache0; ++C0)
			{
				uint32 Index = Cache0[C0];
				OptimizeVertexData& VertexData = VertexDataList[Index];

				if (VertexData.CachePos1 >= EntriesInCache1)
				{
					VertexData.CachePos1 = EntriesInCache1;
					Cache1[EntriesInCache1++] = Index;
					VertexData.Score = FindVertexScore(VertexData.ActiveFaceListSize, VertexData.CachePos1, LRUCacheSize);
				}
			}

			// find the best scoring triangle in the current cache (including up to 3 that were just evicted)
			BestScore = -1.f;
			for (uint32 C1 = 0; C1 < EntriesInCache1; ++C1)
			{
				uint32 Index = Cache1[C1];
				OptimizeVertexData& VertexData = VertexDataList[Index];
				VertexData.CachePos0 = VertexData.CachePos1;
				VertexData.CachePos1 = EvictedCacheIndex;
				for (uint32 j = 0; j < VertexData.ActiveFaceListSize; ++j)
				{
					uint32 Face = ActiveFaceList[VertexData.ActiveFaceListStart + j];
					float FaceScore = 0.f;
					for (uint32 V = 0; V < 3; V++)
					{
						uint32 FaceIndex = InIndexList[Face + V];
						OptimizeVertexData& FaceVertexData = VertexDataList[FaceIndex];
						FaceScore += FaceVertexData.Score;
					}
					if (FaceScore > BestScore)
					{
						BestScore = FaceScore;
						BestFace = Face;
					}
				}
			}

			uint32* SwapTemp = Cache0;
			Cache0 = Cache1;
			Cache1 = SwapTemp;

			EntriesInCache0 = FMath::Min(EntriesInCache1, (uint32)LRUCacheSize);
		}
	}

} // namespace 

struct FLandscapeDebugOptions
{
	FLandscapeDebugOptions()
		: bShowPatches(false)
		, bDisableStatic(false)
		, CombineMode(eCombineMode_Default)
		, PatchesConsoleCommand(
			TEXT("Landscape.Patches"),
			TEXT("Show/hide Landscape patches"),
			FConsoleCommandDelegate::CreateRaw(this, &FLandscapeDebugOptions::Patches))
		, StaticConsoleCommand(
			TEXT("Landscape.Static"),
			TEXT("Enable/disable Landscape static drawlists"),
			FConsoleCommandDelegate::CreateRaw(this, &FLandscapeDebugOptions::Static))
		, CombineConsoleCommand(
			TEXT("Landscape.Combine"),
			TEXT("Set landscape component combining mode : 0 = Default, 1 = Combine All, 2 = Disabled"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FLandscapeDebugOptions::Combine))
	{
	}

	enum eCombineMode
	{
		eCombineMode_Default = 0,
		eCombineMode_CombineAll = 1,
		eCombineMode_Disabled = 2
	};

	bool bShowPatches;
	bool bDisableStatic;
	eCombineMode CombineMode;

	FORCEINLINE bool IsCombinedDisabled() const { return CombineMode == eCombineMode_Disabled; }
	FORCEINLINE bool IsCombinedAll() const { return CombineMode == eCombineMode_CombineAll; }
	FORCEINLINE bool IsCombinedDefault() const { return CombineMode == eCombineMode_Default; }

private:
	FAutoConsoleCommand PatchesConsoleCommand;
	FAutoConsoleCommand StaticConsoleCommand;
	FAutoConsoleCommand CombineConsoleCommand;

	void Patches()
	{
		bShowPatches = !bShowPatches;
		UE_LOG(LogLandscape, Display, TEXT("Landscape.Patches: %s"), bShowPatches ? TEXT("Show") : TEXT("Hide"));
	}

	void Static()
	{
		bDisableStatic = !bDisableStatic;
		UE_LOG(LogLandscape, Display, TEXT("Landscape.Static: %s"), bDisableStatic ? TEXT("Disabled") : TEXT("Enabled"));
	}

	void Combine(const TArray<FString>& Args)
	{
		if (Args.Num() >= 1)
		{
			CombineMode = (eCombineMode)FCString::Atoi(*Args[0]);
			UE_LOG(LogLandscape, Display, TEXT("Landscape.Combine: %d"), (int32)CombineMode);
		}
	}
};

FLandscapeDebugOptions GLandscapeDebugOptions;


#if WITH_EDITOR
LANDSCAPE_API bool GLandscapeEditModeActive = false;
LANDSCAPE_API int32 GLandscapeEditRenderMode = ELandscapeEditRenderMode::None;
UMaterialInterface* GLayerDebugColorMaterial = nullptr;
UMaterialInterface* GSelectionColorMaterial = nullptr;
UMaterialInterface* GSelectionRegionMaterial = nullptr;
UMaterialInterface* GMaskRegionMaterial = nullptr;
UMaterialInterface* GColorMaskRegionMaterial = nullptr;
UTexture2D* GLandscapeBlackTexture = nullptr;
UMaterialInterface* GLandscapeLayerUsageMaterial = nullptr;
UMaterialInterface* GLandscapeDirtyMaterial = nullptr;
#endif

void ULandscapeComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	// TODO - investigate whether this is correct

	ALandscapeProxy* Actor = GetLandscapeProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance)
	{
		OutMaterials.Append(MaterialInstancesDynamic.FilterByPredicate([](UMaterialInstanceDynamic* MaterialInstance) { return MaterialInstance != nullptr; }));
	}
	else
	{
		OutMaterials.Append(MaterialInstances.FilterByPredicate([](UMaterialInstanceConstant* MaterialInstance) { return MaterialInstance != nullptr; }));
	}

	if (OverrideMaterial)
	{
		OutMaterials.Add(OverrideMaterial);
	}

	if (OverrideHoleMaterial)
	{
		OutMaterials.Add(OverrideHoleMaterial);
	}

	OutMaterials.Append(MobileMaterialInterfaces);

#if WITH_EDITORONLY_DATA
	if (EditToolRenderData.ToolMaterial)
	{
		OutMaterials.Add(EditToolRenderData.ToolMaterial);
	}

	if (EditToolRenderData.GizmoMaterial)
	{
		OutMaterials.Add(EditToolRenderData.GizmoMaterial);
	}
#endif

#if WITH_EDITOR
	//if (bGetDebugMaterials) // TODO: This should be tested and enabled
	{
		OutMaterials.Add(GLayerDebugColorMaterial);
		OutMaterials.Add(GSelectionColorMaterial);
		OutMaterials.Add(GSelectionRegionMaterial);
		OutMaterials.Add(GMaskRegionMaterial);
		OutMaterials.Add(GColorMaskRegionMaterial);
		OutMaterials.Add(GLandscapeLayerUsageMaterial);
		OutMaterials.Add(GLandscapeDirtyMaterial);
	}
#endif
}

/** 
 * Return any global Lod override for landscape. 
 * A return value less than 0 means no override.
 * Any positive value must still be clamped into the valid Lod range for the landscape.
 */
static int32 GetViewLodOverride(FSceneView const& View)
{
	// Apply r.ForceLOD override
	int32 LodOverride = GetCVarForceLOD();
#if WITH_EDITOR
	// Apply editor landscape lod override
	LodOverride = View.Family->LandscapeLODOverride >= 0 ? View.Family->LandscapeLODOverride : LodOverride;
#endif
	// Use lod 0 if lodding is disabled
	LodOverride = View.Family->EngineShowFlags.LOD == 0 ? 0 : LodOverride;
	return LodOverride;
}

static int32 GetDrawCollisionLodOverride(bool bShowCollisionPawn, bool bShowCollisionVisibility, int32 DrawCollisionPawnLOD, int32 DrawCollisionVisibilityLOD)
{
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	return bShowCollisionPawn ? FMath::Max(DrawCollisionPawnLOD, DrawCollisionVisibilityLOD) : bShowCollisionVisibility ? DrawCollisionVisibilityLOD : -1;
#else
	return -1;
#endif
}

static int32 GetDrawCollisionLodOverride(FSceneView const& View, FCollisionResponseContainer const& CollisionResponse, int32 CollisionLod, int32 SimpleCollisionLod)
{
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bool bShowCollisionPawn = View.Family->EngineShowFlags.CollisionPawn;
	bool bShowCollisionVisibility = View.Family->EngineShowFlags.CollisionVisibility;
	int32 DrawCollisionPawnLOD = CollisionResponse.GetResponse(ECC_Pawn) == ECR_Ignore ? -1 : SimpleCollisionLod;
	int32 DrawCollisionVisibilityLOD = CollisionResponse.GetResponse(ECC_Visibility) == ECR_Ignore ? -1 : CollisionLod;
	return GetDrawCollisionLodOverride(bShowCollisionPawn, bShowCollisionVisibility, DrawCollisionPawnLOD, DrawCollisionVisibilityLOD);
#else
	return -1;
#endif
}

//
// FLandscapeComponentSceneProxy
//
TMap<uint32, FLandscapeSharedBuffers*>FLandscapeComponentSceneProxy::SharedBuffersMap;
TMap<FLandscapeNeighborInfo::FLandscapeKey, TMap<FIntPoint, const FLandscapeNeighborInfo*> > FLandscapeNeighborInfo::SharedSceneProxyMap;

const static FName NAME_LandscapeResourceNameForDebugging(TEXT("Landscape"));

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeSectionLODUniformParameters, "LandscapeContinuousLODParameters");

TGlobalResource<FNullLandscapeRenderSystemResources> GNullLandscapeRenderSystemResources;
TMap<FLandscapeNeighborInfo::FLandscapeKey, FLandscapeRenderSystem*> LandscapeRenderSystems;

void FLandscapeRenderSystem::RegisterEntity(FLandscapeComponentSceneProxy* SceneProxy)
{
	check(IsInRenderingThread());
	check(SceneProxy != nullptr);

	if (NumRegisteredEntities > 0)
	{
		// Calculate new bounding rect of landscape components
		FIntPoint OriginalMin = Min;
		FIntPoint OriginalMax = Min + Size - FIntPoint(1, 1);
		FIntPoint NewMin(FMath::Min(Min.X, SceneProxy->ComponentBase.X), FMath::Min(Min.Y, SceneProxy->ComponentBase.Y));
		FIntPoint NewMax(FMath::Max(OriginalMax.X, SceneProxy->ComponentBase.X), FMath::Max(OriginalMax.Y, SceneProxy->ComponentBase.Y));

		FIntPoint SizeRequired = (NewMax - NewMin) + FIntPoint(1, 1);

		if (NewMin != Min || Size != SizeRequired)
		{
			ResizeAndMoveTo(NewMin, SizeRequired);
			RecreateBuffers();
		}

		// Validate system-wide global parameters
		check(TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff == SceneProxy->UseTessellationComponentScreenSizeFalloff);
		check(TessellationFalloffSettings.TessellationComponentSquaredScreenSize == SceneProxy->TessellationComponentSquaredScreenSize);
		check(TessellationFalloffSettings.TessellationComponentScreenSizeFalloff == SceneProxy->TessellationComponentScreenSizeFalloff);

		if (SceneProxy->MaterialHasTessellationEnabled.Find(true) != INDEX_NONE)
		{
			NumEntitiesWithTessellation++;
		}
	}
	else
	{
		TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff = SceneProxy->UseTessellationComponentScreenSizeFalloff;
		TessellationFalloffSettings.TessellationComponentSquaredScreenSize = SceneProxy->TessellationComponentSquaredScreenSize;
		TessellationFalloffSettings.TessellationComponentScreenSizeFalloff = SceneProxy->TessellationComponentScreenSizeFalloff;

		ResizeAndMoveTo(SceneProxy->ComponentBase, FIntPoint(1, 1));
		RecreateBuffers();
	}

	NumRegisteredEntities++;
	SetSectionLODSettings(SceneProxy->ComponentBase, SceneProxy->LODSettings);
	SetSectionOriginAndRadius(SceneProxy->ComponentBase, FVector4(SceneProxy->GetBounds().Origin, SceneProxy->GetBounds().SphereRadius));
	SetSceneProxy(SceneProxy->ComponentBase, SceneProxy);
}

void FLandscapeRenderSystem::UnregisterEntity(FLandscapeComponentSceneProxy* SceneProxy)
{
	check(IsInRenderingThread());
	check(SceneProxy != nullptr);

	SetSceneProxy(SceneProxy->ComponentBase, nullptr);
	SetSectionOriginAndRadius(SceneProxy->ComponentBase, FVector4(ForceInitToZero));

	LODSettingsComponent LODSettings;
	FMemory::Memzero(&LODSettings, sizeof(LODSettingsComponent));
	SetSectionLODSettings(SceneProxy->ComponentBase, LODSettings);

	if (SceneProxy->MaterialHasTessellationEnabled.Find(true) != INDEX_NONE)
	{
		NumEntitiesWithTessellation--;
	}

	NumRegisteredEntities--;
}

void FLandscapeRenderSystem::ResizeAndMoveTo(FIntPoint NewMin, FIntPoint NewSize)
{
	SectionLODBuffer.SafeRelease();
	SectionLODBiasBuffer.SafeRelease();
	SectionTessellationFalloffCBuffer.SafeRelease();
	SectionTessellationFalloffKBuffer.SafeRelease();

	TResourceArray<float> NewSectionLODValues;
	TResourceArray<float> NewSectionLODBiases;
	TResourceArray<float> NewSectionTessellationFalloffC;
	TResourceArray<float> NewSectionTessellationFalloffK;
	TArray<LODSettingsComponent> NewSectionLODSettings;
	TArray<FVector4> NewSectionOriginAndRadius;
	TArray<FLandscapeComponentSceneProxy*> NewSceneProxies;

	NewSectionLODValues.AddZeroed(NewSize.X * NewSize.Y);
	NewSectionLODBiases.AddZeroed(NewSize.X * NewSize.Y);
	NewSectionTessellationFalloffC.AddZeroed(NewSize.X * NewSize.Y);
	NewSectionTessellationFalloffK.AddZeroed(NewSize.X * NewSize.Y);
	NewSectionLODSettings.AddZeroed(NewSize.X * NewSize.Y);
	NewSectionOriginAndRadius.AddZeroed(NewSize.X * NewSize.Y);
	NewSceneProxies.AddZeroed(NewSize.X * NewSize.Y);

	for (int32 Y = 0; Y < Size.Y; Y++)
	{
		for (int32 X = 0; X < Size.X; X++)
		{
			int32 LinearIndex = Y * Size.X + X;
			int32 NewLinearIndex = (Y + (Min.Y - NewMin.Y)) * NewSize.X + (X + (Min.X - NewMin.X));

			if (NewLinearIndex >= 0 && NewLinearIndex < NewSize.X * NewSize.Y)
			{
				NewSectionLODValues[NewLinearIndex] = SectionLODValues[LinearIndex];
				NewSectionLODBiases[NewLinearIndex] = SectionLODBiases[LinearIndex];
				if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
				{
					NewSectionTessellationFalloffC[NewLinearIndex] = SectionTessellationFalloffC[LinearIndex];
					NewSectionTessellationFalloffK[NewLinearIndex] = SectionTessellationFalloffK[LinearIndex];
				}
				NewSectionLODSettings[NewLinearIndex] = SectionLODSettings[LinearIndex];
				NewSectionOriginAndRadius[NewLinearIndex] = SectionOriginAndRadius[LinearIndex];
				NewSceneProxies[NewLinearIndex] = SceneProxies[LinearIndex];
			}
		}
	}

	Min = NewMin;
	Size = NewSize;
	SectionLODValues = NewSectionLODValues;
	SectionLODBiases = NewSectionLODBiases;
	SectionTessellationFalloffC = NewSectionTessellationFalloffC;
	SectionTessellationFalloffK = NewSectionTessellationFalloffK;
	SectionLODSettings = NewSectionLODSettings;
	SectionOriginAndRadius = NewSectionOriginAndRadius;
	SceneProxies = NewSceneProxies;

	if (!(TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0))
	{
		for (float& Value : SectionTessellationFalloffC)
		{
			Value = 1.0f;
		}

		for (float& Value : SectionTessellationFalloffK)
		{
			Value = 0.0f;
		}
	}

	SectionLODValues.SetAllowCPUAccess(true);
	SectionLODBiases.SetAllowCPUAccess(true);
	SectionTessellationFalloffC.SetAllowCPUAccess(true);
	SectionTessellationFalloffK.SetAllowCPUAccess(true);
}

void FLandscapeRenderSystem::PrepareView(const FSceneView* View)
{
#if PLATFORM_SUPPORTS_LANDSCAPE_VISUAL_MESH_LOD_STREAMING
	const int32 NumSceneProxies = SceneProxies.Num();
	SectionCurrentFirstLODIndices.Empty(NumSceneProxies);
	SectionCurrentFirstLODIndices.AddUninitialized(NumSceneProxies);

	for (int32 Idx = 0; Idx < NumSceneProxies; ++Idx)
	{
		const FLandscapeComponentSceneProxy* Proxy = SceneProxies[Idx];
		SectionCurrentFirstLODIndices[Idx] = Proxy ? Proxy->GetCurrentFirstLODIdx_RenderThread() : 0;
	}
#endif
	
	const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance()
		&& GIsThreadedRendering; // Rendering thread is required to safely use rendering resources in parallel.

	if (bExecuteInParallel)
	{
		PerViewParametersTasks.Add(View, TGraphTask<FComputeSectionPerViewParametersTask>::CreateTask(
			nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(*this, View));
	}
	else
	{
		FComputeSectionPerViewParametersTask Task(*this, View);
		Task.AnyThreadTask();
	}
}

void FLandscapeRenderSystem::BeginRenderView(const FSceneView* View)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLandscapeRenderSystem::BeginRenderView());

	if (FetchHeightmapLODBiasesEventRef.IsValid())
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(FetchHeightmapLODBiasesEventRef, ENamedThreads::GetRenderThread_Local());
		FetchHeightmapLODBiasesEventRef.SafeRelease();
	}

	if (PerViewParametersTasks.Contains(View))
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(PerViewParametersTasks[View], ENamedThreads::GetRenderThread_Local());
		PerViewParametersTasks.Remove(View);
	}

	{
		FScopeLock Lock(&CachedValuesCS);

		SectionLODValues = CachedSectionLODValues[View];

		if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
		{
			SectionTessellationFalloffC = CachedSectionTessellationFalloffC[View];
			SectionTessellationFalloffK = CachedSectionTessellationFalloffK[View];
		}
	}

	RecreateBuffers(View);
}

void FLandscapeRenderSystem::ComputeSectionPerViewParameters(
	const FSceneView* ViewPtrAsIdentifier,
	int32 ViewLODOverride,
	float ViewLODDistanceFactor,
	bool bDrawCollisionPawn,
	bool bDrawCollisionCollision,
	FVector ViewOrigin,
	FMatrix ViewProjectionMarix
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLandscapeRenderSystem::ComputeSectionPerViewParameters());

	TResourceArray<float> NewSectionLODValues;
	TResourceArray<float> NewSectionTessellationFalloffC;
	TResourceArray<float> NewSectionTessellationFalloffK;

	NewSectionLODValues.AddZeroed(SectionLODSettings.Num());

	if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
	{
		NewSectionTessellationFalloffC.AddZeroed(SectionLODSettings.Num());
		NewSectionTessellationFalloffK.AddZeroed(SectionLODSettings.Num());
	}

	float LODScale = ViewLODDistanceFactor * CVarStaticMeshLODDistanceScale.GetValueOnRenderThread();

	for (int32 EntityIndex = 0; EntityIndex < SectionLODSettings.Num(); EntityIndex++)
	{
		float MeshScreenSizeSquared = ComputeBoundsScreenRadiusSquared(FVector(SectionOriginAndRadius[EntityIndex]), SectionOriginAndRadius[EntityIndex].W, ViewOrigin, ViewProjectionMarix);

		float FractionalLOD;
		GetLODFromScreenSize(SectionLODSettings[EntityIndex], MeshScreenSizeSquared, LODScale * LODScale, FractionalLOD);

		int32 ForcedLODLevel = SectionLODSettings[EntityIndex].ForcedLOD;
		ForcedLODLevel = ViewLODOverride >= 0 ? ViewLODOverride : ForcedLODLevel;
		const int32 DrawCollisionLODOverride = GetDrawCollisionLodOverride(bDrawCollisionPawn, bDrawCollisionCollision, SectionLODSettings[EntityIndex].DrawCollisionPawnLOD, SectionLODSettings[EntityIndex].DrawCollisionVisibilityLOD);
		ForcedLODLevel = DrawCollisionLODOverride >= 0 ? DrawCollisionLODOverride : ForcedLODLevel;
		ForcedLODLevel = FMath::Min<int32>(ForcedLODLevel, SectionLODSettings[EntityIndex].LastLODIndex);

#if PLATFORM_SUPPORTS_LANDSCAPE_VISUAL_MESH_LOD_STREAMING
		const float CurFirstLODIdx = (float)SectionCurrentFirstLODIndices[EntityIndex];
#else
		constexpr float CurFirstLODIdx = 0.f;
#endif

		NewSectionLODValues[EntityIndex] = FMath::Max(ForcedLODLevel >= 0 ? ForcedLODLevel : FractionalLOD, CurFirstLODIdx);

		if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
		{
			float MaxTesselationDistance = ComputeBoundsDrawDistance(FMath::Sqrt(TessellationFalloffSettings.TessellationComponentSquaredScreenSize), SectionOriginAndRadius[EntityIndex].W / 2.0f, ViewProjectionMarix);
			float FallOffStartingDistance = FMath::Min(
				ComputeBoundsDrawDistance(FMath::Sqrt(FMath::Min(
					FMath::Square(TessellationFalloffSettings.TessellationComponentScreenSizeFalloff),
					TessellationFalloffSettings.TessellationComponentSquaredScreenSize)), SectionOriginAndRadius[EntityIndex].W / 2.0f, ViewProjectionMarix) - MaxTesselationDistance, MaxTesselationDistance);

			// Calculate the falloff using a = C - K * d by sending C & K into the shader
			NewSectionTessellationFalloffC[EntityIndex] = MaxTesselationDistance / (MaxTesselationDistance - FallOffStartingDistance);
			NewSectionTessellationFalloffK[EntityIndex] = -(1 / (-MaxTesselationDistance + FallOffStartingDistance));
		}
	}

	{
		FScopeLock Lock(&CachedValuesCS);

		CachedSectionLODValues.Add(ViewPtrAsIdentifier, NewSectionLODValues);

		if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
		{
			CachedSectionTessellationFalloffC.Add(ViewPtrAsIdentifier, NewSectionTessellationFalloffC);
			CachedSectionTessellationFalloffK.Add(ViewPtrAsIdentifier, NewSectionTessellationFalloffK);
		}
	}
}

void FLandscapeRenderSystem::FetchHeightmapLODBiases()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLandscapeRenderSystem::FetchHeightmapLODBiases());

	// TODO: this function generates A LOT OF cache misses - it should be much better if we have an event of FTexture2DResource::UpdateTexture
	for (int32 EntityIndex = 0; EntityIndex < SceneProxies.Num(); EntityIndex++)
	{
		FLandscapeComponentSceneProxy* SceneProxy = SceneProxies[EntityIndex];
		if (SceneProxy)
		{
			if (SceneProxy->HeightmapTexture && SceneProxy->HeightmapTexture->Resource != nullptr)
			{
				SectionLODBiases[EntityIndex] = SceneProxy->HeightmapTexture->GetNumMips() - SceneProxy->HeightmapTexture->GetNumResidentMips();

				// TODO: support mipmap LOD bias of XY offset map
				//XYOffsetmapTexture ? ((FTexture2DResource*)XYOffsetmapTexture->Resource)->GetCurrentFirstMip() : 0.0f);
			}
		}
	}
}

void FLandscapeRenderSystem::RecreateBuffers(const FSceneView* InView /* = nullptr */)
{
	if (InView == nullptr || CachedView != InView)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FLandscapeRenderSystem::RecreateBuffers());

		if (Size != FIntPoint::ZeroValue)
		{
			if (!SectionLODBuffer.IsValid())
			{
				FRHIResourceCreateInfo CreateInfo(&SectionLODValues);
				SectionLODBuffer = RHICreateVertexBuffer(SectionLODValues.GetResourceDataSize(), BUF_ShaderResource | BUF_Dynamic, CreateInfo);
				SectionLODSRV = RHICreateShaderResourceView(SectionLODBuffer, sizeof(float), PF_R32_FLOAT);
			}
			else
			{
				float* Data = (float*)RHILockVertexBuffer(SectionLODBuffer, 0, SectionLODValues.GetResourceDataSize(), RLM_WriteOnly);
				FMemory::Memcpy(Data, SectionLODValues.GetData(), SectionLODValues.GetResourceDataSize());
				RHIUnlockVertexBuffer(SectionLODBuffer);
			}

			if (!SectionLODBiasBuffer.IsValid())
			{
				FRHIResourceCreateInfo CreateInfo(&SectionLODBiases);
				SectionLODBiasBuffer = RHICreateVertexBuffer(SectionLODBiases.GetResourceDataSize(), BUF_ShaderResource | BUF_Dynamic, CreateInfo);
				SectionLODBiasSRV = RHICreateShaderResourceView(SectionLODBiasBuffer, sizeof(float), PF_R32_FLOAT);
			}
			else
			{
				float* Data = (float*)RHILockVertexBuffer(SectionLODBiasBuffer, 0, SectionLODBiases.GetResourceDataSize(), RLM_WriteOnly);
				FMemory::Memcpy(Data, SectionLODBiases.GetData(), SectionLODBiases.GetResourceDataSize());
				RHIUnlockVertexBuffer(SectionLODBiasBuffer);
			}

			if (!SectionTessellationFalloffCBuffer.IsValid())
			{
				FRHIResourceCreateInfo CreateInfo(&SectionTessellationFalloffC);
				SectionTessellationFalloffCBuffer = RHICreateVertexBuffer(SectionTessellationFalloffC.GetResourceDataSize(), BUF_ShaderResource | BUF_Dynamic, CreateInfo);
				SectionTessellationFalloffCSRV = RHICreateShaderResourceView(SectionTessellationFalloffCBuffer, sizeof(float), PF_R32_FLOAT);
			}
			else
			{
				// If we use tessellation falloff, update the buffer, otherwise use the one already filled with default parameters
				if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
				{
					float* Data = (float*)RHILockVertexBuffer(SectionTessellationFalloffCBuffer, 0, SectionTessellationFalloffC.GetResourceDataSize(), RLM_WriteOnly);
					FMemory::Memcpy(Data, SectionTessellationFalloffC.GetData(), SectionTessellationFalloffC.GetResourceDataSize());
					RHIUnlockVertexBuffer(SectionTessellationFalloffCBuffer);
				}
			}

			if (!SectionTessellationFalloffKBuffer.IsValid())
			{
				FRHIResourceCreateInfo CreateInfo(&SectionTessellationFalloffK);
				SectionTessellationFalloffKBuffer = RHICreateVertexBuffer(SectionTessellationFalloffK.GetResourceDataSize(), BUF_ShaderResource | BUF_Dynamic, CreateInfo);
				SectionTessellationFalloffKSRV = RHICreateShaderResourceView(SectionTessellationFalloffKBuffer, sizeof(float), PF_R32_FLOAT);
			}
			else
			{
				// If we use tessellation falloff, update the buffer, otherwise use the one already filled with default parameters
				if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
				{
					float* Data = (float*)RHILockVertexBuffer(SectionTessellationFalloffKBuffer, 0, SectionTessellationFalloffK.GetResourceDataSize(), RLM_WriteOnly);
					FMemory::Memcpy(Data, SectionTessellationFalloffK.GetData(), SectionTessellationFalloffK.GetResourceDataSize());
					RHIUnlockVertexBuffer(SectionTessellationFalloffKBuffer);
				}
			}

			FLandscapeSectionLODUniformParameters Parameters;
			Parameters.Min = Min;
			Parameters.Size = Size;
			Parameters.SectionLOD = SectionLODSRV;
			Parameters.SectionLODBias = SectionLODBiasSRV;
			Parameters.SectionTessellationFalloffC = SectionTessellationFalloffCSRV;
			Parameters.SectionTessellationFalloffK = SectionTessellationFalloffKSRV;

			if (UniformBuffer.IsValid())
			{
				UniformBuffer.UpdateUniformBufferImmediate(Parameters);
			}
			else
			{
				UniformBuffer = TUniformBufferRef<FLandscapeSectionLODUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleFrame);
			}
		}

		CachedView = InView;
	}
}

void FLandscapeRenderSystem::BeginFrame()
{
	CachedView = nullptr;

	CachedSectionLODValues.Empty();

	if (TessellationFalloffSettings.UseTessellationComponentScreenSizeFalloff && NumEntitiesWithTessellation > 0)
	{
		CachedSectionTessellationFalloffC.Empty();
		CachedSectionTessellationFalloffK.Empty();
	}

	const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance()
		&& GIsThreadedRendering; // Rendering thread is required to safely use rendering resources in parallel.

	if (bExecuteInParallel)
	{
		FetchHeightmapLODBiasesEventRef = TGraphTask<FGetSectionLODBiasesTask>::CreateTask(
			nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(*this);
	}
	else
	{
		FGetSectionLODBiasesTask Task(*this);
		Task.AnyThreadTask();
	}
}

void FLandscapeRenderSystem::EndFrame()
{
	// Finalize any outstanding jobs before ~FSceneRenderer() so we don't have corrupted accesses
	if (FetchHeightmapLODBiasesEventRef.IsValid())
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(FetchHeightmapLODBiasesEventRef, ENamedThreads::GetRenderThread_Local());
		FetchHeightmapLODBiasesEventRef.SafeRelease();
	}

	for (auto& Pair : PerViewParametersTasks)
	{
		const FSceneView* View = Pair.Key;
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(PerViewParametersTasks[View], ENamedThreads::GetRenderThread_Local());
	}

	PerViewParametersTasks.Empty();
}

FLandscapeRenderSystem::FComputeSectionPerViewParametersTask::FComputeSectionPerViewParametersTask(FLandscapeRenderSystem& InRenderSystem, const FSceneView* InView)
	: RenderSystem(InRenderSystem)
	, ViewPtrAsIdentifier(InView)
	, ViewLODOverride(GetViewLodOverride(*InView))
	, ViewLODDistanceFactor(InView->LODDistanceFactor)
	, ViewEngineShowFlagCollisionPawn(InView->Family->EngineShowFlags.CollisionPawn)
	, ViewEngineShowFlagCollisionVisibility(InView->Family->EngineShowFlags.CollisionVisibility)
	, ViewOrigin(GetLODView(*InView).ViewMatrices.GetViewOrigin())
	, ViewProjectionMatrix(GetLODView(*InView).ViewMatrices.GetProjectionMatrix())
{
}

class FLandscapePersistentViewUniformBufferExtension : public IPersistentViewUniformBufferExtension
{
public:
	virtual void BeginFrame() override
	{
		for (auto& Pair : LandscapeRenderSystems)
		{
			FLandscapeRenderSystem& RenderSystem = *Pair.Value;

			RenderSystem.BeginFrame();
		}
	}

	virtual void PrepareView(const FSceneView* View) override
	{
		for (auto& Pair : LandscapeRenderSystems)
		{
			FLandscapeRenderSystem& RenderSystem = *Pair.Value;

			RenderSystem.PrepareView(View);
		}
	}

	virtual void BeginRenderView(const FSceneView* View, bool bShouldWaitForJobs = true) override
	{
		if (!bShouldWaitForJobs)
		{
			return;
		}

		for (auto& Pair : LandscapeRenderSystems)
		{
			FLandscapeRenderSystem& RenderSystem = *Pair.Value;

			RenderSystem.BeginRenderView(View);
		}
	}

	virtual void EndFrame() override
	{
		for (auto& Pair : LandscapeRenderSystems)
		{
			FLandscapeRenderSystem& RenderSystem = *Pair.Value;

			RenderSystem.EndFrame();
		}
	}

} LandscapePersistentViewUniformBufferExtension;

FLandscapeComponentSceneProxy::FLandscapeComponentSceneProxy(ULandscapeComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent, NAME_LandscapeResourceNameForDebugging)
	, FLandscapeNeighborInfo(InComponent->GetWorld(), InComponent->GetLandscapeProxy()->GetLandscapeGuid(), InComponent->GetSectionBase() / InComponent->ComponentSizeQuads, InComponent->GetHeightmap(), InComponent->ForcedLOD, InComponent->LODBias)
	, MaxLOD(FMath::CeilLogTwo(InComponent->SubsectionSizeQuads + 1) - 1)
	, UseTessellationComponentScreenSizeFalloff(InComponent->GetLandscapeProxy()->UseTessellationComponentScreenSizeFalloff)
	, bRequiresAdjacencyInformation(false)
	, NumWeightmapLayerAllocations(InComponent->GetWeightmapLayerAllocations().Num())
	, StaticLightingLOD(InComponent->GetLandscapeProxy()->StaticLightingLOD)
	, WeightmapSubsectionOffset(InComponent->WeightmapSubsectionOffset)
	, FirstLOD(0)
	, LastLOD(MaxLOD)
	, ComponentMaxExtend(0.0f)
	, ComponentSquaredScreenSizeToUseSubSections(FMath::Square(InComponent->GetLandscapeProxy()->ComponentScreenSizeToUseSubSections))
	, TessellationComponentSquaredScreenSize(FMath::Square(InComponent->GetLandscapeProxy()->TessellationComponentScreenSize))
	, TessellationComponentScreenSizeFalloff(InComponent->GetLandscapeProxy()->TessellationComponentScreenSizeFalloff)
	, NumSubsections(InComponent->NumSubsections)
	, SubsectionSizeQuads(InComponent->SubsectionSizeQuads)
	, SubsectionSizeVerts(InComponent->SubsectionSizeQuads + 1)
	, ComponentSizeQuads(InComponent->ComponentSizeQuads)
	, ComponentSizeVerts(InComponent->ComponentSizeQuads + 1)
	, SectionBase(InComponent->GetSectionBase())
	, LandscapeComponent(InComponent)
	, WeightmapScaleBias(InComponent->WeightmapScaleBias)
	, WeightmapTextures(InComponent->GetWeightmapTextures())
	, VisibilityWeightmapTexture(nullptr)
	, VisibilityWeightmapChannel(-1)
	, NormalmapTexture(InComponent->GetHeightmap())
	, BaseColorForGITexture(InComponent->GIBakedBaseColorTexture)
	, HeightmapScaleBias(InComponent->HeightmapScaleBias)
	, XYOffsetmapTexture(InComponent->XYOffsetmapTexture)
	, BlendableLayerMask(InComponent->MobileBlendableLayerMask)
	, SharedBuffersKey(0)
	, SharedBuffers(nullptr)
	, VertexFactory(nullptr)
	, ComponentLightInfo(nullptr)
#if WITH_EDITORONLY_DATA
	, EditToolRenderData(InComponent->EditToolRenderData)
	, LODFalloff_DEPRECATED(InComponent->GetLandscapeProxy()->LODFalloff_DEPRECATED)
#endif
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	, CollisionMipLevel(InComponent->CollisionMipLevel)
	, SimpleCollisionMipLevel(InComponent->SimpleCollisionMipLevel)
	, CollisionResponse(InComponent->GetLandscapeProxy()->BodyInstance.GetResponseToChannels())
#endif
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	, LightMapResolution(InComponent->GetStaticLightMapResolution())
#endif
{
	const auto FeatureLevel = GetScene().GetFeatureLevel();

	if (FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		if (InComponent->GetLandscapeProxy()->bUseDynamicMaterialInstance)
		{
			AvailableMaterials.Append(InComponent->MaterialInstancesDynamic);
		}
		else
		{
			AvailableMaterials.Append(InComponent->MaterialInstances);
		}
	}
	else
	{
		AvailableMaterials.Append(InComponent->MobileMaterialInterfaces);
	}

	MaterialIndexToDisabledTessellationMaterial = InComponent->MaterialIndexToDisabledTessellationMaterial;
	LODIndexToMaterialIndex = InComponent->LODIndexToMaterialIndex;
	check(LODIndexToMaterialIndex.Num() == MaxLOD+1);

	if (!IsComponentLevelVisible())
	{
		bNeedsLevelAddedToWorldNotification = true;
	}

	SetLevelColor(FLinearColor(1.f, 1.f, 1.f));

	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		HeightmapTexture = nullptr;
		HeightmapSubsectionOffsetU = 0;
		HeightmapSubsectionOffsetV = 0;
	}
	else
	{
		HeightmapSubsectionOffsetU = ((float)(InComponent->SubsectionSizeQuads + 1) / (float)FMath::Max<int32>(1, HeightmapTexture->GetSizeX()));
		HeightmapSubsectionOffsetV = ((float)(InComponent->SubsectionSizeQuads + 1) / (float)FMath::Max<int32>(1, HeightmapTexture->GetSizeY()));
	}

	float ScreenSizeRatioDivider = FMath::Max(InComponent->GetLandscapeProxy()->LOD0DistributionSetting * GLandscapeLOD0DistributionScale, 1.01f);
	// Cancel out so that landscape is not affected by r.StaticMeshLODDistanceScale
	float CurrentScreenSizeRatio = InComponent->GetLandscapeProxy()->LOD0ScreenSize / CVarStaticMeshLODDistanceScale.GetValueOnAnyThread();

	LODScreenRatioSquared.AddUninitialized(MaxLOD + 1);

	// LOD 0 handling
	LODScreenRatioSquared[0] = FMath::Square(CurrentScreenSizeRatio);
	LODSettings.LOD0ScreenSizeSquared = FMath::Square(CurrentScreenSizeRatio);
	CurrentScreenSizeRatio /= ScreenSizeRatioDivider;
	LODSettings.LOD1ScreenSizeSquared = FMath::Square(CurrentScreenSizeRatio);
	ScreenSizeRatioDivider = FMath::Max(InComponent->GetLandscapeProxy()->LODDistributionSetting * GLandscapeLODDistributionScale, 1.01f);
	LODSettings.LODOnePlusDistributionScalarSquared = FMath::Square(ScreenSizeRatioDivider);

	// Other LODs
	for (int32 LODIndex = 1; LODIndex <= MaxLOD; ++LODIndex) // This should ALWAYS be calculated from the component size, not user MaxLOD override
	{
		LODScreenRatioSquared[LODIndex] = FMath::Square(CurrentScreenSizeRatio);
		CurrentScreenSizeRatio /= ScreenSizeRatioDivider;
	}

	FirstLOD = 0;
	LastLOD = MaxLOD;	// we always need to go to MaxLOD regardless of LODBias as we could need the lowest LODs due to streaming.

	// Make sure out LastLOD is > of MinStreamedLOD otherwise we would not be using the right LOD->MIP, the only drawback is a possible minor memory usage for overallocating static mesh element batch
	const int32 MinStreamedLOD = HeightmapTexture ? FMath::Min<int32>(HeightmapTexture->GetNumMips() - HeightmapTexture->GetNumResidentMips(), FMath::CeilLogTwo(SubsectionSizeVerts) - 1) : 0;
	LastLOD = FMath::Max(MinStreamedLOD, LastLOD);

	// Clamp to MaxLODLevel
	const int32 MaxLODLevel = InComponent->GetLandscapeProxy()->MaxLODLevel;
	if ( MaxLODLevel >= 0)
	{
		MaxLOD = FMath::Min<int8>(MaxLODLevel, MaxLOD);
		LastLOD = FMath::Min<int32>(MaxLODLevel, LastLOD);
	}

	// Clamp ForcedLOD to the valid range and then apply
	ForcedLOD = ForcedLOD >= 0 ? FMath::Clamp<int32>(ForcedLOD, FirstLOD, LastLOD) : ForcedLOD;
	FirstLOD = ForcedLOD >= 0 ? ForcedLOD : FirstLOD;
	LastLOD = ForcedLOD >= 0 ? ForcedLOD : LastLOD;

	LODSettings.LastLODIndex = LastLOD;
	LODSettings.LastLODScreenSizeSquared = LODScreenRatioSquared[LastLOD];
	LODSettings.ForcedLOD = ForcedLOD;

	LODBias = FMath::Clamp<int8>(LODBias, -MaxLOD, MaxLOD);

	int8 LocalLODBias = LODBias + (int8)GLandscapeMeshLODBias;
	MinValidLOD = FMath::Clamp<int8>(LocalLODBias, -MaxLOD, MaxLOD);
	MaxValidLOD = FMath::Min<int32>(MaxLOD, MaxLOD + LocalLODBias);

	LastVirtualTextureLOD = MaxLOD;
	FirstVirtualTextureLOD = FMath::Max(MaxLOD - InComponent->GetLandscapeProxy()->VirtualTextureNumLods, 0);
	VirtualTextureLodBias = InComponent->GetLandscapeProxy()->VirtualTextureLodBias;

#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	LODSettings.DrawCollisionPawnLOD = CollisionResponse.GetResponse(ECC_Pawn) == ECR_Ignore ? -1 : SimpleCollisionMipLevel;
	LODSettings.DrawCollisionVisibilityLOD = CollisionResponse.GetResponse(ECC_Visibility) == ECR_Ignore ? -1 : CollisionMipLevel;
#else
	LODSettings.DrawCollisionPawnLOD = LODSettings.DrawCollisionVisibilityLOD = -1;
#endif

	ComponentMaxExtend = SubsectionSizeQuads * FMath::Max(InComponent->GetComponentTransform().GetScale3D().X, InComponent->GetComponentTransform().GetScale3D().Y);

	if (NumSubsections > 1)
	{
		FRotator ComponentRotator = LandscapeComponent->GetComponentRotation();
		float SubSectionMaxExtend = ComponentMaxExtend / 2.0f;
		FVector ComponentTopLeftCorner = LandscapeComponent->Bounds.Origin - ComponentRotator.RotateVector(FVector(SubSectionMaxExtend, SubSectionMaxExtend, 0.0f));

		SubSectionScreenSizeTestingPosition.AddUninitialized(MAX_SUBSECTION_COUNT);

		for (int32 SubY = 0; SubY < NumSubsections; ++SubY)
		{
			for (int32 SubX = 0; SubX < NumSubsections; ++SubX)
			{
				int32 SubSectionIndex = SubX + SubY * NumSubsections;
				SubSectionScreenSizeTestingPosition[SubSectionIndex] = ComponentTopLeftCorner + ComponentRotator.RotateVector(FVector(ComponentMaxExtend * SubX, ComponentMaxExtend * SubY, 0.0f));
			}
		}
	}

	if (InComponent->StaticLightingResolution > 0.f)
	{
		StaticLightingResolution = InComponent->StaticLightingResolution;
	}
	else
	{
		StaticLightingResolution = InComponent->GetLandscapeProxy()->StaticLightingResolution;
	}

	ComponentLightInfo = MakeUnique<FLandscapeLCI>(InComponent);
	check(ComponentLightInfo);

	const bool bHasStaticLighting = ComponentLightInfo->GetLightMap() || ComponentLightInfo->GetShadowMap();

	// Check material usage
	if (ensure(AvailableMaterials.Num() > 0))
	{
		for (UMaterialInterface*& MaterialInterface : AvailableMaterials)
		{
			if (MaterialInterface == nullptr ||
				(bHasStaticLighting && !MaterialInterface->CheckMaterialUsage_Concurrent(MATUSAGE_StaticLighting)))
			{
				MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
			}
		}
	}
	else
	{
		AvailableMaterials.Add(UMaterial::GetDefaultMaterial(MD_Surface));
	}

	MaterialRelevances.Reserve(AvailableMaterials.Num());

	for (UMaterialInterface*& MaterialInterface : AvailableMaterials)
	{
		const UMaterial* LandscapeMaterial = MaterialInterface != nullptr ? MaterialInterface->GetMaterial_Concurrent() : nullptr;

		if (LandscapeMaterial != nullptr)
		{
			UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);

			// In some case it's possible that the Material Instance we have and the Material are not related, for example, in case where content was force deleted, we can have a MIC with no parent, so GetMaterial will fallback to the default material.
			// and since the MIC is not really valid, dont generate the relevance.
			if (MaterialInstance == nullptr || MaterialInstance->IsChildOf(LandscapeMaterial))
			{
				MaterialRelevances.Add(MaterialInterface->GetRelevance_Concurrent(FeatureLevel));
			}

			bRequiresAdjacencyInformation |= RequiresAdjacencyInformation(MaterialInterface, XYOffsetmapTexture == nullptr ? &FLandscapeVertexFactory::StaticType : &FLandscapeXYOffsetVertexFactory::StaticType, InComponent->GetWorld()->FeatureLevel.GetValue());

			bool HasTessellationEnabled = false;

			if (FeatureLevel >= ERHIFeatureLevel::SM5)
			{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
				HasTessellationEnabled = LandscapeMaterial->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}

			MaterialHasTessellationEnabled.Add(HasTessellationEnabled);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) || (UE_BUILD_SHIPPING && WITH_EDITOR)
	if (GIsEditor)
	{
		ALandscapeProxy* Proxy = InComponent->GetLandscapeProxy();
		// Try to find a color for level coloration.
		if (Proxy)
		{
			ULevel* Level = Proxy->GetLevel();
			ULevelStreaming* LevelStreaming = FLevelUtils::FindStreamingLevel(Level);
			if (LevelStreaming)
			{
				SetLevelColor(LevelStreaming->LevelColor);
			}
		}
	}
#endif

	const int8 SubsectionSizeLog2 = FMath::CeilLogTwo(InComponent->SubsectionSizeQuads + 1);
	SharedBuffersKey = (SubsectionSizeLog2 & 0xf) | ((NumSubsections & 0xf) << 4) |
		(FeatureLevel <= ERHIFeatureLevel::ES3_1 ? 0 : 1 << 30) | (XYOffsetmapTexture == nullptr ? 0 : 1 << 31);

	bSupportsHeightfieldRepresentation = true;

#if WITH_EDITOR
	TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = InComponent->GetWeightmapLayerAllocations();
	
	for (auto& Allocation : ComponentWeightmapLayerAllocations)
	{
		if (Allocation.LayerInfo != nullptr)
		{
			LayerColors.Add(Allocation.LayerInfo->LayerUsageDebugColor);
		}
	}

	for (int32 Idx = 0; Idx < InComponent->WeightmapLayerAllocations.Num(); Idx++)
	{
		FWeightmapLayerAllocationInfo& Allocation = InComponent->WeightmapLayerAllocations[Idx];
		if (Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer && Allocation.IsAllocated())
		{
			VisibilityWeightmapTexture = WeightmapTextures[Allocation.WeightmapTextureIndex];
			VisibilityWeightmapChannel = Allocation.WeightmapTextureChannel;
			break;
		}
	}
#endif
}

void FLandscapeComponentSceneProxy::CreateRenderThreadResources()
{
	LLM_SCOPE(ELLMTag::Landscape);

	check(HeightmapTexture != nullptr);

	if (IsComponentLevelVisible())
	{
		RegisterNeighbors(this);
	}

	auto FeatureLevel = GetScene().GetFeatureLevel();

	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		SharedBuffers = new FLandscapeSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			FeatureLevel, bRequiresAdjacencyInformation, /*NumOcclusionVertices*/ 0);

		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);

		if (!XYOffsetmapTexture)
		{
			FLandscapeVertexFactory* LandscapeVertexFactory = new FLandscapeVertexFactory(FeatureLevel);
			LandscapeVertexFactory->Data.PositionComponent = FVertexStreamComponent(SharedBuffers->VertexBuffer, 0, sizeof(FLandscapeVertex), VET_Float4);
			LandscapeVertexFactory->InitResource();
			SharedBuffers->VertexFactory = LandscapeVertexFactory;
		}
		else
		{
			FLandscapeXYOffsetVertexFactory* LandscapeXYOffsetVertexFactory = new FLandscapeXYOffsetVertexFactory(FeatureLevel);
			LandscapeXYOffsetVertexFactory->Data.PositionComponent = FVertexStreamComponent(SharedBuffers->VertexBuffer, 0, sizeof(FLandscapeVertex), VET_Float4);
			LandscapeXYOffsetVertexFactory->InitResource();
			SharedBuffers->VertexFactory = LandscapeXYOffsetVertexFactory;
		}

		// we need the fixed grid vertex factory for both virtual texturing and grass : 
		bool bNeedsFixedGridVertexFactory = UseVirtualTexturing(FeatureLevel);

#if WITH_EDITOR
		bNeedsFixedGridVertexFactory |= (SharedBuffers->GrassIndexBuffer != nullptr);
#endif // WITH_EDITOR

		if (bNeedsFixedGridVertexFactory)
		{
			//todo[vt]: We will need a version of this to support XYOffsetmapTexture
			FLandscapeFixedGridVertexFactory* LandscapeVertexFactory = new FLandscapeFixedGridVertexFactory(FeatureLevel);
			LandscapeVertexFactory->Data.PositionComponent = FVertexStreamComponent(SharedBuffers->VertexBuffer, 0, sizeof(FLandscapeVertex), VET_Float4);
			LandscapeVertexFactory->InitResource();
			SharedBuffers->FixedGridVertexFactory = LandscapeVertexFactory;
		}
	}

	SharedBuffers->AddRef();

	if (bRequiresAdjacencyInformation)
	{
		if (SharedBuffers->AdjacencyIndexBuffers == nullptr)
		{
			ensure(SharedBuffers->NumIndexBuffers > 0);
			if (SharedBuffers->IndexBuffers[0])
			{
				// Recreate Index Buffers, this case happens only there are Landscape Components using different material (one uses tessellation, other don't use it) 
				if (SharedBuffers->bUse32BitIndices && !((FRawStaticIndexBuffer16or32<uint32>*)SharedBuffers->IndexBuffers[0])->Num())
				{
					SharedBuffers->CreateIndexBuffers<uint32>(FeatureLevel, bRequiresAdjacencyInformation);
				}
				else if (!((FRawStaticIndexBuffer16or32<uint16>*)SharedBuffers->IndexBuffers[0])->Num())
				{
					SharedBuffers->CreateIndexBuffers<uint16>(FeatureLevel, bRequiresAdjacencyInformation);
				}
			}

			SharedBuffers->AdjacencyIndexBuffers = new FLandscapeSharedAdjacencyIndexBuffer(SharedBuffers);
		}

		// Delayed Initialize for IndexBuffers
		for (int32 i = 0; i < SharedBuffers->NumIndexBuffers; i++)
		{
			SharedBuffers->IndexBuffers[i]->InitResource();
		}
	}

	// Assign vertex factory
	VertexFactory = SharedBuffers->VertexFactory;
	FixedGridVertexFactory = SharedBuffers->FixedGridVertexFactory;

	// Assign LandscapeUniformShaderParameters
	LandscapeUniformShaderParameters.InitResource();

	// Create per Lod uniform buffers
	const int32 NumMips = FMath::CeilLogTwo(SubsectionSizeVerts); 
	// create as many as there are potential mips (even if MaxLOD can be inferior than that), because the grass could need that much :
	LandscapeFixedGridUniformShaderParameters.AddDefaulted(NumMips);
	for (int32 LodIndex = 0; LodIndex < NumMips; ++LodIndex)
	{
		LandscapeFixedGridUniformShaderParameters[LodIndex].InitResource();
		FLandscapeFixedGridUniformShaderParameters Parameters;
		Parameters.LodValues = FVector4(
			LodIndex, 
			0.f,
			(float)((SubsectionSizeVerts >> LodIndex) - 1),
			1.f / (float)((SubsectionSizeVerts >> LodIndex) - 1));
		LandscapeFixedGridUniformShaderParameters[LodIndex].SetContents(Parameters);
	}

#if WITH_EDITOR
	// Create MeshBatch for grass rendering
	if (SharedBuffers->GrassIndexBuffer)
	{
		check(FixedGridVertexFactory != nullptr);

		GrassMeshBatch.Elements.Empty(NumMips);
		GrassMeshBatch.Elements.AddDefaulted(NumMips);
		GrassBatchParams.Empty(NumMips);
		GrassBatchParams.AddDefaulted(NumMips);
		
		// Grass is being generated using LOD0 material only
		// It uses the fixed grid vertex factory so it doesn't support XY offsets
		FMaterialRenderProxy* RenderProxy = AvailableMaterials[LODIndexToMaterialIndex[0]]->GetRenderProxy();
		GrassMeshBatch.VertexFactory = FixedGridVertexFactory;
		GrassMeshBatch.MaterialRenderProxy = RenderProxy;
		GrassMeshBatch.LCI = nullptr;
		GrassMeshBatch.ReverseCulling = false;
		GrassMeshBatch.CastShadow = false;
		GrassMeshBatch.Type = PT_PointList;
		GrassMeshBatch.DepthPriorityGroup = SDPG_World;

		// Combined grass rendering batch element
		FMeshBatchElement* GrassBatchElement = &GrassMeshBatch.Elements[0];
		FLandscapeBatchElementParams* BatchElementParams = &GrassBatchParams[0];
		BatchElementParams->LandscapeUniformShaderParametersResource = &LandscapeUniformShaderParameters;
		BatchElementParams->FixedGridUniformShaderParameters = &LandscapeFixedGridUniformShaderParameters;
		BatchElementParams->SceneProxy = this;
		BatchElementParams->CurrentLOD = 0;
		GrassBatchElement->UserData = BatchElementParams;
		GrassBatchElement->PrimitiveUniformBuffer = GetUniformBuffer();
		GrassBatchElement->IndexBuffer = SharedBuffers->GrassIndexBuffer;
		GrassBatchElement->NumPrimitives = FMath::Square(NumSubsections) * FMath::Square(SubsectionSizeVerts);
		GrassBatchElement->FirstIndex = 0;
		GrassBatchElement->MinVertexIndex = 0;
		GrassBatchElement->MaxVertexIndex = SharedBuffers->NumVertices - 1;

		// Grass system is also used to bake out heights which are source for collision data when bBakeMaterialPositionOffsetIntoCollision is enabled
		for (int32 Mip = 1; Mip < NumMips; ++Mip)
		{
			const int32 MipSubsectionSizeVerts = SubsectionSizeVerts >> Mip;

			FMeshBatchElement* CollisionBatchElement = &GrassMeshBatch.Elements[Mip];
			*CollisionBatchElement = *GrassBatchElement;
			FLandscapeBatchElementParams* CollisionBatchElementParams = &GrassBatchParams[Mip];
			*CollisionBatchElementParams = *BatchElementParams;
			CollisionBatchElementParams->CurrentLOD = Mip;
			CollisionBatchElement->UserData = CollisionBatchElementParams;
			CollisionBatchElement->NumPrimitives = FMath::Square(NumSubsections) * FMath::Square(MipSubsectionSizeVerts);
			CollisionBatchElement->FirstIndex = SharedBuffers->GrassIndexMipOffsets[Mip];
		}
	}
#endif

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		for (int32 SubY = 0; SubY < NumSubsections; SubY++)
		{
			for (int32 SubX = 0; SubX < NumSubsections; SubX++)
			{
				const int8 SubSectionIdx = SubX + SubY * NumSubsections;

				FRayTracingGeometryInitializer Initializer;
				static const FName DebugName("FLandscapeComponentSceneProxy");
				static int32 DebugNumber = 0;
				Initializer.DebugName = FName(DebugName, DebugNumber++);

				FRHIResourceCreateInfo CreateInfo;
				Initializer.IndexBuffer = nullptr;
				Initializer.GeometryType = RTGT_Triangles;
				Initializer.bFastBuild = true;
				Initializer.bAllowUpdate = true;
				FRayTracingGeometrySegment Segment;
				Segment.VertexBuffer = nullptr;
				Segment.VertexBufferStride = sizeof(FVector);
				Segment.VertexBufferElementType = VET_Float3;
				Initializer.Segments.Add(Segment);
				SectionRayTracingStates[SubSectionIdx].Geometry.SetInitializer(Initializer);
				SectionRayTracingStates[SubSectionIdx].Geometry.InitResource();

				FLandscapeVertexFactoryMVFParameters UniformBufferParams;
				UniformBufferParams.SubXY = FIntPoint(SubX, SubY);
				SectionRayTracingStates[SubSectionIdx].UniformBuffer = FLandscapeVertexFactoryMVFUniformBufferRef::CreateUniformBufferImmediate(UniformBufferParams, UniformBuffer_MultiFrame);
			}
		}
	}
#endif
}

void FLandscapeComponentSceneProxy::DestroyRenderThreadResources()
{
	FPrimitiveSceneProxy::DestroyRenderThreadResources();
	UnregisterNeighbors(this);
}

void FLandscapeComponentSceneProxy::OnLevelAddedToWorld()
{
	RegisterNeighbors(this);
}

FLandscapeComponentSceneProxy::~FLandscapeComponentSceneProxy()
{
	// Free the subsection uniform buffer
	LandscapeUniformShaderParameters.ReleaseResource();

	// Free the lod uniform buffers
	for (int32 i = 0; i < LandscapeFixedGridUniformShaderParameters.Num(); ++i)
	{
		LandscapeFixedGridUniformShaderParameters[i].ReleaseResource();
	}

	if (SharedBuffers)
	{
		check(SharedBuffers == FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey));
		if (SharedBuffers->Release() == 0)
		{
			FLandscapeComponentSceneProxy::SharedBuffersMap.Remove(SharedBuffersKey);
		}
		SharedBuffers = nullptr;
	}

#if RHI_RAYTRACING
	for (int32 SubY = 0; SubY < NumSubsections; SubY++)
	{
		for (int32 SubX = 0; SubX < NumSubsections; SubX++)
		{
			const int8 SubSectionIdx = SubX + SubY * NumSubsections;
			SectionRayTracingStates[SubSectionIdx].Geometry.ReleaseResource();
			SectionRayTracingStates[SubSectionIdx].RayTracingDynamicVertexBuffer.Release();
		}
	}
#endif
}

bool FLandscapeComponentSceneProxy::CanBeOccluded() const
{
	for (const FMaterialRelevance& Relevance : MaterialRelevances)
	{
		if (!Relevance.bDisableDepthTest)
		{
			return true;
		}
	}

	return false;
}

FPrimitiveViewRelevance FLandscapeComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	const bool bCollisionView = (View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn);
	Result.bDrawRelevance = (IsShown(View) || bCollisionView) && View->Family->EngineShowFlags.Landscape;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

	auto FeatureLevel = View->GetFeatureLevel();

#if WITH_EDITOR
	if (!GLandscapeEditModeActive)
	{
		// No tools to render, just use the cached material relevance.
#endif
		for (const FMaterialRelevance& MaterialRelevance : MaterialRelevances)
		{
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
		}

#if WITH_EDITOR
	}
	else
	{
		for (const FMaterialRelevance& MaterialRelevance : MaterialRelevances)
		{
			// Also add the tool material(s)'s relevance to the MaterialRelevance
			FMaterialRelevance ToolRelevance = MaterialRelevance;

			// Tool brushes and Gizmo
			if (EditToolRenderData.ToolMaterial)
			{
				Result.bDynamicRelevance = true;
				ToolRelevance |= EditToolRenderData.ToolMaterial->GetRelevance_Concurrent(FeatureLevel);
			}

			if (EditToolRenderData.GizmoMaterial)
			{
				Result.bDynamicRelevance = true;
				ToolRelevance |= EditToolRenderData.GizmoMaterial->GetRelevance_Concurrent(FeatureLevel);
			}

			// Region selection
			if (EditToolRenderData.SelectedType)
			{
				if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::SelectRegion) && (EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_REGION)
					&& !(GLandscapeEditRenderMode & ELandscapeEditRenderMode::Mask) && GSelectionRegionMaterial)
				{
					Result.bDynamicRelevance = true;
					ToolRelevance |= GSelectionRegionMaterial->GetRelevance_Concurrent(FeatureLevel);
				}
				if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::SelectComponent) && (EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_COMPONENT) && GSelectionColorMaterial)
				{
					Result.bDynamicRelevance = true;
					ToolRelevance |= GSelectionColorMaterial->GetRelevance_Concurrent(FeatureLevel);
				}
			}

			// Mask
			if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::Mask) && GMaskRegionMaterial != nullptr &&
				(((EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_REGION)) || (!(GLandscapeEditRenderMode & ELandscapeEditRenderMode::InvertedMask))))
			{
				Result.bDynamicRelevance = true;
				ToolRelevance |= GMaskRegionMaterial->GetRelevance_Concurrent(FeatureLevel);
			}

			if (GLandscapeViewMode == ELandscapeViewMode::LayerContribution)
			{
				Result.bDynamicRelevance = true;
				ToolRelevance |= GColorMaskRegionMaterial->GetRelevance_Concurrent(FeatureLevel);
			}

			if (CVarLandscapeShowDirty.GetValueOnRenderThread() && GLandscapeDirtyMaterial)
			{
				Result.bDynamicRelevance = true;
				ToolRelevance |= GLandscapeDirtyMaterial->GetRelevance_Concurrent(FeatureLevel);
			}

			ToolRelevance.SetPrimitiveViewRelevance(Result);
		}
	}

	// Various visualizations need to render using dynamic relevance
	if ((View->Family->EngineShowFlags.Bounds && IsSelected()) ||
		GLandscapeDebugOptions.bShowPatches)
	{
		Result.bDynamicRelevance = true;
	}
#endif

#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const bool bInCollisionView = View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn;
#endif

	// Use the dynamic path for rendering landscape components pass only for Rich Views or if the static path is disabled for debug.
	if (IsRichView(*View->Family) ||
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		bInCollisionView ||
#endif
		GLandscapeDebugOptions.bDisableStatic ||
		View->Family->EngineShowFlags.Wireframe ||
#if WITH_EDITOR
		(IsSelected() && !GLandscapeEditModeActive) ||
		(GLandscapeViewMode != ELandscapeViewMode::Normal) ||
		(CVarLandscapeShowDirty.GetValueOnAnyThread() && GLandscapeDirtyMaterial) ||
		(GetViewLodOverride(*View) >= 0)
#else
		IsSelected()
#endif
		)
	{
		Result.bDynamicRelevance = true;
	}
	else
	{
		Result.bStaticRelevance = true;
	}

	Result.bShadowRelevance = (GAllowLandscapeShadows > 0) && IsShadowCast(View) && View->Family->EngineShowFlags.Landscape;
	return Result;
}

/**
*	Determines the relevance of this primitive's elements to the given light.
*	@param	LightSceneProxy			The light to determine relevance for
*	@param	bDynamic (output)		The light is dynamic for this primitive
*	@param	bRelevant (output)		The light is relevant for this primitive
*	@param	bLightMapped (output)	The light is light mapped for this primitive
*/
void FLandscapeComponentSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const
{
	// Attach the light to the primitive's static meshes.
	bDynamic = true;
	bRelevant = false;
	bLightMapped = true;
	bShadowMapped = true;

	if (ComponentLightInfo)
	{
		ELightInteractionType InteractionType = ComponentLightInfo->GetInteraction(LightSceneProxy).GetType();

		if (InteractionType != LIT_CachedIrrelevant)
		{
			bRelevant = true;
		}

		if (InteractionType != LIT_CachedLightMap && InteractionType != LIT_CachedIrrelevant)
		{
			bLightMapped = false;
		}

		if (InteractionType != LIT_Dynamic)
		{
			bDynamic = false;
		}

		if (InteractionType != LIT_CachedSignedDistanceFieldShadowMap2D)
		{
			bShadowMapped = false;
		}
	}
	else
	{
		bRelevant = true;
		bLightMapped = false;
	}
}

SIZE_T FLandscapeComponentSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FLightInteraction FLandscapeComponentSceneProxy::FLandscapeLCI::GetInteraction(const class FLightSceneProxy* LightSceneProxy) const
{
	// ask base class
	ELightInteractionType LightInteraction = GetStaticInteraction(LightSceneProxy, IrrelevantLights);

	if (LightInteraction != LIT_MAX)
	{
		return FLightInteraction(LightInteraction);
	}

	// Use dynamic lighting if the light doesn't have static lighting.
	return FLightInteraction::Dynamic();
}

#if WITH_EDITOR
namespace DebugColorMask
{
	const FLinearColor Masks[5] =
	{
		FLinearColor(1.f, 0.f, 0.f, 0.f),
		FLinearColor(0.f, 1.f, 0.f, 0.f),
		FLinearColor(0.f, 0.f, 1.f, 0.f),
		FLinearColor(0.f, 0.f, 0.f, 1.f),
		FLinearColor(0.f, 0.f, 0.f, 0.f)
	};
};
#endif

void FLandscapeComponentSceneProxy::OnTransformChanged()
{
	// Set Lightmap ScaleBias
	int32 PatchExpandCountX = 0;
	int32 PatchExpandCountY = 0;
	int32 DesiredSize = 1; // output by GetTerrainExpandPatchCount but not used below
	const float LightMapRatio = ::GetTerrainExpandPatchCount(StaticLightingResolution, PatchExpandCountX, PatchExpandCountY, ComponentSizeQuads, (NumSubsections * (SubsectionSizeQuads + 1)), DesiredSize, StaticLightingLOD);
	const float LightmapLODScaleX = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountX);
	const float LightmapLODScaleY = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountY);
	const float LightmapBiasX = PatchExpandCountX * LightmapLODScaleX;
	const float LightmapBiasY = PatchExpandCountY * LightmapLODScaleY;
	const float LightmapScaleX = LightmapLODScaleX * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / ComponentSizeQuads;
	const float LightmapScaleY = LightmapLODScaleY * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / ComponentSizeQuads;
	const float LightmapExtendFactorX = (float)SubsectionSizeQuads * LightmapScaleX;
	const float LightmapExtendFactorY = (float)SubsectionSizeQuads * LightmapScaleY;

	// cache component's WorldToLocal
	FMatrix LtoW = GetLocalToWorld();
	WorldToLocal = LtoW.Inverse();

	// cache component's LocalToWorldNoScaling
	LocalToWorldNoScaling = LtoW;
	LocalToWorldNoScaling.RemoveScaling();

	// Set FLandscapeUniformVSParameters for this subsection
	FLandscapeUniformShaderParameters LandscapeParams;
	LandscapeParams.ComponentBaseX = ComponentBase.X;
	LandscapeParams.ComponentBaseY = ComponentBase.Y;
	LandscapeParams.SubsectionSizeVerts = SubsectionSizeVerts;
	LandscapeParams.NumSubsections = NumSubsections;
	LandscapeParams.LastLOD = LastLOD;
	LandscapeParams.HeightmapUVScaleBias = HeightmapScaleBias;
	LandscapeParams.WeightmapUVScaleBias = WeightmapScaleBias;
	LandscapeParams.LocalToWorldNoScaling = LocalToWorldNoScaling;

	LandscapeParams.LandscapeLightmapScaleBias = FVector4(
		LightmapScaleX,
		LightmapScaleY,
		LightmapBiasY,
		LightmapBiasX);
	LandscapeParams.SubsectionSizeVertsLayerUVPan = FVector4(
		SubsectionSizeVerts,
		1.f / (float)SubsectionSizeQuads,
		SectionBase.X,
		SectionBase.Y
	);
	LandscapeParams.SubsectionOffsetParams = FVector4(
		HeightmapSubsectionOffsetU,
		HeightmapSubsectionOffsetV,
		WeightmapSubsectionOffset,
		SubsectionSizeQuads
	);
	LandscapeParams.LightmapSubsectionOffsetParams = FVector4(
		LightmapExtendFactorX,
		LightmapExtendFactorY,
		0,
		0
	);
	LandscapeParams.BlendableLayerMask = FVector4(
		BlendableLayerMask & (1 << 0) ? 1 : 0,
		BlendableLayerMask & (1 << 1) ? 1 : 0,
		BlendableLayerMask & (1 << 2) ? 1 : 0,
		0
	);

	if (HeightmapTexture)
	{
		LandscapeParams.HeightmapTexture = HeightmapTexture->TextureReference.TextureReferenceRHI;
		LandscapeParams.HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
	}
	else
	{
		LandscapeParams.HeightmapTexture = GBlackTexture->TextureRHI;
		LandscapeParams.HeightmapTextureSampler = GBlackTexture->SamplerStateRHI;
	}

	if (XYOffsetmapTexture)
	{
		LandscapeParams.XYOffsetmapTexture = XYOffsetmapTexture->TextureReference.TextureReferenceRHI;
		LandscapeParams.XYOffsetmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
	}
	else
	{
		LandscapeParams.XYOffsetmapTexture = GBlackTexture->TextureRHI;
		LandscapeParams.XYOffsetmapTextureSampler = GBlackTexture->SamplerStateRHI;
	}

	if (NormalmapTexture)
	{
		LandscapeParams.NormalmapTexture = NormalmapTexture->TextureReference.TextureReferenceRHI;
		LandscapeParams.NormalmapTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	}
	else
	{
		LandscapeParams.NormalmapTexture = GBlackTexture->TextureRHI;
		LandscapeParams.NormalmapTextureSampler = GBlackTexture->SamplerStateRHI;
	}

	LandscapeUniformShaderParameters.SetContents(LandscapeParams);

	if (bRegistered)
	{
		FVector4 OriginAndSphereRadius(GetBounds().Origin, GetBounds().SphereRadius);

		FLandscapeRenderSystem& RenderSystem = *LandscapeRenderSystems.FindChecked(LandscapeKey);
		RenderSystem.SetSectionOriginAndRadius(ComponentBase, OriginAndSphereRadius);
	}

	// Recache mesh draw commands for changed uniform buffers
	GetScene().UpdateCachedRenderStates(this);
}

/** Creates a mesh batch for virtual texture rendering. Will render a simple fixed grid with combined subsections. */
bool FLandscapeComponentSceneProxy::GetMeshElementForVirtualTexture(int32 InLodIndex, ERuntimeVirtualTextureMaterialType MaterialType, UMaterialInterface* InMaterialInterface, FMeshBatch& OutMeshBatch, TArray<FLandscapeBatchElementParams>& OutStaticBatchParamArray) const
{
	if (InMaterialInterface == nullptr)
	{
		return false;
	}

	OutMeshBatch.VertexFactory = FixedGridVertexFactory;
	OutMeshBatch.MaterialRenderProxy = InMaterialInterface->GetRenderProxy();
	OutMeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
	OutMeshBatch.CastShadow = false;
	OutMeshBatch.bUseForDepthPass = false;
	OutMeshBatch.bUseAsOccluder = false;
	OutMeshBatch.bUseForMaterial = false;
	OutMeshBatch.Type = PT_TriangleList;
	OutMeshBatch.DepthPriorityGroup = SDPG_World;
	OutMeshBatch.LODIndex = InLodIndex;
	OutMeshBatch.bDitheredLODTransition = false;
	OutMeshBatch.bRenderToVirtualTexture = true;
	OutMeshBatch.RuntimeVirtualTextureMaterialType = (uint32)MaterialType;

	OutMeshBatch.Elements.Empty(1);

	FLandscapeBatchElementParams* BatchElementParams = new(OutStaticBatchParamArray) FLandscapeBatchElementParams;
	BatchElementParams->SceneProxy = this;
	BatchElementParams->LandscapeUniformShaderParametersResource = &LandscapeUniformShaderParameters;
	BatchElementParams->FixedGridUniformShaderParameters = &LandscapeFixedGridUniformShaderParameters;
	BatchElementParams->CurrentLOD = InLodIndex;

	int32 LodSubsectionSizeVerts = SubsectionSizeVerts >> InLodIndex;

	FMeshBatchElement BatchElement;
	BatchElement.UserData = BatchElementParams;
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.IndexBuffer = SharedBuffers->IndexBuffers[InLodIndex];
	BatchElement.NumPrimitives = FMath::Square((LodSubsectionSizeVerts - 1)) * FMath::Square(NumSubsections) * 2;
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = SharedBuffers->IndexRanges[InLodIndex].MinIndexFull;
	BatchElement.MaxVertexIndex = SharedBuffers->IndexRanges[InLodIndex].MaxIndexFull;

	OutMeshBatch.Elements.Add(BatchElement);

	return true;
}

void FLandscapeComponentSceneProxy::ApplyWorldOffset(FVector InOffset)
{
	FPrimitiveSceneProxy::ApplyWorldOffset(InOffset);

	if (NumSubsections > 1)
	{
		for (int32 SubY = 0; SubY < NumSubsections; ++SubY)
		{
			for (int32 SubX = 0; SubX < NumSubsections; ++SubX)
			{
				int32 SubSectionIndex = SubX + SubY * NumSubsections;
				SubSectionScreenSizeTestingPosition[SubSectionIndex] += InOffset;
			}
		}
	}
}

template<class ArrayType>
bool FLandscapeComponentSceneProxy::GetStaticMeshElement(int32 LODIndex, bool bForToolMesh, bool bForcedLOD, FMeshBatch& MeshBatch, ArrayType& OutStaticBatchParamArray) const
{
	UMaterialInterface* MaterialInterface = nullptr;

	{
		int32 MaterialIndex = LODIndexToMaterialIndex[LODIndex];

		// Defaults to the material interface w/ potential tessellation
		MaterialInterface = AvailableMaterials[MaterialIndex];

		if (!MaterialInterface)
		{
			return false;
		}

		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AvailableMaterials[MaterialIndex]);
		bool HasTessellationEnabled = (GetScene().GetFeatureLevel() >= ERHIFeatureLevel::SM5) ? MaterialInstance != nullptr && RequiresAdjacencyInformation(MaterialInstance, VertexFactory->GetType(), GetScene().GetFeatureLevel()) && MaterialIndexToDisabledTessellationMaterial[MaterialIndex] != INDEX_NONE : false;

		if (HasTessellationEnabled)
		{
			{
				// Sanity check non-tessellated materials
				UMaterialInstance* NonTessellatedLandscapeMI = Cast<UMaterialInstance>(AvailableMaterials[MaterialIndexToDisabledTessellationMaterial[MaterialIndex]]);

				// Make sure that the Material instance we are going to use has the tessellation disabled
				UMaterialInstanceDynamic* NonTessellatedLandscapeMID = Cast<UMaterialInstanceDynamic>(NonTessellatedLandscapeMI);
				ULandscapeMaterialInstanceConstant* NonTessellatedLandscapeMIC = Cast<ULandscapeMaterialInstanceConstant>(NonTessellatedLandscapeMI);

				if (NonTessellatedLandscapeMID != nullptr)
				{
					NonTessellatedLandscapeMIC = Cast<ULandscapeMaterialInstanceConstant>(NonTessellatedLandscapeMID->Parent);
				}

				check(NonTessellatedLandscapeMIC != nullptr && NonTessellatedLandscapeMIC->bDisableTessellation);
			}

			float TessellationLODScreenSizeThreshold = LODIndex == 0 ? FLT_MAX : LODScreenRatioSquared[LODIndex];
			if (TessellationLODScreenSizeThreshold < TessellationComponentSquaredScreenSize || bForToolMesh)
			{
				// Selectively disable tessellation
				MaterialInterface = AvailableMaterials[MaterialIndexToDisabledTessellationMaterial[MaterialIndex]];
			}
		}
	}

	// Based on the final material we selected, detect if it has tessellation
	// Could be different from bRequiresAdjacencyInformation during shader compilation
	bool bCurrentRequiresAdjacencyInformation = RequiresAdjacencyInformation(MaterialInterface, VertexFactory->GetType(), GetScene().GetFeatureLevel());

	check(!bCurrentRequiresAdjacencyInformation || (bCurrentRequiresAdjacencyInformation && SharedBuffers->AdjacencyIndexBuffers));

	{
		MeshBatch.VertexFactory = VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();

		MeshBatch.LCI = ComponentLightInfo.Get();
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.CastShadow = bForToolMesh ? false : true;
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = ShouldUseAsOccluder() && GetScene().GetShadingPath() == EShadingPath::Deferred && !IsMovable();
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = bCurrentRequiresAdjacencyInformation ? PT_12_ControlPointPatchList : PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = LODIndex;
		MeshBatch.bDitheredLODTransition = false;

		// Combined batch element
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

		FLandscapeBatchElementParams* BatchElementParams = new(OutStaticBatchParamArray) FLandscapeBatchElementParams;
		BatchElementParams->LandscapeUniformShaderParametersResource = &LandscapeUniformShaderParameters;
		BatchElementParams->FixedGridUniformShaderParameters = &LandscapeFixedGridUniformShaderParameters;
		BatchElementParams->SceneProxy = this;
		BatchElementParams->CurrentLOD = LODIndex;

		BatchElement.UserData = BatchElementParams;
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.IndexBuffer = bCurrentRequiresAdjacencyInformation ? SharedBuffers->AdjacencyIndexBuffers->IndexBuffers[LODIndex] : SharedBuffers->IndexBuffers[LODIndex];
		BatchElement.NumPrimitives = FMath::Square((SubsectionSizeVerts >> LODIndex) - 1) * FMath::Square(NumSubsections) * 2;
		BatchElement.FirstIndex = 0;
		BatchElement.MinVertexIndex = SharedBuffers->IndexRanges[LODIndex].MinIndexFull;
		BatchElement.MaxVertexIndex = SharedBuffers->IndexRanges[LODIndex].MaxIndexFull;

		// The default is overridden here only by mobile landscape to punch holes in the geometry
		ApplyMeshElementModifier(BatchElement, LODIndex);
	}

	return true;
}

void FLandscapeComponentSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	if (AvailableMaterials.Num() == 0)
	{
		return;
	}

	int32 TotalBatchCount = 1 + LastLOD - FirstLOD;
	TotalBatchCount += (1 + LastVirtualTextureLOD - FirstVirtualTextureLOD) * RuntimeVirtualTextureMaterialTypes.Num();

	StaticBatchParamArray.Empty(TotalBatchCount);
	PDI->ReserveMemoryForMeshes(TotalBatchCount);

	// Add fixed grid mesh batches for runtime virtual texture usage
 	for (ERuntimeVirtualTextureMaterialType MaterialType : RuntimeVirtualTextureMaterialTypes)
 	{
		const int32 MaterialIndex = LODIndexToMaterialIndex[FirstLOD];

		for (int32 LODIndex = FirstVirtualTextureLOD; LODIndex <= LastVirtualTextureLOD; ++LODIndex)
		{
			FMeshBatch RuntimeVirtualTextureMeshBatch;
			if (GetMeshElementForVirtualTexture(LODIndex, MaterialType, AvailableMaterials[MaterialIndex], RuntimeVirtualTextureMeshBatch, StaticBatchParamArray))
			{
				PDI->DrawMesh(RuntimeVirtualTextureMeshBatch, FLT_MAX);
			}
		}
 	}

	for (int32 LODIndex = FirstLOD; LODIndex <= LastLOD; LODIndex++)
	{
		FMeshBatch MeshBatch;

		if (GetStaticMeshElement(LODIndex, false, false, MeshBatch, StaticBatchParamArray))
		{
			PDI->DrawMesh(MeshBatch, LODIndex == FirstLOD ? FLT_MAX : (FMath::Sqrt(LODScreenRatioSquared[LODIndex]) * 2.0f));
		}
	}

	check(StaticBatchParamArray.Num() <= TotalBatchCount);
}

int8 FLandscapeComponentSceneProxy::GetLODFromScreenSize(float InScreenSizeSquared, float InViewLODScale) const
{
	float FractionalLOD;

	return FLandscapeRenderSystem::GetLODFromScreenSize(LODSettings, InScreenSizeSquared, InViewLODScale, FractionalLOD);
}

namespace
{
	FLinearColor GetColorForLod(int32 CurrentLOD, int32 ForcedLOD, bool DisplayCombinedBatch)
	{
		int32 ColorIndex = INDEX_NONE;
		if (GEngine->LODColorationColors.Num() > 0)
		{
			ColorIndex = CurrentLOD;
			ColorIndex = FMath::Clamp(ColorIndex, 0, GEngine->LODColorationColors.Num() - 1);
		}
		const FLinearColor& LODColor = ColorIndex != INDEX_NONE ? GEngine->LODColorationColors[ColorIndex] : FLinearColor::Gray;

		if (ForcedLOD >= 0)
		{
			return LODColor;
		}

		if (DisplayCombinedBatch)
		{
			return LODColor * 0.2f;
		}

		return LODColor * 0.1f;
	}
}

void FLandscapeComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FLandscapeComponentSceneProxy_GetMeshElements);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeDynamicDrawTime);

	int32 NumPasses = 0;
	int32 NumTriangles = 0;
	int32 NumDrawCalls = 0;
	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			FLandscapeElementParamArray& ParameterArray = Collector.AllocateOneFrameResource<FLandscapeElementParamArray>();
			ParameterArray.ElementParams.AddDefaulted(1);

			const FSceneView* View = Views[ViewIndex];

			int32 ForcedLODLevel = ForcedLOD;

			const int32 ViewLodOverride = GetViewLodOverride(*View);
			ForcedLODLevel = ViewLodOverride >= 0 ? ViewLodOverride : ForcedLODLevel;

#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			const int32 DrawCollisionLodOverride = GetDrawCollisionLodOverride(*View, CollisionResponse, CollisionMipLevel, SimpleCollisionMipLevel);
			ForcedLODLevel = DrawCollisionLodOverride >= 0 ? DrawCollisionLodOverride : ForcedLODLevel;
#endif

			ForcedLODLevel = FMath::Min(ForcedLODLevel, (int32)LODSettings.LastLODIndex);

			const float LODScale = View->LODDistanceFactor * CVarStaticMeshLODDistanceScale.GetValueOnRenderThread();
			const float MeshScreenSizeSquared = ComputeBoundsScreenRadiusSquared(GetBounds().Origin, GetBounds().SphereRadius, *View);
			int32 LODToRender = ForcedLODLevel >= 0 ? ForcedLODLevel : GetLODFromScreenSize(MeshScreenSizeSquared, LODScale * LODScale);

#if PLATFORM_SUPPORTS_LANDSCAPE_VISUAL_MESH_LOD_STREAMING
			LODToRender = FMath::Max<int32>(LODToRender, GetCurrentFirstLODIdx_RenderThread());
#endif
			FMeshBatch& Mesh = Collector.AllocateMesh();
			GetStaticMeshElement(LODToRender, false, ForcedLODLevel >= 0, Mesh, ParameterArray.ElementParams);

#if WITH_EDITOR
			FMeshBatch& MeshTools = Collector.AllocateMesh();
			// No Tessellation on tool material
			GetStaticMeshElement(LODToRender, true, ForcedLODLevel >= 0, MeshTools, ParameterArray.ElementParams);
#endif

			// Render the landscape component
#if WITH_EDITOR
			switch (GLandscapeViewMode)
			{
			case ELandscapeViewMode::DebugLayer:
			{
				if (GLayerDebugColorMaterial)
				{
					auto DebugColorMaterialInstance = new FLandscapeDebugMaterialRenderProxy(GLayerDebugColorMaterial->GetRenderProxy(),
						(EditToolRenderData.DebugChannelR >= 0 ? WeightmapTextures[EditToolRenderData.DebugChannelR / 4] : nullptr),
						(EditToolRenderData.DebugChannelG >= 0 ? WeightmapTextures[EditToolRenderData.DebugChannelG / 4] : nullptr),
						(EditToolRenderData.DebugChannelB >= 0 ? WeightmapTextures[EditToolRenderData.DebugChannelB / 4] : nullptr),
						(EditToolRenderData.DebugChannelR >= 0 ? DebugColorMask::Masks[EditToolRenderData.DebugChannelR % 4] : DebugColorMask::Masks[4]),
						(EditToolRenderData.DebugChannelG >= 0 ? DebugColorMask::Masks[EditToolRenderData.DebugChannelG % 4] : DebugColorMask::Masks[4]),
						(EditToolRenderData.DebugChannelB >= 0 ? DebugColorMask::Masks[EditToolRenderData.DebugChannelB % 4] : DebugColorMask::Masks[4])
					);

					MeshTools.MaterialRenderProxy = DebugColorMaterialInstance;
					Collector.RegisterOneFrameMaterialProxy(DebugColorMaterialInstance);

					MeshTools.bCanApplyViewModeOverrides = true;
					MeshTools.bUseWireframeSelectionColoring = IsSelected();

					Collector.AddMesh(ViewIndex, MeshTools);

					NumPasses++;
					NumTriangles += MeshTools.GetNumPrimitives();
					NumDrawCalls += MeshTools.Elements.Num();
				}
			}
			break;

			case ELandscapeViewMode::LayerDensity:
			{
				int32 ColorIndex = FMath::Min<int32>(NumWeightmapLayerAllocations, GEngine->ShaderComplexityColors.Num());
				auto LayerDensityMaterialInstance = new FColoredMaterialRenderProxy(GEngine->LevelColorationUnlitMaterial->GetRenderProxy(), ColorIndex ? GEngine->ShaderComplexityColors[ColorIndex - 1] : FLinearColor::Black);

				MeshTools.MaterialRenderProxy = LayerDensityMaterialInstance;
				Collector.RegisterOneFrameMaterialProxy(LayerDensityMaterialInstance);

				MeshTools.bCanApplyViewModeOverrides = true;
				MeshTools.bUseWireframeSelectionColoring = IsSelected();

				Collector.AddMesh(ViewIndex, MeshTools);

				NumPasses++;
				NumTriangles += MeshTools.GetNumPrimitives();
				NumDrawCalls += MeshTools.Elements.Num();
			}
			break;

			case ELandscapeViewMode::LayerUsage:
			{
				if (GLandscapeLayerUsageMaterial)
				{
					float Rotation = ((SectionBase.X / ComponentSizeQuads) ^ (SectionBase.Y / ComponentSizeQuads)) & 1 ? 0 : 2.f * PI;
					auto LayerUsageMaterialInstance = new FLandscapeLayerUsageRenderProxy(GLandscapeLayerUsageMaterial->GetRenderProxy(), ComponentSizeVerts, LayerColors, Rotation);
					MeshTools.MaterialRenderProxy = LayerUsageMaterialInstance;
					Collector.RegisterOneFrameMaterialProxy(LayerUsageMaterialInstance);
					MeshTools.bCanApplyViewModeOverrides = true;
					MeshTools.bUseWireframeSelectionColoring = IsSelected();
					Collector.AddMesh(ViewIndex, MeshTools);
					NumPasses++;
					NumTriangles += MeshTools.GetNumPrimitives();
					NumDrawCalls += MeshTools.Elements.Num();
				}
			}
			break;

			case ELandscapeViewMode::LOD:
			{

				const bool bMaterialModifiesMeshPosition = Mesh.MaterialRenderProxy->GetMaterial(View->GetFeatureLevel())->MaterialModifiesMeshPosition_RenderThread();

				auto& TemplateMesh = bIsWireframe ? Mesh : MeshTools;
				for (int32 i = 0; i < TemplateMesh.Elements.Num(); i++)
				{
					FMeshBatch& LODMesh = Collector.AllocateMesh();
					LODMesh = TemplateMesh;
					LODMesh.Elements.Empty(1);
					LODMesh.Elements.Add(TemplateMesh.Elements[i]);
					int32 CurrentLOD = ((FLandscapeBatchElementParams*)TemplateMesh.Elements[i].UserData)->CurrentLOD;
					LODMesh.VisualizeLODIndex = CurrentLOD;
					FLinearColor Color = GetColorForLod(CurrentLOD, ForcedLOD, true);
					FMaterialRenderProxy* LODMaterialProxy = (FMaterialRenderProxy*)new FColoredMaterialRenderProxy(GEngine->LevelColorationUnlitMaterial->GetRenderProxy(), Color);
					Collector.RegisterOneFrameMaterialProxy(LODMaterialProxy);
					LODMesh.MaterialRenderProxy = LODMaterialProxy;
					LODMesh.bCanApplyViewModeOverrides = !bIsWireframe;
					LODMesh.bWireframe = bIsWireframe;
					LODMesh.bUseWireframeSelectionColoring = IsSelected();
					Collector.AddMesh(ViewIndex, LODMesh);

					NumTriangles += TemplateMesh.Elements[i].NumPrimitives;
					NumDrawCalls++;
				}
				NumPasses++;

			}
			break;

			case ELandscapeViewMode::WireframeOnTop:
			{
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);
				NumPasses++;
				NumTriangles += Mesh.GetNumPrimitives();
				NumDrawCalls += Mesh.Elements.Num();

				// wireframe on top
				FMeshBatch& WireMesh = Collector.AllocateMesh();
				WireMesh = MeshTools;
				auto WireMaterialInstance = new FColoredMaterialRenderProxy(GEngine->LevelColorationUnlitMaterial->GetRenderProxy(), FLinearColor(0, 0, 1));
				WireMesh.MaterialRenderProxy = WireMaterialInstance;
				Collector.RegisterOneFrameMaterialProxy(WireMaterialInstance);
				WireMesh.bCanApplyViewModeOverrides = false;
				WireMesh.bWireframe = true;
				Collector.AddMesh(ViewIndex, WireMesh);
				NumPasses++;
				NumTriangles += WireMesh.GetNumPrimitives();
				NumDrawCalls++;
			}
			break;

			case ELandscapeViewMode::LayerContribution:
			{
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);
				NumPasses++;
				NumTriangles += Mesh.GetNumPrimitives();
				NumDrawCalls += Mesh.Elements.Num();

				FMeshBatch& MaskMesh = Collector.AllocateMesh();
				MaskMesh = MeshTools;
				auto ColorMaskMaterialInstance = new FLandscapeMaskMaterialRenderProxy(GColorMaskRegionMaterial->GetRenderProxy(), EditToolRenderData.LayerContributionTexture ? EditToolRenderData.LayerContributionTexture : GLandscapeBlackTexture, true);
				MaskMesh.MaterialRenderProxy = ColorMaskMaterialInstance;
				Collector.RegisterOneFrameMaterialProxy(ColorMaskMaterialInstance);
				Collector.AddMesh(ViewIndex, MaskMesh);
				NumPasses++;
				NumTriangles += MaskMesh.GetNumPrimitives();
				NumDrawCalls += MaskMesh.Elements.Num();
			}
			break;

			default:

#endif // WITH_EDITOR

#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				const bool bInCollisionView = View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn;
				if (AllowDebugViewmodes() && bInCollisionView)
				{
					const bool bDrawSimpleCollision = View->Family->EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore;
					const bool bDrawComplexCollision = View->Family->EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore;
					if (bDrawSimpleCollision || bDrawComplexCollision)
					{
						// Override the mesh's material with our material that draws the collision color
						auto CollisionMaterialInstance = new FColoredMaterialRenderProxy(
							GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
							GetWireframeColor()
						);
						Collector.RegisterOneFrameMaterialProxy(CollisionMaterialInstance);

						Mesh.MaterialRenderProxy = CollisionMaterialInstance;
						Mesh.bCanApplyViewModeOverrides = true;
						Mesh.bUseWireframeSelectionColoring = IsSelected();

						Collector.AddMesh(ViewIndex, Mesh);

						NumPasses++;
						NumTriangles += Mesh.GetNumPrimitives();
						NumDrawCalls += Mesh.Elements.Num();
					}
				}
#if WITH_EDITOR
				else if (CVarLandscapeShowDirty.GetValueOnRenderThread() && GLandscapeDirtyMaterial)
				{
					Mesh.bCanApplyViewModeOverrides = false;
					Collector.AddMesh(ViewIndex, Mesh);
					NumPasses++;
					NumTriangles += Mesh.GetNumPrimitives();
					NumDrawCalls += Mesh.Elements.Num();

					FMeshBatch& MaskMesh = Collector.AllocateMesh();
					MaskMesh = MeshTools;
										
					auto DirtyMaterialInstance = new FLandscapeMaskMaterialRenderProxy(GLandscapeDirtyMaterial->GetRenderProxy(), EditToolRenderData.DirtyTexture ? EditToolRenderData.DirtyTexture : GLandscapeBlackTexture, true);
					MaskMesh.MaterialRenderProxy = DirtyMaterialInstance;
					Collector.RegisterOneFrameMaterialProxy(DirtyMaterialInstance);
					Collector.AddMesh(ViewIndex, MaskMesh);
					NumPasses++;
					NumTriangles += MaskMesh.GetNumPrimitives();
					NumDrawCalls += MaskMesh.Elements.Num();
				}
#endif
				else
#endif
					// Regular Landscape rendering. Only use the dynamic path if we're rendering a rich view or we've disabled the static path for debugging.
					if (IsRichView(ViewFamily) ||
						GLandscapeDebugOptions.bDisableStatic ||
						bIsWireframe ||
#if WITH_EDITOR
						(IsSelected() && !GLandscapeEditModeActive) ||
						(GetViewLodOverride(*View) >= 0)
#else
						IsSelected()
#endif
						)
					{
						Mesh.bCanApplyViewModeOverrides = true;
						Mesh.bUseWireframeSelectionColoring = IsSelected();

						Collector.AddMesh(ViewIndex, Mesh);

						NumPasses++;
						NumTriangles += Mesh.GetNumPrimitives();
						NumDrawCalls += Mesh.Elements.Num();
					}

#if WITH_EDITOR
			} // switch
#endif

#if WITH_EDITOR
			  // Extra render passes for landscape tools
			if (GLandscapeEditModeActive)
			{
				// Region selection
				if (EditToolRenderData.SelectedType)
				{
					if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::SelectRegion) && (EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_REGION)
						&& !(GLandscapeEditRenderMode & ELandscapeEditRenderMode::Mask))
					{
						FMeshBatch& SelectMesh = Collector.AllocateMesh();
						SelectMesh = MeshTools;
						auto SelectMaterialInstance = new FLandscapeSelectMaterialRenderProxy(GSelectionRegionMaterial->GetRenderProxy(), EditToolRenderData.DataTexture ? EditToolRenderData.DataTexture : GLandscapeBlackTexture);
						SelectMesh.MaterialRenderProxy = SelectMaterialInstance;
						Collector.RegisterOneFrameMaterialProxy(SelectMaterialInstance);
						Collector.AddMesh(ViewIndex, SelectMesh);
						NumPasses++;
						NumTriangles += SelectMesh.GetNumPrimitives();
						NumDrawCalls += SelectMesh.Elements.Num();
					}

					if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::SelectComponent) && (EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_COMPONENT))
					{
						FMeshBatch& SelectMesh = Collector.AllocateMesh();
						SelectMesh = MeshTools;
						SelectMesh.MaterialRenderProxy = GSelectionColorMaterial->GetRenderProxy();
						Collector.AddMesh(ViewIndex, SelectMesh);
						NumPasses++;
						NumTriangles += SelectMesh.GetNumPrimitives();
						NumDrawCalls += SelectMesh.Elements.Num();
					}
				}

				// Mask
				if ((GLandscapeEditRenderMode & ELandscapeEditRenderMode::SelectRegion) && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Mask))
				{
					if (EditToolRenderData.SelectedType & FLandscapeEditToolRenderData::ST_REGION)
					{
						FMeshBatch& MaskMesh = Collector.AllocateMesh();
						MaskMesh = MeshTools;
						auto MaskMaterialInstance = new FLandscapeMaskMaterialRenderProxy(GMaskRegionMaterial->GetRenderProxy(), EditToolRenderData.DataTexture ? EditToolRenderData.DataTexture : GLandscapeBlackTexture, !!(GLandscapeEditRenderMode & ELandscapeEditRenderMode::InvertedMask));
						MaskMesh.MaterialRenderProxy = MaskMaterialInstance;
						Collector.RegisterOneFrameMaterialProxy(MaskMaterialInstance);
						Collector.AddMesh(ViewIndex, MaskMesh);
						NumPasses++;
						NumTriangles += MaskMesh.GetNumPrimitives();
						NumDrawCalls += MaskMesh.Elements.Num();
					}
					else if (!(GLandscapeEditRenderMode & ELandscapeEditRenderMode::InvertedMask))
					{
						FMeshBatch& MaskMesh = Collector.AllocateMesh();
						MaskMesh = MeshTools;
						auto MaskMaterialInstance = new FLandscapeMaskMaterialRenderProxy(GMaskRegionMaterial->GetRenderProxy(), GLandscapeBlackTexture, false);
						MaskMesh.MaterialRenderProxy = MaskMaterialInstance;
						Collector.RegisterOneFrameMaterialProxy(MaskMaterialInstance);
						Collector.AddMesh(ViewIndex, MaskMesh);
						NumPasses++;
						NumTriangles += MaskMesh.GetNumPrimitives();
						NumDrawCalls += MaskMesh.Elements.Num();
					}
				}

				// Edit mode tools
				if (EditToolRenderData.ToolMaterial)
				{
					FMeshBatch& EditMesh = Collector.AllocateMesh();
					EditMesh = MeshTools;
					EditMesh.MaterialRenderProxy = EditToolRenderData.ToolMaterial->GetRenderProxy();
					Collector.AddMesh(ViewIndex, EditMesh);
					NumPasses++;
					NumTriangles += EditMesh.GetNumPrimitives();
					NumDrawCalls += EditMesh.Elements.Num();
				}

				if (EditToolRenderData.GizmoMaterial && GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo)
				{
					FMeshBatch& EditMesh = Collector.AllocateMesh();
					EditMesh = MeshTools;
					EditMesh.MaterialRenderProxy = EditToolRenderData.GizmoMaterial->GetRenderProxy();
					Collector.AddMesh(ViewIndex, EditMesh);
					NumPasses++;
					NumTriangles += EditMesh.GetNumPrimitives();
					NumDrawCalls += EditMesh.Elements.Num();
				}
			}
#endif // WITH_EDITOR

			if (GLandscapeDebugOptions.bShowPatches)
			{
				DrawWireBox(Collector.GetPDI(ViewIndex), GetBounds().GetBox(), FColor(255, 255, 0), SDPG_World);
			}

			if (ViewFamily.EngineShowFlags.Bounds)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_LandscapeComponentRenderPasses, NumPasses);
	INC_DWORD_STAT_BY(STAT_LandscapeDrawCalls, NumDrawCalls);
	INC_DWORD_STAT_BY(STAT_LandscapeTriangles, NumTriangles * NumPasses);
}

#if RHI_RAYTRACING
void FLandscapeComponentSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	if (!bRegistered || !CVarRayTracingLandscape.GetValueOnRenderThread())
	{
		return;
	}

	int32 ForcedLODLevel = ForcedLOD;

	int32 ViewLodOveride = GetViewLodOverride(*Context.ReferenceView);
	ForcedLODLevel = ViewLodOveride >= 0 ? ViewLodOveride : ForcedLODLevel;

	float MeshScreenSizeSquared = ComputeBoundsScreenRadiusSquared(GetBounds().Origin, GetBounds().SphereRadius, *Context.ReferenceView);
	float LODScale = Context.ReferenceView->LODDistanceFactor * CVarStaticMeshLODDistanceScale.GetValueOnRenderThread();
	int32 LODToRender = ForcedLODLevel >= 0 ? ForcedLODLevel : GetLODFromScreenSize(MeshScreenSizeSquared, LODScale * LODScale);
	
	FLandscapeElementParamArray& ParameterArray = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FLandscapeElementParamArray>();
	ParameterArray.ElementParams.AddDefaulted(NumSubsections * NumSubsections);

	if (AvailableMaterials.Num() == 0)
	{
		return;
	}

	const int8 CurrentLODIndex = LODToRender;
	int8 MaterialIndex = LODIndexToMaterialIndex.IsValidIndex(CurrentLODIndex) ? LODIndexToMaterialIndex[CurrentLODIndex] : INDEX_NONE;
	UMaterialInterface* SelectedMaterial = MaterialIndex != INDEX_NONE ? AvailableMaterials[MaterialIndex] : nullptr;

	// this is really not normal that we have no material at this point, so do not continue
	if (SelectedMaterial == nullptr)
	{
		return;
	}

	FMeshBatch BaseMeshBatch;
	BaseMeshBatch.VertexFactory = VertexFactory;
	BaseMeshBatch.MaterialRenderProxy = SelectedMaterial->GetRenderProxy();
	BaseMeshBatch.LCI = ComponentLightInfo.Get();
	BaseMeshBatch.CastShadow = true;
	BaseMeshBatch.CastRayTracedShadow = true;
	BaseMeshBatch.bUseForMaterial = true;
	BaseMeshBatch.SegmentIndex = 0;

	BaseMeshBatch.Elements.Empty();

	FLandscapeRenderSystem& RenderSystem = *LandscapeRenderSystems.FindChecked(LandscapeKey);

	for (int32 SubY = 0; SubY < NumSubsections; SubY++)
	{
		for (int32 SubX = 0; SubX < NumSubsections; SubX++)
		{
			const int8 SubSectionIdx = SubX + SubY * NumSubsections;
			const int8 CurrentLOD = LODToRender;

			FMeshBatch MeshBatch = BaseMeshBatch;

			FMeshBatchElement BatchElement;
			FLandscapeBatchElementParams& BatchElementParams = ParameterArray.ElementParams[SubSectionIdx];

			BatchElementParams.LandscapeUniformShaderParametersResource = &LandscapeUniformShaderParameters;
			BatchElementParams.FixedGridUniformShaderParameters = &LandscapeFixedGridUniformShaderParameters;
			BatchElementParams.SceneProxy = this;
			BatchElementParams.CurrentLOD = CurrentLOD;
			BatchElement.UserData = &BatchElementParams;
			BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

			int32 LodSubsectionSizeVerts = SubsectionSizeVerts >> CurrentLOD;

			if (LodSubsectionSizeVerts <= 0)
			{
				continue;
			}

			uint32 NumPrimitives = FMath::Square(LodSubsectionSizeVerts - 1) * 2;

			BatchElement.IndexBuffer = SharedBuffers->ZeroOffsetIndexBuffers[CurrentLOD];
			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = NumPrimitives;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = 0;

			MeshBatch.Elements.Add(BatchElement);

			SectionRayTracingStates[SubSectionIdx].Geometry.Initializer.IndexBuffer = BatchElement.IndexBuffer->IndexBufferRHI;

			BatchElementParams.LandscapeVertexFactoryMVFUniformBuffer = SectionRayTracingStates[SubSectionIdx].UniformBuffer;

			bool bNeedsRayTracingGeometryUpdate = false;

			// Detect force update CVar
			bNeedsRayTracingGeometryUpdate |= (CurrentLOD <= GLandscapeRayTracingGeometryLODsThatUpdateEveryFrame) ? true : false;

			// Detect continuous LOD parameter changes. This is for far-away high LODs - they change rarely yet the BLAS refit time is not ideal, even if they contains tiny amount of triangles
			{
				if (SectionRayTracingStates[SubSectionIdx].CurrentLOD != CurrentLOD)
				{
					bNeedsRayTracingGeometryUpdate = true;
					SectionRayTracingStates[SubSectionIdx].CurrentLOD = CurrentLOD;
					SectionRayTracingStates[SubSectionIdx].RayTracingDynamicVertexBuffer.Release();
				}
				if (SectionRayTracingStates[SubSectionIdx].HeightmapLODBias != RenderSystem.GetSectionLODBias(ComponentBase))
				{
					bNeedsRayTracingGeometryUpdate = true;
					SectionRayTracingStates[SubSectionIdx].HeightmapLODBias = RenderSystem.GetSectionLODBias(ComponentBase);
				}

				if (SectionRayTracingStates[SubSectionIdx].FractionalLOD != RenderSystem.GetSectionLODValue(ComponentBase))
				{
					bNeedsRayTracingGeometryUpdate = true;
					SectionRayTracingStates[SubSectionIdx].FractionalLOD = RenderSystem.GetSectionLODValue(ComponentBase);
				}
			}

			if (GLandscapeRayTracingGeometryDetectTextureStreaming > 0)
			{
				const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
				const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(((FSceneInterface*)Context.Scene)->GetFeatureLevel(), FallbackMaterialRenderProxyPtr);

				if (Material.HasVertexPositionOffsetConnected())
				{
					const FMaterialRenderProxy* MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? FallbackMaterialRenderProxyPtr : MeshBatch.MaterialRenderProxy;

					FMaterialRenderContext MaterialRenderContext(MaterialRenderProxy, Material, Context.ReferenceView);

					const FUniformExpressionSet& UniformExpressionSet = Material.GetRenderingThreadShaderMap()->GetUniformExpressionSet();
					const uint32 Hash = UniformExpressionSet.GetReferencedTexture2DRHIHash(MaterialRenderContext);

					if (SectionRayTracingStates[SubSectionIdx].ReferencedTextureRHIHash != Hash)
					{
						bNeedsRayTracingGeometryUpdate = true;
						SectionRayTracingStates[SubSectionIdx].ReferencedTextureRHIHash = Hash;
					}
				}
			}

			FRayTracingInstance RayTracingInstance;
			RayTracingInstance.Geometry = &SectionRayTracingStates[SubSectionIdx].Geometry;
			RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);
			RayTracingInstance.Materials.Add(MeshBatch);
			RayTracingInstance.BuildInstanceMaskAndFlags();
			OutRayTracingInstances.Add(RayTracingInstance);

			if (bNeedsRayTracingGeometryUpdate)
			{
				// Use the internal managed vertex buffer because landscape dynamic RT geometries are not updated every frame
				// which is a requirement for the shared vertex buffer usage

				Context.DynamicRayTracingGeometriesToUpdate.Add(
					FRayTracingDynamicGeometryUpdateParams
					{
						RayTracingInstance.Materials,
						false,
						(uint32)FMath::Square(LodSubsectionSizeVerts),
						FMath::Square(LodSubsectionSizeVerts) * (uint32)sizeof(FVector),
						(uint32)FMath::Square(LodSubsectionSizeVerts - 1) * 2,
						&SectionRayTracingStates[SubSectionIdx].Geometry,
						&SectionRayTracingStates[SubSectionIdx].RayTracingDynamicVertexBuffer,
						true
					}
				);
			}
		}
	}
}
#endif

int32 FLandscapeComponentSceneProxy::CollectOccluderElements(FOccluderElementsCollector& Collector) const
{
	// TODO: implement
	return 0;
}

//
// FLandscapeVertexBuffer
//

/**
* Initialize the RHI for this rendering resource
*/
void FLandscapeVertexBuffer::InitRHI()
{
	SCOPED_LOADTIMER(FLandscapeVertexBuffer_InitRHI);

	// create a static vertex buffer
	FRHIResourceCreateInfo CreateInfo;
	void* BufferData = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(NumVertices * sizeof(FLandscapeVertex), BUF_Static, CreateInfo, BufferData);
	FLandscapeVertex* Vertex = (FLandscapeVertex*)BufferData;
	int32 VertexIndex = 0;
	for (int32 SubY = 0; SubY < NumSubsections; SubY++)
	{
		for (int32 SubX = 0; SubX < NumSubsections; SubX++)
		{
			for (int32 y = 0; y < SubsectionSizeVerts; y++)
			{
				for (int32 x = 0; x < SubsectionSizeVerts; x++)
				{
					Vertex->VertexX = x;
					Vertex->VertexY = y;
					Vertex->SubX = SubX;
					Vertex->SubY = SubY;
					Vertex++;
					VertexIndex++;
				}
			}
		}
	}
	check(NumVertices == VertexIndex);
	RHIUnlockVertexBuffer(VertexBufferRHI);
}

//
// FLandscapeSharedBuffers
//

template <typename INDEX_TYPE>
void FLandscapeSharedBuffers::CreateIndexBuffers(ERHIFeatureLevel::Type InFeatureLevel, bool bRequiresAdjacencyInformation)
{
	if (InFeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		if (!bVertexScoresComputed)
		{
			bVertexScoresComputed = ComputeVertexScores();
		}
	}

	TArray<INDEX_TYPE> VertexToIndexMap;
	VertexToIndexMap.AddUninitialized(FMath::Square(SubsectionSizeVerts * NumSubsections));
	FMemory::Memset(VertexToIndexMap.GetData(), 0xff, NumVertices * sizeof(INDEX_TYPE));

	INDEX_TYPE VertexCount = 0;
	int32 SubsectionSizeQuads = SubsectionSizeVerts - 1;

	// Layout index buffer to determine best vertex order
	int32 MaxLOD = NumIndexBuffers - 1;
	for (int32 Mip = MaxLOD; Mip >= 0; Mip--)
	{
		int32 LodSubsectionSizeQuads = (SubsectionSizeVerts >> Mip) - 1;

		TArray<INDEX_TYPE> NewIndices;
		int32 ExpectedNumIndices = FMath::Square(NumSubsections) * FMath::Square(LodSubsectionSizeQuads) * 6;
		NewIndices.Empty(ExpectedNumIndices);

		int32& MaxIndexFull = IndexRanges[Mip].MaxIndexFull;
		int32& MinIndexFull = IndexRanges[Mip].MinIndexFull;
		MaxIndexFull = 0;
		MinIndexFull = MAX_int32;

		if (InFeatureLevel <= ERHIFeatureLevel::ES3_1)
		{
			// mobile version shares vertices across LODs to save memory
			float MipRatio = (float)SubsectionSizeQuads / (float)LodSubsectionSizeQuads; // Morph current MIP to base MIP

			for (int32 SubY = 0; SubY < NumSubsections; SubY++)
			{
				for (int32 SubX = 0; SubX < NumSubsections; SubX++)
				{
					TArray<INDEX_TYPE> SubIndices;
					SubIndices.Empty(FMath::Square(LodSubsectionSizeQuads) * 6);

					int32& MaxIndex = IndexRanges[Mip].MaxIndex[SubX][SubY];
					int32& MinIndex = IndexRanges[Mip].MinIndex[SubX][SubY];
					MaxIndex = 0;
					MinIndex = MAX_int32;

					for (int32 Y = 0; Y < LodSubsectionSizeQuads; Y++)
					{
						for (int32 X = 0; X < LodSubsectionSizeQuads; X++)
						{
							INDEX_TYPE QuadIndices[4];

							for (int32 CornerId = 0; CornerId < 4; CornerId++)
							{
								const int32 CornerX = FMath::RoundToInt((float)(X + (CornerId & 1)) * MipRatio);
								const int32 CornerY = FMath::RoundToInt((float)(Y + (CornerId >> 1)) * MipRatio);
								const FLandscapeVertexRef VertexRef(CornerX, CornerY, SubX, SubY);

								const INDEX_TYPE VertexIndex = FLandscapeVertexRef::GetVertexIndex(VertexRef, NumSubsections, SubsectionSizeVerts);
								if (VertexToIndexMap[VertexIndex] == INDEX_TYPE(-1))
								{
									VertexToIndexMap[VertexIndex] = QuadIndices[CornerId] = VertexCount++;
								}
								else
								{
									QuadIndices[CornerId] = VertexToIndexMap[VertexIndex];
								}

								// update the min/max index ranges
								MaxIndex = FMath::Max<int32>(MaxIndex, QuadIndices[CornerId]);
								MinIndex = FMath::Min<int32>(MinIndex, QuadIndices[CornerId]);
							}

							SubIndices.Add(QuadIndices[0]);
							SubIndices.Add(QuadIndices[3]);
							SubIndices.Add(QuadIndices[1]);

							SubIndices.Add(QuadIndices[0]);
							SubIndices.Add(QuadIndices[2]);
							SubIndices.Add(QuadIndices[3]);
						}
					}

					// update min/max for full subsection
					MaxIndexFull = FMath::Max<int32>(MaxIndexFull, MaxIndex);
					MinIndexFull = FMath::Min<int32>(MinIndexFull, MinIndex);

					TArray<INDEX_TYPE> NewSubIndices;
					::OptimizeFaces<INDEX_TYPE>(SubIndices, NewSubIndices, 32);
					NewIndices.Append(NewSubIndices);
				}
			}
		}
		else
		{
			// non-mobile version
			int32 SubOffset = 0;
			for (int32 SubY = 0; SubY < NumSubsections; SubY++)
			{
				for (int32 SubX = 0; SubX < NumSubsections; SubX++)
				{
					int32& MaxIndex = IndexRanges[Mip].MaxIndex[SubX][SubY];
					int32& MinIndex = IndexRanges[Mip].MinIndex[SubX][SubY];
					MaxIndex = 0;
					MinIndex = MAX_int32;

					for (int32 y = 0; y < LodSubsectionSizeQuads; y++)
					{
						for (int32 x = 0; x < LodSubsectionSizeQuads; x++)
						{
							INDEX_TYPE i00 = (x + 0) + (y + 0) * SubsectionSizeVerts + SubOffset;
							INDEX_TYPE i10 = (x + 1) + (y + 0) * SubsectionSizeVerts + SubOffset;
							INDEX_TYPE i11 = (x + 1) + (y + 1) * SubsectionSizeVerts + SubOffset;
							INDEX_TYPE i01 = (x + 0) + (y + 1) * SubsectionSizeVerts + SubOffset;

							NewIndices.Add(i00);
							NewIndices.Add(i11);
							NewIndices.Add(i10);

							NewIndices.Add(i00);
							NewIndices.Add(i01);
							NewIndices.Add(i11);

							// Update the min/max index ranges
							MaxIndex = FMath::Max<int32>(MaxIndex, i00);
							MinIndex = FMath::Min<int32>(MinIndex, i00);
							MaxIndex = FMath::Max<int32>(MaxIndex, i10);
							MinIndex = FMath::Min<int32>(MinIndex, i10);
							MaxIndex = FMath::Max<int32>(MaxIndex, i11);
							MinIndex = FMath::Min<int32>(MinIndex, i11);
							MaxIndex = FMath::Max<int32>(MaxIndex, i01);
							MinIndex = FMath::Min<int32>(MinIndex, i01);
						}
					}

					// update min/max for full subsection
					MaxIndexFull = FMath::Max<int32>(MaxIndexFull, MaxIndex);
					MinIndexFull = FMath::Min<int32>(MinIndexFull, MinIndex);

					SubOffset += FMath::Square(SubsectionSizeVerts);
				}
			}

			check(MinIndexFull <= (uint32)((INDEX_TYPE)(~(INDEX_TYPE)0)));
			check(NewIndices.Num() == ExpectedNumIndices);
		}

		// Create and init new index buffer with index data
		FRawStaticIndexBuffer16or32<INDEX_TYPE>* IndexBuffer = (FRawStaticIndexBuffer16or32<INDEX_TYPE>*)IndexBuffers[Mip];
		if (!IndexBuffer)
		{
			IndexBuffer = new FRawStaticIndexBuffer16or32<INDEX_TYPE>(false);
		}
		IndexBuffer->AssignNewBuffer(NewIndices);

		// Delay init resource to keep CPU data until create AdjacencyIndexbuffers
		if (!bRequiresAdjacencyInformation)
		{
			IndexBuffer->InitResource();
		}

		IndexBuffers[Mip] = IndexBuffer;

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			TArray<INDEX_TYPE> ZeroOffsetIndices;

			for (int32 y = 0; y < LodSubsectionSizeQuads; y++)
			{
				for (int32 x = 0; x < LodSubsectionSizeQuads; x++)
				{
					INDEX_TYPE i00 = (x + 0) + (y + 0) * (SubsectionSizeVerts >> Mip);
					INDEX_TYPE i10 = (x + 1) + (y + 0) * (SubsectionSizeVerts >> Mip);
					INDEX_TYPE i11 = (x + 1) + (y + 1) * (SubsectionSizeVerts >> Mip);
					INDEX_TYPE i01 = (x + 0) + (y + 1) * (SubsectionSizeVerts >> Mip);

					ZeroOffsetIndices.Add(i00);
					ZeroOffsetIndices.Add(i11);
					ZeroOffsetIndices.Add(i10);

					ZeroOffsetIndices.Add(i00);
					ZeroOffsetIndices.Add(i01);
					ZeroOffsetIndices.Add(i11);
				}
			}

			FRawStaticIndexBuffer16or32<INDEX_TYPE>* ZeroOffsetIndexBuffer = new FRawStaticIndexBuffer16or32<INDEX_TYPE>(false);
			ZeroOffsetIndexBuffer->AssignNewBuffer(ZeroOffsetIndices);
			ZeroOffsetIndexBuffer->InitResource();
			ZeroOffsetIndexBuffers[Mip] = ZeroOffsetIndexBuffer;
		}
#endif
	}
}

void FLandscapeSharedBuffers::CreateOccluderIndexBuffer(int32 NumOccluderVertices)
{
	if (NumOccluderVertices <= 0 || NumOccluderVertices > MAX_uint16)
	{
		return;
	}

	uint16 NumLineQuads = ((uint16)FMath::Sqrt(NumOccluderVertices) - 1);
	uint16 NumLineVtx = NumLineQuads + 1;
	check(NumLineVtx*NumLineVtx == NumOccluderVertices);

	int32 NumTris = NumLineQuads*NumLineQuads * 2;
	int32 NumIndices = NumTris * 3;
	OccluderIndicesSP = MakeShared<FOccluderIndexArray, ESPMode::ThreadSafe>();
	OccluderIndicesSP->SetNumUninitialized(NumIndices, false);

	uint16* OcclusionIndices = OccluderIndicesSP->GetData();
	const uint16 NumLineVtxPlusOne = NumLineVtx + 1;
	const uint16 QuadIndices[2][3] = { {0, NumLineVtx, NumLineVtxPlusOne}, {0, NumLineVtxPlusOne, 1} };
	uint16 QuadOffset = 0;
	int32 Index = 0;
	for (int32 y = 0; y < NumLineQuads; y++)
	{
		for (int32 x = 0; x < NumLineQuads; x++)
		{
			for (int32 i = 0; i < 2; i++)
			{
				OcclusionIndices[Index++] = QuadIndices[i][0] + QuadOffset;
				OcclusionIndices[Index++] = QuadIndices[i][1] + QuadOffset;
				OcclusionIndices[Index++] = QuadIndices[i][2] + QuadOffset;
			}
			QuadOffset++;
		}
		QuadOffset++;
	}

	INC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderIndicesSP->GetAllocatedSize());
}

#if WITH_EDITOR
template <typename INDEX_TYPE>
void FLandscapeSharedBuffers::CreateGrassIndexBuffer()
{
	TArray<INDEX_TYPE> NewIndices;

	int32 ExpectedNumIndices = FMath::Square(NumSubsections) * (FMath::Square(SubsectionSizeVerts) * 4 / 3 - 1); // *4/3 is for mips, -1 because we only go down to 2x2 not 1x1
	NewIndices.Empty(ExpectedNumIndices);

	int32 NumMips = FMath::CeilLogTwo(SubsectionSizeVerts);

	for (int32 Mip = 0; Mip < NumMips; ++Mip)
	{
		// Store offset to the start of this mip in the index buffer
		GrassIndexMipOffsets.Add(NewIndices.Num());

		int32 MipSubsectionSizeVerts = SubsectionSizeVerts >> Mip;
		int32 SubOffset = 0;
		for (int32 SubY = 0; SubY < NumSubsections; SubY++)
		{
			for (int32 SubX = 0; SubX < NumSubsections; SubX++)
			{
				for (int32 y = 0; y < MipSubsectionSizeVerts; y++)
				{
					for (int32 x = 0; x < MipSubsectionSizeVerts; x++)
					{
						// intentionally using SubsectionSizeVerts not MipSubsectionSizeVerts, this is a vert buffer index not a mip vert index
						NewIndices.Add(x + y * SubsectionSizeVerts + SubOffset);
					}
				}

				// intentionally using SubsectionSizeVerts not MipSubsectionSizeVerts (as above)
				SubOffset += FMath::Square(SubsectionSizeVerts);
			}
		}
	}

	check(NewIndices.Num() == ExpectedNumIndices);

	// Create and init new index buffer with index data
	FRawStaticIndexBuffer16or32<INDEX_TYPE>* IndexBuffer = new FRawStaticIndexBuffer16or32<INDEX_TYPE>(false);
	IndexBuffer->AssignNewBuffer(NewIndices);
	IndexBuffer->InitResource();
	GrassIndexBuffer = IndexBuffer;
}
#endif

FLandscapeSharedBuffers::FLandscapeSharedBuffers(const int32 InSharedBuffersKey, const int32 InSubsectionSizeQuads, const int32 InNumSubsections, const ERHIFeatureLevel::Type InFeatureLevel, const bool bRequiresAdjacencyInformation, int32 NumOccluderVertices)
	: SharedBuffersKey(InSharedBuffersKey)
	, NumIndexBuffers(FMath::CeilLogTwo(InSubsectionSizeQuads + 1))
	, SubsectionSizeVerts(InSubsectionSizeQuads + 1)
	, NumSubsections(InNumSubsections)
	, VertexFactory(nullptr)
	, FixedGridVertexFactory(nullptr)
	, VertexBuffer(nullptr)
	, AdjacencyIndexBuffers(nullptr)
	, bUse32BitIndices(false)
#if WITH_EDITOR
	, GrassIndexBuffer(nullptr)
#endif
{
	NumVertices = FMath::Square(SubsectionSizeVerts) * FMath::Square(NumSubsections);
	if (InFeatureLevel > ERHIFeatureLevel::ES3_1)
	{
		// Vertex Buffer cannot be shared
		VertexBuffer = new FLandscapeVertexBuffer(InFeatureLevel, NumVertices, SubsectionSizeVerts, NumSubsections);
	}
	IndexBuffers = new FIndexBuffer*[NumIndexBuffers];
	FMemory::Memzero(IndexBuffers, sizeof(FIndexBuffer*)* NumIndexBuffers);
	IndexRanges = new FLandscapeIndexRanges[NumIndexBuffers]();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		ZeroOffsetIndexBuffers.AddZeroed(NumIndexBuffers);
	}
#endif

	// See if we need to use 16 or 32-bit index buffers
	if (NumVertices > 65535)
	{
		bUse32BitIndices = true;
		CreateIndexBuffers<uint32>(InFeatureLevel, bRequiresAdjacencyInformation);
#if WITH_EDITOR
		if (InFeatureLevel > ERHIFeatureLevel::ES3_1)
		{
			CreateGrassIndexBuffer<uint32>();
		}
#endif
	}
	else
	{
		CreateIndexBuffers<uint16>(InFeatureLevel, bRequiresAdjacencyInformation);
#if WITH_EDITOR
		if (InFeatureLevel > ERHIFeatureLevel::ES3_1)
		{
			CreateGrassIndexBuffer<uint16>();
		}
#endif
	}

	CreateOccluderIndexBuffer(NumOccluderVertices);
}

FLandscapeSharedBuffers::~FLandscapeSharedBuffers()
{
	delete VertexBuffer;

	for (int32 i = 0; i < NumIndexBuffers; i++)
	{
		IndexBuffers[i]->ReleaseResource();
		delete IndexBuffers[i];
	}
	delete[] IndexBuffers;
	delete[] IndexRanges;

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		while (ZeroOffsetIndexBuffers.Num() > 0)
		{
			FIndexBuffer* Buffer = ZeroOffsetIndexBuffers.Pop();
			Buffer->ReleaseResource();
			delete Buffer;
		}
	}
#endif

#if WITH_EDITOR
	if (GrassIndexBuffer)
	{
		GrassIndexBuffer->ReleaseResource();
		delete GrassIndexBuffer;
	}
#endif

	delete AdjacencyIndexBuffers;
	delete VertexFactory;

	if (OccluderIndicesSP.IsValid())
	{
		DEC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderIndicesSP->GetAllocatedSize());
	}
}

template<typename IndexType>
static void BuildLandscapeAdjacencyIndexBuffer(int32 LODSubsectionSizeQuads, int32 NumSubsections, const FRawStaticIndexBuffer16or32<IndexType>* Indices, TArray<IndexType>& OutPnAenIndices)
{
	if (Indices && Indices->Num())
	{
		// Landscape use regular grid, so only expand Index buffer works
		// PN AEN Dominant Corner
		uint32 TriCount = LODSubsectionSizeQuads*LODSubsectionSizeQuads * 2;

		uint32 ExpandedCount = 12 * TriCount * NumSubsections * NumSubsections;

		OutPnAenIndices.Empty(ExpandedCount);
		OutPnAenIndices.AddUninitialized(ExpandedCount);

		for (int32 SubY = 0; SubY < NumSubsections; SubY++)
		{
			for (int32 SubX = 0; SubX < NumSubsections; SubX++)
			{
				uint32 SubsectionTriIndex = (SubX + SubY * NumSubsections) * TriCount;

				for (uint32 TriIdx = SubsectionTriIndex; TriIdx < SubsectionTriIndex + TriCount; ++TriIdx)
				{
					uint32 OutStartIdx = TriIdx * 12;
					uint32 InStartIdx = TriIdx * 3;
					OutPnAenIndices[OutStartIdx + 0] = Indices->Get(InStartIdx + 0);
					OutPnAenIndices[OutStartIdx + 1] = Indices->Get(InStartIdx + 1);
					OutPnAenIndices[OutStartIdx + 2] = Indices->Get(InStartIdx + 2);

					OutPnAenIndices[OutStartIdx + 3] = Indices->Get(InStartIdx + 0);
					OutPnAenIndices[OutStartIdx + 4] = Indices->Get(InStartIdx + 1);
					OutPnAenIndices[OutStartIdx + 5] = Indices->Get(InStartIdx + 1);
					OutPnAenIndices[OutStartIdx + 6] = Indices->Get(InStartIdx + 2);
					OutPnAenIndices[OutStartIdx + 7] = Indices->Get(InStartIdx + 2);
					OutPnAenIndices[OutStartIdx + 8] = Indices->Get(InStartIdx + 0);

					OutPnAenIndices[OutStartIdx + 9] = Indices->Get(InStartIdx + 0);
					OutPnAenIndices[OutStartIdx + 10] = Indices->Get(InStartIdx + 1);
					OutPnAenIndices[OutStartIdx + 11] = Indices->Get(InStartIdx + 2);
				}
			}
		}
	}
	else
	{
		OutPnAenIndices.Empty();
	}
}


FLandscapeSharedAdjacencyIndexBuffer::FLandscapeSharedAdjacencyIndexBuffer(FLandscapeSharedBuffers* Buffers)
{
	check(Buffers && Buffers->IndexBuffers);

	// Currently only support PN-AEN-Dominant Corner, which is the only mode for UE4 for now
	IndexBuffers.Empty(Buffers->NumIndexBuffers);

	bool b32BitIndex = Buffers->NumVertices > 65535;
	for (int32 i = 0; i < Buffers->NumIndexBuffers; ++i)
	{
		if (b32BitIndex)
		{
			TArray<uint32> OutPnAenIndices;
			BuildLandscapeAdjacencyIndexBuffer<uint32>((Buffers->SubsectionSizeVerts >> i) - 1, Buffers->NumSubsections, (FRawStaticIndexBuffer16or32<uint32>*)Buffers->IndexBuffers[i], OutPnAenIndices);

			FRawStaticIndexBuffer16or32<uint32>* IndexBuffer = new FRawStaticIndexBuffer16or32<uint32>();
			IndexBuffer->AssignNewBuffer(OutPnAenIndices);
			IndexBuffers.Add(IndexBuffer);
		}
		else
		{
			TArray<uint16> OutPnAenIndices;
			BuildLandscapeAdjacencyIndexBuffer<uint16>((Buffers->SubsectionSizeVerts >> i) - 1, Buffers->NumSubsections, (FRawStaticIndexBuffer16or32<uint16>*)Buffers->IndexBuffers[i], OutPnAenIndices);

			FRawStaticIndexBuffer16or32<uint16>* IndexBuffer = new FRawStaticIndexBuffer16or32<uint16>();
			IndexBuffer->AssignNewBuffer(OutPnAenIndices);
			IndexBuffers.Add(IndexBuffer);
		}

		IndexBuffers[i]->InitResource();
	}
}

FLandscapeSharedAdjacencyIndexBuffer::~FLandscapeSharedAdjacencyIndexBuffer()
{
	for (int32 i = 0; i < IndexBuffers.Num(); ++i)
	{
		IndexBuffers[i]->ReleaseResource();
		delete IndexBuffers[i];
	}
}

//
// FLandscapeVertexFactoryVertexShaderParameters
//

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryVertexShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeVertexFactoryVertexShaderParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
	}

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
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FLandscapeComponentSceneProxy* SceneProxy = BatchElementParams->SceneProxy;

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);

		if (SceneProxy && SceneProxy->bRegistered)
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), LandscapeRenderSystems.FindChecked(SceneProxy->LandscapeKey)->UniformBuffer);
		}
		else
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), GNullLandscapeRenderSystemResources.UniformBuffer);
		}

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeVertexFactoryMVFParameters>(), BatchElementParams->LandscapeVertexFactoryMVFUniformBuffer);
		}
#endif
	}
};

/** 
  * Shader parameters for use with FLandscapeFixedGridVertexFactory
  * Simple grid rendering (without dynamic lod blend) needs a simpler fixed setup.
  */
class FLandscapeFixedGridVertexFactoryVertexShaderParameters : public FLandscapeVertexFactoryVertexShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeFixedGridVertexFactoryVertexShaderParameters, NonVirtual);
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
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeFixedGridUniformShaderParameters>(), (*BatchElementParams->FixedGridUniformShaderParameters)[BatchElementParams->CurrentLOD]);

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeVertexFactoryMVFParameters>(), BatchElementParams->LandscapeVertexFactoryMVFUniformBuffer);
		}
#endif
	}
};

//
// FLandscapeVertexFactoryPixelShaderParameters
//

void FLandscapeVertexFactoryPixelShaderParameters::GetElementShaderBindings(
	const class FSceneInterface* Scene,
	const FSceneView* InView,
	const class FMeshMaterialShader* Shader,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory,
	const FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams
) const
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimePS);

	const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;

	ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);
}

//
// FLandscapeVertexFactory
//

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeVertexFactoryMVFParameters, "LandscapeMVF");

void FLandscapeVertexFactory::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(Data.PositionComponent, 0));

	// create the actual device decls
	InitDeclaration(Elements);
}

FLandscapeVertexFactory::FLandscapeVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FVertexFactory(InFeatureLevel)
{
}

bool FLandscapeVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	// only compile landscape materials for landscape vertex factory
	// The special engine materials must be compiled for the landscape vertex factory because they are used with it for wireframe, etc.
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FLandscapeVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactory, SF_Vertex, FLandscapeVertexFactoryVertexShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactory, SF_Compute, FLandscapeVertexFactoryVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactory, SF_RayHitGroup, FLandscapeVertexFactoryVertexShaderParameters);
#endif // RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactory, SF_Pixel, FLandscapeVertexFactoryPixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FLandscapeVertexFactory, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false, true, false);

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FLandscapeVertexFactory::Copy(const FLandscapeVertexFactory& Other)
{
	//SetSceneProxy(Other.Proxy());
	FLandscapeVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.Data;
	ENQUEUE_RENDER_COMMAND(FLandscapeVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

//
// FLandscapeXYOffsetVertexFactory
//

void FLandscapeXYOffsetVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FLandscapeVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("LANDSCAPE_XYOFFSET"), TEXT("1"));
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeXYOffsetVertexFactory, SF_Vertex, FLandscapeVertexFactoryVertexShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeXYOffsetVertexFactory, SF_Compute, FLandscapeVertexFactoryVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeXYOffsetVertexFactory, SF_RayHitGroup, FLandscapeVertexFactoryVertexShaderParameters);
#endif // RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeXYOffsetVertexFactory, SF_Pixel, FLandscapeVertexFactoryPixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FLandscapeXYOffsetVertexFactory, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false, true, false);

//
// FLandscapeFixedGridVertexFactory
//

void FLandscapeFixedGridVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FLandscapeVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("FIXED_GRID"), TEXT("1"));
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactory, SF_Vertex, FLandscapeFixedGridVertexFactoryVertexShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactory, SF_Compute, FLandscapeFixedGridVertexFactoryVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactory, SF_RayHitGroup, FLandscapeFixedGridVertexFactoryVertexShaderParameters);
#endif
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactory, SF_Pixel, FLandscapeVertexFactoryPixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FLandscapeFixedGridVertexFactory, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false, true, false);


/** ULandscapeMaterialInstanceConstant */
ULandscapeMaterialInstanceConstant::ULandscapeMaterialInstanceConstant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsLayerThumbnail = false;
}

void ULandscapeMaterialInstanceConstant::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	UpdateCachedTextureStreaming();
#endif // WITH_EDITOR
}

float ULandscapeMaterialInstanceConstant::GetLandscapeTexelFactor(const FName& TextureName) const
{
	for (const FLandscapeMaterialTextureStreamingInfo& Info : TextureStreamingInfo)
	{
		if (Info.TextureName == TextureName)
		{
			return Info.TexelFactor;
		}
	}
	return 1.0f;
}

#if WITH_EDITOR

void ULandscapeMaterialInstanceConstant::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateCachedTextureStreaming();
}

FLandscapeMaterialTextureStreamingInfo& ULandscapeMaterialInstanceConstant::AcquireTextureStreamingInfo(const FName& TextureName)
{
	for (FLandscapeMaterialTextureStreamingInfo& Info : TextureStreamingInfo)
	{
		if (Info.TextureName == TextureName)
		{
			return Info;
		}
	}
	FLandscapeMaterialTextureStreamingInfo& Info = TextureStreamingInfo.AddDefaulted_GetRef();
	Info.TextureName = TextureName;
	Info.TexelFactor = 1.0f;
	return Info;
}

void ULandscapeMaterialInstanceConstant::UpdateCachedTextureStreaming()
{
	// Remove outdated elements that no longer match the material's expressions.
	TextureStreamingInfo.Empty();

	const UMaterial* Material = GetMaterial();
	if (Material)
	{
		int32 NumExpressions = Material->Expressions.Num();
		for (int32 ExpressionIndex = 0; ExpressionIndex < NumExpressions; ExpressionIndex++)
		{
			UMaterialExpression* Expression = Material->Expressions[ExpressionIndex];
			UMaterialExpressionTextureSample* TextureSample = Cast<UMaterialExpressionTextureSample>(Expression);

			// TODO: This is only works for direct Coordinate Texture Sample cases
			if (TextureSample && TextureSample->Texture && TextureSample->Coordinates.IsConnected())
			{
				if (UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(TextureSample->Coordinates.Expression))
				{
					FLandscapeMaterialTextureStreamingInfo& Info = AcquireTextureStreamingInfo(TextureSample->Texture->GetFName());
					Info.TexelFactor *= FPlatformMath::Max(TextureCoordinate->UTiling, TextureCoordinate->VTiling);
				}
				else if (UMaterialExpressionLandscapeLayerCoords* TerrainTextureCoordinate = Cast<UMaterialExpressionLandscapeLayerCoords>(TextureSample->Coordinates.Expression))
				{
					FLandscapeMaterialTextureStreamingInfo& Info = AcquireTextureStreamingInfo(TextureSample->Texture->GetFName());
					Info.TexelFactor *= TerrainTextureCoordinate->MappingScale;
				}
			}
		}
	}
}

#endif // WITH_EDITOR

class FLandscapeMaterialResource : public FMaterialResource
{
	const bool bIsLayerThumbnail;
	const bool bDisableTessellation;
	const bool bMobile;
	const bool bEditorToolUsage;

public:
	FLandscapeMaterialResource(ULandscapeMaterialInstanceConstant* Parent)
		: bIsLayerThumbnail(Parent->bIsLayerThumbnail)
		, bDisableTessellation(Parent->bDisableTessellation)
		, bMobile(Parent->bMobile)
		, bEditorToolUsage(Parent->bEditorToolUsage)
	{
	}

	void GetShaderMapId(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, FMaterialShaderMapId& OutId) const override
	{
		FMaterialResource::GetShaderMapId(Platform, TargetPlatform, OutId);

#if WITH_EDITOR
		if (bIsLayerThumbnail || bDisableTessellation)
		{
			FSHA1 Hash;
			Hash.Update(OutId.BasePropertyOverridesHash.Hash, UE_ARRAY_COUNT(OutId.BasePropertyOverridesHash.Hash));

			const FString HashString = TEXT("bOverride_TessellationMode");
			Hash.UpdateWithString(*HashString, HashString.Len());

			Hash.Final();
			Hash.GetHash(OutId.BasePropertyOverridesHash.Hash);
		}
#endif
	}

	bool IsUsedWithLandscape() const override
	{
		return !bIsLayerThumbnail;
	}

	bool IsUsedWithStaticLighting() const override
	{
		if (bIsLayerThumbnail)
		{
			return false;
		}
		return FMaterialResource::IsUsedWithStaticLighting();
	}

	bool IsUsedWithSkeletalMesh()          const override { return false; }
	bool IsUsedWithParticleSystem()        const override { return false; }
	bool IsUsedWithParticleSprites()       const override { return false; }
	bool IsUsedWithBeamTrails()            const override { return false; }
	bool IsUsedWithMeshParticles()         const override { return false; }
	bool IsUsedWithNiagaraSprites()       const override { return false; }
	bool IsUsedWithNiagaraRibbons()       const override { return false; }
	bool IsUsedWithNiagaraMeshParticles()       const override { return false; }
	bool IsUsedWithMorphTargets()          const override { return false; }
	bool IsUsedWithSplineMeshes()          const override { return false; }
	bool IsUsedWithInstancedStaticMeshes() const override { return false; }
	bool IsUsedWithAPEXCloth()             const override { return false; }
	bool IsUsedWithGeometryCache()         const override { return false; }
	EMaterialTessellationMode GetTessellationMode() const override { return (bIsLayerThumbnail || bDisableTessellation) ? MTM_NoTessellation : FMaterialResource::GetTessellationMode(); };

	bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const override
	{
		// Don't compile if this is a mobile shadermap and a desktop MIC, and vice versa, unless it's a tool material
		if (!(IsPCPlatform(Platform) && bEditorToolUsage) && bMobile != IsMobilePlatform(Platform))
		{
			// @todo For some reason this causes this resource to return true for IsCompilationFinished. For now we will needlessly compile this shader until this is fixed.
			//return false;
		}

		if (VertexFactoryType)
		{
			// Always check against FLocalVertexFactory in editor builds as it is required to render thumbnails
			// Thumbnail MICs are only rendered in the preview scene using a simple LocalVertexFactory
			if (bIsLayerThumbnail)
			{
				static const FName LocalVertexFactory = FName(TEXT("FLocalVertexFactory"));
				if (!IsMobilePlatform(Platform) && VertexFactoryType->GetFName() == LocalVertexFactory)
				{
					if (Algo::Find(GetAllowedShaderTypesInThumbnailRender(), ShaderType->GetFName()))
					{
						return FMaterialResource::ShouldCache(Platform, ShaderType, VertexFactoryType);
					}
					else
					{
						if (Algo::Find(GetExcludedShaderTypesInThumbnailRender(), ShaderType->GetFName()))
						{
							UE_LOG(LogLandscape, VeryVerbose, TEXT("Excluding shader %s from landscape thumbnail material"), ShaderType->GetName());
							return false;
						}
						else
						{
							if (Platform == EShaderPlatform::SP_PCD3D_SM5)
							{
								UE_LOG(LogLandscape, Warning, TEXT("Shader %s unknown by landscape thumbnail material, please add to either AllowedShaderTypes or ExcludedShaderTypes"), ShaderType->GetName());
							}
							return FMaterialResource::ShouldCache(Platform, ShaderType, VertexFactoryType);
						}
					}
				}
			}
			else
			{
				// Landscape MICs are only for use with the Landscape vertex factories

				// For now only compile FLandscapeFixedGridVertexFactory for grass and runtime virtual texture page rendering (can change if we need for other cases)
				// Todo: only compile LandscapeXYOffsetVertexFactory if we are using it
				bool bIsGrassShaderType = Algo::Find(GetGrassShaderTypes(), ShaderType->GetFName()) != nullptr;
				bool bIsGPULightmassShaderType = Algo::Find(GetGPULightmassShaderTypes(), ShaderType->GetFName()) != nullptr;
				bool bIsRuntimeVirtualTextureShaderType = Algo::Find(GetRuntimeVirtualTextureShaderTypes(), ShaderType->GetFName()) != nullptr;

				bool bIsShaderTypeUsingFixedGrid = bIsGrassShaderType || bIsRuntimeVirtualTextureShaderType || bIsGPULightmassShaderType;

				bool bIsRayTracingShaderType = FName(TEXT("FRayTracingDynamicGeometryConverterCS")) == ShaderType->GetFName();

				static const FName LandscapeVertexFactory = FName(TEXT("FLandscapeVertexFactory"));
				static const FName LandscapeXYOffsetVertexFactory = FName(TEXT("FLandscapeXYOffsetVertexFactory"));
				static const FName LandscapeVertexFactoryMobile = FName(TEXT("FLandscapeVertexFactoryMobile"));
				if (VertexFactoryType->GetFName() == LandscapeVertexFactory ||
					VertexFactoryType->GetFName() == LandscapeXYOffsetVertexFactory ||
					VertexFactoryType->GetFName() == LandscapeVertexFactoryMobile)
				{
					return (bIsRayTracingShaderType || !bIsShaderTypeUsingFixedGrid) && FMaterialResource::ShouldCache(Platform, ShaderType, VertexFactoryType);
				}

				static const FName LandscapeFixedGridVertexFactory = FName(TEXT("FLandscapeFixedGridVertexFactory"));
				static const FName LandscapeFixedGridVertexFactoryMobile = FName(TEXT("FLandscapeFixedGridVertexFactoryMobile"));
				if (VertexFactoryType->GetFName() == LandscapeFixedGridVertexFactory ||
					VertexFactoryType->GetFName() == LandscapeFixedGridVertexFactoryMobile)
				{
					return (bIsRayTracingShaderType || bIsShaderTypeUsingFixedGrid) && FMaterialResource::ShouldCache(Platform, ShaderType, VertexFactoryType);
				}
			}
		}

		return false;
	}

	static const TArray<FName>& GetAllowedShaderTypesInThumbnailRender()
	{
		// reduce the number of shaders compiled for the thumbnail materials by only compiling with shader types known to be used by the preview scene
		static const TArray<FName> AllowedShaderTypes =
		{
			FName(TEXT("TBasePassVSFNoLightMapPolicy")),
			FName(TEXT("TBasePassPSFNoLightMapPolicy")),
			FName(TEXT("TBasePassVSFCachedPointIndirectLightingPolicy")),
			FName(TEXT("TBasePassPSFCachedPointIndirectLightingPolicy")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_OutputDepthfalse")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_OutputDepthtrue")), // used by LPV
			FName(TEXT("TShadowDepthPSPixelShadowDepth_NonPerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthPSPixelShadowDepth_NonPerspectiveCorrecttrue")), // used by LPV
			FName(TEXT("TBasePassPSFSimpleDirectionalLightLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleDirectionalLightLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSimpleDirectionalLightLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleNoLightmapLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleNoLightmapLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSimpleNoLightmapLightingPolicy")),
			FName(TEXT("TBasePassVSFSimpleNoLightmapLightingPolicyAtmosphericFog")),
			FName(TEXT("FAnisotropyVS")),
			FName(TEXT("FAnisotropyPS")),
			FName(TEXT("TDepthOnlyVS<false>")),
			FName(TEXT("TDepthOnlyVS<true>")),
			FName(TEXT("FDepthOnlyPS<true>")),
			FName(TEXT("FDepthOnlyPS<false>")),
			// UE-44519, masked material with landscape layers requires FHitProxy shaders.
			FName(TEXT("FHitProxyVS")),
			FName(TEXT("FHitProxyPS")),
			FName(TEXT("FVelocityVS")),
			FName(TEXT("FVelocityPS")),

			FName(TEXT("TBasePassPSFSimpleStationaryLightSingleSampleShadowsLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleStationaryLightSingleSampleShadowsLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSimpleStationaryLightSingleSampleShadowsLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleStationaryLightPrecomputedShadowsLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleStationaryLightPrecomputedShadowsLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSimpleStationaryLightPrecomputedShadowsLightingPolicy")),
			FName(TEXT("TBasePassVSFNoLightMapPolicyAtmosphericFog")),
			FName(TEXT("TBasePassDSFNoLightMapPolicy")),
			FName(TEXT("TBasePassHSFNoLightMapPolicy")),
			FName(TEXT("TLightMapDensityVSFNoLightMapPolicy")),
			FName(TEXT("TLightMapDensityPSFNoLightMapPolicy")),

			// Mobile
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMLightingPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMLightingPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMLightingPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMLightingPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightCSMLightingPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightLightingPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightLightingPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightLightingPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightLightingPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightLightingPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileDirectionalLightCSMAndSHIndirectPolicyHDRLinear64")),

			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMAndSHIndirectPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMAndSHIndirectPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMAndSHIndirectPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMAndSHIndirectPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightCSMAndSHIndirectPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightAndSHIndirectPolicyINT32_MAXHDRLinear64Skylight")),

			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightAndSHIndirectPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightAndSHIndirectPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightAndSHIndirectPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightAndSHIndirectPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightAndSHIndirectPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightAndSHIndirectPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightAndSHIndirectPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDirectionalLightAndSHIndirectPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileDirectionalLightAndSHIndirectPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFNoLightMapPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFNoLightMapPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFNoLightMapPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFNoLightMapPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFNoLightMapPolicyHDRLinear64")),

			// Forward shading required
			FName(TEXT("TBasePassPSFCachedPointIndirectLightingPolicySkylight")),
			FName(TEXT("TBasePassPSFNoLightMapPolicySkylight")),

			// Runtime virtual texture
			FName(TEXT("TVirtualTextureVSBaseColor")),
			FName(TEXT("TVirtualTextureVSBaseColorNormal")),
			FName(TEXT("TVirtualTextureVSBaseColorNormalSpecular")),
			FName(TEXT("TVirtualTextureVSWorldHeight")),
			FName(TEXT("TVirtualTexturePSBaseColor")),
			FName(TEXT("TVirtualTexturePSBaseColorNormal")),
			FName(TEXT("TVirtualTexturePSBaseColorNormalSpecular")),
			FName(TEXT("TVirtualTexturePSWorldHeight")),
		};
		return AllowedShaderTypes;
	}

	static const TArray<FName>& GetExcludedShaderTypesInThumbnailRender()
	{
		// shader types known *not* to be used by the preview scene
		static const TArray<FName> ExcludedShaderTypes =
		{
			// This is not an exhaustive list
			FName(TEXT("FDebugViewModeVS")),
			FName(TEXT("FConvertToUniformMeshVS")),
			FName(TEXT("FConvertToUniformMeshGS")),

			// No lightmap on thumbnails
			FName(TEXT("TLightMapDensityVSFDummyLightMapPolicy")),
			FName(TEXT("TLightMapDensityPSFDummyLightMapPolicy")),
			FName(TEXT("TLightMapDensityPSTLightMapPolicyHQ")),
			FName(TEXT("TLightMapDensityVSTLightMapPolicyHQ")),
			FName(TEXT("TLightMapDensityPSTLightMapPolicyLQ")),
			FName(TEXT("TLightMapDensityVSTLightMapPolicyLQ")),
			FName(TEXT("TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ")),
			FName(TEXT("TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQSkylight")),
			FName(TEXT("TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ")),
			FName(TEXT("TBasePassPSTLightMapPolicyHQ")),
			FName(TEXT("TBasePassPSTLightMapPolicyHQSkylight")),
			FName(TEXT("TBasePassVSTLightMapPolicyHQ")),
			FName(TEXT("TBasePassPSTLightMapPolicyLQ")),
			FName(TEXT("TBasePassPSTLightMapPolicyLQSkylight")),
			FName(TEXT("TBasePassVSTLightMapPolicyLQ")),
			FName(TEXT("TBasePassVSFSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy")),

			// Mobile
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightCSMWithLightmapPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightWithLightmapPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightWithLightmapPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightWithLightmapPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileMovableDirectionalLightWithLightmapPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileMovableDirectionalLightWithLightmapPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicyINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicyINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicy0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicy0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSFMobileDistanceFieldShadowsAndLQLightMapPolicyHDRLinear64")),
			FName(TEXT("TMobileBasePassPSTLightMapPolicyLQINT32_MAXHDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSTLightMapPolicyLQINT32_MAXHDRLinear64")),
			FName(TEXT("TMobileBasePassPSTLightMapPolicyLQ0HDRLinear64Skylight")),
			FName(TEXT("TMobileBasePassPSTLightMapPolicyLQ0HDRLinear64")),
			FName(TEXT("TMobileBasePassVSTLightMapPolicyLQHDRLinear64")),

			FName(TEXT("TBasePassVSFCachedVolumeIndirectLightingPolicy")),
			FName(TEXT("TBasePassPSFCachedVolumeIndirectLightingPolicy")),
			FName(TEXT("TBasePassPSFCachedVolumeIndirectLightingPolicySkylight")),
			FName(TEXT("TBasePassPSFPrecomputedVolumetricLightmapLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy")),
			FName(TEXT("TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy")),

			FName(TEXT("TBasePassPSFSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy")),

			FName(TEXT("TBasePassVSFCachedPointIndirectLightingPolicyAtmosphericFog")),
			FName(TEXT("TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy")),
			FName(TEXT("TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicy")),
			FName(TEXT("TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicyAtmosphericFog")),
			FName(TEXT("TBasePassVSFSelfShadowedTranslucencyPolicy")),
			FName(TEXT("TBasePassPSFSelfShadowedTranslucencyPolicy")),
			FName(TEXT("TBasePassPSFSelfShadowedTranslucencyPolicySkylight")),
			FName(TEXT("TBasePassVSFSelfShadowedTranslucencyPolicyAtmosphericFog")),

			FName(TEXT("TShadowDepthVSVertexShadowDepth_PerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_PerspectiveCorrecttrue")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_OnePassPointLightfalse")),
			FName(TEXT("TShadowDepthPSPixelShadowDepth_PerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthPSPixelShadowDepth_PerspectiveCorrecttrue")),
			FName(TEXT("TShadowDepthPSPixelShadowDepth_OnePassPointLightfalse")),
			FName(TEXT("TShadowDepthPSPixelShadowDepth_OnePassPointLighttrue")),

			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_OutputDepthfalse")),
			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_OutputDepthtrue")),
			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_PerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_PerspectiveCorrecttrue")),
			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_OnePassPointLightfalse")),
			FName(TEXT("FOnePassPointShadowDepthGS")),

			FName(TEXT("TTranslucencyShadowDepthVS<TranslucencyShadowDepth_Standard>")),
			FName(TEXT("TTranslucencyShadowDepthPS<TranslucencyShadowDepth_Standard>")),
			FName(TEXT("TTranslucencyShadowDepthVS<TranslucencyShadowDepth_PerspectiveCorrect>")),
			FName(TEXT("TTranslucencyShadowDepthPS<TranslucencyShadowDepth_PerspectiveCorrect>")),

			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_OnePassPointLight")),
			FName(TEXT("TShadowDepthVSForGSVertexShadowDepth_OnePassPointLightPositionOnly")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_OnePassPointLightPositionOnly")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_OutputDepthPositionOnly")),
			FName(TEXT("TShadowDepthVSVertexShadowDepth_PerspectiveCorrectPositionOnly")),

			FName(TEXT("TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQAtmosphericFog")),
			FName(TEXT("TBasePassVSTLightMapPolicyHQAtmosphericFog")),
			FName(TEXT("TBasePassVSTLightMapPolicyLQAtmosphericFog")),
			FName(TEXT("TBasePassVSFPrecomputedVolumetricLightmapLightingPolicyAtmosphericFog")),
			FName(TEXT("TBasePassPSFSelfShadowedVolumetricLightmapPolicy")),
			FName(TEXT("TBasePassPSFSelfShadowedVolumetricLightmapPolicySkylight")),
			FName(TEXT("TBasePassVSFSelfShadowedVolumetricLightmapPolicyAtmosphericFog")),
			FName(TEXT("TBasePassVSFSelfShadowedVolumetricLightmapPolicy")),

			FName(TEXT("TBasePassPSFSimpleLightmapOnlyLightingPolicy")),
			FName(TEXT("TBasePassPSFSimpleLightmapOnlyLightingPolicySkylight")),
			FName(TEXT("TBasePassVSFSimpleLightmapOnlyLightingPolicy")),

			FName(TEXT("TShadowDepthDSVertexShadowDepth_OnePassPointLightfalse")),
			FName(TEXT("TShadowDepthHSVertexShadowDepth_OnePassPointLightfalse")),
			FName(TEXT("TShadowDepthDSVertexShadowDepth_OutputDepthfalse")),
			FName(TEXT("TShadowDepthHSVertexShadowDepth_OutputDepthfalse")),
			FName(TEXT("TShadowDepthDSVertexShadowDepth_OutputDepthtrue")),
			FName(TEXT("TShadowDepthHSVertexShadowDepth_OutputDepthtrue")),

			FName(TEXT("TShadowDepthDSVertexShadowDepth_PerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthHSVertexShadowDepth_PerspectiveCorrectfalse")),
			FName(TEXT("TShadowDepthDSVertexShadowDepth_PerspectiveCorrecttrue")),
			FName(TEXT("TShadowDepthHSVertexShadowDepth_PerspectiveCorrecttrue")),

			FName(TEXT("FVelocityDS")),
			FName(TEXT("FVelocityHS")),
			FName(TEXT("FHitProxyDS")),
			FName(TEXT("FHitProxyHS")),

			FName(TEXT("TLightMapDensityDSTLightMapPolicyHQ")),
			FName(TEXT("TLightMapDensityHSTLightMapPolicyHQ")),
			FName(TEXT("TLightMapDensityDSTLightMapPolicyLQ")),
			FName(TEXT("TLightMapDensityHSTLightMapPolicyLQ")),
			FName(TEXT("TLightMapDensityDSFDummyLightMapPolicy")),
			FName(TEXT("TLightMapDensityHSFDummyLightMapPolicy")),
			FName(TEXT("TLightMapDensityDSFNoLightMapPolicy")),
			FName(TEXT("TLightMapDensityHSFNoLightMapPolicy")),
			FName(TEXT("FDepthOnlyDS")),
			FName(TEXT("FDepthOnlyHS")),
			FName(TEXT("FDebugViewModeDS")),
			FName(TEXT("FDebugViewModeHS")),
			FName(TEXT("TBasePassDSTDistanceFieldShadowsAndLightMapPolicyHQ")),
			FName(TEXT("TBasePassHSTDistanceFieldShadowsAndLightMapPolicyHQ")),

			FName(TEXT("TBasePassDSTLightMapPolicyHQ")),
			FName(TEXT("TBasePassHSTLightMapPolicyHQ")),
			FName(TEXT("TBasePassDSTLightMapPolicyLQ")),
			FName(TEXT("TBasePassHSTLightMapPolicyLQ")),
			FName(TEXT("TBasePassDSFCachedPointIndirectLightingPolicy")),
			FName(TEXT("TBasePassHSFCachedPointIndirectLightingPolicy")),
			FName(TEXT("TBasePassDSFCachedVolumeIndirectLightingPolicy")),
			FName(TEXT("TBasePassHSFCachedVolumeIndirectLightingPolicy")),

			FName(TEXT("TBasePassDSFPrecomputedVolumetricLightmapLightingPolicy")),
			FName(TEXT("TBasePassHSFPrecomputedVolumetricLightmapLightingPolicy")),

#if RHI_RAYTRACING
				// No ray tracing on thumbnails
				FName(TEXT("TMaterialCHSFPrecomputedVolumetricLightmapLightingPolicy")),
				FName(TEXT("TMaterialCHSFNoLightMapPolicy")),
				FName(TEXT("FRayTracingDynamicGeometryConverterCS")),
				FName(TEXT("FTrivialMaterialCHS"))
#endif // RHI_RAYTRACING
		};
		return ExcludedShaderTypes;
	}

	static const TArray<FName>& GetGPULightmassShaderTypes()
	{
		static const TArray<FName> ShaderTypes =
		{
			FName(TEXT("TLightmapMaterialCHS<true>")),
			FName(TEXT("TLightmapMaterialCHS<false>")),
			FName(TEXT("FVLMVoxelizationVS")),
			FName(TEXT("FVLMVoxelizationGS")),
			FName(TEXT("FVLMVoxelizationPS")),
			FName(TEXT("FLightmapGBufferVS")),
			FName(TEXT("FLightmapGBufferPS")),
		};
		return ShaderTypes;		
	}

	static const TArray<FName>& GetGrassShaderTypes()
	{
		static const TArray<FName> ShaderTypes =
		{
			FName(TEXT("FLandscapeGrassWeightVS")),
			FName(TEXT("FLandscapeGrassWeightPS")),
			FName(TEXT("FLandscapePhysicalMaterialVS")),
			FName(TEXT("FLandscapePhysicalMaterialPS")),
		};
		return ShaderTypes;
	}

	static const TArray<FName>& GetRuntimeVirtualTextureShaderTypes()
	{
		static const TArray<FName> ShaderTypes =
		{
			FName(TEXT("TVirtualTextureVSBaseColor")),
			FName(TEXT("TVirtualTextureVSBaseColorNormal")),
			FName(TEXT("TVirtualTextureVSBaseColorNormalSpecular")),
			FName(TEXT("TVirtualTextureVSWorldHeight")),
			FName(TEXT("TVirtualTexturePSBaseColor")),
			FName(TEXT("TVirtualTexturePSBaseColorNormal")),
			FName(TEXT("TVirtualTexturePSBaseColorNormalSpecular")),
			FName(TEXT("TVirtualTexturePSWorldHeight")),
		};
		return ShaderTypes;
	}
};

FMaterialResource* ULandscapeMaterialInstanceConstant::AllocatePermutationResource()
{
	return new FLandscapeMaterialResource(this);
}

bool ULandscapeMaterialInstanceConstant::HasOverridenBaseProperties() const
{
	if (Parent)
	{
		// force a static permutation for ULandscapeMaterialInstanceConstants
		if (!Parent->IsA<ULandscapeMaterialInstanceConstant>())
		{
			return true;
		}
		ULandscapeMaterialInstanceConstant* LandscapeMICParent = CastChecked<ULandscapeMaterialInstanceConstant>(Parent);
		if (bDisableTessellation != LandscapeMICParent->bDisableTessellation)
		{
			return true;
		}
	}

	return Super::HasOverridenBaseProperties();
}

//////////////////////////////////////////////////////////////////////////

void ULandscapeComponent::GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const
{
	ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(GetOuter());
	FSphere BoundingSphere = Bounds.GetSphere();
	float LocalStreamingDistanceMultiplier = 1.f;
	float TexelFactor = 0.0f;
	if (Proxy)
	{
		LocalStreamingDistanceMultiplier = FMath::Max(0.0f, Proxy->StreamingDistanceMultiplier);
		TexelFactor = 0.75f * LocalStreamingDistanceMultiplier * ComponentSizeQuads * FMath::Abs(Proxy->GetRootComponent()->GetRelativeScale3D().X);
	}

	ERHIFeatureLevel::Type FeatureLevel = LevelContext.GetFeatureLevel();
	int32 MaterialInstanceCount = FeatureLevel >= ERHIFeatureLevel::SM5 ? GetMaterialInstanceCount() : MobileMaterialInterfaces.Num();

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialInstanceCount; ++MaterialIndex)
	{
		const UMaterialInterface* MaterialInterface = FeatureLevel >= ERHIFeatureLevel::SM5 ? GetMaterialInstance(MaterialIndex) : MobileMaterialInterfaces[MaterialIndex];

		// Normal usage...
		// Enumerate the textures used by the material.
		if (MaterialInterface)
		{
			TArray<UTexture*> Textures;
			MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::Num, false, FeatureLevel, false);

			const ULandscapeMaterialInstanceConstant* LandscapeMaterial = Cast<ULandscapeMaterialInstanceConstant>(MaterialInterface);

			// Add each texture to the output with the appropriate parameters.
			// TODO: Take into account which UVIndex is being used.
			for (int32 TextureIndex = 0; TextureIndex < Textures.Num(); TextureIndex++)
			{
				UTexture2D* Texture2D = Cast<UTexture2D>(Textures[TextureIndex]);
				if (!Texture2D) continue;

				FStreamingRenderAssetPrimitiveInfo& StreamingTexture = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
				StreamingTexture.Bounds = BoundingSphere;
				StreamingTexture.TexelFactor = TexelFactor;
				StreamingTexture.RenderAsset = Texture2D;

				if (LandscapeMaterial)
				{
					const float MaterialTexelFactor = LandscapeMaterial->GetLandscapeTexelFactor(Texture2D->GetFName());
					StreamingTexture.TexelFactor *= MaterialTexelFactor;
				}
			}

			// Lightmap
			const FMeshMapBuildData* MapBuildData = GetMeshMapBuildData();

			FLightMap2D* Lightmap = MapBuildData && MapBuildData->LightMap ? MapBuildData->LightMap->GetLightMap2D() : nullptr;
			uint32 LightmapIndex = AllowHighQualityLightmaps(FeatureLevel) ? 0 : 1;
			if (Lightmap && Lightmap->IsValid(LightmapIndex))
			{
				const FVector2D& Scale = Lightmap->GetCoordinateScale();
				if (Scale.X > SMALL_NUMBER && Scale.Y > SMALL_NUMBER)
				{
					const float LightmapTexelFactor = TexelFactor / FMath::Min(Scale.X, Scale.Y);
					new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(Lightmap->GetTexture(LightmapIndex), Bounds, LightmapTexelFactor);
					new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(Lightmap->GetAOMaterialMaskTexture(), Bounds, LightmapTexelFactor);
					new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(Lightmap->GetSkyOcclusionTexture(), Bounds, LightmapTexelFactor);
				}
			}

			// Shadowmap
			FShadowMap2D* Shadowmap = MapBuildData && MapBuildData->ShadowMap ? MapBuildData->ShadowMap->GetShadowMap2D() : nullptr;
			if (Shadowmap && Shadowmap->IsValid())
			{
				const FVector2D& Scale = Shadowmap->GetCoordinateScale();
				if (Scale.X > SMALL_NUMBER && Scale.Y > SMALL_NUMBER)
				{
					const float ShadowmapTexelFactor = TexelFactor / FMath::Min(Scale.X, Scale.Y);
					new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(Shadowmap->GetTexture(), Bounds, ShadowmapTexelFactor);
				}
			}
		}
	}

	// Weightmap
	for (int32 TextureIndex = 0; TextureIndex < WeightmapTextures.Num(); TextureIndex++)
	{
		FStreamingRenderAssetPrimitiveInfo& StreamingWeightmap = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
		StreamingWeightmap.Bounds = BoundingSphere;
		StreamingWeightmap.TexelFactor = TexelFactor;
		StreamingWeightmap.RenderAsset = WeightmapTextures[TextureIndex];
	}

	// Heightmap
	if (HeightmapTexture)
	{
		FStreamingRenderAssetPrimitiveInfo& StreamingHeightmap = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
		StreamingHeightmap.Bounds = BoundingSphere;

		float HeightmapTexelFactor = TexelFactor * (static_cast<float>(HeightmapTexture->GetSizeY()) / (ComponentSizeQuads + 1));
		StreamingHeightmap.TexelFactor = ForcedLOD >= 0 ? -(1 << (13 - ForcedLOD)) : HeightmapTexelFactor; // Minus Value indicate forced resolution (Mip 13 for 8k texture)
		StreamingHeightmap.RenderAsset = HeightmapTexture;
	}

	// XYOffset
	if (XYOffsetmapTexture)
	{
		FStreamingRenderAssetPrimitiveInfo& StreamingXYOffset = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
		StreamingXYOffset.Bounds = BoundingSphere;
		StreamingXYOffset.TexelFactor = TexelFactor;
		StreamingXYOffset.RenderAsset = XYOffsetmapTexture;
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		if (EditToolRenderData.DataTexture)
		{
			FStreamingRenderAssetPrimitiveInfo& StreamingDatamap = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
			StreamingDatamap.Bounds = BoundingSphere;
			StreamingDatamap.TexelFactor = TexelFactor;
			StreamingDatamap.RenderAsset = EditToolRenderData.DataTexture;
		}

		if (EditToolRenderData.LayerContributionTexture)
		{
			FStreamingRenderAssetPrimitiveInfo& StreamingDatamap = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
			StreamingDatamap.Bounds = BoundingSphere;
			StreamingDatamap.TexelFactor = TexelFactor;
			StreamingDatamap.RenderAsset = EditToolRenderData.LayerContributionTexture;
		}

		if (EditToolRenderData.DirtyTexture)
		{
			FStreamingRenderAssetPrimitiveInfo& StreamingDatamap = *new(OutStreamingRenderAssets)FStreamingRenderAssetPrimitiveInfo;
			StreamingDatamap.Bounds = BoundingSphere;
			StreamingDatamap.TexelFactor = TexelFactor;
			StreamingDatamap.RenderAsset = EditToolRenderData.DirtyTexture;
		}
	}
#endif

	if (LODStreamingProxy && LODStreamingProxy->IsStreamable())
	{
		const float MeshTexelFactor = ForcedLOD >= 0 ?
			-FMath::Max<int32>(LODStreamingProxy->GetStreamableResourceState().MaxNumLODs - ForcedLOD, 1) :
			(IsRegistered() ? Bounds.SphereRadius * 2.f : 0.f);
		new (OutStreamingRenderAssets) FStreamingRenderAssetPrimitiveInfo(LODStreamingProxy, Bounds, MeshTexelFactor, PackedRelativeBox_Identity, true);
	}
}

void ALandscapeProxy::ChangeTessellationComponentScreenSize(float InTessellationComponentScreenSize)
{
	TessellationComponentScreenSize = FMath::Clamp<float>(InTessellationComponentScreenSize, 0.01f, 1.0f);

	if (LandscapeComponents.Num() > 0)
	{
		int32 ComponentCount = LandscapeComponents.Num();
		FLandscapeComponentSceneProxy** RenderProxies = new FLandscapeComponentSceneProxy*[ComponentCount];
		for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
		{
			RenderProxies[Idx] = (FLandscapeComponentSceneProxy*)(LandscapeComponents[Idx]->SceneProxy);
		}

		float TessellationComponentScreenSizeLocal = TessellationComponentScreenSize;
		ENQUEUE_RENDER_COMMAND(LandscapeChangeTessellationComponentScreenSizeCommand)(
			[RenderProxies, ComponentCount, TessellationComponentScreenSizeLocal](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
				{
					if (RenderProxies[Idx] != nullptr)
					{
						RenderProxies[Idx]->ChangeTessellationComponentScreenSize_RenderThread(TessellationComponentScreenSizeLocal);
					}
				}

				delete[] RenderProxies;
			}
		);
	}
}

void ALandscapeProxy::ChangeComponentScreenSizeToUseSubSections(float InComponentScreenSizeToUseSubSections)
{
	ComponentScreenSizeToUseSubSections = FMath::Clamp<float>(InComponentScreenSizeToUseSubSections, 0.01f, 1.0f);

	if (LandscapeComponents.Num() > 0)
	{
		int32 ComponentCount = LandscapeComponents.Num();
		FLandscapeComponentSceneProxy** RenderProxies = new FLandscapeComponentSceneProxy*[ComponentCount];
		for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
		{
			RenderProxies[Idx] = (FLandscapeComponentSceneProxy*)(LandscapeComponents[Idx]->SceneProxy);
		}

		float ComponentScreenSizeToUseSubSectionsLocal = ComponentScreenSizeToUseSubSections;
		ENQUEUE_RENDER_COMMAND(LandscapeChangeComponentScreenSizeToUseSubSectionsCommand)(
			[RenderProxies, ComponentCount, ComponentScreenSizeToUseSubSectionsLocal](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
				{
					if (RenderProxies[Idx] != nullptr)
					{
						RenderProxies[Idx]->ChangeComponentScreenSizeToUseSubSections_RenderThread(ComponentScreenSizeToUseSubSectionsLocal);
					}
				}

				delete[] RenderProxies;
			}
		);
	}
}

void ALandscapeProxy::ChangeUseTessellationComponentScreenSizeFalloff(bool InUseTessellationComponentScreenSizeFalloff)
{
	UseTessellationComponentScreenSizeFalloff = InUseTessellationComponentScreenSizeFalloff;

	if (LandscapeComponents.Num() > 0)
	{
		int32 ComponentCount = LandscapeComponents.Num();
		FLandscapeComponentSceneProxy** RenderProxies = new FLandscapeComponentSceneProxy*[ComponentCount];
		for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
		{
			RenderProxies[Idx] = (FLandscapeComponentSceneProxy*)(LandscapeComponents[Idx]->SceneProxy);
		}

		ENQUEUE_RENDER_COMMAND(LandscapeChangeUseTessellationComponentScreenSizeFalloffCommand)(
			[RenderProxies, ComponentCount, InUseTessellationComponentScreenSizeFalloff](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
				{
					if (RenderProxies[Idx] != nullptr)
					{
						RenderProxies[Idx]->ChangeUseTessellationComponentScreenSizeFalloff_RenderThread(InUseTessellationComponentScreenSizeFalloff);
					}
				}

				delete[] RenderProxies;
			}
		);
	}
}

void ALandscapeProxy::ChangeTessellationComponentScreenSizeFalloff(float InTessellationComponentScreenSizeFalloff)
{
	TessellationComponentScreenSizeFalloff = FMath::Clamp<float>(TessellationComponentScreenSizeFalloff, 0.01f, 1.0f);

	if (LandscapeComponents.Num() > 0)
	{
		int32 ComponentCount = LandscapeComponents.Num();
		FLandscapeComponentSceneProxy** RenderProxies = new FLandscapeComponentSceneProxy*[ComponentCount];
		for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
		{
			RenderProxies[Idx] = (FLandscapeComponentSceneProxy*)(LandscapeComponents[Idx]->SceneProxy);
		}

		float TessellationComponentScreenSizeFalloffLocal = TessellationComponentScreenSizeFalloff;
		ENQUEUE_RENDER_COMMAND(LandscapeChangeTessellationComponentScreenSizeFalloffCommand)(
			[RenderProxies, ComponentCount, TessellationComponentScreenSizeFalloffLocal](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 Idx = 0; Idx < ComponentCount; ++Idx)
				{
					if (RenderProxies[Idx] != nullptr)
					{
						RenderProxies[Idx]->ChangeTessellationComponentScreenSizeFalloff_RenderThread(TessellationComponentScreenSizeFalloffLocal);
					}
				}

				delete[] RenderProxies;
			}
		);
	}
}

void ALandscapeProxy::ChangeLODDistanceFactor(float InLODDistanceFactor)
{
	// Deprecated
}

void FLandscapeComponentSceneProxy::ChangeTessellationComponentScreenSize_RenderThread(float InTessellationComponentScreenSize)
{
	TessellationComponentSquaredScreenSize = FMath::Square(InTessellationComponentScreenSize);
}

void FLandscapeComponentSceneProxy::ChangeComponentScreenSizeToUseSubSections_RenderThread(float InComponentScreenSizeToUseSubSections)
{
	ComponentSquaredScreenSizeToUseSubSections = FMath::Square(InComponentScreenSizeToUseSubSections);
}

void FLandscapeComponentSceneProxy::ChangeUseTessellationComponentScreenSizeFalloff_RenderThread(bool InUseTessellationComponentScreenSizeFalloff)
{
	UseTessellationComponentScreenSizeFalloff = InUseTessellationComponentScreenSizeFalloff;
}

void FLandscapeComponentSceneProxy::ChangeTessellationComponentScreenSizeFalloff_RenderThread(float InTessellationComponentScreenSizeFalloff)
{
	TessellationComponentScreenSizeFalloff = InTessellationComponentScreenSizeFalloff;
}

bool FLandscapeComponentSceneProxy::HeightfieldHasPendingStreaming() const
{
	return HeightmapTexture && HeightmapTexture->bHasStreamingUpdatePending;
}

void FLandscapeComponentSceneProxy::GetHeightfieldRepresentation(UTexture2D*& OutHeightmapTexture, UTexture2D*& OutDiffuseColorTexture, UTexture2D*& OutVisibilityTexture, FHeightfieldComponentDescription& OutDescription)
{
	OutHeightmapTexture = HeightmapTexture;
	OutDiffuseColorTexture = BaseColorForGITexture;
	OutVisibilityTexture = VisibilityWeightmapTexture;
	
	OutDescription.HeightfieldScaleBias = HeightmapScaleBias;

	OutDescription.MinMaxUV = FVector4(
		HeightmapScaleBias.Z,
		HeightmapScaleBias.W,
		HeightmapScaleBias.Z + SubsectionSizeVerts * NumSubsections * HeightmapScaleBias.X - HeightmapScaleBias.X,
		HeightmapScaleBias.W + SubsectionSizeVerts * NumSubsections * HeightmapScaleBias.Y - HeightmapScaleBias.Y);

	OutDescription.HeightfieldRect = FIntRect(SectionBase.X, SectionBase.Y, SectionBase.X + NumSubsections * SubsectionSizeQuads, SectionBase.Y + NumSubsections * SubsectionSizeQuads);

	OutDescription.NumSubsections = NumSubsections;

	OutDescription.SubsectionScaleAndBias = FVector4(SubsectionSizeQuads, SubsectionSizeQuads, HeightmapSubsectionOffsetU, HeightmapSubsectionOffsetV);

	OutDescription.VisibilityChannel = VisibilityWeightmapChannel;
}

void FLandscapeComponentSceneProxy::GetLCIs(FLCIArray& LCIs)
{
	FLightCacheInterface* LCI = ComponentLightInfo.Get();
	if (LCI)
	{
		LCIs.Push(LCI);
	}
}

//
// FLandscapeNeighborInfo
//
void FLandscapeNeighborInfo::RegisterNeighbors(FLandscapeComponentSceneProxy* SceneProxy /* = nullptr */)
{
	check(IsInRenderingThread());
	if (!bRegistered)
	{
		if (!SharedSceneProxyMap.Find(LandscapeKey))
		{
			LandscapeRenderSystems.Add(LandscapeKey, new FLandscapeRenderSystem {});

			GetRendererModule().RegisterPersistentViewUniformBufferExtension(&LandscapePersistentViewUniformBufferExtension);
		}

		// Register ourselves in the map.
		TMap<FIntPoint, const FLandscapeNeighborInfo*>& SceneProxyMap = SharedSceneProxyMap.FindOrAdd(LandscapeKey);

		const FLandscapeNeighborInfo* Existing = SceneProxyMap.FindRef(ComponentBase);
		if (Existing == nullptr)//(ensure(Existing == nullptr))
		{
			SceneProxyMap.Add(ComponentBase, this);
			bRegistered = true;

			// Find Neighbors
			Neighbors[0] = SceneProxyMap.FindRef(ComponentBase + FIntPoint(0, -1));
			Neighbors[1] = SceneProxyMap.FindRef(ComponentBase + FIntPoint(-1, 0));
			Neighbors[2] = SceneProxyMap.FindRef(ComponentBase + FIntPoint(1, 0));
			Neighbors[3] = SceneProxyMap.FindRef(ComponentBase + FIntPoint(0, 1));

			// Add ourselves to our neighbors
			if (Neighbors[0])
			{
				Neighbors[0]->Neighbors[3] = this;
			}
			if (Neighbors[1])
			{
				Neighbors[1]->Neighbors[2] = this;
			}
			if (Neighbors[2])
			{
				Neighbors[2]->Neighbors[1] = this;
			}
			if (Neighbors[3])
			{
				Neighbors[3]->Neighbors[0] = this;
			}

			if (SceneProxy != nullptr)
			{
				FLandscapeRenderSystem& RenderSystem = *LandscapeRenderSystems.FindChecked(LandscapeKey);
				RenderSystem.RegisterEntity(SceneProxy);
			}
		}
		else
		{
			UE_LOG(LogLandscape, Warning, TEXT("Duplicate ComponentBase %d, %d"), ComponentBase.X, ComponentBase.Y);
		}
	}
}

void FLandscapeNeighborInfo::UnregisterNeighbors(FLandscapeComponentSceneProxy* SceneProxy /* = nullptr */)
{
	check(IsInRenderingThread());
	
	if (bRegistered)
	{
		// Remove ourselves from the map
		TMap<FIntPoint, const FLandscapeNeighborInfo*>* SceneProxyMap = SharedSceneProxyMap.Find(LandscapeKey);
		check(SceneProxyMap);

		const FLandscapeNeighborInfo* MapEntry = SceneProxyMap->FindRef(ComponentBase);
		if (MapEntry == this) //(/*ensure*/(MapEntry == this))
		{
			SceneProxyMap->Remove(ComponentBase);

			if (SceneProxy != nullptr)
			{
				FLandscapeRenderSystem& RenderSystem = *LandscapeRenderSystems.FindChecked(LandscapeKey);
				RenderSystem.UnregisterEntity(SceneProxy);
			}

			if (SceneProxyMap->Num() == 0)
			{
				FLandscapeRenderSystem* RenderSystemPtr = LandscapeRenderSystems.FindChecked(LandscapeKey);
				check(RenderSystemPtr->NumRegisteredEntities == 0);

				delete RenderSystemPtr;
				LandscapeRenderSystems.Remove(LandscapeKey);

				// remove the entire LandscapeKey entry as this is the last scene proxy
				SharedSceneProxyMap.Remove(LandscapeKey);
			}
			else
			{
				// remove reference to us from our neighbors
				if (Neighbors[0])
				{
					Neighbors[0]->Neighbors[3] = nullptr;
				}
				if (Neighbors[1])
				{
					Neighbors[1]->Neighbors[2] = nullptr;
				}
				if (Neighbors[2])
				{
					Neighbors[2]->Neighbors[1] = nullptr;
				}
				if (Neighbors[3])
				{
					Neighbors[3]->Neighbors[0] = nullptr;
				}
			}
		}
	}
}

//
// FLandscapeMeshProxySceneProxy
//
FLandscapeMeshProxySceneProxy::FLandscapeMeshProxySceneProxy(UStaticMeshComponent* InComponent, const FGuid& InGuid, const TArray<FIntPoint>& InProxyComponentBases, int8 InProxyLOD)
	: FStaticMeshSceneProxy(InComponent, false)
{
	if (!IsComponentLevelVisible())
	{
		bNeedsLevelAddedToWorldNotification = true;
	}

	ProxyNeighborInfos.Empty(InProxyComponentBases.Num());
	for (FIntPoint ComponentBase : InProxyComponentBases)
	{
		new(ProxyNeighborInfos) FLandscapeNeighborInfo(InComponent->GetWorld(), InGuid, ComponentBase, nullptr, InProxyLOD, 0);
	}
}

SIZE_T FLandscapeMeshProxySceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}


void FLandscapeMeshProxySceneProxy::CreateRenderThreadResources()
{
	FStaticMeshSceneProxy::CreateRenderThreadResources();

	if (IsComponentLevelVisible())
	{
		for (FLandscapeNeighborInfo& Info : ProxyNeighborInfos)
		{
			Info.RegisterNeighbors();
		}
	}
}

void FLandscapeMeshProxySceneProxy::OnLevelAddedToWorld()
{
	for (FLandscapeNeighborInfo& Info : ProxyNeighborInfos)
	{
		Info.RegisterNeighbors();
	}
}

void FLandscapeMeshProxySceneProxy::DestroyRenderThreadResources()
{
	FStaticMeshSceneProxy::DestroyRenderThreadResources();

	for (FLandscapeNeighborInfo& Info : ProxyNeighborInfos)
	{
		Info.UnregisterNeighbors();
	}
}

FPrimitiveSceneProxy* ULandscapeMeshProxyComponent::CreateSceneProxy()
{
	if (GetStaticMesh() == NULL
		|| GetStaticMesh()->RenderData == NULL
		|| GetStaticMesh()->RenderData->LODResources.Num() == 0
		|| GetStaticMesh()->RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() == 0)
	{
		return NULL;
	}

	return new FLandscapeMeshProxySceneProxy(this, LandscapeGuid, ProxyComponentBases, ProxyLOD);
}
