// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshSolverUtilitiesModule.h"

#define LOCTEXT_NAMESPACE "FMeshSolverUtilitiesModule"

void FMeshSolverUtilitiesModule::StartupModule()
{
}

void FMeshSolverUtilitiesModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMeshSolverUtilitiesModule, MeshSolverUtilities)