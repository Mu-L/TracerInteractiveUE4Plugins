// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyLightComponent.cpp: SkyLightComponent implementation.
=============================================================================*/

#include "Components/SkyLightComponent.h"
#include "Engine/Texture2D.h"
#include "SceneManagement.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/ScopeLock.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Engine/SkyLight.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Net/UnrealNetwork.h"
#include "Misc/MapErrors.h"
#include "ShaderCompiler.h"
#include "Components/BillboardComponent.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Text.h"

#if RHI_RAYTRACING
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#endif

#define LOCTEXT_NAMESPACE "SkyLightComponent"

void OnUpdateSkylights(UWorld* InWorld)
{
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* SkylightComponent = *It;
		if (InWorld->ContainsActor(SkylightComponent->GetOwner()) && !SkylightComponent->IsPendingKill())
		{			
			SkylightComponent->SetCaptureIsDirty();			
		}
	}
	USkyLightComponent::UpdateSkyCaptureContents(InWorld);
}

static bool SkipStaticSkyLightCapture(USkyLightComponent& SkyLight)
{
	// We do the following because capture is a heavy operation that can time out on some platforms at launch. But it is not needed for a static sky light.
	// According to mobility, we remove sky light from capture update queue if Mobility==Static==StaticLighting. The render side proxy will never be created.
	// We do not even need to check if lighting as been built because the skylight does not generate reflection in the static mobility case.
	// and Lightmass will capture the scene in any case independently using CaptureEmissiveRadianceEnvironmentCubeMap.
	// This is also fine in editor because a static sky light will not contribute to any lighting when drag and drop in a level and captured. 
	// In this case only a "lighting build" will result in usable lighting on any objects.
	// One exception however is when ray tracing is enabled as light mobility is not relevant to ray tracing effects, many still requiring information from the sky light even if it is static.
	return SkyLight.HasStaticLighting() && !IsRayTracingEnabled();
}

FAutoConsoleCommandWithWorld CaptureConsoleCommand(
	TEXT("r.SkylightRecapture"),
	TEXT("Updates all stationary and movable skylights, useful for debugging the capture pipeline"),
	FConsoleCommandWithWorldDelegate::CreateStatic(OnUpdateSkylights)
	);

int32 GUpdateSkylightsEveryFrame = 0;
FAutoConsoleVariableRef CVarUpdateSkylightsEveryFrame(
	TEXT("r.SkylightUpdateEveryFrame"),
	GUpdateSkylightsEveryFrame,
	TEXT("Whether to update all skylights every frame.  Useful for debugging."),
	ECVF_Default
	);

float GSkylightIntensityMultiplier = 1.0f;
FAutoConsoleVariableRef CVarSkylightIntensityMultiplier(
	TEXT("r.SkylightIntensityMultiplier"),
	GSkylightIntensityMultiplier,
	TEXT("Intensity scale on Stationary and Movable skylights.  This is useful to control overall lighting contrast in dynamically lit games with scalability levels which disable Ambient Occlusion.  For example, if medium quality disables SSAO and DFAO, reduce skylight intensity."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

constexpr EPixelFormat SKYLIGHT_CUBEMAP_FORMAT = PF_FloatRGBA;

void FSkyTextureCubeResource::InitRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM5 || GSupportsRenderTargetFormat_PF_FloatRGBA)
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = TEXT("SkyTextureCube");
		
		checkf(FMath::IsPowerOfTwo(Size), TEXT("Size of SkyTextureCube must be a power of two; size is %d"), Size);
		TextureCubeRHI = RHICreateTextureCube(Size, Format, NumMips, 0, CreateInfo);
		TextureRHI = TextureCubeRHI;

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer
		(
			SF_Trilinear,
			AM_Clamp,
			AM_Clamp,
			AM_Clamp
		);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}
}

void FSkyTextureCubeResource::Release()
{
	check( IsInGameThread() );
	checkSlow(NumRefs > 0);
	if(--NumRefs == 0)
	{
		BeginReleaseResource(this);
		// Have to defer actual deletion until above rendering command has been processed, we will use the deferred cleanup interface for that
		BeginCleanup(this);
	}
}

void UWorld::UpdateAllSkyCaptures()
{
	TArray<USkyLightComponent*> UpdatedComponents;

	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* CaptureComponent = *It;

		if (ContainsActor(CaptureComponent->GetOwner()) && !CaptureComponent->IsPendingKill())
		{
			// Purge cached derived data and force an update
			CaptureComponent->SetCaptureIsDirty();
			UpdatedComponents.Add(CaptureComponent);
		}
	}

	USkyLightComponent::UpdateSkyCaptureContents(this);
}

void FSkyLightSceneProxy::Initialize(
	float InBlendFraction, 
	const FSHVectorRGB3* InIrradianceEnvironmentMap, 
	const FSHVectorRGB3* BlendDestinationIrradianceEnvironmentMap,
	const float* InAverageBrightness,
	const float* BlendDestinationAverageBrightness)
{
	BlendFraction = FMath::Clamp(InBlendFraction, 0.0f, 1.0f);

	if (BlendFraction > 0 && BlendDestinationProcessedTexture != NULL)
	{
		if (BlendFraction < 1)
		{
			IrradianceEnvironmentMap = (*InIrradianceEnvironmentMap) * (1 - BlendFraction) + (*BlendDestinationIrradianceEnvironmentMap) * BlendFraction;
			AverageBrightness = *InAverageBrightness * (1 - BlendFraction) + (*BlendDestinationAverageBrightness) * BlendFraction;
		}
		else
		{
			// Blend is full destination, treat as source to avoid blend overhead in shaders
			IrradianceEnvironmentMap = *BlendDestinationIrradianceEnvironmentMap;
			AverageBrightness = *BlendDestinationAverageBrightness;
		}
	}
	else
	{
		// Blend is full source
		IrradianceEnvironmentMap = *InIrradianceEnvironmentMap;
		AverageBrightness = *InAverageBrightness;
		BlendFraction = 0;
	}
}

FLinearColor FSkyLightSceneProxy::GetEffectiveLightColor() const
{
	return LightColor * GSkylightIntensityMultiplier;
}

FSkyLightSceneProxy::FSkyLightSceneProxy(const USkyLightComponent* InLightComponent)
	: LightComponent(InLightComponent)
	, ProcessedTexture(InLightComponent->ProcessedSkyTexture)
	, SkyDistanceThreshold(InLightComponent->SkyDistanceThreshold)
	, BlendDestinationProcessedTexture(InLightComponent->BlendDestinationProcessedSkyTexture)
	, bCastShadows(InLightComponent->CastShadows)
	, bWantsStaticShadowing(InLightComponent->Mobility == EComponentMobility::Stationary)
	, bHasStaticLighting(InLightComponent->HasStaticLighting())
	, bCastVolumetricShadow(InLightComponent->bCastVolumetricShadow)
	, bCastRayTracedShadow(InLightComponent->bCastRaytracedShadow)
	, bAffectReflection(InLightComponent->bAffectReflection)
	, bAffectGlobalIllumination(InLightComponent->bAffectGlobalIllumination)
	, OcclusionCombineMode(InLightComponent->OcclusionCombineMode)
	, IndirectLightingIntensity(InLightComponent->IndirectLightingIntensity)
	, VolumetricScatteringIntensity(FMath::Max(InLightComponent->VolumetricScatteringIntensity, 0.0f))
	, OcclusionMaxDistance(InLightComponent->OcclusionMaxDistance)
	, Contrast(InLightComponent->Contrast)
	, OcclusionExponent(FMath::Clamp(InLightComponent->OcclusionExponent, .1f, 10.0f))
	, MinOcclusion(FMath::Clamp(InLightComponent->MinOcclusion, 0.0f, 1.0f))
	, OcclusionTint(InLightComponent->OcclusionTint)
	, SamplesPerPixel(InLightComponent->SamplesPerPixel)
#if RHI_RAYTRACING
	, ImportanceSamplingData(InLightComponent->ImportanceSamplingData)
#endif
	, LightColor(FLinearColor(InLightComponent->LightColor) * InLightComponent->Intensity)
	, bMovable(InLightComponent->IsMovable())
{
	const FSHVectorRGB3* InIrradianceEnvironmentMap = &InLightComponent->IrradianceEnvironmentMap;
	const FSHVectorRGB3* BlendDestinationIrradianceEnvironmentMap = &InLightComponent->BlendDestinationIrradianceEnvironmentMap;
	const float* InAverageBrightness = &InLightComponent->AverageBrightness;
	const float* BlendDestinationAverageBrightness = &InLightComponent->BlendDestinationAverageBrightness;
	float InBlendFraction = InLightComponent->BlendFraction;
	FSkyLightSceneProxy* LightSceneProxy = this;
	ENQUEUE_RENDER_COMMAND(FInitSkyProxy)(
		[InIrradianceEnvironmentMap, BlendDestinationIrradianceEnvironmentMap, InAverageBrightness, BlendDestinationAverageBrightness, InBlendFraction, LightSceneProxy](FRHICommandList& RHICmdList)
		{
			// Only access the irradiance maps on the RT, even though they belong to the USkyLightComponent, 
			// Because FScene::UpdateSkyCaptureContents does not block the RT so the writes could still be in flight
			LightSceneProxy->Initialize(InBlendFraction, InIrradianceEnvironmentMap, BlendDestinationIrradianceEnvironmentMap, InAverageBrightness, BlendDestinationAverageBrightness);
		});
}

