// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template <typename T, int d>
	void UpdateShapesArrayFromGeometry(TShapesArray<T, d>& ShapesArray, TSerializablePtr<TImplicitObject<T, d>> Geometry)
	{
		if(Geometry)
		{
			if(const auto* Union = Geometry->template GetObject<TImplicitObjectUnion<T, d>>())
			{
				ShapesArray.SetNum(Union->GetObjects().Num());
				int32 Inner = 0;
				for(const auto& Geom : Union->GetObjects())
				{
					ShapesArray[Inner] = TPerShapeData<T, d>::CreatePerShapeData();
					ShapesArray[Inner]->Geometry = MakeSerializable(Geom);
					++Inner;
				}
			}
			else
			{
				ShapesArray.SetNum(1);
				ShapesArray[0] = TPerShapeData<T, d>::CreatePerShapeData();
				ShapesArray[0]->Geometry = Geometry;
			}
		}
		else
		{
			ShapesArray.Reset();
		}
	}

	template <typename T, int d>
	TPerShapeData<T, d>::TPerShapeData()
	: UserData(nullptr)
	{
	}

	template <typename T, int d>
	TPerShapeData<T, d>::~TPerShapeData()
	{
	}

	template <typename T, int d>
	TUniquePtr<TPerShapeData<T, d>> TPerShapeData<T, d>::CreatePerShapeData()
	{
		return TUniquePtr<TPerShapeData<T, d>>(new TPerShapeData<T, d>());
	}

	template <typename T, int d>
	TPerShapeData<T, d>* TPerShapeData<T, d>::SerializationFactory(FChaosArchive& Ar, TPerShapeData<T, d>*)
	{
		return Ar.IsLoading() ? new TPerShapeData<T, d>() : nullptr;
	}

	template <typename T, int d>
	void TPerShapeData<T, d>::Serialize(FChaosArchive& Ar)
	{
		Ar << Geometry;
		Ar << QueryData;
		Ar << SimData;
	}


	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SetHandle(int32 Index, TGeometryParticleHandle<T, d>* Handle)
	{
		Handle->SetSOALowLevel(this);
		MGeometryParticleHandle[Index] = AsAlwaysSerializable(Handle);
	}

	template <>
	void TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, TGeometryParticleHandle<float, 3>* Handle)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
	}

	template<class T, int d, EGeometryParticlesSimType SimType>
	CHAOS_API TGeometryParticlesImp<T, d, SimType>* TGeometryParticlesImp<T, d, SimType>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<T,d,SimType>* Particles)
	{
		int8 ParticleType = Ar.IsLoading() ? 0 : (int8)Particles->ParticleType();
		Ar << ParticleType;
		switch ((EParticleType)ParticleType)
		{
		case EParticleType::Static: return Ar.IsLoading() ? new TGeometryParticlesImp<T, d, SimType>() : nullptr;
		case EParticleType::Kinematic: return Ar.IsLoading() ? new TKinematicGeometryParticlesImp<T, d, SimType>() : nullptr;
		case EParticleType::Dynamic: return Ar.IsLoading() ? new TPBDRigidParticles<T, d>() : nullptr;
		case EParticleType::Clustered: return Ar.IsLoading() ? new TPBDRigidClusteredParticles<T, d>() : nullptr;
		default:
			check(false); return nullptr;
		}
	}
	
	template<>
	TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* Particles)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
		return nullptr;
	}

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SerializeGeometryParticleHelper(FChaosArchive& Ar, TGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>* GeometryParticles)
	{
		auto& SerializableGeometryParticles = AsAlwaysSerializableArray(GeometryParticles->MGeometryParticle);
		Ar << SerializableGeometryParticles;
	}
	
	template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::RigidBodySim>;
	template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>;
	template class TPerShapeData<float, 3>;
	template void UpdateShapesArrayFromGeometry(TShapesArray<float, 3>& ShapesArray, TSerializablePtr<TImplicitObject<float, 3>> Geometry);
}
