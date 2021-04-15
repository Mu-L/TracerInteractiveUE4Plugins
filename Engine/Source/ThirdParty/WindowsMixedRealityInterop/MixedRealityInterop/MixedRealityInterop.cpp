// Copyright (c) Microsoft Corporation. All rights reserved.

// To use this lib in engines that do not build cppwinrt:
// WinRT headers and types must be in the cpp and not the header.

#include "stdafx.h"
#include "MixedRealityInterop.h"

#include "wrl/client.h"
#include "wrl/wrappers/corewrappers.h"

#include <Roapi.h>
#include <queue>

#include "winrt/Windows.Devices.Haptics.h"
#include "winrt/Windows.Perception.h"
#include "winrt/Windows.Perception.People.h"
#include "winrt/Windows.Perception.Spatial.h"
#include "winrt/Windows.UI.Input.Spatial.h"
#include "winrt/Windows.Foundation.Numerics.h"
#include <winrt/Windows.Perception.People.h>
#include <winrt/Windows.Foundation.Metadata.h>

#include <winrt/Windows.Media.SpeechRecognition.h>

#include <HolographicSpaceInterop.h>
#include <SpatialInteractionManagerInterop.h>
#include <Windows.Graphics.Holographic.h>
#include <windows.ui.input.spatial.h>
#include <Windows.Perception.People.h>

#include <DXGI1_4.h>

#include "winrt/Windows.Graphics.Holographic.h"
#include "winrt/Windows.Graphics.DirectX.Direct3D11.h"
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <map>
#include <iterator>

#include <string>
#include <functional>

#if !PLATFORM_HOLOLENS && defined(_WIN64)
// Remoting
#include <winrt/Microsoft.Holographic.AppRemoting.h>
#define HOLO_STREAMING_RENDERING 1
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Storage.h>
// HoloLens 1 Remoting
#include <HolographicStreamerHelpers.h>
#else
#define HOLO_STREAMING_RENDERING 0
#endif

#include "SpatialAnchorHelper.h"
#include "SpeechRecognizer.h"
#include "GestureRecognizer.h"
#include "AzureSpatialAnchors.h"

#include <sstream>
#include "HandMeshObserver.h"

//https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=vs-2019
//_M_ARM64 Defined as 1 for compilations that target 64-bit ARM processors. Otherwise, undefined.
#if !PLATFORM_HOLOLENS || (PLATFORM_HOLOLENS && defined(_M_ARM64))
#	define HOLOLENS_BLOCKING_PRESENT 0
#else
#	define HOLOLENS_BLOCKING_PRESENT 1
#endif

#define LOG_HOLOLENS_FRAME_COUNTER 0


#pragma comment(lib, "OneCore")

using namespace Microsoft::WRL;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Foundation::Numerics;

using namespace winrt::Windows::Devices::Haptics;
using namespace winrt::Windows::Graphics::Holographic;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::UI::Input::Spatial;
using namespace winrt::Windows::Perception::People;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::People;
using namespace winrt::Windows::Storage::Streams;

using namespace winrt::Windows::Media::SpeechRecognition;

namespace WindowsMixedReality
{
	// Since WinRT types cannot be included in the header, 
	// we are declaring classes using WinRT types in this cpp file.
	// Forward declare the relevant classes here to keep variables at the top of the class.
	class TrackingFrame;
	class HolographicCameraResources;

	void StartMeshObserver(
		float InTriangleDensity,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AllocFunctionPointer)(MeshUpdate*),
		void(*RemovedMeshPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	);
	void UpdateMeshObserverBoundingVolume(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem InCoordinateSystem, winrt::Windows::Foundation::Numerics::float3 Position);
	void StopMeshObserver();

	void StartSceneUnderstandingObserver(
		bool bGeneratePlanes,
		bool bGenerateSceneMeshes,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AddPlaneFunctionPointer)(PlaneUpdate*),
		void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
		void(*AllocMeshFunctionPointer)(MeshUpdate*),
		void(*RemoveMeshFunctionPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	);
	void StopSceneUnderstandingObserver();
	void _SetSUCoordinateSystem();

	void StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*));
	void UpdateQRCodeObserverCoordinateSystem(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem InCoordinateSystem);
	void StopQRCodeObserver();

	bool bInitialized = false;
	bool isRemoteHolographicSpace = false;
	bool m_isHL1Remoting = false;

	HolographicSpace holographicSpace = nullptr;
	winrt::Windows::Perception::Spatial::SpatialLocator Locator = nullptr;
	IDirect3DDevice InteropD3DDevice = nullptr;

	/** Function pointer for tracking state change */
	void(*OnTrackingChanged)(HMDSpatialLocatability);
	std::mutex locatorMutex;

	SpatialInteractionManager interactionManager = nullptr;

	// Reference Frames
	winrt::Windows::Perception::Spatial::SpatialStationaryFrameOfReference StationaryReferenceFrame = nullptr;
	winrt::Windows::Perception::Spatial::SpatialStageFrameOfReference StageReferenceFrame = nullptr;
	winrt::Windows::Perception::Spatial::SpatialLocatorAttachedFrameOfReference AttachedReferenceFrame = nullptr;

	// Tracking frames.
	std::unique_ptr<TrackingFrame> currentFrame = nullptr;
	float4x4 LastKnownCoordinateSystemTransform = float4x4::identity();
	float4x4 LastKnownThirdCameraCoordinateSystemTransform = float4x4::identity();
	HolographicStereoTransform LastKnownProjection;
	HolographicStereoTransform LastKnownThirdCameraProjection;
	std::mutex poseLock;
	std::mutex disposeLock_GetProjection;
	std::mutex disposeLock_Present;
	HMDTrackingOrigin currentTrackingOrigin = HMDTrackingOrigin::Eye;
	HMDTrackingOrigin desiredTrackingOrigin = HMDTrackingOrigin::Eye;

	// Event registration tokens declared in cpp since events surface WinRT types.
	winrt::event_token CameraAddedToken;
	winrt::event_token CameraRemovedToken;
	winrt::event_token LocatabilityChangedToken;
	winrt::event_token StageChangedEventToken;
	winrt::event_token UserPresenceChangedToken;

	UserPresence currentUserPresence = UserPresence::Unknown;
	bool userPresenceChanged = true;
	std::mutex PresenceLock;

	// Variables used from event handlers must be declared inside of the cpp.
	// Camera resources.
	float nearPlaneDistance = 0.001f;
	float ScreenScaleFactor = 1.0f;
	std::shared_ptr<HolographicCameraResources> CameraResources = nullptr;
	std::shared_ptr<HolographicCameraResources> PVCameraResources = nullptr;
	std::recursive_mutex CameraResourcesLock;
	std::mutex StageLock;

	bool isThirdCameraActive = false;

	// Hidden Area Mesh
	std::vector<DirectX::XMFLOAT2> hiddenMesh[2];
	std::vector<DirectX::XMFLOAT2> visibleMesh[2];

	// Flags for supported API features.
	bool isSpatialStageSupported = false;
	bool isHiddenAreaMeshSupported = false;
	bool isVisibleAreaMeshSupported = false;
	bool isDepthBasedReprojectionSupported = false;
	bool isUserPresenceSupported = false;
	// Spatial Controllers
	bool supportsSpatialInput = false;
	bool supportsSourceOrientation = false;
	bool supportsMotionControllers = false;
	bool supportsHapticFeedback = false;
	bool supportsHandedness = false;
	bool supportsHandTracking = false;

	// Eye tracking
	bool supportsEyeTracking = false;
	bool eyeTrackingAllowed = false;

	// Spatial Anchors
	std::shared_ptr<WindowsMixedReality::SpatialAnchorHelper> m_spatialAnchorHelper = nullptr;

	std::shared_ptr<WindowsMixedReality::SpatialAnchorHelper> GetSpatialAnchorHelper()
	{
		return m_spatialAnchorHelper;
	}

	// Remoting
	void(*m_logCallback)(const wchar_t*) = nullptr;
	wchar_t m_ip[32] = L"000.000.000.000";

	// Controller pose
	float3 ControllerPositions[2];
	quaternion ControllerOrientations[2];
	bool ControllerIsTracked[2];

	PointerPoseInfo PointerPoses[2];

	// IDs for unhanded controllers.
	int HandIDs[2];

	std::map<uint32_t, std::shared_ptr<HandMeshUpdateObserver> > MeshUpdateObserverMap;

	// Controller state
	HMDInputPressState CurrentSelectState[2];
	HMDInputPressState PreviousSelectState[2];

	HMDInputPressState CurrentGraspState[2];
	HMDInputPressState PreviousGraspState[2];

	HMDInputPressState CurrentMenuState[2];
	HMDInputPressState PreviousMenuState[2];

	HMDInputPressState CurrentThumbstickPressState[2];
	HMDInputPressState PreviousThumbstickPressState[2];

	HMDInputPressState CurrentTouchpadPressState[2];
	HMDInputPressState PreviousTouchpadPressState[2];

	HMDInputPressState CurrentTouchpadIsTouchedState[2];
	HMDInputPressState PreviousTouchpadIsTouchedState[2];

	const HandJointKind Joints[NumHMDHandJoints] = {
		HandJointKind::Palm,
		HandJointKind::Wrist,
		HandJointKind::ThumbMetacarpal,
		HandJointKind::ThumbProximal,
		HandJointKind::ThumbDistal,
		HandJointKind::ThumbTip,
		HandJointKind::IndexMetacarpal,
		HandJointKind::IndexProximal,
		HandJointKind::IndexIntermediate,
		HandJointKind::IndexDistal,
		HandJointKind::IndexTip,
		HandJointKind::MiddleMetacarpal,
		HandJointKind::MiddleProximal,
		HandJointKind::MiddleIntermediate,
		HandJointKind::MiddleDistal,
		HandJointKind::MiddleTip,
		HandJointKind::RingMetacarpal,
		HandJointKind::RingProximal,
		HandJointKind::RingIntermediate,
		HandJointKind::RingDistal,
		HandJointKind::RingTip,
		HandJointKind::LittleMetacarpal,
		HandJointKind::LittleProximal,
		HandJointKind::LittleIntermediate,
		HandJointKind::LittleDistal,
		HandJointKind::LittleTip,
	};
	bool JointPoseValid[2];
	JointPose JointPoses[2][NumHMDHandJoints];

	std::mutex speechRecognizerLock;
	std::map<int, SpeechRecognizer*> speechRecognizerMap;
	int speechRecognizerIndex = 0;

	std::mutex gestureRecognizerLock;
	std::map<int, std::shared_ptr<GestureRecognizer> > gestureRecognizerMap;
	int gestureRecognizerIndex = 0;
	SpatialInteractionManager GestureRecognizer::m_InteractionManager = nullptr;

	DirectX::XMFLOAT3 LSRPosition;
	bool bIsLSRSetThisFrame = false;

#if !PLATFORM_HOLOLENS
	HWND stereoWindowHandle;

#if HOLO_STREAMING_RENDERING
	// Remoting
	winrt::Microsoft::Holographic::AppRemoting::RemoteContext m_remoteContext = nullptr;
	winrt::Microsoft::Holographic::AppRemoting::IRemoteSpeech remoteSpeech = nullptr;
	winrt::Microsoft::Holographic::AppRemoting::IRemoteContext::OnConnected_revoker m_onConnectedEventRevoker;
	winrt::Microsoft::Holographic::AppRemoting::IRemoteContext::OnDisconnected_revoker m_onDisconnectedEventRevoker;
	winrt::Microsoft::Holographic::AppRemoting::IRemoteSpeech::OnRecognizedSpeech_revoker m_onRecognizedSpeechRevoker;
	winrt::Microsoft::Holographic::AppRemoting::IRemoteContext::OnListening_revoker m_onListeningEventRevoker;

	// HoloLens 1 Remoting
	Microsoft::Holographic::HolographicStreamerHelpers^ m_streamerHelpers;
	Microsoft::WRL::Wrappers::SRWLock m_connectionStateLock;
	Windows::Foundation::EventRegistrationToken ConnectedToken;
	Windows::Foundation::EventRegistrationToken DisconnectedToken;
	Microsoft::Holographic::ConnectedEvent^ RemotingConnectedEvent = nullptr;
	Microsoft::Holographic::DisconnectedEvent^ RemotingDisconnectedEvent = nullptr;

	std::map<uint32_t, MixedRealityInterop::ConnectionCallback > ConnectionHandlerMap;
	std::recursive_mutex ConnectionHandlerLock;
	uint32_t ConnectionHandlerMaxId = 1;

	std::mutex remoteContextLock;
	HMDRemotingConnectionState remotingState = HMDRemotingConnectionState::Disconnected;
#endif // HOLO_STREAMING_RENDERING
#endif // !PLATFORM_HOLOLENS

	// Forward references
	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem GetReferenceCoordinateSystem();

	inline DirectX::XMFLOAT3 ToDirectXVec(float3 v)
	{
		return DirectX::XMFLOAT3(v.x, v.y, v.z);
	}

	struct QuadLayer
	{
	public:
		QuadLayer(HolographicQuadLayer quadLayer)
			: quadLayer(quadLayer)
		{ }

		uint32_t index = 0;

		ID3D11Texture2D* texture = nullptr;
		HolographicQuadLayer quadLayer = nullptr;

		float width = 1;
		float height = 1;

		DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0, 0, 0);
		DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4(0, 0, 0, 1);
		DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3(1, 1, 1);

		HMDLayerType layerType;

		int priority = 0;

		winrt::Windows::Perception::Spatial::SpatialAnchor anchor = nullptr;

		bool ValidateData()
		{
			return texture != nullptr
				&& quadLayer != nullptr;
		}
	};

	std::mutex quadLayerLock;
	std::vector<QuadLayer> quadLayers;

	// Remote Speech
#if HOLO_STREAMING_RENDERING
	concurrency::task<winrt::Windows::Storage::IStorageFolder> GetTempFolderAsync()
	{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
		wchar_t tempFolderPath[MAX_PATH];

		if (ExpandEnvironmentStringsW(L"%TEMP%", tempFolderPath, _countof(tempFolderPath)) == 0)
		{
			winrt::throw_last_error();
		}

		return concurrency::create_task([tempFolderPath]() {
			auto folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(tempFolderPath).get();
			return folder.as<winrt::Windows::Storage::IStorageFolder>();
			});
#endif

		return concurrency::create_task([]() {
			auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalCacheFolder();
			return folder.as<winrt::Windows::Storage::IStorageFolder>();
			});
	}

	concurrency::task<winrt::Windows::Storage::IStorageFile> CreateGrammarFileAsync()
	{
		const winrt::hstring ns = L"http://www.w3.org/2001/06/grammar";
		const winrt::Windows::Foundation::IReference<winrt::hstring> nsRef(ns);

		winrt::Windows::Data::Xml::Dom::XmlDocument doc;

		auto grammar = doc.CreateElementNS(nsRef, L"grammar");
		grammar.SetAttribute(L"version", L"1.0");
		grammar.SetAttribute(L"xml:lang", L"en-US");
		grammar.SetAttribute(L"root", L"remoting");
		doc.AppendChild(grammar);

		auto rule = doc.CreateElementNS(nsRef, L"rule");
		rule.SetAttribute(L"id", L"remoting");
		grammar.AppendChild(rule);

		auto item = doc.CreateElementNS(nsRef, L"item");
		item.InnerText(L"Hello world");

		rule.AppendChild(item);

		return GetTempFolderAsync().then(
			[doc](winrt::Windows::Storage::IStorageFolder tempFolder) -> winrt::Windows::Storage::IStorageFile {
				auto file =
					tempFolder.CreateFileAsync(L"grammar.xml", winrt::Windows::Storage::CreationCollisionOption::ReplaceExisting).get();
				doc.SaveToFileAsync(file).get();

				return file;
			});
	}
#endif

	HolographicViewConfigurationKind GetCameraKind(HolographicCamera const& Camera)
	{
		auto kind = HolographicViewConfigurationKind::Display;
		if (isRemoteHolographicSpace)
		{
			// to reduce output spam for the below exception when remoting.
			return kind;
		}
#if PLATFORM_HOLOLENS
		try
		{
			// handling exception for current limitation of remoting camera support. 
			// https://docs.microsoft.com/en-us/windows/mixed-reality/holographic-remoting-troubleshooting
			auto ViewConfiguration = Camera.ViewConfiguration();
			if (!ViewConfiguration)
			{
				return kind;
			}

			kind = ViewConfiguration.Kind();
		}
		catch (winrt::hresult_error const&) {}
#endif
		return kind;
	}

