// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleHandle.h"

namespace Chaos
{

template <typename T, int d>
class TPBDRigidsSOAs
{
public:
	TPBDRigidsSOAs()
	{
#if CHAOS_DETERMINISTIC
		BiggestParticleID = 0;
#endif

		StaticParticles = MakeUnique<TGeometryParticles<T, d>>();
		StaticDisabledParticles = MakeUnique <TGeometryParticles<T, d>>();

		KinematicParticles = MakeUnique < TKinematicGeometryParticles<T, d>>();
		KinematicDisabledParticles = MakeUnique < TKinematicGeometryParticles<T, d>>();

		DynamicDisabledParticles = MakeUnique<TPBDRigidParticles<T, d>>();
		DynamicParticles = MakeUnique<TPBDRigidParticles<T, d>>();

		ClusteredParticles = MakeUnique< TPBDRigidClusteredParticles<T, d>>();
		ClusteredParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering

		GeometryCollectionParticles = MakeUnique<TPBDGeometryCollectionParticles<T, d>>();
		GeometryCollectionParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering
		
		UpdateViews();
	}

	TPBDRigidsSOAs(const TPBDRigidsSOAs<T,d>&) = delete;
	TPBDRigidsSOAs(TPBDRigidsSOAs<T, d>&& Other) = delete;

	void Reset()
	{
		check(0);
	}
	
	TArray<TGeometryParticleHandle<T, d>*> CreateStaticParticles(int32 NumParticles, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		auto Results =  CreateParticlesHelper<TGeometryParticleHandle<T, d>>(NumParticles, Params.bDisabled ? StaticDisabledParticles : StaticParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<TKinematicGeometryParticleHandle<T, d>*> CreateKinematicParticles(int32 NumParticles, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		auto Results = CreateParticlesHelper<TKinematicGeometryParticleHandle<T, d>>(NumParticles, Params.bDisabled ? KinematicDisabledParticles : KinematicParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<TPBDRigidParticleHandle<T, d>*> CreateDynamicParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto Results = CreateParticlesHelper<TPBDRigidParticleHandle<T, d>>(NumParticles, Params.bDisabled ? DynamicDisabledParticles : DynamicParticles, Params);

		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(Results, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		UpdateViews();
		return Results;
	}
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> CreateGeometryCollectionParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		TArray<TPBDGeometryCollectionParticleHandle<T, d>*> Results = CreateParticlesHelper<TPBDGeometryCollectionParticleHandle<T, d>>(
			NumParticles, GeometryCollectionParticles, Params);
		//TArray<TPBDRigidParticleHandle<T, d>*>& RigidHandles = (TArray<TPBDRigidParticleHandle<T, d>*>*)&Results;//*static_cast<TArray<TPBDRigidParticleHandle<T, d>*>*>(&Results);
		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(Results, ActiveGeometryCollectionToIndex, ActiveGeometryCollectionArray);
		}
		UpdateGeometryCollectionViews();
		UpdateViews();
		return Results;
	}

	/** Used specifically by PBDRigidClustering. These have special properties for maintaining relative order, efficiently switching from kinematic to dynamic, disable to enable, etc... */
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> CreateClusteredParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto NewClustered = CreateParticlesHelper<TPBDRigidClusteredParticleHandle<T, d>>(NumParticles, ClusteredParticles, Params);
		
		if (!Params.bDisabled)
		{
			InsertToMapAndArray(NewClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
		}

		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(reinterpret_cast<TArray<TPBDRigidParticleHandle<T,d>*>&>(NewClustered), ActiveParticlesToIndex, ActiveParticlesArray);
			InsertToMapAndArray(NewClustered, ActiveClusteredToIndex, ActiveClusteredArray);
		}

		UpdateViews();
		
		return NewClustered;
	}

	void DestroyParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		check(Particle->AsClustered() == nullptr);	//not supported

		if (auto PBDRigid = Particle->AsDynamic())
		{
			RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}

		ParticleHandles.DestroyHandleSwap(Particle);
		UpdateViews();
	}

	void DisableParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		if (auto PBDRigid = Particle->AsDynamic())
		{
			PBDRigid->Disabled() = true;
			PBDRigid->V() = TVector<T, d>(0);
			PBDRigid->W() = TVector<T, d>(0);

			if (auto PBDRigidClustered = Particle->AsClustered())
			{
				RemoveFromMapAndArray(PBDRigidClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
				RemoveFromMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
			}
			else
			{
				Particle->MoveToSOA(*DynamicDisabledParticles);
			}
			RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		else if (Particle->AsKinematic())
		{
			Particle->MoveToSOA(*KinematicDisabledParticles);
		}
		else
		{
			Particle->MoveToSOA(*StaticDisabledParticles);
		}
		UpdateViews();
	}

	void EnableParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		if (auto PBDRigid = Particle->AsDynamic())
		{
			if (auto PBDRigidClustered = Particle->AsClustered())
			{
				InsertToMapAndArray(PBDRigidClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
				if (!PBDRigid->Sleeping())
				{
					InsertToMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
				}
			}
			else
			{
				Particle->MoveToSOA(*DynamicParticles);
			}

			if (!PBDRigid->Sleeping())
			{
				InsertToMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
			}

			PBDRigid->Disabled() = false;
		}
		else if (Particle->AsKinematic())
		{
			Particle->MoveToSOA(*KinematicParticles);
		}
		else
		{
			Particle->MoveToSOA(*StaticParticles);
		}
		UpdateViews();
	}

	void ActivateParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		if (auto PBDRigid = Particle->AsDynamic())
		{
			check(!PBDRigid->Disabled());
			if (auto PBDRigidClustered = Particle->AsClustered())
			{
				InsertToMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
			}

			InsertToMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		
		UpdateViews();
	}

	void DeactivateParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		if (auto PBDRigid = Particle->AsDynamic())
		{
			check(!PBDRigid->Disabled());
			if (auto PBDRigidClustered = Particle->AsClustered())
			{
				RemoveFromMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
			}

			RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}

		UpdateViews();
	}

