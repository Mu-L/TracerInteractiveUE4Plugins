// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintEditorUtils.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "UObject/PropertyPortFlags.h"
#include "Blueprint/WidgetTree.h"
#include "MovieScene.h"
#include "WidgetBlueprint.h"
#include "HAL/PlatformApplicationMisc.h"

#if WITH_EDITOR
	#include "Exporters/Exporter.h"
#endif // WITH_EDITOR
#include "ObjectEditorUtils.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Animation/WidgetAnimation.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Templates/WidgetTemplateClass.h"
#include "Templates/WidgetTemplateBlueprintClass.h"
#include "Factories.h"
#include "UnrealExporter.h"
#include "Framework/Commands/GenericCommands.h"
#include "ScopedTransaction.h"
#include "Components/CanvasPanel.h"
#include "Utility/WidgetSlotPair.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetNavigation.h"

#define LOCTEXT_NAMESPACE "UMG"

class FWidgetObjectTextFactory : public FCustomizableTextObjectFactory
{
public:
	FWidgetObjectTextFactory()
		: FCustomizableTextObjectFactory(GWarn)
	{
	}

	// FCustomizableTextObjectFactory implementation

	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		const bool bIsWidget = ObjectClass->IsChildOf(UWidget::StaticClass());
		const bool bIsSlot = ObjectClass->IsChildOf(UPanelSlot::StaticClass());
		const bool bIsSlotMetaData = ObjectClass->IsChildOf(UWidgetSlotPair::StaticClass());

		return bIsWidget || bIsSlot || bIsSlotMetaData;
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		check(NewObject);

		if ( UWidget* Widget = Cast<UWidget>(NewObject) )
		{
			NewWidgetMap.Add(Widget->GetFName(), Widget);
		}
		else if ( UWidgetSlotPair* SlotMetaData = Cast<UWidgetSlotPair>(NewObject) )
		{
			MissingSlotData.Add(SlotMetaData->GetWidgetName(), SlotMetaData);
		}
	}

	// FCustomizableTextObjectFactory (end)

public:

	// Name->Instance object mapping
	TMap<FName, UWidget*> NewWidgetMap;

	// Instance->OldSlotMetaData that didn't survive the journey because it wasn't copied.
	TMap<FName, UWidgetSlotPair*> MissingSlotData;
};

bool FWidgetBlueprintEditorUtils::VerifyWidgetRename(TSharedRef<class FWidgetBlueprintEditor> BlueprintEditor, FWidgetReference Widget, const FText& NewName, FText& OutErrorMessage)
{
	if (NewName.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("EmptyWidgetName", "Empty Widget Name");
		return false;
	}

	const FString& NewNameString = NewName.ToString();

	if (NewNameString.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("WidgetNameTooLong", "Widget Name is Too Long");
		return false;
	}

	UWidget* RenamedTemplateWidget = Widget.GetTemplate();
	if ( !RenamedTemplateWidget )
	{
		// In certain situations, the template might be lost due to mid recompile with focus lost on the rename box at
		// during a strange moment.
		return false;
	}

	// Slug the new name down to a valid object name
	const FName NewNameSlug = MakeObjectNameFromDisplayLabel(NewNameString, RenamedTemplateWidget->GetFName());

	UWidgetBlueprint* Blueprint = BlueprintEditor->GetWidgetBlueprintObj();
	UWidget* ExistingTemplate = Blueprint->WidgetTree->FindWidget(NewNameSlug);

	bool bIsSameWidget = false;
	if (ExistingTemplate != nullptr)
	{
		if ( RenamedTemplateWidget != ExistingTemplate )
		{
			OutErrorMessage = LOCTEXT("ExistingWidgetName", "Existing Widget Name");
			return false;
		}
		else
		{
			bIsSameWidget = true;
		}
	}
	else
	{
		// Not an existing widget in the tree BUT it still mustn't create a UObject name clash
		UWidget* WidgetPreview = Widget.GetPreview();
		if (WidgetPreview)
		{
			// Dummy rename with flag REN_Test returns if rename is possible
			if (!WidgetPreview->Rename(*NewNameSlug.ToString(), nullptr, REN_Test))
			{
				OutErrorMessage = LOCTEXT("ExistingObjectName", "Existing Object Name");
				return false;
			}
		}
		UWidget* WidgetTemplate = RenamedTemplateWidget;
		// Dummy rename with flag REN_Test returns if rename is possible
		if (!WidgetTemplate->Rename(*NewNameSlug.ToString(), nullptr, REN_Test))
		{
			OutErrorMessage = LOCTEXT("ExistingObjectName", "Existing Object Name");
			return false;
		}
	}

	UProperty* Property = Blueprint->ParentClass->FindPropertyByName( NewNameSlug );
	if ( Property && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property))
	{
		return true;
	}

	FKismetNameValidator Validator(Blueprint);

	// For variable comparison, use the slug
	EValidatorResult ValidatorResult = Validator.IsValid(NewNameSlug);

	if (ValidatorResult != EValidatorResult::Ok)
	{
		if (bIsSameWidget && (ValidatorResult == EValidatorResult::AlreadyInUse || ValidatorResult == EValidatorResult::ExistingName))
		{
			// Continue successfully
		}
		else
		{
			OutErrorMessage = INameValidatorInterface::GetErrorText(NewNameString, ValidatorResult);
			return false;
		}
	}

	return true;
}

