// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ArrayND.h"
#include "Chaos/BoundingVolumeUtilities.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/Transform.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/ISpatialAcceleration.h"
#include "HAL/IConsoleManager.h"

#include <memory>
#include <unordered_set>

// Required for debug blocks below in raycasts
//#include "Engine/Engine.h"
//#include "Engine/World.h"
//#include "DrawDebugHelpers.h"

struct FBoundingVolumeCVars
{
	static int32 FilterFarBodies;
	static FAutoConsoleVariableRef CVarFilterFarBodies;
};

namespace Chaos
{

template <typename OBJECT_ARRAY, typename T, int d>
class TBoundingVolume;

template <typename OBJECT_ARRAY, typename T, int d>
void FixupLeafObj(const OBJECT_ARRAY& Objects, TArray<TBoundingVolume<OBJECT_ARRAY, T, d>>& Leafs);

template<class OBJECT_ARRAY, class T, int d>
class TBoundingVolume final : public ISpatialAcceleration<T,d>
{
  public:
	  static constexpr int32 DefaultMaxCells = 15;
	TBoundingVolume()
		: MObjects(nullptr)
	{
	}

	TBoundingVolume(const OBJECT_ARRAY& Objects, const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells)
	    : MObjects(&Objects)
	{
		Reinitialize(bUseVelocity, Dt, MaxCells);
	}

	TBoundingVolume(const OBJECT_ARRAY& Objects, const TArray<uint32>& ActiveIndices, const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells)
	    : MObjects(&Objects)
	{
		check(GetObjectCount(Objects) > 0);
		Reinitialize(ActiveIndices, bUseVelocity, Dt, MaxCells);
	}

	TBoundingVolume(TBoundingVolume<OBJECT_ARRAY, T, d>&& Other)
		: MObjects(Other.MObjects), MGlobalObjects(MoveTemp(Other.MGlobalObjects)), MAllObjects(MoveTemp(Other.MAllObjects)), MWorldSpaceBoxes(MoveTemp(Other.MWorldSpaceBoxes)), MGrid(MoveTemp(Other.MGrid)), MElements(MoveTemp(Other.MElements)), bIsEmpty(Other.bIsEmpty)
	{
	}

private:
	TBoundingVolume(const TBoundingVolume<OBJECT_ARRAY, T, d>& Other)
		: MObjects(Other.MObjects), MGlobalObjects(Other.MGlobalObjects), MAllObjects(Other.MAllObjects), MWorldSpaceBoxes(Other.MWorldSpaceBoxes), MGrid(Other.MGrid), MElements(Other.MElements.Copy()), bIsEmpty(Other.bIsEmpty)
	{
	}

public:
	TBoundingVolume<OBJECT_ARRAY, T, d>& operator=(const TBoundingVolume<OBJECT_ARRAY, T, d>& Other) = delete;
	TBoundingVolume<OBJECT_ARRAY, T, d>& operator=(TBoundingVolume<OBJECT_ARRAY, T, d>&& Other)
	{
		const_cast<OBJECT_ARRAY*&>(MObjects) = const_cast<OBJECT_ARRAY*>(Other.MObjects);
		MGlobalObjects = MoveTemp(Other.MGlobalObjects);
		MAllObjects = MoveTemp(Other.MAllObjects);
		MWorldSpaceBoxes = MoveTemp(Other.MWorldSpaceBoxes);
		MGrid = MoveTemp(Other.MGrid);
		MElements = MoveTemp(Other.MElements);
		bIsEmpty = Other.bIsEmpty;
		return *this;
	}

	TBoundingVolume<OBJECT_ARRAY, T, d> Copy() const
	{
		return TBoundingVolume<OBJECT_ARRAY, T, d>(*this);
	}

	void Reinitialize(const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells)
	{
		MGlobalObjects.Reset();
		MAllObjects.Reset();
		for (int32 i = 0; i < GetObjectCount(*MObjects); ++i)
		{
			if (IsDisabled(*MObjects, i))
				continue;
			if (HasBoundingBox(*MObjects, i))
			{
				MAllObjects.Add(i);
			}
			else
			{
				MGlobalObjects.Add(i);
			}
		}
		bIsEmpty = MAllObjects.Num() == 0;
		GenerateTree(*MObjects, MAllObjects, bUseVelocity, Dt, MaxCells);
		check(bIsEmpty || MGrid.GetNumCells() > 0);
	}

	void Reinitialize(const TArray<uint32>& ActiveIndices, const bool bUseVelocity = false, const T Dt = 0, const int32 MaxCells = DefaultMaxCells)
	{
		MGlobalObjects.Reset();
		MAllObjects.Reset();
		for (const auto& Index : ActiveIndices)
		{
			check(!IsDisabled(*MObjects, Index));
			if (HasBoundingBox(*MObjects, Index))
			{
				MAllObjects.Add(Index);
			}
			else
			{
				MGlobalObjects.Add(Index);
			}
		}
		bIsEmpty = MAllObjects.Num() == 0;
		GenerateTree(*MObjects, MAllObjects, bUseVelocity, Dt, MaxCells);
		check(bIsEmpty || MGrid.GetNumCells() > 0);
	}

