// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Menus/LayoutsMenu.h"
// Runtime/Core
#include "Containers/Array.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "HAL/FileManagerGeneric.h"
#include "Logging/MessageLog.h"
#include "Templates/SharedPointer.h"
// Runtime
#include "CoreGlobals.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
// Developer
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "ToolMenus.h"
// Editor
#include "Classes/EditorStyleSettings.h"
#include "Dialogs/CustomDialog.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Frame/MainFrameActions.h"
#include "LevelViewportActions.h"
#include "Menus/SaveLayoutDialog.h"
#include "Misc/MessageDialog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealEdGlobals.h"
#include "UnrealEdMisc.h"

#define LOCTEXT_NAMESPACE "MainFrameActions"

DEFINE_LOG_CATEGORY_STATIC(LogLayoutsMenu, Fatal, All);

namespace FPrivateFLayoutsMenu
{
	enum class ELayoutsMenu
	{
		Load,
		Save,
		Remove
	};

	enum class ELayoutsType
	{
		Engine,
		User
	};

// Get LayoutsDirectory path
FString CreateAndGetDefaultLayoutDirInternal()
{
	// Get LayoutsDirectory path
	const FString LayoutsDirectory = FPaths::EngineDefaultLayoutDir();
	// If the directory does not exist, create it (but it will not have saved Layouts inside)
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LayoutsDirectory))
	{
		PlatformFile.CreateDirectory(*LayoutsDirectory);
	}
	// Return result
	return LayoutsDirectory;
}

FString CreateAndGetUserLayoutDirInternal()
{
	// Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPaths::EngineUserLayoutDir();
	// If the directory does not exist, create it (but it will not have saved Layouts inside)
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*UserLayoutsDirectory))
	{
		PlatformFile.CreateDirectory(*UserLayoutsDirectory);
	}
	// Return result
	return UserLayoutsDirectory;
}

TArray<FString> GetIniFilesInFolderInternal(const FString& InStringDirectory)
{
	// Find all ini files in folder
	TArray<FString> LayoutIniFileNames;
	const FString LayoutIniFilePaths = FPaths::Combine(*InStringDirectory, TEXT("*.ini"));
	FFileManagerGeneric::Get().FindFiles(LayoutIniFileNames, *LayoutIniFilePaths, true, false);
	return LayoutIniFileNames;
}

