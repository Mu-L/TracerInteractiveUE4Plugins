// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Text/STextBlock.h"
#include "SlateGlobals.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Widgets/Text/SlateTextBlockLayout.h"
#include "Types/ReflectionMetadata.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#if WITH_ACCESSIBILITY
#include "Widgets/Accessibility/SlateAccessibleWidgets.h"
#endif

DECLARE_CYCLE_STAT(TEXT("STextBlock::SetText Time"), Stat_SlateTextBlockSetText, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::OnPaint Time"), Stat_SlateTextBlockOnPaint, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::ComputeDesiredSize"), Stat_SlateTextBlockCDS, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::ComputeVolitility"), Stat_SlateTextBlockCV, STATGROUP_SlateVerbose)

STextBlock::STextBlock()
{
	SetCanTick(false);
	bCanSupportFocus = false;
	bSimpleTextMode = false;

#if WITH_ACCESSIBILITY
	AccessibleBehavior = EAccessibleBehavior::Auto;
	bCanChildrenBeAccessible = false;
#endif
}

STextBlock::~STextBlock()
{
	// Needed to avoid "deletion of pointer to incomplete type 'FSlateTextBlockLayout'; no destructor called" error when using TUniquePtr
}

void STextBlock::Construct( const FArguments& InArgs )
{
	TextStyle = *InArgs._TextStyle;

	HighlightText = InArgs._HighlightText;
	WrapTextAt = InArgs._WrapTextAt;
	AutoWrapText = InArgs._AutoWrapText;
	WrappingPolicy = InArgs._WrappingPolicy;
	Margin = InArgs._Margin;
	LineHeightPercentage = InArgs._LineHeightPercentage;
	Justification = InArgs._Justification;
	MinDesiredWidth = InArgs._MinDesiredWidth;

	Font = InArgs._Font;
	StrikeBrush = InArgs._StrikeBrush;
	ColorAndOpacity = InArgs._ColorAndOpacity;
	ShadowOffset = InArgs._ShadowOffset;
	ShadowColorAndOpacity = InArgs._ShadowColorAndOpacity;
	HighlightColor = InArgs._HighlightColor;
	HighlightShape = InArgs._HighlightShape;

	bSimpleTextMode = InArgs._SimpleTextMode;

	SetOnMouseDoubleClick(InArgs._OnDoubleClicked);

	BoundText = InArgs._Text;

	//if(!bSimpleTextMode)
	{
		// We use a dummy style here (as it may not be safe to call the delegates used to compute the style), but the correct style is set by ComputeDesiredSize
		TextLayoutCache = MakeUnique<FSlateTextBlockLayout>(this, FTextBlockStyle::GetDefault(), InArgs._TextShapingMethod, InArgs._TextFlowDirection, FCreateSlateTextLayout(), FPlainTextLayoutMarshaller::Create(), InArgs._LineBreakPolicy);
		TextLayoutCache->SetDebugSourceInfo(TAttribute<FString>::Create(TAttribute<FString>::FGetter::CreateLambda([this] { return FReflectionMetaData::GetWidgetDebugInfo(this); })));
	}
}

FSlateFontInfo STextBlock::GetFont() const
{
	return Font.IsSet() ? Font.Get() : TextStyle.Font;
}

const FSlateBrush* STextBlock::GetStrikeBrush() const
{
	return StrikeBrush.IsSet() ? StrikeBrush.Get() : &TextStyle.StrikeBrush;
}

FSlateColor STextBlock::GetColorAndOpacity() const
{
	return ColorAndOpacity.IsSet() ? ColorAndOpacity.Get() : TextStyle.ColorAndOpacity;
}

FVector2D STextBlock::GetShadowOffset() const
{
	return ShadowOffset.IsSet() ? ShadowOffset.Get() : TextStyle.ShadowOffset;
}

FLinearColor STextBlock::GetShadowColorAndOpacity() const
{
	return ShadowColorAndOpacity.IsSet() ? ShadowColorAndOpacity.Get() : TextStyle.ShadowColorAndOpacity;
}

