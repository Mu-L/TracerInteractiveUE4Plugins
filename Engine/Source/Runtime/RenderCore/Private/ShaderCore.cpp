// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCore.h: Shader core module implementation.
=============================================================================*/

#include "ShaderCore.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Stats/StatsMisc.h"
#include "Misc/ScopedSlowTask.h"
#include "Shader.h"
#include "VertexFactory.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "RHIShaderFormatDefinitions.inl"
#if WITH_EDITOR
#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/ConfigCacheIni.h"
#endif

static TAutoConsoleVariable<int32> CVarShaderDevelopmentMode(
	TEXT("r.ShaderDevelopmentMode"),
	0,
	TEXT("0: Default, 1: Enable various shader development utilities, such as the ability to retry on failed shader compile, and extra logging as shaders are compiled."),
	ECVF_Default);

void UpdateShaderDevelopmentMode()
{
	// Keep LogShaders verbosity in sync with r.ShaderDevelopmentMode
	// r.ShaderDevelopmentMode==1 results in all LogShaders log messages being displayed
	bool bLogShadersUnsuppressed = UE_LOG_ACTIVE(LogShaders, Log);
	bool bDesiredLogShadersUnsuppressed = CVarShaderDevelopmentMode.GetValueOnGameThread() == 1;

	if (bLogShadersUnsuppressed != bDesiredLogShadersUnsuppressed)
	{
		if (bDesiredLogShadersUnsuppressed)
		{
			UE_SET_LOG_VERBOSITY(LogShaders, Log);
		}
		else
		{
			UE_SET_LOG_VERBOSITY(LogShaders, Error);
		}
	}
}

//
// Shader stats
//

DEFINE_STAT(STAT_ShaderCompiling_NiagaraShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumTotalNiagaraShaders);

DEFINE_STAT(STAT_ShaderCompiling_OpenColorIOShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumTotalOpenColorIOShaders);

DEFINE_STAT(STAT_ShaderCompiling_MaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_GlobalShaders);
DEFINE_STAT(STAT_ShaderCompiling_RHI);
DEFINE_STAT(STAT_ShaderCompiling_HashingShaderFiles);
DEFINE_STAT(STAT_ShaderCompiling_LoadingShaderFiles);
DEFINE_STAT(STAT_ShaderCompiling_HLSLTranslation);
DEFINE_STAT(STAT_ShaderCompiling_DDCLoading);
DEFINE_STAT(STAT_ShaderCompiling_MaterialLoading);
DEFINE_STAT(STAT_ShaderCompiling_MaterialCompiling);

DEFINE_STAT(STAT_ShaderCompiling_NumTotalMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumSpecialMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumParticleMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumSkinnedMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumLitMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumUnlitMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumTransparentMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumOpaqueMaterialShaders);
DEFINE_STAT(STAT_ShaderCompiling_NumMaskedMaterialShaders);


DEFINE_STAT(STAT_Shaders_NumShadersLoaded);
DEFINE_STAT(STAT_Shaders_NumShaderResourcesLoaded);
DEFINE_STAT(STAT_Shaders_NumShaderMaps);
DEFINE_STAT(STAT_Shaders_RTShaderLoadTime);
DEFINE_STAT(STAT_Shaders_NumShadersUsedForRendering);
DEFINE_STAT(STAT_Shaders_TotalRTShaderInitForRenderingTime);
DEFINE_STAT(STAT_Shaders_FrameRTShaderInitForRenderingTime);
DEFINE_STAT(STAT_Shaders_ShaderMemory);
DEFINE_STAT(STAT_Shaders_ShaderResourceMemory);
DEFINE_STAT(STAT_Shaders_ShaderMapMemory);

/** Protects GShaderFileCache from simultaneous access by multiple threads. */
FCriticalSection FileCacheCriticalSection;

/** The shader file cache, used to minimize shader file reads */
TMap<FString, FString> GShaderFileCache;

class FShaderHashCache
{
public:
	FShaderHashCache()
		: bInitialized(false)
	{
	}

	void Initialize()
	{
		const FString EmptyDirectory("");
		for (auto& Platform : Platforms)
		{
			Platform.IncludeDirectory = EmptyDirectory;
			Platform.ShaderHashCache.Reset();
		}


		TArray<FName> Modules;
		FModuleManager::Get().FindModules(SHADERFORMAT_MODULE_WILDCARD, Modules);

		if (!Modules.Num())
		{
			UE_LOG(LogShaders, Error, TEXT("No target shader formats found!"));
		}

		TArray<FName> SupportedFormats;

		for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
		{
			IShaderFormat* ShaderFormat = FModuleManager::LoadModuleChecked<IShaderFormatModule>(Modules[ModuleIndex]).GetShaderFormat();
			if (ShaderFormat)
			{
				FString IncludeDirectory = ShaderFormat->GetPlatformIncludeDirectory();
				if (!IncludeDirectory.IsEmpty())
				{
					IncludeDirectory = "/" + IncludeDirectory + "/";
				}

				SupportedFormats.Reset();
				ShaderFormat->GetSupportedFormats(SupportedFormats);

				for (int32 FormatIndex = 0; FormatIndex < SupportedFormats.Num(); ++FormatIndex)
				{
					const EShaderPlatform ShaderPlatform = ShaderFormatNameToShaderPlatform(SupportedFormats[FormatIndex]);
					if (ShaderPlatform != SP_NumPlatforms)
					{
						Platforms[ShaderPlatform].IncludeDirectory = IncludeDirectory;
					}
				}
			}
		}

		bInitialized = true;
	}

	FSHAHash* FindHash(EShaderPlatform ShaderPlatform, const FString& VirtualFilePath)
	{
		check(ShaderPlatform < ARRAY_COUNT(Platforms));
		checkf(bInitialized, TEXT("GShaderHashCache::Initialize needs to be called before GShaderHashCache::FindHash."));

		return Platforms[ShaderPlatform].ShaderHashCache.Find(VirtualFilePath);
	}

	FSHAHash& AddHash(EShaderPlatform ShaderPlatform, const FString& VirtualFilePath)
	{
		check(ShaderPlatform < ARRAY_COUNT(Platforms));
		checkf(bInitialized, TEXT("GShaderHashCache::Initialize needs to be called before GShaderHashCache::AddHash."));

		return Platforms[ShaderPlatform].ShaderHashCache.Add(VirtualFilePath, FSHAHash());
	}

	bool ShouldIgnoreInclude(const FString& VirtualFilePath, EShaderPlatform ShaderPlatform)
	{
		// Ignore only platform specific files, which won't be used by the target platform.
		if (VirtualFilePath.StartsWith(TEXT("/Engine/Private/Platform/"))
			|| VirtualFilePath.StartsWith(TEXT("/Engine/Public/Platform/")))
		{
			const FString& PlatformIncludeDirectory = GetPlatformIncludeDirectory(ShaderPlatform);

			if (PlatformIncludeDirectory.IsEmpty() || !VirtualFilePath.Contains(PlatformIncludeDirectory))
			{
				return true;
			}
		}

		return false;
	}

	void Empty()
	{
		for (auto& Platform : Platforms)
		{
			Platform.ShaderHashCache.Reset();
		}
	}


private:

	struct FPlatform
	{
		/** Folder with platform specific shader files. */
		FString IncludeDirectory;

		/** The shader file hash cache, used to minimize loading and hashing shader files; it includes also hashes for multiple filenames
		by making the key the concatenated list of filenames.
		*/
		TMap<FString, FSHAHash> ShaderHashCache;
	};

	FPlatform Platforms[EShaderPlatform::SP_NumPlatforms];
	bool bInitialized;


	const FString& GetPlatformIncludeDirectory(EShaderPlatform ShaderPlatform)
	{
		check(ShaderPlatform < EShaderPlatform::SP_NumPlatforms);
		checkf(bInitialized, TEXT("GShaderHashCache::Initialize needs to be called before GShaderHashCache::GetPlatformIncludeDirectory."));

		return Platforms[ShaderPlatform].IncludeDirectory;
	}
};

FShaderHashCache GShaderHashCache;

/** Global map of virtual file path to physical file paths */
static TMap<FString, FString> GShaderSourceDirectoryMappings;

