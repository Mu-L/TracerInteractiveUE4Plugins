// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceOcclusion.generated.h"

UCLASS(EditInlineNew, Category = "Camera", meta = (DisplayName = "Occlusion Query"))
class NIAGARA_API UNiagaraDataInterfaceOcclusion : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool RequiresDepthBuffer() const override { return true; }
	//UNiagaraDataInterface Interface

	void QueryOcclusionFactorGPU(FVectorVMContext& Context);
	void QueryOcclusionFactorCircleGPU(FVectorVMContext& Context);
	
private:
	static const FName GetCameraOcclusionRectangleName;
	static const FName GetCameraOcclusionCircleName;
};

struct FNiagaraDataIntefaceProxyOcclusionQuery : public FNiagaraDataInterfaceProxy
{
	// There's nothing in this proxy. It just reads from scene textures.

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};