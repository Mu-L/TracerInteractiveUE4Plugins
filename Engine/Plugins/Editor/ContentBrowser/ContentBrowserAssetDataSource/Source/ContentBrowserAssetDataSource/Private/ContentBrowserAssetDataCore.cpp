// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserAssetDataCore.h"
#include "ContentBrowserDataSource.h"
#include "IAssetTools.h"
#include "IAssetRegistry.h"
#include "AssetViewUtils.h"
#include "AssetPropertyTagCache.h"
#include "ObjectTools.h"
#include "Misc/BlacklistNames.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManager.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "AssetFolderContextMenu.h"
#include "AssetFileContextMenu.h"

#define LOCTEXT_NAMESPACE "ContentBrowserAssetDataSource"

namespace ContentBrowserAssetData
{

bool IsTopLevelFolder(const FName InFolderPath)
{
	TStringBuilder<FName::StringBufferSize> FolderPathStr;
	InFolderPath.ToString(FolderPathStr);

	int32 SlashCount = 0;
	for (const TCHAR PathChar : FStringView(FolderPathStr))
	{
		if (PathChar == TEXT('/'))
		{
			if (++SlashCount > 1)
			{
				break;
			}
		}
	}

	return SlashCount == 1;
}

FContentBrowserItemData CreateAssetFolderItem(UContentBrowserDataSource* InOwnerDataSource, const FName InVirtualPath, const FName InFolderPath)
{
	static const FName GameRootPath = "/Game";
	static const FName EngineRootPath = "/Engine";

	const FString FolderItemName = FPackageName::GetShortName(InFolderPath);

	FText FolderDisplayNameOverride;
	if (InFolderPath == GameRootPath)
	{
		FolderDisplayNameOverride = LOCTEXT("GameFolderDisplayName", "Content");
	}
	else if (InFolderPath == EngineRootPath)
	{
		FolderDisplayNameOverride = LOCTEXT("EngineFolderDisplayName", "Engine Content");
	}
	else if (IsTopLevelFolder(InFolderPath))
	{
		FolderDisplayNameOverride = FText::Format(LOCTEXT("ContentFolderDisplayNameFmt", "{0} Content"), FText::AsCultureInvariant(FolderItemName));
	}

	return FContentBrowserItemData(InOwnerDataSource, EContentBrowserItemFlags::Type_Folder | EContentBrowserItemFlags::Category_Asset, InVirtualPath, *FolderItemName, MoveTemp(FolderDisplayNameOverride), MakeShared<FContentBrowserAssetFolderItemDataPayload>(InFolderPath));
}

FContentBrowserItemData CreateAssetFileItem(UContentBrowserDataSource* InOwnerDataSource, const FName InVirtualPath, const FAssetData& InAssetData)
{
	return FContentBrowserItemData(InOwnerDataSource, EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset, InVirtualPath, InAssetData.AssetName, FText(), MakeShared<FContentBrowserAssetFileItemDataPayload>(InAssetData));
}

TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> GetAssetFolderItemPayload(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem)
{
	if (InItem.GetOwnerDataSource() == InOwnerDataSource && InItem.IsFolder())
	{
		return StaticCastSharedPtr<const FContentBrowserAssetFolderItemDataPayload>(InItem.GetPayload());
	}
	return nullptr;
}

TSharedPtr<const FContentBrowserAssetFileItemDataPayload> GetAssetFileItemPayload(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem)
{
	if (InItem.GetOwnerDataSource() == InOwnerDataSource && InItem.IsFile())
	{
		return StaticCastSharedPtr<const FContentBrowserAssetFileItemDataPayload>(InItem.GetPayload());
	}
	return nullptr;
}

void EnumerateAssetFolderItemPayloads(const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, TFunctionRef<bool(const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>&)> InFolderPayloadCallback)
{
	for (const FContentBrowserItemData& Item : InItems)
	{
		if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, Item))
		{
			if (!InFolderPayloadCallback(FolderPayload.ToSharedRef()))
			{
				break;
			}
		}
	}
}

void EnumerateAssetFileItemPayloads(const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, TFunctionRef<bool(const TSharedRef<const FContentBrowserAssetFileItemDataPayload>&)> InAssetPayloadCallback)
{
	for (const FContentBrowserItemData& Item : InItems)
	{
		if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, Item))
		{
			if (!InAssetPayloadCallback(AssetPayload.ToSharedRef()))
			{
				break;
			}
		}
	}
}

void EnumerateAssetItemPayloads(const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, TFunctionRef<bool(const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>&)> InFolderPayloadCallback, TFunctionRef<bool(const TSharedRef<const FContentBrowserAssetFileItemDataPayload>&)> InAssetPayloadCallback)
{
	for (const FContentBrowserItemData& Item : InItems)
	{
		if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, Item))
		{
			if (!InFolderPayloadCallback(FolderPayload.ToSharedRef()))
			{
				break;
			}
		}

		if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, Item))
		{
			if (!InAssetPayloadCallback(AssetPayload.ToSharedRef()))
			{
				break;
			}
		}
	}
}

bool IsPrimaryAsset(const FAssetData& InAssetData)
{
	// Check for the asset being a redirector first, as currently only class 
	// redirectors emit non-primary assets from the Asset Registry
	return !InAssetData.IsRedirector() || InAssetData.IsUAsset();
}

void SetOptionalErrorMessage(FText* OutErrorMsg, FText InErrorMsg)
{
	if (OutErrorMsg)
	{
		*OutErrorMsg = MoveTemp(InErrorMsg);
	}
}

bool CanModifyPath(IAssetTools* InAssetTools, const FName InFolderPath, FText* OutErrorMsg)
{
	const TSharedRef<FBlacklistPaths>& WritableFolderFilter = InAssetTools->GetWritableFolderBlacklist();
	if (!WritableFolderFilter->PassesStartsWithFilter(InFolderPath))
	{
		SetOptionalErrorMessage(OutErrorMsg, FText::Format(LOCTEXT("Error_FolderIsLocked", "Folder '{0}' is Locked"), FText::FromName(InFolderPath)));
		return false;
	}
	return true;
}

