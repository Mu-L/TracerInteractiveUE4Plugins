// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"



UMultiClickSequenceInputBehavior::UMultiClickSequenceInputBehavior()
{
	bInActiveSequence = false;
}


void UMultiClickSequenceInputBehavior::Initialize(IClickSequenceBehaviorTarget* TargetIn)
{
	this->Target = TargetIn;
	bInActiveSequence = false;
}


FInputCaptureRequest UMultiClickSequenceInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	check(bInActiveSequence == false);   // should not happen...
	bInActiveSequence = false;

	if ( IsPressed(input) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(input) ) )
	{
		if ( Target->CanBeginClickSequence(GetDeviceRay(input)) )
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any);
		}
	}
	return FInputCaptureRequest::Ignore();
}


FInputCaptureUpdate UMultiClickSequenceInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	Modifiers.UpdateModifiers(input, Target);

	Target->OnBeginClickSequence(GetDeviceRay(input));
	bInActiveSequence = true;
	
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}


FInputCaptureUpdate UMultiClickSequenceInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	check(bInActiveSequence == true);   // should always be the case!

	// This is a hack to avoid terminating multi-click sequences if the user does alt+mouse camera navigation.
	// This entire class should be deprecated and removed, in which case this hack won't be relevant...
	if (input.bAltKeyDown)
	{
		return FInputCaptureUpdate::Continue();
	}

	Modifiers.UpdateModifiers(input, Target);

	// allow target to abort click sequence
	if (Target->RequestAbortClickSequence())
	{
		Target->OnTerminateClickSequence();
		bInActiveSequence = false;
		return FInputCaptureUpdate::End();
	}

	if (IsReleased(input)) 
	{
		bool bContinue = Target->OnNextSequenceClick(GetDeviceRay(input));
		if (bContinue == false)
		{
			bInActiveSequence = false;
			return FInputCaptureUpdate::End();
		}
	}
	else
	{
		Target->OnNextSequencePreview(GetDeviceRay(input));
	}

	return FInputCaptureUpdate::Continue();
}


void UMultiClickSequenceInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	Target->OnTerminateClickSequence();
	bInActiveSequence = false;
}


bool UMultiClickSequenceInputBehavior::WantsHoverEvents()
{
	return true;
}


FInputCaptureRequest UMultiClickSequenceInputBehavior::WantsHoverCapture(const FInputDeviceState& InputState)
{
	return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UMultiClickSequenceInputBehavior::BeginHoverCapture(const FInputDeviceState& InputState, EInputCaptureSide eSide) 
{
	if (Target != nullptr)
	{
		Modifiers.UpdateModifiers(InputState, Target);
		Target->OnBeginSequencePreview(FInputDeviceRay(InputState.Mouse.WorldRay, InputState.Mouse.Position2D));
		return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
	}
	return FInputCaptureUpdate::Ignore();
}

FInputCaptureUpdate UMultiClickSequenceInputBehavior::UpdateHoverCapture(const FInputDeviceState& InputState)
{
	if (Target != nullptr)
	{
		Modifiers.UpdateModifiers(InputState, Target);
		Target->OnBeginSequencePreview(FInputDeviceRay(InputState.Mouse.WorldRay, InputState.Mouse.Position2D));
		return FInputCaptureUpdate::Continue();
	}
	return FInputCaptureUpdate::End();
}


void UMultiClickSequenceInputBehavior::EndHoverCapture()
{
	// end
}
