// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputActionValue.h"
#include "GameFramework/PlayerInput.h"

/* UEnhancedInputComponent interface
 *****************************************************************************/

UEnhancedInputComponent::UEnhancedInputComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Priority = -1;
	bBlockInput = false;
}

bool UEnhancedInputComponent::HasBindings( ) const
{
	return GetActionEventBindings().Num() > 0 || GetActionValueBindings().Num() > 0 || GetDebugKeyBindings().Num() > 0 || Super::HasBindings();
}

template<typename T>
bool RemoveBindingByIndex(TArray<T>& Bindings, const int32 BindingIndex)
{
	if (BindingIndex >= 0 && BindingIndex < Bindings.Num())
	{
		Bindings.RemoveAt(BindingIndex, 1, false);
		return true;
	}
	return false;
}

bool UEnhancedInputComponent::RemoveActionEventBinding(const int32 BindingIndex)
{
	return RemoveBindingByIndex(EnhancedActionEventBindings, BindingIndex);
}

bool UEnhancedInputComponent::RemoveActionValueBinding(const int32 BindingIndex)
{
	return RemoveBindingByIndex(EnhancedActionValueBindings, BindingIndex);
}

bool UEnhancedInputComponent::RemoveDebugKeyBinding(const int32 BindingIndex)
{
	return RemoveBindingByIndex(DebugKeyBindings, BindingIndex);
}

bool UEnhancedInputComponent::RemoveBindingByHandle(const uint32 Handle)
{
	auto TryRemove = [](auto& Bindings, auto Predicate)
	{
		int32 EventFoundIndex = Bindings.IndexOfByPredicate(Predicate);
		return EventFoundIndex != INDEX_NONE && RemoveBindingByIndex(Bindings, EventFoundIndex);
	};

	// TODO: Searching three separate arrays is slow. Store a map of handles -> type? (and index?)
	return	TryRemove(EnhancedActionEventBindings, [Handle](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding) { return Binding->GetHandle() == Handle; }) ||
			TryRemove(EnhancedActionValueBindings, [Handle](const FEnhancedInputActionValueBinding& Binding) { return Binding.GetHandle() == Handle; }) ||
			TryRemove(DebugKeyBindings, [Handle](const TUniquePtr<FInputDebugKeyBinding>& Binding) { return Binding->GetHandle() == Handle; });
}

bool UEnhancedInputComponent::RemoveBinding(const FInputBindingHandle& BindingToRemove)
{
	return RemoveBindingByHandle(BindingToRemove.GetHandle());
}

FInputActionValue UEnhancedInputComponent::GetBoundActionValue(const UInputAction* Action) const
{
	for (const FEnhancedInputActionValueBinding& Binding : GetActionValueBindings())
	{
		if (Binding.GetAction() == Action)
		{
			return Binding.GetValue();
		}
	}
	return FInputActionValue(Action->ValueType, FVector::ZeroVector);
}


FInputBindingHandle::FInputBindingHandle()
{
	// Handles are shared between all binding types
	// Must be in C++ to avoid duplicate statics across execution units
	static uint32 GHandle = 1;
	Handle = GHandle++;
}