FLinearColor STextBlock::GetHighlightColor() const
{
	return HighlightColor.IsSet() ? HighlightColor.Get() : TextStyle.HighlightColor;
}

const FSlateBrush* STextBlock::GetHighlightShape() const
{
	return HighlightShape.IsSet() ? HighlightShape.Get() : &TextStyle.HighlightShape;
}

void STextBlock::InvalidateText(EInvalidateWidget InvalidateReason)
{
	if (bSimpleTextMode && EnumHasAnyFlags(InvalidateReason, EInvalidateWidget::Layout))
	{
		CachedSimpleDesiredSize.Reset();
	}

	Invalidate(InvalidateReason);
}

void STextBlock::SetText( const TAttribute< FString >& InText )
{
	if (InText.IsSet() && !InText.IsBound())
	{
		SetText(FText::AsCultureInvariant(InText.Get()));
		return;
	}

	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockSetText);
	BoundText = MakeAttributeLambda([InText]()
	{
		return FText::AsCultureInvariant(InText.Get(FString()));
	});
	InvalidateText(EInvalidateWidget::LayoutAndVolatility);
}

void STextBlock::SetText( const FString& InText )
{
	SetText(FText::AsCultureInvariant(InText));
}

void STextBlock::SetText( const TAttribute< FText >& InText )
{
	if (InText.IsSet() && !InText.IsBound())
	{
		SetText(InText.Get());
		return;
	}

	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockSetText);
	BoundText = InText;
	InvalidateText(EInvalidateWidget::LayoutAndVolatility);
}

void STextBlock::SetText( const FText& InText )
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockSetText);

	if ( !BoundText.IsBound() )
	{
		const FString& OldString = BoundText.Get().ToString();
		const int32 OldLength = OldString.Len();

		// Only compare reasonably sized strings, it's not worth checking this
		// for large blocks of text.
		if ( OldLength <= 20 )
		{
			const FString& NewString = InText.ToString();
			if ( OldString.Compare(NewString, ESearchCase::CaseSensitive) == 0 )
			{
				return;
			}
		}
	}

	BoundText = InText;
	InvalidateText(EInvalidateWidget::LayoutAndVolatility);
}

void STextBlock::SetHighlightText(TAttribute<FText> InText)
{
	HighlightText = InText;
}

int32 STextBlock::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockOnPaint);

	if (bSimpleTextMode)
	{
		// Draw the optional shadow
		const FLinearColor LocalShadowColorAndOpacity = GetShadowColorAndOpacity();
		const FVector2D LocalShadowOffset = GetShadowOffset();
		const bool ShouldDropShadow = LocalShadowColorAndOpacity.A > 0.f && LocalShadowOffset.SizeSquared() > 0.f;

		const bool bShouldBeEnabled = ShouldBeEnabled(bParentEnabled);

		const FText& LocalText = GetText();
		FSlateFontInfo LocalFont = GetFont();

		if (ShouldDropShadow)
		{
			const int32 OutlineSize = LocalFont.OutlineSettings.OutlineSize;
			if (!LocalFont.OutlineSettings.bApplyOutlineToDropShadows)
			{
				LocalFont.OutlineSettings.OutlineSize = 0;
			}

			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToOffsetPaintGeometry(LocalShadowOffset),
				LocalText,
				LocalFont,
				bShouldBeEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				InWidgetStyle.GetColorAndOpacityTint() * LocalShadowColorAndOpacity
			);

			// Restore outline size for main text
			LocalFont.OutlineSettings.OutlineSize = OutlineSize;

			// actual text should appear above the shadow
			++LayerId;
		}

		// Draw the text itself
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LocalText,
			LocalFont,
			bShouldBeEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
			InWidgetStyle.GetColorAndOpacityTint() * GetColorAndOpacity().GetColor(InWidgetStyle)
			);
	}
	else
	{
		const FVector2D LastDesiredSize = TextLayoutCache->GetDesiredSize();

		// OnPaint will also update the text layout cache if required
		LayerId = TextLayoutCache->OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));

		const FVector2D NewDesiredSize = TextLayoutCache->GetDesiredSize();

		// HACK: Due to the nature of wrapping and layout, we may have been arranged in a different box than what we were cached with.  Which
		// might update wrapping, so make sure we always set the desired size to the current size of the text layout, which may have changed
		// during paint.
		bool bCanWrap = WrapTextAt.Get() > 0 || AutoWrapText.Get();

		if (bCanWrap && !NewDesiredSize.Equals(LastDesiredSize))
		{
			const_cast<STextBlock*>(this)->Invalidate(EInvalidateWidget::Layout);
		}
	}

	return LayerId;
}

