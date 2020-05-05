// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "NiagaraEditorModule.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItemLinkedInputCollection.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackInputCategory.h"
#include "ViewModels/Stack/NiagaraStackModuleItemOutputCollection.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraScript.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraCommon.h"
#include "NiagaraConstants.h"
#include "Widgets/SWidget.h"
#include "NiagaraActions.h"
#include "NiagaraClipboard.h"
#include "NiagaraConvertInPlaceUtilityBase.h"
#include "Framework/Notifications/NotificationManager.h"
#include "NiagaraMessageManager.h"

#include "ScopedTransaction.h"
#include "NiagaraScriptVariable.h"

// TODO: Remove these
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "NiagaraStackModuleItem"

TArray<ENiagaraScriptUsage> UsagePriority = { // Ordered such as the highest priority has the largest index
	ENiagaraScriptUsage::ParticleUpdateScript,
	ENiagaraScriptUsage::ParticleSpawnScript,
	ENiagaraScriptUsage::EmitterUpdateScript,
	ENiagaraScriptUsage::EmitterSpawnScript,
	ENiagaraScriptUsage::SystemUpdateScript,
	ENiagaraScriptUsage::SystemSpawnScript };

UNiagaraNodeOutput* GetOutputNodeForModuleDependency(ENiagaraScriptUsage DependantUsage, UNiagaraScript* DependencyScript, UNiagaraSystem& System, const FNiagaraEmitterHandle* EmitterHandle, FNiagaraModuleDependency Dependency)
{
	UNiagaraNodeOutput* TargetOutputNode = nullptr;
	if (DependencyScript)
	{
		UNiagaraScript* OutputScript = nullptr;
		TArray<ENiagaraScriptUsage> SupportedUsages = UNiagaraScript::GetSupportedUsageContextsForBitmask(DependencyScript->ModuleUsageBitmask);

		if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::AllScripts)
		{
			int32 ClosestDistance = MAX_int32;
			int32 DependantIndex = UsagePriority.IndexOfByPredicate(
				[&](const ENiagaraScriptUsage CurrentUsage)
			{
				return UNiagaraScript::IsEquivalentUsage(DependantUsage, CurrentUsage);
			});

			for (ENiagaraScriptUsage PossibleUsage : SupportedUsages)
			{
				int32 PossibleIndex = UsagePriority.IndexOfByPredicate(
					[&](const ENiagaraScriptUsage CurrentUsage)
				{
					return UNiagaraScript::IsEquivalentUsage(PossibleUsage, CurrentUsage);
				});

				if (PossibleIndex == INDEX_NONE)
				{
					// This usage isn't in the execution flow so check the next one.
					continue;
				}

				int32 Distance = PossibleIndex - DependantIndex;
				bool bCorrectOrder = (Dependency.Type == ENiagaraModuleDependencyType::PreDependency && Distance >= 0) || (Dependency.Type == ENiagaraModuleDependencyType::PostDependency && Distance <= 0);
				if ((FMath::Abs(Distance) < ClosestDistance) && bCorrectOrder)
				{
					ClosestDistance = Distance;
					FGuid EmitterHandleId = EmitterHandle != nullptr ? EmitterHandle->GetId() : FGuid();
					OutputScript = FNiagaraEditorUtilities::GetScriptFromSystem(System, EmitterHandleId, PossibleUsage, FGuid());
				}
			}
		}
		else if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::SameScript)
		{
			if (SupportedUsages.Contains(DependantUsage))
			{
				FGuid EmitterHandleId = EmitterHandle != nullptr ? EmitterHandle->GetId() : FGuid();
				OutputScript = FNiagaraEditorUtilities::GetScriptFromSystem(System, EmitterHandleId, DependantUsage, FGuid());
			}
		}

		if (OutputScript != nullptr)
		{
			TargetOutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*OutputScript);
		}
	}
	return TargetOutputNode;
}

UNiagaraStackModuleItem::UNiagaraStackModuleItem()
	: FunctionCallNode(nullptr)
	, bCanRefresh(false)
	, InputCollection(nullptr)
	, bIsModuleScriptReassignmentPending(false)
{
}

UNiagaraNodeFunctionCall& UNiagaraStackModuleItem::GetModuleNode() const
{
	return *FunctionCallNode;
}

void UNiagaraStackModuleItem::Initialize(FRequiredEntryData InRequiredEntryData, INiagaraStackItemGroupAddUtilities* InGroupAddUtilities, UNiagaraNodeFunctionCall& InFunctionCallNode)
{
	checkf(FunctionCallNode == nullptr, TEXT("Can not set the node more than once."));
	FString ModuleStackEditorDataKey = InFunctionCallNode.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
	Super::Initialize(InRequiredEntryData, ModuleStackEditorDataKey);
	GroupAddUtilities = InGroupAddUtilities;
	FunctionCallNode = &InFunctionCallNode;
	OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*FunctionCallNode);

	// We do not need to include child filters for NiagaraNodeAssignments as they do not display their output or linked input collections
	if (!FunctionCallNode->IsA<UNiagaraNodeAssignment>())
	{
		AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackModuleItem::FilterOutputCollection));
		AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackModuleItem::FilterLinkedInputCollection));
	}

	FNiagaraMessageManager::Get()->GetOnRequestRefresh().AddUObject(this, &UNiagaraStackModuleItem::OnMessageManagerRefresh);
}

FText UNiagaraStackModuleItem::GetDisplayName() const
{
	return FunctionCallNode->GetNodeTitle(ENodeTitleType::ListView);
}

UObject* UNiagaraStackModuleItem::GetDisplayedObject() const
{
	return FunctionCallNode;
}

FText UNiagaraStackModuleItem::GetTooltipText() const
{
	if (FunctionCallNode != nullptr)
	{
		return FunctionCallNode->GetTooltipText();
	}
	else
	{
		return FText();
	}
}

INiagaraStackItemGroupAddUtilities* UNiagaraStackModuleItem::GetGroupAddUtilities()
{
	return GroupAddUtilities;
}

void UNiagaraStackModuleItem::FinalizeInternal()
{
	FNiagaraMessageManager::Get()->GetOnRequestRefresh().RemoveAll(this);
	Super::FinalizeInternal();
}

void UNiagaraStackModuleItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	bCanRefresh = false;
	bCanMoveAndDeleteCache.Reset();
	bIsScratchModuleCache.Reset();

	if (FunctionCallNode != nullptr && FunctionCallNode->ScriptIsValid())
	{
		// Determine if meta-data requires that we add our own refresh button here.
		if (FunctionCallNode->FunctionScript)
		{
			UNiagaraScriptSource* Source = CastChecked<UNiagaraScriptSource>(FunctionCallNode->FunctionScript->GetSource());
			UNiagaraGraph* Graph = CastChecked<UNiagaraGraph>(Source->NodeGraph);
			const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& MetaDataMap = Graph->GetAllMetaData();
			auto Iter = MetaDataMap.CreateConstIterator();
			while (Iter)
			{
				// TODO: This should never be null, but somehow it is in some assets so guard this to prevent crashes
				// until we have better repro steps.
				if (Iter.Value() != nullptr)
				{
					auto PropertyIter = Iter.Value()->Metadata.PropertyMetaData.CreateConstIterator();
					while (PropertyIter)
					{
						if (PropertyIter.Key() == (TEXT("DisplayNameArg0")))
						{
							bCanRefresh = true;
						}
						++PropertyIter;
					}
				}
				++Iter;
			}
		}

		if (InputCollection == nullptr)
		{
			TArray<FString> InputParameterHandlePath;
			InputCollection = NewObject<UNiagaraStackFunctionInputCollection>(this);
			InputCollection->Initialize(CreateDefaultChildRequiredData(), *FunctionCallNode, *FunctionCallNode, GetStackEditorDataKey());
		}

		// NiagaraNodeAssignments should not display OutputCollection and LinkedInputCollection as they effectively handle this through their InputCollection 
		if (!FunctionCallNode->IsA<UNiagaraNodeAssignment>())
		{

			if (LinkedInputCollection == nullptr)
			{
				LinkedInputCollection = NewObject<UNiagaraStackModuleItemLinkedInputCollection>(this);
				LinkedInputCollection->Initialize(CreateDefaultChildRequiredData(), *FunctionCallNode);
				LinkedInputCollection->AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackModuleItem::FilterLinkedInputCollectionChild));
			}

			if (OutputCollection == nullptr)
			{
				OutputCollection = NewObject<UNiagaraStackModuleItemOutputCollection>(this);
				OutputCollection->Initialize(CreateDefaultChildRequiredData(), *FunctionCallNode);
				OutputCollection->AddChildFilter(FOnFilterChild::CreateUObject(this, &UNiagaraStackModuleItem::FilterOutputCollectionChild));
			}

			InputCollection->SetShouldShowInStack(GetStackEditorData().GetShowOutputs() || GetStackEditorData().GetShowLinkedInputs());

			NewChildren.Add(InputCollection);
			NewChildren.Add(LinkedInputCollection);
			NewChildren.Add(OutputCollection);
		
		}
		else
		{
			// We do not show the expander arrow for InputCollections of NiagaraNodeAssignments as they only have this one collection
			InputCollection->SetShouldShowInStack(false);

			NewChildren.Add(InputCollection);

			UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(FunctionCallNode);
			if (AssignmentNode->GetAssignmentTargets().Num() == 0)
			{
				FText EmptyAssignmentNodeMessageText = LOCTEXT("EmptyAssignmentNodeMessage", "No Parameters\n\nTo add a parameter use the add button in the header, or drag a parameter from the parameters tab to the header.");
				UNiagaraStackItemTextContent* EmptyAssignmentNodeMessage = FindCurrentChildOfTypeByPredicate<UNiagaraStackItemTextContent>(NewChildren,
					[&](UNiagaraStackItemTextContent* CurrentStackItemTextContent) { return CurrentStackItemTextContent->GetDisplayName().IdenticalTo(EmptyAssignmentNodeMessageText); });

				if (EmptyAssignmentNodeMessage == nullptr)
				{
					EmptyAssignmentNodeMessage = NewObject<UNiagaraStackItemTextContent>(this);
					EmptyAssignmentNodeMessage->Initialize(CreateDefaultChildRequiredData(), EmptyAssignmentNodeMessageText, false, GetStackEditorDataKey());
				}
				NewChildren.Add(EmptyAssignmentNodeMessage);
			}
		}
	}

	RefreshIsEnabled();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
	RefreshIssues(NewIssues);
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackModuleItem::CanDropInternal(const FDropRequest& DropRequest)
{
	if (DropRequest.DragDropOperation->IsOfType<FNiagaraParameterDragOperation>() && DropRequest.DropOptions != UNiagaraStackEntry::EDropOptions::Overview && FunctionCallNode->IsA<UNiagaraNodeAssignment>())
	{
		TSharedRef<const FNiagaraParameterDragOperation> ParameterDragDropOp = StaticCastSharedRef<const FNiagaraParameterDragOperation>(DropRequest.DragDropOperation);
		TSharedPtr<const FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<const FNiagaraParameterAction>(ParameterDragDropOp->GetSourceAction());
		if (ParameterAction.IsValid())
		{
			if (FNiagaraStackGraphUtilities::CanWriteParameterFromUsage(ParameterAction->GetParameter(), OutputNode->GetUsage()))
			{
				return FDropRequestResponse(EItemDropZone::OntoItem, LOCTEXT("DropParameterToAdd", "Add this parameter to this 'Set Variables' node."));
			}
			else
			{
				return FDropRequestResponse(TOptional<EItemDropZone>(), LOCTEXT("CantDropParameterByUsage", "Can not drop this parameter here because\nit can't be written in this usage context."));
			}
		}
	}
	return TOptional<FDropRequestResponse>();
}

TOptional<UNiagaraStackEntry::FDropRequestResponse> UNiagaraStackModuleItem::DropInternal(const FDropRequest& DropRequest)
{
	// If the drop was allowed from the can drop just return the drop zone here since it will be handled by the drop target in the stack control.
	// TODO: Unify the drop target dropping with the existing drag/drop code.
	if (DropRequest.DropOptions != UNiagaraStackEntry::EDropOptions::Overview)
	{
		return FDropRequestResponse(DropRequest.DropZone);
	}
	return TOptional<FDropRequestResponse>();
}

