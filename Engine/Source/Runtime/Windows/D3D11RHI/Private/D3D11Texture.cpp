// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11VertexBuffer.cpp: D3D texture RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"

#if PLATFORM_DESKTOP && !PLATFORM_HOLOLENS
// For Depth Bounds Test interface
#include "Windows/AllowWindowsPlatformTypes.h"
#include "nvapi.h"
#include "amd_ags.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "HAL/LowLevelMemTracker.h"

int64 FD3D11GlobalStats::GDedicatedVideoMemory = 0;
int64 FD3D11GlobalStats::GDedicatedSystemMemory = 0;
int64 FD3D11GlobalStats::GSharedSystemMemory = 0;
int64 FD3D11GlobalStats::GTotalGraphicsMemory = 0;


/*-----------------------------------------------------------------------------
	Texture allocator support.
-----------------------------------------------------------------------------*/

static bool ShouldCountAsTextureMemory(uint32 BindFlags)
{
	return (BindFlags & (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)) == 0;
}

// @param b3D true:3D, false:2D or cube map
static TStatId GetD3D11StatEnum(uint32 BindFlags, bool bCubeMap, bool b3D)
{
#if STATS
	if(ShouldCountAsTextureMemory(BindFlags))
	{
		// normal texture
		if(bCubeMap)
		{
			return GET_STATID(STAT_TextureMemoryCube);
		}
		else if(b3D)
		{
			return GET_STATID(STAT_TextureMemory3D);
		}
		else
		{
			return GET_STATID(STAT_TextureMemory2D);
		}
	}
	else
	{
		// render target
		if(bCubeMap)
		{
			return GET_STATID(STAT_RenderTargetMemoryCube);
		}
		else if(b3D)
		{
			return GET_STATID(STAT_RenderTargetMemory3D);
		}
		else
		{
			return GET_STATID(STAT_RenderTargetMemory2D);
		}
	}
#endif
	return TStatId();
}

// Note: This function can be called from many different threads
// @param TextureSize >0 to allocate, <0 to deallocate
// @param b3D true:3D, false:2D or cube map
void UpdateD3D11TextureStats(uint32 BindFlags, uint32 MiscFlags, int64 TextureSize, bool b3D)
{
	if(TextureSize == 0)
	{
		return;
	}
	
	int64 AlignedSize = (TextureSize > 0) ? Align(TextureSize, 1024) / 1024 : -(Align(-TextureSize, 1024) / 1024);
	if(ShouldCountAsTextureMemory(BindFlags))
	{
		FPlatformAtomics::InterlockedAdd(&GCurrentTextureMemorySize, AlignedSize);
	}
	else
	{
		FPlatformAtomics::InterlockedAdd(&GCurrentRendertargetMemorySize, AlignedSize);
	}

	bool bCubeMap = (MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;

	INC_MEMORY_STAT_BY_FName(GetD3D11StatEnum(BindFlags, bCubeMap, b3D).GetName(), TextureSize);

	if(TextureSize > 0)
	{
		INC_DWORD_STAT(STAT_D3D11TexturesAllocated);
	}
	else
	{
		INC_DWORD_STAT(STAT_D3D11TexturesReleased);
	}
}

template<typename BaseResourceType>
void D3D11TextureAllocated( TD3D11Texture2D<BaseResourceType>& Texture )
{
	ID3D11Texture2D* D3D11Texture2D = Texture.GetResource();

	if(D3D11Texture2D)
	{
		if ( (Texture.Flags & TexCreate_Virtual) == TexCreate_Virtual )
		{
			Texture.SetMemorySize(0);
		}
		else
		{
			D3D11_TEXTURE2D_DESC Desc;

			D3D11Texture2D->GetDesc( &Desc );
			check(Texture.IsCubemap() == ((Desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0));

			int64 TextureSize = CalcTextureSize( Desc.Width, Desc.Height, Texture.GetFormat(), Desc.MipLevels ) * Desc.ArraySize;

			Texture.SetMemorySize( TextureSize );
			UpdateD3D11TextureStats(Desc.BindFlags, Desc.MiscFlags, TextureSize, false);

#if PLATFORM_WINDOWS
			// On Windows there is no way to hook into the low level d3d allocations and frees.
			// This means that we must manually add the tracking here.
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Texture.GetResource(), Texture.GetMemorySize(), ELLMTag::GraphicsPlatform));
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Texture.GetResource(), Texture.GetMemorySize(), ELLMTag::Textures));
#endif
		}
	}
}

template<typename BaseResourceType>
void D3D11TextureDeleted( TD3D11Texture2D<BaseResourceType>& Texture )
{
	ID3D11Texture2D* D3D11Texture2D = Texture.GetResource();

	if(D3D11Texture2D)
	{
		D3D11_TEXTURE2D_DESC Desc;

		D3D11Texture2D->GetDesc( &Desc );
		check(Texture.IsCubemap() == ((Desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0));

		// When using virtual textures use the current memory size, which is the number of physical pages allocated, not virtual
		int64 TextureSize = 0;
		if ( (Texture.GetFlags() & TexCreate_Virtual) == TexCreate_Virtual )
		{
			TextureSize = Texture.GetMemorySize();
		}
		else
		{
			TextureSize = CalcTextureSize( Desc.Width, Desc.Height, Texture.GetFormat(), Desc.MipLevels ) * Desc.ArraySize;
		}

		UpdateD3D11TextureStats(Desc.BindFlags, Desc.MiscFlags, -TextureSize, false);

#if PLATFORM_WINDOWS
		// On Windows there is no way to hook into the low level d3d allocations and frees.
		// This means that we must manually add the tracking here.
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Texture.GetResource()));
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, Texture.GetResource()));
#endif
	}
}

void D3D11TextureAllocated2D( FD3D11Texture2D& Texture )
{
	D3D11TextureAllocated(Texture);
}

void D3D11TextureAllocated( FD3D11Texture3D& Texture )
{
	ID3D11Texture3D* D3D11Texture3D = Texture.GetResource();

	if(D3D11Texture3D)
	{
		D3D11_TEXTURE3D_DESC Desc;

		D3D11Texture3D->GetDesc( &Desc );

		int64 TextureSize = CalcTextureSize3D( Desc.Width, Desc.Height, Desc.Depth, Texture.GetFormat(), Desc.MipLevels );

		Texture.SetMemorySize( TextureSize );

		UpdateD3D11TextureStats(Desc.BindFlags, Desc.MiscFlags, TextureSize, true);

#if PLATFORM_WINDOWS
		// On Windows there is no way to hook into the low level d3d allocations and frees.
		// This means that we must manually add the tracking here.
		LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Texture.GetResource(), Texture.GetMemorySize(), ELLMTag::GraphicsPlatform));
		LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Texture.GetResource(), Texture.GetMemorySize(), ELLMTag::Textures));
#endif
	}
}

void D3D11TextureDeleted( FD3D11Texture3D& Texture )
{
	ID3D11Texture3D* D3D11Texture3D = Texture.GetResource();

	if(D3D11Texture3D)
	{
		D3D11_TEXTURE3D_DESC Desc;

		D3D11Texture3D->GetDesc( &Desc );

		int64 TextureSize = CalcTextureSize3D( Desc.Width, Desc.Height, Desc.Depth, Texture.GetFormat(), Desc.MipLevels );

		UpdateD3D11TextureStats(Desc.BindFlags, Desc.MiscFlags, -TextureSize, true);

#if PLATFORM_WINDOWS
		// On Windows there is no way to hook into the low level d3d allocations and frees.
		// This means that we must manually add the tracking here.
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Texture.GetResource()));
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, Texture.GetResource()));
#endif
	}
}

template<typename BaseResourceType>
TD3D11Texture2D<BaseResourceType>::~TD3D11Texture2D()
{
	D3D11TextureDeleted(*this);
	if (bPooled)
	{
		ReturnPooledTexture2D(this->GetNumMips(), this->GetFormat(), this->GetResource());
	}

#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	D3DRHI->DestroyVirtualTexture(GetFlags(), GetRawTextureMemory());
#endif
}

template TD3D11Texture2D<FD3D11BaseTexture2D>::~TD3D11Texture2D();

FD3D11Texture3D::~FD3D11Texture3D()
{
	D3D11TextureDeleted( *this );
}

uint64 FD3D11DynamicRHI::RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	OutAlign = 0;
	return CalcTextureSize(SizeX, SizeY, (EPixelFormat)Format, NumMips);
}

uint64 FD3D11DynamicRHI::RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	OutAlign = 0;
	return CalcTextureSize3D(SizeX, SizeY, SizeZ, (EPixelFormat)Format, NumMips);
}

uint64 FD3D11DynamicRHI::RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	OutAlign = 0;
	return CalcTextureSize(Size, Size, (EPixelFormat)Format, NumMips) * 6;
}

/**
 * Retrieves texture memory stats. 
 */
void FD3D11DynamicRHI::RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats)
{
	OutStats.DedicatedVideoMemory = FD3D11GlobalStats::GDedicatedVideoMemory;
    OutStats.DedicatedSystemMemory = FD3D11GlobalStats::GDedicatedSystemMemory;
    OutStats.SharedSystemMemory = FD3D11GlobalStats::GSharedSystemMemory;
	OutStats.TotalGraphicsMemory = FD3D11GlobalStats::GTotalGraphicsMemory ? FD3D11GlobalStats::GTotalGraphicsMemory : -1;

	OutStats.AllocatedMemorySize = int64(GCurrentTextureMemorySize) * 1024;
	OutStats.LargestContiguousAllocation = OutStats.AllocatedMemorySize;
	OutStats.TexturePoolSize = GTexturePoolSize;
	OutStats.PendingMemoryAdjustment = 0;
}

/**
 * Fills a texture with to visualize the texture pool memory.
 *
 * @param	TextureData		Start address
 * @param	SizeX			Number of pixels along X
 * @param	SizeY			Number of pixels along Y
 * @param	Pitch			Number of bytes between each row
 * @param	PixelSize		Number of bytes each pixel represents
 *
 * @return true if successful, false otherwise
 */
bool FD3D11DynamicRHI::RHIGetTextureMemoryVisualizeData( FColor* /*TextureData*/, int32 /*SizeX*/, int32 /*SizeY*/, int32 /*Pitch*/, int32 /*PixelSize*/ )
{
	// currently only implemented for console (Note: Keep this function for further extension. Talk to NiklasS for more info.)
	return false;
}

/*------------------------------------------------------------------------------
	Texture pooling.
------------------------------------------------------------------------------*/

/** Define to 1 to enable the pooling of 2D texture resources. */
#define USE_TEXTURE_POOLING 0

/** A texture resource stored in the pool. */
struct FPooledTexture2D
{
	/** The texture resource. */
	TRefCountPtr<ID3D11Texture2D> Resource;
};

/** A pool of D3D texture resources. */
struct FTexturePool
{
	TArray<FPooledTexture2D> Textures;
};

/** The global texture pool. */
struct FGlobalTexturePool
{
	/** Formats stored in the pool. */
	enum EInternalFormat
	{
		IF_DXT1,
		IF_DXT5,
		IF_BC5,
		IF_Max
	};

	enum
	{
		/** Minimum mip count for which to pool textures. */
		MinMipCount = 7,
		/** Maximum mip count for which to pool textures. */
		MaxMipCount = 13,
		/** The number of pools based on mip levels. */
		MipPoolCount = MaxMipCount - MinMipCount,
	};

	/** The individual texture pools. */
	FTexturePool Pools[MipPoolCount][IF_Max];
};
FGlobalTexturePool GTexturePool;

/**
 * Releases all pooled textures.
 */
void ReleasePooledTextures()
{
	for (int32 MipPoolIndex = 0; MipPoolIndex < FGlobalTexturePool::MipPoolCount; ++MipPoolIndex)
	{
		for (int32 FormatPoolIndex = 0; FormatPoolIndex < FGlobalTexturePool::IF_Max; ++FormatPoolIndex)
		{
			GTexturePool.Pools[MipPoolIndex][FormatPoolIndex].Textures.Empty();
		}
	}
}

/**
 * Retrieves the texture pool for the specified mip count and format.
 */
FTexturePool* GetTexturePool(int32 MipCount, EPixelFormat PixelFormat)
{
	FTexturePool* Pool = NULL;
	int32 MipPool = MipCount - FGlobalTexturePool::MinMipCount;
	if (MipPool >= 0 && MipPool < FGlobalTexturePool::MipPoolCount)
	{
		int32 FormatPool = -1;
		switch (PixelFormat)
		{
			case PF_DXT1: FormatPool = FGlobalTexturePool::IF_DXT1; break;
			case PF_DXT5: FormatPool = FGlobalTexturePool::IF_DXT5; break;
			case PF_BC5: FormatPool = FGlobalTexturePool::IF_BC5; break;
		}
		if (FormatPool >= 0 && FormatPool < FGlobalTexturePool::IF_Max)
		{
			Pool = &GTexturePool.Pools[MipPool][FormatPool];
		}
	}
	return Pool;
}

