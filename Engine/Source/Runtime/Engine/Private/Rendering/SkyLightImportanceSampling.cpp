// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkyLightImportanceSampling.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

DECLARE_GPU_STAT_NAMED(BuildSkyLightMipTree, TEXT("Build SkyLight Mip Tree"));

#if RHI_RAYTRACING

class FBuildMipTreeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildMipTreeCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FBuildMipTreeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TextureParameter.Bind(Initializer.ParameterMap, TEXT("Texture"));
		TextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		FaceIndexParameter.Bind(Initializer.ParameterMap, TEXT("FaceIndex"));
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
	}

	FBuildMipTreeCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef Texture,
		const FIntVector& Dimensions,
		uint32 FaceIndex,
		uint32 MipLevel,
		FRWBuffer& MipTree)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetShaderValue(RHICmdList, ShaderRHI, FaceIndexParameter, FaceIndex);
		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);
		SetTextureParameter(RHICmdList, ShaderRHI, TextureParameter, TextureSamplerParameter, TStaticSamplerState<SF_Bilinear>::GetRHI(), Texture);

		check(MipTreeParameter.IsBound());
		MipTreeParameter.SetBuffer(RHICmdList, ShaderRHI, MipTree);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		ERHIAccess TransitionAccess,
		FRWBuffer& MipTree)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		MipTreeParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.Transition(FRHITransitionInfo(MipTree.UAV, ERHIAccess::Unknown, TransitionAccess));
	}


private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter,TextureSamplerParameter);

	LAYOUT_FIELD(FShaderParameter, DimensionsParameter);
	LAYOUT_FIELD(FShaderParameter, FaceIndexParameter);
	LAYOUT_FIELD(FShaderParameter, MipLevelParameter);
	LAYOUT_FIELD(FRWShaderParameter, MipTreeParameter);
};

class FBuildSolidAnglePdfCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildSolidAnglePdfCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FBuildSolidAnglePdfCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		SolidAnglePdfParameter.Bind(Initializer.ParameterMap, TEXT("SolidAnglePdf"));
	}

	FBuildSolidAnglePdfCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		uint32 MipLevel,
		const FIntVector& Dimensions,
		FRWBuffer& SolidAnglePdf
	)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);
		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		check(SolidAnglePdfParameter.IsBound());
		SolidAnglePdfParameter.SetBuffer(RHICmdList, ShaderRHI, SolidAnglePdf);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		ERHIAccess TransitionAccess,
		FRWBuffer& MipTreePdf
	)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SolidAnglePdfParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.Transition(FRHITransitionInfo(MipTreePdf.UAV, ERHIAccess::Unknown, TransitionAccess));
	}

private:
	LAYOUT_FIELD(FShaderParameter, MipLevelParameter);
	LAYOUT_FIELD(FShaderParameter, DimensionsParameter);
	LAYOUT_FIELD(FRWShaderParameter, SolidAnglePdfParameter);
};

class FBuildMipTreePdfCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildMipTreePdfCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FBuildMipTreePdfCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		MipTreePdfParameter.Bind(Initializer.ParameterMap, TEXT("MipTreePdf"));
	}

	FBuildMipTreePdfCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FRWBuffer& MipTree,
		const FIntVector& Dimensions,
		uint32 MipLevel,
		FRWBuffer& MipTreePdf)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeParameter, MipTree.SRV);
		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);

		check(MipTreePdfParameter.IsBound());
		MipTreePdfParameter.SetBuffer(RHICmdList, ShaderRHI, MipTreePdf);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		ERHIAccess TransitionAccess,
		FRWBuffer& MipTreePdf)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		MipTreePdfParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.Transition(FRHITransitionInfo(MipTreePdf.UAV, ERHIAccess::Unknown, TransitionAccess));
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, MipTreeParameter);
	LAYOUT_FIELD(FShaderParameter, DimensionsParameter);
	LAYOUT_FIELD(FShaderParameter, MipLevelParameter);

	LAYOUT_FIELD(FRWShaderParameter, MipTreePdfParameter);
};

