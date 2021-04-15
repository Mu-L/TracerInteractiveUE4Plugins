// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene.h"
#include "GPULightmass.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Async/Async.h"
#include "LightmapEncoding.h"
#include "GPULightmassCommon.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/ScopedSlowTask.h"
#include "LightmapPreviewVirtualTexture.h"
#include "EngineModule.h"
#include "LightmapRenderer.h"
#include "VolumetricLightmap.h"
#include "GPUScene.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Logging/MessageLog.h"
#include "Misc/ConfigCacheIni.h"
#include "LevelEditorViewport.h"
#include "Editor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "LightmapDenoising.h"
#include "Misc/FileHelper.h"
#include "Components/ReflectionCaptureComponent.h"

#define LOCTEXT_NAMESPACE "StaticLightingSystem"

extern RENDERER_API void SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance(FVector4* OutSkyIrradianceEnvironmentMap, const FSHVectorRGB3 SkyIrradiance);
extern ENGINE_API bool GCompressLightmaps;

extern float GetTerrainExpandPatchCount(float LightMapRes, int32& X, int32& Y, int32 ComponentSize, int32 LightmapSize, int32& DesiredSize, uint32 LightingLOD);

namespace GPULightmass
{

FScene::FScene(FGPULightmass* InGPULightmass)
	: GPULightmass(InGPULightmass)
	, Settings(InGPULightmass->Settings)
	, Geometries(*this)
{
	StaticMeshInstances.LinkRenderStateArray(RenderState.StaticMeshInstanceRenderStates);
	InstanceGroups.LinkRenderStateArray(RenderState.InstanceGroupRenderStates);
	Landscapes.LinkRenderStateArray(RenderState.LandscapeRenderStates);

	RenderState.Settings = Settings;

	ENQUEUE_RENDER_COMMAND(RenderThreadInit)(
		[&RenderState = RenderState](FRHICommandListImmediate&) mutable
	{
		RenderState.RenderThreadInit();
	});
}

void FSceneRenderState::RenderThreadInit()
{
	check(IsInRenderingThread());

	LightmapRenderer = MakeUnique<FLightmapRenderer>(this);
	VolumetricLightmapRenderer = MakeUnique<FVolumetricLightmapRenderer>(this);
	IrradianceCache = MakeUnique<FIrradianceCache>(Settings->IrradianceCacheQuality, Settings->IrradianceCacheSpacing, Settings->IrradianceCacheCornerRejection);
	IrradianceCache->CurrentRevision = LightmapRenderer->GetCurrentRevision();
}

const FMeshMapBuildData* FScene::GetComponentLightmapData(const UPrimitiveComponent* InComponent, int32 LODIndex)
{
	if (const ULandscapeComponent* LandscapeComponent = Cast<const ULandscapeComponent>(InComponent))
	{
		if (RegisteredLandscapeComponentUObjects.Contains(LandscapeComponent))
		{
			FLandscapeRef Instance = RegisteredLandscapeComponentUObjects[LandscapeComponent];

			return Instance->GetMeshMapBuildDataForLODIndex(LODIndex);
		}
	}
	else if (const UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<const UInstancedStaticMeshComponent>(InComponent))
	{
		if (RegisteredInstancedStaticMeshComponentUObjects.Contains(InstancedStaticMeshComponent))
		{
			FInstanceGroupRef Instance = RegisteredInstancedStaticMeshComponentUObjects[InstancedStaticMeshComponent];

			return Instance->GetMeshMapBuildDataForLODIndex(LODIndex);
		}
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<const UStaticMeshComponent>(InComponent))
	{
		if (RegisteredStaticMeshComponentUObjects.Contains(StaticMeshComponent))
		{
			FStaticMeshInstanceRef Instance = RegisteredStaticMeshComponentUObjects[StaticMeshComponent];

			return Instance->GetMeshMapBuildDataForLODIndex(LODIndex);
		}
	}

	return nullptr;
}

const FLightComponentMapBuildData* FScene::GetComponentLightmapData(const ULightComponent* InComponent)
{
	if (const UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(InComponent))
	{
		if (LightScene.RegisteredDirectionalLightComponentUObjects.Contains(DirectionalLight))
		{
			return LightScene.RegisteredDirectionalLightComponentUObjects[DirectionalLight]->LightComponentMapBuildData.Get();
		}
	}
	else if (const URectLightComponent* RectLight = Cast<URectLightComponent>(InComponent))
	{
		if (LightScene.RegisteredRectLightComponentUObjects.Contains(RectLight))
		{
			return LightScene.RegisteredRectLightComponentUObjects[RectLight]->LightComponentMapBuildData.Get();
		}
	}
	else if (const USpotLightComponent* SpotLight = Cast<USpotLightComponent>(InComponent))
	{
		if (LightScene.RegisteredSpotLightComponentUObjects.Contains(SpotLight))
		{
			return LightScene.RegisteredSpotLightComponentUObjects[SpotLight]->LightComponentMapBuildData.Get();
		}
	}
	else if (const UPointLightComponent* PointLight = Cast<UPointLightComponent>(InComponent))
	{
		if (LightScene.RegisteredPointLightComponentUObjects.Contains(PointLight))
		{
			return LightScene.RegisteredPointLightComponentUObjects[PointLight]->LightComponentMapBuildData.Get();
		}
	}

	return nullptr;
}

void FScene::GatherImportanceVolumes()
{
	FBox CombinedImportanceVolume(ForceInit);
	TArray<FBox> ImportanceVolumes;

	for (TObjectIterator<ALightmassImportanceVolume> It; It; ++It)
	{
		ALightmassImportanceVolume* LMIVolume = *It;
		if (GPULightmass->World->ContainsActor(LMIVolume) && !LMIVolume->IsPendingKill())
		{
			CombinedImportanceVolume += LMIVolume->GetComponentsBoundingBox(true);
			ImportanceVolumes.Add(LMIVolume->GetComponentsBoundingBox(true));
		}
	}

	if (CombinedImportanceVolume.GetExtent().SizeSquared() == 0)
	{
		float MinimumImportanceVolumeExtentWithoutWarning = 0.0f;
		verify(GConfig->GetFloat(TEXT("DevOptions.StaticLightingSceneConstants"), TEXT("MinimumImportanceVolumeExtentWithoutWarning"), MinimumImportanceVolumeExtentWithoutWarning, GLightmassIni));

		FBox AutomaticImportanceVolumeBounds(ForceInit);

		for (FGeometryAndItsArray GeomIt : Geometries)
		{
			FGeometry& Geometry = GeomIt.GetGeometry();

			if (Geometry.bCastShadow)
			{
				AutomaticImportanceVolumeBounds += Geometry.WorldBounds.GetBox();
			}
		}

		FBox ReasonableSceneBounds = AutomaticImportanceVolumeBounds;
		if (ReasonableSceneBounds.GetExtent().SizeSquared() > (MinimumImportanceVolumeExtentWithoutWarning * MinimumImportanceVolumeExtentWithoutWarning))
		{
			// Emit a serious warning to the user about performance.
			FMessageLog("LightingResults").PerformanceWarning(LOCTEXT("LightmassError_MissingImportanceVolume", "No importance volume found and the scene is so large that the automatically synthesized volume will not yield good results.  Please add a tightly bounding lightmass importance volume to optimize your scene's quality and lighting build times."));

			// Clamp the size of the importance volume we create to a reasonable size
			ReasonableSceneBounds = FBox(ReasonableSceneBounds.GetCenter() - MinimumImportanceVolumeExtentWithoutWarning, ReasonableSceneBounds.GetCenter() + MinimumImportanceVolumeExtentWithoutWarning);
		}
		else
		{
			// The scene isn't too big, so we'll use the scene's bounds as a synthetic importance volume
			// NOTE: We don't want to pop up a message log for this common case when creating a new level, so we just spray a log message.  It's not very important to a user.
			UE_LOG(LogGPULightmass, Warning, TEXT("No importance volume found, so the scene bounding box was used.  You can optimize your scene's quality and lighting build times by adding importance volumes."));

			float AutomaticImportanceVolumeExpandBy = 0.0f;
			verify(GConfig->GetFloat(TEXT("DevOptions.StaticLightingSceneConstants"), TEXT("AutomaticImportanceVolumeExpandBy"), AutomaticImportanceVolumeExpandBy, GLightmassIni));

			// Expand the scene's bounds a bit to make sure volume lighting samples placed on surfaces are inside
			ReasonableSceneBounds = ReasonableSceneBounds.ExpandBy(AutomaticImportanceVolumeExpandBy);
		}

		CombinedImportanceVolume = ReasonableSceneBounds;
		ImportanceVolumes.Add(ReasonableSceneBounds);
	}

	float TargetDetailCellSize = GPULightmass->World->GetWorldSettings()->LightmassSettings.VolumetricLightmapDetailCellSize;

	ENQUEUE_RENDER_COMMAND(UpdateVLMRendererVolume)([&RenderState = RenderState, CombinedImportanceVolume, ImportanceVolumes, TargetDetailCellSize](FRHICommandList&) mutable {
		RenderState.VolumetricLightmapRenderer->CombinedImportanceVolume = CombinedImportanceVolume;
		RenderState.VolumetricLightmapRenderer->ImportanceVolumes = ImportanceVolumes;
		RenderState.VolumetricLightmapRenderer->TargetDetailCellSize = TargetDetailCellSize;
	});
}

FGeometryIterator FGeometryRange::begin()
{
	TArray<FGeometryArrayBase*> Arrays { &Scene.StaticMeshInstances, &Scene.InstanceGroups, &Scene.Landscapes };
	int StartIndex = 0;
	while (StartIndex < Arrays.Num() && Arrays[StartIndex]->Num() == 0)
	{
		StartIndex++;
	}
	return FGeometryIterator { 0, Arrays, StartIndex };
}

FGeometryIterator FGeometryRange::end()
{
	return FGeometryIterator { Scene.Landscapes.Num(), {&Scene.StaticMeshInstances, &Scene.InstanceGroups, &Scene.Landscapes}, 3 };
}

void AddLightToLightmap(
	FLightmap& Lightmap,
	FLocalLightBuildInfo& Light)
{
	// For both static and stationary lights
	Lightmap.LightmapObject->LightGuids.Add(Light.GetComponentUObject()->LightGuid);

	if (Light.bStationary)
	{
		Lightmap.NumStationaryLightsPerShadowChannel[Light.ShadowMapChannel]++;
		Lightmap.LightmapObject->bShadowChannelValid[Light.ShadowMapChannel] = true;
		// TODO: implement SDF. For area lights and invalid channels this will be fixed to 1
		Lightmap.LightmapObject->InvUniformPenumbraSize[Light.ShadowMapChannel] = 1.0f / Light.GetComponentUObject()->GetUniformPenumbraSize();

		// TODO: needs GPUScene update to reflect penumbra size changes
	}
}

void RemoveLightFromLightmap(
	FLightmap& Lightmap,
	FLocalLightBuildInfo& Light)
{
	Lightmap.LightmapObject->LightGuids.Remove(Light.GetComponentUObject()->LightGuid);

	if (Light.bStationary)
	{
		Lightmap.NumStationaryLightsPerShadowChannel[Light.ShadowMapChannel]--;

		if (Lightmap.NumStationaryLightsPerShadowChannel[Light.ShadowMapChannel] == 0)
		{
			Lightmap.LightmapObject->bShadowChannelValid[Light.ShadowMapChannel] = false;
			Lightmap.LightmapObject->InvUniformPenumbraSize[Light.ShadowMapChannel] = 1.0f;
		}
	}
}
template<typename LightComponentType>
struct LightTypeInfo
{
};

template<>
struct LightTypeInfo<UDirectionalLightComponent>
{
	using BuildInfoType = FDirectionalLightBuildInfo;
	using LightRefType = FDirectionalLightRef;
	using RenderStateType = FDirectionalLightRenderState;
	using RenderStateRefType = FDirectionalLightRenderStateRef;

	using LightComponentRegistrationType = TMap<UDirectionalLightComponent*, LightRefType>;
	inline static LightComponentRegistrationType& GetLightComponentRegistration(FLightScene& LightScene)
	{
		return LightScene.RegisteredDirectionalLightComponentUObjects;
	}

	using LightArrayType = TLightArray<BuildInfoType>;
	inline static LightArrayType& GetLightArray(FLightScene& LightScene)
	{
		return LightScene.DirectionalLights;
	}

	using LightRenderStateArrayType = TLightRenderStateArray<RenderStateType>;
	inline static LightRenderStateArrayType& GetLightRenderStateArray(FLightSceneRenderState& LightSceneRenderState)
	{
		return LightSceneRenderState.DirectionalLights;
	}
};

template<>
struct LightTypeInfo<UPointLightComponent>
{
	using BuildInfoType = FPointLightBuildInfo;
	using LightRefType = FPointLightRef;
	using RenderStateType = FPointLightRenderState;
	using RenderStateRefType = FPointLightRenderStateRef;

	using LightComponentRegistrationType = TMap<UPointLightComponent*, LightRefType>;
	inline static LightComponentRegistrationType& GetLightComponentRegistration(FLightScene& LightScene)
	{
		return LightScene.RegisteredPointLightComponentUObjects;
	}

	using LightArrayType = TLightArray<BuildInfoType>;
	inline static LightArrayType& GetLightArray(FLightScene& LightScene)
	{
		return LightScene.PointLights;
	}

	using LightRenderStateArrayType = TLightRenderStateArray<RenderStateType>;
	inline static LightRenderStateArrayType& GetLightRenderStateArray(FLightSceneRenderState& LightSceneRenderState)
	{
		return LightSceneRenderState.PointLights;
	}
};

template<>
struct LightTypeInfo<USpotLightComponent>
{
	using BuildInfoType = FSpotLightBuildInfo;
	using LightRefType = FSpotLightRef;
	using RenderStateType = FSpotLightRenderState;
	using RenderStateRefType = FSpotLightRenderStateRef;

	using LightComponentRegistrationType = TMap<USpotLightComponent*, LightRefType>;
	inline static LightComponentRegistrationType& GetLightComponentRegistration(FLightScene& LightScene)
	{
		return LightScene.RegisteredSpotLightComponentUObjects;
	}

	using LightArrayType = TLightArray<BuildInfoType>;
	inline static LightArrayType& GetLightArray(FLightScene& LightScene)
	{
		return LightScene.SpotLights;
	}

	using LightRenderStateArrayType = TLightRenderStateArray<RenderStateType>;
	inline static LightRenderStateArrayType& GetLightRenderStateArray(FLightSceneRenderState& LightSceneRenderState)
	{
		return LightSceneRenderState.SpotLights;
	}
};

template<>
struct LightTypeInfo<URectLightComponent>
{
	using BuildInfoType = FRectLightBuildInfo;
	using LightRefType = FRectLightRef;
	using RenderStateType = FRectLightRenderState;
	using RenderStateRefType = FRectLightRenderStateRef;

	using LightComponentRegistrationType = TMap<URectLightComponent*, LightRefType>;
	inline static LightComponentRegistrationType& GetLightComponentRegistration(FLightScene& LightScene)
	{
		return LightScene.RegisteredRectLightComponentUObjects;
	}

	using LightArrayType = TLightArray<BuildInfoType>;
	inline static LightArrayType& GetLightArray(FLightScene& LightScene)
	{
		return LightScene.RectLights;
	}

