// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SystemTextures.h: System textures definitions.
=============================================================================*/

#pragma once

#include "RenderGraph.h"

/**
 * Encapsulates the system textures used for scene rendering.
 */
class FSystemTextures : public FRenderResource
{
public:
	FSystemTextures()
		: FRenderResource()
		, FeatureLevelInitializedTo(ERHIFeatureLevel::Num)
	{}

	/**
	 * Initialize/allocate textures if not already.
	 */
	inline void InitializeTextures(FRHICommandListImmediate& RHICmdList, const ERHIFeatureLevel::Type InFeatureLevel)
	{
		// if this is the first call initialize everything
		if (FeatureLevelInitializedTo == ERHIFeatureLevel::Num)
		{
			InitializeCommonTextures(RHICmdList);
			InitializeFeatureLevelDependentTextures(RHICmdList, InFeatureLevel);
		}
		// otherwise, if we request a higher feature level, we might need to initialize those textures that depend on the feature level
		else if (InFeatureLevel > FeatureLevelInitializedTo)
		{
			InitializeFeatureLevelDependentTextures(RHICmdList, InFeatureLevel);
		}
		// there's no needed setup for those feature levels lower or identical to the current one
	}

	// FRenderResource interface.
	/**
	 * Release textures when device is lost/destroyed.
	 */
	virtual void ReleaseDynamicRHI();

	// -----------

	/**
		Any Textures added here MUST be explicitly released on ReleaseDynamicRHI()!
		Some RHIs need all their references released during destruction!
	*/

	// float4(1,1,1,1) can be used in case a light is not shadow casting
	TRefCountPtr<IPooledRenderTarget> WhiteDummy;
	// float4(0,0,0,0) can be used in additive postprocessing to avoid a shader combination
	TRefCountPtr<IPooledRenderTarget> BlackDummy;
	// float4(0,0,0,1)
	TRefCountPtr<IPooledRenderTarget> BlackAlphaOneDummy;
	// used by the material expression Noise
	TRefCountPtr<IPooledRenderTarget> PerlinNoiseGradient;
	// used by the material expression Noise (faster version, should replace old version), todo: move out of SceneRenderTargets
	TRefCountPtr<IPooledRenderTarget> PerlinNoise3D;
	// Sobol sampling texture, the first sample points for four sobol dimensions in RGBA
	TRefCountPtr<IPooledRenderTarget> SobolSampling;
	/** SSAO randomization */
	TRefCountPtr<IPooledRenderTarget> SSAORandomization;
	/** GTAO randomization */
	TRefCountPtr<IPooledRenderTarget> GTAORandomization;

	/** Preintegrated GF for single sample IBL */
	TRefCountPtr<IPooledRenderTarget> PreintegratedGF;
	/** Hair BSDF LUT texture */
	TRefCountPtr<IPooledRenderTarget> HairLUT0;
	TRefCountPtr<IPooledRenderTarget> HairLUT1;
	TRefCountPtr<IPooledRenderTarget> HairLUT2;
	/** Linearly Transformed Cosines LUTs */
	TRefCountPtr<IPooledRenderTarget> LTCMat;
	TRefCountPtr<IPooledRenderTarget> LTCAmp;
	/** Texture that holds a single value containing the maximum depth that can be stored as FP16. */
	TRefCountPtr<IPooledRenderTarget> MaxFP16Depth;
	/** Depth texture that holds a single depth value */
	TRefCountPtr<IPooledRenderTarget> DepthDummy;
	/** Stencil texture that holds a single stencil value. */
	TRefCountPtr<IPooledRenderTarget> StencilDummy;
	// float4(0,1,0,1)
	TRefCountPtr<IPooledRenderTarget> GreenDummy;
	// float4(0.5,0.5,0.5,1)
	TRefCountPtr<IPooledRenderTarget> DefaultNormal8Bit;
	// float4(0.5,0.5,0.5,0.5)
	TRefCountPtr<IPooledRenderTarget> MidGreyDummy;

	/** float4(0,0,0,0) volumetric texture. */
	TRefCountPtr<IPooledRenderTarget> VolumetricBlackDummy;
	

	// Dummy 0 Uint texture for RHIs that need explicit overloads
	TRefCountPtr<IPooledRenderTarget> ZeroUIntDummy;

	// SRV for WhiteDummy Texture.
	TRefCountPtr<FRHIShaderResourceView> WhiteDummySRV;
	// SRV for StencilDummy Texture.
	TRefCountPtr<FRHIShaderResourceView> StencilDummySRV;

	FRDGTextureRef GetWhiteDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetBlackDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetZeroUIntDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetBlackAlphaOneDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetPerlinNoiseGradient(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetPerlinNoise3D(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetSobolSampling(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetSSAORandomization(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetPreintegratedGF(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetLTCMat(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetLTCAmp(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetMaxFP16Depth(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetDepthDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetStencilDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetGreenDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetDefaultNormal8Bit(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetMidGreyDummy(FRDGBuilder& GraphBuilder) const;
	FRDGTextureRef GetVolumetricBlackDummy(FRDGBuilder& GraphBuilder) const;

protected:
	/** Maximum feature level that the textures have been initialized up to */
	ERHIFeatureLevel::Type FeatureLevelInitializedTo;

	void InitializeCommonTextures(FRHICommandListImmediate& RHICmdList);
	void InitializeFeatureLevelDependentTextures(FRHICommandListImmediate& RHICmdList, const ERHIFeatureLevel::Type InFeatureLevel);
};

/** The global system textures used for scene rendering. */
RENDERER_API extern TGlobalResource<FSystemTextures> GSystemTextures;
