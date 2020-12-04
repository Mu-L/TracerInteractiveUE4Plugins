// Copyright Epic Games, Inc. All Rights Reserved.


#include "FileHelpers.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/RedirectCollector.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Engine/Brush.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Editor/EditorEngine.h"
#include "ISourceControlModule.h"
#include "SourceControlOperations.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Factories/Factory.h"
#include "Factories/FbxSceneImportFactory.h"
#include "GameMapsSettings.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h" 
#include "UnrealEdMisc.h"
#include "EditorDirectories.h"
#include "Dialogs/Dialogs.h"
#include "UnrealEdGlobals.h"
#include "EditorLevelUtils.h"
#include "BusyCursor.h"
#include "MRUFavoritesList.h"
#include "Framework/Application/SlateApplication.h"
#include "Exporters/Exporter.h"

#include "PackagesDialog.h"
#include "Interfaces/IMainFrameModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"

#include "DesktopPlatformModule.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"

#include "Dialogs/DlgPickPath.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#include "ObjectTools.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "AutoSaveUtils.h"
#include "AssetRegistryModule.h"
#include "Misc/BlacklistNames.h"
#include "EngineAnalytics.h"
#include "StudioAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "HierarchicalLOD.h"

DEFINE_LOG_CATEGORY_STATIC(LogFileHelpers, Log, All);


//definition of flag used to do special work when we're attempting to load the "startup map"
bool FEditorFileUtils::bIsLoadingDefaultStartupMap = false;
bool FEditorFileUtils::bIsPromptingForCheckoutAndSave = false;
TSet<FString> FEditorFileUtils::PackagesNotSavedDuringSaveAll;
TSet<FString> FEditorFileUtils::PackagesNotToPromptAnyMore;


static TAutoConsoleVariable<int32> CVarSkipSourceControlCheckForEditablePackages(
	TEXT("r.Editor.SkipSourceControlCheckForEditablePackages"),
	0,
    TEXT("Whether to skip the source control status check for editable packages, 0: Disable (Default), 1: Enable"));

#define LOCTEXT_NAMESPACE "FileHelpers"

/** A special output device that puts save output in the message log when flushed */
class FSaveErrorOutputDevice : public FOutputDevice
{
public:
	virtual void Serialize( const TCHAR* InData, ELogVerbosity::Type Verbosity, const class FName& Category ) override
	{
		if ( Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning )
		{
			EMessageSeverity::Type Severity;
			if ( Verbosity == ELogVerbosity::Error )
			{
				Severity = EMessageSeverity::Error;
			}
			else
			{
				Severity = EMessageSeverity::Warning;
			}
			ErrorMessages.Add(FTokenizedMessage::Create(Severity, FText::FromName(InData)));
		}
	}

	virtual void Flush() override
	{
		if ( ErrorMessages.Num() > 0 )
		{
			FMessageLog EditorErrors("EditorErrors");
			EditorErrors.NewPage(LOCTEXT("SaveOutputPageLabel", "Save Output"));
			EditorErrors.AddMessages(ErrorMessages);
			EditorErrors.Open();
			ErrorMessages.Empty();
		}
	}

private:
	// Holds the errors for the message log.
	TArray< TSharedRef< FTokenizedMessage > > ErrorMessages;
};

namespace FileDialogHelpers
{
	/**
	 * @param Title                  The title of the dialog
	 * @param FileTypes              Filter for which file types are accepted and should be shown
	 * @param InOutLastPath          Keep track of the last location from which the user attempted an import
	 * @param DefaultFile            Default file name to use for saving.
	 * @param OutOpenFilenames       The list of filenames that the user attempted to open
	 *
	 * @return true if the dialog opened successfully and the user accepted; false otherwise.
	 */
	bool SaveFile( const FString& Title, const FString& FileTypes, FString& InOutLastPath, const FString& DefaultFile, FString& OutFilename )
	{
		OutFilename = FString();

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		bool bFileChosen = false;
		TArray<FString> OutFilenames;
		if (DesktopPlatform)
		{
			bFileChosen = DesktopPlatform->SaveFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				Title,
				InOutLastPath,
				DefaultFile,
				FileTypes,
				EFileDialogFlags::None,
				OutFilenames
			);
		}

		bFileChosen = (OutFilenames.Num() > 0);

		if (bFileChosen)
		{
			// User successfully chose a file; remember the path for the next time the dialog opens.
			InOutLastPath = OutFilenames[0];
			OutFilename = OutFilenames[0];
		}

		return bFileChosen;
	}

	/**
	 * @param Title                  The title of the dialog
	 * @param FileTypes              Filter for which file types are accepted and should be shown
	 * @param InOutLastPath    Keep track of the last location from which the user attempted an import
	 * @param DialogMode             Multiple items vs single item.
	 * @param OutOpenFilenames       The list of filenames that the user attempted to open
	 *
	 * @return true if the dialog opened successfully and the user accepted; false otherwise.
	 */
	bool OpenFiles( const FString& Title, const FString& FileTypes, FString& InOutLastPath, EFileDialogFlags::Type DialogMode, TArray<FString>& OutOpenFilenames ) 
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		bool bOpened = false;
		if ( DesktopPlatform )
		{
			bOpened = DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				Title,
				InOutLastPath,
				TEXT(""),
				FileTypes,
				DialogMode,
				OutOpenFilenames
			);
		}

		bOpened = (OutOpenFilenames.Num() > 0);

		if ( bOpened )
		{
			// User successfully chose a file; remember the path for the next time the dialog opens.
			InOutLastPath = OutOpenFilenames[0];
		}

		return bOpened;
	}
}



/**
 * Queries the user if they want to quit out of interpolation editing before save.
 *
 * @return		true if in interpolation editing mode, false otherwise.
 */
static bool InInterpEditMode()
{
	// Must exit Interpolation Editing mode before you can save - so it can reset everything to its initial state.
	if( GLevelEditorModeTools().IsModeActive( FBuiltinEditorModes::EM_InterpEdit ) )
	{
		const bool ExitInterp = EAppReturnType::Yes == FMessageDialog::Open( EAppMsgType::YesNo, EAppReturnType::Yes, NSLOCTEXT("UnrealEd", "Prompt_21", "You must close Matinee before saving level.\nDo you wish to do this now and continue?") );
		if(!ExitInterp)
		{
			return true;
		}

		GLevelEditorModeTools().DeactivateMode( FBuiltinEditorModes::EM_InterpEdit );
	}
	return false;
}

/**
* Prompts user with a confirmation dialog if there are checkouts or modifications in other branches
*
* @return true if checkout should proceed
*/
static bool ConfirmPackageBranchCheckOutStatus(const TArray<UPackage*>& PackagesToCheckOut)
{
	//@TODO: Need more info here (in the event multiple packages are trying to be saved at once; the prompt shown is misleading in that case (you might be OK with stomping over one file but not others later on in the list))
	for (UPackage* CurPackage : PackagesToCheckOut)
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(CurPackage, EStateCacheUsage::Use);

		// If checked out or modified in another branch, warn about possible loss of changes and confirm checkout
		if (SourceControlState.IsValid() && SourceControlState->IsCheckedOutOrModifiedInOtherBranch())
		{
			int32 HeadCL;
			FString HeadBranch, HeadAction;
			FNumberFormattingOptions NoCommas;
			NoCommas.UseGrouping = false;

			const FString& CurrentBranch = FEngineVersion::Current().GetBranch();

			SourceControlState->GetOtherBranchHeadModification(HeadBranch, HeadAction, HeadCL);

			FText InfoText;

			if (SourceControlState->IsModifiedInOtherBranch())
			{
				int32 CurrentBranchIdx = SourceControlProvider.GetStateBranchIndex(CurrentBranch);
				int32 HeadBranchIdx = SourceControlProvider.GetStateBranchIndex(HeadBranch);
				{
					if (CurrentBranchIdx != INDEX_NONE && HeadBranchIdx != INDEX_NONE)
					{
						// modified
						if (CurrentBranchIdx < HeadBranchIdx)
						{
							InfoText = LOCTEXT("WarningModifiedOtherBranchHigher", "Modified in higher branch, consider waiting for package to be merged down.");
						}
						else
						{
							InfoText = LOCTEXT("WarningModifiedOtherBranchLower", "Modified in lower branch, keep track of your work. You may need to redo it during the merge.");
						}
					}
				}
			}
			else
			{
				// checked out

				FString Username;
				if (!SourceControlState->GetOtherUserBranchCheckedOuts().Split(TEXT("@"), &Username, nullptr))
				{
					Username = SourceControlState->GetOtherUserBranchCheckedOuts();
				}

				InfoText = FText::Format(LOCTEXT("WarningCheckedOutOtherBranchHigher", "Please ask if {0}'s change can wait."), FText::FromString(Username));

			}

			const FText PackageNameText = FText::FromName(CurPackage->GetFName());

			const FText Message = SourceControlState->IsModifiedInOtherBranch() ? FText::Format(LOCTEXT("WarningModifiedOtherBranch", "WARNING: Package {3} modified in {0} CL {1}\n\n{2}\n\nCheck out packages anyway?"), FText::FromString(HeadBranch), FText::AsNumber(HeadCL, &NoCommas), InfoText, PackageNameText)
				: FText::Format(LOCTEXT("WarningCheckedOutOtherBranch", "WARNING: Package {2} checked out in {0}\n\n{1}\n\nCheck out packages anyway?"), FText::FromString(SourceControlState->GetOtherUserBranchCheckedOuts()), InfoText, PackageNameText);

			const FText Title = SourceControlState->IsModifiedInOtherBranch() ? FText::FromString("Package Branch Modifications") : FText::FromString("Package Branch Checkouts");

			return FMessageDialog::Open(EAppMsgType::YesNo, Message, &Title) == EAppReturnType::Yes;
		}
	}

	return true;
}

/**
 * Maps loaded level packages to the package filenames.
 */
static TMap<FName, FString> LevelFilenames;

void FEditorFileUtils::RegisterLevelFilename(UObject* Object, const FString& NewLevelFilename)
{
	const FName PackageName(*Object->GetOutermost()->GetName());
	//UE_LOG(LogFileHelpers, Log, TEXT("RegisterLevelFilename: package %s to name %s"), *PackageName, *NewLevelFilename );
	FString* ExistingFilenamePtr = LevelFilenames.Find( PackageName );
	if ( ExistingFilenamePtr )
	{
		// Update the existing entry with the new filename.
		*ExistingFilenamePtr = NewLevelFilename;
	}
	else
	{
		// Set for the first time.
		LevelFilenames.Add( PackageName, NewLevelFilename );
	}

	// Mirror the world's filename to UnrealEd's title bar.
	if ( Object == GWorld )
	{
		IMainFrameModule& MainFrameModule = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.SetLevelNameForWindowTitle(NewLevelFilename);
	}
}

///////////////////////////////////////////////////////////////////////////////

FString FEditorFileUtils::GetFilename(const FName& PackageName)
{
	// First see if it is an in-memory package that already has an associated filename
	const FString PackageNameString = PackageName.ToString();
	const bool bIncludeReadOnlyRoots = false;
	if ( FPackageName::IsValidLongPackageName(PackageNameString, bIncludeReadOnlyRoots) )
	{
		return FPackageName::LongPackageNameToFilename(PackageNameString, FPackageName::GetMapPackageExtension());
	}

	FString* Result = LevelFilenames.Find( PackageName );
	if ( !Result )
	{
		//UE_LOG(LogFileHelpers, Log, TEXT("GetFilename with package %s, returning EMPTY"), *PackageName );
		return FString(TEXT(""));
	}
	// Verify that the file still exists, if it does not, reset the level filename
	else if ( IFileManager::Get().FileSize( **Result ) == INDEX_NONE )
	{
		*Result = FString(TEXT(""));
		UWorld* World = GWorld;
		if ( World && World->GetOutermost()->GetFName() == PackageName )
		{
			IMainFrameModule& MainFrameModule = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
			MainFrameModule.SetLevelNameForWindowTitle(*Result);
		}
	}

	//UE_LOG(LogFileHelpers, Log, TEXT("GetFilename with package %s, returning %s"), *PackageName, **Result );
	return *Result;
}

FString FEditorFileUtils::GetFilename(UObject* LevelObject)
{
	return GetFilename( LevelObject->GetOutermost()->GetFName() );
}

///////////////////////////////////////////////////////////////////////////////

static FString GetDefaultDirectory()
{
	return FEditorDirectories::Get().GetLastDirectory(ELastDirectory::UNR);
}

///////////////////////////////////////////////////////////////////////////////

/**
* Returns a file filter string appropriate for a specific file interaction.
*
* @param	Interaction		A file interaction to get a filter string for.
* @return					A filter string.
*/
FString FEditorFileUtils::GetFilterString(EFileInteraction Interaction)
{
	FString Result;
	TSet<FString> Extensions;

	switch( Interaction )
	{
	case FI_Load:
	case FI_Save:
		{
			Result = FString::Printf( TEXT("Map files (*%s)|*%s|All files (*.*)|*.*"), *FPackageName::GetMapPackageExtension(),
																					   *FPackageName::GetMapPackageExtension());
		}
		break;

	case FI_ImportScene:
		{
			TArray<UFactory*> Factories;
			for (UClass* Class : TObjectRange<UClass>())
			{
				if (Class->IsChildOf<USceneImportFactory>() && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					UFactory* Factory = Class->GetDefaultObject<UFactory>();
					if (Factory->bEditorImport)
					{
						Factories.Add(Factory);
					}
				}

			}

			if (Factories.Num() > 0)
			{
				FString FileTypes;
				FString AllExtensions;
				TMultiMap<uint32, UFactory*> FilterIndexToFactory;

				ObjectTools::GenerateFactoryFileExtensions(Factories, FileTypes, AllExtensions, FilterIndexToFactory);

				FileTypes = FString::Printf(TEXT("All Files (%s)|%s|%s"), *AllExtensions, *AllExtensions, *FileTypes);


				Result = FileTypes;
			}

		}
		break;

	case FI_ExportScene:
		{
			for (UClass* Class : TObjectRange<UClass>())
			{
				if (!Class->IsChildOf<UExporter>() || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					continue;
				}

				UExporter* Exporter = Class->GetDefaultObject<UExporter>();
				if (!Exporter->SupportsObject(UWorld::StaticClass()->GetDefaultObject()))
				{
					continue;
				}

				// Ignore generic UObject exporters
				if (!Exporter->SupportedClass || !Exporter->SupportedClass->IsChildOf<UWorld>())
				{
					continue;
				}

				for (int32 i = 0; i < Exporter->FormatExtension.Num(); ++i)
				{
					FString FormatExtensionLower = Exporter->FormatExtension[i].ToLower();
					if (FormatExtensionLower == TEXT("copy"))
					{
						continue;
					}

					// Skip over duplicates
					if (Extensions.Contains(FormatExtensionLower))
					{
						continue;
					}
					Extensions.Add(FormatExtensionLower);

					if (Result.Len() > 0)
					{
						Result += TEXT("|");
					}

					const FString& FormatDescription = Exporter->FormatDescription[i];
					Result += FString::Printf(TEXT("%s (*.%s)|*.%s"), *FormatDescription, *FormatExtensionLower, *FormatExtensionLower);
				}
			}
		}
		break;

	default:
		checkf( 0, TEXT("Unkown EFileInteraction" ) );
	}

	return Result;
}

///////////////////////////////////////////////////////////////////////////////


/**
 * @param	World					The world to save.
 * @param	ForceFilename			If non-NULL, save the level package to this name (full path+filename).
 * @param	OverridePath			If non-NULL, override the level path with this path.
 * @param	FilenamePrefix			If non-NULL, prepend this string the the level filename.
 * @param	bRenamePackageToFile	If true, rename the level package to the filename if save was successful.
 * @param	bCheckDirty				If true, don't save the level if it is not dirty.
 * @param	FinalFilename			[out] The full path+filename the level was saved to.
 * @param	bAutosaving				Should be set to true if autosaving; passed to UWorld::SaveWorld.
 * @param	bPIESaving				Should be set to true if saving for PIE; passed to UWorld::SaveWorld.
 * @return							true if the level was saved.
 */
static bool SaveWorld(UWorld* World,
					   const FString* ForceFilename,
					   const TCHAR* OverridePath,
					   const TCHAR* FilenamePrefix,
					   bool bRenamePackageToFile,
					   bool bCheckDirty,
					   FString& FinalFilename,
					   bool bAutosaving,
					   bool bPIESaving)
{
	// SaveWorld not reentrant - check that we are not already in the process of saving here (for example, via autosave)
	static bool bIsReentrant = false;
	if (bIsReentrant)
	{
		return false;
	}

	TGuardValue<bool> ReentrantGuard(bIsReentrant, true);

	if ( !World )
	{
		FinalFilename = LOCTEXT("FilenameUnavailable", "Filename Not available!").ToString();
		return false;
	}

	UPackage* Package = Cast<UPackage>( World->GetOuter() );
	if ( !Package )
	{
		FinalFilename = LOCTEXT("FilenameUnavailableInvalidOuter", "Filename Not available. Outer package invalid!").ToString();
		return false;
	}

	// Don't save if the world doesn't need saving.
	if ( bCheckDirty && !Package->IsDirty() )
	{
		FinalFilename = LOCTEXT("FilenameUnavailableNotDirty", "Filename Not available. Package not dirty.").ToString();
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SaveWorld);

	FString PackageName = Package->GetName();

	FString	ExistingFilename;
	FString	Path;
	FString	CleanFilename;

	// Does a filename already exist for this package?
	const bool bPackageExists = FPackageName::DoesPackageExist( PackageName, NULL, &ExistingFilename );

	if ( ForceFilename )
	{
		Path				= FPaths::GetPath(*ForceFilename);
		CleanFilename		= FPaths::GetCleanFilename(*ForceFilename);
	}
	else if ( bPackageExists )
	{
		if( bPIESaving && FCString::Stristr( *ExistingFilename, *FPackageName::GetMapPackageExtension() ) == NULL )
		{
			// If package exists, but doesn't feature the default extension, it will not load when launched,
			// Change the extension of the map to the default for the auto-save
			Path			= AutoSaveUtils::GetAutoSaveDir();
			CleanFilename	= FPackageName::GetLongPackageAssetName(PackageName) + FPackageName::GetMapPackageExtension();
		}
		else
		{
			// We're not forcing a filename, so go with the filename that exists.
			Path			= FPaths::GetPath(ExistingFilename);
			CleanFilename	= FPaths::GetCleanFilename(ExistingFilename);
		}
	}
	else if ( !bAutosaving && FPackageName::IsValidLongPackageName(PackageName, false) )
	{
		// If the package is made with a path in a non-read-only root, save it there
		const FString ImplicitFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());
		Path = FPaths::GetPath(ImplicitFilename);
		CleanFilename = FPaths::GetCleanFilename(ImplicitFilename);
	}
	else
	{
		// No package filename exists and none was specified, so save the package in the autosaves folder.
		Path			= AutoSaveUtils::GetAutoSaveDir();
		CleanFilename	= FPackageName::GetLongPackageAssetName(PackageName) + FPackageName::GetMapPackageExtension();
	}

	// Optionally override path.
	if ( OverridePath )
	{
		FinalFilename = FString(OverridePath) + TEXT("/");
	}
	else
	{
		FinalFilename = Path + TEXT("/");
	}

	// Apply optional filename prefix.
	if ( FilenamePrefix )
	{
		FinalFilename += FString(FilenamePrefix);
	}

	// Munge remaining clean filename minus path + extension with path and optional prefix.
	FinalFilename += CleanFilename;

	// Prepare the new package name
	FString NewPackageName;
	if ( !FPackageName::TryConvertFilenameToLongPackageName(FinalFilename, NewPackageName) )
	{
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format( NSLOCTEXT("Editor", "SaveWorld_BadFilename", "Failed to save the map. The filename '{0}' is not within the game or engine content folders found in '{1}'."), FText::FromString( FinalFilename ), FText::FromString( FPaths::RootDir() ) ) );
		return false;
	}

	// Before doing any work, check to see if 1) the package name is in use by another object, 2) the world object can be renamed if necessary; and 3) the file is writable.
	bool bSuccess = false;

	const FString OriginalWorldName = World->GetName();
	const FString OriginalPackageName = Package->GetName();
	const FString NewWorldAssetName = FPackageName::GetLongPackageAssetName(NewPackageName);
	bool bValidWorldName = true;
	bool bPackageNeedsRename = false;
	bool bWorldNeedsRename = false;

	if ( bRenamePackageToFile )
	{
		// Rename the world package if needed
		if ( Package->GetName() != NewPackageName )
		{
			bValidWorldName = Package->Rename( *NewPackageName, NULL, REN_Test );
			if ( bValidWorldName )
			{
				bPackageNeedsRename = true;
			}
		}

		if ( bValidWorldName )
		{
			// Rename the world if the package changed
			if ( World->GetName() != NewWorldAssetName )
			{
				bValidWorldName = World->Rename( *NewWorldAssetName, NULL, REN_Test );
				if ( bValidWorldName )
				{
					bWorldNeedsRename = true;
				}
			}
		}
	}

	if ( !bValidWorldName )
	{
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_LevelNameExists", "A level with that name already exists. Please choose another name.") );
	}
	else if( IFileManager::Get().IsReadOnly(*FinalFilename) )
	{
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format(NSLOCTEXT("UnrealEd", "PackageFileIsReadOnly", "Unable to save package to {0} because the file is read-only!"), FText::FromString(FinalFilename)) );
	}
	else
	{
		// Save the world package after doing optional garbage collection.
		const FScopedBusyCursor BusyCursor;

		FFormatNamedArguments Args;
		Args.Add( TEXT("MapFilename"), FText::FromString( FPaths::GetCleanFilename(FinalFilename) ) );
		
		FScopedSlowTask SlowTask(100, FText::Format( NSLOCTEXT("UnrealEd", "SavingMap_F", "Saving map: {MapFilename}..." ), Args ));
		SlowTask.MakeDialog(true);

		SlowTask.EnterProgressFrame(25);

		FSoftObjectPath OldPath( World );
		bool bAddedAssetPathRedirection = false;

		// Rename the package and the object, as necessary
		UWorld* DuplicatedWorld = nullptr;
		if ( bRenamePackageToFile )
		{
			if (bPackageNeedsRename)
			{
				// If we are doing a SaveAs on a world that already exists, we need to duplicate it.
				if (bPackageExists)
				{
					ObjectTools::FPackageGroupName NewPGN;
					NewPGN.PackageName = NewPackageName;
					NewPGN.ObjectName = NewWorldAssetName;

					bool bPromptToOverwrite = false;
					TSet<UPackage*> PackagesUserRefusedToFullyLoad;
					DuplicatedWorld = Cast<UWorld>(ObjectTools::DuplicateSingleObject(World, NewPGN, PackagesUserRefusedToFullyLoad, bPromptToOverwrite));
					if (DuplicatedWorld)
					{
						Package = DuplicatedWorld->GetOutermost();
					}
					else
					{
						// Avoid assert during rename when duplicate fails
						if (!Package->Rename(*NewPackageName, NULL, REN_Test))
						{
							FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("UnrealEd", "Error_OverwriteMapCleanup", "Unable to overwrite existing package {0}."), FText::FromString(NewPackageName)));
							return false;
						}
					}
				}

				if (!DuplicatedWorld)
				{
					// Duplicate failed or not needed. Just do a rename.
					Package->Rename(*NewPackageName, NULL, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					
					if (bWorldNeedsRename)
					{
						// Unload package of existing MapBuildData to allow overwrite
						if (World->PersistentLevel->MapBuildData && !World->PersistentLevel->MapBuildData->IsLegacyBuildData())
						{
							FString NewBuiltPackageName = World->GetOutermost()->GetName() + TEXT("_BuiltData");
							UObject* ExistingObject = StaticFindObject(nullptr, 0, *NewBuiltPackageName);
							if (ExistingObject && ExistingObject != World->PersistentLevel->MapBuildData->GetOutermost())
							{
								TArray<UPackage*> AllPackagesToUnload;
								AllPackagesToUnload.Add(Cast<UPackage>(ExistingObject));
								UPackageTools::UnloadPackages(AllPackagesToUnload);
							}
						}

						World->Rename(*NewWorldAssetName, NULL, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					}

					// We're changing the world path, add a path redirector so that soft object paths get fixed on save
					FSoftObjectPath NewPath( World );
					GRedirectCollector.AddAssetPathRedirection( *OldPath.GetAssetPathString(), *NewPath.GetAssetPathString() );
					bAddedAssetPathRedirection = true;
				}
			}
		}

		// Mark package as fully loaded, this is usually set implicitly by calling IsFullyLoaded before saving, but that path can get skipped for levels
		Package->MarkAsFullyLoaded();

		SlowTask.EnterProgressFrame(50);

		// Save package.
		{
			const FString AutoSavingString = (bAutosaving || bPIESaving) ? TEXT("true") : TEXT("false");
			const FString KeepDirtyString = bPIESaving ? TEXT("true") : TEXT("false");
			FSaveErrorOutputDevice SaveErrors;

			bSuccess = GEditor->Exec( NULL, *FString::Printf( TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\" SILENT=true AUTOSAVING=%s KEEPDIRTY=%s"), *Package->GetName(), *FinalFilename, *AutoSavingString, *KeepDirtyString ), SaveErrors );
			SaveErrors.Flush();
		}

		// @todo Autosaving should save build data as well
		if (bSuccess && !bAutosaving)
		{
			// Also save MapBuildData packages when saving the current level and save external packages if the world was duplicated
			FEditorFileUtils::SaveMapDataPackages(DuplicatedWorld ? DuplicatedWorld : World, bCheckDirty || bPIESaving, DuplicatedWorld != nullptr);
		}

		SlowTask.EnterProgressFrame(25);

		// If the package save was not successful. Trash the duplicated world or rename back if the duplicate failed.
		if( bRenamePackageToFile && !bSuccess )
		{
			if (bPackageNeedsRename)
			{
				if (DuplicatedWorld)
				{
					DuplicatedWorld->Rename(nullptr, GetTransientPackage(), REN_NonTransactional | REN_DontCreateRedirectors);
					DuplicatedWorld->MarkPendingKill();
					DuplicatedWorld->SetFlags(RF_Transient);
					DuplicatedWorld = nullptr;
				}
				else
				{
					Package->Rename(*OriginalPackageName, NULL, REN_NonTransactional);

					if (bWorldNeedsRename)
					{
						World->Rename(*OriginalWorldName, NULL, REN_NonTransactional);
					}
				}
			}
		}
	}

	return bSuccess;
}