static TAutoConsoleVariable<int32> CVarForceDebugViewModes(
	TEXT("r.ForceDebugViewModes"),
	0,
	TEXT("0: Setting has no effect.\n")
	TEXT("1: Forces debug view modes to be available, even on cooked builds.")
	TEXT("2: Forces debug view modes to be unavailable, even on editor builds.  Removes many shader permutations for faster shader iteration."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

/** Returns true if debug viewmodes are allowed for the current platform. */
bool AllowDebugViewmodes()
{	
	int32 ForceDebugViewValue = CVarForceDebugViewModes.GetValueOnAnyThread();

	// To use debug viewmodes on consoles, r.ForceDebugViewModes must be set to 1 in ConsoleVariables.ini
	// And EngineDebugMaterials must be in the StartupPackages for the target platform.
	bool bForceEnable = ForceDebugViewValue == 1;
	bool bForceDisable = ForceDebugViewValue == 2;

	//bool bEnabled = (!IsRunningCommandlet() && !FPlatformProperties::RequiresCookedData());	
	//bEnabled |= bForceEnable;
	//bEnabled &= !bForceDisable;

	return (!bForceDisable) && (bForceEnable || (!IsRunningCommandlet() && !FPlatformProperties::RequiresCookedData()));
}

/** Returns true if debug viewmodes are allowed for the current platform. */
bool AllowDebugViewmodes(EShaderPlatform Platform)
{
#if WITH_EDITOR
	const int32 ForceDebugViewValue = CVarForceDebugViewModes.GetValueOnAnyThread();
	bool bForceEnable = ForceDebugViewValue == 1;
	bool bForceDisable = ForceDebugViewValue == 2;
	ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(ShaderPlatformToPlatformName(Platform).ToString());
	return (!bForceDisable) && (bForceEnable || !TargetPlatform || !TargetPlatform->RequiresCookedData());
#else
	return AllowDebugViewmodes();
#endif
}

#if WITH_EDITOR
static void GetShaderCompilerPlatformConfigs(const TCHAR* Key, uint64& OutPlatformFlags)
{
	for (uint32 ShaderPlatformIndex = 0; ShaderPlatformIndex < SP_NumPlatforms; ++ShaderPlatformIndex)
	{
		EShaderPlatform ShaderPlatform = EShaderPlatform(ShaderPlatformIndex);
		FName PlatformName = ShaderPlatformToPlatformName(ShaderPlatform);
		if (!PlatformName.IsNone())
		{
			FConfigFile EngineSettings;
			FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName.ToString());

			bool bEnabled = false;
			if (EngineSettings.GetBool(TEXT("ShaderCompiler"), Key, bEnabled))
			{
				uint64 Mask = (uint64)1 << ShaderPlatformIndex;
				if (bEnabled)
				{
					OutPlatformFlags |= Mask;
				}
				else
				{
					OutPlatformFlags &= ~Mask;
				}
			}
		}
	}
}
#endif

static uint64 GetKeepShaderDebugInfoPlatforms()
{
	uint64 KeepDebugInfoPlatforms = 0;

	// First check the global cvars
	static IConsoleVariable* CVarKeepDebugInfo = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.KeepDebugInfo"));
	if (CVarKeepDebugInfo && CVarKeepDebugInfo->GetInt())
	{
		KeepDebugInfoPlatforms = ~(uint64)0;
	}

#if WITH_EDITOR
	// Then load the per platform settings.
	GetShaderCompilerPlatformConfigs(TEXT("r.Shaders.KeepDebugInfo"), KeepDebugInfoPlatforms);
#endif

	return KeepDebugInfoPlatforms;
}

bool ShouldKeepShaderDebugInfo(EShaderPlatform Platform)
{
	static uint64 KeepShaderDebugInfoPlatforms = GetKeepShaderDebugInfoPlatforms();
	return (KeepShaderDebugInfoPlatforms & (uint64(1) << Platform)) != 0;
}

static uint64 GetExportShaderDebugInfoPlatforms()
{
	uint64 ExportDebugInfoPlatforms = 0;

	// First check the global cvars

	// r.DumpShaderDebugInfo should also turn on ExportShaderDebugInfo
	// The difference is that r.DumpShaderDebugInfo will also output engine debug files such as converted hlsl or SCW helper files.
	// Where as r.Shader.ExportDebugInfo is purely to export the graphics debugging tool's debug info files.
	static IConsoleVariable* CVarExportDebugInfo = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.ExportDebugInfo"));
	static IConsoleVariable* CVarDumpDebugInfo = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
	if ((CVarExportDebugInfo && CVarExportDebugInfo->GetInt()) || (CVarDumpDebugInfo && CVarDumpDebugInfo->GetInt()))
	{
		ExportDebugInfoPlatforms = ~(uint64)0;
	}

#if WITH_EDITOR
	// Then load the per platform settings.
	GetShaderCompilerPlatformConfigs(TEXT("r.Shaders.ExportDebugInfo"), ExportDebugInfoPlatforms);
#endif

	return ExportDebugInfoPlatforms;
}

bool ShouldExportShaderDebugInfo(EShaderPlatform Platform)
{
	static uint64 GExportDebugInfoPlatforms = GetExportShaderDebugInfoPlatforms();
	return (GExportDebugInfoPlatforms & (uint64(1) << Platform)) != 0;
}

bool FShaderParameterMap::FindParameterAllocation(const TCHAR* ParameterName,uint16& OutBufferIndex,uint16& OutBaseIndex,uint16& OutSize) const
{
	const FParameterAllocation* Allocation = ParameterMap.Find(ParameterName);
	if(Allocation)
	{
		OutBufferIndex = Allocation->BufferIndex;
		OutBaseIndex = Allocation->BaseIndex;
		OutSize = Allocation->Size;

		if (Allocation->bBound)
		{
			// Can detect copy-paste errors in binding parameters.  Need to fix all the false positives before enabling.
			//UE_LOG(LogShaders, Warning, TEXT("Parameter %s was bound multiple times. Code error?"), ParameterName);
		}

		Allocation->bBound = true;
		return true;
	}
	else
	{
		return false;
	}
}

bool FShaderParameterMap::ContainsParameterAllocation(const TCHAR* ParameterName) const
{
	return ParameterMap.Find(ParameterName) != NULL;
}

void FShaderParameterMap::AddParameterAllocation(const TCHAR* ParameterName,uint16 BufferIndex,uint16 BaseIndex,uint16 Size,EShaderParameterType ParameterType)
{
	check(ParameterType < EShaderParameterType::Num);
	FParameterAllocation Allocation;
	Allocation.BufferIndex = BufferIndex;
	Allocation.BaseIndex = BaseIndex;
	Allocation.Size = Size;
	Allocation.Type = ParameterType;
	ParameterMap.Add(ParameterName,Allocation);
}

void FShaderParameterMap::RemoveParameterAllocation(const TCHAR* ParameterName)
{
	ParameterMap.Remove(ParameterName);
}

void FShaderCompilerOutput::GenerateOutputHash()
{
	FSHA1 HashState;
	
	const TArray<uint8>& Code = ShaderCode.GetReadAccess();

	// we don't hash the optional attachments as they would prevent sharing (e.g. many materials share the same VS)
	uint32 ShaderCodeSize = ShaderCode.GetShaderCodeSize();

	HashState.Update(Code.GetData(), ShaderCodeSize * Code.GetTypeSize());
	ParameterMap.UpdateHash(HashState);
	HashState.Final();
	HashState.GetHash(&OutputHash.Hash[0]);
}

static void ReportVirtualShaderFilePathError(TArray<FShaderCompilerError>* CompileErrors, FString ErrorString)
{
	if (CompileErrors)
	{
		CompileErrors->Add(FShaderCompilerError(*ErrorString));
	}

	UE_LOG(LogShaders, Error, TEXT("%s"), *ErrorString);
}

bool CheckVirtualShaderFilePath(const FString& VirtualFilePath, TArray<FShaderCompilerError>* CompileErrors /*= nullptr*/)
{
	bool bSuccess = true;

	if (!VirtualFilePath.StartsWith(TEXT("/")))
	{
		FString Error = FString::Printf(TEXT("Virtual shader source file name \"%s\" should be absolute from the virtual root directory \"/\"."), *VirtualFilePath);
		ReportVirtualShaderFilePathError(CompileErrors, Error);
		bSuccess = false;
	}

	if (VirtualFilePath.Contains(TEXT("..")))
	{
		FString Error = FString::Printf(TEXT("Virtual shader source file name \"%s\" should have relative directories (\"../\") collapsed."), *VirtualFilePath);
		ReportVirtualShaderFilePathError(CompileErrors, Error);
		bSuccess = false;
	}

	if (VirtualFilePath.Contains(TEXT("\\")))
	{
		FString Error = FString::Printf(TEXT("Backslashes are not permitted in virtual shader source file name \"%s\""), *VirtualFilePath);
		ReportVirtualShaderFilePathError(CompileErrors, Error);
		bSuccess = false;
	}

	FString Extension = FPaths::GetExtension(VirtualFilePath);
	if (VirtualFilePath.StartsWith(TEXT("/Engine/Shared/")))
	{
		if ((Extension != TEXT("h")))
		{
			FString Error = FString::Printf(TEXT("Extension on virtual shader source file name \"%s\" is wrong. Only .h is allowed for shared headers that are shared between C++ and shader code."), *VirtualFilePath);
			ReportVirtualShaderFilePathError(CompileErrors, Error);
			bSuccess = false;
		}	
	}
	else
	{
		if ((Extension != TEXT("usf") && Extension != TEXT("ush")) || VirtualFilePath.EndsWith(TEXT(".usf.usf")))
		{
			FString Error = FString::Printf(TEXT("Extension on virtual shader source file name \"%s\" is wrong. Only .usf or .ush allowed."), *VirtualFilePath);
			ReportVirtualShaderFilePathError(CompileErrors, Error);
			bSuccess = false;
		}
	}

	return bSuccess;
}

/**
* Add a new entry to the list of shader source files
* Only unique entries which can be loaded are added as well as their #include files
*
* @param OutVirtualFilePaths - [out] list of shader source files to add to
* @param ShaderFilename - shader file to add
*/
static void AddShaderSourceFileEntry(TArray<FString>& OutVirtualFilePaths, FString VirtualFilePath, EShaderPlatform ShaderPlatform)
{
	check(CheckVirtualShaderFilePath(VirtualFilePath));
	if (!OutVirtualFilePaths.Contains(VirtualFilePath))
	{
		OutVirtualFilePaths.Add(VirtualFilePath);

		TArray<FString> ShaderIncludes;
		GetShaderIncludes(*VirtualFilePath, *VirtualFilePath, OutVirtualFilePaths, ShaderPlatform);
		for( int32 IncludeIdx=0; IncludeIdx < ShaderIncludes.Num(); IncludeIdx++ )
		{
			OutVirtualFilePaths.AddUnique(ShaderIncludes[IncludeIdx]);
		}
	}
}

/**
* Generates a list of virtual paths of all shader source that engine needs to load.
*
* @param OutVirtualFilePaths - [out] list of shader source files to add to
*/
static void GetAllVirtualShaderSourcePaths(TArray<FString>& OutVirtualFilePaths, EShaderPlatform ShaderPlatform)
{
	// add all shader source files for hashing
	for( TLinkedList<FVertexFactoryType*>::TIterator FactoryIt(FVertexFactoryType::GetTypeList()); FactoryIt; FactoryIt.Next() )
	{
		FVertexFactoryType* VertexFactoryType = *FactoryIt;
		if( VertexFactoryType )
		{
			FString ShaderFilename(VertexFactoryType->GetShaderFilename());
			AddShaderSourceFileEntry(OutVirtualFilePaths, ShaderFilename, ShaderPlatform);
		}
	}
	for( TLinkedList<FShaderType*>::TIterator ShaderIt(FShaderType::GetTypeList()); ShaderIt; ShaderIt.Next() )
	{
		FShaderType* ShaderType = *ShaderIt;
		if(ShaderType)
		{
			FString ShaderFilename(ShaderType->GetShaderFilename());
			AddShaderSourceFileEntry(OutVirtualFilePaths, ShaderFilename, ShaderPlatform);
		}
	}

	//#todo-rco: No need to loop through Shader Pipeline Types (yet)

	// Always add ShaderVersion.ush, so if shader forgets to include it, it will still won't break DDC.
	AddShaderSourceFileEntry(OutVirtualFilePaths, FString(TEXT("/Engine/Public/ShaderVersion.ush")), ShaderPlatform);
	AddShaderSourceFileEntry(OutVirtualFilePaths, FString(TEXT("/Engine/Private/MaterialTemplate.ush")), ShaderPlatform);
	AddShaderSourceFileEntry(OutVirtualFilePaths, FString(TEXT("/Engine/Private/Common.ush")), ShaderPlatform);
	AddShaderSourceFileEntry(OutVirtualFilePaths, FString(TEXT("/Engine/Private/Definitions.usf")), ShaderPlatform);
}

/**
* Kick off SHA verification for all shader source files
*/
void VerifyShaderSourceFiles(EShaderPlatform ShaderPlatform)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// get the list of shader files that can be used
		TArray<FString> VirtualShaderSourcePaths;
		GetAllVirtualShaderSourcePaths(VirtualShaderSourcePaths, ShaderPlatform);
		FScopedSlowTask SlowTask(VirtualShaderSourcePaths.Num());
		for( int32 ShaderFileIdx=0; ShaderFileIdx < VirtualShaderSourcePaths.Num(); ShaderFileIdx++ )
		{
			SlowTask.EnterProgressFrame(1);
			// load each shader source file. This will cache the shader source data after it has been verified
			LoadShaderSourceFile(*VirtualShaderSourcePaths[ShaderFileIdx], nullptr, nullptr);
		}
	}
}

