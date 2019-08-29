// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "QosRegionManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "QosInterface.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "QosEvaluator.h"
#include "QosModule.h"

FOnlineSessionSettingsQos::FOnlineSessionSettingsQos(bool bInIsDedicated)
{
	NumPublicConnections = 1;
	NumPrivateConnections = 0;

	bIsLANMatch = false;
	bShouldAdvertise = true;
	bAllowJoinInProgress = true;
	bAllowInvites = true;
	bUsesPresence = false;
	bAllowJoinViaPresence = true;
	bAllowJoinViaPresenceFriendsOnly = false;

	FString GameModeStr(GAMEMODE_QOS);
	Set(SETTING_GAMEMODE, GameModeStr, EOnlineDataAdvertisementType::ViaOnlineService);
	Set(SETTING_QOS, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	Set(SETTING_REGION, FQosInterface::Get()->GetRegionId(), EOnlineDataAdvertisementType::ViaOnlineService);
	bIsDedicated = bInIsDedicated;
}

UQosRegionManager::UQosRegionManager(const FObjectInitializer& ObjectInitializer)
	: bUseOldQosServers(false)
	, NumTestsPerRegion(3)
	, PingTimeout(5.0f)
	, LastCheckTimestamp(0)
	, Evaluator(nullptr)
	, QosEvalResult(EQosCompletionResult::Invalid)
{
	check(GConfig);
	GConfig->GetString(TEXT("Qos"), TEXT("ForceRegionId"), ForceRegionId, GGameIni);

	// get a forced region id from the command line as an override
	bRegionForcedViaCommandline = FParse::Value(FCommandLine::Get(), TEXT("McpRegion="), ForceRegionId);
	if (!ForceRegionId.IsEmpty())
	{
		ForceRegionId.ToUpperInline();
	}
}

void UQosRegionManager::PostReloadConfig(UProperty* PropertyThatWasLoaded)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		for (int32 RegionIdx = RegionOptions.Num() - 1; RegionIdx >= 0; RegionIdx--)
		{
			FQosRegionInfo& Region = RegionOptions[RegionIdx];

			bool bFound = false;
			for (FQosDatacenterInfo& Datacenter : Datacenters)
			{
				if (Datacenter.RegionId == Region.Region.RegionId)
				{
					bFound = true;
				}
			}

			if (!bFound)
			{
				// Old value needs to be removed, preserve order
				RegionOptions.RemoveAt(RegionIdx);
			}
		}

		for (int32 MetaIdx = 0; MetaIdx < Datacenters.Num(); MetaIdx++)
		{
			FQosDatacenterInfo& Datacenter = Datacenters[MetaIdx];

			bool bFound = false;
			for (FQosRegionInfo& Region : RegionOptions)
			{
				if (Datacenter.RegionId == Region.Region.RegionId)
				{
					// Overwrite the metadata
					Region.Region = Datacenter;
					bFound = true;
				}
			}

			if (!bFound)
			{
				// Add new value not in old list
				FQosRegionInfo NewRegion(Datacenter);
				RegionOptions.Insert(NewRegion, MetaIdx);
			}
		}

		OnQoSSettingsChangedDelegate.ExecuteIfBound();

		// Validate the current region selection
		TrySetDefaultRegion();
	}
}

int32 UQosRegionManager::GetMaxPingMs() const
{
	int32 MaxPing = -1;
	if (GConfig->GetInt(TEXT("Qos"), TEXT("MaximumPingMs"), MaxPing, GGameIni) && MaxPing > 0)
	{
		return MaxPing;
	}
	return -1;
}

// static
FString UQosRegionManager::GetDatacenterId()
{
	struct FDcidInfo
	{
		FDcidInfo()
		{
			FString OverrideDCID;
			if (FParse::Value(FCommandLine::Get(), TEXT("DCID="), OverrideDCID))
			{
				// DCID specified on command line
				DCIDString = OverrideDCID.ToUpper();
			}
			else
			{
				FString DefaultDCID;
				check(GConfig);
				if (GConfig->GetString(TEXT("Qos"), TEXT("DCID"), DefaultDCID, GGameIni))
				{
					// DCID specified in ini file
					DCIDString = DefaultDCID.ToUpper();
				}
			}
		}

		FString DCIDString;
	};
	static FDcidInfo DCID;
	return DCID.DCIDString;
}

