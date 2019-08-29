// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AutomatedLevelSequenceCapture.h"
#include "MovieScene.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Slate/SceneViewport.h"
#include "Misc/CommandLine.h"
#include "LevelSequenceActor.h"
#include "JsonObjectConverter.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "MovieSceneTranslatorEDL.h"
#include "FCPXML/FCPXMLMovieSceneTranslator.h"
#include "EngineUtils.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "MovieSceneCaptureModule.h"
#include "MovieSceneTimeHelpers.h"
#include "MovieSceneToolHelpers.h"

const FName UAutomatedLevelSequenceCapture::AutomatedLevelSequenceCaptureUIName = FName(TEXT("AutomatedLevelSequenceCaptureUIInstance"));

struct FMovieSceneTimeController_FrameStep : FMovieSceneTimeController
{
	FMovieSceneTimeController_FrameStep()
		: DeltaTime(0)
		, CurrentTime(-1)
	{}

	virtual void OnTick(float DeltaSeconds, float InPlayRate) override
	{
		// Move onto the next frame in the sequence. Play rate dilation occurs in OnRequestCurrentTime, since this InPlayRate does not consider the global world settings dilation
		DeltaTime = FFrameTime(1);
	}

	virtual void OnStartPlaying(const FQualifiedFrameTime& InStartTime)
	{
		DeltaTime   = FFrameTime(0);
		CurrentTime = FFrameTime(-1);
	} 

	virtual FFrameTime OnRequestCurrentTime(const FQualifiedFrameTime& InCurrentTime, float InPlayRate) override
	{
		TOptional<FQualifiedFrameTime> StartTimeIfPlaying = GetPlaybackStartTime();
		if (!StartTimeIfPlaying.IsSet())
		{
			return InCurrentTime.Time;
		}
		else
		{
			// Scale the delta time (should be one frame) by this frame's play rate, and add it to the current time offset
			if (InPlayRate == 1.f)
			{
				CurrentTime += DeltaTime;
			}
			else
			{
				CurrentTime += DeltaTime * InPlayRate;
			}

			DeltaTime = FFrameTime(0);

			ensure(CurrentTime >= 0);
			return StartTimeIfPlaying->ConvertTo(InCurrentTime.Rate) + CurrentTime;
		}
	}

	FFrameTime DeltaTime;
	FFrameTime CurrentTime;
};

UAutomatedLevelSequenceCapture::UAutomatedLevelSequenceCapture(const FObjectInitializer& Init)
	: Super(Init)
{
#if WITH_EDITORONLY_DATA == 0
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		checkf(false, TEXT("Automated level sequence captures can only be used in editor builds."));
	}
#else
	bUseCustomStartFrame = false;
	CustomStartFrame = 0;
	bUseCustomEndFrame = false;
	CustomEndFrame = 1;
	WarmUpFrameCount = 0;
	DelayBeforeWarmUp = 0.0f;
	DelayBeforeShotWarmUp = 0.0f;
	bWriteEditDecisionList = true;
	bWriteFinalCutProXML = true;

	RemainingWarmUpFrames = 0;

	NumShots = 0;
	ShotIndex = -1;

	BurnInOptions = Init.CreateDefaultSubobject<ULevelSequenceBurnInOptions>(this, AutomatedLevelSequenceCaptureUIName);
#endif
}

#if WITH_EDITORONLY_DATA
void UAutomatedLevelSequenceCapture::AddFormatMappings(TMap<FString, FStringFormatArg>& OutFormatMappings, const FFrameMetrics& FrameMetrics) const
{
	OutFormatMappings.Add(TEXT("shot"), CachedState.CurrentShotName);
	OutFormatMappings.Add(TEXT("shot_frame"), FString::Printf(TEXT("%0*d"), Settings.ZeroPadFrameNumbers, CachedState.CurrentShotLocalTime.Time.FrameNumber.Value));

	if (CachedState.CameraComponent && CachedState.CameraComponent->GetOwner())
	{
		OutFormatMappings.Add(TEXT("camera"), CachedState.CameraComponent->GetOwner()->GetName());
	}
}

