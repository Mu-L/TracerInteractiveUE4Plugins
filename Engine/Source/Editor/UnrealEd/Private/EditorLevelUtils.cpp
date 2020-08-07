// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
EditorLevelUtils.cpp: Editor-specific level management routines
=============================================================================*/

#include "EditorLevelUtils.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Model.h"
#include "Engine/Brush.h"
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdEngine.h"
#include "Factories/WorldFactory.h"
#include "Editor/GroupActor.h"
#include "EngineGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ReferenceChainSearch.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "FileHelpers.h"
#include "UnrealEdGlobals.h"
#include "EditorSupportDelegates.h"

#include "BusyCursor.h"
#include "LevelUtils.h"
#include "Layers/LayersSubsystem.h"

#include "ScopedTransaction.h"
#include "ActorEditorUtils.h"
#include "ContentStreaming.h"
#include "PackageTools.h"

#include "AssetRegistryModule.h"
#include "Engine/LevelStreamingVolume.h"
#include "Components/ModelComponent.h"
#include "Misc/RuntimeErrors.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"

DEFINE_LOG_CATEGORY(LogLevelTools);

#define LOCTEXT_NAMESPACE "EditorLevelUtils"


int32 UEditorLevelUtils::MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevelStreaming* DestStreamingLevel, bool bWarnAboutReferences, bool bWarnAboutRenaming)
{
	return MoveActorsToLevel(ActorsToMove, DestStreamingLevel ? DestStreamingLevel->GetLoadedLevel() : nullptr, bWarnAboutReferences, bWarnAboutRenaming);
}

int32 UEditorLevelUtils::MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevel* DestLevel, bool bWarnAboutReferences, bool bWarnAboutRenaming)
{
	int32 NumMovedActors = 0;

	if (DestLevel)
	{
		UWorld* OwningWorld = DestLevel->OwningWorld;

		// Backup the current contents of the clipboard string as we'll be using cut/paste features to move actors
		// between levels and this will trample over the clipboard data.
		FString OriginalClipboardContent;
		FPlatformApplicationMisc::ClipboardPaste(OriginalClipboardContent);

		// The final list of actors to move after invalid actors were removed
		TArray<AActor*> FinalMoveList;
		FinalMoveList.Reserve(ActorsToMove.Num());

		bool bIsDestLevelLocked = FLevelUtils::IsLevelLocked(DestLevel);
		if (!bIsDestLevelLocked)
		{
			for (AActor* CurActor : ActorsToMove)
			{
				if (!CurActor)
				{
					continue;
				}

				bool bIsSourceLevelLocked = FLevelUtils::IsLevelLocked(CurActor);

				if (!bIsSourceLevelLocked)
				{
					if (CurActor->GetLevel() != DestLevel)
					{
						FinalMoveList.Add(CurActor);
					}
					else
					{
						UE_LOG(LogLevelTools, Warning, TEXT("%s is already in the destination level so it was ignored"), *CurActor->GetName());
					}
				}
				else
				{
					UE_LOG(LogLevelTools, Error, TEXT("The source level '%s' is locked so actors could not be moved"), *CurActor->GetLevel()->GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogLevelTools, Error, TEXT("The destination level '%s' is locked so actors could not be moved"), *DestLevel->GetName());
		}


		if (FinalMoveList.Num() > 0)
		{
			TMap<FSoftObjectPath, FSoftObjectPath> ActorPathMapping;
			GEditor->SelectNone(false, true, false);

			USelection* ActorSelection = GEditor->GetSelectedActors();
			ActorSelection->BeginBatchSelectOperation();
			for (AActor* Actor : FinalMoveList)
			{
				ActorPathMapping.Add(FSoftObjectPath(Actor), FSoftObjectPath());
				GEditor->SelectActor(Actor, true, false);
			}
			ActorSelection->EndBatchSelectOperation(false);

			if (GEditor->GetSelectedActorCount() > 0)
			{
				// Start the transaction
				FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MoveSelectedActorsToSelectedLevel", "Move Actors To Level"));

				// Cache the old level
				ULevel* OldCurrentLevel = OwningWorld->GetCurrentLevel();

				// We are moving the actors so cut them to remove them from the existing level
				const bool bShoudCut = true;
				const bool bIsMove = true;
				GEditor->CopySelectedActorsToClipboard(OwningWorld, bShoudCut, bIsMove, bWarnAboutReferences);

				const bool bLevelVisible = DestLevel->bIsVisible;
				if (!bLevelVisible)
				{
					UEditorLevelUtils::SetLevelVisibility(DestLevel, true, false);
				}

				// Scope this so that Actors that have been pasted will have their final levels set before doing the actor mapping
				{
					// Set the new level and force it visible while we do the paste
					FLevelPartitionOperationScope LevelPartitionScope(DestLevel);
					OwningWorld->SetCurrentLevel(LevelPartitionScope.GetLevel());
										
					const bool bDuplicate = false;
					const bool bOffsetLocations = false;
					const bool bWarnIfHidden = false;
					GEditor->edactPasteSelected(OwningWorld, bDuplicate, bOffsetLocations, bWarnIfHidden);

					// Restore the original current level
					OwningWorld->SetCurrentLevel(OldCurrentLevel);
				}

				// Build a remapping of old to new names so we can do a fixup
				for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
				{
					AActor* Actor = static_cast<AActor*>(*It);
					FSoftObjectPath NewPath = FSoftObjectPath(Actor);

					bool bFoundMatch = false;

					// First try exact match
					for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
					{
						if (Pair.Value.IsNull() && NewPath.GetSubPathString() == Pair.Key.GetSubPathString())
						{
							bFoundMatch = true;
							Pair.Value = NewPath;
							break;
						}
					}

					if (!bFoundMatch)
					{
						// Remove numbers from end as it may have had to add some to disambiguate
						FString PartialPath = NewPath.GetSubPathString();
						int32 IgnoreNumber;
						FActorLabelUtilities::SplitActorLabel(PartialPath, IgnoreNumber);

						for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
						{
							if (Pair.Value.IsNull())
							{
								FString KeyPartialPath = Pair.Key.GetSubPathString();
								FActorLabelUtilities::SplitActorLabel(KeyPartialPath, IgnoreNumber);
								if (PartialPath == KeyPartialPath)
								{
									bFoundMatch = true;
									Pair.Value = NewPath;
									break;
								}
							}
						}
					}

					if (!bFoundMatch)
					{
						UE_LOG(LogLevelTools, Error, TEXT("Cannot find remapping for moved actor ID %s, any soft references pointing to it will be broken!"), *Actor->GetPathName());
					}
				}

				FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				TArray<FAssetRenameData> RenameData;

				for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
				{
					if (Pair.Value.IsValid())
					{
						RenameData.Add(FAssetRenameData(Pair.Key, Pair.Value, true));
					}
				}
					
				if (RenameData.Num() > 0)
				{
					if(bWarnAboutRenaming)
					{
						AssetToolsModule.Get().RenameAssetsWithDialog(RenameData);
					}
					else
					{
						AssetToolsModule.Get().RenameAssets(RenameData);
					}
				}

				// Restore new level visibility to previous state
				if (!bLevelVisible)
				{
					UEditorLevelUtils::SetLevelVisibility(DestLevel, false, false);
				}
			}

			// The moved (pasted) actors will now be selected
			NumMovedActors += FinalMoveList.Num();
		}

		// Restore the original clipboard contents
		FPlatformApplicationMisc::ClipboardCopy(*OriginalClipboardContent);
	}

	return NumMovedActors;
}

int32 UEditorLevelUtils::MoveSelectedActorsToLevel(ULevelStreaming* DestStreamingLevel, bool bWarnAboutReferences)
{
	ensureAsRuntimeWarning(DestStreamingLevel != nullptr);
	return DestStreamingLevel ? MoveSelectedActorsToLevel(DestStreamingLevel->GetLoadedLevel(), bWarnAboutReferences) : 0;
}

int32 UEditorLevelUtils::MoveSelectedActorsToLevel(ULevel* DestLevel, bool bWarnAboutReferences)
{
	if (ensureAsRuntimeWarning(DestLevel != nullptr))
	{
		TArray<AActor*> ActorsToMove;
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				ActorsToMove.Add(Actor);
			}
		}

		return MoveActorsToLevel(ActorsToMove, DestLevel, bWarnAboutReferences);
	}

	return 0;
}

