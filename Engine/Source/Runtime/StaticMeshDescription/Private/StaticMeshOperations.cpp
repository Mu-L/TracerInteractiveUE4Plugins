// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshOperations.h"

#include "StaticMeshAttributes.h"
#include "UVMapSettings.h"

#include "Async/ParallelFor.h"
#include "LayoutUV.h"
#include "MeshUtilitiesCommon.h"
#include "Misc/SecureHash.h"
#include "OverlappingCorners.h"
#include "RawMesh.h"

#if WITH_MIKKTSPACE
#include "mikktspace.h"
#endif //WITH_MIKKTSPACE

DEFINE_LOG_CATEGORY(LogStaticMeshOperations);

#define LOCTEXT_NAMESPACE "StaticMeshOperations"

static bool GetPolygonTangentsAndNormals(FMeshDescription& MeshDescription,
										 FPolygonID PolygonID,
										 float ComparisonThreshold, 
										 TVertexAttributesConstRef<const FVector> VertexPositions,
										 TVertexInstanceAttributesConstRef<const FVector2D> VertexUVs,
										 TPolygonAttributesRef<FVector> PolygonNormals,
										 TPolygonAttributesRef<FVector> PolygonTangents,
										 TPolygonAttributesRef<FVector> PolygonBinormals,
										 TPolygonAttributesRef<FVector> PolygonCenters)
{
	bool bValidNTBs = true;

	// Calculate the tangent basis for the polygon, based on the average of all constituent triangles
	FVector Normal(FVector::ZeroVector);
	FVector Tangent(FVector::ZeroVector);
	FVector Binormal(FVector::ZeroVector);
	FVector Center(FVector::ZeroVector);

	// Calculate the center of this polygon
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(PolygonID);
	for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
	{
		Center += VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceID)];
	}
	Center /= float(VertexInstanceIDs.Num());

	float AdjustedComparisonThreshold = FMath::Max(ComparisonThreshold, MIN_flt);
	for (const FTriangleID TriangleID : MeshDescription.GetPolygonTriangleIDs(PolygonID))
	{
		TArrayView<const FVertexInstanceID> TriangleVertexInstances = MeshDescription.GetTriangleVertexInstances(TriangleID);
		const FVertexID VertexID0 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[0]);
		const FVertexID VertexID1 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[1]);
		const FVertexID VertexID2 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[2]);

		const FVector Position0 = VertexPositions[VertexID0];
		const FVector DPosition1 = VertexPositions[VertexID1] - Position0;
		const FVector DPosition2 = VertexPositions[VertexID2] - Position0;

		const FVector2D UV0 = VertexUVs[TriangleVertexInstances[0]];
		const FVector2D DUV1 = VertexUVs[TriangleVertexInstances[1]] - UV0;
		const FVector2D DUV2 = VertexUVs[TriangleVertexInstances[2]] - UV0;

		// We have a left-handed coordinate system, but a counter-clockwise winding order
		// Hence normal calculation has to take the triangle vectors cross product in reverse.
		FVector TmpNormal = FVector::CrossProduct(DPosition2, DPosition1).GetSafeNormal(AdjustedComparisonThreshold);
		if (!TmpNormal.IsNearlyZero(ComparisonThreshold))
		{
			FMatrix	ParameterToLocal(
				DPosition1,
				DPosition2,
				Position0,
				FVector::ZeroVector
			);

			FMatrix ParameterToTexture(
				FPlane(DUV1.X, DUV1.Y, 0, 0),
				FPlane(DUV2.X, DUV2.Y, 0, 0),
				FPlane(UV0.X, UV0.Y, 1, 0),
				FPlane(0, 0, 0, 1)
			);

			// Use InverseSlow to catch singular matrices.  Inverse can miss this sometimes.
			const FMatrix TextureToLocal = ParameterToTexture.Inverse() * ParameterToLocal;

			FVector TmpTangent = TextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal();
			FVector TmpBinormal = TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal();
			FVector::CreateOrthonormalBasis(TmpTangent, TmpBinormal, TmpNormal);

			if (TmpTangent.IsNearlyZero() || TmpTangent.ContainsNaN()
				|| TmpBinormal.IsNearlyZero() || TmpBinormal.ContainsNaN())
			{
				TmpTangent = FVector::ZeroVector;
				TmpBinormal = FVector::ZeroVector;
				bValidNTBs = false;
			}

			if (TmpNormal.IsNearlyZero() || TmpNormal.ContainsNaN())
			{
				TmpNormal = FVector::ZeroVector;
				bValidNTBs = false;
			}

			Normal += TmpNormal;
			Tangent += TmpTangent;
			Binormal += TmpBinormal;
		}
		else
		{
			//This will force a recompute of the normals and tangents
			Normal = FVector::ZeroVector;
			Tangent = FVector::ZeroVector;
			Binormal = FVector::ZeroVector;

			// The polygon is degenerated
			bValidNTBs = false;
		}
	}

	PolygonNormals[PolygonID] = Normal.GetSafeNormal();
	PolygonTangents[PolygonID] = Tangent.GetSafeNormal();
	PolygonBinormals[PolygonID] = Binormal.GetSafeNormal();
	PolygonCenters[PolygonID] = Center;

	return bValidNTBs;
}

void FStaticMeshOperations::ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, float ComparisonThreshold)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FStaticMeshOperations::ComputePolygonTangentsAndNormals_Selection"));

	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.RegisterPolygonNormalAndTangentAttributes();

	TArray<FPolygonID> PolygonIDs;
	PolygonIDs.Reserve(MeshDescription.Polygons().Num());
	for (const FPolygonID& Polygon : MeshDescription.Polygons().GetElementIDs())
	{
		PolygonIDs.Add(Polygon);
	}

	// Split work in batch to reduce call overhead
	const int32 BatchSize = 8 * 1024;
	const int32 BatchCount = 1 + PolygonIDs.Num() / BatchSize;

	ParallelFor(BatchCount,
		[&PolygonIDs, &BatchSize, &ComparisonThreshold, &MeshDescription, &Attributes](int32 BatchIndex)
		{
			TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesConstRef<FVector2D> VertexUVs = Attributes.GetVertexInstanceUVs();
			TPolygonAttributesRef<FVector> PolygonNormals = Attributes.GetPolygonNormals();
			TPolygonAttributesRef<FVector> PolygonTangents = Attributes.GetPolygonTangents();
			TPolygonAttributesRef<FVector> PolygonBinormals = Attributes.GetPolygonBinormals();
			TPolygonAttributesRef<FVector> PolygonCenters = Attributes.GetPolygonCenters();

			FVertexInstanceArray& VertexInstanceArray = MeshDescription.VertexInstances();
			FVertexArray& VertexArray = MeshDescription.Vertices();
			FPolygonArray& PolygonArray = MeshDescription.Polygons();

			int32 Indice = BatchIndex * BatchSize;
			int32 LastIndice = FMath::Min(Indice + BatchSize, PolygonIDs.Num());
			for (; Indice < LastIndice; ++Indice)
			{
				const FPolygonID PolygonID = PolygonIDs[Indice];

				if (!PolygonNormals[PolygonID].IsNearlyZero())
				{
					//By pass normal calculation if its already done
					continue;
				}

				GetPolygonTangentsAndNormals(MeshDescription, PolygonID, ComparisonThreshold, VertexPositions, VertexUVs, PolygonNormals, PolygonTangents, PolygonBinormals, PolygonCenters);
			}
		}
	);
}

void FStaticMeshOperations::DetermineEdgeHardnessesFromVertexInstanceNormals(FMeshDescription& MeshDescription, float Tolerance)
{
	FStaticMeshAttributes Attributes(MeshDescription);

	TVertexInstanceAttributesRef<const FVector> VertexNormals = Attributes.GetVertexInstanceNormals();
	TEdgeAttributesRef<bool> EdgeHardnesses = Attributes.GetEdgeHardnesses();

	// Holds unique vertex instance IDs for a given edge vertex
	TArray<FVertexInstanceID> UniqueVertexInstanceIDs;

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		// Get list of polygons connected to this edge
		TArray<FPolygonID, TInlineAllocator<2>> ConnectedPolygonIDs = MeshDescription.GetEdgeConnectedPolygons<TInlineAllocator<2>>(EdgeID);
		if (ConnectedPolygonIDs.Num() == 0)
		{
			// What does it mean if an edge has no connected polygons? For now we just skip it
			continue;
		}

		// Assume by default that the edge is soft - but as soon as any vertex instance belonging to a connected polygon
		// has a distinct normal from the others (within the given tolerance), we mark it as hard.
		// The exception is if an edge has exactly one connected polygon: in this case we automatically deem it a hard edge.
		bool bEdgeIsHard = (ConnectedPolygonIDs.Num() == 1);

		// Examine vertices on each end of the edge, if we haven't yet identified it as 'hard'
		for (int32 VertexIndex = 0; !bEdgeIsHard && VertexIndex < 2; ++VertexIndex)
		{
			const FVertexID VertexID = MeshDescription.GetEdgeVertex(EdgeID, VertexIndex);

			const int32 ReservedElements = 4;
			UniqueVertexInstanceIDs.Reset(ReservedElements);

			// Get a list of all vertex instances for this vertex which form part of any polygon connected to the edge
			for (const FVertexInstanceID VertexInstanceID : MeshDescription.GetVertexVertexInstances(VertexID))
			{
				for (const FPolygonID PolygonID : MeshDescription.GetVertexInstanceConnectedPolygons<TInlineAllocator<8>>(VertexInstanceID))
				{
					if (ConnectedPolygonIDs.Contains(PolygonID))
					{
						UniqueVertexInstanceIDs.AddUnique(VertexInstanceID);
						break;
					}
				}
			}
			check(UniqueVertexInstanceIDs.Num() > 0);

			// First unique vertex instance is used as a reference against which the others are compared.
			// (not a perfect approach: really the 'median' should be used as a reference)
			const FVector ReferenceNormal = VertexNormals[UniqueVertexInstanceIDs[0]];
			for (int32 Index = 1; Index < UniqueVertexInstanceIDs.Num(); ++Index)
			{
				if (!VertexNormals[UniqueVertexInstanceIDs[Index]].Equals(ReferenceNormal, Tolerance))
				{
					bEdgeIsHard = true;
					break;
				}
			}
		}

		EdgeHardnesses[EdgeID] = bEdgeIsHard;
	}
}


//////////////////

struct FVertexInfo
{
	FVertexInfo()
	{
		TriangleID = FTriangleID::Invalid;
		VertexInstanceID = FVertexInstanceID::Invalid;
		UVs = FVector2D(0.0f, 0.0f);
	}

	FTriangleID TriangleID;
	FVertexInstanceID VertexInstanceID;
	FVector2D UVs;
	//Most of the time a edge has two triangles
	TArray<FEdgeID, TInlineAllocator<2>> EdgeIDs;
};

/** Helper struct for building acceleration structures. */
namespace MeshDescriptionOperationNamespace
{
	struct FIndexAndZ
	{
		float Z;
		int32 Index;
		const FVector *OriginalVector;

		/** Default constructor. */
		FIndexAndZ() {}

		/** Initialization constructor. */
		FIndexAndZ(int32 InIndex, const FVector& V)
		{
			Z = 0.30f * V.X + 0.33f * V.Y + 0.37f * V.Z;
			Index = InIndex;
			OriginalVector = &V;
		}
	};
	/** Sorting function for vertex Z/index pairs. */
	struct FCompareIndexAndZ
	{
		FORCEINLINE bool operator()(const FIndexAndZ& A, const FIndexAndZ& B) const { return A.Z < B.Z; }
	};
}

