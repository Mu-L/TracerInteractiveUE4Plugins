// Copyright Epic Games, Inc. All Rights Reserved.


#include "Frame/MainFrameActions.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "AboutScreen.h"
#include "CreditsScreen.h"
#include "DesktopPlatformModule.h"
#include "ISourceControlModule.h"
#include "GameProjectGenerationModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "SourceCodeNavigation.h"
#include "SourceControlWindows.h"
#include "ISettingsModule.h"
#include "Interfaces/IProjectManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "PlatformInfo.h"
#include "EditorStyleSet.h"
#include "Settings/EditorExperimentalSettings.h"
#include "CookerSettings.h"
#include "UnrealEdMisc.h"
#include "FileHelpers.h"
#include "EditorAnalytics.h"
#include "LevelEditor.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "InstalledPlatformInfo.h"
#include "Misc/ConfigCacheIni.h"
#include "MainFrameModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Dialogs/SOutputLogDialog.h"
#include "IUATHelperModule.h"
#include "Menus/LayoutsMenu.h"
#include "TargetReceipt.h"

#include "Settings/EditorSettings.h"
#include "AnalyticsEventAttribute.h"
#include "Kismet2/DebuggerCommands.h"
#include "GameMapsSettings.h"
#include "DerivedDataCacheInterface.h"

#define LOCTEXT_NAMESPACE "MainFrameActions"

DEFINE_LOG_CATEGORY_STATIC(MainFrameActions, Log, All);


TSharedRef< FUICommandList > FMainFrameCommands::ActionList( new FUICommandList() );

TWeakPtr<SNotificationItem> FMainFrameActionCallbacks::ChoosePackagesToCheckInNotification;

namespace
{
	const FName SwitchProjectBundle = "SwitchProject";
}

FMainFrameCommands::FMainFrameCommands()
	: TCommands<FMainFrameCommands>(
		TEXT("MainFrame"), // Context name for fast lookup
		LOCTEXT( "MainFrame", "Main Frame" ), // Localized context name for displaying
		NAME_None,	 // No parent context
		FEditorStyle::GetStyleSetName() ), // Icon Style Set
	  ToggleFullscreenConsoleCommand(
		TEXT( "MainFrame.ToggleFullscreen" ),
		TEXT( "Toggles the editor between \"full screen\" mode and \"normal\" mode.  In full screen mode, the task bar and window title area are hidden." ),
		FConsoleCommandDelegate::CreateStatic( &FMainFrameActionCallbacks::ToggleFullscreen_Execute ) )
{
	AddBundle(SwitchProjectBundle, LOCTEXT("SwitchProjectBundle", "Switch Project"));
}


