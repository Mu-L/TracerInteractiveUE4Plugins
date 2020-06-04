// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorUtilities.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraph.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeInput.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraHlslTranslator.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Stats/Stats.h"
#include "NiagaraEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeEmitter"

DECLARE_CYCLE_STAT(TEXT("Niagara - Module - NiagaraNodeEmitter_Compile"), STAT_NiagaraEditor_Module_NiagaraNodeEmitter_Compile, STATGROUP_NiagaraEditor);


void UNiagaraNodeEmitter::PostInitProperties()
{
	Super::PostInitProperties();
	PinPendingRename = nullptr;
	CachedGraph = nullptr;
	CachedScriptSource = nullptr;
}

UNiagaraSystem* UNiagaraNodeEmitter::GetOwnerSystem() const
{
	return OwnerSystem;
}

void UNiagaraNodeEmitter::SetOwnerSystem(UNiagaraSystem* InOwnerSystem)
{
	OwnerSystem = InOwnerSystem;
	RefreshFromExternalChanges();
}

FGuid UNiagaraNodeEmitter::GetEmitterHandleId() const
{
	return EmitterHandleId;
}

void UNiagaraNodeEmitter::SetEmitterHandleId(FGuid InEmitterHandleId)
{
	EmitterHandleId = InEmitterHandleId;
	DisplayName = GetNameFromEmitter();
}

void UNiagaraNodeEmitter::PostLoad()
{
	Super::PostLoad();
}

bool UNiagaraNodeEmitter::IsPinNameEditable(const UEdGraphPin* GraphPinObj) const
{
	return false;
}

bool UNiagaraNodeEmitter::IsPinNameEditableUponCreation(const UEdGraphPin* GraphPinObj) const
{
	return false;
}


bool UNiagaraNodeEmitter::VerifyEditablePinName(const FText& InName, FText& OutErrorMessage, const UEdGraphPin* InGraphPinObj) const
{
	if (InName.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("InvalidName", "Invalid pin name");
		return false;
	}
	return true;
}

bool UNiagaraNodeEmitter::CommitEditablePinName(const FText& InName, UEdGraphPin* InGraphPinObj, bool bSuppressEvents)
{
	return false;
}

bool UNiagaraNodeEmitter::GenerateCompileHashForClassMembers(const UClass* InClass, FNiagaraCompileHashVisitor* InVisitor) const
{
	if (InClass == UNiagaraNodeEmitter::StaticClass())
	{
		// For emitters, we really just want the emitter name.
		FName EmitterName;
		if (OwnerSystem != nullptr && EmitterHandleId.IsValid())
		{
			for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
			{
				if (EmitterHandle.GetId() == EmitterHandleId)
				{
					EmitterName = (EmitterHandle.GetName());
					break;
				}
			}
		}
		else if (CachedUniqueName.IsValid())
		{
			EmitterName = (CachedUniqueName);
		}

		InVisitor->UpdateString(TEXT("EmitterName"), EmitterName.ToString());
		return true;
	}
	else
	{
		return Super::GenerateCompileHashForClassMembers(InClass, InVisitor);
	}
}

void UNiagaraNodeEmitter::AllocateDefaultPins()
{
	UNiagaraEmitter* Emitter = nullptr;
	if (OwnerSystem)
	{
		for (int32 i = 0; i < OwnerSystem->GetNumEmitters(); ++i)
		{
			if (OwnerSystem->GetEmitterHandle(i).GetId() == EmitterHandleId)
			{
				Emitter = OwnerSystem->GetEmitterHandle(i).GetInstance();
			}
		}
	}

	const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(GetSchema());
	CreatePin(EGPD_Input, NiagaraSchema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetParameterMapDef()), TEXT("InputMap"));
	CreatePin(EGPD_Output, NiagaraSchema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetParameterMapDef()), TEXT("OutputMap"));
}

bool UNiagaraNodeEmitter::CanUserDeleteNode() const
{
	return false;
}

bool UNiagaraNodeEmitter::CanDuplicateNode() const
{
	return false;
}

FText UNiagaraNodeEmitter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FText UsageText;
	if (ScriptType == ENiagaraScriptUsage::EmitterSpawnScript)
	{
		UsageText = LOCTEXT("SpawnTitle", "Spawn");
	}
	else if (ScriptType == ENiagaraScriptUsage::EmitterUpdateScript)
	{
		UsageText = LOCTEXT("UpdateTitle", "Update");
	}
	else
	{
		UsageText = LOCTEXT("Unknown Title", "Unknown");
	}
	return FText::Format(LOCTEXT("EmitterNameTitle","Emitter {0} {1}"), DisplayName, UsageText);
}

