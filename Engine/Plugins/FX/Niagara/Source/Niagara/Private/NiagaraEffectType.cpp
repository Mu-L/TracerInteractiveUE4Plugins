// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraEffectType.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraSystem.h"

//In an effort to cut the impact of runtime perf tracking, I limit the number of fames we actually sample on.
int32 GNumFramesBetweenRuntimePerfSamples = 5;
static FAutoConsoleVariableRef CVarNumFramesBetweenRuntimePerfSamples(TEXT("fx.NumFramesBetweenRuntimePerfSamples"), GNumFramesBetweenRuntimePerfSamples, TEXT("How many frames between each sample of Niagara runtime perf. \n"), ECVF_ReadOnly);

int32 GNiagaraRuntimeCycleHistorySize = 15;
static FAutoConsoleVariableRef CVarNiagaraRuntimeCycleHistorySize(TEXT("fx.NiagaraRuntimeCycleHistorySize"), GNiagaraRuntimeCycleHistorySize, TEXT("How many frames history to use in Niagara's runtime performance trackers. \n"), ECVF_ReadOnly);

UNiagaraEffectType::UNiagaraEffectType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, UpdateFrequency(ENiagaraScalabilityUpdateFrequency::SpawnOnly)
	, CullReaction(ENiagaraCullReaction::DeactivateImmediate)
	, SignificanceHandler(nullptr)
	, NumInstances(0)
	, bNewSystemsSinceLastScalabilityUpdate(false)
	, AvgTimeMS_GT(0.0f)
	, AvgTimeMS_GT_CNC(0.0f)
	, AvgTimeMS_RT(0.0f)
	, CycleHistory_GT(GNiagaraRuntimeCycleHistorySize)
	, CycleHistory_GT_CNC(GNiagaraRuntimeCycleHistorySize)
	, CycleHistory_RT(GNiagaraRuntimeCycleHistorySize)
	, FramesSincePerfSampled(0)
	, bSampleRunTimePerfThisFrame(false)
{
}

void UNiagaraEffectType::BeginDestroy()
{
	Super::BeginDestroy();

	ReleaseFence.BeginFence();
}

bool UNiagaraEffectType::IsReadyForFinishDestroy()
{
	return ReleaseFence.IsFenceComplete() && Super::IsReadyForFinishDestroy();
}

void UNiagaraEffectType::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
}

void UNiagaraEffectType::PostLoad()
{
	Super::PostLoad();

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

	/** Init signficance handlers to match previous behavior. */
	if (NiagaraVer < FNiagaraCustomVersion::SignificanceHandlers)
	{
		if (UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
		{
			SignificanceHandler = nullptr;
		}
		else
		{
			SignificanceHandler = NewObject<UNiagaraSignificanceHandlerDistance>(this);
		}
	}
}

const FNiagaraSystemScalabilitySettings& UNiagaraEffectType::GetActiveSystemScalabilitySettings()const
{
	for (const FNiagaraSystemScalabilitySettings& Settings : SystemScalabilitySettings.Settings)
	{
		if (Settings.Platforms.IsActive())
		{
			return Settings;
		}
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Could not find active system scalability settings for EffectType %s"), *GetFullName());

	static FNiagaraSystemScalabilitySettings Dummy;
	return Dummy;
}

const FNiagaraEmitterScalabilitySettings& UNiagaraEffectType::GetActiveEmitterScalabilitySettings()const
{
	for (const FNiagaraEmitterScalabilitySettings& Settings : EmitterScalabilitySettings.Settings)
	{
		if (Settings.Platforms.IsActive())
		{
			return Settings;
		}
	}
	
	//UE_LOG(LogNiagara, Warning, TEXT("Could not find active emitter scalability settings for EffectType %s"), *GetFullName());

	static FNiagaraEmitterScalabilitySettings Dummy;
	return Dummy;
}

#if WITH_EDITOR

void UNiagaraEffectType::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FNiagaraSystemUpdateContext UpdateContext;
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* System = *It;
		if (System->GetEffectType() == this)
		{
			System->OnScalabilityCVarChanged();
			UpdateContext.Add(System, true);
		}
	}
}
#endif