void UNiagaraStackModuleItem::RefreshIssues(TArray<FStackIssue>& NewIssues)
{
	if (!GetIsEnabled())
	{
		NewIssues.Empty();
		return;
	}

	if (FunctionCallNode != nullptr)
	{
		if (FunctionCallNode->FunctionScript != nullptr)
		{
			if (FunctionCallNode->FunctionScript->bDeprecated)
			{
				FText ModuleScriptDeprecationShort = LOCTEXT("ModuleScriptDeprecationShort", "Deprecated module");
				if (CanMoveAndDelete())
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("ScriptName"), FText::FromString(FunctionCallNode->GetFunctionName()));

					if (FunctionCallNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						Args.Add(TEXT("Recommendation"), FText::FromString(FunctionCallNode->FunctionScript->DeprecationRecommendation->GetPathName()));
					}

					if (FunctionCallNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						Args.Add(TEXT("Message"), FunctionCallNode->FunctionScript->DeprecationMessage);
					}

					FText FormatString = LOCTEXT("ModuleScriptDeprecationUnknownLong", "The script asset for the assigned module {ScriptName} has been deprecated.");

					if (FunctionCallNode->FunctionScript->DeprecationRecommendation != nullptr &&
						FunctionCallNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						FormatString = LOCTEXT("ModuleScriptDeprecationMessageAndRecommendationLong", "The script asset for the assigned module {ScriptName} has been deprecated. Reason:\n{Message}.\nSuggested replacement: {Recommendation}");
					}
					else if (FunctionCallNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						FormatString = LOCTEXT("ModuleScriptDeprecationLong", "The script asset for the assigned module {ScriptName} has been deprecated. Suggested replacement: {Recommendation}");
					}
					else if (FunctionCallNode->FunctionScript->DeprecationMessage.IsEmptyOrWhitespace() == false)
					{
						FormatString = LOCTEXT("ModuleScriptDeprecationMessageLong", "The script asset for the assigned module {ScriptName} has been deprecated. Reason:\n{Message}");
					}

					FText LongMessage = FText::Format(FormatString, Args);

					int32 AddIdx = NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Warning,
						ModuleScriptDeprecationShort,
						LongMessage,
						GetStackEditorDataKey(),
						false,
						{
							FStackIssueFix(
								LOCTEXT("SelectNewModuleScriptFix", "Select a new module script"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->bIsModuleScriptReassignmentPending = true; })),
							FStackIssueFix(
								LOCTEXT("DeleteFix", "Delete this module"),
								FStackIssueFixDelegate::CreateLambda([this]() { this->Delete(); }))
						}));

					if (FunctionCallNode->FunctionScript->DeprecationRecommendation != nullptr)
					{
						NewIssues[AddIdx].InsertFix(0,
							FStackIssueFix(
							LOCTEXT("SelectNewModuleScriptFixUseRecommended", "Use recommended replacement and keep a disabled backup"),
							FStackIssueFixDelegate::CreateLambda([this]() 
							{
									if (DeprecationDelegate.IsBound())
									{
										DeprecationDelegate.Execute(this);
									}
							})));
					}
				}
				else
				{
					NewIssues.Add(FStackIssue(
						EStackIssueSeverity::Warning,
						ModuleScriptDeprecationShort,
						FText::Format(LOCTEXT("ModuleScriptDeprecationFixParentLong", "The script asset for the assigned module {0} has been deprecated.\nThis module is inherited and this issue must be fixed in the parent emitter.\nYou will need to touch up this instance once that is done."),
						FText::FromString(FunctionCallNode->GetFunctionName())),
						GetStackEditorDataKey(),
						false));
				}
			}

			if (FunctionCallNode->FunctionScript->bExperimental)
			{
				FText ErrorMessage;
				if (FunctionCallNode->FunctionScript->ExperimentalMessage.IsEmptyOrWhitespace())
				{
					ErrorMessage = FText::Format(LOCTEXT("ModuleScriptExperimental", "The script asset for this module is experimental, use with care!"), FText::FromString(FunctionCallNode->GetFunctionName()));
				}
				else
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("Module"), FText::FromString(FunctionCallNode->GetFunctionName()));
					Args.Add(TEXT("Message"), FunctionCallNode->FunctionScript->ExperimentalMessage);
					ErrorMessage = FText::Format(LOCTEXT("ModuleScriptExperimentalReason", "The script asset for this module is marked as experimental, reason:\n{Message}."), Args);
				}

				NewIssues.Add(FStackIssue(
					EStackIssueSeverity::Info,
					LOCTEXT("ModuleScriptExperimentalShort", "Experimental module"),
					ErrorMessage,
					GetStackEditorDataKey(),
					true));
			}
		}

		TArray<TSharedRef<const INiagaraMessage>> Messages = FNiagaraMessageManager::Get()->GetMessagesForAssetKeyAndObjectKey(
			GetSystemViewModel()->GetMessageLogGuid(), FObjectKey(FunctionCallNode));
		for (TSharedRef<const INiagaraMessage> Message : Messages)
		{
			// Sometimes compile errors with the same info are generated, so guard against duplicates here.
			FStackIssue Issue = FNiagaraStackGraphUtilities::MessageManagerMessageToStackIssue(Message, GetStackEditorDataKey());
			if (NewIssues.ContainsByPredicate([&Issue](const FStackIssue& NewIssue)
				{ return NewIssue.GetUniqueIdentifier() == Issue.GetUniqueIdentifier(); }) == false)
			{
				NewIssues.Add(Issue);
			}
		}

		if (FunctionCallNode->FunctionScript == nullptr && FunctionCallNode->GetClass() == UNiagaraNodeFunctionCall::StaticClass())
		{
			FText ModuleScriptMissingShort = LOCTEXT("ModuleScriptMissingShort", "Missing module script");
			if (CanMoveAndDelete())
			{
				NewIssues.Add(FStackIssue(
					EStackIssueSeverity::Error,
					ModuleScriptMissingShort,
					FText::Format(LOCTEXT("ModuleScriptMissingLong", "The script asset for the assigned module {0} is missing."), FText::FromString(FunctionCallNode->GetFunctionName())),
					GetStackEditorDataKey(),
					false,
					{
						FStackIssueFix(
							LOCTEXT("SelectNewModuleScriptFix", "Select a new module script"),
							FStackIssueFixDelegate::CreateLambda([this]() { this->bIsModuleScriptReassignmentPending = true; })),
						FStackIssueFix(
							LOCTEXT("DeleteFix", "Delete this module"),
							FStackIssueFixDelegate::CreateLambda([this]() { this->Delete(); }))
					}));
			}
			else
			{
				// If the module can't be moved or deleted it's inherited and it's not valid to reassign scripts in child emitters because it breaks merging.
				NewIssues.Add(FStackIssue(
					EStackIssueSeverity::Error,
					ModuleScriptMissingShort,
					FText::Format(LOCTEXT("ModuleScriptMissingFixParentLong", "The script asset for the assigned module {0} is missing.  This module is inherited and this issue must be fixed in the parent emitter."), 
						FText::FromString(FunctionCallNode->GetFunctionName())),
					GetStackEditorDataKey(),
					false));
			}
		}
		else if (!FunctionCallNode->ScriptIsValid())
		{
			FStackIssue InvalidScriptError(
				EStackIssueSeverity::Error,
				LOCTEXT("InvalidModuleScriptErrorSummary", "Invalid module script."),
				LOCTEXT("InvalidModuleScriptError", "The script this module is supposed to execute is missing or invalid for other reasons."),
				GetStackEditorDataKey(),
				false);

			NewIssues.Add(InvalidScriptError);
		}

		TOptional<bool> IsEnabled = FNiagaraStackGraphUtilities::GetModuleIsEnabled(*FunctionCallNode);
		if (!IsEnabled.IsSet())
		{
			bIsEnabled = false;
			FText FixDescription = LOCTEXT("EnableModule", "Enable module");
			FStackIssueFix EnableFix(
				FixDescription,
				FStackIssueFixDelegate::CreateLambda([this, FixDescription]()
			{
				SetIsEnabled(true);;
			}));
			FStackIssue InconsistentEnabledError(
				EStackIssueSeverity::Error,
				LOCTEXT("InconsistentEnabledErrorSummary", "The enabled state for this module is inconsistent."),
				LOCTEXT("InconsistentEnabledError", "This module is using multiple functions and their enabled states are inconsistent.\nClick \"Fix issue\" to make all of the functions for this module enabled."),
				GetStackEditorDataKey(),
				false,
				EnableFix);

			NewIssues.Add(InconsistentEnabledError);
		}

		UNiagaraNodeAssignment* AssignmentFunctionCall = Cast<UNiagaraNodeAssignment>(FunctionCallNode);
		if (AssignmentFunctionCall != nullptr)
		{
			TSet<FNiagaraVariable> FoundAssignmentTargets;
			for (const FNiagaraVariable& AssignmentTarget : AssignmentFunctionCall->GetAssignmentTargets())
			{
				if (FoundAssignmentTargets.Contains(AssignmentTarget))
				{
					FText FixDescription = LOCTEXT("RemoveDuplicate", "Remove Duplicate");
					FStackIssueFix RemoveDuplicateFix(FixDescription, FStackIssueFixDelegate::CreateLambda([AssignmentFunctionCall, AssignmentTarget]()
					{
						AssignmentFunctionCall->RemoveParameter(AssignmentTarget);
					}));
					FStackIssue DuplicateAssignmentTargetError(
						EStackIssueSeverity::Error,
						LOCTEXT("DuplicateAssignmentTargetErrorSummary", "Duplicate variables detected."),
						LOCTEXT("DuplicateAssignmentTargetError", "This 'Set Variables' module is attempting to set the same variable more than once, which is unsupported."),
						GetStackEditorDataKey(),
						false,
						RemoveDuplicateFix);

					NewIssues.Add(DuplicateAssignmentTargetError);
				}
				FoundAssignmentTargets.Add(AssignmentTarget);
			}
		}
	}
	// Generate dependency errors with their fixes
	TArray<UNiagaraNodeFunctionCall*> FoundCalls;
	TArray<FNiagaraModuleDependency> DependenciesNeeded;
	TArray<FNiagaraStackModuleData> SystemModuleData = GetSystemViewModel()->GetStackModuleData(this);
	int32 ModuleIndex = INDEX_NONE;

	for (int i = 0; i < SystemModuleData.Num(); i++)
	{
		auto ModuleData = SystemModuleData[i];
		if (ModuleData.ModuleNode == FunctionCallNode)
		{
			ModuleIndex = i;
			break;
		}
	}

	if (ModuleIndex != INDEX_NONE && FunctionCallNode && FunctionCallNode->FunctionScript)
	{
		for (FNiagaraModuleDependency Dependency : FunctionCallNode->FunctionScript->RequiredDependencies)
		{
			if (Dependency.Id == NAME_None)
			{
				continue;
			}
			bool bDependencyMet = false;
			UNiagaraNodeFunctionCall* FunctionNode = nullptr;
			TArray <UNiagaraNodeFunctionCall*> DisabledDependencies;
			TArray <FNiagaraStackModuleData> DisorderedDependencies;

			int32 DependencyModuleIndex = INDEX_NONE;
			for (FNiagaraStackModuleData ModuleData : SystemModuleData)
			{
				FunctionNode = ModuleData.ModuleNode;
				DependencyModuleIndex++;
				if (FunctionNode != nullptr && FunctionNode->FunctionScript != nullptr && FunctionNode->FunctionScript->ProvidedDependencies.Contains(Dependency.Id))
				{
					auto DependencyOutputUsage = ModuleData.Usage;
					int32 PossibleIndex = UsagePriority.IndexOfByPredicate(
						[&](const ENiagaraScriptUsage CurrentUsage)
					{
						return UNiagaraScript::IsEquivalentUsage(DependencyOutputUsage, CurrentUsage);
					});
					int32 DependantIndex = UsagePriority.IndexOfByPredicate(
						[&](const ENiagaraScriptUsage CurrentUsage)
					{
						return UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), CurrentUsage);
					});
					int32 Distance = PossibleIndex - DependantIndex;

					bool bIncorrectOrder = Distance == 0 ? ((Dependency.Type == ENiagaraModuleDependencyType::PreDependency && ModuleIndex < DependencyModuleIndex)
						|| (Dependency.Type == ENiagaraModuleDependencyType::PostDependency && ModuleIndex > DependencyModuleIndex))
						: ((Dependency.Type == ENiagaraModuleDependencyType::PreDependency && Distance < 0)
							|| (Dependency.Type == ENiagaraModuleDependencyType::PostDependency && Distance > 0));

					bool bSameScriptDependencyConstraint = Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::SameScript;
					bool bEquivalentScriptUsage = UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ModuleData.Usage);

					// If the dependency is for modules in the same script, the two modules are only incorrectly ordered if they share equivalent script usages
					if (bSameScriptDependencyConstraint)
					{
						bIncorrectOrder = bEquivalentScriptUsage && bIncorrectOrder;
					}

					if (bIncorrectOrder)
					{
						DisorderedDependencies.Add(ModuleData);
					}
					else if (FunctionNode->IsNodeEnabled() == false)
					{
						DisabledDependencies.Add(FunctionNode);
					}
					else if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::AllScripts || 
						(bSameScriptDependencyConstraint && bEquivalentScriptUsage && OutputNode->GetUsageId() == ModuleData.UsageId))
					{
						bDependencyMet = true;
						break;
					}
				}
			}
			if (bDependencyMet == false)
			{
				TArray<FStackIssueFix> Fixes;
				DependenciesNeeded.Add(Dependency);

				FText DependencyTypeString = Dependency.Type == ENiagaraModuleDependencyType::PreDependency ? LOCTEXT("PreDependency", "pre-dependency") : LOCTEXT("PostDependency", "post-dependency");

				for (UNiagaraNodeFunctionCall* DisabledNode : DisabledDependencies) // module exists but disabled
				{
					UNiagaraStackEntry::FStackIssueFix Fix(
						FText::Format(LOCTEXT("EnableDependency", "Enable dependency module {0}"), FText::FromString(DisabledNode->GetFunctionName())),
						FStackIssueFixDelegate::CreateLambda([this, DisabledNode]()
					{
						FScopedTransaction ScopedTransaction(LOCTEXT("EnableDependencyModule", "Enable dependency module"));
						FNiagaraStackGraphUtilities::SetModuleIsEnabled(*DisabledNode, true);
						OnRequestFullRefreshDeferred().Broadcast();

					}));
					Fixes.Add(Fix);
				}

				for (FNiagaraStackModuleData DisorderedNode : DisorderedDependencies) // module exists but is not in the correct order (and possibly also disabled)
				{
					bool bNeedsEnable = !DisorderedNode.ModuleNode->IsNodeEnabled();
					FText AndEnableModule = bNeedsEnable ? FText::Format(LOCTEXT("AndEnableDependency", "And enable dependency module {0}"), FText::FromString(DisorderedNode.ModuleNode->GetFunctionName())) : FText();
					UNiagaraStackEntry::FStackIssueFix Fix(
						FText::Format(LOCTEXT("ReorderDependency", "Reposition this module in the correct order related to {0} {1}"), FText::FromString(DisorderedNode.ModuleNode->GetFunctionName()), AndEnableModule),
						FStackIssueFixDelegate::CreateLambda([this, bNeedsEnable, DisorderedNode, SystemModuleData, Dependency, ModuleIndex]()
					{
						FScopedTransaction ScopedTransaction(LOCTEXT("ReorderDependencyModule", "Reorder dependency module"));

						FunctionCallNode->Modify();
						// reorder node
						int32 CorrectIndex = Dependency.Type == ENiagaraModuleDependencyType::PostDependency ? DisorderedNode.Index : DisorderedNode.Index + 1;
						checkf(ModuleIndex != INDEX_NONE, TEXT("Module data wasn't found in system for current module!"));
						UNiagaraScript& OwningScript = *FNiagaraEditorUtilities::GetScriptFromSystem(GetSystemViewModel()->GetSystem(), SystemModuleData[ModuleIndex].EmitterHandleId, SystemModuleData[ModuleIndex].Usage, SystemModuleData[ModuleIndex].UsageId);
						UNiagaraNodeFunctionCall* MovedNode;
						FNiagaraStackGraphUtilities::MoveModule(OwningScript, *FunctionCallNode, GetSystemViewModel()->GetSystem(), DisorderedNode.EmitterHandleId, DisorderedNode.Usage, DisorderedNode.UsageId, CorrectIndex, false, MovedNode);
						// enable if needed
						if (bNeedsEnable)
						{
							FNiagaraStackGraphUtilities::SetModuleIsEnabled(*DisorderedNode.ModuleNode, true);
						}
						FNiagaraStackGraphUtilities::RelayoutGraph(*OutputNode->GetGraph());
						OnRequestFullRefreshDeferred().Broadcast();
					}));
					Fixes.Add(Fix);
				}
				if (DisorderedDependencies.Num() == 0 && DisabledDependencies.Num() == 0)
				{
					TArray<FAssetData> ModuleAssets;
					FNiagaraStackGraphUtilities::GetScriptAssetsByDependencyProvided(ENiagaraScriptUsage::Module, Dependency.Id, ModuleAssets);

					// Find duplicate module names in the fixes so that unique fix descriptions can be generated.
					TArray<FName> ModuleNames;
					TArray<FName> DuplicateModuleNames;
					for (FAssetData ModuleAsset : ModuleAssets)
					{
						if (ModuleNames.Contains(ModuleAsset.AssetName))
						{
							DuplicateModuleNames.Add(ModuleAsset.AssetName);
						}
						else
						{
							ModuleNames.Add(ModuleAsset.AssetName);
						}
					}
					for (FAssetData ModuleAsset : ModuleAssets)
					{
						UNiagaraScript* DependencyScript = Cast<UNiagaraScript>(ModuleAsset.GetAsset());
						if (Dependency.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::SameScript && DependencyScript)
						{
							TArray<ENiagaraScriptUsage> SupportedUsages = UNiagaraScript::GetSupportedUsageContextsForBitmask(DependencyScript->ModuleUsageBitmask);
							if (SupportedUsages.Contains(OutputNode->GetUsage()) == false)
							{
								// If the dependency requires the provider be in the same script and the usage of this module doesn't support that usage, skip it.
								continue;
							}
						}
						
						FText AssetNameText = DuplicateModuleNames.Contains(ModuleAsset.AssetName) ? FText::FromName(ModuleAsset.PackageName) : FText::FromName(ModuleAsset.AssetName);
						FText FixDescription = FText::Format(LOCTEXT("AddDependency", "Add new dependency module {0}"), AssetNameText);
						UNiagaraStackEntry::FStackIssueFix Fix(
							FixDescription,
							FStackIssueFixDelegate::CreateLambda([=]()
						{
							FScopedTransaction ScopedTransaction(FixDescription);
							UNiagaraNodeFunctionCall* NewModuleNode = nullptr;
							int32 TargetIndex = 0;
							checkf(DependencyScript != nullptr, TEXT("Add module action failed"));
							// Determine the output node for the group where the added dependency module belongs
							UNiagaraNodeOutput* TargetOutputNode = nullptr;
							for (int i = ModuleIndex; i < SystemModuleData.Num() && i >= 0; i = Dependency.Type == ENiagaraModuleDependencyType::PostDependency ? i + 1 : i - 1) // moving up or down depending on type
							// starting at current module, which is a dependent
							{
								bool bRequiredDependencyFound = SystemModuleData[i].ModuleNode->FunctionScript->RequiredDependencies.ContainsByPredicate(
									[&Dependency](const FNiagaraModuleDependency& RequiredDependency)
								{
									return RequiredDependency.Id == Dependency.Id;
								});
								if (bRequiredDependencyFound && 
									(Dependency.ScriptConstraint != ENiagaraModuleDependencyScriptConstraint::SameScript ||
									SystemModuleData[ModuleIndex].Usage == SystemModuleData[i].Usage)) // check for multiple dependents along the way, and stop adjacent to the last one
								{
									ENiagaraScriptUsage DependencyUsage = SystemModuleData[i].Usage;
									const FNiagaraEmitterHandle* EmitterHandle = GetEmitterViewModel().IsValid()
										? FNiagaraEditorUtilities::GetEmitterHandleForEmitter(GetSystemViewModel()->GetSystem(), *GetEmitterViewModel()->GetEmitter())
										: nullptr;
									UNiagaraNodeOutput* FoundTargetOutputNode = GetOutputNodeForModuleDependency(DependencyUsage, DependencyScript, GetSystemViewModel()->GetSystem(), EmitterHandle, Dependency);
									if (FoundTargetOutputNode != nullptr)
									{
										TargetOutputNode = FoundTargetOutputNode;
										UNiagaraNodeOutput* CurrentOutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*SystemModuleData[i].ModuleNode);
										if (TargetOutputNode == CurrentOutputNode)
										{
											TargetIndex = Dependency.Type == ENiagaraModuleDependencyType::PostDependency ? SystemModuleData[i].Index + 1 : SystemModuleData[i].Index;
										}
										else
										{
											TargetIndex = Dependency.Type == ENiagaraModuleDependencyType::PostDependency ? 0 : INDEX_NONE;
										}
									}
								}
							}

							if (TargetOutputNode == nullptr)
							{
								// If no output node was found than the dependency can't be resolved and it most likely misconfigured in data.
								// TODO: Don't show this toast here, change the fix delegate to return a fix result with whether or not the fix succeeded and any error message for the user.
								FNotificationInfo Error(LOCTEXT("FixFailedToast", "Failed to fix the dependency since\nwe could not find a compatible place to insert the module.\nPlease check the configuration of the dependency.\nSee the log for more details."));
								Error.ExpireDuration = 5.0f;
								Error.bFireAndForget = true;
								Error.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
								FSlateNotificationManager::Get().AddNotification(Error);
								FString ModuleAssetFullName;
								ModuleAsset.GetFullName(ModuleAssetFullName);
								UE_LOG(LogNiagaraEditor, Error, TEXT("Dependency fix failed, could not find a compatible place to insert the module.\nModule requiring dependency: %s\nModule providing dependency: %s\nDependency name: %s\nDependency type: %s"),
									*FunctionCallNode->FunctionScript->GetFullName(), *ModuleAssetFullName, *Dependency.Id.ToString(), Dependency.Type == ENiagaraModuleDependencyType::PreDependency ? TEXT("Pre-dependency") : TEXT("Post-dependency"));
								return;
							}

							TArray<FNiagaraStackModuleData> ScriptModuleData = SystemModuleData.FilterByPredicate([&](FNiagaraStackModuleData CurrentData) {return CurrentData.Usage == DependencyScript->GetUsage(); });
							int32 PreIndex = INDEX_NONE; // index of last pre dependency
							int32 PostIndex = INDEX_NONE; // index of fist post dependency, the module will have to be placed between these indexes
							// for now, we skip the case where the dependencies are fulfilled in other script groups as well as here, because that's extremely unlikely
							if (TargetIndex == INDEX_NONE)
							{
								TargetIndex = 0; //start at the beginning to look for potential dependencies of this dependency
							}
							for (int32 i = TargetIndex; i < ScriptModuleData.Num() && i >= 0; i = Dependency.Type == ENiagaraModuleDependencyType::PostDependency ? i + 1 : i - 1)
							{
								UNiagaraNodeFunctionCall * CurrentNode = ScriptModuleData[i].ModuleNode;
								for (FNiagaraModuleDependency Requirement : DependencyScript->RequiredDependencies)
								{
									if (Requirement.Id == NAME_None)
									{
										continue;
									}

									if (CurrentNode->FunctionScript->ProvidedDependencies.Contains(Requirement.Id))
									{
										if (Requirement.Type == ENiagaraModuleDependencyType::PreDependency)
										{
											PostIndex = i;
										}
										else if (PreIndex == INDEX_NONE) // only record the first post-dependency
										{
											PreIndex = i;
										}
									}
								}
							}
							if (PostIndex != INDEX_NONE)
							{
								TargetIndex = 0; // if it has post dependencies place it at the top
								if (PreIndex != INDEX_NONE)
								{
									TargetIndex = PostIndex; // if it also has post dependencies just add it before its first post dependency
								}
							}
							NewModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleAsset, *TargetOutputNode, TargetIndex);
							checkf(NewModuleNode != nullptr, TEXT("Add module action failed"));
							FNiagaraStackGraphUtilities::InitializeStackFunctionInputs(GetSystemViewModel(), GetEmitterViewModel(), GetStackEditorData(), *NewModuleNode, *NewModuleNode);
							FNiagaraStackGraphUtilities::RelayoutGraph(*TargetOutputNode->GetGraph());
							OnRequestFullRefreshDeferred().Broadcast();
						}));
						Fixes.Add(Fix);
					}
				}
				UNiagaraStackEntry::FStackIssue Error(
					EStackIssueSeverity::Error,
					LOCTEXT("DependencyWarning", "The module has unmet dependencies."),
					FText::Format(LOCTEXT("DependencyWarningLong", "The following {0} is not met: {1}; {2}"), DependencyTypeString, FText::FromName(Dependency.Id), Dependency.Description),
					FString::Printf(TEXT("%s-dependency-%s"), *GetStackEditorDataKey(), *Dependency.Id.ToString()),
					true,
					Fixes);
				NewIssues.Add(Error);
			}
		}
	}
}