ULevel* UEditorLevelUtils::AddLevelsToWorld(UWorld* InWorld, TArray<FString> PackageNames, TSubclassOf<ULevelStreaming> LevelStreamingClass)
{
	if (!ensure(InWorld))
	{
		return nullptr;
	}

	FScopedSlowTask SlowTask(PackageNames.Num(), LOCTEXT("AddLevelsToWorldTask", "Adding Levels to World"));
	SlowTask.MakeDialog();

	// Sort the level packages alphabetically by name.
	PackageNames.Sort();

	// Fire ULevel::LevelDirtiedEvent when falling out of scope.
	FScopedLevelDirtied LevelDirtyCallback;

	// Try to add the levels that were specified in the dialog.
	ULevel* NewLevel = nullptr;
	for (const FString& PackageName : PackageNames)
	{
		SlowTask.EnterProgressFrame();

		if (ULevelStreaming* NewStreamingLevel = AddLevelToWorld_Internal(InWorld, *PackageName, LevelStreamingClass))
		{
			NewLevel = NewStreamingLevel->GetLoadedLevel();
			if (NewLevel)
			{
				LevelDirtyCallback.Request();
			}
		}
	} // for each file

	  // Set the last loaded level to be the current level
	if (NewLevel)
	{
		if (InWorld->SetCurrentLevel(NewLevel))
		{
			FEditorDelegates::NewCurrentLevel.Broadcast();
		}
	}

	// For safety
	if (GLevelEditorModeTools().IsModeActive(FBuiltinEditorModes::EM_Landscape))
	{
		GLevelEditorModeTools().ActivateDefaultMode();
	}

	// Broadcast the levels have changed (new style)
	InWorld->BroadcastLevelsChanged();
	FEditorDelegates::RefreshLevelBrowser.Broadcast();

	// Update volume actor visibility for each viewport since we loaded a level which could potentially contain volumes
	if (GUnrealEd)
	{
		GUnrealEd->UpdateVolumeActorVisibility(nullptr);
	}

	return NewLevel;
}

