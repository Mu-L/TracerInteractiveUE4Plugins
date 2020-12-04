// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AudioMixerLog.h"
#include "AudioMixerTypes.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/ScopeLock.h"
#include "Misc/SingleThreadRunnable.h"
#include "AudioMixerNullDevice.h"
#include "DSP/ParamInterpolator.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/Dsp.h"
#include "Modules/ModuleInterface.h"

// defines used for AudioMixer.h
#define AUDIO_PLATFORM_ERROR(INFO)			(OnAudioMixerPlatformError(INFO, FString(__FILE__), __LINE__))

#ifndef AUDIO_MIXER_ENABLE_DEBUG_MODE
// This define enables a bunch of more expensive debug checks and logging capabilities that are intended to be off most of the time even in debug builds of game/editor.
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#define AUDIO_MIXER_ENABLE_DEBUG_MODE 0
#else
#define AUDIO_MIXER_ENABLE_DEBUG_MODE 1
#endif
#endif


// Enable debug checking for audio mixer

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
#define AUDIO_MIXER_CHECK(expr) ensure(expr)
#define AUDIO_MIXER_CHECK_GAME_THREAD(_MixerDevice)			(_MixerDevice->CheckAudioThread())
#define AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(_MixerDevice)	(_MixerDevice->CheckAudioRenderingThread())
#else
#define AUDIO_MIXER_CHECK(expr)
#define AUDIO_MIXER_CHECK_GAME_THREAD(_MixerDevice)
#define AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(_MixerDevice)
#endif

#define AUDIO_MIXER_MAX_OUTPUT_CHANNELS				8			// Max number of speakers/channels supported (7.1)

#define AUDIO_MIXER_DEFAULT_DEVICE_INDEX			INDEX_NONE

namespace EAudioMixerChannel
{
	/** Enumeration values represent sound file or speaker channel types. */
	enum Type
	{
		FrontLeft,
		FrontRight,
		FrontCenter,
		LowFrequency,
		BackLeft,
		BackRight,
		FrontLeftOfCenter,
		FrontRightOfCenter,
		BackCenter,
		SideLeft,
		SideRight,
		TopCenter,
		TopFrontLeft,
		TopFrontCenter,
		TopFrontRight,
		TopBackLeft,
		TopBackCenter,
		TopBackRight,
		Unknown,
		ChannelTypeCount,
		DefaultChannel = FrontLeft
	};

	static const int32 MaxSupportedChannel = EAudioMixerChannel::TopCenter;

	inline const TCHAR* ToString(EAudioMixerChannel::Type InType)
	{
		switch (InType)
		{
		case FrontLeft:				return TEXT("FrontLeft");
		case FrontRight:			return TEXT("FrontRight");
		case FrontCenter:			return TEXT("FrontCenter");
		case LowFrequency:			return TEXT("LowFrequency");
		case BackLeft:				return TEXT("BackLeft");
		case BackRight:				return TEXT("BackRight");
		case FrontLeftOfCenter:		return TEXT("FrontLeftOfCenter");
		case FrontRightOfCenter:	return TEXT("FrontRightOfCenter");
		case BackCenter:			return TEXT("BackCenter");
		case SideLeft:				return TEXT("SideLeft");
		case SideRight:				return TEXT("SideRight");
		case TopCenter:				return TEXT("TopCenter");
		case TopFrontLeft:			return TEXT("TopFrontLeft");
		case TopFrontCenter:		return TEXT("TopFrontCenter");
		case TopFrontRight:			return TEXT("TopFrontRight");
		case TopBackLeft:			return TEXT("TopBackLeft");
		case TopBackCenter:			return TEXT("TopBackCenter");
		case TopBackRight:			return TEXT("TopBackRight");
		case Unknown:				return TEXT("Unknown");

		default:
			return TEXT("UNSUPPORTED");
		}
	}
}

class USoundWave;
class ICompressedAudioInfo;

namespace Audio
{

   	/** Structure to hold platform device information **/
	struct FAudioPlatformDeviceInfo
	{
		/** The name of the audio device */
		FString Name;

		/** ID of the device. */
		FString DeviceId;

		/** The number of channels supported by the audio device */
		int32 NumChannels;

		/** The sample rate of the audio device */
		int32 SampleRate;

