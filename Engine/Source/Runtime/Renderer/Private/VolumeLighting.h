// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumeLighting.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "Components/LightComponent.h"
#include "Engine/MapBuildDataRegistry.h"

class FVolumeShadowingParameters
{
	DECLARE_TYPE_LAYOUT(FVolumeShadowingParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		WorldToShadowMatrix.Bind(ParameterMap,TEXT("WorldToShadowMatrix"));
		ShadowmapMinMax.Bind(ParameterMap,TEXT("ShadowmapMinMax"));
		DepthBiasParameters.Bind(ParameterMap, TEXT("DepthBiasParameters"));
		ShadowInjectParams.Bind(ParameterMap, TEXT("ShadowInjectParams"));
		ClippingPlanes.Bind(ParameterMap, TEXT("ClippingPlanes"));
		ShadowDepthTexture.Bind(ParameterMap, TEXT("ShadowDepthTexture"));
		ShadowDepthTextureSampler.Bind(ParameterMap, TEXT("ShadowDepthTextureSampler"));
		OnePassShadowParameters.Bind(ParameterMap);
		bStaticallyShadowed.Bind(ParameterMap, TEXT("bStaticallyShadowed"));
		StaticShadowDepthTexture.Bind(ParameterMap, TEXT("StaticShadowDepthTexture"));
		StaticShadowDepthTextureSampler.Bind(ParameterMap, TEXT("StaticShadowDepthTextureSampler"));
		WorldToStaticShadowMatrix.Bind(ParameterMap, TEXT("WorldToStaticShadowMatrix"));
		StaticShadowBufferSize.Bind(ParameterMap, TEXT("StaticShadowBufferSize"));
	}

	template<typename ShaderRHIParamRef>
	void Set(
		FRHICommandList& RHICmdList, 
		const ShaderRHIParamRef ShaderRHI, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FProjectedShadowInfo* ShadowMap, 
		int32 InnerSplitIndex, 
		bool bDynamicallyShadowed) const
	{
		if (bDynamicallyShadowed)
		{
			FVector4 ShadowmapMinMaxValue;
			FMatrix WorldToShadowMatrixValue = ShadowMap->GetWorldToShadowMatrix(ShadowmapMinMaxValue);

			SetShaderValue(RHICmdList, ShaderRHI, WorldToShadowMatrix, WorldToShadowMatrixValue);
			SetShaderValue(RHICmdList, ShaderRHI, ShadowmapMinMax, ShadowmapMinMaxValue);
		}

		// default to ignore the plane
		FVector4 Planes[2] = { FVector4(0, 0, 0, -1), FVector4(0, 0, 0, -1) };
		// .zw:DistanceFadeMAD to use MAD for efficiency in the shader, default to ignore the plane
		FVector4 ShadowInjectParamValue(1, 1, 0, 0);

		if (InnerSplitIndex != INDEX_NONE)
		{
			FShadowCascadeSettings ShadowCascadeSettings;

			LightSceneInfo->Proxy->GetShadowSplitBounds(View, InnerSplitIndex, LightSceneInfo->IsPrecomputedLightingValid(), &ShadowCascadeSettings);
			ensureMsgf(ShadowCascadeSettings.ShadowSplitIndex != INDEX_NONE, TEXT("FLightSceneProxy::GetShadowSplitBounds did not return an initialized ShadowCascadeSettings"));

			// near cascade plane
			{
				ShadowInjectParamValue.X = ShadowCascadeSettings.SplitNearFadeRegion == 0 ? 1.0f : 1.0f / ShadowCascadeSettings.SplitNearFadeRegion;
				Planes[0] = FVector4((FVector)(ShadowCascadeSettings.NearFrustumPlane),  -ShadowCascadeSettings.NearFrustumPlane.W);
			}

			uint32 CascadeCount = LightSceneInfo->Proxy->GetNumViewDependentWholeSceneShadows(View, LightSceneInfo->IsPrecomputedLightingValid());

			// far cascade plane
			if(InnerSplitIndex != CascadeCount - 1)
			{
				ShadowInjectParamValue.Y = 1.0f / (ShadowCascadeSettings.SplitFarFadeRegion == 0.0f ? 0.0001f : ShadowCascadeSettings.SplitFarFadeRegion);
				Planes[1] = FVector4((FVector)(ShadowCascadeSettings.FarFrustumPlane), -ShadowCascadeSettings.FarFrustumPlane.W);
			}

			const FVector2D FadeParams = LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);

			// setup constants for the MAD in shader
			ShadowInjectParamValue.Z = FadeParams.Y;
			ShadowInjectParamValue.W = -FadeParams.X * FadeParams.Y;
		}

		SetShaderValue(RHICmdList, ShaderRHI, ShadowInjectParams, ShadowInjectParamValue);