void UAutomatedLevelSequenceCapture::Initialize(TSharedPtr<FSceneViewport> InViewport, int32 PIEInstance)
{
	Viewport = InViewport;

	// Apply command-line overrides from parent class first. This needs to be called before setting up the capture strategy with the desired frame rate.
	Super::Initialize(InViewport);

	// Apply command-line overrides
	{
		FString LevelSequenceAssetPath;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-LevelSequence=" ), LevelSequenceAssetPath ) )
		{
			LevelSequenceAsset.SetPath( LevelSequenceAssetPath );
		}

		int32 StartFrameOverride;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-MovieStartFrame=" ), StartFrameOverride ) )
		{
			bUseCustomStartFrame = true;
			CustomStartFrame = StartFrameOverride;
		}

		int32 EndFrameOverride;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-MovieEndFrame=" ), EndFrameOverride ) )
		{
			bUseCustomEndFrame = true;
			CustomEndFrame = EndFrameOverride;
		}

		int32 WarmUpFrameCountOverride;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-MovieWarmUpFrames=" ), WarmUpFrameCountOverride ) )
		{
			WarmUpFrameCount = WarmUpFrameCountOverride;
		}

		float DelayBeforeWarmUpOverride;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-MovieDelayBeforeWarmUp=" ), DelayBeforeWarmUpOverride ) )
		{
			DelayBeforeWarmUp = DelayBeforeWarmUpOverride;
		}

		float DelayBeforeShotWarmUpOverride;
		if( FParse::Value( FCommandLine::Get(), TEXT( "-MovieDelayBeforeShotWarmUp=" ), DelayBeforeShotWarmUpOverride ) )
		{
			DelayBeforeShotWarmUp = DelayBeforeShotWarmUpOverride;
		}
	}

	ALevelSequenceActor* Actor = LevelSequenceActor.Get();

	// If we don't have a valid actor, attempt to find a level sequence actor in the world that references this asset
	if( Actor == nullptr )
	{
		if( LevelSequenceAsset.IsValid() )
		{
			ULevelSequence* Asset = Cast<ULevelSequence>( LevelSequenceAsset.TryLoad() );
			if( Asset != nullptr )
			{
				for( auto It = TActorIterator<ALevelSequenceActor>( InViewport->GetClient()->GetWorld() ); It; ++It )
				{
					if( It->LevelSequence == LevelSequenceAsset )
					{
						// Found it!
						Actor = *It;
						this->LevelSequenceActor = Actor;

						break;
					}
				}
			}
		}
	}

	if (!Actor)
	{
		ULevelSequence* Asset = Cast<ULevelSequence>(LevelSequenceAsset.TryLoad());
		if (Asset)
		{
			// Spawn a new actor
			Actor = InViewport->GetClient()->GetWorld()->SpawnActor<ALevelSequenceActor>();
			Actor->SetSequence(Asset);
		
			LevelSequenceActor = Actor;
		}
		else
		{
			//FPlatformMisc::RequestExit(FMovieSceneCaptureExitCodes::AssetNotFound);
		}
	}

	if (Actor)
	{
		// Ensure it doesn't loop (-1 is indefinite)
		Actor->PlaybackSettings.LoopCount = 0;
		Actor->PlaybackSettings.TimeController = MakeShared<FMovieSceneTimeController_FrameStep>();
		Actor->PlaybackSettings.bPauseAtEnd = true;

		if (BurnInOptions)
		{
			Actor->BurnInOptions = BurnInOptions;

			bool bUseBurnIn = false;
			if( FParse::Bool( FCommandLine::Get(), TEXT( "-UseBurnIn=" ), bUseBurnIn ) )
			{
				Actor->BurnInOptions->bUseBurnIn = bUseBurnIn;
			}
		}

		Actor->RefreshBurnIn();

		// Make sure we're not playing yet, and have a fully up to date player based on the above settings (in case AutoPlay was called from BeginPlay)
		if( Actor->SequencePlayer != nullptr )
		{
			if (Actor->SequencePlayer->IsPlaying())
			{
				Actor->SequencePlayer->Stop();
			}
			Actor->InitializePlayer();
		}
		Actor->bAutoPlay = false;

		if (InitializeShots())
		{
			FFrameNumber StartTime, EndTime;
			SetupShot(StartTime, EndTime);
		}
	}
	else
	{
		UE_LOG(LogMovieSceneCapture, Error, TEXT("Could not find or create a Level Sequence Actor for this capture. Capturing will fail."));
	}

	ExportEDL();
	ExportFCPXML();

	CaptureState = ELevelSequenceCaptureState::Setup;
	CaptureStrategy = MakeShareable(new FFixedTimeStepCaptureStrategy(Settings.FrameRate));
}

