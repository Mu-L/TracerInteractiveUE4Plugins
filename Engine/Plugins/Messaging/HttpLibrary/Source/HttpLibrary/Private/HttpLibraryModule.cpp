// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryModule.h"
#include "Modules/ModuleManager.h"

class FHttpLibraryModule : public IHttpLibraryModule
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

IMPLEMENT_MODULE(FHttpLibraryModule, HttpLibrary);
