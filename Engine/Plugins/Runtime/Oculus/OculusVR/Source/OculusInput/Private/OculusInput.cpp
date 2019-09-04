// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OculusInput.h"

#if OCULUS_INPUT_SUPPORTED_PLATFORMS
#include "OculusHMD.h"
#include "Misc/CoreDelegates.h"
#include "Features/IModularFeatures.h"
#include "Misc/ConfigCacheIni.h"

#define OVR_DEBUG_LOGGING 0

#define LOCTEXT_NAMESPACE "OculusInput"

namespace OculusInput
{

const FKey FOculusKey::OculusTouch_Left_Thumbstick("OculusTouch_Left_Thumbstick");
const FKey FOculusKey::OculusTouch_Left_Trigger("OculusTouch_Left_Trigger");
const FKey FOculusKey::OculusTouch_Left_FaceButton1("OculusTouch_Left_FaceButton1");
const FKey FOculusKey::OculusTouch_Left_FaceButton2("OculusTouch_Left_FaceButton2");
const FKey FOculusKey::OculusTouch_Left_IndexPointing("OculusTouch_Left_IndexPointing");
const FKey FOculusKey::OculusTouch_Left_ThumbUp("OculusTouch_Left_ThumbUp");

const FKey FOculusKey::OculusTouch_Right_Thumbstick("OculusTouch_Right_Thumbstick");
const FKey FOculusKey::OculusTouch_Right_Trigger("OculusTouch_Right_Trigger");
const FKey FOculusKey::OculusTouch_Right_FaceButton1("OculusTouch_Right_FaceButton1");
const FKey FOculusKey::OculusTouch_Right_FaceButton2("OculusTouch_Right_FaceButton2");
const FKey FOculusKey::OculusTouch_Right_IndexPointing("OculusTouch_Right_IndexPointing");
const FKey FOculusKey::OculusTouch_Right_ThumbUp("OculusTouch_Right_ThumbUp");

const FKey FOculusKey::OculusRemote_DPad_Down("OculusRemote_DPad_Down");
const FKey FOculusKey::OculusRemote_DPad_Up("OculusRemote_DPad_Up");
const FKey FOculusKey::OculusRemote_DPad_Left("OculusRemote_DPad_Left");
const FKey FOculusKey::OculusRemote_DPad_Right("OculusRemote_DPad_Right");
const FKey FOculusKey::OculusRemote_Enter("OculusRemote_Enter");
const FKey FOculusKey::OculusRemote_Back("OculusRemote_Back");
const FKey FOculusKey::OculusRemote_VolumeUp("OculusRemote_VolumeUp");
const FKey FOculusKey::OculusRemote_VolumeDown("OculusRemote_VolumeDown");
const FKey FOculusKey::OculusRemote_Home("OculusRemote_Home");

const FKey FOculusKey::OculusTouchpad_Touchpad("OculusTouchpad_Touchpad");
const FKey FOculusKey::OculusTouchpad_Touchpad_X("OculusTouchpad_Touchpad_X");
const FKey FOculusKey::OculusTouchpad_Touchpad_Y("OculusTouchpad_Touchpad_Y");
const FKey FOculusKey::OculusTouchpad_Back("OculusTouchpad_Back");

const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_Thumbstick("OculusTouch_Left_Thumbstick");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_Trigger("OculusTouch_Left_Trigger");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_FaceButton1("OculusTouch_Left_FaceButton1");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_FaceButton2("OculusTouch_Left_FaceButton2");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_IndexPointing("OculusTouch_Left_IndexPointing");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Left_ThumbUp("OculusTouch_Left_ThumbUp");

const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_Thumbstick("OculusTouch_Right_Thumbstick");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_Trigger("OculusTouch_Right_Trigger");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_FaceButton1("OculusTouch_Right_FaceButton1");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_FaceButton2("OculusTouch_Right_FaceButton2");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_IndexPointing("OculusTouch_Right_IndexPointing");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouch_Right_ThumbUp("OculusTouch_Right_ThumbUp");

const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_DPad_Down("OculusRemote_DPad_Down");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_DPad_Up("OculusRemote_DPad_Up");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_DPad_Left("OculusRemote_DPad_Left");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_DPad_Right("OculusRemote_DPad_Right");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_Enter("OculusRemote_Enter");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_Back("OculusRemote_Back");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_VolumeUp("OculusRemote_VolumeUp");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_VolumeDown("OculusRemote_VolumeDown");
const FOculusKeyNames::Type FOculusKeyNames::OculusRemote_Home("OculusRemote_Home");

const FOculusKeyNames::Type FOculusKeyNames::OculusTouchpad_Touchpad("OculusTouchpad_Touchpad");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouchpad_Touchpad_X("OculusTouchpad_Touchpad_X");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouchpad_Touchpad_Y("OculusTouchpad_Touchpad_Y");
const FOculusKeyNames::Type FOculusKeyNames::OculusTouchpad_Back("OculusTouchpad_Back");

/** Threshold for treating trigger pulls as button presses, from 0.0 to 1.0 */
float FOculusInput::TriggerThreshold = 0.8f;

/** Are Remote keys mapped to gamepad or not. */
bool FOculusInput::bRemoteKeysMappedToGamepad = true;

float FOculusInput::InitialButtonRepeatDelay = 0.2f;
float FOculusInput::ButtonRepeatDelay = 0.1f;

FOculusInput::FOculusInput( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
	: OVRPluginHandle(nullptr)
	, MessageHandler( InMessageHandler )
	, ControllerPairs()
{
	// take care of backward compatibility of Remote with Gamepad 
	if (bRemoteKeysMappedToGamepad)
	{
		Remote.MapKeysToGamepad();
	}

	OVRPluginHandle = FOculusHMDModule::GetOVRPluginHandle();

	FOculusTouchControllerPair& ControllerPair = *new(ControllerPairs) FOculusTouchControllerPair();

	// @todo: Unreal controller index should be assigned to us by the engine to ensure we don't contest with other devices
	ControllerPair.UnrealControllerIndex = 0; //???? NextUnrealControllerIndex++;

	IModularFeatures::Get().RegisterModularFeature( GetModularFeatureName(), this );

	UE_LOG(LogOcInput, Log, TEXT("OculusInput is initialized"));
}


FOculusInput::~FOculusInput()
{
	IModularFeatures::Get().UnregisterModularFeature( GetModularFeatureName(), this );

	if (OVRPluginHandle)
	{
		FPlatformProcess::FreeDllHandle(OVRPluginHandle);
		OVRPluginHandle = nullptr;
	}
}

void FOculusInput::PreInit()
{
	// Load the config, even if we failed to initialize a controller
	LoadConfig();

	// Register the FKeys
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_Thumbstick, LOCTEXT("OculusTouch_Left_Thumbstick", "Oculus Touch (L) Thumbstick CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_FaceButton1, LOCTEXT("OculusTouch_Left_FaceButton1", "Oculus Touch (L) X Button CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_Trigger, LOCTEXT("OculusTouch_Left_Trigger", "Oculus Touch (L) Trigger CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_FaceButton2, LOCTEXT("OculusTouch_Left_FaceButton2", "Oculus Touch (L) Y Button CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_IndexPointing, LOCTEXT("OculusTouch_Left_IndexPointing", "Oculus Touch (L) Pointing CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Left_ThumbUp, LOCTEXT("OculusTouch_Left_ThumbUp", "Oculus Touch (L) Thumb Up CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));

	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_Thumbstick, LOCTEXT("OculusTouch_Right_Thumbstick", "Oculus Touch (R) Thumbstick CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_FaceButton1, LOCTEXT("OculusTouch_Right_FaceButton1", "Oculus Touch (R) A Button CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_Trigger, LOCTEXT("OculusTouch_Right_Trigger", "Oculus Touch (R) Trigger CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_FaceButton2, LOCTEXT("OculusTouch_Right_FaceButton2", "Oculus Touch (R) B Button CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_IndexPointing, LOCTEXT("OculusTouch_Right_IndexPointing", "Oculus Touch (R) Pointing CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouch_Right_ThumbUp, LOCTEXT("OculusTouch_Right_ThumbUp", "Oculus Touch (R) Thumb Up CapTouch"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));

	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_DPad_Up, LOCTEXT("OculusRemote_DPad_Up", "Oculus Remote D-pad Up"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_DPad_Down, LOCTEXT("OculusRemote_DPad_Down", "Oculus Remote D-pad Down"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_DPad_Left, LOCTEXT("OculusRemote_DPad_Left", "Oculus Remote D-pad Left"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_DPad_Right, LOCTEXT("OculusRemote_DPad_Right", "Oculus Remote D-pad Right"), FKeyDetails::GamepadKey));

	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_Enter, LOCTEXT("OculusRemote_Enter", "Oculus Remote Enter"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_Back, LOCTEXT("OculusRemote_Back", "Oculus Remote Back"), FKeyDetails::GamepadKey));

	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_VolumeUp, LOCTEXT("OculusRemote_VolumeUp", "Oculus Remote Volume Up"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_VolumeDown, LOCTEXT("OculusRemote_VolumeDown", "Oculus Remote Volume Down"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusRemote_Home, LOCTEXT("OculusRemote_Home", "Oculus Remote Home"), FKeyDetails::GamepadKey));

	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouchpad_Touchpad, LOCTEXT("OculusTouchpad_Touchpad", "Oculus Touchpad Button"), FKeyDetails::GamepadKey));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouchpad_Touchpad_X, LOCTEXT("OculusTouchpad_Touchpad_X", "Oculus Touchpad X-Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouchpad_Touchpad_Y, LOCTEXT("OculusTouchpad_Touchpad_Y", "Oculus Touchpad Y-Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis));
	EKeys::AddKey(FKeyDetails(FOculusKey::OculusTouchpad_Back, LOCTEXT("OculusTouchpad_Back", "Oculus Touchpad Back"), FKeyDetails::GamepadKey));

	UE_LOG(LogOcInput, Log, TEXT("OculusInput pre-init called"));
}

void FOculusInput::LoadConfig()
{
	const TCHAR* OculusTouchSettings = TEXT("OculusTouch.Settings");
	float ConfigThreshold = TriggerThreshold;

	if (GConfig->GetFloat(OculusTouchSettings, TEXT("TriggerThreshold"), ConfigThreshold, GEngineIni))
	{
		TriggerThreshold = ConfigThreshold;
	}

	const TCHAR* OculusRemoteSettings = TEXT("OculusRemote.Settings");
	bool b;
	if (GConfig->GetBool(OculusRemoteSettings, TEXT("bRemoteKeysMappedToGamepad"), b, GEngineIni))
	{
		bRemoteKeysMappedToGamepad = b;
	}

	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("InitialButtonRepeatDelay"), InitialButtonRepeatDelay, GInputIni);
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("ButtonRepeatDelay"), ButtonRepeatDelay, GInputIni);
}

void FOculusInput::Tick( float DeltaTime )
{
	// Nothing to do when ticking, for now.  SendControllerEvents() handles everything.
}


void FOculusInput::SendControllerEvents()
{
	const double CurrentTime = FPlatformTime::Seconds();
	const float AnalogButtonPressThreshold = TriggerThreshold;

	if(IOculusHMDModule::IsAvailable() && ovrp_GetInitialized() && FApp::HasVRFocus())
	{
		if (MessageHandler.IsValid() && GEngine->XRSystem->GetHMDDevice())
		{
			OculusHMD::FOculusHMD* OculusHMD = static_cast<OculusHMD::FOculusHMD*>(GEngine->XRSystem->GetHMDDevice());
			ovrp_Update3(ovrpStep_Render, OculusHMD->GetNextFrameNumber(), 0.0);

			ovrpControllerState4 OvrpControllerState;
			
			if (OVRP_SUCCESS(ovrp_GetControllerState4(ovrpController_Remote, &OvrpControllerState)) &&
				(OvrpControllerState.ConnectedControllerTypes & ovrpController_Remote))
			{
				for (int32 ButtonIndex = 0; ButtonIndex < (int32)EOculusRemoteControllerButton::TotalButtonCount; ++ButtonIndex)
				{
					FOculusButtonState& ButtonState = Remote.Buttons[ButtonIndex];
					check(!ButtonState.Key.IsNone()); // is button's name initialized?

					// Determine if the button is pressed down
					bool bButtonPressed = false;
					switch ((EOculusRemoteControllerButton)ButtonIndex)
					{
					case EOculusRemoteControllerButton::DPad_Up:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Up) != 0;
						break;

					case EOculusRemoteControllerButton::DPad_Down:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Down) != 0;
						break;

					case EOculusRemoteControllerButton::DPad_Left:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Left) != 0;
						break;

					case EOculusRemoteControllerButton::DPad_Right:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Right) != 0;
						break;

					case EOculusRemoteControllerButton::Enter:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Start) != 0;
						break;

					case EOculusRemoteControllerButton::Back:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Back) != 0;
						break;

					case EOculusRemoteControllerButton::VolumeUp:
						#ifdef SUPPORT_INTERNAL_BUTTONS
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_VolUp) != 0;
						#endif
						break;

					case EOculusRemoteControllerButton::VolumeDown:
						#ifdef SUPPORT_INTERNAL_BUTTONS
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_VolDown) != 0;
						#endif
						break;

					case EOculusRemoteControllerButton::Home:
						#ifdef SUPPORT_INTERNAL_BUTTONS
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Home) != 0;
						#endif
						break;

					default:
						check(0); // unhandled button, shouldn't happen
						break;
					}

					// Update button state
					if (bButtonPressed != ButtonState.bIsPressed)
					{
						ButtonState.bIsPressed = bButtonPressed;
						if (ButtonState.bIsPressed)
						{
							OnControllerButtonPressed(ButtonState, 0, false);

							// Set the timer for the first repeat
							ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
						}
						else
						{
							OnControllerButtonReleased(ButtonState, 0, false);
						}
					}

					// Apply key repeat, if its time for that
					if (ButtonState.bIsPressed && ButtonState.NextRepeatTime <= CurrentTime)
					{
						OnControllerButtonPressed(ButtonState, 0, true);

						// Set the timer for the next repeat
						ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
					}
				}
			}

			if (OVRP_SUCCESS(ovrp_GetControllerState4(ovrpController_Touchpad, &OvrpControllerState)) && 
				(OvrpControllerState.ConnectedControllerTypes & ovrpController_Touchpad))
			{
				ovrpVector2f ThumbstickValue = OvrpControllerState.Touchpad[0];

				if (ThumbstickValue.x != Touchpad.TouchpadPosition.X)
				{
					Touchpad.TouchpadPosition.X = ThumbstickValue.x;
					MessageHandler->OnControllerAnalog(FOculusKeyNames::OculusTouchpad_Touchpad_X, 0, Touchpad.TouchpadPosition.X);
				}

				if (ThumbstickValue.y != Touchpad.TouchpadPosition.Y)
				{
					Touchpad.TouchpadPosition.Y = ThumbstickValue.y;
					// we need to negate Y value to match XBox controllers
					MessageHandler->OnControllerAnalog(FOculusKeyNames::OculusTouchpad_Touchpad_Y, 0, Touchpad.TouchpadPosition.Y);
				}

				for (int32 ButtonIndex = 0; ButtonIndex < (int32)EOculusTouchpadButton::TotalButtonCount; ++ButtonIndex)
				{
					FOculusButtonState& ButtonState = Touchpad.Buttons[ButtonIndex];
					check(!ButtonState.Key.IsNone()); // is button's name initialized?

					bool bButtonPressed = false;
					switch ((EOculusTouchpadButton)ButtonIndex)
					{
					case EOculusTouchpadButton::Back:
						bButtonPressed = (OvrpControllerState.Buttons & ovrpButton_Back) != 0;
						break;

					case EOculusTouchpadButton::Touchpad:
						bButtonPressed = (OvrpControllerState.Touches & ovrpTouch_LTouchpad) != 0;
						break;

					default:
						check(0); // unhandled button, shouldn't happen
						break;
					}

					// Update button state
					if (bButtonPressed != ButtonState.bIsPressed)
					{
						ButtonState.bIsPressed = bButtonPressed;
						if (ButtonState.bIsPressed)
						{
							OnControllerButtonPressed(ButtonState, 0, false);

							// Set the timer for the first repeat
							ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
						}
						else
						{
							OnControllerButtonReleased(ButtonState, 0, false);
						}
					}

					// Apply key repeat, if its time for that
					if (ButtonState.bIsPressed && ButtonState.NextRepeatTime <= CurrentTime)
					{
						OnControllerButtonPressed(ButtonState, 0, true);

						// Set the timer for the next repeat
						ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
					}
				}
			}

			if (OVRP_SUCCESS(ovrp_GetControllerState4((ovrpController)(ovrpController_LTrackedRemote | ovrpController_RTrackedRemote | ovrpController_Touch), &OvrpControllerState)))
			{
				UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: ButtonState = 0x%X"), OvrpControllerState.Buttons);
				UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: Touches = 0x%X"), OvrpControllerState.Touches);

				for (FOculusTouchControllerPair& ControllerPair : ControllerPairs)
				{
					for( int32 HandIndex = 0; HandIndex < ARRAY_COUNT( ControllerPair.ControllerStates ); ++HandIndex )
					{
						FOculusTouchControllerState& State = ControllerPair.ControllerStates[ HandIndex ];

						bool bIsLeft = (HandIndex == (int32)EControllerHand::Left);

						bool bIsMobileController = bIsLeft ? (OvrpControllerState.ConnectedControllerTypes & ovrpController_LTrackedRemote) != 0 : (OvrpControllerState.ConnectedControllerTypes & ovrpController_RTrackedRemote) != 0;
						bool bIsTouchController = bIsLeft ? (OvrpControllerState.ConnectedControllerTypes & ovrpController_LTouch) != 0 : (OvrpControllerState.ConnectedControllerTypes & ovrpController_RTouch) != 0;
						bool bIsCurrentlyTracked = bIsMobileController || bIsTouchController;

						if (bIsCurrentlyTracked)
						{
							ovrpNode OvrpNode = (HandIndex == (int32)EControllerHand::Left) ? ovrpNode_HandLeft : ovrpNode_HandRight;

							State.bIsConnected = true;
							ovrpBool NodePositionTracked;
							State.bIsPositionTracked = OVRP_SUCCESS(ovrp_GetNodePositionTracked2(OvrpNode, &NodePositionTracked)) && NodePositionTracked;
							ovrpBool NodeOrientationTracked;
							State.bIsOrientationTracked = OVRP_SUCCESS(ovrp_GetNodeOrientationTracked2(OvrpNode, &NodeOrientationTracked)) && NodeOrientationTracked;

							const float OvrTriggerAxis = OvrpControllerState.IndexTrigger[HandIndex];
							const float OvrGripAxis = OvrpControllerState.HandTrigger[HandIndex];

							UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: IndexTrigger[%d] = %f"), int(HandIndex), OvrTriggerAxis);
							UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: HandTrigger[%d] = %f"), int(HandIndex), OvrGripAxis);
							UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: ThumbStick[%d] = { %f, %f }"), int(HandIndex), OvrpControllerState.Thumbstick[HandIndex].x, OvrpControllerState.Thumbstick[HandIndex].y );

							if (OvrpControllerState.RecenterCount[HandIndex] != State.RecenterCount)
							{
								State.RecenterCount = OvrpControllerState.RecenterCount[HandIndex];
								FCoreDelegates::VRControllerRecentered.Broadcast();
							}
							
							if (OvrTriggerAxis != State.TriggerAxis)
							{
								State.TriggerAxis = OvrTriggerAxis;
								MessageHandler->OnControllerAnalog(bIsLeft ? FGamepadKeyNames::MotionController_Left_TriggerAxis : FGamepadKeyNames::MotionController_Right_TriggerAxis, ControllerPair.UnrealControllerIndex, State.TriggerAxis);
							}

							if (OvrGripAxis != State.GripAxis)
							{
								State.GripAxis = OvrGripAxis;
								MessageHandler->OnControllerAnalog(bIsLeft ? FGamepadKeyNames::MotionController_Left_Grip1Axis : FGamepadKeyNames::MotionController_Right_Grip1Axis, ControllerPair.UnrealControllerIndex, State.GripAxis);
							}

							ovrpVector2f ThumbstickValue = bIsMobileController ? OvrpControllerState.Touchpad[HandIndex] : OvrpControllerState.Thumbstick[HandIndex];

							if (ThumbstickValue.x != State.ThumbstickAxes.X)
							{
								State.ThumbstickAxes.X = ThumbstickValue.x;
								MessageHandler->OnControllerAnalog(bIsLeft ? FGamepadKeyNames::MotionController_Left_Thumbstick_X : FGamepadKeyNames::MotionController_Right_Thumbstick_X, ControllerPair.UnrealControllerIndex, State.ThumbstickAxes.X);
							}

							if (ThumbstickValue.y != State.ThumbstickAxes.Y)
							{
								State.ThumbstickAxes.Y = ThumbstickValue.y;
								// we need to negate Y value to match XBox controllers
								MessageHandler->OnControllerAnalog(bIsLeft ? FGamepadKeyNames::MotionController_Left_Thumbstick_Y : FGamepadKeyNames::MotionController_Right_Thumbstick_Y, ControllerPair.UnrealControllerIndex, -State.ThumbstickAxes.Y);
							}

							for (int32 ButtonIndex = 0; ButtonIndex < (int32)EOculusTouchControllerButton::TotalButtonCount; ++ButtonIndex)
							{
								FOculusButtonState& ButtonState = State.Buttons[ButtonIndex];
								check(!ButtonState.Key.IsNone()); // is button's name initialized?

								// Determine if the button is pressed down
								bool bButtonPressed = false;
								switch ((EOculusTouchControllerButton)ButtonIndex)
								{
								case EOculusTouchControllerButton::Trigger:
									bButtonPressed = State.TriggerAxis >= AnalogButtonPressThreshold;
									break;

								case EOculusTouchControllerButton::Grip:
									bButtonPressed = State.GripAxis >= AnalogButtonPressThreshold;
									break;

								case EOculusTouchControllerButton::XA:
									bButtonPressed =
										bIsMobileController ?
										(OvrpControllerState.Buttons & ovrpButton_Back) != 0 : 
										(bIsLeft ? (OvrpControllerState.Buttons & ovrpButton_X) != 0 : (OvrpControllerState.Buttons & ovrpButton_A) != 0);
									break;

								case EOculusTouchControllerButton::YB:
									bButtonPressed = bIsLeft ? (OvrpControllerState.Buttons & ovrpButton_Y) != 0 : (OvrpControllerState.Buttons & ovrpButton_B) != 0;
									break;

								case EOculusTouchControllerButton::Thumbstick:
									bButtonPressed = 
										bIsMobileController ?
											(bIsLeft ? (OvrpControllerState.Buttons & ovrpButton_LTouchpad) != 0 : (OvrpControllerState.Buttons & ovrpButton_RTouchpad) != 0) :
											(bIsLeft ? (OvrpControllerState.Buttons & ovrpButton_LThumb) != 0 : (OvrpControllerState.Buttons & ovrpButton_RThumb) != 0);
									break;
									
								case EOculusTouchControllerButton::Thumbstick_Up:
									if (bIsTouchController && State.ThumbstickAxes.Size() > 0.7f ||
										bIsMobileController && State.Buttons[(int)EOculusTouchControllerButton::Thumbstick].bIsPressed && State.ThumbstickAxes.Size() > 0.5f)
									{
										float Angle = FMath::Atan2(State.ThumbstickAxes.Y, State.ThumbstickAxes.X);
										bButtonPressed = Angle >= (1.0f / 8.0f) * PI && Angle <= (7.0f / 8.0f) * PI;
									}
									break;

								case EOculusTouchControllerButton::Thumbstick_Down:
									if (bIsTouchController && State.ThumbstickAxes.Size() > 0.7f ||
										bIsMobileController && State.Buttons[(int)EOculusTouchControllerButton::Thumbstick].bIsPressed && State.ThumbstickAxes.Size() > 0.5f)
									{
										float Angle = FMath::Atan2(State.ThumbstickAxes.Y, State.ThumbstickAxes.X);
										bButtonPressed = Angle >= (-7.0f / 8.0f) * PI && Angle <= (-1.0f / 8.0f) * PI;
									}
									break;

								case EOculusTouchControllerButton::Thumbstick_Left:
									if (bIsTouchController && State.ThumbstickAxes.Size() > 0.7f ||
										bIsMobileController && State.Buttons[(int)EOculusTouchControllerButton::Thumbstick].bIsPressed && State.ThumbstickAxes.Size() > 0.5f)
									{
										float Angle = FMath::Atan2(State.ThumbstickAxes.Y, State.ThumbstickAxes.X);
										bButtonPressed = Angle <= (-5.0f / 8.0f) * PI || Angle >= (5.0f / 8.0f) * PI;
									}
									break;

								case EOculusTouchControllerButton::Thumbstick_Right:
									if (bIsTouchController && State.ThumbstickAxes.Size() > 0.7f ||
										bIsMobileController && State.Buttons[(int)EOculusTouchControllerButton::Thumbstick].bIsPressed && State.ThumbstickAxes.Size() > 0.5f)
									{
										float Angle = FMath::Atan2(State.ThumbstickAxes.Y, State.ThumbstickAxes.X);
										bButtonPressed = Angle >= (-3.0f / 8.0f) * PI && Angle <= (3.0f / 8.0f) * PI;
									}
									break;

								case EOculusTouchControllerButton::Menu:
									bButtonPressed = bIsLeft && (OvrpControllerState.Buttons & ovrpButton_Start);
									break;
								
								default:
									check(0);
									break;
								}

								// Update button state
								if (bButtonPressed != ButtonState.bIsPressed)
								{
									ButtonState.bIsPressed = bButtonPressed;
									if (ButtonState.bIsPressed)
									{
										OnControllerButtonPressed(ButtonState, ControllerPair.UnrealControllerIndex, false);

										// Set the timer for the first repeat
										ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
									}
									else
									{
										OnControllerButtonReleased(ButtonState, ControllerPair.UnrealControllerIndex, false);
									}
								}

								// Apply key repeat, if its time for that
								if (ButtonState.bIsPressed && ButtonState.NextRepeatTime <= CurrentTime)
								{
									OnControllerButtonPressed(ButtonState, ControllerPair.UnrealControllerIndex, true);

									// Set the timer for the next repeat
									ButtonState.NextRepeatTime = CurrentTime + ButtonRepeatDelay;
								}
							}

							// Handle Capacitive States
							for (int32 CapTouchIndex = 0; CapTouchIndex < (int32)EOculusTouchCapacitiveAxes::TotalAxisCount; ++CapTouchIndex)
							{
								FOculusTouchCapacitiveState& CapState = State.CapacitiveAxes[CapTouchIndex];

								float CurrentAxisVal = 0.f;
								switch ((EOculusTouchCapacitiveAxes)CapTouchIndex)
								{
								case EOculusTouchCapacitiveAxes::XA:
								{
									const uint32 mask = (bIsLeft) ? ovrpTouch_X : ovrpTouch_A;
									CurrentAxisVal = (OvrpControllerState.Touches & mask) != 0 ? 1.f : 0.f;
									break;
								}
								case EOculusTouchCapacitiveAxes::YB:
								{
									const uint32 mask = (bIsLeft) ? ovrpTouch_Y : ovrpTouch_B;
									CurrentAxisVal = (OvrpControllerState.Touches & mask) != 0 ? 1.f : 0.f;
									break;
								}
								case EOculusTouchCapacitiveAxes::Thumbstick:
								{
									const uint32 mask = bIsMobileController ? ((bIsLeft) ? ovrpTouch_LTouchpad : ovrpTouch_RTouchpad) : ((bIsLeft) ? ovrpTouch_LThumb : ovrpTouch_RThumb);
									CurrentAxisVal = (OvrpControllerState.Touches & mask) != 0 ? 1.f : 0.f;
									break;
								}
								case EOculusTouchCapacitiveAxes::Trigger:
								{
									const uint32 mask = (bIsLeft) ? ovrpTouch_LIndexTrigger : ovrpTouch_RIndexTrigger;
									CurrentAxisVal = (OvrpControllerState.Touches & mask) != 0 ? 1.f : 0.f;
									break;
								}
								case EOculusTouchCapacitiveAxes::IndexPointing:
								{
									const uint32 mask = (bIsLeft) ? ovrpNearTouch_LIndexTrigger : ovrpNearTouch_RIndexTrigger;
									CurrentAxisVal = (OvrpControllerState.NearTouches & mask) != 0 ? 0.f : 1.f;
									break;
								}
								case EOculusTouchCapacitiveAxes::ThumbUp:
								{
									const uint32 mask = (bIsLeft) ? ovrpNearTouch_LThumbButtons : ovrpNearTouch_RThumbButtons;
									CurrentAxisVal = (OvrpControllerState.NearTouches & mask) != 0 ? 0.f : 1.f;
									break;
								}
								default:
									check(0);
								}
							
								if (CurrentAxisVal != CapState.State)
								{
									MessageHandler->OnControllerAnalog(CapState.Axis, ControllerPair.UnrealControllerIndex, CurrentAxisVal);

									CapState.State = CurrentAxisVal;
								}
							}
						}
						else
						{
							// Controller isn't available right now.  Zero out input state, so that if it comes back it will send fresh event deltas
							State = FOculusTouchControllerState((EControllerHand)HandIndex);
							UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("SendControllerEvents: Controller for the hand %d is not tracked"), int(HandIndex));
						}
					}
				}
			}
		}
	}
	UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT(""));
}


