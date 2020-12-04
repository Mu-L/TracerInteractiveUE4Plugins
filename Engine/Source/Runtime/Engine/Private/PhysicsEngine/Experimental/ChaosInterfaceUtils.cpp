// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/CastingUtilities.h"
#include "Chaos/Convex.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Levelset.h"
#include "Chaos/Sphere.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/UniformGrid.h"

#include "Physics/PhysicsInterfaceTypes.h"

#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/SphylElem.h"
#include "PhysicsEngine/SphereElem.h"

#if PHYSICS_INTERFACE_PHYSX
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

#if PHYSICS_INTERFACE_PHYSX

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

	void CreateGeometry(const FGeometryAddParams& InParams, TArray<TUniquePtr<Chaos::FImplicitObject>>& OutGeoms, Chaos::FShapesArray& OutShapes)
	{
		LLM_SCOPE(ELLMTag::ChaosGeometry);
		const FVector& Scale = InParams.Scale;
		TArray<TUniquePtr<Chaos::FImplicitObject>>& Geoms = OutGeoms;
		Chaos::FShapesArray& Shapes = OutShapes;

		ECollisionTraceFlag CollisionTraceType = InParams.CollisionTraceType;
		if (CollisionTraceType == CTF_UseDefault)
		{
			CollisionTraceType = UPhysicsSettings::Get()->DefaultShapeComplexity;
		}

		const float CollisionMarginFraction = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginFraction);
		const float CollisionMarginMax = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginMax);

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

		auto NewShapeHelper = [&InParams, &CollisionTraceType](Chaos::TSerializablePtr<Chaos::FImplicitObject> InGeom, int32 ShapeIdx, void* UserData, ECollisionEnabled::Type ShapeCollisionEnabled, bool bComplexShape = false)
		{
			TUniquePtr<Chaos::FPerShapeData> NewShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeIdx);
			NewShape->SetGeometry(InGeom);
			NewShape->SetQueryData(bComplexShape ? InParams.CollisionData.CollisionFilterData.QueryComplexFilter : InParams.CollisionData.CollisionFilterData.QuerySimpleFilter);
			NewShape->SetSimData(InParams.CollisionData.CollisionFilterData.SimFilter);
			NewShape->SetCollisionTraceType(ConvertCollisionTraceFlag(CollisionTraceType));
			NewShape->UpdateShapeBounds(InParams.WorldTransform);
			NewShape->SetUserData(UserData);

			// The following does nearly the same thing that happens in UpdatePhysicsFilterData.
			// TODO: Refactor so that this code is not duplicated
			const bool bBodyEnableSim = InParams.CollisionData.CollisionFlags.bEnableSimCollisionSimple || InParams.CollisionData.CollisionFlags.bEnableSimCollisionComplex;
			const bool bBodyEnableQuery = InParams.CollisionData.CollisionFlags.bEnableQueryCollision;
			const bool bShapeEnableSim = ShapeCollisionEnabled == ECollisionEnabled::QueryAndPhysics || ShapeCollisionEnabled == ECollisionEnabled::PhysicsOnly;
			const bool bShapeEnableQuery = ShapeCollisionEnabled == ECollisionEnabled::QueryAndPhysics || ShapeCollisionEnabled == ECollisionEnabled::QueryOnly;
			NewShape->SetSimEnabled(bBodyEnableSim && bShapeEnableSim);
			NewShape->SetQueryEnabled(bBodyEnableQuery && bShapeEnableQuery);

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
				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere), Shapes.Num(), (void*)SphereElem.GetUserData(), SphereElem.GetCollisionEnabled());
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

				const float CollisionMargin = FMath::Min(2.0f * HalfExtents.GetAbsMax() * CollisionMarginFraction, CollisionMarginMax);

				// TAABB can handle translations internally but if we have a rotation we need to wrap it in a transform
				TUniquePtr<Chaos::FImplicitObject> Implicit;
				if (!BoxTransform.GetRotation().IsIdentity())
				{
					auto ImplicitBox = MakeUnique<Chaos::TBox<float, 3>>(-HalfExtents, HalfExtents, CollisionMargin);
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<float, 3>(MoveTemp(ImplicitBox), BoxTransform));
				}
				else
				{
					Implicit = MakeUnique<Chaos::TBox<float, 3>>(BoxTransform.GetTranslation() - HalfExtents, BoxTransform.GetTranslation() + HalfExtents, CollisionMargin);
				}

				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit),Shapes.Num(), (void*)BoxElem.GetUserData(), BoxElem.GetCollisionEnabled());
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
					TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere),Shapes.Num(), (void*)UnscaledSphyl.GetUserData(), UnscaledSphyl.GetCollisionEnabled());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(ImplicitSphere));

				}
				else
				{
					Chaos::TVector<float, 3> HalfExtents = ScaledSphylElem.Rotation.RotateVector(Chaos::TVector<float, 3>(0, 0, HalfHeight));

					auto ImplicitCapsule = MakeUnique<Chaos::TCapsule<float>>(ScaledSphylElem.Center - HalfExtents, ScaledSphylElem.Center + HalfExtents, Radius);
					TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitCapsule),Shapes.Num(), (void*)UnscaledSphyl.GetUserData(), UnscaledSphyl.GetCollisionEnabled());
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
				const FTransform& ConvexTransform = InParams.LocalTransform;
				if (const auto& ConvexImplicit = CollisionBody.GetChaosConvexMesh())
				{
					const float CollisionMargin = FMath::Min(CollisionBody.ElemBox.GetSize().GetMax() * CollisionMarginFraction, CollisionMarginMax);

					if (!ConvexTransform.GetTranslation().IsNearlyZero() || !ConvexTransform.GetRotation().IsIdentity())
					{
						// this path is taken when objects are welded
						//TUniquePtr<Chaos::FImplicitObject> ScaledImplicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FImplicitObject>(MakeSerializable(ConvexImplicit), Scale, CollisionMargin));
						TUniquePtr<Chaos::FImplicitObject> Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<float, 3>(MakeSerializable(ConvexImplicit), ConvexTransform));
						TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit), Shapes.Num(), (void*)CollisionBody.GetUserData(), CollisionBody.GetCollisionEnabled());
						Shapes.Emplace(MoveTemp(NewShape));
						Geoms.Add(MoveTemp(Implicit));
					}
					else
					{
						// NOTE: CollisionMargin is on the Instance/Scaled wrapper, not the inner convex (which has no margin). This means that convex shapes grow by the margion size...
						TUniquePtr<Chaos::FImplicitObject> Implicit;
						if (Scale == FVector(1))
						{
							Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectInstanced<Chaos::FConvex>(ConvexImplicit, CollisionMargin));
						}
						else
						{
							Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FConvex>(ConvexImplicit, Scale, CollisionMargin));
						}

						TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit),Shapes.Num(), (void*)CollisionBody.GetUserData(), CollisionBody.GetCollisionEnabled());
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

				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit),Shapes.Num(), nullptr, ECollisionEnabled::QueryAndPhysics, true);
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(Implicit));
			}
