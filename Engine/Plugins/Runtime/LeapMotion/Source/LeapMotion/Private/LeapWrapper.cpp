// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LeapWrapper.h"
#include "LeapUtility.h"
#include "LeapAsync.h"


#pragma region LeapC Wrapper

//Static callback delegate pointer initialization
LeapWrapperCallbackInterface* FLeapWrapper::CallbackDelegate = nullptr;

FLeapWrapper::FLeapWrapper():
	bIsRunning(false),
	bIsConnected(false)
{
	CallbackDelegate = nullptr;
	InterpolatedFrame = nullptr;
	InterpolatedFrameSize = 0;
}

FLeapWrapper::~FLeapWrapper()
{
	bIsRunning = false;
	CallbackDelegate = nullptr;
	LastFrame = nullptr;
	ConnectionHandle = nullptr;

	if (bIsConnected) 
	{
		CloseConnection();
	}
	if (ImageDescription != NULL)
	{
		if(ImageDescription->pBuffer != NULL)
		{
			free(ImageDescription->pBuffer);
		}
		delete ImageDescription;
	}
}

void FLeapWrapper::SetCallbackDelegate(const LeapWrapperCallbackInterface* InCallbackDelegate)
{
	CallbackDelegate = (LeapWrapperCallbackInterface*)InCallbackDelegate;
}

LEAP_CONNECTION* FLeapWrapper::OpenConnection(const LeapWrapperCallbackInterface* InCallbackDelegate)
{
	SetCallbackDelegate(InCallbackDelegate);

	eLeapRS result = LeapCreateConnection(NULL, &ConnectionHandle);
	if (result == eLeapRS_Success) {
		result = LeapOpenConnection(ConnectionHandle);
		if (result == eLeapRS_Success) {
			bIsRunning = true;

			LEAP_CONNECTION* Handle = &ConnectionHandle;
			ProducerLambdaFuture = FLeapAsync::RunLambdaOnBackGroundThread([&, Handle]
			{
				UE_LOG(LeapMotionLog, Log, TEXT("ServiceMessageLoop started."));
				ServiceMessageLoop();
				UE_LOG(LeapMotionLog, Log, TEXT("ServiceMessageLoop stopped."));

				CloseConnectionHandle(Handle);
			});
		}
	}
	return &ConnectionHandle;
}

void FLeapWrapper::CloseConnection() 
{
	if (!bIsConnected)
	{
		//Not connected, already done
		UE_LOG(LeapMotionLog, Log, TEXT("Attempt at closing an already closed connection."));
		return;
	}
	bIsConnected = false;
	bIsRunning = false;
	CleanupLastDevice();

	//Wait for thread to exit - Blocking call, but it should be very quick.
	ProducerLambdaFuture.Wait();
	
	//Nullify the callback delegate. Any outstanding task graphs will not run if the delegate is nullified.
	CallbackDelegate = nullptr;

	UE_LOG(LeapMotionLog, Log, TEXT("Connection successfully closed."));
	//CloseConnectionHandle(&connectionHandle);
}

void FLeapWrapper::SetPolicy(int64 Flags, int64 ClearFlags)
{
	LeapSetPolicyFlags(ConnectionHandle, Flags, ClearFlags);
}

void FLeapWrapper::SetPolicyFlagFromBoolean(eLeapPolicyFlag Flag, bool ShouldSet)
{
	if (ShouldSet)
	{
		SetPolicy(Flag, 0);
	}
	else
	{
		SetPolicy(0, Flag);
	}
}

/** Close the connection and let message thread function end. */
void FLeapWrapper::CloseConnectionHandle(LEAP_CONNECTION* InConnectionHandle) 
{
	bIsRunning = false;
	bIsConnected = false;
	LeapDestroyConnection(*InConnectionHandle);
}

LEAP_TRACKING_EVENT* FLeapWrapper::GetFrame()
{
	LEAP_TRACKING_EVENT *currentFrame;
	DataLock.Lock();
	currentFrame = LastFrame;
	DataLock.Unlock();
	return currentFrame;
}

LEAP_TRACKING_EVENT* FLeapWrapper::GetInterpolatedFrameAtTime(int64 TimeStamp)
{
	uint64_t FrameSize = 0;
	LeapGetFrameSize(ConnectionHandle, TimeStamp, &FrameSize);

	//Check validity of frame size
	if (FrameSize > 0 )
	{
		//Different frame? 
		if (FrameSize != InterpolatedFrameSize)
		{
			//If we already have an allocated frame, free it
			if (InterpolatedFrame)
			{
				free(InterpolatedFrame);
			}
			InterpolatedFrame = (LEAP_TRACKING_EVENT *)malloc(FrameSize);
		}
		InterpolatedFrameSize = FrameSize;

		//Grab the new frame
		LeapInterpolateFrame(ConnectionHandle, TimeStamp, InterpolatedFrame, InterpolatedFrameSize);
	}

	return InterpolatedFrame;
}

