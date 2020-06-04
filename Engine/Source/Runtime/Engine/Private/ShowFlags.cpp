// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShowFlags.cpp: Show Flag Definitions.
=============================================================================*/

#include "ShowFlags.h"
#include "SystemSettings.h"

static bool IsValidNameChar(TCHAR c)
{
	return (c >= (TCHAR)'a' && c <= (TCHAR)'z')
		|| (c >= (TCHAR)'A' && c <= (TCHAR)'Z')
		|| (c >= (TCHAR)'0' && c <= (TCHAR)'9')
		|| (c == (TCHAR)'_'); 
}

static void SkipWhiteSpace(const TCHAR*& p)
{
	for(;;)
	{
		if(IsValidNameChar(*p) || *p == (TCHAR)',' || *p == (TCHAR)'=')
		{
			return;
		}

		++p;
	}
}

// ----------------------------------------------------------------------------

FString FEngineShowFlags::ToString() const
{
	struct FIterSink
	{
		FIterSink(const FEngineShowFlags InEngineShowFlags) : EngineShowFlags(InEngineShowFlags)
		{
		}

		bool OnEngineShowFlag(uint32 InIndex, const FString& InName)
		{
			EShowFlagGroup Group = FEngineShowFlags::FindShowFlagGroup(*InName);
			if (Group != SFG_Transient)
			{
				if (!ret.IsEmpty())
				{
					ret += (TCHAR)',';
				}

				AddNameByIndex(InIndex, ret);

				ret += (TCHAR)'=';
				ret += EngineShowFlags.GetSingleFlag(InIndex) ? (TCHAR)'1' : (TCHAR)'0';
			}
			return true;
		}

		FString ret;
		const FEngineShowFlags EngineShowFlags;
	};

	FIterSink Sink(*this);

	IterateAllFlags(Sink);

	return Sink.ret;
}

bool FEngineShowFlags::SetFromString(const TCHAR* In)
{
	bool bError = false;

	const TCHAR* p = In;

	SkipWhiteSpace(p);

	while(*p)
	{
		FString Name;

		// jump over name
		while(IsValidNameChar(*p))
		{
			Name += *p++;
		}

		int32 Index = FindIndexByName(*Name);

		// true:set false:clear
		bool bSet = true;

		if(*p == (TCHAR)'=')
		{
			++p;
			if(*p == (TCHAR)'0')
			{
				bSet = false;
			}
			++p;
		}

		if(Index == INDEX_NONE)
		{
			// unknown name but we try to parse further
			bError = true;
		}
		else
		{
			SetSingleFlag(Index, bSet);
		}

		if(*p == (TCHAR)',')
		{
			++p;
		}
		else
		{
			// parse error;
			return false;
		}
	}

	return !bError;
}

bool FEngineShowFlags::GetSingleFlag(uint32 Index) const
{
	switch( Index )
	{
	#define SHOWFLAG_ALWAYS_ACCESSIBLE(a,...) case SF_##a: return a != 0;
	#include "ShowFlagsValues.inl"
	default:
		{
			checkNoEntry();
			return false;
		}
	}
}

void FEngineShowFlags::SetSingleFlag(uint32 Index, bool bSet)
{
	switch( Index )
	{
	#define SHOWFLAG_ALWAYS_ACCESSIBLE(a,...) case SF_##a: a = bSet?1:0; break;
	#if UE_BUILD_OPTIMIZED_SHOWFLAGS 
		#define SHOWFLAG_FIXED_IN_SHIPPING(v,a,...) case SF_##a: break;
	#endif
	#include "ShowFlagsValues.inl"
	default:
		{
			checkNoEntry();
		}
	}
}

int32 FEngineShowFlags::FindIndexByName(const TCHAR* Name, const TCHAR *CommaSeparatedNames)
{
	if(!Name)
	{
		// invalid input
		return INDEX_NONE;
	}

	if( CommaSeparatedNames == nullptr)
	{
		// search through all defined showflags.
		FString Search = Name;

		#define SHOWFLAG_ALWAYS_ACCESSIBLE(a,...) if(Search == PREPROCESSOR_TO_STRING(a)) { return (int32)SF_##a; }

		#include "ShowFlagsValues.inl"

		return INDEX_NONE;
	}
	else
	{
		// iterate through CommaSeparatedNames and test 'Name' equals one of them.
		struct FIterSink
		{
			FIterSink(const TCHAR* InName)
			{
				SearchName = InName;
				Ret = INDEX_NONE;
			}

			bool OnEngineShowFlag(uint32 InIndex, const FString& InName)
			{
				if (InName == SearchName)
				{
					Ret = InIndex;
					return false;
				}
				return true;
			}
			const TCHAR* SearchName;
			uint32 Ret;
		};
		FIterSink Sink(Name);
		IterateAllFlags(Sink, CommaSeparatedNames);
		return Sink.Ret;
	}
}

