// Copyright Epic Games, Inc. All Rights Reserved.

#include "SClothPaintTab.h"

#include "AssetRegistryModule.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"


#include "ClothPaintingModule.h"
#include "ClothPainter.h"
#include "ClothingPaintEditMode.h"

#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"

#include "AssetEditorModeManager.h"

#include "ISkeletalMeshEditor.h"
#include "SClothPaintWidget.h"
#include "IPersonaToolkit.h"
#include "SClothAssetSelector.h"
#include "ClothingAsset.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ComponentReregisterContext.h"

#define LOCTEXT_NAMESPACE "SClothPaintTab"

SClothPaintTab::SClothPaintTab() 
	: bModeApplied(false), bPaintModeEnabled(false)
{	
}

SClothPaintTab::~SClothPaintTab()
{
	if(ISkeletalMeshEditor* SkeletalMeshEditor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get()))
	{
		if(FAssetEditorModeManager* ModeManager = SkeletalMeshEditor->GetAssetEditorModeManager())
		{
			ModeManager->ActivateDefaultMode();
		}
	}
}

void SClothPaintTab::Construct(const FArguments& InArgs)
{
	// Detail view for UClothingAssetCommon
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	DetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	
	// Add delegate for editing enabled, which allows us to show a greyed out version with the CDO
	// selected when we haven't got an asset selected to avoid the UI popping.
	DetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &SClothPaintTab::IsAssetDetailsPanelEnabled));

	// Add the CDO by default
	TArray<UObject*> Objects;
	Objects.Add(UClothingAssetCommon::StaticClass()->GetDefaultObject());
	DetailsView->SetObjects(Objects, true);

	HostingApp = InArgs._InHostingApp;

	ModeWidget = nullptr;
	
	FSlateIcon TexturePaintIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode.TexturePaint");

	this->ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(ContentBox, SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Center)
		]
	];

	ISkeletalMeshEditor* SkeletalMeshEditor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get());

	if(SkeletalMeshEditor)
	{
		IPersonaToolkit& Persona = SkeletalMeshEditor->GetPersonaToolkit().Get();

		ContentBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(SelectorWidget, SClothAssetSelector, Persona.GetMesh())
				.OnSelectionChanged(this, &SClothPaintTab::OnAssetSelectionChanged)
		];

		ContentBox->AddSlot()
		.AutoHeight()
		[
			DetailsView->AsShared()
		];
	}
}

void SClothPaintTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SClothPaintTab::TogglePaintMode()
{
	bPaintModeEnabled = !bPaintModeEnabled;
	UpdatePaintTools();
}

bool SClothPaintTab::IsPaintModeActive() const
{
	return bPaintModeEnabled;
}

void SClothPaintTab::UpdatePaintTools()
{
	if(!HostingApp.IsValid())
	{
		// If we have no valid host, we can't do anything with our editor below, so don't perform a
		// state update on the tool mode.
		return;
	}

	if (bPaintModeEnabled)
	{
		ISkeletalMeshEditor* SkeletalMeshEditor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get());
		SkeletalMeshEditor->GetAssetEditorModeManager()->ActivateMode(PaintModeID, true);

		FClothingPaintEditMode* PaintMode = (FClothingPaintEditMode*)SkeletalMeshEditor->GetAssetEditorModeManager()->GetActiveMode(PaintModeID);
		if (PaintMode)
		{
			FClothPainter* ClothPainter = static_cast<FClothPainter*>(PaintMode->GetMeshPainter());
			check(ClothPainter);

			ClothPainter->Reset();
			ModeWidget = StaticCastSharedPtr<SClothPaintWidget>(ClothPainter->GetWidget());
			PaintMode->SetPersonaToolKit(SkeletalMeshEditor->GetPersonaToolkit());

			ContentBox->AddSlot()
			.AutoHeight()
			[
				ModeWidget->AsShared()
			];

			if(SelectorWidget.IsValid())
			{
				TWeakObjectPtr<UClothingAssetCommon> WeakAsset = SelectorWidget->GetSelectedAsset();

				if(WeakAsset.Get())
				{
					ClothPainter->OnAssetSelectionChanged(WeakAsset.Get(), SelectorWidget->GetSelectedLod(), SelectorWidget->GetSelectedMask());
				}
			}
		}
	}
	else
	{
		ContentBox->RemoveSlot(ModeWidget->AsShared());
		ISkeletalMeshEditor* SkeletalMeshEditor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get());
		SkeletalMeshEditor->GetAssetEditorModeManager()->ActivateDefaultMode();
		ModeWidget = nullptr;
	}
}

void SClothPaintTab::OnAssetSelectionChanged(TWeakObjectPtr<UClothingAssetCommon> InAssetPtr, int32 InLodIndex, int32 InMaskIndex)
{
	if(bPaintModeEnabled)
	{
		ISkeletalMeshEditor* SkeletalMeshEditor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get());

		FClothingPaintEditMode* PaintMode = (FClothingPaintEditMode*)SkeletalMeshEditor->GetAssetEditorModeManager()->GetActiveMode(PaintModeID);
		if(PaintMode)
		{
			FClothPainter* ClothPainter = static_cast<FClothPainter*>(PaintMode->GetMeshPainter());

			if(ClothPainter)
			{
				ClothPainter->OnAssetSelectionChanged(InAssetPtr.Get(), InLodIndex, InMaskIndex);
			}
		}
	}

	if(UClothingAssetCommon* Asset = InAssetPtr.Get())
	{
		TArray<UObject*> Objects;

		Objects.Add(Asset);

		DetailsView->SetObjects(Objects, true);
	}
}

bool SClothPaintTab::IsAssetDetailsPanelEnabled()
{
	// Only enable editing if we have a valid details panel that is not observing the CDO
	if(DetailsView.IsValid())
	{
		const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailsView->GetSelectedObjects();

		if(SelectedObjects.Num() > 0)
		{
			return SelectedObjects[0].Get() != UClothingAssetCommon::StaticClass()->GetDefaultObject();
		}
	}

	return false;
}

TSharedRef<IPersonaToolkit> SClothPaintTab::GetPersonaToolkit() const
{
	return GetSkeletalMeshEditor()->GetPersonaToolkit();
}

ISkeletalMeshEditor* SClothPaintTab::GetSkeletalMeshEditor() const
{
	ISkeletalMeshEditor* Editor = static_cast<ISkeletalMeshEditor*>(HostingApp.Pin().Get());
	check(Editor);

	return Editor;
}

#undef LOCTEXT_NAMESPACE //"SClothPaintTab"
