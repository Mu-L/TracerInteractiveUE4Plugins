// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Classes/EditorStyleSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
	#include "UObject/UnrealType.h"
#endif

/* UEditorStyleSettings interface
 *****************************************************************************/

UEditorStyleSettings::UEditorStyleSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
	bEnableUserEditorLayoutManagement = true;

	SelectionColor = FLinearColor(0.828f, 0.364f, 0.003f);
	InactiveSelectionColor = FLinearColor(0.25f, 0.25f, 0.25f);
	PressedSelectionColor = FLinearColor(0.701f, 0.225f, 0.003f);

	EditorWindowBackgroundColor = FLinearColor::White;

	AssetEditorOpenLocation = EAssetEditorOpenLocation::Default;
	bEnableColorizedEditorTabs = true;
	
	bUseGrid = true;

	RegularColor = FLinearColor(0.035f, 0.035f, 0.035f);
	RuleColor = FLinearColor(0.008f, 0.008f, 0.008f);
	CenterColor = FLinearColor::Black;

	GridSnapSize = 16.f;

	bShowFriendlyNames = true;
	bShowNativeComponentNames = true;
	LogTimestampMode = ELogTimes::None;
	
	bEnableLegacyEditorModeUI = false;
}

void UEditorStyleSettings::Init()
{

	// Set from CVar 
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("EnableHighDPIAwareness"));
	bEnableHighDPIAwareness = CVar->GetInt() != 0;
}

FLinearColor UEditorStyleSettings::GetSubduedSelectionColor() const
{
	FLinearColor SubduedSelectionColor = SelectionColor.LinearRGBToHSV();
	SubduedSelectionColor.G *= 0.55f;		// take the saturation 
	SubduedSelectionColor.B *= 0.8f;		// and brightness down

	return SubduedSelectionColor.HSVToLinearRGB();
}

#if WITH_EDITOR

void UEditorStyleSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bResetEditorWindowBackgroundSettings)
	{
		// Reset the settings
		bResetEditorWindowBackgroundSettings = false;

		EditorWindowBackgroundColor = FLinearColor::White;

		FSlateBrush DummyBrush;
		EditorMainWindowBackgroundOverride = DummyBrush;
		EditorChildWindowBackgroundOverride = DummyBrush;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEditorStyleSettings, bEnableWindowAnimations))
	{
		FSlateApplication::Get().EnableMenuAnimations(bEnableWindowAnimations);
	}

	// This property is intentionally not per project so it must be manually written to the correct config file
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEditorStyleSettings, bEnableHighDPIAwareness))
	{
		GConfig->SetBool(TEXT("HDPI"), TEXT("EnableHighDPIAwareness"), bEnableHighDPIAwareness, GEditorSettingsIni);
	}

//	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	SettingChangedEvent.Broadcast(PropertyName);
}

#endif
