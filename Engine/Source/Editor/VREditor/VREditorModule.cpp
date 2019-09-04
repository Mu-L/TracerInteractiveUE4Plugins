// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VREditorModule.h"
#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "IVREditorModule.h"
#include "VREditorModeManager.h"
#include "VREditorStyle.h"
#include "VREditorMode.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "HeadMountedDisplayTypes.h"
#include "VREditorFloatingUI.h"

class FVREditorModule : public IVREditorModule
{
public:
	FVREditorModule()
	{
	}

	// FModuleInterface overrides
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual void PostLoadCallback() override;
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	// IVREditorModule overrides
	virtual bool IsVREditorEnabled() const override;
	virtual bool IsVREditorAvailable() const override;
	virtual bool IsVREditorButtonActive() const override;
	virtual void EnableVREditor( const bool bEnable, const bool bForceWithoutHMD ) override;
	virtual bool IsVREditorModeActive() override;
	virtual UVREditorMode* GetVRMode() override;
	virtual void UpdateActorPreview(TSharedRef<SWidget> InWidget, int32 Index, AActor *Actor) override;
	virtual void UpdateExternalUMGUI(const FVREditorFloatingUICreationContext& CreationContext) override;
	virtual void UpdateExternalSlateUI(TSharedRef<SWidget> InSlateWidget, FName Name, FVector2D InSize) override;
	virtual TSharedPtr<FExtender> GetRadialMenuExtender() override
	{
		return RadialMenuExtender;
	}


	static void ToggleForceVRMode();

	/** Return a multicast delegate which is executed when VR mode starts. */
	virtual FOnVREditingModeEnter& OnVREditingModeEnter() override { return ModeManager.OnVREditingModeEnter(); }

	/** Return a multicast delegate which is executed when VR mode stops. */
	virtual FOnVREditingModeExit& OnVREditingModeExit() override { return ModeManager.OnVREditingModeExit(); }

private:
	TSharedPtr<class FExtender> RadialMenuExtender;

	/** Handles turning VR Editor mode on and off */
	FVREditorModeManager ModeManager;
};

namespace VREd
{
	static FAutoConsoleCommand ForceVRMode( TEXT( "VREd.ForceVRMode" ), TEXT( "Toggles VREditorMode, even if not in immersive VR" ), FConsoleCommandDelegate::CreateStatic( &FVREditorModule::ToggleForceVRMode ) );
}

void FVREditorModule::StartupModule()
{
	RadialMenuExtender = MakeShareable(new FExtender());
}

void FVREditorModule::ShutdownModule()
{

	if (GIsEditor)
	{
		FVREditorStyle::Shutdown();
	}
}

void FVREditorModule::PostLoadCallback()
{
}

bool FVREditorModule::IsVREditorEnabled() const
{
	return ModeManager.IsVREditorActive();
}
													
bool FVREditorModule::IsVREditorAvailable() const
{
	return ModeManager.IsVREditorAvailable();
}

bool FVREditorModule::IsVREditorButtonActive() const
{
	return ModeManager.IsVREditorButtonActive();
}

void FVREditorModule::EnableVREditor( const bool bEnable, const bool bForceWithoutHMD )
{
	ModeManager.EnableVREditor( bEnable, bForceWithoutHMD );
}

bool FVREditorModule::IsVREditorModeActive()
{
	return ModeManager.IsVREditorActive();
}

UVREditorMode* FVREditorModule::GetVRMode()
{
	return ModeManager.GetCurrentVREditorMode();
}

void FVREditorModule::UpdateActorPreview(TSharedRef<SWidget> InWidget, int32 Index, AActor* Actor)
{
	GetVRMode()->RefreshActorPreviewWidget(InWidget, Index, Actor);
}
  
void FVREditorModule::UpdateExternalUMGUI(const FVREditorFloatingUICreationContext& CreationContext)
{
	GetVRMode()->UpdateExternalUMGUI(CreationContext); 
}

void FVREditorModule::UpdateExternalSlateUI(TSharedRef<SWidget> InSlateWidget, FName Name, FVector2D InSize)
{
	GetVRMode()->UpdateExternalSlateUI(InSlateWidget, Name, InSize);
}

void FVREditorModule::ToggleForceVRMode()
{
	const bool bForceWithoutHMD = true;
	FVREditorModule& Self = FModuleManager::GetModuleChecked< FVREditorModule >( TEXT( "VREditor" ) );
	Self.EnableVREditor( !Self.IsVREditorEnabled(), bForceWithoutHMD );
}

IMPLEMENT_MODULE( FVREditorModule, VREditor )