bool TrySaveLayoutOrWarnInternal(
	const FString& InSourceFilePath, const FString& InTargetFilePath, const FText& InWhatIsThis, const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues,
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false, const bool bShowSaveToast = false)
{
	// If desired, ask user whether to keep the LayoutName and LayoutDescription fields
	bool bCleanLayoutNameAndDescriptionFields = false;
	// If we are checking whether to clean the fields, we only want to maintain them if we are saving the file into an existing file that already has the same field values
	if (bCleanLayoutNameAndDescriptionFieldsIfNoSameValues)
	{
		GConfig->UnloadFile(InSourceFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		const FText LayoutNameSource = FLayoutSaveRestore::LoadSectionFromConfig(InSourceFilePath, "LayoutName");
		const FText LayoutDescriptionSource = FLayoutSaveRestore::LoadSectionFromConfig(InSourceFilePath, "LayoutDescription");
		GConfig->UnloadFile(InTargetFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		const FText LayoutNameTarget = FLayoutSaveRestore::LoadSectionFromConfig(InTargetFilePath, "LayoutName");
		const FText LayoutDescriptionTarget = FLayoutSaveRestore::LoadSectionFromConfig(InTargetFilePath, "LayoutDescription");
		// The output target exists (overriding)
		// These fields are not empty in source
		if (!LayoutNameSource.IsEmpty() || !LayoutDescriptionSource.IsEmpty())
		{
			// These fields are different than the ones in target
			if ((LayoutNameSource.ToString() != LayoutNameTarget.ToString()) || (LayoutDescriptionSource.ToString() != LayoutDescriptionTarget.ToString()))
			{
				bCleanLayoutNameAndDescriptionFields = true;
				// We should clean the layout name and description fields, but ask user first
				if (bShouldAskBeforeCleaningLayoutNameAndDescriptionFields)
				{
					// Open Dialog
					const FText TextTitle = LOCTEXT("OverrideLayoutNameAndDescriptionFieldBodyTitle", "Preserve UI Layout Name and Description Fields?");
					const FText TextBody = FText::Format(
						LOCTEXT("OverrideLayoutNameAndDescriptionFieldBody",
							"You are saving a layout that contains a custom layout name and/or description. Do you also want to copy these 2 properties?\n"
							" - Current layout name: {0}\n"
							" - Current layout description: {1}\n\n"
							"If you select \"Preserve Values\", the displayed name and description of the original layout customization will also be copied into the new configuration file.\n\n"
							"If you select \"Clear Values\", these fields will be emptied.\n\n"
							"If you are not sure, select \"Preserve Values\" if you are exporting the layout configuration without making any changes, or \"Clear Values\" if you"" have made or"
							" plan to make changes to the layout.\n\n"
						),
						LayoutNameSource, LayoutDescriptionSource);
					// Dialog SWidget
					TSharedRef<SVerticalBox> DialogContents = SNew(SVerticalBox);
					DialogContents->AddSlot()
						.Padding(0, 16, 0, 0)
						[
							SNew(STextBlock)
							.Text(TextBody)
						];
					const FText PreserveValuesText = LOCTEXT("PreserveValuesText", "Preserve Values");
					const FText ClearValuesText = LOCTEXT("ClearValuesText", "Clear Values");
					const FText CancelText = NSLOCTEXT("Dialogs", "EAppReturnTypeCancel", "Cancel");
					TSharedRef<SCustomDialog> CustomDialog = SNew(SCustomDialog)
						.Title(TextTitle)
						.DialogContent(DialogContents)
						.Buttons({ SCustomDialog::FButton(PreserveValuesText), SCustomDialog::FButton(ClearValuesText), SCustomDialog::FButton(CancelText) })
					;
					// Returns 0 when "Preserve Values" is pressed, 1 when "Clear Values" is pressed, or 2 when Cancel/Esc is pressed
					const int ButtonPressed = CustomDialog->ShowModal();
					// Preserve Values
					if (ButtonPressed == 0)
					{
						bCleanLayoutNameAndDescriptionFields = false;
					}
					// Clear Values
					else if (ButtonPressed == 1)
					{
						bCleanLayoutNameAndDescriptionFields = true;
					}
					// Cancel or Esc or window closed
					else if (ButtonPressed == 2 || ButtonPressed == -1)
					{
						return false;
					}
					// This should never occur
					else
					{
						ensureMsgf(false, TEXT("This option should never occur, something went wrong!"));
						return false;
					}
				}
			}
		}
	}
	// Copy: Replace main layout with desired one
	const FString TargetAbsoluteFilePath = FPaths::ConvertRelativePathToFull(InTargetFilePath);
	const bool bShouldReplace = true;
	const bool bCopyEvenIfReadOnly = true;
	const bool bCopyAttributes = false; // If true, it could e.g., copy the read-only flag of DefaultLayout.ini and make all the save/load stuff stop working
	if (COPY_Fail == IFileManager::Get().Copy(*InTargetFilePath, *InSourceFilePath, bShouldReplace, bCopyEvenIfReadOnly, bCopyAttributes))
	{
		FMessageLog EditorErrors("EditorErrors");
		FText TextBody;
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("WhatIs"), InWhatIsThis);
		// Source does not exist
		if (!FPaths::FileExists(InSourceFilePath))
		{
			Arguments.Add(TEXT("FileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_NoExist_Notification",
				"The requested operation ({WhatIs}) was unsuccessful, the desired file does not exist. File path:\n{FileName}"), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// Target is read-only
		else if (IFileManager::Get().IsReadOnly(*InTargetFilePath))
		{
			Arguments.Add(TEXT("FileName"), FText::FromString(TargetAbsoluteFilePath));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_ReadOnly_Notification",
				"The requested operation ({WhatIs}) was unsuccessful, the target file path is read-only. File path:\n{FileName}"), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// Target and source are the same
		else if (TargetAbsoluteFilePath == FPaths::ConvertRelativePathToFull(InSourceFilePath))
		{
			Arguments.Add(TEXT("SourceFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			Arguments.Add(TEXT("FinalFileName"), FText::FromString(TargetAbsoluteFilePath));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_Fallback_Notification",
				"The requested operation ({WhatIs}) was unsuccessful, target and source layout file paths are the same ({SourceFileName})!\nAre you trying to import or replace a file"
				" that is already in the layouts folder? If so, remove the current file first."), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// We don't specifically know why it failed, this is a fallback
		else
		{
			Arguments.Add(TEXT("SourceFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			Arguments.Add(TEXT("FinalFileName"), FText::FromString(TargetAbsoluteFilePath));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_Fallback_Notification",
				"The requested operation ({WhatIs}) was unsuccessful while copying the layout file from\n{SourceFileName}\ninto\n{FinalFileName}\n\nUsually, this occurs when the"
				" introduced file name contains unsupported characters or the total path length exceeds the OS limit."), Arguments);
			EditorErrors.Warning(TextBody);
		}
		EditorErrors.Notify(LOCTEXT("LoadUnsuccessful_Title", "Load Unsuccessful!"));
		// Show reason
		const FText TextTitle = LOCTEXT("UnsuccessfulCopyHeader", "Unsuccessful copy!");
		FMessageDialog::Open(EAppMsgType::Ok, TextBody, &TextTitle);
		// Return
		return false;
	}
	// Copy successful
	else
	{
		// Clean Layout Name and Description fields
		// We copy twice to make sure we can copy.
		// Problem if we only copied once: If the copy fails, the current EditorLayout.ini would be modified and no longer matches the previous one.
		// The ini file should only be modified if it has been successfully copied to the new (and modified) INI file.
		if (bCleanLayoutNameAndDescriptionFields)
		{
			// Update fields
			FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutName", FText::FromString(""));
			FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutDescription", FText::FromString(""));
			// Flush file
			const bool bRead = true;
			GConfig->Flush(bRead, GEditorLayoutIni);
			// Re-copy file
			if (TargetAbsoluteFilePath != FPaths::ConvertRelativePathToFull(GEditorLayoutIni))
			{
				IFileManager::Get().Copy(*InTargetFilePath, *GEditorLayoutIni, bShouldReplace, bCopyEvenIfReadOnly, bCopyAttributes);
			}
		}
		// Unload target file so it can be re-read into cache properly the next time it is used
		GConfig->UnloadFile(InTargetFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		// Display Editor toast to inform the user of the result of the operation
		if (bShowSaveToast)
		{
			// Code copied from EditorViewportClient.cpp --> FEditorViewportClient::TakeScreenshot(...) to maintain same format than when saving a screenshot
			FNotificationInfo Info(FText::GetEmpty());
			Info.ExpireDuration = 5.0f;
			Info.bUseSuccessFailIcons = false;
			Info.bUseLargeFont = false;
			TSharedPtr<SNotificationItem> SaveMessagePtr = FSlateNotificationManager::Get().AddNotification(Info);
			if (SaveMessagePtr.IsValid())
			{
				const FString& HyperLinkString = TargetAbsoluteFilePath;
				auto OpenScreenshotFolder = [HyperLinkString]
				{
					FPlatformProcess::ExploreFolder(*FPaths::GetPath(HyperLinkString));
				};
				SaveMessagePtr->SetText(LOCTEXT("SuccessfulSave_Toast", "Editor layout file saved as"));
				SaveMessagePtr->SetHyperlink(FSimpleDelegate::CreateLambda(OpenScreenshotFolder), FText::FromString(HyperLinkString));
				SaveMessagePtr->SetCompletionState(SNotificationItem::CS_Success);
			}
		}
		// Return successful copy message
		return true;
	}
}

// Name into display text
FText GetDisplayTextInternal(const FString& InString)
{
	const FText DisplayNameText = FText::FromString(FPaths::GetBaseFilename(*InString));
	const bool bIsBool = false;
	const FText DisplayName = FText::FromString(FName::NameToDisplayString(DisplayNameText.ToString(), bIsBool));
	return DisplayName;
}
FText GetTooltipTextInternal(const FText& InDisplayName, const FString& InLayoutFilePath, const FText& InLayoutName)
{
	FText Tooltip;
	if (InLayoutName.IsEmpty())
	{
		Tooltip = FText::Format(LOCTEXT("DisplayNameFmt", "Layout name:\n{0}\n\nFull file path:\n{1}"), InDisplayName, FText::FromString(InLayoutFilePath));
	}
	else
	{
		Tooltip = FText::Format(LOCTEXT("LayoutNameFmt", "Description:\n{0}.\n\nFull file path:\n{1}"), InLayoutName, FText::FromString(InLayoutFilePath));
	}
	return Tooltip;
}

void DisplayLayoutsInternal(FToolMenuSection& InSection, const TArray<FString>& InLayoutIniFileNames, const FString& InLayoutsDirectory, const ELayoutsMenu InLayoutsMenu, const ELayoutsType InLayoutsType)
{
	// If there are Layout ini files, read them
	for (int32 LayoutIndex = 0; LayoutIndex < InLayoutIniFileNames.Num(); ++LayoutIndex)
	{
		const FString LayoutFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::Combine(InLayoutsDirectory, InLayoutIniFileNames[LayoutIndex]));
		// Make sure it is a layout file
		GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
		{
			// Read and display localization name from INI file
			const FText LayoutName = FLayoutSaveRestore::LoadSectionFromConfig(LayoutFilePath, "LayoutName");
			const FText LayoutDescription = FLayoutSaveRestore::LoadSectionFromConfig(LayoutFilePath, "LayoutDescription");
			// If no localization name, then display the file name
			const FText DisplayName = (!LayoutName.IsEmpty() ? LayoutName : GetDisplayTextInternal(InLayoutIniFileNames[LayoutIndex]));
			const FText Tooltip = GetTooltipTextInternal(DisplayName, LayoutFilePath, LayoutDescription);

			// Create UI action here that calls the necessary code in FLayoutsMenuLoad, Save, or Remove 
			FUIAction UIAction;
			if (InLayoutsMenu == ELayoutsMenu::Load && InLayoutsType == ELayoutsType::Engine)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuLoad::LoadLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuLoad::CanLoadChooseLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsLayoutChecked, LayoutIndex));
			}
			else if (InLayoutsMenu == ELayoutsMenu::Load && InLayoutsType == ELayoutsType::User)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuLoad::LoadUserLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuLoad::CanLoadChooseUserLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsUserLayoutChecked, LayoutIndex));
			}
			else if (InLayoutsMenu == ELayoutsMenu::Save && InLayoutsType == ELayoutsType::Engine)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuSave::OverrideLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuSave::CanSaveChooseLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsLayoutChecked, LayoutIndex));
			}
			else if (InLayoutsMenu == ELayoutsMenu::Save && InLayoutsType == ELayoutsType::User)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuSave::OverrideUserLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuSave::CanSaveChooseUserLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsUserLayoutChecked, LayoutIndex));
			}
			else if (InLayoutsMenu == ELayoutsMenu::Remove && InLayoutsType == ELayoutsType::Engine)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuRemove::RemoveLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuRemove::CanRemoveChooseLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsLayoutChecked, LayoutIndex));
			}
			else if (InLayoutsMenu == ELayoutsMenu::Remove && InLayoutsType == ELayoutsType::User)
			{
				UIAction = FUIAction(FExecuteAction::CreateStatic(&FLayoutsMenuRemove::RemoveUserLayout, LayoutIndex),
					FCanExecuteAction::CreateStatic(&FLayoutsMenuRemove::CanRemoveChooseUserLayout, LayoutIndex),
					FIsActionChecked::CreateStatic(&FLayoutsMenuBase::IsUserLayoutChecked, LayoutIndex));
			}
			else
			{
				ensure(false);
			}
			// Run desired action
			InSection.AddMenuEntry(NAME_None, DisplayName, Tooltip, FSlateIcon(), FToolUIActionChoice(UIAction));
		}
	}
}

// GetOriginalEditorLayoutIniFilePathInternal() and GetDuplicatedEditorLayoutIniFilePathInternal() are used because sometimes the layout saved is not the same than the one loaded, even
// though the visual display and screenshot are 100% the same. In those cases, we still want to show the check mark in the load/save/remove menu indicating that the layout is the same
// than the one in the loaded ini file, even though the actual files might not be exactly the same. In addition, this also helps when temporarily closing unrecognized tabs (i.e., the
// ones that FTabManager::SpawnTab cannot recognized and keeps closed but still saves them in the layout).
const FString& GetOriginalEditorLayoutIniFilePathInternal()
{
	static const FString OriginalEditorLayoutIniFilePath = GEditorLayoutIni + TEXT("_orig.ini");
	return OriginalEditorLayoutIniFilePath;
}

const FString& GetDuplicatedEditorLayoutIniFilePathInternal()
{
	static const FString DuplicatedEditorLayoutIniFilePath = GEditorLayoutIni + TEXT("_temp.ini");
	return DuplicatedEditorLayoutIniFilePath;
}

bool AreFilesIdenticalInternal(const FString& InFirstFileFullPath, const FString& InSecondFileFullPath)
{
	// Checked if same file. I.e.,
		// 1. Same size
		// 2. And same internal text
	const bool bHaveSameSize = (IFileManager::Get().FileSize(*InFirstFileFullPath) == IFileManager::Get().FileSize(*InSecondFileFullPath));
	// Same size --> Same layout file?
	if (bHaveSameSize)
	{
		// Read files and check whether they have the exact same text
		FString StringFirstFileFullPath;
		FFileHelper::LoadFileToString(StringFirstFileFullPath, *InFirstFileFullPath);
		FString StringSecondFileFullPath;
		FFileHelper::LoadFileToString(StringSecondFileFullPath, *InSecondFileFullPath);
		// (No) same text = (No) same layout file
		return (StringFirstFileFullPath == StringSecondFileFullPath);
	}
	// No same size = No same layout file
	else
	{
		return false;
	}
}

void RemoveTempEditorLayoutIniFilesInternal()
{
	const bool bRequireExists = false;
	const bool bEvenIfReadOnly = true;
	const bool bIsQuiet = false;
	// DuplicatedEditorLayoutIniFilePath
	const FString& DuplicatedEditorLayoutIniFilePath = GetDuplicatedEditorLayoutIniFilePathInternal();
	IFileManager::Get().Delete(*DuplicatedEditorLayoutIniFilePath, bRequireExists, bEvenIfReadOnly, bIsQuiet);
	GConfig->UnloadFile(DuplicatedEditorLayoutIniFilePath);
	// OriginalEditorLayoutIniFilePath
	const FString& OriginalEditorLayoutIniFilePath = GetOriginalEditorLayoutIniFilePathInternal();
	IFileManager::Get().Delete(*OriginalEditorLayoutIniFilePath, bRequireExists, bEvenIfReadOnly, bIsQuiet);
	GConfig->UnloadFile(OriginalEditorLayoutIniFilePath);
}

bool IsLayoutCheckedInternal(const FString& InLayoutFullPath, const bool bCheckTempFileToo)
{
	// If same file, return true
	if (AreFilesIdenticalInternal(InLayoutFullPath, GEditorLayoutIni))
	{
		return true;
	}
	// No same size, check if same than temporary one
	else if (bCheckTempFileToo)
	{
		const FString& OriginalEditorLayoutIniFilePath = GetOriginalEditorLayoutIniFilePathInternal();
		const FString& DuplicatedEditorLayoutIniFilePath = GetDuplicatedEditorLayoutIniFilePathInternal();
		if (AreFilesIdenticalInternal(GEditorLayoutIni, DuplicatedEditorLayoutIniFilePath))
		{
			return AreFilesIdenticalInternal(InLayoutFullPath, OriginalEditorLayoutIniFilePath);
		}
		// If GEditorLayoutIni != DuplicatedEditorLayoutIniFilePath, remove DuplicatedEditorLayoutIniFilePath & OriginalEditorLayoutIniFilePath
		else
		{
			RemoveTempEditorLayoutIniFilesInternal();
			return false;
		}
	}
	// No same size, and we should not check if same than temporary ones, so then it is false
	else
	{
		return false;
	}
}

void MakeXLayoutsMenuInternal(UToolMenu* InToolMenu, const bool bDisplayDefaultLayouts, const ELayoutsMenu InLayoutsMenu)
{
#if !PLATFORM_MAC // On Mac, each time a key is pressed, all menus are re-generated, stalling the Editor given that SaveLayout is slow on Mac because it does not caches as in Windows.
	// Update GEditorLayoutIni file. Otherwise, we could not track the changes the user did since the layout was loaded
	FLayoutsMenuSave::SaveLayout();
#endif
	// UE Default Layouts
	if (bDisplayDefaultLayouts)
	{
		FToolMenuSection& Section = InToolMenu->AddSection("DefaultLayouts", LOCTEXT("DefaultLayoutsHeading", "Default Layouts"));
		// Get LayoutsDirectory path
		const FString LayoutsDirectory = CreateAndGetDefaultLayoutDirInternal();
		// Get Layout init files
		const TArray<FString> LayoutIniFileNames = GetIniFilesInFolderInternal(LayoutsDirectory);
		// If there are user Layout ini files, read and display them
		DisplayLayoutsInternal(Section, LayoutIniFileNames, LayoutsDirectory, InLayoutsMenu, ELayoutsType::Engine);
	}
	// User Layouts
	{
		FToolMenuSection& Section = InToolMenu->AddSection("UserDefaultLayouts", LOCTEXT("UserDefaultLayoutsHeading", "User Layouts"));
		// (Create if it does not exist and) Get UserLayoutsDirectory path
		const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
		// Get User Layout init files
		const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
		// If there are user Layout ini files, read and display them
		DisplayLayoutsInternal(Section, UserLayoutIniFileNames, UserLayoutsDirectory, InLayoutsMenu, ELayoutsType::User);
	}
}

// All can be read
/**
 * Static
 * Checks if the selected layout can be read (e.g., when loading layouts).
 * @param	InLayoutIndex  Index from the selected layout.
 * @return true if the selected layout can be read.
 */
bool CanChooseLayoutWhenReadInternal(const int32 InLayoutIndex)
{
	return true;
}
/**
 * Static
 * Checks if the selected user-created layout can be read (e.g., when loading user layouts).
 * @param	InLayoutIndex  Index from the selected user-created layout.
 * @return true if the selected user-created layout can be read.
 */
bool CanChooseUserLayoutWhenReadInternal(const int32 InLayoutIndex)
{
	return true;
}
// Only the layouts created by the user can be modified
/**
 * Static
 * Checks if the selected layout can be modified (e.g., when overriding or removing layouts).
 * @param	InLayoutIndex  Index from the selected layout.
 * @return true if the selected layout can be modified/removed.
 */
bool CanChooseLayoutWhenWriteInternal(const int32 InLayoutIndex)
{
	return false;
}
/**
 * Static
 * Checks if the selected user-created layout can be modified (e.g., when overriding or removing user layouts).
 * @param	InLayoutIndex  Index from the selected user-created layout.
 * @return true if the selected user-created layout can be modified/removed.
 */
bool CanChooseUserLayoutWhenWriteInternal(const int32 InLayoutIndex)
{
	return true;
}

void SaveLayoutWithoutRemovingTempLayoutFiles()
{
	// Save the layout into the Editor
	FGlobalTabmanager::Get()->SaveAllVisualState();
	// Write the saved layout to disk (if it has changed since the last time it was read/written)
	// We must set bRead = true. Otherwise, FLayoutsMenuLoad::ReloadCurrentLayout() would reload the old config file (because it would be cached on memory)
	const bool bRead = true;
	GConfig->Flush(bRead, GEditorLayoutIni);
}

/**
 * It simply check whether PIE, SIE, or any Asset Editor is opened, and ask the user whether he wanna continue closing them or cancel the Editor layout load
 * @return Whether we should continue loading the layout
 */
