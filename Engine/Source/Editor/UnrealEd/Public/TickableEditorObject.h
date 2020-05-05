// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"


/**
 * This class provides common registration for gamethread editor only tickable objects. It is an
 * abstract base class requiring you to implement the GetStatId, IsTickable, and Tick methods.
 * If you need a class that can tick in both the Editor and at Runtime then use FTickableGameObject
 * instead, overriding the IsTickableInEditor() function instead.
 */
class UNREALED_API FTickableEditorObject : public FTickableObjectBase
{
public:

	static void TickObjects(const float DeltaSeconds)
	{
		TArray<FTickableEditorObject*>& PendingTickableObjects = GetPendingTickableObjects();
		TArray<FTickableObjectEntry>& TickableObjects = GetTickableObjects();

		for (FTickableEditorObject* PendingTickable : PendingTickableObjects)
		{
			AddTickableObject(TickableObjects, PendingTickable);
		}
		PendingTickableObjects.Empty();

		if (TickableObjects.Num() > 0)
		{
			check(!bIsTickingObjects);
			bIsTickingObjects = true;

			bool bNeedsCleanup = false;

			for (const FTickableObjectEntry& TickableEntry : TickableObjects)
			{
				if (FTickableObjectBase* TickableObject = TickableEntry.TickableObject)
				{
					if ((TickableEntry.TickType == ETickableTickType::Always) || TickableObject->IsTickable())
					{
						ObjectBeingTicked = TickableObject;
						TickableObject->Tick(DeltaSeconds);
						ObjectBeingTicked = nullptr;
					}

					// In case it was removed during tick
					if (TickableEntry.TickableObject == nullptr)
					{
						bNeedsCleanup = true;
					}
				}
				else
				{
					bNeedsCleanup = true;
				}
			}

			if (bNeedsCleanup)
			{
				TickableObjects.RemoveAll([](const FTickableObjectEntry& Entry) { return (Entry.TickableObject == nullptr); });
			}

			bIsTickingObjects = false;
		}
	}

	/** Registers this instance with the static array of tickable objects. */
	FTickableEditorObject()
	{
		ensure(IsInGameThread() || IsInAsyncLoadingThread());
		check(!GetPendingTickableObjects().Contains(this));
		check(!GetTickableObjects().Contains(this));
		GetPendingTickableObjects().Add(this);
	}

	/** Removes this instance from the static array of tickable objects. */
	virtual ~FTickableEditorObject()
	{
		ensureMsgf(ObjectBeingTicked != this, TEXT("Detected possible memory stomp. We are in the Tickable objects Tick function but hit its deconstructor, the 'this' pointer for the Object will now be invalid"));

		ensure(IsInGameThread() || IsInAsyncLoadingThread());
		if (bCollectionIntact && GetPendingTickableObjects().Remove(this) == 0)
		{
			RemoveTickableObject(GetTickableObjects(), this, bIsTickingObjects);
		}
	}

private:

	/**
	 * Class that avoids crashes when unregistering a tickable editor object too late.
	 *
	 * Some tickable objects can outlive the collection
	 * (global/static destructor order is unpredictable).
	 */
	class TTickableObjectsCollection : public TArray<FTickableObjectEntry>
	{
	public:
		~TTickableObjectsCollection()
		{
			FTickableEditorObject::bCollectionIntact = false;
		}
	};

	friend class TTickableObjectsCollection;

	/** True if collection of tickable objects is still intact. */
	static bool bCollectionIntact;
	/** True if currently ticking of tickable editor objects. */
	static bool bIsTickingObjects;

	/** Set if we are in the Tick function for an editor tickable object */
	static FTickableObjectBase* ObjectBeingTicked;


	static TArray<FTickableObjectEntry>& GetTickableObjects();

	static TArray<FTickableEditorObject*>& GetPendingTickableObjects();
};
