// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetContextMenu.h"
#include "Templates/SubclassOf.h"
#include "Styling/SlateTypes.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/FileManager.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "EditorReimportHandler.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UnrealClient.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/Material.h"
#include "SourceControlOperations.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Materials/MaterialInstanceConstant.h"
#include "FileHelpers.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "ContentBrowserUtils.h"
#include "SAssetView.h"
#include "ContentBrowserModule.h"
#include "Dialogs/Dialogs.h"
#include "SMetaDataView.h"

#include "ObjectTools.h"
#include "PackageTools.h"
#include "Editor.h"

#include "PropertyEditorModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "ConsolidateWindow.h"
#include "ReferencedAssetsUtils.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Internationalization/TextLocalizationResource.h"

#include "SourceControlWindows.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "CollectionAssetManagement.h"
#include "ComponentAssetBroker.h"
#include "Widgets/Input/SNumericEntryBox.h"

#include "SourceCodeNavigation.h"
#include "IDocumentation.h"
#include "EditorClassUtils.h"

#include "Internationalization/Culture.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Engine/LevelStreaming.h"
#include "ContentBrowserCommands.h"

#include "PackageHelperFunctions.h"
#include "EngineUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "Commandlets/TextAssetCommandlet.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

FAssetContextMenu::FAssetContextMenu(const TWeakPtr<SAssetView>& InAssetView)
	: AssetView(InAssetView)
	, bAtLeastOneNonRedirectorSelected(false)
	, bAtLeastOneClassSelected(false)
	, bCanExecuteSCCMerge(false)
	, bCanExecuteSCCCheckOut(false)
	, bCanExecuteSCCOpenForAdd(false)
	, bCanExecuteSCCCheckIn(false)
	, bCanExecuteSCCHistory(false)
	, bCanExecuteSCCRevert(false)
	, bCanExecuteSCCSync(false)
{
	
}

void FAssetContextMenu::BindCommands(TSharedPtr< FUICommandList >& Commands)
{
	Commands->MapAction(FGenericCommands::Get().Duplicate, FUIAction(
		FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteDuplicate),
		FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteDuplicate),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FAssetContextMenu::CanExecuteDuplicate)
		));

	Commands->MapAction(FGlobalEditorCommonCommands::Get().FindInContentBrowser, FUIAction(
		FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteSyncToAssetTree),
		FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteSyncToAssetTree)
		));
}

TSharedRef<SWidget> FAssetContextMenu::MakeContextMenu(const TArray<FAssetData>& InSelectedAssets, const FSourcesData& InSourcesData, TSharedPtr< FUICommandList > InCommandList)
{
	SetSelectedAssets(InSelectedAssets);
	SourcesData = InSourcesData;

	// Cache any vars that are used in determining if you can execute any actions.
	// Useful for actions whose "CanExecute" will not change or is expensive to calculate.
	CacheCanExecuteVars();

	// Get all menu extenders for this context menu from the content browser module
	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>( TEXT("ContentBrowser") );
	TArray<FContentBrowserMenuExtender_SelectedAssets> MenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

	TArray<TSharedPtr<FExtender>> Extenders;
	for (int32 i = 0; i < MenuExtenderDelegates.Num(); ++i)
	{
		if (MenuExtenderDelegates[i].IsBound())
		{
			Extenders.Add(MenuExtenderDelegates[i].Execute(SelectedAssets));
		}
	}
	TSharedPtr<FExtender> MenuExtender = FExtender::Combine(Extenders);

	UContentBrowserAssetContextMenuContext* ContextObject = NewObject<UContentBrowserAssetContextMenuContext>();
	ContextObject->AssetContextMenu = SharedThis(this);

	UToolMenus* ToolMenus = UToolMenus::Get();

	static const FName BaseMenuName("ContentBrowser.AssetContextMenu");
	RegisterContextMenu(BaseMenuName);

	TArray<UObject*> SelectedObjects;

	// Create menu hierarchy based on class hierarchy
	FName MenuName = BaseMenuName;
	{
		// Objects must be loaded for this operation... for now
		TArray<FString> ObjectPaths;
		for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
		{
			ObjectPaths.Add(SelectedAssets[AssetIdx].ObjectPath.ToString());
		}

		ContextObject->SelectedObjects.Reset();
		if (ContentBrowserUtils::LoadAssetsIfNeeded(ObjectPaths, SelectedObjects) && SelectedObjects.Num() > 0)
		{
			ContextObject->SelectedObjects.Append(SelectedObjects);

			// Find common class for selected objects
			UClass* CommonClass = SelectedObjects[0]->GetClass();
			for (int32 ObjIdx = 1; ObjIdx < SelectedObjects.Num(); ++ObjIdx)
			{
				while (!SelectedObjects[ObjIdx]->IsA(CommonClass))
				{
					CommonClass = CommonClass->GetSuperClass();
				}
			}
			ContextObject->CommonClass = CommonClass;

			ContextObject->bCanBeModified = true;

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			const TSharedRef<FBlacklistPaths>& WritableFolderFilter = AssetToolsModule.Get().GetWritableFolderBlacklist();
			if (WritableFolderFilter->HasFiltering())
			{
				for (const UObject* SelectedObject : SelectedObjects)
				{
					if (SelectedObject)
					{
						UPackage* SelectedObjectPackage = SelectedObject->GetOutermost();
						if (SelectedObjectPackage && !WritableFolderFilter->PassesStartsWithFilter(SelectedObjectPackage->GetFName()))
						{
							ContextObject->bCanBeModified = false;
							break;
						}
					}
				}
			}

			MenuName = UToolMenus::JoinMenuPaths(BaseMenuName, CommonClass->GetFName());

			RegisterMenuHierarchy(CommonClass);

			// Find asset actions for common class
			TSharedPtr<IAssetTypeActions> CommonAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(ContextObject->CommonClass).Pin();
			if (CommonAssetTypeActions.IsValid() && CommonAssetTypeActions->HasActions(SelectedObjects))
			{
				ContextObject->CommonAssetTypeActions = CommonAssetTypeActions;
			}
		}
	}

	FToolMenuContext MenuContext(InCommandList, MenuExtender, ContextObject);
	return ToolMenus->GenerateWidget(MenuName, MenuContext);
}

void FAssetContextMenu::RegisterMenuHierarchy(UClass* InClass)
{
	static const FName BaseMenuName("ContentBrowser.AssetContextMenu");

	UToolMenus* ToolMenus = UToolMenus::Get();

	for (UClass* CurrentClass = InClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		FName CurrentMenuName = UToolMenus::JoinMenuPaths(BaseMenuName, CurrentClass->GetFName());
		if (!ToolMenus->IsMenuRegistered(CurrentMenuName))
		{
			FName ParentMenuName;
			UClass* ParentClass = CurrentClass->GetSuperClass();
			if (ParentClass == UObject::StaticClass() || ParentClass == nullptr)
			{
				ParentMenuName = BaseMenuName;
			}
			else
			{
				ParentMenuName = UToolMenus::JoinMenuPaths(BaseMenuName, ParentClass->GetFName());
			}

			ToolMenus->RegisterMenu(CurrentMenuName, ParentMenuName);

			if (ParentMenuName == BaseMenuName)
			{
				break;
			}
		}
	}
}

void FAssetContextMenu::RegisterContextMenu(const FName MenuName)
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName);
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

		Section.AddDynamicEntry("GetActions", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->CommonAssetTypeActions.IsValid())
			{
				Context->CommonAssetTypeActions.Pin()->GetActions(Context->GetSelectedObjects(), InSection);
			}
		}));

		Section.AddDynamicEntry("GetActionsLegacy", FNewToolMenuDelegateLegacy::CreateLambda([](FMenuBuilder& MenuBuilder, UToolMenu* InMenu)
		{
			UContentBrowserAssetContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->CommonAssetTypeActions.IsValid())
			{
				Context->CommonAssetTypeActions.Pin()->GetActions(Context->GetSelectedObjects(), MenuBuilder);
			}
		}));

		Menu->AddDynamicSection("AddMenuOptions", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			UContentBrowserAssetContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->AssetContextMenu.IsValid())
			{
				Context->AssetContextMenu.Pin()->AddMenuOptions(InMenu);
			}
		}));
	}
}

void FAssetContextMenu::AddMenuOptions(UToolMenu* InMenu)
{
	UContentBrowserAssetContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	// Add any type-specific context menu options
	AddAssetTypeMenuOptions(InMenu, Context->SelectedObjects.Num() > 0);

	// Add imported asset context menu options
	if (Context->bCanBeModified)
	{
		AddImportedAssetMenuOptions(InMenu);
	}

	// Add quick access to common commands.
	AddCommonMenuOptions(InMenu);

	// Add quick access to view commands
	AddExploreMenuOptions(InMenu);

	// Add reference options
	AddReferenceMenuOptions(InMenu);

	// Add collection options
	if (Context->bCanBeModified)
	{
		AddCollectionMenuOptions(InMenu);
	}

	// Add documentation options
	AddDocumentationMenuOptions(InMenu);

	// Add source control options
	if (Context->bCanBeModified)
	{
		AddSourceControlMenuOptions(InMenu);
	}
}

void FAssetContextMenu::SetSelectedAssets(const TArray<FAssetData>& InSelectedAssets)
{
	SelectedAssets = InSelectedAssets;
}

void FAssetContextMenu::SetOnFindInAssetTreeRequested(const FOnFindInAssetTreeRequested& InOnFindInAssetTreeRequested)
{
	OnFindInAssetTreeRequested = InOnFindInAssetTreeRequested;
}

void FAssetContextMenu::SetOnRenameRequested(const FOnRenameRequested& InOnRenameRequested)
{
	OnRenameRequested = InOnRenameRequested;
}

void FAssetContextMenu::SetOnRenameFolderRequested(const FOnRenameFolderRequested& InOnRenameFolderRequested)
{
	OnRenameFolderRequested = InOnRenameFolderRequested;
}

void FAssetContextMenu::SetOnDuplicateRequested(const FOnDuplicateRequested& InOnDuplicateRequested)
{
	OnDuplicateRequested = InOnDuplicateRequested;
}

void FAssetContextMenu::SetOnAssetViewRefreshRequested(const FOnAssetViewRefreshRequested& InOnAssetViewRefreshRequested)
{
	OnAssetViewRefreshRequested = InOnAssetViewRefreshRequested;
}

bool FAssetContextMenu::AddImportedAssetMenuOptions(UToolMenu* Menu)
{
	if (AreImportedAssetActionsVisible())
	{
		TArray<FString> ResolvedFilePaths;
		TArray<FString> SourceFileLabels;
		int32 ValidSelectedAssetCount = 0;
		GetSelectedAssetSourceFilePaths(ResolvedFilePaths, SourceFileLabels, ValidSelectedAssetCount);

		FToolMenuSection& Section = Menu->AddSection("ImportedAssetActions", LOCTEXT("ImportedAssetActionsMenuHeading", "Imported Asset"));
		{
			auto CreateSubMenu = [this](UToolMenu* SubMenu, bool bReimportWithNewFile)
			{
				//Get the data, we cannot use the closure since the lambda will be call when the function scope will be gone
				TArray<FString> ResolvedFilePaths;
				TArray<FString> SourceFileLabels;
				int32 ValidSelectedAssetCount = 0;
				GetSelectedAssetSourceFilePaths(ResolvedFilePaths, SourceFileLabels, ValidSelectedAssetCount);
				if (SourceFileLabels.Num() > 0 )
				{
					FToolMenuSection& SubSection = SubMenu->AddSection("Section");
					for (int32 SourceFileIndex = 0; SourceFileIndex < SourceFileLabels.Num(); ++SourceFileIndex)
					{
						FText ReimportLabel = FText::Format(LOCTEXT("ReimportNoLabel", "SourceFile {0}"), SourceFileIndex);
						FText ReimportLabelTooltip;
						if (ValidSelectedAssetCount == 1)
						{
							ReimportLabelTooltip = FText::Format(LOCTEXT("ReimportNoLabelTooltip", "Reimport File: {0}"), FText::FromString(ResolvedFilePaths[SourceFileIndex]));
						}
						if (SourceFileLabels[SourceFileIndex].Len() > 0)
						{
							ReimportLabel = FText::Format(LOCTEXT("ReimportLabel", "{0}"), FText::FromString(SourceFileLabels[SourceFileIndex]));
							if (ValidSelectedAssetCount == 1)
							{
								ReimportLabelTooltip = FText::Format(LOCTEXT("ReimportLabelTooltip", "Reimport {0} File: {1}"), FText::FromString(SourceFileLabels[SourceFileIndex]), FText::FromString(ResolvedFilePaths[SourceFileIndex]));
							}
						}
						if (bReimportWithNewFile)
						{
							SubSection.AddMenuEntry(
								NAME_None,
								ReimportLabel,
								ReimportLabelTooltip,
								FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
								FUIAction(
									FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteReimportWithNewFile, SourceFileIndex),
									FCanExecuteAction()
								)
							);
						}
						else
						{
							SubSection.AddMenuEntry(
								NAME_None,
								ReimportLabel,
								ReimportLabelTooltip,
								FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
								FUIAction(
									FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteReimport, SourceFileIndex),
									FCanExecuteAction()
								)
							);
						}
						
					}
				}
			};

			//Reimport Menu
			if (ValidSelectedAssetCount == 1 && SourceFileLabels.Num() > 1)
			{
				Section.AddSubMenu(
					"Reimport",
					LOCTEXT("Reimport", "Reimport"),
					LOCTEXT("ReimportEmptyTooltip", ""),
					FNewToolMenuDelegate::CreateLambda(CreateSubMenu, false) );
				//With new file
				Section.AddSubMenu(
					"ReimportWithNewFile",
					LOCTEXT("ReimportWithNewFile", "Reimport With New File"),
					LOCTEXT("ReimportEmptyTooltip", ""),
					FNewToolMenuDelegate::CreateLambda(CreateSubMenu, true));
			}
			else
			{
				Section.AddMenuEntry(
					"Reimport",
					LOCTEXT("Reimport", "Reimport"),
					LOCTEXT("ReimportTooltip", "Reimport the selected asset(s) from the source file on disk."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteReimport, (int32)INDEX_NONE),
						FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteReimportAssetActions, ResolvedFilePaths)
					)
				);
				if (ValidSelectedAssetCount == 1)
				{
					//With new file
					Section.AddMenuEntry(
						"ReimportWithNewFile",
						LOCTEXT("ReimportWithNewFile", "Reimport With New File"),
						LOCTEXT("ReimportWithNewFileTooltip", "Reimport the selected asset from a new source file on disk."),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
						FUIAction(
							FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteReimportWithNewFile, (int32)INDEX_NONE),
							FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteReimportAssetActions, ResolvedFilePaths)
						)
					);
				}
			}

			// Show Source In Explorer
			Section.AddMenuEntry(
				"FindSourceFile",
				LOCTEXT("FindSourceFile", "Open Source Location"),
				LOCTEXT("FindSourceFileTooltip", "Opens the folder containing the source of the selected asset(s)."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.OpenSourceLocation"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindSourceInExplorer, ResolvedFilePaths),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteImportedAssetActions, ResolvedFilePaths)
					)
				);

			// Open In External Editor
			Section.AddMenuEntry(
				"OpenInExternalEditor",
				LOCTEXT("OpenInExternalEditor", "Open In External Editor"),
				LOCTEXT("OpenInExternalEditorTooltip", "Open the selected asset(s) in the default external editor."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.OpenInExternalEditor"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteOpenInExternalEditor, ResolvedFilePaths),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteImportedAssetActions, ResolvedFilePaths)
					)
				);
		}

		return true;
	}
	

	return false;
}

