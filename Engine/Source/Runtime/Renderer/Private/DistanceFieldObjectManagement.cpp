// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldObjectManagement.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"

float GAOMaxObjectBoundingRadius = 50000;
FAutoConsoleVariableRef CVarAOMaxObjectBoundingRadius(
	TEXT("r.AOMaxObjectBoundingRadius"),
	GAOMaxObjectBoundingRadius,
	TEXT("Objects larger than this will not contribute to AO calculations, to improve performance."),
	ECVF_RenderThreadSafe
	);

int32 GAOLogObjectBufferReallocation = 0;
FAutoConsoleVariableRef CVarAOLogObjectBufferReallocation(
	TEXT("r.AOLogObjectBufferReallocation"),
	GAOLogObjectBufferReallocation,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

// Must match equivalent shader defines
template<> int32 TDistanceFieldObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride = 17;
template<> int32 TDistanceFieldObjectBuffers<DFPT_HeightField>::ObjectDataStride = 6;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride = 17;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectBoxBoundsStride = 5;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectDataStride = 6;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectBoxBoundsStride = 5;

// In float4's.  Must match corresponding usf definition
int32 UploadObjectDataStride = 1 + FDistanceFieldObjectBuffers::ObjectDataStride;
int32 UploadHeightFieldObjectDataStride = 2 + FHeightFieldObjectBuffers::ObjectDataStride;

template <EDistanceFieldPrimitiveType PrimitiveType>
void TDistanceFieldObjectBuffers<PrimitiveType>::Initialize()
{
	if (MaxObjects > 0)
	{
		const uint32 BufferFlags = BUF_ShaderResource;
		
		uint32 NumComponents = 4;
		EPixelFormat BufferFormat = PF_R32_FLOAT;

		if (RHISupports4ComponentUAVReadWrite(GMaxRHIShaderPlatform))
		{
			NumComponents = 1;
			BufferFormat = PF_A32B32G32R32F;
		}

		uint32 BoundsNumElements;
		const TCHAR* BoundsDebugName;
		const TCHAR* DataDebugName;

		if (PrimitiveType == DFPT_HeightField)
		{
			BoundsNumElements = NumComponents * 2 * MaxObjects;
			BoundsDebugName = TEXT("FHeightFieldObjectBuffers_Bounds");
			DataDebugName = TEXT("FHeightFieldObjectBuffers_Data");
		}
		else
		{
			check(PrimitiveType == DFPT_SignedDistanceField);
			BoundsNumElements = NumComponents * MaxObjects;
			BoundsDebugName = TEXT("FDistanceFieldObjectBuffers_Bounds");
			DataDebugName = TEXT("FDistanceFieldObjectBuffers_Data");
		}

		Bounds.Initialize(GPixelFormats[BufferFormat].BlockBytes, BoundsNumElements, BufferFormat, 0, BoundsDebugName);
		Data.Initialize(GPixelFormats[BufferFormat].BlockBytes, NumComponents * MaxObjects * ObjectDataStride, BufferFormat, 0, DataDebugName);
	}
}

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldUploadDataResource : public FRenderResource
{
public:

	FCPUUpdatedBuffer UploadData;

	TDistanceFieldUploadDataResource()
	{
		// PS4 volatile only supports 8Mb, switch to dynamic once that is fixed.
		// PS4 volatile only supports 8Mb, switch to volatile once that is fixed.
		UploadData.bVolatile = false;

		UploadData.Format = PF_A32B32G32R32F;
		UploadData.Stride = PrimitiveType == DFPT_HeightField ? UploadHeightFieldObjectDataStride : UploadObjectDataStride;
	}

	virtual void InitDynamicRHI()  override
	{
		UploadData.Initialize();
	}

	virtual void ReleaseDynamicRHI() override
	{
		UploadData.Release();
	}
};

TGlobalResource<TDistanceFieldUploadDataResource<DFPT_SignedDistanceField>> GDistanceFieldUploadData;
TGlobalResource<TDistanceFieldUploadDataResource<DFPT_HeightField>> GHeightFieldUploadData;

class FDistanceFieldUploadIndicesResource : public FRenderResource
{
public:

	FCPUUpdatedBuffer UploadIndices;

	FDistanceFieldUploadIndicesResource()
	{
		// PS4 volatile only supports 8Mb, switch to volatile once that is fixed.
		UploadIndices.bVolatile = false;

		UploadIndices.Format = PF_R32_UINT;
		UploadIndices.Stride = 1;
	}

	virtual void InitDynamicRHI()  override
	{
		UploadIndices.Initialize();
	}

	virtual void ReleaseDynamicRHI() override
	{
		UploadIndices.Release();
	}
};

TGlobalResource<FDistanceFieldUploadIndicesResource> GDistanceFieldUploadIndices;
TGlobalResource<FDistanceFieldUploadIndicesResource> GHeightFieldUploadIndices;

class FDistanceFieldRemoveIndicesResource : public FRenderResource
{
public:

	FCPUUpdatedBuffer RemoveIndices;

	FDistanceFieldRemoveIndicesResource()
	{
		RemoveIndices.Format = PF_R32G32B32A32_UINT;
		RemoveIndices.Stride = 1;
	}

	virtual void InitDynamicRHI()  override
	{
		RemoveIndices.Initialize();
	}

	virtual void ReleaseDynamicRHI() override
	{
		RemoveIndices.Release();
	}
};

TGlobalResource<FDistanceFieldRemoveIndicesResource> GDistanceFieldRemoveIndices;
TGlobalResource<FDistanceFieldRemoveIndicesResource> GHeightFieldRemoveIndices;

const uint32 UpdateObjectsGroupSize = 64;

template <EDistanceFieldPrimitiveType PrimitiveType>
class TUploadObjectsToBufferCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TUploadObjectsToBufferCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	TUploadObjectsToBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumUploadOperations.Bind(Initializer.ParameterMap, TEXT("NumUploadOperations"));
		UploadOperationIndices.Bind(Initializer.ParameterMap, TEXT("UploadOperationIndices"));
		UploadOperationData.Bind(Initializer.ParameterMap, TEXT("UploadOperationData"));
		ObjectBufferParameters.Bind(Initializer.ParameterMap);
	}

	TUploadObjectsToBufferCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FScene* Scene, uint32 NumUploadOperationsValue, FRHIShaderResourceView* InUploadOperationIndices, FRHIShaderResourceView* InUploadOperationData)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, NumUploadOperations, NumUploadOperationsValue);

		SetSRVParameter(RHICmdList, ShaderRHI, UploadOperationIndices, InUploadOperationIndices);
		SetSRVParameter(RHICmdList, ShaderRHI, UploadOperationData, InUploadOperationData);

		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& ObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());
		const uint32 NumObjectsInBuffer = bIsHeightField ? SceneData.NumHeightFieldObjectsInBuffer : SceneData.NumObjectsInBuffer;

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (bIsHeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectBufferParameters.Set(RHICmdList, ShaderRHI, ObjectBuffers, NumObjectsInBuffer, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ, true);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FScene* Scene)
	{
		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& ObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());

		ObjectBufferParameters.UnsetParameters(RHICmdList, RHICmdList.GetBoundComputeShader(), ObjectBuffers, true);
	}

private:

	LAYOUT_FIELD(FShaderParameter, NumUploadOperations)
	LAYOUT_FIELD(FShaderResourceParameter, UploadOperationIndices)
	LAYOUT_FIELD(FShaderResourceParameter, UploadOperationData)

	// clang is segfaulting with the templated IMPLEMENT_SHADER_TYPE below with this templated type directly in the LAYOUT_FIELD, so the uses of using like this in this file works around it
	// (and also in DistanceFieldShadowing.cpp)
	// a bugreport has been sent to Apple
	using FDistanceFieldObjectBufferParametersType = TDistanceFieldObjectBufferParameters<PrimitiveType>;
	LAYOUT_FIELD(FDistanceFieldObjectBufferParametersType, ObjectBufferParameters)
};

IMPLEMENT_SHADER_TYPE(template<>, TUploadObjectsToBufferCS<DFPT_SignedDistanceField>, TEXT("/Engine/Private/DistanceFieldObjectCulling.usf"), TEXT("UploadObjectsToBufferCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TUploadObjectsToBufferCS<DFPT_HeightField>, TEXT("/Engine/Private/DistanceFieldObjectCulling.usf"), TEXT("UploadObjectsToBufferCS"), SF_Compute);

template <EDistanceFieldPrimitiveType PrimitiveType>
class TCopyObjectBufferCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TCopyObjectBufferCS, Global);

public:
	typedef typename TChooseClass<PrimitiveType == DFPT_HeightField, FHeightFieldObjectBuffers, FDistanceFieldObjectBuffers>::Result ObjectBuffersType;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	TCopyObjectBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CopyObjectBounds.Bind(Initializer.ParameterMap, TEXT("CopyObjectBounds"));
		CopyObjectData.Bind(Initializer.ParameterMap, TEXT("CopyObjectData"));
		ObjectBufferParameters.Bind(Initializer.ParameterMap);
	}

	TCopyObjectBufferCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, ObjectBuffersType& ObjectBuffersSource, ObjectBuffersType& ObjectBuffersDest, int32 NumObjectsValue)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		FRHIUnorderedAccessView* OutUAVs[2];
		OutUAVs[0] = ObjectBuffersDest.Bounds.UAV;
		OutUAVs[1] = ObjectBuffersDest.Data.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));

		CopyObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffersDest.Bounds);
		CopyObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffersDest.Data);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (PrimitiveType == DFPT_HeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectBufferParameters.Set(RHICmdList, ShaderRHI, ObjectBuffersSource, NumObjectsValue, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, ObjectBuffersType& ObjectBuffersDest)
	{
		ObjectBufferParameters.UnsetParameters(RHICmdList, RHICmdList.GetBoundComputeShader(), ObjectBuffersDest);
		CopyObjectBounds.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		CopyObjectData.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());

		FRHIUnorderedAccessView* OutUAVs[2];
		OutUAVs[0] = ObjectBuffersDest.Bounds.UAV;
		OutUAVs[1] = ObjectBuffersDest.Data.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));
	}

