// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceRenderTarget2DArray.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget2DArray;


struct FRenderTarget2DArrayRWInstanceData_GameThread
{
	FRenderTarget2DArrayRWInstanceData_GameThread()
	{
#if WITH_EDITORONLY_DATA
		bPreviewTexture = false;
#endif
	}

	FIntVector Size = FIntVector(EForceInit::ForceInitToZero);
	EPixelFormat Format = EPixelFormat::PF_A16B16G16R16;

	UTextureRenderTarget2DArray* TargetTexture = nullptr;
#if WITH_EDITORONLY_DATA
	uint32 bPreviewTexture : 1;
#endif
	FNiagaraParameterDirectBinding<UObject*> RTUserParamBinding;
};

struct FRenderTarget2DArrayRWInstanceData_RenderThread
{
	FRenderTarget2DArrayRWInstanceData_RenderThread()
	{
#if WITH_EDITORONLY_DATA
		bPreviewTexture = false;
#endif
	}

	FIntVector Size = FIntVector(EForceInit::ForceInitToZero);
	
	FTextureReferenceRHIRef TextureReferenceRHI;
	FUnorderedAccessViewRHIRef UAV;
#if WITH_EDITORONLY_DATA
	uint32 bPreviewTexture : 1;
#endif
};

struct FNiagaraDataInterfaceProxyRenderTarget2DArrayProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyRenderTarget2DArrayProxy() {}
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

	virtual void ClearBuffers(FRHICommandList& RHICmdList) {}
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FRenderTarget2DArrayRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Render Target 2D Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceRenderTarget2DArray : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();	
		
	virtual void PostInitProperties() override;
	
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return true; }
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FRenderTarget2DArrayRWInstanceData_GameThread); }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual bool CanExposeVariables() const override { return true;}
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const override;

	//~ UNiagaraDataInterface interface END
	void GetSize(FVectorVMContext& Context); 
	void SetSize(FVectorVMContext& Context);

	static const FName SetValueFunctionName;
	static const FName SetSizeFunctionName;
	static const FName GetSizeFunctionName;
	static const FName LinearToIndexName;

	static const FString SizeName;
	static const FString RWOutputName;
	static const FString OutputName;

	UPROPERTY(EditAnywhere, Category = "Render Target")
	FIntVector Size;

	/** When enabled overrides the format of the render target, otherwise uses the project default setting. */
	UPROPERTY(EditAnywhere, Category = "Render Target", meta = (EditCondition = "bOverrideFormat"))
	TEnumAsByte<ETextureRenderTargetFormat> OverrideRenderTargetFormat;

	UPROPERTY(EditAnywhere, Category = "Render Target", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverrideFormat : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, EditAnywhere, Category = "Render Target")
	uint8 bPreviewRenderTarget : 1;
#endif

	UPROPERTY(EditAnywhere, Category = "Render Target", meta = (ToolTip = "When valid the user parameter is used as the render target rather than creating one internal, note that the input render target will be adjusted by the Niagara simulation"))
	FNiagaraUserParameterBinding RenderTargetUserParameter;

protected:
	static FNiagaraVariableBase ExposedRTVar;
	
	UPROPERTY(Transient)
	TMap<uint64, UTextureRenderTarget2DArray*> ManagedRenderTargets;
};