ULevelStreaming* UEditorLevelUtils::AddLevelToWorld(UWorld* InWorld, const TCHAR* LevelPackageName, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FTransform& LevelTransform)
{
	if (!ensure(InWorld))
	{
		return nullptr;
	}

	FScopedSlowTask SlowTask(0, LOCTEXT("AddLevelToWorldTask", "Adding Level to World"));
	SlowTask.MakeDialog();

	// Fire ULevel::LevelDirtiedEvent when falling out of scope.
	FScopedLevelDirtied LevelDirtyCallback;

	// Try to add the levels that were specified in the dialog.
	ULevel* NewLevel = nullptr;

	ULevelStreaming* NewStreamingLevel = AddLevelToWorld_Internal(InWorld, LevelPackageName, LevelStreamingClass, LevelTransform);
	if (NewStreamingLevel)
	{
		NewLevel = NewStreamingLevel->GetLoadedLevel();
		if (NewLevel)
		{
			LevelDirtyCallback.Request();

			// Set the loaded level to be the current level
			if (InWorld->SetCurrentLevel(NewLevel))
			{
				FEditorDelegates::NewCurrentLevel.Broadcast();
			}
		}
	}

	// For safety
	if (GLevelEditorModeTools().IsModeActive(FBuiltinEditorModes::EM_Landscape))
	{
		GLevelEditorModeTools().ActivateDefaultMode();
	}

	// Broadcast the levels have changed (new style)
	InWorld->BroadcastLevelsChanged();
	FEditorDelegates::RefreshLevelBrowser.Broadcast();

	// Update volume actor visibility for each viewport since we loaded a level which could potentially contain volumes
	if (GUnrealEd)
	{
		GUnrealEd->UpdateVolumeActorVisibility(nullptr);
	}

	return NewStreamingLevel;
}

ULevelStreaming* UEditorLevelUtils::AddLevelToWorld_Internal(UWorld* InWorld, const TCHAR* LevelPackageName, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FTransform& LevelTransform)
{
	ULevel* NewLevel = nullptr;
	ULevelStreaming* StreamingLevel = nullptr;
	bool bIsPersistentLevel = (InWorld->PersistentLevel->GetOutermost()->GetName() == FString(LevelPackageName));

	if (bIsPersistentLevel || FLevelUtils::FindStreamingLevel(InWorld, LevelPackageName))
	{
		// Do nothing if the level already exists in the world.
		const FString LevelName(LevelPackageName);
		const FText MessageText = FText::Format(NSLOCTEXT("UnrealEd", "LevelAlreadyExistsInWorld", "A level with that name ({0}) already exists in the world."), FText::FromString(LevelName));
		FMessageDialog::Open(EAppMsgType::Ok, MessageText);
	}
	else
	{
		// If the selected class is still NULL, abort the operation.
		if (LevelStreamingClass == nullptr)
		{
			return nullptr;
		}

		const FScopedBusyCursor BusyCursor;

		StreamingLevel = NewObject<ULevelStreaming>(InWorld, LevelStreamingClass, NAME_None, RF_NoFlags, NULL);

		// Associate a package name.
		StreamingLevel->SetWorldAssetByPackageName(LevelPackageName);

		StreamingLevel->LevelTransform = LevelTransform;

		// Seed the level's draw color.
		StreamingLevel->LevelColor = FLinearColor::MakeRandomColor();

		// Add the new level to world.
		InWorld->AddStreamingLevel(StreamingLevel);

		// Refresh just the newly created level.
		TArray<ULevelStreaming*> LevelsForRefresh;
		LevelsForRefresh.Add(StreamingLevel);
		InWorld->RefreshStreamingLevels(LevelsForRefresh);
		InWorld->MarkPackageDirty();

		NewLevel = StreamingLevel->GetLoadedLevel();
		if (NewLevel != nullptr)
		{
			EditorLevelUtils::SetLevelVisibility(NewLevel, true, true);

			// Levels migrated from other projects may fail to load their world settings
			// If so we create a new AWorldSettings actor here.
			if (NewLevel->GetWorldSettings(false) == nullptr)
			{
				UWorld* SubLevelWorld = CastChecked<UWorld>(NewLevel->GetOuter());

				FActorSpawnParameters SpawnInfo;
				SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				SpawnInfo.Name = GEngine->WorldSettingsClass->GetFName();
				AWorldSettings* NewWorldSettings = SubLevelWorld->SpawnActor<AWorldSettings>(GEngine->WorldSettingsClass, SpawnInfo);

				NewLevel->SetWorldSettings(NewWorldSettings);
			}
		}
	}

	if (NewLevel) // if the level was successfully added
	{
		FEditorDelegates::OnAddLevelToWorld.Broadcast(NewLevel);
	}

	return StreamingLevel;
}

ULevelStreaming* UEditorLevelUtils::SetStreamingClassForLevel(ULevelStreaming* InLevel, TSubclassOf<ULevelStreaming> LevelStreamingClass)
{
	check(InLevel);

	const FScopedBusyCursor BusyCursor;

	// Cache off the package name, as it will be lost when unloading the level
	const FName CachedPackageName = InLevel->GetWorldAssetPackageFName();

	// First hide and remove the level if it exists
	ULevel* Level = InLevel->GetLoadedLevel();
	check(Level);
	SetLevelVisibility(Level, false, false);
	check(Level->OwningWorld);
	UWorld* World = Level->OwningWorld;

	World->RemoveStreamingLevel(InLevel);

	// re-add the level with the desired streaming class
	AddLevelToWorld(World, *(CachedPackageName.ToString()), LevelStreamingClass);

	// Transfer level streaming settings
	ULevelStreaming* NewStreamingLevel = FLevelUtils::FindStreamingLevel(Level);
	if (NewStreamingLevel)
	{
		NewStreamingLevel->LevelTransform = InLevel->LevelTransform;
		NewStreamingLevel->EditorStreamingVolumes = InLevel->EditorStreamingVolumes;
		NewStreamingLevel->MinTimeBetweenVolumeUnloadRequests = InLevel->MinTimeBetweenVolumeUnloadRequests;
		NewStreamingLevel->LevelColor = InLevel->LevelColor;
		NewStreamingLevel->Keywords = InLevel->Keywords;
	}

	return NewStreamingLevel;
}

