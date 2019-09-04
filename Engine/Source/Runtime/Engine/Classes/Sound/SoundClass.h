// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#endif
#include "SoundClass.generated.h"

UENUM()
namespace EAudioOutputTarget
{
	enum Type
	{
		Speaker UMETA(ToolTip = "Sound plays only from speakers."),
		Controller UMETA(ToolTip = "Sound plays only from controller if present."),
		ControllerFallbackToSpeaker UMETA(ToolTip = "Sound plays on the controller if present. If not present, it plays from speakers.")
	};
}

USTRUCT()
struct FSoundClassEditorData
{
	GENERATED_USTRUCT_BODY()

	int32 NodePosX;

	int32 NodePosY;


	FSoundClassEditorData()
		: NodePosX(0)
		, NodePosY(0)
	{
	}


	friend FArchive& operator<<(FArchive& Ar,FSoundClassEditorData& MySoundClassEditorData)
	{
		return Ar << MySoundClassEditorData.NodePosX << MySoundClassEditorData.NodePosY;
	}
};

/**
 * Structure containing configurable properties of a sound class.
 */
USTRUCT(BlueprintType)
struct FSoundClassProperties
{
	GENERATED_USTRUCT_BODY()

	/** Volume multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float Volume;

	/** Pitch multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float Pitch;

	/** The amount of stereo sounds to bleed to the rear speakers */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float StereoBleed;

	/** The amount of a sound to bleed to the LFE channel */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float LFEBleed;

	/** Voice center channel volume - Not a multiplier (no propagation)	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float VoiceCenterChannelVolume;

	/** Volume of the radio filter effect */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float RadioFilterVolume;

	/** Volume at which the radio filter kicks in */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	float RadioFilterVolumeThreshold;

	/** Sound mix voice - whether to apply audio effects */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bApplyEffects:1;

	/** Whether to artificially prioritise the component to play */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bAlwaysPlay:1;

	/** Whether or not this sound plays when the game is paused in the UI */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bIsUISound:1;

	/** Whether or not this is music (propagates only if parent is true) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bIsMusic:1;

	/** Whether or not this sound class has reverb applied */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bReverb:1;

	/** Amount of audio to send to master reverb effect for 2D sounds played with this sound class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundClassProperties)
	float Default2DReverbSendAmount;

	/** Whether or not this sound class forces sounds to the center channel */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bCenterChannelOnly:1;

	/** Whether the Interior/Exterior volume and LPF modifiers should be applied */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	uint32 bApplyAmbientVolumes:1;

	/** Which output target the sound should be played through */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClassProperties)
	TEnumAsByte<EAudioOutputTarget::Type> OutputTarget;

	FSoundClassProperties()
		: Volume(1.0f)
		, Pitch(1.0f)
		, StereoBleed(0.25f)
		, LFEBleed(0.5f)
		, VoiceCenterChannelVolume(0.0f)
		, RadioFilterVolume(0.0f)
		, RadioFilterVolumeThreshold(0.0f)
		, bApplyEffects(false)
		, bAlwaysPlay(false)
		, bIsUISound(false)
		, bIsMusic(false)
		, bReverb(true)
		, Default2DReverbSendAmount(0.0f)
		, bCenterChannelOnly(false)
		, bApplyAmbientVolumes(false)
		, OutputTarget(EAudioOutputTarget::Speaker)
		{
		}
	
};

/**
 * Structure containing information on a SoundMix to activate passively.
 */
USTRUCT(BlueprintType)
struct FPassiveSoundMixModifier
{
	GENERATED_USTRUCT_BODY()

	/** The SoundMix to activate */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=PassiveSoundMixModifier)
	class USoundMix* SoundMix;

	/** Minimum volume level required to activate SoundMix. Below this value the SoundMix will not be active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=PassiveSoundMixModifier)
	float MinVolumeThreshold;

	/** Maximum volume level required to activate SoundMix. Above this value the SoundMix will not be active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=PassiveSoundMixModifier)
	float MaxVolumeThreshold;

	FPassiveSoundMixModifier()
		: SoundMix(NULL)
		, MinVolumeThreshold(0.f)
		, MaxVolumeThreshold(10.f)
	{
	}
	
};

#if WITH_EDITOR
class USoundClass;

/** Interface for sound class graph interaction with the AudioEditor module. */
class ISoundClassAudioEditor
{
public:
	virtual ~ISoundClassAudioEditor() {}

	/** Refreshes the sound class graph links. */
	virtual void RefreshGraphLinks(UEdGraph* SoundClassGraph) = 0;
};
#endif


UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType)
class ENGINE_API USoundClass : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Configurable properties like volume and priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClass)
	struct FSoundClassProperties Properties;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClass)
	TArray<USoundClass*> ChildClasses;

	/** SoundMix Modifiers to activate automatically when a sound of this class is playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=SoundClass)
	TArray<struct FPassiveSoundMixModifier> PassiveSoundMixModifiers;

public:
	UPROPERTY()
	USoundClass* ParentClass;

#if WITH_EDITORONLY_DATA
	/** EdGraph based representation of the SoundClass */
	class UEdGraph* SoundClassGraph;
#endif

protected:

	//~ Begin UObject Interface.
	virtual void Serialize( FArchive& Ar ) override;
	virtual FString GetDesc( void ) override;
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreEditChange(UProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

public:
	/** 
	 * Get the parameters for the sound mix.
	 */
	void Interpolate( float InterpValue, FSoundClassProperties& Current, const FSoundClassProperties& Start, const FSoundClassProperties& End );

	// Sound Class Editor functionality
#if WITH_EDITOR
	/** 
	 * @return true if the child sound class exists in the tree 
	 */
	bool RecurseCheckChild( USoundClass* ChildSoundClass );

	/**
	 * Set the parent class of this SoundClass, removing it as a child from its previous owner
	 *
	 * @param	InParentClass	The New Parent Class of this
	 */
	void SetParentClass( USoundClass* InParentClass );

	/**
	 * Add Referenced objects
	 *
	 * @param	InThis SoundClass we are adding references from.
	 * @param	Collector Reference Collector
	 */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	 * Refresh all EdGraph representations of SoundClasses
	 *
	 * @param	bIgnoreThis	Whether to ignore this SoundClass if it's already up to date
	 */
	void RefreshAllGraphs(bool bIgnoreThis);

	/** Sets the sound cue graph editor implementation.* */
	static void SetSoundClassAudioEditor(TSharedPtr<ISoundClassAudioEditor> InSoundClassAudioEditor);

	/** Gets the sound cue graph editor implementation. */
	static TSharedPtr<ISoundClassAudioEditor> GetSoundClassAudioEditor();

private:

	/** Ptr to interface to sound class editor operations. */
	static TSharedPtr<ISoundClassAudioEditor> SoundClassAudioEditor;

#endif

};

