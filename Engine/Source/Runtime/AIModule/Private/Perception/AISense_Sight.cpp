// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Perception/AISense_Sight.h"
#include "EngineDefines.h"
#include "EngineGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "AISystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Perception/AISightTargetInterface.h"
#include "Perception/AISenseConfig_Sight.h"

#define DO_SIGHT_VLOGGING (0 && ENABLE_VISUAL_LOG)

#if DO_SIGHT_VLOGGING
	#define SIGHT_LOG_SEGMENT UE_VLOG_SEGMENT
	#define SIGHT_LOG_LOCATION UE_VLOG_LOCATION
#else
	#define SIGHT_LOG_SEGMENT(...)
	#define SIGHT_LOG_LOCATION(...)
#endif // DO_SIGHT_VLOGGING

DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight"),STAT_AI_Sense_Sight,STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Update Sort"),STAT_AI_Sense_Sight_UpdateSort,STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Listener Update"), STAT_AI_Sense_Sight_ListenerUpdate, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Register Target"), STAT_AI_Sense_Sight_RegisterTarget, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove By Listener"), STAT_AI_Sense_Sight_RemoveByListener, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove To Target"), STAT_AI_Sense_Sight_RemoveToTarget, STATGROUP_AI);


static const int32 DefaultMaxTracesPerTick = 6;
static const int32 DefaultMinQueriesPerTimeSliceCheck = 40;

//----------------------------------------------------------------------//
// helpers
//----------------------------------------------------------------------//
FORCEINLINE_DEBUGGABLE bool CheckIsTargetInSightPie(const FPerceptionListener& Listener, const UAISense_Sight::FDigestedSightProperties& DigestedProps, const FVector& TargetLocation, const float SightRadiusSq)
{
	if (FVector::DistSquared(Listener.CachedLocation, TargetLocation) <= SightRadiusSq) 
	{
		const FVector DirectionToTarget = (TargetLocation - Listener.CachedLocation).GetUnsafeNormal();
		return FVector::DotProduct(DirectionToTarget, Listener.CachedDirection) > DigestedProps.PeripheralVisionAngleCos;
	}

	return false;
}

//----------------------------------------------------------------------//
// FAISightTarget
//----------------------------------------------------------------------//
const FAISightTarget::FTargetId FAISightTarget::InvalidTargetId = FAISystem::InvalidUnsignedID;

FAISightTarget::FAISightTarget(AActor* InTarget, FGenericTeamId InTeamId)
	: Target(InTarget), SightTargetInterface(NULL), TeamId(InTeamId)
{
	if (InTarget)
	{
		TargetId = InTarget->GetUniqueID();
	}
	else
	{
		TargetId = InvalidTargetId;
	}
}

//----------------------------------------------------------------------//
// FDigestedSightProperties
//----------------------------------------------------------------------//
UAISense_Sight::FDigestedSightProperties::FDigestedSightProperties(const UAISenseConfig_Sight& SenseConfig)
{
	SightRadiusSq = FMath::Square(SenseConfig.SightRadius);
	LoseSightRadiusSq = FMath::Square(SenseConfig.LoseSightRadius);
	PeripheralVisionAngleCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(SenseConfig.PeripheralVisionAngleDegrees), 0.f, PI));
	AffiliationFlags = SenseConfig.DetectionByAffiliation.GetAsFlags();
	// keep the special value of FAISystem::InvalidRange (-1.f) if it's set.
	AutoSuccessRangeSqFromLastSeenLocation = (SenseConfig.AutoSuccessRangeFromLastSeenLocation == FAISystem::InvalidRange) ? FAISystem::InvalidRange : FMath::Square(SenseConfig.AutoSuccessRangeFromLastSeenLocation);
}

UAISense_Sight::FDigestedSightProperties::FDigestedSightProperties()
	: PeripheralVisionAngleCos(0.f), SightRadiusSq(-1.f), AutoSuccessRangeSqFromLastSeenLocation(FAISystem::InvalidRange), LoseSightRadiusSq(-1.f), AffiliationFlags(-1)
{}

