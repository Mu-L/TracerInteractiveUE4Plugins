// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_NiagaraEmitter.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemToolkit.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FAssetTypeActions_NiagaraEmitter::FAssetTypeActions_NiagaraEmitter()
{
}

FAssetTypeActions_NiagaraEmitter::~FAssetTypeActions_NiagaraEmitter()
{
}

FColor FAssetTypeActions_NiagaraEmitter::GetTypeColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.Emitter").ToFColor(true);
}

void FAssetTypeActions_NiagaraEmitter::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Emitter = Cast<UNiagaraEmitter>(*ObjIt);
		if (Emitter != nullptr)
		{
			TSharedRef<FNiagaraSystemToolkit> SystemToolkit(new FNiagaraSystemToolkit());
			SystemToolkit->InitializeWithEmitter(Mode, EditWithinLevelEditor, *Emitter);
		}
	}
}

UClass* FAssetTypeActions_NiagaraEmitter::GetSupportedClass() const
{
	return UNiagaraEmitter::StaticClass();
}

void FAssetTypeActions_NiagaraEmitter::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	auto NiagaraEmitters = GetTypedWeakObjectPtrs<UNiagaraEmitter>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Emitter_NewNiagaraSystem", "Create Niagara System"),
		LOCTEXT("Emitter_NewNiagaraSystemTooltip", "Creates a niagara system using this emitter as a base."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.ParticleSystem"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_NiagaraEmitter::ExecuteNewNiagaraSystem, NiagaraEmitters)
		)
	);

	bool bInObjectsAreCompilable = true;
	for (UObject* InObject : InObjects)
	{
		if (FNiagaraEditorUtilities::IsCompilableAssetClass(InObject->GetClass()) == false)
		{
			bInObjectsAreCompilable = false;
		}
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MarkDependentCompilableAssetsDirtyLabel", "Mark dependent compilable assets dirty"),
		LOCTEXT("MarkDependentCompilableAssetsDirtyToolTip", "Finds all niagara assets which depend on this asset either directly or indirectly,\n and marks them dirty so they can be saved with the latest version."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::MarkDependentCompilableAssetsDirty, InObjects)));
}

void FAssetTypeActions_NiagaraEmitter::ExecuteNewNiagaraSystem(TArray<TWeakObjectPtr<UNiagaraEmitter>> Objects)
{
	const FString DefaultSuffix = TEXT("_System");

	FNiagaraSystemViewModelOptions SystemOptions;
	SystemOptions.bCanModifyEmittersFromTimeline = true;
	SystemOptions.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;

	TArray<UObject*> ObjectsToSync;
	for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Emitter = (*ObjIt).Get();
		if (Emitter)
		{
			// Determine an appropriate names
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Emitter->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

			// Create the factory used to generate the asset
			UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
			Factory->EmittersToAddToNewSystem.Add(Emitter);
			FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
			UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UNiagaraSystem::StaticClass(), Factory);
			
			UNiagaraSystem* System = Cast<UNiagaraSystem>(NewAsset);
			if (System != nullptr)
			{
				ObjectsToSync.Add(NewAsset);
			}
		}
	}

	if (ObjectsToSync.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
	}
}

#undef LOCTEXT_NAMESPACE
