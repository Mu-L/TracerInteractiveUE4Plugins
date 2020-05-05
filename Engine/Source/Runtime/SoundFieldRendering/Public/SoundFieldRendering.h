// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISoundfieldFormat.h"
#include "SphericalHarmonicCalculator.h"
#include "DSP/BufferVectorOperations.h"

SOUNDFIELDRENDERING_API FName GetUnrealAmbisonicsFormatName();
SOUNDFIELDRENDERING_API TUniquePtr<ISoundfieldDecoderStream> CreateDefaultSourceAmbisonicsDecoder(Audio::FMixerDevice* InDevice);
SOUNDFIELDRENDERING_API ISoundfieldEncodingSettingsProxy& GetAmbisonicsSourceDefaultSettings();


class SOUNDFIELDRENDERING_API FAmbisonicsSoundfieldBuffer : public ISoundfieldAudioPacket
{
public:
	// Interleaved audio buffer for all vector parts of the ambisonics stream.
	Audio::AlignedFloatBuffer AudioBuffer;

	// number of channels in the Ambisonics stream.
	// Currently we don't explicitly support mixed order ambisonics,
	// so this will always be equal to (m + 1)^2, where m is the order  of ambisonics this was encoded with.
	int32 NumChannels;

	// This is the rotation of the ambisonics source.
	FQuat Rotation;
	FQuat PreviousRotation;

	FAmbisonicsSoundfieldBuffer()
		: NumChannels(0)
		, Rotation(FQuat::Identity)
		, PreviousRotation(FQuat::Identity)
	{}

	virtual void Serialize(FArchive& Ar) override;
	virtual TUniquePtr<ISoundfieldAudioPacket> Duplicate() const override;
	virtual void Reset() override;

};

class SOUNDFIELDRENDERING_API FAmbisonicsSoundfieldSettings : public ISoundfieldEncodingSettingsProxy
{
public:
	int32 Order;

	virtual uint32 GetUniqueId() const override;
	virtual TUniquePtr<ISoundfieldEncodingSettingsProxy> Duplicate() const override;

};

class SOUNDFIELDRENDERING_API FSoundFieldDecoder
{
public:
	FSoundFieldDecoder(); // initializes VirtualSpeakerWorldLockedSpeakerGains

	void DecodeAudioDirectlyToDeviceOutputPositions(const FAmbisonicsSoundfieldBuffer& InputData, const FSoundfieldSpeakerPositionalData& OutputPositions, Audio::AlignedFloatBuffer& OutputData);
	void DecodeAudioToSevenOneAndDownmixToDevice(const FAmbisonicsSoundfieldBuffer& InputData, const FSoundfieldSpeakerPositionalData& OutputPositions, Audio::AlignedFloatBuffer& OutputData);
	static void RotateFirstOrderAmbisonicsBed(const FAmbisonicsSoundfieldBuffer& InputData, FAmbisonicsSoundfieldBuffer& OutputData, const FQuat& DestinationRotation, const FQuat& PreviousRotation);

	static void FoaRotationInPlace(Audio::AlignedFloatBuffer& InOutFrames, const float XRotDegrees, const float YRotDegrees, const float ZRotDegrees);

private:
	// Special cased, vectorized versions of first order operations.
	static void FirstOrderDecodeLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);
	static void FirstOrderToSevenOneLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);

	// Special cased vectorized versions of 3rd and fifth order operations.
	static void OddOrderDecodeLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);
	static void OddOrderToSevenOneLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);

	// Partially vectorized decode operations for 2nd and 4th order operations.
	static void EvenOrderDecodeLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);
	static void EvenOrderToSevenOneLoop(const int32 NumFrames, const int32 NumOutputChannels, const float* pAmbiFrame, const int32 NumAmbiChannels, float* SpeakerGainsPtr, float* OutputBufferPtrBuffer);

	// Helper function to get the default channel positions for a channel array.
	static TArray<Audio::FChannelPositionInfo>* GetDefaultChannelPositions(int32 InNumChannels);

	Audio::AlignedFloatBuffer VirtualSpeakerScratchBuffers;
	Audio::AlignedFloatBuffer FoaVirtualSpeakerWordLockedGains;
	Audio::AlignedFloatBuffer TargetSpeakerGains;
	Audio::AlignedFloatBuffer CurrentSpeakerGains;
	Audio::AlignedFloatBuffer MixdownGainsMap;
	Audio::FBufferLinearEase SpeakerGainLerper;
	FVector2D LastListenerRotationSphericalCoord{ 0.0f, 0.0f };

	static FSphericalHarmonicCalculator SphereHarmCalc;
	static TArray<Audio::FChannelPositionInfo> VirtualSpeakerLocationsHorzOnly;
	static const VectorRegister Sqrt2Over2Vec;
	static const VectorRegister ZeroVec;

	
	friend class FSoundFieldEncoder;
};

class SOUNDFIELDRENDERING_API FSoundFieldEncoder
{
public:
	FSoundFieldEncoder();
	void EncodeAudioDirectlyFromOutputPositions(const Audio::AlignedFloatBuffer& InputData, const FSoundfieldSpeakerPositionalData& InputPositions, const FAmbisonicsSoundfieldSettings& Settings, FAmbisonicsSoundfieldBuffer& OutputData);

private:
	static void EncodeLoop(const int32 NumFrames, const int32 NumInputChannels, const float* RESTRICT InputAudioPtr, const int32 NumAmbiChannels, float* RESTRICT SpeakerGainsPtr, float* RESTRICT OutputAmbiBuffer);

	Audio::AlignedFloatBuffer VirtualSpeakerScratchBuffers;
	Audio::AlignedFloatBuffer SpeakerGains;
	Audio::AlignedFloatBuffer MixdownGainsMap;
};