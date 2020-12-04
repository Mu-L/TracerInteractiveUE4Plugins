// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "InputCoreTypes.h"
#include "Classes/EditorStyleSettings.h"
#include "AI/NavigationSystemBase.h"
#include "Model.h"
#include "ISourceControlModule.h"
#include "Settings/ContentBrowserSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Settings/EditorProjectSettings.h"
#include "Settings/ClassViewerSettings.h"
#include "Settings/StructViewerSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Settings/EditorMiscSettings.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "Settings/ProjectPackagingSettings.h"
#include "EngineGlobals.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "UnrealEdMisc.h"
#include "CrashReporterSettings.h"
#include "AutoReimport/AutoReimportUtilities.h"
#include "Misc/ConfigCacheIni.h" // for FConfigCacheIni::GetString()
#include "SourceCodeNavigation.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Settings/SkeletalMeshEditorSettings.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DesktopPlatformModule.h"
#include "InstalledPlatformInfo.h"
#include "DrawDebugHelpers.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SettingsClasses"

/* UContentBrowserSettings interface
 *****************************************************************************/

UContentBrowserSettings::FSettingChangedEvent UContentBrowserSettings::SettingChangedEvent;

UContentBrowserSettings::UContentBrowserSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
	, bShowFullCollectionNameInToolTip(true)
{
}


void UContentBrowserSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	SettingChangedEvent.Broadcast(Name);
}

/* UClassViewerSettings interface
*****************************************************************************/

UClassViewerSettings::FSettingChangedEvent UClassViewerSettings::SettingChangedEvent;

UClassViewerSettings::UClassViewerSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UClassViewerSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	SettingChangedEvent.Broadcast(Name);
}

/* UStructViewerSettings interface
*****************************************************************************/

UStructViewerSettings::FSettingChangedEvent UStructViewerSettings::SettingChangedEvent;

void UStructViewerSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	SettingChangedEvent.Broadcast(Name);
}

/* USkeletalMeshEditorSettings interface
*****************************************************************************/

USkeletalMeshEditorSettings::USkeletalMeshEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AnimPreviewLightingDirection = FRotator(-45.0f, 45.0f, 0);
	AnimPreviewSkyColor = FColor::Blue;
	AnimPreviewFloorColor = FColor(51, 51, 51);
	AnimPreviewSkyBrightness = 0.2f * PI;
	AnimPreviewDirectionalColor = FColor::White;
	AnimPreviewLightBrightness = 1.0f * PI;
}

/* UEditorExperimentalSettings interface
 *****************************************************************************/

static TAutoConsoleVariable<int32> CVarEditorHDRSupport(
	TEXT("Editor.HDRSupport"),
	0,
	TEXT("Sets whether or not we should allow the editor to run on HDR monitors"),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarEditorHDRNITLevel(
	TEXT("Editor.HDRNITLevel"),
	160.0f,
	TEXT("Sets The desired NIT level of the editor when running on HDR"),
	ECVF_Default);

UEditorExperimentalSettings::UEditorExperimentalSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
	, bHDREditor(false)
	, HDREditorNITLevel(160.0f)
	, bEnableLocalizationDashboard(true)
	, bUseOpenCLForConvexHullDecomp(false)
	, bAllowPotentiallyUnsafePropertyEditing(false)
{
}

bool UEditorExperimentalSettings::IsClassAllowedToRecompileDuringPIE(UClass* TestClass) const
{
	if (TestClass != nullptr)
	{
		// Rebuild the list if necessary (if the list was edited, either number of entires or value, ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor will get reset below)
		if (ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor.Num() != BaseClassesToAllowRecompilingDuringPlayInEditor.Num())
		{
			ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor.Reset();
			for (TSoftClassPtr<UObject> BaseClassPtr : BaseClassesToAllowRecompilingDuringPlayInEditor)
			{
				if (UClass* BaseClass = BaseClassPtr.Get())
				{
					ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor.Add(BaseClass);
				}
			}
		}

		// See if the test class matches any of the enabled base classes
		for (UClass* BaseClass : ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor)
		{
			if ((BaseClass != nullptr) && TestClass->IsChildOf(BaseClass))
			{
				return true;
			}
		}
	}
	return false;
}

void UEditorExperimentalSettings::PostInitProperties()
{
	CVarEditorHDRSupport->Set(bHDREditor ? 1 : 0, ECVF_SetByProjectSetting);
	CVarEditorHDRNITLevel->Set(HDREditorNITLevel, ECVF_SetByProjectSetting);
	Super::PostInitProperties();
}

void UEditorExperimentalSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (Name == FName(TEXT("ConsoleForGamepadLabels")))
	{
		EKeys::SetConsoleForGamepadLabels(ConsoleForGamepadLabels);
	}
	else if (Name == FName(TEXT("bHDREditor")))
	{
		CVarEditorHDRSupport->Set(bHDREditor ? 1 : 0, ECVF_SetByProjectSetting);
	}
	else if (Name == FName(TEXT("HDREditorNITLevel")))
	{
		CVarEditorHDRNITLevel->Set(HDREditorNITLevel, ECVF_SetByProjectSetting);
	}
	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	ResolvedBaseClassesToAllowRecompilingDuringPlayInEditor.Reset();

	SettingChangedEvent.Broadcast(Name);
}


/* UEditorLoadingSavingSettings interface
 *****************************************************************************/

UEditorLoadingSavingSettings::UEditorLoadingSavingSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
	, bMonitorContentDirectories(true)
	, AutoReimportThreshold(3.f)
	, bAutoCreateAssets(true)
	, bAutoDeleteAssets(true)
	, bDetectChangesOnStartup(true)
	, bDeleteSourceFilesWithAssets(false)
{
	TextDiffToolPath.FilePath = TEXT("P4Merge.exe");

	FAutoReimportDirectoryConfig Default;
	Default.SourceDirectory = TEXT("/Game/");
	AutoReimportDirectorySettings.Add(Default);

	bPromptBeforeAutoImporting = true;
}

// @todo thomass: proper settings support for source control module
void UEditorLoadingSavingSettings::SccHackInitialize()
{
	bSCCUseGlobalSettings = ISourceControlModule::Get().GetUseGlobalSettings();
}

void UEditorLoadingSavingSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Use MemberProperty here so we report the correct member name for nested changes
	const FName Name = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (Name == FName(TEXT("bSCCUseGlobalSettings")))
	{
		// unfortunately we cant use UserSettingChangedEvent here as the source control module cannot depend on the editor
		ISourceControlModule::Get().SetUseGlobalSettings(bSCCUseGlobalSettings);
	}

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	SettingChangedEvent.Broadcast(Name);
}

void UEditorLoadingSavingSettings::PostInitProperties()
{
	if (AutoReimportDirectories_DEPRECATED.Num() != 0)
	{
		AutoReimportDirectorySettings.Empty();
		for (const auto& String : AutoReimportDirectories_DEPRECATED)
		{
			FAutoReimportDirectoryConfig Config;
			Config.SourceDirectory = String;
			AutoReimportDirectorySettings.Add(Config);
		}
		AutoReimportDirectories_DEPRECATED.Empty();
	}
	Super::PostInitProperties();
}

FAutoReimportDirectoryConfig::FParseContext::FParseContext(bool bInEnableLogging)
	: bEnableLogging(bInEnableLogging)
{
	TArray<FString> RootContentPaths;
	FPackageName::QueryRootContentPaths( RootContentPaths );
	for (FString& RootPath : RootContentPaths)
	{
		FString ContentFolder = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(RootPath));
		MountedPaths.Emplace( MoveTemp(ContentFolder), MoveTemp(RootPath) );
	}
}

bool FAutoReimportDirectoryConfig::ParseSourceDirectoryAndMountPoint(FString& SourceDirectory, FString& MountPoint, const FParseContext& InContext)
{
	SourceDirectory.ReplaceInline(TEXT("\\"), TEXT("/"));
	MountPoint.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Check if starts with relative path.
	if (SourceDirectory.StartsWith("../"))
	{
		// Normalize. Interpret setting as a relative path from the Game User directory (Named after the Game)
		SourceDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectUserDir() / SourceDirectory);
	}

	// Check if the source directory is actually a mount point
	const FName SourceDirectoryMountPointName = FPackageName::GetPackageMountPoint(SourceDirectory);
	if (!SourceDirectoryMountPointName.IsNone())
	{
		FString SourceDirectoryMountPoint = SourceDirectoryMountPointName.ToString();
		if (SourceDirectoryMountPoint.Len() + 2 == SourceDirectory.Len())
		{
			// Mount point name + 2 for the directory slashes is the equal, this is exactly a mount point
			MountPoint = SourceDirectory;
			SourceDirectory = FPackageName::LongPackageNameToFilename(MountPoint);
		}
		else
		{
			// Starts off with a mount point (not case sensitive)
			FString SourceMountPoint = TEXT("/") + SourceDirectoryMountPoint + TEXT("/");
			if (MountPoint.IsEmpty() || FPackageName::GetPackageMountPoint(MountPoint).IsNone())
			{
				//Set the mountPoint
				MountPoint = SourceMountPoint;
			}
			FString SourceDirectoryLeftChop = SourceDirectory.Left(SourceMountPoint.Len());
			FString SourceDirectoryRightChop = SourceDirectory.RightChop(SourceMountPoint.Len());
			// Resolve mount point on file system (possibly case sensitive, so re-use original source path)
			SourceDirectory = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(SourceDirectoryLeftChop) / SourceDirectoryRightChop);
		}
	}

	if (!SourceDirectory.IsEmpty() && !MountPoint.IsEmpty())
	{
		// We have both a source directory and a mount point. Verify that the source dir exists, and that the mount point is valid.
		if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
		{
			UE_CLOG(InContext.bEnableLogging, LogAutoReimportManager, Warning, TEXT("Unable to watch directory %s as it doesn't exist."), *SourceDirectory);
			return false;
		}

		if (FPackageName::GetPackageMountPoint(MountPoint).IsNone())
		{
			UE_CLOG(InContext.bEnableLogging, LogAutoReimportManager, Warning, TEXT("Unable to setup directory %s to map to %s, as it's not a valid mounted path. Continuing without mounted path (auto reimports will still work, but auto add won't)."), *SourceDirectory, *MountPoint);
			MountPoint = FString();
			return false; // Return false when unable to determine mount point.
		}
	}
	else if(!MountPoint.IsEmpty())
	{
		// We have just a mount point - validate it, and find its source directory
		if (FPackageName::GetPackageMountPoint(MountPoint).IsNone())
		{
			UE_CLOG(InContext.bEnableLogging, LogAutoReimportManager, Warning, TEXT("Unable to setup directory monitor for %s, as it's not a valid mounted path."), *MountPoint);
			return false;
		}

		SourceDirectory = FPackageName::LongPackageNameToFilename(MountPoint);
	}
	else if(!SourceDirectory.IsEmpty())
	{
		// We have just a source directory - verify whether it's a mounted path, and set up the mount point if so
		if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
		{
			UE_CLOG(InContext.bEnableLogging, LogAutoReimportManager, Warning, TEXT("Unable to watch directory %s as it doesn't exist."), *SourceDirectory);
			return false;
		}

		// Set the mounted path if necessary
		auto* Pair = InContext.MountedPaths.FindByPredicate([&](const TPair<FString, FString>& InPair){
			return SourceDirectory.StartsWith(InPair.Key);
		});
		if (Pair)
		{
			// Resolve source directory by replacing mount point with actual path
			MountPoint = Pair->Value / SourceDirectory.RightChop(Pair->Key.Len());
			MountPoint.ReplaceInline(TEXT("\\"), TEXT("/"));
		}
		else
		{
			UE_CLOG(InContext.bEnableLogging, LogAutoReimportManager, Warning, TEXT("Unable to watch directory %s as not associated with mounted path."), *SourceDirectory);
			return false;
		}
	}
	else
	{
		// Don't have any valid settings
		return false;
	}

	return true;
}

/* UEditorMiscSettings interface
 *****************************************************************************/

UEditorMiscSettings::UEditorMiscSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
}


/* ULevelEditorMiscSettings interface
 *****************************************************************************/

