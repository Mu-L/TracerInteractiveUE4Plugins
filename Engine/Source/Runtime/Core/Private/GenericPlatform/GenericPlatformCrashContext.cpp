// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Internationalization/Culture.h"
#include "Misc/Optional.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineBuildSettings.h"
#include "Stats/Stats.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"

#ifndef NOINITCRASHREPORTER
#define NOINITCRASHREPORTER 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCrashContext, Display, All);

extern CORE_API bool GIsGPUCrashed;

/*-----------------------------------------------------------------------------
	FGenericCrashContext
-----------------------------------------------------------------------------*/

const ANSICHAR* FGenericCrashContext::CrashContextRuntimeXMLNameA = "CrashContext.runtime-xml";
const TCHAR* FGenericCrashContext::CrashContextRuntimeXMLNameW = TEXT( "CrashContext.runtime-xml" );

const ANSICHAR* FGenericCrashContext::CrashConfigFileNameA = "CrashReportClient.ini";
const TCHAR* FGenericCrashContext::CrashConfigFileNameW = TEXT("CrashReportClient.ini");
const FString FGenericCrashContext::CrashConfigExtension = TEXT(".ini");
const FString FGenericCrashContext::ConfigSectionName = TEXT("CrashReportClient");
const FString FGenericCrashContext::CrashConfigPurgeDays = TEXT("CrashConfigPurgeDays");
const FString FGenericCrashContext::CrashGUIDRootPrefix = TEXT("UE4CC-");

const FString FGenericCrashContext::CrashContextExtension = TEXT(".runtime-xml");
const FString FGenericCrashContext::RuntimePropertiesTag = TEXT( "RuntimeProperties" );
const FString FGenericCrashContext::PlatformPropertiesTag = TEXT( "PlatformProperties" );
const FString FGenericCrashContext::EngineDataTag = TEXT( "EngineData" );
const FString FGenericCrashContext::GameDataTag = TEXT( "GameData" );
const FString FGenericCrashContext::EnabledPluginsTag = TEXT("EnabledPlugins");
const FString FGenericCrashContext::UE4MinidumpName = TEXT( "UE4Minidump.dmp" );
const FString FGenericCrashContext::NewLineTag = TEXT( "&nl;" );

const FString FGenericCrashContext::CrashTypeCrash = TEXT("Crash");
const FString FGenericCrashContext::CrashTypeAssert = TEXT("Assert");
const FString FGenericCrashContext::CrashTypeEnsure = TEXT("Ensure");
const FString FGenericCrashContext::CrashTypeGPU = TEXT("GPUCrash");
const FString FGenericCrashContext::CrashTypeHang = TEXT("Hang");

const FString FGenericCrashContext::EngineModeExUnknown = TEXT("Unset");
const FString FGenericCrashContext::EngineModeExDirty = TEXT("Dirty");
const FString FGenericCrashContext::EngineModeExVanilla = TEXT("Vanilla");

bool FGenericCrashContext::bIsInitialized = false;
FPlatformMemoryStats FGenericCrashContext::CrashMemoryStats = FPlatformMemoryStats();
int32 FGenericCrashContext::StaticCrashContextIndex = 0;

const FGuid FGenericCrashContext::ExecutionGuid = FGuid::NewGuid();

namespace NCachedCrashContextProperties
{
	static bool bIsInternalBuild;
	static bool bIsPerforceBuild;
	static bool bIsSourceDistribution;
	static bool bIsUE4Release;
	static TOptional<bool> bIsVanilla;
	static FString GameName;
	static FString ExecutableName;
	static FString DeploymentName;
	static FString BaseDir;
	static FString RootDir;
	static FString EpicAccountId;
	static FString LoginIdStr;
	static FString OsVersion;
	static FString OsSubVersion;
	static int32 NumberOfCores;
	static int32 NumberOfCoresIncludingHyperthreads;
	static FString CPUVendor;
	static FString CPUBrand;
	static FString PrimaryGPUBrand;
	static FString UserName;
	static FString DefaultLocale;
	static int32 CrashDumpMode;
	static int32 SecondsSinceStart;
	static FString CrashGUIDRoot;
	static FString UserActivityHint;
	static FString GameSessionID;
	static FString CommandLine;
	static int32 LanguageLCID;
	static FString CrashReportClientRichText;
	static FString GameStateName;
	static TArray<FString> EnabledPluginsList;
	static TMap<FString, FString> EngineData;
	static TMap<FString, FString> GameData;
}

