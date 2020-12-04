// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputSetting.h"
#include "Misc/Paths.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineQueue.h"
#include "MovieRenderPipelineDataTypes.h"

UMoviePipelineOutputSetting::UMoviePipelineOutputSetting()
	: OutputResolution(FIntPoint(1920, 1080))
	, bUseCustomFrameRate(false)
	, OutputFrameRate(FFrameRate(24, 1))
	, DEBUG_OutputFrameStepOffset(0)
	, bOverrideExistingOutput(true)
	, HandleFrameCount(0)
	, OutputFrameStep(1)
	, bUseCustomPlaybackRange(false)
	, CustomStartFrame(0)
	, CustomEndFrame(0)
	, VersionNumber(1)
	, bAutoVersion(true)
	, ZeroPadFrameNumbers(4)
	, FrameNumberOffset(0)
{
	FileNameFormat = TEXT("{sequence_name}.{frame_number}");
	OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("MovieRenders/");
}

void UMoviePipelineOutputSetting::PostLoad()
{
	Super::PostLoad();

	// In order to ship presets that work with any project, we can't use a relative path because it is
	// relative to the executable and thus has the project name embedded in. To solve this we will save
	// an empty string into the Output Directory and convert it to their relative directory in Post Load.
	// This isn't done in the CDO so that resetting to default value works as expected.
	if (OutputDirectory.Path.Len() == 0)
	{
		OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("MovieRenders/");
	}
}

#if WITH_EDITOR
FText UMoviePipelineOutputSetting::GetFooterText(UMoviePipelineExecutorJob* InJob) const 
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLine(NSLOCTEXT("MovieRenderPipeline", "OutputSettingFooterText_Fmt", "A list of {format_strings} and example values that are valid to use in the File Name Format:\n"));

	FMoviePipelineFormatArgs FormatArgs;
	FormatArgs.InJob = InJob;
	
	// Find the master configuration that owns us
	UMoviePipelineMasterConfig* MasterConfig = GetTypedOuter<UMoviePipelineMasterConfig>();
	if (MasterConfig)
	{
		MasterConfig->GetFormatArguments(FormatArgs);
	}

	for (const TPair<FString, FStringFormatArg>& KVP : FormatArgs.FilenameArguments)
	{
		FStringFormatOrderedArguments OrderedArgs = { KVP.Key, KVP.Value };
		FString FormattedArgs = FString::Format(TEXT("{0} => {1}"), OrderedArgs);

		TextBuilder.AppendLine(FText::FromString(FormattedArgs));
	}
	
	return TextBuilder.ToText();
}
#endif

void UMoviePipelineOutputSetting::GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const
{
	// Resolution Arguments
	{
		FString Resolution = FString::Printf(TEXT("%d_%d"), OutputResolution.X, OutputResolution.Y);
		InOutFormatArgs.FilenameArguments.Add(TEXT("output_resolution"), Resolution);
		InOutFormatArgs.FilenameArguments.Add(TEXT("output_width"), OutputResolution.X);
		InOutFormatArgs.FilenameArguments.Add(TEXT("output_height"), OutputResolution.Y);
	}

	if (bAutoVersion)
	{
		InOutFormatArgs.FilenameArguments.Add(TEXT("version"), TEXT("v00x"));
	}
	else
	{
		FString VersionText = FString::Printf(TEXT("v%0*d"), 3, VersionNumber);
		InOutFormatArgs.FilenameArguments.Add(TEXT("version"), VersionText);
	}
}
