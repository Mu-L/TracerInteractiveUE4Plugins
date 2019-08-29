// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CrashUpload.h"

/**
 * Implementation of the crash report client used for unattended uploads
 */
class FCrashReportClientUnattended
{
public:
	/**
	 * Set up uploader object
	 * @param ErrorReport Error report to upload
	 */
	explicit FCrashReportClientUnattended(FPlatformErrorReport& InErrorReport);

private:
	/**
	 * Update received every second
	 * @param DeltaTime Time since last update, unused
	 * @return Whether the updates should continue
	 */
	bool Tick(float DeltaTime);

	/**
	 * Begin calling Tick once a second
	 */
	void StartTicker();

	/** Object that uploads report files to the server */
	FCrashUploadToReceiver ReceiverUploader;

	/** Object that uploads report files to the server */
	FCrashUploadToDataRouter DataRouterUploader;

	/** Platform code for accessing the report */
	FPlatformErrorReport ErrorReport;
};
