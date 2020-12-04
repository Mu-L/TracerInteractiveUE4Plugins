// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneEventUtils.h"

#include "Algo/Find.h"

#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

#include "MovieSceneSequence.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "MovieSceneEventBlueprintExtension.h"

#include "ScopedTransaction.h"

#include "Engine/Blueprint.h"

#include "KismetCompiler.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"

#define LOCTEXT_NAMESPACE "MovieSceneEventUtils"

FMovieSceneEventEndpointParameters FMovieSceneEventEndpointParameters::Generate(UMovieSceneEventTrack* Track)
{
	check(Track);

	UMovieScene* MovieScene = Track->GetTypedOuter<UMovieScene>();

	FGuid ObjectBindingID;
	if (MovieScene->FindTrackBinding(*Track, ObjectBindingID))
	{
		return Generate(MovieScene, ObjectBindingID);
	}

	FMovieSceneEventEndpointParameters Params;
	Params.SanitizedObjectName = TEXT("None");
	Params.SanitizedEventName  = TEXT("SequenceEvent");

	return Params;
}

FMovieSceneEventEndpointParameters FMovieSceneEventEndpointParameters::Generate(UMovieScene* MovieScene, const FGuid& ObjectBindingID)
{
	check(MovieScene);

	FMovieSceneEventEndpointParameters Params;

	if (ObjectBindingID.IsValid())
	{
		Params.SanitizedObjectName = MovieScene->GetObjectDisplayName(ObjectBindingID).ToString();

		for (TCHAR& Char : Params.SanitizedObjectName)
		{
			if (FCString::Strchr(INVALID_NAME_CHARACTERS, Char) != nullptr)
			{
				Char = '_';
			}
		}

		Params.SanitizedEventName = Params.SanitizedObjectName + TEXT("_Event");

		if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindingID))
		{
			Params.BoundObjectPinClass = const_cast<UClass*>(Possessable->GetPossessedObjectClass());
		}
		else if(FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectBindingID))
		{
			Params.BoundObjectPinClass = Spawnable->GetObjectTemplate()->GetClass();
		}
	}
	else
	{
		Params.BoundObjectPinClass = nullptr;
		Params.SanitizedObjectName = TEXT("None");
		Params.SanitizedEventName  = TEXT("SequenceEvent");
	}

	return Params;
}

UK2Node_CustomEvent* FMovieSceneEventUtils::BindNewUserFacingEvent(FMovieSceneEvent* EntryPoint, UMovieSceneEventSectionBase* EventSection, UBlueprint* Blueprint)
{
	check(EntryPoint && EventSection && Blueprint);

	UMovieSceneEventTrack* Track = EventSection->GetTypedOuter<UMovieSceneEventTrack>();

	// Modify necessary objects
	EventSection->Modify();
	Blueprint->Modify();

	// Ensure the section is bound to the blueprint function generation event
	FMovieSceneEventUtils::BindEventSectionToBlueprint(EventSection, Blueprint);

	// Create the new user-facing event node
	FMovieSceneEventEndpointParameters Params = FMovieSceneEventEndpointParameters::Generate(Track);
	UK2Node_CustomEvent* NewEventNode = CreateUserFacingEvent(Blueprint, Params);

	if (NewEventNode)
	{
		// Bind the node to the event entry point
		UEdGraphPin* BoundObjectPin = FMovieSceneEventUtils::FindBoundObjectPin(NewEventNode, Params.BoundObjectPinClass);
		FMovieSceneEventUtils::SetEndpoint(EntryPoint, EventSection, NewEventNode, BoundObjectPin);
	}

	return NewEventNode;
}

