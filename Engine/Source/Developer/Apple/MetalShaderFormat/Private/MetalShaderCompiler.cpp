// Copyright Epic Games, Inc. All Rights Reserved.
// ..

#include "CoreMinimal.h"
#include "MetalShaderFormat.h"
#include "ShaderCore.h"
#include "MetalShaderResources.h"
#include "ShaderCompilerCommon.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include "Windows/PreWindowsApi.h"
	#include <objbase.h>
	#include <assert.h>
	#include <stdio.h>
	#include "Windows/PostWindowsApi.h"
	#include "Windows/MinWindows.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "ShaderPreprocessor.h"
#include "hlslcc.h"
#include "MetalBackend.h"
#include "MetalDerivedData.h"
#include "DerivedDataCacheInterface.h"

// The Metal standard library extensions we need for UE4.
#include "ue4_stdlib.h"

#if !PLATFORM_WINDOWS
#if PLATFORM_TCHAR_IS_CHAR16
#define FP_TEXT_PASTE(x) L ## x
#define WTEXT(x) FP_TEXT_PASTE(x)
#else
#define WTEXT TEXT
#endif
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMetalShaderCompiler, Log, All); 

static FString	GRemoteBuildServerHost;
static FString	GRemoteBuildServerUser;
static FString	GRemoteBuildServerSSHKey;
static FString	GSSHPath;
static FString	GRSyncPath;

static FString	GMetalToolsPath[AppleSDKCount];
static FString	GMetalBinaryPath[AppleSDKCount];
static FString	GMetalLibraryPath[AppleSDKCount];
static FString	GMetalCompilerVers[AppleSDKCount];
static FString	GTempFolderPath;
static bool		GMetalLoggedRemoteCompileNotConfigured;	// This is used to reduce log spam, its not perfect because there is not a place to reset this flag so a log msg will only be given once per editor run
static bool		GRemoteBuildConfigured = false;

EShaderPlatform MetalShaderFormatToLegacyShaderPlatform(FName ShaderFormat);
FString GetXcodePath();


// Remote Building Utility

// Add (|| PLATFORM_MAC) to enable Mac to Mac remote building
#define UNIXLIKE_TO_MAC_REMOTE_BUILDING (PLATFORM_LINUX)

bool IsRemoteBuildingConfigured(const FShaderCompilerEnvironment* InEnvironment)
{
	// if we have gotten an environment, then it is possible the remote server data has changed, in all other cases, it is not possible for it change
	if (!GRemoteBuildConfigured || InEnvironment != nullptr)
	{
		GRemoteBuildConfigured = false;
		bool	remoteCompilingEnabled = false;
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("EnableRemoteShaderCompile"), remoteCompilingEnabled, GEngineIni);
		if (!remoteCompilingEnabled && !FParse::Param(FCommandLine::Get(), TEXT("enableremote")))
		{
			if (InEnvironment == nullptr || InEnvironment->RemoteServerData.Num() < 2)
			{
				return false;
			}
		}

		bool bUsingXGE = false;
		GConfig->GetBool(TEXT("/Script/UnrealEd.UnrealEdOptions"), TEXT("UsingXGE"), bUsingXGE, GEditorIni);
		if (bUsingXGE)
		{
			if (!GMetalLoggedRemoteCompileNotConfigured)
			{
				if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
				{
					UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Remote shader compilation cannot be used with XGE interface (is this a Launch-on build? try to pre-cook shaders to speed up loading times)."));
				}
				GMetalLoggedRemoteCompileNotConfigured = true;
			}
			return false;
		}

		GRemoteBuildServerHost = "";

		if (InEnvironment != nullptr && InEnvironment->RemoteServerData.Contains(TEXT("RemoteServerName")))
		{
			GRemoteBuildServerHost = InEnvironment->RemoteServerData[TEXT("RemoteServerName")];
		}
		if (GRemoteBuildServerHost.Len() == 0)
		{
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("RemoteServerName"), GRemoteBuildServerHost, GEngineIni);
			if (GRemoteBuildServerHost.Len() == 0)
			{
				// check for it on the command line - meant for ShaderCompileWorker
				if (!FParse::Value(FCommandLine::Get(), TEXT("servername"), GRemoteBuildServerHost) && GRemoteBuildServerHost.Len() == 0)
				{
					if (GRemoteBuildServerHost.Len() == 0)
					{
						if (!GMetalLoggedRemoteCompileNotConfigured)
						{
							if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
							{
								UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Remote Building is not configured: RemoteServerName is not set."));
							}
							GMetalLoggedRemoteCompileNotConfigured = true;
						}
						return false;
					}
				}
			}
		}

		GRemoteBuildServerUser = "";
		if (InEnvironment != nullptr && InEnvironment->RemoteServerData.Contains(TEXT("RSyncUsername")))
		{
			GRemoteBuildServerUser = InEnvironment->RemoteServerData[TEXT("RSyncUsername")];
		}

		if (GRemoteBuildServerUser.Len() == 0)
		{
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("RSyncUsername"), GRemoteBuildServerUser, GEngineIni);

			if (GRemoteBuildServerUser.Len() == 0)
			{
				// check for it on the command line - meant for ShaderCompileWorker
				if (!FParse::Value(FCommandLine::Get(), TEXT("serveruser"), GRemoteBuildServerUser) && GRemoteBuildServerUser.Len() == 0)
				{
					if (GRemoteBuildServerUser.Len() == 0)
					{
						if (!GMetalLoggedRemoteCompileNotConfigured)
						{
							if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
							{
								UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Remote Building is not configured: RSyncUsername is not set."));
							}
							GMetalLoggedRemoteCompileNotConfigured = true;
						}
						return false;
					}
				}
			}
		}

		GRemoteBuildServerSSHKey = "";
		if (InEnvironment != nullptr && InEnvironment->RemoteServerData.Contains(TEXT("SSHPrivateKeyOverridePath")))
		{
			GRemoteBuildServerSSHKey = InEnvironment->RemoteServerData[TEXT("SSHPrivateKeyOverridePath")];
		}
		if (GRemoteBuildServerSSHKey.Len() == 0)
		{
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("SSHPrivateKeyOverridePath"), GRemoteBuildServerSSHKey, GEngineIni);

			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("SSHPrivateKeyOverridePath"), GRemoteBuildServerSSHKey, GEngineIni);
			if (GRemoteBuildServerSSHKey.Len() == 0)
			{
				if (!FParse::Value(FCommandLine::Get(), TEXT("serverkey"), GRemoteBuildServerSSHKey) && GRemoteBuildServerSSHKey.Len() == 0)
				{
					if (GRemoteBuildServerSSHKey.Len() == 0)
					{
						// RemoteToolChain.cs in UBT looks in a few more places but the code in FIOSTargetSettingsCustomization::OnGenerateSSHKey() only puts the key in this location so just going with that to keep things simple
						FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
						GRemoteBuildServerSSHKey = FString::Printf(TEXT("%s\\Unreal Engine\\UnrealBuildTool\\SSHKeys\\%s\\%s\\RemoteToolChainPrivate.key"), *Path, *GRemoteBuildServerHost, *GRemoteBuildServerUser);
					}
				}
			}
		}

		if (!FPaths::FileExists(GRemoteBuildServerSSHKey))
		{
			if (!GMetalLoggedRemoteCompileNotConfigured)
			{
				if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
				{
					UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Remote Building is not configured: SSH private key was not found."));
				}
				GMetalLoggedRemoteCompileNotConfigured = true;
			}
			return false;
		}

	#if PLATFORM_LINUX || PLATFORM_MAC

		// On Unix like systems we have access to ssh and scp at the command line so we can invoke them directly
		GSSHPath = FString(TEXT("/usr/bin/ssh"));
		GRSyncPath = FString(TEXT("/usr/bin/scp"));

	#else

		// Windows requires a Delta copy install for ssh and rsync
		FString DeltaCopyPath;
		GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("DeltaCopyInstallPath"), DeltaCopyPath, GEngineIni);
		if (DeltaCopyPath.IsEmpty() || !FPaths::DirectoryExists(DeltaCopyPath))
		{
			// If no user specified directory try the UE4 bundled directory
			DeltaCopyPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Extras\\ThirdPartyNotUE\\DeltaCopy\\Binaries"));
		}

		if (!FPaths::DirectoryExists(DeltaCopyPath))
		{
			// if no UE4 bundled version of DeltaCopy, try and use the default install location
			FString ProgramPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PROGRAMFILES(X86)"));
			DeltaCopyPath = FPaths::Combine(*ProgramPath, TEXT("DeltaCopy"));
		}

		if (!FPaths::DirectoryExists(DeltaCopyPath))
		{
			if (!GMetalLoggedRemoteCompileNotConfigured)
			{
				if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
				{
					UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Remote Building is not configured: DeltaCopy was not found."));
				}
				GMetalLoggedRemoteCompileNotConfigured = true;
			}
			return false;
		}

		GSSHPath = FPaths::Combine(*DeltaCopyPath, TEXT("ssh.exe"));
		GRSyncPath = FPaths::Combine(*DeltaCopyPath, TEXT("rsync.exe"));

	#endif
		FString XcodePath = GetXcodePath();
		if (XcodePath.Len() <= 0)
		{
			if (!GMetalLoggedRemoteCompileNotConfigured)
			{
				if (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING)
				{
					UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Connection could not be established for remote shader compilation. Check your configuration and the connection to the remote server."));
				}
				GMetalLoggedRemoteCompileNotConfigured = true;
			}
			return false;
		}
		GRemoteBuildConfigured = true;
	}

	return true;	
}

static bool CompileProcessAllowsRuntimeShaderCompiling(const FShaderCompilerInput& InputCompilerEnvironment)
{
    bool bArchiving = InputCompilerEnvironment.Environment.CompilerFlags.Contains(CFLAG_Archive);
    bool bDebug = InputCompilerEnvironment.Environment.CompilerFlags.Contains(CFLAG_Debug);
    
    return !bArchiving && bDebug;
}

static bool ExecProcess(const TCHAR* Command, const TCHAR* Params, int32* OutReturnCode, FString* OutStdOut, FString* OutStdErr)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return FPlatformProcess::ExecProcess(Command, Params, OutReturnCode, OutStdOut, OutStdErr);
#else
	void* ReadPipe = nullptr, *WritePipe = nullptr;
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
	FProcHandle Proc;

	Proc = FPlatformProcess::CreateProc(Command, Params, true, true, true, NULL, -1, NULL, WritePipe);

	if (!Proc.IsValid())
	{
		return false;
	}

	// Wait for the process to complete
	int32 ReturnCode = 0;
	FPlatformProcess::WaitForProc(Proc);
	FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);

	*OutStdOut = FPlatformProcess::ReadPipe(ReadPipe);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	FPlatformProcess::CloseProc(Proc);
	if (OutReturnCode)
		*OutReturnCode = ReturnCode;

	// Did it work?
	return (ReturnCode == 0);
#endif
}

bool ExecRemoteProcess(const TCHAR* Command, const TCHAR* Params, int32* OutReturnCode, FString* OutStdOut, FString* OutStdErr)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return FPlatformProcess::ExecProcess(Command, Params, OutReturnCode, OutStdOut, OutStdErr);
#else
	if (GRemoteBuildServerHost.IsEmpty())
	{
		return false;
	}

	FString CmdLine = FString(TEXT("-i \"")) + GRemoteBuildServerSSHKey + TEXT("\" \"") + GRemoteBuildServerUser + '@' + GRemoteBuildServerHost + TEXT("\" ") + Command + TEXT(" ") + (Params != nullptr ? Params : TEXT(""));
	return ExecProcess(*GSSHPath, *CmdLine, OutReturnCode, OutStdOut, OutStdErr);

#endif
}

bool RemoteFileExists(const FString& Path)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return IFileManager::Get().FileExists(*Path);
#else
	int32 ReturnCode = 1;
	FString StdOut;
	FString StdErr;
	return (ExecRemoteProcess(*FString::Printf(TEXT("test -e \"%s\""), *Path), nullptr, &ReturnCode, &StdOut, &StdErr) && ReturnCode == 0);
#endif
}

static uint32 GetMaxArgLength()
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
    static uint32 MaxLength = 0;
    if (!MaxLength)
    {
        // It's dangerous to use "ARG_MAX" directly because it's a compile time constant and may not be compatible with the running OS.
        // It's safer to get the number from "getconf ARG_MAX" and only use the constant as the fallback
        FString StdOut, StdError;
        if (ExecRemoteProcess(TEXT("/usr/bin/getconf"), TEXT("ARG_MAX"), nullptr, &StdOut, &StdError))
        {
            MaxLength = FCString::Atoi(*StdOut);
            check(MaxLength > 0);
            UE_LOG(LogMetalShaderCompiler, Display, TEXT("Set MaxArgLength to %d via getconf"), MaxLength);
        }
        else
        {
            MaxLength = FMath::Min(ARG_MAX, 256 * 1024);
            UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Failed to determine MaxArgLength via getconf: %s\nSet it to %d which is the lesser of MAX_ARG and the value from the 10.15 SDK"), *StdError, MaxLength);
        }
    }
    return MaxLength;
#else
    // Ask the remote machine via "getconf ARG_MAX"
    return 1024;
#endif
}

FString MakeRemoteTempFolder(FString Path)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return Path;
#else
	if(GTempFolderPath.Len() == 0)
	{
		FString TempFolderPath;
		if (ExecRemoteProcess(TEXT("mktemp -d -t UE4Metal"), nullptr, nullptr, &TempFolderPath, nullptr) && TempFolderPath.Len() > 0)
		{
			TempFolderPath.RemoveAt(TempFolderPath.Len() - 1); // Remove \n at the end of the string
		}
		GTempFolderPath = TempFolderPath;
	}

	return GTempFolderPath;
#endif
}

FString LocalPathToRemote(const FString& LocalPath, const FString& RemoteFolder)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return LocalPath;
#else
	return RemoteFolder / FPaths::GetCleanFilename(LocalPath);
#endif
}

bool CopyLocalFileToRemote(FString const& LocalPath, FString const& RemotePath)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return true;
#else
#if UNIXLIKE_TO_MAC_REMOTE_BUILDING
    // Params formatted for 'scp'
    FString	params = FString::Printf(TEXT("%s %s@%s:%s"), *LocalPath, *GRemoteBuildServerUser, *GRemoteBuildServerHost, *RemotePath);
#else
	FString	remoteBasePath;
	FString remoteFileName;
	FString	remoteFileExt;
	FPaths::Split(RemotePath, remoteBasePath, remoteFileName, remoteFileExt);
	
	FString cygwinLocalPath = TEXT("/cygdrive/") + LocalPath.Replace(TEXT(":"), TEXT(""));

	FString	params = 
		FString::Printf(
			TEXT("-zrltgoDe \"'%s' -i '%s'\" --rsync-path=\"mkdir -p %s && rsync\" --chmod=ug=rwX,o=rxX '%s' \"%s@%s\":'%s'"), 
			*GSSHPath,
			*GRemoteBuildServerSSHKey, 
			*remoteBasePath, 
			*cygwinLocalPath, 
			*GRemoteBuildServerUser,
			*GRemoteBuildServerHost,
			*RemotePath);
			
#endif

	int32	returnCode;
	FString	stdOut, stdErr;
	return ExecProcess(*GRSyncPath, *params, &returnCode, &stdOut, &stdErr);
#endif
}

bool CopyRemoteFileToLocal(FString const& RemotePath, FString const& LocalPath)
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return true;
#else
#if UNIXLIKE_TO_MAC_REMOTE_BUILDING
    // Params formatted for 'scp'
    FString	params = FString::Printf(TEXT("%s@%s:%s %s"), *GRemoteBuildServerUser, *GRemoteBuildServerHost, *RemotePath, *LocalPath);
#else
	FString cygwinLocalPath = TEXT("/cygdrive/") + LocalPath.Replace(TEXT(":"), TEXT(""));

	FString	params = 
		FString::Printf(
			TEXT("-zrltgoDe \"'%s' -i '%s'\" \"%s@%s\":'%s' '%s'"), 
			*GSSHPath,
			*GRemoteBuildServerSSHKey, 
			*GRemoteBuildServerUser,
			*GRemoteBuildServerHost,
			*RemotePath, 
			*cygwinLocalPath);

