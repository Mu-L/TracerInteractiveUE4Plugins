// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/CameraAnimTrackEditor.h"
#include "TrackEditors/CameraAnimTrackEditorHelper.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "AssetData.h"
#include "ReferenceSkeleton.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "SequencerSectionPainter.h"
#include "MovieSceneCommonHelpers.h"
#include "Camera/CameraAnim.h"
#include "Sections/MovieSceneCameraAnimSection.h"
#include "Tracks/MovieSceneCameraAnimTrack.h"
#include "AssetRegistryModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "SequencerUtilities.h"

#define LOCTEXT_NAMESPACE "FCameraAnimTrackEditor"


class FCameraAnimSection : public FSequencerSection
{
public:
	FCameraAnimSection(UMovieSceneSection& InSection)
		: FSequencerSection( InSection )
	{ }

	/** ISequencerSection interface */
	virtual FText GetSectionTitle() const override 
	{ 
		UMovieSceneCameraAnimSection const* const AnimSection = Cast<UMovieSceneCameraAnimSection>(WeakSection.Get());
		UCameraAnim const* const Anim = AnimSection ? AnimSection->AnimData.CameraAnim : nullptr;
		if (Anim)
		{
			return FText::FromString(Anim->GetName());
		}
		return LOCTEXT("NoCameraAnimSection", "No Camera Anim");
	}
};


FCameraAnimTrackEditor::FCameraAnimTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor( InSequencer ) 
{ 
}


TSharedRef<ISequencerTrackEditor> FCameraAnimTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FCameraAnimTrackEditor(InSequencer));
}


bool FCameraAnimTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneCameraAnimTrack::StaticClass();
}


TSharedRef<ISequencerSection> FCameraAnimTrackEditor::MakeSectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding )
{
	check( SupportsType( SectionObject.GetOuter()->GetClass() ) );
	return MakeShareable(new FCameraAnimSection(SectionObject));
}


bool FCameraAnimTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	UCameraAnim* const CameraAnim = Cast<UCameraAnim>(Asset);
	if (CameraAnim)
	{
		if (TargetObjectGuid.IsValid())
		{
			TArray<TWeakObjectPtr<>> OutObjects;
			for (TWeakObjectPtr<> Object : GetSequencer()->FindObjectsInCurrentSequence(TargetObjectGuid))
			{
				OutObjects.Add(Object);
			}

			const FScopedTransaction Transaction(LOCTEXT("AddCameraAnim_Transaction", "Add Camera Anim"));

			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FCameraAnimTrackEditor::AddKeyInternal, OutObjects, CameraAnim));

			return true;
		}
	}

	return false;
}


void FCameraAnimTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	IModuleInterface* TemplateSequenceEditorModule = FModuleManager::Get().GetModule("TemplateSequenceEditor");
	if (TemplateSequenceEditorModule != nullptr)
	{
		// The template sequence plugin will add a new menu which lets people add CameraAnim assets as
		// "legacy" assets, with a way to upgrade them to a template sequence.
		return;
	}

	// only offer this track if we can find a camera component
	UCameraComponent const* const CamComponent = AcquireCameraComponentFromObjectGuid(ObjectBindings[0]);
	if (CamComponent)
	{
		const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();

		// Load the asset registry module
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		// Collect a full list of assets with the specified class
		TArray<FAssetData> AssetDataList;
		AssetRegistryModule.Get().GetAssetsByClass(UCameraAnim::StaticClass()->GetFName(), AssetDataList);

		MenuBuilder.AddSubMenu(
			LOCTEXT("AddCameraAnim", "Camera Anim"), NSLOCTEXT("Sequencer", "AddCameraAnimTooltip", "Adds an additive camera animation track."),
			FNewMenuDelegate::CreateRaw(this, &FCameraAnimTrackEditor::AddCameraAnimSubMenu, ObjectBindings)
			);
	}
}

TSharedRef<SWidget> FCameraAnimTrackEditor::BuildCameraAnimSubMenu(FGuid ObjectBinding)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);

	AddCameraAnimSubMenu(MenuBuilder, ObjectBindings);

	return MenuBuilder.MakeWidget();
}


