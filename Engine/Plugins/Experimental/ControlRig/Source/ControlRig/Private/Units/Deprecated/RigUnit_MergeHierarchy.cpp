// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_MergeHierarchy.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MergeHierarchy::Execute(const FRigUnitContext& Context)
{
	// merge input hierarchy to base
	TargetHierarchy.MergeHierarchy(SourceHierarchy);
}
