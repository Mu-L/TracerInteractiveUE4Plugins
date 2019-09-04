// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "DynamicMeshEditor.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/BufferUtil.h"
#include "MeshRegionBoundaryLoops.h"



void FMeshIndexMappings::Initialize(FDynamicMesh3* Mesh)
{
	if (Mesh->HasAttributes())
	{
		FDynamicMeshAttributeSet* Attribs = Mesh->Attributes();
		UVMaps.SetNum(Attribs->NumUVLayers());
		NormalMaps.SetNum(Attribs->NumNormalLayers());
	}
}





void FDynamicMeshEditResult::GetAllTriangles(TArray<int>& TrianglesOut) const
{
	BufferUtil::AppendElements(TrianglesOut, NewTriangles);

	int NumQuads = NewQuads.Num();
	for (int k = 0; k < NumQuads; ++k)
	{
		TrianglesOut.Add(NewQuads[k].A);
		TrianglesOut.Add(NewQuads[k].B);
	}
	int NumPolys = NewPolygons.Num();
	for (int k = 0; k < NumPolys; ++k)
	{
		BufferUtil::AppendElements(TrianglesOut, NewPolygons[k]);
	}
}






bool FDynamicMeshEditor::StitchVertexLoopsMinimal(const TArray<int>& Loop1, const TArray<int>& Loop2, FDynamicMeshEditResult& ResultOut)
{
	int N = Loop1.Num();
	checkf(N == Loop2.Num(), TEXT("FDynamicMeshEditor::StitchLoop: loops are not the same length!"));
	if (N != Loop2.Num())
	{
		return false;
	}

	ResultOut.NewQuads.Reserve(N);
	ResultOut.NewGroups.Reserve(N);

	int i = 0;
	for (; i < N; ++i) 
	{
		int a = Loop1[i];
		int b = Loop1[(i + 1) % N];
		int c = Loop2[i];
		int d = Loop2[(i + 1) % N];

		int NewGroupID = Mesh->AllocateTriangleGroup();
		ResultOut.NewGroups.Add(NewGroupID);

		FIndex3i t1(b, a, d);
		int tid1 = Mesh->AppendTriangle(t1, NewGroupID);

		FIndex3i t2(a, c, d);
		int tid2 = Mesh->AppendTriangle(t2, NewGroupID);

		ResultOut.NewQuads.Add(FIndex2i(tid1, tid2));

		if (tid1 < 0 || tid2 < 0)
		{
			goto operation_failed;
		}
	}

	return true;

operation_failed:
	// remove what we added so far
	if (i > 0) 
	{
		ResultOut.NewTriangles.SetNum(2 * i + 1);
		if (RemoveTriangles(ResultOut.NewTriangles, false) == false)
		{
			checkf(false, TEXT("FDynamicMeshEditor::StitchLoop: failed to add all triangles, and also failed to back out changes."));
		}
	}
	return false;
}




bool FDynamicMeshEditor::RemoveTriangles(const TArray<int>& Triangles, bool bRemoveIsolatedVerts)
{
	bool bAllOK = true;
	int NumTriangles = Triangles.Num();
	for (int i = 0; i < NumTriangles; ++i) 
	{
		if (Mesh->IsTriangle(Triangles[i]) == false)
		{
			continue;
		}

		EMeshResult result = Mesh->RemoveTriangle(Triangles[i], bRemoveIsolatedVerts, false);
		if (result != EMeshResult::Ok)
		{
			bAllOK = false;
		}
	}
	return bAllOK;
}





/**
 * Make a copy of provided triangles, with new vertices. You provide IndexMaps because
 * you know if you are doing a small subset or a full-mesh-copy.
 */