void FOculusInput::SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	MessageHandler = InMessageHandler;
}


bool FOculusInput::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	// No exec commands supported, for now.
	return false;
}

void FOculusInput::SetChannelValue( int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value )
{
	const EControllerHand Hand = ( ChannelType == FForceFeedbackChannelType::LEFT_LARGE || ChannelType == FForceFeedbackChannelType::LEFT_SMALL ) ? EControllerHand::Left : EControllerHand::Right;

	for( FOculusTouchControllerPair& ControllerPair : ControllerPairs )
	{
		if( ControllerPair.UnrealControllerIndex == ControllerId )
		{
			FOculusTouchControllerState& ControllerState = ControllerPair.ControllerStates[ (int32)Hand ];

			if (ControllerState.bPlayingHapticEffect)
			{
				continue;
			}

			// @todo: The SMALL channel controls frequency, the LARGE channel controls amplitude.  This is a bit of a weird fit.
			if( ChannelType == FForceFeedbackChannelType::LEFT_SMALL || ChannelType == FForceFeedbackChannelType::RIGHT_SMALL )
			{
				ControllerState.ForceFeedbackHapticFrequency = Value;
			}
			else
			{
				ControllerState.ForceFeedbackHapticAmplitude = Value;
			}

			UpdateForceFeedback( ControllerPair, Hand );

			break;
		}
	}
}