static void LogShaderSourceDirectoryMappings()
{
	for (const auto& Iter : GShaderSourceDirectoryMappings)
	{
		UE_LOG(LogShaders, Log, TEXT("Shader directory mapping %s -> %s"), *Iter.Key, *Iter.Value);
	}
}

static FString GetShaderSourceFilePath(const FString& VirtualFilePath, TArray<FShaderCompilerError>* CompileErrors)
{
	// Make sure the .usf extension is correctly set.
	if (!CheckVirtualShaderFilePath(VirtualFilePath, CompileErrors))
	{
		return FString();
	}

	// We don't cache the output of this function because only used in LoadShaderSourceFile that is cached, or when there
	// is shader compilation errors.
	
	FString RealFilePath;

	// Look if this virtual shader source file match any directory mapping.
	const TMap<FString, FString>& ShaderSourceDirectoryMappings = GShaderSourceDirectoryMappings;
	FString ParentVirtualDirectoryPath = FPaths::GetPath(VirtualFilePath);
	FString RelativeVirtualDirectoryPath = FPaths::GetCleanFilename(VirtualFilePath);
	while (!ParentVirtualDirectoryPath.IsEmpty())
	{
		if (ShaderSourceDirectoryMappings.Contains(ParentVirtualDirectoryPath))
		{
			RealFilePath = FPaths::Combine(
				*ShaderSourceDirectoryMappings.Find(ParentVirtualDirectoryPath),
				RelativeVirtualDirectoryPath);
			break;
		}

		RelativeVirtualDirectoryPath = FPaths::GetCleanFilename(ParentVirtualDirectoryPath) / RelativeVirtualDirectoryPath;
		ParentVirtualDirectoryPath = FPaths::GetPath(ParentVirtualDirectoryPath);
	}

	// Make sure a directory mapping has matched.
	if (RealFilePath.IsEmpty())
	{
		FString Error = FString::Printf(TEXT("Can't map virtual shader source path \"%s\"."), *VirtualFilePath);
		Error += TEXT("\nDirectory mappings are:");
		for (const auto& Iter : ShaderSourceDirectoryMappings)
		{
			Error += FString::Printf(TEXT("\n  %s -> %s"), *Iter.Key, *Iter.Value);
		}

		ReportVirtualShaderFilePathError(CompileErrors, Error);
	}
	
	return RealFilePath;
}

