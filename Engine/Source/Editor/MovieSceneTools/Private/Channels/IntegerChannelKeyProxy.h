// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "CurveEditorKeyProxy.h"
#include "MovieSceneKeyProxy.h"
#include "Channels/MovieSceneIntegerChannel.h"

#include "IntegerChannelKeyProxy.generated.h"

class UMovieSceneSection;

UCLASS()
class UIntegerChannelKeyProxy : public UObject, public ICurveEditorKeyProxy, public IMovieSceneKeyProxy
{
public:
	GENERATED_BODY()

	/**
	 * Initialize this key proxy object by caching the underlying key object, and retrieving the time/value each tick
	 */
	void Initialize(FKeyHandle InKeyHandle, TMovieSceneChannelHandle<FMovieSceneIntegerChannel> InChannelHandle, TWeakObjectPtr<UMovieSceneSection> InWeakSection);

private:

	/** Apply this class's properties to the underlying key */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Update this class's properties from the underlying key */
	virtual void UpdateValuesFromRawData() override;

private:

	/** User-facing time of the key, applied to the actual key on PostEditChange, and updated every tick */
	UPROPERTY(EditAnywhere, Category="Key")
	FFrameNumber Time;

	/** User-facing value of the key, applied to the actual key on PostEditChange, and updated every tick */
	UPROPERTY(EditAnywhere, Category="Key", meta=(ShowOnlyInnerProperties))
	int32 Value;

private:

	/** Cached key handle that this key proxy relates to */
	FKeyHandle KeyHandle;
	/** Cached channel in which the key resides */
	TMovieSceneChannelHandle<FMovieSceneIntegerChannel> ChannelHandle;
	/** Cached section in which the channel resides */
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
};