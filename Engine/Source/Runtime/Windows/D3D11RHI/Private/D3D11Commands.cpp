// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Commands.cpp: D3D RHI commands implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "D3D11RHIPrivateUtil.h"
#include "StaticBoundShaderState.h"
#include "GlobalShader.h"
#include "OneColorShader.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "EngineGlobals.h"

#if PLATFORM_DESKTOP
// For Depth Bounds Test interface
#include "Windows/AllowWindowsPlatformTypes.h"
	#include "nvapi.h"
	#include "amd_ags.h"
#if INTEL_EXTENSIONS
	#include "igd11ext.h"
#endif
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define DECLARE_ISBOUNDSHADER(ShaderType) inline void ValidateBoundShader(FD3D11StateCache& InStateCache, FRHI##ShaderType* ShaderType##RHI) \
{ \
	ID3D11##ShaderType* CachedShader; \
	InStateCache.Get##ShaderType(&CachedShader); \
	FD3D11##ShaderType* ShaderType = FD3D11DynamicRHI::ResourceCast(ShaderType##RHI); \
	ensureMsgf(CachedShader == ShaderType->Resource, TEXT("Parameters are being set for a %s which is not currently bound"), TEXT( #ShaderType )); \
	if (CachedShader) { CachedShader->Release(); } \
}

DECLARE_ISBOUNDSHADER(VertexShader)
DECLARE_ISBOUNDSHADER(PixelShader)
DECLARE_ISBOUNDSHADER(GeometryShader)
DECLARE_ISBOUNDSHADER(HullShader)
DECLARE_ISBOUNDSHADER(DomainShader)
DECLARE_ISBOUNDSHADER(ComputeShader)


#if DO_GUARD_SLOW
#define VALIDATE_BOUND_SHADER(s) ValidateBoundShader(StateCache, s)
#else
#define VALIDATE_BOUND_SHADER(s)
#endif

int32 GEnableDX11TransitionChecks = 0;
static FAutoConsoleVariableRef CVarDX11TransitionChecks(
	TEXT("r.TransitionChecksEnableDX11"),
	GEnableDX11TransitionChecks,
	TEXT("Enables transition checks in the DX11 RHI."),
	ECVF_Default
	);

static int32 GUnbindResourcesBetweenDrawsInDX11 = UE_BUILD_DEBUG;
static FAutoConsoleVariableRef CVarUnbindResourcesBetweenDrawsInDX11(
	TEXT("r.UnbindResourcesBetweenDrawsInDX11"),
	GUnbindResourcesBetweenDrawsInDX11,
	TEXT("Unbind resources between material changes in DX11."),
	ECVF_Default
	);

int32 GDX11ReduceRTVRebinds = 1;
static FAutoConsoleVariableRef CVarDX11ReduceRTVRebinds(
	TEXT("r.DX11.ReduceRTVRebinds"),
	GDX11ReduceRTVRebinds,
	TEXT("Reduce # of SetRenderTargetCalls."),
	ECVF_ReadOnly
);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
int32 GLogDX11RTRebinds = 0;
static FAutoConsoleVariableRef CVarLogDx11RTRebinds(
	TEXT("r.DX11.LogRTRebinds"),
	GLogDX11RTRebinds,
	TEXT("Log # of rebinds of RTs per frame"),
	ECVF_Default
);
FThreadSafeCounter GDX11RTRebind;
FThreadSafeCounter GDX11CommitGraphicsResourceTables;
#endif


void FD3D11BaseShaderResource::SetDirty(bool bInDirty, uint32 CurrentFrame)
{
	bDirty = bInDirty;
	if (bDirty)
	{
		LastFrameWritten = CurrentFrame;
	}
	ensureMsgf((GEnableDX11TransitionChecks == 0) || !(CurrentGPUAccess == EResourceTransitionAccess::EReadable && bDirty), TEXT("ShaderResource is dirty, but set to Readable."));
}

#if !PLATFORM_HOLOLENS
//MultiGPU
void FD3D11DynamicRHI::RHIBeginUpdateMultiFrameResource(FRHITexture* RHITexture)
{
	if (!IsRHIDeviceNVIDIA() || GNumAlternateFrameRenderingGroups == 1) return;

	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(RHITexture);

	if (!Texture)
	{
		return;
	}

	if (!Texture->GetIHVResourceHandle())
	{
		// get a resource handle for this texture
		void* IHVHandle = nullptr;
		NvAPI_D3D_GetObjectHandleForResource(Direct3DDevice, Texture->GetResource(), (NVDX_ObjectHandle*)&(IHVHandle));
		Texture->SetIHVResourceHandle(IHVHandle);
	}
	
	RHIPushEvent(TEXT("BeginMFUpdate"), FColor::Black);
	NvAPI_D3D_BeginResourceRendering(Direct3DDevice, (NVDX_ObjectHandle)Texture->GetIHVResourceHandle(), 0);
	RHIPopEvent();
}

void FD3D11DynamicRHI::RHIEndUpdateMultiFrameResource(FRHITexture* RHITexture)
{
	if (!IsRHIDeviceNVIDIA() || GNumAlternateFrameRenderingGroups == 1) return;

	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(RHITexture);

	if (!Texture || !Texture->GetIHVResourceHandle())
	{
		return;
	}

	RHIPushEvent(TEXT("EndMFUpdate"), FColor::Black);
	NvAPI_D3D_EndResourceRendering(Direct3DDevice, (NVDX_ObjectHandle)Texture->GetIHVResourceHandle(), 0);
	RHIPopEvent();	
}

void FD3D11DynamicRHI::RHIBeginUpdateMultiFrameResource(FRHIUnorderedAccessView* UAVRHI)
{
	if (!IsRHIDeviceNVIDIA() || GNumAlternateFrameRenderingGroups == 1) return;

	FD3D11UnorderedAccessView* UAV = ResourceCast(UAVRHI);
	
	if (!UAV)
	{
		return;
	}

	if (!UAV->IHVResourceHandle)
	{
		// get a resource handle for this texture		
		ID3D11Resource* D3DResource = nullptr;
		UAV->View->GetResource(&D3DResource);
		NvAPI_D3D_GetObjectHandleForResource(Direct3DDevice, D3DResource, (NVDX_ObjectHandle*)&(UAV->IHVResourceHandle));
	}
	
	RHIPushEvent(TEXT("BeginMFUpdateUAV"), FColor::Black);
	NvAPI_D3D_BeginResourceRendering(Direct3DDevice, (NVDX_ObjectHandle)UAV->IHVResourceHandle, 0);
	RHIPopEvent();
}

void FD3D11DynamicRHI::RHIEndUpdateMultiFrameResource(FRHIUnorderedAccessView* UAVRHI)
{
	if (!IsRHIDeviceNVIDIA() || GNumAlternateFrameRenderingGroups == 1) return;

	FD3D11UnorderedAccessView* UAV = ResourceCast(UAVRHI);

	if (!UAV || !UAV->IHVResourceHandle)
	{
		return;
	}

	RHIPushEvent(TEXT("EndMFUpdateUAV"), FColor::Black);
	NvAPI_D3D_EndResourceRendering(Direct3DDevice, (NVDX_ObjectHandle)UAV->IHVResourceHandle, 0);
	RHIPopEvent();
}
#endif

// Vertex state.
void FD3D11DynamicRHI::RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset)
{
	FD3D11VertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	ID3D11Buffer* D3DBuffer = VertexBuffer ? VertexBuffer->Resource : NULL;
	TrackResourceBoundAsVB(VertexBuffer, StreamIndex);
	StateCache.SetStreamSource(D3DBuffer, StreamIndex, Offset);
}

// Rasterizer state.
void FD3D11DynamicRHI::RHISetRasterizerState(FRHIRasterizerState* NewStateRHI)
{
	FD3D11RasterizerState* NewState = ResourceCast(NewStateRHI);
	StateCache.SetRasterizerState(NewState->Resource);
}

void FD3D11DynamicRHI::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState)
{
	FRHIGraphicsPipelineStateFallBack* FallbackGraphicsState = static_cast<FRHIGraphicsPipelineStateFallBack*>(GraphicsState);
	IRHICommandContextPSOFallback::RHISetGraphicsPipelineState(GraphicsState);
	const FGraphicsPipelineStateInitializer& PsoInit = FallbackGraphicsState->Initializer;

	ApplyGlobalUniformBuffers(static_cast<FD3D11VertexShader*>(PsoInit.BoundShaderState.VertexShaderRHI));
	ApplyGlobalUniformBuffers(static_cast<FD3D11HullShader*>(PsoInit.BoundShaderState.HullShaderRHI));
	ApplyGlobalUniformBuffers(static_cast<FD3D11DomainShader*>(PsoInit.BoundShaderState.DomainShaderRHI));
	ApplyGlobalUniformBuffers(static_cast<FD3D11GeometryShader*>(PsoInit.BoundShaderState.GeometryShaderRHI));
	ApplyGlobalUniformBuffers(static_cast<FD3D11PixelShader*>(PsoInit.BoundShaderState.PixelShaderRHI));

	// Store the PSO's primitive (after since IRHICommandContext::RHISetGraphicsPipelineState sets the BSS)
	PrimitiveType = PsoInit.PrimitiveType;
}

void FD3D11DynamicRHI::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	FD3D11ComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);
	SetCurrentComputeShader(ComputeShaderRHI);

	if (GUnbindResourcesBetweenDrawsInDX11)
	{
		ClearAllShaderResourcesForFrequency<SF_Compute>();
	}

	ApplyGlobalUniformBuffers(ComputeShader);
}

void FD3D11DynamicRHI::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) 
{ 
	FRHIComputeShader* ComputeShaderRHI = GetCurrentComputeShader();
	FD3D11ComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

	StateCache.SetComputeShader(ComputeShader->Resource);

	GPUProfilingData.RegisterGPUDispatch(FIntVector(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ));	

	if (ComputeShader->bShaderNeedsGlobalConstantBuffer)
	{
		CommitComputeShaderConstants();
	}
	CommitComputeResourceTables(ComputeShader);
	
	Direct3DDeviceIMContext->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);	
	StateCache.SetComputeShader(nullptr);
	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{ 
	FRHIComputeShader* ComputeShaderRHI = GetCurrentComputeShader();
	FD3D11ComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);
	FD3D11VertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

	GPUProfilingData.RegisterGPUDispatch(FIntVector(1, 1, 1));

	StateCache.SetComputeShader(ComputeShader->Resource);
	
	if (ComputeShader->bShaderNeedsGlobalConstantBuffer)
	{
		CommitComputeShaderConstants();
	}
	CommitComputeResourceTables(ComputeShader);

	Direct3DDeviceIMContext->DispatchIndirect(ArgumentBuffer->Resource,ArgumentOffset);
	StateCache.SetComputeShader(nullptr);
	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
{
	// These are the maximum viewport extents for D3D11. Exceeding them leads to badness.
	check(MinX <= (float)D3D11_VIEWPORT_BOUNDS_MAX);
	check(MinY <= (float)D3D11_VIEWPORT_BOUNDS_MAX);
	check(MaxX <= (float)D3D11_VIEWPORT_BOUNDS_MAX);
	check(MaxY <= (float)D3D11_VIEWPORT_BOUNDS_MAX);

	D3D11_VIEWPORT Viewport = { MinX, MinY, MaxX - MinX, MaxY - MinY, MinZ, MaxZ };
	//avoid setting a 0 extent viewport, which the debug runtime doesn't like
	if (Viewport.Width > 0 && Viewport.Height > 0)
	{
		StateCache.SetViewport(Viewport);
		RHISetScissorRect(true, MinX, MinY, MaxX, MaxY);
	}
}