bool FAssetContextMenu::AddCommonMenuOptions(UToolMenu* Menu)
{
	int32 NumAssetItems, NumClassItems;
	ContentBrowserUtils::CountItemTypes(SelectedAssets, NumAssetItems, NumClassItems);

	UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
	const bool bCanBeModified = !Context || Context->bCanBeModified;

	{
		FToolMenuSection& Section = Menu->AddSection("CommonAssetActions", LOCTEXT("CommonAssetActionsMenuHeading", "Common"));

		// Edit
		if (bCanBeModified)
		{
			Section.AddMenuEntry(
				"EditAsset",
				LOCTEXT("EditAsset", "Edit..."),
				LOCTEXT("EditAssetTooltip", "Opens the selected asset(s) for edit."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Edit"),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteEditAsset))
			);
		}
	
		// Only add these options if assets are selected
		if (NumAssetItems > 0)
		{
			if (bCanBeModified)
			{
				// Rename
				Section.AddMenuEntry(FGenericCommands::Get().Rename,
					LOCTEXT("Rename", "Rename"),
					LOCTEXT("RenameTooltip", "Rename the selected asset."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Rename")
				);

				// Duplicate
				Section.AddMenuEntry(FGenericCommands::Get().Duplicate,
					LOCTEXT("Duplicate", "Duplicate"),
					LOCTEXT("DuplicateTooltip", "Create a copy of the selected asset(s)."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Duplicate")
				);

				// Save
				Section.AddMenuEntry(FContentBrowserCommands::Get().SaveSelectedAsset,
					LOCTEXT("SaveAsset", "Save"),
					LOCTEXT("SaveAssetTooltip", "Saves the asset to file."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.SaveIcon16x")
				);

				// Delete
				Section.AddMenuEntry(FGenericCommands::Get().Delete,
					LOCTEXT("Delete", "Delete"),
					LOCTEXT("DeleteTooltip", "Delete the selected assets."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Delete")
				);
			}

			// Asset Actions sub-menu
			Section.AddSubMenu(
				"AssetActionsSubMenu",
				LOCTEXT("AssetActionsSubMenuLabel", "Asset Actions"),
				LOCTEXT("AssetActionsSubMenuToolTip", "Other asset actions"),
				FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::MakeAssetActionsSubMenu),
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteAssetActions )
					),
				EUserInterfaceActionType::Button,
				false, 
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions")
				);

			if (NumClassItems == 0 && bCanBeModified)
			{
				// Asset Localization sub-menu
				Section.AddSubMenu(
					"LocalizationSubMenu",
					LOCTEXT("LocalizationSubMenuLabel", "Asset Localization"),
					LOCTEXT("LocalizationSubMenuToolTip", "Manage the localization of this asset"),
					FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::MakeAssetLocalizationSubMenu),
					FUIAction(),
					EUserInterfaceActionType::Button,
					false,
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetLocalization")
					);
			}
		}
	}

	return true;
}

void FAssetContextMenu::AddExploreMenuOptions(UToolMenu* Menu)
{
	UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();

	FToolMenuSection& Section = Menu->AddSection("AssetContextExploreMenuOptions", LOCTEXT("AssetContextExploreMenuOptionsHeading", "Explore"));
	{
		// Find in Content Browser
		Section.AddMenuEntry(
			FGlobalEditorCommonCommands::Get().FindInContentBrowser, 
			LOCTEXT("ShowInFolderView", "Show in Folder View"),
			LOCTEXT("ShowInFolderViewTooltip", "Selects the folder that contains this asset in the Content Browser Sources Panel.")
			);

		if (Context->bCanBeModified)
		{
			// Find in Explorer
			Section.AddMenuEntry(
				"FindInExplorer",
				ContentBrowserUtils::GetExploreFolderText(),
				LOCTEXT("FindInExplorerTooltip", "Finds this asset on disk"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindInExplorer),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteFindInExplorer)
				)
			);
		}
	}
}

void FAssetContextMenu::MakeAssetActionsSubMenu(UToolMenu* Menu)
{
	UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
	const bool bCanBeModified = !Context || Context->bCanBeModified;

	{
		FToolMenuSection& Section = Menu->AddSection("AssetActionsSection");

		if (bCanBeModified)
		{
			// Create BP Using This
			Section.AddMenuEntry(
				"CreateBlueprintUsing",
				LOCTEXT("CreateBlueprintUsing", "Create Blueprint Using This..."),
				LOCTEXT("CreateBlueprintUsingTooltip", "Create a new Blueprint and add this asset to it"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.CreateClassBlueprint"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCreateBlueprintUsing),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteCreateBlueprintUsing)
				)
			);
		}

		// Capture Thumbnail
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		if (bCanBeModified && SelectedAssets.Num() == 1 && AssetToolsModule.Get().AssetUsesGenericThumbnail(SelectedAssets[0]))
		{
			Section.AddMenuEntry(
				"CaptureThumbnail",
				LOCTEXT("CaptureThumbnail", "Capture Thumbnail"),
				LOCTEXT("CaptureThumbnailTooltip", "Captures a thumbnail from the active viewport."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.CreateThumbnail"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCaptureThumbnail),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteCaptureThumbnail)
				)
			);
		}

		// Clear Thumbnail
		if (bCanBeModified && CanClearCustomThumbnails())
		{
			Section.AddMenuEntry(
				"ClearCustomThumbnail",
				LOCTEXT("ClearCustomThumbnail", "Clear Thumbnail"),
				LOCTEXT("ClearCustomThumbnailTooltip", "Clears all custom thumbnails for selected assets."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.DeleteThumbnail"),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteClearThumbnail))
			);
		}
	}

	// FIND ACTIONS
	{
		FToolMenuSection& Section = Menu->AddSection("AssetContextFindActions", LOCTEXT("AssetContextFindActionsMenuHeading", "Find"));
		// Select Actors Using This Asset
		Section.AddMenuEntry(
			"FindAssetInWorld",
			LOCTEXT("FindAssetInWorld", "Select Actors Using This Asset"),
			LOCTEXT("FindAssetInWorldTooltip", "Selects all actors referencing this asset."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindAssetInWorld),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteFindAssetInWorld)
				)
			);
	}

	// MOVE ACTIONS
	if (bCanBeModified)
	{
		FToolMenuSection& Section = Menu->AddSection("AssetContextMoveActions", LOCTEXT("AssetContextMoveActionsMenuHeading", "Move"));
		bool bHasExportableAssets = false;
		for (const FAssetData& AssetData : SelectedAssets)
		{
			const UObject* Object = AssetData.GetAsset();
			if (Object)
			{
				const UPackage* Package = Object->GetOutermost();
				if (!Package->HasAnyPackageFlags(EPackageFlags::PKG_DisallowExport))
				{
					bHasExportableAssets = true;
					break;
				}
			}
		}

		if (bHasExportableAssets)
		{
			// Export
			Section.AddMenuEntry(
				"Export",
				LOCTEXT("Export", "Export..."),
				LOCTEXT("ExportTooltip", "Export the selected assets to file."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteExport ) )
				);

			// Bulk Export
			if (SelectedAssets.Num() > 1)
			{
				Section.AddMenuEntry(
					"BulkExport",
					LOCTEXT("BulkExport", "Bulk Export..."),
					LOCTEXT("BulkExportTooltip", "Export the selected assets to file in the selected directory"),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteBulkExport ) )
					);
			}
		}

		// Migrate
		Section.AddMenuEntry(
			"MigrateAsset",
			LOCTEXT("MigrateAsset", "Migrate..."),
			LOCTEXT("MigrateAssetTooltip", "Copies all selected assets and their dependencies to another project"),
			FSlateIcon(),
			FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteMigrateAsset ) )
			);
	}

	// ADVANCED ACTIONS
	if (bCanBeModified)
	{
		FToolMenuSection& Section = Menu->AddSection("AssetContextAdvancedActions", LOCTEXT("AssetContextAdvancedActionsMenuHeading", "Advanced"));

		// Reload
		Section.AddMenuEntry(
			"Reload",
			LOCTEXT("Reload", "Reload"),
			LOCTEXT("ReloadTooltip", "Reload the selected assets from their file on disk."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteReload),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteReload)
			)
		);

		// Replace References
		if (CanExecuteConsolidate())
		{
			Section.AddMenuEntry(
				"ReplaceReferences",
				LOCTEXT("ReplaceReferences", "Replace References"),
				LOCTEXT("ConsolidateTooltip", "Replace references to the selected assets."),
				FSlateIcon(),
				FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteConsolidate)
				)
				);
		}

		// Property Matrix
		bool bCanUsePropertyMatrix = true;
		// Materials can't be bulk edited currently as they require very special handling because of their dependencies with the rendering thread, and we'd have to hack the property matrix too much.
		for (auto& Asset : SelectedAssets)
		{
			if (Asset.AssetClass == UMaterial::StaticClass()->GetFName() || Asset.AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName() || Asset.AssetClass == UMaterialFunction::StaticClass()->GetFName() || Asset.AssetClass == UMaterialFunctionInstance::StaticClass()->GetFName())
			{
				bCanUsePropertyMatrix = false;
				break;
			}
		}

		if (bCanUsePropertyMatrix)
		{
			TAttribute<FText>::FGetter DynamicTooltipGetter;
			DynamicTooltipGetter.BindSP(this, &FAssetContextMenu::GetExecutePropertyMatrixTooltip);
			TAttribute<FText> DynamicTooltipAttribute = TAttribute<FText>::Create(DynamicTooltipGetter);

			Section.AddMenuEntry(
				"PropertyMatrix",
				LOCTEXT("PropertyMatrix", "Bulk Edit via Property Matrix..."),
				DynamicTooltipAttribute,
				FSlateIcon(),
				FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecutePropertyMatrix),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecutePropertyMatrix)
				)
				);
		}

		// Create Metadata menu
		Section.AddMenuEntry(
			"ShowAssetMetaData",
			LOCTEXT("ShowAssetMetaData", "Show Metadata"),
			LOCTEXT("ShowAssetMetaDataTooltip", "Show the asset metadata dialog."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteShowAssetMetaData),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteShowAssetMetaData)
			)
		);

		// Chunk actions
		if (GetDefault<UEditorExperimentalSettings>()->bContextMenuChunkAssignments)
		{
			Section.AddMenuEntry(
				"AssignAssetChunk",
				LOCTEXT("AssignAssetChunk", "Assign to Chunk..."),
				LOCTEXT("AssignAssetChunkTooltip", "Assign this asset to a specific Chunk"),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteAssignChunkID) )
				);

			Section.AddSubMenu(
				"RemoveAssetFromChunk",
				LOCTEXT("RemoveAssetFromChunk", "Remove from Chunk..."),
				LOCTEXT("RemoveAssetFromChunkTooltip", "Removed an asset from a Chunk it's assigned to."),
				FNewToolMenuDelegate::CreateRaw(this, &FAssetContextMenu::MakeChunkIDListMenu)
				);

			Section.AddMenuEntry(
				"RemoveAllChunkAssignments",
				LOCTEXT("RemoveAllChunkAssignments", "Remove from all Chunks"),
				LOCTEXT("RemoveAllChunkAssignmentsTooltip", "Removed an asset from all Chunks it's assigned to."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteRemoveAllChunkID) )
				);
		}
	}

	if (bCanBeModified && GetDefault<UEditorExperimentalSettings>()->bTextAssetFormatSupport)
	{
		FToolMenuSection& FormatActionsSection = Menu->AddSection("AssetContextTextAssetFormatActions", LOCTEXT("AssetContextTextAssetFormatActionsHeading", "Text Assets"));
		{
			FormatActionsSection.AddMenuEntry(
				"ExportToTextFormat",
				LOCTEXT("ExportToTextFormat", "Export to text format"),
				LOCTEXT("ExportToTextFormatTooltip", "Exports the selected asset(s) to the experimental text asset format"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExportSelectedAssetsToText))
			);

			FormatActionsSection.AddMenuEntry(
				"ViewSelectedAssetAsText",
				LOCTEXT("ViewSelectedAssetAsText", "View as text"),
				LOCTEXT("ViewSelectedAssetAsTextTooltip", "Opens a window showing the selected asset in text format"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ViewSelectedAssetAsText),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanViewSelectedAssetAsText))
			);

			FormatActionsSection.AddMenuEntry(
				"ViewSelectedAssetAsText",
				LOCTEXT("TextFormatRountrip", "Run Text Asset Roundtrip"),
				LOCTEXT("TextFormatRountripTooltip", "Save the select asset backwards or forwards between text and binary formats and check for determinism"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::DoTextFormatRoundtrip))
			);
		}
	}
}

