// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsImporter.h"

#include "HairDescription.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"
#include "GroomImportOptions.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairImporter, Log, All);

FHairImportContext::FHairImportContext(UGroomImportOptions* InImportOptions, UObject* InParent, UClass* InClass, FName InName, EObjectFlags InFlags)
	: ImportOptions(InImportOptions)
	, Parent(InParent)
	, Class(InClass)
	, Name(InName)
	, Flags(InFlags)
{
}

UGroomAsset* FHairStrandsImporter::ImportHair(const FHairImportContext& ImportContext, FHairDescription& HairDescription, UGroomAsset* ExistingHair)
{
	UGroomAsset* HairAsset = nullptr;
	if (ExistingHair)
	{
		HairAsset = ExistingHair;

		ExistingHair->Reset();
	}

	if (!HairAsset)
	{
		HairAsset = NewObject<UGroomAsset>(ImportContext.Parent, ImportContext.Class, ImportContext.Name, ImportContext.Flags);

		if (!HairAsset)
		{
			UE_LOG(LogHairImporter, Warning, TEXT("Failed to import hair: Could not allocate memory to create asset."));
			return nullptr;
		}
	}

	HairAsset->CommitHairDescription(MoveTemp(HairDescription));
	bool bSucceeded = HairAsset->CacheDerivedData(&ImportContext.ImportOptions->BuildSettings);
	if (!bSucceeded)
	{
		// Purge the newly created asset that failed to import
		if (HairAsset != ExistingHair)
		{
			HairAsset->ClearFlags(RF_Standalone);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		return nullptr;
	}

	HairAsset->InitResource();

	return HairAsset;
}
