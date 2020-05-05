// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReimportHairStrandsFactory.h"

#include "EditorFramework/AssetImportData.h"
#include "HairDescription.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"
#include "GroomAssetImportData.h"
#include "GroomImportOptions.h"
#include "GroomImportOptionsWindow.h"
#include "HairStrandsImporter.h"
#include "HairStrandsTranslator.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "HairStrandsFactory"

UReimportHairStrandsFactory::UReimportHairStrandsFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditorImport = false;

	// The HairStrandsFactory should come before the Reimport factory
	ImportPriority -= 1;
}

bool UReimportHairStrandsFactory::FactoryCanImport(const FString& Filename)
{
	return false;
}

bool UReimportHairStrandsFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	// Lazy init the translators before first use of the CDO
	if (HasAnyFlags(RF_ClassDefaultObject) && Formats.Num() == 0)
	{
		InitTranslators();
	}

	UAssetImportData* ImportData = nullptr;
	if (UGroomAsset* HairAsset = Cast<UGroomAsset>(Obj))
	{
		ImportData = HairAsset->AssetImportData;
	}

	if (ImportData)
	{
		if (GetTranslator(ImportData->GetFirstFilename()).IsValid())
		{
			ImportData->ExtractFilenames(OutFilenames);
			return true;
		}
	}

	return false;
}

void UReimportHairStrandsFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UGroomAsset* Asset = Cast<UGroomAsset>(Obj);
	if (Asset && Asset->AssetImportData && ensure(NewReimportPaths.Num() == 1))
	{
		Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UReimportHairStrandsFactory::Reimport(UObject* Obj)
{
	if (UGroomAsset* HairAsset = Cast<UGroomAsset>(Obj))
	{
		if (!HairAsset || !HairAsset->AssetImportData)
		{
			return EReimportResult::Failed;
		}

		CurrentFilename = HairAsset->AssetImportData->GetFirstFilename();

		UGroomAssetImportData* GroomAssetImportData = Cast<UGroomAssetImportData>(HairAsset->AssetImportData);
		UGroomImportOptions* CurrentOptions = nullptr;
		if (GroomAssetImportData)
		{
			// Duplicate the options to prevent dirtying the asset when they are modified but the re-import is cancelled
			CurrentOptions = DuplicateObject<UGroomImportOptions>(GroomAssetImportData->ImportOptions, nullptr);
		}
		else
		{
			// Convert the AssetImportData to a GroomAssetImportData
			GroomAssetImportData = NewObject<UGroomAssetImportData>(HairAsset);
			GroomAssetImportData->Update(CurrentFilename);
			HairAsset->AssetImportData = GroomAssetImportData;
		}

		if (!CurrentOptions)
		{
			// Make sure to have ImportOptions. Could happen if we just converted the AssetImportData
			CurrentOptions = NewObject<UGroomImportOptions>();
		}

		TSharedPtr<IHairStrandsTranslator> SelectedTranslator = GetTranslator(CurrentFilename);
		if (!SelectedTranslator.IsValid())
		{
			return EReimportResult::Failed;
		}

		// Load the alembic file upfront to preview & report any potential issue
		FProcessedHairDescription OutDescription;
		const bool bRunValidation = RunGroomAssetValidation();
		if (bRunValidation)
		{
			FScopedSlowTask Progress((float)1, LOCTEXT("ReimportHairAsset", "Reimporting hair asset for preview..."), true);
			Progress.MakeDialog(true);

			FHairDescription HairDescription;
			if (!SelectedTranslator->Translate(CurrentFilename, HairDescription, CurrentOptions->ConversionSettings))
			{
				return EReimportResult::Failed;
			}

			FGroomBuilder::ProcessHairDescription(HairDescription, CurrentOptions->BuildSettings, OutDescription);
		}

		if (!GIsRunningUnattendedScript && !IsAutomatedImport())
		{
			TSharedPtr<SGroomImportOptionsWindow> GroomOptionWindow = SGroomImportOptionsWindow::DisplayImportOptions(CurrentOptions, CurrentFilename, bRunValidation ? &OutDescription : nullptr);

			if (!GroomOptionWindow->ShouldImport())
			{
				return EReimportResult::Cancelled;
			}

			// Move the transient ImportOptions to the asset package and set it on the GroomAssetImportData for serialization
			CurrentOptions->Rename(nullptr, GroomAssetImportData);
			GroomAssetImportData->ImportOptions = CurrentOptions;
		}

		FHairDescription HairDescription;
		if (!SelectedTranslator->Translate(CurrentFilename, HairDescription, CurrentOptions->ConversionSettings))
		{
			return EReimportResult::Failed;
		}

		UGroomAsset* ReimportedHair = FHairStrandsImporter::ImportHair(FHairImportContext(CurrentOptions), HairDescription, HairAsset);
		if (!ReimportedHair)
		{
			return EReimportResult::Failed;
		}

		if (HairAsset->GetOuter())
		{
			HairAsset->GetOuter()->MarkPackageDirty();
		}
		else
		{
			HairAsset->MarkPackageDirty();
		}
	}

	return EReimportResult::Succeeded;
}

#undef LOCTEXT_NAMESPACE
