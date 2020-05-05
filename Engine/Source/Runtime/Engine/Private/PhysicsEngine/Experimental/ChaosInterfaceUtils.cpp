// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Levelset.h"
#include "Chaos/Sphere.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/Convex.h"

#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/SphylElem.h"
#include "PhysicsEngine/SphereElem.h"

#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if WITH_PHYSX
#include "PhysXIncludes.h"
#endif

#define FORCE_ANALYTICS 0

namespace ChaosInterface
{
	template<class PHYSX_MESH>
	TArray<Chaos::TVector<int32, 3>> GetMeshElements(const PHYSX_MESH* PhysXMesh)
	{
		check(false);
	}

#if WITH_PHYSX

	template<>
	TArray<Chaos::TVector<int32, 3>> GetMeshElements(const physx::PxConvexMesh* PhysXMesh)
	{
		TArray<Chaos::TVector<int32, 3>> CollisionMeshElements;
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
		int32 offset = 0;
		int32 NbPolygons = static_cast<int32>(PhysXMesh->getNbPolygons());
		for (int32 i = 0; i < NbPolygons; i++)
		{
			physx::PxHullPolygon Poly;
			bool status = PhysXMesh->getPolygonData(i, Poly);
			const auto Indices = PhysXMesh->getIndexBuffer() + Poly.mIndexBase;

			for (int32 j = 2; j < static_cast<int32>(Poly.mNbVerts); j++)
			{
				CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[offset], Indices[offset + j], Indices[offset + j - 1]));
			}
		}
