// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Renderer.cpp: Renderer module implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/CoreMisc.h"
#include "Stats/Stats.h"
#include "Modules/ModuleManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "EngineDefines.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "PostProcess/RenderTargetPool.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneCore.h"
#include "SceneHitProxyRendering.h"
#include "SceneRendering.h"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "TranslucentRendering.h"
#include "RendererModule.h"
#include "GPUBenchmark.h"
#include "SystemSettings.h"

DEFINE_LOG_CATEGORY(LogRenderer);

IMPLEMENT_MODULE(FRendererModule, Renderer);

#if !IS_MONOLITHIC
	// visual studio cannot find cross dll data for visualizers
	// thus as a workaround for now, copy and paste this into every module
	// where we need to visualize SystemSettings
	FSystemSettings* GSystemSettingsForVisualizers = &GSystemSettings;
#endif

// Dummy Reflection capture uniform buffer for translucent tile mesh rendering
class FDummyReflectionCaptureUniformBuffer : public TUniformBuffer<FReflectionCaptureShaderData>
{
	typedef TUniformBuffer< FReflectionCaptureShaderData > Super;
	
public:
	virtual void InitDynamicRHI() override
	{
		FReflectionCaptureShaderData DummyPositionsBuffer;
		FMemory::Memzero(DummyPositionsBuffer);
		SetContentsNoUpdate(DummyPositionsBuffer);
		Super::InitDynamicRHI();
	}
};
static TGlobalResource< FDummyReflectionCaptureUniformBuffer > GDummyReflectionCaptureUniformBuffer;

void FRendererModule::ReallocateSceneRenderTargets()
{
	FLightPrimitiveInteraction::InitializeMemoryPool();
	FSceneRenderTargets::GetGlobalUnsafe().UpdateRHI();
}

void FRendererModule::SceneRenderTargetsSetBufferSize(uint32 SizeX, uint32 SizeY)
{
	FSceneRenderTargets::GetGlobalUnsafe().SetBufferSize(SizeX, SizeY);
	FSceneRenderTargets::GetGlobalUnsafe().UpdateRHI();
}

void FRendererModule::InitializeSystemTextures(FRHICommandListImmediate& RHICmdList)
{
	GSystemTextures.InitializeTextures(RHICmdList, GMaxRHIFeatureLevel);
}

void FRendererModule::DrawTileMesh(FRHICommandListImmediate& RHICmdList, FDrawingPolicyRenderState& DrawRenderState, const FSceneView& SceneView, const FMeshBatch& Mesh, bool bIsHitTesting, const FHitProxyId& HitProxyId)
{
	if (!GUsingNullRHI)
	{
		// Create an FViewInfo so we can initialize its RHI resources
		//@todo - reuse this view for multiple tiles, this is going to be slow for each tile
		FViewInfo View(&SceneView);
		View.ViewRect = View.UnscaledViewRect;
		
		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

		//Apply the minimal forward lighting resources
		extern FForwardLightingViewResources* GetMinimalDummyForwardLightingResources();
		View.ForwardLightingResources = GetMinimalDummyForwardLightingResources();

		View.InitRHIResources();
		DrawRenderState.SetViewUniformBuffer(View.ViewUniformBuffer);

		const auto FeatureLevel = View.GetFeatureLevel();

		if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
		{
			View.MobileDirectionalLightUniformBuffers[0] = TUniformBufferRef<FMobileDirectionalLightShaderParameters>::CreateUniformBufferImmediate(FMobileDirectionalLightShaderParameters(), UniformBuffer_SingleFrame);
		}
		
		const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel);

		//get the blend mode of the material
		const EBlendMode MaterialBlendMode = Material->GetBlendMode();

		GSystemTextures.InitializeTextures(RHICmdList, FeatureLevel);

		// handle translucent material blend modes, not relevant in MaterialTexCoordScalesAnalysis since it outputs the scales.
		if (IsTranslucentBlendMode(MaterialBlendMode) && View.Family->GetDebugViewShaderMode() != DVSM_OutputMaterialTextureScales)
		{
			if (FeatureLevel >= ERHIFeatureLevel::SM4)
			{
				// Crash fix - reflection capture shader parameter is bound but we have no buffer during Build Texture Streaming
				if(!View.ReflectionCaptureUniformBuffer.IsValid())
				{
					View.ReflectionCaptureUniformBuffer = GDummyReflectionCaptureUniformBuffer;
				}
			
				TUniformBufferRef<FTranslucentBasePassUniformParameters> BasePassUniformBuffer;
				CreateTranslucentBasePassUniformBuffer(RHICmdList, View, nullptr, ESceneTextureSetupMode::None, BasePassUniformBuffer);
				DrawRenderState.SetPassUniformBuffer(BasePassUniformBuffer);
				FTranslucencyDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, FTranslucencyDrawingPolicyFactory::ContextType(nullptr, ETranslucencyPass::TPT_AllTranslucency, true), Mesh, false, DrawRenderState, NULL, HitProxyId);
			}
			else
			{
				TUniformBufferRef<FMobileBasePassUniformParameters> BasePassUniformBuffer;
				CreateMobileBasePassUniformBuffer(RHICmdList, View, false, BasePassUniformBuffer);
				DrawRenderState.SetPassUniformBuffer(BasePassUniformBuffer);
				FMobileTranslucencyDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, FMobileTranslucencyDrawingPolicyFactory::ContextType(ETranslucencyPass::TPT_AllTranslucency), Mesh, false, DrawRenderState, NULL, HitProxyId);
			}
		}
		// handle opaque materials
		else
		{
			// make sure we are doing opaque drawing
			DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());

			// draw the mesh
			if (bIsHitTesting)
			{
				FHitProxyDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, FHitProxyDrawingPolicyFactory::ContextType(), Mesh, false, DrawRenderState, NULL, HitProxyId);
			}
			else
			{
				if (FeatureLevel >= ERHIFeatureLevel::SM4)
				{
					TUniformBufferRef<FOpaqueBasePassUniformParameters> BasePassUniformBuffer;
					CreateOpaqueBasePassUniformBuffer(RHICmdList, View, nullptr, BasePassUniformBuffer);
					DrawRenderState.SetPassUniformBuffer(BasePassUniformBuffer);
					FBasePassOpaqueDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, FBasePassOpaqueDrawingPolicyFactory::ContextType(), Mesh, false, DrawRenderState, NULL, HitProxyId);
				}
				else
				{
					TUniformBufferRef<FMobileBasePassUniformParameters> BasePassUniformBuffer;
					CreateMobileBasePassUniformBuffer(RHICmdList, View, false, BasePassUniformBuffer);
					DrawRenderState.SetPassUniformBuffer(BasePassUniformBuffer);
					FMobileBasePassOpaqueDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, FMobileBasePassOpaqueDrawingPolicyFactory::ContextType(), Mesh, false, DrawRenderState, NULL, HitProxyId);
				}
			}
		}	
	}
}

void FRendererModule::RenderTargetPoolFindFreeElement(FRHICommandListImmediate& RHICmdList, const FPooledRenderTargetDesc& Desc, TRefCountPtr<IPooledRenderTarget> &Out, const TCHAR* InDebugName)
{
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Out, InDebugName);
}

void FRendererModule::TickRenderTargetPool()
{
	GRenderTargetPool.TickPoolElements();
}

void FRendererModule::DebugLogOnCrash()
{
	GRenderTargetPool.VisualizeTexture.SortOrder = 1;
	GRenderTargetPool.VisualizeTexture.bFullList = true;
	GRenderTargetPool.VisualizeTexture.DebugLog(false);
	
	GEngine->Exec(NULL, TEXT("rhi.DumpMemory"), *GLog);

	// execute on main thread
	{
		struct FTest
		{
			void Thread()
			{
				GEngine->Exec(NULL, TEXT("Mem FromReport"), *GLog);
			}
		} Test;

		DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.DumpDataAfterCrash"),
			STAT_FSimpleDelegateGraphTask_DumpDataAfterCrash,
			STATGROUP_TaskGraphTasks);

		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateRaw(&Test, &FTest::Thread),
			GET_STATID(STAT_FSimpleDelegateGraphTask_DumpDataAfterCrash), nullptr, ENamedThreads::GameThread
		);
	}
}

