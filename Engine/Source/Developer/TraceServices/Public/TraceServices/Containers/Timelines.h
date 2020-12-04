// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

namespace Trace
{

enum class EEventEnumerate
{
	Continue,
	Stop,
};


template<typename InEventType>
class ITimeline
{
public:
	typedef InEventType EventType;
	typedef TFunctionRef<EEventEnumerate(bool /*bStart*/, double /*Time*/, const EventType& /*Event*/)> EventCallback;
	typedef TFunctionRef<EEventEnumerate(double /*StartTime*/, double /*EndTime*/, uint32 /*Depth*/, const EventType&/*Event*/)> EventRangeCallback;

	struct FTimelineEventInfo
	{
		double StartTime;
		double EndTime;
		double ExclTime = 0.0;
		EventType Event;
	};

	virtual ~ITimeline() = default;
	virtual uint64 GetModCount() const = 0;
	virtual uint64 GetEventCount() const = 0;
	virtual const InEventType& GetEvent(uint64 InIndex) const = 0;
	virtual double GetStartTime() const = 0;
	virtual double GetEndTime() const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, EventCallback Callback) const = 0;
	virtual void EnumerateEventsDownSampled(double IntervalStart, double IntervalEnd, double Resolution, EventRangeCallback Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, EventCallback Callback) const = 0;
	virtual void EnumerateEvents(double IntervalStart, double IntervalEnd, EventRangeCallback Callback) const = 0;
	
	/**
	 * Finds event information for the event closest to InTime from the interval [InTime - DeltaTime, InTime + DeltaTime]
	 * @param InTime - The time used to query for the event 
	 * @param DeltaTime - Events from interval [InTime - DeltaTime, InTime + DeltaTime] will be considered. The one closest to InTime will be returned
	 * @param Depth - The Depth used to query for the event
	 * @return True if an event was found, False if no event was found for the specified input parameters
	 */
	virtual bool GetEventInfo(double InTime, double DeltaTime, int32 Depth, FTimelineEventInfo& EventInfo) const = 0;
};

}