void FStaticMeshOperations::ConvertHardEdgesToSmoothGroup(const FMeshDescription& SourceMeshDescription, TArray<uint32>& FaceSmoothingMasks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::ConvertHardEdgesToSmoothGroup);

	TMap<FPolygonID, uint32> PolygonSmoothGroup;
	PolygonSmoothGroup.Reserve(SourceMeshDescription.Polygons().GetArraySize());
	TArray<bool> ConsumedPolygons;
	ConsumedPolygons.AddZeroed(SourceMeshDescription.Polygons().GetArraySize());

	TMap < FPolygonID, uint32> PolygonAvoidances;

	TEdgeAttributesConstRef<bool> EdgeHardnesses = SourceMeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	int32 TriangleCount = 0;
	TArray<FPolygonID> SoftEdgeNeigbors;
	TArray<FEdgeID> PolygonEdges;
	TArray<FPolygonID> EdgeConnectedPolygons;
	TArray<FPolygonID> ConnectedPolygons;
	TArray<FPolygonID> LastConnectedPolygons;

	for (const FPolygonID PolygonID : SourceMeshDescription.Polygons().GetElementIDs())
	{
		TriangleCount += SourceMeshDescription.GetPolygonTriangleIDs(PolygonID).Num();
		if (ConsumedPolygons[PolygonID.GetValue()])
		{
			continue;
		}

		ConnectedPolygons.Reset();
		LastConnectedPolygons.Reset();
		ConnectedPolygons.Add(PolygonID);
		LastConnectedPolygons.Add(FPolygonID::Invalid);
		while (ConnectedPolygons.Num() > 0)
		{
			check(LastConnectedPolygons.Num() == ConnectedPolygons.Num());
			FPolygonID LastPolygonID = LastConnectedPolygons.Pop(false);
			FPolygonID CurrentPolygonID = ConnectedPolygons.Pop(false);
			if (ConsumedPolygons[CurrentPolygonID.GetValue()])
			{
				continue;
			}
			SoftEdgeNeigbors.Reset();
			uint32& SmoothGroup = PolygonSmoothGroup.FindOrAdd(CurrentPolygonID);
			uint32 AvoidSmoothGroup = 0;
			uint32 NeighborSmoothGroup = 0;
			const uint32 LastSmoothGroupValue = (LastPolygonID == FPolygonID::Invalid) ? 0 : PolygonSmoothGroup[LastPolygonID];
			PolygonEdges.Reset();
			SourceMeshDescription.GetPolygonPerimeterEdges(CurrentPolygonID, PolygonEdges);
			for (const FEdgeID& EdgeID : PolygonEdges)
			{
				bool bIsHardEdge = EdgeHardnesses[EdgeID];
				EdgeConnectedPolygons.Reset();
				SourceMeshDescription.GetEdgeConnectedPolygons(EdgeID, EdgeConnectedPolygons);
				for (const FPolygonID& EdgePolygonID : EdgeConnectedPolygons)
				{
					if (EdgePolygonID == CurrentPolygonID)
					{
						continue;
					}
					uint32 SmoothValue = 0;
					if (PolygonSmoothGroup.Contains(EdgePolygonID))
					{
						SmoothValue = PolygonSmoothGroup[EdgePolygonID];
					}

					if (bIsHardEdge) //Hard Edge
					{
						AvoidSmoothGroup |= SmoothValue;
					}
					else
					{
						NeighborSmoothGroup |= SmoothValue;
						//Put all none hard edge polygon in the next iteration
						if (!ConsumedPolygons[EdgePolygonID.GetValue()])
						{
							ConnectedPolygons.Add(EdgePolygonID);
							LastConnectedPolygons.Add(CurrentPolygonID);
						}
						else
						{
							SoftEdgeNeigbors.Add(EdgePolygonID);
						}
					}
				}
			}

			if (AvoidSmoothGroup != 0)
			{
				PolygonAvoidances.FindOrAdd(CurrentPolygonID) = AvoidSmoothGroup;
				//find neighbor avoidance
				for (FPolygonID& NeighborID : SoftEdgeNeigbors)
				{
					if (!PolygonAvoidances.Contains(NeighborID))
					{
						continue;
					}
					AvoidSmoothGroup |= PolygonAvoidances[NeighborID];
				}
				uint32 NewSmoothGroup = 1;
				while ((NewSmoothGroup & AvoidSmoothGroup) != 0 && NewSmoothGroup < MAX_uint32)
				{
					//Shift the smooth group
					NewSmoothGroup = NewSmoothGroup << 1;
				}
				SmoothGroup = NewSmoothGroup;
				//Apply to all neighboard
				for (FPolygonID& NeighborID : SoftEdgeNeigbors)
				{
					PolygonSmoothGroup[NeighborID] |= NewSmoothGroup;
				}
			}
			else if (NeighborSmoothGroup != 0)
			{
				SmoothGroup |= LastSmoothGroupValue | NeighborSmoothGroup;
			}
			else
			{
				SmoothGroup = 1;
			}
			ConsumedPolygons[CurrentPolygonID.GetValue()] = true;
		}
	}
	//Set the smooth group in the FaceSmoothingMasks parameter
	check(FaceSmoothingMasks.Num() == TriangleCount);
	int32 TriangleIndex = 0;
	for (const FPolygonID PolygonID : SourceMeshDescription.Polygons().GetElementIDs())
	{
		uint32 PolygonSmoothValue = PolygonSmoothGroup[PolygonID];
		for (const FTriangleID TriangleID : SourceMeshDescription.GetPolygonTriangleIDs(PolygonID))
		{
			FaceSmoothingMasks[TriangleIndex++] = PolygonSmoothValue;
		}
	}
}

void FStaticMeshOperations::ConvertSmoothGroupToHardEdges(const TArray<uint32>& FaceSmoothingMasks, FMeshDescription& DestinationMeshDescription)
{
	TEdgeAttributesRef<bool> EdgeHardnesses = DestinationMeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);

	TArray<bool> ConsumedPolygons;
	ConsumedPolygons.AddZeroed(DestinationMeshDescription.Polygons().Num());
	for (const FPolygonID PolygonID : DestinationMeshDescription.Polygons().GetElementIDs())
	{
		if (ConsumedPolygons[PolygonID.GetValue()])
		{
			continue;
		}
		TArray<FPolygonID> ConnectedPolygons;
		ConnectedPolygons.Add(PolygonID);
		while (ConnectedPolygons.Num() > 0)
		{
			FPolygonID CurrentPolygonID = ConnectedPolygons.Pop(false);
			int32 CurrentPolygonIDValue = CurrentPolygonID.GetValue();
			check(FaceSmoothingMasks.IsValidIndex(CurrentPolygonIDValue));
			const uint32 ReferenceSmoothGroup = FaceSmoothingMasks[CurrentPolygonIDValue];
			TArray<FEdgeID> PolygonEdges;
			DestinationMeshDescription.GetPolygonPerimeterEdges(CurrentPolygonID, PolygonEdges);
			for (const FEdgeID& EdgeID : PolygonEdges)
			{
				const bool bIsHardEdge = EdgeHardnesses[EdgeID];
				if (bIsHardEdge)
				{
					continue;
				}
				const TArray<FPolygonID>& EdgeConnectedPolygons = DestinationMeshDescription.GetEdgeConnectedPolygons(EdgeID);
				for (const FPolygonID& EdgePolygonID : EdgeConnectedPolygons)
				{
					int32 EdgePolygonIDValue = EdgePolygonID.GetValue();
					if (EdgePolygonID == CurrentPolygonID || ConsumedPolygons[EdgePolygonIDValue])
					{
						continue;
					}
					check(FaceSmoothingMasks.IsValidIndex(EdgePolygonIDValue));
					const uint32 TestSmoothGroup = FaceSmoothingMasks[EdgePolygonIDValue];
					if ((TestSmoothGroup & ReferenceSmoothGroup) == 0)
					{
						EdgeHardnesses[EdgeID] = true;
						break;
					}
					else
					{
						ConnectedPolygons.Add(EdgePolygonID);
					}
				}
			}
			ConsumedPolygons[CurrentPolygonID.GetValue()] = true;
		}
	}
}

void FStaticMeshOperations::ConvertToRawMesh(const FMeshDescription& SourceMeshDescription, FRawMesh& DestinationRawMesh, const TMap<FName, int32>& MaterialMap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::ConvertToRawMesh);

	DestinationRawMesh.Empty();

	//Gather all array data
	TVertexAttributesConstRef<FVector> VertexPositions = SourceMeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = SourceMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = SourceMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = SourceMeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = SourceMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = SourceMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotName = SourceMeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	DestinationRawMesh.VertexPositions.AddZeroed(SourceMeshDescription.Vertices().Num());
	TArray<int32> RemapVerts;
	RemapVerts.AddZeroed(SourceMeshDescription.Vertices().GetArraySize());
	int32 VertexIndex = 0;
	for (const FVertexID& VertexID : SourceMeshDescription.Vertices().GetElementIDs())
	{
		DestinationRawMesh.VertexPositions[VertexIndex] = VertexPositions[VertexID];
		RemapVerts[VertexID.GetValue()] = VertexIndex;
		++VertexIndex;
	}

	int32 TriangleNumber = SourceMeshDescription.Triangles().Num();
	DestinationRawMesh.FaceMaterialIndices.AddZeroed(TriangleNumber);
	DestinationRawMesh.FaceSmoothingMasks.AddZeroed(TriangleNumber);

	bool bHasVertexColor = HasVertexColor(SourceMeshDescription);

	int32 WedgeIndexNumber = TriangleNumber * 3;
	if (bHasVertexColor)
	{
		DestinationRawMesh.WedgeColors.AddZeroed(WedgeIndexNumber);
	}
	DestinationRawMesh.WedgeIndices.AddZeroed(WedgeIndexNumber);
	DestinationRawMesh.WedgeTangentX.AddZeroed(WedgeIndexNumber);
	DestinationRawMesh.WedgeTangentY.AddZeroed(WedgeIndexNumber);
	DestinationRawMesh.WedgeTangentZ.AddZeroed(WedgeIndexNumber);
	int32 ExistingUVCount = VertexInstanceUVs.GetNumIndices();
	for (int32 UVIndex = 0; UVIndex < ExistingUVCount; ++UVIndex)
	{
		DestinationRawMesh.WedgeTexCoords[UVIndex].AddZeroed(WedgeIndexNumber);
	}

	int32 TriangleIndex = 0;
	int32 WedgeIndex = 0;
	for (const FPolygonID PolygonID : SourceMeshDescription.Polygons().GetElementIDs())
	{
		const FPolygonGroupID& PolygonGroupID = SourceMeshDescription.GetPolygonPolygonGroup(PolygonID);
		int32 PolygonIDValue = PolygonID.GetValue();
		const TArray<FTriangleID>& TriangleIDs = SourceMeshDescription.GetPolygonTriangleIDs(PolygonID);
		for (const FTriangleID TriangleID : TriangleIDs)
		{
			if (MaterialMap.Num() > 0 && MaterialMap.Contains(PolygonGroupMaterialSlotName[PolygonGroupID]))
			{
				DestinationRawMesh.FaceMaterialIndices[TriangleIndex] = MaterialMap[PolygonGroupMaterialSlotName[PolygonGroupID]];
			}
			else
			{
				DestinationRawMesh.FaceMaterialIndices[TriangleIndex] = PolygonGroupID.GetValue();
			}
			DestinationRawMesh.FaceSmoothingMasks[TriangleIndex] = 0; //Conversion of soft/hard to smooth mask is done after the geometry is converted
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVertexInstanceID VertexInstanceID = SourceMeshDescription.GetTriangleVertexInstance(TriangleID, Corner);

				if (bHasVertexColor)
				{
					DestinationRawMesh.WedgeColors[WedgeIndex] = FLinearColor(VertexInstanceColors[VertexInstanceID]).ToFColor(true);
				}
				DestinationRawMesh.WedgeIndices[WedgeIndex] = RemapVerts[SourceMeshDescription.GetVertexInstanceVertex(VertexInstanceID).GetValue()];
				DestinationRawMesh.WedgeTangentX[WedgeIndex] = VertexInstanceTangents[VertexInstanceID];
				DestinationRawMesh.WedgeTangentY[WedgeIndex] = FVector::CrossProduct(VertexInstanceNormals[VertexInstanceID], VertexInstanceTangents[VertexInstanceID]).GetSafeNormal() * VertexInstanceBinormalSigns[VertexInstanceID];
				DestinationRawMesh.WedgeTangentZ[WedgeIndex] = VertexInstanceNormals[VertexInstanceID];
				for (int32 UVIndex = 0; UVIndex < ExistingUVCount; ++UVIndex)
				{
					DestinationRawMesh.WedgeTexCoords[UVIndex][WedgeIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
				}
				++WedgeIndex;
			}
			++TriangleIndex;
		}
	}
	//Convert the smoothgroup
	ConvertHardEdgesToSmoothGroup(SourceMeshDescription, DestinationRawMesh.FaceSmoothingMasks);
}