FString ParseVirtualShaderFilename(const FString& InFilename)
{
	FString ShaderDir = FString(FPlatformProcess::ShaderDir());
	ShaderDir.ReplaceInline(TEXT("\\"), TEXT("/"));
	int32 CharIndex = ShaderDir.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, ShaderDir.Len() - 1);
	if (CharIndex != INDEX_NONE)
	{
		ShaderDir = ShaderDir.Right(ShaderDir.Len() - CharIndex);
	}

	FString RelativeFilename = InFilename.Replace(TEXT("\\"), TEXT("/"));
	// remove leading "/" because this makes path absolute on Linux (and Mac).
	if (RelativeFilename.Len() > 0 && RelativeFilename[0] == TEXT('/'))
	{
		RelativeFilename = RelativeFilename.Right(RelativeFilename.Len() - 1);
	}
	RelativeFilename = IFileManager::Get().ConvertToRelativePath(*RelativeFilename);
	CharIndex = RelativeFilename.Find(ShaderDir);
	if (CharIndex != INDEX_NONE)
	{
		CharIndex += ShaderDir.Len();
		if (RelativeFilename.GetCharArray()[CharIndex] == TEXT('/'))
		{
			CharIndex++;
		}
		if (RelativeFilename.Contains(TEXT("WorkingDirectory")))
		{
			const int32 NumDirsToSkip = 3;
			int32 NumDirsSkipped = 0;
			int32 NewCharIndex = CharIndex;

			do
			{
				NewCharIndex = RelativeFilename.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CharIndex);
				CharIndex = (NewCharIndex == INDEX_NONE) ? CharIndex : NewCharIndex + 1;
			}
			while (NewCharIndex != INDEX_NONE && ++NumDirsSkipped < NumDirsToSkip);
		}
		RelativeFilename = RelativeFilename.Mid(CharIndex, RelativeFilename.Len() - CharIndex);
	}

	// add leading "/" to the relative filename because that's what virtual shader path expects
	FString OutputFilename;
	if (RelativeFilename.Len() > 0 && RelativeFilename[0] != TEXT('/'))
	{
		OutputFilename = TEXT("/") + RelativeFilename;
	}
	else
	{
		OutputFilename = RelativeFilename;
	}
	check(CheckVirtualShaderFilePath(OutputFilename));
	return OutputFilename;
}

bool LoadShaderSourceFile(const TCHAR* VirtualFilePath, FString* OutFileContents, TArray<FShaderCompilerError>* OutCompileErrors) // TODO: const FString&
{
	// it's not expected that cooked platforms get here, but if they do, this is the final out
	if (FPlatformProperties::RequiresCookedData())
	{
		return false;
	}

	bool bResult = false;

	STAT(double ShaderFileLoadingTime = 0);
	{
		SCOPE_SECONDS_COUNTER(ShaderFileLoadingTime);

		// Protect GShaderFileCache from simultaneous access by multiple threads
		FScopeLock ScopeLock(&FileCacheCriticalSection);

		FString* CachedFile = GShaderFileCache.Find(VirtualFilePath);

		//if this file has already been loaded and cached, use that
		if (CachedFile)
		{
			if (OutFileContents)
			{
				*OutFileContents = *CachedFile;
			}
			bResult = true;
		}
		else
		{
			FString ShaderFilePath = GetShaderSourceFilePath(VirtualFilePath, OutCompileErrors);

			// verify SHA hash of shader files on load. missing entries trigger an error
			FString FileContents;
			if (!ShaderFilePath.IsEmpty() && FFileHelper::LoadFileToString(FileContents, *ShaderFilePath, FFileHelper::EHashOptions::EnableVerify|FFileHelper::EHashOptions::ErrorMissingHash) )
			{
				//update the shader file cache
				GShaderFileCache.Add(VirtualFilePath, FileContents);

				if (OutFileContents)
				{
					*OutFileContents = MoveTemp(FileContents);
				}
				bResult = true;
			}
		}
	}
	INC_FLOAT_STAT_BY(STAT_ShaderCompiling_LoadingShaderFiles,(float)ShaderFileLoadingTime);

	return bResult;
}

void LoadShaderSourceFileChecked(const TCHAR* VirtualFilePath, FString& OutFileContents)
{
	if (!LoadShaderSourceFile(VirtualFilePath, &OutFileContents, nullptr))
	{
		UE_LOG(LogShaders, Fatal, TEXT("Couldn't find source file of virtual shader path \'%s\'"), VirtualFilePath);
	}
}

/**
 * Walks InStr until we find either an end-of-line or TargetChar.
 */
const TCHAR* SkipToCharOnCurrentLine(const TCHAR* InStr, TCHAR TargetChar)
{
	const TCHAR* Str = InStr;
	if (Str)
	{
		while (*Str && *Str != TargetChar && *Str != TEXT('\n'))
		{
			++Str;
		}
		if (*Str != TargetChar)
		{
			Str = NULL;
		}
	}
	return Str;
}

/**
 * Recursively populates IncludeFilenames with the unique include filenames found in the shader file named Filename.
 */
