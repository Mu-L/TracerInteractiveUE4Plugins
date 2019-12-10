// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: Hair manager implementation.
=============================================================================*/

#include "HairStrandsInterface.h"
#include "HairStrandsRendering.h"
#include "HairStrandsMeshProjection.h"

#include "SkeletalRenderPublic.h"
#include "SceneRendering.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairRendering, Log, All);

static int32 GHairStrandsRenderingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRenderingEnable(TEXT("r.HairStrands.Enable"), GHairStrandsRenderingEnable, TEXT("Enable/Disable hair strands rendering"));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometrySection& In, const FTransform& InTransform)
{
	FHairStrandsProjectionMeshData::Section Out;
	Out.IndexBuffer = In.IndexBuffer;
	Out.PositionBuffer = In.PositionBuffer;
	Out.TotalVertexCount = In.TotalVertexCount;
	Out.TotalIndexCount = In.TotalIndexCount;
	Out.VertexBaseIndex = In.VertexBaseIndex;
	Out.IndexBaseIndex = In.IndexBaseIndex;
	Out.NumPrimitives = In.NumPrimitives;
	Out.SectionIndex = In.SectionIndex;
	Out.LODIndex = In.LODIndex;
	Out.LocalToWorld = InTransform;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime execution order (on the render thread):
//  * Register
//  * For each frame
//		* Update
//		* AddProjectionQuery (Opt)
//		* Project (Opt)
//		* Update triangles information for dynamic meshes
//		* RunHairStrandsInterpolation (Interpolation callback)
//  * UnRegister
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairStrandsManager
{
	struct Element
	{
		uint64 Id = 0;
		EWorldType::Type WorldType = EWorldType::None;
		FHairStrandsDebugInfo DebugInfo;
		FHairStrandsInterpolationData InterpolationData;
		FHairStrandsProjectionHairData RenProjectionHairDatas;
		FHairStrandsProjectionHairData SimProjectionHairDatas;
		FCachedGeometry CachedGeometry;
		FTransform SkeletalLocalToWorld;
		FVector SkeletalDeformedPositionOffset;
		const FSkeletalMeshObject* MeshObject = nullptr;
		int32 FrameLODIndex = -1;
	};
	TArray<Element> Elements;

	struct ProjectionQuery
	{
		uint64 Id = 0;
		EWorldType::Type WorldType = EWorldType::None;
		int32 LODIndex = -1;
		FVector RestPositionOffset = FVector::ZeroVector;
		bool bProcessed = false;
	};
	TArray<ProjectionQuery> ProjectionsQueries;
};

FHairStrandsManager GHairManager;

void AddHairStrandsProjectionQuery(
	FRHICommandListImmediate& RHICmdList,
	uint64 Id,
	EWorldType::Type WorldType,
	int32 LODIndex,
	const FVector& RestPositionOffset)
{
	GHairManager.ProjectionsQueries.Add({ Id, WorldType, LODIndex, RestPositionOffset, false });
}

void RegisterHairStrands(
	uint64 Id, 
	EWorldType::Type WorldType,
	const FHairStrandsInterpolationData& InterpolationData, 
	const FHairStrandsProjectionHairData& RenProjectionDatas,
 	const FHairStrandsProjectionHairData& SimProjectionDatas,
	const FHairStrandsDebugInfo& DebugInfo)
{
	for (int32 Index = 0; Index < GHairManager.Elements.Num(); ++Index)
	{
		if (GHairManager.Elements[Index].Id == Id && GHairManager.Elements[Index].WorldType == WorldType)
		{
			// Component already registered. This should not happen.
			UE_LOG(LogHairRendering, Warning, TEXT("Component already register. This should't happen. Please report this to a rendering engineer."))
			return;
		}
	}

	FHairStrandsManager::Element& E =  GHairManager.Elements.AddDefaulted_GetRef();
	E.Id = Id;
	E.WorldType = WorldType;
	E.InterpolationData = InterpolationData;
	E.RenProjectionHairDatas = RenProjectionDatas;
	for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.RenProjectionHairDatas.HairGroups)
	{
		ProjectionData.LocalToWorld = FTransform::Identity;
	}
	E.SimProjectionHairDatas = SimProjectionDatas;
	for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.SimProjectionHairDatas.HairGroups)
	{
		ProjectionData.LocalToWorld = FTransform::Identity;
	}
	E.SkeletalLocalToWorld = FTransform::Identity;
	E.SkeletalDeformedPositionOffset = FVector::ZeroVector;
	E.DebugInfo = DebugInfo;
}

