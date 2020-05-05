// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailMultiTopLevelObjectRootNode.h"
#include "IDetailRootObjectCustomization.h"
#include "DetailWidgetRow.h"
#include "ObjectPropertyNode.h"
#include "Misc/ConfigCacheIni.h"


void SDetailMultiTopLevelObjectTableRow::Construct( const FArguments& InArgs, TSharedRef<FDetailTreeNode> InOwnerTreeNode, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	OwnerTreeNode = InOwnerTreeNode;
	ExpansionArrowUsage = InArgs._ExpansionArrowUsage;

	ChildSlot
	[	
		SNew( SBox )
		.Padding( FMargin( 0.0f, 0.0f, SDetailTableRowBase::ScrollbarPaddingSize, 0.0f ) )
		[
			SNew( SHorizontalBox )
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f, 2.0f, 2.0f)
			.AutoWidth()
			[
				SNew( SExpanderArrow, SharedThis(this) )
				.Visibility(ExpansionArrowUsage == EExpansionArrowUsage::Default ? EVisibility::Visible : EVisibility::Collapsed)
			]
			+SHorizontalBox::Slot()
			.Expose(ContentSlot)
			[
				SNullWidget::NullWidget
			]
		]
	];

	STableRow< TSharedPtr< FDetailTreeNode > >::ConstructInternal(
		STableRow::FArguments()
			.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(false),
		InOwnerTableView
	);
}


void SDetailMultiTopLevelObjectTableRow::SetContent(TSharedRef<SWidget> InContent)
{
	(*ContentSlot)
	[
		InContent
	];
}

const FSlateBrush* SDetailMultiTopLevelObjectTableRow::GetBackgroundImage() const
{
	if (IsHovered())
	{
		return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
	}
	else
	{
		return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
	}
}

FReply SDetailMultiTopLevelObjectTableRow::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (ExpansionArrowUsage != EExpansionArrowUsage::None && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ToggleExpansion();
		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SDetailMultiTopLevelObjectTableRow::OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDown(InMyGeometry, InMouseEvent);
}


FDetailMultiTopLevelObjectRootNode::FDetailMultiTopLevelObjectRootNode( const FDetailNodeList& InChildNodes, const TSharedPtr<IDetailRootObjectCustomization>& InRootObjectCustomization, IDetailsViewPrivate* InDetailsView, const FObjectPropertyNode* RootNode)
	: ChildNodes(InChildNodes)
	, DetailsView(InDetailsView)
	, RootObjectCustomization(InRootObjectCustomization)
	, bShouldBeVisible(false)
	, bHasFilterStrings(false)
{
	RootObjectSet.RootObjects.Reserve(RootNode->GetNumObjects());
	for (int32 ObjectIndex = 0; ObjectIndex < RootNode->GetNumObjects(); ++ObjectIndex)
	{
		RootObjectSet.RootObjects.Add(RootNode->GetUObject(ObjectIndex));
	}

	RootObjectSet.CommonBaseClass = RootNode->GetObjectBaseClass();

	NodeName = RootObjectSet.CommonBaseClass->GetFName();
}

void FDetailMultiTopLevelObjectRootNode::OnItemExpansionChanged(bool bIsExpanded, bool bShouldSaveState)
{
	if (bShouldSaveState)
	{
		GConfig->SetBool(TEXT("DetailMultiObjectNodeExpansion"), *NodeName.ToString(), bIsExpanded, GEditorPerProjectIni);
	}
}

bool FDetailMultiTopLevelObjectRootNode::ShouldBeExpanded() const
{
	if (bHasFilterStrings)
	{
		return true;
	}
	else
	{
		bool bShouldBeExpanded = true;
		GConfig->GetBool(TEXT("DetailMultiObjectNodeExpansion"), *NodeName.ToString(), bShouldBeExpanded, GEditorPerProjectIni);
		return bShouldBeExpanded;
	}
}

