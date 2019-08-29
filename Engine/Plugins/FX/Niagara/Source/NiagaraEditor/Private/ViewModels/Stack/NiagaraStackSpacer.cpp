// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSpacer.h"
#include "NiagaraActions.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

void UNiagaraStackSpacer::Initialize(FRequiredEntryData InRequiredEntryData, FName InSpacerKey, float InSpacerScale, EStackRowStyle InRowStyle)
{
	Super::Initialize(InRequiredEntryData, FString());
	SpacerKey = InSpacerKey;
	SpacerScale = InSpacerScale;
	RowStyle = InRowStyle;
}

FText UNiagaraStackSpacer::GetDisplayName() const
{
	return FText();
}

bool UNiagaraStackSpacer::GetCanExpand() const
{
	return false;
}

UNiagaraStackEntry::EStackRowStyle UNiagaraStackSpacer::GetStackRowStyle() const
{
	return RowStyle;
}

FName UNiagaraStackSpacer::GetSpacerKey() const
{
	return SpacerKey;
}

float UNiagaraStackSpacer::GetSpacerScale() const
{
	return SpacerScale;
}

FReply UNiagaraStackSpacer::OnStackSpacerDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	return FReply::Unhandled(); 
}

bool UNiagaraStackSpacer::OnStackSpacerAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	return false;
}
