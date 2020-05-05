// Copyright Epic Games, Inc. All Rights Reserved.

#include "Perception/AISenseConfig_Damage.h"

UAISenseConfig_Damage::UAISenseConfig_Damage(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) 
{
	DebugColor = FColor::Red;
}

TSubclassOf<UAISense> UAISenseConfig_Damage::GetSenseImplementation() const
{
	return Implementation;
}