void FDynamicMeshEditor::DuplicateTriangles(const TArray<int>& Triangles, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	ResultOut.Reset();
	IndexMaps.Initialize(Mesh);

	for (int TriangleID : Triangles) 
	{
		FIndex3i Tri = Mesh->GetTriangle(TriangleID);

		int NewGroupID = FindOrCreateDuplicateGroup(TriangleID, IndexMaps, ResultOut);

		FIndex3i NewTri;
		NewTri[0] = FindOrCreateDuplicateVertex(Tri[0], IndexMaps, ResultOut);
		NewTri[1] = FindOrCreateDuplicateVertex(Tri[1], IndexMaps, ResultOut);
		NewTri[2] = FindOrCreateDuplicateVertex(Tri[2], IndexMaps, ResultOut);

		int NewTriangleID = Mesh->AppendTriangle(NewTri, NewGroupID);
		IndexMaps.SetTriangle(TriangleID, NewTriangleID);
		ResultOut.NewTriangles.Add(NewTriangleID);

		CopyAttributes(TriangleID, NewTriangleID, IndexMaps, ResultOut);

		//Mesh->CheckValidity(true);
	}

}




bool FDynamicMeshEditor::DisconnectTriangles(const TArray<int>& Triangles, TArray<FLoopPairSet>& LoopSetOut)
{
	check(Mesh->HasAttributes() == false);  // not supported yet

	// find the region boundary loops
	FMeshRegionBoundaryLoops RegionLoops(Mesh, Triangles, false);
	bool bOK = RegionLoops.Compute();
	check(bOK);
	if (!bOK)
	{
		return false;
	}
	TArray<FEdgeLoop>& Loops = RegionLoops.Loops;

	// need to test Contains() many times
	TSet<int> TriangleSet;
	TriangleSet.Reserve(Triangles.Num() * 3);
	for (int TriID : Triangles)
	{
		TriangleSet.Add(TriID);
	}

	// process each loop island
	int NumLoops = Loops.Num();
	LoopSetOut.SetNum(NumLoops);
	for ( int li = 0; li < NumLoops; ++li)
	{
		FEdgeLoop& Loop = Loops[li];
		FLoopPairSet& LoopPair = LoopSetOut[li];
		LoopPair.LoopA = Loop;

		// duplicate the vertices
		int NumVertices = Loop.Vertices.Num();
		TMap<int, int> LoopVertexMap; LoopVertexMap.Reserve(NumVertices);
		TArray<int> NewVertexLoop; NewVertexLoop.SetNum(NumVertices);
		for (int vi = 0; vi < NumVertices; ++vi)
		{
			int VertID = Loop.Vertices[vi];
			int NewVertID = Mesh->AppendVertex(*Mesh, VertID);
			LoopVertexMap.Add(Loop.Vertices[vi], NewVertID);
			NewVertexLoop[vi] = NewVertID;
		}

		// for each border triangle, rewrite vertices
		int NumEdges = Loop.Edges.Num();
		for (int ei = 0; ei < NumEdges; ++ei)
		{
			int EdgeID = Loop.Edges[ei];
			FIndex2i EdgeTris = Mesh->GetEdgeT(EdgeID);
			int EditTID = TriangleSet.Contains(EdgeTris.A) ? EdgeTris.A : EdgeTris.B;
			if (EditTID == FDynamicMesh3::InvalidID)
			{
				continue;		// happens on final edge, and on input boundary edges
			}

			FIndex3i OldTri = Mesh->GetTriangle(EditTID);
			FIndex3i NewTri = OldTri;
			int Modified = 0;
			for (int j = 0; j < 3; ++j)
			{
				const int* NewVertID = LoopVertexMap.Find(OldTri[j]);
				if (NewVertID != nullptr)
				{
					NewTri[j] = *NewVertID;
					++Modified;
				}
			}
			if (Modified > 0)
			{
				Mesh->SetTriangle(EditTID, NewTri, false);
			}
		}

		LoopPair.LoopB.InitializeFromVertices(Mesh, NewVertexLoop, false);
	}

	return true;
}




FVector3f FDynamicMeshEditor::ComputeAndSetQuadNormal(const FIndex2i& QuadTris, bool bIsPlanar)
{
	FVector3f Normal(0, 0, 1);
	if (bIsPlanar)
	{
		Normal = (FVector3f)Mesh->GetTriNormal(QuadTris.A);
	}
	else
	{
		Normal = (FVector3f)Mesh->GetTriNormal(QuadTris.A);
		Normal += (FVector3f)Mesh->GetTriNormal(QuadTris.B);
		Normal.Normalize();
	}
	SetQuadNormals(QuadTris, Normal);
	return Normal;
}




void FDynamicMeshEditor::SetQuadNormals(const FIndex2i& QuadTris, const FVector3f& Normal)
{
	check(Mesh->HasAttributes());
	FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();

	FIndex3i Triangle1 = Mesh->GetTriangle(QuadTris.A);

	FIndex3i NormalTriangle1;
	NormalTriangle1[0] = Normals->AppendElement(Normal, Triangle1[0]);
	NormalTriangle1[1] = Normals->AppendElement(Normal, Triangle1[1]);
	NormalTriangle1[2] = Normals->AppendElement(Normal, Triangle1[2]);
	Normals->SetTriangle(QuadTris.A, NormalTriangle1);

	if (Mesh->IsTriangle(QuadTris.B))
	{
		FIndex3i Triangle2 = Mesh->GetTriangle(QuadTris.B);
		FIndex3i NormalTriangle2;
		for (int j = 0; j < 3; ++j)
		{
			int i = Triangle1.IndexOf(Triangle2[j]);
			if (i == -1)
			{
				NormalTriangle2[j] = Normals->AppendElement(Normal, Triangle2[j]);
			}
			else
			{
				NormalTriangle2[j] = NormalTriangle1[i];
			}
		}
		Normals->SetTriangle(QuadTris.B, NormalTriangle2);
	}

}


void FDynamicMeshEditor::SetTriangleNormals(const TArray<int>& Triangles, const FVector3f& Normal)
{
	check(Mesh->HasAttributes());
	FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();

	TMap<int, int> Vertices;

	for (int tid : Triangles)
	{
		FIndex3i BaseTri = Mesh->GetTriangle(tid);
		FIndex3i ElemTri;
		for (int j = 0; j < 3; ++j)
		{
			const int* FoundElementID = Vertices.Find(BaseTri[j]);
			if (FoundElementID == nullptr)
			{
				ElemTri[j] = Normals->AppendElement(Normal, BaseTri[j]);
				Vertices.Add(BaseTri[j], ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}
		Normals->SetTriangle(tid, ElemTri);
	}
}





void FDynamicMeshEditor::SetQuadUVsFromProjection(const FIndex2i& QuadTris, const FFrame3f& ProjectionFrame, float UVScaleFactor, int UVLayerIndex)
{
	check(Mesh->HasAttributes() && Mesh->Attributes()->NumUVLayers() > UVLayerIndex );
	FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

	FIndex4i AllUVIndices(-1, -1, -1, -1);
	FVector2f AllUVs[4];

	// project first triangle
	FIndex3i Triangle1 = Mesh->GetTriangle(QuadTris.A);
	FIndex3i UVTriangle1;
	for (int j = 0; j < 3; ++j)
	{
		FVector2f UV = ProjectionFrame.ToPlaneUV( (FVector3f)Mesh->GetVertex(Triangle1[j]), 2);
		UVTriangle1[j] = UVs->AppendElement(UV, Triangle1[j]);
		AllUVs[j] = UV;
		AllUVIndices[j] = UVTriangle1[j];
	}
	UVs->SetTriangle(QuadTris.A, UVTriangle1);

	// project second triangle
	if (Mesh->IsTriangle(QuadTris.B))
	{
		FIndex3i Triangle2 = Mesh->GetTriangle(QuadTris.B);
		FIndex3i UVTriangle2;
		for (int j = 0; j < 3; ++j)
		{
			int i = Triangle1.IndexOf(Triangle2[j]);
			if (i == -1)
			{
				FVector2f UV = ProjectionFrame.ToPlaneUV( (FVector3f)Mesh->GetVertex(Triangle2[j]), 2);
				UVTriangle2[j] = UVs->AppendElement(UV, Triangle2[j]);
				AllUVs[3] = UV;
				AllUVIndices[3] = UVTriangle2[j];
			}
			else
			{
				UVTriangle2[j] = UVTriangle1[i];
			}
		}
		UVs->SetTriangle(QuadTris.B, UVTriangle2);
	}

	// shift UVs so that their bbox min-corner is at origin and scaled by external scale factor
	FAxisAlignedBox2f UVBounds(FAxisAlignedBox2f::Empty());
	UVBounds.Contain(AllUVs[0]);  UVBounds.Contain(AllUVs[1]);  UVBounds.Contain(AllUVs[2]);
	if (AllUVIndices[3] != -1)
	{
		UVBounds.Contain(AllUVs[3]);
	}
	for (int j = 0; j < 4; ++j)
	{
		if (AllUVIndices[j] != -1)
		{
			FVector2f TransformedUV = (AllUVs[j] - UVBounds.Min) * UVScaleFactor;
			UVs->SetElement(AllUVIndices[j], TransformedUV);
		}
	}
}






void FDynamicMeshEditor::ReverseTriangleOrientations(const TArray<int>& Triangles, bool bInvertNormals)
{
	for (int tid : Triangles)
	{
		Mesh->ReverseTriOrientation(tid);
	}
	if (bInvertNormals)
	{
		InvertTriangleNormals(Triangles);
	}
}


void FDynamicMeshEditor::InvertTriangleNormals(const TArray<int>& Triangles)
{
	// @todo re-use the TBitA

	if (Mesh->HasVertexNormals())
	{
		TBitArray<FDefaultBitArrayAllocator> DoneVertices(false, Mesh->MaxVertexID());
		for (int TriangleID : Triangles)
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);
			for (int j = 0; j < 3; ++j)
			{
				if (DoneVertices[Tri[j]] == false)
				{
					Mesh->SetVertexNormal(Tri[j], -Mesh->GetVertexNormal(Tri[j]));
					DoneVertices[Tri[j]] = true;
				}
			}
		}
	}


	if (Mesh->HasAttributes())
	{
		for (FDynamicMeshNormalOverlay* Normals : Mesh->Attributes()->GetAllNormalLayers())
		{
			TBitArray<FDefaultBitArrayAllocator> DoneNormals(false, Normals->MaxElementID());
			for (int TriangleID : Triangles)
			{
				FIndex3i ElemTri = Normals->GetTriangle(TriangleID);
				for (int j = 0; j < 3; ++j)
				{
					if (DoneNormals[ElemTri[j]] == false)
					{
						Normals->SetElement(ElemTri[j], -Normals->GetElement(ElemTri[j]));
						DoneNormals[ElemTri[j]] = true;
					}
				}
			}
		}
	}
}