void FAssetContextMenu::ExportSelectedAssetsToText()
{
	FString FailedPackage;
	for (const FAssetData& Asset : SelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		FString Filename = FPackageName::LongPackageNameToFilename(Package->GetPathName(), FPackageName::GetTextAssetPackageExtension());
		if (!SavePackageHelper(Package, Filename))
		{
			FailedPackage = Package->GetPathName();
			break;
		}
	}

	if (FailedPackage.Len() > 0)
	{
		FNotificationInfo Info(LOCTEXT("ExportedTextAssetFailed", "Exported selected asset(s) failed"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("ExportedTextAssetsSuccessfully", "Exported selected asset(s) successfully"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void FAssetContextMenu::ViewSelectedAssetAsText()
{
	if (SelectedAssets.Num() == 1)
	{
		UPackage* Package = SelectedAssets[0].GetPackage();
		FString TargetFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), nullptr, *FPackageName::GetTextAssetPackageExtension());
		if (SavePackageHelper(Package, TargetFilename))
		{
			FString TextFormat;
			if (FFileHelper::LoadFileToString(TextFormat, *TargetFilename))
			{
				SGenericDialogWidget::OpenDialog(LOCTEXT("TextAssetViewerTitle", "Viewing AS Text Asset..."), SNew(STextBlock).Text(FText::FromString(TextFormat)));
			}
			IFileManager::Get().Delete(*TargetFilename);
		}
	}
}

bool FAssetContextMenu::CanViewSelectedAssetAsText() const
{
	return SelectedAssets.Num() == 1;
}

void FAssetContextMenu::DoTextFormatRoundtrip()
{
	UTextAssetCommandlet::FProcessingArgs Args;
	Args.NumSaveIterations = 1;
	Args.bIncludeEngineContent = true;
	Args.bVerifyJson = true;
	Args.CSVFilename = TEXT("");
	Args.ProcessingMode = ETextAssetCommandletMode::RoundTrip;
	Args.bFilenameIsFilter = false;

	for (const FAssetData& Asset : SelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		Args.Filename = FPackageName::LongPackageNameToFilename(Package->GetPathName());
		if (!UTextAssetCommandlet::DoTextAssetProcessing(Args))
		{
			FNotificationInfo Info(LOCTEXT("RountripTextAssetFailed", "Roundtripping of selected asset(s) failed"));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return;
		}
	}

	FNotificationInfo Info(LOCTEXT("RoundtripTextAssetsSuccessfully", "Roundtripped selected asset(s) successfully"));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

bool FAssetContextMenu::CanExecuteAssetActions() const
{
	return !bAtLeastOneClassSelected;
}

void FAssetContextMenu::MakeAssetLocalizationSubMenu(UToolMenu* Menu)
{
	TArray<FCultureRef> CurrentCultures;

	// Build up the list of cultures already used
	{
		TSet<FString> CultureNames;

		bool bIncludeEngineCultures = false;
		bool bIncludeProjectCultures = false;

		for (const FAssetData& Asset : SelectedAssets)
		{
			const FString AssetPath = Asset.ObjectPath.ToString();

			if (ContentBrowserUtils::IsEngineFolder(AssetPath))
			{
				bIncludeEngineCultures = true;
			}
			else
			{
				bIncludeProjectCultures = true;
			}

			{
				FString AssetLocalizationRoot;
				if (FPackageLocalizationUtil::GetLocalizedRoot(AssetPath, FString(), AssetLocalizationRoot))
				{
					FString AssetLocalizationFileRoot;
					if (FPackageName::TryConvertLongPackageNameToFilename(AssetLocalizationRoot, AssetLocalizationFileRoot))
					{
						TArray<FString> CulturePaths;
						CulturePaths.Add(MoveTemp(AssetLocalizationFileRoot));
						CultureNames.Append(TextLocalizationResourceUtil::GetLocalizedCultureNames(CulturePaths));
					}
				}
			}
		}

		ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::None;
		if (bIncludeEngineCultures)
		{
			LocLoadFlags |= ELocalizationLoadFlags::Engine;
		}
		if (bIncludeProjectCultures)
		{
			LocLoadFlags |= ELocalizationLoadFlags::Game;
		}
		CultureNames.Append(FTextLocalizationManager::Get().GetLocalizedCultureNames(LocLoadFlags));

		CurrentCultures = FInternationalization::Get().GetAvailableCultures(CultureNames.Array(), false);
		if (CurrentCultures.Num() == 0)
		{
			CurrentCultures.Add(FInternationalization::Get().GetCurrentCulture());
		}
	}

	// Sort by display name for the UI
	CurrentCultures.Sort([](const FCultureRef& FirstCulture, const FCultureRef& SecondCulture) -> bool
	{
		const FText FirstDisplayName = FText::FromString(FirstCulture->GetDisplayName());
		const FText SecondDisplayName = FText::FromString(SecondCulture->GetDisplayName());
		return FirstDisplayName.CompareTo(SecondDisplayName) < 0;
	});

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	// Now build up the list of available localized or source assets based upon the current selection and current cultures
	FSourceAssetsState SourceAssetsState;
	TArray<FLocalizedAssetsState> LocalizedAssetsState;
	for (const FCultureRef& CurrentCulture : CurrentCultures)
	{
		FLocalizedAssetsState& LocalizedAssetsStateForCulture = LocalizedAssetsState[LocalizedAssetsState.AddDefaulted()];
		LocalizedAssetsStateForCulture.Culture = CurrentCulture;

		for (const FAssetData& Asset : SelectedAssets)
		{
			// Can this type of asset be localized?
			bool bCanLocalizeAsset = false;
			{
				TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(Asset.GetClass()).Pin();
				if (AssetTypeActions.IsValid())
				{
					bCanLocalizeAsset = AssetTypeActions->CanLocalize();
				}
			}

			if (!bCanLocalizeAsset)
			{
				continue;
			}

			const FString ObjectPath = Asset.ObjectPath.ToString();
			if (FPackageName::IsLocalizedPackage(ObjectPath))
			{
				// Get the source path for this asset
				FString SourceObjectPath;
				if (FPackageLocalizationUtil::ConvertLocalizedToSource(ObjectPath, SourceObjectPath))
				{
					SourceAssetsState.CurrentAssets.Add(*SourceObjectPath);
				}
			}
			else
			{
				SourceAssetsState.SelectedAssets.Add(Asset.ObjectPath);

				// Get the localized path for this asset and culture
				FString LocalizedObjectPath;
				if (FPackageLocalizationUtil::ConvertSourceToLocalized(ObjectPath, CurrentCulture->GetName(), LocalizedObjectPath))
				{
					// Does this localized asset already exist?
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
					FAssetData LocalizedAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*LocalizedObjectPath);

					if (LocalizedAssetData.IsValid())
					{
						LocalizedAssetsStateForCulture.CurrentAssets.Add(*LocalizedObjectPath);
					}
					else
					{
						LocalizedAssetsStateForCulture.NewAssets.Add(*LocalizedObjectPath);
					}
				}
			}
		}
	}

#if USE_STABLE_LOCALIZATION_KEYS
	// Add the Localization ID options
	{
		FToolMenuSection& Section = Menu->AddSection("LocalizationId", LOCTEXT("LocalizationIdHeading", "Localization ID"));
		{
			// Show the localization ID if we have a single asset selected
			if (SelectedAssets.Num() == 1)
			{
				const FString LocalizationId = TextNamespaceUtil::GetPackageNamespace(SelectedAssets[0].GetAsset());
				Section.AddMenuEntry(
					"CopyLocalizationId",
					FText::Format(LOCTEXT("CopyLocalizationIdFmt", "ID: {0}"), LocalizationId.IsEmpty() ? LOCTEXT("EmptyLocalizationId", "None") : FText::FromString(LocalizationId)),
					LOCTEXT("CopyLocalizationIdTooltip", "Copy the localization ID to the clipboard."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCopyTextToClipboard, LocalizationId))
					);
			}

			// Always show the reset localization ID option
			Section.AddMenuEntry(
				"ResetLocalizationId",
				LOCTEXT("ResetLocalizationId", "Reset Localization ID"),
				LOCTEXT("ResetLocalizationIdTooltip", "Reset the localization ID. Note: This will re-key all the text within this asset."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteResetLocalizationId))
				);
		}
	}
#endif // USE_STABLE_LOCALIZATION_KEYS

	// Add the localization cache options
	if (SelectedAssets.Num() == 1)
	{
		FString PackageFilename;
		if (FPackageName::DoesPackageExist(SelectedAssets[0].PackageName.ToString(), nullptr, &PackageFilename))
		{
			FToolMenuSection& Section = Menu->AddSection("LocalizationCache", LOCTEXT("LocalizationCacheHeading", "Localization Cache"));
			{
				// Always show the reset localization ID option
				Section.AddMenuEntry(
					"ShowLocalizationCache",
					LOCTEXT("ShowLocalizationCache", "Show Localization Cache"),
					LOCTEXT("ShowLocalizationCacheTooltip", "Show the cached list of localized texts stored in the package header."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteShowLocalizationCache, PackageFilename))
					);
			}
		}
	}

	// If we found source assets for localized assets, then we can show the Source Asset options
	if (SourceAssetsState.CurrentAssets.Num() > 0)
	{
		FToolMenuSection& Section = Menu->AddSection("ManageSourceAsset", LOCTEXT("ManageSourceAssetHeading", "Manage Source Asset"));
		{
			Section.AddMenuEntry(
				"ShowSourceAsset",
				LOCTEXT("ShowSourceAsset", "Show Source Asset"),
				LOCTEXT("ShowSourceAssetTooltip", "Show the source asset in the Content Browser."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindInAssetTree, SourceAssetsState.CurrentAssets.Array()))
				);

			Section.AddMenuEntry(
				"EditSourceAsset",
				LOCTEXT("EditSourceAsset", "Edit Source Asset"),
				LOCTEXT("EditSourceAssetTooltip", "Edit the source asset."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Edit"),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteOpenEditorsForAssets, SourceAssetsState.CurrentAssets.Array()))
				);
		}
	}

	// If we currently have source assets selected, then we can show the Localized Asset options
	if (SourceAssetsState.SelectedAssets.Num() > 0)
	{
		FToolMenuSection& Section = Menu->AddSection("ManageLocalizedAsset", LOCTEXT("ManageLocalizedAssetHeading", "Manage Localized Asset"));
		{
			Section.AddSubMenu(
				"CreateLocalizedAsset",
				LOCTEXT("CreateLocalizedAsset", "Create Localized Asset"),
				LOCTEXT("CreateLocalizedAssetTooltip", "Create a new localized asset."),
				FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::MakeCreateLocalizedAssetSubMenu, SourceAssetsState.SelectedAssets, LocalizedAssetsState),
				FUIAction(),
				EUserInterfaceActionType::Button,
				false,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Duplicate")
				);

			int32 NumLocalizedAssets = 0;
			for (const FLocalizedAssetsState& LocalizedAssetsStateForCulture : LocalizedAssetsState)
			{
				NumLocalizedAssets += LocalizedAssetsStateForCulture.CurrentAssets.Num();
			}

			if (NumLocalizedAssets > 0)
			{
				Section.AddSubMenu(
					"ShowLocalizedAsset",
					LOCTEXT("ShowLocalizedAsset", "Show Localized Asset"),
					LOCTEXT("ShowLocalizedAssetTooltip", "Show the localized asset in the Content Browser."),
					FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::MakeShowLocalizedAssetSubMenu, LocalizedAssetsState),
					FUIAction(),
					EUserInterfaceActionType::Button,
					false,
					FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser")
					);

				Section.AddSubMenu(
					"EditLocalizedAsset",
					LOCTEXT("EditLocalizedAsset", "Edit Localized Asset"),
					LOCTEXT("EditLocalizedAssetTooltip", "Edit the localized asset."),
					FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::MakeEditLocalizedAssetSubMenu, LocalizedAssetsState),
					FUIAction(),
					EUserInterfaceActionType::Button,
					false,
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Edit")
					);
			}
		}
	}
}

void FAssetContextMenu::MakeCreateLocalizedAssetSubMenu(UToolMenu* Menu, TSet<FName> InSelectedSourceAssets, TArray<FLocalizedAssetsState> InLocalizedAssetsState)
{
	FToolMenuSection& Section = Menu->AddSection("Section");

	for (const FLocalizedAssetsState& LocalizedAssetsStateForCulture : InLocalizedAssetsState)
	{
		// If we have less localized assets than we have selected source assets, then we'll have some assets to create localized variants of
		if (LocalizedAssetsStateForCulture.CurrentAssets.Num() < InSelectedSourceAssets.Num())
		{
			Section.AddMenuEntry(
				NAME_None,
				FText::FromString(LocalizedAssetsStateForCulture.Culture->GetDisplayName()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCreateLocalizedAsset, InSelectedSourceAssets, LocalizedAssetsStateForCulture))
				);
		}
	}
}

void FAssetContextMenu::MakeShowLocalizedAssetSubMenu(UToolMenu* Menu, TArray<FLocalizedAssetsState> InLocalizedAssetsState)
{
	FToolMenuSection& Section = Menu->AddSection("Section");

	for (const FLocalizedAssetsState& LocalizedAssetsStateForCulture : InLocalizedAssetsState)
	{
		if (LocalizedAssetsStateForCulture.CurrentAssets.Num() > 0)
		{
			Section.AddMenuEntry(
				NAME_None,
				FText::FromString(LocalizedAssetsStateForCulture.Culture->GetDisplayName()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindInAssetTree, LocalizedAssetsStateForCulture.CurrentAssets.Array()))
				);
		}
	}
}

void FAssetContextMenu::MakeEditLocalizedAssetSubMenu(UToolMenu* Menu, TArray<FLocalizedAssetsState> InLocalizedAssetsState)
{
	FToolMenuSection& Section = Menu->AddSection("Section");

	for (const FLocalizedAssetsState& LocalizedAssetsStateForCulture : InLocalizedAssetsState)
	{
		if (LocalizedAssetsStateForCulture.CurrentAssets.Num() > 0)
		{
			Section.AddMenuEntry(
				NAME_None,
				FText::FromString(LocalizedAssetsStateForCulture.Culture->GetDisplayName()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteOpenEditorsForAssets, LocalizedAssetsStateForCulture.CurrentAssets.Array()))
				);
		}
	}
}

void FAssetContextMenu::ExecuteCreateLocalizedAsset(TSet<FName> InSelectedSourceAssets, FLocalizedAssetsState InLocalizedAssetsStateForCulture)
{
	TArray<UPackage*> PackagesToSave;
	TArray<FAssetData> NewObjects;

	for (const FName SourceAssetName : InSelectedSourceAssets)
	{
		if (InLocalizedAssetsStateForCulture.CurrentAssets.Contains(SourceAssetName))
		{
			// Asset is already localized
			continue;
		}

		UObject* SourceAssetObject = LoadObject<UObject>(nullptr, *SourceAssetName.ToString());
		if (!SourceAssetObject)
		{
			// Source object cannot be loaded
			continue;
		}

		FString LocalizedPackageName;
		if (!FPackageLocalizationUtil::ConvertSourceToLocalized(SourceAssetObject->GetOutermost()->GetPathName(), InLocalizedAssetsStateForCulture.Culture->GetName(), LocalizedPackageName))
		{
			continue;
		}

		ObjectTools::FPackageGroupName NewAssetName;
		NewAssetName.PackageName = LocalizedPackageName;
		NewAssetName.ObjectName = SourceAssetObject->GetName();

		TSet<UPackage*> PackagesNotDuplicated;
		UObject* NewObject = ObjectTools::DuplicateSingleObject(SourceAssetObject, NewAssetName, PackagesNotDuplicated);
		if (NewObject)
		{
			PackagesToSave.Add(NewObject->GetOutermost());
			NewObjects.Add(FAssetData(NewObject));
		}
	}

	if (PackagesToSave.Num() > 0)
	{
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty*/false, /*bPromptToSave*/false);
	}

	OnFindInAssetTreeRequested.ExecuteIfBound(NewObjects);
}