bool CanModifyItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return CanModifyAssetFolderItem(InAssetTools, *FolderPayload, OutErrorMsg);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanModifyAssetFileItem(InAssetTools, *AssetPayload, OutErrorMsg);
	}

	return false;
}

bool CanModifyAssetFolderItem(IAssetTools* InAssetTools, const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, FText* OutErrorMsg)
{
	return CanModifyPath(InAssetTools, InFolderPayload.GetInternalPath(), OutErrorMsg);
}

bool CanModifyAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FText* OutErrorMsg)
{
	return CanModifyPath(InAssetTools, InAssetPayload.GetAssetData().PackageName, OutErrorMsg);
}

bool CanEditItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanEditAssetFileItem(InAssetTools, *AssetPayload, OutErrorMsg);
	}

	return false;
}

bool CanEditAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FText* OutErrorMsg)
{
	return CanModifyAssetFileItem(InAssetTools, InAssetPayload, OutErrorMsg);
}

bool CanPreviewItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanPreviewAssetFileItem(InAssetTools, *AssetPayload, OutErrorMsg);
	}

	return false;
}

bool CanPreviewAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FText* OutErrorMsg)
{
	return true;
}

bool EditOrPreviewAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads, const bool bIsPreview)
{
	if (InAssetPayloads.Num() == 0)
	{
		return false;
	}

	const EAssetTypeActivationMethod::Type ActivationMethod = bIsPreview ? EAssetTypeActivationMethod::Previewed : EAssetTypeActivationMethod::Opened;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

	TMap<TSharedPtr<IAssetTypeActions>, TArray<FAssetData>> TypeActionsToAssetData;

	const FText DefaultText = InAssetPayloads.Num() == 1
		? FText::Format(LOCTEXT("LoadingAssetName", "Loading {0}..."), FText::FromName(InAssetPayloads[0]->GetAssetData().AssetName))
		: FText::Format(LOCTEXT("LoadingXAssets", "Loading {0} {0}|plural(one=Asset,other=Assets)..."), InAssetPayloads.Num());

	FScopedSlowTask SlowTask(100, DefaultText);

	// Iterate over all activated assets to map them to AssetTypeActions.
	// This way individual asset type actions will get a batched list of assets to operate on
	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		const FAssetData& AssetData = AssetPayload->GetAssetData();
		TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetPayload->GetAssetTypeActions();
		TArray<FAssetData>& AssetList = TypeActionsToAssetData.FindOrAdd(AssetTypeActions);
		AssetList.AddUnique(AssetData);
	}

	// Now that we have created our map, load and activate all the lists of objects for each asset type action.
	const bool bHasOpenActivationMethod = (ActivationMethod == EAssetTypeActivationMethod::DoubleClicked || ActivationMethod == EAssetTypeActivationMethod::Opened);
	for (auto& TypeActionToObjectsPair : TypeActionsToAssetData)
	{
		SlowTask.EnterProgressFrame(25.0f / TypeActionsToAssetData.Num());

		const TSharedPtr<IAssetTypeActions> TypeActions = TypeActionToObjectsPair.Key;
		TArray<FAssetData>& AssetsToLoad = TypeActionToObjectsPair.Value;
		if (TypeActions.IsValid())
		{
			AssetsToLoad = TypeActions->GetValidAssetsForPreviewOrEdit(AssetsToLoad, bIsPreview);
		}

		TArray<UObject*> ObjList;
		ObjList.Reserve(AssetsToLoad.Num());
		
		for (const FAssetData& AssetData : AssetsToLoad)
		{
			if (!AssetData.IsAssetLoaded() && FEditorFileUtils::IsMapPackageAsset(AssetData.ObjectPath.ToString()))
			{
				SlowTask.MakeDialog();
			}

			SlowTask.EnterProgressFrame(75.0f / InAssetPayloads.Num(), FText::Format(LOCTEXT("LoadingAssetName", "Loading {0}..."), FText::FromName(AssetData.AssetName)));

			ObjList.Add(AssetData.GetAsset());
		}

		bool bOpenEditorForAssets = bHasOpenActivationMethod;
		if (TypeActions.IsValid())
		{
			bOpenEditorForAssets = !TypeActions->AssetsActivatedOverride(ObjList, ActivationMethod);
		}

		if (bOpenEditorForAssets)
		{
			AssetEditorSubsystem->OpenEditorForAssets(ObjList);
		}
	}

	return true;
}

bool EditOrPreviewItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, const bool bIsPreview)
{
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	EnumerateAssetFileItemPayloads(InOwnerDataSource, InItems, [InAssetTools, bIsPreview , &AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		if ((bIsPreview ? CanPreviewAssetFileItem(InAssetTools, *InAssetPayload, nullptr) : CanEditAssetFileItem(InAssetTools, *InAssetPayload, nullptr)))
		{
			AssetPayloads.Add(InAssetPayload);
		}
		return true;
	});

	return EditOrPreviewAssetFileItems(AssetPayloads, bIsPreview);
}

bool EditItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems)
{
	return EditOrPreviewItems(InAssetTools, InOwnerDataSource, InItems, /*bIsPreview*/false);
}

bool EditAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads)
{
	return EditOrPreviewAssetFileItems(InAssetPayloads, /*bIsPreview*/false);
}

bool PreviewItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems)
{
	return EditOrPreviewItems(InAssetTools, InOwnerDataSource, InItems, /*bIsPreview*/true);
}

bool PreviewAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads)
{
	return EditOrPreviewAssetFileItems(InAssetPayloads, /*bIsPreview*/true);
}

