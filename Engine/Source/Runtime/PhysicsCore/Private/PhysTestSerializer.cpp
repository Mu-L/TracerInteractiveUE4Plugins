// Copyright Epic Games, Inc. All Rights Reserved.

// Physics engine integration utilities

#include "PhysTestSerializer.h"

#if WITH_PHYSX
#include "PhysXIncludes.h"
#include "PhysXSupportCore.h"
#endif

#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"

using namespace Chaos;

#if WITH_PHYSX
#include "PhysXToChaosUtil.h"
#endif

#include "PhysicsPublicCore.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

FPhysTestSerializer::FPhysTestSerializer()
	: bDiskDataIsChaos(false)
	, bChaosDataReady(false)
{
}

void FPhysTestSerializer::Serialize(const TCHAR* FilePrefix)
{
	check(IsInGameThread());
	int32 Tries = 0;
	FString UseFileName;
	const FString FullPathPrefix = FPaths::ProfilingDir() / FilePrefix;
	do
	{
		UseFileName = FString::Printf(TEXT("%s_%d.bin"), *FullPathPrefix, Tries++);
	} while (IFileManager::Get().FileExists(*UseFileName));

	//this is not actually file safe but oh well, very unlikely someone else is trying to create this file at the same time
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*UseFileName));
	if (File)
	{
		FChaosArchive Ar(*File);
		UE_LOG(LogPhysicsCore, Log, TEXT("PhysTestSerialize File: %s"), *UseFileName);
		Serialize(Ar);
	}
	else
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("Could not create PhysTestSerialize file(%s)"), *UseFileName);
	}
}

void FPhysTestSerializer::Serialize(Chaos::FChaosArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		//make sure any context we had set is restored before writing out sqcapture
		Ar.SetContext(MoveTemp(ChaosContext));
	}

	static const FName TestSerializerName = TEXT("PhysTestSerializer");

	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, TestSerializerName, false);
		int Version = 1;
		Ar << Version;
		Ar << bDiskDataIsChaos;

		if (Version >= 1)
		{
			//use version recorded
			ArchiveVersion.Serialize(Ar);
		}
		else
		{
			//no version recorded so use the latest versions in GUIDs we rely on before making serialization version change
			ArchiveVersion.SetVersion(FPhysicsObjectVersion::GUID, FPhysicsObjectVersion::SerializeGTGeometryParticles, TEXT("SerializeGTGeometryParticles"));
			ArchiveVersion.SetVersion(FDestructionObjectVersion::GUID, FDestructionObjectVersion::GroupAndAttributeNameRemapping, TEXT("GroupAndAttributeNameRemapping"));
			ArchiveVersion.SetVersion(FExternalPhysicsCustomObjectVersion::GUID, FExternalPhysicsCustomObjectVersion::BeforeCustomVersionWasAdded, TEXT("BeforeCustomVersionWasAdded"));
		}
		
		Ar.SetCustomVersions(ArchiveVersion);
		Ar << Data;
	}

	if (Ar.IsLoading())
	{
		CreatePhysXData();
		CreateChaosData();
		Ar.SetContext(MoveTemp(ChaosContext));	//make sure any context we created during load is used for sqcapture
	}

	bool bHasSQCapture = !!SQCapture;
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, TestSerializerName, false);
		Ar << bHasSQCapture;
	}
	if(bHasSQCapture)
	{
		if (Ar.IsLoading())
		{
			SQCapture = TUniquePtr<FSQCapture>(new FSQCapture(*this));
		}
		SQCapture->Serialize(Ar);
	}
	ChaosContext = Ar.StealContext();
}

