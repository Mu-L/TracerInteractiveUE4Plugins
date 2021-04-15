// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTools.h"
#include "SequencerScriptingEditor.h"
#include "MovieSceneCapture.h"
#include "MovieSceneCaptureDialogModule.h"
#include "AutomatedLevelSequenceCapture.h"
#include "MovieSceneTimeHelpers.h"
#include "UObject/Stack.h"
#include "UObject/Package.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "FbxExporter.h"
#include "FbxImporter.h"
#include "MovieSceneToolsUserSettings.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneEventUtils.h"
#include "MovieSceneSequenceEditor.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "MovieSceneCommonHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "ScopedTransaction.h"
#include "Exporters/AnimSeqExportOption.h"

#include "K2Node_CustomEvent.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintActionMenuItem.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "SequencerTools"

bool USequencerToolsFunctionLibrary::RenderMovie(UMovieSceneCapture* InCaptureSettings, FOnRenderMovieStopped OnFinishedCallback)
{
	IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
	
	// Because this comes from the Python/BP layer we need to soft-validate the state before we pass it onto functions that do a assert-based validation.
	if (!InCaptureSettings)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot start Render Sequence to Movie with null capture settings."), ELogVerbosity::Error);
		return false;
	}

	if (IsRenderingMovie())
	{
		FFrame::KismetExecutionMessage(TEXT("Capture already in progress."), ELogVerbosity::Error);
		return false;
	}

	// If they're capturing a level sequence we'll do some additional checking as there are more parameters on the Automated Level Sequence capture.
	UAutomatedLevelSequenceCapture* LevelSequenceCapture = Cast<UAutomatedLevelSequenceCapture>(InCaptureSettings);
	if (LevelSequenceCapture)
	{
		if (!LevelSequenceCapture->LevelSequenceAsset.IsValid())
		{
			// UE_LOG(LogTemp, Warning, TEXT("No Level Sequence Asset specified in UAutomatedLevelSequenceCapture."));
			FFrame::KismetExecutionMessage(TEXT("No Level Sequence Asset specified in UAutomatedLevelSequenceCapture."), ELogVerbosity::Error);
			return false;
		}

		if (!LevelSequenceCapture->bUseCustomStartFrame && !LevelSequenceCapture->bUseCustomEndFrame)
		{
			// If they don't want to use a custom start/end frame we override the default values to be the length of the sequence, as the default is [0,1)
			ULevelSequence* LevelSequence = Cast<ULevelSequence>(LevelSequenceCapture->LevelSequenceAsset.TryLoad());
			if (!LevelSequence)
			{
				const FString ErrorMessage = FString::Printf(TEXT("Specified Level Sequence Asset failed to load. Specified Asset Path: %s"), *LevelSequenceCapture->LevelSequenceAsset.GetAssetPathString());
				FFrame::KismetExecutionMessage(*ErrorMessage, ELogVerbosity::Error);
				return false;
			}

			FFrameRate DisplayRate = LevelSequence->GetMovieScene()->GetDisplayRate();
			FFrameRate TickResolution = LevelSequence->GetMovieScene()->GetTickResolution();
			
			LevelSequenceCapture->Settings.CustomFrameRate = DisplayRate;
			LevelSequenceCapture->Settings.bUseCustomFrameRate = true;
			LevelSequenceCapture->Settings.bUseRelativeFrameNumbers = false;
			TRange<FFrameNumber> Range = LevelSequence->GetMovieScene()->GetPlaybackRange();

			FFrameNumber StartFrame = UE::MovieScene::DiscreteInclusiveLower(Range);
			FFrameNumber EndFrame = UE::MovieScene::DiscreteExclusiveUpper(Range);

			FFrameNumber RoundedStartFrame = FFrameRate::TransformTime(StartFrame, TickResolution, DisplayRate).CeilToFrame();
			FFrameNumber RoundedEndFrame = FFrameRate::TransformTime(EndFrame, TickResolution, DisplayRate).CeilToFrame();

			LevelSequenceCapture->CustomStartFrame = RoundedStartFrame;
			LevelSequenceCapture->CustomEndFrame = RoundedEndFrame;
		}
	}

	auto LocalCaptureStoppedCallback = [OnFinishedCallback](bool bSuccess)
	{
		OnFinishedCallback.ExecuteIfBound(bSuccess);
	};

	MovieSceneCaptureModule.StartCapture(InCaptureSettings);
	MovieSceneCaptureModule.GetCurrentCapture()->CaptureStoppedDelegate.AddLambda(LocalCaptureStoppedCallback);
	return true;
}

void USequencerToolsFunctionLibrary::CancelMovieRender()
{
	IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
	TSharedPtr<FMovieSceneCaptureBase> CurrentCapture = MovieSceneCaptureModule.GetCurrentCapture();
	if (CurrentCapture.IsValid())
	{
		// We just invoke the capture's Cancel function. This will cause a shut-down of the capture (the same as the UI)
		// which will invoke all of the necessary callbacks as well. We don't null out CurrentCapture because that is done
		// as the result of its shutdown callbacks.
		CurrentCapture->Cancel();
	}
}

TArray<FSequencerBoundObjects> USequencerToolsFunctionLibrary::GetBoundObjects(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, const FSequencerScriptingRange& InRange)
{
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;

	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(InWorld, InSequence, Settings, OutActor);

	Player->Initialize(InSequence, InWorld->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *InSequence, *Player);

	// Evaluation needs to occur in order to obtain spawnables
	FFrameRate Resolution = InSequence->GetMovieScene()->GetTickResolution();
	TRange<FFrameNumber> SpecifiedRange = InRange.ToNative(Resolution);
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(SpecifiedRange.GetLowerBoundValue().Value, EUpdatePositionMethod::Play));

	FMovieSceneSequenceID SequenceId = Player->State.FindSequenceId(InSequence);

	TArray<FSequencerBoundObjects> BoundObjects;
	for (FSequencerBindingProxy Binding : InBindings)
	{
		FMovieSceneObjectBindingID ObjectBinding(Binding.BindingID, SequenceId);
		BoundObjects.Add(FSequencerBoundObjects(Binding, Player->GetBoundObjects(ObjectBinding)));
	}

	Player->Stop();
	InWorld->DestroyActor(OutActor);
	return BoundObjects;
}


TArray<FSequencerBoundObjects> USequencerToolsFunctionLibrary::GetObjectBindings(UWorld* InWorld, ULevelSequence* InSequence, const TArray<UObject*>& InObjects, const FSequencerScriptingRange& InRange)
{
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;

	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(InWorld, InSequence, Settings, OutActor);

	Player->Initialize(InSequence, InWorld->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *InSequence, *Player);

	FFrameRate Resolution = InSequence->GetMovieScene()->GetTickResolution();
	TRange<FFrameNumber> SpecifiedRange = InRange.ToNative(Resolution);
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(SpecifiedRange.GetLowerBoundValue().Value, EUpdatePositionMethod::Play));

	TArray<FSequencerBoundObjects> BoundObjects;

	for (UObject* Object : InObjects)
	{
		TArray<FMovieSceneObjectBindingID> ObjectBindings = Player->GetObjectBindings(Object);
		for (FMovieSceneObjectBindingID ObjectBinding : ObjectBindings)
		{
			FSequencerBindingProxy Binding(ObjectBinding.GetGuid(), Player->State.FindSequence(ObjectBinding.GetSequenceID()));
			BoundObjects.Add(FSequencerBoundObjects(Binding, TArray<UObject*>({ Object })));
		}
	}

	Player->Stop();
	InWorld->DestroyActor(OutActor);
	return BoundObjects;
}

bool USequencerToolsFunctionLibrary::ExportFBX(UWorld* World, ULevelSequence* Sequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions, const FString& InFBXFileName)
{
	UnFbx::FFbxExporter* Exporter = UnFbx::FFbxExporter::GetInstance();
	//Show the fbx export dialog options
	Exporter->SetExportOptionsOverride(OverrideOptions);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	TArray<FGuid> Bindings;
	for (const FSequencerBindingProxy& Proxy : InBindings)
	{
		if (Proxy.Sequence == Sequence)
		{
			Bindings.Add(Proxy.BindingID);
		}
	}

	INodeNameAdapter NodeNameAdapter;
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
	Player->Initialize(Sequence, World->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);
	FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
	bool bDidExport = false;
	FMovieSceneSequenceTransform RootToLocalTransform;
	
	{
		FSpawnableRestoreState SpawnableRestoreState(MovieScene);

		if (SpawnableRestoreState.bWasChanged)
		{
			// Evaluate at the beginning of the subscene time to ensure that spawnables are created before export
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()).Value, EUpdatePositionMethod::Play));
		}

		bDidExport = MovieSceneToolHelpers::ExportFBX(World, MovieScene, Player, Bindings, NodeNameAdapter, Template, InFBXFileName, RootToLocalTransform);
	}

	Player->Stop();
	Exporter->SetExportOptionsOverride(nullptr);
	World->DestroyActor(OutActor);
	
	return bDidExport;
}