		/** The data format of the audio stream */
		EAudioMixerStreamDataFormat::Type Format;

		/** The output channel array of the audio device */
		TArray<EAudioMixerChannel::Type> OutputChannelArray;

		/** Whether or not this device is the system default */
		uint8 bIsSystemDefault : 1;

		FAudioPlatformDeviceInfo()
		{
			Reset();
		}

		void Reset()
		{
			Name = TEXT("Unknown");
			DeviceId = TEXT("Unknown");
			NumChannels = 0;
			SampleRate = 0;
			Format = EAudioMixerStreamDataFormat::Unknown;
			OutputChannelArray.Reset();
			bIsSystemDefault = false;
		}

	};

	/** Platform independent audio mixer interface. */
	class IAudioMixer
	{
	public:
		/** Callback to generate a new audio stream buffer. */
		virtual bool OnProcessAudioStream(AlignedFloatBuffer& OutputBuffer) = 0;

		/** Called when audio render thread stream is shutting down. Last function called. Allows cleanup on render thread. */
		virtual void OnAudioStreamShutdown() = 0;

		bool IsMainAudioMixer() const { return bIsMainAudioMixer; }

		/** Called by FWindowsMMNotificationClient to bypass notifications for audio device changes: */
		AUDIOMIXERCORE_API static bool ShouldIgnoreDeviceSwaps();

		/** Called by FWindowsMMNotificationClient to toggle logging for audio device changes: */
		AUDIOMIXERCORE_API static bool ShouldLogDeviceSwaps();

	protected:

		IAudioMixer() 
		: bIsMainAudioMixer(false) 
		{}

		bool bIsMainAudioMixer;
	};


	/** Defines parameters needed for opening a new audio stream to device. */
	struct FAudioMixerOpenStreamParams
	{
		/** The audio device index to open. */
		uint32 OutputDeviceIndex;

		/** The number of desired audio frames in audio callback. */
		uint32 NumFrames;
		
		/** The number of queued buffers to use for the strea. */
		int32 NumBuffers;

		/** Owning platform independent audio mixer ptr.*/
		IAudioMixer* AudioMixer;
		
		/** The desired sample rate */
		uint32 SampleRate;

		/** Whether or not to try and restore audio to this stream if the audio device is removed (and the device becomes available again). */
		bool bRestoreIfRemoved;

		/* The maximum number of sources we will try to decode or playback at once. */
		int32 MaxSources;

		FAudioMixerOpenStreamParams()
			: OutputDeviceIndex(INDEX_NONE)
			, NumFrames(1024)
			, NumBuffers(1)
			, AudioMixer(nullptr)
			, SampleRate(44100)
			, bRestoreIfRemoved(false)
			, MaxSources(0)
		{}
	};

	struct FAudioOutputStreamInfo
	{
		/** The index of the output device for the audio stream. */
		uint32 OutputDeviceIndex;

		FAudioPlatformDeviceInfo DeviceInfo;

		/** The state of the output audio stream. */
		EAudioOutputStreamState::Type StreamState;

		/** The callback to use for platform-independent layer. */
		IAudioMixer* AudioMixer;

		/** The number of queued buffers to use. */
		uint32 NumBuffers;

		/** Number of output frames */
		int32 NumOutputFrames;

		FAudioOutputStreamInfo()
		{
			Reset();
		}

		~FAudioOutputStreamInfo()
		{

		}

		void Reset()
		{
			OutputDeviceIndex = 0;
			DeviceInfo.Reset();
			StreamState = EAudioOutputStreamState::Closed;
			AudioMixer = nullptr;
			NumBuffers = 2;
			NumOutputFrames = 0;
		}
	};

	enum class EAudioDeviceRole
	{
		Console,
		Multimedia,
		Communications,
	};

	enum class EAudioDeviceState
	{
		Active,
		Disabled,
		NotPresent,
		Unplugged,
	};

	/** Struct used to store render time analysis data. */
	struct AUDIOMIXERCORE_API FAudioRenderTimeAnalysis
	{
		double AvgRenderTime;
		double MaxRenderTime;
		double TotalRenderTime;
		double RenderTimeSinceLastLog;
		uint32 StartTime;
		double MaxSinceTick;
		uint64 RenderTimeCount;
		int32 RenderInstanceId;

