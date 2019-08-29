// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryModule.h"
#include "Modules/ModuleManager.h"

class FJsonLibraryModule : public IJsonLibraryModule
{
public:
	virtual void StartupModule() override
	{
		//
	}

	virtual void ShutdownModule() override
	{
		//
	}
};

IMPLEMENT_MODULE(FJsonLibraryModule, JsonLibrary);