bool FWidgetBlueprintEditorUtils::RenameWidget(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, const FName& OldObjectName, const FString& NewDisplayName)
{
	UWidgetBlueprint* Blueprint = BlueprintEditor->GetWidgetBlueprintObj();
	check(Blueprint);

	UWidget* Widget = Blueprint->WidgetTree->FindWidget(OldObjectName);
	check(Widget);

	UClass* ParentClass = Blueprint->ParentClass;
	check( ParentClass );

	bool bRenamed = false;

	TSharedPtr<INameValidatorInterface> NameValidator = MakeShareable(new FKismetNameValidator(Blueprint));

	// Get the new FName slug from the given display name
	const FName NewFName = MakeObjectNameFromDisplayLabel(NewDisplayName, Widget->GetFName());

	UObjectPropertyBase* ExistingProperty = Cast<UObjectPropertyBase>(ParentClass->FindPropertyByName(NewFName));
	const bool bBindWidget = ExistingProperty && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(ExistingProperty) && Widget->IsA(ExistingProperty->PropertyClass);

	// NewName should be already validated. But one must make sure that NewTemplateName is also unique.
	const bool bUniqueNameForTemplate = ( EValidatorResult::Ok == NameValidator->IsValid( NewFName ) || bBindWidget );
	if ( bUniqueNameForTemplate )
	{
		// Stringify the FNames
		const FString NewNameStr = NewFName.ToString();
		const FString OldNameStr = OldObjectName.ToString();

		const FScopedTransaction Transaction(LOCTEXT("RenameWidget", "Rename Widget"));

		// Rename Template
		Blueprint->Modify();
		Widget->Modify();

		// Rename Preview before renaming the template widget so the preview widget can be found
		UWidget* WidgetPreview = BlueprintEditor->GetReferenceFromTemplate(Widget).GetPreview();
		if(WidgetPreview)
		{
			WidgetPreview->SetDisplayLabel(NewDisplayName);
			WidgetPreview->Rename(*NewNameStr);
		}

		if (!WidgetPreview || WidgetPreview != Widget)
		{
			// Find and update all variable references in the graph
			Widget->SetDisplayLabel(NewDisplayName);
			Widget->Rename(*NewNameStr);
		}

		// Update Variable References and
		// Update Event References to member variables
		FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, OldObjectName, NewFName);
		
		// Find and update all binding references in the widget blueprint
		for ( FDelegateEditorBinding& Binding : Blueprint->Bindings )
		{
			if ( Binding.ObjectName == OldNameStr )
			{
				Binding.ObjectName = NewNameStr;
			}
		}

		// Update widget blueprint names
		for( UWidgetAnimation* WidgetAnimation : Blueprint->Animations )
		{
			for( FWidgetAnimationBinding& AnimBinding : WidgetAnimation->AnimationBindings )
			{
				if( AnimBinding.WidgetName == OldObjectName )
				{
					AnimBinding.WidgetName = NewFName;

					WidgetAnimation->MovieScene->Modify();

					if (AnimBinding.SlotWidgetName == NAME_None)
					{
						FMovieScenePossessable* Possessable = WidgetAnimation->MovieScene->FindPossessable(AnimBinding.AnimationGuid);
						if (Possessable)
						{
							Possessable->SetName(NewFName.ToString());
						}
					}
					else
					{
						break;
					}
				}
			}
		}

		// Update any explicit widget bindings.
		Blueprint->WidgetTree->ForEachWidget([OldObjectName, NewFName](UWidget* Widget) {
			if (Widget->Navigation)
			{
				Widget->Navigation->SetFlags(RF_Transactional);
				Widget->Navigation->Modify();
				Widget->Navigation->TryToRenameBinding(OldObjectName, NewFName);
			}
		});

		// Validate child blueprints and adjust variable names to avoid a potential name collision
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewFName);

		// Refresh references and flush editors
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		bRenamed = true;
	}

	return bRenamed;
}

void FWidgetBlueprintEditorUtils::CreateWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, FVector2D TargetLocation)
{
	BlueprintEditor->PasteDropLocation = TargetLocation;

	TSet<FWidgetReference> Widgets = BlueprintEditor->GetSelectedWidgets();
	UWidgetBlueprint* BP = BlueprintEditor->GetWidgetBlueprintObj();

	MenuBuilder.PushCommandList(BlueprintEditor->DesignerCommandList.ToSharedRef());

	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.PopCommandList();
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Actions");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT( "EditBlueprint_Label", "Edit Widget Blueprint..." ),
			LOCTEXT( "EditBlueprint_Tooltip", "Open the selected Widget Blueprint(s) for edit." ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic( &FWidgetBlueprintEditorUtils::ExecuteOpenSelectedWidgetsForEdit, Widgets ),
				FCanExecuteAction(),
				FIsActionChecked(),
				FIsActionButtonVisible::CreateStatic( &FWidgetBlueprintEditorUtils::CanOpenSelectedWidgetsForEdit, Widgets )
				)
			);

		MenuBuilder.AddSubMenu(
			LOCTEXT("WidgetTree_WrapWith", "Wrap With..."),
			LOCTEXT("WidgetTree_WrapWithToolTip", "Wraps the currently selected widgets inside of another container widget"),
			FNewMenuDelegate::CreateStatic(&FWidgetBlueprintEditorUtils::BuildWrapWithMenu, BlueprintEditor, BP, Widgets)
			);

		if ( Widgets.Num() == 1 )
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("WidgetTree_ReplaceWith", "Replace With..."),
				LOCTEXT("WidgetTree_ReplaceWithToolTip", "Replaces the currently selected widget, with another widget"),
				FNewMenuDelegate::CreateStatic(&FWidgetBlueprintEditorUtils::BuildReplaceWithMenu, BlueprintEditor, BP, Widgets)
				);
		}
	}
	MenuBuilder.EndSection();
}

void FWidgetBlueprintEditorUtils::ExecuteOpenSelectedWidgetsForEdit( TSet<FWidgetReference> SelectedWidgets )
{
	for ( auto& Widget : SelectedWidgets )
	{
		FAssetEditorManager::Get().OpenEditorForAsset( Widget.GetTemplate()->GetClass()->ClassGeneratedBy );
	}
}

bool FWidgetBlueprintEditorUtils::CanOpenSelectedWidgetsForEdit( TSet<FWidgetReference> SelectedWidgets )
{
	bool bCanOpenAllForEdit = SelectedWidgets.Num() > 0;
	for ( auto& Widget : SelectedWidgets )
	{
		auto Blueprint = Widget.GetTemplate()->GetClass()->ClassGeneratedBy;
		if ( !Blueprint || !Blueprint->IsA( UWidgetBlueprint::StaticClass() ) )
		{
			bCanOpenAllForEdit = false;
			break;
		}
	}

	return bCanOpenAllForEdit;
}

void FWidgetBlueprintEditorUtils::DeleteWidgets(UWidgetBlueprint* Blueprint, TSet<FWidgetReference> Widgets)
{
	if ( Widgets.Num() > 0 )
	{
		const FScopedTransaction Transaction(LOCTEXT("RemoveWidget", "Remove Widget"));
		Blueprint->WidgetTree->SetFlags(RF_Transactional);
		Blueprint->WidgetTree->Modify();
		Blueprint->Modify();

		bool bRemoved = false;
		for ( FWidgetReference& Item : Widgets )
		{
			UWidget* WidgetTemplate = Item.GetTemplate();
			WidgetTemplate->SetFlags(RF_Transactional);
			// Find and update all binding references in the widget blueprint
			for (int32 BindingIndex = Blueprint->Bindings.Num() - 1; BindingIndex >= 0; BindingIndex--)
			{
				FDelegateEditorBinding& Binding = Blueprint->Bindings[BindingIndex];
				if (Binding.ObjectName == WidgetTemplate->GetName())
				{
					Blueprint->Bindings.RemoveAt(BindingIndex);
				}
			}

			// Modify the widget's parent
			UPanelWidget* Parent = WidgetTemplate->GetParent();
			if ( Parent )
			{
				Parent->SetFlags(RF_Transactional);
				Parent->Modify();
			}
			
			// Modify the widget being removed.
			WidgetTemplate->Modify();

			bRemoved = Blueprint->WidgetTree->RemoveWidget(WidgetTemplate);

			// If the widget we're removing doesn't have a parent it may be rooted in a named slot,
			// so check there as well.
			if ( WidgetTemplate->GetParent() == nullptr )
			{
				bRemoved |= FindAndRemoveNamedSlotContent(WidgetTemplate, Blueprint->WidgetTree);
			}

			// Rename the removed widget to the transient package so that it doesn't conflict with future widgets sharing the same name.
			WidgetTemplate->Rename(nullptr, GetTransientPackage());

			// Rename all child widgets as well, to the transient package so that they don't conflict with future widgets sharing the same name.
			TArray<UWidget*> ChildWidgets;
			UWidgetTree::GetChildWidgets(WidgetTemplate, ChildWidgets);
			for ( UWidget* Widget : ChildWidgets )
			{
				Widget->SetFlags(RF_Transactional);
				Widget->Rename(nullptr, GetTransientPackage());
			}
		}

		//TODO UMG There needs to be an event for widget removal so that caches can be updated, and selection

		if ( bRemoved )
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}
}

