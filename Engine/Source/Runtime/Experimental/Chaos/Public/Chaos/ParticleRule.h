// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/PBDRigidParticles.h"

namespace Chaos
{
	template<typename T, int d>
	class TParticles;

	template<typename T, int d>
	class TDynamicParticles;

	template<typename T, int d>
	class TPBDParticles;

	template<typename T, int d>
	class TRigidParticles;

	template<typename T, int d>
	class TPBDRigidParticles;

/**
 * Apply an effect to all particles.
 */
template<class T, int d>
class CHAOS_API TParticleRule
{
  public:
	virtual void Apply(TParticles<T, d>& InParticles, const T Dt) const { check(0); }
	virtual void Apply(TDynamicParticles<T, d>& InParticles, const T Dt) const { Apply(static_cast<TParticles<T, d>&>(InParticles), Dt); }
	virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const { Apply(static_cast<TDynamicParticles<T, d>&>(InParticles), Dt); }
};

}
