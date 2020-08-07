// Copyright Epic Games, Inc. All Rights Reserved.


#include "AssetRenameManager.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Class.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Layout/Margin.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "EditorStyleSet.h"
#include "SourceControlOperations.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "FileHelpers.h"
#include "SDiscoveringAssetsDialog.h"
#include "AssetRegistryModule.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "ObjectTools.h"
#include "Interfaces/IMainFrameModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/RedirectCollector.h"
#include "Settings/EditorProjectSettings.h"
#include "AssetToolsLog.h"
#include "Settings/EditorProjectSettings.h"
#include "Engine/World.h"
#include "Engine/MapBuildDataRegistry.h"

#define LOCTEXT_NAMESPACE "AssetRenameManager"

namespace AssetRenameManagerImpl
{
	// Same as CheckSubPath.IsEmpty() || SubPath == CheckSubPath || SubPath.StartsWith(CheckSubPath + TEXT("."))
	// but with early outs and without having to concatenate a string for comparison
	static bool IsSubPath(const FString& SubPath, const FString& CheckSubPath)
	{
		const int32 CheckSubPathLen = CheckSubPath.Len();
		if (CheckSubPathLen == 0)
		{
			return true;
		}

		const int32 SubPathLen = SubPath.Len();
		if (SubPathLen == CheckSubPathLen)
		{
			if (SubPathLen)
			{
				// Checking the last character first should skip most string compare since lots of paths might have the same beginning
				return (*SubPath)[SubPathLen - 1] == (*CheckSubPath)[SubPathLen - 1] && SubPath == CheckSubPath;
			}
			else
			{
				// Both strings are empty
				return true;
			}
		}
		else
		{
			//Checking for the . at the exact position first should eliminate most of the StartsWith comparison.
			return SubPathLen > CheckSubPathLen && (*SubPath)[CheckSubPathLen] == TEXT('.') && SubPath.StartsWith(CheckSubPath);
		}
	}
}

struct FAssetRenameDataWithReferencers : public FAssetRenameData
{
	TArray<FName> ReferencingPackageNames;
	FText FailureReason;
	bool bCreateRedirector;
	bool bRenameFailed;

	FAssetRenameDataWithReferencers(const FAssetRenameData& InRenameData)
		: FAssetRenameData(InRenameData)
		, bCreateRedirector(false)
		, bRenameFailed(false)
	{
		if (Asset.IsValid() && !OldObjectPath.IsValid())
		{
			OldObjectPath = FSoftObjectPath(Asset.Get());
		}
		else if (OldObjectPath.IsValid() && !Asset.IsValid())
		{
			Asset = OldObjectPath.ResolveObject();
		}

		if (!NewName.IsEmpty() && !NewObjectPath.IsValid())
		{
			NewObjectPath = FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), *NewPackagePath, *NewName, *NewName));
		}
		else if (NewObjectPath.IsValid() && NewName.IsEmpty())
		{
			NewName = NewObjectPath.GetAssetName();
			NewPackagePath = FPackageName::GetLongPackagePath(NewObjectPath.GetLongPackageName());
		}
	}
};

class SRenameFailures : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRenameFailures){}

		SLATE_ARGUMENT(TArray<FText>, FailedRenames)

	SLATE_END_ARGS()

	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct( const FArguments& InArgs )
	{
		for (const FText& RenameText : InArgs._FailedRenames)
		{
			FailedRenames.Add( MakeShareable( new FText(RenameText) ) );
		}

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage( FEditorStyle::GetBrush("Docking.Tab.ContentAreaBrush") )
			.Padding(FMargin(4, 8, 4, 4))
			[
				SNew(SVerticalBox)

				// Title text
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock) .Text( LOCTEXT("RenameFailureTitle", "The following assets could not be renamed.") )
				]

				// Failure list
				+SVerticalBox::Slot()
				.Padding(0, 8)
				.FillHeight(1.f)
				[
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
					[
						SNew(SListView<TSharedRef<FText>>)
						.ListItemsSource(&FailedRenames)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SRenameFailures::MakeListViewWidget)
					]
				]

				// Close button
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.OnClicked(this, &SRenameFailures::CloseClicked)
					.Text(LOCTEXT("RenameFailuresCloseButton", "Close"))
				]
			]
		];
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	static void OpenRenameFailuresDialog(const TArray<FText>& InFailedRenames)
	{
		TSharedRef<SWindow> RenameWindow = SNew(SWindow)
			.Title(LOCTEXT("FailedRenamesDialog", "Failed Renames"))
			.ClientSize(FVector2D(800,400))
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			[
				SNew(SRenameFailures).FailedRenames(InFailedRenames)
			];

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));

		if (MainFrameModule.GetParentWindow().IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(RenameWindow, MainFrameModule.GetParentWindow().ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(RenameWindow);
		}
	}

private:
	TSharedRef<ITableRow> MakeListViewWidget(TSharedRef<FText> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return
			SNew(STableRow< TSharedRef<FText> >, OwnerTable)
			[
				SNew(STextBlock).Text(Item.Get())
			];
	}

	FReply CloseClicked()
	{
		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}

		return FReply::Handled();
	}

private:
	TArray< TSharedRef<FText> > FailedRenames;
};


///////////////////////////
// FAssetRenameManager
///////////////////////////

/** Renames assets using the specified names. */
bool FAssetRenameManager::RenameAssets(const TArray<FAssetRenameData>& AssetsAndNames) const
{
	// If the asset registry is still loading assets, we cant check for referencers, so we must open the rename dialog
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		UE_LOG(LogAssetTools, Warning, TEXT("Unable To Rename While Discovering Assets"));
		return false;
	}
	const bool bAutoCheckout = true;
	const bool bWithDialog = false;
	return FixReferencesAndRename(AssetsAndNames, bAutoCheckout, bWithDialog);
}

void FAssetRenameManager::RenameAssetsWithDialog(const TArray<FAssetRenameData>& AssetsAndNames, bool bAutoCheckout) const
{
	bool bWithDialog = true;

	// If the asset registry is still loading assets, we cant check for referencers, so we must open the rename dialog
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		// Open a dialog asking the user to wait while assets are being discovered
		SDiscoveringAssetsDialog::OpenDiscoveringAssetsDialog(
			SDiscoveringAssetsDialog::FOnAssetsDiscovered::CreateSP(this, &FAssetRenameManager::FixReferencesAndRenameCallback, AssetsAndNames, bAutoCheckout, bWithDialog)
		);
	}
	else
	{
		// No need to wait, attempt to fix references and rename now.
		FixReferencesAndRename(AssetsAndNames, bAutoCheckout, bWithDialog);
	}
}