bool CanDuplicateItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanDuplicateAssetFileItem(InAssetTools, *AssetPayload, OutErrorMsg);
	}

	return false;
}

bool CanDuplicateAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FText* OutErrorMsg)
{
	if (InAssetPayload.GetAssetData().IsRedirector())
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotDuplicateRedirectors", "Cannot duplicate redirectors"));
		return false;
	}

	return true;
}

bool DuplicateItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, UObject*& OutSourceAsset, FAssetData& OutNewAsset)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		if (CanDuplicateAssetFileItem(InAssetTools, *AssetPayload, nullptr))
		{
			return DuplicateAssetFileItem(InAssetTools, *AssetPayload, OutSourceAsset, OutNewAsset);
		}
	}

	return false;
}

bool DuplicateAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, UObject*& OutSourceAsset, FAssetData& OutNewAsset)
{
	// We need to potentially load the asset in order to duplicate it
	if (UObject* Asset = InAssetPayload.LoadAsset())
	{
		// Find a unique default name for the duplicated asset
		FString DefaultAssetName;
		FString PackageNameToUse;
		InAssetTools->CreateUniqueAssetName(Asset->GetOutermost()->GetPathName(), FString(), PackageNameToUse, DefaultAssetName);

		OutSourceAsset = Asset;
		OutNewAsset = FAssetData(*PackageNameToUse, *FPackageName::GetLongPackagePath(PackageNameToUse), *DefaultAssetName, Asset->GetClass()->GetFName());
		return true;
	}

	return false;
}

bool DuplicateItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, TArray<FAssetData>& OutNewAssets)
{
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	EnumerateAssetFileItemPayloads(InOwnerDataSource, InItems, [InAssetTools, &AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		if (CanDuplicateAssetFileItem(InAssetTools, *InAssetPayload, nullptr))
		{
			AssetPayloads.Add(InAssetPayload);
		}
		return true;
	});

	return DuplicateAssetFileItems(AssetPayloads, OutNewAssets);
}

bool DuplicateAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads, TArray<FAssetData>& OutNewAssets)
{
	TArray<UObject*> ObjectsToDuplicate;

	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		// We need to potentially load the asset in order to duplicate it
		if (UObject* Asset = AssetPayload->LoadAsset())
		{
			ObjectsToDuplicate.Add(Asset);
		}
	}

	if (ObjectsToDuplicate.Num() > 0)
	{
		TArray<UObject*> NewObjects;
		ObjectTools::DuplicateObjects(ObjectsToDuplicate, FString(), FString(), /*bOpenDialog=*/false, &NewObjects);

		if (NewObjects.Num() > 0)
		{
			for (UObject* NewAsset : NewObjects)
			{
				OutNewAssets.Emplace(FAssetData(NewAsset));
			}

			return true;
		}
	}

	return false;
}

bool CanSaveItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanSaveAssetFileItem(InAssetTools, *AssetPayload, InSaveFlags, OutErrorMsg);
	}

	return false;
}

bool CanSaveAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, const EContentBrowserItemSaveFlags InSaveFlags, FText* OutErrorMsg)
{
	if (!CanModifyAssetFileItem(InAssetTools, InAssetPayload, OutErrorMsg))
	{
		return false;
	}

	if (EnumHasAnyFlags(InSaveFlags, EContentBrowserItemSaveFlags::SaveOnlyIfLoaded))
	{
		// Can't save a package that hasn't been loaded
		UPackage* Package = InAssetPayload.GetPackage(/*bTryRecacheIfNull*/true);
		if (!Package)
		{
			ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotSaveUnloadedAsset", "Cannot save unloaded asset"));
			return false;
		}
	}

	return true;
}

bool SaveItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, const EContentBrowserItemSaveFlags InSaveFlags)
{
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	EnumerateAssetFileItemPayloads(InOwnerDataSource, InItems, [InAssetTools, InSaveFlags, &AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		if (CanSaveAssetFileItem(InAssetTools, *InAssetPayload, InSaveFlags, nullptr))
		{
			AssetPayloads.Add(InAssetPayload);
		}
		return true;
	});

	return SaveAssetFileItems(AssetPayloads, InSaveFlags);
}

bool SaveAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads, const EContentBrowserItemSaveFlags InSaveFlags)
{
	TArray<UPackage*> PackagesToSave;

	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		if (UPackage* Package = EnumHasAnyFlags(InSaveFlags, EContentBrowserItemSaveFlags::SaveOnlyIfLoaded) ? AssetPayload->GetPackage() : AssetPayload->LoadPackage())
		{
			if (!EnumHasAnyFlags(InSaveFlags, EContentBrowserItemSaveFlags::SaveOnlyIfDirty) || Package->IsDirty())
			{
				PackagesToSave.Add(Package);
			}
		}
	}

	// TODO: Interactive vs non-interactive save?
	return PackagesToSave.Num() > 0
		&& FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty*/false, /*bPromptToSave*/false) != FEditorFileUtils::PR_Failure;
}

bool IsRunningPIE(FText* OutErrorMsg)
{
	if (GIsEditor)
	{
		if (FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext())
		{
			SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotDeleteAssetInPIE", "Assets cannot be deleted while in PIE"));
			return true;
		}
	}
	return false;
}

bool CanDeleteItem(IAssetTools* InAssetTools, IAssetRegistry* InAssetRegistry, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return CanDeleteAssetFolderItem(InAssetTools, InAssetRegistry, *FolderPayload, OutErrorMsg);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanDeleteAssetFileItem(InAssetTools, *AssetPayload, OutErrorMsg);
	}

	return false;
}