void FMainFrameCommands::RegisterCommands()
{
	// Some commands cannot be processed in a commandlet or if the editor is started without a project
	if ( !IsRunningCommandlet() && FApp::HasProjectName() && !IsRunningDedicatedServer())
	{
		// The global action list was created at static initialization time. Create a handler for otherwise unhandled keyboard input to route key commands through this list.
		FSlateApplication::Get().SetUnhandledKeyDownEventHandler( FOnKeyEvent::CreateStatic( &FMainFrameActionCallbacks::OnUnhandledKeyDownEvent ) );
	}

	// Make a default can execute action that disables input when in debug mode
	FCanExecuteAction DefaultExecuteAction = FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::DefaultCanExecuteAction );

	UI_COMMAND( SaveAll, "Save All", "Saves all unsaved levels and assets to disk", EUserInterfaceActionType::Button, FInputChord( EModifierKey::Control | EModifierKey::Shift, EKeys::S ) );
	ActionList->MapAction( SaveAll, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::SaveAll ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::CanSaveWorld ) );

	UI_COMMAND( ChooseFilesToSave, "Choose Files to Save...", "Opens a dialog with save options for content and levels", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( ChooseFilesToSave, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ChoosePackagesToSave ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::CanSaveWorld ) );

	UI_COMMAND( ChooseFilesToCheckIn, "Submit to Source Control...", "Opens a dialog with check in options for content and levels", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( ChooseFilesToCheckIn, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ChoosePackagesToCheckIn ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::CanChoosePackagesToCheckIn ) );

	UI_COMMAND( ConnectToSourceControl, "Connect To Source Control...", "Connect to source control to allow source control operations to be performed on content and levels.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( ConnectToSourceControl, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ConnectToSourceControl ), DefaultExecuteAction );

	UI_COMMAND( NewProject, "New Project...", "Opens a dialog to create a new game project", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( NewProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::NewProject, false, true), DefaultExecuteAction );

	UI_COMMAND( OpenProject, "Open Project...", "Opens a dialog to choose a game project to open", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( OpenProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::NewProject, true, false), DefaultExecuteAction );

	UI_COMMAND( AddCodeToProject, "New C++ Class...", "Adds C++ code to the project. The code can only be compiled if you have an appropriate C++ compiler installed.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( AddCodeToProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::AddCodeToProject ));

	UI_COMMAND( RefreshCodeProject, "Refresh code project", "Refreshes your C++ code project.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( RefreshCodeProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::RefreshCodeProject ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::IsCodeProject ) );

	UI_COMMAND( OpenIDE, "Open IDE", "Opens your C++ code in an integrated development environment.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( OpenIDE, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::OpenIDE ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::IsCodeProject ), FGetActionCheckState(), FIsActionButtonVisible::CreateStatic( &FMainFrameActionCallbacks::CanOpenIDE ) );

	UI_COMMAND( ZipUpProject, "Zip Up Project", "Zips up the project into a zip file.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction(ZipUpProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ZipUpProject ), DefaultExecuteAction);

	UI_COMMAND( PackagingSettings, "Packaging Settings...", "Opens the settings for project packaging", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( PackagingSettings, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::PackagingSettings ), DefaultExecuteAction );

	//UI_COMMAND( LocalizeProject, "Localize Project...", "Opens the dashboard for managing project localization data.", EUserInterfaceActionType::Button, FInputChord() );
	//ActionList->MapAction( LocalizeProject, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::LocalizeProject ), DefaultExecuteAction );

	const int32 MaxProjects = 20;
	for( int32 CurProjectIndex = 0; CurProjectIndex < MaxProjects; ++CurProjectIndex )
	{
		// NOTE: The actual label and tool-tip will be overridden at runtime when the command is bound to a menu item, however
		// we still need to set one here so that the key bindings UI can function properly
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("CurrentProjectIndex"), CurProjectIndex);
		const FText Message = FText::Format( LOCTEXT( "SwitchProject", "Switch Project {CurrentProjectIndex}" ), Arguments ); 
		TSharedRef< FUICommandInfo > SwitchProject =
			FUICommandInfoDecl(
				this->AsShared(),
				FName( *FString::Printf( TEXT( "SwitchProject%i" ), CurProjectIndex ) ),
				Message,
				LOCTEXT( "SwitchProjectToolTip", "Restarts the editor and switches to selected project" ),
				SwitchProjectBundle)
			.UserInterfaceType( EUserInterfaceActionType::Button )
			.DefaultChord( FInputChord() );
		SwitchProjectCommands.Add( SwitchProject );

		ActionList->MapAction( SwitchProjectCommands[ CurProjectIndex ], FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::SwitchProjectByIndex, CurProjectIndex ),
			FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::CanSwitchToProject, CurProjectIndex ),
			FIsActionChecked::CreateStatic( &FMainFrameActionCallbacks::IsSwitchProjectChecked, CurProjectIndex ) );
	}

	UI_COMMAND( Exit, "Exit", "Exits the application", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( Exit, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::Exit ), DefaultExecuteAction );

	ActionList->MapAction( FGenericCommands::Get().Undo, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ExecuteExecCommand, FString( TEXT("TRANSACTION UNDO") ) ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::Undo_CanExecute ) );

	ActionList->MapAction( FGenericCommands::Get().Redo, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ExecuteExecCommand, FString( TEXT("TRANSACTION REDO") ) ), FCanExecuteAction::CreateStatic( &FMainFrameActionCallbacks::Redo_CanExecute ) );

	UI_COMMAND( OpenDeviceManagerApp, "Device Manager", "Opens up the device manager app", EUserInterfaceActionType::Check, FInputChord() );
	ActionList->MapAction( OpenDeviceManagerApp, 
												FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::OpenSlateApp, FName( TEXT( "DeviceManager" ) ) ),
												FCanExecuteAction(),
												FIsActionChecked::CreateStatic( &FMainFrameActionCallbacks::OpenSlateApp_IsChecked, FName( TEXT( "DeviceManager" ) ) ) );

	UI_COMMAND( OpenSessionManagerApp, "Session Manager", "Opens up the session manager app", EUserInterfaceActionType::Check, FInputChord() );
	ActionList->MapAction( OpenSessionManagerApp, 
												FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::OpenSlateApp, FName( "SessionFrontend" ) ),
												FCanExecuteAction(),
												FIsActionChecked::CreateStatic( &FMainFrameActionCallbacks::OpenSlateApp_IsChecked, FName("SessionFrontend" ) ) );

	UI_COMMAND(VisitOnlineLearning, "Online Learning...", "Learn Unreal Engine for free with easy-to-follow video courses and guided learning paths.", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(VisitOnlineLearning, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::VisitOnlineLearning));

	UI_COMMAND(VisitForums, "Forums...", "Go to the Unreal Engine forums to view announcements and engage in discussions with other developers.", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(VisitForums, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::VisitForums));

	UI_COMMAND(ReportABug, "Report a Bug...", "Found a bug?  Go here to fill out a bug report", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(ReportABug, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::ReportABug));

	UI_COMMAND(OpenIssueTracker, "Issue Tracker", "Go here to view the Unreal Engine bug tracking website", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(OpenIssueTracker, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::OpenIssueTracker));

	UI_COMMAND( VisitAskAQuestionPage, "Ask a Question...", "Have a question?  Go here to ask about anything and everything related to Unreal.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( VisitAskAQuestionPage, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::VisitAskAQuestionPage ) );

	UI_COMMAND( VisitSearchForAnswersPage, "Answer Hub...", "Go to the AnswerHub to ask questions, search existing answers, and share your knowledge with other UE4 developers.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( VisitSearchForAnswersPage, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::VisitSearchForAnswersPage ) );

	UI_COMMAND( VisitSupportWebSite, "Support...", "Navigates to the Unreal Engine Support web site's main page.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( VisitSupportWebSite, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::VisitSupportWebSite ) );

	UI_COMMAND( VisitEpicGamesDotCom, "Visit UnrealEngine.com...", "Navigates to UnrealEngine.com where you can learn more about Unreal Technology.", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( VisitEpicGamesDotCom, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::VisitEpicGamesDotCom ) );

	UI_COMMAND( AboutUnrealEd, "About Editor...", "Displays application credits and copyright information", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( AboutUnrealEd, FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::AboutUnrealEd_Execute ) );

	UI_COMMAND( CreditsUnrealEd, "Credits", "Displays application credits", EUserInterfaceActionType::Button, FInputChord() );
	ActionList->MapAction( CreditsUnrealEd, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::CreditsUnrealEd_Execute) );

	// Layout commands
	UI_COMMAND(ImportLayout, "Import Layout...", "Import a custom layout (or set of layouts) from a different directory and load it into your current instance of the Unreal Editor UI", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(ImportLayout, FExecuteAction::CreateStatic(&FLayoutsMenuLoad::ImportLayout));

	UI_COMMAND(SaveLayoutAs, "Save Layout As...", "Save the current layout customization on disk so it can be loaded later", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(SaveLayoutAs, FExecuteAction::CreateStatic(&FLayoutsMenuSave::SaveLayoutAs));

	UI_COMMAND(ExportLayout, "Export Layout...", "Export the custom layout customization to a different directory", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(ExportLayout, FExecuteAction::CreateStatic(&FLayoutsMenuSave::ExportLayout));

	UI_COMMAND(RemoveUserLayouts, "Remove All User Layouts...", "Remove all the layout customizations created by the user", EUserInterfaceActionType::Button, FInputChord());
	ActionList->MapAction(
		RemoveUserLayouts,
		FExecuteAction::CreateStatic(&FLayoutsMenuRemove::RemoveUserLayouts),
		FCanExecuteAction::CreateStatic(&FLayoutsMenu::IsThereUserLayouts)
	);


#if !PLATFORM_MAC && !PLATFORM_LINUX // Fullscreen mode in the editor is currently unsupported on Mac and Linux
	UI_COMMAND( ToggleFullscreen, "Enable Fullscreen", "Enables fullscreen mode for the application, expanding across the entire monitor", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::F11) );
	ActionList->MapAction( ToggleFullscreen,
		FExecuteAction::CreateStatic( &FMainFrameActionCallbacks::ToggleFullscreen_Execute ),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic( &FMainFrameActionCallbacks::FullScreen_IsChecked )
	);
#endif

	UI_COMMAND(OpenWidgetReflector, "Open Widget Reflector", "Opens the Widget Reflector", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift | EModifierKey::Control , EKeys::W));
	ActionList->MapAction(OpenWidgetReflector, FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::OpenWidgetReflector_Execute));

	FGlobalEditorCommonCommands::MapActions(ActionList);
}

FReply FMainFrameActionCallbacks::OnUnhandledKeyDownEvent(const FKeyEvent& InKeyEvent)
{
	if(!GIsSlowTask)
	{
		if (FMainFrameCommands::ActionList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
		else if (FPlayWorldCommands::GlobalPlayWorldActions.IsValid() && FPlayWorldCommands::GlobalPlayWorldActions->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

bool FMainFrameActionCallbacks::DefaultCanExecuteAction()
{
	return FSlateApplication::Get().IsNormalExecution();
}

void FMainFrameActionCallbacks::ChoosePackagesToSave()
{
	const bool bPromptUserToSave = true;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = true;
	const bool bFastSave = false;
	const bool bClosingEditor = false;
	const bool bNotifyNoPackagesSaved = true;
	const bool bCanBeDeclined = false;
	FEditorFileUtils::SaveDirtyPackages( bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined );
}

void FMainFrameActionCallbacks::ChoosePackagesToCheckIn()
{
	FSourceControlWindows::ChoosePackagesToCheckIn();
}

bool FMainFrameActionCallbacks::CanChoosePackagesToCheckIn()
{
	return FSourceControlWindows::CanChoosePackagesToCheckIn();
}

void FMainFrameActionCallbacks::ConnectToSourceControl()
{
	ELoginWindowMode::Type Mode = !FSlateApplication::Get().GetActiveModalWindow().IsValid() ? ELoginWindowMode::Modeless : ELoginWindowMode::Modal;
	ISourceControlModule::Get().ShowLoginDialog(FSourceControlLoginClosed(), Mode);
}

bool FMainFrameActionCallbacks::CanSaveWorld()
{
	return FSlateApplication::Get().IsNormalExecution() && (!GUnrealEd || !GUnrealEd->GetPackageAutoSaver().IsAutoSaving());
}

void FMainFrameActionCallbacks::SaveAll()
{
	const bool bPromptUserToSave = false;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = true;
	const bool bFastSave = false;
	const bool bNotifyNoPackagesSaved = false;
	const bool bCanBeDeclined = false;
	FEditorFileUtils::SaveDirtyPackages( bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined );
}

TArray<FString> FMainFrameActionCallbacks::ProjectNames;

void FMainFrameActionCallbacks::CacheProjectNames()
{
	ProjectNames.Empty();

	// The switch project menu is filled with recently opened project files
	ProjectNames = GetDefault<UEditorSettings>()->RecentlyOpenedProjectFiles;
}

void FMainFrameActionCallbacks::NewProject( bool bAllowProjectOpening, bool bAllowProjectCreate )
{
	if (GUnrealEd->WarnIfLightingBuildIsCurrentlyRunning())
	{
		return;
	}

	FText Title;
	if (bAllowProjectOpening && bAllowProjectCreate)
	{
		Title = LOCTEXT( "SelectProjectWindowHeader", "Select Project");
	}
	else if (bAllowProjectOpening)
	{
		Title = LOCTEXT( "OpenProjectWindowHeader", "Open Project");
	}
	else
	{
		Title = LOCTEXT( "NewProjectWindowHeader", "New Project");
	}

	TSharedRef<SWindow> NewProjectWindow =
		SNew(SWindow)
		.Title(Title)
		.ClientSize( FMainFrameModule::GetProjectBrowserWindowSize() )
		.SizingRule( ESizingRule::UserSized )
		.SupportsMinimize(false) .SupportsMaximize(false);

	NewProjectWindow->SetContent( FGameProjectGenerationModule::Get().CreateGameProjectDialog(bAllowProjectOpening, bAllowProjectCreate) );

	IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(NewProjectWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(NewProjectWindow);
	}
}

void FMainFrameActionCallbacks::AddCodeToProject()
{
	FGameProjectGenerationModule::Get().OpenAddCodeToProjectDialog();
}

/**
 * Gets compilation flags for UAT for this system.
 */
const TCHAR* GetUATCompilationFlags()
{
	// We never want to compile editor targets when invoking UAT in this context.
	// If we are installed or don't have a compiler, we must assume we have a precompiled UAT.
	return TEXT("-nocompileeditor");
}

FString GetCookingOptionalParams()
{
	FString OptionalParams;
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	
	if (PackagingSettings->bSkipEditorContent)
	{
		OptionalParams += TEXT(" -SkipCookingEditorContent");
	}

	if (FDerivedDataCacheInterface* DDC = GetDerivedDataCache())
	{
		OptionalParams += FString::Printf(TEXT(" -ddc=%s"), DDC->GetGraphName());
	}

	return OptionalParams;
}


void FMainFrameActionCallbacks::CookContent(const FName InPlatformInfoName)
{
	const PlatformInfo::FPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(InPlatformInfoName);
	check(PlatformInfo);

	if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(PlatformInfo->BinaryFolderName))
	{
		if (!FInstalledPlatformInfo::OpenInstallerOptions())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesCook", "Missing required files to cook for this platform."));
		}
		return;
	}

	FString OptionalParams;

	if (!FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(PlatformInfo->VanillaPlatformName))
	{
		return;
	}

	if (PlatformInfo->SDKStatus == PlatformInfo::EPlatformSDKStatus::NotInstalled)
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->TargetPlatformName.ToString(), PlatformInfo->SDKTutorial);
		return;
	}

	// Append any extra UAT flags specified for this platform flavor
	if (!PlatformInfo->UATCommandLine.IsEmpty())
	{
		OptionalParams += TEXT(" ");
		OptionalParams += PlatformInfo->UATCommandLine;
	}
	else
	{
		OptionalParams += TEXT(" -targetplatform=");
		OptionalParams += *PlatformInfo->TargetPlatformName.ToString();
	}

	OptionalParams += GetCookingOptionalParams();

	UCookerSettings const* CookerSettings = GetDefault<UCookerSettings>();
	if (CookerSettings->bIterativeCookingForFileCookContent)
	{
		OptionalParams += TEXT(" -iterate");
	}

	FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
	FString CommandLine = FString::Printf(TEXT("-ScriptsForProject=\"%s\" BuildCookRun %s%s -nop4 -project=\"%s\" -cook -skipstage -ue4exe=\"%s\" %s -utf8output"),
		*ProjectPath,
		GetUATCompilationFlags(),
		FApp::IsEngineInstalled() ? TEXT(" -installed") : TEXT(""),
		*ProjectPath,
		*FUnrealEdMisc::Get().GetExecutableForCommandlets(),
		*OptionalParams
	);
	
	IUATHelperModule::Get().CreateUatTask(CommandLine, PlatformInfo->DisplayName, LOCTEXT("CookingContentTaskName", "Cooking content"), LOCTEXT("CookingTaskName", "Cooking"), FEditorStyle::GetBrush(TEXT("MainFrame.CookContent")));
}

bool FMainFrameActionCallbacks::CookContentCanExecute( const FName PlatformInfoName )
{
	return true;
}

void FMainFrameActionCallbacks::PackageBuildConfiguration( EProjectPackagingBuildConfigurations BuildConfiguration )
{
	UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
	PackagingSettings->BuildConfiguration = BuildConfiguration;
}

bool FMainFrameActionCallbacks::CanPackageBuildConfiguration( EProjectPackagingBuildConfigurations BuildConfiguration )
{
	return true;
}

bool FMainFrameActionCallbacks::PackageBuildConfigurationIsChecked( EProjectPackagingBuildConfigurations BuildConfiguration )
{
	return (GetDefault<UProjectPackagingSettings>()->BuildConfiguration == BuildConfiguration);
}

void FMainFrameActionCallbacks::PackageBuildTarget( FString TargetName )
{
	UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();
	PackagingSettings->BuildTarget = TargetName;
}

bool FMainFrameActionCallbacks::PackageBuildTargetIsChecked( FString TargetName )
{
	const FTargetInfo* Target = GetDefault<UProjectPackagingSettings>()->GetBuildTargetInfo();
	return (Target != nullptr && Target->Name == TargetName);
}

void FMainFrameActionCallbacks::PackageProject( const FName InPlatformInfoName )
{
	GUnrealEd->CancelPlayingViaLauncher();
	SaveAll();

	// does the project have any code?
	FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
	bool bProjectHasCode = GameProjectModule.Get().ProjectHasCodeFiles();
	
	const PlatformInfo::FPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(InPlatformInfoName);
	check(PlatformInfo);

	if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(PlatformInfo->BinaryFolderName))
	{
		if (!FInstalledPlatformInfo::OpenInstallerOptions())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesPackage", "Missing required files to package this platform."));
		}
		return;
	}

	if (UGameMapsSettings::GetGameDefaultMap().IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingGameDefaultMap", "No Game Default Map specified in Project Settings > Maps & Modes."));
		return;
	}

	if (PlatformInfo->SDKStatus == PlatformInfo::EPlatformSDKStatus::NotInstalled || (bProjectHasCode && PlatformInfo->bUsesHostCompiler && !FSourceCodeNavigation::IsCompilerAvailable()))
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->TargetPlatformName.ToString(), PlatformInfo->SDKTutorial);
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), 0.0));
		FEditorAnalytics::ReportEvent(TEXT("Editor.Package.Failed"), PlatformInfo->TargetPlatformName.ToString(), bProjectHasCode, EAnalyticsErrorCodes::SDKNotFound, ParamArray);
		return;
	}

	UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
	const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[PackagingSettings->BuildConfiguration];
	bool bAssetNativizationEnabled = (PackagingSettings->BlueprintNativizationMethod != EProjectPackagingBlueprintNativizationMethod::Disabled);

	const ITargetPlatform* const Platform = GetTargetPlatformManager()->FindTargetPlatform(PlatformInfo->TargetPlatformName.ToString());
	{
		if (Platform)
		{
			FString NotInstalledTutorialLink;
			FString DocumentationLink;
			FText CustomizedLogMessage;

			int32 Result = Platform->CheckRequirements(bProjectHasCode, ConfigurationInfo.Configuration, bAssetNativizationEnabled, NotInstalledTutorialLink, DocumentationLink, CustomizedLogMessage);

			// report to analytics
			FEditorAnalytics::ReportBuildRequirementsFailure(TEXT("Editor.Package.Failed"), PlatformInfo->TargetPlatformName.ToString(), bProjectHasCode, Result);

			// report to main frame
			bool UnrecoverableError = false;

			// report to message log
			if ((Result & ETargetPlatformReadyStatus::SDKNotFound) != 0)
			{
				AddMessageLog(
					LOCTEXT("SdkNotFoundMessage", "Software Development Kit (SDK) not found."),
					CustomizedLogMessage.IsEmpty() ? FText::Format(LOCTEXT("SdkNotFoundMessageDetail", "Please install the SDK for the {0} target platform!"), Platform->DisplayName()) : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);
				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::LicenseNotAccepted) != 0)
			{
				AddMessageLog(
					LOCTEXT("LicenseNotAcceptedMessage", "License not accepted."),
					CustomizedLogMessage.IsEmpty() ? LOCTEXT("LicenseNotAcceptedMessageDetail", "License must be accepted in project settings to deploy your app to the device.") : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);

				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::ProvisionNotFound) != 0)
			{
				AddMessageLog(
					LOCTEXT("ProvisionNotFoundMessage", "Provision not found."),
					CustomizedLogMessage.IsEmpty() ? LOCTEXT("ProvisionNotFoundMessageDetail", "A provision is required for deploying your app to the device.") : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);
				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::SigningKeyNotFound) != 0)
			{
				AddMessageLog(
					LOCTEXT("SigningKeyNotFoundMessage", "Signing key not found."),
					CustomizedLogMessage.IsEmpty() ? LOCTEXT("SigningKeyNotFoundMessageDetail", "The app could not be digitally signed, because the signing key is not configured.") : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);
				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::ManifestNotFound) != 0)
			{
				AddMessageLog(
					LOCTEXT("ManifestNotFound", "Manifest not found."),
					CustomizedLogMessage.IsEmpty() ? LOCTEXT("ManifestNotFoundMessageDetail", "The generated application manifest could not be found.") : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);
				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::RemoveServerNameEmpty) != 0
					&& (bProjectHasCode || (Result & ETargetPlatformReadyStatus::CodeBuildRequired)
						|| (!FApp::GetEngineIsPromotedBuild() && !FApp::IsEngineInstalled())))
			{
				AddMessageLog(
					LOCTEXT("RemoveServerNameNotFound", "Remote compiling requires a server name. "),
					CustomizedLogMessage.IsEmpty() ? LOCTEXT("RemoveServerNameNotFoundDetail", "Please specify one in the Remote Server Name settings field.") : CustomizedLogMessage,
					NotInstalledTutorialLink,
					DocumentationLink
				);
				UnrecoverableError = true;
			}

			if ((Result & ETargetPlatformReadyStatus::CodeUnsupported) != 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_SelectedPlatform", "Sorry, packaging a code-based project for the selected platform is currently not supported. This feature may be available in a future release."));
				UnrecoverableError = true;
			}
			else if ((Result & ETargetPlatformReadyStatus::PluginsUnsupported) != 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_ThirdPartyPlugins", "Sorry, packaging a project with third-party plugins is currently not supported for the selected platform. This feature may be available in a future release."));
				UnrecoverableError = true;
			}

			if (UnrecoverableError)
			{
				return;
			}
		}
	}

	if (!FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(PlatformInfo->VanillaPlatformName))
	{
		return;
	}

	// let the user pick a target directory
	if (PackagingSettings->StagingDirectory.Path.IsEmpty())
	{
		PackagingSettings->StagingDirectory.Path = FPaths::ProjectDir();
	}

	FString OutFolderName;

	void* ParentWindowWindowHandle = nullptr;
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
	if ( MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid() )
	{
		ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
	}
	
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindowWindowHandle, LOCTEXT("PackageDirectoryDialogTitle", "Package project...").ToString(), PackagingSettings->StagingDirectory.Path, OutFolderName))
	{
		return;
	}

	PackagingSettings->StagingDirectory.Path = OutFolderName;
	PackagingSettings->SaveConfig();

	// create the packager process
	FString OptionalParams;
	
	if (PackagingSettings->FullRebuild)
	{
		OptionalParams += TEXT(" -clean");
	}

	if ( PackagingSettings->bCompressed )
	{
		OptionalParams += TEXT(" -compressed");
	}

	OptionalParams += GetCookingOptionalParams();

	if (PackagingSettings->bUseIoStore)
	{
		OptionalParams += TEXT(" -iostore");

		// Pak file(s) must be used when using container file(s)
		PackagingSettings->UsePakFile = true;
	}

	if (PackagingSettings->UsePakFile)
	{
		OptionalParams += TEXT(" -pak");
	}

	if (PackagingSettings->bUseIoStore)
	{
		OptionalParams += TEXT(" -iostore");
	}

	if (PackagingSettings->IncludePrerequisites)
	{
		OptionalParams += TEXT(" -prereqs");
	}

	if (!PackagingSettings->ApplocalPrerequisitesDirectory.Path.IsEmpty())
	{
		OptionalParams += FString::Printf(TEXT(" -applocaldirectory=\"%s\""), *(PackagingSettings->ApplocalPrerequisitesDirectory.Path));
	}
	else if (PackagingSettings->IncludeAppLocalPrerequisites)
	{
		OptionalParams += TEXT(" -applocaldirectory=\"$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies\"");
	}

	if (PackagingSettings->ForDistribution)
	{
		OptionalParams += TEXT(" -distribution");
	}

	if (!PackagingSettings->IncludeDebugFiles)
	{
		OptionalParams += TEXT(" -nodebuginfo");
	}

	if (PackagingSettings->bGenerateChunks)
	{
		OptionalParams += TEXT(" -manifests");
	}

	bool bTargetPlatformCanUseCrashReporter = PlatformInfo->bTargetPlatformCanUseCrashReporter;
	if (bTargetPlatformCanUseCrashReporter && PlatformInfo->TargetPlatformName == FName("WindowsNoEditor") && PlatformInfo->PlatformFlavor == TEXT("Win32"))
	{
		FString MinumumSupportedWindowsOS;
		GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("MinimumOSVersion"), MinumumSupportedWindowsOS, GEngineIni);
		if (MinumumSupportedWindowsOS == TEXT("MSOS_XP"))
		{
			OptionalParams += TEXT(" -SpecifiedArchitecture=_xp");
			bTargetPlatformCanUseCrashReporter = false;
		}
	}

	// Append any extra UAT flags specified for this platform flavor
	if (!PlatformInfo->UATCommandLine.IsEmpty())
	{
		OptionalParams += TEXT(" ");
		OptionalParams += PlatformInfo->UATCommandLine;
	}
	else
	{
		OptionalParams += TEXT(" -targetplatform=");
		OptionalParams += *PlatformInfo->TargetPlatformName.ToString();
	}

	// Get the target to build
	const FTargetInfo* Target = PackagingSettings->GetBuildTargetInfo();

	// Only build if the user elects to do so
	bool bBuild = false;
	if(PackagingSettings->Build == EProjectPackagingBuild::Always)
	{
		bBuild = true;
	}
	else if(PackagingSettings->Build == EProjectPackagingBuild::Never)
	{
		bBuild = false;
	}
	else if(PackagingSettings->Build == EProjectPackagingBuild::IfProjectHasCode)
	{
		bBuild = true;
		if (FApp::GetEngineIsPromotedBuild() && !bAssetNativizationEnabled)
		{
			FString BaseDir;

			// Get the target name
			FString TargetName;
			if (Target == nullptr)
			{
				TargetName = TEXT("UE4Game");
			}
			else
			{
				TargetName = Target->Name;
			}

			// Get the directory containing the receipt for this target, depending on whether the project needs to be built or not
			FString ProjectDir = FPaths::GetPath(FPaths::GetProjectFilePath());
			if (Target != nullptr && FPaths::IsUnderDirectory(Target->Path, ProjectDir))
			{
				UE_LOG(LogMainFrame, Log, TEXT("Selected target: %s"), *Target->Name);
				BaseDir = ProjectDir;
			}
			else
			{
				FText Reason;
				if (Platform->RequiresTempTarget(bProjectHasCode, ConfigurationInfo.Configuration, false, Reason))
				{
					UE_LOG(LogMainFrame, Log, TEXT("Project requires temp target (%s)"), *Reason.ToString());
					BaseDir = ProjectDir;
				}
				else
				{
					UE_LOG(LogMainFrame, Log, TEXT("Project does not require temp target"));
					BaseDir = FPaths::EngineDir();
				}
			}

			// Check if the receipt is for a matching promoted target
			FString PlatformName = Platform->GetPlatformInfo().UBTTargetId.ToString();

			extern LAUNCHERSERVICES_API bool HasPromotedTarget(const TCHAR* BaseDir, const TCHAR* TargetName, const TCHAR* Platform, EBuildConfiguration Configuration, const TCHAR* Architecture);
			if (HasPromotedTarget(*BaseDir, *TargetName, *PlatformName, ConfigurationInfo.Configuration, nullptr))
			{
				bBuild = false;
			}
		}
	}
	else if(PackagingSettings->Build == EProjectPackagingBuild::IfEditorWasBuiltLocally)
	{
		bBuild = !FApp::GetEngineIsPromotedBuild();
	}
	if(bBuild)
	{
		OptionalParams += TEXT(" -build");
	}

	// Whether to include the crash reporter.
	if (PackagingSettings->IncludeCrashReporter && bTargetPlatformCanUseCrashReporter)
	{
		OptionalParams += TEXT( " -CrashReporter" );
	}

	if (PackagingSettings->bBuildHttpChunkInstallData)
	{
		OptionalParams += FString::Printf(TEXT(" -manifests -createchunkinstall -chunkinstalldirectory=\"%s\" -chunkinstallversion=%s"), *(PackagingSettings->HttpChunkInstallDataDirectory.Path), *(PackagingSettings->HttpChunkInstallDataVersion));
	}

	int32 NumCookers = GetDefault<UEditorExperimentalSettings>()->MultiProcessCooking;
	if (NumCookers > 0 )
	{
		OptionalParams += FString::Printf(TEXT(" -NumCookersToSpawn=%d"), NumCookers); 
	}

	if (Target == nullptr)
	{
		OptionalParams += FString::Printf(TEXT(" -clientconfig=%s"), LexToString(ConfigurationInfo.Configuration));
	}
	else if(Target->Type == EBuildTargetType::Server)
	{
		OptionalParams += FString::Printf(TEXT(" -target=%s -serverconfig=%s"), *Target->Name, LexToString(ConfigurationInfo.Configuration));
	}
	else
	{
		OptionalParams += FString::Printf(TEXT(" -target=%s -clientconfig=%s"), *Target->Name, LexToString(ConfigurationInfo.Configuration));
	}

	FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
	FString CommandLine = FString::Printf(TEXT("-ScriptsForProject=\"%s\" BuildCookRun %s%s -nop4 -project=\"%s\" -cook -stage -archive -archivedirectory=\"%s\" -package -ue4exe=\"%s\" %s -utf8output"),
		*ProjectPath,
		GetUATCompilationFlags(),
		FApp::IsEngineInstalled() ? TEXT(" -installed") : TEXT(""),
		*ProjectPath,
		*PackagingSettings->StagingDirectory.Path,
		*FUnrealEdMisc::Get().GetExecutableForCommandlets(),
		*OptionalParams
	);

	IUATHelperModule::Get().CreateUatTask( CommandLine, PlatformInfo->DisplayName, LOCTEXT("PackagingProjectTaskName", "Packaging project"), LOCTEXT("PackagingTaskName", "Packaging"), FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")) );
}

