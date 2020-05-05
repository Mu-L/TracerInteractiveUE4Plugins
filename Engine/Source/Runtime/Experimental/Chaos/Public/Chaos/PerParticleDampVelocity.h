// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Matrix.h"
#include "Chaos/PerParticleRule.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace Chaos
{
	template<class T, int d>
	class TPerParticleDampVelocity : public TPerParticleRule<T, d>
	{
	public:
		TPerParticleDampVelocity(const T Coefficient = 0.01)
		    : MCoefficient(Coefficient)
		{
		}
		virtual ~TPerParticleDampVelocity() {}

		template<class T_PARTICLES>
		inline void UpdatePositionBasedState(const T_PARTICLES& InParticles, const TArray<int32>& InActiveIndices)
		{
			static_assert(d == 3, "Damp Velocities currently only supports 3D vectors.");
			MXcm = TVector<T, d>(0.f, 0.f, 0.f);
			MVcm = TVector<T, d>(0.f, 0.f, 0.f);
			T Mcm = (T)0;
			for (const int32 Index : InActiveIndices)
			{
				if (!InParticles.InvM(Index))
				{
					continue;
				}
				MXcm += InParticles.X(Index) * InParticles.M(Index);
				MVcm += InParticles.V(Index) * InParticles.M(Index);
				Mcm += InParticles.M(Index);
			}

			if (Mcm != 0.0f)
			{
				MXcm /= Mcm;
				MVcm /= Mcm;
			}

			TVector<T, d> L = TVector<T, d>(0.f, 0.f, 0.f);
			PMatrix<T, d, d> I(0);
			for (const int32 Index : InActiveIndices)
			{
				if (!InParticles.InvM(Index))
				{
					continue;
				}
				TVector<T, d> V = InParticles.X(Index) - MXcm;
				L += TVector<T, d>::CrossProduct(V, InParticles.M(Index) * InParticles.V(Index));
				PMatrix<T, d, d> M(0, V[2], -V[1], -V[2], 0, V[0], V[1], -V[0], 0);
				I += M.GetTransposed() * M * InParticles.M(Index);
			}
#if COMPILE_WITHOUT_UNREAL_SUPPORT
			MOmega = I.Determinant() > 1e-7 ? TRigidTransform<T, d>(I).InverseTransformVector(L) : TVector<T, d>(0);
#else
			const T Det = I.Determinant();
			MOmega = Det < SMALL_NUMBER || !FGenericPlatformMath::IsFinite(Det) ?
			    TVector<T, d>(0) :
			    I.InverseTransformVector(L); // Calls FMatrix::InverseFast(), which tests against SMALL_NUMBER
#endif
		}

		template<class T_PARTICLES>
		inline void UpdatePositionBasedState(const T_PARTICLES& InParticles)
		{
			if (ActiveIndices.Num() != InParticles.Size())
			{
				if ((uint32)ActiveIndices.Num() < InParticles.Size())
				{
					uint32 CurrNum = ActiveIndices.Num();
					ActiveIndices.AddUninitialized(InParticles.Size() - CurrNum);
					for (; CurrNum < InParticles.Size(); ++CurrNum)
					{
						ActiveIndices[CurrNum] = CurrNum;
					}
				}
				else if ((uint32)ActiveIndices.Num() > InParticles.Size())
				{
					ActiveIndices.SetNum(InParticles.Size());
				}
			}

			UpdatePositionBasedState(InParticles, ActiveIndices);
		}
		inline void Apply(TDynamicParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
		{
			if (InParticles.InvM(Index) == 0)
			{
				return; // Do not damp kinematic particles
			}
			ApplyHelper(InParticles, Dt, Index);
		}

		inline void Apply(TRigidParticles<T, d>& InParticles, const T Dt, const int32 Index) const override //-V762
		{
			if (InParticles.InvM(Index) == 0)
			{
				return; // Do not damp kinematic rigid bodies
			}
			ApplyHelper(InParticles, Dt, Index);
		}

	protected:
		template<class T_PARTICLES>
		inline void ApplyHelper(T_PARTICLES& InParticles, const T Dt, const int32 Index) const
		{
			TVector<T, d> R = InParticles.X(Index) - MXcm;
			TVector<T, d> Dv = MVcm - InParticles.V(Index) + TVector<T, d>::CrossProduct(R, MOmega);
			InParticles.V(Index) += MCoefficient * Dv;
		}

	protected:
		mutable T MCoefficient;  // The mutable allows to be modified in derived classes Apply const functions

	private:
		TArray<int32> ActiveIndices;
		TVector<T, d> MXcm, MVcm, MOmega;
	};
}
