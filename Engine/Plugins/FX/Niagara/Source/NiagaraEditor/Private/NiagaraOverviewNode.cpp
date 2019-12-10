// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraOverviewNode.h"
#include "NiagaraSystem.h"
#include "NiagaraEditorModule.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewNodeStackItem"

bool UNiagaraOverviewNode::bColorsAreInitialized = false;
FLinearColor UNiagaraOverviewNode::EmitterColor;
FLinearColor UNiagaraOverviewNode::SystemColor;

UNiagaraOverviewNode::UNiagaraOverviewNode()
	: OwningSystem(nullptr)
	, EmitterHandleGuid(FGuid())
{
};

void UNiagaraOverviewNode::Initialize(UNiagaraSystem* InOwningSystem)
{
	OwningSystem = InOwningSystem;
}

void UNiagaraOverviewNode::Initialize(UNiagaraSystem* InOwningSystem, FGuid InEmitterHandleGuid)
{
	OwningSystem = InOwningSystem;
	EmitterHandleGuid = InEmitterHandleGuid;
}

const FGuid UNiagaraOverviewNode::GetEmitterHandleGuid() const
{
	return EmitterHandleGuid;
}

FText UNiagaraOverviewNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (OwningSystem == nullptr)
	{
		return FText::GetEmpty();
	}

	if (EmitterHandleGuid.IsValid())
	{
		for (const FNiagaraEmitterHandle& Handle : OwningSystem->GetEmitterHandles())
		{
			if (Handle.GetId() == EmitterHandleGuid)
			{
				return FText::FromName(Handle.GetName());
			}
		}
		ensureMsgf(false, TEXT("Failed to find matching emitter handle for existing overview node!"));
		return LOCTEXT("UnknownEmitterName", "Unknown Emitter");
	}
	else
	{
		return FText::FromString(OwningSystem->GetName());
	}
}

FLinearColor UNiagaraOverviewNode::GetNodeTitleColor() const
{
	if (bColorsAreInitialized == false)
	{
		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
		EmitterColor = NiagaraEditorModule.GetWidgetProvider()->GetColorForExecutionCategory(UNiagaraStackEntry::FExecutionCategoryNames::Emitter);
		SystemColor = NiagaraEditorModule.GetWidgetProvider()->GetColorForExecutionCategory(UNiagaraStackEntry::FExecutionCategoryNames::System);
	}
	return EmitterHandleGuid.IsValid() ? EmitterColor : SystemColor;
}

bool UNiagaraOverviewNode::CanUserDeleteNode() const
{
	return EmitterHandleGuid.IsValid();
}

bool UNiagaraOverviewNode::CanDuplicateNode() const
{
	// The class object must return true for can duplicate otherwise the CanImportNodesFromText utility function fails.
	return HasAnyFlags(RF_ClassDefaultObject) || EmitterHandleGuid.IsValid();
}

UNiagaraSystem* UNiagaraOverviewNode::GetOwningSystem()
{
	return OwningSystem;
}

#undef LOCTEXT_NAMESPACE