INamedSlotInterface* FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(UWidget* WidgetTemplate, UWidgetTree* WidgetTree)
{
	return Cast<INamedSlotInterface>(FindNamedSlotHostWidgetForContent(WidgetTemplate, WidgetTree));
}

UWidget* FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(UWidget* WidgetTemplate, UWidgetTree* WidgetTree)
{
	UWidget* HostWidget = nullptr;

	WidgetTree->ForEachWidget([&](UWidget* Widget) {

		if (HostWidget != nullptr)
		{
			return;
		}

		if (INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Widget))
		{
			TArray<FName> SlotNames;
			NamedSlotHost->GetSlotNames(SlotNames);

			for (FName SlotName : SlotNames)
			{
				if (UWidget* SlotContent = NamedSlotHost->GetContentForSlot(SlotName))
				{
					if (SlotContent == WidgetTemplate)
					{
						HostWidget = Widget;
					}
				}
			}
		}
	});

	return HostWidget;
}

void FWidgetBlueprintEditorUtils::FindAllAncestorNamedSlotHostWidgetsForContent(TArray<FWidgetReference>& OutSlotHostWidgets, UWidget* WidgetTemplate, TSharedRef<FWidgetBlueprintEditor> BlueprintEditor)
{
	OutSlotHostWidgets.Empty();
	UUserWidget* Preview = BlueprintEditor->GetPreview();
	UWidgetBlueprint* WidgetBP = BlueprintEditor->GetWidgetBlueprintObj();
	UWidgetTree* WidgetTree = (WidgetBP != nullptr) ? WidgetBP->WidgetTree : nullptr;

	if (Preview != nullptr && WidgetTree != nullptr)
	{
		// Find the first widget up the chain with a null parent, they're the only candidates for this approach.
		while (WidgetTemplate && WidgetTemplate->GetParent())
		{
			WidgetTemplate = WidgetTemplate->GetParent();
		}

		UWidget* SlotHostWidget = FindNamedSlotHostWidgetForContent(WidgetTemplate, WidgetTree);
		while (SlotHostWidget != nullptr)
		{
			UWidget* SlotWidget = Preview->GetWidgetFromName(SlotHostWidget->GetFName());
			FWidgetReference WidgetRef;

			if (SlotWidget != nullptr)
			{
				WidgetRef = BlueprintEditor->GetReferenceFromPreview(SlotWidget);

				if (WidgetRef.IsValid())
				{
					OutSlotHostWidgets.Add(WidgetRef);
				}
			}

			WidgetTemplate = WidgetRef.GetTemplate();

			SlotHostWidget = nullptr;
			if (WidgetTemplate != nullptr)
			{
				// Find the first widget up the chain with a null parent, they're the only candidates for this approach.
				while (WidgetTemplate->GetParent())
				{
					WidgetTemplate = WidgetTemplate->GetParent();
				}

				SlotHostWidget = FindNamedSlotHostWidgetForContent(WidgetRef.GetTemplate(), WidgetTree);
			}
		}
	}
}

bool FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(UWidget* WidgetTemplate, INamedSlotInterface* NamedSlotHost)
{
	return ReplaceNamedSlotHostContent(WidgetTemplate, NamedSlotHost, nullptr);
}

bool FWidgetBlueprintEditorUtils::ReplaceNamedSlotHostContent(UWidget* WidgetTemplate, INamedSlotInterface* NamedSlotHost, UWidget* NewContentWidget)
{
	TArray<FName> SlotNames;
	NamedSlotHost->GetSlotNames(SlotNames);

	for (FName SlotName : SlotNames)
	{
		if (UWidget* SlotContent = NamedSlotHost->GetContentForSlot(SlotName))
		{
			if (SlotContent == WidgetTemplate)
			{
				NamedSlotHost->SetContentForSlot(SlotName, NewContentWidget);
				return true;
			}
		}
	}

	return false;
}

bool FWidgetBlueprintEditorUtils::FindAndRemoveNamedSlotContent(UWidget* WidgetTemplate, UWidgetTree* WidgetTree)
{
	UWidget* NamedSlotHostWidget = FindNamedSlotHostWidgetForContent(WidgetTemplate, WidgetTree);
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(NamedSlotHostWidget) )
	{
		NamedSlotHostWidget->Modify();
		return RemoveNamedSlotHostContent(WidgetTemplate, NamedSlotHost);
	}

	return false;
}

void FWidgetBlueprintEditorUtils::BuildWrapWithMenu(FMenuBuilder& Menu, TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	TArray<UClass*> WrapperClasses;
	for ( TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt )
	{
		UClass* WidgetClass = *ClassIt;
		if ( FWidgetBlueprintEditorUtils::IsUsableWidgetClass(WidgetClass) )
		{
			if ( WidgetClass->IsChildOf(UPanelWidget::StaticClass()) )
			{
				WrapperClasses.Add(WidgetClass);
			}
		}
	}

	WrapperClasses.Sort([] (UClass& Lhs, UClass& Rhs) { return Lhs.GetDisplayNameText().CompareTo(Rhs.GetDisplayNameText()) < 0; });

	Menu.BeginSection("WrapWith", LOCTEXT("WidgetTree_WrapWith", "Wrap With..."));
	{
		for ( UClass* WrapperClass : WrapperClasses )
		{
			Menu.AddMenuEntry(
				WrapperClass->GetDisplayNameText(),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::WrapWidgets, BlueprintEditor, BP, Widgets, WrapperClass),
					FCanExecuteAction()
				));
		}
	}
	Menu.EndSection();
}

