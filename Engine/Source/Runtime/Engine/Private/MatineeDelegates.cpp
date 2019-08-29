// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MatineeDelegates.h"

FMatineeDelegates& FMatineeDelegates::Get()
{
	// return the singleton object
	static FMatineeDelegates Singleton;
	return Singleton;
}