static USkeletalMeshComponent* GetSkelMeshComponent(IMovieScenePlayer* Player, const FSequencerBindingProxy& Binding)
{
	FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
	for (TWeakObjectPtr<UObject> RuntimeObject : Player->FindBoundObjects(Binding.BindingID, Template))
	{

		if (AActor* Actor = Cast<AActor>(RuntimeObject.Get()))
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
				{
					return SkeletalMeshComp;
				}
			}
		}
		else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RuntimeObject.Get()))
		{
			if (SkeletalMeshComponent->SkeletalMesh)
			{
				return SkeletalMeshComponent;
			}
		}
	}
	return nullptr;
}

bool USequencerToolsFunctionLibrary::ExportAnimSequence(UWorld* World, ULevelSequence*  Sequence,  UAnimSequence* AnimSequence, UAnimSeqExportOption* ExportOptions,const FSequencerBindingProxy& Binding)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (Binding.Sequence != Sequence || !AnimSequence)
	{
		return false;
	}
	INodeNameAdapter NodeNameAdapter;
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
	FMovieSceneSequenceTransform RootToLocalTransform;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
	Player->Initialize(Sequence, World->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);

	bool bResult = false;
	FScopedTransaction ExportAnimSequenceTransaction(NSLOCTEXT("Sequencer", "ExportAnimSequence", "Export Anim Sequence"));
	{
		FSpawnableRestoreState SpawnableRestoreState(MovieScene);
 
		if (SpawnableRestoreState.bWasChanged)
		{
			// Evaluate at the beginning of the subscene time to ensure that spawnables are created before export
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()).Value, EUpdatePositionMethod::Play));
		}
 
		USkeletalMeshComponent* SkeletalMeshComp =  GetSkelMeshComponent(Player, Binding);
		if (SkeletalMeshComp && SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->Skeleton)
		{
			AnimSequence->SetSkeleton(SkeletalMeshComp->SkeletalMesh->Skeleton);
			bResult = MovieSceneToolHelpers::ExportToAnimSequence(AnimSequence,ExportOptions, MovieScene, Player, SkeletalMeshComp, Template, RootToLocalTransform);
		}
	}
	
	Player->Stop();
	World->DestroyActor(OutActor);

	return bResult;
}



TArray<FGuid> AddActors(UWorld* World, UMovieSceneSequence* InSequence, UMovieScene* InMovieScene, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID,const TArray<TWeakObjectPtr<AActor> >& InActors)
{
	TArray<FGuid> PossessableGuids;

	if (InMovieScene->IsReadOnly())
	{
		return PossessableGuids;
	}

	for (TWeakObjectPtr<AActor> WeakActor : InActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			FGuid ExistingGuid = Player->FindObjectId(*Actor, TemplateID);
			if (!ExistingGuid.IsValid())
			{
				InMovieScene->Modify();
				const FGuid PossessableGuid = InMovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
				PossessableGuids.Add(PossessableGuid);
				InSequence->BindPossessableObject(PossessableGuid, *Actor, World);

				//TODO New to figure way to call void FLevelSequenceEditorToolkit::AddDefaultTracksForActor(AActor& Actor, const FGuid Binding)

				if (Actor->IsA<ACameraActor>())
				{
					MovieSceneToolHelpers::CameraAdded(InMovieScene, PossessableGuid, 0);
				}
			}
		}
	}
	return PossessableGuids;
}


