// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraSystemScript.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptInputCollectionViewModel.h"
#include "SNiagaraParameterCollection.h"
#include "SNiagaraScriptGraph.h"

#include "Widgets/Layout/SSplitter.h"

void SNiagaraSystemScript::Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;
	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(0.3f)
		[
			SNew(SNiagaraParameterCollection, SystemViewModel->GetSystemScriptViewModel()->GetInputCollectionViewModel())
		]
		+ SSplitter::Slot()
		.Value(0.7f)
		[
			SNew(SNiagaraScriptGraph, SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel())
		]
	];
}
