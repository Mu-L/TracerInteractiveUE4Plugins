// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"

namespace Trace
{
	class IAnalysisSession;
	class FLogProvider;
}

class FLogTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FLogTraceAnalyzer(Trace::IAnalysisSession& Session, Trace::FLogProvider& LogProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override {};

private:
	enum : uint16
	{
		RouteId_LogCategory,
		RouteId_LogMessageSpec,
		RouteId_LogMessage,
	};

	Trace::IAnalysisSession& Session;
	Trace::FLogProvider& LogProvider;
};
