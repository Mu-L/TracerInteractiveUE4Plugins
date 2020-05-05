// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraActions.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "EdGraphSchema_Niagara.h"
#include "Widgets/SWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "ScopedTransaction.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraActions"

/************************************************************************/
/* FNiagaraMenuAction													*/
/************************************************************************/
FNiagaraMenuAction::FNiagaraMenuAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, FOnExecuteStackAction InAction, int32 InSectionID)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID)
	, Action(InAction)
{}

FNiagaraMenuAction::FNiagaraMenuAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, FOnExecuteStackAction InAction, FCanExecuteStackAction InCanPerformAction, int32 InSectionID)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID)
	, Action(InAction)
	, CanPerformAction(InCanPerformAction)
{}

TOptional<FNiagaraVariable> FNiagaraMenuAction::GetParameterVariable() const
{
	return ParameterVariable;
}

void FNiagaraMenuAction::SetParamterVariable(const FNiagaraVariable& InParameterVariable)
{
	ParameterVariable = InParameterVariable;
}

/************************************************************************/
/* FNiagaraParameterAction												*/
/************************************************************************/
FNiagaraParameterAction::FNiagaraParameterAction(const FNiagaraVariable& InParameter,
	const TArray<FNiagaraGraphParameterReferenceCollection>& InReferenceCollection,
	FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords,
	TSharedPtr<TArray<FName>> ParameterWithNamespaceModifierRenamePending, 
	int32 InSectionID)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID)
	, Parameter(InParameter)
	, ReferenceCollection(InReferenceCollection)
	, bIsExternallyReferenced(false)
	, ParameterWithNamespaceModifierRenamePendingWeak(ParameterWithNamespaceModifierRenamePending)
{
}

FNiagaraParameterAction::FNiagaraParameterAction(const FNiagaraVariable& InParameter,
	FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords,
	TSharedPtr<TArray<FName>> ParameterWithNamespaceModifierRenamePending, 
	int32 InSectionID)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID)
	, Parameter(InParameter)
	, bIsExternallyReferenced(false)
	, ParameterWithNamespaceModifierRenamePendingWeak(ParameterWithNamespaceModifierRenamePending)
{
}

bool FNiagaraParameterAction::GetIsNamespaceModifierRenamePending() const
{
	TSharedPtr<TArray<FName>> ParameterNamesWithNamespaceModifierRenamePending = ParameterWithNamespaceModifierRenamePendingWeak.Pin();
	if (ParameterNamesWithNamespaceModifierRenamePending.IsValid())
	{
		return ParameterNamesWithNamespaceModifierRenamePending->Contains(Parameter.GetName());
	}
	return false;
}

void FNiagaraParameterAction::SetIsNamespaceModifierRenamePending(bool bIsNamespaceModifierRenamePending)
{
	TSharedPtr<TArray<FName>> ParameterNamesWithNamespaceModifierRenamePending = ParameterWithNamespaceModifierRenamePendingWeak.Pin();
	if (ParameterNamesWithNamespaceModifierRenamePending.IsValid())
	{
		if (bIsNamespaceModifierRenamePending)
		{
			ParameterNamesWithNamespaceModifierRenamePending->AddUnique(Parameter.GetName());
		}
		else
		{
			ParameterNamesWithNamespaceModifierRenamePending->Remove(Parameter.GetName());
		}
	}
}

/************************************************************************/
/* FNiagaraScriptVarAndViewInfoAction									*/
/************************************************************************/
FNiagaraScriptVarAndViewInfoAction::FNiagaraScriptVarAndViewInfoAction(const FNiagaraScriptVariableAndViewInfo& InScriptVariableAndViewInfo,
	FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, int32 InSectionID)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID)
	, ScriptVariableAndViewInfo(InScriptVariableAndViewInfo)
{
}


