// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamerDelegates.h"

UPixelStreamerDelegates* UPixelStreamerDelegates::Singleton = nullptr;

void UPixelStreamerDelegates::CreateInstance()
{
	Singleton = NewObject<UPixelStreamerDelegates>();
	Singleton->AddToRoot();
}