/**
 * Retrieves a texture from the pool if one exists.
 */
bool GetPooledTexture2D(int32 MipCount, EPixelFormat PixelFormat, FPooledTexture2D* OutTexture)
{
#if USE_TEXTURE_POOLING
	FTexturePool* Pool = GetTexturePool(MipCount,PixelFormat);
	if (Pool && Pool->Textures.Num() > 0)
	{
		*OutTexture = Pool->Textures.Last();

		{
			D3D11_TEXTURE2D_DESC Desc;
			OutTexture->Resource->GetDesc(&Desc);
			check(Desc.Format == GPixelFormats[PixelFormat].PlatformFormat);
			check(MipCount == Desc.MipLevels);
			check(Desc.Width == Desc.Height);
			check(Desc.Width == (1 << (MipCount-1)));
			int32 TextureSize = CalcTextureSize(Desc.Width, Desc.Height, PixelFormat, Desc.MipLevels);
			DEC_MEMORY_STAT_BY(STAT_D3D11TexturePoolMemory,TextureSize);
		}

		Pool->Textures.RemoveAt(Pool->Textures.Num() - 1);
		return true;
	}
#endif // #if USE_TEXTURE_POOLING
	return false;
}

/**
 * Returns a texture to its pool.
 */
void ReturnPooledTexture2D(int32 MipCount, EPixelFormat PixelFormat, ID3D11Texture2D* InResource)
{
#if USE_TEXTURE_POOLING
	FTexturePool* Pool = GetTexturePool(MipCount,PixelFormat);
	if (Pool)
	{
		FPooledTexture2D* PooledTexture = new(Pool->Textures) FPooledTexture2D;
		PooledTexture->Resource = InResource;
		{
			D3D11_TEXTURE2D_DESC Desc;
			PooledTexture->Resource->GetDesc(&Desc);
			check(Desc.Format == GPixelFormats[PixelFormat].PlatformFormat);
			check(MipCount == Desc.MipLevels);
			check(Desc.Width == Desc.Height);
			check(Desc.Width == (1 << (MipCount-1)));
			int32 TextureSize = CalcTextureSize(Desc.Width, Desc.Height, PixelFormat, Desc.MipLevels);
			INC_MEMORY_STAT_BY(STAT_D3D11TexturePoolMemory,TextureSize);
		}
	}
#endif // #if USE_TEXTURE_POOLING
}

DXGI_FORMAT FD3D11DynamicRHI::GetPlatformTextureResourceFormat(DXGI_FORMAT InFormat, uint32 InFlags)
{
	// DX 11 Shared textures must be B8G8R8A8_UNORM
	if (InFlags & TexCreate_Shared)
	{
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
	return InFormat;
}

/** If true, guard texture creates with SEH to log more information about a driver crash we are seeing during texture streaming. */
#define GUARDED_TEXTURE_CREATES (PLATFORM_WINDOWS && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

/**
 * Creates a 2D texture optionally guarded by a structured exception handler.
 */
void SafeCreateTexture2D(ID3D11Device* Direct3DDevice, int32 UEFormat, const D3D11_TEXTURE2D_DESC* TextureDesc, const D3D11_SUBRESOURCE_DATA* SubResourceData, ID3D11Texture2D** OutTexture2D)
{
#if GUARDED_TEXTURE_CREATES
	bool bDriverCrash = true;
	__try
	{
#endif // #if GUARDED_TEXTURE_CREATES
		VERIFYD3D11CREATETEXTURERESULT(
			Direct3DDevice->CreateTexture2D(TextureDesc,SubResourceData,OutTexture2D),
			UEFormat,
			TextureDesc->Width,
			TextureDesc->Height,
			TextureDesc->ArraySize,
			TextureDesc->Format,
			TextureDesc->MipLevels,
			TextureDesc->BindFlags,
			TextureDesc->Usage,
			TextureDesc->CPUAccessFlags,
			TextureDesc->MiscFlags,			
			TextureDesc->SampleDesc.Count,
			TextureDesc->SampleDesc.Quality,
			SubResourceData ? SubResourceData->pSysMem : nullptr,
			SubResourceData ? SubResourceData->SysMemPitch : 0,
			SubResourceData ? SubResourceData->SysMemSlicePitch : 0,
			Direct3DDevice
			);
#if GUARDED_TEXTURE_CREATES
		bDriverCrash = false;
	}
	__finally
	{
		if (bDriverCrash)
		{
			UE_LOG(LogD3D11RHI,Error,
				TEXT("Driver crashed while creating texture: %ux%ux%u %s(0x%08x) with %u mips, PF_ %d"),
				TextureDesc->Width,
				TextureDesc->Height,
				TextureDesc->ArraySize,
				GetD3D11TextureFormatString(TextureDesc->Format),
				(uint32)TextureDesc->Format,
				TextureDesc->MipLevels,
				UEFormat
				);
		}
	}
#endif // #if GUARDED_TEXTURE_CREATES
}

template<typename BaseResourceType>
TD3D11Texture2D<BaseResourceType>* FD3D11DynamicRHI::CreateD3D11Texture2D(uint32 SizeX,uint32 SizeY,uint32 SizeZ,bool bTextureArray,bool bCubeTexture,uint8 Format,
	uint32 NumMips,uint32 NumSamples,uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	check(SizeX > 0 && SizeY > 0 && NumMips > 0);

	if (bCubeTexture)
	{
		checkf(SizeX <= GetMaxCubeTextureDimension(), TEXT("Requested cube texture size too large: %i, %i"), SizeX, GetMaxCubeTextureDimension());
		check(SizeX == SizeY);
	}
	else
	{
		checkf(SizeX <= GetMax2DTextureDimension(), TEXT("Requested texture2d x size too large: %i, %i"), SizeX, GetMax2DTextureDimension());
		checkf(SizeY <= GetMax2DTextureDimension(), TEXT("Requested texture2d y size too large: %i, %i"), SizeY, GetMax2DTextureDimension());
	}

	if (bTextureArray)
	{
		checkf(SizeZ <= GetMaxTextureArrayLayers(), TEXT("Requested texture array size too large: %i, %i"), SizeZ, GetMaxTextureArrayLayers());
	}

	// Render target allocation with UAV flag will silently fail in feature level 10
	check(FeatureLevel >= D3D_FEATURE_LEVEL_11_0 || !(Flags & TexCreate_UAV));

	SCOPE_CYCLE_COUNTER(STAT_D3D11CreateTextureTime);

	bool bPooledTexture = true;

	const bool bSRGB = (Flags & TexCreate_SRGB) != 0;

	const DXGI_FORMAT PlatformResourceFormat = FD3D11DynamicRHI::GetPlatformTextureResourceFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, Flags);
	const DXGI_FORMAT PlatformShaderResourceFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);
	const DXGI_FORMAT PlatformRenderTargetFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);
	
	// Determine the MSAA settings to use for the texture.
	D3D11_DSV_DIMENSION DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	D3D11_RTV_DIMENSION RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	D3D11_SRV_DIMENSION ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	uint32 CPUAccessFlags = 0;
	D3D11_USAGE TextureUsage = D3D11_USAGE_DEFAULT;
	bool bCreateShaderResource = true;

	uint32 ActualMSAACount = NumSamples;

	uint32 ActualMSAAQuality = GetMaxMSAAQuality(ActualMSAACount);

	// 0xffffffff means not supported
	if (ActualMSAAQuality == 0xffffffff || (Flags & TexCreate_Shared) != 0)
	{
		// no MSAA
		ActualMSAACount = 1;
		ActualMSAAQuality = 0;
	}

	if(ActualMSAACount > 1)
	{
		DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		bPooledTexture = false;
	}

	if (NumMips < 1 || SizeX != SizeY || (1 << (NumMips - 1)) != SizeX || (Flags & TexCreate_Shared) != 0)
	{
		bPooledTexture = false;
	}

	if (Flags & TexCreate_CPUReadback)
	{
		check(!(Flags & TexCreate_RenderTargetable));
		check(!(Flags & TexCreate_DepthStencilTargetable));
		check(!(Flags & TexCreate_ShaderResource));

		CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		TextureUsage = D3D11_USAGE_STAGING;
		bCreateShaderResource = false;
	}

	if (Flags & TexCreate_CPUWritable)
	{
		CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		TextureUsage = D3D11_USAGE_STAGING;
		bCreateShaderResource = false;
	}

	// Describe the texture.
	D3D11_TEXTURE2D_DESC TextureDesc;
	ZeroMemory( &TextureDesc, sizeof( D3D11_TEXTURE2D_DESC ) );
	TextureDesc.Width = SizeX;
	TextureDesc.Height = SizeY;
	TextureDesc.MipLevels = NumMips;
	TextureDesc.ArraySize = SizeZ;
	TextureDesc.Format = PlatformResourceFormat;
	TextureDesc.SampleDesc.Count = ActualMSAACount;
	TextureDesc.SampleDesc.Quality = ActualMSAAQuality;
	TextureDesc.Usage = TextureUsage;
	TextureDesc.BindFlags = bCreateShaderResource? D3D11_BIND_SHADER_RESOURCE : 0;
	TextureDesc.CPUAccessFlags = CPUAccessFlags;
	TextureDesc.MiscFlags = bCubeTexture ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

	// NV12 doesn't support SRV in NV12 format so don't create SRV for it.
	// Todo: add support for SRVs of underneath luminance & chrominance textures.
	if (Format == PF_NV12)
	{
		// JoeG - I moved this to below the bind flags because it is valid to bind R8 or B8G8 to this
		// And creating a SRV afterward would fail because of the missing bind flags
		bCreateShaderResource = false;
	}

	if (Flags & TexCreate_DisableSRVCreation)
	{
		bCreateShaderResource = false;
	}

	if (Flags & TexCreate_Shared)
	{
		TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
	}

	if (Flags & TexCreate_GenerateMipCapable)
	{
		// Set the flag that allows us to call GenerateMips on this texture later
		TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		bPooledTexture = false;
	}

	// Set up the texture bind flags.
	bool bCreateRTV = false;
	bool bCreateDSV = false;
	bool bCreatedRTVPerSlice = false;

	if(Flags & TexCreate_RenderTargetable)
	{
		check(!(Flags & TexCreate_DepthStencilTargetable));
		check(!(Flags & TexCreate_ResolveTargetable));		
		TextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		bCreateRTV = true;		
	}
	else if(Flags & TexCreate_DepthStencilTargetable)
	{
		check(!(Flags & TexCreate_RenderTargetable));
		check(!(Flags & TexCreate_ResolveTargetable));
		TextureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL; 
		bCreateDSV = true;
	}
	else if(Flags & TexCreate_ResolveTargetable)
	{
		check(!(Flags & TexCreate_RenderTargetable));
		check(!(Flags & TexCreate_DepthStencilTargetable));
		if(Format == PF_DepthStencil || Format == PF_ShadowDepth || Format == PF_D24)
		{
			TextureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL; 
			bCreateDSV = true;
		}
		else
		{
			TextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			bCreateRTV = true;
		}
	}
	// NV12 doesn't support RTV in NV12 format so don't create RTV for it.
	// Todo: add support for RTVs of underneath luminance & chrominance textures.
	if (Format == PF_NV12)
	{
		bCreateRTV = false;
	}

	if (Flags & TexCreate_UAV)
	{
		TextureDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		bPooledTexture = false;
	}

	if (bCreateDSV && !(Flags & TexCreate_ShaderResource))
	{
		TextureDesc.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;
		bCreateShaderResource = false;
	}

	if (bCreateDSV || bCreateRTV || bCubeTexture || bTextureArray)
	{
		bPooledTexture = false;
	}

	FVRamAllocation VRamAllocation;

	if (FPlatformMemory::SupportsFastVRAMMemory())
	{
		if (Flags & TexCreate_FastVRAM)
		{
			VRamAllocation = FFastVRAMAllocator::GetFastVRAMAllocator()->AllocTexture2D(TextureDesc);
		}
	}

	TRefCountPtr<ID3D11Texture2D> TextureResource;
	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	TRefCountPtr<ID3D11DepthStencilView> DepthStencilViews[FExclusiveDepthStencil::MaxIndex];
	
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	// Turn off pooling when we are using virtual textures or the texture is offline processed as we control when the memory is released
	if ( (Flags & (TexCreate_Virtual | TexCreate_OfflineProcessed)) != 0 )
	{
		bPooledTexture = false;
	}
	void* RawTextureMemory = nullptr;
