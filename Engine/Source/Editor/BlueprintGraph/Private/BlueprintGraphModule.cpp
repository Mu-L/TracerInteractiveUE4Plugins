// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "BlueprintGraphModule.h"

#include "EdGraphSchema_K2.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE( FBlueprintGraphModule, BlueprintGraph );

void FBlueprintGraphModule::ShutdownModule()
{
	UEdGraphSchema_K2::Shutdown();
}

