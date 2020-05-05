// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Texture.h: Implementation of D3D12 Texture
=============================================================================*/
#pragma once

void SafeCreateTexture2D(FD3D12Device* pDevice, 
	FD3D12Adapter* Adapter,
	const D3D12_RESOURCE_DESC& TextureDesc,
	const D3D12_CLEAR_VALUE* ClearValue, 
	FD3D12ResourceLocation* OutTexture2D, 
	uint8 Format, 
	uint32 Flags,
	D3D12_RESOURCE_STATES InitialState,
	const TCHAR* Name);


/** Texture base class. */
class FD3D12TextureBase : public FD3D12BaseShaderResource, public FD3D12TransientResource, public FD3D12LinkedAdapterObject<FD3D12TextureBase>
{
public:

	FD3D12TextureBase(class FD3D12Device* InParent)
		: FD3D12BaseShaderResource(InParent)
		, MemorySize(0)
		, BaseShaderResource(this)
		, bCreatedRTVsPerSlice(false)
		, NumDepthStencilViews(0)
	{
	}

	virtual ~FD3D12TextureBase() {}

	inline void SetCreatedRTVsPerSlice(bool Value, int32 InRTVArraySize)
	{ 
		bCreatedRTVsPerSlice = Value;
		RTVArraySize = InRTVArraySize;
	}

	void SetNumRenderTargetViews(int32 InNumViews)
	{
		RenderTargetViews.Empty(InNumViews);
		RenderTargetViews.AddDefaulted(InNumViews);
	}

	void SetDepthStencilView(FD3D12DepthStencilView* View, uint32 SubResourceIndex)
	{
		if (SubResourceIndex < FExclusiveDepthStencil::MaxIndex)
		{
			DepthStencilViews[SubResourceIndex] = View;
			NumDepthStencilViews = FMath::Max(SubResourceIndex + 1, NumDepthStencilViews);
		}
		else
		{
			check(false);
		}
	}

	void SetRenderTargetViewIndex(FD3D12RenderTargetView* View, uint32 SubResourceIndex)
	{
		if (SubResourceIndex < (uint32)RenderTargetViews.Num())
		{
			RenderTargetViews[SubResourceIndex] = View;
		}
		else
		{
			check(false);
		}
	}

	void SetRenderTargetView(FD3D12RenderTargetView* View)
	{
		RenderTargetViews.Empty(1);
		RenderTargetViews.Add(View);
	}

	int32 GetMemorySize() const
	{
		return MemorySize;
	}

	void SetMemorySize(int32 InMemorySize)
	{
		MemorySize = InMemorySize;
	}

	void SetAliasingSource(FTextureRHIRef& SourceTextureRHI)
	{
		AliasingSourceTexture = SourceTextureRHI;
	}

	// Accessors.
	FD3D12Resource* GetResource() const { return ResourceLocation.GetResource(); }
	uint64 GetOffset() const { return ResourceLocation.GetOffsetFromBaseOfResource(); }
	FD3D12ShaderResourceView* GetShaderResourceView() const { return ShaderResourceView; }
	FD3D12BaseShaderResource* GetBaseShaderResource() const { return BaseShaderResource; }
	inline const FTextureRHIRef& GetAliasingSourceTexture() const { return AliasingSourceTexture; }

	void SetShaderResourceView(FD3D12ShaderResourceView* InShaderResourceView) { ShaderResourceView = InShaderResourceView; }

	static inline bool ShouldDeferCmdListOperation(FRHICommandList* RHICmdList)
	{
		if (RHICmdList == nullptr)
		{
			return false;
		}

		if (RHICmdList->Bypass() || !IsRunningRHIInSeparateThread())
		{
			return false;
		}

		return true;
	}

	void UpdateTexture(const D3D12_TEXTURE_COPY_LOCATION& DestCopyLocation, uint32 DestX, uint32 DestY, uint32 DestZ, const D3D12_TEXTURE_COPY_LOCATION& SourceCopyLocation);
	void CopyTextureRegion(uint32 DestX, uint32 DestY, uint32 DestZ, FD3D12TextureBase* SourceTexture, const D3D12_BOX& SourceBox);
	void InitializeTextureData(class FRHICommandListImmediate* RHICmdList, const void* InitData, uint32 InitDataSize, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumSlices, uint32 NumMips, EPixelFormat Format, D3D12_RESOURCE_STATES DestinationState);

