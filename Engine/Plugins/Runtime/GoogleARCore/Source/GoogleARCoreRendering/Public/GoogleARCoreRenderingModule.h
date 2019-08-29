// Copyright 2017 Google Inc.

#pragma once

#include "Modules/ModuleManager.h"

class FGoogleARCoreRenderingModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation. */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
