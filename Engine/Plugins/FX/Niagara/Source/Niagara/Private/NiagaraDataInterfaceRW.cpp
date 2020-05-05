// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"


#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRW"

// Global HLSL variable base names, used by HLSL.
NIAGARA_API extern const FString NumVoxelsName(TEXT("NumVoxels_"));
NIAGARA_API extern const FString VoxelSizeName(TEXT("VoxelSize_"));
NIAGARA_API extern const FString WorldBBoxSizeName(TEXT("WorldBBoxSize_"));

NIAGARA_API extern const FString NumCellsName(TEXT("NumCells_"));
NIAGARA_API extern const FString CellSizeName(TEXT("CellSize_"));

// Global VM function names, also used by the shaders code generation methods.
NIAGARA_API extern const FName NumVoxelsFunctionName("GetNumVoxels");
NIAGARA_API extern const FName VoxelSizeFunctionName("GetVoxelSize");

NIAGARA_API extern const FName NumCellsFunctionName("GetNumCells");
NIAGARA_API extern const FName CellSizeFunctionName("GetCellSize");

NIAGARA_API extern const FName WorldBBoxSizeFunctionName("GetWorldBBoxSize");

NIAGARA_API extern const FName SimulationToUnitFunctionName("SimulationToUnit");
NIAGARA_API extern const FName UnitToSimulationFunctionName("UnitToSimulation");
NIAGARA_API extern const FName UnitToIndexFunctionName("UnitToIndex");
NIAGARA_API extern const FName IndexToUnitFunctionName("IndexToUnit");
NIAGARA_API extern const FName IndexToUnitStaggeredXFunctionName("IndexToUnitStaggeredX");
NIAGARA_API extern const FName IndexToUnitStaggeredYFunctionName("IndexToUnitStaggeredY");

NIAGARA_API extern const FName IndexToLinearFunctionName("IndexToLinear");
NIAGARA_API extern const FName LinearToIndexFunctionName("LinearToIndex");


UNiagaraDataInterfaceRWBase::UNiagaraDataInterfaceRWBase(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

bool UNiagaraDataInterfaceRWBase::Equals(const UNiagaraDataInterface* Other) const
{
	const UNiagaraDataInterfaceRWBase* OtherTyped = CastChecked<const UNiagaraDataInterfaceRWBase>(Other);

	if (OtherTyped)
	{
		return OutputShaderStages.Difference(OtherTyped->OutputShaderStages).Num() == 0 && IterationShaderStages.Difference(OtherTyped->IterationShaderStages).Num() == 0;
	}

	return false;
}

bool UNiagaraDataInterfaceRWBase::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceRWBase* OtherTyped = CastChecked<UNiagaraDataInterfaceRWBase>(Destination);

	OtherTyped->OutputShaderStages = OutputShaderStages;
	OtherTyped->IterationShaderStages = IterationShaderStages;

	return true;
}

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceGrid3D::UNiagaraDataInterfaceGrid3D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumVoxels(3, 3, 3)
	, VoxelSize(1.)
	, SetGridFromVoxelSize(false)	
	, WorldBBoxSize(100., 100., 100.)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRW());
	PushToRenderThread();
}


void UNiagaraDataInterfaceGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldBBoxSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("WorldBBoxSize")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumVoxelsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SimulationToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("SimulationToUnitTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumVoxelsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = VoxelSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("VoxelSize")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == WorldBBoxSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == NumVoxelsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
	else if (BindingInfo.Name == SimulationToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == VoxelSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

bool UNiagaraDataInterfaceGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid3D>(Other);

	return OtherTyped->NumVoxels == NumVoxels &&
		FMath::IsNearlyEqual(OtherTyped->VoxelSize, VoxelSize) &&		
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize);
}

void UNiagaraDataInterfaceGrid3D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int3 {NumVoxelsName};
		float3 {VoxelSizeName};		
		float3 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("VoxelSizeName"), VoxelSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid3D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("FunctionName"), FunctionInfo.InstanceName},
		{ TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("VoxelSizeName"), VoxelSizeName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};

	if (FunctionInfo.DefinitionName == WorldBBoxSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_WorldBBox)
			{
				Out_WorldBBox = {WorldBBoxSizeName};				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NumVoxelsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumVoxelsX, out int Out_NumVoxelsY, out int Out_NumVoxelsZ)
			{
				Out_NumVoxelsX = {NumVoxelsName}.x;
				Out_NumVoxelsY = {NumVoxelsName}.y;
				Out_NumVoxelsZ = {NumVoxelsName}.z;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SimulationToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Simulation, float4x4 In_SimulationToUnitTransform, out float3 Out_Unit)
			{
				Out_Unit = mul(float4(In_Simulation, 1.0), In_SimulationToUnitTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, out int Out_IndexX, out int Out_IndexY, out int Out_IndexZ)
			{
				int3 Out_IndexTmp = round(In_Unit * {NumVoxelsName} - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;
				Out_IndexZ = Out_IndexTmp.z;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumVoxelsName}.x + In_IndexZ * {NumVoxelsName}.x * {NumVoxelsName}.y;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == VoxelSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_VoxelSize)
			{
				Out_VoxelSize = {VoxelSizeName};
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}

	return false;
}



bool UNiagaraDataInterfaceGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid3D>(Destination);


	OtherTyped->NumVoxels = NumVoxels;
	OtherTyped->VoxelSize = VoxelSize;
	OtherTyped->SetGridFromVoxelSize = SetGridFromVoxelSize;		
	OtherTyped->WorldBBoxSize = WorldBBoxSize;

	return true;
}

void UNiagaraDataInterfaceGrid3D::PushToRenderThread()
{


}

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceGrid2D::UNiagaraDataInterfaceGrid2D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumCellsX(3)
	, NumCellsY(3)
	, NumCellsMaxAxis(3)
	, NumAttributes(1)
	, SetGridFromMaxAxis(false)	
	, WorldBBoxSize(100., 100.)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRW());
	PushToRenderThread();
}


void UNiagaraDataInterfaceGrid2D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{	
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldBBoxSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("WorldBBoxSize")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SimulationToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("SimulationToUnitTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToSimulationFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("UnitToSimulationTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitStaggeredXFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitStaggeredYFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = LinearToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = CellSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("CellSize")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}


void UNiagaraDataInterfaceGrid2D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == WorldBBoxSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == NumCellsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SimulationToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == UnitToSimulationFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitStaggeredXFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitStaggeredYFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == LinearToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == CellSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}


bool UNiagaraDataInterfaceGrid2D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid2D* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid2D>(Other);

	return 
		OtherTyped->NumCellsX == NumCellsX &&
		OtherTyped->NumCellsY == NumCellsY &&
		OtherTyped->NumAttributes == NumAttributes &&
		OtherTyped->NumCellsMaxAxis == NumCellsMaxAxis &&		
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize);
}

void UNiagaraDataInterfaceGrid2D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int2 {NumCellsName};
		float2 {CellSizeName};		
		float2 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("FunctionName"), FunctionInfo.InstanceName},
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};

	if (FunctionInfo.DefinitionName == WorldBBoxSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_WorldBBox)
			{
				Out_WorldBBox = {WorldBBoxSizeName};				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NumCellsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumCellsX, out int Out_NumCellsY)
			{
				Out_NumCellsX = {NumCellsName}.x;
				Out_NumCellsY = {NumCellsName}.y;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SimulationToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Simulation, float4x4 In_SimulationToUnitTransform, out float3 Out_Unit)
			{
				Out_Unit = mul(float4(In_Simulation, 1.0), In_SimulationToUnitTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToSimulationFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, float4x4 In_UnitToSimulationTransform, out float3 Out_Simulation)
			{
				Out_Simulation = mul(float4(In_Unit, 1.0), In_UnitToSimulationTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float2 In_Unit, out int Out_IndexX, out int Out_IndexY)
			{
				int2 Out_IndexTmp = round(In_Unit * float2({NumCellsName})  - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + .5) / float2({NumCellsName}), 0);
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitStaggeredXFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + float2(0.0, 0.5)) / float2({NumCellsName}), 0);
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitStaggeredYFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) +  + float2(0.5, 0.0)) / float2({NumCellsName}), 0);
			}
		)");
		
		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumCellsName}.x;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == LinearToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_Linear, out int Out_IndexX, out int Out_IndexY)
			{
				Out_IndexX = In_Linear % {NumCellsName}.x;
				Out_IndexY = In_Linear / {NumCellsName}.x;				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == CellSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_CellSize)
			{
				Out_CellSize = {CellSizeName};
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}

	return false;
}


bool UNiagaraDataInterfaceGrid2D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid2D* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid2D>(Destination);


	OtherTyped->NumCellsX = NumCellsX;
	OtherTyped->NumCellsY = NumCellsY;
	OtherTyped->NumAttributes = NumAttributes;
	OtherTyped->NumCellsMaxAxis = NumCellsMaxAxis;
	OtherTyped->SetGridFromMaxAxis = SetGridFromMaxAxis;	
	OtherTyped->WorldBBoxSize = WorldBBoxSize;

	return true;
}

void UNiagaraDataInterfaceGrid2D::PushToRenderThread()
{


}


#undef LOCTEXT_NAMESPACE
