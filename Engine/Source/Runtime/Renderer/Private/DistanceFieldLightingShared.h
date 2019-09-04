// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldLightingShared.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "DistanceFieldAtlas.h"
#include "Templates/UniquePtr.h"
#include "SceneRendering.h"

class FLightSceneProxy;
class FMaterialRenderProxy;
class FPrimitiveSceneInfo;
class FSceneRenderer;
class FShaderParameterMap;
class FViewInfo;

template<typename ReferencedType> class TScopedPointer;

DECLARE_LOG_CATEGORY_EXTERN(LogDistanceField, Warning, All);

/** Tile sized used for most AO compute shaders. */
extern int32 GDistanceFieldAOTileSizeX;
extern int32 GDistanceFieldAOTileSizeY;
extern int32 GAverageObjectsPerShadowCullTile;

extern int32 GDistanceFieldGI;

inline bool DoesPlatformSupportDistanceFieldGI(EShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5;
}

inline bool SupportsDistanceFieldGI(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GDistanceFieldGI 
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportDistanceFieldGI(ShaderPlatform);
}

extern bool IsDistanceFieldGIAllowed(const FViewInfo& View);
extern bool UseDistanceFieldAO();
extern bool UseAOObjectDistanceField();

class FDistanceFieldObjectBuffers
{
public:

	// In float4's
	static int32 ObjectDataStride;

	int32 MaxObjects;

	FRWBuffer Bounds;
	FRWBuffer Data;

	FDistanceFieldObjectBuffers()
	{
		MaxObjects = 0;
	}

	void Initialize();

	void Release()
	{
		Bounds.Release();
		Data.Release();
	}

	size_t GetSizeBytes() const
	{
		return Bounds.NumBytes + Data.NumBytes;
	}
};

class FSurfelBuffers
{
public:

	// In float4's
	static int32 SurfelDataStride;
	static int32 InterpolatedVertexDataStride;

	int32 MaxSurfels;

	void Initialize()
	{
		if (MaxSurfels > 0)
		{
			InterpolatedVertexData.Initialize(sizeof(FVector4), MaxSurfels * InterpolatedVertexDataStride, PF_A32B32G32R32F, BUF_Static);
			Surfels.Initialize(sizeof(FVector4), MaxSurfels * SurfelDataStride, PF_A32B32G32R32F, BUF_Static);
		}
	}

	void Release()
	{
		InterpolatedVertexData.Release();
		Surfels.Release();
	}

	size_t GetSizeBytes() const
	{
		return InterpolatedVertexData.NumBytes + Surfels.NumBytes;
	}

	FRWBuffer InterpolatedVertexData;
	FRWBuffer Surfels;
};

class FInstancedSurfelBuffers
{
public:

	int32 MaxSurfels;

	void Initialize()
	{
		if (MaxSurfels > 0)
		{
			VPLFlux.Initialize(sizeof(FVector4), MaxSurfels, PF_A32B32G32R32F, BUF_Static);
		}
	}

	void Release()
	{
		VPLFlux.Release();
	}

	size_t GetSizeBytes() const
	{
		return VPLFlux.NumBytes;
	}

	FRWBuffer VPLFlux;
};