static void ValidateScissorRect(const D3D11_VIEWPORT& Viewport, const D3D11_RECT& ScissorRect)
{
	ensure(ScissorRect.left   >= (LONG)Viewport.TopLeftX);
	ensure(ScissorRect.top    >= (LONG)Viewport.TopLeftY);
	ensure(ScissorRect.right  <= (LONG)Viewport.TopLeftX + (LONG)Viewport.Width);
	ensure(ScissorRect.bottom <= (LONG)Viewport.TopLeftY + (LONG)Viewport.Height);
	ensure(ScissorRect.left <= ScissorRect.right && ScissorRect.top <= ScissorRect.bottom);
}

void FD3D11DynamicRHI::RHISetScissorRect(bool bEnable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
{
	D3D11_VIEWPORT Viewport;
	StateCache.GetViewport(&Viewport);

	D3D11_RECT ScissorRect;
	if (bEnable)
	{
		ScissorRect.left   = MinX;
		ScissorRect.top    = MinY;
		ScissorRect.right  = MaxX;
		ScissorRect.bottom = MaxY;
	}
	else
	{
		ScissorRect.left   = (LONG) Viewport.TopLeftX;
		ScissorRect.top    = (LONG) Viewport.TopLeftY;
		ScissorRect.right  = (LONG) Viewport.TopLeftX + (LONG) Viewport.Width;
		ScissorRect.bottom = (LONG) Viewport.TopLeftY + (LONG) Viewport.Height;
	}

	ValidateScissorRect(Viewport, ScissorRect);
	Direct3DDeviceIMContext->RSSetScissorRects(1, &ScissorRect);
}

/**
* Set bound shader state. This will set the vertex decl/shader, and pixel shader
* @param BoundShaderState - state resource
*/
void FD3D11DynamicRHI::RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderStateRHI)
{
	FD3D11BoundShaderState* BoundShaderState = ResourceCast(BoundShaderStateRHI);

	StateCache.SetStreamStrides(BoundShaderState->StreamStrides);
	StateCache.SetInputLayout(BoundShaderState->InputLayout);
	StateCache.SetVertexShader(BoundShaderState->VertexShader);
	StateCache.SetPixelShader(BoundShaderState->PixelShader);

	StateCache.SetHullShader(BoundShaderState->HullShader);
	StateCache.SetDomainShader(BoundShaderState->DomainShader);
	StateCache.SetGeometryShader(BoundShaderState->GeometryShader);

	if(BoundShaderState->HullShader != NULL && BoundShaderState->DomainShader != NULL)
	{
		bUsingTessellation = true;
	}
	else
	{
		bUsingTessellation = false;
	}

	// @TODO : really should only discard the constants if the shader state has actually changed.
	bDiscardSharedConstants = true;

	// Prevent transient bound shader states from being recreated for each use by keeping a history of the most recently used bound shader states.
	// The history keeps them alive, and the bound shader state cache allows them to am be reused if needed.
	BoundShaderStateHistory.Add(BoundShaderState);

	// Shader changed so all resource tables are dirty
	DirtyUniformBuffers[SF_Vertex] = 0xffff;
	DirtyUniformBuffers[SF_Pixel] = 0xffff;
	DirtyUniformBuffers[SF_Hull] = 0xffff;
	DirtyUniformBuffers[SF_Domain] = 0xffff;
	DirtyUniformBuffers[SF_Geometry] = 0xffff;

	// Shader changed.  All UB's must be reset by high level code to match other platforms anway.
	// Clear to catch those bugs, and bugs with stale UB's causing layout mismatches.
	// Release references to bound uniform buffers.
	for (int32 Frequency = 0; Frequency < SF_NumStandardFrequencies; ++Frequency)
	{
		for (int32 BindIndex = 0; BindIndex < MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE; ++BindIndex)
		{
			BoundUniformBuffers[Frequency][BindIndex].SafeRelease();
		}
	}

	extern bool D3D11RHI_ShouldCreateWithD3DDebug();
	static bool bHasD3DDebug = D3D11RHI_ShouldCreateWithD3DDebug();
	if (GUnbindResourcesBetweenDrawsInDX11 || bHasD3DDebug)
	{
		ClearAllShaderResources();
	}
}

template <EShaderFrequency ShaderFrequency>
FORCEINLINE void FD3D11DynamicRHI::SetShaderTexture(FD3D11TextureBase* NewTexture, ID3D11ShaderResourceView* ShaderResourceView, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	if ((NewTexture == nullptr) || (NewTexture->GetRenderTargetView(0, 0) != NULL) || (NewTexture->HasDepthStencilView()))
	{
		SetShaderResourceView<ShaderFrequency>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI ? NewTextureRHI->GetName() : NAME_None, FD3D11StateCache::SRV_Dynamic);
	}
	else
	{
		SetShaderResourceView<ShaderFrequency>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI->GetName(), FD3D11StateCache::SRV_Static);
	}
}

void FD3D11DynamicRHI::RHISetShaderTexture(FRHIGraphicsShader* ShaderRHI,uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	FD3D11TextureBase* NewTexture = GetD3D11TextureFromRHITexture(NewTextureRHI);
	ID3D11ShaderResourceView* ShaderResourceView = NewTexture ? NewTexture->GetShaderResourceView() : nullptr;

	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
	{
		FD3D11VertexShader* VertexShader = static_cast<FD3D11VertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShader);
		SetShaderTexture<SF_Vertex>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
	}
	break;
	case SF_Hull:
	{
		FD3D11HullShader* HullShader = static_cast<FD3D11HullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShader);
		SetShaderTexture<SF_Hull>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
	}
	break;
	case SF_Domain:
	{
		FD3D11DomainShader* DomainShader = static_cast<FD3D11DomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShader);
		SetShaderTexture<SF_Domain>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
	}
	break;
	case SF_Geometry:
	{
		FD3D11GeometryShader* GeometryShader = static_cast<FD3D11GeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShader);
		SetShaderTexture<SF_Geometry>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
	}
	break;
	case SF_Pixel:
	{
		FD3D11PixelShader* PixelShader = static_cast<FD3D11PixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShader);
		SetShaderTexture<SF_Pixel>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}
}

void FD3D11DynamicRHI::RHISetShaderTexture(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D11TextureBase* NewTexture = GetD3D11TextureFromRHITexture(NewTextureRHI);
	ID3D11ShaderResourceView* ShaderResourceView = NewTexture ? NewTexture->GetShaderResourceView() : nullptr;
	SetShaderTexture<SF_Compute>(NewTexture, ShaderResourceView, TextureIndex, NewTextureRHI);
}


void FD3D11DynamicRHI::RHISetUAVParameter(FRHIPixelShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	FD3D11UnorderedAccessView* UAV = ResourceCast(UAVRHI);

	if (UAV)
	{
		ConditionalClearShaderResource(UAV->Resource, true);

		//check it's safe for r/w for this UAV
		const EResourceTransitionAccess CurrentUAVAccess = UAV->Resource->GetCurrentGPUAccess();
		const bool UAVDirty = UAV->Resource->IsDirty();
		ensureMsgf((GEnableDX11TransitionChecks == 0) || !UAVDirty || (CurrentUAVAccess == EResourceTransitionAccess::ERWNoBarrier), TEXT("UAV: %i is in unsafe state for GPU R/W: %s, Dirty: %i"), UAVIndex, *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)CurrentUAVAccess], (int32)UAVDirty);

		//UAVs always dirty themselves. If a shader wanted to just read, it should use an SRV.
		UAV->Resource->SetDirty(true, PresentCounter);
	}
	if (CurrentUAVs[UAVIndex] != UAV)
	{
		CurrentUAVs[UAVIndex] = UAV;
		UAVSChanged = 1;
	}
}

void FD3D11DynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D11UnorderedAccessView* UAV = ResourceCast(UAVRHI);

	if(UAV)
	{
		ConditionalClearShaderResource(UAV->Resource, true);		

		//check it's safe for r/w for this UAV
		const EResourceTransitionAccess CurrentUAVAccess = UAV->Resource->GetCurrentGPUAccess();
		const bool UAVDirty = UAV->Resource->IsDirty();
		ensureMsgf((GEnableDX11TransitionChecks == 0) || !UAVDirty || (CurrentUAVAccess == EResourceTransitionAccess::ERWNoBarrier), TEXT("UAV: %i is in unsafe state for GPU R/W: %s, Dirty: %i"), UAVIndex, *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)CurrentUAVAccess], (int32)UAVDirty);

		//UAVs always dirty themselves. If a shader wanted to just read, it should use an SRV.
		UAV->Resource->SetDirty(true, PresentCounter);
	}

	ID3D11UnorderedAccessView* D3D11UAV = UAV ? UAV->View : NULL;

	uint32 InitialCount = -1;
	Direct3DDeviceIMContext->CSSetUnorderedAccessViews(UAVIndex,1,&D3D11UAV, &InitialCount );
}

void FD3D11DynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount )
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D11UnorderedAccessView* UAV = ResourceCast(UAVRHI);
	
	if(UAV)
	{
		ConditionalClearShaderResource(UAV->Resource, true);

		//check it's safe for r/w for this UAV
		const EResourceTransitionAccess CurrentUAVAccess = UAV->Resource->GetCurrentGPUAccess();
		const bool UAVDirty = UAV->Resource->IsDirty();
		ensureMsgf((GEnableDX11TransitionChecks == 0) || !UAVDirty || (CurrentUAVAccess == EResourceTransitionAccess::ERWNoBarrier), TEXT("UAV: %i is in unsafe state for GPU R/W: %s, Dirty: %i"), UAVIndex, *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)CurrentUAVAccess], (int32)UAVDirty);

		//UAVs always dirty themselves. If a shader wanted to just read, it should use an SRV.
		UAV->Resource->SetDirty(true, PresentCounter);
	}

	ID3D11UnorderedAccessView* D3D11UAV = UAV ? UAV->View : NULL;
	Direct3DDeviceIMContext->CSSetUnorderedAccessViews(UAVIndex,1,&D3D11UAV, &InitialCount );
}

void FD3D11DynamicRHI::RHISetShaderResourceViewParameter(FRHIGraphicsShader* ShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	FD3D11ShaderResourceView* SRV = ResourceCast(SRVRHI);
	FD3D11BaseShaderResource* Resource = nullptr;
	ID3D11ShaderResourceView* D3D11SRV = nullptr;
	if (SRV)
	{
		Resource = SRV->Resource;
		D3D11SRV = SRV->View;
	}
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
	{
		FD3D11VertexShader* VertexShader = static_cast<FD3D11VertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShader);
		SetShaderResourceView<SF_Vertex>(Resource, D3D11SRV, TextureIndex, NAME_None);
	}
	break;
	case SF_Hull:
	{
		FD3D11HullShader* HullShader = static_cast<FD3D11HullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShader);
		SetShaderResourceView<SF_Hull>(Resource, D3D11SRV, TextureIndex, NAME_None);
	}
	break;
	case SF_Domain:
	{
		FD3D11DomainShader* DomainShader = static_cast<FD3D11DomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShader);
		SetShaderResourceView<SF_Domain>(Resource, D3D11SRV, TextureIndex, NAME_None);
	}
	break;
	case SF_Geometry:
	{
		FD3D11GeometryShader* GeometryShader = static_cast<FD3D11GeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShader);
		SetShaderResourceView<SF_Geometry>(Resource, D3D11SRV, TextureIndex, NAME_None);
	}
	break;
	case SF_Pixel:
	{
		FD3D11PixelShader* PixelShader = static_cast<FD3D11PixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShader);
		SetShaderResourceView<SF_Pixel>(Resource, D3D11SRV, TextureIndex, NAME_None);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}
}

void FD3D11DynamicRHI::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D11ShaderResourceView* SRV = ResourceCast(SRVRHI);

	FD3D11BaseShaderResource* Resource = nullptr;
	ID3D11ShaderResourceView* D3D11SRV = nullptr;
	
	if (SRV)
	{
		Resource = SRV->Resource;
		D3D11SRV = SRV->View;
	}

	SetShaderResourceView<SF_Compute>(Resource, D3D11SRV, TextureIndex, NAME_None);
}

