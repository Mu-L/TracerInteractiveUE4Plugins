// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "Windows/WindowsHWrapper.h"

/**
 * WinMain, called when the application is started
 */
int WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int nCmdShow)
{
	// Run the app
	RunCrashReportClient(GetCommandLineW());

	return 0;
}
