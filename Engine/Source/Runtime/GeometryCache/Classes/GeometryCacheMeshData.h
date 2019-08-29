// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "DynamicMeshBuilder.h"
#include "UObject/GeometryObjectVersion.h"
#include "GeometryCacheMeshData.generated.h"

/** Stores per-batch data used for rendering */
USTRUCT()
struct FGeometryCacheMeshBatchInfo
{
	GENERATED_USTRUCT_BODY()

	/** Starting index into IndexBuffer to draw from */
	uint32 StartIndex;
	/** Total number of Triangles to draw */
	uint32 NumTriangles;
	/** Index to Material used to draw this batch */
	uint32 MaterialIndex;

	friend FArchive& operator<<(FArchive& Ar, FGeometryCacheMeshBatchInfo& Mesh)
	{
		Ar << Mesh.StartIndex;
		Ar << Mesh.NumTriangles;
		Ar << Mesh.MaterialIndex;
		return Ar;
	}
};

/** Stores per Track/Mesh data used for rendering*/
USTRUCT()
struct FGeometryCacheMeshData
{
	GENERATED_USTRUCT_BODY()

	FGeometryCacheMeshData() {}
	~FGeometryCacheMeshData()
	{
		Vertices.Empty();
		BatchesInfo.Empty();
		BoundingBox.Init();
	}
	
	/** Draw-able vertices */
	TArray<FDynamicMeshVertex> Vertices;
	/** Array of per-batch info structs*/
	TArray<FGeometryCacheMeshBatchInfo> BatchesInfo;
	/** Bounding box for this sample in the track */
	FBox BoundingBox;
	/** Indices for this sample, used for drawing the mesh */
	TArray<uint32> Indices;
		
	/** Serialization for FVertexAnimationSample. */
	friend FArchive& operator<<(FArchive& Ar, FGeometryCacheMeshData& Mesh)
	{
		Ar.UsingCustomVersion(FGeometryObjectVersion::GUID);

		int32 NumVertices = 0;
		
		if (Ar.IsSaving())
		{
			NumVertices = Mesh.Vertices.Num();
		}

		Ar << NumVertices;
		if (Ar.IsLoading())
		{
			Mesh.Vertices.AddUninitialized(NumVertices);
		}

		if (Ar.CustomVer(FGeometryObjectVersion::GUID) < FGeometryObjectVersion::CompressGeometryCache)
		{
			for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
			{
				FDynamicMeshVertex& Vertex = Mesh.Vertices[VertexIndex];
				Ar << Vertex.Position;
				Ar << Vertex.TextureCoordinate[0];
				Ar << Vertex.TangentX;
				Ar << Vertex.TangentZ;
				Ar << Vertex.Color;
			}
		}
		else if(NumVertices)
		{
			if (Ar.CustomVer(FGeometryObjectVersion::GUID) < FGeometryObjectVersion::DynamicMeshVertexLayoutChange)
			{
				struct FDummyVertex
				{
					FVector Position;
					FVector2D TextureCoordinate;
					FPackedNormal TangentX;
					FPackedNormal TangentZ;
					FColor Color;
				};

				TArray<FDummyVertex> DummyVertices;
				DummyVertices.AddUninitialized(NumVertices);

				Ar.SerializeCompressed(&DummyVertices[0], DummyVertices.Num()*DummyVertices.GetTypeSize(), COMPRESS_ZLIB);

				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					FDynamicMeshVertex& Vertex = Mesh.Vertices[VertexIndex];
					const FDummyVertex& DummyVertex = DummyVertices[VertexIndex];

					Vertex.Position = DummyVertex.Position;
					Vertex.TextureCoordinate[0] = DummyVertex.TextureCoordinate;
					Vertex.TangentX = DummyVertex.TangentX;
					Vertex.TangentZ = DummyVertex.TangentZ;
					Vertex.Color = DummyVertex.Color;
				}
			}
			else
			{
				Ar.SerializeCompressed(&Mesh.Vertices[0], Mesh.Vertices.Num()*Mesh.Vertices.GetTypeSize(), COMPRESS_ZLIB);
			}			
		}

	
		Ar << Mesh.BoundingBox;
		Ar << Mesh.BatchesInfo;
		Ar << Mesh.Indices;	

		return Ar;
	}

	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
	{
		// Calculate resource size according to what is actually serialized
		CumulativeResourceSize.AddUnknownMemoryBytes(Vertices.Num() * sizeof(FDynamicMeshVertex));
		CumulativeResourceSize.AddUnknownMemoryBytes(BatchesInfo.Num() * sizeof(FGeometryCacheMeshBatchInfo));
		CumulativeResourceSize.AddUnknownMemoryBytes(sizeof(Vertices));
		CumulativeResourceSize.AddUnknownMemoryBytes(sizeof(BatchesInfo));
		CumulativeResourceSize.AddUnknownMemoryBytes(sizeof(BoundingBox));
		CumulativeResourceSize.AddUnknownMemoryBytes(Indices.Num() * sizeof(uint32));
		CumulativeResourceSize.AddUnknownMemoryBytes(sizeof(Indices));
	}
};
