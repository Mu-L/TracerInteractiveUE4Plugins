// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRenderer.h"
#include "ParticleResources.h"
#include "ParticleBeamTrailVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraVertexFactory.h"
#include "Engine/Engine.h"
#include "DynamicBufferAllocator.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraGPUSortInfo.h"
#include "Materials/MaterialInstanceDynamic.h"

DECLARE_CYCLE_STAT(TEXT("Sort Particles"), STAT_NiagaraSortParticles, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - All"), STAT_NiagaraAllocateGlobalFloatAll, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - InsideLock"), STAT_NiagaraAllocateGlobalFloatInsideLock, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Alloc New Buffer"), STAT_NiagaraAllocateGlobalFloatAllocNew, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Map Buffer"), STAT_NiagaraAllocateGlobalFloatMapBuffer, STATGROUP_Niagara);

int32 GNiagaraRadixSortThreshold = 400;
static FAutoConsoleVariableRef CVarNiagaraRadixSortThreshold(
	TEXT("Niagara.RadixSortThreshold"),
	GNiagaraRadixSortThreshold,
	TEXT("Instance count at which radix sort gets used instead of introspective sort.\n")
	TEXT("Set to  -1 to never use radixsort. (default=400)"),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

bool UNiagaraRendererProperties::GetIsActive()const
{
	return GetIsEnabled() && Platforms.IsActive();
}

//////////////////////////////////////////////////////////////////////////

class FNiagaraEmptyBufferSRV : public FRenderResource
{
public:
	FNiagaraEmptyBufferSRV(EPixelFormat InPixelFormat, const FString& InDebugName, uint32 InDefaultValue = 0) : PixelFormat(InPixelFormat), DebugName(InDebugName), DefaultValue(InDefaultValue) {}
	EPixelFormat PixelFormat;
	FString DebugName;
	FVertexBufferRHIRef Buffer;
	FShaderResourceViewRHIRef SRV;
	uint32 DefaultValue = 0;


	virtual void InitRHI() override
	{
		// Create a buffer with one element.
		uint32 NumBytes = GPixelFormats[PixelFormat].BlockBytes;
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = *DebugName;
		Buffer = RHICreateVertexBuffer(NumBytes, BUF_ShaderResource | BUF_Static, CreateInfo);

		// Zero the buffer memory.
		void* Data = RHILockVertexBuffer(Buffer, 0, NumBytes, RLM_WriteOnly);
		FMemory::Memset(Data, 0, NumBytes);
		
		if (PixelFormat == PF_R8G8B8A8)
		{
			*reinterpret_cast<uint32*>(Data) = DefaultValue;
		}

		RHIUnlockVertexBuffer(Buffer);

		SRV = RHICreateShaderResourceView(Buffer, NumBytes, PixelFormat);
	}

	virtual void ReleaseRHI() override
	{
		SRV.SafeRelease();
		Buffer.SafeRelease();
	}
};

class FNiagaraEmptyTextureSRV : public FRenderResource
{
public:
	FNiagaraEmptyTextureSRV(EPixelFormat InPixelFormat, const FString& InDebugName) : PixelFormat(InPixelFormat), DebugName(InDebugName) {}
	EPixelFormat PixelFormat;
	FString DebugName;
	FTexture2DRHIRef Texture;
	FShaderResourceViewRHIRef SRV;

	virtual void InitRHI() override
	{
		// Create a 1x1 texture.
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = *DebugName;
		Texture = RHICreateTexture2D(1, 1, PixelFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);

		// Zero the texture memory (there's only 1 row, so we can use the stride).
		uint32 Stride;
		void* Pixels = RHILockTexture2D(Texture, 0, RLM_WriteOnly, Stride, false);
		FMemory::Memset(Pixels, 0, Stride);
		RHIUnlockTexture2D(Texture, 0, false);

		SRV = RHICreateShaderResourceView(Texture, 0);
	}

	virtual void ReleaseRHI() override
	{
		SRV.SafeRelease();
		Texture.SafeRelease();
	}
};

FRHIShaderResourceView* FNiagaraRenderer::GetDummyFloatBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyFloatBuffer(PF_R32_FLOAT, TEXT("NiagaraRenderer::DummyFloat"));
	return DummyFloatBuffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyFloat2Buffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyFloat2Buffer(PF_G16R16F, TEXT("NiagaraRenderer::DummyFloat2"));
	return DummyFloat2Buffer.SRV;
}