bool FMainFrameActionCallbacks::PackageProjectCanExecute( const FName PlatformInfoName )
{
	return true;
}

void FMainFrameActionCallbacks::RefreshCodeProject()
{
	if ( !FSourceCodeNavigation::IsCompilerAvailable() )
	{
		// Attempt to trigger the tutorial if the user doesn't have a compiler installed for the project.
		FSourceCodeNavigation::AccessOnCompilerNotFound().Broadcast();
	}

	FText FailReason, FailLog;
	if(!FGameProjectGenerationModule::Get().UpdateCodeProject(FailReason, FailLog))
	{
		SOutputLogDialog::Open(LOCTEXT("RefreshProject", "Refresh Project"), FailReason, FailLog, FText::GetEmpty());
	}
}

bool FMainFrameActionCallbacks::IsCodeProject()
{
	// Not particularly rigorous, but assume it's a code project if it can find a Source directory
	const bool bIsCodeProject = IFileManager::Get().DirectoryExists(*FPaths::GameSourceDir());
	return bIsCodeProject;
}

void FMainFrameActionCallbacks::OpenIDE()
{
	if ( !FSourceCodeNavigation::IsCompilerAvailable() )
	{
		// Attempt to trigger the tutorial if the user doesn't have a compiler installed for the project.
		FSourceCodeNavigation::AccessOnCompilerNotFound().Broadcast();
	}
	else
	{
		if ( !FSourceCodeNavigation::OpenModuleSolution() )
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("OpenIDEFailed_UnableToOpenSolution", "Unable to open solution"));
		}
	}
}