bool CheckAskUserAndClosePIESIEAndAssetEditors(const FText& InitialMessage)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = (GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr);
	if (!GEditor || !AssetEditorSubsystem)
	{
		ensureMsgf(false, TEXT("Both GEditor and AssetEditorSubsystem should not be false when CheckAskUserAndClosePIESIEAndAssetEditors() is called."));
		return true;
	}
	// If none are running, return
	const bool bIsPIEOrSIERunning = ((GEditor && GEditor->PlayWorld) || GIsPlayInEditorWorld);
	const TArray<UObject*> AllEditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	const bool bAreAssetEditorOpened = (AllEditedAssets.Num() > 0);
	if (!bIsPIEOrSIERunning && !bAreAssetEditorOpened)
	{
		return true;
	}
	// Collect all open assets
	FText OpenedEditorAssets;
	if (bAreAssetEditorOpened)
	{
		FString AllAssets;
		for (const UObject* EditedAsset : AllEditedAssets)
		{
			if (EditedAsset->IsAsset())
			{
				if (!AllAssets.IsEmpty())
					AllAssets += TEXT(", ");
				AllAssets += EditedAsset->GetName();
			}
		}
		OpenedEditorAssets = FText::Format(LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsOpenEditorAssets", "\nOpen Asset Editors: {0}."), FText::FromString(AllAssets));
	}
	else
	{
		OpenedEditorAssets = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsOpenEditorAssetsEmpty", "\n");
	}
	FText TextTitle;
	FText IfYesText;
	// If both PIE/SIE and Asset Editors are opened
	if (bIsPIEOrSIERunning && bAreAssetEditorOpened)
	{
		TextTitle = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesHeaderAll", "Close PIE/SIE and Asset Editors?");
		IfYesText = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesBodyAll", "If \"Yes\", your current game instances (PIE or SIE) as well as all open Asset Editors"
			" will be closed. Any unsaved changes in those will also be lost.");
	}
	// If PIE or SIE are opened
	else if (bIsPIEOrSIERunning)
	{
		TextTitle = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesHeaderPIE", "Close PIE/SIE?");
		IfYesText = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesBodyPIE", "If \"Yes\", your current game instances (PIE or SIE) will be closed. Any unsaved changes"
			" in those will also be lost.");
	}
	// If any Asset Editors is opened
	else if (bAreAssetEditorOpened)
	{
		TextTitle = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesHeaderEditorAssets", "Close Asset Editors?");
		IfYesText = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfYesBodyEditorAssets", "If \"Yes\", all open Asset Editors will be closed. Any unsaved changes in those"
			" will also be lost.");
	}
	// FMessageDialog
	const FText IfNoText = LOCTEXT("CheckAskUserAndClosePIESIEAndAssetEditorsIfNoBody", "If \"No\", you can manually reload the layout from the \"User Layouts\" section later.");
	const FText TextBody = FText::Format(LOCTEXT("ClosePIESIEAssetEditorsBody", "{0}\n\n{1}{2}\n\n{3}"), InitialMessage, IfYesText, OpenedEditorAssets, IfNoText);
	if (EAppReturnType::Yes != FMessageDialog::Open(EAppMsgType::YesNo, TextBody, &TextTitle))
	{
		return false;
	}
	// If PIE or SIE are opened, ask the user whether he wants to automatically close them and continue loading the layout
	if (bIsPIEOrSIERunning)
	{
		// Close PIE/SIE
		if (GEditor && GEditor->PlayWorld)
		{
			GEditor->EndPlayMap();
		}
		else
		{
			ensureMsgf(false,
				TEXT("This has not been tested because the code does not reach this by default. The layout is loaded through the Editor UI, and GIsPlayInEditorWorld should"
					" not have any kind of Editor UI, so it should not be possible to load a layout in that status."));
		}
	}
	// If any Asset Editors is opened, ask the user whether he wants to automatically close them and continue loading the layout
	if (bAreAssetEditorOpened)
	{
		// Close asset editors
		AssetEditorSubsystem->CloseAllAssetEditors();
	}
	return true;
}

	FText GenerateLocalizedTextForFile(const FText& InText)
	{
		// Proper FText to FString
		FString StringSimulatingText;
		FTextStringHelper::WriteToBuffer(StringSimulatingText, InText);
		// Sanitize text (truncate if too big)
		FString SanitazedTruncatedText = (StringSimulatingText.Len() < 100 ? StringSimulatingText : StringSimulatingText.Left(100));
		FSaveLayoutDialogUtils::SanitizeText(SanitazedTruncatedText);
		// Create full file path
		if (StringSimulatingText.Len() < 10 || StringSimulatingText.Left(9) != TEXT("NSLOCTEXT"))
		{
			FString StringSimulatingTextRecreated =
				// Namespace
				TEXT("NSLOCTEXT(\"LayoutNamespace\", "
				// Key
				"\"") + SanitazedTruncatedText + TEXT("\", "
				// Source string
				"\"") + StringSimulatingText + TEXT("\")");
			return FText::FromString(StringSimulatingTextRecreated);
		}
		else
		{
			return FText::FromString(StringSimulatingText);
		}
	}

	void SaveExportLayoutCommon(const FString& InDefaultDirectory, const bool bMustBeSavedInDefaultDirectory, const FText& InWhatIsThis, const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields)
	{
		// Export/SaveAs the user-selected layout configuration files and load one of them
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			bool bWereFilesSelected = false;
			TArray<FString> LayoutFilePaths;
			bool bWasDialogOpened = bMustBeSavedInDefaultDirectory;
			// "Save Layout As..."
			if (bMustBeSavedInDefaultDirectory)
			{
				TArray<FText> LayoutNames;
				LayoutNames.Emplace(FLayoutSaveRestore::LoadSectionFromConfig(GEditorLayoutIni, "LayoutName"));
				TArray<FText> LayoutDescriptions;
				LayoutDescriptions.Emplace(FLayoutSaveRestore::LoadSectionFromConfig(GEditorLayoutIni, "LayoutDescription"));
				// We want to avoid the duplication of the file/name/description fields, so we add "Copy of " at the beginning of the name and description fields
				if (LayoutNames[0].ToString().Len() > 0)
				{
					LayoutNames[0] = FText::FromString(TEXT("Copy of ") + LayoutNames[0].ToString());
				}
				if (LayoutDescriptions[0].ToString().Len() > 0)
				{
					LayoutDescriptions[0] = FText::FromString(TEXT("Copy of ") + LayoutDescriptions[0].ToString());
				}
				// Create SWidget for saving the layout in its own SWindow and block the thread until it is finished
				const TSharedRef<FSaveLayoutDialogParams> SaveLayoutDialogParams = MakeShared<FSaveLayoutDialogParams>(InDefaultDirectory, TEXT(".ini"), LayoutNames, LayoutDescriptions);
				bWasDialogOpened = FSaveLayoutDialogUtils::CreateSaveLayoutAsDialogInStandaloneWindow(SaveLayoutDialogParams);
				bWereFilesSelected = SaveLayoutDialogParams->bWereFilesSelected;
				LayoutFilePaths = SaveLayoutDialogParams->LayoutFilePaths;
				LayoutNames = SaveLayoutDialogParams->LayoutNames;
				LayoutDescriptions = SaveLayoutDialogParams->LayoutDescriptions;

				// Update GEditorLayoutIni file if LayoutNames or LayoutDescriptions were modified by the user
				if (bWasDialogOpened && LayoutNames.Num() > 0 && LayoutDescriptions.Num() > 0 && (LayoutNames[0].ToString().Len() > 0 || LayoutDescriptions[0].ToString().Len() > 0))
				{
					checkf(LayoutNames.Num() == LayoutDescriptions.Num(), TEXT("There should be the same number of LayoutNames and LayoutDescriptions."));
					for (int32 Index = 0; Index < LayoutNames.Num(); ++Index)
					{
						const FText& LayoutNameAsTextText = GenerateLocalizedTextForFile(LayoutNames[Index]);
						const FText& LayoutDescriptionAsTextText = GenerateLocalizedTextForFile(LayoutDescriptions[Index]);
						// Update fields
						FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutName", LayoutNameAsTextText);
						FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutDescription", LayoutDescriptionAsTextText);
						// Flush file
						const bool bRead = true;
						GConfig->Flush(bRead, GEditorLayoutIni);
					}
				}
			}
			// "Export Layout..." (or "Save Layout As..." dialog could not be opened)
			if (!bWasDialogOpened)
			{
				// Open the "save file" dialog so user can save his/her layout configuration file
				const FString DefaultFile = "";
				bWereFilesSelected = DesktopPlatform->SaveFileDialog(
					FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
					TEXT("Export a Layout Configuration File"),
					InDefaultDirectory,
					DefaultFile,
					TEXT("Layout configuration files|*.ini|"),
					EFileDialogFlags::None, //EFileDialogFlags::Multiple, // Allow/Avoid multiple file selection
					LayoutFilePaths
				);
			}
			// If file(s) selected, copy them into the user layouts directory and load one of them
			if (bWereFilesSelected && LayoutFilePaths.Num() > 0)
			{
				// Iterate over selected layout ini files
				FString FirstGoodLayoutFile = "";
				for (const FString& LayoutFilePath : LayoutFilePaths)
				{
					// If writing in the right folder
					const FString LayoutFilePathAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::GetPath(LayoutFilePath));
					const FString DefaultDirectoryAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::GetPath(InDefaultDirectory));
					if (!bMustBeSavedInDefaultDirectory || (LayoutFilePathAbsolute == DefaultDirectoryAbsolute))
					{
						// Save in the user layout folder
						const FString& SourceFilePath = GEditorLayoutIni;
						const FString& TargetFilePath = LayoutFilePath;
						const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = !bMustBeSavedInDefaultDirectory;
						const bool bShowSaveToast = true;
						TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, InWhatIsThis, bCleanLayoutNameAndDescriptionFieldsIfNoSameValues,
							bShouldAskBeforeCleaningLayoutNameAndDescriptionFields, bShowSaveToast);
					}
					// If trying to write in a different folder (which is not allowed)
					else
					{
						// Warn the user that the file will not be copied in there
						const FText Title = LOCTEXT("SaveAsFailedMsg_Title", "Save As Failed");
						FMessageDialog::Open(
							EAppMsgType::Ok,
							FText::Format(
								LOCTEXT("SaveAsFailedMsg",
									"In order to save the layout and allow Unreal to use it, you must save it in the predefined folder:\n{0}\n\nNevertheless, you tried to save it in:\n{1}\n\n"
									"If you simply wish to export a copy of the current configuration in {1} (e.g., to later copy it into a different machine), you could use the \"Export Layout...\""
									" functionality. However, Unreal would not be able to load it until you import it with \"Import Layout...\"."),
								FText::FromString(DefaultDirectoryAbsolute), FText::FromString(LayoutFilePathAbsolute)),
							&Title);
					}
				}
			}
		}
	}

	int32 GetNumberLayoutFiles(const FString& InLayoutsDirectory)
	{
		// Get Layout init files in desired directory
		const TArray<FString> LayoutIniFileNames = GetIniFilesInFolderInternal(InLayoutsDirectory);
		// Count how many layout files exist
		int32 NumberLayoutFiles = 0;
		for (const FString& LayoutIniFileName : LayoutIniFileNames)
		{
			const FString LayoutFilePath = FPaths::Combine(InLayoutsDirectory, LayoutIniFileName);
			GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
			if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
			{
				++NumberLayoutFiles;
			}
		}
		// Return result
		return NumberLayoutFiles;
	}
};