ULevelEditorMiscSettings::ULevelEditorMiscSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
	bAutoApplyLightingEnable = true;
	SectionName = TEXT("Misc");
	CategoryName = TEXT("LevelEditor");
	EditorScreenshotSaveDirectory.Path = FPaths::ScreenShotDir();
	bPromptWhenAddingToLevelBeforeCheckout = true;
	bPromptWhenAddingToLevelOutsideBounds = true;
	PercentageThresholdForPrompt = 20.0f;
	MinimumBoundsForCheckingSize = FVector(500.0f, 500.0f, 50.0f);
	bCreateNewAudioDeviceForPlayInEditor = true;
	bEnableLegacyMeshPaintMode = false;
	bAvoidRelabelOnPasteSelected = false;
}

void ULevelEditorMiscSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	if (Name == FName(TEXT("bNavigationAutoUpdate")))
	{
		FWorldContext &EditorContext = GEditor->GetEditorWorldContext();
		FNavigationSystem::SetNavigationAutoUpdateEnabled(bNavigationAutoUpdate, EditorContext.World()->GetNavigationSystem());
	}

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}
}


/* ULevelEditorPlaySettings interface
 *****************************************************************************/

ULevelEditorPlaySettings::ULevelEditorPlaySettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
	ClientWindowWidth = 640;
	ClientWindowHeight = 480;
	PlayNetMode = EPlayNetMode::PIE_Standalone;
	bLaunchSeparateServer = false;
	PlayNumberOfClients = 1;
	ServerPort = 17777;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PlayNetDedicated = false;
	AutoConnectToServer = true;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	RunUnderOneProcess = true;
	RouteGamepadToSecondWindow = false;
	BuildGameBeforeLaunch = EPlayOnBuildMode::PlayOnBuild_Default;
	LaunchConfiguration = EPlayOnLaunchConfiguration::LaunchConfig_Default;
	bAutoCompileBlueprintsOnLaunch = true;
	CenterNewWindow = false;
	NewWindowPosition = FIntPoint::NoneValue; // It will center PIE to the middle of the screen the first time it is run (until the user drag the window somewhere else)

	EnablePIEEnterAndExitSounds = false;

	bShowServerDebugDrawingByDefault = true;
	ServerDebugDrawingColorTintStrength = 0.0f;
	ServerDebugDrawingColorTint = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void ULevelEditorPlaySettings::PushDebugDrawingSettings()
{
#if ENABLE_DRAW_DEBUG
	extern ENGINE_API float GServerDrawDebugColorTintStrength;
	extern ENGINE_API FLinearColor GServerDrawDebugColorTint;

	GServerDrawDebugColorTintStrength = ServerDebugDrawingColorTintStrength;
	GServerDrawDebugColorTint = ServerDebugDrawingColorTint;
#endif
}

void FPlayScreenResolution::PostInitProperties()
{
	ScaleFactor = 1.0f;
	LogicalHeight = Height;
	LogicalWidth = Width;

	UDeviceProfile* DeviceProfile = UDeviceProfileManager::Get().FindProfile(ProfileName, false);
	if (DeviceProfile)
	{
		GetMutableDefault<ULevelEditorPlaySettings>()->RescaleForMobilePreview(DeviceProfile, LogicalWidth, LogicalHeight, ScaleFactor);
	}
}

void ULevelEditorPlaySettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (BuildGameBeforeLaunch != EPlayOnBuildMode::PlayOnBuild_Always && !FSourceCodeNavigation::IsCompilerAvailable())
	{
		BuildGameBeforeLaunch = EPlayOnBuildMode::PlayOnBuild_Never;
	}

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, bOnlyLoadVisibleLevelsInPIE))
	{
		for (TObjectIterator<UWorld> WorldIt; WorldIt; ++WorldIt)
		{
			WorldIt->PopulateStreamingLevelsToConsider();
		}
	}

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, NetworkEmulationSettings))
	{
		NetworkEmulationSettings.OnPostEditChange(PropertyChangedEvent);
	}

	PushDebugDrawingSettings();
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, bShowServerDebugDrawingByDefault))
	{
		// If the show option is turned on or off, force it on or off in any active PIE instances too as a QOL aid so they don't have to stop and restart PIE again for it to take effect
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if ((WorldContext.WorldType == EWorldType::PIE) &&
				(WorldContext.World() != nullptr) &&
				(WorldContext.World()->GetNetMode() == NM_Client) &&
				(WorldContext.GameViewport != nullptr))
			{
				WorldContext.GameViewport->EngineShowFlags.SetServerDrawDebug(bShowServerDebugDrawingByDefault);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULevelEditorPlaySettings::PostInitProperties()
{
	Super::PostInitProperties();

	NewWindowWidth = FMath::Max(0, NewWindowWidth);
	NewWindowHeight = FMath::Max(0, NewWindowHeight);

	NetworkEmulationSettings.OnPostInitProperties();

#if WITH_EDITOR
	FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(this, &ULevelEditorPlaySettings::UpdateCustomSafeZones);
#endif

	for (FPlayScreenResolution& Resolution : LaptopScreenResolutions)
	{
		Resolution.PostInitProperties();
	}
	for (FPlayScreenResolution& Resolution : MonitorScreenResolutions)
	{
		Resolution.PostInitProperties();
	}
	for (FPlayScreenResolution& Resolution : PhoneScreenResolutions)
	{
		Resolution.PostInitProperties();
	}
	for (FPlayScreenResolution& Resolution : TabletScreenResolutions)
	{
		Resolution.PostInitProperties();
	}
	for (FPlayScreenResolution& Resolution : TelevisionScreenResolutions)
	{
		Resolution.PostInitProperties();
	}

	PushDebugDrawingSettings();
}

bool ULevelEditorPlaySettings::CanEditChange(const FProperty* InProperty) const
{
	const bool ParentVal = Super::CanEditChange(InProperty);
	FName PropertyName = InProperty->GetFName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, AdditionalServerLaunchParameters))
	{
		return ParentVal && (!RunUnderOneProcess && (PlayNetMode == EPlayNetMode::PIE_Client || bLaunchSeparateServer));
	}

	return ParentVal;
}

