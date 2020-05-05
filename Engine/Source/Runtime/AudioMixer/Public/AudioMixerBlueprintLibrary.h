// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "Sound/SoundEffectSource.h"
#include "SampleBuffer.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundSubmixSend.h"
#include "DSP/SpectrumAnalyzer.h"
#include "AudioMixerBlueprintLibrary.generated.h"

class USoundSubmix;

UENUM(BlueprintType)
enum class EFFTSize : uint8
{
	// 512
	DefaultSize, 

	// 64
	Min,

	// 256
	Small,

	// 512
	Medium, 

	// 1024
	Large, 

	// 4096
	Max, 
};

UENUM()
enum class EFFTPeakInterpolationMethod : uint8
{
	NearestNeighbor,
	Linear,
	Quadratic
};

UENUM()
enum class EFFTWindowType : uint8
{
	// No window is applied. Technically a boxcar window.
	None, 

	// Mainlobe width of -3 dB and sidelove attenuation of ~-40 dB. Good for COLA.
	Hamming,

	// Mainlobe width of -3 dB and sidelobe attenuation of ~-30dB. Good for COLA.
	Hann,

	// Mainlobe width of -3 dB and sidelobe attenuation of ~-60db. Tricky for COLA.
	Blackman
};

UENUM(BlueprintType)
enum class EAudioSpectrumType : uint8
{
	// Spectrum frequency values are equal to magnitude of frequency.
	MagnitudeSpectrum,

	// Spectrum frequency values are equal to magnitude squared.
	PowerSpectrum
};

/** 
* Called when a load request for a sound has completed.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnSoundLoadComplete, const class USoundWave*, LoadedSoundWave, const bool, WasCancelled);


UCLASS(meta=(ScriptName="AudioMixerLibrary"))
class AUDIOMIXER_API UAudioMixerBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Adds a submix effect preset to the master submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta=(WorldContext="WorldContextObject"))
	static void AddMasterSubmixEffect(const UObject* WorldContextObject, USoundEffectSubmixPreset* SubmixEffectPreset);

	/** Removes a submix effect preset from the master submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta=(WorldContext="WorldContextObject"))
	static void RemoveMasterSubmixEffect(const UObject* WorldContextObject, USoundEffectSubmixPreset* SubmixEffectPreset);

	/** Clears all master submix effects. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void ClearMasterSubmixEffects(const UObject* WorldContextObject);

	/** Adds a submix effect preset to the given submix at the end of its submix effect chain. Returns the number of submix effects. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static int32 AddSubmixEffect(const UObject* WorldContextObject, USoundSubmix* SoundSubmix, USoundEffectSubmixPreset* SubmixEffectPreset);

	/** Removes all instances of a submix effect preset from the given submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void RemoveSubmixEffectPreset(const UObject* WorldContextObject, USoundSubmix* SoundSubmix, USoundEffectSubmixPreset* SubmixEffectPreset);

	/** Removes the submix effect at the given submix chain index, if there is a submix effect at that index. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void RemoveSubmixEffectPresetAtIndex(const UObject* WorldContextObject, USoundSubmix* SoundSubmix, int32 SubmixChainIndex);

	/** Replaces the submix effect at the given submix chain index, adds the effect if there is none at that index. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void ReplaceSoundEffectSubmix(const UObject* WorldContextObject, USoundSubmix* InSoundSubmix, int32 SubmixChainIndex, USoundEffectSubmixPreset* SubmixEffectPreset);

	/** Clears all submix effects on the given submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void ClearSubmixEffects(const UObject* WorldContextObject, USoundSubmix* SoundSubmix);

	/** Start recording audio. By leaving the Submix To Record field blank, you can record the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	static void StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration, USoundSubmix* SubmixToRecord = nullptr);
	
	/** Stop recording audio. Path can be absolute, or relative (to the /Saved/BouncedWavFiles folder). By leaving the Submix To Record field blank, you can record the master output of the game.  */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Recording Output", AdvancedDisplay = 4))
	static USoundWave* StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundSubmix* SubmixToRecord = nullptr, USoundWave* ExistingSoundWaveToOverwrite= nullptr);

	/** Pause recording audio, without finalizing the recording to disk. By leaving the Submix To Record field blank, you can record the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	static void PauseRecordingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToPause = nullptr);

	/** Resume recording audio after pausing. By leaving the Submix To Pause field blank, you can record the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	static void ResumeRecordingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToPause = nullptr);

	/** Start spectrum analysis of the audio output. By leaving the Submix To Analyze blank, you can analyze the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	static void StartAnalyzingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToAnalyze = nullptr, EFFTSize FFTSize = EFFTSize::DefaultSize, EFFTPeakInterpolationMethod InterpolationMethod = EFFTPeakInterpolationMethod::Linear, EFFTWindowType WindowType = EFFTWindowType::Hann, float HopSize = 0);

	/** Start spectrum analysis of the audio output. By leaving the Submix To Stop Analyzing blank, you can analyze the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	static void StopAnalyzingOutput(const UObject* WorldContextObject, USoundSubmix* SubmixToStopAnalyzing = nullptr);

	/** Start spectrum analysis of the audio output. By leaving the Submix To Analyze blank, you can analyze the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 3))
	static void GetMagnitudeForFrequencies(const UObject* WorldContextObject, const TArray<float>& Frequencies, TArray<float>& Magnitudes, USoundSubmix* SubmixToAnalyze = nullptr);

	/** Start spectrum analysis of the audio output. By leaving the Submix To Analyze blank, you can analyze the master output of the game. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 3))
	static void GetPhaseForFrequencies(const UObject* WorldContextObject, const TArray<float>& Frequencies, TArray<float>& Phases, USoundSubmix* SubmixToAnalyze = nullptr);

	/** Adds source effect entry to preset chain. Only effects the instance of the preset chain */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void AddSourceEffectToPresetChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, FSourceEffectChainEntry Entry);

	/** Adds source effect entry to preset chain. Only affects the instance of preset chain. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void RemoveSourceEffectFromPresetChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, int32 EntryIndex);

	/** Set whether or not to bypass the effect at the source effect chain index. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static void SetBypassSourceEffectChainEntry(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain, int32 EntryIndex, bool bBypassed);

	/** Returns the number of effect chain entries in the given source effect chain. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects", meta = (WorldContext = "WorldContextObject"))
	static int32 GetNumberOfEntriesInSourceEffectChain(const UObject* WorldContextObject, USoundEffectSourcePresetChain* PresetChain);

	/** Begin loading a sound into the cache so that it can be played immediately. */
	UFUNCTION(BlueprintCallable, Category = "Sound")
	static void PrimeSoundForPlayback(USoundWave* SoundWave, const FOnSoundLoadComplete OnLoadCompletion);

	/** Begin loading any sounds referenced by a sound cue into the cache so that it can be played immediately. */
	UFUNCTION(BlueprintCallable, Category = "Sound")
	static void PrimeSoundCueForPlayback(USoundCue* SoundCue);

	/** Trim memory used by the audio cache. Returns the number of megabytes freed. */
	UFUNCTION(BlueprintCallable, Category = "Sound")
	static float TrimAudioCache(float InMegabytesToFree);

private:
	static void PopulateSpectrumAnalyzerSettings(EFFTSize FFTSize, EFFTPeakInterpolationMethod InterpolationMethod, EFFTWindowType WindowType, float HopSize, Audio::FSpectrumAnalyzerSettings &OutSettings);
};