void UQosRegionManager::BeginQosEvaluation(UWorld* World, const TSharedPtr<IAnalyticsProvider>& AnalyticsProvider, const FSimpleDelegate& OnComplete)
{
	check(World);

	// There are valid cached results, use them
	if ((RegionOptions.Num() > 0) &&
		(QosEvalResult == EQosCompletionResult::Success) &&
		(FDateTime::UtcNow() - LastCheckTimestamp).GetTotalSeconds() <= 3)
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([OnComplete]()
		{
			OnComplete.ExecuteIfBound();
		}));
		return;
	}

	// add to the completion delegate
	OnQosEvalCompleteDelegate.Add(OnComplete);

	// if we're already evaluating, simply return
	if (Evaluator != nullptr)
	{
		return;
	}

	// create a new evaluator and start the process of running
	Evaluator = NewObject<UQosEvaluator>();
	Evaluator->AddToRoot();
	Evaluator->SetWorld(World);
	Evaluator->SetAnalyticsProvider(AnalyticsProvider);

	FQosParams Params;
	Params.ControllerId = 0;
	Params.bUseOldQosServers = bUseOldQosServers;
	Params.NumTestsPerRegion = NumTestsPerRegion;
	Params.Timeout = PingTimeout;
	Evaluator->FindDatacenters(Params, Datacenters, FOnQosSearchComplete::CreateUObject(this, &UQosRegionManager::OnQosEvaluationComplete));
}

void UQosRegionManager::OnQosEvaluationComplete(EQosCompletionResult Result, const TArray<FQosRegionInfo>& RegionInfo)
{
	// toss the evaluator
	if (Evaluator != nullptr)
	{
		Evaluator->RemoveFromRoot();
		Evaluator = nullptr;
	}
	QosEvalResult = Result;
	RegionOptions.Empty(RegionInfo.Num());

	// Always capture the region information (its still correct, even if in a bad state)
	RegionOptions = RegionInfo;

	LastCheckTimestamp = FDateTime::UtcNow();

	if (!SelectedRegionId.IsEmpty() && SelectedRegionId == NO_REGION)
	{
		// Put the dev region back into the list and select it
		ForceSelectRegion(SelectedRegionId);
	}

	// treat lack of any regions as a failure
	if (RegionInfo.Num() <= 0)
	{
		QosEvalResult = EQosCompletionResult::Failure;
	}

	if (QosEvalResult == EQosCompletionResult::Success ||
		QosEvalResult == EQosCompletionResult::Failure)
	{
		if (RegionOptions.Num() > 0)
		{
			// Try to set something regardless of Qos result
			TrySetDefaultRegion();
		}
	}
	
	// fire notifications
	TArray<FSimpleDelegate> NotifyList = OnQosEvalCompleteDelegate;
	OnQosEvalCompleteDelegate.Empty();
	for (const auto& Callback : NotifyList)
	{
		Callback.ExecuteIfBound();
	}
}

FString UQosRegionManager::GetRegionId() const
{
	if (!ForceRegionId.IsEmpty())
	{
		// we may have updated INI to bypass this process
		return ForceRegionId;
	}

	if (QosEvalResult == EQosCompletionResult::Invalid)
	{
		// if we haven't run the evaluator just use the region from settings
		// development dedicated server will come here, live services should use -mcpregion
		return NO_REGION;
	}

	if (SelectedRegionId.IsEmpty())
	{
		// Always set some kind of region, empty implies "wildcard" to the matchmaking code
		UE_LOG(LogQos, Verbose, TEXT("No region currently set."));
		return NO_REGION;
	}

	return SelectedRegionId;
}

FString UQosRegionManager::GetBestRegion() const
{
	if (!ForceRegionId.IsEmpty())
	{
		return ForceRegionId;
	}

	FString BestRegionId;

	// try to select the lowest ping
	int32 BestPing = INT_MAX;
	const TArray<FQosRegionInfo>& LocalRegionOptions = GetRegionOptions();
	for (const FQosRegionInfo& Region : LocalRegionOptions)
	{
		bool bValidResults = (Region.Result == EQosRegionResult::Success) || (Region.Result == EQosRegionResult::Incomplete);
		if (Region.IsUsable() && bValidResults &&
			!Region.Region.bBeta && Region.AvgPingMs < BestPing)
		{
			BestPing = Region.AvgPingMs;
			BestRegionId = Region.Region.RegionId;
		}
	}

	return BestRegionId;
}

const TArray<FQosRegionInfo>& UQosRegionManager::GetRegionOptions() const
{
	if (ForceRegionId.IsEmpty())
	{
		return RegionOptions;
	}

	static TArray<FQosRegionInfo> ForcedRegionOptions;
	ForcedRegionOptions.Empty(1);
	for (const FQosRegionInfo& RegionOption : RegionOptions)
	{
		if (RegionOption.Region.RegionId == ForceRegionId)
		{
			ForcedRegionOptions.Add(RegionOption);
		}
	}
#if !UE_BUILD_SHIPPING
	if (ForcedRegionOptions.Num() == 0)
	{
		FQosRegionInfo FakeRegionInfo;
		FakeRegionInfo.Region.DisplayName =	NSLOCTEXT("MMRegion", "DevRegion", "Development");
		FakeRegionInfo.Region.RegionId = ForceRegionId;
		FakeRegionInfo.Region.bEnabled = true;
		FakeRegionInfo.Region.bVisible = true;
		FakeRegionInfo.Region.bBeta = false;
		FakeRegionInfo.Result = EQosRegionResult::Success;
		FakeRegionInfo.AvgPingMs = 0;
		ForcedRegionOptions.Add(FakeRegionInfo);
	}
#endif
	return ForcedRegionOptions;
}

