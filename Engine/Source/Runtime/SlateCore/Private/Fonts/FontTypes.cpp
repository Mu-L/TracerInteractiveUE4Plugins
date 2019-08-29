// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fonts/FontTypes.h"


FSlateFontAtlas::FSlateFontAtlas( uint32 InWidth, uint32 InHeight )
	: FSlateTextureAtlas( InWidth, InHeight, sizeof(uint8), ESlateTextureAtlasPaddingStyle::PadWithZero, true )
{
}

FSlateFontAtlas::~FSlateFontAtlas()
{

}	

/** 
 * Adds a character to the texture.
 *
 * @param CharInfo	Information about the size of the character
 */
const FAtlasedTextureSlot* FSlateFontAtlas::AddCharacter( const FCharacterRenderData& RenderData )
{
	return AddTexture( RenderData.MeasureInfo.SizeX, RenderData.MeasureInfo.SizeY, RenderData.RawPixels );
}

void FSlateFontAtlas::Flush()
{
	// Empty the atlas
	EmptyAtlasData();

	// Recreate the data
	InitAtlasData();

	bNeedsUpdate = true;
	ConditionalUpdateTexture();
}
