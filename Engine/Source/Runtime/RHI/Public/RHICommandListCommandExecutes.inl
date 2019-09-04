// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHICommandListCommandExecutes.inl: RHI Command List execute functions.
=============================================================================*/

#if !defined(INTERNAL_DECORATOR)
	#define INTERNAL_DECORATOR(Method) CmdList.GetContext().Method
#endif

//for functions where the signatures do not match between gfx and compute commandlists
#if !defined(INTERNAL_DECORATOR_COMPUTE)
#define INTERNAL_DECORATOR_COMPUTE(Method) CmdList.GetComputeContext().Method
#endif

//for functions where the signatures match between gfx and compute commandlists
#if !defined(INTERNAL_DECORATOR_CONTEXT_PARAM1)
#define INTERNAL_DECORATOR_CONTEXT(Method) IRHIComputeContext& Context = (CmdListType == ECmdList::EGfx) ? CmdList.GetContext() : CmdList.GetComputeContext(); Context.Method
#endif

class FRHICommandListBase;
class IRHIComputeContext;
struct FComputedBSS;
struct FComputedGraphicsPipelineState;
struct FComputedUniformBuffer;
struct FMemory;
struct FRHICommandAutomaticCacheFlushAfterComputeShader;
struct FRHICommandBeginDrawingViewport;
struct FRHICommandBeginFrame;
struct FRHICommandBeginOcclusionQueryBatch;
struct FRHICommandBeginRenderQuery;
struct FRHICommandBeginScene;
struct FRHICommandBindClearMRTValues;
struct FRHICommandBuildLocalBoundShaderState;
struct FRHICommandBuildLocalGraphicsPipelineState;
struct FRHICommandBuildLocalUniformBuffer;
struct FRHICommandClearUAV;
struct FRHICommandCopyToResolveTarget;
struct FRHICommandDrawIndexedIndirect;
struct FRHICommandDrawIndexedPrimitive;
struct FRHICommandDrawIndexedPrimitiveIndirect;
struct FRHICommandDrawPrimitive;
struct FRHICommandDrawPrimitiveIndirect;
struct FRHICommandSetDepthBounds;
struct FRHICommandEndDrawingViewport;
struct FRHICommandEndFrame;
struct FRHICommandEndOcclusionQueryBatch;
struct FRHICommandEndRenderQuery;
struct FRHICommandEndScene;
struct FRHICommandFlushComputeShaderCache;
struct FRHICommandSetBlendFactor;
struct FRHICommandSetBoundShaderState;
struct FRHICommandSetLocalGraphicsPipelineState;
struct FRHICommandSetRasterizerState;
struct FRHICommandSetRenderTargets;
struct FRHICommandSetRenderTargetsAndClear;
struct FRHICommandSetScissorRect;
struct FRHICommandSetStencilRef;
struct FRHICommandSetStereoViewport;
struct FRHICommandSetStreamSource;
struct FRHICommandSetViewport;
struct FRHICommandTransitionTextures;
struct FRHICommandTransitionTexturesArray;
struct FRHICommandUpdateTextureReference;
struct FRHICommandBuildAccelerationStructure;
struct FRHICommandClearRayTracingBindings;
struct FRHICommandRayTraceOcclusion;
struct FRHICommandRayTraceIntersection;
struct FRHICommandRayTraceDispatch;
struct FRHICommandSetRayTracingBindings;

enum class ECmdList;
template <typename TRHIShader> struct FRHICommandSetLocalUniformBuffer;

void FRHICommandBeginUpdateMultiFrameResource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginUpdateMultiFrameResource);
	INTERNAL_DECORATOR(RHIBeginUpdateMultiFrameResource)(Texture);
}

void FRHICommandEndUpdateMultiFrameResource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndUpdateMultiFrameResource);
	INTERNAL_DECORATOR(RHIEndUpdateMultiFrameResource)(Texture);
}

void FRHICommandBeginUpdateMultiFrameUAV::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginUpdateMultiFrameUAV);
	INTERNAL_DECORATOR(RHIBeginUpdateMultiFrameResource)(UAV);
}

void FRHICommandEndUpdateMultiFrameUAV::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndUpdateMultiFrameUAV);
	INTERNAL_DECORATOR(RHIEndUpdateMultiFrameResource)(UAV);
}

