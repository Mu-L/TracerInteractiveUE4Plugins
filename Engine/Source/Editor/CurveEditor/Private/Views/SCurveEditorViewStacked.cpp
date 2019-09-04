// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Views/SCurveEditorViewStacked.h"
#include "CurveEditor.h"
#include "CurveModel.h"
#include "SCurveEditorPanel.h"

#include "Widgets/Text/STextBlock.h"

#include "EditorStyleSet.h"

constexpr float StackedHeight = 150.f;
constexpr float StackedPadding = 10.f;

void SCurveEditorViewStacked::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	bFixedOutputBounds = true;
	OutputMin = 0.0;
	OutputMax = 1.0;

	SInteractiveCurveEditorView::Construct(InArgs, InCurveEditor);
}

FVector2D SCurveEditorViewStacked::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(100.f, StackedHeight*CurveInfoByID.Num() + StackedPadding*(CurveInfoByID.Num()+1));
}

void SCurveEditorViewStacked::GetGridLinesY(TSharedRef<FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>& MajorGridLabels) const
{
	const double ValuePerPixel = 1.0 / StackedHeight;
	const double ValueSpacePadding = StackedPadding * ValuePerPixel;

	FCurveEditorScreenSpace ViewSpace = GetViewSpace();
	for (int32 Index = 0; Index < CurveInfoByID.Num(); ++Index)
	{
		double Padding = (Index + 1)*ValueSpacePadding;
		double LowerValue = Index + Padding;

		// Lower Grid line
		MajorGridLines.Add(ViewSpace.ValueToScreen(LowerValue));
		// Center Grid line
		MajorGridLines.Add(ViewSpace.ValueToScreen(LowerValue + 0.5));
		// Upper Grid line
		MajorGridLines.Add(ViewSpace.ValueToScreen(LowerValue + 1.0));

		MinorGridLines.Add(ViewSpace.ValueToScreen(LowerValue + 0.25));
		MinorGridLines.Add(ViewSpace.ValueToScreen(LowerValue + 0.75));
	}
}

void SCurveEditorViewStacked::PaintView(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		DrawBackground(AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawViewGrids(AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, DrawEffects);
		DrawLabels(AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, DrawEffects);
		DrawCurves(CurveEditor.ToSharedRef(), AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, DrawEffects);
	}
}

void SCurveEditorViewStacked::DrawViewGrids(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	const int32 GridLineLayerId = BaseLayerId + CurveViewConstants::ELayerOffset::GridLines;

	// Rendering info
	const float          Width = AllottedGeometry.GetLocalSize().X;
	const float          Height = AllottedGeometry.GetLocalSize().Y;
	const FLinearColor   MajorGridColor = CurveEditor->GetPanel()->GetGridLineTint();
	const FLinearColor   MinorGridColor = MajorGridColor.CopyWithNewOpacity(MajorGridColor.A * .5f);
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
	const FSlateBrush*   WhiteBrush = FEditorStyle::GetBrush("WhiteBrush");

	TArray<float> MajorGridLines, MinorGridLines;
	TArray<FText> MajorGridLabels;

	GetGridLinesX(CurveEditor.ToSharedRef(), MajorGridLines, MinorGridLines, MajorGridLabels);

	// Pre-allocate an array of line points to draw our vertical lines. Each major grid line
	// will overwrite the X value of both points but leave the Y value untouched so they draw from the bottom to the top.
	TArray<FVector2D> LinePoints;
	LinePoints.Add(FVector2D(0.f, 0.f));
	LinePoints.Add(FVector2D(0.f, 0.f));

	const double ValuePerPixel = 1.0 / StackedHeight;
	const double ValueSpacePadding = StackedPadding * ValuePerPixel;

	FCurveEditorScreenSpace ViewSpace = GetViewSpace();
	for (auto It = CurveInfoByID.CreateConstIterator(); It; ++It)
	{
		FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
		if (!ensureAlways(Curve))
		{
			continue;
		}

		const int32  Index = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
		double Padding = (Index + 1)*ValueSpacePadding;
		double LowerValue = Index + Padding;

		double PixelBottom = ViewSpace.ValueToScreen(LowerValue);
		double PixelTop    = ViewSpace.ValueToScreen(LowerValue + 1.0);

		if (!FSlateRect::DoRectanglesIntersect(MyCullingRect, TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), FSlateRect(0, PixelTop, Width, PixelBottom))))
		{
			continue;
		}

		// Tint the views based on their curve color
		{
			FLinearColor CurveColorTint = Curve->GetColor().CopyWithNewOpacity(0.05f);
			const FPaintGeometry BoxGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2D(Width, StackedHeight),
			FSlateLayoutTransform(FVector2D(0.f, PixelTop))
			);
			
			FSlateDrawElement::MakeBox(OutDrawElements, GridLineLayerId + 1, BoxGeometry, WhiteBrush, DrawEffects, CurveColorTint);
		}

		// Horizontal grid lines
		{
			LinePoints[0].X = 0.0;
			LinePoints[1].X = Width;

			// Top grid line
			LinePoints[0].Y = LinePoints[1].Y = PixelTop;
			FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MajorGridColor, false);

			// Center line
			LinePoints[0].Y = LinePoints[1].Y = ViewSpace.ValueToScreen(LowerValue + 0.5);
			FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MajorGridColor, false);

			// Bottom line
			LinePoints[0].Y = LinePoints[1].Y = PixelBottom;
			FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MajorGridColor, false);

			// Minor intermediate lines
			LinePoints[0].Y = LinePoints[1].Y = ViewSpace.ValueToScreen(LowerValue + 0.25);
			FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MinorGridColor, false);

			LinePoints[0].Y = LinePoints[1].Y = ViewSpace.ValueToScreen(LowerValue + 0.75);
			FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MinorGridColor, false);
		}

		// Vertical grid lines
		{
			const float RoundedWidth = FMath::RoundToFloat(Width);

			LinePoints[0].Y = PixelTop;
			LinePoints[1].Y = PixelBottom;

			// Draw major vertical grid lines
			for (float VerticalLine : MajorGridLines)
			{
				VerticalLine = FMath::RoundToFloat(VerticalLine);
				if (VerticalLine >= 0 || VerticalLine <= RoundedWidth)
				{
					LinePoints[0].X = LinePoints[1].X = VerticalLine;
					FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MajorGridColor, false);
				}
			}

			// Now draw the minor vertical lines which are drawn with a lighter color.
			for (float VerticalLine : MinorGridLines)
			{
				VerticalLine = FMath::RoundToFloat(VerticalLine);
				if (VerticalLine >= 0 || VerticalLine <= RoundedWidth)
				{
					LinePoints[0].X = LinePoints[1].X = VerticalLine;
					FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, PaintGeometry, LinePoints, DrawEffects, MinorGridColor, false);
				}
			}
		}
	}
}