void FD3D11DynamicRHI::RHISetShaderSampler(FRHIGraphicsShader* ShaderRHI,uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FD3D11SamplerState* NewState = ResourceCast(NewStateRHI);
	ID3D11SamplerState* StateResource = NewState->Resource;
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
	{
		FD3D11VertexShader* VertexShader = static_cast<FD3D11VertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShader);
		StateCache.SetSamplerState<SF_Vertex>(StateResource, SamplerIndex);
	}
		break;
	case SF_Hull:
	{
		FD3D11HullShader* HullShader = static_cast<FD3D11HullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShader);
		StateCache.SetSamplerState<SF_Hull>(StateResource, SamplerIndex);
	}
	break;
	case SF_Domain:
	{
		FD3D11DomainShader* DomainShader = static_cast<FD3D11DomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShader);
		StateCache.SetSamplerState<SF_Domain>(StateResource, SamplerIndex);
	}
	break;
	case SF_Geometry:
	{
		FD3D11GeometryShader* GeometryShader = static_cast<FD3D11GeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShader);
		StateCache.SetSamplerState<SF_Geometry>(StateResource, SamplerIndex);
	}
	break;
	case SF_Pixel:
	{
		FD3D11PixelShader* PixelShader = static_cast<FD3D11PixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShader);
		StateCache.SetSamplerState<SF_Pixel>(StateResource, SamplerIndex);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}
}

void FD3D11DynamicRHI::RHISetShaderSampler(FRHIComputeShader* ComputeShaderRHI,uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	FD3D11ComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);
	FD3D11SamplerState* NewState = ResourceCast(NewStateRHI);

	ID3D11SamplerState* StateResource = NewState->Resource;
	StateCache.SetSamplerState<SF_Compute>(StateResource, SamplerIndex);
}

void FD3D11DynamicRHI::RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
{
	FMemory::Memzero(GlobalUniformBuffers.GetData(), GlobalUniformBuffers.Num() * sizeof(FRHIUniformBuffer*));

	for (int32 Index = 0; Index < InUniformBuffers.GetUniformBufferCount(); ++Index)
	{
		GlobalUniformBuffers[InUniformBuffers.GetSlot(Index)] = InUniformBuffers.GetUniformBuffer(Index);
	}
}

void FD3D11DynamicRHI::RHISetShaderUniformBuffer(FRHIGraphicsShader* ShaderRHI,uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	check(BufferRHI->GetLayout().GetHash());
	FD3D11UniformBuffer* Buffer = ResourceCast(BufferRHI);
	ID3D11Buffer* ConstantBuffer = Buffer ? Buffer->Resource : NULL;
	EShaderFrequency Stage = SF_NumFrequencies;
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
	{
		FD3D11VertexShader* VertexShader = static_cast<FD3D11VertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShader);
		Stage = SF_Vertex;
		StateCache.SetConstantBuffer<SF_Vertex>(ConstantBuffer, BufferIndex);
	}
	break;
	case SF_Hull:
	{
		FD3D11HullShader* HullShader = static_cast<FD3D11HullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShader);
		Stage = SF_Hull;
		StateCache.SetConstantBuffer<SF_Hull>(ConstantBuffer, BufferIndex);
	}
	break;
	case SF_Domain:
	{
		FD3D11DomainShader* DomainShader = static_cast<FD3D11DomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShader);
		Stage = SF_Domain;
		StateCache.SetConstantBuffer<SF_Domain>(ConstantBuffer, BufferIndex);
	}
	break;
	case SF_Geometry:
	{
		FD3D11GeometryShader* GeometryShader = static_cast<FD3D11GeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShader);
		Stage = SF_Geometry;
		StateCache.SetConstantBuffer<SF_Geometry>(ConstantBuffer, BufferIndex);
	}
	break;
	case SF_Pixel:
	{
		FD3D11PixelShader* PixelShader = static_cast<FD3D11PixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShader);
		Stage = SF_Pixel;
		StateCache.SetConstantBuffer<SF_Pixel>(ConstantBuffer, BufferIndex);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
		return;
	}

	BoundUniformBuffers[Stage][BufferIndex] = BufferRHI;
	DirtyUniformBuffers[Stage] |= (1 << BufferIndex);
}

void FD3D11DynamicRHI::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader,uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	check(BufferRHI->GetLayout().GetHash());
	//VALIDATE_BOUND_SHADER(ComputeShader);
	FD3D11UniformBuffer* Buffer = ResourceCast(BufferRHI);
	{
		ID3D11Buffer* ConstantBuffer = Buffer ? Buffer->Resource : NULL;
		StateCache.SetConstantBuffer<SF_Compute>(ConstantBuffer, BufferIndex);
	}

	BoundUniformBuffers[SF_Compute][BufferIndex] = BufferRHI;
	DirtyUniformBuffers[SF_Compute] |= (1 << BufferIndex);
}

void FD3D11DynamicRHI::RHISetShaderParameter(FRHIGraphicsShader* ShaderRHI,uint32 BufferIndex,uint32 BaseIndex,uint32 NumBytes,const void* NewValue)
{
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
	{
		FD3D11VertexShader* VertexShader = static_cast<FD3D11VertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShader);
		checkSlow(VSConstantBuffers[BufferIndex]);
		VSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case SF_Hull:
	{
		FD3D11HullShader* HullShader = static_cast<FD3D11HullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShader);
		checkSlow(HSConstantBuffers[BufferIndex]);
		HSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case SF_Domain:
	{
		FD3D11DomainShader* DomainShader = static_cast<FD3D11DomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShader);
		checkSlow(DSConstantBuffers[BufferIndex]);
		DSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case SF_Geometry:
	{
		FD3D11GeometryShader* GeometryShader = static_cast<FD3D11GeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShader);
		checkSlow(GSConstantBuffers[BufferIndex]);
		GSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case SF_Pixel:
	{
		FD3D11PixelShader* PixelShader = static_cast<FD3D11PixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShader);
		checkSlow(PSConstantBuffers[BufferIndex]);
		PSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}
}

void FD3D11DynamicRHI::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex,uint32 BaseIndex,uint32 NumBytes,const void* NewValue)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	checkSlow(CSConstantBuffers[BufferIndex]);
	CSConstantBuffers[BufferIndex]->UpdateConstant((const uint8*)NewValue,BaseIndex,NumBytes);
}

void FD3D11DynamicRHI::ValidateExclusiveDepthStencilAccess(FExclusiveDepthStencil RequestedAccess) const
{
	const bool bSrcDepthWrite = RequestedAccess.IsDepthWrite();
	const bool bSrcStencilWrite = RequestedAccess.IsStencilWrite();

	if (bSrcDepthWrite || bSrcStencilWrite)
	{
		// New Rule: You have to call SetRenderTarget[s]() before
		ensure(CurrentDepthTexture);

		const bool bDstDepthWrite = CurrentDSVAccessType.IsDepthWrite();
		const bool bDstStencilWrite = CurrentDSVAccessType.IsStencilWrite();

		// requested access is not possible, fix SetRenderTarget EExclusiveDepthStencil or request a different one
		ensureMsgf(
			!bSrcDepthWrite || bDstDepthWrite, 
			TEXT("Expected: SrcDepthWrite := false or DstDepthWrite := true. Actual: SrcDepthWrite := %s or DstDepthWrite := %s"),
			(bSrcDepthWrite) ? TEXT("true") : TEXT("false"),
			(bDstDepthWrite) ? TEXT("true") : TEXT("false")
			);

		ensureMsgf(
			!bSrcStencilWrite || bDstStencilWrite,
			TEXT("Expected: SrcStencilWrite := false or DstStencilWrite := true. Actual: SrcStencilWrite := %s or DstStencilWrite := %s"),
			(bSrcStencilWrite) ? TEXT("true") : TEXT("false"),
			(bDstStencilWrite) ? TEXT("true") : TEXT("false")
			);
	}
}

void FD3D11DynamicRHI::RHISetDepthStencilState(FRHIDepthStencilState* NewStateRHI,uint32 StencilRef)
{
	FD3D11DepthStencilState* NewState = ResourceCast(NewStateRHI);

	ValidateExclusiveDepthStencilAccess(NewState->AccessType);

	StateCache.SetDepthStencilState(NewState->Resource, StencilRef);
}

void FD3D11DynamicRHI::RHISetStencilRef(uint32 StencilRef)
{
	StateCache.SetStencilRef(StencilRef);
}

void FD3D11DynamicRHI::RHISetBlendState(FRHIBlendState* NewStateRHI,const FLinearColor& BlendFactor)
{
	FD3D11BlendState* NewState = ResourceCast(NewStateRHI);
	StateCache.SetBlendState(NewState->Resource, (const float*)&BlendFactor, 0xffffffff);
}

void FD3D11DynamicRHI::RHISetBlendFactor(const FLinearColor& BlendFactor)
{
	StateCache.SetBlendFactor((const float*)&BlendFactor, 0xffffffff);
}
void FD3D11DynamicRHI::CommitRenderTargetsAndUAVs()
{
	CommitRenderTargets(false);
	FMemory::Memset(UAVBound, 0); //force to be rebound if any is set
	UAVSChanged = 1;
	CommitUAVs();

}
void FD3D11DynamicRHI::CommitRenderTargets(bool bClearUAVs)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D11RenderTargetCommits);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	GDX11RTRebind.Increment();
#endif
	ID3D11RenderTargetView* RTArray[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	for (uint32 RenderTargetIndex = 0; RenderTargetIndex < NumSimultaneousRenderTargets; ++RenderTargetIndex)
	{
		RTArray[RenderTargetIndex] = CurrentRenderTargets[RenderTargetIndex];
	}

	
	Direct3DDeviceIMContext->OMSetRenderTargets(
		NumSimultaneousRenderTargets,
		RTArray,
		CurrentDepthStencilTarget
	);

	if(bClearUAVs)
	{
		for(uint32 i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; ++i)
		{
			CurrentUAVs[i] = nullptr;
			UAVBound[i] = nullptr;
		}
		UAVBindFirst = 0;
		UAVBindCount = 0;
		UAVSChanged = 0;
	}
}

void FD3D11DynamicRHI::InternalSetUAVPS(uint32 BindIndex, FD3D11UnorderedAccessView* UnorderedAccessViewRHI)
{
	check(BindIndex < D3D11_PS_CS_UAV_REGISTER_COUNT);
	if(CurrentUAVs[BindIndex] != UnorderedAccessViewRHI)
	{
		CurrentUAVs[BindIndex] = UnorderedAccessViewRHI;
		UAVSChanged = 1;

	}
	ConditionalClearShaderResource(UnorderedAccessViewRHI->Resource, true);
}

