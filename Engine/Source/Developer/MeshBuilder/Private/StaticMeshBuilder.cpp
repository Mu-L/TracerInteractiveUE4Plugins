// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "MeshAttributes.h"
#include "MeshDescriptionHelper.h"
#include "BuildOptimizationHelper.h"
#include "Components.h"
#include "IMeshReductionManagerModule.h"
#include "MeshBuild.h"
#include "Modules/ModuleManager.h"
#include "IMeshReductionInterfaces.h"

DEFINE_LOG_CATEGORY(LogStaticMeshBuilder);

//////////////////////////////////////////////////////////////////////////
//Local functions definition
void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, const FMeshDescription& MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const FOverlappingCorners& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts);
void BuildAllBufferOptimizations(struct FStaticMeshLODResources& StaticMeshLOD, const struct FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices);
//////////////////////////////////////////////////////////////////////////

FStaticMeshBuilder::FStaticMeshBuilder()
{

}

static bool UseNativeQuadraticReduction()
{
	// Are we using our tool, or simplygon?  The tool is only changed during editor restarts
	IMeshReduction* ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetStaticMeshReductionInterface();

	FString VersionString = ReductionModule->GetVersionString();
	TArray<FString> SplitVersionString;
	VersionString.ParseIntoArray(SplitVersionString, TEXT("_"), true);

	bool bUseQuadricSimplier = SplitVersionString[0].Equals("QuadricMeshReduction");
	return bUseQuadricSimplier;
}