void FWidgetBlueprintEditorUtils::WrapWidgets(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets, UClass* WidgetClass)
{
	const FScopedTransaction Transaction(LOCTEXT("WrapWidgets", "Wrap Widgets"));

	TSharedPtr<FWidgetTemplateClass> Template = MakeShareable(new FWidgetTemplateClass(WidgetClass));

	// Old Parent -> New Parent Map
	TMap<UPanelWidget*, UPanelWidget*> OldParentToNewParent;

	for (FWidgetReference& Item : Widgets)
	{
		int32 OutIndex;
		UWidget* Widget = Item.GetTemplate();
		UPanelWidget* CurrentParent = BP->WidgetTree->FindWidgetParent(Widget, OutIndex);
		UWidget* CurrentSlot = FindNamedSlotHostWidgetForContent(Widget, BP->WidgetTree);

		// If the widget doesn't currently have a slot or parent, and isn't the root, ignore it.
		if (CurrentSlot == nullptr && CurrentParent == nullptr && Widget != BP->WidgetTree->RootWidget)
		{
			continue;
		}

		Widget->Modify();
		BP->WidgetTree->SetFlags(RF_Transactional);
		BP->WidgetTree->Modify();

		if (CurrentSlot)
		{
			// If this is a named slot, we need to properly remove and reassign the slot content
			INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(CurrentSlot);
			if (NamedSlotHost != nullptr)
			{
				CurrentSlot->SetFlags(RF_Transactional);
				CurrentSlot->Modify();

				UPanelWidget* NewSlotContents = CastChecked<UPanelWidget>(Template->Create(BP->WidgetTree));
				NewSlotContents->SetDesignerFlags(BlueprintEditor->GetCurrentDesignerFlags());

				FWidgetBlueprintEditorUtils::ReplaceNamedSlotHostContent(Widget, NamedSlotHost, NewSlotContents);

				NewSlotContents->AddChild(Widget);

			}
		}
		else if (CurrentParent)
		{
			UPanelWidget*& NewWrapperWidget = OldParentToNewParent.FindOrAdd(CurrentParent);
			if (NewWrapperWidget == nullptr || !NewWrapperWidget->CanAddMoreChildren())
			{
				NewWrapperWidget = CastChecked<UPanelWidget>(Template->Create(BP->WidgetTree));
				NewWrapperWidget->SetDesignerFlags(BlueprintEditor->GetCurrentDesignerFlags());				

				CurrentParent->SetFlags(RF_Transactional);
				CurrentParent->Modify();
				CurrentParent->ReplaceChildAt(OutIndex, NewWrapperWidget);

				NewWrapperWidget->AddChild(Widget);
			}
		}
		else
		{
			UPanelWidget* NewRootContents = CastChecked<UPanelWidget>(Template->Create(BP->WidgetTree));
			NewRootContents->SetDesignerFlags(BlueprintEditor->GetCurrentDesignerFlags());

			BP->WidgetTree->RootWidget = NewRootContents;
			NewRootContents->AddChild(Widget);
		}

	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
}

void FWidgetBlueprintEditorUtils::BuildReplaceWithMenu(FMenuBuilder& Menu, TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	Menu.BeginSection("ReplaceWith", LOCTEXT("WidgetTree_ReplaceWith", "Replace With..."));
	{
		if ( Widgets.Num() == 1 )
		{
			FWidgetReference Widget = *Widgets.CreateIterator();
			UClass* WidgetClass = Widget.GetTemplate()->GetClass();
			TWeakObjectPtr<UClass> TemplateWidget = BlueprintEditor->GetSelectedTemplate();
			FAssetData SelectedUserWidget = BlueprintEditor->GetSelectedUserWidget();
			if (TemplateWidget.IsValid() || SelectedUserWidget.ObjectPath != NAME_None)
			{
				Menu.AddMenuEntry(
					FText::Format(LOCTEXT("WidgetTree_ReplaceWithSelection", "Replace With {0}"), FText::FromString(TemplateWidget.IsValid() ? TemplateWidget->GetName() : SelectedUserWidget.AssetName.ToString())),
					FText::Format(LOCTEXT("WidgetTree_ReplaceWithSelectionToolTip", "Replace this widget with a {0}"), FText::FromString(TemplateWidget.IsValid() ? TemplateWidget->GetName() : SelectedUserWidget.AssetName.ToString())),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::ReplaceWidgetWithSelectedTemplate, BlueprintEditor, BP, Widget),
						FCanExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::CanBeReplacedWithTemplate, BlueprintEditor, BP, Widget)
					));
				Menu.AddMenuSeparator();
			}

			if ( WidgetClass->IsChildOf(UPanelWidget::StaticClass()) && Cast<UPanelWidget>(Widget.GetTemplate())->GetChildrenCount() == 1 )
			{
				Menu.AddMenuEntry(
					LOCTEXT("ReplaceWithChild", "Replace With Child"),
					LOCTEXT("ReplaceWithChildTooltip", "Remove this widget and insert the children of this widget into the parent."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::ReplaceWidgetWithChildren, BlueprintEditor, BP, Widget),
						FCanExecuteAction()
					));

				Menu.AddMenuSeparator();
			}
		}

		TArray<UClass*> ReplacementClasses;
		for ( TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt )
		{
			UClass* WidgetClass = *ClassIt;
			if ( FWidgetBlueprintEditorUtils::IsUsableWidgetClass(WidgetClass) )
			{
				if ( WidgetClass->IsChildOf(UPanelWidget::StaticClass()) )
				{
					// Only allow replacement with panels that accept multiple children
					if ( WidgetClass->GetDefaultObject<UPanelWidget>()->CanHaveMultipleChildren() )
					{
						ReplacementClasses.Add(WidgetClass);
					}
				}
			}
		}

		ReplacementClasses.Sort([] (UClass& Lhs, UClass& Rhs) { return Lhs.GetDisplayNameText().CompareTo(Rhs.GetDisplayNameText()) < 0; });

		for ( UClass* ReplacementClass : ReplacementClasses )
		{
			Menu.AddMenuEntry(
				ReplacementClass->GetDisplayNameText(),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateStatic(&FWidgetBlueprintEditorUtils::ReplaceWidgets, BlueprintEditor, BP, Widgets, ReplacementClass)
				));
		}
	}
	Menu.EndSection();
}

void FWidgetBlueprintEditorUtils::ReplaceWidgetWithSelectedTemplate(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, FWidgetReference Widget)
{
	// @Todo: Needs to deal with bound object in animation tracks

	const FScopedTransaction Transaction(LOCTEXT("ReplaceWidgets", "Replace Widgets"));
	bool bIsUserWidget = false;

	UWidget* ThisWidget = Widget.GetTemplate();
	UWidget* NewReplacementWidget;
	if (BlueprintEditor->GetSelectedTemplate().IsValid())
	{
		UClass* WidgetClass = BlueprintEditor->GetSelectedTemplate().Get();
		TSharedPtr<FWidgetTemplateClass> Template = MakeShareable(new FWidgetTemplateClass(WidgetClass));
		NewReplacementWidget = Template->Create(BP->WidgetTree);
	}
	else if (BlueprintEditor->GetSelectedUserWidget().ObjectPath != NAME_None)
	{
		bIsUserWidget = true;
		FAssetData WidgetAssetData = BlueprintEditor->GetSelectedUserWidget();
		TSharedPtr<FWidgetTemplateBlueprintClass> Template = MakeShareable(new FWidgetTemplateBlueprintClass(WidgetAssetData));
		NewReplacementWidget = Template->Create(BP->WidgetTree);
	}
	else
	{
		return;
	}

	NewReplacementWidget->SetFlags(RF_Transactional);
	NewReplacementWidget->Modify();

	if (UPanelWidget* ExistingPanel = Cast<UPanelWidget>(ThisWidget))
	{
		// if they are both panel widgets then call the existing replace function
		UPanelWidget* ReplacementPanelWidget = Cast<UPanelWidget>(NewReplacementWidget);
		if (ReplacementPanelWidget)
		{
			TSet<FWidgetReference> WidgetToReplace;
			WidgetToReplace.Add(Widget);
			ReplaceWidgets(BlueprintEditor, BP, WidgetToReplace, ReplacementPanelWidget->GetClass());
			return;
		}
	}
	ThisWidget->SetFlags(RF_Transactional);
	ThisWidget->Modify();
	BP->WidgetTree->SetFlags(RF_Transactional);
	BP->WidgetTree->Modify();

	if (UPanelWidget* CurrentParent = ThisWidget->GetParent())
	{
		CurrentParent->SetFlags(RF_Transactional);
		CurrentParent->Modify();
		CurrentParent->ReplaceChild(ThisWidget, NewReplacementWidget);

		FString ReplaceName = ThisWidget->GetName();
		bool bIsGeneratedName = ThisWidget->IsGeneratedName();
		// Rename the removed widget to the transient package so that it doesn't conflict with future widgets sharing the same name.
		ThisWidget->Rename(nullptr, nullptr);

		// Rename the new Widget to maintain the current name if it's not a generic name
		if (!bIsGeneratedName)
		{
			ReplaceName = FindNextValidName(BP->WidgetTree, ReplaceName);
			NewReplacementWidget->Rename(*ReplaceName, BP->WidgetTree);
		}
	}
	else if (ThisWidget == BP->WidgetTree->RootWidget)
	{
		BP->WidgetTree->RootWidget = NewReplacementWidget;
	}
	else
	{
		return;
	}

	// Delete the widget that has been replaced
	TSet<FWidgetReference> WidgetsToDelete;
	WidgetsToDelete.Add(Widget);
	DeleteWidgets(BP, WidgetsToDelete);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
}