USkyLightComponent::USkyLightComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> StaticTexture(TEXT("/Engine/EditorResources/LightIcons/SkyLight"));
		StaticEditorTexture = StaticTexture.Object;
		StaticEditorTextureScale = 1.0f;
		DynamicEditorTexture = StaticTexture.Object;
		DynamicEditorTextureScale = 1.0f;
	}
#endif

	Brightness_DEPRECATED = 1;
	Intensity = 1;
	IndirectLightingIntensity = 1.0f;
	SkyDistanceThreshold = 150000;
	Mobility = EComponentMobility::Stationary;
	bLowerHemisphereIsBlack = true;
	bSavedConstructionScriptValuesValid = true;
	bHasEverCaptured = false;
	OcclusionMaxDistance = 1000;
	MinOcclusion = 0;
	OcclusionExponent = 1;
	OcclusionTint = FColor::Black;
	CubemapResolution = 128;
	LowerHemisphereColor = FLinearColor::Black;
	AverageBrightness = 1.0f;
	BlendDestinationAverageBrightness = 1.0f;
	bCastVolumetricShadow = true;
	bCastRaytracedShadow = false;
	bAffectReflection = true;
	bAffectGlobalIllumination = true;
	SamplesPerPixel = 4;
}

FSkyLightSceneProxy* USkyLightComponent::CreateSceneProxy() const
{
	if (ProcessedSkyTexture)
	{
		return new FSkyLightSceneProxy(this);
	}

	return NULL;
}

void USkyLightComponent::SetCaptureIsDirty()
{ 
	if (GetVisibleFlag() && bAffectsWorld && !SkipStaticSkyLightCapture(*this))
	{
		FScopeLock Lock(&SkyCapturesToUpdateLock);

		SkyCapturesToUpdate.AddUnique(this);

		// Mark saved values as invalid, in case a sky recapture is requested in a construction script between a save / restore of sky capture state
		bSavedConstructionScriptValuesValid = false;
	}
}

void USkyLightComponent::SanitizeCubemapSize()
{
	const int32 MaxCubemapResolution = GetMaxCubeTextureDimension();
	const int32 MinCubemapResolution = 8;

	CubemapResolution = FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(CubemapResolution)), MinCubemapResolution, MaxCubemapResolution);

#if WITH_EDITOR
	if (FApp::CanEverRender() && !FApp::IsUnattended())
	{
		SIZE_T TexMemRequired = CalcTextureSize(CubemapResolution, CubemapResolution, SKYLIGHT_CUBEMAP_FORMAT, FMath::CeilLogTwo(CubemapResolution) + 1) * CubeFace_MAX;

		FTextureMemoryStats TextureMemStats;
		RHIGetTextureMemoryStats(TextureMemStats);

		if (TextureMemStats.DedicatedVideoMemory > 0 && TexMemRequired > SIZE_T(TextureMemStats.DedicatedVideoMemory / 4))
		{
			FNumberFormattingOptions FmtOpts = FNumberFormattingOptions()
				.SetUseGrouping(false)
				.SetMaximumFractionalDigits(2)
				.SetMinimumFractionalDigits(0)
				.SetRoundingMode(HalfFromZero);

			EAppReturnType::Type Response = FPlatformMisc::MessageBoxExt(
				EAppMsgType::YesNo,
				*FText::Format(
					LOCTEXT("MemAllocWarning_Message_SkylightCubemap", "A resolution of {0} will require {1} of video memory. Are you sure?"),
					FText::AsNumber(CubemapResolution, &FmtOpts),
					FText::AsMemory(TexMemRequired, &FmtOpts)
				).ToString(),
				*LOCTEXT("MemAllocWarning_Title_SkylightCubemap", "Memory Allocation Warning").ToString()
			);

			if (Response == EAppReturnType::No)
			{
				CubemapResolution = PreEditCubemapResolution;
			}
		}

		PreEditCubemapResolution = CubemapResolution;
	}
#endif // WITH_EDITOR
}

void USkyLightComponent::SetBlendDestinationCaptureIsDirty()
{ 
	if (GetVisibleFlag() && bAffectsWorld && BlendDestinationCubemap)
	{
		SkyCapturesToUpdateBlendDestinations.AddUnique(this); 

		// Mark saved values as invalid, in case a sky recapture is requested in a construction script between a save / restore of sky capture state
		bSavedConstructionScriptValuesValid = false;
	}
}

TArray<USkyLightComponent*> USkyLightComponent::SkyCapturesToUpdate;
TArray<USkyLightComponent*> USkyLightComponent::SkyCapturesToUpdateBlendDestinations;
FCriticalSection USkyLightComponent::SkyCapturesToUpdateLock;

void USkyLightComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	bool bHidden = false;
#if WITH_EDITORONLY_DATA
	bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif // WITH_EDITORONLY_DATA

	if(!ShouldComponentAddToScene())
	{
		bHidden = true;
	}

	const bool bIsValid = SourceType != SLS_SpecifiedCubemap || Cubemap != NULL;

	if (bAffectsWorld && GetVisibleFlag() && !bHidden && bIsValid)
	{
		// Create the light's scene proxy.
		SceneProxy = CreateSceneProxy();

		if (SceneProxy)
		{
			// Add the light to the scene.
			GetWorld()->Scene->SetSkyLight(SceneProxy);
		}
	}
}

void USkyLightComponent::PostInitProperties()
{
	// Skip default object or object belonging to a default object (eg default ASkyLight's component)
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Enqueue an update by default, so that newly placed components will get an update
		// PostLoad will undo this for components loaded from disk
		FScopeLock Lock(&SkyCapturesToUpdateLock);
		SkyCapturesToUpdate.AddUnique(this);
	}

	Super::PostInitProperties();
}

void USkyLightComponent::PostLoad()
{
	Super::PostLoad();

	SanitizeCubemapSize();

	// All components are queued for update on creation by default. But we do not want this top happen in some cases.
	if (!GetVisibleFlag() || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || SkipStaticSkyLightCapture(*this))
	{
		FScopeLock Lock(&SkyCapturesToUpdateLock);
		SkyCapturesToUpdate.Remove(this);
	}
}

/** 
 * Fast path for updating light properties that doesn't require a re-register,
 * Which would otherwise cause the scene's static draw lists to be recreated.
 */
void USkyLightComponent::UpdateLimitedRenderingStateFast()
{
	if (SceneProxy)
	{
		FSkyLightSceneProxy* LightSceneProxy = SceneProxy;
		FLinearColor InLightColor = FLinearColor(LightColor) * Intensity;
		float InIndirectLightingIntensity = IndirectLightingIntensity;
		float InVolumetricScatteringIntensity = VolumetricScatteringIntensity;
		ENQUEUE_RENDER_COMMAND(FFastUpdateSkyLightCommand)(
			[LightSceneProxy, InLightColor, InIndirectLightingIntensity, InVolumetricScatteringIntensity](FRHICommandList& RHICmdList)
			{
				LightSceneProxy->SetLightColor(InLightColor);
				LightSceneProxy->IndirectLightingIntensity = InIndirectLightingIntensity;
				LightSceneProxy->VolumetricScatteringIntensity = InVolumetricScatteringIntensity;
			});
	}
}

void USkyLightComponent::UpdateOcclusionRenderingStateFast()
{
	if (SceneProxy && IsOcclusionSupported())
	{
		FSkyLightSceneProxy* InLightSceneProxy = SceneProxy;
		float InContrast = Contrast;
		float InOcclusionExponent = OcclusionExponent;
		float InMinOcclusion = MinOcclusion;
		FColor InOcclusionTint = OcclusionTint;
		ENQUEUE_RENDER_COMMAND(FFastUpdateSkyLightOcclusionCommand)(
			[InLightSceneProxy, InContrast, InOcclusionExponent, InMinOcclusion, InOcclusionTint](FRHICommandList& RHICmdList)
			{
				InLightSceneProxy->Contrast = InContrast;
				InLightSceneProxy->OcclusionExponent = InOcclusionExponent;
				InLightSceneProxy->MinOcclusion = InMinOcclusion;
				InLightSceneProxy->OcclusionTint = InOcclusionTint;
			});
	}

}

/** 
* This is called when property is modified by InterpPropertyTracks
*
* @param PropertyThatChanged	Property that changed
*/
void USkyLightComponent::PostInterpChange(FProperty* PropertyThatChanged)
{
	static FName LightColorName(TEXT("LightColor"));
	static FName IntensityName(TEXT("Intensity"));
	static FName IndirectLightingIntensityName(TEXT("IndirectLightingIntensity"));
	static FName VolumetricScatteringIntensityName(TEXT("VolumetricScatteringIntensity"));

	FName PropertyName = PropertyThatChanged->GetFName();
	if (PropertyName == LightColorName
		|| PropertyName == IntensityName
		|| PropertyName == IndirectLightingIntensityName
		|| PropertyName == VolumetricScatteringIntensityName)
	{
		UpdateLimitedRenderingStateFast();
	}
	else
	{
		Super::PostInterpChange(PropertyThatChanged);
	}
}

void USkyLightComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SceneProxy)
	{
		GetWorld()->Scene->DisableSkyLight(SceneProxy);

		FSkyLightSceneProxy* LightSceneProxy = SceneProxy;
		ENQUEUE_RENDER_COMMAND(FDestroySkyLightCommand)(
			[LightSceneProxy](FRHICommandList& RHICmdList)
			{
				delete LightSceneProxy;
			});

		SceneProxy = nullptr;
	}
}