#endif

	int32	returnCode;
	FString	stdOut, stdErr;
	return ExecProcess(*GRSyncPath, *params, &returnCode, &stdOut, &stdErr);
#endif
}

bool ChecksumRemoteFile(FString const& RemotePath, uint32* CRC, uint32* Len)
{
	int32 ReturnCode = -1;
	FString Output;
	bool bOK = ExecRemoteProcess(TEXT("/usr/bin/cksum"), *RemotePath, &ReturnCode, &Output, nullptr);
	if (bOK)
	{
#if !PLATFORM_WINDOWS
		if(swscanf(TCHAR_TO_WCHAR(*Output), WTEXT("%u %u"), CRC, Len) != 2)
#else
		if(swscanf_s(*Output, TEXT("%u %u"), CRC, Len) != 2)
#endif
		{
			bOK = false;
		}
	}
	return bOK;
}

bool ModificationTimeRemoteFile(FString const& RemotePath, uint64& Time)
{
	int32 ReturnCode = -1;
	FString Output;
	FString Args = TEXT(" -f \"%Sm\" -t \"%s\" ") + RemotePath;
	bool bOK = ExecRemoteProcess(TEXT("/usr/bin/stat"), *Args, &ReturnCode, &Output, nullptr);
	if (bOK)
	{
		LexFromString(Time, *Output);
	}
	return bOK;
}

bool RemoveRemoteFile(FString const& RemotePath)
{
	int32 ReturnCode = -1;
	FString Output;
	bool bOK = ExecRemoteProcess(TEXT("/bin/rm"), *RemotePath, &ReturnCode, &Output, nullptr);
	if (bOK)
	{
		bOK = (ReturnCode == 0);
	}
	return bOK;
}

// SDK Utility

// Returns the SDK name for a given ShaderPlatform
// Note: This is NOT CORRECT
// We may want to compile SM5 stuff for ios but we are not using the correct compiler.
// This has always been broken.
const TCHAR* GetAppleSDKName(EShaderPlatform ShaderPlatform)
{
	static const TCHAR _MacOSXSDK[] = TEXT("macosx");
	static const TCHAR _IOSSDK[] = TEXT("iphoneos");
	static const TCHAR _TVOSSDK[] = TEXT("appletvos");
	
	switch(ShaderPlatform)
	{
		case SP_METAL:
		case SP_METAL_MRT:
			return _IOSSDK;
		case SP_METAL_SM5:
		case SP_METAL_SM5_NOTESS:
		case SP_METAL_MACES3_1:
		case SP_METAL_MRT_MAC:
			return _MacOSXSDK;
		case SP_METAL_TVOS:
		case SP_METAL_MRT_TVOS:
			return _TVOSSDK;
		default:
			// fall through and die
			break;
	}
	
	// We can't proceed without an sdk, of course.
	UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Invalid Shader Platform %u"), ShaderPlatform);
	return nullptr;
}

EShaderPlatform AppleSDKToBaseShaderPlatform(EAppleSDKType SDK)
{
	switch(SDK)
	{
		case AppleSDKMac:
			return SP_METAL_SM5;
		case AppleSDKIOS:
			return SP_METAL;
		case AppleSDKTVOS:
			return SP_METAL_TVOS;
		default:
			break;
	}
	
	UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("%u is not valid apple sdk type"), SDK);
	return SP_NumPlatforms;
}

EAppleSDKType ShaderPlatformToAppleSDK(EShaderPlatform ShaderPlatform)
{
	switch(ShaderPlatform)
	{
		case SP_METAL:
		case SP_METAL_MRT:
			return AppleSDKIOS;
		case SP_METAL_SM5:
		case SP_METAL_SM5_NOTESS:
		case SP_METAL_MACES3_1:
		case SP_METAL_MRT_MAC:
			return AppleSDKMac;
		case SP_METAL_TVOS:
		case SP_METAL_MRT_TVOS:
			return AppleSDKTVOS;
		default:
			// fall through and die
			UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Passed in weird ShaderPlatform %u"), ShaderPlatform);
			return AppleSDKMac;
			break;
	}
	
	UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("%u is not a Metal shader platform"), ShaderPlatform);
	return AppleSDKCount;
}

// Because all the tools included with xcode are liable to move anywhere at anytime we need to invoke them via xcrun.
bool ExecXcodeCommand(EShaderPlatform ShaderPlatform, const TCHAR* Command, const TCHAR* Parameters, int32* OutReturnCode, FString* OutStdOut, FString* OutStdErr)
{
	const TCHAR* SDKName = GetAppleSDKName(ShaderPlatform);
	
	FString Params = FString::Printf(TEXT("-sdk %s %s %s"), SDKName, Command, Parameters);
	return ExecRemoteProcess(TEXT("/usr/bin/xcrun"), *Params, OutReturnCode, OutStdOut, OutStdErr);
}

FString GetXcodePath()
{
#if PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING
	return FPlatformMisc::GetXcodePath();
#else
	FString XcodePath;
	if (ExecRemoteProcess(TEXT("/usr/bin/xcode-select"),TEXT("--print-path"), nullptr, &XcodePath, nullptr) && XcodePath.Len() > 0)
	{
		XcodePath.RemoveAt(XcodePath.Len() - 1); // Remove \n at the end of the string
	}
	return XcodePath;
#endif
}

// PathPrefix should be "programs:" or "libraries:" and both followed by "=DIR" where DIR is the path to extract.
// This function also handles the case when multiple paths are concatenated via colons (like it's the case with the $PATH environment variable).
static bool ExtractXcodeCompilerPath(const FString& InPathInfo, const TCHAR* PathPrefix, FString& OutPath, const TCHAR* RequiredFilename)
{
	if (InPathInfo.Contains(PathPrefix))
	{
		// Scan output directory. Note that it might contain multiple paths separated by colons (like the $PATH environment variable)
		int32 IndexStart = InPathInfo.Find(TEXT("="));
		
		if (InPathInfo.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, IndexStart + 1) != INDEX_NONE)
		{
			// Find directory in concatenated path list that contains the required file, either "metal" or "include/metal/metal_stdlib"
			for (int32 IndexEnd = 0; IndexStart != INDEX_NONE; IndexStart = IndexEnd)
			{
				// Skip the current "=" or ":" character
				++IndexStart;
				IndexEnd = InPathInfo.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, IndexStart);
				
				// Extract install directory DIR from first substring of "programs: =DIR:FURTHER_DIRS"
				if (IndexEnd == INDEX_NONE)
				{
					OutPath = InPathInfo.RightChop(IndexStart);
				}
				else
				{
					OutPath = InPathInfo.Mid(IndexStart, IndexEnd - IndexStart);
				}
				
				// Check if required file exists in this directory
				if (RemoteFileExists(OutPath / RequiredFilename))
				{
					// Found required file, stop scanning for paths
					return true;
				}
			}
		}
		else
		{
			// Extract install directory DIR from right side of "programs: =DIR"
			OutPath = InPathInfo.RightChop(IndexStart + 1);
			
			// Check if required file exists in this directory
			return RemoteFileExists(OutPath / RequiredFilename);
		}
	}
	
	// Compiler path not found
	return false;
}

bool ExtractCompilerInfo(EShaderPlatform ShaderPlatform, FString* OutVersion, FString* OutInstalledDirectory, FString* OutLibDirectory)
{
	{
		// Fetch the version of the metal frontend for ShaderPlatform
		// We are only interested in the (metalfe-XXX.X.XX) part.
		// xcrun -sdk <sdk> metal -v
		// For example (in xcode 11.1):
		// xcrun -sdk macosx metal --version
		// Apple LLVM version 902.9 (metalfe-902.9.58)
		// Target: air64-apple-darwin19.0.0
		// Thread model: posix
		// InstalledDir: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/metal/macos/bin
		
		bool bOK = false;
		FString OutputString;
		bOK = ExecXcodeCommand(ShaderPlatform, TEXT("metal"), TEXT("-v"), nullptr, &OutputString, &OutputString);

		if(bOK && OutputString.Len() > 0)
		{
			int32 VersionStart = OutputString.Find(TEXT("(metalfe"));
			
			if(VersionStart != -1)
			{
				// this should be something in the form of metalfe-XXX.X.XX
				check(OutVersion);
				*OutVersion = OutputString.RightChop(VersionStart+1);
				int32 End = OutVersion->Find(TEXT(")"));
				*OutVersion = OutVersion->Left(End);
			}
			else
			{
				UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Malformed result from metal -v.\nOutput\n%s"), *OutputString);
				return false;
			}
		}
		else
		{
			UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Malformed result from metal -v.\nOutput\n%s"), *OutputString);
			return false;
		}
	}
	
	{
		// Fetch the directories where the binaries live and where metal_stdlib lives
		// $ xcrun -sdk <sdk> metal --print-search-dirs
		// For example (Xcode 11.1):
		// $ xcrun -sdk macosx metal --print-search-dirs
		// programs: =/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/metal/macos/bin
		// libraries: =/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/metal/macos/lib/clang/902.9
		FString OutputString;
		bool bOk = ExecXcodeCommand(ShaderPlatform, TEXT("metal"), TEXT("--print-search-dirs"), nullptr, &OutputString, &OutputString);
		if(bOk && OutputString.Len() > 0)
		{
			// split into lines and look for the output
			TArray<FString> Lines;
			OutputString.ParseIntoArrayLines(Lines, true);
			
			// Extract directory where the "metal" executable lives
			check(OutInstalledDirectory);
			ExtractXcodeCompilerPath(Lines[0], TEXT("programs:"), *OutInstalledDirectory, TEXT("metal"));
			
			// Extract directory where the "include/metal/metal_stdlib" header file lives, and append the additional reltive path
			check(OutLibDirectory);
			if (ExtractXcodeCompilerPath(Lines[1], TEXT("libraries:"), *OutLibDirectory, TEXT("include/metal/metal_stdlib")))
			{
				// Ends up pointing to the clang version base. we want the metal headers.
				*OutLibDirectory /= TEXT("include/metal");
			}
		}
		else
		{
			UE_LOG(LogMetalShaderCompiler, Warning, TEXT("Malformed result from metal --print-search-dirs.\nOutput\n%s"), *OutputString);
			return false;
		}
	}
	
	return true;
}

static bool SingleCompilerSetup(EAppleSDKType SDK)
{
	FString Version;
	FString BinaryDirectory;
	FString LibraryDirectory;
	
	EShaderPlatform ShaderPlatform = AppleSDKToBaseShaderPlatform(SDK);
	if (!ExtractCompilerInfo(ShaderPlatform, &Version, &BinaryDirectory, &LibraryDirectory))
	{
		UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Failed to extract Metal compiler search directories"));
		return false;
	}
	
	FString MetalCompilerPath = BinaryDirectory / TEXT("metal");
	FString MetalStdlibPath = LibraryDirectory / TEXT("metal_stdlib");
	
//	UE_LOG(LogMetalShaderCompiler, Log, TEXT("Ver %s"), *Version);
//	UE_LOG(LogMetalShaderCompiler, Log, TEXT("BinDir %s"), *BinaryDirectory);
//	UE_LOG(LogMetalShaderCompiler, Log, TEXT("LibDir %s"), *LibraryDirectory);
	
	bool bMetalExists = RemoteFileExists(MetalCompilerPath);
	bool bLibExists = RemoteFileExists(MetalStdlibPath);
	
	if(!bMetalExists)
	{
		UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Missing Metal frontend at %s"), *MetalCompilerPath)
	}
	
	if(!bLibExists)
	{
		UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Missing Metal headers at %s"), *MetalStdlibPath)
	}
	
	GMetalToolsPath[SDK] = *BinaryDirectory;
	GMetalBinaryPath[SDK] = *MetalCompilerPath;
	GMetalLibraryPath[SDK] = *MetalStdlibPath;
	GMetalCompilerVers[SDK] = *Version;
	
	return true;
}

static void DoMetalCompilerSetup()
{
	// should technically be atomic or dispatch_once.
	static bool bSetupComplete = false;
	
	if(!bSetupComplete)
	{
		// Does the compiler exist?
		SingleCompilerSetup(AppleSDKMac);
		SingleCompilerSetup(AppleSDKIOS);
		SingleCompilerSetup(AppleSDKTVOS);
		bSetupComplete = true;
	}
}

static bool IsMetalCompilerAvailable(EShaderPlatform ShaderPlatform)
{
	DoMetalCompilerSetup();
	EAppleSDKType SDK = ShaderPlatformToAppleSDK(ShaderPlatform);
	return GMetalCompilerVers[SDK].Len() > 0;
}

const FString& GetMetalToolsPath(EShaderPlatform ShaderPlatform)
{
	DoMetalCompilerSetup();
	EAppleSDKType SDK = ShaderPlatformToAppleSDK(ShaderPlatform);
	return GMetalToolsPath[SDK];
}

const FString& GetMetalCompilerVersion(EShaderPlatform ShaderPlatform)
{
	DoMetalCompilerSetup();
	EAppleSDKType SDK = ShaderPlatformToAppleSDK(ShaderPlatform);
	return GMetalCompilerVers[SDK];
}

const FString& GetMetalLibraryPath(EShaderPlatform ShaderPlatform)
{
	DoMetalCompilerSetup();
	EAppleSDKType SDK = ShaderPlatformToAppleSDK(ShaderPlatform);
	return GMetalLibraryPath[SDK];
}

uint16 GetXcodeVersion(uint64& BuildVersion)
{
	BuildVersion = 0;
	
	static uint64 Build = 0;
	static uint16 Version = UINT16_MAX;
	if (Version == UINT16_MAX)
	{
		Version = 0; // No Xcode install is 0, so only text shaders will work
		FString XcodePath = GetXcodePath();
		// Because of where and when this is called you can't invoke it on Win->Mac builds
		if (XcodePath.Len() > 0 && PLATFORM_MAC)
		{
			FString Path = FString::Printf(TEXT("%s/usr/bin/xcodebuild"), *XcodePath);
			FString Result;
			bool bOK = false;
			bOK = ExecRemoteProcess(*Path, TEXT("-version"), nullptr, &Result, nullptr);
			if (bOK && Result.Len() > 0)
			{
				uint32 Major = 0;
				uint32 Minor = 0;
				uint32 Patch = 0;
				int32 NumResults = 0;
	#if !PLATFORM_WINDOWS
				NumResults = swscanf(TCHAR_TO_WCHAR(*Result), WTEXT("Xcode %u.%u.%u"), &Major, &Minor, &Patch);
	#else
				NumResults = swscanf_s(*Result, TEXT("Xcode %u.%u.%u"), &Major, &Minor, &Patch);
	#endif
				if (NumResults >= 2)
				{
					Version = (((Major & 0xff) << 8) | ((Minor & 0xf) << 4) | (Patch & 0xf));
					
					ANSICHAR const* BuildScan = "Xcode %*u.%*u.%*u\nBuild version %s";
					if (NumResults == 2)
					{
						BuildScan = "Xcode %*u.%*u\nBuild version %s";
					}
					
					ANSICHAR Buffer[9] = {0,0,0,0,0,0,0,0,0};
#if !PLATFORM_WINDOWS
					if(sscanf(TCHAR_TO_ANSI(*Result), BuildScan, Buffer))
#else
					if(sscanf_s(TCHAR_TO_ANSI(*Result), BuildScan, Buffer, 9))
#endif
					{
						FMemory::Memcpy(&Build, Buffer, sizeof(uint64));
					}
				}
			}
		}
	}
	BuildVersion = Build;
	return Version;
}

/*------------------------------------------------------------------------------
	Shader compiling.
------------------------------------------------------------------------------*/

static inline uint32 ParseNumber(const TCHAR* Str)
{
	uint32 Num = 0;
	while (*Str && *Str >= '0' && *Str <= '9')
	{
		Num = Num * 10 + *Str++ - '0';
	}
	return Num;
}

static inline uint32 ParseNumber(const ANSICHAR* Str)
{
	uint32 Num = 0;
	while (*Str && *Str >= '0' && *Str <= '9')
	{
		Num = Num * 10 + *Str++ - '0';
	}
	return Num;
}

