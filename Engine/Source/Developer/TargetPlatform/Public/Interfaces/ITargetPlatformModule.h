// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class ITargetPlatform;

/**
 * Interface for target platform modules.
 */
class ITargetPlatformModule
	: public IModuleInterface
{
public:

	/**
	 * Gets the module's target platform.
	 *
	 * @return The target platform.
	 */
	virtual ITargetPlatform* GetTargetPlatform() = 0;

public:

	/** Virtual destructor. */
	virtual ~ITargetPlatformModule() { }
};
