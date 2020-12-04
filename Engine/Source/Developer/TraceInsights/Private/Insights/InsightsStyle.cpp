// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define IMAGE_BRUSH(RelativePath, ...)  FSlateImageBrush (FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...)    FSlateBoxBrush   (FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__)

TSharedPtr<FSlateStyleSet> FInsightsStyle::StyleInstance = nullptr;

void FInsightsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FInsightsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FInsightsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("InsightsStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FInsightsStyle::Create()
{
	TSharedRef<FSlateStyleSet> StyleRef = MakeShared<FSlateStyleSet>(FInsightsStyle::GetStyleSetName());
	StyleRef->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleRef->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet& Style = StyleRef.Get();

	//Style.Set("AppIcon", new IMAGE_BRUSH("Icons/Insights/AppIcon_24x", FVector2D(24.0f, 24.0f)));

	//////////////////////////////////////////////////

	Style.Set("WhiteBrush", new FSlateColorBrush(FLinearColor::White));
	Style.Set("SingleBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Style.Set("DoubleBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	Style.Set("EventBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Style.Set("HoveredEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));
	Style.Set("SelectedEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	//////////////////////////////////////////////////
	// Icons for major components

	Style.Set("SessionInfo.Icon.Large", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(32.0f, 32.0f)));
	Style.Set("SessionInfo.Icon.Small", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(16.0f, 16.0f)));

	Style.Set("Toolbar.Icon.Large", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(32.0f, 32.0f)));
	Style.Set("Toolbar.Icon.Small", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(16.0f, 16.0f)));

	//////////////////////////////////////////////////
	// Start Page buttons

	Style.Set("StartPage.Icon.Large", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(32.0f, 32.0f)));
	Style.Set("StartPage.Icon.Small", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", FVector2D(16.0f, 16.0f)));

	Style.Set("Open.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/Profiler_LoadMultiple_Profiler_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("Open.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/Profiler_Load_Profiler_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("OpenFile.Icon.Large", new IMAGE_BRUSH("Icons/LV_Load", FVector2D(32.0f, 32.0f)));
	Style.Set("OpenFile.Icon.Small", new IMAGE_BRUSH("Icons/LV_Load", FVector2D(16.0f, 16.0f)));

	//////////////////////////////////////////////////
	// Timing Insights

	Style.Set("TimingProfiler.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("TimingProfiler.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("FramesTrack.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("FramesTrack.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("GraphTrack.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("GraphTrack.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("TimingView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("TimingView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("TimersView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("TimersView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("StatsCountersView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("StatsCountersView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("LogView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_CopyToClipboard_32x", FVector2D(32.0f, 32.0f)));
	Style.Set("LogView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_CopyToClipboard_32x", FVector2D(16.0f, 16.0f)));

	Style.Set("TableTreeView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("TableTreeView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/Profiler_Data_Capture_40x", FVector2D(16.0f, 16.0f)));

	//////////////////////////////////////////////////
	// Asset Loading Insights

	Style.Set("LoadingProfiler.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("LoadingProfiler.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	//////////////////////////////////////////////////
	// Networking Insights

	Style.Set("NetworkingProfiler.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("NetworkingProfiler.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("PacketOveriew.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("PacketOveriew.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("PacketContentView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("PacketContentView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("NetStatsView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("NetStatsView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	//////////////////////////////////////////////////
	// Memory Insights

	Style.Set("MemoryProfiler.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("MemoryProfiler.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("MemTagTreeView.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(32.0f, 32.0f)));
	Style.Set("MemTagTreeView.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", FVector2D(16.0f, 16.0f)));

	Style.Set("Mem.Add.Small", new IMAGE_BRUSH("Icons/icon_Cascade_AddLOD2_40x", FVector2D(20.0f, 20.0f)));
	Style.Set("Mem.Remove.Small", new IMAGE_BRUSH("Icons/icon_Cascade_DeleteLOD_40x", FVector2D(20.0f, 20.0f)));

	//////////////////////////////////////////////////

	Style.Set("FindFirst", new IMAGE_BRUSH("Animation/backward_end", FVector2D(20.0f, 20.0f)));
	Style.Set("FindPrevious", new IMAGE_BRUSH("Animation/backward", FVector2D(20.0f, 20.0f)));
	Style.Set("FindNext", new IMAGE_BRUSH("Animation/forward", FVector2D(20.0f, 20.0f)));
	Style.Set("FindLast", new IMAGE_BRUSH("Animation/forward_end", FVector2D(20.0f, 20.0f)));

	return StyleRef;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH

const ISlateStyle& FInsightsStyle::Get()
{
	return *StyleInstance;
}