//----------------------------------------------------------------------//
// UAISense_Sight
//----------------------------------------------------------------------//
UAISense_Sight::UAISense_Sight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaxTracesPerTick(DefaultMaxTracesPerTick)
	, MinQueriesPerTimeSliceCheck(DefaultMinQueriesPerTimeSliceCheck)
	, MaxTimeSlicePerTick(0.005) // 5ms
	, HighImportanceQueryDistanceThreshold(300.f)
	, MaxQueryImportance(60.f)
	, SightLimitQueryImportance(10.f)
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		UAISenseConfig_Sight* SightConfigCDO = GetMutableDefault<UAISenseConfig_Sight>();
		SightConfigCDO->Implementation = UAISense_Sight::StaticClass();

		OnNewListenerDelegate.BindUObject(this, &UAISense_Sight::OnNewListenerImpl);
		OnListenerUpdateDelegate.BindUObject(this, &UAISense_Sight::OnListenerUpdateImpl);
		OnListenerRemovedDelegate.BindUObject(this, &UAISense_Sight::OnListenerRemovedImpl);
	}

	NotifyType = EAISenseNotifyType::OnPerceptionChange;
	
	bAutoRegisterAllPawnsAsSources = true;
	bNeedsForgettingNotification = true;

	DefaultSightCollisionChannel = GET_AI_CONFIG_VAR(DefaultSightCollisionChannel);
}

FORCEINLINE_DEBUGGABLE float UAISense_Sight::CalcQueryImportance(const FPerceptionListener& Listener, const FVector& TargetLocation, const float SightRadiusSq) const
{
	const float DistanceSq = FVector::DistSquared(Listener.CachedLocation, TargetLocation);
	return DistanceSq <= HighImportanceDistanceSquare ? MaxQueryImportance
		: FMath::Clamp((SightLimitQueryImportance - MaxQueryImportance) / SightRadiusSq * DistanceSq + MaxQueryImportance, 0.f, MaxQueryImportance);
}

void UAISense_Sight::PostInitProperties()
{
	Super::PostInitProperties();
	HighImportanceDistanceSquare = FMath::Square(HighImportanceQueryDistanceThreshold);
}

bool UAISense_Sight::ShouldAutomaticallySeeTarget(const FDigestedSightProperties& PropDigest, FAISightQuery* SightQuery, FPerceptionListener& Listener, AActor* TargetActor, float& OutStimulusStrength) const
{
	OutStimulusStrength = 1.0f;

	if ((PropDigest.AutoSuccessRangeSqFromLastSeenLocation != FAISystem::InvalidRange) && (SightQuery->LastSeenLocation != FAISystem::InvalidLocation))
	{
		const float DistanceToLastSeenLocationSq = FVector::DistSquared(TargetActor->GetActorLocation(), SightQuery->LastSeenLocation);
		return (DistanceToLastSeenLocationSq <= PropDigest.AutoSuccessRangeSqFromLastSeenLocation);
	}

	return false;
}

float UAISense_Sight::Update()
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight);

	const UWorld* World = GEngine->GetWorldFromContextObject(GetPerceptionSystem()->GetOuter(), EGetWorldErrorMode::LogAndReturnNull);

	if (World == NULL)
	{
		return SuspendNextUpdate;
	}

	int32 TracesCount = 0;
	int32 NumQueriesProcessed = 0;
	double TimeSliceEnd = FPlatformTime::Seconds() + MaxTimeSlicePerTick;
	bool bHitTimeSliceLimit = false;
