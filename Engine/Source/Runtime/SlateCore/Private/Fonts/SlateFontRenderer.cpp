// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fonts/SlateFontRenderer.h"
#include "Fonts/FontCacheCompositeFont.h"
#include "Fonts/SlateTextShaper.h"
#include "Fonts/LegacySlateFontInfoCache.h"
#include "HAL/IConsoleManager.h"
#include "SlateGlobals.h"

DECLARE_CYCLE_STAT(TEXT("Freetype Render Glyph"), STAT_FreetypeRenderGlyph, STATGROUP_Slate);

/**
 * Method for rendering fonts with the possibility of an outline.
 * 0 = freetype does everything and generates a bitmap for the base glyph. 
 * 1 = We override the freetype rasterizer.  Can help with some rendering anomalies on complicated fonts when freetype generates a wildly different stroke from the base glyph
 * Note: The font cache must be flushed if changing this in the middle of a running instance
 */ 
static int32 OutlineFontRenderMethod = 0;
FAutoConsoleVariableRef CVarOutlineFontRenderMethod(
	TEXT("Slate.OutlineFontRenderMethod"),
	OutlineFontRenderMethod,
	TEXT("Changes the render method for outline fonts.  0 = freetype does everything and generates a bitmap for the base glyph (default).  1 = We override the freetype rasterizer.  Can help with some rendering anomalies on complicated fonts."));

/**
 * Enable or disable anti-aliasing for font rendering.
 * Note: The font cache must be flushed if changing this in the middle of a running instance
 */ 
static int32 EnableFontAntiAliasing = 1;
FAutoConsoleVariableRef CVarEnableFontAntiAliasing(
	TEXT("Slate.EnableFontAntiAliasing"),
	EnableFontAntiAliasing,
	TEXT("Enable or disable anti-aliasing for font rendering (0 = off, 1 = on). Enabled by default."));

namespace SlateFontRendererUtils
{

#if WITH_FREETYPE

void AppendGlyphFlags(const FFontData& InFontData, uint32& InOutGlyphFlags)
{
	// Setup additional glyph flags
	InOutGlyphFlags |= GlobalGlyphFlags;

	if(EnableFontAntiAliasing)
	{
		switch(InFontData.GetHinting())
		{
		case EFontHinting::Auto:		InOutGlyphFlags |= FT_LOAD_FORCE_AUTOHINT; break;
		case EFontHinting::AutoLight:	InOutGlyphFlags |= FT_LOAD_TARGET_LIGHT; break;
		case EFontHinting::Monochrome:	InOutGlyphFlags |= FT_LOAD_TARGET_MONO | FT_LOAD_FORCE_AUTOHINT; break;
		case EFontHinting::None:		InOutGlyphFlags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING; break;
		case EFontHinting::Default:
		default:						InOutGlyphFlags |= FT_LOAD_TARGET_NORMAL; break;
		}
	}
	else
	{
		InOutGlyphFlags |= FT_LOAD_TARGET_MONO | FT_LOAD_FORCE_AUTOHINT;
	}
}

#endif // WITH_FREETYPE

} // namespace SlateFontRendererUtils


FSlateFontRenderer::FSlateFontRenderer(const FFreeTypeLibrary* InFTLibrary, FFreeTypeGlyphCache* InFTGlyphCache, FFreeTypeKerningPairCache* InFTKerningPairCache, FCompositeFontCache* InCompositeFontCache)
	: FTLibrary(InFTLibrary)
	, FTGlyphCache(InFTGlyphCache)
	, FTKerningPairCache(InFTKerningPairCache)
	, CompositeFontCache(InCompositeFontCache)
{
	check(FTLibrary);
	check(FTGlyphCache);
	check(FTKerningPairCache);
	check(CompositeFontCache);
}

uint16 FSlateFontRenderer::GetMaxHeight(const FSlateFontInfo& InFontInfo, const float InScale) const
{
#if WITH_FREETYPE
	// Just get info for the null character 
	TCHAR Char = 0;
	const FFontData& FontData = CompositeFontCache->GetDefaultFontData(InFontInfo);
	const FFreeTypeFaceGlyphData FaceGlyphData = GetFontFaceForCharacter(FontData, Char, InFontInfo.FontFallback);

	if (FaceGlyphData.FaceAndMemory.IsValid())
	{
		FreeTypeUtils::ApplySizeAndScale(FaceGlyphData.FaceAndMemory->GetFace(), InFontInfo.Size, InScale);

		// Adjust the height by the size of the outline that was applied.  
		const float HeightAdjustment = InFontInfo.OutlineSettings.OutlineSize;
		return static_cast<uint16>((FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FaceGlyphData.FaceAndMemory->GetScaledHeight()) + HeightAdjustment) * InScale);
	}

	return 0;
