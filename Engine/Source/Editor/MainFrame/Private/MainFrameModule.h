// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Frame/MainFrameActions.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Interfaces/IMainFrameModule.h"
#include "UnrealEdMisc.h"
#include "Frame/MainFrameHandler.h"
#include "Misc/CompilationResult.h"

/**
 * Editor main frame module
 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
class FMainFrameModule
	: public IMainFrameModule
{
public:

	// IMainFrameModule interface

	virtual void CreateDefaultMainFrame( const bool bStartImmersive, const bool bStartPIE ) override;
	virtual void RecreateDefaultMainFrame(const bool bStartImmersive, const bool bStartPIE) override;
	virtual TSharedRef<SWidget> MakeMainMenu(const TSharedPtr<FTabManager>& TabManager, const FName MenuName, FToolMenuContext& ToolMenuContext) const override;
	
	// deprecated in 4.26
	virtual TSharedRef<SWidget> MakeMainTabMenu(const TSharedPtr<FTabManager>& TabManager, const FName MenuName, FToolMenuContext& ToolMenuContext) const override;
	virtual TSharedRef<SWidget> MakeDeveloperTools( const TArray<FMainFrameDeveloperTool>& AdditionalTools ) const override;

	virtual bool IsWindowInitialized( ) const override
	{
		return MainFrameHandler->GetParentWindow().IsValid();
	}
	
	virtual TSharedPtr<SWindow> GetParentWindow( ) const override
	{
		return MainFrameHandler->GetParentWindow();
	}

	virtual void SetMainTab(const TSharedRef<SDockTab>& MainTab) override
	{
		MainFrameHandler->SetMainTab(MainTab);
	}

	virtual void EnableTabClosedDelegate( ) override
	{
		MainFrameHandler->EnableTabClosedDelegate();
	}

	virtual void DisableTabClosedDelegate( ) override
	{
		MainFrameHandler->DisableTabClosedDelegate();
	}

	virtual void RequestCloseEditor( ) override
	{
		ClearDelayedShowMainFrameDelegate();

		if ( MainFrameHandler->CanCloseEditor() )
		{
			MainFrameHandler->ShutDownEditor();
		}
		else
		{
			FUnrealEdMisc::Get().ClearPendingProjectName();
		}
	}

	virtual void SetLevelNameForWindowTitle( const FString& InLevelFileName ) override;
	
	virtual FString GetLoadedLevelName( ) const override
	{ 
		return LoadedLevelName; 
	}

	virtual TSharedRef<FUICommandList>& GetMainFrameCommandBindings( ) override
	{
		return FMainFrameCommands::ActionList;
	}

	virtual class FMainMRUFavoritesList* GetMRUFavoritesList() const override
	{
		return MRUFavoritesList;
	}

	virtual const FText GetApplicationTitle( const bool bIncludeGameName ) const override
	{
		return OverriddenWindowTitle.IsEmpty() ? StaticGetApplicationTitle( bIncludeGameName ) : OverriddenWindowTitle;
	}

	virtual void SetApplicationTitleOverride(const FText& NewOverriddenApplicationTitle) override;

	virtual void ShowAboutWindow( ) const override
	{
		FMainFrameActionCallbacks::AboutUnrealEd_Execute();
	}
	
	DECLARE_DERIVED_EVENT(FMainFrameModule, IMainFrameModule::FMainFrameCreationFinishedEvent, FMainFrameCreationFinishedEvent);
	virtual FMainFrameCreationFinishedEvent& OnMainFrameCreationFinished( ) override
	{
		return MainFrameCreationFinishedEvent;
	}

	DECLARE_DERIVED_EVENT(FMainFrameModule, IMainFrameModule::FMainFrameSDKNotInstalled, FMainFrameSDKNotInstalled);
	virtual FMainFrameSDKNotInstalled& OnMainFrameSDKNotInstalled( ) override
	{
		return MainFrameSDKNotInstalled;
	}
	void BroadcastMainFrameSDKNotInstalled(const FString& PlatformName, const FString& DocLink) override
	{
		return MainFrameSDKNotInstalled.Broadcast(PlatformName, DocLink);
	}

	virtual void EnableDelayedShowMainFrame() override
	{
		bDelayedShowMainFrame = true;
	}

	virtual void ShowDelayedMainFrame() override
	{
		bDelayedShowMainFrame = false;

		if (DelayedShowMainFrameDelegate.IsBound())
		{
			DelayedShowMainFrameDelegate.Execute();
			ClearDelayedShowMainFrameDelegate();
		}
	}

public:

	// IModuleInterface interface

	virtual void StartupModule( ) override;
	virtual void ShutdownModule( ) override;

	virtual bool SupportsDynamicReloading( ) override
	{
		return true; // @todo: Eventually, this should probably not be allowed.
	}

protected:

	/**
	 * Checks whether the project dialog should be shown at startup.
	 *
	 * The project dialog should be shown if the Editor was started without a game specified.
	 *
	 * @return true if the project dialog should be shown, false otherwise.
	 */
	bool ShouldShowProjectDialogAtStartup( ) const;

public:

	/** Get the size of the project browser window */
	static FVector2D GetProjectBrowserWindowSize() { return FVector2D(1100, 740); }

private:

	// Handles the level editor module starting to recompile.
	void HandleLevelEditorModuleCompileStarted( bool bIsAsyncCompile );

	// Handles the user requesting the current compilation to be canceled
	void OnCancelCodeCompilationClicked();

	// Handles the level editor module finishing to recompile.
	void HandleLevelEditorModuleCompileFinished( const FString& LogDump, ECompilationResult::Type CompilationResult, bool bShowLog );

	/** Called when Hot Reload completes */
	void HandleHotReloadFinished( bool bWasTriggeredAutomatically );

	// Handles the code accessor having finished launching its editor
	void HandleCodeAccessorLaunched( const bool WasSuccessful );

	// Handle an open file operation failing
	void HandleCodeAccessorOpenFileFailed(const FString& Filename);

	// Handles launching code accessor
	void HandleCodeAccessorLaunching( );

	// Reset delegate
	void ClearDelayedShowMainFrameDelegate()
	{
		DelayedShowMainFrameDelegate.Unbind();
	}

private:

	// Weak pointer to the level editor's compile notification item.
	TWeakPtr<SNotificationItem> CompileNotificationPtr;

	// Friendly name for persistently level name currently loaded.  Used for window and tab titles.
	FString LoadedLevelName;

	// Override window title, or empty to not override
	FText OverriddenWindowTitle;

	/// Event to be called when the mainframe is fully created.
	FMainFrameCreationFinishedEvent MainFrameCreationFinishedEvent;

	/// Event to be called when the editor tried to use a platform, but it wasn't installed
	FMainFrameSDKNotInstalled MainFrameSDKNotInstalled;

	// Commands used by main frame in menus and key bindings.
	TSharedPtr<class FMainFrameCommands> MainFrameActions;

	// Holds the main frame handler.
	TSharedPtr<class FMainFrameHandler> MainFrameHandler;

	// Absolute real time that we started compiling modules. Used for stats tracking.
	double ModuleCompileStartTime;

	// Holds the collection of most recently used favorites.
	class FMainMRUFavoritesList* MRUFavoritesList;

	// Weak pointer to the code accessor's notification item.
	TWeakPtr<class SNotificationItem> CodeAccessorNotificationPtr;

	// Delegate that holds a delayed call to ShowMainFrameWindow
	FSimpleDelegate DelayedShowMainFrameDelegate;

	// Allow delaying when to show main frame's window
	bool bDelayedShowMainFrame;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