bool UpdateHairStrands(
	uint64 Id, 
	EWorldType::Type WorldType, 
	const FTransform& HairLocalToWorld, 
	const FTransform& SkeletalLocalToWorld,
	const FVector& SkeletalDeformedPositionOffset)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.RenProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.SimProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		E.SkeletalLocalToWorld = SkeletalLocalToWorld;
		E.SkeletalDeformedPositionOffset = SkeletalDeformedPositionOffset;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id, 
	EWorldType::Type NewWorldType)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id)
			continue;

		E.WorldType = NewWorldType;
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FTransform& HairLocalToWorld,
	const FHairStrandsProjectionHairData& RenProjectionDatas,
	const FHairStrandsProjectionHairData& SimProjectionDatas)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		E.RenProjectionHairDatas = RenProjectionDatas;
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.RenProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		E.SimProjectionHairDatas = SimProjectionDatas;
		for (FHairStrandsProjectionHairData::HairGroup& ProjectionData : E.SimProjectionHairDatas.HairGroups)
		{
			ProjectionData.LocalToWorld = HairLocalToWorld;
		}
		return true;
	}

	return false;
}

bool UpdateHairStrands(
	uint64 Id,
	EWorldType::Type WorldType,
	const FSkeletalMeshObject* MeshObject)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Id != Id || E.WorldType != WorldType)
			continue;

		E.MeshObject = MeshObject;
		return true;
	}

	return false;
}

void UnregisterHairStrands(uint64 Id)
{
	for (int32 Index=0;Index< GHairManager.Elements.Num();++Index)
	{
		if (GHairManager.Elements[Index].Id == Id)
		{
			GHairManager.Elements[Index] = GHairManager.Elements[GHairManager.Elements.Num()-1];
			GHairManager.Elements.SetNum(GHairManager.Elements.Num() - 1);
		}
	}
}

void RunHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList, 
	EWorldType::Type WorldType, 
	TShaderMap<FGlobalShaderType>* ShaderMap, 
	EHairStrandsInterpolationType Type)
{
	check(IsInRenderingThread());

	// Update geometry cached based on GPU Skin output
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		E.CachedGeometry = E.MeshObject ? E.MeshObject->GetCachedGeometry() : FCachedGeometry();
	}

	// Process projection queries
	for (FHairStrandsManager::ProjectionQuery& Q : GHairManager.ProjectionsQueries)
	{	
		for (FHairStrandsManager::Element& E : GHairManager.Elements)
		{
			if (E.Id != Q.Id || E.WorldType != Q.WorldType || E.CachedGeometry.Sections.Num() == 0 || Q.bProcessed)
				continue;
	
			FHairStrandsProjectionMeshData MeshData;
			for (FCachedGeometrySection Section : E. CachedGeometry.Sections)
			{
				check(Section.LODIndex == Q.LODIndex);
				MeshData.Sections.Add(ConvertMeshSection(Section, E.SkeletalLocalToWorld));
			}

			for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
			{
				ProjectionHairData.LODDatas[Q.LODIndex].RestPositionOffset = Q.RestPositionOffset;
				ProjectHairStrandsOntoMesh(RHICmdList, ShaderMap, Q.LODIndex, MeshData, ProjectionHairData);
				UpdateHairStrandsMeshTriangles(RHICmdList, ShaderMap, Q.LODIndex, HairStrandsTriangleType::RestPose, MeshData, ProjectionHairData);
			}

			for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
			{
				ProjectionHairData.LODDatas[Q.LODIndex].RestPositionOffset = Q.RestPositionOffset;
				ProjectHairStrandsOntoMesh(RHICmdList, ShaderMap, Q.LODIndex, MeshData, ProjectionHairData);
				UpdateHairStrandsMeshTriangles(RHICmdList, ShaderMap, Q.LODIndex, HairStrandsTriangleType::RestPose, MeshData, ProjectionHairData);
			}
			Q.bProcessed = true;
		}
	}

	// Update dynamic mesh triangles
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		E.FrameLODIndex = -1;
		if (E.WorldType != WorldType || E.CachedGeometry.Sections.Num() == 0)
			continue;
	
		FHairStrandsProjectionMeshData MeshData;
		for (FCachedGeometrySection Section : E.CachedGeometry.Sections)
		{
			// Ensure all mesh's sections have the same LOD index
			if (E.FrameLODIndex < 0) E.FrameLODIndex = Section.LODIndex;
			check(E.FrameLODIndex == Section.LODIndex);
	
			MeshData.Sections.Add(ConvertMeshSection(Section, E.SkeletalLocalToWorld));
		}

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
		{
			if (EHairStrandsInterpolationType::RenderStrands == Type && 0 <= E.FrameLODIndex && E.FrameLODIndex < ProjectionHairData.LODDatas.Num() && ProjectionHairData.LODDatas[E.FrameLODIndex].bIsValid)
			{
				ProjectionHairData.LODDatas[E.FrameLODIndex].DeformedPositionOffset = E.SkeletalDeformedPositionOffset;
				UpdateHairStrandsMeshTriangles(RHICmdList, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshData, ProjectionHairData);
			}
		}

		for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
		{
			if (EHairStrandsInterpolationType::SimulationStrands == Type && 0 <= E.FrameLODIndex && E.FrameLODIndex < ProjectionHairData.LODDatas.Num() && ProjectionHairData.LODDatas[E.FrameLODIndex].bIsValid)
			{
				ProjectionHairData.LODDatas[E.FrameLODIndex].DeformedPositionOffset = E.SkeletalDeformedPositionOffset;
				UpdateHairStrandsMeshTriangles(RHICmdList, ShaderMap, E.FrameLODIndex, HairStrandsTriangleType::DeformedPose, MeshData, ProjectionHairData);
			}
		}
	}

	// Hair interpolation
	if (EHairStrandsInterpolationType::RenderStrands == Type)
	{
		for (FHairStrandsManager::Element& E : GHairManager.Elements)
		{
			if (E.WorldType != WorldType)
				continue;

			if (E.InterpolationData.Input && E.InterpolationData.Output && E.InterpolationData.Function)
			{
				E.InterpolationData.Function(RHICmdList, E.InterpolationData.Input, E.InterpolationData.Output, E.RenProjectionHairDatas, E.SimProjectionHairDatas, E.FrameLODIndex);
			}
		}
	}
}

