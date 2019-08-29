// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EyeTrackerTypes.generated.h"


// struct FEyeTrackerKeys
// {
// 	static const FKey Gaze_X;
// 	static const FKey Gaze_Y;
// };

/** 
 * Represents a unified gaze ray that incorporates both eyes.
 */
USTRUCT(BlueprintType)
struct FEyeTrackerGazeData
{
	GENERATED_BODY()

public:

	FEyeTrackerGazeData()
		: ConfidenceValue(0.f)
	{}

	/** Origin of the unified gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gaze Data")
	FVector GazeOrigin;
	
	/** Forward direction of the unified gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze Data")
	FVector GazeDirection;

	/** Location that the eyes converge. This is the 3d point where the tracked viewer is looking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze Data")
	FVector FixationPoint;

	/** Value [0..1] that represents confidence in the gaze ray data above. Stable, reliably tracked eyes will be at or near 1. Fast-moving or unreliably tracked eyes will be at or near 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaze Data")
	float ConfidenceValue;
};


/** 
 * Stereo gaze data that contains information for each eye individually.
 * This may not be available with all devices.
 */
USTRUCT(BlueprintType)
struct FEyeTrackerStereoGazeData
{
	GENERATED_BODY()

public:

	FEyeTrackerStereoGazeData()
		: ConfidenceValue(0.f)
	{}

	/** Origin of the left eye's gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	FVector LeftEyeOrigin;
	/** Forward direction of the left eye's gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	FVector LeftEyeDirection;

	/** Origin of the right eye's gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	FVector RightEyeOrigin;
	/** Forward direction of the right eye's gaze ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	FVector RightEyeDirection;

	/** Location that the eyes converge. This is the 3d point where the tracked viewer is looking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	FVector FixationPoint;

	/** Value [0..1] that represents confidence in the gaze ray data above. Stable, reliably tracked eyes will be at or near 1. Fast-moving or unreliably tracked eyes will be at or near 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stereo Gaze Data")
	float ConfidenceValue;

};

UENUM(BlueprintType)
enum class EEyeTrackerStatus : uint8
{
	/** Eyetracking feature is not available (device not plugged in, etc) */
	NotConnected,
	/** Eyetracking is operating, but eyes are not being tracked */
	NotTracking,
	/** Eyetracking is operating and eyes are being tracked */
	Tracking,					
};
