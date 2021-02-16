// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"

#include "DMXRuntimeLog.h"
#include "DMXStats.h"
#include "Library/DMXEntityFixturePatch.h"

/** Listener for a fixture patch's OnFixturePatchReceivedDMXDelegate that does not entangle UObjects references */
class FDMXSharedListener
	: public TSharedFromThis<FDMXSharedListener>
{
public:
	static TSharedPtr<FDMXSharedListener> Create(UDMXComponent* InOwner, UDMXEntityFixturePatch* FixturePatch)
	{
		// Since the patch may have no changed data, we get latest data here, then forward that to the component.
		// That alike, latest data is broadcast instantly when the user sets the patch.
		if (InOwner && InOwner->IsValidLowLevel() && FixturePatch && FixturePatch->IsValidLowLevel())
		{
			FDMXNormalizedAttributeValueMap LatestAttributeValues;
			FixturePatch->GetNormalizedAttributesValues(LatestAttributeValues);
			InOwner->OnFixturePatchReceivedDMX(FixturePatch, LatestAttributeValues);
		}

		// Create the actual object
		TSharedPtr<FDMXSharedListener> NewSharedListener = MakeShared<FDMXSharedListener>();

		NewSharedListener->Owner = InOwner;
		NewSharedListener->SetFixturePatch(FixturePatch);

		return NewSharedListener;
	}

	void OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& ValuePerAttribute)
	{
		Owner->OnFixturePatchReceivedDMX(FixturePatch, ValuePerAttribute);
	}

	void SetFixturePatch(UDMXEntityFixturePatch* FixturePatch)
	{
		Reset();

		if (FixturePatch && !FixturePatch->OnFixturePatchReceivedDMX.IsBoundToObject(this))
		{
			CachedFixturePatch = FixturePatch;
			ReceiveHandle = FixturePatch->OnFixturePatchReceivedDMX.AddSP(this, &FDMXSharedListener::OnFixturePatchReceivedDMX);
		}
	}

private: 
	void Reset()
	{
		if (CachedFixturePatch.IsValid() && CachedFixturePatch->IsValidLowLevel())
		{
			check(ReceiveHandle.IsValid());
			CachedFixturePatch->OnFixturePatchReceivedDMX.Remove(ReceiveHandle);
		}
	}

	FDelegateHandle ReceiveHandle;

	TWeakObjectPtr<UDMXEntityFixturePatch> CachedFixturePatch;

	/** The component that owns the listener. No need to add to referenced objects, instead reset instance in Owner::Destroy */
	UDMXComponent* Owner;
};

UDMXComponent::UDMXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false;
}

void UDMXComponent::OnRegister()
{
	Super::OnRegister();
	
	if (bTickInEditor)
	{
		UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
		SharedListener = FDMXSharedListener::Create(this, FixturePatch);
	}
}

void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();


	UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
	SharedListener = FDMXSharedListener::Create(this, FixturePatch);
}

void UDMXComponent::DestroyComponent(bool bPromoteChildren)
{
	Super::DestroyComponent(bPromoteChildren);

	// Explicitily destroy so we don't get callbacks when this is no longer fully valid
	SharedListener.Reset();
}

#if WITH_EDITOR
void UDMXComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UActorComponent, bTickInEditor) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXComponent, FixturePatchRef))
	{
		if (bTickInEditor || GIsPlayInEditorWorld)
		{
			UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
			SharedListener = FDMXSharedListener::Create(this, FixturePatch);
		}
	}
}
#endif // WITH_EDITOR

void UDMXComponent::OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute)
{
	OnFixturePatchReceived.Broadcast(FixturePatch, NormalizedValuePerAttribute);
}

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatchRef.SetEntity(InFixturePatch);

	// The condition that is not met here is !GIsPlayInEditorWorld && GIsEditor.In other words: We're in Editor but not playing.
	//
	// We do not need to create a new listener under this condition - No listener ever was created and we do not expect an editor time listener.
	// It would cause significant pointless overhead when not PIE, given DMXFixtureActor isn't supporting editor time visualization.
	if (GIsPlayInEditorWorld || !GIsEditor)
	{
		UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
		SharedListener = FDMXSharedListener::Create(this, FixturePatch);
	}
}