void FRHICommandSetStencilRef::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStencilRef);
	INTERNAL_DECORATOR(RHISetStencilRef)(StencilRef);
}

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetShaderParameter<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderParameter);
	INTERNAL_DECORATOR(RHISetShaderParameter)(Shader, BufferIndex, BaseIndex, NumBytes, NewValue); 
}
template struct FRHICommandSetShaderParameter<FRHIVertexShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderParameter<FRHIHullShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderParameter<FRHIDomainShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderParameter<FRHIGeometryShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderParameter<FRHIPixelShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderParameter<FRHIComputeShader, ECmdList::EGfx>;
template<> void FRHICommandSetShaderParameter<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderParameter)(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
}

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetShaderUniformBuffer<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderUniformBuffer);
	INTERNAL_DECORATOR(RHISetShaderUniformBuffer)(Shader, BaseIndex, UniformBuffer);
}
template struct FRHICommandSetShaderUniformBuffer<FRHIVertexShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderUniformBuffer<FRHIHullShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderUniformBuffer<FRHIDomainShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderUniformBuffer<FRHIGeometryShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderUniformBuffer<FRHIPixelShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderUniformBuffer<FRHIComputeShader, ECmdList::EGfx>;
template<> void FRHICommandSetShaderUniformBuffer<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderUniformBuffer);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderUniformBuffer)(Shader, BaseIndex, UniformBuffer);
}

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetShaderTexture<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderTexture);
	INTERNAL_DECORATOR(RHISetShaderTexture)(Shader, TextureIndex, Texture);
}
template struct FRHICommandSetShaderTexture<FRHIVertexShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderTexture<FRHIHullShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderTexture<FRHIDomainShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderTexture<FRHIGeometryShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderTexture<FRHIPixelShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderTexture<FRHIComputeShader, ECmdList::EGfx>;
template<> void FRHICommandSetShaderTexture<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderTexture);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderTexture)(Shader, TextureIndex, Texture);
}

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetShaderResourceViewParameter<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderResourceViewParameter);
	INTERNAL_DECORATOR(RHISetShaderResourceViewParameter)(Shader, SamplerIndex, SRV);
}
template struct FRHICommandSetShaderResourceViewParameter<FRHIVertexShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderResourceViewParameter<FRHIHullShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderResourceViewParameter<FRHIDomainShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderResourceViewParameter<FRHIGeometryShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderResourceViewParameter<FRHIPixelShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderResourceViewParameter<FRHIComputeShader, ECmdList::EGfx>;
template<> void FRHICommandSetShaderResourceViewParameter<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderResourceViewParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderResourceViewParameter)(Shader, SamplerIndex, SRV);
}

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetUAVParameter<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetUAVParameter);
	INTERNAL_DECORATOR_CONTEXT(RHISetUAVParameter)(Shader, UAVIndex, UAV);
}
template struct FRHICommandSetUAVParameter<FRHIComputeShader, ECmdList::EGfx>;
template struct FRHICommandSetUAVParameter<FRHIComputeShader, ECmdList::ECompute>;

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetUAVParameter_IntialCount<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetUAVParameter);
	INTERNAL_DECORATOR_CONTEXT(RHISetUAVParameter)(Shader, UAVIndex, UAV, InitialCount);
}
template struct FRHICommandSetUAVParameter_IntialCount<FRHIComputeShader, ECmdList::EGfx>;
template struct FRHICommandSetUAVParameter_IntialCount<FRHIComputeShader, ECmdList::ECompute>;

template <typename TRHIShader, ECmdList CmdListType>
void FRHICommandSetShaderSampler<TRHIShader, CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderSampler);
	INTERNAL_DECORATOR(RHISetShaderSampler)(Shader, SamplerIndex, Sampler);
}
template struct FRHICommandSetShaderSampler<FRHIVertexShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderSampler<FRHIHullShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderSampler<FRHIDomainShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderSampler<FRHIGeometryShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderSampler<FRHIPixelShader, ECmdList::EGfx>;
template struct FRHICommandSetShaderSampler<FRHIComputeShader, ECmdList::EGfx>;
template<> void FRHICommandSetShaderSampler<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderSampler);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderSampler)(Shader, SamplerIndex, Sampler);
}

void FRHICommandDrawPrimitive::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawPrimitive);
	INTERNAL_DECORATOR(RHIDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
}

void FRHICommandDrawIndexedPrimitive::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedPrimitive);
	INTERNAL_DECORATOR(RHIDrawIndexedPrimitive)(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
}

