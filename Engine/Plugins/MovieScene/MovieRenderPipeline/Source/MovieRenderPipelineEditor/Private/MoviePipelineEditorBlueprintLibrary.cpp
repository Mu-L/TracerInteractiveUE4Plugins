// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineEditorBlueprintLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "MoviePipelineQueue.h"
#include "Misc/MessageDialog.h"
#include "PackageHelperFunctions.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Settings/EditorLoadingSavingSettings.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEditorBlueprintLibrary"

bool UMoviePipelineEditorBlueprintLibrary::ExportConfigToAsset(const UMoviePipelineMasterConfig* InConfig, const FString& InPackagePath, const FString& InFileName, const bool bInSaveAsset, UMoviePipelineMasterConfig*& OutAsset, FText& OutErrorReason)
{
	if(!InConfig)
	{
		OutErrorReason = LOCTEXT("CantExportNullConfigToPackage", "Can't export a null configuration to a package.");
		return false;
	}
	
	FString FixedAssetName = ObjectTools::SanitizeObjectName(InFileName);
	FString NewPackageName = FPackageName::GetLongPackagePath(InPackagePath) + TEXT("/") + FixedAssetName;

	if (!FPackageName::IsValidLongPackageName(NewPackageName, false, &OutErrorReason))
	{
		return false;
	}

	UPackage* NewPackage = CreatePackage(*NewPackageName);
	NewPackage->AddToRoot();
	
	// Duplicate the provided config into this package.
	UMoviePipelineMasterConfig* NewConfig = Cast<UMoviePipelineMasterConfig>(StaticDuplicateObject(InConfig, NewPackage, FName(*InFileName), RF_NoFlags));
	NewConfig->SetFlags(RF_Public | RF_Transactional | RF_Standalone);
	NewConfig->MarkPackageDirty();

	// Mark it so it shows up in the Content Browser immediately
	FAssetRegistryModule::AssetCreated(NewConfig);

	// If they want to save, ask them to save (and add to version control)
	if (bInSaveAsset)
	{
		TArray<UPackage*> Packages;
		Packages.Add(NewConfig->GetOutermost());

		return UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
	}

	return true;
}

bool UMoviePipelineEditorBlueprintLibrary::IsMapValidForRemoteRender(const TArray<UMoviePipelineExecutorJob*>& InJobs)
{
	for (const UMoviePipelineExecutorJob* Job : InJobs)
	{
		FString PackageName = Job->Map.GetLongPackageName();
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			return false;
		}
	}
	return true;
}

void UMoviePipelineEditorBlueprintLibrary::WarnUserOfUnsavedMap()
{
	FText FailureReason = LOCTEXT("UnsavedMapFailureDialog", "One or more jobs in the queue have an unsaved map as their target map. These unsaved maps cannot be loaded by an external process, and the render has been aborted.");
	FMessageDialog::Open(EAppMsgType::Ok, FailureReason);
}

UMoviePipelineQueue* UMoviePipelineEditorBlueprintLibrary::SaveQueueToManifestFile(UMoviePipelineQueue* InPipelineQueue, FString& OutManifestFilePath)
{
	FString InFileName = TEXT("QueueManifest");
	FString InPackagePath = TEXT("/Engine/MovieRenderPipeline/Editor/Transient");

	FString FixedAssetName = ObjectTools::SanitizeObjectName(InFileName);
	FString NewPackageName = FPackageName::GetLongPackagePath(InPackagePath) + TEXT("/") + FixedAssetName;

	// If there's already a package with this name, rename it so that the newly created one can always get a fixed name.
	// The fixed name is important because in the new process it'll start the unique name count over.
	if (UPackage* OldPackage = FindObject<UPackage>(nullptr, *NewPackageName))
	{
		FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass(), "DEAD_NewProcessExecutor_SerializedPackage");
		OldPackage->Rename(*UniqueName.ToString());
		OldPackage->SetFlags(RF_Transient);
	}

	UPackage* NewPackage = CreatePackage(*NewPackageName);

	// Duplicate the Queue into this package as we don't want to just rename the existing that belongs to the editor subsystem.
	UMoviePipelineQueue* DuplicatedQueue = CastChecked<UMoviePipelineQueue>(StaticDuplicateObject(InPipelineQueue, NewPackage));
	DuplicatedQueue->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	// Save the package to disk.
	FString ManifestFileName = TEXT("MovieRenderPipeline/QueueManifest") + FPackageName::GetTextAssetPackageExtension();
	OutManifestFilePath = FPaths::ProjectSavedDir() / ManifestFileName;

	// Fully load the package before trying to save.
	LoadPackage(NewPackage, *NewPackageName, LOAD_None);

	{
		UEditorLoadingSavingSettings* SaveSettings = GetMutableDefault<UEditorLoadingSavingSettings>();
		uint32 bSCCAutoAddNewFiles = SaveSettings->bSCCAutoAddNewFiles;
		SaveSettings->bSCCAutoAddNewFiles = 0;
		bool bSuccess = SavePackageHelper(NewPackage, *OutManifestFilePath);
		SaveSettings->bSCCAutoAddNewFiles = bSCCAutoAddNewFiles;
		
		if(!bSuccess)
		{
			return nullptr;
		}
	}

	NewPackage->SetFlags(RF_Transient);
	NewPackage->ClearFlags(RF_Standalone);
	DuplicatedQueue->SetFlags(RF_Transient);
	DuplicatedQueue->ClearFlags(RF_Public | RF_Transactional | RF_Standalone);

	return DuplicatedQueue;
}

FString UMoviePipelineEditorBlueprintLibrary::ConvertManifestFileToString(const FString& InManifestFilePath)
{
	// Due to API limitations we can't convert package -> text directly and instead need to re-load it, escape it, and then put it onto the command line :-)
	FString OutString;
	FFileHelper::LoadFileToString(OutString, *InManifestFilePath);

	return OutString;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineEditorBlueprintLibrary"