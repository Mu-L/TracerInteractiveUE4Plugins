// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Stats/Stats.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MonitoredProcess.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IAudioFormat.h"
#include "Interfaces/IAudioFormatModule.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "PlatformInfo.h"
#include "DesktopPlatformModule.h"

#if PHYSICS_INTERFACE_PHYSX
#include "IPhysXCooking.h"
#include "IPhysXCookingModule.h"
#endif // PHYSICS_INTERFACE_PHYSX

DEFINE_LOG_CATEGORY_STATIC(LogTargetPlatformManager, Log, All);

#define AUTOSDKS_ENABLED (WITH_UNREAL_DEVELOPER_TOOLS || !IS_MONOLITHIC) && PLATFORM_WINDOWS

static const size_t MaxPlatformCount = 64;		// In the unlikely event that someone bumps this please note that there's
												// an implicit assumption that there won't be more than 64 unique target
												// platforms in the FTargetPlatformSet code since it uses one bit of an
												// uint64 per platform.

static const ITargetPlatform* TargetPlatformArray[MaxPlatformCount];

static int32 PlatformCounter = 0;

int32 
ITargetPlatform::AssignPlatformOrdinal(const ITargetPlatform& Platform)
{
	check(PlatformCounter < MaxPlatformCount);

	const int32 Ordinal = PlatformCounter++;

	check(TargetPlatformArray[Ordinal] == nullptr);
	TargetPlatformArray[Ordinal] = &Platform;

	return Ordinal;
}

const ITargetPlatform* 
ITargetPlatform::GetPlatformFromOrdinal(int32 Ordinal)
{
	check(Ordinal < PlatformCounter);

	return TargetPlatformArray[Ordinal];
}


/**
 * Module for the target platform manager
 */
class FTargetPlatformManagerModule
	: public ITargetPlatformManagerModule
{
public:

	/** Default constructor. */
	FTargetPlatformManagerModule()
		: bRestrictFormatsToRuntimeOnly(false)
		, bForceCacheUpdate(true)
		, bHasInitErrors(false)
		, bIgnoreFirstDelegateCall(true)
	{
#if AUTOSDKS_ENABLED		
		
		// AutoSDKs only enabled if UE_SDKS_ROOT is set.
		if (IsAutoSDKsEnabled())
		{					
			DECLARE_SCOPE_CYCLE_COUNTER( TEXT( "FTargetPlatformManagerModule.StartAutoSDK" ), STAT_FTargetPlatformManagerModule_StartAutoSDK, STATGROUP_TargetPlatform );

			// amortize UBT cost by calling it once for all platforms, rather than once per platform.
			if (FParse::Param(FCommandLine::Get(), TEXT("Multiprocess"))==false)
			{
				FString UBTParams(TEXT("-Mode=SetupPlatforms"));
				int32 UBTReturnCode = -1;
				FString UBTOutput;
				if (!FDesktopPlatformModule::Get()->InvokeUnrealBuildToolSync(UBTParams, *GLog, true, UBTReturnCode, UBTOutput))
				{
					UE_LOG(LogTargetPlatformManager, Fatal, TEXT("Failed to run UBT to check SDK status!"));
				}
			}

			// we have to setup our local environment according to AutoSDKs or the ITargetPlatform's IsSDkInstalled calls may fail
			// before we get a change to setup for a given platform.  Use the platforminfo list to avoid any kind of interdependency.
			for (const PlatformInfo::FPlatformInfo& PlatformInfo : PlatformInfo::GetPlatformInfoArray())
			{
				SetupAndValidateAutoSDK(PlatformInfo.AutoSDKPath);
			}
		}
#endif
		// Calling a virtual function from a constructor, but with no expectation that a derived implementation of this
		// method would be called.  This is solely to avoid duplicating code in this implementation, not for polymorphism.
		FTargetPlatformManagerModule::Invalidate();

		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FTargetPlatformManagerModule::ModulesChangesCallback);
	}

	/** Destructor. */
	virtual ~FTargetPlatformManagerModule()
	{
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}

