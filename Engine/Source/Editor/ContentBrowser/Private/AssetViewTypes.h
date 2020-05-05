// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "UObject/GCObject.h"
#include "Misc/Paths.h"
#include "Editor/ContentBrowser/Private/ContentBrowserUtils.h"
#include "Misc/TextFilterUtils.h"

class UFactory;

namespace EAssetItemType
{
	enum Type
	{
		Normal,
		Folder,
		Creation,
		Duplication
	};
}

/** Base class for items displayed in the asset view */
struct FAssetViewItem
{
	FAssetViewItem()
		: bRenameWhenScrolledIntoview(false)
	{
	}

	virtual ~FAssetViewItem() {}

	/** Get the type of this asset item */
	virtual EAssetItemType::Type GetType() const = 0;

	/** Get whether this is a temporary item */
	virtual bool IsTemporaryItem() const = 0;

	/** Updates cached custom column data, does nothing by default */
	virtual void CacheCustomColumns(const TArray<FAssetViewCustomColumn>& CustomColumns, bool bUpdateSortData, bool bUpdateDisplayText, bool bUpdateExisting) {}

	/** Broadcasts whenever a rename is requested */
	FSimpleDelegate RenamedRequestEvent;

	/** Broadcasts whenever a rename is canceled */
	FSimpleDelegate RenameCanceledEvent;

	/** An event to fire when the asset data for this item changes */
	DECLARE_MULTICAST_DELEGATE( FOnAssetDataChanged );
	FOnAssetDataChanged OnAssetDataChanged;

	/** True if this item will enter inline renaming on the next scroll into view */
	bool bRenameWhenScrolledIntoview;
};

/** Item that represents an asset */
struct FAssetViewAsset : public FAssetViewItem
{
	/** The asset registry data associated with this item */
	FAssetData Data;

	/** Map of values for custom columns */
	TMap<FName, FString> CustomColumnData;

	/** Map of display text for custom columns */
	TMap<FName, FText> CustomColumnDisplayText;

	TCHAR FirstFewAssetNameCharacters[8];

	explicit FAssetViewAsset(const FAssetData& AssetData)
		: Data(AssetData)
	{
		SetFirstFewAssetNameCharacters();
	}

	void SetFirstFewAssetNameCharacters()
	{
		TextFilterUtils::FNameBufferWithNumber NameBuffer(Data.AssetName);
		if (NameBuffer.IsWide())
		{
			FCStringWide::Strncpy(FirstFewAssetNameCharacters, NameBuffer.GetWideNamePtr(), UE_ARRAY_COUNT(FirstFewAssetNameCharacters));
		}
		else
		{
			int32 NumChars = FMath::Min<int32>(FCStringAnsi::Strlen(NameBuffer.GetAnsiNamePtr()), UE_ARRAY_COUNT(FirstFewAssetNameCharacters) - 1);
			FPlatformString::Convert(FirstFewAssetNameCharacters, NumChars, NameBuffer.GetAnsiNamePtr(), NumChars);
			FirstFewAssetNameCharacters[NumChars] = 0;
		}
		FCStringWide::Strupr(FirstFewAssetNameCharacters);
	}

	void SetAssetData(const FAssetData& NewData)
	{
		Data = NewData;
		SetFirstFewAssetNameCharacters();

		OnAssetDataChanged.Broadcast();
	}

	bool GetTagValue(FName Tag, FString& OutString) const
	{
		const FString* FoundString = CustomColumnData.Find(Tag);

		if (FoundString)
		{
			OutString = *FoundString;
			return true;
		}

		return Data.GetTagValue(Tag, OutString);
	}

	// FAssetViewItem interface
	virtual EAssetItemType::Type GetType() const override
	{
		return EAssetItemType::Normal;
	}

	virtual bool IsTemporaryItem() const override
	{
		return false;
	}

