// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessEyeAdaptation.h: Post processing eye adaptation implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RendererInterface.h"
#include "SceneRendering.h"
#include "PostProcess/RenderingCompositionGraph.h"

#define EYE_ADAPTATION_PARAMS_SIZE 4

FORCEINLINE float EV100ToLuminance(float EV100)
{
	return 1.2 * FMath::Pow(2.0f, EV100);
}

FORCEINLINE float EV100ToLog2(float EV100)
{
	return EV100 + 0.263f; // Where .263 is log2(1.2)
}

FORCEINLINE float LuminanceToEV100(float Luminance)
{
	return FMath::Log2(Luminance / 1.2f);
}

FORCEINLINE float Log2ToEV100(float Log2)
{
	return Log2 - 0.263f; // Where .263 is log2(1.2)
}

// Computes the eye-adaptation from HDRHistogram.
// ePId_Input0: HDRHistogram or nothing
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessEyeAdaptation : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessEyeAdaptation(bool bInIsComputePass)
	{
		bIsComputePass = bInIsComputePass;
		bPreferAsyncCompute = false;
		bPreferAsyncCompute &= (GNumAlternateFrameRenderingGroups == 1); // Can't handle multi-frame updates on async pipe
	}

	// compute the parameters used for eye-adaptation.  These will default to values
	// that disable eye-adaptation if the hardware doesn't support SM5 feature-level
	static void ComputeEyeAdaptationParamsValue(const FViewInfo& View, FVector4 Out[EYE_ADAPTATION_PARAMS_SIZE]);
	
	// Computes the a fix exposure to be used to replace the dynamic exposure when it's not available (< SM5).
	static float GetFixedExposure(const FViewInfo& View);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	virtual FRHIComputeFence* GetComputePassEndFence() const override { return AsyncEndFence; }

private:
	template <typename TRHICmdList>
	void DispatchCS(TRHICmdList& RHICmdList, FRenderingCompositePassContext& Context, FRHIUnorderedAccessView* DestUAV, IPooledRenderTarget* LastEyeAdaptation);

	FComputeFenceRHIRef AsyncEndFence;
};

// Write Log2(Luminance) in the alpha channel.
// ePId_Input0: Half-Res HDR scene color
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessBasicEyeAdaptationSetUp : public TRenderingCompositePassBase<1, 1>
{
public:
	
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
	virtual void Release() override { delete this; }
};

// ePId_Input0: Downsampled SceneColor Log
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessBasicEyeAdaptation : public TRenderingCompositePassBase<1, 1>
{
public:
	FRCPassPostProcessBasicEyeAdaptation(FIntPoint InDownsampledViewRect)
	: DownsampledViewRect(InDownsampledViewRect) 
	{
	}

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	FIntPoint DownsampledViewRect;
};

// Console Variable that is used to over-ride the post process settings.
extern TAutoConsoleVariable<int32> CVarEyeAdaptationMethodOverride;

// Query the view for the auto exposure method, and allow for CVar override.
static inline EAutoExposureMethod GetAutoExposureMethod(const FViewInfo& View)
{
	EAutoExposureMethod AutoExposureMethodId = View.FinalPostProcessSettings.AutoExposureMethod;
	const int32 EyeOverride = CVarEyeAdaptationMethodOverride.GetValueOnRenderThread();

	// Early out for common case
	if (EyeOverride < 0) return AutoExposureMethodId;

	// Additional branching for override.
	switch (EyeOverride)
	{
	case 1:
	{
			  AutoExposureMethodId = EAutoExposureMethod::AEM_Histogram;
			  break;
	}
	case 2:
	{
			  AutoExposureMethodId = EAutoExposureMethod::AEM_Basic;
			  break;
	}
	case 3:
	{
			  AutoExposureMethodId = EAutoExposureMethod::AEM_Manual;
			  break;
	}
	default:
	{
			   // Should only happen if the user supplies an override > 3
			   AutoExposureMethodId = EAutoExposureMethod::AEM_MAX;
			   break;
	}
	}
	return AutoExposureMethodId;
}

// @return true if the current feature level supports this auto exposure method.
static inline bool IsAutoExposureMethodSupported(const ERHIFeatureLevel::Type& FeatureLevel, const EAutoExposureMethod& AutoExposureMethodId)
{
	bool Result = false;

	switch (AutoExposureMethodId)
	{
	case EAutoExposureMethod::AEM_Histogram:
		Result = FeatureLevel >= ERHIFeatureLevel::SM5;
		break;
	case EAutoExposureMethod::AEM_Basic:
	case EAutoExposureMethod::AEM_Manual:
		Result = FeatureLevel >= ERHIFeatureLevel::ES3_1;
		break;
	default:
		break;
	}
	return Result;
}

extern TAutoConsoleVariable<float> CVarEyeAdaptationFocus;
static inline float GetBasicAutoExposureFocus()
{
	// Hard coded value camp.
	static float clampValue = 10.f;
	float FocusValue = CVarEyeAdaptationFocus.GetValueOnRenderThread();
	
	return FMath::Max(FMath::Min(FocusValue, clampValue), 0.0f);
}