	/**
	* Get the render target view for the specified mip and array slice.
	* An array slice of -1 is used to indicate that no array slice should be required.
	*/
	FD3D12RenderTargetView* GetRenderTargetView(int32 MipIndex, int32 ArraySliceIndex) const
	{
		int32 ArrayIndex = MipIndex;

		if (bCreatedRTVsPerSlice)
		{
			check(ArraySliceIndex >= 0);
			ArrayIndex = MipIndex * RTVArraySize + ArraySliceIndex;
			check(ArrayIndex < RenderTargetViews.Num());
		}
		else
		{
			// Catch attempts to use a specific slice without having created the texture to support it
			check(ArraySliceIndex == -1 || ArraySliceIndex == 0);
		}

		if (ArrayIndex < RenderTargetViews.Num())
		{
			return RenderTargetViews[ArrayIndex];
		}
		return 0;
	}
	FD3D12DepthStencilView* GetDepthStencilView(FExclusiveDepthStencil AccessType) const
	{
		return DepthStencilViews[AccessType.GetIndex()];
	}

	// New Monolithic Graphics drivers have optional "fast calls" replacing various D3d functions
	// You can't use fast version of XXSetShaderResources (called XXSetFastShaderResource) on dynamic or d/s targets
	inline bool HasDepthStencilView() const
	{
		return (NumDepthStencilViews > 0);
	}

	inline bool HasRenderTargetViews() const
	{
		return (RenderTargetViews.Num() > 0);
	}

	void AliasResources(FD3D12TextureBase* Texture)
	{
		// Alias the location, will perform an addref underneath
		FD3D12ResourceLocation::Alias(ResourceLocation, Texture->ResourceLocation);

		// Do not copy the BaseShaderResource from the source texture (this is initialized correctly here, and is used for
		// state caching logic).

		ShaderResourceView = Texture->ShaderResourceView;

		for (uint32 Index = 0; Index < FExclusiveDepthStencil::MaxIndex; Index++)
		{
			DepthStencilViews[Index] = Texture->DepthStencilViews[Index];
		}
		for (int32 Index = 0; Index < Texture->RenderTargetViews.Num(); Index++)
		{
			RenderTargetViews[Index] = Texture->RenderTargetViews[Index];
		}
	}

	// Modifiers.
	void SetReadBackListHandle(FD3D12CommandListHandle listToWaitFor) { ReadBackSyncPoint = listToWaitFor; }
	FD3D12CLSyncPoint GetReadBackSyncPoint() const { return ReadBackSyncPoint; }

	FD3D12CLSyncPoint ReadBackSyncPoint;

protected:

	/** Amount of memory allocated by this texture, in bytes. */
	int32 MemorySize;

	/** Pointer to the base shader resource. Usually the object itself, but not for texture references. */
	FD3D12BaseShaderResource* BaseShaderResource;

	/** A shader resource view of the texture. */
	TRefCountPtr<FD3D12ShaderResourceView> ShaderResourceView;

	/** A render targetable view of the texture. */
	TArray<TRefCountPtr<FD3D12RenderTargetView>, TInlineAllocator<1>> RenderTargetViews;

	bool bCreatedRTVsPerSlice;

	int32 RTVArraySize;

	/** A depth-stencil targetable view of the texture. */
	TRefCountPtr<FD3D12DepthStencilView> DepthStencilViews[FExclusiveDepthStencil::MaxIndex];

	/** Number of Depth Stencil Views - used for fast call tracking. */
	uint32	NumDepthStencilViews;

	TMap<uint32, FD3D12LockedResource*> LockedMap;

	FTextureRHIRef AliasingSourceTexture;
};

#if PLATFORM_WINDOWS
struct FD3D12TextureLayout {};
#endif

/** 2D texture (vanilla, cubemap or 2D array) */
template<typename BaseResourceType>
class TD3D12Texture2D : public BaseResourceType, public FD3D12TextureBase
{
public:

	/** Flags used when the texture was created */
	uint32 Flags;

