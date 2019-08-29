// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/MaterialTrackEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Sections/MovieSceneParameterSection.h"
#include "Tracks/MovieSceneMaterialTrack.h"
#include "Sections/ParameterSection.h"
#include "SequencerUtilities.h"
#include "Modules/ModuleManager.h"
#include "MaterialEditorModule.h"


#define LOCTEXT_NAMESPACE "MaterialTrackEditor"


FMaterialTrackEditor::FMaterialTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMovieSceneTrackEditor( InSequencer )
{
}


TSharedRef<ISequencerSection> FMaterialTrackEditor::MakeSectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding )
{
	UMovieSceneParameterSection* ParameterSection = Cast<UMovieSceneParameterSection>(&SectionObject);
	checkf( ParameterSection != nullptr, TEXT("Unsupported section type.") );

	return MakeShareable(new FParameterSection( *ParameterSection ));
}


TSharedPtr<SWidget> FMaterialTrackEditor::BuildOutlinerEditWidget( const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params )
{
	UMovieSceneMaterialTrack* MaterialTrack = Cast<UMovieSceneMaterialTrack>(Track);
	FOnGetContent MenuContent = FOnGetContent::CreateSP(this, &FMaterialTrackEditor::OnGetAddParameterMenuContent, ObjectBinding, MaterialTrack);

	return FSequencerUtilities::MakeAddButton(LOCTEXT( "AddParameterButton", "Parameter" ), MenuContent, Params.NodeIsHovered);
}


struct FParameterNameAndAction
{
	FName ParameterName;
	FUIAction Action;

	FParameterNameAndAction( FName InParameterName, FUIAction InAction )
	{
		ParameterName = InParameterName;
		Action = InAction;
	}

	bool operator<(FParameterNameAndAction const& Other) const
	{
		return ParameterName < Other.ParameterName;
	}
};


