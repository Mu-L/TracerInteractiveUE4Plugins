// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalVertexBuffer.cpp: Metal vertex buffer RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "MetalCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "MetalLLM.h"
#include <objc/runtime.h>

#if STATS
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size) \
	switch(Type)	{ \
		case RRT_UniformBuffer: INC_DWORD_STAT_BY(STAT_MetalUniform##Name, Size); break; \
		case RRT_IndexBuffer: INC_DWORD_STAT_BY(STAT_MetalIndex##Name, Size); break; \
		case RRT_StructuredBuffer: \
		case RRT_VertexBuffer: INC_DWORD_STAT_BY(STAT_MetalVertex##Name, Size); break; \
		default: break; \
	}
#else
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size)
#endif

@implementation FMetalBufferData

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FMetalBufferData)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		self->Data = nullptr;
		self->Len = 0;
	}
	return Self;
}
-(instancetype)initWithSize:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
	}
	return Self;
}
-(instancetype)initWithBytes:(void const*)InData length:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
		FMemory::Memcpy(self->Data, InData, InSize);
	}
	return Self;
}
-(void)dealloc
{
	if (self->Data)
	{
		FMemory::Free(self->Data);
		self->Data = nullptr;
		self->Len = 0;
	}
	[super dealloc];
}
@end


FMetalVertexBuffer::FMetalVertexBuffer(uint32 InSize, uint32 InUsage)
	: FRHIVertexBuffer(InSize, InUsage)
	, FMetalRHIBuffer(InSize, InUsage|EMetalBufferUsage_LinearTex, RRT_VertexBuffer)
{
}

FMetalVertexBuffer::~FMetalVertexBuffer()
{
}

void FMetalVertexBuffer::Swap(FMetalVertexBuffer& Other)
{
	FRHIVertexBuffer::Swap(Other);
	FMetalRHIBuffer::Swap(Other);
}

void FMetalRHIBuffer::Swap(FMetalRHIBuffer& Other)
{
	::Swap(*this, Other);
}

static bool CanUsePrivateMemory()
{
	return (FMetalCommandQueue::SupportsFeature(EMetalFeaturesEfficientBufferBlits) || FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs));
}

bool FMetalRHIBuffer::UsePrivateMemory() const
{
	return (FMetalCommandQueue::SupportsFeature(EMetalFeaturesEfficientBufferBlits) && (Usage & (BUF_Dynamic|BUF_Static)))
	|| (FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs) && (Usage & (BUF_ShaderResource|BUF_UnorderedAccess)));
}

