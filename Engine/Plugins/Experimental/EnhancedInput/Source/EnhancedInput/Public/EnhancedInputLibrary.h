// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingQuery.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"

#include "EnhancedInputLibrary.generated.h"

class APlayerController;
class UInputMappingContext;
class UInputAction;
class UEnhancedPlayerInput;

UCLASS()
class ENHANCEDINPUT_API UEnhancedInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Call SubsystemPredicate on each registered player and standalone enhanced input subsystem.
	 */
	static void ForEachSubsystem(TFunctionRef<void(IEnhancedInputSubsystemInterface*)> SubsystemPredicate);

	/**
	 * Flag all enhanced input subsystems making use of the mapping context for reapplication of all control mappings at the end of this frame.
	 * @param Context				Mappings will be rebuilt for all subsystems utilizing this context.
	 * @param bForceImmediately		The mapping changes will be applied synchronously, rather than at the end of the frame, making them available to the input system on the same frame.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input")
	static void RequestRebuildControlMappingsUsingContext(const UInputMappingContext* Context, bool bForceImmediately = false);


	// Internal helper functionality

	// GetInputActionvalue internal accessor function for actions that have been bound to from a UEnhancedInputComponent
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta = (HidePin = "Action"))
	static FInputActionValue GetBoundActionValue(AActor* Actor, const UInputAction* Action);

	// FInputActionValue internal auto-converters.

	/** Interpret an InputActionValue as a boolean input */
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta = (BlueprintAutocast))
	static bool Conv_InputActionValueToBool(FInputActionValue InValue);

	/** Interpret an InputActionValue as a 1D axis (float) input */
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta = (BlueprintAutocast))
	static float Conv_InputActionValueToAxis1D(FInputActionValue InValue);

	/** Interpret an InputActionValue as a 2D axis (Vector2D) input */
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta = (BlueprintAutocast))
	static FVector2D Conv_InputActionValueToAxis2D(FInputActionValue InValue);

	/** Interpret an InputActionValue as a 3D axis (Vector) input */
	UFUNCTION(BlueprintPure, BlueprintInternalUseOnly, meta = (BlueprintAutocast))
	static FVector Conv_InputActionValueToAxis3D(FInputActionValue InValue);

};
