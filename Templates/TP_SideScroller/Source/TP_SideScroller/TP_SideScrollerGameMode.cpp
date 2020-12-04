// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_SideScrollerGameMode.h"
#include "TP_SideScrollerCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATP_SideScrollerGameMode::ATP_SideScrollerGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/SideScrollerCPP/Blueprints/SideScrollerCharacter"));
	if (PlayerPawnBPClass.Class != nullptr)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