	/** Initialization constructor. */
	TD3D12Texture2D(
	class FD3D12Device* InParent,
		uint32 InSizeX,
		uint32 InSizeY,
		uint32 InSizeZ,
		uint32 InNumMips,
		uint32 InNumSamples,
		EPixelFormat InFormat,
		bool bInCubemap,
		uint32 InFlags,
		const FClearValueBinding& InClearValue,
		const FD3D12TextureLayout* InTextureLayout = nullptr
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
		, void* InRawTextureMemory = nullptr
#endif
		)
		: BaseResourceType(
			InSizeX,
			InSizeY,
			InSizeZ,
			InNumMips,
			InNumSamples,
			InFormat,
			InFlags,
			InClearValue
			)
		, FD3D12TextureBase(InParent)
		, Flags(InFlags)
		, bCubemap(bInCubemap)
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
		, RawTextureMemory(InRawTextureMemory)
#endif
	{
		if (InTextureLayout == nullptr)
		{
			FMemory::Memzero(TextureLayout);
		}
		else
		{
			TextureLayout = *InTextureLayout;
		}
	}

	virtual ~TD3D12Texture2D();

	/**
	* Locks one of the texture's mip-maps.
	* @return A pointer to the specified texture data.
	*/
	void* Lock(class FRHICommandListImmediate* RHICmdList, uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride);

	/** Unlocks a previously locked mip-map. */
	void Unlock(class FRHICommandListImmediate* RHICmdList, uint32 MipIndex, uint32 ArrayIndex);

	//* Update the contents of the Texture2D using a Copy command */
	void UpdateTexture2D(class FRHICommandListImmediate* RHICmdList, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData);

	// Accessors.
	FD3D12Resource* GetResource() const { return (FD3D12Resource*)FD3D12TextureBase::GetResource(); }

	void GetReadBackHeapDesc(D3D12_PLACED_SUBRESOURCE_FOOTPRINT& OutFootprint, uint32 Subresource) const;

	bool IsCubemap() const { return bCubemap; }

	/** FRHITexture override.  See FRHITexture::GetNativeResource() */
	virtual void* GetNativeResource() const override final
	{
		FD3D12Resource* Resource = GetResource();
		return (Resource == nullptr) ? nullptr : Resource->GetResource();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return static_cast<FD3D12TextureBase*>(this);
	}

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	void* GetRawTextureMemory() const
	{
		return RawTextureMemory;
	}

	void SetRawTextureMemory(void* Memory)
	{
		RawTextureMemory = Memory;
	}
	FPlatformMemory::FPlatformVirtualMemoryBlock& GetRawTextureBlock()
	{
		return RawTextureBlock;
	}
#endif

	const FD3D12TextureLayout& GetTextureLayout() const { return TextureLayout; }

private:
	/** Unlocks a previously locked mip-map. */
	void UnlockInternal(class FRHICommandListImmediate* RHICmdList, TD3D12Texture2D* Previous, uint32 MipIndex, uint32 ArrayIndex);

	/** Whether the texture is a cube-map. */
	const uint32 bCubemap : 1;

#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	void* RawTextureMemory;
	FPlatformMemory::FPlatformVirtualMemoryBlock RawTextureBlock;
#endif

	FD3D12TextureLayout TextureLayout;
};

/** 3D Texture */
class FD3D12Texture3D : public FRHITexture3D, public FD3D12TextureBase
{
public:

	/** Initialization constructor. */
	FD3D12Texture3D(
	class FD3D12Device* InParent,
		uint32 InSizeX,
		uint32 InSizeY,
		uint32 InSizeZ,
		uint32 InNumMips,
		EPixelFormat InFormat,
		uint32 InFlags,
		const FClearValueBinding& InClearValue
		)
		: FRHITexture3D(InSizeX, InSizeY, InSizeZ, InNumMips, InFormat, InFlags, InClearValue)
		, FD3D12TextureBase(InParent)
	{
	}

	virtual ~FD3D12Texture3D();

	/** FRHITexture override.  See FRHITexture::GetNativeResource() */
	virtual void* GetNativeResource() const override final
	{
		FD3D12Resource* Resource = GetResource();
		return (Resource == nullptr) ? nullptr : Resource->GetResource();
	}

	// Accessors.
	FD3D12Resource* GetResource() const { return (FD3D12Resource*)FD3D12TextureBase::GetResource(); }

	virtual void* GetTextureBaseRHI() override final
	{
		return static_cast<FD3D12TextureBase*>(this);
	}

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
};

class FD3D12BaseTexture2D : public FRHITexture2D, public FD3D12FastClearResource
{
public:
	FD3D12BaseTexture2D(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, EPixelFormat InFormat, uint32 InFlags, const FClearValueBinding& InClearValue)
		: FRHITexture2D(InSizeX, InSizeY, InNumMips, InNumSamples, InFormat, InFlags, InClearValue)
	{}
	uint32 GetSizeZ() const { return 0; }

