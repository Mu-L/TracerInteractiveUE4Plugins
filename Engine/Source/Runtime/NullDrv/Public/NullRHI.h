// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ResourceArray.h"
#include "Serialization/LargeMemoryData.h"
#include "RHI.h"

struct Rect;

/** A null implementation of the dynamically bound RHI. */
class FNullDynamicRHI : public FDynamicRHI , public IRHICommandContextPSOFallback
{
public:

	FNullDynamicRHI();

	// FDynamicRHI interface.
	virtual void Init();
	virtual void Shutdown();
	virtual const TCHAR* GetName() override { return TEXT("Null"); }

	virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) final override
	{ 
		return new FRHISamplerState(); 
	}
	virtual FRasterizerStateRHIRef RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer) final override
	{ 
		return new FRHIRasterizerState(); 
	}
	virtual FDepthStencilStateRHIRef RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer) final override
	{ 
		return new FRHIDepthStencilState(); 
	}
	virtual FBlendStateRHIRef RHICreateBlendState(const FBlendStateInitializerRHI& Initializer) final override
	{ 
		return new FRHIBlendState(); 
	}
	virtual FVertexDeclarationRHIRef RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements) final override
	{ 
		return new FRHIVertexDeclaration(); 
	}

	virtual FPixelShaderRHIRef RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIPixelShader(); 
	}

	virtual FVertexShaderRHIRef RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIVertexShader(); 
	}

	virtual FHullShaderRHIRef RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIHullShader(); 
	}

	virtual FDomainShaderRHIRef RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIDomainShader(); 
	}

	virtual FGeometryShaderRHIRef RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIGeometryShader(); 
	}

	virtual FComputeShaderRHIRef RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash) final override
	{ 
		return new FRHIComputeShader(); 
	}


	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader) final override
	{ 
		return new FRHIBoundShaderState(); 
	}

	virtual void RHISetComputeShader(FRHIComputeShader* ComputeShader) final override
	{

	}

	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override
	{

	}

	virtual void RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override
	{

	}

	virtual void RHICreateTransition(FRHITransition* Transition, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines, ERHICreateTransitionFlags CreateFlags, TArrayView<const FRHITransitionInfo> Infos) final override
	{
	}

	virtual void RHIReleaseTransition(FRHITransition* Transition) final override
	{
	}

	virtual void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) final override
	{
	}

	virtual void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) final override
	{
	}

	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override
	{

	}

	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) final override
	{ 
		return new FRHIUniformBuffer(Layout); 
	}

	virtual void RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents) final override
	{

	}

	virtual FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState , FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		if(CreateInfo.ResourceArray) 
		{ 
			CreateInfo.ResourceArray->Discard(); 
		} 
		return new FRHIIndexBuffer(Stride,Size,InUsage); 
	}

	virtual void* LockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode) final override
	{ 
		return GetStaticBuffer(Size);
	}
	virtual void UnlockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer) final override
	{

	}

	virtual void RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer) final override
	{

	}

	virtual FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		if(CreateInfo.ResourceArray) 
		{ 
			CreateInfo.ResourceArray->Discard(); 
		} 
		return new FRHIVertexBuffer(Size,InUsage); 
	}

	virtual void* LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override
	{ 
		return GetStaticBuffer(VertexBuffer->GetSize());
	}

	virtual void UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer) final override
	{

	}

	virtual void RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer) final override
	{

	}


	virtual void RHICopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer) final override
	{

	}

	virtual FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		if(CreateInfo.ResourceArray) 
		{ 
			CreateInfo.ResourceArray->Discard(); 
		} 
		return new FRHIStructuredBuffer(Stride,Size,InUsage); 
	}

	virtual void* LockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) final override
	{ 
		return GetStaticBuffer(StructuredBuffer->GetSize());
	}
	virtual void UnlockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) final override
	{

	}


	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) final override
	{ 
		return new FRHIUnorderedAccessView(); 
	}


	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel) final override
	{ 
		return new FRHIUnorderedAccessView(); 
	}


	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format) final override
	{ 
		return new FRHIUnorderedAccessView(); 
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format) final override
	{
		return new FRHIUnorderedAccessView();
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer) final override
	{ 
		return new FRHIShaderResourceView(); 
	}


	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) final override
	{ 
		return new FRHIShaderResourceView(); 
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer) final override
	{
		return new FRHIShaderResourceView();
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer) final override
	{ 
		return new FRHIShaderResourceView(); 
	}

	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values) final override
	{

	}

	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) final override
	{

	}

	virtual uint64 RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override
	{ 
		OutAlign = 0; 
		return 0; 
	}


	virtual uint64 RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override
	{ 
		OutAlign = 0; 
		return 0; 
	}

	virtual uint64 RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) final override
	{ 
		OutAlign = 0; 
		return 0; 
	}

	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) final override
	{

	}

	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData,int32 SizeX,int32 SizeY,int32 Pitch,int32 PixelSize) final override
	{ 
		return false; 
	}

	virtual FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime) final override
	{ 
		return new FRHITextureReferenceNullImpl(); 
	}

	virtual void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture) final override
	{ 
		if(TextureRef) 
		{ 
			((FRHITextureReferenceNullImpl*)TextureRef)->SetReferencedTexture(NewTexture); 
		} 
	}


	virtual FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		return new FRHITexture2D(SizeX,SizeY,NumMips,NumSamples,(EPixelFormat)Format,Flags, CreateInfo.ClearValueBinding); 
	}

	virtual FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips) final override
	{ 
		return FTexture2DRHIRef(); 
	}

	virtual void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D) final override
	{
	}
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		return new FRHITexture2DArray(SizeX,SizeY,SizeZ,NumMips,NumSamples,(EPixelFormat)Format,Flags, CreateInfo.ClearValueBinding); 
	}

	virtual FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		return new FRHITexture3D(SizeX, SizeY, SizeZ, NumMips, (EPixelFormat)Format, Flags, CreateInfo.ClearValueBinding);
	}
	virtual void RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo) final override
	{
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) final override
	{ 
		return new FRHIShaderResourceView(); 
	}

	virtual void RHIGenerateMips(FRHITexture* Texture) final override
	{

	}
	virtual uint32 RHIComputeMemorySize(FRHITexture* TextureRHI) final override
	{ 
		return 0; 
	}
	virtual FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) final override
	{ 
		return new FRHITexture2D(NewSizeX,NewSizeY,NewMipCount,1,Texture2D->GetFormat(),Texture2D->GetFlags(), Texture2D->GetClearBinding());
	}
	virtual ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override
	{ 
		return TexRealloc_Succeeded; 
	}
	virtual ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) final override
	{ 
		return TexRealloc_Succeeded; 
	}
	virtual void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override
	{ 
		DestStride = 0; 
		return GetStaticTextureBuffer(Texture->GetSizeX(), Texture->GetSizeY(), Texture->GetFormat(), DestStride);
	}
	virtual void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail) final override
	{

	}
	virtual void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override
	{ 
		DestStride = 0; 
		return GetStaticTextureBuffer(Texture->GetSizeX(), Texture->GetSizeY(), Texture->GetFormat(), DestStride);
	}
	virtual void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail) final override
	{

	}
	virtual void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) final override
	{

	}
	virtual void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) final override
	{

	}
	virtual FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		return new FRHITextureCube(Size, NumMips, (EPixelFormat)Format, Flags, CreateInfo.ClearValueBinding);
	}
	virtual FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo) final override
	{ 
		return new FRHITextureCube(Size, NumMips, (EPixelFormat)Format, Flags, CreateInfo.ClearValueBinding);
	}
	virtual void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) final override
	{ 
		DestStride = 0; 
		return GetStaticTextureBuffer(Texture->GetSize(), Texture->GetSize(), Texture->GetFormat(), DestStride);
	}
	virtual void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) final override
	{
	}
	virtual void RHICopyToResolveTarget(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FResolveParams& ResolveParams) final override
	{

	}

	virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo) final override
	{

	}

	virtual void RHIBindDebugLabelName(FRHITexture* Texture, const TCHAR* Name) final override
	{

	}

	virtual void RHIReadSurfaceData(FRHITexture* Texture,FIntRect Rect,TArray<FColor>& OutData,FReadSurfaceDataFlags InFlags) final override
	{ 
		OutData.AddZeroed(Rect.Width() * Rect.Height()); 
	}


	virtual void RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex) final override
	{

	}


	virtual void RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex) final override
	{

	}

	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,TArray<FFloat16Color>& OutData,ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex) final override
	{

	}

	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData) final override
	{

	}



	virtual FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType) final override
	{ 
		return new FRHIRenderQuery(); 
	}

	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) final override
	{

	}
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) final override
	{

	}


	virtual bool RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex = INDEX_NONE) final override
	{ 
		return true; 
	}

	virtual void RHISubmitCommandsHint() final override
	{
	}


	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) final override
	{
	}

	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) final override
	{
	}

	virtual FTexture2DRHIRef RHIGetViewportBackBuffer(FRHIViewport* Viewport) final override
	{ 
		return new FRHITexture2D(1,1,1,1,PF_B8G8R8A8,TexCreate_RenderTargetable, FClearValueBinding()); 
	}

	virtual void RHIBeginFrame() final override
	{

	}


	virtual void RHIEndFrame() final override
	{

	}
	virtual void RHIBeginScene() final override
	{

	}
	virtual void RHIEndScene() final override
	{

	}
	virtual void RHIAliasTextureResources(FTextureRHIRef& DestTexture, FTextureRHIRef& SrcTexture) final override
	{

	}
	virtual void RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport) final override
	{

	}
	virtual void RHIAcquireThreadOwnership() final override
	{

	}
	virtual void RHIReleaseThreadOwnership() final override
	{

	}


	virtual void RHIFlushResources() final override
	{

	}

	virtual uint32 RHIGetGPUFrameCycles(uint32 GPUIndex = 0) final override
	{ 
		return 0; 
	}

	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) final override
	{ 
		return new FRHIViewport(); 
	}
	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen) final override
	{

	}

	virtual EColorSpaceAndEOTF RHIGetColorSpace(FRHIViewport* Viewport ) final override
	{
		return EColorSpaceAndEOTF::EColorSpace_Rec709;
	}

	virtual void RHICheckViewportHDRStatus(FRHIViewport* Viewport) final override
	{
	}

	virtual void RHITick(float DeltaTime) final override
	{

	}

	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset) final override
	{
	}

	virtual void RHISetRasterizerState(FRHIRasterizerState* NewState) final override
	{

	}

	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) final override
	{

	}

	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override
	{
	}

	virtual void RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderState) final override
	{
	}


	virtual void RHISetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* NewTexture) final override
	{
	}

	virtual void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture) final override
	{

	}

	virtual void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState) final override
	{

	}

	virtual void RHISetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* NewState) final override
	{
	}

	virtual void RHISetUAVParameter(FRHIPixelShader* PixelShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override
	{
	}

	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV) final override
	{

	}


	virtual void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount) final override
	{

	}


	virtual void RHISetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override
	{
	}

	virtual void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV) final override
	{
	}


	virtual void RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override
	{
	}

	virtual void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer) final override
	{
	}

	virtual void RHISetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
	}

	virtual void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
	}


	virtual void RHISetDepthStencilState(FRHIDepthStencilState* NewState, uint32 StencilRef) final override
	{

	}

	virtual void RHISetBlendState(FRHIBlendState* NewState, const FLinearColor& BlendFactor) final override
	{

	}

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
	{
	}

	virtual void RHIEndRenderPass()
	{
	}

	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override
	{

	}
	virtual void RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override
	{

	}

	virtual void RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override
	{

	}


	virtual void RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override
	{

	}
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override
	{

	}

	virtual void RHIBlockUntilGPUIdle() final override
	{
	}
	virtual bool RHIEnqueueDecompress(uint8_t*, uint8_t*, int, void*) final override
	{
		return false;
	}
	virtual bool RHIEnqueueCompress(uint8_t*, uint8_t*, int, void*) final override
	{
		return false;
	}
	virtual bool RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate) final override
	{ 
		return false; 
	}
	virtual void RHIGetSupportedResolution(uint32& Width, uint32& Height) final override
	{

	}
	virtual void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip) final override
	{

	}
	virtual void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip) final override
	{

	}
	virtual void RHIExecuteCommandList(FRHICommandList* CmdList) final override
	{

	}
	virtual void RHIEnableDepthBoundsTest(bool bEnable) final override
	{
	}
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override
	{
	}
	virtual void RHISetShadingRate(EVRSShadingRate ShadingRate, EVRSRateCombiner Combiner) final override
	{
	}
	virtual void RHISetShadingRateImage(FRHITexture* RateImageTexture, EVRSRateCombiner Combiner)  final override
	{
	}
	virtual void* RHIGetNativeDevice() final override
	{ 
		return 0; 
	}
	virtual void* RHIGetNativeInstance() final override
	{
		return 0;
	}
	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override
	{
	}
	virtual void RHIPopEvent()
	{
	}
	virtual class IRHICommandContext* RHIGetDefaultContext() final override
	{ 
		return this; 
	}
	virtual class IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num) final override
	{ 
		return nullptr; 
	}

private:
	FLargeMemoryData MemoryBuffer;

	/** Allocates a static buffer for RHI functions to return as a write destination. */
	void* GetStaticBuffer(size_t Size);
	void* GetStaticTextureBuffer(int32 SizeX, int32 SizeY, EPixelFormat Format, uint32& DestStride);
};