void FGenericCrashContext::Initialize()
{
#if !NOINITCRASHREPORTER
	NCachedCrashContextProperties::bIsInternalBuild = FEngineBuildSettings::IsInternalBuild();
	NCachedCrashContextProperties::bIsPerforceBuild = FEngineBuildSettings::IsPerforceBuild();
	NCachedCrashContextProperties::bIsSourceDistribution = FEngineBuildSettings::IsSourceDistribution();
	NCachedCrashContextProperties::bIsUE4Release = FApp::IsEngineInstalled();

	NCachedCrashContextProperties::GameName = FString::Printf( TEXT("UE4-%s"), FApp::GetProjectName() );
	NCachedCrashContextProperties::ExecutableName = FPlatformProcess::ExecutableName();
	NCachedCrashContextProperties::BaseDir = FPlatformProcess::BaseDir();
	NCachedCrashContextProperties::RootDir = FPlatformMisc::RootDir();
	NCachedCrashContextProperties::EpicAccountId = FPlatformMisc::GetEpicAccountId();
	NCachedCrashContextProperties::LoginIdStr = FPlatformMisc::GetLoginId();
	FPlatformMisc::GetOSVersions(NCachedCrashContextProperties::OsVersion, NCachedCrashContextProperties::OsSubVersion);
	NCachedCrashContextProperties::NumberOfCores = FPlatformMisc::NumberOfCores();
	NCachedCrashContextProperties::NumberOfCoresIncludingHyperthreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	NCachedCrashContextProperties::CPUVendor = FPlatformMisc::GetCPUVendor();
	NCachedCrashContextProperties::CPUBrand = FPlatformMisc::GetCPUBrand();
	NCachedCrashContextProperties::PrimaryGPUBrand = FPlatformMisc::GetPrimaryGPUBrand();
	NCachedCrashContextProperties::UserName = FPlatformProcess::UserName();
	NCachedCrashContextProperties::DefaultLocale = FPlatformMisc::GetDefaultLocale();
	NCachedCrashContextProperties::CommandLine = FCommandLine::IsInitialized() ? FCommandLine::GetOriginalForLogging() : TEXT(""); 

	// Use -epicapp value from the commandline to start. This will also be set by the game
	FParse::Value(FCommandLine::Get(), TEXT("EPICAPP="), NCachedCrashContextProperties::DeploymentName);

	if (FInternationalization::IsAvailable())
	{
		NCachedCrashContextProperties::LanguageLCID = FInternationalization::Get().GetCurrentCulture()->GetLCID();
	}
	else
	{
		FCulturePtr DefaultCulture = FInternationalization::Get().GetCulture(TEXT("en"));
		if (DefaultCulture.IsValid())
		{
			NCachedCrashContextProperties::LanguageLCID = DefaultCulture->GetLCID();
		}
		else
		{
			const int DefaultCultureLCID = 1033;
			NCachedCrashContextProperties::LanguageLCID = DefaultCultureLCID;
		}
	}

	// Using the -fullcrashdump parameter will cause full memory minidumps to be created for crashes
	NCachedCrashContextProperties::CrashDumpMode = (int32)ECrashDumpMode::Default;
	if (FPlatformMisc::SupportsFullCrashDumps() && FCommandLine::IsInitialized())
	{
		const TCHAR* CmdLine = FCommandLine::Get();
		if (FParse::Param( CmdLine, TEXT("fullcrashdumpalways") ))
		{
			NCachedCrashContextProperties::CrashDumpMode = (int32)ECrashDumpMode::FullDumpAlways;
		}
		else if (FParse::Param( CmdLine, TEXT("fullcrashdump") ))
		{
			NCachedCrashContextProperties::CrashDumpMode = (int32)ECrashDumpMode::FullDump;
		}
	}

	const FGuid Guid = FGuid::NewGuid();
	const FString IniPlatformName(FPlatformProperties::IniPlatformName());
	NCachedCrashContextProperties::CrashGUIDRoot = FString::Printf(TEXT("%s%s-%s"), *CrashGUIDRootPrefix, *IniPlatformName, *Guid.ToString(EGuidFormats::Digits));

	// Initialize delegate for updating SecondsSinceStart, because FPlatformTime::Seconds() is not POSIX safe.
	const float PollingInterval = 1.0f;
	FTicker::GetCoreTicker().AddTicker( FTickerDelegate::CreateLambda( []( float DeltaTime )
	{
        QUICK_SCOPE_CYCLE_COUNTER(STAT_NCachedCrashContextProperties_LambdaTicker);

		NCachedCrashContextProperties::SecondsSinceStart = int32(FPlatformTime::Seconds() - GStartTime);
		return true;
	} ), PollingInterval );

	FCoreDelegates::UserActivityStringChanged.AddLambda([](const FString& InUserActivity)
	{
		NCachedCrashContextProperties::UserActivityHint = InUserActivity;
	});

	FCoreDelegates::GameSessionIDChanged.AddLambda([](const FString& InGameSessionID)
	{
		NCachedCrashContextProperties::GameSessionID = InGameSessionID;
	});

	FCoreDelegates::GameStateClassChanged.AddLambda([](const FString& InGameStateName)
	{
		NCachedCrashContextProperties::GameStateName = InGameStateName;
	});

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FCoreDelegates::CrashOverrideParamsChanged.AddLambda([](const FCrashOverrideParameters& InParams)
	{
		if (InParams.bSetCrashReportClientMessageText)
		{
			NCachedCrashContextProperties::CrashReportClientRichText = InParams.CrashReportClientMessageText;
		}
		if (InParams.bSetGameNameSuffix)
		{
			NCachedCrashContextProperties::GameName = FString(TEXT("UE4-")) + FApp::GetProjectName() + InParams.GameNameSuffix;
		}
	});
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FCoreDelegates::IsVanillaProductChanged.AddLambda([](bool bIsVanilla)
	{
		NCachedCrashContextProperties::bIsVanilla = bIsVanilla;
	});

	FCoreDelegates::ConfigReadyForUse.AddStatic(FGenericCrashContext::InitializeFromConfig);

	bIsInitialized = true;
#endif	// !NOINITCRASHREPORTER
}

