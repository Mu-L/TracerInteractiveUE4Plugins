// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvironmentQuery/EnvQueryContext.h"

UEnvQueryContext::UEnvQueryContext(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void UEnvQueryContext::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	// empty in base class
}
