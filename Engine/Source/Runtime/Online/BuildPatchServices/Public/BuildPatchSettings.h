// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Variant.h"
#include "Interfaces/IBuildManifest.h"
#include "BuildPatchInstall.h"
#include "BuildPatchVerify.h"
#include "BuildPatchFeatureLevel.h"

namespace BuildPatchServices
{
	/**
	 * Defines a list of all build patch services initialization settings, can be used to override default init behaviors.
	 */
	struct FBuildPatchServicesInitSettings
	{
	public:
		/**
		 * Default constructor. Initializes all members with default behavior values.
		 */
		FBuildPatchServicesInitSettings();

		// The application settings directory.
		FString ApplicationSettingsDir;
		// The application project name.
		FString ProjectName;
		// The local machine config file name.
		FString LocalMachineConfigFileName;
	};

	/**
	 * Defines a list of all the options of an installation task.
	 */
	struct FInstallerConfiguration
	{
		// The manifest that the current install was generated from (if applicable).
		IBuildManifestPtr CurrentManifest;
		// The manifest to be installed.
		IBuildManifestRef InstallManifest;
		// The directory to install to.
		FString InstallDirectory;
		// The directory for storing the intermediate files. This would usually be inside the InstallDirectory. Empty string will use module's global setting.
		FString StagingDirectory;
		// The directory for placing files that are believed to have local changes, before we overwrite them. Empty string will use module's global setting. If both empty, the feature disables.
		FString BackupDirectory;
		// The list of chunk database filenames that will be used to pull patch data from.
		TArray<FString> ChunkDatabaseFiles;
		// The list of cloud directory roots that will be used to pull patch data from. Empty array will use module's global setting.
		TArray<FString> CloudDirectories;
		// The set of tags that describe what to be installed. Empty set means full installation.
		TSet<FString> InstallTags;
		// The mode for installation.
		EInstallMode InstallMode;
		// The mode for verification.
		EVerifyMode VerifyMode;
		// Whether the operation is a repair to an existing installation only.
		bool bIsRepair;
		// Whether to run the prerequisite installer provided if it hasn't been ran before on this machine.
		bool bRunRequiredPrereqs;
		// Whether to allow this installation to run concurrently with any existing installations.
		bool bAllowConcurrentExecution;

		/**
		 * Construct with install manifest, provides common defaults for other settings.
		 */
		FInstallerConfiguration(const IBuildManifestRef& InInstallManifest)
			: CurrentManifest(nullptr)
			, InstallManifest(InInstallManifest)
			, InstallDirectory()
			, StagingDirectory()
			, BackupDirectory()
			, ChunkDatabaseFiles()
			, CloudDirectories()
			, InstallTags()
			, InstallMode(EInstallMode::NonDestructiveInstall)
			, VerifyMode(EVerifyMode::ShaVerifyAllFiles)
			, bIsRepair(false)
			, bRunRequiredPrereqs(true)
			, bAllowConcurrentExecution(false)
		{}

		/**
		 * RValue constructor to allow move semantics.
		 */
		FInstallerConfiguration(FInstallerConfiguration&& MoveFrom)
			: CurrentManifest(MoveTemp(MoveFrom.CurrentManifest))
			, InstallManifest(MoveTemp(MoveFrom.InstallManifest))
			, InstallDirectory(MoveTemp(MoveFrom.InstallDirectory))
			, StagingDirectory(MoveTemp(MoveFrom.StagingDirectory))
			, BackupDirectory(MoveTemp(MoveFrom.BackupDirectory))
			, ChunkDatabaseFiles(MoveTemp(MoveFrom.ChunkDatabaseFiles))
			, CloudDirectories(MoveTemp(MoveFrom.CloudDirectories))
			, InstallTags(MoveTemp(MoveFrom.InstallTags))
			, InstallMode(MoveFrom.InstallMode)
			, VerifyMode(MoveFrom.VerifyMode)
			, bIsRepair(MoveFrom.bIsRepair)
			, bRunRequiredPrereqs(MoveFrom.bRunRequiredPrereqs)
			, bAllowConcurrentExecution(MoveFrom.bAllowConcurrentExecution)
		{}

