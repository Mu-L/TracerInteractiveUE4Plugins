// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerEdMode.h"
#include "EditorViewportClient.h"
#include "Curves/KeyHandle.h"
#include "ISequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "Sequencer.h"
#include "Framework/Application/SlateApplication.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "SequencerCommonHelpers.h"
#include "MovieSceneHitProxy.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sections/MovieSceneAudioSection.h"
#include "SubtitleManager.h"
#include "SequencerMeshTrail.h"
#include "SequencerKeyActor.h"
#include "EditorWorldExtension.h"
#include "ViewportWorldInteraction.h"
#include "SSequencer.h"
#include "Evaluation/MovieScene3DTransformTemplate.h"
#include "MovieSceneToolHelpers.h"

const FEditorModeID FSequencerEdMode::EM_SequencerMode(TEXT("EM_SequencerMode"));

static TAutoConsoleVariable<bool> CVarDrawMeshTrails(
	TEXT("Sequencer.DrawMeshTrails"),
	true,
	TEXT("Toggle to show or hide Level Sequencer VR Editor trails"));

FSequencerEdMode::FSequencerEdMode()
{
	FSequencerEdModeTool* SequencerEdModeTool = new FSequencerEdModeTool(this);

	Tools.Add( SequencerEdModeTool );
	SetCurrentTool( SequencerEdModeTool );

	bDrawMeshTrails = CVarDrawMeshTrails->GetBool();
	CVarDrawMeshTrails.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([this](IConsoleVariable* Var) 
	{
		bDrawMeshTrails = Var->GetBool();
	}));

	AudioTexture = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent"));
	check(AudioTexture);
}

FSequencerEdMode::~FSequencerEdMode()
{
	CVarDrawMeshTrails.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
}

void FSequencerEdMode::Enter()
{
	FEdMode::Enter();
}

void FSequencerEdMode::Exit()
{
	CleanUpMeshTrails();

	Sequencers.Reset();

	FEdMode::Exit();
}

bool FSequencerEdMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	// Compatible with all modes so that we can take over with the sequencer hotkeys
	return true;
}

bool FSequencerEdMode::InputKey( FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event )
{
	TSharedPtr<FSequencer> ActiveSequencer;

	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		ActiveSequencer = WeakSequencer.Pin();
		if (ActiveSequencer.IsValid())
		{
			break;
		}
	}

	if (ActiveSequencer.IsValid() && Event != IE_Released)
	{
		FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();

		if (ActiveSequencer->GetCommandBindings(ESequencerCommandBindings::Shared).Get()->ProcessCommandBindings(Key, KeyState, (Event == IE_Repeat) ))
		{
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

void FSequencerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

#if WITH_EDITORONLY_DATA
	if (PDI)
	{
		DrawAudioTracks(PDI);
	}

	// Draw spline trails using the PDI
	if (View->Family->EngineShowFlags.Splines)
	{
		DrawTracks3D(PDI);
	}
	// Draw mesh trails (doesn't use the PDI)
	else if (bDrawMeshTrails)
	{
		PDI = nullptr;
		DrawTracks3D(PDI);
	}
#endif
}

void FSequencerEdMode::DrawHUD(FEditorViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient,Viewport,View,Canvas);

	if( ViewportClient->AllowsCinematicControl() )
	{
		// Get the size of the viewport
		const int32 SizeX = Viewport->GetSizeXY().X;
		const int32 SizeY = Viewport->GetSizeXY().Y;

		// Draw subtitles (toggle is handled internally)
		FVector2D MinPos(0.f, 0.f);
		FVector2D MaxPos(1.f, .9f);
		FIntRect SubtitleRegion(FMath::TruncToInt(SizeX * MinPos.X), FMath::TruncToInt(SizeY * MinPos.Y), FMath::TruncToInt(SizeX * MaxPos.X), FMath::TruncToInt(SizeY * MaxPos.Y));
		FSubtitleManager::GetSubtitleManager()->DisplaySubtitles( Canvas, SubtitleRegion, ViewportClient->GetWorld()->GetAudioTimeSeconds() );
	}
}

void FSequencerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FMeshTrailData& MeshTrail : MeshTrails)
	{
		Collector.AddReferencedObject(MeshTrail.Track);
		Collector.AddReferencedObject(MeshTrail.Trail);
	}
}