bool FWidgetBlueprintEditorUtils::CanBeReplacedWithTemplate(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, FWidgetReference Widget)
{
	FAssetData SelectedUserWidget = BlueprintEditor->GetSelectedUserWidget();
	UWidget* ThisWidget = Widget.GetTemplate();
	UPanelWidget* ExistingPanel = Cast<UPanelWidget>(ThisWidget);
	// If selecting another widget blueprint
	if (SelectedUserWidget.ObjectPath != NAME_None)
	{
		if (ExistingPanel)
		{
			if (ExistingPanel->GetChildrenCount() != 0)
			{
				return false;
			}
		}
		UUserWidget* NewUserWidget = CastChecked<UUserWidget>(FWidgetTemplateBlueprintClass(SelectedUserWidget).Create(BP->WidgetTree));
		const bool bFreeFromCircularRefs = BP->IsWidgetFreeFromCircularReferences(NewUserWidget);
		NewUserWidget->Rename(nullptr, nullptr);
		return bFreeFromCircularRefs;
	}

	UClass* WidgetClass = BlueprintEditor->GetSelectedTemplate().Get();
	const bool bCanReplace = WidgetClass->IsChildOf(UPanelWidget::StaticClass());
	if (!ExistingPanel && !bCanReplace)
	{
		return true;
	}
	else if (!ExistingPanel && bCanReplace)
	{
		return true;
	}
	else if (ExistingPanel && !bCanReplace)
	{
		if (ExistingPanel->GetChildrenCount() == 0)
		{
			return true;
		}
		else 
		{
			return false;
		}
	}
	else 
	{
		if (ExistingPanel->GetClass()->GetDefaultObject<UPanelWidget>()->CanHaveMultipleChildren() && bCanReplace)
		{
			const bool bChildAllowed = WidgetClass->GetDefaultObject<UPanelWidget>()->CanHaveMultipleChildren() || ExistingPanel->GetChildrenCount() == 0;
			return bChildAllowed;
		}
		else
		{
			return true;
		}
	}
}