class FDistanceFieldObjectBufferParameters
{
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		SceneObjectBounds.Bind(ParameterMap, TEXT("SceneObjectBounds"));
		SceneObjectData.Bind(ParameterMap, TEXT("SceneObjectData"));
		NumSceneObjects.Bind(ParameterMap, TEXT("NumSceneObjects"));
		DistanceFieldTexture.Bind(ParameterMap, TEXT("DistanceFieldTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldAtlasTexelSize"));
	}

	template<typename TParamRef>
	void Set(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FDistanceFieldObjectBuffers& ObjectBuffers, int32 NumObjectsValue, bool bBarrier = false)
	{
		if (bBarrier)
		{
			FRHIUnorderedAccessView* OutUAVs[2];
			OutUAVs[0] = ObjectBuffers.Bounds.UAV;
			OutUAVs[1] = ObjectBuffers.Data.UAV;
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, ARRAY_COUNT(OutUAVs));
		}

		SceneObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Bounds);
		SceneObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Data);
		SetShaderValue(RHICmdList, ShaderRHI, NumSceneObjects, NumObjectsValue);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldTexture,
			DistanceFieldSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
			);

		const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldAtlasTexelSize, InvTextureDim);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FDistanceFieldObjectBuffers& ObjectBuffers, bool bBarrier = false)
	{
		SceneObjectBounds.UnsetUAV(RHICmdList, ShaderRHI);
		SceneObjectData.UnsetUAV(RHICmdList, ShaderRHI);

		if (bBarrier)
		{
			FRHIUnorderedAccessView* OutUAVs[2];
			OutUAVs[0] = ObjectBuffers.Bounds.UAV;
			OutUAVs[1] = ObjectBuffers.Data.UAV;
			RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, ARRAY_COUNT(OutUAVs));
		}
	}

	friend FArchive& operator<<(FArchive& Ar, FDistanceFieldObjectBufferParameters& P)
	{
		Ar << P.SceneObjectBounds << P.SceneObjectData << P.NumSceneObjects << P.DistanceFieldTexture << P.DistanceFieldSampler << P.DistanceFieldAtlasTexelSize;
		return Ar;
	}

	bool AnyBound() const
	{
		return SceneObjectBounds.IsBound() || SceneObjectData.IsBound() || NumSceneObjects.IsBound() || DistanceFieldTexture.IsBound() || DistanceFieldSampler.IsBound() || DistanceFieldAtlasTexelSize.IsBound();
	}

private:
	FRWShaderParameter SceneObjectBounds;
	FRWShaderParameter SceneObjectData;
	FShaderParameter NumSceneObjects;
	FShaderResourceParameter DistanceFieldTexture;
	FShaderResourceParameter DistanceFieldSampler;
	FShaderParameter DistanceFieldAtlasTexelSize;
};

class FSurfelBufferParameters
{
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		InterpolatedVertexData.Bind(ParameterMap, TEXT("InterpolatedVertexData"));
		SurfelData.Bind(ParameterMap, TEXT("SurfelData"));
		VPLFlux.Bind(ParameterMap, TEXT("VPLFlux"));
	}

	template<typename TParamRef>
	void Set(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FSurfelBuffers& SurfelBuffers, const FInstancedSurfelBuffers& InstancedSurfelBuffers)
	{
		InterpolatedVertexData.SetBuffer(RHICmdList, ShaderRHI, SurfelBuffers.InterpolatedVertexData);
		SurfelData.SetBuffer(RHICmdList, ShaderRHI, SurfelBuffers.Surfels);
		VPLFlux.SetBuffer(RHICmdList, ShaderRHI, InstancedSurfelBuffers.VPLFlux);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI)
	{
		InterpolatedVertexData.UnsetUAV(RHICmdList, ShaderRHI);
		SurfelData.UnsetUAV(RHICmdList, ShaderRHI);
		VPLFlux.UnsetUAV(RHICmdList, ShaderRHI);
	}

	friend FArchive& operator<<(FArchive& Ar, FSurfelBufferParameters& P)
	{
		Ar << P.InterpolatedVertexData << P.SurfelData << P.VPLFlux;
		return Ar;
	}

private:
	FRWShaderParameter InterpolatedVertexData;
	FRWShaderParameter SurfelData;
	FRWShaderParameter VPLFlux;
};

class FDistanceFieldCulledObjectBuffers
{
public:

	static int32 ObjectDataStride;
	static int32 ObjectBoxBoundsStride;

	bool bWantBoxBounds;
	int32 MaxObjects;

	FRWBuffer ObjectIndirectArguments;
	FRWBuffer ObjectIndirectDispatch;
	FRWBufferStructured Bounds;
	FRWBufferStructured Data;
	FRWBufferStructured BoxBounds;

	FDistanceFieldCulledObjectBuffers()
	{
		MaxObjects = 0;
		bWantBoxBounds = false;
	}

