// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"

UNavArea_Null::UNavArea_Null(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = FLT_MAX;
	AreaFlags = 0;
}