#pragma region Camera Resources
	class TrackingFrame
	{
	public:
		TrackingFrame(HolographicFrame frame)
		{
			Frame = HolographicFrame(frame);
			Count = NextCount++;
		}

		void UpdatePrediction()
		{
			if (Frame == nullptr)
			{
				return;
			}

			Frame.UpdateCurrentPrediction();
		}

		bool CalculatePose(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& CoordinateSystem);

		HolographicFrame Frame = nullptr;

		int Count = -1;

	private:
		static int NextCount;
	};

	int TrackingFrame::NextCount = 0;

	class HolographicCameraResources
	{
	public:
		HolographicCameraResources(
			const winrt::Windows::Graphics::Holographic::HolographicCamera& InCamera)
			: Camera(InCamera)
		{
			bool bIsStereo = InCamera.IsStereo();
			bStereoEnabled = bIsStereo;
			RenderTargetSize = InCamera.RenderTargetSize();

			Viewport.TopLeftX = Viewport.TopLeftY = 0.0f;
			Viewport.Width = RenderTargetSize.Width;
			Viewport.Height = RenderTargetSize.Height;
			Viewport.MinDepth = 0;
			Viewport.MaxDepth = 1.0f;
		}

		winrt::Windows::Graphics::Holographic::HolographicCamera GetCamera() const { return Camera; }
		winrt::Windows::Foundation::Size GetRenderTargetSize() const { return RenderTargetSize; }
		const D3D11_VIEWPORT& GetViewport() const { return Viewport; }
		bool IsStereoEnabled() const { return bStereoEnabled; }

	private:
		winrt::Windows::Graphics::Holographic::HolographicCamera Camera;
		winrt::Windows::Foundation::Size RenderTargetSize;
		D3D11_VIEWPORT Viewport;
		bool bStereoEnabled;

	public:
		bool CalculatePose(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& CoordinateSystem, const HolographicCameraPose& InPose, const HolographicFramePrediction& Prediction, bool displayCamera = true)
		{
			Pose = InPose;
			if (Pose == nullptr)
			{
				return false;
			}

			// Get position and orientation from a stationary or stage reference frame.
			winrt::Windows::Foundation::IReference<HolographicStereoTransform> stationaryViewTransform = Pose.TryGetViewTransform(CoordinateSystem);

			// Get rotation only from attached reference frame.
			winrt::Windows::Foundation::IReference<HolographicStereoTransform> orientationOnlyTransform{ nullptr };
			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem locatorAttachedCoordinateSystem = nullptr;
			if (AttachedReferenceFrame != nullptr)
			{
				locatorAttachedCoordinateSystem = AttachedReferenceFrame.GetStationaryCoordinateSystemAtTimestamp(Prediction.Timestamp());

				SpatialPointerPose pointerPose = SpatialPointerPose::TryGetAtTimestamp(StationaryReferenceFrame.CoordinateSystem(), Prediction.Timestamp());

				if (pointerPose != nullptr && displayCamera)
				{
					AttachedReferenceFrame.RelativePosition(pointerPose.Head().Position());
					// Let the mesh observer and the QR code observer update their transforms
					UpdateMeshObserverBoundingVolume(CoordinateSystem, pointerPose.Head().Position());
					UpdateQRCodeObserverCoordinateSystem(CoordinateSystem);
				}

				orientationOnlyTransform = Pose.TryGetViewTransform(locatorAttachedCoordinateSystem);
			}

			if ((stationaryViewTransform == nullptr) &&
				(orientationOnlyTransform == nullptr))
			{
				// We have no information for either frames
				return false;
			}

			bool orientationOnlyTracking = false;
			if (stationaryViewTransform == nullptr)
			{
				// We have lost world-locked tracking (6dof), and need to fall back to orientation-only tracking attached to the hmd (3dof).
				orientationOnlyTracking = true;
			}

			// If the stationary/stage is valid, cache transform between coordinate systems so we can reuse it in subsequent frames.
			if (!orientationOnlyTracking && locatorAttachedCoordinateSystem != nullptr)
			{
				winrt::Windows::Foundation::IReference<float4x4> locatorToFixedCoordTransform = CoordinateSystem.TryGetTransformTo(locatorAttachedCoordinateSystem);
				if (locatorToFixedCoordTransform != nullptr)
				{
					if (displayCamera)
					{
						LastKnownCoordinateSystemTransform = locatorToFixedCoordTransform.Value();
					}
					else
					{
						LastKnownThirdCameraCoordinateSystemTransform = locatorToFixedCoordTransform.Value();
					}
				}
			}

			HolographicStereoTransform hst;
			if (!orientationOnlyTracking)
			{
				hst = stationaryViewTransform.Value();
			}
			else
			{
				hst = orientationOnlyTransform.Value();
			}

			leftPose = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)&hst.Left);
			rightPose = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)&hst.Right);

			// When our position-tracked transform is not valid, re-use the last known transform between coordinate systems to adjust the 
			// position and orientation so there's no visible jump.
			if (orientationOnlyTracking)
			{
				DirectX::XMMATRIX lastKnownCoordSystemTransform = DirectX::XMMatrixIdentity();
				if (displayCamera)
				{
					lastKnownCoordSystemTransform = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)&LastKnownCoordinateSystemTransform);
				}
				else
				{
					lastKnownCoordSystemTransform = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)&LastKnownThirdCameraCoordinateSystemTransform);
				}

				// Transform the left and right poses by the last known coordinate system transform.
				leftPose = DirectX::XMMatrixMultiply(lastKnownCoordSystemTransform, leftPose);
				rightPose = DirectX::XMMatrixMultiply(lastKnownCoordSystemTransform, rightPose);
			}

			return true;
		}

		DirectX::XMMATRIX leftPose = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX rightPose = DirectX::XMMatrixIdentity();

		HolographicCameraPose Pose = nullptr;

		void SetFocusPoint(const DirectX::XMFLOAT3 pos)
		{
			if (RenderingParameters == nullptr)
			{
				return;
			}

			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem cs = GetReferenceCoordinateSystem();

			if (cs == nullptr)
			{
				return;
			}

			RenderingParameters.SetFocusPoint(cs, winrt::Windows::Foundation::Numerics::float3(pos.x, pos.y, pos.z));
		}

		bool CreateRenderingParameters(TrackingFrame* frame, bool& succeeded)
		{
			succeeded = true;

			if (frame->Frame == nullptr
				|| holographicSpace == nullptr)
			{
				return false;
			}

			if (!isRemoteHolographicSpace && !holographicSpace.IsAvailable())
			{
				return false;
			}

			// Getting rendering parameters can fail if the PC goes to sleep.
			// Wrap this in a try-catch so we do not crash.
			RenderingParameters = nullptr;
			try
			{
				RenderingParameters = frame->Frame.GetRenderingParameters(Pose);
			}
			catch (winrt::hresult_error const&)
			{
				RenderingParameters = nullptr;
				succeeded = false;
			}

			if (RenderingParameters == nullptr)
			{
				return false;
			}

			// Get the WinRT object representing the holographic camera's back buffer.
			IDirect3DSurface surface = RenderingParameters.Direct3D11BackBuffer();
			if (surface == nullptr)
			{
				return false;
			}

			// Get a DXGI interface for the holographic camera's back buffer.
			// Holographic cameras do not provide the DXGI swap chain, which is owned
			// by the system. The Direct3D back buffer resource is provided using WinRT
			// interop APIs.
			winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> DxgiInterfaceAccess =
				surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
			if (!DxgiInterfaceAccess)
			{
				return false;
			}

			ComPtr<ID3D11Resource> resource;
			DxgiInterfaceAccess->GetInterface(IID_PPV_ARGS(&resource));
			if (resource == nullptr)
			{
				return false;
			}

			// Get a Direct3D interface for the holographic camera's back buffer.
			resource.As(&BackBufferTexture);
			if (BackBufferTexture == nullptr)
			{
				return false;
			}

			if (bIsLSRSetThisFrame)
			{
				SetFocusPoint(LSRPosition);
				// Note: if we use point LSR at runtime on HoloLens, this would only work for the first camera resources.
				// Since we are only using point LSR when remoting, this is fine for now.
				bIsLSRSetThisFrame = false;
			}

			return true;
		}

		bool CommitDepthBuffer(TrackingFrame* frame, Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture, bool& succeeded)
		{
			succeeded = true;

			if (frame->Frame == nullptr
				|| holographicSpace == nullptr)
			{
				return false;
			}

			if (!isRemoteHolographicSpace && !holographicSpace.IsAvailable())
			{
				return false;
			}

			if (RenderingParameters == nullptr)
			{
				return false;
			}

			// Use depth buffer to stabilize frame.
			CommitDepthTexture(depthTexture, RenderingParameters);

			RenderingParameters = nullptr;

			return true;
		}

		ID3D11Texture2D* GetBackBufferTexture() const { return BackBufferTexture.Get(); }
		HolographicCameraRenderingParameters RenderingParameters = nullptr;

	private:
		Microsoft::WRL::ComPtr<ID3D11Texture2D> BackBufferTexture;

		bool CommitDepthTexture(Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture, HolographicCameraRenderingParameters RenderingParameters)
		{
			if (isRemoteHolographicSpace && m_isHL1Remoting)
			{
				return false;
			}

			if (!isDepthBasedReprojectionSupported || depthTexture == nullptr)
			{
				return false;
			}

			Microsoft::WRL::ComPtr<IDXGIResource1> depthResource;
			HRESULT hr = depthTexture.As(&depthResource);
			ComPtr<IDXGISurface2> depthDxgiSurface;
			if (SUCCEEDED(hr))
			{
				hr = depthResource->CreateSubresourceSurface(0, &depthDxgiSurface);
			}

			if (FAILED(hr))
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> depthD3DSurface;
			hr = CreateDirect3D11SurfaceFromDXGISurface(depthDxgiSurface.Get(), &depthD3DSurface);
			if (FAILED(hr) || depthD3DSurface == nullptr)
			{
				return false;
			}

			winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface depth_winrt = nullptr;
			winrt::check_hresult(depthD3DSurface.Get()->QueryInterface(
				winrt::guid_of<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>(),
				reinterpret_cast<void**>(winrt::put_abi(depth_winrt))));

			if (depth_winrt != nullptr)
			{
				try
				{
					RenderingParameters.CommitDirect3D11DepthBuffer(depth_winrt);
				}
				catch (winrt::hresult_error const&)
				{
					return false;
				}
			}

			return true;
		}
	};

	bool TrackingFrame::CalculatePose(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& CoordinateSystem)
	{
		if (Frame == nullptr)
		{
			return false;
		}

		// Get a prediction of where holographic cameras will be when this frame is presented.
		HolographicFramePrediction Prediction = Frame.CurrentPrediction();
		if (!Prediction)
		{
			return false;
		}

		IVectorView<HolographicCameraPose> CameraPoses = Prediction.CameraPoses();
		if (CameraPoses == nullptr || CoordinateSystem == nullptr)
		{
			return false;
		}

		UINT32 Size = CameraPoses.Size();
		if (Size == 0)
		{
			return false;
		}

		for (unsigned int i = 0; i < CameraPoses.Size(); i++)
		{
			const HolographicCameraPose& pose = CameraPoses.GetAt(i);
			auto kind = GetCameraKind(pose.HolographicCamera());

			if (kind == HolographicViewConfigurationKind::Display)
			{
				CameraResources->CalculatePose(CoordinateSystem, pose, Prediction);
			}
			else if (kind == HolographicViewConfigurationKind::PhotoVideoCamera)
			{
				PVCameraResources->CalculatePose(CoordinateSystem, pose, Prediction, false);
			}
		}

		return true;
	}
#pragma endregion

	bool checkUniversalApiContract(int contractNumber)
	{
		return winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", contractNumber);
	}

	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem GetReferenceCoordinateSystem()
	{
		std::lock_guard<std::mutex> lock(StageLock);

		// Check for new stage if necessary.
		if (desiredTrackingOrigin == HMDTrackingOrigin::Floor
			&& isSpatialStageSupported && !isRemoteHolographicSpace)
		{
			if (StageReferenceFrame == nullptr)
			{
				StageReferenceFrame = winrt::Windows::Perception::Spatial::SpatialStageFrameOfReference::Current();
			}

			if (StageReferenceFrame != nullptr)
			{
				currentTrackingOrigin = HMDTrackingOrigin::Floor;
				return StageReferenceFrame.CoordinateSystem();
			}
		}

		if (StationaryReferenceFrame != nullptr)
		{
			currentTrackingOrigin = HMDTrackingOrigin::Eye;
			return StationaryReferenceFrame.CoordinateSystem();
		}

		return nullptr;
	}

	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem GetAttachedCoordinateSystem()
	{
		winrt::Windows::Foundation::DateTime dt = winrt::clock::now();
		PerceptionTimestamp ts = PerceptionTimestampHelper::FromHistoricalTargetTime(dt);
		return AttachedReferenceFrame.GetStationaryCoordinateSystemAtTimestamp(ts);
	}

	static UserPresence GetInteropUserPresence()
	{
		std::lock_guard<std::mutex> lock(poseLock);

		if (!isUserPresenceSupported || holographicSpace == nullptr)
		{
			return UserPresence::Unknown;
		}

		switch (holographicSpace.UserPresence())
		{
		case HolographicSpaceUserPresence::Absent:
			return UserPresence::NotWorn;
		case HolographicSpaceUserPresence::PresentActive:
		case HolographicSpaceUserPresence::PresentPassive:
			return UserPresence::Worn;
		default:
			return UserPresence::Unknown;
		}
	}

#pragma region Event Callbacks
	void OnLocatabilityChanged(
		const winrt::Windows::Perception::Spatial::SpatialLocator& Sender,
		const winrt::Windows::Foundation::IInspectable&)
	{
		std::lock_guard guard(locatorMutex);
		if (OnTrackingChanged != nullptr)
		{
			OnTrackingChanged(HMDSpatialLocatability(Sender.Locatability()));
		}
	}

	void InternalCreateHiddenVisibleAreaMesh(HolographicCamera Camera)
	{
		if (isRemoteHolographicSpace)
		{
			return;
		}

		for (int i = (int)HMDEye::Left;
			i <= (int)HMDEye::Right; i++)
		{
			if (isHiddenAreaMeshSupported)
			{
				winrt::array_view<winrt::Windows::Foundation::Numerics::float2> vertices =
					Camera.LeftViewportParameters().HiddenAreaMesh();
				if (i == (int)HMDEye::Right)
				{
					vertices = Camera.RightViewportParameters().HiddenAreaMesh();
				}

				hiddenMesh[i].clear();

				for (int v = 0; v < (int)vertices.size(); v++)
				{
					hiddenMesh[i].push_back(DirectX::XMFLOAT2(vertices[v].x, vertices[v].y));
				}
			}

			if (isVisibleAreaMeshSupported)
			{
				winrt::array_view<winrt::Windows::Foundation::Numerics::float2> vertices =
					Camera.LeftViewportParameters().VisibleAreaMesh();
				if (i == (int)HMDEye::Right)
				{
					vertices = Camera.RightViewportParameters().VisibleAreaMesh();
				}

				visibleMesh[i].clear();

				for (int v = 0; v < (int)vertices.size(); v++)
				{
					visibleMesh[i].push_back(DirectX::XMFLOAT2(vertices[v].x, vertices[v].y));
				}
			}
		}
	}

	void MixedRealityInterop::CreateHiddenVisibleAreaMesh()
	{
		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		if (CameraResources == nullptr)
		{
			return;
		}

		HolographicCamera camera = CameraResources->GetCamera();
		if (camera == nullptr)
		{
			return;
		}

		InternalCreateHiddenVisibleAreaMesh(camera);
	}

	bool MixedRealityInterop::GetHiddenAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length)
	{
		if (hiddenMesh[(int)eye].empty())
		{
			return false;
		}

		length = (int)hiddenMesh[(int)eye].size();
		vertices = &hiddenMesh[(int)eye][0];

		return true;
	}

	bool MixedRealityInterop::GetVisibleAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length)
	{
		if (visibleMesh[(int)eye].empty())
		{
			return false;
		}

		length = (int)visibleMesh[(int)eye].size();
		vertices = &visibleMesh[(int)eye][0];

		return true;
	}

	void OnCameraAdded(
		const HolographicSpace& sender,
		const HolographicSpaceCameraAddedEventArgs& args)
	{
		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		HolographicCamera Camera = args.Camera();

		auto kind = GetCameraKind(Camera);
		if (kind == HolographicViewConfigurationKind::Display)
		{
			CameraResources = std::make_shared<HolographicCameraResources>(Camera);
		}
		else if (kind == HolographicViewConfigurationKind::PhotoVideoCamera)
		{
			PVCameraResources = std::make_shared<HolographicCameraResources>(Camera);
			isThirdCameraActive = true;
		}

		// Switch near and far planes for the camera to match Unreal's reverse-z projection.
		// The HoloLens compositor does not support a near-infinite reverse-z clipping plane, so use a very large value instead.
		Camera.SetNearPlaneDistance(FLT_MAX / 100.0f);
		Camera.SetFarPlaneDistance(nearPlaneDistance);

		InternalCreateHiddenVisibleAreaMesh(Camera);
	}

	void OnCameraRemoved(
		const HolographicSpace& sender,
		const HolographicCamera& Camera)
	{
		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		auto kind = GetCameraKind(Camera);
		if (kind == HolographicViewConfigurationKind::Display)
		{
			if (CameraResources == nullptr)
			{
				return;
			}

			if (Camera == CameraResources->GetCamera())
			{
				CameraResources.reset();
				CameraResources = nullptr;
			}

		}
		else if (kind == HolographicViewConfigurationKind::PhotoVideoCamera)
		{
			isThirdCameraActive = false;
			if (PVCameraResources == nullptr)
			{
				return;
			}

			if (Camera == PVCameraResources->GetCamera())
			{
				PVCameraResources.reset();
				PVCameraResources = nullptr;
			}
		}
	}

	void OnUserPresenceChanged(
		const HolographicSpace& sender,
		const winrt::Windows::Foundation::IInspectable& args)
	{
		std::lock_guard<std::mutex> lock(PresenceLock);

		UserPresence updatedPresence = GetInteropUserPresence();

		// OnUserPresenceChanged can fire more often than Unreal cares about since the Windows MR platform has multiple events for a valid worn state.
		if (currentUserPresence != updatedPresence)
		{
			currentUserPresence = updatedPresence;
			userPresenceChanged = true;
		}
	}
