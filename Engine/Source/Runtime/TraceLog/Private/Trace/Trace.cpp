// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Trace.h"
#include "Trace/Detail/EventDef.h"

#if UE_TRACE_ENABLED

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
bool	Writer_SendTo(const ANSICHAR*);
bool	Writer_WriteTo(const ANSICHAR*);
uint32	Writer_EventToggle(const ANSICHAR*, bool);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
template <int DestSize>
static void ToAnsiCheap(ANSICHAR (&Dest)[DestSize], const WIDECHAR* Src)
{
	for (ANSICHAR& Out : Dest)
	{
		Out = ANSICHAR(*Src++ & 0x7f);
		if (Out == '\0')
		{
			break;
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
bool SendTo(const TCHAR* InHost)
{
	char Host[32];
	ToAnsiCheap(Host, InHost);
	return Private::Writer_SendTo(Host);
}

////////////////////////////////////////////////////////////////////////////////
bool WriteTo(const TCHAR* InPath)
{
	char Path[512];
	ToAnsiCheap(Path, InPath);
	return Private::Writer_WriteTo(Path);
}

////////////////////////////////////////////////////////////////////////////////
uint32 ToggleEvent(const TCHAR* Wildcard, bool bState)
{
	ANSICHAR WildcardA[64];
	ToAnsiCheap(WildcardA, Wildcard);

	return Private::Writer_EventToggle(WildcardA, bState);
}

} // namespace Trace

#else

// Workaround for module not having any exported symbols
TRACELOG_API int TraceLogExportedSymbol = 0;

#endif // UE_TRACE_ENABLED
