// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/ReleaseObjectVersion.h"

namespace Chaos
{
	// Metadata for a convex shape used by the manifold generation system and anything
	// else that can benefit from knowing which vertices are associated with the faces.
	// @todo(chaos): support asset-dependent index size (8, 16, 32 bit). Make GetVertexPlanes and GetPlaneVertices return a converting array view.
	class CHAOS_API FConvexStructureData
	{
	public:
		bool IsValid() const
		{
			return PlaneVertices.Num() > 0;
		}

		TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const
		{
			return MakeArrayView(VertexPlanes[VertexIndex]);
		}
		
		TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const
		{
			return MakeArrayView(PlaneVertices[FaceIndex]);
		}

		void SetPlaneVertices(TArray<TArray<int32>>&& InPlaneVertices, int32 NumVerts)
		{
			// Steal the arrays of vertices per plane
			PlaneVertices = MoveTemp(InPlaneVertices);

			// Generate the arrays of planes per vertex
			VertexPlanes.SetNum(NumVerts);
			for (int32 PlaneIndex = 0; PlaneIndex < PlaneVertices.Num(); ++PlaneIndex)
			{
				for (int32 VertexIndex = 0; VertexIndex < PlaneVertices[PlaneIndex].Num(); ++VertexIndex)
				{
					const int32 PlaneVertexIndex = PlaneVertices[PlaneIndex][VertexIndex];
					VertexPlanes[PlaneVertexIndex].Add(PlaneIndex);
				}
			}
		}

		void Serialize(FArchive& Ar)
		{
			Ar << PlaneVertices;
			Ar << VertexPlanes;
		}

		friend FArchive& operator<<(FArchive& Ar, FConvexStructureData& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}

	private:
		// For each face: the set of vertex indices that form the corners of the face in counter-clockwise order
		TArray<TArray<int32>> PlaneVertices;

		// For each vertex: the set of face indices that use the vertex
		TArray<TArray<int32>> VertexPlanes;
	};



	class CHAOS_API FConvex final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;
		using TType = float;
		static constexpr unsigned D = 3;