struct FHlslccMetalHeader : public CrossCompiler::FHlslccHeader
{
	FHlslccMetalHeader(uint8 const Version, bool const bUsingTessellation);
	virtual ~FHlslccMetalHeader();
	
	// After the standard header, different backends can output their own info
	virtual bool ParseCustomHeaderEntries(const ANSICHAR*& ShaderSource) override;
	
	float  TessellationMaxTessFactor;
	uint32 TessellationOutputControlPoints;
	uint32 TessellationDomain; // 3 = tri, 4 = quad
	uint32 TessellationInputControlPoints;
	uint32 TessellationPatchesPerThreadGroup;
	uint32 TessellationPatchCountBuffer;
	uint32 TessellationIndexBuffer;
	uint32 TessellationHSOutBuffer;
	uint32 TessellationHSTFOutBuffer;
	uint32 TessellationControlPointOutBuffer;
	uint32 TessellationControlPointIndexBuffer;
	EMetalOutputWindingMode TessellationOutputWinding;
	EMetalPartitionMode TessellationPartitioning;
	TMap<uint8, TArray<uint8>> ArgumentBuffers;
	int8 SideTable;
	uint8 Version;
	bool bUsingTessellation;
};

FHlslccMetalHeader::FHlslccMetalHeader(uint8 const InVersion, bool const bInUsingTessellation)
{
	TessellationMaxTessFactor = 0.0f;
	TessellationOutputControlPoints = 0;
	TessellationDomain = 0;
	TessellationInputControlPoints = 0;
	TessellationPatchesPerThreadGroup = 0;
	TessellationOutputWinding = EMetalOutputWindingMode::Clockwise;
	TessellationPartitioning = EMetalPartitionMode::Pow2;
	
	TessellationPatchCountBuffer = UINT_MAX;
	TessellationIndexBuffer = UINT_MAX;
	TessellationHSOutBuffer = UINT_MAX;
	TessellationHSTFOutBuffer = UINT_MAX;
	TessellationControlPointOutBuffer = UINT_MAX;
	TessellationControlPointIndexBuffer = UINT_MAX;
	
	SideTable = -1;
	Version = InVersion;
	bUsingTessellation = bInUsingTessellation;
}

FHlslccMetalHeader::~FHlslccMetalHeader()
{
	
}

bool FHlslccMetalHeader::ParseCustomHeaderEntries(const ANSICHAR*& ShaderSource)
{
#define DEF_PREFIX_STR(Str) \
static const ANSICHAR* Str##Prefix = "// @" #Str ": "; \
static const int32 Str##PrefixLen = FCStringAnsi::Strlen(Str##Prefix)
	DEF_PREFIX_STR(TessellationOutputControlPoints);
	DEF_PREFIX_STR(TessellationDomain);
	DEF_PREFIX_STR(TessellationInputControlPoints);
	DEF_PREFIX_STR(TessellationMaxTessFactor);
	DEF_PREFIX_STR(TessellationOutputWinding);
	DEF_PREFIX_STR(TessellationPartitioning);
	DEF_PREFIX_STR(TessellationPatchesPerThreadGroup);
	DEF_PREFIX_STR(TessellationPatchCountBuffer);
	DEF_PREFIX_STR(TessellationIndexBuffer);
	DEF_PREFIX_STR(TessellationHSOutBuffer);
	DEF_PREFIX_STR(TessellationHSTFOutBuffer);
	DEF_PREFIX_STR(TessellationControlPointOutBuffer);
	DEF_PREFIX_STR(TessellationControlPointIndexBuffer);
	DEF_PREFIX_STR(ArgumentBuffers);
	DEF_PREFIX_STR(SideTable);
#undef DEF_PREFIX_STR
	
	const ANSICHAR* SideTableString = FCStringAnsi::Strstr(ShaderSource, SideTablePrefix);
	if (SideTableString)
	{
		ShaderSource = SideTableString;
		ShaderSource += SideTablePrefixLen;
		while (*ShaderSource && *ShaderSource != '\n')
		{
			if (*ShaderSource == '(')
			{
				ShaderSource++;
				if (*ShaderSource && *ShaderSource != '\n')
				{
					SideTable = (int8)ParseNumber(ShaderSource);
				}
			}
			else
			{
				ShaderSource++;
			}
		}
		
		if (*ShaderSource && !CrossCompiler::Match(ShaderSource, '\n'))
		{
			return false;
		}
		
		if (SideTable < 0)
		{
			UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Couldn't parse the SideTable buffer index for bounds checking"));
			return false;
		}
	}
	
	const ANSICHAR* ArgumentTable = FCStringAnsi::Strstr(ShaderSource, ArgumentBuffersPrefix);
	if (ArgumentTable)
	{
		ShaderSource = ArgumentTable;
		ShaderSource += ArgumentBuffersPrefixLen;
		while (*ShaderSource && *ShaderSource != '\n')
		{
			int32 ArgumentBufferIndex = -1;
			if (!CrossCompiler::ParseIntegerNumber(ShaderSource, ArgumentBufferIndex))
			{
				return false;
			}
			check(ArgumentBufferIndex >= 0);
			
			if (!CrossCompiler::Match(ShaderSource, '['))
			{
				return false;
			}
			
			TArray<uint8> Mask;
			while (*ShaderSource && *ShaderSource != ']')
			{
				int32 MaskIndex = -1;
				if (!CrossCompiler::ParseIntegerNumber(ShaderSource, MaskIndex))
				{
					return false;
				}
				
				check(MaskIndex >= 0);
				Mask.Add((uint8)MaskIndex);
				
				if (!CrossCompiler::Match(ShaderSource, ',') && *ShaderSource != ']')
				{
					return false;
				}
			}
			
			if (!CrossCompiler::Match(ShaderSource, ']'))
			{
				return false;
			}
			
			if (!CrossCompiler::Match(ShaderSource, ',') && *ShaderSource != '\n')
			{
				return false;
			}
			
			ArgumentBuffers.Add((uint8)ArgumentBufferIndex, Mask);
		}
	}
	
	// Early out for non-tessellation...
	if (!bUsingTessellation)
	{
		return true;
	}
	
	auto ParseUInt32Attribute = [&ShaderSource](const ANSICHAR* prefix, int32 prefixLen, uint32& attributeOut)
	{
		if (FCStringAnsi::Strncmp(ShaderSource, prefix, prefixLen) == 0)
		{
			ShaderSource += prefixLen;
			
			if (!CrossCompiler::ParseIntegerNumber(ShaderSource, attributeOut))
			{
				return false;
			}
			
			if (!CrossCompiler::Match(ShaderSource, '\n'))
			{
				return false;
			}
		}
		
		return true;
	};
 
	// Read number of tessellation output control points
	if (!ParseUInt32Attribute(TessellationOutputControlPointsPrefix, TessellationOutputControlPointsPrefixLen, TessellationOutputControlPoints))
	{
		return false;
	}
	
	// Read the tessellation domain (tri vs quad)
	if (FCStringAnsi::Strncmp(ShaderSource, TessellationDomainPrefix, TessellationDomainPrefixLen) == 0)
	{
		ShaderSource += TessellationDomainPrefixLen;
		
		if (FCStringAnsi::Strncmp(ShaderSource, "tri", 3) == 0)
		{
			ShaderSource += 3;
			TessellationDomain = 3;
		}
		else if (FCStringAnsi::Strncmp(ShaderSource, "quad", 4) == 0)
		{
			ShaderSource += 4;
			TessellationDomain = 4;
		}
		else
		{
			return false;
		}
		
		if (!CrossCompiler::Match(ShaderSource, '\n'))
		{
			return false;
		}
	}
	
	// Read number of tessellation input control points
	if (!ParseUInt32Attribute(TessellationInputControlPointsPrefix, TessellationInputControlPointsPrefixLen, TessellationInputControlPoints))
	{
		return false;
	}
	
	// Read max tessellation factor
	if (FCStringAnsi::Strncmp(ShaderSource, TessellationMaxTessFactorPrefix, TessellationMaxTessFactorPrefixLen) == 0)
	{
		ShaderSource += TessellationMaxTessFactorPrefixLen;
		
#if PLATFORM_WINDOWS
		if (sscanf_s(ShaderSource, "%g\n", &TessellationMaxTessFactor) != 1)
#else
		if (sscanf(ShaderSource, "%g\n", &TessellationMaxTessFactor) != 1)
#endif
		{
			return false;
		}
		
		while (*ShaderSource != '\n')
		{
			++ShaderSource;
		}
		++ShaderSource; // to match the newline
	}
	
	// Read tessellation output winding mode
	if (FCStringAnsi::Strncmp(ShaderSource, TessellationOutputWindingPrefix, TessellationOutputWindingPrefixLen) == 0)
	{
		ShaderSource += TessellationOutputWindingPrefixLen;
		
		if (FCStringAnsi::Strncmp(ShaderSource, "cw", 2) == 0)
		{
			ShaderSource += 2;
			TessellationOutputWinding = EMetalOutputWindingMode::Clockwise;
		}
		else if (FCStringAnsi::Strncmp(ShaderSource, "ccw", 3) == 0)
		{
			ShaderSource += 3;
			TessellationOutputWinding = EMetalOutputWindingMode::CounterClockwise;
		}
		else
		{
			return false;
		}
		
		if (!CrossCompiler::Match(ShaderSource, '\n'))
		{
			return false;
		}
	}
	
	// Read tessellation partition mode
	if (FCStringAnsi::Strncmp(ShaderSource, TessellationPartitioningPrefix, TessellationPartitioningPrefixLen) == 0)
	{
		ShaderSource += TessellationPartitioningPrefixLen;
		
		static char const* partitionModeNames[] =
		{
			// order match enum order
			"pow2",
			"integer",
			"fractional_odd",
			"fractional_even",
		};
		
		bool match = false;
		for (size_t i = 0; i < sizeof(partitionModeNames) / sizeof(partitionModeNames[0]); ++i)
		{
			size_t partitionModeNameLen = strlen(partitionModeNames[i]);
			if (FCStringAnsi::Strncmp(ShaderSource, partitionModeNames[i], partitionModeNameLen) == 0)
			{
				ShaderSource += partitionModeNameLen;
				TessellationPartitioning = (EMetalPartitionMode)i;
				match = true;
				break;
			}
		}
		
		if (!match)
		{
			return false;
		}
		
		if (!CrossCompiler::Match(ShaderSource, '\n'))
		{
			return false;
		}
	}
	
	// Read number of tessellation patches per threadgroup
	if (!ParseUInt32Attribute(TessellationPatchesPerThreadGroupPrefix, TessellationPatchesPerThreadGroupPrefixLen, TessellationPatchesPerThreadGroup))
	{
		return false;
	}
	
	if (!ParseUInt32Attribute(TessellationPatchCountBufferPrefix, TessellationPatchCountBufferPrefixLen, TessellationPatchCountBuffer))
	{
		TessellationPatchCountBuffer = UINT_MAX;
	}
	
	if (!ParseUInt32Attribute(TessellationIndexBufferPrefix, TessellationIndexBufferPrefixLen, TessellationIndexBuffer))
	{
		TessellationIndexBuffer = UINT_MAX;
	}
	
	if (!ParseUInt32Attribute(TessellationHSOutBufferPrefix, TessellationHSOutBufferPrefixLen, TessellationHSOutBuffer))
	{
		TessellationHSOutBuffer = UINT_MAX;
	}
	
	if (!ParseUInt32Attribute(TessellationControlPointOutBufferPrefix, TessellationControlPointOutBufferPrefixLen, TessellationControlPointOutBuffer))
	{
		TessellationControlPointOutBuffer = UINT_MAX;
	}
	
	if (!ParseUInt32Attribute(TessellationHSTFOutBufferPrefix, TessellationHSTFOutBufferPrefixLen, TessellationHSTFOutBuffer))
	{
		TessellationHSTFOutBuffer = UINT_MAX;
	}
	
	if (!ParseUInt32Attribute(TessellationControlPointIndexBufferPrefix, TessellationControlPointIndexBufferPrefixLen, TessellationControlPointIndexBuffer))
	{
		TessellationControlPointIndexBuffer = UINT_MAX;
	}
	
	return true;
}

/**
 * Construct the final microcode from the compiled and verified shader source.
 * @param ShaderOutput - Where to store the microcode and parameter map.
 * @param InShaderSource - Metal source with input/output signature.
 * @param SourceLen - The length of the Metal source code.
 */