#else
	Flags &= ~TexCreate_Virtual;
#endif

	if (bPooledTexture)
	{
		FPooledTexture2D PooledTexture;
		if (GetPooledTexture2D(NumMips, (EPixelFormat)Format, &PooledTexture))
		{
			TextureResource = PooledTexture.Resource;
		}
	}

	if (!IsValidRef(TextureResource))
	{
		TArray<D3D11_SUBRESOURCE_DATA> SubResourceData;

		if (CreateInfo.BulkData)
		{
			uint8* Data = (uint8*)CreateInfo.BulkData->GetResourceBulkData();

			// each mip of each array slice counts as a subresource
			SubResourceData.AddZeroed(NumMips * SizeZ);

			uint32 SliceOffset = 0;
			for (uint32 ArraySliceIndex = 0; ArraySliceIndex < SizeZ; ++ArraySliceIndex)
			{			
				uint32 MipOffset = 0;
				for(uint32 MipIndex = 0;MipIndex < NumMips;++MipIndex)
				{
					uint32 DataOffset = SliceOffset + MipOffset;
					uint32 SubResourceIndex = ArraySliceIndex * NumMips + MipIndex;

					uint32 NumBlocksX = FMath::Max<uint32>(1,(SizeX >> MipIndex) / GPixelFormats[Format].BlockSizeX);
					uint32 NumBlocksY = FMath::Max<uint32>(1,(SizeY >> MipIndex) / GPixelFormats[Format].BlockSizeY);

					SubResourceData[SubResourceIndex].pSysMem = &Data[DataOffset];
					SubResourceData[SubResourceIndex].SysMemPitch      =  NumBlocksX * GPixelFormats[Format].BlockBytes;
					SubResourceData[SubResourceIndex].SysMemSlicePitch =  NumBlocksX * NumBlocksY * SubResourceData[MipIndex].SysMemPitch;

					MipOffset                                  += NumBlocksY * SubResourceData[MipIndex].SysMemPitch;
				}
				SliceOffset += MipOffset;
			}
		}

#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
		if ( (Flags & (TexCreate_Virtual | TexCreate_OfflineProcessed)) != 0 )
		{
			RawTextureMemory = CreateVirtualTexture(SizeX, SizeY, SizeZ, NumMips, bCubeTexture, Flags, &TextureDesc, &TextureResource);
		}
		else
#endif
		{
			SafeCreateTexture2D(Direct3DDevice, Format, &TextureDesc, CreateInfo.BulkData != NULL ? (const D3D11_SUBRESOURCE_DATA*)SubResourceData.GetData() : NULL, TextureResource.GetInitReference());
		}

		if(bCreateRTV)
		{
			// Create a render target view for each mip
			for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				if ((Flags & TexCreate_TargetArraySlicesIndependently) && (bTextureArray || bCubeTexture))
				{
					bCreatedRTVPerSlice = true;

					for (uint32 SliceIndex = 0; SliceIndex < TextureDesc.ArraySize; SliceIndex++)
					{
						D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
						FMemory::Memzero(&RTVDesc,sizeof(RTVDesc));
						RTVDesc.Format = PlatformRenderTargetFormat;
						RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
						RTVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
						RTVDesc.Texture2DArray.ArraySize = 1;
						RTVDesc.Texture2DArray.MipSlice = MipIndex;

						TRefCountPtr<ID3D11RenderTargetView> RenderTargetView;
						VERIFYD3D11RESULT_EX(Direct3DDevice->CreateRenderTargetView(TextureResource,&RTVDesc,RenderTargetView.GetInitReference()), Direct3DDevice);
						RenderTargetViews.Add(RenderTargetView);
					}
				}
				else
				{
					D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
					FMemory::Memzero(&RTVDesc,sizeof(RTVDesc));
					RTVDesc.Format = PlatformRenderTargetFormat;
					if (bTextureArray || bCubeTexture)
					{
						RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
						RTVDesc.Texture2DArray.FirstArraySlice = 0;
						RTVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
						RTVDesc.Texture2DArray.MipSlice = MipIndex;
					}
					else
					{
						RTVDesc.ViewDimension = RenderTargetViewDimension;
						RTVDesc.Texture2D.MipSlice = MipIndex;
					}

					TRefCountPtr<ID3D11RenderTargetView> RenderTargetView;
					VERIFYD3D11RESULT_EX(Direct3DDevice->CreateRenderTargetView(TextureResource,&RTVDesc,RenderTargetView.GetInitReference()), Direct3DDevice);
					RenderTargetViews.Add(RenderTargetView);
				}
			}
		}
	
		if(bCreateDSV)
		{
			// Create a depth-stencil-view for the texture.
			D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc;
			FMemory::Memzero(&DSVDesc,sizeof(DSVDesc));
			DSVDesc.Format = FindDepthStencilDXGIFormat(PlatformResourceFormat);
			if(bTextureArray || bCubeTexture)
			{
				DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				DSVDesc.Texture2DArray.FirstArraySlice = 0;
				DSVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
				DSVDesc.Texture2DArray.MipSlice = 0;
			}
			else
			{
				DSVDesc.ViewDimension = DepthStencilViewDimension;
				DSVDesc.Texture2D.MipSlice = 0;
			}

			for (uint32 AccessType = 0; AccessType < FExclusiveDepthStencil::MaxIndex; ++AccessType)
			{
				// Create a read-only access views for the texture.
				// Read-only DSVs are not supported in Feature Level 10 so 
				// a dummy DSV is created in order reduce logic complexity at a higher-level.
				if(Direct3DDevice->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0 || Direct3DDevice->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_1)
				{
					DSVDesc.Flags = (AccessType & FExclusiveDepthStencil::DepthRead_StencilWrite) ? D3D11_DSV_READ_ONLY_DEPTH : 0;
					if(HasStencilBits(DSVDesc.Format))
					{
						DSVDesc.Flags |= (AccessType & FExclusiveDepthStencil::DepthWrite_StencilRead) ? D3D11_DSV_READ_ONLY_STENCIL : 0;
					}
				}
				VERIFYD3D11RESULT_EX(Direct3DDevice->CreateDepthStencilView(TextureResource,&DSVDesc,DepthStencilViews[AccessType].GetInitReference()), Direct3DDevice);
			}
		}
	}
	check(IsValidRef(TextureResource));

	// Create a shader resource view for the texture.
	if (bCreateShaderResource)
	{
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
			SRVDesc.Format = PlatformShaderResourceFormat;

			if (bCubeTexture && bTextureArray)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
				SRVDesc.TextureCubeArray.MostDetailedMip = 0;
				SRVDesc.TextureCubeArray.MipLevels = NumMips;
				SRVDesc.TextureCubeArray.First2DArrayFace = 0;
				SRVDesc.TextureCubeArray.NumCubes = SizeZ / 6;
			}
			else if(bCubeTexture)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				SRVDesc.TextureCube.MostDetailedMip = 0;
				SRVDesc.TextureCube.MipLevels = NumMips;
			}
			else if(bTextureArray)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				SRVDesc.Texture2DArray.MostDetailedMip = 0;
				SRVDesc.Texture2DArray.MipLevels = NumMips;
				SRVDesc.Texture2DArray.FirstArraySlice = 0;
				SRVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
			}
			else
			{
				SRVDesc.ViewDimension = ShaderResourceViewDimension;
				SRVDesc.Texture2D.MostDetailedMip = 0;
				SRVDesc.Texture2D.MipLevels = NumMips;
			}
			VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(TextureResource,&SRVDesc,ShaderResourceView.GetInitReference()), Direct3DDevice);
		}

		check(IsValidRef(ShaderResourceView));
	}

	TD3D11Texture2D<BaseResourceType>* Texture2D = new TD3D11Texture2D<BaseResourceType>(
		this,
		TextureResource,
		ShaderResourceView,
		bCreatedRTVPerSlice,
		TextureDesc.ArraySize,
		RenderTargetViews,
		DepthStencilViews,
		SizeX,
		SizeY,
		SizeZ,
		NumMips,
		ActualMSAACount,
		(EPixelFormat)Format,
		bCubeTexture,
		Flags,
		bPooledTexture,
		CreateInfo.ClearValueBinding
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
		, RawTextureMemory
#endif
		);

	Texture2D->ResourceInfo.VRamAllocation = VRamAllocation;

	if (Flags & TexCreate_RenderTargetable)
	{
		Texture2D->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
	}

	D3D11TextureAllocated(*Texture2D);
	
#if !PLATFORM_HOLOLENS
	if (IsRHIDeviceNVIDIA() && (Flags & TexCreate_AFRManual))
	{
		// get a resource handle for this texture
		void* IHVHandle = nullptr;
		//getobjecthandle not threadsafe
		NvAPI_D3D_GetObjectHandleForResource(Direct3DDevice, Texture2D->GetResource(), (NVDX_ObjectHandle*)&(IHVHandle));
		Texture2D->SetIHVResourceHandle(IHVHandle);
		
		NvU32 ManualAFR = 1;
		NvAPI_D3D_SetResourceHint(Direct3DDevice, (NVDX_ObjectHandle)IHVHandle, NVAPI_D3D_SRH_CATEGORY_SLI, NVAPI_D3D_SRH_SLI_APP_CONTROLLED_INTERFRAME_CONTENT_SYNC, &ManualAFR);
	}
#endif
	if (CreateInfo.BulkData)
	{
		CreateInfo.BulkData->Discard();
	}

	return Texture2D;
}

FD3D11Texture3D* FD3D11DynamicRHI::CreateD3D11Texture3D(uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 Format,uint32 NumMips,uint32 Flags,FRHIResourceCreateInfo& CreateInfo)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D11CreateTextureTime);
	
	const bool bSRGB = (Flags & TexCreate_SRGB) != 0;

	const DXGI_FORMAT PlatformResourceFormat = (DXGI_FORMAT)GPixelFormats[Format].PlatformFormat;
	const DXGI_FORMAT PlatformShaderResourceFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);
	const DXGI_FORMAT PlatformRenderTargetFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);

	// Describe the texture.
	D3D11_TEXTURE3D_DESC TextureDesc;
	ZeroMemory( &TextureDesc, sizeof( D3D11_TEXTURE3D_DESC ) );
	TextureDesc.Width = SizeX;
	TextureDesc.Height = SizeY;
	TextureDesc.Depth = SizeZ;
	TextureDesc.MipLevels = NumMips;
	TextureDesc.Format = PlatformResourceFormat;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = 0;

	if (Flags & TexCreate_GenerateMipCapable)
	{
		// Set the flag that allows us to call GenerateMips on this texture later
		TextureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
	}

	if (Flags & TexCreate_UAV)
	{
		TextureDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	bool bCreateRTV = false;

	if(Flags & TexCreate_RenderTargetable)
	{
		TextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;		
		bCreateRTV = true;
	}

	// Set up the texture bind flags.
	check(!(Flags & TexCreate_DepthStencilTargetable));
	check(!(Flags & TexCreate_ResolveTargetable));
	check(Flags & TexCreate_ShaderResource);


	TArray<D3D11_SUBRESOURCE_DATA> SubResourceData;

	if (CreateInfo.BulkData)
	{
		uint8* Data = (uint8*)CreateInfo.BulkData->GetResourceBulkData();
		SubResourceData.AddZeroed(NumMips);
		uint32 MipOffset = 0;
		for(uint32 MipIndex = 0;MipIndex < NumMips;++MipIndex)
		{
			SubResourceData[MipIndex].pSysMem = &Data[MipOffset];
			SubResourceData[MipIndex].SysMemPitch      =  FMath::Max<uint32>(1,SizeX >> MipIndex) * GPixelFormats[Format].BlockBytes;
			SubResourceData[MipIndex].SysMemSlicePitch =  FMath::Max<uint32>(1,SizeY >> MipIndex) * SubResourceData[MipIndex].SysMemPitch;
			MipOffset                                  += FMath::Max<uint32>(1,SizeZ >> MipIndex) * SubResourceData[MipIndex].SysMemSlicePitch;
		}
	}

	FVRamAllocation VRamAllocation;

	if (FPlatformMemory::SupportsFastVRAMMemory())
	{
		if (Flags & TexCreate_FastVRAM)
		{
			VRamAllocation = FFastVRAMAllocator::GetFastVRAMAllocator()->AllocTexture3D(TextureDesc);
		}
	}

	TRefCountPtr<ID3D11Texture3D> TextureResource;
	const D3D11_SUBRESOURCE_DATA* SubResData = CreateInfo.BulkData != nullptr ? (const D3D11_SUBRESOURCE_DATA*)SubResourceData.GetData() : nullptr;
	VERIFYD3D11CREATETEXTURERESULT(
		Direct3DDevice->CreateTexture3D(&TextureDesc, SubResData,TextureResource.GetInitReference()),
		Format,
		SizeX,
		SizeY,
		SizeZ,
		PlatformShaderResourceFormat,
		NumMips,
		TextureDesc.BindFlags,
		TextureDesc.Usage,
		TextureDesc.CPUAccessFlags,
		TextureDesc.MiscFlags,
		0,
		0,
		SubResData ? SubResData->pSysMem : nullptr,
		SubResData ? SubResData->SysMemPitch : 0,
		SubResData ? SubResData->SysMemSlicePitch : 0,
		Direct3DDevice
		);

	// Create a shader resource view for the texture.
	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
		SRVDesc.Format = PlatformShaderResourceFormat;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		SRVDesc.Texture3D.MipLevels = NumMips;
		SRVDesc.Texture3D.MostDetailedMip = 0;
		VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(TextureResource,&SRVDesc,ShaderResourceView.GetInitReference()), Direct3DDevice);
	}

	TRefCountPtr<ID3D11RenderTargetView> RenderTargetView;
	if(bCreateRTV)
	{
		// Create a render-target-view for the texture.
		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
		FMemory::Memzero(&RTVDesc,sizeof(RTVDesc));
		RTVDesc.Format = PlatformRenderTargetFormat;
		RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
		RTVDesc.Texture3D.MipSlice = 0;
		RTVDesc.Texture3D.FirstWSlice = 0;
		RTVDesc.Texture3D.WSize = SizeZ;

		VERIFYD3D11RESULT_EX(Direct3DDevice->CreateRenderTargetView(TextureResource,&RTVDesc,RenderTargetView.GetInitReference()), Direct3DDevice);
	}

	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	RenderTargetViews.Add(RenderTargetView);
	FD3D11Texture3D* Texture3D = new FD3D11Texture3D(this,TextureResource,ShaderResourceView,RenderTargetViews,SizeX,SizeY,SizeZ,NumMips,(EPixelFormat)Format,Flags, CreateInfo.ClearValueBinding);

	Texture3D->ResourceInfo.VRamAllocation = VRamAllocation;

	if (Flags & TexCreate_RenderTargetable)
	{
		Texture3D->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
	}

	D3D11TextureAllocated(*Texture3D);