FRHIShaderResourceView* FNiagaraRenderer::GetDummyFloat4Buffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyFloat4Buffer(PF_A32B32G32R32F, TEXT("NiagaraRenderer::DummyFloat4"));
	return DummyFloat4Buffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyWhiteColorBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyWhiteColorBuffer(PF_R8G8B8A8, TEXT("NiagaraRenderer::DummyWhiteColorBuffer"), FColor::White.ToPackedRGBA());
	return DummyWhiteColorBuffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyIntBuffer(PF_R32_SINT, TEXT("NiagaraRenderer::DummyInt"));
	return DummyIntBuffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyUIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyUIntBuffer(PF_R32_UINT, TEXT("NiagaraRenderer::DummyUInt"));
	return DummyUIntBuffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyUInt4Buffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyUInt4Buffer(PF_R32G32B32A32_UINT, TEXT("NiagaraRenderer::DummyUInt4"));
	return DummyUInt4Buffer.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyTextureReadBuffer2D()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyTextureSRV> DummyTextureReadBuffer2D(PF_R32_FLOAT, TEXT("NiagaraRenderer::DummyTextureReadBuffer2D"));
	return DummyTextureReadBuffer2D.SRV;
}

FRHIShaderResourceView* FNiagaraRenderer::GetDummyHalfBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraEmptyBufferSRV> DummyHalfBuffer(PF_R16F, TEXT("NiagaraRenderer::DummyHalf"));
	return DummyHalfBuffer.SRV;
}

FParticleRenderData FNiagaraRenderer::TransferDataToGPU(FGlobalDynamicReadBuffer& DynamicReadBuffer, const FNiagaraRendererLayout* RendererLayout, FNiagaraDataBuffer* SrcData)
{
	const int32 TotalFloatSize = RendererLayout->GetTotalFloatComponents_RenderThread() * SrcData->GetNumInstances();
	const int32 TotalHalfSize = RendererLayout->GetTotalHalfComponents_RenderThread() * SrcData->GetNumInstances();

	const int32 ComponentHalfStrideDest = SrcData->GetNumInstances() * sizeof(FFloat16);
	const int32 ComponentFloatStrideDest = SrcData->GetNumInstances() * sizeof(float);

	FGlobalDynamicReadBuffer::FAllocation FloatAllocation = TotalFloatSize ? DynamicReadBuffer.AllocateFloat(TotalFloatSize) : FGlobalDynamicReadBuffer::FAllocation();
	FGlobalDynamicReadBuffer::FAllocation HalfAllocation = TotalHalfSize ? DynamicReadBuffer.AllocateHalf(TotalHalfSize) : FGlobalDynamicReadBuffer::FAllocation();

	for (const FNiagaraRendererVariableInfo& VarInfo : RendererLayout->GetVFVariables_RenderThread())
	{
		int32 GpuOffset = VarInfo.GetGPUOffset();
		if (GpuOffset != INDEX_NONE && VarInfo.bUpload)
		{
			if (VarInfo.bHalfType)
			{
				GpuOffset &= ~(1 << 31);
				for (int32 CompIdx = 0; CompIdx < VarInfo.NumComponents; ++CompIdx)
				{
					FFloat16* SrcComponent = (FFloat16*)SrcData->GetComponentPtrHalf(VarInfo.DatasetOffset + CompIdx);
					void* Dest = HalfAllocation.Buffer + ComponentHalfStrideDest * (GpuOffset + CompIdx);
					FMemory::Memcpy(Dest, SrcComponent, ComponentHalfStrideDest);
				}
			}
			else
			{
				for (int32 CompIdx = 0; CompIdx < VarInfo.NumComponents; ++CompIdx)
				{
					float* SrcComponent = (float*)SrcData->GetComponentPtrFloat(VarInfo.DatasetOffset + CompIdx);
					void* Dest = FloatAllocation.Buffer + ComponentFloatStrideDest * (GpuOffset + CompIdx);
					FMemory::Memcpy(Dest, SrcComponent, ComponentFloatStrideDest);
				}
			}
		}
	}
	return FParticleRenderData{ FloatAllocation, HalfAllocation };
}

//////////////////////////////////////////////////////////////////////////

FNiagaraDynamicDataBase::FNiagaraDynamicDataBase(const FNiagaraEmitterInstance* InEmitter)
{
	check(InEmitter);

	FNiagaraDataSet& DataSet = InEmitter->GetData();
	SimTarget = DataSet.GetSimTarget();

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		//On CPU we pass through direct ptr to the most recent data buffer.
		Data.CPUParticleData = &DataSet.GetCurrentDataChecked();

		//Mark this buffer as in use by this renderer. Prevents this buffer being reused to write new simulation data while it's inuse by the renderer.
		Data.CPUParticleData->AddReadRef();
	}
	else
	{
		//On GPU we must access the correct buffer via the GPUExecContext. Probably a way to route this data better outside the dynamic data in future.
		//During simulation, the correct data buffer for rendering will be placed in the GPUContext and AddReadRef called.
		check(SimTarget == ENiagaraSimTarget::GPUComputeSim);
		Data.GPUExecContext = InEmitter->GetGPUContext();
	}
}

FNiagaraDynamicDataBase::~FNiagaraDynamicDataBase()
{
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		check(Data.CPUParticleData);
		//Release our ref on the buffer so it can be reused as a destination for a new simulation tick.
		Data.CPUParticleData->ReleaseReadRef();
	}
}

FNiagaraDataBuffer* FNiagaraDynamicDataBase::GetParticleDataToRender(bool bIsLowLatencyTranslucent)const
{
	FNiagaraDataBuffer* Ret = nullptr;

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		Ret = Data.CPUParticleData;
	}
	else
	{
		Ret = Data.GPUExecContext->GetDataToRender(bIsLowLatencyTranslucent);
	}

	checkSlow(Ret == nullptr || Ret->IsBeingRead());
	return Ret;
}

//////////////////////////////////////////////////////////////////////////