void FOculusInput::SetChannelValues( int32 ControllerId, const FForceFeedbackValues& Values )
{
	for( FOculusTouchControllerPair& ControllerPair : ControllerPairs )
	{
		if( ControllerPair.UnrealControllerIndex == ControllerId )
		{
			// @todo: The SMALL channel controls frequency, the LARGE channel controls amplitude.  This is a bit of a weird fit.
			FOculusTouchControllerState& LeftControllerState = ControllerPair.ControllerStates[ (int32)EControllerHand::Left ];
			if (!LeftControllerState.bPlayingHapticEffect)
			{
				LeftControllerState.ForceFeedbackHapticFrequency = Values.LeftSmall;
				LeftControllerState.ForceFeedbackHapticAmplitude = Values.LeftLarge;
				UpdateForceFeedback(ControllerPair, EControllerHand::Left);
			}

			FOculusTouchControllerState& RightControllerState = ControllerPair.ControllerStates[(int32)EControllerHand::Right];
			if (!RightControllerState.bPlayingHapticEffect)
			{
				RightControllerState.ForceFeedbackHapticFrequency = Values.RightSmall;
				RightControllerState.ForceFeedbackHapticAmplitude = Values.RightLarge;
				UpdateForceFeedback(ControllerPair, EControllerHand::Right);
			}
		}
	}
}