	using LightRenderStateArrayType = TLightRenderStateArray<RenderStateType>;
	inline static LightRenderStateArrayType& GetLightRenderStateArray(FLightSceneRenderState& LightSceneRenderState)
	{
		return LightSceneRenderState.RectLights;
	}
};

template<typename LightComponentType>
void FScene::AddLight(LightComponentType* PointLightComponent)
{
	if (LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene).Contains(PointLightComponent))
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: duplicated component registration"));
		return;
	}

	const bool bCastStationaryShadows = PointLightComponent->CastShadows && PointLightComponent->CastStaticShadows && !PointLightComponent->HasStaticLighting();

	if (bCastStationaryShadows)
	{
		if (PointLightComponent->PreviewShadowMapChannel == INDEX_NONE)
		{
			UE_LOG(LogGPULightmass, Log, TEXT("Ignoring light with ShadowMapChannel == -1 (probably in the middle of SpawnActor)"));
			return;
		}
	}

	typename LightTypeInfo<LightComponentType>::BuildInfoType Light(PointLightComponent);

	typename LightTypeInfo<LightComponentType>::LightRefType LightRef = LightTypeInfo<LightComponentType>::GetLightArray(LightScene).Emplace(MoveTemp(Light));
	LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene).Add(PointLightComponent, LightRef);

	typename LightTypeInfo<LightComponentType>::RenderStateType LightRenderState(PointLightComponent);

	TArray<FPrimitiveSceneProxy*> SceneProxiesToUpdateOnRenderThread;
	TArray<FGeometryRenderStateToken> RelevantGeometriesToUpdateOnRenderThread;

	for (FGeometryAndItsArray GeomIt : Geometries)
	{
		FGeometry& Geometry = GeomIt.GetGeometry();

		if (Light.AffectsBounds(Geometry.WorldBounds))
		{
			if (Light.bStationary)
			{
				RelevantGeometriesToUpdateOnRenderThread.Add({ GeomIt.Index, GeomIt.Array.GetRenderStateArray() });
			}

			for (FLightmapRef& Lightmap : Geometry.LODLightmaps)
			{
				if (Lightmap.IsValid())
				{
					AddLightToLightmap(Lightmap.GetReference_Unsafe(), Light);
				}
			}

			if (Geometry.GetComponentUObject()->SceneProxy)
			{
				SceneProxiesToUpdateOnRenderThread.Add(Geometry.GetComponentUObject()->SceneProxy);
			}
		}
	}

	ENQUEUE_RENDER_COMMAND(UpdateStaticLightingBufferCmd)(
		[SceneProxiesToUpdateOnRenderThread = MoveTemp(SceneProxiesToUpdateOnRenderThread)](FRHICommandListImmediate& RHICmdList)
	{
		for (FPrimitiveSceneProxy* SceneProxy : SceneProxiesToUpdateOnRenderThread)
		{
			if (SceneProxy->GetPrimitiveSceneInfo() && SceneProxy->GetPrimitiveSceneInfo()->IsIndexValid())
			{
				SceneProxy->GetPrimitiveSceneInfo()->UpdateStaticLightingBuffer();
				AddPrimitiveToUpdateGPU(*SceneProxy->GetPrimitiveSceneInfo()->Scene, SceneProxy->GetPrimitiveSceneInfo()->GetIndex());
			}
		}
	});

	ENQUEUE_RENDER_COMMAND(RenderThreadUpdate)(
		[
			&RenderState = RenderState,
			LightRenderState = MoveTemp(LightRenderState),
			RelevantGeometriesToUpdateOnRenderThread
		](FRHICommandListImmediate& RHICmdList) mutable
	{
		typename LightTypeInfo<LightComponentType>::RenderStateRefType LightRenderStateRef = LightTypeInfo<LightComponentType>::GetLightRenderStateArray(RenderState.LightSceneRenderState).Emplace(MoveTemp(LightRenderState));

		for (FGeometryRenderStateToken Token : RelevantGeometriesToUpdateOnRenderThread)
		{
			for (FLightmapRenderStateRef& Lightmap : Token.RenderStateArray->Get(Token.ElementId).LODLightmapRenderStates)
			{
				if (Lightmap.IsValid())
				{
					Lightmap->AddRelevantLight(LightRenderStateRef);
				}
			}
		}
	});

	ENQUEUE_RENDER_COMMAND(InvalidateRevision)([&RenderState = RenderState](FRHICommandListImmediate& RHICmdList) { 
		RenderState.LightmapRenderer->BumpRevision();
		RenderState.VolumetricLightmapRenderer->FrameNumber = 0;
		RenderState.VolumetricLightmapRenderer->SamplesTaken = 0;
	});
}

template void FScene::AddLight(UDirectionalLightComponent* LightComponent);
template void FScene::AddLight(UPointLightComponent* LightComponent);
template void FScene::AddLight(USpotLightComponent* LightComponent);
template void FScene::AddLight(URectLightComponent* LightComponent);

template<typename LightComponentType>
void FScene::RemoveLight(LightComponentType* PointLightComponent)
{
	if (!LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene).Contains(PointLightComponent))
	{
		return;
	}

	typename LightTypeInfo<LightComponentType>::LightRefType Light = LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene)[PointLightComponent];

	TArray<FPrimitiveSceneProxy*> SceneProxiesToUpdateOnRenderThread;
	TArray<FGeometryRenderStateToken> RelevantGeometriesToUpdateOnRenderThread;

	for (FGeometryAndItsArray GeomIt : Geometries)
	{
		FGeometry& Geometry = GeomIt.GetGeometry();

		if (Light->AffectsBounds(Geometry.WorldBounds))
		{
			if (Light->bStationary)
			{
				RelevantGeometriesToUpdateOnRenderThread.Add({ GeomIt.Index, GeomIt.Array.GetRenderStateArray() });
			}

			for (FLightmapRef& Lightmap : Geometry.LODLightmaps)
			{
				if (Lightmap.IsValid())
				{
					RemoveLightFromLightmap(Lightmap.GetReference_Unsafe(), Light);
				}
			}

			if (Geometry.GetComponentUObject()->SceneProxy)
			{
				SceneProxiesToUpdateOnRenderThread.Add(Geometry.GetComponentUObject()->SceneProxy);
			}
		}
	}

	ENQUEUE_RENDER_COMMAND(UpdateStaticLightingBufferCmd)(
		[SceneProxiesToUpdateOnRenderThread = MoveTemp(SceneProxiesToUpdateOnRenderThread)](FRHICommandListImmediate& RHICmdList)
	{
		for (FPrimitiveSceneProxy* SceneProxy : SceneProxiesToUpdateOnRenderThread)
		{
			if (SceneProxy->GetPrimitiveSceneInfo() && SceneProxy->GetPrimitiveSceneInfo()->IsIndexValid())
			{
				SceneProxy->GetPrimitiveSceneInfo()->UpdateStaticLightingBuffer();
				AddPrimitiveToUpdateGPU(*SceneProxy->GetPrimitiveSceneInfo()->Scene, SceneProxy->GetPrimitiveSceneInfo()->GetIndex());
			}
		}
	});

	int32 ElementId = Light.GetElementId();
	LightTypeInfo<LightComponentType>::GetLightArray(LightScene).RemoveAt(ElementId);
	LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene).Remove(PointLightComponent);

	ENQUEUE_RENDER_COMMAND(RenderThreadUpdate)(
		[
			&RenderState = RenderState,
			RelevantGeometriesToUpdateOnRenderThread,
			ElementId
		](FRHICommandListImmediate& RHICmdList) mutable
	{
		typename LightTypeInfo<LightComponentType>::RenderStateRefType LightRenderStateRef(LightTypeInfo<LightComponentType>::GetLightRenderStateArray(RenderState.LightSceneRenderState).Elements[ElementId], LightTypeInfo<LightComponentType>::GetLightRenderStateArray(RenderState.LightSceneRenderState));

		for (FGeometryRenderStateToken Token : RelevantGeometriesToUpdateOnRenderThread)
		{
			for (FLightmapRenderStateRef& Lightmap : Token.RenderStateArray->Get(Token.ElementId).LODLightmapRenderStates)
			{
				if (Lightmap.IsValid())
				{
					Lightmap->RemoveRelevantLight(LightRenderStateRef);
				}
			}
		}

		LightTypeInfo<LightComponentType>::GetLightRenderStateArray(RenderState.LightSceneRenderState).RemoveAt(ElementId);
	});

	ENQUEUE_RENDER_COMMAND(InvalidateRevision)([&RenderState = RenderState](FRHICommandListImmediate& RHICmdList) { RenderState.LightmapRenderer->BumpRevision(); });
}

template void FScene::RemoveLight(UDirectionalLightComponent* LightComponent);
template void FScene::RemoveLight(UPointLightComponent* LightComponent);
template void FScene::RemoveLight(USpotLightComponent* LightComponent);
template void FScene::RemoveLight(URectLightComponent* LightComponent);

template<typename LightComponentType>
bool FScene::HasLight(LightComponentType* PointLightComponent)
{
	return LightTypeInfo<LightComponentType>::GetLightComponentRegistration(LightScene).Contains(PointLightComponent);
}

template bool FScene::HasLight(UDirectionalLightComponent* LightComponent);
template bool FScene::HasLight(UPointLightComponent* LightComponent);
template bool FScene::HasLight(USpotLightComponent* LightComponent);
template bool FScene::HasLight(URectLightComponent* LightComponent);

void FScene::AddLight(USkyLightComponent* SkyLight)
{
	if (LightScene.SkyLight.IsSet() && LightScene.SkyLight->ComponentUObject == SkyLight)
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: duplicated component registration"));
		return;
	}

	if (!SkyLight->GetProcessedSkyTexture())
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Skipping skylight with empty cubemap"));
		return;
	}

	if (LightScene.SkyLight.IsSet())
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: trying to add more than one skylight - removing the old one"));
		RemoveLight(LightScene.SkyLight->ComponentUObject);
	}

	int32 LightId = INDEX_NONE;

	FSkyLightBuildInfo NewSkyLight;
	NewSkyLight.ComponentUObject = SkyLight;

	LightScene.SkyLight = MoveTemp(NewSkyLight);

	FSkyLightRenderState NewSkyLightRenderState;
	NewSkyLightRenderState.bStationary = !SkyLight->HasStaticLighting();
	NewSkyLightRenderState.Color = SkyLight->GetLightColor() * SkyLight->Intensity;
	NewSkyLightRenderState.TextureDimensions = FIntPoint(SkyLight->GetProcessedSkyTexture()->GetSizeX(), SkyLight->GetProcessedSkyTexture()->GetSizeY());
	NewSkyLightRenderState.IrradianceEnvironmentMap = SkyLight->GetIrradianceEnvironmentMap();
#if RHI_RAYTRACING
	NewSkyLightRenderState.ImportanceSamplingData = SkyLight->GetImportanceSamplingData();
#endif
	ENQUEUE_RENDER_COMMAND(AddLightRenderState)(
		[&RenderState = RenderState, NewSkyLightRenderState = MoveTemp(NewSkyLightRenderState), ProcessedSkyTexture = SkyLight->GetProcessedSkyTexture()](FRHICommandListImmediate& RHICmdList) mutable
	{
		// Dereferencing ProcessedSkyTexture must be deferred onto render thread
		NewSkyLightRenderState.ProcessedTexture = ProcessedSkyTexture->TextureRHI;
		NewSkyLightRenderState.ProcessedTextureSampler = ProcessedSkyTexture->SamplerStateRHI;

		NewSkyLightRenderState.SkyIrradianceEnvironmentMap.Initialize(sizeof(FVector4), 7, 0, TEXT("SkyIrradianceEnvironmentMap"));

		// Set the captured environment map data
		void* DataPtr = RHICmdList.LockStructuredBuffer(NewSkyLightRenderState.SkyIrradianceEnvironmentMap.Buffer, 0, NewSkyLightRenderState.SkyIrradianceEnvironmentMap.NumBytes, RLM_WriteOnly);
		SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance((FVector4*)DataPtr, NewSkyLightRenderState.IrradianceEnvironmentMap);
		RHICmdList.UnlockStructuredBuffer(NewSkyLightRenderState.SkyIrradianceEnvironmentMap.Buffer);

		RenderState.LightSceneRenderState.SkyLight = MoveTemp(NewSkyLightRenderState);

		RenderState.LightmapRenderer->BumpRevision();
	});
}

void FScene::RemoveLight(USkyLightComponent* SkyLight)
{
	if (!LightScene.SkyLight.IsSet() || LightScene.SkyLight->ComponentUObject != SkyLight)
	{
		return;
	}

	check(LightScene.SkyLight.IsSet());

	LightScene.SkyLight.Reset();

	ENQUEUE_RENDER_COMMAND(RemoveLightRenderState)(
		[&RenderState = RenderState](FRHICommandListImmediate& RHICmdList) mutable
	{
		RenderState.LightSceneRenderState.SkyLight.Reset();

		RenderState.LightmapRenderer->BumpRevision();
	});
}

template<typename LightType, typename GeometryRefType>
TArray<int32> AddAllPossiblyRelevantLightsToGeometry(
	TEntityArray<LightType>& LightArray,
	GeometryRefType Instance
)
{
	TArray<int32> RelevantLightsToAddOnRenderThread;

	for (LightType& Light : LightArray.Elements)
	{
		if (Light.AffectsBounds(Instance->WorldBounds))
		{
			if (Light.bStationary)
			{
				RelevantLightsToAddOnRenderThread.Add(&Light - LightArray.Elements.GetData());
			}

			for (FLightmapRef& Lightmap : Instance->LODLightmaps)
			{
				if (Lightmap.IsValid())
				{
					AddLightToLightmap(Lightmap.GetReference_Unsafe(), Light);
				}
			}
		}
	}

	return RelevantLightsToAddOnRenderThread;
}

