// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Color.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Debug/ReporterGraph.h"
#include "Engine/Engine.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Tickable.h"
#include "NetworkPredictionTypes.h"
#include "CanvasItem.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "NetworkSimulationModel.h"
#include "NetworkSimulationModelCVars.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetworkSimDebug, Log, All);

namespace NetworkSimulationModelDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(DrawKeyframes, 1, "nsm.debug.DrawKeyFrames", "Draws keyframe data (text) in debug graphs");
	NETSIM_DEVCVAR_SHIPCONST_INT(DrawNetworkSendLines, 1, "nsm.debug.DrawNetworkSendLines", "Draws lines representing network traffic in debugger");
	NETSIM_DEVCVAR_SHIPCONST_INT(GatherServerSidePIE, 1, "nsm.debug.GatherServerSide", "Whenever we gather debug info from a client side actor, also gather server side equivelent. Only works in PIE.");
}

struct FNetworkSimulationModelDebuggerManager;

NETWORKPREDICTION_API UObject* FindReplicatedObjectOnPIEServer(UObject* ClientObject);

// ------------------------------------------------------------------------------------------------------------------------
//	Debugger support classes
// ------------------------------------------------------------------------------------------------------------------------

struct INetworkSimulationModelDebugger
{
	virtual ~INetworkSimulationModelDebugger() { }

	bool IsActive() { return bActive; }
	void SetActive(bool InActive) { bActive = InActive; }

	virtual void GatherCurrent(FNetworkSimulationModelDebuggerManager& Out, UCanvas* C) = 0;
	virtual void Tick( float DeltaTime ) = 0;

protected:
	bool bActive = false; // Whether you should draw every frame
};

struct NETWORKPREDICTION_API FNetworkSimulationModelDebuggerManager: public FTickableGameObject, FNoncopyable
{
	static FNetworkSimulationModelDebuggerManager& Get();

	~FNetworkSimulationModelDebuggerManager()
	{
		if (Graph.IsValid())
		{
			Graph->RemoveFromRoot();
		}
	}