void BuildMetalShaderOutput(
	FShaderCompilerOutput& ShaderOutput,
	const FShaderCompilerInput& ShaderInput,
	FSHAHash const& GUIDHash,
	uint32 CCFlags,
	const ANSICHAR* InShaderSource,
	uint32 SourceLen,
	uint32 SourceCRCLen,
	uint32 SourceCRC,
	uint8 Version,
	TCHAR const* Standard,
	TCHAR const* MinOSVersion,
	EMetalTypeBufferMode TypeMode,
	TArray<FShaderCompilerError>& OutErrors,
	FMetalTessellationOutputs const& TessOutputAttribs,
	uint32 TypedBuffers,
	uint32 InvariantBuffers,
	uint32 TypedUAVs,
	uint32 ConstantBuffers,
	TArray<uint8> const& TypedBufferFormats,
	bool bAllowFastIntriniscs
	)
{
	ShaderOutput.bSucceeded = false;
	
	const ANSICHAR* USFSource = InShaderSource;
	
	uint32 NumLines = 0;
	const ANSICHAR* Main = FCStringAnsi::Strstr(USFSource, "Main_");
	while (Main && *Main)
	{
		if (*Main == '\n')
		{
			NumLines++;
		}
		Main++;
	}
	
	FString const* UsingTessellationDefine = ShaderInput.Environment.GetDefinitions().Find(TEXT("USING_TESSELLATION"));
	bool bUsingTessellation = (UsingTessellationDefine != nullptr && FString("1") == *UsingTessellationDefine);
	
	FHlslccMetalHeader CCHeader(Version, bUsingTessellation);
	if (!CCHeader.Read(USFSource, SourceLen))
	{
		UE_LOG(LogMetalShaderCompiler, Fatal, TEXT("Bad hlslcc header found"));
	}
	
	EShaderFrequency Frequency = (EShaderFrequency)ShaderOutput.Target.Frequency;
	const bool bIsMobile = (ShaderInput.Target.Platform == SP_METAL || ShaderInput.Target.Platform == SP_METAL_MRT || ShaderInput.Target.Platform == SP_METAL_TVOS || ShaderInput.Target.Platform == SP_METAL_MRT_TVOS);
	bool bNoFastMath = ShaderInput.Environment.CompilerFlags.Contains(CFLAG_NoFastMath);
	FString const* UsingWPO = ShaderInput.Environment.GetDefinitions().Find(TEXT("USES_WORLD_POSITION_OFFSET"));
	if (UsingWPO && FString("1") == *UsingWPO && (ShaderInput.Target.Platform == SP_METAL_MRT || ShaderInput.Target.Platform == SP_METAL_MRT_TVOS) && Frequency == SF_Vertex)
	{
		// WPO requires that we make all multiply/sincos instructions invariant :(
		bNoFastMath = true;
	}
	
	
	FMetalCodeHeader Header;
	Header.CompileFlags = (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Debug) ? (1 << CFLAG_Debug) : 0);
	Header.CompileFlags |= (bNoFastMath ? (1 << CFLAG_NoFastMath) : 0);
	Header.CompileFlags |= (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo) ? (1 << CFLAG_KeepDebugInfo) : 0);
	Header.CompileFlags |= (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_ZeroInitialise) ? (1 <<  CFLAG_ZeroInitialise) : 0);
	Header.CompileFlags |= (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_BoundsChecking) ? (1 << CFLAG_BoundsChecking) : 0);
	Header.CompileFlags |= (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive) ? (1 << CFLAG_Archive) : 0);
	Header.CompilerVersion = GetXcodeVersion(Header.CompilerBuild);
	Header.Version = Version;
	Header.SideTable = -1;
	Header.SourceLen = SourceCRCLen;
	Header.SourceCRC = SourceCRC;
    Header.Bindings.bDiscards = false;
	Header.Bindings.ConstantBuffers = ConstantBuffers;
	{
		Header.Bindings.TypedBuffers = TypedBuffers;
		for (uint32 i = 0; i < (uint32)TypedBufferFormats.Num(); i++)
		{
			if ((TypedBuffers & (1 << i)) != 0)
			{
				check(TypedBufferFormats[i] > (uint8)EMetalBufferFormat::Unknown);
                check(TypedBufferFormats[i] < (uint8)EMetalBufferFormat::Max);
                if ((TypeMode > EMetalTypeBufferModeRaw)
                && (TypeMode <= EMetalTypeBufferModeTB)
                && (TypedBufferFormats[i] < (uint8)EMetalBufferFormat::RGB8Sint || TypedBufferFormats[i] > (uint8)EMetalBufferFormat::RGB32Float)
                && (TypeMode == EMetalTypeBufferMode2D || TypeMode == EMetalTypeBufferModeTB || !(TypedUAVs & (1 << i))))
                {
                	Header.Bindings.LinearBuffer |= (1 << i);
	                Header.Bindings.TypedBuffers &= ~(1 << i);
                }
			}
		}
		
		if (Version == 6 || ShaderInput.Environment.CompilerFlags.Contains(CFLAG_ForceDXC))
		{
			Header.Bindings.LinearBuffer = Header.Bindings.TypedBuffers;
			Header.Bindings.TypedBuffers = 0;
		}
		
		// Raw mode means all buffers are invariant
		if (TypeMode == EMetalTypeBufferModeRaw)
		{
			Header.Bindings.TypedBuffers = 0;
		}
	}
	
	FShaderParameterMap& ParameterMap = ShaderOutput.ParameterMap;

	TBitArray<> UsedUniformBufferSlots;
	UsedUniformBufferSlots.Init(false,32);

	// Write out the magic markers.
	Header.Frequency = Frequency;

	// Only inputs for vertex shaders must be tracked.
	if (Frequency == SF_Vertex)
	{
		static const FString AttributePrefix = TEXT("in_ATTRIBUTE");
		for (auto& Input : CCHeader.Inputs)
		{
			// Only process attributes.
			if (Input.Name.StartsWith(AttributePrefix))
			{
				uint8 AttributeIndex = ParseNumber(*Input.Name + AttributePrefix.Len());
				Header.Bindings.InOutMask |= (1 << AttributeIndex);
			}
		}
	}

	// Then the list of outputs.
	static const FString TargetPrefix = "FragColor";
	static const FString TargetPrefix2 = "SV_Target";
	// Only outputs for pixel shaders must be tracked.
	if (Frequency == SF_Pixel)
	{
		for (auto& Output : CCHeader.Outputs)
		{
			// Handle targets.
			if (Output.Name.StartsWith(TargetPrefix))
			{
				uint8 TargetIndex = ParseNumber(*Output.Name + TargetPrefix.Len());
				Header.Bindings.InOutMask |= (1 << TargetIndex);
			}
			else if (Output.Name.StartsWith(TargetPrefix2))
			{
				uint8 TargetIndex = ParseNumber(*Output.Name + TargetPrefix2.Len());
				Header.Bindings.InOutMask |= (1 << TargetIndex);
			}
        }
		
		// For fragment shaders that discard but don't output anything we need at least a depth-stencil surface, so we need a way to validate this at runtime.
		if (FCStringAnsi::Strstr(USFSource, "[[ depth(") != nullptr || FCStringAnsi::Strstr(USFSource, "[[depth(") != nullptr)
		{
			Header.Bindings.InOutMask |= 0x8000;
		}
        
        // For fragment shaders that discard but don't output anything we need at least a depth-stencil surface, so we need a way to validate this at runtime.
        if (FCStringAnsi::Strstr(USFSource, "discard_fragment()") != nullptr)
        {
            Header.Bindings.bDiscards = true;
        }
	}

	// Then 'normal' uniform buffers.
	for (auto& UniformBlock : CCHeader.UniformBlocks)
	{
		uint16 UBIndex = UniformBlock.Index;
		if (UBIndex >= Header.Bindings.NumUniformBuffers)
		{
			Header.Bindings.NumUniformBuffers = UBIndex + 1;
		}
		UsedUniformBufferSlots[UBIndex] = true;
		ParameterMap.AddParameterAllocation(*UniformBlock.Name, UBIndex, 0, 0, EShaderParameterType::UniformBuffer);
	}

	// Packed global uniforms
	const uint16 BytesPerComponent = 4;
	TMap<ANSICHAR, uint16> PackedGlobalArraySize;
	for (auto& PackedGlobal : CCHeader.PackedGlobals)
	{
		ParameterMap.AddParameterAllocation(
			*PackedGlobal.Name,
			PackedGlobal.PackedType,
			PackedGlobal.Offset * BytesPerComponent,
			PackedGlobal.Count * BytesPerComponent,
			EShaderParameterType::LooseData
			);

		uint16& Size = PackedGlobalArraySize.FindOrAdd(PackedGlobal.PackedType);
		Size = FMath::Max<uint16>(BytesPerComponent * (PackedGlobal.Offset + PackedGlobal.Count), Size);
	}

	// Packed Uniform Buffers
	TMap<int, TMap<ANSICHAR, uint16> > PackedUniformBuffersSize;
	for (auto& PackedUB : CCHeader.PackedUBs)
	{
		for (auto& Member : PackedUB.Members)
		{
			ParameterMap.AddParameterAllocation(
												*Member.Name,
												EArrayType_FloatHighp,
												Member.Offset * BytesPerComponent,
												Member.Count * BytesPerComponent, 
												EShaderParameterType::LooseData
												);
			
			uint16& Size = PackedUniformBuffersSize.FindOrAdd(PackedUB.Attribute.Index).FindOrAdd(EArrayType_FloatHighp);
			Size = FMath::Max<uint16>(BytesPerComponent * (Member.Offset + Member.Count), Size);
		}
	}

	// Setup Packed Array info
	Header.Bindings.PackedGlobalArrays.Reserve(PackedGlobalArraySize.Num());
	for (auto Iterator = PackedGlobalArraySize.CreateIterator(); Iterator; ++Iterator)
	{
		ANSICHAR TypeName = Iterator.Key();
		uint16 Size = Iterator.Value();
		Size = (Size + 0xf) & (~0xf);
		CrossCompiler::FPackedArrayInfo Info;
		Info.Size = Size;
		Info.TypeName = TypeName;
		Info.TypeIndex = (uint8)CrossCompiler::PackedTypeNameToTypeIndex(TypeName);
		Header.Bindings.PackedGlobalArrays.Add(Info);
	}

	// Setup Packed Uniform Buffers info
	Header.Bindings.PackedUniformBuffers.Reserve(PackedUniformBuffersSize.Num());
	
	// In this mode there should only be 0 or 1 packed UB that contains all the aligned & named global uniform parameters
	check(PackedUniformBuffersSize.Num() <= 1);
	for (auto Iterator = PackedUniformBuffersSize.CreateIterator(); Iterator; ++Iterator)
	{
		int BufferIndex = Iterator.Key();
		auto& ArraySizes = Iterator.Value();
		for (auto IterSizes = ArraySizes.CreateIterator(); IterSizes; ++IterSizes)
		{
			ANSICHAR TypeName = IterSizes.Key();
			uint16 Size = IterSizes.Value();
			Size = (Size + 0xf) & (~0xf);
			CrossCompiler::FPackedArrayInfo Info;
			Info.Size = Size;
			Info.TypeName = TypeName;
			Info.TypeIndex = BufferIndex;
			Header.Bindings.PackedGlobalArrays.Add(Info);
		}
	}

	uint32 NumTextures = 0;
	
	// Then samplers.
	TMap<FString, uint32> SamplerMap;
	for (auto& Sampler : CCHeader.Samplers)
	{
		ParameterMap.AddParameterAllocation(
			*Sampler.Name,
			0,
			Sampler.Offset,
			Sampler.Count,
			EShaderParameterType::SRV
			);

		NumTextures += Sampler.Count;

		for (auto& SamplerState : Sampler.SamplerStates)
		{
			SamplerMap.Add(SamplerState, Sampler.Count);
		}
	}
	
	Header.Bindings.NumSamplers = CCHeader.SamplerStates.Num();

	// Then UAVs (images in Metal)
	for (auto& UAV : CCHeader.UAVs)
	{
		ParameterMap.AddParameterAllocation(
			*UAV.Name,
			0,
			UAV.Offset,
			UAV.Count,
			EShaderParameterType::UAV
			);

		Header.Bindings.NumUAVs = FMath::Max<uint8>(
			Header.Bindings.NumSamplers,
			UAV.Offset + UAV.Count
			);
	}

	for (auto& SamplerState : CCHeader.SamplerStates)
	{
		if (!SamplerMap.Contains(SamplerState.Name))
		{
			SamplerMap.Add(SamplerState.Name, 1);
		}
		
		ParameterMap.AddParameterAllocation(
			*SamplerState.Name,
			0,
			SamplerState.Index,
			SamplerMap[SamplerState.Name],
			EShaderParameterType::Sampler
			);
	}

	Header.NumThreadsX = CCHeader.NumThreads[0];
	Header.NumThreadsY = CCHeader.NumThreads[1];
	Header.NumThreadsZ = CCHeader.NumThreads[2];
	
	if (ShaderInput.Target.Platform == SP_METAL_SM5 && (Frequency == SF_Vertex || Frequency == SF_Hull || Frequency == SF_Domain))
	{
		FMetalTessellationHeader TessHeader;
		TessHeader.TessellationOutputControlPoints 		= CCHeader.TessellationOutputControlPoints;
		TessHeader.TessellationDomain					= CCHeader.TessellationDomain;
		TessHeader.TessellationInputControlPoints       = CCHeader.TessellationInputControlPoints;
		TessHeader.TessellationMaxTessFactor            = CCHeader.TessellationMaxTessFactor;
		TessHeader.TessellationOutputWinding			= CCHeader.TessellationOutputWinding;
		TessHeader.TessellationPartitioning				= CCHeader.TessellationPartitioning;
		TessHeader.TessellationPatchesPerThreadGroup    = CCHeader.TessellationPatchesPerThreadGroup;
		TessHeader.TessellationPatchCountBuffer         = CCHeader.TessellationPatchCountBuffer;
		TessHeader.TessellationIndexBuffer              = CCHeader.TessellationIndexBuffer;
		TessHeader.TessellationHSOutBuffer              = CCHeader.TessellationHSOutBuffer;
		TessHeader.TessellationHSTFOutBuffer            = CCHeader.TessellationHSTFOutBuffer;
		TessHeader.TessellationControlPointOutBuffer    = CCHeader.TessellationControlPointOutBuffer;
		TessHeader.TessellationControlPointIndexBuffer  = CCHeader.TessellationControlPointIndexBuffer;
		TessHeader.TessellationOutputAttribs            = TessOutputAttribs;

		Header.Tessellation.Add(TessHeader);
		
	}
	Header.bDeviceFunctionConstants				= (FCStringAnsi::Strstr(USFSource, "#define __METAL_DEVICE_CONSTANT_INDEX__ 1") != nullptr);
	Header.SideTable 							= CCHeader.SideTable;
	Header.Bindings.ArgumentBufferMasks			= CCHeader.ArgumentBuffers;
	Header.Bindings.ArgumentBuffers				= 0;
	for (auto const& Pair : Header.Bindings.ArgumentBufferMasks)
	{
		Header.Bindings.ArgumentBuffers |= (1 << Pair.Key);
	}
	
	// Build the SRT for this shader.
	{
		// Build the generic SRT for this shader.
		FShaderCompilerResourceTable GenericSRT;
		BuildResourceTableMapping(ShaderInput.Environment.ResourceTableMap, ShaderInput.Environment.ResourceTableLayoutHashes, UsedUniformBufferSlots, ShaderOutput.ParameterMap, GenericSRT);

		// Copy over the bits indicating which resource tables are active.
		Header.Bindings.ShaderResourceTable.ResourceTableBits = GenericSRT.ResourceTableBits;

		Header.Bindings.ShaderResourceTable.ResourceTableLayoutHashes = GenericSRT.ResourceTableLayoutHashes;

		// Now build our token streams.
		BuildResourceTableTokenStream(GenericSRT.TextureMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.TextureMap);
		BuildResourceTableTokenStream(GenericSRT.ShaderResourceViewMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.ShaderResourceViewMap);
		BuildResourceTableTokenStream(GenericSRT.SamplerMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.SamplerMap);
		BuildResourceTableTokenStream(GenericSRT.UnorderedAccessViewMap, GenericSRT.MaxBoundResourceTable, Header.Bindings.ShaderResourceTable.UnorderedAccessViewMap);

		Header.Bindings.NumUniformBuffers = FMath::Max((uint8)GetNumUniformBuffersUsed(GenericSRT), Header.Bindings.NumUniformBuffers);
	}

	FString MetalCode = FString(USFSource);
	if (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo) || ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Debug))
	{
		MetalCode.InsertAt(0, FString::Printf(TEXT("// %s\n"), *CCHeader.Name));
		Header.ShaderName = CCHeader.Name;

		//@TODO disabled but left for reference - seems to cause Metal shader compile errors at the moment
#if (0)
		if (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
		{
			static FString UE4StdLib((TCHAR*)FUTF8ToTCHAR((ANSICHAR const*)ue4_stdlib_metal, ue4_stdlib_metal_len).Get());
			MetalCode.ReplaceInline(TEXT("#include \"ue4_stdlib.metal\""), *UE4StdLib);
			MetalCode.ReplaceInline(TEXT("#pragma once"), TEXT(""));
		}
#endif
	}
	
	if (Header.Bindings.NumSamplers > MaxMetalSamplers)
	{
		ShaderOutput.bSucceeded = false;
		FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
		
		FString SamplerList;
		for (int32 i = 0; i < CCHeader.SamplerStates.Num(); i++)
		{
			auto const& Sampler = CCHeader.SamplerStates[i];
			SamplerList += FString::Printf(TEXT("%d:%s\n"), Sampler.Index, *Sampler.Name);
		}
		
		NewError->StrippedErrorMessage =
			FString::Printf(TEXT("shader uses %d (%d) samplers exceeding the limit of %d\nSamplers:\n%s"),
				Header.Bindings.NumSamplers, CCHeader.SamplerStates.Num(), MaxMetalSamplers, *SamplerList);
	}
	else if(CompileProcessAllowsRuntimeShaderCompiling(ShaderInput))
	{
		// Write out the header and shader source code.
		FMemoryWriter Ar(ShaderOutput.ShaderCode.GetWriteAccess(), true);
		uint8 PrecompiledFlag = 0;
		Ar << PrecompiledFlag;
		Ar << Header;
		Ar.Serialize((void*)USFSource, SourceLen + 1 - (USFSource - InShaderSource));
		
		// store data we can pickup later with ShaderCode.FindOptionalData('n'), could be removed for shipping
		ShaderOutput.ShaderCode.AddOptionalData('n', TCHAR_TO_UTF8(*ShaderInput.GenerateShaderName()));

		if (ShaderInput.ExtraSettings.bExtractShaderSource)
		{
			ShaderOutput.OptionalFinalShaderSource = MetalCode;
		}

		ShaderOutput.NumInstructions = NumLines;
		ShaderOutput.NumTextureSamplers = Header.Bindings.NumSamplers;
		ShaderOutput.bSucceeded = true;
	}
	else
	{
		uint64 XcodeBuildVers = 0;
		uint16 XcodeVers = GetXcodeVersion(XcodeBuildVers);
		uint16 XcodeMajorVers = ((XcodeVers >> 8) & 0xff);
		
        // metal commandlines
        FString DebugInfo = ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo) ? TEXT("-gline-tables-only") : TEXT("");
		if (XcodeMajorVers >= 10 && ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
		{
			DebugInfo += TEXT(" -MO");
		}
		
        FString MathMode = bNoFastMath ? TEXT("-fno-fast-math") : TEXT("-ffast-math");
        
		// at this point, the shader source is ready to be compiled
		// We need to use a temp directory path that will be consistent across devices so that debug info
		// can be loaded (as it must be at a consistent location).
#if PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING
		FString TempDir = TEXT("/tmp");
#else
		FString TempDir = FPlatformProcess::UserTempDir();
#endif
		
		int32 ReturnCode = 0;
		FString Results;
		FString Errors;
		bool bSucceeded = false;

		bool bRemoteBuildingConfigured = IsRemoteBuildingConfigured(&ShaderInput.Environment);
		
		EShaderPlatform ShaderPlatform = EShaderPlatform(ShaderInput.Target.Platform);
		
		const FString& MetalToolsPath = GetMetalToolsPath(ShaderPlatform);
		
		bool bMetalCompilerAvailable = false;

		if (((PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING) || bRemoteBuildingConfigured) && IsMetalCompilerAvailable(ShaderPlatform))
		{
			bMetalCompilerAvailable = true;
		}
		
		bool bDebugInfoSucceded = false;
		FMetalShaderBytecode Bytecode;
		FMetalShaderDebugInfo DebugCode;
		
		FString HashedName = FString::Printf(TEXT("%u_%u"), SourceCRCLen, SourceCRC);
		
		if(!bMetalCompilerAvailable)
		{
			// No Metal Compiler - just put the source code directly into /tmp and report error - we are now using text shaders when this was not the requested configuration
			// Move it into place using an atomic move - ensures only one compile "wins"
			FString InputFilename = (TempDir / HashedName) + TEXT(".metal");
			FString SaveFile = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderTemp"), TEXT(""));
			FFileHelper::SaveStringToFile(MetalCode, *SaveFile);
			IFileManager::Get().Move(*InputFilename, *SaveFile, false, false, true, true);
			IFileManager::Get().Delete(*SaveFile);
			
			TCHAR const* Message = nullptr;
			if (PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING)
			{
				Message = TEXT("Xcode's metal shader compiler was not found, verify Xcode has been installed on this Mac and that it has been selected in Xcode > Preferences > Locations > Command-line Tools.");
			}
			else if (!bRemoteBuildingConfigured)
			{
				Message = TEXT("Remote shader compilation has not been configured in the Editor settings for this project. Please follow the instructions for enabling remote compilation for iOS.");
			}
			else
			{
				Message = TEXT("Xcode's metal shader compiler was not found, verify Xcode has been installed on the Mac used for remote compilation and that the Mac is accessible via SSH from this machine.");
			}
			
			FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
			Error->ErrorVirtualFilePath = InputFilename;
			Error->ErrorLineString = TEXT("0");
			Error->StrippedErrorMessage = FString(Message);
			
			bRemoteBuildingConfigured = false;
		}
		else
		{
			// Compiler available - more intermediate files will be created - to avoid cross stream clashes - add uniqueness to our tmp folder - but uniqueness that can be reused so no random GUIDs.
			
			TCHAR const* CompileType = bRemoteBuildingConfigured ? TEXT("remotely") : TEXT("locally");
			
			bool bFoundStdLib = false;
			const FString& StdLibPath = GetMetalLibraryPath(ShaderPlatform);
			bFoundStdLib = RemoteFileExists(*StdLibPath);
			
			// PCHs need the same checksum to ensure that the result can be used with the current version of the file
			uint32 PchCRC = 0;
			uint32 PchLen = 0;
			bool const bChkSum = ChecksumRemoteFile(StdLibPath, &PchCRC, &PchLen);
			
			// PCHs need the modifiction time (in secs. since UTC Epoch) to ensure that the result can be used with the current version of the file
			uint64 ModTime = 0;
			bool const bModTime = ModificationTimeRemoteFile(StdLibPath, ModTime);
			const FString& CompilerVersion = GetMetalCompilerVersion(ShaderPlatform);

			static uint32 UE4StdLibCRCLen = ue4_stdlib_metal_len;
			static uint32 UE4StdLibCRC = 0;
			{
				if (UE4StdLibCRC == 0)
				{
					TArrayView<const uint8> UE4PCHData((const uint8*)ue4_stdlib_metal, ue4_stdlib_metal_len);
					FString UE4StdLibFilename = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderStdLib"), TEXT(""));
					if (FFileHelper::SaveArrayToFile(UE4PCHData, *UE4StdLibFilename))
					{
						FString RemoteTempPath = LocalPathToRemote(UE4StdLibFilename, MakeRemoteTempFolder(FString(TempDir)));
						CopyLocalFileToRemote(UE4StdLibFilename, RemoteTempPath);
						ChecksumRemoteFile(*RemoteTempPath, &UE4StdLibCRC, &UE4StdLibCRCLen);
						IFileManager::Get().Delete(*UE4StdLibFilename);
					}
				}
				
				if(UE4StdLibCRCLen != 0 && UE4StdLibCRC != 0 && PchLen != 0 && PchCRC != 0)
				{
					// If we need to add more items (e.g debug info, math mode, std) and this gets too long - convert to using a hash of all the required items instead
					TempDir /= FString::Printf(TEXT("UE4_%s_%hu_%u_%u_%u_%u"), *CompilerVersion, XcodeVers, UE4StdLibCRC, UE4StdLibCRCLen, PchCRC, PchLen);
				}
			}
			
			// Now write out the source metal file since we have added to the tempDir path
			FString MetalFilePath = (TempDir / HashedName) + TEXT(".metal");
			FString InputFilename = MetalFilePath;
			FString ObjFilename = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderObj"), TEXT(""));
			FString OutputFilename = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderOut"), TEXT(""));
			
			// Move it into place using an atomic move - ensures only one compile "wins"
			FString SaveFile = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderTemp"), TEXT(""));
			FFileHelper::SaveStringToFile(MetalCode, *SaveFile);
			IFileManager::Get().Move(*MetalFilePath, *SaveFile, false, false, true, true);
			IFileManager::Get().Delete(*SaveFile);
			
			bool bUseSharedPCH = false;
			FString MetalPCHFile;
			
			FString VersionedName = FString::Printf(TEXT("metal_stdlib_%u%u%llu%s%s%s%s%s%s%d.pch"), PchCRC, PchLen, ModTime, *GUIDHash.ToString(), *CompilerVersion, MinOSVersion, *DebugInfo, *MathMode, Standard, GetTypeHash(MetalToolsPath));
			
			// get rid of some not so filename-friendly characters ('=',' ' -> '_')
			VersionedName = VersionedName.Replace(TEXT("="), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
			
			MetalPCHFile = TempDir / VersionedName;
			FString RemoteMetalPCHFile = LocalPathToRemote(MetalPCHFile, TempDir);

			if(bFoundStdLib && bChkSum)
			{
				if(RemoteFileExists(*RemoteMetalPCHFile))
				{
					bUseSharedPCH = true;
				}
				else
				{
					FMetalShaderBytecodeJob Job;
					Job.ShaderFormat = ShaderInput.ShaderFormat;
					Job.Hash = GUIDHash;
					Job.TmpFolder = TempDir;
					Job.InputFile = StdLibPath;
					Job.OutputFile = MetalPCHFile;
					Job.CompilerVersion = CompilerVersion;
					Job.MinOSVersion = MinOSVersion;
					Job.DebugInfo = DebugInfo;
					Job.MathMode = MathMode;
					Job.Standard = Standard;
					Job.SourceCRCLen = PchLen;
					Job.SourceCRC = PchCRC;
					Job.bRetainObjectFile = false;
					Job.bCompileAsPCH = true;
					
					FMetalShaderBytecodeCooker* BytecodeCooker = new FMetalShaderBytecodeCooker(Job);
					bool bDataWasBuilt = false;
					TArray<uint8> OutData;
					bUseSharedPCH = GetDerivedDataCacheRef().GetSynchronous(BytecodeCooker, OutData, &bDataWasBuilt) && OutData.Num();
					if (bUseSharedPCH)
					{
						FMemoryReader Ar(OutData);
						Ar << Bytecode;
						
						if (!bDataWasBuilt)
						{
							FString TempPath = FPaths::CreateTempFilename(*TempDir, TEXT("MetalSharedPCH-"), TEXT(".metal.pch"));
							if (FFileHelper::SaveArrayToFile(Bytecode.OutputFile, *TempPath))
							{
								IFileManager::Get().Move(*MetalPCHFile, *TempPath, false, false, true, false);
								IFileManager::Get().Delete(*TempPath);
							}
							
							int64 FileSize = IFileManager::Get().FileSize(*MetalPCHFile);
							if(FileSize == Bytecode.OutputFile.Num())
							{
								bUseSharedPCH = true;
							}
							else
							{
								bUseSharedPCH = false;
								
								FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
								Error->ErrorVirtualFilePath = InputFilename;
								Error->ErrorLineString = TEXT("0");
								Error->StrippedErrorMessage = FString::Printf(TEXT("Metal Shared PCH failed to save %s to %s - compilation will continue without a PCH: %s."), CompileType, *TempPath, *MetalPCHFile);
							}
						}
					}
					else
					{
						FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
						Error->ErrorVirtualFilePath = InputFilename;
						Error->ErrorLineString = TEXT("0");
						Error->StrippedErrorMessage = FString::Printf(TEXT("Metal Shared PCH generation failed %s - compilation will continue without a PCH: %s."), CompileType, *Job.Message);
					}
				}
			}
			else
			{
				FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
				Error->ErrorVirtualFilePath = InputFilename;
				Error->ErrorLineString = TEXT("0");
				Error->StrippedErrorMessage = FString::Printf(TEXT("Metal Shared PCH generation failed - cannot find metal_stdlib header relative to %s %s."), *MetalToolsPath, CompileType);
			}
		
			uint32 DebugInfoHandle = 0;
			if (!bIsMobile && !ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive))
			{
				FMetalShaderDebugInfoJob Job;
				Job.ShaderFormat = ShaderInput.ShaderFormat;
				Job.Hash = GUIDHash;
				Job.CompilerVersion = CompilerVersion;
				Job.MinOSVersion = MinOSVersion;
				Job.DebugInfo = DebugInfo;
				Job.MathMode = MathMode;
				Job.Standard = Standard;
				Job.SourceCRCLen = SourceCRCLen;
				Job.SourceCRC = SourceCRC;
				
				Job.MetalCode = MetalCode;
				
				FMetalShaderDebugInfoCooker* DebugInfoCooker = new FMetalShaderDebugInfoCooker(Job);
				
				DebugInfoHandle = GetDerivedDataCacheRef().GetAsynchronous(DebugInfoCooker);
			}
			
			// Attempt to precompile the ue4_stdlib.metal file as a PCH, using the metal_stdlib PCH if it exists
			// Will fallback to just using the raw ue4_stdlib.metal file if PCH compilation fails
			// The ue4_stdlib.metal PCH is not cached in the DDC as modifications to the file invalidate the PCH, so it is only valid for this SCW's existence.
			FString UE4StdLibFilePath = TempDir / TEXT("ue4_stdlib.metal");
			static FString RemoteUE4StdLibFolder = MakeRemoteTempFolder(TempDir);
			FString RemoteUE4StdLibFilePath = LocalPathToRemote(UE4StdLibFilePath, RemoteUE4StdLibFolder);
			{
				uint32 RemotePchCRC = 0;
				uint32 RemotePchLen = 0;
				if (!RemoteFileExists(RemoteUE4StdLibFilePath) || !ChecksumRemoteFile(*RemoteUE4StdLibFilePath, &RemotePchCRC, &RemotePchLen) || RemotePchCRC != UE4StdLibCRC)
				{
					TArrayView<const uint8> UE4PCHData((const uint8*)ue4_stdlib_metal, ue4_stdlib_metal_len);
					FString UE4StdLibFilename = FPaths::CreateTempFilename(*TempDir, TEXT("ShaderStdLib"), TEXT(""));
					if (FFileHelper::SaveArrayToFile(UE4PCHData, *UE4StdLibFilename))
					{
						IFileManager::Get().Move(*UE4StdLibFilePath, *UE4StdLibFilename, false, false, true, true);
						IFileManager::Get().Delete(*UE4StdLibFilename);
					}
					CopyLocalFileToRemote(UE4StdLibFilePath, RemoteUE4StdLibFilePath);
				}
				
#if (PLATFORM_MAC && !UNIXLIKE_TO_MAC_REMOTE_BUILDING)				
				FString Defines = Header.bDeviceFunctionConstants ? TEXT("-D__METAL_DEVICE_CONSTANT_INDEX__=1") : TEXT("");
				Defines += FString::Printf(TEXT(" -D__METAL_USE_TEXTURE_CUBE_ARRAY__=%d"), !bIsMobile);
				switch(TypeMode)
				{
					case EMetalTypeBufferModeRaw:
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_READ_IMPL__=0");
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_RW_IMPL__=0");
						break;
					case EMetalTypeBufferMode2DSRV:
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_READ_IMPL__=1");
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_RW_IMPL__=0");
						break;
					case EMetalTypeBufferModeTBSRV:
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_READ_IMPL__=3");
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_RW_IMPL__=0");
						break;
					case EMetalTypeBufferMode2D:
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_READ_IMPL__=1");
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_RW_IMPL__=1");
						break;
					case EMetalTypeBufferModeTB:
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_READ_IMPL__=3");
						Defines += TEXT(" -D__METAL_TYPED_BUFFER_RW_IMPL__=3");
						break;
					default:
						break;
				}
				
				int64 UnixTime = IFileManager::Get().GetTimeStamp(*UE4StdLibFilePath).ToUnixTimestamp();
				FString UE4StdLibFilePCH = FString::Printf(TEXT("%s.%u%u%u%u%s%s%s%s%s%s%d%d%lld.pch"), *UE4StdLibFilePath, UE4StdLibCRC, UE4StdLibCRCLen, PchCRC, PchLen, *GUIDHash.ToString(), *CompilerVersion, MinOSVersion, *DebugInfo, *MathMode, Standard, GetTypeHash(MetalToolsPath), GetTypeHash(Defines), UnixTime);
				FString RemoteUE4StdLibFilePCH = LocalPathToRemote(UE4StdLibFilePCH, RemoteUE4StdLibFolder);
				if (RemoteFileExists(RemoteUE4StdLibFilePath) && !IFileManager::Get().FileExists(*UE4StdLibFilePCH) && !RemoteFileExists(RemoteUE4StdLibFilePCH))
				{
					FMetalShaderBytecodeJob Job;
					Job.ShaderFormat = ShaderInput.ShaderFormat;
					Job.Hash = GUIDHash;
					Job.TmpFolder = TempDir;
					Job.InputFile = RemoteUE4StdLibFilePath;
					Job.OutputFile = RemoteUE4StdLibFilePCH;
					Job.CompilerVersion = CompilerVersion;
					Job.MinOSVersion = MinOSVersion;
					Job.DebugInfo = DebugInfo;
					Job.MathMode = MathMode;
					Job.Standard = Standard;
					Job.SourceCRCLen = ue4_stdlib_metal_len;
					Job.SourceCRC = FCrc::MemCrc32(ue4_stdlib_metal, Job.SourceCRCLen);
					Job.bRetainObjectFile = false;
					Job.bCompileAsPCH = true;
                    Job.Defines = Defines;
					
					FMetalShaderBytecodeCooker Cooker(Job);
					TArray<uint8> Data;
					Cooker.Build(Data);
				}
				
				if (IFileManager::Get().FileExists(*UE4StdLibFilePCH) && RemoteFileExists(RemoteUE4StdLibFilePath))
				{
					if (bUseSharedPCH)
					{
						CopyLocalFileToRemote(MetalPCHFile, RemoteMetalPCHFile);
					}
					MetalPCHFile = UE4StdLibFilePCH;
					bUseSharedPCH = true;
				}
#endif
			}
			
			FMetalShaderBytecodeJob Job;
			Job.ShaderFormat = ShaderInput.ShaderFormat;
			Job.Hash = GUIDHash;
			Job.TmpFolder = TempDir;
			Job.InputFile = InputFilename;
			// With the debug-info enabled don't use a shared PCH, should help resolve issues with shader debugging.
			if (bUseSharedPCH && !ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
			{
				Job.InputPCHFile = MetalPCHFile;
			}
			Job.OutputFile = OutputFilename;
			Job.OutputObjectFile = ObjFilename;
			Job.CompilerVersion = CompilerVersion;
			Job.MinOSVersion = MinOSVersion;
			Job.DebugInfo = DebugInfo;
			Job.MathMode = MathMode;
			Job.Standard = Standard;
			Job.SourceCRCLen = SourceCRCLen;
			Job.SourceCRC = SourceCRC;
			Job.bRetainObjectFile = ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive);
			Job.bCompileAsPCH = false;
			Job.IncludeDir = RemoteUE4StdLibFolder;

			FMetalShaderBytecodeCooker* BytecodeCooker = new FMetalShaderBytecodeCooker(Job);
			
			bool bDataWasBuilt = false;
			TArray<uint8> OutData;
			bSucceeded = GetDerivedDataCacheRef().GetSynchronous(BytecodeCooker, OutData, &bDataWasBuilt);
			if (bSucceeded)
			{
				if (OutData.Num())
				{
					FMemoryReader Ar(OutData);
					Ar << Bytecode;
					
					if (!bIsMobile && !ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive))
					{
						GetDerivedDataCacheRef().WaitAsynchronousCompletion(DebugInfoHandle);
						TArray<uint8> DebugData;
						bDebugInfoSucceded = GetDerivedDataCacheRef().GetAsynchronousResults(DebugInfoHandle, DebugData);
						if (bDebugInfoSucceded)
						{
							if (DebugData.Num())
							{
								FMemoryReader DebugAr(DebugData);
								DebugAr << DebugCode;
							}
						}
					}
				}
				else
				{
					FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
					Error->ErrorVirtualFilePath = InputFilename;
					Error->ErrorLineString = TEXT("0");
					Error->StrippedErrorMessage = FString::Printf(TEXT("DDC returned empty byte array despite claiming that the bytecode was built successfully."));
				}
			}
			else
			{
				FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();
				Error->ErrorVirtualFilePath = InputFilename;
				Error->ErrorLineString = TEXT("0");
				Error->StrippedErrorMessage = Job.Message;
			}
		}
		
		if (bSucceeded)
		{
			// Write out the header and compiled shader code
			FMemoryWriter Ar(ShaderOutput.ShaderCode.GetWriteAccess(), true);
			uint8 PrecompiledFlag = 1;
			Ar << PrecompiledFlag;
			Ar << Header;

			// jam it into the output bytes
			Ar.Serialize(Bytecode.OutputFile.GetData(), Bytecode.OutputFile.Num());

			if (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive))
			{
				ShaderOutput.ShaderCode.AddOptionalData('o', Bytecode.ObjectFile.GetData(), Bytecode.ObjectFile.Num());
			}
			
			if (bDebugInfoSucceded && !ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive) && DebugCode.CompressedData.Num())
			{
				ShaderOutput.ShaderCode.AddOptionalData('z', DebugCode.CompressedData.GetData(), DebugCode.CompressedData.Num());
				ShaderOutput.ShaderCode.AddOptionalData('p', TCHAR_TO_UTF8(*Bytecode.NativePath));
				ShaderOutput.ShaderCode.AddOptionalData('u', (const uint8*)&DebugCode.UncompressedSize, sizeof(DebugCode.UncompressedSize));
			}
			
			if (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
			{
				// store data we can pickup later with ShaderCode.FindOptionalData('n'), could be removed for shipping
				ShaderOutput.ShaderCode.AddOptionalData('n', TCHAR_TO_UTF8(*ShaderInput.GenerateShaderName()));
				if (DebugCode.CompressedData.Num() == 0)
				{
					ShaderOutput.ShaderCode.AddOptionalData('c', TCHAR_TO_UTF8(*MetalCode));
					ShaderOutput.ShaderCode.AddOptionalData('p', TCHAR_TO_UTF8(*Bytecode.NativePath));
				}
			}
			else if (ShaderInput.Environment.CompilerFlags.Contains(CFLAG_Archive))
			{
				ShaderOutput.ShaderCode.AddOptionalData('c', TCHAR_TO_UTF8(*MetalCode));
				ShaderOutput.ShaderCode.AddOptionalData('p', TCHAR_TO_UTF8(*Bytecode.NativePath));
			}
			
			ShaderOutput.NumTextureSamplers = Header.Bindings.NumSamplers;
		}

		if (ShaderInput.ExtraSettings.bExtractShaderSource)
		{
			ShaderOutput.OptionalFinalShaderSource = MetalCode;
		}
		
		ShaderOutput.NumInstructions = NumLines;
		ShaderOutput.bSucceeded = bSucceeded;
	}
}

