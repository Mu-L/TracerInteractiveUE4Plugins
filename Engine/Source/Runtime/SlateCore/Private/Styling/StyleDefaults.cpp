// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/StyleDefaults.h"
#include "Fonts/LegacySlateFontInfoCache.h"

const FSlateFontInfo FStyleDefaults::GetFontInfo(uint16 Size)
{
	return FSlateFontInfo(FLegacySlateFontInfoCache::Get().GetDefaultFont(), Size, TEXT("Regular"));
}

float FStyleDefaults::DefaultFloat = 0.0f;
FVector2D FStyleDefaults::DefaultFVector2D = FVector2D( 66.0f, 66.0f );
FLinearColor FStyleDefaults::DefaultColor = FLinearColor( 1, 1, 1 );
FMargin FStyleDefaults::DefaultMargin = 0.0f;
FSlateSound FStyleDefaults::DefaultSound;
FSlateColor FStyleDefaults::DefaultSlateColor = FSlateColor::UseForeground();