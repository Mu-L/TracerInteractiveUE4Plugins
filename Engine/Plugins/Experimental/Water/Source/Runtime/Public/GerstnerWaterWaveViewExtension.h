// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GerstnerWaterWaves.h"
#include "SceneViewExtension.h"

class AWaterBody;
class UGerstnerWaterWaves;

class FGerstnerWaterWaveViewExtension : public FSceneViewExtensionBase
{
public:

	FGerstnerWaterWaveViewExtension(const FAutoRegister& AutoReg);
	~FGerstnerWaterWaveViewExtension();

	// FSceneViewExtensionBase implementation : 
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;
	
	TArray<const AWaterBody*>* WaterBodies = nullptr;

	bool bRebuildGPUData = false;

	FStructuredBufferRHIRef DataBuffer;
	FShaderResourceViewRHIRef DataSRV;

	FStructuredBufferRHIRef IndirectionBuffer;
	FShaderResourceViewRHIRef IndirectionSRV;

};