//#define AISENSE_SIGHT_TIMESLICING_DEBUG
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
	double TimeSpent = 0.0;
	double LastTime = FPlatformTime::Seconds();
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG
	static const int32 InitialInvalidItemsSize = 16;
	TArray<int32> InvalidQueries;
	TArray<FAISightTarget::FTargetId> InvalidTargets;
	InvalidQueries.Reserve(InitialInvalidItemsSize);
	InvalidTargets.Reserve(InitialInvalidItemsSize);

	AIPerception::FListenerMap& ListenersMap = *GetListeners();

	FAISightQuery* SightQuery = SightQueryQueue.GetData();
	for (int32 QueryIndex = 0; QueryIndex < SightQueryQueue.Num(); ++QueryIndex, ++SightQuery)
	{
		// Time slice limit check - spread out checks to every N queries so we don't spend more time checking timer than doing work
		NumQueriesProcessed++;
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
		TimeSpent += (FPlatformTime::Seconds() - LastTime);
		LastTime = FPlatformTime::Seconds();
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG
		if (bHitTimeSliceLimit == false && (NumQueriesProcessed % MinQueriesPerTimeSliceCheck) == 0 && FPlatformTime::Seconds() > TimeSliceEnd)
		{
			bHitTimeSliceLimit = true;
			// do not break here since that would bypass queue aging
		}

		if (TracesCount < MaxTracesPerTick && bHitTimeSliceLimit == false)
		{
			FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
			FAISightTarget& Target = ObservedTargets[SightQuery->TargetId];

			AActor* TargetActor = Target.Target.Get();
			UAIPerceptionComponent* ListenerPtr = Listener.Listener.Get();
			ensure(ListenerPtr);

			// @todo figure out what should we do if not valid
			if (TargetActor && ListenerPtr)
			{
				const FVector TargetLocation = TargetActor->GetActorLocation();
				const FDigestedSightProperties& PropDigest = DigestedProperties[SightQuery->ObserverId];
				const float SightRadiusSq = SightQuery->bLastResult ? PropDigest.LoseSightRadiusSq : PropDigest.SightRadiusSq;
				
				float StimulusStrength = 1.f;
				
				// @Note that automagical "seeing" does not care about sight range nor vision cone
				const bool bShouldAutomatically = ShouldAutomaticallySeeTarget(PropDigest, SightQuery, Listener, TargetActor, StimulusStrength);
				if (bShouldAutomatically)
				{
					// Pretend like we've seen this target where we last saw them
					Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, StimulusStrength, SightQuery->LastSeenLocation, Listener.CachedLocation));
					SightQuery->bLastResult = true;
				}
				else if (CheckIsTargetInSightPie(Listener, PropDigest, TargetLocation, SightRadiusSq))
				{
					SIGHT_LOG_SEGMENT(ListenerPtr->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Green, TEXT("%s"), *(Target.TargetId.ToString()));

					FVector OutSeenLocation(0.f);
					// do line checks
					if (Target.SightTargetInterface != NULL)
					{
						int32 NumberOfLoSChecksPerformed = 0;
						// defaulting to 1 to have "full strength" by default instead of "no strength"
						if (Target.SightTargetInterface->CanBeSeenFrom(Listener.CachedLocation, OutSeenLocation, NumberOfLoSChecksPerformed, StimulusStrength, ListenerPtr->GetBodyActor()) == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, StimulusStrength, OutSeenLocation, Listener.CachedLocation));
							SightQuery->bLastResult = true;
							SightQuery->LastSeenLocation = OutSeenLocation;
						}
						// communicate failure only if we've seen give actor before
						else if (SightQuery->bLastResult == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
							SightQuery->bLastResult = false;
							SightQuery->LastSeenLocation = FAISystem::InvalidLocation;
						}

						if (SightQuery->bLastResult == false)
						{
							SIGHT_LOG_LOCATION(ListenerPtr->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}

						TracesCount += NumberOfLoSChecksPerformed;
					}
					else
					{
						// we need to do tests ourselves
						FHitResult HitResult;
						const bool bHit = World->LineTraceSingleByChannel(HitResult, Listener.CachedLocation, TargetLocation
							, DefaultSightCollisionChannel
							, FCollisionQueryParams(SCENE_QUERY_STAT(AILineOfSight), true, ListenerPtr->GetBodyActor()));

						++TracesCount;

						auto HitResultActorIsOwnedByTargetActor = [&HitResult, TargetActor]()
						{
							AActor* HitResultActor = HitResult.Actor.Get();
							return (HitResultActor ? HitResultActor->IsOwnedBy(TargetActor) : false);
						};

						if (bHit == false || HitResultActorIsOwnedByTargetActor())
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 1.f, TargetLocation, Listener.CachedLocation));
							SightQuery->bLastResult = true;
							SightQuery->LastSeenLocation = TargetLocation;
						}
						// communicate failure only if we've seen give actor before
						else if (SightQuery->bLastResult == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
							SightQuery->bLastResult = false;
							SightQuery->LastSeenLocation = FAISystem::InvalidLocation;
						}

						if (SightQuery->bLastResult == false)
						{
							SIGHT_LOG_LOCATION(ListenerPtr->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}
					}
				}
				// communicate failure only if we've seen give actor before
				else if (SightQuery->bLastResult)
				{
					SIGHT_LOG_SEGMENT(ListenerPtr->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Red, TEXT("%s"), *(Target.TargetId.ToString()));
					Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					SightQuery->bLastResult = false;
				}

				SightQuery->Importance = CalcQueryImportance(Listener, TargetLocation, SightRadiusSq);

				// restart query
				SightQuery->Age = 0.f;
			}
			else
			{
				// put this index to "to be removed" array
				InvalidQueries.Add(QueryIndex);
				if (TargetActor == nullptr)
				{
					InvalidTargets.AddUnique(SightQuery->TargetId);
				}
			}
		}
		else
		{
			// age unprocessed queries so that they can advance in the queue during next sort
			SightQuery->Age += 1.f;
		}

		SightQuery->RecalcScore();
	}
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
	UE_LOG(LogAIPerception, VeryVerbose, TEXT("UAISense_Sight::Update processed %d sources in %f seconds [time slice limited? %d]"), NumQueriesProcessed, TimeSpent, bHitTimeSliceLimit ? 1 : 0);
#else
	UE_LOG(LogAIPerception, VeryVerbose, TEXT("UAISense_Sight::Update processed %d sources [time slice limited? %d]"), NumQueriesProcessed, bHitTimeSliceLimit ? 1 : 0);
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG

	if (InvalidQueries.Num() > 0)
	{
		for (int32 Index = InvalidQueries.Num() - 1; Index >= 0; --Index)
		{
			// removing with swapping here, since queue is going to be sorted anyway
			SightQueryQueue.RemoveAtSwap(InvalidQueries[Index], 1, /*bAllowShrinking*/false);
		}

		if (InvalidTargets.Num() > 0)
		{
			// this should not be happening since UAIPerceptionSystem::OnPerceptionStimuliSourceEndPlay introduction
			UE_VLOG(GetPerceptionSystem(), LogAIPerception, Error, TEXT("Invalid sight targets found during UAISense_Sight::Update call"));

			for (const auto& TargetId : InvalidTargets)
			{
				// remove affected queries
				RemoveAllQueriesToTarget(TargetId, DontSort);
				// remove target itself
				ObservedTargets.Remove(TargetId);
			}

			// remove holes
			ObservedTargets.Compact();
		}
	}

	// sort Sight Queries
	{
		SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_UpdateSort);
		SortQueries();
	}

	//return SightQueryQueue.Num() > 0 ? 1.f/6 : FLT_MAX;
	return 0.f;
}

void UAISense_Sight::RegisterEvent(const FAISightEvent& Event)
{

}

void UAISense_Sight::RegisterSource(AActor& SourceActor)
{
	RegisterTarget(SourceActor, Sort);
}

void UAISense_Sight::UnregisterSource(AActor& SourceActor)
{
	const FAISightTarget::FTargetId AsTargetId = SourceActor.GetUniqueID();
	FAISightTarget AsTarget;
	
	if (ObservedTargets.RemoveAndCopyValue(AsTargetId, AsTarget)
		&& SightQueryQueue.Num() > 0)
	{
		AActor* TargetActor = AsTarget.Target.Get();

		if (TargetActor)
		{
			// notify all interested observers that this source is no longer
			// visible		
			AIPerception::FListenerMap& ListenersMap = *GetListeners();
			const FAISightQuery* SightQuery = &SightQueryQueue[SightQueryQueue.Num() - 1];
			for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0; --QueryIndex, --SightQuery)
			{
				if (SightQuery->TargetId == AsTargetId)
				{
					if (SightQuery->bLastResult == true)
					{
						FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
						ensure(Listener.Listener.IsValid());

						Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, SightQuery->LastSeenLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					}

					SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
				}
			}
			// no point in sorting, we haven't change the order of other queries
		}
	}
}

