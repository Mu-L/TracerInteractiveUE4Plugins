// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewSceneCustomizations.h"
#include "Modules/ModuleManager.h"
#include "AssetData.h"
#include "IDetailPropertyRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PersonaPreviewSceneDescription.h"
#include "PersonaPreviewSceneController.h"
#include "PersonaPreviewSceneDefaultController.h"
#include "PersonaPreviewSceneRefPoseController.h"
#include "PersonaPreviewSceneAnimationController.h"
#include "Engine/PreviewMeshCollection.h"
#include "Factories/PreviewMeshCollectionFactory.h"
#include "IPropertyUtilities.h"
#include "Preferences/PersonaOptions.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Animation/AnimBlueprint.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SComboBox.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Algo/Sort.h"
#include "ScopedTransaction.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "PreviewSceneCustomizations"

// static list that contains available classes, so that we can only allow these classes
TArray<FName> FPreviewSceneDescriptionCustomization::AvailableClassNameList;

FPreviewSceneDescriptionCustomization::FPreviewSceneDescriptionCustomization(const FString& InSkeletonName, const TSharedRef<class IPersonaToolkit>& InPersonaToolkit)
	: SkeletonName(InSkeletonName)
	, PersonaToolkit(InPersonaToolkit)
	, PreviewScene(StaticCastSharedRef<FAnimationEditorPreviewScene>(InPersonaToolkit->GetPreviewScene()))
	, EditableSkeleton(InPersonaToolkit->GetEditableSkeleton())
{
	// setup custom factory up-front so we can control its lifetime
	FactoryToUse = NewObject<UPreviewMeshCollectionFactory>();
	FactoryToUse->AddToRoot();

	// only first time
	if (AvailableClassNameList.Num() == 0)
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			if (ClassIt->IsChildOf(UDataAsset::StaticClass()) && ClassIt->ImplementsInterface(UPreviewCollectionInterface::StaticClass()))
			{
				AvailableClassNameList.Add(ClassIt->GetFName());
			}
		}
	}
}

FPreviewSceneDescriptionCustomization::~FPreviewSceneDescriptionCustomization()
{
	if (FactoryToUse)
	{
		FactoryToUse->RemoveFromRoot();
		FactoryToUse = nullptr;
	}
}

void FPreviewSceneDescriptionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	MyDetailLayout = &DetailBuilder;
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	TSharedRef<IPropertyHandle> PreviewControllerProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, PreviewController));
	TSharedRef<IPropertyHandle> SkeletalMeshProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, PreviewMesh));

	AdditionalMeshesProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, AdditionalMeshes));

	TArray<UClass*> BuiltInPreviewControllers = { UPersonaPreviewSceneDefaultController::StaticClass(), UPersonaPreviewSceneRefPoseController::StaticClass(), UPersonaPreviewSceneAnimationController::StaticClass() };

	TArray<UClass*> DynamicPreviewControllers;
	
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* CurrentClass = *It;
		if (CurrentClass->IsChildOf(UPersonaPreviewSceneController::StaticClass()) &&
			!(CurrentClass->HasAnyClassFlags(CLASS_Abstract)) &&
			!BuiltInPreviewControllers.Contains(CurrentClass))
		{
			DynamicPreviewControllers.Add(CurrentClass);
		}
	}

	Algo::SortBy(DynamicPreviewControllers, [](UClass* Cls) { return Cls->GetName(); });

	ControllerItems.Reset();

	for (UClass* ControllerClass : BuiltInPreviewControllers)
	{
		ControllerItems.Add(MakeShared<FPersonaModeComboEntry>(ControllerClass));
	}
	for (UClass* ControllerClass : DynamicPreviewControllers)
	{
		ControllerItems.Add(MakeShared<FPersonaModeComboEntry>(ControllerClass));
	}

	PreviewControllerProperty->MarkHiddenByCustomization();

	IDetailCategoryBuilder& AnimCategory = DetailBuilder.EditCategory("Animation");
	AnimCategory.AddCustomRow(PreviewControllerProperty->GetPropertyDisplayName())
	.NameContent()
	[
		PreviewControllerProperty->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(200.0f)
	[
		SNew(SComboBox<TSharedPtr<FPersonaModeComboEntry>>)
		.OptionsSource(&ControllerItems)
		.OnGenerateWidget(this, &FPreviewSceneDescriptionCustomization::MakeControllerComboEntryWidget)
		.OnSelectionChanged(this, &FPreviewSceneDescriptionCustomization::OnComboSelectionChanged)
		[
			SNew(STextBlock)
			.Text(this, &FPreviewSceneDescriptionCustomization::GetCurrentPreviewControllerText)
		]
	];

	TSharedPtr<FAnimationEditorPreviewScene> PreviewScenePtr = PreviewScene.Pin();
	UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = PreviewScenePtr->GetPreviewSceneDescription();

	FSimpleDelegate PropertyChangedDelegate = FSimpleDelegate::CreateSP(this, &FPreviewSceneDescriptionCustomization::HandlePreviewControllerPropertyChanged);

	for (const FProperty* TestProperty : TFieldRange<FProperty>(PersonaPreviewSceneDescription->PreviewControllerInstance->GetClass()))
	{
		if (TestProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			const bool bAdvancedDisplay = TestProperty->HasAnyPropertyFlags(CPF_AdvancedDisplay);
			const EPropertyLocation::Type PropertyLocation = bAdvancedDisplay ? EPropertyLocation::Advanced : EPropertyLocation::Common;

			IDetailPropertyRow* NewRow = PersonaPreviewSceneDescription->PreviewControllerInstance->AddPreviewControllerPropertyToDetails(PersonaToolkit.Pin().ToSharedRef(), DetailBuilder, AnimCategory, TestProperty, PropertyLocation);
			if (NewRow)
			{
				NewRow->GetPropertyHandle()->SetOnPropertyValueChanged(PropertyChangedDelegate);
			}
		}
	}

	// if mesh editor, we hide preview mesh section and additional mesh section
	// sometimes additional meshes are interfering with preview mesh, it is not a great experience
	const bool bMeshEditor = PersonaToolkit.Pin()->GetContext() == USkeletalMesh::StaticClass()->GetFName();
	if (!bMeshEditor)
	{
		FText PreviewMeshName;
		if (PersonaToolkit.Pin()->GetContext() == UAnimationAsset::StaticClass()->GetFName())
		{
			PreviewMeshName = FText::Format(LOCTEXT("PreviewMeshAnimation", "{0}\n(Animation)"), SkeletalMeshProperty->GetPropertyDisplayName());
		}
		else if(PersonaToolkit.Pin()->GetContext() == UAnimBlueprint::StaticClass()->GetFName())
		{
			PreviewMeshName = FText::Format(LOCTEXT("PreviewMeshAnimBlueprint", "{0}\n(Animation Blueprint)"), SkeletalMeshProperty->GetPropertyDisplayName());
		}
		else if(PersonaToolkit.Pin()->GetContext() == UPhysicsAsset::StaticClass()->GetFName())
		{
			PreviewMeshName = FText::Format(LOCTEXT("PreviewMeshPhysicsAsset", "{0}\n(Physics Asset)"), SkeletalMeshProperty->GetPropertyDisplayName());
		}
		else if(PersonaToolkit.Pin()->GetContext() == USkeleton::StaticClass()->GetFName())
		{
			PreviewMeshName = FText::Format(LOCTEXT("PreviewMeshSkeleton", "{0}\n(Skeleton)"), SkeletalMeshProperty->GetPropertyDisplayName());
		}
		else
		{
			PreviewMeshName = SkeletalMeshProperty->GetPropertyDisplayName();
		}

		DetailBuilder.EditCategory("Mesh")
		.AddProperty(SkeletalMeshProperty)
		.CustomWidget()
		.NameContent()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SkeletalMeshProperty->CreatePropertyNameWidget(PreviewMeshName)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyToAsset", "Apply To Asset"))
				.ToolTipText(LOCTEXT("ApplyToAssetToolTip", "The preview mesh has changed, but it will not be able to be saved until it is applied to the asset. Click here to make the change to the preview mesh persistent."))
				.Visibility_Lambda([this]()
				{
					TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
					USkeletalMesh* SkeletalMesh = PinnedPersonaToolkit->GetPreviewMesh();
					return (SkeletalMesh != PinnedPersonaToolkit->GetPreviewScene()->GetPreviewMesh()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnClicked_Lambda([this]() 
				{
					TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
					PinnedPersonaToolkit->SetPreviewMesh(PinnedPersonaToolkit->GetPreviewScene()->GetPreviewMesh(), true);
					return FReply::Handled();
				})
			]
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		.MinDesiredWidth(250.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(USkeletalMesh::StaticClass())
			.PropertyHandle(SkeletalMeshProperty)
			.OnShouldFilterAsset(this, &FPreviewSceneDescriptionCustomization::HandleShouldFilterAsset, FName("Skeleton"), PersonaToolkit.Pin()->GetContext() == UPhysicsAsset::StaticClass()->GetFName())
			.OnObjectChanged(this, &FPreviewSceneDescriptionCustomization::HandleMeshChanged)
			.ThumbnailPool(DetailBuilder.GetThumbnailPool())
			.CustomResetToDefault(FResetToDefaultOverride::Create(
				FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> PropertyHandle) -> bool {
					if (PreviewScene.IsValid())
					{
						return PreviewScene.Pin()->GetPreviewMesh() != nullptr;
					}
					return false;
				}),
				FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> PropertyHandle) {
					if (PreviewScene.IsValid())
					{
						PreviewScene.Pin()->SetPreviewMesh(nullptr, false);
					}
				})
			))
		];

		// Customize animation blueprint preview
		TSharedRef<IPropertyHandle> PreviewAnimationBlueprintProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, PreviewAnimationBlueprint));
		TSharedRef<IPropertyHandle> ApplicationMethodProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, ApplicationMethod));
		TSharedRef<IPropertyHandle> LinkedAnimGraphTagProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UPersonaPreviewSceneDescription, LinkedAnimGraphTag));
		
		if (PersonaToolkit.Pin()->GetContext() == UAnimBlueprint::StaticClass()->GetFName())
		{
			DetailBuilder.EditCategory("Animation Blueprint")
			.AddProperty(PreviewAnimationBlueprintProperty)
			.CustomWidget()
			.NameContent()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					PreviewAnimationBlueprintProperty->CreatePropertyNameWidget()
				]
			]
			.ValueContent()
			.MaxDesiredWidth(250.0f)
			.MinDesiredWidth(250.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UAnimBlueprint::StaticClass())
				.PropertyHandle(PreviewAnimationBlueprintProperty)
				.OnShouldFilterAsset(this, &FPreviewSceneDescriptionCustomization::HandleShouldFilterAsset, FName("TargetSkeleton"), false)
				.OnObjectChanged(this, &FPreviewSceneDescriptionCustomization::HandlePreviewAnimBlueprintChanged)
				.ThumbnailPool(DetailBuilder.GetThumbnailPool())
			];

			ApplicationMethodProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]()
			{
				FScopedTransaction Transaction(LOCTEXT("SetAnimationBlueprintApplicationMethod", "Set Application Method"));

				TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
				TSharedRef<FAnimationEditorPreviewScene> LocalPreviewScene = StaticCastSharedRef<FAnimationEditorPreviewScene>(PinnedPersonaToolkit->GetPreviewScene());
				UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = LocalPreviewScene->GetPreviewSceneDescription();
				PinnedPersonaToolkit->GetAnimBlueprint()->SetPreviewAnimationBlueprintApplicationMethod(PersonaPreviewSceneDescription->ApplicationMethod);
				LocalPreviewScene->SetPreviewAnimationBlueprint(PersonaPreviewSceneDescription->PreviewAnimationBlueprint.Get(), PinnedPersonaToolkit->GetAnimBlueprint());
			}));

			DetailBuilder.EditCategory("Animation Blueprint")
			.AddProperty(ApplicationMethodProperty)
			.IsEnabled(MakeAttributeLambda([this]()
			{
				TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
				TSharedRef<FAnimationEditorPreviewScene> LocalPreviewScene = StaticCastSharedRef<FAnimationEditorPreviewScene>(PinnedPersonaToolkit->GetPreviewScene());
				UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = LocalPreviewScene->GetPreviewSceneDescription();
				
				return PersonaPreviewSceneDescription->PreviewAnimationBlueprint.IsValid();
			}));
		
			LinkedAnimGraphTagProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]()
			{
				FScopedTransaction Transaction(LOCTEXT("SetAnimationBlueprintTag", "Set Linked Anim Graph Tag"));

				TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
				TSharedRef<FAnimationEditorPreviewScene> LocalPreviewScene = StaticCastSharedRef<FAnimationEditorPreviewScene>(PinnedPersonaToolkit->GetPreviewScene());
				UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = LocalPreviewScene->GetPreviewSceneDescription();
				PinnedPersonaToolkit->GetAnimBlueprint()->SetPreviewAnimationBlueprintTag(PersonaPreviewSceneDescription->LinkedAnimGraphTag);
				LocalPreviewScene->SetPreviewAnimationBlueprint(PersonaPreviewSceneDescription->PreviewAnimationBlueprint.Get(), PinnedPersonaToolkit->GetAnimBlueprint());
			}));

			DetailBuilder.EditCategory("Animation Blueprint")
			.AddProperty(LinkedAnimGraphTagProperty)
			.IsEnabled(MakeAttributeLambda([this]()
			{
				TSharedPtr<IPersonaToolkit> PinnedPersonaToolkit = PersonaToolkit.Pin();
				TSharedRef<FAnimationEditorPreviewScene> LocalPreviewScene = StaticCastSharedRef<FAnimationEditorPreviewScene>(PinnedPersonaToolkit->GetPreviewScene());
				UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = LocalPreviewScene->GetPreviewSceneDescription();
				
				return PersonaPreviewSceneDescription->PreviewAnimationBlueprint.IsValid() && PersonaPreviewSceneDescription->ApplicationMethod == EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph;
			}));
		}
		else
		{
			PreviewAnimationBlueprintProperty->MarkHiddenByCustomization();
			ApplicationMethodProperty->MarkHiddenByCustomization();
			LinkedAnimGraphTagProperty->MarkHiddenByCustomization();
		}

