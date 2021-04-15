// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsFactory.h"

#include "EditorFramework/AssetImportData.h"
#include "GroomAsset.h"
#include "GroomAssetImportData.h"
#include "GroomImportOptions.h"
#include "GroomBuilder.h"
#include "GroomImportOptionsWindow.h"
#include "HairDescription.h"
#include "HairStrandsEditor.h"
#include "HairStrandsImporter.h"
#include "HairStrandsTranslator.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "HairStrandsFactory"

UHairStrandsFactory::UHairStrandsFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UGroomAsset::StaticClass();
	bCreateNew = false;		// manual creation not allow
	bEditAfterNew = false;
	bEditorImport = true;	// only allow import

	// Slightly increased priority to allow its translators to check if they can translate the file
	ImportPriority += 1;

	// Lazy init the translators to let them register themselves before the CDO is used
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ImportOptions = NewObject<UGroomImportOptions>();

		InitTranslators();
	}
}

void UHairStrandsFactory::InitTranslators()
{
	Formats.Reset();

	Translators = FGroomEditor::Get().GetHairTranslators();
	for (TSharedPtr<IGroomTranslator> Translator : Translators)
	{
		Formats.Add(Translator->GetSupportedFormat());
	}
}

void UHairStrandsFactory::GetSupportedFileExtensions(TArray<FString>& OutExtensions) const
{
	if (HasAnyFlags(RF_ClassDefaultObject) && Formats.Num() == 0)
	{
		// Init the translators the first time the CDO is used
		UHairStrandsFactory* Factory = const_cast<UHairStrandsFactory*>(this);
		Factory->InitTranslators();
	}

	Super::GetSupportedFileExtensions(OutExtensions);
}

UObject* UHairStrandsFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, 
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) 
{
	bOutOperationCanceled = false;

	// Translate the hair data from the file
	TSharedPtr<IGroomTranslator> SelectedTranslator = GetTranslator(Filename);
	if (!SelectedTranslator.IsValid())
	{
		return nullptr;
	}

	{
		// Load the alembic file upfront to preview & report any potential issue
		FProcessedHairDescription OutDescription;
		{
			FScopedSlowTask Progress((float)1, LOCTEXT("ImportHairAssetForPreview", "Importing hair asset for preview..."), true);
			Progress.MakeDialog(true);

			FHairDescription HairDescription;
			if (!SelectedTranslator->Translate(Filename, HairDescription, ImportOptions->ConversionSettings))
			{
				return nullptr;
			}

			FGroomBuilder::ProcessHairDescription(HairDescription, OutDescription);
		
			// Populate the interpolation settings based on the group count, as this is used later during the ImportHair() to define 
			// the exact number of group to create
			const uint32 GroupCount = OutDescription.HairGroups.Num();
			if (GroupCount != uint32(ImportOptions->InterpolationSettings.Num()))
			{
				ImportOptions->InterpolationSettings.Init(FHairGroupsInterpolation(), GroupCount);
			}
		}

		// Convert the process hair description into hair groups
		UGroomHairGroupsPreview* GroupsPreview = NewObject<UGroomHairGroupsPreview>();
		{
			uint32 GroupIndex = 0;
			for (TPair<int32, FProcessedHairDescription::FHairGroup> HairGroupIt : OutDescription.HairGroups)
			{
				const FProcessedHairDescription::FHairGroup& Group = HairGroupIt.Value;
				const FHairGroupInfo& GroupInfo = Group.Key;

				FGroomHairGroupPreview& OutGroup = GroupsPreview->Groups.AddDefaulted_GetRef();
				OutGroup.GroupID = GroupInfo.GroupID;
				OutGroup.CurveCount = GroupInfo.NumCurves;
				OutGroup.GuideCount = GroupInfo.NumGuides;

				if (OutGroup.GroupID < OutDescription.HairGroups.Num())
				{				
					OutGroup.InterpolationSettings = ImportOptions->InterpolationSettings[OutGroup.GroupID];
				}
			}
		}

		if (!GIsRunningUnattendedScript && !IsAutomatedImport())
		{
			// Display import options and handle user cancellation
			TSharedPtr<SGroomImportOptionsWindow> GroomOptionWindow = SGroomImportOptionsWindow::DisplayImportOptions(ImportOptions, GroupsPreview, Filename);
			if (!GroomOptionWindow->ShouldImport())
			{
				bOutOperationCanceled = true;
				return nullptr;
			}
		}

		// Save the options as the new default
		for (const FGroomHairGroupPreview& GroupPreview : GroupsPreview->Groups)
		{
			if (GroupPreview.GroupID < OutDescription.HairGroups.Num())
			{
				ImportOptions->InterpolationSettings[GroupPreview.GroupID] = GroupPreview.InterpolationSettings;
			}
		}
		ImportOptions->SaveConfig();
	}

	FScopedSlowTask Progress((float) 1, LOCTEXT("ImportHairAsset", "Importing hair asset..."), true);
	Progress.MakeDialog(true);

	FHairDescription HairDescription;
	if (!SelectedTranslator->Translate(Filename, HairDescription, ImportOptions->ConversionSettings))
	{
		return nullptr;
	}

	// Might try to import the same file in the same folder, so if an asset already exists there, reuse and update it
	// Since we are importing (not reimporting) we reset the object completely. All previous settings will be lost.
	UGroomAsset* ExistingAsset = FindObject<UGroomAsset>(InParent, *InName.ToString());
	if (ExistingAsset)
	{
		ExistingAsset->SetNumGroup(0);
	}

	FHairImportContext HairImportContext(ImportOptions, InParent, InClass, InName, Flags);
	UGroomAsset* CurrentAsset = FHairStrandsImporter::ImportHair(HairImportContext, HairDescription, ExistingAsset);
	if (CurrentAsset)
	{
		// Setup asset import data
		if (!CurrentAsset->AssetImportData || !CurrentAsset->AssetImportData->IsA<UGroomAssetImportData>())
		{
			CurrentAsset->AssetImportData = NewObject<UGroomAssetImportData>(CurrentAsset);
		}
		CurrentAsset->AssetImportData->Update(Filename);

		UGroomAssetImportData* GroomAssetImportData = Cast<UGroomAssetImportData>(CurrentAsset->AssetImportData);
		GroomAssetImportData->ImportOptions = DuplicateObject<UGroomImportOptions>(ImportOptions, GroomAssetImportData);
	}

	return CurrentAsset;
}

bool UHairStrandsFactory::FactoryCanImport(const FString& Filename)
{
	for (TSharedPtr<IGroomTranslator> Translator : Translators)
	{
		if (Translator->CanTranslate(Filename))
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<IGroomTranslator> UHairStrandsFactory::GetTranslator(const FString& Filename)
{
	FString Extension = FPaths::GetExtension(Filename);
	for (TSharedPtr<IGroomTranslator> Translator : Translators)
	{
		if (Translator->IsFileExtensionSupported(Extension))
		{
			return Translator;
		}
	}
	return {};
}

#undef LOCTEXT_NAMESPACE