FMetalRHIBuffer::FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType)
: Data(nullptr)
, LastUpdate(0)
, LockOffset(0)
, LockSize(0)
, Size(InSize)
, Usage(InUsage)
, Mode(BUFFER_STORAGE_MODE)
, Type(InType)
{
	// No life-time usage information? Enforce Dynamic.
	if ((Usage & (BUF_Volatile|BUF_Dynamic|BUF_Static)) == 0)
	{
		Usage |= BUF_Dynamic;
	}
	
	Mode = UsePrivateMemory() ? mtlpp::StorageMode::Private : BUFFER_STORAGE_MODE;
	
	if (InSize)
	{
		checkf(InSize <= 1024 * 1024 * 1024, TEXT("Metal doesn't support buffers > 1GB"));
		
		// Temporary buffers less than the buffer page size - currently 4Kb - is better off going through the set*Bytes API if available.
		// These can't be used for shader resources or UAVs if we want to use the 'Linear Texture' code path
		if (!(InUsage & (BUF_UnorderedAccess|BUF_ShaderResource|EMetalBufferUsage_GPUOnly)) && (InUsage & BUF_Volatile) && InSize < MetalBufferPageSize && (InSize < MetalBufferBytesSize))
		{
			Data = [[FMetalBufferData alloc] initWithSize:InSize];
			METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
		}
		else
		{
			uint32 AllocSize = Size;
			
			if ((InUsage & EMetalBufferUsage_LinearTex) && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
			{
				if (InUsage & BUF_UnorderedAccess)
				{
					// Padding for write flushing when not using linear texture bindings for buffers
					AllocSize = Align(AllocSize + 512, 1024);
				}
				
				if (InUsage & (BUF_ShaderResource|BUF_UnorderedAccess))
				{
					uint32 NumElements = AllocSize;
					uint32 SizeX = NumElements;
					uint32 SizeY = 1;
					uint32 Dimension = GMaxTextureDimensions;
					while (SizeX > GMaxTextureDimensions)
					{
						while((NumElements % Dimension) != 0)
						{
							check(Dimension >= 1);
							Dimension = (Dimension >> 1);
						}
						SizeX = Dimension;
						SizeY = NumElements / Dimension;
						if(SizeY > GMaxTextureDimensions)
						{
							Dimension <<= 1;
							checkf(SizeX <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %u to a 2D texture."), Dimension, (int32)GMaxTextureDimensions, AllocSize);
							if(Dimension <= GMaxTextureDimensions)
							{
								AllocSize = Align(Size, Dimension);
								NumElements = AllocSize;
								SizeX = NumElements;
							}
							else
							{
								// We don't know the Pixel Format and so the bytes per element for the potential linear texture
								// Use max texture dimension as the align to be a worst case rather than crashing
								AllocSize = Align(Size, GMaxTextureDimensions);
								break;
							}
						}
					}
					
					AllocSize = Align(AllocSize, 1024);
				}
			}
			
			Alloc(AllocSize, RLM_WriteOnly);
		}
	}
}

FMetalRHIBuffer::~FMetalRHIBuffer()
{
	for (auto& Pair : LinearTextures)
	{
		SafeReleaseMetalTexture(Pair.Value);
		Pair.Value = nil;
	}
	LinearTextures.Empty();
	
	if (CPUBuffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, CPUBuffer.GetLength());
		SafeReleaseMetalBuffer(CPUBuffer);
	}
	if (Buffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Buffer.GetLength());
		SafeReleaseMetalBuffer(Buffer);
	}
	if (Data)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Size);
		SafeReleaseMetalObject(Data);
	}
}

void FMetalRHIBuffer::Alias()
{
	if (Mode == mtlpp::StorageMode::Private && Buffer.GetHeap() && !Buffer.IsAliasable())
	{
		Buffer.MakeAliasable();
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		MetalLLM::LogAliasBuffer(Buffer);
#endif
	}
}

void FMetalRHIBuffer::Unalias()
{
	if (Mode == mtlpp::StorageMode::Private && Buffer.GetHeap() && Buffer.IsAliasable())
	{
		uint32 Len = Buffer.GetLength();
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
		SafeReleaseMetalBuffer(Buffer);
		Buffer = nil;
		
		Alloc(Len, RLM_WriteOnly);
	}
}

void FMetalRHIBuffer::Alloc(uint32 InSize, EResourceLockMode LockMode, bool bIsUniformBuffer)
{
	if (!Buffer)
	{
        check(LockMode != RLM_ReadOnly);
        FMetalPooledBufferArgs Args(GetMetalDeviceContext().GetDevice(), InSize, Usage, Mode);
		Buffer = GetMetalDeviceContext().CreatePooledBuffer(Args);
		METAL_FATAL_ASSERT(Buffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)Mode);

        Buffer.SetOwner(this);

        METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
        
		if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
		{
			for (auto& Pair : LinearTextures)
			{
				SafeReleaseMetalTexture(Pair.Value);
				Pair.Value = nil;
				
				Pair.Value = AllocLinearTexture(Pair.Key.Key, Pair.Key.Value);
				check(Pair.Value);
			}
		}

		bIsUniformBufferBacking = bIsUniformBuffer;
	}
}

void FMetalRHIBuffer::AllocTransferBuffer(bool bOnRHIThread, uint32 InSize, EResourceLockMode LockMode)
{
	if (!CPUBuffer && ((LockMode == RLM_WriteOnly && CanUsePrivateMemory()) || Mode == mtlpp::StorageMode::Private))
	{
		FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), InSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
        CPUBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
		CPUBuffer.SetOwner(this);
        check(CPUBuffer && CPUBuffer.GetPtr());
        METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
        METAL_FATAL_ASSERT(CPUBuffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)mtlpp::StorageMode::Shared);
    }
}

