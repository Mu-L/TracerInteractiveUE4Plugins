// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Unix/UnixPlatformOutputDevices.h"
#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceConsole.h"
#include "Misc/OutputDeviceRedirector.h"

void FUnixOutputDevices::SetupOutputDevices()
{
	check(GLog);
	check(GLogConsole);

	CachedAbsoluteFilename[0] = 0;

	// add file log
	GLog->AddOutputDevice(FPlatformOutputDevices::GetLog());

	// @todo: set to false for minor utils?
	bool bLogToConsole = !NO_LOGGING && !FParse::Param(FCommandLine::Get(), TEXT("NOCONSOLE"));

	if (bLogToConsole)
	{
		GLog->AddOutputDevice(GLogConsole);
	}

	// debug and event logging is not really supported on Unix. 
}

FString FUnixOutputDevices::GetAbsoluteLogFilename()
{
	// FIXME: this function should not exist once FGenericPlatformOutputDevices::GetAbsoluteLogFilename() returns absolute filename (see UE-25650)
	return FPaths::ConvertRelativePathToFull(FGenericPlatformOutputDevices::GetAbsoluteLogFilename());
}

class FOutputDevice* FUnixOutputDevices::GetEventLog()
{
	return NULL; // @TODO No event logging
}
