// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MaterialBakingHelpers.h"
#include "Math/Color.h"
#include "Async/ParallelFor.h"

static FColor BoxBlurSample(TArray<FColor>& InBMP, int32 X, int32 Y, int32 InImageWidth, int32 InImageHeight)
{
	const int32 SampleCount = 8;
	const int32 PixelIndices[SampleCount] = { -(InImageWidth + 1), -InImageWidth, -(InImageWidth - 1),
		-1, 1,
		(InImageWidth + -1), InImageWidth, (InImageWidth + 1) };

	static const int32 PixelOffsetX[SampleCount] = { -1, 0, 1,
		-1, 1,
		-1, 0, 1 };

	int32 PixelsSampled = 0;
	int32 CombinedColorR = 0;
	int32 CombinedColorG = 0;
	int32 CombinedColorB = 0;
	int32 CombinedColorA = 0;

	// Take samples for blur with square indices
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const int32 PixelIndex = ((Y * InImageWidth) + X) + PixelIndices[SampleIndex];
		const int32 XIndex = X + PixelOffsetX[SampleIndex];

		// Check if we are not out of texture bounds
		if (InBMP.IsValidIndex(PixelIndex) && XIndex >= 0 && XIndex < InImageWidth)
		{
			const FColor SampledColor = InBMP[PixelIndex];
			// Check if the pixel is a rendered one (not clear color)
			if ((!(SampledColor.R == 255 && SampledColor.B == 255 && SampledColor.G == 0)))
			{
				CombinedColorR += SampledColor.R;
				CombinedColorG += SampledColor.G;
				CombinedColorB += SampledColor.B;
				CombinedColorA += SampledColor.A;
				++PixelsSampled;
			}
		}
	}

	if (PixelsSampled == 0)
	{
		return InBMP[((Y * InImageWidth) + X)];
	}

	return FColor(CombinedColorR / PixelsSampled, CombinedColorG / PixelsSampled, CombinedColorB / PixelsSampled, CombinedColorA / PixelsSampled);
}

void FMaterialBakingHelpers::PerformUVBorderSmear(TArray<FColor>& InOutPixels, int32 ImageWidth, int32 ImageHeight, int32 MaxIterations)
{
	// This is ONLY possible because this is never called from multiple threads
	static TArray<FColor> Swap;
	if (Swap.Num())
	{
		Swap.SetNum(0, false);
	}
	Swap.Append(InOutPixels);

	TArray<FColor>* Current = &InOutPixels;
	TArray<FColor>* Scratch = &Swap;

	MaxIterations = MaxIterations < 1 ? FMath::Max(ImageWidth, ImageHeight) : MaxIterations;
	const int32 NumThreads = [&]()
	{
		return FPlatformProcess::SupportsMultithreading() ? FPlatformMisc::NumberOfCores() : 1;
	}();

	const int32 LinesPerThread = FMath::CeilToInt((float)ImageHeight / (float)NumThreads);
	uint32* MagentaPixels = new uint32[NumThreads];
	FMemory::Memset(MagentaPixels, 0xff, sizeof(uint32) * NumThreads);

	uint32 TotalPixels = ImageWidth * ImageHeight;
	uint32 SummedMagentaPixels = 1;

	// Sampling
	int32 LoopCount = 0;
	while (SummedMagentaPixels && (LoopCount <= MaxIterations))
	{
		SummedMagentaPixels = 0;
		// Left / right, Top / down
		ParallelFor(NumThreads, [ImageWidth, ImageHeight, MaxIterations, LoopCount, Current, Scratch, &MagentaPixels, LinesPerThread]
		(int32 Index)
		{
			if(MagentaPixels[Index] > 0)
			{
				MagentaPixels[Index] = 0;

				const int32 StartY = FMath::CeilToInt((Index)* LinesPerThread);
				const int32 EndY = FMath::Min(FMath::CeilToInt((Index + 1) * LinesPerThread), ImageHeight);

				for (int32 Y = StartY; Y < EndY; Y++)
				{
					for (int32 X = 0; X < ImageWidth; X++)
					{
						const int32 PixelIndex = (Y * ImageWidth) + X;
						FColor& Color = (*Current)[PixelIndex];
						if (Color.R == 255 && Color.B == 255 && Color.G == 0)
						{
							MagentaPixels[Index]++;
							const FColor SampledColor = BoxBlurSample(*Scratch, X, Y, ImageWidth, ImageHeight);
							// If it's a valid pixel
							if (!(SampledColor.R == 255 && SampledColor.B == 255 && SampledColor.G == 0))
							{
								Color = SampledColor;
							}
							else
							{
								// If we are at the end of our iterations, replace the pixels with black
								if (LoopCount == (MaxIterations - 1))
								{
									Color = FColor(0, 0, 0, 0);
								}
							}
						}
					}
				}
			}
		});

		for (int32 ThreadIndex = 0; ThreadIndex < NumThreads; ++ThreadIndex)
		{
			SummedMagentaPixels += MagentaPixels[ThreadIndex];
		}

		// If after iterating over all pixels we found they were all magenta, exit early
		if(SummedMagentaPixels >= TotalPixels)
		{
			FMemory::Memset(Current->GetData(), 0, Current->Num() * sizeof(FColor));
			break;
		}

		TArray<FColor>& Temp = *Scratch;
		Scratch = Current;
		Current = &Temp;

		LoopCount++;
	}

	if (Current != &InOutPixels)
	{
		InOutPixels.Empty();
		InOutPixels.Append(*Current);
	}

	delete[] MagentaPixels;
}

