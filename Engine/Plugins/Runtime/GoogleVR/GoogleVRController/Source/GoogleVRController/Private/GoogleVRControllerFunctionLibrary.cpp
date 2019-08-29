// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GoogleVRControllerFunctionLibrary.h"
#include "GoogleVRController.h"
#include "GoogleVRControllerPrivate.h"
#include "InputCoreTypes.h"

UGoogleVRControllerFunctionLibrary::UGoogleVRControllerFunctionLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

FGoogleVRController* GetGoogleVRController()
{
	TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>( IMotionController::GetModularFeatureName() );
	for( auto MotionController : MotionControllers )
	{
		if (MotionController != nullptr && MotionController->GetMotionControllerDeviceTypeName() == FGoogleVRController::DeviceTypeName)
		{
			return static_cast<FGoogleVRController*>(MotionController);
		}
	}

	return nullptr;
}

EGoogleVRControllerAPIStatus UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerAPIStatus()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return (EGoogleVRControllerAPIStatus) GVRController->GetApiStatus();
	}
#endif
	return EGoogleVRControllerAPIStatus::Unknown;
}

EGoogleVRControllerState UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerState(EControllerHand Hand)
{
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetControllerState(Hand);
	}

	return EGoogleVRControllerState::Disconnected;
}

EGoogleVRControllerHandedness UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerHandedness()
{
	EGoogleVRControllerHandedness HandednessPrefsEnum = EGoogleVRControllerHandedness::Unknown;
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		int HandednessPrefsInt = GVRController->GetGVRControllerHandedness();
		if (HandednessPrefsInt == 0)
		{
			HandednessPrefsEnum = EGoogleVRControllerHandedness::RightHanded;
		}
		else if (HandednessPrefsInt == 1)
		{
			HandednessPrefsEnum = EGoogleVRControllerHandedness::LeftHanded;
		}
	}
	return HandednessPrefsEnum;
}

FVector UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerRawAccel(EControllerHand Hand)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		gvr::ControllerState* ControllerState = GVRController->GetCachedControllerState(Hand);
		if (ControllerState)
		{
			gvr_vec3f ControllerAccel = ControllerState->GetAccel();
			return FVector(ControllerAccel.x, ControllerAccel.y, ControllerAccel.z);
		}
	}
#endif

	return FVector::ZeroVector;
}

FVector UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerRawGyro(EControllerHand Hand)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		gvr::ControllerState* ControllerState = GVRController->GetCachedControllerState(Hand);
		if (ControllerState)
		{
			gvr_vec3f ControllerGyro = ControllerState->GetGyro();
			return FVector(ControllerGyro.x, ControllerGyro.y, ControllerGyro.z);
		}
	}
#endif

	return FVector::ZeroVector;
}

FRotator UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerOrientation(EControllerHand Hand)
{
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		FRotator orientation;
		FVector position;
		GVRController->GetControllerOrientationAndPosition(0, Hand, orientation, position, GVRController->GetWorldToMetersScale());
		return orientation;
	}
	return FRotator::ZeroRotator;
}


UGoogleVRControllerEventManager* UGoogleVRControllerFunctionLibrary::GetGoogleVRControllerEventManager()
{
	return UGoogleVRControllerEventManager::GetInstance();
}

bool UGoogleVRControllerFunctionLibrary::IsArmModelEnabled()
{
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetUseArmModel();
	}

	return false;
}

void UGoogleVRControllerFunctionLibrary::SetArmModelEnabled(bool bArmModelEnabled)
{
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->SetUseArmModel(bArmModelEnabled);
	}
}

FVector UGoogleVRControllerFunctionLibrary::GetArmModelPointerPositionOffset()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		const gvr_arm_model::Vector3& Position = GVRController->GetArmModelController().GetPointerPositionOffset();
		return GVRController->ConvertGvrVectorToUnreal(Position.x(), Position.y(), Position.z(), GVRController->GetWorldToMetersScale());
	}
#endif

	return FVector::ZeroVector;
}

float UGoogleVRControllerFunctionLibrary::GetArmModelAddedElbowHeight()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetAddedElbowHeight();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetArmModelAddedElbowHeight(float ElbowHeight)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetAddedElbowHeight(ElbowHeight);
	}
#endif
}

float UGoogleVRControllerFunctionLibrary::GetArmModelAddedElbowDepth()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetAddedElbowDepth();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetArmModelAddedElbowDepth(float ElbowDepth)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetAddedElbowDepth(ElbowDepth);
	}
#endif
}

float UGoogleVRControllerFunctionLibrary::GetArmModelPointerTiltAngle()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetPointerTiltAngle();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetArmModelPointerTiltAngle(float TiltAngle)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetPointerTiltAngle(TiltAngle);
	}
#endif
}

bool UGoogleVRControllerFunctionLibrary::WillArmModelUseAccelerometer()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetUseAccelerometer();
	}
#endif

	return false;
}

void UGoogleVRControllerFunctionLibrary::SetWillArmModelUseAccelerometer(bool UseAccelerometer)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetUseAccelerometer(UseAccelerometer);
	}
#endif
}

void UGoogleVRControllerFunctionLibrary::SetArmModelIsLockedToHead(bool IsLockedToHead)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if (GVRController != nullptr)
	{
		return GVRController->GetArmModelController().SetIsLockedToHead(IsLockedToHead);
	}
#endif
}

bool UGoogleVRControllerFunctionLibrary::GetArmModelIsLockedToHead()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if (GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetIsLockedToHead();
	}
#endif
	return false;
}

float UGoogleVRControllerFunctionLibrary::GetFadeDistanceFromFace()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetFadeDistanceFromFace();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetFadeDistanceFromFace(float DistanceFromFace)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetFadeDistanceFromFace(DistanceFromFace);
	}
#endif
}

float UGoogleVRControllerFunctionLibrary::GetTooltipMinDistanceFromFace()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetTooltipMinDistanceFromFace();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetTooltipMinDistanceFromFace(float DistanceFromFace)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetTooltipMinDistanceFromFace(DistanceFromFace);
	}
#endif
}

int UGoogleVRControllerFunctionLibrary::GetTooltipMaxAngleFromCamera()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetTooltipMaxAngleFromCamera();
	}
#endif

	return 0.0f;
}

void UGoogleVRControllerFunctionLibrary::SetTooltipMaxAngleFromCamera(int AngleFromCamera)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		GVRController->GetArmModelController().SetTooltipMaxAngleFromCamera(AngleFromCamera);
	}
#endif
}

float UGoogleVRControllerFunctionLibrary::GetControllerAlphaValue()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetControllerAlphaValue();
	}
#endif

	return 0.0f;
}

float UGoogleVRControllerFunctionLibrary::GetTooltipAlphaValue()
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetArmModelController().GetTooltipAlphaValue();
	}
#endif

	return 0.0f;
}

bool UGoogleVRControllerFunctionLibrary::GetBatteryCharging(EControllerHand Hand)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetBatteryCharging(Hand);
	}
#endif

	return false;
}

EGoogleVRControllerBatteryLevel UGoogleVRControllerFunctionLibrary::GetBatteryLevel(EControllerHand Hand)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetBatteryLevel(Hand);
	}
#endif

	return EGoogleVRControllerBatteryLevel::Unknown;
}

int64_t UGoogleVRControllerFunctionLibrary::GetLastBatteryTimestamp(EControllerHand Hand)
{
#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS
	FGoogleVRController* GVRController = GetGoogleVRController();
	if(GVRController != nullptr)
	{
		return GVRController->GetLastBatteryTimestamp(Hand);
	}
#endif

	return 0;
}
