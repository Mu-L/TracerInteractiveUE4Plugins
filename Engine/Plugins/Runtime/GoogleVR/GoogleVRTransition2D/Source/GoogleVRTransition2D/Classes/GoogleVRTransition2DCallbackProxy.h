// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h" 
#include "UObject/ScriptMacros.h"
#include "Delegates/Delegate.h"
#include "GoogleVRTransition2DCallbackProxy.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGoogleVRTransition2DDelegate);

UCLASS()
class UGoogleVRTransition2DCallbackProxy : public UObject
{
	GENERATED_BODY()
public:
	// delegate to handle the completion of the 2D transition
	UPROPERTY(BLueprintAssignable, Category="Google VR")
	FGoogleVRTransition2DDelegate OnTransitionTo2D;

public:
	static UGoogleVRTransition2DCallbackProxy *GetInstance();
};