void FLayoutsMenuLoad::MakeLoadLayoutsMenu(UToolMenu* InToolMenu)
{
	// MakeLoadLayoutsMenu
	const bool bDisplayDefaultLayouts = true;
	FPrivateFLayoutsMenu::MakeXLayoutsMenuInternal(InToolMenu, bDisplayDefaultLayouts, FPrivateFLayoutsMenu::ELayoutsMenu::Load);

	// Additional sections
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

		// Separator
		if (FLayoutsMenuBase::IsThereUserLayouts())
		{
			Section.AddMenuSeparator("AdditionalSectionsSeparator");
		}

		// Import...
		{
			Section.AddMenuEntry(FMainFrameCommands::Get().ImportLayout);
		}
	}
}

bool FLayoutsMenuLoad::CanLoadChooseLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsLayoutChecked(InLayoutIndex) && FPrivateFLayoutsMenu::CanChooseLayoutWhenReadInternal(InLayoutIndex);
}
bool FLayoutsMenuLoad::CanLoadChooseUserLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsUserLayoutChecked(InLayoutIndex) && FPrivateFLayoutsMenu::CanChooseUserLayoutWhenReadInternal(InLayoutIndex);
}

void FLayoutsMenuLoad::ReloadCurrentLayout()
{
	// If PIE, SIE, or any Asset Editors are opened, ask the user whether he wants to automatically close them and continue loading the layout
	if (!FPrivateFLayoutsMenu::CheckAskUserAndClosePIESIEAndAssetEditors(LOCTEXT("AreYouSureToLoadHeader", "Are you sure you want to continue loading the selected layout profile?")))
	{
		return;
	}
	// Create duplicated ini file (OriginalEditorLayoutIniFilePath)
	// Explanation:
	//     Assume a layout is saved with (at least) a window that is dependent on a plugin. If that plugin is disabled and the editor restarted, that window will be saved on the layout but will
	//     not visually appear. We still wanna save the layout with it, so if its plugin is re-enabled, the window appear again. However, while the plugin is disabled, the layout ini file changes
	//     to reflect that the plugin is not opened.
	// Technical details:
	//     Rather than changing the string generated in the ini file (which could affect other parts of the code), we will duplicate the ini file when loaded. If the ini file is different than
	//     its duplicated copy, then some widget is missing (most probably due to disabled plugins). If that is the case, we will re-save the ini file without telling UE that it changed. This way,
	//     the ini file would match its original one, and it would only be re-modified if the user modifies the layout (but in that case it should no longer match the original one).
	const bool bShouldReplace = true;
	const bool bEvenIfReadOnly = true;
	const bool bCopyAttributes = false; // If true, it could e.g., copy the read-only flag of DefaultLayout.ini and make all the save/load stuff stop working
	const FString& OriginalEditorLayoutIniFilePath = FPrivateFLayoutsMenu::GetOriginalEditorLayoutIniFilePathInternal();
	IFileManager::Get().Copy(*OriginalEditorLayoutIniFilePath, *GEditorLayoutIni, bShouldReplace, bEvenIfReadOnly, bCopyAttributes);
	GConfig->UnloadFile(OriginalEditorLayoutIniFilePath);
	// Editor is reset on-the-fly
	FUnrealEdMisc::Get().AllowSavingLayoutOnClose(false);
	EditorReinit();
	FUnrealEdMisc::Get().AllowSavingLayoutOnClose(true);
	// Save layout and create duplicated ini file (DuplicatedEditorLayoutIniFilePath)
	FPrivateFLayoutsMenu::SaveLayoutWithoutRemovingTempLayoutFiles();
	// If same file, remove temp files
	const bool bCheckTempFileToo = false;
	if (FPrivateFLayoutsMenu::IsLayoutCheckedInternal(OriginalEditorLayoutIniFilePath, bCheckTempFileToo))
	{
		FPrivateFLayoutsMenu::RemoveTempEditorLayoutIniFilesInternal();
	}
	// Else, create DuplicatedEditorLayoutIniFilePath
	else
	{
		const FString& DuplicatedEditorLayoutIniFilePath = FPrivateFLayoutsMenu::GetDuplicatedEditorLayoutIniFilePathInternal();
		IFileManager::Get().Copy(*DuplicatedEditorLayoutIniFilePath, *GEditorLayoutIni, bShouldReplace, bEvenIfReadOnly, bCopyAttributes);
		GConfig->UnloadFile(DuplicatedEditorLayoutIniFilePath);
	}
}

void FLayoutsMenuLoad::LoadLayout(const FString& InLayoutPath)
{
	// Replace main layout with desired one
	const FString& SourceFilePath = InLayoutPath;
	const FString& TargetFilePath = GEditorLayoutIni;
	const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
	const bool SucessfullySaved = FPrivateFLayoutsMenu::TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, LOCTEXT("LoadLayoutText", "loading the layout"),
		bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
	// Reload current layout
	if (SucessfullySaved)
	{
		FLayoutsMenuLoad::ReloadCurrentLayout();
	}
}

void FLayoutsMenuLoad::LoadLayout(const int32 InLayoutIndex)
{
	// Replace main layout with desired one, reset layout & restart Editor
	LoadLayout(FLayoutsMenuBase::GetLayout(InLayoutIndex));
}

void FLayoutsMenuLoad::LoadUserLayout(const int32 InLayoutIndex)
{
	// Replace main layout with desired one, reset layout & restart Editor
	LoadLayout(FLayoutsMenuBase::GetUserLayout(InLayoutIndex));
}

