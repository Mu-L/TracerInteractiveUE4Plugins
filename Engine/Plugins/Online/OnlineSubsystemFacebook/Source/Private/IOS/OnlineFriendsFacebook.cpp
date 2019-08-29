// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "OnlineFriendsFacebook.h"
#include "OnlineSubsystemFacebookPrivate.h"

FOnlineFriendsFacebook::FOnlineFriendsFacebook(FOnlineSubsystemFacebook* InSubsystem) 
	: FOnlineFriendsFacebookCommon(InSubsystem)
{
}

FOnlineFriendsFacebook::FOnlineFriendsFacebook()
	: FOnlineFriendsFacebookCommon(nullptr)
{
}

FOnlineFriendsFacebook::~FOnlineFriendsFacebook()
{
}
