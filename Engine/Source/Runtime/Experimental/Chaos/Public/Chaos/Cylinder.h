// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Rotation.h"
#include "Chaos/Sphere.h"
#include "ChaosArchive.h"

namespace Chaos
{
	template<typename T>
	struct TCylinderSpecializeSamplingHelper;

	template<class T>
	class TCylinder final : public FImplicitObject
	{
	public:
		using FImplicitObject::SignedDistance;
		using FImplicitObject::GetTypeName;

		TCylinder(const TVector<T, 3>& x1, const TVector<T, 3>& x2, const T Radius)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Cylinder)
		    , MPlane1(x1, (x2 - x1).GetSafeNormal()) // Plane normals point inward
		    , MPlane2(x2, -MPlane1.Normal())
		    , MHeight((x2 - x1).Size())
		    , MRadius(Radius)
		    , MLocalBoundingBox(x1, x1)
		{
			MLocalBoundingBox.GrowToInclude(x2);
			MLocalBoundingBox = TAABB<T, 3>(MLocalBoundingBox.Min() - TVector<T, 3>(MRadius), MLocalBoundingBox.Max() + TVector<T, 3>(MRadius));
		}
		TCylinder(const TCylinder<T>& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Cylinder)
		    , MPlane1(Other.MPlane1)
		    , MPlane2(Other.MPlane2)
		    , MHeight(Other.MHeight)
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(Other.MLocalBoundingBox)
		{
		}
		TCylinder(TCylinder<T>&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Cylinder)
		    , MPlane1(MoveTemp(Other.MPlane1))
		    , MPlane2(MoveTemp(Other.MPlane2))
		    , MHeight(Other.MHeight)
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
		{
		}
		~TCylinder() {}

		static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::Cylinder; }

		/**
		 * Returns sample points centered about the origin.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const int32 NumPoints, const bool IncludeEndCaps = true) const
		{
			TArray<TVector<T, 3>> Points;
			const TVector<T, 3> Mid = GetCenter();
			TCylinderSpecializeSamplingHelper<T>::ComputeSamplePoints(
			    Points, TCylinder<T>(MPlane1.X() - Mid, MPlane2.X() - Mid, GetRadius()), NumPoints, IncludeEndCaps);
			return Points;
		}
		/** 
		 * Returns sample points centered about the origin. 
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const T PointsPerUnitArea, const bool IncludeEndCaps = true, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(IncludeEndCaps))), MinPoints, MaxPoints), IncludeEndCaps); }

		/**
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const int32 NumPoints, const bool IncludeEndCaps = true) const
		{
			TArray<TVector<T, 3>> Points;
			TCylinderSpecializeSamplingHelper<T>::ComputeSamplePoints(Points, *this, NumPoints, IncludeEndCaps);
			return Points;
		}
		/** 
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const T PointsPerUnitArea, const bool IncludeEndCaps = true, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(IncludeEndCaps))), MinPoints, MaxPoints), IncludeEndCaps); }
#if 0
		/*virtual*/ T SignedDistance(const TVector<T, 3>& x) const /*override*/
		{
			TVector<T, 3> V = MPlane1.X() - x;
			T Plane1Distance = TVector<T, 3>::DotProduct(V, MPlane1.Normal());
			T PlaneDistance = FMath::Max(Plane1Distance, -MHeight - Plane1Distance);
			T CylinderDistance = (V - Plane1Distance * MPlane1.Normal()).Size() - MRadius;
			return CylinderDistance > 0.0 && PlaneDistance > 0.0 ?
			    FMath::Sqrt(FMath::Square(CylinderDistance) + FMath::Square(PlaneDistance)) :
			    FMath::Max(CylinderDistance, PlaneDistance);
		}
