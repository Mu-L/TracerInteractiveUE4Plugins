// Copyright Epic Games, Inc. All Rights Reserved.

#include "PaperRuntimeSettings.h"

//////////////////////////////////////////////////////////////////////////
// UPaperRuntimeSettings

UPaperRuntimeSettings::UPaperRuntimeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bEnableSpriteAtlasGroups(false)
	, bEnableTerrainSplineEditing(false)
	, bResizeSpriteDataToMatchTextures(true)
{
}
