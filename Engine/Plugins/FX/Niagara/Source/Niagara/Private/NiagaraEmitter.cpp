// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraCustomVersion.h"
#include "UObject/Package.h"
#include "UObject/Linker.h"
#include "NiagaraModule.h"
#include "NiagaraSystem.h"
#include "NiagaraStats.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
const FName UNiagaraEmitter::PrivateMemberNames::EventHandlerScriptProps = GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, EventHandlerScriptProps);

const FString InitialNotSynchronizedReason("Emitter created");
#endif

static int32 GbForceNiagaraCompileOnLoad = 0;
static FAutoConsoleVariableRef CVarForceNiagaraCompileOnLoad(
	TEXT("fx.ForceCompileOnLoad"),
	GbForceNiagaraCompileOnLoad,
	TEXT("If > 0 emitters will be forced to compile on load. \n"),
	ECVF_Default
	);

static int32 GbForceNiagaraMergeOnLoad = 0;
static FAutoConsoleVariableRef CVarForceNiagaraMergeOnLoad(
	TEXT("fx.ForceMergeOnLoad"),
	GbForceNiagaraMergeOnLoad,
	TEXT("If > 0 emitters will be forced to merge on load. \n"),
	ECVF_Default
);

static int32 GbForceNiagaraFailToCompile = 0;
static FAutoConsoleVariableRef CVarForceNiagaraCompileToFail(
	TEXT("fx.ForceNiagaraCompileToFail"),
	GbForceNiagaraFailToCompile,
	TEXT("If > 0 emitters will go through the motions of a compile, but will never set valid bytecode. \n"),
	ECVF_Default
);

static int32 GbEnableEmitterChangeIdMergeLogging = 0;
static FAutoConsoleVariableRef CVarEnableEmitterChangeIdMergeLogging(
	TEXT("fx.EnableEmitterMergeChangeIdLogging"),
	GbEnableEmitterChangeIdMergeLogging,
	TEXT("If > 0 verbose change id information will be logged to help with debuggin merge issues. \n"),
	ECVF_Default
);

void FNiagaraEmitterScriptProperties::InitDataSetAccess()
{
	EventReceivers.Empty();
	EventGenerators.Empty();

	if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		//UE_LOG(LogNiagara, Log, TEXT("InitDataSetAccess: %s %d %d"), *Script->GetPathName(), Script->ReadDataSets.Num(), Script->WriteDataSets.Num());
		// TODO: add event receiver and generator lists to the script properties here
		//
		for (FNiagaraDataSetID &ReadID : Script->GetVMExecutableData().ReadDataSets)
		{
			EventReceivers.Add( FNiagaraEventReceiverProperties(ReadID.Name, "", "") );
		}

		for (FNiagaraDataSetProperties &WriteID : Script->GetVMExecutableData().WriteDataSets)
		{
			FNiagaraEventGeneratorProperties Props(WriteID, "");
			EventGenerators.Add(Props);
		}
	}
}

bool FNiagaraEmitterScriptProperties::DataSetAccessSynchronized() const
{
	if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		if (Script->GetVMExecutableData().ReadDataSets.Num() != EventReceivers.Num())
		{
			return false;
		}
		if (Script->GetVMExecutableData().WriteDataSets.Num() != EventGenerators.Num())
		{
			return false;
		}
		return true;
	}
	else
	{
		return EventReceivers.Num() == 0 && EventGenerators.Num() == 0;
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraEmitter::UNiagaraEmitter(const FObjectInitializer& Initializer)
: Super(Initializer)
, FixedBounds(FBox(FVector(-100), FVector(100)))
, MinDetailLevel(0)
, MaxDetailLevel(4)
, bInterpolatedSpawning(false)
, bFixedBounds(false)
, bUseMinDetailLevel(false)
, bUseMaxDetailLevel(false)
, bRequiresPersistentIDs(false)
, MaxDeltaTimePerTick(0.125)
, bLimitDeltaTime(true)
#if WITH_EDITORONLY_DATA
, ThumbnailImageOutOfDate(true)
#endif
{
}

void UNiagaraEmitter::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		SpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "SpawnScript", EObjectFlags::RF_Transactional);
		SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScript);

		UpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "UpdateScript", EObjectFlags::RF_Transactional);
		UpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleUpdateScript);

		EmitterSpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterSpawnScript", EObjectFlags::RF_Transactional);
		EmitterSpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterSpawnScript);
		
		EmitterUpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterUpdateScript", EObjectFlags::RF_Transactional);
		EmitterUpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterUpdateScript);

		GPUComputeScript = NewObject<UNiagaraScript>(this, "GPUComputeScript", EObjectFlags::RF_Transactional);
		GPUComputeScript->SetUsage(ENiagaraScriptUsage::ParticleGPUComputeScript);

	}
	UniqueEmitterName = TEXT("Emitter");

	GenerateStatID();
}