#if WITH_EDITOR
void ULevelEditorPlaySettings::UpdateCustomSafeZones()
{
	// Prefer to use r.DebugSafeZone.TitleRatio if it is set
	if (FDisplayMetrics::GetDebugTitleSafeZoneRatio() < 1.f)
	{
		FSlateApplication::Get().ResetCustomSafeZone();
		PIESafeZoneOverride = FMargin();
	}
	else
	{
		PIESafeZoneOverride = CalculateCustomUnsafeZones(CustomUnsafeZoneStarts, CustomUnsafeZoneDimensions, DeviceToEmulate, FVector2D(NewWindowWidth, NewWindowHeight));
	}

	FMargin SafeZoneRatio = PIESafeZoneOverride;
	SafeZoneRatio.Left /= (NewWindowWidth / 2.0f);
	SafeZoneRatio.Right /= (NewWindowWidth / 2.0f);
	SafeZoneRatio.Bottom /= (NewWindowHeight / 2.0f);
	SafeZoneRatio.Top /= (NewWindowHeight / 2.0f);
	FSlateApplication::Get().OnDebugSafeZoneChanged.Broadcast(SafeZoneRatio, true);
}
#endif

FMargin ULevelEditorPlaySettings::CalculateCustomUnsafeZones(TArray<FVector2D>& CustomSafeZoneStarts, TArray<FVector2D>& CustomSafeZoneDimensions, FString& DeviceType, FVector2D PreviewSize)
{
	int32 PreviewHeight = PreviewSize.Y;
	int32 PreviewWidth = PreviewSize.X;
	bool bPreviewIsPortrait = PreviewHeight > PreviewWidth;
	FMargin CustomSafeZoneOverride = FMargin();
	CustomSafeZoneStarts.Empty();
	CustomSafeZoneDimensions.Empty();
	UDeviceProfile* DeviceProfile = UDeviceProfileManager::Get().FindProfile(DeviceType, false);
	if (DeviceProfile)
	{
		FString CVarUnsafeZonesString;
		if (DeviceProfile->GetConsolidatedCVarValue(TEXT("r.CustomUnsafeZones"), CVarUnsafeZonesString))
		{
			TArray<FString> UnsafeZones;
			CVarUnsafeZonesString.ParseIntoArray(UnsafeZones, TEXT(";"), true);
			for (FString UnsafeZone : UnsafeZones)
			{
				FString Orientation;
				FString FixedState;
				FString TempString;
				FVector2D Start;
				FVector2D Dimensions;
				bool bAdjustsToDeviceRotation = false;
				UnsafeZone.Split(TEXT("("), &TempString, &UnsafeZone);
				Orientation = UnsafeZone.Left(1);
				UnsafeZone.Split(TEXT("["), &TempString, &UnsafeZone);
				if (TempString.Contains(TEXT("free")))
				{
					bAdjustsToDeviceRotation = true;
				}

				UnsafeZone.Split(TEXT(","), &TempString, &UnsafeZone);
				Start.X = FCString::Atof(*TempString);
				UnsafeZone.Split(TEXT("]"), &TempString, &UnsafeZone);
				Start.Y = FCString::Atof(*TempString);
				UnsafeZone.Split(TEXT("["), &TempString, &UnsafeZone);
				UnsafeZone.Split(TEXT(","), &TempString, &UnsafeZone);
				Dimensions.X = FCString::Atof(*TempString);
				Dimensions.Y = FCString::Atof(*UnsafeZone);

				bool bShouldScale = false;
				float CVarMobileContentScaleFactor = FCString::Atof(*DeviceProfile->GetCVarValue(TEXT("r.MobileContentScaleFactor")));
				if (CVarMobileContentScaleFactor != 0)
				{
					bShouldScale = true;
				}
				else
				{
					if (DeviceProfile->GetConsolidatedCVarValue(TEXT("r.MobileContentScaleFactor"), CVarMobileContentScaleFactor, true))
					{
						bShouldScale = true;
					}
				}
				if (bShouldScale)
				{
					Start *= CVarMobileContentScaleFactor;
					Dimensions *= CVarMobileContentScaleFactor;
				}

				if (!bAdjustsToDeviceRotation && ((Orientation.Contains(TEXT("L")) && bPreviewIsPortrait) ||
					(Orientation.Contains(TEXT("P")) && !bPreviewIsPortrait)))
				{
					float Placeholder = Start.X;
					Start.X = Start.Y;
					Start.Y = Placeholder;

					Placeholder = Dimensions.X;
					Dimensions.X = Dimensions.Y;
					Dimensions.Y = Placeholder;
				}

				if (Start.X < 0)
				{
					Start.X += PreviewWidth;
				}
				if (Start.Y < 0)
				{
					Start.Y += PreviewHeight;
				}

				// Remove any overdraw if this is an unsafe zone that could adjust with device rotation
				if (bAdjustsToDeviceRotation)
				{
					if (Dimensions.X + Start.X > PreviewWidth)
					{
						Dimensions.X = PreviewWidth - Start.X;
					}
					if (Dimensions.Y + Start.Y > PreviewHeight)
					{
						Dimensions.Y = PreviewHeight - Start.Y;
					}
				}

				CustomSafeZoneStarts.Add(Start);
				CustomSafeZoneDimensions.Add(Dimensions);

				if (Start.X + Dimensions.X == PreviewWidth && !FMath::IsNearlyZero(Start.X))
				{
					CustomSafeZoneOverride.Right = FMath::Max(CustomSafeZoneOverride.Right, Dimensions.X);
				}
				else if (Start.X == 0.0f && Start.X + Dimensions.X != PreviewWidth)
				{
					CustomSafeZoneOverride.Left = FMath::Max(CustomSafeZoneOverride.Left, Dimensions.X);
				}
				if (Start.Y + Dimensions.Y == PreviewHeight && !FMath::IsNearlyZero(Start.Y))
				{
					CustomSafeZoneOverride.Bottom = FMath::Max(CustomSafeZoneOverride.Bottom, Dimensions.Y);
				}
				else if (Start.Y == 0.0f && Start.Y + Dimensions.Y != PreviewHeight)
				{
					CustomSafeZoneOverride.Top = FMath::Max(CustomSafeZoneOverride.Top, Dimensions.Y);
				}
			}
		}
	}
	return CustomSafeZoneOverride;
}

