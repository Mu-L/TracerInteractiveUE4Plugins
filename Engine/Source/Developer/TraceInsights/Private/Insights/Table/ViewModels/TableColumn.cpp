// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TableColumn.h"

#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"

#define LOCTEXT_NAMESPACE "TableColumn"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableCellValueGetter> FTableColumn::GetDefaultValueGetter()
{
	return MakeShareable(new FTableCellValueGetter());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TOptional<FTableCellValue> FTableColumn::GetValue(const FBaseTreeNode& InNode) const
{
	return ValueGetter->GetValue(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableCellValueFormatter> FTableColumn::GetDefaultValueFormatter()
{
	return MakeShareable(new FTableCellValueFormatter());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FTableColumn::GetValueAsText(const FBaseTreeNode& InNode) const
{
	return ValueFormatter->FormatValue(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FTableColumn::GetValueAsTooltipText(const FBaseTreeNode& InNode) const
{
	return ValueFormatter->FormatValueForTooltip(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