FString GetAutoSaveFilename(UPackage* const Package, const FString& AutoSavePathRoot, const int32 AutoSaveIndex, const FString& PackageExt)
{
	// Come up with a meaningful name for the auto-save file
	const FString PackagePathName = Package->GetPathName();

	FString AutoSavePath;
	FString PackageRoot;
	FString PackagePath;
	FString PackageName;
	const bool bStripRootLeadingSlash = true;
	if(FPackageName::SplitLongPackageName(PackagePathName, PackageRoot, PackagePath, PackageName, bStripRootLeadingSlash))
	{
		AutoSavePath = AutoSavePathRoot / PackageRoot / PackagePath;
	}
	else
	{
		AutoSavePath = AutoSavePathRoot;
		PackageName = FPaths::GetBaseFilename(PackagePathName);
	}

	// Ensure the directory we're about to save to exists
	IFileManager::Get().MakeDirectory(*AutoSavePath, true);

	// Create an auto-save filename
	const FString Filename = AutoSavePath / *FString::Printf(TEXT("%s_Auto%i%s"), *PackageName, AutoSaveIndex, *PackageExt);
	return Filename;
}

/** Renames a single level, preserving the common suffix */
bool RenameStreamingLevel( FString& LevelToRename, const FString& OldBaseLevelName, const FString& NewBaseLevelName )
{
	// Make sure the level starts with the original level name
	if( LevelToRename.StartsWith( OldBaseLevelName ) )	// Not case sensitive
	{
		// Grab the tail of the streaming level name, basically everything after the old base level name
		FString SuffixToPreserve = LevelToRename.Right( LevelToRename.Len() - OldBaseLevelName.Len() );

		// Rename the level!
		LevelToRename = NewBaseLevelName + SuffixToPreserve;

		return true;
	}

	return false;
}

static bool OpenSaveAsDialog(UClass* SavedClass, const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName)
{
	FString DefaultPath = InDefaultPath;

	if (DefaultPath.IsEmpty())
	{
		DefaultPath = TEXT("/Game/Maps");
	}

	FString NewNameSuggestion = InNewNameSuggestion;
	check(!NewNameSuggestion.IsEmpty());

	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = DefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = NewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(SavedClass->GetFName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = (SavedClass == UWorld::StaticClass())
			? LOCTEXT("SaveLevelDialogTitle", "Save Level As")
			: LOCTEXT("SaveAssetDialogTitle", "Save Asset As");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	
	if ( !SaveObjectPath.IsEmpty() )
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}

/**
 * Prompts the user with a dialog for selecting a filename.
 */
static bool SaveAsImplementation( UWorld* InWorld, const FString& DefaultFilename, const bool bAllowStreamingLevelRename, FString* OutSavedFilename )
{
	UEditorLoadingSavingSettings* LoadingSavingSettings = GetMutableDefault<UEditorLoadingSavingSettings>();

	// Get default path and filename. If no default filename was supplied, create one.
	FString DefaultDirectory = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::LEVEL);
	FString Filename = FPaths::GetCleanFilename(DefaultFilename);
	if (Filename.IsEmpty())
	{
		const FString DefaultName = TEXT("NewMap");
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(DefaultDirectory / DefaultName, PackageName))
		{
			// Initial location is invalid (e.g. lies outside of the project): set location to /Game/Maps instead
			DefaultDirectory = FPaths::ProjectContentDir() / TEXT("Maps");
			ensure(FPackageName::TryConvertFilenameToLongPackageName(DefaultDirectory / DefaultName, PackageName));
		}
		FString Name;
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(PackageName, TEXT(""), PackageName, Name);

		Filename = FPaths::GetCleanFilename(FPackageName::LongPackageNameToFilename(PackageName));
	}

	// Disable autosaving while the "Save As..." dialog is up.
	const bool bOldAutoSaveState = LoadingSavingSettings->bAutoSaveEnable;
	LoadingSavingSettings->bAutoSaveEnable = false;

	bool bStatus = false;

	// Loop through until a valid filename is given or the user presses cancel
	bool bFilenameIsValid = false;

	FString SaveFilename;
	while( !bFilenameIsValid )
	{
		SaveFilename = FString();
		bool bSaveFileLocationSelected = false;

		FString DefaultPackagePath;
		FPackageName::TryConvertFilenameToLongPackageName(DefaultDirectory / Filename, DefaultPackagePath);

		FString PackageName;
		bSaveFileLocationSelected = OpenSaveAsDialog(
			UWorld::StaticClass(),
			FPackageName::GetLongPackagePath(DefaultPackagePath),
			FPaths::GetBaseFilename(Filename),
			PackageName);

		if( bSaveFileLocationSelected )
		{
			SaveFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());

			FText ErrorMessage;
			bFilenameIsValid = FEditorFileUtils::IsValidMapFilename(SaveFilename, ErrorMessage);
			
			if ( bFilenameIsValid )
			{
				// If there is an existing world in memory that shares this name unload it now to prepare for overwrite.
				// Don't do this if we are using save as to overwrite the current level since it will just save naturally.
				const FString NewPackageName = FPackageName::FilenameToLongPackageName(SaveFilename);
				UPackage* ExistingPackage = FindPackage(nullptr, *NewPackageName);
				if ( ExistingPackage && ExistingPackage != InWorld->GetOutermost() )
				{
					bFilenameIsValid = FEditorFileUtils::AttemptUnloadInactiveWorldPackage(ExistingPackage, ErrorMessage);
				}
			}

			if ( !bFilenameIsValid )
			{
				// Start the loop over, prompting for save again
				const FText DisplayFilename = FText::FromString( IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*SaveFilename) );
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("Filename"), DisplayFilename);
				Arguments.Add(TEXT("LineTerminators"), FText::FromString(LINE_TERMINATOR LINE_TERMINATOR));
				Arguments.Add(TEXT("ErrorMessage"), ErrorMessage);
				const FText DisplayMessage = FText::Format( NSLOCTEXT("SaveAsImplementation", "InvalidMapName", "Failed to save map {Filename}{LineTerminators}{ErrorMessage}"), Arguments );
				FMessageDialog::Open( EAppMsgType::Ok, DisplayMessage );
				continue;
			}

			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::LEVEL, FPaths::GetPath(SaveFilename));

			// Check to see if there are streaming level associated with the P map, and if so, we'll
			// prompt to rename those and fixup all of the named-references to levels in the maps.
			bool bCanRenameStreamingLevels = false;
			FString OldBaseLevelName, NewBaseLevelName;

			if( bAllowStreamingLevelRename )
			{
				const FString OldLevelName = FPaths::GetBaseFilename(Filename);
				const FString NewLevelName = FPaths::GetBaseFilename(SaveFilename);

				// The old and new level names must have a common suffix.  We'll detect that now.
				int32 NumSuffixChars = 0;
				{
					for( int32 CharsFromEndIndex = 0; ; ++CharsFromEndIndex )
					{
						const int32 OldLevelNameCharIndex = ( OldLevelName.Len() - 1 ) - CharsFromEndIndex;
						const int32 NewLevelNameCharIndex = ( NewLevelName.Len() - 1 ) - CharsFromEndIndex;

						if( OldLevelNameCharIndex <= 0 || NewLevelNameCharIndex <= 0 )
						{
							// We've processed all characters in at least one of the strings!
							break;
						}

						if( FChar::ToUpper( OldLevelName[ OldLevelNameCharIndex ] ) != FChar::ToUpper( NewLevelName[ NewLevelNameCharIndex ] ) )
						{
							// Characters don't match.  We have the common suffix now.
							break;
						}

						// We have another common character in the suffix!
						++NumSuffixChars;
					}

				}


				// We can only proceed if we found a common suffix
				if( NumSuffixChars > 0 )
				{
					FString CommonSuffix = NewLevelName.Right( NumSuffixChars );

					OldBaseLevelName = OldLevelName.Left( OldLevelName.Len() - CommonSuffix.Len() );
					NewBaseLevelName = NewLevelName.Left( NewLevelName.Len() - CommonSuffix.Len() );


					// OK, make sure this is really the persistent level
					if( InWorld->PersistentLevel->IsPersistentLevel() )
					{
						// Check to see if we actually have anything to rename
						bool bAnythingToRename = false;
						{
							// Check for contained streaming levels
							for (ULevelStreaming* CurStreamingLevel : InWorld->GetStreamingLevels())
							{
								if (CurStreamingLevel)
								{
									// Update the package name
									FString PackageNameToRename = CurStreamingLevel->GetWorldAssetPackageName();
									if( RenameStreamingLevel( PackageNameToRename, OldBaseLevelName, NewBaseLevelName ) )
									{
										bAnythingToRename = true;
									}
								}
							}
						}

						if( bAnythingToRename )
						{
							// OK, we can go ahead and rename levels
							bCanRenameStreamingLevels = true;
						}
					}
				}
			}

			if( bCanRenameStreamingLevels )
			{
				// Prompt to update streaming levels and such
				// Return value:  0 = yes, 1 = no, 2 = cancel
				const EAppReturnType::Type DlgResult =
					FMessageDialog::Open( EAppMsgType::YesNoCancel, EAppReturnType::No,
					FText::Format( NSLOCTEXT("UnrealEd", "SaveLevelAs_PromptToRenameStreamingLevels_F", "Would you like to update references to streaming levels and rename those as well?\n\nIf you select Yes, references to streaming levels in {0} will be renamed to {1} (including Level Blueprint level name references.)  You should also do this for each of your streaming level maps.\n\nIf you select No, the level will be saved with the specified name and no other changes will be made." ),
					FText::FromString(FPaths::GetBaseFilename(Filename)), FText::FromString(FPaths::GetBaseFilename(SaveFilename)) ) );

				if( DlgResult != EAppReturnType::Cancel )	// Cancel?
				{
					if( DlgResult == EAppReturnType::Yes )	// Yes?
					{
						// Update streaming level names
						for (ULevelStreaming* CurStreamingLevel : InWorld->GetStreamingLevels())
						{
							if (CurStreamingLevel)
							{
								// Update the package name
								FString PackageNameToRename = CurStreamingLevel->GetWorldAssetPackageName();
								if( RenameStreamingLevel( PackageNameToRename, OldBaseLevelName, NewBaseLevelName ) )
								{
									CurStreamingLevel->SetWorldAssetByPackageName(FName( *PackageNameToRename ));

									// Level was renamed!
									CurStreamingLevel->MarkPackageDirty();
								}
							}
						}
					}

					// Save the level!
					bStatus = FEditorFileUtils::SaveMap( InWorld, SaveFilename );
				}
				else
				{
					// User canceled, nothing to do.
				}
			}
			else
			{
				// Save the level
				bStatus = FEditorFileUtils::SaveMap( InWorld, SaveFilename );
			}
		}
		else
		{
			// User canceled the save dialog, do not prompt again.
			break;
		}

	}

	// Restore autosaving to its previous state.
	LoadingSavingSettings->bAutoSaveEnable = bOldAutoSaveState;

	// Update SCC state
	ISourceControlModule::Get().QueueStatusUpdate(InWorld->GetOutermost());

	if (bStatus && OutSavedFilename)
	{
		*OutSavedFilename = SaveFilename;
	}

	return bStatus;
}

/**
 * @return		true if GWorld's package is dirty.
 */
static bool IsWorldDirty()
{
	UPackage* Package = CastChecked<UPackage>(GWorld->GetOuter());
	return Package->IsDirty();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FEditorFileUtils
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FEditorFileUtils::SaveAssetsAs(const TArray<UObject*>& Assets, TArray<UObject*>& OutSavedAssets)
{
	for (UObject* Asset : Assets)
	{
		const FString OldPackageName = Asset->GetOutermost()->GetName();
		
		FString OldPackagePath;
		FString OldAssetName;
		
		if (Asset->HasAnyFlags(RF_Transient))
		{
			// determine default package path
			const FString DefaultDirectory = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::NEW_ASSET);
			FPackageName::TryConvertFilenameToLongPackageName(DefaultDirectory, OldPackagePath);

			if (OldPackagePath.IsEmpty())
			{
				OldPackagePath = TEXT("/Game");
			}

			// determine default asset name
			FString DefaultName = FString(NSLOCTEXT("UnrealEd", "PrefixNew", "New").ToString() + Asset->GetClass()->GetName());

			FString UniquePackageName;
			FString UniqueAssetName;

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().CreateUniqueAssetName(OldPackagePath / DefaultName, TEXT(""), UniquePackageName, UniqueAssetName);

			OldAssetName = FPaths::GetCleanFilename(UniqueAssetName);
		}
		else
		{
			OldAssetName = FPackageName::GetLongPackageAssetName(OldPackageName);
			OldPackagePath = FPackageName::GetLongPackagePath(OldPackageName);
		}

		FString NewPackageName;

		// get destination for asset
		bool FilenameValid = false;

		while (!FilenameValid)
		{
			if (!OpenSaveAsDialog(Asset->GetClass(), OldPackagePath, OldAssetName, NewPackageName))
			{
				return;
			}

			FText OutError;
			FilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
		}

		// process asset
		if (NewPackageName.IsEmpty())
		{
			OutSavedAssets.Add(Asset); // user canceled
		}
		else if (NewPackageName != OldPackageName)
		{
			// duplicate asset at destination
			const FString NewAssetName = FPackageName::GetLongPackageAssetName(NewPackageName);
			UPackage* DuplicatedPackage = CreatePackage( *NewPackageName);
			UObject* DuplicatedAsset = StaticDuplicateObject(Asset, DuplicatedPackage, *NewAssetName);

			if (DuplicatedAsset != nullptr)
			{
				// update duplicated asset & notify asset registry
				if (Asset->HasAnyFlags(RF_Transient))
				{
					DuplicatedAsset->ClearFlags(RF_Transient);
					DuplicatedAsset->SetFlags(RF_Public | RF_Standalone);
				}

				if (Asset->GetOutermost()->HasAnyPackageFlags(PKG_DisallowExport))
				{
					DuplicatedPackage->SetPackageFlags(PKG_DisallowExport);
				}

				DuplicatedAsset->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(DuplicatedAsset);
				OutSavedAssets.Add(DuplicatedAsset);

				// update last save directory
				const FString PackageFilename = FPackageName::LongPackageNameToFilename(NewPackageName);
				const FString PackagePath = FPaths::GetPath(PackageFilename);

				FEditorDirectories::Get().SetLastDirectory(ELastDirectory::NEW_ASSET, PackagePath);
			}
			else
			{
				OutSavedAssets.Add(Asset); // error duplicating
			}
		}
		else
		{
			OutSavedAssets.Add(Asset); // save existing asset
		}
	}

	// save packages
	TArray<UPackage*> PackagesToSave;

	for (UObject* Asset : OutSavedAssets)
	{
		PackagesToSave.Add(Asset->GetOutermost());
	}

	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, true, false);
}


/**
 * Does a saveAs for the specified level.
 *
 * @param	InLevel		The level to be SaveAs'd.
 * @return				true if the world was saved.
 */
bool FEditorFileUtils::SaveLevelAs(ULevel* InLevel, FString* OutSavedFilename)
{
	FString DefaultFilename;

	if (InLevel->IsPersistentLevel())
	{
		DefaultFilename = GetFilename( InLevel );
	}
	else
	{
		DefaultFilename = FPackageName::LongPackageNameToFilename( InLevel->GetOutermost()->GetName() );
	}

	// We'll allow the map to be renamed when saving a level as a new file name this way
	const bool bAllowStreamingLevelRename = InLevel->IsPersistentLevel();

	return SaveAsImplementation( CastChecked<UWorld>(InLevel->GetOuter()), DefaultFilename, bAllowStreamingLevelRename, OutSavedFilename);
}

/**
 * Presents the user with a file dialog for importing.
 * If the import is not a merge (bMerging is false), AskSaveChanges() is called first.
 */
void FEditorFileUtils::Import()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FEditorFileUtils::Import);

	TArray<FString> OpenedFiles;
	FString DefaultLocation(GetDefaultDirectory());

	if (FileDialogHelpers::OpenFiles(NSLOCTEXT("UnrealEd", "ImportScene", "Import Scene").ToString(), GetFilterString(FI_ImportScene), DefaultLocation, EFileDialogFlags::None, OpenedFiles))
	{
		Import(OpenedFiles[0]);
	}
}

void FEditorFileUtils::Import(const FString& InFilename)
{
	const FScopedBusyCursor BusyCursor;

	USceneImportFactory *SceneFactory = nullptr;
	for (UClass* Class : TObjectRange<UClass>())
	{
		if (Class->IsChildOf<USceneImportFactory>() && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			USceneImportFactory* TestFactory = Class->GetDefaultObject<USceneImportFactory>();
			if (TestFactory->FactoryCanImport(InFilename))
			{
				/// Pick the first one for now 
				SceneFactory = TestFactory;
				break;
			}
		}

	}

	if (SceneFactory)
	{
		FString Path = "/Game";

		//Ask the user for the root path where they want to any content to be placed
		if(SceneFactory->ImportsAssets())
		{
			TSharedRef<SDlgPickPath> PickContentPathDlg =
				SNew(SDlgPickPath)
				.Title(LOCTEXT("ChooseImportRootContentPath", "Choose Location for importing the scene content"));


			if (PickContentPathDlg->ShowModal() == EAppReturnType::Cancel)
			{
				return;
			}

			Path = PickContentPathDlg->GetPath().ToString();
		}


		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<FString> Files;
		Files.Add(InFilename);

		const bool bSyncToBrowser = SceneFactory->ImportsAssets();
		AssetToolsModule.Get().ImportAssets(Files, Path, SceneFactory, bSyncToBrowser);
	}
	else
	{

		FFormatNamedArguments Args;

		Args.Add(TEXT("MapFilename"), FText::FromString(FPaths::GetCleanFilename(InFilename)));
		GWarn->BeginSlowTask(FText::Format(NSLOCTEXT("UnrealEd", "ImportingMap_F", "Importing map: {MapFilename}..."), Args), true);
		GEditor->Exec(GWorld, *FString::Printf(TEXT("MAP IMPORTADD FILE=\"%s\""), *InFilename));

		GWarn->EndSlowTask();
	}

	GEditor->RedrawLevelEditingViewports();

	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(InFilename)); // Save path as default for next time.

	FEditorDelegates::RefreshAllBrowsers.Broadcast();
}