FMargin ULevelEditorPlaySettings::FlipCustomUnsafeZones(TArray<FVector2D>& CustomSafeZoneStarts, TArray<FVector2D>& CustomSafeZoneDimensions, FString& DeviceType, FVector2D PreviewSize)
{
	FMargin CustomSafeZoneOverride = CalculateCustomUnsafeZones(CustomSafeZoneStarts, CustomSafeZoneDimensions, DeviceType, PreviewSize);
	for (FVector2D& CustomSafeZoneStart : CustomSafeZoneStarts)
	{
		CustomSafeZoneStart.X = PreviewSize.X - CustomSafeZoneStart.X;
	}
	for (FVector2D& CustomSafeZoneDimension : CustomSafeZoneDimensions)
	{
		CustomSafeZoneDimension.X *= -1.0f;
	}
	float Placeholder = CustomSafeZoneOverride.Left;
	CustomSafeZoneOverride.Left = CustomSafeZoneOverride.Right;
	CustomSafeZoneOverride.Right = Placeholder;
	return CustomSafeZoneOverride;
}

void ULevelEditorPlaySettings::RescaleForMobilePreview(const UDeviceProfile* DeviceProfile, int32 &PreviewWidth, int32 &PreviewHeight, float &ScaleFactor)
{
	bool bShouldScale = false;
	float CVarMobileContentScaleFactor = 0.0f;
	const FString ScaleFactorString = DeviceProfile->GetCVarValue(TEXT("r.MobileContentScaleFactor"));
	if (ScaleFactorString != FString())
	{
		CVarMobileContentScaleFactor = FCString::Atof(*ScaleFactorString);

		if (!FMath::IsNearlyEqual(CVarMobileContentScaleFactor, 0.0f))
		{
			bShouldScale = true;
			ScaleFactor = CVarMobileContentScaleFactor;
		}
	}
	else
	{
		TMap<FString, FString> ParentValues;
		DeviceProfile->GatherParentCVarInformationRecursively(ParentValues);
		const FString* ParentScaleFactorPtr = ParentValues.Find(TEXT("r.MobileContentScaleFactor"));
		if (ParentScaleFactorPtr != nullptr)
		{
			FString CompleteString = *ParentScaleFactorPtr;
			FString DiscardString;
			FString ValueString;
			CompleteString.Split(TEXT("="), &DiscardString, &ValueString);
			CVarMobileContentScaleFactor = FCString::Atof(*ValueString);
			if (!FMath::IsNearlyEqual(CVarMobileContentScaleFactor, 0.0f))
			{
				bShouldScale = true;
				ScaleFactor = CVarMobileContentScaleFactor;
			}
		}
	}
	if (bShouldScale)
	{
		if (DeviceProfile->DeviceType == TEXT("Android"))
		{
			const float OriginalPreviewWidth = PreviewWidth;
			const float OriginalPreviewHeight = PreviewHeight;
			float TempPreviewHeight = 0.0f;
			float TempPreviewWidth = 0.0f;
			// Portrait
			if (PreviewHeight > PreviewWidth)
			{
				TempPreviewHeight = 1280 * ScaleFactor;
			
			}
			// Landscape
			else
			{
				TempPreviewHeight = 720 * ScaleFactor;
			}
			TempPreviewWidth = TempPreviewHeight * OriginalPreviewWidth / OriginalPreviewHeight + 0.5f;
			PreviewHeight = (int32)FMath::GridSnap(TempPreviewHeight, 8.0f);
			PreviewWidth = (int32)FMath::GridSnap(TempPreviewWidth, 8.0f);
		}
		else
		{
			PreviewWidth *= ScaleFactor;
			PreviewHeight *= ScaleFactor;
		}

	}
}

void ULevelEditorPlaySettings::RegisterCommonResolutionsMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(GetCommonResolutionsMenuName());
	check(Menu);

	FToolMenuSection& ResolutionsSection = Menu->AddSection("CommonResolutions");
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	auto AddSubMenuToSection = [&ResolutionsSection](const FString& SectionName, const FText& SubMenuTitle, const TArray<FPlayScreenResolution>& Resolutions)
	{
		ResolutionsSection.AddSubMenu(
			FName(*SectionName),
			SubMenuTitle,
			FText(),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateStatic(&ULevelEditorPlaySettings::AddScreenResolutionSection, &Resolutions, SectionName))
		);
	};

	AddSubMenuToSection(FString("Phones"), LOCTEXT("CommonPhonesSectionHeader", "Phones"), PlaySettings->PhoneScreenResolutions);
	AddSubMenuToSection(FString("Tablets"), LOCTEXT("CommonTabletsSectionHeader", "Tablets"), PlaySettings->TabletScreenResolutions);
	AddSubMenuToSection(FString("Laptops"), LOCTEXT("CommonLaptopsSectionHeader", "Laptops"), PlaySettings->LaptopScreenResolutions);
	AddSubMenuToSection(FString("Monitors"), LOCTEXT("CommonMonitorsSectionHeader", "Monitors"), PlaySettings->MonitorScreenResolutions);
	AddSubMenuToSection(FString("Televisions"), LOCTEXT("CommonTelevesionsSectionHeader", "Televisions"), PlaySettings->TelevisionScreenResolutions);
}

FName ULevelEditorPlaySettings::GetCommonResolutionsMenuName()
{
	const static FName MenuName("EditorSettingsViewer.LevelEditorPlaySettings");
	return MenuName;
}

