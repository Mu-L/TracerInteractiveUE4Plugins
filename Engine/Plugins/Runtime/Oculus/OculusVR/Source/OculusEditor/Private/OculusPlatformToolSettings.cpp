// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OculusPlatformToolSettings.h"


UOculusPlatformToolSettings::UOculusPlatformToolSettings()
	: OculusTargetPlatform(EOculusPlatformTarget::Rift)
{
	uint8 NumPlatforms = (uint8)EOculusPlatformTarget::Length;
	OculusApplicationID.Init("", NumPlatforms);
	OculusApplicationToken.Init("", NumPlatforms);
	OculusReleaseChannel.Init("Alpha", NumPlatforms);
	OculusReleaseNote.Init("", NumPlatforms);
	OculusLaunchFilePath.Init("", NumPlatforms);
}