FMetalTexture FMetalRHIBuffer::AllocLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor& LinearTextureDesc)
{
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
	{
		mtlpp::PixelFormat MTLFormat = (mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat;
		
		mtlpp::TextureDescriptor Desc;
		NSUInteger Options = ((NSUInteger)Mode << mtlpp::ResourceStorageModeShift) | ((NSUInteger)Buffer.GetCpuCacheMode() << mtlpp::ResourceCpuCacheModeShift);
		Options = FMetalCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions(Options | mtlpp::ResourceOptions::HazardTrackingModeUntracked));
		NSUInteger TexUsage = mtlpp::TextureUsage::Unknown;
		if (Usage & BUF_ShaderResource)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderRead;
		}
		if (Usage & BUF_UnorderedAccess)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderWrite;
		}
		
		uint32 BytesPerElement = (0 == LinearTextureDesc.BytesPerElement) ? GPixelFormats[InFormat].BlockBytes : LinearTextureDesc.BytesPerElement;
		if (MTLFormat == mtlpp::PixelFormat::RG11B10Float && MTLFormat != (mtlpp::PixelFormat)GPixelFormats[InFormat].PlatformFormat)
		{
			BytesPerElement = 4;
		}

		const uint32 MinimumByteAlignment = GetMetalDeviceContext().GetDevice().GetMinimumLinearTextureAlignmentForPixelFormat((mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat);
		const uint32 MinimumElementAlignment = MinimumByteAlignment / BytesPerElement;

		uint32 Offset = LinearTextureDesc.StartOffsetBytes;
		check(Offset % MinimumByteAlignment == 0);

		uint32 NumElements = (UINT_MAX == LinearTextureDesc.NumElements) ? ((Size - Offset) / BytesPerElement) : LinearTextureDesc.NumElements;
		NumElements = Align(NumElements, MinimumElementAlignment);

		uint32 RowBytes = NumElements * BytesPerElement;

		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
		{
			Desc = mtlpp::TextureDescriptor::TextureBufferDescriptor(MTLFormat, NumElements, mtlpp::ResourceOptions(Options), mtlpp::TextureUsage(TexUsage));
			Desc.SetAllowGPUOptimisedContents(false);
		}
		else
		{
			uint32 Width = NumElements;
			uint32 Height = 1;

			if (NumElements > GMaxTextureDimensions)
			{
				uint32 Dimension = GMaxTextureDimensions;
				while ((NumElements % Dimension) != 0)
				{
					check(Dimension >= 1);
					Dimension = (Dimension >> 1);
				}

				Width = Dimension;
				Height = NumElements / Dimension;

				// If we're just trying to fit as many elements as we can into
				// the available buffer space, we can trim some padding at the
				// end of the buffer in order to create widest possible linear
				// texture that will fit.
				if ((UINT_MAX == LinearTextureDesc.NumElements) && (Height > GMaxTextureDimensions))
				{
					Width = GMaxTextureDimensions;
					Height = 1;

					while ((Width * Height) < NumElements)
					{
						Height <<= 1;
					}

					while ((Width * Height) > NumElements)
					{
						Height -= 1;
					}
				}

				checkf(Width <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Width, (int32)GMaxTextureDimensions, Buffer.GetLength(), BytesPerElement, NumElements);
				checkf(Height <= GMaxTextureDimensions, TEXT("Calculated height %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Height, (int32)GMaxTextureDimensions, Buffer.GetLength(), BytesPerElement, NumElements);
			}

			RowBytes = Width * BytesPerElement;

			check(RowBytes % MinimumByteAlignment == 0);
			check((RowBytes * Height) + Offset <= Buffer.GetLength());

			Desc = mtlpp::TextureDescriptor::Texture2DDescriptor(MTLFormat, Width, Height, NO);
			Desc.SetStorageMode(Mode);
			Desc.SetCpuCacheMode(Buffer.GetCpuCacheMode());
			Desc.SetUsage((mtlpp::TextureUsage)TexUsage);
			Desc.SetResourceOptions((mtlpp::ResourceOptions)Options);
		}

		FMetalTexture Texture = MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, NewTexture(Desc, Offset, RowBytes));
		METAL_FATAL_ASSERT(Texture, TEXT("Failed to create linear texture, desc %s from buffer %s"), *FString([Desc description]), *FString([Buffer description]));

		return Texture;
	}
	else
	{
		return nil;
	}
}