void ULevelEditorPlaySettings::AddScreenResolutionSection(UToolMenu* InToolMenu, const TArray<FPlayScreenResolution>* Resolutions, const FString SectionName)
{
	check(Resolutions);
	for (const FPlayScreenResolution& Resolution : *Resolutions)
	{
		FInternationalization& I18N = FInternationalization::Get();

		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), FText::AsNumber(Resolution.Width, NULL, I18N.GetInvariantCulture()));
		Args.Add(TEXT("Height"), FText::AsNumber(Resolution.Height, NULL, I18N.GetInvariantCulture()));
		Args.Add(TEXT("AspectRatio"), FText::FromString(Resolution.AspectRatio));

		FText ToolTip;
		if (!Resolution.ProfileName.IsEmpty())
		{
			Args.Add(TEXT("LogicalWidth"), FText::AsNumber(Resolution.LogicalWidth, NULL, I18N.GetInvariantCulture()));
			Args.Add(TEXT("LogicalHeight"), FText::AsNumber(Resolution.LogicalHeight, NULL, I18N.GetInvariantCulture()));
			Args.Add(TEXT("ScaleFactor"), FText::AsNumber(Resolution.ScaleFactor, NULL, I18N.GetInvariantCulture()));
			ToolTip = FText::Format(LOCTEXT("CommonResolutionFormatWithContentScale", "{Width} x {Height} ({AspectRatio}, Logical Res: {LogicalWidth} x {LogicalHeight}, Content Scale: {ScaleFactor})"), Args);
		}
		else
		{
			ToolTip = FText::Format(LOCTEXT("CommonResolutionFormat", "{Width} x {Height} ({AspectRatio})"), Args);
		}

		UCommonResolutionMenuContext* Context = InToolMenu->FindContext<UCommonResolutionMenuContext>();
		check(Context);
		check(Context->GetUIActionFromLevelPlaySettings.IsBound());

		FUIAction Action = Context->GetUIActionFromLevelPlaySettings.Execute(Resolution);
		InToolMenu->AddMenuEntry(FName(*SectionName), FToolMenuEntry::InitMenuEntry(FName(*Resolution.Description), FText::FromString(Resolution.Description), ToolTip, FSlateIcon(), Action));
	}
}

/* ULevelEditorViewportSettings interface
 *****************************************************************************/

ULevelEditorViewportSettings::ULevelEditorViewportSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
	MinimumOrthographicZoom = 250.0f;
	bLevelStreamingVolumePrevis = false;
	BillboardScale = 1.0f;
	TransformWidgetSizeAdjustment = 0.0f;
	SelectedSplinePointSizeAdjustment = 0.0f;
	SplineLineThicknessAdjustment = 0.0f;
	SplineTangentHandleSizeAdjustment = 0.0f;
	SplineTangentScale = 1.0f;
	MeasuringToolUnits = MeasureUnits_Centimeters;
	bAllowArcballRotate = false;
	bAllowScreenRotate = false;
	// Set a default preview mesh
	PreviewMeshes.Add(FSoftObjectPath("/Engine/EditorMeshes/ColorCalibrator/SM_ColorCalibrator.SM_ColorCalibrator"));
}

void ULevelEditorViewportSettings::PostInitProperties()
{
	Super::PostInitProperties();
	UBillboardComponent::SetEditorScale(BillboardScale);
	UArrowComponent::SetEditorScale(BillboardScale);
}

void ULevelEditorViewportSettings::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, bAllowTranslateRotateZWidget))
	{
		if (bAllowTranslateRotateZWidget)
		{
			GLevelEditorModeTools().SetWidgetMode(FWidget::WM_TranslateRotateZ);
		}
		else if (GLevelEditorModeTools().GetWidgetMode() == FWidget::WM_TranslateRotateZ)
		{
			GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
		}
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, bHighlightWithBrackets))
	{
		GEngine->SetSelectedMaterialColor(bHighlightWithBrackets
			? FLinearColor::Black
			: GetDefault<UEditorStyleSettings>()->SelectionColor);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, SelectionHighlightIntensity))
	{
		GEngine->SelectionHighlightIntensity = SelectionHighlightIntensity;
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, BSPSelectionHighlightIntensity))
	{
		GEngine->BSPSelectionHighlightIntensity = BSPSelectionHighlightIntensity;
	}
	else if ((Name == FName(TEXT("UserDefinedPosGridSizes"))) || (Name == FName(TEXT("UserDefinedRotGridSizes"))) || (Name == FName(TEXT("ScalingGridSizes"))) || (Name == FName(TEXT("GridIntervals")))) //@TODO: This should use GET_MEMBER_NAME_CHECKED
	{
		const float MinGridSize = (Name == FName(TEXT("GridIntervals"))) ? 4.0f : 0.0001f; //@TODO: This should use GET_MEMBER_NAME_CHECKED
		TArray<float>* ArrayRef = nullptr;
		int32* IndexRef = nullptr;

		if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, ScalingGridSizes))
		{
			ArrayRef = &(ScalingGridSizes);
			IndexRef = &(CurrentScalingGridSize);
		}

		if (ArrayRef && IndexRef)
		{
			// Don't allow an empty array of grid sizes
			if (ArrayRef->Num() == 0)
			{
				ArrayRef->Add(MinGridSize);
			}

			// Don't allow negative numbers
			for (int32 SizeIdx = 0; SizeIdx < ArrayRef->Num(); ++SizeIdx)
			{
				if ((*ArrayRef)[SizeIdx] < MinGridSize)
				{
					(*ArrayRef)[SizeIdx] = MinGridSize;
				}
			}
		}
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, bUsePowerOf2SnapSize))
	{
		const float BSPSnapSize = bUsePowerOf2SnapSize ? 128.0f : 100.0f;
		UModel::SetGlobalBSPTexelScale(BSPSnapSize);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, BillboardScale))
	{
		UBillboardComponent::SetEditorScale(BillboardScale);
		UArrowComponent::SetEditorScale(BillboardScale);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, bEnableLayerSnap))
	{
		ULevelEditor2DSettings* Settings2D = GetMutableDefault<ULevelEditor2DSettings>();
		if (bEnableLayerSnap && !Settings2D->bEnableSnapLayers)
		{
			Settings2D->bEnableSnapLayers = true;
		}
	}

	if (!FUnrealEdMisc::Get().IsDeletePreferences())
	{
		SaveConfig();
	}

	GEditor->RedrawAllViewports();

	SettingChangedEvent.Broadcast(Name);
}

/* UProjectPackagingSettings interface
 *****************************************************************************/

const UProjectPackagingSettings::FConfigurationInfo UProjectPackagingSettings::ConfigurationInfo[PPBC_MAX] = 
{
	/* PPBC_Debug */         { EBuildConfiguration::Debug, LOCTEXT("DebugConfiguration", "Debug"), LOCTEXT("DebugConfigurationTooltip", "Package the game in Debug configuration") },
	/* PPBC_DebugGame */     { EBuildConfiguration::DebugGame, LOCTEXT("DebugGameConfiguration", "DebugGame"), LOCTEXT("DebugGameConfigurationTooltip", "Package the game in DebugGame configuration") },
	/* PPBC_Development */   { EBuildConfiguration::Development, LOCTEXT("DevelopmentConfiguration", "Development"), LOCTEXT("DevelopmentConfigurationTooltip", "Package the game in Development configuration") },
	/* PPBC_Test */          { EBuildConfiguration::Test, LOCTEXT("TestConfiguration", "Test"), LOCTEXT("TestConfigurationTooltip", "Package the game in Test configuration") },
	/* PPBC_Shipping */      { EBuildConfiguration::Shipping, LOCTEXT("ShippingConfiguration", "Shipping"), LOCTEXT("ShippingConfigurationTooltip", "Package the game in Shipping configuration") },
};