void FEditorFileUtils::Export(bool bExportSelectedActorsOnly)
{
	// @todo: extend this to multiple levels.
	UWorld* World = GWorld;
	const FString LevelFilename = GetFilename( World );//->GetOutermost()->GetName() );
	FString ExportFilename;
	FString LastUsedPath = GetDefaultDirectory();
	if( FileDialogHelpers::SaveFile( NSLOCTEXT("UnrealEd", "Export", "Export").ToString(), GetFilterString(FI_ExportScene), LastUsedPath, FPaths::GetBaseFilename(LevelFilename), ExportFilename ) )
	{
		GEditor->ExportMap( World, *ExportFilename, bExportSelectedActorsOnly );
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(ExportFilename)); // Save path as default for next time.
	}
}

static bool IsCheckOutSelectedDisabled()
{
	return !(ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable());
}

bool FEditorFileUtils::AddCheckoutPackageItems(bool bCheckDirty, TArray<UPackage*> PackagesToCheckOut, TArray<UPackage*>* OutPackagesNotNeedingCheckout, bool* bOutHavePackageToCheckOut)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if (ISourceControlModule::Get().IsEnabled() && SourceControlProvider.IsAvailable())
	{
		TArray<UPackage*> SourceControlCheckPackages;
		if (CVarSkipSourceControlCheckForEditablePackages.GetValueOnAnyThread())
		{
			for (auto Package : PackagesToCheckOut)
			{
				if (!Package)
				{
					continue;
				}
				
				FString Filename;
				if (FPackageName::DoesPackageExist(Package->GetName(), NULL, &Filename))
				{
					if (IFileManager::Get().IsReadOnly(*Filename))
					{
						// check if the package is readonly
						SourceControlCheckPackages.Add(Package);
					}
					else
					{
						auto SourceControlState = SourceControlProvider.GetState(Package, EStateCacheUsage::Use);
						if (!SourceControlState)
						{
							// check if source control doesn't know about the package
							SourceControlCheckPackages.Add(Package);
						}
					}
				}
			}
		}
		else
		{
			SourceControlCheckPackages = PackagesToCheckOut;
		}
		
		if (SourceControlCheckPackages.Num())
		{
			// Update the source control status of all potentially relevant packages
			SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), SourceControlCheckPackages);
		}
	}

	FPackagesDialogModule& CheckoutPackagesDialogModule = FModuleManager::LoadModuleChecked<FPackagesDialogModule>(TEXT("PackagesDialog"));

	bool bPackagesAdded = false;
	bool bShowWarning = false;
	bool bOtherBranchWarning = false;
	bool bHavePackageToCheckOut = false;

	if (OutPackagesNotNeedingCheckout)
	{
		OutPackagesNotNeedingCheckout->Reset();
	}

	CheckoutPackagesDialogModule.RemoveAllPackageItems();

	// Iterate through all the packages and add them to the dialog if necessary.
	for (TArray<UPackage*>::TConstIterator PackageIter(PackagesToCheckOut); PackageIter; ++PackageIter)
	{
		UPackage* CurPackage = *PackageIter;
		FString Filename;
		// Assume the package is read only just in case we cant find a file
		bool bPkgReadOnly = true;
		bool bCareAboutReadOnly = SourceControlProvider.UsesLocalReadOnlyState();
		// Find the filename for this package
		bool bFoundFile = FPackageName::DoesPackageExist(CurPackage->GetName(), NULL, &Filename);
		if (bFoundFile)
		{
			// determine if the package file is read only
			bPkgReadOnly = IFileManager::Get().IsReadOnly(*Filename);
		}

		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(CurPackage, EStateCacheUsage::Use);

		// Package does not need to be checked out if its already checked out or we are ignoring it for source control
		bool bSCCCanEdit = !SourceControlState.IsValid() || SourceControlState->CanCheckIn() || SourceControlState->IsIgnored() || SourceControlState->IsUnknown() || (bCareAboutReadOnly && !bPkgReadOnly);
		bool bIsSourceControlled = SourceControlState.IsValid() && SourceControlState->IsSourceControlled();

		if (!bSCCCanEdit && (bIsSourceControlled && (!bCheckDirty || (bCheckDirty && CurPackage->IsDirty()))) && !SourceControlState->IsCheckedOut())
		{
			if (SourceControlState.IsValid() && (!SourceControlState->IsCurrent() || SourceControlState->IsCheckedOutOther()))
			{
				if (!PackagesNotToPromptAnyMore.Contains(CurPackage->GetName()))
				{
					if (!SourceControlState->IsCurrent())
					{
						// This package is not at the head revision and it should be ghosted as a result
						CheckoutPackagesDialogModule.AddPackageItem(CurPackage, ECheckBoxState::Unchecked, true, TEXT("SavePackages.SCC_DlgNotCurrent"), SourceControlState->GetDisplayTooltip().ToString());
					}
					else if (SourceControlState->IsCheckedOutOther())
					{
						// This package is checked out by someone else so it should be ghosted
						CheckoutPackagesDialogModule.AddPackageItem(CurPackage, ECheckBoxState::Unchecked, true, TEXT("SavePackages.SCC_DlgCheckedOutOther"), SourceControlState->GetDisplayTooltip().ToString());
					}
					bShowWarning = true;
					bPackagesAdded = true;
				}
				else
				{
					if (OutPackagesNotNeedingCheckout)
					{
						// File has already been made writable, just allow it to be saved without prompting
						OutPackagesNotNeedingCheckout->Add(CurPackage);
					}
				}
			}
			else
			{
				// Provided it's not in the list to not prompt any more, add it to the dialog
				if (!PackagesNotToPromptAnyMore.Contains(CurPackage->GetName()))
				{
					FText Tooltip = NSLOCTEXT("PackagesDialogModule", "Dlg_NotCheckedOutTip", "Not checked out");

					if (SourceControlState.IsValid())
					{
						if (SourceControlState->IsCheckedOutOrModifiedInOtherBranch())
						{
							bShowWarning = true;
							bOtherBranchWarning = true;
						}						

						Tooltip = SourceControlState->GetDisplayTooltip();
					}

					bHavePackageToCheckOut = true;
					//Add this package to the dialog if its not checked out, in the source control depot, dirty(if we are checking), and read only
					//This package could also be marked for delete, which we will treat as SCC_ReadOnly until it is time to check it out. At that time, we will revert it.
					CheckoutPackagesDialogModule.AddPackageItem(CurPackage, ECheckBoxState::Checked, false, TEXT("SavePackages.SCC_DlgReadOnly"), Tooltip.ToString());
					bPackagesAdded = true;
				}
				else if (OutPackagesNotNeedingCheckout)
				{
					// The current package doesn't need to be checked out in order to save as it's already writable.
					OutPackagesNotNeedingCheckout->Add(CurPackage);
				}
			}
		}
		else if (bPkgReadOnly && bFoundFile && (IsCheckOutSelectedDisabled() || !bCareAboutReadOnly))
		{
			const FText Tooltip = SourceControlState.IsValid() ? SourceControlState->GetDisplayTooltip() : NSLOCTEXT("PackagesDialogModule", "Dlg_NotCheckedOutTip", "Not checked out");

			// Don't disable the item if the server is available.  If the user updates source control within the dialog then the item should not be disabled so it can be checked out
			bool bIsDisabled = !ISourceControlModule::Get().IsEnabled();

			// This package is read only but source control is not available, show the dialog so users can save the package by making the file writable or by connecting to source control.
			// If we don't care about read-only state, we should allow the user to make the file writable whatever the state of source control.
			CheckoutPackagesDialogModule.AddPackageItem(CurPackage, ECheckBoxState::Unchecked, bIsDisabled, TEXT("SavePackages.SCC_DlgReadOnly"), Tooltip.ToString());
			PackagesNotToPromptAnyMore.Remove(CurPackage->GetName());
			bPackagesAdded = true;
		}
		else if (OutPackagesNotNeedingCheckout)
		{
			// The current package does not need to be checked out in order to save.
			OutPackagesNotNeedingCheckout->Add(CurPackage);
			PackagesNotToPromptAnyMore.Remove(CurPackage->GetName());
		}
	}

	if (bPackagesAdded)
	{
		if (bShowWarning)
		{
			if (!bOtherBranchWarning)
			{
				CheckoutPackagesDialogModule.SetWarning(
					NSLOCTEXT("PackagesDialogModule", "CheckoutPackagesWarnMessage", "Warning: There are modified assets which you will not be able to check out as they are locked or not at the head revision. You may lose your changes if you continue, as you will be unable to submit them to source control."));
			}
			else
			{
				CheckoutPackagesDialogModule.SetWarning(
					NSLOCTEXT("PackagesDialogModule", "CheckoutPackagesOtherBranchWarnMessage", "Warning: There are assets checked out or modified in another branch.  If you check out files in the current branch, you may lose your changes."));
			}
		}
		else
		{
			CheckoutPackagesDialogModule.SetWarning(FText::GetEmpty());
		}
	}

	if (bOutHavePackageToCheckOut)
	{
		*bOutHavePackageToCheckOut = bHavePackageToCheckOut;
	}

	return bPackagesAdded;
}

void FEditorFileUtils::UpdateCheckoutPackageItems(bool bCheckDirty, TArray<UPackage*> PackagesToCheckOut, TArray<UPackage*>* OutPackagesNotNeedingCheckout)
{
	AddCheckoutPackageItems(bCheckDirty, PackagesToCheckOut, OutPackagesNotNeedingCheckout, nullptr);
}

bool FEditorFileUtils::PromptToCheckoutPackages(bool bCheckDirty, const TArray<UPackage*>& PackagesToCheckOut, TArray<UPackage*>* OutPackagesCheckedOutOrMadeWritable, TArray<UPackage*>* OutPackagesNotNeedingCheckout, const bool bPromptingAfterModify )
{
	bool bResult = true;

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	
	// The checkout dialog to show users if any packages need to be checked out
	const FText DialogTitle = NSLOCTEXT("PackagesDialogModule", "CheckoutPackagesDialogTitle", "Check Out Assets");
	const FText DialogHeading = NSLOCTEXT("PackagesDialogModule", "CheckoutPackagesDialogMessage", "Select assets to check out.");

	FPackagesDialogModule& CheckoutPackagesDialogModule = FModuleManager::LoadModuleChecked<FPackagesDialogModule>( TEXT("PackagesDialog") );

	// Add any of the packages which do not report as editable by source control, yet are currently in the source control depot
	// If the user has specified to check for dirty packages, only add those which are dirty
	bool bPackagesAdded = false;

	// If we found at least one package that can be checked out, this will be true
	bool bHavePackageToCheckOut = false;

	const bool bReadOnly = false;
	const bool bAllowSourceControlConnection = true;
	CheckoutPackagesDialogModule.CreatePackagesDialog(
		DialogTitle,
		DialogHeading,
		bReadOnly,
		bAllowSourceControlConnection,
		FSimpleDelegate::CreateStatic(&UpdateCheckoutPackageItems, bCheckDirty, PackagesToCheckOut, OutPackagesNotNeedingCheckout)
		);

	// If we got here and we have one package, it's because someone explicitly saved the asset, therefore remove the package from the ignore list.
	if(PackagesToCheckOut.Num()==1)
	{
		const FString& PackageName = PackagesToCheckOut[0]->GetName();
		PackagesNotSavedDuringSaveAll.Remove(PackageName);
	}

	bPackagesAdded = AddCheckoutPackageItems(bCheckDirty, PackagesToCheckOut, OutPackagesNotNeedingCheckout, &bHavePackageToCheckOut);

	// If any packages were added to the dialog, show the dialog to the user and allow them to select which files to check out
	if ( bPackagesAdded )
	{
		TAttribute<bool> CheckOutSelectedDisabledAttrib;
		if( !bHavePackageToCheckOut && !IsCheckOutSelectedDisabled() )
		{
			// No packages to checkout and we are connected to the server
			CheckOutSelectedDisabledAttrib.Set( true );
		}
		else
		{
			// There may be packages to check out or we arent connected to the server. We'll determine if we enable the button via a delegate 
			CheckOutSelectedDisabledAttrib.BindStatic( &IsCheckOutSelectedDisabled );
		}
		
		// Prepare the buttons for the checkout dialog

		// The checkout button should be disabled if no packages can be checked out.
		CheckoutPackagesDialogModule.AddButton(DRT_CheckOut, NSLOCTEXT("PackagesDialogModule", "Dlg_CheckOutButtonp", "Check Out Selected"), NSLOCTEXT("PackagesDialogModule", "Dlg_CheckOutTooltip", "Attempt to Check Out Checked Assets"), CheckOutSelectedDisabledAttrib );
		
		// Make writable button to make checked files writable
		CheckoutPackagesDialogModule.AddButton(DRT_MakeWritable, NSLOCTEXT("PackagesDialogModule", "Dlg_MakeWritableButton", "Make Writable"), NSLOCTEXT("PackagesDialogModule", "Dlg_MakeWritableTooltip", "Makes selected files writable on disk"));

		// The cancel button should be different if we are prompting during a modify.
		const FText CancelButtonText  = bPromptingAfterModify ? NSLOCTEXT("PackagesDialogModule", "Dlg_AskMeLater", "Ask Me Later") : NSLOCTEXT("PackagesDialogModule", "Dlg_Cancel", "Cancel");
		const FText CancelButtonToolTip = bPromptingAfterModify ? NSLOCTEXT("PackagesDialogModule", "Dlg_AskMeLaterToolTip", "Don't ask again until this asset is saved") : NSLOCTEXT("PackagesDialogModule", "Dlg_CancelTooltip", "Cancel Request"); 
		CheckoutPackagesDialogModule.AddButton(DRT_Cancel, CancelButtonText, CancelButtonToolTip);

		// loop until a meaningful operation was performed (checked out successfully, made writable etc.)
		bool bPerformedOperation = false;
		while(!bPerformedOperation)
		{
			// Show the dialog and store the user's response
			EDialogReturnType UserResponse = CheckoutPackagesDialogModule.ShowPackagesDialog(PackagesNotSavedDuringSaveAll);
			// If the user has not cancelled out of the dialog
			if ( UserResponse == DRT_CheckOut )
			{
				// Get the packages that should be checked out from the user's choices in the dialog
				TArray<UPackage*> PkgsToCheckOut;
				CheckoutPackagesDialogModule.GetResults( PkgsToCheckOut, ECheckBoxState::Checked );

				if(CheckoutPackages(PkgsToCheckOut, OutPackagesCheckedOutOrMadeWritable) == ECommandResult::Cancelled)
				{
					CheckoutPackagesDialogModule.SetMessage(NSLOCTEXT("PackagesDialogModule", "CancelledCheckoutPackagesDialogMessage", "Check out operation was cancelled.\nSelect assets to make writable or try to check out again, right-click assets for more options."));
				}
				else
				{
					bPerformedOperation = true;
				}
			}
			else if( UserResponse == DRT_MakeWritable )
			{
				// Get the packages that should be made writable out from the user's choices in the dialog
				TArray<UPackage*> PkgsToMakeWritable;
				// Both undetermined and checked should be made writable.  Undetermined is only available when packages cant be checked out
				CheckoutPackagesDialogModule.GetResults( PkgsToMakeWritable, ECheckBoxState::Undetermined );
				CheckoutPackagesDialogModule.GetResults( PkgsToMakeWritable, ECheckBoxState::Checked);

				bool bPackageFailedWritable = false;
				FString PkgsWhichFailedWritable;

				// Attempt to make writable each package the user checked
				for( TArray<UPackage*>::TIterator PkgsToMakeWritableIter( PkgsToMakeWritable ); PkgsToMakeWritableIter; ++PkgsToMakeWritableIter )
				{
					UPackage* PackageToMakeWritable = *PkgsToMakeWritableIter;
					FString Filename;

					bool bFoundFile = FPackageName::DoesPackageExist( PackageToMakeWritable->GetName(), NULL, &Filename );
					if( bFoundFile )
					{
						// If we're ignoring the package due to the user ignoring it for saving, remove it from the ignore list
						// as getting here means we've explicitly decided to save the asset.
						PackagesNotSavedDuringSaveAll.Remove( PackageToMakeWritable->GetName() );

						// Get the fully qualified filename.
						const FString FullFilename = FPaths::ConvertRelativePathToFull(Filename);

						// Knock off the read only flag from the current file attributes
						if (FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false))
						{
							PackagesNotToPromptAnyMore.Add(PackageToMakeWritable->GetName());
							if (OutPackagesCheckedOutOrMadeWritable)
							{
								OutPackagesCheckedOutOrMadeWritable->Add(PackageToMakeWritable);
							}
						}
						else
						{
							bPackageFailedWritable = true;
							PkgsWhichFailedWritable += FString::Printf( TEXT("\n%s"), *PackageToMakeWritable->GetName() );
						}
					}
					else if (OutPackagesCheckedOutOrMadeWritable)
					{
						OutPackagesCheckedOutOrMadeWritable->Append(PackagesToCheckOut);
					}
				}

				if ( bPackageFailedWritable ) 
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Packages"), FText::FromString( PkgsWhichFailedWritable ));
					FText MessageFormatting = NSLOCTEXT("FileHelper", "FailedMakingWritableDlgMessageFormatting", "The following assets could not be made writable:{Packages}");
					FText Message = FText::Format( MessageFormatting, Arguments );

					FText Title = NSLOCTEXT("FileHelper", "FailedMakingWritableDlg_Title", "Unable to make assets writable");
					FMessageDialog::Open(EAppMsgType::Ok, Message, &Title);
				}

				bPerformedOperation = true;
			}
			else if (UserResponse == DRT_Save)
			{
				bResult = true;
				bPerformedOperation = true;
			}
			else if (UserResponse == DRT_Cancel || UserResponse == DRT_None)
			{
				// Handle the case of the user canceling out of the dialog
				bResult = false;
				bPerformedOperation = true;
			}
		}
	}

	// Update again to catch potentially new SCC states
	ISourceControlModule::Get().QueueStatusUpdate(PackagesToCheckOut);

	// If any files were just checked out, remove any pending flag to show a notification prompting for checkout.
	if (GUnrealEd && PackagesToCheckOut.Num() > 0)
	{
		for (UPackage* Package : PackagesToCheckOut)
		{
			GUnrealEd->PackageToNotifyState.Add(Package, NS_DialogPrompted);
		}
	}

	if (OutPackagesNotNeedingCheckout)
	{
		ISourceControlModule::Get().QueueStatusUpdate(*OutPackagesNotNeedingCheckout);
	}

	return bResult;
}

ECommandResult::Type FEditorFileUtils::CheckoutPackages(const TArray<UPackage*>& PkgsToCheckOut, TArray<UPackage*>* OutPackagesCheckedOut, const bool bErrorIfAlreadyCheckedOut, const bool bConfirmPackageBranchCheckOutStatus)
{
	const bool bErrorIfFileMissing = false;

	ECommandResult::Type CheckOutResult = ECommandResult::Succeeded;
	FString PkgsWhichFailedCheckout;

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	TArray<UPackage*> FinalPackageCheckoutList;
	TArray<UPackage*> FinalPackageMarkForAddList;

	// Source control may have been enabled in the package checkout dialog.
	// Ensure the status is up to date
	if(PkgsToCheckOut.Num() > 0)
	{
		CheckOutResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), PkgsToCheckOut);
	}
	
	if(CheckOutResult != ECommandResult::Cancelled)
	{
		// If any packages are checked out or modified in another branch, prompt for confirmation
		if (bConfirmPackageBranchCheckOutStatus && !ConfirmPackageBranchCheckOutStatus(PkgsToCheckOut))
		{
			return ECommandResult::Cancelled;
		}

		// Assemble a final list of packages to check out
		for( auto PkgsToCheckOutIter = PkgsToCheckOut.CreateConstIterator(); PkgsToCheckOutIter; ++PkgsToCheckOutIter )
		{
			UPackage* PackageToCheckOut = *PkgsToCheckOutIter;
			FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(PackageToCheckOut, EStateCacheUsage::Use);

			// If the file was marked for delete, revert it now so it can be checked out below
			if ( SourceControlState.IsValid() && SourceControlState->IsDeleted() )
			{
				SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), PackageToCheckOut);
				SourceControlState = SourceControlProvider.GetState(PackageToCheckOut, EStateCacheUsage::ForceUpdate);
			}

			// Mark the package for check out if possible
			bool bShowCheckoutError = true;
			if( SourceControlState.IsValid() )
			{
				if( SourceControlState->CanCheckout() )
				{
					bShowCheckoutError = false;
					FinalPackageCheckoutList.Add(PackageToCheckOut);
				}
				else if (SourceControlState->CanAdd())
				{
					// Cannot add unsaved packages to source control
					FString Filename;
					if (FPackageName::DoesPackageExist(PackageToCheckOut->GetName(), nullptr, &Filename))
					{
						bShowCheckoutError = false;
						FinalPackageMarkForAddList.Add(PackageToCheckOut);
					}
					else if (!bErrorIfFileMissing)
					{
						// Silently skip package that has not been saved yet
						// Expected when called by InternalCheckoutAndSavePackages before packages saved
						bShowCheckoutError = false;
					}
				}
				else if (SourceControlState->IsAdded())
				{
					if (!bErrorIfAlreadyCheckedOut)
					{
						bShowCheckoutError = false;
					}
				}
				else if( !bErrorIfAlreadyCheckedOut && SourceControlState->IsCheckedOut() && !SourceControlState->IsCheckedOutOther() )
				{
					bShowCheckoutError = false;
				}
			}

			// If the package couldn't be checked out, log it so the list of failures can be displayed afterwards
			if(bShowCheckoutError)
			{
				const FString PackageToCheckOutName = PackageToCheckOut->GetName();
				PkgsWhichFailedCheckout += FString::Printf( TEXT("\n%s"), *PackageToCheckOutName );
				CheckOutResult = ECommandResult::Failed;
			}
		}
	}

	// Attempt to check out each package the user specified to be checked out that is not read only
	if(FinalPackageCheckoutList.Num() > 0)
	{
		CheckOutResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), FinalPackageCheckoutList);
	}

	// Attempt to mark for add each package the user specified that is not already tracked by source control
	ECommandResult::Type MarkForAddResult = ECommandResult::Cancelled;
	if (FinalPackageMarkForAddList.Num() > 0)
	{
		MarkForAddResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), FinalPackageMarkForAddList);
	}

	TArray<UPackage*> CombinedPackageList = FinalPackageCheckoutList;
	CombinedPackageList.Append(FinalPackageMarkForAddList);

	if (CombinedPackageList.Num() > 0)
	{
		{
			// Checked out some or all files successfully, so check their state
			for (int32 i=0; i < CombinedPackageList.Num(); ++i)
			{
				const bool bCheckedOut = (i < FinalPackageCheckoutList.Num()) && (CheckOutResult != ECommandResult::Cancelled);
				const bool bMarkedForAdd = (i >= FinalPackageMarkForAddList.Num()) && (MarkForAddResult != ECommandResult::Cancelled);
				if (!(bCheckedOut || bMarkedForAdd))
				{
					continue;
				}

				UPackage* CurPackage = CombinedPackageList[i];

				// If we're ignoring the package due to the user ignoring it for saving, remove it from the ignore list
				// as getting here means we've explicitly decided to save the asset.
				const FString CurPackageName = CurPackage->GetName();
				PackagesNotSavedDuringSaveAll.Remove(CurPackageName);

				FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(CurPackage, EStateCacheUsage::Use);
				if (SourceControlState.IsValid() && (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded()))
				{
					if ( OutPackagesCheckedOut )
					{
						OutPackagesCheckedOut->Add(CurPackage);
					}
				}
				else
				{
					PkgsWhichFailedCheckout += FString::Printf( TEXT("\n%s"), *CurPackageName );
					CheckOutResult = ECommandResult::Failed;
				}
			}
		}
	}

	// If any packages failed the check out process, report them to the user so they know
	if ( !PkgsWhichFailedCheckout.IsEmpty() )
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Packages"), FText::FromString( PkgsWhichFailedCheckout ));
		FText MessageFormat = NSLOCTEXT("FileHelper", "FailedCheckoutDlgMessageFormatting", "The following assets could not be successfully checked out from source control:{Packages}");
		FText Message = FText::Format( MessageFormat, Arguments );

		FText Title = NSLOCTEXT("FileHelper", "FailedCheckoutDlg_Title", "Unable to Check Out From Source Control!");
		FMessageDialog::Open(EAppMsgType::Ok, Message, &Title);
	}

	return CheckOutResult;
}

ECommandResult::Type FEditorFileUtils::CheckoutPackages(const TArray<FString>& PkgsToCheckOut, TArray<FString>* OutPackagesCheckedOut, const bool bErrorIfAlreadyCheckedOut)
{
	ECommandResult::Type CheckOutResult = ECommandResult::Succeeded;
	FString PkgsWhichFailedCheckout;

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	// Source control may have been enabled in the package checkout dialog.
	// Ensure the status is up to date
	if(PkgsToCheckOut.Num() > 0)
	{
		// We have an array of package names, but the SCC needs an array of their corresponding filenames
		TArray<FString> PkgsToCheckOutFilenames;
		PkgsToCheckOutFilenames.Reserve(PkgsToCheckOut.Num());

		for( auto PkgsToCheckOutIter = PkgsToCheckOut.CreateConstIterator(); PkgsToCheckOutIter; ++PkgsToCheckOutIter )
		{
			const FString& PackageToCheckOutName = *PkgsToCheckOutIter;

			FString PackageFilename;
			if(FPackageName::DoesPackageExist(PackageToCheckOutName, nullptr, &PackageFilename))
			{
				PkgsToCheckOutFilenames.Add(PackageFilename);
			}
		}

		CheckOutResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), PkgsToCheckOutFilenames);
	}

	TArray<FString> FinalPackageCheckoutList;
	TArray<FString> FinalPackageMarkForAddList;
	if(CheckOutResult != ECommandResult::Cancelled)
	{
		// Assemble a final list of packages to check out
		for( auto PkgsToCheckOutIter = PkgsToCheckOut.CreateConstIterator(); PkgsToCheckOutIter; ++PkgsToCheckOutIter )
		{
			const FString& PackageToCheckOutName = *PkgsToCheckOutIter;

			// The SCC needs the filename
			FString PackageFilename;
			FPackageName::DoesPackageExist(PackageToCheckOutName, nullptr, &PackageFilename);

			FSourceControlStatePtr SourceControlState;
			if(!PackageFilename.IsEmpty())
			{
				SourceControlState = SourceControlProvider.GetState(PackageFilename, EStateCacheUsage::Use);
			}

			// If the file was marked for delete, revert it now so it can be checked out below
			if ( SourceControlState.IsValid() && SourceControlState->IsDeleted() )
			{
				SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), PackageFilename);
				SourceControlState = SourceControlProvider.GetState(PackageFilename, EStateCacheUsage::ForceUpdate);
			}

			// Mark the package for check out if possible
			bool bShowCheckoutError = true;
			if( SourceControlState.IsValid() )
			{
				if( SourceControlState->CanCheckout() )
				{
					bShowCheckoutError = false;
					FinalPackageCheckoutList.Add(PackageToCheckOutName);
				}
				else if (SourceControlState->CanAdd())
				{
					bShowCheckoutError = false;
					FinalPackageMarkForAddList.Add(PackageToCheckOutName);
				}
				else if( !bErrorIfAlreadyCheckedOut && SourceControlState->IsCheckedOut() && !SourceControlState->IsCheckedOutOther() )
				{
					bShowCheckoutError = false;
				}
			}

			// If the package couldn't be checked out, log it so the list of failures can be displayed afterwards
			if(bShowCheckoutError)
			{
				PkgsWhichFailedCheckout += FString::Printf( TEXT("\n%s"), *PackageToCheckOutName );
				CheckOutResult = ECommandResult::Failed;
			}
		}
	}

	// We have an array of package names, but the SCC needs an array of their corresponding filenames
	auto GetFilenamesFromPackageNames = [](const TArray<FString>& PackageNames)
	{
		TArray<FString> Filenames;
		Filenames.Reserve(PackageNames.Num());

		for (const FString& PackageName : PackageNames)
		{
			FString PackageFilename;
			if (FPackageName::DoesPackageExist(PackageName, nullptr, &PackageFilename))
			{
				Filenames.Add(PackageFilename);
			}
		}

		return Filenames;
	};

	// Attempt to check out each package the user specified to be checked out that is not read only
	if (FinalPackageCheckoutList.Num() > 0)
	{
		// We have an array of package names, but the SCC needs an array of their corresponding filenames
		TArray<FString> Filenames = GetFilenamesFromPackageNames(FinalPackageCheckoutList);
		CheckOutResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), Filenames);
	}

	// Attempt to mark for add each package the user specified not already tracked by source control
	ECommandResult::Type MarkForAddResult = ECommandResult::Succeeded;
	if (FinalPackageMarkForAddList.Num() > 0)
	{
		TArray<FString> Filenames = GetFilenamesFromPackageNames(FinalPackageMarkForAddList);
		MarkForAddResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), Filenames);
	}

	TArray<FString> CombinedPackageList = FinalPackageCheckoutList;
	CombinedPackageList.Append(FinalPackageMarkForAddList);

	if (CombinedPackageList.Num() > 0)
	{
		{
			// Checked out some or all files successfully, so check their state
			for (int32 i = 0; i < CombinedPackageList.Num(); ++i)
			{
				const bool bCheckedOut = (i < FinalPackageCheckoutList.Num()) && (CheckOutResult != ECommandResult::Cancelled);
				const bool bMarkedForAdd = (i >= FinalPackageCheckoutList.Num()) && (MarkForAddResult != ECommandResult::Cancelled);
				if (!(bCheckedOut || bMarkedForAdd))
				{
					continue;
				}

				const FString& CurPackageName = CombinedPackageList[i];

				// If we're ignoring the package due to the user ignoring it for saving, remove it from the ignore list
				// as getting here means we've explicitly decided to save the asset.
				PackagesNotSavedDuringSaveAll.Remove(CurPackageName);

				// The SCC needs the filename
				FString PackageFilename;
				FPackageName::DoesPackageExist(CurPackageName, nullptr, &PackageFilename);

				FSourceControlStatePtr SourceControlState;
				if(!PackageFilename.IsEmpty())
				{
					SourceControlState = SourceControlProvider.GetState(PackageFilename, EStateCacheUsage::Use);
				}

				if (SourceControlState.IsValid() && (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded()))
				{
					if ( OutPackagesCheckedOut )
					{
						OutPackagesCheckedOut->Add(CurPackageName);
					}
				}
				else
				{
					PkgsWhichFailedCheckout += FString::Printf( TEXT("\n%s"), *CurPackageName );
					CheckOutResult = ECommandResult::Failed;
				}
			}
		}
	}

	// If any packages failed the check out process, report them to the user so they know
	if ( CheckOutResult == ECommandResult::Failed )
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Packages"), FText::FromString( PkgsWhichFailedCheckout ));
		FText MessageFormat = NSLOCTEXT("FileHelper", "FailedCheckoutDlgMessageFormatting", "The following assets could not be successfully checked out from source control:{Packages}");
		FText Message = FText::Format( MessageFormat, Arguments );

		FText Title = NSLOCTEXT("FileHelper", "FailedCheckoutDlg_Title", "Unable to Check Out From Source Control!");
		FMessageDialog::Open(EAppMsgType::Ok, Message, &Title);
	}

	return CheckOutResult;
}

/**
 * Prompt the user with a check-box dialog allowing him/her to check out relevant level packages 
 * from source control
 *
 * @param	bCheckDirty					If true, non-dirty packages won't be added to the dialog
 * @param	SpecificLevelsToCheckOut	If specified, only the provided levels' packages will display in the
 *										dialog if they are under source control; If nothing is specified, all levels
 *										referenced by GWorld whose packages are under source control will be displayed
 * @param	OutPackagesNotNeedingCheckout	If not null, this array will be populated with packages that the user was not prompted about and do not need to be checked out to save.  Useful for saving packages even if the user canceled the checkout dialog.
 *
 * @return	true if the user did not cancel out of the dialog and has potentially checked out some files (or if there is
 *			no source control integration); false if the user cancelled the dialog
 */
bool FEditorFileUtils::PromptToCheckoutLevels(bool bCheckDirty, const TArray<ULevel*>& SpecificLevelsToCheckOut, TArray<UPackage*>* OutPackagesNotNeedingCheckout )
{
	bool bResult = true;

	// Only attempt to display the dialog and check out packages if source control integration is present
	TArray<UPackage*> PromptPackages;
	bool bPackagesAdded = false;

	// If levels were specified by the user, they should be the only ones considered potentially relevant
	for ( TArray<ULevel*>::TConstIterator SpecificLevelsIter( SpecificLevelsToCheckOut ); SpecificLevelsIter; ++SpecificLevelsIter )
	{
		UPackage* LevelsWorldPackage = ( *SpecificLevelsIter )->GetOutermost();
		check(LevelsWorldPackage);

		// If the user has specified to check if the package is dirty, do so before deeming
		// the package potentially relevant
		if (!bCheckDirty || LevelsWorldPackage->IsDirty())
		{
			PromptPackages.AddUnique( LevelsWorldPackage );
		}

		// When prompting for level check out, also add any dependent packages (i.e. external actors)
		for (UPackage* OwnedPackage : LevelsWorldPackage->GetExternalPackages())
		{
			if (OwnedPackage && (!bCheckDirty || OwnedPackage->IsDirty()))
			{
				PromptPackages.Add(OwnedPackage);
			}
		}
	}

	// Prompt the user with the provided packages if they prove to be relevant (i.e. in source control and not checked out)
	// Note: The user's dirty flag option is not passed in here because it's already been taken care of within the function (with a special case)
	bResult = FEditorFileUtils::PromptToCheckoutPackages( false, PromptPackages, nullptr, OutPackagesNotNeedingCheckout );

	return bResult;
}

/**
 * Overloaded version of PromptToCheckOutLevels which prompts the user with a check-box dialog allowing
 * him/her to check out the relevant level package if necessary
 *
 * @param	bCheckDirty				If true, non-dirty packages won't be added to the dialog
 * @param	SpecificLevelToCheckOut	The level whose package will display in the dialog if it is
 *									under source control
 *
 * @return	true if the user did not cancel out of the dialog and has potentially checked out some files (or if there is
 *			no source control integration); false if the user cancelled the dialog
 */
bool FEditorFileUtils::PromptToCheckoutLevels(bool bCheckDirty, ULevel* SpecificLevelToCheckOut)
{
	check( SpecificLevelToCheckOut != NULL );

	// Add the specified level to an array and use the other version of this function
	TArray<ULevel*> LevelsToCheckOut;
	LevelsToCheckOut.AddUnique( SpecificLevelToCheckOut );

	return FEditorFileUtils::PromptToCheckoutLevels( bCheckDirty, LevelsToCheckOut );	
}

void FEditorFileUtils::OpenLevelPickingDialog(const FOnLevelsChosen& OnLevelsChosen, const FOnLevelPickingCancelled& OnLevelPickingCancelled, bool bAllowMultipleSelection)
{
	struct FLocal
	{
		static void OnLevelsSelected(const TArray<FAssetData>& SelectedLevels, FOnLevelsChosen OnLevelsChosenDelegate)
		{
			if ( SelectedLevels.Num() > 0 )
			{
				// We selected a level. Save the path to this level to use as the default path next time we open.
				const FAssetData& FirstAssetData = SelectedLevels[0];
				
				// Convert from package name to filename. Add a trailing slash to prevent an invalid conversion when an asset is in a root folder (e.g. /Game)
				FString FilesystemPath = FPackageName::LongPackageNameToFilename(FirstAssetData.PackagePath.ToString() + TEXT("/"));;

				// Remove the slash if needed
				if ( FilesystemPath.EndsWith(TEXT("/"), ESearchCase::CaseSensitive) )
				{
					FilesystemPath.LeftChopInline(1, false);
				}

				FEditorDirectories::Get().SetLastDirectory(ELastDirectory::LEVEL, FilesystemPath);

				OnLevelsChosenDelegate.ExecuteIfBound(SelectedLevels);
			}
		}

		static void OnDialogCancelled(FOnLevelPickingCancelled OnLevelPickingCancelledDelegate)
		{
			OnLevelPickingCancelledDelegate.ExecuteIfBound();
		}
	};

	// Determine the starting path. Try to use the most recently used directory
	FString DefaultPath;
	{
		FString DefaultFilesystemDirectory = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::LEVEL);

		//ensure trailing "/" for directory name since TryConvertFilenameToLongPackageName expects one
		if(!DefaultFilesystemDirectory.IsEmpty() && !DefaultFilesystemDirectory.EndsWith("/"))
		{
			DefaultFilesystemDirectory.AppendChar(TEXT('/'));
		}

		if (DefaultFilesystemDirectory.IsEmpty() || !FPackageName::TryConvertFilenameToLongPackageName(DefaultFilesystemDirectory, DefaultPath))
		{
			// No saved path, just use a reasonable default
			DefaultPath = TEXT("/Game/Maps");
		}

		//OpenAssetDialog expects no trailing "/" so remove if necessary
		DefaultPath.RemoveFromEnd(TEXT("/"));
	}

	FOpenAssetDialogConfig OpenAssetDialogConfig;
	OpenAssetDialogConfig.DialogTitleOverride = LOCTEXT("OpenLevelDialogTitle", "Open Level");
	OpenAssetDialogConfig.DefaultPath = DefaultPath;
	OpenAssetDialogConfig.AssetClassNames.Add(UWorld::StaticClass()->GetFName());
	OpenAssetDialogConfig.bAllowMultipleSelection = bAllowMultipleSelection;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().CreateOpenAssetDialog(OpenAssetDialogConfig,
													 FOnAssetsChosenForOpen::CreateStatic(&FLocal::OnLevelsSelected, OnLevelsChosen),
													 FOnAssetDialogCancelled::CreateStatic(&FLocal::OnDialogCancelled, OnLevelPickingCancelled));
}

bool FEditorFileUtils::IsValidMapFilename(const FString& MapFilename, FText& OutErrorMessage)
{
	if( FPaths::GetExtension(MapFilename, true) != FPackageName::GetMapPackageExtension() )
	{
		OutErrorMessage = FText::Format( NSLOCTEXT("IsValidMapFilename", "FileIsNotAMap", "Filename does not have a {0} extension."), FText::FromString(FPackageName::GetMapPackageExtension()) );
		return false;
	}

	if( !FFileHelper::IsFilenameValidForSaving( MapFilename, OutErrorMessage ) )
	{
		return false;
	}

	// Make sure we can make a package name out of this file
	FString PackageName;
	if ( !FPackageName::TryConvertFilenameToLongPackageName(MapFilename, PackageName) )
	{
		TArray<FString> RootContentPaths;
		FPackageName::QueryRootContentPaths( RootContentPaths );

		const FString AbsoluteMapFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*MapFilename);
		TArray<FString> AbsoluteContentPaths;
		bool bValidPathButContainsInvalidCharacters = false;
		for( TArray<FString>::TConstIterator RootPathIt( RootContentPaths ); RootPathIt; ++RootPathIt )
		{
			const FString& RootPath = *RootPathIt;
			const FString& ContentFolder = FPackageName::LongPackageNameToFilename( RootPath );
			const FString AbsoluteContentFolder = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *ContentFolder );

			if ( AbsoluteMapFilePath.StartsWith(AbsoluteContentFolder) )
			{
				bValidPathButContainsInvalidCharacters = true;
			}

			AbsoluteContentPaths.Add(AbsoluteContentFolder);
			
		}

		if ( bValidPathButContainsInvalidCharacters )
		{
			FString InvalidCharacters = TEXT(".\\:");
			OutErrorMessage = FText::Format( NSLOCTEXT("IsValidMapFilename", "NotAValidPackage_InvalidCharacters", "The path contains at least one of these invalid characters below the content folder [{0}]"), FText::FromString(InvalidCharacters) );
		}
		else
		{
			FString ValidPathsString;
			for( TArray<FString>::TConstIterator RootPathIt( AbsoluteContentPaths ); RootPathIt; ++RootPathIt )
			{
				ValidPathsString += LINE_TERMINATOR;
				ValidPathsString += *RootPathIt;
			}

			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("LineTerminators"), FText::FromString(LINE_TERMINATOR));
			Arguments.Add(TEXT("ValidPaths"), FText::FromString(ValidPathsString));
			OutErrorMessage = FText::Format( NSLOCTEXT("IsValidMapFilename", "NotAValidPackage", "File is not in any of the following content folders:{LineTerminators}{ValidPaths}"), Arguments );
		}

		return false;
	}

	// Make sure the final package name contains no illegal characters
	{
		FName PackageFName(*PackageName);
		if ( !PackageFName.IsValidGroupName(OutErrorMessage) )
		{
			return false;
		}
	}

	// If there is a uasset file at the save location with the same name, this is an invalid filename
	const FString UAssetFilename = FPaths::GetBaseFilename(MapFilename, false) + FPackageName::GetAssetPackageExtension();
	if ( FPaths::FileExists(UAssetFilename) )
	{
		OutErrorMessage = NSLOCTEXT("IsValidMapFilename", "MapNameInUseByAsset", "Filename is in use by an asset file in the folder.");
		return false;
	}

	return true;
}

bool FEditorFileUtils::AttemptUnloadInactiveWorldPackage(UPackage* PackageToUnload, FText& OutErrorMessage)
{
	if ( ensure(PackageToUnload) )
	{
		UWorld* ExistingWorld = UWorld::FindWorldInPackage(PackageToUnload);
		if ( ExistingWorld )
		{
			bool bContinueUnloadingExistingWorld = false;

			switch (ExistingWorld->WorldType)
			{
				case EWorldType::None:
				case EWorldType::Inactive:
					// Untyped and inactive worlds are safe to unload
					bContinueUnloadingExistingWorld = true;
					break;

				case EWorldType::Editor:
					OutErrorMessage = NSLOCTEXT("SaveAsImplementation", "ExistingWorldNotInactive", "You can not unload a level you are currently editing.");
					bContinueUnloadingExistingWorld = false;
					break;

				case EWorldType::Game:
				case EWorldType::PIE:
				case EWorldType::EditorPreview:
				default:
					OutErrorMessage = NSLOCTEXT("SaveAsImplementation", "ExistingWorldInvalid", "The level you are attempting to unload is invalid.");
					bContinueUnloadingExistingWorld = false;
					break;
			}

			if ( !bContinueUnloadingExistingWorld )
			{
				return false;
			}
		}

		TArray<UPackage*> PackagesToUnload;
		PackagesToUnload.Add(PackageToUnload);
		TWeakObjectPtr<UPackage> WeakPackage = PackageToUnload;
		if (!UPackageTools::UnloadPackages(PackagesToUnload, OutErrorMessage))
		{
			return false;
		}

		if ( WeakPackage.IsValid() )
		{
			OutErrorMessage = NSLOCTEXT("SaveAsImplementation", "ExistingPackageFailedToUnload", "Failed to unload existing level.");
			return false;
		}
	}

	return true;
}

/**
 * Prompts the user to save the current map if necessary, the presents a load dialog and
 * loads a new map if selected by the user.
 */