void FAssetContextMenu::ExecuteFindInAssetTree(TArray<FName> InAssets)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter ARFilter;
	ARFilter.ObjectPaths = MoveTemp(InAssets);
	
	TArray<FAssetData> FoundLocalizedAssetData;
	AssetRegistryModule.Get().GetAssets(ARFilter, FoundLocalizedAssetData);

	OnFindInAssetTreeRequested.ExecuteIfBound(FoundLocalizedAssetData);
}

void FAssetContextMenu::ExecuteOpenEditorsForAssets(TArray<FName> InAssets)
{
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorsForAssets(InAssets);
}

bool FAssetContextMenu::AddReferenceMenuOptions(UToolMenu* Menu)
{
	UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();

	{
		FToolMenuSection& Section = Menu->AddSection("AssetContextReferences", LOCTEXT("ReferencesMenuHeading", "References"));

		Section.AddMenuEntry(
			"CopyReference",
			LOCTEXT("CopyReference", "Copy Reference"),
			LOCTEXT("CopyReferenceTooltip", "Copies reference paths for the selected assets to the clipboard."),
			FSlateIcon(),
			FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteCopyReference ) )
			);
	
		if (Context->bCanBeModified)
		{
			Section.AddMenuEntry(
				"CopyFilePath",
				LOCTEXT("CopyFilePath", "Copy File Path"),
				LOCTEXT("CopyFilePathTooltip", "Copies the file paths on disk for the selected assets to the clipboard."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCopyFilePath))
			);
		}
	}

	return true;
}

bool FAssetContextMenu::AddDocumentationMenuOptions(UToolMenu* Menu)
{
	bool bAddedOption = false;

	// Objects must be loaded for this operation... for now
	UClass* SelectedClass = (SelectedAssets.Num() > 0 ? SelectedAssets[0].GetClass() : nullptr);
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (SelectedClass != AssetData.GetClass())
		{
			SelectedClass = nullptr;
			break;
		}
	}

	// Go to C++ Code
	if( SelectedClass != nullptr )
	{
		// Blueprints are special.  We won't link to C++ and for documentation we'll use the class it is generated from
		const bool bIsBlueprint = SelectedClass->IsChildOf<UBlueprint>();
		if (bIsBlueprint)
		{
			const FString ParentClassPath = SelectedAssets[0].GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UBlueprint,ParentClass));
			if (!ParentClassPath.IsEmpty())
			{
				SelectedClass = FindObject<UClass>(nullptr,*ParentClassPath);
			}
		}

		if ( !bIsBlueprint && FSourceCodeNavigation::IsCompilerAvailable() )
		{
			FString ClassHeaderPath;
			if( FSourceCodeNavigation::FindClassHeaderPath( SelectedClass, ClassHeaderPath ) && IFileManager::Get().FileSize( *ClassHeaderPath ) != INDEX_NONE )
			{
				bAddedOption = true;

				const FString CodeFileName = FPaths::GetCleanFilename( *ClassHeaderPath );

				FToolMenuSection& Section = Menu->AddSection( "AssetCode"/*, LOCTEXT("AssetCodeHeading", "C++")*/ );
				{
					Section.AddMenuEntry(
						"GoToCodeForAsset",
						FText::Format( LOCTEXT("GoToCodeForAsset", "Open {0}"), FText::FromString( CodeFileName ) ),
						FText::Format( LOCTEXT("GoToCodeForAsset_ToolTip", "Opens the header file for this asset ({0}) in a code editing program"), FText::FromString( CodeFileName ) ),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.GoToCodeForAsset"),
						FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToCodeForAsset, SelectedClass ) )
						);
				}
			}
		}

		const FString DocumentationLink = FEditorClassUtils::GetDocumentationLink(SelectedClass);
		if (bIsBlueprint || !DocumentationLink.IsEmpty())
		{
			bAddedOption = true;

			FToolMenuSection& Section = Menu->AddSection( "AssetDocumentation"/*, LOCTEXT("AseetDocsHeading", "Documentation")*/ );
			{
					if (bIsBlueprint)
					{
						if (!DocumentationLink.IsEmpty())
						{
							Section.AddMenuEntry(
								"GoToDocsForAssetWithClass",
								FText::Format( LOCTEXT("GoToDocsForAssetWithClass", "View Documentation - {0}"), SelectedClass->GetDisplayNameText() ),
								FText::Format( LOCTEXT("GoToDocsForAssetWithClass_ToolTip", "Click to open documentation for {0}"), SelectedClass->GetDisplayNameText() ),
								FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
								FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, SelectedClass ) )
								);
						}

						UEnum* BlueprintTypeEnum = StaticEnum<EBlueprintType>();
						const FString EnumString = SelectedAssets[0].GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UBlueprint,BlueprintType));
						EBlueprintType BlueprintType = (!EnumString.IsEmpty() ? (EBlueprintType)BlueprintTypeEnum->GetValueByName(*EnumString) : BPTYPE_Normal);

						switch (BlueprintType)
						{
						case BPTYPE_FunctionLibrary:
							Section.AddMenuEntry(
								"GoToDocsForMacroBlueprint",
								LOCTEXT("GoToDocsForMacroBlueprint", "View Documentation - Function Library"),
								LOCTEXT("GoToDocsForMacroBlueprint_ToolTip", "Click to open documentation on blueprint function libraries"),
								FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
								FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, UBlueprint::StaticClass(), FString(TEXT("UBlueprint_FunctionLibrary")) ) )
								);
							break;
						case BPTYPE_Interface:
							Section.AddMenuEntry(
								"GoToDocsForInterfaceBlueprint",
								LOCTEXT("GoToDocsForInterfaceBlueprint", "View Documentation - Interface"),
								LOCTEXT("GoToDocsForInterfaceBlueprint_ToolTip", "Click to open documentation on blueprint interfaces"),
								FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
								FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, UBlueprint::StaticClass(), FString(TEXT("UBlueprint_Interface")) ) )
								);
							break;
						case BPTYPE_MacroLibrary:
							Section.AddMenuEntry(
								"GoToDocsForMacroLibrary",
								LOCTEXT("GoToDocsForMacroLibrary", "View Documentation - Macro"),
								LOCTEXT("GoToDocsForMacroLibrary_ToolTip", "Click to open documentation on blueprint macros"),
								FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
								FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, UBlueprint::StaticClass(), FString(TEXT("UBlueprint_Macro")) ) )
								);
							break;
						default:
							Section.AddMenuEntry(
								"GoToDocsForBlueprint",
								LOCTEXT("GoToDocsForBlueprint", "View Documentation - Blueprint"),
								LOCTEXT("GoToDocsForBlueprint_ToolTip", "Click to open documentation on blueprints"),
								FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
								FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, UBlueprint::StaticClass(), FString(TEXT("UBlueprint")) ) )
								);
						}
					}
					else
					{
						Section.AddMenuEntry(
							"GoToDocsForAsset",
							LOCTEXT("GoToDocsForAsset", "View Documentation"),
							LOCTEXT("GoToDocsForAsset_ToolTip", "Click to open documentation"),
							FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered" ),
							FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteGoToDocsForAsset, SelectedClass ) )
							);
					}
			}
		}
	}

	return bAddedOption;
}

bool FAssetContextMenu::AddAssetTypeMenuOptions(UToolMenu* Menu, bool bHasObjectsSelected)
{
	bool bAnyTypeOptions = false;

	if (bHasObjectsSelected)
	{
		// Label "GetAssetActions" section
		UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
		if (Context)
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			if (Context->CommonAssetTypeActions.IsValid())
			{
				Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), Context->CommonAssetTypeActions.Pin()->GetName());
			}
			else if (Context->CommonClass)
			{
				Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), FText::FromName(Context->CommonClass->GetFName()));
			}
			else
			{
				Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), FText::FromString(TEXT("Asset")));
			}

			bAnyTypeOptions = true;
		}
	}

	return bAnyTypeOptions;
}

bool FAssetContextMenu::AddSourceControlMenuOptions(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->AddSection("AssetContextSourceControl");
	
	if ( ISourceControlModule::Get().IsEnabled() )
	{
		// SCC sub menu
		Section.AddSubMenu(
			"SourceControlSubMenu",
			LOCTEXT("SourceControlSubMenuLabel", "Source Control"),
			LOCTEXT("SourceControlSubMenuToolTip", "Source control actions."),
			FNewToolMenuDelegate::CreateSP(this, &FAssetContextMenu::FillSourceControlSubMenu),
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSourceControlActions )
				),
			EUserInterfaceActionType::Button,
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.StatusIcon.On")
			);
	}
	else
	{
		Section.AddMenuEntry(
			"SCCConnectToSourceControl",
			LOCTEXT("SCCConnectToSourceControl", "Connect To Source Control..."),
			LOCTEXT("SCCConnectToSourceControlTooltip", "Connect to source control to allow source control operations to be performed on content and levels."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Connect"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteEnableSourceControl ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSourceControlActions )
				)
			);
	}

	// Diff selected
	if (CanExecuteDiffSelected())
	{
		Section.AddMenuEntry(
			"DiffSelected",
			LOCTEXT("DiffSelected", "Diff Selected"),
			LOCTEXT("DiffSelectedTooltip", "Diff the two assets that you have selected."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Diff"),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteDiffSelected)
			)
		);
	}

	return true;
}

void FAssetContextMenu::FillSourceControlSubMenu(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->AddSection("AssetSourceControlActions", LOCTEXT("AssetSourceControlActionsMenuHeading", "Source Control"));

	if( CanExecuteSCCMerge() )
	{
		Section.AddMenuEntry(
			"SCCMerge",
			LOCTEXT("SCCMerge", "Merge"),
			LOCTEXT("SCCMergeTooltip", "Opens the blueprint editor with the merge tool open."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteSCCMerge),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteSCCMerge)
			)
		);
	}

	if( CanExecuteSCCSync() )
	{
		Section.AddMenuEntry(
			"SCCSync",
			LOCTEXT("SCCSync", "Sync"),
			LOCTEXT("SCCSyncTooltip", "Updates the item to the latest version in source control."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Sync"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCSync ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCSync )
			)
		);
	}

	if ( CanExecuteSCCCheckOut() )
	{
		Section.AddMenuEntry(
			"SCCCheckOut",
			LOCTEXT("SCCCheckOut", "Check Out"),
			LOCTEXT("SCCCheckOutTooltip", "Checks out the selected asset from source control."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.CheckOut"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCCheckOut ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCCheckOut )
			)
		);
	}

	if ( CanExecuteSCCOpenForAdd() )
	{
		Section.AddMenuEntry(
			"SCCOpenForAdd",
			LOCTEXT("SCCOpenForAdd", "Mark For Add"),
			LOCTEXT("SCCOpenForAddTooltip", "Adds the selected asset to source control."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Add"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCOpenForAdd ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCOpenForAdd )
			)
		);
	}

	if ( CanExecuteSCCCheckIn() )
	{
		Section.AddMenuEntry(
			"SCCCheckIn",
			LOCTEXT("SCCCheckIn", "Check In"),
			LOCTEXT("SCCCheckInTooltip", "Checks in the selected asset to source control."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Submit"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCCheckIn ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCCheckIn )
			)
		);
	}

	Section.AddMenuEntry(
		"SCCRefresh",
		LOCTEXT("SCCRefresh", "Refresh"),
		LOCTEXT("SCCRefreshTooltip", "Updates the source control status of the asset."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Refresh"),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCRefresh ),
			FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCRefresh )
			)
		);

	if( CanExecuteSCCHistory() )
	{
		Section.AddMenuEntry(
			"SCCHistory",
			LOCTEXT("SCCHistory", "History"),
			LOCTEXT("SCCHistoryTooltip", "Displays the source control revision history of the selected asset."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.History"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCHistory ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCHistory )
			)
		);

		Section.AddMenuEntry(
			"SCCDiffAgainstDepot",
			LOCTEXT("SCCDiffAgainstDepot", "Diff Against Depot"),
			LOCTEXT("SCCDiffAgainstDepotTooltip", "Look at differences between your version of the asset and that in source control."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Diff"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCDiffAgainstDepot ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCDiffAgainstDepot )
			)
		);	
	}

	if( CanExecuteSCCRevert() )
	{
		Section.AddMenuEntry(
			"SCCRevert",
			LOCTEXT("SCCRevert", "Revert"),
			LOCTEXT("SCCRevertTooltip", "Reverts the asset to the state it was before it was checked out."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Revert"),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteSCCRevert ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteSCCRevert )
			)
		);
	}
}

bool FAssetContextMenu::CanExecuteSourceControlActions() const
{
	return !bAtLeastOneClassSelected;
}

bool FAssetContextMenu::AddCollectionMenuOptions(UToolMenu* Menu)
{
	class FManageCollectionsContextMenu
	{
	public:
		static void CreateManageCollectionsSubMenu(UToolMenu* SubMenu, TSharedRef<FCollectionAssetManagement> QuickAssetManagement)
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			TArray<FCollectionNameType> AvailableCollections;
			CollectionManagerModule.Get().GetRootCollections(AvailableCollections);

			CreateManageCollectionsSubMenu(SubMenu, QuickAssetManagement, MoveTemp(AvailableCollections));
		}

		static void CreateManageCollectionsSubMenu(UToolMenu* SubMenu, TSharedRef<FCollectionAssetManagement> QuickAssetManagement, TArray<FCollectionNameType> AvailableCollections)
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			AvailableCollections.Sort([](const FCollectionNameType& One, const FCollectionNameType& Two) -> bool
			{
				return One.Name.LexicalLess(Two.Name);
			});

			FToolMenuSection& Section = SubMenu->AddSection("Section");
			for (const FCollectionNameType& AvailableCollection : AvailableCollections)
			{
				// Never display system collections
				if (AvailableCollection.Type == ECollectionShareType::CST_System)
				{
					continue;
				}

				// Can only manage assets for static collections
				ECollectionStorageMode::Type StorageMode = ECollectionStorageMode::Static;
				CollectionManagerModule.Get().GetCollectionStorageMode(AvailableCollection.Name, AvailableCollection.Type, StorageMode);
				if (StorageMode != ECollectionStorageMode::Static)
				{
					continue;
				}

				TArray<FCollectionNameType> AvailableChildCollections;
				CollectionManagerModule.Get().GetChildCollections(AvailableCollection.Name, AvailableCollection.Type, AvailableChildCollections);

				if (AvailableChildCollections.Num() > 0)
				{
					Section.AddSubMenu(
						NAME_None,
						FText::FromName(AvailableCollection.Name), 
						FText::GetEmpty(), 
						FNewToolMenuDelegate::CreateStatic(&FManageCollectionsContextMenu::CreateManageCollectionsSubMenu, QuickAssetManagement, AvailableChildCollections),
						FUIAction(
							FExecuteAction::CreateStatic(&FManageCollectionsContextMenu::OnCollectionClicked, QuickAssetManagement, AvailableCollection),
							FCanExecuteAction::CreateStatic(&FManageCollectionsContextMenu::IsCollectionEnabled, QuickAssetManagement, AvailableCollection),
							FGetActionCheckState::CreateStatic(&FManageCollectionsContextMenu::GetCollectionCheckState, QuickAssetManagement, AvailableCollection)
							), 
						EUserInterfaceActionType::ToggleButton,
						false,
						FSlateIcon(FEditorStyle::GetStyleSetName(), ECollectionShareType::GetIconStyleName(AvailableCollection.Type))
						);
				}
				else
				{
					Section.AddMenuEntry(
						NAME_None,
						FText::FromName(AvailableCollection.Name), 
						FText::GetEmpty(), 
						FSlateIcon(FEditorStyle::GetStyleSetName(), ECollectionShareType::GetIconStyleName(AvailableCollection.Type)), 
						FUIAction(
							FExecuteAction::CreateStatic(&FManageCollectionsContextMenu::OnCollectionClicked, QuickAssetManagement, AvailableCollection),
							FCanExecuteAction::CreateStatic(&FManageCollectionsContextMenu::IsCollectionEnabled, QuickAssetManagement, AvailableCollection),
							FGetActionCheckState::CreateStatic(&FManageCollectionsContextMenu::GetCollectionCheckState, QuickAssetManagement, AvailableCollection)
							), 
						EUserInterfaceActionType::ToggleButton
						);
				}
			}
		}

	private:
		static bool IsCollectionEnabled(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			return QuickAssetManagement->IsCollectionEnabled(InCollectionKey);
		}

		static ECheckBoxState GetCollectionCheckState(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			return QuickAssetManagement->GetCollectionCheckState(InCollectionKey);
		}

		static void OnCollectionClicked(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			// The UI actions don't give you the new check state, so we need to emulate the behavior of SCheckBox
			// Basically, checked will transition to unchecked (removing items), and anything else will transition to checked (adding items)
			if (GetCollectionCheckState(QuickAssetManagement, InCollectionKey) == ECheckBoxState::Checked)
			{
				QuickAssetManagement->RemoveCurrentAssetsFromCollection(InCollectionKey);
			}
			else
			{
				QuickAssetManagement->AddCurrentAssetsToCollection(InCollectionKey);
			}
		}
	};

	bool bHasAddedItems = false;

	FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

	FToolMenuSection& Section = Menu->AddSection("AssetContextCollections", LOCTEXT("AssetCollectionOptionsMenuHeading", "Collections"));

	// Show a sub-menu that allows you to quickly add or remove the current asset selection from the available collections
	if (CollectionManagerModule.Get().HasCollections())
	{
		TSharedRef<FCollectionAssetManagement> QuickAssetManagement = MakeShareable(new FCollectionAssetManagement());
		QuickAssetManagement->SetCurrentAssets(SelectedAssets);

		Section.AddSubMenu(
			"ManageCollections",
			LOCTEXT("ManageCollections", "Manage Collections"),
			LOCTEXT("ManageCollections_ToolTip", "Manage the collections that the selected asset(s) belong to."),
			FNewToolMenuDelegate::CreateStatic(&FManageCollectionsContextMenu::CreateManageCollectionsSubMenu, QuickAssetManagement)
			);

		bHasAddedItems = true;
	}

	// "Remove from collection" (only display option if exactly one collection is selected)
	if ( SourcesData.Collections.Num() == 1 && !SourcesData.IsDynamicCollection() )
	{
		Section.AddMenuEntry(
			"RemoveFromCollection",
			FText::Format(LOCTEXT("RemoveFromCollectionFmt", "Remove From {0}"), FText::FromName(SourcesData.Collections[0].Name)),
			LOCTEXT("RemoveFromCollection_ToolTip", "Removes the selected asset from the current collection."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteRemoveFromCollection ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteRemoveFromCollection )
				)
			);

		bHasAddedItems = true;
	}


	return bHasAddedItems;
}

bool FAssetContextMenu::AreImportedAssetActionsVisible() const
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	
	// Check that all of the selected assets are imported
	for (auto& SelectedAsset : SelectedAssets)
	{
		auto AssetClass = SelectedAsset.GetClass();
		if (AssetClass)
		{
			auto AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetClass).Pin();
			if (!AssetTypeActions.IsValid() || !AssetTypeActions->IsImportedAsset())
			{
				return false;
			}
		}
	}

	return true;
}

bool FAssetContextMenu::CanExecuteImportedAssetActions(const TArray<FString> ResolvedFilePaths) const
{
	if (ResolvedFilePaths.Num() == 0)
	{
		return false;
	}

	// Verify that all the file paths are legitimate
	for (const auto& SourceFilePath : ResolvedFilePaths)
	{
		if (!SourceFilePath.Len() || IFileManager::Get().FileSize(*SourceFilePath) == INDEX_NONE)
		{
			return false;
		}
	}

	return true;
}

bool FAssetContextMenu::CanExecuteReimportAssetActions(const TArray<FString> ResolvedFilePaths) const
{
	if (ResolvedFilePaths.Num() == 0)
	{
		return false;
	}

	// Verify that all the file paths are non-empty
	for (const auto& SourceFilePath : ResolvedFilePaths)
	{
		if (!SourceFilePath.Len())
		{
			return false;
		}
	}

	return true;
}

void FAssetContextMenu::ExecuteReimport(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	// Reimport all selected assets
	TArray<UObject *> CopyOfSelectedAssets;
	for (const FAssetData &SelectedAsset : SelectedAssets)
	{
		UObject *Asset = SelectedAsset.GetAsset();
		CopyOfSelectedAssets.Add(Asset);
	}
	FReimportManager::Instance()->ValidateAllSourceFileAndReimport(CopyOfSelectedAssets, true, SourceFileIndex, false);
}

void FAssetContextMenu::ExecuteReimportWithNewFile(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	// Ask for a new files and reimport the selected asset
	check(SelectedAssets.Num() == 1);

	TArray<UObject *> CopyOfSelectedAssets;
	for (const FAssetData &SelectedAsset : SelectedAssets)
	{
		UObject *Asset = SelectedAsset.GetAsset();
		CopyOfSelectedAssets.Add(Asset);
	}

	TArray<FString> AssetSourcePaths;
	UClass* ObjectClass = CopyOfSelectedAssets[0]->GetClass();
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const auto AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(ObjectClass);
	if (AssetTypeActions.IsValid())
	{
		AssetTypeActions.Pin()->GetResolvedSourceFilePaths(CopyOfSelectedAssets, AssetSourcePaths);
	}

	int32 SourceFileIndexToReplace = SourceFileIndex;
	//Check if the data is valid
	if (SourceFileIndexToReplace == INDEX_NONE)
	{
		// Ask for a new file for the index 0
		// TODO(?) need to do anything for multiple source paths here?
		// UDIM textures will have multiple source paths for example, but they come through this path
		SourceFileIndexToReplace = 0;
	}
	check(SourceFileIndexToReplace >= 0);
	check(AssetSourcePaths.IsValidIndex(SourceFileIndexToReplace));

	FReimportManager::Instance()->ValidateAllSourceFileAndReimport(CopyOfSelectedAssets, true, SourceFileIndexToReplace, true);
}

void FAssetContextMenu::ExecuteFindSourceInExplorer(const TArray<FString> ResolvedFilePaths)
{
	// Open all files in the explorer
	for (const auto& SourceFilePath : ResolvedFilePaths)
	{
		FPlatformProcess::ExploreFolder(*FPaths::GetPath(SourceFilePath));
	}
}

void FAssetContextMenu::ExecuteOpenInExternalEditor(const TArray<FString> ResolvedFilePaths)
{
	// Open all files in their respective editor
	for (const auto& SourceFilePath : ResolvedFilePaths)
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*SourceFilePath, NULL, ELaunchVerb::Edit);
	}
}

void FAssetContextMenu::GetSelectedAssetsByClass(TMap<UClass*, TArray<UObject*> >& OutSelectedAssetsByClass) const
{
	// Sort all selected assets by class
	for (const auto& SelectedAsset : SelectedAssets)
	{
		auto Asset = SelectedAsset.GetAsset();
		auto AssetClass = Asset->GetClass();

		if ( !OutSelectedAssetsByClass.Contains(AssetClass) )
		{
			OutSelectedAssetsByClass.Add(AssetClass);
		}
		
		OutSelectedAssetsByClass[AssetClass].Add(Asset);
	}
}

void FAssetContextMenu::GetSelectedAssetSourceFilePaths(TArray<FString>& OutFilePaths, TArray<FString>& OutUniqueSourceFileLabels, int32 &OutValidSelectedAssetCount) const
{
	OutFilePaths.Empty();
	OutUniqueSourceFileLabels.Empty();
	TMap<UClass*, TArray<UObject*> > SelectedAssetsByClass;
	GetSelectedAssetsByClass(SelectedAssetsByClass);
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	OutValidSelectedAssetCount = 0;
	// Get the source file paths for the assets of each type
	for (const auto& AssetsByClassPair : SelectedAssetsByClass)
	{
		const auto AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetsByClassPair.Key);
		if (AssetTypeActions.IsValid())
		{
			const auto& TypeAssets = AssetsByClassPair.Value;
			OutValidSelectedAssetCount += TypeAssets.Num();
			TArray<FString> AssetSourcePaths;
			AssetTypeActions.Pin()->GetResolvedSourceFilePaths(TypeAssets, AssetSourcePaths);
			OutFilePaths.Append(AssetSourcePaths);

			TArray<FString> AssetSourceLabels;
			AssetTypeActions.Pin()->GetSourceFileLabels(TypeAssets, AssetSourceLabels);
			for (const FString& Label : AssetSourceLabels)
			{
				OutUniqueSourceFileLabels.AddUnique(Label);
			}
		}
	}
}

void FAssetContextMenu::ExecuteSyncToAssetTree()
{
	// Copy this as the sync may adjust our selected assets array
	const TArray<FAssetData> SelectedAssetsCopy = SelectedAssets;
	OnFindInAssetTreeRequested.ExecuteIfBound(SelectedAssetsCopy);
}

void FAssetContextMenu::ExecuteFindInExplorer()
{
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		const UObject* Asset = SelectedAssets[AssetIdx].GetAsset();
		if (Asset)
		{
			FAssetData AssetData(Asset);

			const FString PackageName = AssetData.PackageName.ToString();

			static const TCHAR* ScriptString = TEXT("/Script/");
			if (PackageName.StartsWith(ScriptString))
			{
				// Handle C++ classes specially, as FPackageName::LongPackageNameToFilename won't return the correct path in this case
				const FString ModuleName = PackageName.RightChop(FCString::Strlen(ScriptString));
				FString ModulePath;
				if (FSourceCodeNavigation::FindModulePath(ModuleName, ModulePath))
				{
					FString RelativePath;
					if (AssetData.GetTagValue("ModuleRelativePath", RelativePath))
					{
						const FString FullFilePath = FPaths::ConvertRelativePathToFull(ModulePath / (*RelativePath));
						FPlatformProcess::ExploreFolder(*FullFilePath);
					}
				}

				return;
			}

			const bool bIsWorldAsset = (AssetData.AssetClass == UWorld::StaticClass()->GetFName());
			const FString Extension = bIsWorldAsset ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
			const FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, Extension);
			const FString FullFilePath = FPaths::ConvertRelativePathToFull(FilePath);
			FPlatformProcess::ExploreFolder(*FullFilePath);
		}
	}
}

void FAssetContextMenu::ExecuteCreateBlueprintUsing()
{
	if(SelectedAssets.Num() == 1)
	{
		UObject* Asset = SelectedAssets[0].GetAsset();
		FKismetEditorUtilities::CreateBlueprintUsingAsset(Asset, true);
	}
}

void FAssetContextMenu::GetSelectedAssets(TArray<UObject*>& Assets, bool SkipRedirectors) const
{
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		if (SkipRedirectors && (SelectedAssets[AssetIdx].AssetClass == UObjectRedirector::StaticClass()->GetFName()))
		{
			// Don't operate on Redirectors
			continue;
		}

		UObject* Object = SelectedAssets[AssetIdx].GetAsset();

		if (Object)
		{
			Assets.Add(Object);
		}
	}
}

/** Generates a reference graph of the world and can then find actors referencing specified objects */
struct WorldReferenceGenerator : public FFindReferencedAssets
{
	void BuildReferencingData()
	{
		MarkAllObjects();

		const int32 MaxRecursionDepth = 0;
		const bool bIncludeClasses = true;
		const bool bIncludeDefaults = false;
		const bool bReverseReferenceGraph = true;


		UWorld* World = GWorld;

		// Generate the reference graph for the world
		FReferencedAssets* WorldReferencer = new(Referencers)FReferencedAssets(World);
		FFindAssetsArchive(World, WorldReferencer->AssetList, &ReferenceGraph, MaxRecursionDepth, bIncludeClasses, bIncludeDefaults, bReverseReferenceGraph);

		// Also include all the streaming levels in the results
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (StreamingLevel)
			{
				if (ULevel* Level = StreamingLevel->GetLoadedLevel())
				{
					// Generate the reference graph for each streamed in level
					FReferencedAssets* LevelReferencer = new(Referencers) FReferencedAssets(Level);			
					FFindAssetsArchive(Level, LevelReferencer->AssetList, &ReferenceGraph, MaxRecursionDepth, bIncludeClasses, bIncludeDefaults, bReverseReferenceGraph);
				}
			}
		}

