// Copyright Epic Games, Inc. All Rights Reserved.

#include "GpuProfilerTrace.h"
#include "GPUProfiler.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CString.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Trace/Trace.inl"
#include "UObject/NameTypes.h"

#if GPUPROFILERTRACE_ENABLED

namespace GpuProfilerTrace
{

static const uint32 MaxEventBufferSize = 16 << 10;

struct
{
	int64							CalibrationBias;
	FGPUTimingCalibrationTimestamp	Calibration;
	uint64							TimestampBase;
	uint64							LastTimestamp;
	uint32							RenderingFrameNumber;
	uint16							EventBufferSize;
	bool							bActive;
	uint8							EventBuffer[MaxEventBufferSize];
} GCurrentFrame;

static TSet<uint32> GEventNames;

RHI_API UE_TRACE_CHANNEL_EXTERN(GpuChannel)
UE_TRACE_CHANNEL_DEFINE(GpuChannel)

UE_TRACE_EVENT_BEGIN(GpuProfiler, EventSpec, Important)
	UE_TRACE_EVENT_FIELD(uint32, EventType)
	UE_TRACE_EVENT_FIELD(uint16[], Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GpuProfiler, Frame)
	UE_TRACE_EVENT_FIELD(uint64, CalibrationBias)
	UE_TRACE_EVENT_FIELD(uint64, TimestampBase)
	UE_TRACE_EVENT_FIELD(uint32, RenderingFrameNumber)
	UE_TRACE_EVENT_FIELD(uint8[], Data)
UE_TRACE_EVENT_END()

} // namespace GpuProfilerTrace

void FGpuProfilerTrace::BeginFrame(FGPUTimingCalibrationTimestamp& Calibration)
{
	using namespace GpuProfilerTrace;

	if (!bool(GpuChannel))
	{
		return;
	}

	GCurrentFrame.Calibration = Calibration;
	ensure(GCurrentFrame.Calibration.CPUMicroseconds > 0 && GCurrentFrame.Calibration.GPUMicroseconds > 0);
	GCurrentFrame.TimestampBase = 0;
	GCurrentFrame.EventBufferSize = 0;
	GCurrentFrame.bActive = true;
}

void FGpuProfilerTrace::SpecifyEventByName(const FName& Name)
{
	using namespace GpuProfilerTrace;

	if (!GCurrentFrame.bActive)
	{
		return;
	}

	// This function is only called from FRealtimeGPUProfilerFrame::UpdateStats
	// at the end of the frame, so the access to this container is thread safe

	uint32 Index = Name.GetComparisonIndex().ToUnstableInt();
	if (!GEventNames.Contains(Index))
	{
		GEventNames.Add(Index);

		FString String = Name.ToString();
		uint32 NameLength = String.Len() + 1;
		static_assert(sizeof(TCHAR) == sizeof(uint16), "");

		UE_TRACE_LOG(GpuProfiler, EventSpec, GpuChannel)
			<< EventSpec.EventType(Index)
			<< EventSpec.Name((const uint16*)(*String), NameLength);
	}
}

void FGpuProfilerTrace::BeginEventByName(const FName& Name, uint32 FrameNumber, uint64 TimestampMicroseconds)
{
	using namespace GpuProfilerTrace;

	if (!GCurrentFrame.bActive)
	{
		return;
	}

	if (GCurrentFrame.EventBufferSize >= MaxEventBufferSize - 18) // 10 + 8
	{
		return;
	}
	if (GCurrentFrame.TimestampBase == 0)
	{
		GCurrentFrame.TimestampBase = TimestampMicroseconds;
		GCurrentFrame.LastTimestamp = GCurrentFrame.TimestampBase;
		GCurrentFrame.RenderingFrameNumber = FrameNumber;
		if (!GCurrentFrame.Calibration.GPUMicroseconds)
		{
			GCurrentFrame.Calibration.GPUMicroseconds = TimestampMicroseconds;
		}
	}
	uint8* BufferPtr = GCurrentFrame.EventBuffer + GCurrentFrame.EventBufferSize;
	uint64 TimestampDelta = TimestampMicroseconds - GCurrentFrame.LastTimestamp;
	GCurrentFrame.LastTimestamp = TimestampMicroseconds;
	FTraceUtils::Encode7bit((TimestampDelta << 1ull) | 0x1, BufferPtr);
	*reinterpret_cast<uint32*>(BufferPtr) = uint32(Name.GetComparisonIndex().ToUnstableInt());
	GCurrentFrame.EventBufferSize = BufferPtr - GCurrentFrame.EventBuffer + sizeof(uint32);
}

void FGpuProfilerTrace::EndEvent(uint64 TimestampMicroseconds)
{
	using namespace GpuProfilerTrace;

	if (!GCurrentFrame.bActive)
	{
		return;
	}

	uint64 TimestampDelta = TimestampMicroseconds - GCurrentFrame.LastTimestamp;
	GCurrentFrame.LastTimestamp = TimestampMicroseconds;
	uint8* BufferPtr = GCurrentFrame.EventBuffer + GCurrentFrame.EventBufferSize;
	FTraceUtils::Encode7bit(TimestampDelta << 1ull, BufferPtr);
	GCurrentFrame.EventBufferSize = BufferPtr - GCurrentFrame.EventBuffer;
}

void FGpuProfilerTrace::EndFrame()
{
	using namespace GpuProfilerTrace;

	if (GCurrentFrame.EventBufferSize)
	{
		// This subtraction is intended to be performed on uint64 to leverage the wrap around behavior defined by the standard
		uint64 Bias = GCurrentFrame.Calibration.CPUMicroseconds - GCurrentFrame.Calibration.GPUMicroseconds;
		UE_TRACE_LOG(GpuProfiler, Frame, GpuChannel)
			<< Frame.CalibrationBias(Bias)
			<< Frame.TimestampBase(GCurrentFrame.TimestampBase)
			<< Frame.RenderingFrameNumber(GCurrentFrame.RenderingFrameNumber)
			<< Frame.Data(GCurrentFrame.EventBuffer, GCurrentFrame.EventBufferSize);

		GCurrentFrame.EventBufferSize = 0;
	}

	GCurrentFrame.bActive = false;
}

#endif
