// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"

namespace Chaos
{
template <typename T>
TTriangleMeshImplicitObject<T>::TTriangleMeshImplicitObject(TParticles<T, 3>&& Particles, TArray<TVector<int32, 3>>&& Elements)
	: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
	, MParticles(MoveTemp(Particles))
	, MElements(MoveTemp(Elements))
	, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
{
	for (uint32 Idx = 1; Idx < MParticles.Size(); ++Idx)
	{
		MLocalBoundingBox.GrowToInclude(MParticles.X(Idx));
	}
	RebuildBV();
}

template <typename T>
struct TTriangleMeshRaycastVisitor
{
	TTriangleMeshRaycastVisitor(const TVector<T,3>& InStart, const TVector<T,3>& InDir, const T InThickness, const TParticles<T,3>& InParticles, const TArray<TVector<int32, 3>>& InElements)
	: Particles(InParticles)
	, Elements(InElements)
	, StartPoint(InStart)
	, Dir(InDir)
	, Thickness(InThickness)
	, OutTime(TNumericLimits<T>::Max())
	{
	}

	enum class ERaycastType
	{
		Raycast,
		Sweep
	};

	template <ERaycastType SQType>
	bool Visit(int32 TriIdx, T& CurLength)
	{
		constexpr T Epsilon = 1e-4;
		constexpr T Epsilon2 = Epsilon * Epsilon;
		const T Thickness2 = SQType == ERaycastType::Sweep ? Thickness * Thickness : 0;
		T MinTime = 0;	//no need to initialize, but fixes warning

		const T R = Thickness + Epsilon;
		const T R2 = R * R;

		const TVector<T, 3>& A = Particles.X(Elements[TriIdx][0]);
		const TVector<T, 3>& B = Particles.X(Elements[TriIdx][1]);
		const TVector<T, 3>& C = Particles.X(Elements[TriIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> TriNormal = TVector<T, 3>::CrossProduct(AB, AC);
		const T NormalLength = TriNormal.SafeNormalize();
		if (!ensure(NormalLength > Epsilon))
		{
			//hitting degenerate triangle so keep searching - should be fixed before we get to this stage
			return true;
		}

		const TPlane<T, 3> TriPlane{ A, TriNormal };
		TVector<T, 3> RaycastPosition;
		TVector<T, 3> RaycastNormal;
		T Time;

		//Check if we even intersect with triangle plane
		int32 DummyFaceIndex;
		if (TriPlane.Raycast(StartPoint, Dir, CurLength, Thickness, Time, RaycastPosition, RaycastNormal, DummyFaceIndex))
		{
			TVector<T, 3> IntersectionPosition = RaycastPosition;
			TVector<T, 3> IntersectionNormal = RaycastNormal;
			bool bTriangleIntersects = false;
			if (Time == 0)
			{
				//Initial overlap so no point of intersection, do an explicit sphere triangle test.
				const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, StartPoint);
				const T DistToTriangle2 = (StartPoint - ClosestPtOnTri).SizeSquared();
				if (DistToTriangle2 <= R2)
				{
					OutTime = 0;
					OutFaceIndex = TriIdx;
					return false; //no one will beat Time == 0
				}
			}
			else
			{
				const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(RaycastPosition, A, B, C, RaycastPosition);	//We know Position is on the triangle plane
				const T DistToTriangle2 = (RaycastPosition - ClosestPtOnTri).SizeSquared();
				bTriangleIntersects = DistToTriangle2 <= Epsilon2;	//raycast gave us the intersection point so sphere radius is already accounted for
			}

			if (SQType == ERaycastType::Sweep && !bTriangleIntersects)
			{
				//sphere is not immediately touching the triangle, but it could start intersecting the perimeter as it sweeps by
				TVector<T, 3> BorderPositions[3];
				TVector<T, 3> BorderNormals[3];
				T BorderTimes[3];
				bool bBorderIntersections[3];

				{
					TVector<T, 3> ABCapsuleAxis = B - A;
					T ABHeight = ABCapsuleAxis.SafeNormalize();
					bBorderIntersections[0] = TCapsule<T>::RaycastFast(Thickness, ABHeight, ABCapsuleAxis, A, B, StartPoint, Dir, CurLength, 0, BorderTimes[0], BorderPositions[0], BorderNormals[0], DummyFaceIndex);
				}
				
				{
					TVector<T, 3> BCCapsuleAxis = C - B;
					T BCHeight = BCCapsuleAxis.SafeNormalize();
					bBorderIntersections[1] = TCapsule<T>::RaycastFast(Thickness, BCHeight, BCCapsuleAxis, B, C, StartPoint, Dir, CurLength, 0, BorderTimes[1], BorderPositions[1], BorderNormals[1], DummyFaceIndex);
				}
				
				{
					TVector<T, 3> ACCapsuleAxis = C - A;
					T ACHeight = ACCapsuleAxis.SafeNormalize();
					bBorderIntersections[2] = TCapsule<T>::RaycastFast(Thickness, ACHeight, ACCapsuleAxis, A, C, StartPoint, Dir, CurLength, 0, BorderTimes[2], BorderPositions[2], BorderNormals[2], DummyFaceIndex);
				}

				int32 MinBorderIdx = INDEX_NONE;
				T MinBorderTime = 0;	//initialization not needed, but fixes warning

				for (int32 BorderIdx = 0; BorderIdx < 3; ++BorderIdx)
				{
					if (bBorderIntersections[BorderIdx])
					{
						if (!bTriangleIntersects || BorderTimes[BorderIdx] < MinBorderTime)
						{
							MinBorderTime = BorderTimes[BorderIdx];
							MinBorderIdx = BorderIdx;
							bTriangleIntersects = true;
						}
					}
				}

				if (MinBorderIdx != INDEX_NONE)
				{
					IntersectionNormal = BorderNormals[MinBorderIdx];
					IntersectionPosition = BorderPositions[MinBorderIdx] - IntersectionNormal * Thickness;

					if (Time == 0)
					{
						//we were initially overlapping with triangle plane so no normal was given. Compute it now
						TVector<T, 3> TmpNormal;
						const T SignedDistance = TriPlane.PhiWithNormal(StartPoint, TmpNormal);
						RaycastNormal = SignedDistance >= 0 ? TmpNormal : -TmpNormal;
					}

					Time = MinBorderTime;
				}
			}

			if (bTriangleIntersects)
			{
				if (Time < OutTime)
				{
					OutPosition = IntersectionPosition;
					OutNormal = RaycastNormal;	//We use the plane normal even when hitting triangle edges. This is to deal with triangles that approximate a single flat surface.
					OutTime = Time;
					CurLength = Time;	//prevent future rays from going any farther
					OutFaceIndex = TriIdx;
				}
			}
		}

		return true;
	}

	bool VisitRaycast(TSpatialVisitorData<int32> TriIdx, T& CurLength)
	{
		return Visit<ERaycastType::Raycast>(TriIdx.Payload, CurLength);
	}

	bool VisitSweep(TSpatialVisitorData<int32> TriIdx, T& CurLength)
	{
		return Visit<ERaycastType::Sweep>(TriIdx.Payload, CurLength);
	}

	const TParticles<T, 3>& Particles;
	const TArray<TVector<int32, 3>>& Elements;
	const TVector<T, 3>& StartPoint;
	const TVector<T, 3>& Dir;
	const T Thickness;
	T OutTime;
	TVector<T, 3> OutPosition;
	TVector<T, 3> OutNormal;
	int32 OutFaceIndex;
};

template <typename T>
bool TTriangleMeshImplicitObject<T>::Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const
{
	TTriangleMeshRaycastVisitor<T> SQVisitor(StartPoint, Dir, Thickness, MParticles, MElements);

	if (Thickness > 0)
	{
		BVH.Sweep(StartPoint, Dir, Length, TVector<T, 3>(Thickness), SQVisitor);
	}
	else
	{
		BVH.Raycast(StartPoint, Dir, Length, SQVisitor);
	}

	if (SQVisitor.OutTime <= Length)
	{
		OutTime = SQVisitor.OutTime;
		OutPosition = SQVisitor.OutPosition;
		OutNormal = SQVisitor.OutNormal;
		OutFaceIndex = SQVisitor.OutFaceIndex;
		return true;
	}
	else
	{
		return false;
	}
}

template <typename T>
bool TTriangleMeshImplicitObject<T>::Overlap(const TVector<T, 3>& Point, const T Thickness) const
{
	TBox<T, 3> QueryBounds(Point, Point);
	QueryBounds.Thicken(Thickness);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	const T Epsilon = 1e-4;
	//ensure(Thickness > Epsilon);	//There's no hope for this to work unless thickness is large (really a sphere overlap test)
	//todo: turn ensure back on, off until some other bug is fixed

	for (int32 TriIdx : PotentialIntersections)
	{
		const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
		const T NormalLength = Normal.SafeNormalize();
		if (!ensure(NormalLength > Epsilon))
		{
			//hitting degenerate triangle - should be fixed before we get to this stage
			continue;
		}

		const TPlane<T, 3> TriPlane{ A, Normal };
		const TVector<T,3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Point);
		const T Distance2 = (ClosestPointOnTri - Point).SizeSquared();
		if (Distance2 <= Thickness * Thickness)	//This really only has a hope in working if thickness is > 0
		{
			return true;
		}
	}
	return false;
}

template <typename T>
bool TTriangleMeshImplicitObject<T>::OverlapGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const
{
	auto OverlapTriangle = [&](const TVector<T, 3>& A, const TVector<T, 3>& B, const TVector<T, 3>& C) -> bool
	{
		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;

		//It's most likely that the query object is in front of the triangle since queries tend to be on the outside.
		//However, maybe we should check if it's behind the triangle plane. Also, we should enforce this winding in some way
		const TVector<T, 3> Offset = TVector<T, 3>::CrossProduct(AB, AC);

		// Ugly but required for now until we have an easier way to do tri collisons 
		TParticles<T, 3> Particles;
		Particles.AddParticles(3);

		Particles.X(0) = A;
		Particles.X(1) = B;
		Particles.X(2) = C;

		TConvex<T, 3> TriangleConvex(Particles);

		return GJKIntersection(TriangleConvex, QueryGeom, QueryTM, Thickness, Offset);
	};

	bool bResult = false;
	TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
	QueryBounds.Thicken(Thickness);
	QueryBounds = QueryBounds.TransformedBox(QueryTM);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);


