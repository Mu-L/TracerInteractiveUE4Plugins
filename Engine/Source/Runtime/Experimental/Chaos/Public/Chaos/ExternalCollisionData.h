// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Box.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"


class UPhysicalMaterial;

namespace Chaos
{
	/**
	 * Collision event data stored for use by other systems (e.g. Niagara, gameplay events)
	 */
	template<class T, int d>
	struct TCollisionData
	{
		TCollisionData()
			: Location(TVector<T, d>((T)0.0))
			, AccumulatedImpulse(TVector<T, d>((T)0.0))
			, Normal(TVector<T, d>((T)0.0))
			, Velocity1(TVector<T, d>((T)0.0))
			, Velocity2(TVector<T, d>((T)0.0))
		    , DeltaVelocity1(TVector<T, d>((T)0.0))
		    , DeltaVelocity2(TVector<T, d>((T)0.0))
			, AngularVelocity1(TVector<T, d>((T)0.0))
			, AngularVelocity2(TVector<T, d>((T)0.0))
			, Mass1((T)0.0)
			, Mass2((T)0.0)
			, PenetrationDepth((T)0.0)
			, Particle(nullptr)
			, Levelset(nullptr)
			, ParticleProxy(nullptr)
		    , LevelsetProxy(nullptr)
		{}

		TCollisionData(TVector<T, d> InLocation, TVector<T, d> InAccumulatedImpulse, TVector<T, d> InNormal, TVector<T, d> InVelocity1, TVector<T, d> InVelocity2, TVector<T, d> InDeltaVelocity1, TVector<T, d> InDeltaVelocity2
		, TVector<T, d> InAngularVelocity1, TVector<T, d> InAngularVelocity2, T InMass1, T InMass2, T InPenetrationDepth, TGeometryParticle<T, d>* InParticle
		, TGeometryParticle<T, d>* InLevelset, IPhysicsProxyBase* InParticleProxy, IPhysicsProxyBase* InLevelsetProxy)
		    : Location(InLocation)
			, AccumulatedImpulse(InAccumulatedImpulse)
			, Normal(InNormal)
			, Velocity1(InVelocity1)
			, Velocity2(InVelocity2)
			, DeltaVelocity1(InDeltaVelocity1)
			, DeltaVelocity2(InDeltaVelocity2)
			, AngularVelocity1(InAngularVelocity1)
			, AngularVelocity2(InAngularVelocity2)
			, Mass1(InMass1)
			, Mass2(InMass2)
			, PenetrationDepth(InPenetrationDepth)
			, Particle(InParticle)
			, Levelset(InLevelset)
			, ParticleProxy(InParticleProxy)
		    , LevelsetProxy(InLevelsetProxy)
		{}

		bool IsValid() { return (ParticleProxy && LevelsetProxy); }

		TVector<T, d> Location;
		TVector<T, d> AccumulatedImpulse;
		TVector<T, d> Normal;
		TVector<T, d> Velocity1, Velocity2;
		TVector<T, d> DeltaVelocity1, DeltaVelocity2;
		TVector<T, d> AngularVelocity1, AngularVelocity2;
		T Mass1, Mass2;
		T PenetrationDepth;
		TGeometryParticle<T, d>* Particle;
		TGeometryParticle<T, d>* Levelset;
		IPhysicsProxyBase* ParticleProxy;
		IPhysicsProxyBase* LevelsetProxy;
	};

	/*
	CollisionData used in the subsystems
	*/
	template<class T, int d>
	struct TCollisionDataExt
	{
		TCollisionDataExt()
			: Location(TVector<T, d>((T)0.0))
			, AccumulatedImpulse(TVector<T, d>((T)0.0))
			, Normal(TVector<T, d>((T)0.0))
			, Velocity1(TVector<T, d>((T)0.0))
			, Velocity2(TVector<T, d>((T)0.0))
			, AngularVelocity1(TVector<T, d>((T)0.0))
			, AngularVelocity2(TVector<T, d>((T)0.0))
			, Mass1((T)0.0)
			, Mass2((T)0.0)
			, Particle(nullptr)
			, Levelset(nullptr)
		    , ParticleProxy(nullptr)
		    , LevelsetProxy(nullptr)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{}

		TCollisionDataExt(
		    TVector<T, d> InLocation, TVector<T, d> InAccumulatedImpulse, TVector<T, d> InNormal, TVector<T, d> InVelocity1, TVector<T, d> InVelocity2
			, TVector<T, d> InAngularVelocity1, TVector<T, d> InAngularVelocity2, T InMass1, T InMass2, TGeometryParticle<T, d>* InParticle
			, TGeometryParticle<T, d>* InLevelset, IPhysicsProxyBase* InParticleProxy, IPhysicsProxyBase* InLevelsetProxy
			, float InBoundingboxVolume, float InBoundingboxExtentMin, float InBoundingboxExtentMax, int32 InSurfaceType)
			: Location(InLocation)
			, AccumulatedImpulse(InAccumulatedImpulse)
			, Normal(InNormal)
			, Velocity1(InVelocity1)
			, Velocity2(InVelocity2)
			, AngularVelocity1(InAngularVelocity1)
			, AngularVelocity2(InAngularVelocity2)
			, Mass1(InMass1)
			, Mass2(InMass2)
			, Particle(InParticle)
			, Levelset(InLevelset)
		    , ParticleProxy(InParticleProxy)
		    , LevelsetProxy(InLevelsetProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		TCollisionDataExt(const TCollisionData<T, d>& InCollisionData)
			: Location(InCollisionData.Location)
			, AccumulatedImpulse(InCollisionData.AccumulatedImpulse)
			, Normal(InCollisionData.Normal)
			, Velocity1(InCollisionData.Velocity1)
			, Velocity2(InCollisionData.Velocity2)
			, AngularVelocity1(InCollisionData.AngularVelocity1)
			, AngularVelocity2(InCollisionData.AngularVelocity2)
			, Mass1(InCollisionData.Mass1)
			, Mass2(InCollisionData.Mass2)
			, Particle(InCollisionData.Particle)
			, Levelset(InCollisionData.Levelset)
		    , ParticleProxy(InCollisionData.ParticleProxy)
		    , LevelsetProxy(InCollisionData.LevelsetProxy)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{
		}

		TVector<T, d> Location;
		TVector<T, d> AccumulatedImpulse;
		TVector<T, d> Normal;
		TVector<T, d> Velocity1, Velocity2;
		TVector<T, d> AngularVelocity1, AngularVelocity2;
		T Mass1, Mass2;
		TGeometryParticle<T, d>* Particle;
		TGeometryParticle<T, d>* Levelset;
		IPhysicsProxyBase* ParticleProxy;
		IPhysicsProxyBase* LevelsetProxy;
		float BoundingboxVolume;
		float BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;
	};

	/*
	BreakingData passed from the physics solver to subsystems
	*/
	template<class T, int d>
	struct TBreakingData
	{
		TBreakingData()
			: Particle(nullptr)
		    , ParticleProxy(nullptr)
			, Location(TVector<T, d>((T)0.0))
			, Velocity(TVector<T, d>((T)0.0))
			, AngularVelocity(TVector<T, d>((T)0.0))
			, Mass((T)0.0)
			, BoundingBox(TAABB<T, d>(TVector<T, d>((T)0.0), TVector<T, d>((T)0.0)))
		{}

		TGeometryParticleHandle<T, d>* Particle;
		IPhysicsProxyBase* ParticleProxy;
		TVector<T, d> Location;
		TVector<T, d> Velocity;
		TVector<T, d> AngularVelocity;
		T Mass;
		Chaos::TAABB<T, d> BoundingBox;
	};

	/*
	BreakingData used in the subsystems
	*/
	template<class T, int d>
	struct TBreakingDataExt
	{
		TBreakingDataExt()
			: Location(TVector<T, d>((T)0.0))
			, Velocity(TVector<T, d>((T)0.0))
			, AngularVelocity(TVector<T, d>((T)0.0))
			, Mass((T)0.0)
		    , Particle(nullptr)
		    , ParticleProxy(nullptr)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{}

		TBreakingDataExt(TVector<T, d> InLocation
			, TVector<T, d> InVelocity
			, TVector<T, d> InAngularVelocity
			, T InMass
			, TGeometryParticleHandle<T, d>* InParticle
			, IPhysicsProxyBase* InParticleProxy
			, float InBoundingboxVolume
			, float InBoundingboxExtentMin
			, float InBoundingboxExtentMax
			, int32 InSurfaceType)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
		    , Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		TBreakingDataExt(const TBreakingData<float, 3>& InBreakingData)
			: Location(InBreakingData.Location)
			, Velocity(InBreakingData.Velocity)
			, AngularVelocity(InBreakingData.AngularVelocity)
			, Mass(InBreakingData.Mass)
		    , Particle(InBreakingData.Particle)
		    , ParticleProxy(InBreakingData.ParticleProxy)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{
		}

		TVector<T, d> Location;
		TVector<T, d> Velocity;
		TVector<T, d> AngularVelocity;
		T Mass;
		TGeometryParticleHandle<T, d>* Particle;
		IPhysicsProxyBase* ParticleProxy;
		float BoundingboxVolume;
		float BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;

		FVector TransformTranslation;
		FQuat TransformRotation;
		FVector TransformScale;

		FBox BoundingBox;

		// Please don't be tempted to add the code below back. Holding onto a UObject pointer without the GC knowing about it is 
		// not a safe thing to do.
		//UPhysicalMaterial* PhysicalMaterialTest;
		FName PhysicalMaterialName;
	};

	/*
	TrailingData passed from the physics solver to subsystems
	*/
	template<class T, int d>
	struct TTrailingData
	{
		TTrailingData()
			: Location(TVector<T, d>((T)0.0))
			, Velocity(TVector<T, d>((T)0.0))
			, AngularVelocity(TVector<T, d>((T)0.0))
			, Mass((T)0.0)
			, Particle(nullptr)
		    , ParticleProxy(nullptr)
			, BoundingBox(TAABB<T, d>(TVector<T, d>((T)0.0), TVector<T, d>((T)0.0)))
		{}

		TTrailingData(TVector<T, d> InLocation, TVector<T, d> InVelocity, TVector<T, d> InAngularVelocity, T InMass
			, TGeometryParticleHandle<T, d>* InParticle, IPhysicsProxyBase* InParticleProxy, Chaos::TAABB<T, d>& InBoundingBox)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
			, Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingBox(InBoundingBox)
		{}

		TVector<T, d> Location;
		TVector<T, d> Velocity;
		TVector<T, d> AngularVelocity;
		T Mass;
		TGeometryParticleHandle<T, d>* Particle;
		IPhysicsProxyBase* ParticleProxy;
		Chaos::TAABB<T, d> BoundingBox;

		friend inline uint32 GetTypeHash(const TTrailingData& Other)
		{
			return ::GetTypeHash(Other.Particle);
		}

		friend bool operator==(const TTrailingData& A, const TTrailingData& B)
		{
			return A.Particle == B.Particle;
		}
	};

	/*
	TrailingData used in subsystems
	*/
	template<class T, int d>
	struct TTrailingDataExt
	{
		TTrailingDataExt()
			: Location(TVector<T, d>((T)0.0))
			, Velocity(TVector<T, d>((T)0.0))
			, AngularVelocity(TVector<T, d>((T)0.0))
			, Mass((T)0.0)
			, Particle(nullptr)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{}

		TTrailingDataExt(TVector<T, d> InLocation
			, TVector<T, d> InVelocity
			, TVector<T, d> InAngularVelocity
			, T InMass
			, TGeometryParticleHandle<T, d>* InParticle
			, IPhysicsProxyBase* InParticleProxy
			, float InBoundingboxVolume
			, float InBoundingboxExtentMin
			, float InBoundingboxExtentMax
			, int32 InSurfaceType)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
			, Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		TTrailingDataExt(const TTrailingData<float, 3>& InTrailingData)
			: Location(InTrailingData.Location)
			, Velocity(InTrailingData.Velocity)
			, AngularVelocity(InTrailingData.AngularVelocity)
			, Mass(InTrailingData.Mass)
			, Particle(InTrailingData.Particle)
		    , ParticleProxy(InTrailingData.ParticleProxy)
			, BoundingboxVolume((T)-1.0)
			, BoundingboxExtentMin((T)-1.0)
			, BoundingboxExtentMax((T)-1.0)
			, SurfaceType(-1)
		{}

		TVector<T, d> Location;
		TVector<T, d> Velocity;
		TVector<T, d> AngularVelocity;
		T Mass;
		TGeometryParticleHandle<T, d>* Particle;
		IPhysicsProxyBase* ParticleProxy;
		//	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
								 // It is important to be able to get extra data from the component
		float BoundingboxVolume;
		float BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;

		friend inline uint32 GetTypeHash(const TTrailingDataExt& Other)
		{
			return ::GetTypeHash(Other.Particle);
		}

		friend bool operator==(const TTrailingDataExt& A, const TTrailingDataExt& B)
		{
			return A.Particle == B.Particle;
		}
	};

	template<class T, int d>
	struct TSleepingData
	{
		TSleepingData()
			: Particle(nullptr)
			, Sleeping(true)
		{}

		TSleepingData(
		    TGeometryParticle<T, d>* InParticle, bool InSleeping)
			: Particle(InParticle)
			, Sleeping(InSleeping)
		{}

		TGeometryParticle<T, d>* Particle;
		bool Sleeping;	// if !Sleeping == Awake
	};

}