public:

	// ITargetPlatformManagerModule interface

	virtual bool HasInitErrors(FString* OutErrorMessages) const
	{
		if (OutErrorMessages)
		{
			*OutErrorMessages = InitErrorMessages;
		}
		return bHasInitErrors;
	}

	virtual void Invalidate() override
	{
		bForceCacheUpdate = true;

		SetupSDKStatus();
		//GetTargetPlatforms(); redudant with next call
		GetActiveTargetPlatforms();

		// If we've had an error due to an invalid target platform, don't do additional work
		if (!bHasInitErrors)
		{
			GetAudioFormats();
			GetTextureFormats();
			GetShaderFormats();
		}

		bForceCacheUpdate = false;
		OnTargetPlatformsInvalidated.Broadcast();
	}

	virtual FOnTargetPlatformsInvalidated& GetOnTargetPlatformsInvalidatedDelegate() override
	{
		return OnTargetPlatformsInvalidated;
	}

	virtual const TArray<ITargetPlatform*>& GetTargetPlatforms() override
	{
		if (Platforms.Num() == 0 || bForceCacheUpdate)
		{
			DiscoverAvailablePlatforms();
		}

		return Platforms;
	}

	virtual ITargetDevicePtr FindTargetDevice(const FTargetDeviceId& DeviceId) override
	{
		ITargetPlatform* Platform = FindTargetPlatform(DeviceId.GetPlatformName());

		if (Platform != nullptr)
		{
			return Platform->GetDevice(DeviceId);
		}

		return nullptr;
	}

	virtual ITargetPlatform* FindTargetPlatform(FStringView Name) override
	{
		GetTargetPlatforms(); // Populates PlatformsByName

		if (ITargetPlatform** Platform = PlatformsByName.Find(FName(Name)))
		{
			return *Platform;
		}

		return nullptr;
	}

	virtual ITargetPlatform* FindTargetPlatformWithSupport(FName SupportType, FName RequiredSupportedValue)
	{
		const TArray<ITargetPlatform*>& TargetPlatforms = GetTargetPlatforms();

		for (int32 Index = 0; Index < TargetPlatforms.Num(); Index++)
		{
			//@todo-lh:
			// FAllDesktopPlatformProperties will be removed soon as it's no longer maintained
			// and will be replaced by the platform specific subclasses eventually, so skip "AllDesktop.
			// Find platform specific subclass instead.
			if (TargetPlatforms[Index]->PlatformName() == TEXT("AllDesktop"))
			{
				continue;
			}
			if (TargetPlatforms[Index]->SupportsValueForType(SupportType, RequiredSupportedValue))
			{
				return TargetPlatforms[Index];
			}
		}

		return nullptr;
	}

	virtual const TArray<ITargetPlatform*>& GetCookingTargetPlatforms() override
	{
		static bool bInitialized = false;
		static TArray<ITargetPlatform*> Results;

		if ( !bInitialized || bForceCacheUpdate )
		{
			Results = GetActiveTargetPlatforms();

			FString PlatformStr;
			if (FParse::Value(FCommandLine::Get(), TEXT("TARGETPLATFORM="), PlatformStr))
			{
				if (PlatformStr == TEXT("None"))
				{
					Results = Platforms;
				}
			}
			else
			{
				Results = Platforms;
			}
		}

		return Results;
	}

	virtual const TArray<ITargetPlatform*>& GetActiveTargetPlatforms() override
	{
		static bool bInitialized = false;
		static TArray<ITargetPlatform*> Results;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			bHasInitErrors = false; // If we had errors before, reset the flag and see later in this function if there are errors.
			InitErrorMessages.Empty();

			Results.Empty(Results.Num());

			const TArray<ITargetPlatform*>& TargetPlatforms = GetTargetPlatforms();	

			FString PlatformStr;

			if (FParse::Value(FCommandLine::Get(), TEXT("TARGETPLATFORM="), PlatformStr))
			{
				if (PlatformStr == TEXT("None"))
				{
				}
				else if (PlatformStr == TEXT("All"))
				{
					Results = TargetPlatforms;
				}
				else
				{
					TArray<FString> PlatformNames;

					PlatformStr.ParseIntoArray(PlatformNames, TEXT("+"), true);

					// for nicer user response
					FString AvailablePlatforms;

					for (int32 Index = 0; Index < TargetPlatforms.Num(); Index++)
					{
						if (PlatformNames.Contains(TargetPlatforms[Index]->PlatformName()))
						{							
							Results.Add(TargetPlatforms[Index]);						
						}

						if(!AvailablePlatforms.IsEmpty())
						{
							AvailablePlatforms += TEXT(", ");
						}
						AvailablePlatforms += TargetPlatforms[Index]->PlatformName();
					}

					if (Results.Num() == 0)
					{
						// An invalid platform was specified...
						// Inform the user.
						bHasInitErrors = true;
						InitErrorMessages.Appendf(TEXT("Invalid target platform specified (%s). Available = { %s } "), *PlatformStr, *AvailablePlatforms);
						UE_LOG(LogTargetPlatformManager, Error, TEXT("Invalid target platform specified (%s). Available = { %s } "), *PlatformStr, *AvailablePlatforms);
						return Results;
					}
				}
			}
			else
			{
				// if there is no argument, use the current platform and only build formats that are actually needed to run.
				bRestrictFormatsToRuntimeOnly = true;

				for (int32 Index = 0; Index < TargetPlatforms.Num(); Index++)
				{
					if (TargetPlatforms[Index]->IsRunningPlatform())
					{						
						Results.Add(TargetPlatforms[Index]);					
					}
				}
			}

			if (!Results.Num())
			{
				UE_LOG(LogTargetPlatformManager, Display, TEXT("Not building assets for any platform."));
			}
			else
			{
				for (int32 Index = 0; Index < Results.Num(); Index++)
				{
					UE_LOG(LogTargetPlatformManager, Display, TEXT("Building Assets For %s"), *Results[Index]->PlatformName());
				}
			}
		}

		return Results;
	}

	virtual bool RestrictFormatsToRuntimeOnly() override
	{
		GetActiveTargetPlatforms(); // make sure this is initialized

		return bRestrictFormatsToRuntimeOnly;
	}

	virtual ITargetPlatform* GetRunningTargetPlatform() override
	{
		static bool bInitialized = false;
		static ITargetPlatform* Result = nullptr;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Result = nullptr;

			const TArray<ITargetPlatform*>& TargetPlatforms = GetTargetPlatforms();	

			for (int32 Index = 0; Index < TargetPlatforms.Num(); Index++)
			{
				if (TargetPlatforms[Index]->IsRunningPlatform())
				{
					 // we should not have two running platforms
					checkf((Result == nullptr),
						TEXT("Found multiple running platforms.\n\t%s\nand\n\t%s"),
						*Result->PlatformName(),
						*TargetPlatforms[Index]->PlatformName()
						);
					Result = TargetPlatforms[Index];
				}
			}
		}

		return Result;
	}

	virtual const TArray<const IAudioFormat*>& GetAudioFormats() override
	{
		static bool bInitialized = false;
		static TArray<const IAudioFormat*> Results;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Results.Empty(Results.Num());

			TArray<FName> Modules;

			FModuleManager::Get().FindModules(TEXT("*AudioFormat*"), Modules);

			if (!Modules.Num())
			{
				UE_LOG(LogTargetPlatformManager, Error, TEXT("No target audio formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				IAudioFormatModule* Module = FModuleManager::LoadModulePtr<IAudioFormatModule>(Modules[Index]);
				if (Module)
				{
					IAudioFormat* Format = Module->GetAudioFormat();
					if (Format != nullptr)
					{
						Results.Add(Format);
					}
				}
			}
		}

		return Results;
	}

	virtual const IAudioFormat* FindAudioFormat(FName Name) override
	{
		const TArray<const IAudioFormat*>& AudioFormats = GetAudioFormats();

		for (int32 Index = 0; Index < AudioFormats.Num(); Index++)
		{
			TArray<FName> Formats;

			AudioFormats[Index]->GetSupportedFormats(Formats);

			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					return AudioFormats[Index];
				}
			}
		}

		return nullptr;
	}

	virtual const TArray<const ITextureFormat*>& GetTextureFormats() override
	{
		static bool bInitialized = false;
		static TArray<const ITextureFormat*> Results;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Results.Empty(Results.Num());

			TArray<FName> Modules;

			FModuleManager::Get().FindModules(TEXT("*TextureFormat*"), Modules);

			if (!Modules.Num())
			{
				UE_LOG(LogTargetPlatformManager, Error, TEXT("No target texture formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				ITextureFormatModule* Module = FModuleManager::LoadModulePtr<ITextureFormatModule>(Modules[Index]);
				if (Module)
				{
					ITextureFormat* Format = Module->GetTextureFormat();
					if (Format != nullptr)
					{
						Results.Add(Format);
					}
				}
			}
		}

		return Results;
	}

	virtual const ITextureFormat* FindTextureFormat(FName Name) override
	{
		const TArray<const ITextureFormat*>& TextureFormats = GetTextureFormats();

		for (int32 Index = 0; Index < TextureFormats.Num(); Index++)
		{
			TArray<FName> Formats;

			TextureFormats[Index]->GetSupportedFormats(Formats);

			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					return TextureFormats[Index];
				}
			}
		}

		return nullptr;
	}

	virtual const TArray<const IShaderFormat*>& GetShaderFormats() override
	{
		static bool bInitialized = false;
		static TArray<const IShaderFormat*> Results;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Results.Empty(Results.Num());

			TArray<FName> Modules;

			FModuleManager::Get().FindModules(SHADERFORMAT_MODULE_WILDCARD, Modules);

			if (!Modules.Num())
			{
				UE_LOG(LogTargetPlatformManager, Error, TEXT("No target shader formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				IShaderFormatModule* Module = FModuleManager::LoadModulePtr<IShaderFormatModule>(Modules[Index]);
				if (Module)
				{
					IShaderFormat* Format = Module->GetShaderFormat();
					if (Format != nullptr)
					{
						Results.Add(Format);
					}
				}
			}
		}
		return Results;
	}

	virtual const IShaderFormat* FindShaderFormat(FName Name) override
	{
		const TArray<const IShaderFormat*>& ShaderFormats = GetShaderFormats();	

		for (int32 Index = 0; Index < ShaderFormats.Num(); Index++)
		{
			TArray<FName> Formats;
			
			ShaderFormats[Index]->GetSupportedFormats(Formats);
		
			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					return ShaderFormats[Index];
				}
			}
		}

		return nullptr;
	}

	virtual uint32 ShaderFormatVersion(FName Name) override
	{
		static TMap<FName, uint32> AlreadyFound;
		uint32* Result = AlreadyFound.Find(Name);

		if (!Result)
		{
			const IShaderFormat* SF = FindShaderFormat(Name);

			if (SF)
			{
				Result = &AlreadyFound.Add(Name, SF->GetVersion(Name));
			}
		}

		if (!Result)
		{
			UE_LOG(LogTargetPlatformManager, Fatal, TEXT("No ShaderFormat found for %s!"), *Name.ToString());
		}

		return *Result;
	}

	virtual const TArray<const IPhysXCooking*>& GetPhysXCooking() override
	{
		static bool bInitialized = false;
		static TArray<const IPhysXCooking*> Results;

#if PHYSICS_INTERFACE_PHYSX
		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Results.Empty(Results.Num());
			
			TArray<FName> Modules;
			FModuleManager::Get().FindModules(TEXT("PhysXCooking*"), Modules);
			
			if (!Modules.Num())
			{
				UE_LOG(LogTargetPlatformManager, Error, TEXT("No target PhysX formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				IPhysXCookingModule* Module = FModuleManager::LoadModulePtr<IPhysXCookingModule>(Modules[Index]);
				if (Module)
				{
					IPhysXCooking* Format = Module->GetPhysXCooking();
					if (Format != nullptr)
					{
						Results.Add(Format);
					}
				}
			}
		}
#endif // PHYSICS_INTERFACE_PHYSX

		return Results;
	}

	virtual const IPhysXCooking* FindPhysXCooking(FName Name) override
	{
#if PHYSICS_INTERFACE_PHYSX 
		const TArray<const IPhysXCooking*>& PhysXCooking = GetPhysXCooking();

		for (int32 Index = 0; Index < PhysXCooking.Num(); Index++)
		{
			TArray<FName> Formats;

			PhysXCooking[Index]->GetSupportedFormats(Formats);
		
			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					return PhysXCooking[Index];
				}
			}
		}
