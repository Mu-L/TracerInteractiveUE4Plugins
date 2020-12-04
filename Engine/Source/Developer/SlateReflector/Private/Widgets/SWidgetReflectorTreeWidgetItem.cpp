// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SWidgetReflectorTreeWidgetItem.h"
#include "SlateOptMacros.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SHyperlink.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "SWidgetReflector"

/* SMultiColumnTableRow overrides
 *****************************************************************************/

FName SReflectorTreeWidgetItem::NAME_WidgetName(TEXT("WidgetName"));
FName SReflectorTreeWidgetItem::NAME_WidgetInfo(TEXT("WidgetInfo"));
FName SReflectorTreeWidgetItem::NAME_Visibility(TEXT("Visibility"));
FName SReflectorTreeWidgetItem::NAME_Focusable(TEXT("Focusable"));
FName SReflectorTreeWidgetItem::NAME_Clipping(TEXT("Clipping"));
FName SReflectorTreeWidgetItem::NAME_ForegroundColor(TEXT("ForegroundColor"));
FName SReflectorTreeWidgetItem::NAME_Address(TEXT("Address"));

void SReflectorTreeWidgetItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	this->WidgetInfo = InArgs._WidgetInfoToVisualize;
	this->OnAccessSourceCode = InArgs._SourceCodeAccessor;
	this->OnAccessAsset = InArgs._AssetAccessor;
	this->SetPadding(0);

	check(WidgetInfo.IsValid());
	CachedWidgetType = WidgetInfo->GetWidgetType();
	CachedWidgetTypeAndShortName = WidgetInfo->GetWidgetTypeAndShortName();
	CachedWidgetVisibility = WidgetInfo->GetWidgetVisibilityText();
	CachedWidgetClipping = WidgetInfo->GetWidgetClippingText();
	bCachedWidgetFocusable = WidgetInfo->GetWidgetFocusable();
	bCachedWidgetVisible = WidgetInfo->GetWidgetVisible();
	bCachedWidgetNeedsTick = WidgetInfo->GetWidgetNeedsTick();
	bCachedWidgetIsVolatile = WidgetInfo->GetWidgetIsVolatile();
	bCachedWidgetIsVolatileIndirectly = WidgetInfo->GetWidgetIsVolatileIndirectly();
	bCachedWidgetHasActiveTimers = WidgetInfo->GetWidgetHasActiveTimers();
	CachedReadableLocation = WidgetInfo->GetWidgetReadableLocation();
	CachedWidgetFile = WidgetInfo->GetWidgetFile();
	CachedWidgetLineNumber = WidgetInfo->GetWidgetLineNumber();
	CachedAssetData = WidgetInfo->GetWidgetAssetData();

	SMultiColumnTableRow< TSharedRef<FWidgetReflectorNodeBase> >::Construct(SMultiColumnTableRow< TSharedRef<FWidgetReflectorNodeBase> >::FArguments().Padding(0), InOwnerTableView);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SReflectorTreeWidgetItem::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == NAME_WidgetName )
	{
		return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SExpanderArrow, SharedThis(this))
			.IndentAmount(16)
			.ShouldDrawWires(true)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SReflectorTreeWidgetItem::GetWidgetTypeAndShortName)
			.ColorAndOpacity(this, &SReflectorTreeWidgetItem::GetTint)
		];
	}
	else if (ColumnName == NAME_WidgetInfo )
	{
		return SNew(SBox)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SHyperlink)
				.Text(this, &SReflectorTreeWidgetItem::GetReadableLocationAsText)
				.OnNavigate(this, &SReflectorTreeWidgetItem::HandleHyperlinkNavigate)
			];
	}
	else if (ColumnName == NAME_Visibility )
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SReflectorTreeWidgetItem::GetVisibilityAsString)
					.Justification(ETextJustify::Center)
			];
	}
	else if (ColumnName == NAME_Focusable)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SCheckBox)
				.Style(FCoreStyle::Get(), TEXT("WidgetReflector.FocusableCheck"))
				.IsChecked(this, &SReflectorTreeWidgetItem::GetFocusableAsCheckBoxState)
			];
	}
	else if ( ColumnName == NAME_Clipping )
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SReflectorTreeWidgetItem::GetClippingAsString)
			];
	}
	else if (ColumnName == NAME_ForegroundColor )
	{
		const FSlateColor Foreground = WidgetInfo->GetWidgetForegroundColor();

		return SNew(SBorder)
			// Show unset color as an empty space.
			.Visibility(Foreground.IsColorSpecified() ? EVisibility::Visible : EVisibility::Hidden)
			// Show a checkerboard background so we can see alpha values well
			.BorderImage(FCoreStyle::Get().GetBrush("Checkerboard"))
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				// Show a color block
				SNew(SColorBlock)
					.Color(Foreground.GetSpecifiedColor())
					.Size(FVector2D(16.0f, 16.0f))
			];
	}
	else if (ColumnName == NAME_Address )
	{
		const FString WidgetAddress = FWidgetReflectorNodeUtils::WidgetAddressToString(WidgetInfo->GetWidgetAddress());
		const FText Address = FText::FromString(WidgetAddress);
		const FString ConditionalBreakPoint = FString::Printf(TEXT("this == (SWidget*)%s"), *WidgetAddress);

		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SHyperlink)
				.ToolTipText(LOCTEXT("ClickToCopyBreakpoint", "Click to copy conditional breakpoint for this instance."))
				.Text(LOCTEXT("CBP", "[CBP]"))
				.OnNavigate_Lambda([ConditionalBreakPoint](){ FPlatformApplicationMisc::ClipboardCopy(*ConditionalBreakPoint); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0, 0, 2, 0))
			[
				SNew(SHyperlink)
				.ToolTipText(LOCTEXT("ClickToCopy", "Click to copy address."))
				.Text(Address)
				.OnNavigate_Lambda([Address]() { FPlatformApplicationMisc::ClipboardCopy(*Address.ToString()); })
			];
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SReflectorTreeWidgetItem::HandleHyperlinkNavigate()
{
	if (CachedAssetData.IsValid())
	{
		if (OnAccessAsset.IsBound())
		{
			CachedAssetData.GetPackage();
			OnAccessAsset.Execute(CachedAssetData.GetAsset());
			return;
		}
	}

	if (OnAccessSourceCode.IsBound())
	{
		OnAccessSourceCode.Execute(GetWidgetFile(), GetWidgetLineNumber(), 0);
	}
}

#undef LOCTEXT_NAMESPACE