void FRendererModule::GPUBenchmark(FSynthBenchmarkResults& InOut, float WorkScale)
{
	check(IsInGameThread());

	FSceneViewInitOptions ViewInitOptions;
	FIntRect ViewRect(0, 0, 1, 1);

	FBox LevelBox(FVector(-WORLD_MAX), FVector(+WORLD_MAX));
	ViewInitOptions.SetViewRectangle(ViewRect);

	// Initialize Projection Matrix and ViewMatrix since FSceneView initialization is doing some math on them.
	// Otherwise it trips NaN checks.
	const FVector ViewPoint = LevelBox.GetCenter();
	ViewInitOptions.ViewOrigin = FVector(ViewPoint.X, ViewPoint.Y, 0.0f);
	ViewInitOptions.ViewRotationMatrix = FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, -1, 0, 0),
		FPlane(0, 0, -1, 0),
		FPlane(0, 0, 0, 1));

	const float ZOffset = WORLD_MAX;
	ViewInitOptions.ProjectionMatrix = FReversedZOrthoMatrix(
		LevelBox.GetSize().X / 2.f,
		LevelBox.GetSize().Y / 2.f,
		0.5f / ZOffset,
		ZOffset
		);

	FSceneView DummyView(ViewInitOptions);
	FlushRenderingCommands();
	ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
	  RendererGPUBenchmarkCommand,
	  FSceneView, DummyView, DummyView,
	  float, WorkScale, WorkScale,
	  FSynthBenchmarkResults&, InOut, InOut,
	{
		RendererGPUBenchmark(RHICmdList, InOut, DummyView, WorkScale);
	});
	FlushRenderingCommands();
}

void FRendererModule::QueryVisualizeTexture(FQueryVisualizeTexureInfo& Out)
{
	check(IsInGameThread());
	FlushRenderingCommands();

	GRenderTargetPool.VisualizeTexture.QueryInfo(Out);
}