	void Initialize()
	{
		if (MaxObjects > 0)
		{
			const uint32 FastVRamFlag = GFastVRamConfig.DistanceFieldCulledObjectBuffers | ( IsTransientResourceBufferAliasingEnabled() ? BUF_Transient : BUF_None );

			ObjectIndirectArguments.Initialize(sizeof(uint32), 5, PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("FDistanceFieldCulledObjectBuffers::ObjectIndirectArguments"));
			ObjectIndirectDispatch.Initialize(sizeof(uint32), 3, PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("FDistanceFieldCulledObjectBuffers::ObjectIndirectDispatch"));
			Bounds.Initialize(sizeof(FVector4), MaxObjects, BUF_Static | FastVRamFlag, TEXT("FDistanceFieldCulledObjectBuffers::Bounds"));
			Data.Initialize(sizeof(FVector4), MaxObjects * ObjectDataStride, BUF_Static | FastVRamFlag, TEXT("FDistanceFieldCulledObjectBuffers::Data"));

			if (bWantBoxBounds)
			{
				BoxBounds.Initialize(sizeof(FVector4), MaxObjects * ObjectBoxBoundsStride, BUF_Static | FastVRamFlag, TEXT("FDistanceFieldCulledObjectBuffers::BoxBounds"));
			}
		}
	}

	void AcquireTransientResource()
	{
		Bounds.AcquireTransientResource();
		Data.AcquireTransientResource();
		if (bWantBoxBounds)
		{
			BoxBounds.AcquireTransientResource();
		}
	}

	void DiscardTransientResource()
	{
		Bounds.DiscardTransientResource();
		Data.DiscardTransientResource();
		if (bWantBoxBounds)
		{
			BoxBounds.DiscardTransientResource();
		}
	}

	void Release()
	{
		ObjectIndirectArguments.Release();
		ObjectIndirectDispatch.Release();
		Bounds.Release();
		Data.Release();
		BoxBounds.Release();
	}

	size_t GetSizeBytes() const
	{
		return ObjectIndirectArguments.NumBytes + ObjectIndirectDispatch.NumBytes + Bounds.NumBytes + Data.NumBytes + BoxBounds.NumBytes;
	}
};

class FDistanceFieldObjectBufferResource : public FRenderResource
{
public:
	FDistanceFieldCulledObjectBuffers Buffers;

	virtual void InitDynamicRHI()  override
	{
		Buffers.Initialize();
	}

	virtual void ReleaseDynamicRHI() override
	{
		Buffers.Release();
	}
};