void UEditorLevelUtils::MakeLevelCurrent(ULevel* InLevel, bool bEvenIfLocked)
{
	if (ensureAsRuntimeWarning(InLevel != nullptr))
	{
		// Locked levels can't be made current.
		if (bEvenIfLocked || !FLevelUtils::IsLevelLocked(InLevel))
		{
			// Make current broadcast if it changed
			if (InLevel->OwningWorld->SetCurrentLevel(InLevel))
			{
				FEditorDelegates::NewCurrentLevel.Broadcast();
			}

			// Deselect all selected builder brushes.
			bool bDeselectedSomething = false;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				AActor* Actor = static_cast<AActor*>(*It);
				checkSlow(Actor->IsA(AActor::StaticClass()));
				ABrush* Brush = Cast< ABrush >(Actor);
				if (Brush && FActorEditorUtils::IsABuilderBrush(Actor))
				{
					GEditor->SelectActor(Actor, /*bInSelected=*/ false, /*bNotify=*/ false);
					bDeselectedSomething = true;
				}
			}

			// Send a selection change callback if necessary.
			if (bDeselectedSomething)
			{
				GEditor->NoteSelectionChange();
			}

			// Force the current level to be visible.
			SetLevelVisibility(InLevel, true, false);
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_OperationDisallowedOnLockedLevelMakeLevelCurrent", "MakeLevelCurrent: The requested operation could not be completed because the level is locked."));
		}
	}
}

void UEditorLevelUtils::MakeLevelCurrent(ULevelStreaming* InStreamingLevel)
{
	if (ensureAsRuntimeWarning(InStreamingLevel != nullptr))
	{
		MakeLevelCurrent(InStreamingLevel->GetLoadedLevel());
	}
}

bool UEditorLevelUtils::PrivateRemoveInvalidLevelFromWorld(ULevelStreaming* InLevelStreaming)
{
	bool bRemovedLevelStreaming = false;
	if (InLevelStreaming)
	{
		check(InLevelStreaming->GetLoadedLevel() == NULL); // This method is designed to be used to remove left over references to null levels 

		InLevelStreaming->Modify();

		// Disassociate the level from the volume.
		for (ALevelStreamingVolume* LevelStreamingVolume : InLevelStreaming->EditorStreamingVolumes)
		{
			if (LevelStreamingVolume)
			{
				LevelStreamingVolume->Modify();
				LevelStreamingVolume->StreamingLevelNames.Remove(InLevelStreaming->GetWorldAssetPackageFName());
			}
		}

		// Disassociate the volumes from the level.
		InLevelStreaming->EditorStreamingVolumes.Empty();

		if (UWorld* OwningWorld = InLevelStreaming->GetWorld())
		{
			OwningWorld->RemoveStreamingLevel(InLevelStreaming);
			OwningWorld->RefreshStreamingLevels();
			bRemovedLevelStreaming = true;
		}
	}
	return bRemovedLevelStreaming;
}