void FLayoutsMenuLoad::ImportLayout()
{
	// Import the user-selected layout configuration files and load one of them
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		// Open File Dialog so user can select his/her desired layout configuration files
		TArray<FString> LayoutFilePaths;
		const FString LastDirectory = FPaths::ProjectContentDir();
		const FString DefaultDirectory = LastDirectory;
		const FString DefaultFile = "";
		const bool bWereFilesSelected = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import a Layout Configuration File"),
			DefaultDirectory,
			DefaultFile,
			TEXT("Layout configuration files|*.ini|"),
			EFileDialogFlags::Multiple, //EFileDialogFlags::None, // Allow/Avoid multiple file selection
			LayoutFilePaths
		);
		// If file(s) selected, copy them into the user layouts directory and load one of them
		if (bWereFilesSelected && LayoutFilePaths.Num() > 0)
		{
			// (Create if it does not exist and) Get UserLayoutsDirectory path
			const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
			// Iterate over selected layout ini files
			FString FirstGoodLayoutFile = "";
			const FText TrySaveLayoutOrWarnInternalText = LOCTEXT("ImportLayoutText", "importing the layout");
			for (const FString& LayoutFilePath : LayoutFilePaths)
			{
				// If file is a layout file, import it
				GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
				if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
				{
					if (FirstGoodLayoutFile == "")
					{
						FirstGoodLayoutFile = LayoutFilePath;
					}
					// Save in the user layout folder
					const FString& SourceFilePath = LayoutFilePath;
					const FString& TargetFilePath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), FPaths::GetCleanFilename(LayoutFilePath));
					const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
					const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
					FPrivateFLayoutsMenu::TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, TrySaveLayoutOrWarnInternalText,
						bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
				}
				// File is not a layout file, warn the user
				else
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("FileName"), FText::FromString(FPaths::ConvertRelativePathToFull(LayoutFilePath)));
					const FText TextBody = FText::Format(LOCTEXT("UnsuccessfulImportBody", "Unsuccessful import, {FileName} is not a layout configuration file!"), Arguments);
					const FText TextTitle = LOCTEXT("UnsuccessfulImportHeader", "Unsuccessful Import!");
					FMessageDialog::Open(EAppMsgType::Ok, TextBody, &TextTitle);
				}
			}
			// If PIE, SIE, or any Asset Editors are opened, ask the user whether he wants to automatically close them and continue loading the layout
			if (!FPrivateFLayoutsMenu::CheckAskUserAndClosePIESIEAndAssetEditors(LOCTEXT("LayoutImportClosePIEAndEditorAssetsHeader",
				"The layout(s) were successfully imported into the \"User Layouts\" section. Do you want to continue loading the selected layout profile?")))
			{
				return;
			}
			// Replace current layout with first one
			if (FirstGoodLayoutFile != "")
			{
				const FString& SourceFilePath = FirstGoodLayoutFile;
				const FString& TargetFilePath = GEditorLayoutIni;
				const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
				const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
				const bool SucessfullySaved = FPrivateFLayoutsMenu::TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, TrySaveLayoutOrWarnInternalText,
					bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
				// Reload current layout
				if (SucessfullySaved)
				{
					ReloadCurrentLayout();
				}
			}
		}
	}
}



void FLayoutsMenuSave::MakeSaveLayoutsMenu(UToolMenu* InToolMenu)
{
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		// MakeOverrideLayoutsMenu
		const bool bDisplayDefaultLayouts = false;
		FPrivateFLayoutsMenu::MakeXLayoutsMenuInternal(InToolMenu, bDisplayDefaultLayouts, FPrivateFLayoutsMenu::ELayoutsMenu::Save);

		// Additional sections
		{
			FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

			// Separator
			if (FLayoutsMenuBase::IsThereUserLayouts())
			{
				Section.AddMenuSeparator("AdditionalSectionsSeparator");
			}

			// Save as...
			{
				Section.AddMenuEntry(FMainFrameCommands::Get().SaveLayoutAs);
			}

			// Export...
			{
				Section.AddMenuEntry(FMainFrameCommands::Get().ExportLayout);
			}
		}
	}
}

bool FLayoutsMenuSave::CanSaveChooseLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsLayoutChecked(InLayoutIndex) && FPrivateFLayoutsMenu::CanChooseLayoutWhenWriteInternal(InLayoutIndex);
}
bool FLayoutsMenuSave::CanSaveChooseUserLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsUserLayoutChecked(InLayoutIndex) && FPrivateFLayoutsMenu::CanChooseUserLayoutWhenWriteInternal(InLayoutIndex);
}

void FLayoutsMenuSave::OverrideLayout(const int32 InLayoutIndex)
{
	// Default layouts should never be modified, so this function should never be called
	UE_LOG(LogLayoutsMenu, Fatal, TEXT("Default layouts should never be modified, so this function should never be called."));
}

void FLayoutsMenuSave::OverrideUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	// Get User Layout init files
	const TArray<FString> UserLayoutIniFileNames = FPrivateFLayoutsMenu::GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Are you sure you want to do this?
	if (!FSaveLayoutDialogUtils::OverrideLayoutDialog(UserLayoutIniFileNames[InLayoutIndex]))
	{
		return;
	}
	// Target and source files
	const FString& SourceFilePath = GEditorLayoutIni;
	const FString& TargetFilePath = DesiredUserLayoutFullPath;
	// Update GEditorLayoutIni file
	SaveLayout();
	// Replace desired layout with current one
	const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = true;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
	const bool bShowSaveToast = true;
	FPrivateFLayoutsMenu::TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, LOCTEXT("OverrideLayoutText", "overriding the layout"),
		bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, bShouldAskBeforeCleaningLayoutNameAndDescriptionFields, bShowSaveToast);
}

void FLayoutsMenuSave::SaveLayout()
{
	// Save layout
	FPrivateFLayoutsMenu::SaveLayoutWithoutRemovingTempLayoutFiles();
	// Remove temporary Editor Layout ini files if the layout (thus also GEditorLayoutIni) changed
	const bool bCheckTempFileToo = false;
	const FString& DuplicatedEditorLayoutIniFilePath = FPrivateFLayoutsMenu::GetDuplicatedEditorLayoutIniFilePathInternal();
	if (!FPrivateFLayoutsMenu::IsLayoutCheckedInternal(DuplicatedEditorLayoutIniFilePath, bCheckTempFileToo))
	{
		FPrivateFLayoutsMenu::RemoveTempEditorLayoutIniFilesInternal();
	}
}

