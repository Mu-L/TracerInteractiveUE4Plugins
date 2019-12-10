// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

#include "ChaosStats.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsSolver.h"

template< class PARTICLE_TYPE >
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner, FInitialState InInitialState)
	: Base(InOwner)
	, bInitialized(false)
	, InitialState(InInitialState)
	, Particle(InParticle)
	, Handle(InHandle)
{
	BufferedData = Chaos::FMultiBufferFactory< typename PARTICLE_TYPE::FData >::CreateBuffer(Chaos::EMultiBufferMode::Double);
}

template< class PARTICLE_TYPE >
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::~FSingleParticlePhysicsProxy()
{
}

template< class PARTICLE_TYPE >
const FInitialState& 
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::GetInitialState() const 
{ 
	return InitialState; 
}


//
// TGeometryParticle<float, 3> template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TKinematicGeometryParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		if (Data->Geometry && Data->Geometry->HasBoundingBox())
		{
			RigidHandle->SetHasBounds(true);
			RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
			RigidHandle->SetWorldSpaceInflatedBounds(Data->Geometry->BoundingBox().TransformedBox(Chaos::TRigidTransform<float, 3>(Data->X, Data->R)));
		}
		RigidHandle->SetSpatialIdx(Data->SpatialIdx);	//todo: this needs to only happen once during initialization
	}
}



template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}


template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}


template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TGeometryParticle
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ConcreteType()
{
	return EPhysicsProxyType::SingleGeometryParticleType;
}

//
// TKinematicGeometryParticle template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TKinematicGeometryParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TKinematicGeometryParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		RigidHandle->SetV(Data->MV);
		RigidHandle->SetW(Data->MW);
		
		if (Data->Geometry && Data->Geometry->HasBoundingBox())
		{
			RigidHandle->SetHasBounds(true);
			RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
			Chaos::TBox<float, 3> WorldSpaceBox = Data->Geometry->BoundingBox().TransformedBox(Chaos::TRigidTransform<float, 3>(Data->X, Data->R));
			WorldSpaceBox.ThickenSymmetrically(Data->MV);
			RigidHandle->SetWorldSpaceInflatedBounds(WorldSpaceBox);
		}
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TKinematicGeometryParticle
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ConcreteType()
{
	return EPhysicsProxyType::SingleKinematicParticleType;
}


//
// TPBDRigidParticle template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TPBDRigidParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TPBDRigidParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		RigidHandle->SetV(Data->MV);
		RigidHandle->SetW(Data->MW);
		RigidHandle->SetM(Data->MM);
		RigidHandle->SetInvM(Data->MInvM);
		RigidHandle->SetI(Data->MI);
		RigidHandle->SetInvI(Data->MInvI);
		RigidHandle->SetExternalForce(Data->MExternalForce);
		RigidHandle->SetExternalTorque(Data->MExternalTorque);
		GetSolver()->GetEvolution()->GetGravityForces().SetEnabled(*RigidHandle, Data->MGravityEnabled);

		if (Data->Geometry && Data->Geometry->HasBoundingBox())
		{
			RigidHandle->SetHasBounds(true);
			RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
			Chaos::TBox<float, 3> WorldSpaceBox = Data->Geometry->BoundingBox().TransformedBox(Chaos::TRigidTransform<float, 3>(Data->X, Data->R));
			WorldSpaceBox.ThickenSymmetrically(Data->MV);
			RigidHandle->SetWorldSpaceInflatedBounds(WorldSpaceBox);
		}
		else
		{
			//todo: compute bounds based on sample points
		}
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->SetExternalForce(Chaos::TVector<float, 3>(0),false);
	Particle->SetExternalTorque(Chaos::TVector<float, 3>(0),false);
	Particle->ClearDirtyFlags();
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
	if (Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = static_cast<Chaos::TPBDRigidParticleHandle<float, 3>*>(Handle))
	{
		Chaos::TPBDRigidParticleData<float, 3>* Buffer = BufferedData->AccessProducerBuffer();
		Buffer->X = RigidHandle->X();
		Buffer->R = RigidHandle->R();
		Buffer->MV = RigidHandle->V();
		Buffer->MW = RigidHandle->W();
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TPBDRigidParticle without triggering invalidation of the physics state.
	if (Particle)
	{
		const Chaos::TPBDRigidParticleData<float, 3>* Buffer = BufferedData->GetConsumerBuffer();
		Particle->SetX(Buffer->X, false);
		Particle->SetR(Buffer->R, false);
		Particle->SetV(Buffer->MV, false);
		Particle->SetW(Buffer->MW, false);
	}
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
EPhysicsProxyType  FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ConcreteType() 
{ 
	return EPhysicsProxyType::SingleRigidParticleType;
}

template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;