//We want to fill the FMeshDescription vertex position mesh attribute with the FRawMesh vertex position
//We will also weld the vertex position (old FRawMesh is not always welded) and construct a mapping array to match the FVertexID
void FillMeshDescriptionVertexPositionNoDuplicate(const TArray<FVector>& RawMeshVertexPositions, FMeshDescription& DestinationMeshDescription, TArray<FVertexID>& RemapVertexPosition)
{
	TVertexAttributesRef<FVector> VertexPositions = DestinationMeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	const int32 NumVertex = RawMeshVertexPositions.Num();

	TMap<int32, int32> TempRemapVertexPosition;
	TempRemapVertexPosition.Reserve(NumVertex);

	// Create a list of vertex Z/index pairs
	TArray<MeshDescriptionOperationNamespace::FIndexAndZ> VertIndexAndZ;
	VertIndexAndZ.Reserve(NumVertex);

	for (int32 VertexIndex = 0; VertexIndex < NumVertex; ++VertexIndex)
	{
		new(VertIndexAndZ)MeshDescriptionOperationNamespace::FIndexAndZ(VertexIndex, RawMeshVertexPositions[VertexIndex]);
	}

	// Sort the vertices by z value
	VertIndexAndZ.Sort(MeshDescriptionOperationNamespace::FCompareIndexAndZ());

	int32 VertexCount = 0;
	// Search for duplicates, quickly!
	for (int32 i = 0; i < VertIndexAndZ.Num(); i++)
	{
		int32 Index_i = VertIndexAndZ[i].Index;
		if (TempRemapVertexPosition.Contains(Index_i))
		{
			continue;
		}
		TempRemapVertexPosition.FindOrAdd(Index_i) = VertexCount;
		// only need to search forward, since we add pairs both ways
		for (int32 j = i + 1; j < VertIndexAndZ.Num(); j++)
		{
			if (FMath::Abs(VertIndexAndZ[j].Z - VertIndexAndZ[i].Z) > SMALL_NUMBER)
				break; // can't be any more dups

			const FVector& PositionA = *(VertIndexAndZ[i].OriginalVector);
			const FVector& PositionB = *(VertIndexAndZ[j].OriginalVector);

			if (PositionA.Equals(PositionB, SMALL_NUMBER))
			{
				TempRemapVertexPosition.FindOrAdd(VertIndexAndZ[j].Index) = VertexCount;
			}
		}
		VertexCount++;
	}

	//Make sure the vertex are added in the same order to be lossless when converting the FRawMesh
	//In case there is a duplicate even reordering it will not be lossless, but MeshDescription do not support
	//bad data like duplicated vertex position.
	RemapVertexPosition.AddUninitialized(NumVertex);
	DestinationMeshDescription.ReserveNewVertices(VertexCount);
	TArray<FVertexID> UniqueVertexDone;
	UniqueVertexDone.AddUninitialized(VertexCount);
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		UniqueVertexDone[VertexIndex] = FVertexID::Invalid;
	}
	for (int32 VertexIndex = 0; VertexIndex < NumVertex; ++VertexIndex)
	{
		int32 RealIndex = TempRemapVertexPosition[VertexIndex];
		if (UniqueVertexDone[RealIndex] != FVertexID::Invalid)
		{
			RemapVertexPosition[VertexIndex] = UniqueVertexDone[RealIndex];
			continue;
		}
		FVertexID VertexID = DestinationMeshDescription.CreateVertex();
		UniqueVertexDone[RealIndex] = VertexID;
		VertexPositions[VertexID] = RawMeshVertexPositions[VertexIndex];
		RemapVertexPosition[VertexIndex] = VertexID;
	}
}

//Discover degenerated triangle
bool IsTriangleDegenerated(const FRawMesh& SourceRawMesh, const TArray<FVertexID>& RemapVertexPosition, const int32 VerticeIndexBase)
{
	FVertexID VertexIDs[3];
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		int32 VerticeIndex = VerticeIndexBase + Corner;
		VertexIDs[Corner] = RemapVertexPosition[SourceRawMesh.WedgeIndices[VerticeIndex]];
	}
	return (VertexIDs[0] == VertexIDs[1] || VertexIDs[0] == VertexIDs[2] || VertexIDs[1] == VertexIDs[2]);
}

void FStaticMeshOperations::ConvertFromRawMesh(const FRawMesh& SourceRawMesh, FMeshDescription& DestinationMeshDescription, const TMap<int32, FName>& MaterialMap, bool bSkipNormalsAndTangents)
{
	DestinationMeshDescription.Empty();

	DestinationMeshDescription.ReserveNewVertexInstances(SourceRawMesh.WedgeIndices.Num());
	DestinationMeshDescription.ReserveNewPolygons(SourceRawMesh.WedgeIndices.Num() / 3);
	//Approximately 2.5 edges per polygons
	DestinationMeshDescription.ReserveNewEdges(SourceRawMesh.WedgeIndices.Num() * 2.5f / 3);

	//Gather all array data
	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = DestinationMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = DestinationMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = DestinationMeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = DestinationMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = DestinationMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = DestinationMeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	int32 NumTexCoords = 0;
	int32 MaxTexCoords = MAX_MESH_TEXTURE_COORDS;
	TArray<int32> TextureCoordinnateRemapIndex;
	TextureCoordinnateRemapIndex.AddZeroed(MaxTexCoords);
	for (int32 TextureCoordinnateIndex = 0; TextureCoordinnateIndex < MaxTexCoords; ++TextureCoordinnateIndex)
	{
		TextureCoordinnateRemapIndex[TextureCoordinnateIndex] = INDEX_NONE;
		if (SourceRawMesh.WedgeTexCoords[TextureCoordinnateIndex].Num() == SourceRawMesh.WedgeIndices.Num())
		{
			TextureCoordinnateRemapIndex[TextureCoordinnateIndex] = NumTexCoords;
			NumTexCoords++;
		}
	}
	VertexInstanceUVs.SetNumIndices(NumTexCoords);

	//Ensure we do not have any duplicate, We found all duplicated vertex and compact them and build a remap indice array to remap the wedgeindices
	TArray<FVertexID> RemapVertexPosition;
	FillMeshDescriptionVertexPositionNoDuplicate(SourceRawMesh.VertexPositions, DestinationMeshDescription, RemapVertexPosition);

	bool bHasColors = SourceRawMesh.WedgeColors.Num() > 0;
	bool bHasTangents = SourceRawMesh.WedgeTangentX.Num() > 0 && SourceRawMesh.WedgeTangentY.Num() > 0;
	bool bHasNormals = SourceRawMesh.WedgeTangentZ.Num() > 0;

	TArray<FPolygonGroupID> PolygonGroups;
	TMap<int32, FPolygonGroupID> MaterialIndexToPolygonGroup;

	//Create the PolygonGroups
	for (int32 MaterialIndex : SourceRawMesh.FaceMaterialIndices)
	{
		if (!MaterialIndexToPolygonGroup.Contains(MaterialIndex))
		{
			FPolygonGroupID PolygonGroupID(MaterialIndex);
			DestinationMeshDescription.CreatePolygonGroupWithID(PolygonGroupID);
			if (MaterialMap.Contains(MaterialIndex))
			{
				PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = MaterialMap[MaterialIndex];
			}
			else
			{
				PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = FName(*FString::Printf(TEXT("MaterialSlot_%d"), MaterialIndex));
			}
			PolygonGroups.Add(PolygonGroupID);
			MaterialIndexToPolygonGroup.Add(MaterialIndex, PolygonGroupID);
		}
	}

	//Triangles
	int32 TriangleCount = SourceRawMesh.WedgeIndices.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		int32 VerticeIndexBase = TriangleIndex * 3;
		//Check if the triangle is degenerated and skip the data if its the case
		if (IsTriangleDegenerated(SourceRawMesh, RemapVertexPosition, VerticeIndexBase))
		{
			continue;
		}

		//PolygonGroup
		FPolygonGroupID PolygonGroupID = FPolygonGroupID::Invalid;
		FName PolygonGroupImportedMaterialSlotName = NAME_None;
		int32 MaterialIndex = SourceRawMesh.FaceMaterialIndices[TriangleIndex];
		if (MaterialIndexToPolygonGroup.Contains(MaterialIndex))
		{
			PolygonGroupID = MaterialIndexToPolygonGroup[MaterialIndex];
		}
		else if (MaterialMap.Num() > 0 && MaterialMap.Contains(MaterialIndex))
		{
			PolygonGroupImportedMaterialSlotName = MaterialMap[MaterialIndex];
			for (const FPolygonGroupID& SearchPolygonGroupID : DestinationMeshDescription.PolygonGroups().GetElementIDs())
			{
				if (PolygonGroupImportedMaterialSlotNames[SearchPolygonGroupID] == PolygonGroupImportedMaterialSlotName)
				{
					PolygonGroupID = SearchPolygonGroupID;
					break;
				}
			}
		}

		if (PolygonGroupID == FPolygonGroupID::Invalid)
		{
			PolygonGroupID = DestinationMeshDescription.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = PolygonGroupImportedMaterialSlotName == NAME_None ? FName(*FString::Printf(TEXT("MaterialSlot_%d"), MaterialIndex)) : PolygonGroupImportedMaterialSlotName;
			PolygonGroups.Add(PolygonGroupID);
			MaterialIndexToPolygonGroup.Add(MaterialIndex, PolygonGroupID);
		}
		TArray<FVertexInstanceID> TriangleVertexInstanceIDs;
		TriangleVertexInstanceIDs.SetNum(3);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			int32 VerticeIndex = VerticeIndexBase + Corner;
			FVertexID VertexID = RemapVertexPosition[SourceRawMesh.WedgeIndices[VerticeIndex]];
			FVertexInstanceID VertexInstanceID = DestinationMeshDescription.CreateVertexInstance(VertexID);
			TriangleVertexInstanceIDs[Corner] = VertexInstanceID;
			VertexInstanceColors[VertexInstanceID] = bHasColors ? FLinearColor::FromSRGBColor(SourceRawMesh.WedgeColors[VerticeIndex]) : FLinearColor::White;
			VertexInstanceNormals[VertexInstanceID] = bHasNormals ? SourceRawMesh.WedgeTangentZ[VerticeIndex] : FVector(ForceInitToZero);

			if (bHasTangents)
			{
				VertexInstanceTangents[VertexInstanceID] = SourceRawMesh.WedgeTangentX[VerticeIndex];
				VertexInstanceBinormalSigns[VertexInstanceID] = FMatrix(SourceRawMesh.WedgeTangentX[VerticeIndex].GetSafeNormal(),
					SourceRawMesh.WedgeTangentY[VerticeIndex].GetSafeNormal(),
					SourceRawMesh.WedgeTangentZ[VerticeIndex].GetSafeNormal(),
					FVector::ZeroVector).Determinant() < 0 ? -1.0f : +1.0f;
			}
			else
			{
				VertexInstanceTangents[VertexInstanceID] = FVector(ForceInitToZero);
				VertexInstanceBinormalSigns[VertexInstanceID] = 0.0f;
			}

			for (int32 TextureCoordinnateIndex = 0; TextureCoordinnateIndex < NumTexCoords; ++TextureCoordinnateIndex)
			{
				int32 TextureCoordIndex = TextureCoordinnateRemapIndex[TextureCoordinnateIndex];
				if (TextureCoordIndex == INDEX_NONE)
				{
					continue;
				}
				VertexInstanceUVs.Set(VertexInstanceID, TextureCoordIndex, SourceRawMesh.WedgeTexCoords[TextureCoordinnateIndex][VerticeIndex]);
			}
		}

		DestinationMeshDescription.CreatePolygon(PolygonGroupID, TriangleVertexInstanceIDs);
	}

	ConvertSmoothGroupToHardEdges(SourceRawMesh.FaceSmoothingMasks, DestinationMeshDescription);

	//Create the missing normals and tangents, should we use Mikkt space for tangent???
	if (!bSkipNormalsAndTangents && (!bHasNormals || !bHasTangents))
	{
		//DestinationMeshDescription.ComputePolygonTangentsAndNormals(0.0f);
		ComputePolygonTangentsAndNormals(DestinationMeshDescription, 0.0f);

		//Create the missing normals and recompute the tangents with MikkTSpace.
		EComputeNTBsFlags ComputeNTBsOptions = EComputeNTBsFlags::Tangents | EComputeNTBsFlags::UseMikkTSpace | EComputeNTBsFlags::BlendOverlappingNormals;
		ComputeTangentsAndNormals(DestinationMeshDescription, ComputeNTBsOptions);
	}
}