#if !PLATFORM_HOLOLENS
	if (IsRHIDeviceNVIDIA() && (Flags & TexCreate_AFRManual))
	{
		// get a resource handle for this texture
		void* IHVHandle = nullptr;
		//getobjecthandle not threadsafe
		NvAPI_D3D_GetObjectHandleForResource(Direct3DDevice, Texture3D->GetResource(), (NVDX_ObjectHandle*)&(IHVHandle));
		Texture3D->SetIHVResourceHandle(IHVHandle);

		NvU32 ManualAFR = 1;
		NvAPI_D3D_SetResourceHint(Direct3DDevice, (NVDX_ObjectHandle)IHVHandle, NVAPI_D3D_SRH_CATEGORY_SLI, NVAPI_D3D_SRH_SLI_APP_CONTROLLED_INTERFRAME_CONTENT_SYNC, &ManualAFR);
	}
#endif
	if (CreateInfo.BulkData)
	{
		CreateInfo.BulkData->Discard();
	}

	return Texture3D;
}

/*-----------------------------------------------------------------------------
	2D texture support.
-----------------------------------------------------------------------------*/

FTexture2DRHIRef FD3D11DynamicRHI::RHICreateTexture2D(uint32 SizeX,uint32 SizeY,uint8 Format,uint32 NumMips,uint32 NumSamples,uint32 Flags,FRHIResourceCreateInfo& CreateInfo)
{
	return CreateD3D11Texture2D<FD3D11BaseTexture2D>(SizeX,SizeY,1,false,false,Format,NumMips,NumSamples,Flags,CreateInfo);
}

FTexture2DRHIRef FD3D11DynamicRHI::RHICreateTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 SizeX,
	uint32 SizeY,
	uint8 Format,
	uint32 NumMips,
	uint32 NumSamples,
	uint32 Flags,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTexture2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
}

FTexture2DRHIRef FD3D11DynamicRHI::RHIAsyncCreateTexture2D(uint32 SizeX,uint32 SizeY,uint8 Format,uint32 NumMips,uint32 Flags,void** InitialMipData,uint32 NumInitialMips)
{
	FD3D11Texture2D* NewTexture = NULL;
	TRefCountPtr<ID3D11Texture2D> TextureResource;
	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	D3D11_TEXTURE2D_DESC TextureDesc = {0};
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;

	D3D11_SUBRESOURCE_DATA SubResourceData[ MAX_TEXTURE_MIP_COUNT ];
	FPlatformMemory::Memzero( SubResourceData, sizeof( D3D11_SUBRESOURCE_DATA ) * MAX_TEXTURE_MIP_COUNT );

	uint32 InvalidFlags = TexCreate_RenderTargetable | TexCreate_ResolveTargetable | TexCreate_DepthStencilTargetable | TexCreate_GenerateMipCapable | TexCreate_UAV | TexCreate_Presentable | TexCreate_CPUReadback;
	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	
	check(GRHISupportsAsyncTextureCreation);
	check((Flags & InvalidFlags) == 0);

	const DXGI_FORMAT PlatformResourceFormat = (DXGI_FORMAT)GPixelFormats[Format].PlatformFormat;
	const DXGI_FORMAT PlatformShaderResourceFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat,((Flags & TexCreate_SRGB) != 0));

	TextureDesc.Width = SizeX;
	TextureDesc.Height = SizeY;
	TextureDesc.MipLevels = NumMips;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = PlatformResourceFormat;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = 0;

	for (uint32 MipIndex = 0; MipIndex < NumInitialMips; ++MipIndex)
	{
		uint32 NumBlocksX = FMath::Max<uint32>(1,(SizeX >> MipIndex) / GPixelFormats[Format].BlockSizeX);
		uint32 NumBlocksY = FMath::Max<uint32>(1,(SizeY >> MipIndex) / GPixelFormats[Format].BlockSizeY);

		SubResourceData[MipIndex].pSysMem = InitialMipData[MipIndex];
		SubResourceData[MipIndex].SysMemPitch = NumBlocksX * GPixelFormats[Format].BlockBytes;
		SubResourceData[MipIndex].SysMemSlicePitch = NumBlocksX * NumBlocksY * GPixelFormats[Format].BlockBytes;
	}

	void* TempBuffer = ZeroBuffer;
	uint32 TempBufferSize = ZeroBufferSize;
	for (uint32 MipIndex = NumInitialMips; MipIndex < NumMips; ++MipIndex)
	{
		uint32 NumBlocksX = FMath::Max<uint32>(1,(SizeX >> MipIndex) / GPixelFormats[Format].BlockSizeX);
		uint32 NumBlocksY = FMath::Max<uint32>(1,(SizeY >> MipIndex) / GPixelFormats[Format].BlockSizeY);
		uint32 MipSize = NumBlocksX * NumBlocksY * GPixelFormats[Format].BlockBytes;

		if (MipSize > TempBufferSize)
		{
			UE_LOG(LogD3D11RHI, Display,TEXT("Temp texture streaming buffer not large enough, needed %d bytes"),MipSize);
			check(TempBufferSize == ZeroBufferSize);
			TempBufferSize = MipSize;
			TempBuffer = FMemory::Malloc(TempBufferSize);
			FMemory::Memzero(TempBuffer,TempBufferSize);
		}

		SubResourceData[MipIndex].pSysMem = TempBuffer;
		SubResourceData[MipIndex].SysMemPitch = NumBlocksX * GPixelFormats[Format].BlockBytes;
		SubResourceData[MipIndex].SysMemSlicePitch = MipSize;
	}

	SafeCreateTexture2D(Direct3DDevice, Format, &TextureDesc,SubResourceData,TextureResource.GetInitReference());

	if (TempBufferSize != ZeroBufferSize)
	{
		FMemory::Free(TempBuffer);
	}

	SRVDesc.Format = PlatformShaderResourceFormat;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = NumMips;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(TextureResource,&SRVDesc,ShaderResourceView.GetInitReference()), Direct3DDevice);

	NewTexture = new FD3D11Texture2D(
		this,
		TextureResource,
		ShaderResourceView,
		false,
		1,
		RenderTargetViews,
		/*DepthStencilViews=*/ NULL,
		SizeX,
		SizeY,
		0,
		NumMips,
		/*ActualMSAACount=*/ 1,
		(EPixelFormat)Format,
		/*bInCubemap=*/ false,
		Flags,
		/*bPooledTexture=*/ false,
		FClearValueBinding()
		);

	D3D11TextureAllocated(*NewTexture);
	
	return NewTexture;
}

void FD3D11DynamicRHI::RHICopySharedMips(FRHITexture2D* DestTexture2DRHI, FRHITexture2D* SrcTexture2DRHI)
{
	FD3D11Texture2D* DestTexture2D = ResourceCast(DestTexture2DRHI);
	FD3D11Texture2D* SrcTexture2D = ResourceCast(SrcTexture2DRHI);

	// Use the GPU to asynchronously copy the old mip-maps into the new texture.
	const uint32 NumSharedMips = FMath::Min(DestTexture2D->GetNumMips(),SrcTexture2D->GetNumMips());
	const uint32 SourceMipOffset = SrcTexture2D->GetNumMips() - NumSharedMips;
	const uint32 DestMipOffset   = DestTexture2D->GetNumMips() - NumSharedMips;
	for(uint32 MipIndex = 0;MipIndex < NumSharedMips;++MipIndex)
	{
		// Use the GPU to copy between mip-maps.
		Direct3DDeviceIMContext->CopySubresourceRegion(
			DestTexture2D->GetResource(),
			D3D11CalcSubresource(MipIndex + DestMipOffset,0,DestTexture2D->GetNumMips()),
			0,
			0,
			0,
			SrcTexture2D->GetResource(),
			D3D11CalcSubresource(MipIndex + SourceMipOffset,0,SrcTexture2D->GetNumMips()),
			NULL
			);
	}
}

FTexture2DArrayRHIRef FD3D11DynamicRHI::RHICreateTexture2DArray(uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 Format,uint32 NumMips,uint32 NumSamples,uint32 Flags,FRHIResourceCreateInfo& CreateInfo)
{
	check(SizeZ >= 1);
	return CreateD3D11Texture2D<FD3D11BaseTexture2DArray>(SizeX,SizeY,SizeZ,true,false,Format,NumMips,NumSamples,Flags,CreateInfo);
}

FTexture2DArrayRHIRef FD3D11DynamicRHI::RHICreateTexture2DArray_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 SizeX,
	uint32 SizeY,
	uint32 SizeZ,
	uint8 Format,
	uint32 NumMips,
	uint32 NumSamples,
	uint32 Flags,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
}

FTexture3DRHIRef FD3D11DynamicRHI::RHICreateTexture3D(uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 Format,uint32 NumMips,uint32 Flags,FRHIResourceCreateInfo& CreateInfo)
{
	check(SizeZ >= 1);
	return CreateD3D11Texture3D(SizeX,SizeY,SizeZ,Format,NumMips,Flags,CreateInfo);
}

FTexture3DRHIRef FD3D11DynamicRHI::RHICreateTexture3D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 SizeX,
	uint32 SizeY,
	uint32 SizeZ,
	uint8 Format,
	uint32 NumMips,
	uint32 Flags,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
}