void FAssetRenameManager::FindSoftReferencesToObject(FSoftObjectPath TargetObject, TArray<UObject*>& ReferencingObjects) const
{
	TArray<FAssetRenameDataWithReferencers> AssetsToRename;
	AssetsToRename.Emplace(FAssetRenameDataWithReferencers(FAssetRenameData(TargetObject, TargetObject, true)));

	// Fill out referencers from asset registry
	PopulateAssetReferencers(AssetsToRename);

	// Load all referencing objects and find for referencing objects
	TMap<FSoftObjectPath, TArray<UObject*>> ReferencingObjectsMap;

	GatherReferencingObjects(AssetsToRename, ReferencingObjectsMap);

	// Build an array out of the map results.
	for (const auto& It : ReferencingObjectsMap)
	{
		for (UObject* Obj : It.Value)
		{
			ReferencingObjects.AddUnique(Obj);
		}
	}
}

void FAssetRenameManager::FindSoftReferencesToObjects(const TArray<FSoftObjectPath>& TargetObjects, TMap<FSoftObjectPath, TArray<UObject*>>& ReferencingObjects) const
{
	TArray<FAssetRenameDataWithReferencers> AssetsToRename;
	for (const FSoftObjectPath& TargetObject : TargetObjects)
	{
		AssetsToRename.Emplace(FAssetRenameDataWithReferencers(FAssetRenameData(TargetObject, TargetObject, true)));
	}

	// Fill out referencers from asset registry
	PopulateAssetReferencers(AssetsToRename);

	// Load all referencing objects and find for referencing objects
	GatherReferencingObjects(AssetsToRename, ReferencingObjects);
}

void FAssetRenameManager::FixReferencesAndRenameCallback(TArray<FAssetRenameData> AssetsAndNames, bool bAutoCheckout, bool bWithDialog) const
{
	FixReferencesAndRename(AssetsAndNames, bAutoCheckout, bWithDialog);
}

bool FAssetRenameManager::FixReferencesAndRename(const TArray<FAssetRenameData>& AssetsAndNames, bool bAutoCheckout, bool bWithDialog) const
{
	bool bSoftReferencesOnly = true;
	// Prep a list of assets to rename with an extra boolean to determine if they should leave a redirector or not
	TArray<FAssetRenameDataWithReferencers> AssetsToRename;
	AssetsToRename.Reset(AssetsAndNames.Num());
	// Avoid duplicates when adding MapBuildData to list
	TSet<UObject*> AssetsToRenameLookup;
	for (const FAssetRenameData& AssetRenameData : AssetsAndNames)
	{
		AssetsToRenameLookup.Add(AssetRenameData.Asset.Get());
	}
	for (const FAssetRenameData& AssetRenameData : AssetsAndNames)
	{
		if (!AssetRenameData.OldObjectPath.IsValid() && !AssetRenameData.NewObjectPath.IsValid())
		{
			// Rename MapBuildData when renaming world
			UWorld* World = Cast<UWorld>(AssetRenameData.Asset.Get());
			if (World && World->PersistentLevel && World->PersistentLevel->MapBuildData && !AssetsToRenameLookup.Contains(World->PersistentLevel->MapBuildData))
			{
				// Leave MapBuildData inside the map's package
				if (World->PersistentLevel->MapBuildData->GetOutermost() != World->GetOutermost())
				{
					FString NewMapBuildDataName = AssetRenameData.NewName + TEXT("_BuiltData");
					// Perform rename of MapBuildData before world otherwise original files left behind
					AssetsToRename.EmplaceAt(0, FAssetRenameDataWithReferencers(FAssetRenameData(World->PersistentLevel->MapBuildData, AssetRenameData.NewPackagePath, NewMapBuildDataName)));
					AssetsToRename[0].bOnlyFixSoftReferences = AssetRenameData.bOnlyFixSoftReferences;
					AssetsToRenameLookup.Add(World->PersistentLevel->MapBuildData);
				}
			}
		}

		// Perform rename of MapBuildData before world otherwise original files left behind
		UMapBuildDataRegistry* MapBuildData = Cast<UMapBuildDataRegistry>(AssetRenameData.Asset.Get());
		if (MapBuildData)
		{
			AssetsToRename.EmplaceAt(0, FAssetRenameDataWithReferencers(AssetRenameData));
		}
		else
		{
			AssetsToRename.Emplace(FAssetRenameDataWithReferencers(AssetRenameData));
		}

		if (!AssetRenameData.bOnlyFixSoftReferences)
		{
			bSoftReferencesOnly = false;
		}
	}

	// Warn the user if they are about to rename an asset that is referenced by a CDO
	TArray<TWeakObjectPtr<UObject>> CDOAssets = FindCDOReferencedAssets(AssetsToRename);

	// Warn the user if there were any references
	if (CDOAssets.Num())
	{
		FString AssetNames;
		for (auto AssetIt = CDOAssets.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			UObject* Asset = (*AssetIt).Get();
			if (Asset)
			{
				AssetNames += FString("\n") + Asset->GetName();
			}
		}

		const FText MessageText = FText::Format(LOCTEXT("RenameCDOReferences", "The following assets are referenced by one or more Class Default Objects: \n{0}\n\nContinuing with the rename may require code changes to fix these references. Do you wish to continue?"), FText::FromString(AssetNames));
		if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No, MessageText) == EAppReturnType::No)
		{
			return false;
		}
	}

	// Fill out the referencers for the assets we are renaming
	PopulateAssetReferencers(AssetsToRename);

	// Update the source control state for the packages containing the assets we are renaming if source control is enabled. If source control is enabled and this fails we can not continue.
	if (bSoftReferencesOnly || UpdatePackageStatus(AssetsToRename))
	{
		// Detect whether the assets are being referenced by a collection. Assets within a collection must leave a redirector to avoid the collection losing its references.
		DetectReferencingCollections(AssetsToRename);

		// Load all referencing packages and mark any assets that must have redirectors.
		TArray<UPackage*> ReferencingPackagesToSave;
		TArray<UObject*> SoftReferencingObjects;
		LoadReferencingPackages(AssetsToRename, bSoftReferencesOnly, true, ReferencingPackagesToSave, SoftReferencingObjects);

		// Prompt to check out source package and all referencing packages, leave redirectors for assets referenced by packages that are not checked out and remove those packages from the save list.
		const bool bUserAcceptedCheckout = CheckOutPackages(AssetsToRename, ReferencingPackagesToSave, bAutoCheckout);

		if (bUserAcceptedCheckout || bSoftReferencesOnly)
		{
			// If any referencing packages are left read-only, the checkout failed or SCC was not enabled. Trim them from the save list and leave redirectors.
			DetectReadOnlyPackages(AssetsToRename, ReferencingPackagesToSave);

			if (bSoftReferencesOnly)
			{
				if (ReferencingPackagesToSave.Num() > 0)
				{
					// Only do the rename if there are actually packages with references
					PerformAssetRename(AssetsToRename);

					for (const FAssetRenameDataWithReferencers& RenameData : AssetsToRename)
					{
						// Add source and destination packages so those get saved at the same time
						UPackage* OldPackage = FindPackage(nullptr, *RenameData.OldObjectPath.GetLongPackageName());
						UPackage* NewPackage = FindPackage(nullptr, *RenameData.NewObjectPath.GetLongPackageName());

						ReferencingPackagesToSave.AddUnique(OldPackage);
						ReferencingPackagesToSave.AddUnique(NewPackage);
					}

					FString AssetNames;
					for (UPackage* PackageToSave : ReferencingPackagesToSave)
					{
						AssetNames += FString("\n") + PackageToSave->GetName();
					}

					// Warn user before saving referencing packages
					bool bAgreedToSaveReferencingPackages = bAutoCheckout;
					if (!bAgreedToSaveReferencingPackages)
					{
						const FText MessageText = FText::Format(LOCTEXT("SoftReferenceFixedUp", "The following packages were fixed up because they have soft references to a renamed object: \n{0}\n\nDo you want to save them now?\nIf you quit without saving references will be broken!"), FText::FromString(AssetNames));
						bAgreedToSaveReferencingPackages = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes, MessageText) == EAppReturnType::Yes;
					}

					if (bAgreedToSaveReferencingPackages)
					{
						SaveReferencingPackages(ReferencingPackagesToSave);
					}
				}
			}
			else
			{
				// Perform the rename, leaving redirectors only for assets which need them
				PerformAssetRename(AssetsToRename);

				// Save all packages that were referencing any of the assets that were moved without redirectors
				SaveReferencingPackages(ReferencingPackagesToSave);

				// Issue post rename event
				AssetPostRenameEvent.Broadcast(AssetsAndNames);
			}
		}
	}

	// Finally, report any failures that happened during the rename
	return ReportFailures(AssetsToRename, bWithDialog) == 0;
}