UK2Node_CustomEvent* FMovieSceneEventUtils::CreateUserFacingEvent(UBlueprint* Blueprint, const FMovieSceneEventEndpointParameters& Parameters)
{
	static const TCHAR* const EventGraphName = TEXT("Sequencer Events");
	UEdGraph* SequenceEventGraph = FindObject<UEdGraph>(Blueprint, EventGraphName);
	if (!SequenceEventGraph)
	{
		SequenceEventGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, EventGraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

		SequenceEventGraph->GraphGuid = FGuid::NewGuid();
		Blueprint->UbergraphPages.Add(SequenceEventGraph);
	}

	// Create a custom event node to replace the original event node imported from text
	UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(SequenceEventGraph);

	check(Parameters.SanitizedEventName.Len() != 0);
	CustomEventNode->CustomFunctionName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, Parameters.SanitizedEventName);

	// Ensure that it is editable
	CustomEventNode->bIsEditable = true;

	CustomEventNode->CreateNewGuid();
	CustomEventNode->PostPlacedNewNode();
	CustomEventNode->AllocateDefaultPins();

	const FVector2D NewPosition = SequenceEventGraph->GetGoodPlaceForNewNode();
	CustomEventNode->NodePosX = NewPosition.X;
	CustomEventNode->NodePosY = NewPosition.Y;

	if (Parameters.BoundObjectPinClass != nullptr)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = Parameters.BoundObjectPinClass;

		CustomEventNode->CreateUserDefinedPin(*Parameters.SanitizedObjectName, PinType, EGPD_Output, true);
	}

	SequenceEventGraph->AddNode(CustomEventNode, false, false);

	FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, CustomEventNode->CustomFunctionName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return CustomEventNode;
}

UK2Node* FMovieSceneEventUtils::FindEndpoint(FMovieSceneEvent* EntryPoint, UMovieSceneEventSectionBase* EventSection, UBlueprint* OwnerBlueprint)
{
	check(OwnerBlueprint);
	check(EntryPoint);

	if (EntryPoint->WeakEndpoint.IsStale())
	{
		return nullptr;
	}
	if (UK2Node* Node = Cast<UK2Node>(EntryPoint->WeakEndpoint.Get()))
	{
		return Node;
	}

	if (!EntryPoint->GraphGuid_DEPRECATED.IsValid())
	{
		return nullptr;
	}

	if (EntryPoint->NodeGuid_DEPRECATED.IsValid())
	{
		for (UEdGraph* Graph : OwnerBlueprint->UbergraphPages)
		{
			if (Graph->GraphGuid == EntryPoint->GraphGuid_DEPRECATED)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node->NodeGuid == EntryPoint->NodeGuid_DEPRECATED)
					{
						UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node);
						if (ensureMsgf(CustomEvent, TEXT("Encountered an event entry point node that is bound to something other than a custom event")))
						{
							CustomEvent->OnUserDefinedPinRenamed().AddUObject(EventSection, &UMovieSceneEventSectionBase::OnUserDefinedPinRenamed);
							EntryPoint->WeakEndpoint = CustomEvent;
							return CustomEvent;
						}
					}
				}
			}
		}
	}
	// If the node guid is invalid, this must be a function graph on the BP
	else for (UEdGraph* Graph : OwnerBlueprint->FunctionGraphs)
	{
		if (Graph->GraphGuid == EntryPoint->GraphGuid_DEPRECATED)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
				{
					FunctionEntry->OnUserDefinedPinRenamed().AddUObject(EventSection, &UMovieSceneEventSectionBase::OnUserDefinedPinRenamed);
					EntryPoint->WeakEndpoint = FunctionEntry;
					return FunctionEntry;
				}
			}
		}
	}

	return nullptr;
}

UEdGraphPin* FMovieSceneEventUtils::FindBoundObjectPin(UK2Node* InEndpoint, UClass* BoundObjectPinClass)
{
	if (!BoundObjectPinClass)
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : InEndpoint->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && Pin->PinType.PinSubCategoryObject == BoundObjectPinClass)
		{
			return Pin;
		}
	}

	return nullptr;
}

