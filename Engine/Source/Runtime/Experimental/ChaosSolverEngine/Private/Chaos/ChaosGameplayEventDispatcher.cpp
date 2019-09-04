// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosGameplayEventDispatcher.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "SolverObjects/SolverObject.h"
#include "PBDRigidsSolver.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsCollisionHandler.h"
#include "ChaosStats.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "PhysicsEngine/BodySetup.h"

// PRAGMA_DISABLE_OPTIMIZATION

// internal
static void DispatchPendingBreakEvents(TArray<FChaosBreakEvent> const& Events, TMap<UPrimitiveComponent*, FBreakEventCallbackWrapper> const& Registrations)
{
	for (FChaosBreakEvent const& E : Events)
	{
		if (E.Component)
		{
			const FBreakEventCallbackWrapper* const Callback = Registrations.Find(E.Component);
			if (Callback)
			{
				Callback->BreakEventCallback(E);
			}
		}
	}
}

static void SetCollisionInfoFromComp(FRigidBodyCollisionInfo& Info, UPrimitiveComponent* Comp)
{
	if (Comp)
	{
		Info.Component = Comp;
		Info.Actor = Comp->GetOwner();
		
		const FBodyInstance* const BodyInst = Comp->GetBodyInstance();
		Info.BodyIndex = BodyInst ? BodyInst->InstanceBodyIndex : INDEX_NONE;
		Info.BoneName = BodyInst && BodyInst->BodySetup.IsValid() ? BodyInst->BodySetup->BoneName : NAME_None;
	}
	else
	{
		Info.Component = nullptr;
		Info.Actor = nullptr;
		Info.BodyIndex = INDEX_NONE;
		Info.BoneName = NAME_None;
	}
}