void USkyLightComponent::UpdateImportanceSamplingData()
{
	check(IsInGameThread());

#if RHI_RAYTRACING
	if (IsRayTracingEnabled() && ProcessedSkyTexture)
	{
		if (!ImportanceSamplingData.IsValid())
		{
			ImportanceSamplingData = new FSkyLightImportanceSamplingData();
			BeginInitResource(ImportanceSamplingData);
			MarkRenderStateDirty();
		}

		ENQUEUE_RENDER_COMMAND(UpdateImportanceSamplingDataCmd)(
			[this](FRHICommandListImmediate& RHICmdList)
		{
			ImportanceSamplingData->BuildCDFs(ProcessedSkyTexture);
		});
	}
#endif
}

#if WITH_EDITOR
void USkyLightComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	PreEditCubemapResolution = CubemapResolution;
}

void USkyLightComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USkyLightComponent, CubemapResolution))
	{
		// Simply rounds the cube map size to nearest power of two. Occasionally checks for out of video mem.
		SanitizeCubemapSize();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SetCaptureIsDirty();
}

bool USkyLightComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (FCString::Strcmp(*PropertyName, TEXT("Cubemap")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("SourceCubemapAngle")) == 0)
		{
			return SourceType == SLS_SpecifiedCubemap;
		}

		if (FCString::Strcmp(*PropertyName, TEXT("LowerHemisphereColor")) == 0)
		{
			return bLowerHemisphereIsBlack;
		}

		if (FCString::Strcmp(*PropertyName, TEXT("Contrast")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("OcclusionMaxDistance")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("MinOcclusion")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("OcclusionTint")) == 0)
		{
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
			return Mobility == EComponentMobility::Movable && CastShadows && CVar->GetValueOnGameThread() != 0;
		}
	}

	return Super::CanEditChange(InProperty);
}

void USkyLightComponent::CheckForErrors()
{
	AActor* Owner = GetOwner();

	if (Owner && GetVisibleFlag() && bAffectsWorld)
	{
		UWorld* ThisWorld = Owner->GetWorld();
		bool bMultipleFound = false;

		if (ThisWorld)
		{
			for (TObjectIterator<USkyLightComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				USkyLightComponent* Component = *ComponentIt;

				if (Component != this 
					&& !Component->IsPendingKill()
					&& Component->GetVisibleFlag()
					&& Component->bAffectsWorld
					&& Component->GetOwner() 
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bMultipleFound = true;
					break;
				}
			}
		}

		if (bMultipleFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT( "MapCheck_Message_MultipleSkyLights", "Multiple sky lights are active, only one can be enabled per world." )))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyLights));
		}
	}
}

#endif // WITH_EDITOR

void USkyLightComponent::BeginDestroy()
{
	// Deregister the component from the update queue
	{
		FScopeLock Lock(&SkyCapturesToUpdateLock); 
		SkyCapturesToUpdate.Remove(this);
	}
	
	SkyCapturesToUpdateBlendDestinations.Remove(this);

	// Release reference
	ProcessedSkyTexture = NULL;

#if RHI_RAYTRACING
	ImportanceSamplingData.SafeRelease();
#endif

	// Begin a fence to track the progress of the above BeginReleaseResource being completed on the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool USkyLightComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

TStructOnScope<FActorComponentInstanceData> USkyLightComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> InstanceData = MakeStructOnScope<FActorComponentInstanceData, FPrecomputedSkyLightInstanceData>(this);
	FPrecomputedSkyLightInstanceData* SkyLightInstanceData = InstanceData.Cast<FPrecomputedSkyLightInstanceData>();
	SkyLightInstanceData->LightGuid = LightGuid;
	SkyLightInstanceData->ProcessedSkyTexture = ProcessedSkyTexture;
#if RHI_RAYTRACING
	SkyLightInstanceData->ImportanceSamplingData = ImportanceSamplingData;
#endif

	// Block until the rendering thread has completed its writes from a previous capture
	IrradianceMapFence.Wait();
	SkyLightInstanceData->IrradianceEnvironmentMap = IrradianceEnvironmentMap;
	SkyLightInstanceData->AverageBrightness = AverageBrightness;
	// RHI_RAYTRACING #SkyLightIS @todo:
	return InstanceData;
}

