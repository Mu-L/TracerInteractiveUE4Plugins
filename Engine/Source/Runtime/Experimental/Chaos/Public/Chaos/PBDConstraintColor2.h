// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDConstraintGraph2.h"

// @todo(ccaulfield): can we get rid of this now?
#define USE_CONTACT_LEVELS 1

namespace Chaos
{
	template <typename T, int d>
	class TGeometryParticleHandle;

	/**
	 * Generates color information for a single constraint rule in a connection graph.
	 * Edges with the same color are non-interacting and can safely be processed in parallel.
	 */
	template<typename T, int d>
	class TPBDConstraintColor2
	{
	public:
		typedef TPBDConstraintGraph2<T, d> FConstraintGraph;
		typedef TSet<int32> FColorSet;
		typedef TArray<int32> FConstraintList;
		typedef TMap<int32, FConstraintList> FColorToConstraintListMap;
		typedef TArray<FColorToConstraintListMap> FLevelToColorToConstraintListMap;

		/**
		 * Initialize the color structures based on the connectivity graph (i.e., reset all color-related node, edge and island data).
		 */
		void InitializeColor(const FConstraintGraph& ConstraintGraph);

		/**
		 * Calculate the color information for the specified island.
		 */
		void ComputeColor(const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId);

		/**
		 * Get the Level-Color-ConstraintList map for the specified island.
		 */
		const FLevelToColorToConstraintListMap& GetIslandLevelToColorToConstraintListMap(int32 Island) const;

		/**
		 * Get the maximum color index used in the specified island.
		 */
		int GetIslandMaxColor(int32 Island) const;

		/**
		 * Get the maximum level index used in the specified island.
		 */
		int GetIslandMaxLevel(int32 Island) const;

	private:
		void ComputeContactGraph(const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId);
		void ComputeIslandColoring(const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId);

		struct FGraphNodeColor
		{
			FGraphNodeColor()
				: NextColor(0)
			{
			}
			int32 NextColor;
			FColorSet UsedColors;
		};

		struct FGraphEdgeColor
		{
			FGraphEdgeColor()
				: Color(INDEX_NONE)
	#ifdef USE_CONTACT_LEVELS
				, Level(INDEX_NONE)
	#endif
			{
			}
			int32 Color;
	#ifdef USE_CONTACT_LEVELS
			int32 Level;
	#endif
		};

		struct FIslandColor
		{
			FIslandColor()
				: MaxColor(0)
	#ifdef USE_CONTACT_LEVELS
				, MaxLevel(0)
	#endif
			{
			}
			int32 MaxColor;
	#ifdef USE_CONTACT_LEVELS
			int32 MaxLevel;
	#endif
			FLevelToColorToConstraintListMap LevelToColorToConstraintListMap;
		};

		TArray<FGraphNodeColor> Nodes;
		TArray<FGraphEdgeColor> Edges;
		TArray<FIslandColor> IslandData;
		FLevelToColorToConstraintListMap EmptyLevelToColorToConstraintListMap;
	};

}