/*------------------------------------------------------------------------------
	External interface.
------------------------------------------------------------------------------*/

static const EHlslShaderFrequency FrequencyTable[] =
{
	HSF_VertexShader,
	HSF_HullShader,
	HSF_DomainShader,
	HSF_PixelShader,
	HSF_InvalidFrequency,
	HSF_ComputeShader
};

FString CreateRemoteDataFromEnvironment(const FShaderCompilerEnvironment& Environment)
{
	FString Line = TEXT("\n#if 0 /*BEGIN_REMOTE_SERVER*/\n");
	for (auto Pair : Environment.RemoteServerData)
	{
		Line += FString::Printf(TEXT("%s=%s\n"), *Pair.Key, *Pair.Value);
	}
	Line += TEXT("#endif /*END_REMOTE_SERVER*/\n");
	return Line;
}

void CreateEnvironmentFromRemoteData(const FString& String, FShaderCompilerEnvironment& OutEnvironment)
{
	FString Prolog = TEXT("#if 0 /*BEGIN_REMOTE_SERVER*/");
	int32 FoundBegin = String.Find(Prolog, ESearchCase::CaseSensitive);
	if (FoundBegin == INDEX_NONE)
	{
		return;
	}
	int32 FoundEnd = String.Find(TEXT("#endif /*END_REMOTE_SERVER*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FoundBegin);
	if (FoundEnd == INDEX_NONE)
	{
		return;
	}

	// +1 for EOL
	const TCHAR* Ptr = &String[FoundBegin + 1 + Prolog.Len()];
	const TCHAR* PtrEnd = &String[FoundEnd];
	while (Ptr < PtrEnd)
	{
		FString Key;
		if (!CrossCompiler::ParseIdentifier(Ptr, Key))
		{
			return;
		}
		if (!CrossCompiler::Match(Ptr, TEXT("=")))
		{
			return;
		}
		FString Value;
		if (!CrossCompiler::ParseString(Ptr, Value))
		{
			return;
		}
		if (!CrossCompiler::Match(Ptr, '\n'))
		{
			return;
		}
		OutEnvironment.RemoteServerData.FindOrAdd(Key) = Value;
	}
}

void CompileShader_Metal(const FShaderCompilerInput& _Input,FShaderCompilerOutput& Output,const FString& WorkingDirectory)
{
	auto Input = _Input;
	FString PreprocessedShader;
	FShaderCompilerDefinitions AdditionalDefines;
	EHlslCompileTarget HlslCompilerTarget = HCT_FeatureLevelES3_1; // Always ES3.1 for now due to the way RCO has configured the MetalBackend
	EHlslCompileTarget MetalCompilerTarget = HCT_FeatureLevelES3_1; // Varies depending on the actual intended Metal target.

	// Work out which standard we need, this is dependent on the shader platform.
	const bool bIsMobile = (Input.Target.Platform == SP_METAL || Input.Target.Platform == SP_METAL_MRT || Input.Target.Platform == SP_METAL_TVOS || Input.Target.Platform == SP_METAL_MRT_TVOS);
	TCHAR const* StandardPlatform = nullptr;
	if (bIsMobile)
	{
		StandardPlatform = TEXT("ios");
		AdditionalDefines.SetDefine(TEXT("IOS"), 1);
	}
	else
	{
		StandardPlatform = TEXT("macos");
		AdditionalDefines.SetDefine(TEXT("MAC"), 1);
	}
	
	AdditionalDefines.SetDefine(TEXT("COMPILER_METAL"), 1);

	static FName NAME_SF_METAL(TEXT("SF_METAL"));
	static FName NAME_SF_METAL_MRT(TEXT("SF_METAL_MRT"));
	static FName NAME_SF_METAL_TVOS(TEXT("SF_METAL_TVOS"));
	static FName NAME_SF_METAL_MRT_TVOS(TEXT("SF_METAL_MRT_TVOS"));
	static FName NAME_SF_METAL_SM5_NOTESS(TEXT("SF_METAL_SM5_NOTESS"));
	static FName NAME_SF_METAL_SM5(TEXT("SF_METAL_SM5"));
	static FName NAME_SF_METAL_MACES3_1(TEXT("SF_METAL_MACES3_1"));
	static FName NAME_SF_METAL_MRT_MAC(TEXT("SF_METAL_MRT_MAC"));
	
    EMetalGPUSemantics Semantics = EMetalGPUSemanticsMobile;
	
	FString const* MaxVersion = Input.Environment.GetDefinitions().Find(TEXT("MAX_SHADER_LANGUAGE_VERSION"));
    uint8 VersionEnum = 0;
    if (MaxVersion)
    {
        if(MaxVersion->IsNumeric())
        {
            LexFromString(VersionEnum, *(*MaxVersion));
        }
    }
	
	// The new compiler is only available on Mac or Windows for the moment.
#if !(PLATFORM_MAC || PLATFORM_WINDOWS)
	VersionEnum = FMath::Min(VersionEnum, (uint8)5);
#endif
	
	bool bAppleTV = (Input.ShaderFormat == NAME_SF_METAL_TVOS || Input.ShaderFormat == NAME_SF_METAL_MRT_TVOS);
    if (Input.ShaderFormat == NAME_SF_METAL || Input.ShaderFormat == NAME_SF_METAL_TVOS)
	{
		UE_CLOG(VersionEnum < 2, LogShaders, Warning, TEXT("Metal shader version must be Metal v1.2 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
        VersionEnum = VersionEnum >= 2 ? VersionEnum : 2;
        AdditionalDefines.SetDefine(TEXT("METAL_PROFILE"), 1);
	}
	else if (Input.ShaderFormat == NAME_SF_METAL_MRT || Input.ShaderFormat == NAME_SF_METAL_MRT_TVOS)
	{
		UE_CLOG(VersionEnum < 2, LogShaders, Warning, TEXT("Metal shader version must be Metal v1.2 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
		AdditionalDefines.SetDefine(TEXT("METAL_MRT_PROFILE"), 1);
		VersionEnum = VersionEnum >= 2 ? VersionEnum : 2;
		MetalCompilerTarget = HCT_FeatureLevelSM5;
		Semantics = EMetalGPUSemanticsTBDRDesktop;
	}
	else if (Input.ShaderFormat == NAME_SF_METAL_MACES3_1)
	{
        UE_CLOG(VersionEnum < 3, LogShaders, Warning, TEXT("Metal shader version must be Metal v2.0 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
		AdditionalDefines.SetDefine(TEXT("METAL_PROFILE"), 1);
		VersionEnum = VersionEnum >= 3 ? VersionEnum : 3;
		MetalCompilerTarget = HCT_FeatureLevelES3_1;
		Semantics = EMetalGPUSemanticsImmediateDesktop;
	}
	else if (Input.ShaderFormat == NAME_SF_METAL_SM5_NOTESS)
	{
        UE_CLOG(VersionEnum < 3, LogShaders, Warning, TEXT("Metal shader version must be Metal v2.0 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
		AdditionalDefines.SetDefine(TEXT("METAL_SM5_NOTESS_PROFILE"), 1);
		AdditionalDefines.SetDefine(TEXT("USING_VERTEX_SHADER_LAYER"), 1);
		VersionEnum = VersionEnum >= 3 ? VersionEnum : 3;
		MetalCompilerTarget = HCT_FeatureLevelSM5;
		Semantics = EMetalGPUSemanticsImmediateDesktop;
	}
	else if (Input.ShaderFormat == NAME_SF_METAL_SM5)
	{
        UE_CLOG(VersionEnum < 3, LogShaders, Warning, TEXT("Metal shader version must be Metal v2.0 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
		AdditionalDefines.SetDefine(TEXT("METAL_SM5_PROFILE"), 1);
		AdditionalDefines.SetDefine(TEXT("USING_VERTEX_SHADER_LAYER"), 1);
		VersionEnum = VersionEnum >= 3 ? VersionEnum : 3;
		MetalCompilerTarget = HCT_FeatureLevelSM5;
		Semantics = EMetalGPUSemanticsImmediateDesktop;
	}
	else if (Input.ShaderFormat == NAME_SF_METAL_MRT_MAC)
	{
        UE_CLOG(VersionEnum < 3, LogShaders, Warning, TEXT("Metal shader version must be Metal v2.0 or higher for format %s!"), VersionEnum, *Input.ShaderFormat.ToString());
		AdditionalDefines.SetDefine(TEXT("METAL_MRT_PROFILE"), 1);
		VersionEnum = VersionEnum >= 3 ? VersionEnum : 3;
		MetalCompilerTarget = HCT_FeatureLevelSM5;
		Semantics = EMetalGPUSemanticsTBDRDesktop;
	}
	else
	{
		Output.bSucceeded = false;
		new(Output.Errors) FShaderCompilerError(*FString::Printf(TEXT("Invalid shader format '%s' passed to compiler."), *Input.ShaderFormat.ToString()));
		return;
	}
	

	bool const bUseSC = Input.Environment.CompilerFlags.Contains(CFLAG_ForceDXC);
	if (bUseSC)
	{
        AdditionalDefines.SetDefine(TEXT("COMPILER_HLSLCC"), 2);
	}
	else
	{
        AdditionalDefines.SetDefine(TEXT("COMPILER_HLSLCC"), 1);
		AdditionalDefines.SetDefine(TEXT("row_major"), TEXT(""));
	}
	
    EMetalTypeBufferMode TypeMode = EMetalTypeBufferModeRaw;
	FString MinOSVersion;
	FString StandardVersion;
	switch(VersionEnum)
    {
		case 6:
        case 5:
            // Enable full SM5 feature support so tessellation & fragment UAVs compile
            TypeMode = EMetalTypeBufferModeTB;
            HlslCompilerTarget = HCT_FeatureLevelSM5;
            StandardVersion = TEXT("2.1");
            if (bAppleTV)
            {
                MinOSVersion = TEXT("-mtvos-version-min=12.0");
            }
            else if (bIsMobile)
            {
                MinOSVersion = TEXT("-mios-version-min=12.0");
            }
            else
            {
                MinOSVersion = TEXT("-mmacosx-version-min=10.14");
            }
            break;
		case 4:
			// Enable full SM5 feature support so tessellation & fragment UAVs compile
			TypeMode = EMetalTypeBufferModeTB;
            HlslCompilerTarget = HCT_FeatureLevelSM5;
			StandardVersion = TEXT("2.1");
			if (bAppleTV)
			{
				MinOSVersion = TEXT("-mtvos-version-min=12.0");
				TypeMode = EMetalTypeBufferModeTBSRV;
			}
			else if (bIsMobile)
			{
				MinOSVersion = TEXT("-mios-version-min=12.0");
				TypeMode = EMetalTypeBufferModeTBSRV;
			}
			else
			{
				MinOSVersion = TEXT("-mmacosx-version-min=10.14");
			}
			break;
		case 3:
			// Enable full SM5 feature support so tessellation & fragment UAVs compile
			TypeMode = EMetalTypeBufferMode2D;
            HlslCompilerTarget = HCT_FeatureLevelSM5;
			StandardVersion = TEXT("2.0");
			if (bAppleTV)
			{
				MinOSVersion = TEXT("-mtvos-version-min=11.0");
				TypeMode = EMetalTypeBufferMode2DSRV;
			}
			else if (bIsMobile)
			{
				MinOSVersion = TEXT("-mios-version-min=11.0");
				TypeMode = EMetalTypeBufferMode2DSRV;
			}
			else
			{
				MinOSVersion = TEXT("-mmacosx-version-min=10.13");
			}
			break;
		case 2:
			// Enable full SM5 feature support so tessellation & fragment UAVs compile
			TypeMode = EMetalTypeBufferMode2D;
            HlslCompilerTarget = HCT_FeatureLevelSM5;
			StandardVersion = TEXT("1.2");
			if (bAppleTV)
			{
				MinOSVersion = TEXT("-mtvos-version-min=10.0");
				TypeMode = EMetalTypeBufferMode2DSRV;
			}
			else if (bIsMobile)
			{
				MinOSVersion = TEXT("-mios-version-min=10.0");
				TypeMode = EMetalTypeBufferMode2DSRV;
			}
			else
			{
				Output.bSucceeded = false;
				FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
				NewError->StrippedErrorMessage = FString::Printf(
															 TEXT("Metal %s is no longer supported in UE4 for macOS."),
															 *StandardVersion
															 );
				return;
			}
			break;
		case 1:
		{
			HlslCompilerTarget = bIsMobile ? HlslCompilerTarget : HCT_FeatureLevelSM5;
			StandardVersion = TEXT("1.1");
			MinOSVersion = bIsMobile ? TEXT("") : TEXT("-mmacosx-version-min=10.11");
			
			Output.bSucceeded = false;
			FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
			NewError->StrippedErrorMessage = FString::Printf(
														 TEXT("Metal %s is no longer supported in UE4."),
														 *StandardVersion
														 );
			return;
		}
		case 0:
		default:
		{
			check(bIsMobile);
			StandardVersion = TEXT("1.0");
			MinOSVersion = TEXT("");
			
			Output.bSucceeded = false;
			FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
			NewError->StrippedErrorMessage = FString::Printf(
															 TEXT("Metal %s is no longer supported in UE4."),
															 *StandardVersion
															 );
			return;
		}
	}
	
	// Force floats if the material requests it
	const bool bUseFullPrecisionInPS = Input.Environment.CompilerFlags.Contains(CFLAG_UseFullPrecisionInPS);
	if (bUseFullPrecisionInPS || (VersionEnum < 2)) // Too many bugs in Metal 1.0 & 1.1 with half floats the more time goes on and the compiler stack changes
	{
		AdditionalDefines.SetDefine(TEXT("FORCE_FLOATS"), (uint32)1);
	}
	
	FString Standard = FString::Printf(TEXT("-std=%s-metal%s"), StandardPlatform, *StandardVersion);
	
	bool const bDirectCompile = FParse::Param(FCommandLine::Get(), TEXT("directcompile"));
	if (bDirectCompile)
	{
		Input.DumpDebugInfoPath = FPaths::GetPath(Input.VirtualSourceFilePath);
	}
	
	const bool bDumpDebugInfo = (Input.DumpDebugInfoPath != TEXT("") && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath));

	// Allow the shader pipeline to override the platform default in here.
	uint32 MaxUnrollLoops = 32;
	if (Input.Environment.CompilerFlags.Contains(CFLAG_AvoidFlowControl))
	{
		AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)0);
		MaxUnrollLoops = 1024; // Max. permitted by hlslcc
	}
	else if (Input.Environment.CompilerFlags.Contains(CFLAG_PreferFlowControl))
	{
		AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)0);
		MaxUnrollLoops = 0;
	}
	else
	{
		AdditionalDefines.SetDefine(TEXT("COMPILER_SUPPORTS_ATTRIBUTES"), (uint32)1);
	}

	if (!Input.bSkipPreprocessedCache && !bDirectCompile)
	{
		FString const* UsingTessellationDefine = Input.Environment.GetDefinitions().Find(TEXT("USING_TESSELLATION"));
		bool bUsingTessellation = (UsingTessellationDefine != nullptr && FString("1") == *UsingTessellationDefine);
		if (bUsingTessellation && (Input.Target.Frequency == SF_Vertex))
		{
			// force HULLSHADER on so that VS that is USING_TESSELLATION can be built together with the proper HS
			FString const* VertexShaderDefine = Input.Environment.GetDefinitions().Find(TEXT("VERTEXSHADER"));
			check(VertexShaderDefine && FString("1") == *VertexShaderDefine);
			FString const* HullShaderDefine = Input.Environment.GetDefinitions().Find(TEXT("HULLSHADER"));
			check(HullShaderDefine && FString("0") == *HullShaderDefine);
			Input.Environment.SetDefine(TEXT("HULLSHADER"), 1u);
		}
		if (Input.Target.Frequency == SF_Hull)
		{
			check(bUsingTessellation);
			// force VERTEXSHADER on so that HS that is USING_TESSELLATION can be built together with the proper VS
			FString const* VertexShaderDefine = Input.Environment.GetDefinitions().Find(TEXT("VERTEXSHADER"));
			check(VertexShaderDefine && FString("0") == *VertexShaderDefine);
			FString const* HullShaderDefine = Input.Environment.GetDefinitions().Find(TEXT("HULLSHADER"));
			check(HullShaderDefine && FString("1") == *HullShaderDefine);

			// enable VERTEXSHADER so that this HS will hash uniquely with its associated VS
			// We do not want a given HS to be shared among numerous VS'Sampler
			// this should accomplish that goal -- see GenerateOutputHash
			Input.Environment.SetDefine(TEXT("VERTEXSHADER"), 1u);
		}
	}

	if (Input.bSkipPreprocessedCache)
	{
		if (!FFileHelper::LoadFileToString(PreprocessedShader, *Input.VirtualSourceFilePath))
		{
			return;
		}

		// Remove const as we are on debug-only mode
		CrossCompiler::CreateEnvironmentFromResourceTable(PreprocessedShader, (FShaderCompilerEnvironment&)Input.Environment);
		CreateEnvironmentFromRemoteData(PreprocessedShader, (FShaderCompilerEnvironment&)Input.Environment);
	}
	else
	{
		if (!PreprocessShader(PreprocessedShader, Output, Input, AdditionalDefines))
		{
			// The preprocessing stage will add any relevant errors.
			return;
		}
	}

	char* MetalShaderSource = NULL;
	char* ErrorLog = NULL;

	const EHlslShaderFrequency Frequency = FrequencyTable[Input.Target.Frequency];
	if (Frequency == HSF_InvalidFrequency)
	{
		Output.bSucceeded = false;
		FShaderCompilerError* NewError = new(Output.Errors) FShaderCompilerError();
		NewError->StrippedErrorMessage = FString::Printf(
			TEXT("%s shaders not supported for use in Metal."),
			CrossCompiler::GetFrequencyName((EShaderFrequency)Input.Target.Frequency)
			);
		return;
	}

	FShaderParameterParser ShaderParameterParser;
	if (!ShaderParameterParser.ParseAndMoveShaderParametersToRootConstantBuffer(
		Input, Output, PreprocessedShader, /* ConstantBufferType = */ nullptr))
	{
		// The FShaderParameterParser will add any relevant errors.
		return;
	}

	// This requires removing the HLSLCC_NoPreprocess flag later on!
	RemoveUniformBuffersFromSource(Input.Environment, PreprocessedShader);
	
	uint32 CCFlags = HLSLCC_NoPreprocess | HLSLCC_PackUniformsIntoUniformBufferWithNames | HLSLCC_FixAtomicReferences | HLSLCC_RetainSizes | HLSLCC_KeepSamplerAndImageNames;
	if (!bDirectCompile || UE_BUILD_DEBUG)
	{
		// Validation is expensive - only do it when compiling directly for debugging
		CCFlags |= HLSLCC_NoValidation;
	}

	// Required as we added the RemoveUniformBuffersFromSource() function (the cross-compiler won't be able to interpret comments w/o a preprocessor)
	CCFlags &= ~HLSLCC_NoPreprocess;

	// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
	if (bDumpDebugInfo && !bDirectCompile)
	{
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / FPaths::GetBaseFilename(Input.GetSourceFilename() + TEXT(".usf"))));
		if (FileWriter)
		{
			auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShader);
			FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
			{
				FString Line = CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);
				FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());

				// add the remote data if necessary
//				if (IsRemoteBuildingConfigured(&Input.Environment))
				{
					Line = CreateRemoteDataFromEnvironment(Input.Environment);
					FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
				}
			}
			FileWriter->Close();
			delete FileWriter;
		}

		if (Input.bGenerateDirectCompileFile)
		{
			FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input, CCFlags), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
		}
	}

	FSHAHash GUIDHash;
	if (!bDirectCompile)
	{
		TArray<FString> GUIDFiles;
		GUIDFiles.Add(FPaths::ConvertRelativePathToFull(TEXT("/Engine/Public/Platform/Metal/MetalCommon.ush")));
		GUIDFiles.Add(FPaths::ConvertRelativePathToFull(TEXT("/Engine/Public/ShaderVersion.ush")));
		GUIDHash = GetShaderFilesHash(GUIDFiles, Input.Target.GetPlatform());
	}
	else
	{
		FGuid Guid = FGuid::NewGuid();
		FSHA1::HashBuffer(&Guid, sizeof(FGuid), GUIDHash.Hash);
	}

	FMetalShaderOutputCooker* Cooker = new FMetalShaderOutputCooker(Input,Output,WorkingDirectory, PreprocessedShader, GUIDHash, VersionEnum, CCFlags, HlslCompilerTarget, MetalCompilerTarget, Semantics, TypeMode, MaxUnrollLoops, Frequency, bDumpDebugInfo, Standard, MinOSVersion);
		
	bool bDataWasBuilt = false;
	TArray<uint8> OutData;
	bool bCompiled = GetDerivedDataCacheRef().GetSynchronous(Cooker, OutData, &bDataWasBuilt) && OutData.Num();
	Output.bSucceeded = bCompiled;
	if (bCompiled && !bDataWasBuilt)
	{
		FShaderCompilerOutput TestOutput;
		FMemoryReader Reader(OutData);
		Reader << TestOutput;
			
		// If successful update the header & optional data to provide the proper material name
		if (TestOutput.bSucceeded)
		{
			TArray<uint8> const& Code = TestOutput.ShaderCode.GetReadAccess();
				
			// Parse the existing data and extract the source code. We have to recompile it
			FShaderCodeReader ShaderCode(Code);
			FMemoryReader Ar(Code, true);
			Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());
				
			// was the shader already compiled offline?
			uint8 OfflineCompiledFlag;
			Ar << OfflineCompiledFlag;
			check(OfflineCompiledFlag == 0 || OfflineCompiledFlag == 1);
				
			// get the header
			FMetalCodeHeader Header;
			Ar << Header;
				
			// remember where the header ended and code (precompiled or source) begins
			int32 CodeOffset = Ar.Tell();
			uint32 CodeSize = ShaderCode.GetActualShaderCodeSize() - CodeOffset;
			const uint8* SourceCodePtr = (uint8*)Code.GetData() + CodeOffset;
				
			// Copy the non-optional shader bytecode
			TArray<uint8> SourceCode;
			SourceCode.Append(SourceCodePtr, ShaderCode.GetActualShaderCodeSize() - CodeOffset);
				
			// store data we can pickup later with ShaderCode.FindOptionalData('n'), could be removed for shipping
			ANSICHAR const* Text = ShaderCode.FindOptionalData('c');
			ANSICHAR const* Path = ShaderCode.FindOptionalData('p');
			ANSICHAR const* Name = ShaderCode.FindOptionalData('n');
				
			int32 ObjectSize = 0;
			uint8 const* Object = ShaderCode.FindOptionalDataAndSize('o', ObjectSize);
				
			int32 DebugSize = 0;
			uint8 const* Debug = ShaderCode.FindOptionalDataAndSize('z', DebugSize);
				
			int32 UncSize = 0;
			uint8 const* UncData = ShaderCode.FindOptionalDataAndSize('u', UncSize);
				
			// Replace the shader name.
			if (Header.ShaderName.Len())
			{
				Header.ShaderName = Input.GenerateShaderName();
			}
				
			// Write out the header and shader source code.
			FMemoryWriter WriterAr(Output.ShaderCode.GetWriteAccess(), true);
			WriterAr << OfflineCompiledFlag;
			WriterAr << Header;
			WriterAr.Serialize((void*)SourceCodePtr, CodeSize);
				
			if (Name)
			{
				Output.ShaderCode.AddOptionalData('n', TCHAR_TO_UTF8(*Input.GenerateShaderName()));
			}
			if (Path)
			{
				Output.ShaderCode.AddOptionalData('p', Path);
			}
			if (Text)
			{
				Output.ShaderCode.AddOptionalData('c', Text);
			}
			if (Object && ObjectSize)
			{
				Output.ShaderCode.AddOptionalData('o', Object, ObjectSize);
			}
			if (Debug && DebugSize && UncSize && UncData)
			{
				Output.ShaderCode.AddOptionalData('z', Debug, DebugSize);
				Output.ShaderCode.AddOptionalData('u', UncData, UncSize);
			}
				
			Output.ParameterMap = TestOutput.ParameterMap;
			Output.Errors = TestOutput.Errors;
			Output.Target = TestOutput.Target;
			Output.NumInstructions = TestOutput.NumInstructions;
			Output.NumTextureSamplers = TestOutput.NumTextureSamplers;
			Output.bSucceeded = TestOutput.bSucceeded;
			Output.bFailedRemovingUnused = TestOutput.bFailedRemovingUnused;
			Output.bSupportsQueryingUsedAttributes = TestOutput.bSupportsQueryingUsedAttributes;
			Output.UsedAttributes = TestOutput.UsedAttributes;
		}
	}

	ShaderParameterParser.ValidateShaderParameterTypes(Input, Output);
}

