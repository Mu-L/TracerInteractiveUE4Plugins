// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SlateViewerApp.h"
#include "LinuxCommonStartup.h"

int main(int argc, char *argv[])
{
	return CommonLinuxMain(argc, argv, &RunSlateViewer);
}