#else
	return 0;
#endif // WITH_FREETYPE
}

int16 FSlateFontRenderer::GetBaseline(const FSlateFontInfo& InFontInfo, const float InScale) const
{
#if WITH_FREETYPE
	// Just get info for the null character 
	TCHAR Char = 0;
	const FFontData& FontData = CompositeFontCache->GetDefaultFontData(InFontInfo);
	const FFreeTypeFaceGlyphData FaceGlyphData = GetFontFaceForCharacter(FontData, Char, InFontInfo.FontFallback);

	if (FaceGlyphData.FaceAndMemory.IsValid())
	{
		FreeTypeUtils::ApplySizeAndScale(FaceGlyphData.FaceAndMemory->GetFace(), InFontInfo.Size, InScale);

		return static_cast<int16>((FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FaceGlyphData.FaceAndMemory->GetDescender())) * InScale);
	}

	return 0;
#else
	return 0;
#endif // WITH_FREETYPE
}

void FSlateFontRenderer::GetUnderlineMetrics(const FSlateFontInfo& InFontInfo, const float InScale, int16& OutUnderlinePos, int16& OutUnderlineThickness) const
{
#if WITH_FREETYPE
	const FFontData& FontData = CompositeFontCache->GetDefaultFontData(InFontInfo);
	FT_Face FontFace = GetFontFace(FontData);

	if (FontFace && FT_IS_SCALABLE(FontFace))
	{
		FreeTypeUtils::ApplySizeAndScale(FontFace, InFontInfo.Size, InScale);

		OutUnderlinePos = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FT_MulFix(FontFace->underline_position, FontFace->size->metrics.y_scale)) * InScale);
		OutUnderlineThickness = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FT_MulFix(FontFace->underline_thickness, FontFace->size->metrics.y_scale)) * InScale);
	}
	else
#endif // WITH_FREETYPE
	{
		OutUnderlinePos = 0;
		OutUnderlineThickness = 0;
	}
}

void FSlateFontRenderer::GetStrikeMetrics(const FSlateFontInfo& InFontInfo, const float InScale, int16& OutStrikeLinePos, int16& OutStrikeLineThickness) const
{
#if WITH_FREETYPE
	const FFontData& FontData = CompositeFontCache->GetDefaultFontData(InFontInfo);
	FT_Face FontFace = GetFontFace(FontData);

	if (FontFace && FT_IS_SCALABLE(FontFace))
	{
		FreeTypeUtils::ApplySizeAndScale(FontFace, InFontInfo.Size, InScale);

		// Place the strike 2/5th down the text by height (the code below does 3/5th as it counts from the bottom, not the top)
		OutStrikeLinePos = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FT_MulFix(FT_MulFix(FT_DivFix(FontFace->height, 5), 3), FontFace->size->metrics.y_scale)) * InScale);
		OutStrikeLineThickness = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(FT_MulFix(FontFace->underline_thickness, FontFace->size->metrics.y_scale)) * InScale);
	}
	else
#endif // WITH_FREETYPE
	{
		OutStrikeLinePos = 0;
		OutStrikeLineThickness = 0;
	}
}

bool FSlateFontRenderer::HasKerning(const FFontData& InFontData) const
{
#if WITH_FREETYPE
	FT_Face FontFace = GetFontFace(InFontData);

	if (!FontFace)
	{
		return false;
	}

	return FT_HAS_KERNING(FontFace) != 0;
#else
	return false;
#endif // WITH_FREETYPE
}

int8 FSlateFontRenderer::GetKerning(const FFontData& InFontData, const int32 InSize, TCHAR First, TCHAR Second, const float InScale) const
{
#if WITH_FREETYPE
	int8 Kerning = 0;

	FT_Face FontFace = GetFontFace(InFontData);

	// Check if this font has kerning as not all fonts do.
	// We also can't perform kerning between two separate font faces
	if (FontFace && FT_HAS_KERNING(FontFace))
	{
		FT_UInt FirstIndex = FT_Get_Char_Index(FontFace, First);
		FT_UInt SecondIndex = FT_Get_Char_Index(FontFace, Second);

		FT_Vector KerningVec;
		if (FTKerningPairCache->FindOrCache(FontFace, FFreeTypeKerningPairCache::FKerningPair(FirstIndex, SecondIndex), FT_KERNING_DEFAULT, InSize, InScale, KerningVec))
		{
			// Return pixel sizes
			Kerning = FreeTypeUtils::Convert26Dot6ToRoundedPixel<int8>(KerningVec.x);
		}
	}

	return Kerning;
#else
	return 0;
#endif // WITH_FREETYPE
}

bool FSlateFontRenderer::CanLoadCharacter(const FFontData& InFontData, TCHAR Char, EFontFallback MaxFallbackLevel) const
{
	bool bReturnVal = false;

#if WITH_FREETYPE
	const FFreeTypeFaceGlyphData FaceGlyphData = GetFontFaceForCharacter(InFontData, Char, MaxFallbackLevel);
	bReturnVal = FaceGlyphData.FaceAndMemory.IsValid() && FaceGlyphData.GlyphIndex != 0;
#endif // WITH_FREETYPE

	return bReturnVal;
}

#if WITH_FREETYPE

FFreeTypeFaceGlyphData FSlateFontRenderer::GetFontFaceForCharacter(const FFontData& InFontData, TCHAR Char, EFontFallback MaxFallbackLevel) const 
{
	FFreeTypeFaceGlyphData ReturnVal;
	const bool bOverrideFallback = Char == SlateFontRendererUtils::InvalidSubChar;

	// Try the requested font first
	{
		ReturnVal.FaceAndMemory = CompositeFontCache->GetFontFace(InFontData);

		if (ReturnVal.FaceAndMemory.IsValid())
		{
			ReturnVal.GlyphIndex = FT_Get_Char_Index(ReturnVal.FaceAndMemory->GetFace(), Char);
			ReturnVal.CharFallbackLevel = EFontFallback::FF_NoFallback;
		}
	}

	// If the requested glyph doesn't exist, use the localization fallback font
	if (!ReturnVal.FaceAndMemory.IsValid() || (Char != 0 && ReturnVal.GlyphIndex == 0))
	{
		const bool bCanFallback = bOverrideFallback || MaxFallbackLevel >= EFontFallback::FF_LocalizedFallback;

		if (bCanFallback && FLegacySlateFontInfoCache::Get().IsLocalizedFallbackFontAvailable())
		{
			ReturnVal.FaceAndMemory = CompositeFontCache->GetFontFace(FLegacySlateFontInfoCache::Get().GetLocalizedFallbackFontData(FLegacySlateFontInfoCache::FFallbackContext(&InFontData, Char)));

			if (ReturnVal.FaceAndMemory.IsValid())
			{	
				ReturnVal.GlyphIndex = FT_Get_Char_Index(ReturnVal.FaceAndMemory->GetFace(), Char);

				if (ReturnVal.GlyphIndex != 0)
				{
					ReturnVal.CharFallbackLevel = EFontFallback::FF_LocalizedFallback;
					ReturnVal.GlyphFlags |= FT_LOAD_FORCE_AUTOHINT;
				}
			}
		}
	}

	// If the requested glyph doesn't exist, use the last resort fallback font
	if (!ReturnVal.FaceAndMemory.IsValid() || (Char != 0 && ReturnVal.GlyphIndex == 0))
	{
		const bool bCanFallback = bOverrideFallback || MaxFallbackLevel >= EFontFallback::FF_LastResortFallback;

		if (bCanFallback && FLegacySlateFontInfoCache::Get().IsLastResortFontAvailable())
		{
			ReturnVal.FaceAndMemory = CompositeFontCache->GetFontFace(FLegacySlateFontInfoCache::Get().GetLastResortFontData(FLegacySlateFontInfoCache::FFallbackContext(&InFontData, Char)));

			if (ReturnVal.FaceAndMemory.IsValid())
			{
				ReturnVal.GlyphIndex = FT_Get_Char_Index(ReturnVal.FaceAndMemory->GetFace(), Char);

				if (ReturnVal.GlyphIndex != 0)
				{
					ReturnVal.CharFallbackLevel = EFontFallback::FF_LastResortFallback;
					ReturnVal.GlyphFlags |= FT_LOAD_FORCE_AUTOHINT;
				}
			}
		}
	}

	// Found an invalid glyph?
	if (Char != 0 && ReturnVal.GlyphIndex == 0)
	{
		ReturnVal.FaceAndMemory.Reset();
	}

	return ReturnVal;
}

