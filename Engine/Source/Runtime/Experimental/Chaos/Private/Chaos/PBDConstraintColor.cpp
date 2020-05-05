// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintColor.h"

#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ChaosLog.h"

#include <memory>
#include <queue>
#include <sstream>

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeColors"), STAT_Constraint_ComputeColor, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeContactGraph"), STAT_Constraint_ComputeContactGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeIslandColoring"), STAT_Constraint_ComputeIslandColoring, STATGROUP_Chaos);

void FPBDConstraintColor::ComputeIslandColoring(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeIslandColoring);
	const TArray<TGeometryParticleHandle<FReal, 3>*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);
	FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = IslandData[Island].LevelToColorToConstraintListMap;
	int32& MaxColor = IslandData[Island].MaxColor;

	const int32 MaxLevel = IslandData[Island].MaxLevel;
	
	LevelToColorToConstraintListMap.Reset();
	LevelToColorToConstraintListMap.SetNum(MaxLevel + 1);
	MaxColor = -1;

	TSet<int32> ProcessedNodes;
	TArray<int32> NodesToProcess;

	for (const TGeometryParticleHandle<FReal, 3>* Particle :IslandParticles)
	{
		if (!ConstraintGraph.ParticleToNodeIndex.Find(Particle))
		{
			continue;
		}

		const int32 ParticleNodeIndex = ConstraintGraph.ParticleToNodeIndex[Particle];

		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;
		if (ProcessedNodes.Contains(ParticleNodeIndex) || bIsParticleDynamic == false)
		{
			continue;
		}

		NodesToProcess.Add(ParticleNodeIndex);

		while (NodesToProcess.Num())
		{
			const int32 NodeIndex = NodesToProcess.Last();
			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];
			FGraphNodeColor& ColorNode = Nodes[NodeIndex];

			NodesToProcess.SetNum(NodesToProcess.Num() - 1, /*bAllowShrinking=*/false);
			ProcessedNodes.Add(NodeIndex);

			for (const int32 EdgeIndex : GraphNode.Edges)
			{
				const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
				FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

				// If this is not from our rule, ignore it
				if (GraphEdge.Data.GetContainerId() != ContainerId)
				{
					continue;
				}

				// If edge has been colored skip it
				if (ColorEdge.Color >= 0)
				{
					continue;
				}

				// Get index to the other node on the edge
				int32 OtherNodeIndex = INDEX_NONE;
				if (GraphEdge.FirstNode == NodeIndex)
				{
					OtherNodeIndex = GraphEdge.SecondNode;
				}
				if (GraphEdge.SecondNode == NodeIndex)
				{
					OtherNodeIndex = GraphEdge.FirstNode;
				}

				// Find next color that is not used already at this node
				while (ColorNode.UsedColors.Contains(ColorNode.NextColor))
				{
					ColorNode.NextColor++;
				}
				int32 ColorToUse = ColorNode.NextColor;

				// Exclude colors used by the other node (but still allow this node to use them for other edges)
				if (OtherNodeIndex != INDEX_NONE)
				{
					FGraphNodeColor& OtherColorNode = Nodes[OtherNodeIndex];

					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.Nodes[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.Particle->CastToRigidParticle() && OtherGraphNode.Particle->ObjectState() == EObjectStateType::Dynamic;
					if (bIsOtherGraphNodeDynamic)
					{
						while (OtherColorNode.UsedColors.Contains(ColorToUse) || ColorNode.UsedColors.Contains(ColorToUse))
						{
							ColorToUse++;
						}
					}
				}

				// Assign color and set as used at this node
				MaxColor = FMath::Max(ColorToUse, MaxColor);
				ColorNode.UsedColors.Add(ColorToUse);
				ColorEdge.Color = ColorToUse;

				// Bump color to use next time, but only if we weren't forced to use a different color by the other node
				if ((ColorToUse == ColorNode.NextColor) && bIsParticleDynamic == true)
				{
					ColorNode.NextColor++;
				}

				int32 Level = ColorEdge.Level;

				if ((Level < 0) || (Level >= LevelToColorToConstraintListMap.Num()))
				{
					UE_LOG(LogChaos, Error, TEXT("\t **** Level is out of bounds!!!!  Level - %d, LevelToColorToConstraintListMap.Num() - %d"), Level, LevelToColorToConstraintListMap.Num());
					continue;
				}

				if (!LevelToColorToConstraintListMap[Level].Contains(ColorEdge.Color))
				{
					LevelToColorToConstraintListMap[Level].Add(ColorEdge.Color, {});
				}

				LevelToColorToConstraintListMap[Level][ColorEdge.Color].Add(GraphEdge.Data.GetConstraintHandle());

				if (OtherNodeIndex != INDEX_NONE)
				{
					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.Nodes[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.Particle->CastToRigidParticle() && OtherGraphNode.Particle->ObjectState() == EObjectStateType::Dynamic;
					if (bIsOtherGraphNodeDynamic)
					{
						FGraphNodeColor& OtherColorNode = Nodes[OtherNodeIndex];

						// Mark other node as not allowing use of this color
						if (bIsParticleDynamic)
						{
							OtherColorNode.UsedColors.Add(ColorEdge.Color);
						}

						// Queue other node for processing
						if (!ProcessedNodes.Contains(OtherNodeIndex))
						{
							ensure(OtherGraphNode.Island == GraphNode.Island);
							checkSlow(IslandParticles.Find(OtherGraphNode.Particle) != INDEX_NONE);
							NodesToProcess.Add(OtherNodeIndex);
						}
					}
				}
			}
		}
	}
}

void FPBDConstraintColor::ComputeContactGraph(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeContactGraph);
	const TArray<int32>& ConstraintDataIndices = ConstraintGraph.GetIslandConstraintData(Island);

	IslandData[Island].MaxLevel = ConstraintDataIndices.Num() ? 0 : -1;

	std::queue<std::pair<int32, int32>> QueueToProcess;
	for (const TGeometryParticleHandle<FReal, 3>* Particle : ConstraintGraph.GetIslandParticles(Island))
	{
		const int32* NodeIndexPtr = ConstraintGraph.ParticleToNodeIndex.Find(Particle);
		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;
		if (bIsParticleDynamic == false && NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;
			QueueToProcess.push(std::make_pair(0, NodeIndex));
		}
	}

	while (!QueueToProcess.empty())
	{
		const std::pair<int32, int32> Elem = QueueToProcess.front();
		QueueToProcess.pop();

		int32 Level = Elem.first;
		int32 NodeIndex = Elem.second;
		const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];

		for (int32 EdgeIndex : GraphNode.Edges)
		{
			const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
			FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

			// If this is not from our rule, ignore it
			if (GraphEdge.Data.GetContainerId() != ContainerId)
			{
				continue;
			}

			// If we have already been assigned a level, move on
			if (ColorEdge.Level >= 0)
			{
				continue;
			}

			// Does the node have edges that are not from this Island?
			// @todo(ccaulfield): look into this - this should never happen I think? it's an O(N) check
			if (!ConstraintDataIndices.Contains(EdgeIndex))
			{
				continue;
			}

			// Assign the level and update max level for the island if required
			ColorEdge.Level = Level;
			IslandData[Island].MaxLevel = FGenericPlatformMath::Max(IslandData[Island].MaxLevel, ColorEdge.Level);

			// Find adjacent node and recurse
			int32 OtherNode = INDEX_NONE;
			if (GraphEdge.FirstNode == NodeIndex)
			{
				OtherNode = GraphEdge.SecondNode;
			}
			if (GraphEdge.SecondNode == NodeIndex)
			{
				OtherNode = GraphEdge.FirstNode;
			}
			if (OtherNode != INDEX_NONE)
			{
				QueueToProcess.push(std::make_pair(ColorEdge.Level + 1, OtherNode));
			}
		}
	}

	// @todo(ccaulfield): What is this for? Isolated particles with no constraints? 
	// If so we should add them to some new islands, keeping the number of particles to each island in some reasonable range.
	// Answer: If an island is only dynamics the above code would be skipped. This simply adds them all to level 0
	for (const int32 EdgeIndex : ConstraintDataIndices)
	{
		check(Edges[EdgeIndex].Level <= IslandData[Island].MaxLevel);
		if (Edges[EdgeIndex].Level < 0)
		{
			Edges[EdgeIndex].Level = 0;
		}
	}

	check(IslandData[Island].MaxLevel >= 0 || !ConstraintDataIndices.Num());
}

void FPBDConstraintColor::InitializeColor(const FPBDConstraintGraph& ConstraintGraph)
{
	// The Number of nodes is large and fairly constant so persist rather than resetting every frame
	if (Nodes.Num() != ConstraintGraph.Nodes.Num())
	{
		// Nodes need to grow when the nodes of the constraint graph grows
		Nodes.AddDefaulted(ConstraintGraph.Nodes.Num() - Nodes.Num());
	}
	
	// Reset the existing Nodes - so colors are all reset to zero
	for (int32 UpdatedNode : UpdatedNodes)
	{
		Nodes[UpdatedNode].NextColor = 0;
		Nodes[UpdatedNode].UsedColors.Empty();
	}

	// edges are not persistent right now so we still reset them
	Edges.Reset();
	IslandData.Reset();

	Edges.SetNum(ConstraintGraph.Edges.Num());
	IslandData.SetNum(ConstraintGraph.IslandToData.Num());

	UpdatedNodes = ConstraintGraph.GetUpdatedNodes();
}

void FPBDConstraintColor::ComputeColor(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeColor);
	if (bUseContactGraph)
	{
		ComputeContactGraph(Island, ConstraintGraph, ContainerId);
	}
	else
	{
		for (FGraphEdgeColor& Edge : Edges)
		{
			Edge.Level = 0;
		}
		IslandData[Island].MaxLevel = 0;
	}
	ComputeIslandColoring(Island, ConstraintGraph, ContainerId);
}

const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& FPBDConstraintColor::GetIslandLevelToColorToConstraintListMap(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].LevelToColorToConstraintListMap;
	}
	return EmptyLevelToColorToConstraintListMap;
}

int FPBDConstraintColor::GetIslandMaxColor(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].MaxColor;
	}
	return -1;
}

int FPBDConstraintColor::GetIslandMaxLevel(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].MaxLevel;
	}
	return -1;
}
