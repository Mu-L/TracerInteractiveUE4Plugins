// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "ViewModels/NiagaraParameterEditMode.h"
#include "ViewModels/NiagaraScriptViewModel.h"

class FNiagaraMessageLogViewModel;
class UNiagaraScript;

class FNiagaraStandaloneScriptViewModel : public FNiagaraScriptViewModel
{
public:
	FNiagaraStandaloneScriptViewModel(
		FText DisplayName,
		ENiagaraParameterEditMode InParameterEditMode,
		TSharedPtr<FNiagaraMessageLogViewModel> InNiagaraMessageLogViewModel,
		const FGuid& InMessageLogGuidKey
	);

	void Initialize(UNiagaraScript* InScript, UNiagaraScript* InSourceScript);
	UNiagaraScript* GetStandaloneScript();

private:
	virtual void OnVMScriptCompiled(UNiagaraScript* InScript) override;

	/** Sends messages to FNiagaraMessageManager for all compile events from the last compile. */
	void SendLastCompileMessages(const UNiagaraScript* InScript);

	TSharedPtr<FNiagaraMessageLogViewModel> NiagaraMessageLogViewModel;
	const UNiagaraScript* SourceScript;
	const FGuid ScriptMessageLogGuidKey;
};