#endif
		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const override
		{
			TVector<T, 3> Normal1, Normal2;
			const T Distance1 = MPlane1.PhiWithNormal(x, Normal1); // positive on the cylinder side
			if (Distance1 < 0) // off end 1
			{
				check(MPlane2.PhiWithNormal(x, Normal2) > 0);
				const TVector<T, 3> v = x - TVector<T, 3>(Normal1 * Distance1 + MPlane1.X());
				if (v.Size() > MRadius)
				{
					const TVector<T, 3> Corner = v.GetSafeNormal() * MRadius + MPlane1.X();
					Normal = x - Corner;
					return Normal.SafeNormalize();
				}
				else
				{
					Normal = -Normal1;
					return -Distance1;
				}
			}
			const T Distance2 = MPlane2.PhiWithNormal(x, Normal2);
			if (Distance2 < 0) // off end 2
			{
				check(MPlane1.PhiWithNormal(x, Normal1) > 0);
				const TVector<T, 3> v = x - TVector<T, 3>(Normal2 * Distance2 + MPlane2.X());
				if (v.Size() > MRadius)
				{
					const TVector<T, 3> Corner = v.GetSafeNormal() * MRadius + MPlane2.X();
					Normal = x - Corner;
					return Normal.SafeNormalize();
				}
				else
				{
					Normal = -Normal2;
					return -Distance2;
				}
			}
			// Both distances are positive, should add to the height of the cylinder.
			check(FMath::Abs(Distance1 + Distance2 - MHeight) < KINDA_SMALL_NUMBER);
			const TVector<T, 3> SideVector = (x - TVector<T, 3>(Normal1 * Distance1 + MPlane1.X()));
			const T SideDistance = SideVector.Size() - MRadius;
			if (SideDistance < 0)
			{
				// We're inside the cylinder. If the distance to a endcap is less
				// than the SideDistance, push out the end.
				const T TopDistance = Distance1 < Distance2 ? Distance1 : Distance2;
				if (TopDistance < -SideDistance)
				{
					Normal = Distance1 < Distance2 ? -Normal1 : -Normal2;
					return -TopDistance;
				}
			}
			Normal = SideVector.GetSafeNormal();
			return SideDistance;
		}

		virtual const TAABB<T, 3> BoundingBox() const override { return MLocalBoundingBox; }

		T GetRadius() const { return MRadius; }
		T GetHeight() const { return MHeight; }
		const TVector<T, 3>& GetX1() const { return MPlane1.X(); }
		const TVector<T, 3>& GetX2() const { return MPlane2.X(); }
		/** Returns the bottommost point on the cylinder. */
		const TVector<T, 3>& GetOrigin() const { return MPlane1.X(); }
		/** Returns the topmost point on the cylinder. */
		const TVector<T, 3>& GetInsertion() const { return MPlane2.X(); }
		TVector<T, 3> GetCenter() const { return (MPlane1.X() + MPlane2.X()) * (T)0.5; }
		/** Returns the centroid (center of mass). */
		TVector<T, 3> GetCenterOfMass() const { return GetCenter(); }

		TVector<T, 3> GetAxis() const { return (MPlane2.X() - MPlane1.X()).GetSafeNormal(); }

		T GetArea(const bool IncludeEndCaps = true) const { return GetArea(MHeight, MRadius, IncludeEndCaps); }
		static T GetArea(const T Height, const T Radius, const bool IncludeEndCaps)
		{
			static const T PI2 = 2. * PI;
			return IncludeEndCaps ?
				PI2 * Radius * (Height + Radius) :
				PI2 * Radius * Height;
		}

		T GetVolume() const { return GetVolume(MHeight, MRadius); }
		static T GetVolume(const T Height, const T Radius) { return PI * Radius * Radius * Height; }

		PMatrix<T, 3, 3> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, MHeight, MRadius); }
		static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const T Height, const T Radius)
		{
			// https://www.wolframalpha.com/input/?i=cylinder
			const T RR = Radius * Radius;
			const T Diag12 = Mass / 12. * (3.*RR + Height*Height);
			const T Diag3 = Mass / 2. * RR;
			return PMatrix<T, 3, 3>(Diag12, Diag12, Diag3);
		}

		static TRotation<T, 3> GetRotationOfMass()
		{ return TRotation<T, 3>(TVector<T, 3>(0), 1); }

		virtual void Serialize(FChaosArchive& Ar)
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			FImplicitObject::SerializeImp(Ar);
			Ar << MPlane1;
			Ar << MPlane2;
			Ar << MHeight;
			Ar << MRadius;
			TBox<T, 3>::SerializeAsAABB(Ar, MLocalBoundingBox);
		}

	private:
		virtual Pair<TVector<T, 3>, bool> FindClosestIntersectionImp(const TVector<T, 3>& StartPoint, const TVector<T, 3>& EndPoint, const T Thickness) const override
		{
			TArray<Pair<T, TVector<T, 3>>> Intersections;
			// Flatten to Plane defined by StartPoint and MPlane1.Normal()
			// Project End and Center into Plane
			TVector<T, 3> ProjectedEnd = EndPoint - TVector<T, 3>::DotProduct(EndPoint - StartPoint, MPlane1.Normal()) * MPlane1.Normal();
			TVector<T, 3> ProjectedCenter = MPlane1.X() - TVector<T, 3>::DotProduct(MPlane1.X() - StartPoint, MPlane1.Normal()) * MPlane1.Normal();
			auto ProjectedSphere = TSphere<T, 3>(ProjectedCenter, MRadius);
			auto InfiniteCylinderIntersection = ProjectedSphere.FindClosestIntersection(StartPoint, ProjectedEnd, Thickness);
			if (InfiniteCylinderIntersection.Second)
			{
				auto UnprojectedIntersection = TPlane<T, 3>(InfiniteCylinderIntersection.First, (StartPoint - InfiniteCylinderIntersection.First).GetSafeNormal()).FindClosestIntersection(StartPoint, EndPoint, 0);
				check(UnprojectedIntersection.Second);
				Intersections.Add(MakePair((UnprojectedIntersection.First - StartPoint).Size(), UnprojectedIntersection.First));
			}
			auto Plane1Intersection = MPlane1.FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (Plane1Intersection.Second)
				Intersections.Add(MakePair((Plane1Intersection.First - StartPoint).Size(), Plane1Intersection.First));
			auto Plane2Intersection = MPlane2.FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (Plane2Intersection.Second)
				Intersections.Add(MakePair((Plane2Intersection.First - StartPoint).Size(), Plane2Intersection.First));
			Intersections.Sort([](const Pair<T, TVector<T, 3>>& Elem1, const Pair<T, TVector<T, 3>>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (SignedDistance(Elem.Second) <= (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(TVector<T, 3>(0), false);
		}

		virtual uint32 GetTypeHash() const override
		{
			const uint32 PlaneHashes = HashCombine(MPlane1.GetTypeHash(), MPlane2.GetTypeHash());
			const uint32 PropertyHash = HashCombine(::GetTypeHash(MHeight), ::GetTypeHash(MRadius));

			return HashCombine(PlaneHashes, PropertyHash);
		}

		//needed for serialization
		TCylinder() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Cylinder) {}
		friend FImplicitObject;	//needed for serialization

	private:
		TPlane<T, 3> MPlane1, MPlane2;
		T MHeight, MRadius;
		TAABB<T, 3> MLocalBoundingBox;
	};

	template<typename T>
	struct TCylinderSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<TVector<T, 3>>& Points, const TCylinder<T>& Cylinder, const int32 NumPoints, const bool IncludeEndCaps = true)
		{
			if (NumPoints <= 1 || Cylinder.GetRadius() <= KINDA_SMALL_NUMBER)
			{
				const int32 Offset = Points.Num();
				if (Cylinder.GetHeight() <= KINDA_SMALL_NUMBER)
				{
					Points.SetNumUninitialized(Offset + 1);
					Points[Offset] = Cylinder.GetCenter();
				}
				else
				{
					Points.SetNumUninitialized(Offset + 3);
					Points[Offset + 0] = Cylinder.GetOrigin();
					Points[Offset + 1] = Cylinder.GetCenter();
					Points[Offset + 2] = Cylinder.GetInsertion();
				}
				return;
			}
			ComputeGoldenSpiralPoints(Points, Cylinder, NumPoints, IncludeEndCaps);
		}

		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<TVector<T, 3>>& Points, const TCylinder<T>& Cylinder, const int32 NumPoints, const bool IncludeEndCaps = true)
		{ ComputeGoldenSpiralPoints(Points, Cylinder.GetOrigin(), Cylinder.GetAxis(), Cylinder.GetRadius(), Cylinder.GetHeight(), NumPoints, IncludeEndCaps); }

		/**
		 * Use the golden spiral method to generate evenly spaced points on a cylinder.
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the cylinder, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Origin is the bottom-most point of the cylinder.
		 * \p Axis is the orientation of the cylinder.
		 * \p Radius is the radius of the cylinder.
		 * \p Height is the height of the cylinder.
		 * \p NumPoints is the number of points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 3>>& Points,
		    const TVector<T, 3>& Origin,
		    const TVector<T, 3>& Axis,
		    const T Radius,
		    const T Height,
		    const int32 NumPoints,
		    const bool IncludeEndCaps = true,
		    int32 SpiralSeed = 0)
		{
			// Axis should be normalized.
			checkSlow(FMath::Abs(Axis.Size() - 1.0) < KINDA_SMALL_NUMBER);

			const int32 Offset = Points.Num();
			ComputeGoldenSpiralPointsUnoriented(Points, Radius, Height, NumPoints, IncludeEndCaps, SpiralSeed);

			// At this point, Points are centered about the origin (0,0,0), built
			// along the Z axis.  Transform them to where they should be.
			const T HalfHeight = Height / 2;
			const TRotation<float, 3> Rotation = TRotation<float, 3>::FromRotatedVector(TVector<float, 3>(0, 0, 1), Axis);
			checkSlow(((Origin + Axis * Height) - (Rotation.RotateVector(TVector<T, 3>(0, 0, Height)) + Origin)).Size() < KINDA_SMALL_NUMBER);
			for (int32 i = Offset; i < Points.Num(); i++)
			{
				TVector<T, 3>& Point = Points[i];
				const TVector<T, 3> PointNew = Rotation.RotateVector(Point + TVector<T, 3>(0, 0, HalfHeight)) + Origin;
				checkSlow(FMath::Abs(TCylinder<T>(Origin, Origin + Axis * Height, Radius).SignedDistance(PointNew)) < KINDA_SMALL_NUMBER);
				Point = PointNew;
			}
		}

		/**
		 * Generates evenly spaced points on a cylinder, oriented about the Z axis, 
		 * varying from [-Height/2, Height/2].
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the cylinder, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Radius is the radius of the cylinder.
		 * \p Height is the height of the cylinder.
		 * \p NumPoints is the number of points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static FORCEINLINE void ComputeGoldenSpiralPointsUnoriented(
		    TArray<TVector<T, 3>>& Points,
		    const T Radius,
		    const T Height,
		    const int32 NumPoints,
		    const bool IncludeEndCaps = true,
		    int32 SpiralSeed = 0)
		{
			// Evenly distribute points between the cylinder body and the end caps.
			int32 NumPointsEndCap;
			int32 NumPointsCylinder;
			if (IncludeEndCaps)
			{
				const T CapArea = PI * Radius * Radius;
				const T CylArea = 2.0 * PI * Radius * Height;
				const T AllArea = CylArea + CapArea * 2;
				if (AllArea > KINDA_SMALL_NUMBER)
				{
					NumPointsCylinder = static_cast<int32>(round(CylArea / AllArea * NumPoints));
					NumPointsCylinder += (NumPoints - NumPointsCylinder) % 2;
					NumPointsEndCap = (NumPoints - NumPointsCylinder) / 2;
				}
				else
				{
					NumPointsCylinder = 0;
					NumPointsEndCap = (NumPoints - (NumPoints % 2)) / 2;
				}
			}
			else
			{
				NumPointsCylinder = NumPoints;
				NumPointsEndCap = 0;
			}
			const int32 NumPointsToAdd = NumPointsCylinder + NumPointsEndCap * 2;
			Points.Reserve(Points.Num() + NumPointsToAdd);

			int32 Offset = Points.Num();
			const T HalfHeight = Height / 2;
			TArray<TVector<T, 2>> Points2D;
			Points2D.Reserve(NumPointsEndCap);
			if (IncludeEndCaps)
			{
				TSphereSpecializeSamplingHelper<T, 2>::ComputeGoldenSpiralPoints(
				    Points2D, TVector<T, 2>((T)0.0), Radius, NumPointsEndCap, SpiralSeed);
				Offset = Points.AddUninitialized(Points2D.Num());
				for (int32 i = 0; i < Points2D.Num(); i++)
				{
					const TVector<T, 2>& Pt = Points2D[i];
					checkSlow(Pt.Size() < Radius + KINDA_SMALL_NUMBER);
					Points[i + Offset] = TVector<T, 3>(Pt[0], Pt[1], -HalfHeight);
				}
				// Advance the SpiralSeed by the number of points generated.
				SpiralSeed += Points2D.Num();
			}

			Offset = Points.AddUninitialized(NumPointsCylinder);
			static const T Increment = PI * (1.0 + sqrt(5));
			for (int32 i = 0; i < NumPointsCylinder; i++)
			{
				// In the 2D sphere (disc) case, we vary R so it increases monotonically,
				// which spreads points out across the disc:
				//     const T R = FMath::Sqrt((0.5 + Index) / NumPoints) * Radius;
				// But we're mapping to a cylinder, which means we want to keep R constant.
				const T R = Radius;
				const T Theta = Increment * (0.5 + i + SpiralSeed);

				// Map polar coordinates to Cartesian, and vary Z by [-HalfHeight, HalfHeight].
				const T Z = FMath::LerpStable(-HalfHeight, HalfHeight, static_cast<T>(i) / (NumPointsCylinder - 1));
				Points[i + Offset] =
				    TVector<T, 3>(
				        R * FMath::Cos(Theta),
				        R * FMath::Sin(Theta),
				        Z);

				checkSlow(FMath::Abs(TVector<T, 2>(Points[i + Offset][0], Points[i + Offset][1]).Size() - Radius) < KINDA_SMALL_NUMBER);
			}
			// Advance the SpiralSeed by the number of points generated.
			SpiralSeed += NumPointsCylinder;

			if (IncludeEndCaps)
			{
				Points2D.Reset();
				TSphereSpecializeSamplingHelper<T, 2>::ComputeGoldenSpiralPoints(
				    Points2D, TVector<T, 2>((T)0.0), Radius, NumPointsEndCap, SpiralSeed);
				Offset = Points.AddUninitialized(Points2D.Num());
				for (int32 i = 0; i < Points2D.Num(); i++)
				{
					const TVector<T, 2>& Pt = Points2D[i];
					checkSlow(Pt.Size() < Radius + KINDA_SMALL_NUMBER);
					Points[i + Offset] = TVector<T, 3>(Pt[0], Pt[1], HalfHeight);
				}
			}
		}
	};
} // namespace Chaos