void UNiagaraEffectType::ProcessLastFrameCycleCounts()
{
	if (FramesSincePerfSampled > GNumFramesBetweenRuntimePerfSamples)
	{ 
		FramesSincePerfSampled = 0;
		bSampleRunTimePerfThisFrame = true;
	}
	else
	{
		++FramesSincePerfSampled;
		bSampleRunTimePerfThisFrame = false;
	}

	CycleHistory_GT.NextFrame();
	CycleHistory_GT_CNC.NextFrame();
	CycleHistory_RT.NextFrame();

	AvgTimeMS_GT = FPlatformTime::ToMilliseconds(CycleHistory_GT.GetAverageCycles());
	AvgTimeMS_GT_CNC = FPlatformTime::ToMilliseconds(CycleHistory_GT_CNC.GetAverageCycles());
	AvgTimeMS_RT = FPlatformTime::ToMilliseconds(CycleHistory_RT.GetAverageCycles());
}

// void UNiagaraEffectType::ApplyDynamicBudget(float InDynamicBudget_GT, float InDynamicBudget_GT_CNC, float InDynamicBudget_RT)
// {
// 	if (bApplyDynamicBudgetsToSignificance)
// 	{
// 		float MinSigificance_GTPerf = (InDynamicBudget_GT > SMALL_NUMBER) ? (AvgTimeMS_GT / InDynamicBudget_GT) : 1.0f;
// 		float MinSigificance_GTTotalPerf = (InDynamicBudget_GT_CNC > SMALL_NUMBER) ? (AvgTimeMS_GT + AvgTimeMS_GT_CNC) / InDynamicBudget_GT_CNC : 1.0f;
// 		float MinSignificance_RTPerf = (InDynamicBudget_RT > SMALL_NUMBER) ? AvgTimeMS_GT_CNC / InDynamicBudget_RT : 1.0f;
// 
// 		MinSignificanceFromPerf = FMath::Max3(MinSigificance_GTPerf, MinSigificance_GTTotalPerf, MinSignificance_RTPerf);
// 	}
// 	else
// 	{
// 		MinSignificanceFromPerf = 0.0f;
// 	}
// }


//////////////////////////////////////////////////////////////////////////

FNiagaraSystemScalabilityOverride::FNiagaraSystemScalabilityOverride()
	: bOverrideDistanceSettings(false)
	, bOverrideInstanceCountSettings(false)
	, bOverridePerSystemInstanceCountSettings(false)
	, bOverrideTimeSinceRendererSettings(false)
{
}

FNiagaraSystemScalabilitySettings::FNiagaraSystemScalabilitySettings()
{
	Clear();
}

void FNiagaraSystemScalabilitySettings::Clear()
{
	Platforms = FNiagaraPlatformSet();
	bCullByDistance = false;
	bCullByMaxTimeWithoutRender = false;
	bCullMaxInstanceCount = false;
	bCullPerSystemMaxInstanceCount = false;
	MaxDistance = 0.0f;
	MaxInstances = 0;
	MaxSystemInstances = 0;
	MaxTimeWithoutRender = 0.0f;
}

FNiagaraEmitterScalabilitySettings::FNiagaraEmitterScalabilitySettings()
{
	Clear();
}

void FNiagaraEmitterScalabilitySettings::Clear()
{
	SpawnCountScale = 1.0f;
	bScaleSpawnCount = false;
}

FNiagaraEmitterScalabilityOverride::FNiagaraEmitterScalabilityOverride()
	: bOverrideSpawnCountScale(false)
{
}


//////////////////////////////////////////////////////////////////////////

#include "NiagaraScalabilityManager.h"
void UNiagaraSignificanceHandlerDistance::CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState)
{
	check(Components.Num() == OutState.Num());
	for (int32 CompIdx = 0; CompIdx < Components.Num(); ++CompIdx)
	{
		UNiagaraComponent* Component = Components[CompIdx];
		FNiagaraScalabilityState& State = OutState[CompIdx];

		float LODDistance = 0.0f;
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
		if (Component->bEnablePreviewLODDistance)
		{
			LODDistance = Component->PreviewLODDistance;
		}
		else
#endif
		if(FNiagaraSystemInstance* Inst = Component->GetSystemInstance())
		{
			LODDistance = Inst->GetLODDistance();
		}

		State.Significance = 1.0f / LODDistance;
	}
}

void UNiagaraSignificanceHandlerAge::CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState)
{
	for (int32 CompIdx = 0; CompIdx < Components.Num(); ++CompIdx)
	{
		UNiagaraComponent* Component = Components[CompIdx];
		FNiagaraScalabilityState& State = OutState[CompIdx];

		if (FNiagaraSystemInstance* Inst = Component->GetSystemInstance())
		{
			State.Significance = 1.0f / Inst->GetAge();//Newer Systems are higher significance.
		}
	}
}

//////////////////////////////////////////////////////////////////////////