#if CHAOS_SIMULATION_DETAIL_VIEW_FACTORY_SELECTOR
		// Physics settings
		ClothSimulationFactoryList.Reset();
		const TArray<IClothingSimulationFactoryClassProvider*> ClassProviders = IModularFeatures::Get().GetModularFeatureImplementations<IClothingSimulationFactoryClassProvider>(IClothingSimulationFactoryClassProvider::FeatureName);
		for (const auto& ClassProvider : ClassProviders)
		{
			// Populate cloth factory list
			ClothSimulationFactoryList.Add(MakeShared<TSubclassOf<class UClothingSimulationFactory>>(ClassProvider->GetClothingSimulationFactoryClass()));
		}

		DetailBuilder.EditCategory("Physics")
		.AddCustomRow(LOCTEXT("PhysicsClothingSimulationFactory", "Clothing Simulation Factory Option"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("PhysicsClothingSimulationFactory_Text", "Clothing Simulation Factory"))
			.ToolTipText(LOCTEXT("PhysicsClothingSimulationFactory_ToolTip", "Select the cloth simulation used to preview the scene."))
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		[
			SNew(SComboBox<TSharedPtr<TSubclassOf<class UClothingSimulationFactory>>>)
			.OptionsSource(&ClothSimulationFactoryList)
			.OnGenerateWidget(this, &FPreviewSceneDescriptionCustomization::MakeClothingSimulationFactoryWidget)
			.OnSelectionChanged(this, &FPreviewSceneDescriptionCustomization::OnClothingSimulationFactorySelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &FPreviewSceneDescriptionCustomization::GetCurrentClothingSimulationFactoryText)
			]
		];
