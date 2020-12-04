// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace UE
{
namespace SlateInsights
{

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

class FSlateInsightsStyle : public FSlateStyleSet
{
public:
	FSlateInsightsStyle()
		: FSlateStyleSet("SlateInsightsStyle")
	{
		const FVector2D Icon16x16(16.0f, 16.0f);
		SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		Set("SlateProfiler.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

		Set("SlateGraph.Color.WidgetCount", FLinearColor(FColorList::Aquamarine));
		Set("SlateGraph.Color.TickCount", FLinearColor(FColorList::BronzeII));
		Set("SlateGraph.Color.TimerCount", FLinearColor(FColorList::BlueViolet));
		Set("SlateGraph.Color.RepaintCount", FLinearColor(FColorList::CadetBlue));
		Set("SlateGraph.Color.VolatilePaintCount", FLinearColor(FColorList::MediumVioletRed));
		Set("SlateGraph.Color.PaintCount", FLinearColor(1.0f, 1.0f, 0.5f, 1.0f));
		Set("SlateGraph.Color.InvalidateCount", FLinearColor(FColorList::Orange));
		Set("SlateGraph.Color.RootInvalidateCount", FLinearColor(0.5f, 1.0f, 0.5f, 1.0f));

		FSlateBrush* WhiteBrush = new FSlateBrush(*FCoreStyle::Get().GetBrush("GenericWhiteBox"));
		Set("Flag.Font", FCoreStyle::Get().GetFontStyle("NormalFont"));
		Set("Flag.WhiteBrush", WhiteBrush);
		Set("Flag.Color.Background", FCoreStyle::Get().GetSlateColor("InvertedForeground"));
		Set("Flag.Color.Selected", FCoreStyle::Get().GetSlateColor("SelectionColor"));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	static FSlateInsightsStyle& Get()
	{
		static FSlateInsightsStyle Inst;
		return Inst;
	}
	
	~FSlateInsightsStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};

#undef IMAGE_BRUSH

} //namespace SlateInsights
} //namespace UE

