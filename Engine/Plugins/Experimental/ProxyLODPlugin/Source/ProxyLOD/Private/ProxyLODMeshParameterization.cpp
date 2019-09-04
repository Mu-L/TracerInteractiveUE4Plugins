// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ProxyLODMeshParameterization.h"

THIRD_PARTY_INCLUDES_START
#include <UVAtlasCode/UVAtlas/inc/UVAtlas.h>
#include <DirectXMeshCode/DirectXMesh/DirectXMesh.h>
THIRD_PARTY_INCLUDES_END


#include "ProxyLODThreadedWrappers.h"
#include "ProxyLODMeshUtilities.h"

bool ProxyLOD::GenerateUVs(FVertexDataMesh& InOutMesh, const FTextureAtlasDesc& TextureAtlasDesc, const bool VertexColorParts)
{
	// desired parameters for ISO-Chart method

	// MaxChartNum = 0 will allow any number of charts to be generated.

	const size_t MaxChartNumber = 0;

	// Let the polys in the partitions stretch some..  1.f will let it stretch freely

	const float MaxStretch = 0.125f; // Question.  Does this override the pIMT?
	
	// Note: I tried constructing this from the normals, but that resulted in some large planar regions being really compressed in the
	// UV chart.
	const bool bComputeIMTFromVertexNormal = false;

	// No Op
	auto NoOpCallBack = [](float percent)->HRESULT {return  S_OK; };

	return GenerateUVs(InOutMesh, TextureAtlasDesc, VertexColorParts, MaxStretch, MaxChartNumber, bComputeIMTFromVertexNormal, NoOpCallBack);
}

