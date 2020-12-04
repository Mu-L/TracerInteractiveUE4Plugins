// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"
#include "Subsystems/WorldSubsystem.h"
#include "TraceFilter.h"

#include "ObjectTrace.generated.h"

#if UE_TRACE_ENABLED && !IS_PROGRAM && !UE_BUILD_SHIPPING
#define OBJECT_TRACE_ENABLED 1
#else
#define OBJECT_TRACE_ENABLED 0
#endif

// World subsystem used to track world info
UCLASS()
class UObjectTraceWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

	UObjectTraceWorldSubsystem()
		: FrameIndex(0)
	{}

public:
	// The frame index incremented each tick
	uint16 FrameIndex;
};

#if OBJECT_TRACE_ENABLED

class UClass;
class UObject;

struct FObjectTrace
{
	/** Initialize object tracing */
	ENGINE_API static void Init();

	/** Shut down object tracing */
	ENGINE_API static void Destroy();

	/** Helper function to output an object */
	ENGINE_API static void OutputClass(const UClass* InClass);

	/** Helper function to output an object */
	ENGINE_API static void OutputObject(const UObject* InObject);

	/** Helper function to output an object event */
	ENGINE_API static void OutputObjectEvent(const UObject* InObject, const TCHAR* InEvent);

	/** Helper function to get an object ID from a UObject */
	ENGINE_API static uint64 GetObjectId(const UObject* InObject);

	/** Helper function to get an object's world's tick counter */
	ENGINE_API static uint16 GetObjectWorldTickCounter(const UObject* InObject);

	/** Helper function to output a world */
	ENGINE_API static void OutputWorld(const UWorld* InWorld);
};

#define TRACE_CLASS(Class) \
	FObjectTrace::OutputClass(Class);

#define TRACE_OBJECT(Object) \
	FObjectTrace::OutputObject(Object);

#if TRACE_FILTERING_ENABLED

#define TRACE_OBJECT_EVENT(Object, Event) \
	if(CAN_TRACE_OBJECT(Object)) { UNCONDITIONAL_TRACE_OBJECT_EVENT(Object, Event); }

#else

#define TRACE_OBJECT_EVENT(Object, Event) \
	UNCONDITIONAL_TRACE_OBJECT_EVENT(Object, Event);

#endif
	
#define UNCONDITIONAL_TRACE_OBJECT_EVENT(Object, Event) \
	FObjectTrace::OutputObjectEvent(Object, TEXT(#Event));

#define TRACE_WORLD(World) \
	FObjectTrace::OutputWorld(World);

#else

#define TRACE_CLASS(Class)
#define TRACE_OBJECT(Object)
#define TRACE_OBJECT_EVENT(Object, Event)
#define TRACE_WORLD(World)

#endif