UMovieScene* GetMovieScene(TWeakObjectPtr<ALevelSequenceActor> LevelSequenceActor)
{
	ALevelSequenceActor* Actor = LevelSequenceActor.Get();
	if (!Actor)
	{
		return nullptr;
	}

	ULevelSequence* LevelSequence = Cast<ULevelSequence>( Actor->LevelSequence.TryLoad() );
	if (!LevelSequence)
	{
		return nullptr;
	}

	return LevelSequence->GetMovieScene();
}

UMovieSceneCinematicShotTrack* GetCinematicShotTrack(TWeakObjectPtr<ALevelSequenceActor> LevelSequenceActor)
{
	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return nullptr;
	}

	return MovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>();
}

bool UAutomatedLevelSequenceCapture::InitializeShots()
{
	NumShots = 0;
	ShotIndex = -1;
	CachedShotStates.Empty();

	if (Settings.HandleFrames <= 0)
	{
		return false;
	}

	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return false;
	}

	UMovieSceneCinematicShotTrack* CinematicShotTrack = GetCinematicShotTrack(LevelSequenceActor);
	if (!CinematicShotTrack)
	{
		return false;
	}

	NumShots = CinematicShotTrack->GetAllSections().Num();
	ShotIndex = 0;
	CachedPlaybackRange = MovieScene->GetPlaybackRange();

	// Compute handle frames in tick resolution space since that is what the section ranges are defined in
	FFrameNumber HandleFramesResolutionSpace = ConvertFrameTime(Settings.HandleFrames, Settings.FrameRate, MovieScene->GetTickResolution()).FloorToFrame();

	for (int32 SectionIndex = 0; SectionIndex < CinematicShotTrack->GetAllSections().Num(); ++SectionIndex)
	{
		UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(CinematicShotTrack->GetAllSections()[SectionIndex]);
		UMovieScene* ShotMovieScene = ShotSection->GetSequence() ? ShotSection->GetSequence()->GetMovieScene() : nullptr;

		if (ShotMovieScene != nullptr)
		{
			// Expand the inner shot section range by the handle size, multiplied by the difference between the outer and inner tick resolutions (and factoring in the time scale)
			const float OuterToInnerRateDilation = (MovieScene->GetTickResolution() == ShotMovieScene->GetTickResolution()) ? 1.f : (ShotMovieScene->GetTickResolution() / MovieScene->GetTickResolution()).AsDecimal();
			const float OuterToInnerScale = OuterToInnerRateDilation * ShotSection->Parameters.TimeScale;

			CachedShotStates.Add(FCinematicShotCache(ShotSection->IsActive(), ShotSection->IsLocked(), ShotSection->GetRange(), ShotMovieScene ? ShotMovieScene->GetPlaybackRange() : TRange<FFrameNumber>::Empty()));

			if (ShotMovieScene)
			{
				TRange<FFrameNumber> NewPlaybackRange = MovieScene::ExpandRange(ShotMovieScene->GetPlaybackRange(), FFrameNumber(FMath::FloorToInt(HandleFramesResolutionSpace.Value * OuterToInnerScale)));
				ShotMovieScene->SetPlaybackRange(NewPlaybackRange, false);
			}

			ShotSection->SetIsLocked(false);
			ShotSection->SetIsActive(false);

			ShotSection->SetRange(MovieScene::ExpandRange(ShotSection->GetRange(), HandleFramesResolutionSpace));
		}
	}
	return NumShots > 0;
}