TArray<TWeakObjectPtr<UObject>> FAssetRenameManager::FindCDOReferencedAssets(const TArray<FAssetRenameDataWithReferencers>& AssetsToRename) const
{
	TArray<TWeakObjectPtr<UObject>> CDOAssets, LocalAssetsToRename;
	for (const FAssetRenameDataWithReferencers& AssetToRename : AssetsToRename)
	{
		if (AssetToRename.Asset.IsValid())
		{
			LocalAssetsToRename.Push(AssetToRename.Asset);
		}
	}

	// Run over all CDOs and check for any references to the assets
	for (TObjectIterator<UClass> ClassDefaultObjectIt; ClassDefaultObjectIt; ++ClassDefaultObjectIt)
	{
		UClass* Cls = (*ClassDefaultObjectIt);
		UObject* CDO = Cls->ClassDefaultObject;

		if (!CDO || !CDO->HasAllFlags(RF_ClassDefaultObject) || Cls->ClassGeneratedBy != nullptr)
		{
			continue;
		}

		// Ignore deprecated and temporary trash classes.
		if (Cls->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) || FKismetEditorUtilities::IsClassABlueprintSkeleton(Cls))
		{
			continue;
		}

		for (TFieldIterator<FObjectProperty> PropertyIt(Cls); PropertyIt; ++PropertyIt)
		{
			const UObject* Object = PropertyIt->GetPropertyValue(PropertyIt->ContainerPtrToValuePtr<UObject>(CDO));
			for (const TWeakObjectPtr<UObject> Asset : LocalAssetsToRename)
			{
				if (Object == Asset.Get())
				{
					CDOAssets.Push(Asset);
					LocalAssetsToRename.Remove(Asset);

					if (LocalAssetsToRename.Num() == 0)
					{
						// No more assets to check
						return MoveTemp(CDOAssets);
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	return MoveTemp(CDOAssets);
}

void FAssetRenameManager::PopulateAssetReferencers(TArray<FAssetRenameDataWithReferencers>& AssetsToPopulate) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TSet<FName> RenamingAssetPackageNames;

	// Get the names of all the packages containing the assets we are renaming so they arent added to the referencing packages list
	for (FAssetRenameDataWithReferencers& AssetToRename : AssetsToPopulate)
	{
		// If we're only fixing soft references we want to check for references inside the original package as we don't save the original package automatically
		if (!AssetToRename.bOnlyFixSoftReferences)
		{
			RenamingAssetPackageNames.Add(FName(*AssetToRename.OldObjectPath.GetLongPackageName()));
		}
	}

	TMap<FName, TArray<FName>> SoftReferencers;
	TMap<FName, TArray<FName>> PackageReferencers;

	TArray<UPackage*> ExtraPackagesToCheckForSoftReferences;
	FEditorFileUtils::GetDirtyWorldPackages(ExtraPackagesToCheckForSoftReferences);
	FEditorFileUtils::GetDirtyContentPackages(ExtraPackagesToCheckForSoftReferences);

	// Gather all referencing packages for all assets that are being renamed
	for (FAssetRenameDataWithReferencers& AssetToRename : AssetsToPopulate)
	{
		AssetToRename.ReferencingPackageNames.Empty();

		FName OldPackageName = FName(*AssetToRename.OldObjectPath.GetLongPackageName());

		TMap<FName, TArray<FName>>& ReferencersMap = AssetToRename.bOnlyFixSoftReferences ? SoftReferencers : PackageReferencers;
		if (!ReferencersMap.Contains(OldPackageName))
		{
			TArray<FName>& Referencers = ReferencersMap.Add(OldPackageName);
			AssetRegistryModule.Get().GetReferencers(OldPackageName, Referencers, AssetToRename.bOnlyFixSoftReferences ? EAssetRegistryDependencyType::Soft : EAssetRegistryDependencyType::Packages);
		}

		for (const FName& ReferencingPackageName : ReferencersMap.FindChecked(OldPackageName))
		{
			if (!RenamingAssetPackageNames.Contains(ReferencingPackageName))
			{
				AssetToRename.ReferencingPackageNames.AddUnique(ReferencingPackageName);
			}
		}

		if (AssetToRename.bOnlyFixSoftReferences)
		{
			AssetToRename.ReferencingPackageNames.AddUnique(FName(*AssetToRename.OldObjectPath.GetLongPackageName()));
			AssetToRename.ReferencingPackageNames.AddUnique(FName(*AssetToRename.NewObjectPath.GetLongPackageName()));

			// Add dirty packages and the package that owns the reference. They will get filtered out in LoadReferencingPackages if they aren't valid
			for (UPackage* Package : ExtraPackagesToCheckForSoftReferences)
			{
				AssetToRename.ReferencingPackageNames.AddUnique(Package->GetFName());
			}
		}
	}
}

bool FAssetRenameManager::UpdatePackageStatus(const TArray<FAssetRenameDataWithReferencers>& AssetsToRename) const
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

		// Update the source control server availability to make sure we can do the rename operation
		SourceControlProvider.Login();
		if (!SourceControlProvider.IsAvailable())
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SourceControl_ServerUnresponsive", "Source Control is unresponsive. Please check your connection and try again."));
			return false;
		}

		// Gather asset package names to update SCC states in a single SCC request
		TArray<UPackage*> PackagesToUpdate;
		for (auto AssetIt = AssetsToRename.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			UObject* Asset = (*AssetIt).Asset.Get();
			if (Asset)
			{
				PackagesToUpdate.AddUnique(Asset->GetOutermost());
			}
		}

		SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), PackagesToUpdate);
	}

	return true;
}

