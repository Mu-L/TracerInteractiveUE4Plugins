// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"

class USoundCueTemplate;

class FAssetTypeActions_SoundCueTemplate : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundCueTemplate", "Sound Cue Template"); }
	virtual FColor GetTypeColor() const override { return FColor::Red; }
	virtual UClass* GetSupportedClass() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual bool AssetsActivatedOverride(const TArray<UObject *>& InObjects, EAssetTypeActivationMethod::Type ActivationType) override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
	virtual bool CanFilter() override { return true; }

protected:
	/** Plays the specified soundcue template */
	void PlaySound(USoundBase* Sound) const;

	/** Stops any currently playing template */
	void StopSound() const;

	/** Converts the provided SoundCue Template to a fully-modifiable SoundCue */
	void ExecuteCopyToSoundCue(TArray<TWeakObjectPtr<USoundCueTemplate>> Objects);
};

class FAssetActionExtender_SoundCueTemplate
{
public:
	static void RegisterMenus();
	static void ExecuteCreateSoundCueTemplate(const struct FToolMenuContext& MenuContext);
};