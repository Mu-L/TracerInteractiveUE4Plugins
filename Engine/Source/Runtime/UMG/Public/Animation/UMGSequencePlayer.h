// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Blueprint/UserWidget.h"
#include "IMovieScenePlayer.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Misc/QualifiedFrameTime.h"
#include "UMGSequencePlayer.generated.h"

class UWidgetAnimation;

UCLASS(Transient, BlueprintType)
class UMG_API UUMGSequencePlayer : public UObject, public IMovieScenePlayer
{
	GENERATED_UCLASS_BODY()

public:
	void InitSequencePlayer(UWidgetAnimation& InAnimation, UUserWidget& InUserWidget);

	/** Updates the running movie */
	void Tick( float DeltaTime );

	/** Begins playing or restarts an animation */
	void Play(float StartAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed);

	/** Begins playing or restarts an animation  and plays to the specified end time */
	void PlayTo(float StartAtTime, float EndAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed);

	/** Stops a running animation and resets time */
	void Stop();

	/** Pauses a running animation */
	void Pause();

	/** Reverses a running animation */
	void Reverse();

	/** Gets the current time position in the player (in seconds). */
	UE_DEPRECATED(4.20, "Please use GetCurrentTime instead.")
	double GetTimeCursorPosition() const { return GetCurrentTime().AsSeconds(); }

	FQualifiedFrameTime GetCurrentTime() const { return FQualifiedFrameTime(TimeCursorPosition, AnimationResolution); }

	/** @return The current animation being played */
	const UWidgetAnimation* GetAnimation() const { return Animation; }

	/** @return */
	UFUNCTION(BlueprintCallable, Category="Animation")
	FName GetUserTag() const { return UserTag; }

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetUserTag(FName InUserTag)
	{
		UserTag = InUserTag;
	}

	/** Sets the number of loops to play */
	void SetNumLoopsToPlay(int32 InNumLoopsToPlay);

	/** Sets the animation playback rate */
	void SetPlaybackSpeed(float PlaybackSpeed);

	/** Gets the current time position in the player (in seconds). */
	bool IsPlayingForward() const { return bIsPlayingForward; }

	/** IMovieScenePlayer interface */
	virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() override { return RootTemplateInstance; }
	virtual UObject* AsUObject() override { return this; }
	virtual void UpdateCameraCut(UObject* CameraObject, UObject* UnlockIfCameraObject, bool bJumpCut) override {}
	virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override {}
	virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override {}
	virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const override;
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;
	virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) override;

	DECLARE_EVENT_OneParam(UUMGSequencePlayer, FOnSequenceFinishedPlaying, UUMGSequencePlayer&);
	FOnSequenceFinishedPlaying& OnSequenceFinishedPlaying() { return OnSequenceFinishedPlayingEvent; }

private:
	/** Internal play function with a verbose parameter set */
	void PlayInternal(double StartAtTime, double EndAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed);

	/** Apply any latent actions which may have accumulated while the sequence was being evaluated */
	void ApplyLatentActions();

	/** Animation being played */
	UPROPERTY()
	UWidgetAnimation* Animation;

	/** The user widget this sequence is animating */
	TWeakObjectPtr<UUserWidget> UserWidget;

	FMovieSceneRootEvaluationTemplateInstance RootTemplateInstance;

	/** The resolution at which all FFrameNumbers are stored */
	FFrameRate AnimationResolution;

	FFrameNumber AbsolutePlaybackStart;

	/** The current time cursor position within the sequence, between 0 and Duration */
	FFrameTime TimeCursorPosition;

	/** The duration of the sequence */
	int32 Duration;

	/** Time at which to end the animation after looping */
	FFrameTime EndTime;

	/** Status of the player (e.g play, stopped) */
	EMovieScenePlayerStatus::Type PlayerStatus;

	/** Delegate to call when a sequence has finished playing */
	FOnSequenceFinishedPlaying OnSequenceFinishedPlayingEvent;

	/** The number of times to loop the animation playback */
	int32 NumLoopsToPlay;

	/** The number of loops completed since the last call to Play() */
	int32 NumLoopsCompleted;

	/** The speed at which the animation should be played */
	float PlaybackSpeed;

	/** The current playback mode. */
	EUMGSequencePlayMode::Type PlayMode;

	/**
	 * The 'state' tag the user may want to use to track what the animation is for.  It's very common in UI to use the
	 * same animation for intro / outro, so this allows you to tag what the animation is currently doing so that you can
	 * have some events just get called back when the animation finishes the outtro, to say, remove the UI then.
	 */
	FName UserTag;

	/** True if the animation is playing forward, otherwise false and it's playing in reverse. */
	bool bIsPlayingForward;

	/** Set to true while evaluating to prevent reentrancy */
	bool bIsEvaluating : 1;

	enum class ELatentAction
	{
		Stop, Pause
	};

	/** Set of latent actions that are to be performed when the sequence has finished evaluating this frame */
	TArray<ELatentAction> LatentActions;
};
