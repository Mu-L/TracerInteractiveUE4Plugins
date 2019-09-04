// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/SingleThreadRunnable.h"
#include "Misc/Timespan.h"
#include "HAL/Runnable.h"
#include "Shared/UdpMessageSegment.h"
#include "Templates/SharedPointer.h"
#include "Containers/Queue.h"

class FEvent;
class FInternetAddr;
class FSocket;
struct FIPv4Endpoint;

#if !defined(WITH_TARGETPLATFORM_SUPPORT)
	#define WITH_TARGETPLATFORM_SUPPORT 0
#endif


/**
 * Implements a beacon sender thread.
 *
 * @todo udpmessaging: gmp: add documentation for FUdpMessageBeacon
 */
class FUdpMessageBeacon
	: public FRunnable
	, private FSingleThreadRunnable
{
public:

	/** 
	 * Creates and initializes a new beacon sender.
	 *
	 * @param InSocket The network socket used to send Hello segments.
	 * @param InSocketId The network socket identifier (used to detect unicast endpoint).
	 * @param InMulticastEndpoint The multicast group endpoint to transport messages to.
	 * @param InStaticEndpoints The static nodes to broadcast to alongside the multicast.
	 */
	FUdpMessageBeacon(FSocket* InSocket, const FGuid& InSocketId, const FIPv4Endpoint& InMulticastEndpoint, const TArray<FIPv4Endpoint>& InStaticEndpoints);

	/** Virtual destructor. */
	virtual ~FUdpMessageBeacon();

public:

	/**
	 * Gets the current time interval between Hello segments.
	 *
	 * @return Beacon interval.
	 */
	FTimespan GetBeaconInterval()
	{
		return BeaconInterval;
	}

	/**
	 * Sets the number of known IP endpoints.
	 *
	 * @param EndpointCount The current number of known endpoints.
	 */
	void SetEndpointCount(int32 EndpointCount);

public:

	//~ FRunnable interface

	virtual FSingleThreadRunnable* GetSingleThreadInterface() override;
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override { }

protected:

	/**
	 * Sends the specified segment.
	 *
	 * @param SegmentType The type of segment to send (Hello or Bye).
	 * @param SocketWaitTime Maximum time to wait for the socket to be ready.
	 * @return true on success, false otherwise.
	 */
	bool SendSegment(EUdpMessageSegments SegmentType, const FTimespan& SocketWaitTime);

	/**
	 * Sends a ping segment to static addresses.
	 * @return true on success, false otherwise.
	 */
	bool SendPing(const FTimespan& SocketWaitTime);

	/**
	 * Update the beacon sender.
	 *
	 * @param CurrentTime The current time (in UTC).
	 * @param SocketWaitTime Maximum time to wait for the socket to be ready.
	 */
	void Update(const FDateTime& CurrentTime, const FTimespan& SocketWaitTime);

protected:

	//~ FSingleThreadRunnable interface

	virtual void Tick() override;

private:

#if WITH_TARGETPLATFORM_SUPPORT
	void HandleTargetPlatformDeviceDiscovered( TSharedRef<class ITargetDevice, ESPMode::ThreadSafe> DiscoveredDevice );
	void HandleTargetPlatformDeviceLost( TSharedRef<class ITargetDevice, ESPMode::ThreadSafe> LostDevice );
	void ProcessPendingEndpoints();
#endif //WITH_TARGETPLATFORM_SUPPORT


	/** Holds the calculated interval between Hello segments. */
	FTimespan BeaconInterval;

	/** Holds an event signaling that an endpoint left. */
	FEvent* EndpointLeftEvent;

	/** Holds the number of known endpoints when NextHelloTime was last calculated. */
	int32 LastEndpointCount;

	/** Holds the time at which the last Hello segment was sent. */
	FDateTime LastHelloSent;

	/** Holds the multicast address and port number to send to. */
	TSharedPtr<FInternetAddr> MulticastAddress;

	/** Holds the static addresses to broadcast ping to. */
	TArray<TSharedPtr<FInternetAddr>> StaticAddresses;

	/** Holds the time at which the next Hello segment must be sent. */
	FDateTime NextHelloTime;

	/** Holds local node identifier. */
	FGuid NodeId;

	/** Holds the socket used to send Hello segments. */
	FSocket* Socket;

	/** Holds a flag indicating that the thread is stopping. */
	bool Stopping;

	/** Holds the thread object. */
	FRunnableThread* Thread;

#if WITH_TARGETPLATFORM_SUPPORT
	/** Holds target devices that have just been discovered **/
	struct FPendingEndpoint
	{
		TSharedPtr<const FInternetAddr> StaticAddress;
		bool bAdd;
	};
	TQueue<FPendingEndpoint,EQueueMode::Mpsc> PendingEndpoints;
#endif //WITH_TARGETPLATFORM_SUPPORT

private:
	
	/** Defines the time interval per endpoint. */
	static const FTimespan IntervalPerEndpoint;

	/** Defines the minimum interval for Hello segments. */
	static const FTimespan MinimumInterval;
};
