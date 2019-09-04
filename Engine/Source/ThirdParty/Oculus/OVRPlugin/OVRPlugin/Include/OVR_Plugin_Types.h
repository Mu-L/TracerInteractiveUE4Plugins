/************************************************************************************

Copyright (c) Facebook Technologies, LLC and its affiliates.  All rights reserved.

Licensed under the Oculus SDK License Version 3.5 (the "License");
you may not use the Oculus SDK except in compliance with the License,
which is provided at the time of installation or download, or which
otherwise accompanies this software in either electronic or hard copy form.

You may obtain a copy of the License at

https://developer.oculus.com/licenses/sdk-3.5/

Unless required by applicable law or agreed to in writing, the Oculus SDK
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************************/

#ifndef OVR_Plugin_Types_h
#define OVR_Plugin_Types_h

#if !defined(OVRP_STRINGIFY)
#define OVRP_STRINGIFYIMPL(x) #x
#define OVRP_STRINGIFY(x) OVRP_STRINGIFYIMPL(x)
#endif

#define OVRP_MAJOR_VERSION 1
#define OVRP_MINOR_VERSION 37
#define OVRP_PATCH_VERSION 0

#define OVRP_VERSION OVRP_MAJOR_VERSION, OVRP_MINOR_VERSION, OVRP_PATCH_VERSION
#define OVRP_VERSION_STR OVRP_STRINGIFY(OVRP_MAJOR_VERSION.OVRP_MINOR_VERSION.OVRP_PATCH_VERSION)




#define OVRP_VERSION_CHANNEL "Release"


#define OVRP_CURRENT_FRAMEINDEX -1

#ifndef OVRP_EXPORT
#ifdef _WIN32
#define OVRP_EXPORT __declspec(dllexport)
#else
#define OVRP_EXPORT
#endif
#endif

#if defined ANDROID || defined __linux__
#define __cdecl
#endif

#ifdef __cplusplus
#define OVRP_REF(Type) Type&
#define OVRP_CONSTREF(Type) const Type&
#define OVRP_DEFAULTVALUE(Value) = Value
#else
#define OVRP_REF(Type) Type*
#define OVRP_CONSTREF(Type) const Type*
#define OVRP_DEFAULTVALUE(Value)
#endif

#ifndef OVRP_MIXED_REALITY_PRIVATE
#define OVRP_MIXED_REALITY_PRIVATE 0
#endif

/// True or false
enum {
  ovrpBool_False = 0,
  ovrpBool_True = 1,
};
typedef int ovrpBool;

/// Byte
typedef unsigned char ovrpByte;

/// UInt16
typedef unsigned short ovrpUInt16;

/// Int64
typedef long long ovrpInt64;

/// Success and failure
typedef enum {
  /// Success
  ovrpSuccess = 0,

  /// Failure
  ovrpFailure = -1000,
  ovrpFailure_InvalidParameter = -1001,
  ovrpFailure_NotInitialized = -1002,
  ovrpFailure_InvalidOperation = -1003,
  ovrpFailure_Unsupported = -1004,
  ovrpFailure_NotYetImplemented = -1005,
  ovrpFailure_OperationFailed = -1006,
  ovrpFailure_InsufficientSize = -1007,
  ovrpFailure_DataIsInvalid = -1008,
} ovrpResult;

#define OVRP_SUCCESS(result) ((result) >= 0)
#define OVRP_FAILURE(result) ((result) < 0)

/// Initialization flags
typedef enum {
  /// Start GearVR battery and volume receivers
  ovrpInitializeFlag_StartGearVRReceivers = (1 << 0),
  /// Supports 2D/3D switching
  ovrpInitializeFlag_SupportsVRToggle = (1 << 1),
  /// Supports Life Cycle Focus (Dash)
  ovrpInitializeFlag_FocusAware = (1 << 2),
  /// DEPRECATED - Turn off Legacy Core Affinity Patch
  /// Background: Some legacy unity versions set thread affinities wrong on newer hardware like Oculus Go
  /// We need patch it in the runtime for published legacy apps.
  /// This flag will be passed from fixed Unity versions explicitly, so we can skip the runtime patch mechanism since we already have proper fixes.
  /// Deprecated Background: Several Unity versions incorrectly indicated they handled applying thread affinity, so this flag has been deprecated
  /// in order to fallback to runtime thread affinity handling. In the future, a new flag will be introduced to allow engine opt-out of
  /// runtime affinity handling.
  ovrpInitializeFlag_NoLegacyCoreAffinityPatch = (1 << 3), // DEPRECATED

  /// Allow to use sRGB frame buffer, we use it as an initialization flag because we need make the window surface
  /// sRGB compilable, this can't be changed after window created.
  ovrpInitializeFlag_SupportSRGBFrameBuffer = (1 << 4),






  ovrpInitializeFlag_EnumSize = 0x7fffffff

} ovrpInitializeFlags;


