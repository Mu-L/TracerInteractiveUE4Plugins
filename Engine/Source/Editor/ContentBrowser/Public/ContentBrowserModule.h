// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "Modules/ModuleInterface.h"
#include "ContentBrowserDelegates.h"

class IContentBrowserSingleton;
struct FARFilter;
class FMainMRUFavoritesList;

/**
 * Content browser module
 */
class FContentBrowserModule : public IModuleInterface
{

public:

	/**  */
	DECLARE_MULTICAST_DELEGATE_TwoParams( FOnFilterChanged, const FARFilter& /*NewFilter*/, bool /*bIsPrimaryBrowser*/ );
	/** */
	DECLARE_MULTICAST_DELEGATE_TwoParams( FOnSearchBoxChanged, const FText& /*InSearchText*/, bool /*bIsPrimaryBrowser*/ );
	/** */
	DECLARE_MULTICAST_DELEGATE_TwoParams( FOnAssetSelectionChanged, const TArray<FAssetData>& /*NewSelectedAssets*/, bool /*bIsPrimaryBrowser*/ );
	/** */
	DECLARE_MULTICAST_DELEGATE_OneParam( FOnSourcesViewChanged, bool /*bExpanded*/ );
	/** */
	DECLARE_MULTICAST_DELEGATE_OneParam( FOnAssetPathChanged, const FString& /*NewPath*/ );

	/**
	 * Called right after the plugin DLL has been loaded and the plugin object has been created
	 */
	virtual void StartupModule();

	/**
	 * Called before the plugin is unloaded, right before the plugin object is destroyed.
	 */
	virtual void ShutdownModule();

	/** Gets the content browser singleton */
	virtual IContentBrowserSingleton& Get() const;

	/** Delegates to be called to extend the content browser menus */
	virtual TArray<FContentBrowserMenuExtender_SelectedPaths>& GetAllAssetContextMenuExtenders() {return AssetContextMenuExtenders;}
	virtual TArray<FContentBrowserMenuExtender_SelectedPaths>& GetAllPathViewContextMenuExtenders() {return PathViewContextMenuExtenders;}
	virtual TArray<FContentBrowserMenuExtender>& GetAllCollectionListContextMenuExtenders() {return CollectionListContextMenuExtenders;}
	virtual TArray<FContentBrowserMenuExtender>& GetAllCollectionViewContextMenuExtenders() {return CollectionViewContextMenuExtenders;}
	virtual TArray<FContentBrowserMenuExtender_SelectedAssets>& GetAllAssetViewContextMenuExtenders() {return AssetViewContextMenuExtenders;}
	virtual TArray<FContentBrowserMenuExtender>& GetAllAssetViewViewMenuExtenders() {return AssetViewViewMenuExtenders;}

	/** Delegates to call to extend the command/keybinds for content browser */
	virtual TArray<FContentBrowserCommandExtender>& GetAllContentBrowserCommandExtenders() { return ContentBrowserCommandExtenders; }

	/** Delegates to be called to extend the drag-and-drop support of the asset view */
	virtual TArray<FAssetViewDragAndDropExtender>& GetAssetViewDragAndDropExtenders() { return AssetViewDragAndDropExtenders; }

	/** Delegate accessors */
	FOnFilterChanged& GetOnFilterChanged() { return OnFilterChanged; } 
	FOnSearchBoxChanged& GetOnSearchBoxChanged() { return OnSearchBoxChanged; } 
	FOnAssetSelectionChanged& GetOnAssetSelectionChanged() { return OnAssetSelectionChanged; } 
	FOnSourcesViewChanged& GetOnSourcesViewChanged() { return OnSourcesViewChanged; }
	FOnAssetPathChanged& GetOnAssetPathChanged() { return OnAssetPathChanged; }

	FMainMRUFavoritesList* GetRecentlyOpenedAssets() const
	{
		return RecentlyOpenedAssets.Get();
	};

	static const FName NumberOfRecentAssetsName;

private:
	/** Resize the recently opened asset list */
	void ResizeRecentAssetList(FName InName);

private:
	IContentBrowserSingleton* ContentBrowserSingleton;
	TSharedPtr<class FContentBrowserSpawner> ContentBrowserSpawner;

	/** All extender delegates for the content browser menus */
	TArray<FContentBrowserMenuExtender_SelectedPaths> AssetContextMenuExtenders;
	TArray<FContentBrowserMenuExtender_SelectedPaths> PathViewContextMenuExtenders;
	TArray<FContentBrowserMenuExtender> CollectionListContextMenuExtenders;
	TArray<FContentBrowserMenuExtender> CollectionViewContextMenuExtenders;
	TArray<FContentBrowserMenuExtender_SelectedAssets> AssetViewContextMenuExtenders;
	TArray<FContentBrowserMenuExtender> AssetViewViewMenuExtenders;
	TArray<FContentBrowserCommandExtender> ContentBrowserCommandExtenders;

	/** All extender delegates for the drag-and-drop support of the asset view */
	TArray<FAssetViewDragAndDropExtender> AssetViewDragAndDropExtenders;

	TUniquePtr<FMainMRUFavoritesList> RecentlyOpenedAssets;

	FOnFilterChanged OnFilterChanged;
	FOnSearchBoxChanged OnSearchBoxChanged;
	FOnAssetSelectionChanged OnAssetSelectionChanged;
	FOnSourcesViewChanged OnSourcesViewChanged;
	FOnAssetPathChanged OnAssetPathChanged;
};