#endif  // #if CHAOS_SIMULATION_DETAIL_VIEW_FACTORY_SELECTOR
		// set the skeleton to use in our factory as we shouldn't be picking one here
		FactoryToUse->CurrentSkeleton = EditableSkeleton.IsValid() ? MakeWeakObjectPtr(const_cast<USkeleton*>(&EditableSkeleton.Pin()->GetSkeleton())) : nullptr;
		TArray<UFactory*> FactoriesToUse({ FactoryToUse });

		FAssetData AdditionalMeshesAsset;
		AdditionalMeshesProperty->GetValue(AdditionalMeshesAsset);

		// bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons option
		DetailBuilder.EditCategory("Additional Meshes")
		.AddCustomRow(LOCTEXT("AdditionalMeshOption", "Additional Mesh Selection Option"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("AdditionalMeshSelectionFromDifferentSkeletons", "Allow Different Skeletons"))
			.ToolTipText(LOCTEXT("AdditionalMeshSelectionFromDifferentSkeletons_ToolTip", "When selecting additional mesh, whether or not filter by the current skeleton."))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FPreviewSceneDescriptionCustomization::HandleAllowDifferentSkeletonsIsChecked)
			.OnCheckStateChanged(this, &FPreviewSceneDescriptionCustomization::HandleAllowDifferentSkeletonsCheckedStateChanged)
		];
	
		// bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons option
		DetailBuilder.EditCategory("Additional Meshes")
		.AddCustomRow(LOCTEXT("AdditionalMeshOption_AnimBP", "Additional Mesh Anim Selection Option"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("UseCustomAnimBP", "Allow Custom AnimBP Override"))
			.ToolTipText(LOCTEXT("UseCustomAnimBP_ToolTip", "When using preview collection, allow it to override custom AnimBP also."))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FPreviewSceneDescriptionCustomization::HandleUseCustomAnimBPIsChecked)
			.OnCheckStateChanged(this, &FPreviewSceneDescriptionCustomization::HandleUseCustomAnimBPCheckedStateChanged)
		];

		FResetToDefaultOverride ResetToDefaultOverride = FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateSP(this, &FPreviewSceneDescriptionCustomization::GetReplaceVisibility),
			FResetToDefaultHandler::CreateSP(this, &FPreviewSceneDescriptionCustomization::OnResetToBaseClicked)
		);

		DetailBuilder.EditCategory("Additional Meshes")
		.AddProperty(AdditionalMeshesProperty)
		.CustomWidget()
		.NameContent()
		[
			AdditionalMeshesProperty->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		.MinDesiredWidth(250.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				// searching uobject is too much for a scale of Fortnite
				// for now we just allow UDataAsset
				.AllowedClass(UDataAsset::StaticClass())
				.PropertyHandle(AdditionalMeshesProperty)
				.OnShouldFilterAsset(this, &FPreviewSceneDescriptionCustomization::HandleShouldFilterAdditionalMesh, true)
				.OnObjectChanged(this, &FPreviewSceneDescriptionCustomization::HandleAdditionalMeshesChanged, &DetailBuilder)
				.CustomResetToDefault(ResetToDefaultOverride)
				.ThumbnailPool(DetailBuilder.GetThumbnailPool())
				.NewAssetFactories(FactoriesToUse)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Visibility(this, &FPreviewSceneDescriptionCustomization::GetSaveButtonVisibility, AdditionalMeshesProperty.ToSharedRef())
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &FPreviewSceneDescriptionCustomization::OnSaveCollectionClicked, AdditionalMeshesProperty.ToSharedRef(), &DetailBuilder)
				.ContentPadding(4.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Persona.SavePreviewMeshCollection"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];

		if (AdditionalMeshesAsset.IsValid())
		{
			TArray<UObject*> Objects;
			Objects.Add(AdditionalMeshesAsset.GetAsset());

			IDetailPropertyRow* PropertyRow = DetailBuilder.EditCategory("Additional Meshes")
			.AddExternalObjectProperty(Objects, "SkeletalMeshes");

			if (PropertyRow)
			{
				PropertyRow->ShouldAutoExpand(true);
			}
		}
	}
	else
	{
		DetailBuilder.HideProperty(SkeletalMeshProperty);
		DetailBuilder.HideProperty(AdditionalMeshesProperty);
	}
}