bool CanDeleteAssetFolderItem(IAssetTools* InAssetTools, IAssetRegistry* InAssetRegistry, const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, FText* OutErrorMsg)
{
	if (!CanModifyAssetFolderItem(InAssetTools, InFolderPayload, OutErrorMsg))
	{
		return false;
	}

	if (IsTopLevelFolder(InFolderPayload.GetInternalPath()))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotDeleteRootFolders", "Cannot delete root folders"));
		return false;
	}

	if (IsRunningPIE(OutErrorMsg))
	{
		return false;
	}

	// Also check that sub-folders aren't locked, as this will be a recursive operation
	bool bCanModifyAllSubPaths = true;
	InAssetRegistry->EnumerateSubPaths(InFolderPayload.GetInternalPath(), [InAssetTools, OutErrorMsg, &bCanModifyAllSubPaths](FName InSubPath)
	{
		bCanModifyAllSubPaths &= CanModifyPath(InAssetTools, InSubPath, OutErrorMsg);
		return bCanModifyAllSubPaths;
	}, true);
	return bCanModifyAllSubPaths;
}

bool CanDeleteAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FText* OutErrorMsg)
{
	if (!CanModifyAssetFileItem(InAssetTools, InAssetPayload, OutErrorMsg))
	{
		return false;
	}

	if (IsRunningPIE(OutErrorMsg))
	{
		return false;
	}

	if (InAssetPayload.GetAssetData().IsRedirector())
	{
		SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotDeleteRedirectors", "Cannot delete redirectors"));
		return false;
	}

	return true;
}

bool DeleteItems(IAssetTools* InAssetTools, IAssetRegistry* InAssetRegistry, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems)
{
	TArray<TSharedRef<const FContentBrowserAssetFolderItemDataPayload>, TInlineAllocator<16>> FolderPayloads;
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	auto ProcessAssetFolderItem = [InAssetTools, InAssetRegistry, &FolderPayloads](const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& InFolderPayload)
	{
		if (CanDeleteAssetFolderItem(InAssetTools, InAssetRegistry, *InFolderPayload, nullptr))
		{
			FolderPayloads.Add(InFolderPayload);
		}
		return true;
	};

	auto ProcessAssetFileItem = [InAssetTools, &AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		if (CanDeleteAssetFileItem(InAssetTools, *InAssetPayload, nullptr))
		{
			AssetPayloads.Add(InAssetPayload);
		}
		return true;
	};

	EnumerateAssetItemPayloads(InOwnerDataSource, InItems, ProcessAssetFolderItem, ProcessAssetFileItem);

	bool bDidDelete = false;

	if (FolderPayloads.Num() > 0)
	{
		bDidDelete |= DeleteAssetFolderItems(FolderPayloads);
	}

	if (AssetPayloads.Num() > 0)
	{
		bDidDelete |= DeleteAssetFileItems(AssetPayloads);
	}

	return bDidDelete;
}

bool DeleteAssetFolderItems(TArrayView<const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>> InFolderPayloads)
{
	TArray<FString> FoldersToDelete;

	for (const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& FolderPayload : InFolderPayloads)
	{
		FoldersToDelete.Add(FolderPayload->GetInternalPath().ToString());
	}

	return FoldersToDelete.Num() > 0
		&& AssetViewUtils::DeleteFolders(FoldersToDelete);
}

bool DeleteAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads)
{
	TArray<FAssetData> AssetsToDelete;

	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		AssetsToDelete.Add(AssetPayload->GetAssetData());
	}

	return AssetsToDelete.Num() > 0
		&& ObjectTools::DeleteAssets(AssetsToDelete) > 0;
}

bool CanRenameItem(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, const FString* InNewName, FText* OutErrorMsg)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return CanRenameAssetFolderItem(InAssetTools, *FolderPayload, InNewName, OutErrorMsg);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return CanRenameAssetFileItem(InAssetTools, *AssetPayload, InNewName, InItem.IsTemporary(), OutErrorMsg);
	}

	return false;
}

bool CanRenameAssetFolderItem(IAssetTools* InAssetTools, const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, const FString* InNewName, FText* OutErrorMsg)
{
	if (!CanModifyAssetFolderItem(InAssetTools, InFolderPayload, OutErrorMsg))
	{
		return false;
	}

	if (IsTopLevelFolder(InFolderPayload.GetInternalPath()))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotRenameRootFolders", "Cannot rename root folders"));
		return false;
	}

	if (InNewName)
	{
		const FString FolderPath = FPaths::GetPath(InFolderPayload.GetInternalPath().ToString());

		FText ValidationErrorMsg;
		if (!AssetViewUtils::IsValidFolderPathForCreate(FolderPath, *InNewName, ValidationErrorMsg))
		{
			ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, ValidationErrorMsg);
			return false;
		}
	}

	return true;
}

bool CanRenameAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, const FString* InNewName, const bool InIsTempoarary, FText* OutErrorMsg)
{
	if (!CanModifyAssetFileItem(InAssetTools, InAssetPayload, OutErrorMsg))
	{
		return false;
	}

	if (InAssetPayload.GetAssetData().IsRedirector())
	{
		SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotRenameRedirectors", "Cannot rename redirectors"));
		return false;
	}

	if (InAssetPayload.GetAssetData().PackageFlags & PKG_FilterEditorOnly)
	{
		SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_CannotRenameCookedPackages", "Cannot rename cooked packages"));
		return false;
	}

	if (InNewName && InNewName->Len() >= NAME_SIZE)
	{
		SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_AssetNameTooLarge", "This asset name is too long. Please choose a shorter name."));
		return false;
	}

	if (InNewName && (InIsTempoarary || InAssetPayload.GetAssetData().AssetName != FName(**InNewName))) // Deliberately ignore case here to allow case-only renames of existing assets
	{
		const FString PackageName = InAssetPayload.GetAssetData().PackagePath.ToString() / *InNewName;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, **InNewName);

		FText ValidationErrorMsg;
		if (!AssetViewUtils::IsValidObjectPathForCreate(ObjectPath, ValidationErrorMsg))
		{
			SetOptionalErrorMessage(OutErrorMsg, ValidationErrorMsg);
			return false;
		}
	}

	return true;
}

