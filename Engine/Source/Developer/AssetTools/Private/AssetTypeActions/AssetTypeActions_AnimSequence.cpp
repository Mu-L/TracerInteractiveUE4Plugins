// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_AnimSequence.h"
#include "Animation/AnimSequence.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "EditorReimportHandler.h"
#include "Animation/AnimMontage.h"
#include "Factories/AnimCompositeFactory.h"
#include "Factories/AnimStreamableFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/PoseAssetFactory.h"
#include "EditorFramework/AssetImportData.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimStreamable.h"
#include "Animation/PoseAsset.h"
#include "AssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "IAnimationModifiersModule.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_AnimSequence::GetSupportedClass() const
{ 
	return UAnimSequence::StaticClass(); 
}

void FAssetTypeActions_AnimSequence::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	auto Sequences = GetTypedWeakObjectPtrs<UAnimSequence>(InObjects);

	// create menu
	Section.AddSubMenu(
		"CreateAnimSubmenu",
		LOCTEXT("CreateAnimSubmenu", "Create"),
		LOCTEXT("CreateAnimSubmenu_ToolTip", "Create assets from this anim sequence"),
		FNewMenuDelegate::CreateSP(this, &FAssetTypeActions_AnimSequence::FillCreateMenu, Sequences),
		false,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Persona.AssetActions.CreateAnimAsset")
		);

	Section.AddMenuEntry(
		"AnimSequence_ReimportWithNewSource",
		LOCTEXT("AnimSequence_ReimportWithNewSource", "Reimport with New Source"),
		LOCTEXT("AnimSequence_ReimportWithNewSourceTooltip", "Reimport the selected sequence(s) from a new source file."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Persona.AssetActions.ReimportAnim"),
		FUIAction(FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteReimportWithNewSource, Sequences))
		);

	Section.AddMenuEntry(
		"AnimSequence_AddAnimationModifier",
		LOCTEXT("AnimSequence_AddAnimationModifier", "Add Animation Modifier(s)"),
		LOCTEXT("AnimSequence_AddAnimationModifierTooltip", "Apply new animation modifier(s)."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.AnimationModifier"),
		FUIAction(FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteAddNewAnimationModifier, Sequences))
	);

	FAssetTypeActions_AnimationAsset::GetActions(InObjects, Section);
}

void FAssetTypeActions_AnimSequence::FillCreateMenu(FMenuBuilder& MenuBuilder, const TArray<TWeakObjectPtr<UAnimSequence>> Sequences) const
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AnimSequence_NewAnimComposite", "Create AnimComposite"),
		LOCTEXT("AnimSequence_NewAnimCompositeTooltip", "Creates an AnimComposite using the selected anim sequence."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.AnimComposite"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteNewAnimComposite, Sequences),
			FCanExecuteAction()
			)
		);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AnimSequence_NewAnimMontage", "Create AnimMontage"),
		LOCTEXT("AnimSequence_NewAnimMontageTooltip", "Creates an AnimMontage using the selected anim sequence."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.AnimMontage"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteNewAnimMontage, Sequences),
			FCanExecuteAction()
			)
		);

	// Not supported, streamable animation logic will be ported to UAnimSequence
	/*MenuBuilder.AddMenuEntry(
		LOCTEXT("AnimSequence_NewAnimStreamable", "Create AnimStreamable"),
		LOCTEXT("AnimSequence_NewAnimStreamableTooltip", "Creates an AnimStreamable using the selected anim sequence."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.AnimMontage"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteNewAnimStreamable, Sequences),
			FCanExecuteAction()
		)
	);*/

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AnimSequence_NewPoseAsset", "Create PoseAsset"),
		LOCTEXT("AnimSequence_NewPoseAssetTooltip", "Creates an PoseAsset using the selected anim sequence."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.PoseAsset"),
		FUIAction(
		FExecuteAction::CreateSP(this, &FAssetTypeActions_AnimSequence::ExecuteNewPoseAsset, Sequences),
		FCanExecuteAction()
		)
		);
}

void FAssetTypeActions_AnimSequence::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for (auto& Asset : TypeAssets)
	{
		const auto AnimSequence = CastChecked<UAnimSequence>(Asset);
		AnimSequence->AssetImportData->ExtractFilenames(OutSourceFilePaths);
	}
}

void FAssetTypeActions_AnimSequence::ExecuteReimportWithNewSource(TArray<TWeakObjectPtr<UAnimSequence>> Objects)
{
	FAssetImportInfo EmptyImportInfo;

	for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Object = (*ObjIt).Get();
		if (Object)
		{
			// Make note of the old import data
			FAssetImportInfo OldImportData = Object->AssetImportData->SourceData;
			// And reset the import data
			Object->AssetImportData->SourceData = EmptyImportInfo;

			bool bSuccess = FReimportManager::Instance()->Reimport(Object, /*bAskForNewFileIfMissing=*/true);

			// restore the old source path in case reimport was not successful
			if (!bSuccess)
			{
				Object->AssetImportData->SourceData = OldImportData;
			}
		}
	}
}

void FAssetTypeActions_AnimSequence::ExecuteNewAnimComposite(TArray<TWeakObjectPtr<UAnimSequence>> Objects) const
{
	const FString DefaultSuffix = TEXT("_Composite");
	UAnimCompositeFactory* Factory = NewObject<UAnimCompositeFactory>();

	CreateAnimationAssets(Objects, UAnimComposite::StaticClass(), Factory, DefaultSuffix, FOnConfigureFactory::CreateSP(this, &FAssetTypeActions_AnimSequence::ConfigureFactoryForAnimComposite));
}

void FAssetTypeActions_AnimSequence::ExecuteNewAnimMontage(TArray<TWeakObjectPtr<UAnimSequence>> Objects) const
{
	const FString DefaultSuffix = TEXT("_Montage");
	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();

	CreateAnimationAssets(Objects, UAnimMontage::StaticClass(), Factory, DefaultSuffix, FOnConfigureFactory::CreateSP(this, &FAssetTypeActions_AnimSequence::ConfigureFactoryForAnimMontage));
}

void FAssetTypeActions_AnimSequence::ExecuteNewAnimStreamable(TArray<TWeakObjectPtr<UAnimSequence>> Objects) const
{
	const FString DefaultSuffix = TEXT("_Streamable");
	UAnimStreamableFactory* Factory = NewObject<UAnimStreamableFactory>();

	auto StreamableConfigure = [](UFactory* AssetFactory, UAnimSequence* SourceAnimation) -> bool
	{
		UAnimStreamableFactory* StreamableAnimFactory = CastChecked<UAnimStreamableFactory>(AssetFactory);
		StreamableAnimFactory->SourceAnimation = SourceAnimation;
		return true;
	};

	CreateAnimationAssets(Objects, UAnimStreamable::StaticClass(), Factory, DefaultSuffix, FOnConfigureFactory::CreateLambda(StreamableConfigure));
}

void FAssetTypeActions_AnimSequence::ExecuteNewPoseAsset(TArray<TWeakObjectPtr<UAnimSequence>> Objects) const
{
	const FString DefaultSuffix = TEXT("_PoseAsset");
	UPoseAssetFactory* Factory = NewObject<UPoseAssetFactory>();
	CreateAnimationAssets(Objects, UPoseAsset::StaticClass(), Factory, DefaultSuffix, FOnConfigureFactory::CreateSP(this, &FAssetTypeActions_AnimSequence::ConfigureFactoryForPoseAsset));
}

void FAssetTypeActions_AnimSequence::ExecuteAddNewAnimationModifier(TArray<TWeakObjectPtr<UAnimSequence>> Objects)
{
	TArray<UAnimSequence*> AnimSequences;

	Algo::TransformIf(Objects, AnimSequences, 
	[](const TWeakObjectPtr<UAnimSequence>& WeakAnimSequence)
	{
		return WeakAnimSequence.Get() && WeakAnimSequence->IsA<UAnimSequence>();
	},
	[](const TWeakObjectPtr<UAnimSequence>& WeakAnimSequence)
	{
		return WeakAnimSequence.Get();
	});

	if (IAnimationModifiersModule* Module = FModuleManager::Get().LoadModulePtr<IAnimationModifiersModule>("AnimationModifiers"))
	{
		Module->ShowAddAnimationModifierWindow(AnimSequences);
	}
}

bool FAssetTypeActions_AnimSequence::ConfigureFactoryForAnimComposite(UFactory* AssetFactory, UAnimSequence* SourceAnimation) const
{
	UAnimCompositeFactory* CompositeFactory = CastChecked<UAnimCompositeFactory>(AssetFactory);
	CompositeFactory->SourceAnimation = SourceAnimation;
	return true;
}

bool FAssetTypeActions_AnimSequence::ConfigureFactoryForAnimMontage(UFactory* AssetFactory, UAnimSequence* SourceAnimation) const
{
	UAnimMontageFactory* MontageFactory = CastChecked<UAnimMontageFactory>(AssetFactory);
	MontageFactory->SourceAnimation = SourceAnimation;
	return true;
}

bool FAssetTypeActions_AnimSequence::ConfigureFactoryForPoseAsset(UFactory* AssetFactory, UAnimSequence* SourceAnimation) const
{
	UPoseAssetFactory* CompositeFactory = CastChecked<UPoseAssetFactory>(AssetFactory);
	CompositeFactory->SourceAnimation = SourceAnimation;
	return CompositeFactory->ConfigureProperties();
}
void FAssetTypeActions_AnimSequence::CreateAnimationAssets(const TArray<TWeakObjectPtr<UAnimSequence>>& AnimSequences, TSubclassOf<UAnimationAsset> AssetClass, UFactory* AssetFactory, const FString& InSuffix, FOnConfigureFactory OnConfigureFactory) const
{
	if ( AnimSequences.Num() == 1 )
	{
		auto AnimSequence = AnimSequences[0].Get();

		if ( AnimSequence )
		{
			// Determine an appropriate name for inline-rename
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(AnimSequence->GetOutermost()->GetName(), InSuffix, PackageName, Name);

			if (OnConfigureFactory.IsBound())
			{
				if (OnConfigureFactory.Execute(AssetFactory, AnimSequence))
				{
					FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
					ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackageName), AssetClass, AssetFactory);
				}
			}
		}
	}
	else
	{
		TArray<UObject*> ObjectsToSync;
		for (auto SeqIt = AnimSequences.CreateConstIterator(); SeqIt; ++SeqIt)
		{
			UAnimSequence* AnimSequence = (*SeqIt).Get();
			if ( AnimSequence )
			{
				// Determine an appropriate name
				FString Name;
				FString PackageName;
				CreateUniqueAssetName(AnimSequence->GetOutermost()->GetName(), InSuffix, PackageName, Name);

				if (OnConfigureFactory.IsBound())
				{
					if (OnConfigureFactory.Execute(AssetFactory, AnimSequence))
					{
						// Create the asset, and assign it's skeleton
						FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
						UAnimationAsset* NewAsset = Cast<UAnimationAsset>(AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), AssetClass, AssetFactory));

						if (NewAsset)
						{
							NewAsset->MarkPackageDirty();
							ObjectsToSync.Add(NewAsset);
						}
					}
				}
			}
		}

		if ( ObjectsToSync.Num() > 0 )
		{
			FAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
		}
	}
}
#undef LOCTEXT_NAMESPACE
