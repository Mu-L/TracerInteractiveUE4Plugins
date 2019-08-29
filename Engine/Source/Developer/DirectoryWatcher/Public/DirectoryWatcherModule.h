// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IDirectoryWatcher;

class FDirectoryWatcherModule : public IModuleInterface
{
public:
	virtual void StartupModule();
	virtual void ShutdownModule();

	/** Gets the directory watcher singleton or returns NULL if the platform does not support directory watching */
	virtual IDirectoryWatcher* Get();

private:
	class IDirectoryWatcher* DirectoryWatcher;
};
