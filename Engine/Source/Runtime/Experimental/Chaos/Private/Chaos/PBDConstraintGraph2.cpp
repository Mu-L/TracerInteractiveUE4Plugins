// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintGraph2.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDRigidParticles.h"
#include "Containers/Queue.h"
#include "Chaos/ParticleHandle.h"

using namespace Chaos;

template<typename T, int d>
TPBDConstraintGraph2<T, d>::TPBDConstraintGraph2()
{
}

template<typename T, int d>
TPBDConstraintGraph2<T, d>::TPBDConstraintGraph2(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
{
	InitializeGraph(Particles);
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::InitializeGraph(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
{
	const int32 NumNonDisabledParticles = Particles.Num();

	Nodes.Reset();
	Nodes.AddDefaulted(NumNonDisabledParticles);

	Edges.Reset();

	ParticleToNodeIndex.Reset();
	ParticleToNodeIndex.Reserve(NumNonDisabledParticles);
	for (int32 Index = 0; Index < NumNonDisabledParticles; ++Index)
	{
		FGraphNode& Node = Nodes[Index];
		Node.Particle = Particles[Index];
		ParticleToNodeIndex.Add(Node.Particle, Index);
	}

	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandToConstraints)
	{
		IslandConstraintList.Reset();
	}
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::ResetIslands(const TArray<TGeometryParticleHandle<T, d> *>& Particles)
{
	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandToConstraints)
	{
		IslandConstraintList.Reset();
	}

	const int32 NumBodies = Particles.Num();
	for (int32 Idx = 0; Idx < NumBodies; ++Idx)	//@todo(ocohen): could go wide per island if we can get at the sets
	{
		if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particles[Idx]->ToDynamic())
		{
			const int32 Island = PBDRigid->Island();
			if (Island >= 0)
			{
				FGraphNode& Node = Nodes[Idx];
				Node.Island = Island;
				for (int32 ConstraintDataIndex : Node.Edges)
				{
					IslandToConstraints[Island].Add(ConstraintDataIndex);
				}
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::ReserveConstraints(const int32 NumConstraints)
{
	Edges.Reserve(Edges.Num() + NumConstraints);
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::AddConstraint(const uint32 InContainerId, const int32 InConstraintIndex, const TVector<TGeometryParticleHandle<T,d>*, 2>& ConstrainedParticles)
{
	// Must have at least one constrained particle
	check((ConstrainedParticles[0]) || (ConstrainedParticles[1]));

	const int32 NewEdgeIndex = Edges.Num();
	FGraphEdge NewEdge;
	NewEdge.Data = { InContainerId, InConstraintIndex };

	if (ConstrainedParticles[0])
	{
		NewEdge.FirstNode = ParticleToNodeIndex[ConstrainedParticles[0]];
		Nodes[NewEdge.FirstNode].Particle = ConstrainedParticles[0];
		Nodes[NewEdge.FirstNode].Edges.Add(NewEdgeIndex);
	}
	if (ConstrainedParticles[1])
	{
		NewEdge.SecondNode = ParticleToNodeIndex[ConstrainedParticles[1]];
		Nodes[NewEdge.SecondNode].Particle = ConstrainedParticles[1];
		Nodes[NewEdge.SecondNode].Edges.Add(NewEdgeIndex);
	}

	Edges.Add(MoveTemp(NewEdge));
}

template<typename T, int d>
const typename TPBDConstraintGraph2<T, d>::FConstraintData& TPBDConstraintGraph2<T, d>::GetConstraintData(int32 ConstraintDataIndex) const
{
	return Edges[ConstraintDataIndex].Data;
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::UpdateIslands(const TArray<TGeometryParticleHandle<T, d> *>& Particles, TSet<TGeometryParticleHandle<T, d> *>& ActiveParticles)
{
	// Maybe expose a memset style function for this instead of iterating
	for (TGeometryParticleHandle<T,d>* Particle : Particles)
	{
		if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())	//todo: if islands are stored in a more base class than PBDRigids this is going to ignore those
		{
			PBDRigid->Island() = INDEX_NONE;
		}
	}
	ComputeIslands(Particles, ActiveParticles);
}


DECLARE_CYCLE_STAT(TEXT("IslandGeneration2"), STAT_IslandGeneration2, STATGROUP_Chaos);

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::ComputeIslands(const TArray<TGeometryParticleHandle<T, d>*>& Particles, TSet<TGeometryParticleHandle<T, d>*>& ActiveParticles)
{
	SCOPE_CYCLE_COUNTER(STAT_IslandGeneration2);

	int32 NextIsland = 0;
	TArray<TSet<TGeometryParticleHandle<T, d>*>> NewIslandParticles;
	TArray<int32> NewIslandToSleepCount;

	const int32 NumNodes = Particles.Num();
	for (int32 Idx = 0; Idx < NumNodes; ++Idx)
	{
		TGeometryParticleHandle<T, d>* Particle = Particles[Idx];

		if (Nodes[Idx].Island >= 0 || Particle->ToDynamic() == nullptr)
		{
			// Island is already known - it was visited in ComputeIsland for a previous node
			continue;
		}

		TSet<TGeometryParticleHandle<T, d>*> SingleIslandParticles;
		TSet<TGeometryParticleHandle<T, d>*> SingleIslandStaticParticles;
		ComputeIsland(Particles, Idx, NextIsland, SingleIslandParticles, SingleIslandStaticParticles);

		for (TGeometryParticleHandle<T, d>* StaticParticle : SingleIslandStaticParticles)
		{
			SingleIslandParticles.Add(StaticParticle);
		}

		if (SingleIslandParticles.Num())
		{
			NewIslandParticles.SetNum(NextIsland + 1);
			NewIslandParticles[NextIsland] = MoveTemp(SingleIslandParticles);
			NextIsland++;
		}
	}

	IslandToConstraints.SetNum(NextIsland);
	IslandToData.SetNum(NextIsland);

	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		const FGraphEdge& Edge = Edges[EdgeIndex];
		int32 FirstIsland = (Edge.FirstNode != INDEX_NONE) ? Nodes[Edge.FirstNode].Island : INDEX_NONE;
		int32 SecondIsland = (Edge.SecondNode != INDEX_NONE) ? Nodes[Edge.SecondNode].Island : INDEX_NONE;
		check(FirstIsland == SecondIsland || FirstIsland == INDEX_NONE || SecondIsland == INDEX_NONE);

		int32 Island = (FirstIsland != INDEX_NONE) ? FirstIsland : SecondIsland;
		check(Island >= 0);

		IslandToConstraints[Island].Add(EdgeIndex);
	}

	NewIslandToSleepCount.SetNum(NewIslandParticles.Num());

	if (NewIslandParticles.Num())
	{
		for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
		{
			NewIslandToSleepCount[Island] = 0;

			for (TGeometryParticleHandle<T, d>* Particle : NewIslandParticles[Island])
			{
				if (TPBDRigidParticleHandle<T,d>* PBDRigid = Particle->ToDynamic())
				{
					PBDRigid->Island() = Island;
				}
			}
		}
		// Force consistent state if no previous islands
		if (!IslandToParticles.Num())
		{
			for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
			{
				IslandToData[Island].bIsIslandPersistant = true;
				bool bSleepState = true;

				for (TGeometryParticleHandle<T, d>* Particle : NewIslandParticles[Island])
				{
					if (!Particle->Sleeping())
					{
						bSleepState = false;
						break;
					}
				}

				for (TGeometryParticleHandle<T, d>* Particle : NewIslandParticles[Island])
				{
					//@todo(DEMO_HACK) : Need to fix, remove the !InParticles.Disabled(Index)
					if (Particle->Sleeping() && !bSleepState/* && !Particle->Disabled()*/)
					{
						ActiveParticles.Add(Particle);	//todo: record state change for potential array reorder
					}

					if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
					{
						if (!Particle->Sleeping() && bSleepState)
						{
							ActiveParticles.Remove(Particle);
							PBDRigid->V() = TVector<T, d>(0);
							PBDRigid->W() = TVector<T, d>(0);
						}

						PBDRigid->SetSleeping(bSleepState);
					}

					if ((Particle->Sleeping() /*|| Particle->Disabled()*/) && ActiveParticles.Contains(Particle))
					{
						ActiveParticles.Remove(Particle);	//todo: record state change for array reorder
					}
				}
			}
		}

		for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
		{
			bool bIsSameIsland = true;

			// Objects were removed from the island
			int32 OtherIsland = -1;

			for (TGeometryParticleHandle<T, d>* Particle : IslandToParticles[Island])
			{
				TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic();
				int32 TmpIsland = PBDRigid ? PBDRigid->Island() : INDEX_NONE; //question: should we even store non dynamics in this array?

				if (OtherIsland == INDEX_NONE && TmpIsland >= 0)
				{
					OtherIsland = TmpIsland;
				}
				else
				{
					if (TmpIsland >= 0 && OtherIsland != TmpIsland)
					{
						bIsSameIsland = false;
						break;
					}
				}
			}

			// A new object entered the island or the island is entirely new particles
			if (bIsSameIsland && (OtherIsland == INDEX_NONE || NewIslandParticles[OtherIsland].Num() != IslandToParticles[Island].Num()))
			{
				bIsSameIsland = false;
			}

			// Find out if we need to activate island
			if (bIsSameIsland)
			{
				NewIslandToSleepCount[OtherIsland] = IslandToSleepCount[Island];
			}
			else
			{
				for (TGeometryParticleHandle<T, d>* Particle : IslandToParticles[Island])
				{
					//if (!Particle->Disabled()) todo: why is this needed?
					{
						if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
						{
							PBDRigid->SetSleeping(false);
						}
						ActiveParticles.Add(Particle);
					}
				}
			}

			// #BG Necessary? Should we ever not find an island?
			if (OtherIsland != INDEX_NONE)
			{
				IslandToData[OtherIsland].bIsIslandPersistant = bIsSameIsland;
			}
		}
	}

	IslandToParticles.Reset();
	IslandToParticles.Reserve(NewIslandParticles.Num());
	for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
	{
		IslandToParticles.Emplace(NewIslandParticles[Island].Array());
	}
	IslandToSleepCount = MoveTemp(NewIslandToSleepCount);

	check(IslandToParticles.Num() == IslandToSleepCount.Num());
	check(IslandToParticles.Num() == IslandToConstraints.Num());
	check(IslandToParticles.Num() == IslandToData.Num());
	// @todo(ccaulfield): make a more complex unit test to check island integrity
	//checkSlow(CheckIslands(InParticles, ActiveIndices));
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::ComputeIsland(const TArray<TGeometryParticleHandle<T, d> *>& Particles, const int32 InNode, const int32 Island, TSet<TGeometryParticleHandle<T, d> *>& DynamicParticlesInIsland,
	TSet<TGeometryParticleHandle<T, d> *>& StaticParticlesInIsland)
{
	TQueue<int32> NodeQueue;
	NodeQueue.Enqueue(InNode);
	while (!NodeQueue.IsEmpty())
	{
		int32 NodeIndex;
		NodeQueue.Dequeue(NodeIndex);
		FGraphNode& Node = Nodes[NodeIndex];

		if (Node.Island >= 0)
		{
			check(Node.Island == Island);
			continue;
		}

		if (Node.Particle->ToDynamic() == nullptr)
		{
			if (!StaticParticlesInIsland.Contains(Node.Particle))
			{
				StaticParticlesInIsland.Add(Node.Particle);
			}
			continue;
		}

		DynamicParticlesInIsland.Add(Node.Particle);
		Node.Island = Island;

		for (const int32 EdgeIndex : Node.Edges)
		{
			const FGraphEdge& Edge = Edges[EdgeIndex];
			int32 OtherNode = INDEX_NONE;
			if (NodeIndex == Edge.FirstNode)
			{
				OtherNode = Edge.SecondNode;
			}
			if (NodeIndex == Edge.SecondNode)
			{
				OtherNode = Edge.FirstNode;
			}
			if (OtherNode != INDEX_NONE)
			{
				NodeQueue.Enqueue(OtherNode);
			}
		}
	}
}

template<typename T, int d>
bool TPBDConstraintGraph2<T, d>::SleepInactive(const int32 Island, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterialAttributes)
{
	// @todo(ccaulfield): should be able to eliminate this when island is already sleeping

	const TArray<TGeometryParticleHandle<T, d>*>& IslandParticles = GetIslandParticles(Island);
	check(IslandParticles.Num());

	if (!IslandToData[Island].bIsIslandPersistant)
	{
		return false;
	}

	int32& IslandSleepCount = IslandToSleepCount[Island];

	TVector<T, d> X(0);
	TVector<T, d> V(0);
	TVector<T, d> W(0);
	T M = 0;
	T LinearSleepingThreshold = FLT_MAX;
	T AngularSleepingThreshold = FLT_MAX;

	for (const TGeometryParticleHandle<T, d>* Particle : IslandToParticles[Island])
	{
		if (const TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
		{
			X += PBDRigid->X() * PBDRigid->M();
			M += PBDRigid->M();
			V += PBDRigid->V() * PBDRigid->M();

			const int32 HandleIdx = Particle->TransientHandleIdx();
			if (PerParticleMaterialAttributes[HandleIdx])
			{
				LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, PerParticleMaterialAttributes[HandleIdx]->SleepingLinearThreshold);
				AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, PerParticleMaterialAttributes[HandleIdx]->SleepingAngularThreshold);
			}
			else
			{
				LinearSleepingThreshold = (T)0;
				LinearSleepingThreshold = (T)0;
			}
		}
	}

	X /= M;
	V /= M;

	for (const TGeometryParticleHandle<T, d>* Particle: IslandParticles)
	{
		if (const TPBDRigidParticleHandle<T,d>* PBDRigid = Particle->ToDynamic())
		{
			W += /*TVector<T, d>::CrossProduct(PBDRigid->X() - X, PBDRigid->M() * PBDRigid->V()/ +*/ PBDRigid->W() * PBDRigid->M();
		}
	}

	W /= M;

	const T VSize = V.SizeSquared();
	const T WSize = W.SizeSquared();

	if (VSize < LinearSleepingThreshold && WSize < AngularSleepingThreshold)
	{
		if (IslandSleepCount > SleepCountThreshold)
		{
			for (TGeometryParticleHandle<T, d>* Particle : IslandParticles)
			{
				if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
				{
					PBDRigid->SetSleeping(true);
					PBDRigid->V() = TVector<T, d>(0);
					PBDRigid->W() = TVector<T, d>(0);
				}
				
			}
			return true;
		}
		else
		{
			IslandSleepCount++;
		}
	}

	return false;
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::WakeIsland(const int32 Island)
{
	for (TGeometryParticleHandle<T, d>* Particle : IslandToParticles[Island])
	{
		if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
		{
			if (PBDRigid->Sleeping())
			{
				PBDRigid->SetSleeping(false);
			}
		}
	}
	IslandToSleepCount[Island] = 0;
}


template<typename T, int d>
void TPBDConstraintGraph2<T, d>::ReconcileIslands()
{
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		bool IsSleeping = true;
		bool IsSet = false;
		for (TGeometryParticleHandle<T, d>* Particle : IslandToParticles[Island])
		{
			if (Particle->ObjectState() == EObjectStateType::Static)
			{
				continue;
			}
			if (!IsSet)
			{
				IsSet = true;
				IsSleeping = Particle->Sleeping();
			}
			if (Particle->Sleeping() != IsSleeping)
			{
				WakeIsland(Island);
				break;
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::EnableParticle(TGeometryParticleHandle<T, d>* Particle, const TGeometryParticleHandle<T, d>* ParentParticle)
{
	if (ParentParticle)
	{
		if (const TPBDRigidParticleHandle<T, d>* ParentPBDRigid = ParentParticle->ToDynamic())
		{
			if (TPBDRigidParticleHandle<T, d>* ChildPBDRigid = Particle->ToDynamic())
			{
				const int32 Island = ParentPBDRigid->Island();
				ChildPBDRigid->Island() = Island;
				if (ensure(IslandToParticles.IsValidIndex(Island)))
				{
					IslandToParticles[Island].Add(Particle);
				}

				const bool SleepState = ParentPBDRigid->Sleeping();
				ChildPBDRigid->SetSleeping(SleepState);	//todo: need to let evolution know to reorder arrays
			}
			else
			{
				ensure(false);	//this should never happen
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::DisableParticle(TGeometryParticleHandle<T, d>* Particle)
{
	if (TPBDRigidParticleHandle<T, d>* PBDRigid = Particle->ToDynamic())
	{
		const int32 Island = PBDRigid->Island();
		if (Island != INDEX_NONE)
		{
			PBDRigid->Island() = INDEX_NONE;

			// @todo(ccaulfield): optimize
			if (ensure(IslandToParticles.IsValidIndex(Island)))
			{
				int32 IslandParticleArrayIdx = IslandToParticles[Island].Find(Particle);
				check(IslandParticleArrayIdx != INDEX_NONE);
				IslandToParticles[Island].RemoveAtSwap(IslandParticleArrayIdx);
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph2<T, d>::DisableParticles(const TSet<TGeometryParticleHandle<T, d> *>& Particles)
{
	// @todo(ccaulfield): optimize
	for (TGeometryParticleHandle<T,d>* Particle : Particles)
	{
		DisableParticle(Particle);
	}
}

template<typename T, int d>
bool TPBDConstraintGraph2<T, d>::CheckIslands(const TArray<TGeometryParticleHandle<T, d> *>& Particles)
{
	bool bIsValid = true;

	// Check that no particles are in multiple islands
	TSet<TGeometryParticleHandle<T, d>*> IslandParticlesUnionSet;
	IslandParticlesUnionSet.Reserve(Particles.Num());
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		TSet<TGeometryParticleHandle<T, d>*> IslandParticlesSet = TSet<TGeometryParticleHandle<T,d>*>(IslandToParticles[Island]);
		TSet<TGeometryParticleHandle<T, d>*> IslandParticlesIntersectSet = IslandParticlesUnionSet.Intersect(IslandParticlesSet);
		if (IslandParticlesIntersectSet.Num() > 0)
		{
			// This islands contains particles that were in a previous island.
			// This is ok only if those particles are static
			for (TGeometryParticleHandle<T,d>* Particle : IslandParticlesIntersectSet)
			{
				if (Particle->ToDynamic())
				{
					UE_LOG(LogChaos, Error, TEXT("Island %d contains non-static particle that is also in another Island"), Island);	//todo: add better logging for bad particle
					bIsValid = false;
				}
			}
		}
		IslandParticlesUnionSet = IslandParticlesUnionSet.Union(IslandParticlesSet);
	}

	// Check that no constraints refer in the same island
	TSet<int32> IslandConstraintDataUnionSet;
	IslandConstraintDataUnionSet.Reserve(Edges.Num());
	for (int32 Island = 0; Island < IslandToConstraints.Num(); ++Island)
	{
		TSet<int32> IslandConstraintDataSet = TSet<int32>(IslandToConstraints[Island]);
		TSet<int32> IslandConstraintDataIntersectSet = IslandConstraintDataUnionSet.Intersect(IslandConstraintDataSet);
		if (IslandConstraintDataIntersectSet.Num() > 0)
		{
			// This islands contains constraints that were in a previous island
			UE_LOG(LogChaos, Error, TEXT("Island %d contains Constraints in another Island"), Island);
			bIsValid = false;
		}
		IslandConstraintDataUnionSet = IslandConstraintDataUnionSet.Union(IslandConstraintDataSet);
	}

	return bIsValid;
}


template class Chaos::TPBDConstraintGraph2<float, 3>;