void ImportFBXCamera(UnFbx::FFbxImporter* FbxImporter, UWorld* World, UMovieSceneSequence* Sequence, UMovieScene* InMovieScene, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID, TMap<FGuid, FString>& InObjectBindingMap, bool bMatchByNameOnly, bool bCreateCameras)
{
	if (bCreateCameras)
	{
		TArray<FbxCamera*> AllCameras;
		MovieSceneToolHelpers::GetCameras(FbxImporter->Scene->GetRootNode(), AllCameras);

		// Find unmatched cameras
		TArray<FbxCamera*> UnmatchedCameras;
		for (auto Camera : AllCameras)
		{
			FString NodeName = MovieSceneToolHelpers::GetCameraName(Camera);

			bool bMatched = false;
			for (auto InObjectBinding : InObjectBindingMap)
			{
				FString ObjectName = InObjectBinding.Value;
				if (ObjectName == NodeName)
				{
					// Look for a valid bound object, otherwise need to create a new camera and assign this binding to it
					bool bFoundBoundObject = false;
					TArrayView<TWeakObjectPtr<>> BoundObjects = Player->FindBoundObjects(InObjectBinding.Key, TemplateID);
					for (auto BoundObject : BoundObjects)
					{
						if (BoundObject.IsValid())
						{
							bFoundBoundObject = true;
							break;
						}
					}
				}
			}

			if (!bMatched)
			{
				UnmatchedCameras.Add(Camera);
			}
		}

		// If there are new cameras, clear the object binding map so that we're only assigning values to the newly created cameras
		if (UnmatchedCameras.Num() != 0)
		{
			InObjectBindingMap.Reset();
			bMatchByNameOnly = true;
		}

		// Add any unmatched cameras
		for (auto UnmatchedCamera : UnmatchedCameras)
		{
			FString CameraName = MovieSceneToolHelpers::GetCameraName(UnmatchedCamera);

			AActor* NewCamera = nullptr;
			if (UnmatchedCamera->GetApertureMode() == FbxCamera::eFocalLength)
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACineCameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}
			else
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}

			// Copy camera properties before adding default tracks so that initial camera properties match and can be restored after sequencer finishes
			MovieSceneToolHelpers::CopyCameraProperties(UnmatchedCamera, NewCamera);

			TArray<TWeakObjectPtr<AActor> > NewCameras;
			NewCameras.Add(NewCamera);
			TArray<FGuid> NewCameraGuids = AddActors(World, Sequence,InMovieScene, Player, TemplateID,NewCameras);

			if (NewCameraGuids.Num())
			{
				InObjectBindingMap.Add(NewCameraGuids[0]);
				InObjectBindingMap[NewCameraGuids[0]] = CameraName;
			}
		}
	}
	//everything created now import it in.
	MovieSceneToolHelpers::ImportFBXCameraToExisting(FbxImporter, Sequence, Player, TemplateID, InObjectBindingMap, bMatchByNameOnly, true);
}

bool USequencerToolsFunctionLibrary::ImportFBX(UWorld* World, ULevelSequence* Sequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* ImportFBXSettings, const FString&  ImportFilename)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return false;
	}

	TMap<FGuid, FString> ObjectBindingMap;
	for (const FSequencerBindingProxy& Binding : InBindings)
	{
		FString Name = MovieScene->GetObjectDisplayName(Binding.BindingID).ToString();
		ObjectBindingMap.Add(Binding.BindingID, Name);
	}

	FFBXInOutParameters InOutParams;
	if (!MovieSceneToolHelpers::ReadyFBXForImport(ImportFilename, ImportFBXSettings, InOutParams))
	{
		return false;
	}

	const bool bMatchByNameOnly = ImportFBXSettings->bMatchByNameOnly;
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
	Player->Initialize(Sequence, World->GetLevel(0), Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);

	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();
	
	bool bResult = false;
	FScopedTransaction ImportFBXTransaction(NSLOCTEXT("Sequencer", "ImportFBX", "Import FBX"));
	{
		FSpawnableRestoreState SpawnableRestoreState(MovieScene);
 
		if (SpawnableRestoreState.bWasChanged)
		{
			// Evaluate at the beginning of the subscene time to ensure that spawnables are created before export
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()).Value, EUpdatePositionMethod::Play));
		}

		ImportFBXCamera(FbxImporter, World, Sequence, MovieScene, Player, MovieSceneSequenceID::Root, ObjectBindingMap, bMatchByNameOnly, ImportFBXSettings->bCreateCameras);

		bResult = MovieSceneToolHelpers::ImportFBXIfReady(World, Sequence, Player, MovieSceneSequenceID::Root, ObjectBindingMap, ImportFBXSettings, InOutParams);
	}
	
	Player->Stop();
	World->DestroyActor(OutActor);
	return bResult;
}