/// Thread Performance
typedef enum {
  ovrpThreadPef_DeadLine_Normal = 0,
  ovrpThreadPef_DeadLine_Hard = 1,
  ovrpThreadPef_DeadLine_Soft = 2,
  ovrpThreadPef_EnumSize = 0x7fffffff
} ovrpThreadPerf;

/// Identifies an eye in a stereo pair.
typedef enum {
  ovrpEye_Center = -2,
  ovrpEye_None = -1,
  ovrpEye_Left = 0,
  ovrpEye_Right = 1,
  ovrpEye_Count,
  ovrpEye_EnumSize = 0x7fffffff
} ovrpEye;

/// Identifies a hand.
typedef enum {
  ovrpHand_None = -1,
  ovrpHand_Left = 0,
  ovrpHand_Right = 1,
  ovrpHand_Count,
  ovrpHand_EnumSize = 0x7fffffff
} ovrpHand;

/// Identifies a tracked device object.
typedef enum {
  ovrpDeviceObject_None = -1,
  ovrpDeviceObject_Zero = 0,
  ovrpDeviceObject_Count,
  ovrpDeviceObject_EnumSize = 0x7fffffff
} ovrpDeviceObject;

/// Identifies a tracking sensor.
typedef enum {
  ovrpTracker_None = -1,
  ovrpTracker_Zero = 0,
  ovrpTracker_One = 1,
  ovrpTracker_Two = 2,
  ovrpTracker_Three = 3,
  ovrpTracker_Count,
  ovrpTracker_EnumSize = 0x7fffffff
} ovrpTracker;

/// Identifies a tracked VR Node.
typedef enum {
  ovrpNode_None = -1,
  ovrpNode_EyeLeft = 0,
  ovrpNode_EyeRight = 1,
  ovrpNode_EyeCenter = 2,
  ovrpNode_HandLeft = 3,
  ovrpNode_HandRight = 4,
  ovrpNode_TrackerZero = 5,
  ovrpNode_TrackerOne = 6,
  ovrpNode_TrackerTwo = 7,
  ovrpNode_TrackerThree = 8,
  ovrpNode_Head = 9,
  ovrpNode_DeviceObjectZero = 10,
  ovrpNode_Count,
  ovrpNode_EnumSize = 0x7fffffff
} ovrpNode;

/// Identifies a tracking origin
typedef enum {
  ovrpTrackingOrigin_EyeLevel = 0,
  ovrpTrackingOrigin_FloorLevel = 1,
  ovrpTrackingOrigin_Stage = 2,
  ovrpTrackingOrigin_Count,
  ovrpTrackingOrigin_EnumSize = 0x7fffffff
} ovrpTrackingOrigin;

/// The charge status of a battery.
typedef enum {
  ovrpBatteryStatus_Charging,
  ovrpBatteryStatus_Discharging,
  ovrpBatteryStatus_Full,
  ovrpBatteryStatus_NotCharging,
  ovrpBatteryStatus_Unknown,
  ovrpBatteryStatus_EnumSize = 0x7fffffff
} ovrpBatteryStatus;

//Handedness of user as specified in the mobile device
typedef enum {
    ovrpHandedness_Unsupported = 0,
    ovrpHandedness_LeftHanded = 1,
    ovrpHandedness_RightHanded = 2
} ovrpHandedness;

/// An oculus platform UI.
typedef enum {
  ovrpUI_None = -1,
  ovrpUI_GlobalMenu = 0,
  ovrpUI_ConfirmQuit,
  ovrpUI_GlobalMenuTutorial, // Deprecated
  ovrpUI_EnumSize = 0x7fffffff
} ovrpUI;

/// A geographical region associated with the current system device.
typedef enum {
  ovrpSystemRegion_Unspecified,
  ovrpSystemRegion_Japan,
  ovrpSystemRegion_China,
  ovrpSystemRegion_EnumSize = 0x7fffffff
} ovrpSystemRegion;

typedef enum {
  ovrpSystemHeadset_None,
  ovrpSystemHeadset_GearVR_R320,  // Note4 Innovator
  ovrpSystemHeadset_GearVR_R321,  // S6 Innovator
  ovrpSystemHeadset_GearVR_R322,  // GearVR Commercial 1
  ovrpSystemHeadset_GearVR_R323,  // GearVR Commercial 2 (USB Type C)
  ovrpSystemHeadset_GearVR_R324,  // GearVR Commercial 3 (USB Type C)
  ovrpSystemHeadset_GearVR_R325,  // GearVR Commercial 4 (USB Type C)
  ovrpSystemHeadset_Oculus_Go,    // Oculus Go Commercial 1
  ovrpSystemHeadset_Oculus_Quest, // Oculus Quest

  ovrpSystemHeadset_Rift_DK1 = 0x1000,
  ovrpSystemHeadset_Rift_DK2,
  ovrpSystemHeadset_Rift_CV1,
  ovrpSystemHeadset_Rift_CB,
  ovrpSystemHeadset_Rift_S,
  ovrpSystemHeadset_EnumSize = 0x7fffffff
} ovrpSystemHeadset;