FLinearColor UNiagaraNodeEmitter::GetNodeTitleColor() const
{
	return CastChecked<UEdGraphSchema_Niagara>(GetSchema())->NodeTitleColor_Attribute;
}

void UNiagaraNodeEmitter::NodeConnectionListChanged()
{
	MarkNodeRequiresSynchronization(__FUNCTION__, true);
	//GetGraph()->NotifyGraphChanged();
}

FString UNiagaraNodeEmitter::GetEmitterUniqueName() const
{
	if (OwnerSystem != nullptr && EmitterHandleId.IsValid())
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
		{
			if (EmitterHandle.GetId() == EmitterHandleId)
			{
				return EmitterHandle.GetUniqueInstanceName();
			}
		}
	}

	return CachedUniqueName.ToString();
}

UNiagaraScriptSource* UNiagaraNodeEmitter::GetScriptSource() const
{
	// First get the emitter that we're referencing..
	UNiagaraEmitter* Emitter = nullptr;
	if (OwnerSystem)
	{
		for (int32 i = 0; i < OwnerSystem->GetNumEmitters(); ++i)
		{
			if (OwnerSystem->GetEmitterHandle(i).GetId() == EmitterHandleId)
			{
				Emitter = OwnerSystem->GetEmitterHandle(i).GetInstance();
			}
		}
	}

	// Now get the graph off that emitter
	if (Emitter != nullptr && Emitter->GraphSource != nullptr)
	{
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Emitter->GraphSource);
		return Source;
	}

	return Cast<UNiagaraScriptSource>(CachedScriptSource);
}

UNiagaraGraph* UNiagaraNodeEmitter::GetCalledGraph() const
{
	// First get the emitter that we're referencing..
	UNiagaraEmitter* Emitter = nullptr;
	if (OwnerSystem)
	{
		for (int32 i = 0; i < OwnerSystem->GetNumEmitters(); ++i)
		{
			if (OwnerSystem->GetEmitterHandle(i).GetId() == EmitterHandleId)
			{
				Emitter = OwnerSystem->GetEmitterHandle(i).GetInstance();
			}
		}
	}

	// Now get the graph off that emitter
	if (Emitter != nullptr && Emitter->GraphSource != nullptr)
	{
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Emitter->GraphSource);
		if (Source)
		{
			return Source->NodeGraph;
		}
	}

	if (CachedGraph != nullptr)
	{
		return CachedGraph;
	}
	return nullptr;
}

bool UNiagaraNodeEmitter::RefreshFromExternalChanges()
{
	DisplayName = GetNameFromEmitter();
	ENodeEnabledState OldEnabledState = GetDesiredEnabledState();
	SyncEnabledState();
	if (OldEnabledState != GetDesiredEnabledState())
	{
		MarkNodeRequiresSynchronization(TEXT("Emitter Node Enabled Changed"), true);
	}
	return true;
}

void UNiagaraNodeEmitter::SyncEnabledState()
{
	if (OwnerSystem != nullptr && EmitterHandleId.IsValid())
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
		{
			if (EmitterHandle.GetId() == EmitterHandleId)
			{
				if (EmitterHandle.GetIsEnabled() == false)
				{
					SetEnabledState(ENodeEnabledState::Disabled, false);
				}
				else
				{
					SetEnabledState(ENodeEnabledState::Enabled, false);
				}
			}
		}
	}
}

void UNiagaraNodeEmitter::SetCachedVariablesForCompilation(const FName& InUniqueName, UNiagaraGraph* InGraph, UNiagaraScriptSourceBase* InSource)
{
	CachedUniqueName = InUniqueName;
	CachedGraph = InGraph;
	CachedScriptSource = InSource;
}


FText UNiagaraNodeEmitter::GetNameFromEmitter()
{
	if (OwnerSystem != nullptr && EmitterHandleId.IsValid())
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
		{
			if (EmitterHandle.GetId() == EmitterHandleId)
			{
				return FText::AsCultureInvariant(EmitterHandle.GetName().ToString());
			}
		}
	}
	else if (CachedUniqueName.IsValid())
	{
		return FText::AsCultureInvariant(CachedUniqueName.ToString());
	}
	return FText();
}

