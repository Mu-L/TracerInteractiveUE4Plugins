// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynthesisModule.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UI/SynthSlateStyle.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSynthesis);

IMPLEMENT_MODULE(FSynthesisModule, Synthesis)

void FSynthesisModule::StartupModule()
{
	FSynthSlateStyleSet::Initialize();

	FModuleManager::Get().LoadModuleChecked(TEXT("SignalProcessing"));
}

void FSynthesisModule::ShutdownModule()
{
	FSynthSlateStyleSet::Shutdown();
}