EVisibility FPreviewSceneDescriptionCustomization::GetSaveButtonVisibility(TSharedRef<IPropertyHandle> InAdditionalMeshesProperty) const
{
	FAssetData AdditionalMeshesAsset;
	InAdditionalMeshesProperty->GetValue(AdditionalMeshesAsset);
	UObject* Object = AdditionalMeshesAsset.GetAsset();

	return Object == nullptr || !Object->HasAnyFlags(RF_Transient) ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply FPreviewSceneDescriptionCustomization::OnSaveCollectionClicked(TSharedRef<IPropertyHandle> InAdditionalMeshesProperty, IDetailLayoutBuilder* DetailLayoutBuilder)
{
	FAssetData AdditionalMeshesAsset;
	InAdditionalMeshesProperty->GetValue(AdditionalMeshesAsset);
	UPreviewMeshCollection* DefaultPreviewMeshCollection = CastChecked<UPreviewMeshCollection>(AdditionalMeshesAsset.GetAsset());
	if (DefaultPreviewMeshCollection)
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UPreviewMeshCollection* NewPreviewMeshCollection = Cast<UPreviewMeshCollection>(AssetTools.CreateAssetWithDialog(UPreviewMeshCollection::StaticClass(), FactoryToUse));
		if (NewPreviewMeshCollection)
		{
			NewPreviewMeshCollection->Skeleton = DefaultPreviewMeshCollection->Skeleton;
			NewPreviewMeshCollection->SkeletalMeshes = DefaultPreviewMeshCollection->SkeletalMeshes;
			InAdditionalMeshesProperty->SetValue(FAssetData(NewPreviewMeshCollection));
			PreviewScene.Pin()->SetAdditionalMeshes(NewPreviewMeshCollection);

			DetailLayoutBuilder->ForceRefreshDetails();
		}
	}

	return FReply::Handled();
}