void FRHICommandSetBlendFactor::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetBlendFactor);
	INTERNAL_DECORATOR(RHISetBlendFactor)(BlendFactor);
}

void FRHICommandSetStreamSource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStreamSource);
	INTERNAL_DECORATOR(RHISetStreamSource)(StreamIndex, VertexBuffer, Offset);
}

void FRHICommandSetViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetViewport);
	INTERNAL_DECORATOR(RHISetViewport)(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
}

void FRHICommandSetStereoViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStereoViewport);
	INTERNAL_DECORATOR(RHISetStereoViewport)(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
}

void FRHICommandSetScissorRect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetScissorRect);
	INTERNAL_DECORATOR(RHISetScissorRect)(bEnable, MinX, MinY, MaxX, MaxY);
}

void FRHICommandBeginRenderPass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginRenderPass);
	INTERNAL_DECORATOR(RHIBeginRenderPass)(Info, Name);
}

void FRHICommandEndRenderPass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndRenderPass);
	INTERNAL_DECORATOR(RHIEndRenderPass)();
}

void FRHICommandNextSubpass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(NextSubpass);
	INTERNAL_DECORATOR(RHINextSubpass)();
}

void FRHICommandBeginComputePass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginComputePass);
	INTERNAL_DECORATOR(RHIBeginComputePass)(Name);
}

void FRHICommandEndComputePass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndComputePass);
	INTERNAL_DECORATOR(RHIEndComputePass)();
}

void FRHICommandSetRenderTargets::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetRenderTargets);
	INTERNAL_DECORATOR(RHISetRenderTargets)(
		NewNumSimultaneousRenderTargets,
		NewRenderTargetsRHI,
		&NewDepthStencilTarget,
		NewNumUAVs,
		UAVs);
}

void FRHICommandBindClearMRTValues::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BindClearMRTValues);
	INTERNAL_DECORATOR(RHIBindClearMRTValues)(
		bClearColor,
		bClearDepth,
		bClearStencil		
		);
}

template<ECmdList CmdListType>
void FRHICommandSetComputeShader<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetComputeShader);
	INTERNAL_DECORATOR_CONTEXT(RHISetComputeShader)(ComputeShader);
}
template struct FRHICommandSetComputeShader<ECmdList::EGfx>;
template struct FRHICommandSetComputeShader<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandSetComputePipelineState<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetComputePipelineState);
	extern FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
	FRHIComputePipelineState* RHIComputePipelineState = ExecuteSetComputePipelineState(ComputePipelineState);
	INTERNAL_DECORATOR_CONTEXT(RHISetComputePipelineState)(RHIComputePipelineState);
}
template struct FRHICommandSetComputePipelineState<ECmdList::EGfx>;
template struct FRHICommandSetComputePipelineState<ECmdList::ECompute>;

void FRHICommandSetGraphicsPipelineState::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetGraphicsPipelineState);
	extern FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(FGraphicsPipelineState* GraphicsPipelineState);
	FRHIGraphicsPipelineState* RHIGraphicsPipelineState = ExecuteSetGraphicsPipelineState(GraphicsPipelineState);
	INTERNAL_DECORATOR(RHISetGraphicsPipelineState)(RHIGraphicsPipelineState);
}

template<ECmdList CmdListType>
void FRHICommandDispatchComputeShader<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DispatchComputeShader);
	INTERNAL_DECORATOR_CONTEXT(RHIDispatchComputeShader)(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
template struct FRHICommandDispatchComputeShader<ECmdList::EGfx>;
template struct FRHICommandDispatchComputeShader<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandDispatchIndirectComputeShader<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DispatchIndirectComputeShader);
	INTERNAL_DECORATOR_CONTEXT(RHIDispatchIndirectComputeShader)(ArgumentBuffer, ArgumentOffset);
}
template struct FRHICommandDispatchIndirectComputeShader<ECmdList::EGfx>;
template struct FRHICommandDispatchIndirectComputeShader<ECmdList::ECompute>;

void FRHICommandAutomaticCacheFlushAfterComputeShader::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(AutomaticCacheFlushAfterComputeShader);
	INTERNAL_DECORATOR(RHIAutomaticCacheFlushAfterComputeShader)(bEnable);
}

void FRHICommandFlushComputeShaderCache::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(FlushComputeShaderCache);
	INTERNAL_DECORATOR(RHIFlushComputeShaderCache)();
}

void FRHICommandDrawPrimitiveIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawPrimitiveIndirect);
	INTERNAL_DECORATOR(RHIDrawPrimitiveIndirect)(ArgumentBuffer, ArgumentOffset);
}

void FRHICommandDrawIndexedIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedIndirect);
	INTERNAL_DECORATOR(RHIDrawIndexedIndirect)(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
}

void FRHICommandDrawIndexedPrimitiveIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedPrimitiveIndirect);
	INTERNAL_DECORATOR(RHIDrawIndexedPrimitiveIndirect)(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
}

void FRHICommandSetDepthBounds::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EnableDepthBoundsTest);
	INTERNAL_DECORATOR(RHISetDepthBounds)(MinDepth, MaxDepth);
}

void FRHICommandClearTinyUAV::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ClearTinyUAV);
	INTERNAL_DECORATOR(RHIClearTinyUAV)(UnorderedAccessViewRHI, Values);
}

void FRHICommandCopyToResolveTarget::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(CopyToResolveTarget);
	INTERNAL_DECORATOR(RHICopyToResolveTarget)(SourceTexture, DestTexture, ResolveParams);
}

void FRHICommandCopyTexture::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(CopyTexture);
	INTERNAL_DECORATOR(RHICopyTexture)(SourceTexture, DestTexture, CopyInfo);
}

void FRHICommandTransitionTextures::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionTextures);
	INTERNAL_DECORATOR(RHITransitionResources)(TransitionType, &Textures[0], NumTextures);
}

void FRHICommandTransitionTexturesArray::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionTextures);
	INTERNAL_DECORATOR(RHITransitionResources)(TransitionType, &Textures[0], Textures.Num());
}

template<ECmdList CmdListType>
void FRHICommandTransitionUAVs<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionUAVs);
	INTERNAL_DECORATOR_CONTEXT(RHITransitionResources)(TransitionType, TransitionPipeline, UAVs, NumUAVs, WriteFence);
}
template struct FRHICommandTransitionUAVs<ECmdList::EGfx>;
template struct FRHICommandTransitionUAVs<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandSetAsyncComputeBudget<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetAsyncComputeBudget);
	INTERNAL_DECORATOR_CONTEXT(RHISetAsyncComputeBudget)(Budget);
}
template struct FRHICommandSetAsyncComputeBudget<ECmdList::EGfx>;
template struct FRHICommandSetAsyncComputeBudget<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandWaitComputeFence<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(WaitComputeFence);
	INTERNAL_DECORATOR_CONTEXT(RHIWaitComputeFence)(WaitFence);
}
template struct FRHICommandWaitComputeFence<ECmdList::EGfx>;
template struct FRHICommandWaitComputeFence<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandCopyToStagingBuffer<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EnqueueStagedRead);
	INTERNAL_DECORATOR_CONTEXT(RHICopyToStagingBuffer)(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
}
template struct FRHICommandCopyToStagingBuffer<ECmdList::EGfx>;
template struct FRHICommandCopyToStagingBuffer<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandWriteGPUFence<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(WriteGPUFence);
	INTERNAL_DECORATOR_CONTEXT(RHIWriteGPUFence)(Fence);
}
template struct FRHICommandWriteGPUFence<ECmdList::EGfx>;
template struct FRHICommandWriteGPUFence<ECmdList::ECompute>;


void FRHICommandBuildLocalUniformBuffer::Execute(FRHICommandListBase& CmdList)
{
	LLM_SCOPE(ELLMTag::Shaders);
	RHISTAT(BuildLocalUniformBuffer);
	check(!IsValidRef(WorkArea.ComputedUniformBuffer->UniformBuffer)); // should not already have been created
	check(WorkArea.Layout);
	check(WorkArea.Contents); 
	if (WorkArea.ComputedUniformBuffer->UseCount)
	{
		WorkArea.ComputedUniformBuffer->UniformBuffer = GDynamicRHI->RHICreateUniformBuffer(WorkArea.Contents, *WorkArea.Layout, UniformBuffer_SingleFrame, EUniformBufferValidation::ValidateResources);
	}
	WorkArea.Layout = nullptr;
	WorkArea.Contents = nullptr;
}

