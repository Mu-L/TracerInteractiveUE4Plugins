// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeInput.h"
#include "UObject/UnrealType.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraGraph.h"

#include "NiagaraNodeOutput.h"
#include "NiagaraNodeOp.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraDataInterface.h"
#include "SNiagaraGraphNodeInput.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeInput"

DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - UNiagaraNodeInput - SortNodes"), STAT_NiagaraEditor_UNiagaraNodeInput_SortNodes, STATGROUP_NiagaraEditor);

UNiagaraNodeInput::UNiagaraNodeInput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Usage(ENiagaraInputNodeUsage::Undefined)
	, CallSortPriority(0)
	, DataInterface(nullptr)
{
	bCanRenameNode = true;
}

void UNiagaraNodeInput::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		if (UClass* Class = const_cast<UClass*>(Input.GetType().GetClass()))
		{
			check(Class->IsChildOf(UNiagaraDataInterface::StaticClass()));
			if (DataInterface)
			{
				if (DataInterface->GetClass() != Class)
				{
					//Class has changed so clear this out and allocate pins will create a new instance of the correct type.
					//Should we preserve old objects somewhere so settings aren't lost when switching around types?
					DataInterface = nullptr;
				}
				else
				{
					//Keep it with the same name as the input 
					if (PropertyChangedEvent.Property->GetName() == TEXT("Input"))
					{
						DataInterface->Rename(*Input.GetName().ToString());
					}
				}
			}
		}
		else
		{
			DataInterface = nullptr;
		}

		ReallocatePins();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNiagaraNodeInput::PostLoad()
{
	Super::PostLoad();
	if (DataInterface != nullptr)
	{
		DataInterface->OnChanged().AddUObject(this, &UNiagaraNodeInput::DataInterfaceChanged);
	}
}

void UNiagaraNodeInput::BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive /*= true*/, bool bFilterForCompilation /*= true*/) const
{
	if (!IsNodeEnabled() && OutHistory.GetIgnoreDisabled())
	{
		RouteParameterMapAroundMe(OutHistory, bRecursive);
		return;
	}

	if (Input.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
	{
		int32 ParamMapIdx = OutHistory.FindMatchingParameterMapFromContextInputs(Input);

		if (ParamMapIdx == INDEX_NONE && Usage != ENiagaraInputNodeUsage::TranslatorConstant)
		{
			ParamMapIdx = OutHistory.CreateParameterMap();
		}
		else if (ParamMapIdx == INDEX_NONE && OutHistory.Histories.Num() != 0)
		{
			ParamMapIdx = 0;
		}

		if (ParamMapIdx != INDEX_NONE)
		{
			uint32 NodeIdx = OutHistory.BeginNodeVisitation(ParamMapIdx, this);
			OutHistory.EndNodeVisitation(ParamMapIdx, NodeIdx);

			OutHistory.RegisterParameterMapPin(ParamMapIdx, GetOutputPin(0));
		}
	}
}


void UNiagaraNodeInput::AllocateDefaultPins()
{
	if (UClass* Class = const_cast<UClass*>(Input.GetType().GetClass()))
	{
		check(Class->IsChildOf(UNiagaraDataInterface::StaticClass()));
		if (!DataInterface)
		{
			DataInterface = NewObject<UNiagaraDataInterface>(this, Class, NAME_None, RF_Transactional | RF_Public);
		}
	}

	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	//If we're a parameter node for a funciton or a module the we allow a "default" input pin.
	UNiagaraScript* OwnerScript = GetTypedOuter<UNiagaraScript>();
	if (OwnerScript)
	{
		if ((!IsRequired() && IsExposed()) && DataInterface == nullptr && Usage == ENiagaraInputNodeUsage::Parameter && (OwnerScript->IsStandaloneScript()))
		{
			UEdGraphPin* NewPin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Input.GetType()), TEXT("Default"));
			NewPin->bDefaultValueIsIgnored = true;
		}
	}

	CreatePin(EGPD_Output, Schema->TypeDefinitionToPinType(Input.GetType()), TEXT("Input"));
}

FText UNiagaraNodeInput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromName(Input.GetName());
}

int32 UNiagaraNodeInput::GenerateNewSortPriority(const UNiagaraGraph* Graph, FName& ProposedName, ENiagaraInputNodeUsage Usage)
{
	int32 HighestSortOrder = -1; // Set to -1 initially, so that if there are no nodes, the return value will be zero.
	
	if (Usage == ENiagaraInputNodeUsage::Parameter && Graph != nullptr)
	{
		TArray<UNiagaraNodeInput*> InputNodes;
		Graph->GetNodesOfClass(InputNodes);
		for (UNiagaraNodeInput* InputNode : InputNodes)
		{
			if (InputNode->Usage == Usage && InputNode->CallSortPriority > HighestSortOrder)
			{
				HighestSortOrder = InputNode->CallSortPriority;
			}
		}
	}
	return HighestSortOrder + 1;
}