		FConvex()
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Volume(0.f)
			, CenterOfMass(FVec3(0.f))
		{}
		FConvex(const FConvex&) = delete;
		FConvex(FConvex&& Other)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(Other.Planes))
		    , SurfaceParticles(MoveTemp(Other.SurfaceParticles))
		    , LocalBoundingBox(MoveTemp(Other.LocalBoundingBox))
			, StructureData(MoveTemp(Other.StructureData))
			, Volume(MoveTemp(Other.Volume))
			, CenterOfMass(MoveTemp(Other.CenterOfMass))
		{}

		// NOTE: This constructor will result in approximate COM and volume calculations, since it does
		// not have face indices for surface particles.
		// NOTE: Convex constructed this way will not contain any structure data
		// @todo(chaos): Keep track of invalid state and ensure on volume or COM access?
		// @todo(chaos): Add plane vertex indices in the constructor and call CreateStructureData
		// @todo(chaos): Merge planes? Or assume the input is a good convex hull?
		FConvex(TArray<TPlaneConcrete<FReal, 3>>&& InPlanes, TParticles<FReal, 3>&& InSurfaceParticles)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(InPlanes))
		    , SurfaceParticles(MoveTemp(InSurfaceParticles))
		    , LocalBoundingBox(TAABB<FReal, 3>::EmptyAABB())
		{
			for (uint32 ParticleIndex = 0; ParticleIndex < SurfaceParticles.Size(); ++ParticleIndex)
			{
				LocalBoundingBox.GrowToInclude(SurfaceParticles.X(ParticleIndex));
			}

			// For now we approximate COM and volume with the bounding box
			CenterOfMass = LocalBoundingBox.GetCenterOfMass();
			Volume = LocalBoundingBox.GetVolume();
		}

		FConvex(const TParticles<FReal, 3>& InParticles, const FReal InMargin)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{
			const uint32 NumParticles = InParticles.Size();
			if (NumParticles == 0)
			{
				return;
			}

			TArray<TArray<int32>> FaceIndices;
			FConvexBuilder::Build(InParticles, Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());

			// @chaos(todo): this only works with triangles. Fix that an we can run MergeFaces before calling this
			CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);

			FConvexBuilder::MergeFaces(Planes, FaceIndices, SurfaceParticles);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());

			CreateStructureData(MoveTemp(FaceIndices));

			ApplyMargin(InMargin);
		}

	private:
		void ApplyMargin(FReal InMargin);
		void ShrinkCore(FReal InMargin);
		void CreateStructureData(TArray<TArray<int32>>&& FaceIndices);

	public:
		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::Convex;
		}

		virtual const TAABB<FReal, 3> BoundingBox() const override
		{
			return LocalBoundingBox;
		}

		// Return the distance to the surface (including the margin)
		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const override
		{
			float Phi = PhiWithNormalInternal(x, Normal);
			if (Phi > 0)
			{
				//Outside convex, so test against bounding box - this is done to avoid
				//inaccurate results given by PhiWithNormalInternal when x is far outside

				FVec3 SnappedPosition, BoundingNormal;
				const float BoundingPhi = LocalBoundingBox.PhiWithNormal(x, BoundingNormal);
				if (BoundingPhi <= 0)
				{
					//Inside bounding box - snap to convex
					SnappedPosition = x - Phi * Normal;
				}
				else
				{
					//Snap to bounding box, and test convex
					SnappedPosition = x - BoundingPhi * BoundingNormal;
					Phi = PhiWithNormalInternal(SnappedPosition, Normal);
					SnappedPosition -= Phi * Normal;
				}

				//one final snap to ensure we're on the surface
				Phi = PhiWithNormalInternal(SnappedPosition, Normal);
				SnappedPosition -= Phi * Normal;

				//Return phi/normal based on distance from original position to snapped position
				const FVec3 Difference = x - SnappedPosition;
				Phi = Difference.Size();
				if (CHAOS_ENSURE(Phi > TNumericLimits<float>::Min())) //Phi shouldn't be 0 here since we only enter this block if x was outside the convex
				{
					Normal = (Difference) / Phi;
				}
				else
				{
					Normal = FVector::ForwardVector;
				}
			}
			return Phi - GetMargin();
		}

	private:
		// Distance to the core shape (excluding margin)
		FReal PhiWithNormalInternal(const FVec3& x, FVec3& Normal) const
		{
			const int32 NumPlanes = Planes.Num();
			if (NumPlanes == 0)
			{
				return FLT_MAX;
			}
			check(NumPlanes > 0);

			FReal MaxPhi = TNumericLimits<FReal>::Lowest();
			int32 MaxPlane = 0;

			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				const FReal Phi = Planes[Idx].SignedDistance(x);
				if (Phi > MaxPhi)
				{
					MaxPhi = Phi;
					MaxPlane = Idx;
				}
			}

			return Planes[MaxPlane].PhiWithNormal(x, Normal);
		}

	public:
		/** Calls \c GJKRaycast(), which may return \c true but 0 for \c OutTime, 
		 * which means the bodies are touching, but not by enough to determine \c OutPosition 
		 * and \c OutNormal should be.  The burden for detecting this case is deferred to the
		 * caller. 
		 */
		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override
		{
			OutFaceIndex = INDEX_NONE;	//finding face is expensive, should be called directly by user
			const FRigidTransform3 StartTM(StartPoint, FRotation3::FromIdentity());
			const TSphere<FReal, 3> Sphere(FVec3(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, GetMargin());
		}

		virtual Pair<FVec3, bool> FindClosestIntersectionImp(const FVec3& StartPoint, const FVec3& EndPoint, const FReal Thickness) const override
		{
			// @todo(chaos): margin

			const int32 NumPlanes = Planes.Num();
			TArray<Pair<FReal, FVec3>> Intersections;
			Intersections.Reserve(FMath::Min(static_cast<int32>(NumPlanes*.1), 16)); // Was NumPlanes, which seems excessive.
			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				auto PlaneIntersection = Planes[Idx].FindClosestIntersection(StartPoint, EndPoint, Thickness);
				if (PlaneIntersection.Second)
				{
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).SizeSquared(), PlaneIntersection.First));
				}
			}
			Intersections.Sort([](const Pair<FReal, FVec3>& Elem1, const Pair<FReal, FVec3>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (this->SignedDistance(Elem.Second) < (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(FVec3(0), false);
		}

		// Whether the structure data has been created for this convex (will eventually always be true)
		bool HasStructureData() const { return StructureData.IsValid(); }

		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlane(const FVec3& Normal) const;

		// Get the index of the plane that most opposes the normal, assuming it passes through the specified vertex
		int32 GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const;

		// Get the set of planes that pass through the specified vertex
		TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const;

		// Get the list of vertices that form the boundary of the specified face
		TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const;

		int32 NumPlanes() const
		{
			return Planes.Num();
		}

		int32 NumVertices() const
		{
			return (int32)SurfaceParticles.Size();
		}

		// Get the plane at the specified index (e.g., indices from GetVertexPlanes)
		const TPlaneConcrete<FReal, 3>& GetPlane(int32 FaceIndex) const
		{
			return Planes[FaceIndex];
		}

		// Get the vertex at the specified index (e.g., indices from GetPlaneVertices)
		const FVec3& GetVertex(int32 VertexIndex) const
		{
			return SurfaceParticles.X(VertexIndex);
		}


		// @todo(chaos): margin
		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const override;

		FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const
		{
			// For convexes, this function must be called with a face index.
			// If this ensure is getting hit, fix the caller so that it
			// passes in a valid face index.
			if (CHAOS_ENSURE(FaceIndex != INDEX_NONE))
			{
				const TPlaneConcrete<FReal, 3>& OpposingFace = GetFaces()[FaceIndex];
				return OpposingFace.Normal();
			}
			return FVec3(0.f, 0.f, 1.f);
		}

		// @todo(chaos): margin
		virtual int32 FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist = 0.01) const override;

		// Return support point on the core shape ignoring margin
		FORCEINLINE FVec3 SupportCore(const FVec3& Direction) const
		{
			return SupportImpl(Direction, 0);
		}

		// Return support point on the outer shape including margin
		FORCEINLINE FVec3 Support(const FVec3& Direction, const FReal Thickness) const
		{
			return SupportImpl(Direction, GetMargin() + Thickness);
		}

	private:
		FVec3 SupportImpl(const FVec3& Direction, const FReal Thickness) const
		{
			FReal MaxDot = TNumericLimits<FReal>::Lowest();
			int32 MaxVIdx = 0;
			const int32 NumVertices = SurfaceParticles.Size();

			if(ensure(NumVertices > 0))
			{
				for(int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					const FReal Dot = FVec3::DotProduct(SurfaceParticles.X(Idx), Direction);
					if(Dot > MaxDot)
					{
						MaxDot = Dot;
						MaxVIdx = Idx;
					}
				}
			}
			else
			{
				UE_LOG(LogChaos, Warning, TEXT("Attempting to get a support for an empty convex. Returning object center."));
				return FVec3(0);
			}

			if (Thickness)
			{
				return SurfaceParticles.X(MaxVIdx) + Direction.GetUnsafeNormal() * Thickness;
			}
			return SurfaceParticles.X(MaxVIdx);
		}

	public:
		virtual FString ToString() const
		{
			return FString::Printf(TEXT("Convex"));
		}

		const TParticles<FReal, 3>& GetSurfaceParticles() const
		{
			return SurfaceParticles;
		}

		const TArray<TPlaneConcrete<FReal, 3>>& GetFaces() const
		{
			return Planes;
		}

		const FReal GetVolume() const
		{
			return Volume;
		}

		const FMatrix33 GetInertiaTensor(const FReal Mass) const
		{
			// TODO: More precise inertia!
			return LocalBoundingBox.GetInertiaTensor(Mass);
		}

		const FVec3 GetCenterOfMass() const
		{
			return CenterOfMass;
		}

		virtual uint32 GetTypeHash() const override
		{
			uint32 Result = LocalBoundingBox.GetTypeHash();

			Result = HashCombine(Result, SurfaceParticles.GetTypeHash());

			for(const TPlaneConcrete<FReal, 3>& Plane : Planes)
			{
				Result = HashCombine(Result, Plane.GetTypeHash());
			}

			return Result;
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			FImplicitObject::SerializeImp(Ar);

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::ConvexUsesTPlaneConcrete)
			{
				TArray<TPlane<FReal, 3>> TmpPlanes;
				Ar << TmpPlanes;

				Planes.SetNum(TmpPlanes.Num());
				for(int32 Idx = 0; Idx < Planes.Num(); ++Idx)
				{
					Planes[Idx] = TmpPlanes[Idx].PlaneConcrete();
				}
			}
			else
			{
				Ar << Planes;
			}

			Ar << SurfaceParticles;
			TBox<FReal,3>::SerializeAsAABB(Ar, LocalBoundingBox);

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddConvexCenterOfMassAndVolume)
			{
				Ar << Volume;
				Ar << CenterOfMass;
			}
			else if (Ar.IsLoading())
			{
				// Rebuild convex in order to extract face indices.
				// TODO: Make it so it can take SurfaceParticles as both input and output without breaking...
				TArray<TArray<int32>> FaceIndices;
				TParticles<FReal, 3> TempSurfaceParticles;
				FConvexBuilder::Build(SurfaceParticles, Planes, FaceIndices, TempSurfaceParticles, LocalBoundingBox);
				CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);
			}

			Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
			if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MarginAddedToConvexAndBox)
			{
				Ar << FImplicitObject::Margin;
			}

			if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::StructureDataAddedToConvex)
			{
				Ar << StructureData;
			}
			else if (Ar.IsLoading())
			{
				// Generate the structure data from the planes and vertices
				TArray<TArray<int32>> FaceIndices;
				FConvexBuilder::BuildPlaneVertexIndices(Planes, SurfaceParticles, FaceIndices);
				CreateStructureData(MoveTemp(FaceIndices));
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override
		{
			SerializeImp(Ar);
		}

		virtual bool IsValidGeometry() const override
		{
			return (SurfaceParticles.Size() > 0 && Planes.Num() > 0);
		}

		virtual bool IsPerformanceWarning() const override
		{
			return FConvexBuilder::IsPerformanceWarning(Planes.Num(), SurfaceParticles.Size());
		}

		virtual FString PerformanceWarningAndSimplifaction() override
		{

			FString PerformanceWarningString = FConvexBuilder::PerformanceWarningString(Planes.Num(), SurfaceParticles.Size());
			if (FConvexBuilder::IsGeometryReductionEnabled())
			{
				PerformanceWarningString += ", [Simplifying]";
				SimplifyGeometry();
			}

			return PerformanceWarningString;
		}

		void SimplifyGeometry()
		{
			TArray<TArray<int32>> FaceIndices;
			FConvexBuilder::Simplify(Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
			FConvexBuilder::MergeFaces(Planes, FaceIndices, SurfaceParticles);
			CreateStructureData(MoveTemp(FaceIndices));
		}

		FVec3 GetCenter() const
		{
			return FVec3(0);
		}

	private:
		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TParticles<FReal, 3> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TAABB<FReal, 3> LocalBoundingBox;
		FConvexStructureData StructureData;
		float Volume;
		FVec3 CenterOfMass;
	};

	extern CHAOS_API int32 Chaos_Collision_ConvexMarginType;
}