#endif
		}
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		for (const auto& PhysXMesh : InParams.TriMeshes)
		{
			auto Implicit = ConvertPhysXMeshToLevelset(PhysXMesh, Scale);
			auto NewShape = NewShapeHelper(MakeSerializable(Implicit), Shapes.Num(), nullptr, ECollisionEnabled::QueryAndPhysics, true);
			Shapes.Emplace(MoveTemp(NewShape));
			Geoms.Add(MoveTemp(Implicit));

		}
#endif
	}

#if WITH_CHAOS
	bool CalculateMassPropertiesOfImplicitType(
		Chaos::TMassProperties<float, 3>& OutMassProperties,
		const Chaos::TRigidTransform<float, 3>& WorldTransform,
		const Chaos::FImplicitObject* ImplicitObject,
		float InDensityKGPerCM)
	{
		// WIP
		// @todo : Support center of mass offsets.
		// @todo : Support Mass space alignment. 

		using namespace Chaos;

		if (ImplicitObject)
		{
			// Hack to handle Transformed and Scaled<ImplicitObjectTriangleMesh> until CastHelper can properly support transformed
			// Commenting this out temporarily as it breaks vehicles
			/*	if (Chaos::IsScaled(ImplicitObject->GetType(true)) && Chaos::GetInnerType(ImplicitObject->GetType(true)) & Chaos::ImplicitObjectType::TriangleMesh)
				{
					OutMassProperties.Volume = 0.f;
					OutMassProperties.Mass = FLT_MAX;
					OutMassProperties.InertiaTensor = FMatrix33(0, 0, 0);
					OutMassProperties.CenterOfMass = FVector(0);
					OutMassProperties.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
					return false;
				}
				else if (ImplicitObject->GetType(true) & Chaos::ImplicitObjectType::TriangleMesh)
				{
					OutMassProperties.Volume = 0.f;
					OutMassProperties.Mass = FLT_MAX;
					OutMassProperties.InertiaTensor = FMatrix33(0, 0, 0);
					OutMassProperties.CenterOfMass = FVector(0);
					OutMassProperties.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
					return false;
				}
			else*/

			//todo: Still need to handle scaled
			Chaos::Utilities::CastHelper(*ImplicitObject, FTransform::Identity, [&OutMassProperties, InDensityKGPerCM](const auto& Object, const auto& LocalTM)
				{
					OutMassProperties.Volume = Object.GetVolume();
					OutMassProperties.Mass = OutMassProperties.Volume * InDensityKGPerCM;
					OutMassProperties.InertiaTensor = Object.GetInertiaTensor(OutMassProperties.Mass);
					OutMassProperties.CenterOfMass = LocalTM.TransformPosition(Object.GetCenterOfMass());
					OutMassProperties.RotationOfMass = LocalTM.GetRotation();
				});
			return true;
		}
		return false;
	}

	void CalculateMassPropertiesFromShapeCollection(Chaos::TMassProperties<float, 3>& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM)
	{
		float TotalMass = 0.f;
		Chaos::FVec3 TotalCenterOfMass(0.f);
		TArray< Chaos::TMassProperties<float, 3> > MassPropertiesList;
		for (const FPhysicsShapeHandle& ShapeHandle : InShapes)
		{
			if (const Chaos::FPerShapeData* Shape = ShapeHandle.Shape)
			{
				if (const Chaos::FImplicitObject* ImplicitObject = Shape->GetGeometry().Get())
				{
					FTransform WorldTransform(ShapeHandle.ActorRef->R(), ShapeHandle.ActorRef->X());
					Chaos::TMassProperties<float, 3> MassProperties;
					if (CalculateMassPropertiesOfImplicitType(MassProperties, WorldTransform, ImplicitObject, InDensityKGPerCM))
					{
						MassPropertiesList.Add(MassProperties);
						TotalMass += MassProperties.Mass;
						TotalCenterOfMass += MassProperties.CenterOfMass * MassProperties.Mass;
					}
				}
			}
		}

		if (TotalMass > 0.f)
		{
			TotalCenterOfMass /= TotalMass;
		}

		Chaos::PMatrix<float, 3, 3> Tensor;
		if (MassPropertiesList.Num())
		{
			Tensor = Chaos::CombineWorldSpace<float, 3>(MassPropertiesList, InDensityKGPerCM).InertiaTensor;
		}
		else
		{
			// @todo : Add support for all types, but for now just hard code a unit sphere tensor {r:50cm} if the type was not processed
			Tensor = Chaos::PMatrix<float, 3, 3>(5.24e5, 5.24e5, 5.24e5);
			TotalMass = 523.f;
		}

		OutProperties.InertiaTensor = Tensor;
		OutProperties.Mass = TotalMass;
		OutProperties.CenterOfMass = TotalCenterOfMass;
	}

	void CalculateMassPropertiesFromShapeCollection(Chaos::TMassProperties<float, 3>& OutProperties, const Chaos::FShapesArray& InShapes, const TArray<bool>& bContributesToMass, float InDensityKGPerCM)
	{
		float TotalMass = 0.f;
		Chaos::FVec3 TotalCenterOfMass(0.f);
		TArray< Chaos::TMassProperties<float, 3> > MassPropertiesList;
		for (int32 ShapeIndex = 0; ShapeIndex < InShapes.Num(); ++ShapeIndex)
		{
			const TUniquePtr<Chaos::FPerShapeData>& Shape = InShapes[ShapeIndex];
			const bool bHassMass = (ShapeIndex < bContributesToMass.Num())? bContributesToMass[ShapeIndex] : true;
			if (bHassMass)
			{
				if (const Chaos::FImplicitObject* ImplicitObject = Shape->GetGeometry().Get())
				{
					Chaos::TMassProperties<float, 3> MassProperties;
					if (CalculateMassPropertiesOfImplicitType(MassProperties, FTransform::Identity, ImplicitObject, InDensityKGPerCM))
					{
						MassPropertiesList.Add(MassProperties);
						TotalMass += MassProperties.Mass;
						TotalCenterOfMass += MassProperties.CenterOfMass * MassProperties.Mass;
					}
				}
			}
		}

		if (TotalMass > 0.f)
		{
			TotalCenterOfMass /= TotalMass;
		}

		Chaos::PMatrix<float, 3, 3> Tensor;
		if (MassPropertiesList.Num())
		{
			Tensor = Chaos::CombineWorldSpace<float, 3>(MassPropertiesList, InDensityKGPerCM).InertiaTensor;
		}
		else
		{
			// @todo : Add support for all types, but for now just hard code a unit sphere tensor {r:50cm} if the type was not processed
			Tensor = Chaos::PMatrix<float, 3, 3>(5.24e5f, 5.24e5f, 5.24e5f);
			TotalMass = 523.0f;
		}

		OutProperties.InertiaTensor = Tensor;
		OutProperties.Mass = TotalMass;
		OutProperties.CenterOfMass = TotalCenterOfMass;
	}

#endif // WITH_CHAOS

}