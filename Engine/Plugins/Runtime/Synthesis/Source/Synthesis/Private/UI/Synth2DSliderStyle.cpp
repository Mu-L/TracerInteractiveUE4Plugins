// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "UI/Synth2DSliderStyle.h"
#include "Interfaces/IPluginManager.h"
#include "SynthesisModule.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"

FSynth2DSliderStyle::FSynth2DSliderStyle()
{
}

FSynth2DSliderStyle::~FSynth2DSliderStyle()
{
}

void FSynth2DSliderStyle::GetResources(TArray< const FSlateBrush* >& OutBrushes) const
{
}

const FSynth2DSliderStyle& FSynth2DSliderStyle::GetDefault()
{
	static FSynth2DSliderStyle Default;
	return Default;
}

const FName FSynth2DSliderStyle::TypeName(TEXT("Synth2DSliderStyle"));
