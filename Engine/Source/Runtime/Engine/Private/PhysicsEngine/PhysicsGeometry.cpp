// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

#include "Physics/PhysicsGeometry.h"

#include "PhysicsEngine/AggregateGeom.h"

//#include "PhysXPublic.h"
#include "PhysicsEngine/PhysXSupport.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Engine/Engine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "PxSphereGeometry.h"

using namespace physx;

extern TAutoConsoleVariable<float> CVarContactOffsetFactor;
extern TAutoConsoleVariable<float> CVarMaxContactOffset;

void FBodySetupShapeIterator::GetContactOffsetParams(float& InOutContactOffsetFactor, float& InOutMinContactOffset, float& InOutMaxContactOffset)
{
	// Get contact offset params
	InOutContactOffsetFactor = CVarContactOffsetFactor.GetValueOnAnyThread();
	InOutMaxContactOffset = CVarMaxContactOffset.GetValueOnAnyThread();

	InOutContactOffsetFactor = InOutContactOffsetFactor < 0.f ? UPhysicsSettings::Get()->ContactOffsetMultiplier : InOutContactOffsetFactor;
	InOutMaxContactOffset = InOutMaxContactOffset < 0.f ? UPhysicsSettings::Get()->MaxContactOffset : InOutMaxContactOffset;

	InOutMinContactOffset = UPhysicsSettings::Get()->MinContactOffset;
}

FBodySetupShapeIterator::FBodySetupShapeIterator(const FVector& InScale3D, const FTransform& InRelativeTM, bool bDoubleSidedTrimeshes)
	: Scale3D(InScale3D)
	, RelativeTM(InRelativeTM)
	, bDoubleSidedTriMeshGeo(bDoubleSidedTrimeshes)
{
	SetupNonUniformHelper(Scale3D, MinScale, MinScaleAbs, ShapeScale3DAbs);
	{
		float MinScaleRelative;
		float MinScaleAbsRelative;
		FVector Scale3DAbsRelative;
		FVector Scale3DRelative = RelativeTM.GetScale3D();

		SetupNonUniformHelper(Scale3DRelative, MinScaleRelative, MinScaleAbsRelative, Scale3DAbsRelative);

		MinScaleAbs *= MinScaleAbsRelative;
		ShapeScale3DAbs.X *= Scale3DAbsRelative.X;
		ShapeScale3DAbs.Y *= Scale3DAbsRelative.Y;
		ShapeScale3DAbs.Z *= Scale3DAbsRelative.Z;

		ShapeScale3D = Scale3D;
		ShapeScale3D.X *= Scale3DAbsRelative.X;
		ShapeScale3D.Y *= Scale3DAbsRelative.Y;
		ShapeScale3D.Z *= Scale3DAbsRelative.Z;
	}

	GetContactOffsetParams(ContactOffsetFactor, MinContactOffset, MaxContactOffset);
}

FBodySetupShapeIterator::FBodySetupShapeIterator()
	: Scale3D(FVector::OneVector)
	, RelativeTM(FTransform::Identity)
	, bDoubleSidedTriMeshGeo(false)
{

}

template <typename ElemType, typename GeomType>
void FBodySetupShapeIterator::ForEachShape(const TArrayView<ElemType>& Elements, TFunctionRef<void(const ElemType& Elem, const GeomType& Geom, const FBodySetupShapeIterator::TTransform& LocalPose, float ContactOffset, float RestOffset)> VisitorFunc) const
{
	for(int32 ElemIdx = 0; ElemIdx < Elements.Num(); ElemIdx++)
	{
		const ElemType& Elem = Elements[ElemIdx];
		GeomType Geom;
		PxTransform PLocalPose;

		if(PopulateGeometryAndTransform(Elem, Geom, PLocalPose))
		{
			const float RestOffset = ComputeRestOffset(Elem);
			const float ContactOffset = ComputeContactOffset(Geom) + RestOffset;
			VisitorFunc(Elem, Geom, PLocalPose, ContactOffset, RestOffset);
		}
		else
		{
			UE_LOG(LogPhysics, Warning, TEXT("ForeachShape(%s): ScaledElem[%d] invalid"), *GetDebugName<ElemType>(), ElemIdx);
		}
	}
}


//////////////////////// Sphere elements ////////////////////////
template <> bool FBodySetupShapeIterator::PopulateGeometryAndTransform(const FKSphereElem& SphereElem, PxSphereGeometry& OutGeometry, FBodySetupShapeIterator::TTransform& OutTM) const
{
	const FKSphereElem ScaledSphereElem = SphereElem.GetFinalScaled(Scale3D, RelativeTM);
	OutGeometry.radius = FMath::Max(ScaledSphereElem.Radius, KINDA_SMALL_NUMBER);

	if(ensure(OutGeometry.isValid()))
	{
		OutTM = PxTransform(U2PVector(ScaledSphereElem.Center));
		return true;
	}
	else
	{
		return false;
	}
}