void FSequencerEdMode::OnKeySelected(FViewport* Viewport, HMovieSceneKeyProxy* KeyProxy)
{
	if (!KeyProxy)
	{
		return;
	}

	const bool bToggleSelection = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	const bool bAddToSelection = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);

	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		bool bChangedSelection = false;

		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		if (Sequencer.IsValid())
		{
			Sequencer->SetLocalTimeDirectly(KeyProxy->Key.Time);

			FSequencerSelection& Selection = Sequencer->GetSelection();
			if (!bAddToSelection && !bToggleSelection)
			{
				if (!bChangedSelection)
				{
					Sequencer->GetSelection().SuspendBroadcast();
					bChangedSelection = true;
				}

				Sequencer->GetSelection().EmptySelectedKeys();
			}

			for (const FTrajectoryKey::FData KeyData : KeyProxy->Key.KeyData)
			{
				UMovieSceneSection* Section = KeyData.Section.Get();
				TOptional<FSectionHandle> SectionHandle = Sequencer->GetNodeTree()->GetSectionHandle(Section);
				if (SectionHandle && KeyData.KeyHandle.IsSet())
				{
					TArray<TSharedRef<FSequencerSectionKeyAreaNode>> KeyAreaNodes;
					SectionHandle->GetTrackNode()->GetChildKeyAreaNodesRecursively(KeyAreaNodes);

					for (TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode : KeyAreaNodes)
					{
						TSharedPtr<IKeyArea> KeyArea = KeyAreaNode->GetKeyArea(Section);
						if (KeyArea.IsValid() && KeyArea->GetName() == KeyData.ChannelName)
						{
							if (!bChangedSelection)
							{
								Sequencer->GetSelection().SuspendBroadcast();
								bChangedSelection = true;
							}

							Sequencer->SelectKey(Section, KeyArea, KeyData.KeyHandle.GetValue(), bToggleSelection);
							break;
						}
					}
				}
			}
			if (bChangedSelection)
			{
				Sequencer->GetSelection().ResumeBroadcast();
				Sequencer->GetSelection().GetOnKeySelectionChanged().Broadcast();
				Sequencer->GetSelection().GetOnOutlinerNodeSelectionChangedObjectGuids().Broadcast();
			}
		}
	}
}

void FSequencerEdMode::DrawMeshTransformTrailFromKey(const class ASequencerKeyActor* KeyActor)
{
	ASequencerMeshTrail* Trail = Cast<ASequencerMeshTrail>(KeyActor->GetOwner());
	if(Trail != nullptr)
	{
		FMeshTrailData* TrailPtr = MeshTrails.FindByPredicate([Trail](const FMeshTrailData InTrail)
		{
			return Trail == InTrail.Trail;
		});
		if(TrailPtr != nullptr)
		{
			// From the key, get the mesh trail, and then the track associated with that mesh trail
			UMovieScene3DTransformTrack* Track = TrailPtr->Track;
			// Draw a mesh trail for the key's associated actor
			TArray<TWeakObjectPtr<UObject>> KeyObjects;
			AActor* TrailActor = KeyActor->GetAssociatedActor();
			KeyObjects.Add(TrailActor);
			FPrimitiveDrawInterface* PDI = nullptr;

			for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
			{
				TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
				if (Sequencer.IsValid())
				{
					DrawTransformTrack(Sequencer, PDI, Track, KeyObjects, true);
				}
			}
		}
	}
}

void FSequencerEdMode::CleanUpMeshTrails()
{
	// Clean up any existing trails
	for (FMeshTrailData& MeshTrail : MeshTrails)
	{
		if (MeshTrail.Trail)
		{
			MeshTrail.Trail->Cleanup();
		}
	}
	MeshTrails.Empty();
}

namespace SequencerEdMode_Draw3D
{
static const FColor	KeySelectedColor(255,128,0);
static const float	DrawTrackTimeRes = 0.1f;
static const float	CurveHandleScale = 0.5f;
}

