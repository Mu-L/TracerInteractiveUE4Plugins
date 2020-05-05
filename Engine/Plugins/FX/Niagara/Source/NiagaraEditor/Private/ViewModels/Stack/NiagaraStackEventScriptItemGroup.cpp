// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEventScriptItemGroup.h"
#include "NiagaraEmitter.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Customizations/NiagaraEventScriptPropertiesCustomization.h"

#include "Internationalization/Internationalization.h"
#include "ScopedTransaction.h"
#include "IDetailTreeNode.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackEventScriptItemGroup"

void UNiagaraStackEventHandlerPropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData, FGuid InEventScriptUsageId)
{
	FString EventStackEditorDataKey = FString::Printf(TEXT("Event-%s-Properties"), *InEventScriptUsageId.ToString(EGuidFormats::DigitsWithHyphens));
	Super::Initialize(InRequiredEntryData, EventStackEditorDataKey);

	EventScriptUsageId = InEventScriptUsageId;

	Emitter = GetEmitterViewModel()->GetEmitter();
	Emitter->OnPropertiesChanged().AddUObject(this, &UNiagaraStackEventHandlerPropertiesItem::EventHandlerPropertiesChanged);
}

void UNiagaraStackEventHandlerPropertiesItem::FinalizeInternal()
{
	if (Emitter.IsValid())
	{
		Emitter->OnPropertiesChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

FText UNiagaraStackEventHandlerPropertiesItem::GetDisplayName() const
{
	return LOCTEXT("EventHandlerPropertiesDisplayName", "Event Handler Properties");
}

bool UNiagaraStackEventHandlerPropertiesItem::TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		if (HasBaseEventHandler())
		{
			const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
			if (BaseEmitter != nullptr && Emitter != BaseEmitter)
			{
				TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
				bCanResetToBaseCache = MergeManager->IsEventHandlerPropertySetDifferentFromBase(*Emitter.Get(), *BaseEmitter, EventScriptUsageId);
			}
			else
			{
				bCanResetToBaseCache = false;
			}
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	if (bCanResetToBaseCache.GetValue())
	{
		OutCanResetToBaseMessage = LOCTEXT("CanResetToBase", "Reset the event handler properties to the state defined by the parent emitter.");
		return true;
	}
	else
	{
		OutCanResetToBaseMessage = LOCTEXT("CanNotResetToBase", "No parent to reset to, or not different from parent.");
		return false;
	}
}

void UNiagaraStackEventHandlerPropertiesItem::ResetToBase()
{
	FText Unused;
	if (TestCanResetToBaseWithMessage(Unused))
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		MergeManager->ResetEventHandlerPropertySetToBase(*Emitter, *BaseEmitter, EventScriptUsageId);
		RefreshChildren();
	}
}

void UNiagaraStackEventHandlerPropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (EmitterObject == nullptr)
	{
		EmitterObject = NewObject<UNiagaraStackObject>(this);
		EmitterObject->Initialize(CreateDefaultChildRequiredData(), Emitter.Get(), GetStackEditorDataKey());
		EmitterObject->RegisterInstancedCustomPropertyTypeLayout(FNiagaraEventScriptProperties::StaticStruct()->GetFName(), 
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraEventScriptPropertiesCustomization::MakeInstance, 
			TWeakObjectPtr<UNiagaraSystem>(&GetSystemViewModel()->GetSystem()), TWeakObjectPtr<UNiagaraEmitter>(GetEmitterViewModel()->GetEmitter())));
		EmitterObject->SetOnSelectRootNodes(UNiagaraStackObject::FOnSelectRootNodes::CreateUObject(this, &UNiagaraStackEventHandlerPropertiesItem::SelectEmitterStackObjectRootTreeNodes));
	}

	NewChildren.Add(EmitterObject);

	bCanResetToBaseCache.Reset();
	bHasBaseEventHandlerCache.Reset();

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackEventHandlerPropertiesItem::EventHandlerPropertiesChanged()
{
	bCanResetToBaseCache.Reset();
}