LEAP_DEVICE_INFO* FLeapWrapper::GetDeviceProperties()
{
	LEAP_DEVICE_INFO *currentDevice;
	DataLock.Lock();
	currentDevice = LastDevice;
	DataLock.Unlock();
	return currentDevice;
}

const char* FLeapWrapper::ResultString(eLeapRS Result)
{
	switch (Result) {
	case eLeapRS_Success:                  return "eLeapRS_Success";
	case eLeapRS_UnknownError:             return "eLeapRS_UnknownError";
	case eLeapRS_InvalidArgument:          return "eLeapRS_InvalidArgument";
	case eLeapRS_InsufficientResources:    return "eLeapRS_InsufficientResources";
	case eLeapRS_InsufficientBuffer:       return "eLeapRS_InsufficientBuffer";
	case eLeapRS_Timeout:                  return "eLeapRS_Timeout";
	case eLeapRS_NotConnected:             return "eLeapRS_NotConnected";
	case eLeapRS_HandshakeIncomplete:      return "eLeapRS_HandshakeIncomplete";
	case eLeapRS_BufferSizeOverflow:       return "eLeapRS_BufferSizeOverflow";
	case eLeapRS_ProtocolError:            return "eLeapRS_ProtocolError";
	case eLeapRS_InvalidClientID:          return "eLeapRS_InvalidClientID";
	case eLeapRS_UnexpectedClosed:         return "eLeapRS_UnexpectedClosed";
	case eLeapRS_UnknownImageFrameRequest: return "eLeapRS_UnknownImageFrameRequest";
	case eLeapRS_UnknownTrackingFrameID:   return "eLeapRS_UnknownTrackingFrameID";
	case eLeapRS_RoutineIsNotSeer:         return "eLeapRS_RoutineIsNotSeer";
	case eLeapRS_TimestampTooEarly:        return "eLeapRS_TimestampTooEarly";
	case eLeapRS_ConcurrentPoll:           return "eLeapRS_ConcurrentPoll";
	case eLeapRS_NotAvailable:             return "eLeapRS_NotAvailable";
	case eLeapRS_NotStreaming:             return "eLeapRS_NotStreaming";
	case eLeapRS_CannotOpenDevice:         return "eLeapRS_CannotOpenDevice";
	default:                               return "unknown result type.";
	}
}

void FLeapWrapper::EnableImageStream(bool bEnable)
{
	//TODO: test the image/buffer stream code

	if (ImageDescription == NULL)
	{
		ImageDescription = new LEAP_IMAGE_FRAME_DESCRIPTION;
		ImageDescription->pBuffer = NULL;
	}

	int OldLength = ImageDescription->buffer_len;

	//if the size is different realloc the buffer
	if (ImageDescription->buffer_len != OldLength)
	{
		if (ImageDescription->pBuffer != NULL)
		{
			free(ImageDescription->pBuffer);
		}
		ImageDescription->pBuffer = (void*)malloc(ImageDescription->buffer_len);
	}
}


void FLeapWrapper::Millisleep(int milliseconds)
{
	FPlatformProcess::Sleep(((float)milliseconds) / 1000.f);
}

void FLeapWrapper::SetDevice(const LEAP_DEVICE_INFO *DeviceProps)
{
	DataLock.Lock();
	if (LastDevice)
	{
		free(LastDevice->serial);
	}
	else 
	{
		LastDevice = (LEAP_DEVICE_INFO*) malloc(sizeof(*DeviceProps));
	}
	if (LastDevice)
	{
		*LastDevice = *DeviceProps;
		LastDevice->serial = (char*)malloc(DeviceProps->serial_length);
		if (LastDevice->serial)
		{
			memcpy(LastDevice->serial, DeviceProps->serial, DeviceProps->serial_length);
		}
	}
	DataLock.Unlock();
}

void FLeapWrapper::CleanupLastDevice()
{
	if (LastDevice)
	{
		free(LastDevice->serial);
	}
	LastDevice = nullptr;
}

void FLeapWrapper::SetFrame(const LEAP_TRACKING_EVENT *Frame)
{
	DataLock.Lock();
	if (!LastFrame)
	{
		LastFrame = (LEAP_TRACKING_EVENT *)malloc(sizeof(*Frame));
	}
	if (LastFrame)
	{
		*LastFrame = *Frame;
	}
	DataLock.Unlock();
}


