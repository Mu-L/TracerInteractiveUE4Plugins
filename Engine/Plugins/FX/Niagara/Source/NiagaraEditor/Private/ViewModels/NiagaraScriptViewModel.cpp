// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptViewModel.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptInputCollectionViewModel.h"
#include "NiagaraScriptOutputCollectionViewModel.h"
#include "NiagaraNodeInput.h"
#include "INiagaraCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "GraphEditAction.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "ViewModels/TNiagaraViewModelManager.h"
#include "NiagaraDataInterface.h"

template<> TMap<UNiagaraScript*, TArray<FNiagaraScriptViewModel*>> TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptViewModel>::ObjectsToViewModels{};

FNiagaraScriptViewModel::FNiagaraScriptViewModel(FText DisplayName, ENiagaraParameterEditMode InParameterEditMode)
	: InputCollectionViewModel(MakeShareable(new FNiagaraScriptInputCollectionViewModel(DisplayName, InParameterEditMode)))
	, OutputCollectionViewModel(MakeShareable(new FNiagaraScriptOutputCollectionViewModel(InParameterEditMode)))
	, GraphViewModel(MakeShareable(new FNiagaraScriptGraphViewModel(DisplayName)))
	, VariableSelection(MakeShareable(new FNiagaraObjectSelection()))
	, bUpdatingSelectionInternally(false)
	, LastCompileStatus(ENiagaraScriptCompileStatus::NCS_Unknown)
{
	InputCollectionViewModel->GetSelection().OnSelectedObjectsChanged().AddRaw(this, &FNiagaraScriptViewModel::InputViewModelSelectionChanged);
	InputCollectionViewModel->OnParameterValueChanged().AddRaw(this, &FNiagaraScriptViewModel::InputParameterValueChanged);
	OutputCollectionViewModel->OnParameterValueChanged().AddRaw(this, &FNiagaraScriptViewModel::OutputParameterValueChanged);
	GraphViewModel->GetNodeSelection()->OnSelectedObjectsChanged().AddRaw(this, &FNiagaraScriptViewModel::GraphViewModelSelectedNodesChanged);
	GEditor->RegisterForUndo(this);
}

void FNiagaraScriptViewModel::OnVMScriptCompiled(UNiagaraScript* InScript)
{
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		UNiagaraScript* Script = Scripts[i].Get();
		if (Script == InScript && InScript->IsStandaloneScript())
		{
			if (InScript->GetVMExecutableData().IsValid() == false)
			{
				continue;
			}

			LastCompileStatus = InScript->GetVMExecutableData().LastCompileStatus;

			if (InScript->GetVMExecutableData().LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error)
			{
				const FString& ErrorMsg = InScript->GetVMExecutableData().ErrorMsg;
				GraphViewModel->SetErrorTextToolTip(ErrorMsg + "\n(These same errors are also in the log)");
			}
			else
			{
				GraphViewModel->SetErrorTextToolTip("");
			}

			InputCollectionViewModel->RefreshParameterViewModels();
			OutputCollectionViewModel->RefreshParameterViewModels();
		}
	}
}

bool FNiagaraScriptViewModel::IsGraphDirty() const
{
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (!Scripts[i].IsValid())
		{
			continue;
		}

		if (Scripts[i]->IsCompilable() && !Scripts[i]->AreScriptAndSourceSynchronized())
		{
			return true;
		}
	}
	return false;
}


FNiagaraScriptViewModel::~FNiagaraScriptViewModel()
{
	InputCollectionViewModel->GetSelection().OnSelectedObjectsChanged().RemoveAll(this);
	GraphViewModel->GetNodeSelection()->OnSelectedObjectsChanged().RemoveAll(this);

	if (Source.IsValid() && Source != nullptr)
	{
		UNiagaraGraph* Graph = Source->NodeGraph;
		if (Graph != nullptr)
		{
			Graph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
		}
	}

	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (Scripts[i].IsValid())
		{
			Scripts[i]->OnVMScriptCompiled().RemoveAll(this);
		}
	}

	GEditor->UnregisterForUndo(this);

	for (TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptViewModel>::Handle RegisteredHandle : RegisteredHandles)
	{
		UnregisterViewModelWithMap(RegisteredHandle);
	}
	//UE_LOG(LogNiagaraEditor, Warning, TEXT("Deleting script view model %p"), this);

}


