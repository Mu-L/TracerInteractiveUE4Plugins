// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvironmentQuery/EnvQueryGenerator.h"

UEnvQueryGenerator::UEnvQueryGenerator(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bAutoSortTests = true;
}

void UEnvQueryGenerator::UpdateNodeVersion()
{
	VerNum = EnvQueryGeneratorVersion::Latest;
}

void UEnvQueryGenerator::PostLoad()
{
	Super::PostLoad();
	UpdateNodeVersion();
}