bool RenameItem(IAssetTools* InAssetTools, IAssetRegistry* InAssetRegistry, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, const FString& InNewName)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		if (CanRenameAssetFolderItem(InAssetTools, *FolderPayload, &InNewName, nullptr))
		{
			return RenameAssetFolderItem(InAssetRegistry, *FolderPayload, InNewName);
		}
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		if (CanRenameAssetFileItem(InAssetTools, *AssetPayload, &InNewName, InItem.IsTemporary(), nullptr))
		{
			return RenameAssetFileItem(InAssetTools, *AssetPayload, InNewName);
		}
	}

	return false;
}

bool RenameAssetFolderItem(IAssetRegistry* InAssetRegistry, const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, const FString& InNewName)
{
	const FString OldPath = InFolderPayload.GetInternalPath().ToString();
	const FString NewPath = FPaths::GetPath(OldPath) / InNewName;

	// Ensure the folder exists on disk
	FString NewPathOnDisk;
	if (FPackageName::TryConvertLongPackageNameToFilename(NewPath, NewPathOnDisk) && IFileManager::Get().MakeDirectory(*NewPathOnDisk, true))
	{
		if (InAssetRegistry->AddPath(NewPath) && AssetViewUtils::RenameFolder(NewPath, OldPath))
		{
			return true;
		}
	}

	return false;
}

bool RenameAssetFileItem(IAssetTools* InAssetTools, const FContentBrowserAssetFileItemDataPayload& InAssetPayload, const FString& InNewName)
{
	// We need to potentially load the asset in order to rename it
	if (UObject* Asset = InAssetPayload.LoadAsset())
	{
		const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());

		TArray<FAssetRenameData> AssetsAndNames;
		AssetsAndNames.Emplace(FAssetRenameData(Asset, PackagePath, InNewName));
		// Note: This also returns false for Pending results as the rename may yet fail or be canceled, so the change has to be detected later via the asset registry
		return InAssetTools->RenameAssetsWithDialog(AssetsAndNames) == EAssetRenameResult::Success;
	}

	return false;
}

bool CopyItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	// The destination path must be writable
	if (!CanModifyPath(InAssetTools, InDestPath, nullptr))
	{
		return false;
	}

	TArray<TSharedRef<const FContentBrowserAssetFolderItemDataPayload>, TInlineAllocator<16>> FolderPayloads;
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	auto ProcessAssetFolderItem = [&FolderPayloads](const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& InFolderPayload)
	{
		FolderPayloads.Add(InFolderPayload);
		return true;
	};

	auto ProcessAssetFileItem = [&AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		AssetPayloads.Add(InAssetPayload);
		return true;
	};

	EnumerateAssetItemPayloads(InOwnerDataSource, InItems, ProcessAssetFolderItem, ProcessAssetFileItem);

	bool bDidCopy = false;

	if (FolderPayloads.Num() > 0)
	{
		bDidCopy |= CopyAssetFolderItems(FolderPayloads, InDestPath);
	}

	if (AssetPayloads.Num() > 0)
	{
		bDidCopy |= CopyAssetFileItems(AssetPayloads, InDestPath);
	}

	return bDidCopy;
}

bool CopyAssetFolderItems(TArrayView<const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>> InFolderPayloads, const FName InDestPath)
{
	TArray<FString> FoldersToCopy;

	for (const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& FolderPayload : InFolderPayloads)
	{
		FoldersToCopy.Add(FolderPayload->GetInternalPath().ToString());
	}

	return FoldersToCopy.Num() > 0
		&& AssetViewUtils::CopyFolders(FoldersToCopy, InDestPath.ToString());
}

bool CopyAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads, const FName InDestPath)
{
	TArray<UObject*> AssetsToCopy;

	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		// We need to potentially load the asset in order to duplicate it
		if (UObject* Asset = AssetPayload->LoadAsset())
		{
			AssetsToCopy.Add(Asset);
		}
	}

	if (AssetsToCopy.Num() > 0)
	{
		TArray<UObject*> NewObjects;
		ObjectTools::DuplicateObjects(AssetsToCopy, FString(), InDestPath.ToString(), /*bOpenDialog=*/false, &NewObjects);

		return NewObjects.Num() > 0;
	}

	return false;
}

bool MoveItems(IAssetTools* InAssetTools, const UContentBrowserDataSource* InOwnerDataSource, TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	// The destination path must be writable
	if (!CanModifyPath(InAssetTools, InDestPath, nullptr))
	{
		return false;
	}

	TArray<TSharedRef<const FContentBrowserAssetFolderItemDataPayload>, TInlineAllocator<16>> FolderPayloads;
	TArray<TSharedRef<const FContentBrowserAssetFileItemDataPayload>, TInlineAllocator<16>> AssetPayloads;

	auto ProcessAssetFolderItem = [InAssetTools, &FolderPayloads](const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& InFolderPayload)
	{
		// Moving has to be able to delete the original item
		if (CanModifyAssetFolderItem(InAssetTools, *InFolderPayload, nullptr))
		{
			FolderPayloads.Add(InFolderPayload);
		}
		return true;
	};

	auto ProcessAssetFileItem = [InAssetTools, &AssetPayloads](const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& InAssetPayload)
	{
		// Moving has to be able to delete the original item
		if (CanModifyAssetFileItem(InAssetTools, *InAssetPayload, nullptr))
		{
			AssetPayloads.Add(InAssetPayload);
		}
		return true;
	};

	EnumerateAssetItemPayloads(InOwnerDataSource, InItems, ProcessAssetFolderItem, ProcessAssetFileItem);

	bool bDidMove = false;

	if (FolderPayloads.Num() > 0)
	{
		bDidMove |= MoveAssetFolderItems(FolderPayloads, InDestPath);
	}

	if (AssetPayloads.Num() > 0)
	{
		bDidMove |= MoveAssetFileItems(AssetPayloads, InDestPath);
	}

	return bDidMove;
}

bool MoveAssetFolderItems(TArrayView<const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>> InFolderPayloads, const FName InDestPath)
{
	TArray<FString> FoldersToMove;

	for (const TSharedRef<const FContentBrowserAssetFolderItemDataPayload>& FolderPayload : InFolderPayloads)
	{
		FoldersToMove.Add(FolderPayload->GetInternalPath().ToString());
	}

	return FoldersToMove.Num() > 0
		&& AssetViewUtils::MoveFolders(FoldersToMove, InDestPath.ToString());
}

bool MoveAssetFileItems(TArrayView<const TSharedRef<const FContentBrowserAssetFileItemDataPayload>> InAssetPayloads, const FName InDestPath)
{
	TArray<UObject*> AssetsToMove;

	for (const TSharedRef<const FContentBrowserAssetFileItemDataPayload>& AssetPayload : InAssetPayloads)
	{
		// We need to potentially load the asset in order to duplicate it
		if (UObject* Asset = AssetPayload->LoadAsset())
		{
			AssetsToMove.Add(Asset);
		}
	}

	if (AssetsToMove.Num() > 0)
	{
		AssetViewUtils::MoveAssets(AssetsToMove, InDestPath.ToString());
		return true;
	}

	return false;
}

bool IsItemDirty(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return IsAssetFileItemDirty(*AssetPayload);
	}

	return false;
}

bool IsAssetFileItemDirty(const FContentBrowserAssetFileItemDataPayload& InAssetPayload)
{
	UPackage* AssetPackage = InAssetPayload.GetPackage();
	return AssetPackage && AssetPackage->IsDirty();
}

bool UpdateItemThumbnail(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return UpdateAssetFileItemThumbnail(*AssetPayload, InThumbnail);
	}

	return false;
}

bool UpdateAssetFileItemThumbnail(const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FAssetThumbnail& InThumbnail)
{
	InAssetPayload.UpdateThumbnail(InThumbnail);
	return true;
}

bool AppendItemReference(IAssetRegistry* InAssetRegistry, const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FString& InOutStr)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return AppendAssetFolderItemReference(InAssetRegistry, *FolderPayload, InOutStr);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return AppendAssetFileItemReference(*AssetPayload, InOutStr);
	}

	return false;
}

void AppendAssetExportText(const FAssetData& AssetData, FString& InOutStr)
{
	if (InOutStr.IsEmpty())
	{
		AssetData.GetExportTextName(InOutStr);
	}
	else
	{
		if (InOutStr.Len() > 0)
		{
			InOutStr += LINE_TERMINATOR;
		}
		InOutStr += AssetData.GetExportTextName();
	}
}

bool AppendAssetFolderItemReference(IAssetRegistry* InAssetRegistry, const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, FString& InOutStr)
{
	// Folders gather the assets references from within them
	FARFilter AssetFilter;
	AssetFilter.PackagePaths.Add(InFolderPayload.GetInternalPath());
	InAssetRegistry->EnumerateAssets(AssetFilter, [&InOutStr](const FAssetData& AssetData)
	{
		if (ContentBrowserAssetData::IsPrimaryAsset(AssetData))
		{
			AppendAssetExportText(AssetData, InOutStr);
		}
		return true;
	});

	return true;
}

bool AppendAssetFileItemReference(const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FString& InOutStr)
{
	AppendAssetExportText(InAssetPayload.GetAssetData(), InOutStr);
	return true;
}

bool GetItemPhysicalPath(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return GetAssetFolderItemPhysicalPath(*FolderPayload, OutDiskPath);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return GetAssetFileItemPhysicalPath(*AssetPayload, OutDiskPath);
	}

	return false;
}

bool GetAssetFolderItemPhysicalPath(const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, FString& OutDiskPath)
{
	const FString& FolderFilename = InFolderPayload.GetFilename();
	if (!FolderFilename.IsEmpty())
	{
		OutDiskPath = FolderFilename;
		return true;
	}

	return false;
}

bool GetAssetFileItemPhysicalPath(const FContentBrowserAssetFileItemDataPayload& InAssetPayload, FString& OutDiskPath)
{
	const FString& AssetFilename = InAssetPayload.GetFilename();
	if (!AssetFilename.IsEmpty())
	{
		OutDiskPath = AssetFilename;
		return true;
	}

	return false;
}

void GetClassItemAttribute(const FAssetData& InAssetData, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	check(InAssetData.IsValid());

	OutAttributeValue.SetValue(InAssetData.AssetClass);

	if (InIncludeMetaData)
	{
		static const FText ClassDisplayName = LOCTEXT("AttributeDisplayName_Class", "Class");

		FContentBrowserItemDataAttributeMetaData AttributeMetaData;
		AttributeMetaData.AttributeType = UObject::FAssetRegistryTag::TT_Hidden;
		AttributeMetaData.DisplayName = ClassDisplayName;
		OutAttributeValue.SetMetaData(MoveTemp(AttributeMetaData));
	}
}

void GetGenericItemAttribute(const FName InTagKey, const FString& InTagValue, const FAssetPropertyTagCache::FClassPropertyTagCache& InClassPropertyTagCache, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	check(!InTagKey.IsNone());

	if (FTextStringHelper::IsComplexText(*InTagValue))
	{
		FText TmpText;
		if (FTextStringHelper::ReadFromBuffer(*InTagValue, TmpText))
		{
			OutAttributeValue.SetValue(TmpText);
		}
	}
	if (!OutAttributeValue.IsValid())
	{
		OutAttributeValue.SetValue(InTagValue);
	}

	if (InIncludeMetaData)
	{
		FContentBrowserItemDataAttributeMetaData AttributeMetaData;
		if (const FAssetPropertyTagCache::FPropertyTagCache* PropertyTagCache = InClassPropertyTagCache.GetCacheForTag(InTagKey))
		{
			AttributeMetaData.AttributeType = PropertyTagCache->TagType;
			AttributeMetaData.DisplayFlags = PropertyTagCache->DisplayFlags;
			AttributeMetaData.DisplayName = PropertyTagCache->DisplayName;
			AttributeMetaData.TooltipText = PropertyTagCache->TooltipText;
			AttributeMetaData.Suffix = PropertyTagCache->Suffix;
			AttributeMetaData.bIsImportant = !PropertyTagCache->ImportantValue.IsEmpty() && PropertyTagCache->ImportantValue == InTagValue;
		}
		else
		{
			AttributeMetaData.DisplayName = FText::AsCultureInvariant(FName::NameToDisplayString(InTagKey.ToString(), /*bIsBool*/false));
		}
		OutAttributeValue.SetMetaData(MoveTemp(AttributeMetaData));
	}
}

bool GetItemAttribute(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, InItem))
	{
		return GetAssetFolderItemAttribute(*FolderPayload, InIncludeMetaData, InAttributeKey, OutAttributeValue);
	}

	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return GetAssetFileItemAttribute(*AssetPayload, InIncludeMetaData, InAttributeKey, OutAttributeValue);
	}

	return false;
}

