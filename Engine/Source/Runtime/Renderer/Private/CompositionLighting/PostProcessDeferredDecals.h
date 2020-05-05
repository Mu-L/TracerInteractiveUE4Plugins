// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessDeferredDecals.h: Deferred Decals implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RendererInterface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "DecalRenderingCommon.h"

// ePId_Input0: SceneColor (not needed for DBuffer decals)
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessDeferredDecals : public TRenderingCompositePassBase<1, 1>
{
public:
	// One instance for each render stage
	FRCPassPostProcessDeferredDecals(EDecalRenderStage InDecalRenderStage);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	// see EDecalRenderStage
	EDecalRenderStage CurrentStage;
	void DecodeRTWriteMask(FRenderingCompositePassContext& Context);
};

static inline bool IsWritingToGBufferA(FDecalRenderingCommon::ERenderTargetMode RenderTargetMode)
{
	return RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal
		|| RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal
		|| RenderTargetMode == FDecalRenderingCommon::RTM_GBufferNormal;
}

struct FDecalRenderTargetManager
{
	enum EDecalResolveBufferIndex
	{
		SceneColorIndex,
		GBufferAIndex,
		GBufferBIndex,
		GBufferCIndex,
		GBufferEIndex,
		DBufferAIndex,
		DBufferBIndex,
		DBufferCIndex,
		DBufferMaskIndex,
		ResolveBufferMax,
	};
	//
	FRHICommandList& RHICmdList;
	//
	bool TargetsToTransitionWritable[ResolveBufferMax];
	//
	FRHITexture* TargetsToResolve[ResolveBufferMax];
	//
	bool bGufferADirty;
	bool bGufferBCDirty;
	ERHIFeatureLevel::Type FeatureLevel;

	// constructor
	FDecalRenderTargetManager(FRHICommandList& InRHICmdList, EShaderPlatform ShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel, EDecalRenderStage CurrentStage);

	// destructor
	~FDecalRenderTargetManager()
	{

	}

	void ResolveTargets();

	void SetRenderTargetMode(FDecalRenderingCommon::ERenderTargetMode CurrentRenderTargetMode, bool bHasNormal, bool bPerPixelDBufferMask);
};

extern FRHIBlendState* GetDecalBlendState(const ERHIFeatureLevel::Type SMFeatureLevel, EDecalRenderStage InDecalRenderStage, EDecalBlendMode DecalBlendMode, bool bHasNormal);

extern void RenderMeshDecals(FRenderingCompositePassContext& Context, EDecalRenderStage CurrentDecalStage);
