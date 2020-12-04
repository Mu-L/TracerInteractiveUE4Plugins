// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputBase.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Engine/RendererSettings.h"

namespace UE
{
namespace MoviePipeline
{
	void ValidateOutputFormatString(FString& InOutFilenameFormatString, const bool bTestRenderPass, const bool bTestFrameNumber)
	{
		const FString FrameNumberIdentifiers[] = { TEXT("{frame_number}"), TEXT("{frame_number_shot}"), TEXT("{frame_number_rel}"), TEXT("{frame_number_shot_rel}") };

		// If there is more than one file being written for this frame, make sure they uniquely identify.
		if (bTestRenderPass)
		{
			if (!InOutFilenameFormatString.Contains(TEXT("{render_pass}"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Multiple render passes exported but no {render_pass} format found. Automatically adding!"));
				
				// Search for a frame number in the output string
				int32 FrameNumberIndex = INDEX_NONE;
				for (const FString& Identifier : FrameNumberIdentifiers)
				{
					FrameNumberIndex = InOutFilenameFormatString.Find(Identifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (FrameNumberIndex != INDEX_NONE)
					{
						break;
					}
				}

				if (FrameNumberIndex == INDEX_NONE)
				{
					// No frame number found, so just append render_pass
					InOutFilenameFormatString += TEXT("{render_pass}");
				}
				else
				{
					// If a frame number is found, we need to insert render_pass first before it, so various editing
					// software will still be able to identify if this is an image sequence
					InOutFilenameFormatString.InsertAt(FrameNumberIndex, TEXT("{render_pass}."));
				}
			}
		}

		if (bTestFrameNumber)
		{
			// Ensure there is a frame number in the output string somewhere to uniquely identify individual files in an image sequence.
			int32 FrameNumberIndex = INDEX_NONE;
			for (const FString& Identifier : FrameNumberIdentifiers)
			{
				FrameNumberIndex = InOutFilenameFormatString.Find(Identifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (FrameNumberIndex != INDEX_NONE)
				{
					break;
				}
			}

			// We want to insert a {file_dup} before the frame number. This instructs the name resolver to put the (2) before
			// the frame number, so that they're still properly recognized as image sequences by other software. It will resolve
			// to "" if not needed.
			if (FrameNumberIndex == INDEX_NONE)
			{
				// Previously, the frame number identifier would be inserted so that files would not be overwritten. However, users prefer to have exact control over the filename.
				//InOutFilenameFormatString.Append(TEXT("{file_dup}.{frame_number}"));
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Frame number identifier not found. Files may be overwritten."));
			}
			else
			{
				// The user had already specified a frame number identifier, so we need to insert the
				// file_dup tag before it.
				InOutFilenameFormatString.InsertAt(FrameNumberIndex, TEXT("{file_dup}"));
			}
		}

		if (!InOutFilenameFormatString.Contains(TEXT("{file_dup}"), ESearchCase::IgnoreCase))
		{
			InOutFilenameFormatString.Append(TEXT("{file_dup}"));
		}
	}

	void RemoveFrameNumberFormatStrings(FString& InOutFilenameFormatString, const bool bIncludeShots)
	{
		// Strip {frame_number} related separators from their file name, otherwise it will create one output file per frame.
		InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number}"), TEXT(""));
		InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_rel}"), TEXT(""));

		if (bIncludeShots)
		{
			InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_shot}"), TEXT(""));
			InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_shot_rel}"), TEXT(""));
		}
	}
}
}