void UAISense_Sight::CleanseInvalidSources()
{
	bool bInvalidSourcesFound = false;
	int32 NumInvalidSourcesFound = 0;
	for (FTargetsContainer::TIterator ItTarget(ObservedTargets); ItTarget; ++ItTarget)
	{
		if (ItTarget->Value.Target.IsValid() == false)
		{
			// remove affected queries
			RemoveAllQueriesToTarget(ItTarget->Key, DontSort);
			// remove target itself
			ItTarget.RemoveCurrent();

			bInvalidSourcesFound = true;
			NumInvalidSourcesFound++;
		}
	}

	UE_LOG(LogAIPerception, Verbose, TEXT("UAISense_Sight::CleanseInvalidSources called and removed %d invalid sources"), NumInvalidSourcesFound);

	if (bInvalidSourcesFound)
	{
		// remove holes
		ObservedTargets.Compact();
		SortQueries();
	}
	else
	{
		UE_VLOG(GetPerceptionSystem(), LogAIPerception, Error, TEXT("UAISense_Sight::CleanseInvalidSources called and no invalid targets were found"));
	}
}

bool UAISense_Sight::RegisterTarget(AActor& TargetActor, FQueriesOperationPostProcess PostProcess)
{
	return RegisterTarget(TargetActor, PostProcess, [](FAISightQuery& Query) {});
}

bool UAISense_Sight::RegisterTarget(AActor& TargetActor, FQueriesOperationPostProcess PostProcess, TFunctionRef<void(FAISightQuery&)> OnAddedFunc)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RegisterTarget);
	
	FAISightTarget* SightTarget = ObservedTargets.Find(TargetActor.GetUniqueID());
	
	if (SightTarget != nullptr && SightTarget->GetTargetActor() != &TargetActor)
	{
		// this means given unique ID has already been recycled. 
		FAISightTarget NewSightTarget(&TargetActor);

		SightTarget = &(ObservedTargets.Add(NewSightTarget.TargetId, NewSightTarget));
		SightTarget->SightTargetInterface = Cast<IAISightTargetInterface>(&TargetActor);
	}
	else if (SightTarget == nullptr)
	{
		FAISightTarget NewSightTarget(&TargetActor);

		SightTarget = &(ObservedTargets.Add(NewSightTarget.TargetId, NewSightTarget));
		SightTarget->SightTargetInterface = Cast<IAISightTargetInterface>(&TargetActor);
	}

	// set/update data
	SightTarget->TeamId = FGenericTeamId::GetTeamIdentifier(&TargetActor);
	
	// generate all pairs and add them to current Sight Queries
	bool bNewQueriesAdded = false;
	AIPerception::FListenerMap& ListenersMap = *GetListeners();
	const FVector TargetLocation = TargetActor.GetActorLocation();

	for (AIPerception::FListenerMap::TConstIterator ItListener(ListenersMap); ItListener; ++ItListener)
	{
		const FPerceptionListener& Listener = ItListener->Value;
		const IGenericTeamAgentInterface* ListenersTeamAgent = Listener.GetTeamAgent();

		if (Listener.HasSense(GetSenseID()) && Listener.GetBodyActor() != &TargetActor)
		{
			const FDigestedSightProperties& PropDigest = DigestedProperties[Listener.GetListenerID()];
			if (FAISenseAffiliationFilter::ShouldSenseTeam(ListenersTeamAgent, TargetActor, PropDigest.AffiliationFlags))
			{
				// create a sight query		
				FAISightQuery& AddedQuery = SightQueryQueue.AddDefaulted_GetRef();
				AddedQuery.ObserverId = ItListener->Key;
				AddedQuery.TargetId = SightTarget->TargetId;
				AddedQuery.Importance = CalcQueryImportance(ItListener->Value, TargetLocation, PropDigest.SightRadiusSq);
				
				OnAddedFunc(AddedQuery);
				bNewQueriesAdded = true;
			}
		}
	}

	// sort Sight Queries
	if (PostProcess == Sort && bNewQueriesAdded)
	{
		SortQueries();
		RequestImmediateUpdate();
	}

	return bNewQueriesAdded;
}

