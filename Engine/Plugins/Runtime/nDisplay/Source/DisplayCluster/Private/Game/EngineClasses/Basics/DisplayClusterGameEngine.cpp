// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterGameEngine.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "DisplayClusterConfigurationTypes.h"
#include "IDisplayClusterConfiguration.h"

#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DisplayClusterAppExit.h"
#include "Misc/Parse.h"
#include "Misc/QualifiedFrameTime.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "Stats/Stats.h"

#include "Misc/CoreDelegates.h"


void UDisplayClusterGameEngine::Init(class IEngineLoop* InEngineLoop)
{
	// Detect requested operation mode
	OperationMode = DetectOperationMode();

	// Initialize Display Cluster
	if (!GDisplayCluster->Init(OperationMode))
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::KillImmediately, FString("Couldn't initialize DisplayCluster module"));
	}

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Our parsing function for arguments like:
		// -ArgName1="ArgValue 1" -ArgName2=ArgValue2 ArgName3=ArgValue3
		//
		auto ParseCommandArg = [](const FString& CommandLine, const FString& ArgName, FString& OutArgVal) {
			const FString Tag = FString::Printf(TEXT("-%s="), *ArgName);
			const int32 TagPos = CommandLine.Find(Tag);

			if (TagPos == INDEX_NONE)
			{
				// Try old method, where the '-' prefix is missing and quoted values with spaces are not supported.
				return FParse::Value(*CommandLine, *ArgName, OutArgVal);
			}

			const TCHAR* TagValue = &CommandLine[TagPos + Tag.Len()];

			if (*TagValue == TEXT('"'))
			{
				return FParse::QuotedString(TagValue, OutArgVal);
			}

			return FParse::Token(TagValue, OutArgVal, false);
		};

		FString ConfigPath;

		// Extract config path from command line
		if (!ParseCommandArg(FCommandLine::Get(), DisplayClusterStrings::args::Config, ConfigPath))
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::KillImmediately, FString("No config file specified. Cluster operation mode requires config file."));
		}

		// Clean the file path before using it
		DisplayClusterHelpers::str::TrimStringValue(ConfigPath);

		// Load config data
		const UDisplayClusterConfigurationData* ConfigData = IDisplayClusterConfiguration::Get().LoadConfig(ConfigPath);
		if (!ConfigData)
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::KillImmediately, FString("An error occurred during loading the configuration file"));
		}

		FString NodeId;

		// Extract node ID from command line
		if (!ParseCommandArg(FCommandLine::Get(), DisplayClusterStrings::args::Node, NodeId))
		{
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Node ID is not specified. Trying to resolve from host address..."));

			// Find node ID based on the host address
			if (!GetResolvedNodeId(ConfigData, NodeId))
			{
				FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::KillImmediately, FString("Couldn't resolve node ID. Try to specify host addresses explicitly."));
			}

			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Node ID has been successfully resolved: %s"), *NodeId);
		}

		// Clean node ID string
		DisplayClusterHelpers::str::TrimStringValue(NodeId);

		// Start game session
		if (!GDisplayCluster->StartSession(ConfigData, NodeId))
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::KillImmediately, FString("Couldn't start DisplayCluster session"));
		}

		// Initialize internals
		InitializeInternals();
	}

	// Initialize base stuff.
	UGameEngine::Init(InEngineLoop);
}

EDisplayClusterOperationMode UDisplayClusterGameEngine::DetectOperationMode() const
{
	EDisplayClusterOperationMode OpMode = EDisplayClusterOperationMode::Disabled;
	if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::Cluster))
	{
		OpMode = EDisplayClusterOperationMode::Cluster;
	}

	UE_LOG(LogDisplayClusterEngine, Log, TEXT("Detected operation mode: %s"), *DisplayClusterTypesConverter::template ToString(OpMode));

	return OpMode;
}

bool UDisplayClusterGameEngine::InitializeInternals()
{
	// This function is called after a session had been started so it's safe to get config data from the config manager
	const UDisplayClusterConfigurationData* Config = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	check(Config);

	// Store diagnostics settings locally
	Diagnostics = Config->Diagnostics;
	
	InputMgr       = GDisplayCluster->GetPrivateInputMgr();
	ClusterMgr     = GDisplayCluster->GetPrivateClusterMgr();
	NodeController = ClusterMgr->GetController();

	check(ClusterMgr);
	check(InputMgr);
	check(NodeController);

	const UDisplayClusterConfigurationClusterNode* CfgLocalNode = GDisplayCluster->GetPrivateConfigMgr()->GetLocalNode();
	const bool bSoundEnabled = (CfgLocalNode ? CfgLocalNode->bIsSoundEnabled : false);
	UE_LOG(LogDisplayClusterEngine, Log, TEXT("Configuring sound enabled: %s"), *DisplayClusterTypesConverter::template ToString(bSoundEnabled));
	if (!bSoundEnabled)
	{
		AudioDeviceManager = nullptr;
	}

	return true;
}