	for (int32 TriIdx : PotentialIntersections)
	{
	const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
	const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
	const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);

		if (OverlapTriangle(A, B, C))
		{
			return true;
		}
	}

	return false;
}


template <typename T>
struct TTriangleMeshSweepVisitor
{
	TTriangleMeshSweepVisitor(const TTriangleMeshImplicitObject<T>& InTriMesh, const TImplicitObject<T,3>& InQueryGeom, const TRigidTransform<T,3>& InStartTM, const TVector<T,3>& InDir, const T InThickness)
	: TriMesh(InTriMesh)
	, StartTM(InStartTM)
	, QueryGeom(InQueryGeom)
	, Dir(InDir)
	, Thickness(InThickness)
	, OutTime(TNumericLimits<T>::Max())
	{
	}

	bool VisitSweep(const TSpatialVisitorData<int32>& VisitData, T& CurLength)
	{
		const int32 TriIdx = VisitData.Payload;

		TParticles<T, 3> TriParticles;
		TriParticles.AddParticles(3);
		TriParticles.X(0) = TriMesh.MParticles.X(TriMesh.MElements[TriIdx][0]);
		TriParticles.X(1) = TriMesh.MParticles.X(TriMesh.MElements[TriIdx][1]);
		TriParticles.X(2) = TriMesh.MParticles.X(TriMesh.MElements[TriIdx][2]);

		TConvex<T, 3> TriConvex(TriParticles);

		T Time;
		TVector<T, 3> HitPosition;
		TVector<T, 3> HitNormal;
		if (GJKRaycast<T>(TriConvex, QueryGeom, StartTM, Dir, CurLength, Time, HitPosition, HitNormal, Thickness))
		{
			if (Time < OutTime)
			{
				OutNormal = HitNormal;
				OutPosition = HitPosition;
				OutTime = Time;
				CurLength = Time;
				OutFaceIndex = TriIdx;

				if (Time == 0)
				{
					//initial overlap, no one will beat this
					return false;
				}
			}
		}

		return true;
	}