FNiagaraRenderer::FNiagaraRenderer(ERHIFeatureLevel::Type InFeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: DynamicDataRender(nullptr)
	, bLocalSpace(Emitter->GetCachedEmitter()->bLocalSpace)
	, bHasLights(false)
	, bMotionBlurEnabled(InProps ? InProps->bMotionBlurEnabled : false)
	, SimTarget(Emitter->GetCachedEmitter()->SimTarget)
	, NumIndicesPerInstance(InProps ? InProps->GetNumIndicesPerInstance() : 0)
	, FeatureLevel(InFeatureLevel)
{
#if STATS
	EmitterStatID = Emitter->GetCachedEmitter()->GetStatID(false, false);
#endif
}

void FNiagaraRenderer::Initialize(const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent)
{
	//Get our list of valid base materials. Fall back to default material if they're not valid.
	InProps->GetUsedMaterials(Emitter, BaseMaterials_GT);
	bool bCreateMidsForUsedMaterials = InProps->NeedsMIDsForMaterials();

	uint32 Index = 0;
	for (UMaterialInterface*& Mat : BaseMaterials_GT)
	{
		if (!IsMaterialValid(Mat))
		{
			Mat = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		else if (Mat && bCreateMidsForUsedMaterials && !Mat->IsA<UMaterialInstanceDynamic>())
		{
			const UNiagaraComponent* Comp = InComponent;
			for (const FNiagaraMaterialOverride& Override : Comp->EmitterMaterials)
			{
				if (Override.EmitterRendererProperty == InProps)
				{
					if (Index == Override.MaterialSubIndex)
					{
						Mat = Override.Material;
						continue;
					}					
				}
			}
		}

		Index ++;
		if (Mat)
			BaseMaterialRelevance_GT |= Mat->GetRelevance_Concurrent(FeatureLevel);
	}
}

FNiagaraRenderer::~FNiagaraRenderer()
{
	ReleaseRenderThreadResources();
	SetDynamicData_RenderThread(nullptr);
}

void FNiagaraRenderer::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) 
{
	if (Batcher)
	{
		NumRegisteredGPURenderers = Batcher->GetGPUInstanceCounterManager().GetGPURendererCount();
		if (NumRegisteredGPURenderers)
		{
			NumRegisteredGPURenderers->Value += GetMaxIndirectArgs();
		}
	}
}

void FNiagaraRenderer::ReleaseRenderThreadResources()
{
	if (NumRegisteredGPURenderers)
	{
		NumRegisteredGPURenderers->Value -= GetMaxIndirectArgs();
		NumRegisteredGPURenderers.SafeRelease();
	}
}

FPrimitiveViewRelevance FNiagaraRenderer::GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const
{
	FPrimitiveViewRelevance Result;
	bool bHasDynamicData = HasDynamicData();

	//Always draw so our LastRenderTime is updated. We may not have dynamic data if we're disabled from visibility culling.
	Result.bDrawRelevance =/* bHasDynamicData && */SceneProxy->IsShown(View) && View->Family->EngineShowFlags.Particles;
	Result.bShadowRelevance = bHasDynamicData && SceneProxy->IsShadowCast(View);
	Result.bDynamicRelevance = bHasDynamicData;
	if (bHasDynamicData)
	{
		Result.bOpaque = View->Family->EngineShowFlags.Bounds;
		DynamicDataRender->GetMaterialRelevance().SetPrimitiveViewRelevance(Result);
	}

	return Result;
}

void FNiagaraRenderer::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	check(IsInRenderingThread());
	if (DynamicDataRender)
	{
		delete DynamicDataRender;
		DynamicDataRender = NULL;
	}
	DynamicDataRender = NewDynamicData;
}

struct FParticleOrderAsUint
{
	uint32 OrderAsUint;
	int32 Index;

	template <bool bStrictlyPositive, bool bAscending>
	FORCEINLINE void SetAsUint(int32 InIndex, float InOrder) 
	{
		const uint32 SortKeySignBit = 0x80000000;
		uint32 InOrderAsUint = reinterpret_cast<uint32&>(InOrder);
		InOrderAsUint = (bStrictlyPositive || InOrder >= 0) ? (InOrderAsUint | SortKeySignBit) : ~InOrderAsUint;
		OrderAsUint = bAscending ? InOrderAsUint : ~InOrderAsUint;

		Index = InIndex;
	}

	template <bool bStrictlyPositive, bool bAscending>
	FORCEINLINE_DEBUGGABLE void SetAsUint(int32 InIndex, FFloat16 InOrder) 
	{
		const uint32 SortKeySignBit = 0x8000;
		uint32 InOrderAsUint = InOrder.Encoded;
		InOrderAsUint = (bStrictlyPositive || InOrder.Components.Sign != 0) ? (InOrderAsUint | SortKeySignBit) : ~InOrderAsUint;
		OrderAsUint = bAscending ? InOrderAsUint : ~InOrderAsUint;
		OrderAsUint &= 0xFFFF;
		Index = InIndex;
	}
		
	FORCEINLINE operator uint32() const { return OrderAsUint; }
};