class FDistanceFieldCulledObjectBufferParameters
{
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ObjectIndirectArguments.Bind(ParameterMap, TEXT("ObjectIndirectArguments"));
		CulledObjectBounds.Bind(ParameterMap, TEXT("CulledObjectBounds"));
		CulledObjectData.Bind(ParameterMap, TEXT("CulledObjectData"));
		CulledObjectBoxBounds.Bind(ParameterMap, TEXT("CulledObjectBoxBounds"));
		DistanceFieldTexture.Bind(ParameterMap, TEXT("DistanceFieldTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldAtlasTexelSize"));
	}

	template<typename TShaderRHI, typename TRHICommandList>
	void Set(TRHICommandList& RHICmdList, TShaderRHI* ShaderRHI, const FDistanceFieldCulledObjectBuffers& ObjectBuffers)
	{
		ObjectIndirectArguments.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.ObjectIndirectArguments);
		CulledObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Bounds);
		CulledObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Data);

		if (CulledObjectBoxBounds.IsBound())
		{
			check(ObjectBuffers.bWantBoxBounds);
			CulledObjectBoxBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.BoxBounds);
		}

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldTexture,
			DistanceFieldSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
			);

		const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldAtlasTexelSize, InvTextureDim);
	}

	template<typename TRHIShader, typename TRHICommandList>
	void UnsetParameters(TRHICommandList& RHICmdList, TRHIShader* ShaderRHI)
	{
		ObjectIndirectArguments.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectBounds.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectData.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectBoxBounds.UnsetUAV(RHICmdList, ShaderRHI);
	}

	void GetUAVs(const FDistanceFieldCulledObjectBuffers& ObjectBuffers, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		uint32 MaxIndex = 0;
		MaxIndex = FMath::Max(MaxIndex, ObjectIndirectArguments.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectBounds.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectData.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectBoxBounds.GetUAVIndex());

		UAVs.AddZeroed(MaxIndex + 1);

		if (ObjectIndirectArguments.IsUAVBound())
		{
			UAVs[ObjectIndirectArguments.GetUAVIndex()] = ObjectBuffers.ObjectIndirectArguments.UAV;
		}

		if (CulledObjectBounds.IsUAVBound())
		{
			UAVs[CulledObjectBounds.GetUAVIndex()] = ObjectBuffers.Bounds.UAV;
		}

		if (CulledObjectData.IsUAVBound())
		{
			UAVs[CulledObjectData.GetUAVIndex()] = ObjectBuffers.Data.UAV;
		}

		if (CulledObjectBoxBounds.IsUAVBound())
		{
			UAVs[CulledObjectBoxBounds.GetUAVIndex()] = ObjectBuffers.BoxBounds.UAV;
		}

		check(UAVs.Num() > 0);
	}

	friend FArchive& operator<<(FArchive& Ar, FDistanceFieldCulledObjectBufferParameters& P)
	{
		Ar << P.ObjectIndirectArguments << P.CulledObjectBounds << P.CulledObjectData << P.CulledObjectBoxBounds << P.DistanceFieldTexture << P.DistanceFieldSampler << P.DistanceFieldAtlasTexelSize;
		return Ar;
	}

private:
	FRWShaderParameter ObjectIndirectArguments;
	FRWShaderParameter CulledObjectBounds;
	FRWShaderParameter CulledObjectData;
	FRWShaderParameter CulledObjectBoxBounds;
	FShaderResourceParameter DistanceFieldTexture;
	FShaderResourceParameter DistanceFieldSampler;
	FShaderParameter DistanceFieldAtlasTexelSize;
};

class FCPUUpdatedBuffer
{
public:

	EPixelFormat Format;
	int32 Stride;
	int32 MaxElements;

	// Volatile must be written every frame before use.  Supports multiple writes per frame on PS4, unlike Dynamic.
	bool bVolatile;

	FVertexBufferRHIRef Buffer;
	FShaderResourceViewRHIRef BufferSRV;

	FCPUUpdatedBuffer()
	{
		Format = PF_A32B32G32R32F;
		Stride = 1;
		MaxElements = 0;
		bVolatile = true;
	}

	void Initialize()
	{
		if (MaxElements > 0 && Stride > 0)
		{
			FRHIResourceCreateInfo CreateInfo;
			Buffer = RHICreateVertexBuffer(MaxElements * Stride * GPixelFormats[Format].BlockBytes, (bVolatile ? BUF_Volatile : BUF_Dynamic)  | BUF_ShaderResource, CreateInfo);
			BufferSRV = RHICreateShaderResourceView(Buffer, GPixelFormats[Format].BlockBytes, Format);
		}
	}

	void Release()
	{
		Buffer.SafeRelease();
		BufferSRV.SafeRelease(); 
	}

	size_t GetSizeBytes() const
	{
		return MaxElements * Stride * GPixelFormats[Format].BlockBytes;
	}
};

static int32 LightTileDataStride = 1;

/**  */
class FLightTileIntersectionResources
{
public:

	FLightTileIntersectionResources() :
		b16BitIndices(false)
	{}