void FGenericCrashContext::InitializeFromConfig()
{
#if !NOINITCRASHREPORTER
	PurgeOldCrashConfig();

	const bool bForceGetSection = false;
	const bool bConstSection = true;
	FConfigSection* CRCConfigSection = GConfig->GetSectionPrivate(*ConfigSectionName, bForceGetSection, bConstSection, GEngineIni);

	if (CRCConfigSection != nullptr)
	{
		// Create a config file and save to a temp location. This file will be copied to
		// the crash folder for all crash reports create by this session.
		FConfigFile CrashConfigFile;

		FConfigSection CRCConfigSectionCopy(*CRCConfigSection);
		CrashConfigFile.Add(ConfigSectionName, CRCConfigSectionCopy);

		CrashConfigFile.Dirty = true;
		CrashConfigFile.Write(GetCrashConfigFilePath());
	}

	// Read the initial un-localized crash context text
	UpdateLocalizedStrings();

	// Make sure we get updated text once the localized version is loaded
	FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddStatic(&UpdateLocalizedStrings);
#endif	// !NOINITCRASHREPORTER
}

void FGenericCrashContext::UpdateLocalizedStrings()
{
#if !NOINITCRASHREPORTER
	// Allow overriding the crash text
	FText CrashReportClientRichText;
	if (GConfig->GetText(TEXT("CrashContextProperties"), TEXT("CrashReportClientRichText"), CrashReportClientRichText, GEngineIni))
	{
		NCachedCrashContextProperties::CrashReportClientRichText = CrashReportClientRichText.ToString();
	}
#endif
}

FGenericCrashContext::FGenericCrashContext(ECrashContextType InType, const TCHAR* InErrorMessage)
	: Type(InType)
	, ErrorMessage(InErrorMessage)
	, NumMinidumpFramesToIgnore(0)
{
	CommonBuffer.Reserve( 32768 );
	CrashContextIndex = StaticCrashContextIndex++;
}