void FD3D11DynamicRHI::CommitUAVs()
{
	if (!UAVSChanged)
	{
		return;
	}
	int32 First = -1;
	int32 Count = 0;
	for (int32 i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; ++i)
	{
		if (CurrentUAVs[i] != nullptr)
		{
			First = i;
			break;
		}
	}

	if (First != -1)
	{
		FD3D11UnorderedAccessView* RHIUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT];
		ID3D11UnorderedAccessView* UAVs[D3D11_PS_CS_UAV_REGISTER_COUNT];
		FMemory::Memset(UAVs, 0);

		for (int32 i = First; i < D3D11_PS_CS_UAV_REGISTER_COUNT; ++i)
		{
			if (CurrentUAVs[i] == nullptr)
				break;
			RHIUAVs[i] = CurrentUAVs[i].GetReference();
			UAVs[i] = RHIUAVs[i]->View;
			Count++;
		}

		if (First != UAVBindFirst || Count != UAVBindCount || 0 != FMemory::Memcmp(&UAVs[First], &UAVBound[First], sizeof(UAVs[0]) * Count))
		{
			SCOPE_CYCLE_COUNTER(STAT_D3D11RenderTargetCommitsUAV);
			for (int32 i = First; i < First + Count; ++i)
			{
				if (UAVs[i] != UAVBound[i])
				{
					FD3D11UnorderedAccessView* RHIUAV = RHIUAVs[i];
					ID3D11UnorderedAccessView* UAV = UAVs[i];
					if (UAV)
					{
						//check it's safe for r/w for this UAV
						const EResourceTransitionAccess CurrentUAVAccess = RHIUAV->Resource->GetCurrentGPUAccess();
						const bool UAVDirty = RHIUAV->Resource->IsDirty();
						const bool bAccessPass = (CurrentUAVAccess == EResourceTransitionAccess::ERWBarrier && !UAVDirty) || (CurrentUAVAccess == EResourceTransitionAccess::ERWNoBarrier);
						ensureMsgf((GEnableDX11TransitionChecks == 0) || bAccessPass, TEXT("UAV: %i is in unsafe state for GPU R/W: %s"), i, *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)CurrentUAVAccess]);

						//UAVs get set to dirty.  If the shader just wanted to read it should have used an SRV.
						RHIUAV->Resource->SetDirty(true, PresentCounter);
					}

					// Unbind any shader views of the UAV's resource.
					ConditionalClearShaderResource(RHIUAV->Resource, true);
					UAVBound[i] = UAV;
				}
			}
			static const uint32 UAVInitialCountArray[D3D11_PS_CS_UAV_REGISTER_COUNT] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
			Direct3DDeviceIMContext->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, 0, 0, First, Count, &UAVs[First], &UAVInitialCountArray[0]);
		}

	}
	else
	{
		if (First != UAVBindFirst)
		{
			Direct3DDeviceIMContext->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, 0, 0, 0, 0, nullptr, nullptr);
		}
	}

	UAVBindFirst = First;
	UAVBindCount = Count;
	UAVSChanged = 0;
}

struct FRTVDesc
{
	uint32 Width;
	uint32 Height;
	DXGI_SAMPLE_DESC SampleDesc;
};

// Return an FRTVDesc structure whose
// Width and height dimensions are adjusted for the RTV's miplevel.
FRTVDesc GetRenderTargetViewDesc(ID3D11RenderTargetView* RenderTargetView)
{
	D3D11_RENDER_TARGET_VIEW_DESC TargetDesc;
	RenderTargetView->GetDesc(&TargetDesc);

	TRefCountPtr<ID3D11Resource> BaseResource;
	RenderTargetView->GetResource((ID3D11Resource**)BaseResource.GetInitReference());
	uint32 MipIndex = 0;
	FRTVDesc ret;
	memset(&ret, 0, sizeof(ret));

	switch (TargetDesc.ViewDimension)
	{
		case D3D11_RTV_DIMENSION_TEXTURE2D:
		case D3D11_RTV_DIMENSION_TEXTURE2DMS:
		case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
		case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
		{
			D3D11_TEXTURE2D_DESC Desc;
			((ID3D11Texture2D*)(BaseResource.GetReference()))->GetDesc(&Desc);
			ret.Width = Desc.Width;
			ret.Height = Desc.Height;
			ret.SampleDesc = Desc.SampleDesc;
			if (TargetDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D || TargetDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
			{
				// All the non-multisampled texture types have their mip-slice in the same position.
				MipIndex = TargetDesc.Texture2D.MipSlice;
			}
			break;
		}
		case D3D11_RTV_DIMENSION_TEXTURE3D:
		{
			D3D11_TEXTURE3D_DESC Desc;
			((ID3D11Texture3D*)(BaseResource.GetReference()))->GetDesc(&Desc);
			ret.Width = Desc.Width;
			ret.Height = Desc.Height;
			ret.SampleDesc.Count = 1;
			ret.SampleDesc.Quality = 0;
			MipIndex = TargetDesc.Texture3D.MipSlice;
			break;
		}
		default:
		{
			// not expecting 1D targets.
			checkNoEntry();
		}
	}
	ret.Width >>= MipIndex;
	ret.Height >>= MipIndex;
	return ret;
}

void FD3D11DynamicRHI::RHISetRenderTargets(
	uint32 NewNumSimultaneousRenderTargets,
	const FRHIRenderTargetView* NewRenderTargetsRHI,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI)
{
	FD3D11TextureBase* NewDepthStencilTarget = GetD3D11TextureFromRHITexture(NewDepthStencilTargetRHI ? NewDepthStencilTargetRHI->Texture : nullptr);

#if CHECK_SRV_TRANSITIONS
	// if the depth buffer is writable then it counts as unresolved.
	if (NewDepthStencilTargetRHI && NewDepthStencilTargetRHI->GetDepthStencilAccess() == FExclusiveDepthStencil::DepthWrite_StencilWrite && NewDepthStencilTarget)
	{		
		check(UnresolvedTargetsConcurrencyGuard.Increment() == 1);
		UnresolvedTargets.Add(NewDepthStencilTarget->GetResource(), FUnresolvedRTInfo(NewDepthStencilTargetRHI->Texture->GetName(), 0, 1, -1, 1));
		check(UnresolvedTargetsConcurrencyGuard.Decrement() == 0);
	}
#endif

	check(NewNumSimultaneousRenderTargets <= MaxSimultaneousRenderTargets);

	bool bTargetChanged = false;

	// Set the appropriate depth stencil view depending on whether depth writes are enabled or not
	ID3D11DepthStencilView* DepthStencilView = NULL;
	if(NewDepthStencilTarget)
	{
		CurrentDSVAccessType = NewDepthStencilTargetRHI->GetDepthStencilAccess();
		DepthStencilView = NewDepthStencilTarget->GetDepthStencilView(CurrentDSVAccessType);

		// Unbind any shader views of the depth stencil target that are bound.
		ConditionalClearShaderResource(NewDepthStencilTarget, false);
	}

	// Check if the depth stencil target is different from the old state.
	if(CurrentDepthStencilTarget != DepthStencilView)
	{
		CurrentDepthTexture = NewDepthStencilTarget;
		CurrentDepthStencilTarget = DepthStencilView;
		bTargetChanged = true;
	}

	if (NewDepthStencilTarget)
	{
		uint32 CurrentFrame = PresentCounter;
		const EResourceTransitionAccess CurrentAccess = NewDepthStencilTarget->GetCurrentGPUAccess();
		const uint32 LastFrameWritten = NewDepthStencilTarget->GetLastFrameWritten();
		const bool bReadable = CurrentAccess == EResourceTransitionAccess::EReadable;
		const bool bDepthWrite = NewDepthStencilTargetRHI->GetDepthStencilAccess().IsDepthWrite();
		const bool bAccessValid =	!bReadable ||
									LastFrameWritten != CurrentFrame || 
									!bDepthWrite;

		ensureMsgf((GEnableDX11TransitionChecks == 0) || bAccessValid, TEXT("DepthTarget '%s' is not GPU writable."), *NewDepthStencilTargetRHI->Texture->GetName().ToString());

		//switch to writable state if this is the first render of the frame.  Don't switch if it's a later render and this is a depth test only situation
		if (!bAccessValid || (bReadable && bDepthWrite))
		{
			DUMP_TRANSITION(NewDepthStencilTargetRHI->Texture->GetName(), EResourceTransitionAccess::EWritable);
			NewDepthStencilTarget->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
		}

		if (bDepthWrite)
		{
			NewDepthStencilTarget->SetDirty(true, CurrentFrame);
		}
	}

	// Gather the render target views for the new render targets.
	ID3D11RenderTargetView* NewRenderTargetViews[MaxSimultaneousRenderTargets];
	for(uint32 RenderTargetIndex = 0;RenderTargetIndex < MaxSimultaneousRenderTargets;++RenderTargetIndex)
	{
		ID3D11RenderTargetView* RenderTargetView = NULL;
		if(RenderTargetIndex < NewNumSimultaneousRenderTargets && NewRenderTargetsRHI[RenderTargetIndex].Texture != nullptr)
		{
			int32 RTMipIndex = NewRenderTargetsRHI[RenderTargetIndex].MipIndex;
			int32 RTSliceIndex = NewRenderTargetsRHI[RenderTargetIndex].ArraySliceIndex;
			FD3D11TextureBase* NewRenderTarget = GetD3D11TextureFromRHITexture(NewRenderTargetsRHI[RenderTargetIndex].Texture);

			if (NewRenderTarget)
			{
				RenderTargetView = NewRenderTarget->GetRenderTargetView(RTMipIndex, RTSliceIndex);
				uint32 CurrentFrame = PresentCounter;
				const EResourceTransitionAccess CurrentAccess = NewRenderTarget->GetCurrentGPUAccess();
				const uint32 LastFrameWritten = NewRenderTarget->GetLastFrameWritten();
				const bool bReadable = CurrentAccess == EResourceTransitionAccess::EReadable;
				const bool bAccessValid = !bReadable || LastFrameWritten != CurrentFrame;
				ensureMsgf((GEnableDX11TransitionChecks == 0) || bAccessValid, TEXT("RenderTarget '%s' is not GPU writable."), *NewRenderTargetsRHI[RenderTargetIndex].Texture->GetName().ToString());
								
				if (!bAccessValid || bReadable)
				{
					DUMP_TRANSITION(NewRenderTargetsRHI[RenderTargetIndex].Texture->GetName(), EResourceTransitionAccess::EWritable);
					NewRenderTarget->SetCurrentGPUAccess(EResourceTransitionAccess::EWritable);
				}
				NewRenderTarget->SetDirty(true, CurrentFrame);
			}

			ensureMsgf(RenderTargetView, TEXT("Texture being set as render target has no RTV"));
#if CHECK_SRV_TRANSITIONS			
			if (RenderTargetView)
			{
				// remember this target as having been bound for write.
				ID3D11Resource* RTVResource;
				RenderTargetView->GetResource(&RTVResource);
				check(UnresolvedTargetsConcurrencyGuard.Increment() == 1);
				UnresolvedTargets.Add(RTVResource, FUnresolvedRTInfo(NewRenderTargetsRHI[RenderTargetIndex].Texture->GetName(), RTMipIndex, 1, RTSliceIndex, 1));
				check(UnresolvedTargetsConcurrencyGuard.Decrement() == 0);
				RTVResource->Release();
			}
#endif
			
			// Unbind any shader views of the render target that are bound.
			ConditionalClearShaderResource(NewRenderTarget, false);

#if UE_BUILD_DEBUG	
			// A check to allow you to pinpoint what is using mismatching targets
			// We filter our d3ddebug spew that checks for this as the d3d runtime's check is wrong.
			// For filter code, see D3D11Device.cpp look for "OMSETRENDERTARGETS_INVALIDVIEW"
			if(RenderTargetView && DepthStencilView)
			{
				FRTVDesc RTTDesc = GetRenderTargetViewDesc(RenderTargetView);

				TRefCountPtr<ID3D11Texture2D> DepthTargetTexture;
				DepthStencilView->GetResource((ID3D11Resource**)DepthTargetTexture.GetInitReference());

				D3D11_TEXTURE2D_DESC DTTDesc;
				DepthTargetTexture->GetDesc(&DTTDesc);

				// enforce color target is <= depth and MSAA settings match
				if(RTTDesc.Width > DTTDesc.Width || RTTDesc.Height > DTTDesc.Height || 
					RTTDesc.SampleDesc.Count != DTTDesc.SampleDesc.Count || 
					RTTDesc.SampleDesc.Quality != DTTDesc.SampleDesc.Quality)
				{
					UE_LOG(LogD3D11RHI, Fatal,TEXT("RTV(%i,%i c=%i,q=%i) and DSV(%i,%i c=%i,q=%i) have mismatching dimensions and/or MSAA levels!"),
						RTTDesc.Width,RTTDesc.Height,RTTDesc.SampleDesc.Count,RTTDesc.SampleDesc.Quality,
						DTTDesc.Width,DTTDesc.Height,DTTDesc.SampleDesc.Count,DTTDesc.SampleDesc.Quality);
				}
			}
#endif
		}

		NewRenderTargetViews[RenderTargetIndex] = RenderTargetView;

		// Check if the render target is different from the old state.
		if(CurrentRenderTargets[RenderTargetIndex] != RenderTargetView)
		{
			CurrentRenderTargets[RenderTargetIndex] = RenderTargetView;
			bTargetChanged = true;
		}
	}
	if(NumSimultaneousRenderTargets != NewNumSimultaneousRenderTargets)
	{
		NumSimultaneousRenderTargets = NewNumSimultaneousRenderTargets;
		uint32 Bit = 1;
		uint32 Mask = 0;
		for (uint32 Index = 0; Index < NumSimultaneousRenderTargets; ++Index)
		{
			Mask |= Bit;
			Bit <<= 1;
		}
		CurrentRTVOverlapMask = Mask;
		bTargetChanged = true;
	}

	// Only make the D3D call to change render targets if something actually changed.
	if(bTargetChanged)
	{
		CommitRenderTargets(true);
		CurrentUAVMask = 0;
	}

	// Set the viewport to the full size of render target 0.
	if (NewRenderTargetViews[0])
	{
		// check target 0 is valid
		check(0 < NewNumSimultaneousRenderTargets && NewRenderTargetsRHI[0].Texture != nullptr);
		FRTVDesc RTTDesc = GetRenderTargetViewDesc(NewRenderTargetViews[0]);
		RHISetViewport(0.0f, 0.0f, 0.0f, (float)RTTDesc.Width, (float)RTTDesc.Height, 1.0f);
	}
	else if( DepthStencilView )
	{
		TRefCountPtr<ID3D11Texture2D> DepthTargetTexture;
		DepthStencilView->GetResource((ID3D11Resource**)DepthTargetTexture.GetInitReference());

		D3D11_TEXTURE2D_DESC DTTDesc;
		DepthTargetTexture->GetDesc(&DTTDesc);
		RHISetViewport(0.0f, 0.0f, 0.0f, (float)DTTDesc.Width, (float)DTTDesc.Height, 1.0f);
	}
}

void FD3D11DynamicRHI::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{

	this->RHISetRenderTargets(RenderTargetsInfo.NumColorRenderTargets,
		RenderTargetsInfo.ColorRenderTarget,
		&RenderTargetsInfo.DepthStencilRenderTarget);
	
	if (RenderTargetsInfo.bClearColor || RenderTargetsInfo.bClearStencil || RenderTargetsInfo.bClearDepth)
	{
		FLinearColor ClearColors[MaxSimultaneousRenderTargets];
		float DepthClear = 0.0;
		uint32 StencilClear = 0;

		if (RenderTargetsInfo.bClearColor)
		{
			for (int32 i = 0; i < RenderTargetsInfo.NumColorRenderTargets; ++i)
			{
				if (RenderTargetsInfo.ColorRenderTarget[i].Texture != nullptr)
				{
					const FClearValueBinding& ClearValue = RenderTargetsInfo.ColorRenderTarget[i].Texture->GetClearBinding();
					checkf(ClearValue.ColorBinding == EClearBinding::EColorBound, TEXT("Texture: %s does not have a color bound for fast clears"), *RenderTargetsInfo.ColorRenderTarget[i].Texture->GetName().GetPlainNameString());
					ClearColors[i] = ClearValue.GetClearColor();
				}
			}
		}
		if (RenderTargetsInfo.bClearDepth || RenderTargetsInfo.bClearStencil)
		{
			const FClearValueBinding& ClearValue = RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetClearBinding();
			checkf(ClearValue.ColorBinding == EClearBinding::EDepthStencilBound, TEXT("Texture: %s does not have a DS value bound for fast clears"), *RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetName().GetPlainNameString());
			ClearValue.GetDepthStencil(DepthClear, StencilClear);
	}

		this->RHIClearMRTImpl(RenderTargetsInfo.bClearColor, RenderTargetsInfo.NumColorRenderTargets, ClearColors, RenderTargetsInfo.bClearDepth, DepthClear, RenderTargetsInfo.bClearStencil, StencilClear);
	}
}

// Primitive drawing.

static D3D11_PRIMITIVE_TOPOLOGY GetD3D11PrimitiveType(EPrimitiveType PrimitiveType, bool bUsingTessellation)
{
	if(bUsingTessellation)
	{
		switch(PrimitiveType)
		{
		case PT_1_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
		case PT_2_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST;

		// This is the case for tessellation without AEN or other buffers, so just flip to 3 CPs
		case PT_TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

		case PT_LineList:
		case PT_TriangleStrip:
		case PT_QuadList:
		case PT_PointList:
		case PT_RectList:
			UE_LOG(LogD3D11RHI, Fatal,TEXT("Invalid type specified for tessellated render, probably missing a case in FStaticMeshSceneProxy::GetMeshElement"));
			break;
		default:
			// Other cases are valid.
			break;
		};
	}

	switch(PrimitiveType)
	{
	case PT_TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case PT_TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	case PT_LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	case PT_PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;

	// ControlPointPatchList types will pretend to be TRIANGLELISTS with a stride of N 
	// (where N is the number of control points specified), so we can return them for
	// tessellation and non-tessellation. This functionality is only used when rendering a 
	// default material with something that claims to be tessellated, generally because the 
	// tessellation material failed to compile for some reason.
	case PT_3_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
	case PT_4_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
	case PT_5_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST;
	case PT_6_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST;
	case PT_7_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST;
	case PT_8_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST; 
	case PT_9_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST; 
	case PT_10_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST; 
	case PT_11_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST; 
	case PT_12_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST; 
	case PT_13_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST; 
	case PT_14_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST; 
	case PT_15_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST; 
	case PT_16_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST; 
	case PT_17_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST; 
	case PT_18_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST; 
	case PT_19_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST; 
	case PT_20_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST; 
	case PT_21_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST; 
	case PT_22_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST; 
	case PT_23_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST; 
	case PT_24_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST; 
	case PT_25_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST; 
	case PT_26_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST; 
	case PT_27_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST; 
	case PT_28_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST; 
	case PT_29_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST; 
	case PT_30_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST; 
	case PT_31_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST; 
	case PT_32_ControlPointPatchList: return D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST; 
	default: UE_LOG(LogD3D11RHI, Fatal,TEXT("Unknown primitive type: %u"),PrimitiveType);
	};

	return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}


void FD3D11DynamicRHI::CommitNonComputeShaderConstants()
{
	FD3D11BoundShaderState* CurrentBoundShaderState = (FD3D11BoundShaderState*)BoundShaderStateHistory.GetLast();
	check(CurrentBoundShaderState);

	// Only set the constant buffer if this shader needs the global constant buffer bound
	// Otherwise we will overwrite a different constant buffer
	if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Vertex])
	{
		// Commit and bind vertex shader constants
		for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
		{
			FD3D11ConstantBuffer* ConstantBuffer = VSConstantBuffers[i];
			FD3DRHIUtil::CommitConstants<SF_Vertex>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
		}
	}

	// Skip HS/DS CB updates in cases where tessellation isn't being used
	// Note that this is *potentially* unsafe because bDiscardSharedConstants is cleared at the
	// end of the function, however we're OK for now because bDiscardSharedConstants
	// is always reset whenever bUsingTessellation changes in SetBoundShaderState()
	if(bUsingTessellation)
	{
		if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Hull])
		{
			// Commit and bind hull shader constants
			for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
			{
				FD3D11ConstantBuffer* ConstantBuffer = HSConstantBuffers[i];
				FD3DRHIUtil::CommitConstants<SF_Hull>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
			}
		}

		if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Domain])
		{
			// Commit and bind domain shader constants
			for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
			{
				FD3D11ConstantBuffer* ConstantBuffer = DSConstantBuffers[i];
				FD3DRHIUtil::CommitConstants<SF_Domain>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
			}
		}
	}

	if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Geometry])
	{
		// Commit and bind geometry shader constants
		for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
		{
			FD3D11ConstantBuffer* ConstantBuffer = GSConstantBuffers[i];
			FD3DRHIUtil::CommitConstants<SF_Geometry>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
		}
	}

	if (CurrentBoundShaderState->bShaderNeedsGlobalConstantBuffer[SF_Pixel])
	{
		// Commit and bind pixel shader constants
		for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
		{
			FD3D11ConstantBuffer* ConstantBuffer = PSConstantBuffers[i];
			FD3DRHIUtil::CommitConstants<SF_Pixel>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
		}
	}

	bDiscardSharedConstants = false;
}