void FStaticMeshOperations::AppendMeshDescription(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, FAppendSettings& AppendSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::AppendMeshDescription);

	//Vertex Attributes
	TVertexAttributesConstRef<FVector> SourceVertexPositions = SourceMesh.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TVertexAttributesConstRef<float> SourceVertexCornerSharpness = SourceMesh.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness);

	TVertexAttributesRef<FVector> TargetVertexPositions = TargetMesh.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TVertexAttributesRef<float> TargetVertexCornerSharpness = TargetMesh.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness);

	//Edge Attributes
	TEdgeAttributesConstRef<bool> SourceEdgeHardnesses = SourceMesh.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	TEdgeAttributesConstRef<float> SourceEdgeCreaseSharpnesses = SourceMesh.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);

	TEdgeAttributesRef<bool> TargetEdgeHardnesses = TargetMesh.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	TEdgeAttributesRef<float> TargetEdgeCreaseSharpnesses = TargetMesh.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);

	//PolygonGroup Attributes
	TPolygonGroupAttributesConstRef<FName> SourceImportedMaterialSlotNames = SourceMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	TPolygonGroupAttributesRef<FName> TargetImportedMaterialSlotNames = TargetMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	//VertexInstance Attributes
	TVertexInstanceAttributesConstRef<FVector> SourceVertexInstanceNormals = SourceMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesConstRef<FVector> SourceVertexInstanceTangents = SourceMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesConstRef<float> SourceVertexInstanceBinormalSigns = SourceMesh.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesConstRef<FVector4> SourceVertexInstanceColors = SourceMesh.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesConstRef<FVector2D> SourceVertexInstanceUVs = SourceMesh.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	TVertexInstanceAttributesRef<FVector> TargetVertexInstanceNormals = TargetMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector> TargetVertexInstanceTangents = TargetMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> TargetVertexInstanceBinormalSigns = TargetMesh.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesRef<FVector4> TargetVertexInstanceColors = TargetMesh.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesRef<FVector2D> TargetVertexInstanceUVs = TargetMesh.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	//Copy into the target mesh
	TargetMesh.ReserveNewVertices(SourceMesh.Vertices().Num());
	TargetMesh.ReserveNewVertexInstances(SourceMesh.VertexInstances().Num());
	TargetMesh.ReserveNewEdges(SourceMesh.Edges().Num());
	TargetMesh.ReserveNewPolygons(SourceMesh.Polygons().Num());

	if (SourceVertexInstanceUVs.GetNumIndices() > TargetVertexInstanceUVs.GetNumIndices())
	{
		TargetVertexInstanceUVs.SetNumIndices(SourceVertexInstanceUVs.GetNumIndices());
	}

	//PolygonGroups
	PolygonGroupMap RemapPolygonGroup;
	if (AppendSettings.PolygonGroupsDelegate.IsBound())
	{
		AppendSettings.PolygonGroupsDelegate.Execute(SourceMesh, TargetMesh, RemapPolygonGroup);
	}
	else
	{
		for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
		{
			FPolygonGroupID TargetMatchingID = FPolygonGroupID::Invalid;
			for (FPolygonGroupID TargetPolygonGroupID : TargetMesh.PolygonGroups().GetElementIDs())
			{
				if (SourceImportedMaterialSlotNames[SourcePolygonGroupID] == TargetImportedMaterialSlotNames[TargetPolygonGroupID])
				{
					TargetMatchingID = TargetPolygonGroupID;
					break;
				}
			}
			if (TargetMatchingID == FPolygonGroupID::Invalid)
			{
				TargetMatchingID = TargetMesh.CreatePolygonGroup();
				TargetImportedMaterialSlotNames[TargetMatchingID] = SourceImportedMaterialSlotNames[SourcePolygonGroupID];
			}
			RemapPolygonGroup.Add(SourcePolygonGroupID, TargetMatchingID);
		}
	}

	//Vertices
	TMap<FVertexID, FVertexID> SourceVertexIDRemap;
	SourceVertexIDRemap.Reserve(SourceMesh.Vertices().Num());
	for (FVertexID SourceVertexID : SourceMesh.Vertices().GetElementIDs())
	{
		FVertexID TargetVertexID = TargetMesh.CreateVertex();
		TargetVertexPositions[TargetVertexID] = (SourceVertexPositions[SourceVertexID] - AppendSettings.MergedAssetPivot);
		TargetVertexCornerSharpness[TargetVertexID] = SourceVertexCornerSharpness[SourceVertexID];
		SourceVertexIDRemap.Add(SourceVertexID, TargetVertexID);
	}

	// Transform vertices properties
	if (AppendSettings.MeshTransform)
	{
		const FTransform& Transform = AppendSettings.MeshTransform.GetValue();
		for (const TPair<FVertexID, FVertexID>& VertexIDPair : SourceVertexIDRemap)
		{
			FVector& Position = TargetVertexPositions[VertexIDPair.Value];
			Position = Transform.TransformPosition(Position);
		}
	}

	//Edges
	TMap<FEdgeID, FEdgeID> SourceEdgeIDRemap;
	SourceEdgeIDRemap.Reserve(SourceMesh.Edges().Num());
	for (const FEdgeID SourceEdgeID : SourceMesh.Edges().GetElementIDs())
	{
		const FVertexID EdgeVertex0 = SourceMesh.GetEdgeVertex(SourceEdgeID, 0);
		const FVertexID EdgeVertex1 = SourceMesh.GetEdgeVertex(SourceEdgeID, 1);
		FEdgeID TargetEdgeID = TargetMesh.CreateEdge(SourceVertexIDRemap[EdgeVertex0], SourceVertexIDRemap[EdgeVertex1]);
		TargetEdgeHardnesses[TargetEdgeID] = SourceEdgeHardnesses[SourceEdgeID];
		TargetEdgeCreaseSharpnesses[TargetEdgeID] = SourceEdgeCreaseSharpnesses[SourceEdgeID];
		SourceEdgeIDRemap.Add(SourceEdgeID, TargetEdgeID);
	}

	//VertexInstances
	TMap<FVertexInstanceID, FVertexInstanceID> SourceVertexInstanceIDRemap;
	SourceVertexInstanceIDRemap.Reserve(SourceMesh.VertexInstances().Num());
	for (const FVertexInstanceID& SourceVertexInstanceID : SourceMesh.VertexInstances().GetElementIDs())
	{
		FVertexInstanceID TargetVertexInstanceID = TargetMesh.CreateVertexInstance(SourceVertexIDRemap[SourceMesh.GetVertexInstanceVertex(SourceVertexInstanceID)]);
		SourceVertexInstanceIDRemap.Add(SourceVertexInstanceID, TargetVertexInstanceID);

		TargetVertexInstanceNormals[TargetVertexInstanceID] = SourceVertexInstanceNormals[SourceVertexInstanceID];
		TargetVertexInstanceTangents[TargetVertexInstanceID] = SourceVertexInstanceTangents[SourceVertexInstanceID];
		TargetVertexInstanceBinormalSigns[TargetVertexInstanceID] = SourceVertexInstanceBinormalSigns[SourceVertexInstanceID];

		if (AppendSettings.bMergeVertexColor)
		{
			TargetVertexInstanceColors[TargetVertexInstanceID] = SourceVertexInstanceColors[SourceVertexInstanceID];
		}

		for (int32 UVChannelIndex = 0; UVChannelIndex < SourceVertexInstanceUVs.GetNumIndices(); ++UVChannelIndex)
		{
			TargetVertexInstanceUVs.Set(TargetVertexInstanceID, UVChannelIndex, SourceVertexInstanceUVs.Get(SourceVertexInstanceID, UVChannelIndex));
		}
	}

	// Transform vertex instances properties
	if (AppendSettings.MeshTransform)
	{
		const FTransform& Transform = AppendSettings.MeshTransform.GetValue();
		bool bFlipBinormal = Transform.GetDeterminant() < 0;
		float BinormalSignsFactor = bFlipBinormal ? -1.f : 1.f;
		for (const TPair<FVertexInstanceID, FVertexInstanceID>& VertexInstanceIDPair : SourceVertexInstanceIDRemap)
		{
			FVertexInstanceID InstanceID = VertexInstanceIDPair.Value;

			FVector& Normal = TargetVertexInstanceNormals[InstanceID];
			Normal = Transform.TransformVectorNoScale(Normal);

			FVector& Tangent = TargetVertexInstanceTangents[InstanceID];
			Tangent = Transform.TransformVectorNoScale(Tangent);

			TargetVertexInstanceBinormalSigns[InstanceID] *= BinormalSignsFactor;
		}
	}

	//Polygons
	for (const FPolygonID SourcePolygonID : SourceMesh.Polygons().GetElementIDs())
	{
		const TArray<FVertexInstanceID>& PerimeterVertexInstanceIDs = SourceMesh.GetPolygonVertexInstances(SourcePolygonID);
		const FPolygonGroupID PolygonGroupID = SourceMesh.GetPolygonPolygonGroup(SourcePolygonID);
		//Find the polygonGroupID
		FPolygonGroupID TargetPolygonGroupID = RemapPolygonGroup[PolygonGroupID];

		int32 PolygonVertexCount = PerimeterVertexInstanceIDs.Num();
		TArray<FVertexInstanceID> VertexInstanceIDs;
		VertexInstanceIDs.Reserve(PolygonVertexCount);
		for (const FVertexInstanceID VertexInstanceID : PerimeterVertexInstanceIDs)
		{
			VertexInstanceIDs.Add(SourceVertexInstanceIDRemap[VertexInstanceID]);
		}
		// Insert a polygon into the mesh
		const FPolygonID TargetPolygonID = TargetMesh.CreatePolygon(TargetPolygonGroupID, VertexInstanceIDs);
	}
}



//////////////////////////////////////////////////////////////////////////
// Normals tangents and Bi-normals
void FStaticMeshOperations::AreNormalsAndTangentsValid(const FMeshDescription& MeshDescription, bool& bHasInvalidNormals, bool& bHasInvalidTangents)
{
	bHasInvalidNormals = false;
	bHasInvalidTangents = false;
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);

	for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		bHasInvalidNormals |= (VertexInstanceNormals[VertexInstanceID].IsNearlyZero() || VertexInstanceNormals[VertexInstanceID].ContainsNaN());
		bHasInvalidTangents |= (VertexInstanceTangents[VertexInstanceID].IsNearlyZero() || VertexInstanceTangents[VertexInstanceID].ContainsNaN());
		if (bHasInvalidNormals && bHasInvalidTangents)
		{
			break;
		}
	}
}

void ClearNormalsAndTangentsData(FMeshDescription& MeshDescription, bool bClearNormals, bool bClearTangents)
{
	if (!bClearNormals && bClearTangents)
	{
		return;
	}

	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> VertexBinormalSigns = MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);

	//Zero out all value that need to be recompute
	for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		if (bClearNormals)
		{
			VertexInstanceNormals[VertexInstanceID] = FVector::ZeroVector;
		}
		if (bClearTangents)
		{
			//Dump the tangents
			VertexBinormalSigns[VertexInstanceID] = 0.0f;
			VertexInstanceTangents[VertexInstanceID] = FVector::ZeroVector;
		}
	}
}

struct FNTBGroupKeyFuncs : public TDefaultMapKeyFuncs<FVector2D, FVector, false>
{
	//We need to sanitize the key here to make sure -0.0f fall on the same hash then 0.0f
	static FORCEINLINE_DEBUGGABLE uint32 GetKeyHash(KeyInitType Key)
	{
		FVector2D TmpKey;
		TmpKey.X = FMath::IsNearlyZero(Key.X) ? 0.0f : Key.X;
		TmpKey.Y = FMath::IsNearlyZero(Key.Y) ? 0.0f : Key.Y;
		return FCrc::MemCrc32(&TmpKey, sizeof(TmpKey));
	}
};

void FStaticMeshOperations::RecomputeNormalsAndTangentsIfNeeded(FMeshDescription& MeshDescription, EComputeNTBsFlags ComputeNTBsOptions)
{
	if (!EnumHasAllFlags(ComputeNTBsOptions, EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents))
	{
		bool bRecomputeNormals = false;
		bool bRecomputeTangents = false;
		
		AreNormalsAndTangentsValid(MeshDescription, bRecomputeNormals, bRecomputeTangents);
		
		ComputeNTBsOptions |= (bRecomputeNormals ? EComputeNTBsFlags::Normals : EComputeNTBsFlags::None);
		ComputeNTBsOptions |= (bRecomputeTangents ? EComputeNTBsFlags::Tangents : EComputeNTBsFlags::None);
	}

	if (EnumHasAnyFlags(ComputeNTBsOptions, EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents))
	{
		ComputeTangentsAndNormals(MeshDescription, ComputeNTBsOptions);
	}
}