void FDynamicMeshEditor::CopyAttributes(int FromTriangleID, int ToTriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	if (Mesh->HasAttributes() == false)
	{
		return;
	}

	int UVLayerIndex = 0;
	for (FDynamicMeshUVOverlay* UVOverlay : Mesh->Attributes()->GetAllUVLayers())
	{
		FIndex3i FromElemTri = UVOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = UVOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID )
			{
				int NewElemID = FindOrCreateDuplicateUV(FromElemTri[j], UVLayerIndex, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		UVOverlay->SetTriangle(ToTriangleID, ToElemTri);
		UVLayerIndex++;
	}


	int NormalLayerIndex = 0;
	for (FDynamicMeshNormalOverlay* NormalOverlay : Mesh->Attributes()->GetAllNormalLayers())
	{
		FIndex3i FromElemTri = NormalOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = NormalOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID)
			{
				int NewElemID = FindOrCreateDuplicateNormal(FromElemTri[j], NormalLayerIndex, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		NormalOverlay->SetTriangle(ToTriangleID, ToElemTri);
		NormalLayerIndex++;
	}
}



int FDynamicMeshEditor::FindOrCreateDuplicateUV(int ElementID, int UVLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewUV(UVLayerIndex, ElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = UVOverlay->GetParentVertex(ElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = UVOverlay->AppendElement(
			UVOverlay->GetElement(ElementID), NewParentVertexID);

		IndexMaps.SetUV(UVLayerIndex, ElementID, NewElementID);
	}
	return NewElementID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateNormal(int ElementID, int NormalLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewNormal(NormalLayerIndex, ElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->GetNormalLayer(NormalLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = NormalOverlay->GetParentVertex(ElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = NormalOverlay->AppendElement(
			NormalOverlay->GetElement(ElementID), NewParentVertexID);

		IndexMaps.SetNormal(NormalLayerIndex, ElementID, NewElementID);
	}
	return NewElementID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateVertex(int VertexID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	int NewVertexID = IndexMaps.GetNewVertex(VertexID);
	if (NewVertexID == IndexMaps.InvalidID())
	{
		NewVertexID = Mesh->AppendVertex(*Mesh, VertexID);
		IndexMaps.SetVertex(VertexID, NewVertexID);
		ResultOut.NewVertices.Add(NewVertexID);
	}
	return NewVertexID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateGroup(int TriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	int GroupID = Mesh->GetTriangleGroup(TriangleID);
	int NewGroupID = IndexMaps.GetNewGroup(GroupID);
	if (NewGroupID == IndexMaps.InvalidID())
	{
		NewGroupID = Mesh->AllocateTriangleGroup();
		IndexMaps.SetGroup(GroupID, NewGroupID);
		ResultOut.NewGroups.Add(NewGroupID);
	}
	return NewGroupID;
}




void FDynamicMeshEditor::AppendMesh(const FDynamicMesh3* AppendMesh,
	FMeshIndexMappings& IndexMapsOut, FDynamicMeshEditResult& ResultOut,
	TFunction<FVector3d(int, const FVector3d&)> PositionTransform,
	TFunction<FVector3f(int, const FVector3f&)> NormalTransform)
{
	IndexMapsOut.Reset();

	FIndexMapi& VertexMap = IndexMapsOut.GetVertexMap();
	VertexMap.Reserve(AppendMesh->VertexCount());
	for (int VertID : AppendMesh->VertexIndicesItr())
	{
		FVector3d Position = AppendMesh->GetVertex(VertID);
		if (PositionTransform != nullptr)
		{
			Position = PositionTransform(VertID, Position);
		}
		int NewVertID = Mesh->AppendVertex(Position);
		VertexMap.Add(VertID, NewVertID);

		if (AppendMesh->HasVertexNormals() && Mesh->HasVertexNormals())
		{
			FVector3f Normal = AppendMesh->GetVertexNormal(VertID);
			if (NormalTransform != nullptr)
			{
				Normal = NormalTransform(VertID, Normal);
			}
			Mesh->SetVertexNormal(NewVertID, Normal);
		}

		if (AppendMesh->HasVertexColors() && Mesh->HasVertexColors()) 
		{
			FVector3f Color = AppendMesh->GetVertexColor(VertID);
			Mesh->SetVertexColor(NewVertID, Color);
		}
	}

	for (int TriID : AppendMesh->TriangleIndicesItr())
	{
		FIndex3i Tri = AppendMesh->GetTriangle(TriID);
		int NewTriID = Mesh->AppendTriangle(VertexMap.GetTo(Tri.A), VertexMap.GetTo(Tri.B), VertexMap.GetTo(Tri.C));
	}
}