void SCurveEditorViewStacked::DrawLabels(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	const int32 LabelLayerId = BaseLayerId + CurveViewConstants::ELayerOffset::Labels;

	const double ValuePerPixel = 1.0 / StackedHeight;
	const double ValueSpacePadding = StackedPadding * ValuePerPixel;

	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("FontAwesome.11");
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FCurveEditorScreenSpaceV ViewSpace = GetViewSpace();

	// Draw the curve labels for each view
	for (auto It = CurveInfoByID.CreateConstIterator(); It; ++It)
	{
		FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
		if (!ensureAlways(Curve))
		{
			continue;
		}

		const int32  CurveIndexFromBottom = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
		const double PaddingToBottomOfView = (CurveIndexFromBottom + 1)*ValueSpacePadding;
		
		const float PixelBottom = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView);
		const float PixelTop = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView + 1.0);

		if (!FSlateRect::DoRectanglesIntersect(MyCullingRect, TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), FSlateRect(0, PixelTop, LocalSize.X, PixelBottom))))
		{
			continue;
		}

		const FText Label = Curve->GetLongDisplayName();

		const FVector2D Position(CurveViewConstants::CurveLabelOffsetX, PixelTop + CurveViewConstants::CurveLabelOffsetY);

		const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(Position));
		const FPaintGeometry LabelDropshadowGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(Position + FVector2D(2, 2)));

		// Drop shadow
		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId, LabelDropshadowGeometry, Label, FontInfo, DrawEffects, FLinearColor::Black.CopyWithNewOpacity(0.80f));
		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId+1, LabelGeometry, Label, FontInfo, DrawEffects, Curve->GetColor());
	}
}

void SCurveEditorViewStacked::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	if (!CurveEditor->AreBoundTransformUpdatesSuppressed())
	{
		double ValuePerPixel = 1.0 / StackedHeight;
		double ValueSpacePadding = StackedPadding * ValuePerPixel;

		for (auto It = CurveInfoByID.CreateIterator(); It; ++It)
		{
			FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
			if (!ensureAlways(Curve))
			{
				continue;
			}

			const int32  CurveIndexFromBottom = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
			const double PaddingToBottomOfView = (CurveIndexFromBottom + 1)*ValueSpacePadding;
			const double ValueOffset = -CurveIndexFromBottom - PaddingToBottomOfView;

			double CurveOutputMin = 0, CurveOutputMax = 1;
			Curve->GetValueRange(CurveOutputMin, CurveOutputMax);

			if (CurveOutputMax > CurveOutputMin)
			{
				It->Value.ViewToCurveTransform = Concatenate(FVector2D(0.f, ValueOffset), Concatenate(FScale2D(1.f, (CurveOutputMax - CurveOutputMin)), FVector2D(0.f, CurveOutputMin)));
			}
			else
			{
				It->Value.ViewToCurveTransform = Concatenate(FVector2D(0.f, ValueOffset-0.5), FVector2D(0.f, CurveOutputMin));
			}
		}

		OutputMax = FMath::Max(OutputMin + CurveInfoByID.Num() + ValueSpacePadding*(CurveInfoByID.Num()+1), 1.0);
	}

	SInteractiveCurveEditorView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}