#endif // PHYSICS_INTERFACE_PHYSX

		return nullptr;
	}

protected:

	/**
	 * Checks whether AutoSDK is enabled.
	 *
	 * @return true if the SDK is enabled, false otherwise.
	 */
	bool IsAutoSDKsEnabled()
	{
		// AutoSDKs only enabled if UE_SDKS_ROOT is set.
		static const TCHAR* SDKRootEnvVar = TEXT("UE_SDKS_ROOT");
		return !FPlatformMisc::GetEnvironmentVariable(SDKRootEnvVar).IsEmpty();
	}

	/** Discovers the available target platforms. */
	void DiscoverAvailablePlatforms()
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FTargetPlatformManagerModule::DiscoverAvailablePlatforms"), STAT_FTargetPlatformManagerModule_DiscoverAvailablePlatforms, STATGROUP_TargetPlatform);

		Platforms.Empty(Platforms.Num());
		PlatformsByName.Empty(PlatformsByName.Num());

#if !IS_MONOLITHIC
		// Find all module subdirectories and add them so we can load dependent modules for target platform modules
		// We may not be able to restrict this to subdirectories found in FPlatformInfo because we could have a subdirectory
		// that is not one of these platforms. Imagine a "Sega" shared directory for the "Genesis" and "Dreamcast" platforms
		TArray<FString> ModuleSubdirs;
		IFileManager::Get().FindFilesRecursive(ModuleSubdirs, *FPlatformProcess::GetModulesDirectory(), TEXT("*"), false, true);
		for (const FString& ModuleSubdir : ModuleSubdirs)
		{
			FModuleManager::Get().AddBinariesDirectory(*ModuleSubdir, false);
		}
