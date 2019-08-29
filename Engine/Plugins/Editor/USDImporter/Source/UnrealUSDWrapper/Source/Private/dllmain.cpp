// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

// dllmain.cpp : Defines the entry point for the DLL application.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "UnrealUSDWrapper.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		InitWrapper();
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

