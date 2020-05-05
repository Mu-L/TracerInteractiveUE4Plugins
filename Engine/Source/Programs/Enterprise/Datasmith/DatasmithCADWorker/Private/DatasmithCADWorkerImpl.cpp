// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADWorkerImpl.h"

#include "CoreTechFileParser.h"
#include "DatasmithCommands.h"
#include "DatasmithDispatcherConfig.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

using namespace DatasmithDispatcher;


FDatasmithCADWorkerImpl::FDatasmithCADWorkerImpl(int32 InServerPID, int32 InServerPort, const FString& InEnginePluginsPath, const FString& InCachePath)
	: ServerPID(InServerPID)
	, ServerPort(InServerPort)
	, EnginePluginsPath(InEnginePluginsPath)
	, CachePath(InCachePath)
	, PingStartCycle(0)
{
}

bool FDatasmithCADWorkerImpl::Run()
{
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("connect to %d..."), ServerPort);
	bool bConnected = NetworkInterface.Connect(TEXT("Datasmith CAD Worker"), ServerPort, Config::ConnectTimeout_s);
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("connected to %d %s"), ServerPort, bConnected ? TEXT("OK") : TEXT("FAIL"));
	if (bConnected)
	{
		CommandIO.SetNetworkInterface(&NetworkInterface);
	}
	else
	{
		UE_LOG(LogDatasmithCADWorker, Error, TEXT("Server connection failure. exit"));
		return false;
	}

	InitiatePing();

	bool bIsRunning = true;
	while (bIsRunning)
	{
		if (TSharedPtr<ICommand> Command = CommandIO.GetNextCommand(1.0))
		{
			switch(Command->GetType())
			{
				case ECommandId::Ping:
					ProcessCommand(*StaticCast<FPingCommand*>(Command.Get()));
					break;

				case ECommandId::BackPing:
					ProcessCommand(*StaticCast<FBackPingCommand*>(Command.Get()));
					break;

				case ECommandId::RunTask:
					ProcessCommand(*StaticCast<FRunTaskCommand*>(Command.Get()));
					break;

				case ECommandId::ImportParams:
					ProcessCommand(*StaticCast<FImportParametersCommand*>(Command.Get()));
					break;

				case ECommandId::Terminate:
					UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Terminate command received. Exiting."));
					bIsRunning = false;
					break;

				case ECommandId::NotifyEndTask:
				default:
					break;
			}
		}
		else
		{
			if (bIsRunning)
			{
				bIsRunning = ServerPID == 0 ? true : FPlatformProcess::IsApplicationRunning(ServerPID);
				UE_CLOG(!bIsRunning, LogDatasmithCADWorker, Error, TEXT("Worker failure: server lost"));
			}
		}
	}

	UE_CLOG(!bIsRunning, LogDatasmithCADWorker, Verbose, TEXT("Worker loop exit..."));
	CommandIO.Disconnect(0);
	return true;
}

void FDatasmithCADWorkerImpl::InitiatePing()
{
	PingStartCycle = FPlatformTime::Cycles64();
	FPingCommand Ping;
	CommandIO.SendCommand(Ping, Config::SendCommandTimeout_s);
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FPingCommand& PingCommand)
{
	FBackPingCommand BackPing;
	CommandIO.SendCommand(BackPing, Config::SendCommandTimeout_s);
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FBackPingCommand& BackPingCommand)
{
	if (PingStartCycle)
	{
		double ElapsedTime_s = FGenericPlatformTime::ToSeconds(FPlatformTime::Cycles64() - PingStartCycle);
		UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Ping %f s"), ElapsedTime_s);
	}
	PingStartCycle = 0;
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FImportParametersCommand& ImportParametersCommand)
{
	ImportParameters = ImportParametersCommand.ImportParameters;
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FRunTaskCommand& RunTaskCommand)
{
	const CADLibrary::FFileDescription& FileToProcess = RunTaskCommand.JobFileDescription;
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Process %s"), *FileToProcess.Name);

	FCompletedTaskCommand CompletedTask;
#ifdef CAD_INTERFACE
	CADLibrary::FCoreTechFileParser FileParser(ImportParameters, EnginePluginsPath, CachePath);
	CADLibrary::FCoreTechFileParser::EProcessResult ProcessResult = FileParser.ProcessFile(FileToProcess);

	CompletedTask.ProcessResult = ProcessResult;

	if (CompletedTask.ProcessResult == ETaskState::ProcessOk)
	{
		CompletedTask.ExternalReferences = FileParser.GetExternalRefSet().Array();
		CompletedTask.SceneGraphFileName = FileParser.GetSceneGraphFile();
		CompletedTask.GeomFileName = FileParser.GetMeshFileName();
		CompletedTask.WarningMessages = FileParser.GetWarningMessages();
	}
#endif // CAD_INTERFACE
	CommandIO.SendCommand(CompletedTask, Config::SendCommandTimeout_s);

	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("End of Process %s"), *FileToProcess.Name);
}