void USkyLightComponent::ApplyComponentInstanceData(FPrecomputedSkyLightInstanceData* LightMapData)
{
	check(LightMapData);

	LightGuid = (HasStaticShadowing() ? LightMapData->LightGuid : FGuid());
	ProcessedSkyTexture = LightMapData->ProcessedSkyTexture;
#if RHI_RAYTRACING
	ImportanceSamplingData = LightMapData->ImportanceSamplingData;
#endif
	IrradianceEnvironmentMap = LightMapData->IrradianceEnvironmentMap;
	AverageBrightness = LightMapData->AverageBrightness;

	if (ProcessedSkyTexture && bSavedConstructionScriptValuesValid)
	{
		// We have valid capture state, remove the queued update
		FScopeLock Lock(&SkyCapturesToUpdateLock);
		SkyCapturesToUpdate.Remove(this);
	}

	MarkRenderStateDirty();
}

void USkyLightComponent::UpdateSkyCaptureContentsArray(UWorld* WorldToUpdate, TArray<USkyLightComponent*>& ComponentArray, bool bOperateOnBlendSource)
{
	const bool bIsCompilingShaders = GShaderCompilingManager != NULL && GShaderCompilingManager->IsCompiling();

	// Iterate backwards so we can remove elements without changing the index
	for (int32 CaptureIndex = ComponentArray.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
	{
		USkyLightComponent* CaptureComponent = ComponentArray[CaptureIndex];
		AActor* Owner = CaptureComponent->GetOwner();

		if (((!Owner || !Owner->GetLevel() || Owner->GetLevel()->bIsVisible) && CaptureComponent->GetWorld() == WorldToUpdate)
			// Only process sky capture requests once async shader compiling completes, otherwise we will capture the scene with temporary shaders
			&& (!bIsCompilingShaders || CaptureComponent->SourceType == SLS_SpecifiedCubemap))
		{
			// Only capture valid sky light components
			if (CaptureComponent->SourceType != SLS_SpecifiedCubemap || CaptureComponent->Cubemap)
			{
				if (bOperateOnBlendSource)
				{
					ensure(!CaptureComponent->ProcessedSkyTexture || CaptureComponent->ProcessedSkyTexture->GetSizeX() == CaptureComponent->ProcessedSkyTexture->GetSizeY());

					// Allocate the needed texture on first capture
					if (!CaptureComponent->ProcessedSkyTexture || CaptureComponent->ProcessedSkyTexture->GetSizeX() != CaptureComponent->CubemapResolution)
					{
						CaptureComponent->ProcessedSkyTexture = new FSkyTextureCubeResource();
						CaptureComponent->ProcessedSkyTexture->SetupParameters(CaptureComponent->CubemapResolution, FMath::CeilLogTwo(CaptureComponent->CubemapResolution) + 1, SKYLIGHT_CUBEMAP_FORMAT);
						BeginInitResource(CaptureComponent->ProcessedSkyTexture);
						CaptureComponent->MarkRenderStateDirty();
					}

					WorldToUpdate->Scene->UpdateSkyCaptureContents(CaptureComponent, CaptureComponent->bCaptureEmissiveOnly, CaptureComponent->Cubemap, CaptureComponent->ProcessedSkyTexture, CaptureComponent->AverageBrightness, CaptureComponent->IrradianceEnvironmentMap, NULL);
					CaptureComponent->UpdateImportanceSamplingData();
				}
				else
				{
					ensure(!CaptureComponent->BlendDestinationProcessedSkyTexture || CaptureComponent->BlendDestinationProcessedSkyTexture->GetSizeX() == CaptureComponent->BlendDestinationProcessedSkyTexture->GetSizeY());

					// Allocate the needed texture on first capture
					if (!CaptureComponent->BlendDestinationProcessedSkyTexture || CaptureComponent->BlendDestinationProcessedSkyTexture->GetSizeX() != CaptureComponent->CubemapResolution)
					{
						CaptureComponent->BlendDestinationProcessedSkyTexture = new FSkyTextureCubeResource();
						CaptureComponent->BlendDestinationProcessedSkyTexture->SetupParameters(CaptureComponent->CubemapResolution, FMath::CeilLogTwo(CaptureComponent->CubemapResolution) + 1, SKYLIGHT_CUBEMAP_FORMAT);
						BeginInitResource(CaptureComponent->BlendDestinationProcessedSkyTexture);
						CaptureComponent->MarkRenderStateDirty(); 
					}

					WorldToUpdate->Scene->UpdateSkyCaptureContents(CaptureComponent, CaptureComponent->bCaptureEmissiveOnly, CaptureComponent->BlendDestinationCubemap, CaptureComponent->BlendDestinationProcessedSkyTexture, CaptureComponent->BlendDestinationAverageBrightness, CaptureComponent->BlendDestinationIrradianceEnvironmentMap, NULL);
					CaptureComponent->UpdateImportanceSamplingData();
				}

				CaptureComponent->IrradianceMapFence.BeginFence();
				CaptureComponent->bHasEverCaptured = true;
				CaptureComponent->MarkRenderStateDirty();
			}

			// Only remove queued update requests if we processed it for the right world
			ComponentArray.RemoveAt(CaptureIndex);
		}
	}
}

void USkyLightComponent::UpdateSkyCaptureContents(UWorld* WorldToUpdate)
{
	if (WorldToUpdate->Scene)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_SkylightCaptures);

		if (GUpdateSkylightsEveryFrame)
		{
			for (TObjectIterator<USkyLightComponent> It; It; ++It)
			{
				USkyLightComponent* SkylightComponent = *It;
				if (WorldToUpdate->ContainsActor(SkylightComponent->GetOwner()) && !SkylightComponent->IsPendingKill())
				{			
					SkylightComponent->SetCaptureIsDirty();			
				}
			}
		}

		if (SkyCapturesToUpdate.Num() > 0)
		{
			FScopeLock Lock(&SkyCapturesToUpdateLock);
			UpdateSkyCaptureContentsArray(WorldToUpdate, SkyCapturesToUpdate, true);
		}
		
		if (SkyCapturesToUpdateBlendDestinations.Num() > 0)
		{
			UpdateSkyCaptureContentsArray(WorldToUpdate, SkyCapturesToUpdateBlendDestinations, false);
		}
	}
}

