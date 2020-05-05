// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Model/TimingProfilerPrivate.h"

namespace Trace
{
	class FAnalysisSession;
}

class FGpuProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FGpuProfilerAnalyzer(Trace::FAnalysisSession& Session, Trace::FTimingProfilerProvider& TimingProfilerProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_Frame,
	};

	double GpuTimestampToSessionTime(uint64 GpuMicroseconds);

	Trace::FAnalysisSession& Session;
	Trace::FTimingProfilerProvider& TimingProfilerProvider;
	Trace::FTimingProfilerProvider::TimelineInternal& Timeline;
	TMap<uint64, uint32> EventTypeMap;
	uint64 GpuTimeOffset;
	double MinTime = 0.0f;
	bool Calibrated;
};
