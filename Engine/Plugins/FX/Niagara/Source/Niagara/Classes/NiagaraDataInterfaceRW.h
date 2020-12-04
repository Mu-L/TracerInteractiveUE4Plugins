// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.generated.h"

class FNiagaraSystemInstance;

// Global HLSL variable base names, used by HLSL.
extern NIAGARA_API const FString NumAttributesName;
extern NIAGARA_API const FString NumCellsName;
extern NIAGARA_API const FString CellSizeName;
extern NIAGARA_API const FString WorldBBoxSizeName;

// Global VM function names, also used by the shaders code generation methods.
extern NIAGARA_API const FName NumCellsFunctionName;
extern NIAGARA_API const FName CellSizeFunctionName;

extern NIAGARA_API const FName WorldBBoxSizeFunctionName;

extern NIAGARA_API const FName SimulationToUnitFunctionName;
extern NIAGARA_API const FName UnitToSimulationFunctionName;
extern NIAGARA_API const FName UnitToIndexFunctionName;
extern NIAGARA_API const FName UnitToFloatIndexFunctionName;
extern NIAGARA_API const FName IndexToUnitFunctionName;

extern NIAGARA_API const FName IndexToUnitStaggeredXFunctionName;
extern NIAGARA_API const FName IndexToUnitStaggeredYFunctionName;

extern NIAGARA_API const FName IndexToLinearFunctionName;
extern NIAGARA_API const FName LinearToIndexFunctionName;

extern NIAGARA_API const FName ExecutionIndexToGridIndexFunctionName;
extern NIAGARA_API const FName ExecutionIndexToUnitFunctionName;
UENUM()
enum class ESetResolutionMethod
{
	Independent,
	MaxAxis,
	CellSize
};


// #todo(dmp): some of the stuff we'd expect to see here is on FNiagaraDataInterfaceProxy - refactor?
struct FNiagaraDataInterfaceProxyRW : public FNiagaraDataInterfaceProxy
{
public:
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }	

	// Get the element count for this instance
	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const = 0;

	// For data interfaces that support iteration on the GPU we need to be able to get the 'real' element count as known only by the GPU
	// The dispatch will use the CPU count, which is potentially an over-estimation, and the value inside the buffer will be used to clip instances that are not valid
	virtual uint32 GetGPUInstanceCountOffset(FNiagaraSystemInstanceID SystemInstanceID) const { return INDEX_NONE; }

	virtual void ClearBuffers(FRHICommandList& RHICmdList) {}

	virtual FNiagaraDataInterfaceProxyRW* AsIterationProxy() override { return this; }
};

UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceRWBase : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	TSet<int> OutputShaderStages;

	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	TSet<int> IterationShaderStages;

public:
	//~ UObject interface

	virtual void PostLoad() override
	{
		Super::PostLoad();
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);		
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

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;	
};


UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceGrid3D : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	// Number of cells
	UPROPERTY(EditAnywhere, Category = "Grid")
	FIntVector NumCells;

	// World space size of a cell
	UPROPERTY(EditAnywhere, Category = "Grid")
	float CellSize;

	// Number of cells on the longest axis
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumCellsMaxAxis;

	// Method for setting the grid resolution
	UPROPERTY(EditAnywhere, Category = "Grid")
	ESetResolutionMethod SetResolutionMethod;
	
	// World size of the grid
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

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override
	{
		const bool ParentVal = Super::CanEditChange(InProperty);

		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceGrid3D, NumCells))
		{
			return SetResolutionMethod == ESetResolutionMethod::Independent;
		}
		else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceGrid3D, CellSize))
		{
			return SetResolutionMethod == ESetResolutionMethod::CellSize;
		}
		else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceGrid3D, NumCellsMaxAxis))
		{
			return SetResolutionMethod == ESetResolutionMethod::MaxAxis;
		}

		return ParentVal;
	}
#endif
protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END	
};


UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterfaceGrid2D : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

public:
	// Number of cells in X
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumCellsX;

	// Number of cells in Y
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumCellsY;
	
	// Number of cells on the longest axis
	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	int32 NumCellsMaxAxis;

	// Number of Attributes
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumAttributes;

	// Set grid resolution according to longest axis
	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	bool SetGridFromMaxAxis;	

	// World size of the grid
	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
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

#if WITH_EDITOR		
	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors) override;
#endif
	//~ UNiagaraDataInterface interface END


protected:
	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END	
};