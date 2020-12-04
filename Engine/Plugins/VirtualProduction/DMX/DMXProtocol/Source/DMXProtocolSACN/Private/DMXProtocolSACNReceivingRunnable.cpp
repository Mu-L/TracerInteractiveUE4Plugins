// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACNReceivingRunnable.h"

#include "DMXProtocolTypes.h"
#include "DMXProtocolSACN.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "Async/Async.h"
#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"

FDMXProtocolSACNReceivingRunnable::FDMXProtocolSACNReceivingRunnable(uint32 InReceivingRefreshRate, const TSharedRef<FDMXProtocolSACN, ESPMode::ThreadSafe>& InProtocolSACN)
	: Thread(nullptr)
	, bStopping(false)
	, ReceivingRefreshRate(InReceivingRefreshRate)
	, ProtocolSACNPtr(InProtocolSACN)
{
}

FDMXProtocolSACNReceivingRunnable::~FDMXProtocolSACNReceivingRunnable()
{
	Stop();

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;

		Thread = nullptr;
	}
}

TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> FDMXProtocolSACNReceivingRunnable::CreateNew(uint32 InReceivingRefreshRate, const TSharedRef<FDMXProtocolSACN, ESPMode::ThreadSafe>& InProtocolSACN)
{
	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> NewReceivingRunnable = MakeShared<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe>(InReceivingRefreshRate, InProtocolSACN);

	NewReceivingRunnable->Thread = FRunnableThread::Create(static_cast<FRunnable*>(NewReceivingRunnable.Get()), TEXT("DMXProtocolSACNReceivingRunnable"), 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	return NewReceivingRunnable;
}

void FDMXProtocolSACNReceivingRunnable::ClearBuffers()
{
	Queue.Empty();

	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

	AsyncTask(ENamedThreads::GameThread, [ThisSP]() {
		ThisSP->GameThreadOnlyBuffer.Reset();
	});
}

void FDMXProtocolSACNReceivingRunnable::PushDMXPacket(uint16 InUniverse, const FDMXProtocolE131DMPLayerPacket& E131DMPLayerPacket)
{
	TSharedPtr<FDMXSignal> DMXSignal = MakeShared<FDMXSignal>(FApp::GetCurrentTime(), InUniverse, TArray<uint8>(E131DMPLayerPacket.DMX, DMX_UNIVERSE_SIZE));

	Queue.Enqueue(DMXSignal);
}

void FDMXProtocolSACNReceivingRunnable::GameThread_InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment)
{
	check(IsInGameThread());

	TArray<uint8> Channels;
	Channels.AddZeroed(DMX_UNIVERSE_SIZE);

	for (const TPair<uint32, uint8>& ChannelValue : DMXFragment)
	{
		Channels[ChannelValue.Key - 1] = ChannelValue.Value;
	}

	TSharedPtr<FDMXSignal>* ExistingSignalPtr = GameThreadOnlyBuffer.Find(UniverseID);
	if (ExistingSignalPtr)
	{
		// Copy fragments into existing
		for (const TPair<uint32, uint8>& ChannelValue : DMXFragment)
		{
			(*ExistingSignalPtr)->ChannelData[ChannelValue.Key - 1] = ChannelValue.Value;
		}
	}
	else
	{
		GameThreadOnlyBuffer.Add(UniverseID, MakeShared<FDMXSignal>(FApp::GetCurrentTime(), UniverseID, Channels));
	}
}

void FDMXProtocolSACNReceivingRunnable::SetRefreshRate(uint32 NewReceivingRefreshRate)
{
	FScopeLock Lock(&SetReceivingRateLock);

	ReceivingRefreshRate = NewReceivingRefreshRate;
}

bool FDMXProtocolSACNReceivingRunnable::Init()
{
	return true;
}

uint32 FDMXProtocolSACNReceivingRunnable::Run()
{
	while (!bStopping)
	{
		Update();

		FPlatformProcess::SleepNoStats(1.f / ReceivingRefreshRate);
	}

	return 0;
}

void FDMXProtocolSACNReceivingRunnable::Stop()
{
	bStopping = true;
}

void FDMXProtocolSACNReceivingRunnable::Exit()
{

}

FSingleThreadRunnable* FDMXProtocolSACNReceivingRunnable::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolSACNReceivingRunnable::Tick()
{
	// Only called when platform is single-threaded
	Update();
}

void FDMXProtocolSACNReceivingRunnable::Update()
{
	if (bStopping || IsEngineExitRequested())
	{
		return;
	}

	// Let the game thread capture This
	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

	AsyncTask(ENamedThreads::GameThread, [ThisSP]() {

		// Drop signals if they're more than one frame behind the current rate (2 frames)
		double TolerableTimeSeconds = FApp::GetCurrentTime() + 2.f / ThisSP->ReceivingRefreshRate;

		TSharedPtr<FDMXSignal> Signal;
		while (ThisSP->Queue.Dequeue(Signal))
		{
			double SignalTimeSeconds = Signal->Timestamp;
			if (SignalTimeSeconds > TolerableTimeSeconds)
			{
				ThisSP->Queue.Empty();

				UE_LOG(LogDMXProtocol, Warning, TEXT("DMX sACN Network Buffer overflow. Dropping DMX signal."));
				break;
			}

			ThisSP->GameThreadOnlyBuffer.FindOrAdd(Signal->UniverseID) = Signal;

			if (TSharedPtr<FDMXProtocolSACN, ESPMode::ThreadSafe> ProtocolSACN = ThisSP->ProtocolSACNPtr.Pin())
			{
				ProtocolSACN->GetOnGameThreadOnlyBufferUpdated().Broadcast(ProtocolSACN->GetProtocolName(), Signal->UniverseID);
			}
		}
	});
}