#endif // WITH_FREETYPE

bool FSlateFontRenderer::GetRenderData(const FShapedGlyphEntry& InShapedGlyph, const FFontOutlineSettings& InOutlineSettings, FCharacterRenderData& OutRenderData) const
{
#if WITH_FREETYPE
	SCOPE_CYCLE_COUNTER(STAT_FreetypeRenderGlyph);

	FFreeTypeFaceGlyphData FaceGlyphData;
	FaceGlyphData.FaceAndMemory = InShapedGlyph.FontFaceData->FontFace.Pin();
	FaceGlyphData.GlyphIndex = InShapedGlyph.GlyphIndex;
	FaceGlyphData.GlyphFlags = InShapedGlyph.FontFaceData->GlyphFlags;

	if (FaceGlyphData.FaceAndMemory.IsValid())
	{
		check(FaceGlyphData.FaceAndMemory->IsValid());

		FT_Error Error = FreeTypeUtils::LoadGlyph(FaceGlyphData.FaceAndMemory->GetFace(), FaceGlyphData.GlyphIndex, FaceGlyphData.GlyphFlags, InShapedGlyph.FontFaceData->FontSize, InShapedGlyph.FontFaceData->FontScale);
		check(Error == 0);

		OutRenderData.Char = 0;
		return GetRenderDataInternal(FaceGlyphData, InShapedGlyph.FontFaceData->FontScale, InOutlineSettings, OutRenderData);
	}
#endif // WITH_FREETYPE
	return false;
}

#if WITH_FREETYPE

/**
 * Represents one or more pixels of a rasterized glyph that have the same coverage (filled amount)
 */
struct FRasterizerSpan
{
	/** Start x location of the span */
	int32 X;
	/** Start y location of the span  */
	int32 Y;
	
	/** Length of the span */
	int32 Width;
	
	/** How "filled" the span is where 0 is completely transparent and 255 is completely opaque */
	uint8 Coverage;

	FRasterizerSpan(int32 InX, int32 InY, int32 InWidth, uint8 InCoverage)
		: X(InX)
		, Y(InY)
		, Width(InWidth)
		, Coverage(InCoverage)
	{}
};

/**
 * Represents a single rasterized glyph
 */
struct FRasterizerSpanList
{
	/** List of spans in the glyph */
	TArray<FRasterizerSpan> Spans;
	/** Bounds around the glyph */
	FBox2D BoundingBox;

	FRasterizerSpanList()
		: BoundingBox(ForceInit)
	{}
};


/**
 * Rasterizes a font glyph
 */
void RenderOutlineRows(FT_Library Library, FT_Outline* Outline, FRasterizerSpanList& OutSpansList)
{
	auto RasterizerCallback = [](const int32 Y, const int32 Count, const FT_Span* const Spans, void* const UserData)
	{
		FRasterizerSpanList& UserDataSpanList = *static_cast<FRasterizerSpanList*>(UserData);

		TArray<FRasterizerSpan>& UserDataSpans = UserDataSpanList.Spans;
		FBox2D& BoundingBox = UserDataSpanList.BoundingBox;

		UserDataSpans.Reserve(UserDataSpans.Num() + Count);
		for(int32 SpanIndex = 0; SpanIndex < Count; ++SpanIndex)
		{
			const FT_Span& Span = Spans[SpanIndex];

			BoundingBox += FVector2D(Span.x, Y);
			BoundingBox += FVector2D(Span.x + Span.len - 1, Y);

			UserDataSpans.Add(FRasterizerSpan(Span.x, Y, Span.len, Span.coverage));
		}
	};

	FT_Raster_Params RasterParams;
	FMemory::Memzero<FT_Raster_Params>(RasterParams);
	RasterParams.flags = FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT;
	RasterParams.gray_spans = RasterizerCallback;
	RasterParams.user = &OutSpansList;

	FT_Outline_Render(Library, Outline, &RasterParams);

}

