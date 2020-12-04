// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackModuleItem.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraActions.h"
#include "NiagaraConstants.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeAssignment.h"
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "SDropTarget.h"
#include "NiagaraEditorUtilities.h"
#include "SGraphActionMenu.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "ViewModels/Stack/NiagaraStackClipboardUtilities.h"
#include "Widgets/SNiagaraLibraryOnlyToggleHeader.h"
#include "NiagaraEditorModule.h"
#include "SNiagaraGraphActionWidget.h"

#define LOCTEXT_NAMESPACE "NiagaraStackModuleItem"

bool SNiagaraStackModuleItem::bLibraryOnly = true;

void SNiagaraStackModuleItem::Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InModuleItem, UNiagaraStackViewModel* InStackViewModel)
{
	ModuleItem = &InModuleItem;
	SNiagaraStackItem::Construct(SNiagaraStackItem::FArguments(), InModuleItem, InStackViewModel);
}

void SNiagaraStackModuleItem::FillRowContextMenu(FMenuBuilder& MenuBuilder)
{
	FNiagaraStackEditorWidgetsUtilities::AddStackModuleItemContextMenuActions(MenuBuilder, *ModuleItem, this->AsShared());
	FNiagaraStackEditorWidgetsUtilities::AddStackItemContextMenuActions(MenuBuilder, *ModuleItem);
}

FReply SNiagaraStackModuleItem::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	const UNiagaraNodeFunctionCall& ModuleFunctionCall = ModuleItem->GetModuleNode();
	if (ModuleFunctionCall.FunctionScript != nullptr)
	{
		if (ModuleFunctionCall.FunctionScript->IsAsset() || GbShowNiagaraDeveloperWindows > 0)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(const_cast<UNiagaraScript*>(ModuleFunctionCall.FunctionScript));
			return FReply::Handled();
		}
		else if (ModuleItem->IsScratchModule())
		{
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel = ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(ModuleFunctionCall.FunctionScript);
			if (ScratchPadScriptViewModel.IsValid())
			{
				ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchPadScriptViewModel.ToSharedRef());
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

void SNiagaraStackModuleItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ModuleItem->GetIsModuleScriptReassignmentPending())
	{
		ModuleItem->SetIsModuleScriptReassignmentPending(false);
		ShowReassignModuleScriptMenu();
	}
	SNiagaraStackItem::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SNiagaraStackModuleItem::AddCustomRowWidgets(TSharedRef<SHorizontalBox> HorizontalBox)
{
	// Scratch navigation
	if(ModuleItem->IsScratchModule())
	{
		HorizontalBox->AddSlot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "RoundButton")
			.OnClicked(this, &SNiagaraStackModuleItem::ScratchButtonPressed)
			.ToolTipText(LOCTEXT("OpenInScratchToolTip", "Open this module in the scratch pad."))
			.ContentPadding(FMargin(1.0f, 0.0f))
			.Content()
			[
				SNew(SImage)
				.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Scratch"))
			]
		];
	}
	// Add menu.
	HorizontalBox->AddSlot()
	.VAlign(VAlign_Center)
	.AutoWidth()
	[
		SAssignNew(AddButton, SComboButton)
		.HasDownArrow(false)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.ForegroundColor(FSlateColor::UseForeground())
		.OnGetMenuContent(this, &SNiagaraStackModuleItem::RaiseActionMenuClicked)
		.ContentPadding(FMargin(2))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Visibility(this, &SNiagaraStackModuleItem::GetRaiseActionMenuVisibility)
		.IsEnabled(this, &SNiagaraStackModuleItem::GetButtonsEnabled)
		.ButtonContent()
		[
			SNew(SImage)
			.Image(FEditorStyle::Get().GetBrush("PropertyWindow.Button_AddToArray"))
		]
	];

	// Refresh button
	HorizontalBox->AddSlot()
	.VAlign(VAlign_Center)
	.AutoWidth()
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.IsFocusable(false)
		.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
		.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh this module"))
		.Visibility(this, &SNiagaraStackModuleItem::GetRefreshVisibility)
		.IsEnabled(this, &SNiagaraStackModuleItem::GetButtonsEnabled)
		.OnClicked(this, &SNiagaraStackModuleItem::RefreshClicked)
		.Content()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
		.Text(FEditorFontGlyphs::Refresh)
		]
	];
}

