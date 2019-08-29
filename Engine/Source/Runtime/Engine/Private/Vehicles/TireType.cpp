// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Vehicles/TireType.h"
#include "EngineDefines.h"

UTireType::UTireType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Property initialization
	FrictionScale = 1.0f;
}