#endif

		// find a set of valid target platform names (the platform DataDrivenPlatformInfo.ini file was found indicates support for the platform 
		// exists on disk, so the TP is expected to work)
		const TArray<PlatformInfo::FPlatformInfo>& PlatformInfos = PlatformInfo::GetPlatformInfoArray();

		TSet<ITargetPlatformModule*> ProcessedModules;
		FScopedSlowTask SlowTask(PlatformInfos.Num());
		for (const PlatformInfo::FPlatformInfo& PlatInfo : PlatformInfos)
		{
			SlowTask.EnterProgressFrame(1);

			// by defaulty load all PIs we can have
			bool bLoadTargetPlatform = true;

			// disabled?
			if (PlatInfo.bEnabledForUse == false)
			{
				bLoadTargetPlatform = false;
			}

#if WITH_EDITOR
			// if we have the editor and we are using -game
			// only need to instantiate the current platform 
			if (IsRunningGame())
			{
				if (PlatInfo.IniPlatformName != FPlatformProperties::IniPlatformName())
				{
					bLoadTargetPlatform = false;
				}
			}
#endif

			// now load the TP module
			if (bLoadTargetPlatform)
			{
				// there are two ways targetplatform modules are setup: a single DLL per TargetPlatform, or a DLL for the platform
				// that returns multiple TargetPlatforms. we try single first, then full platform
				FName FullPlatformModuleName = *(PlatInfo.IniPlatformName + TEXT("TargetPlatform"));
				FName SingleTargetPlatformModuleName = *(PlatInfo.TargetPlatformName.ToString() + TEXT("TargetPlatform"));
				bool bFullPlatformModuleNameIsValid = !PlatInfo.IniPlatformName.IsEmpty();

				ITargetPlatformModule* Module = nullptr;
				
				if (FModuleManager::Get().ModuleExists(*SingleTargetPlatformModuleName.ToString()))
				{
					Module = FModuleManager::LoadModulePtr<ITargetPlatformModule>(SingleTargetPlatformModuleName);
				}
				else if (bFullPlatformModuleNameIsValid && FModuleManager::Get().ModuleExists(*FullPlatformModuleName.ToString()))
				{
					Module = FModuleManager::LoadModulePtr<ITargetPlatformModule>(FullPlatformModuleName);
				}

				// if we have already processed this module, we can skip it!
				if (ProcessedModules.Contains(Module))
				{
					continue;
				}

				// original logic for module loading here
				if (Module)
				{
					ProcessedModules.Add(Module);

					TArray<ITargetPlatform*> TargetPlatforms = Module->GetTargetPlatforms();
					for (ITargetPlatform* Platform : TargetPlatforms)
					{
						// would like to move this check to GetActiveTargetPlatforms, but too many things cache this result
						// this setup will become faster after TTP 341897 is complete.
					RETRY_SETUPANDVALIDATE:
						if (SetupAndValidateAutoSDK(Platform->GetPlatformInfo().AutoSDKPath))
						{
							const FString& PlatformName = Platform->PlatformName();
							UE_LOG(LogTargetPlatformManager, Display, TEXT("Loaded TargetPlatform '%s'"), *PlatformName);
							Platforms.Add(Platform);
							PlatformsByName.Add(FName(PlatformName), Platform);
						}
						else
						{
							// this hack is here because if you try and setup and validate autosdk some times it will fail because shared files are in use by another child cooker
							static bool bIsChildCooker = FParse::Param(FCommandLine::Get(), TEXT("cookchild"));
							if (bIsChildCooker)
							{
								static int Counter = 0;
								++Counter;
								if (Counter < 10)
								{
									goto RETRY_SETUPANDVALIDATE;
								}
							}
							UE_LOG(LogTargetPlatformManager, Display, TEXT("Failed to SetupAndValidateAutoSDK for platform '%s'"), *Platform->PlatformName());
						}
					}
				}
			}
		}

		if (!Platforms.Num())
		{
			UE_LOG(LogTargetPlatformManager, Error, TEXT("No target platforms found!"));
		}
	}

	bool UpdatePlatformEnvironment(const FString& PlatformName, TArray<FString> &Keys, TArray<FString> &Values) override
	{
		SetupEnvironmentVariables(Keys, Values);
		return SetupSDKStatus(PlatformName);	
	}

	bool SetupAndValidateAutoSDK(const FString& AutoSDKPath)
	{
#if AUTOSDKS_ENABLED
		bool bValidSDK = false;
		if (AutoSDKPath.Len() > 0)
		{		
			FName PlatformFName(*AutoSDKPath);

			// cache result of the last setup attempt to avoid calling UBT all the time.
			bool* bPreviousSetupSuccessful = PlatformsSetup.Find(PlatformFName);
			if (bPreviousSetupSuccessful)
			{
				bValidSDK = *bPreviousSetupSuccessful;
			}
			else
			{
				bValidSDK = SetupEnvironmentFromAutoSDK(AutoSDKPath);
				PlatformsSetup.Add(PlatformFName, bValidSDK);
			}
		}
		else
		{
			// if a platform has no AutoSDKPath, then just assume the SDK is installed, we have no basis for determining it.
			bValidSDK = true;
		}
		return bValidSDK;
#else
		return true;
#endif // AUTOSDKS_ENABLED
	}
	
	bool SetupEnvironmentFromAutoSDK(const FString& AutoSDKPath)
	{						
#if AUTOSDKS_ENABLED
		
		if (!IsAutoSDKsEnabled())
		{
			return true;
		}

		// Invoke UBT to perform SDK switching, or detect that a proper manual SDK is already setup.				
#if PLATFORM_WINDOWS
		FString HostPlatform(TEXT("HostWin64"));
#else
#error Fill in your host platform directory
#endif		

		static const FString SDKRootEnvFar(TEXT("UE_SDKS_ROOT"));
		FString SDKPath = FPlatformMisc::GetEnvironmentVariable(*SDKRootEnvFar);

		FString TargetSDKRoot = FPaths::Combine(*SDKPath, *HostPlatform, *AutoSDKPath);
		static const FString SDKInstallManifestFileName(TEXT("CurrentlyInstalled.txt"));
		FString SDKInstallManifestFilePath = FPaths::Combine(*TargetSDKRoot, *SDKInstallManifestFileName);

		// If we are using a manual install, then it is valid for there to be no OutputEnvVars file.
		TUniquePtr<FArchive> InstallManifestFile(IFileManager::Get().CreateFileReader(*SDKInstallManifestFilePath));
		if (InstallManifestFile)
		{
			TArray<FString> FileLines;
			int64 FileSize = InstallManifestFile->TotalSize();
			int64 MemSize = FileSize + 1;
			void* FileMem = FMemory::Malloc(MemSize);
			FMemory::Memset(FileMem, 0, MemSize);

			InstallManifestFile->Serialize(FileMem, FileSize);

			FString FileAsString(ANSI_TO_TCHAR(FileMem));
			FileAsString.ParseIntoArrayLines(FileLines);

			FMemory::Free(FileMem);
			InstallManifestFile->Close();

			
			if (FileLines.Num() != 2)
			{
				UE_LOG(LogTargetPlatformManager, Warning, TEXT("Malformed install manifest file for Platform %s"), *AutoSDKPath);
				return false;
			}

			static const FString ManualSDKString(TEXT("ManualSDK"));
			if (FileLines[1].Compare(ManualSDKString, ESearchCase::IgnoreCase) == 0)
			{
				UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Platform %s has manual sdk install"), *AutoSDKPath);				
				return true;
			}
		}
		else
		{	
			UE_LOG(LogTargetPlatformManager, Log, TEXT("Install manifest file for Platform %s not found.  Platform not set up."), *AutoSDKPath);			
			return false;			
		}		

		static const FString SDKEnvironmentVarsFile(TEXT("OutputEnvVars.txt"));
		FString EnvVarFileName = FPaths::Combine(*TargetSDKRoot, *SDKEnvironmentVarsFile);		

		// If we are using a manual install, then it is valid for there to be no OutputEnvVars file.
		TUniquePtr<FArchive> EnvVarFile(IFileManager::Get().CreateFileReader(*EnvVarFileName));
		if (EnvVarFile)
		{
			TArray<FString> FileLines;
			{
				int64 FileSize = EnvVarFile->TotalSize();
				int64 MemSize = FileSize + 1;
				void* FileMem = FMemory::Malloc(MemSize);
				FMemory::Memset(FileMem, 0, MemSize);

				EnvVarFile->Serialize(FileMem, FileSize);

				FString FileAsString(ANSI_TO_TCHAR(FileMem));
				FileAsString.ParseIntoArrayLines(FileLines);

				FMemory::Free(FileMem);
				EnvVarFile->Close();
			}

			TArray<FString> PathAdds;
			TArray<FString> PathRemoves;
			TArray<FString> EnvVarNames;
			TArray<FString> EnvVarValues;

			const FString VariableSplit(TEXT("="));
			for (int32 i = 0; i < FileLines.Num(); ++i)
			{				
				const FString& VariableString = FileLines[i];

				FString Left;
				FString Right;
				VariableString.Split(VariableSplit, &Left, &Right);
				
				if (Left.Compare(TEXT("strippath"), ESearchCase::IgnoreCase) == 0)
				{
					PathRemoves.Add(Right);
				}
				else if (Left.Compare(TEXT("addpath"), ESearchCase::IgnoreCase) == 0)
				{
					PathAdds.Add(Right);
				}
				else
				{
					// convenience for setup.bat writers.  Trim any accidental whitespace from var names/values.
					EnvVarNames.Add(Left.TrimStartAndEnd());
					EnvVarValues.Add(Right.TrimStartAndEnd());
				}
			}

			// don't actually set anything until we successfully validate and read all values in.
			// we don't want to set a few vars, return a failure, and then have a platform try to
			// build against a manually installed SDK with half-set env vars.
			SetupEnvironmentVariables(EnvVarNames, EnvVarValues);


			FString OrigPathVar = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));

			// actually perform the PATH stripping / adding.
			const TCHAR* PathDelimiter = FPlatformMisc::GetPathVarDelimiter();
			TArray<FString> PathVars;
			OrigPathVar.ParseIntoArray(PathVars, PathDelimiter, true);

			TArray<FString> ModifiedPathVars;
			ModifiedPathVars = PathVars;

			// perform removes first, in case they overlap with any adds.
			for (int32 PathRemoveIndex = 0; PathRemoveIndex < PathRemoves.Num(); ++PathRemoveIndex)
			{
				const FString& PathRemove = PathRemoves[PathRemoveIndex];
				for (int32 PathVarIndex = 0; PathVarIndex < PathVars.Num(); ++PathVarIndex)
				{
					const FString& PathVar = PathVars[PathVarIndex];
					if (PathVar.Find(PathRemove, ESearchCase::IgnoreCase) >= 0)
					{
						UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Removing Path: '%s'"), *PathVar);
						ModifiedPathVars.Remove(PathVar);
					}
				}
			}

			// remove all the of ADDs so that if this function is executed multiple times, the paths will be guarateed to be in the same order after each run.
			// If we did not do this, a 'remove' that matched some, but not all, of our 'adds' would cause the order to change.
			for (int32 PathAddIndex = 0; PathAddIndex < PathAdds.Num(); ++PathAddIndex)			
			{
				const FString& PathAdd = PathAdds[PathAddIndex];				
				for (int32 PathVarIndex = 0; PathVarIndex < PathVars.Num(); ++PathVarIndex)
				{
					const FString& PathVar = PathVars[PathVarIndex];
					if (PathVar.Find(PathAdd, ESearchCase::IgnoreCase) >= 0)
					{
						UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Removing Path: '%s'"), *PathVar);
						ModifiedPathVars.Remove(PathVar);
					}
				}
			}

			// perform adds, but don't add duplicates
			for (int32 PathAddIndex = 0; PathAddIndex < PathAdds.Num(); ++PathAddIndex)
			{
				const FString& PathAdd = PathAdds[PathAddIndex];
				if (!ModifiedPathVars.Contains(PathAdd))
				{
					UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Adding Path: '%s'"), *PathAdd);					
					ModifiedPathVars.Add(PathAdd);
				}
			}

			FString ModifiedPath = FString::Join(ModifiedPathVars, PathDelimiter);
			FPlatformMisc::SetEnvironmentVar(TEXT("PATH"), *ModifiedPath);			
		}
		else
		{
			UE_LOG(LogTargetPlatformManager, Warning, TEXT("OutputEnvVars.txt not found for platform: '%s'"), *AutoSDKPath);			
			return false;
		}

		UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Platform %s has auto sdk install"), *AutoSDKPath);		
		return true;
