// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "WebUIModule.h"
#include "Modules/ModuleManager.h"

class FWebUIModule : public IWebUIModule
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

IMPLEMENT_MODULE(FWebUIModule, WebUI);