void UAISense_Sight::OnNewListenerImpl(const FPerceptionListener& NewListener)
{
	UAIPerceptionComponent* NewListenerPtr = NewListener.Listener.Get();
	check(NewListenerPtr);
	const UAISenseConfig_Sight* SenseConfig = Cast<const UAISenseConfig_Sight>(NewListenerPtr->GetSenseConfig(GetSenseID()));
	check(SenseConfig);
	const FDigestedSightProperties PropertyDigest(*SenseConfig);
	DigestedProperties.Add(NewListener.GetListenerID(), PropertyDigest);

	GenerateQueriesForListener(NewListener, PropertyDigest);
}

void UAISense_Sight::GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest)
{
	GenerateQueriesForListener(Listener, PropertyDigest, [](FAISightQuery& Query) {});
}

void UAISense_Sight::GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest, TFunctionRef<void(FAISightQuery&)> OnAddedFunc)
{
	bool bNewQueriesAdded = false;
	const IGenericTeamAgentInterface* ListenersTeamAgent = Listener.GetTeamAgent();
	const AActor* Avatar = Listener.GetBodyActor();

	// create sight queries with all legal targets
	for (FTargetsContainer::TConstIterator ItTarget(ObservedTargets); ItTarget; ++ItTarget)
	{
		const AActor* TargetActor = ItTarget->Value.GetTargetActor();
		if (TargetActor == NULL || TargetActor == Avatar)
		{
			continue;
		}

		if (FAISenseAffiliationFilter::ShouldSenseTeam(ListenersTeamAgent, *TargetActor, PropertyDigest.AffiliationFlags))
		{
			// create a sight query		
			FAISightQuery& AddedQuery = SightQueryQueue.AddDefaulted_GetRef();
			AddedQuery.ObserverId = Listener.GetListenerID();
			AddedQuery.TargetId = ItTarget->Key;
			AddedQuery.Importance = CalcQueryImportance(Listener, ItTarget->Value.GetLocationSimple(), PropertyDigest.SightRadiusSq);

			OnAddedFunc(AddedQuery);
			bNewQueriesAdded = true;
		}
	}

	// sort Sight Queries
	if (bNewQueriesAdded)
	{
		SortQueries();
		RequestImmediateUpdate();
	}
}

void UAISense_Sight::OnListenerUpdateImpl(const FPerceptionListener& UpdatedListener)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_ListenerUpdate);

	// first, naive implementation:
	// 1. remove all queries by this listener
	// 2. proceed as if it was a new listener

	// see if this listener is a Target as well
	const FAISightTarget::FTargetId AsTargetId = UpdatedListener.GetBodyActorUniqueID();
	FAISightTarget* AsTarget = ObservedTargets.Find(AsTargetId);
	if (AsTarget != NULL)
	{
		if (AsTarget->Target.IsValid())
		{
			// if still a valid target then backup list of observers for which the listener was visible to restore in the newly created queries
			TSet<FPerceptionListenerID> LastVisibleObservers;
			RemoveAllQueriesToTarget(AsTargetId, DontSort, [&LastVisibleObservers](const FAISightQuery& Query)
			{
				if (Query.bLastResult)
				{
					LastVisibleObservers.Add(Query.ObserverId);
				}
			});

			RegisterTarget(*(AsTarget->Target.Get()), DontSort, [&LastVisibleObservers](FAISightQuery& Query)
			{
				Query.bLastResult = LastVisibleObservers.Contains(Query.ObserverId);
			});
		}
		else
		{
			RemoveAllQueriesToTarget(AsTargetId, DontSort);
		}
	}

	const FPerceptionListenerID ListenerID = UpdatedListener.GetListenerID();

	if (UpdatedListener.HasSense(GetSenseID()))
	{
		// if still a valid sense then backup list of targets that were visible by the listener to restore in the newly created queries
		TSet<FAISightTarget::FTargetId> LastVisibleTargets;
		RemoveAllQueriesByListener(UpdatedListener, DontSort, [&LastVisibleTargets](const FAISightQuery& Query)
		{
			if (Query.bLastResult)
			{
				LastVisibleTargets.Add(Query.TargetId);
			}			
		});

		const UAISenseConfig_Sight* SenseConfig = Cast<const UAISenseConfig_Sight>(UpdatedListener.Listener->GetSenseConfig(GetSenseID()));
		check(SenseConfig);
		FDigestedSightProperties& PropertiesDigest = DigestedProperties.FindOrAdd(ListenerID);
		PropertiesDigest = FDigestedSightProperties(*SenseConfig);

		GenerateQueriesForListener(UpdatedListener, PropertiesDigest, [&LastVisibleTargets](FAISightQuery& Query)
		{
			Query.bLastResult = LastVisibleTargets.Contains(Query.TargetId);
		});
	}
	else
	{
		// remove all queries
		RemoveAllQueriesByListener(UpdatedListener, DontSort);

		DigestedProperties.Remove(ListenerID);
	}
}