bool FPreviewSceneDescriptionCustomization::HandleShouldFilterAdditionalMesh(const FAssetData& InAssetData, bool bCanUseDifferentSkeleton)
{
	// see if it's in valid class set
	bool bValidClass = false;

	// first to see if it's allowed class
	for (FName& ClassName: AvailableClassNameList)
	{
		if (ClassName == InAssetData.AssetClass)
		{
			bValidClass = true;
			break;
		}
	}

	// not valid class, filter it
	if (!bValidClass)
	{
		return true;
	}

	return HandleShouldFilterAsset(InAssetData, FName("Skeleton"), bCanUseDifferentSkeleton);
}

bool FPreviewSceneDescriptionCustomization::HandleShouldFilterAsset(const FAssetData& InAssetData, FName InTag, bool bCanUseDifferentSkeleton)
{
	if (bCanUseDifferentSkeleton && GetDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons)
	{
		return false;
	}

	FString SkeletonTag = InAssetData.GetTagValueRef<FString>(InTag);
	if (SkeletonName.IsEmpty() || SkeletonTag == SkeletonName)
	{
		return false;
	}

	return true;
}

FText FPreviewSceneDescriptionCustomization::GetCurrentPreviewControllerText() const
{
	UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = PreviewScene.Pin()->GetPreviewSceneDescription();
	return PersonaPreviewSceneDescription->PreviewController->GetDisplayNameText();
}

TSharedRef<SWidget> FPreviewSceneDescriptionCustomization::MakeControllerComboEntryWidget(TSharedPtr<FPersonaModeComboEntry> InItem) const
{
	return
		SNew(STextBlock)
		.Text(InItem->Text);
}

void FPreviewSceneDescriptionCustomization::OnComboSelectionChanged(TSharedPtr<FPersonaModeComboEntry> InSelectedItem, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FAnimationEditorPreviewScene> PreviewScenePtr = PreviewScene.Pin();
	UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = PreviewScenePtr->GetPreviewSceneDescription();

	PersonaPreviewSceneDescription->SetPreviewController(InSelectedItem->Class, PreviewScenePtr.Get());

	MyDetailLayout->ForceRefreshDetails();
}

