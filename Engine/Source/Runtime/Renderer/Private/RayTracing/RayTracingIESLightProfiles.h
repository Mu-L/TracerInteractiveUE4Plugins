// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Engine/TextureLightProfile.h"
#include "RendererInterface.h"

#if RHI_RAYTRACING

class FIESLightProfileResource
{
public:
	void BuildIESLightProfilesTexture(const TArray<UTextureLightProfile*, SceneRenderingAllocator>& NewIESProfilesArray);

	uint32 GetIESLightProfilesCount() const
	{
		return IESTextureData.Num();
	}

	void Release()
	{
		check(IsInRenderingThread());

		TextureRHI.SafeRelease();
		IESProfilesBulkData.Empty();
		IESTextureData.Empty();
	}

	FTexture2DRHIRef GetTexture()
	{
		return TextureRHI;
	}

private:
	FTexture2DRHIRef					TextureRHI;
	TArray<FFloat16>					IESProfilesBulkData;
	TArray<const UTextureLightProfile*>	IESTextureData;

	bool IsIESTextureFormatValid(const UTextureLightProfile* Texture) const
	{
		if (Texture
			&& Texture->PlatformData 
			&& Texture->PlatformData->PixelFormat == PF_FloatRGBA
			&& Texture->PlatformData->Mips.Num() == 1
			&& Texture->PlatformData->Mips[0].SizeX == AllowedIESProfileWidth() 
			//#dxr_todo: UE-70840 anisotropy in IES files is ignored so far (to support that, we should not store one IES profile per row but use more than one row per profile in that case)
			&& Texture->PlatformData->Mips[0].SizeY == 1
			)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	static uint32 AllowedIESProfileWidth()
	{
		return 256;
	}
};

#endif