void FAssetRenameManager::LoadReferencingPackages(TArray<FAssetRenameDataWithReferencers>& AssetsToRename, bool bLoadAllPackages, bool bCheckStatus, TArray<UPackage*>& OutReferencingPackagesToSave, TArray<UObject*>& OutSoftReferencingObjects) const
{
	const UBlueprintEditorProjectSettings* EditorProjectSettings = GetDefault<UBlueprintEditorProjectSettings>();
	bool bLoadPackagesForSoftReferences = EditorProjectSettings->bValidateUnloadedSoftActorReferences;
	bool bStartedSlowTask = false;
	const FText ReferenceUpdateSlowTask = LOCTEXT("ReferenceUpdateSlowTask", "Updating Asset References");

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	for (int32 AssetIdx = 0; AssetIdx < AssetsToRename.Num(); ++AssetIdx)
	{
		if (bStartedSlowTask)
		{
			GWarn->StatusUpdate(AssetIdx, AssetsToRename.Num(), ReferenceUpdateSlowTask);
		}
		
		FAssetRenameDataWithReferencers& RenameData = AssetsToRename[AssetIdx];

		UObject* Asset = RenameData.Asset.Get();
		if (Asset)
		{
			// Make sure this asset is local. Only local assets should be renamed without a redirector
			if (bCheckStatus)
			{
				FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Asset->GetOutermost(), EStateCacheUsage::ForceUpdate);
				const bool bLocalFile = !SourceControlState.IsValid() || SourceControlState->IsLocal();
				if (!bLocalFile)
				{
					if (SourceControlState->IsSourceControlled())
					{
						// If this asset is locked or not current, mark it failed to prevent it from being renamed
						if (SourceControlState->IsCheckedOutOther())
						{
							RenameData.bRenameFailed = true;
							RenameData.FailureReason = LOCTEXT("RenameFailedCheckedOutByOther", "Checked out by another user.");
						}
						else if (!SourceControlState->IsCurrent())
						{
							RenameData.bRenameFailed = true;
							RenameData.FailureReason = LOCTEXT("RenameFailedNotCurrent", "Out of date.");
						}
					}

					// This asset is not local. It is not safe to rename it without leaving a redirector
					RenameData.bCreateRedirector = true;
					if (!bLoadAllPackages)
					{
						continue;
					}
				}
			}
		}
		else
		{
			// The asset for this rename must have been GCed or is otherwise invalid. Skip it unless this is a soft reference only fix
			if (!bLoadAllPackages)
			{
				continue;
			}
		}

		TMap<FSoftObjectPath, FSoftObjectPath> ModifiedPaths;
		ModifiedPaths.Add(RenameData.OldObjectPath, RenameData.NewObjectPath);

		TArray<UPackage*> PackagesToSaveForThisAsset;
		bool bAllPackagesLoadedForThisAsset = true;
		for (int32 i = 0; i < RenameData.ReferencingPackageNames.Num(); i++)
		{
			FName PackageName = RenameData.ReferencingPackageNames[i];
			// Check if the package is a map before loading it!
			if (!bLoadAllPackages && FEditorFileUtils::IsMapPackageAsset(PackageName.ToString()))
			{
				// This reference was a map package, don't load it and leave a redirector for this asset
				// For subobjects we want to load maps packages and treat them normally
				RenameData.bCreateRedirector = true;
				bAllPackagesLoadedForThisAsset = false;
				break;
			}
			UPackage* Package = FindPackage(nullptr, *PackageName.ToString());

			// Don't load package if this is a soft reference fix and the project settings say not to
			if (!Package && (!RenameData.bOnlyFixSoftReferences || bLoadPackagesForSoftReferences))
			{
				if(!bStartedSlowTask)
				{
					bStartedSlowTask = true;
					GWarn->BeginSlowTask(ReferenceUpdateSlowTask, true);
				}
				Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);
			}

			if (Package)
			{				
				bool bFoundSoftReference = CheckPackageForSoftObjectReferences(Package, ModifiedPaths, OutSoftReferencingObjects);

				// Only add to list if we're doing a hard reference fixup or we found a soft reference
				bool bAdd = !RenameData.bOnlyFixSoftReferences || bFoundSoftReference;

				if (bAdd)
				{
					PackagesToSaveForThisAsset.Add(Package);
				}
				else
				{
					// This package does not actually reference the asset, so remove it
					RenameData.ReferencingPackageNames.RemoveAt(i);
					i--;
				}
			}
			else
			{
				RenameData.bCreateRedirector = true;
				if (!bLoadAllPackages)
				{
					bAllPackagesLoadedForThisAsset = false;
					break;
				}
			}
		}

		if (bAllPackagesLoadedForThisAsset)
		{
			for (UPackage* Package : PackagesToSaveForThisAsset)
			{
				OutReferencingPackagesToSave.AddUnique(Package);
			}
		}
	}

	if (bStartedSlowTask)
	{
		GWarn->EndSlowTask();
	}
}

void FAssetRenameManager::GatherReferencingObjects(TArray<FAssetRenameDataWithReferencers>& AssetsToRename, TMap<FSoftObjectPath, TArray<UObject*>>& OutSoftReferencingObjects) const
{
	const UBlueprintEditorProjectSettings* EditorProjectSettings = GetDefault<UBlueprintEditorProjectSettings>();
	bool bLoadPackagesForSoftReferences = EditorProjectSettings->bValidateUnloadedSoftActorReferences;

	TMap<UPackage*, TMap<FSoftObjectPath, FSoftObjectPath>> ReferencingPackages;

	for (int32 AssetIdx = 0; AssetIdx < AssetsToRename.Num(); ++AssetIdx)
	{
		FAssetRenameDataWithReferencers& RenameData = AssetsToRename[AssetIdx];

		UObject* Asset = RenameData.Asset.Get();
		if (!Asset)
		{
			// The asset for this rename must have been GCed or is otherwise invalid. Skip it unless this is a soft reference only fix
			continue;
		}

		for (FName PackageName : RenameData.ReferencingPackageNames)
		{
			UPackage* Package = FindPackage(nullptr, *PackageName.ToString());

			// Don't load package if this is a soft reference fix and the project settings say not to
			if (!Package && (!RenameData.bOnlyFixSoftReferences || bLoadPackagesForSoftReferences))
			{
				Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);
			}

			if (Package)
			{
				ReferencingPackages.FindOrAdd(Package).Add(RenameData.OldObjectPath, RenameData.NewObjectPath);
			}
		}
	}

	TArray<UPackage*> PackagesToSaveForThisAsset;
	bool bAllPackagesLoadedForThisAsset = true;
	for (const auto& ReferencingPackage : ReferencingPackages)
	{
		CheckPackageForSoftObjectReferences(ReferencingPackage.Key, ReferencingPackage.Value, OutSoftReferencingObjects);
	}
}