FVector2D STextBlock::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockCDS);

	if (bSimpleTextMode)
	{
		const FVector2D LocalShadowOffset = GetShadowOffset();

		const float LocalOutlineSize = GetFont().OutlineSettings.OutlineSize;

		// Account for the outline width impacting both size of the text by multiplying by 2
		// Outline size in Y is accounted for in MaxHeight calculation in Measure()
		const FVector2D ComputedOutlineSize(LocalOutlineSize * 2, LocalOutlineSize);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(GetText(), GetFont()) + ComputedOutlineSize + LocalShadowOffset;

		CachedSimpleDesiredSize = FVector2D(FMath::Max(MinDesiredWidth.Get(0.0f), TextSize.X), TextSize.Y);
		return CachedSimpleDesiredSize.GetValue();
	}
	else
	{
		// ComputeDesiredSize will also update the text layout cache if required
		const FVector2D TextSize = TextLayoutCache->ComputeDesiredSize(
			FSlateTextBlockLayout::FWidgetArgs(BoundText, HighlightText, WrapTextAt, AutoWrapText, WrappingPolicy, Margin, LineHeightPercentage, Justification),
			LayoutScaleMultiplier, GetComputedTextStyle()
		);

		return FVector2D(FMath::Max(MinDesiredWidth.Get(0.0f), TextSize.X), TextSize.Y);
	}
}

bool STextBlock::ComputeVolatility() const
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockCV);
	return SLeafWidget::ComputeVolatility() 
		|| BoundText.IsBound()
		|| Font.IsBound()
		|| ColorAndOpacity.IsBound()
		|| ShadowOffset.IsBound()
		|| ShadowColorAndOpacity.IsBound()
		|| HighlightColor.IsBound()
		|| HighlightShape.IsBound()
		|| HighlightText.IsBound()
		|| WrapTextAt.IsBound()
		|| AutoWrapText.IsBound()
		|| WrappingPolicy.IsBound()
		|| Margin.IsBound()
		|| Justification.IsBound()
		|| LineHeightPercentage.IsBound()
		|| MinDesiredWidth.IsBound();
}

