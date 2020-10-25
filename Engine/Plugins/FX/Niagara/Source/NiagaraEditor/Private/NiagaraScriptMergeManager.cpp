// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraRendererProperties.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "NiagaraDataInterface.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraStackEditorData.h"

#include "UObject/PropertyPortFlags.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Modules/ModuleManager.h"
#include "NiagaraConstants.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptMergeManager"

DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - DiffEmitters"), STAT_NiagaraEditor_ScriptMergeManager_DiffEmitters, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - MergeEmitter"), STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - IsModuleInputDifferentFromBase"), STAT_NiagaraEditor_ScriptMergeManager_IsModuleInputDifferentFromBase, STATGROUP_NiagaraEditor);

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(
	const UNiagaraEmitter& InOwningEmitter,
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	UEdGraphPin& InOverridePin
)
	: OwningScript(&InOwningScript)
	, OwningFunctionCallNode(&InOwningFunctionCallNode)
	, OverridePin(&InOverridePin)
	, DataValueObject(nullptr)
{
	
	InputName = FNiagaraParameterHandle(OverridePin->PinName).GetName().ToString();
	OverrideNode = CastChecked<UNiagaraNodeParameterMapSet>(OverridePin->GetOwningNode());
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	Type = NiagaraSchema->PinToTypeDefinition(OverridePin);

	if (OverridePin->LinkedTo.Num() == 0)
	{
		LocalValueString = OverridePin->DefaultValue;
	}
	else if (OverridePin->LinkedTo.Num() == 1)
	{
		OverrideValueNodePersistentId = OverridePin->LinkedTo[0]->GetOwningNode()->NodeGuid;

		if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeParameterMapGet>())
		{
			LinkedValueHandle = FNiagaraParameterHandle(OverridePin->LinkedTo[0]->PinName);
		}
		else if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeInput>())
		{
			UNiagaraNodeInput* DataInputNode = CastChecked<UNiagaraNodeInput>(OverridePin->LinkedTo[0]->GetOwningNode());
			DataValueInputName = DataInputNode->Input.GetName();
			DataValueObject = DataInputNode->GetDataInterface();
		}
		else if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeFunctionCall>())
		{
			DynamicValueFunction = MakeShared<FNiagaraStackFunctionMergeAdapter>(InOwningEmitter, *OwningScript.Get(), *CastChecked<UNiagaraNodeFunctionCall>(OverridePin->LinkedTo[0]->GetOwningNode()), INDEX_NONE);
		}
		else
		{
			UE_LOG(LogNiagaraEditor, Error, TEXT("Invalid Stack Graph - Unsupported input node connection. Owning Node: %s"), *OverrideNode->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Error, TEXT("Invalid Stack Graph - Input had multiple connections. Owning Node: %s"), *OverrideNode->GetPathName());
	}
}

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	FString InInputName,
	FNiagaraVariable InRapidIterationParameter
)
	: OwningScript(&InOwningScript)
	, OwningFunctionCallNode(&InOwningFunctionCallNode)
	, InputName(InInputName)
	, Type(InRapidIterationParameter.GetType())
	, OverridePin(nullptr)
	, LocalValueRapidIterationParameter(InRapidIterationParameter)
	, DataValueObject(nullptr)
{
}

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(UEdGraphPin* InStaticSwitchPin)
	: OwningScript(nullptr)
	, OwningFunctionCallNode(CastChecked<UNiagaraNodeFunctionCall>(InStaticSwitchPin->GetOwningNode()))
	, InputName(InStaticSwitchPin->PinName.ToString())
	, OverridePin(nullptr)
	, DataValueObject(nullptr)
	, StaticSwitchValue(InStaticSwitchPin->DefaultValue)
{
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	Type = NiagaraSchema->PinToTypeDefinition(InStaticSwitchPin);
}

UNiagaraScript* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOwningScript() const
{
	return OwningScript.Get();
}

UNiagaraNodeFunctionCall* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOwningFunctionCall() const
{
	return OwningFunctionCallNode.Get();
}

FString FNiagaraStackFunctionInputOverrideMergeAdapter::GetInputName() const
{
	return InputName;
}

UNiagaraNodeParameterMapSet* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverrideNode() const
{
	return OverrideNode.Get();
}

const FNiagaraTypeDefinition& FNiagaraStackFunctionInputOverrideMergeAdapter::GetType() const
{
	return Type;
}

UEdGraphPin* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverridePin() const
{
	return OverridePin;
}

const FGuid& FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverrideNodeId() const
{
	return OverrideValueNodePersistentId;
}

TOptional<FString> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLocalValueString() const
{
	return LocalValueString;
}

TOptional<FNiagaraVariable> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLocalValueRapidIterationParameter() const
{
	return LocalValueRapidIterationParameter;
}

TOptional<FNiagaraParameterHandle> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLinkedValueHandle() const
{
	return LinkedValueHandle;
}

TOptional<FName> FNiagaraStackFunctionInputOverrideMergeAdapter::GetDataValueInputName() const
{
	return DataValueInputName;
}

UNiagaraDataInterface* FNiagaraStackFunctionInputOverrideMergeAdapter::GetDataValueObject() const
{
	return DataValueObject;
}

TSharedPtr<FNiagaraStackFunctionMergeAdapter> FNiagaraStackFunctionInputOverrideMergeAdapter::GetDynamicValueFunction() const
{
	return DynamicValueFunction;
}

TOptional<FString> FNiagaraStackFunctionInputOverrideMergeAdapter::GetStaticSwitchValue() const
{
	return StaticSwitchValue;
}

FNiagaraStackFunctionMergeAdapter::FNiagaraStackFunctionMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraScript& InOwningScript, UNiagaraNodeFunctionCall& InFunctionCallNode, int32 InStackIndex)
{
	OwningScript = &InOwningScript;
	FunctionCallNode = &InFunctionCallNode;
	StackIndex = InStackIndex;

	int32 EmitterScratchPadScriptIndex = InOwningEmitter.ScratchPadScripts.IndexOfByKey(FunctionCallNode->FunctionScript);
	int32 ParentEmitterScratchPadScriptIndex = InOwningEmitter.ParentScratchPadScripts.IndexOfByKey(FunctionCallNode->FunctionScript);
	if (EmitterScratchPadScriptIndex != INDEX_NONE)
	{
		ScratchPadScriptIndex = InOwningEmitter.ParentScratchPadScripts.Num() + EmitterScratchPadScriptIndex;
	}
	else if (ParentEmitterScratchPadScriptIndex != INDEX_NONE)
	{
		ScratchPadScriptIndex = ParentEmitterScratchPadScriptIndex;
	}
	else
	{
		ScratchPadScriptIndex = INDEX_NONE;
	}

	FString UniqueEmitterName = InOwningEmitter.GetUniqueEmitterName();

	TSet<FString> AliasedInputsAdded;
	UNiagaraNodeParameterMapSet* OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*FunctionCallNode);
	if (OverrideNode != nullptr)
	{
		TArray<UEdGraphPin*> OverridePins;
		OverrideNode->GetInputPins(OverridePins);
		for (UEdGraphPin* OverridePin : OverridePins)
		{
			if (OverridePin->PinType.PinCategory != UEdGraphSchema_Niagara::PinCategoryMisc &&
				OverridePin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct())
			{
				FNiagaraParameterHandle InputHandle(OverridePin->PinName);
				if (InputHandle.GetNamespace().ToString() == FunctionCallNode->GetFunctionName())
				{
					InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(InOwningEmitter, *OwningScript.Get(), *FunctionCallNode.Get(), *OverridePin));
					AliasedInputsAdded.Add(OverridePin->PinName.ToString());
				}
			}
		}
	}

	FString RapidIterationParameterNamePrefix = TEXT("Constants." + UniqueEmitterName + ".");
	TArray<FNiagaraVariable> RapidIterationParameters;
	OwningScript->RapidIterationParameters.GetParameters(RapidIterationParameters);
	for (const FNiagaraVariable& RapidIterationParameter : RapidIterationParameters)
	{
		FNiagaraParameterHandle AliasedInputHandle(*RapidIterationParameter.GetName().ToString().RightChop(RapidIterationParameterNamePrefix.Len()));
		if (AliasedInputHandle.GetNamespace().ToString() == FunctionCallNode->GetFunctionName())
		{
			// Currently rapid iteration parameters for assignment nodes in emitter scripts get double aliased which prevents their inputs from
			// being diffed correctly, so we need to un-mangle the names here so that the diffs are correct.
			if (FunctionCallNode->IsA<UNiagaraNodeAssignment>() &&
				(OwningScript->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || OwningScript->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript))
			{
				FString InputName = AliasedInputHandle.GetName().ToString();
				if (InputName.StartsWith(UniqueEmitterName + TEXT(".")))
				{
					FString UnaliasedInputName = TEXT("Emitter") + InputName.RightChop(UniqueEmitterName.Len());
					AliasedInputHandle = FNiagaraParameterHandle(AliasedInputHandle.GetNamespace(), *UnaliasedInputName);
				}
			}

			if (AliasedInputsAdded.Contains(AliasedInputHandle.GetParameterHandleString().ToString()) == false)
			{
				InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(*OwningScript.Get(), *FunctionCallNode.Get(), AliasedInputHandle.GetName().ToString(), RapidIterationParameter));
			}
		}
	}

	TArray<UEdGraphPin*> StaticSwitchPins;
	TSet<UEdGraphPin*> StaticSwitchPinsHidden;
	FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*FunctionCallNode.Get(), StaticSwitchPins, StaticSwitchPinsHidden);
	for (UEdGraphPin* StaticSwitchPin : StaticSwitchPins)
	{
		// TODO: Only add static switch overrides when the current value is different from the default.  This requires a 
		// refactor of the static switch default storage to use the same data format as FNiagaraVariables.
		InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(StaticSwitchPin));
	}
}

UNiagaraNodeFunctionCall* FNiagaraStackFunctionMergeAdapter::GetFunctionCallNode() const
{
	return FunctionCallNode.Get();
}

int32 FNiagaraStackFunctionMergeAdapter::GetStackIndex() const
{
	return StackIndex;
}

int32 FNiagaraStackFunctionMergeAdapter::GetScratchPadScriptIndex() const
{
	return ScratchPadScriptIndex;
}

const TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>>& FNiagaraStackFunctionMergeAdapter::GetInputOverrides() const
{
	return InputOverrides;
}

TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> FNiagaraStackFunctionMergeAdapter::GetInputOverrideByInputName(FString InputName) const
{
	for (TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : InputOverrides)
	{
		if (InputOverride->GetInputName() == InputName)
		{
			return InputOverride;
		}
	}
	return TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter>();
}

FNiagaraScriptStackMergeAdapter::FNiagaraScriptStackMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraNodeOutput& InOutputNode, UNiagaraScript& InScript)
{
	OutputNode = &InOutputNode;
	InputNode.Reset();
	Script = &InScript;
	UniqueEmitterName = InOwningEmitter.GetUniqueEmitterName();

	TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackGroups;
	FNiagaraStackGraphUtilities::GetStackNodeGroups(*OutputNode, StackGroups);

	if (StackGroups.Num() >= 2 && StackGroups[0].EndNode->IsA<UNiagaraNodeInput>())
	{
		InputNode = Cast<UNiagaraNodeInput>(StackGroups[0].EndNode);
	}

	if (StackGroups.Num() > 2 && StackGroups[0].EndNode->IsA<UNiagaraNodeInput>() && StackGroups.Last().EndNode->IsA<UNiagaraNodeOutput>())
	{
		for (int i = 1; i < StackGroups.Num() - 1; i++)
		{
			UNiagaraNodeFunctionCall* ModuleFunctionCallNode = Cast<UNiagaraNodeFunctionCall>(StackGroups[i].EndNode);
			if (ModuleFunctionCallNode != nullptr)
			{
				// The first stack node group is the input node, so we subtract one to get the index of the module.
				int32 StackIndex = i - 1;
				ModuleFunctions.Add(MakeShared<FNiagaraStackFunctionMergeAdapter>(InOwningEmitter, *Script.Get(), *ModuleFunctionCallNode, StackIndex));
			}
		}
	}
}

UNiagaraNodeInput* FNiagaraScriptStackMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

UNiagaraNodeOutput* FNiagaraScriptStackMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraScript* FNiagaraScriptStackMergeAdapter::GetScript() const
{
	return Script.Get();
}

FString FNiagaraScriptStackMergeAdapter::GetUniqueEmitterName() const
{
	return UniqueEmitterName;
}

const TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& FNiagaraScriptStackMergeAdapter::GetModuleFunctions() const
{
	return ModuleFunctions;
}

TSharedPtr<FNiagaraStackFunctionMergeAdapter> FNiagaraScriptStackMergeAdapter::GetModuleFunctionById(FGuid FunctionCallNodeId) const
{
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> Modulefunction : ModuleFunctions)
	{
		if (Modulefunction->GetFunctionCallNode()->NodeGuid == FunctionCallNodeId)
		{
			return Modulefunction;
		}
	}
	return TSharedPtr<FNiagaraStackFunctionMergeAdapter>();
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InEventScriptProperties, nullptr, InOutputNode);
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InEventScriptProperties, InEventScriptProperties, InOutputNode);
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, nullptr, nullptr, InOutputNode);
}

void FNiagaraEventHandlerMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, FNiagaraEventScriptProperties* InEditableEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Emitter = MakeWeakObjectPtr(const_cast<UNiagaraEmitter*>(&InEmitter));

	EventScriptProperties = InEventScriptProperties;
	EditableEventScriptProperties = InEditableEventScriptProperties;

	OutputNode = InOutputNode;

	if (EventScriptProperties != nullptr && OutputNode != nullptr)
	{
		EventStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode.Get(), *EventScriptProperties->Script);
		InputNode = EventStack->GetInputNode();
	}
}