/// These types are used to hide platform-specific details when passing
/// render device, OS, and texture data to the API.
///
/// The benefit of having these wrappers versus platform-specific API functions is
/// that they allow game glue code to be portable. A typical example is an
/// engine that has multiple back ends, say GL and D3D. Portable code that calls
/// these back ends may also use LibOVR. To do this, back ends can be modified
/// to return portable types such as ovrTexture and ovrRenderAPIConfig.
typedef enum {
  ovrpRenderAPI_None,
  ovrpRenderAPI_OpenGL,
  ovrpRenderAPI_Android_GLES, // Deprecated, use ovrpRenderAPI_OpenGL instead
  ovrpRenderAPI_D3D9, // Deprecated, unsupported
  ovrpRenderAPI_D3D10, // Deprecated, unsupported
  ovrpRenderAPI_D3D11,
  ovrpRenderAPI_D3D12,
  ovrpRenderAPI_Vulkan,
  ovrpRenderAPI_Count,
  ovrpRenderAPI_EnumSize = 0x7fffffff
} ovrpRenderAPIType;

/// Identifies a controller button.
typedef enum {
  ovrpButton_None = 0,
  ovrpButton_A = 0x00000001,
  ovrpButton_B = 0x00000002,
  ovrpButton_X = 0x00000100,
  ovrpButton_Y = 0x00000200,
  ovrpButton_Up = 0x00010000,
  ovrpButton_Down = 0x00020000,
  ovrpButton_Left = 0x00040000,
  ovrpButton_Right = 0x00080000,
  ovrpButton_Start = 0x00100000,
  ovrpButton_Back = 0x00200000,
  ovrpButton_LShoulder = 0x00000800,
  ovrpButton_LThumb = 0x00000400,
  ovrpButton_LTouchpad = 0x40000000,
  ovrpButton_RShoulder = 0x00000008,
  ovrpButton_RThumb = 0x00000004,
  ovrpButton_RTouchpad = 0x80000000,
  ovrpButton_VolUp = 0x00400000,
  ovrpButton_VolDown = 0x00800000,
  ovrpButton_Home = 0x01000000,
  ovrpButton_EnumSize = 0x7fffffff
} ovrpButton;

/// Identifies a controller touch.
typedef enum {
  ovrpTouch_None = 0,
  ovrpTouch_A = ovrpButton_A,
  ovrpTouch_B = ovrpButton_B,
  ovrpTouch_X = ovrpButton_X,
  ovrpTouch_Y = ovrpButton_Y,
  ovrpTouch_LIndexTrigger = 0x00001000,
  ovrpTouch_LThumb = ovrpButton_LThumb,
  ovrpTouch_LThumbRest = 0x00000800,
  ovrpTouch_LTouchpad = ovrpButton_LTouchpad,
  ovrpTouch_RIndexTrigger = 0x00000010,
  ovrpTouch_RThumb = ovrpButton_RThumb,
  ovrpTouch_RThumbRest = 0x00000008,
  ovrpTouch_RTouchpad = ovrpButton_RTouchpad,
  ovrpTouch_EnumSize = 0x7fffffff
} ovrpTouch;

/// Identifies a controller near touch.
typedef enum {
  ovrpNearTouch_None = 0,
  ovrpNearTouch_LIndexTrigger = 0x00000001,
  ovrpNearTouch_LThumbButtons = 0x00000002,
  ovrpNearTouch_RIndexTrigger = 0x00000004,
  ovrpNearTouch_RThumbButtons = 0x00000008,
  ovrpNearTouch_EnumSize = 0x7fffffff
} ovrpNearTouch;

/// Identifies a controller.
typedef enum {
  ovrpController_None = 0,
  ovrpController_LTouch = 0x01,
  ovrpController_RTouch = 0x02,
  ovrpController_Touch = 0x03,
  ovrpController_Remote = 0x04,
  ovrpController_Gamepad = 0x10,
  ovrpController_Touchpad = 0x08000000,
  ovrpController_LTrackedRemote = 0x01000000,
  ovrpController_RTrackedRemote = 0x02000000,
  ovrpController_Active = 0x80000000,
  ovrpController_EnumSize = 0x7fffffff
} ovrpController;