static void GetShaderIncludes(const TCHAR* EntryPointVirtualFilePath, const TCHAR* VirtualFilePath, TArray<FString>& IncludeVirtualFilePaths, EShaderPlatform ShaderPlatform, uint32 DepthLimit, bool AddToIncludeFile)
{
	FString FileContents;
	LoadShaderSourceFile(VirtualFilePath, &FileContents, nullptr);

	//avoid an infinite loop with a 0 length string
	if (FileContents.Len() > 0)
	{
		if (AddToIncludeFile)
		{
			IncludeVirtualFilePaths.Add(VirtualFilePath);
		}

		//find the first include directive
		const TCHAR* IncludeBegin = FCString::Strstr(*FileContents, TEXT("#include "));

		uint32 SearchCount = 0;
		const uint32 MaxSearchCount = 200;
		//keep searching for includes as long as we are finding new ones and haven't exceeded the fixed limit
		while (IncludeBegin != NULL && SearchCount < MaxSearchCount && DepthLimit > 0)
		{
			//find the first double quotation after the include directive
			const TCHAR* IncludeFilenameBegin = SkipToCharOnCurrentLine(IncludeBegin, TEXT('\"'));

			if (IncludeFilenameBegin)
			{
				//find the trailing double quotation
				const TCHAR* IncludeFilenameEnd = SkipToCharOnCurrentLine(IncludeFilenameBegin + 1, TEXT('\"'));

				if (IncludeFilenameEnd)
				{
					//construct a string between the double quotations
					FString ExtractedIncludeFilename(FString((int32)(IncludeFilenameEnd - IncludeFilenameBegin - 1), IncludeFilenameBegin + 1));

					// If the include is relative, then it must be relative to the current virtual file path.
					if (!ExtractedIncludeFilename.StartsWith(TEXT("/")))
					{
						ExtractedIncludeFilename = FPaths::GetPath(VirtualFilePath) / ExtractedIncludeFilename;

						// Collapse any relative directories to allow #include "../MyFile.ush"
						FPaths::CollapseRelativeDirectories(ExtractedIncludeFilename);
					}

					//CRC the template, not the filled out version so that this shader's CRC will be independent of which material references it.
					if (ExtractedIncludeFilename == TEXT("/Engine/Generated/Material.ush"))
					{
						ExtractedIncludeFilename = TEXT("/Engine/Private/MaterialTemplate.ush");
					}

					// Ignore uniform buffer, vertex factory and instanced stereo includes
					bool bIgnoreInclude = ExtractedIncludeFilename.StartsWith(TEXT("/Engine/Generated/"));

					// Check virtual.
					bIgnoreInclude |= !CheckVirtualShaderFilePath(ExtractedIncludeFilename);

					// Include only platform specific files, which will be used by the target platform.
					bIgnoreInclude = bIgnoreInclude || GShaderHashCache.ShouldIgnoreInclude(ExtractedIncludeFilename, ShaderPlatform);

					//vertex factories need to be handled separately
					if (!bIgnoreInclude)
					{
						if (!IncludeVirtualFilePaths.Contains(ExtractedIncludeFilename))
						{
							GetShaderIncludes(EntryPointVirtualFilePath, *ExtractedIncludeFilename, IncludeVirtualFilePaths, ShaderPlatform, DepthLimit - 1, true);
						}
					}
				}
			}

			// Skip to the end of the line.
			IncludeBegin = SkipToCharOnCurrentLine(IncludeBegin, TEXT('\n'));
		
			//find the next include directive
			if (IncludeBegin && *IncludeBegin != 0)
			{
				IncludeBegin = FCString::Strstr(IncludeBegin + 1, TEXT("#include "));
			}
			SearchCount++;
		}

		if (SearchCount == MaxSearchCount || DepthLimit == 0)
		{
			UE_LOG(LogShaders, Warning, TEXT("GetShaderIncludes parsing terminated early to avoid infinite looping!\n Entrypoint \'%s\' CurrentInclude \'%s\' SearchCount %u Depth %u"), 
				EntryPointVirtualFilePath, 
				VirtualFilePath,
				SearchCount, 
				DepthLimit);
		}
	}
}

void GetShaderIncludes(const TCHAR* EntryPointVirtualFilePath, const TCHAR* VirtualFilePath, TArray<FString>& IncludeVirtualFilePaths, EShaderPlatform ShaderPlatform, uint32 DepthLimit)
{
	GetShaderIncludes(EntryPointVirtualFilePath, VirtualFilePath, IncludeVirtualFilePaths, ShaderPlatform, DepthLimit, false);
}

static void UpdateSingleShaderFilehash(FSHA1& InOutHashState, const TCHAR* VirtualFilePath, EShaderPlatform ShaderPlatform)
{
	// Get the list of includes this file contains
	TArray<FString> IncludeVirtualFilePaths;
	GetShaderIncludes(VirtualFilePath, VirtualFilePath, IncludeVirtualFilePaths, ShaderPlatform);
#if WITH_EDITOR &&  !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if ( UE_LOG_ACTIVE(LogTemp, Verbose) )
	{
		UE_LOG(LogTemp, Verbose, TEXT("Generating hash of file %s, "), VirtualFilePath);
	}
#endif
	for (int32 IncludeIndex = 0; IncludeIndex < IncludeVirtualFilePaths.Num(); IncludeIndex++)
	{
		// Load the include file and hash it
		FString IncludeFileContents;
		LoadShaderSourceFileChecked(*IncludeVirtualFilePaths[IncludeIndex], IncludeFileContents);
		InOutHashState.UpdateWithString(*IncludeFileContents, IncludeFileContents.Len());
#if WITH_EDITOR &&  !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (UE_LOG_ACTIVE(LogTemp, Verbose))
		{
			uint8 HashBytes[20];
			FSHA1::HashBuffer(&InOutHashState, sizeof(FSHA1), HashBytes);
			
			UE_LOG(LogTemp, Verbose, TEXT("Processing include file for %s, %s, %s"), VirtualFilePath, *IncludeVirtualFilePaths[IncludeIndex], *BytesToHex(HashBytes, 20));
		}
#endif
	}

	// Load the source file and hash it
	FString FileContents;
	LoadShaderSourceFileChecked(VirtualFilePath, FileContents);
	InOutHashState.UpdateWithString(*FileContents, FileContents.Len());
}

/**
 * Calculates a Hash for the given filename and its includes if it does not already exist in the Hash cache.
 * @param Filename - shader file to Hash
 * @param ShaderPlatform - shader platform to Hash
 */