UProjectPackagingSettings::UProjectPackagingSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
{
}


void UProjectPackagingSettings::PostInitProperties()
{
	// Build code projects by default
	Build = EProjectPackagingBuild::IfProjectHasCode;

	// Cache the current set of Blueprint assets selected for nativization.
	CachedNativizeBlueprintAssets = NativizeBlueprintAssets;

	FixCookingPaths();

	Super::PostInitProperties();
}

void UProjectPackagingSettings::FixCookingPaths()
{
	// Fix AlwaysCook/NeverCook paths to use content root
	for (FDirectoryPath& PathToFix : DirectoriesToAlwaysCook)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}

	for (FDirectoryPath& PathToFix : DirectoriesToNeverCook)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}

	for (FDirectoryPath& PathToFix : TestDirectoriesToNotSearch)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}
}

void UProjectPackagingSettings::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.MemberProperty != nullptr)
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	if (Name == FName(TEXT("DirectoriesToAlwaysCook")) || Name == FName(TEXT("DirectoriesToNeverCook")) || Name == FName(TEXT("TestDirectoriesToNotSearch")) || Name == NAME_None)
	{
		// We need to fix paths for no name updates to catch the reloadconfig call
		FixCookingPaths();
	}
	else if (Name == FName((TEXT("StagingDirectory"))))
	{
		// fix up path
		FString Path = StagingDirectory.Path;
		FPaths::MakePathRelativeTo(Path, FPlatformProcess::BaseDir());
		StagingDirectory.Path = Path;
	}
	else if (Name == FName(TEXT("ForDistribution")))
	{
		if (ForDistribution && BuildConfiguration != EProjectPackagingBuildConfigurations::PPBC_Shipping)
		{
			BuildConfiguration = EProjectPackagingBuildConfigurations::PPBC_Shipping;
			// force serialization for "Build COnfiguration"
			UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UProjectPackagingSettings, BuildConfiguration)), GetDefaultConfigFilename());
		}
	}
	else if (Name == FName(TEXT("bGenerateChunks")))
	{
		if (bGenerateChunks)
		{
			UsePakFile = true;
		}
	}
	else if (Name == FName(TEXT("UsePakFile")))
	{
		if (!UsePakFile)
		{
			bGenerateChunks = false;
			bBuildHttpChunkInstallData = false;
		}
	}
	else if (Name == FName(TEXT("bBuildHTTPChunkInstallData")))
	{
		if (bBuildHttpChunkInstallData)
		{
			UsePakFile = true;
			bGenerateChunks = true;
			//Ensure data is something valid
			if (HttpChunkInstallDataDirectory.Path.IsEmpty())
			{
				auto CloudInstallDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath())) / TEXT("ChunkInstall");
				HttpChunkInstallDataDirectory.Path = CloudInstallDir;
			}
			if (HttpChunkInstallDataVersion.IsEmpty())
			{
				HttpChunkInstallDataVersion = TEXT("release1");
			}
		}
	}
	else if (Name == FName((TEXT("ApplocalPrerequisitesDirectory"))))
	{
		// If a variable is already in use, assume the user knows what they are doing and don't modify the path
		if(!ApplocalPrerequisitesDirectory.Path.Contains("$("))
		{
			// Try making the path local to either project or engine directories.
			FString EngineRootedPath = ApplocalPrerequisitesDirectory.Path;
			FString EnginePath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::EngineDir())) + "/";
			FPaths::MakePathRelativeTo(EngineRootedPath, *EnginePath);
			if (FPaths::IsRelative(EngineRootedPath))
			{
				ApplocalPrerequisitesDirectory.Path = "$(EngineDir)/" + EngineRootedPath;
				return;
			}

			FString ProjectRootedPath = ApplocalPrerequisitesDirectory.Path;
			FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath())) + "/";
			FPaths::MakePathRelativeTo(ProjectRootedPath, *ProjectPath);
			if (FPaths::IsRelative(EngineRootedPath))
			{
				ApplocalPrerequisitesDirectory.Path = "$(ProjectDir)/" + ProjectRootedPath;
				return;
			}
		}
	}
	else if (Name == FName((TEXT("NativizeBlueprintAssets"))))
	{
		int32 AssetIndex;
		auto OnSelectBlueprintForExclusiveNativizationLambda = [](const FString& PackageName, bool bSelect)
		{
			if (!PackageName.IsEmpty())
			{
				// This should only apply to loaded packages. Any unloaded packages defer setting the transient flag to when they're loaded.
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					// Find the Blueprint asset within the package.
					if (UBlueprint* Blueprint = FindObject<UBlueprint>(Package, *FPaths::GetBaseFilename(PackageName)))
					{
						// We're toggling the transient flag on or off.
						if ((Blueprint->NativizationFlag == EBlueprintNativizationFlag::ExplicitlyEnabled) != bSelect)
						{
							Blueprint->NativizationFlag = bSelect ? EBlueprintNativizationFlag::ExplicitlyEnabled : EBlueprintNativizationFlag::Disabled;
						}
					}
				}
			}
		};

		if (NativizeBlueprintAssets.Num() > 0)
		{
			for (AssetIndex = 0; AssetIndex < NativizeBlueprintAssets.Num(); ++AssetIndex)
			{
				const FString& PackageName = NativizeBlueprintAssets[AssetIndex].FilePath;
				if (AssetIndex >= CachedNativizeBlueprintAssets.Num())
				{
					// A new entry was added; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
					OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

					// Add an entry to the end of the cached list.
					CachedNativizeBlueprintAssets.Add(NativizeBlueprintAssets[AssetIndex]);
				}
				else if (!PackageName.Equals(CachedNativizeBlueprintAssets[AssetIndex].FilePath))
				{
					if (NativizeBlueprintAssets.Num() < CachedNativizeBlueprintAssets.Num())
					{
						// An entry was removed; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);

						// Remove this entry from the cached list.
						CachedNativizeBlueprintAssets.RemoveAt(AssetIndex);
					}
					else if(NativizeBlueprintAssets.Num() > CachedNativizeBlueprintAssets.Num())
					{
						// A new entry was inserted; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

						// Insert the new entry into the cached list.
						CachedNativizeBlueprintAssets.Insert(NativizeBlueprintAssets[AssetIndex], AssetIndex);
					}
					else
					{
						// An entry was changed; toggle the exclusive flag on the corresponding Blueprint assets (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
						OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

						// Update the cached entry.
						CachedNativizeBlueprintAssets[AssetIndex].FilePath = PackageName;
					}
				}
			}

			if (CachedNativizeBlueprintAssets.Num() > NativizeBlueprintAssets.Num())
			{
				// Removed entries at the end of the list; toggle the exclusive flag on the corresponding Blueprint asset(s) (if loaded).
				for (AssetIndex = NativizeBlueprintAssets.Num(); AssetIndex < CachedNativizeBlueprintAssets.Num(); ++AssetIndex)
				{
					OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
				}

				// Remove entries from the end of the cached list.
				CachedNativizeBlueprintAssets.RemoveAt(NativizeBlueprintAssets.Num(), CachedNativizeBlueprintAssets.Num() - NativizeBlueprintAssets.Num());
			}
		}
		else if(CachedNativizeBlueprintAssets.Num() > 0)
		{
			// Removed all entries; toggle the exclusive flag on the corresponding Blueprint asset(s) (if loaded).
			for (AssetIndex = 0; AssetIndex < CachedNativizeBlueprintAssets.Num(); ++AssetIndex)
			{
				OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
			}

			// Clear the cached list.
			CachedNativizeBlueprintAssets.Empty();
		}
	}
}