bool GetAssetFolderItemAttribute(const FContentBrowserAssetFolderItemDataPayload& InFolderPayload, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	// Hard-coded attribute keys
	{
		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsDeveloperContent)
		{
			const bool bIsDevelopersFolder = AssetViewUtils::IsDevelopersFolder(InFolderPayload.GetInternalPath().ToString());
			OutAttributeValue.SetValue(bIsDevelopersFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsLocalizedContent)
		{
			const bool bIsLocalizedFolder = FPackageName::IsLocalizedPackage(InFolderPayload.GetInternalPath().ToString());
			OutAttributeValue.SetValue(bIsLocalizedFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsEngineContent)
		{
			const bool bIsEngineFolder = AssetViewUtils::IsEngineFolder(InFolderPayload.GetInternalPath().ToString(), /*bIncludePlugins*/true);
			OutAttributeValue.SetValue(bIsEngineFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsProjectContent)
		{
			const bool bIsProjectFolder = AssetViewUtils::IsProjectFolder(InFolderPayload.GetInternalPath().ToString(), /*bIncludePlugins*/true);
			OutAttributeValue.SetValue(bIsProjectFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsPluginContent)
		{
			const bool bIsPluginFolder = AssetViewUtils::IsPluginFolder(InFolderPayload.GetInternalPath().ToString());
			OutAttributeValue.SetValue(bIsPluginFolder);
			return true;
		}
	}

	return false;
}

bool GetAssetFileItemAttribute(const FContentBrowserAssetFileItemDataPayload& InAssetPayload, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	// Hard-coded attribute keys
	{
		static const FName NAME_Type = "Type";

		if (InAttributeKey == ContentBrowserItemAttributes::ItemTypeName || InAttributeKey == NAME_Class || InAttributeKey == NAME_Type)
		{
			GetClassItemAttribute(InAssetPayload.GetAssetData(), InIncludeMetaData, OutAttributeValue);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemTypeDisplayName)
		{
			if (TSharedPtr<IAssetTypeActions> AssetTypeActions = InAssetPayload.GetAssetTypeActions())
			{
				OutAttributeValue.SetValue(AssetTypeActions->GetName());
				return true;
			}
			return false;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemDescription)
		{
			if (TSharedPtr<IAssetTypeActions> AssetTypeActions = InAssetPayload.GetAssetTypeActions())
			{
				const FText AssetDescription = AssetTypeActions->GetAssetDescription(InAssetPayload.GetAssetData());
				if (!AssetDescription.IsEmpty())
				{
					OutAttributeValue.SetValue(AssetDescription);
					return true;
				}
			}
			return false;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsDeveloperContent)
		{
			const bool bIsDevelopersFolder = AssetViewUtils::IsDevelopersFolder(InAssetPayload.GetAssetData().PackageName.ToString());
			OutAttributeValue.SetValue(bIsDevelopersFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsLocalizedContent)
		{
			const bool bIsLocalizedFolder = FPackageName::IsLocalizedPackage(InAssetPayload.GetAssetData().PackageName.ToString());
			OutAttributeValue.SetValue(bIsLocalizedFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsEngineContent)
		{
			const bool bIsEngineFolder = AssetViewUtils::IsEngineFolder(InAssetPayload.GetAssetData().PackageName.ToString(), /*bIncludePlugins*/true);
			OutAttributeValue.SetValue(bIsEngineFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsProjectContent)
		{
			const bool bIsProjectFolder = AssetViewUtils::IsProjectFolder(InAssetPayload.GetAssetData().PackageName.ToString(), /*bIncludePlugins*/true);
			OutAttributeValue.SetValue(bIsProjectFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemIsPluginContent)
		{
			const bool bIsPluginFolder = AssetViewUtils::IsPluginFolder(InAssetPayload.GetAssetData().PackageName.ToString());
			OutAttributeValue.SetValue(bIsPluginFolder);
			return true;
		}

		if (InAttributeKey == ContentBrowserItemAttributes::ItemColor)
		{
			if (TSharedPtr<IAssetTypeActions> AssetTypeActions = InAssetPayload.GetAssetTypeActions())
			{
				const FLinearColor AssetColor = AssetTypeActions->GetTypeColor();//.ReinterpretAsLinear();
				OutAttributeValue.SetValue(AssetColor.ToString());
				return true;
			}
			return false;
		}
	}

	// Generic attribute keys
	{
		const FAssetData& AssetData = InAssetPayload.GetAssetData();
		const FAssetPropertyTagCache::FClassPropertyTagCache& ClassPropertyTagCache = FAssetPropertyTagCache::Get().GetCacheForClass(AssetData.AssetClass);

		FName FoundAttributeKey = InAttributeKey;
		FAssetDataTagMapSharedView::FFindTagResult FoundValue = AssetData.TagsAndValues.FindTag(FoundAttributeKey);
		if (!FoundValue.IsSet())
		{
			// Check to see if the key we were given resolves as an alias
			FoundAttributeKey = ClassPropertyTagCache.GetTagNameFromAlias(FoundAttributeKey);
			if (!FoundAttributeKey.IsNone())
			{
				FoundValue = AssetData.TagsAndValues.FindTag(FoundAttributeKey);
			}
		}
		if (FoundValue.IsSet())
		{
			GetGenericItemAttribute(FoundAttributeKey, FoundValue.GetValue(), ClassPropertyTagCache, InIncludeMetaData, OutAttributeValue);
			return true;
		}
	}

	return false;
}

bool GetItemAttributes(const UContentBrowserDataSource* InOwnerDataSource, const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, InItem))
	{
		return GetAssetFileItemAttributes(*AssetPayload, InIncludeMetaData, OutAttributeValues);
	}

	return false;
}

bool GetAssetFileItemAttributes(const FContentBrowserAssetFileItemDataPayload& InAssetPayload, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues)
{
	// Hard-coded attribute keys
	{
		FContentBrowserItemDataAttributeValue& ClassAttributeValue = OutAttributeValues.Add(NAME_Class);
		GetClassItemAttribute(InAssetPayload.GetAssetData(), InIncludeMetaData, ClassAttributeValue);
	}

	// Generic attribute keys
	{
		const FAssetData& AssetData = InAssetPayload.GetAssetData();
		const FAssetPropertyTagCache::FClassPropertyTagCache& ClassPropertyTagCache = FAssetPropertyTagCache::Get().GetCacheForClass(AssetData.AssetClass);

		OutAttributeValues.Reserve(OutAttributeValues.Num() + AssetData.TagsAndValues.Num());
		for (const auto& TagAndValue : AssetData.TagsAndValues)
		{
			FContentBrowserItemDataAttributeValue& GenericAttributeValue = OutAttributeValues.Add(TagAndValue.Key);
			GetGenericItemAttribute(TagAndValue.Key, TagAndValue.Value, ClassPropertyTagCache, InIncludeMetaData, GenericAttributeValue);
		}
	}

	return true;
}

void PopulateAssetFolderContextMenu(const UContentBrowserDataSource* InOwnerDataSource, UToolMenu* InMenu, FAssetFolderContextMenu& InAssetFolderContextMenu)
{
	const UContentBrowserDataMenuContext_FolderMenu* ContextObject = InMenu->FindContext<UContentBrowserDataMenuContext_FolderMenu>();
	checkf(ContextObject, TEXT("Required context UContentBrowserDataMenuContext_FolderMenu was missing!"));

	// Extract the internal package paths that belong to this data source from the full list of selected items given in the context
	TArray<FString> SelectedPackagePaths;
	for (const FContentBrowserItem& SelectedItem : ContextObject->SelectedItems)
	{
		for (const FContentBrowserItemData& SelectedItemData : SelectedItem.GetInternalItems())
		{
			if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InOwnerDataSource, SelectedItemData))
			{
				SelectedPackagePaths.Add(FolderPayload->GetInternalPath().ToString());
			}
		}
	}

	InAssetFolderContextMenu.MakeContextMenu(
		InMenu,
		SelectedPackagePaths
		);
}

void PopulateAssetFileContextMenu(const UContentBrowserDataSource* InOwnerDataSource, UToolMenu* InMenu, FAssetFileContextMenu& InAssetFileContextMenu)
{
	const UContentBrowserDataMenuContext_FileMenu* ContextObject = InMenu->FindContext<UContentBrowserDataMenuContext_FileMenu>();
	checkf(ContextObject, TEXT("Required context UContentBrowserDataMenuContext_FileMenu was missing!"));

	// Extract the internal asset data that belong to this data source from the full list of selected items given in the context
	TArray<FAssetData> SelectedAssets;
	TMap<FAssetData, FContentBrowserItem> SelectedAssetsToItems;
	for (const FContentBrowserItem& SelectedItem : ContextObject->SelectedItems)
	{
		if (const FContentBrowserItemData* SelectedItemData = SelectedItem.GetPrimaryInternalItem())
		{
			if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InOwnerDataSource, *SelectedItemData))
			{
				SelectedAssets.Add(AssetPayload->GetAssetData());
				SelectedAssetsToItems.Add(AssetPayload->GetAssetData(), SelectedItem);
			}
		}
	}

	FAssetFileContextMenu::FOnShowAssetsInPathsView OnShowAssetsInPathsView = FAssetFileContextMenu::FOnShowAssetsInPathsView::CreateLambda([SelectedAssetsToItems, OnShowInPathsView = ContextObject->OnShowInPathsView](const TArray<FAssetData>& InAssetsToShow)
	{
		if (OnShowInPathsView.IsBound())
		{
			TArray<FContentBrowserItem> ItemsToShow;
			for (const FAssetData& AssetToShow : InAssetsToShow)
			{
				if (const FContentBrowserItem* ItemToShow = SelectedAssetsToItems.Find(AssetToShow))
				{
					ItemsToShow.Emplace(*ItemToShow);
				}
			}
			OnShowInPathsView.Execute(ItemsToShow);
		}
	});

	InAssetFileContextMenu.MakeContextMenu(
		InMenu,
		SelectedAssets,
		OnShowAssetsInPathsView
		);
}

}

#undef LOCTEXT_NAMESPACE