bool UEditorLevelUtils::RemoveInvalidLevelFromWorld(ULevelStreaming* InLevelStreaming)
{
	bool bRemoveSuccessful = PrivateRemoveInvalidLevelFromWorld(InLevelStreaming);
	if (bRemoveSuccessful)
	{
		// Redraw the main editor viewports.
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();

		// Broadcast the levels have changed (new style)
		InLevelStreaming->GetWorld()->BroadcastLevelsChanged();
		FEditorDelegates::RefreshLevelBrowser.Broadcast();

		// Update selection for any selected actors that were in the level and are no longer valid
		GEditor->NoteSelectionChange();

		// Collect garbage to clear out the destroyed level
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
	return bRemoveSuccessful;
}


ULevelStreaming* UEditorLevelUtils::CreateNewStreamingLevel(TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& PackagePath /*= TEXT("")*/, bool bMoveSelectedActorsIntoNewLevel /*= false*/)
{
	FString Filename;
	if (PackagePath.IsEmpty() || FPackageName::TryConvertLongPackageNameToFilename(PackagePath, Filename, FPackageName::GetMapPackageExtension()))
	{
		if (ensureAsRuntimeWarning(LevelStreamingClass.Get() != nullptr))
		{
			return CreateNewStreamingLevelForWorld(*GEditor->GetEditorWorldContext().World(), LevelStreamingClass, Filename, bMoveSelectedActorsIntoNewLevel);
		}
	}

	return nullptr;
}


ULevelStreaming* UEditorLevelUtils::CreateNewStreamingLevelForWorld(UWorld& InWorld, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& DefaultFilename /* = TEXT( "" ) */, bool bMoveSelectedActorsIntoNewLevel /* = false */, UWorld* InTemplateWorld /* = nullptr */)
{
	// Editor modes cannot be active when any level saving occurs.
	GLevelEditorModeTools().DeactivateAllModes();

	// This is the world we are adding the new level to
	UWorld* WorldToAddLevelTo = &InWorld;

	// This is the new streaming level's world not the persistent level world
	UWorld* NewLevelWorld = nullptr;
	bool bNewWorldSaved = false;
	FString NewPackageName = DefaultFilename;

	if (InTemplateWorld)
	{
		// Copy and save the new world to disk.
		bNewWorldSaved = FEditorFileUtils::SaveLevelAs(InTemplateWorld->PersistentLevel, &NewPackageName);
		if (bNewWorldSaved && !NewPackageName.IsEmpty())
		{
			NewPackageName = FPackageName::FilenameToLongPackageName(NewPackageName);
			UPackage* NewPackage = LoadPackage(nullptr, *NewPackageName, LOAD_None);
			if (NewPackage)
			{
				NewLevelWorld = UWorld::FindWorldInPackage(NewPackage);
			}
		}
	}
	else
	{
		// Create a new world
		UWorldFactory* Factory = NewObject<UWorldFactory>();
		Factory->WorldType = EWorldType::Inactive;
		UPackage* Pkg = CreatePackage(NULL, NULL);
		FName WorldName(TEXT("Untitled"));
		EObjectFlags Flags = RF_Public | RF_Standalone;
		NewLevelWorld = CastChecked<UWorld>(Factory->FactoryCreateNew(UWorld::StaticClass(), Pkg, WorldName, Flags, NULL, GWarn));
		if (NewLevelWorld)
		{
			FAssetRegistryModule::AssetCreated(NewLevelWorld);
		}

		// Save the new world to disk.
		bNewWorldSaved = FEditorFileUtils::SaveLevel(NewLevelWorld->PersistentLevel, DefaultFilename);
		if (bNewWorldSaved)
		{
			NewPackageName = NewLevelWorld->GetOutermost()->GetName();
		}
	}

	// If the new world was saved successfully, import it as a streaming level.
	ULevelStreaming* NewStreamingLevel = nullptr;
	ULevel* NewLevel = nullptr;
	if (bNewWorldSaved)
	{
		NewStreamingLevel = AddLevelToWorld(WorldToAddLevelTo, *NewPackageName, LevelStreamingClass);
		if (NewStreamingLevel != nullptr)
		{
			NewLevel = NewStreamingLevel->GetLoadedLevel();
			// If we are moving the selected actors to the new level move them now
			if (bMoveSelectedActorsIntoNewLevel)
			{
				MoveSelectedActorsToLevel(NewStreamingLevel);
			}

			// Finally make the new level the current one
			if (WorldToAddLevelTo->SetCurrentLevel(NewLevel))
			{
				FEditorDelegates::NewCurrentLevel.Broadcast();
			}
		}
	}

	// Broadcast the levels have changed (new style)
	WorldToAddLevelTo->BroadcastLevelsChanged();
	FEditorDelegates::RefreshLevelBrowser.Broadcast();

	return NewStreamingLevel;
}


bool UEditorLevelUtils::RemoveLevelFromWorld(ULevel* InLevel)
{
	ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
	Layers->RemoveLevelLayerInformation(InLevel);

	GEditor->CloseEditedWorldAssets(CastChecked<UWorld>(InLevel->GetOuter()));

	UWorld* OwningWorld = InLevel->OwningWorld;
	const FName LevelPackageName = InLevel->GetOutermost()->GetFName();
	const bool bRemovingCurrentLevel = InLevel->IsCurrentLevel();
	const bool bRemoveSuccessful = PrivateRemoveLevelFromWorld(InLevel);
	if (bRemoveSuccessful)
	{
		if (bRemovingCurrentLevel)
		{
			// we must set a new level.  It must succeed
			bool bEvenIfLocked = true;
			MakeLevelCurrent(OwningWorld->PersistentLevel, bEvenIfLocked);
		}

		FEditorSupportDelegates::PrepareToCleanseEditorObject.Broadcast(InLevel);

		GEditor->Trans->Reset(LOCTEXT("RemoveLevelTransReset", "Removing Levels from World"));

		EditorDestroyLevel(InLevel);

		// Redraw the main editor viewports.
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();

		// Broadcast the levels have changed (new style)
		OwningWorld->BroadcastLevelsChanged();
		FEditorDelegates::RefreshLevelBrowser.Broadcast();

		// Reset transaction buffer and run GC to clear out the destroyed level
		GEditor->Cleanse(true, false, LOCTEXT("RemoveLevelTransReset", "Removing Levels from World"));

		// Ensure that world was removed
		UPackage* LevelPackage = FindObjectFast<UPackage>(NULL, LevelPackageName);
		if (LevelPackage != nullptr)
		{
			UWorld* TheWorld = UWorld::FindWorldInPackage(LevelPackage->GetOutermost());
			if (TheWorld != nullptr)
			{
				FReferenceChainSearch RefChainSearch(TheWorld, EReferenceChainSearchMode::Shortest | EReferenceChainSearchMode::PrintResults);
				UE_LOG(LogStreaming, Fatal, TEXT("Removed world %s not cleaned up by garbage collection! Referenced by:") LINE_TERMINATOR TEXT("%s"), *TheWorld->GetPathName(), *RefChainSearch.GetRootPath());
			}
		}
	}
	return bRemoveSuccessful;
}


bool UEditorLevelUtils::PrivateRemoveLevelFromWorld(ULevel* InLevel)
{
	if (!InLevel || InLevel->IsPersistentLevel())
	{
		return false;
	}

	if (FLevelUtils::IsLevelLocked(InLevel))
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_OperationDisallowedOnLockedLevelRemoveLevelFromWorld", "RemoveLevelFromWorld: The requested operation could not be completed because the level is locked."));
		return false;
	}

	int32 StreamingLevelIndex = INDEX_NONE;

	for (int32 LevelIndex = 0; LevelIndex < InLevel->OwningWorld->GetStreamingLevels().Num(); ++LevelIndex)
	{
		ULevelStreaming* StreamingLevel = InLevel->OwningWorld->GetStreamingLevels()[LevelIndex];
		if (StreamingLevel && StreamingLevel->GetLoadedLevel() == InLevel)
		{
			StreamingLevelIndex = LevelIndex;
			break;
		}
	}

	if (StreamingLevelIndex != INDEX_NONE)
	{
		ULevelStreaming* StreamingLevel = InLevel->OwningWorld->GetStreamingLevels()[StreamingLevelIndex];
		StreamingLevel->MarkPendingKill();
		InLevel->OwningWorld->RemoveStreamingLevel(StreamingLevel);
		InLevel->OwningWorld->RefreshStreamingLevels();
	}
	else if (InLevel->bIsVisible)
	{
		InLevel->OwningWorld->RemoveFromWorld(InLevel);
		check(InLevel->bIsVisible == false);
	}

	InLevel->ReleaseRenderingResources();

	IStreamingManager::Get().RemoveLevel(InLevel);
	UWorld* World = InLevel->OwningWorld;
	World->RemoveLevel(InLevel);
	if (InLevel->bIsLightingScenario)
	{
		World->PropagateLightingScenarioChange();
	}
	InLevel->ClearLevelComponents();

	// remove all group actors from the world in the level we are removing
	// otherwise, this will cause group actors to not be garbage collected
	for (int32 GroupIndex = World->ActiveGroupActors.Num() - 1; GroupIndex >= 0; --GroupIndex)
	{
		AGroupActor* GroupActor = Cast<AGroupActor>(World->ActiveGroupActors[GroupIndex]);
		if (GroupActor && GroupActor->IsInLevel(InLevel))
		{
			World->ActiveGroupActors.RemoveAt(GroupIndex);
		}
	}

	// Mark all model components as pending kill so GC deletes references to them.
	for (UModelComponent* ModelComponent : InLevel->ModelComponents)
	{
		if (ModelComponent != nullptr)
		{
			ModelComponent->MarkPendingKill();
		}
	}

	// Mark all actors and their components as pending kill so GC will delete references to them.
	for (AActor* Actor : InLevel->Actors)
	{
		if (Actor != nullptr)
		{
			Actor->MarkComponentsAsPendingKill();
			Actor->MarkPendingKill();
		}
	}

	World->MarkPackageDirty();
	World->BroadcastLevelsChanged();

	return true;
}