#if WITH_EDITORONLY_DATA
bool UNiagaraEmitter::GetForceCompileOnLoad()
{
	return GbForceNiagaraCompileOnLoad > 0;
}

bool UNiagaraEmitter::IsSynchronizedWithParent() const
{
	if (Parent == nullptr)
	{
		// If the emitter has no parent than it is synchronized by default.
		return true;
	}

	if (ParentAtLastMerge == nullptr)
	{
		// If the parent was valid but the parent at last merge isn't, they we don't know if it's up to date so we say it's not, and let 
		// the actual merge code print an appropriate message to the log.
		return false;
	}

	if (Parent->GetChangeId().IsValid() == false ||
		ParentAtLastMerge->GetChangeId().IsValid() == false)
	{
		// If any of the change Ids aren't valid then we assume we're out of sync.
		return false;
	}

	// Otherwise check the change ids, and the force flag.
	return Parent->GetChangeId() == ParentAtLastMerge->GetChangeId() && GbForceNiagaraMergeOnLoad <= 0;
}

INiagaraMergeManager::FMergeEmitterResults UNiagaraEmitter::MergeChangesFromParent()
{
	if (GbEnableEmitterChangeIdMergeLogging)
	{
		UE_LOG(LogNiagara, Log, TEXT("Emitter %s is merging changes from parent %s because its Change ID was updated."), *GetPathName(),
			Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"));

		UE_LOG(LogNiagara, Log, TEXT("\nEmitter %s Id=%s \nParentAtLastMerge %s id=%s \nParent %s Id=%s."), 
			*GetPathName(), *ChangeId.ToString(),
			ParentAtLastMerge != nullptr ? *ParentAtLastMerge->GetPathName() : TEXT("(null)"), ParentAtLastMerge != nullptr ? *ParentAtLastMerge->GetChangeId().ToString() : TEXT("(null)"),
			Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"), Parent != nullptr ? *Parent->GetChangeId().ToString() : TEXT("(null)"));
	}

	if (Parent == nullptr)
	{
		// If we don't have a copy of the parent emitter, this emitter can't safely be merged.
		INiagaraMergeManager::FMergeEmitterResults MergeResults;
		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToDiff;
		MergeResults.bModifiedGraph = false;
		MergeResults.ErrorMessages.Add(NSLOCTEXT("NiagaraEmitter", "NoParentErrorMessage", "This emitter has no 'Parent' so changes can't be merged in."));
		return MergeResults;
	}

	if (ParentAtLastMerge == nullptr)
	{
		// If we don't have a copy of the last merged parent emitter, this emitter can't safely be
		// merged.
		INiagaraMergeManager::FMergeEmitterResults MergeResults;
		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToDiff;
		MergeResults.bModifiedGraph = false;
		MergeResults.ErrorMessages.Add(NSLOCTEXT("NiagaraEmitter", "NoLastMergedParentErrorMessage", "This emitter has no 'ParentAtLastMerge' so changes can't be merged in."));
		return MergeResults;
	}

	INiagaraModule& NiagaraModule = FModuleManager::Get().GetModuleChecked<INiagaraModule>("Niagara");
	const INiagaraMergeManager& MergeManager = NiagaraModule.GetMergeManager();
	INiagaraMergeManager::FMergeEmitterResults MergeResults = MergeManager.MergeEmitter(*Parent, *ParentAtLastMerge, *this);
	if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied || MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededNoDifferences)
	{
		if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied)
		{
			UpdateFromMergedCopy(MergeManager, MergeResults.MergedInstance);
		}

		// Update the last merged source and clear it's stand alone and public flags since it's not an asset.
		ParentAtLastMerge = Parent->DuplicateWithoutMerging(this);
		ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	}
	else
	{
		UE_LOG(LogNiagara, Warning, TEXT("Failed to merge changes for parent emitter.  Emitter: %s  Parent Emitter: %s  Error Message: %s"),
			*GetPathName(), Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"), *MergeResults.GetErrorMessagesString());
	}

	return MergeResults;
}

bool UNiagaraEmitter::UsesEmitter(const UNiagaraEmitter& InEmitter) const
{
	return Parent == &InEmitter || (Parent != nullptr && Parent->UsesEmitter(InEmitter));
}

UNiagaraEmitter* UNiagaraEmitter::DuplicateWithoutMerging(UObject* InOuter)
{
	UNiagaraEmitter* Duplicate;
	{
		TGuardValue<UNiagaraEmitter*> ParentGuard(Parent, nullptr);
		TGuardValue<UNiagaraEmitter*> ParentAtLastMergeGuard(ParentAtLastMerge, nullptr);
		Duplicate = Cast<UNiagaraEmitter>(StaticDuplicateObject(this, InOuter));
	}
	return Duplicate;
}

#endif

void UNiagaraEmitter::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
}