const UNiagaraEmitter* FNiagaraEventHandlerMergeAdapter::GetEmitter() const
{
	return Emitter.Get();
}

FGuid FNiagaraEventHandlerMergeAdapter::GetUsageId() const
{
	if (EventScriptProperties != nullptr)
	{
		return EventScriptProperties->Script->GetUsageId();
	}
	else
	{
		return OutputNode->GetUsageId();
	}
}

const FNiagaraEventScriptProperties* FNiagaraEventHandlerMergeAdapter::GetEventScriptProperties() const
{
	return EventScriptProperties;
}

FNiagaraEventScriptProperties* FNiagaraEventHandlerMergeAdapter::GetEditableEventScriptProperties() const
{
	return EditableEventScriptProperties;
}

UNiagaraNodeOutput* FNiagaraEventHandlerMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraNodeInput* FNiagaraEventHandlerMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEventHandlerMergeAdapter::GetEventStack() const
{
	return EventStack;
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InSimulationStage, nullptr, InOutputNode);
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraSimulationStageBase* InSimulationStage, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InSimulationStage, InSimulationStage, InOutputNode);
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, nullptr, nullptr, InOutputNode);
}

void FNiagaraSimulationStageMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, UNiagaraSimulationStageBase* InEditableSimulationStage, UNiagaraNodeOutput* InOutputNode)
{
	Emitter = MakeWeakObjectPtr(const_cast<UNiagaraEmitter*>(&InEmitter));

	SimulationStage = InSimulationStage;
	EditableSimulationStage = InEditableSimulationStage;

	OutputNode = InOutputNode;

	if (SimulationStage != nullptr && OutputNode != nullptr)
	{
		SimulationStageStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode.Get(), *SimulationStage->Script);
		InputNode = SimulationStageStack->GetInputNode();
	}
}

const UNiagaraEmitter* FNiagaraSimulationStageMergeAdapter::GetEmitter() const
{
	return Emitter.Get();
}

FGuid FNiagaraSimulationStageMergeAdapter::GetUsageId() const
{
	if (SimulationStage != nullptr)
	{
		return SimulationStage->Script->GetUsageId();
	}
	else
	{
		return OutputNode->GetUsageId();
	}
}

const UNiagaraSimulationStageBase* FNiagaraSimulationStageMergeAdapter::GetSimulationStage() const
{
	return SimulationStage;
}

UNiagaraSimulationStageBase* FNiagaraSimulationStageMergeAdapter::GetEditableSimulationStage() const
{
	return EditableSimulationStage;
}

UNiagaraNodeOutput* FNiagaraSimulationStageMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraNodeInput* FNiagaraSimulationStageMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraSimulationStageMergeAdapter::GetSimulationStageStack() const
{
	return SimulationStageStack;
}

FNiagaraRendererMergeAdapter::FNiagaraRendererMergeAdapter(UNiagaraRendererProperties& InRenderer)
{
	Renderer = &InRenderer;
}

UNiagaraRendererProperties* FNiagaraRendererMergeAdapter::GetRenderer()
{
	return Renderer.Get();
}

FNiagaraEmitterMergeAdapter::FNiagaraEmitterMergeAdapter(const UNiagaraEmitter& InEmitter)
{
	Initialize(InEmitter, nullptr);
}

FNiagaraEmitterMergeAdapter::FNiagaraEmitterMergeAdapter(UNiagaraEmitter& InEmitter)
{
	Initialize(InEmitter, &InEmitter);
}

void FNiagaraEmitterMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, UNiagaraEmitter* InEditableEmitter)
{
	Emitter = &InEmitter;
	EditableEmitter = InEditableEmitter;
	UNiagaraScriptSource* EmitterScriptSource = Cast<UNiagaraScriptSource>(Emitter->GraphSource);
	UNiagaraGraph* Graph = EmitterScriptSource->NodeGraph;
	TArray<UNiagaraNodeOutput*> OutputNodes;
	Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);

	TArray<UNiagaraNodeOutput*> EventOutputNodes;
	TArray<UNiagaraNodeOutput*> SimulationStageOutputNodes;
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterSpawnScript))
		{
			EmitterSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->EmitterSpawnScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterUpdateScript))
		{
			EmitterUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->EmitterUpdateScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleSpawnScript))
		{
			ParticleSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->SpawnScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
		{
			ParticleUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->UpdateScriptProps.Script);
		}
		else if(UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleEventScript))
		{
			EventOutputNodes.Add(OutputNode);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleSimulationStageScript))
		{
			SimulationStageOutputNodes.Add(OutputNode);
		}
	}

	// Create an event handler adapter for each usage id even if it's missing an event script properties struct or an output node.  These
	// incomplete adapters will be caught if they are diffed.
	for (const FNiagaraEventScriptProperties& EventScriptProperties : Emitter->GetEventHandlers())
	{
		UNiagaraNodeOutput** MatchingOutputNodePtr = EventOutputNodes.FindByPredicate(
			[=](UNiagaraNodeOutput* EventOutputNode) { return EventOutputNode->GetUsageId() == EventScriptProperties.Script->GetUsageId(); });

		UNiagaraNodeOutput* MatchingOutputNode = MatchingOutputNodePtr != nullptr ? *MatchingOutputNodePtr : nullptr;

		if (EditableEmitter == nullptr)
		{
			EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), &EventScriptProperties, MatchingOutputNode));
		}
		else
		{
			FNiagaraEventScriptProperties* EditableEventScriptProperties = EditableEmitter->GetEventHandlerByIdUnsafe(EventScriptProperties.Script->GetUsageId());
			EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), EditableEventScriptProperties, MatchingOutputNode));
		}

		if (MatchingOutputNode != nullptr)
		{
			EventOutputNodes.Remove(MatchingOutputNode);
		}
	}

	for (UNiagaraNodeOutput* EventOutputNode : EventOutputNodes)
	{
		EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), EventOutputNode));
	}

	// Create an shader stage adapter for each usage id even if it's missing a shader stage object or an output node.  These
	// incomplete adapters will be caught if they are diffed.
	for (const UNiagaraSimulationStageBase* SimulationStage : Emitter->GetSimulationStages())
	{
		UNiagaraNodeOutput** MatchingOutputNodePtr = SimulationStageOutputNodes.FindByPredicate(
			[=](UNiagaraNodeOutput* SimulationStageOutputNode) { return SimulationStageOutputNode->GetUsageId() == SimulationStage->Script->GetUsageId(); });

		UNiagaraNodeOutput* MatchingOutputNode = MatchingOutputNodePtr != nullptr ? *MatchingOutputNodePtr : nullptr;

		if (EditableEmitter == nullptr)
		{
			SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), SimulationStage, MatchingOutputNode));
		}
		else
		{
			UNiagaraSimulationStageBase* EditableSimulationStage = EditableEmitter->GetSimulationStageById(SimulationStage->Script->GetUsageId());
			SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), EditableSimulationStage, MatchingOutputNode));
		}

		if (MatchingOutputNode != nullptr)
		{
			SimulationStageOutputNodes.Remove(MatchingOutputNode);
		}
	}

	for (UNiagaraNodeOutput* SimulationStageOutputNode : SimulationStageOutputNodes)
	{
		SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), SimulationStageOutputNode));
	}

	// Renderers
	for (UNiagaraRendererProperties* RendererProperties : Emitter->GetRenderers())
	{
		Renderers.Add(MakeShared<FNiagaraRendererMergeAdapter>(*RendererProperties));
	}

	EditorData = Cast<const UNiagaraEmitterEditorData>(Emitter->GetEditorData());
}