FString FEngineShowFlags::FindNameByIndex(uint32 InIndex)
{
	FString Name;

	#define SHOWFLAG_ALWAYS_ACCESSIBLE(a,...) case SF_##a: Name = PREPROCESSOR_TO_STRING(a); break;

	switch (InIndex)
	{
		#include "ShowFlagsValues.inl"
	default:
		break;
	}

	return Name;
}

void FEngineShowFlags::AddNameByIndex(uint32 InIndex, FString& Out)
{
	#define SHOWFLAG_ALWAYS_ACCESSIBLE(a,...) case SF_##a: Out += PREPROCESSOR_TO_STRING(a); break;
	switch (InIndex)
	{
		#include "ShowFlagsValues.inl"
		default:
			break;
	}
}

void ApplyViewMode(EViewModeIndex ViewModeIndex, bool bPerspective, FEngineShowFlags& EngineShowFlags)
{
	bool bPostProcessing = true;

	switch(ViewModeIndex)
	{
		case VMI_BrushWireframe:
			bPostProcessing = false;
			break;
		case VMI_Wireframe:
			bPostProcessing = false;
			break;
		case VMI_Unlit:
			bPostProcessing = false;
			break;
		default:
		case VMI_Lit: 
			bPostProcessing = true;
			break;
		case VMI_Lit_DetailLighting:	
			bPostProcessing = true;
			break;
		case VMI_LightingOnly:
			bPostProcessing = true;
			break;
		case VMI_LightComplexity:
			bPostProcessing = false;
			break;
		case VMI_ShaderComplexity:
		case VMI_QuadOverdraw:
		case VMI_ShaderComplexityWithQuadOverdraw:
		case VMI_PrimitiveDistanceAccuracy:
		case VMI_MeshUVDensityAccuracy:
		case VMI_MaterialTextureScaleAccuracy:
		case VMI_RequiredTextureResolution:
		case VMI_LODColoration:
		case VMI_HLODColoration:
			bPostProcessing = false;
			break;
		case VMI_StationaryLightOverlap:
			bPostProcessing = false;
			break;
		case VMI_LightmapDensity:
			bPostProcessing = false;
			break;
		case VMI_LitLightmapDensity:
			bPostProcessing = false;
			break;
		case VMI_VisualizeBuffer:
			bPostProcessing = true;
			break;
		case VMI_ReflectionOverride:
			bPostProcessing = true;
			break;
		case VMI_CollisionPawn:
		case VMI_CollisionVisibility:
			bPostProcessing = false;
			break;
		case VMI_RayTracingDebug:
			bPostProcessing = true;
			break;
		case VMI_PathTracing:
			bPostProcessing = true;
			break;
	}

	if(!bPerspective)
	{
		bPostProcessing = false;
	}

	// set the EngineShowFlags:

	// Assigning the new state like this ensures we always set the same variables (they depend on the view mode)
	// This is affecting the state of showflags - if the state can be changed by the user as well it should better be done in EngineShowFlagOverride

	EngineShowFlags.SetOverrideDiffuseAndSpecular(ViewModeIndex == VMI_Lit_DetailLighting);
	EngineShowFlags.SetLightingOnlyOverride(ViewModeIndex == VMI_LightingOnly);
	EngineShowFlags.SetReflectionOverride(ViewModeIndex == VMI_ReflectionOverride);
	EngineShowFlags.SetVisualizeBuffer(ViewModeIndex == VMI_VisualizeBuffer);
	EngineShowFlags.SetVisualizeLightCulling(ViewModeIndex == VMI_LightComplexity);
	EngineShowFlags.SetShaderComplexity(ViewModeIndex == VMI_ShaderComplexity || ViewModeIndex == VMI_QuadOverdraw || ViewModeIndex == VMI_ShaderComplexityWithQuadOverdraw);
	EngineShowFlags.SetQuadOverdraw(ViewModeIndex == VMI_QuadOverdraw);
	EngineShowFlags.SetShaderComplexityWithQuadOverdraw(ViewModeIndex == VMI_ShaderComplexityWithQuadOverdraw);
	EngineShowFlags.SetPrimitiveDistanceAccuracy(ViewModeIndex == VMI_PrimitiveDistanceAccuracy);
	EngineShowFlags.SetMeshUVDensityAccuracy(ViewModeIndex == VMI_MeshUVDensityAccuracy);
	EngineShowFlags.SetMaterialTextureScaleAccuracy(ViewModeIndex == VMI_MaterialTextureScaleAccuracy);
	EngineShowFlags.SetRequiredTextureResolution(ViewModeIndex == VMI_RequiredTextureResolution);
	EngineShowFlags.SetStationaryLightOverlap(ViewModeIndex == VMI_StationaryLightOverlap);
	EngineShowFlags.SetLightMapDensity(ViewModeIndex == VMI_LightmapDensity || ViewModeIndex == VMI_LitLightmapDensity);
	EngineShowFlags.SetPostProcessing(bPostProcessing);
	EngineShowFlags.SetBSPTriangles(ViewModeIndex != VMI_BrushWireframe && ViewModeIndex != VMI_LitLightmapDensity);
	EngineShowFlags.SetBrushes(ViewModeIndex == VMI_BrushWireframe);
	EngineShowFlags.SetWireframe(ViewModeIndex == VMI_Wireframe || ViewModeIndex == VMI_BrushWireframe);
	EngineShowFlags.SetCollisionPawn(ViewModeIndex == VMI_CollisionPawn);
	EngineShowFlags.SetCollisionVisibility(ViewModeIndex == VMI_CollisionVisibility);
	EngineShowFlags.SetLODColoration(ViewModeIndex == VMI_LODColoration);
	EngineShowFlags.SetHLODColoration(ViewModeIndex == VMI_HLODColoration);
	EngineShowFlags.SetRayTracingDebug(ViewModeIndex == VMI_RayTracingDebug);
	EngineShowFlags.SetPathTracing(ViewModeIndex == VMI_PathTracing);
}