		FAudioRenderTimeAnalysis();
		void Start();
		void End();
	};

	/** Class which wraps an output float buffer and handles conversion to device stream formats. */
	class AUDIOMIXERCORE_API FOutputBuffer
	{
	public:
		FOutputBuffer()
			: AudioMixer(nullptr)
			, DataFormat(EAudioMixerStreamDataFormat::Unknown)
		{}

		~FOutputBuffer() = default;
 
		/** Initialize the buffer with the given samples and output format. */
		void Init(IAudioMixer* InAudioMixer, const int32 InNumSamples, const int32 InNumBuffers, const EAudioMixerStreamDataFormat::Type InDataFormat);

		/** Gets the next mixed buffer from the audio mixer. Returns false if our buffer is already full. */
		bool MixNextBuffer();

		/** Gets the buffer data ptrs. Returns a TArrayView for the full buffer size requested, but in the case of an underrun, OutBytesPopped will be less that the size of the returned TArrayView. */
		TArrayView<const uint8> PopBufferData(int32& OutBytesPopped) const;

		/** Gets the number of frames of the buffer. */
		int32 GetNumSamples() const;

		/** Returns the format of the buffer. */
		EAudioMixerStreamDataFormat::Type GetFormat() const { return DataFormat; }


	private:
		IAudioMixer* AudioMixer;

		// Circular buffer used to buffer audio between the audio render thread and the platform interface thread.
		mutable Audio::TCircularAudioBuffer<uint8> CircularBuffer;
		
		// Buffer that we render audio to from the IAudioMixer instance associated with this output buffer.
		Audio::AlignedFloatBuffer RenderBuffer;

		// Buffer read by the platform interface thread.
		mutable Audio::AlignedByteBuffer PopBuffer;

		// For non-float situations, this buffer is used to convert RenderBuffer before pushing it to CircularBuffer.
		AlignedByteBuffer FormattedBuffer;
 		EAudioMixerStreamDataFormat::Type DataFormat;

		static size_t GetSizeForDataFormat(EAudioMixerStreamDataFormat::Type InDataFormat);
		int32 CallCounterMixNextBuffer{ 0 };
	};

