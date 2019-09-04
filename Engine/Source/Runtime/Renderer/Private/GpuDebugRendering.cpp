// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GpuDebugRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"

FGpuDebugPrimitiveBuffers AllocateGpuDebugPrimitiveBuffers(FRHICommandListImmediate& RHICmdList)
{
	FGpuDebugPrimitiveBuffers Output;
	{
		FIntPoint Resolution(1, 1);
		EPixelFormat Format = PF_R32_UINT;
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Resolution, Format, FClearValueBinding::Black, TexCreate_None, TexCreate_UAV, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Output.DebugPrimitiveCountTexture, TEXT("DebugPrimitiveCountTexture"));
		}
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Resolution, Format, FClearValueBinding::None, TexCreate_CPUReadback, TexCreate_None, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Output.DebugPrimitiveCountStagingTexture, TEXT("DebugPrimitiveCountStagingTexture"));
		}
	}

	{
		FIntPoint Resolution(1024, 1);
		EPixelFormat Format = PF_A32B32G32R32F;
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Resolution, Format, FClearValueBinding::Black, TexCreate_None, TexCreate_UAV, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Output.DebugPrimitiveTexture, TEXT("DebugPrimitiveTexture"));
		}
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Resolution, Format, FClearValueBinding::None, TexCreate_CPUReadback, TexCreate_None, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Output.DebugPrimitiveStagingTexture, TEXT("DebugPrimitiveStagingTexture"));
		}
	}

	uint32 ClearValues[4] = { 0, 0, 0, 0 };
	RHICmdList.ClearTinyUAV(Output.DebugPrimitiveCountTexture->GetRenderTargetItem().UAV, ClearValues);
	RHICmdList.ClearTinyUAV(Output.DebugPrimitiveTexture->GetRenderTargetItem().UAV, ClearValues);

	return Output;
}

struct FLine { FVector Start; FVector End; FLinearColor Color; };
typedef TArray<FLine> FLineArray;
static FLineArray ReadGPUDebugPrimitives(FRHICommandListImmediate& RHICmdList, FGpuDebugPrimitiveBuffers& DebugPrimitiveBuffer)
{
	// Count
	uint32 PointCount = 0;
	{
		FTextureRHIRef SourceTexture = DebugPrimitiveBuffer.DebugPrimitiveCountTexture->GetRenderTargetItem().TargetableTexture;
		FTextureRHIRef StagingTexture = DebugPrimitiveBuffer.DebugPrimitiveCountStagingTexture->GetRenderTargetItem().ShaderResourceTexture;

		// Transfer memory GPU -> CPU
		RHICmdList.CopyToResolveTarget(SourceTexture, StagingTexture, FResolveParams());
		if (StagingTexture.IsValid())
		{
			const uint32* CountData = nullptr;
			int32 BufferWidth = 0, BufferHeight = 0;
			RHICmdList.MapStagingSurface(StagingTexture, *(void**)&CountData, BufferWidth, BufferHeight);

			if (CountData)
			{
				PointCount = CountData[0];
				PointCount = FMath::Min(PointCount, 1024u);
			}
			RHICmdList.UnmapStagingSurface(StagingTexture);
		}

	}

	// Primitives
	FLineArray Output;
	if (PointCount > 0)
	{
		auto ToColor = [](uint32 ColorIndex)
		{
			FLinearColor Color;
			switch (ColorIndex)
			{
			case 0:		return FLinearColor::Red;
			case 1:		return FLinearColor::Green;
			case 2:		return FLinearColor::Blue;
			case 3:		return FLinearColor::Yellow;
			default:	return FLinearColor::White;
			}
		};

		FTextureRHIRef SourceTexture = DebugPrimitiveBuffer.DebugPrimitiveTexture->GetRenderTargetItem().TargetableTexture;
		FTextureRHIRef StagingTexture = DebugPrimitiveBuffer.DebugPrimitiveStagingTexture->GetRenderTargetItem().ShaderResourceTexture;

		// Transfer memory GPU -> CPU
		RHICmdList.CopyToResolveTarget(SourceTexture, StagingTexture, FResolveParams());
		if (StagingTexture.IsValid())
		{	
			const FVector4* PrimitiveData = nullptr;
			int32 BufferWidth = 0, BufferHeight = 0;
			RHICmdList.MapStagingSurface(StagingTexture, *(void**)&PrimitiveData, BufferWidth, BufferHeight);
			
			if (PrimitiveData)
			{
				for (uint32 i = 0; i < PointCount; i += 2)
				{
					const FVector P0 = PrimitiveData[i+0];
					const FVector P1 = PrimitiveData[i+1];
					const FLinearColor Color = ToColor(uint32(PrimitiveData[i + 0].W));
					Output.Add({ P0, P1, Color });
				}
			}
			RHICmdList.UnmapStagingSurface(StagingTexture);
		}

	}
	return Output;
}

void BindGpuDebugPrimitiveBuffers(FRHIRenderPassInfo& RPInfo, FGpuDebugPrimitiveBuffers& DebugPrimitiveBuffer, uint32 UAVIndex)
{
	// Currently this value is hardcoded into the shader. Once the index is configurable in the shader, this check should be remove
	check(UAVIndex == 0); 

	RPInfo.UAVIndex = UAVIndex;
	RPInfo.NumUAVs = 2;
	RPInfo.UAVs[UAVIndex+0] = DebugPrimitiveBuffer.DebugPrimitiveCountTexture->GetRenderTargetItem().UAV;
	RPInfo.UAVs[UAVIndex+1] = DebugPrimitiveBuffer.DebugPrimitiveTexture->GetRenderTargetItem().UAV;
}

void DrawGpuDebugPrimitives(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views, FGpuDebugPrimitiveBuffers& DebugPrimitiveBuffer)
{
	const FLineArray DebugLines = ReadGPUDebugPrimitives(RHICmdList, DebugPrimitiveBuffer);

	uint32 ViewIndex = 0;
	for (FViewInfo& View : Views)
	{
		FViewElementPDI PDI(&View, nullptr, nullptr);

		for(int32 i=0; i < DebugLines.Num(); ++i)
		{
			const FLine& Line = DebugLines[i];
			PDI.DrawLine(Line.Start, Line.End, Line.Color, 0);
		}

		ViewIndex++;
	}
}