	virtual void GetWriteMaskProperties(void*& OutData, uint32& OutSize) override final
	{
		FD3D12FastClearResource::GetWriteMaskProperties(OutData, OutSize);
	}
};

class FD3D12BaseTexture2DArray : public FRHITexture2DArray, public FD3D12FastClearResource
{
public:
	FD3D12BaseTexture2DArray(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, EPixelFormat InFormat, uint32 InFlags, const FClearValueBinding& InClearValue)
		: FRHITexture2DArray(InSizeX, InSizeY, InSizeZ, InNumMips, InNumSamples, InFormat, InFlags, InClearValue)
	{
		check(InNumSamples == 1);
	}
};

class FD3D12BaseTextureCube : public FRHITextureCube, public FD3D12FastClearResource
{
public:
	FD3D12BaseTextureCube(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, EPixelFormat InFormat, uint32 InFlags, const FClearValueBinding& InClearValue)
		: FRHITextureCube(InSizeX, InNumMips, InFormat, InFlags, InClearValue)
		, SliceCount(InSizeZ)
	{
		check(InNumSamples == 1);
	}
	uint32 GetSizeX() const { return GetSize(); }
	uint32 GetSizeY() const { return GetSize(); }
	uint32 GetSizeZ() const { return SliceCount; }

private:
	uint32 SliceCount;
};

typedef TD3D12Texture2D<FD3D12BaseTexture2D>      FD3D12Texture2D;
typedef TD3D12Texture2D<FD3D12BaseTexture2DArray> FD3D12Texture2DArray;
typedef TD3D12Texture2D<FD3D12BaseTextureCube>    FD3D12TextureCube;

/** Texture reference class. */
class FD3D12TextureReference : public FRHITextureReference, public FD3D12TextureBase
{
public:
	FD3D12TextureReference(class FD3D12Device* InParent, FLastRenderTimeContainer* LastRenderTime)
		: FRHITextureReference(LastRenderTime)
		, FD3D12TextureBase(InParent)
	{
		BaseShaderResource = NULL;
	}

	void SetReferencedTexture(FRHITexture* InTexture, FD3D12BaseShaderResource* InBaseShaderResource, FD3D12ShaderResourceView* InSRV)
	{
		ShaderResourceView = InSRV;
		BaseShaderResource = InBaseShaderResource;
		FRHITextureReference::SetReferencedTexture(InTexture);
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return static_cast<FD3D12TextureBase*>(this);
	}

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
};

/** Given a pointer to a RHI texture that was created by the D3D12 RHI, returns a pointer to the FD3D12TextureBase it encapsulates. */
FORCEINLINE FD3D12TextureBase* GetD3D12TextureFromRHITexture(FRHITexture* Texture)
{
	if (!Texture)
	{
		return NULL;
	}
	
	FD3D12TextureBase* Result((FD3D12TextureBase*)Texture->GetTextureBaseRHI());
	check(Result);
	return Result;
}

class FD3D12TextureStats
{
public:

	static bool ShouldCountAsTextureMemory(uint32 MiscFlags);
	// @param b3D true:3D, false:2D or cube map
	static TStatId GetD3D12StatEnum(uint32 MiscFlags, bool bCubeMap, bool b3D);

	// Note: This function can be called from many different threads
	// @param TextureSize >0 to allocate, <0 to deallocate
	// @param b3D true:3D, false:2D or cube map
	static void UpdateD3D12TextureStats(const D3D12_RESOURCE_DESC& Desc, int64 TextureSize, bool b3D, bool bCubeMap);

	template<typename BaseResourceType>
	static void D3D12TextureAllocated(TD3D12Texture2D<BaseResourceType>& Texture, const D3D12_RESOURCE_DESC *Desc = nullptr);

	template<typename BaseResourceType>
	static void D3D12TextureDeleted(TD3D12Texture2D<BaseResourceType>& Texture);

	static void D3D12TextureAllocated2D(FD3D12Texture2D& Texture);

	static void D3D12TextureAllocated(FD3D12Texture3D& Texture);

	static void D3D12TextureDeleted(FD3D12Texture3D& Texture);
};

template<>
struct TD3D12ResourceTraits<FRHITexture3D>
{
	typedef FD3D12Texture3D TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHITexture2D>
{
	typedef FD3D12Texture2D TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHITexture2DArray>
{
	typedef FD3D12Texture2DArray TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHITextureCube>
{
	typedef FD3D12TextureCube TConcreteType;
};