bool ProxyLOD::GenerateUVs(FVertexDataMesh& InOutMesh, const FTextureAtlasDesc& TextureAtlasDesc, const bool VertexColorParts, 
	                      const float MaxStretch, const size_t MaxChartNumber, const bool bComputeIMTFromVertexNormal, 
	                      std::function<HRESULT __cdecl(float percentComplete)> StatusCallBack, float* MaxStretchOut, size_t* NumChartsOut)
{

	std::vector<uint32> DirextXAdjacency;

	const bool bValidMesh = GenerateAdjacenyAndCleanMesh(InOutMesh, DirextXAdjacency);

	if (!bValidMesh) return false;


	// Data from the existing mesh

	const DirectX::XMFLOAT3* Pos = (DirectX::XMFLOAT3*) (InOutMesh.Points.GetData());
	const size_t NumVerts = InOutMesh.Points.Num();
	const size_t NumFaces = InOutMesh.Indices.Num() / 3;
	uint32* indices = InOutMesh.Indices.GetData();



	// The mesh adjacency

	const uint32* adjacency = DirextXAdjacency.data();


	// Size of the texture atlas

	const size_t width = TextureAtlasDesc.Size.X;
	const size_t height = TextureAtlasDesc.Size.Y;
	const float gutter = TextureAtlasDesc.Gutter;


	// Partition and mesh info to capture

	std::vector<DirectX::UVAtlasVertex> vb;
	std::vector<uint8> ib;
	std::vector<uint32> vertexRemapArray;
	std::vector<uint32> facePartitioning;

	// Capture stats about the result.
	float maxStretchUsed = 0.f;
	size_t numChartsUsed = 0;

	

	float * pIMTArray = new float[NumFaces * 3];
	if (!bComputeIMTFromVertexNormal)
	{
		for (int32 f = 0; f < NumFaces; ++f)
		{
			int32 offset = 3 * f;
			{
				pIMTArray[offset] = 1.f;
				pIMTArray[offset + 1] = 0.f;
				pIMTArray[offset + 2] = 1.f;
			}
		}
	}
	else
	{
		// per-triangle IMT from per-vertex data(normal). 

		const TArray<FVector>& Normals = InOutMesh.Normal;
		const float* PerVertSignal = (float*)Normals.GetData();
		size_t SignalStride = 3 * sizeof(float);
		HRESULT IMTResult = UVAtlasComputeIMTFromPerVertexSignal(Pos, NumVerts, indices, DXGI_FORMAT_R32_UINT, NumFaces, PerVertSignal, 3, SignalStride, StatusCallBack, pIMTArray);

		if (FAILED(IMTResult))
		{
			return false;
		}
	}

	HRESULT hr = DirectX::UVAtlasCreate(Pos, NumVerts,
		indices, DXGI_FORMAT_R32_UINT, NumFaces,
		MaxChartNumber, MaxStretch,
		width, height, gutter,
		adjacency, NULL /*false adj*/, pIMTArray /*IMTArray*/,
		StatusCallBack, DirectX::UVATLAS_DEFAULT_CALLBACK_FREQUENCY,
		DirectX::UVATLAS_DEFAULT, vb, ib,
		&facePartitioning, &vertexRemapArray, &maxStretchUsed, &numChartsUsed);

	if (MaxStretchOut)
	{
		*MaxStretchOut = maxStretchUsed;
	}
	if (NumChartsOut)
	{
		*NumChartsOut = numChartsUsed;
	}
	if (FAILED(hr)) return false;

	if (pIMTArray) delete[] pIMTArray;
	

	// testing
	check(ib.size() / sizeof(uint32) == NumFaces * 3);
	check(facePartitioning.size() == NumFaces);
	check(vertexRemapArray.size() == vb.size());

	// The mesh partitioning may split vertices, and this needs to be reflected in the mesh.

	// Update Faces
	{
		std::memcpy(indices, ib.data(), sizeof(uint32) * 3 * NumFaces);
	}

	// Add UVs
	{
		const size_t NumNewVerts = vb.size();
		ResizeArray(InOutMesh.UVs, NumNewVerts);

		FVector2D* UVCoords = InOutMesh.UVs.GetData();
		size_t j = 0;
		for (auto it = vb.cbegin(); it != vb.cend() && j < NumNewVerts; ++it, ++j)
		{
			const auto& UV = it->uv;
			UVCoords[j] = FVector2D(UV.x, UV.y);
		}

	}

	ProxyLOD::FTaskGroup TaskGroup;


	TaskGroup.Run([&]()
	{
		const size_t NumNewVerts = vb.size();
		bool bReducedVertCount = NumNewVerts < NumVerts;
		check(!bReducedVertCount);

		// Copy the New Verts into a TArray so we can do a swap.
		TArray<FVector> NewVertArray;
		ResizeArray(NewVertArray, NumNewVerts);

		// re-order the verts 
		DirectX::UVAtlasApplyRemap(InOutMesh.Points.GetData(), sizeof(FVector), NumVerts, NumNewVerts, vertexRemapArray.data(), NewVertArray.GetData());

		// swap the data into the raw mesh 
		Swap(NewVertArray, InOutMesh.Points);
	});

	// Update the normals
	TaskGroup.Run([&]()
	{
		const size_t NumNewVerts = vb.size();
		TArray<FVector> NewNormalsArray;
		ResizeArray(NewNormalsArray, NumNewVerts);

		// re-order the verts 
		DirectX::UVAtlasApplyRemap(InOutMesh.Normal.GetData(), sizeof(FVector), NumVerts, NumNewVerts, vertexRemapArray.data(), NewNormalsArray.GetData());

		// swap the data into the raw mesh 
		Swap(NewNormalsArray, InOutMesh.Normal);

	});

	// Update the transfer normals
	TaskGroup.Run([&]()
	{
		const size_t NumNewVerts = vb.size();
		TArray<FVector> NewTransferNormalsArray;
		ResizeArray(NewTransferNormalsArray, NumNewVerts);

		// re-order the verts 
		DirectX::UVAtlasApplyRemap(InOutMesh.TransferNormal.GetData(), sizeof(FVector), NumVerts, NumNewVerts, vertexRemapArray.data(), NewTransferNormalsArray.GetData());

		// swap the data into the raw mesh 
		Swap(NewTransferNormalsArray, InOutMesh.TransferNormal);

	});

	// Update the tangents
	TaskGroup.Run([&]()
	{
		const size_t NumNewVerts = vb.size();
		TArray<FVector> NewTangentArray;
		ResizeArray(NewTangentArray, NumNewVerts);

		// re-order the verts 
		DirectX::UVAtlasApplyRemap(InOutMesh.Tangent.GetData(), sizeof(FVector), NumVerts, NumNewVerts, vertexRemapArray.data(), NewTangentArray.GetData());

		// swap the data into the raw mesh 
		Swap(NewTangentArray, InOutMesh.Tangent);

	});

	TaskGroup.Run([&]()
	{
		const size_t NumNewVerts = vb.size();
		TArray<FVector> NewBiTangentArray;
		ResizeArray(NewBiTangentArray, NumNewVerts);

		// re-order the verts 
		DirectX::UVAtlasApplyRemap(InOutMesh.BiTangent.GetData(), sizeof(FVector), NumVerts, NumNewVerts, vertexRemapArray.data(), NewBiTangentArray.GetData());

		// swap the data into the raw mesh 
		Swap(NewBiTangentArray, InOutMesh.BiTangent);

	});

	TaskGroup.Wait();

	ResizeArray(InOutMesh.FacePartition, NumFaces);

	ProxyLOD::Parallel_For(ProxyLOD::FIntRange(0, NumFaces),
		[&InOutMesh, &facePartitioning](const ProxyLOD::FIntRange& Range)
	{
		auto& FacePartition = InOutMesh.FacePartition;
		for (int32 f = Range.begin(), F = Range.end(); f < F; ++f)
		{
			FacePartition[f] = facePartitioning[f];
		}
	});

	if (VertexColorParts)
	{
		// Color the verts by partition for debuging.

		ColorPartitions(InOutMesh, facePartitioning);
	}

	return true;
}


