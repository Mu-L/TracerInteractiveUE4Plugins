// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Transport.h"
#include "Analysis/StreamReader.h"
#include "Containers/Array.h"

namespace Trace
{

class FStreamReader;

////////////////////////////////////////////////////////////////////////////////
class FTidPacketTransport
	: public FTransport
{
public:
	typedef UPTRINT ThreadIter;

	void					Update();
	ThreadIter				ReadThreads();
	FStreamReader*			GetNextThread(ThreadIter& Iter);
	int32					GetThreadId(ThreadIter Iter) const;

private:
	struct FThreadStream
	{
		FStreamBuffer		Buffer;
		uint32				ThreadId;
	};

	bool					ReadPacket();
	FThreadStream&			FindOrAddThread(uint32 ThreadId);
	TArray<FThreadStream>	Threads;
};

} // namespace Trace