bool FStaticMeshBuilder::Build(FStaticMeshRenderData& StaticMeshRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup)
{
	// The tool can only been switch by restarting the editor
	static bool bIsThirdPartyReductiontool = !UseNativeQuadraticReduction();

	if (!StaticMesh->IsMeshDescriptionValid(0))
	{
		//Warn the user that there is no mesh description data
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot find a valid mesh description to build the asset."));
		return false;
	}

	if (StaticMeshRenderData.LODResources.Num() > 0)
	{
		//At this point the render data is suppose to be empty
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot build static mesh render data twice [%s]."), *StaticMesh->GetFullName());
		
		//Crash in debug
		checkSlow(StaticMeshRenderData.LODResources.Num() == 0);

		return false;
	}

	const int32 NumSourceModels = StaticMesh->GetNumSourceModels();
	StaticMeshRenderData.AllocateLODResources(NumSourceModels);

	TArray<FMeshDescription> MeshDescriptions;
	MeshDescriptions.SetNum(NumSourceModels);

	const FMeshSectionInfoMap BeforeBuildSectionInfoMap = StaticMesh->GetSectionInfoMap();
	const FMeshSectionInfoMap BeforeBuildOriginalSectionInfoMap = StaticMesh->GetOriginalSectionInfoMap();

	for (int32 LodIndex = 0; LodIndex < NumSourceModels; ++LodIndex)
	{
		FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LodIndex);

		float MaxDeviation = 0.0f;
		FMeshBuildSettings& LODBuildSettings = SrcModel.BuildSettings;
		const FMeshDescription* OriginalMeshDescription = StaticMesh->GetMeshDescription(LodIndex);
		FMeshDescriptionHelper MeshDescriptionHelper(&LODBuildSettings);

		FMeshReductionSettings ReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LodIndex);

		//Make sure we do not reduce a non custom LOD by himself
		const int32 BaseReduceLodIndex = FMath::Clamp<int32>(ReductionSettings.BaseLODModel, 0, OriginalMeshDescription == nullptr ? LodIndex - 1 : LodIndex);
		// Use simplifier if a reduction in triangles or verts has been requested.
		bool bUseReduction = StaticMesh->IsReductionActive(LodIndex);

		if (OriginalMeshDescription != nullptr)
		{
			MeshDescriptionHelper.GetRenderMeshDescription(StaticMesh, *OriginalMeshDescription, MeshDescriptions[LodIndex]);
		}
		else
		{
			if (bUseReduction)
			{
				// Initialize an empty mesh description that the reduce will fill
				UStaticMesh::RegisterMeshAttributes(MeshDescriptions[LodIndex]);
			}
			else
			{
				//Duplicate the lodindex 0 we have a 100% reduction which is like a duplicate
				MeshDescriptions[LodIndex] = MeshDescriptions[BaseReduceLodIndex];
				//Set the overlapping threshold
				float ComparisonThreshold = StaticMesh->GetSourceModel(BaseReduceLodIndex).BuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
				MeshDescriptionHelper.FindOverlappingCorners(MeshDescriptions[LodIndex], ComparisonThreshold);
				if (LodIndex > 0)
				{
					
					//Make sure the SectionInfoMap is taken from the Base RawMesh
					int32 SectionNumber = StaticMesh->GetOriginalSectionInfoMap().GetSectionNumber(BaseReduceLodIndex);
					for (int32 SectionIndex = 0; SectionIndex < SectionNumber; ++SectionIndex)
					{
						//Keep the old data if its valid
						bool bHasValidLODInfoMap = StaticMesh->GetSectionInfoMap().IsValidSection(LodIndex, SectionIndex);
						//Section material index have to be remap with the ReductionSettings.BaseLODModel SectionInfoMap to create
						//a valid new section info map for the reduced LOD.
						if (!bHasValidLODInfoMap && StaticMesh->GetSectionInfoMap().IsValidSection(BaseReduceLodIndex, SectionIndex))
						{
							//Copy the BaseLODModel section info to the reduce LODIndex.
							FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(BaseReduceLodIndex, SectionIndex);
							FMeshSectionInfo OriginalSectionInfo = StaticMesh->GetOriginalSectionInfoMap().Get(BaseReduceLodIndex, SectionIndex);
							StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
							StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, SectionIndex, OriginalSectionInfo);
						}
					}
				}
			}

			if (LodIndex > 0)
			{
				LODBuildSettings = StaticMesh->GetSourceModel(BaseReduceLodIndex).BuildSettings;
			}
		}

		//Reduce LODs
		if (bUseReduction)
		{
			float OverlappingThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
			FOverlappingCorners OverlappingCorners;
			FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, MeshDescriptions[BaseReduceLodIndex], OverlappingThreshold);

			int32 OldSectionInfoMapCount = StaticMesh->GetSectionInfoMap().GetSectionNumber(LodIndex);

			if (LodIndex == BaseReduceLodIndex)
			{
				//When using LOD 0, we use a copy of the mesh description since reduce do not support inline reducing
				FMeshDescription BaseMeshDescription = MeshDescriptions[BaseReduceLodIndex];
				MeshDescriptionHelper.ReduceLOD(BaseMeshDescription, MeshDescriptions[LodIndex], ReductionSettings, OverlappingCorners, MaxDeviation);
			}
			else
			{
				MeshDescriptionHelper.ReduceLOD(MeshDescriptions[BaseReduceLodIndex], MeshDescriptions[LodIndex], ReductionSettings, OverlappingCorners, MaxDeviation);
			}
			

			const TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescriptions[LodIndex].PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
			const TPolygonGroupAttributesRef<FName> BasePolygonGroupImportedMaterialSlotNames = MeshDescriptions[BaseReduceLodIndex].PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
			// Recompute adjacency information. Since we change the vertices when we reduce
			MeshDescriptionHelper.FindOverlappingCorners(MeshDescriptions[LodIndex], OverlappingThreshold);
			
			//Make sure the static mesh SectionInfoMap is up to date with the new reduce LOD
			//We have to remap the material index with the ReductionSettings.BaseLODModel sectionInfoMap
			//Set the new SectionInfoMap for this reduced LOD base on the ReductionSettings.BaseLODModel SectionInfoMap
			TArray<int32> BaseUniqueMaterialIndexes;
			//Find all unique Material in used order
			for (const FPolygonGroupID& PolygonGroupID : MeshDescriptions[BaseReduceLodIndex].PolygonGroups().GetElementIDs())
			{
				int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(BasePolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = PolygonGroupID.GetValue();
				}
				BaseUniqueMaterialIndexes.AddUnique(MaterialIndex);
			}
			TArray<int32> UniqueMaterialIndex;
			//Find all unique Material in used order
			for (const FPolygonGroupID& PolygonGroupID : MeshDescriptions[LodIndex].PolygonGroups().GetElementIDs())
			{
				int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = PolygonGroupID.GetValue();
				}
				UniqueMaterialIndex.AddUnique(MaterialIndex);
			}

			//If the reduce did not output the same number of section use the base LOD sectionInfoMap
			bool bIsOldMappingInvalid = OldSectionInfoMapCount != MeshDescriptions[LodIndex].PolygonGroups().Num();

			bool bValidBaseSectionInfoMap = BeforeBuildSectionInfoMap.GetSectionNumber(BaseReduceLodIndex) > 0;
			//All used material represent a different section
			for (int32 SectionIndex = 0; SectionIndex < UniqueMaterialIndex.Num(); ++SectionIndex)
			{
				//Keep the old data
				bool bHasValidLODInfoMap = !bIsOldMappingInvalid && BeforeBuildSectionInfoMap.IsValidSection(LodIndex, SectionIndex);
				//Section material index have to be remap with the ReductionSettings.BaseLODModel SectionInfoMap to create
				//a valid new section info map for the reduced LOD.

				//Find the base LOD section using this material
				if (!bHasValidLODInfoMap)
				{
					bool bSectionInfoSet = false;
					if (bValidBaseSectionInfoMap)
					{
						for (int32 BaseSectionIndex = 0; BaseSectionIndex < BaseUniqueMaterialIndexes.Num(); ++BaseSectionIndex)
						{
							if (UniqueMaterialIndex[SectionIndex] == BaseUniqueMaterialIndexes[BaseSectionIndex])
							{
								//Copy the base sectionInfoMap
								FMeshSectionInfo SectionInfo = BeforeBuildSectionInfoMap.Get(BaseReduceLodIndex, BaseSectionIndex);
								FMeshSectionInfo OriginalSectionInfo = BeforeBuildOriginalSectionInfoMap.Get(BaseReduceLodIndex, BaseSectionIndex);
								StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
								StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, BaseSectionIndex, OriginalSectionInfo);
								bSectionInfoSet = true;
								break;
							}
						}
					}

					if (!bSectionInfoSet)
					{
						//Just set the default section info in case we did not found any match with the Base Lod
						FMeshSectionInfo SectionInfo;
						SectionInfo.MaterialIndex = SectionIndex;
						StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
						StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
					}
				}
			}
		}

		const FPolygonGroupArray& PolygonGroups = MeshDescriptions[LodIndex].PolygonGroups();

		FStaticMeshLODResources& StaticMeshLOD = StaticMeshRenderData.LODResources[LodIndex];
		StaticMeshLOD.MaxDeviation = MaxDeviation;

		//discover degenerate triangle with this threshold
		float VertexComparisonThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;

		//Build new vertex buffers
		TArray< FStaticMeshBuildVertex > StaticMeshBuildVertices;

		TArray< uint32 > IndexBuffer;

		StaticMeshLOD.Sections.Empty(PolygonGroups.Num());
		TArray<int32> RemapVerts; //Because we will remove MeshVertex that are redundant, we need a remap
								  //Render data Wedge map is only set for LOD 0???
		TArray<int32> TempWedgeMap;
		TArray<int32> &WedgeMap = (LodIndex == 0) ? StaticMeshRenderData.WedgeMap : TempWedgeMap;

		//Prepare the PerSectionIndices array so we can optimize the index buffer for the GPU
		TArray<TArray<uint32> > PerSectionIndices;
		PerSectionIndices.AddDefaulted(MeshDescriptions[LodIndex].PolygonGroups().Num());

		//Build the vertex and index buffer
		BuildVertexBuffer(StaticMesh, LodIndex, MeshDescriptions[LodIndex], StaticMeshLOD, LODBuildSettings, IndexBuffer, WedgeMap, PerSectionIndices, StaticMeshBuildVertices, MeshDescriptionHelper.GetOverlappingCorners(), VertexComparisonThreshold, RemapVerts);

		// Concatenate the per-section index buffers.
		TArray<uint32> CombinedIndices;
		bool bNeeds32BitIndices = false;
		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); SectionIndex++)
		{
			FStaticMeshSection& Section = StaticMeshLOD.Sections[SectionIndex];
			TArray<uint32> const& SectionIndices = PerSectionIndices[SectionIndex];
			Section.FirstIndex = 0;
			Section.NumTriangles = 0;
			Section.MinVertexIndex = 0;
			Section.MaxVertexIndex = 0;

			if (SectionIndices.Num())
			{
				Section.FirstIndex = CombinedIndices.Num();
				Section.NumTriangles = SectionIndices.Num() / 3;

				CombinedIndices.AddUninitialized(SectionIndices.Num());
				uint32* DestPtr = &CombinedIndices[Section.FirstIndex];
				uint32 const* SrcPtr = SectionIndices.GetData();

				Section.MinVertexIndex = *SrcPtr;
				Section.MaxVertexIndex = *SrcPtr;

				for (int32 Index = 0; Index < SectionIndices.Num(); Index++)
				{
					uint32 VertIndex = *SrcPtr++;

					bNeeds32BitIndices |= (VertIndex > MAX_uint16);
					Section.MinVertexIndex = FMath::Min<uint32>(VertIndex, Section.MinVertexIndex);
					Section.MaxVertexIndex = FMath::Max<uint32>(VertIndex, Section.MaxVertexIndex);
					*DestPtr++ = VertIndex;
				}
			}
		}

		const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;
		StaticMeshLOD.IndexBuffer.SetIndices(CombinedIndices, IndexBufferStride);

		BuildAllBufferOptimizations(StaticMeshLOD, LODBuildSettings, CombinedIndices, bNeeds32BitIndices, StaticMeshBuildVertices);
	} //End of LOD for loop

	// Calculate the bounding box.
	FBox BoundingBox(ForceInit);
	FPositionVertexBuffer& BasePositionVertexBuffer = StaticMeshRenderData.LODResources[0].VertexBuffers.PositionVertexBuffer;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		BoundingBox += BasePositionVertexBuffer.VertexPosition(VertexIndex);
	}
	BoundingBox.GetCenterAndExtents(StaticMeshRenderData.Bounds.Origin, StaticMeshRenderData.Bounds.BoxExtent);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	StaticMeshRenderData.Bounds.SphereRadius = 0.0f;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		StaticMeshRenderData.Bounds.SphereRadius = FMath::Max(
			(BasePositionVertexBuffer.VertexPosition(VertexIndex) - StaticMeshRenderData.Bounds.Origin).Size(),
			StaticMeshRenderData.Bounds.SphereRadius
		);
	}

	return true;
}