void FOculusInput::UpdateForceFeedback( const FOculusTouchControllerPair& ControllerPair, const EControllerHand Hand )
{
	const FOculusTouchControllerState& ControllerState = ControllerPair.ControllerStates[ (int32)Hand ];

	if( ControllerState.bIsConnected && !ControllerState.bPlayingHapticEffect)
	{
		if(IOculusHMDModule::IsAvailable() && ovrp_GetInitialized() && FApp::HasVRFocus())
		{
			ovrpControllerState4 OvrpControllerState;
			
			if (OVRP_SUCCESS(ovrp_GetControllerState4((ovrpController)(ovrpController_Active | ovrpController_LTrackedRemote | ovrpController_RTrackedRemote), &OvrpControllerState)) &&
				(OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch | ovrpController_LTrackedRemote | ovrpController_RTrackedRemote)))
			{
				float FreqMin, FreqMax = 0.f;
				GetHapticFrequencyRange(FreqMin, FreqMax);

				// Map the [0.0 - 1.0] range to a useful range of frequencies for the Oculus controllers
				const float ActualFrequency = FMath::Lerp(FreqMin, FreqMax, FMath::Clamp(ControllerState.ForceFeedbackHapticFrequency, 0.0f, 1.0f));

				// Oculus SDK wants amplitude values between 0.0 and 1.0
				const float ActualAmplitude = ControllerState.ForceFeedbackHapticAmplitude * GetHapticAmplitudeScale();

				ovrpController OvrController = ovrpController_None;
				if (OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch))
				{
					OvrController = ( Hand == EControllerHand::Left ) ? ovrpController_LTouch : ovrpController_RTouch;
				}
				else if (OvrpControllerState.ConnectedControllerTypes & (ovrpController_LTrackedRemote | ovrpController_RTrackedRemote))
				{
					OvrController = ( Hand == EControllerHand::Left ) ? ovrpController_LTrackedRemote : ovrpController_RTrackedRemote;
				}

				static float LastAmplitudeSent = -1;
				if (ActualAmplitude != LastAmplitudeSent)
				{
					ovrp_SetControllerVibration2(OvrController, ActualFrequency, ActualAmplitude);
					LastAmplitudeSent = ActualAmplitude;
				}
			}
		}
	}
}

