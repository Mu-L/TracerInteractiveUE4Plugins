// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxNoEditorTargetPlatformModule.cpp: Implements the FLinuxNoEditorTargetPlatformModule class.
=============================================================================*/

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Interfaces/ITargetPlatformModule.h"

#include "LinuxTargetDevice.h"
#include "LinuxTargetPlatform.h"

/**
 * Holds the target platform singleton.
 */
static ITargetPlatform* Singleton = NULL;


/**
 * Module for the Linux target platform (without editor).
 */
class FLinuxNoEditorTargetPlatformModule
	: public ITargetPlatformModule
{
public:

	virtual ~FLinuxNoEditorTargetPlatformModule( )
	{
		Singleton = NULL;
	}

	virtual ITargetPlatform* GetTargetPlatform( )
	{
		if (Singleton == NULL && TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, false> >::IsUsable())
		{
			Singleton = new TLinuxTargetPlatform<FLinuxPlatformProperties<false, false, false, false> >();
		}

		return Singleton;
	}
};


IMPLEMENT_MODULE(FLinuxNoEditorTargetPlatformModule, LinuxNoEditorTargetPlatform);