/// Used to specify recentering behavior.
typedef enum {
  /// Recenter all default axes as defined by the current tracking origin type.
  ovrpRecenterFlag_Default = 0x00000000,
  /// Recenter only controllers that require drift correction.
  ovrpRecenterFlag_Controllers = 0x40000000,
  /// Clear the ShouldRecenter flag and leave all axes unchanged. Useful for apps that perform
  /// custom recentering logic.
  ovrpRecenterFlag_IgnoreAll = 0x80000000,
  ovrpRecenterFlag_EnumSize = 0x7fffffff
} ovrpRecenterFlag;

/// Logging levels
typedef enum {
  ovrpLogLevel_Debug = 0,
  ovrpLogLevel_Info = 1,
  ovrpLogLevel_Error = 2,
  ovrpLogLevel_EnumSize = 0x7fffffff
} ovrpLogLevel;

/// Foveation levels
///
/// Levels should be consecutive integer enums, otherwise change GetTiledMultiResLevel
/// and SetTiledMultiResLevel to work without that assumption
typedef enum {
  ovrpTiledMultiResLevel_Off = 0,
  ovrpTiledMultiResLevel_LMSLow = 1,
  ovrpTiledMultiResLevel_LMSMedium = 2,
  ovrpTiledMultiResLevel_LMSHigh = 3,
  ovrpTiledMultiResLevel_LMSHighTop = 4,
  ovrpTiledMultiResLevel_EnumSize = 0x7fffffff
} ovrpTiledMultiResLevel;

#if defined(__arm__)
typedef void(* ovrpLogCallback)(ovrpLogLevel, const char*);
#else
typedef void(__cdecl* ovrpLogCallback)(ovrpLogLevel, const char*);
#endif

typedef struct {
  int MajorVersion;
  int MinorVersion;
  int PatchVersion;
} ovrpVersion;

typedef struct {
  float LatencyRender;
  float LatencyTimewarp;
  float LatencyPostPresent;
  float ErrorRender;
  float ErrorTimewarp;
} ovrpAppLatencyTimings;

enum { ovrpAppPerfFrameStatsMaxCount = 5 };

/// App Perf Frame Stats
typedef struct {
  int HmdVsyncIndex;

  int AppFrameIndex;
  int AppDroppedFrameCount;
  float AppMotionToPhotonLatency;
  float AppQueueAheadTime;
  float AppCpuElapsedTime;
  float AppGpuElapsedTime;

  int CompositorFrameIndex;
  int CompositorDroppedFrameCount;
  float CompositorLatency;
  float CompositorCpuElapsedTime;
  float CompositorGpuElapsedTime;
  float CompositorCpuStartToGpuEndElapsedTime;
  float CompositorGpuEndToVsyncElapsedTime;
} ovrpAppPerfFrameStats;

/// App Perf Stats
typedef struct {
  ovrpAppPerfFrameStats FrameStats[ovrpAppPerfFrameStatsMaxCount];
  int FrameStatsCount;
  ovrpBool AnyFrameStatsDropped;
  float AdaptiveGpuPerformanceScale;
} ovrpAppPerfStats;

/// Cross-platform perf metrics
typedef enum {
  ovrpPerfMetrics_App_CpuTime_Float = 0,
  ovrpPerfMetrics_App_GpuTime_Float = 1,
  ovrpPerfMetrics_App_MotionToPhotonLatencyTime_Float_DEPRECATED = 2,

  ovrpPerfMetrics_Compositor_CpuTime_Float = 3,
  ovrpPerfMetrics_Compositor_GpuTime_Float = 4,
  ovrpPerfMetrics_Compositor_DroppedFrameCount_Int = 5,
  ovrpPerfMetrics_Compositor_LatencyTime_Float_DEPRECATED = 6,

  ovrpPerfMetrics_System_GpuUtilPercentage_Float = 7,
  ovrpPerfMetrics_System_CpuUtilAveragePercentage_Float = 8,
  ovrpPerfMetrics_System_CpuUtilWorstPercentage_Float = 9,

  // 1.32.0
  ovrpPerfMetrics_Device_CpuClockFrequencyInMHz_Float = 10,
  ovrpPerfMetrics_Device_GpuClockFrequencyInMHz_Float = 11,
  ovrpPerfMetrics_Device_CpuClockLevel_Int = 12,
  ovrpPerfMetrics_Device_GpuClockLevel_Int = 13,

  ovrpPerfMetrics_Count,
  ovrpPerfMetrics_Max = 0x7fffffff,
} ovrpPerfMetrics;

/// A 2D size with integer components.
typedef struct { int w, h; } ovrpSizei;

/// A 2D size with float components.
typedef struct { float w, h; } ovrpSizef;

/// A 2D vector with integer components.
typedef struct { int x, y; } ovrpVector2i;

/// A 2D vector with float components.
typedef struct { float x, y; } ovrpVector2f;

