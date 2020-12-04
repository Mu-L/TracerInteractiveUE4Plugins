// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDLongRangeConstraintsBase
{
public:
	enum class EMode : uint8
	{
		FastTetherFastLength,
		AccurateTetherFastLength,
		AccurateTetherAccurateLength,
	};

	TPBDLongRangeConstraintsBase(
		const TDynamicParticles<T, d>& InParticles,
		const TMap<int32, TSet<uint32>>& PointToNeighbors,
		const int32 NumberOfAttachments = 1,
		const T Stiffness = (T)1,
		const T LimitScale = (T)1,
		const EMode Mode = EMode::AccurateTetherFastLength);

	virtual ~TPBDLongRangeConstraintsBase() {}

	EMode GetMode() const { return MMode; }

	const TArray<TVector<uint32, 2>>& GetEuclideanConstraints() const { return MEuclideanConstraints; }
	const TArray<TArray<uint32>>& GetGeodesicConstraints() const { return MGeodesicConstraints; }

	const TArray<T>& GetDists() const { return MDists; }

	static TArray<TArray<uint32>> ComputeIslands(const TMap<int32, TSet<uint32>>& PointToNeighbors, const TArray<uint32>& KinematicParticles);

protected:
	template<class TConstraintType>
	inline TVector<T, d> GetDelta(const TConstraintType& Constraint, const TPBDParticles<T, d>& InParticles, const T RefDist) const
	{
		checkSlow(Constraint.Num() > 1);
		const uint32 i2 = Constraint[Constraint.Num() - 1];
		const uint32 i2m1 = Constraint[Constraint.Num() - 2];
		checkSlow(InParticles.InvM(Constraint[0]) == (T)0.);
		checkSlow(InParticles.InvM(i2) > (T)0.);
		const T Distance = ComputeGeodesicDistance(InParticles, Constraint); // This function is used for either Euclidean or Geodesic distances
		if (Distance < RefDist)
		{
			return TVector<T, d>((T)0.);
		}

		//const TVector<T, d> Direction = (InParticles.P(i2m1) - InParticles.P(i2)).GetSafeNormal();
		TVector<T, d> Direction = InParticles.P(i2m1) - InParticles.P(i2);
		const T DirLen = Direction.SafeNormalize();

		const T Offset = Distance - RefDist;
		const TVector<T, d> Delta = MStiffness * Offset * Direction;

	/*  // ryan - this currently fails:

		const T NewDirLen = (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		//T Correction = (InParticles.P(i2) - InParticles.P(i2m1)).Size() - (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		const T Correction = DirLen - NewDirLen;
		check(Correction >= 0);

		//T NewDist = (Distance - (InParticles.P(i2) - InParticles.P(i2m1)).Size() + (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size());
		const T NewDist = Distance - DirLen + NewDirLen;
		check(FGenericPlatformMath::Abs(NewDist - RefDist) < 1e-4);
	*/
		return Delta;
	};

	static T ComputeGeodesicDistance(const TParticles<T, d>& InParticles, const TArray<uint32>& Path)
	{
		T distance = 0;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.X(Path[i]) - InParticles.X(Path[i + 1])).Size();
		}
		return distance;
	}
	static T ComputeGeodesicDistance(const TPBDParticles<T, d>& InParticles, const TArray<uint32>& Path)
	{
		T distance = 0;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.P(Path[i]) - InParticles.P(Path[i + 1])).Size();
		}
		return distance;
	}

	void ComputeEuclideanConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments);
	void ComputeGeodesicConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments);

	static T ComputeDistance(const TParticles<T, d>& InParticles, const uint32 i, const uint32 j) { return (InParticles.X(i) - InParticles.X(j)).Size(); }
	static T ComputeDistance(const TPBDParticles<T, d>& InParticles, const uint32 i, const uint32 j) { return (InParticles.P(i) - InParticles.P(j)).Size(); }

	static T ComputeGeodesicDistance(const TParticles<T, d>& InParticles, const TVector<uint32, 2>& Path)
	{
		return (InParticles.X(Path[0]) - InParticles.X(Path[1])).Size();
	}
	static T ComputeGeodesicDistance(const TPBDParticles<T, d>& InParticles, const TVector<uint32, 2>& Path)
	{
		return (InParticles.P(Path[0]) - InParticles.P(Path[1])).Size();
	}

protected:
	TArray<TVector<uint32, 2>> MEuclideanConstraints;
	TArray<TArray<uint32>> MGeodesicConstraints;
	TArray<T> MDists;
	T MStiffness;
	EMode MMode;
};
}