void FScene::AddGeometryInstanceFromComponent(UStaticMeshComponent* InComponent)
{
	if (RegisteredStaticMeshComponentUObjects.Contains(InComponent))
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: duplicated component registration"));
		return;
	}
	
	FStaticMeshInstanceRef Instance = StaticMeshInstances.Emplace(InComponent);
	Instance->WorldBounds = InComponent->Bounds;
	Instance->bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;
	Instance->bLODsShareStaticLighting = InComponent->GetStaticMesh()->CanLODsShareStaticLighting();

	RegisteredStaticMeshComponentUObjects.Add(InComponent, Instance);

	const int32 SMCurrentMinLOD = InComponent->GetStaticMesh()->MinLOD.Default;
	int32 EffectiveMinLOD = InComponent->bOverrideMinLOD ? InComponent->MinLOD : SMCurrentMinLOD;

	// Find the first LOD with any vertices (ie that haven't been stripped)
	int FirstAvailableLOD = 0;
	for (; FirstAvailableLOD < InComponent->GetStaticMesh()->RenderData->LODResources.Num(); FirstAvailableLOD++)
	{
		if (InComponent->GetStaticMesh()->RenderData->LODResources[FirstAvailableLOD].GetNumVertices() > 0)
		{
			break;
		}
	}

	Instance->ClampedMinLOD = FMath::Clamp(EffectiveMinLOD, FirstAvailableLOD, InComponent->GetStaticMesh()->RenderData->LODResources.Num() - 1);

	Instance->AllocateLightmaps(Lightmaps);

	FStaticMeshInstanceRenderState InstanceRenderState;
	InstanceRenderState.ComponentUObject = Instance->ComponentUObject;
	InstanceRenderState.RenderData = Instance->ComponentUObject->GetStaticMesh()->RenderData.Get();
	InstanceRenderState.LocalToWorld = InComponent->GetRenderMatrix();
	InstanceRenderState.WorldBounds = InComponent->Bounds;
	InstanceRenderState.ActorPosition = InComponent->GetAttachmentRootActor() ? InComponent->GetAttachmentRootActor()->GetActorLocation() : FVector(ForceInitToZero);
	InstanceRenderState.LocalBounds = InComponent->CalcBounds(FTransform::Identity);
	InstanceRenderState.bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;
	InstanceRenderState.LODOverrideColorVertexBuffers.AddZeroed(InComponent->GetStaticMesh()->RenderData->LODResources.Num());
	InstanceRenderState.LODOverrideColorVFUniformBuffers.AddDefaulted(InComponent->GetStaticMesh()->RenderData->LODResources.Num());
	InstanceRenderState.ClampedMinLOD = Instance->ClampedMinLOD;

	for (int32 LODIndex = Instance->ClampedMinLOD; LODIndex < FMath::Min(InComponent->LODData.Num(), InComponent->GetStaticMesh()->RenderData->LODResources.Num()); LODIndex++)
	{
		const FStaticMeshComponentLODInfo& ComponentLODInfo = InComponent->LODData[LODIndex];

		// Initialize this LOD's overridden vertex colors, if it has any
		if (ComponentLODInfo.OverrideVertexColors)
		{
			bool bBroken = false;
			for (int32 SectionIndex = 0; SectionIndex < InComponent->GetStaticMesh()->RenderData->LODResources[LODIndex].Sections.Num(); SectionIndex++)
			{
				const FStaticMeshSection& Section = InComponent->GetStaticMesh()->RenderData->LODResources[LODIndex].Sections[SectionIndex];
				if (Section.MaxVertexIndex >= ComponentLODInfo.OverrideVertexColors->GetNumVertices())
				{
					bBroken = true;
					break;
				}
			}
			if (!bBroken)
			{
				InstanceRenderState.LODOverrideColorVertexBuffers[LODIndex] = ComponentLODInfo.OverrideVertexColors;
			}
		}
	}

	TArray<FLightmapRenderState::Initializer> InstanceLightmapRenderStateInitializers;
	TArray<FLightmapResourceCluster*> ResourceClusters;

	for (FLightmapRef& Lightmap : Instance->LODLightmaps)
	{
		if (Lightmap.IsValid())
		{
			Lightmap->CreateGameThreadResources();

			for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
			{
				AddLightToLightmap(Lightmap.GetReference_Unsafe(), DirectionalLight);
			}

			FLightmapResourceCluster* ResourceCluster = Lightmap->ResourceCluster.Release();

			FLightmapRenderState::Initializer Initializer {
				Lightmap->Name,
				Lightmap->Size,
				FMath::Min((int32)FMath::CeilLogTwo((uint32)FMath::Min(Lightmap->GetPaddedSizeInTiles().X, Lightmap->GetPaddedSizeInTiles().Y)), GPreviewLightmapMipmapMaxLevel),
				ResourceCluster, // temporarily promote unique ptr to raw ptr to make it copyable
				FVector4(Lightmap->LightmapObject->CoordinateScale, Lightmap->LightmapObject->CoordinateBias)
			};

			InstanceLightmapRenderStateInitializers.Add(MoveTemp(Initializer));
			ResourceClusters.Add(ResourceCluster);
		}
		else
		{
			InstanceLightmapRenderStateInitializers.Add(FLightmapRenderState::Initializer {});
		}
	}
	
	TArray<int32> RelevantPointLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.PointLights, Instance);
	TArray<int32> RelevantSpotLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.SpotLights, Instance);
	TArray<int32> RelevantRectLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.RectLights, Instance);

	ENQUEUE_RENDER_COMMAND(RenderThreadInit)(
		[
			InstanceRenderState = MoveTemp(InstanceRenderState), 
			InstanceLightmapRenderStateInitializers = MoveTemp(InstanceLightmapRenderStateInitializers),
			&RenderState = RenderState,
			RelevantPointLightsToAddOnRenderThread,
			RelevantSpotLightsToAddOnRenderThread,
			RelevantRectLightsToAddOnRenderThread
		](FRHICommandListImmediate&) mutable
	{
		FStaticMeshInstanceRenderStateRef InstanceRenderStateRef = RenderState.StaticMeshInstanceRenderStates.Emplace(MoveTemp(InstanceRenderState));

		InstanceRenderStateRef->PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->ActorPosition,
			InstanceRenderStateRef->WorldBounds,
			InstanceRenderStateRef->LocalBounds,
			false,
			false,
			false,
			false,
			false,
			false,
			0b111,
			1.0f,
			0,
			INDEX_NONE,
			false);

		for (int32 LODIndex = 0; LODIndex < InstanceLightmapRenderStateInitializers.Num(); LODIndex++)
		{
			FLightmapRenderState::Initializer& Initializer = InstanceLightmapRenderStateInitializers[LODIndex];
			if (Initializer.IsValid())
			{
				FLightmapRenderStateRef LightmapRenderState = RenderState.LightmapRenderStates.Emplace(Initializer, RenderState.StaticMeshInstanceRenderStates.CreateGeometryInstanceRef(InstanceRenderStateRef, LODIndex));
				FLightmapPreviewVirtualTexture* LightmapPreviewVirtualTexture = new FLightmapPreviewVirtualTexture(LightmapRenderState, RenderState.LightmapRenderer.Get());
				LightmapRenderState->LightmapPreviewVirtualTexture = LightmapPreviewVirtualTexture;
				LightmapRenderState->ResourceCluster->AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;
				LightmapRenderState->ResourceCluster->InitResource();

				{
					IAllocatedVirtualTexture* AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;

					check(AllocatedVT);

					AllocatedVT->GetPackedPageTableUniform(&LightmapRenderState->LightmapVTPackedPageTableUniform[0]);
					uint32 NumLightmapVTLayers = AllocatedVT->GetNumTextureLayers();
					for (uint32 LayerIndex = 0u; LayerIndex < NumLightmapVTLayers; ++LayerIndex)
					{
						AllocatedVT->GetPackedUniform(&LightmapRenderState->LightmapVTPackedUniform[LayerIndex], LayerIndex);
					}
					for (uint32 LayerIndex = NumLightmapVTLayers; LayerIndex < 5u; ++LayerIndex)
					{
						LightmapRenderState->LightmapVTPackedUniform[LayerIndex] = FUintVector4(ForceInitToZero);
					}
				}

				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(LightmapRenderState);

				for (int32 ElementId : RelevantPointLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FPointLightRenderStateRef(RenderState.LightSceneRenderState.PointLights.Elements[ElementId], RenderState.LightSceneRenderState.PointLights));
				}

				for (int32 ElementId : RelevantSpotLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FSpotLightRenderStateRef(RenderState.LightSceneRenderState.SpotLights.Elements[ElementId], RenderState.LightSceneRenderState.SpotLights));
				}

				for (int32 ElementId : RelevantRectLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FRectLightRenderStateRef(RenderState.LightSceneRenderState.RectLights.Elements[ElementId], RenderState.LightSceneRenderState.RectLights));
				}
			}
			else
			{
				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(RenderState.LightmapRenderStates.CreateNullRef());
			}
		}

		for (int32 LODIndex = InstanceRenderStateRef->ClampedMinLOD; LODIndex < InstanceLightmapRenderStateInitializers.Num(); LODIndex++)
		{
			if (InstanceRenderStateRef->LODOverrideColorVertexBuffers[LODIndex] != nullptr)
			{
				const FLocalVertexFactory* LocalVF = &InstanceRenderStateRef->ComponentUObject->GetStaticMesh()->RenderData->LODVertexFactories[LODIndex].VertexFactoryOverrideColorVertexBuffer;
				InstanceRenderStateRef->LODOverrideColorVFUniformBuffers[LODIndex] = CreateLocalVFUniformBuffer(LocalVF, LODIndex, InstanceRenderStateRef->LODOverrideColorVertexBuffers[LODIndex], 0, 0);
			}
		}

		RenderState.LightmapRenderer->BumpRevision();

		RenderState.CachedRayTracingScene.Reset();
	});

	bNeedsVoxelization = true;

	for (auto ResourceCluster : ResourceClusters)
	{
		ResourceCluster->UpdateUniformBuffer(ERHIFeatureLevel::SM5);
	}

	if (InComponent->GetWorld())
	{
		InComponent->GetWorld()->SendAllEndOfFrameUpdates();
	}
}

void FScene::RemoveGeometryInstanceFromComponent(UStaticMeshComponent* InComponent)
{
	if (!RegisteredStaticMeshComponentUObjects.Contains(InComponent))
	{
		return;
	}

	FStaticMeshInstanceRef Instance = RegisteredStaticMeshComponentUObjects[InComponent];
	for (FLightmapRef& Lightmap : Instance->LODLightmaps)
	{
		if (Lightmap.IsValid())
		{
			Lightmaps.Remove(Lightmap);
		}
	}

	int32 ElementId = Instance.GetElementId();
	StaticMeshInstances.RemoveAt(ElementId);
	RegisteredStaticMeshComponentUObjects.Remove(InComponent);

	ENQUEUE_RENDER_COMMAND(RenderThreadRemove)(
		[ElementId, &RenderState = RenderState](FRHICommandListImmediate&) mutable
	{
		for (FLightmapRenderStateRef& Lightmap : RenderState.StaticMeshInstanceRenderStates.Elements[ElementId].LODLightmapRenderStates)
		{
			if (Lightmap.IsValid())
			{
				Lightmap->ResourceCluster->ReleaseResource();

				FVirtualTextureProducerHandle ProducerHandle = Lightmap->LightmapPreviewVirtualTexture->ProducerHandle;
				GetRendererModule().ReleaseVirtualTextureProducer(ProducerHandle);

				RenderState.LightmapRenderStates.Remove(Lightmap);
			}
		}

		RenderState.StaticMeshInstanceRenderStates.RemoveAt(ElementId);

		RenderState.LightmapRenderer->BumpRevision();

		RenderState.CachedRayTracingScene.Reset();
	});

	bNeedsVoxelization = true;
}

void FScene::AddGeometryInstanceFromComponent(UInstancedStaticMeshComponent* InComponent)
{
	if (InComponent->PerInstanceSMData.Num() == 0)
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Skipping empty instanced static mesh"));
		return;
	}

	if (RegisteredInstancedStaticMeshComponentUObjects.Contains(InComponent))
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: duplicated component registration"));
		return;
	}

	FInstanceGroupRef Instance = InstanceGroups.Emplace(InComponent);
	Instance->WorldBounds = InComponent->Bounds;
	Instance->bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;

	RegisteredInstancedStaticMeshComponentUObjects.Add(InComponent, Instance);

	if (InComponent->GetWorld())
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISMC = Cast<UHierarchicalInstancedStaticMeshComponent>(InComponent))
		{
			HISMC->BuildTreeIfOutdated(false, true);
		}

		InComponent->GetWorld()->SendAllEndOfFrameUpdates();
	}

	Instance->AllocateLightmaps(Lightmaps);

	TArray<FLightmapRenderState::Initializer> InstanceLightmapRenderStateInitializers;
	TArray<FLightmapResourceCluster*> ResourceClusters;

	for (int32 LODIndex = 0; LODIndex < Instance->LODLightmaps.Num(); LODIndex++)
	{
		FLightmapRef& Lightmap = Instance->LODLightmaps[LODIndex];

		if (Lightmap.IsValid())
		{
			Lightmap->CreateGameThreadResources();

			{
				int32 BaseLightMapWidth = Instance->LODPerInstanceLightmapSize[LODIndex].X;
				int32 BaseLightMapHeight = Instance->LODPerInstanceLightmapSize[LODIndex].Y;

				FVector2D Scale = FVector2D(BaseLightMapWidth - 2, BaseLightMapHeight - 2) / Lightmap->Size;
				Lightmap->LightmapObject->CoordinateScale = Scale;
				Lightmap->LightmapObject->CoordinateBias = FVector2D(0, 0);

				int32 InstancesPerRow = FMath::CeilToInt(FMath::Sqrt(InComponent->PerInstanceSMData.Num()));
				Lightmap->MeshMapBuildData->PerInstanceLightmapData.AddDefaulted(InComponent->PerInstanceSMData.Num());
				for (int32 GameThreadInstanceIndex = 0; GameThreadInstanceIndex < InComponent->PerInstanceSMData.Num(); GameThreadInstanceIndex++)
				{
					int32 RenderIndex = InComponent->GetRenderIndex(GameThreadInstanceIndex);
					if (RenderIndex != INDEX_NONE)
					{
						int32 X = RenderIndex % InstancesPerRow;
						int32 Y = RenderIndex / InstancesPerRow;
						FVector2D Bias = (FVector2D(X, Y) * FVector2D(BaseLightMapWidth, BaseLightMapHeight) + FVector2D(1, 1)) / Lightmap->Size;
						Lightmap->MeshMapBuildData->PerInstanceLightmapData[GameThreadInstanceIndex].LightmapUVBias = Bias;
						Lightmap->MeshMapBuildData->PerInstanceLightmapData[GameThreadInstanceIndex].ShadowmapUVBias = Bias;
					}
				}
			}

			for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
			{
				AddLightToLightmap(Lightmap.GetReference_Unsafe(), DirectionalLight);
			}

			FLightmapResourceCluster* ResourceCluster = Lightmap->ResourceCluster.Release();

			FLightmapRenderState::Initializer Initializer {
				Lightmap->Name,
				Lightmap->Size,
				FMath::Min((int32)FMath::CeilLogTwo((uint32)FMath::Min(Lightmap->GetPaddedSizeInTiles().X, Lightmap->GetPaddedSizeInTiles().Y)), GPreviewLightmapMipmapMaxLevel),
				ResourceCluster, // temporarily promote unique ptr to raw ptr to make it copyable
				FVector4(Lightmap->LightmapObject->CoordinateScale, Lightmap->LightmapObject->CoordinateBias)
			};

			InstanceLightmapRenderStateInitializers.Add(Initializer);
			ResourceClusters.Add(ResourceCluster);
		}
		else
		{
			InstanceLightmapRenderStateInitializers.Add(FLightmapRenderState::Initializer{});
		}
	}

	InComponent->FlushInstanceUpdateCommands();

	FInstanceGroupRenderState InstanceRenderState;
	InstanceRenderState.ComponentUObject = Instance->ComponentUObject;
	InstanceRenderState.RenderData = Instance->ComponentUObject->GetStaticMesh()->RenderData.Get();
	InstanceRenderState.InstancedRenderData = MakeUnique<FInstancedStaticMeshRenderData>(Instance->ComponentUObject, ERHIFeatureLevel::SM5);
	InstanceRenderState.LocalToWorld = InComponent->GetRenderMatrix();
	InstanceRenderState.WorldBounds = InComponent->Bounds;
	InstanceRenderState.ActorPosition = InComponent->GetAttachmentRootActor() ? InComponent->GetAttachmentRootActor()->GetActorLocation() : FVector(ForceInitToZero);
	InstanceRenderState.LocalBounds = InComponent->CalcBounds(FTransform::Identity);
	InstanceRenderState.bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;

	for (int32 LODIndex = 0; LODIndex < Instance->LODLightmaps.Num(); LODIndex++)
	{
		InstanceRenderState.LODPerInstanceLightmapSize.Add(Instance->LODPerInstanceLightmapSize[LODIndex]);
	}

	TArray<int32> RelevantPointLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.PointLights, Instance);
	TArray<int32> RelevantSpotLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.SpotLights, Instance);
	TArray<int32> RelevantRectLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.RectLights, Instance);

	ENQUEUE_RENDER_COMMAND(RenderThreadInit)(
		[
			InstanceRenderState = MoveTemp(InstanceRenderState),
			InstanceLightmapRenderStateInitializers = MoveTemp(InstanceLightmapRenderStateInitializers),
			&RenderState = RenderState,
			RelevantPointLightsToAddOnRenderThread,
			RelevantSpotLightsToAddOnRenderThread,
			RelevantRectLightsToAddOnRenderThread
		](FRHICommandListImmediate&) mutable
	{

		InstanceRenderState.InstanceOriginBuffer = InstanceRenderState.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetInstanceOriginBuffer();
		InstanceRenderState.InstanceTransformBuffer = InstanceRenderState.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetInstanceTransformBuffer();
		InstanceRenderState.InstanceLightmapBuffer = InstanceRenderState.InstancedRenderData->PerInstanceRenderData->InstanceBuffer.GetInstanceLightmapBuffer();

		FInstanceGroupRenderStateRef InstanceRenderStateRef = RenderState.InstanceGroupRenderStates.Emplace(MoveTemp(InstanceRenderState));

		InstanceRenderStateRef->UniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>::CreateUniformBufferImmediate(GetPrimitiveUniformShaderParameters(
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->ActorPosition,
			InstanceRenderStateRef->WorldBounds,
			InstanceRenderStateRef->LocalBounds,
			false,
			false,
			false,
			false,
			false,
			false,
			0b111,
			1.0f,
			0,
			INDEX_NONE,
			false), UniformBuffer_MultiFrame);

		for (int32 LODIndex = 0; LODIndex < InstanceLightmapRenderStateInitializers.Num(); LODIndex++)
		{
			FLightmapRenderState::Initializer& Initializer = InstanceLightmapRenderStateInitializers[LODIndex];
			if (Initializer.IsValid())
			{
				FLightmapRenderStateRef LightmapRenderState = RenderState.LightmapRenderStates.Emplace(Initializer, RenderState.InstanceGroupRenderStates.CreateGeometryInstanceRef(InstanceRenderStateRef, LODIndex));
				FLightmapPreviewVirtualTexture* LightmapPreviewVirtualTexture = new FLightmapPreviewVirtualTexture(LightmapRenderState, RenderState.LightmapRenderer.Get());
				LightmapRenderState->LightmapPreviewVirtualTexture = LightmapPreviewVirtualTexture;
				LightmapRenderState->ResourceCluster->AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;
				LightmapRenderState->ResourceCluster->InitResource();

				{
					IAllocatedVirtualTexture* AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;

					check(AllocatedVT);

					AllocatedVT->GetPackedPageTableUniform(&LightmapRenderState->LightmapVTPackedPageTableUniform[0]);
					uint32 NumLightmapVTLayers = AllocatedVT->GetNumTextureLayers();
					for (uint32 LayerIndex = 0u; LayerIndex < NumLightmapVTLayers; ++LayerIndex)
					{
						AllocatedVT->GetPackedUniform(&LightmapRenderState->LightmapVTPackedUniform[LayerIndex], LayerIndex);
					}
					for (uint32 LayerIndex = NumLightmapVTLayers; LayerIndex < 5u; ++LayerIndex)
					{
						LightmapRenderState->LightmapVTPackedUniform[LayerIndex] = FUintVector4(ForceInitToZero);
					}
				}

				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(MoveTemp(LightmapRenderState));

				for (int32 ElementId : RelevantPointLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FPointLightRenderStateRef(RenderState.LightSceneRenderState.PointLights.Elements[ElementId], RenderState.LightSceneRenderState.PointLights));
				}

				for (int32 ElementId : RelevantSpotLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FSpotLightRenderStateRef(RenderState.LightSceneRenderState.SpotLights.Elements[ElementId], RenderState.LightSceneRenderState.SpotLights));
				}

				for (int32 ElementId : RelevantRectLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FRectLightRenderStateRef(RenderState.LightSceneRenderState.RectLights.Elements[ElementId], RenderState.LightSceneRenderState.RectLights));
				}
			}
			else
			{
				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(RenderState.LightmapRenderStates.CreateNullRef());
			}
		}

		RenderState.LightmapRenderer->BumpRevision();

		RenderState.CachedRayTracingScene.Reset();
	});

	bNeedsVoxelization = true;

	for (auto ResourceCluster : ResourceClusters)
	{
		ResourceCluster->UpdateUniformBuffer(ERHIFeatureLevel::SM5);
	}
}

