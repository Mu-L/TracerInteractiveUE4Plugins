// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridPathAIController.h"
#include "Navigation/GridPathFollowingComponent.h"

AGridPathAIController::AGridPathAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGridPathFollowingComponent>(TEXT("PathFollowingComponent")))
{

}
