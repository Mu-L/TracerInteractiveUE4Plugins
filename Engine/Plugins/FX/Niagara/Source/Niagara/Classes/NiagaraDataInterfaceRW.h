// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.generated.h"

class FNiagaraSystemInstance;

// Global HLSL variable base names, used by HLSL.
extern NIAGARA_API const FString NumVoxelsName;
extern NIAGARA_API const FString VoxelSizeName;
extern NIAGARA_API const FString WorldBBoxSizeName;

extern NIAGARA_API const FString NumCellsName;
extern NIAGARA_API const FString CellSizeName;

// Global VM function names, also used by the shaders code generation methods.
extern NIAGARA_API const FName NumVoxelsFunctionName;
extern NIAGARA_API const FName VoxelSizeFunctionName;

extern NIAGARA_API const FName NumCellsFunctionName;
extern NIAGARA_API const FName CellSizeFunctionName;

extern NIAGARA_API const FName WorldBBoxSizeFunctionName;

extern NIAGARA_API const FName SimulationToUnitFunctionName;
extern NIAGARA_API const FName UnitToSimulationFunctionName;
extern NIAGARA_API const FName UnitToIndexFunctionName;
extern NIAGARA_API const FName IndexToUnitFunctionName;
extern NIAGARA_API const FName IndexToUnitStaggeredXFunctionName;
extern NIAGARA_API const FName IndexToUnitStaggeredYFunctionName;

extern NIAGARA_API const FName IndexToLinearFunctionName;
extern NIAGARA_API const FName LinearToIndexFunctionName;



// #todo(dmp): some of the stuff we'd expect to see here is on FNiagaraDataInterfaceProxy - refactor?
struct FNiagaraDataInterfaceProxyRW : public FNiagaraDataInterfaceProxy
{
public:

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}	

	virtual void ClearBuffers(FRHICommandList& RHICmdList) {}
};


UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceRWBase : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "RW")
	TSet<int> OutputShaderStages;

	UPROPERTY(EditAnywhere, Category = "RW")
	TSet<int> IterationShaderStages;

public:
	//~ UObject interface

	virtual void PostLoad() override
	{
		Super::PostLoad();

		PushToRenderThread();
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		PushToRenderThread();
	}	
	
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override
	{
		Super::PreEditChange(PropertyAboutToChange);

		// Flush the rendering thread before making any changes to make sure the 
		// data read by the compute shader isn't subject to a race condition.
		// TODO(mv): Solve properly using something like a RT Proxy.
		//FlushRenderingCommands();
	}

#endif
//~ UObject interface END

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override
	{
		return true;
	}

#if WITH_EDITOR	
	// Editor functionality
	virtual TArray<FNiagaraDataInterfaceError> GetErrors() override
	{
		// TODO(mv): Improve error messages?
		TArray<FNiagaraDataInterfaceError> Errors;

		return Errors;
	}
#endif

	void EmptyVMFunction(FVectorVMContext& Context) {}


protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	virtual void PushToRenderThread() {};

};


UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceGrid3D : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Grid", meta = (EditCondition = "!SetGridFromVoxelSize"))
	FIntVector NumVoxels;

	UPROPERTY(EditAnywhere, Category = "Grid", meta = (EditCondition = "SetGridFromVoxelSize"))
	float VoxelSize;

	UPROPERTY(EditAnywhere, Category = "Grid")
	bool SetGridFromVoxelSize;		

	UPROPERTY(EditAnywhere, Category = "Grid")
	FVector WorldBBoxSize;


public:

	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;	


	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	//~ UNiagaraDataInterface interface END


protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	virtual void PushToRenderThread() override;
};


UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceGrid2D : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Grid", meta = (EditCondition = "!SetGridFromMaxAxis"))
	int32 NumCellsX;

	UPROPERTY(EditAnywhere, Category = "Grid", meta = (EditCondition = "!SetGridFromMaxAxis"))
	int32 NumCellsY;
	
	UPROPERTY(EditAnywhere, Category = "Grid", meta = (EditCondition = "SetGridFromMaxAxis"))
	int32 NumCellsMaxAxis;

	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumAttributes;

	UPROPERTY(EditAnywhere, Category = "Grid")
	bool SetGridFromMaxAxis;	

	UPROPERTY(EditAnywhere, Category = "Grid")
	FVector2D WorldBBoxSize;


public:
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	//~ UNiagaraDataInterface interface END


protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	virtual void PushToRenderThread() override;
};