	const TTriangleMeshImplicitObject<T>& TriMesh;
	const TRigidTransform<T, 3> StartTM;
	const TImplicitObject<T, 3>& QueryGeom;
	const TVector<T, 3>& Dir;
	const T Thickness;

	T OutTime;
	TVector<T, 3> OutPosition;
	TVector<T, 3> OutNormal;
	int32 OutFaceIndex;
};

template <typename T>
bool TTriangleMeshImplicitObject<T>::SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness) const
{
	bool bHit = false;
	TTriangleMeshSweepVisitor<T> SQVisitor(*this, QueryGeom, StartTM, Dir, Thickness);
	const TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
	const TVector<T, 3> StartPoint = StartTM.TransformPositionNoScale(QueryBounds.Center());
	const TVector<T, 3> Inflation = QueryBounds.Extents() * 0.5 + TVector<T, 3>(Thickness);
	BVH.template Sweep<TTriangleMeshSweepVisitor<T>, false>(StartPoint, Dir, Length, Inflation, SQVisitor);

	if (SQVisitor.OutTime <= Length)
	{
		OutTime = SQVisitor.OutTime;
		OutPosition = SQVisitor.OutPosition;
		OutNormal = SQVisitor.OutNormal;
		OutFaceIndex = SQVisitor.OutFaceIndex;
		bHit = true;
	}
	return bHit;
}