/// A 3D vector with float components.
typedef struct { float x, y, z; } ovrpVector3f;

/// A 4D vector with float components.
typedef struct { float x, y, z, w; } ovrpVector4f;

/// A quaternion rotation.
typedef struct { float x, y, z, w; } ovrpQuatf;

/// Row-major 4x4 matrix.
typedef struct { float M[4][4]; } ovrpMatrix4f;

/// Position and orientation together.
typedef struct {
  ovrpQuatf Orientation;
  ovrpVector3f Position;
} ovrpPosef;

/// Position and orientation together.
typedef struct {
  ovrpPosef Pose;
  ovrpVector3f Velocity;
  ovrpVector3f Acceleration;
  ovrpVector3f AngularVelocity;
  ovrpVector3f AngularAcceleration;
  double Time;
} ovrpPoseStatef;

/// Asymmetric fov port
typedef struct {
  float UpTan;
  float DownTan;
  float LeftTan;
  float RightTan;
} ovrpFovf;

/// Asymmetric frustum for a camera.
typedef struct {
  /// Near clip plane.
  float zNear;
  /// Far clip plane.
  float zFar;
  ovrpFovf Fov;
} ovrpFrustum2f;

/// A 2D rectangle with a position and size as integers.
typedef struct {
  ovrpVector2i Pos;
  ovrpSizei Size;
} ovrpRecti;

/// A 2D rectangle with a position and size as floats.
typedef struct {
  ovrpVector2f Pos;
  ovrpSizef Size;
} ovrpRectf;

typedef struct {
  ovrpRectf LeftRect;
  ovrpRectf RightRect;
  ovrpVector4f LeftScaleBias;
  ovrpVector4f RightScaleBias;
} ovrpTextureRectMatrixf;

typedef struct {
	float WarpLeft;
	float WarpRight;
	float WarpUp;
	float WarpDown;
	float SizeLeft;
	float SizeRight;
	float SizeUp;
	float SizeDown;
} ovrpOctilinearLayout;

typedef struct { float r, g, b, a; } ovrpColorf;

/// Describes Input State for use with Gamepads and Oculus Controllers.
typedef struct {
  unsigned int ConnectedControllerTypes;
  unsigned int Buttons;
  unsigned int Touches;
  unsigned int NearTouches;
  float IndexTrigger[2];
  float HandTrigger[2];
  ovrpVector2f Thumbstick[2];
  ovrpVector2f Touchpad[2];
  unsigned char BatteryPercentRemaining[2];
  unsigned char RecenterCount[2];
  unsigned char Reserved[28];
} ovrpControllerState4;

/// Describes Haptics Buffer for use with Oculus Controllers.
typedef struct {
  const void* Samples;
  int SamplesCount;
} ovrpHapticsBuffer;

typedef struct {
  int SamplesAvailable;
  int SamplesQueued;
} ovrpHapticsState;

typedef struct {
  int SampleRateHz;
  int SampleSizeInBytes;
  int MinimumSafeSamplesQueued;
  int MinimumBufferSamplesCount;
  int OptimalBufferSamplesCount;
  int MaximumBufferSamplesCount;
} ovrpHapticsDesc;

/// Boundary types that specify a surface in the boundary system
typedef enum {
  /// Outer boundary - closely represents user setup walls, floor and ceiling
  ovrpBoundary_Outer = 0x0001,
  /// Play area - smaller convex area inside outer boundary where gameplay happens
  ovrpBoundary_PlayArea = 0x0100,
} ovrpBoundaryType;

/// Contains boundary test information
typedef struct {
  /// Indicates if the boundary system is being triggered and visible
  ovrpBool IsTriggering;
  /// Distance to the closest play area or outer boundary surface
  float ClosestDistance;
  /// Closest point in the surface
  ovrpVector3f ClosestPoint;
  /// Normal of the closest point
  ovrpVector3f ClosestPointNormal;
} ovrpBoundaryTestResult;

/// Boundary system look and feel
typedef struct {
  // Modulate color and alpha (color, brightness and opacity)
  ovrpColorf Color;
} ovrpBoundaryLookAndFeel;

/// Boundary system geometry
typedef struct {
  /// The boundary type that the geometry represents.
  ovrpBoundaryType BoundaryType;
  /// A pointer to a clock-wise ordered array of points. Max count of 256.
  ovrpVector3f Points[256];
  /// The number of points. Max count of 256.
  int PointsCount;
} ovrpBoundaryGeometry;

typedef struct {
  /// Distance between eyes.
  float InterpupillaryDistance;
  /// Eye height relative to the ground.
  float EyeHeight;
  /// Eye offset forward from the head center at EyeHeight.
  float HeadModelDepth;
  /// Neck joint offset down from the head center at EyeHeight.
  float HeadModelHeight;
} ovrpHeadModelParms;

