// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"

struct FSlateDynamicImageBrush;

/**
 * Core slate style
 */
class SLATECORE_API FCoreStyle 
{
public:

	static TSharedRef<class ISlateStyle> Create( const FName& InStyleSetName = "CoreStyle" );

	/** @return the singleton instance. */
	static const ISlateStyle& Get( )
	{
		return *(Instance.Get());
	}

	/** Get the default font for Slate */
	static TSharedRef<const FCompositeFont> GetDefaultFont();

	/** Get a font style using the default for for Slate */
	static FSlateFontInfo GetDefaultFontStyle(const FName InTypefaceFontName, const int32 InSize, const FFontOutlineSettings& InOutlineSettings = FFontOutlineSettings());

	static void ResetToDefault( );

	/** Used to override the default selection colors */
	static void SetSelectorColor( const FLinearColor& NewColor );
	static void SetSelectionColor( const FLinearColor& NewColor );
	static void SetInactiveSelectionColor( const FLinearColor& NewColor );
	static void SetPressedSelectionColor( const FLinearColor& NewColor );
	static void SetFocusBrush(FSlateBrush* NewBrush);

	// todo: jdale - These are only here because of UTouchInterface::Activate and the fact that GetDynamicImageBrush is non-const
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, FName TextureName, const ANSICHAR* Specifier = nullptr );
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, const ANSICHAR* Specifier, class UTexture2D* TextureResource, FName TextureName );
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, class UTexture2D* TextureResource, FName TextureName );

	static const int32 RegularTextSize = 9;
	static const int32 SmallTextSize = 8;

private:

	static void SetStyle( const TSharedRef< class ISlateStyle >& NewStyle );

private:

	/** Singleton instances of this style. */
	static TSharedPtr< class ISlateStyle > Instance;
};