		TArray<UObject*> ReferencedObjects;
		// Special case for blueprints
		for (AActor* Actor : FActorRange(World))
		{
			ReferencedObjects.Reset();
			Actor->GetReferencedContentObjects(ReferencedObjects);
			for(UObject* Reference : ReferencedObjects)
			{
				TSet<UObject*>& Objects = ReferenceGraph.FindOrAdd(Reference);
				Objects.Add(Actor);
			}
		}
	}

	void MarkAllObjects()
	{
		// Mark all objects so we don't get into an endless recursion
		for (FObjectIterator It; It; ++It)
		{
			It->Mark(OBJECTMARK_TagExp);
		}
	}

	void Generate( const UObject* AssetToFind, TSet<const UObject*>& OutObjects )
	{
		// Don't examine visited objects
		if (!AssetToFind->HasAnyMarks(OBJECTMARK_TagExp))
		{
			return;
		}

		AssetToFind->UnMark(OBJECTMARK_TagExp);

		// Return once we find a parent object that is an actor
		if (AssetToFind->IsA(AActor::StaticClass()))
		{
			OutObjects.Add(AssetToFind);
			return;
		}

		// Traverse the reference graph looking for actor objects
		TSet<UObject*>* ReferencingObjects = ReferenceGraph.Find(AssetToFind);
		if (ReferencingObjects)
		{
			for(TSet<UObject*>::TConstIterator SetIt(*ReferencingObjects); SetIt; ++SetIt)
			{
				Generate(*SetIt, OutObjects);
			}
		}
	}
};

void FAssetContextMenu::ExecuteFindAssetInWorld()
{
	TArray<UObject*> AssetsToFind;
	const bool SkipRedirectors = true;
	GetSelectedAssets(AssetsToFind, SkipRedirectors);

	const bool NoteSelectionChange = true;
	const bool DeselectBSPSurfs = true;
	const bool WarnAboutManyActors = false;
	GEditor->SelectNone(NoteSelectionChange, DeselectBSPSurfs, WarnAboutManyActors);

	if (AssetsToFind.Num() > 0)
	{
		FScopedSlowTask SlowTask(2 + AssetsToFind.Num(), NSLOCTEXT("AssetContextMenu", "FindAssetInWorld", "Finding actors that use this asset..."));
		SlowTask.MakeDialog();

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		TSet<const UObject*> OutObjects;
		WorldReferenceGenerator ObjRefGenerator;

		SlowTask.EnterProgressFrame();
		ObjRefGenerator.BuildReferencingData();

		for (UObject* AssetToFind : AssetsToFind)
		{
			SlowTask.EnterProgressFrame();
			ObjRefGenerator.MarkAllObjects();
			ObjRefGenerator.Generate(AssetToFind, OutObjects);
		}

		SlowTask.EnterProgressFrame();

		if (OutObjects.Num() > 0)
		{
			const bool InSelected = true;
			const bool Notify = false;

			// Select referencing actors
			for (const UObject* Object : OutObjects)
			{
				GEditor->SelectActor(const_cast<AActor*>(CastChecked<AActor>(Object)), InSelected, Notify);
			}

			GEditor->NoteSelectionChange();
		}
		else
		{
			FNotificationInfo Info(LOCTEXT("NoReferencingActorsFound", "No actors found."));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
}

void FAssetContextMenu::ExecutePropertyMatrix()
{
	TArray<UObject*> ObjectsForPropertiesMenu;
	const bool SkipRedirectors = true;
	GetSelectedAssets(ObjectsForPropertiesMenu, SkipRedirectors);

	if ( ObjectsForPropertiesMenu.Num() > 0 )
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
		PropertyEditorModule.CreatePropertyEditorToolkit( EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), ObjectsForPropertiesMenu );
	}
}

void FAssetContextMenu::ExecuteShowAssetMetaData()
{
	for (const FAssetData& AssetData : SelectedAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (Asset)
		{
			TMap<FName, FString>* TagValues = UMetaData::GetMapForObject(Asset);
			if (TagValues)
			{
				// Create and display a resizable window to display the MetaDataView for each asset with metadata
				FString Title = FString::Printf(TEXT("Metadata: %s"), *AssetData.AssetName.ToString());

				TSharedPtr< SWindow > Window = SNew(SWindow)
					.Title(FText::FromString(Title))
					.SupportsMaximize(false)
					.SupportsMinimize(false)
					.MinWidth(500.0f)
					.MinHeight(250.0f)
					[
						SNew(SBorder)
						.Padding(4.f)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							SNew(SMetaDataView, *TagValues)
						]
					];

				FSlateApplication::Get().AddWindow(Window.ToSharedRef());
			}
		}
	}
}

bool FAssetContextMenu::CanModifyPath(const FString& InPath) const
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	if (!AssetToolsModule.Get().GetWritableFolderBlacklist()->PassesStartsWithFilter(InPath))
	{
		return false;
	}

	return true;
}

void FAssetContextMenu::ExecuteEditAsset()
{
	TMap<UClass*, TArray<UObject*> > SelectedAssetsByClass;
	GetSelectedAssetsByClass(SelectedAssetsByClass);

	// Open 
	for (const auto& AssetsByClassPair : SelectedAssetsByClass)
	{
		const auto& TypeAssets = AssetsByClassPair.Value;
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(TypeAssets);
	}
}

void FAssetContextMenu::ExecuteSaveAsset()
{
	TArray<UPackage*> PackagesToSave;
	GetSelectedPackages(PackagesToSave);

	const bool bCheckDirty = false;
	const bool bPromptToSave = false;
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}

void FAssetContextMenu::ExecuteDiffSelected() const
{
	if (SelectedAssets.Num() >= 2)
	{
		UObject* FirstObjectSelected = SelectedAssets[0].GetAsset();
		UObject* SecondObjectSelected = SelectedAssets[1].GetAsset();

		if ((FirstObjectSelected != NULL) && (SecondObjectSelected != NULL))
		{
			// Load the asset registry module
			FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

			FRevisionInfo CurrentRevision; 
			CurrentRevision.Revision = TEXT("");

			AssetToolsModule.Get().DiffAssets(FirstObjectSelected, SecondObjectSelected, CurrentRevision, CurrentRevision);
		}
	}
}

void FAssetContextMenu::ExecuteDuplicate() 
{
	TArray<UObject*> ObjectsToDuplicate;
	const bool SkipRedirectors = true;
	GetSelectedAssets(ObjectsToDuplicate, SkipRedirectors);

	if ( ObjectsToDuplicate.Num() == 1 )
	{
		OnDuplicateRequested.ExecuteIfBound(ObjectsToDuplicate[0]);
	}
	else if ( ObjectsToDuplicate.Num() > 1 )
	{
		TArray<UObject*> NewObjects;
		ObjectTools::DuplicateObjects(ObjectsToDuplicate, TEXT(""), TEXT(""), /*bOpenDialog=*/false, &NewObjects);

		TArray<FAssetData> AssetsToSync;
		for ( auto ObjIt = NewObjects.CreateConstIterator(); ObjIt; ++ObjIt )
		{
			new(AssetsToSync) FAssetData(*ObjIt);
		}

		// Sync to asset tree
		if ( NewObjects.Num() > 0 )
		{
			OnFindInAssetTreeRequested.ExecuteIfBound(AssetsToSync);
		}
	}
}

void FAssetContextMenu::ExecuteRename()
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	TArray< FString > SelectedFolders = AssetView.Pin()->GetSelectedFolders();

	if ( AssetViewSelectedAssets.Num() == 1 && SelectedFolders.Num() == 0 )
	{
		// Don't operate on Redirectors
		if ( AssetViewSelectedAssets[0].AssetClass != UObjectRedirector::StaticClass()->GetFName() )
		{
			if (CanModifyPath(AssetViewSelectedAssets[0].PackageName.ToString()))
			{
				OnRenameRequested.ExecuteIfBound(AssetViewSelectedAssets[0]);
			}
			else
			{
				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
				AssetToolsModule.Get().NotifyBlockedByWritableFolderFilter();
			}
		}
	}

	if ( AssetViewSelectedAssets.Num() == 0 && SelectedFolders.Num() == 1 )
	{
		if (CanModifyPath(SelectedFolders[0]))
		{
			OnRenameFolderRequested.ExecuteIfBound(SelectedFolders[0]);
		}
		else
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().NotifyBlockedByWritableFolderFilter();
		}
	}
}

void FAssetContextMenu::ExecuteDelete()
{
	// Don't allow asset deletion during PIE
	if (GIsEditor)
	{
		UEditorEngine* Editor = GEditor;
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			FNotificationInfo Notification(LOCTEXT("CannotDeleteAssetInPIE", "Assets cannot be deleted while in PIE."));
			Notification.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Notification);
			return;
		}
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const TSharedRef<FBlacklistPaths>& WritableFolderFilter = AssetToolsModule.Get().GetWritableFolderBlacklist();
	const bool bHasWritableFolderFilter = WritableFolderFilter->HasFiltering();

	TArray<FString> SelectedFolders = AssetView.Pin()->GetSelectedFolders();
	if (SelectedFolders.Num() > 0 && !AssetToolsModule.Get().AllPassWritableFolderFilter(SelectedFolders))
	{
		AssetToolsModule.Get().NotifyBlockedByWritableFolderFilter();
		return;
	}

	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	if(AssetViewSelectedAssets.Num() > 0)
	{
		TArray<FAssetData> AssetsToDelete;

		for( auto AssetIt = AssetViewSelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt )
		{
			const FAssetData& AssetData = *AssetIt;

			if( AssetData.AssetClass == UObjectRedirector::StaticClass()->GetFName() )
			{
				// Don't operate on Redirectors
				continue;
			}

			if (bHasWritableFolderFilter && !WritableFolderFilter->PassesStartsWithFilter(AssetData.PackageName))
			{
				AssetToolsModule.Get().NotifyBlockedByWritableFolderFilter();
				return;
			}

			AssetsToDelete.Add( AssetData );
		}

		if ( AssetsToDelete.Num() > 0 )
		{
			ObjectTools::DeleteAssets( AssetsToDelete );
		}
	}

	if(SelectedFolders.Num() > 0)
	{
		FText Prompt;
		if ( SelectedFolders.Num() == 1 )
		{
			Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Single", "Delete folder '{0}'?"), FText::FromString(SelectedFolders[0]));
		}
		else
		{
			Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Multiple", "Delete {0} folders?"), FText::AsNumber(SelectedFolders.Num()));
		}

		// Spawn a confirmation dialog since this is potentially a highly destructive operation
		ContentBrowserUtils::DisplayConfirmationPopup(
			Prompt,
			LOCTEXT("FolderDeleteConfirm_Yes", "Delete"),
			LOCTEXT("FolderDeleteConfirm_No", "Cancel"),
			AssetView.Pin().ToSharedRef(),
			FOnClicked::CreateSP( this, &FAssetContextMenu::ExecuteDeleteFolderConfirmed ));
	}
}

bool FAssetContextMenu::CanExecuteReload() const
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	TArray< FString > SelectedFolders = AssetView.Pin()->GetSelectedFolders();

	int32 NumAssetItems, NumClassItems;
	ContentBrowserUtils::CountItemTypes(AssetViewSelectedAssets, NumAssetItems, NumClassItems);

	int32 NumAssetPaths, NumClassPaths;
	ContentBrowserUtils::CountPathTypes(SelectedFolders, NumAssetPaths, NumClassPaths);

	bool bHasSelectedCollections = false;
	for (const FString& SelectedFolder : SelectedFolders)
	{
		if (ContentBrowserUtils::IsCollectionPath(SelectedFolder))
		{
			bHasSelectedCollections = true;
			break;
		}
	}

	// We can't reload classes, or folders containing classes, or any collection folders
	return ((NumAssetItems > 0 && NumClassItems == 0) || (NumAssetPaths > 0 && NumClassPaths == 0)) && !bHasSelectedCollections;
}

void FAssetContextMenu::ExecuteReload()
{
	// Don't allow asset reload during PIE
	if (GIsEditor)
	{
		UEditorEngine* Editor = GEditor;
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			FNotificationInfo Notification(LOCTEXT("CannotReloadAssetInPIE", "Assets cannot be reloaded while in PIE."));
			Notification.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Notification);
			return;
		}
	}

	TArray<FAssetData> AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	if (AssetViewSelectedAssets.Num() > 0)
	{
		TArray<UPackage*> PackagesToReload;

		for (auto AssetIt = AssetViewSelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			const FAssetData& AssetData = *AssetIt;

			if (AssetData.AssetClass == UObjectRedirector::StaticClass()->GetFName())
			{
				// Don't operate on Redirectors
				continue;
			}

			if (AssetData.AssetClass == UUserDefinedStruct::StaticClass()->GetFName())
			{
				FNotificationInfo Notification(LOCTEXT("CannotReloadUserStruct", "User created structures cannot be safely reloaded."));
				Notification.ExpireDuration = 3.0f;
				FSlateNotificationManager::Get().AddNotification(Notification);
				continue;
			}

			if (AssetData.AssetClass == UUserDefinedEnum::StaticClass()->GetFName())
			{
				FNotificationInfo Notification(LOCTEXT("CannotReloadUserEnum", "User created enumerations cannot be safely reloaded."));
				Notification.ExpireDuration = 3.0f;
				FSlateNotificationManager::Get().AddNotification(Notification);
				continue;
			}

			PackagesToReload.AddUnique(AssetData.GetPackage());
		}

		if (PackagesToReload.Num() > 0)
		{
			UPackageTools::ReloadPackages(PackagesToReload);
		}
	}
}

FReply FAssetContextMenu::ExecuteDeleteFolderConfirmed()
{
	TArray< FString > SelectedFolders = AssetView.Pin()->GetSelectedFolders();
	if(SelectedFolders.Num() > 0)
	{
		ContentBrowserUtils::DeleteFolders(SelectedFolders);
	}

	return FReply::Handled();
}

void FAssetContextMenu::ExecuteConsolidate()
{
	TArray<UObject*> ObjectsToConsolidate;
	const bool SkipRedirectors = true;
	GetSelectedAssets(ObjectsToConsolidate, SkipRedirectors);

	if ( ObjectsToConsolidate.Num() >  0 )
	{
		FConsolidateToolWindow::AddConsolidationObjects( ObjectsToConsolidate );
	}
}

void FAssetContextMenu::ExecuteCaptureThumbnail()
{
	FViewport* Viewport = GEditor->GetActiveViewport();

	if ( ensure(GCurrentLevelEditingViewportClient) && ensure(Viewport) )
	{
		//have to re-render the requested viewport
		FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
		//remove selection box around client during render
		GCurrentLevelEditingViewportClient = NULL;
		Viewport->Draw();

		ContentBrowserUtils::CaptureThumbnailFromViewport(Viewport, SelectedAssets);

		//redraw viewport to have the yellow highlight again
		GCurrentLevelEditingViewportClient = OldViewportClient;
		Viewport->Draw();
	}
}