void FScene::RemoveGeometryInstanceFromComponent(UInstancedStaticMeshComponent* InComponent)
{
	if (!RegisteredInstancedStaticMeshComponentUObjects.Contains(InComponent))
	{
		return;
	}

	FInstanceGroupRef Instance = RegisteredInstancedStaticMeshComponentUObjects[InComponent];
	for (FLightmapRef& Lightmap : Instance->LODLightmaps)
	{
		if (Lightmap.IsValid())
		{
			Lightmaps.Remove(Lightmap);
		}
	}

	int32 ElementId = Instance.GetElementId();
	InstanceGroups.RemoveAt(ElementId);
	RegisteredInstancedStaticMeshComponentUObjects.Remove(InComponent);

	if (UHierarchicalInstancedStaticMeshComponent* HISMC = Cast<UHierarchicalInstancedStaticMeshComponent>(InComponent))
	{
		HISMC->BuildTreeIfOutdated(false, true);
	}

	InComponent->FlushInstanceUpdateCommands();

	ENQUEUE_RENDER_COMMAND(RenderThreadRemove)(
		[ElementId, &RenderState = RenderState](FRHICommandListImmediate&) mutable
	{
		RenderState.InstanceGroupRenderStates.Elements[ElementId].InstancedRenderData->ReleaseResources(nullptr, nullptr);
		RenderState.InstanceGroupRenderStates.Elements[ElementId].UniformBuffer.SafeRelease();

		for (FLightmapRenderStateRef& Lightmap : RenderState.InstanceGroupRenderStates.Elements[ElementId].LODLightmapRenderStates)
		{
			if (Lightmap.IsValid())
			{
				Lightmap->ResourceCluster->ReleaseResource();

				FVirtualTextureProducerHandle ProducerHandle = Lightmap->LightmapPreviewVirtualTexture->ProducerHandle;
				GetRendererModule().ReleaseVirtualTextureProducer(ProducerHandle);

				RenderState.LightmapRenderStates.Remove(Lightmap);
			}
		}

		RenderState.InstanceGroupRenderStates.RemoveAt(ElementId);

		RenderState.LightmapRenderer->BumpRevision();

		RenderState.CachedRayTracingScene.Reset();
	});

	bNeedsVoxelization = true;
}

void FScene::AddGeometryInstanceFromComponent(ULandscapeComponent* InComponent)
{
	if (InComponent->GetLandscapeInfo() == nullptr)
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Skipping landscape with empty info object"));
		return;
	}

	if (RegisteredLandscapeComponentUObjects.Contains(InComponent))
	{
		UE_LOG(LogGPULightmass, Log, TEXT("Warning: duplicated component registration"));
		return;
	}

	FLandscapeRef Instance = Landscapes.Emplace(InComponent);
	Instance->WorldBounds = InComponent->Bounds;
	Instance->bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;

	RegisteredLandscapeComponentUObjects.Add(InComponent, Instance);

	Instance->AllocateLightmaps(Lightmaps);

	TArray<FLightmapRenderState::Initializer> InstanceLightmapRenderStateInitializers;
	TArray<FLightmapResourceCluster*> ResourceClusters;

	for (int32 LODIndex = 0; LODIndex < Instance->LODLightmaps.Num(); LODIndex++)
	{
		FLightmapRef& Lightmap = Instance->LODLightmaps[LODIndex];

		if (Lightmap.IsValid())
		{
			Lightmap->CreateGameThreadResources();

			Lightmap->LightmapObject->CoordinateScale = FVector2D(1, 1);
			Lightmap->LightmapObject->CoordinateBias = FVector2D(0, 0);

			for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
			{
				AddLightToLightmap(Lightmap.GetReference_Unsafe(), DirectionalLight);
			}

			FLightmapResourceCluster* ResourceCluster = Lightmap->ResourceCluster.Release();

			FLightmapRenderState::Initializer Initializer{
				Lightmap->Name,
				Lightmap->Size,
				FMath::Min((int32)FMath::CeilLogTwo((uint32)FMath::Min(Lightmap->GetPaddedSizeInTiles().X, Lightmap->GetPaddedSizeInTiles().Y)), GPreviewLightmapMipmapMaxLevel),
				ResourceCluster, // temporarily promote unique ptr to raw ptr to make it copyable
				FVector4(Lightmap->LightmapObject->CoordinateScale, Lightmap->LightmapObject->CoordinateBias)
			};

			InstanceLightmapRenderStateInitializers.Add(Initializer);
			ResourceClusters.Add(ResourceCluster);
		}
		else
		{
			InstanceLightmapRenderStateInitializers.Add(FLightmapRenderState::Initializer{});
		}
	}

	FLandscapeRenderState InstanceRenderState;
	InstanceRenderState.ComponentUObject = Instance->ComponentUObject;
	InstanceRenderState.LocalToWorld = InComponent->GetRenderMatrix();
	InstanceRenderState.WorldBounds = InComponent->Bounds;
	InstanceRenderState.ActorPosition = InComponent->GetAttachmentRootActor() ? InComponent->GetAttachmentRootActor()->GetActorLocation() : FVector(ForceInitToZero);
	InstanceRenderState.LocalBounds = InComponent->CalcBounds(FTransform::Identity);
	InstanceRenderState.bCastShadow = InComponent->CastShadow && InComponent->bCastStaticShadow;

	const int8 SubsectionSizeLog2 = FMath::CeilLogTwo(InComponent->SubsectionSizeQuads + 1);
	InstanceRenderState.SharedBuffersKey = (SubsectionSizeLog2 & 0xf) | ((InComponent->NumSubsections & 0xf) << 4) |
		(InComponent->GetWorld()->FeatureLevel <= ERHIFeatureLevel::ES3_1 ? 0 : (1 << 30)) | (InComponent->XYOffsetmapTexture == nullptr ? 0 : 1 << 31);
	InstanceRenderState.SharedBuffersKey |= 1 << 29; // Use this bit to indicate it is GPULightmass specific buffer (which only has FixedGridVertexFactory created)
	TEnumAsByte<ERHIFeatureLevel::Type> FeatureLevel = InComponent->GetWorld()->FeatureLevel;

	TArray<UMaterialInterface*> AvailableMaterials;

	if (InComponent->GetLandscapeProxy()->bUseDynamicMaterialInstance)
	{
		AvailableMaterials.Append(InComponent->MaterialInstancesDynamic);
	}
	else
	{
		AvailableMaterials.Append(InComponent->MaterialInstances);
	}

	int32 LODIndex = 0;
	InstanceRenderState.MaterialInterface = AvailableMaterials[
		InComponent->MaterialIndexToDisabledTessellationMaterial[InComponent->LODIndexToMaterialIndex[LODIndex]] != INDEX_NONE ? 
			InComponent->MaterialIndexToDisabledTessellationMaterial[InComponent->LODIndexToMaterialIndex[LODIndex]] :
			InComponent->LODIndexToMaterialIndex[LODIndex]
	];

	InstanceRenderState.LocalToWorldNoScaling = InstanceRenderState.LocalToWorld;
	InstanceRenderState.LocalToWorldNoScaling.RemoveScaling();

	FLandscapeRenderState::Initializer Initializer;
	Initializer.SubsectionSizeQuads        = InComponent->SubsectionSizeQuads;
	Initializer.SubsectionSizeVerts        = InComponent->SubsectionSizeQuads + 1;
	Initializer.NumSubsections             = InComponent->NumSubsections;
	Initializer.ComponentSizeQuads         = InComponent->ComponentSizeQuads;
	Initializer.ComponentSizeVerts         = InComponent->ComponentSizeQuads + 1;
	Initializer.StaticLightingResolution   = InComponent->StaticLightingResolution > 0.f ? InComponent->StaticLightingResolution : InComponent->GetLandscapeProxy()->StaticLightingResolution;
	Initializer.StaticLightingLOD          = InComponent->GetLandscapeProxy()->StaticLightingLOD;
	Initializer.ComponentBase              = InComponent->GetSectionBase() / InComponent->ComponentSizeQuads;
	Initializer.SectionBase                = InComponent->GetSectionBase();
	Initializer.HeightmapTexture           = InComponent->GetHeightmap();
	Initializer.HeightmapSubsectionOffsetU = ((float)(InComponent->SubsectionSizeQuads + 1) / (float)InComponent->GetHeightmap()->GetSizeX());
	Initializer.HeightmapSubsectionOffsetV = ((float)(InComponent->SubsectionSizeQuads + 1) / (float)InComponent->GetHeightmap()->GetSizeY());
	Initializer.HeightmapScaleBias         = InComponent->HeightmapScaleBias;
	Initializer.WeightmapScaleBias         = InComponent->WeightmapScaleBias;
	Initializer.WeightmapSubsectionOffset  = InComponent->WeightmapSubsectionOffset;

	TArray<int32> RelevantPointLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.PointLights, Instance);
	TArray<int32> RelevantSpotLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.SpotLights, Instance);
	TArray<int32> RelevantRectLightsToAddOnRenderThread = AddAllPossiblyRelevantLightsToGeometry(LightScene.RectLights, Instance);

	ENQUEUE_RENDER_COMMAND(RenderThreadInit)(
		[
			InstanceRenderState = MoveTemp(InstanceRenderState),
			FeatureLevel,
			Initializer,
			InstanceLightmapRenderStateInitializers = MoveTemp(InstanceLightmapRenderStateInitializers),
			&RenderState = RenderState,
			RelevantPointLightsToAddOnRenderThread,
			RelevantSpotLightsToAddOnRenderThread,
			RelevantRectLightsToAddOnRenderThread
		](FRHICommandListImmediate& RHICmdList) mutable
	{
		InstanceRenderState.SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(InstanceRenderState.SharedBuffersKey);
		if (InstanceRenderState.SharedBuffers == nullptr)
		{
			InstanceRenderState.SharedBuffers = new FLandscapeSharedBuffers(
				InstanceRenderState.SharedBuffersKey, Initializer.SubsectionSizeQuads, Initializer.NumSubsections,
				FeatureLevel, false, /*NumOcclusionVertices*/ 0);

			FLandscapeComponentSceneProxy::SharedBuffersMap.Add(InstanceRenderState.SharedBuffersKey, InstanceRenderState.SharedBuffers);

			FLandscapeFixedGridVertexFactory* LandscapeVertexFactory = new FLandscapeFixedGridVertexFactory(FeatureLevel);
			LandscapeVertexFactory->Data.PositionComponent = FVertexStreamComponent(InstanceRenderState.SharedBuffers->VertexBuffer, 0, sizeof(FLandscapeVertex), VET_Float4);
			LandscapeVertexFactory->InitResource();
			InstanceRenderState.SharedBuffers->FixedGridVertexFactory = LandscapeVertexFactory;
		}
		check(InstanceRenderState.SharedBuffers);
		InstanceRenderState.SharedBuffers->AddRef();

		InstanceRenderState.SubsectionSizeVerts = Initializer.SubsectionSizeVerts;
		InstanceRenderState.NumSubsections = Initializer.NumSubsections;

		FLandscapeRenderStateRef InstanceRenderStateRef = RenderState.LandscapeRenderStates.Emplace(MoveTemp(InstanceRenderState));

		InstanceRenderStateRef->UniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>::CreateUniformBufferImmediate(GetPrimitiveUniformShaderParameters(
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->LocalToWorld,
			InstanceRenderStateRef->ActorPosition,
			InstanceRenderStateRef->WorldBounds,
			InstanceRenderStateRef->LocalBounds,
			false,
			false,
			false,
			false,
			false,
			false,
			0b111,
			1.0f,
			0,
			INDEX_NONE,
			false), UniformBuffer_MultiFrame);

		int32 MaxLOD = 0;
		InstanceRenderStateRef->LandscapeFixedGridUniformShaderParameters.AddDefaulted(MaxLOD + 1);
		for (int32 LodIndex = 0; LodIndex <= MaxLOD; ++LodIndex)
		{
			InstanceRenderStateRef->LandscapeFixedGridUniformShaderParameters[LodIndex].InitResource();
			FLandscapeFixedGridUniformShaderParameters Parameters;
			Parameters.LodValues = FVector4(
				LodIndex,
				0.f,
				(float)((InstanceRenderStateRef->SubsectionSizeVerts >> LodIndex) - 1),
				1.f / (float)((InstanceRenderStateRef->SubsectionSizeVerts >> LodIndex) - 1));
			InstanceRenderStateRef->LandscapeFixedGridUniformShaderParameters[LodIndex].SetContents(Parameters);
		}

		{
			// Set Lightmap ScaleBias
			int32 PatchExpandCountX = 0;
			int32 PatchExpandCountY = 0;
			int32 DesiredSize = 1; // output by GetTerrainExpandPatchCount but not used below
			const float LightMapRatio = GetTerrainExpandPatchCount(Initializer.StaticLightingResolution, PatchExpandCountX, PatchExpandCountY, Initializer.ComponentSizeQuads, (Initializer.NumSubsections * (Initializer.SubsectionSizeQuads + 1)), DesiredSize, Initializer.StaticLightingLOD);
			const float LightmapLODScaleX = LightMapRatio / ((Initializer.ComponentSizeVerts >> Initializer.StaticLightingLOD) + 2 * PatchExpandCountX);
			const float LightmapLODScaleY = LightMapRatio / ((Initializer.ComponentSizeVerts >> Initializer.StaticLightingLOD) + 2 * PatchExpandCountY);
			const float LightmapBiasX = PatchExpandCountX * LightmapLODScaleX;
			const float LightmapBiasY = PatchExpandCountY * LightmapLODScaleY;
			const float LightmapScaleX = LightmapLODScaleX * (float)((Initializer.ComponentSizeVerts >> Initializer.StaticLightingLOD) - 1) / Initializer.ComponentSizeQuads;
			const float LightmapScaleY = LightmapLODScaleY * (float)((Initializer.ComponentSizeVerts >> Initializer.StaticLightingLOD) - 1) / Initializer.ComponentSizeQuads;
			const float LightmapExtendFactorX = (float)Initializer.SubsectionSizeQuads * LightmapScaleX;
			const float LightmapExtendFactorY = (float)Initializer.SubsectionSizeQuads * LightmapScaleY;

			// Set FLandscapeUniformVSParameters for this subsection
			FLandscapeUniformShaderParameters LandscapeParams;
			LandscapeParams.ComponentBaseX = Initializer.ComponentBase.X;
			LandscapeParams.ComponentBaseY = Initializer.ComponentBase.Y;
			LandscapeParams.SubsectionSizeVerts = Initializer.SubsectionSizeVerts;
			LandscapeParams.NumSubsections = Initializer.NumSubsections;
			LandscapeParams.LastLOD = FMath::CeilLogTwo(Initializer.SubsectionSizeQuads + 1) - 1;
			LandscapeParams.HeightmapUVScaleBias = Initializer.HeightmapScaleBias;
			LandscapeParams.WeightmapUVScaleBias = Initializer.WeightmapScaleBias;
			LandscapeParams.LocalToWorldNoScaling = InstanceRenderState.LocalToWorldNoScaling;

			LandscapeParams.LandscapeLightmapScaleBias = FVector4(
				LightmapScaleX,
				LightmapScaleY,
				LightmapBiasY,
				LightmapBiasX);
			LandscapeParams.SubsectionSizeVertsLayerUVPan = FVector4(
				Initializer.SubsectionSizeQuads + 1,
				1.f / (float)Initializer.SubsectionSizeQuads,
				Initializer.SectionBase.X,
				Initializer.SectionBase.Y
			);
			LandscapeParams.SubsectionOffsetParams = FVector4(
				Initializer.HeightmapSubsectionOffsetU,
				Initializer.HeightmapSubsectionOffsetV,
				Initializer.WeightmapSubsectionOffset,
				Initializer.SubsectionSizeQuads
			);
			LandscapeParams.LightmapSubsectionOffsetParams = FVector4(
				LightmapExtendFactorX,
				LightmapExtendFactorY,
				0,
				0
			);

			LandscapeParams.HeightmapTexture = Initializer.HeightmapTexture->TextureReference.TextureReferenceRHI;
			LandscapeParams.HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

			LandscapeParams.NormalmapTexture = Initializer.HeightmapTexture->TextureReference.TextureReferenceRHI;
			LandscapeParams.NormalmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

			// No support for XYOffset
			LandscapeParams.XYOffsetmapTexture = GBlackTexture->TextureRHI;
			LandscapeParams.XYOffsetmapTextureSampler = GBlackTexture->SamplerStateRHI;

			InstanceRenderStateRef->LandscapeUniformShaderParameters = MakeUnique<TUniformBuffer<FLandscapeUniformShaderParameters>>();
			InstanceRenderStateRef->LandscapeUniformShaderParameters->InitResource();
			InstanceRenderStateRef->LandscapeUniformShaderParameters->SetContents(LandscapeParams);
		}

		for (int32 LODIndex = 0; LODIndex < InstanceLightmapRenderStateInitializers.Num(); LODIndex++)
		{
			FLightmapRenderState::Initializer& LightmapInitializer = InstanceLightmapRenderStateInitializers[LODIndex];
			if (LightmapInitializer.IsValid())
			{
				FLightmapRenderStateRef LightmapRenderState = RenderState.LightmapRenderStates.Emplace(LightmapInitializer, RenderState.LandscapeRenderStates.CreateGeometryInstanceRef(InstanceRenderStateRef, LODIndex));
				FLightmapPreviewVirtualTexture* LightmapPreviewVirtualTexture = new FLightmapPreviewVirtualTexture(LightmapRenderState, RenderState.LightmapRenderer.Get());
				LightmapRenderState->LightmapPreviewVirtualTexture = LightmapPreviewVirtualTexture;
				LightmapRenderState->ResourceCluster->AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;
				LightmapRenderState->ResourceCluster->InitResource();

				{
					IAllocatedVirtualTexture* AllocatedVT = LightmapPreviewVirtualTexture->AllocatedVT;

					check(AllocatedVT);

					AllocatedVT->GetPackedPageTableUniform(&LightmapRenderState->LightmapVTPackedPageTableUniform[0]);
					uint32 NumLightmapVTLayers = AllocatedVT->GetNumTextureLayers();
					for (uint32 LayerIndex = 0u; LayerIndex < NumLightmapVTLayers; ++LayerIndex)
					{
						AllocatedVT->GetPackedUniform(&LightmapRenderState->LightmapVTPackedUniform[LayerIndex], LayerIndex);
					}
					for (uint32 LayerIndex = NumLightmapVTLayers; LayerIndex < 5u; ++LayerIndex)
					{
						LightmapRenderState->LightmapVTPackedUniform[LayerIndex] = FUintVector4(ForceInitToZero);
					}
				}

				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(MoveTemp(LightmapRenderState));

				for (int32 ElementId : RelevantPointLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FPointLightRenderStateRef(RenderState.LightSceneRenderState.PointLights.Elements[ElementId], RenderState.LightSceneRenderState.PointLights));
				}

				for (int32 ElementId : RelevantSpotLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FSpotLightRenderStateRef(RenderState.LightSceneRenderState.SpotLights.Elements[ElementId], RenderState.LightSceneRenderState.SpotLights));
				}

				for (int32 ElementId : RelevantRectLightsToAddOnRenderThread)
				{
					LightmapRenderState->AddRelevantLight(FRectLightRenderStateRef(RenderState.LightSceneRenderState.RectLights.Elements[ElementId], RenderState.LightSceneRenderState.RectLights));
				}
			}
			else
			{
				InstanceRenderStateRef->LODLightmapRenderStates.Emplace(RenderState.LightmapRenderStates.CreateNullRef());
			}
		}

#if 1
#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			// For DynamicGeometryCollection
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

			for (int32 SubY = 0; SubY < InstanceRenderStateRef->NumSubsections; SubY++)
			{
				for (int32 SubX = 0; SubX < InstanceRenderStateRef->NumSubsections; SubX++)
				{
					const int8 SubSectionIdx = SubX + SubY * InstanceRenderStateRef->NumSubsections;

					int32 LodSubsectionSizeVerts = InstanceRenderStateRef->SubsectionSizeVerts;
					uint32 NumPrimitives = FMath::Square(LodSubsectionSizeVerts - 1) * 2;

					FRayTracingGeometryInitializer GeometryInitializer;
					FRHIResourceCreateInfo CreateInfo;
					GeometryInitializer.IndexBuffer = InstanceRenderStateRef->SharedBuffers->ZeroOffsetIndexBuffers[0]->IndexBufferRHI;
					GeometryInitializer.TotalPrimitiveCount = NumPrimitives;
					GeometryInitializer.GeometryType = RTGT_Triangles;
					GeometryInitializer.bFastBuild = false;
					GeometryInitializer.bAllowUpdate = false;

					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = nullptr;
					Segment.VertexBufferStride = sizeof(FVector);
					Segment.VertexBufferElementType = VET_Float3;
					Segment.NumPrimitives = NumPrimitives;
					GeometryInitializer.Segments.Add(Segment);

					InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx] = MakeUnique<FLandscapeRenderState::FLandscapeSectionRayTracingState>();
					InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->Geometry.SetInitializer(GeometryInitializer);
					InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->Geometry.InitResource();

					FRayTracingDynamicGeometryCollection DynamicGeometryCollection;

					TArray<FMeshBatch> MeshBatches = InstanceRenderStateRef->GetMeshBatchesForGBufferRendering(0);

					FLandscapeVertexFactoryMVFParameters UniformBufferParams;
					UniformBufferParams.SubXY = FIntPoint(SubX, SubY);
					InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->UniformBuffer = FLandscapeVertexFactoryMVFUniformBufferRef::CreateUniformBufferImmediate(UniformBufferParams, UniformBuffer_SingleFrame);

					FLandscapeBatchElementParams& BatchElementParams = *(FLandscapeBatchElementParams*)MeshBatches[0].Elements[0].UserData;
					BatchElementParams.LandscapeVertexFactoryMVFUniformBuffer = InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->UniformBuffer;

					MeshBatches[0].Elements[0].IndexBuffer = InstanceRenderStateRef->SharedBuffers->ZeroOffsetIndexBuffers[0];
					MeshBatches[0].Elements[0].FirstIndex = 0;
					MeshBatches[0].Elements[0].NumPrimitives = NumPrimitives;
					MeshBatches[0].Elements[0].MinVertexIndex = 0;
					MeshBatches[0].Elements[0].MaxVertexIndex = 0;

					FRayTracingDynamicGeometryUpdateParams UpdateParams
					{
						MeshBatches,
						false,
						(uint32)FMath::Square(LodSubsectionSizeVerts),
						FMath::Square(LodSubsectionSizeVerts) * (uint32)sizeof(FVector),
						(uint32)FMath::Square(LodSubsectionSizeVerts - 1) * 2,
						&InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->Geometry,
						&InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->RayTracingDynamicVertexBuffer,
						false
					};

					DynamicGeometryCollection.AddDynamicMeshBatchForGeometryUpdate(
						InstanceRenderStateRef->ComponentUObject->GetWorld()->Scene->GetRenderScene(),
						nullptr,
						nullptr,
						UpdateParams,
						0
					);

					DynamicGeometryCollection.DispatchUpdates(RHICmdList);

					// Landscape VF doesn't really use the vertex buffer in HitGroupSystemParameters
					// We can release after all related RHI cmds get dispatched onto the cmd list
					InstanceRenderStateRef->SectionRayTracingStates[SubSectionIdx]->RayTracingDynamicVertexBuffer.Release();
				}
			}
		}
#endif
#endif
		RenderState.LightmapRenderer->BumpRevision();
	});

	bNeedsVoxelization = true;

	for (auto ResourceCluster : ResourceClusters)
	{
		ResourceCluster->UpdateUniformBuffer(ERHIFeatureLevel::SM5);
	}

	if (InComponent->GetWorld())
	{
		InComponent->GetWorld()->SendAllEndOfFrameUpdates();
	}
}

void FScene::RemoveGeometryInstanceFromComponent(ULandscapeComponent* InComponent)
{
	if (!RegisteredLandscapeComponentUObjects.Contains(InComponent))
	{
		return;
	}

	FLandscapeRef Instance = RegisteredLandscapeComponentUObjects[InComponent];

	for (FLightmapRef& Lightmap : Instance->LODLightmaps)
	{
		if (Lightmap.IsValid())
		{
			Lightmaps.Remove(Lightmap);
		}
	}

	int32 ElementId = Instance.GetElementId();
	Landscapes.RemoveAt(ElementId);
	RegisteredLandscapeComponentUObjects.Remove(InComponent);

	if (InComponent->GetLandscapeProxy())
	{
		TSet<ULandscapeComponent*> Components;
		Components.Add(InComponent);
		InComponent->GetLandscapeProxy()->FlushGrassComponents(&Components, false);
	}

	ENQUEUE_RENDER_COMMAND(RenderThreadRemove)(
		[ElementId, &RenderState = RenderState](FRHICommandListImmediate&) mutable
	{
		FLandscapeRenderState& LandscapeRenderState = RenderState.LandscapeRenderStates.Elements[ElementId];

		if (LandscapeRenderState.SharedBuffers->Release() == 0)
		{
			FLandscapeComponentSceneProxy::SharedBuffersMap.Remove(LandscapeRenderState.SharedBuffersKey);
		}

		LandscapeRenderState.LandscapeUniformShaderParameters->ReleaseResource();

		for (auto& UniformBuffer : LandscapeRenderState.LandscapeFixedGridUniformShaderParameters)
		{
			UniformBuffer.ReleaseResource();
		}

		if (IsRayTracingEnabled())
		{
			for (int32 SubY = 0; SubY < LandscapeRenderState.NumSubsections; SubY++)
			{
				for (int32 SubX = 0; SubX < LandscapeRenderState.NumSubsections; SubX++)
				{
					const int8 SubSectionIdx = SubX + SubY * LandscapeRenderState.NumSubsections;

					LandscapeRenderState.SectionRayTracingStates[SubSectionIdx]->Geometry.ReleaseResource();
				}
			}
		}

		for (FLightmapRenderStateRef& Lightmap : RenderState.LandscapeRenderStates.Elements[ElementId].LODLightmapRenderStates)
		{
			if (Lightmap.IsValid())
			{
				Lightmap->ResourceCluster->ReleaseResource();

				FVirtualTextureProducerHandle ProducerHandle = Lightmap->LightmapPreviewVirtualTexture->ProducerHandle;
				GetRendererModule().ReleaseVirtualTextureProducer(ProducerHandle);

				RenderState.LightmapRenderStates.Remove(Lightmap);
			}
		}

		RenderState.LandscapeRenderStates.RemoveAt(ElementId);

		RenderState.LightmapRenderer->BumpRevision();
	});

	bNeedsVoxelization = true;

	if (InComponent->GetWorld())
	{
		InComponent->GetWorld()->SendAllEndOfFrameUpdates();
	}
}

void FScene::BackgroundTick()
{
	int32 Percentage = FPlatformAtomics::AtomicRead(&RenderState.Percentage);

	if (GPULightmass->LightBuildNotification.IsValid())
	{
		bool bIsViewportNonRealtime = GCurrentLevelEditingViewportClient && !GCurrentLevelEditingViewportClient->IsRealtime();
		if (bIsViewportNonRealtime)
		{
			if (GPULightmass->Settings->Mode == EGPULightmassMode::FullBake)
			{
				FText Text = FText::Format(LOCTEXT("LightBuildProgressMessage", "Building lighting{0}:  {1}%"), FText(), FText::AsNumber(Percentage));
				GPULightmass->LightBuildNotification->SetText(Text);
			}
			else
			{
				FText Text = FText::Format(LOCTEXT("LightBuildProgressForCurrentViewMessage", "Building lighting for current view{0}:  {1}%"), FText(), FText::AsNumber(Percentage));
				GPULightmass->LightBuildNotification->SetText(Text);
			}
		}
		else
		{
			if (GPULightmass->Settings->Mode == EGPULightmassMode::FullBake)
			{
				FText Text = FText::Format(LOCTEXT("LightBuildProgressSlowModeMessage", "Building lighting{0}:  {1}% (slow mode)"), FText(), FText::AsNumber(Percentage));
				GPULightmass->LightBuildNotification->SetText(Text);
			}
			else
			{
				FText Text = FText::Format(LOCTEXT("LightBuildProgressForCurrentViewSlowModeMessage", "Building lighting for current view{0}:  {1}% (slow mode)"), FText(), FText::AsNumber(Percentage));
				GPULightmass->LightBuildNotification->SetText(Text);
			}
		}
	}
	GPULightmass->LightBuildPercentage = Percentage;

	if (Percentage < 100 || GPULightmass->Settings->Mode == EGPULightmassMode::BakeWhatYouSee)
	{
		if (bNeedsVoxelization)
		{
			GatherImportanceVolumes();

			ENQUEUE_RENDER_COMMAND(BackgroundTickRenderThread)([&RenderState = RenderState](FRHICommandListImmediate&) mutable {
				RenderState.VolumetricLightmapRenderer->VoxelizeScene();
				RenderState.VolumetricLightmapRenderer->FrameNumber = 0;
				RenderState.VolumetricLightmapRenderer->SamplesTaken = 0;
			});

			bNeedsVoxelization = false;
		}

		ENQUEUE_RENDER_COMMAND(BackgroundTickRenderThread)([&RenderState = RenderState](FRHICommandListImmediate&) mutable {
			RenderState.BackgroundTick();
		});
	}
	else
	{
		ApplyFinishedLightmapsToWorld();
	}
}

void FSceneRenderState::BackgroundTick()
{
	LightmapRenderer->BackgroundTick();
	VolumetricLightmapRenderer->BackgroundTick();

	if (IrradianceCache->CurrentRevision != LightmapRenderer->GetCurrentRevision())
	{
		IrradianceCache = MakeUnique<FIrradianceCache>(Settings->IrradianceCacheQuality, Settings->IrradianceCacheSpacing, Settings->IrradianceCacheCornerRejection);
		IrradianceCache->CurrentRevision = LightmapRenderer->GetCurrentRevision();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GPULightmassCountProgress);

		uint64 SamplesTaken = 0;
		uint64 TotalSamples = 0;

		if (!LightmapRenderer->bOnlyBakeWhatYouSee)
		{
			// Count work has been done
			for (FLightmapRenderState& Lightmap : LightmapRenderStates.Elements)
			{
				for (int32 Y = 0; Y < Lightmap.GetPaddedSizeInTiles().Y; Y++)
				{
					for (int32 X = 0; X < Lightmap.GetPaddedSizeInTiles().X; X++)
					{
						FTileVirtualCoordinates VirtualCoordinates(FIntPoint(X, Y), 0);

						TotalSamples += Settings->GISamples * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;
						SamplesTaken += (Lightmap.DoesTileHaveValidCPUData(VirtualCoordinates, LightmapRenderer->GetCurrentRevision()) ?
							Settings->GISamples :
							FMath::Min(Lightmap.RetrieveTileState(VirtualCoordinates).RenderPassIndex, Settings->GISamples - 1)) * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;
					}
				}
			}

			{
				int32 NumCellsPerBrick = 5 * 5 * 5;
				SamplesTaken += VolumetricLightmapRenderer->SamplesTaken;
				TotalSamples += (uint64)VolumetricLightmapRenderer->NumTotalBricks * NumCellsPerBrick * Settings->GISamples * VolumetricLightmapRenderer->GetGISamplesMultiplier();
			}
		}
		else
		{
			if (LightmapRenderer->RecordedTileRequests.Num() > 0)
			{
				for (FLightmapTileRequest& Tile : LightmapRenderer->RecordedTileRequests)
				{
					TotalSamples += Settings->GISamples * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;

					SamplesTaken += (Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, LightmapRenderer->GetCurrentRevision()) ?
						Settings->GISamples :
						FMath::Min(Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex, Settings->GISamples - 1)) * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;
				}
			}
			else
			{
				for (TArray<FLightmapTileRequest>& FrameRequests : LightmapRenderer->TilesVisibleLastFewFrames)
				{
					for (FLightmapTileRequest& Tile : FrameRequests)
					{
						TotalSamples += Settings->GISamples * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;

						SamplesTaken += (Tile.RenderState->DoesTileHaveValidCPUData(Tile.VirtualCoordinates, LightmapRenderer->GetCurrentRevision()) ?
							Settings->GISamples :
							FMath::Min(Tile.RenderState->RetrieveTileState(Tile.VirtualCoordinates).RenderPassIndex, Settings->GISamples - 1)) * GPreviewLightmapPhysicalTileSize * GPreviewLightmapPhysicalTileSize;
					}
				}
			}
		}

		FPlatformAtomics::InterlockedExchange(&Percentage, FMath::Max(FMath::FloorToInt(SamplesTaken * 100.0 / TotalSamples), 0));
	}
}