FName UNiagaraNodeInput::GenerateUniqueName(const UNiagaraGraph* Graph, FName& ProposedName, ENiagaraInputNodeUsage Usage)
{
	check(Usage != ENiagaraInputNodeUsage::SystemConstant && Usage != ENiagaraInputNodeUsage::Undefined);
	TSet<FName> InputNames;
	if (Usage == ENiagaraInputNodeUsage::Parameter && Graph != nullptr)
	{
		TArray<UNiagaraNodeInput*> InputNodes;
		Graph->GetNodesOfClass(InputNodes);
		for (UNiagaraNodeInput* InputNode : InputNodes)
		{
			if (InputNode->Usage == Usage)
			{
				InputNames.Add(InputNode->Input.GetName());
			}
		}
	}
	else if (Usage == ENiagaraInputNodeUsage::Attribute && Graph != nullptr)
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* Node : OutputNodes)
		{
			for (const FNiagaraVariable& Output : Node->Outputs)
			{
				InputNames.Add(Output.GetName());
			}
		}
	}

	return FNiagaraUtilities::GetUniqueName(ProposedName, FNiagaraEditorUtilities::GetSystemConstantNames().Union(InputNames));
}

bool UNiagaraNodeInput::VerifyNodeRenameTextCommit(const FText& NewText, UNiagaraNode* NodeBeingChanged, FText& OutErrorMessage)
{
	FName NewName = *NewText.ToString();
	TSet<FName> SystemConstantNames = FNiagaraEditorUtilities::GetSystemConstantNames();

	// Disallow empty names
	if (NewName == FName())
	{
		OutErrorMessage = LOCTEXT("NiagaraInputNameEmptyWarn", "Cannot have empty name!");
		return false;
	}

	// Disallow name changes to system constants
	if (SystemConstantNames.Contains(NewName))
	{
		OutErrorMessage = FText::Format(LOCTEXT("NiagaraInputNameSystemWarn", "\"{0}\" is a system constant name."), FText::FromName(NewName));
		return false;
	}

	// @TODO: Prevent any hlsl keywords or invalid hlsl characters from being used as names!

	UNiagaraNodeInput* InputNodeBeingChanged = Cast<UNiagaraNodeInput>(NodeBeingChanged);
	UNiagaraNodeOutput* OutputNodeBeingChanged = Cast<UNiagaraNodeOutput>(NodeBeingChanged);

	// Make sure that we aren't changing names to something already in the graph.
	if (NodeBeingChanged != nullptr)
	{
		UNiagaraGraph* Graph = CastChecked<UNiagaraGraph>(NodeBeingChanged->GetGraph());

		// If dealing with parameter, check to make sure that we don't conflict with any other parameter name.
		if (InputNodeBeingChanged != nullptr && InputNodeBeingChanged->Usage == ENiagaraInputNodeUsage::Parameter)
		{
			TArray<UNiagaraNodeInput*> InputNodes;
			Graph->GetNodesOfClass<UNiagaraNodeInput>(InputNodes);

			for (UNiagaraNodeInput* Node : InputNodes)
			{
				if (Node == nullptr || Node == InputNodeBeingChanged || Node->Usage != InputNodeBeingChanged->Usage)
				{
					continue;
				}

				bool bIsSame = Node->ReferencesSameInput(InputNodeBeingChanged);
				// This should still allow case changes b/c we test to make sure that they aren't referencing the same node.
				if (bIsSame == false && Node->Input.GetName().IsEqual(NewName, ENameCase::IgnoreCase))
				{
					OutErrorMessage = FText::Format(LOCTEXT("NiagaraInputNameSameParameterWarn", "\"{0}\" is the name of another parameter."), FText::FromName(NewName));
					return false;
				}
			}
		}

		// If dealing with Attributes, check to make sure that we don't conflict with any other attribute name.
		if ((InputNodeBeingChanged != nullptr && InputNodeBeingChanged->Usage == ENiagaraInputNodeUsage::Attribute) || OutputNodeBeingChanged != nullptr)
		{
			TArray<UNiagaraNodeOutput*> OutputNodes;
			Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
			for (UNiagaraNodeOutput* Node : OutputNodes)
			{
				for (const FNiagaraVariable& Output : Node->Outputs)
				{
					if (InputNodeBeingChanged != nullptr && Output.GetName().IsEqual(InputNodeBeingChanged->Input.GetName(), ENameCase::IgnoreCase))
					{
						continue;
					}

					if (Output.GetName().IsEqual(NewName, ENameCase::IgnoreCase))
					{
						OutErrorMessage = FText::Format(LOCTEXT("NiagaraInputNameSameAttributeWarn", "\"{0}\" is the name of another attribute. Hit \"Escape\" to cancel edit."), FText::FromName(NewName));
						return false;
					}
				}
			}
		}
	}
	return true;
}