/** Called by ServiceMessageLoop() when a connection event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleConnectionEvent(const LEAP_CONNECTION_EVENT *ConnectionEvent)
{
	bIsConnected = true;
	if (CallbackDelegate) 
	{
		CallbackDelegate->OnConnect();
	}
}

/** Called by ServiceMessageLoop() when a connection lost event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleConnectionLostEvent(const LEAP_CONNECTION_LOST_EVENT *ConnectionLostEvent)
{
	bIsConnected = false;
	CleanupLastDevice();

	if (CallbackDelegate) 
	{
		CallbackDelegate->OnConnectionLost();
	}
}

/**
* Called by ServiceMessageLoop() when a device event is returned by LeapPollConnection()
*/
void FLeapWrapper::HandleDeviceEvent(const LEAP_DEVICE_EVENT *DeviceEvent)
{
	LEAP_DEVICE DeviceHandle;
	//Open device using LEAP_DEVICE_REF from event struct.
	eLeapRS Result = LeapOpenDevice(DeviceEvent->device, &DeviceHandle);
	if (Result != eLeapRS_Success)
	{
		UE_LOG(LeapMotionLog, Log, TEXT("Could not open device %s.\n"), ResultString(Result));
		return;
	}

	//Create a struct to hold the device properties, we have to provide a buffer for the serial string
	LEAP_DEVICE_INFO DeviceProperties = { sizeof(DeviceProperties) };
	// Start with a length of 1 (pretending we don't know a priori what the length is).
	// Currently device serial numbers are all the same length, but that could change in the future
	DeviceProperties.serial_length = 1;
	DeviceProperties.serial = (char*)malloc(DeviceProperties.serial_length);
	//This will fail since the serial buffer is only 1 character long
	// But deviceProperties is updated to contain the required buffer length
	Result = LeapGetDeviceInfo(DeviceHandle, &DeviceProperties);
	if (Result == eLeapRS_InsufficientBuffer) 
	{
		//try again with correct buffer size
		free(DeviceProperties.serial);
		DeviceProperties.serial = (char*)malloc(DeviceProperties.serial_length);
		Result = LeapGetDeviceInfo(DeviceHandle, &DeviceProperties);
		if (Result != eLeapRS_Success) {
			printf("Failed to get device info %s.\n", ResultString(Result));
			free(DeviceProperties.serial);
			return;
		}
	}
	SetDevice(&DeviceProperties);

	if (CallbackDelegate) 
	{
		TaskRefDeviceFound = FLeapAsync::RunShortLambdaOnGameThread([DeviceEvent, DeviceProperties, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnDeviceFound(&DeviceProperties);
			}
		});
	}

	free(DeviceProperties.serial);
	LeapCloseDevice(DeviceHandle);
}

/** Called by ServiceMessageLoop() when a device lost event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleDeviceLostEvent(const LEAP_DEVICE_EVENT *DeviceEvent) {
	if (CallbackDelegate)
	{
		TaskRefDeviceLost = FLeapAsync::RunShortLambdaOnGameThread([DeviceEvent, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnDeviceLost(LastDevice->serial);
			}
		});
	}
}

/** Called by ServiceMessageLoop() when a device failure event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleDeviceFailureEvent(const LEAP_DEVICE_FAILURE_EVENT *DeviceFailureEvent) 
{
	if (CallbackDelegate) 
	{
		TaskRefDeviceFailure = FLeapAsync::RunShortLambdaOnGameThread([DeviceFailureEvent, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnDeviceFailure(DeviceFailureEvent->status, DeviceFailureEvent->hDevice);
			}
		});
	}
}

/** Called by ServiceMessageLoop() when a tracking event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleTrackingEvent(const LEAP_TRACKING_EVENT *TrackingEvent) {
	SetFrame(TrackingEvent); //support polling tracking data from different thread

	//Callback delegate is checked twice since the second call happens on the second thread and may be invalidated!
	if (CallbackDelegate) 
	{
		LeapWrapperCallbackInterface* SafeDelegate = CallbackDelegate;

		//Run this on bg thread still
		CallbackDelegate->OnFrame(TrackingEvent);
	}
}

void FLeapWrapper::HandleImageEvent(const LEAP_IMAGE_EVENT *ImageEvent)
{	
	//Callback with data
	if (CallbackDelegate)
	{
		//Do image handling on background thread for performance
		CallbackDelegate->OnImage(ImageEvent);
	}
}

/** Called by ServiceMessageLoop() when a log event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleLogEvent(const LEAP_LOG_EVENT *LogEvent) 
{
	if (CallbackDelegate)
	{
		TaskRefLog = FLeapAsync::RunShortLambdaOnGameThread([LogEvent, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnLog(LogEvent->severity, LogEvent->timestamp, LogEvent->message);
			}
		});
	}
}

/** Called by ServiceMessageLoop() when a policy event is returned by LeapPollConnection(). */
void FLeapWrapper::HandlePolicyEvent(const LEAP_POLICY_EVENT *PolicyEvent)
{
	if (CallbackDelegate)
	{
		TaskRefPolicy = FLeapAsync::RunShortLambdaOnGameThread([PolicyEvent, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnPolicy(PolicyEvent->current_policy);
			}
		});
	}
}

