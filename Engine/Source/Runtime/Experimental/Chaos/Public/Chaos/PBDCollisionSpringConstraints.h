// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Array.h"
#include "Chaos/PBDCollisionSpringConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDConstraintContainer.h"

#include <algorithm>

namespace Chaos
{
template<class T, int d>
class TPBDCollisionSpringConstraints : public PBDCollisionSpringConstraintsBase<T, d>, public FPBDConstraintContainer
{
	typedef PBDCollisionSpringConstraintsBase<T, d> Base;
	using Base::MBarys;
	using Base::MConstraints;

  public:
	TPBDCollisionSpringConstraints(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 3>>& Elements, const TSet<TVector<int32, 2>>& DisabledCollisionElements, const T Dt, const T Height = (T)0, const T Stiffness = (T)1)
	    : Base(InParticles, Elements, DisabledCollisionElements, Dt, Height, Stiffness) {}
	virtual ~TPBDCollisionSpringConstraints() {}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndex) const
	{
		const int32 i = InConstraintIndex;
		const auto& Constraint = MConstraints[i];
		int32 i1 = Constraint[0];
		int32 i2 = Constraint[1];
		int32 i3 = Constraint[2];
		int32 i4 = Constraint[3];
		auto Delta = Base::GetDelta(InParticles, i);
		// TODO(mlentine): Figure out what the best multipilier here is
		T Multiplier = 1;
		if (InParticles.InvM(i1) > 0)
		{
			InParticles.P(i1) -= Multiplier * InParticles.InvM(i1) * Delta;
		}
		if (InParticles.InvM(i2))
		{
			InParticles.P(i2) += Multiplier * InParticles.InvM(i2) * MBarys[i][0] * Delta;
		}
		if (InParticles.InvM(i3))
		{
			InParticles.P(i3) += Multiplier * InParticles.InvM(i3) * MBarys[i][1] * Delta;
		}
		if (InParticles.InvM(i4))
		{
			InParticles.P(i4) += Multiplier * InParticles.InvM(i4) * MBarys[i][2] * Delta;
		}
	}


	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
	{
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

};
}
#endif