void FSequencerEdMode::GetParents(TArray<const UObject *>& Parents, const UObject* InObject)
{
	const AActor* Actor = Cast<AActor>(InObject);
	if (Actor)
	{
		Parents.Emplace(Actor);
		const AActor* ParentActor = Actor->GetAttachParentActor();
		if (ParentActor)
		{
			GetParents(Parents, ParentActor);
		}
	}
}
/** This is not that scalable moving forward with stuff like the control rig , need a better caching solution there */
bool FSequencerEdMode::GetParentTM(FTransform& CurrentRefTM, const TSharedPtr<FSequencer>& Sequencer, UObject* ParentObject, FFrameTime KeyTime)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return false;
	}

	FGuid ObjectBinding = Sequencer->FindCachedObjectId(*ParentObject, Sequencer->GetFocusedTemplateID());
	if (!ObjectBinding.IsValid())
	{
		return false;
	}

	const FMovieSceneBinding* Binding = Sequence->GetMovieScene()->FindBinding(ObjectBinding);
	if (!Binding)
	{
		return false;
	}
	//TODO this doesn't handle blended sections at all
	for (const UMovieSceneTrack* Track : Binding->GetTracks())
	{
		const UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
		if (!TransformTrack)
		{
			continue;
		}

		//we used to loop between sections here and only evaluate if we are in a section, this will give us wrong transfroms though
		//when in between or outside of the section range. We still want to evaluate, though it is heavy.

		FMovieSceneEvaluationTrack* EvalTrack = MovieSceneToolHelpers::GetEvaluationTrack(Sequencer.Get(), TransformTrack->GetSignature());
		if (EvalTrack)
		{
			FVector ParentKeyPos;
			FRotator ParentKeyRot;
			GetLocationAtTime(EvalTrack, ParentObject, KeyTime, ParentKeyPos, ParentKeyRot, Sequencer);
			CurrentRefTM = FTransform(ParentKeyRot, ParentKeyPos);
			return true;
		}
		
	}

	return false;
}

FTransform FSequencerEdMode::GetRefFrameFromParents(const TSharedPtr<FSequencer>& Sequencer, const TArray<const UObject *>& Parents, FFrameTime KeyTime)
{
	FTransform RefTM = FTransform::Identity;
	FTransform ParentRefTM = FTransform::Identity;

	for (const UObject* Object : Parents)
	{
		const AActor* Actor = Cast<AActor>(Object);
		if (Actor != nullptr)
		{
			if (Actor->GetRootComponent() != nullptr && Actor->GetRootComponent()->GetAttachParent() != nullptr)
			{
				//Always get local ref tm since we don't know which parent is in the sequencer or not.
				if (!GetParentTM(ParentRefTM, Sequencer, Actor->GetRootComponent()->GetAttachParent()->GetOwner(), KeyTime))
				{
					AActor *Parent = Actor->GetRootComponent()->GetAttachParent()->GetOwner();
					if (Parent && Parent->GetRootComponent())
					{
						ParentRefTM = Parent->GetRootComponent()->GetRelativeTransform();
					}
					else
					{
						continue;
					}
				}
				RefTM = ParentRefTM * RefTM;
			}
		}
		else
		{
			const USceneComponent* SceneComponent = Cast<USceneComponent>(Object);
			FTransform CurrentRefTM = FTransform::Identity;
			UObject* ParentObject = SceneComponent->GetAttachParent() == SceneComponent->GetOwner()->GetRootComponent() ? static_cast<UObject*>(SceneComponent->GetOwner()) : SceneComponent->GetAttachParent();

			if (SceneComponent->GetAttachParent() != nullptr)
			{
				if (!GetParentTM(CurrentRefTM, Sequencer, ParentObject, KeyTime))
				{
					CurrentRefTM = RefTM * SceneComponent->GetAttachParent()->GetRelativeTransform();
				}
			}
			RefTM = CurrentRefTM * RefTM;
		}
	}
	return RefTM;
}