void UNiagaraEmitter::PostLoad()
{
	Super::PostLoad();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}

	if (!GPUComputeScript)
	{
		GPUComputeScript = NewObject<UNiagaraScript>(this, "GPUComputeScript", EObjectFlags::RF_Transactional);
		GPUComputeScript->SetUsage(ENiagaraScriptUsage::ParticleGPUComputeScript);
#if WITH_EDITORONLY_DATA
		GPUComputeScript->SetSource(SpawnScriptProps.Script ? SpawnScriptProps.Script->GetSource() : nullptr);
#endif
	}

	if (EmitterSpawnScriptProps.Script == nullptr || EmitterUpdateScriptProps.Script == nullptr)
	{
		EmitterSpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterSpawnScript", EObjectFlags::RF_Transactional);
		EmitterSpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterSpawnScript);

		EmitterUpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterUpdateScript", EObjectFlags::RF_Transactional);
		EmitterUpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterUpdateScript);

#if WITH_EDITORONLY_DATA
		if (SpawnScriptProps.Script)
		{
			EmitterSpawnScriptProps.Script->SetSource(SpawnScriptProps.Script->GetSource());
			EmitterUpdateScriptProps.Script->SetSource(SpawnScriptProps.Script->GetSource());
		}
#endif
	}

	//Temporarily disabling interpolated spawn if the script type and flag don't match.
	if (SpawnScriptProps.Script)
	{
		SpawnScriptProps.Script->ConditionalPostLoad();
		bool bActualInterpolatedSpawning = SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript();
		if (bInterpolatedSpawning != bActualInterpolatedSpawning)
		{
			bInterpolatedSpawning = false;
			if (bActualInterpolatedSpawning)
			{
#if WITH_EDITORONLY_DATA
				SpawnScriptProps.Script->InvalidateCompileResults();//clear out the script as it was compiled with interpolated spawn.
#endif
				SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScript);
			}
			UE_LOG(LogNiagara, Warning, TEXT("Disabling interpolated spawn because emitter flag and script type don't match. Did you adjust this value in the UI? Emitter may need recompile.. %s"), *GetFullName());
		}
	}

#if WITH_EDITORONLY_DATA
	if (GetOuter()->IsA<UNiagaraEmitter>())
	{
		// If this emitter is owned by another emitter, remove it's inheritance information so that it doesn't try to merge changes.
		Parent = nullptr;
		ParentAtLastMerge = nullptr;
	}

	GraphSource->ConditionalPostLoad();
	GraphSource->PostLoadFromEmitter(*this);
#endif

	TArray<UNiagaraScript*> AllScripts;
	GetScripts(AllScripts, false);

	// Post load scripts for use below.
	for (UNiagaraScript* Script : AllScripts)
	{
		Script->ConditionalPostLoad();
	}

#if WITH_EDITORONLY_DATA

	// Handle emitter inheritance.
	if (Parent != nullptr)
	{
		Parent->ConditionalPostLoad();
	}
	if (ParentAtLastMerge != nullptr)
	{
		ParentAtLastMerge->ConditionalPostLoad();
	}
	if (IsSynchronizedWithParent() == false)
	{
		MergeChangesFromParent();
	}

	// Reset scripts if recompile is forced.
	bool bGenerateNewChangeId = false;
	FString GenerateNewChangeIdReason;
	if (GetForceCompileOnLoad())
	{
		// If we are a standalone emitter, then we invalidate id's, which should cause systems dependent on us to regenerate.
		UObject* OuterObj = GetOuter();
		if (OuterObj == GetOutermost())
		{
			GraphSource->InvalidateCachedCompileIds();
			bGenerateNewChangeId = true;
			GenerateNewChangeIdReason = TEXT("PostLoad - Force compile on load");
			if (GEnableVerboseNiagaraChangeIdLogging)
			{
				UE_LOG(LogNiagara, Log, TEXT("InvalidateCachedCompileIds for %s because GbForceNiagaraCompileOnLoad = %d"), *GetPathName(), GbForceNiagaraCompileOnLoad);
			}
		}
	}
	
	if (ChangeId.IsValid() == false)
	{
		// If the change id is already invalid we need to generate a new one, and can skip checking the owned scripts.
		bGenerateNewChangeId = true;
		GenerateNewChangeIdReason = TEXT("PostLoad - Change id was invalid.");
		if (GEnableVerboseNiagaraChangeIdLogging)
		{
			UE_LOG(LogNiagara, Log, TEXT("Change ID updated for emitter %s because the ID was invalid."), *GetPathName());
		}
	}
	else
	{
		for (UNiagaraScript* Script : AllScripts)
		{
			if (Script->AreScriptAndSourceSynchronized() == false)
			{
				bGenerateNewChangeId = true;
				GenerateNewChangeIdReason = TEXT("PostLoad - Script out of sync");
				if (GEnableVerboseNiagaraChangeIdLogging)
				{
					UE_LOG(LogNiagara, Log, TEXT("Change ID updated for emitter %s because of a change to its script %s"), *GetPathName(), *Script->GetPathName());
				}
			}
		}
	}

	if (bGenerateNewChangeId)
	{
		UpdateChangeId(GenerateNewChangeIdReason);
	}

	GraphSource->OnChanged().AddUObject(this, &UNiagaraEmitter::GraphSourceChanged);

	EmitterSpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	EmitterUpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	if (SpawnScriptProps.Script)
	{
		SpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}
	
	if (UpdateScriptProps.Script)
	{
		UpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		EventScriptProperties.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		Renderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
	}
