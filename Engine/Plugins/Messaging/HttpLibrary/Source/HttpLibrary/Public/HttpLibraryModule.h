// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Modules/ModuleManager.h"

class IHttpLibraryModule : public IModuleInterface
{
public:

	static inline IHttpLibraryModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IHttpLibraryModule>( "HttpLibrary" );
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "HttpLibrary" );
	}
};