void UAutomatedLevelSequenceCapture::RestoreShots()
{
	if (Settings.HandleFrames <= 0)
	{
		return;
	}

	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return;
	}

	UMovieSceneCinematicShotTrack* CinematicShotTrack = GetCinematicShotTrack(LevelSequenceActor);
	if (!CinematicShotTrack)
	{
		return;
	}

	MovieScene->SetPlaybackRange(CachedPlaybackRange, false);

	for (int32 SectionIndex = 0; SectionIndex < CinematicShotTrack->GetAllSections().Num(); ++SectionIndex)
	{
		UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(CinematicShotTrack->GetAllSections()[SectionIndex]);
		UMovieScene* ShotMovieScene = ShotSection->GetSequence() ? ShotSection->GetSequence()->GetMovieScene() : nullptr;
		if (ShotMovieScene)
		{
			ShotMovieScene->SetPlaybackRange(CachedShotStates[SectionIndex].MovieSceneRange, false);
		}
		ShotSection->SetIsActive(CachedShotStates[SectionIndex].bActive);
		ShotSection->SetRange(CachedShotStates[SectionIndex].ShotRange);
		ShotSection->SetIsLocked(CachedShotStates[SectionIndex].bLocked);
	}
}

bool UAutomatedLevelSequenceCapture::SetupShot(FFrameNumber& StartTime, FFrameNumber& EndTime)
{
	if (Settings.HandleFrames <= 0)
	{
		return false;
	}

	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return false;
	}

	UMovieSceneCinematicShotTrack* CinematicShotTrack = GetCinematicShotTrack(LevelSequenceActor);
	if (!CinematicShotTrack)
	{
		return false;
	}

	if (ShotIndex > CinematicShotTrack->GetAllSections().Num()-1)
	{
		return false;
	}

	// Disable all shots unless it's the current one being rendered
	for (int32 SectionIndex = 0; SectionIndex < CinematicShotTrack->GetAllSections().Num(); ++SectionIndex)
	{
		UMovieSceneSection* ShotSection = CinematicShotTrack->GetAllSections()[SectionIndex];

		ShotSection->SetIsActive(SectionIndex == ShotIndex);
		ShotSection->MarkAsChanged();

		if (SectionIndex == ShotIndex)
		{
			TRange<FFrameNumber> TotalRange = TRange<FFrameNumber>::Intersection(ShotSection->GetRange(), CachedPlaybackRange);

			StartTime = TotalRange.IsEmpty() ? FFrameNumber(0) : MovieScene::DiscreteInclusiveLower(TotalRange);
			EndTime   = TotalRange.IsEmpty() ? FFrameNumber(0) : MovieScene::DiscreteExclusiveUpper(TotalRange);

			MovieScene->SetPlaybackRange(StartTime, (EndTime - StartTime).Value, false);
			MovieScene->MarkAsChanged();
		}
	}

	return true;
}