void FNiagaraRenderer::ProcessMaterialParameterBindings(TConstArrayView< FNiagaraMaterialAttributeBinding > InMaterialParameterBindings, const FNiagaraEmitterInstance* InEmitter, TConstArrayView<UMaterialInterface*> InMaterials) const
{
	if (InMaterialParameterBindings.Num() == 0 || !InEmitter)
		return;

	FNiagaraSystemInstance* SystemInstance = InEmitter->GetParentSystemInstance();
	if (SystemInstance)
	{
		auto SystemSim = SystemInstance->GetSystemSimulation();

		if (SystemSim.IsValid())
		{
			for (UMaterialInterface* Mat : InMaterials)
			{
				UMaterialInstanceDynamic* MatDyn = Cast<UMaterialInstanceDynamic>(Mat);
				if (MatDyn)
				{
					for (const FNiagaraMaterialAttributeBinding& Binding : InMaterialParameterBindings)
					{

						if (Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetVec4Def() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetVec4Def()))
						{
							FLinearColor Var(1.0f, 1.0f, 1.0f, 1.0f);
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							MatDyn->SetVectorParameterValue(Binding.MaterialParameterName, Var);
						}
						else if (Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetColorDef() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetColorDef()))
						{
							FLinearColor Var(1.0f, 1.0f, 1.0f, 1.0f);
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							MatDyn->SetVectorParameterValue(Binding.MaterialParameterName, Var);
						}
						else if (Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetVec3Def() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetVec3Def()))
						{
							FLinearColor Var(1.0f, 1.0f, 1.0f, 1.0f);
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							MatDyn->SetVectorParameterValue(Binding.MaterialParameterName, Var);
						}
						else if (Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetVec2Def() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetVec2Def()))
						{
							FLinearColor Var(1.0f, 1.0f, 1.0f, 1.0f);
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							MatDyn->SetVectorParameterValue(Binding.MaterialParameterName, Var);
						}
						else if (Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetFloatDef() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetFloatDef()))
						{
							float Var = 1.0f;
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							MatDyn->SetScalarParameterValue(Binding.MaterialParameterName, Var);
						}
						else if (
							Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetUObjectDef() ||
							Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetUTextureDef() ||
							Binding.GetParamMapBindableVariable().GetType() == FNiagaraTypeDefinition::GetUTextureRenderTargetDef() ||
							(Binding.GetParamMapBindableVariable().GetType().IsDataInterface() && Binding.NiagaraChildVariable.GetType() == FNiagaraTypeDefinition::GetUTextureDef())
							)
						{
							UObject* Var = nullptr;
							InEmitter->GetBoundRendererValue_GT(Binding.GetParamMapBindableVariable(), Binding.NiagaraChildVariable, &Var);
							if (Var)
							{
								UTexture* Tex = Cast<UTexture>(Var);
								if (Tex && Tex->Resource != nullptr)
								{
									MatDyn->SetTextureParameterValue(Binding.MaterialParameterName, Tex);
								}
							}
						}
					}
				}
			}
		}
	}
}

