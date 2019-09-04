// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	UnixPlatformSurvey.h: Unix platform hardware-survey classes
==============================================================================================*/

#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformSurvey.h"

/**
* Unix implementation of FGenericPlatformSurvey
**/
struct FUnixPlatformSurvey : public FGenericPlatformSurvey
{
	/** Start, or check on, the hardware survey */
	APPLICATIONCORE_API static bool GetSurveyResults(FHardwareSurveyResults& OutResults, bool bWait = false);

private:

	/**
	 * Fills out the actual OS name ("distro" is an OS for all the practical purposes), as detailed as possible.
	 */
	static void GetOSName(FHardwareSurveyResults& Results);

	/**
	 * Safely write strings into the fixed length TCHAR buffers of a FHardwareSurveyResults member
	 */
	static void WriteFStringToResults(TCHAR* OutBuffer, const FString& InString);
};

typedef FUnixPlatformSurvey FPlatformSurvey;
