// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"




int FDynamicMesh3::AppendVertex(const FVertexInfo& VtxInfo)
{
	int vid = VertexRefCounts.Allocate();
	int i = 3 * vid;
	Vertices.InsertAt(VtxInfo.Position[2], i + 2);
	Vertices.InsertAt(VtxInfo.Position[1], i + 1);
	Vertices.InsertAt(VtxInfo.Position[0], i);

	if (HasVertexNormals()) 
	{
		FVector3f n = (VtxInfo.bHaveN) ? VtxInfo.Normal : FVector3f::UnitY();
		VertexNormals->InsertAt(n[2], i + 2);
		VertexNormals->InsertAt(n[1], i + 1);
		VertexNormals->InsertAt(n[0], i);
	}

	if (HasVertexColors()) 
	{
		FVector3f c = (VtxInfo.bHaveC) ? VtxInfo.Color : FVector3f::One();
		VertexColors->InsertAt(c[2], i + 2);
		VertexColors->InsertAt(c[1], i + 1);
		VertexColors->InsertAt(c[0], i);
	}

	if (HasVertexUVs()) 
	{
		FVector2f u = (VtxInfo.bHaveUV) ? VtxInfo.UV : FVector2f::Zero();
		int j = 2 * vid;
		VertexUVs->InsertAt(u[1], j + 1);
		VertexUVs->InsertAt(u[0], j);
	}

	AllocateEdgesList(vid);

	UpdateTimeStamp(true, true);
	return vid;
}



int FDynamicMesh3::AppendVertex(const FDynamicMesh3& from, int fromVID)
{
	int bi = 3 * fromVID;

	int vid = VertexRefCounts.Allocate();
	int i = 3 * vid;
	Vertices.InsertAt(from.Vertices[bi + 2], i + 2);
	Vertices.InsertAt(from.Vertices[bi + 1], i + 1);
	Vertices.InsertAt(from.Vertices[bi], i);
	if (HasVertexNormals()) 
	{
		if (from.HasVertexNormals()) 
		{
			VertexNormals->InsertAt((*from.VertexNormals)[bi + 2], i + 2);
			VertexNormals->InsertAt((*from.VertexNormals)[bi + 1], i + 1);
			VertexNormals->InsertAt((*from.VertexNormals)[bi], i);
		}
		else 
		{
			VertexNormals->InsertAt(0, i + 2);
			VertexNormals->InsertAt(1, i + 1);       // y-up
			VertexNormals->InsertAt(0, i);
		}
	}

	if (HasVertexColors()) 
	{
		if (from.HasVertexColors()) 
		{
			VertexColors->InsertAt((*from.VertexColors)[bi + 2], i + 2);
			VertexColors->InsertAt((*from.VertexColors)[bi + 1], i + 1);
			VertexColors->InsertAt((*from.VertexColors)[bi], i);
		}
		else 
		{
			VertexColors->InsertAt(1, i + 2);
			VertexColors->InsertAt(1, i + 1);       // white
			VertexColors->InsertAt(1, i);
		}
	}

	if (HasVertexUVs()) 
	{
		int j = 2 * vid;
		if (from.HasVertexUVs()) 
		{
			int bj = 2 * fromVID;
			VertexUVs->InsertAt((*from.VertexUVs)[bj + 1], j + 1);
			VertexUVs->InsertAt((*from.VertexUVs)[bj], j);
		}
		else 
		{
			VertexUVs->InsertAt(0, j + 1);
			VertexUVs->InsertAt(0, j);
		}
	}

	AllocateEdgesList(vid);

	UpdateTimeStamp(true, true);
	return vid;
}



EMeshResult FDynamicMesh3::InsertVertex(int vid, const FVertexInfo& info, bool bUnsafe)
{
	if (VertexRefCounts.IsValid(vid))
	{
		return EMeshResult::Failed_VertexAlreadyExists;
	}

	bool bOK = (bUnsafe) ? VertexRefCounts.AllocateAtUnsafe(vid) :
		VertexRefCounts.AllocateAt(vid);
	if (bOK == false)
	{
		return EMeshResult::Failed_CannotAllocateVertex;
	}

	int i = 3 * vid;
	Vertices.InsertAt(info.Position[2], i + 2);
	Vertices.InsertAt(info.Position[1], i + 1);
	Vertices.InsertAt(info.Position[0], i);

	if (HasVertexNormals()) 
	{
		FVector3f n = (info.bHaveN) ? info.Normal : FVector3f::UnitY();
		VertexNormals->InsertAt(n[2], i + 2);
		VertexNormals->InsertAt(n[1], i + 1);
		VertexNormals->InsertAt(n[0], i);
	}

	if (HasVertexColors()) 
	{
		FVector3f c = (info.bHaveC) ? info.Color : FVector3f::One();
		VertexColors->InsertAt(c[2], i + 2);
		VertexColors->InsertAt(c[1], i + 1);
		VertexColors->InsertAt(c[0], i);
	}

	if (HasVertexUVs()) 
	{
		FVector2f u = (info.bHaveUV) ? info.UV : FVector2f::Zero();
		int j = 2 * vid;
		VertexUVs->InsertAt(u[1], j + 1);
		VertexUVs->InsertAt(u[0], j);
	}

	AllocateEdgesList(vid);

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}




int FDynamicMesh3::AppendTriangle(const FIndex3i& tv, int gid) 
{
	if (IsVertex(tv[0]) == false || IsVertex(tv[1]) == false || IsVertex(tv[2]) == false) 
	{
		check(false);
		return InvalidID;
	}
	if (tv[0] == tv[1] || tv[0] == tv[2] || tv[1] == tv[2])
	{
		check(false);
		return InvalidID;
	}

	// look up edges. if any already have two triangles, this would 
	// create non-manifold geometry and so we do not allow it
	int e0 = FindEdge(tv[0], tv[1]);
	int e1 = FindEdge(tv[1], tv[2]);
	int e2 = FindEdge(tv[2], tv[0]);
	if ((e0 != InvalidID && IsBoundaryEdge(e0) == false)
		|| (e1 != InvalidID && IsBoundaryEdge(e1) == false)
		|| (e2 != InvalidID && IsBoundaryEdge(e2) == false)) 
	{
		return NonManifoldID;
	}

	bool bHasGroups = HasTriangleGroups();  // have to check before changing .triangles

	// now safe to insert triangle
	int tid = TriangleRefCounts.Allocate();
	int i = 3 * tid;
	Triangles.InsertAt(tv[2], i + 2);
	Triangles.InsertAt(tv[1], i + 1);
	Triangles.InsertAt(tv[0], i);
	if (bHasGroups) 
	{
		TriangleGroups->InsertAt(gid, tid);
		GroupIDCounter = FMath::Max(GroupIDCounter, gid + 1);
	}

	// increment ref counts and update/create edges
	VertexRefCounts.Increment(tv[0]);
	VertexRefCounts.Increment(tv[1]);
	VertexRefCounts.Increment(tv[2]);

	AddTriangleEdge(tid, tv[0], tv[1], 0, e0);
	AddTriangleEdge(tid, tv[1], tv[2], 1, e1);
	AddTriangleEdge(tid, tv[2], tv[0], 2, e2);

	if (HasAttributes())
	{
		Attributes()->OnNewTriangle(tid, false);
	}

	UpdateTimeStamp(true, true);
	return tid;
}




EMeshResult FDynamicMesh3::InsertTriangle(int tid, const FIndex3i& tv, int gid, bool bUnsafe)
{
	if (TriangleRefCounts.IsValid(tid))
	{
		return EMeshResult::Failed_TriangleAlreadyExists;
	}

	if (IsVertex(tv[0]) == false || IsVertex(tv[1]) == false || IsVertex(tv[2]) == false) 
	{
		check(false);
		return EMeshResult::Failed_NotAVertex;
	}
	if (tv[0] == tv[1] || tv[0] == tv[2] || tv[1] == tv[2]) 
	{
		check(false);
		return EMeshResult::Failed_InvalidNeighbourhood;
	}

	// look up edges. if any already have two triangles, this would 
	// create non-manifold geometry and so we do not allow it
	int e0 = FindEdge(tv[0], tv[1]);
	int e1 = FindEdge(tv[1], tv[2]);
	int e2 = FindEdge(tv[2], tv[0]);
	if ((e0 != InvalidID && IsBoundaryEdge(e0) == false)
		|| (e1 != InvalidID && IsBoundaryEdge(e1) == false)
		|| (e2 != InvalidID && IsBoundaryEdge(e2) == false)) 
	{
		return EMeshResult::Failed_WouldCreateNonmanifoldEdge;
	}

	bool bOK = (bUnsafe) ? TriangleRefCounts.AllocateAtUnsafe(tid) :
		TriangleRefCounts.AllocateAt(tid);
	if (bOK == false)
	{
		return EMeshResult::Failed_CannotAllocateTriangle;
	}

	// now safe to insert triangle
	int i = 3 * tid;
	Triangles.InsertAt(tv[2], i + 2);
	Triangles.InsertAt(tv[1], i + 1);
	Triangles.InsertAt(tv[0], i);
	if (HasTriangleGroups()) 
	{
		TriangleGroups->InsertAt(gid, tid);
		GroupIDCounter = FMath::Max(GroupIDCounter, gid + 1);
	}

	// increment ref counts and update/create edges
	VertexRefCounts.Increment(tv[0]);
	VertexRefCounts.Increment(tv[1]);
	VertexRefCounts.Increment(tv[2]);

	AddTriangleEdge(tid, tv[0], tv[1], 0, e0);
	AddTriangleEdge(tid, tv[1], tv[2], 1, e1);
	AddTriangleEdge(tid, tv[2], tv[0], 2, e2);

	if (HasAttributes())
	{
		Attributes()->OnNewTriangle(tid, true);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}










void FDynamicMesh3::CompactInPlace(FCompactMaps* CompactInfo)
{
	// @todo support this
	check(HasAttributes() == false);

	//IndexMap mapV = (bComputeCompactInfo) ? IndexMap(MaxVertexID, VertexCount) : nullptr;

	// find first free vertex, and last used vertex
	int iLastV = MaxVertexID() - 1, iCurV = 0;
	while (iLastV >= 0 && VertexRefCounts.IsValidUnsafe(iLastV) == false)
	{
		iLastV--;
	}
	while (iCurV < iLastV && VertexRefCounts.IsValidUnsafe(iCurV))
	{
		iCurV++;
	}

	TDynamicVector<short> &vref = VertexRefCounts.GetRawRefCountsUnsafe();

	while (iCurV < iLastV)
	{
		int kc = iCurV * 3, kl = iLastV * 3;
		Vertices[kc] = Vertices[kl];  Vertices[kc + 1] = Vertices[kl + 1];  Vertices[kc + 2] = Vertices[kl + 2];
		if (HasVertexNormals())
		{
			(*VertexNormals)[kc] = (*VertexNormals)[kl];  
			(*VertexNormals)[kc + 1] = (*VertexNormals)[kl + 1];  
			(*VertexNormals)[kc + 2] = (*VertexNormals)[kl + 2];
		}
		if (HasVertexColors())
		{
			(*VertexColors)[kc] = (*VertexColors)[kl];  
			(*VertexColors)[kc + 1] = (*VertexColors)[kl + 1];  
			(*VertexColors)[kc + 2] = (*VertexColors)[kl + 2];
		}
		if (HasVertexUVs())
		{
			int ukc = iCurV * 2, ukl = iLastV * 2;
			(*VertexUVs)[ukc] = (*VertexUVs)[ukl]; 
			(*VertexUVs)[ukc + 1] = (*VertexUVs)[ukl + 1];
		}

		for (int eid : VertexEdgeLists.Values(iLastV))
		{
			// replace vertex in edges
			ReplaceEdgeVertex(eid, iLastV, iCurV);

			// replace vertex in triangles
			int t0 = Edges[4 * eid + 2];
			ReplaceTriangleVertex(t0, iLastV, iCurV);
			int t1 = Edges[4 * eid + 3];
			if (t1 != InvalidID)
			{
				ReplaceTriangleVertex(t1, iLastV, iCurV);
			}
		}

		// shift vertex refcount to position
		vref[iCurV] = vref[iLastV];
		vref[iLastV] = FRefCountVector::INVALID_REF_COUNT;

		// move edge list
		VertexEdgeLists.Move(iLastV, iCurV);

		if (CompactInfo != nullptr)
		{
			CompactInfo->MapV[iLastV] = iCurV;
		}

		// move cur forward one, last back one, and  then search for next valid
		iLastV--; iCurV++;
		while (iLastV >= 0 && VertexRefCounts.IsValidUnsafe(iLastV) == false)
		{
			iLastV--;
		}
		while (iCurV < iLastV && VertexRefCounts.IsValidUnsafe(iCurV))
		{
			iCurV++;
		}
	}

	// trim vertices data structures
	VertexRefCounts.Trim(VertexCount());
	Vertices.Resize(VertexCount() * 3);
	if (HasVertexNormals())
	{
		VertexNormals->Resize(VertexCount() * 3);
	}
	if (HasVertexColors())
	{
		VertexColors->Resize(VertexCount() * 3);
	}
	if (HasVertexUVs())
	{
		VertexUVs->Resize(VertexCount() * 2);
	}

	// [TODO] VertexEdgeLists!!!

	/** shift triangles **/

	// find first free triangle, and last valid triangle
	int iLastT = MaxTriangleID() - 1, iCurT = 0;
	while (iLastT >= 0 && TriangleRefCounts.IsValidUnsafe(iLastT) == false)
	{
		iLastT--;
	}
	while (iCurT < iLastT && TriangleRefCounts.IsValidUnsafe(iCurT))
	{
		iCurT++;
	}

	TDynamicVector<short> &tref = TriangleRefCounts.GetRawRefCountsUnsafe();

	while (iCurT < iLastT)
	{
		int kc = iCurT * 3, kl = iLastT * 3;

		// shift triangle
		for (int j = 0; j < 3; ++j)
		{
			Triangles[kc + j] = Triangles[kl + j];
			TriangleEdges[kc + j] = TriangleEdges[kl + j];
		}
		if (HasTriangleGroups())
		{
			(*TriangleGroups)[iCurT] = (*TriangleGroups)[iLastT];
		}

		// update edges
		for (int j = 0; j < 3; ++j)
		{
			int eid = TriangleEdges[kc + j];
			ReplaceEdgeTriangle(eid, iLastT, iCurT);
		}

		// shift triangle refcount to position
		tref[iCurT] = tref[iLastT];
		tref[iLastT] = FRefCountVector::INVALID_REF_COUNT;

		// move cur forward one, last back one, and  then search for next valid
		iLastT--; iCurT++;
		while (iLastT >= 0 && TriangleRefCounts.IsValidUnsafe(iLastT) == false)
		{
			iLastT--;
		}
		while (iCurT < iLastT && TriangleRefCounts.IsValidUnsafe(iCurT))
		{
			iCurT++;
		}
	}

	// trim triangles data structures
	TriangleRefCounts.Trim(TriangleCount());
	Triangles.Resize(TriangleCount() * 3);
	TriangleEdges.Resize(TriangleCount() * 3);
	if (HasTriangleGroups())
	{
		TriangleGroups->Resize(TriangleCount());
	}

	/** shift edges **/

	// find first free edge, and last used edge
	int iLastE = MaxEdgeID() - 1, iCurE = 0;
	while (iLastE >= 0 && EdgeRefCounts.IsValidUnsafe(iLastE) == false)
	{
		iLastE--;
	}
	while (iCurE < iLastE && EdgeRefCounts.IsValidUnsafe(iCurE))
	{
		iCurE++;
	}

	TDynamicVector<short> &eref = EdgeRefCounts.GetRawRefCountsUnsafe();

	while (iCurE < iLastE) 
	{
		int kc = iCurE * 4, kl = iLastE * 4;

		// shift edge
		for (int j = 0; j < 4; ++j) 
		{
			Edges[kc + j] = Edges[kl + j];
		}

		// replace edge in vertex edges lists
		int v0 = Edges[kc], v1 = Edges[kc + 1];
		VertexEdgeLists.Replace(v0, [iLastE](int eid) { return eid == iLastE; }, iCurE);
		VertexEdgeLists.Replace(v1, [iLastE](int eid) { return eid == iLastE; }, iCurE);

		// replace edge in triangles
		ReplaceTriangleEdge(Edges[kc + 2], iLastE, iCurE);
		if (Edges[kc + 3] != InvalidID)
		{
			ReplaceTriangleEdge(Edges[kc + 3], iLastE, iCurE);
		}

		// shift triangle refcount to position
		eref[iCurE] = eref[iLastE];
		eref[iLastE] = FRefCountVector::INVALID_REF_COUNT;

		// move cur forward one, last back one, and  then search for next valid
		iLastE--; iCurE++;
		while (iLastE >= 0 && EdgeRefCounts.IsValidUnsafe(iLastE) == false)
		{
			iLastE--;
		}
		while (iCurE < iLastE && EdgeRefCounts.IsValidUnsafe(iCurE))
		{
			iCurE++;
		}
	}

	// trim edge data structures
	EdgeRefCounts.Trim(EdgeCount());
	Edges.Resize(EdgeCount() * 4);
}







EMeshResult FDynamicMesh3::ReverseTriOrientation(int tID) 
{
	if (!IsTriangle(tID))
	{
		return EMeshResult::Failed_NotATriangle;
	}
	ReverseTriOrientationInternal(tID);
	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}

void FDynamicMesh3::ReverseTriOrientationInternal(int tID)
{
	FIndex3i t = GetTriangle(tID);
	SetTriangleInternal(tID, t[1], t[0], t[2]);
	FIndex3i te = GetTriEdges(tID);
	SetTriangleEdgesInternal(tID, te[0], te[2], te[1]);
	if (HasAttributes())
	{
		Attributes()->OnReverseTriOrientation(tID);
	}
}

void FDynamicMesh3::ReverseOrientation(bool bFlipNormals)
{
	for (int tid : TriangleIndicesItr()) 
	{
		ReverseTriOrientationInternal(tid);
	}
	if (bFlipNormals && HasVertexNormals()) 
	{
		for (int vid : VertexIndicesItr()) 
		{
			int i = 3 * vid;
			(*VertexNormals)[i] = -(*VertexNormals)[i];
			(*VertexNormals)[i + 1] = -(*VertexNormals)[i + 1];
			(*VertexNormals)[i + 2] = -(*VertexNormals)[i + 2];
		}
	}
	UpdateTimeStamp(true, true);
}






EMeshResult FDynamicMesh3::RemoveVertex(int vID, bool bRemoveAllTriangles, bool bPreserveManifold)
{
	if (VertexRefCounts.IsValid(vID) == false)
	{
		return EMeshResult::Failed_NotAVertex;
	}

	if (bRemoveAllTriangles) 
	{
		// if any one-ring vtx is a boundary vtx and one of its outer-ring edges is an
		// interior edge then we will create a bowtie if we remove that triangle
		if (bPreserveManifold) 
		{
			for (int tid : VtxTrianglesItr(vID)) 
			{
				FIndex3i tri = GetTriangle(tid);
				int j = IndexUtil::FindTriIndex(vID, tri);
				int oa = tri[(j + 1) % 3], ob = tri[(j + 2) % 3];
				int eid = FindEdge(oa, ob);
				if (IsBoundaryEdge(eid))
				{
					continue;
				}
				if (IsBoundaryVertex(oa) || IsBoundaryVertex(ob))
				{
					return EMeshResult::Failed_WouldCreateBowtie;
				}
			}
		}

		TArray<int> tris;
		GetVtxTriangles(vID, tris, true);
		for (int tID : tris) 
		{
			EMeshResult result = RemoveTriangle(tID, false, bPreserveManifold);
			if (result != EMeshResult::Ok)
			{
				return result;
			}
		}
	}

	if (VertexRefCounts.GetRefCount(vID) != 1)
	{
		return EMeshResult::Failed_VertexStillReferenced;
	}

	VertexRefCounts.Decrement(vID);
	check(VertexRefCounts.IsValid(vID) == false);
	VertexEdgeLists.Clear(vID);

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}








EMeshResult FDynamicMesh3::RemoveTriangle(int tID, bool bRemoveIsolatedVertices, bool bPreserveManifold)
{
	if (!TriangleRefCounts.IsValid(tID)) 
	{
		check(false);
		return EMeshResult::Failed_NotATriangle;
	}

	FIndex3i tv = GetTriangle(tID);
	FIndex3i te = GetTriEdges(tID);

	// if any tri vtx is a boundary vtx connected to two interior edges, then
	// we cannot remove this triangle because it would create a bowtie vertex!
	// (that vtx already has 2 boundary edges, and we would add two more)
	if (bPreserveManifold) 
	{
		for (int j = 0; j < 3; ++j) 
		{
			if (IsBoundaryVertex(tv[j])) 
			{
				if (IsBoundaryEdge(te[j]) == false && IsBoundaryEdge(te[(j + 2) % 3]) == false)
				{
					return EMeshResult::Failed_WouldCreateBowtie;
				}
			}
		}
	}

	// Remove triangle from its edges. if edge has no triangles left,
	// then it is removed.
	for (int j = 0; j < 3; ++j) 
	{
		int eid = te[j];
		ReplaceEdgeTriangle(eid, tID, InvalidID);
		if (Edges[4 * eid + 2] == InvalidID) 
		{
			int a = Edges[4 * eid];
			VertexEdgeLists.Remove(a, eid);

			int b = Edges[4 * eid + 1];
			VertexEdgeLists.Remove(b, eid);

			EdgeRefCounts.Decrement(eid);
		}
	}

	// free this triangle
	TriangleRefCounts.Decrement(tID);
	check(TriangleRefCounts.IsValid(tID) == false);

	// Decrement vertex refcounts. If any hit 1 and we got remove-isolated flag,
	// we need to remove that vertex
	for (int j = 0; j < 3; ++j) 
	{
		int vid = tv[j];
		VertexRefCounts.Decrement(vid);
		if (bRemoveIsolatedVertices && VertexRefCounts.GetRefCount(vid) == 1) 
		{
			VertexRefCounts.Decrement(vid);
			check(VertexRefCounts.IsValid(vid) == false);
			VertexEdgeLists.Clear(vid);
		}
	}

	if (HasAttributes())
	{
		Attributes()->OnRemoveTriangle(tID, bRemoveIsolatedVertices);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}







EMeshResult FDynamicMesh3::SetTriangle(int tID, const FIndex3i& newv, bool bRemoveIsolatedVertices)
{
	// @todo support this. 
	check(HasAttributes() == false);

	FIndex3i tv = GetTriangle(tID);
	FIndex3i te = GetTriEdges(tID);
	if (tv[0] == newv[0] && tv[1] == newv[1])
	{
		te[0] = -1;
	}
	if (tv[1] == newv[1] && tv[2] == newv[2])
	{
		te[1] = -1;
	}
	if (tv[2] == newv[2] && tv[0] == newv[0])
	{
		te[2] = -1;
	}

	if (!TriangleRefCounts.IsValid(tID)) 
	{
		check(false);
		return EMeshResult::Failed_NotATriangle;
	}
	if (IsVertex(newv[0]) == false || IsVertex(newv[1]) == false || IsVertex(newv[2]) == false) 
	{
		check(false);
		return EMeshResult::Failed_NotAVertex;
	}
	if (newv[0] == newv[1] || newv[0] == newv[2] || newv[1] == newv[2]) 
	{
		check(false);
		return EMeshResult::Failed_BrokenTopology;
	}
	// look up edges. if any already have two triangles, this would 
	// create non-manifold geometry and so we do not allow it
	int e0 = FindEdge(newv[0], newv[1]);
	int e1 = FindEdge(newv[1], newv[2]);
	int e2 = FindEdge(newv[2], newv[0]);
	if ((te[0] != -1 && e0 != InvalidID && IsBoundaryEdge(e0) == false)
		|| (te[1] != -1 && e1 != InvalidID && IsBoundaryEdge(e1) == false)
		|| (te[2] != -1 && e2 != InvalidID && IsBoundaryEdge(e2) == false)) 
	{
		return EMeshResult::Failed_BrokenTopology;
	}


	// [TODO] check that we are not going to create invalid stuff...

	// Remove triangle from its edges. if edge has no triangles left, then it is removed.
	for (int j = 0; j < 3; ++j) 
	{
		int eid = te[j];
		if (eid == -1)      // we don't need to modify this edge
		{
			continue;
		}
		ReplaceEdgeTriangle(eid, tID, InvalidID);
		if (Edges[4 * eid + 2] == InvalidID) 
		{
			int a = Edges[4 * eid];
			VertexEdgeLists.Remove(a, eid);

			int b = Edges[4 * eid + 1];
			VertexEdgeLists.Remove(b, eid);

			EdgeRefCounts.Decrement(eid);
		}
	}

	// Decrement vertex refcounts. If any hit 1 and we got remove-isolated flag,
	// we need to remove that vertex
	for (int j = 0; j < 3; ++j) 
	{
		int vid = tv[j];
		if (vid == newv[j])     // we don't need to modify this vertex
		{
			continue;
		}
		VertexRefCounts.Decrement(vid);
		if (bRemoveIsolatedVertices && VertexRefCounts.GetRefCount(vid) == 1) 
		{
			VertexRefCounts.Decrement(vid);
			check(VertexRefCounts.IsValid(vid) == false);
			VertexEdgeLists.Clear(vid);
		}
	}


	// ok now re-insert with vertices
	int i = 3 * tID;
	for (int j = 0; j < 3; ++j) 
	{
		if (newv[j] != tv[j]) 
		{
			Triangles[i + j] = newv[j];
			VertexRefCounts.Increment(newv[j]);
		}
	}

	if (te[0] != -1)
	{
		AddTriangleEdge(tID, newv[0], newv[1], 0, e0);
	}
	if (te[1] != -1)
	{
		AddTriangleEdge(tID, newv[1], newv[2], 1, e1);
	}
	if (te[2] != -1)
	{
		AddTriangleEdge(tID, newv[2], newv[0], 2, e2);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}









EMeshResult FDynamicMesh3::SplitEdge(int eab, FEdgeSplitInfo& SplitInfo, double split_t)
{
	SplitInfo = FEdgeSplitInfo();

	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	// look up primary edge & triangle
	int eab_i = 4 * eab;
	int a = Edges[eab_i], b = Edges[eab_i + 1];
	int t0 = Edges[eab_i + 2];
	if (t0 == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	FIndex3i T0tv = GetTriangle(t0);
	int c = IndexUtil::OrientTriEdgeAndFindOtherVtx(a, b, T0tv);
	if (VertexRefCounts.GetRawRefCount(c) > 32764)
	{
		return EMeshResult::Failed_HitValenceLimit;
	}
	if (a != Edges[eab_i])
	{
		split_t = 1.0 - split_t;    // if we flipped a/b order we need to reverse t
	}

	SplitInfo.OriginalEdge = eab;
	SplitInfo.OriginalVertices = FIndex2i(a, b);   // this is the oriented a,b
	SplitInfo.OriginalTriangles = FIndex2i(t0, InvalidID);
	SplitInfo.SplitT = split_t;

	// quite a bit of code is duplicated between boundary and non-boundary case, but it
	//  is too hard to follow later if we factor it out...
	if (IsBoundaryEdge(eab)) 
	{
		// create vertex
		FVector3d vNew = FVector3d::Lerp(GetVertex(a), GetVertex(b), split_t);
		int f = AppendVertex(vNew);
		if (HasVertexNormals())
		{
			SetVertexNormal(f, FVector3f::Lerp(GetVertexNormal(a), GetVertexNormal(b), (float)split_t).Normalized());
		}
		if (HasVertexColors())
		{
			SetVertexColor(f, FVector3f::Lerp(GetVertexColor(a), GetVertexColor(b), (float)split_t));
		}
		if (HasVertexUVs())
		{
			SetVertexUV(f, FVector2f::Lerp(GetVertexUV(a), GetVertexUV(b), (float)split_t));
		}

		// look up edge bc, which needs to be modified
		FIndex3i T0te = GetTriEdges(t0);
		int ebc = T0te[IndexUtil::FindEdgeIndexInTri(b, c, T0tv)];

		// rewrite existing triangle
		ReplaceTriangleVertex(t0, b, f);

		// add second triangle
		int t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
		if (HasTriangleGroups())
		{
			TriangleGroups->InsertAt((*TriangleGroups)[t0], t2);
		}

		// rewrite edge bc, create edge af
		ReplaceEdgeTriangle(ebc, t0, t2);
		int eaf = eab;
		ReplaceEdgeVertex(eaf, b, f);
		VertexEdgeLists.Remove(b, eab);
		VertexEdgeLists.Insert(f, eaf);

		// create edges fb and fc 
		int efb = AddEdgeInternal(f, b, t2);
		int efc = AddEdgeInternal(f, c, t0, t2);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		SetTriangleEdgesInternal(t2, efb, ebc, efc);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(f, 2);

		SplitInfo.bIsBoundary = true;
		SplitInfo.OtherVertices = FIndex2i(c, InvalidID);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, InvalidID);
		SplitInfo.NewTriangles = FIndex2i(t2, InvalidID);

		if (HasAttributes())
		{
			Attributes()->OnSplitEdge(SplitInfo);
		}

		UpdateTimeStamp(true, true);
		return EMeshResult::Ok;

	}
	else 		// interior triangle branch
	{
		// look up other triangle
		int t1 = Edges[eab_i + 3];
		SplitInfo.OriginalTriangles.B = t1;
		FIndex3i T1tv = GetTriangle(t1);
		int d = IndexUtil::FindTriOtherVtx(a, b, T1tv);
		if (VertexRefCounts.GetRawRefCount(d) > 32764)
		{
			return EMeshResult::Failed_HitValenceLimit;
		}

		// create vertex
		FVector3d vNew = FVector3d::Lerp(GetVertex(a), GetVertex(b), split_t);
		int f = AppendVertex(vNew);
		if (HasVertexNormals())
		{
			SetVertexNormal(f, FVector3f::Lerp(GetVertexNormal(a), GetVertexNormal(b), (float)split_t).Normalized());
		}
		if (HasVertexColors())
		{
			SetVertexColor(f, FVector3f::Lerp(GetVertexColor(a), GetVertexColor(b), (float)split_t));
		}
		if (HasVertexUVs())
		{
			SetVertexUV(f, FVector2f::Lerp(GetVertexUV(a), GetVertexUV(b), (float)split_t));
		}

		// look up edges that we are going to need to update
		// [TODO OPT] could use ordering to reduce # of compares here
		FIndex3i T0te = GetTriEdges(t0);
		int ebc = T0te[IndexUtil::FindEdgeIndexInTri(b, c, T0tv)];
		FIndex3i T1te = GetTriEdges(t1);
		int edb = T1te[IndexUtil::FindEdgeIndexInTri(d, b, T1tv)];

		// rewrite existing triangles
		ReplaceTriangleVertex(t0, b, f);
		ReplaceTriangleVertex(t1, b, f);

		// add two triangles to close holes we just created
		int t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
		int t3 = AddTriangleInternal(f, d, b, InvalidID, InvalidID, InvalidID);
		if (HasTriangleGroups()) 
		{
			TriangleGroups->InsertAt((*TriangleGroups)[t0], t2);
			TriangleGroups->InsertAt((*TriangleGroups)[t1], t3);
		}

		// update the edges we found above, to point to triangles
		ReplaceEdgeTriangle(ebc, t0, t2);
		ReplaceEdgeTriangle(edb, t1, t3);

		// edge eab became eaf
		int eaf = eab; //Edge * eAF = eAB;
		ReplaceEdgeVertex(eaf, b, f);

		// update a/b/f vertex-edges
		VertexEdgeLists.Remove(b, eab);
		VertexEdgeLists.Insert(f, eaf);

		// create edges connected to f  (also updates vertex-edges)
		int efb = AddEdgeInternal(f, b, t2, t3);
		int efc = AddEdgeInternal(f, c, t0, t2);
		int edf = AddEdgeInternal(d, f, t1, t3);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		ReplaceTriangleEdge(t1, edb, edf);
		SetTriangleEdgesInternal(t2, efb, ebc, efc);
		SetTriangleEdgesInternal(t3, edf, edb, efb);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(d);
		VertexRefCounts.Increment(f, 4);

		SplitInfo.bIsBoundary = false;
		SplitInfo.OtherVertices = FIndex2i(c, d);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, edf);
		SplitInfo.NewTriangles = FIndex2i(t2, t3);

		if (HasAttributes())
		{
			Attributes()->OnSplitEdge(SplitInfo);
		}

		UpdateTimeStamp(true, true);
		return EMeshResult::Ok;
	}

}


EMeshResult FDynamicMesh3::SplitEdge(int vA, int vB, FEdgeSplitInfo& SplitInfo)
{
	int eid = FindEdge(vA, vB);
	if (eid == InvalidID) 
	{
		SplitInfo = FEdgeSplitInfo();
		return EMeshResult::Failed_NotAnEdge;
	}
	return SplitEdge(eid, SplitInfo);
}








EMeshResult FDynamicMesh3::FlipEdge(int eab, FEdgeFlipInfo& FlipInfo)
{
	FlipInfo = FEdgeFlipInfo();

	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}
	if (IsBoundaryEdge(eab))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// find oriented edge [a,b], tris t0,t1, and other verts c in t0, d in t1
	int eab_i = 4 * eab;
	int a = Edges[eab_i], b = Edges[eab_i + 1];
	int t0 = Edges[eab_i + 2], t1 = Edges[eab_i + 3];
	FIndex3i T0tv = GetTriangle(t0), T1tv = GetTriangle(t1);
	int c = IndexUtil::OrientTriEdgeAndFindOtherVtx(a, b, T0tv);
	int d = IndexUtil::FindTriOtherVtx(a, b, T1tv);
	if (c == InvalidID || d == InvalidID) 
	{
		return EMeshResult::Failed_BrokenTopology;
	}

	int flipped = FindEdge(c, d);
	if (flipped != InvalidID)
	{
		return EMeshResult::Failed_FlippedEdgeExists;
	}

	// find edges bc, ca, ad, db
	int ebc = FindTriangleEdge(t0, b, c);
	int eca = FindTriangleEdge(t0, c, a);
	int ead = FindTriangleEdge(t1, a, d);
	int edb = FindTriangleEdge(t1, d, b);

	// update triangles
	SetTriangleInternal(t0, c, d, b);
	SetTriangleInternal(t1, d, c, a);

	// update edge AB, which becomes flipped edge CD
	SetEdgeVerticesInternal(eab, c, d);
	SetEdgeTrianglesInternal(eab, t0, t1);
	int ecd = eab;

	// update the two other edges whose triangle nbrs have changed
	if (ReplaceEdgeTriangle(eca, t0, t1) == -1)
	{
		checkf(false, TEXT("FDynamicMesh3.FlipEdge: first ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	if (ReplaceEdgeTriangle(edb, t1, t0) == -1)
	{
		checkf(false, TEXT("FDynamicMesh3.FlipEdge: second ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// update triangle nbr lists (these are edges)
	SetTriangleEdgesInternal(t0, ecd, edb, ebc);
	SetTriangleEdgesInternal(t1, ecd, eca, ead);

	// remove old eab from verts a and b, and Decrement ref counts
	if (VertexEdgeLists.Remove(a, eab) == false)
	{
		checkf(false, TEXT("FDynamicMesh3.FlipEdge: first edge list remove failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	if (VertexEdgeLists.Remove(b, eab) == false)
	{
		checkf(false, TEXT("FDynamicMesh3.FlipEdge: second edge list remove failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	VertexRefCounts.Decrement(a);
	VertexRefCounts.Decrement(b);
	if (IsVertex(a) == false || IsVertex(b) == false)
	{
		checkf(false, TEXT("FDynamicMesh3.FlipEdge: either a or b is not a vertex?"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// add edge ecd to verts c and d, and increment ref counts
	VertexEdgeLists.Insert(c, ecd);
	VertexEdgeLists.Insert(d, ecd);
	VertexRefCounts.Increment(c);
	VertexRefCounts.Increment(d);

	// success! collect up results
	FlipInfo.EdgeID = eab;
	FlipInfo.OriginalVerts = FIndex2i(a, b);
	FlipInfo.OpposingVerts = FIndex2i(c, d);
	FlipInfo.Triangles = FIndex2i(t0, t1);

	if (HasAttributes())
	{
		Attributes()->OnFlipEdge(FlipInfo);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}


EMeshResult FDynamicMesh3::FlipEdge(int vA, int vB, FEdgeFlipInfo& FlipInfo)
{
	int eid = FindEdge(vA, vB);
	if (eid == InvalidID)
	{
		FlipInfo = FEdgeFlipInfo();
		return EMeshResult::Failed_NotAnEdge;
	}
	return FlipEdge(eid, FlipInfo);
}









EMeshResult FDynamicMesh3::CollapseEdge(int vKeep, int vRemove, double collapse_t, FEdgeCollapseInfo& CollapseInfo)
{
	CollapseInfo = FEdgeCollapseInfo();

	if (IsVertex(vKeep) == false || IsVertex(vRemove) == false)
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	int b = vKeep;		// renaming for sanity. We remove a and keep b
	int a = vRemove;

	int eab = FindEdge(a, b);
	if (eab == InvalidID)
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	int t0 = Edges[4 * eab + 2];
	if (t0 == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	FIndex3i T0tv = GetTriangle(t0);
	int c = IndexUtil::FindTriOtherVtx(a, b, T0tv);

	// look up opposing triangle/vtx if we are not in boundary case
	bool bIsBoundaryEdge = false;
	int d = InvalidID;
	int t1 = Edges[4 * eab + 3];
	if (t1 != InvalidID) 
	{
		FIndex3i T1tv = GetTriangle(t1);
		d = IndexUtil::FindTriOtherVtx(a, b, T1tv);
		if (c == d)
		{
			return EMeshResult::Failed_FoundDuplicateTriangle;
		}
	}
	else 
	{
		bIsBoundaryEdge = true;
	}

	CollapseInfo.OpposingVerts = FIndex2i(c, d);

	// We cannot collapse if edge lists of a and b share vertices other
	//  than c and d  (because then we will make a triangle [x b b].
	//  Unfortunately I cannot see a way to do this more efficiently than brute-force search
	//  [TODO] if we had tri iterator for a, couldn't we check each tri for b  (skipping t0 and t1) ?
	int edges_a_count = VertexEdgeLists.GetCount(a);
	int eac = InvalidID, ead = InvalidID, ebc = InvalidID, ebd = InvalidID;
	for (int eid_a : VertexEdgeLists.Values(a)) 
	{
		int vax = GetOtherEdgeVertex(eid_a, a);
		if (vax == c) 
		{
			eac = eid_a;
			continue;
		}
		if (vax == d) 
		{
			ead = eid_a;
			continue;
		}
		if (vax == b)
		{
			continue;
		}
		for (int eid_b : VertexEdgeLists.Values(b)) 
		{
			if (GetOtherEdgeVertex(eid_b, b) == vax)
			{
				return EMeshResult::Failed_InvalidNeighbourhood;
			}
		}
	}

	// [RMS] I am not sure this tetrahedron case will detect bowtie vertices.
	// But the single-triangle case does

	// We cannot collapse if we have a tetrahedron. In this case a has 3 nbr edges,
	//  and edge cd exists. But that is not conclusive, we also have to check that
	//  cd is an internal edge, and that each of its tris contain a or b
	if (edges_a_count == 3 && bIsBoundaryEdge == false) 
	{
		int edc = FindEdge(d, c);
		int edc_i = 4 * edc;
		if (edc != InvalidID && Edges[edc_i + 3] != InvalidID) 
		{
			int edc_t0 = Edges[edc_i + 2];
			int edc_t1 = Edges[edc_i + 3];

			if ((TriangleHasVertex(edc_t0, a) && TriangleHasVertex(edc_t1, b))
				|| (TriangleHasVertex(edc_t0, b) && TriangleHasVertex(edc_t1, a)))
			{
				return EMeshResult::Failed_CollapseTetrahedron;
			}
		}

	}
	else if (bIsBoundaryEdge == true && IsBoundaryEdge(eac)) 
	{
		// Cannot collapse edge if we are down to a single triangle
		ebc = FindEdgeFromTri(b, c, t0);
		if (IsBoundaryEdge(ebc))
		{
			return EMeshResult::Failed_CollapseTriangle;
		}
	}

	// [RMS] this was added from C++ version...seems like maybe I never hit
	//   this case? Conceivably could be porting bug but looking at the
	//   C++ code I cannot see how we could possibly have caught this case...
	//
	// cannot collapse an edge where both vertices are boundary vertices
	// because that would create a bowtie
	//
	// NOTE: potentially scanning all edges here...couldn't we
	//  pick up eac/bc/ad/bd as we go? somehow?
	if (bIsBoundaryEdge == false && IsBoundaryVertex(a) && IsBoundaryVertex(b))
	{
		return EMeshResult::Failed_InvalidNeighbourhood;
	}

	// save vertex positions before we delete removed (can defer kept?)
	FVector3d KeptPos = GetVertex(vKeep);
	FVector3d RemovedPos = GetVertex(vRemove);

	// 1) remove edge ab from vtx b
	// 2) find edges ad and ac, and tris tad, tac across those edges  (will use later)
	// 3) for other edges, replace a with b, and add that edge to b
	// 4) replace a with b in all triangles connected to a
	int tad = InvalidID, tac = InvalidID;
	for (int eid : VertexEdgeLists.Values(a)) 
	{
		int o = GetOtherEdgeVertex(eid, a);
		if (o == b) 
		{
			if (VertexEdgeLists.Remove(b, eid) != true)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at remove case o == b"));
				return EMeshResult::Failed_UnrecoverableError;
			}
		}
		else if (o == c) 
		{
			if (VertexEdgeLists.Remove(c, eid) != true)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at remove case o == c"));
				return EMeshResult::Failed_UnrecoverableError;
			}
			tac = GetOtherEdgeTriangle(eid, t0);
		}
		else if (o == d) 
		{
			if (VertexEdgeLists.Remove(d, eid) != true)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at remove case o == c, step 1"));
				return EMeshResult::Failed_UnrecoverableError;
			}
			tad = GetOtherEdgeTriangle(eid, t1);
		}
		else 
		{
			if (ReplaceEdgeVertex(eid, a, b) == -1)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at remove case else"));
				return EMeshResult::Failed_UnrecoverableError;
			}
			VertexEdgeLists.Insert(b, eid);
		}

		// [TODO] perhaps we can already have unique tri list because of the manifold-nbrhood check we need to do...
		for (int j = 0; j < 2; ++j) 
		{
			int t_j = Edges[4 * eid + 2 + j];
			if (t_j != InvalidID && t_j != t0 && t_j != t1) 
			{
				if (TriangleHasVertex(t_j, a))
				{
					if (ReplaceTriangleVertex(t_j, a, b) == -1)
					{
						checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at remove last check"));
						return EMeshResult::Failed_UnrecoverableError;
					}
					VertexRefCounts.Increment(b);
					VertexRefCounts.Decrement(a);
				}
			}
		}
	}

	if (bIsBoundaryEdge == false) 
	{
		// remove all edges from vtx a, then remove vtx a
		VertexEdgeLists.Clear(a);
		check(VertexRefCounts.GetRefCount(a) == 3);		// in t0,t1, and initial ref
		VertexRefCounts.Decrement(a, 3);
		check(VertexRefCounts.IsValid(a) == false);

		// remove triangles T0 and T1, and update b/c/d refcounts
		TriangleRefCounts.Decrement(t0);
		TriangleRefCounts.Decrement(t1);
		VertexRefCounts.Decrement(c);
		VertexRefCounts.Decrement(d);
		VertexRefCounts.Decrement(b, 2);
		check(TriangleRefCounts.IsValid(t0) == false);
		check(TriangleRefCounts.IsValid(t1) == false);

		// remove edges ead, eab, eac
		EdgeRefCounts.Decrement(ead);
		EdgeRefCounts.Decrement(eab);
		EdgeRefCounts.Decrement(eac);
		check(EdgeRefCounts.IsValid(ead) == false);
		check(EdgeRefCounts.IsValid(eab) == false);
		check(EdgeRefCounts.IsValid(eac) == false);

		// replace t0 and t1 in edges ebd and ebc that we kept
		ebd = FindEdgeFromTri(b, d, t1);
		if (ebc == InvalidID)   // we may have already looked this up
		{
			ebc = FindEdgeFromTri(b, c, t0);
		}

		if (ReplaceEdgeTriangle(ebd, t1, tad) == -1)
		{
			checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=false branch, ebd replace triangle"));
			return EMeshResult::Failed_UnrecoverableError;
		}

		if (ReplaceEdgeTriangle(ebc, t0, tac) == -1)
		{
			checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=false branch, ebc replace triangle"));
			return EMeshResult::Failed_UnrecoverableError;
		}

		// update tri-edge-nbrs in tad and tac
		if (tad != InvalidID) 
		{
			if (ReplaceTriangleEdge(tad, ead, ebd) == -1)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=false branch, ebd replace triangle"));
				return EMeshResult::Failed_UnrecoverableError;
			}
		}
		if (tac != InvalidID) 
		{
			if (ReplaceTriangleEdge(tac, eac, ebc) == -1)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=false branch, ebd replace triangle"));
				return EMeshResult::Failed_UnrecoverableError;
			}
		}

	}
	else 
	{
		//  boundary-edge path. this is basically same code as above, just not referencing t1/d

		// remove all edges from vtx a, then remove vtx a
		VertexEdgeLists.Clear(a);
		check(VertexRefCounts.GetRefCount(a) == 2);		// in t0 and initial ref
		VertexRefCounts.Decrement(a, 2);
		check(VertexRefCounts.IsValid(a) == false);

		// remove triangle T0 and update b/c refcounts
		TriangleRefCounts.Decrement(t0);
		VertexRefCounts.Decrement(c);
		VertexRefCounts.Decrement(b);
		check(TriangleRefCounts.IsValid(t0) == false);

		// remove edges eab and eac
		EdgeRefCounts.Decrement(eab);
		EdgeRefCounts.Decrement(eac);
		check(EdgeRefCounts.IsValid(eab) == false);
		check(EdgeRefCounts.IsValid(eac) == false);

		// replace t0 in edge ebc that we kept
		ebc = FindEdgeFromTri(b, c, t0);
		if (ReplaceEdgeTriangle(ebc, t0, tac) == -1)
		{
			checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=false branch, ebc replace triangle"));
			return EMeshResult::Failed_UnrecoverableError;
		}

		// update tri-edge-nbrs in tac
		if (tac != InvalidID) 
		{
			if (ReplaceTriangleEdge(tac, eac, ebc) == -1)
			{
				checkf(false, TEXT("FDynamicMesh3::CollapseEdge: failed at isboundary=true branch, ebd replace triangle"));
				return EMeshResult::Failed_UnrecoverableError;
			}
		}
	}

	// set kept vertex to interpolated collapse position
	SetVertex(vKeep, FVector3d::Lerp(KeptPos, RemovedPos, collapse_t));

	CollapseInfo.KeptVertex = vKeep;
	CollapseInfo.RemovedVertex = vRemove;
	CollapseInfo.bIsBoundary = bIsBoundaryEdge;
	CollapseInfo.CollapsedEdge = eab;
	CollapseInfo.RemovedTris = FIndex2i(t0, t1);
	CollapseInfo.RemovedEdges = FIndex2i(eac, ead);
	CollapseInfo.KeptEdges = FIndex2i(ebc, ebd);
	CollapseInfo.CollapseT = collapse_t;

	if (HasAttributes())
	{
		Attributes()->OnCollapseEdge(CollapseInfo);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}












EMeshResult FDynamicMesh3::MergeEdges(int eKeep, int eDiscard, FMergeEdgesInfo& MergeInfo)
{
	MergeInfo = FMergeEdgesInfo();

	if (IsEdge(eKeep) == false || IsEdge(eDiscard) == false)
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	FIndex4i edgeinfo_keep = GetEdge(eKeep);
	FIndex4i edgeinfo_discard = GetEdge(eDiscard);
	if (edgeinfo_keep[3] != InvalidID || edgeinfo_discard[3] != InvalidID)
	{
		return EMeshResult::Failed_NotABoundaryEdge;
	}

	int a = edgeinfo_keep[0], b = edgeinfo_keep[1];
	int tab = edgeinfo_keep[2];
	int eab = eKeep;
	int c = edgeinfo_discard[0], d = edgeinfo_discard[1];
	int tcd = edgeinfo_discard[2];
	int ecd = eDiscard;

	// Need to correctly orient a,b and c,d and then check that 
	// we will not join triangles with incompatible winding order
	// I can't see how to do this purely topologically. 
	// So relying on closest-pairs testing.
	IndexUtil::OrientTriEdge(a, b, GetTriangle(tab));
	//int tcd_otherv = OrientTriEdgeAndFindOtherVtx(ref c, ref d, GetTriangle(tcd));
	IndexUtil::OrientTriEdge(c, d, GetTriangle(tcd));
	int x = c; c = d; d = x;   // joinable bdry edges have opposing orientations, so flip to get ac and b/d correspondences
	FVector3d Va = GetVertex(a), Vb = GetVertex(b), Vc = GetVertex(c), Vd = GetVertex(d);
	if (((Va - Vc).SquaredLength() + (Vb - Vd).SquaredLength()) >
		((Va - Vd).SquaredLength() + (Vb - Vc).SquaredLength()))
	{
		return EMeshResult::Failed_SameOrientation;
	}

	// alternative that detects normal flip of triangle tcd. This is a more 
	// robust geometric test, but fails if tri is degenerate...also more expensive
	//FVector3d otherv = GetVertex(tcd_otherv);
	//FVector3d Ncd = TMathUtil.FastNormalDirection(GetVertex(c), GetVertex(d), otherv);
	//FVector3d Nab = TMathUtil.FastNormalDirection(GetVertex(a), GetVertex(b), otherv);
	//if (Ncd.Dot(Nab) < 0)
	//return EMeshResult::Failed_SameOrientation;

	MergeInfo.KeptEdge = eab;
	MergeInfo.RemovedEdge = ecd;

	// if a/c or b/d are connected by an existing edge, we can't merge
	if (a != c && FindEdge(a, c) != InvalidID)
	{
		return EMeshResult::Failed_InvalidNeighbourhood;
	}
	if (b != d && FindEdge(b, d) != InvalidID)
	{
		return EMeshResult::Failed_InvalidNeighbourhood;
	}

	// if vertices at either end already share a common neighbour vertex, and we 
	// do the merge, that would create duplicate edges. This is something like the
	// 'link condition' in edge collapses. 
	// Note that we have to catch cases where both edges to the shared vertex are
	// boundary edges, in that case we will also merge this edge later on
	if (a != c) 
	{
		int ea = 0, ec = 0, other_v = (b == d) ? b : -1;
		for (int cnbr : VtxVerticesItr(c)) 
		{
			if (cnbr != other_v && (ea = FindEdge(a, cnbr)) != InvalidID) 
			{
				ec = FindEdge(c, cnbr);
				if (IsBoundaryEdge(ea) == false || IsBoundaryEdge(ec) == false)
				{
					return EMeshResult::Failed_InvalidNeighbourhood;
				}
			}
		}
	}
	if (b != d) 
	{
		int eb = 0, ed = 0, other_v = (a == c) ? a : -1;
		for (int dnbr : VtxVerticesItr(d)) 
		{
			if (dnbr != other_v && (eb = FindEdge(b, dnbr)) != InvalidID) 
			{
				ed = FindEdge(d, dnbr);
				if (IsBoundaryEdge(eb) == false || IsBoundaryEdge(ed) == false)
				{
					return EMeshResult::Failed_InvalidNeighbourhood;
				}
			}
		}
	}

	// [TODO] this acts on each interior tri twice. could avoid using vtx-tri iterator?
	if (a != c) 
	{
		// replace c w/ a in edges and tris connected to c, and move edges to a
		for (int eid : VertexEdgeLists.Values(c)) 
		{
			if (eid == eDiscard)
			{
				continue;
			}
			ReplaceEdgeVertex(eid, c, a);
			short rc = 0;
			if (ReplaceTriangleVertex(Edges[4 * eid + 2], c, a) >= 0)
			{
				rc++;
			}
			if (Edges[4 * eid + 3] != InvalidID) 
			{
				if (ReplaceTriangleVertex(Edges[4 * eid + 3], c, a) >= 0)
				{
					rc++;
				}
			}
			VertexEdgeLists.Insert(a, eid);
			if (rc > 0) 
			{
				VertexRefCounts.Increment(a, rc);
				VertexRefCounts.Decrement(c, rc);
			}
		}
		VertexEdgeLists.Clear(c);
		VertexRefCounts.Decrement(c);
		MergeInfo.RemovedVerts[0] = c;
	}
	else 
	{
		VertexEdgeLists.Remove(a, ecd);
		MergeInfo.RemovedVerts[0] = InvalidID;
	}
	MergeInfo.KeptVerts[0] = a;

	if (d != b) 
	{
		// replace d w/ b in edges and tris connected to d, and move edges to b
		for (int eid : VertexEdgeLists.Values(d)) 
		{
			if (eid == eDiscard)
			{
				continue;
			}
			ReplaceEdgeVertex(eid, d, b);
			short rc = 0;
			if (ReplaceTriangleVertex(Edges[4 * eid + 2], d, b) >= 0)
			{
				rc++;
			}
			if (Edges[4 * eid + 3] != InvalidID) 
			{
				if (ReplaceTriangleVertex(Edges[4 * eid + 3], d, b) >= 0)
				{
					rc++;
				}
			}
			VertexEdgeLists.Insert(b, eid);
			if (rc > 0) 
			{
				VertexRefCounts.Increment(b, rc);
				VertexRefCounts.Decrement(d, rc);
			}

		}
		VertexEdgeLists.Clear(d);
		VertexRefCounts.Decrement(d);
		MergeInfo.RemovedVerts[1] = d;
	}
	else 
	{
		VertexEdgeLists.Remove(b, ecd);
		MergeInfo.RemovedVerts[1] = InvalidID;
	}
	MergeInfo.KeptVerts[1] = b;

	// replace edge cd with edge ab in triangle tcd
	ReplaceTriangleEdge(tcd, ecd, eab);
	EdgeRefCounts.Decrement(ecd);

	// update edge-tri adjacency
	SetEdgeTrianglesInternal(eab, tab, tcd);

	// Once we merge ab to cd, there may be additional edges (now) connected
	// to either a or b that are connected to the same vertex on their 'other' side.
	// So we now have two boundary edges connecting the same two vertices - disaster!
	// We need to find and merge these edges. 
	// Q: I don't think it is possible to have multiple such edge-pairs at a or b
	//    But I am not certain...is a bit tricky to handle because we modify edges_v...
	MergeInfo.ExtraRemovedEdges = FIndex2i(InvalidID, InvalidID);
	MergeInfo.ExtraKeptEdges = MergeInfo.ExtraRemovedEdges;
	for (int vi = 0; vi < 2; ++vi) 
	{
		int v1 = a, v2 = c;   // vertices of merged edge
		if (vi == 1) 
		{
			v1 = b; v2 = d;
		}
		if (v1 == v2)
		{
			continue;
		}

		TArray<int> edges_v;
		GetVertexEdgesList(v1, edges_v);
		int Nedges = (int)edges_v.Num();
		bool found = false;
		// in this loop, we compare 'other' vert_1 and vert_2 of edges around v1.
		// problem case is when vert_1 == vert_2  (ie two edges w/ same other vtx).
		//restart_merge_loop:
		for (int i = 0; i < Nedges && found == false; ++i) 
		{
			int edge_1 = edges_v[i];
			if (IsBoundaryEdge(edge_1) == false)
			{
				continue;
			}
			int vert_1 = GetOtherEdgeVertex(edge_1, v1);
			for (int j = i + 1; j < Nedges; ++j) 
			{
				int edge_2 = edges_v[j];
				int vert_2 = GetOtherEdgeVertex(edge_2, v1);
				if (vert_1 == vert_2 && IsBoundaryEdge(edge_2))  // if ! boundary here, we are in deep trouble...
				{
					// replace edge_2 w/ edge_1 in tri, update edge and vtx-edge-nbr lists
					int tri_1 = Edges[4 * edge_1 + 2];
					int tri_2 = Edges[4 * edge_2 + 2];
					ReplaceTriangleEdge(tri_2, edge_2, edge_1);
					SetEdgeTrianglesInternal(edge_1, tri_1, tri_2);
					VertexEdgeLists.Remove(v1, edge_2);
					VertexEdgeLists.Remove(vert_1, edge_2);
					EdgeRefCounts.Decrement(edge_2);
					MergeInfo.ExtraRemovedEdges[vi] = edge_2;
					MergeInfo.ExtraKeptEdges[vi] = edge_1;

					//edges_v = VertexEdgeLists_list(v1);      // this code allows us to continue checking, ie in case we had
					//Nedges = edges_v.Count;               // multiple such edges. but I don't think it's possible.
					//goto restart_merge_loop;
					found = true;			  // exit outer i loop
					break;					  // exit inner j loop
				}
			}
		}
	}

	if (HasAttributes())
	{
		Attributes()->OnMergeEdges(MergeInfo);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}









EMeshResult FDynamicMesh3::PokeTriangle(int TriangleID, const FVector3d& BaryCoordinates, FPokeTriangleInfo& PokeResult)
{
	PokeResult = FPokeTriangleInfo();

	if (!IsTriangle(TriangleID))
	{
		return EMeshResult::Failed_NotATriangle;
	}

	FIndex3i tv = GetTriangle(TriangleID);
	FIndex3i te = GetTriEdges(TriangleID);

	// create vertex with interpolated vertex attribs
	FVertexInfo vinfo;
	GetTriBaryPoint(TriangleID, BaryCoordinates[0], BaryCoordinates[1], BaryCoordinates[2], vinfo);
	int center = AppendVertex(vinfo);

	// add in edges to center vtx, do not connect to triangles yet
	int eaC = AddEdgeInternal(tv[0], center, -1, -1);
	int ebC = AddEdgeInternal(tv[1], center, -1, -1);
	int ecC = AddEdgeInternal(tv[2], center, -1, -1);
	VertexRefCounts.Increment(tv[0]);
	VertexRefCounts.Increment(tv[1]);
	VertexRefCounts.Increment(tv[2]);
	VertexRefCounts.Increment(center, 3);

	// old triangle becomes tri along first edge
	SetTriangleInternal(TriangleID, tv[0], tv[1], center);
	SetTriangleEdgesInternal(TriangleID, te[0], ebC, eaC);

	// add two triangles
	int t1 = AddTriangleInternal(tv[1], tv[2], center, te[1], ecC, ebC);
	int t2 = AddTriangleInternal(tv[2], tv[0], center, te[2], eaC, ecC);

	// second and third edges of original tri have neighbours
	ReplaceEdgeTriangle(te[1], TriangleID, t1);
	ReplaceEdgeTriangle(te[2], TriangleID, t2);

	// set the triangles for the edges we created above
	SetEdgeTrianglesInternal(eaC, TriangleID, t2);
	SetEdgeTrianglesInternal(ebC, TriangleID, t1);
	SetEdgeTrianglesInternal(ecC, t1, t2);

	// transfer groups
	if (HasTriangleGroups()) 
	{
		int g = (*TriangleGroups)[TriangleID];
		TriangleGroups->InsertAt(g, t1);
		TriangleGroups->InsertAt(g, t2);
	}

	PokeResult.OriginalTriangle = TriangleID;
	PokeResult.TriVertices = tv;
	PokeResult.NewVertex = center;
	PokeResult.NewTriangles = FIndex2i(t1,t2);
	PokeResult.NewEdges = FIndex3i(eaC, ebC, ecC);
	PokeResult.BaryCoords = BaryCoordinates;

	if (HasAttributes())
	{
		Attributes()->OnPokeTriangle(PokeResult);
	}

	UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}