void FAssetContextMenu::ExecuteClearThumbnail()
{
	ContentBrowserUtils::ClearCustomThumbnails(SelectedAssets);
}

void FAssetContextMenu::ExecuteMigrateAsset()
{
	// Get a list of package names for input into MigratePackages
	TArray<FName> PackageNames;
	PackageNames.Reserve(SelectedAssets.Num());
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		PackageNames.Add(SelectedAssets[AssetIdx].PackageName);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().MigratePackages( PackageNames );
}

void FAssetContextMenu::ExecuteGoToCodeForAsset(UClass* SelectedClass)
{
	if (SelectedClass)
	{
		FString ClassHeaderPath;
		if( FSourceCodeNavigation::FindClassHeaderPath( SelectedClass, ClassHeaderPath ) && IFileManager::Get().FileSize( *ClassHeaderPath ) != INDEX_NONE )
		{
			const FString AbsoluteHeaderPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ClassHeaderPath);
			FSourceCodeNavigation::OpenSourceFile( AbsoluteHeaderPath );
		}
	}
}

void FAssetContextMenu::ExecuteGoToDocsForAsset(UClass* SelectedClass)
{
	ExecuteGoToDocsForAsset(SelectedClass, FString());
}

void FAssetContextMenu::ExecuteGoToDocsForAsset(UClass* SelectedClass, const FString ExcerptSection)
{
	if (SelectedClass)
	{
		FString DocumentationLink = FEditorClassUtils::GetDocumentationLink(SelectedClass, ExcerptSection);
		if (!DocumentationLink.IsEmpty())
		{
			IDocumentation::Get()->Open(DocumentationLink, FDocumentationSourceInfo(TEXT("cb_docs")));
		}
	}
}

void FAssetContextMenu::ExecuteCopyReference()
{
	ContentBrowserUtils::CopyAssetReferencesToClipboard(SelectedAssets);
}

void FAssetContextMenu::ExecuteCopyFilePath()
{
	ContentBrowserUtils::CopyFilePathsToClipboard(SelectedAssets);
}

void FAssetContextMenu::ExecuteCopyTextToClipboard(FString InText)
{
	FPlatformApplicationMisc::ClipboardCopy(*InText);
}

void FAssetContextMenu::ExecuteResetLocalizationId()
{
#if USE_STABLE_LOCALIZATION_KEYS
	const FText ResetLocalizationIdMsg = LOCTEXT("ResetLocalizationIdMsg", "This will reset the localization ID of the selected assets and cause all text within them to lose their existing translations.\n\nAre you sure you want to do this?");
	if (FMessageDialog::Open(EAppMsgType::YesNo, ResetLocalizationIdMsg) != EAppReturnType::Yes)
	{
		return;
	}

	for (const FAssetData& AssetData : SelectedAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (Asset)
		{
			Asset->Modify();
			TextNamespaceUtil::ClearPackageNamespace(Asset);
			TextNamespaceUtil::EnsurePackageNamespace(Asset);
		}
	}
#endif // USE_STABLE_LOCALIZATION_KEYS
}

void FAssetContextMenu::ExecuteShowLocalizationCache(const FString InPackageFilename)
{
	FString CachedLocalizationId;
	TArray<FGatherableTextData> GatherableTextDataArray;

	// Read the localization data from the cache in the package header
	{
		TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*InPackageFilename));
		if (FileReader)
		{
			// Read package file summary from the file
			FPackageFileSummary PackageFileSummary;
			*FileReader << PackageFileSummary;

			CachedLocalizationId = PackageFileSummary.LocalizationId;

			if (PackageFileSummary.GatherableTextDataOffset > 0)
			{
				FileReader->Seek(PackageFileSummary.GatherableTextDataOffset);

				GatherableTextDataArray.SetNum(PackageFileSummary.GatherableTextDataCount);
				for (int32 GatherableTextDataIndex = 0; GatherableTextDataIndex < PackageFileSummary.GatherableTextDataCount; ++GatherableTextDataIndex)
				{
					*FileReader << GatherableTextDataArray[GatherableTextDataIndex];
				}
			}
		}
	}

	// Convert the gathered text array into a readable format
	FString LocalizationCacheStr = FString::Printf(TEXT("Package: %s"), *CachedLocalizationId);
	for (const FGatherableTextData& GatherableTextData : GatherableTextDataArray)
	{
		if (LocalizationCacheStr.Len() > 0)
		{
			LocalizationCacheStr += TEXT("\n\n");
		}

		FString KeysStr;
		FString EditorOnlyKeysStr;
		for (const FTextSourceSiteContext& TextSourceSiteContext : GatherableTextData.SourceSiteContexts)
		{
			FString* KeysStrPtr = TextSourceSiteContext.IsEditorOnly ? &EditorOnlyKeysStr : &KeysStr;
			if (KeysStrPtr->Len() > 0)
			{
				*KeysStrPtr += TEXT(", ");
			}
			*KeysStrPtr += TextSourceSiteContext.KeyName;
		}

		LocalizationCacheStr += FString::Printf(TEXT("Namespace: %s\n"), *GatherableTextData.NamespaceName);
		if (KeysStr.Len() > 0)
		{
			LocalizationCacheStr += FString::Printf(TEXT("Keys: %s\n"), *KeysStr);
		}
		if (EditorOnlyKeysStr.Len() > 0)
		{
			LocalizationCacheStr += FString::Printf(TEXT("Keys (Editor-Only): %s\n"), *EditorOnlyKeysStr);
		}
		LocalizationCacheStr += FString::Printf(TEXT("Source: %s"), *GatherableTextData.SourceData.SourceString);
	}

	// Generate a message box for the result
	SGenericDialogWidget::OpenDialog(LOCTEXT("LocalizationCache", "Localization Cache"), 
		SNew(SBox)
		.MaxDesiredWidth(800.0f)
		.MaxDesiredHeight(400.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.IsReadOnly(true)
			.AutoWrapText(true)
			.Text(FText::AsCultureInvariant(LocalizationCacheStr))
		],
		SGenericDialogWidget::FArguments()
		.UseScrollBox(false)
	);
}

void FAssetContextMenu::ExecuteExport()
{
	TArray<UObject*> ObjectsToExport;
	const bool SkipRedirectors = false;
	GetSelectedAssets(ObjectsToExport, SkipRedirectors);

	if ( ObjectsToExport.Num() > 0 )
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

		AssetToolsModule.Get().ExportAssetsWithDialog(ObjectsToExport, true);
	}
}

void FAssetContextMenu::ExecuteBulkExport()
{
	TArray<UObject*> ObjectsToExport;
	const bool SkipRedirectors = false;
	GetSelectedAssets(ObjectsToExport, SkipRedirectors);

	if ( ObjectsToExport.Num() > 0 )
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

		AssetToolsModule.Get().ExportAssetsWithDialog(ObjectsToExport, false);
	}
}

void FAssetContextMenu::ExecuteRemoveFromCollection()
{
	if ( ensure(SourcesData.Collections.Num() == 1) )
	{
		TArray<FName> AssetsToRemove;
		for (auto AssetIt = SelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			AssetsToRemove.Add((*AssetIt).ObjectPath);
		}

		if ( AssetsToRemove.Num() > 0 )
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			const FCollectionNameType& Collection = SourcesData.Collections[0];
			CollectionManagerModule.Get().RemoveFromCollection(Collection.Name, Collection.Type, AssetsToRemove);
			OnAssetViewRefreshRequested.ExecuteIfBound();
		}
	}
}

void FAssetContextMenu::ExecuteSCCRefresh()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);

	ISourceControlModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FUpdateStatus>(), SourceControlHelpers::PackageFilenames(PackageNames), EConcurrency::Asynchronous);
}

void FAssetContextMenu::ExecuteSCCMerge()
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); AssetIdx++)
	{
		// Get the actual asset (will load it)
		const FAssetData& AssetData = SelectedAssets[AssetIdx];

		UObject* CurrentObject = AssetData.GetAsset();
		if (CurrentObject)
		{
			const FString PackagePath = AssetData.PackageName.ToString();
			const FString PackageName = AssetData.AssetName.ToString();
			auto AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass( CurrentObject->GetClass() ).Pin();
			if( AssetTypeActions.IsValid() )
			{
				AssetTypeActions->Merge(CurrentObject);
			}
		}
	}
}

void FAssetContextMenu::ExecuteSCCCheckOut()
{
	TArray<UPackage*> PackagesToCheckOut;
	GetSelectedPackages(PackagesToCheckOut);

	if ( PackagesToCheckOut.Num() > 0 )
	{
		// Update the source control status of all potentially relevant packages
		if (ISourceControlModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FUpdateStatus>(), PackagesToCheckOut) == ECommandResult::Succeeded)
		{
			// Now check them out
			FEditorFileUtils::CheckoutPackages(PackagesToCheckOut);
		}
	}
}

void FAssetContextMenu::ExecuteSCCOpenForAdd()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	TArray<FString> PackagesToAdd;
	TArray<UPackage*> PackagesToSave;
	for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(*PackageIt), EStateCacheUsage::Use);
		if ( SourceControlState.IsValid() && !SourceControlState->IsSourceControlled() )
		{
			PackagesToAdd.Add(*PackageIt);

			// Make sure the file actually exists on disk before adding it
			FString Filename;
			if ( !FPackageName::DoesPackageExist(*PackageIt, NULL, &Filename) )
			{
				UPackage* Package = FindPackage(NULL, **PackageIt);
				if ( Package )
				{
					PackagesToSave.Add(Package);
				}
			}
		}
	}

	if ( PackagesToAdd.Num() > 0 )
	{
		// If any of the packages are new, save them now
		if ( PackagesToSave.Num() > 0 )
		{
			const bool bCheckDirty = false;
			const bool bPromptToSave = false;
			TArray<UPackage*> FailedPackages;
			const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave, &FailedPackages);
			if(FailedPackages.Num() > 0)
			{
				// don't try and add files that failed to save - remove them from the list
				for(auto FailedPackageIt = FailedPackages.CreateConstIterator(); FailedPackageIt; FailedPackageIt++)
				{
					PackagesToAdd.Remove((*FailedPackageIt)->GetName());
				}
			}
		}

		SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), SourceControlHelpers::PackageFilenames(PackagesToAdd));
	}
}

void FAssetContextMenu::ExecuteSCCCheckIn()
{
	TArray<UPackage*> Packages;
	GetSelectedPackages(Packages);

	// Prompt the user to ask if they would like to first save any dirty packages they are trying to check-in
	const FEditorFileUtils::EPromptReturnCode UserResponse = FEditorFileUtils::PromptForCheckoutAndSave( Packages, true, true );

	// If the user elected to save dirty packages, but one or more of the packages failed to save properly OR if the user
	// canceled out of the prompt, don't follow through on the check-in process
	const bool bShouldProceed = ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Success || UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Declined );
	if ( bShouldProceed )
	{
		TArray<FString> PackageNames;
		GetSelectedPackageNames(PackageNames);

		const bool bUseSourceControlStateCache = true;
		const bool bCheckinGood = FSourceControlWindows::PromptForCheckin(bUseSourceControlStateCache, PackageNames);

		if (!bCheckinGood)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SCC_Checkin_Failed", "Check-in failed as a result of save failure."));
		}
	}
	else
	{
		// If a failure occurred, alert the user that the check-in was aborted. This warning shouldn't be necessary if the user cancelled
		// from the dialog, because they obviously intended to cancel the whole operation.
		if ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Failure )
		{
			FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SCC_Checkin_Aborted", "Check-in aborted as a result of save failure.") );
		}
	}
}

void FAssetContextMenu::ExecuteSCCHistory()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);
	FSourceControlWindows::DisplayRevisionHistory(SourceControlHelpers::PackageFilenames(PackageNames));
}

void FAssetContextMenu::ExecuteSCCDiffAgainstDepot() const
{
	// Load the asset registry module
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	// Iterate over each selected asset
	for(int32 AssetIdx=0; AssetIdx<SelectedAssets.Num(); AssetIdx++)
	{
		// Get the actual asset (will load it)
		const FAssetData& AssetData = SelectedAssets[AssetIdx];

		UObject* CurrentObject = AssetData.GetAsset();
		if( CurrentObject )
		{
			const FString PackagePath = AssetData.PackageName.ToString();
			const FString PackageName = AssetData.AssetName.ToString();
			AssetToolsModule.Get().DiffAgainstDepot( CurrentObject, PackagePath, PackageName );
		}
	}
}

void FAssetContextMenu::ExecuteSCCRevert()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);
	FSourceControlWindows::PromptForRevert(PackageNames);
}

void FAssetContextMenu::ExecuteSCCSync()
{
	TArray<FString> PackageNames;
	GetSelectedPackageNames(PackageNames);
	ContentBrowserUtils::SyncPackagesFromSourceControl(PackageNames);
}

void FAssetContextMenu::ExecuteEnableSourceControl()
{
	ISourceControlModule::Get().ShowLoginDialog(FSourceControlLoginClosed(), ELoginWindowMode::Modeless);
}

bool FAssetContextMenu::CanExecuteSyncToAssetTree() const
{
	return SelectedAssets.Num() > 0;
}

bool FAssetContextMenu::CanExecuteFindInExplorer() const
{
	// selection must contain at least one asset that has already been saved to disk
	for (const FAssetData& Asset : SelectedAssets)
	{
		if ((Asset.PackageFlags & PKG_NewlyCreated) == 0)
		{
			return true;
		}
	}

	return false;
}

bool FAssetContextMenu::CanExecuteCreateBlueprintUsing() const
{
	// Only work if you have a single asset selected
	if(SelectedAssets.Num() == 1)
	{
		UObject* Asset = SelectedAssets[0].GetAsset();
		// See if we know how to make a component from this asset
		TArray< TSubclassOf<UActorComponent> > ComponentClassList = FComponentAssetBrokerage::GetComponentsForAsset(Asset);
		return (ComponentClassList.Num() > 0);
	}

	return false;
}

bool FAssetContextMenu::CanExecuteFindAssetInWorld() const
{
	return bAtLeastOneNonRedirectorSelected;
}

bool FAssetContextMenu::CanExecuteProperties() const
{
	return bAtLeastOneNonRedirectorSelected;
}