void FNiagaraScriptViewModel::SetScripts(UNiagaraScriptSource* InScriptSource, TArray<UNiagaraScript*>& InScripts)
{
	// Remove the graph changed handler on the child.
	if (Source.IsValid())
	{
		UNiagaraGraph* Graph = Source->NodeGraph;
		if (Graph != nullptr)
		{
			Graph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
		}
	}
	else
	{
		Source = nullptr;
	}

	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (Scripts[i].IsValid())
		{
			Scripts[i]->OnVMScriptCompiled().RemoveAll(this);
		}
	}

	for (TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptViewModel>::Handle RegisteredHandle : RegisteredHandles)
	{
		UnregisterViewModelWithMap(RegisteredHandle);
	}
	RegisteredHandles.Empty();

	Scripts.Empty();
	for (UNiagaraScript* Script : InScripts)
	{
		int32 i = Scripts.Add(Script);
		check(Script->GetSource() == InScriptSource);
		Scripts[i]->OnVMScriptCompiled().AddSP(this, &FNiagaraScriptViewModel::OnVMScriptCompiled);
	}
	Source = InScriptSource;

	InputCollectionViewModel->SetScripts(InScripts);
	OutputCollectionViewModel->SetScripts(InScripts);
	GraphViewModel->SetScriptSource(Source.Get());

	// Guess at initial compile status
	LastCompileStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;

	CompileStatuses.Empty();
	CompileErrors.Empty();
	CompilePaths.Empty();

	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		FString Message;
		ENiagaraScriptCompileStatus ScriptStatus = Scripts[i]->GetLastCompileStatus();
		if (Scripts[i].IsValid() && Scripts[i]->IsCompilable() && Scripts[i]->GetVMExecutableData().IsValid() && Scripts[i]->GetVMExecutableData().ByteCode.Num() == 0) // This is either a brand new script or failed in the past. Since we create a default working script, assume invalid.
		{
			Message = TEXT("Please recompile for full error stack.");
			GraphViewModel->SetErrorTextToolTip(Message);
		}
		else // Possibly warnings previously, but still compiled. It *could* have been dirtied somehow, but we assume that it is up-to-date.
		{
			if (Scripts[i].IsValid() && Scripts[i]->IsCompilable() && Scripts[i]->AreScriptAndSourceSynchronized())
			{
				LastCompileStatus = FNiagaraEditorUtilities::UnionCompileStatus(LastCompileStatus, Scripts[i]->GetLastCompileStatus());
			}
			else if (Scripts[i]->IsCompilable())
			{
				//DO nothing
				ScriptStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;
			}
			else
			{
				LastCompileStatus = FNiagaraEditorUtilities::UnionCompileStatus(LastCompileStatus, ENiagaraScriptCompileStatus::NCS_Error);
				ScriptStatus = ENiagaraScriptCompileStatus::NCS_Error;
				Message = TEXT("Please recompile for full error stack.");
				GraphViewModel->SetErrorTextToolTip(Message);
			}
		}

		CompilePaths.Add(Scripts[i]->GetPathName());
		CompileErrors.Add(Message);
		CompileStatuses.Add(ScriptStatus);

		RegisteredHandles.Add(RegisterViewModelWithMap(Scripts[i].Get(), this));
	}

	CompileTypes.SetNum(CompileStatuses.Num());
	for (int32 i = 0; i < CompileStatuses.Num(); i++)
	{
		CompileTypes[i].Key = Scripts[i]->GetUsage();
		CompileTypes[i].Value = Scripts[i]->GetUsageId();
	}
}

void FNiagaraScriptViewModel::SetScripts(UNiagaraEmitter* InEmitter)
{
	if (InEmitter == nullptr)
	{
		TArray<UNiagaraScript*> EmptyScripts;
		SetScripts(nullptr, EmptyScripts);
	}
	else
	{
		TArray<UNiagaraScript*> InScripts;
		InEmitter->GetScripts(InScripts);
		SetScripts(Cast<UNiagaraScriptSource>(InEmitter->GraphSource), InScripts);
	}
}

void FNiagaraScriptViewModel::SetScript(UNiagaraScript* InScript)
{
	TArray<UNiagaraScript*> InScripts;
	UNiagaraScriptSource* InSource = nullptr;
	if (InScript)
	{
		InScripts.Add(InScript);
		InSource = Cast<UNiagaraScriptSource>(InScript->GetSource());
	}
	SetScripts(InSource, InScripts);
}