#endif
}

#if WITH_EDITOR
/** Creates a new emitter with the supplied emitter as a parent emitter and the supplied system as it's owner. */
UNiagaraEmitter* UNiagaraEmitter::CreateWithParentAndOwner(UNiagaraEmitter& InParentEmitter, UObject* InOwner, FName InName, EObjectFlags FlagMask)
{
	UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InParentEmitter, InOwner, InName, FlagMask));
	NewEmitter->Parent = &InParentEmitter;
	NewEmitter->ParentAtLastMerge = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InParentEmitter, NewEmitter));
	NewEmitter->ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	NewEmitter->SetUniqueEmitterName(InName.ToString());
	NewEmitter->GraphSource->MarkNotSynchronized(InitialNotSynchronizedReason);
	return NewEmitter;
}

/** Creates a new emitter by duplicating an existing emitter.  The new emitter  will reference the same parent emitter if one is available. */
UNiagaraEmitter* UNiagaraEmitter::CreateAsDuplicate(const UNiagaraEmitter& InEmitterToDuplicate, FName InDuplicateName, UNiagaraSystem& InDuplicateOwnerSystem)
{
	UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InEmitterToDuplicate, &InDuplicateOwnerSystem));
	NewEmitter->ClearFlags(RF_Standalone | RF_Public);
	NewEmitter->Parent = InEmitterToDuplicate.Parent;
	if (InEmitterToDuplicate.ParentAtLastMerge != nullptr)
	{
		NewEmitter->ParentAtLastMerge = Cast<UNiagaraEmitter>(StaticDuplicateObject(InEmitterToDuplicate.ParentAtLastMerge, NewEmitter));
		NewEmitter->ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	}
	NewEmitter->SetUniqueEmitterName(InDuplicateName.ToString());
	NewEmitter->GraphSource->MarkNotSynchronized(InitialNotSynchronizedReason);

	return NewEmitter;
}

void UNiagaraEmitter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bInterpolatedSpawning))
	{
		bool bActualInterpolatedSpawning = SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript();
		if (bInterpolatedSpawning != bActualInterpolatedSpawning)
		{
			//Recompile spawn script if we've altered the interpolated spawn property.
			SpawnScriptProps.Script->SetUsage(bInterpolatedSpawning ? ENiagaraScriptUsage::ParticleSpawnScriptInterpolated : ENiagaraScriptUsage::ParticleSpawnScript);
			UE_LOG(LogNiagara, Log, TEXT("Updating script usage: Script->IsInterpolatdSpawn %d Emitter->bInterpolatedSpawning %d"), (int32)SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript(), bInterpolatedSpawning);
			if (GraphSource != nullptr)
			{
				GraphSource->MarkNotSynchronized(TEXT("Emitter interpolated spawn changed"));
			}
#if WITH_EDITORONLY_DATA
			UNiagaraSystem::RequestCompileForEmitter(this);
#endif
		}
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, SimTarget))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter simulation target changed."));
		}

#if WITH_EDITORONLY_DATA
		UNiagaraSystem::RequestCompileForEmitter(this);
#endif
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bRequiresPersistentIDs))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter Requires Persistent IDs changed."));
		}

#if WITH_EDITORONLY_DATA
		UNiagaraSystem::RequestCompileForEmitter(this);
#endif
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bLocalSpace))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter LocalSpace changed."));
		}

#if WITH_EDITORONLY_DATA
		UNiagaraSystem::RequestCompileForEmitter(this);
#endif
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bDeterminism))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter Determinism changed."));
		}

#if WITH_EDITORONLY_DATA
		UNiagaraSystem::RequestCompileForEmitter(this);
#endif
	}

	ThumbnailImageOutOfDate = true;
	UpdateChangeId(TEXT("PostEditChangeProperty"));
	OnPropertiesChangedDelegate.Broadcast();
}


