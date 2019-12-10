// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDataprepOperation.h"

#include "DataPrepOperation.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/SNullWidget.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void SDataprepOperation::Construct(const FArguments& InArgs, UDataprepOperation* InOperation, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext)
{
	Operation = InOperation;

	TAttribute<FText> TooltipTextAttribute = MakeAttributeSP( this, &SDataprepOperation::GetTooltipText );
	SetToolTipText( TooltipTextAttribute );

	SDataprepActionBlock::Construct( SDataprepActionBlock::FArguments(), InDataprepActionContext );
}

FText SDataprepOperation::GetBlockTitle() const
{
	return Operation ? Operation->GetDisplayOperationName() : FText::FromString( TEXT("Operation is Nullptr!") ) ;
}

TSharedRef<SWidget> SDataprepOperation::GetContentWidget()
{
	return SNew( SDataprepDetailsView ).Object( Operation );
}

void SDataprepOperation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Operation );
}

FText SDataprepOperation::GetTooltipText() const
{
	FText TooltipText;
	if ( Operation )
	{
		TooltipText = Operation->GetTooltip();
	}
	return TooltipText;
}