bool USequencerToolsFunctionLibrary::ImportFBXToControlRig(UWorld* World, ULevelSequence* Sequence, const FString& ControlRigTrackName, const TArray<FString>& ControlRigNames,
	UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings,
	const FString& ImportFilename)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return false;
	}

	bool bValid = false;

	const TArray<FMovieSceneBinding>& Bindings =  MovieScene->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		if (Binding.GetName() == ControlRigTrackName)
		{
			
			ALevelSequenceActor* OutActor;
			FMovieSceneSequencePlaybackSettings Settings;
			FLevelSequenceCameraSettings CameraSettings;
			ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
			Player->Initialize(Sequence, World->GetLevel(0), Settings, CameraSettings);
			Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);

			const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
			TArray<FName> SelectedControls;
			for (UMovieSceneTrack* Track : Tracks)
			{
				INodeAndChannelMappings* ChannelMapping = Cast<INodeAndChannelMappings>(Track); 
				if (ChannelMapping)
				{
					TArray<FFBXNodeAndChannels>* NodeAndChannels = ChannelMapping->GetNodeAndChannelMappings();
					//use passed in controls for selected, actually selected controls should almost be empty anyway since we just loaded/set everything up.
					for (const FString& StringName : ControlRigNames)
					{
						FName Name(*StringName);
						SelectedControls.Add(Name);
					}

					bValid = MovieSceneToolHelpers::ImportFBXIntoControlRigChannels(MovieScene,ImportFilename, ImportFBXControlRigSettings,
					NodeAndChannels, SelectedControls, MovieScene->GetTickResolution());

					if (NodeAndChannels)
					{
						delete NodeAndChannels;
					}
				}
			}
			return bValid;
		}
	}
	
	return false;
	

}


FMovieSceneEvent USequencerToolsFunctionLibrary::CreateEvent(UMovieSceneSequence* InSequence, UMovieSceneEventSectionBase* InSection, const FSequencerQuickBindingResult& InEndpoint, const TArray<FString>& InPayload)
{
	FMovieSceneEvent Event;

	if (InEndpoint.EventEndpoint == nullptr)
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid endpoint, event will not be initialized"), ELogVerbosity::Warning);
		return Event;
	}

	UMovieScene* MovieScene = InSequence->GetMovieScene();
	FGuid ObjectBindingID;
	MovieScene->FindTrackBinding(*InSection->GetTypedOuter<UMovieSceneTrack>(), ObjectBindingID);
	UClass* BoundObjectPinClass = nullptr;
	if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindingID))
	{
		BoundObjectPinClass = const_cast<UClass*>(Possessable->GetPossessedObjectClass());
	}
	else if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectBindingID))
	{
		BoundObjectPinClass = Spawnable->GetObjectTemplate()->GetClass();
	}

	InSection->Modify();
	FMovieSceneEventUtils::BindEventSectionToBlueprint(InSection, InEndpoint.EventEndpoint->GetBlueprint());

	UEdGraphPin* BoundObjectPin = FMovieSceneEventUtils::FindBoundObjectPin(InEndpoint.EventEndpoint, BoundObjectPinClass);
	FMovieSceneEventUtils::SetEndpoint(&Event, InSection, InEndpoint.EventEndpoint, BoundObjectPin);

	if (InEndpoint.PayloadNames.Num() != InPayload.Num())
	{
		const FString Message = FString::Printf(TEXT("Wrong number of payload values, expecting %i got %i"), InEndpoint.PayloadNames.Num(), InPayload.Num());
		FFrame::KismetExecutionMessage(*Message, ELogVerbosity::Warning);
		return Event;
	}

	for (int32 Index = 0; Index < InEndpoint.PayloadNames.Num(); Index++)
	{
		const FName PayloadName = FName(InEndpoint.PayloadNames[Index]);
		if (!Event.PayloadVariables.Contains(PayloadName))
		{
			Event.PayloadVariables.Add(PayloadName);
			Event.PayloadVariables[PayloadName].Value = InPayload[Index];
		}
	}

	return Event;
}


