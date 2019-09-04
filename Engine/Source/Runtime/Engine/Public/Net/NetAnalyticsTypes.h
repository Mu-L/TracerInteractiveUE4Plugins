// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Tracks an FName ID to a time value. Time will be context dependent, but usually
 * represents the total amount of time a specific action took (how long a package
 * took to load, how long an actor had queued bunches, etc.)
 *
 * Could have used a TPair, but this will make it more obvious what we're tracking.
 */
struct ENGINE_API FDelinquencyNameTimePair
{
public:

	FDelinquencyNameTimePair(FName InName, float InTimeSeconds) :
		Name(InName),
		TimeSeconds(InTimeSeconds)
	{
	}

	FName Name;
	float TimeSeconds;
};

struct ENGINE_API FDelinquencyKeyFuncs : public BaseKeyFuncs<FDelinquencyNameTimePair, FDelinquencyNameTimePair, false>
{
	static KeyInitType GetSetKey(ElementInitType Element)
	{
		return Element;
	}

	static bool Matches(KeyInitType LHS, KeyInitType RHS)
	{
		return LHS.Name == RHS.Name;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key.Name);
	}
};

/**
 * Convenience type that can be used to tracks information about things that can result in prolonged
 * periods of apparent network inactivity, despite actually receiving traffic.
 *
 * The overall number of entries is expected to be small, but ultimately is left up to callers.
 */
struct ENGINE_API FDelinquencyAnalytics
{
public:

	explicit FDelinquencyAnalytics(const uint32 InNumberOfTopOffendersToTrack);

	FDelinquencyAnalytics(FDelinquencyAnalytics&& Other);

	FDelinquencyAnalytics(const FDelinquencyAnalytics&) = delete;
	const FDelinquencyAnalytics& operator=(const FDelinquencyAnalytics&) = delete;
	FDelinquencyAnalytics& operator=(FDelinquencyAnalytics&&) = default;

	void Emplace(FName Name, float TimeSeconds)
	{
		Add(FDelinquencyNameTimePair(Name, TimeSeconds));
	}

	/**
	 * Adds the event to the delinquency tracking, by accumulating its time into total time,
	 * and updating any existing events to choose the one with the highest time.
	 *
	 * When NumberOfTopOffendersToTrack == 0, we will just track the set of all events as well as the total time.
	 *
	 * When NumberOfTopOffendersToTrack > 0, we will track the set, total time, and also maintain sorted list
	 * (highest to lowest) of events that occurred.
	 *
	 * By setting NumberOfTopOffendersToTrack to 0, users can manage their own lists of "TopOffenders", or
	 * otherwise avoid the per add overhead of this tracking.
	 */
	void Add(FDelinquencyNameTimePair&& ToTrack);

	const TArray<FDelinquencyNameTimePair>& GetTopOffenders() const
	{
		return TopOffenders;
	}

	const TSet<FDelinquencyNameTimePair, FDelinquencyKeyFuncs>& GetAllDelinquents() const
	{
		return AllDelinquents;
	}

	const float GetTotalTime() const
	{
		return TotalTime;
	}

	const uint32 GetNumberOfTopOffendersToTrack() const
	{
		return NumberOfTopOffendersToTrack;
	}

	void Reset();

	void CountBytes(class FArchive& Ar) const;

private:

	TArray<FDelinquencyNameTimePair> TopOffenders;
	TSet<FDelinquencyNameTimePair, FDelinquencyKeyFuncs> AllDelinquents;
	float TotalTime;

	// This is explicitly non const, as we will be copying / moving these structs around.
	uint32 NumberOfTopOffendersToTrack;
};

/**
 * Tracks data related specific to a NetDriver that can can result in prolonged periods of apparent
 * network inactivity, despite actually receiving traffic.
 *
 * This includes things like Pending Async Loads.
 *
 * Also @see FConnectionDelinquencyAnalytics and FDelinquencyAnalytics.
 */
struct ENGINE_API FNetAsyncLoadDelinquencyAnalytics
{
	FNetAsyncLoadDelinquencyAnalytics() :
		DelinquentAsyncLoads(0),
		MaxConcurrentAsyncLoads(0)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(const uint32 NumberOfTopOffendersToTrack) :
		DelinquentAsyncLoads(NumberOfTopOffendersToTrack),
		MaxConcurrentAsyncLoads(0)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(FNetAsyncLoadDelinquencyAnalytics&& Other) :
		DelinquentAsyncLoads(MoveTemp(Other.DelinquentAsyncLoads)),
		MaxConcurrentAsyncLoads(Other.MaxConcurrentAsyncLoads)
	{
	}