bool FSlateFontRenderer::GetRenderDataInternal(const FFreeTypeFaceGlyphData& InFaceGlyphData, const float InScale, const FFontOutlineSettings& InOutlineSettings, FCharacterRenderData& OutRenderData) const
{
	SCOPE_CYCLE_COUNTER(STAT_FreetypeRenderGlyph);
	FT_Face Face = InFaceGlyphData.FaceAndMemory->GetFace();

	// Get the lot for the glyph.  This contains measurement info
	FT_GlyphSlot Slot = Face->glyph;

	float ScaledOutlineSize = FMath::RoundToFloat(InOutlineSettings.OutlineSize * InScale);

	if((ScaledOutlineSize > 0 || OutlineFontRenderMethod == 1) && Slot->format == FT_GLYPH_FORMAT_OUTLINE)
	{
		// Render the filled area first
		FRasterizerSpanList FillSpans;
		RenderOutlineRows(FTLibrary->GetLibrary(), &Slot->outline, FillSpans);

		FRasterizerSpanList OutlineSpans;
		
		FT_Stroker Stroker = nullptr;
		FT_Glyph Glyph = nullptr;

		// If there is an outline, render it second after applying a border stroke to the font to produce an outline
		if(ScaledOutlineSize > 0)
		{
			FT_Stroker_New(FTLibrary->GetLibrary(), &Stroker);
			FT_Stroker_Set(Stroker, FMath::TruncToInt(FreeTypeUtils::ConvertPixelTo26Dot6<float>(ScaledOutlineSize)), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);

			FT_Get_Glyph(Slot, &Glyph);

			FT_Bool bInner = false;
			FT_Glyph_StrokeBorder(&Glyph, Stroker, bInner, 0);

			FT_Outline* Outline = &reinterpret_cast<FT_OutlineGlyph>(Glyph)->outline;

			RenderOutlineRows(FTLibrary->GetLibrary(), Outline, OutlineSpans);
		}

		const FBox2D BoundingBox = FillSpans.BoundingBox + OutlineSpans.BoundingBox;

		FVector2D Size = BoundingBox.GetSize();

		//Note: we add 1 to width and height because the size of the rect is inclusive
		int32 Width = FMath::TruncToInt(Size.X)+1;
		int32 Height = FMath::TruncToInt(Size.Y)+1;

		OutRenderData.RawPixels.Reset();

		OutRenderData.MeasureInfo.SizeX = Width;
		OutRenderData.MeasureInfo.SizeY = Height;

		OutRenderData.RawPixels.AddZeroed(Width*Height);

		int32 XMin = BoundingBox.Min.X;
		int32 YMin = BoundingBox.Min.Y;

		// Compute and copy the pixels for the total filled area of the glyph. 

		// Copy the outline area first
		const TArray<FRasterizerSpan>& FirstSpanList = OutlineSpans.Spans;
		{
			for(const FRasterizerSpan& Span : FirstSpanList)
			{
				for(int32 W = 0; W < Span.Width; ++W)
				{
					OutRenderData.RawPixels[((int32)((Height - 1 - (Span.Y - YMin)) * Width + Span.X - XMin + W))] = Span.Coverage;

				}
			}
		}

		// If there is an outline, it was rasterized by freetype with the filled area included
		// This code will eliminate the filled area if the user requests an outline with separate translucency for the fill area
		const TArray<FRasterizerSpan>& SecondSpanList = FillSpans.Spans;
		{
			if(ScaledOutlineSize > 0)
			{
				for (const FRasterizerSpan& Span : SecondSpanList)
				{
					for (int32 W = 0; W < Span.Width; ++W)
					{
						uint8& Src = OutRenderData.RawPixels[((int32)((Height - 1 - (Span.Y - YMin)) * Width + Span.X - XMin + W))];

						if(InOutlineSettings.bSeparateFillAlpha)
						{
							// this method is better for transparent fill areas
							Src = Span.Coverage ? FMath::Abs(Src - Span.Coverage) : 0;
						}
						else
						{
							// This method is better for opaque fill areas 
							Src = Span.Coverage == 255 ? Span.Coverage : Src;
						}
					}
				}
			}
			else
			{
				for (const FRasterizerSpan& Span : SecondSpanList)
				{
					for (int32 w = 0; w < Span.Width; ++w)
					{
						OutRenderData.RawPixels[((int32)((Height - 1 - (Span.Y - YMin)) * Width + Span.X - XMin + w))] = Span.Coverage;
					}
				}
			}
		}

		FT_Stroker_Done(Stroker);
		FT_Done_Glyph(Glyph);

		// Note: in order to render the stroke properly AND to get proper measurements this must be done after rendering the stroke
		FT_Render_Glyph(Slot, EnableFontAntiAliasing ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);
	}
	else
	{
		// This path renders a standard font with no outline.  This may occur if the outline failed to generate
		FT_Render_Glyph(Slot, EnableFontAntiAliasing ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);

		FT_Bitmap* Bitmap = nullptr;
		FT_Bitmap TmpBitmap;
		if(Slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
		{
			// Convert the bitmap to 8bpp grayscale
			FT_Bitmap_New(&TmpBitmap);
			FT_Bitmap_Convert(FTLibrary->GetLibrary(), &Slot->bitmap, &TmpBitmap, 4);
			Bitmap = &TmpBitmap;
		}
		else
		{
			Bitmap = &Slot->bitmap;
		}
		check(Bitmap && Bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);

		OutRenderData.RawPixels.Reset();
		OutRenderData.RawPixels.AddUninitialized(Bitmap->rows * Bitmap->width);

		// Nothing to do for zero width or height glyphs
		if(OutRenderData.RawPixels.Num())
		{
			// Copy the rendered bitmap to our raw pixels array
			// This code assumes we're dealing with an 8bpp grayscale image (as asserted above)
			for(uint32 Row = 0; Row < (uint32)Bitmap->rows; ++Row)
			{
				// Copy a single row. Note Bitmap.pitch contains the offset (in bytes) between rows.  Not always equal to Bitmap.width!
				FMemory::Memcpy(&OutRenderData.RawPixels[Row*Bitmap->width], &Bitmap->buffer[Row*Bitmap->pitch], Bitmap->width);
			}

			// Grayscale images not using 256 colors need to convert their gray range to a 0-255 range
			if(Bitmap->num_grays != 256)
			{
				const int32 GrayBoost = 255 / (Bitmap->num_grays - 1);
				for(uint8& RawPixel : OutRenderData.RawPixels)
				{
					RawPixel *= GrayBoost;
				}
			}
		}

		OutRenderData.MeasureInfo.SizeX = Bitmap->width;
		OutRenderData.MeasureInfo.SizeY = Bitmap->rows;

		if(Bitmap == &TmpBitmap)
		{
			FT_Bitmap_Done(FTLibrary->GetLibrary(), Bitmap);
			Bitmap = nullptr;
		}

		// Reset the outline to zero.  If we are in this path, either the outline failed to generate because the font supported or there is no outline
		// We do not want to take it into account if it failed to generate
		ScaledOutlineSize = 0;
	}

	// Set measurement info for this character
	OutRenderData.GlyphIndex = InFaceGlyphData.GlyphIndex;
	OutRenderData.HasKerning = FT_HAS_KERNING(Face) != 0;

	OutRenderData.MaxHeight = static_cast<int32>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(InFaceGlyphData.FaceAndMemory->GetScaledHeight()) * InScale);
	OutRenderData.MeasureInfo.GlobalAscender = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(InFaceGlyphData.FaceAndMemory->GetAscender()) * InScale);
	OutRenderData.MeasureInfo.GlobalDescender = static_cast<int16>(FreeTypeUtils::Convert26Dot6ToRoundedPixel<int32>(InFaceGlyphData.FaceAndMemory->GetDescender()) * InScale);
	// Note we use Slot->advance instead of Slot->metrics.horiAdvance because Slot->Advance contains transformed position (needed if we scale)
	OutRenderData.MeasureInfo.XAdvance = FreeTypeUtils::Convert26Dot6ToRoundedPixel<int16>(Slot->advance.x);
	OutRenderData.MeasureInfo.HorizontalOffset = Slot->bitmap_left;
	OutRenderData.MeasureInfo.VerticalOffset = Slot->bitmap_top + ScaledOutlineSize;

	return true;
}

FT_Face FSlateFontRenderer::GetFontFace(const FFontData& InFontData) const
{
	TSharedPtr<FFreeTypeFace> FaceAndMemory = CompositeFontCache->GetFontFace(InFontData);
	return (FaceAndMemory.IsValid()) ? FaceAndMemory->GetFace() : nullptr;
}

#endif // WITH_FREETYPE
