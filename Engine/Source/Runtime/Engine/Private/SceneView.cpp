// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneView.cpp: SceneView implementation.
=============================================================================*/

#include "SceneView.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "EngineGlobals.h"
#include "PrimitiveUniformShaderParameters.h"
#include "Engine/Engine.h"
#include "Widgets/SWindow.h"
#include "SceneManagement.h"
#include "EngineModule.h"
#include "BufferVisualizationData.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Engine/TextureCube.h"
#include "StereoRendering.h"
#include "StereoRenderTargetManager.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/RendererSettings.h"
#include "LightPropagationVolumeSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HighResScreenshot.h"
#include "Slate/SceneViewport.h"
#include "RenderUtils.h"

DEFINE_LOG_CATEGORY(LogBufferVisualization);
DEFINE_LOG_CATEGORY(LogMultiView);

DECLARE_CYCLE_STAT(TEXT("StartFinalPostprocessSettings"), STAT_StartFinalPostprocessSettings, STATGROUP_Engine);
DECLARE_CYCLE_STAT(TEXT("OverridePostProcessSettings"), STAT_OverridePostProcessSettings, STATGROUP_Engine);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPrimitiveUniformShaderParameters, "Primitive");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FViewUniformShaderParameters, "View");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedViewUniformShaderParameters, "InstancedView");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileDirectionalLightShaderParameters, "MobileDirectionalLight");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileMovablePointLightUniformShaderParameters, "MobileMovablePointLight0");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_ALIAS_STRUCT(FMobileMovablePointLightUniformShaderParameters, MobileMovablePointLight1);
IMPLEMENT_GLOBAL_SHADER_PARAMETER_ALIAS_STRUCT(FMobileMovablePointLightUniformShaderParameters, MobileMovablePointLight2);
IMPLEMENT_GLOBAL_SHADER_PARAMETER_ALIAS_STRUCT(FMobileMovablePointLightUniformShaderParameters, MobileMovablePointLight3);


