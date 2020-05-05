// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCore.h"
#include "NiagaraMergeable.h"
#include "Shader.h"
#include "Serialization/MemoryImage.h"
#include "NiagaraDataInterfaceBase.generated.h"

class INiagaraCompiler;
class UCurveVector;
class UCurveLinearColor;
class UCurveFloat;
class FNiagaraSystemInstance;
class FNiagaraShader;
class FNiagaraShaderMapPointerTable;
class UNiagaraDataInterfaceBase;
struct FNiagaraDataInterfaceGPUParamInfo;
class FRHICommandList;

struct FNiagaraDataInterfaceProxy;
class NiagaraEmitterInstanceBatcher;
struct FNiagaraComputeInstanceData;

DECLARE_EXPORTED_TEMPLATE_INTRINSIC_TYPE_LAYOUT(template<>, TIndexedPtr<UNiagaraDataInterfaceBase>, NIAGARACORE_API);

struct FNiagaraDataInterfaceSetArgs
{
	TShaderRefBase<FNiagaraShader, FNiagaraShaderMapPointerTable> Shader;
	FNiagaraDataInterfaceProxy* DataInterface;
	FNiagaraSystemInstanceID SystemInstance;
	const NiagaraEmitterInstanceBatcher* Batcher;
	const FNiagaraComputeInstanceData* ComputeInstanceData;
	uint32 SimulationStageIndex;
	bool IsOutputStage;
	bool IsIterationStage;
};

/**
 * An interface to the parameter bindings for the data interface used by a Niagara compute shader.
 * This is not using virtual methods, but derived classes may still override the methods listed below.
 * Overriden methods will be correctly called via indirection through UNiagaraDataInterfaceBase's vtable
 */
struct FNiagaraDataInterfaceParametersCS
{
	DECLARE_EXPORTED_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS, NIAGARACORE_API, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) {}
	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}
	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}

	LAYOUT_FIELD(TIndexedPtr<UNiagaraDataInterfaceBase>, DIType);
};

//////////////////////////////////////////////////////////////////////////

/** Base class for all Niagara data interfaces. */
UCLASS(abstract, EditInlineNew)
class NIAGARACORE_API UNiagaraDataInterfaceBase : public UNiagaraMergeable
{
	GENERATED_UCLASS_BODY()

public:
	
	/** Constructs the correct CS parameter type for this DI (if any). The object type returned by this can only vary by class and not per object data. */
	virtual FNiagaraDataInterfaceParametersCS* CreateComputeParameters() const { return nullptr; }
	virtual const FTypeLayoutDesc* GetComputeParametersTypeDesc() const { return nullptr; }

	/** Methods that operate on an instance of type FNiagaraDataInterfaceParametersCS*, created by the above CreateComputeParameters() method */
	virtual void BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) {}
	virtual void SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}
	virtual void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const {}
};

/** This goes in class declaration for UNiagaraDataInterfaceBase-derived types, that need custom parameter type */
#define DECLARE_NIAGARA_DI_PARAMETER() \
	virtual FNiagaraDataInterfaceParametersCS* CreateComputeParameters() const override; \
	virtual const FTypeLayoutDesc* GetComputeParametersTypeDesc() const override; \
	virtual void BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) override; \
	virtual void SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override; \
	virtual void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override

/** This goes in cpp file matched with class declartion using DECLARE_NIAGARA_DI_PARAMETER() */
#define IMPLEMENT_NIAGARA_DI_PARAMETER(T, ParameterType) \
	static_assert(ParameterType::InterfaceType == ETypeLayoutInterface::NonVirtual, "DI ParameterType must be non-virtual"); \
	FNiagaraDataInterfaceParametersCS* T::CreateComputeParameters() const { return new ParameterType(); } \
	const FTypeLayoutDesc* T::GetComputeParametersTypeDesc() const { return &StaticGetTypeLayoutDesc<ParameterType>(); } \
	void T::BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) { static_cast<ParameterType*>(Base)->Bind(ParameterInfo, ParameterMap); } \
	void T::SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const { static_cast<const ParameterType*>(Base)->Set(RHICmdList, Context); } \
	void T::UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const { static_cast<const ParameterType*>(Base)->Unset(RHICmdList, Context); }

