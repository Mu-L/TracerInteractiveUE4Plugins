// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

//=============================================================================
// USoundConcurrencyFactory
//=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "SoundConcurrencyFactory.generated.h"

UCLASS(hidecategories=Object, MinimalAPI)
class USoundConcurrencyFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn) override;
	// Begin UFactory Interface	
};