void FD3D11DynamicRHI::RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo)
{
	if(Ref)
	{
		OutInfo = Ref->ResourceInfo;
	}
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView(FRHITexture* TextureRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(TextureRHI);

	// Create a Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
	DXGI_FORMAT BaseTextureFormat = DXGI_FORMAT_UNKNOWN;

	if (TextureRHI->GetTexture3D() != NULL)
	{
		FD3D11Texture3D* Texture3D = static_cast<FD3D11Texture3D*>(Texture);

		D3D11_TEXTURE3D_DESC TextureDesc;
		Texture3D->GetResource()->GetDesc(&TextureDesc);
		BaseTextureFormat = TextureDesc.Format;

		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		SRVDesc.Texture3D.MostDetailedMip = CreateInfo.MipLevel;
		SRVDesc.Texture3D.MipLevels = CreateInfo.NumMipLevels;
	}
	else if (TextureRHI->GetTexture2DArray() != NULL)
	{
		FD3D11Texture2DArray* Texture2DArray = static_cast<FD3D11Texture2DArray*>(Texture);

		D3D11_TEXTURE2D_DESC TextureDesc;
		Texture2DArray->GetResource()->GetDesc(&TextureDesc);
		BaseTextureFormat = TextureDesc.Format;

		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.MostDetailedMip = CreateInfo.MipLevel;
		SRVDesc.Texture2DArray.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.Texture2DArray.FirstArraySlice = CreateInfo.FirstArraySlice;
		SRVDesc.Texture2DArray.ArraySize = (CreateInfo.NumArraySlices == 0 ? TextureDesc.ArraySize : CreateInfo.NumArraySlices);
	}
	else if (TextureRHI->GetTextureCube() != NULL)
	{
		FD3D11TextureCube* TextureCube = static_cast<FD3D11TextureCube*>(Texture);

		D3D11_TEXTURE2D_DESC TextureDesc;
		TextureCube->GetResource()->GetDesc(&TextureDesc);
		BaseTextureFormat = TextureDesc.Format;

		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.TextureCube.MostDetailedMip = CreateInfo.MipLevel;
		SRVDesc.TextureCube.MipLevels = CreateInfo.NumMipLevels;
	}
	else
	{
		FD3D11Texture2D* Texture2D = static_cast<FD3D11Texture2D*>(Texture);

		D3D11_TEXTURE2D_DESC TextureDesc;
		Texture2D->GetResource()->GetDesc(&TextureDesc);
		BaseTextureFormat = TextureDesc.Format;

		if (TextureDesc.SampleDesc.Count > 1)
		{
			///MS textures can't have mips apparently, so nothing else to set.
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		}
		else
		{
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			SRVDesc.Texture2D.MostDetailedMip = CreateInfo.MipLevel;
			SRVDesc.Texture2D.MipLevels = CreateInfo.NumMipLevels;
		}
	}

	// Allow input CreateInfo to override SRGB and/or format
	const bool bBaseSRGB = (TextureRHI->GetFlags() & TexCreate_SRGB) != 0;
	const bool bSRGB = CreateInfo.SRGBOverride != SRGBO_ForceDisable && bBaseSRGB;
	if (CreateInfo.Format != PF_Unknown)
	{
		BaseTextureFormat = (DXGI_FORMAT)GPixelFormats[CreateInfo.Format].PlatformFormat;
	}
	SRVDesc.Format = FindShaderResourceDXGIFormat(BaseTextureFormat, bSRGB);

	// Create a Shader Resource View
	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(Texture->GetResource(), &SRVDesc, (ID3D11ShaderResourceView**)ShaderResourceView.GetInitReference()), Direct3DDevice);

	return new FD3D11ShaderResourceView(ShaderResourceView, Texture);
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	return RHICreateShaderResourceView(Texture, CreateInfo);
}

/** Generates mip maps for the surface. */
void FD3D11DynamicRHI::RHIGenerateMips(FRHITexture* TextureRHI)
{
	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(TextureRHI);
	// Surface must have been created with D3D11_BIND_RENDER_TARGET for GenerateMips to work
	check(Texture->GetShaderResourceView() && Texture->GetRenderTargetView(0, -1));
	Direct3DDeviceIMContext->GenerateMips(Texture->GetShaderResourceView());

	GPUProfilingData.RegisterGPUWork(0);
}

/**
 * Computes the size in memory required by a given texture.
 *
 * @param	TextureRHI		- Texture we want to know the size of
 * @return					- Size in Bytes
 */
uint32 FD3D11DynamicRHI::RHIComputeMemorySize(FRHITexture* TextureRHI)
{
	if(!TextureRHI)
	{
		return 0;
	}
	
	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(TextureRHI);
	return Texture->GetMemorySize();
}

/**
 * Asynchronous texture copy helper
 *
 * @param NewTexture2DRHI		- Texture to reallocate
 * @param Texture2DRHI			- Texture to reallocate
 * @param NewMipCount			- New number of mip-levels
 * @param NewSizeX				- New width, in pixels
 * @param NewSizeY				- New height, in pixels
 * @param RequestStatus			- Will be decremented by 1 when the reallocation is complete (success or failure).
 * @return						- New reference to the texture, or an invalid reference upon failure
 */
void FD3D11DynamicRHI::RHIAsyncCopyTexture2DCopy(FRHITexture2D* NewTexture2DRHI, FRHITexture2D* Texture2DRHI, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	FD3D11Texture2D* Texture2D = ResourceCast(Texture2DRHI);
	FD3D11Texture2D* NewTexture2D = ResourceCast(NewTexture2DRHI);

	// Use the GPU to asynchronously copy the old mip-maps into the new texture.
	const uint32 NumSharedMips = FMath::Min(Texture2D->GetNumMips(), NewTexture2D->GetNumMips());
	const uint32 SourceMipOffset = Texture2D->GetNumMips() - NumSharedMips;
	const uint32 DestMipOffset = NewTexture2D->GetNumMips() - NumSharedMips;
	for (uint32 MipIndex = 0; MipIndex < NumSharedMips; ++MipIndex)
	{
		// Use the GPU to copy between mip-maps.
		// This is serialized with other D3D commands, so it isn't necessary to increment Counter to signal a pending asynchronous copy.
		Direct3DDeviceIMContext->CopySubresourceRegion(
			NewTexture2D->GetResource(),
			D3D11CalcSubresource(MipIndex + DestMipOffset, 0, NewTexture2D->GetNumMips()),
			0,
			0,
			0,
			Texture2D->GetResource(),
			D3D11CalcSubresource(MipIndex + SourceMipOffset, 0, Texture2D->GetNumMips()),
			NULL
		);
	}

	// Decrement the thread-safe counter used to track the completion of the reallocation, since D3D handles sequencing the
	// async mip copies with other D3D calls.
	RequestStatus->Decrement();
}


/**
 * Starts an asynchronous texture reallocation. It may complete immediately if the reallocation
 * could be performed without any reshuffling of texture memory, or if there isn't enough memory.
 * The specified status counter will be decremented by 1 when the reallocation is complete (success or failure).
 *
 * Returns a new reference to the texture, which will represent the new mip count when the reallocation is complete.
 * RHIGetAsyncReallocateTexture2DStatus() can be used to check the status of an ongoing or completed reallocation.
 *
 * @param Texture2D		- Texture to reallocate
 * @param NewMipCount	- New number of mip-levels
 * @param NewSizeX		- New width, in pixels
 * @param NewSizeY		- New height, in pixels
 * @param RequestStatus	- Will be decremented by 1 when the reallocation is complete (success or failure).
 * @return				- New reference to the texture, or an invalid reference upon failure
 */
FTexture2DRHIRef FD3D11DynamicRHI::RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2DRHI, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	FD3D11Texture2D* Texture2D = ResourceCast(Texture2DRHI);

	// Allocate a new texture.
	FRHIResourceCreateInfo CreateInfo;
	FD3D11Texture2D* NewTexture2D = CreateD3D11Texture2D<FD3D11BaseTexture2D>(NewSizeX,NewSizeY,1,false,false,Texture2D->GetFormat(),NewMipCount,1,Texture2D->GetFlags(),CreateInfo);

	RHIAsyncCopyTexture2DCopy(NewTexture2D, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);

	return NewTexture2D;
}

FTexture2DRHIRef FD3D11DynamicRHI::AsyncReallocateTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture2D,
	int32 NewMipCount,
	int32 NewSizeX,
	int32 NewSizeY,
	FThreadSafeCounter* RequestStatus)
{
	FTexture2DRHIRef NewTexture2D;

	if (ShouldNotEnqueueRHICommand())
	{
		NewTexture2D = RHIAsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}
	else
	{
		// Allocate a new texture.
		FRHIResourceCreateInfo CreateInfo;
		FD3D11Texture2D* NewTexture2DPointer = CreateD3D11Texture2D<FD3D11BaseTexture2D>(NewSizeX, NewSizeY, 1, false, false, Texture2D->GetFormat(), NewMipCount, 1, Texture2D->GetFlags(), CreateInfo);
		NewTexture2D = NewTexture2DPointer;

		RunOnRHIThread([this, NewTexture2D, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus]()
		{
			RHIAsyncCopyTexture2DCopy(NewTexture2D, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
		});
	}
	return NewTexture2D;
}

/**
 * Returns the status of an ongoing or completed texture reallocation:
 *	TexRealloc_Succeeded	- The texture is ok, reallocation is not in progress.
 *	TexRealloc_Failed		- The texture is bad, reallocation is not in progress.
 *	TexRealloc_InProgress	- The texture is currently being reallocated async.
 *
 * @param Texture2D		- Texture to check the reallocation status for
 * @return				- Current reallocation status
 */
ETextureReallocationStatus FD3D11DynamicRHI::RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted )
{
	return TexRealloc_Succeeded;
}

ETextureReallocationStatus FD3D11DynamicRHI::FinalizeAsyncReallocateTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture2D,
	bool bBlockUntilCompleted)
{
	return RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

/**
 * Cancels an async reallocation for the specified texture.
 * This should be called for the new texture, not the original.
 *
 * @param Texture				Texture to cancel
 * @param bBlockUntilCompleted	If true, blocks until the cancellation is fully completed
 * @return						Reallocation status
 */
ETextureReallocationStatus FD3D11DynamicRHI::RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted )
{
	return TexRealloc_Succeeded;
}

ETextureReallocationStatus FD3D11DynamicRHI::CancelAsyncReallocateTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture2D,
	bool bBlockUntilCompleted)
{
	return RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

template<typename RHIResourceType>
void* TD3D11Texture2D<RHIResourceType>::Lock(uint32 MipIndex,uint32 ArrayIndex,EResourceLockMode LockMode,uint32& DestStride,bool bForceLockDeferred)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D11LockTextureTime);

	// Calculate the subresource index corresponding to the specified mip-map.
	const uint32 Subresource = D3D11CalcSubresource(MipIndex,ArrayIndex,this->GetNumMips());

	// Calculate the dimensions of the mip-map.
	const uint32 BlockSizeX = GPixelFormats[this->GetFormat()].BlockSizeX;
	const uint32 BlockSizeY = GPixelFormats[this->GetFormat()].BlockSizeY;
	const uint32 BlockBytes = GPixelFormats[this->GetFormat()].BlockBytes;
	const uint32 MipSizeX = FMath::Max(this->GetSizeX() >> MipIndex,BlockSizeX);
	const uint32 MipSizeY = FMath::Max(this->GetSizeY() >> MipIndex,BlockSizeY);
	const uint32 NumBlocksX = (MipSizeX + BlockSizeX - 1) / BlockSizeX;
	const uint32 NumBlocksY = (MipSizeY + BlockSizeY - 1) / BlockSizeY;
	const uint32 MipBytes = NumBlocksX * NumBlocksY * BlockBytes;

	FD3D11LockedData LockedData;
#if	PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	if (D3DRHI->HandleSpecialLock(LockedData, MipIndex, ArrayIndex, GetFlags(), LockMode, GetResource(), RawTextureMemory, GetNumMips(), DestStride))
	{
		// nothing left to do...
	}
	else
#endif
	if( LockMode == RLM_WriteOnly )
	{
		if (!bForceLockDeferred && (Flags & TexCreate_CPUWritable))
		{
			D3D11_MAPPED_SUBRESOURCE MappedTexture;
			VERIFYD3D11RESULT_EX(D3DRHI->GetDeviceContext()->Map(GetResource(), Subresource, D3D11_MAP_WRITE, 0, &MappedTexture), D3DRHI->GetDevice());
			LockedData.SetData(MappedTexture.pData);
			LockedData.Pitch = DestStride = MappedTexture.RowPitch;
		}
		else
		{
			// If we're writing to the texture, allocate a system memory buffer to receive the new contents.
			LockedData.AllocData(MipBytes);
			LockedData.Pitch = DestStride = NumBlocksX * BlockBytes;
			LockedData.bLockDeferred = true;
		}
	}
	else
	{
		check(!bForceLockDeferred);
		// If we're reading from the texture, we create a staging resource, copy the texture contents to it, and map it.

		// Create the staging texture.
		D3D11_TEXTURE2D_DESC StagingTextureDesc;
		GetResource()->GetDesc(&StagingTextureDesc);
		StagingTextureDesc.Width = MipSizeX;
		StagingTextureDesc.Height = MipSizeY;
		StagingTextureDesc.MipLevels = 1;
		StagingTextureDesc.ArraySize = 1;
		StagingTextureDesc.Usage = D3D11_USAGE_STAGING;
		StagingTextureDesc.BindFlags = 0;
		StagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		StagingTextureDesc.MiscFlags = 0;
		TRefCountPtr<ID3D11Texture2D> StagingTexture;
		VERIFYD3D11CREATETEXTURERESULT(
			D3DRHI->GetDevice()->CreateTexture2D(&StagingTextureDesc,NULL,StagingTexture.GetInitReference()),
			RHIResourceType::GetFormat(),
			this->GetSizeX(),
			this->GetSizeY(),
			this->GetSizeZ(),
			StagingTextureDesc.Format,
			1,
			0,
			StagingTextureDesc.Usage,
			StagingTextureDesc.CPUAccessFlags,
			StagingTextureDesc.MiscFlags,
			StagingTextureDesc.SampleDesc.Count,
			StagingTextureDesc.SampleDesc.Quality,
			nullptr,
			0,
			0,
			D3DRHI->GetDevice()
			);
		LockedData.StagingResource = StagingTexture;

		// Copy the mip-map data from the real resource into the staging resource
		D3DRHI->GetDeviceContext()->CopySubresourceRegion(StagingTexture,0,0,0,0,GetResource(),Subresource,NULL);

		// Map the staging resource, and return the mapped address.
		D3D11_MAPPED_SUBRESOURCE MappedTexture;
		VERIFYD3D11RESULT_EX(D3DRHI->GetDeviceContext()->Map(StagingTexture,0,D3D11_MAP_READ,0,&MappedTexture), D3DRHI->GetDevice());
		LockedData.SetData(MappedTexture.pData);
		LockedData.Pitch = DestStride = MappedTexture.RowPitch;
	}

	// Add the lock to the outstanding lock list.
	if (!bForceLockDeferred)
	{
		D3DRHI->AddLockedData(FD3D11LockedKey(GetResource(), Subresource), LockedData);
	}
	else
	{
		RunOnRHIThread([this, Subresource, LockedData]()
		{
			D3DRHI->AddLockedData(FD3D11LockedKey(GetResource(), Subresource), LockedData);
		});
	}

	return (void*)LockedData.GetData();
}

template<typename RHIResourceType>
void TD3D11Texture2D<RHIResourceType>::Unlock(uint32 MipIndex,uint32 ArrayIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D11UnlockTextureTime);

	// Calculate the subresource index corresponding to the specified mip-map.
	const uint32 Subresource = D3D11CalcSubresource(MipIndex,ArrayIndex,this->GetNumMips());

	// Find the object that is tracking this lock and remove it from outstanding list
	FD3D11LockedData LockedData;
	verifyf(D3DRHI->RemoveLockedData(FD3D11LockedKey(GetResource(), Subresource), LockedData), TEXT("Texture is not locked"));

#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	if (D3DRHI->HandleSpecialUnlock(MipIndex, GetFlags(), GetResource(), RawTextureMemory))
	{
		// nothing left to do...
	}
	else
#endif
	if (!LockedData.bLockDeferred && (Flags & TexCreate_CPUWritable))
	{
		D3DRHI->GetDeviceContext()->Unmap(GetResource(), 0);
	}
	else if(!LockedData.StagingResource)
	{
		// If we're writing, we need to update the subresource
		D3DRHI->GetDeviceContext()->UpdateSubresource(GetResource(), Subresource, NULL, LockedData.GetData(), LockedData.Pitch, 0);
		LockedData.FreeData();
	}
	else
	{
		D3DRHI->GetDeviceContext()->Unmap(LockedData.StagingResource, 0);
	}
}

void* FD3D11DynamicRHI::RHILockTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	check(TextureRHI);
	FD3D11Texture2D* Texture = ResourceCast(TextureRHI);
	ConditionalClearShaderResource(Texture, false);
	return Texture->Lock(MipIndex,0,LockMode,DestStride);
}

void* FD3D11DynamicRHI::LockTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture,
	uint32 MipIndex,
	EResourceLockMode LockMode,
	uint32& DestStride,
	bool bLockWithinMiptail,
	bool bNeedsDefaultRHIFlush)
{
	void *LockedTexture = nullptr;

	if (ShouldNotEnqueueRHICommand())
	{
		LockedTexture = RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	else if (LockMode == RLM_ReadOnly)
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		LockedTexture = RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	else
	{
		FD3D11Texture2D* TextureD3D11 = ResourceCast(Texture);
		LockedTexture = TextureD3D11->Lock(MipIndex, 0, LockMode, DestStride, true);
	}
	return LockedTexture;
}

void FD3D11DynamicRHI::RHIUnlockTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,bool bLockWithinMiptail)
{
	check(TextureRHI);
	FD3D11Texture2D* Texture = ResourceCast(TextureRHI);
	Texture->Unlock(MipIndex,0);
}

void FD3D11DynamicRHI::UnlockTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture,
	uint32 MipIndex,
	bool bLockWithinMiptail,
	bool bNeedsDefaultRHIFlush)
{
	if (ShouldNotEnqueueRHICommand())
	{
		RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
	}
	else
	{
		RunOnRHIThread([this, Texture, MipIndex, bLockWithinMiptail]()
		{
			RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
		});
	}
}

void* FD3D11DynamicRHI::RHILockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	FD3D11Texture2DArray* Texture = ResourceCast(TextureRHI);
	ConditionalClearShaderResource(Texture, false);
	return Texture->Lock(MipIndex,TextureIndex,LockMode,DestStride);
}

void FD3D11DynamicRHI::RHIUnlockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	FD3D11Texture2DArray* Texture = ResourceCast(TextureRHI);
	Texture->Unlock(MipIndex,TextureIndex);
}

void FD3D11DynamicRHI::RHIUpdateTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,const FUpdateTextureRegion2D& UpdateRegion,uint32 SourcePitch,const uint8* SourceData)
{
	FD3D11Texture2D* Texture = ResourceCast(TextureRHI);

	D3D11_BOX DestBox =
	{
		UpdateRegion.DestX,                      UpdateRegion.DestY,                       0,
		UpdateRegion.DestX + UpdateRegion.Width, UpdateRegion.DestY + UpdateRegion.Height, 1
	};

	check(UpdateRegion.Width % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);
	check(UpdateRegion.Height % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);
	check(UpdateRegion.DestX % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);
	check(UpdateRegion.DestY % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);
	check(UpdateRegion.SrcX % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);
	check(UpdateRegion.SrcY % GPixelFormats[Texture->GetFormat()].BlockSizeX == 0);

	Direct3DDeviceIMContext->UpdateSubresource(Texture->GetResource(), MipIndex, &DestBox, SourceData, SourcePitch, 0);
}

void FD3D11DynamicRHI::UpdateTexture2D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* Texture,
	uint32 MipIndex,
	const struct FUpdateTextureRegion2D& UpdateRegion,
	uint32 SourcePitch,
	const uint8* SourceData)
{
	if (ShouldNotEnqueueRHICommand())
	{
		RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}
	else
	{
		const SIZE_T SourceDataSize = static_cast<SIZE_T>(SourcePitch) * UpdateRegion.Height;
		uint8* SourceDataCopy = (uint8*)FMemory::Malloc(SourceDataSize);
		FMemory::Memcpy(SourceDataCopy, SourceData, SourceDataSize);
		RunOnRHIThread([this, Texture, MipIndex, UpdateRegion, SourcePitch, SourceDataCopy]()
		{
			RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceDataCopy);
			FMemory::Free(SourceDataCopy);
		});
	}
}

void FD3D11DynamicRHI::RHIUpdateTexture3D(FRHITexture3D* TextureRHI,uint32 MipIndex,const FUpdateTextureRegion3D& UpdateRegion,uint32 SourceRowPitch,uint32 SourceDepthPitch,const uint8* SourceData)
{
	FD3D11Texture3D* Texture = ResourceCast(TextureRHI);

	// The engine calls this with the texture size in the region. 
	// Some platforms like D3D11 needs that to be rounded up to the block size.
	const FPixelFormatInfo& Format = GPixelFormats[Texture->GetFormat()];
	const int32 NumBlockX = FMath::DivideAndRoundUp<int32>(UpdateRegion.Width, Format.BlockSizeX);
	const int32 NumBlockY = FMath::DivideAndRoundUp<int32>(UpdateRegion.Height, Format.BlockSizeY);

	D3D11_BOX DestBox =
	{
		UpdateRegion.DestX,                      UpdateRegion.DestY,                       UpdateRegion.DestZ,
		UpdateRegion.DestX + NumBlockX * Format.BlockSizeX, UpdateRegion.DestY + NumBlockY * Format.BlockSizeY, UpdateRegion.DestZ + UpdateRegion.Depth
	};

	Direct3DDeviceIMContext->UpdateSubresource(Texture->GetResource(), MipIndex, &DestBox, SourceData, SourceRowPitch, SourceDepthPitch);
}

void FD3D11DynamicRHI::EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData)
{
	if (RHICmdList.Bypass())
	{
		RHIUpdateTexture3D(UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
		FMemory::Free(UpdateData.Data);
	}
	else
	{
		UpdateData.Texture->AddRef();
		RunOnRHIThread(
			[UpdateData]()
		{
			GD3D11RHI->RHIUpdateTexture3D(
				UpdateData.Texture,
				UpdateData.MipIndex,
				UpdateData.UpdateRegion,
				UpdateData.RowPitch,
				UpdateData.DepthPitch,
				UpdateData.Data);
			UpdateData.Texture->Release();
			FMemory::Free(UpdateData.Data);
		});
		RHICmdList.RHIThreadFence(true);
	}

	UpdateData.Data = nullptr;
}

void FD3D11DynamicRHI::UpdateTexture3D_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture3D* Texture,
	uint32 MipIndex,
	const struct FUpdateTextureRegion3D& UpdateRegion,
	uint32 SourceRowPitch,
	uint32 SourceDepthPitch,
	const uint8* SourceData)
{
	if (ShouldNotEnqueueRHICommand())
	{
		RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
	else
	{
		const SIZE_T SourceDataSize = static_cast<SIZE_T>(SourceDepthPitch) * UpdateRegion.Depth;
		uint8* SourceDataCopy = (uint8*)FMemory::Malloc(SourceDataSize);
		FMemory::Memcpy(SourceDataCopy, SourceData, SourceDataSize);
		RunOnRHIThread([this, Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceDataCopy]()
		{
			RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceDataCopy);
			FMemory::Free(SourceDataCopy);
		});
	}
}

/*-----------------------------------------------------------------------------
	Cubemap texture support.
-----------------------------------------------------------------------------*/
FTextureCubeRHIRef FD3D11DynamicRHI::RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return CreateD3D11Texture2D<FD3D11BaseTextureCube>(Size,Size,6,false,true,Format,NumMips,1,Flags,CreateInfo);
}

FTextureCubeRHIRef FD3D11DynamicRHI::RHICreateTextureCube_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 Size,
	uint8 Format,
	uint32 NumMips,
	uint32 Flags,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTextureCube(Size, Format, NumMips, Flags, CreateInfo);
}

FTextureCubeRHIRef FD3D11DynamicRHI::RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return CreateD3D11Texture2D<FD3D11BaseTextureCube>(Size,Size,6 * ArraySize,true,true,Format,NumMips,1,Flags,CreateInfo);
}

FTextureCubeRHIRef FD3D11DynamicRHI::RHICreateTextureCubeArray_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 Size,
	uint32 ArraySize,
	uint8 Format,
	uint32 NumMips,
	uint32 Flags,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, CreateInfo);
}

void* FD3D11DynamicRHI::RHILockTextureCubeFace(FRHITextureCube* TextureCubeRHI,uint32 FaceIndex,uint32 ArrayIndex,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	FD3D11TextureCube* TextureCube = ResourceCast(TextureCubeRHI);
	ConditionalClearShaderResource(TextureCube, false);
	uint32 D3DFace = GetD3D11CubeFace((ECubeFace)FaceIndex);
	return TextureCube->Lock(MipIndex,D3DFace + ArrayIndex * 6,LockMode,DestStride);
}

void* FD3D11DynamicRHI::RHILockTextureCubeFace_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITextureCube* Texture,
	uint32 FaceIndex,
	uint32 ArrayIndex,
	uint32 MipIndex,
	EResourceLockMode LockMode,
	uint32& DestStride,
	bool bLockWithinMiptail)
{
	void *LockedTexture = nullptr;

	if (ShouldNotEnqueueRHICommand())
	{
		LockedTexture = RHILockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	else if (LockMode == RLM_ReadOnly)
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		LockedTexture = RHILockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	else
	{
		FD3D11TextureCube* TextureCube = ResourceCast(Texture);
		const uint32 D3DFace = GetD3D11CubeFace((ECubeFace)FaceIndex);
		LockedTexture = TextureCube->Lock(MipIndex, D3DFace + ArrayIndex * 6, LockMode, DestStride, true);
	}
	return LockedTexture;
}

void FD3D11DynamicRHI::RHIUnlockTextureCubeFace(FRHITextureCube* TextureCubeRHI,uint32 FaceIndex,uint32 ArrayIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	FD3D11TextureCube* TextureCube = ResourceCast(TextureCubeRHI);
	uint32 D3DFace = GetD3D11CubeFace((ECubeFace)FaceIndex);
	TextureCube->Unlock(MipIndex,D3DFace + ArrayIndex * 6);
}

void FD3D11DynamicRHI::RHIUnlockTextureCubeFace_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITextureCube* Texture,
	uint32 FaceIndex,
	uint32 ArrayIndex,
	uint32 MipIndex,
	bool bLockWithinMiptail)
{
	if (ShouldNotEnqueueRHICommand())
	{
		RHIUnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
	}
	else
	{
		RunOnRHIThread([this, Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail]()
		{
			RHIUnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
		});
	}
}

void FD3D11DynamicRHI::RHIBindDebugLabelName(FRHITexture* TextureRHI, const TCHAR* Name)
{
	//todo: require names at texture creation time.
	FName DebugName(Name);
	TextureRHI->SetName(DebugName);
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	if (FD3D11Texture2D* Texture2D = (FD3D11Texture2D*)TextureRHI->GetTexture2D())
	{
		Texture2D->GetResource()->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(Name) + 1, TCHAR_TO_ANSI(Name));
	}
	else if (FD3D11TextureCube* TextureCube = (FD3D11TextureCube*)TextureRHI->GetTextureCube())
	{
		TextureCube->GetResource()->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(Name) + 1, TCHAR_TO_ANSI(Name));
	}
	else if (FD3D11Texture3D* Texture3D = (FD3D11Texture3D*)TextureRHI->GetTexture3D())
	{
		Texture3D->GetResource()->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(Name) + 1, TCHAR_TO_ANSI(Name));
	}
#endif
}

void FD3D11DynamicRHI::RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
}

void FD3D11DynamicRHI::RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
}

FTextureReferenceRHIRef FD3D11DynamicRHI::RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
{
	return new FD3D11TextureReference(this,LastRenderTime);
}

FTextureReferenceRHIRef FD3D11DynamicRHI::RHICreateTextureReference_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FLastRenderTimeContainer* LastRenderTime)
{
	return RHICreateTextureReference(LastRenderTime);
}

void FD3D11DynamicRHI::RHICopySubTextureRegion(FRHITexture2D* SourceTextureRHI, FRHITexture2D* DestinationTextureRHI, FBox2D SourceBox, FBox2D DestinationBox)
{
	FD3D11Texture2D* SourceTexture = ResourceCast(SourceTextureRHI);
	FD3D11Texture2D* DestinationTexture = ResourceCast(DestinationTextureRHI);

	//Make sure the source box is fitting on right and top side of the source texture, no need to offset the destination
	if (SourceBox.Max.X >= (float)SourceTexture->GetSizeX())
	{
		float Delta = (SourceBox.Max.X - (float)SourceTexture->GetSizeX());
		SourceBox.Max.X -= Delta;
	}
	if (SourceBox.Max.Y >= (float)SourceTexture->GetSizeY())
	{
		float Delta = (SourceBox.Max.Y - (float)SourceTexture->GetSizeY());
		SourceBox.Max.Y -= Delta;
	}

	int32 DestinationOffsetX = 0;
	int32 DestinationOffsetY = 0;
	int32 SourceStartX = SourceBox.Min.X;
	int32 SourceEndX = SourceBox.Max.X;
	int32 SourceStartY = SourceBox.Min.Y;
	int32 SourceEndY = SourceBox.Max.Y;
	//If the source box is not fitting on the left bottom side, offset the result so the destination pixel match the expectation
	if (SourceStartX < 0)
	{
		DestinationOffsetX -= SourceStartX;
		SourceStartX = 0;
	}
	if (SourceStartY < 0)
	{
		DestinationOffsetY -= SourceStartY;
		SourceStartY = 0;
	}

	D3D11_BOX SourceBoxAdjust =
	{
		static_cast<UINT>(SourceStartX),
		static_cast<UINT>(SourceStartY),
		0,
		static_cast<UINT>(SourceEndX),
		static_cast<UINT>(SourceEndY),
		1
	};

	bool bValidDest = DestinationBox.Min.X + DestinationOffsetX + (SourceEndX - SourceStartX) <= DestinationTexture->GetSizeX();
	bValidDest &= DestinationBox.Min.Y + DestinationOffsetY + (SourceEndY - SourceStartY) <= DestinationTexture->GetSizeY();
	bValidDest &= DestinationBox.Min.X <= DestinationBox.Max.X && DestinationBox.Min.Y <= DestinationBox.Max.Y;

	bool bValidSrc = SourceStartX >= 0 && SourceEndX <= (int32)SourceTexture->GetSizeX();
	bValidSrc &= SourceStartY >= 0 && SourceEndY <= (int32)SourceTexture->GetSizeY();
	bValidSrc &= SourceStartX <= SourceEndX && SourceStartY <= SourceEndY;

	if (!ensureMsgf(bValidSrc && bValidDest, TEXT("Invalid copy detected for RHICopySubTextureRegion. Skipping copy.  SrcBox: left:%i, right:%i, top:%i, bottom:%i, DstBox:left:%i, right:%i, top:%i, bottom:%i,  SrcTexSize: %i x %i, DestTexSize: %i x %i "),
		SourceBox.Min.X,
		SourceBox.Max.X,
		SourceBox.Min.Y,
		SourceBox.Max.Y,
		DestinationBox.Min.X,
		DestinationBox.Max.X,
		DestinationBox.Min.Y,
		DestinationBox.Max.Y,
		SourceTexture->GetSizeX(),
		SourceTexture->GetSizeY(),
		DestinationTexture->GetSizeX(),
		DestinationTexture->GetSizeY()))
	{
		return;
	}

	check(SourceBoxAdjust.left % GPixelFormats[SourceTexture->GetFormat()].BlockSizeX == 0);
	check(SourceBoxAdjust.top % GPixelFormats[SourceTexture->GetFormat()].BlockSizeY == 0);
	check((SourceBoxAdjust.right - SourceBoxAdjust.left) % GPixelFormats[SourceTexture->GetFormat()].BlockSizeX == 0);
	check((SourceBoxAdjust.bottom - SourceBoxAdjust.top) % GPixelFormats[SourceTexture->GetFormat()].BlockSizeY == 0);
	check(uint32(DestinationBox.Min.X + DestinationOffsetX) % GPixelFormats[DestinationTexture->GetFormat()].BlockSizeX == 0);
	check(uint32(DestinationBox.Min.Y + DestinationOffsetY) % GPixelFormats[DestinationTexture->GetFormat()].BlockSizeY == 0);

	ID3D11Texture2D* DestinationRessource = DestinationTexture->GetResource();
	Direct3DDeviceIMContext->CopySubresourceRegion(DestinationRessource, 0, DestinationBox.Min.X + DestinationOffsetX, DestinationBox.Min.Y + DestinationOffsetY, 0, SourceTexture->GetResource(), 0, &SourceBoxAdjust);
}

void FD3D11DynamicRHI::RHICopySubTextureRegion_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* SourceTexture,
	FRHITexture2D* DestinationTexture,
	FBox2D SourceBox,
	FBox2D DestinationBox)
{
	if (RHICmdList.Bypass())
	{
		RHICopySubTextureRegion(SourceTexture, DestinationTexture, SourceBox, DestinationBox);
	}
	else
	{
		RunOnRHIThread([this, SourceTexture, DestinationTexture, SourceBox, DestinationBox]()
		{
			RHICopySubTextureRegion(SourceTexture, DestinationTexture, SourceBox, DestinationBox);
		});
	}
}

void FD3D11DynamicRHI::RHIUpdateTextureReference(FRHITextureReference* TextureRefRHI, FRHITexture* NewTextureRHI)
{	
	// Updating texture references is disallowed while the RHI could be caching them in referenced resource tables.
	check(ResourceTableFrameCounter == INDEX_NONE);

	FD3D11TextureReference* TextureRef = (FD3D11TextureReference*)TextureRefRHI;
	if (TextureRef)
	{
		FD3D11TextureBase* NewTexture = NULL;
		ID3D11ShaderResourceView* NewSRV = NULL;
		if (NewTextureRHI)
		{
			NewTexture = GetD3D11TextureFromRHITexture(NewTextureRHI);
			if (NewTexture)
			{
				NewSRV = NewTexture->GetShaderResourceView();
			}
		}
		TextureRef->SetReferencedTexture(NewTextureRHI,NewTexture,NewSRV);
	}
}


template<typename BaseResourceType>
TD3D11Texture2D<BaseResourceType>* FD3D11DynamicRHI::CreateTextureFromResource(bool bTextureArray, bool bCubeTexture, EPixelFormat Format, uint32 TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D11Texture2D* TextureResource)
{
	D3D11_TEXTURE2D_DESC TextureDesc;
	TextureResource->GetDesc(&TextureDesc);

	const bool bSRGB = (TexCreateFlags & TexCreate_SRGB) != 0;

	const DXGI_FORMAT PlatformResourceFormat = FD3D11DynamicRHI::GetPlatformTextureResourceFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, TexCreateFlags);
	const DXGI_FORMAT PlatformShaderResourceFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);
	const DXGI_FORMAT PlatformRenderTargetFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);

	// Determine the MSAA settings to use for the texture.
	D3D11_DSV_DIMENSION DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	D3D11_RTV_DIMENSION RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	D3D11_SRV_DIMENSION ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

	if(TextureDesc.SampleDesc.Count > 1)
	{
		DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
	}

	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	TRefCountPtr<ID3D11DepthStencilView> DepthStencilViews[FExclusiveDepthStencil::MaxIndex];

	bool bCreatedRTVPerSlice = false;

	if(TextureDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
	{
		// Create a render target view for each mip
		for (uint32 MipIndex = 0; MipIndex < TextureDesc.MipLevels; MipIndex++)
		{
			if ((TexCreateFlags & TexCreate_TargetArraySlicesIndependently) && (bTextureArray || bCubeTexture))
			{
				bCreatedRTVPerSlice = true;

				for (uint32 SliceIndex = 0; SliceIndex < TextureDesc.ArraySize; SliceIndex++)
				{
					D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
					FMemory::Memzero(&RTVDesc,sizeof(RTVDesc));
					RTVDesc.Format = PlatformRenderTargetFormat;
					RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					RTVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
					RTVDesc.Texture2DArray.ArraySize = 1;
					RTVDesc.Texture2DArray.MipSlice = MipIndex;

					TRefCountPtr<ID3D11RenderTargetView> RenderTargetView;
					VERIFYD3D11RESULT_EX(Direct3DDevice->CreateRenderTargetView(TextureResource,&RTVDesc,RenderTargetView.GetInitReference()), Direct3DDevice);
					RenderTargetViews.Add(RenderTargetView);
				}
			}
			else
			{
				D3D11_RENDER_TARGET_VIEW_DESC RTVDesc;
				FMemory::Memzero(&RTVDesc,sizeof(RTVDesc));
				RTVDesc.Format = PlatformRenderTargetFormat;
				if (bTextureArray || bCubeTexture)
				{
					RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					RTVDesc.Texture2DArray.FirstArraySlice = 0;
					RTVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
					RTVDesc.Texture2DArray.MipSlice = MipIndex;
				}
				else
				{
					RTVDesc.ViewDimension = RenderTargetViewDimension;
					RTVDesc.Texture2D.MipSlice = MipIndex;
				}

				TRefCountPtr<ID3D11RenderTargetView> RenderTargetView;
				VERIFYD3D11RESULT_EX(Direct3DDevice->CreateRenderTargetView(TextureResource,&RTVDesc,RenderTargetView.GetInitReference()), Direct3DDevice);
				RenderTargetViews.Add(RenderTargetView);
			}
		}
	}

	if(TextureDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		// Create a depth-stencil-view for the texture.
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc;
		FMemory::Memzero(&DSVDesc,sizeof(DSVDesc));
		DSVDesc.Format = FindDepthStencilDXGIFormat(PlatformResourceFormat);
		if(bTextureArray || bCubeTexture)
		{
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DSVDesc.Texture2DArray.FirstArraySlice = 0;
			DSVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
			DSVDesc.Texture2DArray.MipSlice = 0;
		}
		else
		{
			DSVDesc.ViewDimension = DepthStencilViewDimension;
			DSVDesc.Texture2D.MipSlice = 0;
		}

		for (uint32 AccessType = 0; AccessType < FExclusiveDepthStencil::MaxIndex; ++AccessType)
		{
			// Create a read-only access views for the texture.
			// Read-only DSVs are not supported in Feature Level 10 so 
			// a dummy DSV is created in order reduce logic complexity at a higher-level.
			if(Direct3DDevice->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0 || Direct3DDevice->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_1)
			{
				DSVDesc.Flags = (AccessType & FExclusiveDepthStencil::DepthRead_StencilWrite) ? D3D11_DSV_READ_ONLY_DEPTH : 0;
				if(HasStencilBits(DSVDesc.Format))
				{
					DSVDesc.Flags |= (AccessType & FExclusiveDepthStencil::DepthWrite_StencilRead) ? D3D11_DSV_READ_ONLY_STENCIL : 0;
				}
			}
			VERIFYD3D11RESULT_EX(Direct3DDevice->CreateDepthStencilView(TextureResource,&DSVDesc,DepthStencilViews[AccessType].GetInitReference()), Direct3DDevice);
		}
	}

	// Create a shader resource view for the texture.
	if (TextureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
			SRVDesc.Format = PlatformShaderResourceFormat;

			if (bCubeTexture && bTextureArray)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
				SRVDesc.TextureCubeArray.MostDetailedMip = 0;
				SRVDesc.TextureCubeArray.MipLevels = TextureDesc.MipLevels;
				SRVDesc.TextureCubeArray.First2DArrayFace = 0;
				SRVDesc.TextureCubeArray.NumCubes = TextureDesc.ArraySize / 6;
			}
			else if(bCubeTexture)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				SRVDesc.TextureCube.MostDetailedMip = 0;
				SRVDesc.TextureCube.MipLevels = TextureDesc.MipLevels;
			}
			else if(bTextureArray)
			{
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				SRVDesc.Texture2DArray.MostDetailedMip = 0;
				SRVDesc.Texture2DArray.MipLevels = TextureDesc.MipLevels;
				SRVDesc.Texture2DArray.FirstArraySlice = 0;
				SRVDesc.Texture2DArray.ArraySize = TextureDesc.ArraySize;
			}
			else
			{
				SRVDesc.ViewDimension = ShaderResourceViewDimension;
				SRVDesc.Texture2D.MostDetailedMip = 0;
				SRVDesc.Texture2D.MipLevels = TextureDesc.MipLevels;
			}
			VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(TextureResource,&SRVDesc,ShaderResourceView.GetInitReference()), Direct3DDevice);
		}

		check(IsValidRef(ShaderResourceView));
	}

	TD3D11Texture2D<BaseResourceType>* Texture2D = new TD3D11Texture2D<BaseResourceType>(
		this,
		TextureResource,
		ShaderResourceView,
		bCreatedRTVPerSlice,
		TextureDesc.ArraySize,
		RenderTargetViews,
		DepthStencilViews,
		TextureDesc.Width,
		TextureDesc.Height,
		0,
		TextureDesc.MipLevels,
		TextureDesc.SampleDesc.Count,
		Format,
		bCubeTexture,
		TexCreateFlags,
		/*bPooledTexture=*/ false,
		ClearValueBinding
		);

	if (TexCreateFlags & TexCreate_RenderTargetable)
	{
		Texture2D->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
	}

	D3D11TextureAllocated(*Texture2D);

	return Texture2D;
}

