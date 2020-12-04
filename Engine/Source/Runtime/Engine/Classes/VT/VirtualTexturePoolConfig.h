// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RenderResource.h"
#include "RenderCommandFence.h"
#include "Engine/Texture.h"
#include "VirtualTexturing.h"
#include "VirtualTexturePoolConfig.generated.h"

/**
* Settings of a single pool
*/
USTRUCT()
struct FVirtualTextureSpacePoolConfig
{
	GENERATED_USTRUCT_BODY()

	FVirtualTextureSpacePoolConfig() : MinTileSize(0), MaxTileSize(0), SizeInMegabyte(0), bAllowSizeScale(false), ScalabilityGroup(0) {}

	/** Minimum tile size to match (including tile border). */
	UPROPERTY()
	int32 MinTileSize;

	/** Maximum tile size to match (including tile border). */
	UPROPERTY()
	int32 MaxTileSize;

	/** Format set to match. One pool can contain multiple layers with synchronized page table mappings. */
	UPROPERTY()
	TArray< TEnumAsByte<EPixelFormat> > Formats;

	/** Upper limit of size in megabytes to allocate for the pool. The allocator will allocate as close as possible to this limit. */
	UPROPERTY()
	int32 SizeInMegabyte;

	/** Allow the size to allocate for the pool to be scaled by some factor. */
	UPROPERTY()
	bool bAllowSizeScale;

	/** Scalability group index that gives the size scale. */
	UPROPERTY()
	uint32 ScalabilityGroup;

	/** Is this the default config? Use this setting when we can't find any other match. */
	bool IsDefault() const { return Formats.Num() == 0; }
};

UCLASS(config = Engine, transient)
class ENGINE_API UVirtualTexturePoolConfig : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(config)
	int32 DefaultSizeInMegabyte; // Size in tiles of any pools not explicitly specified in the config

	UPROPERTY(config)
	TArray<FVirtualTextureSpacePoolConfig> Pools; // All the VT pools specified in the config

	void FindPoolConfig(TEnumAsByte<EPixelFormat> const* InFormats, int32 InNumLayers, int32 InTileSize, FVirtualTextureSpacePoolConfig& OutConfig) const;
};