bool UEditorLevelUtils::EditorDestroyLevel(ULevel* InLevel)
{
	UWorld* World = InLevel->OwningWorld;

	UObject* Outer = InLevel->GetOuter();

	// Call cleanup on the outer world of the level so external hooks can be properly released, so that unloading the package isn't prevented.
	UWorld* OuterWorld = Cast<UWorld>(Outer);
	if (OuterWorld && OuterWorld != World)
	{
		OuterWorld->CleanupWorld();
	}

	Outer->MarkPendingKill();
	InLevel->MarkPendingKill();
	Outer->ClearFlags(RF_Public | RF_Standalone);

	UPackage* Package = InLevel->GetOutermost();
	// We want to unconditionally destroy the level, so clear the dirty flag here so it can be unloaded successfully
	Package->SetDirtyFlag(false);

	TArray<UPackage*> Packages;
	Packages.Add(Package);
	if (!UPackageTools::UnloadPackages(Packages))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Package"), FText::FromString(Package->GetName()));
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("UnloadPackagesFail", "Unable to unload package '{Package}'."), Args));
		return false;
	}

	return true;
}

ULevel* UEditorLevelUtils::CreateNewLevel(UWorld* InWorld, bool bMoveSelectedActorsIntoNewLevel, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& DefaultFilename)
{
	ULevelStreaming* StreamingLevel = CreateNewStreamingLevelForWorld(*InWorld, LevelStreamingClass, DefaultFilename, bMoveSelectedActorsIntoNewLevel);
	return StreamingLevel->GetLoadedLevel();
}

void UEditorLevelUtils::DeselectAllSurfacesInLevel(ULevel* InLevel)
{
	if (InLevel)
	{
		UModel* Model = InLevel->Model;
		for (int32 SurfaceIndex = 0; SurfaceIndex < Model->Surfs.Num(); ++SurfaceIndex)
		{
			FBspSurf& Surf = Model->Surfs[SurfaceIndex];
			if (Surf.PolyFlags & PF_Selected)
			{
				Model->ModifySurf(SurfaceIndex, false);
				Surf.PolyFlags &= ~PF_Selected;
			}
		}
	}
}

void UEditorLevelUtils::SetLevelVisibilityTemporarily(ULevel* Level, bool bShouldBeVisible)
{
	// Nothing to do
	if (Level == NULL)
	{
		return;
	}

	// Set the visibility of each actor in the p-level
	for (TArray<AActor*>::TIterator ActorIter(Level->Actors); ActorIter; ++ActorIter)
	{
		AActor* CurActor = *ActorIter;
		if (CurActor && !FActorEditorUtils::IsABuilderBrush(CurActor) && CurActor->bHiddenEdLevel == bShouldBeVisible)
		{
			CurActor->bHiddenEdLevel = !bShouldBeVisible;
			CurActor->MarkComponentsRenderStateDirty();
		}
	}

	// Set the visibility of each BSP surface in the p-level
	UModel* CurLevelModel = Level->Model;
	if (CurLevelModel)
	{
		for (TArray<FBspSurf>::TIterator SurfaceIterator(CurLevelModel->Surfs); SurfaceIterator; ++SurfaceIterator)
		{
			FBspSurf& CurSurf = *SurfaceIterator;
			CurSurf.bHiddenEdLevel = !bShouldBeVisible;
		}
	}

	// Add/remove model components from the scene
	for (int32 ComponentIndex = 0; ComponentIndex < Level->ModelComponents.Num(); ComponentIndex++)
	{
		UModelComponent* CurLevelModelCmp = Level->ModelComponents[ComponentIndex];
		if (CurLevelModelCmp)
		{
			CurLevelModelCmp->MarkRenderStateDirty();
		}
	}

	Level->GetWorld()->SendAllEndOfFrameUpdates();

	Level->bIsVisible = bShouldBeVisible;

	if (Level->bIsLightingScenario)
	{
		Level->OwningWorld->PropagateLightingScenarioChange();
	}
}