void FNiagaraRenderer::SortIndices(const FNiagaraGPUSortInfo& SortInfo, const FNiagaraRendererVariableInfo& SortVariable, const FNiagaraDataBuffer& Buffer, FGlobalDynamicReadBuffer::FAllocation& OutIndices)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSortParticles);

	uint32 NumInstances = Buffer.GetNumInstances();
	check(OutIndices.ReadBuffer->NumBytes >= (OutIndices.Buffer - OutIndices.ReadBuffer->MappedBuffer) + NumInstances * sizeof(int32));
	check(SortInfo.SortMode != ENiagaraSortMode::None);
	check(SortInfo.SortAttributeOffset != INDEX_NONE);

	const bool bUseRadixSort = GNiagaraRadixSortThreshold != -1 && (int32)NumInstances  > GNiagaraRadixSortThreshold;
	const bool bSortVarIsHalf = SortVariable.bHalfType;

	int32* RESTRICT IndexBuffer = (int32*)(OutIndices.Buffer);

	FMemMark Mark(FMemStack::Get());
	FParticleOrderAsUint* RESTRICT ParticleOrder = (FParticleOrderAsUint*)FMemStack::Get().Alloc(sizeof(FParticleOrderAsUint) * NumInstances, alignof(FParticleOrderAsUint));

	if (SortInfo.SortMode == ENiagaraSortMode::ViewDepth || SortInfo.SortMode == ENiagaraSortMode::ViewDistance)
	{
		if (bSortVarIsHalf)
		{
			const int32 BaseCompOffset = SortVariable.DatasetOffset;
			FFloat16* RESTRICT PositionX = (FFloat16*)Buffer.GetComponentPtrHalf(BaseCompOffset);
			FFloat16* RESTRICT PositionY = (FFloat16*)Buffer.GetComponentPtrHalf(BaseCompOffset + 1);
			FFloat16* RESTRICT PositionZ = (FFloat16*)Buffer.GetComponentPtrHalf(BaseCompOffset + 2);
			auto GetPos = [&PositionX, &PositionY, &PositionZ](int32 Idx)
			{
				return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
			};

			if (SortInfo.SortMode == ENiagaraSortMode::ViewDepth)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<true, false>(i, FVector::DotProduct(GetPos(i) - SortInfo.ViewOrigin, SortInfo.ViewDirection));
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<true, false>(i, (GetPos(i) - SortInfo.ViewOrigin).SizeSquared());
				}
			}
		}
		else
		{
			const int32 BaseCompOffset = SortVariable.DatasetOffset;
			float* RESTRICT PositionX = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset);
			float* RESTRICT PositionY = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset + 1);
			float* RESTRICT PositionZ = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset + 2);
			auto GetPos = [&PositionX, &PositionY, &PositionZ](int32 Idx)
			{
				return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
			};

			if (SortInfo.SortMode == ENiagaraSortMode::ViewDepth)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<true, false>(i, FVector::DotProduct(GetPos(i) - SortInfo.ViewOrigin, SortInfo.ViewDirection));
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<true, false>(i, (GetPos(i) - SortInfo.ViewOrigin).SizeSquared());
				}
			}
		}
	}
	else
	{
		if (bSortVarIsHalf)
		{
			FFloat16* RESTRICT CustomSorting = (FFloat16*)Buffer.GetComponentPtrHalf(SortVariable.DatasetOffset);
			if (SortInfo.SortMode == ENiagaraSortMode::CustomAscending)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<false, true>(i, CustomSorting[i]);
				}
			}
			else // ENiagaraSortMode::CustomDecending
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<false, false>(i, CustomSorting[i]);
				}
			}
		}
		else
		{
			float* RESTRICT CustomSorting = (float*)Buffer.GetComponentPtrFloat(SortVariable.DatasetOffset);
			if (SortInfo.SortMode == ENiagaraSortMode::CustomAscending)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<false, true>(i, CustomSorting[i]);
				}
			}
			else // ENiagaraSortMode::CustomDecending
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].SetAsUint<false, false>(i, CustomSorting[i]);
				}
			}
		}
	}

	if (!bUseRadixSort)
	{
		Sort(ParticleOrder, NumInstances, [](const FParticleOrderAsUint& A, const FParticleOrderAsUint& B) { return A.OrderAsUint < B.OrderAsUint; });
		//Now transfer to the real index buffer.
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			IndexBuffer[i] = ParticleOrder[i].Index;
		}
	}
	else
	{
		FParticleOrderAsUint* RESTRICT ParticleOrderResult = bUseRadixSort ? (FParticleOrderAsUint*)FMemStack::Get().Alloc(sizeof(FParticleOrderAsUint) * NumInstances, alignof(FParticleOrderAsUint)) : nullptr;
		RadixSort32(ParticleOrderResult, ParticleOrder, NumInstances);
		//Now transfer to the real index buffer.
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			IndexBuffer[i] = ParticleOrderResult[i].Index;
		}
	}
}