void FGenericCrashContext::SerializeContentToBuffer() const
{
	TCHAR CrashGUID[CrashGUIDLength];
	GetUniqueCrashName(CrashGUID, CrashGUIDLength);

	// Must conform against:
	// https://www.securecoding.cert.org/confluence/display/seccode/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers
	AddHeader();

	BeginSection( *RuntimePropertiesTag );
	AddCrashProperty( TEXT( "CrashVersion" ), (int32)ECrashDescVersions::VER_3_CrashContext );
	AddCrashProperty( TEXT( "ExecutionGuid" ), *ExecutionGuid.ToString() );
	AddCrashProperty( TEXT( "CrashGUID" ), (const TCHAR*)CrashGUID);
	AddCrashProperty( TEXT( "ProcessId" ), FPlatformProcess::GetCurrentProcessId() );
	AddCrashProperty( TEXT( "IsInternalBuild" ), NCachedCrashContextProperties::bIsInternalBuild );
	AddCrashProperty( TEXT( "IsPerforceBuild" ), NCachedCrashContextProperties::bIsPerforceBuild );
	AddCrashProperty( TEXT( "IsSourceDistribution" ), NCachedCrashContextProperties::bIsSourceDistribution );
	AddCrashProperty( TEXT( "IsEnsure" ), (Type == ECrashContextType::Ensure) );
	AddCrashProperty( TEXT( "IsAssert" ), (Type == ECrashContextType::Assert) );
	AddCrashProperty( TEXT( "CrashType" ), GetCrashTypeString(Type) );

	AddCrashProperty( TEXT( "SecondsSinceStart" ), NCachedCrashContextProperties::SecondsSinceStart );

	// Add common crash properties.
	if (NCachedCrashContextProperties::GameName.Len() > 0)
	{
		AddCrashProperty(TEXT("GameName"), *NCachedCrashContextProperties::GameName);
	}
	else
	{
		const TCHAR* ProjectName = FApp::GetProjectName();
		if (ProjectName != nullptr && ProjectName[0] != 0)
		{
			AddCrashProperty(TEXT("GameName"), *FString::Printf(TEXT("UE4-%s"), ProjectName));
		}
		else
		{
			AddCrashProperty(TEXT("GameName"), TEXT(""));
		}
	}
	AddCrashProperty( TEXT( "ExecutableName" ), *NCachedCrashContextProperties::ExecutableName );
	AddCrashProperty( TEXT( "BuildConfiguration" ), EBuildConfigurations::ToString( FApp::GetBuildConfiguration() ) );
	AddCrashProperty( TEXT( "GameSessionID" ), *NCachedCrashContextProperties::GameSessionID );
	
	// Unique string specifying the symbols to be used by CrashReporter
	FString Symbols = FString::Printf( TEXT( "%s-%s-%s" ), FApp::GetBuildVersion(), FPlatformMisc::GetUBTPlatform(), EBuildConfigurations::ToString(FApp::GetBuildConfiguration())).Replace( TEXT( "+" ), TEXT( "*" ));
#ifdef UE_BUILD_FLAVOR
	Symbols = FString::Printf(TEXT( "%s-%s" ), *Symbols, *FString(UE_BUILD_FLAVOR));
#endif

	AddCrashProperty( TEXT( "Symbols" ), Symbols);

	AddCrashProperty( TEXT( "PlatformName" ), FPlatformProperties::PlatformName() );
	AddCrashProperty( TEXT( "PlatformNameIni" ), FPlatformProperties::IniPlatformName());
	AddCrashProperty( TEXT( "EngineMode" ), FPlatformMisc::GetEngineMode() );
	AddCrashProperty( TEXT( "EngineModeEx" ), EngineModeExString());

	AddCrashProperty( TEXT( "DeploymentName"), *NCachedCrashContextProperties::DeploymentName );

	AddCrashProperty( TEXT( "EngineVersion" ), *FEngineVersion::Current().ToString() );
	AddCrashProperty( TEXT( "CommandLine" ), *NCachedCrashContextProperties::CommandLine );
	AddCrashProperty( TEXT( "LanguageLCID" ), NCachedCrashContextProperties::LanguageLCID );
	AddCrashProperty( TEXT( "AppDefaultLocale" ), *NCachedCrashContextProperties::DefaultLocale );
	AddCrashProperty( TEXT( "BuildVersion" ), FApp::GetBuildVersion() );
	AddCrashProperty( TEXT( "IsUE4Release" ), NCachedCrashContextProperties::bIsUE4Release );

	// Remove periods from user names to match AutoReporter user names
	// The name prefix is read by CrashRepository.AddNewCrash in the website code
	const bool bSendUserName = NCachedCrashContextProperties::bIsInternalBuild;
	AddCrashProperty( TEXT( "UserName" ), bSendUserName ? *NCachedCrashContextProperties::UserName.Replace( TEXT( "." ), TEXT( "" ) ) : TEXT( "" ) );

	AddCrashProperty( TEXT( "BaseDir" ), *NCachedCrashContextProperties::BaseDir );
	AddCrashProperty( TEXT( "RootDir" ), *NCachedCrashContextProperties::RootDir );
	AddCrashProperty( TEXT( "MachineId" ), *NCachedCrashContextProperties::LoginIdStr.ToUpper() );
	AddCrashProperty( TEXT( "LoginId" ), *NCachedCrashContextProperties::LoginIdStr );
	AddCrashProperty( TEXT( "EpicAccountId" ), *NCachedCrashContextProperties::EpicAccountId );

	// Legacy callstack element for current crash reporter
	AddCrashProperty( TEXT( "NumMinidumpFramesToIgnore"), NumMinidumpFramesToIgnore );
	AddCrashProperty( TEXT( "CallStack" ), TEXT("") );

	// Add new portable callstack element with crash stack
	AddPortableCallStack();
	AddPortableCallStackHash();

	AddCrashProperty( TEXT( "SourceContext" ), TEXT( "" ) );
	AddCrashProperty( TEXT( "UserDescription" ), TEXT( "" ) );
	AddCrashProperty( TEXT( "UserActivityHint" ), *NCachedCrashContextProperties::UserActivityHint );
	AddCrashProperty( TEXT( "ErrorMessage" ), ErrorMessage );
	AddCrashProperty( TEXT( "CrashDumpMode" ), NCachedCrashContextProperties::CrashDumpMode );
	AddCrashProperty( TEXT( "CrashReporterMessage" ), *NCachedCrashContextProperties::CrashReportClientRichText );

	// Add misc stats.
	AddCrashProperty( TEXT( "Misc.NumberOfCores" ), NCachedCrashContextProperties::NumberOfCores );
	AddCrashProperty( TEXT( "Misc.NumberOfCoresIncludingHyperthreads" ), NCachedCrashContextProperties::NumberOfCoresIncludingHyperthreads );
	AddCrashProperty( TEXT( "Misc.Is64bitOperatingSystem" ), (int32)FPlatformMisc::Is64bitOperatingSystem() );

	AddCrashProperty( TEXT( "Misc.CPUVendor" ), *NCachedCrashContextProperties::CPUVendor );
	AddCrashProperty( TEXT( "Misc.CPUBrand" ), *NCachedCrashContextProperties::CPUBrand );
	AddCrashProperty( TEXT( "Misc.PrimaryGPUBrand" ), *NCachedCrashContextProperties::PrimaryGPUBrand );
	AddCrashProperty( TEXT( "Misc.OSVersionMajor" ), *NCachedCrashContextProperties::OsVersion );
	AddCrashProperty( TEXT( "Misc.OSVersionMinor" ), *NCachedCrashContextProperties::OsSubVersion );

	AddCrashProperty(TEXT("GameStateName"), *NCachedCrashContextProperties::GameStateName);

	// #CrashReport: 2015-07-21 Move to the crash report client.
	/*{
		uint64 AppDiskTotalNumberOfBytes = 0;
		uint64 AppDiskNumberOfFreeBytes = 0;
		FPlatformMisc::GetDiskTotalAndFreeSpace( FPlatformProcess::BaseDir(), AppDiskTotalNumberOfBytes, AppDiskNumberOfFreeBytes );
		AddCrashProperty( TEXT( "Misc.AppDiskTotalNumberOfBytes" ), AppDiskTotalNumberOfBytes );
		AddCrashProperty( TEXT( "Misc.AppDiskNumberOfFreeBytes" ), AppDiskNumberOfFreeBytes );
	}*/

	// FPlatformMemory::GetConstants is called in the GCreateMalloc, so we can assume it is always valid.
	{
		// Add memory stats.
		const FPlatformMemoryConstants& MemConstants = FPlatformMemory::GetConstants();

		AddCrashProperty( TEXT( "MemoryStats.TotalPhysical" ), (uint64)MemConstants.TotalPhysical );
		AddCrashProperty( TEXT( "MemoryStats.TotalVirtual" ), (uint64)MemConstants.TotalVirtual );
		AddCrashProperty( TEXT( "MemoryStats.PageSize" ), (uint64)MemConstants.PageSize );
		AddCrashProperty( TEXT( "MemoryStats.TotalPhysicalGB" ), MemConstants.TotalPhysicalGB );
	}

	AddCrashProperty( TEXT( "MemoryStats.AvailablePhysical" ), (uint64)CrashMemoryStats.AvailablePhysical );
	AddCrashProperty( TEXT( "MemoryStats.AvailableVirtual" ), (uint64)CrashMemoryStats.AvailableVirtual );
	AddCrashProperty( TEXT( "MemoryStats.UsedPhysical" ), (uint64)CrashMemoryStats.UsedPhysical );
	AddCrashProperty( TEXT( "MemoryStats.PeakUsedPhysical" ), (uint64)CrashMemoryStats.PeakUsedPhysical );
	AddCrashProperty( TEXT( "MemoryStats.UsedVirtual" ), (uint64)CrashMemoryStats.UsedVirtual );
	AddCrashProperty( TEXT( "MemoryStats.PeakUsedVirtual" ), (uint64)CrashMemoryStats.PeakUsedVirtual );
	AddCrashProperty( TEXT( "MemoryStats.bIsOOM" ), (int32)FPlatformMemory::bIsOOM );
	AddCrashProperty( TEXT( "MemoryStats.OOMAllocationSize"), (uint64)FPlatformMemory::OOMAllocationSize );
	AddCrashProperty( TEXT( "MemoryStats.OOMAllocationAlignment"), (int32)FPlatformMemory::OOMAllocationAlignment );

	{
		FString AllThreadStacks;
		if (GetPlatformAllThreadContextsString(AllThreadStacks))
		{
			CommonBuffer += TEXT("<Threads>");
			CommonBuffer += AllThreadStacks;
			CommonBuffer += TEXT("</Threads>");
			CommonBuffer += LINE_TERMINATOR;
		}
	}

	EndSection( *RuntimePropertiesTag );

	// Add platform specific properties.
	BeginSection( *PlatformPropertiesTag );
	AddPlatformSpecificProperties();
	EndSection( *PlatformPropertiesTag );

	// Add the engine data
	BeginSection( *EngineDataTag );
	for (const TPair<FString, FString>& Pair : NCachedCrashContextProperties::EngineData)
	{
		AddCrashProperty(*Pair.Key, *Pair.Value);
	}
	EndSection( *EngineDataTag );

	// Add the game data
	BeginSection( *GameDataTag );
	for (const TPair<FString, FString>& Pair : NCachedCrashContextProperties::GameData)
	{
		AddCrashProperty(*Pair.Key, *Pair.Value);
	}
	EndSection( *GameDataTag );

	// Writing out the list of plugin JSON descriptors causes us to run out of memory
	// in GMallocCrash on console, so enable this only for desktop platforms.
#if PLATFORM_DESKTOP
	if(NCachedCrashContextProperties::EnabledPluginsList.Num() > 0)
	{
		BeginSection(*EnabledPluginsTag);

		for (const FString& Str : NCachedCrashContextProperties::EnabledPluginsList)
		{
			AddCrashProperty(TEXT("Plugin"), *Str);
		}

		EndSection(*EnabledPluginsTag);
	}
#endif // PLATFORM_DESKTOP

	AddFooter();
}