template<typename CopyFunc>
void CopyRectTiled(
	FIntPoint SrcMin,
	FIntRect DstRect,
	int32 SrcRowPitchInPixels,
	int32 DstRowPitchInPixels,
	CopyFunc Func,
	int32 VirtualTileSize = GPreviewLightmapVirtualTileSize,
	int32 PhysicalTileSize = GPreviewLightmapVirtualTileSize,
	int32 TileBorderSize = 0
)
{
	for (int32 Y = DstRect.Min.Y; Y < DstRect.Max.Y; Y++)
	{
		for (int32 X = DstRect.Min.X; X < DstRect.Max.X; X++)
		{
			FIntPoint SrcPosition = FIntPoint(X, Y) - DstRect.Min + SrcMin;
			FIntPoint SrcTilePosition(SrcPosition.X / VirtualTileSize, SrcPosition.Y / VirtualTileSize);
			FIntPoint PositionInTile(SrcPosition.X % VirtualTileSize, SrcPosition.Y % VirtualTileSize);

			FIntPoint SrcPixelPosition = PositionInTile + FIntPoint(TileBorderSize, TileBorderSize);
			FIntPoint DstPixelPosition = FIntPoint(X, Y);

			int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
			int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

			Func(DstLinearIndex, SrcTilePosition, SrcLinearIndex);
		}
	}
}

void ReadbackVolumetricLightmapDataLayerFromGPU(FRHICommandListImmediate& RHICmdList, FVolumetricLightmapDataLayer& Layer, FIntVector Dimensions)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VolumetricLightmapDataLayerReadback"));
	FTexture2DRHIRef StagingTexture2DSlice = RHICreateTexture2D(Layer.Texture->GetSizeX(), Layer.Texture->GetSizeY(), Layer.Texture->GetFormat(), 1, 1, TexCreate_CPUReadback | TexCreate_HideInVisualizeTexture, CreateInfo);
	FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("VolumetricLightmapDataLayerReadback"));

	check(Dimensions.Z == Layer.Texture->GetSizeZ());

	Layer.Resize(Dimensions.X * Dimensions.Y * Dimensions.Z * GPixelFormats[Layer.Format].BlockBytes);

	for (int32 SliceIndex = 0; SliceIndex < (int32)Layer.Texture->GetSizeZ(); SliceIndex++)
	{
		Fence->Clear();

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = FIntVector(Layer.Texture->GetSizeX(), Layer.Texture->GetSizeY(), 1);
		CopyInfo.SourcePosition = FIntVector(0, 0, SliceIndex);
		RHICmdList.CopyTexture(Layer.Texture, StagingTexture2DSlice, CopyInfo);
		RHICmdList.WriteGPUFence(Fence);

		uint8* Buffer;
		int32 RowPitchInPixels;
		int32 Height;
		RHICmdList.MapStagingSurface(StagingTexture2DSlice, Fence, (void*&)Buffer, RowPitchInPixels, Height);
		check(RowPitchInPixels >= Dimensions.X);
		check(Height == Dimensions.Y);
		RHICmdList.UnmapStagingSurface(StagingTexture2DSlice);

		const int32 SrcPitch = RowPitchInPixels * GPixelFormats[Layer.Format].BlockBytes;
		const int32 DstPitch = Dimensions.X * GPixelFormats[Layer.Format].BlockBytes;
		const int32 DepthPitch = Dimensions.Y * Dimensions.X * GPixelFormats[Layer.Format].BlockBytes;

		const int32 DestZIndex = SliceIndex * DepthPitch;

		for (int32 YIndex = 0; YIndex < Dimensions.Y; YIndex++)
		{
			const int32 DestIndex = DestZIndex + YIndex * DstPitch;
			const int32 SourceIndex = YIndex * SrcPitch;
			FMemory::Memcpy((uint8*)&Layer.Data[DestIndex], (const uint8*)&Buffer[SourceIndex], DstPitch);
		}
	}

}

void GatherBuildDataResourcesToKeep(const ULevel* InLevel, ULevel* LightingScenario, TSet<FGuid>& BuildDataResourcesToKeep)
{
	// This is only required is using a lighting scenario, otherwise the build data is saved within the level itself and follows it's inclusion in the lighting build.
	if (InLevel && LightingScenario)
	{
		BuildDataResourcesToKeep.Add(InLevel->LevelBuildDataId);

		for (const AActor* Actor : InLevel->Actors)
		{
			if (!Actor) // Skip null actors
			{
				continue;
			}

			for (const UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component) // Skip null components
				{
					continue;
				}

				const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
				if (PrimitiveComponent)
				{
					PrimitiveComponent->AddMapBuildDataGUIDs(BuildDataResourcesToKeep);
					continue;
				}

				const ULightComponent* LightComponent = Cast<ULightComponent>(Component);
				if (LightComponent)
				{
					BuildDataResourcesToKeep.Add(LightComponent->LightGuid);
					continue;
				}

				const UReflectionCaptureComponent* ReflectionCaptureComponent = Cast<UReflectionCaptureComponent>(Component);
				if (ReflectionCaptureComponent)
				{
					BuildDataResourcesToKeep.Add(ReflectionCaptureComponent->MapBuildDataId);
					continue;
				}
			}
		}
	}
}

void FScene::ApplyFinishedLightmapsToWorld()
{
	UWorld* World = GPULightmass->World;

	{
		FScopedSlowTask SlowTask(3);
		SlowTask.MakeDialog();

		SlowTask.EnterProgressFrame(1, LOCTEXT("InvalidatingPreviousLightingStatus", "Invalidating previous lighting"));

		FGlobalComponentRecreateRenderStateContext RecreateRenderStateContext; // Implicit FlushRenderingCommands();

		ULevel* LightingScenario = World->GetActiveLightingScenario();

		// Now we can access RT scene & preview lightmap textures directly

		TSet<FGuid> BuildDataResourcesToKeep;

		for (int32 LevelIndex = 0; LevelIndex < World->GetNumLevels(); LevelIndex++)
		{
			ULevel* Level = World->GetLevel(LevelIndex);

			if (Level)
			{
				if (!Level->bIsVisible && !Level->bIsLightingScenario)
				{
					// Do not touch invisible, normal levels
					GatherBuildDataResourcesToKeep(Level, LightingScenario, BuildDataResourcesToKeep);
				}
			}
		}

		for (int32 LevelIndex = 0; LevelIndex < World->GetNumLevels(); LevelIndex++)
		{
			ULevel* Level = World->GetLevel(LevelIndex);

			if (Level)
			{
				// Invalidate static lighting for normal visible levels, and the current lighting scenario
				// Since the current lighting scenario can contain build data for invisible normal levels, use BuildDataResourcesToKeep
				if (Level->bIsVisible && (!Level->bIsLightingScenario || Level == LightingScenario))
				{
					Level->ReleaseRenderingResources();

					if (Level->MapBuildData)
					{
						Level->MapBuildData->InvalidateStaticLighting(World, false, &BuildDataResourcesToKeep);
					}
				}
			}
		}

		for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
		{
			UDirectionalLightComponent* Light = DirectionalLight.ComponentUObject;
			check(!DirectionalLight.bStationary || Light->PreviewShadowMapChannel != INDEX_NONE);

			ULevel* StorageLevel = LightingScenario ? LightingScenario : Light->GetOwner()->GetLevel();
			UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
			FLightComponentMapBuildData& LightBuildData = Registry->FindOrAllocateLightBuildData(Light->LightGuid, true);
			LightBuildData.ShadowMapChannel = DirectionalLight.bStationary ? Light->PreviewShadowMapChannel : INDEX_NONE;
		}

		for (FPointLightBuildInfo& PointLight : LightScene.PointLights.Elements)
		{
			UPointLightComponent* Light = PointLight.ComponentUObject;
			check(!PointLight.bStationary || Light->PreviewShadowMapChannel != INDEX_NONE);

			ULevel* StorageLevel = LightingScenario ? LightingScenario : Light->GetOwner()->GetLevel();
			UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
			FLightComponentMapBuildData& LightBuildData = Registry->FindOrAllocateLightBuildData(Light->LightGuid, true);
			LightBuildData.ShadowMapChannel = PointLight.bStationary ? Light->PreviewShadowMapChannel : INDEX_NONE;
		}

		for (FSpotLightBuildInfo& SpotLight : LightScene.SpotLights.Elements)
		{
			USpotLightComponent* Light = SpotLight.ComponentUObject;
			check(!SpotLight.bStationary || Light->PreviewShadowMapChannel != INDEX_NONE);

			ULevel* StorageLevel = LightingScenario ? LightingScenario : Light->GetOwner()->GetLevel();
			UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
			FLightComponentMapBuildData& LightBuildData = Registry->FindOrAllocateLightBuildData(Light->LightGuid, true);
			LightBuildData.ShadowMapChannel = SpotLight.bStationary ? Light->PreviewShadowMapChannel : INDEX_NONE;
		}

		for (FRectLightBuildInfo& RectLight : LightScene.RectLights.Elements)
		{
			URectLightComponent* Light = RectLight.ComponentUObject;
			check(!RectLight.bStationary || Light->PreviewShadowMapChannel != INDEX_NONE);

			ULevel* StorageLevel = LightingScenario ? LightingScenario : Light->GetOwner()->GetLevel();
			UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
			FLightComponentMapBuildData& LightBuildData = Registry->FindOrAllocateLightBuildData(Light->LightGuid, true);
			LightBuildData.ShadowMapChannel = RectLight.bStationary ? Light->PreviewShadowMapChannel : INDEX_NONE;
		}

		{
			ULevel* SubLevelStorageLevel = LightingScenario ? LightingScenario : World->PersistentLevel;
			UMapBuildDataRegistry* SubLevelRegistry = SubLevelStorageLevel->GetOrCreateMapBuildData();
			FPrecomputedVolumetricLightmapData& SubLevelData = SubLevelRegistry->AllocateLevelPrecomputedVolumetricLightmapBuildData(World->PersistentLevel->LevelBuildDataId);

			SubLevelData = *RenderState.VolumetricLightmapRenderer->GetPrecomputedVolumetricLightmapForPreview()->Data;

			ENQUEUE_RENDER_COMMAND(ReadbackVLMDataCmd)([&SubLevelData](FRHICommandListImmediate& RHICmdList) {
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::GPU0());
				ReadbackVolumetricLightmapDataLayerFromGPU(RHICmdList, SubLevelData.IndirectionTexture, SubLevelData.IndirectionTextureDimensions);
				ReadbackVolumetricLightmapDataLayerFromGPU(RHICmdList, SubLevelData.BrickData.AmbientVector, SubLevelData.BrickDataDimensions);
				for (int32 i = 0; i < UE_ARRAY_COUNT(SubLevelData.BrickData.SHCoefficients); i++)
				{
					ReadbackVolumetricLightmapDataLayerFromGPU(RHICmdList, SubLevelData.BrickData.SHCoefficients[i], SubLevelData.BrickDataDimensions);
				}
				ReadbackVolumetricLightmapDataLayerFromGPU(RHICmdList, SubLevelData.BrickData.DirectionalLightShadowing, SubLevelData.BrickDataDimensions);
			});
		}

		// Fill non-existing mip 0 tiles by upsampling from higher mips, if available
		if (RenderState.LightmapRenderer->bOnlyBakeWhatYouSee)
		{
			for (FLightmapRenderState& Lightmap : RenderState.LightmapRenderStates.Elements)
			{
				for (int32 TileX = 0; TileX < Lightmap.GetPaddedSizeInTiles().X; TileX++)
				{
					FTileDataLayer::Evict();

					for (int32 TileY = 0; TileY < Lightmap.GetPaddedSizeInTiles().Y; TileY++)
					{
						FTileVirtualCoordinates Coords(FIntPoint(TileX, TileY), 0);
						if (!Lightmap.DoesTileHaveValidCPUData(Coords, RenderState.LightmapRenderer->GetCurrentRevision()))
						{
							if (!Lightmap.TileStorage.Contains(Coords))
							{
								Lightmap.TileStorage.Add(Coords, FTileStorage{});
							}

							for (int32 MipLevel = 0; MipLevel <= Lightmap.GetMaxLevel(); MipLevel++)
							{
								FTileVirtualCoordinates ParentCoords(FIntPoint(TileX / (1 << MipLevel), TileY / (1 << MipLevel)), MipLevel);
								if (Lightmap.DoesTileHaveValidCPUData(ParentCoords, RenderState.LightmapRenderer->GetCurrentRevision()))
								{
									Lightmap.TileStorage[Coords].CPUTextureData[0]->Decompress();
									Lightmap.TileStorage[Coords].CPUTextureData[1]->Decompress();
									Lightmap.TileStorage[Coords].CPUTextureData[2]->Decompress();

									Lightmap.TileStorage[ParentCoords].CPUTextureData[0]->Decompress();
									Lightmap.TileStorage[ParentCoords].CPUTextureData[1]->Decompress();
									Lightmap.TileStorage[ParentCoords].CPUTextureData[2]->Decompress();

									for (int32 X = 0; X < GPreviewLightmapVirtualTileSize; X++)
									{
										for (int32 Y = 0; Y < GPreviewLightmapVirtualTileSize; Y++)
										{
											FIntPoint DstPixelPosition = FIntPoint(X, Y);
											FIntPoint SrcPixelPosition = (FIntPoint(TileX, TileY) * GPreviewLightmapVirtualTileSize + FIntPoint(X, Y)) / (1 << MipLevel);
											SrcPixelPosition.X %= GPreviewLightmapVirtualTileSize;
											SrcPixelPosition.Y %= GPreviewLightmapVirtualTileSize;

											int32 DstRowPitchInPixels = GPreviewLightmapVirtualTileSize;
											int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;

											int32 SrcLinearIndex = SrcPixelPosition.Y * SrcRowPitchInPixels + SrcPixelPosition.X;
											int32 DstLinearIndex = DstPixelPosition.Y * DstRowPitchInPixels + DstPixelPosition.X;

											Lightmap.TileStorage[Coords].CPUTextureData[0]->Data[DstLinearIndex] = Lightmap.TileStorage[ParentCoords].CPUTextureData[0]->Data[SrcLinearIndex];
											Lightmap.TileStorage[Coords].CPUTextureData[1]->Data[DstLinearIndex] = Lightmap.TileStorage[ParentCoords].CPUTextureData[1]->Data[SrcLinearIndex];
											Lightmap.TileStorage[Coords].CPUTextureData[2]->Data[DstLinearIndex] = Lightmap.TileStorage[ParentCoords].CPUTextureData[2]->Data[SrcLinearIndex];
										}
									}

									break;
								}
							}
						}
					}
				}
			}
		}

		SlowTask.EnterProgressFrame(1, LOCTEXT("EncodingTexturesStaticLightingStatis", "Encoding textures"));

		{
			int32 NumLightmapsToTranscode = 0;

			for (int32 InstanceIndex = 0; InstanceIndex < StaticMeshInstances.Elements.Num(); InstanceIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < StaticMeshInstances.Elements[InstanceIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (StaticMeshInstances.Elements[InstanceIndex].LODLightmaps[LODIndex].IsValid())
					{
						NumLightmapsToTranscode++;
					}
				}
			}

			for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < InstanceGroups.Elements.Num(); InstanceGroupIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < InstanceGroups.Elements[InstanceGroupIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (InstanceGroups.Elements[InstanceGroupIndex].LODLightmaps[LODIndex].IsValid())
					{
						NumLightmapsToTranscode++;
					}
				}
			}

			for (int32 LandscapeIndex = 0; LandscapeIndex < Landscapes.Elements.Num(); LandscapeIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < Landscapes.Elements[LandscapeIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (Landscapes.Elements[LandscapeIndex].LODLightmaps[LODIndex].IsValid())
					{
						NumLightmapsToTranscode++;
					}
				}
			}

			FDenoiserContext DenoiserContext;

			FScopedSlowTask SubSlowTask(NumLightmapsToTranscode, LOCTEXT("TranscodingLightmaps", "Transcoding lightmaps"));
			SubSlowTask.MakeDialog();

			for (int32 InstanceIndex = 0; InstanceIndex < StaticMeshInstances.Elements.Num(); InstanceIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < StaticMeshInstances.Elements[InstanceIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (StaticMeshInstances.Elements[InstanceIndex].LODLightmaps[LODIndex].IsValid())
					{
						if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("DenoisingAndTranscodingLightmaps", "Denoising & transcoding lightmaps"));
						}
						else
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("TranscodingLightmaps", "Transcoding lightmaps"));
						}

						FLightmapRenderState& Lightmap = RenderState.LightmapRenderStates.Elements[StaticMeshInstances.Elements[InstanceIndex].LODLightmaps[LODIndex].GetElementId()];

						for (auto& Tile : Lightmap.TileStorage)
						{
							Tile.Value.CPUTextureData[0]->Decompress();
							Tile.Value.CPUTextureData[1]->Decompress();
							Tile.Value.CPUTextureData[2]->Decompress();
						}

						// Transencode GI layers
						TArray<FLightSampleData> LightSampleData;
						LightSampleData.AddZeroed(Lightmap.GetSize().X * Lightmap.GetSize().Y); // LightSampleData will have different row pitch as VT is padded to tiles

						{
							int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
							int32 DstRowPitchInPixels = Lightmap.GetSize().X;

							CopyRectTiled(
								FIntPoint(0, 0),
								FIntRect(FIntPoint(0, 0), Lightmap.GetSize()),
								SrcRowPitchInPixels,
								DstRowPitchInPixels,
								[&Lightmap, &LightSampleData](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
							{
								LightSampleData[DstLinearIndex] = ConvertToLightSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[0]->Data[SrcLinearIndex], Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[1]->Data[SrcLinearIndex]);
							});
						}