template <typename TRHIShader>
void FRHICommandSetLocalUniformBuffer<TRHIShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetLocalUniformBuffer);
	check(LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount > 0 && IsValidRef(LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UniformBuffer)); // this should have been created and should have uses outstanding
	INTERNAL_DECORATOR(RHISetShaderUniformBuffer)(Shader, BaseIndex, LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UniformBuffer);
	if (--LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount == 0)
	{
		LocalUniformBuffer.WorkArea->ComputedUniformBuffer->~FComputedUniformBuffer();
	}
}
template struct FRHICommandSetLocalUniformBuffer<FRHIVertexShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIHullShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIDomainShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIGeometryShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIPixelShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIComputeShader>;

void FRHICommandBeginRenderQuery::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginRenderQuery);
	INTERNAL_DECORATOR(RHIBeginRenderQuery)(RenderQuery);
}

void FRHICommandEndRenderQuery::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndRenderQuery);
	INTERNAL_DECORATOR(RHIEndRenderQuery)(RenderQuery);
}

template<ECmdList CmdListType>
void FRHICommandSubmitCommandsHint<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SubmitCommandsHint);
	INTERNAL_DECORATOR_CONTEXT(RHISubmitCommandsHint)();
}
template struct FRHICommandSubmitCommandsHint<ECmdList::EGfx>;
template struct FRHICommandSubmitCommandsHint<ECmdList::ECompute>;

void FRHICommandPollOcclusionQueries::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(PollOcclusionQueries);
	INTERNAL_DECORATOR(RHIPollOcclusionQueries)();
}

#if RHI_RAYTRACING

void FRHICommandCopyBufferRegion::Execute(FRHICommandListBase& CmdList)
{
	INTERNAL_DECORATOR(RHICopyBufferRegion)(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
}

void FRHICommandCopyBufferRegions::Execute(FRHICommandListBase& CmdList)
{
	INTERNAL_DECORATOR(RHICopyBufferRegions)(Params);
}

void FRHICommandBuildAccelerationStructure::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BuildAccelerationStructure);
	if (Geometry)
	{
		INTERNAL_DECORATOR(RHIBuildAccelerationStructure)(Geometry);
	}
	else
	{
		INTERNAL_DECORATOR(RHIBuildAccelerationStructure)(Scene);
	}
}

void FRHICommandClearRayTracingBindings::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ClearRayTracingBindings);
	INTERNAL_DECORATOR(RHIClearRayTracingBindings)(Scene);
}

void FRHICommandUpdateAccelerationStructures::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(UpdateAccelerationStructure);
	INTERNAL_DECORATOR(RHIUpdateAccelerationStructures)(UpdateParams);
}

void FRHICommandBuildAccelerationStructures::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BuildAccelerationStructure);
	INTERNAL_DECORATOR(RHIBuildAccelerationStructures)(UpdateParams);
}

void FRHICommandRayTraceOcclusion::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceOcclusion);
	INTERNAL_DECORATOR(RHIRayTraceOcclusion)(Scene, Rays, Output, NumRays);
}

void FRHICommandRayTraceIntersection::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceIntersection);
	INTERNAL_DECORATOR(RHIRayTraceIntersection)(Scene, Rays, Output, NumRays);
}

void FRHICommandRayTraceDispatch::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceDispatch);
	extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
	INTERNAL_DECORATOR(RHIRayTraceDispatch)(GetRHIRayTracingPipelineState(Pipeline), RayGenShader, Scene, GlobalResourceBindings, Width, Height);
}

void FRHICommandSetRayTracingBindings::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetRayTracingHitGroup);
	extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
	if (BindingType == EBindingType_HitGroup)
	{
		INTERNAL_DECORATOR(RHISetRayTracingHitGroup)(Scene, InstanceIndex, SegmentIndex, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), ShaderIndex,
			NumUniformBuffers, UniformBuffers,
			LooseParameterDataSize, LooseParameterData,
			UserData);
	}
	else
	{
		INTERNAL_DECORATOR(RHISetRayTracingCallableShader)(Scene, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), ShaderIndex, NumUniformBuffers, UniformBuffers, UserData);
	}
}

#endif // RHI_RAYTRACING

void FRHICommandUpdateTextureReference::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(UpdateTextureReference);
	INTERNAL_DECORATOR(RHIUpdateTextureReference)(TextureRef, NewTexture);
}

void FRHIResourceUpdateInfo::ReleaseRefs()
{
	switch (Type)
	{
	case UT_VertexBuffer:
		VertexBuffer.DestBuffer->Release();
		if (VertexBuffer.SrcBuffer)
		{
			VertexBuffer.SrcBuffer->Release();
		}
		break;
	case UT_IndexBuffer:
		IndexBuffer.DestBuffer->Release();
		if (IndexBuffer.SrcBuffer)
		{
			IndexBuffer.SrcBuffer->Release();
		}
		break;
	case UT_VertexBufferSRV:
		VertexBufferSRV.SRV->Release();
		if (VertexBufferSRV.VertexBuffer)
		{
			VertexBufferSRV.VertexBuffer->Release();
		}
		break;
	default:
		// Unrecognized type, do nothing
		break;
	}
}

FRHICommandUpdateRHIResources::~FRHICommandUpdateRHIResources()
{
	if (bNeedReleaseRefs)
	{
		for (int32 Idx = 0; Idx < Num; ++Idx)
		{
			UpdateInfos[Idx].ReleaseRefs();
		}
	}
}

void FRHICommandUpdateRHIResources::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(UpdateRHIResources);
	for (int32 Idx = 0; Idx < Num; ++Idx)
	{
		FRHIResourceUpdateInfo& Info = UpdateInfos[Idx];
		switch (Info.Type)
		{
		case FRHIResourceUpdateInfo::UT_VertexBuffer:
			GDynamicRHI->RHITransferVertexBufferUnderlyingResource(
				Info.VertexBuffer.DestBuffer,
				Info.VertexBuffer.SrcBuffer);
			break;
		case FRHIResourceUpdateInfo::UT_IndexBuffer:
			GDynamicRHI->RHITransferIndexBufferUnderlyingResource(
				Info.IndexBuffer.DestBuffer,
				Info.IndexBuffer.SrcBuffer);
			break;
		case FRHIResourceUpdateInfo::UT_VertexBufferSRV:
			GDynamicRHI->RHIUpdateShaderResourceView(
				Info.VertexBufferSRV.SRV,
				Info.VertexBufferSRV.VertexBuffer,
				Info.VertexBufferSRV.Stride,
				Info.VertexBufferSRV.Format);
			break;
		default:
			// Unrecognized type, do nothing
			break;
		}
	}
}

void FRHICommandBeginScene::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginScene);
	INTERNAL_DECORATOR(RHIBeginScene)();
}

void FRHICommandEndScene::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndScene);
	INTERNAL_DECORATOR(RHIEndScene)();
}

void FRHICommandBeginFrame::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginFrame);
	INTERNAL_DECORATOR(RHIBeginFrame)();
}

void FRHICommandEndFrame::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndFrame);
	INTERNAL_DECORATOR(RHIEndFrame)();
}

void FRHICommandBeginDrawingViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginDrawingViewport);
	INTERNAL_DECORATOR(RHIBeginDrawingViewport)(Viewport, RenderTargetRHI);
}

void FRHICommandEndDrawingViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndDrawingViewport);
	INTERNAL_DECORATOR(RHIEndDrawingViewport)(Viewport, bPresent, bLockToVsync);
}

template<ECmdList CmdListType>
void FRHICommandPushEvent<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
#if	RHI_COMMAND_LIST_DEBUG_TRACES
	if (GetEmitDrawEventsOnlyOnCommandlist())
	{
		return;
	}
#endif
	RHISTAT(PushEvent);
	INTERNAL_DECORATOR_CONTEXT(RHIPushEvent)(Name, Color);
}
template struct FRHICommandPushEvent<ECmdList::EGfx>;
template struct FRHICommandPushEvent<ECmdList::ECompute>;

template<ECmdList CmdListType>
void FRHICommandPopEvent<CmdListType>::Execute(FRHICommandListBase& CmdList)
{
#if	RHI_COMMAND_LIST_DEBUG_TRACES
	if (GetEmitDrawEventsOnlyOnCommandlist())
	{
		return;
	}
#endif
	RHISTAT(PopEvent);
	INTERNAL_DECORATOR_CONTEXT(RHIPopEvent)();
}
template struct FRHICommandPopEvent<ECmdList::EGfx>;
template struct FRHICommandPopEvent<ECmdList::ECompute>;

void FRHICommandInvalidateCachedState::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RHIInvalidateCachedState);
	INTERNAL_DECORATOR(RHIInvalidateCachedState)();
}

void FRHICommandDiscardRenderTargets::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RHIDiscardRenderTargets);
	INTERNAL_DECORATOR(RHIDiscardRenderTargets)(Depth, Stencil, ColorBitMask);
}
