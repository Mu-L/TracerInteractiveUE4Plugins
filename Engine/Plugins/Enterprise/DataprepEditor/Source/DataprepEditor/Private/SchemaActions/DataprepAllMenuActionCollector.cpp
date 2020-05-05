// Copyright Epic Games, Inc. All Rights Reserved.

#include "SchemaActions/DataprepAllMenuActionCollector.h"

#include "SchemaActions/DataprepFilterMenuActionCollector.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "SchemaActions/DataprepSelectionTransformMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"

namespace FDataprepAllMenuActionCollectorUtils
{
	void AddRootCategoryToActions(TArray<TSharedPtr<FDataprepSchemaAction>> Actions, const FText& Category)
	{
		for ( TSharedPtr<FDataprepSchemaAction> Action : Actions )
		{
			if ( Action )
			{
				Action->CosmeticUpdateCategory( FText::FromString( Category.ToString() + TEXT("|") + Action->GetCategory().ToString() ) );
			}
		}
	}
}

TArray<TSharedPtr<FDataprepSchemaAction>> FDataprepAllMenuActionCollector::CollectActions()
{
	FDataprepFilterMenuActionCollector FilterCollector;
	FilterCollector.GroupingPriority = 1;
	TArray< TSharedPtr< FDataprepSchemaAction > > Actions = FilterCollector.CollectActions();
	FDataprepAllMenuActionCollectorUtils::AddRootCategoryToActions( Actions, FDataprepFilterMenuActionCollector::FilterCategory );

	FDataprepSelectionTransformMenuActionCollector SelectionTransformCollector;
	TArray< TSharedPtr< FDataprepSchemaAction > > TransformActions = SelectionTransformCollector.CollectActions();
	FDataprepAllMenuActionCollectorUtils::AddRootCategoryToActions(TransformActions, FDataprepSelectionTransformMenuActionCollector::FilterCategory);
	Actions.Append( MoveTemp( TransformActions ) );

	FDataprepOperationMenuActionCollector OperationCollector;
	TArray< TSharedPtr< FDataprepSchemaAction > > OperationActions = OperationCollector.CollectActions();
	FDataprepAllMenuActionCollectorUtils::AddRootCategoryToActions( OperationActions, FDataprepOperationMenuActionCollector::OperationCategory );
	Actions.Append( MoveTemp( OperationActions ) );

	return Actions;
}

bool FDataprepAllMenuActionCollector::ShouldAutoExpand()
{
	return false;
}
