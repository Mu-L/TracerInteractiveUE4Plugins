// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <openxr/openxr.h>
#include <openxr/openxr_reflection.h>

#define XR_ENUM_CASE_STR(name, val) case name: return TEXT(#name);
constexpr const TCHAR* OpenXRResultToString(XrResult e)
{
	switch (e)
	{
		XR_LIST_ENUM_XrResult(XR_ENUM_CASE_STR)
		default: return TEXT("Unknown");
	}
}

#if DO_CHECK
#define XR_ENSURE(x) [] (XrResult Result) \
	{ \
		return ensureMsgf(XR_SUCCEEDED(Result), TEXT("OpenXR call failed with result %s"), OpenXRResultToString(Result)); \
	} (x)
#else
#define XR_ENSURE(x) XR_SUCCEEDED(x)
#endif

FORCEINLINE FQuat ToFQuat(XrQuaternionf Quat)
{
	return FQuat(-Quat.z, Quat.x, Quat.y, -Quat.w);
}

FORCEINLINE XrQuaternionf ToXrQuat(FQuat Quat)
{
	return XrQuaternionf{ Quat.Y, Quat.Z, -Quat.X, -Quat.W };
}

FORCEINLINE FVector ToFVector(XrVector3f Vector, float Scale = 1.0f)
{
	return FVector(-Vector.z * Scale, Vector.x * Scale, Vector.y * Scale);
}

FORCEINLINE XrVector3f ToXrVector(FVector Vector, float Scale = 1.0f)
{
	if (Vector.IsZero())
		return XrVector3f{ 0.0f, 0.0f, 0.0f };

	return XrVector3f{ Vector.Y / Scale, Vector.Z / Scale, -Vector.X / Scale };
}

FORCEINLINE FTransform ToFTransform(XrPosef Transform, float Scale = 1.0f)
{
	return FTransform(ToFQuat(Transform.orientation), ToFVector(Transform.position, Scale));
}

FORCEINLINE XrPosef ToXrPose(FTransform Transform, float Scale = 1.0f)
{
	return XrPosef{ ToXrQuat(Transform.GetRotation()), ToXrVector(Transform.GetTranslation(), Scale) };
}

FORCEINLINE FTimespan ToFTimespan(XrTime Time)
{
	// XrTime is a nanosecond counter, FTimespan is a 100-nanosecond counter. 
	// We are losing some precision here.
	return FTimespan((Time + 50) / 100); 
}

FORCEINLINE XrTime ToXrTime(FTimespan Time)
{
	return Time.GetTicks() * 100;
}

/** List all OpenXR global entry points used by Unreal. */
#define ENUM_XR_ENTRYPOINTS_GLOBAL(EnumMacro) \
	EnumMacro(PFN_xrEnumerateApiLayerProperties,xrEnumerateApiLayerProperties) \
	EnumMacro(PFN_xrEnumerateInstanceExtensionProperties,xrEnumerateInstanceExtensionProperties) \
	EnumMacro(PFN_xrCreateInstance,xrCreateInstance)

