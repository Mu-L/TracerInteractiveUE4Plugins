// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/PlatformFileTrace.h"

#if PLATFORMFILETRACE_ENABLED

#include "Misc/CString.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "Misc/Parse.h"
#include "Trace/Trace.h"

UE_TRACE_CHANNEL(FileChannel)

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginOpen)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndOpen)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginClose)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndClose)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginRead)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ReadHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint64, Offset)
	UE_TRACE_EVENT_FIELD(uint64, Size)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndRead)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ReadHandle)
	UE_TRACE_EVENT_FIELD(uint64, SizeRead)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, BeginWrite)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WriteHandle)
	UE_TRACE_EVENT_FIELD(uint64, FileHandle)
	UE_TRACE_EVENT_FIELD(uint64, Offset)
	UE_TRACE_EVENT_FIELD(uint64, Size)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(PlatformFile, EndWrite)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WriteHandle)
	UE_TRACE_EVENT_FIELD(uint64, SizeWritten)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

void FPlatformFileTrace::BeginOpen(const TCHAR* Path)
{
	uint16 PathSize = (FCString::Strlen(Path) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(PlatformFile, BeginOpen, FileChannel, PathSize)
		<< BeginOpen.Cycle(FPlatformTime::Cycles64())
		<< BeginOpen.Attachment(Path, PathSize)
		<< BeginOpen.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndOpen(uint64 FileHandle)
{
	UE_TRACE_LOG(PlatformFile, EndOpen, FileChannel)
		<< EndOpen.Cycle(FPlatformTime::Cycles64())
		<< EndOpen.FileHandle(FileHandle)
		<< EndOpen.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::BeginClose(uint64 FileHandle)
{
	UE_TRACE_LOG(PlatformFile, BeginClose, FileChannel)
		<< BeginClose.Cycle(FPlatformTime::Cycles64())
		<< BeginClose.FileHandle(FileHandle)
		<< BeginClose.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndClose()
{
	UE_TRACE_LOG(PlatformFile, EndClose, FileChannel)
		<< EndClose.Cycle(FPlatformTime::Cycles64())
		<< EndClose.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::BeginRead(uint64 ReadHandle, uint64 FileHandle, uint64 Offset, uint64 Size)
{
	UE_TRACE_LOG(PlatformFile, BeginRead, FileChannel)
		<< BeginRead.Cycle(FPlatformTime::Cycles64())
		<< BeginRead.ReadHandle(ReadHandle)
		<< BeginRead.FileHandle(FileHandle)
		<< BeginRead.Offset(Offset)
		<< BeginRead.Size(Size)
		<< BeginRead.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndRead(uint64 ReadHandle, uint64 SizeRead)
{
	UE_TRACE_LOG(PlatformFile, EndRead, FileChannel)
		<< EndRead.Cycle(FPlatformTime::Cycles64())
		<< EndRead.ReadHandle(ReadHandle)
		<< EndRead.SizeRead(SizeRead)
		<< EndRead.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::BeginWrite(uint64 WriteHandle, uint64 FileHandle, uint64 Offset, uint64 Size)
{
	UE_TRACE_LOG(PlatformFile, BeginWrite, FileChannel)
		<< BeginWrite.Cycle(FPlatformTime::Cycles64())
		<< BeginWrite.WriteHandle(WriteHandle)
		<< BeginWrite.FileHandle(FileHandle)
		<< BeginWrite.Offset(Offset)
		<< BeginWrite.Size(Size)
		<< BeginWrite.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::EndWrite(uint64 WriteHandle, uint64 SizeWritten)
{
	UE_TRACE_LOG(PlatformFile, EndWrite, FileChannel)
		<< EndWrite.Cycle(FPlatformTime::Cycles64())
		<< EndWrite.WriteHandle(WriteHandle)
		<< EndWrite.SizeWritten(SizeWritten)
		<< EndWrite.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FPlatformFileTrace::Init(const TCHAR* CmdLine)
{
	if (FParse::Param(CmdLine, TEXT("filetrace")))
	{
		Trace::ToggleChannel(FileChannel, true);
	}
}

#endif