bool UNiagaraStackModuleItem::FilterOutputCollection(const UNiagaraStackEntry& Child) const
{
	if (Child.IsA<UNiagaraStackModuleItemOutputCollection>())
	{
		TArray<UNiagaraStackEntry*> FilteredChildren;
		Child.GetFilteredChildren(FilteredChildren);
		if (FilteredChildren.Num() != 0)
		{
			return true;
		}
		else if (GetStackEditorData().GetShowOutputs() == false)
		{
			return false;
		}
	}
	return true;
}

bool UNiagaraStackModuleItem::FilterOutputCollectionChild(const UNiagaraStackEntry& Child) const
{
	// Filter to only show search result matches inside collapsed collection
	if (GetStackEditorData().GetShowOutputs() == false)
	{
		return Child.GetIsSearchResult();
	}
	return true;
}

bool UNiagaraStackModuleItem::FilterLinkedInputCollection(const UNiagaraStackEntry& Child) const
{
	if (Child.IsA<UNiagaraStackModuleItemLinkedInputCollection>())
	{
		TArray<UNiagaraStackEntry*> FilteredChildren;
		Child.GetFilteredChildren(FilteredChildren);
		if (FilteredChildren.Num() != 0)
		{
 			return true;
		}
		else if (GetStackEditorData().GetShowLinkedInputs() == false && Child.GetShouldShowInStack())
		{
			return false;
		}
	}
	return true;
}

bool UNiagaraStackModuleItem::FilterLinkedInputCollectionChild(const UNiagaraStackEntry& Child) const
{
	// Filter to only show search result matches inside collapsed collection
	if (GetStackEditorData().GetShowLinkedInputs() == false)
	{
		return Child.GetIsSearchResult();
	}
	return true;
}

void UNiagaraStackModuleItem::RefreshIsEnabled()
{
	TOptional<bool> IsEnabled = FNiagaraStackGraphUtilities::GetModuleIsEnabled(*FunctionCallNode);
	if (IsEnabled.IsSet())
	{
		bIsEnabled = IsEnabled.GetValue();
	}
}

void UNiagaraStackModuleItem::OnMessageManagerRefresh(const FGuid& MessageJobBatchAssetKey, const TArray<TSharedRef<const INiagaraMessage>> NewMessages)
{
	if (GetSystemViewModel()->GetMessageLogGuid() == MessageJobBatchAssetKey)
	{
		if (FNiagaraMessageManager::Get()->GetMessagesForAssetKeyAndObjectKey(MessageJobBatchAssetKey, FObjectKey(FunctionCallNode)).Num() > 0)
		{
			RefreshChildren();
		}
	}
}

bool UNiagaraStackModuleItem::CanMoveAndDelete() const
{
	if (bCanMoveAndDeleteCache.IsSet() == false)
	{
		if (HasBaseEmitter() == false)
		{
			// If there is no base emitter all modules can be moved and deleted.
			bCanMoveAndDeleteCache = true;
		}
		else
		{
			// When editing systems only non-base modules can be moved and deleted.
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();

			const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();

			bool bIsMergeable = MergeManager->IsMergeableScriptUsage(OutputNode->GetUsage());
			bool bHasBaseModule = bIsMergeable && BaseEmitter != nullptr && MergeManager->HasBaseModule(*BaseEmitter, OutputNode->GetUsage(), OutputNode->GetUsageId(), FunctionCallNode->NodeGuid);
			bCanMoveAndDeleteCache = bHasBaseModule == false;
		}
	}
	return bCanMoveAndDeleteCache.GetValue();
}

