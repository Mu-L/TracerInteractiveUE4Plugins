// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include <memory>
#include <string>
#include <vector>

#ifdef AJA_EXPORTS
#define AJA_API __declspec(dllexport)
#else
#define AJA_API __declspec(dllimport)
#endif

namespace AJA
{

	/*
	 * Types provided from the interface
	 */

	typedef void* FDeviceScanner;
	typedef void* FDeviceInfo;
	typedef void* FAJADevice;
	typedef uint32_t FAJAVideoFormat;

	using LoggingCallbackPtr =  void (*)(const TCHAR* Format, ...);

	/*
	 * Pixel formats supported
	 */

	enum struct EPixelFormat
	{
		PF_8BIT_YCBCR,	// As Input
		PF_8BIT_ARGB,	// As Input/Output
		PF_10BIT_RGB,	// As Output
	};

	/*
	 * Timecode
	 */

	struct AJA_API FTimecode
	{
		FTimecode();
		bool operator== (const FTimecode& Other) const;

		uint32_t Hours;
		uint32_t Minutes;
		uint32_t Seconds;
		uint32_t Frames;
	};

	enum struct ETimecodeFormat
	{
		TCF_None,
		TCF_LTC,
		TCF_VITC1,
	};

	/*
	 * Logging Callbacks
	 */

	AJA_API void SetLoggingCallbacks(LoggingCallbackPtr LogInfoFunc, LoggingCallbackPtr LogWarningFunc, LoggingCallbackPtr LogErrorFunc);


	namespace Private
	{
		class DeviceScanner;
		class InputChannel;
		class OutputChannel;
		class SyncChannel;
		class VideoFormatsScanner;
	}

	/* AJADeviceScanner definition
	*****************************************************************************/
	class  AJA_API AJADeviceScanner
	{
	public:
		const static int32_t FormatedTextSize = 64;
		using FormatedTextType = TCHAR[FormatedTextSize];

		AJADeviceScanner();
		~AJADeviceScanner();

		AJADeviceScanner(const AJADeviceScanner&) = delete;
		AJADeviceScanner& operator=(const AJADeviceScanner&) = delete;

		int32_t GetNumDevices() const;
		bool GetDeviceTextId(int32_t InDeviceIndex, FormatedTextType& OutTextId) const;
		bool GetNumberVideoChannels(int32_t InDeviceIndex, int32_t& OutInput, int32_t& OutOutput) const;

	private:
		Private::DeviceScanner* Scanner;
	};

	/* AJAVideoFormats definition
	*****************************************************************************/
	struct AJA_API AJAVideoFormats
	{
		const static int32_t FormatedTextSize = 64;
		struct AJA_API VideoFormatDescriptor
		{
			VideoFormatDescriptor();

			FAJAVideoFormat VideoFormatIndex;
			TCHAR FormatedText[FormatedTextSize];
			uint32_t FrameRateNumerator;
			uint32_t FrameRateDenominator;
			uint32_t Width;
			uint32_t Height;
			bool bIsProgressive;
			bool bValid;

			bool operator<(const VideoFormatDescriptor& Other) const;
		};

		AJAVideoFormats(int32_t InDeviceId, bool bForOutput);
		~AJAVideoFormats();

		AJAVideoFormats(const AJAVideoFormats&) = delete;
		AJAVideoFormats& operator=(const AJAVideoFormats&) = delete;

		int32_t GetNumSupportedFormat() const;
		VideoFormatDescriptor GetSupportedFormat(int32_t InIndex) const;
		static VideoFormatDescriptor GetVideoFormat(FAJAVideoFormat InVideoFormatIndex);

	private:
		Private::VideoFormatsScanner* Formats;
	};

	/* AJADeviceOptions definition
	*****************************************************************************/
	struct AJA_API AJADeviceOptions
	{
		AJADeviceOptions(uint32_t InChannelIndex)
			: DeviceIndex(InChannelIndex)
			, bWantMutliFormatMode(false)
		{}

		uint32_t DeviceIndex;
		bool bWantMutliFormatMode;
	};

	/* AJASyncChannel definition
	*****************************************************************************/
	struct AJA_API IAJASyncChannelCallbackInterface
	{
		IAJASyncChannelCallbackInterface();
		virtual ~IAJASyncChannelCallbackInterface();

		virtual void OnInitializationCompleted(bool bSucceed) = 0;
	};

	struct AJA_API AJASyncChannelOptions
	{
		AJASyncChannelOptions(const TCHAR* DebugName, uint32_t InChannelIndex);

		IAJASyncChannelCallbackInterface* CallbackInterface;

		uint32_t ChannelIndex; // [1...x]
		FAJAVideoFormat VideoFormatIndex;
		ETimecodeFormat TimecodeFormat;
		bool bOutput; // port is output
	};

	class AJA_API AJASyncChannel
	{
	public:
		AJASyncChannel(AJASyncChannel&) = delete;
		AJASyncChannel& operator=(AJASyncChannel&) = delete;

		AJASyncChannel();
		~AJASyncChannel();

	public:
		//@param PortIndex [1...x]
		//@param bOutput port is output
		bool Initialize(const AJADeviceOptions& InDevice, const AJASyncChannelOptions& InOption);
		void Uninitialize();