static void VisualizeTextureExec( const TCHAR* Cmd, FOutputDevice &Ar )
{
	check(IsInGameThread());

	FlushRenderingCommands();

	uint32 ParameterCount = 0;

	// parse parameters
	for(;;)
	{
		FString Parameter = FParse::Token(Cmd, 0);

		if(Parameter.IsEmpty())
		{
			break;
		}

		// FULL flag
		if(Parameter == TEXT("fulllist") || Parameter == TEXT("full"))
		{
			GRenderTargetPool.VisualizeTexture.bFullList = true;
			// this one doesn't count as parameter so we can do "vis full"
			continue;
		}
		// SORT0 flag
		else if(Parameter == TEXT("sort0"))
		{
			GRenderTargetPool.VisualizeTexture.SortOrder = 0;
			// this one doesn't count as parameter so we can do "vis full"
			continue;
		}
		// SORT1 flag
		else if(Parameter == TEXT("sort1"))
		{
			GRenderTargetPool.VisualizeTexture.SortOrder = 1;
			// this one doesn't count as parameter so we can do "vis full"
			continue;
		}
		else if(ParameterCount == 0)
		{
			// Init
			GRenderTargetPool.VisualizeTexture.RGBMul = 1;
			GRenderTargetPool.VisualizeTexture.SingleChannelMul = 0.0f;
			GRenderTargetPool.VisualizeTexture.SingleChannel = -1;
			GRenderTargetPool.VisualizeTexture.AMul = 0;
			GRenderTargetPool.VisualizeTexture.UVInputMapping = 3;
			GRenderTargetPool.VisualizeTexture.Flags = 0;
			GRenderTargetPool.VisualizeTexture.Mode = 0;
			GRenderTargetPool.VisualizeTexture.CustomMip = 0;
			GRenderTargetPool.VisualizeTexture.ArrayIndex = 0;
			GRenderTargetPool.VisualizeTexture.bOutputStencil = false;

			// e.g. "VisualizeTexture Name" or "VisualizeTexture 5"
			bool bIsDigit = FChar::IsDigit(**Parameter);

			if (bIsDigit)
			{
				GRenderTargetPool.VisualizeTexture.Mode = FCString::Atoi(*Parameter);
			}

			if(!bIsDigit)
			{
				// the name was specified as string
				const TCHAR* AfterAt = *Parameter;

				while(*AfterAt != 0 && *AfterAt != TCHAR('@'))
				{
					++AfterAt;
				}

				if(*AfterAt == TCHAR('@'))
				{
					// user specified a reuse goal
					FString NameWithoutAt = Parameter.Left(AfterAt - *Parameter);
					GRenderTargetPool.VisualizeTexture.SetObserveTarget(*NameWithoutAt, FCString::Atoi(AfterAt + 1));
				}
				else
				{
					// we take the last one
					GRenderTargetPool.VisualizeTexture.SetObserveTarget(*Parameter);
				}
			}
			else
			{
				// the index was used
				GRenderTargetPool.VisualizeTexture.SetObserveTarget(TEXT(""));
			}
		}
		// GRenderTargetPoolInputMapping mode
		else if(Parameter == TEXT("uv0"))
		{
			GRenderTargetPool.VisualizeTexture.UVInputMapping = 0;
		}
		else if(Parameter == TEXT("uv1"))
		{
			GRenderTargetPool.VisualizeTexture.UVInputMapping = 1;
		}
		else if(Parameter == TEXT("uv2"))
		{
			GRenderTargetPool.VisualizeTexture.UVInputMapping = 2;
		}
		else if(Parameter == TEXT("pip"))
		{
			GRenderTargetPool.VisualizeTexture.UVInputMapping = 3;
		}
		// BMP flag
		else if(Parameter == TEXT("bmp"))
		{
			GRenderTargetPool.VisualizeTexture.bSaveBitmap = true;
		}
		else if (Parameter == TEXT("stencil"))
		{
			GRenderTargetPool.VisualizeTexture.bOutputStencil = true;
		}
		// saturate flag
		else if(Parameter == TEXT("frac"))
		{
			// default already covers this
		}
		// saturate flag
		else if(Parameter == TEXT("sat"))
		{
			GRenderTargetPool.VisualizeTexture.Flags |= 0x1;
		}
		// e.g. mip2 or mip0
		else if(Parameter.Left(3) == TEXT("mip"))
		{
			Parameter = Parameter.Right(Parameter.Len() - 3);
			GRenderTargetPool.VisualizeTexture.CustomMip = FCString::Atoi(*Parameter);
		}
		// e.g. [0] or [2]
		else if(Parameter.Left(5) == TEXT("index"))
		{
			Parameter = Parameter.Right(Parameter.Len() - 5);
			GRenderTargetPool.VisualizeTexture.ArrayIndex = FCString::Atoi(*Parameter);
		}
		// e.g. RGB*6, A, *22, /2.7, A*7
		else if(Parameter.Left(3) == TEXT("rgb")
			|| Parameter.Left(1) == TEXT("a")
			|| Parameter.Left(1) == TEXT("r")
			|| Parameter.Left(1) == TEXT("g")
			|| Parameter.Left(1) == TEXT("b")
			|| Parameter.Left(1) == TEXT("*")
			|| Parameter.Left(1) == TEXT("/"))
		{
			int SingleChannel = -1;

			if(Parameter.Left(3) == TEXT("rgb"))
			{
				Parameter = Parameter.Right(Parameter.Len() - 3);
			}
			else if(Parameter.Left(1) == TEXT("r")) SingleChannel = 0;
			else if(Parameter.Left(1) == TEXT("g")) SingleChannel = 1;
			else if(Parameter.Left(1) == TEXT("b")) SingleChannel = 2;
			else if(Parameter.Left(1) == TEXT("a")) SingleChannel = 3;
			if ( SingleChannel >= 0 )
			{
				Parameter = Parameter.Right(Parameter.Len() - 1);
				GRenderTargetPool.VisualizeTexture.SingleChannel = SingleChannel;
				GRenderTargetPool.VisualizeTexture.SingleChannelMul = 1;
				GRenderTargetPool.VisualizeTexture.RGBMul = 0;
			}

			float Mul = 1.0f;

			// * or /
			if(Parameter.Left(1) == TEXT("*"))
			{
				Parameter = Parameter.Right(Parameter.Len() - 1);
				Mul = FCString::Atof(*Parameter);
			}
			else if(Parameter.Left(1) == TEXT("/"))
			{
				Parameter = Parameter.Right(Parameter.Len() - 1);
				Mul = 1.0f / FCString::Atof(*Parameter);
			}
			GRenderTargetPool.VisualizeTexture.RGBMul *= Mul;
			GRenderTargetPool.VisualizeTexture.SingleChannelMul *= Mul;
			GRenderTargetPool.VisualizeTexture.AMul *= Mul;
		}
		else
		{
			Ar.Logf(TEXT("Error: parameter \"%s\" not recognized"), *Parameter);
		}

		++ParameterCount;
	}

	if(!ParameterCount)
	{
		// show help
		Ar.Logf(TEXT("VisualizeTexture/Vis <TextureId/CheckpointName> [<Mode>] [PIP/UV0/UV1/UV2] [BMP] [FRAC/SAT] [FULL]:"));

		Ar.Logf(TEXT("Mode (examples):"));
		Ar.Logf(TEXT("  RGB      = RGB in range 0..1 (default)"));
		Ar.Logf(TEXT("  *8       = RGB * 8"));
		Ar.Logf(TEXT("  A        = alpha channel in range 0..1"));
		Ar.Logf(TEXT("  R        = red channel in range 0..1"));
		Ar.Logf(TEXT("  G        = green channel in range 0..1"));
		Ar.Logf(TEXT("  B        = blue channel in range 0..1"));
		Ar.Logf(TEXT("  A*16     = Alpha * 16"));
		Ar.Logf(TEXT("  RGB/2    = RGB / 2"));
		Ar.Logf(TEXT("SubResource:"));
		Ar.Logf(TEXT("  MIP5     = Mip level 5 (0 is default)"));
		Ar.Logf(TEXT("  INDEX5   = Array Element 5 (0 is default)"));
		Ar.Logf(TEXT("InputMapping:"));
		Ar.Logf(TEXT("  PIP      = like UV1 but as picture in picture with normal rendering  (default)"));
		Ar.Logf(TEXT("  UV0      = UV in left top"));
		Ar.Logf(TEXT("  UV1      = full texture"));
		Ar.Logf(TEXT("  UV2      = pixel perfect centered"));
		Ar.Logf(TEXT("Flags:"));
		Ar.Logf(TEXT("  BMP      = save out bitmap to the screenshots folder (not on console, normalized)"));
		Ar.Logf(TEXT("STENCIL    = Stencil normally displayed in alpha channel of depth.  This option is used for BMP to get a stencil only BMP."));
		Ar.Logf(TEXT("  FRAC     = use frac() in shader (default)"));
		Ar.Logf(TEXT("  SAT      = use saturate() in shader"));
		Ar.Logf(TEXT("  FULLLIST = show full list, otherwise we hide some textures in the printout"));
		Ar.Logf(TEXT("  SORT0    = sort list by name"));
		Ar.Logf(TEXT("  SORT1    = show list by size"));
		Ar.Logf(TEXT("TextureId:"));
		Ar.Logf(TEXT("  0        = <off>"));

		GRenderTargetPool.VisualizeTexture.DebugLog(true);
	}
	//		Ar.Logf(TEXT("VisualizeTexture %d"), GRenderTargetPool.VisualizeTexture.Mode);
}

