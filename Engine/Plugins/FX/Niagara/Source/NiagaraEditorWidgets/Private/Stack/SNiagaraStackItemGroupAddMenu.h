// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "EdGraph/EdGraphSchema.h"
#include "Styling/SlateTypes.h"

class INiagaraStackItemGroupAddUtilities;

class SNiagaraStackItemGroupAddMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackItemGroupAddMenu) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, INiagaraStackItemGroupAddUtilities* InAddUtilities, int32 InInsertIndex);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<class SEditableTextBox> GetFilterTextBox();

private:
	void CollectAllAddActions(FGraphActionListBuilderBase& OutAllActions);

	void OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType);

	void OnLibraryToggleChanged(ECheckBoxState CheckState);

	ECheckBoxState LibraryToggleIsChecked() const;

private:
	INiagaraStackItemGroupAddUtilities* AddUtilities;

	int32 InsertIndex;

	TSharedPtr<class SGraphActionMenu> AddMenu;

	bool bSetFocusOnNextTick;
	
	static bool bIncludeNonLibraryScripts;
};