	FNetworkSimulationModelDebuggerManager()
	{
		DrawDebugServicesHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FNetworkSimulationModelDebuggerManager::DrawDebugService));
		check(DrawDebugServicesHandle.IsValid());
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Outside API (registration, console commands, draw services, etc)
	// ---------------------------------------------------------------------------------------------------------------------------------------

	template <typename T>
	void RegisterNetworkSimulationModel(T* NetworkSim, AActor* OwningActor);

	void SetDebuggerActive(AActor* OwningActor, bool InActive)
	{
		if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
		{
			Debugger->SetActive(InActive);
		}
		ResetCache();
		Gather(LastCanvas.Get());
	}

	void ToggleDebuggerActive(AActor* OwningActor)
	{
		if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
		{
			Debugger->SetActive(!Debugger->IsActive());
		}
		ResetCache();
		Gather(LastCanvas.Get());
	}

	void SetContinousGather(bool InGather)
	{
		bContinousGather = InGather;
		if (!bContinousGather)
		{
			Gather(LastCanvas.Get());
		}
	}

	void ToggleContinousGather()
	{
		SetContinousGather(!bContinousGather);
	}

	void DrawDebugService(UCanvas* C, APlayerController* PC)
	{
		LastCanvas = C;
		if (bContinousGather)
		{
			Gather(C);
		}
		
		FDisplayDebugManager& DisplayDebugManager = C->DisplayDebugManager;
		DisplayDebugManager.Initialize(C, GEngine->GetSmallFont(), FVector2D(4.0f, 150.0f));

		if (Lines.Num() > 0)
		{
			const float TextScale = FMath::Max(C->SizeX / 1920.0f, 1.0f);
			FCanvasTileItem TextBackgroundTile(FVector2D(0.0f, 120.0f), FVector2D(400.0f, 1800.0f) * TextScale, FColor(0, 0, 0, 100));
			TextBackgroundTile.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(TextBackgroundTile);
		}

		// --------------------------------------------------------
		//	Lines
		// --------------------------------------------------------

		for (FDebugLine& Line : Lines)
		{
			DisplayDebugManager.SetDrawColor(Line.Color);
			DisplayDebugManager.DrawString(Line.Str);
		}

		// --------------------------------------------------------
		//	Canvas Items (graphs+text)
		// --------------------------------------------------------
		
		for (auto& Item : CanvasItems[0])
		{
			C->DrawItem(*Item.Get());
		}

		if (NetworkSimulationModelDebugCVars::DrawKeyframes() > 0)
		{
			for (auto& Item : CanvasItems[1])
			{
				C->DrawItem(*Item.Get());
			}
		}
	}

	virtual void Tick( float DeltaTime )
	{
		for (auto It = DebuggerMap.CreateIterator(); It; ++It)
		{
			AActor* Owner = It.Key().Get();
			if (!Owner)
			{
				It.RemoveCurrent();
				continue;;
			}
			
			if (It.Value()->IsActive())
			{
				It.Value()->Tick(DeltaTime);
			}
		}
	}	

	/** return the stat id to use for this tickable **/
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNetworkSimulationModelDebuggerManager, STATGROUP_TaskGraphTasks);
	}

	/** Gathers latest and Logs single frame */
	void LogSingleFrame(FOutputDevice& Ar)
	{
		Gather(LastCanvas.Get());
		
		for (FDebugLine& Line : Lines)
		{
			Ar.Logf(TEXT("%s"), *Line.Str);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------------
	//	Debugging API used by TNetworkSimulationModelDebugger
	// ---------------------------------------------------------------------------------------------------------------------------------------

	void Emit(const FString& Str = FString(), FColor Color = FColor::White, float XOffset=0.f, float YOffset=0.f)
	{
		Lines.Emplace(FDebugLine{ Str, Color, XOffset, YOffset });
	}

	template <typename TBuffer>
	void EmitElement(TBuffer& Buffer, const FStandardLoggingParameters& Parameters)
	{
		FStringOutputDevice StrOut;
		StrOut.SetAutoEmitLineTerminator(true);

		FStandardLoggingParameters LocalParameters = Parameters;
		LocalParameters.Ar = &StrOut;

		auto* Element = Buffer.FindElementByKeyframe(LocalParameters.Keyframe);
		if (Element)
		{
			Element->Log(LocalParameters);
	
			TArray<FString> StrLines;
			StrOut.ParseIntoArrayLines(StrLines, true);
			for (FString& Str : StrLines)
			{
				Emit(Str);
			}
		}
	}

	void EmitQuad(FVector2D ScreenPosition, FVector2D ScreenSize, FColor Color)
	{
		FVector2D Quad[4];
		
		Quad[0].X = ScreenPosition.X;
		Quad[0].Y = ScreenPosition.Y;

		Quad[1].X = ScreenPosition.X;
		Quad[1].Y = ScreenPosition.Y + ScreenSize.Y;

		Quad[2].X = ScreenPosition.X + ScreenSize.X;
		Quad[2].Y = ScreenPosition.Y + ScreenSize.Y;

		Quad[3].X = ScreenPosition.X + ScreenSize.X;
		Quad[3].Y = ScreenPosition.Y;
		
		CanvasItems[0].Emplace( MakeUnique<FCanvasTriangleItem>(Quad[0], Quad[1], Quad[2], GWhiteTexture) );
		CanvasItems[0].Last()->SetColor(Color);

		CanvasItems[0].Emplace( MakeUnique<FCanvasTriangleItem>(Quad[2], Quad[3], Quad[0], GWhiteTexture) );
		CanvasItems[0].Last()->SetColor(Color);
	}

	void EmitText(FVector2D ScreenPosition, FColor Color, const FString& Str)
	{
		CanvasItems[1].Emplace( MakeUnique<FCanvasTextItem>(ScreenPosition, FText::FromString(Str), GEngine->GetTinyFont(), Color) );
	}

	void EmitLine(FVector2D StartPosition, FVector2D EndPosition, FColor Color, float Thickness=1.f)
	{
		CanvasItems[0].Emplace( MakeUnique<FCanvasLineItem>(StartPosition, EndPosition) );
		CanvasItems[0].Last()->SetColor(Color);
		((FCanvasLineItem*)CanvasItems[0].Last().Get())->LineThickness = Thickness;
	}

private:

	INetworkSimulationModelDebugger* Find(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		INetworkSimulationModelDebugger* Debugger = DebuggerMap.FindRef(TWeakObjectPtr<AActor>(Actor));
		if (!Debugger)
		{
			UE_LOG(LogNetworkSimDebug, Warning, TEXT("Could not find NetworkSimulationModel associated with %s"), *GetPathNameSafe(Actor));
		}
		return Debugger;
	}

	void Gather(UCanvas* C)
	{
		ResetCache();

		for (auto It = DebuggerMap.CreateIterator(); It; ++It)
		{
			AActor* Owner = It.Key().Get();
			if (!Owner)
			{
				It.RemoveCurrent();
				continue;;
			}
			
			if (It.Value()->IsActive())
			{
				It.Value()->GatherCurrent(*this, C);
				if (NetworkSimulationModelDebugCVars::GatherServerSidePIE() > 0)
				{
					if (AActor* ServerSideActor = Cast<AActor>(FindReplicatedObjectOnPIEServer(Owner)))
					{
						if (INetworkSimulationModelDebugger* ServerSideSim = Find(ServerSideActor))
						{
							Emit();
							Emit();
							ServerSideSim->GatherCurrent(*this, nullptr); // Dont do graphs for server side state
						}
					}
				}

				// Only gather first active debugger (it would be great to have more control over this when debugging multiples)
				break;
			}
		}
	}

	void ResetCache()
	{
		Lines.Reset();
		
		CanvasItems[0].Reset();
		CanvasItems[1].Reset();
	}

	TMap<TWeakObjectPtr<AActor>, INetworkSimulationModelDebugger*>	DebuggerMap;
	bool bContinousGather = true; // Whether you should gather new data every frame

	FDelegateHandle DrawDebugServicesHandle;
	struct FDebugLine
	{
		FString Str;
		FColor Color;
		float XOffset;
		float YOffset;
	};
	TArray<FDebugLine> Lines;
	TArray<TUniquePtr<FCanvasItem>> CanvasItems[2];
	TWeakObjectPtr<UReporterGraph> Graph;
	TWeakObjectPtr<UCanvas> LastCanvas;
};

template <typename TNetSimModel>
struct TNetworkSimulationModelDebugger : public INetworkSimulationModelDebugger
{
	using TSimTime = typename TNetSimModel::TSimTime;
	using TDebugState = typename TNetSimModel::TDebugState;

	TNetworkSimulationModelDebugger(TNetSimModel* InNetSim, AActor* OwningActor)
	{
		NetworkSim = InNetSim;
		WeakOwningActor = OwningActor;
	}

	~TNetworkSimulationModelDebugger()
	{
		
	}

	struct FCachedScreenPositionMap
	{
		struct FScreenPositions
		{
			void SetSent(const FVector2D& In) { if (SentPosition == FVector2D::ZeroVector) SentPosition = In; }
			void SetRecv(const FVector2D& In) { if (RecvPosition == FVector2D::ZeroVector) RecvPosition = In; }

			FVector2D SentPosition = FVector2D::ZeroVector;
			FVector2D RecvPosition = FVector2D::ZeroVector;
		};

		TMap<int32, FScreenPositions> Keyframes;
	};

	void GatherDebugGraph(FNetworkSimulationModelDebuggerManager& Out, UCanvas* Canvas, TReplicationBuffer<TDebugState>* DebugBuffer, FRect DrawRect, const float MaxColumnTimeSeconds, const float MaxLocalFrameTime, const FString& Header, FCachedScreenPositionMap& SendCache, FCachedScreenPositionMap& RecvCache)
	{
		static float Pad = 2.f;
		static float BaseLineYPCT = 0.8f;

		auto& InputBuffer = NetworkSim->GetHistoricBuffers() ? NetworkSim->GetHistoricBuffers()->Input : NetworkSim->Buffers.Input;

		if (Canvas && DebugBuffer && DebugBuffer->GetNumValidElements() > 0)
		{
			// Outline + Header
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Min.Y), FVector2D(DrawRect.Min.X, DrawRect.Max.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Min.Y), FVector2D(DrawRect.Max.X, DrawRect.Min.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Max.X, DrawRect.Min.Y), FVector2D(DrawRect.Max.X, DrawRect.Max.Y), FColor::White );
			Out.EmitLine( FVector2D(DrawRect.Min.X, DrawRect.Max.Y), FVector2D(DrawRect.Max.X, DrawRect.Max.Y), FColor::White );

			Out.EmitText( DrawRect.Min, FColor::White, Header );

			// Frame Columns
			const float BaseLineYPos = DrawRect.Min.Y + (BaseLineYPCT * (DrawRect.Max.Y - DrawRect.Min.Y));

			const float AboveBaseLineSecondsToPixelsY = (BaseLineYPos - DrawRect.Min.Y - Pad) / MaxColumnTimeSeconds;
			const float BelowBaseLineSecondsToPixelsY = (DrawRect.Max.Y - BaseLineYPos - Pad) / MaxLocalFrameTime;

			const float SecondsToPixelsY = FMath::Min<float>(BelowBaseLineSecondsToPixelsY, AboveBaseLineSecondsToPixelsY);

			Out.EmitLine( FVector2D(DrawRect.Min.X, BaseLineYPos), FVector2D(DrawRect.Max.X, BaseLineYPos), FColor::Black );


			FTextSizingParameters TextSizing;
			TextSizing.DrawFont = GEngine->GetTinyFont();
			TextSizing.Scaling = FVector2D(1.f,1.f);
			Canvas->CanvasStringSize(TextSizing, TEXT("00000"));

			const float FixedWidth = TextSizing.DrawXL;

			float ScreenX = DrawRect.Min.X;
			float ScreenY = BaseLineYPos + Pad;

			for (auto It = DebugBuffer->CreateIterator(); It; ++It)
			{
				auto* DebugState = It.Element();
				const float FrameHeight = SecondsToPixelsY * DebugState->LocalDeltaTimeSeconds;
				
				// Green local frame time (below baseline)
				FColor Color = FColor::Green;
				Out.EmitQuad(FVector2D( ScreenX, ScreenY), FVector2D( FixedWidth, FrameHeight), Color);
				Out.EmitText(FVector2D( ScreenX, ScreenY), FColor::Black, FString::Printf(TEXT("%.2f"), (DebugState->LocalDeltaTimeSeconds * 1000.f)));

				// Processed InputcmdsKeyframes (above baseline)
				float ClientX = ScreenX;
				float ClientY = ScreenY - Pad;

				for (int32 Keyframe : DebugState->ProcessedKeyframes)
				{
					auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe);
					if (Cmd)
					{
						float ClientSizeX = FixedWidth;
						float ClientSizeY = SecondsToPixelsY * Cmd->GetFrameDeltaTime().ToRealTimeSeconds();

						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Blue);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Keyframe));
						ClientY -= (ClientSizeY + Pad);
					}
				}

				// Unprocessed InputCmds (above processed)				
				for (int32 Keyframe = DebugState->LastProcessedKeyframe+1; Keyframe <= DebugState->HeadKeyframe; ++Keyframe)
				{
					if (auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe))
					{
						float ClientSizeX = FixedWidth;
						float ClientSizeY = SecondsToPixelsY * Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
						FVector2D ScreenPos(ClientX, ClientY - ClientSizeY);
						Out.EmitQuad(ScreenPos, FVector2D( ClientSizeX, ClientSizeY), FColor::Red);
						Out.EmitText(ScreenPos, FColor::White, LexToString(Keyframe));
						ClientY -= (ClientSizeY + Pad);
					}
				}

				// Cache Screen Positions based on keyframe
				RecvCache.Keyframes.FindOrAdd(DebugState->LastReceivedInputKeyframe).SetRecv(FVector2D(ScreenX, BaseLineYPos));
				
				// Advance 
				ScreenX += FixedWidth + Pad;

				// Send cache
				SendCache.Keyframes.FindOrAdd(DebugState->LastSentInputKeyframe).SetSent(FVector2D(ScreenX, BaseLineYPos));
			}


			// Remaining Simulation Time
			TOptional<FVector2D> LastLinePos;
			FVector2D LinePos(DrawRect.Min.X, BaseLineYPos);
			for (auto It = DebugBuffer->CreateIterator(); It; ++It)
			{
				auto* DebugState = It.Element();
				
				LinePos.X += FixedWidth + Pad;
				LinePos.Y = BaseLineYPos - (DebugState->RemainingAllowedSimulationTimeSeconds * SecondsToPixelsY);

				FColor LineColor =  FColor::White;
				if (LinePos.Y < DrawRect.Min.Y)
				{
					LinePos.Y = DrawRect.Min.Y;
					LineColor = FColor::Red;
				}
				if (LinePos.Y > DrawRect.Max.Y)
				{
					LinePos.Y = DrawRect.Max.Y;
					LineColor = FColor::Red;
				}
				
				if (LastLinePos.IsSet())
				{
					Out.EmitLine(LastLinePos.GetValue(), LinePos, LineColor, 2.f);
				}

				LastLinePos = LinePos;
				
			}
		}
	}

	void CalcMaxColumnFrameTime(TReplicationBuffer<TDebugState>* DebugBuffer, float& MaxInputTime, float& MaxLocalFrameTime)
	{
		auto& InputBuffer = NetworkSim->GetHistoricBuffers() ? NetworkSim->GetHistoricBuffers()->Input : NetworkSim->Buffers.Input;

		for (auto It = DebugBuffer->CreateIterator(); It; ++It)
		{
			float ColumnTime = 0.f;

			auto* DebugState = It.Element();
			for (int32 Keyframe : DebugState->ProcessedKeyframes)
			{
				auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe);
				if (Cmd)
				{
					ColumnTime += (float)Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
				}
			}
			for (int32 Keyframe = DebugState->LastProcessedKeyframe+1; Keyframe <= DebugState->HeadKeyframe; ++Keyframe)
			{
				if (auto* Cmd = InputBuffer.FindElementByKeyframe(Keyframe))
				{
					ColumnTime += (float)Cmd->GetFrameDeltaTime().ToRealTimeSeconds();
				}
			}

			MaxInputTime = FMath::Max<float>(ColumnTime, MaxInputTime);
			MaxLocalFrameTime = FMath::Max<float>(DebugState->LocalDeltaTimeSeconds, MaxLocalFrameTime);
		}
	}

	void GatherCurrent(FNetworkSimulationModelDebuggerManager& Out, UCanvas* Canvas) override
	{
		AActor* Owner = WeakOwningActor.Get();
		if (!ensure(Owner))
		{
			return;
		}

		// ------------------------------------------------------------------------------------------------------------------------------------------------
		//	Lines
		// ------------------------------------------------------------------------------------------------------------------------------------------------

		Out.Emit(FString::Printf(TEXT("%s - %s"), *Owner->GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Owner->GetLocalRole())), FColor::Yellow);
		Out.Emit(FString::Printf(TEXT("LastProcessedInputKeyframe: %d (%d Buffered)"), NetworkSim->TickInfo.LastProcessedInputKeyframe, NetworkSim->Buffers.Input.GetHeadKeyframe() - NetworkSim->TickInfo.LastProcessedInputKeyframe));
				
		if (Owner->GetLocalRole() == ROLE_AutonomousProxy)
		{			
			FColor Color = FColor::White;
			const bool FaultDetected = NetworkSim->RepProxy_Autonomous.IsReconcileFaultDetected();

			const int32 LastSerializedKeyframe = NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe();

			// Calc how much predicted time we have processed. Note that we use the motionstate buffer to iterate but the MS is on the input cmd. (if we are buffering cmds, don't want to count them)
			
			TSimTime PredictedMS;
			for (int32 PredKeyrame = LastSerializedKeyframe+1; PredKeyrame <= NetworkSim->Buffers.Sync.GetHeadKeyframe(); ++PredKeyrame)
			{
				if (auto* Cmd = NetworkSim->Buffers.Input.FindElementByKeyframe(PredKeyrame))
				{
					PredictedMS += Cmd->GetFrameDeltaTime();
				}
			}

			FString ConfirmedFrameStr = FString::Printf(TEXT("LastConfirmedFrame: %d. Prediction: %d Frames, %s MS"), LastSerializedKeyframe, NetworkSim->Buffers.Sync.GetHeadKeyframe() - LastSerializedKeyframe, *PredictedMS.ToString());
			if (FaultDetected)
			{
				ConfirmedFrameStr += TEXT(" RECONCILE FAULT DETECTED!");
				Color = FColor::Red;
			}

			Out.Emit(*ConfirmedFrameStr, Color);

			FString SimulationTimeString = FString::Printf(TEXT("Local SimulationTime: %s. SerializedSimulationTime: %s. Difference MS: %s"), *NetworkSim->TickInfo.GetTotalProcessedSimulationTime().ToString(),
				*NetworkSim->RepProxy_Autonomous.GetLastSerializedSimTime().ToString(), *(NetworkSim->TickInfo.GetTotalProcessedSimulationTime() - NetworkSim->RepProxy_Autonomous.GetLastSerializedSimTime()).ToString());
			Out.Emit(*SimulationTimeString, Color);

			FString AllowedSimulationTimeString = FString::Printf(TEXT("Allowed Simulation Time: %s. Keyframe: %d/%d/%d"), *NetworkSim->TickInfo.GetRemainingAllowedSimulationTime().ToString(), NetworkSim->TickInfo.MaxAllowedInputKeyframe, NetworkSim->TickInfo.LastProcessedInputKeyframe, NetworkSim->Buffers.Input.GetHeadKeyframe());
			Out.Emit(*AllowedSimulationTimeString, Color);
		}
		else if (Owner->GetLocalRole() == ROLE_SimulatedProxy)
		{
			FColor Color = FColor::White;
			FString TimeString = FString::Printf(TEXT("Total Processed Simulation Time: %s. Last Serialized Simulation Time: %s. Delta: %s"), *NetworkSim->TickInfo.GetTotalProcessedSimulationTime().ToString(), *NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime().ToString(), *(NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime() - NetworkSim->TickInfo.GetTotalProcessedSimulationTime()).ToString());
			Out.Emit(*TimeString, Color);
		}

		auto EmitBuffer = [&Out](FString BufferName, auto& Buffer)
		{
			Out.Emit();
			Out.Emit(FString::Printf(TEXT("//////////////// %s ///////////////"), *BufferName), FColor::Yellow);
			Out.Emit(FString::Printf(TEXT("%s"), *Buffer.GetBasicDebugStr()));		
			Out.Emit();
			Out.EmitElement(Buffer, FStandardLoggingParameters(nullptr, EStandardLoggingContext::Full, Buffer.GetHeadKeyframe()));
		};

		EmitBuffer(TEXT("InputBuffer"), NetworkSim->Buffers.Input);
		EmitBuffer(TEXT("SyncBuffer"), NetworkSim->Buffers.Sync);

		// ------------------------------------------------------------------------------------------------------------------------------------------------
		//	Canvas
		// ------------------------------------------------------------------------------------------------------------------------------------------------

		if (Canvas)
		{
			FRect ServerRect;
			ServerRect.Min.X = 0.30f * (float)Canvas->SizeX;
			ServerRect.Max.X = 0.95f * (float)Canvas->SizeX;
			ServerRect.Min.Y = 0.05f * (float)Canvas->SizeY;
			ServerRect.Max.Y = 0.45f * (float)Canvas->SizeY;

			FRect ClientRect;
			ClientRect.Min.X = 0.30f * (float)Canvas->SizeX;
			ClientRect.Max.X = 0.95f * (float)Canvas->SizeX;
			ClientRect.Min.Y = 0.55f * (float)Canvas->SizeY;
			ClientRect.Max.Y = 0.95f * (float)Canvas->SizeY;

			float MaxColumnTime = 1.f/60.f;
			float MaxLocalFrameTime = 1.f/60.f;
			CalcMaxColumnFrameTime(NetworkSim->GetRemoteDebugBuffer(), MaxColumnTime, MaxLocalFrameTime);
			CalcMaxColumnFrameTime(NetworkSim->GetLocalDebugBuffer(), MaxColumnTime, MaxLocalFrameTime);

			FCachedScreenPositionMap ServerToClientCache;
			FCachedScreenPositionMap ClientToServerCache;

			GatherDebugGraph(Out, Canvas, NetworkSim->GetRemoteDebugBuffer(), ServerRect, MaxColumnTime, MaxLocalFrameTime, TEXT("Server"), ServerToClientCache, ClientToServerCache);
			GatherDebugGraph(Out, Canvas, NetworkSim->GetLocalDebugBuffer(), ClientRect, MaxColumnTime, MaxLocalFrameTime, TEXT("Client"), ClientToServerCache, ServerToClientCache);

			// Network Send/Recv lines
			if (NetworkSimulationModelDebugCVars::DrawNetworkSendLines() > 0)
			{
				for (auto& It : ServerToClientCache.Keyframes)
				{
					const int32 KeyFrame = It.Key;
					auto& Positions = It.Value;
					if (KeyFrame != 0 && Positions.RecvPosition != FVector2D::ZeroVector && Positions.SentPosition != FVector2D::ZeroVector)
					{
						Out.EmitLine(Positions.SentPosition, Positions.RecvPosition, FColor::Purple);

						FVector2D TextPos = Positions.SentPosition + (0.25f * (Positions.RecvPosition - Positions.SentPosition));
						Out.EmitText(TextPos, FColor::Purple, LexToString(KeyFrame));
					}
				}

				for (auto& It : ClientToServerCache.Keyframes)
				{
					const int32 KeyFrame = It.Key;
					auto& Positions = It.Value;
					if (KeyFrame != 0 && Positions.RecvPosition != FVector2D::ZeroVector && Positions.SentPosition != FVector2D::ZeroVector)
					{
						Out.EmitLine(Positions.SentPosition, Positions.RecvPosition, FColor::Orange);

						FVector2D TextPos = Positions.SentPosition + (0.25f * (Positions.RecvPosition - Positions.SentPosition));
						Out.EmitText(TextPos, FColor::Orange, LexToString(KeyFrame));
					}
				}
			}
		}
	}

	void Tick( float DeltaTime ) override
	{
		AActor* Owner = WeakOwningActor.Get();
		if (!Owner)
		{
			return;
		}

		UWorld* World = Owner->GetWorld();

		if (auto* LatestSync = NetworkSim->Buffers.Sync.GetElementFromHead(0))
		{
			LatestSync->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastPredicted, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), NetworkSim->Driver, NetworkSim->Driver);
		}

		FStuff ServerPIEStuff = GetServerPIEStuff();
		if (ServerPIEStuff.NetworkSim)
		{
			if (auto* ServerLatestSync = ServerPIEStuff.NetworkSim->Buffers.Sync.GetElementFromHead(0))
			{
				ServerLatestSync->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::CurrentServerPIE, ServerPIEStuff.NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), ServerPIEStuff.NetworkSim->Driver, NetworkSim->Driver);
			}
		}

		if (Owner->GetLocalRole() == ROLE_AutonomousProxy)
		{
			for (int32 Keyframe = NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe(); Keyframe < NetworkSim->Buffers.Sync.GetHeadKeyframe(); ++Keyframe)
			{
				if (auto* SyncState = NetworkSim->Buffers.Sync.FindElementByKeyframe(Keyframe))
				{
					const EVisualLoggingContext Context = (Keyframe == NetworkSim->RepProxy_Autonomous.GetLastSerializedKeyframe()) ? EVisualLoggingContext::LastConfirmed : EVisualLoggingContext::OtherPredicted;
					SyncState->VisualLog( FVisualLoggingParameters(Context, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), NetworkSim->Driver, NetworkSim->Driver);
				}
			}
		}
		else if (Owner->GetLocalRole() == ROLE_SimulatedProxy)
		{
			NetworkSim->RepProxy_Simulated.GetLastSerializedSyncState().VisualLog( FVisualLoggingParameters(EVisualLoggingContext::LastConfirmed, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), NetworkSim->Driver, NetworkSim->Driver); 
			if (NetworkSim->GetSimulatedUpdateMode() != ESimulatedUpdateMode::Interpolate)
			{
				FVector2D ServerSimulationTimeData(Owner->GetWorld()->GetTimeSeconds(), NetworkSim->RepProxy_Simulated.GetLastSerializedSimulationTime().ToRealTimeMS());
				UE_VLOG_HISTOGRAM(Owner, LogNetworkSimDebug, Log, "Simulated Time Graph", "Serialized Simulation Time", ServerSimulationTimeData);

				FVector2D LocalSimulationTimeData(Owner->GetWorld()->GetTimeSeconds(), NetworkSim->TickInfo.GetTotalProcessedSimulationTime().ToRealTimeMS());
				UE_VLOG_HISTOGRAM(Owner, LogNetworkSimDebug, Log, "Simulated Time Graph", "Local Simulation Time", LocalSimulationTimeData);
			
			}
		}
	}
	

	struct FStuff
	{
		TNetSimModel* NetworkSim = nullptr;
	};

	FStuff GetStuff()
	{
		return {NetworkSim};
	}

	TFunction< FStuff() > GetServerPIEStuff;

private:
	
	TWeakObjectPtr<AActor>	WeakOwningActor;
	TNetSimModel* NetworkSim = nullptr;
};

template <typename T>
void FNetworkSimulationModelDebuggerManager::RegisterNetworkSimulationModel(T* NetworkSim, AActor* OwningActor)
{
	TNetworkSimulationModelDebugger<T>* Debugger = new TNetworkSimulationModelDebugger<T>(NetworkSim, OwningActor);
	DebuggerMap.Add( TWeakObjectPtr<AActor>(OwningActor), Debugger );

	// Gross stuff so that the debugger can find the ServerPIE equiv
	TWeakObjectPtr<AActor> WeakOwner(OwningActor);
	Debugger->GetServerPIEStuff = [WeakOwner, this]()
	{
		if (AActor* ServerOwner = Cast<AActor>(FindReplicatedObjectOnPIEServer(WeakOwner.Get())))
		{
			return ((TNetworkSimulationModelDebugger<T>*)DebuggerMap.FindRef(TWeakObjectPtr<AActor>(ServerOwner)))->GetStuff();
		}
		return typename TNetworkSimulationModelDebugger<T>::FStuff();
	};
}