void UAutomatedLevelSequenceCapture::SetupFrameRange()
{
	ALevelSequenceActor* Actor = LevelSequenceActor.Get();
	if( Actor )
	{
		ULevelSequence* LevelSequence = Cast<ULevelSequence>( Actor->LevelSequence.TryLoad() );
		if( LevelSequence != nullptr )
		{
			UMovieScene* MovieScene = LevelSequence->GetMovieScene();
			if( MovieScene != nullptr )
			{
				FFrameRate           SourceFrameRate = MovieScene->GetTickResolution();
				TRange<FFrameNumber> SequenceRange   = MovieScene->GetPlaybackRange();

				FFrameNumber PlaybackStartFrame = ConvertFrameTime(MovieScene::DiscreteInclusiveLower(SequenceRange), SourceFrameRate, Settings.FrameRate).CeilToFrame();
				FFrameNumber PlaybackEndFrame   = ConvertFrameTime(MovieScene::DiscreteExclusiveUpper(SequenceRange), SourceFrameRate, Settings.FrameRate).CeilToFrame();

				if( bUseCustomStartFrame )
				{
					PlaybackStartFrame = CustomStartFrame;
				}

				if( !Settings.bUseRelativeFrameNumbers )
				{
				 	// NOTE: The frame number will be an offset from the first frame that we start capturing on, not the frame
				 	// that we start playback at (in the case of WarmUpFrameCount being non-zero).  So we'll cache out frame
				 	// number offset before adjusting for the warm up frames.
				 	this->FrameNumberOffset = PlaybackStartFrame.Value;
				}

				if( bUseCustomEndFrame )
				{
				 	PlaybackEndFrame = CustomEndFrame;
				}

				RemainingWarmUpFrames = FMath::Max( WarmUpFrameCount, 0 );
				if( RemainingWarmUpFrames > 0 )
				{
				 	// We were asked to playback additional frames before we start capturing
				 	PlaybackStartFrame -= RemainingWarmUpFrames;
				}

				// Override the movie scene's playback range
				Actor->SequencePlayer->SetFrameRate(Settings.FrameRate);
				Actor->SequencePlayer->SetFrameRange(PlaybackStartFrame.Value, (PlaybackEndFrame - PlaybackStartFrame).Value);
				Actor->SequencePlayer->JumpToFrame(PlaybackStartFrame.Value);

				Actor->SequencePlayer->SetSnapshotOffsetFrames(WarmUpFrameCount);
			}
		}
	}
}

void UAutomatedLevelSequenceCapture::EnableCinematicMode()
{
	if (!GetSettings().bCinematicMode)
	{
		return;
	}

	// iterate through the controller list and set cinematic mode if necessary
	bool bNeedsCinematicMode = !GetSettings().bAllowMovement || !GetSettings().bAllowTurning || !GetSettings().bShowPlayer || !GetSettings().bShowHUD;
	if (!bNeedsCinematicMode)
	{
		return;
	}

	if (Viewport.IsValid())
	{
		for (FConstPlayerControllerIterator Iterator = Viewport.Pin()->GetClient()->GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PC = Iterator->Get();
			if (PC && PC->IsLocalController())
			{
				PC->SetCinematicMode(true, !GetSettings().bShowPlayer, !GetSettings().bShowHUD, !GetSettings().bAllowMovement, !GetSettings().bAllowTurning);
			}
		}
	}
}