	FNetAsyncLoadDelinquencyAnalytics(const FNetAsyncLoadDelinquencyAnalytics&) = delete;
	const FNetAsyncLoadDelinquencyAnalytics& operator=(const FNetAsyncLoadDelinquencyAnalytics&) = delete;
	FNetAsyncLoadDelinquencyAnalytics& operator=(FNetAsyncLoadDelinquencyAnalytics&&) = default;

	void CountBytes(FArchive& Ar) const
	{
		DelinquentAsyncLoads.CountBytes(Ar);
	}

	void Reset()
	{
		DelinquentAsyncLoads.Reset();
		MaxConcurrentAsyncLoads = 0;
	}

	FDelinquencyAnalytics DelinquentAsyncLoads;
	uint32 MaxConcurrentAsyncLoads;
};

/**
 * Tracks data related specific to a NetConnection that can can result in prolonged periods of apparent
 * network inactivity, despite actually receiving traffic.
 *
 * Also @see FDriverDelinquencyAnalytics and FDelinquencyAnalytics.
 */
struct ENGINE_API FNetQueuedActorDelinquencyAnalytics
{
	FNetQueuedActorDelinquencyAnalytics() :
		DelinquentQueuedActors(0),
		MaxConcurrentQueuedActors(0)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(const uint32 NumberOfTopOffendersToTrack) :
		DelinquentQueuedActors(NumberOfTopOffendersToTrack),
		MaxConcurrentQueuedActors(0)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(FNetQueuedActorDelinquencyAnalytics&& Other) :
		DelinquentQueuedActors(MoveTemp(Other.DelinquentQueuedActors)),
		MaxConcurrentQueuedActors(Other.MaxConcurrentQueuedActors)
	{
	}

	FNetQueuedActorDelinquencyAnalytics(const FNetQueuedActorDelinquencyAnalytics&) = delete;
	const FNetQueuedActorDelinquencyAnalytics& operator=(const FNetQueuedActorDelinquencyAnalytics&) = delete;
	FNetQueuedActorDelinquencyAnalytics& operator=(FNetQueuedActorDelinquencyAnalytics&&) = default;


	void CountBytes(FArchive& Ar) const
	{
		DelinquentQueuedActors.CountBytes(Ar);
	}

	void Reset()
	{
		DelinquentQueuedActors.Reset();
		MaxConcurrentQueuedActors = 0;
	}

	FDelinquencyAnalytics DelinquentQueuedActors;
	uint32 MaxConcurrentQueuedActors;
};

/** Struct wrapping Per Net Connection saturation analytics. */
struct ENGINE_API FNetConnectionSaturationAnalytics
{
	FNetConnectionSaturationAnalytics() :
		NumberOfTrackedFrames(0),
		NumberOfSaturatedFrames(0),
		LongestRunOfSaturatedFrames(0),
		NumberOfReplications(0),
		NumberOfSaturatedReplications(0),
		LongestRunOfSaturatedReplications(0),
		CurrentRunOfSaturatedFrames(0),
		CurrentRunOfSaturatedReplications(0)
	{
	}

	/** The total number of frames that we have currently tracked. */
	const uint32 GetNumberOfTrackedFrames() const
	{
		return NumberOfTrackedFrames;
	}

	/** The number of frames we have reported as saturated.*/
	const uint32 GetNumberOfSaturatedFrames() const
	{
		return NumberOfSaturatedFrames;
	}

	/** The longest number of consecutive frames that we have been saturated. */
	const uint32 GetLongestRunOfSaturatedFrames() const
	{
		return LongestRunOfSaturatedFrames;
	}

	/**
	 * The number of times we have tried to replicate data on this connection
	 * (UNetDriver::ServerReplicateActors / UReplicationGraph::ServerReplicateActors)
	 */
	const uint32 GetNumberOfReplications() const
	{
		return NumberOfReplications;
	}

	/** The number of times we have been pre-empted from replicating all data, due to saturation. */
	const uint32 GetNumberOfSaturatedReplications() const
	{
		return NumberOfSaturatedReplications;
	}

	/** The longest number of consecutive replication attempts where we were pre-empted due to saturation. */
	const uint32 GetLongestRunOfSaturatedReplications() const
	{
		return LongestRunOfSaturatedReplications;
	}

	/** Resets the state of tracking. */
	void Reset();

private:

	friend class UNetConnection;

	void TrackFrame(const bool bIsSaturated);

	void TrackReplication(const bool bIsSaturated);

	uint32 NumberOfTrackedFrames;
	uint32 NumberOfSaturatedFrames;
	uint32 LongestRunOfSaturatedFrames;

	uint32 NumberOfReplications;
	uint32 NumberOfSaturatedReplications;
	uint32 LongestRunOfSaturatedReplications;

	uint32 CurrentRunOfSaturatedFrames;
	uint32 CurrentRunOfSaturatedReplications;
};