// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOS/IOSInputInterface.h"
#include "IOS/IOSAppDelegate.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/EmbeddedCommunication.h"

#import <AudioToolbox/AudioToolbox.h>

DECLARE_LOG_CATEGORY_EXTERN(LogIOSInput, Log, All);


static TAutoConsoleVariable<float> CVarHapticsKickHeavy(TEXT("ios.VibrationHapticsKickHeavyValue"), 0.65f, TEXT("Vibation values higher than this will kick a haptics heavy Impact"));
static TAutoConsoleVariable<float> CVarHapticsKickMedium(TEXT("ios.VibrationHapticsKickMediumValue"), 0.5f, TEXT("Vibation values higher than this will kick a haptics medium Impact"));
static TAutoConsoleVariable<float> CVarHapticsKickLight(TEXT("ios.VibrationHapticsKickLightValue"), 0.3f, TEXT("Vibation values higher than this will kick a haptics light Impact"));
static TAutoConsoleVariable<float> CVarHapticsRest(TEXT("ios.VibrationHapticsRestValue"), 0.2f, TEXT("Vibation values lower than this will allow haptics to Kick again when going over ios.VibrationHapticsKickValue"));

//@interface FControllerHelper : NSObject
//{
//	FIOSInputInterface
//}
//
//@end

TArray<TouchInput> FIOSInputInterface::TouchInputStack = TArray<TouchInput>();
TArray<int32> FIOSInputInterface::KeyInputStack;
FCriticalSection FIOSInputInterface::CriticalSection;

TSharedRef< FIOSInputInterface > FIOSInputInterface::Create(  const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	return MakeShareable( new FIOSInputInterface( InMessageHandler ) );
}