bool FMainFrameActionCallbacks::CanOpenIDE()
{
	return FSourceCodeNavigation::DoesModuleSolutionExist();
}

void FMainFrameActionCallbacks::ZipUpProject()
{
#if PLATFORM_WINDOWS
	FText PlatformName = LOCTEXT("PlatformName_Windows", "Windows");
#elif PLATFORM_MAC
	FText PlatformName = LOCTEXT("PlatformName_Mac", "Mac");
#elif PLATFORM_LINUX
	FText PlatformName = LOCTEXT("PlatformName_Linux", "Linux");
#else
	FText PlatformName = LOCTEXT("PlatformName_Other", "Other OS");
#endif

	bool bOpened = false;
	TArray<FString> SaveFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != NULL)
	{
		bOpened = DesktopPlatform->SaveFileDialog(
			NULL,
			NSLOCTEXT("UnrealEd", "ZipUpProject", "Zip file location").ToString(),
			FPaths::ProjectDir(),
			FApp::GetProjectName(),
			TEXT("Zip file|*.zip"),
			EFileDialogFlags::None,
			SaveFilenames);
	}

	if (bOpened)
	{
		for (FString FileName : SaveFilenames)
		{
			// Ensure path is full rather than relative (for macs)
			FString FinalFileName = FPaths::ConvertRelativePathToFull(FileName);
			FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : FPaths::RootDir() / FApp::GetProjectName();

			FString CommandLine = FString::Printf(TEXT("ZipProjectUp %s -project=\"%s\" -install=\"%s\""), GetUATCompilationFlags(), *ProjectPath, *FinalFileName);

			IUATHelperModule::Get().CreateUatTask( CommandLine, PlatformName, LOCTEXT("ZipTaskName", "Zipping Up Project"),
				LOCTEXT("ZipTaskShortName", "Zip Project Task"), FEditorStyle::GetBrush(TEXT("MainFrame.CookContent")), IUATHelperModule::UatTaskResultCallack(), FPaths::GetPath(FinalFileName));
		}
	}
}