bool AreVerticesEqual(FStaticMeshBuildVertex const& A, FStaticMeshBuildVertex const& B, float ComparisonThreshold)
{
	if (   !A.Position.Equals(B.Position, ComparisonThreshold)
		|| !NormalsEqual(A.TangentX, B.TangentX)
		|| !NormalsEqual(A.TangentY, B.TangentY)
		|| !NormalsEqual(A.TangentZ, B.TangentZ)
		|| A.Color != B.Color)
	{
		return false;
	}

	// UVs
	for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; UVIndex++)
	{
		if (!UVsEqual(A.UVs[UVIndex], B.UVs[UVIndex]))
		{
			return false;
		}
	}

	return true;
}

void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, const FMeshDescription& MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const FOverlappingCorners& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts)
{
	const FVertexArray& Vertices = MeshDescription.Vertices();
	const FVertexInstanceArray& VertexInstances = MeshDescription.VertexInstances();
	const FPolygonGroupArray& PolygonGroupArray = MeshDescription.PolygonGroups();
	const FPolygonArray& PolygonArray = MeshDescription.Polygons();
	TArray<int32> RemapVertexInstanceID;
	// set up vertex buffer elements
	StaticMeshBuildVertices.Reserve(VertexInstances.GetArraySize());
	bool bHasColor = false;

	TPolygonGroupAttributesConstRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>( MeshAttribute::VertexInstance::Normal );
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>( MeshAttribute::VertexInstance::Tangent );
	TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>( MeshAttribute::VertexInstance::BinormalSign );
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>( MeshAttribute::VertexInstance::Color );
	TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>( MeshAttribute::VertexInstance::TextureCoordinate );

	const uint32 NumTextureCoord = VertexInstanceUVs.GetNumIndices();

	TMap<FPolygonGroupID, int32> PolygonGroupToSectionIndex;
	
	
	for (const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
	{
		int32& SectionIndex = PolygonGroupToSectionIndex.FindOrAdd(PolygonGroupID);
		SectionIndex = StaticMeshLOD.Sections.Add(FStaticMeshSection());
		FStaticMeshSection& StaticMeshSection = StaticMeshLOD.Sections[SectionIndex];
		StaticMeshSection.MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
		if (StaticMeshSection.MaterialIndex == INDEX_NONE)
		{
			StaticMeshSection.MaterialIndex = PolygonGroupID.GetValue();
		}
	}

	int32 ReserveIndicesCount = 0;
	for (const FPolygonID& PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		const TArray<FMeshTriangle>& PolygonTriangles = MeshDescription.GetPolygonTriangles(PolygonID);
		ReserveIndicesCount += PolygonTriangles.Num() * 3;
	}
	IndexBuffer.Reset(ReserveIndicesCount);

	//Fill the remap array
	RemapVerts.AddZeroed(ReserveIndicesCount);
	for (int32& RemapIndex : RemapVerts)
	{
		RemapIndex = INDEX_NONE;
	}

	//Initialize the wedge map array tracking correspondence between wedge index and rendering vertex index
	OutWedgeMap.Reset();
	OutWedgeMap.AddZeroed(ReserveIndicesCount);

	int32 WedgeIndex = 0;
	for (const FPolygonID& PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
		const int32 SectionIndex = PolygonGroupToSectionIndex[PolygonGroupID];
		TArray<uint32>& SectionIndices = OutPerSectionIndices[SectionIndex];

		const TArray<FMeshTriangle>& PolygonTriangles = MeshDescription.GetPolygonTriangles(PolygonID);
		uint32 MinIndex = TNumericLimits< uint32 >::Max();
		uint32 MaxIndex = TNumericLimits< uint32 >::Min();
		for (int32 TriangleIndex = 0; TriangleIndex < PolygonTriangles.Num(); ++TriangleIndex)
		{
			const FMeshTriangle& Triangle = PolygonTriangles[TriangleIndex];

			FVector CornerPositions[3];
			for (int32 TriVert = 0; TriVert < 3; ++TriVert)
			{
				const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(TriVert);
				const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
				CornerPositions[TriVert] = VertexPositions[VertexID];
			}
			FOverlappingThresholds OverlappingThresholds;
			OverlappingThresholds.ThresholdPosition = VertexComparisonThreshold;
			// Don't process degenerate triangles.
			if (PointsEqual(CornerPositions[0], CornerPositions[1], OverlappingThresholds)
				|| PointsEqual(CornerPositions[0], CornerPositions[2], OverlappingThresholds)
				|| PointsEqual(CornerPositions[1], CornerPositions[2], OverlappingThresholds))
			{
				continue;
			}

			for (int32 TriVert = 0; TriVert < 3; ++TriVert, ++WedgeIndex)
			{
				const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(TriVert);
				const int32 VertexInstanceValue = VertexInstanceID.GetValue();
				const FVector& VertexPosition = CornerPositions[TriVert];
				const FVector& VertexInstanceNormal = VertexInstanceNormals[VertexInstanceID];
				const FVector& VertexInstanceTangent = VertexInstanceTangents[VertexInstanceID];
				const float VertexInstanceBinormalSign = VertexInstanceBinormalSigns[VertexInstanceID];
				const FVector4& VertexInstanceColor = VertexInstanceColors[VertexInstanceID];

				const FLinearColor LinearColor(VertexInstanceColor);
				if (LinearColor != FLinearColor::White)
				{
					bHasColor = true;
				}

				FStaticMeshBuildVertex StaticMeshVertex;

				StaticMeshVertex.Position = VertexPosition * LODBuildSettings.BuildScale3D;
				const FMatrix ScaleMatrix = FScaleMatrix(LODBuildSettings.BuildScale3D).Inverse().GetTransposed();
				StaticMeshVertex.TangentX = ScaleMatrix.TransformVector(VertexInstanceTangent).GetSafeNormal();
				StaticMeshVertex.TangentY = ScaleMatrix.TransformVector(FVector::CrossProduct(VertexInstanceNormal, VertexInstanceTangent).GetSafeNormal() * VertexInstanceBinormalSign).GetSafeNormal();
				StaticMeshVertex.TangentZ = ScaleMatrix.TransformVector(VertexInstanceNormal).GetSafeNormal();
				StaticMeshVertex.Color = LinearColor.ToFColor(true);
				const uint32 MaxNumTexCoords = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, MAX_STATIC_TEXCOORDS);
				for (uint32 UVIndex = 0; UVIndex < MaxNumTexCoords; ++UVIndex)
				{
					if(UVIndex < NumTextureCoord)
					{
						StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
					}
					else
					{
						StaticMeshVertex.UVs[UVIndex] = FVector2D(0.0f, 0.0f);
					}
				}
					

				//Never add duplicated vertex instance
				//Use WedgeIndex since OverlappingCorners has been built based on that
				const TArray<int32>& DupVerts = OverlappingCorners.FindIfOverlapping(WedgeIndex);

				int32 Index = INDEX_NONE;
				for (int32 k = 0; k < DupVerts.Num(); k++)
				{
					if (DupVerts[k] >= WedgeIndex)
					{
						break;
					}
					int32 Location = RemapVerts.IsValidIndex(DupVerts[k]) ? RemapVerts[DupVerts[k]] : INDEX_NONE;
					if (Location != INDEX_NONE && AreVerticesEqual(StaticMeshVertex, StaticMeshBuildVertices[Location], VertexComparisonThreshold))
					{
						Index = Location;
						break;
					}
				}
				if (Index == INDEX_NONE)
				{
					Index = StaticMeshBuildVertices.Add(StaticMeshVertex);
				}
				RemapVerts[WedgeIndex] = Index;
				const uint32 RenderingVertexIndex = Index;
				IndexBuffer.Add(RenderingVertexIndex);
				OutWedgeMap[WedgeIndex] = RenderingVertexIndex;
				SectionIndices.Add(RenderingVertexIndex);
			}
		}
	}


	//Optimize before setting the buffer
	if (VertexInstances.GetArraySize() < 100000 * 3)
	{
		BuildOptimizationHelper::CacheOptimizeVertexAndIndexBuffer(StaticMeshBuildVertices, OutPerSectionIndices, OutWedgeMap);
		//check(OutWedgeMap.Num() == MeshDescription->VertexInstances().Num());
	}

	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(LODBuildSettings.bUseHighPrecisionTangentBasis);
	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(LODBuildSettings.bUseFullPrecisionUVs);
	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, NumTextureCoord);
	StaticMeshLOD.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices);
	StaticMeshLOD.VertexBuffers.ColorVertexBuffer.Init(StaticMeshBuildVertices);
}