UNiagaraEmitter::FOnPropertiesChanged& UNiagaraEmitter::OnPropertiesChanged()
{
	return OnPropertiesChangedDelegate;
}
#endif


bool UNiagaraEmitter::IsValid()const
{
	if (!SpawnScriptProps.Script || !UpdateScriptProps.Script)
	{
		return false;
	}

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (!SpawnScriptProps.Script->IsScriptCompilationPending(false) && !SpawnScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			return false;
		}
		if (!UpdateScriptProps.Script->IsScriptCompilationPending(false) && !UpdateScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			return false;
		}
		if (EventHandlerScriptProps.Num() != 0)
		{
			for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
			{
				if (!EventHandlerScriptProps[i].Script->IsScriptCompilationPending(false) &&
					!EventHandlerScriptProps[i].Script->DidScriptCompilationSucceed(false))
				{
					return false;
				}
			}
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (!GPUComputeScript->IsScriptCompilationPending(true) && 
			!GPUComputeScript->DidScriptCompilationSucceed(true))
		{
			return false;
		}
	}
	return true;
}

bool UNiagaraEmitter::IsReadyToRun() const
{
	//Check for various failure conditions and bail.
	if (!UpdateScriptProps.Script || !SpawnScriptProps.Script)
	{
		return false;
	}

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (SpawnScriptProps.Script->IsScriptCompilationPending(false))
		{
			return false;
		}
		if (UpdateScriptProps.Script->IsScriptCompilationPending(false))
		{
			return false;
		}
		if (EventHandlerScriptProps.Num() != 0)
		{
			for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
			{
				if (EventHandlerScriptProps[i].Script->IsScriptCompilationPending(false))
				{
					return false;
				}
			}
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (GPUComputeScript->IsScriptCompilationPending(true))
		{
			return false;
		}
	}

	return true;
}

void UNiagaraEmitter::GetScripts(TArray<UNiagaraScript*>& OutScripts, bool bCompilableOnly) const
{
	OutScripts.Add(SpawnScriptProps.Script);
	OutScripts.Add(UpdateScriptProps.Script);
	if (!bCompilableOnly)
	{
		OutScripts.Add(EmitterSpawnScriptProps.Script);
		OutScripts.Add(EmitterUpdateScriptProps.Script);
	}

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script)
		{
			OutScripts.Add(EventHandlerScriptProps[i].Script);
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		OutScripts.Add(GPUComputeScript);
	}
}

UNiagaraScript* UNiagaraEmitter::GetScript(ENiagaraScriptUsage Usage, FGuid UsageId)
{
	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false);
	for (UNiagaraScript* Script : Scripts)
	{
		if (Script->IsEquivalentUsage(Usage) && Script->GetUsageId() == UsageId)
		{
			return Script;
		}
	}
	return nullptr;
}

bool UNiagaraEmitter::IsAllowedByDetailLevel(int32 DetailLevel)const
{
	if ((bUseMinDetailLevel && DetailLevel < MinDetailLevel) || (bUseMaxDetailLevel && DetailLevel > MaxDetailLevel))
	{
		return false;
	}

	return true;
}

bool UNiagaraEmitter::RequiresPersistantIDs()const
{
	return bRequiresPersistentIDs;
}

#if WITH_EDITORONLY_DATA

FGuid UNiagaraEmitter::GetChangeId() const
{
	return ChangeId;
}