void FMainFrameActionCallbacks::PackagingSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Project", "Packaging");
}

//void FMainFrameActionCallbacks::LocalizeProject()
//{
//	FModuleManager::LoadModuleChecked<ILocalizationDashboardModule>("LocalizationDashboard").Show();
//}

void FMainFrameActionCallbacks::SwitchProjectByIndex( int32 ProjectIndex )
{
	FUnrealEdMisc::Get().SwitchProject( ProjectNames[ ProjectIndex ] );
}

void FMainFrameActionCallbacks::SwitchProject(const FString& GameOrProjectFileName)
{
	FUnrealEdMisc::Get().SwitchProject( GameOrProjectFileName );
}

void FMainFrameActionCallbacks::OpenBackupDirectory( FString BackupFile )
{
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*FPaths::GetPath(FPaths::ConvertRelativePathToFull(BackupFile)));
}

void FMainFrameActionCallbacks::ToggleFullscreen_Execute()
{
#if !PLATFORM_MAC && !PLATFORM_LINUX // Fullscreen mode in the editor is currently unsupported on Mac or Linux
	if ( GIsEditor && FApp::HasProjectName() )
	{
		TSharedPtr<SDockTab> LevelEditorTabPtr = FGlobalTabmanager::Get()->TryInvokeTab(FTabId("LevelEditor"));
		const TSharedPtr<SWindow> LevelEditorWindow = FSlateApplication::Get().FindWidgetWindow( LevelEditorTabPtr.ToSharedRef() );

		if (LevelEditorWindow->GetWindowMode() == EWindowMode::Windowed)
		{
			LevelEditorWindow->SetWindowMode(EWindowMode::WindowedFullscreen);
		}
		else
		{
			LevelEditorWindow->SetWindowMode(EWindowMode::Windowed);
		}
	}
#endif
}

