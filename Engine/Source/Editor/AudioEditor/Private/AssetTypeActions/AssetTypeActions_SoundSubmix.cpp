// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundSubmix.h"
#include "Sound/SoundSubmix.h"
#include "AudioEditorModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundSubmix::GetSupportedClass() const
{
	return USoundSubmix::StaticClass();
}

void FAssetTypeActions_SoundSubmix::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Obj : InObjects)
	{
		if (USoundSubmix* SoundSubmix = Cast<USoundSubmix>(Obj))
		{
			IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>( "AudioEditor" );
			AudioEditorModule->CreateSoundSubmixEditor(Mode, EditWithinLevelEditor, SoundSubmix);
		}
	}
}

const TArray<FText>& FAssetTypeActions_SoundSubmix::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundMixSubMenu", "Mix"))
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE