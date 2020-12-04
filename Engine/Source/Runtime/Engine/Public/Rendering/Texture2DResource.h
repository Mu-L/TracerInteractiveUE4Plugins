// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/*=============================================================================
	Texture2DResource.h: Implementation of FTexture2DResource used by streamable UTexture2D.
=============================================================================*/

#include "CoreMinimal.h"
#include "Rendering/StreamableTextureResource.h"

/**
 * FTextureResource implementation for streamable 2D textures.
 */
class FTexture2DResource : public FStreamableTextureResource
{
public:
	/**
	 * Minimal initialization constructor.
	 *
	 * @param InOwner			UTexture2D which this FTexture2DResource represents.
	 * @param InPostInitState	The renderthread coherent state the resource will have once InitRHI() will be called.		
 	 */
	FTexture2DResource(UTexture2D* InOwner, const FStreamableRenderResourceState& InPostInitState);

	/**
	 * Destructor, freeing MipData in the case of resource being destroyed without ever 
	 * having been initialized by the rendering thread via InitRHI.
	 */
	virtual ~FTexture2DResource();

	// Dynamic cast methods.
	ENGINE_API virtual FTexture2DResource* GetTexture2DResource() { return this; }
	// Dynamic cast methods (const).
	ENGINE_API virtual const FTexture2DResource* GetTexture2DResource() const { return this; }

	/** Set the value of Filter, AddressU, AddressV, AddressW and MipBias from FStreamableTextureResource on the gamethread. */
	void CacheSamplerStateInitializer(const UTexture2D* InOwner);

private:

	virtual void CreateTexture() final override;
	virtual void CreatePartiallyResidentTexture() final override;
#if STATS
	virtual void CalcRequestedMipsSize() final override;
#endif

	/** Texture streaming command classes that need to be friends in order to call Update/FinalizeMipCount.	*/
	friend class UTexture2D;
	friend class FTexture2DUpdate;

	/** Resource memory allocated by the owner for serialize bulk mip data into								*/
	FTexture2DResourceMem* ResourceMem;

	/** Local copy/ cache of mip data between creation and first call to InitRHI.							*/
	TArray<void*, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > MipData;

	/**
	 * Writes the data for a single mip-level into a destination buffer.
	 * @param MipIndex	The index of the mip-level to read.
	 * @param Dest		The address of the destination buffer to receive the mip-level's data.
	 * @param DestPitch	Number of bytes per row
	 */
	void GetData( uint32 MipIndex,void* Dest,uint32 DestPitch );
};