TSharedRef<SWidget> SNiagaraStackModuleItem::AddContainerForRowWidgets(TSharedRef<SWidget> RowWidgets)
{
	return SNew(SDropTarget)
	.OnAllowDrop(this, &SNiagaraStackModuleItem::OnModuleItemAllowDrop)
	.OnDrop(this, &SNiagaraStackModuleItem::OnModuleItemDrop)
	.HorizontalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"))
	.VerticalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderVertical"))
	.BackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColor"))
	.BackgroundColorHover(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColorHover"))
	.Content()
	[
		RowWidgets
	];
}

bool SNiagaraStackModuleItem::GetButtonsEnabled() const
{
	return ModuleItem->GetOwnerIsEnabled() && ModuleItem->GetIsEnabled();
}

EVisibility SNiagaraStackModuleItem::GetRaiseActionMenuVisibility() const
{
	return CanRaiseActionMenu() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiagaraStackModuleItem::GetRefreshVisibility() const
{
	return ModuleItem->CanRefresh() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SNiagaraStackModuleItem::ScratchButtonPressed() const
{
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchModuleViewModel =
		ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(ModuleItem->GetModuleNode().FunctionScript);
	if (ScratchModuleViewModel.IsValid())
	{
		ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchModuleViewModel.ToSharedRef());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (SelectedActions.Num() == 1 && (InSelectionType == ESelectInfo::OnKeyPress || InSelectionType == ESelectInfo::OnMouseClick))
	{
		TSharedPtr<FNiagaraMenuAction> Action = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[0]);
		if (Action.IsValid())
		{
			FSlateApplication::Get().DismissAllMenus();
			Action->ExecuteAction();
		}
	}
}

TSharedRef<SWidget> SNiagaraStackModuleItem::RaiseActionMenuClicked()
{
	if (CanRaiseActionMenu())
	{
		TSharedPtr<SGraphActionMenu> GraphActionMenu;

		TSharedRef<SBorder> MenuWidget = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.MinDesiredWidth(300)
			.HeightOverride(400)
			[
				SAssignNew(GraphActionMenu, SGraphActionMenu)
				.OnActionSelected_Static(OnActionSelected)
				.OnCollectAllActions(this, &SNiagaraStackModuleItem::CollectParameterActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
				.OnCreateCustomRowExpander_Static(&SNiagaraStackModuleItem::CreateCustomActionExpander)
				.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		];

		AddButton->SetMenuContentWidgetToFocus(GraphActionMenu->GetFilterTextBox()->AsShared());
		return MenuWidget;
	}
	return SNullWidget::NullWidget;
}

class SNiagaraActionMenuExpander : public SExpanderArrow
{
	SLATE_BEGIN_ARGS(SNiagaraActionMenuExpander) {}
		SLATE_ATTRIBUTE(float, IndentAmount)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const FCustomExpanderData& ActionMenuData)
	{
		OwnerRowPtr = ActionMenuData.TableRow;
		IndentAmount = InArgs._IndentAmount;
		if (!ActionMenuData.RowAction.IsValid())
		{
			SExpanderArrow::FArguments SuperArgs;
			SuperArgs._IndentAmount = InArgs._IndentAmount;

			SExpanderArrow::Construct(SuperArgs, ActionMenuData.TableRow);
		}
		else
		{
			ChildSlot
			.Padding(TAttribute<FMargin>(this, &SNiagaraActionMenuExpander::GetCustomIndentPadding))
			[	
				SNew(SBox)
			];
		}
	}

private:
	FMargin GetCustomIndentPadding() const
	{
		return SExpanderArrow::GetExpanderPadding();
	}
};

TSharedRef<SExpanderArrow> SNiagaraStackModuleItem::CreateCustomActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraActionMenuExpander, ActionMenuData);
}

bool SNiagaraStackModuleItem::CanRaiseActionMenu() const
{
	return Cast<UNiagaraNodeAssignment>(&ModuleItem->GetModuleNode()) != nullptr;
}

FReply SNiagaraStackModuleItem::RefreshClicked()
{
	ModuleItem->Refresh();
	return FReply::Handled();
}

FReply SNiagaraStackModuleItem::OnModuleItemDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	UNiagaraStackEntry::FDropRequest DropRequest(DragDropOperation.ToSharedRef(), EItemDropZone::OntoItem, UNiagaraStackEntry::EDragOptions::None, UNiagaraStackEntry::EDropOptions::None);
	TOptional<UNiagaraStackEntry::FDropRequestResponse> DropResponse = ModuleItem->Drop(DropRequest);
	return DropResponse.IsSet() && DropResponse->DropZone == EItemDropZone::OntoItem ? FReply::Handled() : FReply::Unhandled();
}

bool SNiagaraStackModuleItem::OnModuleItemAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	UNiagaraStackEntry::FDropRequest AllowDropRequest(DragDropOperation.ToSharedRef(), EItemDropZone::OntoItem, UNiagaraStackEntry::EDragOptions::None, UNiagaraStackEntry::EDropOptions::None);
	TOptional<UNiagaraStackEntry::FDropRequestResponse> AllowDropResponse = ModuleItem->CanDrop(AllowDropRequest);
	return AllowDropResponse.IsSet() && AllowDropResponse->DropZone == EItemDropZone::OntoItem;
}