void FWidgetBlueprintEditorUtils::ReplaceWidgetWithChildren(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, FWidgetReference Widget)
{
	if ( UPanelWidget* ExistingPanelTemplate = Cast<UPanelWidget>(Widget.GetTemplate()) )
	{
		UWidget* FirstChildTemplate = ExistingPanelTemplate->GetChildAt(0);

		FScopedTransaction Transaction(LOCTEXT("ReplaceWidgets", "Replace Widgets"));

		ExistingPanelTemplate->Modify();
		FirstChildTemplate->Modify();

		if ( UPanelWidget* PanelParentTemplate = ExistingPanelTemplate->GetParent() )
		{
			PanelParentTemplate->Modify();

			FirstChildTemplate->RemoveFromParent();
			PanelParentTemplate->ReplaceChild(ExistingPanelTemplate, FirstChildTemplate);
		}
		else if ( ExistingPanelTemplate == BP->WidgetTree->RootWidget )
		{
			FirstChildTemplate->RemoveFromParent();

			BP->WidgetTree->Modify();
			BP->WidgetTree->RootWidget = FirstChildTemplate;
		}
		else
		{
			Transaction.Cancel();
			return;
		}

		// Rename the removed widget to the transient package so that it doesn't conflict with future widgets sharing the same name.
		ExistingPanelTemplate->Rename(nullptr, nullptr);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
}

void FWidgetBlueprintEditorUtils::ReplaceWidgets(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets, UClass* WidgetClass)
{
	const FScopedTransaction Transaction(LOCTEXT("ReplaceWidgets", "Replace Widgets"));

	TSharedPtr<FWidgetTemplateClass> Template = MakeShareable(new FWidgetTemplateClass(WidgetClass));

	for ( FWidgetReference& Item : Widgets )
	{
			UPanelWidget* NewReplacementWidget = CastChecked<UPanelWidget>(Template->Create(BP->WidgetTree));
			Item.GetTemplate()->SetFlags(RF_Transactional);
			Item.GetTemplate()->Modify();

			if ( UPanelWidget* CurrentParent = Item.GetTemplate()->GetParent() )
			{
				CurrentParent->SetFlags(RF_Transactional);
				CurrentParent->Modify();
				CurrentParent->ReplaceChild(Item.GetTemplate(), NewReplacementWidget);
			}
			else if ( Item.GetTemplate() == BP->WidgetTree->RootWidget )
			{
				BP->WidgetTree->SetFlags(RF_Transactional);
				BP->WidgetTree->Modify();
				BP->WidgetTree->RootWidget = NewReplacementWidget;
			}
			else
			{
				continue;
			}

			if (UPanelWidget* ExistingPanel = Cast<UPanelWidget>(Item.GetTemplate()))
			{
				ExistingPanel->SetFlags(RF_Transactional);
				ExistingPanel->Modify();
				while (ExistingPanel->GetChildrenCount() > 0)
				{
					UWidget* Widget = ExistingPanel->GetChildAt(0);
					Widget->SetFlags(RF_Transactional);
					Widget->Modify();

					NewReplacementWidget->AddChild(Widget);
				}
			}

			FString ReplaceName = Item.GetTemplate()->GetName();
			bool bIsGeneratedName = Item.GetTemplate()->IsGeneratedName();
			// Rename the removed widget to the transient package so that it doesn't conflict with future widgets sharing the same name.
			Item.GetTemplate()->Rename(nullptr, nullptr);

			// Rename the new Widget to maintain the current name if it's not a generic name
			if (!bIsGeneratedName)
			{
				ReplaceName = FindNextValidName(BP->WidgetTree, ReplaceName);
				NewReplacementWidget->Rename(*ReplaceName, BP->WidgetTree);
			}

	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
}

void FWidgetBlueprintEditorUtils::CutWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	CopyWidgets(BP, Widgets);
	DeleteWidgets(BP, Widgets);
}

void FWidgetBlueprintEditorUtils::CopyWidgets(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	FString ExportedText = CopyWidgetsInternal(BP, Widgets);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

FString FWidgetBlueprintEditorUtils::CopyWidgetsInternal(UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	TSet<UWidget*> TemplateWidgets;

	// Convert the set of widget references into the list of widget templates we're going to copy.
	for ( const FWidgetReference& Widget : Widgets )
	{
		UWidget* TemplateWidget = Widget.GetTemplate();
		TemplateWidgets.Add(TemplateWidget);
	}

	TArray<UWidget*> FinalWidgets;

	// Pair down copied widgets to the legitimate root widgets, if they're parent is not already
	// in the set we're planning to copy, then keep them in the list, otherwise remove widgets that
	// will already be handled when their parent copies into the array.
	for ( UWidget* TemplateWidget : TemplateWidgets )
	{
		bool bFoundParent = false;

		// See if the widget already has a parent in the set we're copying.
		for ( UWidget* PossibleParent : TemplateWidgets )
		{
			if ( PossibleParent != TemplateWidget )
			{
				if ( TemplateWidget->IsChildOf(PossibleParent) )
				{
					bFoundParent = true;
					break;
				}
			}
		}

		if ( !bFoundParent )
		{
			FinalWidgets.Add(TemplateWidget);
			UWidgetTree::GetChildWidgets(TemplateWidget, FinalWidgets);
		}
	}

	FString ExportedText;
	FWidgetBlueprintEditorUtils::ExportWidgetsToText(FinalWidgets, /*out*/ ExportedText);
	return ExportedText;
}

void FWidgetBlueprintEditorUtils::DuplicateWidgets(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, TSet<FWidgetReference> Widgets)
{
	FWidgetReference ParentWidgetRef = (Widgets.Num() == 1) ? *Widgets.CreateIterator() : FWidgetReference();
	if (ParentWidgetRef.IsValid())
	{
		if (UPanelWidget* TargetWidget = Cast<UPanelWidget>(ParentWidgetRef.GetPreview()->GetParent()))
		{
			ParentWidgetRef = BlueprintEditor->GetReferenceFromPreview(TargetWidget);
		}
		else
		{
			ParentWidgetRef = FWidgetReference();
		}
	}

	if (ParentWidgetRef.IsValid())
	{
		FString ExportedText = CopyWidgetsInternal(BP, Widgets);

		FScopedTransaction Transaction(FGenericCommands::Get().Duplicate->GetDescription());
		bool TransactionSuccesful = true;
		PasteWidgetsInternal(BlueprintEditor, BP, ExportedText, ParentWidgetRef, NAME_None, FVector2D::ZeroVector, TransactionSuccesful);
		if (!TransactionSuccesful)
		{
			Transaction.Cancel();
		}
	}
}

void FWidgetBlueprintEditorUtils::ExportWidgetsToText(TArray<UWidget*> WidgetsToExport, /*out*/ FString& ExportedText)
{
	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	FStringOutputDevice Archive;

	// Validate all nodes are from the same scope and set all UUserWidget::WidgetTrees (and things outered to it) to be ignored
	TArray<UObject*> WidgetsToIgnore;
	UObject* LastOuter = nullptr;
	for ( UWidget* Widget : WidgetsToExport )
	{
		// The nodes should all be from the same scope
		UObject* ThisOuter = Widget->GetOuter();
		check((LastOuter == ThisOuter) || (LastOuter == nullptr));
		LastOuter = ThisOuter;

		if ( UUserWidget* UserWidget = Cast<UUserWidget>(Widget) )
		{
			if ( UserWidget->WidgetTree )
			{
				WidgetsToIgnore.Add(UserWidget->WidgetTree);
				// FExportObjectInnerContext does not automatically ignore UObjects if their outer is ignored
				GetObjectsWithOuter(UserWidget->WidgetTree, WidgetsToIgnore);
			}
		}
	}

	const FExportObjectInnerContext Context(WidgetsToIgnore);
	// Export each of the selected nodes
	for ( UWidget* Widget : WidgetsToExport )
	{

		UExporter::ExportToOutputDevice(&Context, Widget, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, LastOuter);

		// Check to see if this widget was content of another widget holding it in a named slot.
		if ( Widget->GetParent() == nullptr )
		{
			for ( UWidget* ExportableWidget : WidgetsToExport )
			{
				if ( INamedSlotInterface* NamedSlotContainer = Cast<INamedSlotInterface>(ExportableWidget) )
				{
					if ( NamedSlotContainer->ContainsContent(Widget) )
					{
						continue;
					}
				}
			}
		}

		if ( Widget->GetParent() == nullptr || !WidgetsToExport.Contains(Widget->GetParent()) )
		{
			auto SlotMetaData = NewObject<UWidgetSlotPair>();
			SlotMetaData->SetWidget(Widget);

			UExporter::ExportToOutputDevice(&Context, SlotMetaData, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, nullptr);
		}
	}

	ExportedText = Archive;
}

TArray<UWidget*> FWidgetBlueprintEditorUtils::PasteWidgets(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, FWidgetReference ParentWidgetRef, FName SlotName, FVector2D PasteLocation)
{
	FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	bool bTransactionSuccessful = true;
	TArray<UWidget*> PastedWidgets = PasteWidgetsInternal(BlueprintEditor, BP, TextToImport, ParentWidgetRef, SlotName, PasteLocation, bTransactionSuccessful);
	if (!bTransactionSuccessful)
	{
		Transaction.Cancel();
	}
	return PastedWidgets;
}

TArray<UWidget*> FWidgetBlueprintEditorUtils::PasteWidgetsInternal(TSharedRef<FWidgetBlueprintEditor> BlueprintEditor, UWidgetBlueprint* BP, const FString& TextToImport, FWidgetReference ParentWidgetRef, FName SlotName, FVector2D PasteLocation, bool& bTransactionSuccessful)
{
	// Import the nodes
	TSet<UWidget*> PastedWidgets;
	TMap<FName, UWidgetSlotPair*> PastedExtraSlotData;
	FWidgetBlueprintEditorUtils::ImportWidgetsFromText(BP, TextToImport, /*out*/ PastedWidgets, /*out*/ PastedExtraSlotData);

	// Ignore an empty set of widget paste data.
	if ( PastedWidgets.Num() == 0 )
	{
		bTransactionSuccessful = false;
		return TArray<UWidget*>();
	}

	TArray<UWidget*> RootPasteWidgets;
	for ( UWidget* NewWidget : PastedWidgets )
	{
		// Widgets with a null parent mean that they were the root most widget of their selection set when
		// they were copied and thus we need to paste only the root most widgets.  All their children will be added
		// automatically.
		if ( NewWidget->GetParent() == nullptr )
		{
			// Check to see if this widget is content of another widget holding it in a named slot.
			bool bIsNamedSlot = false;
			for (UWidget* ContainerWidget : PastedWidgets)
			{
				if (INamedSlotInterface* NamedSlotContainer = Cast<INamedSlotInterface>(ContainerWidget))
				{
					if (NamedSlotContainer->ContainsContent(NewWidget))
					{
						bIsNamedSlot = true;
						break;
					}
				}
			}

			// It's a Root widget only if it's not not in a named slot.
			if (!bIsNamedSlot)
			{
				RootPasteWidgets.Add(NewWidget);
			}
		}
	}

	if ( SlotName == NAME_None )
	{
		UPanelWidget* ParentWidget = nullptr;

		if ( ParentWidgetRef.IsValid() )
		{
			ParentWidget = Cast<UPanelWidget>(ParentWidgetRef.GetTemplate());

			// If the widget isn't a panel, we'll try it's parent to see if the pasted widget can be a sibling
			if (!ParentWidget)
			{
				ParentWidget = ParentWidgetRef.GetTemplate()->GetParent();
			}
		}

		if ( !ParentWidget )
		{
			// If we already have a root widget, then we can't replace the root.
			if ( BP->WidgetTree->RootWidget )
			{
				bTransactionSuccessful = false;
				return TArray<UWidget*>();
			}
		}

		// If there isn't a root widget and we're copying multiple root widgets, then we need to add a container root
		// to hold the pasted data since multiple root widgets isn't permitted.
		if ( !ParentWidget && RootPasteWidgets.Num() > 1 )
		{
			ParentWidget = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
			BP->WidgetTree->Modify();
			BP->WidgetTree->RootWidget = ParentWidget;
		}

		if ( ParentWidget )
		{
			if ( !ParentWidget->CanHaveMultipleChildren() )
			{
				if ( ParentWidget->GetChildrenCount() > 0 || RootPasteWidgets.Num() > 1 )
				{
					FNotificationInfo Info(LOCTEXT("NotEnoughSlots", "Can't paste contents, not enough available slots in target widget."));
					FSlateNotificationManager::Get().AddNotification(Info);

					bTransactionSuccessful = false;
					return TArray<UWidget*>();
				}
			}

			// A bit of a hack, but we can look at the widget's slot properties to determine if it is a canvas slot. If so, we'll try and maintain the relative positions
			bool bShouldReproduceOffsets = true;
			static const FName LayoutDataLabel = FName(TEXT("LayoutData"));
			for (TPair<FName, UWidgetSlotPair*>  SlotData : PastedExtraSlotData)
			{
				UWidgetSlotPair* SlotDataPair = SlotData.Value;
				TMap<FName, FString> SlotProperties;
				SlotDataPair->GetSlotProperties(SlotProperties);
				if (!SlotProperties.Contains(LayoutDataLabel))
				{
					bShouldReproduceOffsets = false;
					break;
				}
			}

			FVector2D FirstWidgetPosition;
			ParentWidget->Modify();
			for ( UWidget* NewWidget : RootPasteWidgets )
			{
				UPanelSlot* Slot = ParentWidget->AddChild(NewWidget);
				if ( Slot )
				{
					if ( UWidgetSlotPair* OldSlotData = PastedExtraSlotData.FindRef(NewWidget->GetFName()) )
					{
						TMap<FName, FString> OldSlotProperties;
						OldSlotData->GetSlotProperties(OldSlotProperties);
						FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, OldSlotProperties);

						// Cache the initial position of the first widget so we can calculate offsets for additional widgets
						if (NewWidget == RootPasteWidgets[0])
						{
							if (UCanvasPanelSlot* FirstCanvasSlot = Cast<UCanvasPanelSlot>(Slot))
							{
								FirstWidgetPosition = FirstCanvasSlot->GetPosition();
							}
						}
					}

					BlueprintEditor->RefreshPreview();
						
					FWidgetReference WidgetRef = BlueprintEditor->GetReferenceFromTemplate(NewWidget);

					UPanelSlot* PreviewSlot = WidgetRef.GetPreview()->Slot;
					UPanelSlot* TemplateSlot = WidgetRef.GetTemplate()->Slot;

					if ( UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PreviewSlot) )
					{
						FVector2D PasteOffset = FVector2D(0, 0);
						if (bShouldReproduceOffsets)
						{
							PasteOffset = CanvasSlot->GetPosition()- FirstWidgetPosition;
						}

						if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(CanvasSlot->Parent))
						{
							Canvas->TakeWidget(); // Generate the underlying widget so redoing the layout below works.
						}

						CanvasSlot->SaveBaseLayout();
						CanvasSlot->SetDesiredPosition(PasteLocation + PasteOffset);
						CanvasSlot->RebaseLayout();
					}

					TMap<FName, FString> SlotProperties;
					FWidgetBlueprintEditorUtils::ExportPropertiesToText(PreviewSlot, SlotProperties);
					FWidgetBlueprintEditorUtils::ImportPropertiesFromText(TemplateSlot, SlotProperties);
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		}
		else
		{
			check(RootPasteWidgets.Num() == 1)
				// If we've arrived here, we must be creating the root widget from paste data, and there can only be
				// one item in the paste data by now.
				BP->WidgetTree->Modify();

			for ( UWidget* NewWidget : RootPasteWidgets )
			{
				BP->WidgetTree->RootWidget = NewWidget;
				break;
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		}
	}
	else
	{
		if ( RootPasteWidgets.Num() > 1 )
		{
			FNotificationInfo Info(LOCTEXT("NamedSlotsOnlyHoldOneWidget", "Can't paste content, a slot can only hold one widget at the root."));
			FSlateNotificationManager::Get().AddNotification(Info);

			bTransactionSuccessful = false;
			return TArray<UWidget*>();
		}

		UWidget* NamedSlotHostWidget = ParentWidgetRef.GetTemplate();

		BP->WidgetTree->Modify();

		NamedSlotHostWidget->SetFlags(RF_Transactional);
		NamedSlotHostWidget->Modify();

		INamedSlotInterface* NamedSlotInterface = Cast<INamedSlotInterface>(NamedSlotHostWidget);
		NamedSlotInterface->SetContentForSlot(SlotName, RootPasteWidgets[0]);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	return RootPasteWidgets;
}

void FWidgetBlueprintEditorUtils::ImportWidgetsFromText(UWidgetBlueprint* BP, const FString& TextToImport, /*out*/ TSet<UWidget*>& ImportedWidgetSet, /*out*/ TMap<FName, UWidgetSlotPair*>& PastedExtraSlotData)
{
	// We create our own transient package here so that we can deserialize the data in isolation and ensure unreferenced
	// objects not part of the deserialization set are unresolved.
	UPackage* TempPackage = NewObject<UPackage>(nullptr, TEXT("/Engine/UMG/Editor/Transient"), RF_Transient);
	TempPackage->AddToRoot();

	// Force the transient package to have the same namespace as the final widget blueprint package.
	// This ensures any text properties serialized from the buffer will be keyed correctly for the target package.
#if USE_STABLE_LOCALIZATION_KEYS
	{
		const FString PackageNamespace = TextNamespaceUtil::EnsurePackageNamespace(BP);
		if (!PackageNamespace.IsEmpty())
		{
			TextNamespaceUtil::ForcePackageNamespace(TempPackage, PackageNamespace);
		}
	}
#endif // USE_STABLE_LOCALIZATION_KEYS

	// Turn the text buffer into objects
	FWidgetObjectTextFactory Factory;
	Factory.ProcessBuffer(TempPackage, RF_Transactional, TextToImport);

	PastedExtraSlotData = Factory.MissingSlotData;

	for ( auto& Entry : Factory.NewWidgetMap )
	{
		UWidget* Widget = Entry.Value;

		ImportedWidgetSet.Add(Widget);

		Widget->SetFlags(RF_Transactional);

		// We don't export parent slot pointers, so each panel will need to point it's children back to itself
		UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
		if (PanelWidget)
		{
			TArray<UPanelSlot*> PanelSlots = PanelWidget->GetSlots();
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); i++)
			{
				PanelWidget->GetChildAt(i)->Slot = PanelSlots[i];
			}
		}

		// If there is an existing widget with the same name, rename the newly placed widget.
		FString WidgetOldName = Widget->GetName();
		FString NewName = FindNextValidName(BP->WidgetTree, WidgetOldName);
		if (NewName != WidgetOldName)
		{
			UWidgetSlotPair* SlotData = PastedExtraSlotData.FindRef(Widget->GetFName());
			if ( SlotData )
			{
				PastedExtraSlotData.Remove(Widget->GetFName());
			}
			Widget->Rename(*NewName, BP->WidgetTree);

			if (Widget->GetDisplayLabel().Equals(WidgetOldName))
			{
				Widget->SetDisplayLabel(Widget->GetName());
			}

			if ( SlotData )
			{
				SlotData->SetWidgetName(Widget->GetFName());
				PastedExtraSlotData.Add(Widget->GetFName(), SlotData);
			}
		}
		else
		{
			Widget->Rename(*WidgetOldName, BP->WidgetTree);
		}
	}

	// Remove the temp package from the root now that it has served its purpose.
	TempPackage->RemoveFromRoot();
}

void FWidgetBlueprintEditorUtils::ExportPropertiesToText(UObject* Object, TMap<FName, FString>& ExportedProperties)
{
	if ( Object )
	{
		for ( TFieldIterator<UProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt )
		{
			UProperty* Property = *PropertyIt;

			// Don't serialize out object properties, we just want value data.
			if ( !Property->IsA<UObjectProperty>() )
			{
				FString ValueText;
				if ( Property->ExportText_InContainer(0, ValueText, Object, Object, Object, PPF_IncludeTransient) )
				{
					ExportedProperties.Add(Property->GetFName(), ValueText);
				}
			}
		}
	}
}

void FWidgetBlueprintEditorUtils::ImportPropertiesFromText(UObject* Object, const TMap<FName, FString>& ExportedProperties)
{
	if ( Object )
	{
		for ( const auto& Entry : ExportedProperties )
		{
			if ( UProperty* Property = FindField<UProperty>(Object->GetClass(), Entry.Key) )
			{
				FEditPropertyChain PropertyChain;
				PropertyChain.AddHead(Property);
				Object->PreEditChange(PropertyChain);

				Property->ImportText(*Entry.Value, Property->ContainerPtrToValuePtr<uint8>(Object), 0, Object);

				FPropertyChangedEvent ChangedEvent(Property);
				Object->PostEditChangeProperty(ChangedEvent);
			}
		}
	}
}

bool FWidgetBlueprintEditorUtils::IsBindWidgetProperty(UProperty* InProperty)
{
	bool bIsOptional;
	return IsBindWidgetProperty(InProperty, bIsOptional);
}

bool FWidgetBlueprintEditorUtils::IsBindWidgetProperty(UProperty* InProperty, bool& bIsOptional)
{
	if ( InProperty )
	{
		bool bIsBindWidget = InProperty->HasMetaData("BindWidget") || InProperty->HasMetaData("BindWidgetOptional");
		bIsOptional = InProperty->HasMetaData("BindWidgetOptional") || ( InProperty->HasMetaData("OptionalWidget") || InProperty->GetBoolMetaData("OptionalWidget") );

		return bIsBindWidget;
	}

	return false;
}

bool FWidgetBlueprintEditorUtils::IsBindWidgetAnimProperty(UProperty* InProperty)
{
	bool bIsOptional;
	return IsBindWidgetAnimProperty(InProperty, bIsOptional);
}

bool FWidgetBlueprintEditorUtils::IsBindWidgetAnimProperty(UProperty* InProperty, bool& bIsOptional)
{
	if (InProperty)
	{
		bool bIsBindWidgetAnim = InProperty->HasMetaData("BindWidgetAnim") || InProperty->HasMetaData("BindWidgetAnimOptional");
		bIsOptional = InProperty->HasMetaData("BindWidgetAnimOptional");

		return bIsBindWidgetAnim;
	}

	return false;
}

bool FWidgetBlueprintEditorUtils::IsUsableWidgetClass(UClass* WidgetClass)
{
	if ( WidgetClass->IsChildOf(UWidget::StaticClass()) )
	{
		// We aren't interested in classes that are experimental or cannot be instantiated
		bool bIsExperimental, bIsEarlyAccess;
		FObjectEditorUtils::GetClassDevelopmentStatus(WidgetClass, bIsExperimental, bIsEarlyAccess);
		const bool bIsInvalid = WidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
		if ( bIsExperimental || bIsEarlyAccess || bIsInvalid )
		{
			return false;
		}

		// Don't include skeleton classes or the same class as the widget being edited
		const bool bIsSkeletonClass = WidgetClass->HasAnyFlags(RF_Transient) && WidgetClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);

		// Check that the asset that generated this class is valid (necessary b/c of a larger issue wherein force delete does not wipe the generated class object)
		if ( bIsSkeletonClass )
		{
			return false;
		}

		return true;
	}

	return false;
}