/************************************************************************/
/* FNiagaraParameterGraphDragOperation									*/
/************************************************************************/
FNiagaraParameterGraphDragOperation::FNiagaraParameterGraphDragOperation()
	: bControlDrag(false)
	, bAltDrag(false)
{

}

TSharedRef<FNiagaraParameterGraphDragOperation> FNiagaraParameterGraphDragOperation::New(const TSharedPtr<FEdGraphSchemaAction>& InActionNode)
{
	TSharedRef<FNiagaraParameterGraphDragOperation> Operation = MakeShareable(new FNiagaraParameterGraphDragOperation);
	Operation->SourceAction = InActionNode;
	Operation->Construct();
	return Operation;
}

void FNiagaraParameterGraphDragOperation::HoverTargetChanged()
{
	if (SourceAction.IsValid())
	{
		if (!HoveredCategoryName.IsEmpty())
		{
			return;
		}
		else if (HoveredAction.IsValid())
		{
			const FSlateBrush* StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
			TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SourceAction);
			if (ParameterAction.IsValid())
			{
				const FLinearColor TypeColor = UEdGraphSchema_Niagara::GetTypeColor(ParameterAction->GetParameter().GetType());
				SetSimpleFeedbackMessage(StatusSymbol, TypeColor, SourceAction->GetMenuDescription());
			}
			return;
		}
	}

	FGraphSchemaActionDragDropAction::HoverTargetChanged();
}

FReply FNiagaraParameterGraphDragOperation::DroppedOnNode(FVector2D ScreenPosition, FVector2D GraphPosition)
{
	FNiagaraParameterAction* ParameterAction = (FNiagaraParameterAction*)SourceAction.Get();
	if (ParameterAction)
	{
		const FNiagaraVariable& Parameter = ParameterAction->GetParameter();
		if (UNiagaraNodeParameterMapGet* GetMapNode = Cast<UNiagaraNodeParameterMapGet>(GetHoveredNode()))
		{
			FScopedTransaction AddNewPinTransaction(LOCTEXT("Drop Onto Get Pin", "Drop parameter onto Get node"));
			GetMapNode->Modify();
			UEdGraphPin* Pin = GetMapNode->RequestNewTypedPin(EGPD_Output, Parameter.GetType(), Parameter.GetName());
			GetMapNode->CancelEditablePinName(FText::GetEmpty(), Pin);
		} 
		else if (UNiagaraNodeParameterMapSet* SetMapNode = Cast<UNiagaraNodeParameterMapSet>(GetHoveredNode()))
		{
			FScopedTransaction AddNewPinTransaction(LOCTEXT("Drop Onto Set Pin", "Drop parameter onto Set node"));
			SetMapNode->Modify();
			UEdGraphPin* Pin = SetMapNode->RequestNewTypedPin(EGPD_Input, Parameter.GetType(), Parameter.GetName());
			SetMapNode->CancelEditablePinName(FText::GetEmpty(), Pin);
		}
	}

	return FReply::Handled();
}

