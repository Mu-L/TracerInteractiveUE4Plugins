// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

bool FMetalRHIBuffer::UsePrivateMemory() const
{
	return (FMetalCommandQueue::SupportsFeature(EMetalFeaturesEfficientBufferBlits) && (Usage & (BUF_Dynamic|BUF_Static)))
	|| (FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs) && Usage & (BUF_ShaderResource|BUF_UnorderedAccess));
}

FMetalRHIBuffer::FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType)
: Data(nullptr)
, LastUpdate(0)
, LockOffset(0)
, LockSize(0)
, Size(InSize)
, Usage(InUsage)
, Type(InType)
{
	// No life-time usage information? Enforce Dynamic.
	if ((Usage & (BUF_Volatile|BUF_Dynamic|BUF_Static)) == 0)
	{
		Usage |= BUF_Dynamic;
	}
	
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
				if ((InUsage & BUF_UnorderedAccess) && ((InSize - AllocSize) < 512))
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
							check(Dimension <= GMaxTextureDimensions);
							AllocSize = Align(Size, Dimension);
							NumElements = AllocSize;
							SizeX = NumElements;
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
	for (TPair<EPixelFormat, FMetalTexture>& Pair : LinearTextures)
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
	if (Buffer.GetStorageMode() == mtlpp::StorageMode::Private && Buffer.GetHeap() && !Buffer.IsAliasable())
	{
		Buffer.MakeAliasable();
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		MetalLLM::LogAliasBuffer(Buffer);
#endif
	}
}

void FMetalRHIBuffer::Unalias()
{
	if (Buffer.GetStorageMode() == mtlpp::StorageMode::Private && Buffer.GetHeap() && Buffer.IsAliasable())
	{
		uint32 Len = Buffer.GetLength();
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
		SafeReleaseMetalBuffer(Buffer);
		Buffer = nil;
		
		Alloc(Len, RLM_WriteOnly);
	}
}

void FMetalRHIBuffer::Alloc(uint32 InSize, EResourceLockMode LockMode)
{
	bool const bUsePrivateMem = UsePrivateMemory();

	if (!Buffer)
	{
        check(LockMode != RLM_ReadOnly);
		mtlpp::StorageMode Mode = (bUsePrivateMem ? mtlpp::StorageMode::Private : BUFFER_STORAGE_MODE);
        FMetalPooledBufferArgs Args(GetMetalDeviceContext().GetDevice(), InSize, Usage, Mode);
		Buffer = GetMetalDeviceContext().CreatePooledBuffer(Args);
		METAL_FATAL_ASSERT(Buffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)Mode);

        Buffer.SetOwner(this);

        METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
        
		if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
		{
			for (TPair<EPixelFormat, FMetalTexture>& Pair : LinearTextures)
			{
				SafeReleaseMetalTexture(Pair.Value);
				Pair.Value = nil;
				
				Pair.Value = AllocLinearTexture(Pair.Key);
				check(Pair.Value);
			}
		}
	}
	
	if (bUsePrivateMem && !CPUBuffer)
	{
        FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), InSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
		CPUBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
		CPUBuffer.SetOwner(this);
		check(CPUBuffer && CPUBuffer.GetPtr());
        METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
		METAL_FATAL_ASSERT(CPUBuffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)mtlpp::StorageMode::Shared);
	}
}