template <typename T>
int32 TTriangleMeshImplicitObject<T>::FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDist) const
{
	//todo: this is horribly slow, need adjacency information
	const T SearchDist2 = SearchDist * SearchDist;
	
	TBox<T, 3> QueryBounds(Position - TVector<T,3>(SearchDist), Position + TVector<T,3>(SearchDist));

	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);
	const T Epsilon = 1e-4;

	T MostOpposingDot = TNumericLimits<T>::Max();
	int32 MostOpposingFace = HintFaceIndex;
	
	for (int32 TriIdx : PotentialIntersections)
	{
		const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
		const T NormalLength = Normal.SafeNormalize();
		if (!ensure(NormalLength > Epsilon))
		{
			//hitting degenerate triangle - should be fixed before we get to this stage
			continue;
		}

		const TPlane<T, 3> TriPlane{ A, Normal };
		const TVector<T, 3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Position);
		const T Distance2 = (ClosestPointOnTri - Position).SizeSquared();
		if (Distance2 < SearchDist2)
		{
			const T Dot = TVector<T, 3>::DotProduct(Normal, UnitDir);
			if (Dot < MostOpposingDot)
			{
				MostOpposingDot = Dot;
				MostOpposingFace = TriIdx;
			}
		}
	}

	return MostOpposingFace;
}

template <typename T>
TVector<T, 3> TTriangleMeshImplicitObject<T>::FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const
{
	return GetFaceNormal(FaceIndex);
}

template <typename T>
TVector<T, 3> TTriangleMeshImplicitObject<T>::GetFaceNormal(const int32 FaceIdx) const
{
	if (ensure(FaceIdx != INDEX_NONE))
	{
		const TVector<T, 3>& A = MParticles.X(MElements[FaceIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[FaceIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[FaceIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
		const T Length = Normal.SafeNormalize();
		ensure(Length);
		return Normal;
	}

	return TVector<T, 3>(0, 0, 1);
}


template<typename T>
void Chaos::TTriangleMeshImplicitObject<T>::RebuildBV()
{
	const int32 NumTris = MElements.Num();
	BVEntries.Reset(NumTris);

	for (int Tri = 0; Tri<NumTris; Tri++)
	{
		BVEntries.Add({ this, Tri });
	}
	BVH.Reinitialize(BVEntries);
}


}


template class Chaos::TTriangleMeshImplicitObject<float>;