IMPLEMENT_SHADER_TYPE(, FBuildMipTreeCS, TEXT("/Engine/Private/Raytracing/BuildMipTreeCS.usf"), TEXT("BuildMipTreeCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FBuildMipTreePdfCS, TEXT("/Engine/Private/Raytracing/BuildMipTreePdfCS.usf"), TEXT("BuildMipTreePdfCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FBuildSolidAnglePdfCS, TEXT("/Engine/Private/Raytracing/BuildSolidAnglePdfCS.usf"), TEXT("BuildSolidAnglePdfCS"), SF_Compute)

void BuildSkyLightMipTree(
	FRHICommandListImmediate& RHICmdList,
	FTextureRHIRef SkyLightTexture,
	FRWBuffer& SkyLightMipTreePosX,
	FRWBuffer& SkyLightMipTreeNegX,
	FRWBuffer& SkyLightMipTreePosY,
	FRWBuffer& SkyLightMipTreeNegY,
	FRWBuffer& SkyLightMipTreePosZ,
	FRWBuffer& SkyLightMipTreeNegZ,
	FIntVector& SkyLightMipTreeDimensions
)
{
	const auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FBuildMipTreeCS> BuildSkyLightMipTreeComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSkyLightMipTreeComputeShader.GetComputeShader());

	FRWBuffer* MipTrees[] = {
		&SkyLightMipTreePosX,
		&SkyLightMipTreeNegX,
		&SkyLightMipTreePosY,
		&SkyLightMipTreeNegY,
		&SkyLightMipTreePosZ,
		&SkyLightMipTreeNegZ
	};

	// Allocate MIP tree
	FIntVector TextureSize = SkyLightTexture->GetSizeXYZ();
	uint32 MipLevelCount = FMath::Min(FMath::CeilLogTwo(TextureSize.X), FMath::CeilLogTwo(TextureSize.Y));
	SkyLightMipTreeDimensions = FIntVector(1 << MipLevelCount, 1 << MipLevelCount, 1);
	uint32 NumElements = SkyLightMipTreeDimensions.X * SkyLightMipTreeDimensions.Y;
	for (uint32 MipLevel = 1; MipLevel <= MipLevelCount; ++MipLevel)
	{
		uint32 NumElementsInLevel = (SkyLightMipTreeDimensions.X >> MipLevel) * (SkyLightMipTreeDimensions.Y >> MipLevel);
		NumElements += NumElementsInLevel;
	}

	for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		MipTrees[FaceIndex]->Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);
	}

	// Execute hierarchical build
	for (uint32 MipLevel = 0; MipLevel <= MipLevelCount; ++MipLevel)
	{
		for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			BuildSkyLightMipTreeComputeShader->SetParameters(RHICmdList, SkyLightTexture, SkyLightMipTreeDimensions, FaceIndex, MipLevel, *MipTrees[FaceIndex]);
			FIntVector MipLevelDimensions = FIntVector(SkyLightMipTreeDimensions.X >> MipLevel, SkyLightMipTreeDimensions.Y >> MipLevel, 1);
			FIntVector NumGroups = FIntVector::DivideAndRoundUp(MipLevelDimensions, FBuildMipTreeCS::GetGroupSize());
			DispatchComputeShader(RHICmdList, BuildSkyLightMipTreeComputeShader, NumGroups.X, NumGroups.Y, 1);
			BuildSkyLightMipTreeComputeShader->UnsetParameters(RHICmdList, ERHIAccess::ERWBarrier, *MipTrees[FaceIndex]);
		}

		FRHITransitionInfo UAVTransitions[6];
		for (int MipTreeIndex = 0; MipTreeIndex < 6; ++MipTreeIndex)
		{
			UAVTransitions[MipTreeIndex] = FRHITransitionInfo(MipTrees[MipTreeIndex]->UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier);
		}
		RHICmdList.Transition(MakeArrayView(UAVTransitions, UE_ARRAY_COUNT(UAVTransitions)));
	}
}

void BuildSolidAnglePdf(
	FRHICommandListImmediate& RHICmdList,
	const FIntVector& Dimensions,
	FRWBuffer& SolidAnglePdf
)
{
	const auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FBuildSolidAnglePdfCS> BuildSolidAnglePdfComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSolidAnglePdfComputeShader.GetComputeShader());

	uint32 NumElements = Dimensions.X * Dimensions.Y;
	uint32 MipLevelCount = FMath::Log2(Dimensions.X);
	for (uint32 MipLevel = 1; MipLevel <= MipLevelCount; ++MipLevel)
	{
		NumElements += (Dimensions.X >> MipLevel) * (Dimensions.Y >> MipLevel);
	}
	SolidAnglePdf.Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);

	for (uint32 MipLevel = 0; MipLevel <= MipLevelCount; ++MipLevel)
	{
		BuildSolidAnglePdfComputeShader->SetParameters(RHICmdList, MipLevel, Dimensions, SolidAnglePdf);
		FIntVector NumGroups = FIntVector::DivideAndRoundUp(Dimensions, FBuildSolidAnglePdfCS::GetGroupSize());
		DispatchComputeShader(RHICmdList, BuildSolidAnglePdfComputeShader, NumGroups.X, NumGroups.Y, 1);
		BuildSolidAnglePdfComputeShader->UnsetParameters(RHICmdList, ERHIAccess::ERWBarrier, SolidAnglePdf);
	}
}