FMetalTexture FMetalRHIBuffer::AllocLinearTexture(EPixelFormat Format)
{
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
	{
		mtlpp::PixelFormat MTLFormat = (mtlpp::PixelFormat)GMetalBufferFormats[Format].LinearTextureFormat;
		
		mtlpp::TextureDescriptor Desc;
		NSUInteger Mode = ((NSUInteger)Buffer.GetStorageMode() << mtlpp::ResourceStorageModeShift) | ((NSUInteger)Buffer.GetCpuCacheMode() << mtlpp::ResourceCpuCacheModeShift);
		Mode = FMetalCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions(Mode | mtlpp::ResourceOptions::HazardTrackingModeUntracked));
		NSUInteger TexUsage = mtlpp::TextureUsage::Unknown;
		if (Usage & BUF_ShaderResource)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderRead;
		}
		if (Usage & BUF_UnorderedAccess)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderWrite;
		}
		
		uint32 Stride = GPixelFormats[Format].BlockBytes;
		if (MTLFormat == mtlpp::PixelFormat::RG11B10Float && MTLFormat != (mtlpp::PixelFormat)GPixelFormats[Format].PlatformFormat)
		{
			Stride = 4;
		}
		NSUInteger NewSize = Size;

		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
		{
			Desc = mtlpp::TextureDescriptor::TextureBufferDescriptor(MTLFormat, NewSize / Stride, mtlpp::ResourceOptions(Mode), mtlpp::TextureUsage(TexUsage));
			Desc.SetAllowGPUOptimisedContents(false);
		}
		else
		{
			uint32 NumElements = (Buffer.GetLength() / Stride);
			uint32 SizeX = NumElements;
			uint32 SizeY = 1;
			if (NumElements > GMaxTextureDimensions)
			{
				uint32 Dimension = GMaxTextureDimensions;
				while((NumElements % Dimension) != 0)
				{
					check(Dimension >= 1);
					Dimension = (Dimension >> 1);
				}
				SizeX = Dimension;
				SizeY = NumElements / Dimension;
				checkf(SizeX <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), SizeX, (int32)GMaxTextureDimensions, Buffer.GetLength(), Stride, NumElements);
				checkf(SizeX <= GMaxTextureDimensions, TEXT("Calculated height %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), SizeY, (int32)GMaxTextureDimensions, Buffer.GetLength(), Stride, NumElements);
			}
			
			check(((SizeX*Stride) % 1024) == 0);
			NewSize = SizeX*Stride;
			
			Desc = mtlpp::TextureDescriptor::Texture2DDescriptor(MTLFormat, SizeX, SizeY, NO);
			Desc.SetStorageMode(Buffer.GetStorageMode());
			Desc.SetCpuCacheMode(Buffer.GetCpuCacheMode());
			Desc.SetUsage((mtlpp::TextureUsage)TexUsage);
			Desc.SetResourceOptions((mtlpp::ResourceOptions)Mode);
		}
		
		FMetalTexture Texture = MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, NewTexture(Desc, 0, NewSize));
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
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandCreateLinearTexture(FMetalRHIBuffer* InBuffer, FRHIResource* InParent, EPixelFormat InFormat)
	: Buffer(InBuffer)
	, Parent(InParent)
	, Format(InFormat)
	{
	}
	
	virtual ~FMetalRHICommandCreateLinearTexture()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		Buffer->CreateLinearTexture(Format, Parent.GetReference());
	}
};

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::CreateLinearTexture(EPixelFormat Format, FRHIResource* InParent)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[Format].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		if (IsRunningRHIInSeparateThread() && !IsInRHIThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass())
		{
			new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FMetalRHICommandCreateLinearTexture>()) FMetalRHICommandCreateLinearTexture(this, InParent, Format);
		}
		else
		{
			FMetalTexture* ExistingTexture = LinearTextures.Find(Format);
			if (ExistingTexture)
			{
				Texture = *ExistingTexture;
			}
			else
			{
				FMetalTexture NewTexture = AllocLinearTexture(Format);
				check(NewTexture);
				check(GMetalBufferFormats[Format].LinearTextureFormat == mtlpp::PixelFormat::RG11B10Float || GMetalBufferFormats[Format].LinearTextureFormat == (mtlpp::PixelFormat)NewTexture.GetPixelFormat());
				LinearTextures.Add(Format, NewTexture);
				Texture = NewTexture;
			}
		}
	}
	return Texture;
}

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::GetLinearTexture(EPixelFormat Format)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[Format].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		FMetalTexture* ExistingTexture = LinearTextures.Find(Format);
		if (ExistingTexture)
		{
			Texture = *ExistingTexture;
		}
	}
	return Texture;
}

