// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolModes/PatchGenerationMode.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "BuildPatchFeatureLevel.h"
#include "BuildPatchTool.h"

using namespace BuildPatchTool;

namespace Constants
{
	static const FString Comma(TEXT(","));
	static const FString Equals(TEXT("="));
	static const FString DoubleQuote(TEXT("\""));
	static const FString SingleQuote(TEXT("'"));
	static const FString Slash(TEXT("/"));
	static const FString Backslash(TEXT("\\"));
	static const FString Custom(TEXT("custom"));
	static const FString CustomInt(TEXT("customint"));
	static const FString CustomFloat(TEXT("customfloat"));
}

class FPatchGenerationToolMode : public IToolMode
{
public:
	FPatchGenerationToolMode(IBuildPatchServicesModule& InBpsInterface)
		: BpsInterface(InBpsInterface)
		, ChunkWindowSize(1048576)
		, bIgnoreOtherWindowSizes(false)
	{}

	virtual~FPatchGenerationToolMode()
	{}

	virtual EReturnCode Execute() override
	{
		// Parse commandline
		if (ProcessCommandline() == false)
		{
			return EReturnCode::ArgumentProcessingError;
		}

		// Print help if requested
		if (bHelp)
		{
			UE_LOG(LogBuildPatchTool, Display, TEXT("GENERATE PATCH DATA MODE"));
			UE_LOG(LogBuildPatchTool, Display, TEXT("This tool supports generating chunk based patches. Chunk based patch data will be generated by default."));
			UE_LOG(LogBuildPatchTool, Display, TEXT(""));
			UE_LOG(LogBuildPatchTool, Display, TEXT("Required arguments:"));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -mode=PatchGeneration    Must be specified to launch the tool in patch generation mode."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -FeatureLevel=Latest     Specifies the client feature level to output data for. See BuildPatchServices::EFeatureLevel for possible values."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -BuildRoot=\"\"            Specifies in quotes the directory containing the build image to be read."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -CloudDir=\"\"             Specifies in quotes the cloud directory where existing data will be recognized from, and new data added to."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -AppName=\"\"              Specifies in quotes, the name of the app."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -BuildVersion=\"\"         Specifies in quotes, the version string for the build image."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -AppLaunch=\"\"            Specifies in quotes, the path to the app executable, must be relative to, and inside of BuildRoot."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -AppArgs=\"\"              Specifies in quotes, the commandline to send to the app on launch."));
			UE_LOG(LogBuildPatchTool, Display, TEXT(""));
			UE_LOG(LogBuildPatchTool, Display, TEXT("Optional arguments:"));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -AppID=123456                Specifies without quotes, the ID number for the app. This will default to 0 if not provided."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -FileList=\"\"                 Specifies in quotes, the path to a text file containing BuildRoot relative files to be included in the build."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -FileIgnoreList=\"\"           Specifies in quotes, the path to a text file containing BuildRoot relative files, separated by \\r\\n line endings, to not be included in the build."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -FileAttributeList=\"\"        Specifies in quotes, the path to a text file containing quoted BuildRoot relative files followed by optional attribute keywords readonly compressed executable, separated by \\r\\n line endings. These attribute will be applied when build is installed client side."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -PrereqIds=\"\"                Specifies in quotes, a comma-separated list of identifiers that the prerequisites satisfy. At install time, a machine which already has installed prerequisites with all of these ids will skip prerequisite installation."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -PrereqName=\"\"               Specifies in quotes, the display name xfor the prerequisites installer."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -PrereqPath=\"\"               Specifies in quotes, the prerequisites installer to launch on successful product install."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("                               This path supports a string replace for \"$[RootDirectory]\". This will be replaced with the root path before executing. The replacement will include trailing /."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -PrereqArgs=\"\"               Specifies in quotes, the commandline to send to prerequisites installer on launch."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("                               This value supports string replacements for \"$[RootDirectory]\" and also \"$[LogDirectory]\". LogDirectory is the path to the program's log output directory so your prereq could create logs there. The replacement will include trailing /."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("                               \"$[Quote]\" can also be used to get a quote character, this is important because the BPT commandline already uses quotes for token parsing."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -DataAgeThreshold=12.5       Specified the maximum age (in days) of existing manifest files whose referenced patch data can be reused in the generated manifest."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -Custom=\"field=value\"        Adds a custom string field to the build manifest."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -CustomInt=\"field=number\"    Adds a custom int64 field to the build manifest."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -CustomFloat=\"field=number\"  Adds a custom double field to the build manifest."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -OutputFilename=\"\"           Specifies in quotes an override for the output manifest filename. Extension of .manifest will be added if not present."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -ChunkWindowSize=1000000     Specifies in bytes, the data window size that should be used when saving new chunks. Default is 1048576 (1MiB)."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("  -IgnoreOtherWindowSizes      If provided, the generation code will only accept chunk matches that are the same as ChunkWindowSize."));
			UE_LOG(LogBuildPatchTool, Display, TEXT(""));
			UE_LOG(LogBuildPatchTool, Display, TEXT("NB: If -DataAgeThreshold is not supplied on the command-line, then all existing data is eligible for reuse in the generated manifest."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("NB: If -OutputFilename is not supplied on the command-line, the default of AppNameBuildVersion.manifest will be used."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("NB:    -OutputFilename must be a clean filename with no path."));
			UE_LOG(LogBuildPatchTool, Display, TEXT("NB: If -ChunkWindowSize provided, it is clamped max 10485760 (10MiB) to min 32000 (32KB)."));
			UE_LOG(LogBuildPatchTool, Display, TEXT(""));
			return EReturnCode::OK;
		}

		// Check existence of the file list
		if (!FileList.IsEmpty() && !FPaths::FileExists(FileList))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Provided file list was not found %s"), *FileList);
			return EReturnCode::FileNotFound;
		}

		// Check existence of file ignore list
		if (!FileIgnoreList.IsEmpty() && !FPaths::FileExists(FileIgnoreList))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Provided file ignore list was not found %s"), *FileIgnoreList);
			return EReturnCode::FileNotFound;
		}

		// Check existence of file attributes list
		if (!FileAttributeList.IsEmpty() && !FPaths::FileExists(FileAttributeList))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Provided file attribute list was not found %s"), *FileAttributeList);
			return EReturnCode::FileNotFound;
		}

		// Default the OutputFilename if not provided
		if (OutputFilename.IsEmpty())
		{
			OutputFilename = FDefaultValueHelper::RemoveWhitespaces(AppName + BuildVersion) + TEXT(".manifest");
		}
		// Otherwise check the parameter
		else
		{
			if (OutputFilename.Contains(TEXT("/")))
			{
				UE_LOG(LogBuildPatchTool, Error, TEXT("Provided OutputFilename should be clean filename only. Invalid arg: %s"), *OutputFilename);
				return EReturnCode::ArgumentProcessingError;
			}
		}

		// Setup and run
		BuildPatchServices::FChunkBuildConfiguration Settings;
		const uint32 DefaultChunkWindowSize = Settings.OutputChunkWindowSize;
		if (!BuildPatchServices::FeatureLevelFromString(*FeatureLevel, Settings.FeatureLevel))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Provided FeatureLevel is not recognised. Invalid arg: -FeatureLevel=%s"), *FeatureLevel);
			return EReturnCode::ArgumentProcessingError;
		}
		Settings.RootDirectory = BuildRoot;
		Settings.AppId = TCString<TCHAR>::Atoi64(*AppId);
		Settings.AppName = AppName;
		Settings.BuildVersion = BuildVersion;
		Settings.LaunchExe = AppLaunch;
		Settings.LaunchCommand = AppArgs;
		Settings.InputListFile = FileList;
		Settings.IgnoreListFile = FileIgnoreList;
		Settings.AttributeListFile = FileAttributeList;
		Settings.PrereqIds = PrereqIdsSet;
		Settings.PrereqName = PrereqName;
		Settings.PrereqPath = PrereqPath;
		Settings.PrereqArgs = PrereqArgs;
		Settings.DataAgeThreshold = TCString<TCHAR>::Atod(*DataAgeThreshold);
		Settings.bShouldHonorReuseThreshold = DataAgeThreshold.IsEmpty() == false;
		Settings.OutputChunkWindowSize = ChunkWindowSize;
		Settings.bShouldMatchAnyWindowSize = !bIgnoreOtherWindowSizes && Settings.FeatureLevel >= BuildPatchServices::EFeatureLevel::VariableSizeChunks;
		Settings.CustomFields = CustomFields;
		Settings.CloudDirectory = CloudDir;
		Settings.OutputFilename = OutputFilename;

