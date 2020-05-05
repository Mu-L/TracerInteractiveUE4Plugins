// Copyright Epic Games, Inc. All Rights Reserved. 

#include "OctreeDynamicMeshComponent.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "VertexFactory.h"
#include "MaterialShared.h"
#include "Engine/CollisionProfile.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"

#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "DynamicMeshChangeTracker.h"



// default proxy for this component
#include "OctreeDynamicMeshSceneProxy.h"




UOctreeDynamicMeshComponent::UOctreeDynamicMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	InitializeNewMesh();
}



void UOctreeDynamicMeshComponent::InitializeMesh(FMeshDescription* MeshDescription)
{
	FMeshDescriptionToDynamicMesh Converter;
	Mesh->Clear();
	Converter.Convert(MeshDescription, *Mesh);

	FAxisAlignedBox3d MeshBounds = Mesh->GetCachedBounds();
	Octree = MakeUnique<FDynamicMeshOctree3>();
	Octree->RootDimension = MeshBounds.MaxDim() * 0.5;

	Octree->Initialize(GetMesh());
	
	FDynamicMeshOctree3::FStatistics Stats;
	Octree->ComputeStatistics(Stats);
	//UE_LOG(LogTemp, Warning, TEXT("OctreeStats %s"), *Stats.ToString());

	OctreeCut = MakeUnique<FDynamicMeshOctree3::FTreeCutSet>();

	NotifyMeshUpdated();
}


TUniquePtr<FDynamicMesh3> UOctreeDynamicMeshComponent::ExtractMesh(bool bNotifyUpdate)
{
	TUniquePtr<FDynamicMesh3> CurMesh = MoveTemp(Mesh);
	InitializeNewMesh();
	if (bNotifyUpdate)
	{
		NotifyMeshUpdated();
	}
	return CurMesh;
}


void UOctreeDynamicMeshComponent::InitializeNewMesh()
{
	Mesh = MakeUnique<FDynamicMesh3>();
	// discard any attributes/etc initialized by default
	Mesh->Clear();
	Octree = MakeUnique<FDynamicMeshOctree3>();
	Octree->Initialize(GetMesh());
	OctreeCut = MakeUnique<FDynamicMeshOctree3::FTreeCutSet>();
}



void UOctreeDynamicMeshComponent::ApplyTransform(const FTransform3d& Transform, bool bInvert)
{
	if (bInvert)
	{
		MeshTransforms::ApplyTransformInverse(*GetMesh(), Transform);
	}
	else
	{
		MeshTransforms::ApplyTransform(*GetMesh(), Transform);
	}

	if (GetCurrentSceneProxy() != nullptr)
	{
		Octree->ModifiedBounds = FAxisAlignedBox3d(
			-TNumericLimits<float>::Max()*FVector3d::One(),
			TNumericLimits<float>::Max()*FVector3d::One());
		NotifyMeshUpdated();
	}
	else
	{
		FAxisAlignedBox3d MeshBounds = Mesh->GetCachedBounds();
		Octree = MakeUnique<FDynamicMeshOctree3>();
		Octree->RootDimension = MeshBounds.MaxDim() * 0.5;
		Octree->Initialize(GetMesh());
		OctreeCut = MakeUnique<FDynamicMeshOctree3::FTreeCutSet>();
	}

}


void UOctreeDynamicMeshComponent::Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions)
{
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	if (bHaveModifiedTopology == false && Converter.HaveMatchingElementCounts(Mesh.Get(), MeshDescription))
	{
		Converter.Update(Mesh.Get(), *MeshDescription);
	}
	else
	{
		Converter.Convert(Mesh.Get(), *MeshDescription);

		//UE_LOG(LogTemp, Warning, TEXT("MeshDescription has %d instances"), MeshDescription->VertexInstances().Num());
	}
}