struct FMetalRHICommandCreateLinearTexture : public FRHICommand<FMetalRHICommandCreateLinearTexture>
{
	FMetalRHIBuffer* Buffer;
	TRefCountPtr<FRHIResource> Parent;
	EPixelFormat Format;
	FMetalLinearTextureDescriptor LinearTextureDesc;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandCreateLinearTexture(FMetalRHIBuffer* InBuffer, FRHIResource* InParent, EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
		: Buffer(InBuffer)
		, Parent(InParent)
		, Format(InFormat)
		, LinearTextureDesc()
	{
		if (InLinearTextureDescriptor)
		{
			LinearTextureDesc = *InLinearTextureDescriptor;
		}
	}
	
	virtual ~FMetalRHICommandCreateLinearTexture()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		Buffer->CreateLinearTexture(Format, Parent.GetReference(), &LinearTextureDesc);
	}
};

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		if (IsRunningRHIInSeparateThread() && !IsInRHIThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass())
		{
			new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FMetalRHICommandCreateLinearTexture>()) FMetalRHICommandCreateLinearTexture(this, InParent, InFormat, InLinearTextureDescriptor);
		}
		else
		{
			LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());

			FMetalTexture* ExistingTexture = LinearTextures.Find(MapKey);
			if (ExistingTexture)
			{
				Texture = *ExistingTexture;
			}
			else
			{
				FMetalTexture NewTexture = AllocLinearTexture(InFormat, MapKey.Value);
				check(NewTexture);
				check(GMetalBufferFormats[InFormat].LinearTextureFormat == mtlpp::PixelFormat::RG11B10Float || GMetalBufferFormats[InFormat].LinearTextureFormat == (mtlpp::PixelFormat)NewTexture.GetPixelFormat());
				LinearTextures.Add(MapKey, NewTexture);
				Texture = NewTexture;
			}
		}
	}
	return Texture;
}

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::GetLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());

		FMetalTexture* ExistingTexture = LinearTextures.Find(MapKey);
		if (ExistingTexture)
		{
			Texture = *ExistingTexture;
		}
	}
	return Texture;
}

bool FMetalRHIBuffer::CanUseBufferAsBackingForAsyncCopy() const
{
	return (!bIsUniformBufferBacking ||
			UniformBufferFrameIndex != GetMetalDeviceContext().GetDeviceFrameIndex() ||
			UniformBufferPreviousOffset != Buffer.GetOffset());
}

void FMetalRHIBuffer::ConditionalSetUniformBufferFrameIndex()
{
	if (bIsUniformBufferBacking)
	{
		UniformBufferFrameIndex = GetMetalDeviceContext().GetDeviceFrameIndex();
	}
}