void USkyLightComponent::CaptureEmissiveRadianceEnvironmentCubeMap(FSHVectorRGB3& OutIrradianceMap, TArray<FFloat16Color>& OutRadianceMap) const
{
	OutIrradianceMap = FSHVectorRGB3();
	if (GetScene() && (SourceType != SLS_SpecifiedCubemap || Cubemap))
	{
		float UnusedAverageBrightness = 1.0f;
		// Capture emissive scene lighting only for the lighting build
		// This is necessary to avoid a feedback loop with the last lighting build results
		GetScene()->UpdateSkyCaptureContents(this, true, Cubemap, NULL, UnusedAverageBrightness, OutIrradianceMap, &OutRadianceMap);
		// Wait until writes to OutIrradianceMap have completed
		FlushRenderingCommands();
	}
}

/** Set brightness of the light */
void USkyLightComponent::SetIntensity(float NewIntensity)
{
	// Can't set brightness on a static light
	if (AreDynamicDataChangesAllowed()
		&& Intensity != NewIntensity)
	{
		Intensity = NewIntensity;
		UpdateLimitedRenderingStateFast();
	}
}

void USkyLightComponent::SetIndirectLightingIntensity(float NewIntensity)
{
	// Can't set brightness on a static light
	if (AreDynamicDataChangesAllowed()
		&& IndirectLightingIntensity != NewIntensity)
	{
		IndirectLightingIntensity = NewIntensity;
		UpdateLimitedRenderingStateFast();
	}
}

void USkyLightComponent::SetVolumetricScatteringIntensity(float NewIntensity)
{
	// Can't set brightness on a static light
	if (AreDynamicDataChangesAllowed()
		&& VolumetricScatteringIntensity != NewIntensity)
	{
		VolumetricScatteringIntensity = NewIntensity;
		UpdateLimitedRenderingStateFast();
	}
}

/** Set color of the light */
void USkyLightComponent::SetLightColor(FLinearColor NewLightColor)
{
	FColor NewColor(NewLightColor.ToFColor(true));

	// Can't set color on a static light
	if (AreDynamicDataChangesAllowed()
		&& LightColor != NewColor)
	{
		LightColor = NewColor;
		UpdateLimitedRenderingStateFast();
	}
}

void USkyLightComponent::SetCubemap(UTextureCube* NewCubemap)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& Cubemap != NewCubemap)
	{
		Cubemap = NewCubemap;
		MarkRenderStateDirty();
		// Note: this will cause the cubemap to be reprocessed including readback from the GPU
		SetCaptureIsDirty();
	}
}