FReply FNiagaraParameterGraphDragOperation::DroppedOnPanel(const TSharedRef<SWidget>& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph)
{
	if (Graph.GetSchema()->IsA<UEdGraphSchema_Niagara>())
	{
		FNiagaraParameterAction* ParameterAction = (FNiagaraParameterAction*)SourceAction.Get();
		if (ParameterAction)
		{
			FNiagaraParameterNodeConstructionParams NewNodeParams;
			NewNodeParams.Graph = &Graph;
			NewNodeParams.GraphPosition = GraphPosition;
			NewNodeParams.Parameter = ParameterAction->GetParameter();

			// Take into account current state of modifier keys in case the user changed his mind
			FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
			const bool bModifiedKeysActive = ModifierKeys.IsControlDown() || ModifierKeys.IsAltDown();
			const bool bAutoCreateGetter = bModifiedKeysActive ? ModifierKeys.IsControlDown() : bControlDrag;
			const bool bAutoCreateSetter = bModifiedKeysActive ? ModifierKeys.IsAltDown() : bAltDrag;
			// Handle Getter/Setters
			if (bAutoCreateGetter || bAutoCreateSetter)
			{
				if (bAutoCreateGetter)
				{
					MakeGetMap(NewNodeParams);
				}
				if (bAutoCreateSetter)
				{
					MakeSetMap(NewNodeParams);
				}
			}
			// Show selection menu
			else
			{
				FMenuBuilder MenuBuilder(true, NULL);
				const FText ParameterNameText = FText::FromName(NewNodeParams.Parameter.GetName());

				MenuBuilder.BeginSection("NiagaraParameterDroppedOnPanel", ParameterNameText);
				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("CreateGetMap", "Get Map including {0}"), ParameterNameText),
					FText::Format(LOCTEXT("CreateGetMapToolTip", "Create Getter for variable '{0}'\n(Ctrl-drag to automatically create a getter)"), ParameterNameText),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FNiagaraParameterGraphDragOperation::MakeGetMap, NewNodeParams), 
						FCanExecuteAction())
				);

				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("CreateSetMap", "Set Map including {0}"), ParameterNameText),
					FText::Format(LOCTEXT("CreateSetMapToolTip", "Create Set Map for parameter '{0}'\n(Alt-drag to automatically create a setter)"), ParameterNameText),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FNiagaraParameterGraphDragOperation::MakeSetMap, NewNodeParams),
						FCanExecuteAction())
				);

				TSharedRef< SWidget > PanelWidget = Panel;
				// Show dialog to choose getter vs setter
				FSlateApplication::Get().PushMenu(
					PanelWidget,
					FWidgetPath(),
					MenuBuilder.MakeWidget(),
					ScreenPosition,
					FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
				);

				MenuBuilder.EndSection();
			}
		}
	}

	return FReply::Handled();
}


bool FNiagaraParameterGraphDragOperation::IsCurrentlyHoveringNode(const UEdGraphNode* TestNode) const
{
	return TestNode == GetHoveredNode();
}

void FNiagaraParameterGraphDragOperation::MakeGetMap(FNiagaraParameterNodeConstructionParams InParams)
{

	FScopedTransaction AddNewPinTransaction(LOCTEXT("MakeGetMap", "Make Get Node For Variable"));
	check(InParams.Graph);
	InParams.Graph->Modify();
	FGraphNodeCreator<UNiagaraNodeParameterMapGet> GetNodeCreator(*InParams.Graph);
	UNiagaraNodeParameterMapGet* GetNode = GetNodeCreator.CreateNode();
	GetNode->NodePosX = InParams.GraphPosition.X;
	GetNode->NodePosY = InParams.GraphPosition.Y;
	GetNodeCreator.Finalize();
	GetNode->RequestNewTypedPin(EGPD_Output, InParams.Parameter.GetType(), InParams.Parameter.GetName());
}

void FNiagaraParameterGraphDragOperation::MakeSetMap(FNiagaraParameterNodeConstructionParams InParams)
{
	FScopedTransaction AddNewPinTransaction(LOCTEXT("MakeSetMap", "Make Set Node For Variable"));
	check(InParams.Graph);
	InParams.Graph->Modify();
	FGraphNodeCreator<UNiagaraNodeParameterMapSet> SetNodeCreator(*InParams.Graph);
	UNiagaraNodeParameterMapSet* SetNode = SetNodeCreator.CreateNode();
	SetNode->NodePosX = InParams.GraphPosition.X;
	SetNode->NodePosY = InParams.GraphPosition.Y;
	SetNodeCreator.Finalize();
	SetNode->RequestNewTypedPin(EGPD_Input, InParams.Parameter.GetType(), InParams.Parameter.GetName());
}

EVisibility FNiagaraParameterGraphDragOperation::GetIconVisible() const
{
	return EVisibility::Collapsed;
}

EVisibility FNiagaraParameterGraphDragOperation::GetErrorIconVisible() const
{
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