void FD3D11DynamicRHI::CommitComputeShaderConstants()
{
	bool bLocalDiscardSharedConstants = true;

	// Commit and bind compute shader constants
	for(uint32 i=0;i<MAX_CONSTANT_BUFFER_SLOTS; i++)
	{
		FD3D11ConstantBuffer* ConstantBuffer = CSConstantBuffers[i];
		FD3DRHIUtil::CommitConstants<SF_Compute>(ConstantBuffer, StateCache, i, bDiscardSharedConstants);
	}
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, uint32 BindIndex, FD3D11BaseShaderResource* RESTRICT ShaderResource, ID3D11ShaderResourceView* RESTRICT SRV, FName ResourceName = FName())
{
	// We set the resource through the RHI to track state for the purposes of unbinding SRVs when a UAV or RTV is bound.
	// todo: need to support SRV_Static for faster calls when possible
	D3D11RHI->SetShaderResourceView<Frequency>(ShaderResource, SRV, BindIndex, ResourceName,FD3D11StateCache::SRV_Unknown);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, uint32 BindIndex, ID3D11SamplerState* RESTRICT SamplerState)
{
	StateCache->SetSamplerState<Frequency>(SamplerState,BindIndex);
}

template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_Surface(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, FD3D11UniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex, FName LayoutName)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	const int32 NumResourcesInTable = Buffer->ResourceTable.Num();
	float CurrentTime = FApp::GetCurrentTime();
	int32 NumSetCalls = 0;
	uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FD3D11BaseShaderResource* ShaderResource = nullptr;
			ID3D11ShaderResourceView* D3D11Resource = nullptr;

			check(ResourceIndex < NumResourcesInTable);
			FRHITexture* TextureRHI = (FRHITexture*)Resources[ResourceIndex].GetReference();
			if (!TextureRHI)
			{
				UE_LOG(LogD3D11RHI, Fatal, TEXT("Null texture (resource %d bind %d) on UB Layout %s"), ResourceIndex, BindIndex, *LayoutName.ToString());
			}
			TextureRHI->SetLastRenderTime(CurrentTime);
			FD3D11TextureBase* TextureD3D11 = GetD3D11TextureFromRHITexture(TextureRHI);
			ShaderResource = TextureD3D11->GetBaseShaderResource();
			D3D11Resource = TextureD3D11->GetShaderResourceView();

			// todo: could coalesce adjacent bound resources.
			SetResource<ShaderFrequency>(D3D11RHI, StateCache, BindIndex, ShaderResource, D3D11Resource, TextureRHI->GetName());
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}
	return NumSetCalls;
}


template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBufferUAVPS(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, FD3D11UniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex, FName LayoutName)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	float CurrentTime = FApp::GetCurrentTime();
	int32 NumSetCalls = 0;
	uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FD3D11UnorderedAccessView* UnorderedAccessViewRHI = (FD3D11UnorderedAccessView*)Resources[ResourceIndex].GetReference();
			if (!UnorderedAccessViewRHI)
			{
				UE_LOG(LogD3D11RHI, Fatal, TEXT("Null UAV (resource %d bind %d) on UB Layout %s"), ResourceIndex, BindIndex, *LayoutName.ToString());
			}
			D3D11RHI->InternalSetUAVPS(BindIndex, UnorderedAccessViewRHI);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}
	return NumSetCalls;
}