UNiagaraEmitter* FNiagaraEmitterMergeAdapter::GetEditableEmitter() const
{
	return EditableEmitter.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetEmitterSpawnStack() const
{
	return EmitterSpawnStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetEmitterUpdateStack() const
{
	return EmitterUpdateStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetParticleSpawnStack() const
{
	return ParticleSpawnStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetParticleUpdateStack() const
{
	return ParticleUpdateStack;
}

const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> FNiagaraEmitterMergeAdapter::GetEventHandlers() const
{
	return EventHandlers;
}

const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> FNiagaraEmitterMergeAdapter::GetSimulationStages() const
{
	return SimulationStages;
}

const TArray<TSharedRef<FNiagaraRendererMergeAdapter>> FNiagaraEmitterMergeAdapter::GetRenderers() const
{
	return Renderers;
}

const UNiagaraEmitterEditorData* FNiagaraEmitterMergeAdapter::GetEditorData() const
{
	return EditorData.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetScriptStack(ENiagaraScriptUsage Usage, FGuid ScriptUsageId)
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::EmitterSpawnScript:
		return EmitterSpawnStack;
	case ENiagaraScriptUsage::EmitterUpdateScript:
		return EmitterUpdateStack;
	case ENiagaraScriptUsage::ParticleSpawnScript:
		return ParticleSpawnStack;
	case ENiagaraScriptUsage::ParticleUpdateScript:
		return ParticleUpdateStack;
	case ENiagaraScriptUsage::ParticleEventScript:
		for (TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandler : EventHandlers)
		{
			if (EventHandler->GetUsageId() == ScriptUsageId)
			{
				return EventHandler->GetEventStack();
			}
		}
		break;
	default:
		checkf(false, TEXT("Unsupported usage"));
	}

	return TSharedPtr<FNiagaraScriptStackMergeAdapter>();
}

TSharedPtr<FNiagaraEventHandlerMergeAdapter> FNiagaraEmitterMergeAdapter::GetEventHandler(FGuid EventScriptUsageId)
{
	for (TSharedRef<FNiagaraEventHandlerMergeAdapter> EventHandler : EventHandlers)
	{
		if (EventHandler->GetUsageId() == EventScriptUsageId)
		{
			return EventHandler;
		}
	}
	return TSharedPtr<FNiagaraEventHandlerMergeAdapter>();
}

TSharedPtr<FNiagaraSimulationStageMergeAdapter> FNiagaraEmitterMergeAdapter::GetSimulationStage(FGuid SimulationStageUsageId)
{
	for (TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStage : SimulationStages)
	{
		if (SimulationStage->GetUsageId() == SimulationStageUsageId)
		{
			return SimulationStage;
		}
	}
	return TSharedPtr<FNiagaraSimulationStageMergeAdapter>();
}

TSharedPtr<FNiagaraRendererMergeAdapter> FNiagaraEmitterMergeAdapter::GetRenderer(FGuid RendererMergeId)
{
	for (TSharedRef<FNiagaraRendererMergeAdapter> Renderer : Renderers)
	{
		if (Renderer->GetRenderer()->GetMergeId() == RendererMergeId)
		{
			return Renderer;
		}
	}
	return TSharedPtr<FNiagaraRendererMergeAdapter>();
}

FNiagaraScriptStackDiffResults::FNiagaraScriptStackDiffResults()
	: bIsValid(true)
{
}

bool FNiagaraScriptStackDiffResults::IsEmpty() const
{
	return
		RemovedBaseModules.Num() == 0 &&
		AddedOtherModules.Num() == 0 &&
		MovedBaseModules.Num() == 0 &&
		MovedOtherModules.Num() == 0 &&
		EnabledChangedBaseModules.Num() == 0 &&
		EnabledChangedOtherModules.Num() == 0 &&
		RemovedBaseInputOverrides.Num() == 0 &&
		AddedOtherInputOverrides.Num() == 0 &&
		ModifiedOtherInputOverrides.Num() == 0 &&
		ChangedBaseUsage.IsSet() == false &&
		ChangedOtherUsage.IsSet() == false;
}

bool FNiagaraScriptStackDiffResults::IsValid() const
{
	return bIsValid;
}

void FNiagaraScriptStackDiffResults::AddError(FText ErrorMessage)
{
	ErrorMessages.Add(ErrorMessage);
	bIsValid = false;
}

const TArray<FText>& FNiagaraScriptStackDiffResults::GetErrorMessages() const
{
	return ErrorMessages;
}

FNiagaraEmitterDiffResults::FNiagaraEmitterDiffResults()
	: bIsValid(true)
{
}

bool FNiagaraEmitterDiffResults::IsValid() const
{
	bool bEventHandlerDiffsAreValid = true;
	for (const FNiagaraModifiedEventHandlerDiffResults& EventHandlerDiffResults : ModifiedEventHandlers)
	{
		if (EventHandlerDiffResults.ScriptDiffResults.IsValid() == false)
		{
			bEventHandlerDiffsAreValid = false;
			break;
		}
	}
	bool bSimulationStageDiffsAreValid = true;
	for (const FNiagaraModifiedSimulationStageDiffResults& SimulationStageDiffResults : ModifiedSimulationStages)
	{
		if (SimulationStageDiffResults.ScriptDiffResults.IsValid() == false)
		{
			bSimulationStageDiffsAreValid = false;
			break;
		}
	}
	return bIsValid &&
		bEventHandlerDiffsAreValid &&
		bSimulationStageDiffsAreValid &&
		EmitterSpawnDiffResults.IsValid() &&
		EmitterUpdateDiffResults.IsValid() &&
		ParticleSpawnDiffResults.IsValid() &&
		ParticleUpdateDiffResults.IsValid();
}

bool FNiagaraEmitterDiffResults::IsEmpty() const
{
	return DifferentEmitterProperties.Num() == 0 &&
		EmitterSpawnDiffResults.IsEmpty() &&
		EmitterUpdateDiffResults.IsEmpty() &&
		ParticleSpawnDiffResults.IsEmpty() &&
		ParticleUpdateDiffResults.IsEmpty() &&
		RemovedBaseEventHandlers.Num() == 0 &&
		AddedOtherEventHandlers.Num() == 0 &&
		ModifiedEventHandlers.Num() == 0 &&
		RemovedBaseSimulationStages.Num() == 0 &&
		AddedOtherSimulationStages.Num() == 0 &&
		ModifiedSimulationStages.Num() == 0 &&
		RemovedBaseRenderers.Num() == 0 &&
		AddedOtherRenderers.Num() == 0 &&
		ModifiedBaseRenderers.Num() == 0 &&
		ModifiedOtherRenderers.Num() == 0 &&
		ModifiedStackEntryDisplayNames.Num() == 0;
}

void FNiagaraEmitterDiffResults::AddError(FText ErrorMessage)
{
	ErrorMessages.Add(ErrorMessage);
	bIsValid = false;
}

const TArray<FText>& FNiagaraEmitterDiffResults::GetErrorMessages() const
{
	return ErrorMessages;
}

FString FNiagaraEmitterDiffResults::GetErrorMessagesString() const
{
	TArray<FString> ErrorMessageStrings;
	for (FText ErrorMessage : ErrorMessages)
	{
		ErrorMessageStrings.Add(ErrorMessage.ToString());
	}
	return FString::Join(ErrorMessageStrings, TEXT("\n"));
}

TSharedRef<FNiagaraScriptMergeManager> FNiagaraScriptMergeManager::Get()
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	return NiagaraEditorModule.GetScriptMergeManager();
}

void FNiagaraScriptMergeManager::DiffChangeIds(const TMap<FGuid, FGuid>& InSourceChangeIds, const TMap<FGuid, FGuid>& InLastMergedChangeIds, const TMap<FGuid, FGuid>& InInstanceChangeIds, TMap<FGuid, FGuid>& OutChangeIdsToKeepOnInstance) const
{
	auto It = InInstanceChangeIds.CreateConstIterator();
	while (It)
	{
		const FGuid* MatchingSourceChangeId = InSourceChangeIds.Find(It.Key());
		const FGuid* MatchingLastMergedChangeId = InLastMergedChangeIds.Find(It.Key());

		// If we don't have source from the original or from the last merged version of the original, then the instance originated the node and needs to keep it.
		if (MatchingSourceChangeId == nullptr && MatchingLastMergedChangeId == nullptr)
		{
			OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
		}
		else if (MatchingSourceChangeId != nullptr && MatchingLastMergedChangeId != nullptr)
		{
			if (*MatchingSourceChangeId == *MatchingLastMergedChangeId)
			{
				// If both had a copy of the node and both agree on the change id, then we should keep the change id of the instance as it will be the most accurate.
				OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
			}
			else
			{
				// If both had a copy of the node and they're different than the source has changed and it's change id should be used since it's the newer.
				OutChangeIdsToKeepOnInstance.Add(It.Key(), *MatchingSourceChangeId);
			}
		}
		else if (MatchingLastMergedChangeId != nullptr)
		{
			// If only the previous version had the matching key, then we may possibly keep this node around as a local node, in which case, we should apply the override. 
			OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
		}
		else if (MatchingSourceChangeId != nullptr)
		{
			// I'm not sure that there's a way for us to reach this situation, where the node exists on the source and the instance, but not the 
			// last merged version.
			check(false);
		}

		++It;
	}
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ResolveChangeIds(TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter, UNiagaraEmitter& OriginalEmitterInstance, const TMap<FGuid, FGuid>& ChangeIdsThatNeedToBeReset) const
{
	FApplyDiffResults DiffResults;

	if (ChangeIdsThatNeedToBeReset.Num() != 0)
	{
		auto It = ChangeIdsThatNeedToBeReset.CreateConstIterator();
		UNiagaraEmitter* Emitter = MergedInstanceAdapter->GetEditableEmitter();

		TArray<UNiagaraGraph*> Graphs;
		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts);

		TArray<UNiagaraScript*> OriginalScripts;
		OriginalEmitterInstance.GetScripts(OriginalScripts);

		// First gather all the graphs used by this emitter..
		for (UNiagaraScript* Script : Scripts)
		{
			if (Script != nullptr && Script->GetSource() != nullptr)
			{
				UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource());
				if (ScriptSource != nullptr)
				{
					Graphs.AddUnique(ScriptSource->NodeGraph);
				}
			}
		}

		// Now gather up all the nodes
		TArray<UNiagaraNode*> Nodes;
		for (UNiagaraGraph* Graph : Graphs)
		{
			Graph->GetNodesOfClass(Nodes);
		}

		// Now go through all the nodes and set the persistent change ids if we encounter a node that needs it's change id kept.
		bool bAnySet = false;
		while (It)
		{
			for (UNiagaraNode* Node : Nodes)
			{
				if (Node->NodeGuid == It.Key())
				{
					Node->ForceChangeId(It.Value(), false);
					bAnySet = true;
					break;
				}
			}
			++It;
		}

		if (bAnySet)
		{
			for (UNiagaraGraph* Graph : Graphs)
			{
				Graph->MarkGraphRequiresSynchronization(TEXT("Overwrote change id's within graph."));
			}
		}

		DiffResults.bModifiedGraph = bAnySet;

		if (bAnySet)
		{
			bool bAnyUpdated = false;
			TMap<FString, FString> RenameMap;
			RenameMap.Add(TEXT("Emitter"), TEXT("Emitter"));
			for (UNiagaraScript* Script : Scripts)
			{
				for (UNiagaraScript* OriginalScript : OriginalScripts)
				{
					if (Script->Usage == OriginalScript->Usage && Script->GetUsageId() == OriginalScript->GetUsageId())
					{
						bAnyUpdated |= Script->SynchronizeExecutablesWithMaster(OriginalScript, RenameMap);
					}
				}
			}

			if (bAnyUpdated)
			{
				//Emitter->OnPostCompile();
			}
		}
	}

	DiffResults.bSucceeded = true;
	return DiffResults;
}

INiagaraMergeManager::FMergeEmitterResults FNiagaraScriptMergeManager::MergeEmitter(UNiagaraEmitter& Parent, UNiagaraEmitter* ParentAtLastMerge, UNiagaraEmitter& Instance) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter);
	INiagaraMergeManager::FMergeEmitterResults MergeResults;
	const bool bNoParentAtLastMerge = (ParentAtLastMerge == nullptr);
	UNiagaraEmitter* FirstEmitterToDiffAgainst = bNoParentAtLastMerge ? &Parent : ParentAtLastMerge;
	FNiagaraEmitterDiffResults DiffResults = DiffEmitters(*FirstEmitterToDiffAgainst, Instance);
	
	if (DiffResults.IsValid() == false)
	{
		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToDiff;
		MergeResults.ErrorMessages = DiffResults.GetErrorMessages();

		auto ReportScriptStackDiffErrors = [](FMergeEmitterResults& EmitterMergeResults, const FNiagaraScriptStackDiffResults& ScriptStackDiffResults, FText ScriptName)
		{
			FText ScriptStackDiffInvalidFormat = LOCTEXT("ScriptStackDiffInvalidFormat", "Failed to diff {0} script stack.  {1} Errors:");
			if (ScriptStackDiffResults.IsValid() == false)
			{
				EmitterMergeResults.ErrorMessages.Add(FText::Format(ScriptStackDiffInvalidFormat, ScriptName, ScriptStackDiffResults.GetErrorMessages().Num()));
				for (const FText& ErrorMessage : ScriptStackDiffResults.GetErrorMessages())
				{
					EmitterMergeResults.ErrorMessages.Add(ErrorMessage);
				}
			}
		};

		ReportScriptStackDiffErrors(MergeResults, DiffResults.EmitterSpawnDiffResults, LOCTEXT("EmitterSpawnScriptName", "Emitter Spawn"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.EmitterUpdateDiffResults, LOCTEXT("EmitterUpdateScriptName", "Emitter Update"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.ParticleSpawnDiffResults, LOCTEXT("ParticleSpawnScriptName", "Particle Spawn"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.ParticleUpdateDiffResults, LOCTEXT("ParticleUpdateScriptName", "Particle Update"));

		for (const FNiagaraModifiedEventHandlerDiffResults& EventHandlerDiffResults : DiffResults.ModifiedEventHandlers)
		{
			FText EventHandlerName = FText::Format(LOCTEXT("EventHandlerScriptNameFormat", "Event Handler - {0}"), FText::FromName(EventHandlerDiffResults.BaseAdapter->GetEventScriptProperties()->SourceEventName));
			ReportScriptStackDiffErrors(MergeResults, EventHandlerDiffResults.ScriptDiffResults, EventHandlerName);
		}
	}
	else if (DiffResults.IsEmpty())
	{
		// If there were no changes made on the instance, check if the instance matches the parent.
		FNiagaraEmitterDiffResults DiffResultsFromParent = DiffEmitters(Parent, Instance);
		if (DiffResultsFromParent.IsValid() && DiffResultsFromParent.IsEmpty())
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededNoDifferences;
		}
		else
		{
			// If there were differences from the parent or the parent diff failed we can just return a copy of the parent as the merged instance since there
			// were no changes in the instance which need to be applied.
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied;
			MergeResults.MergedInstance = Parent.DuplicateWithoutMerging((UObject*)GetTransientPackage());
		}
	}
	else
	{
		UNiagaraEmitter* MergedInstance = Parent.DuplicateWithoutMerging((UObject*)GetTransientPackage());
		TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(*MergedInstance);

		TMap<FGuid, FGuid> SourceChangeIds;
		TMap<FGuid, FGuid> PreviousSourceChangeIds;
		TMap<FGuid, FGuid> LastChangeIds;
		TMap<FGuid, FGuid> ChangeIdsThatNeedToBeReset;
		FNiagaraEditorUtilities::GatherChangeIds(Parent, SourceChangeIds, TEXT("Source"));
		FNiagaraEditorUtilities::GatherChangeIds(*FirstEmitterToDiffAgainst, PreviousSourceChangeIds, TEXT("MergeLast"));
		FNiagaraEditorUtilities::GatherChangeIds(Instance, LastChangeIds, TEXT("Instance"));
		DiffChangeIds(SourceChangeIds, PreviousSourceChangeIds, LastChangeIds, ChangeIdsThatNeedToBeReset);

		MergedInstance->ParentScratchPadScripts.Append(MergedInstance->ScratchPadScripts);
		MergedInstance->ScratchPadScripts.Empty();
		TMap<UNiagaraScript*, UNiagaraScript*> SourceToMergedScratchPadScriptMap;
		CopyInstanceScratchPadScripts(*MergedInstance, Instance, SourceToMergedScratchPadScriptMap);

		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied;
		FApplyDiffResults EmitterSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetEmitterSpawnStack().ToSharedRef(), SourceToMergedScratchPadScriptMap, DiffResults.EmitterSpawnDiffResults, bNoParentAtLastMerge);
		if (EmitterSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterSpawnResults.ErrorMessages);

		FApplyDiffResults EmitterUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetEmitterUpdateStack().ToSharedRef(), SourceToMergedScratchPadScriptMap, DiffResults.EmitterUpdateDiffResults, bNoParentAtLastMerge);
		if (EmitterUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterUpdateResults.ErrorMessages);

		FApplyDiffResults ParticleSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetParticleSpawnStack().ToSharedRef(), SourceToMergedScratchPadScriptMap, DiffResults.ParticleSpawnDiffResults, bNoParentAtLastMerge);
		if (ParticleSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleSpawnResults.ErrorMessages);

		FApplyDiffResults ParticleUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetParticleUpdateStack().ToSharedRef(), SourceToMergedScratchPadScriptMap, DiffResults.ParticleUpdateDiffResults, bNoParentAtLastMerge);
		if (ParticleUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleUpdateResults.ErrorMessages);

		FApplyDiffResults EventHandlerResults = ApplyEventHandlerDiff(MergedInstanceAdapter, SourceToMergedScratchPadScriptMap, DiffResults, bNoParentAtLastMerge);
		if (EventHandlerResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EventHandlerResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EventHandlerResults.ErrorMessages);

		FApplyDiffResults SimulationStageResults = ApplySimulationStageDiff(MergedInstanceAdapter, SourceToMergedScratchPadScriptMap, DiffResults, bNoParentAtLastMerge);
		if (SimulationStageResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= SimulationStageResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(SimulationStageResults.ErrorMessages);

		FApplyDiffResults RendererResults = ApplyRendererDiff(*MergedInstance, DiffResults, bNoParentAtLastMerge);
		if (RendererResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= RendererResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(RendererResults.ErrorMessages);

		CopyPropertiesToBase(MergedInstance, &Instance, DiffResults.DifferentEmitterProperties);

		FApplyDiffResults StackEntryDisplayNameDiffs = ApplyStackEntryDisplayNameDiffs(*MergedInstance, DiffResults);
		if (StackEntryDisplayNameDiffs.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= StackEntryDisplayNameDiffs.bModifiedGraph;
		MergeResults.ErrorMessages.Append(StackEntryDisplayNameDiffs.ErrorMessages);

#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("A"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif
		
		FApplyDiffResults ChangeIdResults = ResolveChangeIds(MergedInstanceAdapter, Instance, ChangeIdsThatNeedToBeReset);
		if (ChangeIdResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ChangeIdResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ChangeIdResults.ErrorMessages);
		
#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("B"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif

		FNiagaraStackGraphUtilities::CleanUpStaleRapidIterationParameters(*MergedInstance);

#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("C"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif
		if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied)
		{
			UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(MergedInstance->GraphSource);
			FNiagaraStackGraphUtilities::RelayoutGraph(*ScriptSource->NodeGraph);
			MergeResults.MergedInstance = MergedInstance;
		}

		TMap<FGuid, FGuid> FinalChangeIds;
		FNiagaraEditorUtilities::GatherChangeIds(*MergedInstance, FinalChangeIds, TEXT("Final"));
	}

	return MergeResults;
}

bool FNiagaraScriptMergeManager::IsMergeableScriptUsage(ENiagaraScriptUsage ScriptUsage) const
{
	return ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript ||
		ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleSpawnScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleUpdateScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleEventScript;
}

