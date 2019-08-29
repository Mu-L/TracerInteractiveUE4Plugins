// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "OculusHMD_Settings.h"

#if OCULUS_HMD_SUPPORTED_PLATFORMS

namespace OculusHMD
{

//-------------------------------------------------------------------------------------------------
// FSettings
//-------------------------------------------------------------------------------------------------

FSettings::FSettings() :
	BaseOffset(0, 0, 0)
	, BaseOrientation(FQuat::Identity)
	, PixelDensity(1.0f)
	, PixelDensityMin(0.5f)
	, PixelDensityMax(1.0f)
	, bPixelDensityAdaptive(false)
	, SystemHeadset(ovrpSystemHeadset_None)
{
	Flags.Raw = 0;
	Flags.bHMDEnabled = true;
	Flags.bChromaAbCorrectionEnabled = true;
	Flags.bUpdateOnRT = true;
	Flags.bHQBuffer = false;
	Flags.bDirectMultiview = true;
	Flags.bIsUsingDirectMultiview = false;
#if PLATFORM_ANDROID
	Flags.bCompositeDepth = false;
#else
	Flags.bCompositeDepth = true;
#endif
	Flags.bSupportsDash = false;
	EyeRenderViewport[0] = EyeRenderViewport[1] = EyeRenderViewport[2] = FIntRect(0, 0, 0, 0);

	RenderTargetSize = FIntPoint(0, 0);
}

TSharedPtr<FSettings, ESPMode::ThreadSafe> FSettings::Clone() const
{
	TSharedPtr<FSettings, ESPMode::ThreadSafe> NewSettings = MakeShareable(new FSettings(*this));
	return NewSettings;
}

bool FSettings::UpdatePixelDensity(const float NewPixelDensity)
{
	if (!bPixelDensityAdaptive)
	{
		PixelDensity = NewPixelDensity;
		PixelDensityMin = FMath::Min(PixelDensity, PixelDensityMin);
		PixelDensityMax = FMath::Max(PixelDensity, PixelDensityMax);
	}
	return true;
}


} // namespace OculusHMD

#endif //OCULUS_HMD_SUPPORTED_PLATFORMS
