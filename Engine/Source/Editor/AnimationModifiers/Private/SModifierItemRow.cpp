// Copyright Epic Games, Inc. All Rights Reserved.

#include "SModifierItemRow.h"
#include "Widgets/Images/SImage.h"
#include "Animation/Skeleton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"

void SModifierItemRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const ModifierListviewItem& Item)
{
	STableRow<ModifierListviewItem>::ConstructInternal(STableRow::FArguments(), InOwnerTableView);

	OnOpenModifier = InArgs._OnOpenModifier;
	InternalItem = Item;
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.0f, 2.0f, 0.0f, 2.0f)
		[
			SNew(SImage)
			.Image(InternalItem->OuterClass == USkeleton::StaticClass() ? FEditorStyle::GetBrush("ClassIcon.Skeleton") : FEditorStyle::GetBrush("ClassIcon.AnimSequence"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.0f, 3.0f, 0.0f, 2.0f)
		[			
			SNew(STextBlock)
			.Text(this, &SModifierItemRow::GetInstanceText)
			.OnDoubleClicked(this, &SModifierItemRow::OnDoubleClicked)
		]
	];
}

FReply SModifierItemRow::OnDoubleClicked(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent)
{
	OnOpenModifier.ExecuteIfBound(InternalItem->Instance);
	return FReply::Handled();
}

FText SModifierItemRow::GetInstanceText() const
{
	FString LabelString = InternalItem->Class->GetName();
	static const FString Postfix("_C");
	// Ensure we remove the modifier class postfix
	LabelString.RemoveFromEnd(Postfix);

	if (InternalItem->Instance.IsValid() && !InternalItem->Instance->IsLatestRevisionApplied())
	{
		LabelString.Append(" (Out of Date)");
	}

	return FText::FromString(LabelString);
}