TSharedPtr<IDetailTreeNode> GetEventHandlerArrayPropertyNode(const TArray<TSharedRef<IDetailTreeNode>>& Nodes)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildrenToCheck;
	for (TSharedRef<IDetailTreeNode> Node : Nodes)
	{
		if (Node->GetNodeType() == EDetailNodeType::Item)
		{
			TSharedPtr<IPropertyHandle> NodePropertyHandle = Node->CreatePropertyHandle();
			if (NodePropertyHandle.IsValid() && NodePropertyHandle->GetProperty()->GetFName() == UNiagaraEmitter::PrivateMemberNames::EventHandlerScriptProps)
			{
				return Node;
			}
		}

		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);
		ChildrenToCheck.Append(Children);
	}
	if (ChildrenToCheck.Num() == 0)
	{
		return nullptr;
	}
	return GetEventHandlerArrayPropertyNode(ChildrenToCheck);
}

void UNiagaraStackEventHandlerPropertiesItem::SelectEmitterStackObjectRootTreeNodes(TArray<TSharedRef<IDetailTreeNode>> Source, TArray<TSharedRef<IDetailTreeNode>>* Selected)
{
	TSharedPtr<IDetailTreeNode> EventHandlerArrayPropertyNode = GetEventHandlerArrayPropertyNode(Source);
	if (EventHandlerArrayPropertyNode.IsValid())
	{
		TArray<TSharedRef<IDetailTreeNode>> EventHandlerArrayItemNodes;
		EventHandlerArrayPropertyNode->GetChildren(EventHandlerArrayItemNodes);
		for (TSharedRef<IDetailTreeNode> EventHandlerArrayItemNode : EventHandlerArrayItemNodes)
		{
			TSharedPtr<IPropertyHandle> EventHandlerArrayItemPropertyHandle = EventHandlerArrayItemNode->CreatePropertyHandle();
			if (EventHandlerArrayItemPropertyHandle.IsValid())
			{
				FStructProperty* StructProperty = CastField<FStructProperty>(EventHandlerArrayItemPropertyHandle->GetProperty());
				if (StructProperty != nullptr && StructProperty->Struct == FNiagaraEventScriptProperties::StaticStruct())
				{
					TArray<void*> RawData;
					EventHandlerArrayItemPropertyHandle->AccessRawData(RawData);
					if (RawData.Num() == 1)
					{
						FNiagaraEventScriptProperties* EventScriptProperties = static_cast<FNiagaraEventScriptProperties*>(RawData[0]);
						if (EventScriptProperties->Script->GetUsageId() == EventScriptUsageId)
						{
							EventHandlerArrayItemNode->GetChildren(*Selected);
							return;
						}
					}
				}
			}
		}
	}
}

bool UNiagaraStackEventHandlerPropertiesItem::HasBaseEventHandler() const
{
	if (bHasBaseEventHandlerCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		if (BaseEmitter != nullptr && Emitter != BaseEmitter)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			bHasBaseEventHandlerCache = MergeManager->HasBaseEventHandler(*BaseEmitter, EventScriptUsageId);
		}
		else
		{
			bHasBaseEventHandlerCache = false;
		}
	}
	return bHasBaseEventHandlerCache.GetValue();
}

void UNiagaraStackEventScriptItemGroup::Initialize(
	FRequiredEntryData InRequiredEntryData,
	TSharedRef<FNiagaraScriptViewModel> InScriptViewModel,
	ENiagaraScriptUsage InScriptUsage,
	FGuid InScriptUsageId)
{
	FText ToolTip = LOCTEXT("EventGroupTooltip", "Determines how this Emitter responds to incoming events. There can be more than one event handler stage per Emitter.");
	FText TempDisplayName = FText::Format(LOCTEXT("TempDisplayNameFormat", "Event Handler - {0}"), FText::FromString(InScriptUsageId.ToString(EGuidFormats::DigitsWithHyphens)));
	Super::Initialize(InRequiredEntryData, TempDisplayName, ToolTip, InScriptViewModel, InScriptUsage, InScriptUsageId);
}

void UNiagaraStackEventScriptItemGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	bHasBaseEventHandlerCache.Reset();

	UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();

	const FNiagaraEventScriptProperties* EventScriptProperties = Emitter->GetEventHandlers().FindByPredicate(
		[=](const FNiagaraEventScriptProperties& InEventScriptProperties) { return InEventScriptProperties.Script->GetUsageId() == GetScriptUsageId(); });

	if (EventScriptProperties != nullptr)
	{
		SetDisplayName(FText::Format(LOCTEXT("FormatEventScriptDisplayName", "Event Handler - Source: {0}"), FText::FromName(EventScriptProperties->SourceEventName)));
	}
	else
	{
		SetDisplayName(LOCTEXT("UnassignedEventDisplayName", "Unassigned Event"));
	}

	if (EventHandlerProperties == nullptr)
	{
		EventHandlerProperties = NewObject<UNiagaraStackEventHandlerPropertiesItem>(this);
		EventHandlerProperties->Initialize(CreateDefaultChildRequiredData(), GetScriptUsageId());
	}
	NewChildren.Add(EventHandlerProperties);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

bool UNiagaraStackEventScriptItemGroup::TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const
{
	if (HasBaseEventHandler())
	{
		OutCanDeleteMessage = LOCTEXT("CantDeleteInherited", "Can not delete this event handler because it's inherited.");
		return false;
	}
	else
	{
		OutCanDeleteMessage = LOCTEXT("CanDelete", "Delete this event handler.");
		return true;
	}
}

void UNiagaraStackEventScriptItemGroup::Delete()
{
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModelPinned = ScriptViewModel.Pin();
	checkf(ScriptViewModelPinned.IsValid(), TEXT("Can not delete when the script view model has been deleted."));

	UNiagaraEmitter* Emitter = GetEmitterViewModel()->GetEmitter();
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Emitter->GraphSource);

	if (!Source || !Source->NodeGraph)
	{
		return;
	}

	//Need to tear down existing systems now.
	FNiagaraSystemUpdateContext UpdateCtx;
	UpdateCtx.SetDestroyOnAdd(true);
	UpdateCtx.Add(Emitter, true);

	FScopedTransaction Transaction(FText::Format(LOCTEXT("DeleteEventHandler", "Deleted {0}"), GetDisplayName()));
	Emitter->Modify();
	Source->NodeGraph->Modify();
	TArray<UNiagaraNode*> EventIndexNodes;
	Source->NodeGraph->BuildTraversal(EventIndexNodes, GetScriptUsage(), GetScriptUsageId());
	for (UNiagaraNode* Node : EventIndexNodes)
	{
		Node->Modify();
	}
	
	// First, remove the event handler script properties object.
	Emitter->RemoveEventHandlerByUsageId(GetScriptUsageId());
	
	// Now remove all graph nodes associated with the event script index.
	for (UNiagaraNode* Node : EventIndexNodes)
	{
		Node->DestroyNode();
	}

	// Set the emitter here to that the internal state of the view model is updated.
	// TODO: Move the logic for managing event handlers into the emitter view model or script view model.
	ScriptViewModelPinned->SetScripts(Emitter);
	
	OnModifiedEventHandlersDelegate.ExecuteIfBound();
}

bool UNiagaraStackEventScriptItemGroup::HasBaseEventHandler() const
{
	if (bHasBaseEventHandlerCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		bHasBaseEventHandlerCache = BaseEmitter != nullptr && FNiagaraScriptMergeManager::Get()->HasBaseEventHandler(*BaseEmitter, GetScriptUsageId());
	}
	return bHasBaseEventHandlerCache.GetValue();
}

void UNiagaraStackEventScriptItemGroup::SetOnModifiedEventHandlers(FOnModifiedEventHandlers OnModifiedEventHandlers)
{
	OnModifiedEventHandlersDelegate = OnModifiedEventHandlers;
}

#undef LOCTEXT_NAMESPACE