static TAutoConsoleVariable<float> CVarSSRMaxRoughness(
	TEXT("r.SSR.MaxRoughness"),
	-1.0f,
	TEXT("Allows to override the post process setting ScreenSpaceReflectionMaxRoughness.\n")
	TEXT("It defines until what roughness we fade the screen space reflections, 0.8 works well, smaller can run faster.\n")
	TEXT("(Useful for testing, no scalability or project setting)\n")
	TEXT(" 0..1: use specified max roughness (overrride PostprocessVolume setting)\n")
	TEXT(" -1: no override (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);


static TAutoConsoleVariable<float> CVarGlobalMinRoughnessOverride(
	TEXT("r.MinRoughnessOverride"),
	0.0f,
	TEXT("WARNING: This is an experimental feature that may change at any time.\n")
	TEXT("Sets a global limit for roughness when used in the direct lighting calculations.\n")
	TEXT("This can be used to limit the amount of fireflies caused by low roughness, in particular when AA is not in use.\n")
	TEXT(" 0.0: no change (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

static TAutoConsoleVariable<int32> CVarFreezeMouseCursor(
	TEXT("r.FreezeMouseCursor"),
	0,
	TEXT("Free the mouse cursor position, for passes which use it to display debug information.\n")
	TEXT("0: default\n")
	TEXT("1: freeze mouse cursor position at current location"),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarShadowFreezeCamera(
	TEXT("r.Shadow.FreezeCamera"),
	0,
	TEXT("Debug the shadow methods by allowing to observe the system from outside.\n")
	TEXT("0: default\n")
	TEXT("1: freeze camera at current location"),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarExposureOffset(
	TEXT("r.ExposureOffset"),
	0.0f,
	TEXT("For adjusting the exposure on top of post process settings and eye adaptation. For developers only. 0:default"),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarRenderTimeFrozen(
	TEXT("r.RenderTimeFrozen"),
	0,
	TEXT("Allows to freeze time based effects in order to provide more deterministic render profiling.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on (Note: this also disables occlusion queries)"),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDepthOfFieldDepthBlurAmount(
	TEXT("r.DepthOfField.DepthBlur.Amount"),
	1.0f,
	TEXT("This scale multiplier only affects the CircleDOF DepthBlur feature (value defines in how many km the radius goes to 50%).\n")
	TEXT(" x: Multiply the existing Depth Blur Amount with x\n")
	TEXT("-x: Override the existing Depth Blur Amount with x (in km)\n")
	TEXT(" 1: No adjustments (default)"),
	ECVF_RenderThreadSafe | ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDepthOfFieldDepthBlurScale(
	TEXT("r.DepthOfField.DepthBlur.Scale"),
	1.0f,
	TEXT("This scale multiplier only affects the CircleDOF DepthBlur feature. This is applied after r.DepthOfField.DepthBlur.ResolutionScale.\n")
	TEXT(" 0: Disable Depth Blur\n")
	TEXT(" x: Multiply the existing Depth Blur Radius with x\n")
	TEXT("-x: Override the existing Depth Blur Radius with x\n")
	TEXT(" 1: No adjustments (default)"),
	ECVF_RenderThreadSafe | ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDepthOfFieldDepthBlurResolutionScale(
	TEXT("r.DepthOfField.DepthBlur.ResolutionScale"),
	1.0f,
	TEXT("This scale multiplier only affects the CircleDOF DepthBlur feature. It's a temporary hack.\n")
	TEXT("It lineary scale the DepthBlur by the resolution increase over 1920 (in width), does only affect resolution larger than that.\n")
	TEXT("Actual math: float Factor = max(ViewWidth / 1920 - 1, 0); DepthBlurRadius *= 1 + Factor * (CVar - 1)\n")
	TEXT(" 1: No adjustments (default)\n")
	TEXT(" x: if the resolution is 1920 there is no change, if 2x larger than 1920 it scale the radius by x"),
	ECVF_RenderThreadSafe | ECVF_Cheat);
#endif

static TAutoConsoleVariable<float> CVarSSAOFadeRadiusScale(
	TEXT("r.AmbientOcclusion.FadeRadiusScale"),
	1.0f,
	TEXT("Allows to scale the ambient occlusion fade radius (SSAO).\n")
	TEXT(" 0.01:smallest .. 1.0:normal (default), <1:smaller, >1:larger"),
	ECVF_Cheat | ECVF_RenderThreadSafe);

// Engine default (project settings):

static TAutoConsoleVariable<int32> CVarDefaultBloom(
	TEXT("r.DefaultFeature.Bloom"),
	1,
	TEXT("Engine default (project setting) for Bloom is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, set BloomIntensity to 0\n")
	TEXT(" 1: on (default)"));

static TAutoConsoleVariable<int32> CVarDefaultAmbientOcclusion(
	TEXT("r.DefaultFeature.AmbientOcclusion"),
	1,
	TEXT("Engine default (project setting) for AmbientOcclusion is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, sets AmbientOcclusionIntensity to 0\n")
	TEXT(" 1: on (default)"));

static TAutoConsoleVariable<int32> CVarDefaultAmbientOcclusionStaticFraction(
	TEXT("r.DefaultFeature.AmbientOcclusionStaticFraction"),
	1,
	TEXT("Engine default (project setting) for AmbientOcclusion is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, sets AmbientOcclusionStaticFraction to 0\n")
	TEXT(" 1: on (default, costs extra pass, only useful if there is some baked lighting)"));

static TAutoConsoleVariable<int32> CVarDefaultAutoExposure(
	TEXT("r.DefaultFeature.AutoExposure"),
	1,
	TEXT("Engine default (project setting) for AutoExposure is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, sets AutoExposureMinBrightness and AutoExposureMaxBrightness to 1\n")
	TEXT(" 1: on (default)"));

static TAutoConsoleVariable<int32> CVarDefaultAutoExposureMethod(
	TEXT("r.DefaultFeature.AutoExposure.Method"),
	0,
	TEXT("Engine default (project setting) for AutoExposure Method (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: Histogram based (requires compute shader, default)\n")
	TEXT(" 1: Basic AutoExposure"));

static TAutoConsoleVariable<float> CVarDefaultAutoExposureBias(
	TEXT("r.DefaultFeature.AutoExposure.Bias"),
	1.0f,
	TEXT("Engine default (project setting) for AutoExposure Exposure Bias (postprocess volume/camera/game setting still can override)\n"));

static TAutoConsoleVariable<int32> CVarDefaultAutoExposureExtendDefaultLuminanceRange(
	TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"),
	0,
	TEXT("Whether the default values for AutoExposure should support an extended range of scene luminance.\n")
	TEXT("This also change the PostProcessSettings.Exposure.MinBrightness, MaxBrightness, HistogramLogMin and HisogramLogMax\n")
	TEXT("to be expressed in EV100 values instead of in Luminance and Log2 Luminance.\n")
	TEXT(" 0: Legacy range (default)\n")
	TEXT(" 1: Extended range"));

static TAutoConsoleVariable<int32> CVarDefaultMotionBlur(
	TEXT("r.DefaultFeature.MotionBlur"),
	1,
	TEXT("Engine default (project setting) for MotionBlur is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, sets MotionBlurAmount to 0\n")
	TEXT(" 1: on (default)"));

// off by default for better performance and less distractions
static TAutoConsoleVariable<int32> CVarDefaultLensFlare(
	TEXT("r.DefaultFeature.LensFlare"),
	0,
	TEXT("Engine default (project setting) for LensFlare is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off, sets LensFlareIntensity to 0\n")
	TEXT(" 1: on (default)"));

// see EAntiAliasingMethod
static TAutoConsoleVariable<int32> CVarDefaultAntiAliasing(
	TEXT("r.DefaultFeature.AntiAliasing"),
	2,
	TEXT("Engine default (project setting) for AntiAliasingMethod is (postprocess volume/camera/game setting still can override)\n")
	TEXT(" 0: off (no anti-aliasing)\n")
	TEXT(" 1: FXAA (faster than TemporalAA but much more shimmering for non static cases)\n")
	TEXT(" 2: TemporalAA (default)\n")
	TEXT(" 3: MSAA (Forward shading only)"),
	ECVF_RenderThreadSafe);

// see ELightUnits
static TAutoConsoleVariable<int32> CVarDefaultPointLightUnits(
	TEXT("r.DefaultFeature.LightUnits"),
	1,
	TEXT("Default units to use for point, spot and rect lights\n")
	TEXT(" 0: unitless \n")
	TEXT(" 1: candelas (default)\n")
	TEXT(" 2: lumens"));

static TAutoConsoleVariable<float> CVarMotionBlurScale(
	TEXT("r.MotionBlur.Scale"),
	1.0f,
	TEXT("Allows to scale the postprocess intensity/amount setting in the postprocess.\n")
	TEXT("1: don't do any scaling (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarMotionBlurAmount(
	TEXT("r.MotionBlur.Amount"),
	-1.0f,
	TEXT("Allows to override the postprocess setting (scale of motion blur)\n")
	TEXT("-1: override (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarMotionBlurMax(
	TEXT("r.MotionBlur.Max"),
	-1.0f,
	TEXT("Allows to override the postprocess setting (max length of motion blur, in percent of the screen width)\n")
	TEXT("-1: override (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMotionBlurTargetFPS(
	TEXT("r.MotionBlur.TargetFPS"),
	-1,
	TEXT("Allows to override the postprocess setting (target FPS for motion blur velocity length scaling).\n")
	TEXT("-1: override (default)")
	TEXT(" 0: target current frame rate with moving average\n")
	TEXT("[1,120]: target FPS for motion blur velocity scaling"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSceneColorFringeMax(
	TEXT("r.SceneColorFringe.Max"),
	-1.0f,
	TEXT("Allows to clamp the postprocess setting (in percent, Scene chromatic aberration / color fringe to simulate an artifact that happens in real-world lens, mostly visible in the image corners)\n")
	TEXT("-1: don't clamp (default)\n")
	TEXT("-2: to test extreme fringe"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTonemapperQuality(
	TEXT("r.Tonemapper.Quality"),
	5,
	TEXT("Defines the Tonemapper Quality in the range 0..5\n")
	TEXT("Depending on the used settings we might pick a faster shader permutation\n")
	TEXT(" 0: basic tonemapper only, lowest quality\n")
	TEXT(" 1: + FilmContrast\n")
	TEXT(" 2: + Vignette\n")
	TEXT(" 3: + FilmShadowTintAmount\n")
	TEXT(" 4: + Grain\n")
	TEXT(" 5: + GrainJitter = full quality (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTessellationAdaptivePixelsPerTriangle(
	TEXT("r.TessellationAdaptivePixelsPerTriangle"),
	48.0f,
	TEXT("Global tessellation factor multiplier"),
	ECVF_RenderThreadSafe);

// should be changed to BaseColor and Metallic, since some time now UE4 is not using DiffuseColor and SpecularColor any more
static TAutoConsoleVariable<float> CVarDiffuseColorMin(
	TEXT("r.DiffuseColor.Min"),
	0.0f,
	TEXT("Allows quick material test by remapping the diffuse color at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarDiffuseColorMax(
	TEXT("r.DiffuseColor.Max"),
	1.0f,
	TEXT("Allows quick material test by remapping the diffuse color at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarRoughnessMin(
	TEXT("r.Roughness.Min"),
	0.0f,
	TEXT("Allows quick material test by remapping the roughness at 0 to a new value (0..1), Only for non shipping built!\n")
	TEXT("0: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarRoughnessMax(
	TEXT("r.Roughness.Max"),
	1.0f,
	TEXT("Allows quick material test by remapping the roughness at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarAllowTranslucencyAfterDOF(
	TEXT("r.SeparateTranslucency"),
	1,
	TEXT("Allows to disable the separate translucency feature (all translucency is rendered in separate RT and composited\n")
	TEXT("after DOF, if not specified otherwise in the material).\n")
	TEXT(" 0: off (translucency is affected by depth of field)\n")
	TEXT(" 1: on costs GPU performance and memory but keeps translucency unaffected by Depth of Field. (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarEnableTemporalUpsample(
	TEXT("r.TemporalAA.Upsampling"),
	0,
	TEXT("Whether to do primary screen percentage with temporal AA or not.\n")
	TEXT(" 0: use spatial upscale pass independently of TAA (default);\n")
	TEXT(" 1: TemporalAA performs spatial and temporal upscale as screen percentage method."),
	ECVF_Default);

// Conversion factor used when "r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange" 
FORCEINLINE float LuminanceToEV100(float Luminance)
{
	return FMath::Log2(Luminance / 1.2f);
}

/** Global vertex color view mode setting when SHOW_VertexColors show flag is set */
EVertexColorViewMode::Type GVertexColorViewMode = EVertexColorViewMode::Color;

/** Global primitive uniform buffer resource containing identity transformations. */
ENGINE_API TGlobalResource<FIdentityPrimitiveUniformBuffer> GIdentityPrimitiveUniformBuffer;

/** Global primitive uniform buffer resource containing identity transformations. */
ENGINE_API TGlobalResource<FDummyMovablePointLightUniformBuffer> GDummyMovablePointLightUniformBuffer;

FSceneViewStateReference::~FSceneViewStateReference()
{
	Destroy();
}

void FSceneViewStateReference::Allocate()
{
	check(!Reference);
	Reference = GetRendererModule().AllocateViewState();
	GlobalListLink = TLinkedList<FSceneViewStateReference*>(this);
	GlobalListLink.LinkHead(GetSceneViewStateList());
}

void FSceneViewStateReference::Destroy()
{
	GlobalListLink.Unlink();

	if (Reference)
	{
		Reference->Destroy();
		Reference = NULL;
	}
}

void FSceneViewStateReference::DestroyAll()
{
	for(TLinkedList<FSceneViewStateReference*>::TIterator ViewStateIt(FSceneViewStateReference::GetSceneViewStateList());ViewStateIt;ViewStateIt.Next())
	{
		FSceneViewStateReference* ViewStateReference = *ViewStateIt;
		ViewStateReference->Reference->Destroy();
		ViewStateReference->Reference = NULL;
	}
}

void FSceneViewStateReference::AllocateAll()
{
	for(TLinkedList<FSceneViewStateReference*>::TIterator ViewStateIt(FSceneViewStateReference::GetSceneViewStateList());ViewStateIt;ViewStateIt.Next())
	{
		FSceneViewStateReference* ViewStateReference = *ViewStateIt;
		ViewStateReference->Reference = GetRendererModule().AllocateViewState();
	}
}

TLinkedList<FSceneViewStateReference*>*& FSceneViewStateReference::GetSceneViewStateList()
{
	static TLinkedList<FSceneViewStateReference*>* List = NULL;
	return List;
}

/**
 * Utility function to create the inverse depth projection transform to be used
 * by the shader system.
 * @param ProjMatrix - used to extract the scene depth ratios
 * @param InvertZ - projection calc is affected by inverted device Z
 * @return vector containing the ratios needed to convert from device Z to world Z
 */
FVector4 CreateInvDeviceZToWorldZTransform(const FMatrix& ProjMatrix)
{
	// The perspective depth projection comes from the the following projection matrix:
	//
	// | 1  0  0  0 |
	// | 0  1  0  0 |
	// | 0  0  A  1 |
	// | 0  0  B  0 |
	//
	// Z' = (Z * A + B) / Z
	// Z' = A + B / Z
	//
	// So to get Z from Z' is just:
	// Z = B / (Z' - A)
	//
	// Note a reversed Z projection matrix will have A=0.
	//
	// Done in shader as:
	// Z = 1 / (Z' * C1 - C2)   --- Where C1 = 1/B, C2 = A/B
	//

	float DepthMul = ProjMatrix.M[2][2];
	float DepthAdd = ProjMatrix.M[3][2];

	if (DepthAdd == 0.f)
	{
		// Avoid dividing by 0 in this case
		DepthAdd = 0.00000001f;
	}

	// perspective
	// SceneDepth = 1.0f / (DeviceZ / ProjMatrix.M[3][2] - ProjMatrix.M[2][2] / ProjMatrix.M[3][2])

	// ortho
	// SceneDepth = DeviceZ / ProjMatrix.M[2][2] - ProjMatrix.M[3][2] / ProjMatrix.M[2][2];

	// combined equation in shader to handle either
	// SceneDepth = DeviceZ * View.InvDeviceZToWorldZTransform[0] + View.InvDeviceZToWorldZTransform[1] + 1.0f / (DeviceZ * View.InvDeviceZToWorldZTransform[2] - View.InvDeviceZToWorldZTransform[3]);

	// therefore perspective needs
	// View.InvDeviceZToWorldZTransform[0] = 0.0f
	// View.InvDeviceZToWorldZTransform[1] = 0.0f
	// View.InvDeviceZToWorldZTransform[2] = 1.0f / ProjMatrix.M[3][2]
	// View.InvDeviceZToWorldZTransform[3] = ProjMatrix.M[2][2] / ProjMatrix.M[3][2]

	// and ortho needs
	// View.InvDeviceZToWorldZTransform[0] = 1.0f / ProjMatrix.M[2][2]
	// View.InvDeviceZToWorldZTransform[1] = -ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f
	// View.InvDeviceZToWorldZTransform[2] = 0.0f
	// View.InvDeviceZToWorldZTransform[3] = 1.0f

	bool bIsPerspectiveProjection = ProjMatrix.M[3][3] < 1.0f;

	if (bIsPerspectiveProjection)
	{
		float SubtractValue = DepthMul / DepthAdd;

		// Subtract a tiny number to avoid divide by 0 errors in the shader when a very far distance is decided from the depth buffer.
		// This fixes fog not being applied to the black background in the editor.
		SubtractValue -= 0.00000001f;

		return FVector4(
			0.0f,
			0.0f,
			1.0f / DepthAdd,
			SubtractValue
			);
	}
	else
	{
		return FVector4(
			1.0f / ProjMatrix.M[2][2],
			-ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f,
			0.0f,
			1.0f
			);
	}
}


void FViewMatrices::Init(const FMinimalInitializer& Initializer)
{
	//check(Initializer.ViewRotationMatrix.GetOrigin().IsNearlyZero());
	check(FVector::Distance(Initializer.ViewRotationMatrix.GetScaleVector(), FVector::OneVector) < KINDA_SMALL_NUMBER);

	FVector LocalViewOrigin = Initializer.ViewOrigin;
	FMatrix ViewRotationMatrix = Initializer.ViewRotationMatrix;
	if (!ViewRotationMatrix.GetOrigin().IsNearlyZero(0.0f))
	{
		LocalViewOrigin += ViewRotationMatrix.InverseTransformPosition(FVector::ZeroVector);
		ViewRotationMatrix = ViewRotationMatrix.RemoveTranslation();
	}

	ViewMatrix = FTranslationMatrix(-LocalViewOrigin) * ViewRotationMatrix;
	HMDViewMatrixNoRoll = Initializer.ViewRotationMatrix;

	// Adjust the projection matrix for the current RHI.
	ProjectionMatrix = AdjustProjectionMatrixForRHI(Initializer.ProjectionMatrix);
	InvProjectionMatrix = InvertProjectionMatrix(ProjectionMatrix);

	// Compute the view projection matrix and its inverse.
	ViewProjectionMatrix = GetViewMatrix() * GetProjectionMatrix();

	// For precision reasons the view matrix inverse is calculated independently.
	InvViewMatrix = ViewRotationMatrix.GetTransposed() * FTranslationMatrix(LocalViewOrigin);
	InvViewProjectionMatrix = InvProjectionMatrix * InvViewMatrix;

	bool bApplyPreViewTranslation = true;
	bool bViewOriginIsFudged = false;

	// Calculate the view origin from the view/projection matrices.
	if (IsPerspectiveProjection())
	{
		this->ViewOrigin = LocalViewOrigin;
	}
#if WITH_EDITOR
	else if (Initializer.bUseFauxOrthoViewPos)
	{
		float DistanceToViewOrigin = WORLD_MAX;
		ViewOrigin = FVector4(InvViewMatrix.TransformVector(FVector(0, 0, -1)).GetSafeNormal() * DistanceToViewOrigin, 1) + LocalViewOrigin;
		bViewOriginIsFudged = true;
	}
#endif
	else
	{
		this->ViewOrigin = FVector4(InvViewMatrix.TransformVector(FVector(0, 0, -1)).GetSafeNormal(), 0);
		// to avoid issues with view dependent effect (e.g. Frensel)
		bApplyPreViewTranslation = false;
	}

	/** The view transform, starting from world-space points translated by -ViewOrigin. */
	FMatrix LocalTranslatedViewMatrix = ViewRotationMatrix;
	FMatrix LocalInvTranslatedViewMatrix = LocalTranslatedViewMatrix.GetTransposed();

	// Translate world-space so its origin is at ViewOrigin for improved precision.
	// Note that this isn't exactly right for orthogonal projections (See the above special case), but we still use ViewOrigin
	// in that case so the same value may be used in shaders for both the world-space translation and the camera's world position.
	if (bApplyPreViewTranslation)
	{
		PreViewTranslation = -FVector(LocalViewOrigin);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		{
			// console variable override
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PreViewTranslation"));
			int32 Value = CVar->GetValueOnAnyThread();

			static FVector PreViewTranslationBackup;

			if (Value)
			{
				PreViewTranslationBackup = PreViewTranslation;
			}
			else
			{
				PreViewTranslation = PreViewTranslationBackup;
			}
		}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	}
	else
	{
		// If not applying PreViewTranslation then we need to use the view matrix directly.
		LocalTranslatedViewMatrix = ViewMatrix;
		LocalInvTranslatedViewMatrix = InvViewMatrix;
	}

	// When the view origin is fudged for faux ortho view position the translations don't cancel out.
	if (bViewOriginIsFudged)
	{
		LocalTranslatedViewMatrix = FTranslationMatrix(-PreViewTranslation)
			* FTranslationMatrix(-LocalViewOrigin) * ViewRotationMatrix;
		LocalInvTranslatedViewMatrix = LocalTranslatedViewMatrix.Inverse();
	}

	// Compute a transform from view origin centered world-space to clip space.
	TranslatedViewMatrix = LocalTranslatedViewMatrix;
	InvTranslatedViewMatrix = LocalInvTranslatedViewMatrix;

	OverriddenTranslatedViewMatrix = FTranslationMatrix(-GetPreViewTranslation()) * GetViewMatrix();
	OverriddenInvTranslatedViewMatrix = GetInvViewMatrix() * FTranslationMatrix(GetPreViewTranslation());

	TranslatedViewProjectionMatrix = LocalTranslatedViewMatrix * ProjectionMatrix;
	InvTranslatedViewProjectionMatrix = InvProjectionMatrix * LocalInvTranslatedViewMatrix;

	// Compute screen scale factors.
	// Stereo renders at half horizontal resolution, but compute shadow resolution based on full resolution.
	const bool bStereo = IStereoRendering::IsStereoEyePass(Initializer.StereoPass);
	const float ScreenXScale = bStereo ? 2.0f : 1.0f;
	ProjectionScale.X = ScreenXScale * FMath::Abs(ProjectionMatrix.M[0][0]);
	ProjectionScale.Y = FMath::Abs(ProjectionMatrix.M[1][1]);
	ScreenScale = FMath::Max(
		Initializer.ConstrainedViewRect.Size().X * 0.5f * ProjectionScale.X,
		Initializer.ConstrainedViewRect.Size().Y * 0.5f * ProjectionScale.Y
	);
}

FViewMatrices::FViewMatrices(const FSceneViewInitOptions& InitOptions) : FViewMatrices()
{
	FMinimalInitializer Initializer;

	Initializer.ViewRotationMatrix   = InitOptions.ViewRotationMatrix;
	Initializer.ProjectionMatrix     = InitOptions.ProjectionMatrix;
	Initializer.ViewOrigin           = InitOptions.ViewOrigin;
	Initializer.ConstrainedViewRect  = InitOptions.GetConstrainedViewRect();
	Initializer.StereoPass           = InitOptions.StereoPass;
#if WITH_EDITOR
	Initializer.bUseFauxOrthoViewPos = InitOptions.bUseFauxOrthoViewPos;
#endif

	Init(Initializer);
}

FViewMatrices::FViewMatrices(const FMinimalInitializer& Initializer) : FViewMatrices()
{
	Init(Initializer);
}


static void SetupViewFrustum(FSceneView& View)
{
	if (View.SceneViewInitOptions.OverrideFarClippingPlaneDistance > 0.0f)
	{
		const FPlane FarPlane(View.ViewMatrices.GetViewOrigin() + View.GetViewDirection() * View.SceneViewInitOptions.OverrideFarClippingPlaneDistance, View.GetViewDirection());
		// Derive the view frustum from the view projection matrix, overriding the far plane
		GetViewFrustumBounds(View.ViewFrustum, View.ViewMatrices.GetViewProjectionMatrix(), FarPlane, true, false);
	}
	else
	{
		// Derive the view frustum from the view projection matrix.
		GetViewFrustumBounds(View.ViewFrustum, View.ViewMatrices.GetViewProjectionMatrix(), false);
	}

	// Derive the view's near clipping distance and plane.
	// The GetFrustumFarPlane() is the near plane because of reverse Z projection.
	static_assert((int32)ERHIZBuffer::IsInverted != 0, "Fix Near Clip distance!");
	View.bHasNearClippingPlane = View.ViewMatrices.GetViewProjectionMatrix().GetFrustumFarPlane(View.NearClippingPlane);
	if (View.ViewMatrices.GetProjectionMatrix().M[2][3] > DELTA)
	{
		// Infinite projection with reversed Z.
		View.NearClippingDistance = View.ViewMatrices.GetProjectionMatrix().M[3][2];
	}
	else
	{
		// Ortho projection with reversed Z.
		View.NearClippingDistance = (1.0f - View.ViewMatrices.GetProjectionMatrix().M[3][2]) / View.ViewMatrices.GetProjectionMatrix().M[2][2];
	}
}

FSceneView::FSceneView(const FSceneViewInitOptions& InitOptions)
	: Family(InitOptions.ViewFamily)
	, State(InitOptions.SceneViewStateInterface)
	, DynamicMeshElementsShadowCullFrustum(nullptr)
	, PreShadowTranslation(FVector::ZeroVector)
	, ViewActor(InitOptions.ViewActor)
	, PlayerIndex(InitOptions.PlayerIndex)
	, Drawer(InitOptions.ViewElementDrawer)
	, UnscaledViewRect(InitOptions.GetConstrainedViewRect())
	, UnconstrainedViewRect(InitOptions.GetViewRect())
	, MaxShadowCascades(10)
	, ViewMatrices(InitOptions)
	, ViewLocation(ForceInitToZero)
	, ViewRotation(ForceInitToZero)
	, BaseHmdOrientation(EForceInit::ForceInit)
	, BaseHmdLocation(ForceInitToZero)
	, WorldToMetersScale(InitOptions.WorldToMetersScale)
	, ShadowViewMatrices(InitOptions)
	, ProjectionMatrixUnadjustedForRHI(InitOptions.ProjectionMatrix)
	, BackgroundColor(InitOptions.BackgroundColor)
	, OverlayColor(InitOptions.OverlayColor)
	, ColorScale(InitOptions.ColorScale)
	, StereoPass(InitOptions.StereoPass)
	, StereoIPD(InitOptions.StereoIPD)
	, bAllowCrossGPUTransfer(true)
	, bOverrideGPUMask(false)
	, GPUMask(FRHIGPUMask::GPU0())
	, bRenderFirstInstanceOnly(false)
	, DiffuseOverrideParameter(FVector4(0,0,0,1))
	, SpecularOverrideParameter(FVector4(0,0,0,1))
	, NormalOverrideParameter(FVector4(0,0,0,1))
	, RoughnessOverrideParameter(FVector2D(0,1))
	, MaterialTextureMipBias(0.f)
	, HiddenPrimitives(InitOptions.HiddenPrimitives)
	, ShowOnlyPrimitives(InitOptions.ShowOnlyPrimitives)
	, OriginOffsetThisFrame(InitOptions.OriginOffsetThisFrame)
	, LODDistanceFactor(InitOptions.LODDistanceFactor)
	, bCameraCut(InitOptions.bInCameraCut)
	, CursorPos(InitOptions.CursorPos)
	, bIsGameView(false)
	, bIsViewInfo(false)
	, bIsSceneCapture(false)
	, bSceneCaptureUsesRayTracing(false)
	, bIsReflectionCapture(false)
	, bIsPlanarReflection(false)
	, bIsVirtualTexture(false)
	, bIsOfflineRender(false)
	, bRenderSceneTwoSided(false)
	, bIsLocked(false)
	, bStaticSceneOnly(false)
	, bIsInstancedStereoEnabled(false)
	, bIsMultiViewEnabled(false)
	, bIsMobileMultiViewEnabled(false)
	, bShouldBindInstancedViewUB(false)
	, UnderwaterDepth(-1.0f)
	, bForceCameraVisibilityReset(false)
	, GlobalClippingPlane(FPlane(0, 0, 0, 0))
	, LensPrincipalPointOffsetScale(0.0f, 0.0f, 1.0f, 1.0f)
#if WITH_EDITOR
	, OverrideLODViewOrigin(InitOptions.OverrideLODViewOrigin)
	, bAllowTranslucentPrimitivesInHitProxy( true )
	, bHasSelectedComponents( false )
#endif
	, AntiAliasingMethod(AAM_None)
	, PrimaryScreenPercentageMethod(EPrimaryScreenPercentageMethod::SpatialUpscale)
	, ForwardLightingResources(nullptr)
	, FeatureLevel(InitOptions.ViewFamily ? InitOptions.ViewFamily->GetFeatureLevel() : GMaxRHIFeatureLevel)
#if RHI_RAYTRACING
	, IESLightProfileResource(nullptr)
#endif
{
	check(UnscaledViewRect.Min.X >= 0);
	check(UnscaledViewRect.Min.Y >= 0);
	check(UnscaledViewRect.Width() > 0);
	check(UnscaledViewRect.Height() > 0);

	ShadowViewMatrices = ViewMatrices;

	SceneViewInitOptions = FSceneViewInitOptions(InitOptions);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		// console variable override
		int32 Value = CVarShadowFreezeCamera.GetValueOnAnyThread();

		static FViewMatrices Backup = ShadowViewMatrices;

		if(Value)
		{
			ShadowViewMatrices = Backup;
		}
		else
		{
			Backup = ShadowViewMatrices;
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	SetupViewFrustum(*this);

	// Determine whether the view should reverse the cull mode due to a negative determinant.  Only do this for a valid scene
	bReverseCulling = (Family && Family->Scene) ? FMath::IsNegativeFloat(ViewMatrices.GetViewMatrix().Determinant()) : false;

	// OpenGL Gamma space output in GLSL flips Y when rendering directly to the back buffer (so not needed on PC, as we never render directly into the back buffer)
	auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	bool bUsingMobileRenderer = FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile;
	bool bPlatformRequiresReverseCulling = ((IsOpenGLPlatform(ShaderPlatform) || IsSwitchPlatform(ShaderPlatform)) && bUsingMobileRenderer && !IsPCPlatform(ShaderPlatform) && !IsVulkanMobilePlatform(ShaderPlatform));
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	check(MobileHDRCvar);
	const bool bSkipPostprocessing = MobileHDRCvar->GetValueOnAnyThread() == 0;
	bReverseCulling = (bPlatformRequiresReverseCulling && bSkipPostprocessing) ? !bReverseCulling : bReverseCulling;

	// Setup transformation constants to be used by the graphics hardware to transform device normalized depth samples
	// into world oriented z.
	InvDeviceZToWorldZTransform = CreateInvDeviceZToWorldZTransform(ProjectionMatrixUnadjustedForRHI);

	static TConsoleVariableData<int32>* SortPolicyCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TranslucentSortPolicy"));
	TranslucentSortPolicy = static_cast<ETranslucentSortPolicy::Type>(SortPolicyCvar->GetValueOnAnyThread());

	TranslucentSortAxis = GetDefault<URendererSettings>()->TranslucentSortAxis;

	// As the world is only accessible from the game thread, bIsGameView should be explicitly
	// set on any other thread.
	if(IsInGameThread())
	{
		bIsGameView = (Family && Family->Scene && Family->Scene->GetWorld() ) ? Family->Scene->GetWorld()->IsGameWorld() : false;
	}

	bUseFieldOfViewForLOD = InitOptions.bUseFieldOfViewForLOD;
	FOV = InitOptions.FOV;
	DesiredFOV = InitOptions.DesiredFOV;

	DrawDynamicFlags = EDrawDynamicFlags::None;
	bAllowTemporalJitter = true;

#if WITH_EDITOR
	bUsePixelInspector = false;

	EditorViewBitflag = InitOptions.EditorViewBitflag;

	SelectionOutlineColor = GEngine->GetSelectionOutlineColor();
#endif

	// Query instanced stereo and multi-view state
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.InstancedStereo"));
	bIsInstancedStereoEnabled = !bUsingMobileRenderer && RHISupportsInstancedStereo(ShaderPlatform) && (CVar && CVar->GetValueOnAnyThread() != 0);

	// TODO: Should be renamed to multi-viewport
	bIsMultiViewEnabled = RHISupportsMultiView(ShaderPlatform) && bIsInstancedStereoEnabled;

	bIsMobileMultiViewEnabled = Family && Family->bRequireMultiView;
	if (bIsMobileMultiViewEnabled && !RHISupportsMobileMultiView(ShaderPlatform))
	{
		// Native mobile multi-view is not supported, attempt to fall back to instancing on compatible RHIs
		if (RHISupportsInstancedStereo(ShaderPlatform) && !GRHISupportsArrayIndexFromAnyShader)
		{
			UE_LOG(LogMultiView, Fatal, TEXT("Mobile Multi-View not supported by the RHI and no fallback is available."));
		}
		bIsMobileMultiViewEnabled = bIsInstancedStereoEnabled = RHISupportsInstancedStereo(ShaderPlatform);
	}

	bShouldBindInstancedViewUB = bIsInstancedStereoEnabled || bIsMobileMultiViewEnabled;

	SetupAntiAliasingMethod();

	if ((AntiAliasingMethod == AAM_TemporalAA && GetFeatureLevel() >= ERHIFeatureLevel::SM5) &&
		(CVarEnableTemporalUpsample.GetValueOnAnyThread() || (Family && Family->GetTemporalUpscalerInterface() != nullptr)))
	{
		PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::TemporalUpscale;
	}

	if (Family && Family->bResolveScene && Family->EngineShowFlags.PostProcessing)
	{
		EyeAdaptationViewState = State;

		// When rendering in stereo we want to use the same exposure for both eyes.
		if (IStereoRendering::IsASecondaryView(*this))
		{
			check(Family->Views.Num() >= 1);
			const FSceneView* PrimaryView = Family->Views[0];
			if (IStereoRendering::IsAPrimaryView(*PrimaryView))
			{
				EyeAdaptationViewState = PrimaryView->State;
			}
		}
	}

	check(VerifyMembersChecks());
}


#if DO_CHECK
bool FSceneView::VerifyMembersChecks() const
{
	if (PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
	{
		checkf(GetFeatureLevel() >= ERHIFeatureLevel::SM5, TEXT("Temporal upsample is SM5 only."));
		checkf(AntiAliasingMethod == AAM_TemporalAA, TEXT("ScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale requires AntiAliasingMethod == AAM_TemporalAA"));
	}

	if (AntiAliasingMethod == AAM_TemporalAA)
	{
		checkf(State, TEXT("TemporalAA requires the view to have a valid state."));
	}

	return true;
}
#endif


void FSceneView::SetupAntiAliasingMethod()
{
	{
		int32 Value = CVarDefaultAntiAliasing.GetValueOnAnyThread();
		if (Value >= 0 && Value < AAM_MAX)
		{
			AntiAliasingMethod = (EAntiAliasingMethod)Value;
		}
	}

	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	if (FeatureLevel <= ERHIFeatureLevel::ES3_1 && CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false)
	{
		// Using mobile MSAA, disable other AA methods.
		AntiAliasingMethod = AAM_None;

		// Turn off various features which won't work with mobile MSAA.
		//FinalPostProcessSettings.DepthOfFieldScale = 0.0f;
	}

	if (Family)
	{
		static IConsoleVariable* CVarMSAACount = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount"));

		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);

		if (AntiAliasingMethod == AAM_MSAA && IsForwardShadingEnabled(ShaderPlatform) && CVarMSAACount->GetInt() <= 0)
		{
			// Fallback to temporal AA so we can easily toggle methods with r.MSAACount
			AntiAliasingMethod = AAM_TemporalAA;
		}

		static const auto PostProcessAAQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PostProcessAAQuality"));
		static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
		static auto* MobileMSAACvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
		uint32 MobileMSAAValue = MobileMSAACvar->GetValueOnAnyThread();

		int32 Quality = FMath::Clamp(PostProcessAAQualityCVar->GetValueOnAnyThread(), 0, 6);
		const bool bWillApplyTemporalAA = Family->EngineShowFlags.PostProcessing || bIsPlanarReflection;

		if (!bWillApplyTemporalAA || !Family->EngineShowFlags.AntiAliasing || Quality <= 0
			// Disable antialiasing in GammaLDR mode to avoid jittering.
			|| (FeatureLevel <= ERHIFeatureLevel::ES3_1 && MobileHDRCvar->GetValueOnAnyThread() == 0)
			|| (FeatureLevel <= ERHIFeatureLevel::ES3_1 && (MobileMSAAValue > 1)))
		{
			AntiAliasingMethod = AAM_None;
		}

		if (AntiAliasingMethod == AAM_TemporalAA)
		{
			if (!Family->EngineShowFlags.TemporalAA || !Family->bRealtimeUpdate || Quality < 3)
			{
				AntiAliasingMethod = AAM_FXAA;
			}
		}

		// Overides the anti aliasing method to temporal AA when using a custom temporal upscaler.
		if (GetFeatureLevel() >= ERHIFeatureLevel::SM5 && Family->GetTemporalUpscalerInterface() != nullptr)
		{
			AntiAliasingMethod = AAM_TemporalAA;
		}
	}

    // TemporalAA requires view state for history.
	if (AntiAliasingMethod == AAM_TemporalAA && !State)
	{
		AntiAliasingMethod = AAM_None;
	}
}

FVector FSceneView::GetTemporalLODOrigin(int32 Index, bool bUseLaggedLODTransition) const
{
	if (bUseLaggedLODTransition && State)
	{
		const FTemporalLODState& LODState = State->GetTemporalLODState();
		if (LODState.TemporalLODLag != 0.0f)
		{
			return LODState.TemporalLODViewOrigin[Index];
		}
	}
	return ViewMatrices.GetViewOrigin();
}

float FSceneView::GetTemporalLODTransition() const
{
	if (State)
	{
		return State->GetTemporalLODTransition();
	}
	return 0.0f;
}

uint32 FSceneView::GetViewKey() const
{
	if (State)
	{
		return State->GetViewKey();
	}
	return 0;
}

uint32 FSceneView::GetOcclusionFrameCounter() const
{
	if (State)
	{
		return State->GetOcclusionFrameCounter();
	}
	return MAX_uint32;
}

void FSceneView::UpdateProjectionMatrix(const FMatrix& NewProjectionMatrix)
{
	ProjectionMatrixUnadjustedForRHI = NewProjectionMatrix;
	InvDeviceZToWorldZTransform = CreateInvDeviceZToWorldZTransform(ProjectionMatrixUnadjustedForRHI);

	// Update init options before creating new view matrices
	SceneViewInitOptions.ProjectionMatrix = NewProjectionMatrix;

	// Create new matrices
	FViewMatrices NewViewMatrices = FViewMatrices(SceneViewInitOptions);
	ViewMatrices = NewViewMatrices;

	SetupViewFrustum(*this);
}

void FViewMatrices::UpdateViewMatrix(const FVector& ViewLocation, const FRotator& ViewRotation)
{
	ViewOrigin = ViewLocation;

	FMatrix ViewPlanesMatrix = FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewRotation) * ViewPlanesMatrix;

	ViewMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix;

	// Duplicate HMD rotation matrix with roll removed
	FRotator HMDViewRotation = ViewRotation;
	HMDViewRotation.Roll = 0.f;
	HMDViewMatrixNoRoll = FInverseRotationMatrix(HMDViewRotation) * ViewPlanesMatrix;

	ViewProjectionMatrix = GetViewMatrix() * GetProjectionMatrix();

	InvViewMatrix = ViewRotationMatrix.GetTransposed() * FTranslationMatrix(ViewLocation);
	InvViewProjectionMatrix = GetInvProjectionMatrix() * GetInvViewMatrix();

	PreViewTranslation = -ViewOrigin;

	TranslatedViewMatrix = ViewRotationMatrix;
	InvTranslatedViewMatrix = TranslatedViewMatrix.GetTransposed();
	OverriddenTranslatedViewMatrix = FTranslationMatrix(-PreViewTranslation) * ViewMatrix;
	OverriddenInvTranslatedViewMatrix = InvViewMatrix * FTranslationMatrix(PreViewTranslation);

	// Compute a transform from view origin centered world-space to clip space.
	TranslatedViewProjectionMatrix = GetTranslatedViewMatrix() * GetProjectionMatrix();
	InvTranslatedViewProjectionMatrix = GetInvProjectionMatrix() * GetInvTranslatedViewMatrix();
}

void FSceneView::UpdateViewMatrix()
{
	FVector StereoViewLocation = ViewLocation;
	FRotator StereoViewRotation = ViewRotation;
	if (GEngine->StereoRenderingDevice.IsValid() && IStereoRendering::IsStereoEyePass(StereoPass))
	{
		GEngine->StereoRenderingDevice->CalculateStereoViewOffset(StereoPass, StereoViewRotation, WorldToMetersScale, StereoViewLocation);
		ViewLocation = StereoViewLocation;
		ViewRotation = StereoViewRotation;
	}

	ViewMatrices.UpdateViewMatrix(StereoViewLocation, StereoViewRotation);
	GetViewFrustumBounds(ViewFrustum, ViewMatrices.GetViewProjectionMatrix(), false);

	// We need to keep ShadowViewMatrices in sync.
	ShadowViewMatrices = ViewMatrices;
}

void FViewMatrices::UpdatePlanarReflectionViewMatrix(const FSceneView& SourceView, const FMirrorMatrix& MirrorMatrix)
{
	// This is a subset of the FSceneView ctor that recomputes the transforms changed by late updating the parent camera (in UpdateViewMatrix)
	const FMatrix LocalViewMatrix(MirrorMatrix * SourceView.ViewMatrices.GetViewMatrix());
	HMDViewMatrixNoRoll = LocalViewMatrix.RemoveTranslation();

	ViewOrigin = LocalViewMatrix.InverseTransformPosition(FVector::ZeroVector);
	PreViewTranslation = -ViewOrigin;

	ViewMatrix = FTranslationMatrix(-ViewOrigin) * HMDViewMatrixNoRoll;
	InvViewMatrix = FTranslationMatrix(-ViewMatrix.GetOrigin()) * ViewMatrix.RemoveTranslation().GetTransposed();

	InvViewMatrix = HMDViewMatrixNoRoll.GetTransposed() * FTranslationMatrix(ViewOrigin);

	ViewProjectionMatrix = GetViewMatrix() * GetProjectionMatrix();
	InvViewProjectionMatrix = GetInvProjectionMatrix() * InvViewMatrix;

	OverriddenTranslatedViewMatrix = TranslatedViewMatrix = HMDViewMatrixNoRoll;
	OverriddenInvTranslatedViewMatrix = InvTranslatedViewMatrix = HMDViewMatrixNoRoll.GetTransposed();

	TranslatedViewProjectionMatrix = GetTranslatedViewMatrix() * GetProjectionMatrix();
	InvTranslatedViewProjectionMatrix = GetInvProjectionMatrix() * GetInvTranslatedViewMatrix();
}

void FSceneView::UpdatePlanarReflectionViewMatrix(const FSceneView& SourceView, const FMirrorMatrix& MirrorMatrix)
{
	ViewMatrices.UpdatePlanarReflectionViewMatrix(SourceView, MirrorMatrix);

	// Update bounds
	GetViewFrustumBounds(ViewFrustum, ViewMatrices.GetViewProjectionMatrix(), false);

	// We need to keep ShadowViewMatrices in sync.
	ShadowViewMatrices = ViewMatrices;
}

FVector4 FSceneView::WorldToScreen(const FVector& WorldPoint) const
{
	return ViewMatrices.GetViewProjectionMatrix().TransformFVector4(FVector4(WorldPoint,1));
}

FVector FSceneView::ScreenToWorld(const FVector4& ScreenPoint) const
{
	return ViewMatrices.GetInvViewProjectionMatrix().TransformFVector4(ScreenPoint);
}

bool FSceneView::ScreenToPixel(const FVector4& ScreenPoint,FVector2D& OutPixelLocation) const
{
	if(ScreenPoint.W != 0.0f)
	{
		//Reverse the W in the case it is negative, this allow to manipulate a manipulator in the same direction when the camera is really close to the manipulator.
		float InvW = (ScreenPoint.W > 0.0f ? 1.0f : -1.0f) / ScreenPoint.W;
		float Y = (GProjectionSignY > 0.0f) ? ScreenPoint.Y : 1.0f - ScreenPoint.Y;
		OutPixelLocation = FVector2D(
			UnscaledViewRect.Min.X + (0.5f + ScreenPoint.X * 0.5f * InvW) * UnscaledViewRect.Width(),
			UnscaledViewRect.Min.Y + (0.5f - Y * 0.5f * InvW) * UnscaledViewRect.Height()
			);
		return true;
	}
	else
	{
		return false;
	}
}

FVector4 FSceneView::PixelToScreen(float InX,float InY,float Z) const
{
	if (GProjectionSignY > 0.0f)
	{
		return FVector4(
			-1.0f + InX / UnscaledViewRect.Width() * +2.0f,
			+1.0f + InY / UnscaledViewRect.Height() * -2.0f,
			Z,
			1
			);
	}
	else
	{
		return FVector4(
			-1.0f + InX / UnscaledViewRect.Width() * +2.0f,
			1.0f - (+1.0f + InY / UnscaledViewRect.Height() * -2.0f),
			Z,
			1
			);
	}
}

/** Transforms a point from the view's world-space into pixel coordinates relative to the view's X,Y. */
bool FSceneView::WorldToPixel(const FVector& WorldPoint,FVector2D& OutPixelLocation) const
{
	const FVector4 ScreenPoint = WorldToScreen(WorldPoint);
	return ScreenToPixel(ScreenPoint, OutPixelLocation);
}

/** Transforms a point from pixel coordinates relative to the view's X,Y (left, top) into the view's world-space. */
FVector4 FSceneView::PixelToWorld(float X,float Y,float Z) const
{
	const FVector4 ScreenPoint = PixelToScreen(X, Y, Z);
	return ScreenToWorld(ScreenPoint);
}

/**
 * Transforms a point from the view's world-space into the view's screen-space.
 * Divides the resulting X, Y, Z by W before returning.
 */
FPlane FSceneView::Project(const FVector& WorldPoint) const
{
	FPlane Result = WorldToScreen(WorldPoint);

	if (Result.W == 0)
	{
		Result.W = KINDA_SMALL_NUMBER;
	}

	const float RHW = 1.0f / Result.W;

	return FPlane(Result.X * RHW,Result.Y * RHW,Result.Z * RHW,Result.W);
}

/**
 * Transforms a point from the view's screen-space into world coordinates
 * multiplies X, Y, Z by W before transforming.
 */
FVector FSceneView::Deproject(const FPlane& ScreenPoint) const
{
	return ViewMatrices.GetInvViewProjectionMatrix().TransformFVector4(FPlane(ScreenPoint.X * ScreenPoint.W,ScreenPoint.Y * ScreenPoint.W,ScreenPoint.Z * ScreenPoint.W,ScreenPoint.W));
}

void FSceneView::DeprojectFVector2D(const FVector2D& ScreenPos, FVector& out_WorldOrigin, FVector& out_WorldDirection) const
{
	const FMatrix InvViewProjectionMatrix = ViewMatrices.GetInvViewProjectionMatrix();
	DeprojectScreenToWorld(ScreenPos, UnscaledViewRect, InvViewProjectionMatrix, out_WorldOrigin, out_WorldDirection);
}

void FSceneView::DeprojectScreenToWorld(const FVector2D& ScreenPos, const FIntRect& ViewRect, const FMatrix& InvViewMatrix, const FMatrix& InvProjectionMatrix, FVector& out_WorldOrigin, FVector& out_WorldDirection)
{
	int32 PixelX = FMath::TruncToInt(ScreenPos.X);
	int32 PixelY = FMath::TruncToInt(ScreenPos.Y);

	// Get the eye position and direction of the mouse cursor in two stages (inverse transform projection, then inverse transform view).
	// This avoids the numerical instability that occurs when a view matrix with large translation is composed with a projection matrix

	// Get the pixel coordinates into 0..1 normalized coordinates within the constrained view rectangle
	const float NormalizedX = (PixelX - ViewRect.Min.X) / ((float)ViewRect.Width());
	const float NormalizedY = (PixelY - ViewRect.Min.Y) / ((float)ViewRect.Height());

	// Get the pixel coordinates into -1..1 projection space
	const float ScreenSpaceX = (NormalizedX - 0.5f) * 2.0f;
	const float ScreenSpaceY = ((1.0f - NormalizedY) - 0.5f) * 2.0f;

	// The start of the ray trace is defined to be at mousex,mousey,1 in projection space (z=1 is near, z=0 is far - this gives us better precision)
	// To get the direction of the ray trace we need to use any z between the near and the far plane, so let's use (mousex, mousey, 0.5)
	const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0.5f, 1.0f);

	// Projection (changing the W coordinate) is not handled by the FMatrix transforms that work with vectors, so multiplications
	// by the projection matrix should use homogeneous coordinates (i.e. FPlane).
	const FVector4 HGRayStartViewSpace = InvProjectionMatrix.TransformFVector4(RayStartProjectionSpace);
	const FVector4 HGRayEndViewSpace = InvProjectionMatrix.TransformFVector4(RayEndProjectionSpace);
	FVector RayStartViewSpace(HGRayStartViewSpace.X, HGRayStartViewSpace.Y, HGRayStartViewSpace.Z);
	FVector RayEndViewSpace(HGRayEndViewSpace.X,   HGRayEndViewSpace.Y,   HGRayEndViewSpace.Z);
	// divide vectors by W to undo any projection and get the 3-space coordinate
	if (HGRayStartViewSpace.W != 0.0f)
	{
		RayStartViewSpace /= HGRayStartViewSpace.W;
	}
	if (HGRayEndViewSpace.W != 0.0f)
	{
		RayEndViewSpace /= HGRayEndViewSpace.W;
	}
	FVector RayDirViewSpace = RayEndViewSpace - RayStartViewSpace;
	RayDirViewSpace = RayDirViewSpace.GetSafeNormal();

	// The view transform does not have projection, so we can use the standard functions that deal with vectors and normals (normals
	// are vectors that do not use the translational part of a rotation/translation)
	const FVector RayStartWorldSpace = InvViewMatrix.TransformPosition(RayStartViewSpace);
	const FVector RayDirWorldSpace = InvViewMatrix.TransformVector(RayDirViewSpace);

	// Finally, store the results in the hitcheck inputs.  The start position is the eye, and the end position
	// is the eye plus a long distance in the direction the mouse is pointing.
	out_WorldOrigin = RayStartWorldSpace;
	out_WorldDirection = RayDirWorldSpace.GetSafeNormal();
}

void FSceneView::DeprojectScreenToWorld(const FVector2D& ScreenPos, const FIntRect& ViewRect, const FMatrix& InvViewProjMatrix, FVector& out_WorldOrigin, FVector& out_WorldDirection)
{
	float PixelX = FMath::TruncToFloat(ScreenPos.X);
	float PixelY = FMath::TruncToFloat(ScreenPos.Y);

	// Get the eye position and direction of the mouse cursor in two stages (inverse transform projection, then inverse transform view).
	// This avoids the numerical instability that occurs when a view matrix with large translation is composed with a projection matrix

	// Get the pixel coordinates into 0..1 normalized coordinates within the constrained view rectangle
	const float NormalizedX = (PixelX - ViewRect.Min.X) / ((float)ViewRect.Width());
	const float NormalizedY = (PixelY - ViewRect.Min.Y) / ((float)ViewRect.Height());

	// Get the pixel coordinates into -1..1 projection space
	const float ScreenSpaceX = (NormalizedX - 0.5f) * 2.0f;
	const float ScreenSpaceY = ((1.0f - NormalizedY) - 0.5f) * 2.0f;

	// The start of the ray trace is defined to be at mousex,mousey,1 in projection space (z=1 is near, z=0 is far - this gives us better precision)
	// To get the direction of the ray trace we need to use any z between the near and the far plane, so let's use (mousex, mousey, 0.5)
	const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0.5f, 1.0f);

	// Projection (changing the W coordinate) is not handled by the FMatrix transforms that work with vectors, so multiplications
	// by the projection matrix should use homogeneous coordinates (i.e. FPlane).
	const FVector4 HGRayStartWorldSpace = InvViewProjMatrix.TransformFVector4(RayStartProjectionSpace);
	const FVector4 HGRayEndWorldSpace = InvViewProjMatrix.TransformFVector4(RayEndProjectionSpace);
	FVector RayStartWorldSpace(HGRayStartWorldSpace.X, HGRayStartWorldSpace.Y, HGRayStartWorldSpace.Z);
	FVector RayEndWorldSpace(HGRayEndWorldSpace.X, HGRayEndWorldSpace.Y, HGRayEndWorldSpace.Z);
	// divide vectors by W to undo any projection and get the 3-space coordinate
	if (HGRayStartWorldSpace.W != 0.0f)
	{
		RayStartWorldSpace /= HGRayStartWorldSpace.W;
	}
	if (HGRayEndWorldSpace.W != 0.0f)
	{
		RayEndWorldSpace /= HGRayEndWorldSpace.W;
	}
	const FVector RayDirWorldSpace = (RayEndWorldSpace - RayStartWorldSpace).GetSafeNormal();

	// Finally, store the results in the outputs
	out_WorldOrigin = RayStartWorldSpace;
	out_WorldDirection = RayDirWorldSpace;
}

bool FSceneView::ProjectWorldToScreen(const FVector& WorldPosition, const FIntRect& ViewRect, const FMatrix& ViewProjectionMatrix, FVector2D& out_ScreenPos)
{
	FPlane Result = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPosition, 1.f));
	if ( Result.W > 0.0f )
	{
		// the result of this will be x and y coords in -1..1 projection space
		const float RHW = 1.0f / Result.W;
		FPlane PosInScreenSpace = FPlane(Result.X * RHW, Result.Y * RHW, Result.Z * RHW, Result.W);

		// Move from projection space to normalized 0..1 UI space
		const float NormalizedX = ( PosInScreenSpace.X / 2.f ) + 0.5f;
		const float NormalizedY = 1.f - ( PosInScreenSpace.Y / 2.f ) - 0.5f;

		FVector2D RayStartViewRectSpace(
			( NormalizedX * (float)ViewRect.Width() ),
			( NormalizedY * (float)ViewRect.Height() )
			);

		out_ScreenPos = RayStartViewRectSpace + FVector2D(static_cast<float>(ViewRect.Min.X), static_cast<float>(ViewRect.Min.Y));

		return true;
	}

	return false;
}


#define LERP_PP(NAME) if(Src.bOverride_ ## NAME)	Dest . NAME = FMath::Lerp(Dest . NAME, Src . NAME, Weight);
#define IF_PP(NAME) if(Src.bOverride_ ## NAME && Src . NAME)

// @param Weight 0..1
void FSceneView::OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight)
{
	SCOPE_CYCLE_COUNTER(STAT_OverridePostProcessSettings);

	if(Weight <= 0.0f)
	{
		// no need to blend anything
		return;
	}

	if(Weight > 1.0f)
	{
		Weight = 1.0f;
	}

	{
		FFinalPostProcessSettings& Dest = FinalPostProcessSettings;

		// The following code needs to be adjusted when settings in FPostProcessSettings change.
		LERP_PP(WhiteTemp);
		LERP_PP(WhiteTint);

		LERP_PP(ColorSaturation);
		LERP_PP(ColorContrast);
		LERP_PP(ColorGamma);
		LERP_PP(ColorGain);
		LERP_PP(ColorOffset);

		LERP_PP(ColorSaturationShadows);
		LERP_PP(ColorContrastShadows);
		LERP_PP(ColorGammaShadows);
		LERP_PP(ColorGainShadows);
		LERP_PP(ColorOffsetShadows);

		LERP_PP(ColorSaturationMidtones);
		LERP_PP(ColorContrastMidtones);
		LERP_PP(ColorGammaMidtones);
		LERP_PP(ColorGainMidtones);
		LERP_PP(ColorOffsetMidtones);

		LERP_PP(ColorSaturationHighlights);
		LERP_PP(ColorContrastHighlights);
		LERP_PP(ColorGammaHighlights);
		LERP_PP(ColorGainHighlights);
		LERP_PP(ColorOffsetHighlights);

		LERP_PP(ColorCorrectionShadowsMax);
		LERP_PP(ColorCorrectionHighlightsMin);

		LERP_PP(BlueCorrection);
		LERP_PP(ExpandGamut);
		LERP_PP(ToneCurveAmount);

		LERP_PP(FilmWhitePoint);
		LERP_PP(FilmSaturation);
		LERP_PP(FilmChannelMixerRed);
		LERP_PP(FilmChannelMixerGreen);
		LERP_PP(FilmChannelMixerBlue);
		LERP_PP(FilmContrast);
		LERP_PP(FilmDynamicRange);
		LERP_PP(FilmHealAmount);
		LERP_PP(FilmToeAmount);
		LERP_PP(FilmShadowTint);
		LERP_PP(FilmShadowTintBlend);
		LERP_PP(FilmShadowTintAmount);

		LERP_PP(FilmSlope);
		LERP_PP(FilmToe);
		LERP_PP(FilmShoulder);
		LERP_PP(FilmBlackClip);
		LERP_PP(FilmWhiteClip);

		LERP_PP(SceneColorTint);
		LERP_PP(SceneFringeIntensity);
		LERP_PP(ChromaticAberrationStartOffset);
		LERP_PP(BloomIntensity);
		LERP_PP(BloomThreshold);
		LERP_PP(Bloom1Tint);
		LERP_PP(BloomSizeScale);
		LERP_PP(Bloom1Size);
		LERP_PP(Bloom2Tint);
		LERP_PP(Bloom2Size);
		LERP_PP(Bloom3Tint);
		LERP_PP(Bloom3Size);
		LERP_PP(Bloom4Tint);
		LERP_PP(Bloom4Size);
		LERP_PP(Bloom5Tint);
		LERP_PP(Bloom5Size);
		LERP_PP(Bloom6Tint);
		LERP_PP(Bloom6Size);
		LERP_PP(BloomDirtMaskIntensity);
		LERP_PP(BloomDirtMaskTint);
		LERP_PP(BloomConvolutionSize);
		LERP_PP(BloomConvolutionCenterUV);
		LERP_PP(BloomConvolutionPreFilterMin);
		LERP_PP(BloomConvolutionPreFilterMax);
		LERP_PP(BloomConvolutionPreFilterMult);
		LERP_PP(AmbientCubemapIntensity);
		LERP_PP(AmbientCubemapTint);
		LERP_PP(CameraShutterSpeed);
		LERP_PP(CameraISO);
		LERP_PP(AutoExposureLowPercent);
		LERP_PP(AutoExposureHighPercent);
		LERP_PP(AutoExposureMinBrightness);
		LERP_PP(AutoExposureMaxBrightness);
		LERP_PP(AutoExposureSpeedUp);
		LERP_PP(AutoExposureSpeedDown);
		LERP_PP(AutoExposureBias);
		LERP_PP(HistogramLogMin);
		LERP_PP(HistogramLogMax);
		LERP_PP(LensFlareIntensity);
		LERP_PP(LensFlareTint);
		LERP_PP(LensFlareBokehSize);
		LERP_PP(LensFlareThreshold);
		LERP_PP(VignetteIntensity);
		LERP_PP(GrainIntensity);
		LERP_PP(GrainJitter);
		LERP_PP(AmbientOcclusionIntensity);
		LERP_PP(AmbientOcclusionStaticFraction);
		LERP_PP(AmbientOcclusionRadius);
		LERP_PP(AmbientOcclusionFadeDistance);
		LERP_PP(AmbientOcclusionFadeRadius);
		LERP_PP(AmbientOcclusionDistance_DEPRECATED);
		LERP_PP(AmbientOcclusionPower);
		LERP_PP(AmbientOcclusionBias);
		LERP_PP(AmbientOcclusionQuality);
		LERP_PP(AmbientOcclusionMipBlend);
		LERP_PP(AmbientOcclusionMipScale);
		LERP_PP(AmbientOcclusionMipThreshold);
		LERP_PP(AmbientOcclusionTemporalBlendWeight);
		LERP_PP(IndirectLightingColor);
		LERP_PP(IndirectLightingIntensity);
		LERP_PP(DepthOfFieldFocalDistance);
		LERP_PP(DepthOfFieldFstop);
		LERP_PP(DepthOfFieldMinFstop);
		LERP_PP(DepthOfFieldSensorWidth);
		LERP_PP(DepthOfFieldDepthBlurRadius);
		LERP_PP(DepthOfFieldDepthBlurAmount);
		LERP_PP(DepthOfFieldFocalRegion);
		LERP_PP(DepthOfFieldNearTransitionRegion);
		LERP_PP(DepthOfFieldFarTransitionRegion);
		LERP_PP(DepthOfFieldScale);
		LERP_PP(DepthOfFieldNearBlurSize);
		LERP_PP(DepthOfFieldFarBlurSize);
		LERP_PP(DepthOfFieldOcclusion);
		LERP_PP(DepthOfFieldSkyFocusDistance);
		LERP_PP(DepthOfFieldVignetteSize);
		LERP_PP(MotionBlurAmount);
		LERP_PP(MotionBlurMax);
		LERP_PP(MotionBlurPerObjectSize);
		LERP_PP(ScreenPercentage);
		LERP_PP(ScreenSpaceReflectionQuality);
		LERP_PP(ScreenSpaceReflectionIntensity);
		LERP_PP(ScreenSpaceReflectionMaxRoughness);

		// Ray Tracing
		if (Src.bOverride_ReflectionsType)
		{
			Dest.ReflectionsType = Src.ReflectionsType;
		}

		if (Src.bOverride_RayTracingReflectionsMaxRoughness)
		{
			Dest.RayTracingReflectionsMaxRoughness = Src.RayTracingReflectionsMaxRoughness;
		}

		if (Src.bOverride_RayTracingReflectionsMaxBounces)
		{
			Dest.RayTracingReflectionsMaxBounces = Src.RayTracingReflectionsMaxBounces;
		}

		if (Src.bOverride_RayTracingReflectionsSamplesPerPixel)
		{
			Dest.RayTracingReflectionsSamplesPerPixel = Src.RayTracingReflectionsSamplesPerPixel;
		}

		if (Src.bOverride_RayTracingReflectionsShadows)
		{
			Dest.RayTracingReflectionsShadows = Src.RayTracingReflectionsShadows;
		}

		if (Src.bOverride_RayTracingReflectionsTranslucency)
		{
			Dest.RayTracingReflectionsTranslucency = Src.RayTracingReflectionsTranslucency;
		}

		if (Src.bOverride_TranslucencyType)
		{
			Dest.TranslucencyType = Src.TranslucencyType;
		}

		if (Src.bOverride_RayTracingTranslucencyMaxRoughness)
		{
			Dest.RayTracingTranslucencyMaxRoughness = Src.RayTracingTranslucencyMaxRoughness;
		}

		if (Src.bOverride_RayTracingTranslucencyRefractionRays)
		{
			Dest.RayTracingTranslucencyRefractionRays = Src.RayTracingTranslucencyRefractionRays;
		}

		if (Src.bOverride_RayTracingTranslucencySamplesPerPixel)
		{
			Dest.RayTracingTranslucencySamplesPerPixel = Src.RayTracingTranslucencySamplesPerPixel;
		}

		if (Src.bOverride_RayTracingTranslucencyShadows)
		{
			Dest.RayTracingTranslucencyShadows = Src.RayTracingTranslucencyShadows;
		}

		if (Src.bOverride_RayTracingTranslucencyRefraction)
		{
			Dest.RayTracingTranslucencyRefraction = Src.RayTracingTranslucencyRefraction;
		}

		if (Src.bOverride_RayTracingGI)
		{
			Dest.RayTracingGIType = Src.RayTracingGIType;
		}

		if (Src.bOverride_RayTracingGIMaxBounces)
		{
			Dest.RayTracingGIMaxBounces = Src.RayTracingGIMaxBounces;
		}

		if (Src.bOverride_RayTracingGISamplesPerPixel)
		{
			Dest.RayTracingGISamplesPerPixel = Src.RayTracingGISamplesPerPixel;
		}

		if (Src.bOverride_RayTracingAO)
		{
			Dest.RayTracingAO = Src.RayTracingAO;
		}

		if (Src.bOverride_RayTracingAOSamplesPerPixel)
		{
			Dest.RayTracingAOSamplesPerPixel = Src.RayTracingAOSamplesPerPixel;
		}

		if (Src.bOverride_RayTracingAOIntensity)
		{
			Dest.RayTracingAOIntensity = Src.RayTracingAOIntensity;
		}

		if (Src.bOverride_RayTracingAORadius)
		{
			Dest.RayTracingAORadius = Src.RayTracingAORadius;
		}

		if (Src.bOverride_PathTracingMaxBounces)
		{
			Dest.PathTracingMaxBounces = Src.PathTracingMaxBounces;
		}

		if (Src.bOverride_PathTracingSamplesPerPixel)
		{
			Dest.PathTracingSamplesPerPixel = Src.PathTracingSamplesPerPixel;
		}


		if (Src.bOverride_DepthOfFieldBladeCount)
		{
			Dest.DepthOfFieldBladeCount = Src.DepthOfFieldBladeCount;
		}

		// cubemaps are getting blended additively - in contrast to other properties, maybe we should make that consistent
		if (Src.AmbientCubemap && Src.bOverride_AmbientCubemapIntensity)
		{
			FFinalPostProcessSettings::FCubemapEntry Entry;

			Entry.AmbientCubemapTintMulScaleValue = FLinearColor(1, 1, 1, 1) * Src.AmbientCubemapIntensity;

			if (Src.bOverride_AmbientCubemapTint)
			{
				Entry.AmbientCubemapTintMulScaleValue *= Src.AmbientCubemapTint;
			}

			Entry.AmbientCubemap = Src.AmbientCubemap;
			Dest.UpdateEntry(Entry, Weight);
		}

		IF_PP(ColorGradingLUT)
		{
			float ColorGradingIntensity = FMath::Clamp(Src.ColorGradingIntensity, 0.0f, 1.0f);
			Dest.LerpTo(Src.ColorGradingLUT, ColorGradingIntensity * Weight);
		}

		// actual texture cannot be blended but the intensity can be blended
		IF_PP(BloomDirtMask)
		{
			Dest.BloomDirtMask = Src.BloomDirtMask;
		}

		if (Src.bOverride_BloomMethod)
		{
			Dest.BloomMethod = Src.BloomMethod;
		}

		// actual texture cannot be blended but the intensity can be blended
		IF_PP(BloomConvolutionTexture)
		{
			Dest.BloomConvolutionTexture = Src.BloomConvolutionTexture;
		}

		// A continuous blending of this value would result trashing the pre-convolved bloom kernel cache.
		IF_PP(BloomConvolutionBufferScale)
		{
			Dest.BloomConvolutionBufferScale = Src.BloomConvolutionBufferScale;
		}

		// Curve assets can not be blended.
		IF_PP(AutoExposureBiasCurve)
		{
			Dest.AutoExposureBiasCurve = Src.AutoExposureBiasCurve;
		}

		// Texture asset isn't blended
		IF_PP(AutoExposureMeterMask)
		{
			Dest.AutoExposureMeterMask = Src.AutoExposureMeterMask;
		}

		// actual texture cannot be blended but the intensity can be blended
		IF_PP(LensFlareBokehShape)
		{
			Dest.LensFlareBokehShape = Src.LensFlareBokehShape;
		}

		if (Src.bOverride_LensFlareTints)
		{
			for (uint32 i = 0; i < 8; ++i)
			{
				Dest.LensFlareTints[i] = FMath::Lerp(Dest.LensFlareTints[i], Src.LensFlareTints[i], Weight);
			}
		}

		if (Src.bOverride_MobileHQGaussian)
		{
			Dest.bMobileHQGaussian = Src.bMobileHQGaussian;
		}

		if (Src.bOverride_AutoExposureMethod)
		{
			Dest.AutoExposureMethod = Src.AutoExposureMethod;
		}

		if (Src.bOverride_AmbientOcclusionRadiusInWS)
		{
			Dest.AmbientOcclusionRadiusInWS = Src.AmbientOcclusionRadiusInWS;
		}

		if (Src.bOverride_MotionBlurTargetFPS)
		{
			Dest.MotionBlurTargetFPS = Src.MotionBlurTargetFPS;
		}

		if (Src.bOverride_AutoExposureApplyPhysicalCameraExposure)
		{
			Dest.AutoExposureApplyPhysicalCameraExposure = Src.AutoExposureApplyPhysicalCameraExposure;
		}
	}

	// will be deprecated soon, use the new asset LightPropagationVolumeBlendable instead
	{
		FLightPropagationVolumeSettings& Dest = FinalPostProcessSettings.BlendableManager.GetSingleFinalData<FLightPropagationVolumeSettings>();

		LERP_PP(LPVIntensity);
		LERP_PP(LPVSecondaryOcclusionIntensity);
		LERP_PP(LPVSecondaryBounceIntensity);
		LERP_PP(LPVVplInjectionBias);
		LERP_PP(LPVGeometryVolumeBias);
		LERP_PP(LPVEmissiveInjectionIntensity);
		LERP_PP(LPVDirectionalOcclusionIntensity);
		LERP_PP(LPVDirectionalOcclusionRadius);
		LERP_PP(LPVDiffuseOcclusionExponent);
		LERP_PP(LPVSpecularOcclusionExponent);
		LERP_PP(LPVDiffuseOcclusionIntensity);
		LERP_PP(LPVSpecularOcclusionIntensity);
		LERP_PP(LPVFadeRange);
		LERP_PP(LPVDirectionalOcclusionFadeRange);

		if (Src.bOverride_LPVSize)
		{
			Dest.LPVSize = Src.LPVSize;
		}
	}

	// Blendable objects
	{
		uint32 Count = Src.WeightedBlendables.Array.Num();

		for(uint32 i = 0; i < Count; ++i)
		{
			UObject* Object = Src.WeightedBlendables.Array[i].Object;

			if(!Object || !Object->IsValidLowLevel())
			{
				continue;
			}

			IBlendableInterface* BlendableInterface = Cast<IBlendableInterface>(Object);

			if(!BlendableInterface)
			{
				continue;
			}

			float LocalWeight = FMath::Min(1.0f, Src.WeightedBlendables.Array[i].Weight) * Weight;

			if(LocalWeight > 0.0f)
			{
				BlendableInterface->OverrideBlendableSettings(*this, LocalWeight);
			}
		}
	}
}

/** Dummy class needed to support Cast<IBlendableInterface>(Object) */
UBlendableInterface::UBlendableInterface(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}



void FSceneView::StartFinalPostprocessSettings(FVector InViewLocation)
{
	SCOPE_CYCLE_COUNTER(STAT_StartFinalPostprocessSettings);

	check(IsInGameThread());

	// The final settings for the current viewer position (blended together from many volumes).
	// Setup by the main thread, passed to the render thread and never touched again by the main thread.

	// Set values before any override happens.
	FinalPostProcessSettings.SetBaseValues();

	// project settings might want to have different defaults
	{
		if(!CVarDefaultBloom.GetValueOnGameThread())
		{
			FinalPostProcessSettings.BloomIntensity = 0;
		}
		if (!CVarDefaultAmbientOcclusion.GetValueOnGameThread())
		{
			FinalPostProcessSettings.AmbientOcclusionIntensity = 0;
		}
		if (!CVarDefaultAutoExposure.GetValueOnGameThread())
		{
			FinalPostProcessSettings.AutoExposureMinBrightness = 1;
			FinalPostProcessSettings.AutoExposureMaxBrightness = 1;
			if (CVarDefaultAutoExposureExtendDefaultLuminanceRange.GetValueOnGameThread())
			{
				FinalPostProcessSettings.AutoExposureMinBrightness = LuminanceToEV100(FinalPostProcessSettings.AutoExposureMinBrightness);
				FinalPostProcessSettings.AutoExposureMaxBrightness = LuminanceToEV100(FinalPostProcessSettings.AutoExposureMaxBrightness);
			}
		}
		else
		{
			int32 Value = CVarDefaultAutoExposureMethod.GetValueOnGameThread();
			if (Value >= 0 && Value < AEM_MAX)
			{
				FinalPostProcessSettings.AutoExposureMethod = (EAutoExposureMethod)Value;
			}
		}

		if (!CVarDefaultMotionBlur.GetValueOnGameThread())
		{
			FinalPostProcessSettings.MotionBlurAmount = 0;
		}
		if (!CVarDefaultLensFlare.GetValueOnGameThread())
		{
			FinalPostProcessSettings.LensFlareIntensity = 0;
		}

		{
			int32 Value = CVarDefaultAmbientOcclusionStaticFraction.GetValueOnGameThread();

			if(!Value)
			{
				FinalPostProcessSettings.AmbientOcclusionStaticFraction = 0.0f;
			}
		}
	}

	{
		if (GEngine->StereoRenderingDevice.IsValid())
		{
			GEngine->StereoRenderingDevice->StartFinalPostprocessSettings(&FinalPostProcessSettings, StereoPass);
		}
	}

	if (State != nullptr)
	{
		State->OnStartPostProcessing(*this);
	}

	UWorld* World = ((Family != nullptr) && (Family->Scene != nullptr)) ? Family->Scene->GetWorld() : nullptr;

	// Some views have no world (e.g. material preview)
	if (World != nullptr)
	{
		World->AddPostProcessingSettings(InViewLocation, this);
	}
}

void FSceneView::EndFinalPostprocessSettings(const FSceneViewInitOptions& ViewInitOptions)
{
	const auto SceneViewFeatureLevel = GetFeatureLevel();

	// will be deprecated soon, use the new asset LightPropagationVolumeBlendable instead
	{
		FLightPropagationVolumeSettings& Dest = FinalPostProcessSettings.BlendableManager.GetSingleFinalData<FLightPropagationVolumeSettings>();

		if(Dest.LPVDirectionalOcclusionIntensity < 0.001f)
		{
			Dest.LPVDirectionalOcclusionIntensity = 0.0f;
		}

		if (Dest.LPVIntensity < 0.001f)
		{
			Dest.LPVIntensity = 0.0f;
		}

		if(!Family->EngineShowFlags.GlobalIllumination)
		{
			Dest.LPVIntensity = 0.0f;
		}
	}

	{
		static const auto SceneColorFringeQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SceneColorFringeQuality"));

		int32 FringeQuality = SceneColorFringeQualityCVar->GetValueOnGameThread();
		if (FringeQuality <= 0)
		{
			FinalPostProcessSettings.SceneFringeIntensity = 0;
		}
	}

	{
		static const auto BloomQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BloomQuality"));

		int Value = BloomQualityCVar->GetValueOnGameThread();

		if(Value <= 0)
		{
			FinalPostProcessSettings.BloomIntensity = 0.0f;
		}
	}

	if(!Family->EngineShowFlags.Bloom)
	{
		FinalPostProcessSettings.BloomIntensity = 0.0f;
	}

	// scale down tone mapper shader permutation
	{
		int32 Quality = CVarTonemapperQuality.GetValueOnGameThread();

		if(Quality < 1)
		{
			FinalPostProcessSettings.FilmContrast = 0;
		}

		if(Quality < 2)
		{
			FinalPostProcessSettings.VignetteIntensity = 0;
		}

		if(Quality < 3)
		{
			FinalPostProcessSettings.FilmShadowTintAmount = 0;
		}

		if(Quality < 4)
		{
			FinalPostProcessSettings.GrainIntensity = 0;
		}

		if(Quality < 5)
		{
			FinalPostProcessSettings.GrainJitter = 0;
		}
	}

	{
		static const auto DepthOfFieldQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DepthOfFieldQuality"));

		int Value = DepthOfFieldQualityCVar->GetValueOnGameThread();

		if(Value <= 0)
		{
			FinalPostProcessSettings.DepthOfFieldScale = 0.0f;
		}
	}

	if(!Family->EngineShowFlags.DepthOfField)
	{
		FinalPostProcessSettings.DepthOfFieldScale = 0;
	}

	if(!Family->EngineShowFlags.Vignette)
	{
		FinalPostProcessSettings.VignetteIntensity = 0;
	}

	if(!Family->EngineShowFlags.Grain)
	{
		FinalPostProcessSettings.GrainIntensity = 0;
		FinalPostProcessSettings.GrainJitter = 0;
	}

	if(!Family->EngineShowFlags.CameraImperfections)
	{
		FinalPostProcessSettings.BloomDirtMaskIntensity = 0;
	}

	if(!Family->EngineShowFlags.AmbientCubemap)
	{
		FinalPostProcessSettings.ContributingCubemaps.Reset();
	}

	if(!Family->EngineShowFlags.LensFlares)
	{
		FinalPostProcessSettings.LensFlareIntensity = 0;
	}

	if (!Family->EngineShowFlags.ToneCurve)
	{
		FinalPostProcessSettings.ToneCurveAmount = 0;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		float Value = CVarExposureOffset.GetValueOnGameThread();
		FinalPostProcessSettings.AutoExposureBias += Value;
	}

	{
		float& DepthBlurAmount = FinalPostProcessSettings.DepthOfFieldDepthBlurAmount;

		float CVarAmount = CVarDepthOfFieldDepthBlurAmount.GetValueOnGameThread();

		DepthBlurAmount = (CVarAmount > 0.0f) ? (DepthBlurAmount * CVarAmount) : -CVarAmount;
	}

	{
		float& DepthBlurRadius = FinalPostProcessSettings.DepthOfFieldDepthBlurRadius;
		{
			float CVarResScale = FMath::Max(1.0f, CVarDepthOfFieldDepthBlurResolutionScale.GetValueOnGameThread());

			float Factor = FMath::Max(UnscaledViewRect.Width() / 1920.0f - 1.0f, 0.0f);

			DepthBlurRadius *= 1.0f + Factor * (CVarResScale - 1.0f);
		}
		{
			float CVarScale = CVarDepthOfFieldDepthBlurScale.GetValueOnGameThread();

			DepthBlurRadius = (CVarScale > 0.0f) ? (DepthBlurRadius * CVarScale) : -CVarScale;
		}
	}
#endif

	{
		if (GEngine->StereoRenderingDevice.IsValid())
		{
			GEngine->StereoRenderingDevice->EndFinalPostprocessSettings(&FinalPostProcessSettings, StereoPass);
		}
	}

	{
		float Value = CVarSSRMaxRoughness.GetValueOnGameThread();

		if(Value >= 0.0f)
		{
			FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness = Value;
		}
	}

	{
		static const auto AmbientOcclusionStaticFractionCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.AmbientOcclusionStaticFraction"));

		float Value = AmbientOcclusionStaticFractionCVar->GetValueOnGameThread();

		if(Value >= 0.0)
		{
			FinalPostProcessSettings.AmbientOcclusionStaticFraction = Value;
		}
	}

	if(!Family->EngineShowFlags.AmbientOcclusion || !Family->EngineShowFlags.ScreenSpaceAO)
	{
		FinalPostProcessSettings.AmbientOcclusionIntensity = 0;
	}

	{
		static const auto AmbientOcclusionRadiusScaleCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.AmbientOcclusionRadiusScale"));

		float Scale = FMath::Clamp(AmbientOcclusionRadiusScaleCVar->GetValueOnGameThread(), 0.1f, 15.0f);

		FinalPostProcessSettings.AmbientOcclusionRadius *= Scale;
	}

	{
		float Scale = FMath::Clamp(CVarSSAOFadeRadiusScale.GetValueOnGameThread(), 0.01f, 50.0f);

		FinalPostProcessSettings.AmbientOcclusionDistance_DEPRECATED *= Scale;
	}

	{
		float Value = FMath::Clamp(CVarMotionBlurScale.GetValueOnGameThread(), 0.0f, 50.0f);

		FinalPostProcessSettings.MotionBlurAmount *= Value;
	}

	{
		float Value = CVarMotionBlurAmount.GetValueOnGameThread();

		if(Value >= 0.0f)
		{
			FinalPostProcessSettings.MotionBlurAmount = Value;
		}
	}

	{
		float Value = CVarMotionBlurMax.GetValueOnGameThread();

		if(Value >= 0.0f)
		{
			FinalPostProcessSettings.MotionBlurMax = Value;
		}
	}

	{
		int32 TargetFPS = CVarMotionBlurTargetFPS.GetValueOnGameThread();

		if (TargetFPS >= 0)
		{
			FinalPostProcessSettings.MotionBlurTargetFPS = TargetFPS;
		}
	}

	{
		float Value = CVarSceneColorFringeMax.GetValueOnGameThread();

		if (Value >= 0.0f)
		{
			FinalPostProcessSettings.SceneFringeIntensity = FMath::Min(FinalPostProcessSettings.SceneFringeIntensity, Value);
		}
		else if (Value == -2.0f)
		{
			FinalPostProcessSettings.SceneFringeIntensity = 5.0f;
		}

		if(!Family->EngineShowFlags.SceneColorFringe || !Family->EngineShowFlags.CameraImperfections)
		{
			FinalPostProcessSettings.SceneFringeIntensity = 0;
		}
	}

	if (!Family->EngineShowFlags.Lighting || !Family->EngineShowFlags.GlobalIllumination)
	{
		FinalPostProcessSettings.IndirectLightingColor = FLinearColor(0,0,0,0);
		FinalPostProcessSettings.IndirectLightingIntensity = 0.0f;
	}

	if (AllowDebugViewmodes())
	{
		ConfigureBufferVisualizationSettings();
	}

#if !(UE_BUILD_SHIPPING)
	if (Family->EngineShowFlags.IsVisualizeCalibrationEnabled())
	{
		ConfigureVisualizeCalibrationSettings();
	}
#endif

#if WITH_EDITOR
	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();

	// Pass highres screenshot materials through post process settings
	FinalPostProcessSettings.HighResScreenshotMaterial = Config.HighResScreenshotMaterial;
	FinalPostProcessSettings.HighResScreenshotMaskMaterial = Config.HighResScreenshotMaskMaterial;
	FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial = NULL;

	// If the highres screenshot UI is open and we're not taking a highres screenshot this frame
	if (Config.bDisplayCaptureRegion && !GIsHighResScreenshot)
	{
		// Only enable the capture region effect if the capture region is different from the view rectangle...
		if ((Config.UnscaledCaptureRegion != UnscaledViewRect) && (Config.UnscaledCaptureRegion.Area() > 0) && (State != NULL))
		{
			// ...and if this is the viewport associated with the highres screenshot UI
			auto ConfigViewport = Config.TargetViewport.Pin();
			if (ConfigViewport.IsValid() && Family->RenderTarget == ConfigViewport->GetViewport())
			{
				static const FName ParamName = "RegionRect";
				FLinearColor NormalizedCaptureRegion;

				// Normalize capture region into view rectangle
				NormalizedCaptureRegion.R = (float)Config.UnscaledCaptureRegion.Min.X / (float)UnscaledViewRect.Width();
				NormalizedCaptureRegion.G = (float)Config.UnscaledCaptureRegion.Min.Y / (float)UnscaledViewRect.Height();
				NormalizedCaptureRegion.B = (float)Config.UnscaledCaptureRegion.Max.X / (float)UnscaledViewRect.Width();
				NormalizedCaptureRegion.A = (float)Config.UnscaledCaptureRegion.Max.Y / (float)UnscaledViewRect.Height();

				// Get a MID for drawing this frame and push the capture region into the shader parameter
				FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial = State->GetReusableMID(Config.HighResScreenshotCaptureRegionMaterial);
				FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial->SetVectorParameterValue(ParamName, NormalizedCaptureRegion);
			}
		}
	}
#endif // WITH_EDITOR

	if (Family->EngineShowFlags.ScreenPercentage)
	{
		FinalPostProcessSettings.ScreenPercentage = FMath::Clamp(FinalPostProcessSettings.ScreenPercentage, 1.f, 400.0f);
	}
	else
	{
		FinalPostProcessSettings.ScreenPercentage = 100;
	}

	check(VerifyMembersChecks());
}

void FSceneView::ConfigureBufferVisualizationSettings()
{
	bool bBufferDumpingRequired = (FScreenshotRequest::IsScreenshotRequested() || GIsHighResScreenshot || GIsDumpingMovie);
	bool bVisualizationRequired = Family->EngineShowFlags.VisualizeBuffer;

	if (bVisualizationRequired || bBufferDumpingRequired)
	{
		FinalPostProcessSettings.bBufferVisualizationDumpRequired = bBufferDumpingRequired;
		FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Empty();

		if (bBufferDumpingRequired)
		{
			FinalPostProcessSettings.BufferVisualizationDumpBaseFilename = FPaths::GetBaseFilename(FScreenshotRequest::GetFilename(), false);
		}

		// Get the list of requested buffers from the console
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationOverviewTargets"));
		FString SelectedMaterialNames = CVar->GetString();

		FBufferVisualizationData& BufferVisualizationData = GetBufferVisualizationData();

		if (BufferVisualizationData.IsDifferentToCurrentOverviewMaterialNames(SelectedMaterialNames))
		{
			FString Left, Right;

			// Update our record of the list of materials we've been asked to display
			BufferVisualizationData.SetCurrentOverviewMaterialNames(SelectedMaterialNames);
			BufferVisualizationData.GetOverviewMaterials().Empty();

			// Extract each material name from the comma separated string
			while (SelectedMaterialNames.Len())
			{
				// Detect last entry in the list
				if (!SelectedMaterialNames.Split(TEXT(","), &Left, &Right))
				{
					Left = SelectedMaterialNames;
					Right = FString();
				}

				// Lookup this material from the list that was parsed out of the global ini file
				Left.TrimStartInline();
				UMaterialInterface* Material = BufferVisualizationData.GetMaterial(*Left);

				if (Material == NULL && Left.Len() > 0)
				{
					UE_LOG(LogBufferVisualization, Warning, TEXT("Unknown material '%s'"), *Left);
				}

				// Add this material into the material list in the post processing settings so that the render thread
				// can pick them up and draw them into the on-screen tiles
				BufferVisualizationData.GetOverviewMaterials().Add(Material);

				SelectedMaterialNames = Right;
			}
		}

		// Copy current material list into settings material list
		for (TArray<UMaterialInterface*>::TConstIterator It = BufferVisualizationData.GetOverviewMaterials().CreateConstIterator(); It; ++It)
		{
			FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(*It);
		}
	}
}

#if !(UE_BUILD_SHIPPING)

void FSceneView::ConfigureVisualizeCalibrationSettings()
{
	const URendererSettings* Settings = GetDefault<URendererSettings>();
	check(Settings);
	
	auto ConfigureCalibrationSettings = [](const FSoftObjectPath& InPath, UMaterialInterface*& OutMaterialInterface, FName& OutMaterialName)
	{
		if (InPath.IsValid())
		{
			if (UMaterial* Material = Cast<UMaterial>(InPath.TryLoad()))
			{
				OutMaterialInterface = Material;
				OutMaterialName = *Material->GetPathName();
			}
			else
			{
				UE_LOG(LogBufferVisualization, Warning, TEXT("Error loading material '%s'"), *InPath.ToString());
				OutMaterialInterface = nullptr;
				OutMaterialName = NAME_None;
			}
		}
	};

	if (Family->EngineShowFlags.VisualizeCalibrationColor)
	{
		ConfigureCalibrationSettings(Settings->VisualizeCalibrationColorMaterialPath, FinalPostProcessSettings.VisualizeCalibrationColorMaterial, CurrentVisualizeCalibrationColorMaterialName);
	}
	else if (Family->EngineShowFlags.VisualizeCalibrationGrayscale)
	{
		ConfigureCalibrationSettings(Settings->VisualizeCalibrationGrayscaleMaterialPath, FinalPostProcessSettings.VisualizeCalibrationGrayscaleMaterial, CurrentVisualizeCalibrationGrayscaleMaterialName);
	}
	else if (Family->EngineShowFlags.VisualizeCalibrationCustom)
	{
		ConfigureCalibrationSettings(Settings->VisualizeCalibrationCustomMaterialPath, FinalPostProcessSettings.VisualizeCalibrationCustomMaterial, CurrentVisualizeCalibrationCustomMaterialName);
	}
}

#endif // #if !(UE_BUILD_SHIPPING)

EShaderPlatform FSceneView::GetShaderPlatform() const
{
	return GShaderPlatformForFeatureLevel[GetFeatureLevel()];
}

bool FSceneView::IsInstancedStereoPass() const
{
	return bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*this) && IStereoRendering::IsAPrimaryView(*this);
}

void FSceneView::SetupViewRectUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters,
	const FIntPoint& BufferSize,
	const FIntRect& EffectiveViewRect,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices) const
{
	checkfSlow(EffectiveViewRect.Area() > 0, TEXT("Invalid-size EffectiveViewRect passed to CreateUniformBufferParameters [%d * %d]."), EffectiveViewRect.Width(), EffectiveViewRect.Height());
	ensureMsgf((BufferSize.X > 0 && BufferSize.Y > 0), TEXT("Invalid-size BufferSize passed to CreateUniformBufferParameters [%d * %d]."), BufferSize.X, BufferSize.Y);

	ViewUniformShaderParameters.ViewRectMin = FVector4(EffectiveViewRect.Min.X, EffectiveViewRect.Min.Y, 0.0f, 0.0f);
	ViewUniformShaderParameters.ViewSizeAndInvSize = FVector4(EffectiveViewRect.Width(), EffectiveViewRect.Height(), 1.0f / float(EffectiveViewRect.Width()), 1.0f / float(EffectiveViewRect.Height()));

	// The light probe ratio is only different during separate forward translucency when r.SeparateTranslucencyScreenPercentage != 100
	ViewUniformShaderParameters.LightProbeSizeRatioAndInvSizeRatio = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Calculate the vector used by shaders to convert clip space coordinates to texture space.
	const float InvBufferSizeX = 1.0f / BufferSize.X;
	const float InvBufferSizeY = 1.0f / BufferSize.Y;
	// to bring NDC (-1..1, 1..-1) into 0..1 UV for BufferSize textures
	const FVector4 ScreenPositionScaleBias(
		EffectiveViewRect.Width() * InvBufferSizeX / +2.0f,
		EffectiveViewRect.Height() * InvBufferSizeY / (-2.0f * GProjectionSignY),
		(EffectiveViewRect.Height() / 2.0f + EffectiveViewRect.Min.Y) * InvBufferSizeY,
		(EffectiveViewRect.Width() / 2.0f + EffectiveViewRect.Min.X) * InvBufferSizeX);

	ViewUniformShaderParameters.ScreenPositionScaleBias = ScreenPositionScaleBias;

	ViewUniformShaderParameters.BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, InvBufferSizeX, InvBufferSizeY);
	ViewUniformShaderParameters.BufferBilinearUVMinMax = FVector4(
		InvBufferSizeX * (EffectiveViewRect.Min.X + 0.5),
		InvBufferSizeY * (EffectiveViewRect.Min.Y + 0.5),
		InvBufferSizeX * (EffectiveViewRect.Max.X - 0.5),
		InvBufferSizeY * (EffectiveViewRect.Max.Y - 0.5));

	/* Texture Level-of-Detail Strategies for Real-Time Ray Tracing https://developer.nvidia.com/raytracinggems Equation 20 */
	float RadFOV = (PI / 180.0f) * FOV;
	ViewUniformShaderParameters.EyeToPixelSpreadAngle = FPlatformMath::Atan((2.0f * FPlatformMath::Tan(RadFOV * 0.5f)) / BufferSize.Y);

	ViewUniformShaderParameters.MotionBlurNormalizedToPixel = FinalPostProcessSettings.MotionBlurMax * EffectiveViewRect.Width() / 100.0f;

	{
		// setup a matrix to transform float4(SvPosition.xyz,1) directly to TranslatedWorld (quality, performance as we don't need to convert or use interpolator)

		//	new_xy = (xy - ViewRectMin.xy) * ViewSizeAndInvSize.zw * float2(2,-2) + float2(-1, 1);

		//  transformed into one MAD:  new_xy = xy * ViewSizeAndInvSize.zw * float2(2,-2)      +       (-ViewRectMin.xy) * ViewSizeAndInvSize.zw * float2(2,-2) + float2(-1, 1);

		float Mx = 2.0f * ViewUniformShaderParameters.ViewSizeAndInvSize.Z;
		float My = -2.0f * ViewUniformShaderParameters.ViewSizeAndInvSize.W;
		float Ax = -1.0f - 2.0f * EffectiveViewRect.Min.X * ViewUniformShaderParameters.ViewSizeAndInvSize.Z;
		float Ay = 1.0f + 2.0f * EffectiveViewRect.Min.Y * ViewUniformShaderParameters.ViewSizeAndInvSize.W;

		// http://stackoverflow.com/questions/9010546/java-transformation-matrix-operations

		ViewUniformShaderParameters.SVPositionToTranslatedWorld =
			FMatrix(FPlane(Mx, 0, 0, 0),
				FPlane(0, My, 0, 0),
				FPlane(0, 0, 1, 0),
				FPlane(Ax, Ay, 0, 1)) * InViewMatrices.GetInvTranslatedViewProjectionMatrix();
	}

	// is getting clamped in the shader to a value larger than 0 (we don't want the triangles to disappear)
	ViewUniformShaderParameters.AdaptiveTessellationFactor = 0.0f;

	if(Family->EngineShowFlags.Tessellation)
	{
		// CVar setting is pixels/tri which is nice and intuitive.  But we want pixels/tessellated edge.  So use a heuristic.
		float TessellationAdaptivePixelsPerEdge = FMath::Sqrt(2.f * CVarTessellationAdaptivePixelsPerTriangle.GetValueOnRenderThread());

		ViewUniformShaderParameters.AdaptiveTessellationFactor = 0.5f * InViewMatrices.GetProjectionMatrix().M[1][1] * float(EffectiveViewRect.Height()) / TessellationAdaptivePixelsPerEdge;
	}

	// Compute coefficients which takes a screen UV and converts to Viewspace.xy / ViewZ
	float InvTanHalfFov = InViewMatrices.GetProjectionMatrix().M[0][0];
	float Ratio = UnscaledViewRect.Width() / (float)UnscaledViewRect.Height();

	float InvFovFixX = 1.0f / (InvTanHalfFov);
	float InvFovFixY = 1.0f / (Ratio * InvTanHalfFov);

	ViewUniformShaderParameters.ScreenToViewSpace.X = BufferSize.X * ViewUniformShaderParameters.ViewSizeAndInvSize.Z * 2 * InvFovFixX;
	ViewUniformShaderParameters.ScreenToViewSpace.Y = BufferSize.Y * ViewUniformShaderParameters.ViewSizeAndInvSize.W  * -2 * InvFovFixY;

	ViewUniformShaderParameters.ScreenToViewSpace.Z = -((ViewUniformShaderParameters.ViewRectMin.X * ViewUniformShaderParameters.ViewSizeAndInvSize.Z * 2 * InvFovFixX) + InvFovFixX);
	ViewUniformShaderParameters.ScreenToViewSpace.W = (ViewUniformShaderParameters.ViewRectMin.Y * ViewUniformShaderParameters.ViewSizeAndInvSize.W * 2 * InvFovFixY) + InvFovFixY;
}

void FSceneView::SetupCommonViewUniformBufferParameters(
	FViewUniformShaderParameters& ViewUniformShaderParameters,
	const FIntPoint& BufferSize,
	int32 NumMSAASamples,
	const FIntRect& EffectiveViewRect,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupCommonViewUniformBufferParameters);
	FVector4 LocalDiffuseOverrideParameter = DiffuseOverrideParameter;
	FVector2D LocalRoughnessOverrideParameter = RoughnessOverrideParameter;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		// assuming we have no color in the multipliers
		float MinValue = LocalDiffuseOverrideParameter.X;
		float MaxValue = MinValue + LocalDiffuseOverrideParameter.W;

		float NewMinValue = FMath::Max(MinValue, CVarDiffuseColorMin.GetValueOnRenderThread());
		float NewMaxValue = FMath::Min(MaxValue, CVarDiffuseColorMax.GetValueOnRenderThread());

		LocalDiffuseOverrideParameter.X = LocalDiffuseOverrideParameter.Y = LocalDiffuseOverrideParameter.Z = NewMinValue;
		LocalDiffuseOverrideParameter.W = NewMaxValue - NewMinValue;
	}
	{
		float MinValue = LocalRoughnessOverrideParameter.X;
		float MaxValue = MinValue + LocalRoughnessOverrideParameter.Y;

		float NewMinValue = FMath::Max(MinValue, CVarRoughnessMin.GetValueOnRenderThread());
		float NewMaxValue = FMath::Min(MaxValue, CVarRoughnessMax.GetValueOnRenderThread());

		LocalRoughnessOverrideParameter.X = NewMinValue;
		LocalRoughnessOverrideParameter.Y = NewMaxValue - NewMinValue;
	}

#endif

	ViewUniformShaderParameters.NumSceneColorMSAASamples = NumMSAASamples;
	ViewUniformShaderParameters.ViewToTranslatedWorld = InViewMatrices.GetOverriddenInvTranslatedViewMatrix();
	ViewUniformShaderParameters.TranslatedWorldToClip = InViewMatrices.GetTranslatedViewProjectionMatrix();
	ViewUniformShaderParameters.WorldToClip = InViewMatrices.GetViewProjectionMatrix();
	ViewUniformShaderParameters.ClipToWorld = InViewMatrices.GetInvViewProjectionMatrix();
	ViewUniformShaderParameters.TranslatedWorldToView = InViewMatrices.GetOverriddenTranslatedViewMatrix();
	ViewUniformShaderParameters.TranslatedWorldToCameraView = InViewMatrices.GetTranslatedViewMatrix();
	ViewUniformShaderParameters.CameraViewToTranslatedWorld = InViewMatrices.GetInvTranslatedViewMatrix();
	ViewUniformShaderParameters.ViewToClip = InViewMatrices.GetProjectionMatrix();
	ViewUniformShaderParameters.ViewToClipNoAA = InViewMatrices.GetProjectionNoAAMatrix();
	ViewUniformShaderParameters.ClipToView = InViewMatrices.GetInvProjectionMatrix();
	ViewUniformShaderParameters.ClipToTranslatedWorld = InViewMatrices.GetInvTranslatedViewProjectionMatrix();
	ViewUniformShaderParameters.ViewForward = InViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(2);
	ViewUniformShaderParameters.ViewUp = InViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(1);
	ViewUniformShaderParameters.ViewRight = InViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(0);
	ViewUniformShaderParameters.HMDViewNoRollUp = InViewMatrices.GetHMDViewMatrixNoRoll().GetColumn(1);
	ViewUniformShaderParameters.HMDViewNoRollRight = InViewMatrices.GetHMDViewMatrixNoRoll().GetColumn(0);
	ViewUniformShaderParameters.InvDeviceZToWorldZTransform = InvDeviceZToWorldZTransform;
	ViewUniformShaderParameters.WorldViewOrigin = InViewMatrices.GetOverriddenInvTranslatedViewMatrix().TransformPosition(FVector(0)) - InViewMatrices.GetPreViewTranslation();
	ViewUniformShaderParameters.WorldCameraOrigin = InViewMatrices.GetViewOrigin();
	ViewUniformShaderParameters.TranslatedWorldCameraOrigin = InViewMatrices.GetViewOrigin() + InViewMatrices.GetPreViewTranslation();
	ViewUniformShaderParameters.PreViewTranslation = InViewMatrices.GetPreViewTranslation();
	ViewUniformShaderParameters.PrevProjection = InPrevViewMatrices.GetProjectionMatrix();
	ViewUniformShaderParameters.PrevViewProj = InPrevViewMatrices.GetViewProjectionMatrix();
	ViewUniformShaderParameters.PrevViewRotationProj = InPrevViewMatrices.ComputeViewRotationProjectionMatrix();
	ViewUniformShaderParameters.PrevViewToClip = InPrevViewMatrices.GetProjectionMatrix();
	ViewUniformShaderParameters.PrevClipToView = InPrevViewMatrices.GetInvProjectionMatrix();
	ViewUniformShaderParameters.PrevTranslatedWorldToClip = InPrevViewMatrices.GetTranslatedViewProjectionMatrix();
	// EffectiveTranslatedViewMatrix != InViewMatrices.TranslatedViewMatrix in the shadow pass
	// and we don't have EffectiveTranslatedViewMatrix for the previous frame to set up PrevTranslatedWorldToView
	// but that is fine to set up PrevTranslatedWorldToView as same as PrevTranslatedWorldToCameraView
	// since the shadow pass doesn't require previous frame computation.
	ViewUniformShaderParameters.PrevTranslatedWorldToView = InPrevViewMatrices.GetTranslatedViewMatrix();
	ViewUniformShaderParameters.PrevViewToTranslatedWorld = InPrevViewMatrices.GetInvTranslatedViewMatrix();
	ViewUniformShaderParameters.PrevTranslatedWorldToCameraView = InPrevViewMatrices.GetTranslatedViewMatrix();
	ViewUniformShaderParameters.PrevCameraViewToTranslatedWorld = InPrevViewMatrices.GetInvTranslatedViewMatrix();
	ViewUniformShaderParameters.PrevWorldCameraOrigin = InPrevViewMatrices.GetViewOrigin();
	// previous view world origin is going to be needed only in the base pass or shadow pass
	// therefore is same as previous camera world origin.
	ViewUniformShaderParameters.PrevWorldViewOrigin = ViewUniformShaderParameters.PrevWorldCameraOrigin;
	ViewUniformShaderParameters.PrevPreViewTranslation = InPrevViewMatrices.GetPreViewTranslation();
	// can be optimized
	ViewUniformShaderParameters.PrevInvViewProj = InPrevViewMatrices.GetInvViewProjectionMatrix();
	ViewUniformShaderParameters.GlobalClippingPlane = FVector4(GlobalClippingPlane.X, GlobalClippingPlane.Y, GlobalClippingPlane.Z, -GlobalClippingPlane.W);

	ViewUniformShaderParameters.FieldOfViewWideAngles = 2.f * InViewMatrices.ComputeHalfFieldOfViewPerAxis();
	ViewUniformShaderParameters.PrevFieldOfViewWideAngles = 2.f * InPrevViewMatrices.ComputeHalfFieldOfViewPerAxis();
	ViewUniformShaderParameters.DiffuseOverrideParameter = LocalDiffuseOverrideParameter;
	ViewUniformShaderParameters.SpecularOverrideParameter = SpecularOverrideParameter;
	ViewUniformShaderParameters.NormalOverrideParameter = NormalOverrideParameter;
	ViewUniformShaderParameters.RoughnessOverrideParameter = LocalRoughnessOverrideParameter;
	ViewUniformShaderParameters.PrevFrameGameTime = Family->CurrentWorldTime - Family->DeltaWorldTime;
	ViewUniformShaderParameters.PrevFrameRealTime = Family->CurrentRealTime  - Family->DeltaWorldTime;
	ViewUniformShaderParameters.WorldCameraMovementSinceLastFrame = InViewMatrices.GetViewOrigin() - InPrevViewMatrices.GetViewOrigin();
	ViewUniformShaderParameters.CullingSign = bReverseCulling ? -1.0f : 1.0f;
	ViewUniformShaderParameters.NearPlane = GNearClippingPlane;
	ViewUniformShaderParameters.MaterialTextureMipBias = 0.0f;
	ViewUniformShaderParameters.MaterialTextureDerivativeMultiply = 1.0f;

	ViewUniformShaderParameters.bCheckerboardSubsurfaceProfileRendering = 0;

	ViewUniformShaderParameters.ScreenToWorld = FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[2][2], 1),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[3][2], 0))
		* InViewMatrices.GetInvViewProjectionMatrix();

	ViewUniformShaderParameters.ScreenToTranslatedWorld = FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[2][2], 1),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[3][2], 0))
		* InViewMatrices.GetInvTranslatedViewProjectionMatrix();

	ViewUniformShaderParameters.MobileMultiviewShadowTransform = FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, InViewMatrices.GetProjectionMatrix().M[2][2], 1),
		FPlane(0, 0, InViewMatrices.GetProjectionMatrix().M[3][2], 0)) *
		InViewMatrices.GetInvTranslatedViewProjectionMatrix() *
		FTranslationMatrix(-InViewMatrices.GetPreViewTranslation());

	ViewUniformShaderParameters.PrevScreenToTranslatedWorld = FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[2][2], 1),
		FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[3][2], 0))
		* InPrevViewMatrices.GetInvTranslatedViewProjectionMatrix();

	FVector DeltaTranslation = InPrevViewMatrices.GetPreViewTranslation() - InViewMatrices.GetPreViewTranslation();
	FMatrix InvViewProj = InViewMatrices.ComputeInvProjectionNoAAMatrix() * InViewMatrices.GetTranslatedViewMatrix().GetTransposed();
	FMatrix PrevViewProj = FTranslationMatrix(DeltaTranslation) * InPrevViewMatrices.GetTranslatedViewMatrix() * InPrevViewMatrices.ComputeProjectionNoAAMatrix();

	ViewUniformShaderParameters.ClipToPrevClip = InvViewProj * PrevViewProj;
	ViewUniformShaderParameters.TemporalAAJitter = FVector4(
		InViewMatrices.GetTemporalAAJitter().X,		InViewMatrices.GetTemporalAAJitter().Y,
		InPrevViewMatrices.GetTemporalAAJitter().X, InPrevViewMatrices.GetTemporalAAJitter().Y );

	ViewUniformShaderParameters.DebugViewModeMask = Family->UseDebugViewPS() ? 1 : 0;
	ViewUniformShaderParameters.UnlitViewmodeMask = !Family->EngineShowFlags.Lighting ? 1 : 0;
	ViewUniformShaderParameters.OutOfBoundsMask = Family->EngineShowFlags.VisualizeOutOfBoundsPixels ? 1 : 0;

	ViewUniformShaderParameters.GameTime = Family->CurrentWorldTime;
	ViewUniformShaderParameters.RealTime = Family->CurrentRealTime;
	ViewUniformShaderParameters.DeltaTime = Family->DeltaWorldTime;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static FIntPoint LockedCursorPos = CursorPos;
	if (CVarFreezeMouseCursor.GetValueOnRenderThread() == 0 && CursorPos.X >= 0 && CursorPos.Y >= 0)
	{
		LockedCursorPos = CursorPos;
	}
	ViewUniformShaderParameters.CursorPosition = LockedCursorPos;
#endif

	ViewUniformShaderParameters.Random = FMath::Rand();
	ViewUniformShaderParameters.FrameNumber = Family->FrameNumber;

	ViewUniformShaderParameters.CameraCut = bCameraCut ? 1 : 0;

	ViewUniformShaderParameters.MinRoughness = FMath::Clamp(CVarGlobalMinRoughnessOverride.GetValueOnRenderThread(), 0.02f, 1.0f);

	//to tail call keep the order and number of parameters of the caller function
	SetupViewRectUniformBufferParameters(ViewUniformShaderParameters, BufferSize, EffectiveViewRect, InViewMatrices, InPrevViewMatrices);
}

bool FSceneView::HasValidEyeAdaptationTexture() const
{
	if (EyeAdaptationViewState)
	{
		return EyeAdaptationViewState->HasValidEyeAdaptationTexture();
	}
	return false;
}

bool FSceneView::HasValidEyeAdaptationBuffer() const
{
	if (EyeAdaptationViewState)
	{
		return EyeAdaptationViewState->HasValidEyeAdaptationBuffer();
	}
	return false;
}

IPooledRenderTarget* FSceneView::GetEyeAdaptationTexture() const
{
	checkf(FeatureLevel > ERHIFeatureLevel::ES3_1, TEXT("SM5 and above use RenderTarget for read back"));
	if (EyeAdaptationViewState)
	{
		return EyeAdaptationViewState->GetCurrentEyeAdaptationTexture();
	}
	return nullptr;
}

const FExposureBufferData* FSceneView::GetEyeAdaptationBuffer() const
{
	checkf(FeatureLevel == ERHIFeatureLevel::ES3_1, TEXT("ES3_1 use RWBuffer for read back"));
	if (EyeAdaptationViewState)
	{
		return EyeAdaptationViewState->GetCurrentEyeAdaptationBuffer();
	}
	return nullptr;
}

FSceneViewFamily::FSceneViewFamily(const ConstructionValues& CVS)
	:
	ViewMode(VMI_Lit),
	RenderTarget(CVS.RenderTarget),
	Scene(CVS.Scene),
	EngineShowFlags(CVS.EngineShowFlags),
	CurrentWorldTime(CVS.CurrentWorldTime),
	DeltaWorldTime(CVS.DeltaWorldTime),
	CurrentRealTime(CVS.CurrentRealTime),
	FrameNumber(UINT_MAX),
	bAdditionalViewFamily(CVS.bAdditionalViewFamily),
	bRealtimeUpdate(CVS.bRealtimeUpdate),
	bDeferClear(CVS.bDeferClear),
	bResolveScene(CVS.bResolveScene),
	bMultiGPUForkAndJoin(false),
	SceneCaptureSource(SCS_FinalColorLDR),
	SceneCaptureCompositeMode(SCCM_Overwrite),
	bWorldIsPaused(false),
	bIsHDR(false),
	bRequireMultiView(false),
	GammaCorrection(CVS.GammaCorrection),
	SecondaryViewFraction(1.0f),
	SecondaryScreenPercentageMethod(ESecondaryScreenPercentageMethod::LowerPixelDensitySimulation),
	ScreenPercentageInterface(nullptr),
	TemporalUpscalerInterface(nullptr)
{
	// If we do not pass a valid scene pointer then SetWorldTimes must be called to initialized with valid times.
	ensure(CVS.bTimesSet);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	int32 Value = CVarRenderTimeFrozen.GetValueOnAnyThread();
	if(Value)
	{
		CurrentWorldTime = 0;
		CurrentRealTime = 0;
	}

	DebugViewShaderMode = ChooseDebugViewShaderMode();
	ViewModeParam = CVS.ViewModeParam;
	ViewModeParamName = CVS.ViewModeParamName;

	if (!AllowDebugViewShaderMode(DebugViewShaderMode, GetShaderPlatform(), GetFeatureLevel()))
	{
		DebugViewShaderMode = DVSM_None;
	}
	bUsedDebugViewVSDSHS = DebugViewShaderMode != DVSM_None && AllowDebugViewVSDSHS(GetShaderPlatform());
#endif

#if !WITH_EDITOR
	check(!EngineShowFlags.StationaryLightOverlap);
#else

	// instead of checking IsGameWorld on rendering thread to see if we allow this flag to be disabled
	// we force it on in the game thread.
	if(IsInGameThread() && Scene)
	{
		UWorld* World = Scene->GetWorld();

		if (World)
		{
			if (World->IsGameWorld())
			{
				EngineShowFlags.LOD = 1;
			}

			bWorldIsPaused = !World->IsCameraMoveable();
		}
	}

	LandscapeLODOverride = -1;
	bDrawBaseInfo = true;
	bNullifyWorldSpacePosition = false;
#endif

	// ScreenPercentage is not supported in ES 3.1 with MobileHDR = false. Disable show flag so to have it respected.
	const bool bIsMobileLDR = (GetFeatureLevel() <= ERHIFeatureLevel::ES3_1 && !IsMobileHDR());
	if (bIsMobileLDR)
	{
		EngineShowFlags.ScreenPercentage = false;
	}

	// TODO: Re-enable Mobile Multi-View on all platforms when all desktop XR plugins support it
#if (PLATFORM_HOLOLENS || PLATFORM_ANDROID || PLATFORM_LUMIN)
	if (GEngine && GEngine->IsStereoscopic3D())
	{
		static const auto MobileMultiViewCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
		const bool bSkipPostprocessing = !IsMobileHDR();
		const bool bUsingMobileRenderer = FSceneInterface::GetShadingPath(GetFeatureLevel()) == EShadingPath::Mobile;
		bRequireMultiView = (GSupportsMobileMultiView || GRHISupportsArrayIndexFromAnyShader) && bUsingMobileRenderer && bSkipPostprocessing && (MobileMultiViewCVar && MobileMultiViewCVar->GetValueOnAnyThread() != 0);
	}
#endif
}

FSceneViewFamily::~FSceneViewFamily()
{
	// If a screen percentage was given for the view family, delete it since any new copy of a view family will Fork it.
	if (ScreenPercentageInterface)
	{
		delete ScreenPercentageInterface;
	}
}

ERHIFeatureLevel::Type FSceneViewFamily::GetFeatureLevel() const
{
	if (Scene)
	{
		return Scene->GetFeatureLevel();
	}
	else
	{
		return GMaxRHIFeatureLevel;
	}
}

const FSceneView& FSceneViewFamily::GetStereoEyeView(const EStereoscopicPass Eye) const
{
	const int32 EyeIndex = static_cast<int32>(Eye);
	check(Views.Num() > 0 && Views.Num() >= EyeIndex);

	if (EyeIndex <= 1) // Mono or left eye
	{
		return *Views[0];
	}
	else if (EyeIndex == 2) // Right eye
	{
		return *Views[1];
	}
	else // For extra views
	{
		return *Views[EyeIndex - eSSP_RIGHT_EYE + 1];
	}
}

bool FSceneViewFamily::SupportsScreenPercentage() const
{
	EShadingPath ShadingPath = Scene->GetShadingPath();

	// The deferred shading renderer supports screen percentage when used normally
	if (Scene->GetShadingPath() == EShadingPath::Deferred)
	{
		return true;
	}

	// Mobile renderer does not support screen percentage with LDR.
	if ((GetFeatureLevel() <= ERHIFeatureLevel::ES3_1 && !IsMobileHDR()))
	{
		return false;
	}
	return true;
}

bool FSceneViewFamily::AllowTranslucencyAfterDOF() const
{
	static IConsoleVariable* CVarMobileMSAA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileMSAA"));
	const bool bMobileMSAA = CVarMobileMSAA ? (CVarMobileMSAA->GetInt() > 1) : false;

	return CVarAllowTranslucencyAfterDOF.GetValueOnRenderThread() != 0
		&& (GetFeatureLevel() > ERHIFeatureLevel::ES3_1 || (IsMobileHDR() && !bMobileMSAA)) // on <= ES3_1 separate translucency requires HDR on and MSAA off
	&& EngineShowFlags.PostProcessing // Used for reflection captures.
	&& !UseDebugViewPS()
	&& EngineShowFlags.SeparateTranslucency;
	// If not, translucency after DOF will be rendered in standard translucency.
}


FSceneViewFamilyContext::~FSceneViewFamilyContext()
{
	// Cleanup the views allocated for this view family.
	for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
	{
		delete Views[ViewIndex];
	}
}

#if RHI_RAYTRACING
void FSceneView::SetupRayTracedRendering()
{
	RayTracingRenderMode = ERayTracingRenderMode::Disabled;

	if (!IsRayTracingEnabled())
	{
		return;
	}

	const FEngineShowFlags& ShowFlags = Family->EngineShowFlags;

	if (ShowFlags.PathTracing)
	{
		RayTracingRenderMode = ERayTracingRenderMode::PathTracing;
	}	
	else if (ShowFlags.RayTracingDebug)
	{
		RayTracingRenderMode = ERayTracingRenderMode::RayTracingDebug;
	}
}
#endif // RHI_RAYTRACING

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

EDebugViewShaderMode FSceneViewFamily::ChooseDebugViewShaderMode() const
{
	if (EngineShowFlags.ShaderComplexity)
	{
		if (EngineShowFlags.QuadOverdraw)
		{
			return DVSM_QuadComplexity;
		}
		else if (EngineShowFlags.ShaderComplexityWithQuadOverdraw)
		{
			return DVSM_ShaderComplexityContainedQuadOverhead;
		}
		else
		{
			return DVSM_ShaderComplexity;
		}
	}
	else if (EngineShowFlags.PrimitiveDistanceAccuracy)
	{
		return DVSM_PrimitiveDistanceAccuracy;
	}
	else if (EngineShowFlags.MeshUVDensityAccuracy)
	{
		return DVSM_MeshUVDensityAccuracy;
	}
	else if (EngineShowFlags.OutputMaterialTextureScales) // Test before accuracy is set since accuracy could also be set.
	{
		return DVSM_OutputMaterialTextureScales;
	}
	else if (EngineShowFlags.MaterialTextureScaleAccuracy)
	{
		return DVSM_MaterialTextureScaleAccuracy;
	}
	else if (EngineShowFlags.RequiredTextureResolution)
	{
		return DVSM_RequiredTextureResolution;
	}
	else if (EngineShowFlags.RayTracingDebug)
	{
		return DVSM_RayTracingDebug;
	}
	else if (EngineShowFlags.LODColoration || EngineShowFlags.HLODColoration)
	{
		return DVSM_LODColoration;
	}
	return DVSM_None;
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