#pragma endregion

	MixedRealityInterop::MixedRealityInterop()
	{
		if (bInitialized)
		{
			return;
		}

		for (int i = 0; i < 2; i++)
		{
			ControllerPositions[i] = float3(0, 0, 0);
			ControllerOrientations[i] = quaternion::identity();
			ControllerIsTracked[i] = false;
			HandIDs[i] = -1;
			JointPoseValid[i] = false;
		}

		ResetButtonStates();

		// APIs introduced in 10586
		bool is10586 = checkUniversalApiContract(2);
		supportsSpatialInput = is10586;

		// APIs introduced in 14393
		bool is14393 = checkUniversalApiContract(3);
		supportsSourceOrientation = is14393;

		// APIs introduced in 15063
		bool is15063 = checkUniversalApiContract(4);
		isSpatialStageSupported = is15063;
		isHiddenAreaMeshSupported = is15063;
		isDepthBasedReprojectionSupported = is15063;
		supportsMotionControllers = is15063;
		supportsHapticFeedback = is15063;

		// APIs introduced in 16299
		bool is16299 = checkUniversalApiContract(5);
		supportsHandedness = is16299;

		// APIs introduced in 17134
		bool is17134 = checkUniversalApiContract(6);
		isVisibleAreaMeshSupported = is17134;
		isUserPresenceSupported = is17134;

		// APIs introduced in 18317/19H1
		bool is19H1 = checkUniversalApiContract(8);
		supportsHandTracking = is19H1;

#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		supportsEyeTracking = is19H1;//This code was hanging sometime -> EyesPose::IsSupported();
#endif

		//make sure to set to empty
		wcsncpy_s(failureString, L"", std::size(failureString));
	}

	bool CreateInteropDevice(ID3D11Device* device)
	{
		// Acquire the DXGI interface for the Direct3D device.
		Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice(device);

		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice.As(&dxgiDevice);

		winrt::com_ptr<::IInspectable> object;
		HRESULT hr = CreateDirect3D11DeviceFromDXGIDevice(
			dxgiDevice.Get(),
			reinterpret_cast<::IInspectable**>(winrt::put_abi(object)));

		if (SUCCEEDED(hr))
		{
			InteropD3DDevice = object.as<IDirect3DDevice>();

			try
			{
				holographicSpace.SetDirect3D11Device(InteropD3DDevice);
			}
			catch (winrt::hresult_error const&)
			{
				return false;
			}

			return true;
		}

		return false;
	}

	UINT64 MixedRealityInterop::GraphicsAdapterLUID()
	{
#if PLATFORM_HOLOLENS
		return 0;
#else
		UINT64 graphicsAdapterLUID = 0;

		// If we do not have a holographic space, the engine is trying to initialize our plugin before we are ready.
		// Create a temporary window to get the correct adapter LUID.
		if (holographicSpace == nullptr)
		{
			HWND temporaryWindowHwnd = CreateWindow(L"STATIC", L"TemporaryWindow", 0, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
			HolographicSpace tempHolographicSpace = nullptr;

			Microsoft::WRL::ComPtr<IHolographicSpaceInterop> spaceInterop = nullptr;
			Windows::Foundation::GetActivationFactory(
				Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Graphics_Holographic_HolographicSpace).Get(),
				&spaceInterop);

			// Get the Holographic Space
			spaceInterop->CreateForWindow(
				temporaryWindowHwnd,
				winrt::guid_of<HolographicSpace>(),
				winrt::put_abi(tempHolographicSpace));

			HolographicAdapterId adapterId = tempHolographicSpace.PrimaryAdapterId();
			graphicsAdapterLUID = ((UINT64)(adapterId.HighPart) << 32) | adapterId.LowPart;

			spaceInterop = nullptr;
			tempHolographicSpace = nullptr;
			DestroyWindow(temporaryWindowHwnd);
		}
		else
		{
			HolographicAdapterId adapterId = holographicSpace.PrimaryAdapterId();
			graphicsAdapterLUID = ((UINT64)(adapterId.HighPart) << 32) | adapterId.LowPart;
		}

		return graphicsAdapterLUID;
#endif
	}

	void MixedRealityInterop::SetLogCallback(void(*functionPointer)(const wchar_t*))
	{
		m_logCallback = functionPointer;
	}

	void Log(const wchar_t* text)
	{
		if (m_logCallback)
		{
			m_logCallback(text);
		}
	}
	void Log(std::wstringstream& stream)
	{
		Log(stream.str().c_str());
	}

	void MixedRealityInterop::Initialize(ID3D11Device* device, float nearPlane)
	{
		nearPlaneDistance = nearPlane;

		LastKnownProjection.Left = winrt::Windows::Foundation::Numerics::float4x4::identity();
		LastKnownProjection.Right = winrt::Windows::Foundation::Numerics::float4x4::identity();

		if (bInitialized)
		{
			return;
		}

		if (device == nullptr)
		{
			Log(L"MixedRealityInterop::Initialize: D3D11Device is null");
			return;
		}
		if (holographicSpace == nullptr)
		{
			//Log(L"MixedRealityInterop::Initialize: holographicSpace is null");
			return;
		}

		if (!isRemoteHolographicSpace && !holographicSpace.IsAvailable())
		{
			Log(L"MixedRealityInterop::Initialize: holographicSpace is not available");
			return;
		}

		// Use the default SpatialLocator to track the motion of the device.
		if (Locator == nullptr)
		{
			Microsoft::WRL::ComPtr<ABI::Windows::Perception::Spatial::ISpatialLocatorStatics> spatialLocatorStatics;
			HRESULT hr = Windows::Foundation::GetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Perception_Spatial_SpatialLocator).Get(), &spatialLocatorStatics);

			spatialLocatorStatics->GetDefault((ABI::Windows::Perception::Spatial::ISpatialLocator**)winrt::put_abi(Locator));
		}
		if (Locator == nullptr)
		{
			Log(L"MixedRealityInterop::Initialize: Locator is null");
			return;
		}

		if (!CreateInteropDevice(device))
		{
			Log(L"MixedRealityInterop::Initialize: CreateInteropDevice() failed");
			return;
		}

		// The simplest way to render world-locked holograms is to create a stationary reference frame
		// when the app is launched. This is roughly analogous to creating a "world" coordinate system
		// with the origin placed at the device's position as the app is launched.
		if (StationaryReferenceFrame == nullptr)
		{
			StationaryReferenceFrame = Locator.CreateStationaryFrameOfReferenceAtCurrentLocation();
			std::lock_guard<std::mutex> lock(gestureRecognizerLock);
			for (auto p : gestureRecognizerMap)
			{
				if (p.second)
				{
					p.second->UpdateFrame(StationaryReferenceFrame);
				}
			}
		}
		if (StationaryReferenceFrame == nullptr)
		{
			Log(L"MixedRealityInterop::Initialize: StationaryReferenceFrame is null");
			return;
		}

		// Create a locator attached frame of reference to fall back to if tracking is lost,
		// allowing for orientation-only tracking to take over.
		if (AttachedReferenceFrame == nullptr)
		{
			AttachedReferenceFrame = Locator.CreateAttachedFrameOfReferenceAtCurrentHeading();
		}

		if (AttachedReferenceFrame == nullptr)
		{
			Log(L"MixedRealityInterop::Initialize: AttachedReferenceFrame is null");
			return;
		}

		// Register events.
		LocatabilityChangedToken = Locator.LocatabilityChanged(
			[=](const winrt::Windows::Perception::Spatial::SpatialLocator& sender, const winrt::Windows::Foundation::IInspectable& args)
			{
				OnLocatabilityChanged(sender, args);
			});

		// Broadcast initial tracking state after setting LocatabilityChanged.
		if (OnTrackingChanged != nullptr)
		{
			OnTrackingChanged(HMDSpatialLocatability(Locator.Locatability()));
		}

		CameraAddedToken = holographicSpace.CameraAdded(
			[=](const HolographicSpace& sender, const HolographicSpaceCameraAddedEventArgs& args)
			{
				OnCameraAdded(sender, args);
			});

		//no CameraRemoved event here because it deadlocks computer, we manually detect removed cameras in InitFrame

		// Check for an updated stage:
		StageChangedEventToken = winrt::Windows::Perception::Spatial::SpatialStageFrameOfReference::CurrentChanged(
			[=](auto&&, auto&&)
			{
				// Reset stage reference frame so we can establish a new one next frame.
				std::lock_guard<std::mutex> lock(StageLock);
				StageReferenceFrame = nullptr;
			});

		if (!isRemoteHolographicSpace && isUserPresenceSupported)
		{
			UserPresenceChangedToken = holographicSpace.UserPresenceChanged(
				[=](const HolographicSpace& sender, const winrt::Windows::Foundation::IInspectable& args)
				{
					OnUserPresenceChanged(sender, args);
				});
		}

		bInitialized = true;
	}

	void MixedRealityInterop::Dispose(bool force)
	{
		std::lock_guard<std::mutex> lock(poseLock);

		std::lock_guard<std::mutex> renderLock_projection(disposeLock_GetProjection);
		std::lock_guard<std::mutex> renderLock_present(disposeLock_Present);

		if (currentFrame != nullptr)
		{
			currentFrame->Frame = nullptr;
			currentFrame = nullptr;
		}

		for (int i = 0; i < 2; i++)
		{
			ControllerPositions[i] = float3(0, 0, 0);
			ControllerOrientations[i] = quaternion::identity();
			HandIDs[i] = -1;

			JointPoseValid[i] = false;

			hiddenMesh[i].clear();
			visibleMesh[i].clear();
		}

		quadLayers.clear();
		if (!m_isHL1Remoting && CameraResources != nullptr && CameraResources->GetCamera() != nullptr)
		{
			for (HolographicQuadLayer layer : CameraResources->GetCamera().QuadLayers())
			{
				layer.Close();
			}
			CameraResources->GetCamera().QuadLayers().Clear();
		}

		if (!force && isRemoteHolographicSpace)
		{
			return;
		}

		if (holographicSpace != nullptr)
		{
			if (CameraAddedToken.value != 0)
			{
				holographicSpace.CameraAdded(CameraAddedToken);
				CameraAddedToken.value = 0;
			}

			if (CameraRemovedToken.value != 0)
			{
				holographicSpace.CameraRemoved(CameraRemovedToken);
				CameraRemovedToken.value = 0;
			}

			if (UserPresenceChangedToken.value != 0)
			{
				holographicSpace.UserPresenceChanged(UserPresenceChangedToken);
				UserPresenceChangedToken.value = 0;
			}
		}

		if (Locator != nullptr && LocatabilityChangedToken.value != 0)
		{
			Locator.LocatabilityChanged(LocatabilityChangedToken);
			LocatabilityChangedToken.value = 0;
		}
		Locator = nullptr;

		if (StageReferenceFrame != nullptr && StageChangedEventToken.value != 0)
		{
			winrt::Windows::Perception::Spatial::SpatialStageFrameOfReference::CurrentChanged(StageChangedEventToken);
			StageChangedEventToken.value = 0;
		}

		bInitialized = false;
		holographicSpace = nullptr;
		interactionManager = nullptr;

		CameraResources = nullptr;
		AttachedReferenceFrame = nullptr;
		StationaryReferenceFrame = nullptr;
		StageReferenceFrame = nullptr;

		isRemoteHolographicSpace = false;

		eyeTrackingAllowed = false;

		StopMeshObserver();

		for (auto speechRecognizer : speechRecognizerMap)
		{
			speechRecognizer.second->StopSpeechRecognizer();
		}
		speechRecognizerMap.clear();
		speechRecognizerIndex = 0;

#if !PLATFORM_HOLOLENS
		if (IsWindow(stereoWindowHandle))
		{
			DestroyWindow(stereoWindowHandle);
		}
		stereoWindowHandle = (HWND)INVALID_HANDLE_VALUE;

#if HOLO_STREAMING_RENDERING
		// Also need to clear out the streamer helper in case of abnormal program termination to fix a race condition in the dll shutdown order
		{
			std::lock_guard<std::mutex> remoteLock(remoteContextLock);
			m_remoteContext = nullptr;
		}
#endif
#endif
	}

	bool MixedRealityInterop::IsStereoEnabled()
	{
		if (CameraResources == nullptr)
		{
			return false;
		}

		return CameraResources->IsStereoEnabled();
	}

	bool MixedRealityInterop::IsTrackingAvailable()
	{
		if (Locator == nullptr)
		{
			return false;
		}

		return Locator.Locatability() != winrt::Windows::Perception::Spatial::SpatialLocatability::Unavailable;
	}

	WindowsMixedReality::HMDSpatialLocatability MixedRealityInterop::GetTrackingState()
	{
		if (Locator == nullptr)
		{
			return HMDSpatialLocatability::Unavailable;
		}

		return HMDSpatialLocatability(Locator.Locatability());
	}

	void MixedRealityInterop::SetTrackingChangedCallback(void(*CallbackPointer)(WindowsMixedReality::HMDSpatialLocatability))
	{
		std::lock_guard guard(locatorMutex);
		OnTrackingChanged = CallbackPointer;
	}

	void MixedRealityInterop::ResetOrientationAndPosition()
	{
		if (Locator == nullptr)
		{
			return;
		}

		StationaryReferenceFrame = Locator.CreateStationaryFrameOfReferenceAtCurrentLocation();

		if (isSpatialStageSupported)
		{
			StageReferenceFrame = winrt::Windows::Perception::Spatial::SpatialStageFrameOfReference::Current();
		}

		{
			std::lock_guard<std::mutex> lock(gestureRecognizerLock);
			for (auto p : gestureRecognizerMap)
			{
				if (p.second)
				{
					p.second->UpdateFrame(StationaryReferenceFrame);
				}
			}
		}
	}

	bool MixedRealityInterop::IsInitialized() const
	{
		if (!isRemoteHolographicSpace && (holographicSpace == nullptr || !holographicSpace.IsAvailable()))
		{
			return false;
		}

		return bInitialized
			&& holographicSpace != nullptr
			&& CameraResources != nullptr;
	}

	bool MixedRealityInterop::IsImmersiveWindowValid()
	{
#if PLATFORM_HOLOLENS
		return false;
#else
		return IsWindow(stereoWindowHandle);
#endif
	}

	bool MixedRealityInterop::IsAvailable()
	{
		if (isRemoteHolographicSpace)
		{
			return holographicSpace != nullptr;
		}

		// APIs introduced in 15063
		if (checkUniversalApiContract(4))
		{
			return HolographicSpace::IsAvailable();
		}

		return true;
	}

	bool MixedRealityInterop::IsCurrentlyImmersive()
	{
		return IsInitialized()
			&& IsImmersiveWindowValid();
	}

