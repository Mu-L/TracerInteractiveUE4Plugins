// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProceduralFoliageBlockingVolume.h"
#include "Components/BrushComponent.h"

static FName ProceduralFoliageBlocking_NAME(TEXT("ProceduralFoliageBlockingVolume"));

AProceduralFoliageBlockingVolume::AProceduralFoliageBlockingVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (UBrushComponent* MyBrushComponent = GetBrushComponent())
	{
		MyBrushComponent->SetCollisionObjectType(ECC_WorldStatic);
		MyBrushComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
}