bool StripShader_Metal(TArray<uint8>& Code, class FString const& DebugPath, bool const bNative)
{
	bool bSuccess = false;
	
	FShaderCodeReader ShaderCode(Code);
	FMemoryReader Ar(Code, true);
	Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());
	
	// was the shader already compiled offline?
	uint8 OfflineCompiledFlag;
	Ar << OfflineCompiledFlag;
	
	if(bNative && OfflineCompiledFlag == 1)
	{
		// get the header
		FMetalCodeHeader Header;
		Ar << Header;
		
		// Must be compiled for archiving or something is very wrong.
		if(bNative == false || Header.CompileFlags & (1 << CFLAG_Archive))
		{
			bSuccess = true;
			
			// remember where the header ended and code (precompiled or source) begins
			int32 CodeOffset = Ar.Tell();
			const uint8* SourceCodePtr = (uint8*)Code.GetData() + CodeOffset;
			
			// Copy the non-optional shader bytecode
			TArray<uint8> SourceCode;
			SourceCode.Append(SourceCodePtr, ShaderCode.GetActualShaderCodeSize() - CodeOffset);
			
			const ANSICHAR* ShaderSource = ShaderCode.FindOptionalData('c');
			const size_t ShaderSourceLength = ShaderSource ? FCStringAnsi::Strlen(ShaderSource) : 0;
			bool const bHasShaderSource = ShaderSourceLength > 0;
			
			const ANSICHAR* ShaderPath = ShaderCode.FindOptionalData('p');
			bool const bHasShaderPath = (ShaderPath && FCStringAnsi::Strlen(ShaderPath) > 0);
			
			if (bHasShaderSource && bHasShaderPath)
			{
				FString DebugFilePath = DebugPath / FString(ShaderPath);
				FString DebugFolderPath = FPaths::GetPath(DebugFilePath);
				if (IFileManager::Get().MakeDirectory(*DebugFolderPath, true))
				{
					FString TempPath = FPaths::CreateTempFilename(*DebugFolderPath, TEXT("MetalShaderFile-"), TEXT(".metal"));
					IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
					IFileHandle* FileHandle = PlatformFile.OpenWrite(*TempPath);
					if (FileHandle)
					{
						FileHandle->Write((const uint8 *)ShaderSource, ShaderSourceLength);
						delete FileHandle;

						IFileManager::Get().Move(*DebugFilePath, *TempPath, true, false, true, false);
						IFileManager::Get().Delete(*TempPath);
					}
					else
					{
						UE_LOG(LogShaders, Error, TEXT("Shader stripping failed: shader %s (Len: %0.8x, CRC: %0.8x) failed to create file %s!"), *Header.ShaderName, Header.SourceLen, Header.SourceCRC, *TempPath);
					}
				}
			}
			
			if (bNative)
			{
				int32 ObjectSize = 0;
				const uint8* ShaderObject = ShaderCode.FindOptionalDataAndSize('o', ObjectSize);
				
				// If ShaderObject and ObjectSize is zero then the code has already been stripped - source code should be the byte code
				if(ShaderObject && ObjectSize)
				{
					TArray<uint8> ObjectCodeArray;
					ObjectCodeArray.Append(ShaderObject, ObjectSize);
					SourceCode = ObjectCodeArray;
				}
			}
			
			// Strip any optional data
			if (bNative || ShaderCode.GetOptionalDataSize() > 0)
			{
				// Write out the header and compiled shader code
				FShaderCode NewCode;
				FMemoryWriter NewAr(NewCode.GetWriteAccess(), true);
				NewAr << OfflineCompiledFlag;
				NewAr << Header;
				
				// jam it into the output bytes
				NewAr.Serialize(SourceCode.GetData(), SourceCode.Num());
				
				Code = NewCode.GetReadAccess();
			}
		}
		else
		{
			UE_LOG(LogShaders, Error, TEXT("Shader stripping failed: shader %s (Len: %0.8x, CRC: %0.8x) was not compiled for archiving into a native library (Native: %s, Compile Flags: %0.8x)!"), *Header.ShaderName, Header.SourceLen, Header.SourceCRC, bNative ? TEXT("true") : TEXT("false"), (uint32)Header.CompileFlags);
		}
	}
	else
	{
		UE_LOG(LogShaders, Error, TEXT("Shader stripping failed: shader %s (Native: %s, Offline Compiled: %d) was not compiled to bytecode for native archiving!"), *DebugPath, bNative ? TEXT("true") : TEXT("false"), OfflineCompiledFlag);
	}
	
	return bSuccess;
}