/** List all OpenXR instance entry points used by Unreal. */
#define ENUM_XR_ENTRYPOINTS(EnumMacro) \
	EnumMacro(PFN_xrDestroyInstance,xrDestroyInstance) \
	EnumMacro(PFN_xrGetInstanceProperties,xrGetInstanceProperties) \
	EnumMacro(PFN_xrPollEvent,xrPollEvent) \
	EnumMacro(PFN_xrResultToString,xrResultToString) \
	EnumMacro(PFN_xrStructureTypeToString,xrStructureTypeToString) \
	EnumMacro(PFN_xrGetSystem,xrGetSystem) \
	EnumMacro(PFN_xrGetSystemProperties,xrGetSystemProperties) \
	EnumMacro(PFN_xrEnumerateEnvironmentBlendModes,xrEnumerateEnvironmentBlendModes) \
	EnumMacro(PFN_xrCreateSession,xrCreateSession) \
	EnumMacro(PFN_xrDestroySession,xrDestroySession) \
	EnumMacro(PFN_xrEnumerateReferenceSpaces,xrEnumerateReferenceSpaces) \
	EnumMacro(PFN_xrCreateReferenceSpace,xrCreateReferenceSpace) \
	EnumMacro(PFN_xrGetReferenceSpaceBoundsRect,xrGetReferenceSpaceBoundsRect) \
	EnumMacro(PFN_xrCreateActionSpace,xrCreateActionSpace) \
	EnumMacro(PFN_xrLocateSpace,xrLocateSpace) \
	EnumMacro(PFN_xrDestroySpace,xrDestroySpace) \
	EnumMacro(PFN_xrEnumerateViewConfigurations,xrEnumerateViewConfigurations) \
	EnumMacro(PFN_xrGetViewConfigurationProperties,xrGetViewConfigurationProperties) \
	EnumMacro(PFN_xrEnumerateViewConfigurationViews,xrEnumerateViewConfigurationViews) \
	EnumMacro(PFN_xrEnumerateSwapchainFormats,xrEnumerateSwapchainFormats) \
	EnumMacro(PFN_xrCreateSwapchain,xrCreateSwapchain) \
	EnumMacro(PFN_xrDestroySwapchain,xrDestroySwapchain) \
	EnumMacro(PFN_xrEnumerateSwapchainImages,xrEnumerateSwapchainImages) \
	EnumMacro(PFN_xrAcquireSwapchainImage,xrAcquireSwapchainImage) \
	EnumMacro(PFN_xrWaitSwapchainImage,xrWaitSwapchainImage) \
	EnumMacro(PFN_xrReleaseSwapchainImage,xrReleaseSwapchainImage) \
	EnumMacro(PFN_xrBeginSession,xrBeginSession) \
	EnumMacro(PFN_xrEndSession,xrEndSession) \
	EnumMacro(PFN_xrRequestExitSession,xrRequestExitSession) \
	EnumMacro(PFN_xrWaitFrame,xrWaitFrame) \
	EnumMacro(PFN_xrBeginFrame,xrBeginFrame) \
	EnumMacro(PFN_xrEndFrame,xrEndFrame) \
	EnumMacro(PFN_xrLocateViews,xrLocateViews) \
	EnumMacro(PFN_xrStringToPath,xrStringToPath) \
	EnumMacro(PFN_xrPathToString,xrPathToString) \
	EnumMacro(PFN_xrCreateActionSet,xrCreateActionSet) \
	EnumMacro(PFN_xrDestroyActionSet,xrDestroyActionSet) \
	EnumMacro(PFN_xrCreateAction,xrCreateAction) \
	EnumMacro(PFN_xrDestroyAction,xrDestroyAction) \
	EnumMacro(PFN_xrSuggestInteractionProfileBindings,xrSuggestInteractionProfileBindings) \
	EnumMacro(PFN_xrAttachSessionActionSets,xrAttachSessionActionSets) \
	EnumMacro(PFN_xrGetCurrentInteractionProfile,xrGetCurrentInteractionProfile) \
	EnumMacro(PFN_xrGetActionStateBoolean,xrGetActionStateBoolean) \
	EnumMacro(PFN_xrGetActionStateFloat,xrGetActionStateFloat) \
	EnumMacro(PFN_xrGetActionStateVector2f,xrGetActionStateVector2f) \
	EnumMacro(PFN_xrGetActionStatePose,xrGetActionStatePose) \
	EnumMacro(PFN_xrSyncActions,xrSyncActions) \
	EnumMacro(PFN_xrEnumerateBoundSourcesForAction,xrEnumerateBoundSourcesForAction) \
	EnumMacro(PFN_xrGetInputSourceLocalizedName,xrGetInputSourceLocalizedName) \
	EnumMacro(PFN_xrApplyHapticFeedback,xrApplyHapticFeedback) \
	EnumMacro(PFN_xrStopHapticFeedback,xrStopHapticFeedback)

/** Declare all XR functions. */
#define DECLARE_XR_ENTRYPOINTS(Type,Func) extern Type OPENXRHMD_API Func;
ENUM_XR_ENTRYPOINTS_GLOBAL(DECLARE_XR_ENTRYPOINTS);
ENUM_XR_ENTRYPOINTS(DECLARE_XR_ENTRYPOINTS);
DECLARE_XR_ENTRYPOINTS(PFN_xrGetInstanceProcAddr,xrGetInstanceProcAddr)
#undef DECLARE_XR_ENTRYPOINTS

/**
 * Initialize essential OpenXR functions.
 * @returns true if initialization was successful.
 */
bool PreInitOpenXRCore(PFN_xrGetInstanceProcAddr InGetProcAddr);

/**
 * Initialize core OpenXR functions.
 * @returns true if initialization was successful.
 */
bool InitOpenXRCore(XrInstance Instance);