bool FEditorFileUtils::LoadMap()
{
	if (GEditor->WarnIfLightingBuildIsCurrentlyRunning())
	{
		return false;
	}

	static bool bIsDialogOpen = false;

	struct FLocal
	{
		static void HandleLevelsChosen(const TArray<FAssetData>& SelectedAssets)
		{
			bIsDialogOpen = false;

			if ( SelectedAssets.Num() > 0 )
			{
				const FAssetData& AssetData = SelectedAssets[0];

				if (!GIsDemoMode)
				{
					// If there are any unsaved changes to the current level, see if the user wants to save those first.
					bool bPromptUserToSave = true;
					bool bSaveMapPackages = true;
					bool bSaveContentPackages = true;
					if (FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages) == false)
					{
						return;
					}
				}

				const FString FileToOpen = FPackageName::LongPackageNameToFilename(AssetData.PackageName.ToString(), FPackageName::GetMapPackageExtension());
				const bool bLoadAsTemplate = false;
				const bool bShowProgress = true;
				FEditorFileUtils::LoadMap(FileToOpen, bLoadAsTemplate, bShowProgress);
			}
		}

		static void HandleDialogCancelled()
		{
			bIsDialogOpen = false;
		}
	};
		
	if (!bIsDialogOpen)
	{
		bIsDialogOpen = true;
		const bool bAllowMultipleSelection = false;
		OpenLevelPickingDialog(FOnLevelsChosen::CreateStatic(&FLocal::HandleLevelsChosen),
								FOnLevelPickingCancelled::CreateStatic(&FLocal::HandleDialogCancelled),
								bAllowMultipleSelection);
	}

	return false; // TODO: Because OpenLevelPickingDialog is not modal, this always returned false. UE-55083 tracks making this return a proper value again.
}

static void NotifyBSPNeedsRebuild(const FString& PackageName)
{
	static TWeakPtr<SNotificationItem> NotificationPtr;

	auto RemoveNotification = []
	{
		TSharedPtr<SNotificationItem> Notification = NotificationPtr.Pin();
		if (Notification.IsValid())
		{
			Notification->SetEnabled(false);
			Notification->SetExpireDuration(0.0f);
			Notification->SetFadeOutDuration(0.5f);
			Notification->ExpireAndFadeout();
			NotificationPtr.Reset();
		}
	};

	// If there's still a notification present from the last time a map was loaded, get rid of it now.
	RemoveNotification();

	FNotificationInfo Info(LOCTEXT("BSPIssues", "Some issues were detected with BSP/Volume geometry in the loaded level or one of its sub-levels.\nThis is due to a fault in previous versions of the editor which has now been fixed, not user error.\nYou can choose to correct these issues by rebuilding the geometry now if you wish."));
	Info.bFireAndForget = true;
	Info.bUseLargeFont = false;
	Info.ExpireDuration = 25.0f;
	Info.FadeOutDuration = 0.5f;

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("RebuildGeometry", "Rebuild Geometry"),
		FText(),
		FSimpleDelegate::CreateLambda([&RemoveNotification]{
			TArray<TWeakObjectPtr<ULevel>> LevelsToRebuild;
			ABrush::NeedsRebuild(&LevelsToRebuild);
			for (const TWeakObjectPtr<ULevel>& Level : LevelsToRebuild)
			{
				if (Level.IsValid())
				{
					GEditor->RebuildLevel(*Level.Get());
				}
			}
			ABrush::OnRebuildDone();
			RemoveNotification();
		}),
		SNotificationItem::CS_None)
	);

	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("DontRebuild", "Don't Rebuild"),
		FText(),
		FSimpleDelegate::CreateLambda([&RemoveNotification]{
			RemoveNotification();
		}),
		SNotificationItem::CS_None)
	);

	Info.Hyperlink = FSimpleDelegate::CreateLambda([PackageName]{
		FMessageLog MessageLog("LoadErrors");
		MessageLog.NewPage(FText::Format(LOCTEXT("GeometryErrors", "Geometry errors from loading map '{0}'"), FText::FromString(PackageName)));

		TArray<TWeakObjectPtr<ULevel>> LevelsToRebuild;
		ABrush::NeedsRebuild(&LevelsToRebuild);
		for (const auto& Level : LevelsToRebuild)
		{
			if (Level.IsValid())
			{
				MessageLog.Message(EMessageSeverity::Info, FText::Format(LOCTEXT("GeometryErrorMap", "Level '{0}' has geometry with invalid normals."), FText::FromString(Level->GetOuter()->GetName())));
			}
		}

		MessageLog.Open();
	});
	Info.HyperlinkText = LOCTEXT("WhichLevels", "Which levels need a geometry rebuild?");

	NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
}

/**
 * Loads the specified map.  Does not prompt the user to save the current map.
 *
 * @param	InFilename		Map package filename, including path.
 *
 * @param	LoadAsTemplate	Forces the map to load into an untitled outermost package
 *							preventing the map saving over the original file.
 */
bool FEditorFileUtils::LoadMap(const FString& InFilename, bool LoadAsTemplate, bool bShowProgress)
{
	double LoadStartTime = FStudioAnalytics::GetAnalyticSeconds();
	
	if (GEditor->WarnIfLightingBuildIsCurrentlyRunning())
	{
		return false;
	}

	const FScopedBusyCursor BusyCursor;

	FString Filename( InFilename );

	FString LongMapPackageName;
	if ( FPackageName::IsValidLongPackageName(InFilename) )
	{
		LongMapPackageName = InFilename;
		FPackageName::TryConvertLongPackageNameToFilename(InFilename, Filename, FPackageName::GetMapPackageExtension());
	}
	else
	{
#if PLATFORM_WINDOWS
	{
		// Check if the Filename is actually from network drive and if so attempt to
		// resolve to local path (if it's pointing to local machine's shared folder)
		FString LocalFilename;
		if ( FWindowsPlatformProcess::ResolveNetworkPath( Filename, LocalFilename ) )
		{
			// Use local path if resolve succeeded
			Filename = FString( *LocalFilename );
		}
	}
#endif

		if ( !FPackageName::TryConvertFilenameToLongPackageName(Filename, LongMapPackageName) )
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("Editor", "MapLoad_FriendlyBadFilename", "Map load failed. The filename '{0}' is not within the game or engine content folders found in '{1}'."), FText::FromString(Filename), FText::FromString(FPaths::RootDir())));
			return false;
		}
	}

	// If a PIE world exists, warn the user that the PIE session will be terminated.
	// Abort if the user refuses to terminate the PIE session.
	if ( GEditor->ShouldAbortBecauseOfPIEWorld() )
	{
		return false;
	}

	// If a level is in memory but never saved to disk, warn the user that the level will be lost.
	if (GEditor->ShouldAbortBecauseOfUnsavedWorld())
	{
		return false;
	}

	// Save last opened level name.
	GConfig->SetString(TEXT("EditorStartup"), TEXT("LastLevel"), *LongMapPackageName, GEditorPerProjectIni);

	// Deactivate any editor modes when loading a new map
	GLevelEditorModeTools().DeactivateAllModes();

	FString LoadCommand = FString::Printf(TEXT("MAP LOAD FILE=\"%s\" TEMPLATE=%d SHOWPROGRESS=%d FEATURELEVEL=%d"), *Filename, LoadAsTemplate, bShowProgress, (int32)GEditor->DefaultWorldFeatureLevel);
	const bool bResult = GEditor->Exec( NULL, *LoadCommand );

	UWorld* World = GWorld;
	// In case the load failed after GWorld was torn down, default to a new blank map
	if( ( !World ) || ( bResult == false ) )
	{
		World = GEditor->NewMap();

		ResetLevelFilenames();

		return false;
	}

	World->IssueEditorLoadWarnings();

	ResetLevelFilenames();

	//only register the file if the name wasn't changed as a result of loading
	if (World->GetOutermost()->GetName() == LongMapPackageName)
	{
		RegisterLevelFilename( World, Filename );
	}

	if( !LoadAsTemplate )
	{
		// Don't set the last directory when loading the simple map or template as it is confusing to users
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(Filename)); // Save path as default for next time.
	}

	//ensure the name wasn't mangled during load before adding to the Recent File list
	if (World->GetOutermost()->GetName() == LongMapPackageName)
	{
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
		FMainMRUFavoritesList* MRUFavoritesList = MainFrameModule.GetMRUFavoritesList();
		if(MRUFavoritesList)
		{
			MRUFavoritesList->AddMRUItem(LongMapPackageName);
		}
	}

	FEditorDelegates::RefreshAllBrowsers.Broadcast();

	if( !GIsDemoMode )
	{
		// Check for deprecated actor classes.
		GEditor->Exec(World, TEXT("MAP CHECKDEP NOCLEARLOG"));
		FMessageLog("MapCheck").Open( EMessageSeverity::Warning );
	}

	// Track time spent loading map.
	const double MapLoadTime = FStudioAnalytics::GetAnalyticSeconds() - LoadStartTime;
	UE_LOG(LogFileHelpers, Log, TEXT("Loading map '%s' took %.3f"), *FPaths::GetBaseFilename(Filename), MapLoadTime);

	FStudioAnalytics::FireEvent_Loading(TEXT("LoadMap"), MapLoadTime, { FAnalyticsEventAttribute(TEXT("MapName"), FPaths::GetBaseFilename(Filename)) });

	if (GUnrealEd)
	{
		// Update volume actor visibility for each viewport since we loaded a level which could
		// potentially contain volumes.
		GUnrealEd->UpdateVolumeActorVisibility(NULL);

		// If there are any old mirrored brushes in the map with inverted polys, fix them here
		GUnrealEd->FixAnyInvertedBrushes(World);
	}

	// Request to rebuild BSP if the loading process flagged it as not up-to-date
	if (ABrush::NeedsRebuild())
	{
		NotifyBSPNeedsRebuild(LongMapPackageName);
	}

	// Fire delegate when a new map is opened, with name of map
	FEditorDelegates::OnMapOpened.Broadcast(InFilename, LoadAsTemplate);

	return bResult;
}

/**
 * Saves the specified map package, returning true on success.
 *
 * @param	World			The world to save.
 * @param	Filename		Map package filename, including path.
 *
 * @return					true if the map was saved successfully.
 */
bool FEditorFileUtils::SaveMap(UWorld* InWorld, const FString& Filename )
{
	bool bLevelWasSaved = false;

	// Disallow the save if in interpolation editing mode and the user doesn't want to exit interpolation mode.
	if ( !InInterpEditMode() )
	{
		const double SaveStartTime = FPlatformTime::Seconds();

		FString FinalFilename;
		bLevelWasSaved = SaveWorld( InWorld, &Filename,
									nullptr, nullptr,
									true, false,
									FinalFilename,
									false, false );

		// Track time spent saving map.
		UE_LOG(LogFileHelpers, Log, TEXT("Saving map '%s' took %.3f"), *FPaths::GetBaseFilename(Filename), FPlatformTime::Seconds() - SaveStartTime );
	}

	return bLevelWasSaved;
}


/**
 * Clears current level filename so that the user must SaveAs on next Save.
 * Called by NewMap() after the contents of the map are cleared.
 * Also called after loading a map template so that the template isn't overwritten.
 */
void FEditorFileUtils::ResetLevelFilenames()
{
	// Empty out any existing filenames.
	LevelFilenames.Empty();

	// Register a blank filename
	const FName PackageName(*GWorld->GetOutermost()->GetName());
	const FString EmptyFilename(TEXT(""));
	LevelFilenames.Add( PackageName, EmptyFilename );

	IMainFrameModule& MainFrameModule = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	MainFrameModule.SetLevelNameForWindowTitle(EmptyFilename);
}

bool FEditorFileUtils::AutosaveMap(const FString& AbsoluteAutosaveDir, const int32 AutosaveIndex, const bool bForceIfNotInList, const TSet< TWeakObjectPtr<UPackage> >& DirtyPackagesForAutoSave)
{
	auto Result = AutosaveMapEx(AbsoluteAutosaveDir, AutosaveIndex, bForceIfNotInList, DirtyPackagesForAutoSave);

	check(Result != EAutosaveContentPackagesResult::Failure);

	return Result == EAutosaveContentPackagesResult::Success;
}

EAutosaveContentPackagesResult::Type FEditorFileUtils::AutosaveMapEx(const FString& AbsoluteAutosaveDir, const int32 AutosaveIndex, const bool bForceIfNotInList, const TSet< TWeakObjectPtr<UPackage> >& DirtyPackagesForAutoSave)
{
	const FScopedBusyCursor BusyCursor;
	bool bResult  = false;
	double TotalSaveTime = 0.0f;

	double SaveStartTime = FPlatformTime::Seconds();

	// Clean up any old worlds.
	CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );

	FWorldContext& EditorContext = GEditor->GetEditorWorldContext();

	// Get the set of all reference worlds.
	TArray<UWorld*> WorldsArray;
	EditorLevelUtils::GetWorlds( EditorContext.World(), WorldsArray, true );

	if ( WorldsArray.Num() > 0 )
	{
		FString FinalFilename;
		for ( int32 WorldIndex = 0 ; WorldIndex < WorldsArray.Num() && FUnrealEdMisc::Get().GetAutosaveState() != FUnrealEdMisc::EAutosaveState::Cancelled ; ++WorldIndex )
		{
			UWorld* World = WorldsArray[ WorldIndex ];
			UPackage* Package = Cast<UPackage>( World->GetOuter() );
			check( Package );

			// If this world needs saving . . .
			if ( Package->IsDirty() && (bForceIfNotInList || DirtyPackagesForAutoSave.Contains(Package)) )
			{
				const FString AutosaveFilename = GetAutoSaveFilename(Package, AbsoluteAutosaveDir, AutosaveIndex, FPackageName::GetMapPackageExtension());
				//UE_LOG(LogFileHelpers, Log,  TEXT("Autosaving '%s'"), *AutosaveFilename );
				const bool bLevelWasSaved = SaveWorld( World, &AutosaveFilename,
					NULL, NULL,
					false, true,
					FinalFilename,
					true, false );

				// Remark the package as being dirty, as saving will have undiritied the package.
				Package->MarkPackageDirty();

				if( bLevelWasSaved == false && FUnrealEdMisc::Get().GetAutosaveState() != FUnrealEdMisc::EAutosaveState::Cancelled )
				{
					UE_LOG(LogFileHelpers, Log, TEXT("Editor autosave (incl. sublevels) failed for file '%s' which belongs to world '%s'. Aborting autosave."), *FinalFilename, *EditorContext.World()->GetOutermost()->GetName() );
					return EAutosaveContentPackagesResult::Failure;
				}

				bResult |= bLevelWasSaved;
			}

			// Now gather the world external packages and save them if needed
			if (World->PersistentLevel)
			{
				for (UPackage* ExternalPackage : World->PersistentLevel->GetLoadedExternalActorPackages())
				{
					if (ExternalPackage->IsDirty() && (bForceIfNotInList || DirtyPackagesForAutoSave.Contains(ExternalPackage))
						&& FPackageName::IsValidLongPackageName(ExternalPackage->GetName(), /*bIncludeReadOnlyRoots=*/false))
					{
						const FString AutosaveFilename = GetAutoSaveFilename(ExternalPackage, AbsoluteAutosaveDir, AutosaveIndex, FPackageName::GetAssetPackageExtension());
						if (!GEditor->Exec(nullptr, *FString::Printf(TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\" SILENT=false AUTOSAVING=true"), *ExternalPackage->GetName(), *AutosaveFilename)))
						{
							return EAutosaveContentPackagesResult::Failure;
						}

						// Re-mark the package as dirty, because autosaving it will have cleared the dirty flag
						ExternalPackage->MarkPackageDirty();
					}
				}
			}
		}

		// Track time spent saving map.
		double ThisTime = FPlatformTime::Seconds() - SaveStartTime;
		TotalSaveTime += ThisTime;
		UE_LOG(LogFileHelpers, Log, TEXT("Editor autosave (incl. external actors) for '%s' took %.3f"), *EditorContext.World()->GetOutermost()->GetName(), ThisTime  );
	}
	if( bResult == true )
	{
		UE_LOG(LogFileHelpers, Log, TEXT("Editor autosave (incl. sublevels & external actors) for all levels took %.3f"), TotalSaveTime );
	}
	return bResult ? EAutosaveContentPackagesResult::Success : EAutosaveContentPackagesResult::NothingToDo;
}

bool FEditorFileUtils::AutosaveContentPackages(const FString& AbsoluteAutosaveDir, const int32 AutosaveIndex, const bool bForceIfNotInList, const TSet< TWeakObjectPtr<UPackage> >& DirtyPackagesForAutoSave)
{
	auto Result = AutosaveContentPackagesEx(AbsoluteAutosaveDir, AutosaveIndex, bForceIfNotInList, DirtyPackagesForAutoSave);

	check(Result != EAutosaveContentPackagesResult::Failure);

	return Result == EAutosaveContentPackagesResult::Success;
}

EAutosaveContentPackagesResult::Type FEditorFileUtils::AutosaveContentPackagesEx(const FString& AbsoluteAutosaveDir, const int32 AutosaveIndex, const bool bForceIfNotInList, const TSet< TWeakObjectPtr<UPackage> >& DirtyPackagesForAutoSave)
{
	const FScopedBusyCursor BusyCursor;
	double SaveStartTime = FPlatformTime::Seconds();
	
	bool bSavedPkgs = false;
	const UPackage* TransientPackage = GetTransientPackage();
	
	TArray<UPackage*> PackagesToSave;

	// Check all packages for dirty, non-map, non-transient packages
	for ( TObjectIterator<UPackage> PackageIter; PackageIter; ++PackageIter )
	{
		UPackage* CurPackage = *PackageIter;
		// If the package is dirty and is not the transient package, we'd like to autosave it
		if ( CurPackage && ( CurPackage != TransientPackage ) && CurPackage->IsDirty() && (bForceIfNotInList || DirtyPackagesForAutoSave.Contains(CurPackage)) )
		{
			bool bSkipPackage = false;
			TArray<UObject*> ObjectsInPackage;
			GetObjectsWithPackage(CurPackage, ObjectsInPackage, false);
			for (auto ObjIt = ObjectsInPackage.CreateConstIterator(); ObjIt; ++ObjIt)
			{
				// Also, make sure this is not a map package
				if (Cast<UWorld>(*ObjIt))
				{
					bSkipPackage = true;
					break;
				}
				else if (Cast<UMapBuildDataRegistry>(*ObjIt))
				{
					// Do not auto save generated map build data packages
					bSkipPackage = true;
					break;
				}
				// handles external actor packages
				else if ((*ObjIt)->GetTypedOuter<UWorld>())
				{
					bSkipPackage = true;
					break;
				}
			}

			if (bSkipPackage)
			{
				continue;
			}

			// Ignore packages with long, invalid names. This culls out packages with paths in read-only roots such as /Temp.
			const bool bInvalidLongPackageName = !FPackageName::IsShortPackageName(CurPackage->GetFName()) && !FPackageName::IsValidLongPackageName(CurPackage->GetName(), /*bIncludeReadOnlyRoots=*/false);
				
			if ( !bInvalidLongPackageName )
			{
				PackagesToSave.Add(CurPackage);
			}
		}
	}

	FScopedSlowTask SlowTask(PackagesToSave.Num()*2, LOCTEXT("PerformingAutoSave_Caption", "Auto-saving out of date packages..."));

	for (UPackage* CurPackage : PackagesToSave)
	{
		SlowTask.DefaultMessage = FText::Format(LOCTEXT("AutoSavingPackage", "Saving package {0}"), FText::FromString(CurPackage->GetName()));
		SlowTask.EnterProgressFrame();

		// In order to save, the package must be fully-loaded first
		if( !CurPackage->IsFullyLoaded() )
		{
			CurPackage->FullyLoad();
		}

		SlowTask.EnterProgressFrame();

		const FString AutosaveFilename = GetAutoSaveFilename(CurPackage, AbsoluteAutosaveDir, AutosaveIndex, FPackageName::GetAssetPackageExtension());
		if (!GEditor->Exec(nullptr, *FString::Printf(TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\" SILENT=false AUTOSAVING=true"), *CurPackage->GetName(), *AutosaveFilename)))
		{
			return EAutosaveContentPackagesResult::Failure;
		}

		// Re-mark the package as dirty, because autosaving it will have cleared the dirty flag
		CurPackage->MarkPackageDirty();
		bSavedPkgs = true;
	}
	
	if ( bSavedPkgs )
	{	
		UE_LOG(LogFileHelpers, Log, TEXT("Auto-saving content packages took %.3f"), FPlatformTime::Seconds() - SaveStartTime );
	}

	return bSavedPkgs ? EAutosaveContentPackagesResult::Success : EAutosaveContentPackagesResult::NothingToDo;
}

enum class InternalSavePackageResult : int8
{
	Success,
	Cancel,
	Continue,
	Error,
};

/**
 * Actually save a package. Prompting for Save as if necessary
 *
 * @param PackageToSave					The package to save.
 * @param bUseDialog					If true, use the normal behavior.
 *										If false, do not prompt message dialog. If it can't save the package, skip it. If the package is a map and the name is not valid, skip it.
 * @param OutPackageLocallyWritable		Set to true if the provided package was locally writable but not under source control (of if source control is disabled).
 * @param SaveOutput					The output from the save process.
 * @return	InternalSavePackageResult::Success if package saving was a success
			InternalSavePackageResult::Continue if the package saving failed and the user doesn't want to retry
			InternalSavePackageResult::Cancel if the user wants to cancel everything
			InternalSavePackageResult::Error if an error occured. Check OutFailureReason
 */
