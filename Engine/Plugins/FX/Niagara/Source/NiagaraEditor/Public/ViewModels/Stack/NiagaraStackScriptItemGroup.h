// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraCommon.h"
#include "UObject/ObjectKey.h"
#include "AssetRegistry/Public/AssetData.h"
#include "NiagaraStackScriptItemGroup.generated.h"

class FNiagaraScriptViewModel;
class UNiagaraStackSpacer;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
class FScriptItemGroupAddUtilities;
class UNiagaraStackModuleItem;
class UEdGraph;
class UNiagaraStackModuleSpacer;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackScriptItemGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	void Initialize(
		FRequiredEntryData InRequiredEntryData,
		FText InDisplayName,
		FText InToolTip,
		TSharedRef<FNiagaraScriptViewModel> InScriptViewModel,
		ENiagaraScriptUsage InScriptUsage,
		FGuid InScriptUsageId = FGuid());

	ENiagaraScriptUsage GetScriptUsage() const { return ScriptUsage; }
	FGuid GetScriptUsageId() const { return ScriptUsageId; }
	UNiagaraNodeOutput* GetScriptOutputNode() const;

	/**
	 * Add a Set Variables module to the stack.
	 * @InModuleSpacer: Target UNiagaraStackModuleSpacer which we use to derive the insertion index.
	 * @InVariable: Initial FNiagaraVariable to populate the new Set Variables module.
	 */
	void AddParameterModuleToStack(const UNiagaraStackModuleSpacer* InModuleSpacer, const FNiagaraVariable& InVariable);

	void AddAssetModuleToStack(const UNiagaraStackModuleSpacer* InModuleSpcaer, const FAssetData& InAsset);

	bool CanAddAssetModuleToStack(const UNiagaraStackModuleSpacer* InModuleSpacer, const FAssetData& InAsset);

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void FinalizeInternal() override;

	virtual TOptional<FDropResult> ChildRequestCanDropInternal(const UNiagaraStackEntry& TargetChild, const TArray<UNiagaraStackEntry*>& DraggedEntries) override;

	virtual TOptional<FDropResult> ChildRequestDropInternal(const UNiagaraStackEntry& TargetChild, const TArray<UNiagaraStackEntry*>& DraggedEntries) override;

private:
	void ItemAdded(UNiagaraNodeFunctionCall* AddedModule);

	void ChildModifiedGroupItems();

	void OnScriptGraphChanged(const struct FEdGraphEditAction& InAction);

protected:
	TWeakPtr<FNiagaraScriptViewModel> ScriptViewModel;
	void RefreshIssues(TArray<FStackIssue>& NewIssues);

private:
	TSharedPtr<FScriptItemGroupAddUtilities> AddUtilities;

	ENiagaraScriptUsage ScriptUsage;

	FGuid ScriptUsageId;
	bool bIsValidForOutput;

	TWeakObjectPtr<UEdGraph> ScriptGraph;

	FDelegateHandle OnGraphChangedHandle;

	TMap<FObjectKey, UNiagaraStackModuleItem*> StackSpacerToModuleItemMap;
};