void BuildSkyLightMipTreePdf(
	FRHICommandListImmediate& RHICmdList,
	const FRWBuffer& SkyLightMipTreePosX,
	const FRWBuffer& SkyLightMipTreeNegX,
	const FRWBuffer& SkyLightMipTreePosY,
	const FRWBuffer& SkyLightMipTreeNegY,
	const FRWBuffer& SkyLightMipTreePosZ,
	const FRWBuffer& SkyLightMipTreeNegZ,
	const FIntVector& SkyLightMipTreeDimensions,
	FRWBuffer& SkyLightMipTreePdfPosX,
	FRWBuffer& SkyLightMipTreePdfNegX,
	FRWBuffer& SkyLightMipTreePdfPosY,
	FRWBuffer& SkyLightMipTreePdfNegY,
	FRWBuffer& SkyLightMipTreePdfPosZ,
	FRWBuffer& SkyLightMipTreePdfNegZ
)
{
	const FRWBuffer* MipTrees[] = {
		&SkyLightMipTreePosX,
		&SkyLightMipTreeNegX,
		&SkyLightMipTreePosY,
		&SkyLightMipTreeNegY,
		&SkyLightMipTreePosZ,
		&SkyLightMipTreeNegZ
	};

	FRWBuffer* MipTreePdfs[] = {
		&SkyLightMipTreePdfPosX,
		&SkyLightMipTreePdfNegX,
		&SkyLightMipTreePdfPosY,
		&SkyLightMipTreePdfNegY,
		&SkyLightMipTreePdfPosZ,
		&SkyLightMipTreePdfNegZ
	};

	const auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FBuildMipTreePdfCS> BuildSkyLightMipTreePdfComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildSkyLightMipTreePdfComputeShader.GetComputeShader());

	uint32 NumElements = SkyLightMipTreePosX.NumBytes / sizeof(float);
	uint32 MipLevelCount = FMath::Log2(SkyLightMipTreeDimensions.X);
	for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		MipTreePdfs[FaceIndex]->Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);

		// Execute hierarchical build
		uint32 MipLevel = 0;
		{
			BuildSkyLightMipTreePdfComputeShader->SetParameters(RHICmdList, *MipTrees[FaceIndex], SkyLightMipTreeDimensions, MipLevel, *MipTreePdfs[FaceIndex]);
			FIntVector MipLevelDimensions = FIntVector(SkyLightMipTreeDimensions.X >> MipLevel, SkyLightMipTreeDimensions.Y >> MipLevel, 1);
			FIntVector NumGroups = FIntVector::DivideAndRoundUp(MipLevelDimensions, FBuildMipTreeCS::GetGroupSize());
			DispatchComputeShader(RHICmdList, BuildSkyLightMipTreePdfComputeShader, NumGroups.X, NumGroups.Y, 1);
		}
		BuildSkyLightMipTreePdfComputeShader->UnsetParameters(RHICmdList, ERHIAccess::ERWBarrier, *MipTreePdfs[FaceIndex]);
	}

	FRHITransitionInfo UAVTransitions[6];
	for (int MipTreeIndex = 0; MipTreeIndex < 6; ++MipTreeIndex)
	{
		UAVTransitions[MipTreeIndex] = FRHITransitionInfo(MipTreePdfs[MipTreeIndex]->UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier);
	}
	RHICmdList.Transition(MakeArrayView(UAVTransitions, UE_ARRAY_COUNT(UAVTransitions)));
}

#endif

void FSkyLightImportanceSamplingData::BuildCDFs(FTexture* ProcessedTexture)
{
	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	SCOPED_DRAW_EVENT(RHICmdList, BuildSkyLightMipTree);
	SCOPED_GPU_STAT(RHICmdList, BuildSkyLightMipTree);

	check(ProcessedTexture);

#if RHI_RAYTRACING
	BuildSkyLightMipTree(
		RHICmdList, 
		ProcessedTexture->TextureRHI, 
		MipTreePosX, 
		MipTreeNegX, 
		MipTreePosY, 
		MipTreeNegY, 
		MipTreePosZ, 
		MipTreeNegZ, 
		MipDimensions
	);

	BuildSkyLightMipTreePdf(
		RHICmdList,
		MipTreePosX,
		MipTreeNegX, 
		MipTreePosY, 
		MipTreeNegY, 
		MipTreePosZ, 
		MipTreeNegZ, 
		MipDimensions,
		MipTreePdfPosX,
		MipTreePdfNegX, 
		MipTreePdfPosY, 
		MipTreePdfNegY, 
		MipTreePdfPosZ, 
		MipTreePdfNegZ);

	BuildSolidAnglePdf(RHICmdList, MipDimensions, SolidAnglePdf);
#endif

	bIsValid = true;
}

void FSkyLightImportanceSamplingData::ReleaseRHI()
{
	bIsValid = false;

	MipDimensions = FIntVector(0, 0, 0);

	MipTreePosX.Release();
	MipTreeNegX.Release();
	MipTreePosY.Release();
	MipTreeNegY.Release();
	MipTreePosZ.Release();
	MipTreeNegZ.Release();

	MipTreePdfPosX.Release();
	MipTreePdfNegX.Release();
	MipTreePdfPosY.Release();
	MipTreePdfNegY.Release();
	MipTreePdfPosZ.Release();
	MipTreePdfNegZ.Release();

	SolidAnglePdf.Release();
}

void FSkyLightImportanceSamplingData::AddRef()
{
	check(IsInGameThread());
	NumRefs++;
}

void FSkyLightImportanceSamplingData::Release()
{
	check(IsInGameThread());
	checkSlow(NumRefs > 0);
	if (--NumRefs == 0)
	{
		BeginReleaseResource(this);
		// Have to defer actual deletion until above rendering command has been processed, we will use the deferred cleanup interface for that
		BeginCleanup(this);
	}
}