void FMovieSceneEventUtils::SetEndpoint(FMovieSceneEvent* EntryPoint, UMovieSceneEventSectionBase* EventSection, UK2Node* InNewEndpoint, UEdGraphPin* BoundObjectPin)
{
	check(EntryPoint);

	UK2Node* ExistingEndpoint = CastChecked<UK2Node>(EntryPoint->WeakEndpoint.Get(), ECastCheckedType::NullAllowed);
	if (ExistingEndpoint)
	{
		ExistingEndpoint->OnUserDefinedPinRenamed().RemoveAll(EventSection);
	}

	if (InNewEndpoint)
	{
		const bool bIsFunction    = InNewEndpoint->IsA<UK2Node_FunctionEntry>();
		const bool bIsCustomEvent = InNewEndpoint->IsA<UK2Node_CustomEvent>();

		checkf(bIsFunction || bIsCustomEvent, TEXT("Only functions and custom events are supported as event endpoints"));


		if (BoundObjectPin)
		{
			EntryPoint->BoundObjectPinName = BoundObjectPin->GetFName();
		}
		else
		{
			EntryPoint->BoundObjectPinName = NAME_None;
		}

		InNewEndpoint->OnUserDefinedPinRenamed().AddUObject(EventSection, &UMovieSceneEventSectionBase::OnUserDefinedPinRenamed);
		EntryPoint->WeakEndpoint = InNewEndpoint;
	}
	else
	{
		EntryPoint->WeakEndpoint = nullptr;
		EntryPoint->BoundObjectPinName = NAME_None;
	}
}