bool UNiagaraEmitter::AreAllScriptAndSourcesSynchronized() const
{
	if (SpawnScriptProps.Script->IsCompilable() && !SpawnScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (UpdateScriptProps.Script->IsCompilable() && !UpdateScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (EmitterSpawnScriptProps.Script->IsCompilable() && !EmitterSpawnScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (EmitterUpdateScriptProps.Script->IsCompilable() && !EmitterUpdateScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script && EventHandlerScriptProps[i].Script->IsCompilable() && !EventHandlerScriptProps[i].Script->AreScriptAndSourceSynchronized())
		{
			return false;
		}
	}

	if (GPUComputeScript->IsCompilable() && !GPUComputeScript->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	return true;
}


UNiagaraEmitter::FOnEmitterCompiled& UNiagaraEmitter::OnEmitterVMCompiled()
{
	return OnVMScriptCompiledDelegate;
}

void UNiagaraEmitter::OnPostCompile()
{
	SyncEmitterAlias(TEXT("Emitter"), UniqueEmitterName);

	SpawnScriptProps.InitDataSetAccess();
	UpdateScriptProps.InitDataSetAccess();

	TSet<FName> SpawnIds;
	TSet<FName> UpdateIds;
	for (const FNiagaraEventGeneratorProperties& SpawnGeneratorProps : SpawnScriptProps.EventGenerators)
	{
		SpawnIds.Add(SpawnGeneratorProps.ID);
	}
	for (const FNiagaraEventGeneratorProperties& UpdateGeneratorProps : UpdateScriptProps.EventGenerators)
	{
		UpdateIds.Add(UpdateGeneratorProps.ID);
	}

	SharedEventGeneratorIds.Empty();
	SharedEventGeneratorIds.Append(SpawnIds.Intersect(UpdateIds).Array());

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script)
		{
			EventHandlerScriptProps[i].InitDataSetAccess();
		}
	}

	if (GbForceNiagaraFailToCompile != 0)
	{
		TArray<UNiagaraScript*> Scripts;
		GetScripts(Scripts, false);
		for (int32 i = 0; i < Scripts.Num(); i++)
		{
			Scripts[i]->InvalidateCompileResults(); 
		}
	}

	OnEmitterVMCompiled().Broadcast(this);
}

UNiagaraEmitter* UNiagaraEmitter::MakeRecursiveDeepCopy(UObject* DestOuter) const
{
	TMap<const UObject*, UObject*> ExistingConversions;
	return MakeRecursiveDeepCopy(DestOuter, ExistingConversions);
}

UNiagaraEmitter* UNiagaraEmitter::MakeRecursiveDeepCopy(UObject* DestOuter, TMap<const UObject*, UObject*>& ExistingConversions) const
{
	ResetLoaders(GetTransientPackage());
	GetTransientPackage()->LinkerCustomVersion.Empty();

	EObjectFlags Flags = RF_AllFlags & ~RF_Standalone & ~RF_Public; // Remove Standalone and Public flags..
	UNiagaraEmitter* Props = CastChecked<UNiagaraEmitter>(StaticDuplicateObject(this, GetTransientPackage(), *GetName(), Flags));
	check(Props->HasAnyFlags(RF_Standalone) == false);
	check(Props->HasAnyFlags(RF_Public) == false);
	Props->Rename(nullptr, DestOuter, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	UE_LOG(LogNiagara, Warning, TEXT("MakeRecursiveDeepCopy %s"), *Props->GetFullName());
	ExistingConversions.Add(this, Props);

	check(GraphSource != Props->GraphSource);

	Props->GraphSource->SubsumeExternalDependencies(ExistingConversions);
	ExistingConversions.Add(GraphSource, Props->GraphSource);

	// Suck in the referenced scripts into this package.
	if (Props->SpawnScriptProps.Script)
	{
		Props->SpawnScriptProps.Script->SubsumeExternalDependencies(ExistingConversions);
		check(Props->GraphSource == Props->SpawnScriptProps.Script->GetSource());
	}

	if (Props->UpdateScriptProps.Script)
	{
		Props->UpdateScriptProps.Script->SubsumeExternalDependencies(ExistingConversions);
		check(Props->GraphSource == Props->UpdateScriptProps.Script->GetSource());
	}

	if (Props->EmitterSpawnScriptProps.Script)
	{
		Props->EmitterSpawnScriptProps.Script->SubsumeExternalDependencies(ExistingConversions);
		check(Props->GraphSource == Props->EmitterSpawnScriptProps.Script->GetSource());
	}
	if (Props->EmitterUpdateScriptProps.Script)
	{
		Props->EmitterUpdateScriptProps.Script->SubsumeExternalDependencies(ExistingConversions);
		check(Props->GraphSource == Props->EmitterUpdateScriptProps.Script->GetSource());
	}


	for (int32 i = 0; i < Props->GetEventHandlers().Num(); i++)
	{
		if (Props->GetEventHandlers()[i].Script)
		{
			Props->GetEventHandlers()[i].Script->SubsumeExternalDependencies(ExistingConversions);
			check(Props->GraphSource == Props->GetEventHandlers()[i].Script->GetSource());
		}
	}
	return Props;
}
#endif

bool UNiagaraEmitter::UsesScript(const UNiagaraScript* Script)const
{
	if (SpawnScriptProps.Script == Script || UpdateScriptProps.Script == Script || EmitterSpawnScriptProps.Script == Script || EmitterUpdateScriptProps.Script == Script)
	{
		return true;
	}
	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script == Script)
		{
			return true;
		}
	}
	return false;
}

bool UNiagaraEmitter::UsesCollection(const class UNiagaraParameterCollection* Collection)const
{
	if (SpawnScriptProps.Script && SpawnScriptProps.Script->UsesCollection(Collection))
	{
		return true;
	}
	if (UpdateScriptProps.Script && UpdateScriptProps.Script->UsesCollection(Collection))
	{
		return true;
	}
	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script && EventHandlerScriptProps[i].Script->UsesCollection(Collection))
		{
			return true;
		}
	}
	return false;
}

FString UNiagaraEmitter::GetUniqueEmitterName()const
{
	return UniqueEmitterName;
}

#if WITH_EDITORONLY_DATA