void FNiagaraScriptViewModel::MarkAllDirty(FString Reason)
{
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (Scripts[i].IsValid() && Scripts[i]->IsCompilable())
		{
			Scripts[i]->MarkScriptAndSourceDesynchronized(Reason);
		}
	}
}

TSharedRef<FNiagaraScriptInputCollectionViewModel> FNiagaraScriptViewModel::GetInputCollectionViewModel()
{
	return InputCollectionViewModel;
}


TSharedRef<FNiagaraScriptOutputCollectionViewModel> FNiagaraScriptViewModel::GetOutputCollectionViewModel()
{
	return OutputCollectionViewModel;
}

TSharedRef<FNiagaraScriptGraphViewModel> FNiagaraScriptViewModel::GetGraphViewModel()
{
	return GraphViewModel;
}

TSharedRef<FNiagaraObjectSelection> FNiagaraScriptViewModel::GetVariableSelection()
{
	return VariableSelection;
}

UNiagaraScript* FNiagaraScriptViewModel::GetStandaloneScript()
{
	if (Scripts.Num() == 1)
	{
		UNiagaraScript* ScriptOutput = Scripts[0].Get();
		if (ScriptOutput && ScriptOutput->IsStandaloneScript())
		{
			return ScriptOutput;
		}
	}
	return nullptr;
}


void FNiagaraScriptViewModel::UpdateCompileStatus(ENiagaraScriptCompileStatus InAggregateCompileStatus, const FString& InAggregateCompileErrorString,
	const TArray<ENiagaraScriptCompileStatus>& InCompileStatuses, const TArray<FString>& InCompileErrors, const TArray<FString>& InCompilePaths,
	const TArray<UNiagaraScript*>& InCompileSources)
{
	if (Source.IsValid() && Source != nullptr)
	{
		CompileStatuses = InCompileStatuses;
		CompileErrors = InCompileErrors;
		CompilePaths = InCompilePaths;

		CompileTypes.SetNum(CompileStatuses.Num());
		for (int32 i = 0; i < CompileStatuses.Num(); i++)
		{
			CompileTypes[i].Key = InCompileSources[i]->GetUsage();
			CompileTypes[i].Value = InCompileSources[i]->GetUsageId();
		}

		LastCompileStatus = InAggregateCompileStatus;
		
		if (LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error)
		{
			GraphViewModel->SetErrorTextToolTip(InAggregateCompileErrorString + "\n(These same errors are also in the log)");
		}
		else
		{
			GraphViewModel->SetErrorTextToolTip("");
		}

		InputCollectionViewModel->RefreshParameterViewModels();
		OutputCollectionViewModel->RefreshParameterViewModels();
	}
}

ENiagaraScriptCompileStatus FNiagaraScriptViewModel::GetScriptCompileStatus(ENiagaraScriptUsage InUsage, FGuid InUsageId) const
{
	ENiagaraScriptCompileStatus Status = ENiagaraScriptCompileStatus::NCS_Unknown;
	for (int32 i = 0; i < CompileTypes.Num(); i++)
	{
		if (UNiagaraScript::IsEquivalentUsage(CompileTypes[i].Key, InUsage) && CompileTypes[i].Value == InUsageId)
		{
			return CompileStatuses[i];
		}
	}
	return Status;
}

FText FNiagaraScriptViewModel::GetScriptErrors(ENiagaraScriptUsage InUsage, FGuid InUsageId) const
{
	FText Text = FText::GetEmpty();
	for (int32 i = 0; i < CompileTypes.Num(); i++)
	{
		if (UNiagaraScript::IsEquivalentUsage(CompileTypes[i].Key, InUsage) && CompileTypes[i].Value == InUsageId)
		{
			return FText::FromString(CompileErrors[i]);
		}
	}
	return Text;
}

UNiagaraScript* FNiagaraScriptViewModel::GetContainerScript(ENiagaraScriptUsage InUsage, FGuid InUsageId) 
{
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (Scripts[i]->ContainsUsage(InUsage) && Scripts[i]->GetUsageId() == InUsageId)
		{
			return Scripts[i].Get();
		}
	}
	return nullptr;
}

UNiagaraScript* FNiagaraScriptViewModel::GetScript(ENiagaraScriptUsage InUsage, FGuid InUsageId) 
{
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		if (Scripts[i]->IsEquivalentUsage(InUsage) && Scripts[i]->GetUsageId() == InUsageId)
		{
			return Scripts[i].Get();
		}
	}
	return nullptr;
}