void FGenericCrashContext::SetNumMinidumpFramesToIgnore(int InNumMinidumpFramesToIgnore)
{
	NumMinidumpFramesToIgnore = InNumMinidumpFramesToIgnore;
}

void FGenericCrashContext::SetDeploymentName(const FString& EpicApp)
{
	NCachedCrashContextProperties::DeploymentName = EpicApp;
}

void FGenericCrashContext::GetUniqueCrashName(TCHAR* GUIDBuffer, int32 BufferSize) const
{
	FCString::Snprintf(GUIDBuffer, BufferSize, TEXT("%s_%04i"), *NCachedCrashContextProperties::CrashGUIDRoot, CrashContextIndex);
}

const bool FGenericCrashContext::IsFullCrashDump() const
{
	if(Type == ECrashContextType::Ensure)
	{
		return (NCachedCrashContextProperties::CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
	else
	{
		return (NCachedCrashContextProperties::CrashDumpMode == (int32)ECrashDumpMode::FullDump) ||
			(NCachedCrashContextProperties::CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
}

void FGenericCrashContext::SerializeAsXML( const TCHAR* Filename ) const
{
	SerializeContentToBuffer();
	// Use OS build-in functionality instead.
	FFileHelper::SaveStringToFile( CommonBuffer, Filename, FFileHelper::EEncodingOptions::AutoDetect );
}

void FGenericCrashContext::AddCrashProperty( const TCHAR* PropertyName, const TCHAR* PropertyValue ) const
{
	CommonBuffer += TEXT( "<" );
	CommonBuffer += PropertyName;
	CommonBuffer += TEXT( ">" );


	AppendEscapedXMLString(CommonBuffer, PropertyValue );

	CommonBuffer += TEXT( "</" );
	CommonBuffer += PropertyName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AddPlatformSpecificProperties() const
{
	// Nothing really to do here. Can be overridden by the platform code.
	// @see FWindowsPlatformCrashContext::AddPlatformSpecificProperties
}

void FGenericCrashContext::AddPortableCallStackHash() const
{
	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStackHash"), TEXT(""));
		return;
	}

	// This may allocate if its the first time calling into this function
	const TCHAR* ExeName = FPlatformProcess::ExecutableName();

	// We dont want this to be thrown into an FString as it will alloc memory
	const TCHAR* UE4EditorName = TEXT("UE4Editor");

	FSHA1 Sha;
	FSHAHash Hash;

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		// If we are our own module or our module contains UE4Editor we assume we own these. We cannot depend on offsets of system libs
		// as they may have different versions
		if (It->ModuleName == ExeName || It->ModuleName.Contains(UE4EditorName))
		{
			Sha.Update(reinterpret_cast<const uint8*>(&It->Offset), sizeof(It->Offset));
		}
	}

	Sha.Final();
	Sha.GetHash(Hash.Hash);

	FString EscapedPortableHash;

	// Allocations here on both the ToString and AppendEscapedXMLString it self adds to the out FString
	AppendEscapedXMLString(EscapedPortableHash, *Hash.ToString());

	AddCrashProperty(TEXT("PCallStackHash"), *EscapedPortableHash);
}

void FGenericCrashContext::AddPortableCallStack() const
{	

	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStack"), TEXT(""));
		return;
	}

	FString CrashStackBuffer = LINE_TERMINATOR;

	// Get the max module name length for padding
	int32 MaxModuleLength = 0;
	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		MaxModuleLength = FMath::Max(MaxModuleLength, It->ModuleName.Len());
	}

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		CrashStackBuffer += FString::Printf(TEXT("%-*s 0x%016x + %-8x"),MaxModuleLength + 1, *It->ModuleName, It->BaseAddress, It->Offset);
		CrashStackBuffer += LINE_TERMINATOR;
	}

	FString EscapedStackBuffer;

	AppendEscapedXMLString(EscapedStackBuffer, *CrashStackBuffer);

	AddCrashProperty(TEXT("PCallStack"), *EscapedStackBuffer);
}