#else
		return true;
#endif
	}

	bool SetupSDKStatus()
	{
		return SetupSDKStatus(TEXT(""));
	}

	bool SetupSDKStatus(const FString& TargetPlatforms)
	{
		DECLARE_SCOPE_CYCLE_COUNTER( TEXT( "FTargetPlatformManagerModule::SetupSDKStatus" ), STAT_FTargetPlatformManagerModule_SetupSDKStatus, STATGROUP_TargetPlatform );

		// run UBT with -validate -allplatforms and read the output
		FString CmdExe, CommandLine;
		
		if (PLATFORM_MAC)
		{
			CmdExe = TEXT("/bin/sh");
			FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Mac/RunMono.sh"));
			CommandLine = TEXT("\"") + ScriptPath + TEXT("\" \"") + FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool.exe")) + TEXT("\" -Mode=ValidatePlatforms");
		}
		else if (PLATFORM_WINDOWS)
		{
			CmdExe = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool.exe"));
			CommandLine = TEXT("-Mode=ValidatePlatforms");
		}
		else if (PLATFORM_LINUX)
		{
			CmdExe = TEXT("/bin/bash");	// bash and not sh because of pushd
			FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Linux/RunMono.sh"));
			CommandLine = TEXT("\"") + ScriptPath + TEXT("\" \"") + FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool.exe")) + TEXT("\" -Mode=ValidatePlatforms");
		}
		else
		{
			checkf(false, TEXT("FTargetPlatformManagerModule::SetupSDKStatus(): Unsupported platform!"));
		}

		// Allow for only a subset of platforms to be reparsed - needed when kicking a change from the UI
		CommandLine += TargetPlatforms.IsEmpty() ? TEXT(" -allplatforms") : (TEXT(" -platforms=") + TargetPlatforms);

		TSharedPtr<FMonitoredProcess> UBTProcess = MakeShareable(new FMonitoredProcess(CmdExe, CommandLine, true));
		UBTProcess->OnOutput().BindStatic(&FTargetPlatformManagerModule::OnStatusOutput);
		SDKStatusMessage = TEXT("");
		UBTProcess->Launch();
		while(UBTProcess->Update())
		{
			FPlatformProcess::Sleep(0.01f);
		}

		TArray<FString> PlatArray;
		SDKStatusMessage.ParseIntoArrayWS(PlatArray);
		for (int Index = 0; Index < PlatArray.Num()-2; ++Index)
		{
			FString Item = PlatArray[Index];
			if (PlatArray[Index].Contains(TEXT("##PlatformValidate:")))
			{
				PlatformInfo::EPlatformSDKStatus Status = PlatArray[Index+2].Contains(TEXT("INVALID")) ? PlatformInfo::EPlatformSDKStatus::NotInstalled : PlatformInfo::EPlatformSDKStatus::Installed;
				FString PlatformName = PlatArray[Index+1];
				if (PlatformName == TEXT("Win32") || PlatformName == TEXT("Win64"))
				{
					PlatformName = TEXT("Windows");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("WindowsNoEditor");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("WindowsClient");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("WindowsServer");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
				else if (PlatformName == TEXT("Mac"))
				{
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("MacNoEditor");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("MacClient");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("MacServer");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
				else if (PlatformName == TEXT("Linux"))
				{
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxNoEditor");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxClient");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxServer");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
				else if (PlatformName == TEXT("LinuxAArch64"))
				{
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxAArch64NoEditor");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxAArch64Client");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
					PlatformName = TEXT("LinuxAArch64Server");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
				else if (PlatformName == TEXT("Desktop"))
				{
					// since Desktop is just packaging, we don't need an SDK, and UBT will return INVALID, since it doesn't build for it
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, PlatformInfo::EPlatformSDKStatus::Installed);
				}
				else if (PlatformName == TEXT("HoloLens"))
				{
					PlatformName = TEXT("HoloLens");
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
				else
				{
					PlatformInfo::UpdatePlatformSDKStatus(PlatformName, Status);
				}
			}
		}
		return true;
	}

private:

	void SetupEnvironmentVariables(TArray<FString> &EnvVarNames, const TArray<FString>& EnvVarValues)
	{
		for (int i = 0; i < EnvVarNames.Num(); ++i)
		{
			const FString& EnvVarName = EnvVarNames[i];
			const FString& EnvVarValue = EnvVarValues[i];
			UE_LOG(LogTargetPlatformManager, Verbose, TEXT("Setting variable '%s' to '%s'."), *EnvVarName, *EnvVarValue);
			FPlatformMisc::SetEnvironmentVar(*EnvVarName, *EnvVarValue);
		}
	}

	void ModulesChangesCallback(FName ModuleName, EModuleChangeReason ReasonForChange)
	{
		if (!bIgnoreFirstDelegateCall && ModuleName.ToString().Contains(TEXT("TargetPlatform")) && !ModuleName.ToString().Contains(TEXT("ProjectTargetPlatformEditor")))
		{
			Invalidate();
		}
		bIgnoreFirstDelegateCall = false;
	}
	
	static FString SDKStatusMessage;
	static void OnStatusOutput(FString Message)
	{
		SDKStatusMessage += Message;
	}

	FString InitErrorMessages;

	// Delegate used to notify users of returned ITargetPlatform* pointers when those pointers are destructed due to a call to Invalidate
	FOnTargetPlatformsInvalidated OnTargetPlatformsInvalidated;

	// If true we should build formats that are actually required for use by the runtime. 
	// This happens for an ordinary editor run and more specifically whenever there is no
	// TargetPlatform= on the command line.
	bool bRestrictFormatsToRuntimeOnly;

	// Flag to force reinitialization of all cached data. This is needed to have up-to-date caches
	// in case of a module reload of a TargetPlatform-Module.
	bool bForceCacheUpdate;

	// Flag to indicate that there were errors during initialization
	bool bHasInitErrors;

	// Flag to avoid redunant reloads
	bool bIgnoreFirstDelegateCall;

	// Holds the list of discovered platforms.
	TArray<ITargetPlatform*> Platforms;

	// Map for fast lookup of platforms by name.
	TMap<FName, ITargetPlatform*> PlatformsByName;

#if AUTOSDKS_ENABLED
	// holds the list of Platforms that have attempted setup.
	TMap<FName, bool> PlatformsSetup;
#endif
};


FString FTargetPlatformManagerModule::SDKStatusMessage = TEXT("");


IMPLEMENT_MODULE(FTargetPlatformManagerModule, TargetPlatform);
