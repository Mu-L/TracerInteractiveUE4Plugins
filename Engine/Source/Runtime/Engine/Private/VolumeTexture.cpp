// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumeTexture.cpp: UvolumeTexture implementation.
=============================================================================*/

#include "Engine/VolumeTexture.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "EngineUtils.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Containers/ResourceArray.h"

// Limit the possible depth of volume texture otherwise when the user converts 2D textures, he can crash the engine.
const int32 MAX_VOLUME_TEXTURE_DEPTH = 512;

extern RHI_API bool GUseTexture3DBulkDataRHI;

UVolumeTexture::UVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SRGB = true;
}

bool UVolumeTexture::UpdateSourceFromSourceTexture()
{
	bool bSourceValid = false;

#if WITH_EDITOR
	if (Source2DTexture && Source2DTileSizeX > 0 && Source2DTileSizeY > 0)
	{
		FTextureSource& InitialSource = Source2DTexture->Source;
		const int32 Num2DTileX = InitialSource.GetSizeX() / Source2DTileSizeX;
		const int32 Num2DTileY = InitialSource.GetSizeY() / Source2DTileSizeY;
		const int32 TileSizeZ = FMath::Min<int32>(Num2DTileX * Num2DTileY, MAX_VOLUME_TEXTURE_DEPTH);
		if (TileSizeZ > 0)
		{
			const int32 FormatDataSize = InitialSource.GetBytesPerPixel();
			if (FormatDataSize > 0)
			{
				TArray64<uint8> Ref2DData;
				if (InitialSource.GetMipData(Ref2DData, 0))
				{
					uint8* NewData = (uint8*)FMemory::Malloc(Source2DTileSizeX * Source2DTileSizeY * TileSizeZ * FormatDataSize);
					uint8* CurPos = NewData;
					
					for (int32 PosZ = 0; PosZ < TileSizeZ; ++PosZ)
					{
						const int32 RefTile2DPosX = (PosZ % Num2DTileX) * Source2DTileSizeX;
						const int32 RefTile2DPosY = ((PosZ / Num2DTileX) % Num2DTileY) * Source2DTileSizeY;

						for (int32 PosY = 0; PosY < Source2DTileSizeY; ++PosY)
						{
							const int32 Ref2DPosY = RefTile2DPosY + PosY; 

							for (int32 PosX = 0; PosX < Source2DTileSizeX; ++PosX)
							{
								const int32 Ref2DPosX = RefTile2DPosX + PosX; 
								const int32 RefPos = Ref2DPosX + Ref2DPosY * InitialSource.GetSizeX();
								FMemory::Memcpy(CurPos, Ref2DData.GetData() + RefPos * FormatDataSize, FormatDataSize);
								CurPos += FormatDataSize;
							}
						}
					}

					Source.Init(Source2DTileSizeX, Source2DTileSizeY, TileSizeZ, 1, InitialSource.GetFormat(), NewData);
					SourceLightingGuid = Source2DTexture->GetLightingGuid();
					bSourceValid = true;

					FMemory::Free(NewData);
				}
			}
		}
	}

	if (bSourceValid)
	{
		SetLightingGuid(); // Because the content has changed, use a new GUID.
	}
	else
	{
		Source.Init(0, 0, 0, 0, TSF_Invalid, nullptr);
		SourceLightingGuid.Invalidate();
	}

	UpdateMipGenSettings();
#endif // WITH_EDITOR

	return bSourceValid;
}

