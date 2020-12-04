// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{

	bool FIgnoreCollisionManager::ContainsHandle(FHandleID Body0)
	{
		return IgnoreCollisionsList.Contains(Body0);
	}

	bool FIgnoreCollisionManager::IgnoresCollision(FHandleID Body0, FHandleID Body1)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			return IgnoreCollisionsList[Body0].Contains(Body1);
		}
		return false;
	}

	int32 FIgnoreCollisionManager::NumIgnoredCollision(FHandleID Body0)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			return IgnoreCollisionsList[Body0].Num();
		}
		return 0;
	}

	void FIgnoreCollisionManager::AddIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
	{
		if (!IgnoreCollisionsList.Contains(Body0))
		{
			IgnoreCollisionsList.Add(Body0, TArray<FHandleID>());
		}
		IgnoreCollisionsList[Body0].Add(Body1);

	}
	void FIgnoreCollisionManager::RemoveIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			IgnoreCollisionsList[Body0].Remove(Body1);
		}
	}

	void FIgnoreCollisionManager::FlipBufferPreSolve()
	{
		BufferedData->FlipProducer();

		// Merge
		for (auto& Elem : BufferedData->GetConsumerBuffer()->PendingActivations)
		{
			if( PendingActivations.Contains(Elem.Key) )
			{
				// the case where the key already existed should be avoided
				// but the implementation is here for completeness. 
				for (auto& Val : Elem.Value)
				{
					if (!PendingActivations[Elem.Key].Contains(Val))
					{
						PendingActivations[Elem.Key].Add(Val);
					}
				}
			}
			else
			{
				PendingActivations.Add(Elem.Key, Elem.Value);
			}
		}

		for (auto& Item : BufferedData->GetConsumerBuffer()->PendingDeactivations)
		{
			if (!PendingDeactivations.Contains(Item))
			{
				PendingDeactivations.Add(Item);
			}
		}

		BufferedData->GetConsumerBufferMutable()->PendingActivations.Empty();
		BufferedData->GetConsumerBufferMutable()->PendingDeactivations.Empty();
	}

	void FIgnoreCollisionManager::ProcessPendingQueues()
	{

		// remove particles that have been created and destroyed
		// before the queue was ever processed. 
		TArray<FGeometryParticle*> PreculledParticles;
		if (PendingActivations.Num() && PendingDeactivations.Num())
		{
			TArray<FGeometryParticle*> DeletionList;
			for (auto& Elem : PendingActivations)
			{
				int32 DeactiveIndex = PendingDeactivations.Find(Elem.Key);
				if (DeactiveIndex != INDEX_NONE)
				{
					DeletionList.Add(Elem.Key);
					PreculledParticles.Add(Elem.Key);
					PendingDeactivations.RemoveAtSwap(DeactiveIndex, 1);
				}
			}
			for (auto* Del : DeletionList)
				PendingActivations.Remove(Del);
		}

		// add collision relationships for particles that have valid
		// handles, and have not already been removed from the 
		// simulation. 
		if (PendingActivations.Num())
		{
			TArray<FGeometryParticle*> DeletionList;
			for (auto& Elem : PendingActivations)
			{
				if (Elem.Key)
				{
					if (Elem.Key->IsParticleValid())
					{
						if (TGeometryParticleHandle<FReal, 3>* Handle0 = Elem.Key->Handle())
						{
							for (int Index = Elem.Value.Num() - 1; Index >= 0; Index--)
							{
								if (TGeometryParticle<FReal, 3>* Val = Elem.Value[Index])
								{
									if (Val->IsParticleValid())
									{
										if (PreculledParticles.Contains(Val))
										{
											Elem.Value.RemoveAtSwap(Index, 1);
										}
										else if (TGeometryParticleHandle<FReal, 3>* Handle1 = Val->Handle())
										{
											FUniqueIdx ID0 = Handle0->UniqueIdx();
											FUniqueIdx ID1 = Handle1->UniqueIdx();
											if (!IgnoresCollision(ID0, ID1))
											{
												Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Handle0->CastToRigidParticle();
												Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Handle1->CastToRigidParticle();

												ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
												AddIgnoreCollisionsFor(ID0, ID1);

												ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
												AddIgnoreCollisionsFor(ID1, ID0);
											}

											Elem.Value.RemoveAtSwap(Index, 1);
										}
									}
								}
							}
						}
						if (!Elem.Value.Num())
							DeletionList.Add(Elem.Key);
					}
				}
			}
			for (auto* Del : DeletionList)
				PendingActivations.Remove(Del);
		}

		// remove relationships that exist and have been initialized. 
		if (PendingDeactivations.Num())
		{
			for (auto Index = PendingDeactivations.Num() - 1; Index >= 0; Index--)
			{
				if (PendingDeactivations[Index])
				{
					FUniqueIdx ID0 = PendingDeactivations[Index]->UniqueIdx();
					if (IgnoreCollisionsList.Contains(ID0))
					{
						IgnoreCollisionsList.Remove(ID0);
					}
				}
			}
			PendingDeactivations.Empty();
		}
	}

} // Chaos