// This function works if you have 1 cluster node per PC. In case of multiple nodes, all of them will have the same node ID.
bool UDisplayClusterGameEngine::GetResolvedNodeId(const UDisplayClusterConfigurationData* ConfigData, FString& NodeId) const
{
	TArray<TSharedPtr<FInternetAddr>> LocalAddresses;
	if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(LocalAddresses))
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get local addresses list. Cannot find node ID by its address."));
		return false;
	}

	if (LocalAddresses.Num() < 1)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No local addresses found"));
		return false;
	}

	for (const auto& it : ConfigData->Cluster->Nodes)
	{
		for (const auto& LocalAddress : LocalAddresses)
		{
			const FIPv4Endpoint ep(LocalAddress);
			const FString epaddr = ep.Address.ToString();
			
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Comparing addresses: %s - %s"), *epaddr, *it.Value->Host);

			//@note: don't add "127.0.0.1" or "localhost" here. There will be a bug. It has been proved already.
			if (epaddr.Equals(it.Value->Host, ESearchCase::IgnoreCase))
			{
				// Found!
				NodeId = it.Key;
				return true;
			}
		}
	}

	// We haven't found anything
	return false;
}

void UDisplayClusterGameEngine::PreExit()
{
	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Close current DisplayCluster session
		GDisplayCluster->EndSession();
	}

	// Release the engine
	UGameEngine::PreExit();
}

bool UDisplayClusterGameEngine::LoadMap(FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error)
{
	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Finish previous scene
		GDisplayCluster->EndScene();

		// Perform map loading
		if (!Super::LoadMap(WorldContext, URL, Pending, Error))
		{
			return false;
		}

		// Start new scene
		GDisplayCluster->StartScene(WorldContext.World());

		// Game start barrier
		if (NodeController)
		{
			NodeController->WaitForGameStart(nullptr, nullptr);
		}
	}
	else
	{
		return Super::LoadMap(WorldContext, URL, Pending, Error);
	}

	return true;
}

void UDisplayClusterGameEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		TOptional<FQualifiedFrameTime> FrameTime;

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame start barrier
		{
			double ThreadTime  = 0.f;
			double BarrierTime = 0.f;

			UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame start"));
			NodeController->WaitForFrameStart(&ThreadTime, &BarrierTime);
			UE_LOG(LogDisplayClusterEngine, VeryVerbose, TEXT("FrameStartBarrier: ThreadTime=%f, BarrierTime=%f"), ThreadTime, BarrierTime);
		}

		// Perform StartFrame notification
		GDisplayCluster->StartFrame(GFrameCounter);

		// Sync DeltaSeconds
		NodeController->GetDeltaTime(DeltaSeconds);
		FApp::SetDeltaTime(DeltaSeconds);
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster delta seconds: %f"), DeltaSeconds);

		// Sync timecode and framerate
		NodeController->GetFrameTime(FrameTime);

		if (FrameTime.IsSet())
		{
			FApp::SetCurrentFrameTime(FrameTime.GetValue());
			UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster timecode: %s | %s"), *FTimecode::FromFrameNumber(FrameTime->Time.GetFrame(), FrameTime->Rate).ToString(), *FrameTime->Rate.ToPrettyText().ToString());
		}
		else
		{
			FApp::InvalidateCurrentFrameTime();
			UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster timecode: [Invalid]"));
		}

		// Perform PreTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PreTick()"));
		GDisplayCluster->PreTick(DeltaSeconds);

		// Perform UGameEngine::Tick() calls for scene actors
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform UGameEngine::Tick()"));
		Super::Tick(DeltaSeconds, bIdleMode);

		// Perform PostTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PostTick()"));
		GDisplayCluster->PostTick(DeltaSeconds);

		if (Diagnostics.bSimulateLag)
		{
			const float LagTime = FMath::RandRange(Diagnostics.MinLagTime, Diagnostics.MaxLagTime);
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Simulating lag: %f seconds"), LagTime);
			FPlatformProcess::Sleep(LagTime);
		}

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame end barrier
		NodeController->WaitForFrameEnd(nullptr, nullptr);

		// Perform EndFrame notification
		GDisplayCluster->EndFrame(GFrameCounter);

		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame end"));
	}
	else
	{
		Super::Tick(DeltaSeconds, bIdleMode);
	}
}