void UQosRegionManager::ForceSelectRegion(const FString& InRegionId)
{
	if (!bRegionForcedViaCommandline)
	{
		QosEvalResult = EQosCompletionResult::Success;
		ForceRegionId = InRegionId.ToUpper();

		// make sure we can select this region
		if (!SetSelectedRegion(ForceRegionId, true))
		{
			UE_LOG(LogQos, Log, TEXT("Failed to force set region id %s"), *ForceRegionId);
			ForceRegionId.Empty();
		}
	}
	else
	{
		UE_LOG(LogQos, Log, TEXT("Forcing region %s skipped because commandline override used %s"), *InRegionId, *ForceRegionId);
	}
}

void UQosRegionManager::TrySetDefaultRegion()
{
	if (!IsRunningDedicatedServer())
	{
		// Try to set a default region if one hasn't already been selected
		if (!SetSelectedRegion(GetRegionId()))
		{
			FString BestRegionId = GetBestRegion();
			if (!SetSelectedRegion(BestRegionId))
			{
				UE_LOG(LogQos, Warning, TEXT("Unable to set a good region!"));
				UE_LOG(LogQos, Warning, TEXT("Wanted to set %s, failed to fall back to %s"), *GetRegionId(), *BestRegionId);
				DumpRegionStats();
			}
		}
	}
}

bool UQosRegionManager::IsUsableRegion(const FString& InRegionId) const
{
	const TArray<FQosRegionInfo>& LocalRegionOptions = GetRegionOptions();
	for (const FQosRegionInfo& RegionInfo : LocalRegionOptions)
	{
		if (RegionInfo.Region.RegionId == InRegionId)
		{
			return RegionInfo.IsUsable();
		}
	}

	UE_LOG(LogQos, Log, TEXT("IsUsableRegion: failed to find region id %s"), *InRegionId);
	return false;
}

bool UQosRegionManager::SetSelectedRegion(const FString& InRegionId, bool bForce)
{
	// make sure we've enumerated
	if (bForce || QosEvalResult == EQosCompletionResult::Success)
	{
		// make sure it's in the option list
		FString RegionId = InRegionId.ToUpper();

		const TArray<FQosRegionInfo>& LocalRegionOptions = GetRegionOptions();
		for (const FQosRegionInfo& RegionInfo : LocalRegionOptions)
		{
			if (RegionInfo.Region.RegionId == RegionId)
			{
				if (RegionInfo.IsUsable())
				{
					SelectedRegionId = RegionId;
					return true;
				}
				else
				{
					return false;
				}
			}
		}
	}

	// can't select a region not in the options list (NONE is special, it means pick best)
	if (!InRegionId.IsEmpty() && (InRegionId != NO_REGION))
	{
		UE_LOG(LogQos, Log, TEXT("SetSelectedRegion: failed to find region id %s"), *InRegionId);
	}
	return false;
}

void UQosRegionManager::ClearSelectedRegion()
{ 
	// Do not default to NO_REGION
	SelectedRegionId.Empty();
	if (!bRegionForcedViaCommandline)
	{
		ForceRegionId.Empty();
	}
}

bool UQosRegionManager::AllRegionsFound() const
{
	int32 NumRegions = 0;
	for (const FQosDatacenterInfo& Datacenter : Datacenters)
	{
		if (Datacenter.IsPingable())
		{
			++NumRegions;
		}
	}

	// Look at real region options here
	if (NumRegions == RegionOptions.Num())
	{
		for (const FQosRegionInfo& Region : RegionOptions)
		{
			// All regions need to have a good amount of data to be consider viable
			bool bGoodPercentage = (((float)Region.NumResponses / (float)NumTestsPerRegion) >= 0.5f);
			if (!bGoodPercentage)
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

void UQosRegionManager::DumpRegionStats()
{
	UE_LOG(LogQos, Display, TEXT("Region Info:"));
	UE_LOG(LogQos, Display, TEXT("Current: %s "), *SelectedRegionId);
	if (!ForceRegionId.IsEmpty())
	{
		UE_LOG(LogQos, Display, TEXT("Forced: %s "), *ForceRegionId);
	}
	
	// Look at real region options here
	UE_LOG(LogQos, Display, TEXT("Overall Result: %s"), ToString(QosEvalResult));
	for (const FQosRegionInfo& Region : RegionOptions)
	{
		UE_LOG(LogQos, Display, TEXT("Region: %s [%s] Ping: %d"), *Region.Region.DisplayName.ToString(), *Region.Region.RegionId, Region.AvgPingMs);
		UE_LOG(LogQos, Display, TEXT("\tEnabled: %d Visible: %d Beta: %d Result: %s"), Region.Region.bEnabled, Region.Region.bVisible, Region.Region.bBeta, ToString(Region.Result));
	}
}

void UQosRegionManager::RegisterQoSSettingsChangedDelegate(const FSimpleDelegate& OnQoSSettingsChanged)
{
	// add to the completion delegate
	OnQoSSettingsChangedDelegate = OnQoSSettingsChanged;
}