bool FAssetRenameManager::CheckOutPackages(TArray<FAssetRenameDataWithReferencers>& AssetsToRename, TArray<UPackage*>& InOutReferencingPackagesToSave, bool bAutoCheckout) const
{
	bool bUserAcceptedCheckout = true;

	// Build list of packages to check out: the source package and any referencing packages (in the case that we do not create a redirector)
	TArray<UPackage*> PackagesToCheckOut;
	PackagesToCheckOut.Reset(AssetsToRename.Num() + InOutReferencingPackagesToSave.Num());

	for (const FAssetRenameDataWithReferencers& AssetToRename : AssetsToRename)
	{
		if (!AssetToRename.bRenameFailed && AssetToRename.Asset.IsValid())
		{
			PackagesToCheckOut.Add(AssetToRename.Asset->GetOutermost());
		}
	}

	for (UPackage* ReferencingPackage : InOutReferencingPackagesToSave)
	{
		PackagesToCheckOut.Add(ReferencingPackage);
	}

	// Check out the packages
	if (PackagesToCheckOut.Num() > 0)
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			TArray<UPackage*> PackagesCheckedOutOrMadeWritable;
			TArray<UPackage*> PackagesNotNeedingCheckout;
			bUserAcceptedCheckout = bAutoCheckout ? AutoCheckOut(PackagesToCheckOut) : FEditorFileUtils::PromptToCheckoutPackages(false, PackagesToCheckOut, &PackagesCheckedOutOrMadeWritable, &PackagesNotNeedingCheckout);
			if (bUserAcceptedCheckout)
			{
				// Make a list of any packages in the list which weren't checked out for some reason
				TArray<UPackage*> PackagesThatCouldNotBeCheckedOut = PackagesToCheckOut;

				for (UPackage* Package : PackagesCheckedOutOrMadeWritable)
				{
					PackagesThatCouldNotBeCheckedOut.RemoveSwap(Package);
				}

				for (UPackage* Package : PackagesNotNeedingCheckout)
				{
					PackagesThatCouldNotBeCheckedOut.RemoveSwap(Package);
				}

				// If there's anything which couldn't be checked out, abort the operation.
				if (PackagesThatCouldNotBeCheckedOut.Num() > 0)
				{
					bUserAcceptedCheckout = false;
				}
			}
		}
		else
		{
			TArray<FString> PackageFilenames = USourceControlHelpers::PackageFilenames(PackagesToCheckOut);
			for (const FString& PackageFilename : PackageFilenames)
			{
				// If the file exist but readonly, do not allow the rename.
				if (IFileManager::Get().FileExists(*PackageFilename) && IFileManager::Get().IsReadOnly(*PackageFilename))
				{
					bUserAcceptedCheckout = false;
					break;
				}
			}
		}
	}

	return bUserAcceptedCheckout;
}

bool FAssetRenameManager::AutoCheckOut(TArray<UPackage*>& PackagesToCheckOut) const
{
	bool bSomethingFailed = false;
	if (PackagesToCheckOut.Num() > 0)
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		ECommandResult::Type StatusResult = SourceControlProvider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), PackagesToCheckOut);

		if (StatusResult != ECommandResult::Succeeded)
		{
			bSomethingFailed = true;
		}
		else
		{
			for (int32 Index = PackagesToCheckOut.Num() - 1; Index >= 0; --Index)
			{
				UPackage* Package = PackagesToCheckOut[Index];
				FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Package, EStateCacheUsage::Use);
				if (SourceControlState->IsCheckedOutOther())
				{
					UE_LOG(LogAssetTools, Warning, TEXT("FAssetRenameManager::AutoCheckOut: package %s is already checked out by someone, will not check out"), *SourceControlState->GetFilename());
					bSomethingFailed = true;
				}
				else if (!SourceControlState->IsCurrent())
				{
					UE_LOG(LogAssetTools, Warning, TEXT("FAssetRenameManager::AutoCheckOut: package %s is not at head, will not check out"), *SourceControlState->GetFilename());
					bSomethingFailed = true;
				}
				else if (!SourceControlState->IsSourceControlled() || SourceControlState->CanEdit())
				{
					PackagesToCheckOut.RemoveAtSwap(Index);
				}
			}

			if (!bSomethingFailed && PackagesToCheckOut.Num() > 0)
			{
				bSomethingFailed = (SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), PackagesToCheckOut) != ECommandResult::Succeeded);
				if (!bSomethingFailed)
				{
					UE_LOG(LogAssetTools, Warning, TEXT("FAssetRenameManager::AutoCheckOut: was not not able to auto checkout."));
					PackagesToCheckOut.Empty();
				}
			}
		}
	}

	return !bSomethingFailed;
}

void FAssetRenameManager::DetectReferencingCollections(TArray<FAssetRenameDataWithReferencers>& AssetsToRename) const
{
	FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

	for (FAssetRenameDataWithReferencers& AssetToRename : AssetsToRename)
	{
		if (AssetToRename.Asset.IsValid())
		{
			TArray<FCollectionNameType> ReferencingCollections;
			CollectionManagerModule.Get().GetCollectionsContainingObject(*AssetToRename.Asset->GetPathName(), ReferencingCollections);

			if (ReferencingCollections.Num() > 0)
			{
				AssetToRename.bCreateRedirector = true;
			}
		}
	}
}

void FAssetRenameManager::DetectReadOnlyPackages(TArray<FAssetRenameDataWithReferencers>& AssetsToRename, TArray<UPackage*>& InOutReferencingPackagesToSave) const
{
	// For each valid package...
	for (int32 PackageIdx = InOutReferencingPackagesToSave.Num() - 1; PackageIdx >= 0; --PackageIdx)
	{
		UPackage* Package = InOutReferencingPackagesToSave[PackageIdx];

		if (Package)
		{
			// Find the package filename
			FString Filename;
			if (FPackageName::DoesPackageExist(Package->GetName(), nullptr, &Filename))
			{
				// If the file is read only
				if (IFileManager::Get().IsReadOnly(*Filename))
				{
					FName PackageName = Package->GetFName();

					// Find all assets that were referenced by this package to create a redirector when named
					for (FAssetRenameDataWithReferencers& RenameData : AssetsToRename)
					{
						if (RenameData.ReferencingPackageNames.Contains(PackageName))
						{
							RenameData.bCreateRedirector = true;
						}
					}

					// Remove the package from the save list
					InOutReferencingPackagesToSave.RemoveAt(PackageIdx);
				}
			}
		}
	}
}

struct FSoftObjectPathRenameSerializer : public FArchiveUObject
{
	void StartSerializingObject(UObject* InCurrentObject)
	{ 
		CurrentObject = InCurrentObject;
		bFoundReference = false; 
	}
	bool HasFoundReference() const 
	{ 
		return bFoundReference; 
	}

	FSoftObjectPathRenameSerializer(const TMap<FSoftObjectPath, FSoftObjectPath>& InRedirectorMap, bool bInCheckOnly, TMap<FSoftObjectPath, TSet<FWeakObjectPtr>>* InCachedObjectPaths, const FName InPackageName = NAME_None)
		: RedirectorMap(InRedirectorMap)
		, CachedObjectPaths(InCachedObjectPaths)
		, CurrentObject(nullptr)
		, PackageName(InPackageName)
		, bSearchOnly(bInCheckOnly)
		, bFoundReference(false)
	{
		if (InCachedObjectPaths)
		{
			DirtyDelegateHandle = UPackage::PackageMarkedDirtyEvent.AddRaw(this, &FSoftObjectPathRenameSerializer::OnMarkPackageDirty);
		}

		this->ArIsObjectReferenceCollector = true;
		this->ArIsModifyingWeakAndStrongReferences = true;

		// Mark it as saving to correctly process all references
		this->SetIsSaving(true);
	}

	virtual ~FSoftObjectPathRenameSerializer()
	{
		UPackage::PackageMarkedDirtyEvent.Remove(DirtyDelegateHandle);
	}

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		if (InProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_IsPlainOldData))
		{
			return true;
		}

		FFieldClass* PropertyClass = InProperty->GetClass();
		if (PropertyClass->GetCastFlags() & (CASTCLASS_FBoolProperty | CASTCLASS_FNameProperty | CASTCLASS_FStrProperty | CASTCLASS_FMulticastDelegateProperty))
		{
			return true;
		}

		if (PropertyClass->GetCastFlags() & (CASTCLASS_FArrayProperty | CASTCLASS_FMapProperty | CASTCLASS_FSetProperty))
		{
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
			{
				return ShouldSkipProperty(ArrayProperty->Inner);
			}
			else if (const FMapProperty* MapProperty = CastField<FMapProperty>(InProperty))
			{
				return ShouldSkipProperty(MapProperty->KeyProp) && ShouldSkipProperty(MapProperty->ValueProp);
			}
			else if (const FSetProperty* SetProperty = CastField<FSetProperty>(InProperty))
			{
				return ShouldSkipProperty(SetProperty->ElementProp);
			}
		}

		return false;
	}

	FArchive& operator<<(FSoftObjectPath& Value)
	{
		using namespace AssetRenameManagerImpl;

		// Ignore untracked references if just doing a search only. We still want to fix them up if they happen to be there
		if (bSearchOnly)
		{
			FSoftObjectPathThreadContext& ThreadContext = FSoftObjectPathThreadContext::Get();
			FName ReferencingPackageName, ReferencingPropertyName;
			ESoftObjectPathCollectType CollectType = ESoftObjectPathCollectType::AlwaysCollect;
			ESoftObjectPathSerializeType SerializeType = ESoftObjectPathSerializeType::AlwaysSerialize;

			ThreadContext.GetSerializationOptions(ReferencingPackageName, ReferencingPropertyName, CollectType, SerializeType, this);

			if (CollectType == ESoftObjectPathCollectType::NeverCollect)
			{
				return *this;
			}
		}

		if (CachedObjectPaths)
		{
			TSet<FWeakObjectPtr>* ObjectSet = CachedObjectPaths->Find(Value);
			if (ObjectSet == nullptr)
			{
				ObjectSet = &CachedObjectPaths->Add(Value);
			}
			ObjectSet->Add(CurrentObject);
		}

		const FString& SubPath = Value.GetSubPathString();
		for (const TPair<FSoftObjectPath, FSoftObjectPath>& Pair : RedirectorMap)
		{
			if (Pair.Key.GetAssetPathName() == Value.GetAssetPathName())
			{
				// Same asset, fix sub path. Asset will be fixed by normal serializePath call below
				const FString& CheckSubPath = Pair.Key.GetSubPathString();

				if (IsSubPath(SubPath, CheckSubPath))
				{
					bFoundReference = true;

					if (!bSearchOnly)
					{
						if (CurrentObject)
						{
							check(!CachedObjectPaths); // Modify can invalidate the object paths map, not allowed to be modifying and using the cache at the same time
							CurrentObject->Modify(true);
						}

						FString NewSubPath(SubPath);
						NewSubPath.ReplaceInline(*CheckSubPath, *Pair.Value.GetSubPathString());
						Value = FSoftObjectPath(Pair.Value.GetAssetPathName(), NewSubPath);
					}
					break;
				}
			}
		}

		return *this;
	}

	void OnMarkPackageDirty(UPackage* Pkg, bool bWasDirty)
	{
		UPackage::PackageMarkedDirtyEvent.Remove(DirtyDelegateHandle);

		if (CachedObjectPaths && Pkg && Pkg->GetFName() == PackageName)
		{
			UE_LOG(LogAssetTools, VeryVerbose, TEXT("Performance: Package unexpectedly modified during serialization by FSoftObjectPathRenameSerializer: %s"), *Pkg->GetFullName());
		}
	}

private:
	const TMap<FSoftObjectPath, FSoftObjectPath>& RedirectorMap;
	TMap<FSoftObjectPath, TSet<FWeakObjectPtr>>* CachedObjectPaths;
	FDelegateHandle DirtyDelegateHandle;
	UObject* CurrentObject;
	FName PackageName;
	bool bSearchOnly;
	bool bFoundReference;

};

void FAssetRenameManager::RenameReferencingSoftObjectPaths(const TArray<UPackage *> PackagesToCheck, const TMap<FSoftObjectPath, FSoftObjectPath>& AssetRedirectorMap) const
{
	// Add redirects as needed
	for (const TPair<FSoftObjectPath, FSoftObjectPath>& Pair : AssetRedirectorMap)
	{
		if (Pair.Key.IsAsset())
		{
			GRedirectCollector.AddAssetPathRedirection(Pair.Key.GetAssetPathName(), Pair.Value.GetAssetPathName());
		}
	}

	FSoftObjectPathRenameSerializer RenameSerializer(AssetRedirectorMap, false, nullptr);

	for (UPackage* Package : PackagesToCheck)
	{
		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithOuter(Package, ObjectsInPackage);

		for (UObject* Object : ObjectsInPackage)
		{
			if (Object->IsPendingKill())
			{
				continue;
			}

			RenameSerializer.StartSerializingObject(Object);
			Object->Serialize(RenameSerializer);

			if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
			{
				// Serialize may have dirtied the BP bytecode in some way
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}
		}
	}

	// Invalidate the soft object tag as we have created new valid paths
	FSoftObjectPath::InvalidateTag();
}