typedef enum { ovrpFunctionEndFrame = 0, ovrpFunctionCreateTexture } ovrpFunctionType;

/// Camera status
typedef enum {
  ovrpCameraStatus_None,
  ovrpCameraStatus_Connected,
  ovrpCameraStatus_Calibrating,
  ovrpCameraStatus_CalibrationFailed,
  ovrpCameraStatus_Calibrated,
  ovrpCameraStatus_EnumSize = 0x7fffffff
} ovrpCameraStatus;

/// Camera intrinsics
typedef struct {
  ovrpBool IsValid;
  double LastChangedTimeSeconds;
  ovrpFovf FOVPort;
  float VirtualNearPlaneDistanceMeters;
  float VirtualFarPlaneDistanceMeters;
  ovrpSizei ImageSensorPixelResolution;
} ovrpCameraIntrinsics;

/// Camera extrinsics
typedef struct {
  ovrpBool IsValid;
  double LastChangedTimeSeconds;
  ovrpCameraStatus CameraStatus;
  ovrpNode AttachedToNode;
  ovrpPosef RelativePose;
} ovrpCameraExtrinsics;

#define OVRP_EXTERNAL_CAMERA_NAME_SIZE 32

#if !OVRP_MIXED_REALITY_PRIVATE
/// Unified camera device types
typedef enum {
  ovrpCameraDevice_None = 0,
  ovrpCameraDevice_WebCamera_First = 100,
  ovrpCameraDevice_WebCamera0 = ovrpCameraDevice_WebCamera_First + 0,
  ovrpCameraDevice_WebCamera1 = ovrpCameraDevice_WebCamera_First + 1,
  ovrpCameraDevice_WebCamera_Last = ovrpCameraDevice_WebCamera1,
  ovrpCameraDevice_ZEDStereoCamera = 300,
  ovrpCameraDevice_EnumSize = 0x7fffffff
} ovrpCameraDevice;
#endif

typedef enum {
  ovrpCameraDeviceDepthSensingMode_Standard = 0,
  ovrpCameraDeviceDepthSensingMode_Fill,
  ovrpCameraDeviceDepthSensingMode_EnumSize = 0x7fffffff
} ovrpCameraDeviceDepthSensingMode;

typedef enum {
  ovrpCameraDeviceDepthQuality_Low = 0,
  ovrpCameraDeviceDepthQuality_Medium,
  ovrpCameraDeviceDepthQuality_High,
  ovrpCameraDeviceDepthQuality_EnumSize = 0x7fffffff
} ovrpCameraDeviceDepthQuality;

typedef struct {
  float fx; /* Focal length in pixels along x axis. */
  float fy; /* Focal length in pixels along y axis. */
  float cx; /* Optical center along x axis, defined in pixels (usually close to width/2). */
  float cy; /* Optical center along y axis, defined in pixels (usually close to height/2). */
  double disto[5]; /* Distortion factor : [ k1, k2, p1, p2, k3 ]. Radial (k1,k2,k3) and Tangential (p1,p2) distortion.*/
  float v_fov; /* Vertical field of view after stereo rectification, in degrees. */
  float h_fov; /* Horizontal field of view after stereo rectification, in degrees.*/
  float d_fov; /* Diagonal field of view after stereo rectification, in degrees.*/
  int w; /* Resolution width */
  int h; /* Resolution height */
} ovrpCameraDeviceIntrinsicsParameters;

const static ovrpPosef s_identityPose = {{0, 0, 0, 1}, {0, 0, 0}};
const static ovrpPoseStatef s_identityPoseState =
    {{{0, 0, 0, 1}, {0, 0, 0}}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0};
const static ovrpFrustum2f s_identityFrustum2 = {0, 0, {0, 0, 0, 0}};
const static ovrpVector3f s_vec3Zero = {0, 0, 0};
const static ovrpVector2f s_vec2Zero = {0, 0};
const static ovrpVector3f s_vec3One = {1, 1, 1};
const static ovrpQuatf s_identifyQuat = {0, 0, 0, 1};
const static ovrpCameraIntrinsics s_invalidCameraIntrinsics = {ovrpBool_False, -1, {0, 0, 0, 0}, 0, 0, {0, 0}};
const static ovrpCameraExtrinsics s_invalidCameraExtrinsics = {ovrpBool_False,
                                                               -1,
                                                               ovrpCameraStatus_None,
                                                               ovrpNode_None,
                                                               {{0, 0, 0, 1}, {0, 0, 0}}};

/// Texture handle which can be cast to GLuint, VkImage, ID3D11Texture2D*, or ID3D12Resource*
typedef unsigned long long ovrpTextureHandle;