void UAutomatedLevelSequenceCapture::Tick(float DeltaSeconds)
{
	ALevelSequenceActor* Actor = LevelSequenceActor.Get();

	if (!Actor || !Actor->SequencePlayer)
	{
		return;
	}

	// Setup the automated capture
	if (CaptureState == ELevelSequenceCaptureState::Setup)
	{
		SetupFrameRange();

		EnableCinematicMode();
		
		// Bind to the event so we know when to capture a frame
		OnPlayerUpdatedBinding = Actor->SequencePlayer->OnSequenceUpdated().AddUObject( this, &UAutomatedLevelSequenceCapture::SequenceUpdated );

		if (DelayBeforeWarmUp + DelayBeforeShotWarmUp > 0)
		{
			CaptureState = ELevelSequenceCaptureState::DelayBeforeWarmUp;

			Actor->GetWorld()->GetTimerManager().SetTimer(DelayTimer, FTimerDelegate::CreateUObject(this, &UAutomatedLevelSequenceCapture::DelayBeforeWarmupFinished), DelayBeforeWarmUp + DelayBeforeShotWarmUp, false);
		}
		else
		{
			DelayBeforeWarmupFinished();
		}
	}

	// Then we'll just wait a little bit.  We'll delay the specified number of seconds before capturing to allow any
	// textures to stream in or post processing effects to settle.
	if( CaptureState == ELevelSequenceCaptureState::DelayBeforeWarmUp )
	{
		// Do nothing, just hold at the current frame. This assumes that the current frame isn't changing by any other mechanisms.
	}
	else if( CaptureState == ELevelSequenceCaptureState::ReadyToWarmUp )
	{
		Actor->SequencePlayer->SetSnapshotSettings(FLevelSequenceSnapshotSettings(Settings.ZeroPadFrameNumbers, Settings.FrameRate));
		Actor->SequencePlayer->Play();
		// Start warming up
		CaptureState = ELevelSequenceCaptureState::WarmingUp;
	}

	// Count down our warm up frames.
	// The post increment is important - it ensures we capture the very first frame if there are no warm up frames,
	// but correctly skip n frames if there are n warmup frames
	if( CaptureState == ELevelSequenceCaptureState::WarmingUp && RemainingWarmUpFrames-- == 0)
	{
		// Start capturing - this will capture the *next* update from sequencer
		CaptureState = ELevelSequenceCaptureState::FinishedWarmUp;
		UpdateFrameState();
		StartCapture();
	}

	if( bCapturing && !Actor->SequencePlayer->IsPlaying() )
	{
		++ShotIndex;

		FFrameNumber StartTime, EndTime;
		if (SetupShot(StartTime, EndTime))
		{
			UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);

			FFrameNumber StartTimePlayRateSpace = ConvertFrameTime(StartTime, MovieScene->GetTickResolution(), Settings.FrameRate).CeilToFrame();
			FFrameNumber EndTimePlayRateSpace   = ConvertFrameTime(EndTime,   MovieScene->GetTickResolution(), Settings.FrameRate).CeilToFrame();

			Actor->SequencePlayer->SetFrameRange(StartTimePlayRateSpace.Value, (EndTimePlayRateSpace - StartTimePlayRateSpace).Value);
			Actor->SequencePlayer->JumpToFrame(StartTimePlayRateSpace.Value);
			Actor->SequencePlayer->Play();
			CaptureState = ELevelSequenceCaptureState::FinishedWarmUp;
			UpdateFrameState();
		}
		else
		{
			Actor->SequencePlayer->OnSequenceUpdated().Remove( OnPlayerUpdatedBinding );
			FinalizeWhenReady();
		}
	}
}

void UAutomatedLevelSequenceCapture::DelayBeforeWarmupFinished()
{
	StartWarmup();

	// Wait a frame to go by after we've set the fixed time step, so that the animation starts
	// playback at a consistent time
	CaptureState = ELevelSequenceCaptureState::ReadyToWarmUp;
}

void UAutomatedLevelSequenceCapture::PauseFinished()
{
	CaptureState = ELevelSequenceCaptureState::FinishedWarmUp;

	if (CachedPlayRate.IsSet())
	{
		ALevelSequenceActor* Actor = LevelSequenceActor.Get();

		// Force an evaluation to capture this frame
		Actor->SequencePlayer->JumpToFrame(Actor->SequencePlayer->GetCurrentTime().Time);

		// Continue playing forwards
		Actor->SequencePlayer->SetPlayRate(CachedPlayRate.GetValue());
		CachedPlayRate.Reset();
	}
}

void UAutomatedLevelSequenceCapture::SequenceUpdated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime)
{
	if (bCapturing)
	{
		FLevelSequencePlayerSnapshot PreviousState = CachedState;

		UpdateFrameState();

		ALevelSequenceActor* Actor = LevelSequenceActor.Get();
		if (Actor && Actor->SequencePlayer)
		{
			// If this is a new shot, set the state to shot warm up and pause on this frame until warmed up			
			bool bHasMultipleShots = PreviousState.CurrentShotName != PreviousState.MasterName;
			bool bNewShot = bHasMultipleShots && PreviousState.ShotID != CachedState.ShotID;
			
			if (bNewShot && Actor->SequencePlayer->IsPlaying() && DelayBeforeShotWarmUp > 0)
			{
				CaptureState = ELevelSequenceCaptureState::Paused;
				Actor->GetWorld()->GetTimerManager().SetTimer(DelayTimer, FTimerDelegate::CreateUObject(this, &UAutomatedLevelSequenceCapture::PauseFinished), DelayBeforeShotWarmUp, false);
				CachedPlayRate = Actor->SequencePlayer->GetPlayRate();
				Actor->SequencePlayer->SetPlayRate(0.f);
			}
			else if (CaptureState == ELevelSequenceCaptureState::FinishedWarmUp)
			{
				CaptureThisFrame( (CurrentTime - PreviousTime) / Settings.FrameRate);

				bool bOnLastFrame = ( CurrentTime.FrameNumber >= Actor->SequencePlayer->GetStartTime().Time.FrameNumber + Actor->SequencePlayer->GetFrameDuration() - 1 );
				bool bLastShot = NumShots == 0 ? true : ShotIndex == NumShots - 1;

				if ((bOnLastFrame && bLastShot) || bFinalizeWhenReady)
				{
					FinalizeWhenReady();
					Actor->SequencePlayer->OnSequenceUpdated().Remove( OnPlayerUpdatedBinding );
				}
			}
		}
	}
}