void FPhysTestSerializer::SetPhysicsData(physx::PxScene& Scene)
{
#if WITH_PHYSX
	check(AlignedDataHelper == nullptr || &Scene != AlignedDataHelper->PhysXScene);

	PxSerializationRegistry* Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
	PxCollection* Collection = PxCollectionExt::createCollection(Scene);

	PxSerialization::complete(*Collection, *Registry);

	//give an ID for every object so we can find it later. This only holds for direct objects like actors and shapes
	const uint32 NumObjects = Collection->getNbObjects();
	TArray<PxBase*> Objects;
	Objects.AddUninitialized(NumObjects);
	Collection->getObjects(Objects.GetData(), NumObjects);
	for (PxBase* Obj : Objects)
	{
		Collection->add(*Obj, (PxSerialObjectId)Obj);
	}

	Data.Empty();
	FPhysXOutputStream Stream(&Data);
	PxSerialization::serializeCollectionToBinary(Stream, *Collection, *Registry);
	Collection->release();
	Registry->release();

	bDiskDataIsChaos = false;
#endif
}

void FPhysTestSerializer::SetPhysicsData(Chaos::FPBDRigidsEvolutionGBF& Evolution)
{
	bDiskDataIsChaos = true;
	Data.Empty();
	FMemoryWriter Ar(Data);
	FChaosArchive ChaosAr(Ar);
	Evolution.Serialize(ChaosAr);
	ChaosContext = ChaosAr.StealContext();
	ArchiveVersion = Ar.GetCustomVersions();
}

#if WITH_PHYSX
FPhysTestSerializer::FPhysXSerializerData::~FPhysXSerializerData()
{
	if (PhysXScene)
	{
		//release all resources the collection created (calling release on the collection is not enough)
		const uint32 NumObjects = Collection->getNbObjects();
		TArray<PxBase*> Objects;
		Objects.AddUninitialized(NumObjects);
		Collection->getObjects(Objects.GetData(), NumObjects);
		for (PxBase* Obj : Objects)
		{
			if (Obj->isReleasable())
			{
				Obj->release();
			}
		}

		Collection->release();
		Registry->release();
		PhysXScene->release();
	}
	FMemory::Free(Data);
}
#endif

void FPhysTestSerializer::CreatePhysXData()
{
#if WITH_PHYSX
	if (bDiskDataIsChaos == false)	//For the moment we don't support chaos to physx direction
	{
		{
			check(Data.Num());	//no data, was the physx scene set?
			AlignedDataHelper = MakeUnique<FPhysXSerializerData>(Data.Num());
			FMemory::Memcpy(AlignedDataHelper->Data, Data.GetData(), Data.Num());
		}

		PxSceneDesc Desc = CreateDummyPhysXSceneDescriptor();	//question: does it matter that this is default and not the one set by user settings?
		AlignedDataHelper->PhysXScene = GPhysXSDK->createScene(Desc);

		AlignedDataHelper->Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
		AlignedDataHelper->Collection = PxSerialization::createCollectionFromBinary(AlignedDataHelper->Data, *AlignedDataHelper->Registry);
		AlignedDataHelper->PhysXScene->addCollection(*AlignedDataHelper->Collection);
	}
#endif
}

#if WITH_PHYSX
physx::PxBase* FPhysTestSerializer::FindObject(uint64 Id)
{
	if (!AlignedDataHelper)
	{
		CreatePhysXData();
	}

	physx::PxBase* Ret = AlignedDataHelper->Collection->find(Id);
	ensure(Ret);
	return Ret;
}
#endif