bool FMainFrameActionCallbacks::FullScreen_IsChecked()
{
	const TSharedPtr<SDockTab> LevelEditorTabPtr = FModuleManager::Get().GetModuleChecked<FLevelEditorModule>( "LevelEditor" ).GetLevelEditorTab();

	const TSharedPtr<SWindow> LevelEditorWindow = LevelEditorTabPtr.IsValid()
		? LevelEditorTabPtr->GetParentWindow()
		: TSharedPtr<SWindow>();

	return (LevelEditorWindow.IsValid())
		? (LevelEditorWindow->GetWindowMode() != EWindowMode::Windowed)
		: false;
}


bool FMainFrameActionCallbacks::CanSwitchToProject( int32 InProjectIndex )
{
	if (FApp::HasProjectName() && ProjectNames[InProjectIndex].StartsWith(FApp::GetProjectName()))
	{
		return false;
	}

	if ( FPaths::IsProjectFilePathSet() && ProjectNames[ InProjectIndex ] == FPaths::GetProjectFilePath() )
	{
		return false;
	}

	return true;
}

bool FMainFrameActionCallbacks::IsSwitchProjectChecked( int32 InProjectIndex )
{
	return CanSwitchToProject( InProjectIndex ) == false;
}


void FMainFrameActionCallbacks::Exit()
{
	FSlateApplication::Get().LeaveDebuggingMode();
	// Shut down the editor
	// NOTE: We can't close the editor from within this stack frame as it will cause various DLLs
	//       (such as MainFrame) to become unloaded out from underneath the code pointer.  We'll shut down
	//       as soon as it's safe to do so.
	GEngine->DeferredCommands.Add( TEXT("CLOSE_SLATE_MAINFRAME"));
}


