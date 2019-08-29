// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MetalShaderFormat.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCore.h"
#include "Interfaces/IShaderFormatArchive.h"
#include "hlslcc.h"
#include "MetalShaderResources.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"
#include "Misc/ConfigCacheIni.h"
#include "MetalBackend.h"

extern uint16 GetXcodeVersion(uint64& BuildVersion);
extern bool StripShader_Metal(TArray<uint8>& Code, class FString const& DebugPath, bool const bNative);
extern uint64 AppendShader_Metal(class FName const& Format, class FString const& ArchivePath, const FSHAHash& Hash, TArray<uint8>& Code);
extern bool FinalizeLibrary_Metal(class FName const& Format, class FString const& ArchivePath, class FString const& LibraryPath, TSet<uint64> const& Shaders, class FString const& DebugOutputDir);

static FName NAME_SF_METAL(TEXT("SF_METAL"));
static FName NAME_SF_METAL_MRT(TEXT("SF_METAL_MRT"));
static FName NAME_SF_METAL_SM5_NOTESS(TEXT("SF_METAL_SM5_NOTESS"));
static FName NAME_SF_METAL_SM5(TEXT("SF_METAL_SM5"));
static FName NAME_SF_METAL_MACES3_1(TEXT("SF_METAL_MACES3_1"));
static FName NAME_SF_METAL_MACES2(TEXT("SF_METAL_MACES2"));
static FName NAME_SF_METAL_MRT_MAC(TEXT("SF_METAL_MRT_MAC"));
static FString METAL_LIB_EXTENSION(TEXT(".metallib"));
static FString METAL_MAP_EXTENSION(TEXT(".metalmap"));

class FMetalShaderFormatArchive : public IShaderFormatArchive
{
public:
	FMetalShaderFormatArchive(FString const& InLibraryName, FName InFormat, FString const& WorkingDirectory)
	: LibraryName(InLibraryName)
	, Format(InFormat)
	, WorkingDir(WorkingDirectory)
	{
		check(LibraryName.Len() > 0);
		check(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT || Format == NAME_SF_METAL_SM5_NOTESS || Format == NAME_SF_METAL_SM5 || Format == NAME_SF_METAL_MACES3_1 || Format == NAME_SF_METAL_MACES2 || Format == NAME_SF_METAL_MRT_MAC);
		ArchivePath = (WorkingDir / Format.GetPlainNameString());
		IFileManager::Get().DeleteDirectory(*ArchivePath, false, true);
		IFileManager::Get().MakeDirectory(*ArchivePath);
		Map.Format = Format.GetPlainNameString();
	}
	
	virtual FName GetFormat( void ) const
	{
		return Format;
	}
	
	virtual bool AddShader( uint8 Frequency, const FSHAHash& Hash, TArray<uint8>& Code )
	{
		uint64 ShaderId = AppendShader_Metal(Format, ArchivePath, Hash, Code);
		if (ShaderId)
		{
			//add Id to our list of shaders processed successfully
			Shaders.Add(ShaderId);
			
			//Note code copy in the map is uncompressed
			Map.HashMap.Add(Hash, TPairInitializer<uint8, TArray<uint8>>(Frequency, Code));
		}
		return (ShaderId > 0);
	}
	
	virtual bool Finalize( FString OutputDir, FString DebugOutputDir, TArray<FString>* OutputFiles )
	{
		bool bOK = false;
		FString LibraryPlatformName = FString::Printf(TEXT("%s_%s"), *LibraryName, *Format.GetPlainNameString());
		FString LibraryPath = (OutputDir / LibraryPlatformName) + METAL_LIB_EXTENSION;
		
		if (FinalizeLibrary_Metal(Format, ArchivePath, LibraryPath, Shaders, DebugOutputDir))
		{
			FString BinaryShaderFile = (OutputDir / LibraryPlatformName) + METAL_MAP_EXTENSION;
			FArchive* BinaryShaderAr = IFileManager::Get().CreateFileWriter(*BinaryShaderFile);
			if( BinaryShaderAr != NULL )
			{
				*BinaryShaderAr << Map;
				BinaryShaderAr->Flush();
				delete BinaryShaderAr;
				
				if (OutputFiles)
				{
					OutputFiles->Add(LibraryPath);
					OutputFiles->Add(BinaryShaderFile);
				}
				
				bOK = true;
			}
		}

		return bOK;
	}
	
public:
	virtual ~FMetalShaderFormatArchive() { }
	
private:
	FString LibraryName;
	FName Format;
	FString WorkingDir;
	FString ArchivePath;
	TSet<uint64> Shaders;
	TSet<FString> SourceFiles;
	FMetalShaderMap Map;
};

class FMetalShaderFormat : public IShaderFormat
{
public:
	enum
	{
		HEADER_VERSION = 57,
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
		OutFormats.Add(NAME_SF_METAL_SM5_NOTESS);
		OutFormats.Add(NAME_SF_METAL_SM5);
		OutFormats.Add(NAME_SF_METAL_MACES3_1);
		OutFormats.Add(NAME_SF_METAL_MACES2);
		OutFormats.Add(NAME_SF_METAL_MRT_MAC);
	}
	virtual void CompileShader(FName Format, const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output,const FString& WorkingDirectory) const override final
	{
		check(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT || Format == NAME_SF_METAL_SM5_NOTESS || Format == NAME_SF_METAL_SM5 || Format == NAME_SF_METAL_MACES3_1 || Format == NAME_SF_METAL_MACES2 || Format == NAME_SF_METAL_MRT_MAC);
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
    virtual class IShaderFormatArchive* CreateShaderArchive( FString const& LibraryName, FName Format, const FString& WorkingDirectory ) const override final
    {
		return new FMetalShaderFormatArchive(LibraryName, Format, WorkingDirectory);
    }
	virtual bool CanCompileBinaryShaders() const override final
	{
#if PLATFORM_MAC
		return FPlatformMisc::IsSupportedXcodeVersionInstalled();
#else
		return IsRemoteBuildingConfigured();
#endif
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
	if(Format == NAME_SF_METAL || Format == NAME_SF_METAL_MRT)
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