private:

	LAYOUT_FIELD(FRWShaderParameter, CopyObjectBounds)
	LAYOUT_FIELD(FRWShaderParameter, CopyObjectData)
	using FDistanceFieldObjectBufferParametersType = TDistanceFieldObjectBufferParameters<PrimitiveType>;
	LAYOUT_FIELD(FDistanceFieldObjectBufferParametersType, ObjectBufferParameters)
};

IMPLEMENT_SHADER_TYPE(template<>, TCopyObjectBufferCS<DFPT_SignedDistanceField>, TEXT("/Engine/Private/DistanceFieldObjectCulling.usf"), TEXT("CopyObjectBufferCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyObjectBufferCS<DFPT_HeightField>, TEXT("/Engine/Private/DistanceFieldObjectCulling.usf"), TEXT("CopyObjectBufferCS"), SF_Compute);

class FCopySurfelBufferCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopySurfelBufferCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
	}

	FCopySurfelBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CopyInterpolatedVertexData.Bind(Initializer.ParameterMap, TEXT("CopyInterpolatedVertexData"));
		CopySurfelData.Bind(Initializer.ParameterMap, TEXT("CopySurfelData"));
		SurfelBufferParameters.Bind(Initializer.ParameterMap);
		NumSurfels.Bind(Initializer.ParameterMap, TEXT("NumSurfels"));
	}

	FCopySurfelBufferCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSurfelBuffers& SurfelBuffersSource, const FInstancedSurfelBuffers& InstancedSurfelBuffersSource, FSurfelBuffers& SurfelBuffersDest, int32 NumSurfelsValue)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		FRHIUnorderedAccessView* OutUAVs[2];
		OutUAVs[0] = SurfelBuffersDest.InterpolatedVertexData.UAV;
		OutUAVs[1] = SurfelBuffersDest.Surfels.UAV;		
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));

		CopyInterpolatedVertexData.SetBuffer(RHICmdList, ShaderRHI, SurfelBuffersDest.InterpolatedVertexData);
		CopySurfelData.SetBuffer(RHICmdList, ShaderRHI, SurfelBuffersDest.Surfels);
		SurfelBufferParameters.Set(RHICmdList, ShaderRHI, SurfelBuffersSource, InstancedSurfelBuffersSource);
		SetShaderValue(RHICmdList, ShaderRHI, NumSurfels, NumSurfelsValue);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FSurfelBuffers& SurfelBuffersDest)
	{
		SurfelBufferParameters.UnsetParameters(RHICmdList, RHICmdList.GetBoundComputeShader());
		CopyInterpolatedVertexData.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		CopySurfelData.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());

		FRHIUnorderedAccessView* OutUAVs[2];
		OutUAVs[0] = SurfelBuffersDest.InterpolatedVertexData.UAV;
		OutUAVs[1] = SurfelBuffersDest.Surfels.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));
	}

private:

	LAYOUT_FIELD(FRWShaderParameter, CopyInterpolatedVertexData)
	LAYOUT_FIELD(FRWShaderParameter, CopySurfelData)
	LAYOUT_FIELD(FSurfelBufferParameters, SurfelBufferParameters)
	LAYOUT_FIELD(FShaderParameter, NumSurfels)
};

IMPLEMENT_SHADER_TYPE(,FCopySurfelBufferCS,TEXT("/Engine/Private/SurfelTree.usf"),TEXT("CopySurfelBufferCS"),SF_Compute);


class FCopyVPLFluxBufferCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyVPLFluxBufferCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
	}

	FCopyVPLFluxBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CopyVPLFlux.Bind(Initializer.ParameterMap, TEXT("CopyVPLFlux"));
		SurfelBufferParameters.Bind(Initializer.ParameterMap);
		NumSurfels.Bind(Initializer.ParameterMap, TEXT("NumSurfels"));
	}

	FCopyVPLFluxBufferCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSurfelBuffers& SurfelBuffersSource, const FInstancedSurfelBuffers& InstancedSurfelBuffersSource, FInstancedSurfelBuffers& InstancedSurfelBuffersDest, int32 NumSurfelsValue)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, InstancedSurfelBuffersDest.VPLFlux.UAV);
		CopyVPLFlux.SetBuffer(RHICmdList, ShaderRHI, InstancedSurfelBuffersDest.VPLFlux);
		SurfelBufferParameters.Set(RHICmdList, ShaderRHI, SurfelBuffersSource, InstancedSurfelBuffersSource);
		SetShaderValue(RHICmdList, ShaderRHI, NumSurfels, NumSurfelsValue);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FInstancedSurfelBuffers& InstancedSurfelBuffersDest)
	{
		SurfelBufferParameters.UnsetParameters(RHICmdList, RHICmdList.GetBoundComputeShader());
		CopyVPLFlux.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, InstancedSurfelBuffersDest.VPLFlux.UAV);
	}

private:

	LAYOUT_FIELD(FRWShaderParameter, CopyVPLFlux)
	LAYOUT_FIELD(FSurfelBufferParameters, SurfelBufferParameters)
	LAYOUT_FIELD(FShaderParameter, NumSurfels)
};

IMPLEMENT_SHADER_TYPE(,FCopyVPLFluxBufferCS,TEXT("/Engine/Private/SurfelTree.usf"),TEXT("CopyVPLFluxBufferCS"),SF_Compute);

template<bool bRemoveFromSameBuffer, EDistanceFieldPrimitiveType PrimitiveType>
class TRemoveObjectsFromBufferCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TRemoveObjectsFromBufferCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
		OutEnvironment.SetDefine(TEXT("REMOVE_FROM_SAME_BUFFER"), bRemoveFromSameBuffer);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	TRemoveObjectsFromBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumRemoveOperations.Bind(Initializer.ParameterMap, TEXT("NumRemoveOperations"));
		RemoveOperationIndices.Bind(Initializer.ParameterMap, TEXT("RemoveOperationIndices"));
		ObjectBufferParameters.Bind(Initializer.ParameterMap);
		ObjectBounds2.Bind(Initializer.ParameterMap, TEXT("ObjectBounds2"));
		ObjectData2.Bind(Initializer.ParameterMap, TEXT("ObjectData2"));
	}

	TRemoveObjectsFromBufferCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FScene* Scene, 
		uint32 NumRemoveOperationsValue, 
		FRHIShaderResourceView* InRemoveOperationIndices,
		FRHIShaderResourceView* InObjectBounds2,
		FRHIShaderResourceView* InObjectData2)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, NumRemoveOperations, NumRemoveOperationsValue);
		SetSRVParameter(RHICmdList, ShaderRHI, RemoveOperationIndices, InRemoveOperationIndices);

		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& ObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());
		const uint32 NumObjectsInBuffer = bIsHeightField ? SceneData.NumHeightFieldObjectsInBuffer : SceneData.NumObjectsInBuffer;

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (PrimitiveType == DFPT_HeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			check(PrimitiveType == DFPT_SignedDistanceField);
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectBufferParameters.Set(RHICmdList, ShaderRHI, ObjectBuffers, NumObjectsInBuffer, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ, true);

		SetSRVParameter(RHICmdList, ShaderRHI, ObjectBounds2, InObjectBounds2);
		SetSRVParameter(RHICmdList, ShaderRHI, ObjectData2, InObjectData2);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FScene* Scene)
	{
		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& ObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());

		ObjectBufferParameters.UnsetParameters(RHICmdList, RHICmdList.GetBoundComputeShader(), ObjectBuffers, true);
	}

private:

	LAYOUT_FIELD(FShaderParameter, NumRemoveOperations)
	LAYOUT_FIELD(FShaderResourceParameter, RemoveOperationIndices)
	using FDistanceFieldObjectBufferParametersType = TDistanceFieldObjectBufferParameters<PrimitiveType>;
	LAYOUT_FIELD(FDistanceFieldObjectBufferParametersType, ObjectBufferParameters)
	LAYOUT_FIELD(FShaderResourceParameter, ObjectBounds2)
	LAYOUT_FIELD(FShaderResourceParameter, ObjectData2)
};

#define IMPLEMENT_REMOVE_OBJECTS_FROM_BUFFER_SHADER_TYPE(bRemoveFromSameBuffer, PrimitiveType) \
	typedef TRemoveObjectsFromBufferCS<bRemoveFromSameBuffer, PrimitiveType> FRemoveObjectsFromBufferCS##bRemoveFromSameBuffer##PrimitiveType; \
	IMPLEMENT_SHADER_TYPE(template<>, FRemoveObjectsFromBufferCS##bRemoveFromSameBuffer##PrimitiveType, TEXT("/Engine/Private/DistanceFieldObjectCulling.usf"), TEXT("RemoveObjectsFromBufferCS"), SF_Compute);

IMPLEMENT_REMOVE_OBJECTS_FROM_BUFFER_SHADER_TYPE(true, DFPT_SignedDistanceField);
IMPLEMENT_REMOVE_OBJECTS_FROM_BUFFER_SHADER_TYPE(false, DFPT_SignedDistanceField);
IMPLEMENT_REMOVE_OBJECTS_FROM_BUFFER_SHADER_TYPE(true, DFPT_HeightField);

void FSurfelBufferAllocator::RemovePrimitive(const FPrimitiveSceneInfo* Primitive)
{
	FPrimitiveSurfelAllocation Allocation;

	if (Allocations.RemoveAndCopyValue(Primitive, Allocation))
	{
		bool bMergedWithExisting = false;

		FPrimitiveSurfelFreeEntry FreeEntry(Allocation.Offset, Allocation.GetTotalNumSurfels());

		// Note: only does one merge
		//@todo - keep free list sorted then can binary search
		for (int32 FreeIndex = 0; FreeIndex < FreeList.Num(); FreeIndex++)
		{
			if (FreeList[FreeIndex].Offset == FreeEntry.Offset + FreeEntry.NumSurfels)
			{
				FreeList[FreeIndex].Offset = FreeEntry.Offset;
				FreeList[FreeIndex].NumSurfels += FreeEntry.NumSurfels;
				bMergedWithExisting = true;
				break;
			}
			else if (FreeList[FreeIndex].Offset + FreeList[FreeIndex].NumSurfels == FreeEntry.Offset)
			{
				FreeList[FreeIndex].NumSurfels += FreeEntry.NumSurfels;
				bMergedWithExisting = true;
				break;
			}
		}

		if (!bMergedWithExisting)
		{
			FreeList.Add(FreeEntry);
		}
	}
}

void FSurfelBufferAllocator::AddPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo, int32 PrimitiveLOD0Surfels, int32 PrimitiveNumSurfels, int32 NumInstances)
{
	int32 BestFreeAllocationIndex = -1;

	for (int32 FreeIndex = 0; FreeIndex < FreeList.Num(); FreeIndex++)
	{
		const FPrimitiveSurfelFreeEntry& CurrentFreeEntry = FreeList[FreeIndex];

		if (CurrentFreeEntry.NumSurfels >= PrimitiveNumSurfels * NumInstances
			&& (BestFreeAllocationIndex == -1 
				|| CurrentFreeEntry.NumSurfels < FreeList[BestFreeAllocationIndex].NumSurfels))
		{
			BestFreeAllocationIndex = FreeIndex;
		}
	}

	if (BestFreeAllocationIndex != -1)
	{
		FPrimitiveSurfelFreeEntry FreeEntry = FreeList[BestFreeAllocationIndex];
						
		if (FreeEntry.NumSurfels == PrimitiveNumSurfels * NumInstances)
		{
			// Existing allocation matches exactly, remove it from the free list
			FreeList.RemoveAtSwap(BestFreeAllocationIndex);
		}
		else
		{
			// Replace with the remaining free range
			FreeList[BestFreeAllocationIndex] = FPrimitiveSurfelFreeEntry(FreeEntry.Offset + PrimitiveNumSurfels * NumInstances, FreeEntry.NumSurfels - PrimitiveNumSurfels * NumInstances);
		}

		Allocations.Add(PrimitiveSceneInfo, FPrimitiveSurfelAllocation(FreeEntry.Offset, PrimitiveLOD0Surfels, PrimitiveNumSurfels, NumInstances));
	}
	else
	{
		// Add a new allocation to the end of the buffer
		Allocations.Add(PrimitiveSceneInfo, FPrimitiveSurfelAllocation(NumSurfelsInBuffer, PrimitiveLOD0Surfels, PrimitiveNumSurfels, NumInstances));
		NumSurfelsInBuffer += PrimitiveNumSurfels * NumInstances;
	}
}

void UpdateGlobalDistanceFieldObjectRemoves(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	TArray<FIntRect> RemoveObjectIndices;

	if (DistanceFieldSceneData.PendingRemoveOperations.Num() > 0)
	{
		TArray<int32, SceneRenderingAllocator> PendingRemoveOperations;

		for (int32 RemoveIndex = 0; RemoveIndex < DistanceFieldSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			// Can't dereference the primitive here, it has already been deleted
			const FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].Primitive;
			DistanceFieldSceneData.SurfelAllocations.RemovePrimitive(Primitive);
			DistanceFieldSceneData.InstancedSurfelAllocations.RemovePrimitive(Primitive);
			const TArray<int32, TInlineAllocator<1>>& DistanceFieldInstanceIndices = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].DistanceFieldInstanceIndices;

			for (int32 RemoveInstanceIndex = 0; RemoveInstanceIndex < DistanceFieldInstanceIndices.Num(); RemoveInstanceIndex++)
			{
				const int32 InstanceIndex = DistanceFieldInstanceIndices[RemoveInstanceIndex];

				// InstanceIndex will be -1 with zero scale meshes
				if (InstanceIndex >= 0)
				{
					FGlobalDFCacheType CacheType = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].bOftenMoving ? GDF_Full : GDF_MostlyStatic;
					DistanceFieldSceneData.PrimitiveModifiedBounds[CacheType].Add(DistanceFieldSceneData.PrimitiveInstanceMapping[InstanceIndex].BoundingSphere);
					PendingRemoveOperations.Add(InstanceIndex);
				}
			}
		}

		DistanceFieldSceneData.PendingRemoveOperations.Reset();

		if (PendingRemoveOperations.Num() > 0)
		{
			check(DistanceFieldSceneData.NumObjectsInBuffer >= PendingRemoveOperations.Num());

			// Sort from smallest to largest
			PendingRemoveOperations.Sort();

			// We have multiple remove requests enqueued in PendingRemoveOperations, can only use the RemoveAtSwap version when there won't be collisions
			const bool bUseRemoveAtSwap = PendingRemoveOperations.Last() < DistanceFieldSceneData.NumObjectsInBuffer - PendingRemoveOperations.Num();

			FDistanceFieldObjectBuffers*& CurrentObjectBuffers = DistanceFieldSceneData.ObjectBuffers[DistanceFieldSceneData.ObjectBufferIndex];

			if (bUseRemoveAtSwap)
			{
				// Remove everything in parallel in the same buffer with a RemoveAtSwap algorithm
				for (int32 RemovePrimitiveIndex = 0; RemovePrimitiveIndex < PendingRemoveOperations.Num(); RemovePrimitiveIndex++)
				{
					DistanceFieldSceneData.NumObjectsInBuffer--;
					const int32 RemoveIndex = PendingRemoveOperations[RemovePrimitiveIndex];
					const int32 MoveFromIndex = DistanceFieldSceneData.NumObjectsInBuffer;

					check(RemoveIndex != MoveFromIndex);
					// Queue a compute shader move
					RemoveObjectIndices.Add(FIntRect(RemoveIndex, MoveFromIndex, 0, 0));

					// Fixup indices of the primitive that is being moved
					FPrimitiveAndInstance& PrimitiveAndInstanceBeingMoved = DistanceFieldSceneData.PrimitiveInstanceMapping[MoveFromIndex];
					check(PrimitiveAndInstanceBeingMoved.Primitive && PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices.Num() > 0);
					PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices[PrimitiveAndInstanceBeingMoved.InstanceIndex] = RemoveIndex;

					DistanceFieldSceneData.PrimitiveInstanceMapping.RemoveAtSwap(RemoveIndex);
				}
			}
			else
			{
				const double StartTime = FPlatformTime::Seconds();

				TArray<FPrimitiveAndInstance> OriginalPrimitiveInstanceMapping;

				// Have to copy the object data to allow parallel removing
				int32 NextObjectBufferIndex = (DistanceFieldSceneData.ObjectBufferIndex + 1) & 1;
				FDistanceFieldObjectBuffers*& NextObjectBuffers = DistanceFieldSceneData.ObjectBuffers[NextObjectBufferIndex];
				
				check(CurrentObjectBuffers != nullptr);

				DistanceFieldSceneData.ObjectBufferIndex = NextObjectBufferIndex;

				if (NextObjectBuffers != nullptr && NextObjectBuffers->MaxObjects < CurrentObjectBuffers->MaxObjects)
				{
					NextObjectBuffers->Release();
					delete NextObjectBuffers;
					NextObjectBuffers = nullptr;
				}

				if (NextObjectBuffers == nullptr)
				{
					NextObjectBuffers = new FDistanceFieldObjectBuffers();
					NextObjectBuffers->MaxObjects = CurrentObjectBuffers->MaxObjects;
					NextObjectBuffers->Initialize();
				}

				OriginalPrimitiveInstanceMapping = DistanceFieldSceneData.PrimitiveInstanceMapping;
				DistanceFieldSceneData.PrimitiveInstanceMapping.Reset();

				const int32 NumDestObjects = DistanceFieldSceneData.NumObjectsInBuffer - PendingRemoveOperations.Num();
				int32 SourceIndex = 0;
				int32 NextPendingRemoveIndex = 0;

				for (int32 DestinationIndex = 0; DestinationIndex < NumDestObjects; DestinationIndex++)
				{
					while (NextPendingRemoveIndex < PendingRemoveOperations.Num()
						&& PendingRemoveOperations[NextPendingRemoveIndex] == SourceIndex)
					{
						NextPendingRemoveIndex++;
						SourceIndex++;
					}

					// Queue a compute shader move
					RemoveObjectIndices.Add(FIntRect(DestinationIndex, SourceIndex, 0, 0));

					// Fixup indices of the primitive that is being moved
					FPrimitiveAndInstance& PrimitiveAndInstanceBeingMoved = OriginalPrimitiveInstanceMapping[SourceIndex];
					check(PrimitiveAndInstanceBeingMoved.Primitive && PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices.Num() > 0);
					PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices[PrimitiveAndInstanceBeingMoved.InstanceIndex] = DestinationIndex;

					check(DistanceFieldSceneData.PrimitiveInstanceMapping.Num() == DestinationIndex);
					DistanceFieldSceneData.PrimitiveInstanceMapping.Add(PrimitiveAndInstanceBeingMoved);

					SourceIndex++;
				}

				DistanceFieldSceneData.NumObjectsInBuffer = NumDestObjects;

				if (GAOLogObjectBufferReallocation)
				{
					const float ElapsedTime = (float)(FPlatformTime::Seconds() - StartTime);
					UE_LOG(LogDistanceField,Warning,TEXT("Global object buffer realloc %.3fs"), ElapsedTime);
				}

				/*
				// Have to remove one at a time while any entries to remove are at the end of the buffer
				DistanceFieldSceneData.NumObjectsInBuffer--;
				const int32 RemoveIndex = DistanceFieldSceneData.PendingRemoveOperations[ParallelConflictIndex];
				const int32 MoveFromIndex = DistanceFieldSceneData.NumObjectsInBuffer;

				if (RemoveIndex != MoveFromIndex)
				{
					// Queue a compute shader move
					RemoveObjectIndices.Add(FIntRect(RemoveIndex, MoveFromIndex, 0, 0));

					// Fixup indices of the primitive that is being moved
					FPrimitiveAndInstance& PrimitiveAndInstanceBeingMoved = DistanceFieldSceneData.PrimitiveInstanceMapping[MoveFromIndex];
					check(PrimitiveAndInstanceBeingMoved.Primitive && PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices.Num() > 0);
					PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices[PrimitiveAndInstanceBeingMoved.InstanceIndex] = RemoveIndex;
				}

				DistanceFieldSceneData.PrimitiveInstanceMapping.RemoveAtSwap(RemoveIndex);
				DistanceFieldSceneData.PendingRemoveOperations.RemoveAtSwap(ParallelConflictIndex);
				*/
			}

			PendingRemoveOperations.Reset();

			if (RemoveObjectIndices.Num() > 0)
			{
				if (RemoveObjectIndices.Num() > GDistanceFieldRemoveIndices.RemoveIndices.MaxElements)
				{
					GDistanceFieldRemoveIndices.RemoveIndices.MaxElements = RemoveObjectIndices.Num() * 5 / 4;
					GDistanceFieldRemoveIndices.RemoveIndices.Release();
					GDistanceFieldRemoveIndices.RemoveIndices.Initialize();
				}

				void* LockedBuffer = RHILockVertexBuffer(GDistanceFieldRemoveIndices.RemoveIndices.Buffer, 0, GDistanceFieldRemoveIndices.RemoveIndices.Buffer->GetSize(), RLM_WriteOnly);
				const uint32 MemcpySize = RemoveObjectIndices.GetTypeSize() * RemoveObjectIndices.Num();
				check(GDistanceFieldRemoveIndices.RemoveIndices.Buffer->GetSize() >= MemcpySize);
				FPlatformMemory::Memcpy(LockedBuffer, RemoveObjectIndices.GetData(), MemcpySize);
				RHIUnlockVertexBuffer(GDistanceFieldRemoveIndices.RemoveIndices.Buffer);

				if (bUseRemoveAtSwap)
				{
					TShaderMapRef<TRemoveObjectsFromBufferCS<true, DFPT_SignedDistanceField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, Scene, RemoveObjectIndices.Num(), GDistanceFieldRemoveIndices.RemoveIndices.BufferSRV, NULL, NULL);

					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(RemoveObjectIndices.Num(), UpdateObjectsGroupSize), 1, 1);
					ComputeShader->UnsetParameters(RHICmdList, Scene);
				}
				else
				{
					TShaderMapRef<TRemoveObjectsFromBufferCS<false, DFPT_SignedDistanceField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, Scene, RemoveObjectIndices.Num(), GDistanceFieldRemoveIndices.RemoveIndices.BufferSRV, CurrentObjectBuffers->Bounds.SRV, CurrentObjectBuffers->Data.SRV);

					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(RemoveObjectIndices.Num(), UpdateObjectsGroupSize), 1, 1);
					ComputeShader->UnsetParameters(RHICmdList, Scene);
				}
			}
		}
	}
}

void UpdateGlobalHeightFieldObjectRemoves(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
	TArray<FIntRect> DstSrcIndices;

	if (SceneData.PendingHeightFieldRemoveOps.Num())
	{
		TArray<int32, SceneRenderingAllocator> PendingRemoveObjectIndices;

		for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldRemoveOps.Num(); ++Idx)
		{
			const FHeightFieldPrimitiveRemoveInfo& RemoveInfo = SceneData.PendingHeightFieldRemoveOps[Idx];
			check(RemoveInfo.DistanceFieldInstanceIndices.Num() == 1);
			const int32 ObjectIdx = RemoveInfo.DistanceFieldInstanceIndices[0];

			if (ObjectIdx >= 0)
			{
				const FGlobalDFCacheType CacheType = RemoveInfo.bOftenMoving ? GDF_Full : GDF_MostlyStatic;
				SceneData.PrimitiveModifiedBounds[CacheType].Add(RemoveInfo.SphereBound);
				PendingRemoveObjectIndices.Add(ObjectIdx);
			}
		}

		SceneData.PendingHeightFieldRemoveOps.Reset();

		if (PendingRemoveObjectIndices.Num())
		{
			check(SceneData.NumHeightFieldObjectsInBuffer >= PendingRemoveObjectIndices.Num());
			check(SceneData.NumHeightFieldObjectsInBuffer == SceneData.HeightfieldPrimitives.Num());

			Algo::Sort(PendingRemoveObjectIndices);

			for (int32 Idx = 0; Idx < PendingRemoveObjectIndices.Num(); ++Idx)
			{
				const int32 LastIdx = PendingRemoveObjectIndices.Num() - 1;
				const int32 RemoveIdx = PendingRemoveObjectIndices[Idx];
				const int32 LastRemoveIdx = PendingRemoveObjectIndices[LastIdx];
				const int32 LastObjectIdx = --SceneData.NumHeightFieldObjectsInBuffer;

				if (LastRemoveIdx < LastObjectIdx)
				{
					DstSrcIndices.Add(FIntRect(RemoveIdx, LastObjectIdx, 0, 0));

					FPrimitiveSceneInfo* Primitive = SceneData.HeightfieldPrimitives[LastObjectIdx];
					check(Primitive && Primitive->DistanceFieldInstanceIndices.Num() == 1);
					Primitive->DistanceFieldInstanceIndices[0] = RemoveIdx;
					SceneData.HeightfieldPrimitives.RemoveAtSwap(RemoveIdx);
				}
				else
				{
					check(LastRemoveIdx == LastObjectIdx);
					SceneData.HeightfieldPrimitives.RemoveAt(LastObjectIdx);
					PendingRemoveObjectIndices.RemoveAt(LastIdx, 1, false);
					--Idx;
				}
			}
		}

		if (DstSrcIndices.Num())
		{
			FCPUUpdatedBuffer& RemoveIndices = GHeightFieldRemoveIndices.RemoveIndices;
			if (DstSrcIndices.Num() > RemoveIndices.MaxElements)
			{
				RemoveIndices.MaxElements = DstSrcIndices.Num() * 5 / 4;
				RemoveIndices.Release();
				RemoveIndices.Initialize();
			}

			void* LockedBuffer = RHILockVertexBuffer(RemoveIndices.Buffer, 0, RemoveIndices.Buffer->GetSize(), RLM_WriteOnly);
			const uint32 MemcpySize = DstSrcIndices.GetTypeSize() * DstSrcIndices.Num();
			check(RemoveIndices.Buffer->GetSize() >= MemcpySize);
			FMemory::Memcpy(LockedBuffer, DstSrcIndices.GetData(), MemcpySize);
			RHIUnlockVertexBuffer(RemoveIndices.Buffer);

			TShaderMapRef<TRemoveObjectsFromBufferCS<true, DFPT_HeightField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
			RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
			ComputeShader->SetParameters(RHICmdList, Scene, DstSrcIndices.Num(), RemoveIndices.BufferSRV, nullptr, nullptr);
			DispatchComputeShader(RHICmdList, ComputeShader, FMath::DivideAndRoundUp<uint32>(DstSrcIndices.Num(), UpdateObjectsGroupSize), 1, 1);
			ComputeShader->UnsetParameters(RHICmdList, Scene);
		}
	}
}

/** Gathers the information needed to represent a single object's distance field and appends it to the upload buffers. */
bool ProcessPrimitiveUpdate(
	bool bIsAddOperation,
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer& SceneRenderer, 
	FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	int32 OriginalNumObjects,
	FVector InvTextureDim,
	bool bPrepareForDistanceFieldGI, 
	bool bAnyViewEnabledDistanceCulling,
	TArray<FMatrix>& ObjectLocalToWorldTransforms,
	TArray<uint32>& UploadObjectIndices,
	TArray<FVector4>& UploadObjectData)
{
	FScene* Scene = SceneRenderer.Scene;
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	ObjectLocalToWorldTransforms.Reset();

	FBox LocalVolumeBounds;
	FVector2D DistanceMinMax;
	FIntVector BlockMin;
	FIntVector BlockSize;
	bool bBuiltAsIfTwoSided;
	bool bMeshWasPlane;
	float SelfShadowBias;
	bool bThrottled;
	PrimitiveSceneInfo->Proxy->GetDistancefieldAtlasData(LocalVolumeBounds, DistanceMinMax, BlockMin, BlockSize, bBuiltAsIfTwoSided, bMeshWasPlane, SelfShadowBias, ObjectLocalToWorldTransforms, bThrottled);

	if (bThrottled)
	{
		return false;
	}

	if (BlockMin.X >= 0 
		&& BlockMin.Y >= 0 
		&& BlockMin.Z >= 0 
		&& ObjectLocalToWorldTransforms.Num() > 0)
	{
		const float BoundingRadius = PrimitiveSceneInfo->Proxy->GetBounds().SphereRadius;
		const FGlobalDFCacheType CacheType = PrimitiveSceneInfo->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;

		// Proxy bounds are only useful if single instance
		if (ObjectLocalToWorldTransforms.Num() > 1 || BoundingRadius < GAOMaxObjectBoundingRadius)
		{
			FPrimitiveSurfelAllocation Allocation;
			FPrimitiveSurfelAllocation InstancedAllocation;
						
			if (bPrepareForDistanceFieldGI)
			{
				const FPrimitiveSurfelAllocation* AllocationPtr = Scene->DistanceFieldSceneData.SurfelAllocations.FindAllocation(PrimitiveSceneInfo);
				const FPrimitiveSurfelAllocation* InstancedAllocationPtr = Scene->DistanceFieldSceneData.InstancedSurfelAllocations.FindAllocation(PrimitiveSceneInfo);

				if (AllocationPtr)
				{
					checkSlow(InstancedAllocationPtr && InstancedAllocationPtr->NumInstances == ObjectLocalToWorldTransforms.Num());
					Allocation = *AllocationPtr;
					InstancedAllocation = *InstancedAllocationPtr;

					extern void GenerateSurfelRepresentation(FRHICommandListImmediate& RHICmdList, FSceneRenderer& Renderer, FViewInfo& View, FPrimitiveSceneInfo* PrimitiveSceneInfo, const FMatrix& Instance0Transform, FPrimitiveSurfelAllocation& Allocation);
					// @todo - support surfel generation without a view
					GenerateSurfelRepresentation(RHICmdList, SceneRenderer, SceneRenderer.Views[0], PrimitiveSceneInfo, ObjectLocalToWorldTransforms[0], Allocation);

					if (Allocation.NumSurfels == 0)
					{
						InstancedAllocation.NumSurfels = 0;
						InstancedAllocation.NumInstances = 0;
						InstancedAllocation.NumLOD0 = 0;
					}
				}
			}

			if (bIsAddOperation)
			{
				PrimitiveSceneInfo->DistanceFieldInstanceIndices.Empty(ObjectLocalToWorldTransforms.Num());
				PrimitiveSceneInfo->DistanceFieldInstanceIndices.AddZeroed(ObjectLocalToWorldTransforms.Num());
			}

			for (int32 TransformIndex = 0; TransformIndex < ObjectLocalToWorldTransforms.Num(); TransformIndex++)
			{
				FMatrix LocalToWorld = ObjectLocalToWorldTransforms[TransformIndex];
				const float MaxScale = LocalToWorld.GetMaximumAxisScale();

				// Skip degenerate primitives
				if (MaxScale > 0)
				{
					uint32 UploadIndex;

					if (bIsAddOperation)
					{
						UploadIndex = OriginalNumObjects + UploadObjectIndices.Num();
						DistanceFieldSceneData.NumObjectsInBuffer++;
					}
					else
					{
						UploadIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex];
					}

					UploadObjectIndices.Add(UploadIndex);

					if (bMeshWasPlane)
					{
						FVector LocalScales = LocalToWorld.GetScaleVector();
						FVector AbsLocalScales(FMath::Abs(LocalScales.X), FMath::Abs(LocalScales.Y), FMath::Abs(LocalScales.Z));
						float MidScale = FMath::Min(AbsLocalScales.X, AbsLocalScales.Y);
						float ScaleAdjust = FMath::Sign(LocalScales.Z) * MidScale / AbsLocalScales.Z;
						// The mesh was determined to be a plane flat in Z during the build process, so we can change the Z scale
						// Helps in cases with modular ground pieces with scales of (10, 10, 1) and some triangles just above Z=0
						LocalToWorld.SetAxis(2, LocalToWorld.GetScaledAxis(EAxis::Z) * ScaleAdjust);
					}

					const FMatrix VolumeToWorld = FScaleMatrix(LocalVolumeBounds.GetExtent()) 
						* FTranslationMatrix(LocalVolumeBounds.GetCenter())
						* LocalToWorld;

					const FVector4 ObjectBoundingSphere(VolumeToWorld.GetOrigin(), VolumeToWorld.GetScaleVector().Size());

					UploadObjectData.Add(ObjectBoundingSphere);

					const float MaxExtent = LocalVolumeBounds.GetExtent().GetMax();

					const FMatrix UniformScaleVolumeToWorld = FScaleMatrix(MaxExtent) 
						* FTranslationMatrix(LocalVolumeBounds.GetCenter())
						* LocalToWorld;

					const FVector InvBlockSize(1.0f / BlockSize.X, 1.0f / BlockSize.Y, 1.0f / BlockSize.Z);

					//float3 VolumeUV = (VolumePosition / LocalPositionExtent * .5f * UVScale + .5f * UVScale + UVAdd;
					const FVector LocalPositionExtent = LocalVolumeBounds.GetExtent() / FVector(MaxExtent);
					const FVector UVScale = FVector(BlockSize) * InvTextureDim;
					const float VolumeScale = UniformScaleVolumeToWorld.GetMaximumAxisScale();

					const FMatrix WorldToVolumeT = UniformScaleVolumeToWorld.Inverse().GetTransposed();
					// WorldToVolumeT
					UploadObjectData.Add(*(FVector4*)&WorldToVolumeT.M[0]);
					UploadObjectData.Add(*(FVector4*)&WorldToVolumeT.M[1]);
					UploadObjectData.Add(*(FVector4*)&WorldToVolumeT.M[2]);

					const float OftenMovingValue = CacheType == GDF_Full ? 1.0f : 0.0f;

					// Clamp to texel center by subtracting a half texel in the [-1,1] position space
					// LocalPositionExtent
					UploadObjectData.Add(FVector4(LocalPositionExtent - InvBlockSize, OftenMovingValue));

					// UVScale, VolumeScale and sign gives bGeneratedAsTwoSided
					const float WSign = bBuiltAsIfTwoSided ? -1 : 1;
					UploadObjectData.Add(FVector4(FVector(BlockSize) * InvTextureDim * .5f / LocalPositionExtent, WSign * VolumeScale));

					// UVAdd
					UploadObjectData.Add(FVector4(FVector(BlockMin) * InvTextureDim + .5f * UVScale, SelfShadowBias));

					// xy - DistanceFieldMAD
					// zw - MinDrawDistance^2, MaxDrawDistance^2
					// [0, 1] -> [MinVolumeDistance, MaxVolumeDistance]
					const int32 PrimIdx = PrimitiveSceneInfo->GetIndex();
					const FPrimitiveBounds& PrimBounds = Scene->PrimitiveBounds[PrimIdx];
					float MinDrawDist2 = PrimBounds.MinDrawDistanceSq;
					// For IEEE compatible machines, float operations goes to inf if overflow
					// In this case, it will effectively disable max draw distance culling
					float MaxDrawDist = FMath::Max(PrimBounds.MaxCullDistance, 0.f) * GetCachedScalabilityCVars().ViewDistanceScale;
#if WITH_EDITOR
					if (!bAnyViewEnabledDistanceCulling)
					{
						MinDrawDist2 = 0.f;
						MaxDrawDist = 0.f;
					}
#endif
					// This is needed to bypass the check Nan/Inf behavior of FVector4.
					// If the check is turned on, FVector4 constructor automatically converts
					// the FVector4 being constructed to (0, 0, 0, 1) when any of inputs
					// to the constructor contains Nan/Inf
					UploadObjectData.AddUninitialized();
					FVector4& TmpVec4 = UploadObjectData.Last();
					TmpVec4.X = DistanceMinMax.Y - DistanceMinMax.X;
					TmpVec4.Y = DistanceMinMax.X;
					TmpVec4.Z = MinDrawDist2;
					TmpVec4.W = MaxDrawDist * MaxDrawDist;

					UploadObjectData.Add(*(FVector4*)&UniformScaleVolumeToWorld.M[0]);
					UploadObjectData.Add(*(FVector4*)&UniformScaleVolumeToWorld.M[1]);
					UploadObjectData.Add(*(FVector4*)&UniformScaleVolumeToWorld.M[2]);

					FMatrix LocalToWorldT = LocalToWorld.GetTransposed();
					UploadObjectData.Add(*(FVector4*)&LocalToWorldT.M[0]);
					UploadObjectData.Add(*(FVector4*)&LocalToWorldT.M[1]);
					UploadObjectData.Add(*(FVector4*)&LocalToWorldT.M[2]);

					UploadObjectData.Add(FVector4(Allocation.Offset, Allocation.NumLOD0, Allocation.NumSurfels, InstancedAllocation.Offset + InstancedAllocation.NumSurfels * TransformIndex));

					FMatrix VolumeToWorldT = VolumeToWorld.GetTransposed();
					UploadObjectData.Add(*(FVector4*)&VolumeToWorldT.M[0]);
					UploadObjectData.Add(*(FVector4*)&VolumeToWorldT.M[1]);
					UploadObjectData.Add(*(FVector4*)&VolumeToWorldT.M[2]);

					checkSlow(UploadObjectData.Num() % UploadObjectDataStride == 0);

					if (bIsAddOperation)
					{
						const int32 AddIndex = UploadIndex;
						DistanceFieldSceneData.PrimitiveInstanceMapping.Add(FPrimitiveAndInstance(ObjectBoundingSphere, PrimitiveSceneInfo, TransformIndex));
						PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex] = AddIndex;
					}
					else 
					{
						// InstanceIndex will be -1 with zero scale meshes
						const int32 InstanceIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex];
						if (InstanceIndex >= 0)
						{
							// For an update transform we have to dirty the previous bounds and the new bounds, in case of large movement (teleport)
							DistanceFieldSceneData.PrimitiveModifiedBounds[CacheType].Add(DistanceFieldSceneData.PrimitiveInstanceMapping[InstanceIndex].BoundingSphere);
							DistanceFieldSceneData.PrimitiveInstanceMapping[InstanceIndex].BoundingSphere = ObjectBoundingSphere;
						}
					}

					DistanceFieldSceneData.PrimitiveModifiedBounds[CacheType].Add(ObjectBoundingSphere);

					extern int32 GAOLogGlobalDistanceFieldModifiedPrimitives;

					if (GAOLogGlobalDistanceFieldModifiedPrimitives)
					{
						UE_LOG(LogDistanceField,Log,TEXT("Global Distance Field %s primitive %s %s %s bounding radius %.1f"), PrimitiveSceneInfo->Proxy->IsOftenMoving() ? TEXT("CACHED") : TEXT("Movable"), (bIsAddOperation ? TEXT("add") : TEXT("update")), *PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(), *PrimitiveSceneInfo->Proxy->GetResourceName().ToString(), BoundingRadius);
					}
				}
				else if (bIsAddOperation)
				{
					// Set to -1 for zero scale meshes
					PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex] = -1;
				}
			}
		}
		else
		{
			UE_LOG(LogDistanceField,Log,TEXT("Primitive %s %s excluded due to bounding radius %f"), *PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(), *PrimitiveSceneInfo->Proxy->GetResourceName().ToString(), BoundingRadius);
		}
	}
	return true;
}

