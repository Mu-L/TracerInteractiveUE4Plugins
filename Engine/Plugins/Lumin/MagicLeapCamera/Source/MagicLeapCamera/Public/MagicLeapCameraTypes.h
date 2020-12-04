// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapCameraTypes.generated.h"

USTRUCT(BlueprintType)
struct MAGICLEAPCAMERA_API FMagicLeapCameraPlaneInfo
{
	GENERATED_BODY()
	/** Width of the output image in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	int32 Width;
	/** Height of the output image in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	int32 Height;
	/** Stride of the output image in bytes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	int32 Stride;
	/** Number of bytes used to represent a pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	int32 BytesPerPixel;
	/** Image data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	TArray<uint8> Data;
};

USTRUCT(BlueprintType)
struct MAGICLEAPCAMERA_API FMagicLeapCameraOutput
{
	GENERATED_BODY()
	/**
		Output image plane info. The number of output planes is determined by the format:
		1 for compressed output such as JPEG stream,
		3 for separate color component output such as YUV/RGB.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	TArray<FMagicLeapCameraPlaneInfo> Planes;
	/** Supported output format specified by MLCameraOutputFormat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|MagicLeap")
	TEnumAsByte<EPixelFormat> Format;
};

/** Delegate used to notify the initiating blueprint when the camera connect task has completed. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCameraConnect, const bool, bSuccess);

/** Delegate used to notify the initiating blueprint when the camera disonnect task has completed. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCameraDisconnect, const bool, bSuccess);

/**
   Delegate used to notify the initiating blueprint when a capture image to file task has completed.
   @note Although this signals the task as complete, it may have failed or been cancelled.
   @param bSuccess True if the task succeeded, false otherwise.
   @param FilePath A string containing the file path to the newly created jpeg.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapCameraCaptureImgToFile, const bool, bSuccess, const FString&, FilePath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapCameraCaptureImgToFileMulti, const bool, bSuccess, const FString&, FilePath);

/**
	Delegate used to pass the captured image back to the initiating blueprint.
	@note The captured texture will remain in memory for the lifetime of the calling application (if the task succeeds).
	@param bSuccess True if the task succeeded, false otherwise.
	@param CaptureTexture A UTexture2D containing the captured image.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapCameraCaptureImgToTexture, const bool, bSuccess, UTexture2D*, CaptureTexture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapCameraCaptureImgToTextureMulti, const bool, bSuccess, UTexture2D*, CaptureTexture);

/**
	Delegate used to notify the initiating blueprint of the result of a request to begin recording video.
	@note Although this signals the task as complete, it may have failed or been cancelled.
	@param bSuccess True if the task succeeded, false otherwise.
*/
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCameraStartRecording, const bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapCameraStartRecordingMulti, const bool, bSuccess);

/**
	Delegate used to notify the initiating blueprint of the result of a request to stop recording video.
	@note Although this signals the task as complete, it may have failed or been cancelled.
	@param bSuccess True if the task succeeded, false otherwise.
	@param FilePath A string containing the path to the newly created mp4.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapCameraStopRecording, const bool, bSuccess, const FString&, FilePath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapCameraStopRecordingMulti, const bool, bSuccess, const FString&, FilePath);

/**
	Delegate used to pass log messages from the capture worker thread to the initiating blueprint.
	@note This is useful if the user wishes to have log messages in 3D space.
	@param LogMessage A string containing the log message.
*/
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCameraLogMessage, const FString&, LogMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapCameraLogMessageMulti, const FString&, LogMessage);