void STextBlock::SetFont(const TAttribute< FSlateFontInfo >& InFont)
{
	if(!Font.IsSet() || !Font.IdenticalTo(InFont))
	{
		Font = InFont;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetStrikeBrush(const TAttribute<const FSlateBrush*>& InStrikeBrush)
{
	if (!StrikeBrush.IsSet() || !StrikeBrush.IdenticalTo(InStrikeBrush))
	{
		StrikeBrush = InStrikeBrush;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetColorAndOpacity(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	if ( !ColorAndOpacity.IsSet() || !ColorAndOpacity.IdenticalTo(InColorAndOpacity) )
	{
		ColorAndOpacity = InColorAndOpacity;
		// HACK: Normally this would be Paint only, but textblocks need to recache layout.
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetTextStyle(const FTextBlockStyle* InTextStyle)
{
	if (InTextStyle)
	{
		TextStyle = *InTextStyle;
	}
	else
	{
		FArguments Defaults;
		TextStyle = *Defaults._TextStyle;
	}

	InvalidateText(EInvalidateWidget::Layout);
}

void STextBlock::SetTextShapingMethod(const TOptional<ETextShapingMethod>& InTextShapingMethod)
{
	if (!bSimpleTextMode)
	{
		TextLayoutCache->SetTextShapingMethod(InTextShapingMethod);
		InvalidateText(EInvalidateWidget::Layout);
	}
}

void STextBlock::SetTextFlowDirection(const TOptional<ETextFlowDirection>& InTextFlowDirection)
{
	if(!bSimpleTextMode)
	{
		TextLayoutCache->SetTextFlowDirection(InTextFlowDirection);
		InvalidateText(EInvalidateWidget::Layout);
	}
}

void STextBlock::SetWrapTextAt(const TAttribute<float>& InWrapTextAt)
{
	if(!WrapTextAt.IdenticalTo(InWrapTextAt))
	{
		WrapTextAt = InWrapTextAt;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetAutoWrapText(const TAttribute<bool>& InAutoWrapText)
{
	if(!AutoWrapText.IdenticalTo(InAutoWrapText))
	{
		AutoWrapText = InAutoWrapText;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetWrappingPolicy(const TAttribute<ETextWrappingPolicy>& InWrappingPolicy)
{
	if(!WrappingPolicy.IdenticalTo(InWrappingPolicy))
	{
		WrappingPolicy = InWrappingPolicy;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetShadowOffset(const TAttribute<FVector2D>& InShadowOffset)
{
	if(!ShadowOffset.IdenticalTo(InShadowOffset))
	{
		ShadowOffset = InShadowOffset;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetShadowColorAndOpacity(const TAttribute<FLinearColor>& InShadowColorAndOpacity)
{
	if(!ShadowColorAndOpacity.IdenticalTo(InShadowColorAndOpacity))
	{
		ShadowColorAndOpacity = InShadowColorAndOpacity;
		// HACK: Normally this would be Paint only, but textblocks need to recache layout.
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetMinDesiredWidth(const TAttribute<float>& InMinDesiredWidth)
{
	if(!MinDesiredWidth.IdenticalTo(InMinDesiredWidth))
	{
		MinDesiredWidth = InMinDesiredWidth;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetLineHeightPercentage(const TAttribute<float>& InLineHeightPercentage)
{
	if(!LineHeightPercentage.IdenticalTo(InLineHeightPercentage))
	{
		LineHeightPercentage = InLineHeightPercentage;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetMargin(const TAttribute<FMargin>& InMargin)
{
	if(!Margin.IdenticalTo(InMargin))
	{
		Margin = InMargin;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetJustification(const TAttribute<ETextJustify::Type>& InJustification)
{
	if(!Justification.IdenticalTo(InJustification))
	{
		Justification = InJustification;
		InvalidateText(EInvalidateWidget::LayoutAndVolatility);
	}
}

FTextBlockStyle STextBlock::GetComputedTextStyle() const
{
	FTextBlockStyle ComputedStyle = TextStyle;
	ComputedStyle.SetFont( GetFont() );
	if (StrikeBrush.IsSet())
	{
		const FSlateBrush* const ComputedStrikeBrush = StrikeBrush.Get();
		if (ComputedStrikeBrush)
		{
			ComputedStyle.SetStrikeBrush(*ComputedStrikeBrush);
		}
	}
	ComputedStyle.SetColorAndOpacity( GetColorAndOpacity() );
	ComputedStyle.SetShadowOffset( GetShadowOffset() );
	ComputedStyle.SetShadowColorAndOpacity( GetShadowColorAndOpacity() );
	ComputedStyle.SetHighlightColor( GetHighlightColor() );
	ComputedStyle.SetHighlightShape( *GetHighlightShape() );
	return ComputedStyle;
}

#if WITH_ACCESSIBILITY
TSharedRef<FSlateAccessibleWidget> STextBlock::CreateAccessibleWidget()
{
	return MakeShareable<FSlateAccessibleWidget>(new FSlateAccessibleTextBlock(SharedThis(this)));
}

TOptional<FText> STextBlock::GetDefaultAccessibleText(EAccessibleType AccessibleType) const
{
	return GetText();
}
#endif