EShaderPlatform MetalShaderFormatToLegacyShaderPlatform(FName ShaderFormat)
{
	static FName NAME_SF_METAL(TEXT("SF_METAL"));
	static FName NAME_SF_METAL_MRT(TEXT("SF_METAL_MRT"));
	static FName NAME_SF_METAL_TVOS(TEXT("SF_METAL_TVOS"));
	static FName NAME_SF_METAL_MRT_TVOS(TEXT("SF_METAL_MRT_TVOS"));
	static FName NAME_SF_METAL_SM5_NOTESS(TEXT("SF_METAL_SM5_NOTESS"));
	static FName NAME_SF_METAL_SM5(TEXT("SF_METAL_SM5"));
	static FName NAME_SF_METAL_MRT_MAC(TEXT("SF_METAL_MRT_MAC"));
	static FName NAME_SF_METAL_MACES3_1(TEXT("SF_METAL_MACES3_1"));
	
	if (ShaderFormat == NAME_SF_METAL)				return SP_METAL;
	if (ShaderFormat == NAME_SF_METAL_MRT)			return SP_METAL_MRT;
	if (ShaderFormat == NAME_SF_METAL_TVOS)			return SP_METAL_TVOS;
	if (ShaderFormat == NAME_SF_METAL_MRT_TVOS)		return SP_METAL_MRT_TVOS;
	if (ShaderFormat == NAME_SF_METAL_MRT_MAC)		return SP_METAL_MRT_MAC;
	if (ShaderFormat == NAME_SF_METAL_SM5)			return SP_METAL_SM5;
	if (ShaderFormat == NAME_SF_METAL_SM5_NOTESS)	return SP_METAL_SM5_NOTESS;
	if (ShaderFormat == NAME_SF_METAL_MACES3_1)		return SP_METAL_MACES3_1;
	
	return SP_NumPlatforms;
}

uint64 AppendShader_Metal(FName const& Format, FString const& WorkingDir, const FSHAHash& Hash, TArray<uint8>& InShaderCode)
{
	uint64 Id = 0;
	
	// Remote building needs to run through the check code for the Metal tools paths to be available for remotes (ensures this will work on incremental launches if there are no shaders to build)
	bool bRemoteBuildingConfigured = IsRemoteBuildingConfigured();
	
	EShaderPlatform Platform = MetalShaderFormatToLegacyShaderPlatform(Format);
	
	if (IsMetalCompilerAvailable(Platform))
	{
		// Parse the existing data and extract the source code. We have to recompile it
		FShaderCodeReader ShaderCode(InShaderCode);
		FMemoryReader Ar(InShaderCode, true);
		Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());
		
		// was the shader already compiled offline?
		uint8 OfflineCompiledFlag;
		Ar << OfflineCompiledFlag;
		if (OfflineCompiledFlag == 1)
		{
			// get the header
			FMetalCodeHeader Header;
			Ar << Header;
			
			// Must be compiled for archiving or something is very wrong.
			if(Header.CompileFlags & (1 << CFLAG_Archive))
			{
				// remember where the header ended and code (precompiled or source) begins
				int32 CodeOffset = Ar.Tell();
				const uint8* SourceCodePtr = (uint8*)InShaderCode.GetData() + CodeOffset;
				
				// Copy the non-optional shader bytecode
				int32 ObjectCodeDataSize = 0;
				uint8 const* Object = ShaderCode.FindOptionalDataAndSize('o', ObjectCodeDataSize);
				
				// 'o' segment missing this is a pre stripped shader
				if(!Object)
				{
					ObjectCodeDataSize = ShaderCode.GetActualShaderCodeSize() - CodeOffset;
					Object = SourceCodePtr;
				}
				
				TArrayView<const uint8> ObjectCodeArray(Object, ObjectCodeDataSize);
				
				// Object code segment
				FString ObjFilename = WorkingDir / FString::Printf(TEXT("Main_%0.8x_%0.8x.o"), Header.SourceLen, Header.SourceCRC);
				
				bool const bHasObjectData = (ObjectCodeDataSize > 0) || IFileManager::Get().FileExists(*ObjFilename);
				if (bHasObjectData)
				{
					// metal commandlines
					int32 ReturnCode = 0;
					FString Results;
					FString Errors;
					
					bool bHasObjectFile = IFileManager::Get().FileExists(*ObjFilename);
					if (ObjectCodeDataSize > 0)
					{
						// write out shader object code source (IR) for archiving to a single library file later
						if( FFileHelper::SaveArrayToFile(ObjectCodeArray, *ObjFilename) )
						{
							bHasObjectFile = true;
						}
					}
					
					if (bHasObjectFile)
					{
						Id = ((uint64)Header.SourceLen << 32) | Header.SourceCRC;
						
						// This is going to get serialised into the shader resource archive we don't anything but the header info now with the archive flag set
						Header.CompileFlags |= (1 << CFLAG_Archive);
						
						// Write out the header and compiled shader code
						FShaderCode NewCode;
						FMemoryWriter NewAr(NewCode.GetWriteAccess(), true);
						NewAr << OfflineCompiledFlag;
						NewAr << Header;
						
						InShaderCode = NewCode.GetReadAccess();
						
						UE_LOG(LogShaders, Verbose, TEXT("Archiving succeeded: shader %s (Len: %0.8x, CRC: %0.8x, SHA: %s)"), *Header.ShaderName, Header.SourceLen, Header.SourceCRC, *Hash.ToString());
					}
					else
					{
						UE_LOG(LogShaders, Error, TEXT("Archiving failed: failed to write temporary file %s for shader %s (Len: %0.8x, CRC: %0.8x, SHA: %s)"), *ObjFilename, *Header.ShaderName, Header.SourceLen, Header.SourceCRC, *Hash.ToString());
					}
				}
				else
				{
					UE_LOG(LogShaders, Error, TEXT("Archiving failed: shader %s (Len: %0.8x, CRC: %0.8x, SHA: %s) has no object data"), *Header.ShaderName, Header.SourceLen, Header.SourceCRC, *Hash.ToString());
				}
			}
			else
			{
				UE_LOG(LogShaders, Error, TEXT("Archiving failed: shader %s (Len: %0.8x, CRC: %0.8x, SHA: %s) was not compiled for archiving (Compile Flags: %0.8x)!"), *Header.ShaderName, Header.SourceLen, Header.SourceCRC, *Hash.ToString(), (uint32)Header.CompileFlags);
			}
		}
		else
		{
			UE_LOG(LogShaders, Error, TEXT("Archiving failed: shader SHA: %s was not compiled to bytecode (%d)!"), *Hash.ToString(), OfflineCompiledFlag);
		}
	}
	else
	{
		UE_LOG(LogShaders, Error, TEXT("Archiving failed: no Xcode install on the local machine or a remote Mac."));
	}
	return Id;
}

bool FinalizeLibrary_Metal(FName const& Format, FString const& WorkingDir, FString const& LibraryPath, TSet<uint64> const& Shaders, class FString const& DebugOutputDir)
{
	bool bOK = false;
	
	// Check remote building before the Metal tools paths to ensure configured
	bool bRemoteBuildingConfigured = IsRemoteBuildingConfigured();

	EShaderPlatform Platform = MetalShaderFormatToLegacyShaderPlatform(Format);
	if (IsMetalCompilerAvailable(Platform))
	{
		int32 ReturnCode = 0;
		FString Results;
		FString Errors;
		
		FString ArchivePath = FPaths::CreateTempFilename(*WorkingDir, TEXT("MetalArchive"), TEXT("")) + TEXT(".metalar");
		
		IFileManager::Get().Delete(*ArchivePath);
		IFileManager::Get().Delete(*LibraryPath);
	
		// Check and init remote handling
		const bool bBuildingRemotely = (!PLATFORM_MAC || UNIXLIKE_TO_MAC_REMOTE_BUILDING) && bRemoteBuildingConfigured;
		FString RemoteDestination = TEXT("/tmp");
		if(bBuildingRemotely)
		{
			RemoteDestination = MakeRemoteTempFolder(TEXT("/tmp"));
			ArchivePath = LocalPathToRemote(ArchivePath, RemoteDestination);
		}
		
		bool bArchiveFileValid = false;
		
		// Archive build phase - like unix ar, build metal archive from all the object files
		{
			// Metal commandlines
			UE_LOG(LogShaders, Display, TEXT("Archiving %d shaders for shader platform: %s"), Shaders.Num(), *Format.GetPlainNameString());
			if(bRemoteBuildingConfigured)
			{
				UE_LOG(LogShaders, Display, TEXT("Attempting to Archive using remote at '%s@%s' with ssh identity '%s'"), *GRemoteBuildServerUser, *GRemoteBuildServerHost, *GRemoteBuildServerSSHKey);
			}
			
			int32 Index = 0;
			FString Params = FString::Printf(TEXT("q \"%s\""), *ArchivePath);
			
			const uint32 ArgCommandMax = GetMaxArgLength();
			const uint32 ArchiveOperationCommandLength = bBuildingRemotely ? GSSHPath.Len() + GetMetalToolsPath(Platform).Len() : GetMetalToolsPath(Platform).Len();

			for (auto Shader : Shaders)
			{
				uint32 Len = (Shader >> 32);
				uint32 CRC = (Shader & 0xffffffff);
				
				// Build source file name path
				UE_LOG(LogShaders, Verbose, TEXT("[%d/%d] %s Main_%0.8x_%0.8x.o"), ++Index, Shaders.Num(), *Format.GetPlainNameString(), Len, CRC);
				FString SourceFileNameParam = FString::Printf(TEXT("\"%s/Main_%0.8x_%0.8x.o\""), *FPaths::ConvertRelativePathToFull(WorkingDir), Len, CRC);
				
				// Remote builds copy file and swizzle Source File Name param
				if(bBuildingRemotely)
				{
					FString DestinationFileNameParam = FString::Printf(TEXT("%s/Main_%0.8x_%0.8x.o"), *RemoteDestination, Len, CRC);
					if(!CopyLocalFileToRemote(SourceFileNameParam, DestinationFileNameParam))
					{
						UE_LOG(LogShaders, Error, TEXT("Archiving failed: Copy object file to remote failed for file:%s"), *SourceFileNameParam);
						Params.Empty();
						break;
					}
					SourceFileNameParam = FString::Printf(TEXT("\"%s\""), *DestinationFileNameParam);		// Wrap each param in it's own string
				}
				
				// Have we gone past sensible argument length - incremently archive
				if (Params.Len() + SourceFileNameParam.Len() + ArchiveOperationCommandLength + 3 >= (ArgCommandMax / 2))
				{
					ExecXcodeCommand(Platform, TEXT("metal-ar"), *Params, &ReturnCode, &Results, &Errors);
					bArchiveFileValid = RemoteFileExists(*ArchivePath);
					
					if (ReturnCode != 0 || !bArchiveFileValid)
					{
						UE_LOG(LogShaders, Error, TEXT("Archiving failed: metal-ar failed with code %d: %s"), ReturnCode, *Errors);
						Params.Empty();
						break;
					}
					
					// Reset params
					Params = FString::Printf(TEXT("q \"%s\""), *ArchivePath);
				}
				
				// Safe to add this file
				Params += TEXT(" ");
				Params += SourceFileNameParam;
			}
		
			// Any left over files - incremently archive again
			if (!Params.IsEmpty())
			{
				ExecXcodeCommand(Platform, TEXT("metal-ar"), *Params, &ReturnCode, &Results, &Errors);
                bArchiveFileValid = RemoteFileExists(*ArchivePath);
				
				if (ReturnCode != 0 || !bArchiveFileValid)
				{
					UE_LOG(LogShaders, Error, TEXT("Archiving failed: metal-ar failed with code %d: %s"), ReturnCode, *Errors);
				}
			}
			
			// If remote, leave the archive file where it is - we don't actually need it locally
		}
		
		// Lib build phase, metalar to metallib 
		{
			// handle compile error
			if (ReturnCode == 0 && bArchiveFileValid)
			{
				UE_LOG(LogShaders, Display, TEXT("Post-processing archive for shader platform: %s"), *Format.GetPlainNameString());
								
				FString RemoteLibPath = LocalPathToRemote(LibraryPath, RemoteDestination);
				FString OriginalRemoteLibPath = RemoteLibPath;
				FString Params;

				if (RemoteFileExists(RemoteLibPath))
				{
					UE_LOG(LogShaders, Warning, TEXT("Archiving warning: target metallib already exists and will be overwritten: %s"), *RemoteLibPath);
				}
				if (RemoveRemoteFile(RemoteLibPath) != 0)
				{
					UE_LOG(LogShaders, Warning, TEXT("Archiving warning: target metallib already exists and count not be overwritten: %s"), *RemoteLibPath);

					// Output to a unique file
					FGuid Guid = FGuid::NewGuid();
					RemoteLibPath = OriginalRemoteLibPath + FString::Printf(TEXT(".%x%x%x%x"), Guid.A, Guid.B, Guid.C, Guid.D);
				}
			
				Params = FString::Printf(TEXT("-o \"%s\" \"%s\""), *RemoteLibPath, *ArchivePath);
				ReturnCode = 0;
				Results = TEXT("");
				Errors = TEXT("");
				
				ExecXcodeCommand(Platform, TEXT("metallib"), *Params, &ReturnCode, &Results, &Errors);
	
				// handle compile error
				if (ReturnCode == 0)
				{
					// There is problem going to location with spaces using remote copy (at least on Mac no combination of \ and/or "" works) - work around this issue @todo investigate this further
					FString FileName = FPaths::GetCleanFilename(LibraryPath);
                    FString LocalCopyLocation = FPaths::Combine(*FPaths::ConvertRelativePathToFull(WorkingDir), FileName);
						
                    if(bBuildingRemotely && CopyRemoteFileToLocal(RemoteLibPath, LocalCopyLocation))
                    {
                        IFileManager::Get().Move(*LibraryPath, *LocalCopyLocation);
                    }
					else if (!bBuildingRemotely && RemoteLibPath != LibraryPath)
					{
						IFileManager::Get().Move(*RemoteLibPath, *LibraryPath);
					}

					bOK = (IFileManager::Get().FileSize(*LibraryPath) > 0);

					if (!bOK)
					{
						UE_LOG(LogShaders, Error, TEXT("Archiving failed: failed to copy to local destination: %s"), *LibraryPath);
					}
				}
				else
				{
					UE_LOG(LogShaders, Error, TEXT("Archiving failed: metallib failed with code %d: %s"), ReturnCode, *Errors);
				}
			}
			else
			{
				UE_LOG(LogShaders, Error, TEXT("Archiving failed: no valid input for metallib."));
			}
		}
	}
	else
	{
		UE_LOG(LogShaders, Error, TEXT("Archiving failed: no Xcode install."));
	}
	
	return bOK;
}