void BuildAllBufferOptimizations(FStaticMeshLODResources& StaticMeshLOD, const FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices)
{
	if (StaticMeshLOD.AdditionalIndexBuffers == nullptr)
	{
		StaticMeshLOD.AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
	}

	const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;

	// Build the reversed index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> InversedIndices;
		const int32 IndexCount = IndexBuffer.Num();
		InversedIndices.AddUninitialized(IndexCount);

		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& SectionInfo = StaticMeshLOD.Sections[SectionIndex];
			const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;

			for (int32 i = 0; i < SectionIndexCount; ++i)
			{
				InversedIndices[SectionInfo.FirstIndex + i] = IndexBuffer[SectionInfo.FirstIndex + SectionIndexCount - 1 - i];
			}
		}
		StaticMeshLOD.AdditionalIndexBuffers->ReversedIndexBuffer.SetIndices(InversedIndices, IndexBufferStride);
	}

	// Build the depth-only index buffer.
	TArray<uint32> DepthOnlyIndices;
	{
		BuildOptimizationHelper::BuildDepthOnlyIndexBuffer(
			DepthOnlyIndices,
			StaticMeshBuildVertices,
			IndexBuffer,
			StaticMeshLOD.Sections
		);

		if (DepthOnlyIndices.Num() < 50000 * 3)
		{
			BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(DepthOnlyIndices);
		}

		StaticMeshLOD.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndices, IndexBufferStride);
	}

	// Build the inversed depth only index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> ReversedDepthOnlyIndices;
		const int32 IndexCount = DepthOnlyIndices.Num();
		ReversedDepthOnlyIndices.AddUninitialized(IndexCount);
		for (int32 i = 0; i < IndexCount; ++i)
		{
			ReversedDepthOnlyIndices[i] = DepthOnlyIndices[IndexCount - 1 - i];
		}
		StaticMeshLOD.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.SetIndices(ReversedDepthOnlyIndices, IndexBufferStride);
	}

	// Build a list of wireframe edges in the static mesh.
	{
		TArray<BuildOptimizationHelper::FMeshEdge> Edges;
		TArray<uint32> WireframeIndices;

		BuildOptimizationHelper::FStaticMeshEdgeBuilder(IndexBuffer, StaticMeshBuildVertices, Edges).FindEdges();
		WireframeIndices.Empty(2 * Edges.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); EdgeIndex++)
		{
			BuildOptimizationHelper::FMeshEdge&	Edge = Edges[EdgeIndex];
			WireframeIndices.Add(Edge.Vertices[0]);
			WireframeIndices.Add(Edge.Vertices[1]);
		}
		StaticMeshLOD.AdditionalIndexBuffers->WireframeIndexBuffer.SetIndices(WireframeIndices, IndexBufferStride);
	}

	// Build the adjacency index buffer used for tessellation.
	if (LODBuildSettings.bBuildAdjacencyBuffer)
	{
		TArray<uint32> AdjacencyIndices;

		BuildOptimizationThirdParty::NvTriStripHelper::BuildStaticAdjacencyIndexBuffer(
			StaticMeshLOD.VertexBuffers.PositionVertexBuffer,
			StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer,
			IndexBuffer,
			AdjacencyIndices
		);
		StaticMeshLOD.AdditionalIndexBuffers->AdjacencyIndexBuffer.SetIndices(AdjacencyIndices, IndexBufferStride);
	}
}