void* FMetalRHIBuffer::Lock(EResourceLockMode LockMode, uint32 Offset, uint32 InSize)
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
        bool const bUsePrivateMem = UsePrivateMemory();
        if (bUsePrivateMem)
        {
			if (CPUBuffer)
			{
				METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
				SafeReleaseMetalBuffer(CPUBuffer);
				CPUBuffer = nil;
			}
			
			if (LastUpdate && LastUpdate == GFrameNumberRenderThread)
			{
				METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
				SafeReleaseMetalBuffer(Buffer);
				Buffer = nil;
			}
		}
		else
		{
			METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
			SafeReleaseMetalBuffer(Buffer);
			Buffer = nil;
		}
	}
    
    Alloc(Len, LockMode);
	
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
		
		// Synchronise the buffer with the CPU
		GetMetalDeviceContext().CopyFromBufferToBuffer(Buffer, 0, CPUBuffer, 0, Buffer.GetLength());

#if PLATFORM_MAC
		if(CPUBuffer.GetStorageMode() == mtlpp::StorageMode::Managed)
		{
			// Synchronise the buffer with the CPU
			GetMetalDeviceContext().SynchroniseResource(CPUBuffer);
		}
#endif
		
		//kick the current command buffer.
		GetMetalDeviceContext().SubmitCommandBufferAndWait();
	}
#if PLATFORM_MAC
	else if(theBufferToUse.GetStorageMode() == mtlpp::StorageMode::Managed)
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalBufferPageOffTime);
		
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
#if PLATFORM_MAC
			if(CPUBuffer.GetStorageMode() == mtlpp::StorageMode::Managed)
			{
				if (GMetalBufferZeroFill)
					MTLPP_VALIDATE(mtlpp::Buffer, CPUBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, Buffer.GetLength())));
				else
					MTLPP_VALIDATE(mtlpp::Buffer, CPUBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(LockOffset, LockSize)));
			}
#endif

			// Synchronise the buffer with the GPU
			GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(CPUBuffer, 0, Buffer, 0, Buffer.GetLength());
			if (UsePrivateMemory())
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
		else if(LockSize && Buffer.GetStorageMode() == mtlpp::StorageMode::Managed)
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
		void* Buffer = RHILockVertexBuffer(VertexBuffer, 0, Size, RLM_WriteOnly);
		
		// copy the contents of the given data into the buffer
		FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);
		
		RHIUnlockVertexBuffer(VertexBuffer);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
	else if (VertexBuffer->Buffer.GetStorageMode() == mtlpp::StorageMode::Private)
	{
		if (VertexBuffer->UsePrivateMemory())
		{
			SafeReleaseMetalBuffer(VertexBuffer->CPUBuffer);
			VertexBuffer->CPUBuffer = nil;
		}

		if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
		{
			GetMetalDeviceContext().FillBuffer(VertexBuffer->Buffer, ns::Range(0, VertexBuffer->Buffer.GetLength()), 0);
		}
	}
#if PLATFORM_MAC
	else if (GMetalBufferZeroFill && VertexBuffer->Buffer.GetStorageMode() == mtlpp::StorageMode::Managed)
	{
		MTLPP_VALIDATE(mtlpp::Buffer, VertexBuffer->Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, VertexBuffer->Buffer.GetLength())));
	}
#endif

	return VertexBuffer;
	}
}

void* FMetalDynamicRHI::RHILockVertexBuffer(FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// default to vertex buffer memory
	return (uint8*)VertexBuffer->Lock(LockMode, Offset, Size);
	}
}

void FMetalDynamicRHI::RHIUnlockVertexBuffer(FRHIVertexBuffer* VertexBufferRHI)
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
		else
		{
			void const* SrcData = SrcVertexBuffer->Lock(RLM_ReadOnly, 0);
			void* DstData = DstVertexBuffer->Lock(RLM_WriteOnly, 0);
			FMemory::Memcpy(DstData, SrcData, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
			SrcVertexBuffer->Unlock();
			DstVertexBuffer->Unlock();
		}
	}
}

struct FMetalRHICommandInitialiseVertexBuffer : public FRHICommand<FMetalRHICommandInitialiseVertexBuffer>
{
	TRefCountPtr<FMetalVertexBuffer> Buffer;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandInitialiseVertexBuffer(FMetalVertexBuffer* InBuffer)
	: Buffer(InBuffer)
	{
	}
	