ENodeVisibility FDetailMultiTopLevelObjectRootNode::GetVisibility() const
{
	ENodeVisibility FinalVisibility = ENodeVisibility::Visible;
	if(RootObjectCustomization.IsValid() && !RootObjectCustomization.Pin()->AreObjectsVisible(RootObjectSet))
	{
		FinalVisibility = ENodeVisibility::ForcedHidden;
	}
	else
	{
		FinalVisibility = bShouldBeVisible ? ENodeVisibility::Visible : ENodeVisibility::HiddenDueToFiltering;
	}

	return FinalVisibility;
}

TSharedRef< ITableRow > FDetailMultiTopLevelObjectRootNode::GenerateWidgetForTableView(const TSharedRef<STableViewBase>& OwnerTable, const FDetailColumnSizeData& ColumnSizeData, bool bAllowFavoriteSystem)
{
	EExpansionArrowUsage ExpansionArrowUsage = EExpansionArrowUsage::None;

	TSharedPtr<IDetailRootObjectCustomization> Customization = RootObjectCustomization.Pin();
	if (Customization)
	{
		ExpansionArrowUsage = Customization->GetExpansionArrowUsage();
	}

	TSharedRef<SDetailMultiTopLevelObjectTableRow> TableRowWidget =
		SNew(SDetailMultiTopLevelObjectTableRow, AsShared(), OwnerTable)
		.ExpansionArrowUsage(ExpansionArrowUsage);

	FDetailWidgetRow Row;
	GenerateWidget_Internal(Row, TableRowWidget);

	TableRowWidget->SetContent(Row.NameWidget.Widget);

	return TableRowWidget;
}


bool FDetailMultiTopLevelObjectRootNode::GenerateStandaloneWidget(FDetailWidgetRow& OutRow) const
{
	GenerateWidget_Internal(OutRow, nullptr);
	return true;
}


void FDetailMultiTopLevelObjectRootNode::GetChildren(FDetailNodeList& OutChildren )
{
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FDetailTreeNode>& Child = ChildNodes[ChildIndex];
		if( Child->GetVisibility() == ENodeVisibility::Visible )
		{
			if( Child->ShouldShowOnlyChildren() )
			{
				Child->GetChildren( OutChildren );
			}
			else
			{
				OutChildren.Add( Child );
			}
		}
	}
}

void FDetailMultiTopLevelObjectRootNode::FilterNode( const FDetailFilter& InFilter )
{
	bShouldBeVisible = false;
	bHasFilterStrings = InFilter.FilterStrings.Num() > 0;

	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FDetailTreeNode>& Child = ChildNodes[ChildIndex];

		Child->FilterNode( InFilter );

		if( Child->GetVisibility() == ENodeVisibility::Visible )
		{
			bShouldBeVisible = true;

			if (DetailsView)
			{
				DetailsView->RequestItemExpanded(Child, Child->ShouldBeExpanded());
			}
		}
	}
}

bool FDetailMultiTopLevelObjectRootNode::ShouldShowOnlyChildren() const
{
	return RootObjectCustomization.IsValid() && RootObjectSet.RootObjects.Num() ? !RootObjectCustomization.Pin()->ShouldDisplayHeader(RootObjectSet) : bShouldShowOnlyChildren;
}

void FDetailMultiTopLevelObjectRootNode::GenerateWidget_Internal(FDetailWidgetRow& OutRow, TSharedPtr<SDetailMultiTopLevelObjectTableRow> TableRowWidget) const
{
	TSharedPtr<SWidget> HeaderWidget;
	if (RootObjectCustomization.IsValid() && RootObjectSet.RootObjects.Num())
	{
		HeaderWidget = RootObjectCustomization.Pin()->CustomizeObjectHeader(RootObjectSet, TableRowWidget);
	}

	OutRow.NameContent()
	[
		HeaderWidget.ToSharedRef()
	];

}