template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_SRV(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, FD3D11UniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex, FName LayoutName)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	float CurrentTime = FApp::GetCurrentTime();
	int32 NumSetCalls = 0;
	uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FD3D11BaseShaderResource* ShaderResource = nullptr;
			ID3D11ShaderResourceView* D3D11Resource = nullptr;

			FD3D11ShaderResourceView* ShaderResourceViewRHI = (FD3D11ShaderResourceView*)Resources[ResourceIndex].GetReference();
			if (!ShaderResourceViewRHI)
			{
				UE_LOG(LogD3D11RHI, Fatal, TEXT("Null SRV (resource %d bind %d) on UB Layout %s"), ResourceIndex, BindIndex, *LayoutName.ToString());
			}
			ShaderResource = ShaderResourceViewRHI->Resource.GetReference();
			D3D11Resource = ShaderResourceViewRHI->View.GetReference();

			// todo: could coalesce adjacent bound resources.
			SetResource<ShaderFrequency>(D3D11RHI, StateCache, BindIndex, ShaderResource, D3D11Resource);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}
	return NumSetCalls;
}

template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_Sampler(FD3D11DynamicRHI* RESTRICT D3D11RHI, FD3D11StateCache* RESTRICT StateCache, FD3D11UniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	int32 NumSetCalls = 0;
	uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			ID3D11SamplerState* D3D11Resource = ((FD3D11SamplerState*)Resources[ResourceIndex].GetReference())->Resource.GetReference();

			// todo: could coalesce adjacent bound resources.
			SetResource<ShaderFrequency>(D3D11RHI, StateCache, BindIndex, D3D11Resource);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}
	return NumSetCalls;
}


template <class ShaderType>
void FD3D11DynamicRHI::SetResourcesFromTables(const ShaderType* RESTRICT Shader)
{
	checkSlow(Shader);

	// Mask the dirty bits by those buffers from which the shader has bound resources.
	uint32 DirtyBits = Shader->ShaderResourceTable.ResourceTableBits & DirtyUniformBuffers[ShaderType::StaticFrequency];
	while (DirtyBits)
	{
		// Scan for the lowest set bit, compute its index, clear it in the set of dirty bits.
		const uint32 LowestBitMask = (DirtyBits) & (-(int32)DirtyBits);
		const int32 BufferIndex = FMath::FloorLog2(LowestBitMask); // todo: This has a branch on zero, we know it could never be zero...
		DirtyBits ^= LowestBitMask;
		FD3D11UniformBuffer* Buffer = (FD3D11UniformBuffer*)BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex].GetReference();
		
		check(BufferIndex < Shader->ShaderResourceTable.ResourceTableLayoutHashes.Num());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!Buffer)
		{
			UE_LOG(LogD3D11RHI, Fatal, TEXT("Shader expected a uniform buffer of struct type %s at slot %u but got null instead.  Rendering code needs to set a valid uniform buffer for this slot."), 
				*Shader->UniformBuffers[BufferIndex].GetPlainNameString(),
				BufferIndex);
		}

		// to track down OR-7159 CRASH: Client crashed at start of match in D3D11Commands.cpp
		{
			const uint32 LayoutHash = Buffer->GetLayout().GetHash();

			if (LayoutHash != Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex])
			{
				auto& BufferLayout = Buffer->GetLayout();
				const auto& DebugName = BufferLayout.GetDebugName();
				const FString& ShaderName = Shader->ShaderName;
#if UE_BUILD_DEBUG
				FString ShaderUB;
				if (BufferIndex < Shader->UniformBuffers.Num())
				{
					ShaderUB = FString::Printf(TEXT("expecting UB '%s'"), *Shader->UniformBuffers[BufferIndex].GetPlainNameString());
				}
				UE_LOG(LogD3D11RHI, Error, TEXT("SetResourcesFromTables upcoming check(%08x != %08x); Bound Layout='%s' Shader='%s' %s"), BufferLayout.GetHash(), Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex], *DebugName, *ShaderName, *ShaderUB);
				FString ResourcesString;
				for (int32 Index = 0; Index < BufferLayout.Resources.Num(); ++Index)
				{
					ResourcesString += FString::Printf(TEXT("%d "), BufferLayout.Resources[Index].MemberType);
				}
				UE_LOG(LogD3D11RHI, Error, TEXT("Layout CB Size %d %d Resources: %s"), BufferLayout.ConstantBufferSize, BufferLayout.Resources.Num(), *ResourcesString);
#else
				UE_LOG(LogD3D11RHI, Error, TEXT("Bound Layout='%s' Shader='%s', Layout CB Size %d %d"), *DebugName, *ShaderName, BufferLayout.ConstantBufferSize, BufferLayout.Resources.Num());
#endif
				// this might mean you are accessing a data you haven't bound e.g. GBuffer
				checkf(BufferLayout.GetHash() == Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex],
					TEXT("Uniform buffer bound to slot %u is not what the shader expected:\n")
					TEXT("\tBound:    Uniform Buffer[%s] with Hash[%u]\n")
					TEXT("\tExpected: Uniform Buffer[%s] with Hash[%u]"),
					BufferIndex, *DebugName, BufferLayout.GetHash(), *Shader->UniformBuffers[BufferIndex].GetPlainNameString(), Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex]);
			}
		}
#endif

		FName LayoutName = *Buffer->GetLayout().GetDebugName();

		// todo: could make this two pass: gather then set
		SetShaderResourcesFromBuffer_Surface<(EShaderFrequency)ShaderType::StaticFrequency>(this, &StateCache, Buffer, Shader->ShaderResourceTable.TextureMap.GetData(), BufferIndex, LayoutName);
		SetShaderResourcesFromBuffer_SRV<(EShaderFrequency)ShaderType::StaticFrequency>(this, &StateCache, Buffer, Shader->ShaderResourceTable.ShaderResourceViewMap.GetData(), BufferIndex, LayoutName);
		SetShaderResourcesFromBuffer_Sampler<(EShaderFrequency)ShaderType::StaticFrequency>(this, &StateCache, Buffer, Shader->ShaderResourceTable.SamplerMap.GetData(), BufferIndex);
	}
	DirtyUniformBuffers[ShaderType::StaticFrequency] = 0;
}

template <class ShaderType>
int32 FD3D11DynamicRHI::SetUAVPSResourcesFromTables(const ShaderType* RESTRICT Shader, bool bForceInvalidate)
{
	checkSlow(Shader);
	int32 NumChanged = 0;
	// Mask the dirty bits by those buffers from which the shader has bound resources.
	uint16 DirtyMask = bForceInvalidate ? 0xffff : DirtyUniformBuffers[ShaderType::StaticFrequency];
	uint32 DirtyBits = Shader->ShaderResourceTable.ResourceTableBits & DirtyMask;
	while (DirtyBits)
	{
		// Scan for the lowest set bit, compute its index, clear it in the set of dirty bits.
		const uint32 LowestBitMask = (DirtyBits) & (-(int32)DirtyBits);
		const int32 BufferIndex = FMath::FloorLog2(LowestBitMask); // todo: This has a branch on zero, we know it could never be zero...
		DirtyBits ^= LowestBitMask;
		FD3D11UniformBuffer* Buffer = (FD3D11UniformBuffer*)BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex].GetReference();

		check(BufferIndex < Shader->ShaderResourceTable.ResourceTableLayoutHashes.Num());
		FName LayoutName = *Buffer->GetLayout().GetDebugName();

		if ((EShaderFrequency)ShaderType::StaticFrequency == SF_Pixel)
		{
			NumChanged += SetShaderResourcesFromBufferUAVPS<(EShaderFrequency)ShaderType::StaticFrequency>(this, &StateCache, Buffer, Shader->ShaderResourceTable.UnorderedAccessViewMap.GetData(), BufferIndex, LayoutName);
		}
	}
	return NumChanged;
}

static int32 PeriodicCheck = 0;

void FD3D11DynamicRHI::CommitGraphicsResourceTables()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	GDX11CommitGraphicsResourceTables.Increment();
#endif
	FD3D11BoundShaderState* RESTRICT CurrentBoundShaderState = (FD3D11BoundShaderState*)BoundShaderStateHistory.GetLast();
	check(CurrentBoundShaderState);
	auto* PixelShader = CurrentBoundShaderState->GetPixelShader();
	if(PixelShader)
	{
		//because d3d11 binding uses the same slots for UAVS and RTVS, we have to rebind, when two shaders with different sets of rendertargets are bound
		//as they can potentially be used by UAVS, which can cause them to unbind RTVs used by subsequent shaders.
		bool bRTVInvalidate = false;
		uint32 UAVMask = PixelShader->UAVMask & CurrentRTVOverlapMask;
		if (GDX11ReduceRTVRebinds &&
			(0 != ((~CurrentUAVMask) & UAVMask) && CurrentUAVMask == (CurrentUAVMask & UAVMask)))
		{
			//if the mask only -adds- uav binds, no RTs will be missing so we just grow the mask
			CurrentUAVMask = UAVMask;
		}
		else if (CurrentUAVMask != UAVMask)
		{
			bRTVInvalidate = true;
			CurrentUAVMask = UAVMask;
		}
		
		if(bRTVInvalidate)
		{
			CommitRenderTargets(true);
		}


		if(SetUAVPSResourcesFromTables(PixelShader, bRTVInvalidate) || UAVSChanged)
		{
			CommitUAVs();
		}
	}

	if (auto* Shader = CurrentBoundShaderState->GetVertexShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (PixelShader)
	{
		SetResourcesFromTables(PixelShader);
	}
	if (auto* Shader = CurrentBoundShaderState->GetHullShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (auto* Shader = CurrentBoundShaderState->GetDomainShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (auto* Shader = CurrentBoundShaderState->GetGeometryShader())
	{
		SetResourcesFromTables(Shader);
	}
}

void FD3D11DynamicRHI::CommitComputeResourceTables(FD3D11ComputeShader* InComputeShader)
{
	FD3D11ComputeShader* RESTRICT ComputeShader = InComputeShader;
	check(ComputeShader);
	SetResourcesFromTables(ComputeShader);
}

void FD3D11DynamicRHI::RHIDrawPrimitive(uint32 BaseVertexIndex,uint32 NumPrimitives,uint32 NumInstances)
{
	RHI_DRAW_CALL_STATS(PrimitiveType, FMath::Max(NumInstances, 1U) * NumPrimitives);

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	uint32 VertexCount = GetVertexCountForPrimitiveCount(NumPrimitives,PrimitiveType);

	GPUProfilingData.RegisterGPUWork(NumPrimitives * NumInstances, VertexCount * NumInstances);
	StateCache.SetPrimitiveTopology(GetD3D11PrimitiveType(PrimitiveType,bUsingTessellation));
	if(NumInstances > 1)
	{
		Direct3DDeviceIMContext->DrawInstanced(VertexCount,NumInstances,BaseVertexIndex,0);
	}
	else
	{
		Direct3DDeviceIMContext->Draw(VertexCount,BaseVertexIndex);
	}

	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	FD3D11VertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

	RHI_DRAW_CALL_INC();

	GPUProfilingData.RegisterGPUWork(0);

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	StateCache.SetPrimitiveTopology(GetD3D11PrimitiveType(PrimitiveType,bUsingTessellation));
	Direct3DDeviceIMContext->DrawInstancedIndirect(ArgumentBuffer->Resource,ArgumentOffset);

	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	FD3D11IndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	FD3D11StructuredBuffer* ArgumentsBuffer = ResourceCast(ArgumentsBufferRHI);

	RHI_DRAW_CALL_INC();

	GPUProfilingData.RegisterGPUWork(1);

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	// determine 16bit vs 32bit indices
	uint32 SizeFormat = sizeof(DXGI_FORMAT);
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);

	TrackResourceBoundAsIB(IndexBuffer);
	StateCache.SetIndexBuffer(IndexBuffer->Resource, Format, 0);
	StateCache.SetPrimitiveTopology(GetD3D11PrimitiveType(PrimitiveType,bUsingTessellation));

	if(NumInstances > 1)
	{
		Direct3DDeviceIMContext->DrawIndexedInstancedIndirect(ArgumentsBuffer->Resource, DrawArgumentsIndex * 5 * sizeof(uint32));
	}
	else
	{
		check(0);
	}

	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBufferRHI,int32 BaseVertexIndex,uint32 FirstInstance,uint32 NumVertices,uint32 StartIndex,uint32 NumPrimitives,uint32 NumInstances)
{
	RHI_DRAW_CALL_STATS(PrimitiveType, FMath::Max(NumInstances, 1U) * NumPrimitives);

	FD3D11IndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	// called should make sure the input is valid, this avoid hidden bugs
	ensure(NumPrimitives > 0);

	GPUProfilingData.RegisterGPUWork(NumPrimitives * NumInstances, NumVertices * NumInstances);

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	// determine 16bit vs 32bit indices
	uint32 SizeFormat = sizeof(DXGI_FORMAT);
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);

	uint32 IndexCount = GetVertexCountForPrimitiveCount(NumPrimitives,PrimitiveType);

	// Verify that we are not trying to read outside the index buffer range
	// test is an optimized version of: StartIndex + IndexCount <= IndexBuffer->GetSize() / IndexBuffer->GetStride() 
	checkf((StartIndex + IndexCount) * IndexBuffer->GetStride() <= IndexBuffer->GetSize(), 		
		TEXT("Start %u, Count %u, Type %u, Buffer Size %u, Buffer stride %u"), StartIndex, IndexCount, PrimitiveType, IndexBuffer->GetSize(), IndexBuffer->GetStride());

	TrackResourceBoundAsIB(IndexBuffer);
	StateCache.SetIndexBuffer(IndexBuffer->Resource, Format, 0);
	StateCache.SetPrimitiveTopology(GetD3D11PrimitiveType(PrimitiveType,bUsingTessellation));

	if (NumInstances > 1 || FirstInstance != 0)
	{
		const uint64 TotalIndexCount = (uint64)NumInstances * (uint64)IndexCount + (uint64)StartIndex;
		checkf(TotalIndexCount <= (uint64)0xFFFFFFFF, TEXT("Instanced Index Draw exceeds maximum d3d11 limit: Total: %llu, NumInstances: %llu, IndexCount: %llu, StartIndex: %llu, FirstInstance: %llu"), TotalIndexCount, NumInstances, IndexCount, StartIndex, FirstInstance);
		Direct3DDeviceIMContext->DrawIndexedInstanced(IndexCount, NumInstances, StartIndex, BaseVertexIndex, FirstInstance);
	}
	else
	{
		Direct3DDeviceIMContext->DrawIndexed(IndexCount,StartIndex,BaseVertexIndex);
	}

	ApplyUAVOverlapState();
}