void FStaticMeshOperations::ComputeTangentsAndNormals(FMeshDescription& MeshDescription, EComputeNTBsFlags ComputeNTBsOptions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::ComputeTangentsAndNormals);

	//For each vertex compute the normals for every connected edges that are smooth between hard edges
	//         H   A    B
	//          \  ||  /
	//       G  -- ** -- C
	//          // |  \
	//         F   E    D
	//
	// The double ** are the vertex, the double line are hard edges, the single line are soft edge.
	// A and F are hard, all other edges are soft. The goal is to compute two average normals one from
	// A to F and a second from F to A. Then we can set the vertex instance normals accordingly.
	// First normal(A to F) = Normalize(A+B+C+D+E+F)
	// Second normal(F to A) = Normalize(F+G+H+A)
	// We found the connected edge using the triangle that share edges

	// @todo: provide an option to weight each contributing polygon normal according to the size of
	// the angle it makes with the vertex being calculated. This means that triangulated faces whose
	// internal edge meets the vertex doesn't get undue extra weight.

	struct FTriangleCornerData
	{
		FVertexInstanceID VertexInstanceID;
		float CornerAngle;
	};

	struct FTriangleData
	{
	public:
		//The area of the triangle
		float Area;

		//Set the corner angle data for a FVertexInstanceID
		void SetCornerAngleData(FVertexInstanceID VertexInstanceID, float CornerAngle, int32 CornerIndex)
		{
			CornerAngleDatas[CornerIndex].VertexInstanceID = VertexInstanceID;
			CornerAngleDatas[CornerIndex].CornerAngle = CornerAngle;
		}

		//Get the angle for the FVertexInstanceID
		float GetCornerAngle(FVertexInstanceID VertexInstanceID)
		{
			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (CornerAngleDatas[CornerIndex].VertexInstanceID == VertexInstanceID)
				{
					return CornerAngleDatas[CornerIndex].CornerAngle;
				}
			}

			//We should always found a valid VertexInstanceID
			check(false);
			return 0.0f;
		}
	private:
		//The data for each corner
		FTriangleCornerData CornerAngleDatas[3];
	};


	//Make sure the meshdescription is triangulate
	if (MeshDescription.Triangles().Num() < MeshDescription.Polygons().Num())
	{
		//Triangulate the mesh, we compute the normals on triangle not on polygon.
		MeshDescription.TriangulateMesh();
	}

	const bool bForceComputeNormals = EnumHasAllFlags(ComputeNTBsOptions, EComputeNTBsFlags::Normals);
	const bool bForceComputeTangent = EnumHasAnyFlags(ComputeNTBsOptions, EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents);
	const bool bComputeTangentWithMikkTSpace = bForceComputeTangent && EnumHasAllFlags(ComputeNTBsOptions, EComputeNTBsFlags::UseMikkTSpace);
	const bool bComputeWeightedNormals = EnumHasAllFlags(ComputeNTBsOptions, EComputeNTBsFlags::WeightedNTBs);

	//Clear any data we want to force-recompute since the following code actually look for any invalid data and recompute it.
	ClearNormalsAndTangentsData(MeshDescription, bForceComputeNormals, bForceComputeTangent);

	//Compute the weight (area and angle) for each triangles
	TMap<FTriangleID, FTriangleData> TriangleDatas;
	if (bComputeWeightedNormals)
	{
		TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		TriangleDatas.Reserve(MeshDescription.Triangles().Num());

		for (FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> VertexInstanceIDs = MeshDescription.GetTriangleVertexInstances(TriangleID);
			//Triangle should use 3 vertex instances
			check(VertexInstanceIDs.Num() == 3);
			const FVector& PointA = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[0])];
			const FVector& PointB = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[1])];
			const FVector& PointC = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[2])];
			FTriangleData& TriangleData = TriangleDatas.FindOrAdd(TriangleID);
			TriangleData.Area = TriangleUtilities::ComputeTriangleArea(PointA, PointB, PointC);
			TriangleData.SetCornerAngleData(VertexInstanceIDs[0], TriangleUtilities::ComputeTriangleCornerAngle(PointA, PointB, PointC), 0);
			TriangleData.SetCornerAngleData(VertexInstanceIDs[1], TriangleUtilities::ComputeTriangleCornerAngle(PointB, PointC, PointA), 1);
			TriangleData.SetCornerAngleData(VertexInstanceIDs[2], TriangleUtilities::ComputeTriangleCornerAngle(PointC, PointA, PointB), 2);
		}
	}

	//Iterate all vertex to compute normals for all vertex instance
	TArray<FVertexID> Vertices;
	Vertices.Reserve(MeshDescription.Vertices().Num());
	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		Vertices.Add(VertexID);
	}

	// Split work in batch to reduce call and allocation overhead
	const int32 BatchSize = 128 * 1024;
	const int32 BatchCount = 1 + Vertices.Num() / BatchSize;

	//Iterate all vertex to compute normals for all vertex instance
	ParallelFor(BatchCount,
		[&Vertices, &BatchSize, &bComputeTangentWithMikkTSpace, &MeshDescription, bComputeWeightedNormals, &TriangleDatas](int32 BatchIndex)
		{
			TVertexInstanceAttributesConstRef<FVector2D> VertexUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			TVertexInstanceAttributesRef<FVector> VertexNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
			TVertexInstanceAttributesRef<FVector> VertexTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
			TVertexInstanceAttributesRef<float> VertexBinormalSigns = MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);

			TPolygonAttributesConstRef<FVector> PolygonNormals = MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal);
			TPolygonAttributesConstRef<FVector> PolygonTangents = MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent);
			TPolygonAttributesConstRef<FVector> PolygonBinormals = MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal);
			TEdgeAttributesConstRef<bool>       EdgeHardnesses = MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);

			check(PolygonNormals.IsValid());
			check(PolygonTangents.IsValid());
			check(PolygonBinormals.IsValid());

			//Reuse containers between iterations to reduce allocations
			TMap<FVector2D, FVector, FDefaultSetAllocator, FNTBGroupKeyFuncs> GroupTangent;
			TMap<FVector2D, FVector, FDefaultSetAllocator, FNTBGroupKeyFuncs> GroupBiNormal;
			TMap<FTriangleID, FVertexInfo> VertexInfoMap;
			TArray<TArray<FTriangleID, TInlineAllocator<8>>> Groups;
			TArray<FTriangleID> ConsumedTriangle;
			TArray<FTriangleID> PolygonQueue;
			TArray<FVertexInstanceID> VertexInstanceInGroup;

			VertexInfoMap.Reserve(20);

			int32 Indice = BatchIndex * BatchSize;
			int32 LastIndice = FMath::Min(Indice + BatchSize, Vertices.Num());
			for (; Indice < LastIndice; ++Indice)
			{
				VertexInfoMap.Reset();

				const FVertexID VertexID = Vertices[Indice];

				bool bPointHasAllTangents = true;
				//Fill the VertexInfoMap
				for (const FEdgeID EdgeID : MeshDescription.GetVertexConnectedEdges(VertexID))
				{
					for (const FTriangleID TriangleID : MeshDescription.GetEdgeConnectedTriangles(EdgeID))
					{
						FVertexInfo& VertexInfo = VertexInfoMap.FindOrAdd(TriangleID);
						int32 EdgeIndex = VertexInfo.EdgeIDs.AddUnique(EdgeID);
						if (VertexInfo.TriangleID == FTriangleID::Invalid)
						{
							VertexInfo.TriangleID = TriangleID;
							for (const FVertexInstanceID VertexInstanceID : MeshDescription.GetTriangleVertexInstances(TriangleID))
							{
								if (MeshDescription.GetVertexInstanceVertex(VertexInstanceID) == VertexID)
								{
									VertexInfo.VertexInstanceID = VertexInstanceID;
									VertexInfo.UVs = VertexUVs.Get(VertexInstanceID, 0);	// UV0
									bPointHasAllTangents &= !VertexNormals[VertexInstanceID].IsNearlyZero() && !VertexTangents[VertexInstanceID].IsNearlyZero();
									if (bPointHasAllTangents)
									{
										FVector TangentX = VertexTangents[VertexInstanceID].GetSafeNormal();
										FVector TangentZ = VertexNormals[VertexInstanceID].GetSafeNormal();
										FVector TangentY = (FVector::CrossProduct(TangentZ, TangentX).GetSafeNormal() * VertexBinormalSigns[VertexInstanceID]).GetSafeNormal();
										if (TangentX.ContainsNaN() || TangentX.IsNearlyZero(SMALL_NUMBER) ||
											TangentY.ContainsNaN() || TangentY.IsNearlyZero(SMALL_NUMBER) ||
											TangentZ.ContainsNaN() || TangentZ.IsNearlyZero(SMALL_NUMBER))
										{
											bPointHasAllTangents = false;
										}
									}
									break;
								}
							}
						}
					}
				}

				if (bPointHasAllTangents)
				{
					continue;
				}

				//Build all group by recursively traverse all polygon connected to the vertex
				Groups.Reset();
				ConsumedTriangle.Reset();
				for (auto Kvp : VertexInfoMap)
				{
					if (ConsumedTriangle.Contains(Kvp.Key))
					{
						continue;
					}

					int32 CurrentGroupIndex = Groups.AddZeroed();
					TArray<FTriangleID, TInlineAllocator<8>>& CurrentGroup = Groups[CurrentGroupIndex];
					PolygonQueue.Reset();
					PolygonQueue.Add(Kvp.Key); //Use a queue to avoid recursive function
					while (PolygonQueue.Num() > 0)
					{
						FTriangleID CurrentPolygonID = PolygonQueue.Pop(false);
						FVertexInfo& CurrentVertexInfo = VertexInfoMap.FindOrAdd(CurrentPolygonID);
						CurrentGroup.AddUnique(CurrentVertexInfo.TriangleID);
						ConsumedTriangle.AddUnique(CurrentVertexInfo.TriangleID);
						for (const FEdgeID EdgeID : CurrentVertexInfo.EdgeIDs)
						{
							if (EdgeHardnesses[EdgeID])
							{
								//End of the group
								continue;
							}
							for (const FTriangleID TriangleID : MeshDescription.GetEdgeConnectedTriangles(EdgeID))
							{
								if (TriangleID == CurrentVertexInfo.TriangleID)
								{
									continue;
								}
								//Add this polygon to the group
								FVertexInfo& OtherVertexInfo = VertexInfoMap.FindOrAdd(TriangleID);
								//Do not repeat polygons
								if (!ConsumedTriangle.Contains(OtherVertexInfo.TriangleID))
								{
									PolygonQueue.Add(TriangleID);
								}
							}
						}
					}
				}

				for (const TArray<FTriangleID, TInlineAllocator<8>>& Group : Groups)
				{
					//Compute tangents data
					GroupTangent.Reset();
					GroupBiNormal.Reset();
					VertexInstanceInGroup.Reset();

					FVector GroupNormal(FVector::ZeroVector);
					for (const FTriangleID TriangleID : Group)
					{
						FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(TriangleID);
						FVertexInfo& CurrentVertexInfo = VertexInfoMap.FindOrAdd(TriangleID);
						float CornerWeight = 1.0f;

						if (bComputeWeightedNormals)
						{
							FTriangleData& TriangleData = TriangleDatas.FindChecked(TriangleID);
							CornerWeight = TriangleData.Area * TriangleData.GetCornerAngle(CurrentVertexInfo.VertexInstanceID);
						}

						const FVector PolyNormal = CornerWeight * PolygonNormals[PolygonID];
						const FVector PolyTangent = CornerWeight * PolygonTangents[PolygonID];
						const FVector PolyBinormal = CornerWeight * PolygonBinormals[PolygonID];

						VertexInstanceInGroup.Add(VertexInfoMap[TriangleID].VertexInstanceID);
						if (!PolyNormal.IsNearlyZero(SMALL_NUMBER) && !PolyNormal.ContainsNaN())
						{
							GroupNormal += PolyNormal;
						}
						if (!bComputeTangentWithMikkTSpace)
						{
							const FVector2D& UVs = VertexInfoMap[TriangleID].UVs;
							bool CreateGroup = (!GroupTangent.Contains(UVs));
							FVector& GroupTangentValue = GroupTangent.FindOrAdd(UVs);
							FVector& GroupBiNormalValue = GroupBiNormal.FindOrAdd(UVs);
							if (CreateGroup)
							{
								GroupTangentValue = FVector(0.0f);
								GroupBiNormalValue = FVector(0.0f);
							}
							if (!PolyTangent.IsNearlyZero(SMALL_NUMBER) && !PolyTangent.ContainsNaN())
							{
								GroupTangentValue += PolyTangent;
							}
							if (!PolyBinormal.IsNearlyZero(SMALL_NUMBER) && !PolyBinormal.ContainsNaN())
							{
								GroupBiNormalValue += PolyBinormal;
							}
						}
					}

					//////////////////////////////////////////////////////////////////////////
					//Apply the group to the Mesh
					GroupNormal.Normalize();
					if (!bComputeTangentWithMikkTSpace)
					{
						for (auto Kvp : GroupTangent)
						{
							Kvp.Value.Normalize();
						}
						for (auto Kvp : GroupBiNormal)
						{
							Kvp.Value.Normalize();
						}
					}
					//Apply the average NTB on all Vertex instance
					for (const FVertexInstanceID VertexInstanceID : VertexInstanceInGroup)
					{
						const FVector2D& VertexUV = VertexUVs.Get(VertexInstanceID, 0);	// UV0

						if (VertexNormals[VertexInstanceID].IsNearlyZero(SMALL_NUMBER))
						{
							VertexNormals[VertexInstanceID] = GroupNormal;
						}

						//If we are not computing the tangent with MikkTSpace, make sure the tangents are valid.
						if (!bComputeTangentWithMikkTSpace)
						{
							//Avoid changing the original group value
							FVector GroupTangentValue = GroupTangent[VertexUV];
							FVector GroupBiNormalValue = GroupBiNormal[VertexUV];

							if (!VertexTangents[VertexInstanceID].IsNearlyZero(SMALL_NUMBER))
							{
								GroupTangentValue = VertexTangents[VertexInstanceID];
							}
							FVector BiNormal(0.0f);
							const FVector& VertexNormal(VertexNormals[VertexInstanceID]);
							if (!VertexNormal.IsNearlyZero(SMALL_NUMBER) && !VertexTangents[VertexInstanceID].IsNearlyZero(SMALL_NUMBER))
							{
								BiNormal = FVector::CrossProduct(VertexNormal, VertexTangents[VertexInstanceID]).GetSafeNormal() * VertexBinormalSigns[VertexInstanceID];
							}
							if (!BiNormal.IsNearlyZero(SMALL_NUMBER))
							{
								GroupBiNormalValue = BiNormal;
							}
							// Gram-Schmidt orthogonalization
							GroupBiNormalValue -= GroupTangentValue * (GroupTangentValue | GroupBiNormalValue);
							GroupBiNormalValue.Normalize();

							GroupTangentValue -= VertexNormal * (VertexNormal | GroupTangentValue);
							GroupTangentValue.Normalize();

							GroupBiNormalValue -= VertexNormal * (VertexNormal | GroupBiNormalValue);
							GroupBiNormalValue.Normalize();
							//Set the value
							VertexTangents[VertexInstanceID] = GroupTangentValue;
							//If the BiNormal is zero set the sign to 1.0f, inlining GetBasisDeterminantSign() to avoid depending on RenderCore.
							VertexBinormalSigns[VertexInstanceID] = FMatrix(GroupTangentValue, GroupBiNormalValue, VertexNormal, FVector::ZeroVector).Determinant() < 0 ? -1.0f : +1.0f;
						}
					}
				}
			}
		}
	);

	if (bForceComputeTangent && bComputeTangentWithMikkTSpace)
	{
		ComputeMikktTangents(MeshDescription, EnumHasAnyFlags(ComputeNTBsOptions, EComputeNTBsFlags::IgnoreDegenerateTriangles));
	}
}

