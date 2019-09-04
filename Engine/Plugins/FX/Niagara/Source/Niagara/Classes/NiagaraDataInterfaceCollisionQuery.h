// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraCollision.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCollisionQuery.generated.h"

class INiagaraCompiler;
class FNiagaraSystemInstance;


struct CQDIPerInstanceData
{
	FNiagaraSystemInstance *SystemInstance;
	FNiagaraDICollisionQueryBatch CollisionBatch;
};


/** Data Interface allowing sampling of color curves. */
UCLASS(EditInlineNew, Category = "Collision", meta = (DisplayName = "Collision Query"))
class NIAGARA_API UNiagaraDataInterfaceCollisionQuery : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

#if WITH_EDITORONLY_DATA
#endif
	FNiagaraSystemInstance *SystemInstance;

	//UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	//UObject Interface End

	/** Initializes the per instance data for this interface. Returns false if there was some error and the simulation should be disabled. */
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) override;
	/** Destroys the per instence data for this interface. */
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) {}

	/** Ticks the per instance data for this interface, if it has any. */
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);
	virtual int32 PerInstanceDataSize()const { return sizeof(CQDIPerInstanceData); }
	
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	// VM functions
	void SubmitQuery(FVectorVMContext& Context);
	void ReadQuery(FVectorVMContext& Context);
	void PerformQuery(FVectorVMContext& Context);
	void PerformQuerySyncCPU(FVectorVMContext& Context);
	void PerformQueryAsyncCPU(FVectorVMContext& Context);
	void PerformQueryGPU(FVectorVMContext& Context);
	void QuerySceneDepth(FVectorVMContext& Context);
	void QueryMeshDistanceField(FVectorVMContext& Context);

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool RequiresDistanceFieldData() const override { return true; }

	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;
	
private:

	static FCriticalSection CriticalSection;
	UEnum* TraceChannelEnum;
};

struct FNiagaraDataIntefaceProxyCollisionQuery : public FNiagaraDataInterfaceProxy
{
	// There's nothing in this proxy. It just reads from scene textures.

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};
