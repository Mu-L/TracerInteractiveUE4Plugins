// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraDataInterfaceCamera.generated.h"

struct CameraDataInterface_InstanceData
{
	FVector CameraLocation;
	FRotator CameraRotation;
	float CameraFOV;
};

UCLASS(EditInlineNew, Category = "Camera", meta = (DisplayName = "Camera Query"))
class NIAGARA_API UNiagaraDataInterfaceCamera : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();

	/** This is used to determine which camera position to query for cpu emitters. If no valid index is supplied, the first controller is used as camera reference. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 PlayerControllerIndex = 0;

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(CameraDataInterface_InstanceData); }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(void* PerInstanceData) const override;
	virtual bool RequiresEarlyViewData() const override { return true; }
	//UNiagaraDataInterface Interface

	void GetCameraFOV(FVectorVMContext& Context);
	void GetCameraProperties(FVectorVMContext& Context);
	void GetViewPropertiesGPU(FVectorVMContext& Context);
	void GetClipSpaceTransformsGPU(FVectorVMContext& Context);
	void GetViewSpaceTransformsGPU(FVectorVMContext& Context);
	
private:
	static const FName GetViewPropertiesName;
	static const FName GetClipSpaceTransformsName;
	static const FName GetViewSpaceTransformsName;
	static const FName GetCameraPropertiesName;
	static const FName GetFieldOfViewName;
};

struct FNiagaraDataIntefaceProxyCameraQuery : public FNiagaraDataInterfaceProxy
{
	// There's nothing in this proxy.
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};

struct FNiagaraDataInterfaceParametersCS_CameraQuery : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CameraQuery, NonVirtual);

	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap);
	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const;

private:

	/** The SceneDepthTexture parameter for depth buffer query. */
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);
};