const FSHAHash& GetShaderFileHash(const TCHAR* VirtualFilePath, EShaderPlatform ShaderPlatform)
{
	// Make sure we are only accessing GShaderHashCache from one thread
	//check(IsInGameThread() || IsAsyncLoading());
	STAT(double HashTime = 0);
	{
		SCOPE_SECONDS_COUNTER(HashTime);

		FSHAHash* CachedHash = GShaderHashCache.FindHash(ShaderPlatform, VirtualFilePath);

		// If a hash for this filename has been cached, use that
		if (CachedHash)
		{
			return *CachedHash;
		}

		FSHA1 HashState;
		UpdateSingleShaderFilehash(HashState, VirtualFilePath, ShaderPlatform);
		HashState.Final();

		// Update the hash cache
		FSHAHash& NewHash = GShaderHashCache.AddHash(ShaderPlatform, VirtualFilePath);
		HashState.GetHash(&NewHash.Hash[0]);

#if WITH_EDITOR &&  !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		UE_LOG(LogTemp, Verbose, TEXT("Final hash for file %s, %s"), VirtualFilePath,*BytesToHex(&NewHash.Hash[0], 20));
#endif
		INC_FLOAT_STAT_BY(STAT_ShaderCompiling_HashingShaderFiles, (float)HashTime);
		return NewHash;
	}
}

/**
 * Calculates a Hash for the given filename and its includes if it does not already exist in the Hash cache.
 * @param Filename - shader file to Hash
 * @param ShaderPlatform - shader platform to Hash
 */
const FSHAHash& GetShaderFilesHash(const TArray<FString>& VirtualFilePaths, EShaderPlatform ShaderPlatform)
{
	// Make sure we are only accessing GShaderHashCache from one thread
	//check(IsInGameThread() || IsAsyncLoading());
	STAT(double HashTime = 0);
	{
		SCOPE_SECONDS_COUNTER(HashTime);

		FString Key;
		for (const FString& Filename : VirtualFilePaths)
		{
			Key += Filename;
		}

		FSHAHash* CachedHash = GShaderHashCache.FindHash(ShaderPlatform, Key);

		// If a hash for this filename has been cached, use that
		if (CachedHash)
		{
			return *CachedHash;
		}

		FSHA1 HashState;
		for (const FString& VirtualFilePath : VirtualFilePaths)
		{
			UpdateSingleShaderFilehash(HashState, *VirtualFilePath, ShaderPlatform);
		}
		HashState.Final();

		// Update the hash cache
		FSHAHash& NewHash = GShaderHashCache.AddHash(ShaderPlatform, Key);
		HashState.GetHash(&NewHash.Hash[0]);

		INC_FLOAT_STAT_BY(STAT_ShaderCompiling_HashingShaderFiles, (float)HashTime);
		return NewHash;
	}
}

void BuildShaderFileToUniformBufferMap(TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		TArray<FString> ShaderSourceFiles;
		GetAllVirtualShaderSourcePaths(ShaderSourceFiles, GMaxRHIShaderPlatform);

		FScopedSlowTask SlowTask(ShaderSourceFiles.Num());

		// Cache UB access strings, make it case sensitive for faster search
		struct FShaderVariable
		{
			FShaderVariable(const TCHAR* ShaderVariable) :
				OriginalShaderVariable(ShaderVariable), 
				SearchKey(FString(ShaderVariable).ToUpper() + TEXT(".")),
				// MCPP inserts a space after a #define replacement, make sure we detect the uniform buffer reference
				SearchKeyWithSpace(FString(ShaderVariable).ToUpper() + TEXT(" ."))
			{}

			const TCHAR* OriginalShaderVariable;
			FString SearchKey;
			FString SearchKeyWithSpace;
		};
		// Cache each UB
		TArray<FShaderVariable> SearchKeys;
		for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
		{
			SearchKeys.Add(FShaderVariable(StructIt->GetShaderVariableName()));
		}

		// Find for each shader file which UBs it needs
		for (int32 FileIndex = 0; FileIndex < ShaderSourceFiles.Num(); FileIndex++)
		{
			SlowTask.EnterProgressFrame(1);

 			FString ShaderFileContents;
			LoadShaderSourceFileChecked(*ShaderSourceFiles[FileIndex], ShaderFileContents);

			// To allow case sensitive search which is way faster on some platforms (no need to look up locale, etc)
			ShaderFileContents.ToUpperInline();

			TArray<const TCHAR*>& ReferencedUniformBuffers = ShaderFileToUniformBufferVariables.FindOrAdd(ShaderSourceFiles[FileIndex]);

			for (int32 SearchKeyIndex = 0; SearchKeyIndex < SearchKeys.Num(); ++SearchKeyIndex)
			{
				// Searching for the uniform buffer shader variable being accessed with '.'
				if (ShaderFileContents.Contains(SearchKeys[SearchKeyIndex].SearchKey, ESearchCase::CaseSensitive)
					|| ShaderFileContents.Contains(SearchKeys[SearchKeyIndex].SearchKeyWithSpace, ESearchCase::CaseSensitive))
				{
					ReferencedUniformBuffers.AddUnique(SearchKeys[SearchKeyIndex].OriginalShaderVariable);
				}
			}
		}
	}
}

void InitializeShaderHashCache()
{
	GShaderHashCache.Initialize();
}

void CheckShaderHashCacheInclude(const FString& VirtualFilePath, EShaderPlatform ShaderPlatform)
{
	bool bIgnoreInclude = GShaderHashCache.ShouldIgnoreInclude(VirtualFilePath, ShaderPlatform);

	checkf(!bIgnoreInclude,
		TEXT("Shader compiler is trying to include %s, which is not located in IShaderFormat::GetPlatformIncludeDirectory for %s."),
		*VirtualFilePath,
		*ShaderPlatformToShaderFormatName(ShaderPlatform).ToString());
}

void InitializeShaderTypes()
{
	UE_LOG(LogShaders, Log, TEXT("InitializeShaderTypes() begin"));

	LogShaderSourceDirectoryMappings();

	TMap<FString, TArray<const TCHAR*> > ShaderFileToUniformBufferVariables;
	BuildShaderFileToUniformBufferMap(ShaderFileToUniformBufferVariables);

	FShaderType::Initialize(ShaderFileToUniformBufferVariables);
	FVertexFactoryType::Initialize(ShaderFileToUniformBufferVariables);

	FShaderPipelineType::Initialize();

	UE_LOG(LogShaders, Log, TEXT("InitializeShaderTypes() end"));
}

void UninitializeShaderTypes()
{
	UE_LOG(LogShaders, Log, TEXT("UninitializeShaderTypes() begin"));

	FShaderPipelineType::Uninitialize();

	FShaderType::Uninitialize();
	FVertexFactoryType::Uninitialize();

	UE_LOG(LogShaders, Log, TEXT("UninitializeShaderTypes() end"));
}