bool USequencerToolsFunctionLibrary::IsEventEndpointValid(const FSequencerQuickBindingResult& InEndpoint)
{
	return InEndpoint.EventEndpoint != nullptr;
}


FSequencerQuickBindingResult USequencerToolsFunctionLibrary::CreateQuickBinding(UMovieSceneSequence* InSequence, UObject* InObject, const FString& InFunctionName, bool bCallInEditor)
{
	FSequencerQuickBindingResult Result;

	FMovieSceneSequenceEditor* SequenceEditor = FMovieSceneSequenceEditor::Find(InSequence);
	if (!SequenceEditor)
	{
		return Result;
	}

	UBlueprint* Blueprint = SequenceEditor->GetOrCreateDirectorBlueprint(InSequence);
	if (!Blueprint)
	{
		return Result;
	}

	UMovieScene* MovieScene = InSequence->GetMovieScene();
	
	FMovieSceneEventEndpointParameters Params;
	Params.SanitizedObjectName = InObject->GetName();
	Params.SanitizedEventName = InFunctionName;
	Params.BoundObjectPinClass = InObject->GetClass();
	UFunction* Function = InObject->GetClass()->FindFunctionByName(FName(InFunctionName));
	if (Function == nullptr)
	{
		const FString Message = FString::Printf(TEXT("Cannot find function %s in class %s"), *(InFunctionName), *(InObject->GetClass()->GetName()));
		FFrame::KismetExecutionMessage(*Message, ELogVerbosity::Warning);
		return Result;
	}

	UBlueprintFunctionNodeSpawner* BlueprintFunctionNodeSpawner = UBlueprintFunctionNodeSpawner::Create(Function);
	FBlueprintActionMenuItem Action(BlueprintFunctionNodeSpawner);

	UK2Node_CustomEvent* NewEventEndpoint = FMovieSceneEventUtils::CreateUserFacingEvent(Blueprint, Params);
	NewEventEndpoint->bCallInEditor = bCallInEditor;
	Result.EventEndpoint = NewEventEndpoint;

	UEdGraphPin* ThenPin = NewEventEndpoint->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* BoundObjectPin = FMovieSceneEventUtils::FindBoundObjectPin(NewEventEndpoint, Params.BoundObjectPinClass);

	FVector2D NodePosition(NewEventEndpoint->NodePosX + 400.f, NewEventEndpoint->NodePosY);
	UEdGraphNode* NewNode = Action.PerformAction(NewEventEndpoint->GetGraph(), BoundObjectPin ? BoundObjectPin : ThenPin, NodePosition);

	if (NewNode == nullptr)
	{
		const FString Message = FString::Printf(TEXT("Failed creating blueprint event node for function %s"), *InFunctionName);
		FFrame::KismetExecutionMessage(*Message, ELogVerbosity::Warning);
		return Result;
	}

	// Link execution pins
	UEdGraphPin* ExecPin = NewNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (ensure(ThenPin && ExecPin))
	{
		ThenPin->MakeLinkTo(ExecPin);
	}

	// Link payload parameters' pins
	UK2Node_EditablePinBase* EditableNode = Cast<UK2Node_EditablePinBase>(NewEventEndpoint);
	if (EditableNode)
	{
		for (UEdGraphPin* PayloadPin : NewNode->Pins)
		{
			if (PayloadPin != BoundObjectPin && PayloadPin->Direction == EGPD_Input && PayloadPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && PayloadPin->LinkedTo.Num() == 0)
			{
				Result.PayloadNames.Add(PayloadPin->PinName.ToString());

				UEdGraphPin* NewPin = EditableNode->CreateUserDefinedPin(PayloadPin->PinName, PayloadPin->PinType, EGPD_Output);
				if (NewNode != NewEventEndpoint && NewPin)
				{
					NewPin->MakeLinkTo(PayloadPin);
				}
			}
		}
	}

	return Result;
}


#undef LOCTEXT_NAMESPACE // "SequencerTools"