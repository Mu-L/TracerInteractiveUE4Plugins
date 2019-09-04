// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InputBehavior.h"
#include "InputBehaviorSet.generated.h"



/**
 * This is an internal structure used by UInputBehaviorSet. 
 */
USTRUCT()
struct INTERACTIVETOOLSFRAMEWORK_API FBehaviorInfo
{
	GENERATED_BODY()
	
	/** Reference to a Behavior */
	UPROPERTY()
	UInputBehavior* Behavior;

	/** Source object that provided this Behavior */
	void* Source;

	/** Group identifier for this Behavior */
	FString Group;

	friend bool operator<(const FBehaviorInfo& l, const FBehaviorInfo& r)
	{
		return l.Behavior->GetPriority() < r.Behavior->GetPriority();
	}
};




/**
 * UInputBehaviorSet manages a set of UInputBehaviors, and provides various functions
 * to query and forward events to the set. Tools and Widgets provide instances of this via
 * IInputBehaviorSource, and UInputRouter collects and manages them (see comments there)
 *
 * Behaviors in the set each have a source pointer and group tag, which allows sets of
 * behaviors to be managed together. For example one UInputBehaviorSet can be merged into 
 * another and removed later.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInputBehaviorSet : public UObject
{
	GENERATED_BODY()

public:
	UInputBehaviorSet();
	virtual ~UInputBehaviorSet();

	//
	// Set Management
	//


	/** @return true if there are no Behaviors in set */
	virtual bool IsEmpty() const;

	/** 
	 * Add a Behavior to the set
	 * @param Behavior Behavior to add to set
	 * @param Source pointer to owning object, used only to identify Behavior later
	 * @param GroupName string identifier for this Behavior or group of Behaviors
	 */
	virtual void Add(UInputBehavior* Behavior, void* Source = nullptr, const FString& GroupName = "");

	/**
	 * Merge another BehaviorSet into this Set
	 * @param OtherSet Set of Behaviors to add to this Set
	 * @param Source pointer to owning object, used only to identify Behavior later. If nullptr, source is copied from other Set.
	 * @param NewGroupName string identifier for this Behavior or group of Behaviors. If empty string, group is copied from other Set.
	 */
	virtual void Add(const UInputBehaviorSet* OtherSet, void* NewSource = nullptr, const FString& NewGroupName = "");

	/**
	 * Remove a Behavior from the Set
	 * @param Behavior Behavior to remove
	 * @return true if Behavior was found and removed
	 */
	virtual bool Remove(UInputBehavior* Behavior);
	/**
	 * Remove a group of Behaviors from the Set
	 * @param GroupName name of group, all Behaviors that were added with this GroupName are removed
	 * @return true if any Behaviors were found and removed
	 */
	virtual bool RemoveByGroup(const FString& GroupName);
	/**
	 * Remove a group of Behaviors from the Set
	 * @param Source source object pointer, all Behaviors that were added with this Source pointer are removed.
	 * @return true if any Behaviors were found and removed
	 */
	virtual bool RemoveBySource(void* Source);
	/**
	 * Remove all Behaviors from the set
	 */
	virtual void RemoveAll();


	//
	// Queries and Event Forwarding
	//


	/**
	 * Call UInputBehavior::WantsCapture() on each valid Behavior and collect up the requests that indicated a Capture was desired.
	 * @param InputState current input device state information
	 * @param ResultOut returned set of non-Ignoring FInputCaptureRequests returned by Behaviors
	 */
	virtual void CollectWantsCapture(const FInputDeviceState& InputState, TArray<FInputCaptureRequest>& ResultOut);

	/**
	 * Call UInputBehavior::UpdateHover() on each valid Behavior that wants hover events
	 * @param InputState current input device state information
	 * @return true if any InputBehavior indicated that it wants hover events
	 */
	virtual bool UpdateHover(const FInputDeviceState&  InputState);

	/**
	 * Call UInputBehavior::EndHover() on each valid Behavior that wants hover events
	 * @param InputState current input device state information
	 * @return true if any InputBehavior indicated that it wants hover events
	 */
	virtual bool EndHover(const FInputDeviceState&  InputState);


protected:

	/** Current set of known Behaviors */
	UPROPERTY()
	TArray<FBehaviorInfo> Behaviors;

	/**
	 * called internally when Behaviors list is updated, to re-sort by Priority, etc (@todo: and emit change event)
	 */
	virtual void BehaviorsModified();


	/** @return true if Behavior supports InputState.InputDevice */
	bool SupportsInputType(UInputBehavior* Behavior, const FInputDeviceState& InputState)
	{
		return (Behavior->GetSupportedDevices() & InputState.InputDevice) != EInputDevices::None;
	}

};





// UInterface for IInputBehavior
UINTERFACE(MinimalAPI)
class UInputBehaviorSource : public UInterface
{
	GENERATED_BODY()
};


/** 
 * UObjects that implement IInputBehaviorSource have an UInputBehaviorSet
 * that they can provide (to UInputRouter, primarily)
 * @todo callback/delegate for when the provided InputBehaviorSet changes
 */
class IInputBehaviorSource
{
	GENERATED_BODY()

public:
	/**
	 * @return The current UInputBehaviorSet for this Source
	 */
	virtual const UInputBehaviorSet* GetInputBehaviors() const = 0;
};