/// Flags passed to ovrp_SetupDistortionWindow.
typedef enum {
  ovrpDistortionWindowFlag_None = 0x00000000,
  /// If true, the distortion window and eye buffers are set up to handle DRM-protected content.
  ovrpDistortionWindowFlag_Protected = 0x00000001,
  /// If true, the compositor's graphics device skips error checking to improve performance.
  ovrpDistortionWindowFlag_NoErrorContext = 0x00000002,
  ovrpDistortionWindowFlag_EnumSize = 0x7fffffff
} ovrpDistortionWindowFlag;

/// A timestep type corresponding to a use case for tracking data.
typedef enum {
  /// Updated from game thread at end of frame, to hand-off state to Render thread.
  ovrpStep_Render = -1,
  /// Updated from physics thread, once per simulation step.
  ovrpStep_Physics = 0,
  ovrpStep_EnumSize = 0x7fffffff
} ovrpStep;

typedef enum {
  ovrpShape_Quad = 0,
  ovrpShape_Cylinder = 1,
  ovrpShape_Cubemap = 2,
  ovrpShape_EyeFov = 3,
  ovrpShape_OffcenterCubemap = 4,
  ovrpShape_Equirect = 5,
  ovrpShape_EnumSize = 0xF
} ovrpShape;

typedef enum {
  ovrpLayout_Stereo = 0,
  ovrpLayout_Mono = 1,
  ovrpLayout_DoubleWide = 2,
  ovrpLayout_Array = 3,
  ovrpLayout_EnumSize = 0xF
} ovrpLayout;

/// A texture format.
typedef enum {
  ovrpTextureFormat_R8G8B8A8_sRGB = 0,
  ovrpTextureFormat_R8G8B8A8 = 1,
  ovrpTextureFormat_R16G16B16A16_FP = 2,
  ovrpTextureFormat_R11G11B10_FP = 3,
  ovrpTextureFormat_B8G8R8A8_sRGB = 4,
  ovrpTextureFormat_B8G8R8A8 = 5,
  ovrpTextureFormat_R5G6B5 = 11,

  //depth texture formats
  ovrpTextureFormat_D16 = 6,
  ovrpTextureFormat_D24_S8 = 7,
  ovrpTextureFormat_D32_FP = 8,
  ovrpTextureFormat_D32_S824_FP = 9,

  ovrpTextureFormat_None = 10,

  ovrpTextureFormat_EnumSize = 0x7fffffff
} ovrpTextureFormat;

/// Flags used by ovrpLayerDesc
typedef enum {
  /// Only create a single stage
  ovrpLayerFlag_Static = (1 << 0),
  /// Boost CPU priority while visible
  ovrpLayerFlag_LoadingScreen = (1 << 1),
  /// Force fov to be symmetric
  ovrpLayerFlag_SymmetricFov = (1 << 2),
  /// Texture origin is in bottom-left
  ovrpLayerFlag_TextureOriginAtBottomLeft = (1 << 3),
  /// Correct for chromatic aberration
  ovrpLayerFlag_ChromaticAberrationCorrection = (1 << 4),
  /// Does not allocate texture space within the swapchain
  ovrpLayerFlag_NoAllocation = (1 << 5),
  /// Enable protected content, added in 1.23
  ovrpLayerFlag_ProtectedContent = (1 << 6),
  /// Allocate AndroidSurfaceSwapChain, instead of regular TextureSwapChain
  ovrpLayerFlag_AndroidSurfaceSwapChain = (1 << 7),
} ovrpLayerFlags;

/// Layer description used by ovrp_SetupLayer to create the layer
#define OVRP_LAYER_DESC       \
  struct {                    \
    ovrpShape Shape;          \
    ovrpLayout Layout;        \
    ovrpSizei TextureSize;    \
    int MipLevels;            \
    int SampleCount;          \
    ovrpTextureFormat Format; \
    int LayerFlags;           \
  }

typedef OVRP_LAYER_DESC ovrpLayerDesc;

#define OVRP_LAYER_DESC_TYPE \
  union {                    \
    ovrpLayerDesc Base;      \
    OVRP_LAYER_DESC;         \
  }

typedef OVRP_LAYER_DESC_TYPE ovrpLayerDesc_Quad;
typedef OVRP_LAYER_DESC_TYPE ovrpLayerDesc_Cylinder;
typedef OVRP_LAYER_DESC_TYPE ovrpLayerDesc_Cubemap;

typedef struct {
  OVRP_LAYER_DESC_TYPE;
  ovrpFovf Fov[ovrpEye_Count];
  ovrpRectf VisibleRect[ovrpEye_Count];
  ovrpSizei MaxViewportSize;
  //added for 1.17
  ovrpTextureFormat DepthFormat;
} ovrpLayerDesc_EyeFov;