	virtual ~FMetalRHICommandInitialiseVertexBuffer()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		if (Buffer->CPUBuffer)
		{
			uint32 Size = FMath::Min(Buffer->Buffer.GetLength(), Buffer->CPUBuffer.GetLength());
			GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(Buffer->CPUBuffer, 0, Buffer->Buffer, 0, Size);

			if (Buffer->UsePrivateMemory())
			{
				SafeReleaseMetalBuffer(Buffer->CPUBuffer);
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

FVertexBufferRHIRef FMetalDynamicRHI::CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		if (CreateInfo.bWithoutNativeResource)
		{
			return new FMetalVertexBuffer(0, 0);
		}
		
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalVertexBuffer> VertexBuffer = new FMetalVertexBuffer(Size, InUsage);
		
		if (CreateInfo.ResourceArray)
		{
			check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
			
			if (VertexBuffer->CPUBuffer)
			{
				FMemory::Memcpy(VertexBuffer->CPUBuffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), Size);

#if PLATFORM_MAC
				if(VertexBuffer->CPUBuffer.GetStorageMode() == mtlpp::StorageMode::Managed)
				{
					MTLPP_VALIDATE(mtlpp::Buffer, VertexBuffer->CPUBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, GMetalBufferZeroFill ? VertexBuffer->CPUBuffer.GetLength() : Size)));
				}
#endif

				if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
				{
					FMetalRHICommandInitialiseVertexBuffer UpdateCommand(VertexBuffer);
					UpdateCommand.Execute(RHICmdList);
				}
				else
				{
					new (RHICmdList.AllocCommand<FMetalRHICommandInitialiseVertexBuffer>()) FMetalRHICommandInitialiseVertexBuffer(VertexBuffer);
				}
			}
			else
			{
				// make a buffer usable by CPU
				void* Buffer = RHILockVertexBuffer(VertexBuffer, 0, Size, RLM_WriteOnly);
				
				// copy the contents of the given data into the buffer
				FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);
				
				RHIUnlockVertexBuffer(VertexBuffer);
			}
			
			// Discard the resource array's contents.
			CreateInfo.ResourceArray->Discard();
		}
		else if (VertexBuffer->Buffer)
		{
			if (VertexBuffer->UsePrivateMemory())
			{
				SafeReleaseMetalBuffer(VertexBuffer->CPUBuffer);
				VertexBuffer->CPUBuffer = nil;
			}
			
			if (GMetalBufferZeroFill && VertexBuffer->Buffer.GetStorageMode() == mtlpp::StorageMode::Private)
			{
				if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
				{
					FMetalRHICommandInitialiseVertexBuffer UpdateCommand(VertexBuffer);
					UpdateCommand.Execute(RHICmdList);
				}
				else
				{
					new (RHICmdList.AllocCommand<FMetalRHICommandInitialiseVertexBuffer>()) FMetalRHICommandInitialiseVertexBuffer(VertexBuffer);
				}
			}
#if PLATFORM_MAC
			else if (GMetalBufferZeroFill && VertexBuffer->Buffer.GetStorageMode() == mtlpp::StorageMode::Managed)
			{
				MTLPP_VALIDATE(mtlpp::Buffer, VertexBuffer->Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, VertexBuffer->Buffer.GetLength())));
			}
#endif
		}
		
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

void* FMetalDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 SizeRHI)
{
	FMetalStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	return Buffer->Lock(Offset, SizeRHI);
}
void FMetalDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	FMetalStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	Buffer->Unlock();
}

void* FMetalDynamicRHI::LockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 SizeRHI)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMetalDynamicRHI_LockStagingBuffer_RenderThread);
	check(IsInRenderingThread());
	
	return RHILockStagingBuffer(StagingBuffer, Offset, SizeRHI);
}
void FMetalDynamicRHI::UnlockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMetalDynamicRHI_UnlockStagingBuffer_RenderThread);
	check(IsInRenderingThread());
	
	return RHIUnlockStagingBuffer(StagingBuffer);
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