		SetShaderValueArray(RHICmdList, ShaderRHI, ClippingPlanes, Planes, UE_ARRAY_COUNT(Planes));
	
		ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();

		if (bDynamicallyShadowed)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DepthBiasParameters, FVector4(ShadowMap->GetShaderDepthBias(), ShadowMap->GetShaderSlopeDepthBias(), ShadowMap->GetShaderMaxSlopeDepthBias(), 1.0f / (ShadowMap->MaxSubjectZ - ShadowMap->MinSubjectZ)));

			FRHITexture* ShadowDepthTextureResource = nullptr;
			if (LightType == LightType_Point || LightType == LightType_Rect)
			{
				if (GBlackTexture && GBlackTexture->TextureRHI)
				{
					ShadowDepthTextureResource = GBlackTexture->TextureRHI->GetTexture2D();
				}
			}
			else
			{
				ShadowDepthTextureResource = ShadowMap->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
			}

			SetTextureParameter(
				RHICmdList, 
				ShaderRHI,
				ShadowDepthTexture,
				ShadowDepthTextureSampler,
				TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				ShadowDepthTextureResource
				);
		}

		OnePassShadowParameters.Set(RHICmdList, ShaderRHI, bDynamicallyShadowed && (LightType == LightType_Point || LightType == LightType_Rect)? ShadowMap : NULL);

		const FStaticShadowDepthMap* StaticShadowDepthMap = LightSceneInfo->Proxy->GetStaticShadowDepthMap();
		const uint32 bStaticallyShadowedValue = LightSceneInfo->IsPrecomputedLightingValid() && StaticShadowDepthMap && StaticShadowDepthMap->Data && StaticShadowDepthMap->TextureRHI ? 1 : 0;
		FRHITexture* StaticShadowDepthMapTexture = bStaticallyShadowedValue ? StaticShadowDepthMap->TextureRHI : GWhiteTexture->TextureRHI;
		const FMatrix WorldToStaticShadow = bStaticallyShadowedValue ? StaticShadowDepthMap->Data->WorldToLight : FMatrix::Identity;
		const FVector4 StaticShadowBufferSizeValue = bStaticallyShadowedValue ? FVector4(StaticShadowDepthMap->Data->ShadowMapSizeX, StaticShadowDepthMap->Data->ShadowMapSizeY, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeX, 1.0f / StaticShadowDepthMap->Data->ShadowMapSizeY) : FVector4(0, 0, 0, 0);

		SetShaderValue(RHICmdList, ShaderRHI, bStaticallyShadowed, bStaticallyShadowedValue);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI,
			StaticShadowDepthTexture,
			StaticShadowDepthTextureSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			StaticShadowDepthMapTexture
			);

		SetShaderValue(RHICmdList, ShaderRHI, WorldToStaticShadowMatrix, WorldToStaticShadow);
		SetShaderValue(RHICmdList, ShaderRHI, StaticShadowBufferSize, StaticShadowBufferSizeValue);
	}

	/** Serializer. */ 
	/*friend FArchive& operator<<(FArchive& Ar,FVolumeShadowingParameters& P)
	{
		Ar << P.WorldToShadowMatrix;
		Ar << P.ShadowmapMinMax;
		Ar << P.DepthBiasParameters;
		Ar << P.ShadowInjectParams;
		Ar << P.ClippingPlanes;
		Ar << P.ShadowDepthTexture;
		Ar << P.ShadowDepthTextureSampler;
		Ar << P.OnePassShadowParameters;
		Ar << P.bStaticallyShadowed;
		Ar << P.StaticShadowDepthTexture;
		Ar << P.StaticShadowDepthTextureSampler;
		Ar << P.WorldToStaticShadowMatrix;
		Ar << P.StaticShadowBufferSize;
		return Ar;
	}*/

private:
	
		LAYOUT_FIELD(FShaderParameter, WorldToShadowMatrix)
		LAYOUT_FIELD(FShaderParameter, ShadowmapMinMax)
		LAYOUT_FIELD(FShaderParameter, DepthBiasParameters)
		LAYOUT_FIELD(FShaderParameter, ShadowInjectParams)
		LAYOUT_FIELD(FShaderParameter, ClippingPlanes)
		LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture)
		LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTextureSampler)
		LAYOUT_FIELD(FOnePassPointShadowProjectionShaderParameters, OnePassShadowParameters)
		LAYOUT_FIELD(FShaderParameter, bStaticallyShadowed)
		LAYOUT_FIELD(FShaderResourceParameter, StaticShadowDepthTexture)
		LAYOUT_FIELD(FShaderResourceParameter, StaticShadowDepthTextureSampler)
		LAYOUT_FIELD(FShaderParameter, WorldToStaticShadowMatrix)
		LAYOUT_FIELD(FShaderParameter, StaticShadowBufferSize)
	
};