#if 0
						{
							// Debug: dump color and SH as pfm
							TArray<FVector> Color;
							TArray<FVector> SH;
							Color.AddZeroed(Lightmap.GetSize().X * Lightmap.GetSize().Y);
							SH.AddZeroed(Lightmap.GetSize().X * Lightmap.GetSize().Y);

							for (int32 Y = 0; Y < Lightmap.GetSize().Y; Y++)
							{
								for (int32 X = 0; X < Lightmap.GetSize().X; X++)
								{
									Color[Y * Lightmap.GetSize().X + X][0] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[0][0];
									Color[Y * Lightmap.GetSize().X + X][1] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[0][1];
									Color[Y * Lightmap.GetSize().X + X][2] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[0][2];

									SH[Y * Lightmap.GetSize().X + X][0] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[1][0];
									SH[Y * Lightmap.GetSize().X + X][1] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[1][1];
									SH[Y * Lightmap.GetSize().X + X][2] = LightSampleData[Y * Lightmap.GetSize().X + X].Coefficients[1][2];
								}
							}

							{
								FFileHelper::SaveStringToFile(
									FString::Printf(TEXT("PF\n%d %d\n-1.0\n"), Lightmap.GetSize().X, Lightmap.GetSize().Y),
									*FString::Printf(TEXT("%s_Irradiance_%dspp.pfm"), *Lightmap.Name, GGPULightmassSamplesPerTexel),
									FFileHelper::EEncodingOptions::ForceAnsi
								);

								TArray<uint8> Bytes((uint8*)Color.GetData(), Color.GetTypeSize() * Color.Num());

								FFileHelper::SaveArrayToFile(Bytes, *FString::Printf(TEXT("%s_Irradiance_%dspp.pfm"), *Lightmap.Name, GGPULightmassSamplesPerTexel), &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
							}

							{
								FFileHelper::SaveStringToFile(
									FString::Printf(TEXT("PF\n%d %d\n-1.0\n"), Lightmap.GetSize().X, Lightmap.GetSize().Y),
									*FString::Printf(TEXT("%s_SH_%dspp.pfm"), *Lightmap.Name, GGPULightmassSamplesPerTexel),
									FFileHelper::EEncodingOptions::ForceAnsi
								);

								TArray<uint8> Bytes((uint8*)SH.GetData(), SH.GetTypeSize() * SH.Num());

								FFileHelper::SaveArrayToFile(Bytes, *FString::Printf(TEXT("%s_SH_%dspp.pfm"), *Lightmap.Name, GGPULightmassSamplesPerTexel), &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
							}
						}
#endif

						if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
						{
							DenoiseLightSampleData(Lightmap.GetSize(), LightSampleData, DenoiserContext);
						}

						FQuantizedLightmapData* QuantizedLightmapData = new FQuantizedLightmapData();
						QuantizedLightmapData->SizeX = Lightmap.GetSize().X;
						QuantizedLightmapData->SizeY = Lightmap.GetSize().Y;

						QuantizeLightSamples(LightSampleData, QuantizedLightmapData->Data, QuantizedLightmapData->Scale, QuantizedLightmapData->Add);

						// Add static lights to lightmap data
						{
							for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
							{
								if (!DirectionalLight.bStationary)
								{
									UDirectionalLightComponent* Light = DirectionalLight.ComponentUObject;
									QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
								}
							}

							for (FPointLightBuildInfo& PointLight : LightScene.PointLights.Elements)
							{
								if (!PointLight.bStationary)
								{
									UPointLightComponent* Light = PointLight.ComponentUObject;
									if (PointLight.AffectsBounds(StaticMeshInstances.Elements[InstanceIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}

							for (FSpotLightBuildInfo& SpotLight : LightScene.SpotLights.Elements)
							{
								if (!SpotLight.bStationary)
								{
									USpotLightComponent* Light = SpotLight.ComponentUObject;
									if (SpotLight.AffectsBounds(StaticMeshInstances.Elements[InstanceIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}

							for (FRectLightBuildInfo& RectLight : LightScene.RectLights.Elements)
							{
								if (!RectLight.bStationary)
								{
									URectLightComponent* Light = RectLight.ComponentUObject;
									if (RectLight.AffectsBounds(StaticMeshInstances.Elements[InstanceIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}
						}

						// Transencode stationary light shadow masks
						TMap<ULightComponent*, FShadowMapData2D*> ShadowMaps;
						{
							auto TransencodeShadowMap = [&Lightmap, &ShadowMaps](
								FLocalLightBuildInfo& LightBuildInfo,
								FLocalLightRenderState& Light
								)
							{
								check(Light.bStationary);
								check(Light.ShadowMapChannel != INDEX_NONE);
								FQuantizedShadowSignedDistanceFieldData2D* ShadowMap = new FQuantizedShadowSignedDistanceFieldData2D(Lightmap.GetSize().X, Lightmap.GetSize().Y);

								int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
								int32 DstRowPitchInPixels = Lightmap.GetSize().X;

								CopyRectTiled(
									FIntPoint(0, 0),
									FIntRect(FIntPoint(0, 0), Lightmap.GetSize()),
									SrcRowPitchInPixels,
									DstRowPitchInPixels,
									[&Lightmap, &ShadowMap, &Light](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
								{
									ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], Light.ShadowMapChannel);
								});

								ShadowMaps.Add(LightBuildInfo.GetComponentUObject(), ShadowMap);
							};

							// For all relevant lights
							// Directional lights are always relevant
							for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
							{
								if (!DirectionalLight.bStationary)
								{
									continue;
								}

								int32 ElementId = &DirectionalLight - LightScene.DirectionalLights.Elements.GetData();
								TransencodeShadowMap(DirectionalLight, RenderState.LightSceneRenderState.DirectionalLights.Elements[ElementId]);
							}

							for (FPointLightRenderStateRef& PointLight : Lightmap.RelevantPointLights)
							{
								int32 ElementId = PointLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.PointLights.Elements[ElementId], PointLight);
							}

							for (FSpotLightRenderStateRef& SpotLight : Lightmap.RelevantSpotLights)
							{
								int32 ElementId = SpotLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.SpotLights.Elements[ElementId], SpotLight);
							}

							for (FRectLightRenderStateRef& RectLight : Lightmap.RelevantRectLights)
							{
								int32 ElementId = RectLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.RectLights.Elements[ElementId], RectLight);
							}
						}

						{
							UStaticMeshComponent* StaticMeshComponent = StaticMeshInstances.Elements[InstanceIndex].ComponentUObject;
							if (StaticMeshComponent && StaticMeshComponent->GetOwner() && StaticMeshComponent->GetOwner()->GetLevel())
							{
								// Should have happened at a higher level
								check(!StaticMeshComponent->IsRenderStateCreated());
								// The rendering thread reads from LODData and IrrelevantLights, therefore
								// the component must have finished detaching from the scene on the rendering
								// thread before it is safe to continue.
								check(StaticMeshComponent->AttachmentCounter.GetValue() == 0);

								// Ensure LODData has enough entries in it, free not required.
								const bool bLODDataCountChanged = StaticMeshComponent->SetLODDataCount(LODIndex + 1, StaticMeshComponent->GetStaticMesh()->GetNumLODs());
								if (bLODDataCountChanged)
								{
									StaticMeshComponent->MarkPackageDirty();
								}

								FStaticMeshComponentLODInfo& ComponentLODInfo = StaticMeshComponent->LODData[LODIndex];

								if (ComponentLODInfo.CreateMapBuildDataId(LODIndex))
								{
									StaticMeshComponent->MarkPackageDirty();
								}

								ELightMapPaddingType PaddingType = GAllowLightmapPadding ? LMPT_NormalPadding : LMPT_NoPadding;
								const bool bHasNonZeroData = QuantizedLightmapData->HasNonZeroData();

								ULevel* StorageLevel = LightingScenario ? LightingScenario : StaticMeshComponent->GetOwner()->GetLevel();
								UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
								FMeshMapBuildData& MeshBuildData = Registry->AllocateMeshBuildData(ComponentLODInfo.MapBuildDataId, true);

								const bool bNeedsLightMap = true;// bHasNonZeroData;
								if (bNeedsLightMap)
								{
									// Create a light-map for the primitive.
									MeshBuildData.LightMap = FLightMap2D::AllocateLightMap(
										Registry,
										QuantizedLightmapData,
										ShadowMaps,
										StaticMeshComponent->Bounds,
										PaddingType,
										LMF_Streamed
									);
								}
								else
								{
									MeshBuildData.LightMap = NULL;
									delete QuantizedLightmapData;
								}
							}
						}

						FTileDataLayer::Evict();
					}
				}
			}

			for (int32 InstanceGroupIndex = 0; InstanceGroupIndex < InstanceGroups.Elements.Num(); InstanceGroupIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < InstanceGroups.Elements[InstanceGroupIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (InstanceGroups.Elements[InstanceGroupIndex].LODLightmaps[LODIndex].IsValid())
					{
						if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("DenoisingAndTranscodingLightmaps", "Denoising & transcoding lightmaps"));
						}
						else
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("TranscodingLightmaps", "Transcoding lightmaps"));
						}

						FLightmapRenderState& Lightmap = RenderState.LightmapRenderStates.Elements[InstanceGroups.Elements[InstanceGroupIndex].LODLightmaps[LODIndex].GetElementId()];

						for (auto& Tile : Lightmap.TileStorage)
						{
							Tile.Value.CPUTextureData[0]->Decompress();
							Tile.Value.CPUTextureData[1]->Decompress();
							Tile.Value.CPUTextureData[2]->Decompress();
						}

						FInstanceGroup& InstanceGroup = InstanceGroups.Elements[InstanceGroupIndex];

						int32 BaseLightMapWidth = InstanceGroup.LODPerInstanceLightmapSize[LODIndex].X;
						int32 BaseLightMapHeight = InstanceGroup.LODPerInstanceLightmapSize[LODIndex].Y;

						int32 InstancesPerRow = FMath::CeilToInt(FMath::Sqrt(InstanceGroup.ComponentUObject->PerInstanceSMData.Num()));

						// Transencode GI layers
						TArray<TArray<FLightSampleData>> InstanceGroupLightSampleData;
						TArray<TUniquePtr<FQuantizedLightmapData>> InstancedSourceQuantizedData;
						TArray<TMap<ULightComponent*, TUniquePtr<FShadowMapData2D>>> InstancedShadowMapData;
						InstanceGroupLightSampleData.AddDefaulted(InstanceGroup.ComponentUObject->PerInstanceSMData.Num());
						InstancedSourceQuantizedData.AddDefaulted(InstanceGroup.ComponentUObject->PerInstanceSMData.Num());
						InstancedShadowMapData.AddDefaulted(InstanceGroup.ComponentUObject->PerInstanceSMData.Num());

						for (int32 InstanceIndex = 0; InstanceIndex < InstanceGroupLightSampleData.Num(); InstanceIndex++)
						{
							TArray<FLightSampleData>& LightSampleData = InstanceGroupLightSampleData[InstanceIndex];
							LightSampleData.AddZeroed(BaseLightMapWidth * BaseLightMapHeight);
							InstancedSourceQuantizedData[InstanceIndex] = MakeUnique<FQuantizedLightmapData>();

							int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
							int32 DstRowPitchInPixels = BaseLightMapWidth;

							int32 RenderIndex = InstanceGroup.ComponentUObject->GetRenderIndex(InstanceIndex);

							if (RenderIndex != INDEX_NONE)
							{
								FIntPoint InstanceTilePos = FIntPoint(RenderIndex % InstancesPerRow, RenderIndex / InstancesPerRow);
								FIntPoint InstanceTileMin = FIntPoint(InstanceTilePos.X * BaseLightMapWidth, InstanceTilePos.Y * BaseLightMapHeight);

								CopyRectTiled(
									InstanceTileMin,
									FIntRect(FIntPoint(0, 0), FIntPoint(BaseLightMapWidth, BaseLightMapHeight)),
									SrcRowPitchInPixels,
									DstRowPitchInPixels,
									[&Lightmap, &LightSampleData](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
								{
									LightSampleData[DstLinearIndex] = ConvertToLightSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[0]->Data[SrcLinearIndex], Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[1]->Data[SrcLinearIndex]);
								});
							}

							if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
							{
								DenoiseLightSampleData(FIntPoint(BaseLightMapWidth, BaseLightMapHeight), LightSampleData, DenoiserContext);
							}

							FQuantizedLightmapData& QuantizedLightmapData = *InstancedSourceQuantizedData[InstanceIndex];
							QuantizedLightmapData.SizeX = BaseLightMapWidth;
							QuantizedLightmapData.SizeY = BaseLightMapHeight;

							QuantizeLightSamples(LightSampleData, QuantizedLightmapData.Data, QuantizedLightmapData.Scale, QuantizedLightmapData.Add);

							// Transencode stationary light shadow masks
							TMap<ULightComponent*, TUniquePtr<FShadowMapData2D>>& ShadowMaps = InstancedShadowMapData[InstanceIndex];

							{
								// For all relevant lights
								// Directional lights are always relevant
								for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
								{
									if (!DirectionalLight.bStationary)
									{
										continue;
									}

									check(DirectionalLight.ShadowMapChannel != INDEX_NONE);
									TUniquePtr<FQuantizedShadowSignedDistanceFieldData2D> ShadowMap = MakeUnique<FQuantizedShadowSignedDistanceFieldData2D>(BaseLightMapWidth, BaseLightMapHeight);

									if (RenderIndex != INDEX_NONE)
									{
										FIntPoint InstanceTilePos = FIntPoint(RenderIndex % InstancesPerRow, RenderIndex / InstancesPerRow);
										FIntPoint InstanceTileMin = FIntPoint(InstanceTilePos.X * BaseLightMapWidth, InstanceTilePos.Y * BaseLightMapHeight);

										CopyRectTiled(											
											InstanceTileMin,
											FIntRect(FIntPoint(0, 0), FIntPoint(BaseLightMapWidth, BaseLightMapHeight)),
											SrcRowPitchInPixels,
											DstRowPitchInPixels,
											[&Lightmap, &ShadowMap, &DirectionalLight](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
										{
											ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], DirectionalLight.ShadowMapChannel);
										});
									}

									ShadowMaps.Add(DirectionalLight.ComponentUObject, MoveTemp(ShadowMap));
								}

								for (FPointLightRenderStateRef& PointLight : Lightmap.RelevantPointLights)
								{
									check(PointLight->bStationary);
									check(PointLight->ShadowMapChannel != INDEX_NONE);
									TUniquePtr<FQuantizedShadowSignedDistanceFieldData2D> ShadowMap = MakeUnique<FQuantizedShadowSignedDistanceFieldData2D>(BaseLightMapWidth, BaseLightMapHeight);

									if (RenderIndex != INDEX_NONE)
									{
										FIntPoint InstanceTilePos = FIntPoint(RenderIndex % InstancesPerRow, RenderIndex / InstancesPerRow);
										FIntPoint InstanceTileMin = FIntPoint(InstanceTilePos.X * BaseLightMapWidth, InstanceTilePos.Y * BaseLightMapHeight);

										CopyRectTiled(
											InstanceTileMin,
											FIntRect(FIntPoint(0, 0), FIntPoint(BaseLightMapWidth, BaseLightMapHeight)),
											SrcRowPitchInPixels,
											DstRowPitchInPixels,
											[&Lightmap, &ShadowMap, &PointLight](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
										{
											ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], PointLight->ShadowMapChannel);
										});
									}

									ShadowMaps.Add(LightScene.PointLights.Elements[PointLight.GetElementIdChecked()].ComponentUObject, MoveTemp(ShadowMap));
								}

								for (FSpotLightRenderStateRef& SpotLight : Lightmap.RelevantSpotLights)
								{
									check(SpotLight->bStationary);
									check(SpotLight->ShadowMapChannel != INDEX_NONE);
									TUniquePtr<FQuantizedShadowSignedDistanceFieldData2D> ShadowMap = MakeUnique<FQuantizedShadowSignedDistanceFieldData2D>(BaseLightMapWidth, BaseLightMapHeight);

									if (RenderIndex != INDEX_NONE)
									{
										FIntPoint InstanceTilePos = FIntPoint(RenderIndex % InstancesPerRow, RenderIndex / InstancesPerRow);
										FIntPoint InstanceTileMin = FIntPoint(InstanceTilePos.X * BaseLightMapWidth, InstanceTilePos.Y * BaseLightMapHeight);

										CopyRectTiled(
											InstanceTileMin,
											FIntRect(FIntPoint(0, 0), FIntPoint(BaseLightMapWidth, BaseLightMapHeight)),
											SrcRowPitchInPixels,
											DstRowPitchInPixels,
											[&Lightmap, &ShadowMap, &SpotLight](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
										{
											ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], SpotLight->ShadowMapChannel);
										});
									}

									ShadowMaps.Add(LightScene.SpotLights.Elements[SpotLight.GetElementIdChecked()].ComponentUObject, MoveTemp(ShadowMap));
								}

								for (FRectLightRenderStateRef& RectLight : Lightmap.RelevantRectLights)
								{
									check(RectLight->bStationary);
									check(RectLight->ShadowMapChannel != INDEX_NONE);
									TUniquePtr<FQuantizedShadowSignedDistanceFieldData2D> ShadowMap = MakeUnique<FQuantizedShadowSignedDistanceFieldData2D>(BaseLightMapWidth, BaseLightMapHeight);

									if (RenderIndex != INDEX_NONE)
									{
										FIntPoint InstanceTilePos = FIntPoint(RenderIndex % InstancesPerRow, RenderIndex / InstancesPerRow);
										FIntPoint InstanceTileMin = FIntPoint(InstanceTilePos.X * BaseLightMapWidth, InstanceTilePos.Y * BaseLightMapHeight);

										CopyRectTiled(
											InstanceTileMin,
											FIntRect(FIntPoint(0, 0), FIntPoint(BaseLightMapWidth, BaseLightMapHeight)),
											SrcRowPitchInPixels,
											DstRowPitchInPixels,
											[&Lightmap, &ShadowMap, &RectLight](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
										{
											ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], RectLight->ShadowMapChannel);
										});
									}

									ShadowMaps.Add(LightScene.RectLights.Elements[RectLight.GetElementIdChecked()].ComponentUObject, MoveTemp(ShadowMap));
								}
							}
						}

						// Add static lights to lightmap data
						// Instanced lightmaps will eventually be merged together, so just add to the first one
						if (InstancedSourceQuantizedData.Num() > 0)
						{
							TUniquePtr<FQuantizedLightmapData>& QuantizedLightmapData = InstancedSourceQuantizedData[0];
							{
								for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
								{
									if (!DirectionalLight.bStationary)
									{
										UDirectionalLightComponent* Light = DirectionalLight.ComponentUObject;
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}

								for (FPointLightBuildInfo& PointLight : LightScene.PointLights.Elements)
								{
									if (!PointLight.bStationary)
									{
										UPointLightComponent* Light = PointLight.ComponentUObject;
										if (PointLight.AffectsBounds(InstanceGroup.WorldBounds))
										{
											QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
										}
									}
								}

								for (FSpotLightBuildInfo& SpotLight : LightScene.SpotLights.Elements)
								{
									if (!SpotLight.bStationary)
									{
										USpotLightComponent* Light = SpotLight.ComponentUObject;
										if (SpotLight.AffectsBounds(InstanceGroup.WorldBounds))
										{
											QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
										}
									}
								}

								for (FRectLightBuildInfo& RectLight : LightScene.RectLights.Elements)
								{
									if (!RectLight.bStationary)
									{
										URectLightComponent* Light = RectLight.ComponentUObject;
										if (RectLight.AffectsBounds(InstanceGroup.WorldBounds))
										{
											QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
										}
									}
								}
							}
						}

						UStaticMesh* ResolvedMesh = InstanceGroup.ComponentUObject->GetStaticMesh();
						if (InstanceGroup.ComponentUObject->LODData.Num() != ResolvedMesh->GetNumLODs())
						{
							InstanceGroup.ComponentUObject->MarkPackageDirty();
						}

						// Ensure LODData has enough entries in it, free not required.
						InstanceGroup.ComponentUObject->SetLODDataCount(ResolvedMesh->GetNumLODs(), ResolvedMesh->GetNumLODs());

						FStaticMeshComponentLODInfo& ComponentLODInfo = InstanceGroup.ComponentUObject->LODData[LODIndex];

						if (ComponentLODInfo.CreateMapBuildDataId(LODIndex))
						{
							InstanceGroup.ComponentUObject->MarkPackageDirty();
						}

						ULevel* StorageLevel = LightingScenario ? LightingScenario : InstanceGroup.ComponentUObject->GetOwner()->GetLevel();
						UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
						FMeshMapBuildData& MeshBuildData = Registry->AllocateMeshBuildData(InstanceGroup.ComponentUObject->LODData[LODIndex].MapBuildDataId, true);

						MeshBuildData.PerInstanceLightmapData.Empty(InstancedSourceQuantizedData.Num());
						MeshBuildData.PerInstanceLightmapData.AddZeroed(InstancedSourceQuantizedData.Num());

						// Create a light-map for the primitive.
						// When using VT, shadow map data is included with lightmap allocation
						const ELightMapPaddingType PaddingType = GAllowLightmapPadding ? LMPT_NormalPadding : LMPT_NoPadding;

						TRefCountPtr<FLightMap2D> NewLightMap = FLightMap2D::AllocateInstancedLightMap(Registry, InstanceGroup.ComponentUObject,
							MoveTemp(InstancedSourceQuantizedData),
							MoveTemp(InstancedShadowMapData),
							Registry, InstanceGroup.ComponentUObject->LODData[LODIndex].MapBuildDataId, InstanceGroup.ComponentUObject->Bounds, PaddingType, LMF_Streamed);

						MeshBuildData.LightMap = NewLightMap;

						FTileDataLayer::Evict();
					}
				}
			}

			for (int32 LandscapeIndex = 0; LandscapeIndex < Landscapes.Elements.Num(); LandscapeIndex++)
			{
				for (int32 LODIndex = 0; LODIndex < Landscapes.Elements[LandscapeIndex].LODLightmaps.Num(); LODIndex++)
				{
					if (Landscapes.Elements[LandscapeIndex].LODLightmaps[LODIndex].IsValid())
					{
						if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("DenoisingAndTranscodingLightmaps", "Denoising & transcoding lightmaps"));
						}
						else
						{
							SubSlowTask.EnterProgressFrame(1, LOCTEXT("TranscodingLightmaps", "Transcoding lightmaps"));
						}

						FLightmapRenderState& Lightmap = RenderState.LightmapRenderStates.Elements[Landscapes.Elements[LandscapeIndex].LODLightmaps[LODIndex].GetElementId()];

						for (auto& Tile : Lightmap.TileStorage)
						{
							Tile.Value.CPUTextureData[0]->Decompress();
							Tile.Value.CPUTextureData[1]->Decompress();
							Tile.Value.CPUTextureData[2]->Decompress();
						}

						// Transencode GI layers
						TArray<FLightSampleData> LightSampleData;
						LightSampleData.AddZeroed(Lightmap.GetSize().X * Lightmap.GetSize().Y); // LightSampleData will have different row pitch as VT is padded to tiles

						{
							int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
							int32 DstRowPitchInPixels = Lightmap.GetSize().X;

							CopyRectTiled(
								FIntPoint(0, 0),
								FIntRect(FIntPoint(0, 0), Lightmap.GetSize()),
								SrcRowPitchInPixels,
								DstRowPitchInPixels,
								[&Lightmap, &LightSampleData](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
							{
								LightSampleData[DstLinearIndex] = ConvertToLightSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[0]->Data[SrcLinearIndex], Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[1]->Data[SrcLinearIndex]);
							});
						}

						if (Settings->DenoisingOptions == EGPULightmassDenoisingOptions::OnCompletion)
						{
							DenoiseLightSampleData(Lightmap.GetSize(), LightSampleData, DenoiserContext);
						}

						FQuantizedLightmapData* QuantizedLightmapData = new FQuantizedLightmapData();
						QuantizedLightmapData->SizeX = Lightmap.GetSize().X;
						QuantizedLightmapData->SizeY = Lightmap.GetSize().Y;

						QuantizeLightSamples(LightSampleData, QuantizedLightmapData->Data, QuantizedLightmapData->Scale, QuantizedLightmapData->Add);

						// Add static lights to lightmap data
						{
							for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
							{
								if (!DirectionalLight.bStationary)
								{
									UDirectionalLightComponent* Light = DirectionalLight.ComponentUObject;
									QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
								}
							}

							for (FPointLightBuildInfo& PointLight : LightScene.PointLights.Elements)
							{
								if (!PointLight.bStationary)
								{
									UPointLightComponent* Light = PointLight.ComponentUObject;
									if (PointLight.AffectsBounds(Landscapes.Elements[LandscapeIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}

							for (FSpotLightBuildInfo& SpotLight : LightScene.SpotLights.Elements)
							{
								if (!SpotLight.bStationary)
								{
									USpotLightComponent* Light = SpotLight.ComponentUObject;
									if (SpotLight.AffectsBounds(Landscapes.Elements[LandscapeIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}

							for (FRectLightBuildInfo& RectLight : LightScene.RectLights.Elements)
							{
								if (!RectLight.bStationary)
								{
									URectLightComponent* Light = RectLight.ComponentUObject;
									if (RectLight.AffectsBounds(Landscapes.Elements[LandscapeIndex].WorldBounds))
									{
										QuantizedLightmapData->LightGuids.Add(Light->LightGuid);
									}
								}
							}
						}

						// Transencode stationary light shadow masks
						TMap<ULightComponent*, FShadowMapData2D*> ShadowMaps;
						{
							auto TransencodeShadowMap = [&Lightmap, &ShadowMaps](
								FLocalLightBuildInfo& LightBuildInfo,
								FLocalLightRenderState& Light
								)
							{
								check(Light.bStationary);
								check(Light.ShadowMapChannel != INDEX_NONE);
								FQuantizedShadowSignedDistanceFieldData2D* ShadowMap = new FQuantizedShadowSignedDistanceFieldData2D(Lightmap.GetSize().X, Lightmap.GetSize().Y);

								int32 SrcRowPitchInPixels = GPreviewLightmapVirtualTileSize;
								int32 DstRowPitchInPixels = Lightmap.GetSize().X;

								CopyRectTiled(
									FIntPoint(0, 0),
									FIntRect(FIntPoint(0, 0), Lightmap.GetSize()),
									SrcRowPitchInPixels,
									DstRowPitchInPixels,
									[&Lightmap, &ShadowMap, &Light](int32 DstLinearIndex, FIntPoint SrcTilePosition, int32 SrcLinearIndex) mutable
								{
									ShadowMap->GetData()[DstLinearIndex] = ConvertToShadowSample(Lightmap.TileStorage[FTileVirtualCoordinates(SrcTilePosition, 0)].CPUTextureData[2]->Data[SrcLinearIndex], Light.ShadowMapChannel);
								});

								ShadowMaps.Add(LightBuildInfo.GetComponentUObject(), ShadowMap);
							};

							// For all relevant lights
							// Directional lights are always relevant
							for (FDirectionalLightBuildInfo& DirectionalLight : LightScene.DirectionalLights.Elements)
							{
								if (!DirectionalLight.bStationary)
								{
									continue;
								}

								int32 ElementId = &DirectionalLight - LightScene.DirectionalLights.Elements.GetData();
								TransencodeShadowMap(DirectionalLight, RenderState.LightSceneRenderState.DirectionalLights.Elements[ElementId]);
							}

							for (FPointLightRenderStateRef& PointLight : Lightmap.RelevantPointLights)
							{
								int32 ElementId = PointLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.PointLights.Elements[ElementId], PointLight);
							}

							for (FSpotLightRenderStateRef& SpotLight : Lightmap.RelevantSpotLights)
							{
								int32 ElementId = SpotLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.SpotLights.Elements[ElementId], SpotLight);
							}

							for (FRectLightRenderStateRef& RectLight : Lightmap.RelevantRectLights)
							{
								int32 ElementId = RectLight.GetElementIdChecked();
								TransencodeShadowMap(LightScene.RectLights.Elements[ElementId], RectLight);
							}
						}

						{
							ULandscapeComponent* LandscapeComponent = Landscapes.Elements[LandscapeIndex].ComponentUObject;
							ELightMapPaddingType PaddingType = LMPT_NoPadding;
							const bool bHasNonZeroData = QuantizedLightmapData->HasNonZeroData();

							ULevel* StorageLevel = LightingScenario ? LightingScenario : LandscapeComponent->GetOwner()->GetLevel();
							UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();
							FMeshMapBuildData& MeshBuildData = Registry->AllocateMeshBuildData(LandscapeComponent->MapBuildDataId, true);

							const bool bNeedsLightMap = true;// bHasNonZeroData;
							if (bNeedsLightMap)
							{
								// Create a light-map for the primitive.
								MeshBuildData.LightMap = FLightMap2D::AllocateLightMap(
									Registry,
									QuantizedLightmapData,
									ShadowMaps,
									LandscapeComponent->Bounds,
									PaddingType,
									LMF_Streamed
								);
							}
							else
							{
								MeshBuildData.LightMap = NULL;
								delete QuantizedLightmapData;
							}

							if (ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(LandscapeComponent->GetOuter()))
							{
								TSet<ULandscapeComponent*> Components;
								Components.Add(LandscapeComponent);
								Proxy->FlushGrassComponents(&Components, false);
							}
						}

						FTileDataLayer::Evict();
					}
				}
			}

		}

		GCompressLightmaps = World->GetWorldSettings()->LightmassSettings.bCompressLightmaps;

		FLightMap2D::EncodeTextures(World, LightingScenario, true, true);
		FShadowMap2D::EncodeTextures(World, LightingScenario, true, true);

		SlowTask.EnterProgressFrame(1, LOCTEXT("ApplyingNewLighting", "Applying new lighting"));

		for (int32 LevelIndex = 0; LevelIndex < World->GetNumLevels(); LevelIndex++)
		{
			bool bMarkLevelDirty = false;
			ULevel* Level = World->GetLevel(LevelIndex);

			if (Level)
			{
				if (Level->bIsVisible && (!Level->bIsLightingScenario || Level == LightingScenario))
				{
					ULevel* StorageLevel = LightingScenario ? LightingScenario : Level;
					UMapBuildDataRegistry* Registry = StorageLevel->GetOrCreateMapBuildData();

					Registry->SetupLightmapResourceClusters();

					Level->InitializeRenderingResources();
				}
			}
		}
	}
}

void FScene::RemoveAllComponents()
{
	TArray<UStaticMeshComponent*> RegisteredStaticMeshComponents;
	TArray<UInstancedStaticMeshComponent*> RegisteredInstancedStaticMeshComponents;
	TArray<ULandscapeComponent*> RegisteredLandscapeComponents;
	RegisteredStaticMeshComponentUObjects.GetKeys(RegisteredStaticMeshComponents);
	RegisteredInstancedStaticMeshComponentUObjects.GetKeys(RegisteredInstancedStaticMeshComponents);
	RegisteredLandscapeComponentUObjects.GetKeys(RegisteredLandscapeComponents);

	for (auto Component : RegisteredStaticMeshComponents)
	{
		RemoveGeometryInstanceFromComponent(Component);
	}
	for (auto Component : RegisteredInstancedStaticMeshComponents)
	{
		RemoveGeometryInstanceFromComponent(Component);
	}
	for (auto Component : RegisteredLandscapeComponents)
	{
		RemoveGeometryInstanceFromComponent(Component);
	}

	TArray<UDirectionalLightComponent*> RegisteredDirectionalLightComponents;
	TArray<UPointLightComponent*> RegisteredPointLightComponents;
	TArray<USpotLightComponent*> RegisteredSpotLightComponents;
	TArray<URectLightComponent*> RegisteredRectLightComponents;
	LightScene.RegisteredDirectionalLightComponentUObjects.GetKeys(RegisteredDirectionalLightComponents);
	LightScene.RegisteredPointLightComponentUObjects.GetKeys(RegisteredPointLightComponents);
	LightScene.RegisteredSpotLightComponentUObjects.GetKeys(RegisteredSpotLightComponents);
	LightScene.RegisteredRectLightComponentUObjects.GetKeys(RegisteredRectLightComponents);

	for (auto Light : RegisteredDirectionalLightComponents)
	{
		RemoveLight(Light);
	}
	for (auto Light : RegisteredPointLightComponents)
	{
		RemoveLight(Light);
	}
	for (auto Light : RegisteredSpotLightComponents)
	{
		RemoveLight(Light);
	}
	for (auto Light : RegisteredRectLightComponents)
	{
		RemoveLight(Light);
	}

	if (LightScene.SkyLight.IsSet())
	{
		RemoveLight(LightScene.SkyLight->ComponentUObject);
	}
}

}


#undef LOCTEXT_NAMESPACE