bool ProcessHeightFieldPrimitiveUpdate(
	bool bIsAddOperation,
	FRHICommandListImmediate& RHICmdList,
	FScene* Scene,
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	int32 OriginalNumObjects,
	TArray<uint32>& UploadObjectIndices,
	TArray<FVector4>& UploadObjectData)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;

	UTexture2D* HeightNormalTexture;
	UTexture2D* DiffuseColorTexture;
	UTexture2D* VisibilityTexture;
	FHeightfieldComponentDescription HeightFieldCompDesc(PrimitiveSceneInfo->Proxy->GetLocalToWorld());
	PrimitiveSceneInfo->Proxy->GetHeightfieldRepresentation(HeightNormalTexture, DiffuseColorTexture, VisibilityTexture, HeightFieldCompDesc);

	const uint32 Handle = GHeightFieldTextureAtlas.GetAllocationHandle(HeightNormalTexture);
	if (Handle == INDEX_NONE)
	{
		return false;
	}

	uint32 UploadIdx;
	if (bIsAddOperation)
	{
		++SceneData.NumHeightFieldObjectsInBuffer;
		SceneData.HeightfieldPrimitives.Add(PrimitiveSceneInfo);

		const FGlobalDFCacheType CacheType = PrimitiveSceneInfo->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
		const FBoxSphereBounds Bounds = PrimitiveSceneInfo->Proxy->GetBounds();
		SceneData.PrimitiveModifiedBounds[CacheType].Add(FVector4(Bounds.Origin, Bounds.SphereRadius));

		UploadIdx = OriginalNumObjects + UploadObjectIndices.Num();
		PrimitiveSceneInfo->DistanceFieldInstanceIndices.Empty(1);
		PrimitiveSceneInfo->DistanceFieldInstanceIndices.Add(UploadIdx);
	}
	else
	{
		UploadIdx = PrimitiveSceneInfo->DistanceFieldInstanceIndices[0];
	}

	UploadObjectIndices.Add(UploadIdx);

	const FBoxSphereBounds& Bounds = PrimitiveSceneInfo->Proxy->GetBounds();
	const FBox BoxBound = Bounds.GetBox();
	UploadObjectData.Add(FVector4(BoxBound.GetCenter(), Bounds.SphereRadius));
	UploadObjectData.Add(FVector4(BoxBound.GetExtent(), 0.f));

	const FMatrix& LocalToWorld = HeightFieldCompDesc.LocalToWorld;
	check(LocalToWorld.GetMaximumAxisScale() > 0.f);
	const FMatrix WorldToLocalT = LocalToWorld.Inverse().GetTransposed();
	UploadObjectData.Add(*(const FVector4*)&WorldToLocalT.M[0]);
	UploadObjectData.Add(*(const FVector4*)&WorldToLocalT.M[1]);
	UploadObjectData.Add(*(const FVector4*)&WorldToLocalT.M[2]);

	const FIntRect& HeightFieldRect = HeightFieldCompDesc.HeightfieldRect;
	const float WorldToLocalScale = FMath::Min3(
		WorldToLocalT.GetColumn(0).Size(),
		WorldToLocalT.GetColumn(1).Size(),
		WorldToLocalT.GetColumn(2).Size());
	UploadObjectData.Add(FVector4(HeightFieldRect.Width(), HeightFieldRect.Height(), WorldToLocalScale, 0.f));

	const FVector4& HeightFieldScaleBias = HeightFieldCompDesc.HeightfieldScaleBias;
	check(HeightFieldScaleBias.Y >= 0.f && HeightFieldScaleBias.Z >= 0.f && HeightFieldScaleBias.W >= 0.f);
	const FVector4 AllocationScaleBias = GHeightFieldTextureAtlas.GetAllocationScaleBias(Handle);
	UploadObjectData.Add(FVector4(
		FMath::Abs(HeightFieldScaleBias.X) * AllocationScaleBias.X,
		HeightFieldScaleBias.Y * AllocationScaleBias.Y,
		HeightFieldScaleBias.Z * AllocationScaleBias.X + AllocationScaleBias.Z,
		HeightFieldScaleBias.W * AllocationScaleBias.Y + AllocationScaleBias.W));

	FVector4 VisUVScaleBias(ForceInitToZero);
	if (VisibilityTexture)
	{
		const uint32 VisHandle = GHFVisibilityTextureAtlas.GetAllocationHandle(VisibilityTexture);
		if (VisHandle != INDEX_NONE)
		{
			const FVector4 ScaleBias = GHFVisibilityTextureAtlas.GetAllocationScaleBias(VisHandle);
			VisUVScaleBias = FVector4(1.f / HeightFieldRect.Width() * ScaleBias.X, 1.f / HeightFieldRect.Height() * ScaleBias.Y, ScaleBias.Z, ScaleBias.W);
		}
	}
	UploadObjectData.Add(VisUVScaleBias);

	check(!(UploadObjectData.Num() % UploadHeightFieldObjectDataStride));

	return true;
}

bool bVerifySceneIntegrity = false;

void FDeferredShadingSceneRenderer::UpdateGlobalDistanceFieldObjectBuffers(FRHICommandListImmediate& RHICmdList)
{
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
		&& (DistanceFieldSceneData.HasPendingOperations() ||
			DistanceFieldSceneData.PendingThrottledOperations.Num() > 0 ||
			DistanceFieldSceneData.AtlasGeneration != GDistanceFieldVolumeTextureAtlas.GetGeneration()))
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateObjectData);
		// Multi-GPU support : Updating on all GPUs may be inefficient for AFR. Work is
		// wasted for any objects that update on consecutive frames.
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());
		SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneObjectData);

		if (DistanceFieldSceneData.ObjectBuffers[DistanceFieldSceneData.ObjectBufferIndex] == nullptr)
		{
			DistanceFieldSceneData.ObjectBuffers[DistanceFieldSceneData.ObjectBufferIndex] = new FDistanceFieldObjectBuffers();
		}

		if (!DistanceFieldSceneData.SurfelBuffers)
		{
			DistanceFieldSceneData.SurfelBuffers = new FSurfelBuffers();
		}

		if (!DistanceFieldSceneData.InstancedSurfelBuffers)
		{
			DistanceFieldSceneData.InstancedSurfelBuffers = new FInstancedSurfelBuffers();
		}

		if (DistanceFieldSceneData.PendingAddOperations.Num() > 0)
		{
			DistanceFieldSceneData.PendingThrottledOperations.Reserve(DistanceFieldSceneData.PendingThrottledOperations.Num() + DistanceFieldSceneData.PendingAddOperations.Num());
		}

		DistanceFieldSceneData.PendingAddOperations.Append(DistanceFieldSceneData.PendingThrottledOperations);
		DistanceFieldSceneData.PendingThrottledOperations.Reset();

		if (DistanceFieldSceneData.AtlasGeneration != GDistanceFieldVolumeTextureAtlas.GetGeneration())
		{
			DistanceFieldSceneData.AtlasGeneration = GDistanceFieldVolumeTextureAtlas.GetGeneration();

			for (int32 PrimitiveInstanceIndex = 0; PrimitiveInstanceIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num(); PrimitiveInstanceIndex++)
			{
				FPrimitiveAndInstance& PrimitiveInstance = DistanceFieldSceneData.PrimitiveInstanceMapping[PrimitiveInstanceIndex];

				// Queue an update of all primitives, since the atlas layout has changed
				if (PrimitiveInstance.InstanceIndex == 0
					&& !DistanceFieldSceneData.HasPendingRemovePrimitive(PrimitiveInstance.Primitive)
					&& !DistanceFieldSceneData.PendingAddOperations.Contains(PrimitiveInstance.Primitive)
					&& !DistanceFieldSceneData.PendingUpdateOperations.Contains(PrimitiveInstance.Primitive))
				{
					DistanceFieldSceneData.PendingUpdateOperations.Add(PrimitiveInstance.Primitive);
				}
			}
		}

		// Process removes before adds, as the adds will overwrite primitive allocation info in DistanceFieldSceneData.SurfelAllocations
		UpdateGlobalDistanceFieldObjectRemoves(RHICmdList, Scene);

		extern int32 GVPLMeshGlobalIllumination;
		TArray<uint32> UploadObjectIndices;
		TArray<FVector4> UploadObjectData;
		const bool bPrepareForDistanceFieldGI = GVPLMeshGlobalIllumination && SupportsDistanceFieldGI(Scene->GetFeatureLevel(), Scene->GetShaderPlatform());

		if (DistanceFieldSceneData.PendingAddOperations.Num() > 0 || DistanceFieldSceneData.PendingUpdateOperations.Num() > 0)
		{
			TArray<FMatrix> ObjectLocalToWorldTransforms;

			const int32 NumUploadOperations = DistanceFieldSceneData.PendingAddOperations.Num() + DistanceFieldSceneData.PendingUpdateOperations.Num();
			UploadObjectData.Empty(NumUploadOperations * UploadObjectDataStride);
			UploadObjectIndices.Empty(NumUploadOperations);

			const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
			const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);

			int32 OriginalNumObjects = DistanceFieldSceneData.NumObjectsInBuffer;
			int32 OriginalNumSurfels = DistanceFieldSceneData.SurfelAllocations.GetNumSurfelsInBuffer();
			int32 OriginalNumInstancedSurfels = DistanceFieldSceneData.InstancedSurfelAllocations.GetNumSurfelsInBuffer();

			if (bPrepareForDistanceFieldGI)
			{
				for (int32 UploadPrimitiveIndex = 0; UploadPrimitiveIndex < DistanceFieldSceneData.PendingAddOperations.Num(); UploadPrimitiveIndex++)
				{
					FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingAddOperations[UploadPrimitiveIndex];

					int32 NumInstances = 0;
					float BoundsSurfaceArea = 0;
					PrimitiveSceneInfo->Proxy->GetDistanceFieldInstanceInfo(NumInstances, BoundsSurfaceArea);

					extern void ComputeNumSurfels(float BoundsSurfaceArea, int32& PrimitiveNumSurfels, int32& PrimitiveLOD0Surfels);
					int32 PrimitiveNumSurfels;
					int32 PrimitiveLOD0Surfels;
					ComputeNumSurfels(BoundsSurfaceArea, PrimitiveNumSurfels, PrimitiveLOD0Surfels);

					if (PrimitiveNumSurfels > 0 && NumInstances > 0)
					{
						const int32 PrimitiveTotalNumSurfels = PrimitiveNumSurfels * NumInstances;

						if (PrimitiveNumSurfels > 5000)
						{
							UE_LOG(LogDistanceField, Warning, TEXT("Primitive %s %s used %u Surfels"), *PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(), *PrimitiveSceneInfo->Proxy->GetResourceName().ToString(), PrimitiveNumSurfels);
						}

						DistanceFieldSceneData.SurfelAllocations.AddPrimitive(PrimitiveSceneInfo, PrimitiveLOD0Surfels, PrimitiveNumSurfels, 1);
						DistanceFieldSceneData.InstancedSurfelAllocations.AddPrimitive(PrimitiveSceneInfo, PrimitiveLOD0Surfels, PrimitiveNumSurfels, NumInstances);
					}
				}

				if (DistanceFieldSceneData.SurfelBuffers->MaxSurfels < DistanceFieldSceneData.SurfelAllocations.GetNumSurfelsInBuffer())
				{
					if (DistanceFieldSceneData.SurfelBuffers->MaxSurfels > 0)
					{
						// Realloc
						FSurfelBuffers* NewSurfelBuffers = new FSurfelBuffers();
						NewSurfelBuffers->MaxSurfels = DistanceFieldSceneData.SurfelAllocations.GetNumSurfelsInBuffer() * 5 / 4;
						NewSurfelBuffers->Initialize();

						{
							TShaderMapRef<FCopySurfelBufferCS> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
							RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
							ComputeShader->SetParameters(RHICmdList, *(DistanceFieldSceneData.SurfelBuffers), *(DistanceFieldSceneData.InstancedSurfelBuffers), *NewSurfelBuffers, OriginalNumSurfels);

							DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(OriginalNumSurfels, UpdateObjectsGroupSize), 1, 1);
							ComputeShader->UnsetParameters(RHICmdList, *NewSurfelBuffers);
						}

						DistanceFieldSceneData.SurfelBuffers->Release();
						delete DistanceFieldSceneData.SurfelBuffers;
						DistanceFieldSceneData.SurfelBuffers = NewSurfelBuffers;
					}
					else
					{
						// First time allocate
						DistanceFieldSceneData.SurfelBuffers->MaxSurfels = DistanceFieldSceneData.SurfelAllocations.GetNumSurfelsInBuffer() * 5 / 4;
						DistanceFieldSceneData.SurfelBuffers->Initialize();
					}
				}

				if (DistanceFieldSceneData.InstancedSurfelBuffers->MaxSurfels < DistanceFieldSceneData.InstancedSurfelAllocations.GetNumSurfelsInBuffer())
				{
					if (DistanceFieldSceneData.InstancedSurfelBuffers->MaxSurfels > 0)
					{
						// Realloc
						FInstancedSurfelBuffers* NewInstancedSurfelBuffers = new FInstancedSurfelBuffers();
						NewInstancedSurfelBuffers->MaxSurfels = DistanceFieldSceneData.InstancedSurfelAllocations.GetNumSurfelsInBuffer() * 5 / 4;
						NewInstancedSurfelBuffers->Initialize();

						{
							TShaderMapRef<FCopyVPLFluxBufferCS> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
							RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
							ComputeShader->SetParameters(RHICmdList, *(DistanceFieldSceneData.SurfelBuffers), *(DistanceFieldSceneData.InstancedSurfelBuffers), *NewInstancedSurfelBuffers, OriginalNumInstancedSurfels);

							DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(OriginalNumInstancedSurfels, UpdateObjectsGroupSize), 1, 1);
							ComputeShader->UnsetParameters(RHICmdList, *NewInstancedSurfelBuffers);
						}

						DistanceFieldSceneData.InstancedSurfelBuffers->Release();
						delete DistanceFieldSceneData.InstancedSurfelBuffers;
						DistanceFieldSceneData.InstancedSurfelBuffers = NewInstancedSurfelBuffers;
					}
					else
					{
						// First time allocate
						DistanceFieldSceneData.InstancedSurfelBuffers->MaxSurfels = DistanceFieldSceneData.InstancedSurfelAllocations.GetNumSurfelsInBuffer() * 5 / 4;
						DistanceFieldSceneData.InstancedSurfelBuffers->Initialize();
					}
				}
			}

			bool bAnyViewEnabledDistanceCulling = !WITH_EDITOR;