bool UNiagaraStackModuleItem::CanRefresh() const
{
	return bCanRefresh;
}

void UNiagaraStackModuleItem::Refresh()
{
	if (CanRefresh())
	{
		if (FunctionCallNode->RefreshFromExternalChanges())
		{
			FunctionCallNode->GetNiagaraGraph()->NotifyGraphNeedsRecompile();
			GetSystemViewModel()->ResetSystem();
		}
		RefreshChildren();
	}
}

bool UNiagaraStackModuleItem::GetIsEnabled() const
{
	return bIsEnabled;
}

void UNiagaraStackModuleItem::SetIsEnabledInternal(bool bInIsEnabled)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("EnableDisableModule", "Enable/Disable Module"));
	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*FunctionCallNode, bInIsEnabled);
	bIsEnabled = bInIsEnabled;
	OnRequestFullRefreshDeferred().Broadcast();
}

bool UNiagaraStackModuleItem::SupportsHighlights() const
{
	return FunctionCallNode != nullptr && FunctionCallNode->FunctionScript != nullptr;
}

const TArray<FNiagaraScriptHighlight>& UNiagaraStackModuleItem::GetHighlights() const
{
	return FunctionCallNode->FunctionScript->Highlights;
}

int32 UNiagaraStackModuleItem::GetModuleIndex() const
{
	TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackGroups;
	FNiagaraStackGraphUtilities::GetStackNodeGroups(*FunctionCallNode, StackGroups);
	int32 ModuleIndex = 0;
	for (FNiagaraStackGraphUtilities::FStackNodeGroup& StackGroup : StackGroups)
	{
		if (StackGroup.EndNode == FunctionCallNode)
		{
			return ModuleIndex;
		}
		if (StackGroup.EndNode->IsA<UNiagaraNodeFunctionCall>())
		{
			ModuleIndex++;
		}
	}
	return INDEX_NONE;
}

