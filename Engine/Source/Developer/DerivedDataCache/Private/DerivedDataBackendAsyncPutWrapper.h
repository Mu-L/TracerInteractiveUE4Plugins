// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "DerivedDataBackendInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/ScopeLock.h"
#include "MemoryDerivedDataBackend.h"
#include "Async/AsyncWork.h"

/** 
 * Thread safe set helper
**/
struct FThreadSet
{
	FCriticalSection	SynchronizationObject;
	TSet<FString>		FilesInFlight;

	void Add(const FString& Key)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		check(Key.Len());
		FilesInFlight.Add(Key);
	}
	void Remove(const FString& Key)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FilesInFlight.Remove(Key);
	}
	bool Exists(const FString& Key)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		return FilesInFlight.Contains(Key);
	}
	bool AddIfNotExists(const FString& Key)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		check(Key.Len());
		if (!FilesInFlight.Contains(Key))
		{
			FilesInFlight.Add(Key);
			return true;
		}
		return false;
	}
};

/** 
 * A backend wrapper that coordinates async puts. This means that a Get will hit an in-memory cache while the async put is still in flight.
**/
class FDerivedDataBackendAsyncPutWrapper : public FDerivedDataBackendInterface
{
public:

	/**
	 * Constructor
	 *
	 * @param	InInnerBackend		Backend to use for storage, my responsibilities are about async puts
	 * @param	bCacheInFlightPuts	if true, cache in-flight puts in a memory cache so that they hit immediately
	 */
	FDerivedDataBackendAsyncPutWrapper(FDerivedDataBackendInterface* InInnerBackend, bool bCacheInFlightPuts);

	/** Return a name for this interface */
	virtual FString GetName() const override
	{
		return FString::Printf(TEXT("AsyncPutWrapper (%s)"), *InnerBackend->GetName());
	}

	/** return true if this cache is writable **/
	virtual bool IsWritable() override;

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass() override;

	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;

	/**
	 * Attempts to make sure the cached data will be available as optimally as possible. This is left up to the implementation to do
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if any steps were performed to optimize future retrieval
	 */
	virtual bool TryToPrefetch(const TCHAR* CacheKey) override;

	/**
	 * Allows the DDC backend to determine if it wants to cache the provided data. Reasons for returning false could be a slow connection,
	 * a file size limit, etc.
	 */
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;

	/**
	 * Synchronous retrieve of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer to receive the results, if any were found
	 * @return				true if any data was found, and in this case OutData is non-empty
	 */
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;

	/**
	 * Asynchronous, fire-and-forget placement of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	InData		Buffer containing the data to cache, can be destroyed after the call returns, immediately
	 * @param	bPutEvenIfExists	If true, then do not attempt skip the put even if CachedDataProbablyExists returns true
	 */
	virtual void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;

	virtual void GatherUsageStats(TMap<FString, FDerivedDataCacheUsageStats>& UsageStatsMap, FString&& GraphPath) override;

	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

private:
	FDerivedDataCacheUsageStats UsageStats;
	FDerivedDataCacheUsageStats PutSyncUsageStats;

	/** Backend to use for storage, my responsibilities are about async puts **/
	FDerivedDataBackendInterface*					InnerBackend;
	/** Memory based cache to deal with gets that happen while an async put is still in flight **/
	TUniquePtr<FDerivedDataBackendInterface>		InflightCache;
	/** We remember outstanding puts so that we don't do them redundantly **/
	FThreadSet										FilesInFlight;
};