void FLayoutsMenuSave::SaveLayoutAs()
{
	// Update GEditorLayoutIni file
	SaveLayout();
	// Copy GEditorLayoutIni into desired file
	const FString DefaultDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	const bool bMustBeSavedInDefaultDirectory = true;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
	FPrivateFLayoutsMenu::SaveExportLayoutCommon(DefaultDirectory, bMustBeSavedInDefaultDirectory, LOCTEXT("SaveLayoutText", "saving the layout"),
		bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
}

void FLayoutsMenuSave::ExportLayout()
{
	// Update GEditorLayoutIni file
	SaveLayout();
	// Copy GEditorLayoutIni into desired file
	const FString DefaultDirectory = FPaths::ProjectContentDir();
	const bool bMustBeSavedInDefaultDirectory = false;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = true;
	FPrivateFLayoutsMenu::SaveExportLayoutCommon(DefaultDirectory, bMustBeSavedInDefaultDirectory, LOCTEXT("ExportLayoutText", "exporting the layout"),
		bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
}



void FLayoutsMenuRemove::MakeRemoveLayoutsMenu(UToolMenu* InToolMenu)
{
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		// MakeRemoveLayoutsMenu
		const bool bDisplayDefaultLayouts = false;
		FPrivateFLayoutsMenu::MakeXLayoutsMenuInternal(InToolMenu, bDisplayDefaultLayouts, FPrivateFLayoutsMenu::ELayoutsMenu::Remove);

		// Additional sections
		{
			FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

			// Separator
			if (FLayoutsMenuBase::IsThereUserLayouts())
			{
				Section.AddMenuSeparator("AdditionalSectionsSeparator");
			}

			// Remove all
			Section.AddMenuEntry(FMainFrameCommands::Get().RemoveUserLayouts);
		}
	}
}

bool FLayoutsMenuRemove::CanRemoveChooseLayout(const int32 InLayoutIndex)
{
	return FPrivateFLayoutsMenu::CanChooseLayoutWhenWriteInternal(InLayoutIndex);
}
bool FLayoutsMenuRemove::CanRemoveChooseUserLayout(const int32 InLayoutIndex)
{
	return FPrivateFLayoutsMenu::CanChooseUserLayoutWhenWriteInternal(InLayoutIndex);
}

void FLayoutsMenuRemove::RemoveLayout(const int32 InLayoutIndex)
{
	// Default layouts should never be modified, so this function should never be called
	UE_LOG(LogLayoutsMenu, Fatal, TEXT("Default layouts should never be modified, so this function should never be called."));
}

void FLayoutsMenuRemove::RemoveUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	// Get User Layout init files
	const TArray<FString> UserLayoutIniFileNames = FPrivateFLayoutsMenu::GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Are you sure you want to do this?
	const FText TextFileNameToRemove = FText::FromString(FPaths::GetBaseFilename(UserLayoutIniFileNames[InLayoutIndex]));
	const FText TextBody = FText::Format(LOCTEXT("ActionRemoveMsg",
		"Are you sure you want to permanently delete the layout profile \"{0}\"? This action cannot be undone."), TextFileNameToRemove);
	const FText TextTitle = FText::Format(LOCTEXT("RemoveUILayout_Title", "Remove UI Layout \"{0}\"?"), TextFileNameToRemove);
	if (EAppReturnType::Ok != FMessageDialog::Open(EAppMsgType::OkCancel, TextBody, &TextTitle))
	{
		return;
	}
	// Remove layout
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*DesiredUserLayoutFullPath);
}

void FLayoutsMenuRemove::RemoveUserLayouts()
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	// Count how many layout files exist
	const int32 NumberUserLayoutFiles = FPrivateFLayoutsMenu::GetNumberLayoutFiles(UserLayoutsDirectory);
	// If files to remove, warn user and remove them all
	if (NumberUserLayoutFiles > 0)
	{
		// Are you sure you want to do this?
		const FText TextBody = FText::Format(LOCTEXT("ActionRemoveAllUserLayoutMsg",
			"Are you sure you want to permanently remove {0} layout {0}|plural(one=profile,other=profiles)? This action cannot be undone."), NumberUserLayoutFiles);
		const FText TextTitle = LOCTEXT("RemoveAllUserLayouts_Title", "Remove All User-Created Layouts?");
		if (EAppReturnType::Ok != FMessageDialog::Open(EAppMsgType::OkCancel, TextBody, &TextTitle))
		{
			return;
		}
		// Get User Layout init files
		const TArray<FString> UserLayoutIniFileNames = FPrivateFLayoutsMenu::GetIniFilesInFolderInternal(UserLayoutsDirectory);
		// If there are user Layout ini files, read them
		for (const FString& UserLayoutIniFileName : UserLayoutIniFileNames)
		{
			// Remove file if it is a layout
			const FString LayoutFilePath = FPaths::Combine(UserLayoutsDirectory, UserLayoutIniFileName);
			GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
			if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
			{
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*LayoutFilePath);
			}
		}
	}
	// If no files to remove, warn user
	else
	{
		// Show reason
		const FText TextBody = LOCTEXT("UnsuccessfulRemoveLayoutBody", "There are no layout profile files created by the user, so none could be removed.");
		const FText TextTitle = LOCTEXT("UnsuccessfulRemoveLayoutHeader", "Unsuccessful Remove All User Layouts!");
		FMessageDialog::Open(EAppMsgType::Ok, TextBody, &TextTitle);
	}
}



FString FLayoutsMenuBase::GetLayout(const int32 InLayoutIndex)
{
	// Get LayoutsDirectory path, layout init files, and desired layout path
	const FString LayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetDefaultLayoutDirInternal();
	const TArray<FString> LayoutIniFileNames = FPrivateFLayoutsMenu::GetIniFilesInFolderInternal(LayoutsDirectory);
	const FString DesiredLayoutFullPath = FPaths::Combine(FPaths::GetPath(LayoutsDirectory), LayoutIniFileNames[InLayoutIndex]);
	// Return full path
	return DesiredLayoutFullPath;
}

FString FLayoutsMenuBase::GetUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path, user layout init files, and desired user layout path
	const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	const TArray<FString> UserLayoutIniFileNames = FPrivateFLayoutsMenu::GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Return full path
	return DesiredUserLayoutFullPath;
}

bool FLayoutsMenuBase::IsThereUserLayouts()
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPrivateFLayoutsMenu::CreateAndGetUserLayoutDirInternal();
	// At least 1 user layout file?
	return FPrivateFLayoutsMenu::GetNumberLayoutFiles(UserLayoutsDirectory) > 0;
}

bool FLayoutsMenuBase::IsLayoutChecked(const int32 InLayoutIndex)
{
#if PLATFORM_MAC // On Mac, each time a key is pressed, all menus are re-generated, stalling the Editor given that SaveLayout is slow on Mac because it does not caches as in Windows.
	return false;
#else
	// Check if the desired layout file matches the one currently loaded
	const bool bCheckTempFileToo = true;
	return FPrivateFLayoutsMenu::IsLayoutCheckedInternal(GetLayout(InLayoutIndex), bCheckTempFileToo);
#endif
}

bool FLayoutsMenuBase::IsUserLayoutChecked(const int32 InLayoutIndex)
{
#if PLATFORM_MAC // On Mac, each time a key is pressed, all menus are re-generated, stalling the Editor given that SaveLayout is slow on Mac because it does not caches as in Windows.
	return false;
#else
	// Check if the desired layout file matches the one currently loaded
	const bool bCheckTempFileToo = true;
	return FPrivateFLayoutsMenu::IsLayoutCheckedInternal(GetUserLayout(InLayoutIndex), bCheckTempFileToo);
#endif
}

#undef LOCTEXT_NAMESPACE

#endif