#if WITH_EDITOR
			for (const FViewInfo& ViewInfo : Views)
			{
				if (!ViewInfo.Family->EngineShowFlags.DistanceCulledPrimitives)
				{
					bAnyViewEnabledDistanceCulling = true;
					break;
				}
			}
#endif

			for (int32 UploadPrimitiveIndex = 0; UploadPrimitiveIndex < DistanceFieldSceneData.PendingAddOperations.Num(); UploadPrimitiveIndex++)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingAddOperations[UploadPrimitiveIndex];

				if (!ProcessPrimitiveUpdate(
					true,
					RHICmdList,
					*this,
					PrimitiveSceneInfo,
					OriginalNumObjects,
					InvTextureDim,
					bPrepareForDistanceFieldGI,
					bAnyViewEnabledDistanceCulling,
					ObjectLocalToWorldTransforms,
					UploadObjectIndices,
					UploadObjectData))
				{
					DistanceFieldSceneData.PendingThrottledOperations.Add(PrimitiveSceneInfo);
				}
			}

			for (TSet<FPrimitiveSceneInfo*>::TIterator It(DistanceFieldSceneData.PendingUpdateOperations); It; ++It)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;

				ProcessPrimitiveUpdate(
					false,
					RHICmdList,
					*this,
					PrimitiveSceneInfo,
					OriginalNumObjects,
					InvTextureDim,
					bPrepareForDistanceFieldGI,
					bAnyViewEnabledDistanceCulling,
					ObjectLocalToWorldTransforms,
					UploadObjectIndices,
					UploadObjectData);
			}

			DistanceFieldSceneData.PendingAddOperations.Reset();
			DistanceFieldSceneData.PendingUpdateOperations.Empty();
			if (DistanceFieldSceneData.PendingThrottledOperations.Num() == 0)
			{
				DistanceFieldSceneData.PendingThrottledOperations.Empty();
			}

			FDistanceFieldObjectBuffers*& ObjectBuffers = DistanceFieldSceneData.ObjectBuffers[DistanceFieldSceneData.ObjectBufferIndex];

			if (ObjectBuffers->MaxObjects < DistanceFieldSceneData.NumObjectsInBuffer)
			{
				if (ObjectBuffers->MaxObjects > 0)
				{
					// Realloc
					FDistanceFieldObjectBuffers* NewObjectBuffers = new FDistanceFieldObjectBuffers();
					NewObjectBuffers->MaxObjects = DistanceFieldSceneData.NumObjectsInBuffer * 5 / 4;
					NewObjectBuffers->Initialize();

					{
						TShaderMapRef<TCopyObjectBufferCS<DFPT_SignedDistanceField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
						ComputeShader->SetParameters(RHICmdList, *ObjectBuffers, *NewObjectBuffers, OriginalNumObjects);

						DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(OriginalNumObjects, UpdateObjectsGroupSize), 1, 1);
						ComputeShader->UnsetParameters(RHICmdList, *NewObjectBuffers);
					}

					ObjectBuffers->Release();
					delete ObjectBuffers;
					ObjectBuffers = NewObjectBuffers;
				}
				else
				{
					// First time allocate
					ObjectBuffers->MaxObjects = DistanceFieldSceneData.NumObjectsInBuffer * 5 / 4;
					ObjectBuffers->Initialize();
				}
			}
		}

		if (UploadObjectIndices.Num() > 0)
		{
			if (UploadObjectIndices.Num() > GDistanceFieldUploadIndices.UploadIndices.MaxElements
				// Shrink if very large
				|| (GDistanceFieldUploadIndices.UploadIndices.MaxElements > 1000 && GDistanceFieldUploadIndices.UploadIndices.MaxElements > UploadObjectIndices.Num() * 2))
			{
				GDistanceFieldUploadIndices.UploadIndices.MaxElements = UploadObjectIndices.Num() * 5 / 4;
				GDistanceFieldUploadIndices.UploadIndices.Release();
				GDistanceFieldUploadIndices.UploadIndices.Initialize();

				GDistanceFieldUploadData.UploadData.MaxElements = UploadObjectIndices.Num() * 5 / 4;
				GDistanceFieldUploadData.UploadData.Release();
				GDistanceFieldUploadData.UploadData.Initialize();
			}

			void* LockedBuffer = RHILockVertexBuffer(GDistanceFieldUploadIndices.UploadIndices.Buffer, 0, GDistanceFieldUploadIndices.UploadIndices.Buffer->GetSize(), RLM_WriteOnly);
			const uint32 MemcpySize = UploadObjectIndices.GetTypeSize() * UploadObjectIndices.Num();
			check(GDistanceFieldUploadIndices.UploadIndices.Buffer->GetSize() >= MemcpySize);
			FPlatformMemory::Memcpy(LockedBuffer, UploadObjectIndices.GetData(), MemcpySize);
			RHIUnlockVertexBuffer(GDistanceFieldUploadIndices.UploadIndices.Buffer);

			LockedBuffer = RHILockVertexBuffer(GDistanceFieldUploadData.UploadData.Buffer, 0, GDistanceFieldUploadData.UploadData.Buffer->GetSize(), RLM_WriteOnly);
			const uint32 MemcpySize2 = UploadObjectData.GetTypeSize() * UploadObjectData.Num();
			check(GDistanceFieldUploadData.UploadData.Buffer->GetSize() >= MemcpySize2);
			FPlatformMemory::Memcpy(LockedBuffer, UploadObjectData.GetData(), MemcpySize2);
			RHIUnlockVertexBuffer(GDistanceFieldUploadData.UploadData.Buffer);

			{
				TShaderMapRef<TUploadObjectsToBufferCS<DFPT_SignedDistanceField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
				ComputeShader->SetParameters(RHICmdList, Scene, UploadObjectIndices.Num(), GDistanceFieldUploadIndices.UploadIndices.BufferSRV, GDistanceFieldUploadData.UploadData.BufferSRV);

				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp<uint32>(UploadObjectIndices.Num(), UpdateObjectsGroupSize), 1, 1);
				ComputeShader->UnsetParameters(RHICmdList, Scene);
			}
		}

		check(DistanceFieldSceneData.NumObjectsInBuffer == DistanceFieldSceneData.PrimitiveInstanceMapping.Num());

		if (bVerifySceneIntegrity)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateObjectData_VerifyIntegrity);
			DistanceFieldSceneData.VerifyIntegrity();
		}
	}
}