		/**
		 * Copy constructor.
		 */
		FInstallerConfiguration(const FInstallerConfiguration& CopyFrom)
			: CurrentManifest(CopyFrom.CurrentManifest)
			, InstallManifest(CopyFrom.InstallManifest)
			, InstallDirectory(CopyFrom.InstallDirectory)
			, StagingDirectory(CopyFrom.StagingDirectory)
			, BackupDirectory(CopyFrom.BackupDirectory)
			, ChunkDatabaseFiles(CopyFrom.ChunkDatabaseFiles)
			, CloudDirectories(CopyFrom.CloudDirectories)
			, InstallTags(CopyFrom.InstallTags)
			, InstallMode(CopyFrom.InstallMode)
			, VerifyMode(CopyFrom.VerifyMode)
			, bIsRepair(CopyFrom.bIsRepair)
			, bRunRequiredPrereqs(CopyFrom.bRunRequiredPrereqs)
			, bAllowConcurrentExecution(CopyFrom.bAllowConcurrentExecution)
		{}
	};

	/**
	 * Defines a list of all options for generation tasks.
	 */
	struct FGenerationConfiguration
	{
		// The client feature level to output data for.
		EFeatureLevel FeatureLevel;
		// The directory to analyze.
		FString RootDirectory;
		// The ID of the app of this build.
		uint32 AppId;
		// The name of the app of this build.
		FString AppName;
		// The version string for this build.
		FString BuildVersion;
		// The local exe path that would launch this build.
		FString LaunchExe;
		// The command line that would launch this build.
		FString LaunchCommand;
		// The path to a file containing a \r\n separated list of RootDirectory relative files to read.
		FString InputListFile;
		// The path to a file containing a \r\n separated list of RootDirectory relative files to ignore.
		FString IgnoreListFile;
		// The path to a file containing a \r\n separated list of RootDirectory relative files followed by attribute keywords.
		FString AttributeListFile;
		// The set of identifiers which the prerequisites satisfy.
		TSet<FString> PrereqIds;
		// The display name of the prerequisites installer.
		FString PrereqName;
		// The path to the prerequisites installer.
		FString PrereqPath;
		// The command line arguments for the prerequisites installer.
		FString PrereqArgs;
		// The maximum age (in days) of existing data files which can be reused in this build.
		float DataAgeThreshold;
		// Indicates whether data age threshold should be honored. If false, ALL data files can be reused.
		bool bShouldHonorReuseThreshold;
		// The chunk window size to be used when saving out new data.
		uint32 OutputChunkWindowSize;
		// Indicates whether any window size chunks should be matched, rather than just out output window size.
		bool bShouldMatchAnyWindowSize;
		// Map of custom fields to add to the manifest.
		TMap<FString, FVariant> CustomFields;
		// The cloud directory that all patch data will be saved to. An empty value will use module's global setting.
		FString CloudDirectory;
		// The output manifest filename.
		FString OutputFilename;

		/**
		 * Default constructor
		 */
		FGenerationConfiguration()
			: FeatureLevel(EFeatureLevel::Latest)
			, RootDirectory()
			, AppId()
			, AppName()
			, BuildVersion()
			, LaunchExe()
			, LaunchCommand()
			, IgnoreListFile()
			, AttributeListFile()
			, PrereqIds()
			, PrereqName()
			, PrereqPath()
			, PrereqArgs()
			, DataAgeThreshold()
			, bShouldHonorReuseThreshold()
			, OutputChunkWindowSize(1048576)
			, bShouldMatchAnyWindowSize(true)
			, CustomFields()
			, CloudDirectory()
			, OutputFilename()
		{}
	};
}
