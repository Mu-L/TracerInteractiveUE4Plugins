// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraScratchPadViewModel.generated.h"

class FNiagaraSystemViewModel;
class FNiagaraScratchPadScriptViewModel;
class FNiagaraObjectSelection;
class UNiagaraScript;

UCLASS()
class NIAGARAEDITOR_API UNiagaraScratchPadViewModel : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnScriptViewModelsChanged);
	DECLARE_MULTICAST_DELEGATE(FOnActiveScriptChanged);
	DECLARE_MULTICAST_DELEGATE(FOnScriptRenamed);
	DECLARE_MULTICAST_DELEGATE(FOnScriptDeleted);

public:
	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

	void Finalize();

	void RefreshScriptViewModels();

	const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& GetScriptViewModels() const;

	const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& GetEditScriptViewModels() const;

	TSharedPtr<FNiagaraScratchPadScriptViewModel> GetViewModelForScript(UNiagaraScript* InScript);

	TSharedPtr<FNiagaraScratchPadScriptViewModel> GetViewModelForEditScript(UNiagaraScript* InEditScript);

	const TArray<ENiagaraScriptUsage>& GetAvailableUsages() const;

	FText GetDisplayNameForUsage(ENiagaraScriptUsage InUsage) const;

	TSharedRef<FNiagaraObjectSelection> GetObjectSelection();

	TSharedPtr<FNiagaraScratchPadScriptViewModel> GetActiveScriptViewModel();

	void SetActiveScriptViewModel(TSharedRef<FNiagaraScratchPadScriptViewModel> InActiveScriptViewModel);

	void FocusScratchPadScriptViewModel(TSharedRef<FNiagaraScratchPadScriptViewModel> InScriptViewModel);

	void ResetActiveScriptViewModel();

	void CopyActiveScript();

	bool CanPasteScript() const;

	void PasteScript();

	void DeleteActiveScript();

	TSharedPtr<FNiagaraScratchPadScriptViewModel> CreateNewScript(ENiagaraScriptUsage InScriptUsage, ENiagaraScriptUsage InTargetSupportedUsage, FNiagaraTypeDefinition InOutputType);

	TSharedPtr<FNiagaraScratchPadScriptViewModel> CreateNewScriptAsDuplicate(const UNiagaraScript* ScriptToDuplicate);

	void CreateAssetFromActiveScript();

	bool CanSelectNextUsageForActiveScript();

	void SelectNextUsageForActiveScript();

	FOnScriptViewModelsChanged& OnScriptViewModelsChanged();

	FOnScriptViewModelsChanged& OnEditScriptViewModelsChanged();

	FOnActiveScriptChanged& OnActiveScriptChanged();

	FOnScriptRenamed& OnScriptRenamed();

	FOnScriptDeleted& OnScriptDeleted();

private:
	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel();

	TSharedRef<FNiagaraScratchPadScriptViewModel> CreateAndSetupScriptviewModel(UNiagaraScript* ScratchPadScript);

	void TearDownScriptViewModel(TSharedRef<FNiagaraScratchPadScriptViewModel> InScriptViewModel);

	void ResetActiveScriptViewModelInternal(bool bRefreshEditScriptViewModels);

	void RefreshEditScriptViewModels();

	void ScriptGraphNodeSelectionChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> InScriptViewModelWeak);

	void ScriptViewModelScriptRenamed();

	void ScriptViewModelPinnedChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModelWeak);

	void ScriptViewModelChangesApplied();

	void ScriptViewModelRequestDiscardChanges(TWeakPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModelWeak);

	void ScriptViewModelVariableSelectionChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModelWeak);

private:
	TSharedPtr<FNiagaraObjectSelection> ObjectSelection;

	TSharedPtr<FNiagaraScratchPadScriptViewModel> ActiveScriptViewModel;

	TWeakPtr<FNiagaraSystemViewModel> SystemViewModelWeak;

	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> ScriptViewModels;

	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> PinnedScriptViewModels;

	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> EditScriptViewModels;

	TArray<ENiagaraScriptUsage> AvailableUsages;

	FOnScriptViewModelsChanged OnScriptViewModelsChangedDelegate;

	FOnScriptViewModelsChanged OnEditScriptViewModelsChangedDelegate;

	FOnActiveScriptChanged OnActiveScriptChangedDelegate;

	FOnScriptRenamed OnScriptRenamedDelegate;

	FOnScriptDeleted OnScriptDeletedDelegate;
};