void UAutomatedLevelSequenceCapture::UpdateFrameState()
{
	ALevelSequenceActor* Actor = LevelSequenceActor.Get();

	if (Actor && Actor->SequencePlayer)
	{
		Actor->SequencePlayer->TakeFrameSnapshot(CachedState);
	}
}

void UAutomatedLevelSequenceCapture::LoadFromConfig()
{
	UMovieSceneCapture::LoadFromConfig();

	BurnInOptions->LoadConfig();
	BurnInOptions->ResetSettings();
	if (BurnInOptions->Settings)
	{
		BurnInOptions->Settings->LoadConfig();
	}
}

void UAutomatedLevelSequenceCapture::SaveToConfig()
{
	FFrameNumber CurrentStartFrame = CustomStartFrame;
	FFrameNumber CurrentEndFrame = CustomEndFrame;
	bool bRestoreFrameOverrides = RestoreFrameOverrides();

	BurnInOptions->SaveConfig();
	if (BurnInOptions->Settings)
	{
		BurnInOptions->Settings->SaveConfig();
	}

	UMovieSceneCapture::SaveToConfig();

	if (bRestoreFrameOverrides)
	{
		SetFrameOverrides(CurrentStartFrame, CurrentEndFrame);
	}
}

void UAutomatedLevelSequenceCapture::Close()
{
	Super::Close();
			
	RestoreShots();
}

bool UAutomatedLevelSequenceCapture::RestoreFrameOverrides()
{
	bool bAnySet = CachedStartFrame.IsSet() || CachedEndFrame.IsSet() || bCachedUseCustomStartFrame.IsSet() || bCachedUseCustomEndFrame.IsSet();
	if (CachedStartFrame.IsSet())
	{
		CustomStartFrame = CachedStartFrame.GetValue();
		CachedStartFrame.Reset();
	}

	if (CachedEndFrame.IsSet())
	{
		CustomEndFrame = CachedEndFrame.GetValue();
		CachedEndFrame.Reset();
	}

	if (bCachedUseCustomStartFrame.IsSet())
	{
		bUseCustomStartFrame = bCachedUseCustomStartFrame.GetValue();
		bCachedUseCustomStartFrame.Reset();
	}

	if (bCachedUseCustomEndFrame.IsSet())
	{
		bUseCustomEndFrame = bCachedUseCustomEndFrame.GetValue();
		bCachedUseCustomEndFrame.Reset();
	}

	return bAnySet;
}

void UAutomatedLevelSequenceCapture::SetFrameOverrides(FFrameNumber InStartFrame, FFrameNumber InEndFrame)
{
	CachedStartFrame = CustomStartFrame;
	CachedEndFrame = CustomEndFrame;
	bCachedUseCustomStartFrame = bUseCustomStartFrame;
	bCachedUseCustomEndFrame = bUseCustomEndFrame;

	CustomStartFrame = InStartFrame;
	CustomEndFrame = InEndFrame;
	bUseCustomStartFrame = true;
	bUseCustomEndFrame = true;
}