void ReassignModuleScript(UNiagaraStackModuleItem* ModuleItem, FAssetData NewModuleScriptAsset)
{
	UNiagaraScript* NewModuleScript = Cast<UNiagaraScript>(NewModuleScriptAsset.GetAsset());
	if (NewModuleScript != nullptr)
	{
		ModuleItem->ReassignModuleScript(NewModuleScript);
	}
}

void SNiagaraStackModuleItem::CollectParameterActions(FGraphActionListBuilderBase& ModuleActions)
{
	UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&ModuleItem->GetModuleNode());
	if (AssignmentNode != nullptr)
	{
		UNiagaraNodeOutput* OutputNode = ModuleItem->GetOutputNode();
		if (OutputNode != nullptr)
		{
			TArray<TSharedPtr<FNiagaraMenuAction>> AllActions;
			AssignmentNode->CollectAddExistingActions(OutputNode->GetUsage(), OutputNode, AllActions);
			AssignmentNode->CollectCreateNewActions(OutputNode->GetUsage(), OutputNode, AllActions);
			
			for (TSharedPtr<FNiagaraMenuAction> Action : AllActions)
			{
				ModuleActions.AddAction(Action);
			}
		}
	}
}

void SNiagaraStackModuleItem::CollectModuleActions(FGraphActionListBuilderBase& ModuleActions)
{
	TArray<FAssetData> ModuleAssets;
	FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions ModuleScriptFilterOptions;
	ModuleScriptFilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
	ModuleScriptFilterOptions.TargetUsageToMatch = ModuleItem->GetOutputNode()->GetUsage();
	ModuleScriptFilterOptions.bIncludeNonLibraryScripts = bLibraryOnly == false;
	FNiagaraEditorUtilities::GetFilteredScriptAssets(ModuleScriptFilterOptions, ModuleAssets);
	for (const FAssetData& ModuleAsset : ModuleAssets)
	{
		FText Category;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Category), Category);
		if (Category.IsEmptyOrWhitespace())
		{
			Category = LOCTEXT("ModuleNotCategorized", "Uncategorized Modules");
		}

		bool bIsInLibrary = FNiagaraEditorUtilities::IsScriptAssetInLibrary(ModuleAsset);

		FText DisplayName = FNiagaraEditorUtilities::FormatScriptName(ModuleAsset.AssetName, bIsInLibrary);

		FText AssetDescription;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Description), AssetDescription);
		FText Description = FNiagaraEditorUtilities::FormatScriptDescription(AssetDescription, ModuleAsset.ObjectPath, bIsInLibrary);

		FText Keywords;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Keywords), Keywords);

		TSharedPtr<FNiagaraMenuAction> ModuleAction(new FNiagaraMenuAction(Category, DisplayName, Description, 0, Keywords,
			FNiagaraMenuAction::FOnExecuteStackAction::CreateStatic(&ReassignModuleScript, ModuleItem, ModuleAsset)));
		ModuleActions.AddAction(ModuleAction);
	}
}

void SNiagaraStackModuleItem::ShowReassignModuleScriptMenu()
{
	TSharedPtr<SNiagaraLibraryOnlyToggleHeader> LibraryOnlyToggle;
	TSharedPtr<SGraphActionMenu> GraphActionMenu;

	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(300)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.Padding(1.0f)
				[
					SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
					.HeaderLabelText(LOCTEXT("ReassignModuleLabel", "Select a new module"))
					.LibraryOnly(this, &SNiagaraStackModuleItem::GetLibraryOnly)
					.LibraryOnlyChanged(this, &SNiagaraStackModuleItem::SetLibraryOnly)
				]
				+SVerticalBox::Slot()
				.FillHeight(15)
				[
					SAssignNew(GraphActionMenu, SGraphActionMenu)
					.OnActionSelected_Static(OnActionSelected)
					.OnCollectAllActions(this, &SNiagaraStackModuleItem::CollectModuleActions)
					.ShowFilterTextBox(true)
				]
			]
		];

	LibraryOnlyToggle->SetActionMenu(GraphActionMenu.ToSharedRef());

	FGeometry ThisGeometry = GetCachedGeometry();
	bool bAutoAdjustForDpiScale = false; // Don't adjust for dpi scale because the push menu command is expecting an unscaled position.
	FVector2D MenuPosition = FSlateApplication::Get().CalculatePopupWindowPosition(ThisGeometry.GetLayoutBoundingRect(), MenuWidget->GetDesiredSize(), bAutoAdjustForDpiScale);
	FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), MenuWidget, MenuPosition, FPopupTransitionEffect::ContextMenu);
}

bool SNiagaraStackModuleItem::GetLibraryOnly() const
{
	return bLibraryOnly;
}

void SNiagaraStackModuleItem::SetLibraryOnly(bool bInLibraryOnly)
{
	bLibraryOnly = bInLibraryOnly;
}

#undef LOCTEXT_NAMESPACE