bool FNiagaraScriptMergeManager::HasBaseModule(const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	TSharedPtr<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter = BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId);
	return BaseScriptStackAdapter.IsValid() && BaseScriptStackAdapter->GetModuleFunctionById(ModuleId).IsValid();
}

bool FNiagaraScriptMergeManager::IsModuleInputDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_IsModuleInputDifferentFromBase);

	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedRef<FNiagaraScriptStackMergeAdapter> ScriptStackAdapter = EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef();
	TSharedPtr<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter = BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId);

	if (BaseScriptStackAdapter.IsValid() == false)
	{
		return false;
	}

	FNiagaraScriptStackDiffResults ScriptStackDiffResults;
	DiffScriptStacks(BaseScriptStackAdapter.ToSharedRef(), ScriptStackAdapter, ScriptStackDiffResults);

	if (ScriptStackDiffResults.IsValid() == false)
	{
		return true;
	}

	if (ScriptStackDiffResults.IsEmpty())
	{
		return false;
	}

	auto FindInputOverrideByInputName = [=](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride)
	{
		return InputOverride->GetOwningFunctionCall()->NodeGuid == ModuleId && InputOverride->GetInputName() == InputName;
	};

	return
		ScriptStackDiffResults.RemovedBaseInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr ||
		ScriptStackDiffResults.AddedOtherInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr ||
		ScriptStackDiffResults.ModifiedOtherInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ResetModuleInputToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	// Diff from the emitter to the base to create a diff which will reset the emitter back to the base.
	FNiagaraScriptStackDiffResults ResetDiffResults;
	DiffScriptStacks(EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), ResetDiffResults);

	if (ResetDiffResults.IsValid() == false)
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfDiffMessage", "Failed to reset input back to it's base value.  It couldn't be diffed successfully.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
		return Results;
	}

	if (ResetDiffResults.IsEmpty())
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfEmptyDiffMessage", "Failed to reset input back to it's base value.  It wasn't different from the base.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
		return Results;
	}

	if (Emitter.ParentScratchPadScripts.Num() != BaseEmitter.ParentScratchPadScripts.Num() + BaseEmitter.ScratchPadScripts.Num())
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfScratchPadScripts", "Failed to reset input back to it's base value.  Its scratch pad scripts were out of sync.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
	}
	
	// Remove items from the diff which are not relevant to this input.
	ResetDiffResults.RemovedBaseModules.Empty();
	ResetDiffResults.AddedOtherModules.Empty();

	auto FindUnrelatedInputOverrides = [=](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride)
	{
		return InputOverride->GetOwningFunctionCall()->NodeGuid != ModuleId || InputOverride->GetInputName() != InputName;
	};

	ResetDiffResults.RemovedBaseInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.AddedOtherInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.ModifiedBaseInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.ModifiedOtherInputOverrides.RemoveAll(FindUnrelatedInputOverrides);

	TMap<UNiagaraScript*, UNiagaraScript*> ScratchScriptMap;
	for (int32 BaseParentScratchPadScriptIndex = 0; BaseParentScratchPadScriptIndex < BaseEmitter.ParentScratchPadScripts.Num(); BaseParentScratchPadScriptIndex++)
	{
		ScratchScriptMap.Add(BaseEmitter.ParentScratchPadScripts[BaseParentScratchPadScriptIndex], Emitter.ParentScratchPadScripts[BaseParentScratchPadScriptIndex]);
	}

	int32 BaseParentScratchPadScriptCount = BaseEmitter.ParentScratchPadScripts.Num();
	for (int32 BaseScratchPadScriptIndex = 0; BaseScratchPadScriptIndex < BaseEmitter.ScratchPadScripts.Num(); BaseScratchPadScriptIndex++)
	{
		ScratchScriptMap.Add(BaseEmitter.ScratchPadScripts[BaseScratchPadScriptIndex], Emitter.ParentScratchPadScripts[BaseParentScratchPadScriptCount + BaseScratchPadScriptIndex]);
	}

	return ApplyScriptStackDiff(EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), ScratchScriptMap, ResetDiffResults, false);
}

bool FNiagaraScriptMergeManager::HasBaseEventHandler(const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetEventHandler(EventScriptUsageId).IsValid();
}

bool FNiagaraScriptMergeManager::IsEventHandlerPropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandlerAdapter = EmitterAdapter->GetEventHandler(EventScriptUsageId);
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> BaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(EventScriptUsageId);

	if (EventHandlerAdapter->GetEditableEventScriptProperties() == nullptr || BaseEventHandlerAdapter->GetEventScriptProperties() == nullptr)
	{
		return true;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseEventHandlerAdapter->GetEventScriptProperties(), EventHandlerAdapter->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetEventHandlerPropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandlerAdapter = EmitterAdapter->GetEventHandler(EventScriptUsageId);
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> BaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(EventScriptUsageId);

	if (EventHandlerAdapter->GetEditableEventScriptProperties() == nullptr || BaseEventHandlerAdapter->GetEventScriptProperties() == nullptr)
	{
		// TODO: Display an error to the user.
		return;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseEventHandlerAdapter->GetEventScriptProperties(), EventHandlerAdapter->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);
	CopyPropertiesToBase(EventHandlerAdapter->GetEditableEventScriptProperties(), BaseEventHandlerAdapter->GetEventScriptProperties(), DifferentProperties);
	Emitter.PostEditChange();
}

bool FNiagaraScriptMergeManager::HasBaseSimulationStage(const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId).IsValid();
}

bool FNiagaraScriptMergeManager::IsSimulationStagePropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraSimulationStageMergeAdapter> SimulationStageAdapter = EmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> BaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);

	if (SimulationStageAdapter->GetEditableSimulationStage() == nullptr || BaseSimulationStageAdapter->GetSimulationStage() == nullptr)
	{
		return true;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseSimulationStageAdapter->GetSimulationStage(), SimulationStageAdapter->GetSimulationStage(), *BaseSimulationStageAdapter->GetSimulationStage()->GetClass(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetSimulationStagePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraSimulationStageMergeAdapter> SimulationStageAdapter = EmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> BaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);

	if (SimulationStageAdapter->GetEditableSimulationStage() == nullptr || BaseSimulationStageAdapter->GetSimulationStage() == nullptr)
	{
		// TODO: Display an error to the user.
		return;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseSimulationStageAdapter->GetSimulationStage(), SimulationStageAdapter->GetSimulationStage(), *BaseSimulationStageAdapter->GetSimulationStage()->GetClass(), DifferentProperties);
	CopyPropertiesToBase(SimulationStageAdapter->GetEditableSimulationStage(), BaseSimulationStageAdapter->GetSimulationStage(), DifferentProperties);
	Emitter.PostEditChange();
}

bool FNiagaraScriptMergeManager::HasBaseRenderer(const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetRenderer(RendererMergeId).IsValid();
}

bool FNiagaraScriptMergeManager::IsRendererDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	FNiagaraEmitterDiffResults DiffResults;
	DiffRenderers(BaseEmitterAdapter->GetRenderers(), EmitterAdapter->GetRenderers(), DiffResults);

	if (DiffResults.IsValid() == false)
	{
		return true;
	}

	if (DiffResults.ModifiedOtherRenderers.Num() == 0)
	{
		return false;
	}

	auto FindRendererByMergeId = [=](TSharedRef<FNiagaraRendererMergeAdapter> Renderer) { return Renderer->GetRenderer()->GetMergeId() == RendererMergeId; };
	return DiffResults.ModifiedOtherRenderers.FindByPredicate(FindRendererByMergeId) != nullptr;
}

void FNiagaraScriptMergeManager::ResetRendererToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	// Diff from the current emitter to the base emitter to create a diff which will reset the emitter back to the base.
	FNiagaraEmitterDiffResults ResetDiffResults;
	DiffRenderers(EmitterAdapter->GetRenderers(), BaseEmitterAdapter->GetRenderers(), ResetDiffResults);

	auto FindUnrelatedRenderers = [=](TSharedRef<FNiagaraRendererMergeAdapter> Renderer)
	{
		return Renderer->GetRenderer()->GetMergeId() != RendererMergeId;
	};

	// Removed added and removed renderers, as well as changes to renderers with different ids from the one being reset.
	ResetDiffResults.RemovedBaseRenderers.Empty();
	ResetDiffResults.AddedOtherRenderers.Empty();
	ResetDiffResults.ModifiedBaseRenderers.RemoveAll(FindUnrelatedRenderers);
	ResetDiffResults.ModifiedOtherRenderers.RemoveAll(FindUnrelatedRenderers);

	ApplyRendererDiff(Emitter, ResetDiffResults, false);
}

bool FNiagaraScriptMergeManager::IsEmitterEditablePropertySetDifferentFromBase(const UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter)
{
	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(&BaseEmitter, &Emitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetEmitterEditablePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter)
{
	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(&BaseEmitter, &Emitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	CopyPropertiesToBase(&Emitter, &BaseEmitter, DifferentProperties);
	Emitter.PostEditChange();
}

FNiagaraEmitterDiffResults FNiagaraScriptMergeManager::DiffEmitters(UNiagaraEmitter& BaseEmitter, UNiagaraEmitter& OtherEmitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_DiffEmitters);

	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(BaseEmitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> OtherEmitterAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(OtherEmitter);

	FNiagaraEmitterDiffResults EmitterDiffResults;
	if (BaseEmitterAdapter->GetEmitterSpawnStack().IsValid() && OtherEmitterAdapter->GetEmitterSpawnStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetEmitterSpawnStack().ToSharedRef(), OtherEmitterAdapter->GetEmitterSpawnStack().ToSharedRef(), EmitterDiffResults.EmitterSpawnDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("EmitterSpawnStacksInvalidMessage", "One of the emitter spawn script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetEmitterUpdateStack().IsValid() && OtherEmitterAdapter->GetEmitterUpdateStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetEmitterUpdateStack().ToSharedRef(), OtherEmitterAdapter->GetEmitterUpdateStack().ToSharedRef(), EmitterDiffResults.EmitterUpdateDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("EmitterUpdateStacksInvalidMessage", "One of the emitter update script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetParticleSpawnStack().IsValid() && OtherEmitterAdapter->GetParticleSpawnStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetParticleSpawnStack().ToSharedRef(), OtherEmitterAdapter->GetParticleSpawnStack().ToSharedRef(), EmitterDiffResults.ParticleSpawnDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("ParticleSpawnStacksInvalidMessage", "One of the particle spawn script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetParticleUpdateStack().IsValid() && OtherEmitterAdapter->GetParticleUpdateStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetParticleUpdateStack().ToSharedRef(), OtherEmitterAdapter->GetParticleUpdateStack().ToSharedRef(), EmitterDiffResults.ParticleUpdateDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("ParticleUpdateStacksInvalidMessage", "One of the particle update script stacks was invalid."));
	}

	DiffEventHandlers(BaseEmitterAdapter->GetEventHandlers(), OtherEmitterAdapter->GetEventHandlers(), EmitterDiffResults);
	DiffSimulationStages(BaseEmitterAdapter->GetSimulationStages(), OtherEmitterAdapter->GetSimulationStages(), EmitterDiffResults);
	DiffRenderers(BaseEmitterAdapter->GetRenderers(), OtherEmitterAdapter->GetRenderers(), EmitterDiffResults);
	DiffEditableProperties(&BaseEmitter, &OtherEmitter, *UNiagaraEmitter::StaticClass(), EmitterDiffResults.DifferentEmitterProperties);
	DiffStackEntryDisplayNames(BaseEmitterAdapter->GetEditorData(), OtherEmitterAdapter->GetEditorData(), EmitterDiffResults.ModifiedStackEntryDisplayNames);

	return EmitterDiffResults;
}

template<typename ValueType>
struct FCommonValuePair
{
	FCommonValuePair(ValueType InBaseValue, ValueType InOtherValue)
		: BaseValue(InBaseValue)
		, OtherValue(InOtherValue)
	{
	}

	ValueType BaseValue;
	ValueType OtherValue;
};

template<typename ValueType>
struct FListDiffResults
{
	TArray<ValueType> RemovedBaseValues;
	TArray<ValueType> AddedOtherValues;
	TArray<FCommonValuePair<ValueType>> CommonValuePairs;
};

template<typename ValueType, typename KeyType, typename KeyFromValueDelegate>
FListDiffResults<ValueType> DiffLists(const TArray<ValueType>& BaseList, const TArray<ValueType> OtherList, KeyFromValueDelegate KeyFromValue)
{
	FListDiffResults<ValueType> DiffResults;

	TMap<KeyType, ValueType> BaseKeyToValueMap;
	TSet<KeyType> BaseKeys;
	for (ValueType BaseValue : BaseList)
	{
		KeyType BaseKey = KeyFromValue(BaseValue);
		BaseKeyToValueMap.Add(BaseKey, BaseValue);
		BaseKeys.Add(BaseKey);
	}

	TMap<KeyType, ValueType> OtherKeyToValueMap;
	TSet<KeyType> OtherKeys;
	for (ValueType OtherValue : OtherList)
	{
		KeyType OtherKey = KeyFromValue(OtherValue);
		OtherKeyToValueMap.Add(OtherKey, OtherValue);
		OtherKeys.Add(OtherKey);
	}

	for (KeyType RemovedKey : BaseKeys.Difference(OtherKeys))
	{
		DiffResults.RemovedBaseValues.Add(BaseKeyToValueMap[RemovedKey]);
	}

	for (KeyType AddedKey : OtherKeys.Difference(BaseKeys))
	{
		DiffResults.AddedOtherValues.Add(OtherKeyToValueMap[AddedKey]);
	}

	for (KeyType CommonKey : BaseKeys.Intersect(OtherKeys))
	{
		DiffResults.CommonValuePairs.Add(FCommonValuePair<ValueType>(BaseKeyToValueMap[CommonKey], OtherKeyToValueMap[CommonKey]));
	}

	return DiffResults;
}

void FNiagaraScriptMergeManager::DiffEventHandlers(const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& BaseEventHandlers, const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& OtherEventHandlers, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraEventHandlerMergeAdapter>> EventHandlerListDiffResults = DiffLists<TSharedRef<FNiagaraEventHandlerMergeAdapter>, FGuid>(
		BaseEventHandlers,
		OtherEventHandlers,
		[](TSharedRef<FNiagaraEventHandlerMergeAdapter> EventHandler) { return EventHandler->GetUsageId(); });

	DiffResults.RemovedBaseEventHandlers.Append(EventHandlerListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherEventHandlers.Append(EventHandlerListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& CommonValuePair : EventHandlerListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetEventScriptProperties() == nullptr || CommonValuePair.BaseValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidBaseEventHandlerDiffFailedFormat", "Failed to diff event handlers, the base event handler was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.BaseValue->GetUsageId().ToString())));
		}
		else if(CommonValuePair.OtherValue->GetEventScriptProperties() == nullptr || CommonValuePair.OtherValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidOtherEventHandlerDiffFailedFormat", "Failed to diff event handlers, the other event handler was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.OtherValue->GetUsageId().ToString())));
		}
		else
		{
			TArray<FProperty*> DifferentProperties;
			DiffEditableProperties(CommonValuePair.BaseValue->GetEventScriptProperties(), CommonValuePair.OtherValue->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);

			FNiagaraScriptStackDiffResults EventHandlerScriptStackDiffResults;
			DiffScriptStacks(CommonValuePair.BaseValue->GetEventStack().ToSharedRef(), CommonValuePair.OtherValue->GetEventStack().ToSharedRef(), EventHandlerScriptStackDiffResults);

			if (DifferentProperties.Num() > 0 || EventHandlerScriptStackDiffResults.IsValid() == false || EventHandlerScriptStackDiffResults.IsEmpty() == false)
			{
				FNiagaraModifiedEventHandlerDiffResults ModifiedEventHandlerResults;
				ModifiedEventHandlerResults.BaseAdapter = CommonValuePair.BaseValue;
				ModifiedEventHandlerResults.OtherAdapter = CommonValuePair.OtherValue;
				ModifiedEventHandlerResults.ChangedProperties.Append(DifferentProperties);
				ModifiedEventHandlerResults.ScriptDiffResults = EventHandlerScriptStackDiffResults;
				DiffResults.ModifiedEventHandlers.Add(ModifiedEventHandlerResults);
			}

			if (EventHandlerScriptStackDiffResults.IsValid() == false)
			{
				for (const FText& EventHandlerScriptStackDiffErrorMessage : EventHandlerScriptStackDiffResults.GetErrorMessages())
				{
					DiffResults.AddError(EventHandlerScriptStackDiffErrorMessage);
				}
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffSimulationStages(const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& BaseSimulationStages, const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& OtherSimulationStages, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraSimulationStageMergeAdapter>> SimulationStageListDiffResults = DiffLists<TSharedRef<FNiagaraSimulationStageMergeAdapter>, FGuid>(
		BaseSimulationStages,
		OtherSimulationStages,
		[](TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStage) { return SimulationStage->GetUsageId(); });

	DiffResults.RemovedBaseSimulationStages.Append(SimulationStageListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherSimulationStages.Append(SimulationStageListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& CommonValuePair : SimulationStageListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetSimulationStage() == nullptr || CommonValuePair.BaseValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidBaseSimulationStageDiffFailedFormat", "Failed to diff shader stages, the base shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.BaseValue->GetUsageId().ToString())));
		}
		else if (CommonValuePair.OtherValue->GetSimulationStage() == nullptr || CommonValuePair.OtherValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidOtherSimulationStageDiffFailedFormat", "Failed to diff shader stage, the other shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.OtherValue->GetUsageId().ToString())));
		}
		else
		{
			TArray<FProperty*> DifferentProperties;
			DiffEditableProperties(CommonValuePair.BaseValue->GetSimulationStage(), CommonValuePair.OtherValue->GetSimulationStage(), *CommonValuePair.BaseValue->GetSimulationStage()->GetClass(), DifferentProperties);

			FNiagaraScriptStackDiffResults SimulationStageScriptStackDiffResults;
			DiffScriptStacks(CommonValuePair.BaseValue->GetSimulationStageStack().ToSharedRef(), CommonValuePair.OtherValue->GetSimulationStageStack().ToSharedRef(), SimulationStageScriptStackDiffResults);

			if (DifferentProperties.Num() > 0 || SimulationStageScriptStackDiffResults.IsValid() == false || SimulationStageScriptStackDiffResults.IsEmpty() == false)
			{
				FNiagaraModifiedSimulationStageDiffResults ModifiedSimulationStageResults;
				ModifiedSimulationStageResults.BaseAdapter = CommonValuePair.BaseValue;
				ModifiedSimulationStageResults.OtherAdapter = CommonValuePair.OtherValue;
				ModifiedSimulationStageResults.ChangedProperties.Append(DifferentProperties);
				ModifiedSimulationStageResults.ScriptDiffResults = SimulationStageScriptStackDiffResults;
				DiffResults.ModifiedSimulationStages.Add(ModifiedSimulationStageResults);
			}

			if (SimulationStageScriptStackDiffResults.IsValid() == false)
			{
				for (const FText& SimulationStageScriptStackDiffErrorMessage : SimulationStageScriptStackDiffResults.GetErrorMessages())
				{
					DiffResults.AddError(SimulationStageScriptStackDiffErrorMessage);
				}
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffRenderers(const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& BaseRenderers, const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& OtherRenderers, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraRendererMergeAdapter>> RendererListDiffResults = DiffLists<TSharedRef<FNiagaraRendererMergeAdapter>, FGuid>(
		BaseRenderers,
		OtherRenderers,
		[](TSharedRef<FNiagaraRendererMergeAdapter> Renderer) { return Renderer->GetRenderer()->GetMergeId(); });

	DiffResults.RemovedBaseRenderers.Append(RendererListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherRenderers.Append(RendererListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraRendererMergeAdapter>>& CommonValuePair : RendererListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetRenderer()->Equals(CommonValuePair.OtherValue->GetRenderer()) == false)
		{
			DiffResults.ModifiedBaseRenderers.Add(CommonValuePair.BaseValue);
			DiffResults.ModifiedOtherRenderers.Add(CommonValuePair.OtherValue);
		}
	}
}

void FNiagaraScriptMergeManager::DiffScriptStacks(TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter, TSharedRef<FNiagaraScriptStackMergeAdapter> OtherScriptStackAdapter, FNiagaraScriptStackDiffResults& DiffResults) const
{
	// Diff the module lists.
	FListDiffResults<TSharedRef<FNiagaraStackFunctionMergeAdapter>> ModuleListDiffResults = DiffLists<TSharedRef<FNiagaraStackFunctionMergeAdapter>, FGuid>(
		BaseScriptStackAdapter->GetModuleFunctions(),
		OtherScriptStackAdapter->GetModuleFunctions(),
		[](TSharedRef<FNiagaraStackFunctionMergeAdapter> FunctionAdapter) { return FunctionAdapter->GetFunctionCallNode()->NodeGuid; });

	// Sort the diff results for easier diff applying and testing.
	auto OrderModuleByStackIndex = [](TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleA, TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleB)
	{
		return ModuleA->GetStackIndex() < ModuleB->GetStackIndex();
	};

	ModuleListDiffResults.RemovedBaseValues.Sort(OrderModuleByStackIndex);
	ModuleListDiffResults.AddedOtherValues.Sort(OrderModuleByStackIndex);

	auto OrderCommonModulePairByBaseStackIndex = [](
		const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuesA,
		const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuesB)
	{
		return CommonValuesA.BaseValue->GetStackIndex() < CommonValuesB.BaseValue->GetStackIndex();
	};

	ModuleListDiffResults.CommonValuePairs.Sort(OrderCommonModulePairByBaseStackIndex);

	// Populate results from the sorted diff.
	DiffResults.RemovedBaseModules.Append(ModuleListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherModules.Append(ModuleListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuePair : ModuleListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetStackIndex() != CommonValuePair.OtherValue->GetStackIndex())
		{
			DiffResults.MovedBaseModules.Add(CommonValuePair.BaseValue);
			DiffResults.MovedOtherModules.Add(CommonValuePair.OtherValue);
		}

		if (CommonValuePair.BaseValue->GetFunctionCallNode()->IsNodeEnabled() != CommonValuePair.OtherValue->GetFunctionCallNode()->IsNodeEnabled())
		{
			DiffResults.EnabledChangedBaseModules.Add(CommonValuePair.BaseValue);
			DiffResults.EnabledChangedOtherModules.Add(CommonValuePair.OtherValue);
		}

		UNiagaraScript* BaseFunctionScript = CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript;
		UNiagaraScript* OtherFunctionScript = CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript;
		bool bFunctionScriptsMatch = BaseFunctionScript == OtherFunctionScript;
		bool bFunctionScriptsAreNotAssets =
			BaseFunctionScript != nullptr && BaseFunctionScript->IsAsset() == false &&
			OtherFunctionScript != nullptr && OtherFunctionScript->IsAsset() == false;
		if (bFunctionScriptsMatch || bFunctionScriptsAreNotAssets)
		{
			DiffFunctionInputs(CommonValuePair.BaseValue, CommonValuePair.OtherValue, DiffResults);
		}
		else
		{
			FText ErrorMessage = FText::Format(LOCTEXT("FunctionScriptMismatchFormat", "Function scripts for function {0} did not match.  Parent: {1} Child: {2}.  This can be fixed by removing the module from the parent, merging the removal to the child, then removing it from the child, and then re-adding it to the parent and merging again."),
				FText::FromString(CommonValuePair.BaseValue->GetFunctionCallNode()->GetFunctionName()), 
				FText::FromString(CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript != nullptr ? CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript->GetPathName() : TEXT("(null)")),
				FText::FromString(CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript != nullptr ? CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript->GetPathName() : TEXT("(null)")));
			DiffResults.AddError(ErrorMessage);
		}
	}

	if (BaseScriptStackAdapter->GetScript()->GetUsage() != OtherScriptStackAdapter->GetScript()->GetUsage())
	{
		DiffResults.ChangedBaseUsage = BaseScriptStackAdapter->GetScript()->GetUsage();
		DiffResults.ChangedOtherUsage = OtherScriptStackAdapter->GetScript()->GetUsage();
	}
}

void FNiagaraScriptMergeManager::DiffFunctionInputs(TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseFunctionAdapter, TSharedRef<FNiagaraStackFunctionMergeAdapter> OtherFunctionAdapter, FNiagaraScriptStackDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> ListDiffResults = DiffLists<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>, FString>(
		BaseFunctionAdapter->GetInputOverrides(),
		OtherFunctionAdapter->GetInputOverrides(),
		[](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverrideAdapter) { return InputOverrideAdapter->GetInputName(); });

	DiffResults.RemovedBaseInputOverrides.Append(ListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherInputOverrides.Append(ListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>>& CommonValuePair : ListDiffResults.CommonValuePairs)
	{
		TOptional<bool> FunctionMatch = DoFunctionInputOverridesMatch(CommonValuePair.BaseValue, CommonValuePair.OtherValue);
		if (FunctionMatch.IsSet())
		{
			if (FunctionMatch.GetValue() == false)
			{
				DiffResults.ModifiedBaseInputOverrides.Add(CommonValuePair.BaseValue);
				DiffResults.ModifiedOtherInputOverrides.Add(CommonValuePair.OtherValue);
			}
		}
		else
		{
			DiffResults.AddError(FText::Format(LOCTEXT("FunctionInputDiffFailedFormat", "Failed to diff function inputs.  Function name: {0}  Input Name: {1}"),
				FText::FromString(BaseFunctionAdapter->GetFunctionCallNode()->GetFunctionName()),
				FText::FromString(CommonValuePair.BaseValue->GetInputName())));
		}
	}
}

void FNiagaraScriptMergeManager::DiffEditableProperties(const void* BaseDataAddress, const void* OtherDataAddress, UStruct& Struct, TArray<FProperty*>& OutDifferentProperties) const
{
	for (TFieldIterator<FProperty> PropertyIterator(&Struct); PropertyIterator; ++PropertyIterator)
	{
		if (PropertyIterator->HasAllPropertyFlags(CPF_Edit) && PropertyIterator->HasMetaData("NiagaraNoMerge") == false)
		{
			if (PropertyIterator->Identical(
				PropertyIterator->ContainerPtrToValuePtr<void>(BaseDataAddress),
				PropertyIterator->ContainerPtrToValuePtr<void>(OtherDataAddress), PPF_DeepComparison) == false)
			{
				OutDifferentProperties.Add(*PropertyIterator);
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffStackEntryDisplayNames(const UNiagaraEmitterEditorData* BaseEditorData, const UNiagaraEmitterEditorData* OtherEditorData, TMap<FString, FText>& OutModifiedStackEntryDisplayNames) const
{
	if (BaseEditorData != nullptr && OtherEditorData != nullptr)
	{
		// find display names that have been added or changed in the instance
		const TMap<FString, FText>& OtherRenames = OtherEditorData->GetStackEditorData().GetAllStackEntryDisplayNames();
		for (auto& Pair : OtherRenames)
		{
			const FText* BaseDisplayName = BaseEditorData->GetStackEditorData().GetStackEntryDisplayName(Pair.Key);
			if (BaseDisplayName == nullptr || !BaseDisplayName->EqualTo(Pair.Value))
			{
				OutModifiedStackEntryDisplayNames.Add(Pair.Key, Pair.Value);
			}
		}
	}
}

TOptional<bool> FNiagaraScriptMergeManager::DoFunctionInputOverridesMatch(TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> BaseFunctionInputAdapter, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OtherFunctionInputAdapter) const
{
	// Local String Value.
	if ((BaseFunctionInputAdapter->GetLocalValueString().IsSet() && OtherFunctionInputAdapter->GetLocalValueString().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLocalValueString().IsSet() == false && OtherFunctionInputAdapter->GetLocalValueString().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLocalValueString().IsSet() && OtherFunctionInputAdapter->GetLocalValueString().IsSet())
	{
		return BaseFunctionInputAdapter->GetLocalValueString().GetValue() == OtherFunctionInputAdapter->GetLocalValueString().GetValue();
	}

	// Local rapid iteration parameter value.
	if ((BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() == false && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet())
	{
		const uint8* BaseRapidIterationParameterValue = BaseFunctionInputAdapter->GetOwningScript()->RapidIterationParameters
			.GetParameterData(BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue());
		const uint8* OtherRapidIterationParameterValue = OtherFunctionInputAdapter->GetOwningScript()->RapidIterationParameters
			.GetParameterData(OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue());
		return FMemory::Memcmp(
			BaseRapidIterationParameterValue,
			OtherRapidIterationParameterValue,
			BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue().GetSizeInBytes()) == 0;
	}

	// Linked value
	if ((BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() == false && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet())
	{
		return BaseFunctionInputAdapter->GetLinkedValueHandle().GetValue() == OtherFunctionInputAdapter->GetLinkedValueHandle().GetValue();
	}

	// Data value
	if ((BaseFunctionInputAdapter->GetDataValueInputName().IsSet() && OtherFunctionInputAdapter->GetDataValueInputName().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetDataValueInputName().IsSet() == false && OtherFunctionInputAdapter->GetDataValueInputName().IsSet()) ||
		(BaseFunctionInputAdapter->GetDataValueObject() != nullptr && OtherFunctionInputAdapter->GetDataValueObject() == nullptr) ||
		(BaseFunctionInputAdapter->GetDataValueObject() == nullptr && OtherFunctionInputAdapter->GetDataValueObject() != nullptr))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDataValueInputName().IsSet() && OtherFunctionInputAdapter->GetDataValueInputName().IsSet() &&
		BaseFunctionInputAdapter->GetDataValueObject() != nullptr && OtherFunctionInputAdapter->GetDataValueObject() != nullptr)
	{
		return 
			BaseFunctionInputAdapter->GetDataValueInputName().GetValue() == OtherFunctionInputAdapter->GetDataValueInputName().GetValue() &&
			BaseFunctionInputAdapter->GetDataValueObject()->Equals(OtherFunctionInputAdapter->GetDataValueObject());
	}

	// Dynamic value
	if ((BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false) ||
		(BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid())
	{
		TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseDynamicValueFunction = BaseFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef();
		TSharedRef<FNiagaraStackFunctionMergeAdapter> OtherDynamicValueFunction = OtherFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef();

		UNiagaraNodeCustomHlsl* BaseCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(BaseDynamicValueFunction->GetFunctionCallNode());
		UNiagaraNodeCustomHlsl* OtherCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(OtherDynamicValueFunction->GetFunctionCallNode());
		if (BaseCustomHlsl != nullptr || OtherCustomHlsl != nullptr)
		{
			if ((BaseCustomHlsl != nullptr && OtherCustomHlsl == nullptr) ||
				(BaseCustomHlsl == nullptr && OtherCustomHlsl != nullptr))
			{
				return false;
			}

			if (BaseCustomHlsl->GetCustomHlsl() != OtherCustomHlsl->GetCustomHlsl() || BaseCustomHlsl->ScriptUsage != OtherCustomHlsl->ScriptUsage)
			{
				return false;
			}
		}
		else if (BaseDynamicValueFunction->GetScratchPadScriptIndex() != INDEX_NONE || OtherDynamicValueFunction->GetScratchPadScriptIndex() != INDEX_NONE)
		{
			int32 BaseScratchPadScriptIndex = BaseDynamicValueFunction->GetScratchPadScriptIndex();
			int32 OtherScratchPadScriptIndex = OtherDynamicValueFunction->GetScratchPadScriptIndex();

			if ((BaseScratchPadScriptIndex != INDEX_NONE && OtherScratchPadScriptIndex == INDEX_NONE) ||
				(BaseScratchPadScriptIndex == INDEX_NONE && OtherScratchPadScriptIndex != INDEX_NONE))
			{
				return false;
			}

			if (BaseScratchPadScriptIndex != OtherScratchPadScriptIndex)
			{
				return false;
			}
		}
		else if (BaseDynamicValueFunction->GetFunctionCallNode()->FunctionScript != OtherDynamicValueFunction->GetFunctionCallNode()->FunctionScript)
		{
			return false;
		}

		FNiagaraScriptStackDiffResults FunctionDiffResults;
		DiffFunctionInputs(BaseDynamicValueFunction, OtherDynamicValueFunction, FunctionDiffResults);

		return
			FunctionDiffResults.RemovedBaseInputOverrides.Num() == 0 &&
			FunctionDiffResults.AddedOtherInputOverrides.Num() == 0 &&
			FunctionDiffResults.ModifiedOtherInputOverrides.Num() == 0;
	}

	// Static switch
	if (BaseFunctionInputAdapter->GetStaticSwitchValue().IsSet() && OtherFunctionInputAdapter->GetStaticSwitchValue().IsSet())
	{
		return BaseFunctionInputAdapter->GetStaticSwitchValue().GetValue() == OtherFunctionInputAdapter->GetStaticSwitchValue().GetValue();
	}

	return TOptional<bool>();
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddModule(
	FString UniqueEmitterName,
	UNiagaraScript& OwningScript,
	UNiagaraNodeOutput& TargetOutputNode,
	const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
	TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModule) const
{
	FApplyDiffResults Results;

	UNiagaraNodeFunctionCall* AddedModuleNode = nullptr;
	if (AddModule->GetFunctionCallNode()->IsA<UNiagaraNodeAssignment>())
	{
		UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(AddModule->GetFunctionCallNode());
		const TArray<FNiagaraVariable>& Targets = AssignmentNode->GetAssignmentTargets();
		const TArray<FString>& Defaults = AssignmentNode->GetAssignmentDefaults();
		AddedModuleNode = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Targets, TargetOutputNode, AddModule->GetStackIndex(),Defaults);
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid;
		AddedModuleNode->RefreshFromExternalChanges();
		Results.bModifiedGraph = true;
	}
	else
	{
		if (AddModule->GetFunctionCallNode()->FunctionScript != nullptr)
		{
			UNiagaraScript* FunctionScript = nullptr;
			if (AddModule->GetScratchPadScriptIndex() != INDEX_NONE)
			{
				UNiagaraScript*const* ScratchScriptPtr = SourceToMergedScratchPadScriptMap.Find(AddModule->GetFunctionCallNode()->FunctionScript);
				if (ScratchScriptPtr != nullptr)
				{
					FunctionScript = *ScratchScriptPtr;
				}
				else
				{
					Results.bSucceeded = false;
					Results.ErrorMessages.Add(FText::Format(
						LOCTEXT("MissingScratchPadScript", "Can not add module {0} from node {1} because its merged instance scratch pad script was missing."),
						FText::FromString(AddModule->GetFunctionCallNode()->GetFunctionName()),
						FText::FromString(AddModule->GetFunctionCallNode()->GetPathName())));
				}
			}
			else
			{
				FunctionScript = AddModule->GetFunctionCallNode()->FunctionScript;
			}

			if (FunctionScript != nullptr)
			{
				AddedModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(FunctionScript, TargetOutputNode, AddModule->GetStackIndex());
				AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's synch up.
				Results.bModifiedGraph = true;
			}
		}
		else
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("AddModuleFailedDueToMissingModuleScriptFormat", "Can not add module {0} from node {1} because its script was missing."),
				FText::FromString(AddModule->GetFunctionCallNode()->GetFunctionName()),
				FText::FromString(AddModule->GetFunctionCallNode()->GetPathName())));
		}
	}

	if (AddedModuleNode != nullptr)
	{
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's synch up.

		AddedModuleNode->SetEnabledState(AddModule->GetFunctionCallNode()->GetDesiredEnabledState(), AddModule->GetFunctionCallNode()->HasUserSetTheEnabledState());
		for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : AddModule->GetInputOverrides())
		{
			FApplyDiffResults AddInputResults = AddInputOverride(UniqueEmitterName, OwningScript, *AddedModuleNode, SourceToMergedScratchPadScriptMap, InputOverride);
			Results.bSucceeded &= AddInputResults.bSucceeded;
			Results.bModifiedGraph |= AddInputResults.bModifiedGraph;
			Results.ErrorMessages.Append(AddInputResults.ErrorMessages);
		}
	}
	else
	{
		Results.bSucceeded = false;
		Results.ErrorMessages.Add(LOCTEXT("AddModuleFailed", "Failed to add module from diff."));
	}

	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::RemoveInputOverride(UNiagaraScript& OwningScript, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToRemove) const
{
	FApplyDiffResults Results;
	if (OverrideToRemove->GetOverridePin() != nullptr && OverrideToRemove->GetOverrideNode() != nullptr)
	{
		FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*OverrideToRemove->GetOverridePin());
		OverrideToRemove->GetOverrideNode()->RemovePin(OverrideToRemove->GetOverridePin());
		Results.bSucceeded = true;
		Results.bModifiedGraph = true;
	}
	else if (OverrideToRemove->GetLocalValueRapidIterationParameter().IsSet())
	{
		OwningScript.Modify();
		OwningScript.RapidIterationParameters.RemoveParameter(OverrideToRemove->GetLocalValueRapidIterationParameter().GetValue());
		Results.bSucceeded = true;
		Results.bModifiedGraph = false;
	}
	else if (OverrideToRemove->GetStaticSwitchValue().IsSet())
	{
		// TODO: Static switches are always treated as overrides right now so removing them is a no-op.  This code should updated
		// so that removing a static switch override sets the value back to the module default.
		Results.bSucceeded = true;
	}
	else
	{
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(LOCTEXT("RemoveInputOverrideFailed", "Failed to remove input override because it was invalid."));
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddInputOverride(
	FString UniqueEmitterName,
	UNiagaraScript& OwningScript,
	UNiagaraNodeFunctionCall& TargetFunctionCall,
	const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
	TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd) const
{
	FApplyDiffResults Results;

	// If an assignment node, make sure that we have an assignment target for the input override.
	UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&TargetFunctionCall);
	if (AssignmentNode)
	{
		FNiagaraParameterHandle FunctionInputHandle(FNiagaraConstants::ModuleNamespace, *OverrideToAdd->GetInputName());
		UNiagaraNodeAssignment* PreviousVersionAssignmentNode = Cast<UNiagaraNodeAssignment>(OverrideToAdd->GetOwningFunctionCall());
		bool bAnyAdded = false;
		for (int32 i = 0; i < PreviousVersionAssignmentNode->NumTargets(); i++)
		{
			const FNiagaraVariable& Var = PreviousVersionAssignmentNode->GetAssignmentTarget(i);

			int32 FoundVarIdx = AssignmentNode->FindAssignmentTarget(Var.GetName());
			if (FoundVarIdx == INDEX_NONE)
			{
				AssignmentNode->AddAssignmentTarget(Var, &PreviousVersionAssignmentNode->GetAssignmentDefaults()[i]);
				bAnyAdded = true;
			}
		}

		if (bAnyAdded)
		{
			AssignmentNode->RefreshFromExternalChanges();
		}
	}

	FNiagaraParameterHandle FunctionInputHandle(FNiagaraConstants::ModuleNamespace, *OverrideToAdd->GetInputName());
	FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FunctionInputHandle, &TargetFunctionCall);

	if (OverrideToAdd->GetOverridePin() != nullptr)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		FNiagaraTypeDefinition InputType = NiagaraSchema->PinToTypeDefinition(OverrideToAdd->GetOverridePin());

		UEdGraphPin& InputOverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(TargetFunctionCall, AliasedFunctionInputHandle, InputType, OverrideToAdd->GetOverrideNode()->NodeGuid);
		if (InputOverridePin.LinkedTo.Num() != 0)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("AddPinBasedInputOverrideFailedOverridePinStillLinkedFormat", "Failed to add input override because the target override pin was still linked to other nodes.  Target Script Usage: {0} Target Script Usage Id: {1} Target Node: {2} Target Input Handle: {3} Linked Node: {4} Linked Pin: {5}"),
				FNiagaraTypeDefinition::GetScriptUsageEnum()->GetDisplayNameTextByValue((int64)OwningScript.GetUsage()),
				FText::FromString(OwningScript.GetUsageId().ToString(EGuidFormats::DigitsWithHyphens)),
				FText::FromString(TargetFunctionCall.GetFunctionName()),
				FText::FromName(AliasedFunctionInputHandle.GetParameterHandleString()),
				InputOverridePin.LinkedTo[0] != nullptr && InputOverridePin.LinkedTo[0]->GetOwningNode() != nullptr
					? InputOverridePin.LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView)
					: FText::FromString(TEXT("(null)")),
				InputOverridePin.LinkedTo[0] != nullptr
					? FText::FromName(InputOverridePin.LinkedTo[0]->PinName)
					: FText::FromString(TEXT("(null)"))));
		}
		else
		{
			if (OverrideToAdd->GetLocalValueString().IsSet())
			{
				InputOverridePin.DefaultValue = OverrideToAdd->GetLocalValueString().GetValue();
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetLinkedValueHandle().IsSet())
			{
				FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(InputOverridePin, OverrideToAdd->GetLinkedValueHandle().GetValue(), OverrideToAdd->GetOverrideNodeId());
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetDataValueInputName().IsSet() && OverrideToAdd->GetDataValueObject() != nullptr)
			{
				FName OverrideValueInputName = OverrideToAdd->GetDataValueInputName().GetValue();
				UNiagaraDataInterface* OverrideValueObject = OverrideToAdd->GetDataValueObject();
				UNiagaraDataInterface* NewOverrideValueObject;
				FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(InputOverridePin, OverrideToAdd->GetDataValueObject()->GetClass(), OverrideValueInputName.ToString(), NewOverrideValueObject, OverrideToAdd->GetOverrideNodeId());
				OverrideValueObject->CopyTo(NewOverrideValueObject);
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetDynamicValueFunction().IsValid())
			{
				UNiagaraNodeCustomHlsl* CustomHlslFunction = Cast<UNiagaraNodeCustomHlsl>(OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode());
				if (CustomHlslFunction != nullptr)
				{
					UNiagaraNodeCustomHlsl* DynamicInputFunctionCall;
					FNiagaraStackGraphUtilities::SetCustomExpressionForFunctionInput(InputOverridePin, CustomHlslFunction->GetCustomHlsl(), DynamicInputFunctionCall, OverrideToAdd->GetOverrideNodeId());
					for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicInputInputOverride : OverrideToAdd->GetDynamicValueFunction()->GetInputOverrides())
					{
						FApplyDiffResults AddResults = AddInputOverride(UniqueEmitterName, OwningScript, *((UNiagaraNodeFunctionCall*)DynamicInputFunctionCall), SourceToMergedScratchPadScriptMap, DynamicInputInputOverride);
						Results.bSucceeded &= AddResults.bSucceeded;
						Results.bModifiedGraph |= AddResults.bModifiedGraph;
						Results.ErrorMessages.Append(AddResults.ErrorMessages);
					}
				}
				else if (OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript != nullptr)
				{
					UNiagaraScript* FunctionScript = nullptr;
					if (OverrideToAdd->GetDynamicValueFunction()->GetScratchPadScriptIndex() != INDEX_NONE)
					{
						UNiagaraScript*const* ScratchScriptPtr = SourceToMergedScratchPadScriptMap.Find(OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript);
						if (ScratchScriptPtr != nullptr)
						{
							FunctionScript = *ScratchScriptPtr;
						}
						else
						{
							Results.bSucceeded = false;
							Results.ErrorMessages.Add(FText::Format(
								LOCTEXT("MissingScratchPadScriptForDynamicInput", "Can not add dynamic input {0} from node {1} because its merged instance scratch pad script was missing."),
								FText::FromString(OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->GetFunctionName()),
								FText::FromString(OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->GetPathName())));
						}
					}
					else
					{
						FunctionScript = OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript;
					}

					if (FunctionScript != nullptr)
					{
						UNiagaraNodeFunctionCall* DynamicInputFunctionCall;
						FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(InputOverridePin, FunctionScript,
							DynamicInputFunctionCall, OverrideToAdd->GetOverrideNodeId(), OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->GetFunctionName());
						for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicInputInputOverride : OverrideToAdd->GetDynamicValueFunction()->GetInputOverrides())
						{
							FApplyDiffResults AddResults = AddInputOverride(UniqueEmitterName, OwningScript, *DynamicInputFunctionCall, SourceToMergedScratchPadScriptMap, DynamicInputInputOverride);
							Results.bSucceeded &= AddResults.bSucceeded;
							Results.bModifiedGraph |= AddResults.bModifiedGraph;
							Results.ErrorMessages.Append(AddResults.ErrorMessages);
						}
					}
				}
				else
				{
					Results.bSucceeded = false;
					Results.ErrorMessages.Add(LOCTEXT("AddPinBasedInputOverrideFailedInvalidDynamicInput", "Failed to add input override because it's dynamic function call's function script was null."));
				}
			}
			else
			{
				Results.bSucceeded = false;
				Results.ErrorMessages.Add(LOCTEXT("AddPinBasedInputOverrideFailed", "Failed to add input override because it was invalid."));
			}
		}
		Results.bModifiedGraph = true;
	}
	else
	{
		if (OverrideToAdd->GetLocalValueRapidIterationParameter().IsSet())
		{
			FNiagaraVariable RapidIterationParameter = FNiagaraStackGraphUtilities::CreateRapidIterationParameter(
				UniqueEmitterName, OwningScript.GetUsage(), AliasedFunctionInputHandle.GetParameterHandleString(), OverrideToAdd->GetLocalValueRapidIterationParameter().GetValue().GetType());
			const uint8* SourceData = OverrideToAdd->GetOwningScript()->RapidIterationParameters.GetParameterData(OverrideToAdd->GetLocalValueRapidIterationParameter().GetValue());
			OwningScript.Modify();
			bool bAddParameterIfMissing = true;
			OwningScript.RapidIterationParameters.SetParameterData(SourceData, RapidIterationParameter, bAddParameterIfMissing);
			Results.bSucceeded = true;
		}
		else if (OverrideToAdd->GetStaticSwitchValue().IsSet())
		{
			TArray<UEdGraphPin*> StaticSwitchPins;
			TSet<UEdGraphPin*> StaticSwitchPinsHidden;
			FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(TargetFunctionCall, StaticSwitchPins, StaticSwitchPinsHidden);
			UEdGraphPin** MatchingStaticSwitchPinPtr = StaticSwitchPins.FindByPredicate([&OverrideToAdd](UEdGraphPin* StaticSwitchPin) { return StaticSwitchPin->PinName == *OverrideToAdd->GetInputName(); });
			if (MatchingStaticSwitchPinPtr != nullptr)
			{
				UEdGraphPin* MatchingStaticSwitchPin = *MatchingStaticSwitchPinPtr;
				const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
				FNiagaraTypeDefinition SwitchType = NiagaraSchema->PinToTypeDefinition(MatchingStaticSwitchPin);
				if (SwitchType == OverrideToAdd->GetType())
				{
					MatchingStaticSwitchPin->DefaultValue = OverrideToAdd->GetStaticSwitchValue().GetValue();
					Results.bSucceeded = true;
				}
				else
				{
					Results.bSucceeded = false;
					Results.ErrorMessages.Add(LOCTEXT("AddStaticInputOverrideFailedWrongType", "Failed to add static switch input override because a the type of the pin matched by name did not match."));
				}
			}
			else
			{
				Results.bSucceeded = false;
				Results.ErrorMessages.Add(LOCTEXT("AddStaticInputOverrideFailedNotFound", "Failed to add static switch input override because a matching pin could not be found."));
			}
		}
		else
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(LOCTEXT("AddParameterBasedInputOverrideFailed", "Failed to add input override because it was invalid."));
		}
		Results.bModifiedGraph = false;
	}
	return Results;
}

