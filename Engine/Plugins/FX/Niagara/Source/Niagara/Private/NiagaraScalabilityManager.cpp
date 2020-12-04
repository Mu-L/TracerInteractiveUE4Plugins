// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraScalabilityManager.h"
#include "NiagaraWorldManager.h"
#include "NiagaraComponent.h"

static float GScalabilityUpdateTime_Low = 1.0f;
static float GScalabilityUpdateTime_Medium = 0.5f;
static float GScalabilityUpdateTime_High = 0.25f;
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_Low(TEXT("fx.NiagaraScalabilityUpdateTime_Low"), GScalabilityUpdateTime_Low, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at Low frequency. \n"), ECVF_Default);
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_Medium(TEXT("fx.NiagaraScalabilityUpdateTime_Medium"), GScalabilityUpdateTime_Medium, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at Medium frequency. \n"), ECVF_Default);
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_High(TEXT("fx.NiagaraScalabilityUpdateTime_High"), GScalabilityUpdateTime_High, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at High frequency. \n"), ECVF_Default);

static int32 GScalabilityManParallelThreshold = 50;
static FAutoConsoleVariableRef CVarScalabilityManParallelThreshold(TEXT("fx.ScalabilityManParallelThreshold"), GScalabilityManParallelThreshold, TEXT("Number of instances required for a niagara significance manger to go parallel for it's update. \n"), ECVF_Default);

FNiagaraScalabilityManager::FNiagaraScalabilityManager()
	: EffectType(nullptr)
	, LastUpdateTime(0.0f)
{

}

FNiagaraScalabilityManager::~FNiagaraScalabilityManager()
{
	for(UNiagaraComponent* Component : ManagedComponents)
	{
		if (Component)
		{
			Component->ScalabilityManagerHandle = INDEX_NONE;
		}
	}
	ManagedComponents.Empty();
}

void FNiagaraScalabilityManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(EffectType);
	Collector.AddReferencedObjects(ManagedComponents);
}


void FNiagaraScalabilityManager::PreGarbageCollectBeginDestroy()
{
	//After the GC has potentially nulled out references to the components we were tracking we clear them out here.
	//This should only be in the case where MarkPendingKill() is called directly. Typical component destruction will unregister in OnComponentDestroyed() or OnUnregister().
	//Components then just clear their handle in BeginDestroy knowing they've already been removed from the manager.
	//I would prefer some pre BeginDestroy() callback into the component in which I could cleanly unregister with the manager in all cases but I don't think that's possible.
	int32 CompIdx = ManagedComponents.Num();
	while (--CompIdx >= 0)
	{
		UNiagaraComponent* Comp = ManagedComponents[CompIdx];
		if (Comp == nullptr)
		{
			//UE_LOG(LogNiagara, Warning, TEXT("Unregister from PreGCBeginDestroy @%d/%d - %s"), CompIdx, ManagedComponents.Num(), *EffectType->GetName());
			UnregisterAt(CompIdx);
		}
		else if (Comp->IsPendingKillOrUnreachable())
		{
			Unregister(Comp);
		}
	}
}

void FNiagaraScalabilityManager::Register(UNiagaraComponent* Component)
{
	check(Component->ScalabilityManagerHandle == INDEX_NONE);
	check(ManagedComponents.Num() == State.Num());

	Component->ScalabilityManagerHandle = ManagedComponents.Add(Component);
	State.AddDefaulted();

	//UE_LOG(LogNiagara, Warning, TEXT("Registered Component %p at index %d"), Component, Component->ScalabilityManagerHandle);
}

void FNiagaraScalabilityManager::Unregister(UNiagaraComponent* Component)
{
	check(Component->ScalabilityManagerHandle != INDEX_NONE);

	int32 IndexToRemove = Component->ScalabilityManagerHandle;
	Component->ScalabilityManagerHandle = INDEX_NONE;
	UnregisterAt(IndexToRemove);
}

void FNiagaraScalabilityManager::UnregisterAt(int32 IndexToRemove)
{
	//UE_LOG(LogNiagara, Warning, TEXT("Unregistering Component %p at index %d (Replaced with %p)"), ManagedComponents[IndexToRemove], IndexToRemove, ManagedComponents.Num() > 1 ? ManagedComponents.Last() : nullptr);

	check(ManagedComponents.Num() == State.Num());
	if (ManagedComponents.IsValidIndex(IndexToRemove))
	{
		ManagedComponents.RemoveAtSwap(IndexToRemove);
		State.RemoveAtSwap(IndexToRemove);
	}
	else
	{
		UE_LOG(LogNiagara, Warning, TEXT("Attempting to unregister an invalid index from the Scalability Manager. Index: %d - Num: %d"), IndexToRemove, ManagedComponents.Num());
	}

	//Redirect the component that is now at IndexToRemove to the correct index.
	if (ManagedComponents.IsValidIndex(IndexToRemove))
	{
		if ((ManagedComponents[IndexToRemove] != nullptr))//Possible this has been GCd. It will be removed later if so.
		{
			ManagedComponents[IndexToRemove]->ScalabilityManagerHandle = IndexToRemove;
		}
	}
}

void FNiagaraScalabilityManager::Update(FNiagaraWorldManager* WorldMan, bool bNewOnly)
{
	//Paranoia code in case the EffectType is GCd from under us.
	if (EffectType == nullptr)
	{
		ManagedComponents.Empty();
		State.Empty();
		LastUpdateTime = 0.0f;
	}

	float WorldTime = WorldMan->GetWorld()->GetTimeSeconds();

	UNiagaraSignificanceHandler* SignificanceHandler = EffectType->GetSignificanceHandler();
	bool bShouldUpdateScalabilityStates = false;
	if (bNewOnly)
	{
		bShouldUpdateScalabilityStates = EffectType->bNewSystemsSinceLastScalabilityUpdate;
	}
	else
	{
		switch (EffectType->UpdateFrequency)
		{
		case ENiagaraScalabilityUpdateFrequency::Continuous: bShouldUpdateScalabilityStates = true; break;
		case ENiagaraScalabilityUpdateFrequency::High: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_High; break;
		case ENiagaraScalabilityUpdateFrequency::Medium: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_Medium; break;
		case ENiagaraScalabilityUpdateFrequency::Low: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_Low; break;
		};
	}

	if (!bShouldUpdateScalabilityStates)
	{
		return;
	}

	LastUpdateTime = WorldTime;
	EffectType->bNewSystemsSinceLastScalabilityUpdate = false;

	//Belt and braces paranoia code to ensure we're safe if a component or System is GCd but the component isn't unregistered for whatever reason.
	int32 CompIdx = 0;
	check(State.Num() == ManagedComponents.Num());
	while (CompIdx < ManagedComponents.Num())
	{
		UNiagaraComponent* Component = ManagedComponents[CompIdx];
		if (Component)
		{
			//Belt and braces GC safety. If someone calls MarkPendingKill() directly and we get here before we clear these out in the post GC callback.
			if (Component->IsPendingKill())
			{
				//UE_LOG(LogNiagara, Warning, TEXT("Unregisteded a pending kill Niagara component from the scalability manager. \nComponent: 0x%P - %s\nEffectType: 0x%P - %s"),
				//	Component, *Component->GetName(), EffectType, *EffectType->GetName());
				Unregister(Component);
				continue;
			}
			if (Component->GetAsset() == nullptr)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Niagara System has been destroyed with components still registered to the scalability manager. Unregistering this component.\nComponent: 0x%P - %s\nEffectType: 0x%P - %s"),
					Component, *Component->GetName(), EffectType, *EffectType->GetName());
				Unregister(Component);
				continue;
			}
		}

		++CompIdx;
	}

	bool bNeedSortedSignificanceCull = false;
	SignificanceSortedIndices.Reset();

	if (SignificanceHandler)
	{
		SignificanceSortedIndices.Reserve(ManagedComponents.Num());
	}

	//TODO parallelize if we exceed GScalabilityManParallelThreshold instances.
	CompIdx = 0;
	bool bAnyDirty = false;
	for (int32 i = 0; i < ManagedComponents.Num(); ++i)
	{
		UNiagaraComponent* Component = ManagedComponents[i];
		
		//The GC can pull this ref from underneath us before the component unregisters itself during BeginDestroy().
		if(!Component)
		{
			continue;
		}

		FNiagaraScalabilityState& CompState = State[i];

		UNiagaraSystem* System = Component->GetAsset();
		System->GetActiveInstancesTempCount() = 0;

		if (System->NeedsSortedSignificanceCull() && SignificanceHandler)
		{
			SignificanceSortedIndices.Add(i);
			bNeedSortedSignificanceCull = true;
		}
		
		//Don't update if we're doing new systems only and this is not new.
		//Saves the potential cost of reavaluating every effect in every tick group something new is added.
		//Though this does mean the sorted significance values will be using out of date distances etc.
		//I'm somewhat on the fence currently as to whether it's better to pay this cost for correctness.
		if(!bNewOnly || Component->GetSystemInstance()->IsPendingSpawn())
		{
			const FNiagaraSystemScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings();

	#if DEBUG_SCALABILITY_STATE
			CompState.bCulledByInstanceCount = false;
			CompState.bCulledByDistance = false;
			CompState.bCulledByVisibility = false;
	#endif
			WorldMan->CalculateScalabilityState(System, ScalabilitySettings, EffectType, Component, false, CompState);

			bAnyDirty |= CompState.bDirty;
		}
	}

	if (bNeedSortedSignificanceCull)
	{
		check(SignificanceHandler);
		SignificanceHandler->CalculateSignificance(ManagedComponents, State);

		SignificanceSortedIndices.Sort([&](int32 A, int32 B) { return State[A].Significance > State[B].Significance; });

		int32 EffectTypeActiveInstances = 0;
		for (int32 i = 0; i < SignificanceSortedIndices.Num(); ++i)
		{
			int32 SortedIdx = SignificanceSortedIndices[i];
			UNiagaraComponent* Component = ManagedComponents[SortedIdx];
			FNiagaraScalabilityState& CompState = State[SortedIdx];
			UNiagaraSystem* System = Component->GetAsset();

			bool bOldCulled = CompState.bCulled;

			const FNiagaraSystemScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings();
			WorldMan->SortedSignificanceCull(EffectType, ScalabilitySettings, CompState.Significance, EffectTypeActiveInstances, System->GetActiveInstancesTempCount(), CompState);

			//Inform the component how significant it is so emitters internally can scale based on that information.
			//e.g. expensive emitters can turn off for all but the N most significant systems.
			int32 SignificanceIndex = CompState.bCulled ? INDEX_NONE : System->GetActiveInstancesTempCount() - 1;
			Component->SetSystemSignificanceIndex(SignificanceIndex);

			CompState.bDirty |= CompState.bCulled != bOldCulled;
			bAnyDirty |= CompState.bDirty;
		}
	}

	if (bAnyDirty)
	{
		CompIdx = 0;
		//As we'll be activating and deactivating here, this must be done on the game thread.
		while (CompIdx < ManagedComponents.Num())
		{
			FNiagaraScalabilityState& CompState = State[CompIdx];
			UNiagaraComponent* Component = ManagedComponents[CompIdx];
			bool bRepeatIndex = false;
			if (Component && CompState.bDirty)
			{
				CompState.bDirty = false;
				if (CompState.bCulled)
				{
					switch (EffectType->CullReaction)
					{
					case ENiagaraCullReaction::Deactivate:					Component->DeactivateInternal(false); bRepeatIndex = true; break;//We don't increment CompIdx here as this call will remove an entry from ManagedObjects;
					case ENiagaraCullReaction::DeactivateImmediate:			Component->DeactivateImmediateInternal(false); bRepeatIndex = true;  break; //We don't increment CompIdx here as this call will remove an entry from ManagedObjects;
					case ENiagaraCullReaction::DeactivateResume:			Component->DeactivateInternal(true); break;
					case ENiagaraCullReaction::DeactivateImmediateResume:	Component->DeactivateImmediateInternal(true); break;
					};
				}
				else
				{
					if (EffectType->CullReaction == ENiagaraCullReaction::Deactivate || EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediate)
					{
						UE_LOG(LogNiagara, Error, TEXT("Niagara Component is incorrectly still registered with the scalability manager. %d - %s "), (int32)EffectType->CullReaction, *Component->GetAsset()->GetFullName());
					}
					Component->ActivateInternal(false, true);
				}

				//TODO: Beyond culling by hard limits here we could progressively scale down fx by biasing detail levels they use. Could also introduce some budgeting here like N at lvl 0, M at lvl 1 etc.
				//TODO: Possibly also limiting the rate at which their instances can tick. Ofc system sims still need to run but instances can skip ticks.
			}

			//If we are making a call that will unregister this component from the manager and remove it from ManagedComponents then we need to visit the new component that is now at this index.
			if (bRepeatIndex == false)
			{
				++CompIdx;
			}
		}
	}
}