void UNiagaraNodeInput::OnRenameNode(const FString& NewName)
{
	UNiagaraGraph* Graph = CastChecked<UNiagaraGraph>(GetGraph());
	TArray<UNiagaraNodeInput*> InputNodes;
	Graph->GetNodesOfClass<UNiagaraNodeInput>(InputNodes);

	TArray<UNiagaraNodeInput*> AffectedNodes;
	AffectedNodes.Add(this);
	for (UNiagaraNodeInput* Node : InputNodes)
	{
		if (Node == this || Node == nullptr)
		{
			continue;
		}

		bool bNeedsSync = Node->ReferencesSameInput(this);
		if (bNeedsSync)
		{
			AffectedNodes.Add(Node);
		}
	}

	for (UNiagaraNodeInput* Node : AffectedNodes)
	{
		Node->Modify();
		Node->Input.SetName(FName(*NewName));
		if (DataInterface != nullptr)
		{
			Node->GetDataInterface()->Rename(*NewName);
		}
		Node->ReallocatePins();
	}

	Graph->MarkGraphRequiresSynchronization(TEXT("Input node renamed"));
}

TSharedPtr<SGraphNode> UNiagaraNodeInput::CreateVisualWidget()
{
	return SNew(SNiagaraGraphNodeInput, this);
}

FLinearColor UNiagaraNodeInput::GetNodeTitleColor() const
{
	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());
	switch (Usage)
	{
	case ENiagaraInputNodeUsage::Parameter:
		return Schema->NodeTitleColor_Constant;
	case ENiagaraInputNodeUsage::SystemConstant:
		return Schema->NodeTitleColor_SystemConstant;
	case ENiagaraInputNodeUsage::Attribute:
		return Schema->NodeTitleColor_Attribute;
	case ENiagaraInputNodeUsage::TranslatorConstant:
		return Schema->NodeTitleColor_TranslatorConstant;
	case ENiagaraInputNodeUsage::RapidIterationParameter:
		return Schema->NodeTitleColor_RapidIteration;
	default:
		// TODO: Do something better here.
		return FLinearColor::Black;
	}
}

bool UNiagaraNodeInput::ReferencesSameInput(UNiagaraNodeInput* Other) const
{
	if (this == Other)
	{
		return true;
	}

	if (Other == nullptr)
	{
		return false;
	}

	if (Usage == Other->Usage)
	{	
		if (Input.GetName() == Other->Input.GetName())
		{
			return true;
		}	
	}
	return false;
}