typedef OVRP_LAYER_DESC_TYPE ovrpLayerDesc_OffcenterCubemap;
typedef OVRP_LAYER_DESC_TYPE ovrpLayerDesc_Equirect;

typedef union {
  OVRP_LAYER_DESC_TYPE;
  ovrpLayerDesc_Quad Quad;
  ovrpLayerDesc_Cylinder Cylinder;
  ovrpLayerDesc_Cubemap Cubemap;
  ovrpLayerDesc_EyeFov EyeFov;
  ovrpLayerDesc_OffcenterCubemap OffcenterCubemap;
  ovrpLayerDesc_Equirect Equirect;
} ovrpLayerDescUnion;

#undef OVRP_LAYER_DESC
#undef OVRP_LAYER_DESC_TYPE

/// Flags used by ovrpLayerSubmit
typedef enum {
  /// Pose relative to head
  ovrpLayerSubmitFlag_HeadLocked = (1 << 0),
  /// Layer is octilinear (LMS)
  ovrpLayerSubmitFlag_Octilinear = (1 << 1),
  /// Use reverse Z
  ovrpLayerSubmitFlag_ReverseZ = (1 << 2),
  /// Disable layer depth compositing on Rift
  ovrpLayerSubmitFlag_NoDepth = (1 << 3),
  /// Use inverse alpha for timewarp blending
  ovrpLayerSubmitFlag_InverseAlpha = (1 << 4),
  /// Combine the submitted layer with the layers generated from OVROverlay commands
  ovrpLayerSubmitFlag_CombineLayerSubmits = (1 << 5),
  /// Enable Positional timeWarp on Fov layer
  ovrpLayerSubmitFlag_PositionalTimeWarp = (1 << 6),
  /// Enable Space warp on Fov layer
  ovrpLayerSubmitFlag_SpaceWarp = (1 << 7),
} ovrpLayerSubmitFlags;

/// Layer state to submit to ovrp_EndFrame
#define OVRP_LAYER_SUBMIT                         \
  struct {                                        \
    int LayerId;                                  \
    int TextureStage;                             \
    ovrpRecti ViewportRect[ovrpEye_Count];        \
    ovrpPosef Pose;                               \
    int LayerSubmitFlags;                         \
    /* Added in 1.31 */                           \
    ovrpVector4f ColorScale;                      \
    ovrpVector4f ColorOffset;                     \
    /* Added in 1.34 */                           \
    ovrpBool OverrideTextureRectMatrix;           \
    ovrpTextureRectMatrixf TextureRectMatrix;     \
    ovrpBool OverridePerLayerColorScaleAndOffset; \
  }

typedef OVRP_LAYER_SUBMIT ovrpLayerSubmit;

#define OVRP_LAYER_SUBMIT_TYPE \
  union {                      \
    ovrpLayerSubmit Base;      \
    OVRP_LAYER_SUBMIT;         \
  }

typedef struct {
  OVRP_LAYER_SUBMIT_TYPE;
  ovrpSizef Size;
} ovrpLayerSubmit_Quad;

typedef struct {
  OVRP_LAYER_SUBMIT_TYPE;
  float ArcWidth;
  float Height;
  float Radius;
} ovrpLayerSubmit_Cylinder;

typedef OVRP_LAYER_SUBMIT_TYPE ovrpLayerSubmit_Cubemap;

typedef struct {
	OVRP_LAYER_SUBMIT_TYPE;
	// added in 1.18
	ovrpOctilinearLayout OctilinearLayout[ovrpEye_Count];
	float DepthNear;
	float DepthFar;
} ovrpLayerSubmit_EyeFov;

typedef OVRP_LAYER_SUBMIT_TYPE ovrpLayerSubmit_OffcenterCubemap;
typedef OVRP_LAYER_SUBMIT_TYPE ovrpLayerSubmit_Equirect;

typedef union {
  OVRP_LAYER_SUBMIT_TYPE;
  ovrpLayerSubmit_Quad Quad;
  ovrpLayerSubmit_Cylinder Cylinder;
  ovrpLayerSubmit_Cubemap Cubemap;
  ovrpLayerSubmit_EyeFov EyeFov;
  ovrpLayerSubmit_OffcenterCubemap OffcenterCubemap;
  ovrpLayerSubmit_Equirect Equirect;
} ovrpLayerSubmitUnion;

typedef enum {
  ovrpViewportStencilType_HiddenArea = 0,
  ovrpViewportStencilType_VisibleArea = 1,
  ovrpViewportStencilType_BorderLine = 2,
  ovrpViewportStencilType_VisibleRectangle = 3,
} ovrpViewportStencilType;

#undef OVRP_LAYER_SUBMIT
#undef OVRP_LAYER_SUBMIT_TYPE

















#endif