/** Called by ServiceMessageLoop() when a config change event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleConfigChangeEvent(const LEAP_CONFIG_CHANGE_EVENT *ConfigChangeEvent)
{
	if (CallbackDelegate)
	{
		TaskRefConfigChange = FLeapAsync::RunShortLambdaOnGameThread([ConfigChangeEvent, this]
		{
			if (CallbackDelegate)
			{
				CallbackDelegate->OnConfigChange(ConfigChangeEvent->requestID, ConfigChangeEvent->status);
			}
		});
	}
}

/** Called by ServiceMessageLoop() when a config response event is returned by LeapPollConnection(). */
void FLeapWrapper::HandleConfigResponseEvent(const LEAP_CONFIG_RESPONSE_EVENT *ConfigResponseEvent)
{
	if (CallbackDelegate)
	{
		TaskRefConfigResponse = FLeapAsync::RunShortLambdaOnGameThread([ConfigResponseEvent, this]
		{
			if (CallbackDelegate) 
			{
				CallbackDelegate->OnConfigResponse(ConfigResponseEvent->requestID, ConfigResponseEvent->value);
			}
		});
	}
}

/**
* Services the LeapC message pump by calling LeapPollConnection().
* The average polling time is determined by the framerate of the Leap Motion service.
*/
void FLeapWrapper::ServiceMessageLoop(void * Unused)
{
	eLeapRS Result;
	LEAP_CONNECTION_MESSAGE Msg;
	LEAP_CONNECTION Handle = ConnectionHandle; //copy handle so it doesn't get released from under us on game thread

	unsigned int Timeout = 1000;
	while (bIsRunning)
	{
		Result = LeapPollConnection(Handle, Timeout, &Msg);

		//Polling may have taken some time, re-check exit condition
		if (!bIsRunning)
		{
			break;
		}

		if (Result != eLeapRS_Success)
		{
			//UE_LOG(LeapMotionLog, Log, TEXT("LeapC PollConnection unsuccessful result %s.\n"), UTF8_TO_TCHAR(ResultString(result)));
			if (!bIsConnected)
			{
				FPlatformProcess::Sleep(5.f);
				continue;
			}
			else
			{
				continue;
			}
		}

		switch (Msg.type)
		{
			case eLeapEventType_Connection:
				HandleConnectionEvent(Msg.connection_event);
				break;
			case eLeapEventType_ConnectionLost:
				HandleConnectionLostEvent(Msg.connection_lost_event);
				break;
			case eLeapEventType_Device:
				HandleDeviceEvent(Msg.device_event);
				break;
			case eLeapEventType_DeviceLost:
				HandleDeviceLostEvent(Msg.device_event);
				break;
			case eLeapEventType_DeviceFailure:
				HandleDeviceFailureEvent(Msg.device_failure_event);
				break;
			case eLeapEventType_Tracking:
				HandleTrackingEvent(Msg.tracking_event);
				break;
			case eLeapEventType_Image:
				HandleImageEvent(Msg.image_event);
				break;
			case eLeapEventType_LogEvent:
				HandleLogEvent(Msg.log_event);
				break;
			case eLeapEventType_Policy:
				HandlePolicyEvent(Msg.policy_event);
				break;
			case eLeapEventType_ConfigChange:
				HandleConfigChangeEvent(Msg.config_change_event);
				break;
			case eLeapEventType_ConfigResponse:
				HandleConfigResponseEvent(Msg.config_response_event);
				break;
			default:
				//discard unknown message types
				//UE_LOG(LeapMotionLog, Log, TEXT("Unhandled message type %i."), (int32)msg.type);
				break;
		} //switch on msg.type
	}//end while running
}

#pragma endregion LeapC Wrapper