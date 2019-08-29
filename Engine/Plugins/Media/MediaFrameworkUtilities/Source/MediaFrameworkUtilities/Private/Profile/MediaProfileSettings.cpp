// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Profile/MediaProfileSettings.h"

#include "MediaAssets/ProxyMediaSource.h"
#include "MediaAssets/ProxyMediaOutput.h"
#include "Profile/MediaProfile.h"


TArray<UProxyMediaSource*> UMediaProfileSettings::GetAllMediaSourceProxy() const
{
	TArray<UProxyMediaSource*> Result;
	Result.Reset(MediaSourceProxy.Num());

	for (auto& Proxy : MediaSourceProxy)
	{
		Result.Add(Proxy.LoadSynchronous());
	}
	return Result;
}


TArray<UProxyMediaOutput*> UMediaProfileSettings::GetAllMediaOutputProxy() const
{
	TArray<UProxyMediaOutput*> Result;
	Result.Reset(MediaOutputProxy.Num());

	for (auto& Proxy : MediaOutputProxy)
	{
		Result.Add(Proxy.LoadSynchronous());
	}
	return Result;
}


UMediaProfile* UMediaProfileSettings::GetStartupMediaProfile() const
{
	return StartupMediaProfile.LoadSynchronous();
}


UMediaProfileEditorSettings::UMediaProfileEditorSettings()
	:bDisplayInToolbar(true)
{
}


UMediaProfile* UMediaProfileEditorSettings::GetUserMediaProfile() const
{
	return UserMediaProfile.LoadSynchronous();
}


void UMediaProfileEditorSettings::SetUserMediaProfile(UMediaProfile* InMediaProfile)
{
	UserMediaProfile = InMediaProfile;
#if WITH_EDITOR
	SaveConfig();
#endif
}