void* FMetalRHIBuffer::Lock(bool bIsOnRHIThread, EResourceLockMode LockMode, uint32 Offset, uint32 InSize, bool bIsUniformBuffer /*= false*/)
{
	check(LockSize == 0 && LockOffset == 0);
	
	if (Data)
	{
		check(Data->Data);
		return ((uint8*)Data->Data) + Offset;
	}
	
	check(!Buffer.IsAliasable());
	
    uint32 Len = Buffer.GetLength();
    
	// In order to properly synchronise the buffer access, when a dynamic buffer is locked for writing, discard the old buffer & create a new one. This prevents writing to a buffer while it is being read by the GPU & thus causing corruption. This matches the logic of other RHIs.
	if (LockMode == RLM_WriteOnly)
	{
        if (CPUBuffer)
        {
			METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
			SafeReleaseMetalBuffer(CPUBuffer);
			CPUBuffer = nil;
			
			if (LastUpdate && LastUpdate == GFrameNumberRenderThread)
			{
				METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
				SafeReleaseMetalBuffer(Buffer);
				Buffer = nil;
			}
		}
		else if (Mode == BUFFER_STORAGE_MODE)
		{
			// Turns out to be better to use Shared->Private blits whenever possible
			// Should only put write-once buffers into Shared/Managed or the cost of recreating linear textures overwhelms any other efficiency
			Mode =  CanUsePrivateMemory() ? mtlpp::StorageMode::Private : Mode;
			METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
			SafeReleaseMetalBuffer(Buffer);
			Buffer = nil;
		}
	}
	
	// When writing to a private buffer, make sure that we can perform an async copy so we don't introduce order of operation bugs
	// When we can't we have to reallocate the backing store
	if (LockMode != RLM_ReadOnly &&
		Mode == mtlpp::StorageMode::Private &&
		Buffer &&
		(!GetMetalDeviceContext().CanAsyncCopyToBuffer(Buffer) ||
		 !CanUseBufferAsBackingForAsyncCopy()))
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
		SafeReleaseMetalBuffer(Buffer);
		Buffer = nil;
	}
	
    Alloc(Len, LockMode, bIsUniformBuffer);
	AllocTransferBuffer(bIsOnRHIThread, Len, LockMode);
	
	FMetalBuffer& theBufferToUse = CPUBuffer ? CPUBuffer : Buffer;
	if(LockMode != RLM_ReadOnly)
	{
        METAL_DEBUG_OPTION(GetMetalDeviceContext().ValidateIsInactiveBuffer(theBufferToUse));
        
		LockSize = Size;
		LockOffset = Offset;
	}
	else if (CPUBuffer)
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalBufferPageOffTime);
        check(bIsOnRHIThread);
		
		// Synchronise the buffer with the CPU
		GetMetalDeviceContext().CopyFromBufferToBuffer(Buffer, 0, CPUBuffer, 0, Buffer.GetLength());
		
		//kick the current command buffer.
		GetMetalDeviceContext().SubmitCommandBufferAndWait();
	}
#if PLATFORM_MAC
	else if(Mode == mtlpp::StorageMode::Managed)
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalBufferPageOffTime);
        check(bIsOnRHIThread);
		
		// Synchronise the buffer with the CPU
		GetMetalDeviceContext().SynchroniseResource(Buffer);
		
		//kick the current command buffer.
		GetMetalDeviceContext().SubmitCommandBufferAndWait();
	}
#endif

	check(theBufferToUse && theBufferToUse.GetPtr());
	check(theBufferToUse.GetContents());

	return ((uint8*)MTLPP_VALIDATE(mtlpp::Buffer, theBufferToUse, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents())) + Offset;
}

void FMetalRHIBuffer::Unlock()
{
	if (!Data)
	{
		if (LockSize && CPUBuffer)
		{
			// Synchronise the buffer with the GPU
			GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(CPUBuffer, 0, Buffer, 0, FMath::Min(CPUBuffer.GetLength(), Buffer.GetLength()));
			
			ConditionalSetUniformBufferPreviousOffset();
			
			if (CPUBuffer)
            {
				SafeReleaseMetalBuffer(CPUBuffer);
				CPUBuffer = nil;
			}
			else
			{
				LastUpdate = GFrameNumberRenderThread;
			}
		}
#if PLATFORM_MAC
		else if(LockSize && Mode == mtlpp::StorageMode::Managed)
		{
			if (GMetalBufferZeroFill)
				MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, Buffer.GetLength())));
			else
				MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(LockOffset, LockSize)));
		}
#endif
	}
	LockSize = 0;
	LockOffset = 0;
}

FVertexBufferRHIRef FMetalDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FMetalVertexBuffer(0, 0);
	}
	
	// make the RHI object, which will allocate memory
	FMetalVertexBuffer* VertexBuffer = new FMetalVertexBuffer(Size, InUsage);

	if (CreateInfo.ResourceArray)
	{
		check(Size >= CreateInfo.ResourceArray->GetResourceDataSize());

		// make a buffer usable by CPU
		void* Buffer = ::RHILockVertexBuffer(VertexBuffer, 0, Size, RLM_WriteOnly);
		
		// copy the contents of the given data into the buffer
		FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);
		
		::RHIUnlockVertexBuffer(VertexBuffer);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
	else if (VertexBuffer->Mode == mtlpp::StorageMode::Private)
	{
		check (!VertexBuffer->CPUBuffer);

		if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
		{
			GetMetalDeviceContext().FillBuffer(VertexBuffer->Buffer, ns::Range(0, VertexBuffer->Buffer.GetLength()), 0);
		}
	}
#if PLATFORM_MAC
	else if (GMetalBufferZeroFill && VertexBuffer->Mode == mtlpp::StorageMode::Managed)
	{
		MTLPP_VALIDATE(mtlpp::Buffer, VertexBuffer->Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, VertexBuffer->Buffer.GetLength())));
	}
#endif

	return VertexBuffer;
	}
}

void* FMetalDynamicRHI::LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// default to vertex buffer memory
	return (uint8*)VertexBuffer->Lock(true, LockMode, Offset, Size);
	}
}

void FMetalDynamicRHI::UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	VertexBuffer->Unlock();
	}
}

void FMetalDynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	@autoreleasepool {
		FMetalVertexBuffer* SrcVertexBuffer = ResourceCast(SourceBufferRHI);
		FMetalVertexBuffer* DstVertexBuffer = ResourceCast(DestBufferRHI);
	
		if (SrcVertexBuffer->Buffer && DstVertexBuffer->Buffer)
		{
			GetMetalDeviceContext().CopyFromBufferToBuffer(SrcVertexBuffer->Buffer, 0, DstVertexBuffer->Buffer, 0, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
		}
		else if (DstVertexBuffer->Buffer)
		{
			FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), SrcVertexBuffer->GetSize(), BUF_Dynamic, mtlpp::StorageMode::Shared);
			FMetalBuffer TempBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
			FMemory::Memcpy(TempBuffer.GetContents(), SrcVertexBuffer->Data->Data, SrcVertexBuffer->GetSize());
			GetMetalDeviceContext().CopyFromBufferToBuffer(TempBuffer, 0, DstVertexBuffer->Buffer, 0, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
			SafeReleaseMetalBuffer(TempBuffer);
		}
		else
		{
			void const* SrcData = SrcVertexBuffer->Lock(true, RLM_ReadOnly, 0);
			void* DstData = DstVertexBuffer->Lock(true, RLM_WriteOnly, 0);
			FMemory::Memcpy(DstData, SrcData, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
			SrcVertexBuffer->Unlock();
			DstVertexBuffer->Unlock();
		}
	}
}

struct FMetalRHICommandInitialiseBuffer : public FRHICommand<FMetalRHICommandInitialiseBuffer>
{
	TRefCountPtr<FRHIResource> Resource;
	FMetalRHIBuffer* Buffer;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandInitialiseBuffer(FMetalRHIBuffer* InBuffer, FRHIResource* InResource)
	: Resource(InResource)
	, Buffer(InBuffer)
	{
	}
	
	virtual ~FMetalRHICommandInitialiseBuffer()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		if (Buffer->CPUBuffer)
		{
			uint32 Size = FMath::Min(Buffer->Buffer.GetLength(), Buffer->CPUBuffer.GetLength());
			GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(Buffer->CPUBuffer, 0, Buffer->Buffer, 0, Size);

			if (Buffer->CPUBuffer)
			{
				SafeReleaseMetalBuffer(Buffer->CPUBuffer);
				Buffer->CPUBuffer = nil;
			}
			else
			{
				Buffer->LastUpdate = GFrameNumberRenderThread;
			}
		}
		else if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
		{
			GetMetalDeviceContext().FillBuffer(Buffer->Buffer, ns::Range(0, Buffer->Buffer.GetLength()), 0);
		}
	}
};