template <> float FBodySetupShapeIterator::ComputeContactOffset(const PxSphereGeometry& PSphereGeom) const
{
	return FMath::Clamp(ContactOffsetFactor * PSphereGeom.radius, MinContactOffset, MaxContactOffset);
}

template <> FString FBodySetupShapeIterator::GetDebugName<FKSphereElem>()  const
{
	return TEXT("Sphere");
}

template <typename ElemType> float FBodySetupShapeIterator::ComputeRestOffset(const ElemType& Elem) const
{
	return Elem.RestOffset;
}

template <> float FBodySetupShapeIterator::ComputeRestOffset(physx::PxTriangleMesh* const&) const
{
	return 0.f;
}

/////////////////// Box elements //////////////////////////////
template <> bool FBodySetupShapeIterator::PopulateGeometryAndTransform(const FKBoxElem& BoxElem, PxBoxGeometry& OutGeometry, FBodySetupShapeIterator::TTransform& OutTM) const
{
	const FKBoxElem ScaledBoxElem = BoxElem.GetFinalScaled(Scale3D, RelativeTM);
	const FTransform& BoxTransform = ScaledBoxElem.GetTransform();

	OutGeometry.halfExtents.x = FMath::Max(ScaledBoxElem.X * 0.5f, KINDA_SMALL_NUMBER);
	OutGeometry.halfExtents.y = FMath::Max(ScaledBoxElem.Y * 0.5f, KINDA_SMALL_NUMBER);
	OutGeometry.halfExtents.z = FMath::Max(ScaledBoxElem.Z * 0.5f, KINDA_SMALL_NUMBER);

	if(OutGeometry.isValid() && BoxTransform.IsValid())
	{
		OutTM = U2PTransform(BoxTransform);
		if(ensure(OutTM.isValid()))
		{
			return true;
		}
	}

	return false;
}

template <> float FBodySetupShapeIterator::ComputeContactOffset(const PxBoxGeometry& PBoxGeom) const
{
	return FMath::Clamp(ContactOffsetFactor * PBoxGeom.halfExtents.minElement(), MinContactOffset, MaxContactOffset);
}

template <> FString FBodySetupShapeIterator::GetDebugName<FKBoxElem>()  const
{
	return TEXT("Box");
}

/////////////////////// Capsule elements /////////////////////////////
template <> bool FBodySetupShapeIterator::PopulateGeometryAndTransform(const FKSphylElem& SphylElem, PxCapsuleGeometry& OutGeometry, FBodySetupShapeIterator::TTransform& OutTM) const
{
	const FKSphylElem ScaledSphylElem = SphylElem.GetFinalScaled(Scale3D, RelativeTM);

	OutGeometry.halfHeight = FMath::Max(ScaledSphylElem.Length * 0.5f, KINDA_SMALL_NUMBER);
	OutGeometry.radius = FMath::Max(ScaledSphylElem.Radius, KINDA_SMALL_NUMBER);

	if(OutGeometry.isValid())
	{
		// The stored capsule transform assumes the capsule axis is down Z. In PhysX, it points down X, so we twiddle the matrix a bit here (swap X and Z and negate Y).
		OutTM = PxTransform(U2PVector(ScaledSphylElem.Center), U2PQuat(ScaledSphylElem.Rotation.Quaternion()) * U2PSphylBasis);

		if(ensure(OutTM.isValid()))
		{
			return true;
		}
	}

	return false;
}

template <> float FBodySetupShapeIterator::ComputeContactOffset(const PxCapsuleGeometry& PCapsuleGeom) const
{
	return FMath::Clamp(ContactOffsetFactor * PCapsuleGeom.radius, MinContactOffset, MaxContactOffset);
}

template <> FString FBodySetupShapeIterator::GetDebugName<FKSphylElem>() const
{
	return TEXT("Capsule");
}