void FD3D11DynamicRHI::RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	FD3D11IndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	FD3D11VertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

	RHI_DRAW_CALL_INC();

	GPUProfilingData.RegisterGPUWork(0);
	
	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();
	
	// Set the index buffer.
	const uint32 SizeFormat = sizeof(DXGI_FORMAT);
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);
	TrackResourceBoundAsIB(IndexBuffer);
	StateCache.SetIndexBuffer(IndexBuffer->Resource, Format, 0);
	StateCache.SetPrimitiveTopology(GetD3D11PrimitiveType(PrimitiveType,bUsingTessellation));
	Direct3DDeviceIMContext->DrawIndexedInstancedIndirect(ArgumentBuffer->Resource,ArgumentOffset);

	ApplyUAVOverlapState();
}

// Raster operations.
void FD3D11DynamicRHI::RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	RHIClearMRTImpl(bClearColor, NumClearColors, ClearColorArray, bClearDepth, Depth, bClearStencil, Stencil);
}

void FD3D11DynamicRHI::RHIClearMRTImpl(bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	FD3D11BoundRenderTargets BoundRenderTargets(Direct3DDeviceIMContext);

	// Must specify enough clear colors for all active RTs
	check(!bClearColor || NumClearColors >= BoundRenderTargets.GetNumActiveTargets());

	// If we're clearing depth or stencil and we have a readonly depth/stencil view bound, we need to use a writable depth/stencil view
	if (CurrentDepthTexture)
	{
		FExclusiveDepthStencil RequestedAccess;
		
		RequestedAccess.SetDepthStencilWrite(bClearDepth, bClearStencil);

		ensure(RequestedAccess.IsValid(CurrentDSVAccessType));
	}

	ID3D11DepthStencilView* DepthStencilView = BoundRenderTargets.GetDepthStencilView();

	if (bClearColor && BoundRenderTargets.GetNumActiveTargets() > 0)
	{
		for (int32 TargetIndex = 0; TargetIndex < BoundRenderTargets.GetNumActiveTargets(); TargetIndex++)
		{				
			ID3D11RenderTargetView* RenderTargetView = BoundRenderTargets.GetRenderTargetView(TargetIndex);
			if (RenderTargetView != nullptr)
			{
				Direct3DDeviceIMContext->ClearRenderTargetView(RenderTargetView, (float*)&ClearColorArray[TargetIndex]);
			}
		}
	}

	if ((bClearDepth || bClearStencil) && DepthStencilView)
	{
		uint32 ClearFlags = 0;
		if (bClearDepth)
		{
			ClearFlags |= D3D11_CLEAR_DEPTH;
		}
		if (bClearStencil)
		{
			ClearFlags |= D3D11_CLEAR_STENCIL;
		}
		Direct3DDeviceIMContext->ClearDepthStencilView(DepthStencilView,ClearFlags,Depth,Stencil);
	}

	GPUProfilingData.RegisterGPUWork(0);
}

void FD3D11DynamicRHI::RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil)
{
	// Not necessary for d3d.
}

// Blocks the CPU until the GPU catches up and goes idle.
void FD3D11DynamicRHI::RHIBlockUntilGPUIdle()
{
	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
	
	D3D11_QUERY_DESC Desc = {};
	Desc.Query = D3D11_QUERY_EVENT;

	TRefCountPtr<ID3D11Query> Query;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateQuery(&Desc, Query.GetInitReference()), Direct3DDevice);
	
	D3D11StallRHIThread();
	
	Direct3DDeviceIMContext->End(Query.GetReference());
	Direct3DDeviceIMContext->Flush();

	for(;;)
	{
		BOOL EventComplete = false;
		Direct3DDeviceIMContext->GetData(Query.GetReference(), &EventComplete, sizeof(EventComplete), 0);
		if (EventComplete)
		{
			break;
		}
		else
		{
			FPlatformProcess::Sleep(0.005f);
		}
	}

	D3D11UnstallRHIThread();
}

/**
 * Returns the total GPU time taken to render the last frame. Same metric as FPlatformTime::Cycles().
 */
uint32 FD3D11DynamicRHI::RHIGetGPUFrameCycles(uint32 GPUIndex)
{
#if INTEL_METRICSDISCOVERY
	if (GDX11IntelMetricsDiscoveryEnabled)
	{
		return IntelMetricsDicoveryGetGPUTime();
	}
#endif // INTEL_METRICSDISCOVERY
	return GGPUFrameTime;
}

void FD3D11DynamicRHI::RHIExecuteCommandList(FRHICommandList* CmdList)
{
	check(0); // this path has gone stale and needs updated methods, starting at ERCT_SetScissorRect
}

// NVIDIA Depth Bounds Test interface
void FD3D11DynamicRHI::EnableDepthBoundsTest(bool bEnable,float MinDepth,float MaxDepth)
{
#if PLATFORM_DESKTOP
	if(MinDepth > MaxDepth)
	{
		UE_LOG(LogD3D11RHI, Error,TEXT("RHIEnableDepthBoundsTest(%i,%f, %f) MinDepth > MaxDepth, cannot set DBT."),bEnable,MinDepth,MaxDepth);
		return;
	}

	if( MinDepth < 0.f || MaxDepth > 1.f)
	{
		UE_LOG(LogD3D11RHI, Verbose,TEXT("RHIEnableDepthBoundsTest(%i,%f, %f) depths out of range, will clamp."),bEnable,MinDepth,MaxDepth);
	}

	MinDepth = FMath::Clamp(MinDepth, 0.0f, 1.0f);
	MaxDepth = FMath::Clamp(MaxDepth, 0.0f, 1.0f);

	if (IsRHIDeviceNVIDIA())
	{
		auto Result = NvAPI_D3D11_SetDepthBoundsTest( Direct3DDevice, bEnable, MinDepth, MaxDepth );
		if (Result != NVAPI_OK)
		{
			static bool bOnce = false;
			if (!bOnce)
			{
				bOnce = true;
				if (bRenderDoc)
				{
					if (FApp::IsUnattended())
					{
						UE_LOG(LogD3D11RHI, Display, TEXT("NvAPI is not available under RenderDoc"));
					}
					else
					{
						UE_LOG(LogD3D11RHI, Warning, TEXT("NvAPI is not available under RenderDoc"));
					}
				}
				else
				{
					UE_LOG(LogD3D11RHI, Error, TEXT("NvAPI_D3D11_SetDepthBoundsTest(%i,%f, %f) returned error code %i. **********PLEASE UPDATE YOUR VIDEO DRIVERS*********"), bEnable, MinDepth, MaxDepth, (unsigned int)Result);
				}
			}
		}
	}
	else if (IsRHIDeviceAMD())
	{
		auto Result = agsDriverExtensionsDX11_SetDepthBounds(AmdAgsContext, Direct3DDeviceIMContext, bEnable, MinDepth, MaxDepth);
		if(Result != AGS_SUCCESS)
		{
			static bool bOnce = false;
			if (!bOnce)
			{
				bOnce = true;
				if (bRenderDoc)
				{
					if (FApp::IsUnattended())
					{
						UE_LOG(LogD3D11RHI, Display, TEXT("AGS is not available under RenderDoc"));
					}
					else
					{
						UE_LOG(LogD3D11RHI, Warning, TEXT("AGS is not available under RenderDoc"));
					}
				}
				else
				{
					UE_LOG(LogD3D11RHI, Error, TEXT("agsDriverExtensionsDX11_SetDepthBounds(%i,%f, %f) returned error code %i. **********PLEASE UPDATE YOUR VIDEO DRIVERS*********"), bEnable, MinDepth, MaxDepth, (unsigned int)Result);
				}
			}
		}
	}
#endif

	StateCache.bDepthBoundsEnabled = bEnable;
	StateCache.DepthBoundsMin = MinDepth;
	StateCache.DepthBoundsMax = MaxDepth;
}

void FD3D11DynamicRHI::RHISubmitCommandsHint()
{

}
IRHICommandContext* FD3D11DynamicRHI::RHIGetDefaultContext()
{
	return this;
}

IRHICommandContextContainer* FD3D11DynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	return nullptr;
}

void FD3D11DynamicRHI::RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
{
	static IConsoleVariable* CVarShowTransitions = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowTransitions"));
	bool bShowTransitionEvents = CVarShowTransitions->GetInt() != 0;

	SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResources, bShowTransitionEvents, TEXT("TransitionTo: %s: %i Textures"), *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)TransitionType], NumTextures);
	for (int32 i = 0; i < NumTextures; ++i)
	{				
		FRHITexture* RenderTarget = InTextures[i];
		if (RenderTarget)
		{
			SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResourcesLoop, bShowTransitionEvents, TEXT("To:%i - %s"), i, *RenderTarget->GetName().ToString());

			FD3D11BaseShaderResource* Resource = nullptr;
			FD3D11Texture2D* SourceTexture2D = static_cast<FD3D11Texture2D*>(RenderTarget->GetTexture2D());
			if (SourceTexture2D)
			{
				Resource = SourceTexture2D;
			}
			FD3D11Texture2DArray* SourceTexture2DArray = static_cast<FD3D11Texture2DArray*>(RenderTarget->GetTexture2DArray());
			if (SourceTexture2DArray)
			{
				Resource = SourceTexture2DArray;
			}
			FD3D11TextureCube* SourceTextureCube = static_cast<FD3D11TextureCube*>(RenderTarget->GetTextureCube());
			if (SourceTextureCube)
			{
				Resource = SourceTextureCube;
			}
			FD3D11Texture3D* SourceTexture3D = static_cast<FD3D11Texture3D*>(RenderTarget->GetTexture3D());
			if (SourceTexture3D)
			{
				Resource = SourceTexture3D;
			}
			DUMP_TRANSITION(RenderTarget->GetName(), TransitionType);
			Resource->SetCurrentGPUAccess(TransitionType);
		}
	}
}