bool FOculusInput::OnControllerButtonPressed(const FOculusButtonState& ButtonState, int32 ControllerId, bool IsRepeat)
{
	bool result = MessageHandler->OnControllerButtonPressed(ButtonState.Key, ControllerId, IsRepeat);

	if (!ButtonState.EmulatedKey.IsNone())
	{
		MessageHandler->OnControllerButtonPressed(ButtonState.EmulatedKey, ControllerId, IsRepeat);
	}

	return result;
}

bool FOculusInput::OnControllerButtonReleased(const FOculusButtonState& ButtonState, int32 ControllerId, bool IsRepeat)
{
	bool result = MessageHandler->OnControllerButtonReleased(ButtonState.Key, ControllerId, IsRepeat);

	if (!ButtonState.EmulatedKey.IsNone())
	{
		MessageHandler->OnControllerButtonReleased(ButtonState.EmulatedKey, ControllerId, IsRepeat);
	}

	return result;
}

FName FOculusInput::GetMotionControllerDeviceTypeName() const
{
	const static FName DefaultName(TEXT("OculusInputDevice"));
	return DefaultName;
}

bool FOculusInput::GetControllerOrientationAndPosition( const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	for( const FOculusTouchControllerPair& ControllerPair : ControllerPairs )
	{
		if( ControllerPair.UnrealControllerIndex == ControllerIndex )
		{
			if( (DeviceHand == EControllerHand::Left) || (DeviceHand == EControllerHand::Right) )
			{
				if (IOculusHMDModule::IsAvailable() && ovrp_GetInitialized())
				{
					OculusHMD::FOculusHMD* OculusHMD = static_cast<OculusHMD::FOculusHMD*>(GEngine->XRSystem->GetHMDDevice());
					ovrpNode Node = DeviceHand == EControllerHand::Left ? ovrpNode_HandLeft : ovrpNode_HandRight;
					ovrpBool bOrientationTracked;
					ovrpBool bPositionTracked;

					if (OVRP_SUCCESS(ovrp_GetNodeOrientationTracked2(Node, &bOrientationTracked)) &&
						OVRP_SUCCESS(ovrp_GetNodePositionTracked2(Node, &bPositionTracked)) &&
						(bOrientationTracked || bPositionTracked))
					{
						OculusHMD::FSettings* Settings;
						OculusHMD::FGameFrame* CurrentFrame;

						if (IsInGameThread())
						{
							Settings = OculusHMD->GetSettings();
							CurrentFrame = OculusHMD->GetNextFrameToRender();
						}
						else
						{
							Settings = OculusHMD->GetSettings_RenderThread();
							CurrentFrame = OculusHMD->GetFrame_RenderThread();
						}

						if (Settings && CurrentFrame)
						{
							ovrpPoseStatef InPoseState;
							OculusHMD::FPose OutPose;

							if (OVRP_SUCCESS(ovrp_GetNodePoseState3(ovrpStep_Render, CurrentFrame->FrameNumber, Node, &InPoseState)) &&
								OculusHMD->ConvertPose_Internal(InPoseState.Pose, OutPose, Settings, WorldToMetersScale))
							{
								if (bOrientationTracked)
								{
									OutOrientation = OutPose.Orientation.Rotator();
								}

								OutPosition = OutPose.Position;

								return true;
							}
						}
					}
				}
			}

			break;
		}
	}

	return false;
}