bool FMainFrameActionCallbacks::Undo_CanExecute()
{
	return GUnrealEd->Trans->CanUndo() && FSlateApplication::Get().IsNormalExecution();
}

bool FMainFrameActionCallbacks::Redo_CanExecute()
{
	return GUnrealEd->Trans->CanRedo() && FSlateApplication::Get().IsNormalExecution();
}


void FMainFrameActionCallbacks::ExecuteExecCommand( FString Command )
{
	GUnrealEd->Exec( GEditor->GetEditorWorldContext(false).World(), *Command );
}


void FMainFrameActionCallbacks::OpenSlateApp_ViaModule( FName AppName, FName ModuleName )
{
	FModuleManager::Get().LoadModule( ModuleName );
	OpenSlateApp( AppName );
}

void FMainFrameActionCallbacks::OpenSlateApp( FName AppName )
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(AppName));
}

bool FMainFrameActionCallbacks::OpenSlateApp_IsChecked( FName AppName )
{
	return false;
}

void FMainFrameActionCallbacks::ReportABug()
{
	FString ReportABugURL;
	if (FUnrealEdMisc::Get().GetURL(TEXT("ReportABugURL"), ReportABugURL, false))
	{
		FPlatformProcess::LaunchURL(*ReportABugURL, NULL, NULL);
	}
}