static bool RendererExec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (FParse::Command(&Cmd, TEXT("VisualizeTexture")) || FParse::Command(&Cmd, TEXT("Vis")))
	{
		VisualizeTextureExec(Cmd, Ar);
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("ShowMipLevels")))
	{
		extern bool GVisualizeMipLevels;
		GVisualizeMipLevels = !GVisualizeMipLevels;
		Ar.Logf( TEXT( "Showing mip levels: %s" ), GVisualizeMipLevels ? TEXT("ENABLED") : TEXT("DISABLED") );
		return true;
	}
	else if(FParse::Command(&Cmd,TEXT("DumpUnbuiltLightInteractions")))
	{
		InWorld->Scene->DumpUnbuiltLightInteractions(Ar);
		return true;
	}
#endif

	return false;
}

ICustomCulling* GCustomCullingImpl = nullptr;

void FRendererModule::RegisterCustomCullingImpl(ICustomCulling* impl)
{
	check(GCustomCullingImpl == nullptr);
	GCustomCullingImpl = impl;
}

void FRendererModule::UnregisterCustomCullingImpl(ICustomCulling* impl)
{
	check(GCustomCullingImpl == impl);
	GCustomCullingImpl = nullptr;
}

FStaticSelfRegisteringExec RendererExecRegistration(RendererExec);

void FRendererModule::ExecVisualizeTextureCmd( const FString& Cmd )
{
	// @todo: Find a nicer way to call this
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	VisualizeTextureExec(*Cmd, *GLog);
#endif
}
