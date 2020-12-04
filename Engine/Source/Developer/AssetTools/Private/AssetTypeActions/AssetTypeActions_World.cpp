// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_World.h"
#include "Misc/PackageName.h"
#include "ThumbnailRendering/WorldThumbnailInfo.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_World::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor )
{
	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UWorld* World = Cast<UWorld>(*ObjIt);
		if (World != nullptr && ensureMsgf(World->GetTypedOuter<UPackage>(), TEXT("World(%s) is not in a package and cannot be opened"), *World->GetFullName()))
		{
			const FString FileToOpen = FPackageName::LongPackageNameToFilename(World->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
			const bool bLoadAsTemplate = false;
			const bool bShowProgress = true;
			FEditorFileUtils::LoadMap(FileToOpen, bLoadAsTemplate, bShowProgress);
			
			// We can only edit one world at a time... so just break after the first valid world to load
			break;
		}
	}
}

UThumbnailInfo* FAssetTypeActions_World::GetThumbnailInfo(UObject* Asset) const
{
	UWorld* World = CastChecked<UWorld>(Asset);
	UThumbnailInfo* ThumbnailInfo = World->ThumbnailInfo;
	if (ThumbnailInfo == NULL)
	{
		ThumbnailInfo = NewObject<UWorldThumbnailInfo>(World, NAME_None, RF_Transactional);
		World->ThumbnailInfo = ThumbnailInfo;
	}

	return ThumbnailInfo;
}

TArray<FAssetData> FAssetTypeActions_World::GetValidAssetsForPreviewOrEdit(TArrayView<const FAssetData> InAssetDatas, bool bIsPreview)
{
	TArray<FAssetData> AssetsToOpen;
	if (InAssetDatas.Num())
	{
		const FAssetData& AssetData = InAssetDatas[0];

		// If there are any unsaved changes to the current level, see if the user wants to save those first
		// If they do not wish to save, then we will bail out of opening this asset.
		constexpr bool bPromptUserToSave = true;
		constexpr bool bSaveMapPackages = true;
		constexpr bool bSaveContentPackages = true;
		if (FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages))
		{
			AssetsToOpen.Add(AssetData);
		}
	}

	return MoveTemp(AssetsToOpen);
}

#undef LOCTEXT_NAMESPACE