void SetLevelVisibilityNoGlobalUpdateInternal(ULevel* Level, const bool bShouldBeVisible, const bool bForceLayersVisible, const ELevelVisibilityDirtyMode ModifyMode)
{
	// Nothing to do
	if (Level == NULL)
	{
		return;
	}

	// Handle the case of the p-level
	// The p-level can't be unloaded, so its actors/BSP should just be temporarily hidden/unhidden
	// Also, intentionally do not force layers visible for the p-level
	if (Level->IsPersistentLevel())
	{
		// Create a transaction so we can undo the visibility toggle
		const FScopedTransaction Transaction(LOCTEXT("ToggleLevelVisibility", "Toggle Level Visibility"));
		if (Level->bIsVisible != bShouldBeVisible && ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
		{
			Level->Modify();
		}
		// Set the visibility of each actor in the p-level
		for (TArray<AActor*>::TIterator PLevelActorIter(Level->Actors); PLevelActorIter; ++PLevelActorIter)
		{
			AActor* CurActor = *PLevelActorIter;
			if (CurActor && !FActorEditorUtils::IsABuilderBrush(CurActor) && CurActor->bHiddenEdLevel == bShouldBeVisible)
			{
				if (ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
				{
					CurActor->Modify();
				}
				
				CurActor->bHiddenEdLevel = !bShouldBeVisible;
				CurActor->RegisterAllComponents();
				CurActor->MarkComponentsRenderStateDirty();
			}
		}

		// Set the visibility of each BSP surface in the p-level
		UModel* CurLevelModel = Level->Model;
		if (CurLevelModel)
		{
			if (ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
			{
				CurLevelModel->Modify();
			}

			for (TArray<FBspSurf>::TIterator SurfaceIterator(CurLevelModel->Surfs); SurfaceIterator; ++SurfaceIterator)
			{
				FBspSurf& CurSurf = *SurfaceIterator;
				CurSurf.bHiddenEdLevel = !bShouldBeVisible;
			}
		}

		// Add/remove model components from the scene
		for (int32 ComponentIndex = 0; ComponentIndex < Level->ModelComponents.Num(); ComponentIndex++)
		{
			UModelComponent* CurLevelModelCmp = Level->ModelComponents[ComponentIndex];
			if (CurLevelModelCmp)
			{
				if (bShouldBeVisible)
				{
					CurLevelModelCmp->RegisterComponentWithWorld(Level->OwningWorld);
				}
				else if (!bShouldBeVisible && CurLevelModelCmp->IsRegistered())
				{
					CurLevelModelCmp->UnregisterComponent();
				}
			}
		}

		Level->GetWorld()->OnLevelsChanged().Broadcast();
	}
	else
	{
		ULevelStreaming* StreamingLevel = NULL;
		if (Level->OwningWorld == NULL || Level->OwningWorld->PersistentLevel != Level)
		{
			StreamingLevel = FLevelUtils::FindStreamingLevel(Level);
		}

		// Create a transaction so we can undo the visibility toggle
		const FScopedTransaction Transaction(LOCTEXT("ToggleLevelVisibility", "Toggle Level Visibility"));

		// Handle the case of a streaming level
		if (StreamingLevel)
		{
			if (ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
			{
				// We need to set the RF_Transactional to make a streaming level serialize itself. so store the original ones, set the flag, and put the original flags back when done
				EObjectFlags cachedFlags = StreamingLevel->GetFlags();
				StreamingLevel->SetFlags(RF_Transactional);
				StreamingLevel->Modify();
				StreamingLevel->SetFlags(cachedFlags);
			}

			// Set the visibility state for this streaming level.  
			StreamingLevel->SetShouldBeVisibleInEditor(bShouldBeVisible);
		}

		ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		if (!bShouldBeVisible)
		{
			Layers->RemoveLevelLayerInformation(Level);
		}

		// UpdateLevelStreaming sets Level->bIsVisible directly, so we need to make sure it gets saved to the transaction buffer.
		if (Level->bIsVisible != bShouldBeVisible && ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
		{
			Level->Modify();
		}

		if (StreamingLevel)
		{
			Level->OwningWorld->FlushLevelStreaming();

			// In the Editor we expect this operation will complete in a single call
			check(Level->bIsVisible == bShouldBeVisible);
		}
		else if (Level->OwningWorld != NULL)
		{
			// In case we level has no associated StreamingLevel, remove or add to world directly
			if (bShouldBeVisible)
			{
				if (!Level->bIsVisible)
				{
					Level->OwningWorld->AddToWorld(Level);
				}
			}
			else
			{
				Level->OwningWorld->RemoveFromWorld(Level);
			}

			// In the Editor we expect this operation will complete in a single call
			check(Level->bIsVisible == bShouldBeVisible);
		}

		if (bShouldBeVisible)
		{
			Layers->AddLevelLayerInformation(Level);
		}

		// Force the level's layers to be visible, if desired
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();

		// Iterate over the level's actors, making a list of their layers and unhiding the layers.
		TArray<AActor*>& Actors = Level->Actors;
		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = Actors[ActorIndex];
			if (Actor)
			{
				bool bModified = false;
				if (bShouldBeVisible && bForceLayersVisible &&
					Layers->IsActorValidForLayer(Actor))
				{
					// Make the actor layer visible, if it's not already.
					if (Actor->bHiddenEdLayer)
					{
						if (ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
						{
							bModified = Actor->Modify();
						}
						
						Actor->bHiddenEdLayer = false;
					}

					const bool bIsVisible = true;
					Layers->SetLayersVisibility(Actor->Layers, bIsVisible);
				}

				// Set the visibility of each actor in the streaming level
				if (!FActorEditorUtils::IsABuilderBrush(Actor) && Actor->bHiddenEdLevel == bShouldBeVisible)
				{
					if (!bModified && ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
					{
						bModified = Actor->Modify();
					}
					Actor->bHiddenEdLevel = !bShouldBeVisible;

					if (bShouldBeVisible)
					{
						Actor->ReregisterAllComponents();
					}
					else
					{
						Actor->UnregisterAllComponents();
					}
				}
			}
		}
	}

	Level->bIsVisible = bShouldBeVisible;

	// If the level is being hidden, deselect actors and surfaces that belong to this level. (Part 1/2)
	if (!bShouldBeVisible && ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		SelectedActors->Modify();
		const TArray<AActor*>& Actors = Level->Actors;
		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = Actors[ActorIndex];
			if (Actor)
			{
				SelectedActors->Deselect(Actor);
			}
		}

		UEditorLevelUtils::DeselectAllSurfacesInLevel(Level);
	}

	if (Level->bIsLightingScenario)
	{
		Level->OwningWorld->PropagateLightingScenarioChange();
	}
}

void UEditorLevelUtils::SetLevelVisibility(ULevel* Level, const bool bShouldBeVisible, const bool bForceLayersVisible, const ELevelVisibilityDirtyMode ModifyMode)
{
	TArray<ULevel*> Levels({ Level });
	TArray<bool> bTheyShouldBeVisible({ bShouldBeVisible });
	SetLevelsVisibility(Levels, bTheyShouldBeVisible, bForceLayersVisible, ModifyMode);
}

void UEditorLevelUtils::SetLevelsVisibility(const TArray<ULevel*>& Levels, const TArray<bool>& bTheyShouldBeVisible, const bool bForceLayersVisible, const ELevelVisibilityDirtyMode ModifyMode)
{
	// Nothing to do
	if (Levels.Num() == 0 || Levels.Num() != bTheyShouldBeVisible.Num())
	{
		return;
	}

	// Perform SetLevelVisibilityNoGlobalUpdateInternal for each Level
	for (int32 LevelIndex = 0; LevelIndex < Levels.Num(); ++LevelIndex)
	{
		ULevel* Level = Levels[LevelIndex];
		if (Level)
		{
			SetLevelVisibilityNoGlobalUpdateInternal(Level, bTheyShouldBeVisible[LevelIndex], bForceLayersVisible, ModifyMode);
		}
	}

	// If at least 1 persistent level, then RedrawAllViewports.Broadcast
	for (int32 LevelIndex = 0; LevelIndex < Levels.Num(); ++LevelIndex)
	{
		ULevel* Level = Levels[LevelIndex];
		if (Level && Level->IsPersistentLevel())
		{
			FEditorSupportDelegates::RedrawAllViewports.Broadcast();
			break;
		}
	}

	// If at least 1 level becomes visible, force layers to update their actor status
	// Otherwise, changes made on the layers for actors belonging to a non-visible level would not work
	{
		for (int32 LevelIndex = 0; LevelIndex < bTheyShouldBeVisible.Num(); ++LevelIndex)
		{
			if (bTheyShouldBeVisible[LevelIndex])
			{
				// Equivalent to GEditor->GetEditorSubsystem<ULayersSubsystem>()->UpdateAllActorsVisibilityDefault();
				FEditorDelegates::RefreshLayerBrowser.Broadcast();
				break;
			}
		}
	}

	// Notify the Scene Outliner, as new Actors may be present in the world.
	GEngine->BroadcastLevelActorListChanged();

	// If the level is being hidden, deselect actors and surfaces that belong to this level. (Part 2/2)
	if (ModifyMode == ELevelVisibilityDirtyMode::ModifyOnChange)
	{
		for (int32 LevelIndex = 0; LevelIndex < bTheyShouldBeVisible.Num(); ++LevelIndex)
		{
			if (!bTheyShouldBeVisible[LevelIndex])
			{
				// Tell the editor selection status was changed.
				GEditor->NoteSelectionChange();
				break;
			}
		}
	}
}

void UEditorLevelUtils::GetWorlds(UWorld* InWorld, TArray<UWorld*>& OutWorlds, bool bIncludeInWorld, bool bOnlyEditorVisible)
{
	OutWorlds.Empty();

	if (!InWorld)
	{
		return;
	}

	if (bIncludeInWorld)
	{
		OutWorlds.AddUnique(InWorld);
	}

	// Iterate over the world's level array to find referenced levels ("worlds"). We don't 
	for (ULevelStreaming* StreamingLevel : InWorld->GetStreamingLevels())
	{
		if (StreamingLevel)
		{
			// If we asked for only sub-levels that are editor-visible, then limit our results appropriately
			bool bShouldAlwaysBeLoaded = false; // Cast< ULevelStreamingAlwaysLoaded >( StreamingLevel ) != NULL;
			if (!bOnlyEditorVisible || bShouldAlwaysBeLoaded || StreamingLevel->GetShouldBeVisibleInEditor())
			{
				const ULevel* Level = StreamingLevel->GetLoadedLevel();

				// This should always be the case for valid level names as the Editor preloads all packages.
				if (Level)
				{
					// Newer levels have their packages' world as the outer.
					UWorld* World = Cast<UWorld>(Level->GetOuter());
					if (World)
					{
						OutWorlds.AddUnique(World);
					}
				}
			}
		}
	}

	// Levels can be loaded directly without StreamingLevel facilities
	for (ULevel* Level : InWorld->GetLevels())
	{
		if (Level)
		{
			// Newer levels have their packages' world as the outer.
			UWorld* World = Cast<UWorld>(Level->GetOuter());
			if (World)
			{
				OutWorlds.AddUnique(World);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