void UAISense_Sight::OnListenerRemovedImpl(const FPerceptionListener& UpdatedListener)
{
	RemoveAllQueriesByListener(UpdatedListener, DontSort);

	DigestedProperties.FindAndRemoveChecked(UpdatedListener.GetListenerID());

	// note: there use to be code to remove all queries _to_ listener here as well
	// but that was wrong - the fact that a listener gets unregistered doesn't have to
	// mean it's being removed from the game altogether.
}

void UAISense_Sight::RemoveAllQueriesByListener(const FPerceptionListener& Listener, FQueriesOperationPostProcess PostProcess)
{
	RemoveAllQueriesByListener(Listener, PostProcess, [](const FAISightQuery& Query) {});
}

void UAISense_Sight::RemoveAllQueriesByListener(const FPerceptionListener& Listener, FQueriesOperationPostProcess PostProcess, TFunctionRef<void(const FAISightQuery&)> OnRemoveFunc)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveByListener);

	if (SightQueryQueue.Num() == 0)
	{
		return;
	}

	const uint32 ListenerId = Listener.GetListenerID();
	bool bQueriesRemoved = false;
	
	for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0 ; --QueryIndex)
	{
		const FAISightQuery& SightQuery = SightQueryQueue[QueryIndex];

		if (SightQuery.ObserverId == ListenerId)
		{
			OnRemoveFunc(SightQuery);
			SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
			bQueriesRemoved = true;
		}
	}

	if (PostProcess == Sort && bQueriesRemoved)
	{
		SortQueries();
	}
}

void UAISense_Sight::RemoveAllQueriesToTarget(const FAISightTarget::FTargetId& TargetId, FQueriesOperationPostProcess PostProcess)
{
	RemoveAllQueriesToTarget(TargetId, PostProcess, [](const FAISightQuery& Query) {});
}

void UAISense_Sight::RemoveAllQueriesToTarget(const FAISightTarget::FTargetId& TargetId, FQueriesOperationPostProcess PostProcess, TFunctionRef<void(const FAISightQuery&)> OnRemoveFunc)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveToTarget);

	if (SightQueryQueue.Num() == 0)
	{
		return;
	}

	bool bQueriesRemoved = false;

	for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0; --QueryIndex)
	{
		const FAISightQuery& SightQuery = SightQueryQueue[QueryIndex];

		if (SightQuery.TargetId == TargetId)
		{
			OnRemoveFunc(SightQuery);
			SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
			bQueriesRemoved = true;
		}
	}

	if (PostProcess == Sort && bQueriesRemoved)
	{
		SortQueries();
	}
}

void UAISense_Sight::OnListenerForgetsActor(const FPerceptionListener& Listener, AActor& ActorToForget)
{
	const uint32 ListenerId = Listener.GetListenerID();
	const uint32 TargetId = ActorToForget.GetUniqueID();
	
	for (FAISightQuery& SightQuery : SightQueryQueue)
	{
		if (SightQuery.ObserverId == ListenerId && SightQuery.TargetId == TargetId)
		{
			// assuming one query per observer-target pair
			SightQuery.ForgetPreviousResult();
			break;
		}
	}
}

void UAISense_Sight::OnListenerForgetsAll(const FPerceptionListener& Listener)
{
	const uint32 ListenerId = Listener.GetListenerID();

	for (FAISightQuery& SightQuery : SightQueryQueue)
	{
		if (SightQuery.ObserverId == ListenerId)
		{
			SightQuery.ForgetPreviousResult();
		}
	}
}