////////////////////////////// Convex elements ////////////////////////////
template <> bool FBodySetupShapeIterator::PopulateGeometryAndTransform(const FKConvexElem& ConvexElem, PxConvexMeshGeometry& OutGeometry, FBodySetupShapeIterator::TTransform& OutTM) const
{
	FTransform NegativeScaleCompensation;
	const bool bUseNegX = CalcMeshNegScaleCompensation(Scale3D * RelativeTM.GetScale3D(), NegativeScaleCompensation);
	OutTM = U2PTransform(NegativeScaleCompensation);

	PxConvexMesh* UseConvexMesh = bUseNegX ? ConvexElem.GetMirroredConvexMesh() : ConvexElem.GetConvexMesh();
	if(UseConvexMesh)
	{
		OutGeometry.convexMesh = UseConvexMesh;
		OutGeometry.scale.scale = U2PVector(ShapeScale3DAbs);	//scale shape about the origin

																													//Scale the position independent of shape scale. This is because physx transforms have no concept of scale
		PxTransform PElementTransform = U2PTransform(RelativeTM);
		OutTM.q *= PElementTransform.q;
		OutTM.p = PElementTransform.p;
		OutTM.p.x *= Scale3D.X;
		OutTM.p.y *= Scale3D.Y;
		OutTM.p.z *= Scale3D.Z;

		if(OutGeometry.isValid())
		{
			PxVec3 PBoundsExtents = OutGeometry.convexMesh->getLocalBounds().getExtents();

			if(ensure(OutTM.isValid()))
			{
				return true;
			}
			else
			{
				UE_LOG(LogPhysics, Warning, TEXT("PopulatePhysXGeometryAndTransform(Convex): ConvexElem invalid"));
			}
		}
		else
		{
			UE_LOG(LogPhysics, Warning, TEXT("PopulatePhysXGeometryAndTransform(Convex): ConvexElem has invalid transform"));
		}
	}
	else
	{
		UE_LOG(LogPhysics, Warning, TEXT("PopulatePhysXGeometryAndTransform(Convex): ConvexElem is missing ConvexMesh"));
	}

	return false;
}

template <> float FBodySetupShapeIterator::ComputeContactOffset(const PxConvexMeshGeometry& PConvexGeom) const
{
	PxVec3 PBoundsExtents = PConvexGeom.convexMesh->getLocalBounds().getExtents();
	return FMath::Clamp(ContactOffsetFactor * PBoundsExtents.minElement(), MinContactOffset, MaxContactOffset);
}

template <> FString FBodySetupShapeIterator::GetDebugName<FKConvexElem>() const
{
	return TEXT("Convex");
}


///////////////////////////////////Trimesh elements ////////////////////////////
template <> bool FBodySetupShapeIterator::PopulateGeometryAndTransform(PxTriangleMesh* const & TriMesh, PxTriangleMeshGeometry& OutGeometry, FBodySetupShapeIterator::TTransform& OutTM) const
{
	OutGeometry.triangleMesh = TriMesh;
	OutGeometry.scale.scale = U2PVector(ShapeScale3D); //scale shape about the origin

	auto ClampScale = [](float& Val) -> void
	{
		Val = Val <= 0.f ? FMath::Min(Val, -KINDA_SMALL_NUMBER) : FMath::Max(KINDA_SMALL_NUMBER, Val);
	};

	ClampScale(OutGeometry.scale.scale.x);
	ClampScale(OutGeometry.scale.scale.y);
	ClampScale(OutGeometry.scale.scale.z);

	if(bDoubleSidedTriMeshGeo)
	{
		OutGeometry.meshFlags |= PxMeshGeometryFlag::eDOUBLE_SIDED;
	}

	if(OutGeometry.isValid())
	{
		//Scale the position independent of shape scale. This is because physx transforms have no concept of scale
		OutTM = U2PTransform(RelativeTM);
		OutTM.p.x *= Scale3D.X;
		OutTM.p.y *= Scale3D.Y;
		OutTM.p.z *= Scale3D.Z;

		return true;
	}
	else
	{
		UE_LOG(LogPhysics, Log, TEXT("PopulatePhysXGeometryAndTransform(TriMesh): TriMesh invalid"));
	}

	return false;
}

template <> float FBodySetupShapeIterator::ComputeContactOffset(const PxTriangleMeshGeometry& PTriMeshGeom) const
{
	return MaxContactOffset;
}

template <> FString FBodySetupShapeIterator::GetDebugName<PxTriangleMesh*>() const
{
	return TEXT("Trimesh");
}

template void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKSphereElem>&, TFunctionRef<void(const FKSphereElem&, const physx::PxSphereGeometry&, const physx::PxTransform&, float, float)>) const;
template void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKBoxElem>&, TFunctionRef<void(const FKBoxElem&, const physx::PxBoxGeometry&, const physx::PxTransform&, float, float)>) const;
template void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKSphylElem>&, TFunctionRef<void(const FKSphylElem&, const physx::PxCapsuleGeometry&, const physx::PxTransform&, float, float)>) const;
template void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKConvexElem>&, TFunctionRef<void(const FKConvexElem&, const physx::PxConvexMeshGeometry&, const physx::PxTransform&, float, float)>) const;
template void FBodySetupShapeIterator::ForEachShape(const TArrayView<physx::PxTriangleMesh*>&, TFunctionRef<void(physx::PxTriangleMesh* const &, const physx::PxTriangleMeshGeometry&, const physx::PxTransform&, float, float)>) const;

#endif