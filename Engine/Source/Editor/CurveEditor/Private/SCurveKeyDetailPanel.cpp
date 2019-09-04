// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SCurveKeyDetailPanel.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IPropertyRowGenerator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "IDetailTreeNode.h"

#define LOCTEXT_NAMESPACE "SCurveEditorPanel"

void SCurveKeyDetailPanel::Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor)
{

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FPropertyRowGeneratorArgs Args;
	Args.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
	// Args.NotifyHook = this;

	PropertyRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(Args);
	PropertyRowGenerator->OnRowsRefreshed().AddSP(this, &SCurveKeyDetailPanel::PropertyRowsRefreshed);
}

void SCurveKeyDetailPanel::PropertyRowsRefreshed()
{
	// UE_LOG(LogTemp, Log, TEXT("PropertyRowsRefreshed"));
	TSharedPtr<SWidget> TimeWidget = nullptr;
	TSharedPtr<SWidget> ValueWidget = nullptr;

	for (TSharedRef<IDetailTreeNode> RootNode : PropertyRowGenerator->GetRootTreeNodes())
	{
		// UE_LOG(LogTemp, Log, TEXT("Root NodeName: %s"), *RootNode->GetNodeName().ToString());
		TArray<TSharedRef<IDetailTreeNode>> Children;
		RootNode->GetChildren(Children);

		for (TSharedRef<IDetailTreeNode> Child : Children)
		{
			TArray<TSharedRef<IDetailTreeNode>> SubChildren;
			Child->GetChildren(SubChildren);
			// UE_LOG(LogTemp, Log, TEXT("Child NodeName: %s NumChildren: %d"), *Child->GetNodeName().ToString(), SubChildren.Num());

			// This is an ugly temporary hack until PropertyRowGenerator returns names for customized properties. This uses the first
			// two fields on the object instead of looking for "Time" and "Value". :(
			if (!TimeWidget.IsValid())
			{
				FNodeWidgets NodeWidgets = Child->CreateNodeWidgets();
				TimeWidget = NodeWidgets.ValueWidget;
			}
			else if (!ValueWidget.IsValid())
			{
				FNodeWidgets NodeWidgets = Child->CreateNodeWidgets();
				ValueWidget = NodeWidgets.ValueWidget;
			}
		}
	}

	if (TimeWidget && ValueWidget)
	{
		ConstructChildLayout(TimeWidget, ValueWidget);
	}
}

void SCurveKeyDetailPanel::ConstructChildLayout(TSharedPtr<SWidget> TimeWidget, TSharedPtr<SWidget> ValueWidget)
{
	check(TimeWidget && ValueWidget);
	TimeWidget->SetToolTipText(LOCTEXT("TimeEditBoxTooltip", "The time of the selected key(s)"));
	ValueWidget->SetToolTipText(LOCTEXT("ValueEditBoxTooltip", "The value of the selected key(s)"));

	ChildSlot
	[
		SNew(SHorizontalBox)
		// "Time" Label
		// + SHorizontalBox::Slot()
		// .VAlign(VAlign_Center)
		// .AutoWidth()
		// [
		// 	SNew(STextBlock)
		// 	.Text(LOCTEXT("KeyDetailTimeLabel", "Time"))
		// ]

		// "Time" Edit box
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Fill)
		// .HAlign(HAlign_Left)
		.Padding(4.f, 0.f, 0.f, 2.f)
		.FillWidth(0.5f)
		[
			TimeWidget.ToSharedRef()
		]

		// "Value" Label
		// + SHorizontalBox::Slot()
		// .VAlign(VAlign_Center)
		// .AutoWidth()
		// [
		// 	SNew(STextBlock)
		// 	.Text(LOCTEXT("KeyDetailValueLabel", "Value"))
		// ]

		// "Value" Edit box
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Fill)
		// .HAlign(HAlign_Left)
		.FillWidth(0.5f)
		.Padding(4.f, 0.f, 0.f, 2.f)
		[
			ValueWidget.ToSharedRef()
		]
	];
}

#undef LOCTEXT_NAMESPACE // "SCurveEditorPanel"