#if DEBUG_SCALABILITY_STATE

void FNiagaraScalabilityManager::Dump()
{
	FString DetailString;

	struct FSummary
	{
		FSummary()
			: NumCulled(0)
			, NumCulledByDistance(0)
			, NumCulledByInstanceCount(0)
			, NumCulledByVisibility(0)
		{}

		int32 NumCulled;
		int32 NumCulledByDistance;
		int32 NumCulledByInstanceCount;
		int32 NumCulledByVisibility;
	}Summary;

	for (int32 i = 0; i < ManagedComponents.Num(); ++i)
	{
		UNiagaraComponent* Comp = ManagedComponents[i];
		FNiagaraScalabilityState& CompState = State[i];

		FString CulledStr = TEXT("Active:");
		if (CompState.bCulled)
		{
			CulledStr = TEXT("Culled:");
			++Summary.NumCulled;
		}
		if (CompState.bCulledByDistance)
		{
			CulledStr += TEXT("-Distance-");
			++Summary.NumCulledByDistance;
		}
		if (CompState.bCulledByInstanceCount)
		{
			CulledStr += TEXT("-Inst Count-");
			++Summary.NumCulledByInstanceCount;
		}
		if (CompState.bCulledByVisibility)
		{
			CulledStr += TEXT("-Visibility-");
			++Summary.NumCulledByVisibility;
		}

		DetailString += FString::Printf(TEXT("| %s | Sig: %2.4f | %p | %s | %s |\n"), *CulledStr, CompState.Significance, Comp, *Comp->GetAsset()->GetPathName(), *Comp->GetPathName());
	}

	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("Effect Type: %s"), *EffectType->GetPathName());
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("| Summary for managed systems of this effect type. Does NOT inclued all possible Niagara FX in scene. |"));
	UE_LOG(LogNiagara, Display, TEXT("| Num Managed Components: %d |"), ManagedComponents.Num());
	UE_LOG(LogNiagara, Display, TEXT("| Num Active: %d |"), ManagedComponents.Num() - Summary.NumCulled);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled: %d |"), Summary.NumCulled);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Distance: %d |"), Summary.NumCulledByDistance);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Instance Count: %d |"), Summary.NumCulledByInstanceCount);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Visibility: %d |"), Summary.NumCulledByVisibility);
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame GT: %d |"), EffectType->GetAverageFrameTime_GT());
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame GT + CNC: %d |"), EffectType->GetAverageFrameTime_GT_CNC());
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame RT: %d |"), EffectType->GetAverageFrameTime_RT());
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("| Details |"));
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------\n%s"), *DetailString);
}

#endif
