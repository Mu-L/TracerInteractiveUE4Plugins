// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Text/STextBlock.h"
#include "SlateGlobals.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Widgets/Text/SlateTextBlockLayout.h"
#include "Types/ReflectionMetadata.h"

DECLARE_CYCLE_STAT(TEXT("STextBlock::SetText Time"), Stat_SlateTextBlockSetText, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::OnPaint Time"), Stat_SlateTextBlockOnPaint, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::ComputeDesiredSize"), Stat_SlateTextBlockCDS, STATGROUP_SlateVerbose)
DECLARE_CYCLE_STAT(TEXT("STextBlock::ComputeVolitility"), Stat_SlateTextBlockCV, STATGROUP_SlateVerbose)

STextBlock::STextBlock()
{
	bCanTick = false;
	bCanSupportFocus = false;
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
	ColorAndOpacity = InArgs._ColorAndOpacity;
	ShadowOffset = InArgs._ShadowOffset;
	ShadowColorAndOpacity = InArgs._ShadowColorAndOpacity;
	HighlightColor = InArgs._HighlightColor;
	HighlightShape = InArgs._HighlightShape;

	OnDoubleClicked = InArgs._OnDoubleClicked;

	BoundText = InArgs._Text;

	// We use a dummy style here (as it may not be safe to call the delegates used to compute the style), but the correct style is set by ComputeDesiredSize
	TextLayoutCache = MakeUnique<FSlateTextBlockLayout>(this, FTextBlockStyle::GetDefault(), InArgs._TextShapingMethod, InArgs._TextFlowDirection, FCreateSlateTextLayout(), FPlainTextLayoutMarshaller::Create(), InArgs._LineBreakPolicy);
	TextLayoutCache->SetDebugSourceInfo(TAttribute<FString>::Create(TAttribute<FString>::FGetter::CreateLambda([this]{ return FReflectionMetaData::GetWidgetDebugInfo(this); })));
}

FSlateFontInfo STextBlock::GetFont() const
{
	return Font.IsSet() ? Font.Get() : TextStyle.Font;
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

void STextBlock::SetText( const TAttribute< FString >& InText )
{
	if (InText.IsSet() && !InText.IsBound())
	{
		SetText(InText.Get());
		return;
	}

	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockSetText);
	struct Local
	{
		static FText PassThroughAttribute( TAttribute< FString > InString )
		{
			return FText::FromString( InString.Get( TEXT("") ) );
		}
	};

	BoundText = TAttribute< FText >::Create(TAttribute<FText>::FGetter::CreateStatic( &Local::PassThroughAttribute, InText) );

	Invalidate(EInvalidateWidget::LayoutAndVolatility);
}

void STextBlock::SetText( const FString& InText )
{
	SetText(FText::FromString(InText));
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
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
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
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
}

void STextBlock::SetHighlightText(TAttribute<FText> InText)
{
	HighlightText = InText;
}

int32 STextBlock::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockOnPaint);
	//SCOPED_NAMED_EVENT_TEXT("STextBlock", FColor::Orange);

	// OnPaint will also update the text layout cache if required
	LayerId = TextLayoutCache->OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));

	return LayerId;
}

FReply STextBlock::OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent )
{
	if ( InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		if( OnDoubleClicked.IsBound() )
		{
			return OnDoubleClicked.Execute();
		}
	}

	return FReply::Unhandled();
}

FVector2D STextBlock::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	SCOPE_CYCLE_COUNTER(Stat_SlateTextBlockCDS);

	// ComputeDesiredSize will also update the text layout cache if required
	const FVector2D TextSize = TextLayoutCache->ComputeDesiredSize(
		FSlateTextBlockLayout::FWidgetArgs(BoundText, HighlightText, WrapTextAt, AutoWrapText, WrappingPolicy, Margin, LineHeightPercentage, Justification),
		LayoutScaleMultiplier, GetComputedTextStyle()
		);

	return FVector2D(FMath::Max(MinDesiredWidth.Get(0.0f), TextSize.X), TextSize.Y);
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
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetColorAndOpacity(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	if ( !ColorAndOpacity.IsSet() || !ColorAndOpacity.IdenticalTo(InColorAndOpacity) )
	{
		ColorAndOpacity = InColorAndOpacity;
		Invalidate(EInvalidateWidget::PaintAndVolatility);
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

	Invalidate(EInvalidateWidget::Layout);
}

void STextBlock::SetTextShapingMethod(const TOptional<ETextShapingMethod>& InTextShapingMethod)
{
	TextLayoutCache->SetTextShapingMethod(InTextShapingMethod);
	Invalidate(EInvalidateWidget::Layout);
}

void STextBlock::SetTextFlowDirection(const TOptional<ETextFlowDirection>& InTextFlowDirection)
{
	TextLayoutCache->SetTextFlowDirection(InTextFlowDirection);
	Invalidate(EInvalidateWidget::Layout);
}

void STextBlock::SetWrapTextAt(const TAttribute<float>& InWrapTextAt)
{
	if(!WrapTextAt.IdenticalTo(InWrapTextAt))
	{
		WrapTextAt = InWrapTextAt;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetAutoWrapText(const TAttribute<bool>& InAutoWrapText)
{
	if(!AutoWrapText.IdenticalTo(InAutoWrapText))
	{
		AutoWrapText = InAutoWrapText;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetWrappingPolicy(const TAttribute<ETextWrappingPolicy>& InWrappingPolicy)
{
	if(!WrappingPolicy.IdenticalTo(InWrappingPolicy))
	{
		WrappingPolicy = InWrappingPolicy;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetShadowOffset(const TAttribute<FVector2D>& InShadowOffset)
{
	if(!ShadowOffset.IdenticalTo(InShadowOffset))
	{
		ShadowOffset = InShadowOffset;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetShadowColorAndOpacity(const TAttribute<FLinearColor>& InShadowColorAndOpacity)
{
	if(!ShadowColorAndOpacity.IdenticalTo(InShadowColorAndOpacity))
	{
		ShadowColorAndOpacity = InShadowColorAndOpacity;
		Invalidate(EInvalidateWidget::PaintAndVolatility);
	}
}

void STextBlock::SetMinDesiredWidth(const TAttribute<float>& InMinDesiredWidth)
{
	if(!MinDesiredWidth.IdenticalTo(InMinDesiredWidth))
	{
		MinDesiredWidth = InMinDesiredWidth;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetLineHeightPercentage(const TAttribute<float>& InLineHeightPercentage)
{
	if(!LineHeightPercentage.IdenticalTo(InLineHeightPercentage))
	{
		LineHeightPercentage = InLineHeightPercentage;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetMargin(const TAttribute<FMargin>& InMargin)
{
	if(!Margin.IdenticalTo(InMargin))
	{
		Margin = InMargin;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STextBlock::SetJustification(const TAttribute<ETextJustify::Type>& InJustification)
{
	if(!Justification.IdenticalTo(InJustification))
	{
		Justification = InJustification;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

FTextBlockStyle STextBlock::GetComputedTextStyle() const
{
	FTextBlockStyle ComputedStyle = TextStyle;
	ComputedStyle.SetFont( GetFont() );
	ComputedStyle.SetColorAndOpacity( GetColorAndOpacity() );
	ComputedStyle.SetShadowOffset( GetShadowOffset() );
	ComputedStyle.SetShadowColorAndOpacity( GetShadowColorAndOpacity() );
	ComputedStyle.SetHighlightColor( GetHighlightColor() );
	ComputedStyle.SetHighlightShape( *GetHighlightShape() );
	return ComputedStyle;
}