#endif
		return CollisionMeshElements;
	}

	template<>
	TArray<Chaos::TVector<int32, 3>> GetMeshElements(const physx::PxTriangleMesh* PhysXMesh)
	{
		TArray<Chaos::TVector<int32, 3>> CollisionMeshElements;
		const auto MeshFlags = PhysXMesh->getTriangleMeshFlags();
		for (int32 j = 0; j < static_cast<int32>(PhysXMesh->getNbTriangles()); ++j)
		{
			if (MeshFlags | physx::PxTriangleMeshFlag::e16_BIT_INDICES)
			{
				const physx::PxU16* Indices = reinterpret_cast<const physx::PxU16*>(PhysXMesh->getTriangles());
				CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
			}
			else
			{
				const physx::PxU32* Indices = reinterpret_cast<const physx::PxU32*>(PhysXMesh->getTriangles());
				CollisionMeshElements.Add(Chaos::TVector<int32, 3>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
			}
		}
		return CollisionMeshElements;
	}

	template<class PHYSX_MESH>
	TUniquePtr<Chaos::FImplicitObject> ConvertPhysXMeshToLevelset(const PHYSX_MESH* PhysXMesh, const FVector& Scale)
	{
#if WITH_CHAOS && !WITH_CHAOS_NEEDS_TO_BE_FIXED
		TArray<Chaos::TVector<int32, 3>> CollisionMeshElements = GetMeshElements(PhysXMesh);
		Chaos::TParticles<float, 3> CollisionMeshParticles;
		CollisionMeshParticles.AddParticles(PhysXMesh->getNbVertices());
		for (uint32 j = 0; j < CollisionMeshParticles.Size(); ++j)
		{
			const auto& Vertex = PhysXMesh->getVertices()[j];
			CollisionMeshParticles.X(j) = Scale * Chaos::TVector<float, 3>(Vertex.x, Vertex.y, Vertex.z);
		}
		Chaos::TAABB<float, 3> BoundingBox(CollisionMeshParticles.X(0), CollisionMeshParticles.X(0));
		for (uint32 j = 1; j < CollisionMeshParticles.Size(); ++j)
		{
			BoundingBox.GrowToInclude(CollisionMeshParticles.X(j));
		}
#if FORCE_ANALYTICS
		return TUniquePtr<Chaos::FImplicitObject>(new Chaos::TBox<float, 3>(BoundingBox));
#else
		int32 MaxAxisSize = 10;
		int32 MaxAxis;
		const auto Extents = BoundingBox.Extents();
		if (Extents[0] > Extents[1] && Extents[0] > Extents[2])
		{
			MaxAxis = 0;
		}
		else if (Extents[1] > Extents[2])
		{
			MaxAxis = 1;
		}
		else
		{
			MaxAxis = 2;
		}
		Chaos::TVector<int32, 3> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
		Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
		Counts[1] = Counts[1] < 1 ? 1 : Counts[1];
		Counts[2] = Counts[2] < 1 ? 1 : Counts[2];
		Chaos::TUniformGrid<float, 3> Grid(BoundingBox.Min(), BoundingBox.Max(), Counts, 1);
		Chaos::TTriangleMesh<float> CollisionMesh(MoveTemp(CollisionMeshElements));
		return TUniquePtr<Chaos::FImplicitObject>(new Chaos::TLevelSet<float, 3>(Grid, CollisionMeshParticles, CollisionMesh));
#endif

#else
		return TUniquePtr<Chaos::FImplicitObject>();
#endif // !WITH_CHAOS_NEEDS_TO_BE_FIXED

	}

#endif

	Chaos::EChaosCollisionTraceFlag ConvertCollisionTraceFlag(ECollisionTraceFlag Flag)
	{
		if (Flag == ECollisionTraceFlag::CTF_UseDefault)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseDefault;
		if (Flag == ECollisionTraceFlag::CTF_UseSimpleAndComplex)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAndComplex;
		if (Flag == ECollisionTraceFlag::CTF_UseSimpleAsComplex)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAsComplex;
		if (Flag == ECollisionTraceFlag::CTF_UseComplexAsSimple)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple;
		if (Flag == ECollisionTraceFlag::CTF_MAX)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_MAX;
		ensure(false);
		return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseDefault;
	}

	void CreateGeometry(const FGeometryAddParams& InParams, TArray<TUniquePtr<Chaos::FImplicitObject>>& OutGeoms, Chaos::TShapesArray<float, 3>& OutShapes)
	{
		LLM_SCOPE(ELLMTag::ChaosGeometry);
		const FVector& Scale = InParams.Scale;
		TArray<TUniquePtr<Chaos::FImplicitObject>>& Geoms = OutGeoms;
		Chaos::TShapesArray<float, 3>& Shapes = OutShapes;

		ECollisionTraceFlag CollisionTraceType = InParams.CollisionTraceType;
		if (CollisionTraceType == CTF_UseDefault)
		{
			CollisionTraceType = UPhysicsSettings::Get()->DefaultShapeComplexity;
		}

#if WITH_CHAOS
		// Complex as simple should not create simple geometry, unless there is no complex geometry.  Otherwise both get queried against.
		bool bMakeSimpleGeometry = (CollisionTraceType != CTF_UseComplexAsSimple) || (InParams.ChaosTriMeshes.Num() == 0);

		// The reverse is true for Simple as Complex.
		const int32 SimpleShapeCount = InParams.Geometry->SphereElems.Num() + InParams.Geometry->BoxElems.Num() + InParams.Geometry->ConvexElems.Num() + InParams.Geometry->SphylElems.Num();
		bool bMakeComplexGeometry = (CollisionTraceType != CTF_UseSimpleAsComplex) || (SimpleShapeCount == 0);
#else
		bool bMakeSimpleGeometry = true;
		bool bMakeComplexGeometry = true;
#endif

		ensure(bMakeComplexGeometry || bMakeSimpleGeometry);

		auto NewShapeHelper = [&InParams, &CollisionTraceType](Chaos::TSerializablePtr<Chaos::FImplicitObject> InGeom, void* UserData, bool bComplexShape = false)
		{
			auto NewShape = Chaos::TPerShapeData<float, 3>::CreatePerShapeData();
			NewShape->Geometry = InGeom;
			NewShape->QueryData = bComplexShape ? InParams.CollisionData.CollisionFilterData.QueryComplexFilter : InParams.CollisionData.CollisionFilterData.QuerySimpleFilter;
			NewShape->SimData = InParams.CollisionData.CollisionFilterData.SimFilter;
			NewShape->CollisionTraceType = ConvertCollisionTraceFlag(CollisionTraceType);
			NewShape->UpdateShapeBounds(InParams.WorldTransform);
			NewShape->UserData = UserData;
			NewShape->bSimulate = bComplexShape ? InParams.CollisionData.CollisionFlags.bEnableSimCollisionComplex : InParams.CollisionData.CollisionFlags.bEnableSimCollisionSimple;
			return NewShape;
		};

		if (bMakeSimpleGeometry)
		{
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphereElems.Num()); ++i)
			{
				const FKSphereElem& SphereElem = InParams.Geometry->SphereElems[i];
				const FKSphereElem ScaledSphereElem = SphereElem.GetFinalScaled(Scale, InParams.LocalTransform);
				const float UseRadius = FMath::Max(ScaledSphereElem.Radius, KINDA_SMALL_NUMBER);
				auto ImplicitSphere = MakeUnique<Chaos::TSphere<float, 3>>(ScaledSphereElem.Center, UseRadius);
				auto NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere), (void*)SphereElem.GetUserData());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(ImplicitSphere));
			}

			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->BoxElems.Num()); ++i)
			{
				const FKBoxElem& BoxElem = InParams.Geometry->BoxElems[i];
				const FKBoxElem ScaledBoxElem = BoxElem.GetFinalScaled(Scale, InParams.LocalTransform);
				const FTransform& BoxTransform = ScaledBoxElem.GetTransform();
				Chaos::TVector<float, 3> HalfExtents = Chaos::TVector<float, 3>(ScaledBoxElem.X * 0.5f, ScaledBoxElem.Y * 0.5f, ScaledBoxElem.Z * 0.5f);

				HalfExtents.X = FMath::Max(HalfExtents.X, KINDA_SMALL_NUMBER);
				HalfExtents.Y = FMath::Max(HalfExtents.Y, KINDA_SMALL_NUMBER);
				HalfExtents.Z = FMath::Max(HalfExtents.Z, KINDA_SMALL_NUMBER);

				// TAABB can handle translations internally but if we have a rotation we need to wrap it in a transform
				TUniquePtr<Chaos::FImplicitObject> Implicit;
				if (!BoxTransform.GetRotation().IsIdentity())
				{
					auto ImplicitBox = MakeUnique<Chaos::TBox<float, 3>>(-HalfExtents, HalfExtents);
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<float, 3>(MoveTemp(ImplicitBox), BoxTransform));
				}
				else
				{
					Implicit = MakeUnique<Chaos::TBox<float, 3>>(BoxTransform.GetTranslation() - HalfExtents, BoxTransform.GetTranslation() + HalfExtents);
				}

				auto NewShape = NewShapeHelper(MakeSerializable(Implicit), (void*)BoxElem.GetUserData());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(Implicit));
			}
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphylElems.Num()); ++i)
			{
				const FKSphylElem& UnscaledSphyl = InParams.Geometry->SphylElems[i];
				const FKSphylElem ScaledSphylElem = UnscaledSphyl.GetFinalScaled(Scale, InParams.LocalTransform);
				float HalfHeight = FMath::Max(ScaledSphylElem.Length * 0.5f, KINDA_SMALL_NUMBER);
				const float Radius = FMath::Max(ScaledSphylElem.Radius, KINDA_SMALL_NUMBER);

				if (HalfHeight < KINDA_SMALL_NUMBER)
				{
					//not a capsule just use a sphere
					auto ImplicitSphere = MakeUnique<Chaos::TSphere<float, 3>>(ScaledSphylElem.Center, Radius);
					auto NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere), (void*)UnscaledSphyl.GetUserData());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(ImplicitSphere));

				}
				else
				{
					Chaos::TVector<float, 3> HalfExtents = ScaledSphylElem.Rotation.RotateVector(Chaos::TVector<float, 3>(0, 0, HalfHeight));

					auto ImplicitCapsule = MakeUnique<Chaos::TCapsule<float>>(ScaledSphylElem.Center - HalfExtents, ScaledSphylElem.Center + HalfExtents, Radius);
					auto NewShape = NewShapeHelper(MakeSerializable(ImplicitCapsule), (void*)UnscaledSphyl.GetUserData());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(ImplicitCapsule));
				}
			}