void FMainFrameActionCallbacks::OpenIssueTracker()
{
	FString IssueTrackerURL;
	if (FUnrealEdMisc::Get().GetURL(TEXT("IssueTrackerURL"), IssueTrackerURL, false))
	{
		FPlatformProcess::LaunchURL(*IssueTrackerURL, NULL, NULL);
	}
}

void FMainFrameActionCallbacks::VisitAskAQuestionPage()
{
	FString AskAQuestionURL;
	if(FUnrealEdMisc::Get().GetURL( TEXT("AskAQuestionURL"), AskAQuestionURL, true ))
	{
		FPlatformProcess::LaunchURL( *AskAQuestionURL, NULL, NULL );
	}
}


void FMainFrameActionCallbacks::VisitSearchForAnswersPage()
{
	FString SearchForAnswersURL;
	if(FUnrealEdMisc::Get().GetURL( TEXT("SearchForAnswersURL"), SearchForAnswersURL, true ))
	{
		FPlatformProcess::LaunchURL( *SearchForAnswersURL, NULL, NULL );
	}
}


void FMainFrameActionCallbacks::VisitSupportWebSite()
{
	FString SupportWebsiteURL;
	if(FUnrealEdMisc::Get().GetURL( TEXT("SupportWebsiteURL"), SupportWebsiteURL, true ))
	{
		FPlatformProcess::LaunchURL( *SupportWebsiteURL, NULL, NULL );
	}
}


void FMainFrameActionCallbacks::VisitEpicGamesDotCom()
{
	FString EpicGamesURL;
	if(FUnrealEdMisc::Get().GetURL( TEXT("EpicGamesURL"), EpicGamesURL ))
	{
		FPlatformProcess::LaunchURL( *EpicGamesURL, NULL, NULL );
	}
}

void FMainFrameActionCallbacks::VisitOnlineLearning()
{
	FString URL;
	if (FUnrealEdMisc::Get().GetURL(TEXT("OnlineLearningURL"), URL))
	{
		FPlatformProcess::LaunchURL(*URL, NULL, NULL);
	}
}

void FMainFrameActionCallbacks::VisitForums()
{
	FString URL;
	if (FUnrealEdMisc::Get().GetURL(TEXT("ForumsURL"), URL))
	{
		FPlatformProcess::LaunchURL(*URL, NULL, NULL);
	}
}

void FMainFrameActionCallbacks::AboutUnrealEd_Execute()
{
	const FText AboutWindowTitle = LOCTEXT( "AboutUnrealEditor", "About Unreal Editor" );

	TSharedPtr<SWindow> AboutWindow = 
		SNew(SWindow)
		.Title( AboutWindowTitle )
		.ClientSize(FVector2D(600.f, 200.f))
		.SupportsMaximize(false) .SupportsMinimize(false)
		.SizingRule( ESizingRule::FixedSize )
		[
			SNew(SAboutScreen)
		];

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
	TSharedPtr<SWindow> ParentWindow = MainFrame.GetParentWindow();

	if ( ParentWindow.IsValid() )
	{
		FSlateApplication::Get().AddModalWindow(AboutWindow.ToSharedRef(), ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(AboutWindow.ToSharedRef());
	}
}

void FMainFrameActionCallbacks::CreditsUnrealEd_Execute()
{
	const FText CreditsWindowTitle = LOCTEXT("CreditsUnrealEditor", "Credits");

	TSharedPtr<SWindow> CreditsWindow =
		SNew(SWindow)
		.Title(CreditsWindowTitle)
		.ClientSize(FVector2D(600.f, 700.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize)
		[
			SNew(SCreditsScreen)
		];

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
	TSharedPtr<SWindow> ParentWindow = MainFrame.GetParentWindow();

	if ( ParentWindow.IsValid() )
	{
		FSlateApplication::Get().AddModalWindow(CreditsWindow.ToSharedRef(), ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(CreditsWindow.ToSharedRef());
	}
}

void FMainFrameActionCallbacks::OpenWidgetReflector_Execute()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId("WidgetReflector"));
}


/* FMainFrameActionCallbacks implementation
 *****************************************************************************/

void FMainFrameActionCallbacks::AddMessageLog( const FText& Text, const FText& Detail, const FString& TutorialLink, const FString& DocumentationLink )
{
	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error);
	Message->AddToken(FTextToken::Create(Text));
	Message->AddToken(FTextToken::Create(Detail));
	if (!TutorialLink.IsEmpty())
	{
		Message->AddToken(FTutorialToken::Create(TutorialLink));
	}
	if (!DocumentationLink.IsEmpty())
	{
		Message->AddToken(FDocumentationToken::Create(DocumentationLink));
	}
	FMessageLog MessageLog("PackagingResults");
	MessageLog.AddMessage(Message);
	MessageLog.Open();
}

#undef LOCTEXT_NAMESPACE