TSharedRef<SWidget> FMaterialTrackEditor::OnGetAddParameterMenuContent( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
{
	FMenuBuilder AddParameterMenuBuilder( true, nullptr );

	UMaterial* Material = GetMaterialForTrack( ObjectBinding, MaterialTrack );
	if ( Material != nullptr )
	{
		UMaterialInterface* MaterialInterface = GetMaterialInterfaceForTrack(ObjectBinding, MaterialTrack);
		
		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>( MaterialInterface );	
		TArray<FMaterialParameterInfo> VisibleExpressions;

		IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
		bool bCollectedVisibleParameters = false;
		if (MaterialEditorModule && MaterialInstance)
		{
			MaterialEditorModule->GetVisibleMaterialParameters(Material, MaterialInstance, VisibleExpressions);
			bCollectedVisibleParameters = true;
		}

		TArray<FParameterNameAndAction> ParameterNamesAndActions;

		// Collect scalar parameters.
		TArray<FMaterialParameterInfo> ScalarParameterInfo;
		TArray<FGuid> ScalarParameterGuids;
		Material->GetAllScalarParameterInfo(ScalarParameterInfo, ScalarParameterGuids );
		for (int32 ScalarParameterIndex = 0; ScalarParameterIndex < ScalarParameterInfo.Num(); ++ScalarParameterIndex)
		{
			if (!bCollectedVisibleParameters || VisibleExpressions.Contains(ScalarParameterInfo[ScalarParameterIndex].Name))
			{
				FName ScalarParameterName = ScalarParameterInfo[ScalarParameterIndex].Name;
				FUIAction AddParameterMenuAction( FExecuteAction::CreateSP( this, &FMaterialTrackEditor::AddScalarParameter, ObjectBinding, MaterialTrack, ScalarParameterName ) );
				FParameterNameAndAction NameAndAction( ScalarParameterName, AddParameterMenuAction );
				ParameterNamesAndActions.Add(NameAndAction);
			}
		}

		// Collect color parameters.
		TArray<FMaterialParameterInfo> ColorParameterInfo;
		TArray<FGuid> ColorParameterGuids;
		Material->GetAllVectorParameterInfo( ColorParameterInfo, ColorParameterGuids );
		for (int32 ColorParameterIndex = 0; ColorParameterIndex < ColorParameterInfo.Num(); ++ColorParameterIndex)
		{
			if (!bCollectedVisibleParameters || VisibleExpressions.Contains(ColorParameterInfo[ColorParameterIndex].Name))
			{
				FName ColorParameterName = ColorParameterInfo[ColorParameterIndex].Name;
				FUIAction AddParameterMenuAction( FExecuteAction::CreateSP( this, &FMaterialTrackEditor::AddColorParameter, ObjectBinding, MaterialTrack, ColorParameterName ) );
				FParameterNameAndAction NameAndAction( ColorParameterName, AddParameterMenuAction );
				ParameterNamesAndActions.Add( NameAndAction );
			}
		}

		// Sort and generate menu.
		ParameterNamesAndActions.Sort();
		for ( FParameterNameAndAction NameAndAction : ParameterNamesAndActions )
		{
			AddParameterMenuBuilder.AddMenuEntry( FText::FromName( NameAndAction.ParameterName ), FText(), FSlateIcon(), NameAndAction.Action );
		}
	}

	return AddParameterMenuBuilder.MakeWidget();
}


UMaterial* FMaterialTrackEditor::GetMaterialForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
{
	UMaterialInterface* MaterialInterface = GetMaterialInterfaceForTrack( ObjectBinding, MaterialTrack );
	if ( MaterialInterface != nullptr )
	{
		UMaterial* Material = Cast<UMaterial>( MaterialInterface );
		if ( Material != nullptr )
		{
			return Material;
		}
		else
		{
			UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>( MaterialInterface );
			if ( MaterialInstance != nullptr )
			{
				return MaterialInstance->GetMaterial();
			}
		}
	}
	return nullptr;
}


void FMaterialTrackEditor::AddScalarParameter( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack, FName ParameterName )
{
	FFrameNumber KeyTime = GetTimeForKey();

	UMaterialInterface* Material = GetMaterialInterfaceForTrack(ObjectBinding, MaterialTrack);
	if (Material != nullptr)
	{
		const FScopedTransaction Transaction( LOCTEXT( "AddScalarParameter", "Add scalar parameter" ) );
		float ParameterValue;
		Material->GetScalarParameterValue(ParameterName, ParameterValue);
		MaterialTrack->Modify();
		MaterialTrack->AddScalarParameterKey(ParameterName, KeyTime, ParameterValue);
	}
	GetSequencer()->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
}


void FMaterialTrackEditor::AddColorParameter( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack, FName ParameterName )
{
	FFrameNumber KeyTime = GetTimeForKey();

	UMaterialInterface* Material = GetMaterialInterfaceForTrack( ObjectBinding, MaterialTrack );
	if ( Material != nullptr )
	{
		const FScopedTransaction Transaction( LOCTEXT( "AddVectorParameter", "Add vector parameter" ) );
		FLinearColor ParameterValue;
		Material->GetVectorParameterValue( ParameterName, ParameterValue );
		MaterialTrack->Modify();
		MaterialTrack->AddColorParameterKey( ParameterName, KeyTime, ParameterValue );
	}
	GetSequencer()->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
}


FComponentMaterialTrackEditor::FComponentMaterialTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMaterialTrackEditor( InSequencer )
{
}


TSharedRef<ISequencerTrackEditor> FComponentMaterialTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable( new FComponentMaterialTrackEditor( OwningSequencer ) );
}


bool FComponentMaterialTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneComponentMaterialTrack::StaticClass();
}

bool FComponentMaterialTrackEditor::GetDefaultExpansionState(UMovieSceneTrack* InTrack) const
{
	return true;
}


UMaterialInterface* FComponentMaterialTrackEditor::GetMaterialInterfaceForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return nullptr;
	}

	UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(SequencerPtr->FindSpawnedObjectOrTemplate( ObjectBinding ));
	UMovieSceneComponentMaterialTrack* ComponentMaterialTrack = Cast<UMovieSceneComponentMaterialTrack>( MaterialTrack );
	if ( Component != nullptr && ComponentMaterialTrack != nullptr )
	{
		return Component->GetMaterial( ComponentMaterialTrack->GetMaterialIndex() );
	}
	return nullptr;
}


#undef LOCTEXT_NAMESPACE