UNiagaraNodeOutput* UNiagaraStackModuleItem::GetOutputNode() const
{
	return OutputNode;
}

bool UNiagaraStackModuleItem::CanAddInput(FNiagaraVariable InputParameter) const
{
	UNiagaraNodeAssignment* AssignmentModule = Cast<UNiagaraNodeAssignment>(FunctionCallNode);
	return AssignmentModule != nullptr &&
		AssignmentModule->GetAssignmentTargets().Contains(InputParameter) == false &&
		FNiagaraStackGraphUtilities::CanWriteParameterFromUsage(InputParameter, OutputNode->GetUsage());
}

void UNiagaraStackModuleItem::AddInput(FNiagaraVariable InputParameter)
{
	if(ensureMsgf(CanAddInput(InputParameter), TEXT("This module doesn't support adding this input.")))
	{
		UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(FunctionCallNode);
		AssignmentNode->AddParameter(InputParameter, FNiagaraConstants::GetAttributeDefaultValue(InputParameter));
		FNiagaraStackGraphUtilities::InitializeStackFunctionInput(GetSystemViewModel(), GetEmitterViewModel(), GetStackEditorData(), *FunctionCallNode, *FunctionCallNode, InputParameter.GetName());
	}
}

bool UNiagaraStackModuleItem::GetIsModuleScriptReassignmentPending() const
{
	return bIsModuleScriptReassignmentPending;
}

void UNiagaraStackModuleItem::SetIsModuleScriptReassignmentPending(bool bIsPending)
{
	bIsModuleScriptReassignmentPending = bIsPending;
}

void UNiagaraStackModuleItem::ReassignModuleScript(UNiagaraScript* ModuleScript)
{
	if (ensureMsgf(FunctionCallNode != nullptr && FunctionCallNode->GetClass() == UNiagaraNodeFunctionCall::StaticClass(),
		TEXT("Can not reassign the module script when the module isn't a valid function call module.")))
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("ReassignModuleTransaction", "Reassign module script"));

		const FString OldName = FunctionCallNode->GetFunctionName();
		UNiagaraScript* OldScript = FunctionCallNode->FunctionScript;

		FunctionCallNode->Modify();
		UNiagaraClipboardContent* OldClipboardContent = nullptr;
		if (ModuleScript->ConversionUtility != nullptr)
		{
			OldClipboardContent = UNiagaraClipboardContent::Create();
			Copy(OldClipboardContent);
		}
		FunctionCallNode->FunctionScript = ModuleScript;
		
		// intermediate refresh to purge any rapid iteration parameters that have been removed in the new script
		RefreshChildren();

		FunctionCallNode->SuggestName(FString());
		const FString NewName = FunctionCallNode->GetFunctionName();
		if (NewName != OldName)
		{
			UNiagaraSystem& System = GetSystemViewModel()->GetSystem();
			UNiagaraEmitter* Emitter = GetEmitterViewModel().IsValid() ? GetEmitterViewModel()->GetEmitter() : nullptr;
			FNiagaraStackGraphUtilities::RenameReferencingParameters(System, Emitter, *FunctionCallNode, OldName, NewName);
			FunctionCallNode->RefreshFromExternalChanges();
			FunctionCallNode->MarkNodeRequiresSynchronization(TEXT("Module script reassigned."), true);
			RefreshChildren();
		}
		
		if (ModuleScript->ConversionUtility != nullptr && OldClipboardContent != nullptr)
		{
			UNiagaraConvertInPlaceUtilityBase* ConversionUtility = NewObject< UNiagaraConvertInPlaceUtilityBase>(GetTransientPackage(), ModuleScript->ConversionUtility);
			FText ConvertMessage;

			UNiagaraClipboardContent* NewClipboardContent = UNiagaraClipboardContent::Create();
			Copy(NewClipboardContent);

			if (ConversionUtility )
			{
				bool bConverted = ConversionUtility->Convert(OldScript, OldClipboardContent, ModuleScript, InputCollection, NewClipboardContent, FunctionCallNode, ConvertMessage);
				if (!ConvertMessage.IsEmptyOrWhitespace())
				{
					// Notify the end-user about the convert message, but continue the process as they could always undo.
					FNotificationInfo Msg(FText::Format(LOCTEXT("FixConvertInPlace", "Conversion Note: {0}"), ConvertMessage));
					Msg.ExpireDuration = 5.0f;
					Msg.bFireAndForget = true;
					Msg.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Note"));
					FSlateNotificationManager::Get().AddNotification(Msg);
				}
			}
		}
	}
}

void UNiagaraStackModuleItem::SetInputValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	InputCollection->SetValuesFromClipboardFunctionInputs(ClipboardFunctionInputs);
}

bool UNiagaraStackModuleItem::TestCanCutWithMessage(FText& OutMessage) const
{
	FText CanCopyMessage;
	if (TestCanCopyWithMessage(CanCopyMessage) == false)
	{
		OutMessage = FText::Format(LOCTEXT("CantCutBecauseCantCopyFormat", "This module can not be cut because it can't be copied.  {0}"), CanCopyMessage);
		return false;
	}

	FText CanDeleteMessage;
	if (TestCanDeleteWithMessage(CanDeleteMessage) == false)
	{
		OutMessage = FText::Format(LOCTEXT("CantCutBecauseCantDeleteFormat", "This module can't be cut because it can't be deleted.  {0}"), CanDeleteMessage);
		return false;
	}

	OutMessage = LOCTEXT("CanCut", "Cut this module.");
	return true;
}

FText UNiagaraStackModuleItem::GetCutTransactionText() const
{
	return LOCTEXT("CutModuleTransactionText", "Cut modules");
}

void UNiagaraStackModuleItem::CopyForCut(UNiagaraClipboardContent* ClipboardContent) const
{
	Copy(ClipboardContent);
}

void UNiagaraStackModuleItem::RemoveForCut()
{
	Delete();
}

bool UNiagaraStackModuleItem::TestCanCopyWithMessage(FText& OutMessage) const
{
	if (FunctionCallNode->GetClass() == UNiagaraNodeFunctionCall::StaticClass())
	{
		if (FunctionCallNode->FunctionScript == nullptr)
		{
			OutMessage = LOCTEXT("CantCopyInvalidModule", "This module can't be copied because it's referenced script is not valid.");
			return false;
		}
	}
	OutMessage = LOCTEXT("CopyModule", "Copy this module.");
	return true;
}

void UNiagaraStackModuleItem::Copy(UNiagaraClipboardContent* ClipboardContent) const
{
	UNiagaraClipboardFunction* ClipboardFunction;
	UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(FunctionCallNode);
	if (AssignmentNode != nullptr)
	{
		ClipboardFunction = UNiagaraClipboardFunction::CreateAssignmentFunction(ClipboardContent, AssignmentNode->GetFunctionName(), AssignmentNode->GetAssignmentTargets(), AssignmentNode->GetAssignmentDefaults());
	}
	else
	{
		checkf(FunctionCallNode->FunctionScript != nullptr, TEXT("Can't copy this module because it's script is invalid.  Call TestCanCopyWithMessage to check this."));
		ClipboardFunction = UNiagaraClipboardFunction::CreateScriptFunction(ClipboardContent, FunctionCallNode->GetFunctionName(), FunctionCallNode->FunctionScript);
	}

	ClipboardFunction->DisplayName = GetAlternateDisplayName().Get(FText::GetEmpty());

	InputCollection->ToClipboardFunctionInputs(ClipboardFunction, ClipboardFunction->Inputs);
	ClipboardContent->Functions.Add(ClipboardFunction);
}

bool UNiagaraStackModuleItem::TestCanPasteWithMessage(const UNiagaraClipboardContent* ClipboardContent, FText& OutMessage) const
{
	if (ClipboardContent->FunctionInputs.Num() > 0)
	{
		OutMessage = LOCTEXT("PasteInputs", "Paste inputs from the clipboard which match inputs on this module by name and type.");
		return true;
	}

	if (RequestCanPasteDelegete.IsBound())
	{
		return RequestCanPasteDelegete.Execute(ClipboardContent, OutMessage);
	}

	OutMessage = FText();
	return false;
}

FText UNiagaraStackModuleItem::GetPasteTransactionText(const UNiagaraClipboardContent* ClipboardContent) const
{
	if (ClipboardContent->FunctionInputs.Num() > 0)
	{
		return LOCTEXT("PasteInputsTransactionText", "Paste inputs to module.");
	}
	else
	{
		return LOCTEXT("PasteModuleTransactionText", "Paste niagara modules");
	}
}

void UNiagaraStackModuleItem::Paste(const UNiagaraClipboardContent* ClipboardContent, FText& OutPasteWarning)
{
	if (ClipboardContent->FunctionInputs.Num() > 0)
	{
		SetInputValuesFromClipboardFunctionInputs(ClipboardContent->FunctionInputs);
	}
	else if (RequestCanPasteDelegete.IsBound())
	{
		// Pasted modules should go after this module, so add 1 to the index.
		int32 PasteIndex = GetModuleIndex() + 1;
		RequestPasteDelegate.Execute(ClipboardContent, PasteIndex, OutPasteWarning);
	}
}

bool UNiagaraStackModuleItem::TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const
{
	if (GetOwnerIsEnabled() == false)
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteOwnerDisabledToolTip", "This module can not be deleted because its owner is disabled.");
		return false;
	}
	else if (CanMoveAndDelete())
	{
		OutCanDeleteMessage = LOCTEXT("DeleteToolTip", "Delete this module.");
		return true;
	}
	else
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteToolTip", "This module can not be deleted becaue it is inherited.");
		return false;
	}
}

FText UNiagaraStackModuleItem::GetDeleteTransactionText() const
{
	return LOCTEXT("DeleteModuleTransaction", "Delete modules");
}

void UNiagaraStackModuleItem::Delete()
{
	checkf(CanMoveAndDelete(), TEXT("This module can't be deleted"));

	const FNiagaraEmitterHandle* EmitterHandle = GetEmitterViewModel().IsValid()
		? FNiagaraEditorUtilities::GetEmitterHandleForEmitter(GetSystemViewModel()->GetSystem(), *GetEmitterViewModel()->GetEmitter())
		: nullptr;
	FGuid EmitterHandleId = EmitterHandle != nullptr ? EmitterHandle->GetId() : FGuid();

	TArray<TWeakObjectPtr<UNiagaraNodeInput>> RemovedNodes;
	bool bRemoved = FNiagaraStackGraphUtilities::RemoveModuleFromStack(GetSystemViewModel()->GetSystem(), EmitterHandleId, *FunctionCallNode, RemovedNodes);
	if (bRemoved)
	{
		UNiagaraGraph* Graph = FunctionCallNode->GetNiagaraGraph();
		Graph->NotifyGraphNeedsRecompile();
		FNiagaraStackGraphUtilities::RelayoutGraph(*FunctionCallNode->GetGraph());
		for (auto InputNode : RemovedNodes)
		{
			if (InputNode != nullptr && InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
			{
				GetSystemViewModel()->NotifyDataObjectChanged(InputNode->GetDataInterface());
			}
		}
		ModifiedGroupItemsDelegate.Broadcast();
	}
}

bool UNiagaraStackModuleItem::IsScratchModule() const
{
	if (bIsScratchModuleCache.IsSet() == false)
	{
		bIsScratchModuleCache = GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(FunctionCallNode->FunctionScript).IsValid();
	}
	return bIsScratchModuleCache.GetValue();
}

UObject* UNiagaraStackModuleItem::GetExternalAsset() const
{
	if (GetModuleNode().FunctionScript != nullptr && GetModuleNode().FunctionScript->IsAsset())
	{
		return GetModuleNode().FunctionScript;
	}
	return nullptr;
}

bool UNiagaraStackModuleItem::CanDrag() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