UK2Node_FunctionEntry* FMovieSceneEventUtils::GenerateEntryPoint(UMovieSceneEventSectionBase* EventSection, FMovieSceneEvent* EntrypointDefinition, FKismetCompilerContext* Compiler, UEdGraphNode* Endpoint)
{
	check(EventSection && Compiler && Endpoint);

	UBlueprint* Blueprint = Compiler->Blueprint;

	UFunction* EndpointFunction = nullptr;

	// Find the function that we need to call on the skeleton class
	{
		if (UK2Node_Event* Event = Cast<UK2Node_Event>(Endpoint))
		{
			EndpointFunction = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(Event->GetFunctionName()) : nullptr;
		}
		else if (UK2Node_FunctionEntry* EndpointEntry = Cast<UK2Node_FunctionEntry>(Endpoint))
		{
			EndpointFunction = EndpointEntry->FindSignatureFunction();
		}
		else
		{
			Compiler->MessageLog.Error(*LOCTEXT("InvalidEndpoint_Error", "Sequencer event is bound to an invalid endpoint node @@").ToString(), Endpoint);
		}
	}

	if (EndpointFunction == nullptr)
	{
		return nullptr;
	}

	// @todo: use a more destriptive name for event entry points?
	static FString DefaultEventEntryName = TEXT("SequenceEvent__ENTRYPOINT");
	UEdGraph* EntryPointGraph = Compiler->SpawnIntermediateFunctionGraph(DefaultEventEntryName + Blueprint->GetName());
	check(EntryPointGraph->Nodes.Num() == 1);

	const UEdGraphSchema*  Schema        = EntryPointGraph->GetSchema();
	UK2Node_FunctionEntry* FunctionEntry = CastChecked<UK2Node_FunctionEntry>(EntryPointGraph->Nodes[0]);

	// -------------------------------------------------------------------------------------
	// Locate and initialize the function entry node
	{
		int32 ExtraFunctionFlags = ( FUNC_BlueprintEvent | FUNC_Public );
		FunctionEntry->AddExtraFlags(ExtraFunctionFlags);
		FunctionEntry->bIsEditable = false;
		FunctionEntry->MetaData.Category = LOCTEXT("DefaultCategory", "Sequencer Event Endpoints");
		FunctionEntry->MetaData.bCallInEditor = EndpointFunction->GetBoolMetaData(FBlueprintMetadata::MD_CallInEditor);
	}

	// -------------------------------------------------------------------------------------
	// Create a function call node to invoke the endpoint function itself
	UK2Node_CallFunction* CallFunctionNode = NewObject<UK2Node_CallFunction>(EntryPointGraph);
	{
		CallFunctionNode->CreateNewGuid();
		CallFunctionNode->PostPlacedNewNode();

		CallFunctionNode->FunctionReference.SetSelfMember(EndpointFunction->GetFName());

		CallFunctionNode->ReconstructNode();

		CallFunctionNode->NodePosX = FunctionEntry->NodePosX + 400.f;
		CallFunctionNode->NodePosY = FunctionEntry->NodePosY - 16.f;

		EntryPointGraph->AddNode(CallFunctionNode, false, false);
	}

	// -------------------------------------------------------------------------------------
	// Create a pin for the bound object if possible
	if (EntrypointDefinition->BoundObjectPinName != NAME_None)
	{
		UEdGraphPin* BoundObjectPin = CallFunctionNode->FindPin(EntrypointDefinition->BoundObjectPinName, EGPD_Input);
		if (BoundObjectPin)
		{
			UEdGraphPin* BoundObjectPinInput = FunctionEntry->CreateUserDefinedPin(BoundObjectPin->PinName, BoundObjectPin->PinType, EGPD_Output, true);
			if (BoundObjectPinInput)
			{
				BoundObjectPinInput->MakeLinkTo(BoundObjectPin);
			}
		}
	}

	// -------------------------------------------------------------------------------------
	// Wire up the function entry 'then' pin to the call function 'execute' pin
	{
		// Wire up the exec nodes
		UEdGraphPin* ThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* ExecPin = CallFunctionNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);

		if (ensure(ThenPin && ExecPin))
		{
			ThenPin->MakeLinkTo(ExecPin);
		}
	}

	// -------------------------------------------------------------------------------------
	// Set pin defaults for each of the relevant pins on the call function node according to the payload
	{
		TArray<FName, TInlineAllocator<8>> ValidPinNames;

		// Gather all input pins for the function call
		for (UEdGraphPin* Pin : CallFunctionNode->Pins)
		{
			// Only consider input pins that are data, and not already connected
			if (Pin->Direction != EGPD_Input || Pin->PinName == UEdGraphSchema_K2::PN_Execute || Pin->PinName == UEdGraphSchema_K2::PN_Self || Pin->LinkedTo.Num() != 0)
			{
				continue;
			}

			const FMovieSceneEventPayloadVariable* PayloadVariable = EntrypointDefinition->PayloadVariables.Find(Pin->PinName);
			if (PayloadVariable)
			{
				bool bMarkAsModified = false;
				Schema->TrySetDefaultValue(*Pin, PayloadVariable->Value, bMarkAsModified);

				ValidPinNames.Add(Pin->PinName);
			}
		}

		// Remove any invalid payload variable names
		if (ValidPinNames.Num() != EntrypointDefinition->PayloadVariables.Num())
		{
			for (auto It = EntrypointDefinition->PayloadVariables.CreateIterator(); It; ++It)
			{
				if (!ValidPinNames.Contains(It->Key))
				{
					Compiler->MessageLog.Note(*FText::Format(LOCTEXT("PayloadParameterRemoved", "Stale Sequencer event payload parameter %s has been removed."), FText::FromName(It->Key)).ToString());
					It.RemoveCurrent();
				}
			}
		}
	}

	return FunctionEntry;
}

void FMovieSceneEventUtils::BindEventSectionToBlueprint(UMovieSceneEventSectionBase* EventSection, UBlueprint* DirectorBP)
{
	check(EventSection && DirectorBP);

	for (UBlueprintExtension* Extension : DirectorBP->Extensions)
	{
		UMovieSceneEventBlueprintExtension* EventExtension = Cast<UMovieSceneEventBlueprintExtension>(Extension);
		if (EventExtension)
		{
			EventExtension->Add(EventSection);
			return;
		}
	}

	UMovieSceneEventBlueprintExtension* EventExtension = NewObject<UMovieSceneEventBlueprintExtension>(DirectorBP);
	EventExtension->Add(EventSection);
	DirectorBP->Extensions.Add(EventExtension);
}

#undef LOCTEXT_NAMESPACE