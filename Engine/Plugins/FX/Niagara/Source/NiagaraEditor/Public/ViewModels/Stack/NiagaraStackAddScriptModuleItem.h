// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraStackAddModuleItem.h"
#include "EdGraph/EdGraphSchema.h"
#include "NiagaraStackAddScriptModuleItem.generated.h"

class UNiagaraNodeOutput;
class UNiagaraStackEditorData;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackAddScriptModuleItem : public UNiagaraStackAddModuleItem
{
	GENERATED_BODY()

public:
	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel, TSharedRef<FNiagaraEmitterViewModel> InEmitterViewModel, UNiagaraStackEditorData& InStackEditorData, UNiagaraNodeOutput& InOutputNode, int32 InTargetIndex);

	virtual EDisplayMode GetDisplayMode() const override;

	virtual void GetAvailableParameters(TArray<FNiagaraVariable>& OutAvailableParameterVariables) const override;

	virtual void GetNewParameterAvailableTypes(TArray<FNiagaraTypeDefinition>& OutAvailableTypes) const override;

	virtual TOptional<FName> GetNewParameterNamespace() const override;

	virtual ENiagaraScriptUsage GetOutputUsage() const override;

	virtual UNiagaraNodeOutput* GetOrCreateOutputNode() override;

	virtual int32 GetTargetIndex() const override;

	virtual FName GetItemBackgroundName() const override;

private:
	TWeakObjectPtr<UNiagaraNodeOutput> OutputNode;

	int32 TargetIndex;
};