// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalDistanceFieldParameters.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"

class FShaderParameterMap;

/** Must match global distance field shaders. */
const int32 GMaxGlobalDistanceFieldClipmaps = 4;

class FGlobalDistanceFieldParameterData
{
public:

	FGlobalDistanceFieldParameterData()
	{
		FPlatformMemory::Memzero(this, sizeof(FGlobalDistanceFieldParameterData));
	}

	FVector4 CenterAndExtent[GMaxGlobalDistanceFieldClipmaps];
	FVector4 WorldToUVAddAndMul[GMaxGlobalDistanceFieldClipmaps];
	FRHITexture* Textures[GMaxGlobalDistanceFieldClipmaps];
	float GlobalDFResolution;
	float MaxDistance;
};

class FGlobalDistanceFieldParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FGlobalDistanceFieldParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		GlobalDistanceFieldTexture0.Bind(ParameterMap, TEXT("GlobalDistanceFieldTexture0"));
		GlobalDistanceFieldTexture1.Bind(ParameterMap, TEXT("GlobalDistanceFieldTexture1"));
		GlobalDistanceFieldTexture2.Bind(ParameterMap, TEXT("GlobalDistanceFieldTexture2"));
		GlobalDistanceFieldTexture3.Bind(ParameterMap, TEXT("GlobalDistanceFieldTexture3"));
		GlobalDistanceFieldSampler0.Bind(ParameterMap, TEXT("GlobalDistanceFieldSampler0"));
		GlobalDistanceFieldSampler1.Bind(ParameterMap, TEXT("GlobalDistanceFieldSampler1"));
		GlobalDistanceFieldSampler2.Bind(ParameterMap, TEXT("GlobalDistanceFieldSampler2"));
		GlobalDistanceFieldSampler3.Bind(ParameterMap, TEXT("GlobalDistanceFieldSampler3"));
		GlobalVolumeCenterAndExtent.Bind(ParameterMap,TEXT("GlobalVolumeCenterAndExtent"));
		GlobalVolumeWorldToUVAddAndMul.Bind(ParameterMap,TEXT("GlobalVolumeWorldToUVAddAndMul"));
		GlobalVolumeDimension.Bind(ParameterMap,TEXT("GlobalVolumeDimension"));
		GlobalVolumeTexelSize.Bind(ParameterMap,TEXT("GlobalVolumeTexelSize"));
		MaxGlobalDistance.Bind(ParameterMap,TEXT("MaxGlobalDistance"));
	}

	bool IsBound() const
	{
		return GlobalVolumeCenterAndExtent.IsBound() || GlobalVolumeWorldToUVAddAndMul.IsBound();
	}

	friend FArchive& operator<<(FArchive& Ar,FGlobalDistanceFieldParameters& Parameters)
	{
		Ar << Parameters.GlobalDistanceFieldTexture0;
		Ar << Parameters.GlobalDistanceFieldTexture1;
		Ar << Parameters.GlobalDistanceFieldTexture2;
		Ar << Parameters.GlobalDistanceFieldTexture3;
		Ar << Parameters.GlobalDistanceFieldSampler0;
		Ar << Parameters.GlobalDistanceFieldSampler1;
		Ar << Parameters.GlobalDistanceFieldSampler2;
		Ar << Parameters.GlobalDistanceFieldSampler3;
		Ar << Parameters.GlobalVolumeCenterAndExtent;
		Ar << Parameters.GlobalVolumeWorldToUVAddAndMul;
		Ar << Parameters.GlobalVolumeDimension;
		Ar << Parameters.GlobalVolumeTexelSize;
		Ar << Parameters.MaxGlobalDistance;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FGlobalDistanceFieldParameterData& ParameterData) const
	{
		if (IsBound())
		{
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldTexture0, GlobalDistanceFieldSampler0, TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), ParameterData.Textures[0] ? ParameterData.Textures[0] : GBlackVolumeTexture->TextureRHI.GetReference());
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldTexture1, GlobalDistanceFieldSampler1, TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), ParameterData.Textures[1] ? ParameterData.Textures[1] : GBlackVolumeTexture->TextureRHI.GetReference());
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldTexture2, GlobalDistanceFieldSampler2, TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), ParameterData.Textures[2] ? ParameterData.Textures[2] : GBlackVolumeTexture->TextureRHI.GetReference());
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldTexture3, GlobalDistanceFieldSampler3, TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), ParameterData.Textures[3] ? ParameterData.Textures[3] : GBlackVolumeTexture->TextureRHI.GetReference());

			SetShaderValueArray(RHICmdList, ShaderRHI, GlobalVolumeCenterAndExtent, ParameterData.CenterAndExtent, GMaxGlobalDistanceFieldClipmaps);
			SetShaderValueArray(RHICmdList, ShaderRHI, GlobalVolumeWorldToUVAddAndMul, ParameterData.WorldToUVAddAndMul, GMaxGlobalDistanceFieldClipmaps);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalVolumeDimension, ParameterData.GlobalDFResolution);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalVolumeTexelSize, 1.0f / ParameterData.GlobalDFResolution);
			SetShaderValue(RHICmdList, ShaderRHI, MaxGlobalDistance, ParameterData.MaxDistance);
		}
	}

private:
	
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldTexture0)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldTexture1)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldTexture2)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldTexture3)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldSampler0)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldSampler1)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldSampler2)
		LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldSampler3)
		LAYOUT_FIELD(FShaderParameter, GlobalVolumeCenterAndExtent)
		LAYOUT_FIELD(FShaderParameter, GlobalVolumeWorldToUVAddAndMul)
		LAYOUT_FIELD(FShaderParameter, GlobalVolumeDimension)
		LAYOUT_FIELD(FShaderParameter, GlobalVolumeTexelSize)
		LAYOUT_FIELD(FShaderParameter, MaxGlobalDistance)
	
};