void FSequencerEdMode::GetLocationAtTime(FMovieSceneEvaluationTrack* Track, UObject* Object, FFrameTime KeyTime, FVector& KeyPos, FRotator& KeyRot, const TSharedPtr<FSequencer>& Sequencer)
{
	FMovieSceneInterrogationData InterrogationData;
	Sequencer->GetEvaluationTemplate().CopyActuators(InterrogationData.GetAccumulator());

	FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, Sequencer->GetFocusedTickResolution()));
	Track->Interrogate(Context, InterrogationData, Object);

	for (const FTransformData& Transform : InterrogationData.Iterate<FTransformData>(UMovieScene3DTransformSection::GetInterrogationKey()))
	{
		KeyPos = Transform.Translation;
		KeyRot = Transform.Rotation;
		break;
	}
}

void FSequencerEdMode::DrawTransformTrack(const TSharedPtr<FSequencer>& Sequencer, FPrimitiveDrawInterface* PDI,
											UMovieScene3DTransformTrack* TransformTrack, TArrayView<const TWeakObjectPtr<>> BoundObjects, const bool bIsSelected)
{
	bool bHitTesting = true;
	if( PDI != nullptr )
	{
		bHitTesting = PDI->IsHitTesting();
	}
	
	ASequencerMeshTrail* TrailActor = nullptr;
	// Get the Trail Actor associated with this track if we are drawing mesh trails
	if (bDrawMeshTrails)
	{
		FMeshTrailData* TrailPtr = MeshTrails.FindByPredicate([TransformTrack](const FMeshTrailData InTrail)
		{
			return InTrail.Track == TransformTrack;
		});
		if (TrailPtr != nullptr)
		{
			TrailActor = TrailPtr->Trail;
		}
	}

	bool bShowTrajectory = TransformTrack->GetAllSections().ContainsByPredicate(
		[bIsSelected](UMovieSceneSection* Section)
		{
			UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
			if (TransformSection)
			{
				switch (TransformSection->GetShow3DTrajectory())
				{
				case EShow3DTrajectory::EST_Always:				return true;
				case EShow3DTrajectory::EST_Never:				return false;
				case EShow3DTrajectory::EST_OnlyWhenSelected:	return bIsSelected;
				}
			}
			return false;
		}
	);
	
	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	FMovieSceneEvaluationTemplate* Template = Sequencer->GetEvaluationTemplate().FindTemplate(Sequencer->GetFocusedTemplateID());
	if (!bShowTrajectory || !Template || !TransformTrack->GetAllSections().ContainsByPredicate([](UMovieSceneSection* In){ return In->IsActive(); }))
	{
		return;
	}

	FLinearColor TrackColor = TransformTrack->GetColorTint();
	// Draw one line per-track (should only really ever be one)
	if (FMovieSceneEvaluationTrack * EvalTrack = MovieSceneToolHelpers::GetEvaluationTrack(Sequencer.Get(), TransformTrack->GetSignature()))
	{
		TArray<FTrajectoryKey> TrajectoryKeys = TransformTrack->GetTrajectoryData(Sequencer->GetLocalTime().Time.FrameNumber, Sequencer->GetSequencerSettings()->GetTrajectoryPathCap());
		for (TWeakObjectPtr<> WeakBinding : BoundObjects)
		{
			UObject* BoundObject = WeakBinding.Get();
			if (!BoundObject)
			{
				continue;
			}
			TArray<const UObject *> Parents;
			GetParents(Parents, BoundObject);

			FVector OldKeyPos(0);
 			FFrameTime OldKeyTime = 0;
			int KeyTimeIndex = 0;
			FTransform OldPosRefTM;;
			FVector OldPos_G;
			//We cache the key times and positions so we don't have to calculate it twice.
			//In particularly in same cases calling GetRefFrame may be heavy.
			//We cache time also so we get cache coherence when iterating over them later.
			struct FKeyPositionRotation
			{
				FVector Position;
				FRotator Rotation;
				FVector WorldPosition;
				FTrajectoryKey TrajectoryKey;

				FKeyPositionRotation(const FTrajectoryKey &InTrajKey, const FVector &InPos, const FRotator &InRot, const FVector &InWorldPos) : 
					 Position(InPos), Rotation(InRot), WorldPosition(InWorldPos), TrajectoryKey(InTrajKey) {}
			};
			TArray<FKeyPositionRotation> KeyPosRots;
			KeyPosRots.Reserve(TrajectoryKeys.Num());
			for (const FTrajectoryKey& NewTrajectoryKey : TrajectoryKeys)
			{
				if (NewTrajectoryKey.KeyData.Num() == 0)
				{
					continue;
				}

				FFrameTime NewKeyTime = NewTrajectoryKey.Time;

				FVector NewKeyPos(0);
				FRotator NewKeyRot(0,0,0);

				GetLocationAtTime(EvalTrack, BoundObject, NewKeyTime, NewKeyPos, NewKeyRot, Sequencer);
				FTransform NewPosRefTM = GetRefFrameFromParents(Sequencer, Parents, NewKeyTime);
				FVector NewKeyPos_G = NewPosRefTM.TransformPosition(NewKeyPos);
				FKeyPositionRotation KeyPosRot(NewTrajectoryKey,NewKeyPos, NewKeyRot, NewKeyPos_G);
				KeyPosRots.Push(KeyPosRot);
				// If not the first keypoint, draw a line to the last keypoint.
				if(KeyTimeIndex > 0)
				{
					int32 NumSteps = FMath::CeilToInt((TickResolution.AsSeconds(NewKeyTime) - TickResolution.AsSeconds(OldKeyTime)) / SequencerEdMode_Draw3D::DrawTrackTimeRes);
					// Limit the number of steps to prevent a rendering performance hit
					NumSteps = FMath::Min( 100, NumSteps );
					FFrameTime DrawSubstep = NumSteps == 0 ? 0 : (NewKeyTime - OldKeyTime)*(1.f/NumSteps);
					// Find position on first keyframe.
					FFrameTime OldTime = OldKeyTime;
					FVector OldPos(0);
					FRotator OldRot(0,0,0);
					GetLocationAtTime(EvalTrack, BoundObject, OldKeyTime, OldPos, OldRot, Sequencer);

					const bool bIsConstantKey = NewTrajectoryKey.Is(ERichCurveInterpMode::RCIM_Constant);
					// For constant interpolation - don't draw ticks - just draw dotted line.
					if (bIsConstantKey)
					{
						if(PDI != nullptr)
						{
							DrawDashedLine(PDI, OldPos_G, NewKeyPos_G, TrackColor, 20, SDPG_Foreground);
						}
					}
					else
					{
						// Then draw a line for each substep.
						for (int32 j=1; j<NumSteps+1; j++)
						{
							FFrameTime NewTime = OldKeyTime + DrawSubstep*j;

							FVector NewPos(0);
							FRotator NewRot(0,0,0);
							GetLocationAtTime(EvalTrack, BoundObject, NewTime, NewPos, NewRot, Sequencer);

							FTransform RefTM = GetRefFrameFromParents(Sequencer, Parents, NewTime);

							FVector NewPos_G = RefTM.TransformPosition(NewPos);
							if (PDI != nullptr)
							{
								PDI->DrawLine(OldPos_G, NewPos_G, TrackColor, SDPG_Foreground);
							}
							// Drawing frames
							// Don't draw point for last one - its the keypoint drawn above.
							if (j != NumSteps)
							{
								if (PDI != nullptr)
								{
									PDI->DrawPoint(NewPos_G, TrackColor, 3.f, SDPG_Foreground);
								}
								else if (TrailActor != nullptr)
								{
									TrailActor->AddFrameMeshComponent(NewTime / TickResolution, FTransform(NewRot, NewPos, FVector(3.0f)));
								}
							}
							OldTime = NewTime;
							OldPos_G = NewPos_G;
						}
					}
				}
				OldPosRefTM = NewPosRefTM;
				OldPos_G = NewKeyPos_G;
				OldKeyTime = NewKeyTime;
				OldKeyPos = NewKeyPos;
				++KeyTimeIndex;
			}

			// Draw keypoints on top of curve
			FColor KeyColor = TrackColor.ToFColor(true);
			for (const FKeyPositionRotation& KeyPosRot: KeyPosRots)
			{
				if (bHitTesting && PDI) 
				{
					PDI->SetHitProxy(new HMovieSceneKeyProxy(TransformTrack, KeyPosRot.TrajectoryKey));
				}

				// Drawing keys
				if (PDI != nullptr)
				{
					PDI->DrawPoint(KeyPosRot.WorldPosition, KeyColor, 6.f, SDPG_Foreground);
				}
				else if (TrailActor != nullptr)
				{
					TArray<UMovieScene3DTransformSection*> AllSections;
					for (const FTrajectoryKey::FData& Value : KeyPosRot.TrajectoryKey.KeyData)
					{
						if (UMovieScene3DTransformSection* Section = Value.Section.Get())
						{
							AllSections.AddUnique(Section);
						}
					}

					for (UMovieScene3DTransformSection* Section : AllSections)
					{
						TrailActor->AddKeyMeshActor(KeyPosRot.TrajectoryKey.Time / TickResolution, FTransform(KeyPosRot.Rotation, KeyPosRot.Position, FVector(3.0f)), Section);
					}
				}

				if (bHitTesting && PDI) 
				{
					PDI->SetHitProxy(nullptr);
				}
			}
		}
	}
}