static InternalSavePackageResult InternalSavePackage(UPackage* PackageToSave, bool bUseDialog, bool& bOutPackageLocallyWritable, FOutputDevice &SaveOutput)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(InternalSavePackage);

	// What we will be returning. Assume for now that everything will go fine
	InternalSavePackageResult ReturnCode = InternalSavePackageResult::Error;

	// Assume the package is locally writable in case SCC is disabled; if SCC is enabled, it will
	// correctly set this value later
	bOutPackageLocallyWritable = true;

	bool bShouldRetrySave = true;
	UWorld*	AssociatedWorld	= UWorld::FindWorldInPackage(PackageToSave);
	// Redirector to world saves with file extension for maps
	const bool bSavingRedirectorToWorld = !AssociatedWorld && UWorld::FollowWorldRedirectorInPackage(PackageToSave);
	const bool bIsMapPackage = AssociatedWorld != NULL || bSavingRedirectorToWorld;

	// The name of the package
	const FString PackageName = PackageToSave->GetName();

	// Place were we should save the file, including the filename
	FString FinalPackageSavePath;
	// Just the filename
	FString FinalPackageFilename;

	// True if we should attempt saving
	bool bAttemptSave = false;

	// If the package already has a valid path to a non read-only location, use it to determine where the file should be saved
	const bool bIncludeReadOnlyRoots = false;
	const bool bIsValidPath = FPackageName::IsValidLongPackageName(PackageName, bIncludeReadOnlyRoots);
	if( bIsValidPath )
	{
		bAttemptSave = true;

		FString ExistingFilename;
		const bool bPackageAlreadyExists = FPackageName::DoesPackageExist(PackageName, NULL, &ExistingFilename);
		if (!bPackageAlreadyExists)
		{
			// Construct a filename from long package name.
			const FString& FileExtension = bIsMapPackage ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
			ExistingFilename = FPackageName::LongPackageNameToFilename(PackageName, FileExtension);

			// Check if we can use this filename.
			FText ErrorText;
			if (!FFileHelper::IsFilenameValidForSaving(ExistingFilename, ErrorText))
			{
				// Display the error (already localized) and exit gracefully.
				FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
				bAttemptSave = false;
			}
		}

		if (bAttemptSave)
		{
			// The file already exists, no need to prompt for save as
			FString BaseFilename, Extension, Directory;
			// Split the path to get the filename without the directory structure
			FPaths::NormalizeFilename(ExistingFilename);
			FPaths::Split(ExistingFilename, Directory, BaseFilename, Extension);
			// The final save path is whatever the existing filename is
			FinalPackageSavePath = ExistingFilename;
			// Format the filename we found from splitting the path
			FinalPackageFilename = FString::Printf( TEXT("%s.%s"), *BaseFilename, *Extension );
		}
	}
	else if ( bUseDialog && bIsMapPackage )	// don't do a SaveAs dialog if dialogs was not requested
	{
		// @todo Only maps should be allowed to change names at save time, for now.
		// If this changes, there must be generic code to rename assets to the new name BEFORE saving to disk.
		// Right now, all of this code is specific to maps

		// There wont be a "not checked out from SCC but writable on disk" conflict if the package is new.
		bOutPackageLocallyWritable = false;

		// Make a list of file types
		// We have to ask for save as.
		FString FileTypes;
		FText SavePackageText;

		if( bIsMapPackage )
		{
			FileTypes = FEditorFileUtils::GetFilterString(FI_Save);
			FinalPackageFilename = FString::Printf( TEXT("Untitled%s"), *FPackageName::GetMapPackageExtension() );
			SavePackageText = NSLOCTEXT("UnrealEd", "SaveMap", "Save Map");
		}
		else
		{
			FileTypes = FString::Printf( TEXT("(*%s)|*%s"), *FPackageName::GetAssetPackageExtension(), *FPackageName::GetAssetPackageExtension() );
			FinalPackageFilename = FString::Printf( TEXT("%s%s"), *PackageToSave->GetName(), *FPackageName::GetAssetPackageExtension() );
			SavePackageText = NSLOCTEXT("UnrealEd", "SaveAsset", "Save Asset");
		}

		// The number of times the user pressed cancel
		int32 NumSkips = 0;

		// If the user presses cancel more than this time, they really don't want to save the file
		const int32 NumSkipsBeforeAbort = 1;

		// if the user hit cancel on the Save dialog, ask again what the user wants to do, 
		// we shouldn't assume they want to skip the file
		// This loop continues indefinitely if the user does not supply a valid filename.  They must supply a valid filename or press cancel
		const FString Directory = *GetDefaultDirectory();
		while( NumSkips < NumSkipsBeforeAbort )
		{
			FString DefaultLocation = Directory;
			FString DefaultPackagePath;
			if (!FPackageName::TryConvertFilenameToLongPackageName(DefaultLocation / FinalPackageFilename, DefaultPackagePath))
			{
				// Original location is invalid; set default location to /Game/Maps
				DefaultLocation = FPaths::ProjectContentDir() / TEXT("Maps");
				ensure(FPackageName::TryConvertFilenameToLongPackageName(DefaultLocation / FinalPackageFilename, DefaultPackagePath));
			}

			FString SaveAsPackageName;
			bool bSaveFile = OpenSaveAsDialog(
				UWorld::StaticClass(),
				FPackageName::GetLongPackagePath(DefaultPackagePath),
				FPaths::GetBaseFilename(FinalPackageFilename),
				SaveAsPackageName);

			if (bSaveFile)
			{
				// Leave out the extension. It will be added below.
				FinalPackageFilename = FPackageName::LongPackageNameToFilename(SaveAsPackageName);
			}

			if( bSaveFile )
			{
				// If the supplied file name is missing an extension then give it the default package
				// file extension.
				if( FinalPackageFilename.Len() > 0 && FPaths::GetExtension(FinalPackageFilename).Len() == 0 )
				{
					FinalPackageFilename += bIsMapPackage ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
				}
			
				FText ErrorMessage;
				bool bValidFilename = FFileHelper::IsFilenameValidForSaving( FinalPackageFilename, ErrorMessage );
				if ( bValidFilename )
				{
					bValidFilename = bIsMapPackage ? FEditorFileUtils::IsValidMapFilename( FinalPackageFilename, ErrorMessage ) : FPackageName::IsValidLongPackageName( FinalPackageFilename, false, &ErrorMessage );
				}

				if ( bValidFilename )
				{
					// If there is an existing world in memory that shares this name unload it now to prepare for overwrite.
					// Don't do this if we are using save as to overwrite the current level since it will just save naturally.
					const FString NewPackageName = FPackageName::FilenameToLongPackageName(FinalPackageFilename);
					UPackage* ExistingPackage = FindPackage(nullptr, *NewPackageName);
					if (ExistingPackage && ExistingPackage != PackageToSave)
					{
						bValidFilename = FEditorFileUtils::AttemptUnloadInactiveWorldPackage(ExistingPackage, ErrorMessage);
					}
				}

				if ( !bValidFilename )
				{
					// Start the loop over, prompting for save again
					const FText DisplayFilename = FText::FromString( IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *FinalPackageFilename ) );
					FFormatNamedArguments Arguments;
					Arguments.Add( TEXT("Filename"), DisplayFilename );
					Arguments.Add( TEXT("LineTerminators"), FText::FromString( LINE_TERMINATOR LINE_TERMINATOR ) );
					Arguments.Add( TEXT("ErrorMessage"), ErrorMessage );
					const FText DisplayMessage = FText::Format( LOCTEXT( "InvalidSaveFilename", "Failed to save to {Filename}{LineTerminators}{ErrorMessage}" ), Arguments );
					FMessageDialog::Open( EAppMsgType::Ok, DisplayMessage );

					// Start the loop over, prompting for save again
					continue;
				}
				else
				{
					FinalPackageSavePath = FinalPackageFilename;
					// Stop looping, we successfully got a valid path and filename to save
					bAttemptSave = true;
					break;
				}
			}
			else
			{
				// if the user hit cancel on the Save dialog, ask again what the user wants to do, 
				// we shouldn't assume they want to skip the file unless they press cancel several times
				++NumSkips;
				if( NumSkips == NumSkipsBeforeAbort )
				{
					// They really want to stop
					ReturnCode = InternalSavePackageResult::Cancel;
				}
			}
		}
	}

	// attempt the save

	while( bAttemptSave )
	{
		bool bWasSuccessful = false;
		// Note: Redirector to world uses SAVEPACKAGE instead of SaveMap
		if (bIsMapPackage && !bSavingRedirectorToWorld)
		{
			// have a Helper attempt to save the map
			SaveOutput.Log("LogFileHelpers", ELogVerbosity::Log, FString::Printf(TEXT("Saving Map: %s"), *PackageName));
			bWasSuccessful = FEditorFileUtils::SaveMap( AssociatedWorld, FinalPackageSavePath );
		}
		else
		{
			// normally, we just save the package
			SaveOutput.Log("LogFileHelpers", ELogVerbosity::Log, FString::Printf(TEXT("Saving Package: %s"), *PackageName));
			bWasSuccessful = GEngine->Exec( NULL, *FString::Printf( TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\" SILENT=true"), *PackageName, *FinalPackageSavePath ), SaveOutput );
		}

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		if (ISourceControlModule::Get().IsEnabled())
		{
			// Assume the package was correctly checked out from SCC
			bOutPackageLocallyWritable = false;

			// Trusting the SCC status in the package file cache to minimize network activity during save.
			const FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(PackageToSave, EStateCacheUsage::Use);
			// If the package is in the depot, and not recognized as editable by source control, and not read-only, then we know the user has made the package locally writable!
			const bool bSCCCanEdit = !SourceControlState.IsValid() || SourceControlState->CanCheckIn() || SourceControlState->IsIgnored() || SourceControlState->IsUnknown();
			const bool bSCCIsCheckedOut = SourceControlState.IsValid() && SourceControlState->IsCheckedOut();
			const bool bInDepot = SourceControlState.IsValid() && SourceControlState->IsSourceControlled();
			if ( !bSCCCanEdit && bInDepot && !IFileManager::Get().IsReadOnly( *FinalPackageSavePath ) && SourceControlProvider.UsesLocalReadOnlyState() && !bSCCIsCheckedOut )
			{
				bOutPackageLocallyWritable = true;
			}
		}
		else
		{
			// If source control is disabled then we don't care if the package is locally writable
			bOutPackageLocallyWritable = false;
		}

		// Handle all failures the same way.
		if ( bUseDialog && !bWasSuccessful )
		{
			// ask the user what to do if we failed
			const FText ErrorPrompt = GEditor->IsPlayingOnLocalPCSession() ?
				NSLOCTEXT("UnrealEd", "Prompt_41", "The asset '{0}' ({1}) cannot be saved as the package is locked because you are in play on PC mode.\n\nCancel: Stop saving all assets and return to the editor.\nRetry: Attempt to save the asset again.\nContinue: Skip saving this asset only." ) :
				NSLOCTEXT("UnrealEd", "Prompt_26", "The asset '{0}' ({1}) failed to save.\n\nCancel: Stop saving all assets and return to the editor.\nRetry: Attempt to save the asset again.\nContinue: Skip saving this asset only." );
			EAppReturnType::Type DialogCode = FMessageDialog::Open( EAppMsgType::CancelRetryContinue, EAppReturnType::Continue, FText::Format(ErrorPrompt, FText::FromString(PackageName), FText::FromString(FinalPackageFilename)) );

			switch (DialogCode)
			{
			case EAppReturnType::Cancel:
				// if this happens, the user wants to stop everything
				bAttemptSave = false;
				ReturnCode = InternalSavePackageResult::Cancel;
				break;
			case EAppReturnType::Retry:
				bAttemptSave = true;
				break;
			case EAppReturnType::Continue:
				ReturnCode = InternalSavePackageResult::Continue;// this is if it failed to save, but the user wants to skip saving it
				bAttemptSave = false;
				break;
			default:
				// Should not get here
				check(0);
				break;
			}
		}
		else if ( !bWasSuccessful )
		{
			// We failed at saving because we are in bIsUnattended mode, there is no need to attempt to save again
			FText FailureReason = FText::Format(NSLOCTEXT("UnrealEd", "SaveAssetFailed", "The asset '{0}' ({1}) failed to save."), FText::FromString(PackageName), FText::FromString(FinalPackageFilename));
			FMessageDialog::Open( EAppMsgType::Ok, FailureReason );
			bAttemptSave = false;
			ReturnCode = InternalSavePackageResult::Error;
		}
		else
		{
			// If we were successful at saving, there is no need to attempt to save again
			bAttemptSave = false;
			ReturnCode = InternalSavePackageResult::Success;
		}
	}

	return ReturnCode;
}

/**
 * Shows a dialog warning a user about packages which failed to save
 * 
 * @param Packages that should be displayed in the dialog
 */
static void InternalWarnUserAboutFailedSave( const TArray<UPackage*>& InFailedPackages, bool bUseDialog )
{
	// Warn the user if any packages failed to save
	if ( InFailedPackages.Num() > 0 )
	{
		FString FailedPackages;
		for ( TArray<UPackage*>::TConstIterator FailedIter( InFailedPackages ); FailedIter; ++FailedIter )
		{
			FailedPackages += FString::Printf( TEXT("\n%s"), *( (*FailedIter)->GetName() ) );
		}

		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Packages"), FText::FromString( FailedPackages ));
		FText MessageFormatting = NSLOCTEXT("FileHelper", "FailedSavePromptMessageFormatting", "The following assets failed to save correctly:{Packages}");
		FText Message = FText::Format( MessageFormatting, Arguments );

		// Display warning
		FText Title = NSLOCTEXT("FileHelper", "FailedSavePrompt_Title", "Packages Failed To Save");
		FMessageDialog::Open(EAppMsgType::Ok, Message, &Title);
	}
}

static TArray<UPackage*> InternalGetDirtyPackages(const bool bSaveMapPackages, const bool bSaveContentPackages)
{
	if (bSaveContentPackages)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	// A list of all packages that need to be saved
	TArray<UPackage*> PackagesToSave;

	if (bSaveMapPackages)
	{
		FEditorFileUtils::GetDirtyWorldPackages(PackagesToSave);
	}

	// Don't iterate through content packages if we don't plan on saving them
	if (bSaveContentPackages)
	{
		FEditorFileUtils::GetDirtyContentPackages(PackagesToSave);
	}

	return PackagesToSave;
}

static void InternalNotifyNoPackagesSaved(const bool bUseDialog)
{
	if (bUseDialog)
	{
		FNotificationInfo NotificationInfo(LOCTEXT("NoAssetsToSave", "No new changes to save!"));
		NotificationInfo.Image = FEditorStyle::GetBrush(FTokenizedMessage::GetSeverityIconName(EMessageSeverity::Info));
		NotificationInfo.bFireAndForget = true;
		NotificationInfo.ExpireDuration = 4.0f; // Need this message to last a little longer than normal since the user may have expected there to be modified files.
		NotificationInfo.bUseThrobber = true;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
	else
	{
		UE_LOG(LogFileHelpers, Log, TEXT("%s"), *LOCTEXT("NoAssetsToSave", "No new changes to save!").ToString());
	}
}


/*
 * @param bUseDialog					If true, use the normal behavior.
 *										If false, do not prompt message dialog. If it can't save the package, skip it. If the package is a map and the name is not valid, skip it.
 * @param	bShowDialogIfError			If InternalSavePackage failed, tell the user with a Dialog
 * @param	OutFailedPackages			Packages that failed to save
 */
static bool InternalSavePackagesFast(const TArray<UPackage*>& PackagesToSave, bool bUseDialog, TArray<UPackage*>& OutFailedPackages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(InternalSavePackagesFast);

	bool bReturnCode = true;

	FSaveErrorOutputDevice SaveErrors;
	GWarn->BeginSlowTask(NSLOCTEXT("UnrealEd", "SavingPackagesE", "Saving packages..."), true);

	TArray<UPackage*> PackagesToClean;
	for (TArray<UPackage*>::TConstIterator PkgIter(PackagesToSave); PkgIter; ++PkgIter)
	{
		UPackage* CurPackage = *PkgIter;

		// Check if a file exists for this package
		FString Filename;
		bool bFoundFile = FPackageName::DoesPackageExist(CurPackage->GetName(), NULL, &Filename);
		if (bFoundFile)
		{
			// determine if the package file is read only
			const bool bPkgReadOnly = IFileManager::Get().IsReadOnly(*Filename);

			// Only save writable files in fast mode
			if (!bPkgReadOnly)
			{
				if (!CurPackage->IsFullyLoaded())
				{
					// Packages must be fully loaded to save
					CurPackage->FullyLoad();
				}

				const UWorld* const AssociatedWorld = UWorld::FindWorldInPackage(CurPackage);
				const bool bIsMapPackage = AssociatedWorld != nullptr;

				const FText SavingPackageText = (bIsMapPackage)
					? FText::Format(NSLOCTEXT("UnrealEd", "SavingMapf", "Saving map {0}"), FText::FromString(CurPackage->GetName()))
					: FText::Format(NSLOCTEXT("UnrealEd", "SavingAssetf", "Saving asset {0}"), FText::FromString(CurPackage->GetName()));

				GWarn->StatusForceUpdate(PkgIter.GetIndex(), PackagesToSave.Num(), SavingPackageText);

				// Save the package

				// if the package we are saving is considered empty, mark it for deletion on disk instead
				if (UPackage::IsEmptyPackage(CurPackage))
				{
					PackagesToClean.Add(CurPackage);
				}
				// Otherwise, save as usual
				else
				{
					bool bPackageLocallyWritable;
					const InternalSavePackageResult SaveStatus = InternalSavePackage(CurPackage, bUseDialog, bPackageLocallyWritable, SaveErrors);

					if (SaveStatus == InternalSavePackageResult::Cancel)
					{
						// we don't want to pop up a message box about failing to save packages if they cancel
						// instead warn here so there is some trace in the log and also unattended builds can find it
						UE_LOG(LogFileHelpers, Warning, TEXT("Cancelled saving package %s"), *CurPackage->GetName());
					}
					else if (SaveStatus == InternalSavePackageResult::Continue || SaveStatus == InternalSavePackageResult::Error)
					{
						// The package could not be saved so add it to the failed array 
						OutFailedPackages.Add(CurPackage);

						if (SaveStatus == InternalSavePackageResult::Error)
						{
							// exit gracefully.
							bReturnCode = false;
						}
					}
				}
			}
		}
	}

	// if we have
	if (PackagesToClean.Num() > 0)
	{
		ObjectTools::CleanupAfterSuccessfulDelete(PackagesToClean, true);
	}
	
	GWarn->EndSlowTask();
	SaveErrors.Flush();

	return bReturnCode;
}

/*
 * @param	bPromptUserToSave			true if we should prompt the user to save dirty packages we found. false to assume all dirty packages should be saved.  Regardless of this setting the user will be prompted for checkout(if needed) unless bFastSave is set
 * @param	bFastSave					true if we should do a fast save. (I.E don't prompt the user to save, don't prompt for checkout, and only save packages that are currently writable).  Note: Still prompts for SaveAs if a package needs a filename
 * @param	bCanBeDeclined				true if the user prompt should contain a "Don't Save" button in addition to "Cancel", which won't result in a failure return code.
 */
static bool InternalSavePackages(const TArray<UPackage*>& PackagesToSave, bool bPromptUserToSave, bool bFastSave, bool bCanBeDeclined)
{
	bool bReturnCode = true;

	if (!bFastSave)
	{
		const bool bCheckDirty = true;
		const bool bAlreadyCheckedOut = false;
		const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptUserToSave, nullptr, bAlreadyCheckedOut, bCanBeDeclined);
		if (Return == FEditorFileUtils::EPromptReturnCode::PR_Cancelled)
		{
			// Only cancel should return false and stop whatever we were doing before.(like closing the editor)
			// If failure is returned, the user was given ample times to retry saving the package and didn't want to
			// So we should continue with whatever we were doing.  
			bReturnCode = false;
		}
	}
	else
	{
		const bool bUseDialog = true;
		TArray<UPackage*> FailedPackages;
		bReturnCode = InternalSavePackagesFast(PackagesToSave, bUseDialog, FailedPackages);
		// Warn the user about any packages which failed to save.
		InternalWarnUserAboutFailedSave(FailedPackages, bUseDialog);
	}
	return bReturnCode;
}

void FEditorFileUtils::SaveMapDataPackages(UWorld* WorldToSave, bool bCheckDirty, bool bSaveExternal)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FEditorFileUtils_SaveMapDataPackages);

	TArray<UPackage*> PackagesToSave;
	UPackage* WorldPackage = WorldToSave->GetOutermost();

	if (!WorldPackage->HasAnyPackageFlags(PKG_PlayInEditor)
		&& !WorldPackage->HasAnyFlags(RF_Transient))
	{
		ULevel* Level = WorldToSave->PersistentLevel;
		if (Level->MapBuildData)
		{
			UPackage* BuiltDataPackage = Level->MapBuildData->GetOutermost();

			if (BuiltDataPackage != WorldPackage)
			{
				PackagesToSave.Add(BuiltDataPackage);
			}
		}

		if (bSaveExternal)
		{
			PackagesToSave.Append(WorldPackage->GetExternalPackages());
		}
	}
			
	if (PackagesToSave.Num() > 0)
	{
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, false, nullptr, false, false);
	}
}

/**
 * Saves the specified level.  SaveAs is performed as necessary.
 *
 * @param	Level				The level to be saved.
 * @param	DefaultFilename		File name to use for this level if it doesn't have one yet (or empty string to prompt)
 *
 * @return				true if the level was saved.
 */
bool FEditorFileUtils::SaveLevel(ULevel* Level, const FString& DefaultFilename, FString* OutSavedFilename )
{
	bool bLevelWasSaved = false;

	// Disallow the save if in interpolation editing mode and the user doesn't want to exit interpolation mode.
	if ( Level && !InInterpEditMode() )
	{
		// Check and see if this is a new map.
		const bool bIsPersistentLevelCurrent = Level->IsPersistentLevel();

		// If the user trying to save the persistent level?
		if ( bIsPersistentLevelCurrent )
		{
			// Check to see if the persistent level is a new map (ie if it has been saved before).
			FString Filename = GetFilename( Level->OwningWorld );
			if( !Filename.Len() )
			{
				// No file name, provided, so use the default file name we were given if we have one
				Filename = FString( DefaultFilename );
			}

			if( !Filename.Len() )
			{
				if (GIsRunningUnattendedScript) // prevent modal if running in Unattended Script mode
				{
					return false;
				}
				else
				{
					// Present the user with a SaveAs dialog.
					const bool bAllowStreamingLevelRename = false;
					bLevelWasSaved = SaveAsImplementation(Level->OwningWorld, Filename, bAllowStreamingLevelRename, OutSavedFilename);
					return bLevelWasSaved;
				}
			}
		}

		////////////////////////////////
		// At this point, we know the level we're saving has been saved before,
		// so don't bother checking the filename.

		UWorld* WorldToSave = Cast<UWorld>( Level->GetOuter() );
		if ( WorldToSave )
		{
			FString FinalFilename;
			bLevelWasSaved = SaveWorld( WorldToSave,
										DefaultFilename.Len() > 0 ? &DefaultFilename : NULL,
										NULL, NULL,
										true, false,
										FinalFilename,
										false, false );
			if (bLevelWasSaved && OutSavedFilename)
			{
				*OutSavedFilename = FinalFilename;
			}
		}
	}

	return bLevelWasSaved;
}