#if !PLATFORM_HOLOLENS
	bool MixedRealityInterop::CreateHolographicSpace(HWND hwnd)
	{
		if (holographicSpace != nullptr)
		{
			// We already have a holographic space.
			return true;
		}

		Microsoft::WRL::ComPtr<IHolographicSpaceInterop> spaceInterop = nullptr;
		HRESULT hr = Windows::Foundation::GetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Graphics_Holographic_HolographicSpace).Get(),
			&spaceInterop);

		// Convert the game window into an immersive holographic space.
		if (FAILED(hr))
		{
			return false;
		}

		// Get the Holographic Space
		hr = spaceInterop->CreateForWindow(
			hwnd,
			winrt::guid_of<HolographicSpace>(),
			winrt::put_abi(holographicSpace));

		if (FAILED(hr))
		{
			return false;
		}

		// Get the interaction manager.
		Microsoft::WRL::ComPtr<ISpatialInteractionManagerInterop> interactionManagerInterop = nullptr;
		hr = Windows::Foundation::GetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_UI_Input_Spatial_SpatialInteractionManager).Get(),
			&interactionManagerInterop);

		if (FAILED(hr))
		{
			return false;
		}

		// Get the Interaction Manager
		hr = interactionManagerInterop->GetForWindow(
			hwnd,
			winrt::guid_of<winrt::Windows::UI::Input::Spatial::SpatialInteractionManager>(),
			winrt::put_abi(interactionManager));

		{
			std::lock_guard<std::mutex> lock(gestureRecognizerLock);
			for (auto p : gestureRecognizerMap)
			{
				if (p.second)
				{
					p.second->Init(m_isHL1Remoting);
				}
			}
		}

		return SUCCEEDED(hr);
	}
#endif

#if !PLATFORM_HOLOLENS
	void ForceAllowInput(HWND hWnd)
	{
		if (!IsWindow(hWnd))
		{
			return;
		}

		// Workaround to successfully route input to our new HWND.
		AllocConsole();
		HWND hWndConsole = GetConsoleWindow();
		SetWindowPos(hWndConsole, 0, 0, 0, 0, 0, SWP_NOACTIVATE);
		FreeConsole();

		SetForegroundWindow(hWnd);
	}
#endif

	void MixedRealityInterop::EnableStereo(bool enableStereo)
	{
#if PLATFORM_HOLOLENS
		if (!enableStereo && holographicSpace != nullptr)
		{
			Dispose();
		}
#else
		if (enableStereo && holographicSpace == nullptr)
		{
			stereoWindowHandle = CreateWindow(L"STATIC", L"UE4Game_WindowsMR", 0, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);

			// Go immersive on this window handle before it has been shown.
			bool Success = CreateHolographicSpace(stereoWindowHandle);
			if (!Success)
			{
				{ std::wstringstream string; string << L"EnableStereo(true) CreateHolographicSpace() failed!"; Log(string); }
			}

			// Show the window to go immersive.
			ShowWindow(stereoWindowHandle, SW_SHOWNORMAL);

			// Force this window into getting input focus.
			ForceAllowInput(stereoWindowHandle);
		}
		else if (!enableStereo && holographicSpace != nullptr)
		{
			Dispose();
		}
#endif
	}

	bool MixedRealityInterop::HasUserPresenceChanged()
	{
		std::lock_guard<std::mutex> lock(PresenceLock);

		bool changedInternal = userPresenceChanged;

		// Reset so we just get this event once.
		if (userPresenceChanged) { userPresenceChanged = false; }

		return changedInternal;
	}

	UserPresence MixedRealityInterop::GetCurrentUserPresence()
	{
		return GetInteropUserPresence();
	}

	bool MixedRealityInterop::IsDisplayOpaque()
	{
		if (HolographicDisplay::GetDefault())
		{
			return HolographicDisplay::GetDefault().IsOpaque();
		}
		return false;
	}

	bool MixedRealityInterop::GetDisplayDimensions(int& width, int& height)
	{
		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		width = 1920;
		height = 1080;

		if (CameraResources == nullptr)
		{
			Log(L"MixedRealityInterop::GetDisplayDimensions: CameraResources is null!");
			return false;
		}

		auto size = CameraResources->GetRenderTargetSize();
		width = (int)(size.Width);
		height = (int)(size.Height);

		return true;
	}

	const wchar_t* MixedRealityInterop::GetDisplayName()
	{
		const wchar_t* name = L"WindowsMixedReality";

		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		if (CameraResources == nullptr)
		{
			return name;
		}

		HolographicCamera camera = CameraResources->GetCamera();
		if (camera == nullptr)
		{
			return name;
		}

		HolographicDisplay display = camera.Display();
		if (display == nullptr)
		{
			return name;
		}

		return display.DisplayName().c_str();
	}

	// Copy a double-wide src texture into a single-wide dst texture with 2 subresources.
	void StereoCopy(
		ID3D11DeviceContext* D3D11Context,
		const float viewportScale,
		ID3D11Texture2D* src,
		ID3D11Texture2D* dst)
	{
		D3D11_TEXTURE2D_DESC desc{};
		dst->GetDesc(&desc);

		const uint32_t scaledWidth = (uint32_t)(desc.Width * viewportScale);
		const uint32_t scaledHeight = (uint32_t)(desc.Height * viewportScale);

		D3D11_BOX box = {};
		box.right = scaledWidth;
		box.bottom = scaledHeight;
		box.back = 1;
		for (int i = 0; i < 2; ++i) { // Copy each eye to HMD backbuffer
			const uint32_t offsetX = (desc.Width - scaledWidth) / 2;
			const uint32_t offsetY = (desc.Height - scaledHeight) / 2;
			D3D11Context->CopySubresourceRegion(dst, i, offsetX, offsetY, 0, src, 0, &box);
			box.left += scaledWidth;
			box.right += scaledWidth;
		}
	}

	bool MixedRealityInterop::IsActiveAndValid()
	{
		if (!IsInitialized()
			|| CameraResources == nullptr)
		{
			return false;
		}
		return true;
	}


	void MixedRealityInterop::BlockUntilNextFrame()
	{
#if HOLOLENS_BLOCKING_PRESENT
		// do nothing, we already blocked in present
#else
		// Wait for a frame to be ready before using it
		// Do not wait for a frame if we are running on the emulator or HL1 Remoting.
		if (!m_isHL1Remoting)
		{
			if (!IsActiveAndValid())
			{
				return;
			}

			try
			{
#if	LOG_HOLOLENS_FRAME_COUNTER
				{ std::wstringstream string; string << L"BlockUntilNextFrame() started"; Log(string); }
				holographicSpace.WaitForNextFrameReady();
				{ std::wstringstream string; string << L"BlockUntilNextFrame() ended"; Log(string); }
#else
				holographicSpace.WaitForNextFrameReady();
#endif
			}
			catch (winrt::hresult_error&)
			{
				Log(L"BlockUntilNextFrame() failed with exception");
			}

		}
#endif
	}

	bool MixedRealityInterop::UpdateRenderThreadFrame()
	{
		if (currentFrame != nullptr
			|| holographicSpace == nullptr
			|| !bInitialized)
		{
			return false;
		}

		HolographicFrame frame = holographicSpace.CreateNextFrame();
		if (frame == nullptr) { return false; }

		for (const auto& cam : frame.RemovedCameras())
		{
			OnCameraRemoved(holographicSpace, cam);
		}

		{
			std::lock_guard<std::mutex> pLock(poseLock);

			if (!IsActiveAndValid())
			{
				return false;
			}

			currentFrame = std::make_unique<TrackingFrame>(frame);
			auto CoordinateSystem = GetReferenceCoordinateSystem();
			if (CoordinateSystem == nullptr)
			{
				currentFrame = nullptr;
				return false;
			}

			if (!currentFrame->CalculatePose(CoordinateSystem))
			{
				// If we fail to calculate a pose for this frame, reset the current frame to try again with a new frame.
				currentFrame = nullptr;
				return false;
			}

#if	LOG_HOLOLENS_FRAME_COUNTER
			{ std::wstringstream string; string << L"UpdateRenderThreadFrame() created " << currentFrame->Count; Log(string); }
#endif
		}


		return true;
	}

	bool MixedRealityInterop::GetCurrentPoseRenderThread(DirectX::XMMATRIX& leftView, DirectX::XMMATRIX& rightView)
	{
		std::lock_guard<std::mutex> lock(poseLock);

		if (!IsActiveAndValid())
		{
			return false;
		}
		if (CameraResources == nullptr)
		{
			return false;
		}

		auto CoordinateSystem = GetReferenceCoordinateSystem();
		if (holographicSpace == nullptr || CoordinateSystem == nullptr) { return false; }

		if (currentFrame == nullptr)
		{
#if	LOG_HOLOLENS_FRAME_COUNTER
			{ std::wstringstream string; string << L"GetCurrentPoseRenderThread() frame is null!"; Log(string); }
#endif
			return false;
		}

#if	LOG_HOLOLENS_FRAME_COUNTER
		{ std::wstringstream string; string << L"GetCurrentPoseRenderThread() getting with " << currentFrame->Count; Log(string); }
#endif

		leftView = CameraResources->leftPose;
		rightView = CameraResources->rightPose;

		return true;
	}

	bool MixedRealityInterop::QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem)
	{
		if (CameraResources == nullptr)
		{
			return false;
		}

		auto CoordinateSystem = GetReferenceCoordinateSystem();
		if (holographicSpace == nullptr || CoordinateSystem == nullptr) { return false; }

		winrt::com_ptr<ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem> ptr{ CoordinateSystem.try_as<ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem>() };

		pCoordinateSystem = ptr.get();
		if (!pCoordinateSystem)
		{
			return false;
		}

		pCoordinateSystem->AddRef();
		return true;
	}

	void MixedRealityInterop::SetTrackingOrigin(HMDTrackingOrigin trackingOrigin)
	{
		desiredTrackingOrigin = trackingOrigin;
	}

	HMDTrackingOrigin MixedRealityInterop::GetTrackingOrigin()
	{
		return currentTrackingOrigin;
	}

	DirectX::XMFLOAT4X4 MixedRealityInterop::GetProjectionMatrix(HMDEye eye)
	{
		std::lock_guard<std::mutex> pLock(poseLock);
		std::lock_guard<std::mutex> lock(disposeLock_GetProjection);

		winrt::Windows::Foundation::Numerics::float4x4 projection = winrt::Windows::Foundation::Numerics::float4x4::identity();

		if (CameraResources == nullptr
			|| CameraResources->Pose == nullptr
			|| (eye == HMDEye::ThirdCamera
				&& (PVCameraResources == nullptr || PVCameraResources->Pose == nullptr)))
		{
			switch (eye)
			{
			case HMDEye::Left:
				projection = LastKnownProjection.Left;
				break;
			case HMDEye::Right:
				projection = LastKnownProjection.Right;
				break;
			case HMDEye::ThirdCamera:
				projection = LastKnownThirdCameraProjection.Left;
				break;
			};
		}
		else
		{
			IHolographicCameraPose pose = CameraResources->Pose;
			if (eye == HMDEye::ThirdCamera)
			{
				pose = PVCameraResources->Pose;
			}

			HolographicStereoTransform CameraProjectionTransform = pose.ProjectionTransform();

			projection = (eye == HMDEye::Left || eye == HMDEye::ThirdCamera)
				? CameraProjectionTransform.Left
				: CameraProjectionTransform.Right;

			if (eye != HMDEye::ThirdCamera)
			{
				LastKnownProjection = HolographicStereoTransform(CameraProjectionTransform);
			}
			else
			{
				LastKnownThirdCameraProjection = HolographicStereoTransform(CameraProjectionTransform);
			}
		}

		return DirectX::XMFLOAT4X4(
			projection.m11, projection.m12, projection.m13, projection.m14,
			projection.m21, projection.m22, projection.m23, projection.m24,
			projection.m31, projection.m32, projection.m33, projection.m34,
			projection.m41, projection.m42, projection.m43, projection.m44);
	}

	void MixedRealityInterop::SetScreenScaleFactor(float scale)
	{
		ScreenScaleFactor = scale;

		std::lock_guard<std::recursive_mutex> lock(CameraResourcesLock);
		if (CameraResources == nullptr)
		{
			return;
		}

		HolographicCamera camera = CameraResources->GetCamera();
		if (camera == nullptr)
		{
			return;
		}

		camera.ViewportScaleFactor(ScreenScaleFactor);
	}

	int32_t MixedRealityInterop::GetMaxQuadLayerCount() const
	{
		if (CameraResources)
		{
			return CameraResources->GetCamera().MaxQuadLayerCount();
		}

		return -1;
	}

	uint32_t MixedRealityInterop::AddQuadLayer(
		uint32_t Id,
		ID3D11Texture2D* quadLayerTexture,
		float widthM, float heightM,
		DirectX::XMFLOAT3 position,
		DirectX::XMFLOAT4 rotation,
		DirectX::XMFLOAT3 scale,
		HMDLayerType layerType,
		bool preserveAspectRatio,
		int priority)
	{
		if (isRemoteHolographicSpace)
		{
			return 0;
		}

		std::lock_guard<std::mutex> lock(quadLayerLock);

		D3D11_TEXTURE2D_DESC desc;
		quadLayerTexture->GetDesc(&desc);

		HolographicQuadLayer quadLayer = HolographicQuadLayer(winrt::Windows::Foundation::Size((float)desc.Width, (float)desc.Height), DirectXPixelFormat::B8G8R8A8UIntNormalized);

		QuadLayer layer(quadLayer);
		layer.index = Id;
		layer.texture = quadLayerTexture;
		layer.width = widthM;
		layer.height = heightM;

		if (preserveAspectRatio)
		{
			D3D11_TEXTURE2D_DESC desc{};
			quadLayerTexture->GetDesc(&desc);

			float R = (float)desc.Width / (float)desc.Height;
			layer.height = widthM / R;
		}

		layer.position = position;
		layer.rotation = rotation;
		layer.scale = scale;

		layer.layerType = layerType;

		if (layerType == HMDLayerType::WorldLocked)
		{
			float3 pos = float3{ layer.position.x, layer.position.y, layer.position.z };
			quaternion rot = quaternion{ layer.rotation.x, layer.rotation.y, layer.rotation.z, layer.rotation.w };

			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem coordinateSystem = GetReferenceCoordinateSystem();

			layer.anchor = winrt::Windows::Perception::Spatial::SpatialAnchor::TryCreateRelativeTo(coordinateSystem, pos, rot);
		}

		layer.priority = priority;

		int priorityIndex = 0;
		for (int i = 0; i < quadLayers.size(); i++)
		{
			if (priority > quadLayers.at(i).priority)
			{
				priorityIndex = i + 1;
			}
		}

		std::vector<QuadLayer>::iterator it = quadLayers.begin();
		quadLayers.insert(it + priorityIndex, layer);

		return layer.index;
	}

	void MixedRealityInterop::RemoveQuadLayer(uint32_t Id)
	{
		std::lock_guard<std::mutex> lock(quadLayerLock);

		int removeIndex = -1;
		for (int i = 0; i < quadLayers.size(); i++)
		{
			if (quadLayers.at(i).index == Id)
			{
				removeIndex = i;
				break;
			}
		}

		if (removeIndex > -1)
		{
			std::vector<QuadLayer>::iterator it = quadLayers.begin();
			quadLayers.erase(it + removeIndex);
		}
	}

	bool MixedRealityInterop::CreateRenderingParameters()
	{
		std::lock_guard<std::mutex> lock(poseLock);
		std::lock_guard<std::recursive_mutex> lock2(CameraResourcesLock);

		bool succeeded = false;

		if (currentFrame == nullptr
			|| currentFrame->Frame == nullptr)
		{
			return succeeded;
		}

		if (PVCameraResources != nullptr
			&& PVCameraResources->Pose != nullptr)
		{
			bool pvSucceeded = true;
			PVCameraResources->CreateRenderingParameters(currentFrame.get(), pvSucceeded);
		}

		if (CameraResources == nullptr
			|| CameraResources->Pose == nullptr)
		{
			return succeeded;
		}

		bool renderingParamsCreated = CameraResources->CreateRenderingParameters(currentFrame.get(), succeeded);

		return succeeded;
	}

	ID3D11Texture2D* MixedRealityInterop::GetBackBufferTexture()
	{
		std::lock_guard<std::mutex> lock(poseLock);
		std::lock_guard<std::recursive_mutex> lock2(CameraResourcesLock);

		if (currentFrame == nullptr
			|| currentFrame->Frame == nullptr
			|| CameraResources == nullptr)
		{
			return nullptr;
		}

		return CameraResources->GetBackBufferTexture();
	}

	bool MixedRealityInterop::CommitDepthBuffer(ID3D11Texture2D* depthTexture)
	{
		std::lock_guard<std::mutex> lock(poseLock);
		std::lock_guard<std::recursive_mutex> lock2(CameraResourcesLock);

		bool succeeded = true;

		if (currentFrame == nullptr
			|| currentFrame->Frame == nullptr
			|| CameraResources == nullptr)
		{
			return succeeded;
		}

		bool renderingParamsCreated = CameraResources->CommitDepthBuffer(currentFrame.get(), depthTexture, succeeded);

		return succeeded;
	}

	bool MixedRealityInterop::CommitThirdCameraDepthBuffer(ID3D11Texture2D* depthTexture)
	{
		std::lock_guard<std::mutex> lock(poseLock);
		std::lock_guard<std::recursive_mutex> lock2(CameraResourcesLock);

		bool succeeded = true;

		if (currentFrame == nullptr
			|| currentFrame->Frame == nullptr
			|| PVCameraResources == nullptr)
		{
			return succeeded;
		}

		bool renderingParamsCreated = PVCameraResources->CommitDepthBuffer(currentFrame.get(), depthTexture, succeeded);

		return succeeded;
	}

	void MixedRealityInterop::SetFocusPointForFrame(DirectX::XMFLOAT3 position)
	{
		LSRPosition = position;
		bIsLSRSetThisFrame = true;
	}

	bool QuadLayerVectorContains(HolographicQuadLayer layer)
	{
		for (HolographicQuadLayer current : CameraResources->GetCamera().QuadLayers())
		{
			if (current == layer)
			{
				return true;
			}
		}

		return false;
	}

	bool MixedRealityInterop::CopyResources(ID3D11DeviceContext* context, ID3D11Texture2D* viewportTexture)
	{
		std::lock_guard<std::mutex> pLock(poseLock);

		if (currentFrame == nullptr
			|| !CameraResources
			|| CameraResources->GetBackBufferTexture() == nullptr
			|| viewportTexture == nullptr)
		{
#if	LOG_HOLOLENS_FRAME_COUNTER
			if (currentFrame == nullptr)
			{
				{ std::wstringstream string; string << L"Present() currentFrame is null"; Log(string); }
			}
			else if (!CameraResources)
			{
				{ std::wstringstream string; string << L"Present() !CameraResources"; Log(string); }
			}
			else if (CameraResources->GetBackBufferTexture() == nullptr)
			{
				{ std::wstringstream string; string << L"Present() CameraResources->GetBackBufferTexture() == nullptr"; Log(string); }
			}
			else //(viewportTexture == nullptr)
			{
				{ std::wstringstream string; string << L"Present() viewportTexture == nullptr"; Log(string); }
			}
#endif
			return true;
		}

		StereoCopy(
			context,
			ScreenScaleFactor,
			viewportTexture,
			CameraResources->GetBackBufferTexture());

		// Copy third camera texture to PVCameraResources backbuffer.
		if (IsThirdCameraActive() && PVCameraResources != nullptr && PVCameraResources->GetBackBufferTexture())
		{
			int w, h;
			GetThirdCameraDimensions(w, h);

			D3D11_TEXTURE2D_DESC desc{};
			viewportTexture->GetDesc(&desc);

			D3D11_BOX box = {};
			box.right = desc.Width;
			box.left = desc.Width - w;
			box.top = 0;
			box.bottom = h;
			box.back = 1;

			context->CopySubresourceRegion(
				PVCameraResources->GetBackBufferTexture(),
				0, 0, 0, 0,
				viewportTexture,
				0,
				&box);
		}

		//Note: HoloLens does not support Quad Layers, so we do not need to render to the third camera here.
		// Quad Layers
		uint32_t maxQuadLayers = (m_isHL1Remoting || (CameraResources.get() == nullptr) || (CameraResources->GetCamera() == nullptr)) ? 0 : CameraResources->GetCamera().MaxQuadLayerCount();
		if (maxQuadLayers > 0)
		{
			if (quadLayers.size() > CameraResources->GetCamera().QuadLayers().Size())
			{
				// quad layer list has changed, clear the existing list so we can render with new priorities.
				CameraResources->GetCamera().QuadLayers().Clear();
			}
			for (QuadLayer layer : quadLayers)
			{
				if (!layer.ValidateData())
				{
					continue;
				}

				if (CameraResources->GetCamera().QuadLayers().Size() < maxQuadLayers &&
					!QuadLayerVectorContains(layer.quadLayer))
				{
					CameraResources->GetCamera().QuadLayers().Append(layer.quadLayer);
				}

				auto quadLayerUpdateParams = currentFrame->Frame.GetQuadLayerUpdateParameters(layer.quadLayer);
				IDirect3DSurface surface = quadLayerUpdateParams.AcquireBufferToUpdateContent();

				ComPtr<IDXGISurface2> quadLayerBackBufferSurface;
				winrt::check_hresult(
					surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()->GetInterface(
						IID_PPV_ARGS(&quadLayerBackBufferSurface)));

				UINT32 subresourceIndex = 0;
				ComPtr<ID3D11Texture2D> quadLayerBackBuffer;
				winrt::check_hresult(quadLayerBackBufferSurface->GetResource(IID_PPV_ARGS(&quadLayerBackBuffer), &subresourceIndex));

				context->CopyResource(
					quadLayerBackBuffer.Get(),
					layer.texture);

				quadLayerUpdateParams.UpdateExtents({ layer.width * layer.scale.x, layer.height * layer.scale.y });

				float3 pos = float3{ layer.position.x, layer.position.y, layer.position.z };
				quaternion rot = quaternion{ layer.rotation.x, layer.rotation.y, layer.rotation.z, layer.rotation.w };

				if (layer.layerType == HMDLayerType::FaceLocked)
				{
					quadLayerUpdateParams.UpdateLocationWithDisplayRelativeMode(pos, rot);
				}
				else
				{
					if (layer.anchor != nullptr)
					{
						quadLayerUpdateParams.UpdateLocationWithStationaryMode(layer.anchor.CoordinateSystem(), float3::zero(), quaternion::identity());
					}
					else
					{
						quadLayerUpdateParams.UpdateLocationWithStationaryMode(StationaryReferenceFrame.CoordinateSystem(), pos, rot);
					}
				}
			}
		}

		return true;
	}

	bool MixedRealityInterop::Present()
	{
		std::lock_guard<std::mutex> pLock(poseLock);
		std::lock_guard<std::mutex> lock(disposeLock_Present);
		if (currentFrame == nullptr)
		{
			return true;
		}

		std::lock_guard<std::recursive_mutex> lock2(CameraResourcesLock);
		if (m_isHL1Remoting || ((CameraResources.get() != nullptr) && (CameraResources->GetCamera() != nullptr)))
		{
#if HOLOLENS_BLOCKING_PRESENT
			HolographicFramePresentResult presentResult = currentFrame->Frame.PresentUsingCurrentPrediction();
#	if	LOG_HOLOLENS_FRAME_COUNTER
			{ std::wstringstream string; string << L"Present() PresentUsingCurrentPrediction with " << currentFrame->Count; Log(string); }
#	endif
#else
			HolographicFramePresentResult presentResult = currentFrame->Frame.PresentUsingCurrentPrediction(HolographicFramePresentWaitBehavior::DoNotWaitForFrameToFinish);
#	if	LOG_HOLOLENS_FRAME_COUNTER
			{ std::wstringstream string; string << L"Present() PresentUsingCurrentPrediction(donotwait) with " << currentFrame->Count; Log(string); }
#	endif
#endif
		}

		// We should not use this again now that we have presented.
		currentFrame = nullptr;

		return true;
	}

	bool MixedRealityInterop::SupportsSpatialInput()
	{
		return supportsSpatialInput;
	}

	bool MixedRealityInterop::SupportsHandTracking() const
	{
		return supportsHandTracking;
	}

	bool MixedRealityInterop::SupportsHandedness()
	{
		return supportsHandedness;
	}

	bool MixedRealityInterop::SupportsEyeTracking() const
	{
		return supportsEyeTracking;
	}

	void MixedRealityInterop::RequestUserPermissionForEyeTracking()
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		if (supportsEyeTracking && !eyeTrackingAllowed)
		{
			EyesPose::RequestAccessAsync().Completed([=](auto&& sender, winrt::Windows::Foundation::AsyncStatus const  args)
				{
					if (args == winrt::Windows::Foundation::AsyncStatus::Completed)
					{
						eyeTrackingAllowed = (sender.GetResults() == winrt::Windows::UI::Input::GazeInputAccessStatus::Allowed);
					}
				});
		}
