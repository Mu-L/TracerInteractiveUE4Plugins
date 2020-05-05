// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRig_ExternalSource.h"
#include "ControlRig.h"

FAnimNode_ControlRig_ExternalSource::FAnimNode_ControlRig_ExternalSource()
{
}

void FAnimNode_ControlRig_ExternalSource::SetControlRig(UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	// requires initializing animation system
}

UControlRig* FAnimNode_ControlRig_ExternalSource::GetControlRig() const
{
	return (ControlRig.IsValid()? ControlRig.Get() : nullptr);
}