void EngineShowFlagOverride(EShowFlagInitMode ShowFlagInitMode, EViewModeIndex ViewModeIndex, FEngineShowFlags& EngineShowFlags, bool bCanDisableTonemapper)
{
	if(ShowFlagInitMode == ESFIM_Game)
	{
		// editor only features
		EngineShowFlags.SetAudioRadius(false);
	}

	{
		// when taking a high resolution screenshot
		if (GIsHighResScreenshot)
		{
			static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HighResScreenshotDelay"));
			if(ICVar->GetValueOnGameThread() < 4)
			{
				// disabled as it requires multiple frames, AA can be done by downsampling, more control and better masking
				EngineShowFlags.TemporalAA = 0;
			}
			// no editor gizmos / selection
			EngineShowFlags.SetModeWidgets(false);
			EngineShowFlags.SetSelection(false);
			EngineShowFlags.SetSelectionOutline(false);
		}
	}

	{
		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LightFunctionQuality"));
		if(ICVar->GetValueOnGameThread() <= 0)
		{
			EngineShowFlags.LightFunctions = 0;
		}
	}

	{
		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.EyeAdaptationQuality"));
		if(ICVar->GetValueOnGameThread() <= 0)
		{
			EngineShowFlags.EyeAdaptation = 0;
		}
	}

	{
		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShadowQuality"));
		if(ICVar->GetValueOnGameThread() <= 0)
		{
			EngineShowFlags.DynamicShadows = 0;
		}
	}

	{
		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SkyLightingQuality"));
		if (ICVar->GetValueOnGameThread() <= 0)
		{
			EngineShowFlags.SkyLighting = 0;
		}
	}

	// some view modes want some features off or on (no state)
	{
		if( ViewModeIndex == VMI_BrushWireframe ||
			ViewModeIndex == VMI_Wireframe ||
			ViewModeIndex == VMI_Unlit ||
			ViewModeIndex == VMI_LightmapDensity ||
			ViewModeIndex == VMI_LitLightmapDensity)
		{
			EngineShowFlags.LightFunctions = 0;
		}

		if( ViewModeIndex == VMI_BrushWireframe ||
			ViewModeIndex == VMI_Wireframe ||
			ViewModeIndex == VMI_Unlit ||
			ViewModeIndex == VMI_ShaderComplexity ||
			ViewModeIndex == VMI_QuadOverdraw ||
			ViewModeIndex == VMI_ShaderComplexityWithQuadOverdraw ||
			ViewModeIndex == VMI_PrimitiveDistanceAccuracy ||
			ViewModeIndex == VMI_MeshUVDensityAccuracy ||
			ViewModeIndex == VMI_MaterialTextureScaleAccuracy ||
			ViewModeIndex == VMI_RequiredTextureResolution ||
			ViewModeIndex == VMI_LightmapDensity ||
			ViewModeIndex == VMI_LitLightmapDensity)
		{
			EngineShowFlags.DynamicShadows = 0;
		}

		if( ViewModeIndex == VMI_BrushWireframe)
		{
			EngineShowFlags.SetBrushes(true);
		}

		if (ViewModeIndex == VMI_Unlit)
		{
			EngineShowFlags.SetLighting(false);
			EngineShowFlags.Atmosphere = 0;
		}

		if( ViewModeIndex == VMI_Wireframe ||
			ViewModeIndex == VMI_BrushWireframe ||
			ViewModeIndex == VMI_StationaryLightOverlap ||
			ViewModeIndex == VMI_ShaderComplexity ||
			ViewModeIndex == VMI_QuadOverdraw ||
			ViewModeIndex == VMI_ShaderComplexityWithQuadOverdraw ||
			ViewModeIndex == VMI_PrimitiveDistanceAccuracy ||
			ViewModeIndex == VMI_MeshUVDensityAccuracy ||
			ViewModeIndex == VMI_MaterialTextureScaleAccuracy ||
			ViewModeIndex == VMI_RequiredTextureResolution ||
			ViewModeIndex == VMI_LODColoration ||
			ViewModeIndex == VMI_HLODColoration ||
			ViewModeIndex == VMI_LightmapDensity)
		{
			EngineShowFlags.SetLighting(false);
			EngineShowFlags.Atmosphere = 0;
			EngineShowFlags.Fog = 0;
		}

		if( ViewModeIndex == VMI_Lit ||
			ViewModeIndex == VMI_LightingOnly ||
			ViewModeIndex == VMI_LitLightmapDensity)
		{
			EngineShowFlags.SetLighting(true);
		}

		if( ViewModeIndex == VMI_LightingOnly ||
			ViewModeIndex == VMI_BrushWireframe ||
			ViewModeIndex == VMI_StationaryLightOverlap)
		{
			EngineShowFlags.SetMaterials(false);
		}

		if( ViewModeIndex == VMI_LightComplexity )
		{
			EngineShowFlags.Translucency = 0;
			EngineShowFlags.Fog = 0;
			EngineShowFlags.Atmosphere = 0;
		}

		if (ViewModeIndex == VMI_PrimitiveDistanceAccuracy ||
			ViewModeIndex == VMI_MeshUVDensityAccuracy ||
			ViewModeIndex == VMI_MaterialTextureScaleAccuracy || 
			ViewModeIndex == VMI_RequiredTextureResolution)
		{
			EngineShowFlags.Decals = 0; // Decals require the use of FDebugPSInLean.
			EngineShowFlags.Particles = 0; // FX are fully streamed.
			EngineShowFlags.Fog = 0;
		}

		if (ViewModeIndex == VMI_LODColoration || ViewModeIndex == VMI_HLODColoration)
		{
			EngineShowFlags.Decals = 0; // Decals require the use of FDebugPSInLean.
		}

		if (ViewModeIndex == VMI_PathTracing)
		{
			EngineShowFlags.SetPathTracing(true);
		}

		if (ViewModeIndex == VMI_RayTracingDebug)
		{
			EngineShowFlags.SetRayTracingDebug(true);
			EngineShowFlags.SetVisualizeHDR(false);
			EngineShowFlags.SetVisualizeMotionBlur(false);
			EngineShowFlags.SetDepthOfField(false);
			EngineShowFlags.SetPostProcessMaterial(false);

			if (bCanDisableTonemapper)
			{
				EngineShowFlags.SetTonemapper(false);
			}
		}
	}

	// disable AA in full screen GBuffer visualization
	if (bCanDisableTonemapper && EngineShowFlags.VisualizeBuffer)
	{
		EngineShowFlags.SetTonemapper(false);
	}

	if (EngineShowFlags.Bones)
	{
		// Disabling some post processing effects when debug rendering bones as they dont work properly together
		EngineShowFlags.TemporalAA = 0;
		EngineShowFlags.MotionBlur = 0;
		EngineShowFlags.Bloom = 0;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LimitRenderingFeatures"));
		if(ICVar)
		{
			 int Value = ICVar->GetValueOnGameThread();

#define DISABLE_ENGINE_SHOWFLAG(Name) if(Value-- >  0) EngineShowFlags.Set##Name(false);
			 DISABLE_ENGINE_SHOWFLAG(AntiAliasing)
			 DISABLE_ENGINE_SHOWFLAG(EyeAdaptation)
			 DISABLE_ENGINE_SHOWFLAG(SeparateTranslucency)
			 DISABLE_ENGINE_SHOWFLAG(DepthOfField)
			 DISABLE_ENGINE_SHOWFLAG(AmbientOcclusion)
			 DISABLE_ENGINE_SHOWFLAG(CameraImperfections)
			 DISABLE_ENGINE_SHOWFLAG(Decals)
			 DISABLE_ENGINE_SHOWFLAG(LensFlares)
			 DISABLE_ENGINE_SHOWFLAG(Bloom)
			 DISABLE_ENGINE_SHOWFLAG(ColorGrading)
			 DISABLE_ENGINE_SHOWFLAG(Tonemapper)
			 DISABLE_ENGINE_SHOWFLAG(Refraction)
			 DISABLE_ENGINE_SHOWFLAG(ReflectionEnvironment)
			 DISABLE_ENGINE_SHOWFLAG(AmbientCubemap)
			 DISABLE_ENGINE_SHOWFLAG(MotionBlur)
			 DISABLE_ENGINE_SHOWFLAG(DirectLighting)
			 DISABLE_ENGINE_SHOWFLAG(Lighting)
			 DISABLE_ENGINE_SHOWFLAG(Translucency)
			 DISABLE_ENGINE_SHOWFLAG(TextRender)
			 DISABLE_ENGINE_SHOWFLAG(Particles)
			 DISABLE_ENGINE_SHOWFLAG(SkeletalMeshes)
			 DISABLE_ENGINE_SHOWFLAG(StaticMeshes)
			 DISABLE_ENGINE_SHOWFLAG(BSP)
			 DISABLE_ENGINE_SHOWFLAG(Paper2DSprites)
#undef DISABLE_ENGINE_SHOWFLAG
		}
	}
#endif

	// force some show flags to be 0 or 1
	{
		const uint8* Force0Ptr = (const uint8*)&GSystemSettings.GetForce0Mask();
		const uint8* Force1Ptr = (const uint8*)&GSystemSettings.GetForce1Mask();
		uint8* Ptr = (uint8*)&EngineShowFlags;

		for(uint32 i = 0; i < sizeof(FEngineShowFlags); ++i)
		{
			uint8 Value = *Ptr;

			Value &= ~(*Force0Ptr);
			Value |= *Force1Ptr;
			*Ptr++ = Value;
			++Force0Ptr;
			++Force1Ptr;
		}
	}
}

