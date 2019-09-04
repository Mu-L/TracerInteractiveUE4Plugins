// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeCustomHlsl.generated.h"

class UNiagaraScript;

UCLASS(MinimalAPI)
class UNiagaraNodeCustomHlsl : public UNiagaraNodeFunctionCall
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Function", meta = (MultiLine = true))
	FString CustomHlsl;

	UPROPERTY()
	ENiagaraScriptUsage ScriptUsage;

	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual FLinearColor GetNodeTitleColor() const override;

	FText NIAGARAEDITOR_API GetHlslText() const;
	void NIAGARAEDITOR_API OnCustomHlslTextCommitted(const FText& InText, ETextCommit::Type InType);

	bool GetTokens(TArray<FString>& OutTokens) const;

	virtual void BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive = true, bool bFilterForCompilation = true) const override;

	// Replace items in the tokens array if they start with the src string or optionally src string and a namespace delimiter
	static void ReplaceExactMatchTokens(TArray<FString>& Tokens, const FString& SrcString, const FString& ReplaceString, bool bAllowNamespaceSeparation);
	static FNiagaraVariable StripVariableToBaseType(const FNiagaraVariable& InVar);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	void InitAsCustomHlslDynamicInput(const FNiagaraTypeDefinition& OutputType);

#endif
protected:
	virtual bool AllowDynamicPins() const override { return true; }

	virtual bool CanRenamePin(const UEdGraphPin* Pin) const override { return UNiagaraNodeWithDynamicPins::CanRenamePin(Pin); }
	virtual bool CanRemovePin(const UEdGraphPin* Pin) const override {
		return UNiagaraNodeWithDynamicPins::CanRemovePin(Pin);
	}
	virtual bool CanMovePin(const UEdGraphPin* Pin) const override {
		return UNiagaraNodeWithDynamicPins::CanMovePin(Pin);
	}

	/** Called when a new typed pin is added by the user. */
	virtual void OnNewTypedPinAdded(UEdGraphPin* NewPin) override;

	/** Called when a pin is renamed. */
	virtual void OnPinRenamed(UEdGraphPin* RenamedPin, const FString& OldPinName) override;
	
	/** Removes a pin from this node with a transaction. */
	virtual void RemoveDynamicPin(UEdGraphPin* Pin) override;

	virtual void MoveDynamicPin(UEdGraphPin* Pin, int32 DirectionToMove) override;

	void RebuildSignatureFromPins();

};