void UNiagaraEmitter::UpdateFromMergedCopy(const INiagaraMergeManager& MergeManager, UNiagaraEmitter* MergedEmitter)
{
	auto ReouterMergedObject = [](UObject* NewOuter, UObject* TargetObject)
	{
		FName MergedObjectUniqueName = MakeUniqueObjectName(NewOuter, TargetObject->GetClass(), TargetObject->GetFName());
		TargetObject->Rename(*MergedObjectUniqueName.ToString(), NewOuter, REN_ForceNoResetLoaders);
	};

	// The merged copy was based on the parent emitter so its name might be wrong, check and fix that first,
	// otherwise the rapid iteration parameter names will be wrong from the copied scripts.
	if (MergedEmitter->GetUniqueEmitterName() != UniqueEmitterName)
	{
		MergedEmitter->SetUniqueEmitterName(UniqueEmitterName);
	}

	// Copy base editable emitter properties.
	TArray<UProperty*> DifferentProperties;
	MergeManager.DiffEditableProperties(this, MergedEmitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	MergeManager.CopyPropertiesToBase(this, MergedEmitter, DifferentProperties);

	// Copy source and scripts
	ReouterMergedObject(this, MergedEmitter->GraphSource);
	GraphSource->OnChanged().RemoveAll(this);
	GraphSource = MergedEmitter->GraphSource;
	GraphSource->OnChanged().AddUObject(this, &UNiagaraEmitter::GraphSourceChanged);

	ReouterMergedObject(this, MergedEmitter->SpawnScriptProps.Script);
	SpawnScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	SpawnScriptProps.Script = MergedEmitter->SpawnScriptProps.Script;
	SpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->UpdateScriptProps.Script);
	UpdateScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	UpdateScriptProps.Script = MergedEmitter->UpdateScriptProps.Script;
	UpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->EmitterSpawnScriptProps.Script);
	EmitterSpawnScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	EmitterSpawnScriptProps.Script = MergedEmitter->EmitterSpawnScriptProps.Script;
	EmitterSpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->EmitterUpdateScriptProps.Script);
	EmitterUpdateScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	EmitterUpdateScriptProps.Script = MergedEmitter->EmitterUpdateScriptProps.Script;
	EmitterUpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->GPUComputeScript);
	GPUComputeScript->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	GPUComputeScript = MergedEmitter->GPUComputeScript;
	GPUComputeScript->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	// Copy event handlers
	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		EventScriptProperties.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	}
	EventHandlerScriptProps.Empty();

	for (FNiagaraEventScriptProperties& MergedEventScriptProperties : MergedEmitter->EventHandlerScriptProps)
	{
		EventHandlerScriptProps.Add(MergedEventScriptProperties);
		ReouterMergedObject(this, MergedEventScriptProperties.Script);
		MergedEventScriptProperties.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	// Copy renderers
	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		Renderer->OnChanged().RemoveAll(this);
	}
	RendererProperties.Empty();

	for (UNiagaraRendererProperties* MergedRenderer : MergedEmitter->RendererProperties)
	{
		ReouterMergedObject(this, MergedRenderer);
		RendererProperties.Add(MergedRenderer);
		MergedRenderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
	}

	// Update the change id since we don't know what's changed.
	UpdateChangeId(TEXT("Updated from merged copy"));
}

void UNiagaraEmitter::SyncEmitterAlias(const FString& InOldName, const FString& InNewName)
{
	TMap<FString, FString> RenameMap;
	RenameMap.Add(InOldName, InNewName);

	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false); // Get all the scripts...

	for (UNiagaraScript* Script : Scripts)
	{
		// We don't mark the package dirty here because this can happen as a result of a compile and we don't want to dirty files
		// due to compilation, in cases where the package should be marked dirty an previous modify would have already done this.
		Script->Modify(false);
		Script->SyncAliases(RenameMap);
	}
}
#endif
bool UNiagaraEmitter::SetUniqueEmitterName(const FString& InName)
{
	if (InName != UniqueEmitterName)
	{
		Modify();
		FString OldName = UniqueEmitterName;
		UniqueEmitterName = InName;

		if (GetName() != InName)
		{
			// Also rename the underlying uobject to keep things consistent.
			FName UniqueObjectName = MakeUniqueObjectName(GetOuter(), UNiagaraEmitter::StaticClass(), *InName);
			Rename(*UniqueObjectName.ToString(), GetOuter(), REN_ForceNoResetLoaders);
		}

#if WITH_EDITORONLY_DATA
		SyncEmitterAlias(OldName, UniqueEmitterName);
#endif
		return true;
	}

	return false;
}