bool UProjectPackagingSettings::CanEditChange( const FProperty* InProperty ) const
{
	if (InProperty->GetFName() == FName(TEXT("NativizeBlueprintAssets")))
	{
		return BlueprintNativizationMethod == EProjectPackagingBlueprintNativizationMethod::Exclusive;
	}

	return Super::CanEditChange(InProperty);
}

bool UProjectPackagingSettings::AddBlueprintAssetToNativizationList(const class UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		// Make sure it's not already in the exclusive list. This can happen if the user previously added this asset in the Project Settings editor.
		const bool bFound = IsBlueprintAssetInNativizationList(InBlueprint);
		if (!bFound)
		{
			// Add this Blueprint asset to the exclusive list.
			FFilePath FileInfo;
			FileInfo.FilePath = PackageName;
			NativizeBlueprintAssets.Add(FileInfo);

			// Also add it to the mirrored list for tracking edits.
			CachedNativizeBlueprintAssets.Add(FileInfo);

			return true;
		}
	}

	return false;
}

bool UProjectPackagingSettings::RemoveBlueprintAssetFromNativizationList(const class UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		int32 AssetIndex = FindBlueprintInNativizationList(InBlueprint);
		if (AssetIndex >= 0)
		{
			// Note: Intentionally not using RemoveAtSwap() here, so that the order is preserved.
			NativizeBlueprintAssets.RemoveAt(AssetIndex);

			// Also remove it from the mirrored list (for tracking edits).
			CachedNativizeBlueprintAssets.RemoveAt(AssetIndex);

			return true;
		}
	}

	return false;
}

TArray<EProjectPackagingBuildConfigurations> UProjectPackagingSettings::GetValidPackageConfigurations()
{
	// Check if the project has code
	FProjectStatus ProjectStatus;
	bool bHasCode = IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && ProjectStatus.bCodeBasedProject;

	// If if does, find all the targets
	const TArray<FTargetInfo>* Targets = nullptr;
	if (bHasCode)
	{
		Targets = &(FDesktopPlatformModule::Get()->GetTargetsForCurrentProject());
	}

	// Set up all the configurations
	TArray<EProjectPackagingBuildConfigurations> Configurations;
	for (int32 Idx = 0; Idx < PPBC_MAX; Idx++)
	{
		EProjectPackagingBuildConfigurations PackagingConfiguration = (EProjectPackagingBuildConfigurations)Idx;

		// Check the target type is valid
		const UProjectPackagingSettings::FConfigurationInfo& Info = UProjectPackagingSettings::ConfigurationInfo[Idx];
		if(!bHasCode && Info.Configuration == EBuildConfiguration::DebugGame)
		{
			continue;
		}

		Configurations.Add(PackagingConfiguration);
	}
	return Configurations;
}

const FTargetInfo* UProjectPackagingSettings::GetBuildTargetInfo() const
{
	const FTargetInfo* DefaultGameTarget = nullptr;
	const FTargetInfo* DefaultClientTarget = nullptr;
	for (const FTargetInfo& Target : FDesktopPlatformModule::Get()->GetTargetsForCurrentProject())
	{
		if (Target.Name == BuildTarget)
		{
			return &Target;
		}
		else if (Target.Type == EBuildTargetType::Game && (DefaultGameTarget == nullptr || Target.Name < DefaultGameTarget->Name))
		{
			DefaultGameTarget = &Target;
		}
		else if (Target.Type == EBuildTargetType::Client && (DefaultClientTarget == nullptr || Target.Name < DefaultClientTarget->Name))
		{
			DefaultClientTarget = &Target;
		}
	}
	return (DefaultGameTarget != nullptr)? DefaultGameTarget : DefaultClientTarget;
}

int32 UProjectPackagingSettings::FindBlueprintInNativizationList(const UBlueprint* InBlueprint) const
{
	int32 ListIndex = INDEX_NONE;
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		for (int32 AssetIndex = 0; AssetIndex < NativizeBlueprintAssets.Num(); ++AssetIndex)
		{
			if (NativizeBlueprintAssets[AssetIndex].FilePath.Equals(PackageName, ESearchCase::IgnoreCase))
			{
				ListIndex = AssetIndex;
				break;
			}
		}
	}
	return ListIndex;
}

/* UCrashReporterSettings interface
*****************************************************************************/
UCrashReporterSettings::UCrashReporterSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#undef LOCTEXT_NAMESPACE
