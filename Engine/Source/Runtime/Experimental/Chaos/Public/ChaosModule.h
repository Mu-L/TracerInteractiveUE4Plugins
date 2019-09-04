// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Modules/ModuleInterface.h"

class CHAOS_API FChaosEngineModule : public IModuleInterface
{
public:

	virtual void StartupModule() override {};
	virtual void ShutdownModule() override {};
};

#endif