void FGenericCrashContext::AddHeader() const
{
	CommonBuffer += TEXT( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" ) LINE_TERMINATOR;
	BeginSection( TEXT("FGenericCrashContext") );
}

void FGenericCrashContext::AddFooter() const
{
	EndSection( TEXT( "FGenericCrashContext" ) );
}

void FGenericCrashContext::BeginSection( const TCHAR* SectionName ) const
{
	CommonBuffer += TEXT( "<" );
	CommonBuffer += SectionName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::EndSection( const TCHAR* SectionName ) const
{
	CommonBuffer += TEXT( "</" );
	CommonBuffer += SectionName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AppendEscapedXMLString(FString& OutBuffer, const TCHAR* Text)
{
	if (!Text)
	{
		return;
	}

	while (*Text)
	{
		switch (*Text)
		{
		case TCHAR('&'):
			OutBuffer += TEXT("&amp;");
			break;
		case TCHAR('"'):
			OutBuffer += TEXT("&quot;");
			break;
		case TCHAR('\''):
			OutBuffer += TEXT("&apos;");
			break;
		case TCHAR('<'):
			OutBuffer += TEXT("&lt;");
			break;
		case TCHAR('>'):
			OutBuffer += TEXT("&gt;");
			break;
		case TCHAR('\r'):
			break;
		default:
			OutBuffer += *Text;
		};

		Text++;
	}
}

FString FGenericCrashContext::UnescapeXMLString( const FString& Text )
{
	return Text
		.Replace(TEXT("&amp;"), TEXT("&"))
		.Replace(TEXT("&quot;"), TEXT("\""))
		.Replace(TEXT("&apos;"), TEXT("'"))
		.Replace(TEXT("&lt;"), TEXT("<"))
		.Replace(TEXT("&gt;"), TEXT(">"));
}

FString FGenericCrashContext::GetCrashGameName()
{
	return NCachedCrashContextProperties::GameName;
}

const TCHAR* FGenericCrashContext::GetCrashTypeString(ECrashContextType Type)
{
	switch (Type)
	{
	case ECrashContextType::Hang:
		return *CrashTypeHang;
	case ECrashContextType::GPUCrash:
		return *CrashTypeGPU;
	case ECrashContextType::Ensure:
		return *CrashTypeEnsure;
	case ECrashContextType::Assert:
		return *CrashTypeAssert;
	default:
		return *CrashTypeCrash;
	}
}

const TCHAR* FGenericCrashContext::EngineModeExString()
{
	return !NCachedCrashContextProperties::bIsVanilla.IsSet() ? *FGenericCrashContext::EngineModeExUnknown :
		(NCachedCrashContextProperties::bIsVanilla.GetValue() ? *FGenericCrashContext::EngineModeExVanilla : *FGenericCrashContext::EngineModeExDirty);
}

const TCHAR* FGenericCrashContext::GetCrashConfigFilePath()
{
	static FString CrashConfigFilePath;
	if (CrashConfigFilePath.IsEmpty())
	{
		CrashConfigFilePath = FPaths::Combine(GetCrashConfigFolder(), *NCachedCrashContextProperties::CrashGUIDRoot, FGenericCrashContext::CrashConfigFileNameW);
	}
	return *CrashConfigFilePath;
}

const TCHAR* FGenericCrashContext::GetCrashConfigFolder()
{
	static FString CrashConfigFolder;
	if (CrashConfigFolder.IsEmpty())
	{
		CrashConfigFolder = FPaths::Combine(*FPaths::GeneratedConfigDir(), TEXT("CrashReportClient"));
	}
	return *CrashConfigFolder;
}

void FGenericCrashContext::PurgeOldCrashConfig()
{
	int32 PurgeDays = 2;
	GConfig->GetInt(*ConfigSectionName, *CrashConfigPurgeDays, PurgeDays, GEngineIni);

	if (PurgeDays > 0)
	{
		IFileManager& FileManager = IFileManager::Get();

		// Delete items older than PurgeDays
		TArray<FString> Directories;
		FileManager.FindFiles(Directories, *(FPaths::Combine(GetCrashConfigFolder(), *CrashGUIDRootPrefix) + TEXT("*")), false, true);

		for (const FString& Dir : Directories)
		{
			const FString CrashConfigDirectory = FPaths::Combine(GetCrashConfigFolder(), *Dir);
			const FDateTime DirectoryAccessTime = FileManager.GetTimeStamp(*CrashConfigDirectory);
			if (FDateTime::Now() - DirectoryAccessTime > FTimespan::FromDays(PurgeDays))
			{
				FileManager.DeleteDirectory(*CrashConfigDirectory, false, true);
			}
		}
	}
}

void FGenericCrashContext::ResetEngineData()
{
	NCachedCrashContextProperties::EngineData.Reset();
}

void FGenericCrashContext::SetEngineData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCachedCrashContextProperties::EngineData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCachedCrashContextProperties::EngineData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCachedCrashContextProperties::EngineData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::ResetGameData()
{
	NCachedCrashContextProperties::GameData.Reset();
}

void FGenericCrashContext::SetGameData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCachedCrashContextProperties::GameData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCachedCrashContextProperties::GameData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCachedCrashContextProperties::GameData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::AddPlugin(const FString& PluginDesc)
{
	NCachedCrashContextProperties::EnabledPluginsList.Add(PluginDesc);
}

FORCENOINLINE void FGenericCrashContext::CapturePortableCallStack(int32 NumStackFramesToIgnore, void* Context)
{
	// If the callstack is for the executing thread, ignore this function
	if(Context == nullptr)
	{
		NumStackFramesToIgnore++;
	}

	// Capture the stack trace
	static const int StackTraceMaxDepth = 100;
	uint64 StackTrace[StackTraceMaxDepth];
	FMemory::Memzero(StackTrace);
	int32 StackTraceDepth = FPlatformStackWalk::CaptureStackBackTrace(StackTrace, StackTraceMaxDepth, Context);

	// Make sure we don't exceed the current stack depth
	NumStackFramesToIgnore = FMath::Min(NumStackFramesToIgnore, StackTraceDepth);

	// Generate the portable callstack from it
	SetPortableCallStack(StackTrace + NumStackFramesToIgnore, StackTraceDepth - NumStackFramesToIgnore);
}

void FGenericCrashContext::SetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames)
{
	GetPortableCallStack(StackFrames, NumStackFrames, CallStack);
}

void FGenericCrashContext::GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack)
{
	// Get all the modules in the current process
	uint32 NumModules = (uint32)FPlatformStackWalk::GetProcessModuleCount();

	TArray<FStackWalkModuleInfo> Modules;
	Modules.AddUninitialized(NumModules);

	NumModules = FPlatformStackWalk::GetProcessModuleSignatures(Modules.GetData(), NumModules);
	Modules.SetNum(NumModules);

	// Update the callstack with offsets from each module
	OutCallStack.Reset(NumStackFrames);
	for(int32 Idx = 0; Idx < NumStackFrames; Idx++)
	{
		const uint64 StackFrame = StackFrames[Idx];

		// Try to find the module containing this stack frame
		const FStackWalkModuleInfo* FoundModule = nullptr;
		for(const FStackWalkModuleInfo& Module : Modules)
		{
			if(StackFrame >= Module.BaseOfImage && StackFrame < Module.BaseOfImage + Module.ImageSize)
			{
				FoundModule = &Module;
				break;
			}
		}

		// Add the callstack item
		if(FoundModule == nullptr)
		{
			OutCallStack.Add(FCrashStackFrame(TEXT("Unknown"), 0, StackFrame));
		}
		else
		{
			OutCallStack.Add(FCrashStackFrame(FPaths::GetBaseFilename(FoundModule->ImageName), FoundModule->BaseOfImage, StackFrame - FoundModule->BaseOfImage));
		}
	}
}

FProgramCounterSymbolInfoEx::FProgramCounterSymbolInfoEx( FString InModuleName, FString InFunctionName, FString InFilename, uint32 InLineNumber, uint64 InSymbolDisplacement, uint64 InOffsetInModule, uint64 InProgramCounter ) :
	ModuleName( InModuleName ),
	FunctionName( InFunctionName ),
	Filename( InFilename ),
	LineNumber( InLineNumber ),
	SymbolDisplacement( InSymbolDisplacement ),
	OffsetInModule( InOffsetInModule ),
	ProgramCounter( InProgramCounter )
{

}
