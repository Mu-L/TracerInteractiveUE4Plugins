// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"
#include "AssetTypeCategories.h"
#include "Templates/SharedPointer.h"


class FMenuBuilder;
class UFactory;
class UToolMenu;

struct FFactoryItem
{
	UFactory* Factory;
	FText DisplayName;

	FFactoryItem(UFactory* InFactory, const FText& InDisplayName);
};

struct FCategorySubMenuItem
{
	FText Name;
	TArray<FFactoryItem> Factories;
	TMap<FString, TSharedPtr<FCategorySubMenuItem>> Children;

	void SortSubMenus(FCategorySubMenuItem* NextNode = nullptr);
};

class FNewAssetOrClassContextMenu
{
public:
	DECLARE_DELEGATE_TwoParams( FOnNewAssetRequested, const FString& /*SelectedPath*/, TWeakObjectPtr<UClass> /*FactoryClass*/ );
	DECLARE_DELEGATE_OneParam( FOnNewClassRequested, const FString& /*SelectedPath*/ );
	DECLARE_DELEGATE_OneParam( FOnNewFolderRequested, const FString& /*SelectedPath*/ );
	DECLARE_DELEGATE_OneParam( FOnImportAssetRequested, const FString& );
	DECLARE_DELEGATE( FOnGetContentRequested )

	/** Makes the context menu widget */
	static void MakeContextMenu(
		UToolMenu* Menu, 
		const TArray<FName>& InSelectedPaths, 
		const FOnNewAssetRequested& InOnNewAssetRequested, 
		const FOnNewClassRequested& InOnNewClassRequested, 
		const FOnNewFolderRequested& InOnNewFolderRequested, 
		const FOnImportAssetRequested& InOnImportAssetRequested, 
		const FOnGetContentRequested& InOnGetContentRequested
		);

	/** Makes the context menu widget */
	static void MakeContextMenu(
		UToolMenu* Menu, 
		const TArray<FString>& InSelectedPaths, 
		const FOnNewAssetRequested& InOnNewAssetRequested, 
		const FOnNewClassRequested& InOnNewClassRequested, 
		const FOnNewFolderRequested& InOnNewFolderRequested, 
		const FOnImportAssetRequested& InOnImportAssetRequested, 
		const FOnGetContentRequested& InOnGetContentRequested
		);

private:
	/** Handle creating a new asset from an asset category */
	static void CreateNewAssetMenuCategory(UToolMenu* Menu, FName SectionName, EAssetTypeCategories::Type AssetTypeCategory, FString InPath, FOnNewAssetRequested InOnNewAssetRequested, FCanExecuteAction InCanExecuteAction);

	static void CreateNewAssetMenus(UToolMenu* Menu, FName SectionName, TSharedPtr<FCategorySubMenuItem> SubMenuData, FString InPath, FOnNewAssetRequested InOnNewAssetRequested, FCanExecuteAction InCanExecuteAction);

	/** Handle when the "Import" button is clicked */
	static void ExecuteImportAsset(FOnImportAssetRequested InOnImportAssetRequested, FString InPath);

	/** Create a new asset using the specified factory at the specified path */
	static void ExecuteNewAsset(FString InPath, TWeakObjectPtr<UClass> FactoryClass, FOnNewAssetRequested InOnNewAssetRequested);

	/** Create a new class at the specified path */
	static void ExecuteNewClass(FString InPath, FOnNewClassRequested InOnNewClassRequested);

	/** Create a new folder at the specified path */
	static void ExecuteNewFolder(FString InPath, FOnNewFolderRequested InOnNewFolderRequested);

	/** Handle when the "Get Content" button is clicked */
	static void ExecuteGetContent( FOnGetContentRequested InOnGetContentRequested );
};
