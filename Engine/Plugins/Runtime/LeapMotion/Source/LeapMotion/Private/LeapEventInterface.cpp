// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "LeapEventInterface.h"


ULeapEventInterface::ULeapEventInterface(const class FObjectInitializer& Init)
	: Super(Init)
{

}

FString ILeapEventInterface::ToString()
{
	return "ILeapEventInterface::ToString()";
}