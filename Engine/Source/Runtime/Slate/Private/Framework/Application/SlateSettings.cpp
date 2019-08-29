// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SlateSettings.cpp: Project configurable Slate settings
=============================================================================*/

#include "SlateSettings.h"

USlateSettings::USlateSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bExplicitCanvasChildZOrder(false)
{
}