void UChaosGameplayEventDispatcher::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_DispatchEventNotifies);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if INCLUDE_CHAOS
	// #todo: determine which way is better -- iterate the whole collision list or iterate the list of components that want notifies
	// collision list could be very small or zero for lots of frames
	// but collision list could be very large for certain frames
	// so do we optimize for the steady state or for the spikes?

	FPhysScene_Chaos const* const Scene = GetPhysicsScene().Get();
	Chaos::FPBDRigidsSolver const* const Solver = GetSolver();
	if (Scene && Solver)
	{
		Chaos::FPBDRigidsSolver::FScopedGetEventsData ScopedAccess = Solver->ScopedGetEventsData();

		SCOPE_CYCLE_COUNTER(STAT_DispatchCollisionEvents);

		// COLLISION EVENTS
		{
			PendingChaosCollisionNotifies.Reset();
			ContactPairToPendingNotifyMap.Reset();

			// get collision data from the solver
			const Chaos::FPBDRigidsSolver::FAllCollisionDataMaps& AllCollisionData_Maps = ScopedAccess.GetAllCollisions_Maps();

			if (AllCollisionData_Maps.AllCollisionData && AllCollisionData_Maps.AllCollisionsIndicesBySolverObject && AllCollisionData_Maps.SolverObjectReverseMapping)
			{
				const float CollisionDataTimestamp = AllCollisionData_Maps.AllCollisionData->TimeCreated;
				if (CollisionDataTimestamp > LastCollisionDataTime)
				{
					LastCollisionDataTime = CollisionDataTimestamp;

					TMap<ISolverObjectBase*, TArray<int32>> const& SolverObjectToCollisionIndicesMap = AllCollisionData_Maps.AllCollisionsIndicesBySolverObject->AllCollisionsIndicesBySolverObjectMap;
					Chaos::FPBDRigidsSolver::FCollisionDataArray const& CollisionData = AllCollisionData_Maps.AllCollisionData->AllCollisionsArray;

					int32 NumCollisions = CollisionData.Num();		// is this zero enough frames to be worth the check?
					if (NumCollisions > 0)
					{
						TArray<SolverObjectWrapper> const& SolverObjectReverseMappingArray = AllCollisionData_Maps.SolverObjectReverseMapping->SolverObjectReverseMappingArray;
						TArray<int32> const& ParticleIndexReverseMappingArray = AllCollisionData_Maps.ParticleIndexReverseMapping->ParticleIndexReverseMappingArray;

						// look through all the components that someone is interested in, and see if they had a collision
						// note that we only need to care about the interaction from the POV of the registered component,
						// since if anyone wants notifications for the other component it hit, it's also registered and we'll get to that elsewhere in the list
						for (TMap<UPrimitiveComponent*, FChaosHandlerSet>::TIterator It(CollisionEventRegistrations); It; ++It)
						{
							const FChaosHandlerSet& HandlerSet = It.Value();

							UPrimitiveComponent* const Comp0 = Cast<UPrimitiveComponent>(It.Key());
							ISolverObjectBase* const SolverObject0 = Scene->GetOwnedSolverObject(Comp0);
							TArray<int32> const* const CollisionIndices = SolverObjectToCollisionIndicesMap.Find(SolverObject0);
							if (CollisionIndices)
							{
								for (int32 EncodedCollisionIdx : *CollisionIndices)
								{
									bool bSwapOrder;
									int32 CollisionIdx = Chaos::FPBDRigidsSolver::DecodeCollisionIndex(EncodedCollisionIdx, bSwapOrder);

									Chaos::TCollisionData<float, 3> const& CollisionDataItem = CollisionData[CollisionIdx];
									const int32 ParticleIndex1 = bSwapOrder ? CollisionDataItem.ParticleIndex : CollisionDataItem.LevelsetIndex;
									ISolverObjectBase* const SolverObject1 = ParticleIndex1 < SolverObjectReverseMappingArray.Num() ? SolverObjectReverseMappingArray[ParticleIndex1].SolverObject : nullptr;
									if (!SolverObject1)
									{
										continue;
									}

									if (HandlerSet.bLegacyComponentNotify)
									{
										bool bNewEntry = false;
										FCollisionNotifyInfo& NotifyInfo = GetPendingCollisionForContactPair(SolverObject0, SolverObject1, bNewEntry);

										// #note: we only notify on the first contact, though we will still accumulate the impulse data from subsequent contacts
										const FVector NormalImpulse = FVector::DotProduct(CollisionDataItem.AccumulatedImpulse, CollisionDataItem.Normal) * CollisionDataItem.Normal;	// project impulse along normal
										const FVector FrictionImpulse = FVector(CollisionDataItem.AccumulatedImpulse) - NormalImpulse; // friction is component not along contact normal
										NotifyInfo.RigidCollisionData.TotalNormalImpulse += NormalImpulse;
										NotifyInfo.RigidCollisionData.TotalFrictionImpulse += FrictionImpulse;

										if (bNewEntry)
										{
											UPrimitiveComponent* const Comp1 = Scene->GetOwningComponent<UPrimitiveComponent>(SolverObject1);

											// fill in legacy contact data
											NotifyInfo.bCallEvent0 = true;
											// if Comp1 wants this event too, it will get its own pending collision entry, so we leave it false

											SetCollisionInfoFromComp(NotifyInfo.Info0, Comp0);
											SetCollisionInfoFromComp(NotifyInfo.Info1, Comp1);

											FRigidBodyContactInfo& NewContact = NotifyInfo.RigidCollisionData.ContactInfos.AddZeroed_GetRef();
											NewContact.ContactNormal = CollisionDataItem.Normal;
											NewContact.ContactPosition = CollisionDataItem.Location;
											NewContact.ContactPenetration = CollisionDataItem.PenetrationDepth; 
											// NewContact.PhysMaterial[1] UPhysicalMaterial required here
										}

									}

									if (HandlerSet.ChaosHandlers.Num() > 0)
									{
										bool bNewEntry = false;
										FChaosPendingCollisionNotify& ChaosNotifyInfo = GetPendingChaosCollisionForContactPair(SolverObject0, SolverObject1, bNewEntry);

										// #note: we only notify on the first contact, though we will still accumulate the impulse data from subsequent contacts
										ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse += CollisionDataItem.AccumulatedImpulse;

										if (bNewEntry)
										{
											UPrimitiveComponent* const Comp1 = Scene->GetOwningComponent<UPrimitiveComponent>(SolverObject1);

											// fill in Chaos contact data
											ChaosNotifyInfo.CollisionInfo.Component = Comp0;
											ChaosNotifyInfo.CollisionInfo.OtherComponent = Comp1;
											ChaosNotifyInfo.CollisionInfo.Location = CollisionDataItem.Location;
											ChaosNotifyInfo.NotifyRecipients = HandlerSet.ChaosHandlers;

											if (bSwapOrder)
											{
												ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse = -CollisionDataItem.AccumulatedImpulse;
												ChaosNotifyInfo.CollisionInfo.Normal = -CollisionDataItem.Normal;

												ChaosNotifyInfo.CollisionInfo.Velocity = CollisionDataItem.Velocity2;
												ChaosNotifyInfo.CollisionInfo.OtherVelocity = CollisionDataItem.Velocity1;
												ChaosNotifyInfo.CollisionInfo.AngularVelocity = CollisionDataItem.AngularVelocity2;
												ChaosNotifyInfo.CollisionInfo.OtherAngularVelocity = CollisionDataItem.AngularVelocity1;
												ChaosNotifyInfo.CollisionInfo.Mass = CollisionDataItem.Mass2;
												ChaosNotifyInfo.CollisionInfo.OtherMass = CollisionDataItem.Mass1;
											}
											else
											{
												ChaosNotifyInfo.CollisionInfo.AccumulatedImpulse = CollisionDataItem.AccumulatedImpulse;
												ChaosNotifyInfo.CollisionInfo.Normal = CollisionDataItem.Normal;

												ChaosNotifyInfo.CollisionInfo.Velocity = CollisionDataItem.Velocity1;
												ChaosNotifyInfo.CollisionInfo.OtherVelocity = CollisionDataItem.Velocity2;
												ChaosNotifyInfo.CollisionInfo.AngularVelocity = CollisionDataItem.AngularVelocity1;
												ChaosNotifyInfo.CollisionInfo.OtherAngularVelocity = CollisionDataItem.AngularVelocity2;
												ChaosNotifyInfo.CollisionInfo.Mass = CollisionDataItem.Mass1;
												ChaosNotifyInfo.CollisionInfo.OtherMass = CollisionDataItem.Mass2;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			// Tell the world and actors about the collisions
			DispatchPendingCollisionNotifies();
		}

		SCOPE_CYCLE_COUNTER(STAT_DispatchBreakEvents);

		// BREAK EVENTS
		// Same dilemma as above, do we iterate all the breaks or all the components registered for notifications? Or switch based on heuristic?
		{
			TArray<FChaosBreakEvent> PendingBreakEvents;

			// get break data from the solver
			const Chaos::FPBDRigidsSolver::FAllBreakingDataMaps& AllBreakingData_Maps = ScopedAccess.GetAllBreakings_Maps();

			if (AllBreakingData_Maps.AllBreakingData && AllBreakingData_Maps.SolverObjectReverseMapping)
			{
				const float BreakingDataTimestamp = AllBreakingData_Maps.AllBreakingData->TimeCreated;
				if (BreakingDataTimestamp > LastBreakingDataTime)
				{
					LastBreakingDataTime = BreakingDataTimestamp;

					Chaos::FPBDRigidsSolver::FBreakingDataArray const& BreakingData = AllBreakingData_Maps.AllBreakingData->AllBreakingsArray;

					// let's assume breaks are very rare, so we will iterate breaks instead of registered components for now
					const int32 NumBreaks = BreakingData.Num();
					if (NumBreaks > 0)
					{
						// Array[ParticleIndex] = SolverObject
						TArray<SolverObjectWrapper> const& ParticleIdxToSolverObjectArray = AllBreakingData_Maps.SolverObjectReverseMapping->SolverObjectReverseMappingArray;

						for (Chaos::TBreakingData<float, 3> const& BreakingDataItem : BreakingData)
						{
							ISolverObjectBase* const SolverObject = ParticleIdxToSolverObjectArray[BreakingDataItem.ParticleIndex].SolverObject;
							UPrimitiveComponent* const PrimComp = Scene->GetOwningComponent<UPrimitiveComponent>(SolverObject);
							if (PrimComp && BreakEventRegistrations.Contains(PrimComp))
							{
								// queue them up so we can release the physics data before trigging BP events
								FChaosBreakEvent& BreakEvent = PendingBreakEvents.AddZeroed_GetRef();
								BreakEvent.Component = PrimComp;
								BreakEvent.Location = BreakingDataItem.Location;
								BreakEvent.Velocity = BreakingDataItem.Velocity;
								BreakEvent.AngularVelocity = BreakingDataItem.AngularVelocity;
								BreakEvent.Mass = BreakingDataItem.Mass;
							}
						}

						DispatchPendingBreakEvents(PendingBreakEvents, BreakEventRegistrations);
					}
				}
			}
		}
	}

	// old iterate-the-entire-list way
#if 0
	const FPhysScene_Chaos* Scene = GetPhysicsScene().Get();
	if (Scene)
	{
		// Populate the pending notify list from the data in the solver
		PendingCollisionNotifies.Reset();
		ContactPairToPendingNotifyMap.Reset();

		const Chaos::PBDRigidsSolver* Solver = GetSolver();
		const Chaos::PBDRigidsSolver::FCollisionData& CollisionData = Solver->GetCollisionData_GameThread();

		for (Chaos::TCollisionData<float, 3> const& CollisionDataItem : CollisionData.CollisionDataArray)
		{
// 			Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
// 			Particles[CollisionDataItem.ParticleIndex].

			ISolverObjectBase* const SolverObject0 = nullptr;		// #todo Solver->GetSolverObject(CollisionDataItem.ParticleIndex)
			UPrimitiveComponent* Comp0 = Scene->GetOwner<UPrimitiveComponent>(SolverObject0);
			FBodyInstance const* const BI0 = Comp0 ? Comp0->GetBodyInstance() : nullptr;

			ISolverObjectBase* const SolverObject1 = nullptr;		// #todo Solver->GetSolverObject(CollisionDataItem.LevelsetIndex)
			UPrimitiveComponent* Comp1 = Scene->GetOwner<UPrimitiveComponent>(SolverObject1);
			FBodyInstance const* const BI1 = Comp1 ? Comp1->GetBodyInstance() : nullptr;

			const bool bWantsNotify0 = BI0 && BI0->bNotifyRigidBodyCollision;
			const bool bWantsNotify1 = BI1 && BI1->bNotifyRigidBodyCollision;

			if (bWantsNotify0 || bWantsNotify1)
			{
//				const FUniqueContactPairKey Pair = { Comp0, Comp1 };
				FCollisionNotifyInfo& NotifyInfo = GetPendingCollisionForContactPair(BI0, BI1);

				// #todo, use a new contact info struct that contains the more detailed contact info we have available
				FRigidBodyContactInfo& NewContact = NotifyInfo.RigidCollisionData.ContactInfos.AddZeroed_GetRef();
				NewContact.ContactNormal = CollisionDataItem.Normal;
				//NewContact.ContactPenetration = 0.f;		// #todo? 
				NewContact.ContactPosition = CollisionDataItem.Location;
				//NewContact.PhysMaterial = ;				// #todo?
			}
		}

		// Tell the world and actors about the collisions
		DispatchPendingNotifies();
	}
#endif		// 0

#endif		// INCLUDE_CHAOS
}

FCollisionNotifyInfo& UChaosGameplayEventDispatcher::GetPendingCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry)
{
	const FUniqueContactPairKey Key = { P0, P1 };
	const int32* PendingNotifyIdx = ContactPairToPendingNotifyMap.Find(Key);
	if (PendingNotifyIdx)
	{
		// we already have one for this pair
		bNewEntry = false;
		return PendingCollisionNotifies[*PendingNotifyIdx];
	}

	// make a new entry
	bNewEntry = true;
	int32 NewIdx = PendingCollisionNotifies.AddZeroed();
	return PendingCollisionNotifies[NewIdx];
}

FChaosPendingCollisionNotify& UChaosGameplayEventDispatcher::GetPendingChaosCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry)
{
	const FUniqueContactPairKey Key = { P0, P1 };
	const int32* PendingNotifyIdx = ContactPairToPendingChaosNotifyMap.Find(Key);
	if (PendingNotifyIdx)
	{
		// we already have one for this pair
		bNewEntry = false;
		return PendingChaosCollisionNotifies[*PendingNotifyIdx];
	}

	// make a new entry
	bNewEntry = true;
	int32 NewIdx = PendingChaosCollisionNotifies.AddZeroed();
	return PendingChaosCollisionNotifies[NewIdx];
}

void UChaosGameplayEventDispatcher::DispatchPendingCollisionNotifies()
{
	UWorld const* const OwningWorld = GetWorld();

	// Let the game-specific PhysicsCollisionHandler process any physics collisions that took place
	if (OwningWorld != nullptr && OwningWorld->PhysicsCollisionHandler != nullptr)
	{
		OwningWorld->PhysicsCollisionHandler->HandlePhysicsCollisions_AssumesLocked(PendingCollisionNotifies);
	}

	// Fire any collision notifies in the queue.
	for (FCollisionNotifyInfo& NotifyInfo : PendingCollisionNotifies)
	{
//		if (NotifyInfo.RigidCollisionData.ContactInfos.Num() > 0)
		{
			if (NotifyInfo.bCallEvent0 && /*NotifyInfo.IsValidForNotify() && */ NotifyInfo.Info0.Actor.IsValid())
			{
				NotifyInfo.Info0.Actor->DispatchPhysicsCollisionHit(NotifyInfo.Info0, NotifyInfo.Info1, NotifyInfo.RigidCollisionData);
			}

			// CHAOS: don't call event 1, because the code below will generate the reflexive hit data as separate entries
		}
	}
	for (FChaosPendingCollisionNotify& NotifyInfo : PendingChaosCollisionNotifies)
	{
		for (UObject* Obj : NotifyInfo.NotifyRecipients)
		{
			IChaosNotifyHandlerInterface* const Handler = Cast< IChaosNotifyHandlerInterface>(Obj);
			ensure(Handler);
			if (Handler)
			{
				Handler->HandlePhysicsCollision(NotifyInfo.CollisionInfo);
			}
		}
	}

	PendingCollisionNotifies.Reset();
	PendingChaosCollisionNotifies.Reset();
}

void UChaosGameplayEventDispatcher::RegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify)
{
	FChaosHandlerSet& HandlerSet = CollisionEventRegistrations.FindOrAdd(ComponentToListenTo);

	if (IChaosNotifyHandlerInterface* ChaosHandler = Cast<IChaosNotifyHandlerInterface>(ObjectToNotify))
	{
		HandlerSet.ChaosHandlers.Add(ObjectToNotify);
	}
	
	// a component can also implement the handler interface to get both types of events, so these aren't mutually exclusive
	if (ObjectToNotify == ComponentToListenTo)
	{
		HandlerSet.bLegacyComponentNotify = true;
	}

	// note: theoretically supportable to have external listeners to the legacy-style notifies, but will take more plumbing
}

void UChaosGameplayEventDispatcher::UnRegisterForCollisionEvents(UPrimitiveComponent* ComponentToListenTo, UObject* ObjectToNotify)
{
	FChaosHandlerSet* HandlerSet = CollisionEventRegistrations.Find(ComponentToListenTo);
	if (HandlerSet)
	{
		HandlerSet->ChaosHandlers.Remove(ObjectToNotify);

		if (ObjectToNotify == ComponentToListenTo)
		{
			HandlerSet->bLegacyComponentNotify = false;
		}

		if ((HandlerSet->ChaosHandlers.Num() == 0) && (HandlerSet->bLegacyComponentNotify == false))
		{
			// no one listening to this component any more, remove it entirely
			CollisionEventRegistrations.Remove(ComponentToListenTo);
		}
	}
}

void UChaosGameplayEventDispatcher::RegisterForBreakEvents(UPrimitiveComponent* Component, FOnBreakEventCallback InFunc)
{
	if (Component)
	{
		FBreakEventCallbackWrapper F = { InFunc };
		BreakEventRegistrations.Add(Component, F);
	}
}

void UChaosGameplayEventDispatcher::UnRegisterForBreakEvents(UPrimitiveComponent* Component)
{
	if (Component)
	{
		BreakEventRegistrations.Remove(Component);
	}
}

// PRAGMA_ENABLE_OPTIMIZATION