// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "Models/WidgetReflectorNode.h"

/**
 * Widget that visualizes the contents of a FReflectorNode.
 */
class SLATEREFLECTOR_API SReflectorTreeWidgetItem
	: public SMultiColumnTableRow<TSharedRef<FWidgetReflectorNodeBase>>
{
public:

	static FName NAME_WidgetName;
	static FName NAME_WidgetInfo;
	static FName NAME_Visibility;
	static FName NAME_Focusable;
	static FName NAME_Clipping;
	static FName NAME_ForegroundColor;
	static FName NAME_Address;

	SLATE_BEGIN_ARGS(SReflectorTreeWidgetItem)
		: _WidgetInfoToVisualize()
		, _SourceCodeAccessor()
		, _AssetAccessor()
	{ }

		SLATE_ARGUMENT(TSharedPtr<FWidgetReflectorNodeBase>, WidgetInfoToVisualize)
		SLATE_ARGUMENT(FAccessSourceCode, SourceCodeAccessor)
		SLATE_ARGUMENT(FAccessAsset, AssetAccessor)

	SLATE_END_ARGS()

public:

	/**
	 * Construct child widgets that comprise this widget.
	 *
	 * @param InArgs Declaration from which to construct this widget.
	 */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

public:

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override;

protected:

	/** @return String representation of the widget we are visualizing */
	FText GetWidgetType() const
	{
		return CachedWidgetType;
	}

	FText GetWidgetTypeAndShortName() const
	{
		return CachedWidgetTypeAndShortName;
	}

	virtual FString GetReadableLocation() const override
	{
		return CachedReadableLocation.ToString();
	}

	FText GetReadableLocationAsText() const
	{
		return CachedReadableLocation;
	}

	FString GetWidgetFile() const
	{
		return CachedWidgetFile;
	}

	int32 GetWidgetLineNumber() const
	{
		return CachedWidgetLineNumber;
	}

	FText GetVisibilityAsString() const
	{
		return CachedWidgetVisibility;
	}
	
	
	FText GetClippingAsString() const
	{
		return CachedWidgetClipping;
	}

	ECheckBoxState GetFocusableAsCheckBoxState() const
	{
		return bCachedWidgetFocusable ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	/** @return The tint of the reflector node */
	FSlateColor GetTint() const
	{
		return WidgetInfo->GetTint();
	}

	void HandleHyperlinkNavigate();

private:

	/** The info about the widget that we are visualizing. */
	TSharedPtr<FWidgetReflectorNodeBase> WidgetInfo;

	FText CachedWidgetType;
	FText CachedWidgetTypeAndShortName;
	FText CachedWidgetVisibility;
	FText CachedWidgetClipping;
	bool bCachedWidgetFocusable;
	bool bCachedWidgetVisible;
	bool bCachedWidgetNeedsTick;
	bool bCachedWidgetIsVolatile;
	bool bCachedWidgetIsVolatileIndirectly;
	bool bCachedWidgetHasActiveTimers;
	FText CachedReadableLocation;
	FString CachedWidgetFile;
	int32 CachedWidgetLineNumber;
	FAssetData CachedAssetData;

	FAccessSourceCode OnAccessSourceCode;

	FAccessAsset OnAccessAsset;
};