void USkyLightComponent::SetCubemapBlend(UTextureCube* SourceCubemap, UTextureCube* DestinationCubemap, float InBlendFraction)
{
	if (AreDynamicDataChangesAllowed()
		&& (Cubemap != SourceCubemap || BlendDestinationCubemap != DestinationCubemap || BlendFraction != InBlendFraction)
		&& SourceType == SLS_SpecifiedCubemap)
	{
		if (Cubemap != SourceCubemap)
		{
			Cubemap = SourceCubemap;
			SetCaptureIsDirty();
		}

		if (BlendDestinationCubemap != DestinationCubemap)
		{
			BlendDestinationCubemap = DestinationCubemap;
			SetBlendDestinationCaptureIsDirty();
		}

		if (BlendFraction != InBlendFraction)
		{
			BlendFraction = InBlendFraction;

			if (SceneProxy)
			{
				const FSHVectorRGB3* InIrradianceEnvironmentMap = &IrradianceEnvironmentMap;
				const FSHVectorRGB3* InBlendDestinationIrradianceEnvironmentMap = &BlendDestinationIrradianceEnvironmentMap;
				const float* InAverageBrightness = &AverageBrightness;
				const float* InBlendDestinationAverageBrightness = &BlendDestinationAverageBrightness;
				FSkyLightSceneProxy* LightSceneProxy = SceneProxy;
				ENQUEUE_RENDER_COMMAND(FUpdateSkyProxy)(
					[InIrradianceEnvironmentMap, InBlendDestinationIrradianceEnvironmentMap, InAverageBrightness, InBlendDestinationAverageBrightness, InBlendFraction, LightSceneProxy](FRHICommandList& RHICmdList)
					{
						// Only access the irradiance maps on the RT, even though they belong to the USkyLightComponent, 
						// Because FScene::UpdateSkyCaptureContents does not block the RT so the writes could still be in flight
						LightSceneProxy->Initialize(InBlendFraction, InIrradianceEnvironmentMap, InBlendDestinationIrradianceEnvironmentMap, InAverageBrightness, InBlendDestinationAverageBrightness);
					});
			}
		}
	}
}

void USkyLightComponent::SetLowerHemisphereColor(const FLinearColor& InLowerHemisphereColor)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& LowerHemisphereColor != InLowerHemisphereColor)
	{
		LowerHemisphereColor = InLowerHemisphereColor;
		MarkRenderStateDirty();
	}
}

void USkyLightComponent::SetOcclusionTint(const FColor& InTint)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& OcclusionTint != InTint)
	{
		OcclusionTint = InTint;
		UpdateOcclusionRenderingStateFast();
	}
}

void USkyLightComponent::SetOcclusionContrast(float InOcclusionContrast)
{
	if (AreDynamicDataChangesAllowed()
		&& Contrast != InOcclusionContrast)
	{
		Contrast = InOcclusionContrast;
		UpdateOcclusionRenderingStateFast();
	}
}

void USkyLightComponent::SetOcclusionExponent(float InOcclusionExponent)
{
	if (AreDynamicDataChangesAllowed()
		&& OcclusionExponent != InOcclusionExponent)
	{
		OcclusionExponent = InOcclusionExponent;
		UpdateOcclusionRenderingStateFast();
	}
}

void USkyLightComponent::SetMinOcclusion(float InMinOcclusion)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& MinOcclusion != InMinOcclusion)
	{
		MinOcclusion = InMinOcclusion;
		UpdateOcclusionRenderingStateFast();
	}
}

bool USkyLightComponent::IsOcclusionSupported() const
{
	FSceneInterface* LocalScene = GetScene();
	if (LocalScene && LocalScene->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1)
	{
		// Sky occlusion is not supported on mobile
		return false;
	}
	return true;
}

void USkyLightComponent::OnVisibilityChanged()
{
	Super::OnVisibilityChanged();

	if (GetVisibleFlag() && !bHasEverCaptured)
	{
		// Capture if we are being enabled for the first time
		SetCaptureIsDirty();
		SetBlendDestinationCaptureIsDirty();
	}
}

void USkyLightComponent::RecaptureSky()
{
	SetCaptureIsDirty();
}

void USkyLightComponent::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Super::Serialize(Ar);

	// if version is between VER_UE4_SKYLIGHT_MOBILE_IRRADIANCE_MAP and FReleaseObjectVersion::SkyLightRemoveMobileIrradianceMap then handle aborted attempt to serialize irradiance data on mobile.
	if (Ar.UE4Ver() >= VER_UE4_SKYLIGHT_MOBILE_IRRADIANCE_MAP && !(Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::SkyLightRemoveMobileIrradianceMap))
	{
		FSHVectorRGB3 DummyIrradianceEnvironmentMap;
		Ar << DummyIrradianceEnvironmentMap;
	}
}



ASkyLight::ASkyLight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LightComponent = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLightComponent0"));
	RootComponent = LightComponent;

#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SkyLightTextureObject;
		FName ID_Sky;
		FText NAME_Sky;

		FConstructorStatics()
				: SkyLightTextureObject(TEXT("/Engine/EditorResources/LightIcons/SkyLight"))
				, ID_Sky(TEXT("Sky"))
			, NAME_Sky(NSLOCTEXT( "SpriteCategory", "Sky", "Sky" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.SkyLightTextureObject.Get();
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_Sky;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_Sky;
			GetSpriteComponent()->SetupAttachment(LightComponent);
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void ASkyLight::GetLifetimeReplicatedProps( TArray< FLifetimeProperty > & OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME( ASkyLight, bEnabled );
}

void ASkyLight::OnRep_bEnabled()
{
	LightComponent->SetVisibility(bEnabled);
}


#undef LOCTEXT_NAMESPACE
