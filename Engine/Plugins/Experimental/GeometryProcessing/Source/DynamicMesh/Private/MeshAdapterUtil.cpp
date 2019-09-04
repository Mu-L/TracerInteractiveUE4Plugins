// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshAdapterUtil.h"

FPointSetAdapterd MeshAdapterUtil::MakeVerticesAdapter(const FDynamicMesh3* Mesh)
{
	FPointSetAdapterd Adapter;
	Adapter.MaxPointID = [Mesh]() { return Mesh->MaxVertexID(); };
	Adapter.PointCount = [Mesh]() { return Mesh->VertexCount(); };
	Adapter.IsPoint = [Mesh](int Idx) { return Mesh->IsVertex(Idx); };
	Adapter.GetPoint = [Mesh](int Idx) { return Mesh->GetVertex(Idx); };
	Adapter.Timestamp = [Mesh] { return Mesh->GetTimestamp(); };

	Adapter.HasNormals = [Mesh] { return Mesh->HasVertexNormals(); };
	Adapter.GetPointNormal = [Mesh](int Idx) {return Mesh->GetVertexNormal(Idx); };

	return Adapter;
}


FPointSetAdapterd MeshAdapterUtil::MakeTriCentroidsAdapter(const FDynamicMesh3* Mesh)
{
	FPointSetAdapterd Adapter;
	Adapter.MaxPointID = [Mesh]() { return Mesh->MaxTriangleID(); };
	Adapter.PointCount = [Mesh]() { return Mesh->TriangleCount(); };
	Adapter.IsPoint = [Mesh](int Idx) { return Mesh->IsTriangle(Idx); };
	Adapter.GetPoint = [Mesh](int Idx) { return Mesh->GetTriCentroid(Idx); };
	Adapter.Timestamp = [Mesh] { return Mesh->GetTimestamp(); };

	Adapter.HasNormals = [] { return true; };
	Adapter.GetPointNormal = [Mesh](int Idx) {return (FVector3f)Mesh->GetTriNormal(Idx); };

	return Adapter;
}




FPointSetAdapterd MeshAdapterUtil::MakeEdgeMidpointsAdapter(const FDynamicMesh3* Mesh)
{
	FPointSetAdapterd Adapter;
	Adapter.MaxPointID = [Mesh]() { return Mesh->MaxEdgeID(); };
	Adapter.PointCount = [Mesh]() { return Mesh->EdgeCount(); };
	Adapter.IsPoint = [Mesh] (int Idx) { return Mesh->IsEdge(Idx); };
	Adapter.GetPoint = [Mesh](int Idx) { return Mesh->GetEdgePoint(Idx, 0.5); };
	Adapter.Timestamp = [Mesh] { return Mesh->GetTimestamp(); };

	Adapter.HasNormals = [] { return false; };
	Adapter.GetPointNormal = [](int Idx) { return FVector3f::UnitY();};

	return Adapter;
}


FPointSetAdapterd MeshAdapterUtil::MakeBoundaryEdgeMidpointsAdapter(const FDynamicMesh3* Mesh)
{
	// may be possible to do this more quickly by directly iterating over Mesh.EdgesBuffer[eid*4+3]  (still need to check valid)
	int NumBoundaryEdges = 0;
	for (int eid : Mesh->BoundaryEdgeIndicesItr())
	{
		NumBoundaryEdges++;
	}

	FPointSetAdapterd Adapter;
	Adapter.MaxPointID = [Mesh]() { return Mesh->MaxEdgeID(); };
	Adapter.PointCount = [NumBoundaryEdges]() { return NumBoundaryEdges; };
	Adapter.IsPoint = [Mesh](int Idx) { return Mesh->IsEdge(Idx) && Mesh->IsBoundaryEdge(Idx); };
	Adapter.GetPoint = [Mesh](int Idx) { return Mesh->GetEdgePoint(Idx, 0.5); };
	Adapter.Timestamp = [Mesh] { return Mesh->GetTimestamp(); };

	Adapter.HasNormals = [] { return false; };
	Adapter.GetPointNormal = [](int Idx) { return FVector3f::UnitY(); };

	return Adapter;
}