void FPreviewSceneDescriptionCustomization::HandlePreviewControllerPropertyChanged()
{
	TSharedPtr<FAnimationEditorPreviewScene> PreviewScenePtr = PreviewScene.Pin();
	UPersonaPreviewSceneDescription* PersonaPreviewSceneDescription = PreviewScenePtr->GetPreviewSceneDescription();
	
	PersonaPreviewSceneDescription->PreviewControllerInstance->UninitializeView(PersonaPreviewSceneDescription, PreviewScenePtr.Get());
	PersonaPreviewSceneDescription->PreviewControllerInstance->InitializeView(PersonaPreviewSceneDescription, PreviewScenePtr.Get());
}

void FPreviewSceneDescriptionCustomization::HandleMeshChanged(const FAssetData& InAssetData)   
{
	USkeletalMesh* NewPreviewMesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	PersonaToolkit.Pin()->SetPreviewMesh(NewPreviewMesh, false);
}

void FPreviewSceneDescriptionCustomization::HandlePreviewAnimBlueprintChanged(const FAssetData& InAssetData)   
{
	UAnimBlueprint* NewAnimBlueprint = Cast<UAnimBlueprint>(InAssetData.GetAsset());
	PersonaToolkit.Pin()->SetPreviewAnimationBlueprint(NewAnimBlueprint);
}

void FPreviewSceneDescriptionCustomization::HandleAdditionalMeshesChanged(const FAssetData& InAssetData, IDetailLayoutBuilder* DetailLayoutBuilder)
{
	UDataAsset* MeshCollection = Cast<UDataAsset>(InAssetData.GetAsset());
	if (!MeshCollection || MeshCollection->GetClass()->ImplementsInterface(UPreviewCollectionInterface::StaticClass()))
	{
		PreviewScene.Pin()->SetAdditionalMeshes(MeshCollection);
	}

	DataAssetToDisplay = MeshCollection;
	DetailLayoutBuilder->ForceRefreshDetails();
}

void FPreviewSceneDescriptionCustomization::HandleAllowDifferentSkeletonsCheckedStateChanged(ECheckBoxState CheckState)
{
	GetMutableDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons = (CheckState == ECheckBoxState::Checked);
}

ECheckBoxState FPreviewSceneDescriptionCustomization::HandleAllowDifferentSkeletonsIsChecked() const
{
	return GetDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FPreviewSceneDescriptionCustomization::HandleUseCustomAnimBPCheckedStateChanged(ECheckBoxState CheckState)
{
	GetMutableDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToUseCustomAnimBP = (CheckState == ECheckBoxState::Checked);

	if (PreviewScene.IsValid())
	{
		PreviewScene.Pin()->RefreshAdditionalMeshes(false);
	}
}

ECheckBoxState FPreviewSceneDescriptionCustomization::HandleUseCustomAnimBPIsChecked() const
{
	return GetDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToUseCustomAnimBP? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool FPreviewSceneDescriptionCustomization::GetReplaceVisibility(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	// Only show the replace button if the current material can be replaced
 	if (AdditionalMeshesProperty.IsValid())
 	{
 		FAssetData AdditionalMeshesAsset;
 		AdditionalMeshesProperty->GetValue(AdditionalMeshesAsset);
 		return AdditionalMeshesAsset.IsValid();
 	}

	return false;
}

/**
* Called when reset to base is clicked
*/
void FPreviewSceneDescriptionCustomization::OnResetToBaseClicked(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	// Only allow reset to base if the current material can be replaced
 	if (AdditionalMeshesProperty.IsValid())
 	{
		FAssetData NullAsset;
		AdditionalMeshesProperty->SetValue(NullAsset);

		PreviewScene.Pin()->SetAdditionalMeshes(nullptr);
 	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 
// FPreviewMeshCollectionEntryCustomization
// 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FPreviewMeshCollectionEntryCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// get the enclosing preview mesh collection to determine the skeleton we want
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	check(OuterObjects.Num() > 0);
		
	if (OuterObjects[0] != nullptr)
	{
		FString SkeletonName = FAssetData(CastChecked<UPreviewMeshCollection>(OuterObjects[0])->Skeleton).GetExportTextName();

		PropertyHandle->GetParentHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPreviewMeshCollectionEntryCustomization::HandleMeshesArrayChanged, CustomizationUtils.GetPropertyUtilities()));

		TSharedPtr<IPropertyHandle> SkeletalMeshProperty = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPreviewMeshCollectionEntry, SkeletalMesh));
		if (SkeletalMeshProperty.IsValid())
		{
			HeaderRow.NameContent()
			[
				SkeletalMeshProperty->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MaxDesiredWidth(250.0f)
			.MinDesiredWidth(250.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(USkeletalMesh::StaticClass())
				.PropertyHandle(SkeletalMeshProperty)
				.OnShouldFilterAsset(this, &FPreviewMeshCollectionEntryCustomization::HandleShouldFilterAsset, SkeletonName)
				.OnObjectChanged(this, &FPreviewMeshCollectionEntryCustomization::HandleMeshChanged)
				.ThumbnailPool(CustomizationUtils.GetThumbnailPool())
			];
		}
	}
}