#endif
	}

	bool MixedRealityInterop::IsEyeTrackingAllowed() const
	{
		return eyeTrackingAllowed;
	}

	bool MixedRealityInterop::GetEyeGaze(EyeGazeRay& eyeRay)
	{
		memset(&eyeRay, 0, sizeof(eyeRay));

#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		if (!supportsEyeTracking || !eyeTrackingAllowed)
		{
			return false;
		}

		try
		{
			auto coordinateSystem = GetReferenceCoordinateSystem();
			if (coordinateSystem == nullptr) { return false; }

			auto currentTimeStamp = PerceptionTimestampHelper::FromHistoricalTargetTime(winrt::clock::now());

			auto pointerPose = SpatialPointerPose::TryGetAtTimestamp(coordinateSystem, currentTimeStamp);
			if (pointerPose == nullptr) { return false; }

			auto eyePos = pointerPose.Eyes();
			if (eyePos == nullptr) { return false; }

			auto gaze = eyePos.Gaze();
			if (gaze == nullptr) { return false; }

			auto orig = gaze.Value().Origin;
			auto dir = gaze.Value().Direction;

			eyeRay.origin = ToDirectXVec(gaze.Value().Origin);
			eyeRay.direction = ToDirectXVec(gaze.Value().Direction);

			return true;
		}
		catch (winrt::hresult_error&)
		{
		}

#endif

		return false;
	}

	bool CheckHandedness(SpatialInteractionSource source, HMDHand hand)
	{
		if (!supportsHandedness
			|| source.Handedness() == SpatialInteractionSourceHandedness::Unspecified)
		{
			return HandIDs[(int)hand] == source.Id();
		}

		SpatialInteractionSourceHandedness desiredHandedness = (hand == HMDHand::Left) ?
			SpatialInteractionSourceHandedness::Left : SpatialInteractionSourceHandedness::Right;

		return source.Handedness() == desiredHandedness;
	}

	bool GetInputSources(IVectorView<SpatialInteractionSourceState>& sourceStates)
	{
		if (interactionManager == nullptr
			|| holographicSpace == nullptr
			|| !bInitialized)
		{
			return false;
		}

		try
		{
			Microsoft::WRL::ComPtr<ABI::Windows::Perception::IPerceptionTimestampHelperStatics> timestampStatics;
			HRESULT hr = Windows::Foundation::GetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Perception_PerceptionTimestampHelper).Get(), &timestampStatics);

			// Convert from winrt DateTime to ABI DateTime.
			winrt::Windows::Foundation::DateTime dt = winrt::clock::now();
			winrt::Windows::Foundation::TimeSpan timespan = dt.time_since_epoch();

			ABI::Windows::Foundation::DateTime dt_abi;
			dt_abi.UniversalTime = timespan.count();

			Microsoft::WRL::ComPtr < ABI::Windows::Perception::IPerceptionTimestamp> timestamp;
			timestampStatics->FromHistoricalTargetTime(dt_abi, &timestamp);

			if (timestamp == nullptr)
			{
				return false;
			}

			winrt::Windows::Perception::PerceptionTimestamp ts = nullptr;

			winrt::check_hresult(timestamp->QueryInterface(winrt::guid_of<winrt::Windows::Perception::PerceptionTimestamp>(),
				reinterpret_cast<void**>(winrt::put_abi(ts))));

			sourceStates = interactionManager.GetDetectedSourcesAtTimestamp(ts);

			return true;
		}
		catch (winrt::hresult_error&)
		{
			return false;
		}
	}

	bool MixedRealityInterop::GetPointerPose(
		HMDHand hand,
		PointerPoseInfo& pose)
	{
		if (!IsInitialized())
		{
			return false;
		}

		pose = PointerPoses[(int)hand];
		return true;
	}

	HMDTrackingStatus MixedRealityInterop::GetControllerTrackingStatus(HMDHand hand)
	{
		HMDTrackingStatus trackingStatus = HMDTrackingStatus::NotTracked;

		if (!IsInitialized())
		{
			return trackingStatus;
		}

		IVectorView<SpatialInteractionSourceState> sourceStates;
		if (!GetInputSources(sourceStates))
		{
			return trackingStatus;
		}

		int sourceCount = sourceStates.Size();
		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			SpatialInteractionSource source = state.Source();
			if (source == nullptr)
			{
				continue;
			}

			if (!CheckHandedness(source, hand))
			{
				continue;
			}

			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem coordinateSystem = GetReferenceCoordinateSystem();
			if (coordinateSystem != nullptr)
			{
				SpatialInteractionSourceProperties prop = state.Properties();
				if (prop == nullptr)
				{
					continue;
				}

				SpatialInteractionSourceLocation sourceLocation = prop.TryGetLocation(coordinateSystem);
				if (sourceLocation != nullptr)
				{
					if (!m_isHL1Remoting && source.IsPointingSupported() && sourceLocation.SourcePointerPose() != nullptr)
					{
						float3 pos = sourceLocation.SourcePointerPose().Position();
						float3 forward = sourceLocation.SourcePointerPose().ForwardDirection();
						float3 up = sourceLocation.SourcePointerPose().UpDirection();
						quaternion rot = sourceLocation.SourcePointerPose().Orientation();

						PointerPoses[(int)hand].origin = DirectX::XMFLOAT3(pos.x, pos.y, pos.z);
						PointerPoses[(int)hand].direction = DirectX::XMFLOAT3(forward.x, forward.y, forward.z);
						PointerPoses[(int)hand].up = DirectX::XMFLOAT3(up.x, up.y, up.z);
						PointerPoses[(int)hand].orientation = DirectX::XMFLOAT4(rot.x, rot.y, rot.z, rot.w);
					}

					if (sourceLocation.Position() != nullptr)
					{
						ControllerPositions[(int)hand] = sourceLocation.Position().Value();
						trackingStatus = HMDTrackingStatus::Tracked;
					}
					if (supportsSourceOrientation &&
						(sourceLocation.Orientation() != nullptr))
					{
						ControllerOrientations[(int)hand] = sourceLocation.Orientation().Value();

						if (sourceLocation.Position() == nullptr)
						{
							trackingStatus = HMDTrackingStatus::InertialOnly;
						}
					}
					else
					{
						ControllerOrientations[(int)hand] = quaternion::identity();
					}
				}
			}
		}

		ControllerIsTracked[(int)hand] =
			trackingStatus == HMDTrackingStatus::Tracked ? true : false;

		return trackingStatus;
	}

	bool MixedRealityInterop::GetControllerOrientationAndPosition(HMDHand hand, DirectX::XMFLOAT4& orientation, DirectX::XMFLOAT3& position)
	{
		if (!supportsHandedness)
		{
			if (HandIDs[(int)hand] == -1)
			{
				return false;
			}
		}

		float3 pos = ControllerPositions[(int)hand];
		quaternion rot = ControllerOrientations[(int)hand];

		orientation = DirectX::XMFLOAT4(rot.x, rot.y, rot.z, rot.w);
		position = DirectX::XMFLOAT3(pos.x, pos.y, pos.z);

		return ControllerIsTracked[(int)hand];
	}

	bool MixedRealityInterop::GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, DirectX::XMFLOAT4& orientation, DirectX::XMFLOAT3& position, float& radius)
	{
		if (!supportsHandTracking)
		{
			if (HandIDs[(int)hand] == -1)
			{
				return false;
			}
		}

		if (!JointPoseValid[(int)hand])
		{
			return false;
		}

		float3 pos = JointPoses[(int)hand][(int)joint].Position;
		quaternion rot = JointPoses[(int)hand][(int)joint].Orientation;
		radius = JointPoses[(int)hand][(int)joint].Radius;

		// Rotate the bones to the MS API's new frame
		rot *= make_quaternion_from_yaw_pitch_roll(0.0f, DirectX::XM_PI, 0.0f);
		rot = normalize(rot);

		orientation = DirectX::XMFLOAT4(rot.x, rot.y, rot.z, rot.w);
		position = DirectX::XMFLOAT3(pos.x, pos.y, pos.z);

		return true;
	}

	HMDInputPressState PressStateFromBool(bool isPressed)
	{
		return isPressed ?
			HMDInputPressState::Pressed :
			HMDInputPressState::Released;
	}

	void UpdateButtonStates(SpatialInteractionSourceState state)
	{
		SpatialInteractionSource source = state.Source();
		if (source == nullptr)
		{
			return;
		}

		int handIndex = 0;
		if (supportsHandedness
			&& source.Handedness() != SpatialInteractionSourceHandedness::Unspecified)
		{
			// Find hand index from source handedness.
			SpatialInteractionSourceHandedness handedness = source.Handedness();
			if (handedness != SpatialInteractionSourceHandedness::Left)
			{
				handIndex = 1;
			}
		}
		else
		{
			// If source does not support handedness, find hand index from HandIDs array.
			handIndex = -1;
			for (int i = 0; i < 2; i++)
			{
				if (source.Id() == HandIDs[i])
				{
					handIndex = i;
					break;
				}
			}

			if (handIndex == -1)
			{
				// No hands.
				return;
			}
		}

		if (isRemoteHolographicSpace)
		{
			// Prior to motion controller support, Select was the only press
			bool isPressed = state.IsPressed();
			PreviousSelectState[handIndex] = CurrentSelectState[handIndex];
			CurrentSelectState[handIndex] = PressStateFromBool(isPressed);

			// HoloLens 2 supports grasp over remoting.
			if (supportsMotionControllers && !m_isHL1Remoting)
			{
				// Grasp
				isPressed = state.IsGrasped();
				PreviousGraspState[handIndex] = CurrentGraspState[handIndex];
				CurrentGraspState[handIndex] = PressStateFromBool(isPressed);
			}
		}
		else if (supportsMotionControllers && !isRemoteHolographicSpace)
		{
			// Select
			bool isPressed = state.IsSelectPressed();
			PreviousSelectState[handIndex] = CurrentSelectState[handIndex];
			CurrentSelectState[handIndex] = PressStateFromBool(isPressed);

			// Grasp
			isPressed = state.IsGrasped();
			PreviousGraspState[handIndex] = CurrentGraspState[handIndex];
			CurrentGraspState[handIndex] = PressStateFromBool(isPressed);

			// Menu
			isPressed = state.IsMenuPressed();
			PreviousMenuState[handIndex] = CurrentMenuState[handIndex];
			CurrentMenuState[handIndex] = PressStateFromBool(isPressed);

			SpatialInteractionControllerProperties controllerProperties = state.ControllerProperties();
			if (controllerProperties == nullptr)
			{
				// All remaining controller buttons require the controller properties.
				return;
			}

			// Thumbstick
			isPressed = controllerProperties.IsThumbstickPressed();
			PreviousThumbstickPressState[handIndex] = CurrentThumbstickPressState[handIndex];
			CurrentThumbstickPressState[handIndex] = PressStateFromBool(isPressed);

			// Touchpad
			isPressed = controllerProperties.IsTouchpadPressed();
			PreviousTouchpadPressState[handIndex] = CurrentTouchpadPressState[handIndex];
			CurrentTouchpadPressState[handIndex] = PressStateFromBool(isPressed);

			// Touchpad (is touched)
			isPressed = controllerProperties.IsTouchpadTouched();
			PreviousTouchpadIsTouchedState[handIndex] = CurrentTouchpadIsTouchedState[handIndex];
			CurrentTouchpadIsTouchedState[handIndex] = PressStateFromBool(isPressed);
		}
	}

	bool HandCurrentlyTracked(int id)
	{
		for (int i = 0; i < 2; i++)
		{
			if (HandIDs[i] == id)
			{
				return true;
			}
		}

		return false;
	}

	void AddHand(int id)
	{
		// Check right hand first (index 1).
		for (int i = 1; i >= 0; i--)
		{
			if (HandIDs[i] == -1)
			{
				HandIDs[i] = id;
				return;
			}
		}
	}

	void UpdateTrackedHands(IVectorView<SpatialInteractionSourceState> sourceStates)
	{
		winrt::Windows::Perception::Spatial::SpatialCoordinateSystem coordinateSystem = GetReferenceCoordinateSystem();
		if (coordinateSystem == nullptr)
		{
			Log(L"UpdateTrackedHands - unable to get reference coordinate system - hand skeleton data may be invalid");
		}

		int sourceCount = sourceStates.Size();

		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			SpatialInteractionSource source = state.Source();
			if (source == nullptr)
			{
				continue;
			}

			if (!HandCurrentlyTracked(source.Id()))
			{
				AddHand(source.Id());
			}
		}
	}

	// Reset any lost hands.
	void ResetHandIDs(IVectorView<SpatialInteractionSourceState> sourceStates)
	{
		int sourceCount = sourceStates.Size();

		for (int i = 0; i < 2; i++)
		{
			// Hand already reset.
			if (HandIDs[i] == -1)
			{
				continue;
			}

			bool handFound = false;
			for (int j = 0; j < sourceCount; j++)
			{
				SpatialInteractionSourceState state = sourceStates.GetAt(j);
				if (state == nullptr)
				{
					continue;
				}

				SpatialInteractionSource source = state.Source();
				if (source == nullptr)
				{
					continue;
				}

				if (HandIDs[i] == source.Id())
				{
					handFound = true;
					break;
				}
			}

			if (!handFound)
			{
				HandIDs[i] = -1;
				JointPoseValid[i] = false;
			}
		}
	}

	void MixedRealityInterop::PollInput()
	{
		IVectorView<SpatialInteractionSourceState> sourceStates;
		if (!GetInputSources(sourceStates))
		{
			return;
		}

		// Update unhanded controller mapping.
		if (isRemoteHolographicSpace)
		{
			// Remove and hands that have been removed since last update.
			ResetHandIDs(sourceStates);

			// Add new tracked hands.
			UpdateTrackedHands(sourceStates);
		}

		int sourceCount = sourceStates.Size();
		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			UpdateButtonStates(state);
		}
	}

	void MixedRealityInterop::PollHandTracking()
	{
		HMDTrackingStatus trackingStatus = HMDTrackingStatus::NotTracked;

		if (!IsInitialized())
		{
			return;
		}

		// Reset joint pose flag for both hands here, since we may not have an input source for a hand.
		// Any sources we do have will get correctly set when retrieving joints.
		for (int i = 0; i < 2; i++)
		{
			JointPoseValid[i] = false;
		}

		IVectorView<SpatialInteractionSourceState> sourceStates;
		if (!GetInputSources(sourceStates))
		{
			return;
		}

		int sourceCount = sourceStates.Size();
		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			SpatialInteractionSource source = state.Source();
			if (source == nullptr)
			{
				continue;
			}

			HMDHand hand = HMDHand::AnyHand;
			if (CheckHandedness(source, HMDHand::Left))
			{
				hand = HMDHand::Left;
			}
			else if (CheckHandedness(source, HMDHand::Right))
			{
				hand = HMDHand::Right;
			}
			else
			{
				continue;
			}

			winrt::Windows::Perception::Spatial::SpatialCoordinateSystem coordinateSystem = GetReferenceCoordinateSystem();
			if (coordinateSystem != nullptr)
			{
				SpatialInteractionSourceProperties prop = state.Properties();
				if (prop == nullptr)
				{
					continue;
				}

				SpatialInteractionSourceLocation sourceLocation = prop.TryGetLocation(coordinateSystem);
				if (sourceLocation != nullptr)
				{
					if (supportsSourceOrientation &&
						(sourceLocation.Orientation() != nullptr))
					{
						if (supportsHandTracking)
						{
							HandPose handPose = state.TryGetHandPose();
							if (handPose != nullptr)
							{
								if (HandMeshUpdateObserver::IsInitialized())
								{
									std::shared_ptr<HandMeshUpdateObserver> Observer;

									if (MeshUpdateObserverMap.count((uint32_t)hand) == 0)
									{
										Observer = std::make_shared<HandMeshUpdateObserver>();
										MeshUpdateObserverMap[(uint32_t)hand] = Observer;
									}
									else
									{
										Observer = MeshUpdateObserverMap[(uint32_t)hand];
									}

									if (Observer->GetSourceId() != source.Id())
									{
										Observer->InitAsync(source);
									}

									Observer->Update(handPose, coordinateSystem);
								}
								JointPoseValid[(int)hand] = handPose.TryGetJoints(coordinateSystem, Joints, JointPoses[(int)hand]);
							}
						}
					}
				}
			}
		}
	}

	HMDInputPressState MixedRealityInterop::GetPressState(HMDHand hand, HMDInputControllerButtons button, bool onlyRegisterClicks)
	{
		int index = (int)hand;

		HMDInputPressState pressState = HMDInputPressState::NotApplicable;

		switch (button)
		{
		case HMDInputControllerButtons::Grasp:
			pressState = (!onlyRegisterClicks) ? CurrentGraspState[index] :
				(CurrentGraspState[index] != PreviousGraspState[index]) ? CurrentGraspState[index] : HMDInputPressState::NotApplicable;
			break;

		case HMDInputControllerButtons::Menu:
			pressState = (!onlyRegisterClicks) ? CurrentMenuState[index] :
				(CurrentMenuState[index] != PreviousMenuState[index]) ? CurrentMenuState[index] : HMDInputPressState::NotApplicable;
			break;

		case HMDInputControllerButtons::Select:
			pressState = (!onlyRegisterClicks) ? CurrentSelectState[index] :
				(CurrentSelectState[index] != PreviousSelectState[index]) ? CurrentSelectState[index] : HMDInputPressState::NotApplicable;
			break;

		case HMDInputControllerButtons::Thumbstick:
			pressState = (!onlyRegisterClicks) ? CurrentThumbstickPressState[index] :
				(CurrentThumbstickPressState[index] != PreviousThumbstickPressState[index]) ? CurrentThumbstickPressState[index] : HMDInputPressState::NotApplicable;
			break;

		case HMDInputControllerButtons::Touchpad:
			pressState = (!onlyRegisterClicks) ? CurrentTouchpadPressState[index] :
				(CurrentTouchpadPressState[index] != PreviousTouchpadPressState[index]) ? CurrentTouchpadPressState[index] : HMDInputPressState::NotApplicable;
			break;

		case HMDInputControllerButtons::TouchpadIsTouched:
			pressState = (!onlyRegisterClicks) ? CurrentTouchpadIsTouchedState[index] :
				(CurrentTouchpadIsTouchedState[index] != PreviousTouchpadIsTouchedState[index]) ? CurrentTouchpadIsTouchedState[index] : HMDInputPressState::NotApplicable;
			break;
		}

		return pressState;
	}

	void MixedRealityInterop::ResetButtonStates()
	{
		for (int i = 0; i < 2; i++)
		{
			CurrentSelectState[i] = HMDInputPressState::NotApplicable;
			PreviousSelectState[i] = HMDInputPressState::NotApplicable;

			CurrentGraspState[i] = HMDInputPressState::NotApplicable;
			PreviousGraspState[i] = HMDInputPressState::NotApplicable;

			CurrentMenuState[i] = HMDInputPressState::NotApplicable;
			PreviousMenuState[i] = HMDInputPressState::NotApplicable;

			CurrentThumbstickPressState[i] = HMDInputPressState::NotApplicable;
			PreviousThumbstickPressState[i] = HMDInputPressState::NotApplicable;

			CurrentTouchpadPressState[i] = HMDInputPressState::NotApplicable;
			PreviousTouchpadPressState[i] = HMDInputPressState::NotApplicable;

			CurrentTouchpadIsTouchedState[i] = HMDInputPressState::NotApplicable;
			PreviousTouchpadIsTouchedState[i] = HMDInputPressState::NotApplicable;
		}
	}

	float MixedRealityInterop::GetAxisPosition(HMDHand hand, HMDInputControllerAxes axis)
	{
		if (!supportsMotionControllers || isRemoteHolographicSpace)
		{
			return 0.0f;
		}

		IVectorView<SpatialInteractionSourceState> sourceStates;
		if (!GetInputSources(sourceStates))
		{
			return 0.0f;
		}

		int sourceCount = sourceStates.Size();
		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			SpatialInteractionSource source = state.Source();
			if (source == nullptr)
			{
				continue;
			}

			if (!CheckHandedness(source, hand))
			{
				continue;
			}

			if (axis == HMDInputControllerAxes::SelectValue)
			{
				return static_cast<float>(state.SelectPressedValue());
			}

			SpatialInteractionControllerProperties controllerProperties = state.ControllerProperties();
			if (controllerProperties == nullptr)
			{
				return 0.0f;
			}

			double axisValue = 0.0;
			switch (axis)
			{
			case HMDInputControllerAxes::ThumbstickX:
				axisValue = controllerProperties.ThumbstickX();
				break;

			case HMDInputControllerAxes::ThumbstickY:
				axisValue = controllerProperties.ThumbstickY();
				break;

			case HMDInputControllerAxes::TouchpadX:
				axisValue = controllerProperties.TouchpadX();
				break;

			case HMDInputControllerAxes::TouchpadY:
				axisValue = controllerProperties.TouchpadY();
				break;
			}

			return static_cast<float>(axisValue);
		}

		return 0.0f;
	}

	void MixedRealityInterop::SubmitHapticValue(HMDHand hand, float value)
	{
		if (!supportsHapticFeedback || isRemoteHolographicSpace)
		{
			return;
		}

		IVectorView<SpatialInteractionSourceState> sourceStates;
		if (!GetInputSources(sourceStates))
		{
			return;
		}

		int sourceCount = sourceStates.Size();
		for (int i = 0; i < sourceCount; i++)
		{
			SpatialInteractionSourceState state = sourceStates.GetAt(i);
			if (state == nullptr)
			{
				continue;
			}

			SpatialInteractionSource source = state.Source();
			if (source == nullptr)
			{
				continue;
			}

			if (!CheckHandedness(source, hand))
			{
				continue;
			}

			SpatialInteractionController controller = source.Controller();
			if (controller == nullptr)
			{
				return;
			}

			SimpleHapticsController hapticsController = controller.SimpleHapticsController();
			if (hapticsController == nullptr)
			{
				return;
			}

			IVectorView<SimpleHapticsControllerFeedback> supportedFeedback = hapticsController.SupportedFeedback();
			uint32_t feedbackSize = supportedFeedback.Size();
			if (feedbackSize == 0)
			{
				return;
			}

			SimpleHapticsControllerFeedback feedback = nullptr;
			for (uint32_t i = 0; i < feedbackSize; i++)
			{
				SimpleHapticsControllerFeedback feed = supportedFeedback.GetAt(i);
				if (feed == nullptr)
				{
					break;
				}

				// Check for specific waveform(s)
				uint16_t waveform = feed.Waveform();
				if (waveform == KnownSimpleHapticsControllerWaveforms::BuzzContinuous())
				{
					// We found a suitable waveform
					feedback = feed;
					break;
				}
			}

			if (feedback == nullptr)
			{
				// We did not find a suitable waveform.
				return;
			}

			// Submit the feedback value
			if (value > 0.0f)
			{
				hapticsController.SendHapticFeedback(
					feedback,
					static_cast<double>(value));
			}
			else
			{
				hapticsController.StopFeedback();
			}
		}
	}

	SpeechRecognizerInterop::SpeechRecognizerInterop()
	{
		std::lock_guard<std::mutex> lock(speechRecognizerLock);

		id = speechRecognizerIndex;
		speechRecognizerMap[id] = new SpeechRecognizer();

		speechRecognizerIndex++;
	}

	void SpeechRecognizerInterop::AddKeyword(const wchar_t* keyword, std::function<void()> callback)
	{
#if HOLO_STREAMING_RENDERING
		// Remoting supports a single remoteSpeech object.  
		// Keywords are aggregated in OnBeginPlay and a single speech recognizer is created, so this will work for all keywords.
		if (id > 0)
		{
			return;
		}
#endif

		if (speechRecognizerMap[id] == nullptr)
		{
			return;
		}

		speechRecognizerMap[id]->AddKeyword(winrt::hstring(keyword), callback);
	}

	void SpeechRecognizerInterop::StartSpeechRecognition()
	{
#if HOLO_STREAMING_RENDERING
		// Remoting supports a single remoteSpeech object.  
		// Keywords are aggregated in OnBeginPlay and a single speech recognizer is created, so this will work for all keywords.
		if (id > 0)
		{
			return;
		}
#endif

		if (speechRecognizerMap[id] == nullptr)
		{
			return;
		}

#if !HOLO_STREAMING_RENDERING
		speechRecognizerMap[id]->StartSpeechRecognizer();
#else
		if (m_remoteContext == nullptr
			|| remoteSpeech == nullptr)
		{
			speechRecognizerMap[id]->StartSpeechRecognizer();
			return;
		}

		CreateGrammarFileAsync().then([this](winrt::Windows::Storage::IStorageFile grammarFile)
			{
				std::vector<winrt::hstring> dictionary;
				for (auto keywordPair : speechRecognizerMap[id]->KeywordMap())
				{
					{ std::wstringstream string; string << L"Adding Keyword " << keywordPair.first.c_str(); Log(string); }

					dictionary.push_back(keywordPair.first);
				}

				remoteSpeech.ApplyParameters(L"en-US", grammarFile, dictionary);

				m_onRecognizedSpeechRevoker = remoteSpeech.OnRecognizedSpeech(
					winrt::auto_revoke, [this](const winrt::Microsoft::Holographic::AppRemoting::RecognizedSpeech& recognizedSpeech)
					{
						{ std::wstringstream string; string << L"Evaluating Keyword " << recognizedSpeech.RecognizedText.c_str(); Log(string); }

						for (auto keywordPair : speechRecognizerMap[id]->KeywordMap())
						{
							if (recognizedSpeech.RecognizedText == keywordPair.first)
							{
								{ std::wstringstream string; string << L"Recognized Keyword " << recognizedSpeech.RecognizedText.c_str(); Log(string); }

								keywordPair.second();
								break;
							}
						}
					});
			});
#endif
	}

	void SpeechRecognizerInterop::StopSpeechRecognition()
	{
		if (speechRecognizerMap[id] == nullptr)
		{
			return;
		}

		speechRecognizerMap[id]->StopSpeechRecognizer();

		auto it = speechRecognizerMap.find(id);
		if (it != speechRecognizerMap.end())
		{
			delete it->second;
			speechRecognizerMap.erase(it);
		}
	}

	bool CreateSpatialAnchorHelper(MixedRealityInterop& inThis)
	{
		Log(L"CreateSpatialAnchorHelper");
		m_spatialAnchorHelper.reset(new WindowsMixedReality::SpatialAnchorHelper(inThis, m_logCallback));
		if (m_spatialAnchorHelper)
		{
			Log(L"CreateSpatialAnchorHelper created");
		}
		return m_spatialAnchorHelper != nullptr;
	}

	void DestroySpatialAnchorHelper()
	{
		Log(L"DestroySpatialAnchorHelper");
		m_spatialAnchorHelper = nullptr;
	}

	bool MixedRealityInterop::IsSpatialAnchorStoreLoaded() const
	{
		// SpatialAnchorManager::RequestStoreAsync() can fail if called before a valid spatial locator is created.
		// This can also fail if called outside of a stereo context while remoting, since the remoting stack may think there is a valid locator.
		// Calls to this function should make a stereo check.
		if (!IsInitialized() ||
			Locator == nullptr ||
			StationaryReferenceFrame == nullptr)
		{
			Log(L"IsSpatialAnchorStoreLoaded: HoloLens application is not yet initialized!  Doing nothing.");
			return false;
		}

		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->IsSpatialAnchorStoreLoaded();
		}
		else
		{
			return false;
		}
	}

	bool MixedRealityInterop::CreateAnchor(const wchar_t* anchorId, const DirectX::XMFLOAT3 inPosition, DirectX::XMFLOAT4 inRotationQuat)
	{
		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->CreateAnchor(anchorId, inPosition, inRotationQuat, GetReferenceCoordinateSystem());
		}
		else
		{
			Log(L"CreateAnchor: m_spatialAnchorHelper is null!  Doing nothing.");
			return false;
		}
	}

	void MixedRealityInterop::RemoveAnchor(const wchar_t* anchorId)
	{
		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->RemoveAnchor(anchorId);
		}
		else
		{
			Log(L"RemoveAnchor: m_spatialAnchorHelper is null!  Doing nothing.");
			return;
		}
	}

	bool MixedRealityInterop::DoesAnchorExist(const wchar_t* anchorId) const
	{
		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->DoesAnchorExist(anchorId);
		}
		else
		{
			Log(L"DoesAnchorExist: m_spatialAnchorHelper is null!  Returning false.");
			return false;
		}
	}

	bool MixedRealityInterop::GetAnchorPose(const wchar_t* anchorId, DirectX::XMFLOAT3& outScale, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outTrans) const
	{
		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->GetAnchorPose(anchorId, outScale, outRot, outTrans, GetReferenceCoordinateSystem());
		}
		else
		{
			Log(L"GetAnchorPose: m_spatialAnchorHelper is null!  Doing nothing.");
			return false;
		}
	}

	bool MixedRealityInterop::SaveAnchor(const wchar_t* saveId, const wchar_t* anchorId)
	{
		if (!IsSpatialAnchorStoreLoaded())
		{
			Log(L"SaveAnchor: Anchor Store is not laoded!  Doing nothing.");
			return false;
		}

		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->SaveAnchor(saveId, anchorId);
		}
		else
		{
			Log(L"SaveAnchor: m_spatialAnchorHelper is null!  Doing nothing.");
			return false;
		}
	}

	void MixedRealityInterop::RemoveSavedAnchor(const wchar_t* saveId)
	{
		if (!IsSpatialAnchorStoreLoaded())
		{
			Log(L"RemoveSavedAnchor: Anchor Store is not laoded!  Doing nothing.");
			return;
		}

		if (m_spatialAnchorHelper)
		{
			m_spatialAnchorHelper->RemoveSavedAnchor(saveId);
		}
		else
		{
			Log(L"RemoveSavedAnchor: m_spatialAnchorHelper is null!  Doing nothing.");
		}
	}

	bool MixedRealityInterop::LoadAnchors(std::function<void(const wchar_t* saveId, const wchar_t* anchorId)> anchorIdWritingFunctionPointer)
	{
		if (!IsSpatialAnchorStoreLoaded())
		{
			Log(L"LoadAnchors: Anchor Store is not laoded!  Doing nothing.");
			return false;
		}

		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->LoadAnchors(anchorIdWritingFunctionPointer);
		}
		else
		{
			Log(L"LoadAnchors: m_spatialAnchorHelper is null!  Doing nothing.");
			return false;
		}
	}

	void MixedRealityInterop::ClearSavedAnchors()
	{
		if (!IsSpatialAnchorStoreLoaded())
		{
			Log(L"ClearSavedAnchors: Anchor Store is not laoded!  Doing nothing.");
			return;
		}

		if (m_spatialAnchorHelper)
		{
			m_spatialAnchorHelper->ClearSavedAnchors();
		}
		else
		{
			Log(L"ClearSavedAnchors: m_spatialAnchorHelper is null!  Doing nothing.");
			return;
		}
	}

	bool MixedRealityInterop::DidAnchorCoordinateSystemChange()
	{
		if (m_spatialAnchorHelper)
		{
			return m_spatialAnchorHelper->DidAnchorCoordinateSystemChange();
		}
		else
		{
			Log(L"DidAnchorCoordinateSystemChange: m_spatialAnchorHelper is null!  Doing nothing.");
			return false;
		}
	}

	GestureRecognizerInterop::GestureRecognizerInterop()
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);

		id = gestureRecognizerIndex;

		gestureRecognizerMap[id] = std::make_shared<GestureRecognizer>(StationaryReferenceFrame);
		gestureRecognizerIndex++;
	}

	void ReleaseSpatialRecognizers()
	{
		{
			std::lock_guard<std::mutex> lock(gestureRecognizerLock);
			for (auto&& p : gestureRecognizerMap)
			{
				p.second->Clean();
			}
		}
		interactionManager = nullptr;
	}

	GestureRecognizerInterop::~GestureRecognizerInterop()
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return;
		}

		auto it = gestureRecognizerMap.find(id);
		if (it != gestureRecognizerMap.end())
		{
			gestureRecognizerMap.erase(it);
		}
	}

	bool GestureRecognizerInterop::SubscribeInteration(std::function<void()> callback)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeInteration(callback);
	}

	bool GestureRecognizerInterop::SubscribeSourceStateChanges(SourceStateCallback callback)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeSourceStateChanges(callback);
	}

	void GestureRecognizerInterop::Reset()
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return;
		}

		gestureRecognizerMap[id]->Reset();
	}

	bool GestureRecognizerInterop::SubscribeTap(TapCallback callback)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeTap(callback);
	}

	bool GestureRecognizerInterop::SubscribeHold(HoldCallback callback)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeHold(callback);
	}

	bool GestureRecognizerInterop::SubscribeManipulation(ManipulationCallback callback)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeManipulation(callback);
	}

	bool GestureRecognizerInterop::SubscribeNavigation(NavigationCallback callback, unsigned int settings)
	{
		std::lock_guard<std::mutex> lock(gestureRecognizerLock);
		if (gestureRecognizerMap.count(id) == 0 || gestureRecognizerMap[id] == nullptr)
		{
			return false;
		}

		return gestureRecognizerMap[id]->SubscribeNavigation(callback, settings);
	}

	HMDRemotingConnectionState MixedRealityInterop::GetConnectionState()
	{
#if HOLO_STREAMING_RENDERING
		if (m_isHL1Remoting)
		{
			return remotingState;
		}
		else
		{
			std::lock_guard<std::mutex> remoteLock(remoteContextLock);
			if (m_remoteContext == nullptr)
			{
				return remotingState;
			}

			switch (m_remoteContext.ConnectionState())
			{
			case winrt::Microsoft::Holographic::AppRemoting::ConnectionState::Connected:
				return HMDRemotingConnectionState::Connected;
				break;
			case winrt::Microsoft::Holographic::AppRemoting::ConnectionState::Connecting:
				return HMDRemotingConnectionState::Connecting;
				break;
			case winrt::Microsoft::Holographic::AppRemoting::ConnectionState::Disconnected:
				return HMDRemotingConnectionState::Disconnected;
				break;
			}

			return remotingState;
		}
#endif

		return HMDRemotingConnectionState::Unknown;
	}

