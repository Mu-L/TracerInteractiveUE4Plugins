// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "ISequencer.h"
#include "Modules/ModuleInterface.h"
#include "AnimatedPropertyKey.h"
#include "ISequencerChannelInterface.h"

class IKeyArea;
class FExtender;
class FStructOnScope;
class FExtensibilityManager;
class FMenuBuilder;
class ISequencerTrackEditor;
class ISequencerEditorObjectBinding;
class IToolkitHost;
class UMovieSceneSequence;

/** Forward declaration for the default templated channel interface. Include SequencerChannelInterface.h for full definition. */
template<typename> struct TSequencerChannelInterface;

namespace SequencerMenuExtensionPoints
{
	static const FName AddTrackMenu_PropertiesSection("AddTrackMenu_PropertiesSection");
}

/** Enum representing supported scrubber styles */
enum class ESequencerScrubberStyle : uint8
{
	/** Scrubber is represented as a single thin line for the current time, with a constant-sized thumb. */
	Vanilla,

	/** Scrubber thumb occupies a full 'display rate' frame, with a single thin line for the current time. Tailored to frame-accuracy scenarios. */
	FrameBlock,
};

/** A delegate which will create an auto-key handler. */
DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<ISequencerTrackEditor>, FOnCreateTrackEditor, TSharedRef<ISequencer>);

/** A delegate which will create an object binding handler. */
DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<ISequencerEditorObjectBinding>, FOnCreateEditorObjectBinding, TSharedRef<ISequencer>);

/** A delegate that is executed when adding menu content. */
DECLARE_DELEGATE_TwoParams(FOnGetAddMenuContent, FMenuBuilder& /*MenuBuilder*/, TSharedRef<ISequencer>);

/** A delegate that is executed when menu object is clicked. Unlike FExtender delegates we pass in the FGuid which exists even for deleted objects. */
DECLARE_DELEGATE_TwoParams(FOnBuildCustomContextMenuForGuid, FMenuBuilder&, FGuid);

/** A delegate that gets executed then a sequencer is created */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSequencerCreated, TSharedRef<ISequencer>);

/**
 * Sequencer view parameters.
 */
struct FSequencerViewParams
{
	FOnGetAddMenuContent OnGetAddMenuContent;

	FOnBuildCustomContextMenuForGuid OnBuildCustomContextMenuForGuid;

	/** Called when this sequencer has received user focus */
	FSimpleDelegate OnReceivedFocus;

	/** A menu extender for the add menu */
	TSharedPtr<FExtender> AddMenuExtender;

	/** A toolbar extender for the main toolbar */
	TSharedPtr<FExtender> ToolbarExtender;

	/** Unique name for the sequencer. */
	FString UniqueName;

	/** Whether the sequencer is read-only */
	bool bReadOnly;

	/** Style of scrubber to use */
	ESequencerScrubberStyle ScrubberStyle;

	FSequencerViewParams(FString InName = FString())
		: UniqueName(MoveTemp(InName))
		, bReadOnly(false)
		, ScrubberStyle(ESequencerScrubberStyle::Vanilla)
	{ }
};


/**
 * Sequencer initialization parameters.
 */
struct FSequencerInitParams
{
	/** The root movie scene sequence being edited. */
	UMovieSceneSequence* RootSequence;

	/** The asset editor created for this (if any) */
	TSharedPtr<IToolkitHost> ToolkitHost;

	/** View parameters */
	FSequencerViewParams ViewParams;

	/** Whether or not sequencer should be edited within the level editor */
	bool bEditWithinLevelEditor;

	/** Domain-specific spawn register for the movie scene */
	TSharedPtr<FMovieSceneSpawnRegister> SpawnRegister;

	/** Accessor for event contexts */
	TAttribute<TArray<UObject*>> EventContexts;

	/** Accessor for playback context */
	TAttribute<UObject*> PlaybackContext;

	FSequencerInitParams()
		: RootSequence(nullptr)
		, ToolkitHost(nullptr)
		, bEditWithinLevelEditor(false)
		, SpawnRegister(nullptr)
	{}
};

/**
 * Interface for the Sequencer module.
 */
