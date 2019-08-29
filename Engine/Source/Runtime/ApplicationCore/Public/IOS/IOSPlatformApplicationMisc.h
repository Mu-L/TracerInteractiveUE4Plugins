// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct APPLICATIONCORE_API FIOSPlatformApplicationMisc : public FGenericPlatformApplicationMisc
{
	static void LoadPreInitModules();

	static class FOutputDeviceError* GetErrorOutputDevice();
	static class GenericApplication* CreateApplication();
	static bool ControlScreensaver(EScreenSaverAction Action);

	static void ResetGamepadAssignments();
	static void ResetGamepadAssignmentToController(int32 ControllerId);
	static bool IsControllerAssignedToGamepad(int32 ControllerId);

	static void ClipboardCopy(const TCHAR* Str);
	static void ClipboardPaste(class FString& Dest);

	static EScreenPhysicalAccuracy ComputePhysicalScreenDensity(int32& ScreenDensity);

private:
	static class FIOSApplication* CachedApplication;
};

typedef FIOSPlatformApplicationMisc FPlatformApplicationMisc;