#if HOLO_STREAMING_RENDERING
	void ReportConnectionStatus(MixedRealityInterop::ConnectionEvent status)
	{
		std::map<uint32_t, MixedRealityInterop::ConnectionCallback > copyOfMap;
		{
			std::lock_guard<std::recursive_mutex> lock(WindowsMixedReality::ConnectionHandlerLock);
			copyOfMap = WindowsMixedReality::ConnectionHandlerMap;
		}

		for (auto p : copyOfMap)
		{
			p.second(status);
		}
	}
#endif

	void MixedRealityInterop::ConnectToRemoteHoloLens(ID3D11Device* device, const wchar_t* ip, int bitrate, bool IsHoloLens1, int port, bool listen)
	{
#if HOLO_STREAMING_RENDERING
		if (IsHoloLens1)
		{
			{ std::wstringstream string; string << L"ConnectToRemoteHoloLens trying to connect to HoloLens1 " << ip; Log(string); }
		}
		else
		{
			{ std::wstringstream string; string << L"ConnectToRemoteHoloLens trying to connect to HoloLens2 " << ip; Log(string); }
		}

		if (m_streamerHelpers != nullptr)
		{
			// We are already connected to the remote device.
			Log(L"ConnectToRemoteHoloLens: Already connected. Doing nothing.");
			return;
		}
		else
		{
			if (bitrate < 1024) { bitrate = 1024; }
			if (bitrate > 99999) { bitrate = 99999; }

			wcsncpy_s(m_ip, ip, std::size(m_ip));

			m_isHL1Remoting = IsHoloLens1;

			// HoloLens 1 has a different remoting stack.
			if (IsHoloLens1)
			{
				supportsHandedness = false;

				// Connecting to the remote device can change the connection state.
				auto exclusiveLock = m_connectionStateLock.LockExclusive();

				m_streamerHelpers = ref new Microsoft::Holographic::HolographicStreamerHelpers();
				m_streamerHelpers->CreateStreamer(device);
				m_streamerHelpers->SetVideoFrameSize(1280, 720);
				m_streamerHelpers->SetMaxBitrate(bitrate);

				RemotingConnectedEvent = ref new Microsoft::Holographic::ConnectedEvent(
					[this]()
					{
						remotingState = HMDRemotingConnectionState::Connected;
						isRemoteHolographicSpace = true;

						winrt::check_hresult(reinterpret_cast<::IUnknown*>(m_streamerHelpers->HolographicSpace)
							->QueryInterface(winrt::guid_of<HolographicSpace>(),
								reinterpret_cast<void**>(winrt::put_abi(holographicSpace))));

						SetInteractionManagerForCurrentView();

						CreateSpatialAnchorHelper(*this);
					});
				ConnectedToken = m_streamerHelpers->OnConnected += RemotingConnectedEvent;

				RemotingDisconnectedEvent = ref new Microsoft::Holographic::DisconnectedEvent(
					[this](_In_ Microsoft::Holographic::HolographicStreamerConnectionFailureReason failureReason)
					{
						ReportConnectionStatus(MixedRealityInterop::ConnectionEvent::DisconnectedFromPeer);

						remotingState = HMDRemotingConnectionState::Disconnected;
					});
				DisconnectedToken = m_streamerHelpers->OnDisconnected += RemotingDisconnectedEvent;

				try
				{
					m_streamerHelpers->Connect(m_ip, 8001);
				}
				catch (Platform::Exception^ ex)
				{
					{ std::wstringstream string; string << L"Connect failed with hr =  " << ex->HResult; Log(string); }
				}

				return;
			}

			// HoloLens 2 Remoting

			// Do not use WMR api's before this call when remoting or you may get access to local machine WMR instead.
			std::lock_guard<std::mutex> remoteLock(remoteContextLock);
			if (m_remoteContext == nullptr)
			{
				//we can reuse the context in case of reconnecting
				m_remoteContext = winrt::Microsoft::Holographic::AppRemoting::RemoteContext::Create(bitrate, true, winrt::Microsoft::Holographic::AppRemoting::PreferredVideoCodec::Any);
				{
					holographicSpace = HolographicSpace::CreateForCoreWindow(nullptr);
					isRemoteHolographicSpace = true;

					// Initialize now so remote holographic space has a valid graphics device before we try to connect.
					Initialize(device);
				}

				m_onConnectedEventRevoker = m_remoteContext.OnConnected(winrt::auto_revoke, [this]()
					{
						Log(L"ConnectToRemoteHoloLens: Connect Succeeded.");

						SetInteractionManagerForCurrentView();

						assert(Locator != nullptr); // bug in Hololens platform cannot initialize the spatial anchor store before the Locator is created.
						CreateSpatialAnchorHelper(*this);

						RequestUserPermissionForEyeTracking();
						ReportConnectionStatus(MixedRealityInterop::ConnectionEvent::Connected);
					});

				m_onListeningEventRevoker = m_remoteContext.OnListening(winrt::auto_revoke, [this](uint16_t port)
					{
						ReportConnectionStatus(MixedRealityInterop::ConnectionEvent::Listening);
					});

				//emptry failure reason as we are trying to connect
				wcsncpy_s(failureString, L"", std::size(failureString));

				m_onDisconnectedEventRevoker =
					m_remoteContext.OnDisconnected(winrt::auto_revoke, [this](winrt::Microsoft::Holographic::AppRemoting::ConnectionFailureReason failureReason)
						{
							const wchar_t ConnectFailureReasonString[static_cast<int32>(winrt::Microsoft::Holographic::AppRemoting::ConnectionFailureReason::PeerDisconnectTimeout) + 1][32] =
							{
								L"None",
								L"Unknown",
								L"NoServerCertificate",
								L"HandshakePortBusy",
								L"HandshakeUnreachable",
								L"HandshakeConnectionFailed",
								L"AuthenticationFailed",
								L"RemotingVersionMismatch",
								L"IncompatibleTransportProtocols",
								L"HandshakeFailed",
								L"TransportPortBusy",
								L"TransportUnreachable",
								L"TransportConnectionFailed",
								L"ProtocolVersionMismatch",
								L"ProtocolError",
								L"VideoCodecNotAvailable",
								L"Canceled",
								L"ConnectionLost",
								L"DeviceLost",
								L"DisconnectRequest",
								L"HandshakeNetworkUnreachable",
								L"HandshakeConnectionRefused",
								L"VideoFormatNotAvailable",
								L"PeerDisconnectRequest",
								L"PeerDisconnectTimeout"
							};

							//copy reason for future retrieval
							wcsncpy_s(failureString, ConnectFailureReasonString[static_cast<int32>(failureReason)], std::size(failureString));

							{ std::wstringstream string; string << L"RemotingDisconnectedEvent: Reason: " << static_cast<int>(failureReason) << " " << ConnectFailureReasonString[static_cast<int32>(failureReason)]; Log(string); }

							ReportConnectionStatus(MixedRealityInterop::ConnectionEvent::DisconnectedFromPeer);

							// Fully disconnect from remote device to ensure we're in a clean state for the next connection.
							DisconnectFromDevice();
						});

				remoteSpeech = m_remoteContext.GetRemoteSpeech();
			}

			if (listen)
			{
				try
				{
					m_remoteContext.Listen(m_ip, port, port + 1);
				}
				catch (winrt::hresult_error const& error)
				{
					{ std::wstringstream string; string << L"ConnectToRemoteHoloLens: Listen Failed " << error.message().c_str(); Log(string); }
					OutputDebugString(L"Listen failed. ");
					OutputDebugString(error.message().c_str());
					OutputDebugString(L"\n");
				}
			}
			else
			{
				try
				{
					m_remoteContext.Connect(m_ip, port);
				}
				catch (winrt::hresult_error const& error)
				{
					{ std::wstringstream string; string << L"ConnectToRemoteHoloLens: Connect Failed " << error.message().c_str(); Log(string); }
					OutputDebugString(L"Connect failed. ");
					OutputDebugString(error.message().c_str());
					OutputDebugString(L"\n");
				}
			}
		}
#endif
	}