void UNiagaraNodeEmitter::BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive /*= true*/, bool bFilterForCompilation /*= true*/) const
{
	Super::BuildParameterMapHistory(OutHistory, bRecursive, bFilterForCompilation);

	if (!IsNodeEnabled() && OutHistory.GetIgnoreDisabled())
	{
		RouteParameterMapAroundMe(OutHistory, bRecursive);
		return;
	}

	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);
	TArray<UEdGraphPin*> OutputPins;
	GetOutputPins(OutputPins);
	
	int32 ParamMapIdx = INDEX_NONE;
	if (GetInputPin(0)->LinkedTo.Num() != 0)
	{
		if (bRecursive)
		{
			ParamMapIdx = OutHistory.TraceParameterMapOutputPin(UNiagaraNode::TraceOutputPin(GetInputPin(0)->LinkedTo[0]));
		}
		else
		{
			ParamMapIdx = OutHistory.CreateParameterMap();
		}
	}

	FString EmitterUniqueName = GetEmitterUniqueName();
	UNiagaraGraph* Graph = GetCalledGraph();
	if (Graph && ParamMapIdx != INDEX_NONE && OutHistory.bShouldBuildSubHistories)
	{
		OutHistory.EnterEmitter(EmitterUniqueName, Graph, this);

		TArray<ENiagaraScriptUsage> Usages;
		Usages.Add(ENiagaraScriptUsage::EmitterSpawnScript);
		Usages.Add(ENiagaraScriptUsage::EmitterUpdateScript);
		Usages.Add(ENiagaraScriptUsage::ParticleSpawnScript);
		Usages.Add(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated);
		Usages.Add(ENiagaraScriptUsage::ParticleUpdateScript);
		Usages.Add(ENiagaraScriptUsage::ParticleEventScript);
		Usages.Add(ENiagaraScriptUsage::ParticleSimulationStageScript);
	
		uint32 NodeIdx = OutHistory.BeginNodeVisitation(ParamMapIdx, this);
		for (ENiagaraScriptUsage OutputNodeUsage : Usages)
		{
			TArray<UNiagaraNodeOutput*> OutputNodes;

			Graph->FindOutputNodes(OutputNodeUsage, OutputNodes);

			// Build up a new parameter map history with all the child graph nodes..
			FNiagaraParameterMapHistoryBuilder ChildBuilder;
			ChildBuilder.ConstantResolver = OutHistory.ConstantResolver;
			ChildBuilder.RegisterEncounterableVariables(OutHistory.GetEncounterableVariables());
			ChildBuilder.EnableScriptWhitelist(true, GetUsage());
			FString LocalEmitterName = TEXT("Emitter");
			ChildBuilder.EnterEmitter(LocalEmitterName, Graph, this);
			for (UNiagaraNodeOutput* OutputNode : OutputNodes)
			{
				ChildBuilder.BuildParameterMaps(OutputNode, true);
			}
			ChildBuilder.ExitEmitter(LocalEmitterName, this);
			 
			TMap<FString, FString> RenameMap;
			RenameMap.Add(LocalEmitterName, EmitterUniqueName);
			for (FNiagaraParameterMapHistory& History : ChildBuilder.Histories)
			{
				OutHistory.Histories[ParamMapIdx].MapPinHistory.Append(History.MapPinHistory);
				for (int32 SrcVarIdx = 0; SrcVarIdx < History.Variables.Num(); SrcVarIdx++)
				{
					FNiagaraVariable& Var = History.Variables[SrcVarIdx];
					Var = FNiagaraParameterMapHistory::ResolveAliases(Var, RenameMap, TEXT("."));

					int32 ExistingIdx = OutHistory.Histories[ParamMapIdx].FindVariable(Var.GetName(), Var.GetType());
					if (ExistingIdx == INDEX_NONE)
					{
						ExistingIdx = OutHistory.AddVariableToHistory(OutHistory.Histories[ParamMapIdx], Var, History.VariablesWithOriginalAliasesIntact[SrcVarIdx], nullptr);
					}
					ensure(ExistingIdx < OutHistory.Histories[ParamMapIdx].PerVariableWarnings.Num());
					ensure(ExistingIdx < OutHistory.Histories[ParamMapIdx].PerVariableReadHistory.Num());
					ensure(ExistingIdx < OutHistory.Histories[ParamMapIdx].PerVariableWriteHistory.Num());
					OutHistory.Histories[ParamMapIdx].PerVariableReadHistory[ExistingIdx].Append(History.PerVariableReadHistory[SrcVarIdx]);
					OutHistory.Histories[ParamMapIdx].PerVariableWriteHistory[ExistingIdx].Append(History.PerVariableWriteHistory[SrcVarIdx]);
					OutHistory.Histories[ParamMapIdx].PerVariableWarnings[ExistingIdx].Append(History.PerVariableWarnings[SrcVarIdx]);	
				}
				OutHistory.Histories[ParamMapIdx].ParameterCollections.Append(History.ParameterCollections);
				OutHistory.Histories[ParamMapIdx].ParameterCollectionNamespaces.Append(History.ParameterCollectionNamespaces);
				OutHistory.Histories[ParamMapIdx].ParameterCollectionVariables.Append(History.ParameterCollectionVariables);
			}
		}

		OutHistory.EndNodeVisitation(ParamMapIdx, NodeIdx);
		OutHistory.ExitEmitter(EmitterUniqueName, this);
	}

	for (UEdGraphPin* Pin : OutputPins)
	{
		FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(Pin);
		if (Type == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			OutHistory.RegisterParameterMapPin(ParamMapIdx, Pin);
		}
	}
}

