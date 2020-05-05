// Copyright Epic Games, Inc. All Rights Reserved.

#include "Linux/LinuxConsoleOutputDevice.h"
#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Linux/LinuxPlatformApplicationMisc.h"

#define CONSOLE_RED		"\x1b[31m"
#define CONSOLE_GREEN	"\x1b[32m"
#define CONSOLE_YELLOW	"\x1b[33m"
#define CONSOLE_BLUE	"\x1b[34m"
#define CONSOLE_NONE	"\x1b[0m"

FLinuxConsoleOutputDevice::FLinuxConsoleOutputDevice()
	: bOverrideColorSet(false),
	  bOutputtingToTerminal(isatty(STDOUT_FILENO))
{
}

FLinuxConsoleOutputDevice::~FLinuxConsoleOutputDevice()
{
}

void FLinuxConsoleOutputDevice::Show(bool bShowWindow)
{
}

bool FLinuxConsoleOutputDevice::IsShown()
{
	return true;
}

void FLinuxConsoleOutputDevice::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	static bool bEntry=false;
	if (!GIsCriticalError || bEntry)
	{
		if (Verbosity == ELogVerbosity::SetColor)
		{
			printf("%s", TCHAR_TO_UTF8(Data));
		}
		else
		{
			bool bNeedToResetColor = false;

			if (bOutputtingToTerminal && !bOverrideColorSet)
			{
				if (Verbosity == ELogVerbosity::Error)
				{
					printf(CONSOLE_RED);
					bNeedToResetColor = true;
				}
				else if (Verbosity == ELogVerbosity::Warning)
				{
					printf(CONSOLE_YELLOW);
					bNeedToResetColor = true;
				}
			}

			printf("%s\n", TCHAR_TO_UTF8(*FOutputDeviceHelper::FormatLogLine(Verbosity, Category, Data, GPrintLogTimes)));

			if (bNeedToResetColor)
			{
				printf(CONSOLE_NONE);
			}
		}
	}
	else
	{
		bEntry=true;
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
		{
#endif // !PLATFORM_EXCEPTIONS_DISABLED
			// Ignore errors to prevent infinite-recursive exception reporting.
			Serialize(Data, Verbosity, Category);
#if !PLATFORM_EXCEPTIONS_DISABLED
		}
		catch (...)
		{
		}
#endif // !PLATFORM_EXCEPTIONS_DISABLED
		bEntry = false;
	}
}