#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
	template <typename T>
	T from_cx(Platform::Object^ from)
	{
		T to{ nullptr };

		winrt::check_hresult(reinterpret_cast<::IUnknown*>(from)
			->QueryInterface(winrt::guid_of<T>(),
				reinterpret_cast<void**>(winrt::put_abi(to))));

		return to;
	}

	template <typename T>
	T^ to_cx(winrt::Windows::Foundation::IUnknown const& from)
	{
		return safe_cast<T^>(reinterpret_cast<Platform::Object^>(winrt::get_abi(from)));
	}
#endif

#if PLATFORM_HOLOLENS
	void MixedRealityInterop::SetHolographicSpace(Windows::Graphics::Holographic::HolographicSpace^ inHolographicSpace)
	{
		holographicSpace = from_cx<winrt::Windows::Graphics::Holographic::HolographicSpace>(inHolographicSpace);
	}
#endif

	void MixedRealityInterop::SetInteractionManagerForCurrentView()
	{
#if !PLATFORM_HOLOLENS
		if (IsRemoting())
#endif
		{
			interactionManager = winrt::Windows::UI::Input::Spatial::SpatialInteractionManager::GetForCurrentView();

			{
				std::lock_guard<std::mutex> lock(gestureRecognizerLock);
				for (auto p : gestureRecognizerMap)
				{
					if (p.second)
					{
						p.second->Clean();
					}
				}

				GestureRecognizer::SetInteractionManager(interactionManager);
				for (auto p : gestureRecognizerMap)
				{
					if (p.second)
					{
						p.second->Init(m_isHL1Remoting);
					}
				}
			}
		}
	}

	void MixedRealityInterop::ConnectToLocalWMRHeadset()
	{
		{ std::wstringstream string; string << L"ConnectToLocalWMRHeadset"; Log(string); }

#if HOLO_STREAMING_RENDERING
		if (m_remoteContext != nullptr)
		{
			// We are already connected to the remote device.
			Log(L"ConnectToLocalWMRHeadset: Already connected. Doing nothing.");
			return;
		}
#endif

		wcsncpy_s(m_ip, L"local", std::size(m_ip));

		CreateSpatialAnchorHelper(*this);
	}

	void MixedRealityInterop::ConnectToLocalHoloLens()
	{
		{ std::wstringstream string; string << L"ConnectToLocalHoloLens"; Log(string); }

		CreateSpatialAnchorHelper(*this);
	}

	void MixedRealityInterop::DisconnectFromDevice()
	{
		//make sure to set to empty
		wcsncpy_s(failureString, L"", std::size(failureString));

#if HOLO_STREAMING_RENDERING
		if (m_isHL1Remoting)
		{
			// Disconnecting from the remote device can change the connection state.
			auto exclusiveLock = m_connectionStateLock.LockExclusive();

			if (m_streamerHelpers != nullptr)
			{
				Log(L"DisconnectFromDevice: Disconnecting from wmr device.");

				m_streamerHelpers->OnConnected -= ConnectedToken;
				m_streamerHelpers->OnDisconnected -= DisconnectedToken;

				RemotingConnectedEvent = nullptr;
				RemotingDisconnectedEvent = nullptr;

				m_streamerHelpers->Disconnect();

				// Reset state
				m_streamerHelpers = nullptr;

				DestroySpatialAnchorHelper();

				Dispose(true);
			}

			return;
		}

		if (m_remoteContext != nullptr)
		{
			Log(L"DisconnectFromDevice: Disconnecting from wmr device.");

			m_onConnectedEventRevoker.revoke();
			m_onDisconnectedEventRevoker.revoke();
			m_onRecognizedSpeechRevoker.revoke();
			m_onListeningEventRevoker.revoke();

			{
				std::lock_guard<std::mutex> remoteLock(remoteContextLock);
				m_remoteContext.Close();
				m_remoteContext = nullptr;
			}

			DestroySpatialAnchorHelper();

			ReleaseSpatialRecognizers();

			Dispose(true);
		}
		else if (m_spatialAnchorHelper != nullptr)
		{
#if PLATFORM_HOLOLENS
			Log(L"DisconnectFromDevice: Disconnecting from LocalHoloLens.");
			DestroySpatialAnchorHelper();
			ReleaseSpatialRecognizers();
#else
			Log(L"DisconnectFromDevice: Disconnecting from LocalWMRHeadset.");
			DestroySpatialAnchorHelper();
#endif
		}
		else
		{
			Log(L"DisconnectFromDevice: Already not connected. Doing nothing.");
		}
#endif
	}

	bool MixedRealityInterop::IsRemoting()
	{
#if HOLO_STREAMING_RENDERING
		return isRemoteHolographicSpace && holographicSpace != nullptr;
#endif
		return false;
	}

	bool MixedRealityInterop::IsRemotingConnected()
	{
#if HOLO_STREAMING_RENDERING
		if (m_remoteContext == nullptr)
		{
			return false;
		}
		return m_remoteContext.ConnectionState() == winrt::Microsoft::Holographic::AppRemoting::ConnectionState::Connected;
#endif
		return false;
	}

	uint32_t MixedRealityInterop::SubscribeConnectionEvent(ConnectionCallback callback)
	{
#if HOLO_STREAMING_RENDERING
		std::lock_guard<std::recursive_mutex> lock(ConnectionHandlerLock);
		ConnectionHandlerMap[ConnectionHandlerMaxId] = callback;
		return ConnectionHandlerMaxId++;

#else
		return 0;
#endif
	}

	void MixedRealityInterop::UnsubscribeConnectionEvent(uint32_t id)
	{
#if HOLO_STREAMING_RENDERING
		if (id == 0)
		{
			return;
		}
		std::lock_guard<std::recursive_mutex> lock(ConnectionHandlerLock);
		ConnectionHandlerMap.erase(id);
#endif
	}

	wchar_t* MixedRealityInterop::GetFailureString()
	{
		return failureString;
	}

	void MixedRealityInterop::StartSpatialMapping(
		float InTriangleDensity,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AllocFunctionPointer)(MeshUpdate*),
		void(*RemovedMeshPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	)
	{
		// HL1 does not support spatial mapping.
		if (!m_isHL1Remoting)
		{
			StartMeshObserver(
				InTriangleDensity,
				InVolumeSize,
				StartFunctionPointer,
				AllocFunctionPointer,
				RemovedMeshPointer,
				FinishFunctionPointer
			);
		}
	}

	void MixedRealityInterop::StopSpatialMapping()
	{
		if (!m_isHL1Remoting)
		{
			StopMeshObserver();
		}
	}

	void MixedRealityInterop::StartSceneUnderstanding(
		bool bGeneratePlanes,
		bool bGenerateSceneMeshes,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AddPlaneFunctionPointer)(PlaneUpdate*),
		void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
		void(*AllocMeshFunctionPointer)(MeshUpdate*),
		void(*RemoveMeshFunctionPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	)
	{
		StartSceneUnderstandingObserver(bGeneratePlanes,
			bGenerateSceneMeshes,
			InVolumeSize,
			StartFunctionPointer,
			AddPlaneFunctionPointer,
			RemovePlaneFunctionPointer,
			AllocMeshFunctionPointer,
			RemoveMeshFunctionPointer,
			FinishFunctionPointer);
	}

	void MixedRealityInterop::StopSceneUnderstanding()
	{
		StopSceneUnderstandingObserver();
	}

	void MixedRealityInterop::SetSUCoordinateSystem()
	{
		_SetSUCoordinateSystem();
	}
	void MixedRealityInterop::StartQRCodeTracking(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*))
	{
		StartQRCodeObserver(AddedFunctionPointer, UpdatedFunctionPointer, RemovedFunctionPointer);
	}

	void MixedRealityInterop::StopQRCodeTracking()
	{
		StopQRCodeObserver();
	}

	bool MixedRealityInterop::IsThirdCameraActive()
	{
		return isThirdCameraActive;
	}

	bool MixedRealityInterop::GetThirdCameraPoseRenderThread(DirectX::XMMATRIX& thirdCameraViewLeft, DirectX::XMMATRIX& thirdCameraViewRight)
	{
		std::lock_guard<std::mutex> lock(poseLock);

		if (!IsInitialized()
			|| PVCameraResources == nullptr)
		{
			return false;
		}

		thirdCameraViewLeft = PVCameraResources->leftPose;
		thirdCameraViewRight = PVCameraResources->rightPose;
		return true;
	}

	bool MixedRealityInterop::SetEnabledMixedRealityCamera(bool enabled)
	{
		if (!IsInitialized() || IsRemoting() || HolographicDisplay::GetDefault() == nullptr)
		{
			//remoting may has no default display in some time
			isThirdCameraActive = false;
			return false;
		}

		auto view = HolographicDisplay::GetDefault().TryGetViewConfiguration(HolographicViewConfigurationKind::PhotoVideoCamera);
		if (!view)
		{
			isThirdCameraActive = false;
			return false;
		}

		view.IsEnabled(enabled);
		return true;
	}

	bool MixedRealityInterop::ResizeMixedRealityCamera(SIZE& sz)
	{
		if (!IsInitialized() || IsRemoting() || HolographicDisplay::GetDefault() == nullptr)
		{
			return false;
		}

		auto view = HolographicDisplay::GetDefault().TryGetViewConfiguration(HolographicViewConfigurationKind::PhotoVideoCamera);
		if (!view)
		{
			return false;
		}

		winrt::Windows::Foundation::Size newSize;
		newSize.Width = (float)sz.cx;
		newSize.Height = (float)sz.cy;
		newSize = view.RequestRenderTargetSize(newSize);
		sz.cx = (LONG)newSize.Width;
		sz.cy = (LONG)newSize.Height;

		return true;
	}

	void MixedRealityInterop::GetThirdCameraDimensions(int& width, int& height)
	{
		width = 0;
		height = 0;
		if (PVCameraResources == nullptr)
		{
			return;
		}

		auto sz = PVCameraResources->GetRenderTargetSize();

		width = (int)sz.Width;
		height = (int)sz.Height;
	}
}

