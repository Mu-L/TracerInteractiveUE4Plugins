// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Apple/ApplePlatformCrashContext.h"

struct CORE_API FIOSCrashContext : public FApplePlatformCrashContext
{
	/** Constructor */
	FIOSCrashContext(ECrashContextType InType, const TCHAR* InErrorMessage);

	/** Copies the PLCrashReporter minidump */
	void CopyMinidump(char const* OutputPath, char const* InputPath) const;

	/** Generates the ensure/crash info into the given folder */
	void GenerateInfoInFolder(char const* const InfoFolder, bool bIsEnsure = false) const;
	
	/** Generates information for crash reporter */
	void GenerateCrashInfo() const;
	
	/** Generates information for ensures sent via the CrashReporter */
	void GenerateEnsureInfo() const;
};

typedef FIOSCrashContext FPlatformCrashContext;