class ISequencerModule
	: public IModuleInterface
{
public:

	virtual ~ISequencerModule();

	/**
	 * Create a new instance of a standalone sequencer that can be added to other UIs.
	 *
	 * @param InitParams Initialization parameters.
	 * @return The new sequencer object.
	 */
	virtual TSharedRef<ISequencer> CreateSequencer(const FSequencerInitParams& InitParams) = 0;

	/** 
	 * Registers a delegate that will create an editor for a track in each sequencer.
	 *
	 * @param InOnCreateTrackEditor	Delegate to register.
	 * @return A handle to the newly-added delegate.
	 */
	virtual FDelegateHandle RegisterTrackEditor(FOnCreateTrackEditor InOnCreateTrackEditor, TArrayView<FAnimatedPropertyKey> AnimatedPropertyTypes = TArrayView<FAnimatedPropertyKey>()) = 0;

	/** 
	 * Unregisters a previously registered delegate for creating a track editor
	 *
	 * @param InHandle	Handle to the delegate to unregister
	 */
	virtual void UnRegisterTrackEditor(FDelegateHandle InHandle) = 0;

	/** 
	 * Registers a delegate that will be called when a sequencer is created
	 *
	 * @param InOnSequencerCreated	Delegate to register.
	 * @return A handle to the newly-added delegate.
	 */
	virtual FDelegateHandle RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate InOnSequencerCreated) = 0;

	/** 
	 * Unregisters a previously registered delegate called when a sequencer is created
	 *
	 * @param InHandle	Handle to the delegate to unregister
	 */
	virtual void UnregisterOnSequencerCreated(FDelegateHandle InHandle) = 0;

	/** 
	 * Registers a delegate that will create editor UI for an object binding in sequencer.
	 *
	 * @param InOnCreateEditorObjectBinding	Delegate to register.
	 * @return A handle to the newly-added delegate.
	 */
	virtual FDelegateHandle RegisterEditorObjectBinding(FOnCreateEditorObjectBinding InOnCreateEditorObjectBinding) = 0;

	/** 
	 * Unregisters a previously registered delegate for creating editor UI for an object binding in sequencer.
	 *
	 * @param InHandle	Handle to the delegate to unregister
	 */
	virtual void UnRegisterEditorObjectBinding(FDelegateHandle InHandle) = 0;

	/**
	 * Register that the specified property type can be animated in sequencer
	 */
	virtual void RegisterPropertyAnimator(FAnimatedPropertyKey Key) = 0;

	/**
	 * Unregister that the specified property type can be animated in sequencer
	 */
	virtual void UnRegisterPropertyAnimator(FAnimatedPropertyKey Key) = 0;

	/**
	 * Check whether the specified property type can be animated by sequeuncer
	 */
	virtual bool CanAnimateProperty(FAnimatedPropertyKey Key) = 0;

	/**
	* Get the extensibility manager for menus.
	*
	* @return ObjectBinding Context Menu extensibility manager.
	*/
	virtual TSharedPtr<FExtensibilityManager> GetObjectBindingContextMenuExtensibilityManager() const = 0;

	/**
	 * Get the extensibility manager for menus.
	 *
	 * @return Add Track Menu extensibility manager.
	 */
	virtual TSharedPtr<FExtensibilityManager> GetAddTrackMenuExtensibilityManager() const = 0;

	/**
	 * Get the extensibility manager for toolbars.
	 *
	 * @return Toolbar extensibility manager.
	 */
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() const = 0;

	/**
	 * Register a sequencer channel type using a default channel interface.
	 */
	template<typename ChannelType>
	void RegisterChannelInterface();

	/**
	 * Register a sequencer channel type using the specified interface.
	 */
	template<typename ChannelType>
	void RegisterChannelInterface(TUniquePtr<ISequencerChannelInterface>&& InInterface);

	/**
	 * Find a sequencer channel for the specified channel type name
	 */
	ISequencerChannelInterface* FindChannelEditorInterface(FName ChannelTypeName) const;

public:

	/** 
	 * Helper template for registering property track editors
	 *
	 * @param InOnCreateTrackEditor	Delegate to register.
	 * @return A handle to the newly-added delegate.
	 */
	template<typename PropertyTrackEditorType>
	FDelegateHandle RegisterPropertyTrackEditor()
	{
		auto PropertyTypes = PropertyTrackEditorType::GetAnimatedPropertyTypes();
		return RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(PropertyTrackEditorType::CreateTrackEditor), PropertyTypes);
	}

public:

	DEPRECATED(4.16, "Please use RegisterTrackEditor")
	FDelegateHandle RegisterTrackEditor_Handle(FOnCreateTrackEditor InOnCreateTrackEditor)
	{
		return RegisterTrackEditor(InOnCreateTrackEditor, TArrayView<FAnimatedPropertyKey>());
	}
	
	DEPRECATED(4.16, "Please use UnRegisterTrackEditor")
	void UnRegisterTrackEditor_Handle(FDelegateHandle InHandle)
	{
		UnRegisterTrackEditor(InHandle);
	}

private:

	/** Map of sequencer interfaces for movie scene channel types, keyed on channel UStruct name */
	TMap<FName, TUniquePtr<ISequencerChannelInterface>> ChannelToEditorInterfaceMap;
};


/**
 * Register a sequencer channel type using a default channel interface.
 */
template<typename ChannelType>
void ISequencerModule::RegisterChannelInterface()
{
	RegisterChannelInterface<ChannelType>(TUniquePtr<ISequencerChannelInterface>(new TSequencerChannelInterface<ChannelType>()));
}

/**
 * Register a sequencer channel type using the specified interface.
 */
template<typename ChannelType>
void ISequencerModule::RegisterChannelInterface(TUniquePtr<ISequencerChannelInterface>&& InInterface)
{
	const FName ChannelTypeName = ChannelType::StaticStruct()->GetFName();
	check(!ChannelToEditorInterfaceMap.Contains(ChannelTypeName));
	ChannelToEditorInterfaceMap.Add(ChannelTypeName, MoveTemp(InInterface));
}

/**
 * Find a sequencer channel for the specified channel type name
 */
inline ISequencerChannelInterface* ISequencerModule::FindChannelEditorInterface(FName ChannelTypeName) const
{
	const TUniquePtr<ISequencerChannelInterface>* Found = ChannelToEditorInterfaceMap.Find(ChannelTypeName);
	ensureMsgf(Found, TEXT("No channel interface found for type ID. Did you call RegisterChannelInterface<> for that type?"));
	return Found ? Found->Get() : nullptr;
}