	void Initialize()
	{
		TileNumCulledObjects.Initialize(sizeof(uint32), TileDimensions.X * TileDimensions.Y, PF_R32_UINT, BUF_Static);
		NextStartOffset.Initialize(sizeof(uint32), 1, PF_R32_UINT, BUF_Static);
		TileStartOffsets.Initialize(sizeof(uint32), TileDimensions.X * TileDimensions.Y, PF_R32_UINT, BUF_Static);

		//@todo - handle max exceeded
		TileArrayData.Initialize(b16BitIndices ? sizeof(uint16) : sizeof(uint32), GAverageObjectsPerShadowCullTile * TileDimensions.X * TileDimensions.Y * LightTileDataStride, b16BitIndices ? PF_R16_UINT : PF_R32_UINT, BUF_Static);
	}

	void Release()
	{
		TileNumCulledObjects.Release();
		NextStartOffset.Release();
		TileStartOffsets.Release();
		TileArrayData.Release();
	}

	size_t GetSizeBytes() const
	{
		return TileNumCulledObjects.NumBytes + NextStartOffset.NumBytes + TileStartOffsets.NumBytes + TileArrayData.NumBytes;
	}

	FIntPoint TileDimensions;

	FRWBuffer TileNumCulledObjects;
	FRWBuffer NextStartOffset;
	FRWBuffer TileStartOffsets;
	FRWBuffer TileArrayData;
	bool b16BitIndices;
};

class FLightTileIntersectionParameters
{
public:

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SHADOW_TILE_ARRAY_DATA_STRIDE"), LightTileDataStride);
	}

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ShadowTileNumCulledObjects.Bind(ParameterMap, TEXT("ShadowTileNumCulledObjects"));
		ShadowTileStartOffsets.Bind(ParameterMap, TEXT("ShadowTileStartOffsets"));
		NextStartOffset.Bind(ParameterMap, TEXT("NextStartOffset"));
		ShadowTileArrayData.Bind(ParameterMap, TEXT("ShadowTileArrayData"));
		ShadowTileListGroupSize.Bind(ParameterMap, TEXT("ShadowTileListGroupSize"));
		ShadowAverageObjectsPerTile.Bind(ParameterMap, TEXT("ShadowAverageObjectsPerTile"));
	}

	bool IsBound() const
	{
		return ShadowTileNumCulledObjects.IsBound() || ShadowTileStartOffsets.IsBound() || NextStartOffset.IsBound() || ShadowTileArrayData.IsBound() || ShadowTileListGroupSize.IsBound() || ShadowAverageObjectsPerTile.IsBound();
	}

	template<typename TParamRef, typename TRHICommandList>
	void Set(TRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FLightTileIntersectionResources& LightTileIntersectionResources)
	{
		ShadowTileNumCulledObjects.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileNumCulledObjects);
		ShadowTileStartOffsets.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileStartOffsets);

		NextStartOffset.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.NextStartOffset);

		// Bind sorted array data if we are after the sort pass
		ShadowTileArrayData.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileArrayData);

		SetShaderValue(RHICmdList, ShaderRHI, ShadowTileListGroupSize, LightTileIntersectionResources.TileDimensions);
		SetShaderValue(RHICmdList, ShaderRHI, ShadowAverageObjectsPerTile, GAverageObjectsPerShadowCullTile);
	}

	void GetUAVs(FLightTileIntersectionResources& TileIntersectionResources, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		int32 MaxIndex = FMath::Max(
			FMath::Max(ShadowTileNumCulledObjects.GetUAVIndex(), ShadowTileStartOffsets.GetUAVIndex()), 
			FMath::Max(NextStartOffset.GetUAVIndex(), ShadowTileArrayData.GetUAVIndex()));
		UAVs.AddZeroed(MaxIndex + 1);

		if (ShadowTileNumCulledObjects.IsUAVBound())
		{
			UAVs[ShadowTileNumCulledObjects.GetUAVIndex()] = TileIntersectionResources.TileNumCulledObjects.UAV;
		}

		if (ShadowTileStartOffsets.IsUAVBound())
		{
			UAVs[ShadowTileStartOffsets.GetUAVIndex()] = TileIntersectionResources.TileStartOffsets.UAV;
		}

		if (NextStartOffset.IsUAVBound())
		{
			UAVs[NextStartOffset.GetUAVIndex()] = TileIntersectionResources.NextStartOffset.UAV;
		}

		if (ShadowTileArrayData.IsUAVBound())
		{
			UAVs[ShadowTileArrayData.GetUAVIndex()] = TileIntersectionResources.TileArrayData.UAV;
		}

		check(UAVs.Num() > 0);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI)
	{
		ShadowTileNumCulledObjects.UnsetUAV(RHICmdList, ShaderRHI);
		ShadowTileStartOffsets.UnsetUAV(RHICmdList, ShaderRHI);
		NextStartOffset.UnsetUAV(RHICmdList, ShaderRHI);
		ShadowTileArrayData.UnsetUAV(RHICmdList, ShaderRHI);
	}

	friend FArchive& operator<<(FArchive& Ar, FLightTileIntersectionParameters& P)
	{
		Ar << P.ShadowTileNumCulledObjects;
		Ar << P.ShadowTileStartOffsets;
		Ar << P.NextStartOffset;
		Ar << P.ShadowTileArrayData;
		Ar << P.ShadowTileListGroupSize;
		Ar << P.ShadowAverageObjectsPerTile;
		return Ar;
	}