template <typename BaseResourceType>
TD3D11Texture2D<BaseResourceType>* FD3D11DynamicRHI::CreateAliasedD3D11Texture2D(TD3D11Texture2D<BaseResourceType>* SourceTexture)
{
	D3D11_TEXTURE2D_DESC TextureDesc;
	SourceTexture->GetResource()->GetDesc(&TextureDesc);

	const bool bSRGB = (SourceTexture->Flags & TexCreate_SRGB) != 0;

	const DXGI_FORMAT PlatformResourceFormat = TextureDesc.Format;
	const DXGI_FORMAT PlatformShaderResourceFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);
	const DXGI_FORMAT PlatformRenderTargetFormat = FindShaderResourceDXGIFormat(PlatformResourceFormat, bSRGB);

	// Determine the MSAA settings to use for the texture.
	D3D11_DSV_DIMENSION DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	D3D11_RTV_DIMENSION RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	D3D11_SRV_DIMENSION ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

	if (TextureDesc.SampleDesc.Count > 1)
	{
		DepthStencilViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		RenderTargetViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		ShaderResourceViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
	}

	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	TArray<TRefCountPtr<ID3D11RenderTargetView> > RenderTargetViews;
	TRefCountPtr<ID3D11DepthStencilView> DepthStencilViews[FExclusiveDepthStencil::MaxIndex];

	bool bCreatedRTVPerSlice = false;
	const bool bCubeTexture = SourceTexture->IsCubemap();
	const bool bTextureArray = !bCubeTexture && TextureDesc.ArraySize > 1;

	if (TextureDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
	{
		// Create a render target view for each mip
		for (uint32 MipIndex = 0; MipIndex < TextureDesc.MipLevels; MipIndex++)
		{
			// Just add null RTV entries (we'll be aliasing from source shortly).
			if ((SourceTexture->Flags & TexCreate_TargetArraySlicesIndependently) && (bTextureArray || bCubeTexture))
			{
				bCreatedRTVPerSlice = true;

				for (uint32 SliceIndex = 0; SliceIndex < TextureDesc.ArraySize; SliceIndex++)
				{
					RenderTargetViews.Add(nullptr);
				}
			}
			else
			{
				RenderTargetViews.Add(nullptr);
			}
		}
	}

	TD3D11Texture2D<BaseResourceType>* Texture2D = new TD3D11Texture2D<BaseResourceType>(
		this,
		nullptr,
		nullptr,
		bCreatedRTVPerSlice,
		TextureDesc.ArraySize,
		RenderTargetViews,
		nullptr,
		TextureDesc.Width,
		TextureDesc.Height,
		0,
		TextureDesc.MipLevels,
		TextureDesc.SampleDesc.Count,
		static_cast<BaseResourceType*>(SourceTexture)->GetFormat(),
		bCubeTexture,
		SourceTexture->Flags,
		/*bPooledTexture=*/ false,
		static_cast<BaseResourceType*>(SourceTexture)->GetClearBinding()
		);

	if (SourceTexture->Flags & TexCreate_RenderTargetable)
	{
		Texture2D->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
	}

	// We'll be the same size, since we're the same thing. Avoid the check in D3D11Resources.h (AliasResources).
	Texture2D->SetMemorySize(SourceTexture->GetMemorySize());

	// Disable deprecation warning; when the DynamicRHI raw-pointer method is fully deprecated, the D3D11 class will still provide a raw pointer version
	// since this is required in this path.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	RHIAliasTextureResources(Texture2D, SourceTexture);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	return Texture2D;
}


FTexture2DRHIRef FD3D11DynamicRHI::RHICreateTexture2DFromResource(EPixelFormat Format, uint32 TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D11Texture2D* TextureResource)
{
	return CreateTextureFromResource<FD3D11BaseTexture2D>(false, false, Format, TexCreateFlags, ClearValueBinding, TextureResource);
}

FTexture2DArrayRHIRef FD3D11DynamicRHI::RHICreateTexture2DArrayFromResource(EPixelFormat Format, uint32 TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D11Texture2D* TextureResource)
{
	return CreateTextureFromResource<FD3D11BaseTexture2DArray>(true, false, Format, TexCreateFlags, ClearValueBinding, TextureResource);
}

FTextureCubeRHIRef FD3D11DynamicRHI::RHICreateTextureCubeFromResource(EPixelFormat Format, uint32 TexCreateFlags, const FClearValueBinding& ClearValueBinding, ID3D11Texture2D* TextureResource)
{
	return CreateTextureFromResource<FD3D11BaseTextureCube>(false, true, Format, TexCreateFlags, ClearValueBinding, TextureResource);
}

void FD3D11DynamicRHI::RHIAliasTextureResources(FRHITexture* DestTextureRHI, FRHITexture* SrcTextureRHI)
{
	FD3D11TextureBase* DestTexture = GetD3D11TextureFromRHITexture(DestTextureRHI);
	FD3D11TextureBase* SrcTexture = GetD3D11TextureFromRHITexture(SrcTextureRHI);

	if (DestTexture && SrcTexture)
	{
		DestTexture->AliasResources(SrcTexture);
	}
}

FTextureRHIRef FD3D11DynamicRHI::RHICreateAliasedTexture(FRHITexture* SourceTexture)
{
	if (SourceTexture->GetTexture2D() != nullptr)
	{
		return CreateAliasedD3D11Texture2D<FD3D11BaseTexture2D>(static_cast<FD3D11Texture2D*>(SourceTexture->GetTexture2D()));
	}
	else if (SourceTexture->GetTexture2DArray() != nullptr)
	{
		return CreateAliasedD3D11Texture2D<FD3D11BaseTexture2DArray>(static_cast<FD3D11Texture2DArray*>(SourceTexture->GetTexture2DArray()));
	}
	else if (SourceTexture->GetTextureCube() != nullptr)
	{
		return CreateAliasedD3D11Texture2D<FD3D11BaseTextureCube>(static_cast<FD3D11TextureCube*>(SourceTexture->GetTextureCube()));
	}

	UE_LOG(LogD3D11RHI, Error, TEXT("Currently FD3D11DynamicRHI::RHICreateAliasedTexture only supports 2D, 2D Array and Cube textures."));
	return nullptr;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void FD3D11DynamicRHI::RHIAliasTextureResources(FTextureRHIRef& DestTextureRHI, FTextureRHIRef& SrcTextureRHI)
{
	// @todo: Move the raw-pointer implementation down here when it's deprecation is completed.
	RHIAliasTextureResources((FRHITexture*)DestTextureRHI, (FRHITexture*)SrcTextureRHI);
}

FTextureRHIRef FD3D11DynamicRHI::RHICreateAliasedTexture(FTextureRHIRef& SourceTexture)
{
	// @todo: Move the raw-pointer implementation down here when it's deprecation is completed.
	return RHICreateAliasedTexture((FRHITexture*)SourceTexture);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FD3D11DynamicRHI::RHICopyTexture(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FRHICopyTextureInfo& CopyInfo)
{
	if (!SourceTextureRHI || !DestTextureRHI || SourceTextureRHI == DestTextureRHI)
	{
		// no need to do anything (silently ignored)
		return;
	}

	RHITransitionResources(EResourceTransitionAccess::EReadable, &SourceTextureRHI, 1);

	FRHICommandList_RecursiveHazardous RHICmdList(this);	

	FD3D11TextureBase* SourceTexture = GetD3D11TextureFromRHITexture(SourceTextureRHI);
	FD3D11TextureBase* DestTexture = GetD3D11TextureFromRHITexture(DestTextureRHI);

	check(SourceTexture && DestTexture);

	GPUProfilingData.RegisterGPUWork();

	if (CopyInfo.Size != FIntVector::ZeroValue)
	{
		D3D11_BOX SrcBox;
		SrcBox.left = CopyInfo.SourcePosition.X;
		SrcBox.top = CopyInfo.SourcePosition.Y;
		SrcBox.front = CopyInfo.SourcePosition.Z;

		for (uint32 SliceIndex = 0; SliceIndex < CopyInfo.NumSlices; ++SliceIndex)
		{
			uint32 SourceSliceIndex = CopyInfo.SourceSliceIndex + SliceIndex;
			uint32 DestSliceIndex = CopyInfo.DestSliceIndex + SliceIndex;

			for (uint32 MipIndex = 0; MipIndex < CopyInfo.NumMips; ++MipIndex)
			{
				uint32 SourceMipIndex = CopyInfo.SourceMipIndex + MipIndex;
				uint32 DestMipIndex = CopyInfo.DestMipIndex + MipIndex;

				const uint32 SourceSubresource = D3D11CalcSubresource(SourceMipIndex, SourceSliceIndex, SourceTextureRHI->GetNumMips());
				const uint32 DestSubresource = D3D11CalcSubresource(DestMipIndex, DestSliceIndex, DestTextureRHI->GetNumMips());

				SrcBox.right = CopyInfo.SourcePosition.X + FMath::Max(CopyInfo.Size.X >> MipIndex, 1);
				SrcBox.bottom = CopyInfo.SourcePosition.Y + FMath::Max(CopyInfo.Size.Y >> MipIndex, 1);
				SrcBox.back = CopyInfo.SourcePosition.Z + FMath::Max(CopyInfo.Size.Z >> MipIndex, 1);

				Direct3DDeviceIMContext->CopySubresourceRegion(DestTexture->GetResource(), DestSubresource, CopyInfo.DestPosition.X, CopyInfo.DestPosition.Y, CopyInfo.DestPosition.Z, SourceTexture->GetResource(), SourceSubresource, &SrcBox);
			}
		}
	}
	else
	{
		// Make sure the params are all by default when using this case
		ensure(CopyInfo.SourceSliceIndex == 0 && CopyInfo.DestSliceIndex == 0 && CopyInfo.SourcePosition == FIntVector::ZeroValue && CopyInfo.DestPosition == FIntVector::ZeroValue && CopyInfo.SourceMipIndex == 0 && CopyInfo.DestMipIndex == 0);
		Direct3DDeviceIMContext->CopyResource(DestTexture->GetResource(), SourceTexture->GetResource());
	}
}