#if WITH_MIKKTSPACE
namespace MeshDescriptionMikktSpaceInterface
{
	int MikkGetNumFaces(const SMikkTSpaceContext* Context)
	{
		FMeshDescription *MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		return MeshDescription->Polygons().GetArraySize();
	}

	int MikkGetNumVertsOfFace(const SMikkTSpaceContext* Context, const int FaceIdx)
	{
		// All of our meshes are triangles.
		FMeshDescription *MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		if (MeshDescription->IsPolygonValid(FPolygonID(FaceIdx)))
		{
			return MeshDescription->GetPolygonVertexInstances(FPolygonID(FaceIdx)).Num();
		}

		return 0;
	}

	void MikkGetPosition(const SMikkTSpaceContext* Context, float Position[3], const int FaceIdx, const int VertIdx)
	{
		FMeshDescription* MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription->GetPolygonVertexInstances(FPolygonID(FaceIdx));
		const FVertexInstanceID VertexInstanceID = VertexInstanceIDs[VertIdx];
		const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);
		const FVector& VertexPosition = MeshDescription->VertexAttributes().GetAttribute<FVector>(VertexID, MeshAttribute::Vertex::Position);
		Position[0] = VertexPosition.X;
		Position[1] = VertexPosition.Y;
		Position[2] = VertexPosition.Z;
	}

	void MikkGetNormal(const SMikkTSpaceContext* Context, float Normal[3], const int FaceIdx, const int VertIdx)
	{
		FMeshDescription* MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription->GetPolygonVertexInstances(FPolygonID(FaceIdx));
		const FVertexInstanceID VertexInstanceID = VertexInstanceIDs[VertIdx];
		const FVector& VertexNormal = MeshDescription->VertexInstanceAttributes().GetAttribute<FVector>(VertexInstanceID, MeshAttribute::VertexInstance::Normal);
		Normal[0] = VertexNormal.X;
		Normal[1] = VertexNormal.Y;
		Normal[2] = VertexNormal.Z;
	}

	void MikkSetTSpaceBasic(const SMikkTSpaceContext* Context, const float Tangent[3], const float BitangentSign, const int FaceIdx, const int VertIdx)
	{
		FMeshDescription* MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription->GetPolygonVertexInstances(FPolygonID(FaceIdx));
		const FVertexInstanceID VertexInstanceID = VertexInstanceIDs[VertIdx];
		const FVector VertexTangent(Tangent[0], Tangent[1], Tangent[2]);
		MeshDescription->VertexInstanceAttributes().SetAttribute<FVector>(VertexInstanceID, MeshAttribute::VertexInstance::Tangent, 0, VertexTangent);
		MeshDescription->VertexInstanceAttributes().SetAttribute<float>(VertexInstanceID, MeshAttribute::VertexInstance::BinormalSign, 0, -BitangentSign);
	}

	void MikkGetTexCoord(const SMikkTSpaceContext* Context, float UV[2], const int FaceIdx, const int VertIdx)
	{
		FMeshDescription* MeshDescription = (FMeshDescription*)(Context->m_pUserData);
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription->GetPolygonVertexInstances(FPolygonID(FaceIdx));
		const FVertexInstanceID VertexInstanceID = VertexInstanceIDs[VertIdx];
		const FVector2D& TexCoord = MeshDescription->VertexInstanceAttributes().GetAttribute<FVector2D>(VertexInstanceID, MeshAttribute::VertexInstance::TextureCoordinate, 0);
		UV[0] = TexCoord.X;
		UV[1] = TexCoord.Y;
	}
}
#endif //#WITH_MIKKTSPACE

void FStaticMeshOperations::ComputeMikktTangents(FMeshDescription& MeshDescription, bool bIgnoreDegenerateTriangles)
{
#if WITH_MIKKTSPACE
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::ComputeMikktTangents);

	// The Mikkt interface does not handle properly polygon array with 'holes'
	// Compact mesh description if this is the case
	if (MeshDescription.Polygons().Num() != MeshDescription.Polygons().GetArraySize())
	{
		FElementIDRemappings Remappings;
		MeshDescription.Compact(Remappings);
	}

	// we can use mikktspace to calculate the tangents
	SMikkTSpaceInterface MikkTInterface;
	MikkTInterface.m_getNormal = MeshDescriptionMikktSpaceInterface::MikkGetNormal;
	MikkTInterface.m_getNumFaces = MeshDescriptionMikktSpaceInterface::MikkGetNumFaces;
	MikkTInterface.m_getNumVerticesOfFace = MeshDescriptionMikktSpaceInterface::MikkGetNumVertsOfFace;
	MikkTInterface.m_getPosition = MeshDescriptionMikktSpaceInterface::MikkGetPosition;
	MikkTInterface.m_getTexCoord = MeshDescriptionMikktSpaceInterface::MikkGetTexCoord;
	MikkTInterface.m_setTSpaceBasic = MeshDescriptionMikktSpaceInterface::MikkSetTSpaceBasic;
	MikkTInterface.m_setTSpace = nullptr;

	SMikkTSpaceContext MikkTContext;
	MikkTContext.m_pInterface = &MikkTInterface;
	MikkTContext.m_pUserData = (void*)(&MeshDescription);
	MikkTContext.m_bIgnoreDegenerates = bIgnoreDegenerateTriangles;
	genTangSpaceDefault(&MikkTContext);
#else
	ensureMsgf(false, TEXT("MikkTSpace tangent generation is not supported on this platform."));
#endif //WITH_MIKKTSPACE
}

void FStaticMeshOperations::FindOverlappingCorners(FOverlappingCorners& OutOverlappingCorners, const FMeshDescription& MeshDescription, float ComparisonThreshold)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::FindOverlappingCorners);

	// @todo: this should be shared with FOverlappingCorners

	const FVertexInstanceArray& VertexInstanceArray = MeshDescription.VertexInstances();
	const FVertexArray& VertexArray = MeshDescription.Vertices();

	int32 NumWedges = 3 * MeshDescription.Triangles().Num();

	// Empty the old data and reserve space for new
	OutOverlappingCorners.Init(NumWedges);

	// Create a list of vertex Z/index pairs
	TArray<MeshDescriptionOperationNamespace::FIndexAndZ> VertIndexAndZ;
	VertIndexAndZ.Reserve(NumWedges);

	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	int32 WedgeIndex = 0;
	for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		const TArray<FTriangleID>& TriangleIDs = MeshDescription.GetPolygonTriangleIDs(PolygonID);
		for (const FTriangleID TriangleID : TriangleIDs)
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVertexInstanceID VertexInstanceID = MeshDescription.GetTriangleVertexInstance(TriangleID, Corner);
				new(VertIndexAndZ)MeshDescriptionOperationNamespace::FIndexAndZ(WedgeIndex, VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceID)]);
				++WedgeIndex;
			}
		}
	}

	// Sort the vertices by z value
	VertIndexAndZ.Sort(MeshDescriptionOperationNamespace::FCompareIndexAndZ());

	// Search for duplicates, quickly!
	for (int32 i = 0; i < VertIndexAndZ.Num(); i++)
	{
		// only need to search forward, since we add pairs both ways
		for (int32 j = i + 1; j < VertIndexAndZ.Num(); j++)
		{
			if (FMath::Abs(VertIndexAndZ[j].Z - VertIndexAndZ[i].Z) > ComparisonThreshold)
				break; // can't be any more dups

			const FVector& PositionA = *(VertIndexAndZ[i].OriginalVector);
			const FVector& PositionB = *(VertIndexAndZ[j].OriginalVector);

			if (PositionA.Equals(PositionB, ComparisonThreshold))
			{
				OutOverlappingCorners.Add(VertIndexAndZ[i].Index, VertIndexAndZ[j].Index);
				OutOverlappingCorners.Add(VertIndexAndZ[j].Index, VertIndexAndZ[i].Index);
			}
		}
	}

	OutOverlappingCorners.FinishAdding();
}

struct FLayoutUVMeshDescriptionView final : FLayoutUV::IMeshView
{
	FMeshDescription& MeshDescription;
	TVertexAttributesConstRef<FVector> Positions;
	TVertexInstanceAttributesConstRef<FVector> Normals;
	TVertexInstanceAttributesRef<FVector2D> TexCoords;

	const uint32 SrcChannel;
	const uint32 DstChannel;

	uint32 NumIndices = 0;
	TArray<int32> RemapVerts;
	TArray<FVector2D> FlattenedTexCoords;

	FLayoutUVMeshDescriptionView(FMeshDescription& InMeshDescription, uint32 InSrcChannel, uint32 InDstChannel)
		: MeshDescription(InMeshDescription)
		, Positions(InMeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position))
		, Normals(InMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal))
		, TexCoords(InMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate))
		, SrcChannel(InSrcChannel)
		, DstChannel(InDstChannel)
	{
		uint32 NumTris = MeshDescription.Triangles().Num();

		NumIndices = NumTris * 3;

		FlattenedTexCoords.SetNumUninitialized(NumIndices);
		RemapVerts.SetNumUninitialized(NumIndices);

		int32 WedgeIndex = 0;

		for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
		{
			const TArray<FTriangleID>& TriangleIDs = MeshDescription.GetPolygonTriangleIDs(PolygonID);
			for (const FTriangleID TriangleID : TriangleIDs)
			{
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVertexInstanceID VertexInstanceID = MeshDescription.GetTriangleVertexInstance(TriangleID, Corner);

					FlattenedTexCoords[WedgeIndex] = TexCoords.Get(VertexInstanceID, SrcChannel);
					RemapVerts[WedgeIndex] = VertexInstanceID.GetValue();
					++WedgeIndex;
				}
			}
		}
	}

	uint32 GetNumIndices() const override { return NumIndices; }

	FVector GetPosition(uint32 Index) const override
	{
		FVertexInstanceID VertexInstanceID(RemapVerts[Index]);
		FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
		return Positions[VertexID];
	}

	FVector GetNormal(uint32 Index) const override
	{
		FVertexInstanceID VertexInstanceID(RemapVerts[Index]);
		return Normals[VertexInstanceID];
	}

	FVector2D GetInputTexcoord(uint32 Index) const override
	{
		return FlattenedTexCoords[Index];
	}

	void InitOutputTexcoords(uint32 Num) override
	{
		// If current DstChannel is out of range of the number of UVs defined by the mesh description, change the index count accordingly
		const uint32 NumUVs = TexCoords.GetNumIndices();
		if (DstChannel >= NumUVs)
		{
			TexCoords.SetNumIndices(DstChannel + 1);
			ensure(false);	// not expecting it to get here
		}
	}

	void SetOutputTexcoord(uint32 Index, const FVector2D& Value) override
	{
		const FVertexInstanceID VertexInstanceID(RemapVerts[Index]);
		TexCoords.Set(VertexInstanceID, DstChannel, Value);
	}
};

int32 FStaticMeshOperations::GetUVChartCount(FMeshDescription& MeshDescription, int32 SrcLightmapIndex, ELightmapUVVersion LightmapUVVersion, const FOverlappingCorners& OverlappingCorners)
{
	uint32 UnusedDstIndex = -1;
	FLayoutUVMeshDescriptionView MeshDescriptionView(MeshDescription, SrcLightmapIndex, UnusedDstIndex);
	FLayoutUV Packer(MeshDescriptionView);
	Packer.SetVersion(LightmapUVVersion);
	return Packer.FindCharts(OverlappingCorners);
}

bool FStaticMeshOperations::CreateLightMapUVLayout(FMeshDescription& MeshDescription,
	int32 SrcLightmapIndex,
	int32 DstLightmapIndex,
	int32 MinLightmapResolution,
	ELightmapUVVersion LightmapUVVersion,
	const FOverlappingCorners& OverlappingCorners)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshOperations::CreateLightMapUVLayout)

		FLayoutUVMeshDescriptionView MeshDescriptionView(MeshDescription, SrcLightmapIndex, DstLightmapIndex);
	FLayoutUV Packer(MeshDescriptionView);
	Packer.SetVersion(LightmapUVVersion);

	if (LightmapUVVersion >= ELightmapUVVersion::ForceLightmapPadding)
	{
		MinLightmapResolution -= 2;
	}

	Packer.FindCharts(OverlappingCorners);
	bool bPackSuccess = Packer.FindBestPacking(MinLightmapResolution);
	if (bPackSuccess)
	{
		Packer.CommitPackedUVs();
	}
	return bPackSuccess;
}

bool FStaticMeshOperations::GenerateUniqueUVsForStaticMesh(const FMeshDescription& MeshDescription, int32 TextureResolution, bool bMergeIdenticalMaterials, TArray<FVector2D>& OutTexCoords)
{
	// Create a copy of original mesh (only copy necessary data)
	FMeshDescription DuplicateMeshDescription(MeshDescription);


	//Make sure we have a destination UV TextureCoordinnate
	{
		TVertexInstanceAttributesRef<FVector2D> DuplicateVertexInstanceUVs = DuplicateMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		if (DuplicateVertexInstanceUVs.GetNumIndices() < 2)
		{
			DuplicateVertexInstanceUVs.SetNumIndices(2);
		}
	}

	TMap<FVertexInstanceID, FVertexInstanceID> RemapVertexInstance;
	//Remove the identical material
	if (bMergeIdenticalMaterials)
	{
		TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = DuplicateMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		TArray<FPolygonID> ToDeletePolygons;
		RemapVertexInstance.Reserve(DuplicateMeshDescription.VertexInstances().Num());
		TArray<FPolygonID> UniquePolygons;

		TArray<FVector2D> RefUVs;
		for (FPolygonID RefPolygonID : DuplicateMeshDescription.Polygons().GetElementIDs())
		{
			FPolygonGroupID RefPolygonGroupID = DuplicateMeshDescription.GetPolygonPolygonGroup(RefPolygonID);
			const TArray<FVertexInstanceID>& RefVertexInstances = DuplicateMeshDescription.GetPolygonVertexInstances(RefPolygonID);

			RefUVs.Empty(RefVertexInstances.Num() * VertexInstanceUVs.GetNumIndices());
			for (FVertexInstanceID RefVertexInstanceID : RefVertexInstances)
			{
				for (int32 UVChannel = 0; UVChannel < VertexInstanceUVs.GetNumIndices(); ++UVChannel)
				{
					RefUVs.Add(VertexInstanceUVs.Get(RefVertexInstanceID, UVChannel));
				}
			}

			FPolygonID MatchPolygonID = FPolygonID::Invalid;
			for (FPolygonID TestPolygonID : UniquePolygons)
			{
				FPolygonGroupID TestPolygonGroupID = DuplicateMeshDescription.GetPolygonPolygonGroup(TestPolygonID);
				if (TestPolygonGroupID != RefPolygonGroupID)
				{
					continue;
				}

				const TArray<FVertexInstanceID>& TestVertexInstances = DuplicateMeshDescription.GetPolygonVertexInstances(TestPolygonID);
				if (TestVertexInstances.Num() != RefVertexInstances.Num())
				{
					continue;
				}

				bool bIdentical = true;
				int32 UVIndex = 0;
				for (FVertexInstanceID TestVertexInstanceID : TestVertexInstances)
				{
					// All UV channels must match for polygons to be identical
					for (int32 UVChannel = 0; bIdentical && UVChannel < VertexInstanceUVs.GetNumIndices(); ++UVIndex, ++UVChannel)
					{
						bIdentical = VertexInstanceUVs.Get(TestVertexInstanceID, UVChannel) == RefUVs[UVIndex];
					}

					if (!bIdentical)
					{
						break;
					}
				}
				if (bIdentical)
				{
					MatchPolygonID = TestPolygonID;
					break;
				}
			}

			if (MatchPolygonID == FPolygonID::Invalid)
			{
				UniquePolygons.Add(RefPolygonID);
				for (FVertexInstanceID RefVertexInstanceID : RefVertexInstances)
				{
					RemapVertexInstance.Add(RefVertexInstanceID, RefVertexInstanceID);
				}
			}
			else
			{
				const TArray<FVertexInstanceID>& TestVertexInstances = DuplicateMeshDescription.GetPolygonVertexInstances(MatchPolygonID);
				int32 VertexInstanceIndex = 0;
				for (FVertexInstanceID RefVertexInstanceID : RefVertexInstances)
				{
					RemapVertexInstance.Add(RefVertexInstanceID, TestVertexInstances[VertexInstanceIndex]);
					VertexInstanceIndex++;
				}
				ToDeletePolygons.Add(RefPolygonID);
			}
		}

		//Delete polygons
		if (ToDeletePolygons.Num() > 0)
		{
			TArray<FEdgeID> OrphanedEdges;
			TArray<FVertexInstanceID> OrphanedVertexInstances;
			TArray<FPolygonGroupID> OrphanedPolygonGroups;
			TArray<FVertexID> OrphanedVertices;
			for (FPolygonID PolygonID : ToDeletePolygons)
			{
				DuplicateMeshDescription.DeletePolygon(PolygonID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
			}
			for (FPolygonGroupID PolygonGroupID : OrphanedPolygonGroups)
			{
				DuplicateMeshDescription.DeletePolygonGroup(PolygonGroupID);
			}
			for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstances)
			{
				DuplicateMeshDescription.DeleteVertexInstance(VertexInstanceID, &OrphanedVertices);
			}
			for (FEdgeID EdgeID : OrphanedEdges)
			{
				DuplicateMeshDescription.DeleteEdge(EdgeID, &OrphanedVertices);
			}
			for (FVertexID VertexID : OrphanedVertices)
			{
				DuplicateMeshDescription.DeleteVertex(VertexID);
			}
			//Avoid compacting the DuplicateMeshDescription, since the remap of the VertexInstaceID will not be good anymore
		}
	}
	// Find overlapping corners for UV generator. Allow some threshold - this should not produce any error in a case if resulting
	// mesh will not merge these vertices.
	FOverlappingCorners OverlappingCorners;
	FindOverlappingCorners(OverlappingCorners, DuplicateMeshDescription, THRESH_POINTS_ARE_SAME);

	// Generate new UVs
	FLayoutUVMeshDescriptionView DuplicateMeshDescriptionView(DuplicateMeshDescription, 0, 1);
	FLayoutUV Packer(DuplicateMeshDescriptionView);
	Packer.FindCharts(OverlappingCorners);

	bool bPackSuccess = Packer.FindBestPacking(FMath::Clamp(TextureResolution / 4, 32, 512));
	if (bPackSuccess)
	{
		Packer.CommitPackedUVs();
		TVertexInstanceAttributesConstRef<FVector2D> DupVertexInstanceUVs = DuplicateMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		// Save generated UVs
		check(DupVertexInstanceUVs.GetNumIndices() > 1);
		OutTexCoords.AddZeroed(VertexInstanceUVs.GetNumElements());
		int32 TextureCoordIndex = 0;
		for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			FVertexInstanceID RemapID = bMergeIdenticalMaterials ? RemapVertexInstance[VertexInstanceID] : VertexInstanceID;
			// Save generated UVs
			OutTexCoords[TextureCoordIndex] = DupVertexInstanceUVs.Get(RemapID, 1);	// UV1
			TextureCoordIndex++;
		}
	}

	return bPackSuccess;
}

bool FStaticMeshOperations::AddUVChannel(FMeshDescription& MeshDescription)
{
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (VertexInstanceUVs.GetNumIndices() >= MAX_MESH_TEXTURE_COORDS)
	{
		UE_LOG(LogStaticMeshOperations, Error, TEXT("AddUVChannel: Cannot add UV channel. Maximum number of UV channels reached (%d)."), MAX_MESH_TEXTURE_COORDS);
		return false;
	}

	VertexInstanceUVs.SetNumIndices(VertexInstanceUVs.GetNumIndices() + 1);
	return true;
}

bool FStaticMeshOperations::InsertUVChannel(FMeshDescription& MeshDescription, int32 UVChannelIndex)
{
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (UVChannelIndex < 0 || UVChannelIndex > VertexInstanceUVs.GetNumIndices())
	{
		UE_LOG(LogStaticMeshOperations, Error, TEXT("InsertUVChannel: Cannot insert UV channel. Given UV channel index %d is out of bounds."), UVChannelIndex);
		return false;
	}

	if (VertexInstanceUVs.GetNumIndices() >= MAX_MESH_TEXTURE_COORDS)
	{
		UE_LOG(LogStaticMeshOperations, Error, TEXT("InsertUVChannel: Cannot insert UV channel. Maximum number of UV channels reached (%d)."), MAX_MESH_TEXTURE_COORDS);
		return false;
	}

	VertexInstanceUVs.InsertIndex(UVChannelIndex);
	return true;
}

bool FStaticMeshOperations::RemoveUVChannel(FMeshDescription& MeshDescription, int32 UVChannelIndex)
{
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (VertexInstanceUVs.GetNumIndices() == 1)
	{
		UE_LOG(LogStaticMeshOperations, Error, TEXT("RemoveUVChannel: Cannot remove UV channel. There must be at least one channel."));
		return false;
	}

	if (UVChannelIndex < 0 || UVChannelIndex >= VertexInstanceUVs.GetNumIndices())
	{
		UE_LOG(LogStaticMeshOperations, Error, TEXT("RemoveUVChannel: Cannot remove UV channel. Given UV channel index %d is out of bounds."), UVChannelIndex);
		return false;
	}

	VertexInstanceUVs.RemoveIndex(UVChannelIndex);
	return true;
}

void FStaticMeshOperations::GeneratePlanarUV(const FMeshDescription& MeshDescription, const FUVMapParameters& Params, TMap<FVertexInstanceID, FVector2D>& OutTexCoords)
{
	// Project along X-axis (left view), UV along Z Y axes
	FVector U = FVector::UpVector;
	FVector V = FVector::RightVector;

	TMeshAttributesConstRef<FVertexID, FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	OutTexCoords.Reserve(MeshDescription.VertexInstances().Num());

	FVector Size = Params.Size * Params.Scale;
	FVector Offset = Params.Position - Size / 2.f;

	for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
		FVector Vertex = VertexPositions[VertexID];

		// Apply the gizmo transforms
		Vertex = Params.Rotation.RotateVector(Vertex);
		Vertex -= Offset;
		Vertex /= Size;

		float UCoord = FVector::DotProduct(Vertex, U) * Params.UVTile.X;
		float VCoord = FVector::DotProduct(Vertex, V) * Params.UVTile.Y;
		OutTexCoords.Add(VertexInstanceID, FVector2D(UCoord, VCoord));
	}
}

