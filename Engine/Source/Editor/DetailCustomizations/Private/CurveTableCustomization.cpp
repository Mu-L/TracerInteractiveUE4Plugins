// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Customizations/CurveTableCustomization.h"

#include "Widgets/Input/SSearchBox.h"

TSharedRef<SWidget> FCurveTableCustomizationLayout::GetListContent()
{
	SAssignNew(RowNameComboListView, SListView<TSharedPtr<FString> >)
		.ListItemsSource(&RowNames)
		.OnSelectionChanged(this, &FCurveTableCustomizationLayout::OnSelectionChanged)
		.OnGenerateRow(this, &FCurveTableCustomizationLayout::HandleRowNameComboBoxGenarateWidget)
		.SelectionMode(ESelectionMode::Single);


	if (CurrentSelectedItem.IsValid())
	{
		RowNameComboListView->SetSelection(CurrentSelectedItem);
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSearchBox)
			.OnTextChanged(this, &FCurveTableCustomizationLayout::OnFilterTextChanged)
		]
	+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			RowNameComboListView.ToSharedRef()
		];
}
