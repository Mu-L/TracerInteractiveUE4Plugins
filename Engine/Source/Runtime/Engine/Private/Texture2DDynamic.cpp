// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture2DDynamic.cpp: Implementation of UTexture2DDynamic.
=============================================================================*/

#include "Engine/Texture2DDynamic.h"
#include "UObject/Package.h"
#include "TextureResource.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"

/*-----------------------------------------------------------------------------
	FTexture2DDynamicResource
-----------------------------------------------------------------------------*/

/** Initialization constructor. */
FTexture2DDynamicResource::FTexture2DDynamicResource(UTexture2DDynamic* InOwner)
:	Owner(InOwner)
{
}

/** Returns the width of the texture in pixels. */
uint32 FTexture2DDynamicResource::GetSizeX() const
{
	return Owner->SizeX;
}

/** Returns the height of the texture in pixels. */
uint32 FTexture2DDynamicResource::GetSizeY() const
{
	return Owner->SizeY;
}

/** Called when the resource is initialized. This is only called by the rendering thread. */
void FTexture2DDynamicResource::InitRHI()
{
	// Create the sampler state RHI resource.
	ESamplerAddressMode SamplerAddressMode = Owner->SamplerAddressMode;
	FSamplerStateInitializerRHI SamplerStateInitializer
	(
		(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter( Owner ),
		SamplerAddressMode,
		SamplerAddressMode,
		SamplerAddressMode
	);
	SamplerStateRHI = GetOrCreateSamplerState( SamplerStateInitializer );

	ETextureCreateFlags  Flags = TexCreate_None;
	if ( Owner->bIsResolveTarget )
	{
		Flags |= TexCreate_ResolveTargetable;
		bIgnoreGammaConversions = true;		// Note, we're ignoring Owner->SRGB (it should be false).
	}
	else if ( Owner->SRGB )
	{
		Flags |= TexCreate_SRGB;
	}
	if ( Owner->bNoTiling )
	{
		Flags |= TexCreate_NoTiling;
	}
	FRHIResourceCreateInfo CreateInfo;
	Texture2DRHI = RHICreateTexture2D(GetSizeX(), GetSizeY(), Owner->Format, Owner->NumMips, 1, Flags, CreateInfo);
	TextureRHI = Texture2DRHI;
	TextureRHI->SetName(Owner->GetFName());
	RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI,TextureRHI);
}

/** Called when the resource is released. This is only called by the rendering thread. */
void FTexture2DDynamicResource::ReleaseRHI()
{
	RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
	FTextureResource::ReleaseRHI();
	Texture2DRHI.SafeRelease();
}

/** Returns the Texture2DRHI, which can be used for locking/unlocking the mips. */
FTexture2DRHIRef FTexture2DDynamicResource::GetTexture2DRHI()
{
	return Texture2DRHI;
}


/*-----------------------------------------------------------------------------
	UTexture2DDynamic
-----------------------------------------------------------------------------*/
UTexture2DDynamic::UTexture2DDynamic(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NeverStream = true;
	Format = PF_B8G8R8A8;
	SamplerAddressMode = AM_Wrap;
}


void UTexture2DDynamic::Init( int32 InSizeX, int32 InSizeY, EPixelFormat InFormat/*=2*/, bool InIsResolveTarget/*=false*/ )
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	Format = (EPixelFormat) InFormat;
	NumMips = 1;
	bIsResolveTarget = InIsResolveTarget;

	// Initialize the resource.
	UpdateResource();
}

FTextureResource* UTexture2DDynamic::CreateResource()
{
	return new FTexture2DDynamicResource(this);
}

float UTexture2DDynamic::GetSurfaceWidth() const
{
	return SizeX;
}

float UTexture2DDynamic::GetSurfaceHeight() const
{
	return SizeY;
}

UTexture2DDynamic* UTexture2DDynamic::Create(int32 InSizeX, int32 InSizeY, EPixelFormat InFormat)
{
	FTexture2DDynamicCreateInfo CreateInfo(InFormat);

	return Create(InSizeX, InSizeY, CreateInfo);
}

UTexture2DDynamic* UTexture2DDynamic::Create(int32 InSizeX, int32 InSizeY, EPixelFormat InFormat, bool InIsResolveTarget)
{
	FTexture2DDynamicCreateInfo CreateInfo(InFormat, InIsResolveTarget);

	return Create(InSizeX, InSizeY, CreateInfo);
}

UTexture2DDynamic* UTexture2DDynamic::Create(int32 InSizeX, int32 InSizeY, const FTexture2DDynamicCreateInfo& InCreateInfo)
{
	EPixelFormat DesiredFormat = EPixelFormat(InCreateInfo.Format);
	if (InSizeX > 0 && InSizeY > 0 )
	{
		
		auto NewTexture = NewObject<UTexture2DDynamic>(GetTransientPackage(), NAME_None, RF_Transient);
		if (NewTexture != NULL)
		{
			NewTexture->Filter = InCreateInfo.Filter;
			NewTexture->SamplerAddressMode = InCreateInfo.SamplerAddressMode;
			NewTexture->SRGB = InCreateInfo.bSRGB;

			// Disable compression
			NewTexture->CompressionSettings		= TC_Default;
#if WITH_EDITORONLY_DATA
			NewTexture->CompressionNone			= true;
			NewTexture->MipGenSettings			= TMGS_NoMipmaps;
			NewTexture->CompressionNoAlpha		= true;
			NewTexture->DeferCompression		= false;
#endif // #if WITH_EDITORONLY_DATA
			if ( InCreateInfo.bIsResolveTarget )
			{
				NewTexture->bNoTiling			= false;
			}
			else
			{
				// Untiled format
				NewTexture->bNoTiling			= true;
			}

			NewTexture->Init(InSizeX, InSizeY, DesiredFormat, InCreateInfo.bIsResolveTarget);
		}
		return NewTexture;
	}
	else
	{
		UE_LOG(LogTexture, Warning, TEXT("Invalid parameters specified for UTexture2DDynamic::Create()"));
		return NULL;
	}
}