/**
 * Flushes the shader file and CRC cache, and regenerates the binary shader files if necessary.
 * Allows shader source files to be re-read properly even if they've been modified since startup.
 */
void FlushShaderFileCache()
{
	UE_LOG(LogShaders, Log, TEXT("FlushShaderFileCache() begin"));

	GShaderHashCache.Empty();
	GShaderFileCache.Empty();

	if (!FPlatformProperties::RequiresCookedData())
	{
		LogShaderSourceDirectoryMappings();

		TMap<FString, TArray<const TCHAR*> > ShaderFileToUniformBufferVariables;
		BuildShaderFileToUniformBufferMap(ShaderFileToUniformBufferVariables);

		for (TLinkedList<FShaderPipelineType*>::TConstIterator It(FShaderPipelineType::GetTypeList()); It; It.Next())
		{
			const auto& Stages = It->GetStages();
			for (const FShaderType* ShaderType : Stages)
			{
				((FShaderType*)ShaderType)->FlushShaderFileCache(ShaderFileToUniformBufferVariables);
			}
		}

		for(TLinkedList<FShaderType*>::TIterator It(FShaderType::GetTypeList()); It; It.Next())
		{
			It->FlushShaderFileCache(ShaderFileToUniformBufferVariables);
		}

		for(TLinkedList<FVertexFactoryType*>::TIterator It(FVertexFactoryType::GetTypeList()); It; It.Next())
		{
			It->FlushShaderFileCache(ShaderFileToUniformBufferVariables);
		}
	}

	UE_LOG(LogShaders, Log, TEXT("FlushShaderFileCache() end"));
}

void GenerateReferencedUniformBuffers(
	const TCHAR* SourceFilename, 
	const TCHAR* ShaderTypeName, 
	const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables,
	TMap<const TCHAR*,FCachedUniformBufferDeclaration>& UniformBufferEntries)
{
	TArray<FString> FilesToSearch;
	GetShaderIncludes(SourceFilename, SourceFilename, FilesToSearch, GMaxRHIShaderPlatform);
	FilesToSearch.Add(SourceFilename);

	for (int32 FileIndex = 0; FileIndex < FilesToSearch.Num(); FileIndex++)
	{
		const TArray<const TCHAR*>& FoundUniformBufferVariables = ShaderFileToUniformBufferVariables.FindChecked(FilesToSearch[FileIndex]);

		for (int32 VariableIndex = 0; VariableIndex < FoundUniformBufferVariables.Num(); VariableIndex++)
		{
			UniformBufferEntries.Add(FoundUniformBufferVariables[VariableIndex], FCachedUniformBufferDeclaration());
		}
	}
}

void SerializeUniformBufferInfo(FShaderSaveArchive& Ar, const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& UniformBufferEntries)
{
	for (TMap<const TCHAR*,FCachedUniformBufferDeclaration>::TConstIterator It(UniformBufferEntries); It; ++It)
	{
		for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
		{
			if (It.Key() == StructIt->GetShaderVariableName())
			{
				// Serialize information about the struct layout so we can detect when it changes
				const FShaderParametersMetadata& Struct = **StructIt;
				const TArray<FShaderParametersMetadata::FMember>& Members = Struct.GetMembers();

				int32 NumMembers = Members.Num();
				// Serializing with NULL so that FShaderSaveArchive will record the length without causing an actual data serialization
				Ar.Serialize(NULL, NumMembers);

				for (int32 MemberIndex = 0; MemberIndex < Members.Num(); MemberIndex++)
				{
					const FShaderParametersMetadata::FMember& Member = Members[MemberIndex];

					// Note: Only comparing number of floats used by each member and type, so this can be tricked (eg. swapping two equal size and type members)
					int32 MemberSize = Member.GetNumColumns() * Member.GetNumRows();
					Ar.Serialize(NULL, MemberSize);
					int32 MemberType = (int32)Member.GetBaseType();
					Ar.Serialize(NULL, MemberType);
				}
				break;
			}
		}
	}
}

FString FShaderCompilerError::GetShaderSourceFilePath() const
{
	if (IFileManager::Get().FileExists(*ErrorVirtualFilePath))
	{
		return ErrorVirtualFilePath;
	}
	else
	{
		return ::GetShaderSourceFilePath(ErrorVirtualFilePath, nullptr);
	}
}

const TMap<FString, FString>& AllShaderSourceDirectoryMappings()
{
	return GShaderSourceDirectoryMappings;
}

void ResetAllShaderSourceDirectoryMappings()
{
	GShaderSourceDirectoryMappings.Reset();
}

void AddShaderSourceDirectoryMapping(const FString& VirtualShaderDirectory, const FString& RealShaderDirectory)
{
	check(IsInGameThread());

	if (FPlatformProperties::RequiresCookedData())
	{
		return;
	}

	// Do sanity checks of the virtual shader directory to map.
	check(VirtualShaderDirectory.StartsWith(TEXT("/")));
	check(!VirtualShaderDirectory.EndsWith(TEXT("/")));
	check(!VirtualShaderDirectory.Contains(FString(TEXT("."))));

	// Detect collisions with any other mappings.
	check(!GShaderSourceDirectoryMappings.Contains(VirtualShaderDirectory));

	// Make sure the real directory to map exists.
	check(FPaths::DirectoryExists(RealShaderDirectory));

	// Make sure the Generated directory does not exist, because is reserved for C++ generated shader source
	// by the FShaderCompilerEnvironment::IncludeVirtualPathToContentsMap member.
	checkf(!FPaths::DirectoryExists(RealShaderDirectory / TEXT("Generated")),
		TEXT("\"%s/Generated\" is not permitted to exist since C++ generated shader file would be mapped to this directory."), *RealShaderDirectory);

	UE_LOG(LogShaders, Log, TEXT("Mapping virtual shader directory %s to %s"),
		*VirtualShaderDirectory, *RealShaderDirectory);
	GShaderSourceDirectoryMappings.Add(VirtualShaderDirectory, RealShaderDirectory);
}