bool FPreviewMeshCollectionEntryCustomization::HandleShouldFilterAsset(const FAssetData& InAssetData, FString SkeletonName)
{
	if (GetDefault<UPersonaOptions>()->bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons)
	{
		return false;
	}

	FString SkeletonTag = InAssetData.GetTagValueRef<FString>("Skeleton");
	if (SkeletonTag == SkeletonName)
	{
		return false;
	}

	return true;
}

void FPreviewMeshCollectionEntryCustomization::HandleMeshChanged(const FAssetData& InAssetData)
{
	if (PreviewScene.IsValid())
	{
		// if mesh changes, don't override base mesh
		PreviewScene.Pin()->RefreshAdditionalMeshes(false);
	}
}

void FPreviewMeshCollectionEntryCustomization::HandleMeshesArrayChanged(TSharedPtr<IPropertyUtilities> PropertyUtilities)
{
	if (PreviewScene.IsValid())
	{
		// if additional mesh changes, allow it to override
		PreviewScene.Pin()->RefreshAdditionalMeshes(true);
		if (PropertyUtilities.IsValid())
		{
			PropertyUtilities->ForceRefresh();
		}
	}
}

#if CHAOS_SIMULATION_DETAIL_VIEW_FACTORY_SELECTOR
TSharedRef<SWidget> FPreviewSceneDescriptionCustomization::MakeClothingSimulationFactoryWidget(TSharedPtr<TSubclassOf<class UClothingSimulationFactory>> Item) const
{
	return SNew(STextBlock)
		.Text(*Item ? FText::FromName((*Item)->GetFName()) : LOCTEXT("PhysicsClothingSimulationFactory_NoneSelected", "None"))
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FPreviewSceneDescriptionCustomization::OnClothingSimulationFactorySelectionChanged(TSharedPtr<TSubclassOf<class UClothingSimulationFactory>> Item, ESelectInfo::Type SelectInfo) const
{
	// Set new factory to the preview mesh component:
	if (const TSharedPtr<IPersonaToolkit> PersonaToolkitPin = PersonaToolkit.Pin())
	{
		if (UDebugSkelMeshComponent* const DebugSkelMeshComponent = PersonaToolkitPin->GetPreviewMeshComponent())
		{
			DebugSkelMeshComponent->UnregisterComponent();
			DebugSkelMeshComponent->ClothingSimulationFactory = *Item;
			DebugSkelMeshComponent->RegisterComponent();
		}
	}
}

FText FPreviewSceneDescriptionCustomization::GetCurrentClothingSimulationFactoryText() const
{
	TSubclassOf<class UClothingSimulationFactory> Item;
	if (const TSharedPtr<IPersonaToolkit> PersonaToolkitPin = PersonaToolkit.Pin())
	{
		if (const UDebugSkelMeshComponent* const DebugSkelMeshComponent = PersonaToolkitPin->GetPreviewMeshComponent())
		{
			Item = DebugSkelMeshComponent->ClothingSimulationFactory;
		}
	}
	return *Item ? FText::FromName((*Item)->GetFName()) : LOCTEXT("PhysicsClothingSimulationFactory_NoneSelected", "None");
}
#endif  // #if CHAOS_SIMULATION_DETAIL_VIEW_FACTORY_SELECTOR
#undef LOCTEXT_NAMESPACE