void FCameraAnimTrackEditor::AddCameraAnimSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBinding)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FCameraAnimTrackEditor::OnCameraAnimAssetSelected, ObjectBinding);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FCameraAnimTrackEditor::OnCameraAnimAssetEnterPressed, ObjectBinding);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.Filter.ClassNames.Add(UCameraAnim::StaticClass()->GetFName());
//		AssetPickerConfig.Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(Skeleton).GetExportTextName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}

TSharedPtr<SWidget> FCameraAnimTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	// Create a container edit box
	return SNew(SHorizontalBox)

	// Add the camera anim combo box
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		FSequencerUtilities::MakeAddButton(LOCTEXT("AddCameraAnim", "Camera Anim"), FOnGetContent::CreateSP(this, &FCameraAnimTrackEditor::BuildCameraAnimSubMenu, ObjectBinding), Params.NodeIsHovered, GetSequencer())
	];
}

void FCameraAnimTrackEditor::OnCameraAnimAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();

	if (SelectedObject && SelectedObject->IsA(UCameraAnim::StaticClass()))
	{
		UCameraAnim* const CameraAnim = CastChecked<UCameraAnim>(AssetData.GetAsset());

		TArray<TWeakObjectPtr<>> OutObjects;
		for (FGuid ObjectBinding : ObjectBindings)
		{
			for (TWeakObjectPtr<> Object : GetSequencer()->FindObjectsInCurrentSequence(ObjectBinding))
			{
				OutObjects.Add(Object);
			}
		}
		
		const FScopedTransaction Transaction(LOCTEXT("AddCameraAnim_Transaction", "Add Camera Anim"));

		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FCameraAnimTrackEditor::AddKeyInternal, OutObjects, CameraAnim));
	}
}

void FCameraAnimTrackEditor::OnCameraAnimAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings)
{
	if (AssetData.Num() > 0)
	{
		OnCameraAnimAssetSelected(AssetData[0].GetAsset(), ObjectBindings);
	}
}

FKeyPropertyResult FCameraAnimTrackEditor::AddKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, UCameraAnim* CameraAnim)
{
	return FCameraAnimTrackEditorHelper::AddCameraAnimKey(*this, KeyTime, Objects, CameraAnim);
}


FKeyPropertyResult FCameraAnimTrackEditorHelper::AddCameraAnimKey(FMovieSceneTrackEditor& TrackEditor, FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, UCameraAnim* CameraAnim)
{
	FKeyPropertyResult KeyPropertyResult;
	const TSharedPtr<ISequencer> Sequencer = TrackEditor.GetSequencer();

	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ++ObjectIndex)
	{
		UObject* Object = Objects[ObjectIndex].Get();

		FMovieSceneTrackEditor::FFindOrCreateHandleResult HandleResult = TrackEditor.FindOrCreateHandleToObject(Object);
		FGuid ObjectHandle = HandleResult.Handle;
		KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;
		if (ObjectHandle.IsValid())
		{
			FMovieSceneTrackEditor::FFindOrCreateTrackResult TrackResult = TrackEditor.FindOrCreateTrackForObject(ObjectHandle, UMovieSceneCameraAnimTrack::StaticClass());
			UMovieSceneTrack* Track = TrackResult.Track;
			KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

			if (ensure(Track))
			{
				UMovieSceneSection* NewSection = Cast<UMovieSceneCameraAnimTrack>(Track)->AddNewCameraAnim(KeyTime, CameraAnim);
				KeyPropertyResult.bTrackModified = true;
				KeyPropertyResult.SectionsCreated.Add(NewSection);

				Sequencer->EmptySelection();
				Sequencer->SelectSection(NewSection);
				Sequencer->ThrobSectionSelection();
			}
		}
	}

	return KeyPropertyResult;
}

UCameraComponent* FCameraAnimTrackEditor::AcquireCameraComponentFromObjectGuid(const FGuid& Guid)
{
	USkeleton* Skeleton = nullptr;
	for (TWeakObjectPtr<> WeakObject : GetSequencer()->FindObjectsInCurrentSequence(Guid))
	{
		UObject* const Obj = WeakObject.Get();
	
		if (AActor* const Actor = Cast<AActor>(Obj))
		{
			UCameraComponent* const CameraComp = MovieSceneHelpers::CameraComponentFromActor(Actor);
			if (CameraComp)
			{
				return CameraComp;
			}
		}
		else if (UCameraComponent* const CameraComp = Cast<UCameraComponent>(Obj))
		{
			if (CameraComp->IsActive())
			{
				return CameraComp;
			}
		}
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE
