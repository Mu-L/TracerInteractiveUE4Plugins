// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "Sound/SoundSourceBus.h"
#include "AssetTypeActions/AssetTypeActions_SoundBase.h"

class FMenuBuilder;
class USoundSourceBus;

class FAssetTypeActions_SoundSourceBus : public FAssetTypeActions_SoundBase
{
public:
	//~ Begin IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundSourceBus", "Source Bus"); }
	virtual FColor GetTypeColor() const override { return FColor(212, 97, 85); }
	virtual const TArray<FText>& GetSubMenus() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual bool CanFilter() override { return true; }
	virtual bool IsImportedAsset() const override { return false; }
	virtual TSharedPtr<class SWidget> GetThumbnailOverlay(const FAssetData& AssetData) const override { return nullptr; }
	//~ End IAssetTypeActions Implementation
};
