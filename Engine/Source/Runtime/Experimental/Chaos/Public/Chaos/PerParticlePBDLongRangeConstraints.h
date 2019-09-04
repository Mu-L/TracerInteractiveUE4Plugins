// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"

namespace Chaos
{
template<class T, int d>
class TPerParticlePBDLongRangeConstraints : public TPerParticleRule<T, d>, public TPBDLongRangeConstraintsBase<T, d>
{
	typedef TPBDLongRangeConstraintsBase<T, d> Base;
	using Base::MConstraints;

  public:
	TPerParticlePBDLongRangeConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments = 1, const T Stiffness = (T)1)
	    : TPBDLongRangeConstraintsBase<T, d>(InParticles, PointToNeighbors, NumberOfAttachments, Stiffness)
	{
		MParticleToConstraints.SetNum(InParticles.Size());
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			const auto& Constraint = MConstraints[i];
			int32 i2 = Constraint[Constraint.Num() - 1];
			MParticleToConstraints[i2].Add(i);
		}
	}
	virtual ~TPerParticlePBDLongRangeConstraints() {}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
	{
		for (int32 i = 0; i < MParticleToConstraints[Index].Num(); ++i)
		{
			const auto CIndex = MParticleToConstraints[Index][i];
			const auto& Constraint = MConstraints[CIndex];
			check(Index == Constraint[Constraint.Num() - 1]);
			check(InParticles.InvM(Index) > 0);
			InParticles.P(Index) += Base::GetDelta(InParticles, CIndex);
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override //-V762
	{
		PhysicsParallelFor(MParticleToConstraints.Num(), [&](int32 Index) {
			Apply(InParticles, Dt, Index);
		});
	}

  private:
	TArray<TArray<int32>> MParticleToConstraints;
};
}