void ProxyLOD::GenerateAdjacency(const FAOSMesh& AOSMesh, std::vector<uint32>& AdjacencyArray)
{
	const uint32 NumTris = AOSMesh.GetNumIndexes() / 3;
	const uint32 AdjacencySize = AOSMesh.GetNumIndexes(); // = 3 for each face.

														  // Get the positions as a single array.
	std::vector<FVector> PosArray;
	AOSMesh.GetPosArray(PosArray);

	// Allocate adjacency
	AdjacencyArray.resize(AdjacencySize);

	// position comparison epsilon
	const float Eps = 0.f;
	HRESULT hr = DirectX::GenerateAdjacencyAndPointReps(AOSMesh.Indexes, NumTris, (DirectX::XMFLOAT3*)PosArray.data(), PosArray.size(), Eps, NULL /* optional point rep pointer*/, AdjacencyArray.data());

}

void ProxyLOD::GenerateAdjacency(const FVertexDataMesh& Mesh, std::vector<uint32>& AdjacencyArray)
{
	const uint32 NumTris = Mesh.Indices.Num() / 3;
	const uint32 AdjacencySize = Mesh.Indices.Num(); // = 3 for each face.

													 // Get the positions as a single array.
	const FVector* PosArray = Mesh.Points.GetData();
	const uint32 NumPos = Mesh.Points.Num();

	// Allocate adjacency
	AdjacencyArray.resize(AdjacencySize);

	// position comparison epsilon
	const float Eps = 0.f;
	HRESULT hr = DirectX::GenerateAdjacencyAndPointReps(Mesh.Indices.GetData(), NumTris, (const DirectX::XMFLOAT3*)PosArray, NumPos, Eps, NULL /* optional point rep pointer*/, AdjacencyArray.data());

}
void ProxyLOD::GenerateAdjacency(const FMeshDescription& RawMesh, std::vector<uint32>& AdjacencyArray)
{
	uint32 NumTris = 0;
	for (const FPolygonID& PolygonID : RawMesh.Polygons().GetElementIDs())
	{
		NumTris += RawMesh.GetPolygonTriangles(PolygonID).Num();
	}
	const uint32 NumVerts = RawMesh.Vertices().Num();
	const uint32 AdjacencySize = RawMesh.VertexInstances().Num(); // 3 for each face

															 // Allocate adjacency
	AdjacencyArray.resize(AdjacencySize);

	TVertexAttributesConstRef<FVector> VertexPositionsAttribute = RawMesh.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TArray<FVector> VertexPositions;
	VertexPositions.AddZeroed(NumVerts);
	for (const FVertexID VertexID : RawMesh.Vertices().GetElementIDs())
	{
		VertexPositions[VertexID.GetValue()] = VertexPositionsAttribute[VertexID];
	}

	TArray<uint32> Indices;
	Indices.AddZeroed(AdjacencySize);

	for (const FVertexInstanceID VertexInstanceID : RawMesh.VertexInstances().GetElementIDs())
	{
		Indices[VertexInstanceID.GetValue()] = RawMesh.GetVertexInstanceVertex(VertexInstanceID).GetValue();
	}
	// position comparison epsilon
	const float Eps = 0.f;
	HRESULT hr = DirectX::GenerateAdjacencyAndPointReps(Indices.GetData(), NumTris, (DirectX::XMFLOAT3*)VertexPositions.GetData(), NumVerts, Eps, NULL /* optional point rep pointer*/, AdjacencyArray.data());
}

bool ProxyLOD::GenerateAdjacenyAndCleanMesh(FVertexDataMesh& InOutMesh, std::vector<uint32>& Adjacency)
{
	std::vector<uint32_t> dupVerts;

	uint32 CleanCount = 0;
	while (CleanCount == 0 || (dupVerts.size() != 0 && CleanCount < 5))
	{
		CleanCount++;

		// Rebuild the adjacency.  
		Adjacency.clear();
		GenerateAdjacency(InOutMesh, Adjacency);

		dupVerts.clear();
		HRESULT hr = DirectX::Clean(InOutMesh.Indices.GetData(), InOutMesh.Indices.Num() / 3, InOutMesh.Points.Num(), Adjacency.data(), NULL, dupVerts, true /*break bowties*/);

		SplitVertices(InOutMesh, dupVerts);

		uint32 Offset = InOutMesh.Points.Num();

		// spatially separate bowties
		for (int32 f = 0; f < InOutMesh.Indices.Num() / 3; ++f)
		{
			for (int32 v = 0; v < 3; ++v)
			{
				const uint32 Idx = InOutMesh.Indices[3 * f + v];

				// This is a duplicate vert, find it's triangle and push it towards the center.
				if (Idx > Offset - 1)
				{
					const uint32 TriIds[3] = { InOutMesh.Indices[3 * f], InOutMesh.Indices[3 * f + 1], InOutMesh.Indices[3 * f + 2] };

					// compute center of this face
					FVector CenterOfFace = InOutMesh.Points[TriIds[0]] + InOutMesh.Points[TriIds[1]] + InOutMesh.Points[TriIds[2]];
					CenterOfFace /= 3.f;

					// Vector to center
					FVector PointToCenter = (InOutMesh.Points[Idx] - CenterOfFace);
					PointToCenter.Normalize();

					// move vert towards center
					InOutMesh.Points[Idx] = InOutMesh.Points[Idx] - 0.0001f * PointToCenter;
				}
			}
		}
	}

	return (dupVerts.size() == 0);

}