void UAutomatedLevelSequenceCapture::SerializeAdditionalJson(FJsonObject& Object)
{
	TSharedRef<FJsonObject> OptionsContainer = MakeShareable(new FJsonObject);
	if (FJsonObjectConverter::UStructToJsonObject(BurnInOptions->GetClass(), BurnInOptions, OptionsContainer, 0, 0))
	{
		Object.SetField(TEXT("BurnInOptions"), MakeShareable(new FJsonValueObject(OptionsContainer)));
	}

	if (BurnInOptions->Settings)
	{
		TSharedRef<FJsonObject> SettingsDataObject = MakeShareable(new FJsonObject);
		if (FJsonObjectConverter::UStructToJsonObject(BurnInOptions->Settings->GetClass(), BurnInOptions->Settings, SettingsDataObject, 0, 0))
		{
			Object.SetField(TEXT("BurnInOptionsInitSettings"), MakeShareable(new FJsonValueObject(SettingsDataObject)));
		}
	}
}

void UAutomatedLevelSequenceCapture::DeserializeAdditionalJson(const FJsonObject& Object)
{
	if (!BurnInOptions)
	{
		BurnInOptions = NewObject<ULevelSequenceBurnInOptions>(this, "BurnInOptions");
	}

	TSharedPtr<FJsonValue> OptionsContainer = Object.TryGetField(TEXT("BurnInOptions"));
	if (OptionsContainer.IsValid())
	{
		FJsonObjectConverter::JsonAttributesToUStruct(OptionsContainer->AsObject()->Values, BurnInOptions->GetClass(), BurnInOptions, 0, 0);
	}

	BurnInOptions->ResetSettings();
	if (BurnInOptions->Settings)
	{
		TSharedPtr<FJsonValue> SettingsDataObject = Object.TryGetField(TEXT("BurnInOptionsInitSettings"));
		if (SettingsDataObject.IsValid())
		{
			FJsonObjectConverter::JsonAttributesToUStruct(SettingsDataObject->AsObject()->Values, BurnInOptions->Settings->GetClass(), BurnInOptions->Settings, 0, 0);
		}
	}
}

void UAutomatedLevelSequenceCapture::ExportEDL()
{
	if (!bWriteEditDecisionList)
	{
		return;
	}
	
	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return;
	}

	UMovieSceneCinematicShotTrack* ShotTrack = MovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	if (!ShotTrack)
	{
		return;
	}

	FString SaveFilename = 	Settings.OutputDirectory.Path / MovieScene->GetOuter()->GetName();
	int32 HandleFrames = Settings.HandleFrames;

	MovieSceneTranslatorEDL::ExportEDL(MovieScene, Settings.FrameRate, SaveFilename, HandleFrames);
}

void UAutomatedLevelSequenceCapture::ExportFCPXML()
{
	if (!bWriteFinalCutProXML)
	{
		return;
	}

	UMovieScene* MovieScene = GetMovieScene(LevelSequenceActor);
	if (!MovieScene)
	{
		return;
	}

	UMovieSceneCinematicShotTrack* ShotTrack = MovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	if (!ShotTrack)
	{
		return;
	}

	FString SaveFilename = Settings.OutputDirectory.Path / MovieScene->GetOuter()->GetName() + TEXT(".xml");
	FString FilenameFormat = Settings.OutputFormat;
	int32 HandleFrames = Settings.HandleFrames;
	FFrameRate FrameRate = Settings.FrameRate;
	uint32 ResX = Settings.Resolution.ResX;
	uint32 ResY = Settings.Resolution.ResY;

	FFCPXMLExporter *Exporter = new FFCPXMLExporter;

	TSharedRef<FMovieSceneTranslatorContext> ExportContext(new FMovieSceneTranslatorContext);
	ExportContext->Init();

	bool bSuccess = Exporter->Export(MovieScene, FilenameFormat, FrameRate, ResX, ResY, HandleFrames, SaveFilename, ExportContext);

	// Log any messages in context
	MovieSceneToolHelpers::MovieSceneTranslatorLogMessages(Exporter, ExportContext, false);

	delete Exporter;
}


#endif