void FNiagaraScriptViewModel::CompileStandaloneScript(bool bForceCompile)
{
	if (Source.IsValid() && Source != nullptr && Scripts.Num() == 1 && Scripts[0].IsValid ())
	{
		if (Scripts[0]->IsStandaloneScript() && Scripts[0]->IsCompilable())
		{
			Scripts[0]->RequestCompile(bForceCompile);
		}
		else if (!Scripts[0]->IsCompilable())
		{
			// do nothing
		}
		else
		{
			ensure(0); // Should not call this for a script that isn't standalone.
		}
	}
	else
	{
		ensure(0); // Should not call this for a script that isn't standalone.
	}
}


ENiagaraScriptCompileStatus FNiagaraScriptViewModel::GetLatestCompileStatus()
{
	if (GraphViewModel->GetGraph() && IsGraphDirty())
		return ENiagaraScriptCompileStatus::NCS_Dirty;
	return LastCompileStatus;
}

void FNiagaraScriptViewModel::RefreshNodes()
{
	if (GraphViewModel->GetGraph())
	{
		TArray<UNiagaraNode*> NiagaraNodes;
		GraphViewModel->GetGraph()->GetNodesOfClass<UNiagaraNode>(NiagaraNodes);
		for (UNiagaraNode* NiagaraNode : NiagaraNodes)
		{
			if (NiagaraNode->RefreshFromExternalChanges())
			{
				for (int32 i = 0; i < Scripts.Num(); i++)
				{
					if (Scripts[i].IsValid() && Scripts[i]->IsCompilable())
					{
						Scripts[i]->MarkScriptAndSourceDesynchronized(TEXT("Nodes manually refreshed"));
					}
				}
			}
		}
	}
}

void FNiagaraScriptViewModel::PostUndo(bool bSuccess)
{
	InputCollectionViewModel->RefreshParameterViewModels();
	OutputCollectionViewModel->RefreshParameterViewModels();
}

void FNiagaraScriptViewModel::GraphViewModelSelectedNodesChanged()
{
	if (bUpdatingSelectionInternally == false)
	{
		bUpdatingSelectionInternally = true;
		{
			TSet<FName> SelectedInputIds;
			for (UObject* SelectedObject : GraphViewModel->GetNodeSelection()->GetSelectedObjects())
			{
				UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(SelectedObject);
				if (InputNode != nullptr)
				{
					SelectedInputIds.Add(InputNode->Input.GetName());
				}
			}

			TSet<TSharedRef<INiagaraParameterViewModel>> ParametersToSelect;
			for (TSharedRef<INiagaraParameterViewModel> Parameter : InputCollectionViewModel->GetParameters())
			{
				if (SelectedInputIds.Contains(Parameter->GetName()))
				{
					ParametersToSelect.Add(Parameter);
				}
			}

			InputCollectionViewModel->GetSelection().SetSelectedObjects(ParametersToSelect);
		}
		bUpdatingSelectionInternally = false;
	}
}

void FNiagaraScriptViewModel::InputViewModelSelectionChanged()
{
	if (bUpdatingSelectionInternally == false)
	{
		bUpdatingSelectionInternally = true;
		{
			TSet<FName> SelectedInputIds;
			for (TSharedRef<INiagaraParameterViewModel> SelectedParameter : InputCollectionViewModel->GetSelection().GetSelectedObjects())
			{
				SelectedInputIds.Add(SelectedParameter->GetName());
			}

			TArray<UNiagaraNodeInput*> InputNodes;
			if (GraphViewModel->GetGraph())
			{
				GraphViewModel->GetGraph()->GetNodesOfClass(InputNodes);
			}
			TSet<UObject*> NodesToSelect;
			for (UNiagaraNodeInput* InputNode : InputNodes)
			{
				if (SelectedInputIds.Contains(InputNode->Input.GetName()))
				{
					NodesToSelect.Add(InputNode);
				}
			}

			GraphViewModel->GetNodeSelection()->SetSelectedObjects(NodesToSelect);
		}
		bUpdatingSelectionInternally = false;
	}
}

void FNiagaraScriptViewModel::InputParameterValueChanged(FName ParameterName)
{
	MarkAllDirty(TEXT("Input parameter value changed"));
}

void FNiagaraScriptViewModel::OutputParameterValueChanged(FName ParameterName)
{
	MarkAllDirty(TEXT("Output parameter value changed"));
}