/** Include this down here so there's no confusion between WinRT/C++ and C++/CX types */

#include "MeshObserver.h"

namespace WindowsMixedReality
{
	void StartMeshObserver(
		float InTriangleDensity,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AllocFunctionPointer)(MeshUpdate*),
		void(*RemovedMeshPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	)
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		if (m_isHL1Remoting)
		{
			return;
		}

		MeshUpdateObserver& Instance = MeshUpdateObserver::Get();
		// Pass any logging callback on
		Instance.SetOnLog(m_logCallback);

		Instance.StartMeshObserver(
			InTriangleDensity,
			InVolumeSize,
			StartFunctionPointer,
			AllocFunctionPointer,
			RemovedMeshPointer,
			FinishFunctionPointer
		);

		HandMeshUpdateObserver::InitStatic(StartFunctionPointer, AllocFunctionPointer, FinishFunctionPointer);
#endif
	}

	void UpdateMeshObserverBoundingVolume(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem InCoordinateSystem, winrt::Windows::Foundation::Numerics::float3 InPosition)
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		MeshUpdateObserver& Instance = MeshUpdateObserver::Get();
		Instance.UpdateBoundingVolume(InCoordinateSystem, InPosition);
#endif
	}

	void StopMeshObserver()
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		if (m_isHL1Remoting)
		{
			return;
		}

		MeshUpdateObserver::Release();
		HandMeshUpdateObserver::InitStatic(nullptr, nullptr, nullptr);
		MeshUpdateObserverMap.clear();

#endif
	}
}

/** Include this down here so there's no confusion between WinRT/C++ and C++/CX types */
#include "SceneUnderstandingObserver.h"

namespace WindowsMixedReality
{
	void StartSceneUnderstandingObserver(
		bool bGeneratePlanes,
		bool bGenerateSceneMeshes,
		float InVolumeSize,
		void(*StartFunctionPointer)(),
		void(*AddPlaneFunctionPointer)(PlaneUpdate*),
		void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
		void(*AllocMeshFunctionPointer)(MeshUpdate*),
		void(*RemoveMeshFunctionPointer)(MeshUpdate*),
		void(*FinishFunctionPointer)()
	)
	{
#if PLATFORM_HOLOLENS
		SceneUnderstandingObserver& Instance = SceneUnderstandingObserver::Get();
		// Pass any logging callback on
		Instance.SetOnLog(m_logCallback);

		Instance.StartSceneUnderstandingObserver(
			bGeneratePlanes,
			bGenerateSceneMeshes,
			InVolumeSize,
			StartFunctionPointer,
			AddPlaneFunctionPointer,
			RemovePlaneFunctionPointer,
			AllocMeshFunctionPointer,
			RemoveMeshFunctionPointer,
			FinishFunctionPointer
		);
#endif
	}
	void StopSceneUnderstandingObserver()
	{
#if PLATFORM_HOLOLENS
		SceneUnderstandingObserver& Instance = SceneUnderstandingObserver::Get();
		Instance.StopSceneUnderstandingObserver();
#endif
	}
	void _SetSUCoordinateSystem()
	{
#if PLATFORM_HOLOLENS
		SceneUnderstandingObserver& Instance = SceneUnderstandingObserver::Get();

		winrt::Windows::Perception::Spatial::SpatialCoordinateSystem cs = GetReferenceCoordinateSystem();
		Instance.SetTrackingCoordinateSystem(cs);
#endif
	}
}

#include "QRCodeObserver.h"

namespace WindowsMixedReality
{
	void StartQRCodeObserver(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*))
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		QRCodeUpdateObserver& Instance = QRCodeUpdateObserver::Get();
		// Pass any logging callback on
		Instance.SetOnLog(m_logCallback);
		Instance.StartQRCodeObserver(AddedFunctionPointer, UpdatedFunctionPointer, RemovedFunctionPointer);
#endif
	}

	void UpdateQRCodeObserverCoordinateSystem(winrt::Windows::Perception::Spatial::SpatialCoordinateSystem InCoordinateSystem)
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		QRCodeUpdateObserver& Instance = QRCodeUpdateObserver::Get();
		Instance.UpdateCoordinateSystem(InCoordinateSystem);
#endif
	}

	void StopQRCodeObserver()
	{
#if PLATFORM_HOLOLENS || HOLO_STREAMING_RENDERING
		QRCodeUpdateObserver& Instance = QRCodeUpdateObserver::Get();
		Instance.Release();
#endif
	}
}