ENGINE_API bool UVolumeTexture::UpdateSourceFromFunction(TFunction<void(int32, int32, int32, void*)> Func, int32 SizeX, int32 SizeY, int32 SizeZ, ETextureSourceFormat Format)
{
	bool bSourceValid = false;

#if WITH_EDITOR
	if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s UpdateSourceFromFunction size in x,y, and z must be greater than zero"), *GetFullName());
		return false;
	}

	// First clear up the existing source with the requested TextureSourceFormat
	Source.Init(0, 0, 0, 1, Format, nullptr);
	// It is now possible to query the correct FormatDataSize (there is no static version of GetBytesPerPixel)
	const int32 FormatDataSize = Source.GetBytesPerPixel();

	// Allocate temp buffer used to fill texture
	uint8* const NewData = (uint8*)FMemory::Malloc(SizeX * SizeY * SizeZ * FormatDataSize);
	uint8* CurPos = NewData;

	// Temporary storage for a single voxel value extracted from the lambda, type depends on Format
	void* const NewValue = FMemory::Malloc(FormatDataSize);

	// Loop over all voxels and fill from our TFunction
	for (int32 PosZ = 0; PosZ < SizeZ; ++PosZ)
	{
		for (int32 PosY = 0; PosY < SizeY; ++PosY)
		{
			for (int32 PosX = 0; PosX < SizeX; ++PosX)
			{
				Func(PosX, PosY, PosZ, NewValue);

				FMemory::Memcpy(CurPos, NewValue, FormatDataSize);

				CurPos += FormatDataSize;
			}
		}
	}

	// Init the final source data from the temp buffer
	Source.Init(SizeX, SizeY, SizeZ, 1, Format, NewData);
	
	// Free temp buffers
	FMemory::Free(NewData);
	FMemory::Free(NewValue);

	SetLightingGuid(); // Because the content has changed, use a new GUID.

	UpdateMipGenSettings();

	// Make sure to update the texture resource so the results of filling the texture 
	UpdateResource();

	bSourceValid = true;
#endif

	return bSourceValid;
}

void UVolumeTexture::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UVolumeTexture::Serialize"), STAT_VolumeTexture_Serialize, STATGROUP_LoadTime);

	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (bCooked || Ar.IsCooking())
	{
		SerializeCookedPlatformData(Ar);
	}

#if WITH_EDITOR
	if (Ar.IsLoading() && !Ar.IsTransacting() && !bCooked)
	{
		BeginCachePlatformData();
	}
#endif // #if WITH_EDITOR
}

void UVolumeTexture::PostLoad()
{
#if WITH_EDITOR
	FinishCachePlatformData();

	if (Source2DTexture && SourceLightingGuid != Source2DTexture->GetLightingGuid())
	{
		UpdateSourceFromSourceTexture();
	}
#endif // #if WITH_EDITOR

	Super::PostLoad();
}

void UVolumeTexture::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITOR
	int32 SizeX = Source.GetSizeX();
	int32 SizeY = Source.GetSizeY();
	int32 SizeZ = Source.GetNumSlices();
#else
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;
#endif

	const FString Dimensions = FString::Printf(TEXT("%dx%dx%d"), SizeX, SizeY, SizeZ);
	OutTags.Add( FAssetRegistryTag("Dimensions", Dimensions, FAssetRegistryTag::TT_Dimensional) );
	OutTags.Add( FAssetRegistryTag("Format", GPixelFormats[GetPixelFormat()].Name, FAssetRegistryTag::TT_Alphabetical) );

	Super::GetAssetRegistryTags(OutTags);
}

void UVolumeTexture::UpdateResource()
{
#if WITH_EDITOR
	// Recache platform data if the source has changed.
	CachePlatformData();
#endif // #if WITH_EDITOR

	// Route to super.
	Super::UpdateResource();
}

FString UVolumeTexture::GetDesc()
{
	return FString::Printf(TEXT("Volume: %dx%dx%d [%s]"),
		GetSizeX(),
		GetSizeY(),
		GetSizeZ(),
		GPixelFormats[GetPixelFormat()].Name
		);
}

uint32 UVolumeTexture::CalcTextureMemorySize(int32 MipCount) const
{
	uint32 Size = 0;
	if (PlatformData)
	{
		const EPixelFormat Format = GetPixelFormat();
		if (Format != PF_Unknown)
		{
			const uint32 Flags = (SRGB ? TexCreate_SRGB : 0)  | TexCreate_OfflineProcessed | (bNoTiling ? TexCreate_NoTiling : 0);

			uint32 SizeX = 0;
			uint32 SizeY = 0;
			uint32 SizeZ = 0;
			CalcMipMapExtent3D(GetSizeX(), GetSizeY(), GetSizeZ(), Format, FMath::Max<int32>(0, GetNumMips() - MipCount), SizeX, SizeY, SizeZ);

			uint32 TextureAlign = 0;
			Size = (uint32)RHICalcTexture3DPlatformSize(SizeX, SizeY, SizeZ, Format, FMath::Max(1, MipCount), Flags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
		}
	}
	return Size;
}

uint32 UVolumeTexture::CalcTextureMemorySizeEnum( ETextureMipCount Enum ) const
{
	if ( Enum == TMC_ResidentMips || Enum == TMC_AllMipsBiased )
	{
		return CalcTextureMemorySize(GetNumMips() - GetCachedLODBias());
	}
	else
	{
		return CalcTextureMemorySize(GetNumMips());
	}
}

class FVolumeTextureBulkData : public FResourceBulkDataInterface
{
public:

	FVolumeTextureBulkData(int32 InFirstMip)
	: FirstMip(InFirstMip)
	{
		FMemory::Memzero(MipData, sizeof(MipData));
		FMemory::Memzero(MipSize, sizeof(MipSize));
	}

	~FVolumeTextureBulkData()
	{ 
		Discard();
	}

	const void* GetResourceBulkData() const override
	{
		return MipData[FirstMip];
	}

	uint32 GetResourceBulkDataSize() const override
	{
		return MipSize[FirstMip];
	}

	void Discard() override
	{
		for (int32 MipIndex = 0; MipIndex < MAX_TEXTURE_MIP_COUNT; ++MipIndex)
		{
			if (MipData[MipIndex])
			{
				FMemory::Free(MipData[MipIndex]);
				MipData[MipIndex] = nullptr;
			}
			MipSize[MipIndex] = 0;
		}
	}

	void MergeMips(int32 NumMips)
	{
		check(NumMips < MAX_TEXTURE_MIP_COUNT);

		uint64 MergedSize = 0;
		for (int32 MipIndex = FirstMip; MipIndex < NumMips; ++MipIndex)
		{
			MergedSize += MipSize[MipIndex];
		}

		// Don't do anything if there is nothing to merge
		if (MergedSize > MipSize[FirstMip])
		{
			uint8* MergedAlloc = (uint8*)FMemory::Malloc(MergedSize);
			uint8* CurrPos = MergedAlloc;
			for (int32 MipIndex = FirstMip; MipIndex < NumMips; ++MipIndex)
			{
				if (MipData[MipIndex])
				{
					FMemory::Memcpy(CurrPos, MipData[MipIndex], MipSize[MipIndex]);
				}
				CurrPos += MipSize[MipIndex];
			}

			Discard();

			MipData[FirstMip] = MergedAlloc;
			MipSize[FirstMip] = MergedSize;
		}
	}

	void** GetMipData() { return MipData; }
	uint32* GetMipSize() { return MipSize; }
	int32 GetFirstMip() const { return FirstMip; }

protected:

	void* MipData[MAX_TEXTURE_MIP_COUNT];
	uint32 MipSize[MAX_TEXTURE_MIP_COUNT];
	int32 FirstMip;
};

class FTexture3DResource : public FTextureResource
{
public:
	/**
	 * Minimal initialization constructor.
	 * @param InOwner - The UVolumeTexture which this FTexture3DResource represents.
	 */
	FTexture3DResource(UVolumeTexture* InOwner, int32 MipBias)
	:	Owner( InOwner )
	,	SizeX(InOwner->GetSizeX())
	,	SizeY(InOwner->GetSizeY())
	,	SizeZ(InOwner->GetSizeZ())
	,	CurrentFirstMip(INDEX_NONE)
	,	NumMips(InOwner->GetNumMips())
	,	PixelFormat(InOwner->GetPixelFormat())
	,	TextureSize(0)
	,	TextureReference(&InOwner->TextureReference)
	,	InitialData(MipBias)
	{
		check(0 < NumMips && NumMips <= MAX_TEXTURE_MIP_COUNT);
		check(0 <= MipBias && MipBias < NumMips);

		STAT(LODGroupStatName = TextureGroupStatFNames[Owner->LODGroup]);
		TextureName = Owner->GetFName();

		CreationFlags = (Owner->SRGB ? TexCreate_SRGB : 0)  | TexCreate_OfflineProcessed | TexCreate_ShaderResource | (Owner->bNoTiling ? TexCreate_NoTiling : 0);
		SamplerFilter = (ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(Owner);

		bGreyScaleFormat = (PixelFormat == PF_G8) || (PixelFormat == PF_BC4);

		FTexturePlatformData* PlatformData = Owner->PlatformData;
		if (PlatformData && PlatformData->TryLoadMips(MipBias, InitialData.GetMipData() + MipBias, Owner))
		{
			for (int32 MipIndex = MipBias; MipIndex < NumMips; ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = PlatformData->Mips[MipIndex];
				
				// The bulk data can be bigger because of memory alignment constraints on each slice and mips.
				InitialData.GetMipSize()[MipIndex] = FMath::Max<int32>(
					MipMap.BulkData.GetBulkDataSize(), 
					CalcTextureMipMapSize3D(SizeX, SizeY, SizeZ, (EPixelFormat)PixelFormat, MipIndex)
					);
			}
		}
	}

	/**
	 * Destructor, freeing MipData in the case of resource being destroyed without ever 
	 * having been initialized by the rendering thread via InitRHI.
	 */	
	~FTexture3DResource() {}
	
	/**
	 * Called when the resource is initialized. This is only called by the rendering thread.
	 */
	virtual void InitRHI() override
	{
		INC_DWORD_STAT_BY(STAT_TextureMemory, TextureSize);
		INC_DWORD_STAT_FNAME_BY(LODGroupStatName, TextureSize);

		CurrentFirstMip = InitialData.GetFirstMip();

		// Create the RHI texture.
		{
			FRHIResourceCreateInfo CreateInfo;
			if (GUseTexture3DBulkDataRHI)
			{
				InitialData.MergeMips(NumMips);
				CreateInfo.BulkData = &InitialData;
			}

			const uint32 BaseMipSizeX = FMath::Max<uint32>(SizeX >> CurrentFirstMip, 1); // BlockSizeX?
			const uint32 BaseMipSizeY = FMath::Max<uint32>(SizeY >> CurrentFirstMip, 1);
			const uint32 BaseMipSizeZ = FMath::Max<uint32>(SizeZ >> CurrentFirstMip, 1);

			CreateInfo.ExtData = Owner->PlatformData ? Owner->PlatformData->GetExtData() : 0;
			Texture3DRHI = RHICreateTexture3D(BaseMipSizeX, BaseMipSizeY, BaseMipSizeZ, PixelFormat, NumMips - CurrentFirstMip, CreationFlags, CreateInfo);
			TextureRHI = Texture3DRHI; 
		}

		TextureRHI->SetName(TextureName);
		RHIBindDebugLabelName(TextureRHI, *TextureName.ToString());

		if (TextureReference)
		{
			RHIUpdateTextureReference(TextureReference->TextureReferenceRHI, TextureRHI);
		}

		if (!GUseTexture3DBulkDataRHI) 
		{
			const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
			const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
			const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
			ensure(GPixelFormats[PixelFormat].BlockSizeZ == 1);

			for (int32 MipIndex = CurrentFirstMip; MipIndex < NumMips; ++MipIndex)
			{
				const uint8* MipData = (const uint8*)InitialData.GetMipData()[MipIndex];
				if (MipData)
				{
					// Could also access the mips size directly.
					const uint32 MipSizeX = FMath::Max<uint32>(SizeX >> MipIndex, 1);
					const uint32 MipSizeY = FMath::Max<uint32>(SizeY >> MipIndex, 1);
					const uint32 MipSizeZ = FMath::Max<uint32>(SizeZ >> MipIndex, 1);

					const uint32 NumBlockX = (uint32)FMath::DivideAndRoundUp<int32>(MipSizeX, BlockSizeX);
					const uint32 NumBlockY = (uint32)FMath::DivideAndRoundUp<int32>(MipSizeY, BlockSizeY);

					// FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, NumBlockX * BlockSizeX, NumBlockY * BlockSizeY, MipSizeZ);
					FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, MipSizeX, MipSizeY, MipSizeZ);

					// RHIUpdateTexture3D crashes on some platforms at engine initialization time.
					// The default volume texture end up being loaded at that point, which is a problem.
					// We check if this is really the rendering thread to find out if the engine is initializing.
					RHIUpdateTexture3D(Texture3DRHI, MipIndex - CurrentFirstMip, UpdateRegion, NumBlockX * BlockBytes, NumBlockX * NumBlockY * BlockBytes, MipData);
				}
			}
			InitialData.Discard();
		}

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer
		(
			SamplerFilter,
			AM_Wrap,
			AM_Wrap,
			AM_Wrap
		);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}

	virtual void ReleaseRHI() override
	{
		DEC_DWORD_STAT_BY( STAT_TextureMemory, TextureSize );
		DEC_DWORD_STAT_FNAME_BY( LODGroupStatName, TextureSize );
		if (TextureReference)
		{
			RHIUpdateTextureReference(TextureReference->TextureReferenceRHI, nullptr);
		}
		Texture3DRHI.SafeRelease();
		FTextureResource::ReleaseRHI();
	}

	/** Returns the width of the texture in pixels. */
	uint32 GetSizeX() const override
	{
		return FMath::Max<uint32>(SizeX >> CurrentFirstMip, 1);
	}
	/** Returns the height of the texture in pixels. */
	uint32 GetSizeY() const override
	{
		return FMath::Max<uint32>(SizeY >> CurrentFirstMip, 1);
	}
	/** Returns the depth of the texture in pixels. */
	uint32 GetSizeZ() const override
	{
		return FMath::Max<uint32>(SizeZ >> CurrentFirstMip, 1);
	}

private:

	/** The UVolumeTexture which this resource represents */
	UVolumeTexture*	Owner;

#if STATS
	/** The FName of the LODGroup-specific stat */
	FName LODGroupStatName;
#endif
	/** The FName of the texture asset */
	FName TextureName;

	/** Dimension X of the resource	*/
	uint32 SizeX;
	/** Dimension Y of the resource	*/
	uint32 SizeY;
	/** Dimension Z of the resource	*/
	uint32 SizeZ;
	/** The first mip cached in the resource. */
	int32 CurrentFirstMip;
	/** Num of mips of the texture */
	int32 NumMips;
	/** Format of the texture */
	uint8 PixelFormat;
	/** Creation flags of the texture */
	uint32 CreationFlags;
	/** Cached texture size for stats. */
	int32 TextureSize;

	/** The filtering to use for this texture */
	ESamplerFilter SamplerFilter;

	/** A reference to the texture's RHI resource as a texture 3D. */
	FTexture3DRHIRef Texture3DRHI;

	/** */
	FTextureReference* TextureReference;

	FVolumeTextureBulkData InitialData;
};

FTextureResource* UVolumeTexture::CreateResource()
{
	const FPixelFormatInfo& FormatInfo = GPixelFormats[GetPixelFormat()];
	const bool bCompressedFormat = FormatInfo.BlockSizeX > 1; 
	const bool bFormatIsSupported = FormatInfo.Supported && (!bCompressedFormat || ShaderPlatformSupportsCompression(GMaxRHIShaderPlatform));

	if (GetNumMips() > 0 && GSupportsTexture3D && bFormatIsSupported)
	{
		return new FTexture3DResource(this, GetCachedLODBias());
	}
	else if (GetNumMips() == 0)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s contains no miplevels! Please delete."), *GetFullName());
	}
	else if (!GSupportsTexture3D)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s cannot be created, rhi does not support 3d textures."), *GetFullName());
	}
	else if (!bFormatIsSupported)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s cannot be created, rhi does not support format %s."), *GetFullName(), FormatInfo.Name);
	}
	return nullptr;
}

void UVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	CumulativeResourceSize.AddUnknownMemoryBytes(CalcTextureMemorySizeEnum(TMC_ResidentMips));
}

#if WITH_EDITOR

void UVolumeTexture::SetDefaultSource2DTileSize()
{
	Source2DTileSizeX = 0;
	Source2DTileSizeY = 0;

	if (Source2DTexture)
	{
		const int32 SourceSizeX = Source2DTexture->Source.GetSizeX();
		const int32 SourceSizeY = Source2DTexture->Source.GetSizeY();
		if (SourceSizeX > 0 && SourceSizeY > 0)
		{
			const int32 NumPixels = SourceSizeX * SourceSizeY;
			const int32 TileSize = FMath::RoundToInt(FMath::Pow((float)NumPixels, 1.f / 3.f));
			const int32 NumTilesBySide = FMath::RoundToInt(FMath::Sqrt((float)(SourceSizeX / TileSize) * (SourceSizeY / TileSize)));
			Source2DTileSizeX = SourceSizeX / NumTilesBySide;
			Source2DTileSizeY = SourceSizeY / NumTilesBySide;
		}
	}
}

void UVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
 	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyChangedEvent.Property)
	{
		static const FName SourceTextureName("Source2DTexture");
		static const FName TileSizeXName("Source2DTileSizeX");
		static const FName TileSizeYName("Source2DTileSizeY");

		const FName PropertyName = PropertyChangedEvent.Property->GetFName();

		// Set default tile size if none is currently specified.
		if (PropertyName == SourceTextureName && !Source2DTileSizeX && !Source2DTileSizeY)
		{
			SetDefaultSource2DTileSize();
		}
		// Update the content of the volume texture
		if (PropertyName == SourceTextureName || PropertyName == TileSizeXName || PropertyName == TileSizeYName)
		{
			UpdateSourceFromSourceTexture();
		}
	}
	
	UpdateMipGenSettings();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


uint32 UVolumeTexture::GetMaximumDimension() const
{
	return GetMax2DTextureDimension();
}

void UVolumeTexture::UpdateMipGenSettings()
{
	if (PowerOfTwoMode == ETexturePowerOfTwoSetting::None && (!Source.IsPowerOfTwo() || !FMath::IsPowerOfTwo(Source.NumSlices)))
	{
		// Force NPT textures to have no mipmaps.
		MipGenSettings = TMGS_NoMipmaps;
		NeverStream = true;
	}
}

#endif // #if WITH_EDITOR

bool UVolumeTexture::ShaderPlatformSupportsCompression(FStaticShaderPlatform ShaderPlatform)
{
	switch (ShaderPlatform)
	{
	case SP_PCD3D_SM5:
	case SP_PS4:
	case SP_XBOXONE_D3D12:
	case SP_VULKAN_SM5:
	case SP_VULKAN_SM5_LUMIN:
		return true;

	default:
		return FDataDrivenShaderPlatformInfo::GetSupportsVolumeTextureCompression(ShaderPlatform);
	}
}