FIOSInputInterface::FIOSInputInterface( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
	: MessageHandler( InMessageHandler )
	, bAllowRemoteRotation(false)
	, bTreatRemoteAsSeparateController(false)
	, bUseRemoteAsVirtualJoystick(true)
	, bUseRemoteAbsoluteDpadValues(false)
	, bAllowControllers(true)
    , LastHapticValue(0.0f)
{
	SCOPED_BOOT_TIMING("FIOSInputInterface::FIOSInputInterface");

#if !PLATFORM_TVOS
	MotionManager = nil;
	ReferenceAttitude = nil;
#endif
	bPauseMotion = false;
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bDisableMotionData"), bPauseMotion, GEngineIni);
	
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bTreatRemoteAsSeparateController"), bTreatRemoteAsSeparateController, GEngineIni);
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bAllowRemoteRotation"), bAllowRemoteRotation, GEngineIni);
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bUseRemoteAsVirtualJoystick"), bUseRemoteAsVirtualJoystick, GEngineIni);
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bUseRemoteAbsoluteDpadValues"), bUseRemoteAbsoluteDpadValues, GEngineIni);
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bAllowControllers"), bAllowControllers, GEngineIni);
	
	[[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification object:nil queue:[NSOperationQueue currentQueue] usingBlock:^(NSNotification* Notification)
	 {
		HandleConnection(Notification.object);
	 }];

	[[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidDisconnectNotification object:nil queue:[NSOperationQueue currentQueue] usingBlock:^(NSNotification* Notification)
	 {
		HandleDisconnect(Notification.object);
	 }];
	

	dispatch_async(dispatch_get_main_queue(), ^
	   {
		   [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{ }];
	   });
	
	FMemory::Memzero(Controllers, sizeof(Controllers));
	
	FEmbeddedDelegates::GetNativeToEmbeddedParamsDelegateForSubsystem(TEXT("iosinput")).AddLambda([this](const FEmbeddedCallParamsHelper& Message)
	{
		FString Error;
#if !PLATFORM_TVOS
		
		// execute any console commands
		if (Message.Command == TEXT("stopmotion"))
		{
			[MotionManager release];
			MotionManager = nil;
			
			bPauseMotion = true;
		}
		else if (Message.Command == TEXT("startmotion"))
		{
			bPauseMotion = false;
		}
		else
#endif
		{
			Error = TEXT("Unknown iosinput command ") + Message.Command;
		}
		
		Message.OnCompleteDelegate({}, Error);
	});

	
#if !PLATFORM_TVOS
	HapticFeedbackSupportLevel = [[[UIDevice currentDevice] valueForKey:@"_feedbackSupportLevel"] intValue];
#else
	HapticFeedbackSupportLevel = 0;
#endif
}

void FIOSInputInterface::SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	MessageHandler = InMessageHandler;
}

void FIOSInputInterface::Tick( float DeltaTime )
{

}

void FIOSInputInterface::HandleConnection(GCController* Controller)
{
	static_assert(GCControllerPlayerIndex1 == 0 && GCControllerPlayerIndex4 == 3, "Apple changed the player index enums");

	// is this guy a gamepad (i.e., not the Remote)
	bool bIsGamepadType = (Controller.extendedGamepad != nil);
	// if we want to use the Remote as a separate player, then we treat it as a Gamepad for player assignment
	bool bIsTreatedAsGamepad = bIsGamepadType || bTreatRemoteAsSeparateController;

	// disallow gamepad types (but still connect remote)
	if (bIsGamepadType && !bAllowControllers)
	{
		return;
	}

	// find a good controller index to use
	bool bFoundSlot = false;
	for (int32 ControllerIndex = 0; ControllerIndex < ARRAY_COUNT(Controllers); ControllerIndex++)
	{
		// is this one already connected for this type of controller?
		if ((!bIsTreatedAsGamepad && Controllers[ControllerIndex].bIsRemoteConnected == false) ||
			( bIsTreatedAsGamepad && Controllers[ControllerIndex].bIsGamepadConnected == false))
		{
			Controller.playerIndex = (GCControllerPlayerIndex)ControllerIndex;
#if PLATFORM_TVOS
			if (Controller.microGamepad != nil)
			{
				Controller.microGamepad.allowsRotation = bAllowRemoteRotation;
				Controller.microGamepad.reportsAbsoluteDpadValues = bUseRemoteAbsoluteDpadValues;
			}
#endif
			
			// update the appropriate flag
			if (bIsTreatedAsGamepad)
			{
				Controllers[ControllerIndex].bIsGamepadConnected = true;
			}
			else
			{
				Controllers[ControllerIndex].bIsRemoteConnected = true;
			}
			
			Controllers[ControllerIndex].bPauseWasPressed = false;
			Controller.controllerPausedHandler = ^(GCController* Cont)
			{
				Controllers[ControllerIndex].bPauseWasPressed = true;
			};
			
			
			bFoundSlot = true;

			UE_LOG(LogIOS, Log, TEXT("New %s controller inserted, assigned to playerIndex %d"), bIsTreatedAsGamepad ? TEXT("Gamepad") : TEXT("Remote"), Controller.playerIndex);
			break;
		}
	}
	checkf(bFoundSlot, TEXT("Used a fifth controller somehow!"));
	
}

void FIOSInputInterface::HandleDisconnect(GCController* Controller)
{
	// if we don't allow controllers, there could be unset player index here
	if (Controller.playerIndex == GCControllerPlayerIndexUnset)
	{
		return;
	}
	
	UE_LOG(LogIOS, Log, TEXT("Controller for playerIndex %d was removed"), Controller.playerIndex);
	
	// mark this controller as disconnected, and reset the state
	FMemory::Memzero(&Controllers[Controller.playerIndex], sizeof(Controllers[Controller.playerIndex]));
}


#if !PLATFORM_TVOS
void ModifyVectorByOrientation(FVector& Vec, bool bIsRotation)
{
    UIInterfaceOrientation Orientation = [[UIApplication sharedApplication] statusBarOrientation];

	switch (Orientation)
	{
	case UIInterfaceOrientationPortrait:
		// this is the base orientation, so nothing to do
		break;

	case UIInterfaceOrientationPortraitUpsideDown:
		if (bIsRotation)
		{
			// negate roll and pitch
			Vec.X = -Vec.X;
			Vec.Z = -Vec.Z;
		}
		else
		{
			// negate x/y
			Vec.X = -Vec.X;
			Vec.Y = -Vec.Y;
		}
		break;

	case UIInterfaceOrientationLandscapeRight:
		if (bIsRotation)
		{
			// swap and negate (as needed) roll and pitch
			float Temp = Vec.X;
			Vec.X = -Vec.Z;
			Vec.Z = Temp;
			Vec.Y *= -1.0f;
		}
		else
		{
			// swap and negate (as needed) x and y
			float Temp = Vec.X;
			Vec.X = -Vec.Y;
			Vec.Y = Temp;
		}
		break;

	case UIInterfaceOrientationLandscapeLeft:
		if (bIsRotation)
		{
			// swap and negate (as needed) roll and pitch
			float Temp = Vec.X;
			Vec.X = -Vec.Z;
			Vec.Z = -Temp;
		}
		else
		{
			// swap and negate (as needed) x and y
			float Temp = Vec.X;
			Vec.X = Vec.Y;
			Vec.Y = -Temp;
		}
		break;
	}
}
#endif

void FIOSInputInterface::ProcessTouchesAndKeys(uint32 ControllerId)
{
	for(int i = 0; i < TouchInputStack.Num(); ++i)
	{
		const TouchInput& Touch = TouchInputStack[i];
		
		// send input to handler
		if (Touch.Type == TouchBegan)
		{
			MessageHandler->OnTouchStarted( NULL, Touch.Position, Touch.Force, Touch.Handle, ControllerId);
		}
		else if (Touch.Type == TouchEnded)
		{
			MessageHandler->OnTouchEnded(Touch.Position, Touch.Handle, ControllerId);
		}
		else if (Touch.Type == TouchMoved)
		{
			MessageHandler->OnTouchMoved(Touch.Position, Touch.Force, Touch.Handle, ControllerId);
		}
		else if (Touch.Type == ForceChanged)
		{
			MessageHandler->OnTouchForceChanged(Touch.Position, Touch.Force, Touch.Handle, ControllerId);
		}
		else if (Touch.Type == FirstMove)
		{
			MessageHandler->OnTouchFirstMove(Touch.Position, Touch.Force, Touch.Handle, ControllerId);
		}
	}
	
	TouchInputStack.Empty(0);
	
	
	// these come in pairs
	for(int32 KeyIndex = 0; KeyIndex < KeyInputStack.Num(); KeyIndex+=2)
	{
		int32 KeyCode = KeyInputStack[KeyIndex];
		int32 CharCode = KeyInputStack[KeyIndex + 1];
		MessageHandler->OnKeyDown(KeyCode, CharCode, false);
		MessageHandler->OnKeyChar(CharCode,  false);
		MessageHandler->OnKeyUp  (KeyCode, CharCode, false);
	}
	KeyInputStack.Empty(0);
}

void FIOSInputInterface::SendControllerEvents()
{
	FScopeLock Lock(&CriticalSection);

	int32 ControllerIndex = -1;
	
#if !PLATFORM_TVOS
	// on ios, touches always go go player 0
	ProcessTouchesAndKeys(0);
#endif

	
#if !PLATFORM_TVOS // @todo tvos: This needs to come from the Microcontroller rotation
	if (!bPauseMotion)
	{
		// Update motion controls.
		FVector Attitude;
		FVector RotationRate;
		FVector Gravity;
		FVector Acceleration;

		GetMovementData(Attitude, RotationRate, Gravity, Acceleration);

		// Fix-up yaw to match directions
		Attitude.Y = -Attitude.Y;
		RotationRate.Y = -RotationRate.Y;

		// munge the vectors based on the orientation
		ModifyVectorByOrientation(Attitude, true);
		ModifyVectorByOrientation(RotationRate, true);
		ModifyVectorByOrientation(Gravity, false);
		ModifyVectorByOrientation(Acceleration, false);

		MessageHandler->OnMotionDetected(Attitude, RotationRate, Gravity, Acceleration, 0);
	}
#endif
		
	for (GCController* Cont in [GCController controllers])
 	{
		GCExtendedGamepad* ExtendedGamepad = Cont.extendedGamepad;
#if PLATFORM_TVOS
		GCMicroGamepad* MicroGamepad = Cont.microGamepad;
#endif
		GCMotion* Motion = Cont.motion;

		// skip over gamepads if we don't allow controllers
		if (ExtendedGamepad != nil && !bAllowControllers)
		{
			continue;
		}
		
		// make sure the connection handler has run on this guy
		if (Cont.playerIndex == GCControllerPlayerIndexUnset)
		{
			HandleConnection(Cont);
		}

		FUserController& Controller = Controllers[Cont.playerIndex];
		
		if (Controller.bPauseWasPressed)
		{
			MessageHandler->OnControllerButtonPressed(FGamepadKeyNames::SpecialRight, Cont.playerIndex, false);
			MessageHandler->OnControllerButtonReleased(FGamepadKeyNames::SpecialRight, Cont.playerIndex, false);
			
			Controller.bPauseWasPressed = false;
		}
		
        const double CurrentTime = FPlatformTime::Seconds();
        const float IniitialRepeatDelay = 0.2f;
        const float RepeatDelay = 0.1;

#define HANDLE_BUTTON_INTERNAL(Gamepad, bWasPressed, bIsPressed, UEButton) \
if (bWasPressed != bIsPressed) \
{ \
	NSLog(@"%@ button %s on controller %d", bIsPressed ? @"Pressed" : @"Released", TCHAR_TO_ANSI(*UEButton.ToString()), (int32)Cont.playerIndex); \
	bIsPressed ? MessageHandler->OnControllerButtonPressed(UEButton, Cont.playerIndex, false) : MessageHandler->OnControllerButtonReleased(UEButton, Cont.playerIndex, false); \
    NextKeyRepeatTime.FindOrAdd(UEButton) = CurrentTime + IniitialRepeatDelay; \
} \
else if(bIsPressed) \
{ \
    double* NextRepeatTime = NextKeyRepeatTime.Find(UEButton); \
    if(NextRepeatTime && *NextRepeatTime <= CurrentTime) \
    { \
        MessageHandler->OnControllerButtonPressed(UEButton, Cont.playerIndex, true); \
*NextRepeatTime = CurrentTime + RepeatDelay; \
    } \
} \
else \
{ \
    NextKeyRepeatTime.Remove(UEButton); \
}
		
#define HANDLE_BUTTON(Gamepad, GCButton, UEButton) \
{ \
	const bool bWasPressed = Controller.Previous##Gamepad != nil && Controller.Previous##Gamepad.GCButton.pressed; \
	const bool bPressed = Gamepad.GCButton.pressed; \
	HANDLE_BUTTON_INTERNAL(Gamepad, bWasPressed, bPressed, UEButton); \
}

        // Send controller events any time we are passed the given input threshold similarly to PC/Console (see: XInputInterface.cpp)
        const float RepeatDeadzone = 0.24;
        
#define HANDLE_ANALOG(Gamepad, GCAxis, UEAxis) \
if ((Controller.Previous##Gamepad != nil && Gamepad.GCAxis.value != Controller.Previous##Gamepad.GCAxis.value) || (Gamepad.GCAxis.value < -RepeatDeadzone || Gamepad.GCAxis.value > RepeatDeadzone)) \
{ \
	NSLog(@"Axis %s is %f", TCHAR_TO_ANSI(*UEAxis.ToString()), Gamepad.GCAxis.value); \
	MessageHandler->OnControllerAnalog(UEAxis, Cont.playerIndex, Gamepad.GCAxis.value); \
}

#define HANDLE_ANALOG_VIRTUAL_BUTTONS(Gamepad, GCAxis, UEButtonNegative, UEButtonPositive) \
{ \
	const bool bWasNegativePressed = Controller.Previous##Gamepad != nil && Controller.Previous##Gamepad.GCAxis.value <= -RepeatDeadzone; \
	const bool bNegativePressed = Gamepad.GCAxis.value <= -RepeatDeadzone; \
	HANDLE_BUTTON_INTERNAL(Gamepad, bWasNegativePressed, bNegativePressed, UEButtonNegative) \
	const bool bWasPositivePressed = Controller.Previous##Gamepad != nil && Controller.Previous##Gamepad.GCAxis.value >= RepeatDeadzone; \
	const bool bPositivePressed = Gamepad.GCAxis.value >= RepeatDeadzone; \
	HANDLE_BUTTON_INTERNAL(Gamepad, bWasPositivePressed, bPositivePressed, UEButtonPositive) \
}
		
		if (ExtendedGamepad != nil)
		{
			HANDLE_BUTTON(ExtendedGamepad, buttonA,			FGamepadKeyNames::FaceButtonBottom);
			HANDLE_BUTTON(ExtendedGamepad, buttonB,			FGamepadKeyNames::FaceButtonRight);
			HANDLE_BUTTON(ExtendedGamepad, buttonX,			FGamepadKeyNames::FaceButtonLeft);
			HANDLE_BUTTON(ExtendedGamepad, buttonY,			FGamepadKeyNames::FaceButtonTop);
			HANDLE_BUTTON(ExtendedGamepad, leftShoulder,	FGamepadKeyNames::LeftShoulder);
			HANDLE_BUTTON(ExtendedGamepad, rightShoulder,	FGamepadKeyNames::RightShoulder);
			HANDLE_BUTTON(ExtendedGamepad, leftTrigger,		FGamepadKeyNames::LeftTriggerThreshold);
			HANDLE_BUTTON(ExtendedGamepad, rightTrigger,	FGamepadKeyNames::RightTriggerThreshold);
			HANDLE_BUTTON(ExtendedGamepad, dpad.up,			FGamepadKeyNames::DPadUp);
			HANDLE_BUTTON(ExtendedGamepad, dpad.down,		FGamepadKeyNames::DPadDown);
			HANDLE_BUTTON(ExtendedGamepad, dpad.right,		FGamepadKeyNames::DPadRight);
			HANDLE_BUTTON(ExtendedGamepad, dpad.left,		FGamepadKeyNames::DPadLeft);
			
			HANDLE_ANALOG(ExtendedGamepad, leftThumbstick.xAxis,	FGamepadKeyNames::LeftAnalogX);
			HANDLE_ANALOG(ExtendedGamepad, leftThumbstick.yAxis,	FGamepadKeyNames::LeftAnalogY);
			HANDLE_ANALOG(ExtendedGamepad, rightThumbstick.xAxis,	FGamepadKeyNames::RightAnalogX);
			HANDLE_ANALOG(ExtendedGamepad, rightThumbstick.yAxis,	FGamepadKeyNames::RightAnalogY);
			HANDLE_ANALOG(ExtendedGamepad, leftTrigger,				FGamepadKeyNames::LeftTriggerAnalog);
			HANDLE_ANALOG(ExtendedGamepad, rightTrigger,			FGamepadKeyNames::RightTriggerAnalog);

			HANDLE_ANALOG_VIRTUAL_BUTTONS(ExtendedGamepad, leftThumbstick.xAxis, FGamepadKeyNames::LeftStickLeft, FGamepadKeyNames::LeftStickRight);
			HANDLE_ANALOG_VIRTUAL_BUTTONS(ExtendedGamepad, leftThumbstick.yAxis, FGamepadKeyNames::LeftStickDown, FGamepadKeyNames::LeftStickUp);
			HANDLE_ANALOG_VIRTUAL_BUTTONS(ExtendedGamepad, rightThumbstick.xAxis, FGamepadKeyNames::RightStickLeft, FGamepadKeyNames::RightStickRight);
			HANDLE_ANALOG_VIRTUAL_BUTTONS(ExtendedGamepad, rightThumbstick.yAxis, FGamepadKeyNames::RightStickDown, FGamepadKeyNames::RightStickUp);

			[Controller.PreviousExtendedGamepad release];
			Controller.PreviousExtendedGamepad = [ExtendedGamepad saveSnapshot];
			[Controller.PreviousExtendedGamepad retain];
		}
#if PLATFORM_TVOS
        // get micro input (shouldn't have the other two)
        else if (MicroGamepad != nil)
        {
			// if we want virtual joysticks, then use the dpad values (and drain the touch queue to not leak memory)
			if (bUseRemoteAsVirtualJoystick)
			{
				HANDLE_ANALOG(MicroGamepad, dpad.xAxis, FGamepadKeyNames::LeftAnalogX);
				HANDLE_ANALOG(MicroGamepad, dpad.yAxis, FGamepadKeyNames::LeftAnalogY);
				
				// @todo tvos: Do these need a deadzone? I need a good test case for these bindings
				HANDLE_BUTTON(MicroGamepad, dpad.up,	FGamepadKeyNames::LeftStickUp);
				HANDLE_BUTTON(MicroGamepad, dpad.down,	FGamepadKeyNames::LeftStickDown);
				HANDLE_BUTTON(MicroGamepad, dpad.right,	FGamepadKeyNames::LeftStickRight);
				HANDLE_BUTTON(MicroGamepad, dpad.left,	FGamepadKeyNames::LeftStickLeft);

				TouchInputStack.Empty(0);
			}
			// otherwise, process touches like ios for the remote's index
			else
			{
				ProcessTouchesAndKeys(Cont.playerIndex);
			}
			
			HANDLE_BUTTON(MicroGamepad, buttonA,	FGamepadKeyNames::FaceButtonBottom);
			HANDLE_BUTTON(MicroGamepad, buttonX,	FGamepadKeyNames::FaceButtonRight);
		
			
			[Controller.PreviousMicroGamepad release];
			Controller.PreviousMicroGamepad = [MicroGamepad saveSnapshot];
			[Controller.PreviousMicroGamepad retain];
        }
        
		// motion is orthogonal to buttons
// @todo tvos: handle motion without attitude or rotation rate
#if 0
		if (Motion != nil)
		{
			FVector Attitude;
			FVector RotationRate;
			FVector Gravity;
			FVector Acceleration;

		
			FQuat CurrentAttitude(Motion.attitude.x, Motion.attitude.y, Motion.attitude.z, Motion.attitude.w);
			
			// save off current as reference when we calibrate
			if (Controller.bNeedsReferenceAttitude)
			{
				Controller.ReferenceAttitude = CurrentAttitude;
				Controller.bHasReferenceAttitude = true;
				Controller.bNeedsReferenceAttitude = false;
			}
			
			// take away the reference if we have it
			if (Controller.bHasReferenceAttitude)
			{
				CurrentAttitude *= Controller.ReferenceAttitude.Inverse();
			}
			
			// convert from GCMotion to unrealism
			Attitude = CurrentAttitude.Euler();
			RotationRate = FVector(float(Motion.rotationRate.x), float(Motion.rotationRate.y), float(Motion.rotationRate.z));
			Gravity = FVector(float(Motion.gravity.x), float(Motion.gravity.y), float(Motion.gravity.z));
			Acceleration = FVector(float(Motion.userAcceleration.x), float(Motion.userAcceleration.y), float(Motion.userAcceleration.z));
			
			
			// Fix-up yaw to match directions we expect
			Attitude.Y = -Attitude.Y;
			RotationRate.Y = -RotationRate.Y;
			
			// @todo tvos: Handle rotated controller? Not sure that we can get if it's in landscape mode or not
			MessageHandler->OnMotionDetected(Attitude, RotationRate, Gravity, Acceleration, 0);
			
//			NSLog(@"Atti %.2f, %.2f, %.2f\n", Attitude.X, Attitude.Y, Attitude.Z);
//			NSLog(@"SrcA %.2f, %.2f, %.2f, %.2f\n", Motion.attitude.x, Motion.attitude.y, Motion.attitude.z, Motion.attitude.w);
//			NSLog(@"Rate %.2f, %.2f, %.2f\n", RotationRate.X, RotationRate.Y, RotationRate.Z);
//			NSLog(@"Grav %.2f, %.2f, %.2f\n", Gravity.X, Gravity.Y, Gravity.Z);
//			NSLog(@"Acce %.2f, %.2f, %.2f\n", Acceleration.X, Acceleration.Y, Acceleration.Z);
		}
#endif
		
#endif
	}
}

void FIOSInputInterface::QueueTouchInput(const TArray<TouchInput>& InTouchEvents)
{
	FScopeLock Lock(&CriticalSection);

	FIOSInputInterface::TouchInputStack.Append(InTouchEvents);
}

void FIOSInputInterface::QueueKeyInput(int32 Key, int32 Char)
{
	FScopeLock Lock(&CriticalSection);

	// put the key and char into the array
	FIOSInputInterface::KeyInputStack.Add(Key);
	FIOSInputInterface::KeyInputStack.Add(Char);
}

void FIOSInputInterface::EnableMotionData(bool bEnable)
{
	bPauseMotion = !bEnable;

#if !PLATFORM_TVOS
	if (bPauseMotion && MotionManager != nil)
	{
		[ReferenceAttitude release];
		ReferenceAttitude = nil;
		
		[MotionManager release];
		MotionManager = nil;
	}
	// When enabled MotionManager will be initialized on first use
#endif
}

bool FIOSInputInterface::IsMotionDataEnabled() const
{
	return !bPauseMotion;
}

void FIOSInputInterface::GetMovementData(FVector& Attitude, FVector& RotationRate, FVector& Gravity, FVector& Acceleration)
{
#if !PLATFORM_TVOS
	// initialize on first use
	if (MotionManager == nil)
	{
		// Look to see if we can create the motion manager
		MotionManager = [[CMMotionManager alloc] init];

		// Check to see if the device supports full motion (gyro + accelerometer)
		if (MotionManager.deviceMotionAvailable)
		{
			MotionManager.deviceMotionUpdateInterval = 0.02;

			// Start the Device updating motion
			[MotionManager startDeviceMotionUpdates];
		}
		else
		{
			[MotionManager startAccelerometerUpdates];
			CenterPitch = CenterPitch = 0;
			bIsCalibrationRequested = false;
		}
	}

	// do we have full motion data?
	if (MotionManager.deviceMotionActive)
	{
		// Grab the values
		CMAttitude* CurrentAttitude = MotionManager.deviceMotion.attitude;
		CMRotationRate CurrentRotationRate = MotionManager.deviceMotion.rotationRate;
		CMAcceleration CurrentGravity = MotionManager.deviceMotion.gravity;
		CMAcceleration CurrentUserAcceleration = MotionManager.deviceMotion.userAcceleration;

		// apply a reference attitude if we have been calibrated away from default
		if (ReferenceAttitude)
		{
			[CurrentAttitude multiplyByInverseOfAttitude : ReferenceAttitude];
		}

		// convert to UE3
		Attitude = FVector(float(CurrentAttitude.pitch), float(CurrentAttitude.yaw), float(CurrentAttitude.roll));
		RotationRate = FVector(float(CurrentRotationRate.x), float(CurrentRotationRate.y), float(CurrentRotationRate.z));
		Gravity = FVector(float(CurrentGravity.x), float(CurrentGravity.y), float(CurrentGravity.z));
		Acceleration = FVector(float(CurrentUserAcceleration.x), float(CurrentUserAcceleration.y), float(CurrentUserAcceleration.z));
	}
	else
	{
		// get the plain accleration
		CMAcceleration RawAcceleration = [MotionManager accelerometerData].acceleration;
		FVector NewAcceleration(RawAcceleration.x, RawAcceleration.y, RawAcceleration.z);

		// storage for keeping the accelerometer values over time (for filtering)
		static bool bFirstAccel = true;

		// how much of the previous frame's acceleration to keep
		const float VectorFilter = bFirstAccel ? 0.0f : 0.85f;
		bFirstAccel = false;

		// apply new accelerometer values to last frames
		FilteredAccelerometer = FilteredAccelerometer * VectorFilter + (1.0f - VectorFilter) * NewAcceleration;

		// create an normalized acceleration vector
		FVector FinalAcceleration = -FilteredAccelerometer.GetSafeNormal();

		// calculate Roll/Pitch
		float CurrentPitch = FMath::Atan2(FinalAcceleration.Y, FinalAcceleration.Z);
		float CurrentRoll = -FMath::Atan2(FinalAcceleration.X, FinalAcceleration.Z);

		// if we want to calibrate, use the current values as center
		if (bIsCalibrationRequested)
		{
			CenterPitch = CurrentPitch;
			CenterRoll = CurrentRoll;
			bIsCalibrationRequested = false;
		}

		CurrentPitch -= CenterPitch;
		CurrentRoll -= CenterRoll;

		Attitude = FVector(CurrentPitch, 0, CurrentRoll);
		RotationRate = FVector(LastPitch - CurrentPitch, 0, LastRoll - CurrentRoll);
		Gravity = FVector(0, 0, 0);

		// use the raw acceleration for acceleration
		Acceleration = NewAcceleration;

		// remember for next time (for rotation rate)
		LastPitch = CurrentPitch;
		LastRoll = CurrentRoll;
	}
#endif
}

void FIOSInputInterface::CalibrateMotion(uint32 PlayerIndex)
{
#if !PLATFORM_TVOS
	// If we are using the motion manager, grab a reference frame.  Note, once you set the Attitude Reference frame
	// all additional reference information will come from it
	if (MotionManager && MotionManager.deviceMotionActive)
	{
		ReferenceAttitude = [MotionManager.deviceMotion.attitude retain];
	}
	else
	{
		bIsCalibrationRequested = true;
	}
#endif

	if (PlayerIndex >= 0 && PlayerIndex < ARRAY_COUNT(Controllers))
	{
		Controllers[PlayerIndex].bNeedsReferenceAttitude = true;
	}
}

bool FIOSInputInterface::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	// Keep track whether the command was handled or not.
	bool bHandledCommand = false;

	if (FParse::Command(&Cmd, TEXT("CALIBRATEMOTION")))
	{
		uint32 PlayerIndex = FCString::Atoi(Cmd);
		CalibrateMotion(PlayerIndex);
		bHandledCommand = true;
	}

	return bHandledCommand;
}
bool FIOSInputInterface::IsControllerAssignedToGamepad(int32 ControllerId) const
{
	return ControllerId < ARRAY_COUNT(Controllers) &&
		(Controllers[ControllerId].bIsGamepadConnected ||
		 Controllers[ControllerId].bIsRemoteConnected);
}

bool FIOSInputInterface::IsGamepadAttached() const
{
	bool bIsAttached = false;
	for(int32 i = 0; i < ARRAY_COUNT(Controllers); ++i)
	{
		bIsAttached |= IsControllerAssignedToGamepad(i);
	}
	return bIsAttached && bAllowControllers;
}

void FIOSInputInterface::SetForceFeedbackChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
	if(HapticFeedbackSupportLevel >= 2)
	{
		// if we are at rest, then kick when we are over the Kick cutoff
		if (LastHapticValue == 0.0f && Value > 0.0f)
		{
			const float HeavyKickVal = CVarHapticsKickHeavy.GetValueOnGameThread();
			const float MediumKickVal = CVarHapticsKickMedium.GetValueOnGameThread();
			const float LightKickVal = CVarHapticsKickLight.GetValueOnGameThread();
			// once we get past the
			if (Value > LightKickVal)
			{
				if (Value > HeavyKickVal)
				{
					FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType::ImpactHeavy);
				}
				else if (Value > MediumKickVal)
				{
					FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType::ImpactMedium);
				}
				else
				{
					FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType::ImpactLight);
				}

				FPlatformMisc::TriggerMobileHaptics();
				
				// remember it to not kick again
				LastHapticValue = Value;
			}
		}
		else
		{
			const float RestVal = CVarHapticsRest.GetValueOnGameThread();

			if (Value >= RestVal)
			{
				// always remember the last value if we are over the Rest amount
				LastHapticValue = Value;
			}
			else
			{
				// release the haptics
				FPlatformMisc::ReleaseMobileHaptics();
				
				// rest
				LastHapticValue = 0.0f;
			}
		}
	}
	else
	{
		if(Value >= 0.3f)
		{
			AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
		}
	}
}

void FIOSInputInterface::SetForceFeedbackChannelValues(int32 ControllerId, const FForceFeedbackValues &Values)
{
	// Use largest vibration state as value
	float MaxLeft = Values.LeftLarge > Values.LeftSmall ? Values.LeftLarge : Values.LeftSmall;
	float MaxRight = Values.RightLarge > Values.RightSmall ? Values.RightLarge : Values.RightSmall;
	float Value = MaxLeft > MaxRight ? MaxLeft : MaxRight;

	// the other function will just play, regardless of channel
	SetForceFeedbackChannelValue(ControllerId, FForceFeedbackChannelType::LEFT_LARGE, Value);
}