	void GenerateTree(const OBJECT_ARRAY& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, const int32 MaxCells)
	{
		if (!AllObjects.Num())
		{
			return;
		}
		ComputeAllWorldSpaceBoundingBoxes(Objects, AllObjects, bUseVelocity, Dt, MWorldSpaceBoxes);
		TBox<T, d> GlobalBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[0], MWorldSpaceBoxes);
		T Dx = TVector<T, d>::DotProduct(GlobalBox.Extents(), TVector<T, d>(1)) / d;
		for (int32 i = 1; i < AllObjects.Num(); ++i)
		{
			const auto& WorldBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[i], MWorldSpaceBoxes);
			Dx += TVector<T, d>::DotProduct(WorldBox.Extents(), TVector<T, d>(1)) / d;
			GlobalBox.GrowToInclude(WorldBox);
		}
		Dx /= AllObjects.Num();
		if (FBoundingVolumeCVars::FilterFarBodies)
		{
			TArray<int32> ObjectsToRemove;
			for (int32 i = 1; i < AllObjects.Num(); ++i)
			{
				const auto& WorldBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[i], MWorldSpaceBoxes);
				const TVector<T, d> MinToDXRatio = WorldBox.Min() / Dx;
				for (int32 Axis = 0; Axis < d; ++Axis)
				{
					if (FMath::Abs(MinToDXRatio[Axis]) > 1e7)
					{
						ObjectsToRemove.Add(i);
						MGlobalObjects.Add(AllObjects[i]);
						break;
					}
				}
			}
			if (ObjectsToRemove.Num())
			{
				for (int32 i = ObjectsToRemove.Num() - 1; i >= 0; --i)
				{
					const_cast<TArray<int32>&>(AllObjects).RemoveAtSwap(ObjectsToRemove[i]);
				}
				GlobalBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[0], MWorldSpaceBoxes);
				Dx = TVector<T, d>::DotProduct(GlobalBox.Extents(), TVector<T, d>(1)) / d;
				for (int32 i = 1; i < AllObjects.Num(); ++i)
				{
					const auto& WorldBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[i], MWorldSpaceBoxes);
					Dx += TVector<T, d>::DotProduct(WorldBox.Extents(), TVector<T, d>(1)) / d;
					GlobalBox.GrowToInclude(WorldBox);
				}
				Dx /= AllObjects.Num();
			}
		}
		TVector<int32, d> Cells = Dx > 0 ? GlobalBox.Extents() / Dx : TVector<int32, d>(MaxCells);
		Cells += TVector<int32, d>(1);
		for (int32 Axis = 0; Axis < d; ++Axis)
		{
			if (Cells[Axis] > MaxCells)
				Cells[Axis] = MaxCells;
			if (!(ensure(Cells[Axis] >=0 )))	//seeing this because GlobalBox is huge leading to int overflow. Need to investigate why bounds get so big
			{
				Cells[Axis] = MaxCells;
			}
		}
		MGrid = TUniformGrid<T, d>(GlobalBox.Min(), GlobalBox.Max(), Cells);
		MElements = TArrayND<TArray<int32>, d>(MGrid);
		for (int32 i = 0; i < AllObjects.Num(); ++i)
		{
			const auto& ObjectBox = Chaos::GetWorldSpaceBoundingBox(Objects, AllObjects[i], MWorldSpaceBoxes);
			const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
			const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
			for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
			{
				for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
				{
					for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
					{
						MElements(x, y, z).Add(AllObjects[i]);
					}
				}
			}
		}
		T NumObjects = 0;
		for (int32 i = 0; i < MGrid.GetNumCells(); ++i)
		{
			NumObjects += MElements(MGrid.GetIndex(i)).Num();
		}
		NumObjects /= AllObjects.Num();
		UE_LOG(LogChaos, Verbose, TEXT("Generated Tree with (%d, %d, %d) Nodes and %f Per Cell"), MGrid.Counts()[0], MGrid.Counts()[1], MGrid.Counts()[2], NumObjects);
	}

	void RemoveElements(const TArray<uint32>& RemovedIndices)
	{
		for (const auto& Index : RemovedIndices)
		{
			if (!HasBoundingBox(*MObjects, Index))
			{
				MGlobalObjects.RemoveSwap(Index);
			}
			else
			{
				if (MWorldSpaceBoxes.Contains(Index))
				{
					const auto& ObjectBox = Chaos::GetWorldSpaceBoundingBox(*MObjects, Index, MWorldSpaceBoxes);
					const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
					const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
					for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
					{
						for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
						{
							for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
							{
								MElements(x, y, z).RemoveSwap(Index);
							}
						}
					}
				}
			}
		}
	}

	void AddElements(const TArray<uint32>& AddedIndices)
	{
		if (!AddedIndices.Num())
		{
			return;
		}
		for (const auto& Index : AddedIndices)
		{
			// Compute and store world space box
			MWorldSpaceBoxes.FindOrAdd(Index) = ComputeWorldSpaceBoundingBox(*MObjects, Index);
		}
		// Compute how many cells need to be added on
		{
			TBox<T, d> AddedBox = Chaos::GetWorldSpaceBoundingBox(*MObjects, AddedIndices[0], MWorldSpaceBoxes);
			for (int32 i = 1; i < AddedIndices.Num(); ++i)
			{
				const TBox<T, d>& WorldBox = Chaos::GetWorldSpaceBoundingBox(*MObjects, AddedIndices[i], MWorldSpaceBoxes);
				AddedBox.GrowToInclude(WorldBox);
			}
			TVector<int32, d> NewCells = MGrid.Counts();
			TVector<T, d> NewMinCorner = MGrid.MinCorner();
			TVector<T, d> NewMaxCorner = MGrid.MaxCorner();
			bool bChanged = false;
			for (int32 Axis = 0; Axis < d; ++Axis)
			{
				if (AddedBox.Min()[Axis] < MGrid.MinCorner()[Axis])
				{
					int32 NumNewCells = static_cast<int32>((MGrid.MinCorner()[Axis] - AddedBox.Min()[Axis]) / MGrid.Dx()[Axis]);
					NewCells[Axis] += NumNewCells;
					NewMinCorner[Axis] -= NumNewCells * MGrid.Dx()[Axis];
					bChanged = true;
				}
				if (AddedBox.Max()[Axis] > MGrid.MaxCorner()[Axis])
				{
					int32 NumNewCells = static_cast<int32>((AddedBox.Max()[Axis] - MGrid.MaxCorner()[Axis]) / MGrid.Dx()[Axis]);
					NewCells[Axis] += NumNewCells;
					NewMaxCorner[Axis] += NumNewCells * MGrid.Dx()[Axis];
					bChanged = true;
				}
			}
			if (bChanged)
			{
				TUniformGrid<T, d> NewGrid(NewMinCorner, NewMaxCorner, NewCells);
				TArrayND<TArray<int32>, d> NewElements(NewGrid);
				for (int32 i = 0; i < MGrid.GetNumCells(); ++i)
				{
					TVector<int32, d> GridIndex = NewGrid.Cell(MGrid.Center(i));
					NewElements(GridIndex) = MElements[i];
				}
				MGrid = NewGrid;
				MElements = std::move(NewElements);
			}
		}
		for (const auto& Index : AddedIndices)
		{
			if (!HasBoundingBox(*MObjects, Index))
			{
				MGlobalObjects.Add(Index);
			}
			else
			{
				// Add elem
				const auto& ObjectBox = Chaos::GetWorldSpaceBoundingBox(*MObjects, Index, MWorldSpaceBoxes);
				const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
				const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
				for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
				{
					for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
					{
						for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
						{
							MElements(x, y, z).Add(Index);
						}
					}
				}
			}
		}
	}

	template<class T_INTERSECTION>
	TArray<int32> FindAllIntersectionsImp(const T_INTERSECTION& Intersection) const
	{
		if (!MObjects)
		{
			return TArray<int32>();
		}
		if (bIsEmpty)
		{
			return TArray<int32>(MGlobalObjects);
		}
		TArray<int32> IntersectionList = FindAllIntersectionsHelper(Intersection);
		IntersectionList.Append(MGlobalObjects);
		return IntersectionList;
	}

	// Begin ISpatialAcceleration interface
	virtual TArray<int32> FindAllIntersections(const TBox<T, d>& Box) const override { return FindAllIntersectionsImp(Box); }
	virtual TArray<int32> FindAllIntersections(const TSpatialRay<T, d>& Ray) const override { return FindAllIntersectionsImp(Ray); }
	virtual TArray<int32> FindAllIntersections(const TVector<T, d>& Point) const override { return FindAllIntersectionsImp(Point); }
	virtual TArray<int32> FindAllIntersections(const TGeometryParticles<T, d>& InParticles, const int32 i) const override
	{
		return FindAllIntersectionsImp(Chaos::GetWorldSpaceBoundingBox(InParticles, i, MWorldSpaceBoxes));
	}
	// End ISpatialAcceleration interface

	template <bool bPruneDuplicates = true>
	struct TSpatialRayIterator
	{
		TSpatialRayIterator(const TVector<T, d>& InStart, const TVector<T, d>& InDir, const T Length, const TBoundingVolume<OBJECT_ARRAY, T, d>& InBV)
			: BV(InBV)
			, Start(InStart)
			, Dir(InDir)
			, OriginalLength(Length)
		{
			TBox<T, d> GlobalBounds(BV.GetGrid().MinCorner(), BV.GetGrid().MaxCorner());

			T TOI;

			InvLength = 1 / OriginalLength;
			for (int Axis = 0; Axis < d; ++Axis)
			{
				bParallel[Axis] = Dir[Axis] == 0;
				InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
			}

			bCellsLeft = GlobalBounds.RaycastFast(Start, Dir, InvDir, bParallel, Length, InvLength, TOI, NextStart);
			if (bCellsLeft)
			{
				CellIdx = BV.GetGrid().Cell(NextStart);
				CellIdx = BV.GetGrid().ClampIndex(CellIdx);	//raycast may have ended slightly outside of grid
				End = Start + InDir * Length;
			}
		}

		TArray<int32> GetNextIntersections()
		{
			TArray<int32> Results;
			if (bCellsLeft)
			{
				do
				{
					//gather all instances in current cell whose bounds intersect with ray
					const TArray<int32>& Instances = BV.GetElements()(CellIdx);
					Results.Reserve(Instances.Num());
					TVector<T, d> TmpPosition;
					T TOI;

					for (int32 Instance : Instances)
					{
						if (bPruneDuplicates)
						{
							if (InstancesSeen.Contains(Instance))
							{
								continue;
							}
							InstancesSeen.Add(Instance);
						}
						const TBox<T, d>& InstanceBounds = BV.GetWorldSpaceBoxes()[Instance];
						if (InstanceBounds.RaycastFast(Start, Dir, InvDir, bParallel, OriginalLength, InvLength, TOI, TmpPosition))
						{
							Results.Add(Instance);
						}
					}

					//find next cell

					//We want to know which plane we used to cross into next cell
					const TVector<T, d> CellCenter = BV.GetGrid().Location(CellIdx);
					const TVector<T, d>& Dx = BV.GetGrid().Dx();

					T Times[3];
					T BestTime = TNumericLimits<T>::Max();
					for (int Axis = 0; Axis < d; ++Axis)
					{
						if (!bParallel[Axis])
						{
							const T CrossPoint = Dir[Axis] > 0 ? CellCenter[Axis] + Dx[Axis] / 2 : CellCenter[Axis] - Dx[Axis] / 2;
							const T Distance = CrossPoint - NextStart[Axis];	//note: CellCenter already has /2, we probably want to use the corner instead
							const T Time = Distance * InvDir[Axis];
							Times[Axis] = Time;
							if (Time < BestTime)
							{
								BestTime = Time;
							}
						}
						else
						{
							Times[Axis] = TNumericLimits<T>::Max();
						}
					}

					for (int Axis = 0; Axis < d; ++Axis)
					{
						constexpr T Epsilon = 1e-2;	//if raycast is slightly off we still count it as hitting the cell surface
						CellIdx[Axis] += (Times[Axis] <= BestTime + Epsilon) ? (Dir[Axis] > 0 ? 1 : -1) : 0;
						if (CellIdx[Axis] < 0 || CellIdx[Axis] >= BV.GetGrid().Counts()[Axis])
						{
							bCellsLeft = false;
							break;
						}
					}


					NextStart = NextStart + Dir * BestTime;
				} while (Results.Num() == 0 && bCellsLeft);	//if results are empty keep traveling
			}

			return Results;
		}

	private:
		TSet<int32> InstancesSeen;
		const TBoundingVolume<OBJECT_ARRAY, T, d>& BV;
		const TVector<T, d> Start;
		const TVector<T, d> Dir;
		const T OriginalLength;
		T InvLength;
		TVector<T, d> End;
		TVector<T, d> NextStart;
		TVector<int32, d> CellIdx;
		TVector<T, d> InvDir;
		bool bParallel[d];
		bool bCellsLeft;
	};

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<T>& Visitor) const override
	{
		TSpatialVisitor<T> ProxyVisitor(Visitor);
		Raycast(Start, Dir, OriginalLength, ProxyVisitor);
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	void Raycast(const TVector<T,d>& Start, const TVector<T, d>& Dir, const T OriginalLength, SQVisitor& Visitor) const
	{
		TBox<T, d> GlobalBounds(MGrid.MinCorner(), MGrid.MaxCorner());
		bool bParallel[d];
		TVector<T, d> InvDir;

		const T InvOriginalLength = 1 / OriginalLength;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		T TOI;
		TVector<T, d> NextStart;
		TVector<int32, d> CellIdx;
		TSet<int32> InstancesSeen;
		bool bCellsLeft = GlobalBounds.RaycastFast(Start, Dir, InvDir, bParallel, OriginalLength, InvOriginalLength, TOI, NextStart);
		if (bCellsLeft)
		{
			CellIdx = MGrid.Cell(NextStart);
			CellIdx = MGrid.ClampIndex(CellIdx);	//raycast may have ended slightly outside of grid
			T CurrentLength = OriginalLength;
			T InvCurrentLength = InvOriginalLength;

			do
			{
				//gather all instances in current cell whose bounds intersect with ray
				const TArray<int32>& Instances = MElements(CellIdx);
				//should we let callback know about max potential?
				TVector<T, d> TmpPosition;

				for (int32 Instance : Instances)
				{
					if (bPruneDuplicates)
					{
						if (InstancesSeen.Contains(Instance))
						{
							continue;
						}
						InstancesSeen.Add(Instance);
					}
					const TBox<T, d>& InstanceBounds = MWorldSpaceBoxes[Instance];
					if (InstanceBounds.RaycastFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, TmpPosition))
					{
						const bool bContinue = Visitor.VisitRaycast(Instance, CurrentLength);
						if (!bContinue)
						{
							return;
						}
						InvCurrentLength = 1 / CurrentLength;
					}
				}

				//find next cell

				//We want to know which plane we used to cross into next cell
				const TVector<T, d> CellCenter = MGrid.Location(CellIdx);
				const TVector<T, d>& Dx = MGrid.Dx();

				T Times[3];
				T BestTime = CurrentLength;
				bool bTerminate = true;
				for (int Axis = 0; Axis < d; ++Axis)
				{
					if (!bParallel[Axis])
					{
						const T CrossPoint = Dir[Axis] > 0 ? CellCenter[Axis] + Dx[Axis] / 2 : CellCenter[Axis] - Dx[Axis] / 2;
						const T Distance = CrossPoint - NextStart[Axis];	//note: CellCenter already has /2, we probably want to use the corner instead
						const T Time = Distance * InvDir[Axis];
						Times[Axis] = Time;
						if (Time < BestTime)
						{
							bTerminate = false;	//found at least one plane to pass through
							BestTime = Time;
						}
					}
					else
					{
						Times[Axis] = TNumericLimits<T>::Max();
					}
				}

				if (bTerminate)
				{
					return;
				}

				for (int Axis = 0; Axis < d; ++Axis)
				{
					constexpr T Epsilon = 1e-2;	//if raycast is slightly off we still count it as hitting the cell surface
					CellIdx[Axis] += (Times[Axis] <= BestTime + Epsilon) ? (Dir[Axis] > 0 ? 1 : -1) : 0;
					if (CellIdx[Axis] < 0 || CellIdx[Axis] >= MGrid.Counts()[Axis])
					{
						return;
					}
				}

				NextStart = NextStart + Dir * BestTime;
			} while (true);
		}
	}

	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<T>& Visitor, const TVector<T, d>& Scale = TVector<T, d>(1)) const override
	{
		TSpatialVisitor<T> ProxyVisitor(Visitor);
		Sweep(Start, Dir, OriginalLength, QueryHalfExtents, ProxyVisitor, Scale);
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, SQVisitor& Visitor, const TVector<T,d>& Scale = TVector<T,d>(1)) const
	{
		const TVector<T, d> ScaledMin = MGrid.MinCorner() * Scale;
		const TVector<T, d> ScaledMax = MGrid.MaxCorner() * Scale;

		TUniformGrid<T, d> ScaledGrid(ScaledMin, ScaledMax, MGrid.Counts(), 0);
		TBox<T, d> GlobalBounds(ScaledMin - QueryHalfExtents, ScaledMax + QueryHalfExtents);
		bool bParallel[d];
		TVector<T, d> InvDir;

		const T InvOriginalLength = 1 / OriginalLength;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}


		struct FCellIntersection
		{
			TVector<int32, d> CellIdx;
			T TOI;
		};

		T TOI;
		TVector<T, d> HitPoint;
		TSet<int32> InstancesSeen;
		TSet<TVector<int32,d>> IdxsSeen;
		const bool bInitialHit = GlobalBounds.RaycastFast(Start, Dir, InvDir, bParallel, OriginalLength, InvOriginalLength, TOI, HitPoint);
		if (bInitialHit)
		{
			//Flood fill from inflated cell so that we get all cells along the ray
			TVector<int32, d> HitCellIdx = ScaledGrid.Cell(HitPoint);
			HitCellIdx = ScaledGrid.ClampIndex(HitCellIdx);	//inflation means we likely are outside grid, just get closest cell
			T CurrentLength = OriginalLength;
			T InvCurrentLength = InvOriginalLength;

			TArray<FCellIntersection> IdxsQueue;	//cells we need to visit
			IdxsQueue.Add({ HitCellIdx, TOI });

			int32 QueueIdx = 0;	//FIFO because early cells are more likely to block later cells we can skip
			while(QueueIdx < IdxsQueue.Num())
			{
				const FCellIntersection CellIntersection = IdxsQueue[QueueIdx++];
				if (CellIntersection.TOI > CurrentLength)
				{
					continue;
				}

				//ray still visiting this cell so check all neighbors
				check(d == 3);
				static const TVector<int32, 3> Neighbors[] =
				{
					//grid on z=-1 plane
					{-1, -1, -1}, {0, -1, -1}, {1, -1, -1},
					{-1, 0, -1}, {0, 0, -1}, {1, 0, -1},
					{-1, 1, -1}, {0, 1, -1}, {1, 1, -1},

					//grid on z=0 plane
					{-1, -1, 0}, {0, -1, 0}, {1, -1, 0},
					{-1, 0, 0},			 {1, 0, 0},
					{-1, 1, 0}, {0, 1, 0}, {1, 0, 0},

					//grid on z=1 plane
					{-1, -1, 1}, {0, -1, 1}, {1, -1, 1},
					{-1, 0, 1}, {0, 0, 1}, {1, 0, 1},
					{-1, 1, 1}, {0, 1, 1}, {1, 1, 1}
				};

				for (const TVector<int32, 3>& Neighbor : Neighbors)
				{
					const TVector<int32, 3> NeighborIdx = Neighbor + CellIntersection.CellIdx;
					bool bSkip = false;
					for (int32 Axis = 0; Axis < d; ++Axis)
					{
						if (NeighborIdx[Axis] < 0 || NeighborIdx[Axis] >= ScaledGrid.Counts()[Axis])
						{
							bSkip = true;
							break;
						}
					}
					if (!bSkip && !IdxsSeen.Contains(NeighborIdx))
					{
						IdxsSeen.Add(NeighborIdx);

						const TVector<T, d> NeighborCenter = ScaledGrid.Location(NeighborIdx);
						const TBox<T, d> InflatedNeighbor(NeighborCenter - QueryHalfExtents - ScaledGrid.Dx(), NeighborCenter + QueryHalfExtents + ScaledGrid.Dx());
						if (InflatedNeighbor.RaycastFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, HitPoint))
						{
							IdxsQueue.Add({ NeighborIdx, TOI });	//should we sort by TOI?
						}
					}
				}

				//check if any instances in the cell are hit
				const TArray<int32>& Instances = MElements(CellIntersection.CellIdx);
				for (int32 Instance : Instances)
				{
					if (bPruneDuplicates)
					{
						if (InstancesSeen.Contains(Instance))
						{
							continue;
						}
						InstancesSeen.Add(Instance);
					}
						
					const TBox<T,d>& InstanceBounds = MWorldSpaceBoxes[Instance];
					const TBox<T, d> InflatedScaledInstanceBounds(InstanceBounds.Min() * Scale - QueryHalfExtents, InstanceBounds.Max() * Scale + QueryHalfExtents);
					if (InflatedScaledInstanceBounds.RaycastFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, HitPoint))
					{
						const bool bContinue = Visitor.VisitSweep(Instance, CurrentLength);
						if (!bContinue)
						{
							return;
						}
						InvCurrentLength = 1 / CurrentLength;
					}
				}
			}
		}
	}

	void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<T>& Visitor, const TVector<T, d>& Scale = TVector<T, d>(1)) const override
	{
		TSpatialVisitor<T> ProxyVisitor(Visitor);
		return Overlap(QueryBounds, ProxyVisitor, Scale);
	}

	template <typename SQVisitor, bool bPruneDuplicates = true>
	void Overlap(const TBox<T,d>& QueryBounds, SQVisitor& Visitor, const TVector<T, d>& Scale = TVector<T, d>(1)) const
	{
		const TVector<T, d> ScaledMin = MGrid.MinCorner() * Scale;
		const TVector<T, d> ScaledMax = MGrid.MaxCorner() * Scale;

		TUniformGrid<T, d> ScaledGrid(ScaledMin, ScaledMax, MGrid.Counts(), 0);
		TBox<T, d> GlobalBounds(ScaledMin, ScaledMax);
		
		const TVector<int32, d> StartIndex = ScaledGrid.ClampIndex(ScaledGrid.Cell(QueryBounds.Min()));
		const TVector<int32, d> EndIndex = ScaledGrid.ClampIndex(ScaledGrid.Cell(QueryBounds.Max()));
		TSet<int32> InstancesSeen;

		for (int32 X = StartIndex[0]; X <= EndIndex[0]; ++X)
		{
			for (int32 Y = StartIndex[1]; Y <= EndIndex[1]; ++Y)
			{
				for (int32 Z = StartIndex[2]; Z <= EndIndex[2]; ++Z)
				{
					const TArray<int32>& Instances = MElements(X, Y, Z);
					for (int32 Instance : Instances)
					{
						if (bPruneDuplicates)
						{
							if (InstancesSeen.Contains(Instance))
							{
								continue;
							}
							InstancesSeen.Add(Instance);
						}
						const TBox<T, d>& InstanceBounds = MWorldSpaceBoxes[Instance];
						const TBox<T, d> InflatedScaledInstanceBounds(InstanceBounds.Min() * Scale, InstanceBounds.Max() * Scale);
						if (QueryBounds.Intersects(InflatedScaledInstanceBounds))
						{
							if (Visitor.VisitOverlap(Instance) == false)
							{
								return;
							}
						}
					}
				}
			}
		}
	}

	const TArray<int32>& GlobalObjects() const
	{
		return MGlobalObjects;
	}

	const TMap<int32, TBox<T, d>>& GetWorldSpaceBoxes() const
	{
		return MWorldSpaceBoxes;
	}

	const TUniformGrid<T, d>& GetGrid() const
	{
		return MGrid;
	}

	// TODO(mlentine): Need to move this elsewhere; probably on CollisionConstraint
	const TBox<T, d>& GetWorldSpaceBoundingBox(const TGeometryParticles<T, d>& InParticles, const int32 Index)
	{
		return Chaos::GetWorldSpaceBoundingBox(InParticles, Index, MWorldSpaceBoxes);
	}

	const TArrayND<TArray<int32>, d>& GetElements() const 
	{ 
		return MElements; 
	}
	
	void Serialize(FArchive& Ar)
	{
		Ar << MGlobalObjects << MWorldSpaceBoxes << MGrid << MElements << bIsEmpty;
	}

  private:

	//Needed for serialization
	void SetObjects(const OBJECT_ARRAY& Object)
	{
		MObjects = &Object;
	}

	template <typename OBJECT_ARRAY2, typename T2, int d2>
	friend void FixupLeafObj(const OBJECT_ARRAY2& Objects, TArray<TBoundingVolume<OBJECT_ARRAY2, T2, d2>>& Leafs);

	TArray<int32> FindAllIntersectionsHelper(const TVector<T, d>& Point) const
	{
		return MElements(MGrid.Cell(Point));
	}

	TArray<int32> FindAllIntersectionsHelper(const TBox<T, d>& ObjectBox) const
	{
		TArray<int32> Intersections;
		const auto StartIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Min()));
		const auto EndIndex = MGrid.ClampIndex(MGrid.Cell(ObjectBox.Max()));
		for (int32 x = StartIndex[0]; x <= EndIndex[0]; ++x)
		{
			for (int32 y = StartIndex[1]; y <= EndIndex[1]; ++y)
			{
				for (int32 z = StartIndex[2]; z <= EndIndex[2]; ++z)
				{
					const TArray<int32>& LocalList = MElements(x, y, z);
					Intersections.Reserve(Intersections.Num() + LocalList.Num());
					for (int32 Item : LocalList)
					{
						if (ObjectBox.Intersects(MWorldSpaceBoxes[Item]))
						{
							Intersections.Add(Item);
						}
					}
				}
			}
		}


		Intersections.Sort();

		for(int32 i = Intersections.Num() - 1; i > 0; i--)
		{
			if(Intersections[i] == Intersections[i - 1])
			{
				Intersections.RemoveAtSwap(i, 1, false);
			}
		}

		return Intersections;
	}

	TArray<int32> FORCENOINLINE FindAllIntersectionsHelper(const TSpatialRay<T, d>& InRay) const
	{
		TArray<int32> Intersections;

		const FBox GlobalBounds(MGrid.MinCorner(), MGrid.MaxCorner());

		FVector HitLocation;
		FVector HitNormal;
		float HitTime;

		if(FMath::LineExtentBoxIntersection(GlobalBounds, InRay.Start, InRay.End, FVector::ZeroVector, HitLocation, HitNormal, HitTime))
		{
			// We definitely hit the box somewhere along the ray, now we need the other end
			FVector AdjustedEnd = InRay.End;
			if(!FMath::PointBoxIntersection(AdjustedEnd, GlobalBounds))
			{
				// End isn't in the box, need another intersection test, may be a way to intuit this without another box vs ray
				ensure(FMath::LineExtentBoxIntersection(GlobalBounds, InRay.End, HitLocation, FVector::ZeroVector, AdjustedEnd, HitNormal, HitTime));
			}

			// Now we can express the remaining line segment in integer cell coordinates
			const FVector GridMin = MGrid.MinCorner();
			const FVector GridCellExtent = MGrid.Dx();

			TVector<int32, 3> Start;
			Start[0] = FMath::FloorToInt((HitLocation.X - GridMin.X) / GridCellExtent.X);
			Start[1] = FMath::FloorToInt((HitLocation.Y - GridMin.Y) / GridCellExtent.Y);
			Start[2] = FMath::FloorToInt((HitLocation.Z - GridMin.Z) / GridCellExtent.Z);

			TVector<int32, 3> End;
			End[0] = FMath::FloorToInt((AdjustedEnd.X - GridMin.X) / GridCellExtent.X);
			End[1] = FMath::FloorToInt((AdjustedEnd.Y - GridMin.Y) / GridCellExtent.Y);
			End[2] = FMath::FloorToInt((AdjustedEnd.Z - GridMin.Z) / GridCellExtent.Z);

			// Points can end up ever so slightly outside of the grid, so clamp these onto the cell dimensions
			Start = MGrid.ClampIndex(Start);
			End = MGrid.ClampIndex(End);

			// Delta through the grid
			const TVector<int32, 3> IntDelta = End - Start;

			// Unsigned length through the grid (and double for less operations later)
			const TVector<int32, 3> AbsIntDelta(FMath::Abs(IntDelta[0]), FMath::Abs(IntDelta[1]), FMath::Abs(IntDelta[2]));
			const TVector<int32, 3> AbsIntDelta2 = AbsIntDelta * 2;

			// Directions each axis is moving
			const TVector<int32, 3> WalkDirections(FMath::Sign(IntDelta[0]), FMath::Sign(IntDelta[1]), FMath::Sign(IntDelta[2]));

			// Cell iterator
			TVector<int32, 3> CurrPoint = Start;
			TArray<TVector<int32, 3>> CellHits;
			int32 NumHitCellElements = 0;

			// Need to take the overall longest dimension to ensure we hit all the cells along the line, there will be
			// an orientation that never moves in other dimensions more than two cells at once
			int32 WalkDim = INDEX_NONE;
			if(AbsIntDelta[0] >= AbsIntDelta[1] && AbsIntDelta[0] >= AbsIntDelta[2])
			{
				WalkDim = 0;
			}
			else if(AbsIntDelta[1] >= AbsIntDelta[0] && AbsIntDelta[1] >= AbsIntDelta[2])
			{
				WalkDim = 1;
			}
			else
			{
				WalkDim = 2;
			}

			// Indices for the other dimensions
			int32 Dims[2];
			Dims[0] = (WalkDim + 1) % 3;
			Dims[1] = (Dims[0] + 1) % 3;

			// Current dimension errors, tracks when each dimension should move
			int32 Dim0Error = AbsIntDelta2[Dims[0]] - AbsIntDelta[WalkDim];
			int32 Dim1Error = AbsIntDelta2[Dims[1]] - AbsIntDelta[WalkDim];

			const int32 Steps = AbsIntDelta[WalkDim];
			for(int32 WalkDimIndex = 0; WalkDimIndex <= Steps; ++WalkDimIndex)
			{
				// Add to list of core cells
				CellHits.Add(CurrPoint);

				if(Dim0Error > 0)
				{
					CurrPoint[Dims[0]] += WalkDirections[Dims[0]];
					Dim0Error -= AbsIntDelta2[WalkDim];
				}

				if(Dim1Error > 0)
				{
					CurrPoint[Dims[1]] += WalkDirections[Dims[1]];
					Dim1Error -= AbsIntDelta2[WalkDim];
				}

				Dim0Error += AbsIntDelta2[Dims[0]];
				Dim1Error += AbsIntDelta2[Dims[1]];

				// Move the walk dimension up one cell
				CurrPoint[WalkDim] += WalkDirections[WalkDim];
			}
			
			// We know all the cells along the line, need to add them and their neighbor's elements.
			// Note this could be more conservative to only take 4 cells per step instead of 9
			// #BGTODO improve
			TVector<float, 3> GridDxOverTwo = MGrid.Dx() / 2.0f;
			for(const TVector<int32, 3> Cell : CellHits)
			{
				TVector<int32, 3> StartCell = Cell;
				StartCell[Dims[0]] -= 1;
				StartCell[Dims[1]] -= 1;

				for(int32 NeighborIndex0 = 0; NeighborIndex0 < 3; ++NeighborIndex0)
				{					
					for(int32 NeighborIndex1 = 0; NeighborIndex1 < 3; ++NeighborIndex1)
					{
						TVector<int32, 3> NextCell(StartCell);
						NextCell[Dims[0]] += NeighborIndex0;
						NextCell[Dims[1]] += NeighborIndex1;

						if(MGrid.IsValid(NextCell))
						{
							FVector CellLocation = MGrid.Location(NextCell);
							FBox CellBox(CellLocation - GridDxOverTwo, CellLocation + GridDxOverTwo);
							
							if(NeighborIndex0 == 1 && NeighborIndex1 == 1)
							{
								TArray<int32> LocalList = MElements(NextCell[0], NextCell[1], NextCell[2]);
							
								for(int32 Index = LocalList.Num() - 1; Index >= 0; Index--)
								{
									const TBox<float, 3>& GlobalBox = MWorldSpaceBoxes[LocalList[Index]];
									FBox ObjectBox(GlobalBox.Min(), GlobalBox.Max());
									if(!FMath::LineExtentBoxIntersection(ObjectBox, InRay.Start, InRay.End, FVector::ZeroVector, HitLocation, HitNormal, HitTime))
									{
										LocalList.RemoveAtSwap(Index);
									}
								}
							
								Intersections.Append(LocalList);
							}
							else if(FMath::LineExtentBoxIntersection(CellBox, InRay.Start, InRay.End, FVector::ZeroVector, HitLocation, HitNormal, HitTime))
							{
								TArray<int32> LocalList = MElements(NextCell[0], NextCell[1], NextCell[2]);
							
								for(int32 Index = LocalList.Num() - 1; Index >= 0; Index--)
								{
									const TBox<float, 3>& GlobalBox = MWorldSpaceBoxes[LocalList[Index]];
									FBox ObjectBox(GlobalBox.Min(), GlobalBox.Max());
									if(!FMath::LineExtentBoxIntersection(ObjectBox, InRay.Start, InRay.End, FVector::ZeroVector, HitLocation, HitNormal, HitTime))
									{
										LocalList.RemoveAtSwap(Index);
									}
								}
							
								Intersections.Append(LocalList);
							}
						}
					}
				}
			}

			Intersections.Sort();

			for (int32 i = Intersections.Num() - 1; i > 0; i--)
			{
				if (Intersections[i] == Intersections[i - 1])
				{
					Intersections.RemoveAtSwap(i, 1, false);
				}
			}

			// Incomment below and link engine to draw out the first query per frame
			//if(IsInGameThread())
			//{
			//	static int32 FrameNum = INDEX_NONE;
			//
			//	// Draw out queries
			//	UWorld* WorldPtr = nullptr;
			//	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
			//	for(const FWorldContext& Context : WorldContexts)
			//	{
			//		UWorld* TestWorld = Context.World();
			//		if(TestWorld && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			//		{
			//			WorldPtr = TestWorld;
			//		}
			//	}
			//
			//	if(WorldPtr)
			//	{
			//		// Can't debug draw without a valid world
			//		if(GFrameNumber != FrameNum)
			//		{
			//			FVector Last = InRay.Start;
			//			for(TVector<int32, 3>& HitCell : CellHits)
			//			{
			//				FVector CellLoc = MGrid.Location(HitCell);
			//				DrawDebugLine(WorldPtr, Last, CellLoc, FColor::Red, false, -1.0f, SDPG_Foreground, 1.0f);
			//				DrawDebugSphere(WorldPtr, CellLoc, 30.0f, 10, FColor::Green, false, -1.0f, SDPG_Foreground, 1.0f);
			//
			//				Last = CellLoc;
			//			}
			//
			//			FrameNum = GFrameNumber;
			//		}
			//	}
			//}
		}
		return Intersections;
	}

	const OBJECT_ARRAY* MObjects;
	TArray<int32> MGlobalObjects;
	TArray<int32> MAllObjects;
	TMap<int32, TBox<T, d>> MWorldSpaceBoxes;
	TUniformGrid<T, d> MGrid;
	TArrayND<TArray<int32>, d> MElements;
	bool bIsEmpty;
	FCriticalSection CriticalSection;
};

template<class OBJECT_ARRAY, class T, int d>
FArchive& operator<<(FArchive& Ar, TBoundingVolume<OBJECT_ARRAY, T, d>& BoundingVolume)
{
	BoundingVolume.Serialize(Ar);
	return Ar;
}

}
