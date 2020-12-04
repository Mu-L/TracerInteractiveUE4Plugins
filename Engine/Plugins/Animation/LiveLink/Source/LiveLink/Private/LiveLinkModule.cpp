// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkModule.h"

#include "Interfaces/IPluginManager.h"
#include "LiveLinkLogInstance.h"
#include "LiveLinkPreset.h"
#include "LiveLinkSettings.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "LiveLinkModule"

FLiveLinkClient* FLiveLinkModule::LiveLinkClient_AnyThread = nullptr;

FLiveLinkModule::FLiveLinkModule()
	: LiveLinkClient()
	, LiveLinkMotionController(LiveLinkClient)
	, HeartbeatEmitter(MakeUnique<FLiveLinkHeartbeatEmitter>())
	, DiscoveryManager(MakeUnique<FLiveLinkMessageBusDiscoveryManager>())
	, LiveLinkDebugCommand(MakeUnique<FLiveLinkDebugCommand>(LiveLinkClient))
{
}

void FLiveLinkModule::StartupModule()
{
	FLiveLinkLogInstance::CreateInstance();
	CreateStyle();

	FPlatformAtomics::InterlockedExchangePtr((void**)&LiveLinkClient_AnyThread, &LiveLinkClient);
	IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
	LiveLinkMotionController.RegisterController();

	//Register for engine initialization completed so we can load default preset if any. Presets could depend on plugins loaded at a later stage.
	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FLiveLinkModule::OnEngineLoopInitComplete);
}

void FLiveLinkModule::ShutdownModule()
{
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	HeartbeatEmitter->Exit();
	DiscoveryManager->Stop();
	LiveLinkMotionController.UnregisterController();

	IModularFeatures::Get().UnregisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
	FPlatformAtomics::InterlockedExchangePtr((void**)&LiveLinkClient_AnyThread, nullptr);

	FLiveLinkLogInstance::DestroyInstance();
}

void FLiveLinkModule::CreateStyle()
{
	static FName LiveLinkStyle(TEXT("LiveLinkCoreStyle"));
	StyleSet = MakeShared<FSlateStyleSet>(LiveLinkStyle);

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("LiveLink"))->GetContentDir();

	const FVector2D Icon16x16(16.0f, 16.0f);

	StyleSet->Set("LiveLinkIcon", new FSlateImageBrush((ContentDir / TEXT("LiveLink_16x")) + TEXT(".png"), Icon16x16));
}

void FLiveLinkModule::OnEngineLoopInitComplete()
{
	if (ULiveLinkPreset* Preset = GetDefault<ULiveLinkSettings>()->DefaultLiveLinkPreset.LoadSynchronous())
	{
		Preset->ApplyToClient();
	}
}

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE