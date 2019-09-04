// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "NiagaraNodeWithDynamicPins.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "Framework/Commands/UIAction.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "SNiagaraGraphPinAdd.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "SNiagaraParameterMapView.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeWithDynamicPins"

const FName UNiagaraNodeWithDynamicPins::AddPinSubCategory("DynamicAddPin");

void UNiagaraNodeWithDynamicPins::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// Check if an add pin was connected and convert it to a typed connection.
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	if (Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && Pin->PinType.PinSubCategory == AddPinSubCategory && Pin->LinkedTo.Num() > 0)
	{
		FNiagaraTypeDefinition LinkedPinType = Schema->PinToTypeDefinition(Pin->LinkedTo[0]);
		Pin->PinType = Schema->TypeDefinitionToPinType(LinkedPinType);
		Pin->PinName = Pin->LinkedTo[0]->GetFName();

		CreateAddPin(Pin->Direction);
		OnNewTypedPinAdded(Pin);
		MarkNodeRequiresSynchronization(__FUNCTION__, true);
		//GetGraph()->NotifyGraphChanged();
	}
}

UEdGraphPin* GetAddPin(TArray<UEdGraphPin*> Pins, EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == Direction &&
			Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && 
			Pin->PinType.PinSubCategory == UNiagaraNodeWithDynamicPins::AddPinSubCategory)
		{
			return Pin;
		}
	}
	return nullptr;
}

bool UNiagaraNodeWithDynamicPins::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType)
{
	return  InType != FNiagaraTypeDefinition::GetGenericNumericDef() && InType.GetScriptStruct() != nullptr;
}

UEdGraphPin* UNiagaraNodeWithDynamicPins::RequestNewTypedPin(EEdGraphPinDirection Direction, const FNiagaraTypeDefinition& Type)
{
	FString DefaultName;
	if (Direction == EGPD_Input)
	{
		TArray<UEdGraphPin*> InPins;
		GetInputPins(InPins);
		DefaultName = TEXT("Input ") + LexToString(InPins.Num());
	}
	else
	{
		TArray<UEdGraphPin*> OutPins;
		GetOutputPins(OutPins);
		DefaultName = TEXT("Output ") + LexToString(OutPins.Num());
	}
	return RequestNewTypedPin(Direction, Type, *DefaultName);
}

UEdGraphPin* UNiagaraNodeWithDynamicPins::RequestNewTypedPin(EEdGraphPinDirection Direction, const FNiagaraTypeDefinition& Type, const FName InName)
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	UEdGraphPin* AddPin = GetAddPin(GetAllPins(), Direction);
	checkf(AddPin != nullptr, TEXT("Add pin is missing"));
	AddPin->Modify();
	AddPin->PinType = Schema->TypeDefinitionToPinType(Type);
	AddPin->PinName = InName;

	CreateAddPin(Direction);
	OnNewTypedPinAdded(AddPin);
	MarkNodeRequiresSynchronization(__FUNCTION__, true);

	return AddPin;
}

void UNiagaraNodeWithDynamicPins::CreateAddPin(EEdGraphPinDirection Direction)
{
	if (!AllowDynamicPins())
	{
		return;
	}
	CreatePin(Direction, FEdGraphPinType(UEdGraphSchema_Niagara::PinCategoryMisc, AddPinSubCategory, nullptr, EPinContainerType::None, false, FEdGraphTerminalType()), TEXT("Add"));
}

bool UNiagaraNodeWithDynamicPins::IsAddPin(const UEdGraphPin* Pin) const
{
	return Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && 
		Pin->PinType.PinSubCategory == UNiagaraNodeWithDynamicPins::AddPinSubCategory;
}

bool UNiagaraNodeWithDynamicPins::CanRenamePin(const UEdGraphPin* Pin) const
{
	return IsAddPin(Pin) == false;
}

bool UNiagaraNodeWithDynamicPins::CanRemovePin(const UEdGraphPin* Pin) const
{
	return IsAddPin(Pin) == false;
}

bool UNiagaraNodeWithDynamicPins::CanMovePin(const UEdGraphPin* Pin) const
{
	return IsAddPin(Pin) == false;
}

void UNiagaraNodeWithDynamicPins::MoveDynamicPin(UEdGraphPin* Pin, int32 DirectionToMove)
{
	TArray<UEdGraphPin*> SameDirectionPins;
	if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		GetInputPins(SameDirectionPins);
	}
	else
	{
		GetOutputPins(SameDirectionPins);
	}

	for (int32 i = 0; i < SameDirectionPins.Num(); i++)
	{
		if (SameDirectionPins[i] == Pin)
		{
			if (i + DirectionToMove >= 0 && i + DirectionToMove < SameDirectionPins.Num())
			{
				Modify();
				UEdGraphPin* PinOld = SameDirectionPins[i + DirectionToMove];
				if (PinOld)
					PinOld->Modify();
				Pin->Modify();

				int32 RealPinIdx = INDEX_NONE;
				int32 SwapRealPinIdx = INDEX_NONE;
				Pins.Find(Pin, RealPinIdx);
				Pins.Find(PinOld, SwapRealPinIdx);
				
				Pins[SwapRealPinIdx] = Pin;
				Pins[RealPinIdx] = PinOld;
				//GetGraph()->NotifyGraphChanged();

				MarkNodeRequiresSynchronization(__FUNCTION__, true);
				break;
			}
		}
	}
}

void UNiagaraNodeWithDynamicPins::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	Super::GetContextMenuActions(Context);
	if (Context.Pin != nullptr)
	{
		Context.MenuBuilder->BeginSection("EdGraphSchema_NiagaraPinActions", LOCTEXT("EditPinMenuHeader", "Edit Pin"));
		if (CanRenamePin(Context.Pin))
		{
			UEdGraphPin* Pin = const_cast<UEdGraphPin*>(Context.Pin);
			TSharedRef<SWidget> RenameWidget =
				SNew(SBox)
				.WidthOverride(100)
				.Padding(FMargin(5, 0, 0, 0))
				[
					SNew(SEditableTextBox)
					.Text_UObject(this, &UNiagaraNodeWithDynamicPins::GetPinNameText, Pin)
					.OnTextCommitted_UObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::PinNameTextCommitted, Pin)
				];
			Context.MenuBuilder->AddWidget(RenameWidget, LOCTEXT("NameMenuItem", "Name"));
		}
		if (CanRemovePin(Context.Pin))
		{
			Context.MenuBuilder->AddMenuEntry(
				LOCTEXT("RemoveDynamicPin", "Remove pin"),
				LOCTEXT("RemoveDynamicPinToolTip", "Remove this pin and any connections."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::RemoveDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context.Pin))));
		}
		if (CanMovePin(Context.Pin))
		{
			TArray<UEdGraphPin*> SameDirectionPins;
			if (Context.Pin->Direction == EEdGraphPinDirection::EGPD_Input)
			{
				GetInputPins(SameDirectionPins);
			}
			else
			{
				GetOutputPins(SameDirectionPins);
			}
			int32 PinIdx = INDEX_NONE;
			SameDirectionPins.Find(const_cast<UEdGraphPin*>(Context.Pin), PinIdx);

			if (PinIdx != 0)
			{
				Context.MenuBuilder->AddMenuEntry(
					LOCTEXT("MoveDynamicPinUp", "Move pin up"),
					LOCTEXT("MoveDynamicPinToolTipUp", "Move this pin and any connections one slot up."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::MoveDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context.Pin), -1)));
			}
			if (PinIdx >= 0 && PinIdx < SameDirectionPins.Num() - 1)
			{
				Context.MenuBuilder->AddMenuEntry(
					LOCTEXT("MoveDynamicPinDown", "Move pin down"),
					LOCTEXT("MoveDynamicPinToolTipDown", "Move this pin and any connections one slot down."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::MoveDynamicPin, const_cast<UEdGraphPin*>(Context.Pin), 1)));
			}
		}
		Context.MenuBuilder->EndSection();
	}
}

void UNiagaraNodeWithDynamicPins::CollectAddPinActions(FGraphActionListBuilderBase& OutActions, bool& bOutCreateRemainingActions, UEdGraphPin* Pin)
{
	TArray<FNiagaraTypeDefinition> Types = FNiagaraTypeRegistry::GetRegisteredTypes();
	Types.Sort([](const FNiagaraTypeDefinition& A, const FNiagaraTypeDefinition& B) { return (A.GetNameText().ToLower().ToString() < B.GetNameText().ToLower().ToString()); });

	for (const FNiagaraTypeDefinition& RegisteredType : Types)
	{
		bool bAllowType = false;
		bAllowType = AllowNiagaraTypeForAddPin(RegisteredType);

		if (bAllowType)
		{
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetName()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);

			const FText DisplayName = RegisteredType.GetNameText();
			const FText Tooltip = FText::Format(LOCTEXT("AddButtonTypeEntryToolTipFormat", "Add a new {0} pin"), RegisteredType.GetNameText());
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
				FText::GetEmpty(), DisplayName, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateUObject(this, &UNiagaraNodeWithDynamicPins::AddParameter, Var, Pin)));

			OutActions.AddAction(Action);
		}
	}

	bOutCreateRemainingActions = false;
}

void UNiagaraNodeWithDynamicPins::AddParameter(FNiagaraVariable Parameter, UEdGraphPin* AddPin)
{
	FScopedTransaction AddNewPinTransaction(LOCTEXT("AddNewPinTransaction", "Add pin to node"));
	this->RequestNewTypedPin(AddPin->Direction, Parameter.GetType(), Parameter.GetName());
}

void UNiagaraNodeWithDynamicPins::RemoveDynamicPin(UEdGraphPin* Pin)
{
	RemovePin(Pin);
	MarkNodeRequiresSynchronization(__FUNCTION__, true);
}

FText UNiagaraNodeWithDynamicPins::GetPinNameText(UEdGraphPin* Pin) const
{
	return FText::FromName(Pin->PinName);
}


void UNiagaraNodeWithDynamicPins::PinNameTextCommitted(const FText& Text, ETextCommit::Type CommitType, UEdGraphPin* Pin)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FScopedTransaction RemovePinTransaction(LOCTEXT("RenamePinTransaction", "Rename pin"));
		Modify();
		FString PinOldName = Pin->PinName.ToString();
		Pin->PinName = *Text.ToString();
		OnPinRenamed(Pin, PinOldName);
		MarkNodeRequiresSynchronization(__FUNCTION__, true);
	}
}

void UNiagaraNodeWithDynamicPins::RemoveDynamicPinFromMenu(UEdGraphPin* Pin)
{
	FScopedTransaction RemovePinTransaction(LOCTEXT("RemovePinTransaction", "Remove pin"));
	RemoveDynamicPin(Pin);
}

void UNiagaraNodeWithDynamicPins::MoveDynamicPinFromMenu(UEdGraphPin* Pin, int32 DirectionToMove)
{
	FScopedTransaction MovePinTransaction(LOCTEXT("MovePinTransaction", "Moved pin"));
	MoveDynamicPin(Pin, DirectionToMove);
}

#undef LOCTEXT_NAMESPACE
