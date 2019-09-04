// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlatformInfo.h"
#include "DesktopPlatformPrivate.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PlatformInfo"

namespace PlatformInfo
{
	TArray<FName> AllPlatformGroupNames;
	TArray<FName> AllVanillaPlatformNames;

namespace
{

TArray<FPlatformInfo> AllPlatformInfoArray;

// we don't need any of this without the editor, although we would ideally not even compile this outside of the editor
// @todo platplug: Figure out why this is compiled on target devices
#if WITH_EDITOR || IS_PROGRAM

void BuildPlatformInfo(const FName& InPlatformInfoName, const FName& InTargetPlatformName, const FText& InDisplayName, const EPlatformType InPlatformType, const EPlatformFlags::Flags InPlatformFlags, const FPlatformIconPaths& InIconPaths, const FString& InUATCommandLine, const FString& InAutoSDKPath, EPlatformSDKStatus InStatus, const FString& InTutorial, bool InEnabled, FString InBinaryFolderName, FString InIniPlatformName, bool InUsesHostCompiler, bool InUATClosesAfterLaunch, bool InIsConfidential, const FName& InUBTTargetId, const FName& InPlatformGroupName, bool InTargetPlatformCanUseCrashReporter)
{
	FPlatformInfo& PlatformInfo = AllPlatformInfoArray.Emplace_GetRef();

	PlatformInfo.PlatformInfoName = InPlatformInfoName;
	PlatformInfo.TargetPlatformName = InTargetPlatformName;

	// See if this name also contains a flavor
	const FString InPlatformInfoNameString = InPlatformInfoName.ToString();
	{
		int32 UnderscoreLoc;
		if(InPlatformInfoNameString.FindChar(TEXT('_'), UnderscoreLoc))
		{
			PlatformInfo.VanillaPlatformName = *InPlatformInfoNameString.Mid(0, UnderscoreLoc);
			PlatformInfo.PlatformFlavor = *InPlatformInfoNameString.Mid(UnderscoreLoc + 1);
		}
		else
		{
			PlatformInfo.VanillaPlatformName = InPlatformInfoName;
		}
	}

	if (PlatformInfo.VanillaPlatformName != NAME_None)
	{
		PlatformInfo::AllVanillaPlatformNames.AddUnique(PlatformInfo.VanillaPlatformName);
	}

	PlatformInfo.DisplayName = InDisplayName;
	PlatformInfo.PlatformType = InPlatformType;
	PlatformInfo.PlatformFlags = InPlatformFlags;
	PlatformInfo.IconPaths = InIconPaths;
	PlatformInfo.UATCommandLine = InUATCommandLine;
	PlatformInfo.AutoSDKPath = InAutoSDKPath;
	PlatformInfo.BinaryFolderName = InBinaryFolderName;
	PlatformInfo.IniPlatformName = InIniPlatformName;
	PlatformInfo.UBTTargetId = InUBTTargetId;
	PlatformInfo.PlatformGroupName = InPlatformGroupName;

	if (InPlatformGroupName != NAME_None)
	{
		PlatformInfo::AllPlatformGroupNames.AddUnique(InPlatformGroupName);
	}

	// Generate the icon style names for FEditorStyle
	PlatformInfo.IconPaths.NormalStyleName = *FString::Printf(TEXT("Launcher.Platform_%s"), *InPlatformInfoNameString);
	PlatformInfo.IconPaths.LargeStyleName  = *FString::Printf(TEXT("Launcher.Platform_%s.Large"), *InPlatformInfoNameString);
	PlatformInfo.IconPaths.XLargeStyleName = *FString::Printf(TEXT("Launcher.Platform_%s.XLarge"), *InPlatformInfoNameString);

	// SDK data
	PlatformInfo.SDKStatus = InStatus;
	PlatformInfo.SDKTutorial = InTutorial;

	// Distribution data
	PlatformInfo.bEnabledForUse = InEnabled;
	PlatformInfo.bUsesHostCompiler = InUsesHostCompiler;
	PlatformInfo.bUATClosesAfterLaunch = InUATClosesAfterLaunch;
	PlatformInfo.bIsConfidential = InIsConfidential;
	PlatformInfo.bTargetPlatformCanUseCrashReporter = InTargetPlatformCanUseCrashReporter;
}


void BuildHardcodedPlatforms()
{
	// PlatformInfoName									TargetPlatformName			DisplayName														PlatformType			PlatformFlags					IconPaths																																		UATCommandLine										AutoSDKPath			SDKStatus						SDKTutorial																								bEnabledForUse										BinaryFolderName	IniPlatformName		FbUsesHostCompiler		bUATClosesAfterLaunch	bIsConfidential	UBTTargetId (match UBT's UnrealTargetPlatform enum)		bTargetPlatformCanUseCrashReporter
	BuildPlatformInfo(TEXT("AllDesktop"),				TEXT("AllDesktop"),			LOCTEXT("DesktopTargetPlatDisplay", "Desktop (Win+Mac+Linux)"),	EPlatformType::Game,	EPlatformFlags::None,			FPlatformIconPaths(TEXT("Launcher/Desktop/Platform_Desktop_24x"), TEXT("Launcher/Desktop/Platform_Desktop_128x")),					TEXT(""),											TEXT(""),			EPlatformSDKStatus::Unknown,	TEXT(""),																								PLATFORM_WINDOWS /* see note below */,						TEXT(""),			TEXT(""),			false,					true,					false,			TEXT("AllDesktop"),	TEXT("Desktop"),	true);
	// Note: For "AllDesktop" bEnabledForUse value, see SProjectTargetPlatformSettings::Construct !!!! IsAvailableOnWindows || IsAvailableOnMac || IsAvailableOnLinux
}

// Gets a string from a section, or empty string if it didn't exist
FString GetSectionString(const FConfigSection& Section, FName Key)
{
	// look for a value prefixed with host:
	FName HostKey = *FString::Printf(TEXT("%s:%s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), *Key.ToString());
	const FConfigValue* HostValue = Section.Find(HostKey);
	return HostValue ? HostValue->GetValue() : Section.FindRef(Key).GetValue();
}

// Gets a bool from a section, or false if it didn't exist
bool GetSectionBool(const FConfigSection& Section, FName Key)
{
	return FCString::ToBool(*GetSectionString(Section, Key));
}

EPlatformFlags::Flags ConvertPlatformFlags(const FString& String)
{
	if (String == TEXT("") == 0 || String == TEXT("None")) { return EPlatformFlags::None; }
	if (String == TEXT("CookFlavor")) { return EPlatformFlags::CookFlavor; }
	if (String == TEXT("BuildFlavor")) { return EPlatformFlags::BuildFlavor; }

	UE_LOG(LogInit, Fatal, TEXT("Unknown platform flag %s in PlatformInfo"), *String);
	return EPlatformFlags::None;
}

void ParseDataDrivenPlatformInfo(const TCHAR* Name, const FConfigSection& Section)
{
	// @todo platplug: use FNames instead of TCHAR* for keys, so we don't ahve to re-convert every time
	FName TargetPlatformName = *GetSectionString(Section, TEXT("TargetPlatformName"));
	FString DisplayName = GetSectionString(Section, TEXT("DisplayName"));
	FString PlatformType = GetSectionString(Section, TEXT("PlatformType"));
	FString PlatformFlags = GetSectionString(Section, TEXT("PlatformFlags"));
	FString NormalIconPath = GetSectionString(Section, TEXT("NormalIconPath"));
	FString LargeIconPath = GetSectionString(Section, TEXT("LargeIconPath"));
	FString XLargeIconPath = GetSectionString(Section, TEXT("XLargeIconPath"));
	// no one has an XLarge path yet, but in case they add one, this will use it
	if (XLargeIconPath == TEXT(""))
	{
		XLargeIconPath = LargeIconPath;
	}
	FString UATCommandLine = GetSectionString(Section, TEXT("UATCommandLine"));
	FString AutoSDKPath = GetSectionString(Section, TEXT("AutoSDKPath"));
	FString TutorialPath = GetSectionString(Section, TEXT("TutorialPath"));
	bool bIsEnabled = GetSectionBool(Section, TEXT("bIsEnabled"));
	FString BinariesDirectoryName = GetSectionString(Section, TEXT("BinariesDirectoryName"));
	FString IniPlatformName = GetSectionString(Section, TEXT("IniPlatformName"));
	bool bUsesHostCompiler = GetSectionBool(Section, TEXT("bUsesHostCompiler"));
	bool bUATClosesAfterLaunch = GetSectionBool(Section, TEXT("bUATClosesAfterLaunch"));
	bool bIsConfidential = GetSectionBool(Section, TEXT("bIsConfidential"));
	FName UBTTargetID = *GetSectionString(Section, TEXT("UBTTargetID"));
	FName PlatformGroupName = *GetSectionString(Section, TEXT("PlatformGroupName"));
	FString TargetPlatformCanUseCrashReporterString = GetSectionString(Section, TEXT("bTargetPlatformCanUseCrashReporter"));
	bool bTargetPlatformCanUseCrashReporter = TargetPlatformCanUseCrashReporterString.IsEmpty() ? true : GetSectionBool(Section, TEXT("bTargetPlatformCanUseCrashReporter"));

	BuildPlatformInfo(Name, TargetPlatformName, FText::FromString(DisplayName), EPlatformTypeFromString(*PlatformType), ConvertPlatformFlags(PlatformFlags), FPlatformIconPaths(NormalIconPath, LargeIconPath, XLargeIconPath), UATCommandLine,
		AutoSDKPath, PlatformInfo::EPlatformSDKStatus::Unknown, TutorialPath, bIsEnabled, BinariesDirectoryName, IniPlatformName, bUsesHostCompiler, bUATClosesAfterLaunch, bIsConfidential, UBTTargetID, PlatformGroupName, bTargetPlatformCanUseCrashReporter);
}

void LoadDataDrivenPlatforms()
{
	// look for the standard DataDriven ini files
	int32 NumDDInfoFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();
	for (int32 Index = 0; Index < NumDDInfoFiles; Index++)
	{
		FConfigFile IniFile;
		FString PlatformName;

		FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

		// now walk over the file, looking for ShaderPlatformInfo sections
		for (auto Section : IniFile)
		{
			if (Section.Key.StartsWith(TEXT("PlatformInfo ")))
			{
				const FString& SectionName = Section.Key;
				ParseDataDrivenPlatformInfo(*SectionName.Mid(13), Section.Value);
			}
		}
	}
}

struct FPlatformInfoAutoInit
{
	FPlatformInfoAutoInit()
	{
		FCoreDelegates::ConfigReadyForUse.AddLambda([]
		{
			BuildHardcodedPlatforms();
			LoadDataDrivenPlatforms();
		});
	}

} GPlatformInfoAutoInit;
#endif

} // anonymous namespace

const FPlatformInfo* FindPlatformInfo(const FName& InPlatformName)
{
	for(const FPlatformInfo& PlatformInfo : AllPlatformInfoArray)
	{
		if(PlatformInfo.PlatformInfoName == InPlatformName)
		{
			return &PlatformInfo;
		}
	}
	return nullptr;
}

const FPlatformInfo* FindVanillaPlatformInfo(const FName& InPlatformName)
{
	const FPlatformInfo* const FoundInfo = FindPlatformInfo(InPlatformName);
	return (FoundInfo) ? (FoundInfo->IsVanilla()) ? FoundInfo : FindPlatformInfo(FoundInfo->VanillaPlatformName) : nullptr;
}

void UpdatePlatformSDKStatus(FString InPlatformName, EPlatformSDKStatus InStatus)
{
	for(const FPlatformInfo& PlatformInfo : AllPlatformInfoArray)
	{
		if(PlatformInfo.VanillaPlatformName == FName(*InPlatformName))
		{
			const_cast<FPlatformInfo&>(PlatformInfo).SDKStatus = InStatus;
		}
	}
}

void UpdatePlatformDisplayName(FString InPlatformName, FText InDisplayName)
{
	for (const FPlatformInfo& PlatformInfo : AllPlatformInfoArray)
	{
		if (PlatformInfo.TargetPlatformName == FName(*InPlatformName))
		{
			const_cast<FPlatformInfo&>(PlatformInfo).DisplayName = InDisplayName;
		}
	}
}

const TArray<FPlatformInfo>& GetPlatformInfoArray()
{
	return AllPlatformInfoArray;
}

TArray<FVanillaPlatformEntry> BuildPlatformHierarchy(const EPlatformFilter InFilter)
{
	TArray<FVanillaPlatformEntry> VanillaPlatforms;

	// Build up a tree from the platforms we support (vanilla outers, with a list of flavors)
	// PlatformInfoArray should be ordered in such a way that the vanilla platforms always appear before their flavors
	for (const PlatformInfo::FPlatformInfo& PlatformInfo : AllPlatformInfoArray)
	{
		if(PlatformInfo.IsVanilla())
		{
			VanillaPlatforms.Add(FVanillaPlatformEntry(&PlatformInfo));
		}
		else
		{
			const bool bHasBuildFlavor = !!(PlatformInfo.PlatformFlags & EPlatformFlags::BuildFlavor);
			const bool bHasCookFlavor  = !!(PlatformInfo.PlatformFlags & EPlatformFlags::CookFlavor);
			
			const bool bValidFlavor = 
				InFilter == EPlatformFilter::All || 
				(InFilter == EPlatformFilter::BuildFlavor && bHasBuildFlavor) || 
				(InFilter == EPlatformFilter::CookFlavor && bHasCookFlavor);

			if(bValidFlavor)
			{
				const FName VanillaPlatformName = PlatformInfo.VanillaPlatformName;
				FVanillaPlatformEntry* const VanillaEntry = VanillaPlatforms.FindByPredicate([VanillaPlatformName](const FVanillaPlatformEntry& Item) -> bool
				{
					return Item.PlatformInfo->PlatformInfoName == VanillaPlatformName;
				});
				check(VanillaEntry);
				VanillaEntry->PlatformFlavors.Add(&PlatformInfo);
			}
		}
	}

	return VanillaPlatforms;
}

FVanillaPlatformEntry BuildPlatformHierarchy(const FName& InPlatformName, const EPlatformFilter InFilter)
{
	FVanillaPlatformEntry VanillaPlatformEntry;
	const FPlatformInfo* VanillaPlatformInfo = FindVanillaPlatformInfo(InPlatformName);
	
	if (VanillaPlatformInfo)
	{
		VanillaPlatformEntry.PlatformInfo = VanillaPlatformInfo;
		
		for (const PlatformInfo::FPlatformInfo& PlatformInfo : AllPlatformInfoArray)
		{
			if (!PlatformInfo.IsVanilla() && PlatformInfo.VanillaPlatformName == VanillaPlatformInfo->PlatformInfoName)
			{
				const bool bHasBuildFlavor = !!(PlatformInfo.PlatformFlags & EPlatformFlags::BuildFlavor);
				const bool bHasCookFlavor = !!(PlatformInfo.PlatformFlags & EPlatformFlags::CookFlavor);

				const bool bValidFlavor =
					InFilter == EPlatformFilter::All ||
					(InFilter == EPlatformFilter::BuildFlavor && bHasBuildFlavor) ||
					(InFilter == EPlatformFilter::CookFlavor && bHasCookFlavor);

				if (bValidFlavor)
				{
					VanillaPlatformEntry.PlatformFlavors.Add(&PlatformInfo);
				}
			}
		}
	}
	
	return VanillaPlatformEntry;
}

EPlatformType EPlatformTypeFromString(const FString& PlatformTypeName)
{
	if (FCString::Strcmp(*PlatformTypeName, TEXT("Game")) == 0)
	{
		return PlatformInfo::EPlatformType::Game;
	}
	else if (FCString::Strcmp(*PlatformTypeName, TEXT("Editor")) == 0)
	{
		return PlatformInfo::EPlatformType::Editor;
	}
	else if (FCString::Strcmp(*PlatformTypeName, TEXT("Client")) == 0)
	{
		return PlatformInfo::EPlatformType::Client;
	}
	else if (FCString::Strcmp(*PlatformTypeName, TEXT("Server")) == 0)
	{
		return PlatformInfo::EPlatformType::Server;
	}
	else
	{
		UE_LOG(LogDesktopPlatform, Warning, TEXT("Unable to read Platform Type from %s, defaulting to Game"), *PlatformTypeName);
		return PlatformInfo::EPlatformType::Game;
	}
}

const TArray<FName>& GetAllPlatformGroupNames()
{
	return PlatformInfo::AllPlatformGroupNames;
}

const TArray<FName>& GetAllVanillaPlatformNames()
{
	return PlatformInfo::AllVanillaPlatformNames;
}

} // namespace PlatformInfo

FString LexToString(const PlatformInfo::EPlatformType Value)
{
	switch (Value)
	{
	case PlatformInfo::EPlatformType::Game:
		return TEXT("Game");
	case PlatformInfo::EPlatformType::Editor:
		return TEXT("Editor");
	case PlatformInfo::EPlatformType::Client:
		return TEXT("Client");
	case PlatformInfo::EPlatformType::Server:
		return TEXT("Server");
	}

	return TEXT("");
}

#undef LOCTEXT_NAMESPACE