FString RemoveSuffixFromName(const FString OldName)
{
	int NameLen = OldName.Len();
	int SuffixIndex = 0;
	if (OldName.FindLastChar('_', SuffixIndex))
	{
		NameLen = SuffixIndex;
		for (int32 i = SuffixIndex + 1; i < OldName.Len(); ++i)
		{
			const TCHAR& C = OldName[i];
			const bool bGoodChar = ((C >= '0') && (C <= '9'));
			if (!bGoodChar)
			{
				return OldName;
			}
		}
	}
	return FString(NameLen, *OldName);
}

FString FWidgetBlueprintEditorUtils::FindNextValidName(UWidgetTree* WidgetTree, const FString& Name)
{
	// If the name of the widget is not already used, we use it.	
	if (FindObject<UObject>(WidgetTree, *Name))
	{
		// If the name is already used, we will suffix it with '_X'
		FString NameWithoutSuffix = RemoveSuffixFromName(Name);
		FString NewName = NameWithoutSuffix;

		int32 Postfix = 0;
		while (FindObject<UObject>(WidgetTree, *NewName))
		{
			++Postfix;
			NewName = FString::Printf(TEXT("%s_%d"), *NameWithoutSuffix, Postfix);
		}

		return NewName;
	}
	return Name;
}

#undef LOCTEXT_NAMESPACE