void EngineShowFlagOrthographicOverride(bool bIsPerspective, FEngineShowFlags& EngineShowFlags)
{
	// Disable post processing that doesn't work in ortho viewports.
	if( !bIsPerspective )
	{
		EngineShowFlags.TemporalAA = 0;
		EngineShowFlags.MotionBlur = 0;
	}
}

EViewModeIndex FindViewMode(const FEngineShowFlags& EngineShowFlags)
{
	if(EngineShowFlags.VisualizeBuffer)
	{
		return VMI_VisualizeBuffer;
	}
	else if(EngineShowFlags.StationaryLightOverlap)
	{
		return VMI_StationaryLightOverlap;
	}
	// Test QuadComplexity before ShaderComplexity because QuadComplexity also use ShaderComplexity
	else if(EngineShowFlags.QuadOverdraw)
	{
		return VMI_QuadOverdraw;
	}
	else if(EngineShowFlags.ShaderComplexityWithQuadOverdraw)
	{
		return VMI_ShaderComplexityWithQuadOverdraw;
	}
	else if(EngineShowFlags.PrimitiveDistanceAccuracy)
	{
		return VMI_PrimitiveDistanceAccuracy;
	}
	else if(EngineShowFlags.MeshUVDensityAccuracy)
	{
		return VMI_MeshUVDensityAccuracy;
	}
	else if(EngineShowFlags.MaterialTextureScaleAccuracy)
	{
		return VMI_MaterialTextureScaleAccuracy;
	}
	else if(EngineShowFlags.RequiredTextureResolution)
	{
		return VMI_RequiredTextureResolution;
	}
	else if(EngineShowFlags.ShaderComplexity)
	{
		return VMI_ShaderComplexity;
	}
	else if(EngineShowFlags.VisualizeLightCulling)
	{
		return VMI_LightComplexity;
	}
	else if(EngineShowFlags.LightMapDensity)
	{
		if(EngineShowFlags.Lighting)
		{
			return VMI_LitLightmapDensity;
		}
		else
		{
			return VMI_LightmapDensity;
		}
	}
	else if(EngineShowFlags.OverrideDiffuseAndSpecular)
	{
		return VMI_Lit_DetailLighting;
	}
	else if (EngineShowFlags.LightingOnlyOverride)
	{
		return VMI_LightingOnly;
	}
	else if (EngineShowFlags.ReflectionOverride)
	{
		return VMI_ReflectionOverride;
	}
	else if(EngineShowFlags.Wireframe)
	{
		if (EngineShowFlags.Brushes)
		{
			return VMI_BrushWireframe;
		}
		else
		{
			return VMI_Wireframe;
		}
	}
	else if(!EngineShowFlags.Materials && EngineShowFlags.Lighting)
	{
		return VMI_LightingOnly;
	}
	else if (EngineShowFlags.CollisionPawn)
	{
		return VMI_CollisionPawn;
	}
	else if (EngineShowFlags.CollisionVisibility)
	{
		return VMI_CollisionVisibility;
	}
	else if (EngineShowFlags.LODColoration)
	{
		return VMI_LODColoration;
	}
	else if (EngineShowFlags.HLODColoration)
	{
		return VMI_HLODColoration;
	}
	else if (EngineShowFlags.PathTracing)
	{
		return VMI_PathTracing;
	}
	else if (EngineShowFlags.RayTracingDebug)
	{
		return VMI_RayTracingDebug;
	}

	return EngineShowFlags.Lighting ? VMI_Lit : VMI_Unlit;
}

