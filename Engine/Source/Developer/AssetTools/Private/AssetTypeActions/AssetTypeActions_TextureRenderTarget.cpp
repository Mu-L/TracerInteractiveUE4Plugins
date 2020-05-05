// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_TextureRenderTarget.h"
#include "ToolMenus.h"
#include "Engine/Texture2D.h"
#include "EditorStyleSet.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_TextureRenderTarget::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Texture::GetActions(InObjects, Section);

	auto RenderTargets = GetTypedWeakObjectPtrs<UTextureRenderTarget>(InObjects);

	Section.AddMenuEntry(
		"TextureRenderTarget_CreateStatic",
		LOCTEXT("TextureRenderTarget_CreateStatic", "Create Static Texture"),
		LOCTEXT("TextureRenderTarget_CreateStaticTooltip", "Creates a static texture from the selected render targets."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Texture2D"),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetTypeActions_TextureRenderTarget::ExecuteCreateStatic, RenderTargets ),
			FCanExecuteAction()
			)
		);
}

void FAssetTypeActions_TextureRenderTarget::ExecuteCreateStatic(TArray<TWeakObjectPtr<UTextureRenderTarget>> Objects)
{
	for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Object = (*ObjIt).Get();
		if ( Object )
		{
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Object->GetOutermost()->GetName(), TEXT("_Tex"), PackageName, Name);

			UObject* NewObj = NULL;
			UTextureRenderTarget2D* TexRT = Cast<UTextureRenderTarget2D>(Object);
			UTextureRenderTargetCube* TexRTCube = Cast<UTextureRenderTargetCube>(Object);
			if( TexRTCube )
			{
				// create a static cube texture as well as its 6 faces
				NewObj = TexRTCube->ConstructTextureCube( CreatePackage(NULL,*PackageName), Name, Object->GetMaskedFlags() );
			}
			else if( TexRT )
			{
				// create a static 2d texture
				NewObj = TexRT->ConstructTexture2D( CreatePackage(NULL,*PackageName), Name, Object->GetMaskedFlags(), CTF_Default, NULL );
			}

			if( NewObj )
			{
				// package needs saving
				NewObj->MarkPackageDirty();

				// Notify the asset registry
				FAssetRegistryModule::AssetCreated(NewObj);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