FNiagaraVariable UNiagaraEmitter::ToEmitterParameter(const FNiagaraVariable& EmitterVar)const
{
	FNiagaraVariable Var = EmitterVar;
	Var.SetName(*Var.GetName().ToString().Replace(TEXT("Emitter."), *(GetUniqueEmitterName() + TEXT("."))));
	return Var;
}

void UNiagaraEmitter::AddRenderer(UNiagaraRendererProperties* Renderer)
{
	Modify();
	RendererProperties.Add(Renderer);
#if WITH_EDITOR
	Renderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
	UpdateChangeId(TEXT("Renderer added"));
#endif
}

void UNiagaraEmitter::RemoveRenderer(UNiagaraRendererProperties* Renderer)
{
	Modify();
	RendererProperties.Remove(Renderer);
#if WITH_EDITOR
	Renderer->OnChanged().RemoveAll(this);
	UpdateChangeId(TEXT("Renderer removed"));
#endif
}

FNiagaraEventScriptProperties* UNiagaraEmitter::GetEventHandlerByIdUnsafe(FGuid ScriptUsageId)
{
	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		if (EventScriptProperties.Script->GetUsageId() == ScriptUsageId)
		{
			return &EventScriptProperties;
		}
	}
	return nullptr;
}

void UNiagaraEmitter::AddEventHandler(FNiagaraEventScriptProperties EventHandler)
{
	Modify();
	EventHandlerScriptProps.Add(EventHandler);
#if WITH_EDITOR
	EventHandler.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	UpdateChangeId(TEXT("Event handler added"));
#endif
}

void UNiagaraEmitter::RemoveEventHandlerByUsageId(FGuid EventHandlerUsageId)
{
	Modify();
	auto FindEventHandlerById = [=](const FNiagaraEventScriptProperties& EventHandler) { return EventHandler.Script->GetUsageId() == EventHandlerUsageId; };
#if WITH_EDITOR
	FNiagaraEventScriptProperties* EventHandler = EventHandlerScriptProps.FindByPredicate(FindEventHandlerById);
	if (EventHandler != nullptr)
	{
		EventHandler->Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	}
#endif
	EventHandlerScriptProps.RemoveAll(FindEventHandlerById);
#if WITH_EDITOR
	UpdateChangeId(TEXT("Event handler removed"));
#endif
}

bool UNiagaraEmitter::IsEventGeneratorShared(FName EventGeneratorId) const
{
	return SharedEventGeneratorIds.Contains(EventGeneratorId);
}

void UNiagaraEmitter::BeginDestroy()
{
#if WITH_EDITOR
	if (GraphSource != nullptr)
	{
		GraphSource->OnChanged().RemoveAll(this);
	}
#endif
	Super::BeginDestroy();
}

#if WITH_EDITORONLY_DATA

void UNiagaraEmitter::UpdateChangeId(const FString& Reason)
{
	// We don't mark the package dirty here because this can happen as a result of a compile and we don't want to dirty files
	// due to compilation, in cases where the package should be marked dirty an previous modify would have already done this.
	Modify(false);
	FGuid OldId = ChangeId;
	ChangeId = FGuid::NewGuid();
	if (GbEnableEmitterChangeIdMergeLogging)
	{
		UE_LOG(LogNiagara, Log, TEXT("Emitter %s change id updated. Reason: %s OldId: %s NewId: %s"),
			*GetPathName(), *Reason, *OldId.ToString(), *ChangeId.ToString());
	}
}

void UNiagaraEmitter::ScriptRapidIterationParameterChanged()
{
	UpdateChangeId(TEXT("Script rapid iteration parameter changed."));
}

void UNiagaraEmitter::RendererChanged()
{
	UpdateChangeId(TEXT("Renderer changed."));
}

void UNiagaraEmitter::GraphSourceChanged()
{
	UpdateChangeId(TEXT("Graph source changed."));
}
#endif

TStatId UNiagaraEmitter::GetStatID(bool bGameThread, bool bConcurrent)const
{
#if STATS
	if (bGameThread)
	{
		if (bConcurrent)
		{
			return StatID_GT;
		}
		else
		{
			return StatID_GT_CNC;
		}
	}
	else
	{
		if (bConcurrent)
		{
			return StatID_RT;
		}
		else
		{
			return StatID_RT_CNC;
		}
	}
#endif
	return TStatId();
}
void UNiagaraEmitter::GenerateStatID()
{
#if STATS
	StatID_GT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(GetName() + TEXT("[GT]"));
	StatID_GT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(GetName() + TEXT("[GT_CNC]"));
	StatID_RT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(GetName() + TEXT("[RT]"));
	StatID_RT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(GetName() + TEXT("[RT_CNC]"));
#endif
}

UNiagaraEmitter* UNiagaraEmitter::GetParent() const
{
	return Parent;
}

void UNiagaraEmitter::RemoveParent()
{
	Parent = nullptr;
	ParentAtLastMerge = nullptr;
}