ETrackingStatus FOculusInput::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
{
	ETrackingStatus TrackingStatus = ETrackingStatus::NotTracked;

	if (DeviceHand != EControllerHand::Left && DeviceHand != EControllerHand::Right)
	{
		return TrackingStatus;
	}

	for( const FOculusTouchControllerPair& ControllerPair : ControllerPairs )
	{
		if( ControllerPair.UnrealControllerIndex == ControllerIndex )
		{
			const FOculusTouchControllerState& ControllerState = ControllerPair.ControllerStates[ (int32)DeviceHand ];

			if( ControllerState.bIsOrientationTracked )
			{
				TrackingStatus = ControllerState.bIsPositionTracked ? ETrackingStatus::Tracked : ETrackingStatus::InertialOnly;
			}

			break;
		}
	}

	return TrackingStatus;
}

void FOculusInput::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values)
{
	for (FOculusTouchControllerPair& ControllerPair : ControllerPairs)
	{
		if (ControllerPair.UnrealControllerIndex == ControllerId)
		{
			FOculusTouchControllerState& ControllerState = ControllerPair.ControllerStates[Hand];
			if (ControllerState.bIsConnected)
			{
				if(IOculusHMDModule::IsAvailable() && ovrp_GetInitialized() && FApp::HasVRFocus())
				{
					static bool pulledHapticsDesc = false;
					if (!pulledHapticsDesc)
					{
						// Buffered haptics is currently only supported on Touch
						ovrp_GetControllerHapticsDesc2(ovrpController_RTouch, &OvrpHapticsDesc);
						pulledHapticsDesc = true;
					}

					ovrpControllerState4 OvrpControllerState;
					
					ovrpController ControllerTypes = (ovrpController)(ovrpController_Active | ovrpController_LTrackedRemote | ovrpController_RTrackedRemote);

#ifdef USE_ANDROID_INPUT
					ControllerTypes = (ovrpController)(ControllerTypes | ovrpController_Touch);
#endif

					if (OVRP_SUCCESS(ovrp_GetControllerState4(ControllerTypes, &OvrpControllerState)) &&
						(OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch | ovrpController_LTrackedRemote | ovrpController_RTrackedRemote)))
					{
						// Buffered haptics is currently only supported on Touch
						FHapticFeedbackBuffer* HapticBuffer = Values.HapticBuffer;
						if ( (OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch)) &&
							HapticBuffer && HapticBuffer->SamplingRate == OvrpHapticsDesc.SampleRateHz)
						{
							const ovrpController OvrpController = (EControllerHand(Hand) == EControllerHand::Left) ? ovrpController_LTouch : ovrpController_RTouch;

							ovrpHapticsState OvrpHapticsState;
							if (OVRP_SUCCESS(ovrp_GetControllerHapticsState2(OvrpController, &OvrpHapticsState)))
							{
								float appFrameRate = 90.f;
								ovrp_GetAppFramerate2(&appFrameRate);

								int wanttosend = (int)ceil((float)OvrpHapticsDesc.SampleRateHz / appFrameRate) + 1;
								wanttosend = FMath::Min(wanttosend, OvrpHapticsDesc.MaximumBufferSamplesCount);
								wanttosend = FMath::Max(wanttosend, OvrpHapticsDesc.MinimumBufferSamplesCount);

								if (OvrpHapticsState.SamplesQueued < OvrpHapticsDesc.MinimumSafeSamplesQueued + wanttosend) //trying to minimize latency
								{
									wanttosend = (OvrpHapticsDesc.MinimumSafeSamplesQueued + wanttosend - OvrpHapticsState.SamplesQueued);
									void *bufferToFree = NULL;
									ovrpHapticsBuffer OvrpHapticsBuffer;
									OvrpHapticsBuffer.SamplesCount = FMath::Min(wanttosend, HapticBuffer->BufferLength - HapticBuffer->SamplesSent);

									if (OvrpHapticsBuffer.SamplesCount == 0 && OvrpHapticsState.SamplesQueued == 0)
									{
										HapticBuffer->bFinishedPlaying = true;
										ControllerState.bPlayingHapticEffect = false;
									}
									else
									{
										if (OvrpHapticsDesc.SampleSizeInBytes == 1)
										{
											uint8* samples = (uint8*)FMemory::Malloc(OvrpHapticsBuffer.SamplesCount * sizeof(*samples));
											for (int i = 0; i < OvrpHapticsBuffer.SamplesCount; i++)
											{
												samples[i] = static_cast<uint8>(HapticBuffer->RawData[HapticBuffer->CurrentPtr + i] * HapticBuffer->ScaleFactor);
											}
											OvrpHapticsBuffer.Samples = bufferToFree = samples;
										}
										else if (OvrpHapticsDesc.SampleSizeInBytes == 2)
										{
											uint16* samples = (uint16*)FMemory::Malloc(OvrpHapticsBuffer.SamplesCount * sizeof(*samples));
											for (int i = 0; i < OvrpHapticsBuffer.SamplesCount; i++)
											{
												const uint32 DataIndex = HapticBuffer->CurrentPtr + (i * 2);
												const uint16* const RawData = reinterpret_cast<uint16*>(&HapticBuffer->RawData[DataIndex]);
												samples[i] = static_cast<uint16>(*RawData * HapticBuffer->ScaleFactor);
											}
											OvrpHapticsBuffer.Samples = bufferToFree = samples;
										}
										else if (OvrpHapticsDesc.SampleSizeInBytes == 4)
										{
											uint32* samples = (uint32*)FMemory::Malloc(OvrpHapticsBuffer.SamplesCount * sizeof(*samples));
											for (int i = 0; i < OvrpHapticsBuffer.SamplesCount; i++)
											{
												const uint32 DataIndex = HapticBuffer->CurrentPtr + (i * 4);
												const uint32* const RawData = reinterpret_cast<uint32*>(&HapticBuffer->RawData[DataIndex]);
												samples[i] = static_cast<uint32>(*RawData * HapticBuffer->ScaleFactor);
											}
											OvrpHapticsBuffer.Samples = bufferToFree = samples;
										}

										ovrp_SetControllerHaptics2(OvrpController, OvrpHapticsBuffer);

										if (bufferToFree)
										{
											FMemory::Free(bufferToFree);
										}

										HapticBuffer->CurrentPtr += (OvrpHapticsBuffer.SamplesCount * OvrpHapticsDesc.SampleSizeInBytes);
										HapticBuffer->SamplesSent += OvrpHapticsBuffer.SamplesCount;

										ControllerState.bPlayingHapticEffect = true;
									}
								}
							}
						} 
						else
						{
							// Buffered haptics is currently only supported on Touch
							if ( (OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch)) && (HapticBuffer) )
							{
								UE_CLOG(OVR_DEBUG_LOGGING, LogOcInput, Log, TEXT("Haptic Buffer not sampled at the correct frequency : %d vs %d"), OvrpHapticsDesc.SampleRateHz, HapticBuffer->SamplingRate);
							}
							float FreqMin, FreqMax = 0.f;
							GetHapticFrequencyRange(FreqMin, FreqMax);

							const float InitialFreq = (Values.Frequency > 0.0f) ? Values.Frequency : 1.0f;
							const float Frequency = FMath::Lerp(FreqMin, FreqMax, FMath::Clamp(InitialFreq, 0.f, 1.f));

							const float Amplitude = Values.Amplitude * GetHapticAmplitudeScale();

							if (ControllerState.HapticAmplitude != Amplitude || ControllerState.HapticFrequency != Frequency)
							{
								ControllerState.HapticAmplitude = Amplitude;
								ControllerState.HapticFrequency = Frequency;

								ovrpController OvrController = ovrpController_None;
								if (OvrpControllerState.ConnectedControllerTypes & (ovrpController_Touch))
								{
									OvrController = (EControllerHand(Hand) == EControllerHand::Left) ? ovrpController_LTouch : ovrpController_RTouch;
								}
								else if (OvrpControllerState.ConnectedControllerTypes & (ovrpController_LTrackedRemote | ovrpController_RTrackedRemote))
								{
									OvrController = (EControllerHand(Hand) == EControllerHand::Left) ? ovrpController_LTrackedRemote : ovrpController_RTrackedRemote;
								}

								ovrp_SetControllerVibration2(OvrController, Frequency, Amplitude);

								ControllerState.bPlayingHapticEffect = (Amplitude != 0.f) && (Frequency != 0.f);
							}
						}
					}
				}
			}

			break;
		}
	}
}

void FOculusInput::GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const
{
	MinFrequency = 0.f;
	MaxFrequency = 1.f;
}

float FOculusInput::GetHapticAmplitudeScale() const
{
	return 1.f;
}

uint32 FOculusInput::GetNumberOfTouchControllers() const
{
	uint32 RetVal = 0;

	for (FOculusTouchControllerPair Pair : ControllerPairs)
	{
		RetVal += (Pair.ControllerStates[0].bIsConnected ? 1 : 0);
		RetVal += (Pair.ControllerStates[1].bIsConnected ? 1 : 0);
	}

	return RetVal;
}

} // namespace OculusInput

#undef LOCTEXT_NAMESPACE
#endif	 // OCULUS_INPUT_SUPPORTED_PLATFORMS
