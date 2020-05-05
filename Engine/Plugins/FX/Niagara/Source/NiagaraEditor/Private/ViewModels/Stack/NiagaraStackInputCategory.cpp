// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackInputCategory.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraClipboard.h"

void UNiagaraStackInputCategory::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UNiagaraNodeFunctionCall& InModuleNode,
	UNiagaraNodeFunctionCall& InInputFunctionCallNode,
	FText InCategoryName,
	FString InOwnerStackItemEditorDataKey)
{
	bool bCategoryIsAdvanced = false;
	FString InputCategoryStackEditorDataKey = FString::Printf(TEXT("%s-InputCategory-%s"), *InInputFunctionCallNode.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), *InCategoryName.ToString());
	Super::Initialize(InRequiredEntryData, bCategoryIsAdvanced, InOwnerStackItemEditorDataKey, InputCategoryStackEditorDataKey);
	ModuleNode = &InModuleNode;
	InputFunctionCallNode = &InInputFunctionCallNode;
	CategoryName = InCategoryName;
	bShouldShowInStack = true;
	
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackInputCategory::FilterForVisibleCondition));
	AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackInputCategory::FilterForIsInlineEditConditionToggle));
}

const FText& UNiagaraStackInputCategory::GetCategoryName() const
{
	return CategoryName;
}

void UNiagaraStackInputCategory::ResetInputs()
{
	Inputs.Empty();
}

void UNiagaraStackInputCategory::AddInput(FName InInputParameterHandle, FNiagaraTypeDefinition InInputType, EStackParameterBehavior InParameterBehavior, bool bIsVisible)
{
	Inputs.Add({ InInputParameterHandle, InInputType, InParameterBehavior, bIsVisible });
}

void UNiagaraStackInputCategory::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	for (FInputParameterHandleAndType& Input : Inputs)
	{
		UNiagaraStackFunctionInput* InputChild = FindCurrentChildOfTypeByPredicate<UNiagaraStackFunctionInput>(CurrentChildren, [&](UNiagaraStackFunctionInput* CurrentInput) 
		{ 
			return CurrentInput->GetInputParameterHandle() == Input.ParameterHandle && CurrentInput->GetInputFunctionCallInitialScript() == InputFunctionCallNode->FunctionScript; 
		});

		if (InputChild == nullptr)
		{
			InputChild = NewObject<UNiagaraStackFunctionInput>(this);
			InputChild->Initialize(CreateDefaultChildRequiredData(), *ModuleNode, *InputFunctionCallNode,
				Input.ParameterHandle, Input.Type, Input.ParameterBehavior, GetOwnerStackItemEditorDataKey());
		}
		InputChild->bIsVisible = Input.bIsVisible;
		NewChildren.Add(InputChild);
	}
}

FText UNiagaraStackInputCategory::GetDisplayName() const
{
	return CategoryName;
}

bool UNiagaraStackInputCategory::GetShouldShowInStack() const
{
	bool bIsVisible = false;
	for (FInputParameterHandleAndType Input : Inputs)
	{
		if (Input.bIsVisible)
		{
			bIsVisible = true;
			break;
		}
	}
	return bIsVisible && bShouldShowInStack;
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackInputCategory::GetStackRowStyle() const
{
	return EStackRowStyle::ItemCategory;
}

bool UNiagaraStackInputCategory::GetIsEnabled() const
{
	return InputFunctionCallNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

void UNiagaraStackInputCategory::SetShouldShowInStack(bool bInShouldShowInStack)
{
	bShouldShowInStack = bInShouldShowInStack;
}

void UNiagaraStackInputCategory::ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const
{
	TArray<UNiagaraStackFunctionInput*> ChildInputs;
	GetUnfilteredChildrenOfType(ChildInputs);
	for (UNiagaraStackFunctionInput* ChildInput : ChildInputs)
	{
		const UNiagaraClipboardFunctionInput* FunctionInput = ChildInput->ToClipboardFunctionInput(InOuter);
		if (FunctionInput != nullptr)
		{
			OutClipboardFunctionInputs.Add(FunctionInput);
		}
	}
}

template<typename Predicate>
void SetValuesFromFunctionInputsInternal(UNiagaraStackInputCategory* Category, const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs, Predicate InputMatchesFilter)
{
	TArray<UNiagaraStackFunctionInput*> ChildInputs;
	Category->GetUnfilteredChildrenOfType(ChildInputs);
	for (UNiagaraStackFunctionInput* ChildInput : ChildInputs)
	{
		for (const UNiagaraClipboardFunctionInput* ClipboardFunctionInput : ClipboardFunctionInputs)
		{
			if (InputMatchesFilter(ChildInput) && ChildInput->GetInputParameterHandle().GetName() == ClipboardFunctionInput->InputName && ChildInput->GetInputType() == ClipboardFunctionInput->InputType)
			{
				ChildInput->SetValueFromClipboardFunctionInput(*ClipboardFunctionInput);
			}
		}
	}
}

void  UNiagaraStackInputCategory::SetStaticSwitchValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	SetValuesFromFunctionInputsInternal(this, ClipboardFunctionInputs, [](UNiagaraStackFunctionInput* ChildInput) { return ChildInput->IsStaticParameter(); });
}

void  UNiagaraStackInputCategory::SetStandardValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	SetValuesFromFunctionInputsInternal(this, ClipboardFunctionInputs, [](UNiagaraStackFunctionInput* ChildInput) { return ChildInput->IsStaticParameter() == false; });
}

bool UNiagaraStackInputCategory::FilterForVisibleCondition(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackFunctionInput* StackFunctionInputChild = Cast<UNiagaraStackFunctionInput>(&Child);
	return StackFunctionInputChild == nullptr || StackFunctionInputChild->GetShouldPassFilterForVisibleCondition();
}

bool UNiagaraStackInputCategory::FilterForIsInlineEditConditionToggle(const UNiagaraStackEntry& Child) const
{
	const UNiagaraStackFunctionInput* StackFunctionInputChild = Cast<UNiagaraStackFunctionInput>(&Child);
	return StackFunctionInputChild == nullptr || StackFunctionInputChild->GetIsInlineEditConditionToggle() == false;
}