const TCHAR* GetViewModeName(EViewModeIndex ViewModeIndex)
{
	switch(ViewModeIndex)
	{
		case VMI_Unknown:					return TEXT("Unknown");
		case VMI_BrushWireframe:			return TEXT("BrushWireframe");
		case VMI_Wireframe:					return TEXT("Wireframe");
		case VMI_Unlit:						return TEXT("Unlit");
		case VMI_Lit:						return TEXT("Lit");
		case VMI_Lit_DetailLighting:		return TEXT("Lit_DetailLighting");
		case VMI_LightingOnly:				return TEXT("LightingOnly");
		case VMI_LightComplexity:			return TEXT("LightComplexity");
		case VMI_ShaderComplexity:			return TEXT("ShaderComplexity");
		case VMI_QuadOverdraw:				return TEXT("QuadOverdraw");
		case VMI_ShaderComplexityWithQuadOverdraw: return TEXT("ShaderComplexityWithQuadOverdraw");
		case VMI_PrimitiveDistanceAccuracy:	return TEXT("PrimitiveDistanceAccuracy");
		case VMI_MeshUVDensityAccuracy:		return TEXT("MeshUVDensityAccuracy");
		case VMI_MaterialTextureScaleAccuracy: return TEXT("MaterialTexturecaleAccuracy");
		case VMI_RequiredTextureResolution: return TEXT("RequiredTextureResolution");
		case VMI_StationaryLightOverlap:	return TEXT("StationaryLightOverlap");
		case VMI_LightmapDensity:			return TEXT("LightmapDensity");
		case VMI_LitLightmapDensity:		return TEXT("LitLightmapDensity");
		case VMI_ReflectionOverride:		return TEXT("ReflectionOverride");
		case VMI_VisualizeBuffer:			return TEXT("VisualizeBuffer");
		case VMI_RayTracingDebug:			return TEXT("RayTracingDebug");
		case VMI_PathTracing:				return TEXT("PathTracing");
		case VMI_CollisionPawn:				return TEXT("CollisionPawn");
		case VMI_CollisionVisibility:		return TEXT("CollisionVis");
		case VMI_LODColoration:				return TEXT("LODColoration");
		case VMI_HLODColoration:			return TEXT("HLODColoration");
	}
	return TEXT("");
}
