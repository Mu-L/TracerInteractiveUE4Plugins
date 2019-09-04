// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "NiagaraStats.h"
#include "Templates/AlignmentTemplates.h"
#include "NDISkeletalMeshCommon.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSkeletalMesh_TriangleSampling"

DECLARE_CYCLE_STAT(TEXT("Skel Mesh Sampling"), STAT_NiagaraSkel_Sample, STATGROUP_Niagara);

//Final binders for all static mesh interface functions.
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomTriCoord);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColor);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColorFallback);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordUV);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidTriCoord);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleCount);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleAt);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordVertices);

const FName FSkeletalMeshInterfaceHelper::RandomTriCoordName("RandomTriCoord");
const FName FSkeletalMeshInterfaceHelper::IsValidTriCoordName("IsValidTriCoord");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName("GetSkinnedTriangleData");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName("GetSkinnedTriangleDataWS");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName("GetSkinnedTriangleDataInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName("GetSkinnedTriangleDataWSInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetTriColorName("GetTriColor");
const FName FSkeletalMeshInterfaceHelper::GetTriUVName("GetTriUV");
const FName FSkeletalMeshInterfaceHelper::GetTriangleCountName("GetFilteredTriangleCount");
const FName FSkeletalMeshInterfaceHelper::GetTriangleAtName("GetFilteredTriangle");
const FName FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName("GetTriCoordVertices");


void UNiagaraDataInterfaceSkeletalMesh::GetTriangleSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::IsValidTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("IsValidDesc", "Determine if this tri coordinate's triangle index is valid for this mesh. Note that this only checks the mesh index buffer size and does not include any filtering settings.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedDataDesc", "Returns skinning dependant data for the pased MeshTriCoord in local space. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedDataWSDesc", "Returns skinning dependant data for the pased MeshTriCoord in world space. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interp")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataDesc", "Returns skinning dependant data for the pased MeshTriCoord in local space. Interpolates between previous and current frame. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interp")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataWSDesc", "Returns skinning dependant data for the pased MeshTriCoord in world space. Interpolates between previous and current frame. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriColorName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriUVName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriangleCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriangleAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TriangleIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 0")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 1")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 2")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetTriCoordVetsName", "Takes the TriangleIndex from a MeshTriCoord and returns the vertices for that triangle.");
#endif
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::BindTriangleSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstanceData, FVMExternalFunction &OutFunc)
{

	if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomTriCoord)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidTriCoord)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, false>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, false>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, true>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder< FNDITransformHandler, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, true>,NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriColorName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		if (InstanceData->HasColorData())
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColor)::Bind(this, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColorFallback)::Bind(this, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriUVName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 2);
		TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordUV)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriangleCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleCount)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriangleAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleAt)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TSkinningModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordVertices)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}

}

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	checkf(false, TEXT("Invalid template call for RandomTriIndex. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 SecIdx = RandStream.RandRange(0, Accessor.LODData->RenderSections.Num() - 1);
	FSkelMeshRenderSection& Sec = Accessor.LODData->RenderSections[SecIdx];
	int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
	return (Sec.BaseIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = SamplingInfo.GetWholeMeshLODBuiltData(InstData->GetLODIndex());
	int32 TriIdx = WholeMeshBuiltData.AreaWeightedTriangleSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return TriIdx;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 Idx = RandStream.RandRange(0, Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1);
	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 Idx = Accessor.SamplingRegionBuiltData->AreaWeightedSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 RegionIdx = RandStream.RandRange(0, InstData->SamplingRegionIndices.Num() - 1);
	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
	const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
	int32 Idx = RandStream.RandRange(0, RegionBuiltData.TriangleIndices.Num() - 1);
	return RegionBuiltData.TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FRandomStream& RandStream, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 RegionIdx = InstData->SamplingRegionAreaWeightedSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
	const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
	int32 Idx = RegionBuiltData.AreaWeightedSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return RegionBuiltData.TriangleIndices[Idx] / 3;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::RandomTriCoord(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryX(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutBaryY(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutBaryZ(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutTri.GetDest() = RandomTriIndex<FilterMode, AreaWeightingMode>(Context.RandStream, MeshAccessor, InstData);
		FVector Bary = RandomBarycentricCoord(Context.RandStream);
		*OutBaryX.GetDest() = Bary.X;		*OutBaryY.GetDest() = Bary.Y;		*OutBaryZ.GetDest() = Bary.Z;

		OutTri.Advance();
		OutBaryX.Advance();		OutBaryY.Advance();		OutBaryZ.Advance();
	}
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::IsValidTriCoord(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		int32 RequestedIndex = (TriParam.Get() * 3) + 2; // Get the last triangle index of the set

		FNiagaraBool Value;
		Value.SetValue(MeshAccessor.IndexBuffer != nullptr && MeshAccessor.IndexBuffer->Num() > RequestedIndex);
		*OutValid.GetDest() = Value;

		OutValid.Advance();
		BaryXParam.Advance();		BaryYParam.Advance();		BaryZParam.Advance(); TriParam.Advance();
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	checkf(false, TEXT("Invalid template call for GetSpecificTriangleCount. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 NumTris = 0;
	for (int32 i = 0; i < Accessor.LODData->RenderSections.Num(); i++)
	{
		NumTris += Accessor.LODData->RenderSections[i].NumTriangles;
	}
	return NumTris;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = SamplingInfo.GetWholeMeshLODBuiltData(InstData->GetLODIndex());
	return WholeMeshBuiltData.AreaWeightedTriangleSampler.GetNumEntries();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return Accessor.SamplingRegionBuiltData->TriangleIndices.Num();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return Accessor.SamplingRegionBuiltData->AreaWeightedSampler.GetNumEntries();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 NumTris = 0;

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		NumTris += RegionBuiltData.TriangleIndices.Num();
	}
	return NumTris;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleCount<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 NumTris = 0;

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		NumTris += RegionBuiltData.TriangleIndices.Num();
	}
	return NumTris;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	int32 Count = GetSpecificTriangleCount<FilterMode, AreaWeightingMode>(MeshAccessor, InstData);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutTri.GetDest() = Count;
		OutTri.Advance();
	}
}


//////////////////////////////////////////////////////////////////////////

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	checkf(false, TEXT("Invalid template call for GetSpecificTriangleAt. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	for (int32 i = 0; i < Accessor.LODData->RenderSections.Num(); i++)
	{
		if (Accessor.LODData->RenderSections[i].NumTriangles > (uint32)FilteredIndex)
		{
			FSkelMeshRenderSection& Sec = Accessor.LODData->RenderSections[i];
			return Sec.BaseIndex + FilteredIndex;
		}
		FilteredIndex -= Accessor.LODData->RenderSections[i].NumTriangles;
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 TriIdx = FilteredIndex;
	return TriIdx;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 MaxIdx = Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1;
	FilteredIndex = FMath::Min(FilteredIndex, MaxIdx);
	return Accessor.SamplingRegionBuiltData->TriangleIndices[FilteredIndex] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 Idx = FilteredIndex;
	int32 MaxIdx = Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1;
	Idx = FMath::Min(Idx, MaxIdx);

	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		if (FilteredIndex < RegionBuiltData.TriangleIndices.Num())
		{
			return RegionBuiltData.TriangleIndices[FilteredIndex] / 3;
		}

		FilteredIndex -= RegionBuiltData.TriangleIndices.Num();
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetSpecificTriangleAt<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		if (FilteredIndex < RegionBuiltData.TriangleIndices.Num())
		{
			return RegionBuiltData.TriangleIndices[FilteredIndex] / 3;
		}
		FilteredIndex -= RegionBuiltData.TriangleIndices.Num();
	}
	return 0;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());
	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryX(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutBaryY(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutBaryZ(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<FilterMode, AreaWeightingMode>(InstData);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		int32 Tri = TriParam.Get();
		int32 RealIdx = 0;
		RealIdx = GetSpecificTriangleAt<FilterMode, AreaWeightingMode>(Accessor, InstData, Tri);

		const int32 TriMax = (Accessor.IndexBuffer->Num() / 3) - 1;
		RealIdx = FMath::Clamp(RealIdx, 0, TriMax);

		*OutTri.GetDest() = RealIdx;
		float Coord = 1.0f / 3.0f;
		*OutBaryX.GetDest() = Coord;		*OutBaryY.GetDest() = Coord;		*OutBaryZ.GetDest() = Coord;

		TriParam.Advance();
		OutTri.Advance();
		OutBaryX.Advance();		OutBaryY.Advance();		OutBaryZ.Advance();
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordColor(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutColorR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorA(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData& LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	const FColorVertexBuffer& Colors = LODData.StaticVertexBuffers.ColorVertexBuffer;
	checkfSlow(Colors.GetNumVertices() != 0, TEXT("Trying to access vertex colors from mesh without any."));

	FMultiSizeIndexContainer& Indices = LODData.MultiSizeIndexContainer;
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	const int32 TriMax = (IndexBuffer->Num() / 3) - 1;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.Get(), 0, TriMax) * 3;
		const int32 Idx0 = IndexBuffer->Get(Tri);
		const int32 Idx1 = IndexBuffer->Get(Tri + 1);
		const int32 Idx2 = IndexBuffer->Get(Tri + 2);

		FLinearColor Color = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(),
			Colors.VertexColor(Idx0).ReinterpretAsLinear(), Colors.VertexColor(Idx1).ReinterpretAsLinear(), Colors.VertexColor(Idx2).ReinterpretAsLinear());

		*OutColorR.GetDest() = Color.R;
		*OutColorG.GetDest() = Color.G;
		*OutColorB.GetDest() = Color.B;
		*OutColorA.GetDest() = Color.A;
		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		OutColorR.Advance();
		OutColorG.Advance();
		OutColorB.Advance();
		OutColorA.Advance();
	}
}

// Where we determine we are sampling a skeletal mesh without tri color we bind to this fallback method 
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordColorFallback(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutColorR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutColorR.GetDestAndAdvance() = 1.0f;
		*OutColorG.GetDestAndAdvance() = 1.0f;
		*OutColorB.GetDestAndAdvance() = 1.0f;
		*OutColorA.GetDestAndAdvance() = 1.0f;
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordUV(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VertexAccessorType VertAccessor;
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> UVSetParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<float> OutUVX(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutUVY(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData& LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	FMultiSizeIndexContainer& Indices = LODData.MultiSizeIndexContainer;
	FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	const int32 TriMax = (IndexBuffer->Num() / 3) - 1;
	const int32 UVSetMax = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() - 1;
	const float InvDt = 1.0f / InstData->DeltaSeconds;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.Get(), 0, TriMax) * 3;
		const int32 Idx0 = IndexBuffer->Get(Tri);
		const int32 Idx1 = IndexBuffer->Get(Tri + 1);
		const int32 Idx2 = IndexBuffer->Get(Tri + 2);

		FVector2D UV0;		FVector2D UV1;		FVector2D UV2;
		int32 UVSet = UVSetParam.Get();
		UVSet = FMath::Clamp(UVSet, 0, UVSetMax);
		UV0 = VertAccessor.GetVertexUV(LODData, Idx0, UVSet);
		UV1 = VertAccessor.GetVertexUV(LODData, Idx1, UVSet);
		UV2 = VertAccessor.GetVertexUV(LODData, Idx2, UVSet);

		FVector2D UV = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), UV0, UV1, UV2);

		*OutUVX.GetDest() = UV.X;
		*OutUVY.GetDest() = UV.Y;

		TriParam.Advance();
		BaryXParam.Advance(); BaryYParam.Advance(); BaryZParam.Advance();
		UVSetParam.Advance();
		OutUVX.Advance();
		OutUVY.Advance();
	}
}

struct FGetTriCoodSkinnedDataOutputHandler
{
	FGetTriCoodSkinnedDataOutputHandler(FVectorVMContext& Context)
		: PosX(Context), PosY(Context), PosZ(Context)
		, VelX(Context), VelY(Context), VelZ(Context)
		, NormX(Context), NormY(Context), NormZ(Context)
		, BinormX(Context), BinormY(Context), BinormZ(Context)
		, TangentX(Context), TangentY(Context), TangentZ(Context)
		, bNeedsPosition(PosX.IsValid() || PosY.IsValid() || PosZ.IsValid())
		, bNeedsVelocity(VelX.IsValid() || VelY.IsValid() || VelZ.IsValid())
		, bNeedsNorm(NormX.IsValid() || NormY.IsValid() || NormZ.IsValid())
		, bNeedsBinorm(BinormX.IsValid() || BinormY.IsValid() || BinormZ.IsValid())
		, bNeedsTangent(TangentX.IsValid() || TangentY.IsValid() || TangentZ.IsValid())
	{
	}

	VectorVM::FExternalFuncRegisterHandler<float> PosX; VectorVM::FExternalFuncRegisterHandler<float> PosY; VectorVM::FExternalFuncRegisterHandler<float> PosZ;
	VectorVM::FExternalFuncRegisterHandler<float> VelX; VectorVM::FExternalFuncRegisterHandler<float> VelY; VectorVM::FExternalFuncRegisterHandler<float> VelZ;

	VectorVM::FExternalFuncRegisterHandler<float> NormX; VectorVM::FExternalFuncRegisterHandler<float> NormY; VectorVM::FExternalFuncRegisterHandler<float> NormZ;
	VectorVM::FExternalFuncRegisterHandler<float> BinormX; VectorVM::FExternalFuncRegisterHandler<float> BinormY; VectorVM::FExternalFuncRegisterHandler<float> BinormZ;
	VectorVM::FExternalFuncRegisterHandler<float> TangentX; VectorVM::FExternalFuncRegisterHandler<float> TangentY; VectorVM::FExternalFuncRegisterHandler<float> TangentZ;

	const bool bNeedsPosition;
	const bool bNeedsVelocity;
	const bool bNeedsNorm;
	const bool bNeedsBinorm;
	const bool bNeedsTangent;

	FORCEINLINE void SetPosition(FVector Position)
	{
		*PosX.GetDestAndAdvance() = Position.X;
		*PosY.GetDestAndAdvance() = Position.Y;
		*PosZ.GetDestAndAdvance() = Position.Z;
	}

	FORCEINLINE void SetVelocity(FVector Velocity)
	{
		*VelX.GetDestAndAdvance() = Velocity.X;
		*VelY.GetDestAndAdvance() = Velocity.Y;
		*VelZ.GetDestAndAdvance() = Velocity.Z;
	}

	FORCEINLINE void SetNormal(FVector Normal)
	{
		*NormX.GetDestAndAdvance() = Normal.X;
		*NormY.GetDestAndAdvance() = Normal.Y;
		*NormZ.GetDestAndAdvance() = Normal.Z;
	}

	FORCEINLINE void SetBinormal(FVector Binormal)
	{
		*BinormX.GetDestAndAdvance() = Binormal.X;
		*BinormY.GetDestAndAdvance() = Binormal.Y;
		*BinormZ.GetDestAndAdvance() = Binormal.Z;
	}

	FORCEINLINE void SetTangent(FVector Tangent)
	{
		*TangentX.GetDestAndAdvance() = Tangent.X;
		*TangentY.GetDestAndAdvance() = Tangent.Y;
		*TangentZ.GetDestAndAdvance() = Tangent.Z;
	}
};

template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType, typename bInterpolated>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordSkinnedData(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	VertexAccessorType VertAccessor;
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

 	if(bInterpolated::Value)
 	{
 		InterpParam.Init(Context);
 	}	

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FGetTriCoodSkinnedDataOutputHandler Output(Context);

	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData& LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>, TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>(InstData);
	const int32 TriMax = (Accessor.IndexBuffer->Num() / 3) - 1;
	const float InvDt = 1.0f / InstData->DeltaSeconds;

	FVector Pos0;		FVector Pos1;		FVector Pos2;
	FVector Prev0;		FVector Prev1;		FVector Prev2;
	FVector Normal;
	FVector Binormal;
	FVector Tangent;
	int32 Idx0; int32 Idx1; int32 Idx2;
	FVector Pos;
	FVector Prev;
	FVector Velocity;

	bool bNeedsCurr = bInterpolated::Value || Output.bNeedsPosition || Output.bNeedsVelocity || Output.bNeedsNorm || Output.bNeedsBinorm || Output.bNeedsTangent;
	bool bNeedsPrev = bInterpolated::Value || Output.bNeedsVelocity;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FMeshTriCoordinate MeshTriCoord(TriParam.GetAndAdvance(), FVector(BaryXParam.GetAndAdvance(), BaryYParam.GetAndAdvance(), BaryZParam.GetAndAdvance()));

		float Interp = 1.0f;
		if (bInterpolated::Value)
		{
			Interp = InterpParam.GetAndAdvance();
		}

		if(MeshTriCoord.Tri < 0 || MeshTriCoord.Tri > TriMax)
		{
			MeshTriCoord = FMeshTriCoordinate(0, FVector(1.0f, 0.0f, 0.0f));
		}

		SkinningHandler.GetTrianlgeIndices(Accessor, MeshTriCoord.Tri, Idx0, Idx1, Idx2);

		if (bNeedsCurr)
		{
			SkinningHandler.GetSkinnedTrianglePositions(Accessor, Idx0, Idx1, Idx2, Pos0, Pos1, Pos2);
		}

		if (bNeedsPrev)
		{
			SkinningHandler.GetSkinnedTrianglePreviousPositions(Accessor, Idx0, Idx1, Idx2, Prev0, Prev1, Prev2);
			Prev = BarycentricInterpolate(MeshTriCoord.BaryCoord, Prev0, Prev1, Prev2);
			TransformHandler.TransformPosition(Prev, PrevTransform);
		}

		if (Output.bNeedsPosition || Output.bNeedsVelocity)
		{
			Pos = BarycentricInterpolate(MeshTriCoord.BaryCoord, Pos0, Pos1, Pos2);
			TransformHandler.TransformPosition(Pos, Transform);
			
			if (bInterpolated::Value)
			{
				Pos = FMath::Lerp(Prev, Pos, Interp);
			}

			Output.SetPosition(Pos);
		}

		if (Output.bNeedsVelocity)
		{
			Velocity = (Pos - Prev) * InvDt;

			//No need to handle velocity wrt interpolation as it's based on the prev position anyway

			Output.SetVelocity(Velocity);
		}

		//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
		if (Output.bNeedsNorm)
		{
			Normal = ((Pos1 - Pos2) ^ (Pos0 - Pos2)).GetSafeNormal();
			TransformHandler.TransformVector(Normal, Transform);

			if (bInterpolated::Value)
			{
				FVector PrevNormal = ((Prev1 - Prev2) ^ (Prev0 - Prev2)).GetSafeNormal();
				TransformHandler.TransformVector(PrevNormal, PrevTransform);

				Normal = FMath::VInterpNormalRotationTo(PrevNormal, Normal, Interp, 1.0f);
			}

			Output.SetNormal(Normal);
		}

		if (Output.bNeedsBinorm || Output.bNeedsTangent)
		{
			FVector2D UV0 = VertAccessor.GetVertexUV(LODData, Idx0, 0);
			FVector2D UV1 = VertAccessor.GetVertexUV(LODData, Idx1, 0);
			FVector2D UV2 = VertAccessor.GetVertexUV(LODData, Idx2, 0);

			// Normal binormal tangent calculation code based on tools code found at:
			// \Engine\Source\Developer\MeshUtilities\Private\MeshUtilities.cpp
			// Skeletal_ComputeTriangleTangents
			FMatrix	ParameterToLocal(
				FPlane(Pos1.X - Pos0.X, Pos1.Y - Pos0.Y, Pos1.Z - Pos0.Z, 0),
				FPlane(Pos2.X - Pos0.X, Pos2.Y - Pos0.Y, Pos2.Z - Pos0.Z, 0),
				FPlane(Pos0.X, Pos0.Y, Pos0.Z, 0),
				FPlane(0, 0, 0, 1)
			);

			FMatrix ParameterToTexture(
				FPlane(UV1.X - UV0.X, UV1.Y - UV0.Y, 0, 0),
				FPlane(UV2.X - UV0.X, UV2.Y - UV0.Y, 0, 0),
				FPlane(UV0.X, UV0.Y, 1, 0),
				FPlane(0, 0, 0, 1)
			);

			// Use InverseSlow to catch singular matrices.  Inverse can miss this sometimes.
			const FMatrix TextureToLocal = ParameterToTexture.Inverse() * ParameterToLocal;

			if (bInterpolated::Value)
			{
				FMatrix	PrevParameterToLocal(
					FPlane(Prev1.X - Prev0.X, Prev1.Y - Prev0.Y, Prev1.Z - Prev0.Z, 0),
					FPlane(Prev2.X - Prev0.X, Prev2.Y - Prev0.Y, Prev2.Z - Prev0.Z, 0),
					FPlane(Prev0.X, Prev0.Y, Prev0.Z, 0),
					FPlane(0, 0, 0, 1)
				);

				// Use InverseSlow to catch singular matrices.  Inverse can miss this sometimes.
				const FMatrix PrevTextureToLocal = ParameterToTexture.Inverse() * PrevParameterToLocal;

				//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
				if (Output.bNeedsBinorm)
				{
					Binormal = (TextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Binormal, Transform);

					FVector PrevBinormal = (PrevTextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(PrevBinormal, PrevTransform);

					Binormal = FMath::VInterpNormalRotationTo(Binormal, Binormal, Interp, 1.0f);

					Output.SetBinormal(Binormal);
				}

				//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
				if (Output.bNeedsTangent)
				{
					Tangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Tangent, Transform);

					FVector PrevTangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(PrevTangent, PrevTransform);

					Tangent = FMath::VInterpNormalRotationTo(PrevTangent, Tangent, Interp, 1.0f);

					Output.SetTangent(Tangent);
				}
			}
			else
			{
				if (Output.bNeedsBinorm)
				{
					Binormal = (TextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Binormal, Transform);

					Output.SetBinormal(Binormal);
				}

				if (Output.bNeedsTangent)
				{
					Tangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Tangent, Transform);
					Output.SetTangent(Tangent);
				}
			}
		}
	}
}

template<typename SkinningHandlerType>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordVertices(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	SkinningHandlerType SkinningHandler;
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<int32> OutV0(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutV1(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutV2(Context);

	int32 Idx0; int32 Idx1; int32 Idx2;
	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);

	const int32 TriMax = (Accessor.IndexBuffer->Num() / 3) - 1;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.GetAndAdvance(), 0, TriMax);
		SkinningHandler.GetTrianlgeIndices(Accessor, Tri, Idx0, Idx1, Idx2);
		*OutV0.GetDestAndAdvance() = Idx0;
		*OutV1.GetDestAndAdvance() = Idx1;
		*OutV2.GetDestAndAdvance() = Idx2;
	}
}

#undef LOCTEXT_NAMESPACE