void FDeferredShadingSceneRenderer::UpdateGlobalHeightFieldObjectBuffers(FRHICommandListImmediate& RHICmdList)
{
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (GHeightFieldTextureAtlas.GetAtlasTexture()
		&& (DistanceFieldSceneData.HasPendingHeightFieldOperations()
			|| DistanceFieldSceneData.HeightFieldAtlasGeneration != GHeightFieldTextureAtlas.GetGeneration()
			|| DistanceFieldSceneData.HFVisibilityAtlasGenerattion != GHFVisibilityTextureAtlas.GetGeneration()))
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateHeightFieldSceneObjectData);
		SCOPED_DRAW_EVENT(RHICmdList, UpdateHeightFieldSceneObjectData);

		if (!DistanceFieldSceneData.HeightFieldObjectBuffers)
		{
			AddOrRemoveSceneHeightFieldPrimitives(true);

			for (int32 Idx = 0; Idx < DistanceFieldSceneData.HeightfieldPrimitives.Num(); ++Idx)
			{
				FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.HeightfieldPrimitives[Idx];
				check(!DistanceFieldSceneData.PendingHeightFieldAddOps.Contains(Primitive));
				DistanceFieldSceneData.PendingHeightFieldAddOps.Add(Primitive);
			}
			DistanceFieldSceneData.HeightfieldPrimitives.Reset();
			DistanceFieldSceneData.HeightFieldObjectBuffers = new FHeightFieldObjectBuffers;
		}

		if (DistanceFieldSceneData.HeightFieldAtlasGeneration != GHeightFieldTextureAtlas.GetGeneration()
			|| DistanceFieldSceneData.HFVisibilityAtlasGenerattion != GHFVisibilityTextureAtlas.GetGeneration())
		{
			DistanceFieldSceneData.HeightFieldAtlasGeneration = GHeightFieldTextureAtlas.GetGeneration();
			DistanceFieldSceneData.HFVisibilityAtlasGenerattion = GHFVisibilityTextureAtlas.GetGeneration();

			for (int32 Idx = 0; Idx < DistanceFieldSceneData.HeightfieldPrimitives.Num(); ++Idx)
			{
				FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.HeightfieldPrimitives[Idx];

				if (!DistanceFieldSceneData.HasPendingRemoveHeightFieldPrimitive(Primitive)
					&& !DistanceFieldSceneData.PendingHeightFieldAddOps.Contains(Primitive)
					&& !DistanceFieldSceneData.PendingHeightFieldUpdateOps.Contains(Primitive))
				{
					DistanceFieldSceneData.PendingHeightFieldUpdateOps.Add(Primitive);
				}
			}
		}

		UpdateGlobalHeightFieldObjectRemoves(RHICmdList, Scene);

		if (DistanceFieldSceneData.PendingHeightFieldAddOps.Num() || DistanceFieldSceneData.PendingHeightFieldUpdateOps.Num())
		{
			const int32 NumAddOps = DistanceFieldSceneData.PendingHeightFieldAddOps.Num();
			const int32 NumUpdateOps = DistanceFieldSceneData.PendingHeightFieldUpdateOps.Num();
			const int32 NumUploadOps = NumAddOps + NumUpdateOps;
			const int32 OriginalNumObjects = DistanceFieldSceneData.NumHeightFieldObjectsInBuffer;
			TArray<uint32> UploadHeightFieldObjectIndices;
			TArray<FVector4> UploadHeightFieldObjectData;

			UploadHeightFieldObjectIndices.Empty(NumUploadOps);
			UploadHeightFieldObjectData.Empty(NumUploadOps * UploadHeightFieldObjectDataStride);

			for (int32 Idx = 0; Idx < NumAddOps; ++Idx)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingHeightFieldAddOps[Idx];
				ProcessHeightFieldPrimitiveUpdate(true, RHICmdList, Scene, PrimitiveSceneInfo, OriginalNumObjects, UploadHeightFieldObjectIndices, UploadHeightFieldObjectData);
			}

			for (int32 Idx = 0; Idx < NumUpdateOps; ++Idx)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingHeightFieldUpdateOps[Idx];
				ProcessHeightFieldPrimitiveUpdate(false, RHICmdList, Scene, PrimitiveSceneInfo, OriginalNumObjects, UploadHeightFieldObjectIndices, UploadHeightFieldObjectData);
			}

			DistanceFieldSceneData.PendingHeightFieldAddOps.Reset();
			DistanceFieldSceneData.PendingHeightFieldUpdateOps.Empty();

			FHeightFieldObjectBuffers*& ObjectBuffers = DistanceFieldSceneData.HeightFieldObjectBuffers;

			if (ObjectBuffers->MaxObjects < DistanceFieldSceneData.NumHeightFieldObjectsInBuffer)
			{
				if (ObjectBuffers->MaxObjects > 0)
				{
					FHeightFieldObjectBuffers* NewObjectBuffers = new FHeightFieldObjectBuffers;
					NewObjectBuffers->MaxObjects = DistanceFieldSceneData.NumHeightFieldObjectsInBuffer * 5 / 4;
					NewObjectBuffers->Initialize();

					TShaderMapRef<TCopyObjectBufferCS<DFPT_HeightField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, *ObjectBuffers, *NewObjectBuffers, OriginalNumObjects);
					DispatchComputeShader(RHICmdList, ComputeShader, FMath::DivideAndRoundUp<uint32>(OriginalNumObjects, UpdateObjectsGroupSize), 1, 1);
					ComputeShader->UnsetParameters(RHICmdList, *NewObjectBuffers);

					ObjectBuffers->Release();
					delete ObjectBuffers;
					ObjectBuffers = NewObjectBuffers;
				}
				else
				{
					ObjectBuffers->MaxObjects = DistanceFieldSceneData.NumHeightFieldObjectsInBuffer * 5 / 4;
					ObjectBuffers->Initialize();
				}
			}

			const int32 NumObjectsToUpload = UploadHeightFieldObjectIndices.Num();

			if (NumObjectsToUpload)
			{
				if (NumObjectsToUpload > GHeightFieldUploadIndices.UploadIndices.MaxElements
					|| (GHeightFieldUploadIndices.UploadIndices.MaxElements > 1000 && GHeightFieldUploadIndices.UploadIndices.MaxElements > NumObjectsToUpload * 2))
				{
					GHeightFieldUploadIndices.UploadIndices.MaxElements = NumObjectsToUpload * 5 / 4;
					GHeightFieldUploadIndices.UploadIndices.Release();
					GHeightFieldUploadIndices.UploadIndices.Initialize();

					GHeightFieldUploadData.UploadData.MaxElements = NumObjectsToUpload * 5 / 4;
					GHeightFieldUploadData.UploadData.Release();
					GHeightFieldUploadData.UploadData.Initialize();
				}

				void* LockedBuffer = RHILockVertexBuffer(GHeightFieldUploadIndices.UploadIndices.Buffer, 0, GHeightFieldUploadIndices.UploadIndices.Buffer->GetSize(), RLM_WriteOnly);
				uint32 MemcpySize = UploadHeightFieldObjectIndices.GetTypeSize() * NumObjectsToUpload;
				check(GHeightFieldUploadIndices.UploadIndices.Buffer->GetSize() >= MemcpySize);
				FMemory::Memcpy(LockedBuffer, UploadHeightFieldObjectIndices.GetData(), MemcpySize);
				RHIUnlockVertexBuffer(GHeightFieldUploadIndices.UploadIndices.Buffer);

				LockedBuffer = RHILockVertexBuffer(GHeightFieldUploadData.UploadData.Buffer, 0, GHeightFieldUploadData.UploadData.Buffer->GetSize(), RLM_WriteOnly);
				MemcpySize = UploadHeightFieldObjectData.GetTypeSize() * UploadHeightFieldObjectData.Num();
				check(GHeightFieldUploadData.UploadData.Buffer->GetSize() >= MemcpySize);
				FMemory::Memcpy(LockedBuffer, UploadHeightFieldObjectData.GetData(), MemcpySize);
				RHIUnlockVertexBuffer(GHeightFieldUploadData.UploadData.Buffer);

				TShaderMapRef<TUploadObjectsToBufferCS<DFPT_HeightField>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
				ComputeShader->SetParameters(RHICmdList, Scene, NumObjectsToUpload, GHeightFieldUploadIndices.UploadIndices.BufferSRV, GHeightFieldUploadData.UploadData.BufferSRV);
				DispatchComputeShader(RHICmdList, ComputeShader, FMath::DivideAndRoundUp<uint32>(NumObjectsToUpload, UpdateObjectsGroupSize), 1, 1);
				ComputeShader->UnsetParameters(RHICmdList, Scene);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::AddOrRemoveSceneHeightFieldPrimitives(bool bSkipAdd)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;

	if (SceneData.HeightFieldObjectBuffers)
	{
		delete SceneData.HeightFieldObjectBuffers;
		SceneData.HeightFieldObjectBuffers = nullptr;
		SceneData.NumHeightFieldObjectsInBuffer = 0;
		SceneData.HeightFieldAtlasGeneration = 0;
		SceneData.HFVisibilityAtlasGenerattion = 0;
	}

	TArray<int32, SceneRenderingAllocator> PendingRemoveIndices;
	for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldRemoveOps.Num(); ++Idx)
	{
		const FHeightFieldPrimitiveRemoveInfo& RemoveInfo = SceneData.PendingHeightFieldRemoveOps[Idx];
		check(RemoveInfo.DistanceFieldInstanceIndices.Num() == 1);
		PendingRemoveIndices.Add(RemoveInfo.DistanceFieldInstanceIndices[0]);
		const FGlobalDFCacheType CacheType = RemoveInfo.bOftenMoving ? GDF_Full : GDF_MostlyStatic;
		SceneData.PrimitiveModifiedBounds[CacheType].Add(RemoveInfo.SphereBound);
	}
	SceneData.PendingHeightFieldRemoveOps.Reset();
	Algo::Sort(PendingRemoveIndices);
	for (int32 Idx = PendingRemoveIndices.Num() - 1; Idx >= 0; --Idx)
	{
		const int32 RemoveIdx = PendingRemoveIndices[Idx];
		const int32 LastObjectIdx = SceneData.HeightfieldPrimitives.Num() - 1;
		if (RemoveIdx != LastObjectIdx)
		{
			SceneData.HeightfieldPrimitives[LastObjectIdx]->DistanceFieldInstanceIndices[0] = RemoveIdx;
		}
		SceneData.HeightfieldPrimitives.RemoveAtSwap(RemoveIdx);
	}

	if (!bSkipAdd)
	{
		for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldAddOps.Num(); ++Idx)
		{
			FPrimitiveSceneInfo* Primitive = SceneData.PendingHeightFieldAddOps[Idx];
			const int32 HFIdx = SceneData.HeightfieldPrimitives.Add(Primitive);
			Primitive->DistanceFieldInstanceIndices.Empty(1);
			Primitive->DistanceFieldInstanceIndices.Add(HFIdx);
			const FGlobalDFCacheType CacheType = Primitive->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
			const FBoxSphereBounds& Bounds = Primitive->Proxy->GetBounds();
			SceneData.PrimitiveModifiedBounds[CacheType].Add(FVector4(Bounds.Origin, Bounds.SphereRadius));
		}
		SceneData.PendingHeightFieldAddOps.Reset();
	}

	SceneData.PendingHeightFieldUpdateOps.Empty();
}

FString GetObjectBufferMemoryString()
{
	return FString::Printf(TEXT("Temp object buffers %.3fMb"), 
		(GDistanceFieldUploadIndices.UploadIndices.GetSizeBytes() + GDistanceFieldUploadData.UploadData.GetSizeBytes() + GDistanceFieldRemoveIndices.RemoveIndices.GetSizeBytes()) / 1024.0f / 1024.0f);
}