bool FAssetContextMenu::CanExecutePropertyMatrix(FText& OutErrorMessage) const
{
	bool bResult = bAtLeastOneNonRedirectorSelected;
	if (bAtLeastOneNonRedirectorSelected)
	{
		TArray<UObject*> ObjectsForPropertiesMenu;
		const bool SkipRedirectors = true;
		GetSelectedAssets(ObjectsForPropertiesMenu, SkipRedirectors);

		// Ensure all Blueprints are valid.
		for (UObject* Object : ObjectsForPropertiesMenu)
		{
			if (UBlueprint* BlueprintObj = Cast<UBlueprint>(Object))
			{
				if (BlueprintObj->GeneratedClass == nullptr)
				{
					OutErrorMessage = LOCTEXT("InvalidBlueprint", "A selected Blueprint is invalid.");
					bResult = false;
					break;
				}
			}
		}
	}
	return bResult;
}

bool FAssetContextMenu::CanExecutePropertyMatrix() const
{
	FText ErrorMessageDummy;
	return CanExecutePropertyMatrix(ErrorMessageDummy);
}

FText FAssetContextMenu::GetExecutePropertyMatrixTooltip() const
{
	FText ResultTooltip;
	if (CanExecutePropertyMatrix(ResultTooltip))
	{
		ResultTooltip = LOCTEXT("PropertyMatrixTooltip", "Opens the property matrix editor for the selected assets.");
	}
	return ResultTooltip;
}

bool FAssetContextMenu::CanExecuteShowAssetMetaData() const
{
	TArray<UObject*> ObjectsForPropertiesMenu;
	const bool SkipRedirectors = true;
	GetSelectedAssets(ObjectsForPropertiesMenu, SkipRedirectors);

	bool bResult = false;
	for (const UObject* Asset : ObjectsForPropertiesMenu)
	{
		if (Asset && UMetaData::GetMapForObject(Asset))
		{
			bResult = true;
			break;
		}
	}
	return bResult;
}

bool FAssetContextMenu::CanExecuteDuplicate() const
{
	const TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	uint32 NumNonRedirectors = 0;

	for (const FAssetData& AssetData : AssetViewSelectedAssets)
	{
		if (!AssetData.IsValid())
		{
			continue;
		}

		if (AssetData.AssetClass == NAME_Class)
		{
			return false;
		}

		if (AssetData.AssetClass != UObjectRedirector::StaticClass()->GetFName())
		{
			++NumNonRedirectors;
		}
	}

	return (NumNonRedirectors > 0);
}

bool FAssetContextMenu::CanExecuteRename() const
{
	return ContentBrowserUtils::CanRenameFromAssetView(AssetView);
}

bool FAssetContextMenu::CanExecuteDelete() const
{
	return ContentBrowserUtils::CanDeleteFromAssetView(AssetView);
}

bool FAssetContextMenu::CanExecuteRemoveFromCollection() const 
{
	return SourcesData.Collections.Num() == 1 && !SourcesData.IsDynamicCollection();
}

bool FAssetContextMenu::CanExecuteSCCRefresh() const
{
	return ISourceControlModule::Get().IsEnabled();
}

bool FAssetContextMenu::CanExecuteSCCMerge() const
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	bool bCanExecuteMerge = bCanExecuteSCCMerge;
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num() && bCanExecuteMerge; AssetIdx++)
	{
		// Get the actual asset (will load it)
		const FAssetData& AssetData = SelectedAssets[AssetIdx];
		UObject* CurrentObject = AssetData.GetAsset();
		if (CurrentObject)
		{
			auto AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(CurrentObject->GetClass()).Pin();
			if (AssetTypeActions.IsValid())
			{
				bCanExecuteMerge = AssetTypeActions->CanMerge();
			}
		}
		else
		{
			bCanExecuteMerge = false;
		}
	}

	return bCanExecuteMerge;
}

bool FAssetContextMenu::CanExecuteSCCCheckOut() const
{
	return bCanExecuteSCCCheckOut;
}

bool FAssetContextMenu::CanExecuteSCCOpenForAdd() const
{
	return bCanExecuteSCCOpenForAdd;
}

bool FAssetContextMenu::CanExecuteSCCCheckIn() const
{
	return bCanExecuteSCCCheckIn;
}

bool FAssetContextMenu::CanExecuteSCCHistory() const
{
	return bCanExecuteSCCHistory;
}

bool FAssetContextMenu::CanExecuteSCCDiffAgainstDepot() const
{
	return bCanExecuteSCCHistory;
}

bool FAssetContextMenu::CanExecuteSCCRevert() const
{
	return bCanExecuteSCCRevert;
}

bool FAssetContextMenu::CanExecuteSCCSync() const
{
	return bCanExecuteSCCSync;
}

bool FAssetContextMenu::CanExecuteConsolidate() const
{
	TArray<UObject*> ProposedObjects;
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		// Don't load assets here. Only operate on already loaded assets.
		if ( SelectedAssets[AssetIdx].IsAssetLoaded() )
		{
			UObject* Object = SelectedAssets[AssetIdx].GetAsset();

			if ( Object )
			{
				ProposedObjects.Add(Object);
			}
		}
	}
	
	if ( ProposedObjects.Num() > 0 )
	{
		TArray<UObject*> CompatibleObjects;
		return FConsolidateToolWindow::DetermineAssetCompatibility(ProposedObjects, CompatibleObjects);
	}

	return false;
}

bool FAssetContextMenu::CanExecuteSaveAsset() const
{
	if ( bAtLeastOneClassSelected )
	{
		return false;
	}

	TArray<UPackage*> Packages;
	GetSelectedPackages(Packages);

	// only enabled if at least one selected package is loaded at all
	for (int32 PackageIdx = 0; PackageIdx < Packages.Num(); ++PackageIdx)
	{
		if ( Packages[PackageIdx] != NULL )
		{
			return true;
		}
	}

	return false;
}

bool FAssetContextMenu::CanExecuteDiffSelected() const
{
	bool bCanDiffSelected = false;
	if (SelectedAssets.Num() == 2 && !bAtLeastOneClassSelected)
	{
		FAssetData const& FirstSelection = SelectedAssets[0];
		FAssetData const& SecondSelection = SelectedAssets[1];

		bCanDiffSelected = FirstSelection.AssetClass == SecondSelection.AssetClass;
	}

	return bCanDiffSelected;
}

bool FAssetContextMenu::CanExecuteCaptureThumbnail() const
{
	return GCurrentLevelEditingViewportClient != NULL;
}

bool FAssetContextMenu::CanClearCustomThumbnails() const
{
	for ( auto AssetIt = SelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt )
	{
		if ( ContentBrowserUtils::AssetHasCustomThumbnail(*AssetIt) )
		{
			return true;
		}
	}

	return false;
}

void FAssetContextMenu::CacheCanExecuteVars()
{
	bAtLeastOneNonRedirectorSelected = false;
	bAtLeastOneClassSelected = false;
	bCanExecuteSCCMerge = false;
	bCanExecuteSCCCheckOut = false;
	bCanExecuteSCCOpenForAdd = false;
	bCanExecuteSCCCheckIn = false;
	bCanExecuteSCCHistory = false;
	bCanExecuteSCCRevert = false;
	bCanExecuteSCCSync = false;

	for (auto AssetIt = SelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt)
	{
		const FAssetData& AssetData = *AssetIt;
		if ( !AssetData.IsValid() )
		{
			continue;
		}

		if ( !bAtLeastOneNonRedirectorSelected && AssetData.AssetClass != UObjectRedirector::StaticClass()->GetFName() )
		{
			bAtLeastOneNonRedirectorSelected = true;
		}

		bAtLeastOneClassSelected |= AssetData.AssetClass == NAME_Class;

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		if ( ISourceControlModule::Get().IsEnabled() )
		{
			// Check the SCC state for each package in the selected paths
			FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(AssetData.PackageName.ToString()), EStateCacheUsage::Use);
			if(SourceControlState.IsValid())
			{
				if (SourceControlState->IsConflicted() )
				{
					bCanExecuteSCCMerge = true;
				}

				if ( SourceControlState->CanCheckout() )
				{
					bCanExecuteSCCCheckOut = true;
				}

				if ( !SourceControlState->IsSourceControlled() && SourceControlState->CanAdd() )
				{
					bCanExecuteSCCOpenForAdd = true;
				}
				else if( SourceControlState->IsSourceControlled() && !SourceControlState->IsAdded() )
				{
					bCanExecuteSCCHistory = true;
				}

				if(!SourceControlState->IsCurrent())
				{
					bCanExecuteSCCSync = true;
				}

				if ( SourceControlState->CanCheckIn() )
				{
					bCanExecuteSCCCheckIn = true;
				}

				if (SourceControlState->CanRevert())
				{
					bCanExecuteSCCRevert = true;
				}
			}
		}

		if ( bAtLeastOneNonRedirectorSelected
			&& bAtLeastOneClassSelected
			&& bCanExecuteSCCMerge
			&& bCanExecuteSCCCheckOut
			&& bCanExecuteSCCOpenForAdd
			&& bCanExecuteSCCCheckIn
			&& bCanExecuteSCCHistory
			&& bCanExecuteSCCRevert
			&& bCanExecuteSCCSync
			)
		{
			// All options are available, no need to keep iterating
			break;
		}
	}
}

void FAssetContextMenu::GetSelectedPackageNames(TArray<FString>& OutPackageNames) const
{
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		OutPackageNames.Add(SelectedAssets[AssetIdx].PackageName.ToString());
	}
}

void FAssetContextMenu::GetSelectedPackages(TArray<UPackage*>& OutPackages) const
{
	for (int32 AssetIdx = 0; AssetIdx < SelectedAssets.Num(); ++AssetIdx)
	{
		UPackage* Package = FindPackage(NULL, *SelectedAssets[AssetIdx].PackageName.ToString());

		if ( Package )
		{
			OutPackages.Add(Package);
		}
	}
}

void FAssetContextMenu::MakeChunkIDListMenu(UToolMenu* Menu)
{
	TArray<int32> FoundChunks;
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	for (const auto& SelectedAsset : AssetViewSelectedAssets)
	{
		UPackage* Package = FindPackage(NULL, *SelectedAsset.PackageName.ToString());

		if (Package)
		{
			for (auto ChunkID : Package->GetChunkIDs())
			{
				FoundChunks.AddUnique(ChunkID);
			}
		}
	}

	FToolMenuSection& Section = Menu->AddSection("Chunks");
	for (auto ChunkID : FoundChunks)
	{
		Section.AddMenuEntry(
			NAME_None,
			FText::Format(LOCTEXT("PackageChunk", "Chunk {0}"), FText::AsNumber(ChunkID)),
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteRemoveChunkID, ChunkID)
			)
		);
	}
}

void FAssetContextMenu::ExecuteAssignChunkID()
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	auto AssetViewPtr = AssetView.Pin();
	if (AssetViewSelectedAssets.Num() > 0 && AssetViewPtr.IsValid())
	{
		// Determine the position of the window so that it will spawn near the mouse, but not go off the screen.
		const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
		FSlateRect Anchor(CursorPos.X, CursorPos.Y, CursorPos.X, CursorPos.Y);

		FVector2D AdjustedSummonLocation = FSlateApplication::Get().CalculatePopupWindowPosition(Anchor, SColorPicker::DEFAULT_WINDOW_SIZE, true, FVector2D::ZeroVector, Orient_Horizontal);

		TSharedPtr<SWindow> Window = SNew(SWindow)
			.AutoCenter(EAutoCenter::None)
			.ScreenPosition(AdjustedSummonLocation)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.SizingRule(ESizingRule::Autosized)
			.Title(LOCTEXT("WindowHeader", "Enter Chunk ID"));

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MeshPaint_LabelStrength", "Chunk ID"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<int32>)
					.AllowSpin(true)
					.MinSliderValue(0)
					.MaxSliderValue(300)
					.MinValue(0)
					.MaxValue(300)
					.Value(this, &FAssetContextMenu::GetChunkIDSelection)
					.OnValueChanged(this, &FAssetContextMenu::OnChunkIDAssignChanged)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ChunkIDAssign_Yes", "OK"))
					.OnClicked(this, &FAssetContextMenu::OnChunkIDAssignCommit, Window)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ChunkIDAssign_No", "Cancel"))
					.OnClicked(this, &FAssetContextMenu::OnChunkIDAssignCancel, Window)
				]
			]
		);

		ChunkIDSelected = 0;
		FSlateApplication::Get().AddModalWindow(Window.ToSharedRef(), AssetViewPtr);
	}
}

void FAssetContextMenu::ExecuteRemoveAllChunkID()
{
	TArray<int32> EmptyChunks;
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	for (const auto& SelectedAsset : AssetViewSelectedAssets)
	{
		UPackage* Package = FindPackage(NULL, *SelectedAsset.PackageName.ToString());

		if (Package)
		{
			Package->SetChunkIDs(EmptyChunks);
			Package->SetDirtyFlag(true);
		}
	}
}

TOptional<int32> FAssetContextMenu::GetChunkIDSelection() const
{
	return ChunkIDSelected;
}

void FAssetContextMenu::OnChunkIDAssignChanged(int32 NewChunkID)
{
	ChunkIDSelected = NewChunkID;
}

FReply FAssetContextMenu::OnChunkIDAssignCommit(TSharedPtr<SWindow> Window)
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	for (const auto& SelectedAsset : AssetViewSelectedAssets)
	{
		UPackage* Package = FindPackage(NULL, *SelectedAsset.PackageName.ToString());

		if (Package)
		{
			TArray<int32> CurrentChunks = Package->GetChunkIDs();
			CurrentChunks.AddUnique(ChunkIDSelected);
			Package->SetChunkIDs(CurrentChunks);
			Package->SetDirtyFlag(true);
		}
	}

	Window->RequestDestroyWindow();

	return FReply::Handled();
}

FReply FAssetContextMenu::OnChunkIDAssignCancel(TSharedPtr<SWindow> Window)
{
	Window->RequestDestroyWindow();

	return FReply::Handled();
}

void FAssetContextMenu::ExecuteRemoveChunkID(int32 ChunkID)
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetView.Pin()->GetSelectedAssets();
	for (const auto& SelectedAsset : AssetViewSelectedAssets)
	{
		UPackage* Package = FindPackage(NULL, *SelectedAsset.PackageName.ToString());

		if (Package)
		{
			int32 FoundIndex;
			TArray<int32> CurrentChunks = Package->GetChunkIDs();
			CurrentChunks.Find(ChunkID, FoundIndex);
			if (FoundIndex != INDEX_NONE)
			{
				CurrentChunks.RemoveAt(FoundIndex);
				Package->SetChunkIDs(CurrentChunks);
				Package->SetDirtyFlag(true);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