void FNiagaraScriptMergeManager::CopyPropertiesToBase(void* BaseDataAddress, const void* OtherDataAddress, TArray<FProperty*> PropertiesToCopy) const
{
	for (FProperty* PropertyToCopy : PropertiesToCopy)
	{
		PropertyToCopy->CopyCompleteValue(PropertyToCopy->ContainerPtrToValuePtr<void>(BaseDataAddress), PropertyToCopy->ContainerPtrToValuePtr<void>(OtherDataAddress));
	}
}

void FNiagaraScriptMergeManager::CopyInstanceScratchPadScripts(UNiagaraEmitter& MergedInstance, const UNiagaraEmitter& SourceInstance, TMap<UNiagaraScript*, UNiagaraScript*>& OutSourceToMergedScratchPadScriptMap) const
{
	for (UNiagaraScript* SourceScratchPadScript : SourceInstance.ScratchPadScripts)
	{
		FName UniqueObjectName = FNiagaraEditorUtilities::GetUniqueObjectName<UNiagaraScript>(&MergedInstance, SourceScratchPadScript->GetName());
		UNiagaraScript* MergedInstanceScratchPadScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(SourceScratchPadScript, &MergedInstance, UniqueObjectName));
		MergedInstance.ScratchPadScripts.Add(MergedInstanceScratchPadScript);
		OutSourceToMergedScratchPadScriptMap.Add(SourceScratchPadScript, MergedInstanceScratchPadScript);
	}
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyScriptStackDiff(
	TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter,
	const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
	const FNiagaraScriptStackDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;

	if (DiffResults.IsEmpty())
	{
		Results.bSucceeded = true;
		Results.bModifiedGraph = false;
		return Results;
	}

	struct FAddInputOverrideActionData
	{
		UNiagaraNodeFunctionCall* TargetFunctionCall;
		TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd;
	};

	// Collect the graph actions from the adapter and diff first.
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> RemoveModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> AddModules;
	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> RemoveInputOverrides;
	TArray<FAddInputOverrideActionData> AddInputOverrideActionDatas;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> EnableModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> DisableModules;

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> RemovedModule : DiffResults.RemovedBaseModules)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(RemovedModule->GetFunctionCallNode()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			if (bNoParentAtLastMerge)
			{
				// If there is no last known parent we don't know if the module was removed in the child, or added in the parent, so
				// instead of removing the parent module we disable it in this case, since removing modules in child emitters isn't
				// supported through the UI.
				DisableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
			else
			{
				RemoveModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
		}
	}

	AddModules.Append(DiffResults.AddedOtherModules);

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> RemovedInputOverrideAdapter : DiffResults.RemovedBaseInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(RemovedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(RemovedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.Add(MatchingInputOverrideAdapter.ToSharedRef());
			}
		}
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> AddedInputOverrideAdapter : DiffResults.AddedOtherInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(AddedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(AddedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.AddUnique(MatchingInputOverrideAdapter.ToSharedRef());
			}

			FAddInputOverrideActionData AddInputOverrideActionData;
			AddInputOverrideActionData.TargetFunctionCall = MatchingModuleAdapter->GetFunctionCallNode();
			AddInputOverrideActionData.OverrideToAdd = AddedInputOverrideAdapter;
			AddInputOverrideActionDatas.Add(AddInputOverrideActionData);
		}
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> ModifiedInputOverrideAdapter : DiffResults.ModifiedOtherInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(ModifiedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(ModifiedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.AddUnique(MatchingInputOverrideAdapter.ToSharedRef());
			}

			FAddInputOverrideActionData AddInputOverrideActionData;
			AddInputOverrideActionData.TargetFunctionCall = MatchingModuleAdapter->GetFunctionCallNode();
			AddInputOverrideActionData.OverrideToAdd = ModifiedInputOverrideAdapter;
			AddInputOverrideActionDatas.Add(AddInputOverrideActionData);
		}
	}

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> EnabledChangedModule : DiffResults.EnabledChangedOtherModules)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(EnabledChangedModule->GetFunctionCallNode()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			if (EnabledChangedModule->GetFunctionCallNode()->IsNodeEnabled())
			{
				EnableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
			else
			{
				DisableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
		}
	}

	// Update the usage if different
	if (DiffResults.ChangedOtherUsage.IsSet())
	{
		BaseScriptStackAdapter->GetScript()->SetUsage(DiffResults.ChangedOtherUsage.GetValue());
		BaseScriptStackAdapter->GetOutputNode()->SetUsage(DiffResults.ChangedOtherUsage.GetValue());
	}

	// Apply the graph actions.
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> RemoveModule : RemoveModules)
	{
		bool bRemoveResults = FNiagaraStackGraphUtilities::RemoveModuleFromStack(*BaseScriptStackAdapter->GetScript(), *RemoveModule->GetFunctionCallNode());
		if (bRemoveResults == false)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(LOCTEXT("RemoveModuleFailedMessage", "Failed to remove module while applying diff"));
		}
		else
		{
			Results.bModifiedGraph = true;
		}
	}

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModuleAdapter : AddModules)
	{
		FApplyDiffResults AddModuleResults = AddModule(BaseScriptStackAdapter->GetUniqueEmitterName(), *BaseScriptStackAdapter->GetScript(), *BaseScriptStackAdapter->GetOutputNode(), SourceToMergedScratchPadScriptMap, AddModuleAdapter);
		Results.bSucceeded &= AddModuleResults.bSucceeded;
		Results.bModifiedGraph |= AddModuleResults.bModifiedGraph;
		Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> RemoveInputOverrideItem : RemoveInputOverrides)
	{
		FApplyDiffResults RemoveInputOverrideResults = RemoveInputOverride(*BaseScriptStackAdapter->GetScript(), RemoveInputOverrideItem);
		Results.bSucceeded &= RemoveInputOverrideResults.bSucceeded;
		Results.bModifiedGraph |= RemoveInputOverrideResults.bModifiedGraph;
		Results.ErrorMessages.Append(RemoveInputOverrideResults.ErrorMessages);
	}

	for (const FAddInputOverrideActionData& AddInputOverrideActionData : AddInputOverrideActionDatas)
	{
		FApplyDiffResults AddInputOverrideResults = AddInputOverride(BaseScriptStackAdapter->GetUniqueEmitterName(), *BaseScriptStackAdapter->GetScript(),
			*AddInputOverrideActionData.TargetFunctionCall, SourceToMergedScratchPadScriptMap, AddInputOverrideActionData.OverrideToAdd.ToSharedRef());
		Results.bSucceeded &= AddInputOverrideResults.bSucceeded;
		Results.bModifiedGraph |= AddInputOverrideResults.bModifiedGraph;
		Results.ErrorMessages.Append(AddInputOverrideResults.ErrorMessages);
	}

	// Apply enabled state last so that it applies to function calls added  from input overrides;
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> EnableModule : EnableModules)
	{
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*EnableModule->GetFunctionCallNode(), true);
	}
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> DisableModule : DisableModules)
	{
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*DisableModule->GetFunctionCallNode(), false);
	}

	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyEventHandlerDiff(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
	const FNiagaraEmitterDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseEventHandlers.Num() > 0)
	{
		// If this becomes supported, it needs to handle the bNoParentAtLastMerge case
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(LOCTEXT("RemovedEventHandlersUnsupported", "Apply diff failed, removed event handlers are currently unsupported."));
		return Results;
	}

	// Apply the modifications first since adding new event handlers may invalidate the adapter.
	for (const FNiagaraModifiedEventHandlerDiffResults& ModifiedEventHandler : DiffResults.ModifiedEventHandlers)
	{
		if (ModifiedEventHandler.OtherAdapter->GetEventScriptProperties() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedEventPropertiesFormat", "Apply diff failed.  The modified event handler with id: {0} was missing it's event properties."),
				FText::FromString(ModifiedEventHandler.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (ModifiedEventHandler.OtherAdapter->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedEventOutputNodeFormat", "Apply diff failed.  The modified event handler with id: {0} was missing it's output node."),
				FText::FromString(ModifiedEventHandler.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			TSharedPtr<FNiagaraEventHandlerMergeAdapter> MatchingBaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(ModifiedEventHandler.OtherAdapter->GetUsageId());
			if (MatchingBaseEventHandlerAdapter.IsValid())
			{
				if (ModifiedEventHandler.ChangedProperties.Num() > 0)
				{
					CopyPropertiesToBase(MatchingBaseEventHandlerAdapter->GetEditableEventScriptProperties(), ModifiedEventHandler.OtherAdapter->GetEditableEventScriptProperties(), ModifiedEventHandler.ChangedProperties);
				}
				if (ModifiedEventHandler.ScriptDiffResults.IsEmpty() == false)
				{
					FApplyDiffResults ApplyEventHandlerStackDiffResults = ApplyScriptStackDiff(MatchingBaseEventHandlerAdapter->GetEventStack().ToSharedRef(), 
						SourceToMergedScratchPadScriptMap, ModifiedEventHandler.ScriptDiffResults, bNoParentAtLastMerge);
					Results.bSucceeded &= ApplyEventHandlerStackDiffResults.bSucceeded;
					Results.bModifiedGraph |= ApplyEventHandlerStackDiffResults.bModifiedGraph;
					Results.ErrorMessages.Append(ApplyEventHandlerStackDiffResults.ErrorMessages);
				}
			}
		}
	}

	UNiagaraScriptSource* EmitterSource = CastChecked<UNiagaraScriptSource>(BaseEmitterAdapter->GetEditableEmitter()->GraphSource);
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;
	for (TSharedRef<FNiagaraEventHandlerMergeAdapter> AddedEventHandler : DiffResults.AddedOtherEventHandlers)
	{
		if (AddedEventHandler->GetEventScriptProperties() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedEventPropertiesFormat", "Apply diff failed.  The added event handler with id: {0} was missing it's event properties."),
				FText::FromString(AddedEventHandler->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (AddedEventHandler->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedEventOutputNodeFormat", "Apply diff failed.  The added event handler with id: {0} was missing it's output node."),
				FText::FromString(AddedEventHandler->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			UNiagaraEmitter* BaseEmitter = BaseEmitterAdapter->GetEditableEmitter();
			FNiagaraEventScriptProperties AddedEventScriptProperties = *AddedEventHandler->GetEventScriptProperties();
			AddedEventScriptProperties.Script = NewObject<UNiagaraScript>(BaseEmitter, MakeUniqueObjectName(BaseEmitter, UNiagaraScript::StaticClass(), "EventScript"), EObjectFlags::RF_Transactional);
			AddedEventScriptProperties.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
			AddedEventScriptProperties.Script->SetUsageId(AddedEventHandler->GetUsageId());
			AddedEventScriptProperties.Script->SetSource(EmitterSource);
			BaseEmitter->AddEventHandler(AddedEventScriptProperties);

			FGuid PreferredOutputNodeGuid = AddedEventHandler->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedEventHandler->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* EventOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleEventScript, AddedEventScriptProperties.Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedEventHandler->GetEventStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitter->GetUniqueEmitterName(), *AddedEventScriptProperties.Script, *EventOutputNode, SourceToMergedScratchPadScriptMap, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new event handler to match the added instance event handler.
			UNiagaraScriptSource* AddedEventScriptSourceFromDiff = Cast<UNiagaraScriptSource>(AddedEventHandler->GetEventScriptProperties()->Script->GetSource());
			UNiagaraGraph* AddedEventScriptGraphFromDiff = AddedEventScriptSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedEventScriptGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId());
			UNiagaraScriptSource* AddedEventScriptSource = Cast<UNiagaraScriptSource>(AddedEventScriptProperties.Script->GetSource());
			UNiagaraGraph* AddedEventScriptGraph = AddedEventScriptSource->NodeGraph;
			AddedEventScriptGraph->ForceBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplySimulationStageDiff(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
	const FNiagaraEmitterDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseSimulationStages.Num() > 0)
	{
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		// If this becomes supported, it needs to handle the bNoParentAtLastMerge case
		Results.ErrorMessages.Add(LOCTEXT("RemovedSimulationStagesUnsupported", "Apply diff failed, removed shader stages are currently unsupported."));
		return Results;
	}

	for (const FNiagaraModifiedSimulationStageDiffResults& ModifiedSimulationStage : DiffResults.ModifiedSimulationStages)
	{
		if (ModifiedSimulationStage.OtherAdapter->GetSimulationStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedSimulationStageObjectFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(ModifiedSimulationStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (ModifiedSimulationStage.OtherAdapter->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedSimulationStageOutputNodeFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's output node."),
				FText::FromString(ModifiedSimulationStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			TSharedPtr<FNiagaraSimulationStageMergeAdapter> MatchingBaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(ModifiedSimulationStage.OtherAdapter->GetUsageId());
			if (MatchingBaseSimulationStageAdapter.IsValid())
			{
				if (ModifiedSimulationStage.ChangedProperties.Num() > 0)
				{
					CopyPropertiesToBase(MatchingBaseSimulationStageAdapter->GetEditableSimulationStage(), ModifiedSimulationStage.OtherAdapter->GetEditableSimulationStage(), ModifiedSimulationStage.ChangedProperties);
				}
				if (ModifiedSimulationStage.ScriptDiffResults.IsEmpty() == false)
				{
					FApplyDiffResults ApplySimulationStageStackDiffResults = ApplyScriptStackDiff(MatchingBaseSimulationStageAdapter->GetSimulationStageStack().ToSharedRef(), SourceToMergedScratchPadScriptMap, ModifiedSimulationStage.ScriptDiffResults, bNoParentAtLastMerge);
					Results.bSucceeded &= ApplySimulationStageStackDiffResults.bSucceeded;
					Results.bModifiedGraph |= ApplySimulationStageStackDiffResults.bModifiedGraph;
					Results.ErrorMessages.Append(ApplySimulationStageStackDiffResults.ErrorMessages);
				}
			}
		}
	}

	UNiagaraScriptSource* EmitterSource = CastChecked<UNiagaraScriptSource>(BaseEmitterAdapter->GetEditableEmitter()->GraphSource);
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;
	for (TSharedRef<FNiagaraSimulationStageMergeAdapter> AddedOtherSimulationStage : DiffResults.AddedOtherSimulationStages)
	{
		if (AddedOtherSimulationStage->GetSimulationStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedSimulationStageObjectFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(AddedOtherSimulationStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (AddedOtherSimulationStage->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedSimulationStageOutputNodeFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's output node."),
				FText::FromString(AddedOtherSimulationStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			UNiagaraEmitter* BaseEmitter = BaseEmitterAdapter->GetEditableEmitter();
			UNiagaraSimulationStageBase* AddedSimulationStage = CastChecked<UNiagaraSimulationStageBase>(StaticDuplicateObject(AddedOtherSimulationStage->GetSimulationStage(), BaseEmitter));
			AddedSimulationStage->Script = NewObject<UNiagaraScript>(AddedSimulationStage, MakeUniqueObjectName(AddedSimulationStage, UNiagaraScript::StaticClass(), "SimulationStage"), EObjectFlags::RF_Transactional);
			AddedSimulationStage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
			AddedSimulationStage->Script->SetUsageId(AddedOtherSimulationStage->GetUsageId());
			AddedSimulationStage->Script->SetSource(EmitterSource);
			BaseEmitter->AddSimulationStage(AddedSimulationStage);

			FGuid PreferredOutputNodeGuid = AddedOtherSimulationStage->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedOtherSimulationStage->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* SimulationStageOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleSimulationStageScript, AddedSimulationStage->Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedOtherSimulationStage->GetSimulationStageStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitter->GetUniqueEmitterName(), *AddedSimulationStage->Script, *SimulationStageOutputNode, SourceToMergedScratchPadScriptMap, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new shader stage to match the added instance shader stage.
			UNiagaraScriptSource* AddedSimulationStageSourceFromDiff = Cast<UNiagaraScriptSource>(AddedOtherSimulationStage->GetSimulationStage()->Script->GetSource());
			UNiagaraGraph* AddedSimulationStageGraphFromDiff = AddedSimulationStageSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedSimulationStageGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleSimulationStageScript, AddedOtherSimulationStage->GetUsageId());
			UNiagaraScriptSource* AddedSimulationStageSource = Cast<UNiagaraScriptSource>(AddedSimulationStage->Script->GetSource());
			UNiagaraGraph* AddedSimulationStageGraph = AddedSimulationStageSource->NodeGraph;
			AddedSimulationStageGraph->ForceBaseId(ENiagaraScriptUsage::ParticleSimulationStageScript, AddedOtherSimulationStage->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyRendererDiff(UNiagaraEmitter& BaseEmitter, const FNiagaraEmitterDiffResults& DiffResults, const bool bNoParentAtLastMerge) const
{
	TArray<UNiagaraRendererProperties*> RenderersToRemove;
	TArray<UNiagaraRendererProperties*> RenderersToAdd;
	TArray<UNiagaraRendererProperties*> RenderersToDisable;

	for (TSharedRef<FNiagaraRendererMergeAdapter> RemovedRenderer : DiffResults.RemovedBaseRenderers)
	{
		auto FindRendererByMergeId = [=](UNiagaraRendererProperties* Renderer) { return Renderer->GetMergeId() == RemovedRenderer->GetRenderer()->GetMergeId(); };
		UNiagaraRendererProperties* const* MatchingRendererPtr = BaseEmitter.GetRenderers().FindByPredicate(FindRendererByMergeId);
		if (MatchingRendererPtr != nullptr)
		{
			if (bNoParentAtLastMerge)
			{
				// If there is no last known parent we don't know if the renderer was removed in the child, or added in the parent, so
				// instead of removing the parent renderer we disable it in this case, since removing renderers in child emitters isn't
				// supported through the UI, and instead the user is expected to disable it.
				RenderersToDisable.Add(*MatchingRendererPtr);
			}
			else
			{
				RenderersToRemove.Add(*MatchingRendererPtr);
			}
		}
	}

	for (TSharedRef<FNiagaraRendererMergeAdapter> AddedRenderer : DiffResults.AddedOtherRenderers)
	{
		RenderersToAdd.Add(Cast<UNiagaraRendererProperties>(StaticDuplicateObject(AddedRenderer->GetRenderer(), &BaseEmitter)));
	}

	for (TSharedRef<FNiagaraRendererMergeAdapter> ModifiedRenderer : DiffResults.ModifiedOtherRenderers)
	{
		auto FindRendererByMergeId = [=](UNiagaraRendererProperties* Renderer) { return Renderer->GetMergeId() == ModifiedRenderer->GetRenderer()->GetMergeId(); };
		UNiagaraRendererProperties*const* MatchingRendererPtr = BaseEmitter.GetRenderers().FindByPredicate(FindRendererByMergeId);
		if (MatchingRendererPtr != nullptr)
		{
			RenderersToRemove.Add(*MatchingRendererPtr);
			RenderersToAdd.Add(Cast<UNiagaraRendererProperties>(StaticDuplicateObject(ModifiedRenderer->GetRenderer(), &BaseEmitter)));
		}
	}

	for (UNiagaraRendererProperties* RendererToRemove : RenderersToRemove)
	{
		BaseEmitter.RemoveRenderer(RendererToRemove);
	}

	for (UNiagaraRendererProperties* RendererToAdd : RenderersToAdd)
	{
		BaseEmitter.AddRenderer(RendererToAdd);
	}

	for (UNiagaraRendererProperties* RendererToDisable : RenderersToDisable)
	{
		RendererToDisable->bIsEnabled = false;
	}

	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyStackEntryDisplayNameDiffs(UNiagaraEmitter& Emitter, const FNiagaraEmitterDiffResults& DiffResults) const
{
	if (DiffResults.ModifiedStackEntryDisplayNames.Num() > 0)
	{
		UNiagaraEmitterEditorData* EditorData = Cast<UNiagaraEmitterEditorData>(Emitter.GetEditorData());
		if (EditorData == nullptr)
		{
			EditorData = NewObject<UNiagaraEmitterEditorData>(&Emitter, NAME_None, RF_Transactional);
			Emitter.SetEditorData(EditorData);
		}

		for (auto& Pair : DiffResults.ModifiedStackEntryDisplayNames)
		{
			EditorData->GetStackEditorData().SetStackEntryDisplayName(Pair.Key, Pair.Value);
		}
	}

	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;
	return Results;
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(const UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = CachedMergeAdapters.Find(FObjectKey(&Emitter));
	if (CachedMergeAdapter == nullptr)
	{
		CachedMergeAdapter = &CachedMergeAdapters.Add(FObjectKey(&Emitter));
	}

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() != nullptr ||
		CachedMergeAdapter->ChangeId != Emitter.GetChangeId())
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
		CachedMergeAdapter->ChangeId = Emitter.GetChangeId();
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = CachedMergeAdapters.Find(FObjectKey(&Emitter));
	if (CachedMergeAdapter == nullptr)
	{
		CachedMergeAdapter = &CachedMergeAdapters.Add(FObjectKey(&Emitter));
	}

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() == nullptr ||
		CachedMergeAdapter->ChangeId != Emitter.GetChangeId())
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
		CachedMergeAdapter->ChangeId = Emitter.GetChangeId();
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}


#undef LOCTEXT_NAMESPACE