void FMetalRHIBuffer::Init_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource)
{
	if (CreateInfo.ResourceArray)
	{
		check(InSize == CreateInfo.ResourceArray->GetResourceDataSize());
		
		AllocTransferBuffer(RHICmdList.IsBottomOfPipe(), InSize, RLM_WriteOnly);
		
		if (CPUBuffer)
		{
			FMemory::Memcpy(CPUBuffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), InSize);
			
			if (RHICmdList.IsBottomOfPipe())
			{
				FMetalRHICommandInitialiseBuffer UpdateCommand(this, Resource);
				UpdateCommand.Execute(RHICmdList);
			}
			else
			{
				new (RHICmdList.AllocCommand<FMetalRHICommandInitialiseBuffer>()) FMetalRHICommandInitialiseBuffer(this, Resource);
			}
		}
		else
		{
			FMemory::Memcpy(Buffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), InSize);
#if PLATFORM_MAC
			if(Mode == mtlpp::StorageMode::Managed)
			{
				MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, GMetalBufferZeroFill ? Buffer.GetLength() : InSize)));
			}
#endif
		}
		
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
	else if (Buffer)
	{
		check (!CPUBuffer);
		
		if (GMetalBufferZeroFill && Mode == mtlpp::StorageMode::Private)
		{
			if (RHICmdList.IsBottomOfPipe())
			{
				FMetalRHICommandInitialiseBuffer UpdateCommand(this, Resource);
				UpdateCommand.Execute(RHICmdList);
			}
			else
			{
				new (RHICmdList.AllocCommand<FMetalRHICommandInitialiseBuffer>()) FMetalRHICommandInitialiseBuffer(this, Resource);
			}
		}
#if PLATFORM_MAC
		else if (GMetalBufferZeroFill && Mode == mtlpp::StorageMode::Managed)
		{
			MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, Buffer.GetLength())));
		}
#endif
	}
}

FVertexBufferRHIRef FMetalDynamicRHI::CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		if (CreateInfo.bWithoutNativeResource)
		{
			return new FMetalVertexBuffer(0, 0);
		}
		
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalVertexBuffer> VertexBuffer = new FMetalVertexBuffer(Size, InUsage);
		
		VertexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, VertexBuffer);
		
		return VertexBuffer.GetReference();
	}
}

void FMetalDynamicRHI::RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
{
	check(DestVertexBuffer);
	FMetalVertexBuffer* Dest = ResourceCast(DestVertexBuffer);
	if (!SrcVertexBuffer)
	{
		TRefCountPtr<FMetalVertexBuffer> DeletionProxy = new FMetalVertexBuffer(0, 0);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FMetalVertexBuffer* Src = ResourceCast(SrcVertexBuffer);
		Dest->Swap(*Src);
	}
}

void* FMetalDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FMetalStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	return Buffer->Lock(Offset, SizeRHI);
}
void FMetalDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	FMetalStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	Buffer->Unlock();
}

FStagingBufferRHIRef FMetalDynamicRHI::RHICreateStagingBuffer()
{
	return new FMetalStagingBuffer();
}

FMetalStagingBuffer::~FMetalStagingBuffer()
{
	if (ShadowBuffer)
	{
		SafeReleaseMetalBuffer(ShadowBuffer);
		ShadowBuffer = nil;
	}
}

// Returns the pointer to read the buffer. There is no locking; the buffer is always shared.
// If this was not fenced correctly it will not have the expected data.
void *FMetalStagingBuffer::Lock(uint32 Offset, uint32 NumBytes)
{
	check(ShadowBuffer);
	check(!bIsLocked);
	bIsLocked = true;
	uint8* BackingPtr = (uint8*)ShadowBuffer.GetContents();
	return BackingPtr + Offset;
}

void FMetalStagingBuffer::Unlock()
{
	// does nothing in metal.
	check(bIsLocked);
	bIsLocked = false;
}