	virtual void CacheCustomColumns(const TArray<FAssetViewCustomColumn>& CustomColumns, bool bUpdateSortData, bool bUpdateDisplayText, bool bUpdateExisting) override
	{
		for (const FAssetViewCustomColumn& Column : CustomColumns)
		{
			if (bUpdateSortData)
			{
				if (bUpdateExisting ? CustomColumnData.Contains(Column.ColumnName) : !CustomColumnData.Contains(Column.ColumnName))
				{
					CustomColumnData.Add(Column.ColumnName, Column.OnGetColumnData.Execute(Data, Column.ColumnName));
				}
			}

			if (bUpdateDisplayText)
			{
				if (bUpdateExisting ? CustomColumnDisplayText.Contains(Column.ColumnName) : !CustomColumnDisplayText.Contains(Column.ColumnName))
				{
					if (Column.OnGetColumnDisplayText.IsBound())
					{
						CustomColumnDisplayText.Add(Column.ColumnName, Column.OnGetColumnDisplayText.Execute(Data, Column.ColumnName));
					}
					else
					{
						CustomColumnDisplayText.Add(Column.ColumnName, FText::AsCultureInvariant(Column.OnGetColumnData.Execute(Data, Column.ColumnName)));
					}
				}
			}
		}
	}
};

/** Item that represents a folder */
struct FAssetViewFolder : public FAssetViewItem
{
	/** The folder this item represents */
	FString FolderPath;

	/** The folder this item represents, minus the preceding path */
	FText FolderName;

	/** Whether this is a developer folder */
	bool bDeveloperFolder;

	/** Whether this is a collection folder */
	bool bCollectionFolder;

	/** Whether this folder is a new folder */
	bool bNewFolder;

	FAssetViewFolder(const FString& InPath)
		: FolderPath(InPath)
		, bNewFolder(false)
	{
		FolderName = FText::FromString(FPaths::GetBaseFilename(FolderPath));
		bDeveloperFolder = ContentBrowserUtils::IsDevelopersFolder(FolderPath);
		bCollectionFolder = ContentBrowserUtils::IsCollectionPath(FolderPath);
	}

	/** Set the name of this folder (without path) */
	void SetFolderName(const FString& InName)
	{
		FolderPath = FPaths::GetPath(FolderPath) / InName;
		FolderName = FText::FromString(InName);
		OnAssetDataChanged.Broadcast();
	}

	// FAssetViewItem interface
	virtual EAssetItemType::Type GetType() const override
	{
		return EAssetItemType::Folder;
	}

	virtual bool IsTemporaryItem() const override
	{
		return false;
	}
};

/** Item that represents an asset that is being created */
struct FAssetViewCreation : public FAssetViewAsset, public FGCObject
{
	/** The class to use when creating the asset */
	UClass* AssetClass;

	/** The factory to use when creating the asset. */
	UFactory* Factory;

	FAssetViewCreation(const FAssetData& AssetData, UClass* InAssetClass, UFactory* InFactory)
		: FAssetViewAsset(AssetData)
		, AssetClass(InAssetClass)
		, Factory(InFactory)
	{}

	// FAssetViewItem interface
	virtual EAssetItemType::Type GetType() const override
	{
		return EAssetItemType::Creation;
	}

	virtual bool IsTemporaryItem() const override
	{
		return true;
	}

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(AssetClass);
		Collector.AddReferencedObject(Factory);
	}
};

/** Item that represents an asset that is being duplicated */
struct FAssetViewDuplication : public FAssetViewAsset
{
	/** The context to use when creating the asset. Used when initializing an asset with another related asset. */
	TWeakObjectPtr<UObject> SourceObject;

	FAssetViewDuplication(const FAssetData& AssetData, const TWeakObjectPtr<UObject>& InSourceObject = NULL)
		: FAssetViewAsset(AssetData)
		, SourceObject(InSourceObject)
	{}

	// FAssetViewItem interface
	virtual EAssetItemType::Type GetType() const override
	{
		return EAssetItemType::Duplication;
	}

	virtual bool IsTemporaryItem() const override
	{
		return true;
	}
};