#if 0
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->TaperedCapsuleElems.Num()); ++i)
			{
				ensure(FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[1], Scale[2]));
				const auto& TCapsule = InParams.Geometry->TaperedCapsuleElems[i];
				if (TCapsule.Length == 0)
				{
					Chaos::TSphere<float, 3>* ImplicitSphere = new Chaos::TSphere<float, 3>(-half_extents, TCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(ImplicitSphere);
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphere,true,true,InActor });
				}
				else
				{
					Chaos::TVector<float, 3> half_extents(0, 0, TCapsule.Length / 2 * Scale[0]);
					auto ImplicitCylinder = MakeUnique<Chaos::TCylinder<float>>(-half_extents, half_extents, TCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphere));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphere,true,true,InActor });

					auto ImplicitSphereA = MakeUnique<Chaos::TSphere<float, 3>>(-half_extents, TCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphereA));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphereA,true,true,InActor });

					auto ImplicitSphereB = MakeUnique<Chaos::TSphere<float, 3>>(half_extents, TCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphereB));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphereB,true,true,InActor });
				}
			}
#endif
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->ConvexElems.Num()); ++i)
			{
				const FKConvexElem& CollisionBody = InParams.Geometry->ConvexElems[i];
				const FTransform& ConvexTransform = CollisionBody.GetTransform();
				if (const auto& ConvexImplicit = CollisionBody.GetChaosConvexMesh())
				{
					//if (!ConvexTransform.GetTranslation().IsNearlyZero() || !ConvexTransform.GetRotation().IsIdentity())
					//{
					//	TUniquePtr<Chaos::FImplicitObject> TransformImplicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<float, 3>(MakeSerializable(ConvexImplicit), ConvexTransform));
					//	TUniquePtr<Chaos::TImplicitObjectScaled<float, 3, false>> Implicit = MakeUnique<Chaos::TImplicitObjectScaled<float, 3, false>>(MoveTemp(TransformImplicit), Scale);
					//	auto NewShape = NewShapeHelper(MakeSerializable(Implicit));
					//	Shapes.Emplace(MoveTemp(NewShape));
					//	Geoms.Add(MoveTemp(Implicit));
					//}
					//else
					{
						TUniquePtr<Chaos::FImplicitObject> Implicit;
						if (Scale == FVector(1))
						{
							Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectInstanced<Chaos::FConvex>(ConvexImplicit));
						}
						else
						{
							Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FConvex>(ConvexImplicit, Scale));
						}

						auto NewShape = NewShapeHelper(MakeSerializable(Implicit), (void*)CollisionBody.GetUserData());
						Shapes.Emplace(MoveTemp(NewShape));
						Geoms.Add(MoveTemp(Implicit));
					}
				}
			}
		}

		if (bMakeComplexGeometry)
		{
			for (auto& ChaosTriMesh : InParams.ChaosTriMeshes)
			{
				TUniquePtr<Chaos::FImplicitObject> Implicit;
				if (Scale == FVector(1))
				{
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectInstanced<Chaos::FTriangleMeshImplicitObject>(ChaosTriMesh));
				}
				else
				{
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>(ChaosTriMesh, Scale));
				}

				ChaosTriMesh->SetCullsBackFaceRaycast(!InParams.bDoubleSided);

				auto NewShape = NewShapeHelper(MakeSerializable(Implicit), nullptr, true);
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(Implicit));
			}
#endif
		}
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		for (const auto& PhysXMesh : InParams.TriMeshes)
		{
			auto Implicit = ConvertPhysXMeshToLevelset(PhysXMesh, Scale);
			auto NewShape = NewShapeHelper(MakeSerializable(Implicit), nullptr, true);
			Shapes.Emplace(MoveTemp(NewShape));
			Geoms.Add(MoveTemp(Implicit));

		}
#endif
	}

}