	void DeactivateParticles(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
	{
		for (auto Particle : Particles)
		{
			DeactivateParticle(Particle);
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		static const FName SOAsName = TEXT("PBDRigidsSOAs");
		FChaosArchiveScopedMemory ScopedMemory(Ar, SOAsName, false);

		ParticleHandles.Serialize(Ar);

		Ar << StaticParticles;
		Ar << StaticDisabledParticles;
		Ar << KinematicParticles;
		Ar << KinematicDisabledParticles;
		Ar << DynamicParticles;
		Ar << DynamicDisabledParticles;
		ensure(ClusteredParticles->Size() == 0);	//not supported yet
		//Ar << ClusteredParticles;
		Ar << GeometryCollectionParticles;

		SerializeMapAndArray(Ar, ActiveParticlesToIndex, ActiveParticlesArray);
		//SerializeMapAndArray(Ar, ActiveClusteredToIndex, ActiveClusteredArray);
		//SerializeMapAndArray(Ar, NonDisabledClusteredToIndex, NonDisabledClusteredArray);

		//todo: update deterministic ID

		UpdateViews();
	}


	const TParticleView<TGeometryParticles<T, d>>& GetNonDisabledView() const { return NonDisabledView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetNonDisabledDynamicView() const { return NonDisabledDynamicView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() const { return ActiveParticlesView; }
	TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() { return ActiveParticlesView; }

	const TParticleView<TGeometryParticles<T, d>>& GetAllParticlesView() const { return AllParticlesView; }

	const TParticleView<TKinematicGeometryParticles<T, d>>& GetActiveKinematicParticlesView() const { return ActiveKinematicParticlesView; }
	TParticleView<TKinematicGeometryParticles<T, d>>& GetActiveKinematicParticlesView() { return ActiveKinematicParticlesView; }

	const TGeometryParticleHandles<T, d>& GetParticleHandles() const { return ParticleHandles; }
	TGeometryParticleHandles<T, d>& GetParticleHandles() { return ParticleHandles; }

	const TPBDRigidParticles<T, d>& GetDynamicParticles() const { return *DynamicParticles; }
	TPBDRigidParticles<T, d>& GetDynamicParticles() { return *DynamicParticles; }

	const TGeometryParticles<T, d>& GetNonDisabledStaticParticles() const { return *StaticParticles; }
	TGeometryParticles<T, d>& GetNonDisabledStaticParticles() { return *StaticParticles; }

	const TPBDGeometryCollectionParticles<T, d>& GetGeometryCollectionParticles() const { return *GeometryCollectionParticles; }
	TPBDGeometryCollectionParticles<T, d>& GetGeometryCollectionParticles() { return *GeometryCollectionParticles; }

	const TParticleView<TPBDGeometryCollectionParticles<T, d>>& GetActiveGeometryCollectionParticlesView() const { return ActiveGeometryCollectionParticlesView; }
	TParticleView<TPBDGeometryCollectionParticles<T, d>>& GetActiveGeometryCollectionParticlesView() { return ActiveGeometryCollectionParticlesView; }

	/**
	 * Update which particle arrays geometry collection particles are in based on 
	 * their object state (static, kinematic, dynamic, sleeping) and their disabled 
	 * state.
	 */
	void UpdateGeometryCollectionViews()
	{
		int32 AIdx = 0, SIdx = 0, KIdx = 0, DIdx = 0;

		for (TPBDGeometryCollectionParticleHandle<T, d>* Handle : ActiveGeometryCollectionArray)
		{
			// If the particle is disabled we treat it as static, but for no reason 
			// other than immediate convenience.
			const Chaos::EObjectStateType State = Handle->Disabled() ? Chaos::EObjectStateType::Static : Handle->ObjectState();

			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				SIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			case Chaos::EObjectStateType::Kinematic:
				KIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			case Chaos::EObjectStateType::Sleeping: // Sleeping is a modified dynamic state
				DIdx++;
				break;

			case Chaos::EObjectStateType::Dynamic:
				DIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			default:
				break;
			};
		}

		bool Changed = 
			ActiveGeometryCollectionArray.Num() != AIdx ||
			StaticGeometryCollectionArray.Num() != SIdx || 
			KinematicGeometryCollectionArray.Num() != KIdx || 
			DynamicGeometryCollectionArray.Num() != DIdx;

		if (Changed)
		{
			ActiveGeometryCollectionArray.SetNumUninitialized(AIdx);
			StaticGeometryCollectionArray.SetNumUninitialized(SIdx);
			KinematicGeometryCollectionArray.SetNumUninitialized(KIdx);
			DynamicGeometryCollectionArray.SetNumUninitialized(DIdx);
		}

		AIdx = SIdx = KIdx = DIdx = 0;

		for (TPBDGeometryCollectionParticleHandle<T, d>* Handle : ActiveGeometryCollectionArray)
		{
			const Chaos::EObjectStateType State = 
				Handle->Disabled() ? Chaos::EObjectStateType::Static : Handle->ObjectState();
			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				Changed |= StaticGeometryCollectionArray[SIdx] != Handle;
				StaticGeometryCollectionArray[SIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			case Chaos::EObjectStateType::Kinematic:
				Changed |= KinematicGeometryCollectionArray[KIdx] != Handle;
				KinematicGeometryCollectionArray[KIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			case Chaos::EObjectStateType::Sleeping: // Sleeping is a modified dynamic state
				Changed |= DynamicGeometryCollectionArray[DIdx] != Handle;
				DynamicGeometryCollectionArray[DIdx++] = Handle;
				break;

			case Chaos::EObjectStateType::Dynamic:
				Changed |= DynamicGeometryCollectionArray[DIdx] != Handle;
				DynamicGeometryCollectionArray[DIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			default:
				break;
			};
		}

		if(Changed)
		{
			UpdateViews();
		}
	}

	//TEMP: only needed while clustering code continues to use direct indices
	const auto& GetActiveClusteredArray() const { return ActiveClusteredArray; }
	const auto& GetNonDisabledClusteredArray() const { return NonDisabledClusteredArray; }

	const auto& GetClusteredParticles() const { return *ClusteredParticles; }
	auto& GetClusteredParticles() { return *ClusteredParticles; }

private:
	template <typename TParticleHandleType, typename TParticles>
	TArray<TParticleHandleType*> CreateParticlesHelper(int32 NumParticles, TUniquePtr<TParticles>& Particles, const TGeometryParticleParameters<T, d>& Params)
	{
		const int32 ParticlesStartIdx = Particles->Size();
		Particles->AddParticles(NumParticles);
		TArray<TParticleHandleType*> ReturnHandles;
		ReturnHandles.AddUninitialized(NumParticles);

		const int32 HandlesStartIdx = ParticleHandles.Size();
		ParticleHandles.AddHandles(NumParticles);

		for (int32 Count = 0; Count < NumParticles; ++Count)
		{
			const int32 ParticleIdx = Count + ParticlesStartIdx;
			const int32 HandleIdx = Count + HandlesStartIdx;

			TUniquePtr<TParticleHandleType> NewParticleHandle = TParticleHandleType::CreateParticleHandle(MakeSerializable(Particles), ParticleIdx, HandleIdx);
#if CHAOS_DETERMINISTIC
			NewParticleHandle->ParticleID() = BiggestParticleID++;
#endif
			ReturnHandles[Count] = NewParticleHandle.Get();
			ParticleHandles.Handle(HandleIdx) = MoveTemp(NewParticleHandle);
		}

		return ReturnHandles;
	}

	template <typename TParticle1, typename TParticle2>
	void InsertToMapAndArray(const TArray<TParticle1*>& ParticlesToInsert, TMap<TParticle2*, int32>& ParticleToIndex, TArray<TParticle2*>& ParticleArray)
	{
		// TODO: Compile time check ensuring TParticle2 is derived from TParticle1?
		int32 NextIdx = ParticleArray.Num();
		for (auto Particle : ParticlesToInsert)
		{
			ParticleToIndex.Add(Particle, NextIdx++);
		}
		ParticleArray.Append(ParticlesToInsert);
	}

	template <typename TParticle>
	static void InsertToMapAndArray(TParticle* Particle, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
	{
		if (ParticleToIndex.Contains(Particle) == false)
		{
			ParticleToIndex.Add(Particle, ParticleArray.Num());
			ParticleArray.Add(Particle);
		}
	}

	template <typename TParticle>
	static void RemoveFromMapAndArray(TParticle* Particle, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
	{
		if (int32* IdxPtr = ParticleToIndex.Find(Particle))
		{
			int32 Idx = *IdxPtr;
			ParticleArray.RemoveAtSwap(Idx);
			if (Idx < ParticleArray.Num())
			{
				//update swapped element with new index
				ParticleToIndex[ParticleArray[Idx]] = Idx;
			}
			ParticleToIndex.Remove(Particle);
		}
	}

	template <typename TParticle>
	void SerializeMapAndArray(FChaosArchive& Ar, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
	{
		TArray<TSerializablePtr<TParticle>>& SerializableArray = AsAlwaysSerializableArray(ParticleArray);
		Ar << SerializableArray;

		int32 Idx = 0;
		for (auto Particle : ParticleArray)
		{
			ParticleToIndex.Add(Particle, Idx++);
		}
	}
	
	//should be called whenever particles are added / removed / reordered
	void UpdateViews()
	{
		//build various views. Group SOA types together for better branch prediction
		{
			TArray<TSOAView<TGeometryParticles<T, d>>> TmpArray = 
			{ 
				StaticParticles.Get(), 
				KinematicParticles.Get(), 
				DynamicParticles.Get(), 
				{&NonDisabledClusteredArray}, 
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				DynamicParticles.Get(), 
				{&NonDisabledClusteredArray}, 
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledDynamicView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				{&ActiveParticlesArray},
				{&ActiveGeometryCollectionArray}
			};
			ActiveParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TGeometryParticles<T, d>>> TmpArray = { StaticParticles.Get(), StaticDisabledParticles.Get(), KinematicParticles.Get(), KinematicDisabledParticles.Get(),
				DynamicParticles.Get(), DynamicDisabledParticles.Get(), ClusteredParticles.Get(), GeometryCollectionParticles.Get() };
			AllParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TKinematicGeometryParticles<T, d>>> TmpArray = 
			{ 
				KinematicParticles.Get(),
				{&KinematicGeometryCollectionArray}
			};
			ActiveKinematicParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDGeometryCollectionParticles<T, d>>> TmpArray = { {&ActiveGeometryCollectionArray} };
			ActiveGeometryCollectionParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
	}

	//Organized by SOA type
	TUniquePtr<TGeometryParticles<T, d>> StaticParticles;
	TUniquePtr<TGeometryParticles<T, d>> StaticDisabledParticles;

	TUniquePtr<TKinematicGeometryParticles<T, d>> KinematicParticles;
	TUniquePtr<TKinematicGeometryParticles<T, d>> KinematicDisabledParticles;

	TUniquePtr<TPBDRigidParticles<T, d>> DynamicParticles;
	TUniquePtr<TPBDRigidParticles<T, d>> DynamicDisabledParticles;

	TUniquePtr<TPBDRigidClusteredParticles<T, d>> ClusteredParticles;

	TUniquePtr<TPBDGeometryCollectionParticles<T, d>> GeometryCollectionParticles;

	TMap<TPBDGeometryCollectionParticleHandle<T, d>*, int32> ActiveGeometryCollectionToIndex;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> ActiveGeometryCollectionArray;

	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> StaticGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> KinematicGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> DynamicGeometryCollectionArray;

	//Utility structures for maintaining an Active particles view
	TMap<TPBDRigidParticleHandle<T, d>*, int32> ActiveParticlesToIndex;
	TArray<TPBDRigidParticleHandle<T, d>*> ActiveParticlesArray;
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, int32> ActiveClusteredToIndex;
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> ActiveClusteredArray;

	//Utility structures for maintaining a NonDisabled particle view
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, int32> NonDisabledClusteredToIndex;
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> NonDisabledClusteredArray;

	//Particle Views
	TParticleView<TGeometryParticles<T, d>> NonDisabledView;							//all particles that are not disabled
	TParticleView<TPBDRigidParticles<T, d>> NonDisabledDynamicView;						//all dynamic particles that are not disabled
	TParticleView<TPBDRigidParticles<T, d>> ActiveParticlesView;						//all particles that are active
	TParticleView<TGeometryParticles<T, d>> AllParticlesView;							//all particles
	TParticleView<TKinematicGeometryParticles<T, d>> ActiveKinematicParticlesView;		//all kinematic particles that are not disabled
	TParticleView<TPBDGeometryCollectionParticles<T, d>> ActiveGeometryCollectionParticlesView; // all geom collection particles that are not disabled

	//Auxiliary data synced with particle handles
	TGeometryParticleHandles<T, d> ParticleHandles;

#if CHAOS_DETERMINISTIC
	int32 BiggestParticleID;
#endif
};
}