void FPhysTestSerializer::CreateChaosData()
{
#if WITH_PHYSX
	if (bDiskDataIsChaos == false)
	{
		if (bChaosDataReady)
		{
			return;
		}

		PxScene* Scene = GetPhysXData();
		check(Scene);

		const uint32 NumStatic = Scene->getNbActors(PxActorTypeFlag::eRIGID_STATIC);
		const uint32 NumDynamic = Scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
		const uint32 NumActors = NumStatic + NumDynamic;

		TArray<PxActor*> Actors;
		Actors.AddUninitialized(NumActors);
		if (NumStatic)
		{
			Scene->getActors(PxActorTypeFlag::eRIGID_STATIC, Actors.GetData(), NumStatic);
			auto NewParticles = Particles.CreateStaticParticles(NumStatic);	//question: do we want to distinguish query only and sim only actors?
			for (uint32 Idx = 0; Idx < NumStatic; ++Idx)
			{
				GTParticles.Emplace(TGeometryParticle<float, 3>::CreateParticle());
				NewParticles[Idx]->GTGeometryParticle() = GTParticles.Last().Get();
			}
		}

		if (NumDynamic)
		{
			Scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, &Actors[NumStatic], NumDynamic);
			auto NewParticles = Particles.CreateDynamicParticles(NumDynamic);	//question: do we want to distinguish query only and sim only actors?

			for (uint32 Idx = 0; Idx < NumDynamic; ++Idx)
			{
				GTParticles.Emplace(TPBDRigidParticle<float, 3>::CreateParticle());
				NewParticles[Idx]->GTGeometryParticle() = GTParticles.Last().Get();
			}
		}

		auto& Handles = Particles.GetParticleHandles();
		int32 Idx = 0;
		for (PxActor* Act : Actors)
		{
			//transform
			PxRigidActor* Actor = static_cast<PxRigidActor*>(Act);
			auto& Particle = Handles.Handle(Idx);
			auto& GTParticle = Particle->GTGeometryParticle();
			Particle->X() = P2UVector(Actor->getGlobalPose().p);
			Particle->R() = P2UQuat(Actor->getGlobalPose().q);
			Particle->GTGeometryParticle()->SetX(Particle->X());
			Particle->GTGeometryParticle()->SetR(Particle->R());

			auto PBDRigid = Particle->CastToRigidParticle();
			if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
			{
				PBDRigid->P() = Particle->X();
				PBDRigid->Q() = Particle->R();

				PBDRigid->GTGeometryParticle()->CastToRigidParticle()->SetP(PBDRigid->P());
				PBDRigid->GTGeometryParticle()->CastToRigidParticle()->SetQ(PBDRigid->R());
			}

			PxActorToChaosHandle.Add(Act, Particle.Get());

			//geometry
			TArray<TUniquePtr<FImplicitObject>> Geoms;
			const int32 NumShapes = Actor->getNbShapes();
			TArray<PxShape*> Shapes;
			Shapes.AddUninitialized(NumShapes);
			Actor->getShapes(Shapes.GetData(), NumShapes);
			for (PxShape* Shape : Shapes)
			{
				if (TUniquePtr<TImplicitObjectTransformed<float, 3>> Geom = PxShapeToChaosGeom(Shape))
				{
					Geoms.Add(MoveTemp(Geom));
				}
			}

			if (Geoms.Num())
			{
				if (Geoms.Num() == 1)
				{
					auto SharedGeom = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(Geoms[0].Release());
					GTParticle->SetGeometry(SharedGeom);
					Particle->SetSharedGeometry(SharedGeom);
				}
				else
				{
					GTParticle->SetGeometry(MakeUnique<FImplicitObjectUnion>(MoveTemp(Geoms)));
					Particle->SetGeometry(GTParticle->Geometry());
				}

				// Fixup bounds
				auto Geom = GTParticle->Geometry();
				if (Geom->HasBoundingBox())
				{
					auto& ShapeArray = GTParticle->ShapesArray();
					for (auto& Shape : ShapeArray)
					{
						Shape->WorldSpaceInflatedShapeBounds = Geom->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Particle->X(), Particle->R()));
					}
				}
			}

			int32 ShapeIdx = 0;
			for (PxShape* Shape : Shapes)
			{
				PxShapeToChaosShapes.Add(Shape, GTParticle->ShapesArray()[ShapeIdx++].Get());
			}

			++Idx;
		}

		ChaosEvolution = MakeUnique<FPBDRigidsEvolutionGBF>(Particles);
	}
	else
	{
		ChaosEvolution = MakeUnique<FPBDRigidsEvolutionGBF>(Particles);

		FMemoryReader Ar(Data);
		FChaosArchive ChaosAr(Ar);

		Ar.SetCustomVersions(ArchiveVersion);

		ChaosEvolution->Serialize(ChaosAr);
		ChaosContext = ChaosAr.StealContext();
	}
	bChaosDataReady = true;
#endif
}
