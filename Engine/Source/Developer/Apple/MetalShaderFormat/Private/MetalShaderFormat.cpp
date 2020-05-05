// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalShaderFormat.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCore.h"
#include "ShaderCodeArchive.h"
#include "hlslcc.h"
#include "MetalShaderResources.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/Archive.h"
#include "Misc/ConfigCacheIni.h"
#include "MetalBackend.h"
#include "Misc/FileHelper.h"
#include "FileUtilities/ZipArchiveWriter.h"

#define WRITE_METAL_SHADER_SOURCE_ARCHIVE 0

extern uint16 GetXcodeVersion(uint64& BuildVersion);
extern bool StripShader_Metal(TArray<uint8>& Code, class FString const& DebugPath, bool const bNative);
extern uint64 AppendShader_Metal(class FName const& Format, class FString const& ArchivePath, const FSHAHash& Hash, TArray<uint8>& Code);
extern bool FinalizeLibrary_Metal(class FName const& Format, class FString const& ArchivePath, class FString const& LibraryPath, TSet<uint64> const& Shaders, class FString const& DebugOutputDir);

static FName NAME_SF_METAL(TEXT("SF_METAL"));
static FName NAME_SF_METAL_MRT(TEXT("SF_METAL_MRT"));
static FName NAME_SF_METAL_TVOS(TEXT("SF_METAL_TVOS"));
static FName NAME_SF_METAL_MRT_TVOS(TEXT("SF_METAL_MRT_TVOS"));
static FName NAME_SF_METAL_SM5_NOTESS(TEXT("SF_METAL_SM5_NOTESS"));
static FName NAME_SF_METAL_SM5(TEXT("SF_METAL_SM5"));
static FName NAME_SF_METAL_MACES3_1(TEXT("SF_METAL_MACES3_1"));
static FName NAME_SF_METAL_MRT_MAC(TEXT("SF_METAL_MRT_MAC"));
static FString METAL_LIB_EXTENSION(TEXT(".metallib"));
static FString METAL_MAP_EXTENSION(TEXT(".metalmap"));

class FMetalShaderFormat : public IShaderFormat
{
public:
	enum
	{
		HEADER_VERSION = 69,
	};
	
	struct FVersion
	{
		uint16 XcodeVersion;
		uint16 HLSLCCMinor		: 8;
		uint16 Format			: 8;
	};
	
	virtual uint32 GetVersion(FName Format) const override final
	{
		return GetMetalFormatVersion(Format);
	}
	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override final
	{
		OutFormats.Add(NAME_SF_METAL);
		OutFormats.Add(NAME_SF_METAL_MRT);
		OutFormats.Add(NAME_SF_METAL_TVOS);
		OutFormats.Add(NAME_SF_METAL_MRT_TVOS);
		OutFormats.Add(NAME_SF_METAL_SM5_NOTESS);
		OutFormats.Add(NAME_SF_METAL_SM5);
		OutFormats.Add(NAME_SF_METAL_MACES3_1);
		OutFormats.Add(NAME_SF_METAL_MRT_MAC);
	}
	virtual void CompileShader(FName Format, const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output,const FString& WorkingDirectory) const override final
	{
		check(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT || Format == NAME_SF_METAL_TVOS || Format == NAME_SF_METAL_MRT_TVOS || Format == NAME_SF_METAL_SM5_NOTESS || Format == NAME_SF_METAL_SM5 || Format == NAME_SF_METAL_MACES3_1 || Format == NAME_SF_METAL_MRT_MAC);
		CompileShader_Metal(Input, Output, WorkingDirectory);
	}
	virtual bool CanStripShaderCode(bool const bNativeFormat) const override final
	{
		return CanCompileBinaryShaders() && bNativeFormat;
	}
	virtual bool StripShaderCode( TArray<uint8>& Code, FString const& DebugOutputDir, bool const bNative ) const override final
	{
		return StripShader_Metal(Code, DebugOutputDir, bNative);
    }
	virtual bool SupportsShaderArchives() const override 
	{ 
		return CanCompileBinaryShaders();
	}
    virtual bool CreateShaderArchive(FString const& LibraryName,
		FName Format,
		const FString& WorkingDirectory,
		const FString& OutputDir,
		const FString& DebugOutputDir,
		const FSerializedShaderArchive& InSerializedShaders,
		const TArray<TArray<uint8>>& ShaderCode,
		TArray<FString>* OutputFiles) const override final
    {
		const int32 NumShadersPerLibrary = 10000;
		check(LibraryName.Len() > 0);
		check(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT || Format == NAME_SF_METAL_TVOS || Format == NAME_SF_METAL_MRT_TVOS || Format == NAME_SF_METAL_SM5_NOTESS || Format == NAME_SF_METAL_SM5 || Format == NAME_SF_METAL_MACES3_1 || Format == NAME_SF_METAL_MRT_MAC);

		const FString ArchivePath = (WorkingDirectory / Format.GetPlainNameString());
		IFileManager::Get().DeleteDirectory(*ArchivePath, false, true);
		IFileManager::Get().MakeDirectory(*ArchivePath);

		FSerializedShaderArchive SerializedShaders(InSerializedShaders);
		check(SerializedShaders.GetNumShaders() == ShaderCode.Num());

		TArray<uint8> StrippedShaderCode;
		TArray<uint8> TempShaderCode;

		TArray<TSet<uint64>> SubLibraries;

		for (int32 ShaderIndex = 0; ShaderIndex < SerializedShaders.GetNumShaders(); ++ShaderIndex)
		{
			SerializedShaders.DecompressShader(ShaderIndex, ShaderCode, TempShaderCode);
			StripShader_Metal(TempShaderCode, DebugOutputDir, true);

			uint64 ShaderId = AppendShader_Metal(Format, ArchivePath, SerializedShaders.ShaderHashes[ShaderIndex], TempShaderCode);
			uint32 LibraryIndex = ShaderIndex / NumShadersPerLibrary;

			if (ShaderId)
			{
				if (SubLibraries.Num() <= (int32)LibraryIndex)
				{
					SubLibraries.Add(TSet<uint64>());
				}
				SubLibraries[LibraryIndex].Add(ShaderId);
			}

			FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
			ShaderEntry.Size = TempShaderCode.Num();
			ShaderEntry.UncompressedSize = TempShaderCode.Num();

			StrippedShaderCode.Append(TempShaderCode);
		}

		SerializedShaders.Finalize();

		bool bOK = false;
		FString LibraryPlatformName = FString::Printf(TEXT("%s_%s"), *LibraryName, *Format.GetPlainNameString());
		volatile int32 CompiledLibraries = 0;
		TArray<FGraphEventRef> Tasks;

		for (uint32 Index = 0; Index < (uint32)SubLibraries.Num(); Index++)
		{
			TSet<uint64>& PartialShaders = SubLibraries[Index];

			FString LibraryPath = (OutputDir / LibraryPlatformName) + FString::Printf(TEXT(".%d"), Index) + METAL_LIB_EXTENSION;
			if (OutputFiles)
			{
				OutputFiles->Add(LibraryPath);
			}

			// Enqueue the library compilation as a task so we can go wide
			FGraphEventRef CompletionFence = FFunctionGraphTask::CreateAndDispatchWhenReady([Format, ArchivePath, LibraryPath, PartialShaders, DebugOutputDir, &CompiledLibraries]()
			{
				if (FinalizeLibrary_Metal(Format, ArchivePath, LibraryPath, PartialShaders, DebugOutputDir))
				{
					FPlatformAtomics::InterlockedIncrement(&CompiledLibraries);
				}
			}, TStatId(), NULL, ENamedThreads::AnyThread);

			Tasks.Add(CompletionFence);
		}

#if WITH_ENGINE
		FGraphEventRef DebugDataCompletionFence = FFunctionGraphTask::CreateAndDispatchWhenReady([Format, OutputDir, LibraryPlatformName, DebugOutputDir]()
		{
			//TODO add a check in here - this will only work if we have shader archiving with debug info set.

			//We want to archive all the metal shader source files so that they can be unarchived into a debug location
			//This allows the debugging of optimised metal shaders within the xcode tool set
			//Currently using the 'tar' system tool to create a compressed tape archive

			//Place the archive in the same position as the .metallib file
			FString CompressedDir = (OutputDir / TEXT("../MetaData/ShaderDebug/"));
			IFileManager::Get().MakeDirectory(*CompressedDir, true);

			FString CompressedPath = (CompressedDir / LibraryPlatformName) + TEXT(".zip");

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			IFileHandle* ZipFile = PlatformFile.OpenWrite(*CompressedPath);
			if (ZipFile)
			{
				FZipArchiveWriter* ZipWriter = new FZipArchiveWriter(ZipFile);

				//Find the metal source files
				TArray<FString> FilesToArchive;
				IFileManager::Get().FindFilesRecursive(FilesToArchive, *DebugOutputDir, TEXT("*.metal"), true, false, false);

				//Write the local file names into the target file
				const FString DebugDir = DebugOutputDir / *Format.GetPlainNameString();

				for (FString FileName : FilesToArchive)
				{
					TArray<uint8> FileData;
					FFileHelper::LoadFileToArray(FileData, *FileName);
					FPaths::MakePathRelativeTo(FileName, *DebugDir);

					ZipWriter->AddFile(FileName, FileData, FDateTime::Now());
				}

				delete ZipWriter;
				ZipWriter = nullptr;
			}
			else
			{
				UE_LOG(LogShaders, Error, TEXT("Failed to create Metal debug .zip output file \"%s\". Debug .zip export will be disabled."), *CompressedPath);
			}
		}, TStatId(), NULL, ENamedThreads::AnyThread);
		Tasks.Add(DebugDataCompletionFence);
#endif // WITH_ENGINE

		// Wait for tasks
		for (auto& Task : Tasks)
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		}

		if (CompiledLibraries == SubLibraries.Num())
		{
			FString BinaryShaderFile = (OutputDir / LibraryPlatformName) + METAL_MAP_EXTENSION;
			FArchive* BinaryShaderAr = IFileManager::Get().CreateFileWriter(*BinaryShaderFile);
			if (BinaryShaderAr != NULL)
			{
				FMetalShaderLibraryHeader Header;
				Header.Format = Format.GetPlainNameString();
				Header.NumLibraries = SubLibraries.Num();
				Header.NumShadersPerLibrary = NumShadersPerLibrary;

				*BinaryShaderAr << Header;
				*BinaryShaderAr << SerializedShaders;
				*BinaryShaderAr << StrippedShaderCode;

				BinaryShaderAr->Flush();
				delete BinaryShaderAr;

				if (OutputFiles)
				{
					OutputFiles->Add(BinaryShaderFile);
				}

				bOK = true;
			}
		}

		return bOK;

		//Map.Format = Format.GetPlainNameString();
    }

	virtual bool CanCompileBinaryShaders() const override final
	{
#if PLATFORM_MAC
		return FPlatformMisc::IsSupportedXcodeVersionInstalled();
#else
		return IsRemoteBuildingConfigured();
#endif
	}
	virtual const TCHAR* GetPlatformIncludeDirectory() const
	{
		return TEXT("Metal");
	}
};

uint32 GetMetalFormatVersion(FName Format)
{
	static_assert(sizeof(FMetalShaderFormat::FVersion) == sizeof(uint32), "Out of bits!");
	union
	{
		FMetalShaderFormat::FVersion Version;
		uint32 Raw;
	} Version;
	
	// Include the Xcode version when the .ini settings instruct us to do so.
	uint16 AppVersion = 0;
	bool bAddXcodeVersionInShaderVersion = false;
	if(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT || Format == NAME_SF_METAL_TVOS || Format == NAME_SF_METAL_MRT_TVOS)
	{
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("XcodeVersionInShaderVersion"), bAddXcodeVersionInShaderVersion, GEngineIni);
	}
	else
	{
		GConfig->GetBool(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("XcodeVersionInShaderVersion"), bAddXcodeVersionInShaderVersion, GEngineIni);
	}

	// We want to include the Xcode App and build version to avoid
	// weird mismatches where some shaders are built with one version
	// of the metal frontend and others with a different version.
	uint64 BuildVersion = 0;
	
	// GetXcodeVersion returns:
	// Major  << 8 | Minor << 4 | Patch
	AppVersion = GetXcodeVersion(BuildVersion);
	
	if (!FApp::IsEngineInstalled() && bAddXcodeVersionInShaderVersion)
	{
		// For local development we'll mix in the xcode version
		// and build version.
		AppVersion ^= (BuildVersion & 0xff);
		AppVersion ^= ((BuildVersion >> 16) & 0xff);
		AppVersion ^= ((BuildVersion >> 32) & 0xff);
		AppVersion ^= ((BuildVersion >> 48) & 0xff);
	}
	else
	{
		// In the other case (ie, shipping editor binary distributions)
		// We will only mix in the Major version of Xcode used to create
		// the shader binaries.
		AppVersion = (AppVersion >> 8) & 0xff;
	}

	Version.Version.XcodeVersion = AppVersion;
	Version.Version.Format = FMetalShaderFormat::HEADER_VERSION;
	Version.Version.HLSLCCMinor = HLSLCC_VersionMinor;
	
	// Check that we didn't overwrite any bits
	check(Version.Version.XcodeVersion == AppVersion);
	check(Version.Version.Format == FMetalShaderFormat::HEADER_VERSION);
	check(Version.Version.HLSLCCMinor == HLSLCC_VersionMinor);
	
	return Version.Raw;
}

/**
 * Module for OpenGL shaders
 */

static IShaderFormat* Singleton = NULL;

class FMetalShaderFormatModule : public IShaderFormatModule
{
public:
	virtual ~FMetalShaderFormatModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	virtual IShaderFormat* GetShaderFormat()
	{
		if (!Singleton)
		{
			Singleton = new FMetalShaderFormat();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE( FMetalShaderFormatModule, MetalShaderFormat);