void UOctreeDynamicMeshComponent::NotifyMeshUpdated()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		FAxisAlignedBox3d DirtyBox = Octree->ModifiedBounds;
		Octree->ResetModifiedBounds();

		// update existing cells
		int TotalTriangleCount = 0;
		int SpillTriangleCount = 0;
		TArray<int32> SetsToUpdate;

		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateExisting);
			FCriticalSection SetsLock;
			int NumCutCells = CutCellSetMap.Num();
			ParallelFor(NumCutCells, [&](int i) 
			{
				const FCutCellIndexSet& CutCellSet = CutCellSetMap[i];

				if (Octree->TestCellIntersection(CutCellSet.CellRef, DirtyBox) == false)
				{
					return;
				}

				TArray<int32>& TriangleSet = TriangleDecomposition.GetIndexSetArray(CutCellSet.DecompSetID);
				TriangleSet.Reset();
				Octree->CollectTriangles(CutCellSet.CellRef, [&TriangleSet](int TriangleID) {
					TriangleSet.Add(TriangleID);
				});

				SetsLock.Lock();
				TotalTriangleCount += TriangleSet.Num();
				SetsToUpdate.Add(CutCellSet.DecompSetID);
				SetsLock.Unlock();
			}, false);
		}

		// update cut set to find new cells
		TArray<FDynamicMeshOctree3::FCellReference> NewCutCells;
		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateCutSet);
			Octree->UpdateLevelCutSet(*OctreeCut, NewCutCells);
		}

		// add new ones
		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_CreateNew);
			for (const FDynamicMeshOctree3::FCellReference& CellRef : NewCutCells)
			{

				int32 IndexSetID = TriangleDecomposition.CreateNewIndexSet();
				TArray<int32>& TriangleSet = TriangleDecomposition.GetIndexSetArray(IndexSetID);
				Octree->CollectTriangles(CellRef, [&TriangleSet](int TriangleID) {
					TriangleSet.Add(TriangleID);
				});
				TotalTriangleCount += TriangleSet.Num();
				FCutCellIndexSet SetMapEntry = { CellRef, IndexSetID };
				CutCellSetMap.Add(SetMapEntry);
				SetsToUpdate.Add(IndexSetID);
			}
		}

		// rebuild spill set (always for now...should track bounds? and do separately for each root cell?)
		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateSpill);
			TArray<int32>& SpillTriangleSet = TriangleDecomposition.GetIndexSetArray(SpillDecompSetID);
			SpillTriangleSet.Reset();
			Octree->CollectRootTriangles(*OctreeCut,
				[&SpillTriangleSet](int TriangleID) {
				SpillTriangleSet.Add(TriangleID);
			});
			Octree->CollectSpillTriangles(
				[&SpillTriangleSet](int TriangleID) {
				SpillTriangleSet.Add(TriangleID);
			});
			TotalTriangleCount += SpillTriangleSet.Num();
			SpillTriangleCount += SpillTriangleSet.Num();
			SetsToUpdate.Add(SpillDecompSetID);
		}


		//UE_LOG(LogTemp, Warning, TEXT("Updating %d of %d decomposition sets, %d tris total, spillcount %d"), SetsToUpdate.Num(), CutCellSetMap.Num(), TotalTriangleCount, SpillTriangleCount);
		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateFromDecomp);
			GetCurrentSceneProxy()->UpdateFromDecomposition(TriangleDecomposition, SetsToUpdate);
		}

	}
}




static void InitializeOctreeCutSet(const FDynamicMesh3& Mesh, const FDynamicMeshOctree3& Octree, TUniquePtr<FDynamicMeshOctree3::FTreeCutSet>& CutSet)
{
	int TriangleCount = Mesh.TriangleCount();
	if (TriangleCount < 50000)
	{
		*CutSet = Octree.BuildLevelCutSet(1);
		return;
	}

	FDynamicMeshOctree3::FStatistics Stats;
	Octree.ComputeStatistics(Stats);
	int MaxLevel = Stats.Levels;

	int CutLevel = 0;
	while ( CutLevel < MaxLevel-1 && Stats.LevelBoxCounts[CutLevel] < 200 && Stats.LevelBoxCounts[CutLevel+1] < 300)
	{
		CutLevel++;
	}
	*CutSet = Octree.BuildLevelCutSet(CutLevel);
	//UE_LOG(LogTemp, Warning, TEXT("InitializeOctreeCutSet - level %d cells %d"), CutLevel, CutSet->CutCells.Num());
}




FPrimitiveSceneProxy* UOctreeDynamicMeshComponent::CreateSceneProxy()
{
	check(GetCurrentSceneProxy() == nullptr);

	FOctreeDynamicMeshSceneProxy* NewProxy = nullptr;
	if (Mesh->TriangleCount() > 0)
	{
		NewProxy = new FOctreeDynamicMeshSceneProxy(this);

		if (TriangleColorFunc != nullptr)
		{
			NewProxy->bUsePerTriangleColor = true;
			NewProxy->PerTriangleColorFunc = [this](const FDynamicMesh3* Mesh, int TriangleID) { return GetTriangleColor(TriangleID); };
		}


		OctreeCut = MakeUnique<FDynamicMeshOctree3::FTreeCutSet>();
		InitializeOctreeCutSet(*Mesh, *Octree, OctreeCut);

		TriangleDecomposition = FArrayIndexSetsDecomposition();

		SpillDecompSetID = TriangleDecomposition.CreateNewIndexSet();

		CutCellSetMap.Reset();
		for (auto CellRef : OctreeCut->CutCells)
		{
			int32 IndexSetID = TriangleDecomposition.CreateNewIndexSet();
			TArray<int32>& TriangleSet = TriangleDecomposition.GetIndexSetArray(IndexSetID);
			Octree->CollectTriangles(CellRef, [&TriangleSet](int TriangleID) {
				TriangleSet.Add(TriangleID);
			});

			FCutCellIndexSet SetMapEntry = { CellRef, IndexSetID };
			CutCellSetMap.Add(SetMapEntry);
		}

		// collect spill triangles
		{
			TArray<int32>& SpillTriangleSet = TriangleDecomposition.GetIndexSetArray(SpillDecompSetID);
			Octree->CollectRootTriangles(*OctreeCut, 
				[&SpillTriangleSet](int TriangleID) {
					SpillTriangleSet.Add(TriangleID);
			});
			Octree->CollectSpillTriangles(
				[&SpillTriangleSet](int TriangleID) {
				SpillTriangleSet.Add(TriangleID);
			});
		}

		NewProxy->InitializeFromDecomposition(TriangleDecomposition);
	}
	return NewProxy;
}



void UOctreeDynamicMeshComponent::NotifyMaterialSetUpdated()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->UpdatedReferencedMaterials();
	}
}




FColor UOctreeDynamicMeshComponent::GetTriangleColor(int TriangleID)
{
	if (TriangleColorFunc != nullptr)
	{
		return TriangleColorFunc(Mesh.Get(), TriangleID);
	}
	else
	{
		return (TriangleID % 2 == 0) ? FColor::Red : FColor::White;
	}
}



FBoxSphereBounds UOctreeDynamicMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds are tighter if the box is generated from transformed vertices.
	FBox BoundingBox(ForceInit);
	for ( FVector3d Vertex : Mesh->VerticesItr() ) 
	{
		BoundingBox += LocalToWorld.TransformPosition((FVector)Vertex);
	}
	return FBoxSphereBounds(BoundingBox);
}



void UOctreeDynamicMeshComponent::ApplyChange(const FMeshVertexChange* Change, bool bRevert)
{
	Octree->ResetModifiedBounds();
	TSet<int> TrianglesToUpdate;
	auto NotifyTriVerticesAffected = [&](int32 vid) 
	{
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			if (TrianglesToUpdate.Contains(tid) == false)
			{
				Octree->NotifyPendingModification(tid);
				TrianglesToUpdate.Add(tid);
			}
		}
	};

	int NV = Change->Vertices.Num();
	const TArray<FVector3d>& Positions = (bRevert) ? Change->OldPositions : Change->NewPositions;
	for (int k = 0; k < NV; ++k)
	{
		int vid = Change->Vertices[k];
		NotifyTriVerticesAffected(vid);
		Mesh->SetVertex(vid, Positions[k]);
	}

	if (Change->bHaveOverlayNormals && Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals())
	{
		FDynamicMeshNormalOverlay* Overlay = Mesh->Attributes()->PrimaryNormals();
		int32 NumNormals = Change->Normals.Num();
		const TArray<FVector3f>& UseNormals = (bRevert) ? Change->OldNormals : Change->NewNormals;
		for (int32 k = 0; k < NumNormals; ++k)
		{
			int32 elemid = Change->Normals[k];
			if (Overlay->IsElement(elemid))
			{
				Overlay->SetElement(elemid, UseNormals[k]);
				int32 ParentVID = Overlay->GetParentVertex(elemid);
				NotifyTriVerticesAffected(ParentVID);
			}
		}
	}

	Octree->ReinsertTriangles(TrianglesToUpdate);

	//NotifyMeshUpdated();

	OnMeshChanged.Broadcast();
}




void UOctreeDynamicMeshComponent::ApplyChange(const FMeshChange* Change, bool bRevert)
{
	TArray<int> RemoveTriangles, AddTriangles;
	bool bRemoveOld = ! bRevert;
	Change->DynamicMeshChange->GetSavedTriangleList(RemoveTriangles, bRemoveOld);
	Change->DynamicMeshChange->GetSavedTriangleList(AddTriangles, !bRemoveOld);

	Octree->ResetModifiedBounds();
	Octree->RemoveTriangles(RemoveTriangles);

	Change->DynamicMeshChange->Apply(Mesh.Get(), bRevert);

	Octree->InsertTriangles(AddTriangles);

	//NotifyMeshUpdated();

	OnMeshChanged.Broadcast();
}


void UOctreeDynamicMeshComponent::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
{
	// full clear and reset
	Mesh->Clear();
	Mesh->Copy(*Change->GetMesh(bRevert));
	Octree = MakeUnique<FDynamicMeshOctree3>();
	Octree->Initialize(GetMesh());
	OctreeCut = MakeUnique<FDynamicMeshOctree3::FTreeCutSet>();

	//NotifyMeshUpdated();

	OnMeshChanged.Broadcast();
}