void FD3D11DynamicRHI::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 InNumUAVs, FRHIComputeFence* WriteFence)
{
	for (int32 i = 0; i < InNumUAVs; ++i)
	{
		if (InUAVs[i])
		{
			FD3D11UnorderedAccessView* UAV = ResourceCast(InUAVs[i]);
			if (UAV && UAV->Resource)
			{
				UAV->Resource->SetCurrentGPUAccess(TransitionType);
				if (TransitionType != EResourceTransitionAccess::ERWNoBarrier)
				{
					UAV->Resource->SetDirty(false, PresentCounter);
				}
			}
		}
	}

	if (WriteFence)
	{
		WriteFence->WriteFence();
	}
}

static TAutoConsoleVariable<int32> CVarAllowUAVFlushExt(
	TEXT("r.D3D11.AutoFlushUAV"),
	1,
	TEXT("If enabled, use NVAPI (Nvidia), AGS (AMD) or Intel Extensions (Intel) to not flush between dispatches/draw calls")
	TEXT(" 1: on (default)\n")
	TEXT(" 0: off"),
	ECVF_RenderThreadSafe);

// Enable this to test if NvAPI/AGS/Intel returned an error during UAV overlap enabling/disabling.
// By default we do not test because this is an optimalisation (if overlapping is not enabled, GPU execution is slower)
// Enable it here to validate if overlapping is actually used.
#if 0
	#define CHECK_AGS(x)   do { AGSReturnCode err = (x); check(err == AGS_SUCCESS); } while(false)
	#define CHECK_NVAPI(x) do { NvAPI_Status  err = (x); check(err == NVAPI_OK);    } while(false)
	#define CHECK_INTEL(x) do { HRESULT       err = (x); check(err == S_OK);        } while(false)
#else
	#define CHECK_AGS(x)   do { (x); } while(false)
	#define CHECK_NVAPI(x) do { (x); } while(false)
	#define CHECK_INTEL(x) do { (x); } while(false)
#endif

bool FD3D11DynamicRHI::IsUAVOverlapSupported()
{
	return IsRHIDeviceNVIDIA() || IsRHIDeviceAMD() || IsRHIDeviceIntel();
}

void FD3D11DynamicRHI::ApplyUAVOverlapState()
{
	if (UAVOverlapState != EUAVOverlapState::EPending)
	{
		return;
	}

	UAVOverlapState = EUAVOverlapState::EOn;

#if !PLATFORM_HOLOLENS
	if (IsRHIDeviceNVIDIA())
	{
		CHECK_NVAPI(NvAPI_D3D11_BeginUAVOverlap(Direct3DDevice));
	}
	else if (IsRHIDeviceAMD())
	{
		CHECK_AGS(agsDriverExtensionsDX11_BeginUAVOverlap(AmdAgsContext, Direct3DDeviceIMContext));
	}
	else if (IsRHIDeviceIntel())
	{
#if INTEL_EXTENSIONS
		if (IntelD3D11ExtensionFuncs && IntelD3D11ExtensionFuncs->D3D11BeginUAVOverlap)
		{
			CHECK_INTEL(IntelD3D11ExtensionFuncs->D3D11BeginUAVOverlap(IntelExtensionContext));
		}
#endif
	}
	else
	{
		ensureMsgf(false, TEXT("BeginUAVOverlap not implemented for this GPU IHV."));
	}
#endif
}

void FD3D11DynamicRHI::RHIBeginUAVOverlap()
{
	checkf(UAVOverlapState == EUAVOverlapState::EOff, TEXT("Mismatched call to BeginUAVOverlap. Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap()."));

	bUAVOverlapAllowed = (CVarAllowUAVFlushExt.GetValueOnRenderThread() != 0) && IsUAVOverlapSupported();
	if (!bUAVOverlapAllowed)
	{
		return;
	}

	// The driver APIs just set an internal flag which is used by the next dispatch in order to determine if there needs to be a barrier before
	// running the CS. This means that we need to call the API *after* the next dispatch, because we always want a barrier before the first
	// dispatch in an overlap group. Consider this example:
	//
	//		// Dispatches 1 and 2 are independent, we want them to overlap.
	//		RHICmdList.BeginUAVOverlap();
	//		Dispatch1();
	//		Dispatch2();
	//		RHICmdList.EndUAVOverlap();
	//
	//		// Dispatches 3 and 4 have no inter-dependencies, so they should overlap, but read UAV locations written by 1 and/or 2, so we want a barrier here.
	//		RHICmdList.BeginUAVOverlap();
	//		Dispatch3();
	//		Dispatch4();
	//		RHICmdList.EndUAVOverlap();
	//
	// If we just call the driver extension API immediately here, it will simply overwrite the flag set by the previous end call, and all 4 dispatches will
	// (potentially) overlap, as if the inner end/begin pair didn't exist, producing incorrect results. Instead, we must set the state to pending here,
	// and the next RHI draw or dispatch function will call the API after running the draw/dispatch, so the example above results in this actual set of driver calls:
	//
	//		Dispatch1();				// Internal barrier before, makes sure we don't overlap any earlier dispatches.
	//		Vendor_BeginUAVOverlap();	// Set overlap flag to true.
	//		Dispatch2();				// No barrier, overlaps 1.
	//		Vendor_EndUAVOverlap();		// Set overlap flag to false.
	//
	//		Dispatch3();				// Barrier before, makes sure we don't overlap 2.
	//		Vendor_BeginUAVOverlap();	// Set overlap flag to true.
	//		Dispatch4();				// No barrier, overlaps 3.
	//		Vendor_EndUAVOverlap();		// Set overlap flag to false, any dispatches after this will not overlap.
	//
	// This correctly serializes dispatches 2 and 3.
	UAVOverlapState = EUAVOverlapState::EPending;
}

void FD3D11DynamicRHI::RHIEndUAVOverlap()
{
	if (!bUAVOverlapAllowed)
	{
		return;
	}

	checkf(UAVOverlapState != EUAVOverlapState::EOff, TEXT("Mismatched call to EndUAVOverlap. Ensure all calls to RHICmdList.BeginUAVOverlap() are paired with a call to RHICmdList.EndUAVOverlap()."));

	// Only call the driver API if we got a dispatch in between the call to RHIBeginUAVOverlap() and this call to RHIEndUAVOverlap(). Otherwise it's an
	// empty overlap group and we can simply cancel the request.
	if (UAVOverlapState == EUAVOverlapState::EOn)
	{
#if !PLATFORM_HOLOLENS
		if (IsRHIDeviceNVIDIA())
		{
			CHECK_NVAPI(NvAPI_D3D11_EndUAVOverlap(Direct3DDevice));
		}
		else if (IsRHIDeviceAMD())
		{
			CHECK_AGS(agsDriverExtensionsDX11_EndUAVOverlap(AmdAgsContext, Direct3DDeviceIMContext));
		}
		else if (IsRHIDeviceIntel())
		{
#if INTEL_EXTENSIONS
			if (IntelD3D11ExtensionFuncs && IntelD3D11ExtensionFuncs->D3D11EndUAVOverlap)
			{
				CHECK_INTEL(IntelD3D11ExtensionFuncs->D3D11EndUAVOverlap(IntelExtensionContext));
			}
#endif
		}
		else
		{
			ensureMsgf(false, TEXT("EndUAVOverlap not implemented for this GPU IHV."));
		}
#endif	
	}

	UAVOverlapState = EUAVOverlapState::EOff;
}

void FD3D11DynamicRHI::RHIAutomaticCacheFlushAfterComputeShader(bool bEnable)
{
	if (bEnable)
	{
		if (UAVOverlapState != EUAVOverlapState::EOff)
		{
			RHIEndUAVOverlap();
		}
	}
	else
	{
		if (UAVOverlapState == EUAVOverlapState::EOff)
		{
			RHIBeginUAVOverlap();
		}
	}
}

void FD3D11DynamicRHI::RHIFlushComputeShaderCache()
{
	if (UAVOverlapState != EUAVOverlapState::EOff)
	{
		RHIEndUAVOverlap();
		RHIBeginUAVOverlap();
	}
}

//*********************** StagingBuffer Implementation ***********************//

FStagingBufferRHIRef FD3D11DynamicRHI::RHICreateStagingBuffer()
{
	return new FD3D11StagingBuffer();
}

void FD3D11DynamicRHI::RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* StagingBufferRHI, uint32 Offset, uint32 NumBytes)
{
	FD3D11VertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
	FD3D11StagingBuffer* StagingBuffer = ResourceCast(StagingBufferRHI);
	if (StagingBuffer)
	{
		ensureMsgf(!StagingBuffer->bIsLocked, TEXT("Attempting to Copy to a locked staging buffer. This may have undefined behavior"));
		if (SourceBuffer)
		{
			ensureMsgf((SourceBufferRHI->GetUsage() & BUF_SourceCopy) != 0, TEXT("Buffers used as copy source need to be created with BUF_SourceCopy"));

			if (!StagingBuffer->StagedRead || StagingBuffer->ShadowBufferSize < NumBytes)
			{
				// Free previously allocated buffer.
				if (StagingBuffer->StagedRead)
				{
					StagingBuffer->StagedRead.SafeRelease();
				}

				// Allocate a new one with enough space.
				// @todo-mattc I feel like we should allocate more than NumBytes to handle small reads without blowing tons of space. Need to pool this.
				D3D11_BUFFER_DESC StagedReadDesc;
				ZeroMemory(&StagedReadDesc, sizeof(D3D11_BUFFER_DESC));
				StagedReadDesc.ByteWidth = NumBytes;
				StagedReadDesc.Usage = D3D11_USAGE_STAGING;
				StagedReadDesc.BindFlags = 0;
				StagedReadDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				StagedReadDesc.MiscFlags = 0;
				TRefCountPtr<ID3D11Buffer> StagingVertexBuffer;
				VERIFYD3D11RESULT_EX(Direct3DDevice->CreateBuffer(&StagedReadDesc, NULL, StagingBuffer->StagedRead.GetInitReference()), Direct3DDevice);
				StagingBuffer->ShadowBufferSize = NumBytes;
				StagingBuffer->Context = Direct3DDeviceIMContext;
			}

			// Copy the contents of the vertex buffer to the staging buffer.
			D3D11_BOX SourceBox;
			SourceBox.left = Offset;
			SourceBox.right = NumBytes;
			SourceBox.top = SourceBox.front = 0;
			SourceBox.bottom = SourceBox.back = 1;
			Direct3DDeviceIMContext->CopySubresourceRegion(StagingBuffer->StagedRead, 0, 0, 0, 0, SourceBuffer->Resource, 0, &SourceBox);
		}
	}
}

void FD3D11DynamicRHI::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	// @todo-staging Implement real fences for D3D11
	// D3D11 only has the generic fence for now.
	FGenericRHIGPUFence* Fence = ResourceCast(FenceRHI);
	check(Fence);
	Fence->WriteInternal();
}

void* FD3D11DynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(StagingBufferRHI);
	FD3D11StagingBuffer* StagingBuffer = ResourceCast(StagingBufferRHI);
	return StagingBuffer->Lock(Offset, SizeRHI);
}

void FD3D11DynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI)
{
	FD3D11StagingBuffer* StagingBuffer = ResourceCast(StagingBufferRHI);
	StagingBuffer->Unlock();
}