void UNiagaraNodeEmitter::Compile(FHlslNiagaraTranslator *Translator, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraNodeEmitter_Compile);
	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);
	InputPins.RemoveAll([](UEdGraphPin* InputPin) { return (InputPin->PinType.PinCategory != UEdGraphSchema_Niagara::PinCategoryType) && (InputPin->PinType.PinCategory != UEdGraphSchema_Niagara::PinCategoryEnum); });

	TArray<UEdGraphPin*> OutputPins;
	GetOutputPins(OutputPins);

	check(Outputs.Num() == 0);

	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());

	// First compile fully down the hierarchy for our predecessors..
	UNiagaraGraph* CalledGraph = GetCalledGraph();
	TArray<UNiagaraNodeInput*> InputsNodes;
	UNiagaraGraph::FFindInputNodeOptions Options;
	Options.bSort = true;
	Options.bFilterDuplicates = true;
	Options.bFilterByScriptUsage = true;
	Options.TargetScriptUsage = Translator->GetTargetUsage() == ENiagaraScriptUsage::SystemSpawnScript ? ENiagaraScriptUsage::EmitterSpawnScript : ENiagaraScriptUsage::EmitterUpdateScript;
	
	if (CalledGraph && IsNodeEnabled()) // Called graph may be null on an disabled emitter
	{
		CalledGraph->FindInputNodes(InputsNodes, Options);
	}

	TArray<int32> CompileInputs;
	
	if (InputPins.Num() > 1)
	{
		Translator->Error(LOCTEXT("TooManyOutputPinsError", "Too many input pins on node."), this, nullptr);
		return;
	}

	int32 InputPinCompiled = Translator->CompilePin(InputPins[0]);
	if (!IsNodeEnabled())
	{
		// Do the minimal amount of work necessary if we are disabled.
		CompileInputs.Reserve(1);
		CompileInputs.Add(InputPinCompiled);
		Translator->Emitter(this, CompileInputs, Outputs);
		return;
	}

	if (InputsNodes.Num() <= 0)
	{
		Translator->Error(LOCTEXT("InputNodesNotFound", "Input nodes on called graph not found"), this, nullptr);
		return;
	}

	CompileInputs.Reserve(InputsNodes.Num());

	bool bError = false;
	for (UNiagaraNodeInput* EmitterInputNode : InputsNodes)
	{
		if (EmitterInputNode->Input.IsEquivalent(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"))))
		{
			CompileInputs.Add(InputPinCompiled);
		}
		else
		{
			CompileInputs.Add(INDEX_NONE);
		}
	}

	if (!bError)
	{
		Translator->Emitter(this, CompileInputs, Outputs);
	}
}

void UNiagaraNodeEmitter::GatherExternalDependencyData(ENiagaraScriptUsage InMasterUsage, const FGuid& InMasterUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FString>& InReferencedObjs) const
{
	UNiagaraGraph* CalledGraph = GetCalledGraph();

	if (CalledGraph && IsNodeEnabled()) // Skip if disabled
	{
		ENiagaraScriptUsage TargetUsage = InMasterUsage == ENiagaraScriptUsage::SystemSpawnScript ? ENiagaraScriptUsage::EmitterSpawnScript : ENiagaraScriptUsage::EmitterUpdateScript;
		InReferencedCompileHashes.Add(CalledGraph->GetCompileDataHash(TargetUsage, FGuid(0,0,0,0)));
		InReferencedObjs.Add(CalledGraph->GetPathName());
		CalledGraph->GatherExternalDependencyData(TargetUsage, FGuid(0, 0, 0, 0), InReferencedCompileHashes, InReferencedObjs);
	}
}

#undef LOCTEXT_NAMESPACE // NiagaraNodeEmitter
