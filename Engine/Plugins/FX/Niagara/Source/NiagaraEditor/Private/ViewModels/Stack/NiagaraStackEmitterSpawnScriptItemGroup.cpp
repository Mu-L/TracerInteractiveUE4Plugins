// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackEmitterSpawnScriptItemGroup.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackSpacer.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitter.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitterDetailsCustomization.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackScriptItemGroup"

void UNiagaraStackEmitterPropertiesItem::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, TEXT("EmitterProperties"));
	Emitter = GetEmitterViewModel()->GetEmitter();
	Emitter->OnPropertiesChanged().AddUObject(this, &UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged);
}

void UNiagaraStackEmitterPropertiesItem::FinalizeInternal()
{
	if (Emitter.IsValid())
	{
		Emitter->OnPropertiesChanged().RemoveAll(this);
	}
	Super::FinalizeInternal();
}

FText UNiagaraStackEmitterPropertiesItem::GetDisplayName() const
{
	return LOCTEXT("EmitterPropertiesDisplayName", "Emitter Properties");
}

bool UNiagaraStackEmitterPropertiesItem::CanResetToBase() const
{
	if (bCanResetToBaseCache.IsSet() == false)
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		if (BaseEmitter != nullptr && Emitter != BaseEmitter)
		{
			TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
			bCanResetToBaseCache = MergeManager->IsEmitterEditablePropertySetDifferentFromBase(*Emitter.Get(), *BaseEmitter);
		}
		else
		{
			bCanResetToBaseCache = false;
		}
	}
	return bCanResetToBaseCache.GetValue();
}

void UNiagaraStackEmitterPropertiesItem::ResetToBase()
{
	if (CanResetToBase())
	{
		const UNiagaraEmitter* BaseEmitter = GetEmitterViewModel()->GetEmitter()->GetParent();
		TSharedRef<FNiagaraScriptMergeManager> MergeManager = FNiagaraScriptMergeManager::Get();
		MergeManager->ResetEmitterEditablePropertySetToBase(*Emitter, *BaseEmitter);
	}
}

void UNiagaraStackEmitterPropertiesItem::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	if (EmitterObject == nullptr)
	{
		EmitterObject = NewObject<UNiagaraStackObject>(this);
		FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(), FExecutionCategoryNames::Emitter, NAME_None, GetStackEditorData());
		EmitterObject->Initialize(RequiredEntryData, Emitter.Get(), GetStackEditorDataKey());
		EmitterObject->RegisterInstancedCustomPropertyLayout(UNiagaraEmitter::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraEmitterDetails::MakeInstance));
	}

	NewChildren.Add(EmitterObject);
	bCanResetToBaseCache.Reset();
	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackEmitterPropertiesItem::EmitterPropertiesChanged()
{
	bCanResetToBaseCache.Reset();
}

UNiagaraStackEmitterSpawnScriptItemGroup::UNiagaraStackEmitterSpawnScriptItemGroup()
	: PropertiesItem(nullptr)
{
}

void UNiagaraStackEmitterSpawnScriptItemGroup::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	FName PropertiesSpacerKey = "PropertiesSpacer";
	UNiagaraStackSpacer* PropertiesSpacer = FindCurrentChildOfTypeByPredicate<UNiagaraStackSpacer>(CurrentChildren,
		[=](UNiagaraStackSpacer* CurrentPropertiesSpacer) { return CurrentPropertiesSpacer->GetSpacerKey() == PropertiesSpacerKey; });

	if (PropertiesSpacer == nullptr)
	{
		PropertiesSpacer = NewObject<UNiagaraStackSpacer>(this);
		PropertiesSpacer->Initialize(CreateDefaultChildRequiredData(), PropertiesSpacerKey);
	}

	NewChildren.Add(PropertiesSpacer);

	if (PropertiesItem == nullptr)
	{
		PropertiesItem = NewObject<UNiagaraStackEmitterPropertiesItem>(this);
		PropertiesItem->Initialize(CreateDefaultChildRequiredData());
	}

	NewChildren.Add(PropertiesItem);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

#undef LOCTEXT_NAMESPACE