		// Check feature compatibility
		const bool bHasCustomFields = Settings.CustomFields.Num() > 0;
		if (Settings.FeatureLevel < BuildPatchServices::EFeatureLevel::CustomFields && bHasCustomFields)
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Invalid args: FeatureLevel %s is not compatible with Custom, CustomInt, or CustomFloat."), BuildPatchServices::FeatureLevelToString(Settings.FeatureLevel));
			return EReturnCode::ArgumentProcessingError;
		}
		const bool bHasAnyPrereqInfo = !PrereqName.IsEmpty() || !PrereqPath.IsEmpty() || !PrereqArgs.IsEmpty();
		if (Settings.FeatureLevel < BuildPatchServices::EFeatureLevel::StoresPrerequisitesInfo && bHasAnyPrereqInfo)
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Invalid args: FeatureLevel %s is not compatible with PrereqName, PrereqPath, or PrereqArgs."), BuildPatchServices::FeatureLevelToString(Settings.FeatureLevel));
			return EReturnCode::ArgumentProcessingError;
		}
		const bool bHasPrereqIds = Settings.PrereqIds.Num() > 0;
		if (Settings.FeatureLevel < BuildPatchServices::EFeatureLevel::StoresPrerequisiteIds && bHasPrereqIds)
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Invalid args: FeatureLevel %s is not compatible with PrereqIds."), BuildPatchServices::FeatureLevelToString(Settings.FeatureLevel));
			return EReturnCode::ArgumentProcessingError;
		}
		const bool bHasNonDefaultWindowSize = Settings.OutputChunkWindowSize != DefaultChunkWindowSize;
		if (Settings.FeatureLevel < BuildPatchServices::EFeatureLevel::VariableSizeChunks && bHasNonDefaultWindowSize)
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("Invalid args: FeatureLevel %s is not compatible with -ChunkWindowSize=%u."), BuildPatchServices::FeatureLevelToString(Settings.FeatureLevel), Settings.OutputChunkWindowSize);
			return EReturnCode::ArgumentProcessingError;
		}

		// Run the build generation
		bool bSuccess = BpsInterface.ChunkBuildDirectory(Settings);
		return bSuccess ? EReturnCode::OK : EReturnCode::ToolFailure;
	}

private:

	bool ParsePrereqIds(const FString& ParamValue, TSet<FString>& OutPrereqIds)
	{
		if (ParamValue.Contains(Constants::Slash)
			|| ParamValue.Contains(Constants::Backslash)
			|| ParamValue.Contains(Constants::DoubleQuote)
			|| ParamValue.Contains(Constants::Slash))
		{
			return false;
		}

		TArray<FString> ParamValues;
		ParamValue.ParseIntoArray(ParamValues, *Constants::Comma);
		OutPrereqIds.Append(ParamValues);
		return true;
	}

	bool ParseCustomField(const FString& Switch, TMap<FString, FVariant>& Fields)
	{
		FString Type, Left, Right;

		Switch.Split(Constants::Equals, &Type, &Right);
		Type.ToLowerInline();
		Right.Split(Constants::Equals, &Left, &Right);
		Left.TrimStartAndEndInline();
		Right.TrimStartAndEndInline();
		if (Type.Equals(Constants::Custom, ESearchCase::CaseSensitive))
		{
			CustomFields.Add(Left, FVariant(Right));
		}
		else if (Type.Equals(Constants::CustomInt, ESearchCase::CaseSensitive))
		{
			if (!Right.IsNumeric())
			{
				UE_LOG(LogBuildPatchTool, Error, TEXT("An error occurred processing numeric token from commandline -%s"), *Switch);
				return false;
			}
			CustomFields.Add(Left, FVariant(TCString<TCHAR>::Atoi64(*Right)));
		}
		else if (Type.Equals(Constants::CustomFloat, ESearchCase::CaseSensitive))
		{
			if (!Right.IsNumeric())
			{
				UE_LOG(LogBuildPatchTool, Error, TEXT("An error occurred processing numeric token from commandline -%s"), *Switch);
				return false;
			}
			CustomFields.Add(Left, FVariant(TCString<TCHAR>::Atod(*Right)));
		}

		return true;
	}

	bool ProcessCommandline()
	{
#define PARSE_SWITCH(Switch) ParseSwitch(TEXT(#Switch "="), Switch, Switches)
		TArray<FString> Tokens, Switches;
		FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);

		bHelp = ParseOption(TEXT("help"), Switches);
		if (bHelp)
		{
			return true;
		}

		// Grab the FeatureLevel. This is required param but safe to default, we can change this to a warning after first release, and then an error later, as part of a friendly roll out.
		PARSE_SWITCH(FeatureLevel);
		FeatureLevel.TrimStartAndEndInline();
		if (FeatureLevel.IsEmpty())
		{
			UE_LOG(LogBuildPatchTool, Log, TEXT("FeatureLevel was not provided, defaulting to LatestJson. Please provide the FeatureLevel commandline argument which matches the existing client support."));
			FeatureLevel = TEXT("LatestJson");
		}

		// Get all required parameters
		if (!(PARSE_SWITCH(CloudDir)
		   && PARSE_SWITCH(BuildRoot)
		   && PARSE_SWITCH(AppName)
		   && PARSE_SWITCH(BuildVersion)
		   && PARSE_SWITCH(AppLaunch)
		   && PARSE_SWITCH(AppArgs)))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("CloudDir, BuildRoot, AppName, BuildVersion, AppLaunch, and AppArgs are required parameters"));
			return false;
		}
		NormalizeUriPath(CloudDir);
		NormalizeUriPath(BuildRoot);
		NormalizeUriFile(AppLaunch);

		// Get optional parameters
		PARSE_SWITCH(AppId);
		PARSE_SWITCH(FileList);
		PARSE_SWITCH(FileIgnoreList);
		PARSE_SWITCH(FileAttributeList);
		PARSE_SWITCH(PrereqIds);
		PARSE_SWITCH(PrereqName);
		PARSE_SWITCH(PrereqPath);
		PARSE_SWITCH(PrereqArgs);
		PARSE_SWITCH(DataAgeThreshold);
		PARSE_SWITCH(ChunkWindowSize);
		bIgnoreOtherWindowSizes = ParseOption(TEXT("IgnoreOtherWindowSizes"), Switches);
		PARSE_SWITCH(OutputFilename);
		NormalizeUriFile(FileList);
		NormalizeUriFile(FileIgnoreList);
		NormalizeUriFile(FileAttributeList);
		NormalizeUriFile(PrereqPath);
		NormalizeUriFile(OutputFilename);

		// Check manifest file extension.
		const TCHAR* ManifestExtension = TEXT(".manifest");
		if (!OutputFilename.IsEmpty() && !OutputFilename.EndsWith(ManifestExtension))
		{
			OutputFilename += ManifestExtension;
		}

		// Clamp ChunkWindowSize to sane range.
		const uint32 RequestedChunkWindowSize = ChunkWindowSize;
		ChunkWindowSize = FMath::Clamp<uint32>(ChunkWindowSize, 32000, 10485760);
		if (RequestedChunkWindowSize != ChunkWindowSize)
		{
			UE_LOG(LogBuildPatchTool, Warning, TEXT("Requested -ChunkWindowSize=%u is outside of allowed range 10485760 >= n >= 32000. Please update your args to be within range. Continuing with %u."), RequestedChunkWindowSize, ChunkWindowSize);
		}

		// Check numeric values
		if (!AppId.IsEmpty() && !AppId.IsNumeric())
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("An error occurred processing numeric token from commandline -AppId=%s"), *AppId);
			return false;
		}
		if (!DataAgeThreshold.IsEmpty() && !DataAgeThreshold.IsNumeric())
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("An error occurred processing numeric token from commandline -DataAgeThreshold=%s"), *DataAgeThreshold);
			return false;
		}

		// Get custom fields to add to manifest
		// These are optional, but a failure to parse one is an error
		for (const FString& Switch : Switches)
		{
			if (Switch.StartsWith(Constants::Custom) && !ParseCustomField(Switch, CustomFields))
			{
				return false;
			}
		}

		if (!PrereqIds.IsEmpty() && !ParsePrereqIds(PrereqIds, PrereqIdsSet))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("An error occurred processing comma-separated list from commandline -PrereqIds=%s"), *PrereqIds);
			return false;
		}

		return true;
#undef PARSE_SWITCH
	}

private:
	IBuildPatchServicesModule& BpsInterface;
	bool bHelp;
	FString FeatureLevel;
	FString BuildRoot;
	FString CloudDir;
	FString AppId;
	FString AppName;
	FString BuildVersion;
	FString AppLaunch;
	FString AppArgs;
	FString PrereqIds;
	TSet<FString> PrereqIdsSet;
	FString PrereqName;
	FString PrereqPath;
	FString PrereqArgs;
	FString FileList;
	FString FileIgnoreList;
	FString FileAttributeList;
	FString DataAgeThreshold;
	uint32 ChunkWindowSize;
	bool bIgnoreOtherWindowSizes;
	TMap<FString, FVariant> CustomFields;
	FString OutputFilename;
};

BuildPatchTool::IToolModeRef BuildPatchTool::FPatchGenerationToolModeFactory::Create(IBuildPatchServicesModule& BpsInterface)
{
	return MakeShareable(new FPatchGenerationToolMode(BpsInterface));
}