	/** Abstract interface for receiving audio device changed notifications */
	class AUDIOMIXERCORE_API IAudioMixerDeviceChangedLister
	{
	public:
		virtual void RegisterDeviceChangedListener() {}
		virtual void UnregisterDeviceChangedListener() {}
		virtual void OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
		virtual void OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
		virtual void OnDeviceAdded(const FString& DeviceId) {}
		virtual void OnDeviceRemoved(const FString& DeviceId) {}
		virtual void OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState) {}
		virtual FString GetDeviceId() const { return FString(); }
	};


	/** Abstract interface for mixer platform. */
	class AUDIOMIXERCORE_API IAudioMixerPlatformInterface : public FRunnable,
														public FSingleThreadRunnable,
														public IAudioMixerDeviceChangedLister
	{

	public: // Virtual functions
		
		/** Virtual destructor. */
		virtual ~IAudioMixerPlatformInterface();

		/** Returns the platform API enumeration. */
		virtual EAudioMixerPlatformApi::Type GetPlatformApi() const = 0;

		/** Initialize the hardware. */
		virtual bool InitializeHardware() = 0;

		/** Check if audio device changed if applicable. Return true if audio device changed. */
		virtual bool CheckAudioDeviceChange() { return false; };

		/** Resumes playback on new audio device after device change. */
		virtual void ResumePlaybackOnNewDevice() {}

		/** Teardown the hardware. */
		virtual bool TeardownHardware() = 0;
		
		/** Is the hardware initialized. */
		virtual bool IsInitialized() const = 0;

		/** Returns the number of output devices. */
		virtual bool GetNumOutputDevices(uint32& OutNumOutputDevices) { OutNumOutputDevices = 1; return true; }

		/** Gets the device information of the given device index. */
		virtual bool GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo) = 0;

		/**
		 * Returns the name of the currently used audio device.
		 */
		virtual FString GetCurrentDeviceName() { return CurrentDeviceName; }

		/**
		 * Can be used to look up the current index for a given device name.
		 * On most platforms, this index may be invalidated if any devices are added or removed.
		 * Returns INDEX_NONE if no mapping is found
		 */
		virtual int32 GetIndexForDevice(const FString& InDeviceName);

		/** Gets the platform specific audio settings. */
		virtual FAudioPlatformSettings GetPlatformSettings() const = 0;

		/** Returns the default device index. */
		virtual bool GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const { OutDefaultDeviceIndex = 0; return true; }

		/** Opens up a new audio stream with the given parameters. */
		virtual bool OpenAudioStream(const FAudioMixerOpenStreamParams& Params) = 0;

		/** Closes the audio stream (if it's open). */
		virtual bool CloseAudioStream() = 0;

		/** Starts the audio stream processing and generating audio. */
		virtual bool StartAudioStream() = 0;

		/** Stops the audio stream (but keeps the audio stream open). */
		virtual bool StopAudioStream() = 0;

		/** Resets the audio stream to use a new audio device with the given device ID (empty string means default). */
		virtual bool MoveAudioStreamToNewAudioDevice(const FString& InNewDeviceId) { return true;  }

		/** Returns the platform device info of the currently open audio stream. */
		virtual FAudioPlatformDeviceInfo GetPlatformDeviceInfo() const = 0;

		/** Submit the given buffer to the platform's output audio device. */
		virtual void SubmitBuffer(const uint8* Buffer) {};

		/** Returns the name of the format of the input sound wave. */
		virtual FName GetRuntimeFormat(USoundWave* InSoundWave) = 0;

		/** Allows platforms to filter the requested number of frames to render. Some platforms only support specific frame counts. */
		virtual int32 GetNumFrames(const int32 InNumReqestedFrames) { return InNumReqestedFrames; }

		/** Checks if the platform has a compressed audio format for sound waves. */
		virtual bool HasCompressedAudioInfoClass(USoundWave* InSoundWave) = 0;

		/** Whether or not the platform supports realtime decompression. */
		virtual bool SupportsRealtimeDecompression() const { return false; }

		/** Whether or not the platform disables caching of decompressed PCM data (i.e. to save memory on fixed memory platforms) */
		virtual bool DisablePCMAudioCaching() const { return false; }

		/** Whether or not this platform has hardware decompression. */
		virtual bool SupportsHardwareDecompression() const { return false; }

		/** Whether this is an interface for a non-realtime renderer. If true, synch events will behave differently to avoid deadlocks. */
		virtual bool IsNonRealtime() const { return false; }

		/** Creates a Compressed audio info class suitable for decompressing this SoundWave. */
		virtual ICompressedAudioInfo* CreateCompressedAudioInfo(USoundWave* SoundWave) = 0;

		/** Return any optional device name defined in platform configuratio. */
		virtual FString GetDefaultDeviceName() = 0;

		// Helper function to gets the channel map type at the given index.
		static bool GetChannelTypeAtIndex(const int32 Index, EAudioMixerChannel::Type& OutType);

        // Function to stop all audio from rendering. Used on mobile platforms which can suspend the application.
        virtual void SuspendContext() {}
        
        // Function to resume audio rendering. Used on mobile platforms which can suspend the application.
        virtual void ResumeContext() {}
        
		// Function called at the beginning of every call of UpdateHardware on the audio thread.
		virtual void OnHardwareUpdate() {}

	public: // Public Functions
		//~ Begin FRunnable
		uint32 Run() override;
		//~ End FRunnable

		/**
		*  FSingleThreadRunnable accessor for ticking this FRunnable when multi-threading is disabled.
		*  @return FSingleThreadRunnable Interface for this FRunnable object.
		*/
		virtual class FSingleThreadRunnable* GetSingleThreadInterface() override { return this; }

		//~ Begin FSingleThreadRunnable Interface
		virtual void Tick() override;
		//~ End FSingleThreadRunnable Interface

		/** Constructor. */
		IAudioMixerPlatformInterface();

		/** Retrieves the next generated buffer and feeds it to the platform mixer output stream. */
		void ReadNextBuffer();

		/** Reset the fade state (use if reusing audio platform interface, e.g. in main audio device. */
		virtual void FadeIn();

		/** Start a fadeout. Prevents pops during shutdown. */
		virtual void FadeOut();

		/** Returns the last error generated. */
		FString GetLastError() const { return LastError; }

		/** This is called after InitializeHardware() is called. */
		void PostInitializeHardware();

	protected:
		
		// Run the "main" audio device
		uint32 MainAudioDeviceRun();
		
		// Wrapper around the thread Run. This is virtualized so a platform can fundamentally override the render function.
		virtual uint32 RunInternal();

		/** Is called when an error is generated. */
		inline void OnAudioMixerPlatformError(const FString& ErrorDetails, const FString& FileName, int32 LineNumber)
		{
#if !NO_LOGGING
			// Log once on these errors to avoid Spam.
			static FCriticalSection Cs;
			static TSet<uint32> LogHistory;
			FScopeLock Lock(&Cs);
			LastError = FString::Printf(TEXT("Audio Platform Device Error: %s (File %s, Line %d)"), *ErrorDetails, *FileName, LineNumber);
			uint32 Hash = GetTypeHash(LastError);
			if (!LogHistory.Contains(Hash))
			{
				UE_LOG(LogAudioMixer, Error, TEXT("%s"), *LastError);
				LogHistory.Add(Hash);
			}
#endif //!NO_LOGGING
		}

		/** Start generating audio from our mixer. */
		void BeginGeneratingAudio();

		/** Stops the render thread from generating audio. */
		void StopGeneratingAudio();

		/** Performs buffer fades for shutdown/startup of audio mixer. */
		void ApplyMasterAttenuation(TArrayView<const uint8>& InOutPoppedAudio);

		template<typename BufferType>
		void ApplyAttenuationInternal(TArrayView<BufferType>& InOutBuffer);

		/** When called, spins up a thread to start consuming output when no audio device is available. */
		void StartRunningNullDevice();

		/** When called, terminates the null device. */
		void StopRunningNullDevice();

	protected:

		/** The audio device stream info. */
		FAudioOutputStreamInfo AudioStreamInfo;
		FAudioMixerOpenStreamParams OpenStreamParams;

		/** List of generated output buffers. */
		Audio::FOutputBuffer OutputBuffer;

		/** Whether or not we warned of buffer underrun. */
		bool bWarnedBufferUnderrun;

		/** The audio render thread. */
		FRunnableThread* AudioRenderThread;

		/** The render thread sync event. */
		FEvent* AudioRenderEvent;

		/** Critical Section used for times when we need the render loop to halt for the device swap. */
		FCriticalSection DeviceSwapCriticalSection;

		/** This is used if we are attempting to TryLock on DeviceSwapCriticalSection, but a buffer callback is being called in the current thread. */
		FThreadSafeBool bIsInDeviceSwap;

		/** Event allows you to block until fadeout is complete. */
		FEvent* AudioFadeEvent;

		/** The number of mixer buffers to queue on the output source voice. */
		int32 NumOutputBuffers;

		/** The fade value. Used for fading in/out master audio. */
		float FadeVolume;

		/** Source param used to fade in and out audio device. */
		FParam FadeParam;

		/** This device name can be used to override the default device being used on platforms that use strings to identify audio devices. */
		FString CurrentDeviceName;

		/** String containing the last generated error. */
		FString LastError;

		int32 CallCounterApplyAttenuationInternal{ 0 };
		int32 CallCounterReadNextBuffer{ 0 };

		FThreadSafeBool bPerformingFade;
		FThreadSafeBool bFadedOut;
		FThreadSafeBool bIsDeviceInitialized;

		FThreadSafeBool bMoveAudioStreamToNewAudioDevice;
		FThreadSafeBool bIsUsingNullDevice;
		FThreadSafeBool bIsGeneratingAudio;

	private:
		TUniquePtr<FMixerNullCallback> NullDeviceCallback;
	};
}

/**
 * Interface for audio device modules
 */

class FAudioDevice;

/** Defines the interface of a module implementing an audio device and associated classes. */
class IAudioDeviceModule : public IModuleInterface
{
public:

	/** Creates a new instance of the audio device implemented by the module. */
	virtual bool IsAudioMixerModule() const { return false; }
	virtual FAudioDevice* CreateAudioDevice() { return nullptr; }
	virtual Audio::IAudioMixerPlatformInterface* CreateAudioMixerPlatformInterface() { return nullptr; }
};