bool FEditorFileUtils::SaveDirtyPackages(const bool bPromptUserToSave, const bool bSaveMapPackages, const bool bSaveContentPackages, const bool bFastSave, const bool bNotifyNoPackagesSaved, const bool bCanBeDeclined, bool* bOutPackagesNeededSaving )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FEditorFileUtils::SaveDirtyPackages);

	bool bReturnCode = true;

	if (bOutPackagesNeededSaving != NULL)
	{
		*bOutPackagesNeededSaving = false;
	}

	TArray<UPackage*> PackagesToSave = InternalGetDirtyPackages(bSaveMapPackages, bSaveContentPackages);

	// Need to track the number of packages we're not ignoring for save.
	int32 NumPackagesNotIgnored = 0;

	for (auto* Package : PackagesToSave)
	{
		// Count the number of packages to not ignore.
		NumPackagesNotIgnored += (PackagesNotSavedDuringSaveAll.Find(Package->GetName()) == NULL) ? 1 : 0;
	}

	if (PackagesToSave.Num() > 0 && (NumPackagesNotIgnored > 0 || bPromptUserToSave))
	{
		if (bOutPackagesNeededSaving != NULL)
		{
			*bOutPackagesNeededSaving = true;
		}

		bReturnCode = InternalSavePackages(PackagesToSave, bPromptUserToSave, bFastSave, bCanBeDeclined);
	}
	else if (bNotifyNoPackagesSaved)
	{
		InternalNotifyNoPackagesSaved(true);
	}
	return bReturnCode;
}

bool FEditorFileUtils::SaveDirtyContentPackages(TArray<UClass*>& SaveContentClasses, const bool bPromptUserToSave, const bool bFastSave, const bool bNotifyNoPackagesSaved, const bool bCanBeDeclined)
{
	bool bReturnCode = true;

	// A list of all packages that need to be saved
	TArray<UPackage*> PackagesToSave;

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// Make a list of all content packages that we should save
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage*	Package = *It;
		bool		bShouldIgnorePackage = false;

		// Only look at root packages.
		bShouldIgnorePackage |= Package->GetOuter() != NULL;
		// Don't try to save "Transient" package.
		bShouldIgnorePackage |= Package == GetTransientPackage();
		// Ignore PIE packages.
		bShouldIgnorePackage |= Package->HasAnyPackageFlags(PKG_PlayInEditor);
		// Ignore packages that haven't been modified.
		bShouldIgnorePackage |= !Package->IsDirty();

		// Ignore packages with long, invalid names. This culls out packages with paths in read-only roots such as /Temp.
		bShouldIgnorePackage |= (!FPackageName::IsShortPackageName(Package->GetFName()) && !FPackageName::IsValidLongPackageName(Package->GetName(), /*bIncludeReadOnlyRoots=*/false));

		if (!bShouldIgnorePackage)
		{
			TArray<UObject*> Objects;
			GetObjectsWithPackage(Package, Objects);

			for (auto Iter = Objects.CreateIterator(); Iter; ++Iter)
			{
				bool bNeedToSave = false;

				for (const UClass* ClassType : SaveContentClasses)
				{
					if ((*Iter)->GetClass()->IsChildOf(ClassType))
					{
						bNeedToSave = true;
						break;
					}
				}

				if (bNeedToSave)
				{
					// add to asset 
					PackagesToSave.Add(Package);
					break;
				}
			}
		}
	}

	bool bResult = false;
	if (PackagesToSave.Num() > 0)
	{
		bResult = InternalSavePackages(PackagesToSave, bPromptUserToSave, bFastSave, bCanBeDeclined);
	}
	else if (bNotifyNoPackagesSaved)
	{
		InternalNotifyNoPackagesSaved(true);
		bResult = true;
	}
	return bResult;
}

/**
 * Saves the active level, prompting the use for checkout if necessary.
 *
 * @return	true on success, False on fail
 */
bool FEditorFileUtils::SaveCurrentLevel()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FEditorFileUtils_SaveCurrentLevel);

	bool bReturnCode = true;
	ULevel* Level = GWorld->GetCurrentLevel();
	if (Level)
	{
		// Check dirtiness if the level is using external actors, no need to save it needlessly
		bool bCheckDirty = Level->IsUsingExternalActors();
		if (FEditorFileUtils::PromptToCheckoutLevels(bCheckDirty, Level))
		{
			UPackage* LevelPackage = Level->GetPackage();
			// Save the level
			if (!bCheckDirty || LevelPackage->IsDirty())
			{
				bReturnCode &= FEditorFileUtils::SaveLevel(Level);
			}

			// Gather the level owned packages (i.e external actors and save them)
			TArray<UPackage*> PackagesToSave = Level->GetLoadedExternalActorPackages();
			for (auto It = PackagesToSave.CreateIterator(); It; ++It)
			{
				UPackage* Package = *It;
				if (bCheckDirty && !Package->IsDirty() && !UPackage::IsEmptyPackage(Package))
				{
					It.RemoveCurrent();
				}
			}
			bReturnCode &= InternalSavePackages(PackagesToSave, false, false, false);
		}
	}
	return bReturnCode;
}

/*
 * Helper code for PromptForCheckoutAndSave
 * @param FinalSaveList					Package to save
 * @param bUseDialog					Use dialog with InternalSavePackage & if we show errors
 * @param OutFailedPackages				Packages that couldn't be save
 */
FEditorFileUtils::EPromptReturnCode InternalPromptForCheckoutAndSave(const TArray<UPackage*>& FinalSaveList, bool bUseDialog, TArray<UPackage*>& OutFailedPackages)
{
	FEditorFileUtils::EPromptReturnCode ReturnResponse = FEditorFileUtils::PR_Success;
	const FScopedBusyCursor BusyCursor;
	FSaveErrorOutputDevice SaveErrors;

	TArray<UPackage*, TInlineAllocator<2>> WritablePackageFiles;
	TArray<UPackage*> PackagesToClean;
	{
		FScopedSlowTask SlowTask(FinalSaveList.Num() * 2, NSLOCTEXT("UnrealEd", "SavingPackagesE", "Saving packages..."));
		SlowTask.MakeDialog();

		for (UPackage* Package : FinalSaveList)
		{
			SlowTask.EnterProgressFrame(1);

			if (!Package->IsFullyLoaded())
			{
				// Packages must be fully loaded to save.
				Package->FullyLoad();
			}

			const UWorld* const AssociatedWorld = UWorld::FindWorldInPackage(Package);
			const bool bIsMapPackage = AssociatedWorld != nullptr;

			const FText SavingPackageText = (bIsMapPackage)
				? FText::Format(NSLOCTEXT("UnrealEd", "SavingMapf", "Saving map {0}"), FText::FromString(Package->GetName()))
				: FText::Format(NSLOCTEXT("UnrealEd", "SavingAssetf", "Saving asset {0}"), FText::FromString(Package->GetName()));

			SlowTask.EnterProgressFrame(1, SavingPackageText);

			// if the package we are saving is considered empty, mark it for deletion on disk instead
			if (UPackage::IsEmptyPackage(Package))
			{
				PackagesToClean.Add(Package);
			}
			// Otherwise, save as usual
			else
			{
				// Save the package
				bool bPackageLocallyWritable;
				const InternalSavePackageResult SaveStatus = InternalSavePackage(Package, bUseDialog, bPackageLocallyWritable, SaveErrors);

				// If InternalSavePackage reported that the provided package was locally writable, add it to the list of writable files
				// to warn the user about
				if (bPackageLocallyWritable)
				{
					WritablePackageFiles.Add(Package);
				}

				if (SaveStatus == InternalSavePackageResult::Cancel)
				{
					// No need to save anything else, the user wants to cancel everything
					ReturnResponse = FEditorFileUtils::PR_Cancelled;
					break;
				}
				else if (SaveStatus == InternalSavePackageResult::Continue || SaveStatus == InternalSavePackageResult::Error)
				{
					// The package could not be saved so add it to the failed array and change the return response to indicate failure
					OutFailedPackages.Add(Package);
					ReturnResponse = FEditorFileUtils::PR_Failure;
				}
			}
		}
	}

	SaveErrors.Flush();

	if (PackagesToClean.Num() > 0)
	{
		ObjectTools::CleanupAfterSuccessfulDelete(PackagesToClean, true);
	}

	// If any packages were saved that weren't actually in source control but instead forcibly made writable,
	// then warn the user about those packages
	if (WritablePackageFiles.Num() > 0)
	{
		FString WritableFiles;
		for (UPackage* PackageIter : WritablePackageFiles)
		{
			// A warning message was created.  Try and show it.
			WritableFiles += FString::Printf(TEXT("\n%s"), *PackageIter->GetName());
		}

		const FText WritableFileWarning = FText::Format(NSLOCTEXT("UnrealEd", "Warning_WritablePackagesNotCheckedOut", "The following assets are writable on disk but not checked out from source control:{0}"),
			FText::FromString(WritableFiles));

		UE_LOG(LogFileHelpers, Warning, TEXT("%s"), *WritableFileWarning.ToString());
		if (bUseDialog)
		{
			FSuppressableWarningDialog::FSetupInfo Info(WritableFileWarning, NSLOCTEXT("UnrealEd", "Warning_WritablePackagesNotCheckedOutTitle", "Writable Assets Not Checked Out"), "WritablePackagesNotCheckedOut");
			Info.ConfirmText = NSLOCTEXT("ModalDialogs", "WritablePackagesNotCheckedOutConfirm", "Close");

			FSuppressableWarningDialog PromptForWritableFiles(Info);
			PromptForWritableFiles.ShowModal();
		}
	}

	// Warn the user if any packages failed to save
	if (OutFailedPackages.Num() > 0)
	{
		// Show a dialog for the failed packages
		InternalWarnUserAboutFailedSave(OutFailedPackages, bUseDialog);
	}

	return ReturnResponse;
}

/**
 * Optionally prompts the user for which of the provided packages should be saved, and then additionally prompts the user to check-out any of
 * the provided packages which are under source control. If the user cancels their way out of either dialog, no packages are saved. It is possible the user
 * will be prompted again, if the saving process fails for any reason. In that case, the user will be prompted on a package-by-package basis, allowing them
 * to retry saving, skip trying to save the current package, or to again cancel out of the entire dialog. If the user skips saving a package that failed to save,
 * the package will be added to the optional OutFailedPackages array, and execution will continue. After all packages are saved (or not), the user is provided with
 * a warning about any packages that were writable on disk but not in source control, as well as a warning about which packages failed to save.
 *
 * @param		PackagesToSave				The list of packages to save.  Both map and content packages are supported 
 * @param		bCheckDirty					If true, only packages that are dirty in PackagesToSave will be saved	
 * @param		bPromptToSave				If true the user will be prompted with a list of packages to save, otherwise all passed in packages are saved
 * @param		OutFailedPackages			[out] If specified, will be filled in with all of the packages that failed to save successfully
 * @param		bAlreadyCheckedOut			If true, the user will not be prompted with the source control dialog
 * @param		bCanBeDeclined				If true, offer a "Don't Save" option in addition to "Cancel", which will not result in a cancellation return code.
 *
 * @return		An enum value signifying success, failure, user declined, or cancellation. If any packages at all failed to save during execution, the return code will be 
 *				failure, even if other packages successfully saved. If the user cancels at any point during any prompt, the return code will be cancellation, even though it
 *				is possible some packages have been successfully saved (if the cancel comes on a later package that can't be saved for some reason). If the user opts the "Don't
 *				Save" option on the dialog, the return code will indicate the user has declined out of the prompt. This way calling code can distinguish between a decline and a cancel
 *				and then proceed as planned, or abort its operation accordingly.
 */
FEditorFileUtils::EPromptReturnCode FEditorFileUtils::PromptForCheckoutAndSave( const TArray<UPackage*>& InPackages, bool bCheckDirty, bool bPromptToSave, TArray<UPackage*>* OutFailedPackages, bool bAlreadyCheckedOut, bool bCanBeDeclined )
{
	// Check for re-entrance into this function
	if ( bIsPromptingForCheckoutAndSave )
	{
		return PR_Cancelled;
	}

	// Gather packages owned by the packages we are saving so we can prompt for them as well.
	TArray<UPackage*> PackagesToSave(InPackages);

	// When saving a package which owns other packages, add those to the prompt as well,
	// if we do not check dirty, we aren't already checked out and we prompt
	if (!bAlreadyCheckedOut && !bCheckDirty && bPromptToSave)
	{
		for (UPackage* Package : InPackages)
		{
			for (UPackage* ExternalPackage : Package->GetExternalPackages())
			{
				PackagesToSave.AddUnique(ExternalPackage);
			}
		}
	}

	if (GIsRunningUnattendedScript)
	{
		return UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, bCheckDirty) ? PR_Success : PR_Failure;
	}

	if ( FApp::IsUnattended() && !bAlreadyCheckedOut )
	{
		return PR_Cancelled;
	}

	// Prevent re-entrance into this function by setting up a guard value
	TGuardValue<bool> PromptForCheckoutAndSaveGuard(bIsPromptingForCheckoutAndSave, true);

	// Initialize the value we will return to indicate success
	FEditorFileUtils::EPromptReturnCode ReturnResponse = PR_Success;
	
	// Keep a static list of packages that have been unchecked by the user and uncheck them next time
	static TArray<TWeakObjectPtr<UPackage>> UncheckedPackages;

	// Keep a list of packages that have been filtered to be saved specifically; this could occur as the result of prompting the user
	// for which packages to save or from filtering by whether the package is dirty or not. This method allows us to save loop iterations and array copies.
	TArray<UPackage*> FilteredPackages;

	// Prompt the user for which packages they would like to save
	if( bPromptToSave )
	{
		// Set up the save package dialog
		FPackagesDialogModule& PackagesDialogModule = FModuleManager::LoadModuleChecked<FPackagesDialogModule>( TEXT("PackagesDialog") );
		PackagesDialogModule.CreatePackagesDialog(NSLOCTEXT("PackagesDialogModule", "PackagesDialogTitle", "Save Content"), NSLOCTEXT("PackagesDialogModule", "PackagesDialogMessage", "Select content to save."));
		PackagesDialogModule.AddButton(DRT_Save, NSLOCTEXT("PackagesDialogModule", "SaveSelectedButton", "Save Selected"), NSLOCTEXT("PackagesDialogModule", "SaveSelectedButtonTip", "Attempt to save the selected content"));
		if (bCanBeDeclined)
		{
			PackagesDialogModule.AddButton(DRT_DontSave, NSLOCTEXT("PackagesDialogModule", "DontSaveSelectedButton", "Don't Save"), NSLOCTEXT("PackagesDialogModule", "DontSaveSelectedButtonTip", "Do not save any content"));
		}
		PackagesDialogModule.AddButton(DRT_Cancel, NSLOCTEXT("PackagesDialogModule", "CancelButton", "Cancel"), NSLOCTEXT("PackagesDialogModule", "CancelButtonTip", "Do not save any content and cancel the current operation"));

		TArray<UPackage*> AddPackageItemsChecked;
		TArray<UPackage*> AddPackageItemsUnchecked;
		for ( TArray<UPackage*>::TConstIterator PkgIter(PackagesToSave); PkgIter; ++PkgIter )
		{
			UPackage* CurPackage = *PkgIter;
			check( CurPackage );

			// If the caller set bCheckDirty to true, only consider dirty packages
			if ( !bCheckDirty || ( bCheckDirty && CurPackage->IsDirty() ) )
			{
				// Never save the transient package
				if ( CurPackage != GetTransientPackage() )
				{
					// Never save compiled in packages
					if (CurPackage->HasAnyPackageFlags(PKG_CompiledIn) == false)
					{
						if (UncheckedPackages.Contains(MakeWeakObjectPtr(CurPackage)))
						{
							AddPackageItemsUnchecked.Add(CurPackage);
						}
						else
						{
							AddPackageItemsChecked.Add(CurPackage);
						}
					}
					else
					{
						UE_LOG(LogFileHelpers, Warning, TEXT("PromptForCheckoutAndSave attempted to open the save dialog with a compiled in package: %s"), *CurPackage->GetName());
					}
				}
				else
				{
					UE_LOG(LogFileHelpers, Warning, TEXT("PromptForCheckoutAndSave attempted to open the save dialog with the transient package"));
				}
			}
		}

		if ( AddPackageItemsUnchecked.Num() > 0 || AddPackageItemsChecked.Num() > 0 )
		{
			for (auto Iter = AddPackageItemsChecked.CreateIterator(); Iter; ++Iter)
			{
				PackagesDialogModule.AddPackageItem(*Iter, ECheckBoxState::Checked);
			}
			for (auto Iter = AddPackageItemsUnchecked.CreateIterator(); Iter; ++Iter)
			{
				PackagesDialogModule.AddPackageItem(*Iter, ECheckBoxState::Unchecked);
			}

			// If valid packages were added to the dialog, display it to the user
			const EDialogReturnType UserResponse = PackagesDialogModule.ShowPackagesDialog(PackagesNotSavedDuringSaveAll);

			// If the user has responded yes, they want to save the packages they have checked
			if ( UserResponse == DRT_Save )
			{
				PackagesToSave.Reset();
				PackagesDialogModule.GetResults(PackagesToSave, ECheckBoxState::Checked );

				TArray<UPackage*> UncheckedPackagesRaw;
				PackagesDialogModule.GetResults( UncheckedPackagesRaw, ECheckBoxState::Unchecked );
				UncheckedPackages.Empty();
				for (UPackage* Package : UncheckedPackagesRaw)
				{
					UncheckedPackages.Add(MakeWeakObjectPtr(Package));
				}
			}
			// If the user has responded they don't wish to save, set the response type accordingly
			else if ( UserResponse == DRT_DontSave )
			{
				ReturnResponse = PR_Declined;
			}
			// If the user has cancelled from the dialog, set the response type accordingly
			else
			{
				ReturnResponse = PR_Cancelled;
			}
		}
	}
	else
	{
		// The user will not be prompted about which files to save, so consider all provided packages directly
		for ( TArray<UPackage*>::TIterator PkgIter(PackagesToSave); PkgIter; ++PkgIter )
		{
			UPackage* CurPackage = *PkgIter;
			check( CurPackage );

			// (Don't consider non-dirty packages if the caller has specified bCheckDirty as true)
			if ( !bCheckDirty || CurPackage->IsDirty() )
			{
				// Never save the transient package
				if ( CurPackage != GetTransientPackage() )
				{
					// Never save compiled in packages
					if (CurPackage->HasAnyPackageFlags(PKG_CompiledIn))
					{
						PkgIter.RemoveCurrent();
						UE_LOG(LogFileHelpers, Warning, TEXT("PromptForCheckoutAndSave attempted to save a compiled in package: %s"), *CurPackage->GetName());
					}
				}
				else
				{
					PkgIter.RemoveCurrent();
					UE_LOG(LogFileHelpers, Warning, TEXT("PromptForCheckoutAndSave attempted to save the transient package"));
				}
			}
			else
			{
				PkgIter.RemoveCurrent();
			}
		}
	}

	// Assemble list of packages to save
	// If there are any packages to save and the user didn't decline/cancel, then first prompt to check out any that are under source control,
	// and then go ahead and save the specified packages
	if ( PackagesToSave.Num() > 0 && ReturnResponse == PR_Success )
	{
		TArray<UPackage*> FailedPackages;

		TArray<UPackage*> PackagesCheckedOutOrMadeWritable;
		TArray<UPackage*> PackagesNotNeedingCheckout;

		// Prompt to check-out any packages under source control
		bool bUserResponse = true;

		if (!bAlreadyCheckedOut)
		{
			bUserResponse = FEditorFileUtils::PromptToCheckoutPackages(false, PackagesToSave, &PackagesCheckedOutOrMadeWritable, &PackagesNotNeedingCheckout);
		}

		if( bAlreadyCheckedOut || (bUserResponse && (PackagesCheckedOutOrMadeWritable.Num() > 0 || PackagesNotNeedingCheckout.Num() > 0)) )
		{
			TArray<UPackage*> FinalSaveList;
			
			if (bAlreadyCheckedOut)
			{
				FinalSaveList = PackagesToSave;
			}
			else
			{
				FinalSaveList = PackagesNotNeedingCheckout;
				FinalSaveList.Append(PackagesCheckedOutOrMadeWritable);
			}

			{
				const bool bUseDialog = true;
				ReturnResponse = InternalPromptForCheckoutAndSave(FinalSaveList, bUseDialog, FailedPackages);
			}

			// Set the failure array to have the same contents as the local one.
			// The local one is required so we can always display the error, even if an array is not provided.
			if (OutFailedPackages)
			{
				*OutFailedPackages = FailedPackages;
			}
		}
		else
		{
			// The user cancelled the checkout dialog, so set the return response accordingly  
			ReturnResponse = PR_Cancelled;
		}

	}

	return ReturnResponse;
}