void FSequencerEdMode::DrawTracks3D(FPrimitiveDrawInterface* PDI)
{
	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			continue;
		}

		UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
		if (!Sequence)
		{
			continue;
		}

		// Gather a map of object bindings to their implict selection state
		TMap<const FMovieSceneBinding*, bool> ObjectBindingNodesSelectionMap;

		const FSequencerSelection& Selection = Sequencer->GetSelection();
		const TSharedRef<FSequencerNodeTree>& NodeTree  = Sequencer->GetNodeTree();
		for (const FMovieSceneBinding& Binding : Sequence->GetMovieScene()->GetBindings())
		{
			TSharedPtr<FSequencerObjectBindingNode> ObjectBindingNode = NodeTree->FindObjectBindingNode(Binding.GetObjectGuid());
			if (!ObjectBindingNode.IsValid())
			{
				continue;
			}

			bool bSelected = false;
			auto Traverse_IsSelected = [&Selection, &bSelected](FSequencerDisplayNode& InNode)
			{
				TSharedRef<FSequencerDisplayNode> Shared = InNode.AsShared();
				if (Selection.IsSelected(Shared) || Selection.NodeHasSelectedKeysOrSections(Shared))
				{
					bSelected = true;
					// Stop traversing
					return false;
				}

				return true;
			};

			ObjectBindingNode->Traverse_ParentFirst(Traverse_IsSelected, true);

			// If one of our parent is selected, we're considered selected
			TSharedPtr<FSequencerDisplayNode> ParentNode = ObjectBindingNode->GetParent();
			while (!bSelected && ParentNode.IsValid())
			{
				if (Selection.IsSelected(ParentNode.ToSharedRef()) || Selection.NodeHasSelectedKeysOrSections(ParentNode.ToSharedRef()))
				{
					bSelected = true;
				}

				ParentNode = ParentNode->GetParent();
			}

			ObjectBindingNodesSelectionMap.Add(&Binding, bSelected);
		}

		// Gather up the transform track nodes from the object binding nodes
		for (TTuple<const FMovieSceneBinding*, bool>& Pair : ObjectBindingNodesSelectionMap)
		{
			for (UMovieSceneTrack* Track : Pair.Key->GetTracks())
			{
				UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
				if (!TransformTrack)
				{
					continue;
				}

				// Ensure that we've got a mesh trail for this track
				if (bDrawMeshTrails)
				{
					const bool bHasMeshTrail = Algo::FindBy(MeshTrails, TransformTrack, &FMeshTrailData::Track) != nullptr;
					if (!bHasMeshTrail)
					{
						UViewportWorldInteraction* WorldInteraction = Cast<UViewportWorldInteraction>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UViewportWorldInteraction::StaticClass() ) );
						if( WorldInteraction != nullptr )
						{
							ASequencerMeshTrail* TrailActor = WorldInteraction->SpawnTransientSceneActor<ASequencerMeshTrail>(TEXT("SequencerMeshTrail"), true);
							FMeshTrailData MeshTrail = FMeshTrailData(TransformTrack, TrailActor);
							MeshTrails.Add(MeshTrail);
						}
					}
				}

				const bool bIsSelected = Pair.Value;
				DrawTransformTrack(Sequencer, PDI, TransformTrack, Sequencer->FindObjectsInCurrentSequence(Pair.Key->GetObjectGuid()), bIsSelected);
			}
		}
	}
}

void FSequencerEdMode::DrawAudioTracks(FPrimitiveDrawInterface* PDI)
{
	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			continue;
		}

		UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
		if (!Sequence)
		{
			continue;
		}

		FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();

		const FSequencerSelection& Selection = Sequencer->GetSelection();
		for (UMovieSceneTrack* Track : Selection.GetSelectedTracks())
		{
			UMovieSceneAudioTrack* AudioTrack = Cast<UMovieSceneAudioTrack>(Track);

			if (!AudioTrack || !AudioTrack->IsAMasterTrack())
			{
				continue;
			}

			for (UMovieSceneSection* Section : AudioTrack->GetAudioSections())
			{
				UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section);
				const FMovieSceneActorReferenceData& AttachActorData = AudioSection->GetAttachActorData();

				TMovieSceneChannelData<const FMovieSceneActorReferenceKey> ChannelData = AttachActorData.GetData();

				TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
				TArrayView<const FMovieSceneActorReferenceKey> Values = ChannelData.GetValues();
		
				FMovieSceneActorReferenceKey CurrentValue;
				AttachActorData.Evaluate(CurrentTime.Time, CurrentValue);

				for (int32 Index = 0; Index < Times.Num(); ++Index)
				{
					FMovieSceneObjectBindingID AttachBindingID = Values[Index].Object;
					FName AttachSocketName = Values[Index].SocketName;

					FMovieSceneSequenceID SequenceID = Sequencer->GetFocusedTemplateID();
					if (AttachBindingID.GetSequenceID().IsValid())
					{
						// Ensure that this ID is resolvable from the root, based on the current local sequence ID
						FMovieSceneObjectBindingID RootBindingID = AttachBindingID.ResolveLocalToRoot(SequenceID, Sequencer->GetEvaluationTemplate().GetHierarchy());
						SequenceID = RootBindingID.GetSequenceID();
					}

					// If the transform is set, otherwise use the bound actor's transform
					FMovieSceneEvaluationOperand ObjectOperand(SequenceID, AttachBindingID.GetGuid());

					for (TWeakObjectPtr<> WeakObject : Sequencer->FindBoundObjects(ObjectOperand))
					{
						AActor* AttachActor = Cast<AActor>(WeakObject.Get());
						if (AttachActor)
						{
							USceneComponent* AttachComponent = AudioSection->GetAttachComponent(AttachActor, Values[Index]);
							if (AttachComponent)
							{
								FVector Location = AttachComponent->GetSocketLocation(AttachSocketName);
								bool bIsActive = CurrentValue == Values[Index];
								FColor Color = bIsActive ? FColor::Green : FColor::White;

								float Scale = PDI->View->WorldToScreen(Location).W * (4.0f / PDI->View->UnscaledViewRect.Width() / PDI->View->ViewMatrices.GetProjectionMatrix().M[0][0]);
								Scale *= bIsActive ? 15.f : 10.f;

								PDI->DrawSprite(Location, Scale, Scale, AudioTexture->Resource, Color, SDPG_Foreground, 0.0, 0.0, 0.0, 0.0, SE_BLEND_Masked);
								break;
							}
						}
					}
				}
			}
		}
	}
}

FSequencerEdModeTool::FSequencerEdModeTool(FSequencerEdMode* InSequencerEdMode) :
	SequencerEdMode(InSequencerEdMode)
{
}

FSequencerEdModeTool::~FSequencerEdModeTool()
{
}

bool FSequencerEdModeTool::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if( Key == EKeys::LeftMouseButton )
	{
		if( Event == IE_Pressed)
		{
			int32 HitX = ViewportClient->Viewport->GetMouseX();
			int32 HitY = ViewportClient->Viewport->GetMouseY();
			HHitProxy*HitResult = ViewportClient->Viewport->GetHitProxy(HitX, HitY);

			if(HitResult)
			{
				if( HitResult->IsA(HMovieSceneKeyProxy::StaticGetType()) )
				{
					HMovieSceneKeyProxy* KeyProxy = (HMovieSceneKeyProxy*)HitResult;
					SequencerEdMode->OnKeySelected(ViewportClient->Viewport, KeyProxy);
				}
			}
		}
	}

	return FModeTool::InputKey(ViewportClient, Viewport, Key, Event);
}