void GetGroomInterpolationData(const EWorldType::Type WorldType, FHairStrandsProjectionMeshData& OutGeometries)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		for (FCachedGeometrySection Section : E.CachedGeometry.Sections)
		{		
			OutGeometries.Sections.Add(ConvertMeshSection(Section, E.SkeletalLocalToWorld));
		}
	}
}

void GetGroomInterpolationData(const EWorldType::Type WorldType, const bool bRenderData, FHairStrandsProjectionHairData& Out, TArray<int32>& OutLODIndices)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.WorldType != WorldType)
			continue;

		const bool bHasDynamicMesh = E.CachedGeometry.Sections.Num() > 0;
		if (bHasDynamicMesh)
		{
			if (bRenderData)
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.RenProjectionHairDatas.HairGroups)
				{
					Out.HairGroups.Add(ProjectionHairData);
					OutLODIndices.Add(E.FrameLODIndex);
				}
			}
			else
			{
				for (FHairStrandsProjectionHairData::HairGroup& ProjectionHairData : E.SimProjectionHairDatas.HairGroups)
				{
					Out.HairGroups.Add(ProjectionHairData);
					OutLODIndices.Add(E.FrameLODIndex);
				}
			}
		}
	}
}

FHairStrandsDebugInfos GetHairStandsDebugInfos()
{
	FHairStrandsDebugInfos Infos;
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		FHairStrandsDebugInfo& Info = Infos.AddDefaulted_GetRef();
		Info = E.DebugInfo;
		Info.Id = E.Id;
		Info.WorldType = E.WorldType;

		const uint32 GroupCount = Info.HairGroups.Num();
		for (uint32 GroupIt=0; GroupIt < GroupCount; ++GroupIt)
		{		
			FHairStrandsDebugInfo::HairGroup& GroupInfo = Info.HairGroups[GroupIt];
			if (GroupIt < uint32(E.RenProjectionHairDatas.HairGroups.Num()))
			{
				FHairStrandsProjectionHairData::HairGroup& ProjectionHair = E.RenProjectionHairDatas.HairGroups[GroupIt];
				GroupInfo.LODCount = ProjectionHair.LODDatas.Num();
				GroupInfo.bHasSkinInterpolation = ProjectionHair.LODDatas.Num() > 0;
			}
			else
			{
				GroupInfo.LODCount = 0;
				GroupInfo.bHasSkinInterpolation = false;
			}
		}
	}

	return Infos;
}

bool IsHairStrandsEnable(EShaderPlatform Platform) 
{ 
	return 
		IsHairStrandsSupported(Platform) && 
		GHairStrandsRenderingEnable == 1 && 
		GHairManager.Elements.Num() > 0; 
}