void FAssetRenameManager::OnMarkPackageDirty(UPackage* Pkg, bool bWasDirty)
{
	// Remove from cache
	CachedSoftReferences.Remove(Pkg->GetFName());
}

bool FAssetRenameManager::CheckPackageForSoftObjectReferences(UPackage* Package, const TMap<FSoftObjectPath, FSoftObjectPath>& AssetRedirectorMap, TArray<UObject*>& OutReferencingObjects) const
{	
	TMap<FSoftObjectPath, TArray<UObject*>> ReferencingObjectsMap;
	
	CheckPackageForSoftObjectReferences(Package, AssetRedirectorMap, ReferencingObjectsMap);

	// Build an array out of the map results.
	for (const auto& It : ReferencingObjectsMap)
	{
		for (UObject* Obj : It.Value)
		{
			OutReferencingObjects.AddUnique(Obj);
		}
	}
	return OutReferencingObjects.Num() != 0;
}

bool FAssetRenameManager::CheckPackageForSoftObjectReferences(UPackage* Package, const TMap<FSoftObjectPath, FSoftObjectPath>& AssetRedirectorMap, TMap<FSoftObjectPath, TArray<UObject*>>& OutReferencingObjects) const
{
	using namespace AssetRenameManagerImpl;

	bool bFoundReference = false;

	// First check cache
	FCachedSoftReference* CachedReferences = CachedSoftReferences.Find(Package->GetFName());

	if (CachedReferences == nullptr)
	{
		// Bind to dirty callback if we aren't already
		if (!DirtyDelegateHandle.IsValid())
		{
			DirtyDelegateHandle = UPackage::PackageMarkedDirtyEvent.AddSP(const_cast<FAssetRenameManager*>(this), &FAssetRenameManager::OnMarkPackageDirty);
		}

		//Extract all objects soft references along with their referencer and cache them to avoid having to serialize again
		TMap<FSoftObjectPath, FSoftObjectPath> EmptyMap;
		TMap<FSoftObjectPath, TSet<FWeakObjectPtr>> MapForCache;
		FSoftObjectPathRenameSerializer CheckSerializer(EmptyMap, true, &MapForCache, Package->GetFName());

		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithOuter(Package, ObjectsInPackage);

		for (UObject* Object : ObjectsInPackage)
		{
			if (Object->IsPendingKill())
			{
				continue;
			}

			CheckSerializer.StartSerializingObject(Object);
			Object->Serialize(CheckSerializer);
		}

		CachedReferences = &CachedSoftReferences.Add(Package->GetFName());
		CachedReferences->Map = MoveTemp(MapForCache);

		CachedReferences->Map.GenerateKeyArray(CachedReferences->Keys);
		
		// Keys need to be sorted for binary search
		CachedReferences->Keys.Sort(FSoftObjectPathFastLess());
	}

	for (const TPair<FSoftObjectPath, FSoftObjectPath>& Pair : AssetRedirectorMap)
	{
		const FString& CheckSubPath = Pair.Key.GetSubPathString();

		// Find where we're going to start iterating
		int32 Index = Algo::LowerBound(CachedReferences->Keys, Pair.Key, FSoftObjectPathFastLess());
		for (int32 Num = CachedReferences->Keys.Num(); Index < Num; ++Index)
		{
			const FSoftObjectPath& CachedKey = CachedReferences->Keys[Index];
			const FString& SubPath = CachedKey.GetSubPathString();

			// Stop as soon as we're not anymore in the range we're searching
			if (Pair.Key.GetAssetPathName() != CachedKey.GetAssetPathName())
			{
				break;
			}

			// Check if CheckSubPath is included in SubPath first to handle this case:
			//
			// SubPath:      PersistentLevel.Level_1_4__Head_Level_300.Level_1_4__Head_Level_300
			//    which is >
			// CheckSubPath: PersistentLevel.Level_1_4__Head_Level_300
			//
			if (IsSubPath(SubPath, CheckSubPath))
			{
				bFoundReference = true;
				for (const FWeakObjectPtr& WeakPtr : *CachedReferences->Map.Find(CachedKey))
				{
					UObject* ObjectPtr = WeakPtr.Get();
					if (ObjectPtr)
					{
						OutReferencingObjects.FindOrAdd(CachedKey).AddUnique(ObjectPtr);
					}
				}
			}
			else if (SubPath > CheckSubPath)
			{
				// Stop once CheckSubPath is not included in SubPath anymore and we're out of search range
				break;
			}
		}
	}

	return bFoundReference;
}

void FAssetRenameManager::PerformAssetRename(TArray<FAssetRenameDataWithReferencers>& AssetsToRename) const
{
	const FText AssetRenameSlowTask = LOCTEXT("AssetRenameSlowTask", "Renaming Assets");
	GWarn->BeginSlowTask(AssetRenameSlowTask, true);

	/**
	 * We need to collect and check those cause dependency graph is only
	 * representing on-disk state and we want to support rename for in-memory
	 * objects. It is only needed for string references as in memory references
	 * for other objects are pointers, so renames doesn't apply to those.
	 */
	TArray<UPackage *> DirtyPackagesToCheckForSoftReferences;

	FEditorFileUtils::GetDirtyWorldPackages(DirtyPackagesToCheckForSoftReferences);
	FEditorFileUtils::GetDirtyContentPackages(DirtyPackagesToCheckForSoftReferences);

	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PotentialPackagesToDelete;
	for (int32 AssetIdx = 0; AssetIdx < AssetsToRename.Num(); ++AssetIdx)
	{
		GWarn->StatusUpdate(AssetIdx, AssetsToRename.Num(), AssetRenameSlowTask);

		FAssetRenameDataWithReferencers& RenameData = AssetsToRename[AssetIdx];

		if (RenameData.bRenameFailed)
		{
			// The rename failed at some earlier step, skip this asset
			continue;
		}

		UObject* Asset = RenameData.Asset.Get();
		TArray<UPackage *> PackagesToCheckForSoftReferences;

		if (!RenameData.bOnlyFixSoftReferences)
		{
			// If bOnlyFixSoftReferences was set these got appended in find references
			PackagesToCheckForSoftReferences.Append(DirtyPackagesToCheckForSoftReferences);

			if (!Asset)
			{
				// This asset was invalid or GCed before the rename could occur
				RenameData.bRenameFailed = true;
				continue;
			}

			ObjectTools::FPackageGroupName PGN;
			PGN.ObjectName = RenameData.NewName;
			PGN.GroupName = TEXT("");
			PGN.PackageName = RenameData.NewPackagePath / PGN.ObjectName;
			const bool bLeaveRedirector = RenameData.bCreateRedirector;

			UPackage* OldPackage = Asset->GetOutermost();
			bool bOldPackageAddedToRootSet = false;
			if (!bLeaveRedirector && !OldPackage->IsRooted())
			{
				bOldPackageAddedToRootSet = true;
				OldPackage->AddToRoot();
			}

			TSet<UPackage*> ObjectsUserRefusedToFullyLoad;
			FText ErrorMessage;
			if (ObjectTools::RenameSingleObject(Asset, PGN, ObjectsUserRefusedToFullyLoad, ErrorMessage, nullptr, bLeaveRedirector))
			{
				PackagesToSave.AddUnique(Asset->GetOutermost());

				// Automatically save renamed assets
				if (bLeaveRedirector)
				{
					PackagesToSave.AddUnique(OldPackage);
				}
				else if (bOldPackageAddedToRootSet)
				{
					// Since we did not leave a redirector and the old package wasnt already rooted, attempt to delete it when we are done. 
					PotentialPackagesToDelete.AddUnique(OldPackage);
				}
			}
			else
			{
				// No need to keep the old package rooted, the asset was never renamed out of it
				if (bOldPackageAddedToRootSet)
				{
					OldPackage->RemoveFromRoot();
				}

				// Mark the rename as a failure to report it later
				RenameData.bRenameFailed = true;
				RenameData.FailureReason = ErrorMessage;
			}
		}

		for (FName PackageName : RenameData.ReferencingPackageNames)
		{
			UPackage* PackageToCheck = FindPackage(nullptr, *PackageName.ToString());
			if (PackageToCheck)
			{
				PackagesToCheckForSoftReferences.Add(PackageToCheck);
			}
		}

		TMap<FSoftObjectPath, FSoftObjectPath> RedirectorMap;
		RedirectorMap.Add(RenameData.OldObjectPath, RenameData.NewObjectPath);

		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			// Add redirect for class and default as well
			RedirectorMap.Add(FString::Printf(TEXT("%s_C"), *RenameData.OldObjectPath.ToString()), FString::Printf(TEXT("%s_C"), *RenameData.NewObjectPath.ToString()));
			RedirectorMap.Add(FString::Printf(TEXT("%s.Default__%s_C"), *RenameData.OldObjectPath.GetLongPackageName(), *RenameData.OldObjectPath.GetAssetName()), FString::Printf(TEXT("%s.Default__%s_C"), *RenameData.NewObjectPath.GetLongPackageName(), *RenameData.NewObjectPath.GetAssetName()));
		}

		RenameReferencingSoftObjectPaths(PackagesToCheckForSoftReferences, RedirectorMap);
	}

	GWarn->EndSlowTask();

	// Save all renamed assets and any redirectors that were left behind
	if (PackagesToSave.Num() > 0)
	{
		const bool bCheckDirty = false;
		const bool bPromptToSave = false;
		const bool bAlreadyCheckedOut = true;
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave, nullptr, bAlreadyCheckedOut);

		ISourceControlModule::Get().QueueStatusUpdate(PackagesToSave);
	}

	// Now branch the files in source control if possible
	for (const FAssetRenameDataWithReferencers& RenameData : AssetsToRename)
	{
		UPackage* OldPackage = FindPackage(nullptr, *RenameData.OldObjectPath.GetLongPackageName());
		UPackage* NewPackage = FindPackage(nullptr, *RenameData.NewObjectPath.GetLongPackageName());

		// If something went wrong when saving and the new asset does not exist on disk, don't branch it
		// as it will just create a copy and any attempt to load it will result in crashes.
		if (!RenameData.bOnlyFixSoftReferences && NewPackage && FPackageName::DoesPackageExist(NewPackage->GetName()))
		{
			if (ISourceControlModule::Get().IsEnabled())
			{
				ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
				const FString SourceFilename = USourceControlHelpers::PackageFilename(OldPackage);
				FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceFilename, EStateCacheUsage::ForceUpdate);
				if (SourceControlState.IsValid() && SourceControlState->IsSourceControlled())
				{
					// Do not attempt to branch if the old file was open for add
					if (!SourceControlState->IsAdded())
					{
						SourceControlHelpers::BranchPackage(NewPackage, OldPackage);
					}
				}
			}
		}
	}

	// Clean up all packages that were left empty
	if (PotentialPackagesToDelete.Num() > 0)
	{
		for (UPackage* Package : PotentialPackagesToDelete)
		{
			Package->RemoveFromRoot();
		}

		ObjectTools::CleanupAfterSuccessfulDelete(PotentialPackagesToDelete);
	}
}

void FAssetRenameManager::SaveReferencingPackages(const TArray<UPackage*>& ReferencingPackagesToSave) const
{
	if (ReferencingPackagesToSave.Num() > 0)
	{
		const bool bCheckDirty = false;
		const bool bPromptToSave = false;
		FEditorFileUtils::PromptForCheckoutAndSave(ReferencingPackagesToSave, bCheckDirty, bPromptToSave);

		ISourceControlModule::Get().QueueStatusUpdate(ReferencingPackagesToSave);
	}
}

int32 FAssetRenameManager::ReportFailures(const TArray<FAssetRenameDataWithReferencers>& AssetsToRename, bool bWithDialog) const
{
	TArray<FText> FailedRenames;
	for (const FAssetRenameDataWithReferencers& RenameData : AssetsToRename)
	{
		if (RenameData.bRenameFailed)
		{
			UObject* Asset = RenameData.Asset.Get();
			if (Asset)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("FailureReason"), RenameData.FailureReason);
				Args.Add(TEXT("AssetName"), FText::FromString(Asset->GetOutermost()->GetName()));

				FailedRenames.Add(FText::Format(LOCTEXT("AssetRenameFailure", "{AssetName} - {FailureReason}"), Args));
			}
			else
			{
				FailedRenames.Add(LOCTEXT("InvalidAssetText", "Invalid Asset"));
			}
		}
	}

	if (FailedRenames.Num() > 0)
	{
		if (bWithDialog)
		{
			SRenameFailures::OpenRenameFailuresDialog(FailedRenames);
		}
		else
		{
			for (const FText FailedRename : FailedRenames)
			{
				UE_LOG(LogAssetTools, Error, TEXT("%s"), *FailedRename.ToString());
			}
		}
	}

	return FailedRenames.Num();
}

#undef LOCTEXT_NAMESPACE
