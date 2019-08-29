// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


/**
 * Implements the ProfilerMessages module.
 */
class FProfilerMessagesModule
	: public IModuleInterface
{
public:

	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
};


IMPLEMENT_MODULE(FProfilerMessagesModule, ProfilerMessages);