void UNiagaraNodeInput::AutowireNewNode(UEdGraphPin* FromPin)
{
	UEdGraphPin* OutputPin = GetOutputPin(0);
	if (FromPin != nullptr && OutputPin && OutputPin->PinType == FromPin->PinType)
	{
		const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());
		check(Schema);

		if (Usage == ENiagaraInputNodeUsage::Parameter)
		{
			TArray<UNiagaraNodeInput*> InputNodes;
			GetGraph()->GetNodesOfClass(InputNodes);
			int32 NumMatches = 0;
			int32 HighestSortPriority = -1; // Set to -1 initially, so that in the event of no nodes, we still get zero.
			for (UNiagaraNodeInput* InputNode : InputNodes)
			{
				if (InputNode == nullptr)
				{
					continue;
				}

				if (InputNode != this && InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
				{
					if (ReferencesSameInput(InputNode))
					{
						NumMatches++;

						check(InputNode->Input.GetName() == this->Input.GetName());
						check(InputNode->ExposureOptions.bCanAutoBind == this->ExposureOptions.bCanAutoBind);
						check(InputNode->ExposureOptions.bExposed == this->ExposureOptions.bExposed);
						check(InputNode->ExposureOptions.bHidden == this->ExposureOptions.bHidden);
						check(InputNode->ExposureOptions.bRequired == this->ExposureOptions.bRequired);
						check(InputNode->DataInterface == this->DataInterface);
					}

					if (InputNode->CallSortPriority > HighestSortPriority)
					{
						HighestSortPriority = InputNode->CallSortPriority;
					}
				}
			}

			FName CandidateName = Input.GetName();
			FNiagaraTypeDefinition Type = Input.GetType();
			if (Type == FNiagaraTypeDefinition::GetGenericNumericDef())
			{
				//Try to get a real type if we've been set to numeric
				Type = Schema->PinToTypeDefinition(FromPin);
			}

			CallSortPriority = HighestSortPriority + 1;
			ReallocatePins();
		}

		TArray<UEdGraphPin*> OutPins;
		GetOutputPins(OutPins);
		check(OutPins.Num() == 1 && OutPins[0] != NULL);

		if (GetSchema()->TryCreateConnection(FromPin, OutPins[0]))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
}

void UNiagaraNodeInput::NotifyInputTypeChanged()
{
	ReallocatePins();
}


void UNiagaraNodeInput::NotifyExposureOptionsChanged()
{
	ReallocatePins();
}

void UNiagaraNodeInput::Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs)
{
	if (!IsNodeEnabled())
	{
		Outputs.Add(INDEX_NONE);
		return;
	}

	UNiagaraGraph* Graph = GetNiagaraGraph();
	if (Input.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
	{
		Outputs.Add(INDEX_NONE);
		Translator->Error(LOCTEXT("InvalidPinType", "Numeric types should be able to be inferred from use by this phase of compilation."), this, nullptr);
		return;
	}

	int32 FunctionParam = INDEX_NONE;
	if (IsExposed() && Translator->GetFunctionParameter(Input, FunctionParam))
	{
		//If we're in a function and this parameter hasn't been provided, compile the local default.
		if (FunctionParam == INDEX_NONE)
		{
			TArray<UEdGraphPin*> InputPins;
			GetInputPins(InputPins);
			int32 Default = InputPins.Num() > 0 ? Translator->CompilePin(InputPins[0]) : INDEX_NONE;
			if (Default == INDEX_NONE)
			{
				//We failed to compile the default pin so just use the value of the input.
				if (Usage == ENiagaraInputNodeUsage::Parameter && DataInterface != nullptr)
				{
					check(Input.GetType().GetClass());
					Outputs.Add(Translator->RegisterDataInterface(Input, DataInterface, false, false));
					return;
				}
				else
				{
					Default = Translator->GetConstant(Input);
				}
			}
			Outputs.Add(Default);
			return;
		}
	}

	switch (Usage)
	{
	case ENiagaraInputNodeUsage::Parameter:
		if (DataInterface)
		{
			check(Input.GetType().GetClass());
			Outputs.Add(Translator->RegisterDataInterface(Input, DataInterface, false, false)); break;
		}
		else
		{
			Outputs.Add(Translator->GetParameter(Input)); break;
		}
	case ENiagaraInputNodeUsage::SystemConstant:
		Outputs.Add(Translator->GetParameter(Input)); break;
	case ENiagaraInputNodeUsage::Attribute:
		Outputs.Add(Translator->GetAttribute(Input)); break;
	case ENiagaraInputNodeUsage::TranslatorConstant:
		Outputs.Add(Translator->GetParameter(Input)); break;
	case ENiagaraInputNodeUsage::RapidIterationParameter:
		Outputs.Add(Translator->GetRapidIterationParameter(Input)); break;
	default:
		check(false);
	}
}

void UNiagaraNodeInput::SortNodes(TArray<UNiagaraNodeInput*>& InOutNodes)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_UNiagaraNodeInput_SortNodes);

	auto SortVars = [](UNiagaraNodeInput& A, UNiagaraNodeInput& B)
	{
		if (A.CallSortPriority < B.CallSortPriority)
		{
			return true;
		}
		else if (A.CallSortPriority > B.CallSortPriority)
		{
			return false;
		}

		//If equal priority, sort lexicographically.
		return A.Input.GetName().ToString() < B.Input.GetName().ToString();
	};
	InOutNodes.Sort(SortVars);
}

UNiagaraDataInterface* UNiagaraNodeInput::GetDataInterface() const
{
	return DataInterface;
}

void UNiagaraNodeInput::SetDataInterface(UNiagaraDataInterface* InDataInterface)
{
	if (DataInterface != nullptr)
	{
		DataInterface->OnChanged().RemoveAll(this);
	}
	DataInterface = InDataInterface;
	if (DataInterface != nullptr)
	{
		DataInterface->OnChanged().AddUObject(this, &UNiagaraNodeInput::DataInterfaceChanged);
	}
	DataInterfaceChanged();
}

void UNiagaraNodeInput::DataInterfaceChanged()
{
	// Don't use GetNiagaraGraph() here since this may be called on a temporary node which isn't
	// in a proper graph yet.
	UNiagaraGraph* NiagaraGraph = Cast<UNiagaraGraph>(GetGraph());
	if (NiagaraGraph != nullptr)
	{
		NiagaraGraph->NotifyGraphDataInterfaceChanged();
	}
}

#undef LOCTEXT_NAMESPACE
