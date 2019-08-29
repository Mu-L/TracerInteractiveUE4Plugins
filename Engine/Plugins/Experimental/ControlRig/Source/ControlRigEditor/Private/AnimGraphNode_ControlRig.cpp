// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ControlRig.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_ControlRig"

UAnimGraphNode_ControlRig::UAnimGraphNode_ControlRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_ControlRig::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNode_ControlRig_Title", "Control Rig");
}

FText UAnimGraphNode_ControlRig::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_ControlRig_Tooltip", "Evaluates a control rig");
}

#undef LOCTEXT_NAMESPACE
