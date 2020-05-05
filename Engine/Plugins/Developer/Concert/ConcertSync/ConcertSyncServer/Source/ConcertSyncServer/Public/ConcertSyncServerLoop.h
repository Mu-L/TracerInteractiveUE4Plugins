// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "ConcertSyncSessionFlags.h"

class UConcertServerConfig;
class IConcertFileSharingService;

struct FConcertSyncServerLoopInitArgs
{
	/** Framerate that the main loop should try to Tick at */
	int32 IdealFramerate = 60;

	/** Flags controlling what features are enabled for sessions within this server */
	EConcertSyncSessionFlags SessionFlags = EConcertSyncSessionFlags::None;

	/** The role that this server will perform (eg, MultiUser, DisasterRecovery, etc) */
	FString ServiceRole;

	/** Friendly name to use for this service (when showing it to a user in log messages, etc) */
	FString ServiceFriendlyName;

	/** The session filter to apply when auto-archiving sessions on this server */
	FConcertSessionFilter ServiceAutoArchiveSessionFilter;

	/** The optional file sharing server to exchange large files. Can be null. */
	TSharedPtr<IConcertFileSharingService> FileSharingService;

	/** Function to get the server settings object to configure the server with with, or unbound to parse the default settings */
	TFunction<const UConcertServerConfig*()> GetServerConfigFunc;

	/** Whether the service should show the log console. */
	bool bShowConsole = true;
};

/**
 * Blocking main loop for running a Concert Sync server application.
 */
int32 ConcertSyncServerLoop(int32 ArgC, TCHAR** ArgV, const FConcertSyncServerLoopInitArgs& InitArgs);

#include "ConcertSyncServerLoop.inl"