private:
	FRWShaderParameter ShadowTileNumCulledObjects;
	FRWShaderParameter ShadowTileStartOffsets;
	FRWShaderParameter NextStartOffset;
	FRWShaderParameter ShadowTileArrayData;
	FShaderParameter ShadowTileListGroupSize;
	FShaderParameter ShadowAverageObjectsPerTile;
};

extern void CullDistanceFieldObjectsForLight(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy, 
	const FMatrix& WorldToShadowValue, 
	int32 NumPlanes, 
	const FPlane* PlaneData, 
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<class FLightTileIntersectionResources>& TileIntersectionResources);

class FUniformMeshBuffers
{
public:

	int32 MaxElements;

	FVertexBufferRHIRef TriangleData;
	FShaderResourceViewRHIRef TriangleDataSRV;

	FRWBuffer TriangleAreas;
	FRWBuffer TriangleCDFs;

	FUniformMeshBuffers()
	{
		MaxElements = 0;
	}

	void Initialize();

	void Release()
	{
		TriangleData.SafeRelease();
		TriangleDataSRV.SafeRelease(); 
		TriangleAreas.Release();
		TriangleCDFs.Release();
	}
};

class FUniformMeshConverter
{
public:
	static int32 Convert(
		FRHICommandListImmediate& RHICmdList, 
		FSceneRenderer& Renderer,
		FViewInfo& View, 
		const FPrimitiveSceneInfo* PrimitiveSceneInfo, 
		int32 LODIndex,
		FUniformMeshBuffers*& OutUniformMeshBuffers,
		const FMaterialRenderProxy*& OutMaterialRenderProxy,
		FRHIUniformBuffer*& OutPrimitiveUniformBuffer);

	static void GenerateSurfels(
		FRHICommandListImmediate& RHICmdList, 
		FViewInfo& View, 
		const FPrimitiveSceneInfo* PrimitiveSceneInfo, 
		const FMaterialRenderProxy* MaterialProxy,
		FRHIUniformBuffer* PrimitiveUniformBuffer,
		const FMatrix& Instance0Transform,
		int32 SurfelOffset,
		int32 NumSurfels);
};

class FPreCulledTriangleBuffers
{
public:

	int32 MaxIndices;

	FRWBuffer TriangleVisibleMask;

	FPreCulledTriangleBuffers()
	{
		MaxIndices = 0;
	}

	void Initialize()
	{
		if (MaxIndices > 0)
		{
			TriangleVisibleMask.Initialize(sizeof(uint32), MaxIndices / 3, PF_R32_UINT);
		}
	}

	void Release()
	{
		TriangleVisibleMask.Release();
	}

	size_t GetSizeBytes() const
	{
		return TriangleVisibleMask.NumBytes;
	}
};

extern TGlobalResource<FDistanceFieldObjectBufferResource> GAOCulledObjectBuffers;

extern bool SupportsDistanceFieldAO(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);