bool FEditorFileUtils::SaveWorlds(UWorld* InWorld, const FString& RootPath, const TCHAR* Prefix, TArray<FString>& OutFilenames)
{
	const FScopedBusyCursor BusyCursor;

	TArray<UWorld*> WorldsArray;
	EditorLevelUtils::GetWorlds( InWorld, WorldsArray, true );

	TArray<FName> PackageNames; 

	// Save all packages containing levels that are currently "referenced" by the global world pointer.
	bool bSavedAll = true;
	FString FinalFilename;
	for ( int32 WorldIndex = 0 ; WorldIndex < WorldsArray.Num() ; ++WorldIndex )
	{
		UWorld* World = WorldsArray[WorldIndex];

		const FString WorldPath = FString::Printf(TEXT("%s%s"), *RootPath, *FPackageName::GetLongPackagePath(World->GetOuter()->GetName()));
		const bool bLevelWasSaved = SaveWorld( World, NULL,
												*WorldPath, Prefix,
												false, false,
												FinalFilename,
												false, true);

		if (bLevelWasSaved)
		{
			OutFilenames.Add(FinalFilename);
		}
		else
		{
			bSavedAll = false;
		}
	}

	return bSavedAll;
}

/**
 * DEPRECATED in version 4.18, Call FFileHelper::IsFilenameValidForSaving instead
 */
bool FEditorFileUtils::IsFilenameValidForSaving( const FString& Filename, FText& OutError )
{
	return FFileHelper::IsFilenameValidForSaving(Filename, OutError);
}

void FEditorFileUtils::LoadDefaultMapAtStartup()
{
	FString EditorStartupMap;
	// Last opened map.
	if (GetDefault<UEditorLoadingSavingSettings>()->LoadLevelAtStartup == ELoadLevelAtStartup::LastOpened)
	{
		GConfig->GetString(TEXT("EditorStartup"), TEXT("LastLevel"), EditorStartupMap, GEditorPerProjectIni);
	}
	// Default project map.
	if (EditorStartupMap.IsEmpty()) 
	{
		EditorStartupMap = GetDefault<UGameMapsSettings>()->EditorStartupMap.GetLongPackageName();
	}
	
	const bool bIncludeReadOnlyRoots = true;
	if ( FPackageName::IsValidLongPackageName(EditorStartupMap, bIncludeReadOnlyRoots) )
	{
		FString MapFilenameToLoad = FPackageName::LongPackageNameToFilename( EditorStartupMap );

		bIsLoadingDefaultStartupMap = true;
		FEditorFileUtils::LoadMap( MapFilenameToLoad + FPackageName::GetMapPackageExtension(), GUnrealEd && GUnrealEd->IsTemplateMap(EditorStartupMap), true );
		bIsLoadingDefaultStartupMap = false;
	}
}

void FEditorFileUtils::FindAllPackageFiles(TArray<FString>& OutPackages)
{
#if UE_BUILD_SHIPPING
	FString Key = TEXT("Paths");
#else
	// decide which paths to use by commandline parameter
	// Used only for testing wrangled content -- not for ship!
	FString PathSet(TEXT("Normal"));
	FParse::Value(FCommandLine::Get(), TEXT("PATHS="), PathSet);

	FString Key = (PathSet == TEXT("Cutdown")) ? TEXT("CutdownPaths") : TEXT("Paths");
#endif

	TArray<FString> Paths;
	GConfig->GetArray( TEXT("Core.System"), *Key, Paths, GEngineIni );

	for (int32 PathIndex = 0; PathIndex < Paths.Num(); PathIndex++)
	{
		FPackageName::FindPackagesInDirectory(OutPackages, Paths[PathIndex]);
	}
}

void FEditorFileUtils::FindAllSubmittablePackageFiles(TMap<FString, FSourceControlStatePtr>& OutPackages, const bool bIncludeMaps)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	OutPackages.Empty();

	TArray<FString> Packages;
	FEditorFileUtils::FindAllPackageFiles(Packages);

	// Handle the project file
	FSourceControlStatePtr ProjectFileSourceControlState = SourceControlProvider.GetState(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), EStateCacheUsage::Use);

	if (ProjectFileSourceControlState.IsValid() && ProjectFileSourceControlState->IsCurrent() &&
		(ProjectFileSourceControlState->CanCheckIn() || (!ProjectFileSourceControlState->IsSourceControlled() && ProjectFileSourceControlState->CanAdd())))
	{
		OutPackages.Add(FPaths::GetProjectFilePath(), MoveTemp(ProjectFileSourceControlState));
	}

	for (TArray<FString>::TConstIterator PackageIter(Packages); PackageIter; ++PackageIter)
	{
		const FString Filename = *PackageIter;

		FString PackageName;
		FString FailureReason;
		if (!FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName, &FailureReason))
		{
			UE_LOG(LogFileHelpers, Warning, TEXT("%s"), *FailureReason);
			continue;
		}

		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(FPaths::ConvertRelativePathToFull(Filename), EStateCacheUsage::Use);

		// Only include non-map packages that are currently checked out or packages not under source control
		if (SourceControlState.IsValid() && SourceControlState->IsCurrent() &&
			(SourceControlState->CanCheckIn() || (!SourceControlState->IsSourceControlled() && SourceControlState->CanAdd())) &&
			(bIncludeMaps || !IsMapPackageAsset(*Filename)))
		{
			OutPackages.Add(MoveTemp(PackageName), MoveTemp(SourceControlState));
		}
	}
}

static void FindAllConfigFilesRecursive(TArray<FString>& OutConfigFiles, const FString& ParentDirectory)
{
	TArray<FString> IniFilenames;
	IFileManager::Get().FindFiles(IniFilenames, *(FPaths::ProjectConfigDir() / ParentDirectory / TEXT("*.ini")), true, false);
	for (const FString& IniFilename : IniFilenames)
	{
		OutConfigFiles.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / ParentDirectory / IniFilename));
	}

	TArray<FString> Subdirectories;
	IFileManager::Get().FindFiles(Subdirectories, *(FPaths::ProjectConfigDir() / ParentDirectory / TEXT("*")), false, true);
	for (const FString& Subdirectory : Subdirectories)
	{
		FindAllConfigFilesRecursive(OutConfigFiles, ParentDirectory / Subdirectory);
	}
}

void FEditorFileUtils::FindAllConfigFiles(TArray<FString>& OutConfigFiles)
{
	FindAllConfigFilesRecursive(OutConfigFiles, FString());
}

void FEditorFileUtils::FindAllSubmittableConfigFiles(TMap<FString, TSharedPtr<class ISourceControlState, ESPMode::ThreadSafe> >& OutConfigFiles)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	TArray<FString> ConfigFilenames;
	FEditorFileUtils::FindAllConfigFiles(ConfigFilenames);

	for (const FString& ConfigFilename : ConfigFilenames)
	{
		// Only check files which are intended to be under source control. Ignore all user config files.
		if (FPaths::GetCleanFilename(ConfigFilename) != TEXT("DefaultEditorPerProjectUserSettings.ini") && !FPaths::GetCleanFilename(ConfigFilename).StartsWith(TEXT("User")))
		{
			FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(ConfigFilename, EStateCacheUsage::Use);

			// Only include config files that are currently checked out or packages not under source control
			if (SourceControlState.IsValid() && SourceControlState->IsCurrent() &&
				(SourceControlState->CanCheckIn() || (!SourceControlState->IsSourceControlled() && SourceControlState->CanAdd())))
			{
				OutConfigFiles.Add(ConfigFilename, SourceControlState);
			}
		}
	}
}

bool FEditorFileUtils::IsMapPackageAsset(const FString& ObjectPath)
{
	FString MapFilePath;
	return FEditorFileUtils::IsMapPackageAsset(ObjectPath, MapFilePath);
}

bool FEditorFileUtils::IsMapPackageAsset(const FString& ObjectPath, FString& MapFilePath)
{
	const FString PackageName = ExtractPackageName(ObjectPath);
	if ( PackageName.Len() > 0 )
	{
		FString PackagePath;
		if ( FPackageName::DoesPackageExist(PackageName, NULL, &PackagePath) )
		{
			const FString FileExtension = FPaths::GetExtension(PackagePath, true);
			if ( FileExtension == FPackageName::GetMapPackageExtension() )
			{
				MapFilePath = PackagePath;
				return true;
			}
		}
	}

	return false;
}

FString FEditorFileUtils::ExtractPackageName(const FString& ObjectPath)
{
	// To find the package name in an object path we need to find the path left of the FIRST delimiter.
	// Assets like BSPs, lightmaps etc. can have multiple '.' delimiters.
	const int32 PackageDelimiterPos = ObjectPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart);
	if ( PackageDelimiterPos != INDEX_NONE )
	{
		return ObjectPath.Left(PackageDelimiterPos);
	}

	return ObjectPath;
}

void FEditorFileUtils::GetDirtyWorldPackages(TArray<UPackage*>& OutDirtyPackages)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const TSharedRef<FBlacklistPaths>& WritableFolderFilter = AssetToolsModule.Get().GetWritableFolderBlacklist();
	const bool bHasWritableFolderFilter = WritableFolderFilter->HasFiltering();

	for (TObjectIterator<UWorld> WorldIt; WorldIt; ++WorldIt)
	{
		UPackage* WorldPackage = WorldIt->GetOutermost();
		if (!WorldPackage->HasAnyPackageFlags(PKG_PlayInEditor)
			&& !WorldPackage->HasAnyFlags(RF_Transient)
			&& (!bHasWritableFolderFilter || WritableFolderFilter->PassesStartsWithFilter(WorldPackage->GetName()))
			)
		{
			if (WorldPackage->IsDirty())
			{
				// IF the package is dirty and its not a pie package, add the world package to the list of packages to save
				OutDirtyPackages.Add(WorldPackage);
			}

			// Add the Map built data as well if world is
			if (WorldIt->PersistentLevel && WorldIt->PersistentLevel->MapBuildData)
			{
				UPackage* BuiltDataPackage = WorldIt->PersistentLevel->MapBuildData->GetOutermost();

				if (BuiltDataPackage != WorldPackage)
				{
					if (WorldPackage->IsDirty() && !BuiltDataPackage->IsDirty())
					{
						// Mark built data package dirty if has not been given name yet
						// Otherwise SaveDirtyPackages will fail to create built data file on disk due to re-entrance guard in PromptForCheckoutAndSave preventing a second pop-up window
						if (!FPackageName::IsValidLongPackageName(BuiltDataPackage->GetName(), /*bIncludeReadOnlyRoots= */ false))
						{
							BuiltDataPackage->MarkPackageDirty();
						}
					}

					if (BuiltDataPackage->IsDirty())
					{
						// If built data package does not have a name yet add the world package so a user is prompted to have a name chosen
						if (!WorldPackage->IsDirty())
						{
							const FString WorldPackageName = WorldPackage->GetName();
							const bool bIncludeReadOnlyRoots = false;
							const bool bIsValidPath = FPackageName::IsValidLongPackageName(WorldPackageName, bIncludeReadOnlyRoots);
							if (!bIsValidPath)
							{
								WorldPackage->MarkPackageDirty();
								OutDirtyPackages.Add(WorldPackage);
							}
						}

						OutDirtyPackages.Add(BuiltDataPackage);
					}
				}
			}

			// Make sure we also save the dirty HLOD packages associated with this map.
			if (WorldIt->HierarchicalLODBuilder)
			{
				const AWorldSettings* WorldSettings = WorldIt->GetWorldSettings();
				if (WorldSettings && WorldSettings->bEnableHierarchicalLODSystem)
				{
					TSet<UPackage*> HLODPackages;
					WorldIt->HierarchicalLODBuilder->GetMeshesPackagesToSave(WorldIt->PersistentLevel, HLODPackages);
					for (UPackage* HLODPackage : HLODPackages)
					{
						if (HLODPackage->IsDirty())
						{
							OutDirtyPackages.Add(HLODPackage);
						}
					}
				}
			}

			// Now gather the world external packages and save them if needed
			if (WorldIt->PersistentLevel)
			{
				for (UPackage* ExternalPackage : WorldIt->PersistentLevel->GetLoadedExternalActorPackages())
				{
					if (ExternalPackage->IsDirty())
					{
						OutDirtyPackages.Add(ExternalPackage);
					}
				}
			}
		}
	}
}

void FEditorFileUtils::GetDirtyContentPackages(TArray<UPackage*>& OutDirtyPackages)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const TSharedRef<FBlacklistPaths>& WritableFolderFilter = AssetToolsModule.Get().GetWritableFolderBlacklist();
	const bool bHasWritableFolderFilter = WritableFolderFilter->HasFiltering();

	// Make a list of all content packages that we should save
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage*	Package = *It;
		bool		bShouldIgnorePackage = false;

		// Only look at root packages.
		bShouldIgnorePackage |= Package->GetOuter() != NULL;
		// Don't try to save "Transient" package.
		bShouldIgnorePackage |= Package == GetTransientPackage();
		// Don't try to save packages with the RF_Transient flag
		bShouldIgnorePackage |= Package->HasAnyFlags(RF_Transient);
		// Ignore PIE packages, or packages containing map data
		bShouldIgnorePackage |= Package->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsMapData);
		// Ignore packages that haven't been modified.
		bShouldIgnorePackage |= !Package->IsDirty();

		if (!bShouldIgnorePackage)
		{
			UObject* Asset = Package->FindAssetInPackage();
			const bool bIsMapPackage = Cast<UWorld>(Asset) != nullptr;
			const bool bIsExternalMapObject = Asset && Asset->GetTypedOuter<UWorld>() != nullptr;

			// Ignore map packages, they are caught above.
			bShouldIgnorePackage |= bIsMapPackage;

			// Ignore external actors, they are caught alongside maps
			bShouldIgnorePackage |= bIsExternalMapObject;

			if (!bShouldIgnorePackage)
			{
				FString PackageName = Package->GetName();

				// Ignore packages with long, invalid names. This culls out packages with paths in read-only roots such as /Temp.
				bShouldIgnorePackage |= (!FPackageName::IsShortPackageName(Package->GetFName()) && !FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots=*/false));

				// Ignore packages that cannot be saved due to a custom filter
				if (!bShouldIgnorePackage && bHasWritableFolderFilter)
				{
					bShouldIgnorePackage |= (!WritableFolderFilter->PassesStartsWithFilter(PackageName));
				}
			}
		}

		if (!bShouldIgnorePackage)
		{
			OutDirtyPackages.Add(Package);
		}
	}
}

UWorld* UEditorLoadingAndSavingUtils::LoadMap(const FString& Filename)
{
	const bool bLoadAsTemplate = false;
	const bool bShowProgress = true;
	if (FEditorFileUtils::LoadMap(Filename, bLoadAsTemplate, bShowProgress))
	{
		return GEditor->GetEditorWorldContext().World();
	}

	return nullptr;
}

bool UEditorLoadingAndSavingUtils::SaveMap(UWorld* World, const FString& AssetPath)
{
	bool bSucceeded = false;
	FString SaveFilename;
	if( FPackageName::TryConvertLongPackageNameToFilename(AssetPath, SaveFilename, FPackageName::GetMapPackageExtension()))
	{
		bSucceeded = FEditorFileUtils::SaveMap(World, SaveFilename);
		if (bSucceeded)
		{
			FAssetRegistryModule::AssetCreated(World);
		}
	}

	return bSucceeded;
}

UWorld* UEditorLoadingAndSavingUtils::NewBlankMap(bool bSaveExistingMap)
{
	GLevelEditorModeTools().DeactivateAllModes();

	const bool bPromptUserToSave = false;
	const bool bFastSave = !bPromptUserToSave;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = false;
	if (bSaveExistingMap && FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave) == false)
	{
		// something went wrong or the user pressed cancel.  Return to the editor so the user doesn't lose their changes		
		return nullptr;
	}

	UWorld* World = GEditor->NewMap();

	FEditorFileUtils::ResetLevelFilenames();

	return World;
}

UWorld* UEditorLoadingAndSavingUtils::NewMapFromTemplate(const FString& PathToTemplateLevel, bool bSaveExistingMap)
{
	bool bSaveMapPackages = true;
	bool bSaveContentPackages = false;
	if (bSaveExistingMap && SaveDirtyPackages(bSaveMapPackages, bSaveContentPackages) == false)
	{
		return nullptr;
	}

	const bool bLoadAsTemplate = true;
	// Load the template map file - passes LoadAsTemplate==true making the
	// level load into an untitled package that won't save over the template
	FEditorFileUtils::LoadMap(*PathToTemplateLevel, bLoadAsTemplate);

	return GEditor->GetEditorWorldContext().World();
}

UWorld* UEditorLoadingAndSavingUtils::LoadMapWithDialog()
{
	if (!FEditorFileUtils::LoadMap())
	{
		return nullptr;
	}

	return GEditor->GetEditorWorldContext().World();
}

static bool InternalCheckoutAndSavePackages(const TArray<UPackage*>& PackagesToSave, bool bUseDialog)
{
	bool bResult = true;
	if (PackagesToSave.Num() > 0)
	{
		if (bUseDialog)
		{
			const bool bPromptUserToSave = true;
			const bool bFastSave = false;
			const bool bCanBeDeclined = true;
			bResult = InternalSavePackages(PackagesToSave, bPromptUserToSave, bFastSave, bCanBeDeclined);
		}
		else
		{
			const FScopedBusyCursor BusyCursor;
			// Prevent modal window if not requested.
			TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

			TArray<UPackage*> PackagesCheckedOut;
			const bool bErrorIfAlreadyCheckedOut = false;
			const bool bConfirmPackageBranchCheckOutStatus = false;

			FEditorFileUtils::CheckoutPackages(PackagesToSave, &PackagesCheckedOut, bErrorIfAlreadyCheckedOut, bConfirmPackageBranchCheckOutStatus);

			// Cannot mark files for add until after packages saved
			TArray<UPackage*> PackagesToMarkForAdd;
			for (UPackage* Package : PackagesToSave)
			{
				// List unsaved packages that were not checked out
				if (!PackagesCheckedOut.Contains(Package))
				{
					PackagesToMarkForAdd.Add(Package);
				}
			}

			TArray<UPackage*> FailedPackages;
			FEditorFileUtils::EPromptReturnCode ReturnResponse = InternalPromptForCheckoutAndSave(PackagesToSave, bUseDialog, FailedPackages);

			// Mark files for add now that packages have saved
			PackagesToMarkForAdd.RemoveAll([&FailedPackages](UPackage* Package) { return FailedPackages.Contains(Package); });
			if (PackagesToMarkForAdd.Num() > 0)
			{
				FEditorFileUtils::CheckoutPackages(PackagesToMarkForAdd, nullptr, bErrorIfAlreadyCheckedOut, bConfirmPackageBranchCheckOutStatus);
			}

			bResult = (ReturnResponse == FEditorFileUtils::PR_Success);
		}
	}
	else
	{
		InternalNotifyNoPackagesSaved(bUseDialog);
	}

	return bResult;
}

static TArray<UPackage*> InternalGetValidPackages(const TArray<UPackage*>& PackagesToSave, bool bCheckDirty)
{
	//Prevent all prompt code
	TArray<UPackage*> Packages;
	Packages.Reserve(PackagesToSave.Num());
	for (UPackage* Package : PackagesToSave)
	{
		if (Package && !Package->HasAnyFlags(RF_ClassDefaultObject))
		{
			Package = Package->GetOutermost();
			if (Package != GetTransientPackage() && Package->HasAnyPackageFlags(PKG_CompiledIn) == false)
			{
				if (!bCheckDirty || Package->IsDirty())
				{
					Package->FullyLoad();
					Packages.AddUnique(Package);
				}
			}
		}
	}
	return Packages;
}

bool UEditorLoadingAndSavingUtils::SavePackages(const TArray<UPackage*>& PackagesToSave, bool bOnlyDirty)
{
	TArray<UPackage*> Packages = InternalGetValidPackages(PackagesToSave, bOnlyDirty);
	return InternalCheckoutAndSavePackages(Packages, false);
}

bool UEditorLoadingAndSavingUtils::SavePackagesWithDialog(const TArray<UPackage*>& PackagesToSave, bool bOnlyDirty)
{
	TArray<UPackage*> Packages = InternalGetValidPackages(PackagesToSave, bOnlyDirty);
	return InternalCheckoutAndSavePackages(Packages, true);
}

bool UEditorLoadingAndSavingUtils::SaveDirtyPackages(const bool bSaveMapPackages, const bool bSaveContentPackages)
{
	TArray<UPackage*> Packages = InternalGetDirtyPackages(bSaveMapPackages, bSaveContentPackages);
	return InternalCheckoutAndSavePackages(Packages, false);
}

bool UEditorLoadingAndSavingUtils::SaveDirtyPackagesWithDialog(const bool bSaveMapPackages, const bool bSaveContentPackages)
{
	TArray<UPackage*> Packages = InternalGetDirtyPackages(bSaveMapPackages, bSaveContentPackages);
	return InternalCheckoutAndSavePackages(Packages, true);
}

bool UEditorLoadingAndSavingUtils::SaveCurrentLevel()
{
	return FEditorFileUtils::SaveCurrentLevel();
}

void UEditorLoadingAndSavingUtils::GetDirtyMapPackages(TArray<UPackage*>& OutDirtyPackages)
{
	FEditorFileUtils::GetDirtyWorldPackages(OutDirtyPackages);
}

void UEditorLoadingAndSavingUtils::GetDirtyContentPackages(TArray<UPackage*>& OutDirtyPackages)
{
	FEditorFileUtils::GetDirtyContentPackages(OutDirtyPackages);
}

void UEditorLoadingAndSavingUtils::ImportScene(const FString& Filename)
{
	FEditorFileUtils::Import(Filename);
}

void UEditorLoadingAndSavingUtils::ExportScene(bool bExportSelectedActorsOnly)
{
	FEditorFileUtils::Export(bExportSelectedActorsOnly);
}

void UEditorLoadingAndSavingUtils::UnloadPackages(const TArray<UPackage*>& PackagesToUnload, bool& bOutAnyPackagesUnloaded, FText& OutErrorMessage)
{
	bOutAnyPackagesUnloaded = UPackageTools::UnloadPackages(PackagesToUnload, OutErrorMessage);
}

void UEditorLoadingAndSavingUtils::ReloadPackages(const TArray<UPackage*>& PackagesToReload, bool& bOutAnyPackagesReloaded, FText& OutErrorMessage, const EReloadPackagesInteractionMode InteractionMode)
{
	bOutAnyPackagesReloaded = UPackageTools::ReloadPackages(PackagesToReload, OutErrorMessage, InteractionMode);
}

#undef LOCTEXT_NAMESPACE
