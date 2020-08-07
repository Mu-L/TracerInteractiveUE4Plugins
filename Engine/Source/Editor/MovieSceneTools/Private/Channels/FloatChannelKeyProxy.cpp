// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/FloatChannelKeyProxy.h"
#include "GenericPlatform/GenericPlatformMath.h"

void UFloatChannelKeyProxy::Initialize(FKeyHandle InKeyHandle, TMovieSceneChannelHandle<FMovieSceneFloatChannel> InChannelHandle, TWeakObjectPtr<UMovieSceneSection> InWeakSection)
{
	KeyHandle     = InKeyHandle;
	ChannelHandle = InChannelHandle;
	WeakSection   = InWeakSection;
}


void UFloatChannelKeyProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnProxyValueChanged(ChannelHandle, WeakSection.Get(), KeyHandle, Value, Time);
}

void UFloatChannelKeyProxy::UpdateValuesFromRawData()
{
	auto* Channel = ChannelHandle.Get();
	if (Channel)
	{
		auto ChannelData = Channel->GetData();
		int32 KeyIndex = ChannelData.GetIndex(KeyHandle);
		if (KeyIndex != INDEX_NONE && KeyIndex < FMath::Min(ChannelData.GetValues().Num(), ChannelData.GetTimes().Num()))
		{
			RefreshCurrentValue(ChannelHandle, KeyHandle, Value, Time);
		}
	}
}