// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Modules/ModuleManager.h"

class IWebUIModule : public IModuleInterface
{
public:

	static inline IWebUIModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IWebUIModule>( "WebUI" );
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "WebUI" );
	}
};
