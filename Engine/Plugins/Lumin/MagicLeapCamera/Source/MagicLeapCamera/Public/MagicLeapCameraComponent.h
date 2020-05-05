// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapCameraComponent.generated.h"

/**
  The MagicLeapCameraComponent provides access to and maintains state for camera capture functionality.
  The connection to the device's camera is managed internally.  Users of this component
  are able to asynchronously capture camera images and footage to file.  Alternatively,
  a camera image can be captured directly to texture.  The user need only make the relevant
  asynchronous call and then register the appropriate event handlers for the
  operation's completion.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPCAMERA_API UMagicLeapCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Notifies the MagicLeapCameraPlugin of a new user. */
	void BeginPlay() override;

	/** Notifies the MagicLeapCameraPlugin that a user is being destroyed (needed for auto-disconnecting the camera). */
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
		Initiates a capture image to file task on a separate thread.
		@brief The newly created jpeg file will have an automatically generated name which is guaranteed
			   to be unique.  Upon completion, a successful operation will provide the file path of the newly
			   created jpeg to the FMagicLeapCameraCaptureImgToFile event handler.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Camera | MagicLeap")
	virtual bool CaptureImageToFileAsync();

	/**
		Initiates a capture image to memory task on a speparate thread.
		@brief The user should register event handlers for both the success and fail events.  Upon completion,
			   a successful operation will provide a dynamically generated texture containing the captured
			   image to the FMagicLeapCameraCaptureImgToTextureSuccess event handler.
		@note The generated texture will be garbage collected when this app is destroyed.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Camera | MagicLeap")
	virtual bool CaptureImageToTextureAsync();

	/**
		Initiates the capturing of video/audio data on a separate thread.
		@note The system will continue to record video until StopRecordingVideo is called.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Camera | MagicLeap")
	virtual bool StartRecordingAsync();

	/**
		Stops the recording and saves the video/audio data to an mp4 file.
		@note The newly created mp4 file will have an automatically generated name which is guaranteed
			  to be unique.
		@return True if the call succeeds, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Camera | MagicLeap")
	virtual bool StopRecordingAsync();

	/**
		Gets the capture state of the component.
		@return True if the component is currently capturing, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "Camera | MagicLeap")
	bool IsCapturing() const;

private:
	UPROPERTY(BlueprintAssignable, Category = "Camera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraCaptureImgToFileMulti OnCaptureImgToFile;
	UPROPERTY(BlueprintAssignable, Category = "Camera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraCaptureImgToTextureMulti OnCaptureImgToTexture;
	UPROPERTY(BlueprintAssignable, Category = "Camera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraStartRecordingMulti OnStartRecording;
	UPROPERTY(BlueprintAssignable, Category = "Camera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraStopRecordingMulti OnStopRecording;
	UPROPERTY(BlueprintAssignable, Category = "Camera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraLogMessageMulti OnLogMessage;
};