		// Only available if the initialization succeeded
		bool WaitForSync(FTimecode& OutTimecode) const;
		bool GetTimecode(FTimecode& OutTimecode) const;

	private:
		Private::SyncChannel* Channel;
	};


	/* IAJAInputOutputChannelCallbackInterface definition
	*****************************************************************************/
	struct AJA_API AJAInputFrameData
	{
		AJAInputFrameData();

		FTimecode Timecode;
		uint32_t FramesDropped; // frame dropped by the AJA
	};

	struct AJA_API AJAOutputFrameData : AJAInputFrameData
	{
		AJAOutputFrameData();

		uint32_t FramesLost; // frame ready by the game but not sent to AJA
	};

	struct AJA_API AJAAncillaryFrameData
	{
		AJAAncillaryFrameData();

		uint8_t* AncBuffer;
		uint32_t AncBufferSize;
		uint8_t* AncF2Buffer;
		uint32_t AncF2BufferSize;
	};

	struct AJA_API AJAAudioFrameData
	{
		AJAAudioFrameData();

		uint8_t* AudioBuffer;
		uint32_t AudioBufferSize;
		uint32_t NumChannels;
		uint32_t AudioRate;
		uint32_t NumSamples;
	};

	struct AJA_API AJAVideoFrameData
	{
		AJAVideoFrameData();
		FAJAVideoFormat VideoFormatIndex;
		uint8_t* VideoBuffer;
		uint32_t VideoBufferSize;
		uint32_t Stride;
		uint32_t Width;
		uint32_t Height;
		bool bIsProgressivePicture;
	};

	struct AJA_API IAJAInputOutputChannelCallbackInterface : IAJASyncChannelCallbackInterface
	{
		IAJAInputOutputChannelCallbackInterface();

		virtual bool OnInputFrameReceived(const AJAInputFrameData& InFrameData, const AJAAncillaryFrameData& InAncillaryFrame, const AJAAudioFrameData& InAudioFrame, const AJAVideoFrameData& InVideoFrame) = 0;
		virtual void OnOutputFrameStarted() { }
		virtual bool OnOutputFrameCopied(const AJAOutputFrameData& InFrameData) = 0;
		virtual void OnCompletion(bool bSucceed) = 0;
	};

	/* AJAInputOutputChannelOptions definition
	*****************************************************************************/
	enum class EAJAReferenceType
	{
		EAJA_REFERENCETYPE_EXTERNAL,
		EAJA_REFERENCETYPE_FREERUN,
		EAJA_REFERENCETYPE_INPUT,
	};

	struct AJA_API AJAInputOutputChannelOptions
	{
		AJAInputOutputChannelOptions(const TCHAR* DebugName, uint32_t InChannelIndex);

		IAJAInputOutputChannelCallbackInterface* CallbackInterface;

		uint32_t NumberOfAudioChannel;
		uint32_t ChannelIndex; // [1...x]
		uint32_t SynchronizeChannelIndex; // [1...x]
		uint32_t OutputKeyChannelIndex; // [1...x] for output
		FAJAVideoFormat VideoFormatIndex;
		EPixelFormat PixelFormat;
		ETimecodeFormat TimecodeFormat;
		EAJAReferenceType OutputReferenceType;

		union
		{
			struct 
			{
				uint32_t bUseAutoCirculating : 1;
				uint32_t bOutput : 1; // port is output
				uint32_t bOutputKey : 1; // output will also sent the key on OutputKeyPortIndex
				uint32_t bOutputFreeRun : 1; // output as fast as the card & game can do
				uint32_t bUseAncillary : 1; // enable ANC system
				uint32_t bUseAncillaryField2 : 1; // enable ANC Field 2 system
				uint32_t bUseAudio : 1; // enable audio input/output
				uint32_t bUseVideo : 1; // enable video input/output
			};
			uint32_t Options;
		};
	};

	/* AJAInputChannel definition
	*****************************************************************************/
	class AJA_API AJAInputChannel
	{
	public:
		AJAInputChannel(AJAInputChannel&) = delete;
		AJAInputChannel& operator=(AJAInputChannel&) = delete;

		AJAInputChannel();
		~AJAInputChannel();

	public:
		bool Initialize(const AJADeviceOptions& InDevice, const AJAInputOutputChannelOptions& Options);
		void Uninitialize();

		// Only available if the initialization succeeded
		uint32_t GetFrameDropCount() const;

	private:
		Private::InputChannel* Channel;
	};

	/* AJAOutputChannel definition
	*****************************************************************************/
	class AJA_API AJAOutputChannel
	{
	public:
		AJAOutputChannel(AJAOutputChannel&) = delete;
		AJAOutputChannel& operator=(AJAOutputChannel&) = delete;

		AJAOutputChannel();
		~AJAOutputChannel();

	public:
		bool Initialize(const AJADeviceOptions& InDevice, const AJAInputOutputChannelOptions& Options);
		void Uninitialize();

		// Set a new video buffer that will be copied to the AJA.
		bool SetVideoBuffer(const FTimecode& InTimecode, const uint8_t* InVideoBuffer, uint32_t InVideoBufferSize);

		bool GetOutputDimension(uint32_t& OutWidth, uint32_t& OutHeight) const;

	private:
		Private::OutputChannel* Channel;
	};

}

