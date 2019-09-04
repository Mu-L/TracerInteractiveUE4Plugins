// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"

namespace Chaos
{
	template<typename T, int d>
	struct TSphereSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<TVector<T, 3>>& Points, const class TSphere<T, 3>& Sphere, const int32 NumPoints)
		{
			check(false);
		}
	};

	template<class T, int d>
	class TSphere final : public TImplicitObject<T, d>
	{
	public:
		IMPLICIT_OBJECT_SERIALIZER(TSphere)

		TSphere(const TVector<T, d>& Center, const T Radius)
		    : TImplicitObject<T, d>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Sphere)
		    , MCenter(Center)
		    , MRadius(Radius)
		    , MLocalBoundingBox(MCenter - MRadius, MCenter + MRadius)
		{
		}

		TSphere(const TSphere<T, d>& Other)
		    : TImplicitObject<T, d>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Sphere)
		    , MCenter(Other.MCenter)
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(Other.MLocalBoundingBox)
		{
		}

		TSphere(TSphere<T, d>&& Other)
		    : TImplicitObject<T, d>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Sphere)
		    , MCenter(MoveTemp(Other.MCenter))
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
		{
		}

		TSphere& operator=(TSphere<T, d>&& InSteal)
		{
			this->Type = InSteal.Type;
			this->bIsConvex = InSteal.bIsConvex;
			this->bIgnoreAnalyticCollisions = InSteal.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = InSteal.bHasBoundingBox;
			MCenter = MoveTemp(InSteal.MCenter);
			MRadius = InSteal.MRadius;
			MLocalBoundingBox = MoveTemp(InSteal.MLocalBoundingBox);

			return *this;
		}

		virtual ~TSphere() {}

		static ImplicitObjectType GetType() { return ImplicitObjectType::Sphere; }

		virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
		{
			Normal = x - MCenter;
			return Normal.SafeNormalize() - MRadius;
		}

		bool Intersects(const TSphere<T, d>& Other) const
		{
			T CenterDistance = FVector::DistSquared(Other.Center(), Center());
			T RadialSum = Other.Radius() + Radius();
			return RadialSum >= CenterDistance;
		}

		TVector<T, d> FindClosestPoint(const TVector<T, d>& StartPoint, const T Thickness = (T)0) const
		{
			TVector<T, 3> Result = MCenter + (StartPoint - MCenter).GetSafeNormal() * (MRadius + Thickness);
			return Result;
		}

		virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal) const override
		{
			ensure(FMath::IsNearlyEqual(Dir.SizeSquared(),1, KINDA_SMALL_NUMBER));
			ensure(Length > 0);

			const T EffectiveRadius = Thickness + MRadius;
			const T EffectiveRadius2 = EffectiveRadius * EffectiveRadius;
			const TVector<T, d> Offset = MCenter - StartPoint;
			const T OffsetSize2 = Offset.SizeSquared();
			if (OffsetSize2 < EffectiveRadius2)
			{
				//initial overlap
				OutTime = 0;	//no position or normal since initial overlap
				return true;
			}

			//(MCenter-X) \dot (MCenter-X) = EffectiveRadius^2
			//Let X be on ray, then (MCenter - StartPoint - t Dir) \dot (MCenter - StartPoint - t Dir) = EffectiveRadius^2
			//Let Offset = (MCenter - StartPoint), then reduces to quadratic: t^2 - 2t*(Offset \dot Dir) + Offset^2 - EffectiveRadius^2 = 0
			//const T A = 1;
			const T HalfB = -TVector<T, d>::DotProduct(Offset, Dir);
			const T C = OffsetSize2 - EffectiveRadius2;
			//time = (-b +- sqrt(b^2 - 4ac)) / 2a
			//2 from the B cancels because of 2a and 4ac
			const T QuarterUnderRoot = HalfB * HalfB - C;
			if (QuarterUnderRoot < 0)
			{
				return false;
			}

			constexpr T Epsilon = 1e-4;
			//we early out if starting in sphere, so using first time is always acceptable
			T FirstTime = QuarterUnderRoot < Epsilon ? -HalfB : -HalfB - FMath::Sqrt(QuarterUnderRoot);
			if (FirstTime >= 0 && FirstTime <= Length)
			{
				const TVector<T, d> FinalSpherePosition = StartPoint + FirstTime * Dir;
				const TVector<T, d> FinalNormal = (FinalSpherePosition - MCenter) / EffectiveRadius;
				const TVector<T, d> IntersectionPosition = FinalSpherePosition - FinalNormal * Thickness;
				
				OutTime = FirstTime / Length;
				OutPosition = IntersectionPosition;
				OutNormal = FinalNormal;
				return true;
			}

			return false;
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			TVector<T, d> Direction = EndPoint - StartPoint;
			T Length = Direction.Size();
			Direction = Direction.GetSafeNormal();
			TVector<T, d> SphereToStart = StartPoint - MCenter;
			T DistanceProjected = TVector<T, d>::DotProduct(Direction, SphereToStart);
			T EffectiveRadius = MRadius + Thickness;
			T UnderRoot = DistanceProjected * DistanceProjected - SphereToStart.SizeSquared() + EffectiveRadius * EffectiveRadius;
			if (UnderRoot < 0)
			{
				return MakePair(TVector<T, d>(0), false);
			}
			if (UnderRoot == 0)
			{
				if (-DistanceProjected < 0 || -DistanceProjected > Length)
				{
					return MakePair(TVector<T, d>(0), false);
				}
				return MakePair(TVector<T, d>(-DistanceProjected * Direction + StartPoint), true);
			}
			T Root1 = -DistanceProjected + sqrt(UnderRoot);
			T Root2 = -DistanceProjected - sqrt(UnderRoot);
			if (Root1 < 0 || Root1 > Length)
			{
				if (Root2 < 0 || Root2 > Length)
				{
					return MakePair(TVector<T, d>(0), false);
				}
				return MakePair(TVector<T, d>(Root2 * Direction + StartPoint), true);
			}
			if (Root2 < 0 || Root2 > Length)
			{
				return MakePair(TVector<T, d>(Root1 * Direction + StartPoint), true);
			}
			if (Root1 < Root2)
			{
				return MakePair(TVector<T, d>(Root1 * Direction + StartPoint), true);
			}
			return MakePair(TVector<T, d>(Root2 * Direction + StartPoint), true);
		}

		virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const override
		{
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = Direction.SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
			{
				return MCenter;
			}
			const TVector<T,d> Normalized = Direction / sqrt(SizeSqr);

			return MCenter + Normalized * (MRadius + Thickness);
		}

		virtual const TBox<T, d>& BoundingBox() const { return MLocalBoundingBox; }

		T GetArea() const { return GetArea(MRadius); }
		static T GetArea(const T Radius)
		{
			static const T FourPI = PI * 4;
			static const T TwoPI = PI * 2;
			return d == 3 ? FourPI * Radius * Radius : TwoPI * Radius;
		}

		T GetVolume() const { return GetVolume(MRadius); }
		static T GetVolume(const T Radius)
		{
			check(d == 3);
			static const T FourThirdsPI = 4. / 3 * PI;
			return FourThirdsPI * Radius * Radius * Radius;
		}

		const TVector<T, d>& Center() const { return MCenter; }
		const TVector<T, d>& GetCenterOfMass() const { return Center(); }
		T Radius() const { return MRadius; }

		virtual FString ToString() const
		{
			return FString::Printf(TEXT("TSphere Center:%s, Radius:%f"), *Center().ToString(), Radius());
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			TImplicitObject<T, d>::SerializeImp(Ar);
			Ar << MCenter << MRadius;
			if (Ar.IsLoading())
			{
				MLocalBoundingBox = TBox<T, d>(MCenter - MRadius, MCenter + MRadius);
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override { SerializeImp(Ar); }
		virtual void Serialize(FArchive& Ar) override { SerializeImp(Ar); }

		/**
		 * Returns sample points centered about the origin.
		 */
		TArray<TVector<T, d>> ComputeLocalSamplePoints(const int NumPoints) const
		{
			TArray<TVector<T, d>> Points;
			TSphere<T, d> LocalSphere(TVector<T, d>(0.0), MRadius);
			TSphereSpecializeSamplingHelper<T, d>::ComputeSamplePoints(Points, LocalSphere, NumPoints);
			return Points;
		}
		TArray<TVector<T, d>> ComputeLocalSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		/**
		 * Returns sample points at the current location of the sphere.
		 */
		TArray<TVector<T, d>> ComputeSamplePoints(const int NumPoints) const
		{
			TArray<TVector<T, d>> Points;
			TSphereSpecializeSamplingHelper<T, d>::ComputeSamplePoints(Points, *this, NumPoints);
			return Points;
		}
		TArray<TVector<T, d>> ComputeSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		PMatrix<T, d, d> GetInertiaTensor(const T Mass, const bool ThinShell = false) const { return GetInertiaTensor(Mass, MRadius, ThinShell); }
		static PMatrix<T, d, d> GetInertiaTensor(const T Mass, const T Radius, const bool ThinShell = false)
		{
			static const T TwoThirds = 2. / 3;
			static const T TwoFifths = 2. / 5;
			const T Diagonal = ThinShell ? TwoThirds * Mass * Radius * Radius : TwoFifths * Mass * Radius * Radius;
			return PMatrix<T, d, d>(Diagonal, Diagonal, Diagonal);
		}

		static TRotation<T, d> GetRotationOfMass()
		{ return TRotation<T, d>(TVector<T, d>(0), 1); }

	private:
		TVector<T, d> MCenter;
		T MRadius;
		TBox<T, d> MLocalBoundingBox;

	private:
		TSphere()
		    : TImplicitObject<T, d>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Sphere) {} //needed for serialization
		friend TImplicitObject<T, d>; //needed for serialization
	};

	template<typename T>
	struct TSphereSpecializeSamplingHelper<T, 2>
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<TVector<T, 2>>& Points, const TSphere<T, 2>& Sphere, const int32 NumPoints)
		{
			if (NumPoints <= 1 || Sphere.Radius() < KINDA_SMALL_NUMBER)
			{
				const int32 Offset = Points.AddUninitialized(1);
				Points[Offset] = Sphere.Center();
				return;
			}
			ComputeGoldenSpiralPoints(Points, Sphere, NumPoints);
		}

		/**
		 * Returns \c NumPoints points evenly distributed on a 2D \c Sphere (disk).
		 */
		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<TVector<T, 2>>& Points, const TSphere<T, 2>& Sphere, const int32 NumPoints)
		{
			ComputeGoldenSpiralPoints(Points, Sphere.Center(), Sphere.Radius(), NumPoints);
		}

		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 2>>& Points,
		    const TVector<T, 2>& Center,
		    const T Radius,
		    const int32 NumPoints,
		    const int32 SpiralSeed = 0)
		{
			const int32 Offset = Points.AddUninitialized(NumPoints);

			// Stand at the center, turn a golden ratio of whole turns, then emit
			// a point in that direction.
			//
			// Golden ratio: (1 + sqrt(5)) / 2
			// Polar sunflower increment: pi * (1 + sqrt(5))

			// Increment = 10.16640738463053...
			static const T Increment = PI * (1.0 + sqrt(5));
			for (int32 i = 0; i < NumPoints; i++)
			{
				const T Z = 0.5 + i;
				// sqrt((i+0.5) / NumPoints) sampling i = [0, NumPoints) varies: (0, 1).
				// We then scale to the radius of our Sphere.
				const T R = FMath::Sqrt(Z / NumPoints) * Radius;
				// Theta increases linearly from [Increment/2, Increment*NumPoints)
				const T Theta = Increment * (Z + SpiralSeed);

				// Convert polar coordinates to Cartesian, offset by the Sphere's location.
				const int32 Index = i + Offset;
				Points[Index] = Center + TVector<T, 2>(R * FMath::Cos(Theta), R * FMath::Sin(Theta));

				// Check to make sure the point is inside the sphere
				checkSlow((Points[Index] - Center).Size() - Radius < KINDA_SMALL_NUMBER);
			}
		}
	};

	template<typename T>
	struct TSphereSpecializeSamplingHelper<T, 3>
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<TVector<T, 3>>& Points, const TSphere<T, 3>& Sphere, const int32 NumPoints)
		{
			if (NumPoints <= 1 || Sphere.Radius() < KINDA_SMALL_NUMBER)
			{
				const int32 Offset = Points.AddUninitialized(1);
				Points[Offset] = Sphere.Center();
				return;
			}
			ComputeGoldenSpiralPoints(Points, Sphere, NumPoints);
		}

		/**
		 * Returns \c NumPoints points evenly distributed on a 3D \c Sphere.
		 */
		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 3>>& Points, const TSphere<T, 3>& Sphere, const int32 NumPoints,
		    const bool FirstHalf = true, const bool SecondHalf = true, const int32 SpiralSeed = 0)
		{
			ComputeGoldenSpiralPoints(Points, Sphere.Center(), Sphere.Radius(), NumPoints, FirstHalf, SecondHalf, SpiralSeed);
		}

		/**
		 * Use the golden spiral method to evenly distribute points on a sphere.
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the sphere, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Center is the center of the sphere.
		 * \p Radius is the radius of the sphere.
		 * \p NumPoints is the number of points to generate.
		 * \p BottomHalf causes the bottom half of the sphere to be generated, 
		 *    starting at \p Center - (0, 0, \p Radius).
		 * \p TopHalf causes the top half of the sphere to be generated, 
		 *    starting at \p Center.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 3>>& Points,
		    const TVector<T, 3>& Center,
		    const T Radius,
		    const int32 NumPoints,
		    const bool BottomHalf = true,
		    const bool TopHalf = true,
		    const int32 SpiralSeed = 0)
		{
			if (!TopHalf && !BottomHalf)
			{
				return;
			}

			const int32 Offset = Points.AddUninitialized(NumPoints);

			// We use the same method in 3D as 2D, but in spherical coordinates rather than polar.
			//
			// Theta is the angle about the Z axis, relative to the positive X axis.
			// Phi is the angle between the positive Z axis and the line from the origin to the point

			// GRIncrement = 10.16640738463053...
			static const T GRIncrement = PI * (1.0 + sqrt(5));

			// If PhiSteps is 2X NumPoints, then we'll only generate half the sphere.
			//const int32 PhiSteps = TopHalf + BottomHalf == 1 ? NumPoints * 2 : NumPoints;
			// If PhiSeed is 0, then we'll generate the sphere from the beginning - the bottom.
			// Otherwise, we'll generate the sphere from the middle.
			//const int32 PhiSeed = !TopHalf && BottomHalf ? NumPoints : 0;

			int32 Index = Offset;
			if (BottomHalf && !TopHalf)
			{
				for (int32 i = 0; i < NumPoints; i++)
				{
					const T Sample = 0.5 + i;
					// ((i + 0.5) / (NumPoints * 2)) varies: (0.0, 0.5)
					// So, (2 * (i + 0.5) / (NumPoints * 2)) varies: (0.0, 1.0)
					// So, ((2 * (i + 0.5) / (NumPoints * 2)) - 1) varies: (-1, 0.0)
					const T V = (2.0 * (0.5 + i) / (2.0 * NumPoints)) - 1.0;
					const T Phi = FMath::Acos(V);
					checkSlow(Phi > PI / 2 - KINDA_SMALL_NUMBER);
					const T Theta = GRIncrement * (Sample + SpiralSeed);

					// Convert spherical coordinates to Cartesian, scaled by the radius of our Sphere, and offset by its location.
					const T SinPhi = FMath::Sin(Phi);
					TVector<T, 3>& Pt = Points[Index++];
					Pt = Center +
					    TVector<T, 3>(
					        Radius * FMath::Cos(Theta) * SinPhi,
					        Radius * FMath::Sin(Theta) * SinPhi,
					        Radius * FMath::Cos(Phi));

					checkSlow(FMath::Abs(TSphere<T, 3>(Center, Radius).SignedDistance(Pt)) < KINDA_SMALL_NUMBER);
					checkSlow(Pt[2] < Center[2] + KINDA_SMALL_NUMBER);
				}
			}
			else if (!BottomHalf && TopHalf)
			{
				for (int32 i = 0; i < NumPoints; i++)
				{
					const T Sample = 0.5 + i;
					const T V = (2.0 * (0.5 + i) / (2.0 * NumPoints)); // varies: (0.0, 1.0)
					const T Phi = FMath::Acos(V);
					checkSlow(Phi < PI / 2 + KINDA_SMALL_NUMBER);
					const T Theta = GRIncrement * (Sample + SpiralSeed);

					// Convert spherical coordinates to Cartesian, scaled by the radius of our Sphere, and offset by its location.
					const T SinPhi = FMath::Sin(Phi);
					TVector<T, 3>& Pt = Points[Index++];
					Pt = Center +
					    TVector<T, 3>(
					        Radius * FMath::Cos(Theta) * SinPhi,
					        Radius * FMath::Sin(Theta) * SinPhi,
					        Radius * FMath::Cos(Phi));

					checkSlow(FMath::Abs(TSphere<T, 3>(Center, Radius).SignedDistance(Pt)) < KINDA_SMALL_NUMBER);
					checkSlow(Pt[2] > Center[2] - KINDA_SMALL_NUMBER);
				}
			}
			else
			{
				for (int32 i = 0; i < NumPoints; i++)
				{
					const T Sample = 0.5 + i;
					// arccos(x), where x = [-1, 1] varies: [PI, 0]
					// ((i + 0.5) / NumPoints) varies: (0.0, 1.0)
					// So, (2 * (i + 0.5) / NumPoints) varies: (0.0, 2.0)
					// So, (1 - (2 * (i + 0.5) / NumPoints) varies: (-1.0, 1.0)
					// So, Phi varies: (PI, 0) as i varies: [0, NumPoints-1].
					const T Phi = FMath::Acos(1.0 - 2.0 * Sample / NumPoints);
					// Theta varies: [5.0832036..., NumPoints*Increment)
					const T Theta = GRIncrement * (Sample + SpiralSeed);

					// Convert spherical coordinates to Cartesian, scaled by the radius of our Sphere, and offset by its location.
					const T SinPhi = FMath::Sin(Phi);
					TVector<T, 3>& Pt = Points[Index++];
					Pt = Center +
					    TVector<T, 3>(
					        Radius * FMath::Cos(Theta) * SinPhi,
					        Radius * FMath::Sin(Theta) * SinPhi,
					        Radius * FMath::Cos(Phi));

					checkSlow(FMath::Abs(TSphere<T, 3>(Center, Radius).SignedDistance(Pt)) < KINDA_SMALL_NUMBER);
				}
			}
		}

		static FORCEINLINE void ComputeBottomHalfSemiSphere(
		    TArray<TVector<T, 3>>& Points, const TSphere<T, 3>& Sphere, const int32 NumPoints, const int32 SpiralSeed = 0)
		{
			ComputeGoldenSpiralPoints(Points, Sphere, NumPoints, true, false, SpiralSeed);
		}

		static FORCEINLINE void ComputeTopHalfSemiSphere(
		    TArray<TVector<T, 3>>& Points, const TSphere<T, 3>& Sphere, const int32 NumPoints, const int32 SpiralSeed = 0)
		{
			ComputeGoldenSpiralPoints(Points, Sphere, NumPoints, false, true, SpiralSeed);
		}
	};

} // namespace Chaos