void FStaticMeshOperations::GenerateCylindricalUV(FMeshDescription& MeshDescription, const FUVMapParameters& Params, TMap<FVertexInstanceID, FVector2D>& OutTexCoords)
{
	FVector Size = Params.Size * Params.Scale;
	FVector Offset = Params.Position;

	// Cylinder along X-axis, counterclockwise from -Y axis as seen from left view
	FVector V = FVector::ForwardVector;
	Offset.X -= Size.X / 2.f;

	TMeshAttributesConstRef<FVertexID, FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	OutTexCoords.Reserve(MeshDescription.VertexInstances().Num());

	const float AngleOffset = PI; // offset to get the same result as in 3dsmax

	for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
		FVector Vertex = VertexPositions[VertexID];

		// Apply the gizmo transforms
		Vertex = Params.Rotation.RotateVector(Vertex);
		Vertex -= Offset;
		Vertex /= Size;

		float Angle = FMath::Atan2(Vertex.Z, Vertex.Y);

		Angle += AngleOffset;
		Angle *= Params.UVTile.X;

		float UCoord = Angle / (2 * PI);
		float VCoord = FVector::DotProduct(Vertex, V) * Params.UVTile.Y;

		OutTexCoords.Add(VertexInstanceID, FVector2D(UCoord, VCoord));
	}

	// Fix the UV coordinates for triangles at the seam where the angle wraps around
	for (const FPolygonID& PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		const TArray<FVertexInstanceID>& VertexInstances = MeshDescription.GetPolygonVertexInstances(PolygonID);
		int32 NumInstances = VertexInstances.Num();
		if (NumInstances >= 2)
		{
			for (int32 StartIndex = 0; StartIndex < NumInstances; ++StartIndex)
			{
				int32 EndIndex = StartIndex + 1;
				if (EndIndex >= NumInstances)
				{
					EndIndex = EndIndex % NumInstances;
				}

				const FVector2D& StartUV = OutTexCoords[VertexInstances[StartIndex]];
				FVector2D& EndUV = OutTexCoords[VertexInstances[EndIndex]];

				// TODO: Improve fix for UVTile other than 1
				float Threshold = 0.5f / Params.UVTile.X;
				if (FMath::Abs(EndUV.X - StartUV.X) > Threshold)
				{
					// Fix the U coordinate to get the texture go counterclockwise
					if (EndUV.X > Threshold)
					{
						if (EndUV.X >= 1.f)
						{
							EndUV.X -= 1.f;
						}
					}
					else
					{
						if (EndUV.X <= 0)
						{
							EndUV.X += 1.f;
						}
					}
				}
			}
		}
	}
}

void FStaticMeshOperations::GenerateBoxUV(const FMeshDescription& MeshDescription, const FUVMapParameters& Params, TMap<FVertexInstanceID, FVector2D>& OutTexCoords)
{
	FVector Size = Params.Size * Params.Scale;
	FVector HalfSize = Size / 2.0f;

	TMeshAttributesConstRef<FVertexID, FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	OutTexCoords.Reserve(MeshDescription.VertexInstances().Num());

	// Setup the UVs such that the mapping is from top-left to bottom-right when viewed orthographically
	TArray<TPair<FVector, FVector>> PlaneUVs;
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::ForwardVector, FVector::RightVector));	// Top view
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::BackwardVector, FVector::RightVector));	// Bottom view
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::ForwardVector, FVector::DownVector));		// Right view
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::BackwardVector, FVector::DownVector));	// Left view
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::LeftVector, FVector::DownVector));		// Front view
	PlaneUVs.Add(TPair<FVector, FVector>(FVector::RightVector, FVector::DownVector));		// Back view

	TArray<FPlane> BoxPlanes;
	const FVector& Center = Params.Position;

	BoxPlanes.Add(FPlane(Center + FVector(0, 0, HalfSize.Z), FVector::UpVector));		// Top plane
	BoxPlanes.Add(FPlane(Center - FVector(0, 0, HalfSize.Z), FVector::DownVector));		// Bottom plane
	BoxPlanes.Add(FPlane(Center + FVector(0, HalfSize.Y, 0), FVector::RightVector));	// Right plane
	BoxPlanes.Add(FPlane(Center - FVector(0, HalfSize.Y, 0), FVector::LeftVector));		// Left plane
	BoxPlanes.Add(FPlane(Center + FVector(HalfSize.X, 0, 0), FVector::ForwardVector));	// Front plane
	BoxPlanes.Add(FPlane(Center - FVector(HalfSize.X, 0, 0), FVector::BackwardVector));	// Back plane

	// For each polygon, find the box plane that best matches the polygon normal
	for (const FPolygonID& PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		const TArray<FVertexInstanceID>& VertexInstances = MeshDescription.GetPolygonVertexInstances(PolygonID);
		check(VertexInstances.Num() == 3);

		FVector Vertex0 = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstances[0])];
		FVector Vertex1 = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstances[1])];
		FVector Vertex2 = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstances[2])];

		FPlane PolygonPlane(Vertex0, Vertex2, Vertex1);

		// Find the box plane that is most aligned with the polygon plane
		// TODO: Also take the distance between the planes into consideration
		float MaxProj = 0.f;
		int32 BestPlaneIndex = 0;
		for (int32 Index = 0; Index < BoxPlanes.Num(); ++Index)
		{
			float Proj = FVector::DotProduct(BoxPlanes[Index], PolygonPlane);
			if (Proj > MaxProj)
			{
				MaxProj = Proj;
				BestPlaneIndex = Index;
			}
		}

		FVector U = PlaneUVs[BestPlaneIndex].Key;
		FVector V = PlaneUVs[BestPlaneIndex].Value;
		FVector Offset = Params.Position - HalfSize * (U + V);

		for (const FVertexInstanceID& VertexInstanceID : VertexInstances)
		{
			const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
			FVector Vertex = VertexPositions[VertexID];

			// Apply the gizmo transforms
			Vertex = Params.Rotation.RotateVector(Vertex);
			Vertex -= Offset;
			Vertex /= Size;

			float UCoord = FVector::DotProduct(Vertex, U) * Params.UVTile.X;
			float VCoord = FVector::DotProduct(Vertex, V) * Params.UVTile.Y;

			OutTexCoords.Add(VertexInstanceID, FVector2D(UCoord, VCoord));
		}
	}
}

void FStaticMeshOperations::SwapPolygonPolygonGroup(FMeshDescription& MeshDescription, int32 SectionIndex, int32 TriangleIndexStart, int32 TriangleIndexEnd, bool bRemoveEmptyPolygonGroup)
{
	int32 TriangleIndex = 0;
	TPolygonGroupAttributesRef<FName> PolygonGroupNames = MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	FPolygonGroupID TargetPolygonGroupID(SectionIndex);
	if (!bRemoveEmptyPolygonGroup)
	{
		while (!MeshDescription.PolygonGroups().IsValid(TargetPolygonGroupID))
		{
			TargetPolygonGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupNames[TargetPolygonGroupID] = FName(*(TEXT("SwapPolygonMaterialSlotName_") + FString::FromInt(TargetPolygonGroupID.GetValue())));
			TargetPolygonGroupID = FPolygonGroupID(SectionIndex);
		}
	}
	else
	{
		//This will not follow the SectionIndex value if the value is greater then the number of section (do not use this when merging meshes)
		if (!MeshDescription.PolygonGroups().IsValid(TargetPolygonGroupID))
		{
			TargetPolygonGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupNames[TargetPolygonGroupID] = FName(*(TEXT("SwapPolygonMaterialSlotName_") + FString::FromInt(TargetPolygonGroupID.GetValue())));
		}
	}

	for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		int32 TriangleCount = MeshDescription.GetPolygonTriangleIDs(PolygonID).Num();
		if (TriangleIndex >= TriangleIndexStart && TriangleIndex < TriangleIndexEnd)
		{
			check(TriangleIndex + (TriangleCount - 1) < TriangleIndexEnd);
			FPolygonGroupID OldpolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
			if (OldpolygonGroupID != TargetPolygonGroupID)
			{
				MeshDescription.SetPolygonPolygonGroup(PolygonID, TargetPolygonGroupID);
				if (bRemoveEmptyPolygonGroup && MeshDescription.GetPolygonGroupPolygons(OldpolygonGroupID).Num() < 1)
				{
					MeshDescription.DeletePolygonGroup(OldpolygonGroupID);
				}
			}
		}
		TriangleIndex += TriangleCount;
	}
}

bool FStaticMeshOperations::HasVertexColor(const FMeshDescription& MeshDescription)
{
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	bool bHasVertexColor = false;
	FVector4 WhiteColor(FLinearColor::White);
	for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		if (VertexInstanceColors[VertexInstanceID] != WhiteColor)
		{
			bHasVertexColor = true;
			break;
		}
	}
	return bHasVertexColor;
}

void FStaticMeshOperations::BuildWeldedVertexIDRemap(const FMeshDescription& MeshDescription, const float WeldingThreshold, TMap<FVertexID, FVertexID>& OutVertexIDRemap)
{
	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	int32 NumVertex = MeshDescription.Vertices().Num();
	OutVertexIDRemap.Reserve(NumVertex);

	// Create a list of vertex Z/index pairs
	TArray<MeshDescriptionOperationNamespace::FIndexAndZ> VertIndexAndZ;
	VertIndexAndZ.Reserve(NumVertex);

	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		new(VertIndexAndZ)MeshDescriptionOperationNamespace::FIndexAndZ(VertexID.GetValue(), VertexPositions[VertexID]);
	}

	// Sort the vertices by z value
	VertIndexAndZ.Sort(MeshDescriptionOperationNamespace::FCompareIndexAndZ());

	// Search for duplicates, quickly!
	for (int32 i = 0; i < VertIndexAndZ.Num(); i++)
	{
		FVertexID Index_i = FVertexID(VertIndexAndZ[i].Index);
		if (OutVertexIDRemap.Contains(Index_i))
		{
			continue;
		}
		OutVertexIDRemap.FindOrAdd(Index_i) = Index_i;
		// only need to search forward, since we add pairs both ways
		for (int32 j = i + 1; j < VertIndexAndZ.Num(); j++)
		{
			if (FMath::Abs(VertIndexAndZ[j].Z - VertIndexAndZ[i].Z) > WeldingThreshold)
				break; // can't be any more dups

			const FVector& PositionA = *(VertIndexAndZ[i].OriginalVector);
			const FVector& PositionB = *(VertIndexAndZ[j].OriginalVector);

			if (PositionA.Equals(PositionB, WeldingThreshold))
			{
				OutVertexIDRemap.FindOrAdd(FVertexID(VertIndexAndZ[j].Index)) = Index_i;
			}
		}
	}
}

FSHAHash FStaticMeshOperations::ComputeSHAHash(const FMeshDescription& MeshDescription)
{
	FSHA1 HashState;
	TArray< FName > AttributesNames;

	auto HashAttributeSet = [&AttributesNames, &HashState](const FAttributesSetBase& AttributeSet)
	{
		AttributesNames.Reset();
		AttributeSet.GetAttributeNames(AttributesNames);

		for (FName AttributeName : AttributesNames)
		{
			uint32 AttributeHash = AttributeSet.GetHash(AttributeName);
			HashState.Update((uint8*)&AttributeHash, sizeof(AttributeHash));
		}
	};

	HashAttributeSet(MeshDescription.VertexAttributes());
	HashAttributeSet(MeshDescription.VertexInstanceAttributes());
	HashAttributeSet(MeshDescription.EdgeAttributes());
	HashAttributeSet(MeshDescription.PolygonAttributes());
	HashAttributeSet(MeshDescription.PolygonGroupAttributes());

	FSHAHash OutHash;

	HashState.Final();
	HashState.GetHash(&OutHash.Hash[0]);

	return OutHash;
}

void FStaticMeshOperations::FlipPolygons(FMeshDescription& MeshDescription)
{
	TSet<FVertexInstanceID> VertexInstanceIDs;
	for (FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		VertexInstanceIDs.Append(MeshDescription.GetPolygonVertexInstances(PolygonID));
		MeshDescription.ReversePolygonFacing(PolygonID);
	}

	// Flip tangents and normals
	const TVertexInstanceAttributesRef<FVector> VertexNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	const TVertexInstanceAttributesRef<FVector> VertexTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);

	for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
	{
		// Just reverse the sign of the normals/tangents; note that since binormals are the cross product of normal with tangent, they are left untouched
		FVector Normal = VertexNormals[VertexInstanceID] * -1.0f;
		FVector Tangent = VertexTangents[VertexInstanceID] * -1.0f;

		TAttributesSet<FVertexInstanceID>& AttributesSet = MeshDescription.VertexInstanceAttributes();
		AttributesSet.SetAttribute(VertexInstanceID, MeshAttribute::VertexInstance::Normal, 0, Normal);
		AttributesSet.SetAttribute(VertexInstanceID, MeshAttribute::VertexInstance::Tangent, 0, Tangent);
	}
}

#undef LOCTEXT_NAMESPACE