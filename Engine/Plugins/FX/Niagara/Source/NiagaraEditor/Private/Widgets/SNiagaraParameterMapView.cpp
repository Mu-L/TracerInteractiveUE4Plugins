// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraParameterMapView.h"
#include "SNiagaraParameterMapPaletteItem.h"
#include "NiagaraObjectSelection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "NiagaraCommon.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraGraph.h"
#include "NiagaraParameterStore.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraActions.h"
#include "SGraphActionMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "NiagaraConstants.h"
#include "EdGraph/EdGraphSchema.h"
#include "Framework/Application/SlateApplication.h"
#include "IAssetTools.h"
#include "AssetRegistryModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ScopedTransaction.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptVariable.h"
#include "Widgets/SPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SToolTip.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/Stack/NiagaraStackSystemSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "SNiagaraGraphActionWidget.h"
#include "NiagaraEditorSettings.h"
#include "Classes/EditorStyleSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeEmitter.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterMapView"

FText NiagaraParameterMapSectionID::OnGetSectionTitle(const NiagaraParameterMapSectionID::Type InSection)
{
	TArray<FName> SectionNamespaces;
	OnGetSectionNamespaces(InSection, SectionNamespaces);
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
	if (NamespaceMetadata.IsValid())
	{
		return NamespaceMetadata.DisplayNameLong.IsEmptyOrWhitespace() == false
			? NamespaceMetadata.DisplayNameLong
			: NamespaceMetadata.DisplayName;
	}
	else if (SectionNamespaces.Num() == 1)
	{
		return FText::FromName(SectionNamespaces[0]);
	}
	else
	{
		return NSLOCTEXT("GraphActionNode", "Unknown", "Unknown");
	}
}

void NiagaraParameterMapSectionID::OnGetSectionNamespaces(const NiagaraParameterMapSectionID::Type InSection, TArray<FName>& OutSectionNamespaces)
{
	switch (InSection)
	{
	case NiagaraParameterMapSectionID::ENGINE:
		OutSectionNamespaces.Add(FNiagaraConstants::EngineNamespace);
		break;
	case NiagaraParameterMapSectionID::EMITTER:
		OutSectionNamespaces.Add(FNiagaraConstants::EmitterNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_INPUT:
		OutSectionNamespaces.Add(FNiagaraConstants::ModuleNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_OUTPUT:
		OutSectionNamespaces.Add(FNiagaraConstants::OutputNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_LOCAL:
		OutSectionNamespaces.Add(FNiagaraConstants::LocalNamespace);
		OutSectionNamespaces.Add(FNiagaraConstants::ModuleNamespace);
		break;
	case NiagaraParameterMapSectionID::TRANSIENT:
		OutSectionNamespaces.Add(FNiagaraConstants::TransientNamespace);
		break;
	case NiagaraParameterMapSectionID::DATA_INSTANCE:
		OutSectionNamespaces.Add(FNiagaraConstants::DataInstanceNamespace);
		break;
	case NiagaraParameterMapSectionID::STATIC_SWITCH:
		OutSectionNamespaces.Add(FNiagaraConstants::StaticSwitchNamespace);
		break;
	case NiagaraParameterMapSectionID::SYSTEM:
		OutSectionNamespaces.Add(FNiagaraConstants::SystemNamespace);
		break;
	case NiagaraParameterMapSectionID::PARTICLE:
		OutSectionNamespaces.Add(FNiagaraConstants::ParticleAttributeNamespace);
		break;
	case NiagaraParameterMapSectionID::USER:
		OutSectionNamespaces.Add(FNiagaraConstants::UserNamespace);
		break;
	case NiagaraParameterMapSectionID::PARAMETERCOLLECTION:
		OutSectionNamespaces.Add(FNiagaraConstants::ParameterCollectionNamespace);
		break;
	}
}

NiagaraParameterMapSectionID::Type NiagaraParameterMapSectionID::OnGetSectionFromVariable(const FNiagaraVariable& InVar, bool IsStaticSwitchVariable, FNiagaraParameterHandle& OutParameterHandle, const NiagaraParameterMapSectionID::Type DefaultType)
{
	OutParameterHandle = FNiagaraParameterHandle(InVar.GetName());
	Type SectionID = DefaultType;
	if (IsStaticSwitchVariable)
	{
		SectionID = NiagaraParameterMapSectionID::STATIC_SWITCH;
	}
	else if (OutParameterHandle.IsEmitterHandle())
	{
		SectionID = NiagaraParameterMapSectionID::EMITTER;
	}
	else if (OutParameterHandle.IsModuleHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_INPUT;
	}
	else if (OutParameterHandle.IsOutputHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_OUTPUT;
	}
	else if (OutParameterHandle.IsLocalHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_LOCAL;
	}
	else if (OutParameterHandle.IsUserHandle())
	{
		SectionID = NiagaraParameterMapSectionID::USER;
	}
	else if (OutParameterHandle.IsEngineHandle())
	{
		SectionID = NiagaraParameterMapSectionID::ENGINE;
	}
	else if (OutParameterHandle.IsSystemHandle())
	{
		SectionID = NiagaraParameterMapSectionID::SYSTEM;
	}
	else if (OutParameterHandle.IsParticleAttributeHandle())
	{
		SectionID = NiagaraParameterMapSectionID::PARTICLE;
	}
	else if (OutParameterHandle.IsParameterCollectionHandle())
	{
		SectionID = NiagaraParameterMapSectionID::PARAMETERCOLLECTION;
	}
	else if (OutParameterHandle.IsTransientHandle())
	{
		SectionID = NiagaraParameterMapSectionID::TRANSIENT;
	}
	else if (OutParameterHandle.IsDataInstanceHandle())
	{
		SectionID = NiagaraParameterMapSectionID::DATA_INSTANCE;
	}

	return SectionID;
}

SNiagaraParameterMapView::~SNiagaraParameterMapView()
{
	// Unregister all commands for right click on action node
	ToolkitCommands->UnmapAction(FGenericCommands::Get().Delete);
	ToolkitCommands->UnmapAction(FGenericCommands::Get().Rename);
	ToolkitCommands->UnmapAction(FGenericCommands::Get().Copy);

	TSet<UObject*> Objects = SelectedScriptObjects->GetSelectedObjects();
	for (UObject* Object : Objects)
	{
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
		{
			System->GetExposedParameters().RemoveAllOnChangedHandlers(this);
			break;
		}
	}

	EmptyGraphs();
	if (CachedSystem.IsValid())
	{
		CachedSystem->GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
		CachedSystem->EditorOnlyAddedParameters.RemoveOnChangedHandler(AddedParameterStoreChangedHandle);
		CachedSystem.Reset();
	}

	SelectedScriptObjects->OnSelectedObjectsChanged().RemoveAll(this);
	if (SelectedVariableObjects)
	{
		SelectedVariableObjects->OnSelectedObjectsChanged().RemoveAll(this);
	}

	UNiagaraEditorSettings::OnSettingsChanged().RemoveAll(this);
}

const FSlateBrush* SNiagaraParameterMapView::GetViewOptionsBorderBrush()
{
	UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
	return Settings->GetDisplayAdvancedParameterPanelCategories()
		? FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DepressedHighlightedButtonBrush")
		: FEditorStyle::GetBrush("NoBrush");
}

void SNiagaraParameterMapView::Construct(const FArguments& InArgs, const TArray<TSharedRef<FNiagaraObjectSelection>>& InSelectedObjects, const EToolkitType InToolkitType, const TSharedPtr<FUICommandList>& InToolkitCommands)
{
	bNeedsRefresh = false;
	bIsAddingParameter = false;
	ToolkitType = InToolkitType;
	ToolkitCommands = InToolkitCommands;
	AddParameterButtons.SetNum(NiagaraParameterMapSectionID::Num);
	const FVector2D ViewOptionsShadowOffset = FNiagaraEditorStyle::Get().GetVector("NiagaraEditor.Stack.ViewOptionsShadowOffset");
	ParametersWithNamespaceModifierRenamePending = MakeShared<TArray<FName>>();

	SelectedScriptObjects = InSelectedObjects[0];
	SelectedScriptObjects->OnSelectedObjectsChanged().AddSP(this, &SNiagaraParameterMapView::SelectedObjectsChanged);
	if (InSelectedObjects.Num() == 2)
	{
		//SelectedVariableObjects->OnSelectedObjectsChanged().AddSP(this, &SNiagaraParameterMapView::SelectedObjectsChanged);
		SelectedVariableObjects = InSelectedObjects[1];
	}
	
	// Register all commands for right click on action node
	{
		TSharedPtr<FUICommandList> ToolKitCommandList = ToolkitCommands;
		ToolKitCommandList->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnDeleteEntry),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanDeleteEntry));
		ToolKitCommandList->MapAction(FGenericCommands::Get().Rename,
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnRequestRenameOnActionNode),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanRequestRenameOnActionNode));
		ToolKitCommandList->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnCopyParameterReference),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanCopyParameterReference));
	}

	UNiagaraEditorSettings::OnSettingsChanged().AddSP(this, &SNiagaraParameterMapView::NiagaraEditorSettingsChanged);

	Refresh(false);

	SAssignNew(FilterBox, SSearchBox)
		.OnTextChanged(this, &SNiagaraParameterMapView::OnFilterTextChanged);

	// View options
	TSharedRef<SWidget> ViewOptionsWidget = SNew(SBorder)
		.Padding(0)
		.BorderImage_Static(&SNiagaraParameterMapView::GetViewOptionsBorderBrush)
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewOptions")))
			.ToolTipText(LOCTEXT("ViewOptionsToolTip", "View Options"))
			.OnGetMenuContent(this, &SNiagaraParameterMapView::GetViewOptionsMenu)
			.ButtonContent()
			[
				SNew(SOverlay)
				// drop shadow
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				.Padding(ViewOptionsShadowOffset.X, ViewOptionsShadowOffset.Y, 0, 0)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GenericViewButton"))
					.ColorAndOpacity(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.Stack.ViewOptionsShadowColor"))
				]
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GenericViewButton"))
					.ColorAndOpacity(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
				]
			]
		];


	// create the main action list piece of this widget
	SAssignNew(GraphActionMenu, SGraphActionMenu, false)
		.OnGetFilterText(this, &SNiagaraParameterMapView::GetFilterText)
		.OnCreateWidgetForAction(this, &SNiagaraParameterMapView::OnCreateWidgetForAction)
		.OnCollectAllActions(this, &SNiagaraParameterMapView::CollectAllActions)
		.OnCollectStaticSections(this, &SNiagaraParameterMapView::CollectStaticSections)
		.OnActionDragged(this, &SNiagaraParameterMapView::OnActionDragged)
		.OnActionSelected(this, &SNiagaraParameterMapView::OnActionSelected)
		.OnActionDoubleClicked(this, &SNiagaraParameterMapView::OnActionDoubleClicked)
		.OnContextMenuOpening(this, &SNiagaraParameterMapView::OnContextMenuOpening)
		.OnGetSectionTitle(this, &SNiagaraParameterMapView::OnGetSectionTitle)
		.OnGetSectionToolTip(this, &SNiagaraParameterMapView::OnGetSectionToolTip)
		.OnGetSectionWidget(this, &SNiagaraParameterMapView::OnGetSectionWidget)
		.OnCreateCustomRowExpander_Static(&SNiagaraParameterMapView::CreateCustomActionExpander)
		.OnActionMatchesName(this, &SNiagaraParameterMapView::HandleActionMatchesName)
		.AutoExpandActionMenu(false)
		.AlphaSortItems(false)
		.UseSectionStyling(true)
		.ShowFilterTextBox(true);

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(300)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(4.0f)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ParameterMapPanel")))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						// Filter Box
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							FilterBox.ToSharedRef()
						]
						// Filter Box View Options
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0, 0, 0)
						[
							ViewOptionsWidget
						]
					]
				]
			]
		
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				GraphActionMenu.ToSharedRef()
			]
		]
	];
}

void SNiagaraParameterMapView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bNeedsRefresh)
	{
		GraphActionMenu->RefreshAllActions(true);
		bNeedsRefresh = false;
	}
}

bool SNiagaraParameterMapView::ParameterAddEnabled() const
{
	return Graphs.Num() > 0;
}

void SNiagaraParameterMapView::AddParameter(FNiagaraVariable NewVariable)
{
	bool bEnterRenameModeOnAdd = true;
	AddParameter(NewVariable, bEnterRenameModeOnAdd);
}

void SNiagaraParameterMapView::AddParameter(FNiagaraVariable NewVariable, bool bEnterRenameModeOnAdd)
{
	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	FNiagaraParameterHandle ParameterHandle;
	bool bSuccess = false;

	NiagaraParameterMapSectionID::Type SectionID = NiagaraParameterMapSectionID::OnGetSectionFromVariable(NewVariable, IsStaticSwitchParameter(NewVariable, Graphs), ParameterHandle);
	if (ToolkitType == SCRIPT)
	{
		if (Graphs.Num() > 0)
		{
			TSet<FName> Names;
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					for (const auto& ParameterElement : Graph->GetParameterReferenceMap())
					{
						Names.Add(ParameterElement.Key.GetName());
					}
				}
			}
			const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(NewVariable.GetName(), Names);
			NewVariable.SetName(NewUniqueName);

			FScopedTransaction AddTransaction(LOCTEXT("AddScriptParameterTransaction", "Add parameter to script."));
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					Graph->Modify();
					Graph->AddParameter(NewVariable);
					bSuccess = true;
				}
			}
		}
	}
	else if (ToolkitType == SYSTEM)
	{
		UNiagaraSystem* System = CachedSystem.Get();
		if (System != nullptr)
		{
			FScopedTransaction AddTransaction(LOCTEXT("AddSystemParameterTransaction", "Add parameter to system."));
			System->Modify();
			if (SectionID == NiagaraParameterMapSectionID::USER)
			{
				bSuccess = FNiagaraEditorUtilities::AddParameter(NewVariable, System->GetExposedParameters(), *System, nullptr);
			}
			else
			{
				bSuccess = FNiagaraEditorUtilities::AddParameter(NewVariable, System->EditorOnlyAddedParameters, *System, nullptr);
			}
		}
	}

	if (bSuccess)
	{
		GraphActionMenu->RefreshAllActions(true);
		GraphActionMenu->SelectItemByName(*FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(NewVariable.GetName()).ToString());
		if (bEnterRenameModeOnAdd)
		{
			TArray<FName> Namespaces;
			NiagaraParameterMapSectionID::OnGetSectionNamespaces(SectionID, Namespaces);
			FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(Namespaces);
			if (NamespaceMetadata.IsValid() && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName) == false)
			{
				GraphActionMenu->OnRequestRenameOnActionNode();
			}
		}
	}
}

TSharedRef<SWidget> SNiagaraParameterMapView::GetViewOptionsMenu()
{
	FMenuBuilder MenuBuilder(false, nullptr);

	auto ToggleShowAdvancedCategoriesActionLambda = [this]() {
		UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
		Settings->SetDisplayAdvancedParameterPanelCategories(!Settings->GetDisplayAdvancedParameterPanelCategories());
	};

	auto GetShowAdvancedCategoriesCheckStateActionLambda = []() {
		UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
		return Settings->GetDisplayAdvancedParameterPanelCategories() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;;
	};

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowAdvancedCategoriesLabel", "Show Advanced Categories"),
		LOCTEXT("ShowAdvancedCategoriesToolTip", "Display advanced categories for the parameter panel."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda(ToggleShowAdvancedCategoriesActionLambda),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda(GetShowAdvancedCategoriesCheckStateActionLambda)),
		NAME_None,
		EUserInterfaceActionType::Check
	);
		
	return MenuBuilder.MakeWidget();
}

bool SNiagaraParameterMapView::AllowMakeTypeGeneric(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef();
}

bool SNiagaraParameterMapView::AllowMakeTypeAttribute(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef() && InType != FNiagaraTypeDefinition::GetGenericNumericDef();
}

void SNiagaraParameterMapView::OnFilterTextChanged(const FText& InFilterText)
{
	GraphActionMenu->GenerateFilteredItems(false);
}

FText SNiagaraParameterMapView::GetFilterText() const
{
	return FilterBox->GetText();
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SNiagaraParameterMapPalleteItem, InCreateData)
		.OnItemRenamed(this, &SNiagaraParameterMapView::OnPostRenameActionNode);
}

void SNiagaraParameterMapView::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	LastCollectedParameters.Empty();

	if (Graphs.Num() == 0)
	{
		return;
	}

	TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>> ParameterEntries;
	if (ToolkitType == SCRIPT)
	{
		CollectAllActionsForScriptToolkit(ParameterEntries);
	}
	else if (ToolkitType == SYSTEM)
	{
		CollectAllActionsForSystemToolkit(ParameterEntries);
	}

	ParameterEntries.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });
	const FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}");
	for (const auto& ParameterEntry : ParameterEntries)
	{
		const FNiagaraVariable& Parameter = ParameterEntry.Key;
		FNiagaraParameterHandle Handle;
		const NiagaraParameterMapSectionID::Type Section = NiagaraParameterMapSectionID::OnGetSectionFromVariable(Parameter, IsStaticSwitchParameter(Parameter, Graphs), Handle);
		if (Section == NiagaraParameterMapSectionID::NONE)
		{
			continue;
		}

		if (HiddenSectionIDs.Contains(Section))
		{
			continue;
		}

		bool bIsExternallyReferenced = false;
		for (const FNiagaraGraphParameterReferenceCollection& ReferenceCollection : ParameterEntry.Value)
		{
			for (const FNiagaraGraphParameterReference& ParameterReference : ReferenceCollection.ParameterReferences)
			{
				UNiagaraNode* ReferenceNode = Cast<UNiagaraNode>(ParameterReference.Value.Get());
				if (ReferenceNode != nullptr)
				{
					UNiagaraGraph* ReferenceGraph;
					UNiagaraNodeAssignment* OwningAssignmentNode = ReferenceNode->GetTypedOuter<UNiagaraNodeAssignment>();
					if (OwningAssignmentNode != nullptr)
					{
						ReferenceGraph = Cast<UNiagaraGraph>(OwningAssignmentNode->GetGraph());
					}
					else
					{
						ReferenceGraph = Cast<UNiagaraGraph>(ReferenceNode->GetGraph());
					}
					if (ReferenceGraph != nullptr)
					{
						if (Graphs.ContainsByPredicate([ReferenceGraph](TWeakObjectPtr<UNiagaraGraph> Graph) { return Graph.Get() == ReferenceGraph; }) == false)
						{
							bIsExternallyReferenced = true;
							break;
						}
					}
				}
			}
			if (bIsExternallyReferenced)
			{
				break;
			}
		}

		const FText Name = FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(Parameter.GetName());
		const FText Tooltip = FText::Format(TooltipFormat, Name, Parameter.GetType().GetNameText());
		TSharedPtr<FNiagaraParameterAction> ParameterAction(new FNiagaraParameterAction(Parameter, ParameterEntry.Value, FText::GetEmpty(), Name, Tooltip, 0, FText(), ParametersWithNamespaceModifierRenamePending, Section));
		ParameterAction->bIsExternallyReferenced = bIsExternallyReferenced;
		OutAllActions.AddAction(ParameterAction);
		LastCollectedParameters.Add(Parameter);
	}
}

void SNiagaraParameterMapView::CollectAllActionsForScriptToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& OutParameterEntries)
{
	// For scripts we use the reference maps cached in the graph to collect parameters.
	for (auto& GraphWeakPtr : Graphs)
	{
		if (!GraphWeakPtr.IsValid())
		{
			continue;
		}
		UNiagaraGraph* Graph = GraphWeakPtr.Get();
		for (const TPair<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterElement : Graph->GetParameterReferenceMap())
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(ParameterElement.Key);
			if (Found)
			{
				Found->Add(ParameterElement.Value);
			}
			else
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection;
				Collection.Add(ParameterElement.Value);
				OutParameterEntries.Add(ParameterElement.Key, Collection);
			}
		}
	}
}

void SNiagaraParameterMapView::CollectAllActionsForSystemToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& OutParameterEntries)
{
	// For systems we need to collect the user parameters if a system is selected, and then we use parameter map traversal
	// to find the compile time parameters.
	UNiagaraSystem* System = CachedSystem.Get();
	if (System != nullptr)
	{
		// Collect user parameters.
		TArray<FNiagaraVariable> ExposedVars;
		System->GetExposedParameters().GetParameters(ExposedVars);
		for (const FNiagaraVariable& ExposedVar : ExposedVars)
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(ExposedVar);
			if (!Found)
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection = { FNiagaraGraphParameterReferenceCollection(true) };
				OutParameterEntries.Add(ExposedVar, Collection);
			}
		}

		// Collect manually added parameters.
		TArray<FNiagaraVariable> AddedVars;
		System->EditorOnlyAddedParameters.GetParameters(AddedVars);
		for (const FNiagaraVariable& AddedVar : AddedVars)
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(AddedVar);
			if (!Found)
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection = { FNiagaraGraphParameterReferenceCollection(true) };
				OutParameterEntries.Add(AddedVar, Collection);
			}
		}
	}

	for (TWeakObjectPtr<UNiagaraGraph> GraphWeak : Graphs)
	{
		UNiagaraGraph* Graph = GraphWeak.Get();
		if (Graph == nullptr)
		{
			continue;
		}

		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			UNiagaraNode* NodeToTraverse = OutputNode;
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::SystemSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::SystemUpdateScript)
			{
				// Traverse past the emitter nodes, otherwise the system scripts will pick up all of the emitter and particle script parameters.
				UEdGraphPin* InputPin = FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse);
				while (NodeToTraverse != nullptr && InputPin != nullptr && InputPin->LinkedTo.Num() == 1 &&
					(NodeToTraverse->IsA<UNiagaraNodeOutput>() || NodeToTraverse->IsA<UNiagaraNodeEmitter>()))
				{
					NodeToTraverse = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
					InputPin = NodeToTraverse != nullptr ? FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse) : nullptr;
				}
			}

			if (NodeToTraverse == nullptr)
			{
				continue;
			}

			bool bIgnoreDisabled = true;
			FNiagaraParameterMapHistoryBuilder Builder;
			UNiagaraEmitter* GraphOwningEmitter = Graph->GetTypedOuter<UNiagaraEmitter>();
			FCompileConstantResolver ConstantResolver = GraphOwningEmitter != nullptr
				? FCompileConstantResolver(GraphOwningEmitter)
				: FCompileConstantResolver();

			Builder.SetIgnoreDisabled(bIgnoreDisabled);
			Builder.ConstantResolver = ConstantResolver;
			NodeToTraverse->BuildParameterMapHistory(Builder, true, false);
			
			TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ReferenceCollectionsForTraversedNode;
			if (Builder.Histories.Num() == 1)
			{
				for (int32 VariableIndex = 0; VariableIndex < Builder.Histories[0].Variables.Num(); VariableIndex++)
				{
					FNiagaraVariable& HistoryVariable = Builder.Histories[0].Variables[VariableIndex];
					FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ReferenceCollectionsForTraversedNode.Find(HistoryVariable);
					if (ReferenceCollection == nullptr)
					{
						FNiagaraGraphParameterReferenceCollection NewReferenceCollection(false);
						NewReferenceCollection.Graph = Graph;
						ReferenceCollection = &ReferenceCollectionsForTraversedNode.Add(HistoryVariable, NewReferenceCollection);
					}

					TArray<TTuple<const UEdGraphPin*, const UEdGraphPin*>>& ReadHistory = Builder.Histories[0].PerVariableReadHistory[VariableIndex];
					for (const TTuple<const UEdGraphPin*, const UEdGraphPin*>& Read : ReadHistory)
					{
						if (Read.Key->GetOwningNode() != nullptr)
						{
							ReferenceCollection->ParameterReferences.Add(FNiagaraGraphParameterReference(Read.Key->PersistentGuid, Read.Key->GetOwningNode()));
						}
					}

					TArray<const UEdGraphPin*>& WriteHistory = Builder.Histories[0].PerVariableWriteHistory[VariableIndex];
					for (const UEdGraphPin* Write : WriteHistory)
					{
						if (Write->GetOwningNode() != nullptr)
						{
							ReferenceCollection->ParameterReferences.Add(FNiagaraGraphParameterReference(Write->PersistentGuid, Write->GetOwningNode()));
						}
					}
				}
			}

			for (const TPair<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ReferenceCollectionForOutputNode : ReferenceCollectionsForTraversedNode)
			{
				OutParameterEntries.FindOrAdd(ReferenceCollectionForOutputNode.Key).Add(ReferenceCollectionForOutputNode.Value);
			}
		}
	}
}

void SNiagaraParameterMapView::CollectStaticSections(TArray<int32>& StaticSectionIDs)
{
	for (int32 SectionID = 0; SectionID < NiagaraParameterMapSectionID::Num; SectionID++)
	{
		if (HiddenSectionIDs.Contains(SectionID) == false)
		{
			StaticSectionIDs.Add(SectionID);
		}
	}
}

FReply SNiagaraParameterMapView::OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FEdGraphSchemaAction> InAction(InActions.Num() > 0 ? InActions[0] : NULL);
	if (InAction.IsValid())
	{
		FNiagaraParameterAction* ParameterAction = (FNiagaraParameterAction*)InAction.Get();
		if (ParameterAction)
		{
			if (IsScriptToolkit())
			{
				TSharedRef<FNiagaraParameterGraphDragOperation> DragOperation = FNiagaraParameterGraphDragOperation::New(InAction);
				DragOperation->SetAltDrag(MouseEvent.IsAltDown());
				DragOperation->SetCtrlDrag(MouseEvent.IsLeftControlDown() || MouseEvent.IsRightControlDown());
				return FReply::Handled().BeginDragDrop(DragOperation);
			}
			else if (IsSystemToolkit())
			{
				TSharedRef<FNiagaraParameterDragOperation> DragOperation = MakeShared<FNiagaraParameterDragOperation>(InAction);
				DragOperation->CurrentHoverText = InAction->GetMenuDescription();
				DragOperation->SetupDefaults();
				DragOperation->Construct();
				return FReply::Handled().BeginDragDrop(DragOperation);
			}
		}
	}

	return FReply::Handled();
}

void SNiagaraParameterMapView::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions, ESelectInfo::Type InSelectionType)
{
	if (!IsScriptToolkit())
	{
		// Don't accept any input for SystemToolkits, as there's no parameters panel there
		return;
	}
	
	// TODO: Can there be multiple actions and graphs? 
	if (InActions.Num() == 1 && InActions[0].IsValid() && Graphs.Num() > 0 && Graphs[0].IsValid()) 
	{
		if (FNiagaraParameterAction* Action = (FNiagaraParameterAction*)InActions[0].Get())
		{
			if (UNiagaraScriptVariable* Variable = Graphs[0]->GetScriptVariable(Action->Parameter))
			{
				SelectedVariableObjects->SetSelectedObject(Variable);
				return;
			}
		}
	} 
	
	// If a variable wasn't selected just clear the current selection
	// TODO: Get proper clearing to work. Current there's no way to clear while clicking on an empty location in the graph area
	if (SelectedVariableObjects.IsValid())
	{
		SelectedVariableObjects->ClearSelectedObjects();
	}
}

void SNiagaraParameterMapView::OnActionDoubleClicked(const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions)
{

}

TSharedPtr<SWidget> SNiagaraParameterMapView::OnContextMenuOpening()
{
	// Check if the selected action is valid for a context menu
	if (SelectionHasContextMenu())
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);
		MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
		{
			TAttribute<FText> CopyReferenceToolTip;
			CopyReferenceToolTip.Bind(this, &SNiagaraParameterMapView::GetCopyParameterReferenceToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None, LOCTEXT("CopyReference", "Copy Reference"), CopyReferenceToolTip);

			TAttribute<FText> DeleteToolTip;
			DeleteToolTip.Bind(this, &SNiagaraParameterMapView::GetDeleteEntryToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, TAttribute<FText>(), DeleteToolTip);

			TAttribute<FText> RenameToolTip;
			RenameToolTip.Bind(this, &SNiagaraParameterMapView::GetRenameOnActionNodeToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("Rename", "Rename"), RenameToolTip);

			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespace", "Change Namespace"),
				LOCTEXT("ChangeNamespaceToolTip", "Select a new namespace for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraParameterMapView::GetChangeNamespaceSubMenu, false));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespaceModifier", "Change Namespace Modifier"),
				LOCTEXT("ChangeNamespaceModifierToolTip", "Edit the namespace modifier for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraParameterMapView::GetChangeNamespaceModifierSubMenu, false));

			MenuBuilder.AddMenuSeparator();

			TAttribute<FText> DuplicateToolTip;
			DuplicateToolTip.Bind(this, &SNiagaraParameterMapView::GetDuplicateParameterToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DuplicateParameter", "Duplicate"),
				DuplicateToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnDuplicateParameter),
					FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanDuplicateParameter)));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateToNewNamespace", "Duplicate to Namespace"),
				LOCTEXT("DuplicateToNewNamespaceToolTip", "Duplicate this parameter to a new namespace."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraParameterMapView::GetChangeNamespaceSubMenu, true));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateWithNewNamespaceModifier", "Duplicate with Namespace Modifier"),
				LOCTEXT("DupilcateWithNewNamespaceModifierToolTip", "Duplicate this parameter with a different namespace modifier."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraParameterMapView::GetChangeNamespaceModifierSubMenu, true));
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}

FText SNiagaraParameterMapView::OnGetSectionTitle(int32 InSectionID)
{
	return NiagaraParameterMapSectionID::OnGetSectionTitle((NiagaraParameterMapSectionID::Type)InSectionID);
}

TSharedPtr<IToolTip> SNiagaraParameterMapView::OnGetSectionToolTip(int32 InSectionID)
{
	TArray<FName> SectionNamespaces;
	NiagaraParameterMapSectionID::OnGetSectionNamespaces((NiagaraParameterMapSectionID::Type)InSectionID, SectionNamespaces);
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
	if (NamespaceMetadata.IsValid() && NamespaceMetadata.Description.IsEmptyOrWhitespace() == false)
	{
		return SNew(SToolTip)
			.Text(NamespaceMetadata.Description);
	}
	return TSharedPtr<IToolTip>();
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnGetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID)
{
	if (InSectionID == NiagaraParameterMapSectionID::STATIC_SWITCH)
	{
		return SNullWidget::NullWidget;
	}

	if (IsSystemToolkit())
	{
		TArray<FName> SectionNamespaces;
		NiagaraParameterMapSectionID::OnGetSectionNamespaces((NiagaraParameterMapSectionID::Type)InSectionID, SectionNamespaces);
		FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
		if (NamespaceMetadata.IsValid() && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventCreatingInSystemEditor))
		{
			return SNullWidget::NullWidget;
		}
	}

	TWeakPtr<SWidget> WeakRowWidget = RowWidget;
	FText AddNewText = LOCTEXT("AddNewParameter", "Add Parameter");
	FName MetaDataTag = TEXT("AddNewParameter");
	return CreateAddToSectionButton((NiagaraParameterMapSectionID::Type) InSectionID, WeakRowWidget, AddNewText, MetaDataTag);
}

TSharedRef<SWidget> SNiagaraParameterMapView::CreateAddToSectionButton(const NiagaraParameterMapSectionID::Type InSection, TWeakPtr<SWidget> WeakRowWidget, FText AddNewText, FName MetaDataTag)
{
	TSharedPtr<SComboButton> Button;
	SAssignNew(Button, SComboButton)
	.ButtonStyle(FEditorStyle::Get(), "RoundButton")
	.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
	.ContentPadding(FMargin(2, 0))
	.OnGetMenuContent(this, &SNiagaraParameterMapView::OnGetParameterMenu, InSection)
	.IsEnabled(this, &SNiagaraParameterMapView::ParameterAddEnabled)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	.HasDownArrow(false)
	.AddMetaData<FTagMetaData>(FTagMetaData(MetaDataTag))
	.ButtonContent()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(0, 1))
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush("Plus"))
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(FMargin(2,0,0,0))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text(AddNewText)
			.Visibility(this, &SNiagaraParameterMapView::OnAddButtonTextVisibility, WeakRowWidget, InSection)
			.ShadowOffset(FVector2D(1,1))
		]
	];
	AddParameterButtons[InSection] = Button;

	return Button.ToSharedRef();
}

bool SNiagaraParameterMapView::SelectionHasContextMenu() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	for (TSharedPtr<FEdGraphSchemaAction> Action : SelectedActions)
	{
		TSharedPtr<FNiagaraParameterAction> NiagaraAction = StaticCastSharedPtr<FNiagaraParameterAction>(Action);
		if (NiagaraAction && IsStaticSwitchParameter(NiagaraAction->GetParameter(), Graphs))
		{
			return false;
		}
	}
	return SelectedActions.Num() > 0;
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnGetParameterMenu(const NiagaraParameterMapSectionID::Type InSection)
{
	bool bTypeIsAttribute = true; 	// Leaving around the old generic path in case it is needed in the future.

	TSharedRef<SNiagaraAddParameterMenu> MenuWidget = SNew(SNiagaraAddParameterMenu, Graphs)
		.OnAddParameter(this, &SNiagaraParameterMapView::AddParameter)
		.OnAllowMakeType(this, bTypeIsAttribute ? &SNiagaraParameterMapView::AllowMakeTypeAttribute : &SNiagaraParameterMapView::AllowMakeTypeGeneric)
		.Section(InSection)
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(true);

	AddParameterButtons[InSection]->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox()->AsShared());
	return MenuWidget;
}

EVisibility SNiagaraParameterMapView::OnAddButtonTextVisibility(TWeakPtr<SWidget> RowWidget, const NiagaraParameterMapSectionID::Type InSection) const
{
	return EVisibility::Collapsed; // RowWidget.Pin()->IsHovered() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

void SNiagaraParameterMapView::Refresh(bool bRefreshMenu/* = true*/)
{
	EmptyGraphs();
	if (CachedSystem.IsValid())
	{
		CachedSystem->GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
		CachedSystem->EditorOnlyAddedParameters.RemoveOnChangedHandler(AddedParameterStoreChangedHandle);
		CachedSystem.Reset();
	}

	TSet<UObject*> Objects = SelectedScriptObjects->GetSelectedObjects();
	for (UObject* Object : Objects)
	{
		if (UNiagaraScript* Script = Cast<UNiagaraScript>(Object))
		{
			AddGraph(Script->GetSource());
			break;
		}
		else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Object))
		{
			AddGraph(Emitter->GraphSource);
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
		{
			CachedSystem = System;
			AddGraph(System->GetSystemSpawnScript()->GetSource());
			UserParameterStoreChangedHandle = System->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateSP(this, &SNiagaraParameterMapView::OnSystemParameterStoreChanged));
			AddedParameterStoreChangedHandle = System->EditorOnlyAddedParameters.AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateSP(this, &SNiagaraParameterMapView::OnSystemParameterStoreChanged));
		}
	}

	HiddenSectionIDs.Empty();
	const UNiagaraEditorSettings* NiagaraEditorSettings = GetDefault<UNiagaraEditorSettings>();
	bool bShowAdvanced = NiagaraEditorSettings->GetDisplayAdvancedParameterPanelCategories();
	for (int32 SectionID = 0; SectionID < NiagaraParameterMapSectionID::Num; SectionID++)
	{
		TArray<FName> Namespaces;
		NiagaraParameterMapSectionID::OnGetSectionNamespaces((NiagaraParameterMapSectionID::Type)SectionID, Namespaces);
		FNiagaraNamespaceMetadata NamespaceMetadata = NiagaraEditorSettings->GetMetaDataForNamespaces(Namespaces);
		if (NamespaceMetadata.IsValid() == false ||
			(IsScriptToolkit() && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::HideInScript)) ||
			(IsSystemToolkit() && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::HideInSystem)) ||
			(IsScriptToolkit() && bShowAdvanced == false && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::AdvancedInScript)) ||
			(IsSystemToolkit() && bShowAdvanced == false && NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)))
		{
			HiddenSectionIDs.Add(SectionID);
		}
	}

	if (bRefreshMenu)
	{
		GraphActionMenu->RefreshAllActions(true);
	}
}

void SNiagaraParameterMapView::SelectedObjectsChanged()
{
	Refresh(true);
}

void SNiagaraParameterMapView::EmptyGraphs()
{
	checkf(Graphs.Num() == OnGraphChangedHandles.Num() && Graphs.Num() == OnRecompileHandles.Num(), TEXT("Graphs and change delegates out of sync!"));
	for (int GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
	{
		if (Graphs[GraphIndex].IsValid())
		{
			Graphs[GraphIndex]->RemoveOnGraphChangedHandler(OnGraphChangedHandles[GraphIndex]);
			Graphs[GraphIndex]->RemoveOnGraphNeedsRecompileHandler(OnRecompileHandles[GraphIndex]);
		}
	}
	Graphs.Empty();
	OnGraphChangedHandles.Empty();
	OnRecompileHandles.Empty();
}

void SNiagaraParameterMapView::AddGraph(UNiagaraGraph* Graph)
{
	if (Graph && Graphs.Contains(Graph) == false)
	{
		Graphs.Add(Graph);
		FDelegateHandle OnGraphChangedHandle = Graph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &SNiagaraParameterMapView::OnGraphChanged));
		FDelegateHandle OnRecompileHandle = Graph->AddOnGraphNeedsRecompileHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &SNiagaraParameterMapView::OnGraphChanged));
		if (ToolkitType == EToolkitType::SCRIPT)
		{
			OnSubObjectSelectionChangedHandle = Graph->OnSubObjectSelectionChanged().AddSP(this, &SNiagaraParameterMapView::HandleGraphSubObjectSelectionChanged);
		}

		OnGraphChangedHandles.Add(OnGraphChangedHandle);
		OnRecompileHandles.Add(OnRecompileHandle);
	}
}

void SNiagaraParameterMapView::AddGraph(UNiagaraScriptSourceBase* SourceBase)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(SourceBase);
	if (Source)
	{
		AddGraph(Source->NodeGraph);
	}
}

void SNiagaraParameterMapView::OnGraphChanged(const FEdGraphEditAction& InAction)
{
	RefreshActions();
}

void SNiagaraParameterMapView::OnSystemParameterStoreChanged()
{
	if (bIsAddingParameter == false && CachedSystem.IsValid())
	{
		RefreshActions();
	}
}

FText SNiagaraParameterMapView::GetDeleteEntryToolTip() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText ErrorMessage;
	if (GetSingleParameterActionForSelection(ParameterAction, ErrorMessage) == false)
	{
		return ErrorMessage;
	}

	if (ParameterAction->bIsExternallyReferenced)
	{
		return LOCTEXT("CantDeleteExternal", "This parameter is referenced in an external script and can't be deleted.");
	}

	return LOCTEXT("DeleteSelected", "Delete the selected parameter.");
}

void SNiagaraParameterMapView::OnDeleteEntry()
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused) && ParameterAction->bIsExternallyReferenced == false)
	{
		if (IsScriptToolkit())
		{
			FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersWithPins", "Remove parameter and referenced pins"));
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					Graph->RemoveParameter(ParameterAction->Parameter);
				}
			}
		}
		else if (IsSystemToolkit() && CachedSystem.IsValid())
		{
			FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersFromSystem", "Remove parameter"));
			UNiagaraSystem* System = CachedSystem.Get();
			System->Modify();
			System->GetExposedParameters().RemoveParameter(ParameterAction->Parameter);
			System->EditorOnlyAddedParameters.RemoveParameter(ParameterAction->Parameter);
		}
	}
}

bool SNiagaraParameterMapView::CanDeleteEntry() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	return GetSingleParameterActionForSelection(ParameterAction, Unused) && ParameterAction->bIsExternallyReferenced == false;
}

FText SNiagaraParameterMapView::GetRenameOnActionNodeToolTip() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText ErrorMessage;
	if (GetSingleParameterActionForSelection(ParameterAction, ErrorMessage) == false)
	{
		return ErrorMessage;
	}

	if (ParameterAction->bIsExternallyReferenced)
	{
		return LOCTEXT("CantRenameExternal", "This parameter is referenced in an external script and can't be renamed.");
	}

	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (FNiagaraParameterUtilities::GetNamespaceEditData(ParameterAction->GetParameter().GetName(), ParameterHandle, NamespaceMetadata, ErrorMessage) == false)
	{
		return ErrorMessage;
	}

	if (NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName))
	{	
		return LOCTEXT("RenamingNotSupported", "The namespace for this parameter doesn't support renaming.");
	}

	return LOCTEXT("RenameParameter", "Rename this parameter.");
}

void SNiagaraParameterMapView::OnRequestRenameOnActionNode()
{
	// Attempt to rename in both menus, only one of them will have anything selected
	GraphActionMenu->OnRequestRenameOnActionNode();
}


bool SNiagaraParameterMapView::CanRequestRenameOnActionNode() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	FText Unused;
	return 
		GetSingleParameterActionForSelection(ParameterAction, Unused) &&
		ParameterAction->bIsExternallyReferenced == false &&
		FNiagaraParameterUtilities::GetNamespaceEditData(ParameterAction->GetParameter().GetName(), ParameterHandle, NamespaceMetadata, Unused) &&
		NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName) == false;
}

void SNiagaraParameterMapView::OnPostRenameActionNode(const FText& InText, TSharedRef<FNiagaraParameterAction> InAction)
{
	FText TransactionName;
	if (IsScriptToolkit())
	{
		TransactionName = LOCTEXT("RenameParameterScriptTransaction", "Rename parameter and pins.");
	}
	else if (IsSystemToolkit())
	{
		TransactionName = LOCTEXT("RenameParameterSystemTransaction", "Rename parameter.");
	}
	FScopedTransaction RenameTransaction(TransactionName);
	RenameParameter(InAction, *InText.ToString());
}

bool SNiagaraParameterMapView::GetSingleParameterActionForSelection(
	TSharedPtr<FNiagaraParameterAction>& OutParameterAction,
	FText& OutErrorMessage) const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);

	if (SelectedActions.Num() != 1)
	{
		// Can only operate on single items.
		OutParameterAction.Reset();
		OutErrorMessage = LOCTEXT("CanOnlyEditSingle", "Can only edit single selections.");
		return false;
	}

	OutParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
	if (OutParameterAction.IsValid() == false)
	{
		// Invalid action.
		OutErrorMessage = LOCTEXT("InvalidParameterAction", "Parameter action is invalid.");
		return false;
	}

	return true;
}

bool SNiagaraParameterMapView::ParameterExistsByName(FName ParameterName) const
{
	auto MatchVariableByName = [ParameterName](const FNiagaraVariable& Variable) { return Variable.GetName() == ParameterName; };
	return LastCollectedParameters.ContainsByPredicate(MatchVariableByName);
}

void SNiagaraParameterMapView::GetChangeNamespaceSubMenu(FMenuBuilder& MenuBuilder, bool bDuplicateParameter)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused))
	{
		TArray<FNiagaraParameterUtilities::FChangeNamespaceMenuData> MenuData;
		FNiagaraParameterUtilities::GetChangeNamespaceMenuData(ParameterAction->Parameter.GetName(),
			IsScriptToolkit() ? FNiagaraParameterUtilities::EParameterContext::Script : FNiagaraParameterUtilities::EParameterContext::System, MenuData);
		for (const FNiagaraParameterUtilities::FChangeNamespaceMenuData& MenuDataItem : MenuData)
		{
			bool bCanChange = MenuDataItem.bCanChange;
			FText CanChangeToolTip = MenuDataItem.CanChangeToolTip;
			if (bCanChange && bDuplicateParameter == false)
			{
				if (ParameterAction->bIsExternallyReferenced)
				{
					bCanChange = false;
					CanChangeToolTip = LOCTEXT("CantChangeNamespaceExternallyReferenced", "Parameter is from an externally referenced script and can't be directly edited.");
				}
				else
				{
					// Check for an existing duplicate by name.
					FName NewName = FNiagaraParameterUtilities::ChangeNamespace(ParameterAction->Parameter.GetName(), MenuDataItem.Metadata);
					if (ParameterExistsByName(NewName))
					{
						bCanChange = false;
						CanChangeToolTip = LOCTEXT("CantMoveAlreadyExits", "Can not move to this namespace because a parameter with this name already exists.");
					}
				}
			}

			FUIAction Action = FUIAction(
				FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnChangeNamespace, MenuDataItem.Metadata, bDuplicateParameter),
				FCanExecuteAction::CreateLambda([bCanChange]() { return bCanChange; }));

			TSharedRef<SWidget> MenuItemWidget = FNiagaraParameterUtilities::CreateNamespaceMenuItemWidget(MenuDataItem.NamespaceParameterName, CanChangeToolTip);
			MenuBuilder.AddMenuEntry(Action, MenuItemWidget, NAME_None, CanChangeToolTip);
		}
	}
}

void SNiagaraParameterMapView::OnChangeNamespace(FNiagaraNamespaceMetadata Metadata, bool bDuplicateParameter)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused) && (bDuplicateParameter || ParameterAction->bIsExternallyReferenced == false))
	{
		FName NewName = FNiagaraParameterUtilities::ChangeNamespace(ParameterAction->Parameter.GetName(), Metadata);
		if (NewName != NAME_None)
		{
			bool bParameterExists = ParameterExistsByName(NewName);
			if (bDuplicateParameter)
			{
				FName NewUniqueName;
				if (bParameterExists)
				{
					TSet<FName> ParameterNames;
					for (FNiagaraVariable& LastCollectedParameter : LastCollectedParameters)
					{
						ParameterNames.Add(LastCollectedParameter.GetName());
					}
					NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
				}
				else
				{
					NewUniqueName = NewName;
				}
				FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToNewNamespaceTransaction", "Duplicate parameter to new namespace"));
				AddParameter(FNiagaraVariable(ParameterAction->Parameter.GetType(), NewUniqueName), false);
			}
			else if (bParameterExists == false)
			{
				FScopedTransaction Transaction(LOCTEXT("ChangeNamespaceTransaction", "Change namespace"));
				RenameParameter(ParameterAction, NewName);
			}
		}
	}
}

TArray<FName> SNiagaraParameterMapView::GetOptionalNamespaceModifiers() const
{
	TArray<FName> OptionalNamespaceModifiers;
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused))
	{
		FNiagaraParameterUtilities::EParameterContext ParameterContext = IsScriptToolkit() 
			? FNiagaraParameterUtilities::EParameterContext::Script 
			: FNiagaraParameterUtilities::EParameterContext::System;
		FNiagaraParameterUtilities::GetOptionalNamespaceModifiers(ParameterAction->Parameter.GetName(), ParameterContext, OptionalNamespaceModifiers);
	}
	return OptionalNamespaceModifiers;
}

void SNiagaraParameterMapView::GetChangeNamespaceModifierSubMenu(FMenuBuilder& MenuBuilder, bool bDuplicateParameter)
{
	for (FName OptionalNamespaceModifier : GetOptionalNamespaceModifiers())
	{
		TAttribute<FText> SetToolTip;
		SetToolTip.Bind(TAttribute<FText>::FGetter::CreateSP(this, &SNiagaraParameterMapView::GetSetNamespaceModifierToolTip, OptionalNamespaceModifier, bDuplicateParameter));
		MenuBuilder.AddMenuEntry(
			FText::FromName(OptionalNamespaceModifier),
			SetToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnSetNamespaceModifier, OptionalNamespaceModifier, bDuplicateParameter),
				FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanSetNamespaceModifier, OptionalNamespaceModifier, bDuplicateParameter)));
	}

	TAttribute<FText> SetCustomToolTip;
	SetCustomToolTip.Bind(TAttribute<FText>::FGetter::CreateSP(this, &SNiagaraParameterMapView::GetSetCustomNamespaceModifierToolTip, bDuplicateParameter));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CustomNamespaceModifier", "Custom..."),
		SetCustomToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnSetCustomNamespaceModifier, bDuplicateParameter),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanSetCustomNamespaceModifier, bDuplicateParameter)));

	TAttribute<FText> SetNoneToolTip;
	SetNoneToolTip.Bind(TAttribute<FText>::FGetter::CreateSP(this, &SNiagaraParameterMapView::GetSetNamespaceModifierToolTip, FName(NAME_None), bDuplicateParameter));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NoneNamespaceModifier", "Clear"),
		SetNoneToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnSetNamespaceModifier, FName(NAME_None), bDuplicateParameter),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanSetNamespaceModifier, FName(NAME_None), bDuplicateParameter)));
}

bool SNiagaraParameterMapView::TestCanSetNamespaceModifierWithMessage(FName InNamespaceModifier, bool bDuplicateParameter, FText& OutMessage) const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText SetMessage;
	if (GetSingleParameterActionForSelection(ParameterAction, OutMessage) == false)
	{
		return false;
	}

	if (FNiagaraParameterUtilities::TestCanSetSpecificNamespaceModifierWithMessage(ParameterAction->Parameter.GetName(), InNamespaceModifier, OutMessage) == false)
	{
		return false;
	}

	if (bDuplicateParameter == false)
	{
		if (ParameterAction->bIsExternallyReferenced)
		{
			OutMessage = LOCTEXT("CantChangeNamespaceModifierExternallyReferenced", "Parameter is from an externally referenced script and can't be directly edited.");
			return false;
		}

		if (InNamespaceModifier != NAME_None)
		{
			FName NewName = FNiagaraParameterUtilities::SetSpecificNamespaceModifier(ParameterAction->Parameter.GetName(), InNamespaceModifier);
			if (ParameterExistsByName(NewName))
			{
				OutMessage = LOCTEXT("CantChangeNamespaceModifierAlreadyExits", "Can't set this namespace modifier because it would create a parameter that already exists.");
				return false;
			}
		}
	}

	return true;
}

FText SNiagaraParameterMapView::GetSetNamespaceModifierToolTip(FName InNamespaceModifier, bool bDuplicateParameter) const
{
	FText SetMessage;
	TestCanSetNamespaceModifierWithMessage(InNamespaceModifier, bDuplicateParameter, SetMessage);
	return SetMessage;
}

bool SNiagaraParameterMapView::CanSetNamespaceModifier(FName InNamespaceModifier, bool bDuplicateParameter) const
{
	FText Unused;
	return TestCanSetNamespaceModifierWithMessage(InNamespaceModifier, bDuplicateParameter, Unused);
}

void SNiagaraParameterMapView::OnSetNamespaceModifier(FName InNamespaceModifier, bool bDuplicateParameter)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused) && 
		(bDuplicateParameter || ParameterAction->bIsExternallyReferenced == false))
	{
		FName NewName = FNiagaraParameterUtilities::SetSpecificNamespaceModifier(ParameterAction->Parameter.GetName(), InNamespaceModifier);
		if (NewName != NAME_None)
		{
			bool bParameterExists = ParameterExistsByName(NewName);
			if (bDuplicateParameter)
			{
				FName NewUniqueName;
				if (bParameterExists)
				{
					TSet<FName> ParameterNames;
					for (FNiagaraVariable& LastCollectedParameter : LastCollectedParameters)
					{
						ParameterNames.Add(LastCollectedParameter.GetName());
					}
					NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
				}
				else
				{
					NewUniqueName = NewName;
				}
				FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToWithNamespaceModifierTransaction", "Duplicate parameter with namespace modifier"));
				AddParameter(FNiagaraVariable(ParameterAction->Parameter.GetType(), NewUniqueName), false);
			}
			else if (bParameterExists == false)
			{
				FScopedTransaction Transaction(LOCTEXT("ChangeNamespaceModifierTransaction", "Change namespace modifier"));
				RenameParameter(ParameterAction, NewName);
			}
		}
	}
}

bool SNiagaraParameterMapView::TestCanSetCustomNamespaceModifierWithMessage(bool bDuplicateParameter, FText& OutMessage) const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText SetMessage;
	if (GetSingleParameterActionForSelection(ParameterAction, OutMessage) == false)
	{
		return false;
	}

	if (FNiagaraParameterUtilities::TestCanSetCustomNamespaceModifierWithMessage(ParameterAction->Parameter.GetName(), OutMessage) == false)
	{
		return false;
	}

	if (bDuplicateParameter == false && ParameterAction->bIsExternallyReferenced)
	{
		OutMessage = LOCTEXT("CantChangeNamespaceModifierExternallyReferenced", "Parameter is from an externally referenced script and can't be directly edited.");
		return false;
	}

	return true;
}

FText SNiagaraParameterMapView::GetSetCustomNamespaceModifierToolTip(bool bDuplicateParameter) const
{
	FText SetMessage;
	TestCanSetCustomNamespaceModifierWithMessage(bDuplicateParameter, SetMessage);
	return SetMessage;
}

bool SNiagaraParameterMapView::CanSetCustomNamespaceModifier(bool bDuplicateParameter) const
{
	FText Unused;
	return TestCanSetCustomNamespaceModifierWithMessage(bDuplicateParameter, Unused);
}

void SNiagaraParameterMapView::OnSetCustomNamespaceModifier(bool bDuplicateParameter)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused) && (bDuplicateParameter || ParameterAction->bIsExternallyReferenced == false))
	{
		TSet<FName> ParameterNames;
		for (FNiagaraVariable& LastCollectedParameter : LastCollectedParameters)
		{
			ParameterNames.Add(LastCollectedParameter.GetName());
		}
		FName NewName = FNiagaraParameterUtilities::SetCustomNamespaceModifier(ParameterAction->Parameter.GetName(), ParameterNames);
		if (NewName != NAME_None)
		{
			if (bDuplicateParameter)
			{
				bool bParameterExists = ParameterNames.Contains(NewName);
				FName NewUniqueName;
				if (bParameterExists)
				{
					NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
				}
				else
				{
					NewUniqueName = NewName;
				}
				FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToWithCustomNamespaceModifierTransaction", "Duplicate parameter with custom namespace modifier"));
				AddParameter(FNiagaraVariable(ParameterAction->Parameter.GetType(), NewUniqueName), false);

				ParametersWithNamespaceModifierRenamePending->Add(NewUniqueName);
			}
			else
			{
				if (ParameterAction->Parameter.GetName() != NewName)
				{
					FScopedTransaction Transaction(LOCTEXT("SetCustomNamespaceModifierTransaction", "Set custom namespace modifier"));
					RenameParameter(ParameterAction, NewName);
				}
				ParametersWithNamespaceModifierRenamePending->Add(NewName);
			}
		}
	}
}

bool SNiagaraParameterMapView::TestCanDuplicateParameterWithMessage(FText& OutMessage) const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	if (GetSingleParameterActionForSelection(ParameterAction, OutMessage) == false)
	{
		return false;
	}

	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (FNiagaraParameterUtilities::GetNamespaceEditData(ParameterAction->Parameter.GetName(), ParameterHandle, NamespaceMetadata, OutMessage) == false)
	{
		return false;
	}

	if (NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName))
	{
		OutMessage = LOCTEXT("CantDuplicateWhenCantRename", "This parameter can not be duplicated because it does not support editing its name.");
		return false;
	}

	return true;
}

FText SNiagaraParameterMapView::GetDuplicateParameterToolTip() const
{
	FText DuplicateMessage;
	TestCanDuplicateParameterWithMessage(DuplicateMessage);
	return DuplicateMessage;
}

bool SNiagaraParameterMapView::CanDuplicateParameter() const
{
	FText Unused;
	return TestCanDuplicateParameterWithMessage(Unused);
}

void SNiagaraParameterMapView::OnDuplicateParameter()
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction;
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	FText Unused;
	if (GetSingleParameterActionForSelection(ParameterAction, Unused) &&
		FNiagaraParameterUtilities::GetNamespaceEditData(ParameterAction->Parameter.GetName(), ParameterHandle, NamespaceMetadata, Unused))
	{
		TSet<FName> ParameterNames;
		for (FNiagaraVariable& LastCollectedParameter : LastCollectedParameters)
		{
			ParameterNames.Add(LastCollectedParameter.GetName());
		}
		FName NewUniqueName = FNiagaraUtilities::GetUniqueName(ParameterAction->Parameter.GetName(), ParameterNames);
		FScopedTransaction Transaction(LOCTEXT("DuplicateParameterTransaction", "Duplicate parameter"));
		AddParameter(FNiagaraVariable(ParameterAction->Parameter.GetType(), NewUniqueName));
	}
}

FText SNiagaraParameterMapView::GetCopyParameterReferenceToolTip() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() != 1)
	{
		return LOCTEXT("CantCopyMultipleSelection", "Can only copy single parameters.");
	}

	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
	if (ParameterAction.IsValid() == false)
	{
		return LOCTEXT("CantCopyInvalidToolTip", "Can only copy valid parameters.");
	}

	return LOCTEXT("CopyReferenceToolTip", "Copy a string reference for this parameter to the clipboard.\nThis reference can be used in expressions and custom HLSL nodes.");
}

bool SNiagaraParameterMapView::CanCopyParameterReference() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() == 1)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
		if (ParameterAction.IsValid() && ParameterAction->Parameter.IsValid())
		{
			return true;
		}
	}
	return false;
}

void SNiagaraParameterMapView::OnCopyParameterReference()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() == 1)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
		FPlatformApplicationMisc::ClipboardCopy(*ParameterAction->Parameter.GetName().ToString());
	}
}

void SNiagaraParameterMapView::RenameParameter(TSharedPtr<FNiagaraParameterAction> ParameterAction, FName NewName)
{
	if (ensureMsgf(ParameterAction->bIsExternallyReferenced == false, TEXT("Can not modify an externally referenced parameter.")) == false)
	{
		return;
	}

	FNiagaraVariable Parameter = ParameterAction->Parameter;
	if (Parameter.GetName() == NewName)
	{
		return;
	}

	bool bSuccess = false;
	if (ToolkitType == SCRIPT)
	{
		if (Graphs.Num() > 0)
		{
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeak : Graphs)
			{
				UNiagaraGraph* Graph = GraphWeak.Get();
				if (Graph == nullptr)
				{
					// Ignore invalid graphs.
					continue;
				}

				const FNiagaraGraphParameterReferenceCollection* ReferenceCollection = Graph->GetParameterReferenceMap().Find(Parameter);
				if (ensureMsgf(ReferenceCollection != nullptr, TEXT("Parameter in view which wasn't in the reference collection.")) == false)
				{
					// Can't handle parameters with no reference collections.
					continue;
				}

				Graph->RenameParameter(Parameter, NewName);
				bSuccess = true;
			}
		}
	}
	else if (ToolkitType == SYSTEM)
	{
		UNiagaraSystem* System = CachedSystem.Get();
		if (System != nullptr)
		{
			// Rename the parameter in the parameter stores.
			FNiagaraParameterStore* OwningParameterStore = nullptr;
			if (System->GetExposedParameters().IndexOf(Parameter) != INDEX_NONE)
			{
				OwningParameterStore = &System->GetExposedParameters();
			}
			else if (System->EditorOnlyAddedParameters.IndexOf(Parameter) != INDEX_NONE)
			{
				OwningParameterStore = &System->EditorOnlyAddedParameters;
			}

			if(OwningParameterStore != nullptr)
			{
				TArray<FNiagaraVariable> OwningParameters;
				OwningParameterStore->GetParameters(OwningParameters);
				if (OwningParameters.ContainsByPredicate([NewName](const FNiagaraVariable& Variable) { return Variable.GetName() == NewName; }))
				{
					// If the parameter store already has a parameter with this name, remove the old parameter to prevent collisions.
					OwningParameterStore->RemoveParameter(Parameter);
				}
				else
				{
					// Otherwise it's safe to rename.
					OwningParameterStore->RenameParameter(Parameter, NewName);
				}
				bSuccess = true;
			}

			if (bSuccess)
			{
				// Look for set variables nodes or linked inputs which reference this parameter.
				for (FNiagaraGraphParameterReferenceCollection& ReferenceCollection : ParameterAction->ReferenceCollection)
				{
					for (FNiagaraGraphParameterReference& ParameterReference : ReferenceCollection.ParameterReferences)
					{
						UNiagaraNode* ReferenceNode = Cast<UNiagaraNode>(ParameterReference.Value);
						if (ReferenceNode != nullptr)
						{
							UNiagaraNodeAssignment* OwningAssignmentNode = ReferenceNode->GetTypedOuter<UNiagaraNodeAssignment>();
							if (OwningAssignmentNode != nullptr)
							{
								// If this is owned by a set variables node and it's not locked, update the assignment target on the assignment node.
								FNiagaraStackGraphUtilities::TryRenameAssignmentTarget(*OwningAssignmentNode, Parameter, NewName);
							}
							else
							{
								// Otherwise if the reference node is a get node it's for a linked input so we can just update pin name.
								UNiagaraNodeParameterMapGet* ReferenceGetNode = Cast<UNiagaraNodeParameterMapGet>(ReferenceNode);
								if (ReferenceGetNode != nullptr)
								{
									UEdGraphPin** LinkedInputPinPtr = ReferenceGetNode->Pins.FindByPredicate([&ParameterReference](UEdGraphPin* Pin) { return Pin->PersistentGuid == ParameterReference.Key; });
									if (LinkedInputPinPtr != nullptr)
									{
										UEdGraphPin* LinkedInputPin = *LinkedInputPinPtr;
										LinkedInputPin->Modify();
										LinkedInputPin->PinName = NewName;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (bSuccess)
	{
		GraphActionMenu->RefreshAllActions(true);
		GraphActionMenu->SelectItemByName(*FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(NewName).ToString());
	}
}

bool SNiagaraParameterMapView::IsSystemToolkit() const
{
	return ToolkitType == EToolkitType::SYSTEM;
}

bool SNiagaraParameterMapView::IsScriptToolkit() const
{
	return ToolkitType == EToolkitType::SCRIPT;
}

bool SNiagaraParameterMapView::HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const
{
	return FName(*InAction->GetMenuDescription().ToString()) == InName;
}

void SNiagaraParameterMapView::RefreshActions()
{
	bNeedsRefresh = true;
}

void SNiagaraParameterMapView::HandleGraphSubObjectSelectionChanged(const UObject* NewSelection)
{
	if (NewSelection->IsA<UNiagaraScriptVariable>())
	{
		FName VariableName = static_cast<const UNiagaraScriptVariable*>(NewSelection)->Variable.GetName();
		FName VariableActionName = *FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(VariableName).ToString();
		GraphActionMenu->SelectItemByName(VariableActionName);
	}
	SelectedVariableObjects->SetSelectedObject(const_cast<UObject*>(NewSelection));
}

bool SNiagaraParameterMapView::IsStaticSwitchParameter(const FNiagaraVariable& Variable, const TArray<TWeakObjectPtr<UNiagaraGraph>>& Graphs)
{
	for (auto& GraphWeakPtr : Graphs)
	{
		if (UNiagaraGraph* Graph = GraphWeakPtr.Get())
		{
			TArray<FNiagaraVariable> SwitchInputs = Graph->FindStaticSwitchInputs();
			if (SwitchInputs.Contains(Variable))
			{
				return true;
			}
		}
	}
	return false;
}

void SNiagaraParameterMapView::NiagaraEditorSettingsChanged(const FString& PropertyName, const UNiagaraEditorSettings* NiagaraEditorSettings)
{
	Refresh();
}

/************************************************************************/
/* SNiagaraAddParameterMenu                                             */
/************************************************************************/
void SNiagaraAddParameterMenu::Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs)
{
	this->OnAddParameter = InArgs._OnAddParameter;
	this->OnCollectCustomActions = InArgs._OnCollectCustomActions;
	this->OnAllowMakeType = InArgs._OnAllowMakeType;
	this->Section = InArgs._Section;
	this->AllowCreatingNew = InArgs._AllowCreatingNew;
	this->ShowNamespaceCategory = InArgs._ShowNamespaceCategory;
	this->ShowGraphParameters = InArgs._ShowGraphParameters;
	this->AutoExpandMenu = InArgs._AutoExpandMenu;
	this->IsParameterRead = InArgs._IsParameterRead;

	Graphs = InGraphs;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.MinDesiredWidth(300)
			.MaxDesiredHeight(700) // Set max desired height to prevent flickering bug for menu larger than screen
			[
				SAssignNew(GraphMenu, SGraphActionMenu)
				.OnActionSelected(this, &SNiagaraAddParameterMenu::OnActionSelected)
				.OnCollectAllActions(this, &SNiagaraAddParameterMenu::CollectAllActions)
				.AutoExpandActionMenu(AutoExpandMenu.Get())
				.ShowFilterTextBox(true)
				.OnCreateCustomRowExpander_Static(&SNiagaraParameterMapView::CreateCustomActionExpander)
				.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		]
	];
}

TSharedRef<SEditableTextBox> SNiagaraAddParameterMenu::GetSearchBox()
{
	return GraphMenu->GetFilterTextBox();
}

void SNiagaraAddParameterMenu::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FNiagaraMenuAction> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[ActionIndex]);

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				CurrentAction->ExecuteAction();
			}
		}
	}
}

void SNiagaraAddParameterMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (OnCollectCustomActions.IsBound())
	{
		bool bCreateRemainingActions = true;
		OnCollectCustomActions.Execute(OutAllActions, bCreateRemainingActions);
		if (!bCreateRemainingActions)
		{
			return;
		}
	}

	auto CanCollectSection = [&](const NiagaraParameterMapSectionID::Type GivenSectionID)
	{
		NiagaraParameterMapSectionID::Type ID = Section.Get();
		return ID == NiagaraParameterMapSectionID::NONE || (ID != NiagaraParameterMapSectionID::NONE && ID == GivenSectionID);
	};

	TArray<NiagaraParameterMapSectionID::Type> IDsExcluded;
	// If this is a write node, exclude any read-only vars.
	if (!IsParameterRead.Get())
	{
		IDsExcluded.Add(NiagaraParameterMapSectionID::USER);
		IDsExcluded.Add(NiagaraParameterMapSectionID::ENGINE);
		IDsExcluded.Add(NiagaraParameterMapSectionID::PARAMETERCOLLECTION);
	}

	// If this doesn't have particles in the script, exclude reading or writing them.
	for (TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
	{
		UNiagaraGraph* Graph = GraphWeakPtr.Get();
		bool IsModule = Graph->FindOutputNode(ENiagaraScriptUsage::Module) != nullptr || Graph->FindOutputNode(ENiagaraScriptUsage::DynamicInput) != nullptr
			|| Graph->FindOutputNode(ENiagaraScriptUsage::Function) != nullptr;

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Graph->GetOuter());
		if (Source && IsModule)
		{
			UNiagaraScript* Script = Cast<UNiagaraScript>(Source->GetOuter());
			if (Script)
			{
				TArray<ENiagaraScriptUsage> Usages = Script->GetSupportedUsageContexts();
				if (!Usages.Contains(ENiagaraScriptUsage::ParticleEventScript) && 
					!Usages.Contains(ENiagaraScriptUsage::ParticleSpawnScript) && 
					!Usages.Contains(ENiagaraScriptUsage::ParticleUpdateScript))
				{
					IDsExcluded.Add(NiagaraParameterMapSectionID::PARTICLE);
				}

				if (!IsParameterRead.Get())
				{
					if (!Usages.Contains(ENiagaraScriptUsage::SystemSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::SystemUpdateScript))
					{
						IDsExcluded.Add(NiagaraParameterMapSectionID::SYSTEM);
					}

					if (!Usages.Contains(ENiagaraScriptUsage::EmitterSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::EmitterUpdateScript))
					{
						IDsExcluded.Add(NiagaraParameterMapSectionID::EMITTER);
					}
				}
			}
		}
	}
	// Particle
	if (CanCollectSection(NiagaraParameterMapSectionID::PARTICLE) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::PARTICLE))
	{
		const FText Category = ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::PARTICLE) : LOCTEXT("UseExistingParticleAttribute", "Use Existing");
		TArray<FNiagaraVariable> Variables;
		Variables = FNiagaraConstants::GetCommonParticleAttributes();
		AddParameterGroup(OutAllActions, Variables, NiagaraParameterMapSectionID::PARTICLE, Category, FString(), true, false);
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::PARTICLE);
	}

	// DataInstance
	if (CanCollectSection(NiagaraParameterMapSectionID::DATA_INSTANCE) && IDsExcluded.Contains(NiagaraParameterMapSectionID::DATA_INSTANCE) == false && IDsExcluded.Contains(NiagaraParameterMapSectionID::PARTICLE) == false)
	{
		TArray<FNiagaraVariable> Variables;
		Variables.Add(SYS_PARAM_INSTANCE_ALIVE);
		AddParameterGroup(OutAllActions, Variables, NiagaraParameterMapSectionID::DATA_INSTANCE, FText(), FString(), true, false);
	}

	// Emitter
	if (CanCollectSection(NiagaraParameterMapSectionID::EMITTER) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::EMITTER))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::EMITTER);
	}

	// Module
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_INPUT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_INPUT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_INPUT);
	}

	// Module Output
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_OUTPUT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_OUTPUT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_OUTPUT);
	}

	// Module Local
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_LOCAL) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_LOCAL))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_LOCAL);
	}

	// Transient
	if (CanCollectSection(NiagaraParameterMapSectionID::TRANSIENT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::TRANSIENT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::TRANSIENT);
	}

	// System
	if (CanCollectSection(NiagaraParameterMapSectionID::SYSTEM) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::SYSTEM))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::SYSTEM);
	}

	// User
	if (CanCollectSection(NiagaraParameterMapSectionID::USER) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::USER))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::USER);
	}

	// Parameter collections
	if (CanCollectSection(NiagaraParameterMapSectionID::PARAMETERCOLLECTION) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::PARAMETERCOLLECTION))
	{
		CollectParameterCollectionsActions(OutAllActions);
	}

	// Engine
	if (CanCollectSection(NiagaraParameterMapSectionID::ENGINE) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::ENGINE))
	{
		const FText Category = NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::ENGINE);
		
		TArray<FNiagaraVariable> Variables = FNiagaraConstants::GetEngineConstants();
		TArray<FName> EngineNamespaces;
		NiagaraParameterMapSectionID::OnGetSectionNamespaces(NiagaraParameterMapSectionID::ENGINE, EngineNamespaces);
		Variables.RemoveAll([EngineNamespaces](const FNiagaraVariable& Variable)
		{
			FNiagaraParameterHandle VariableHandle(Variable.GetName());
			TArray<FName> VariableNameParts = VariableHandle.GetHandleParts();
			if (VariableNameParts.Num() <= EngineNamespaces.Num())
			{
				return true;
			}

			for (int32 NamespaceIndex = 0; NamespaceIndex < EngineNamespaces.Num(); NamespaceIndex++)
			{
				if (VariableNameParts[NamespaceIndex] != EngineNamespaces[NamespaceIndex])
				{
					return true;
				}
			}

			return false;
		});

		AddParameterGroup(OutAllActions,
			Variables,
			NiagaraParameterMapSectionID::ENGINE,
			ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::ENGINE) : FText::GetEmpty(),
			FString(), true, false);
	}

	// Collect parameter actions
	if (ShowGraphParameters.Get())
	{
		for (TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
		{
			TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterEntries = Graph.Get()->GetParameterReferenceMap();
			ParameterEntries.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });

			for (const auto& ParameterEntry : ParameterEntries)
			{
				const FNiagaraVariable& Parameter = ParameterEntry.Key;
				FNiagaraParameterHandle Handle;
				bool IsStaticSwitch = SNiagaraParameterMapView::IsStaticSwitchParameter(Parameter, Graphs);
				const NiagaraParameterMapSectionID::Type ParameterSectionID = NiagaraParameterMapSectionID::OnGetSectionFromVariable(Parameter, IsStaticSwitch, Handle);
				if (CanCollectSection(ParameterSectionID))
				{
					if (IDsExcluded.Contains(ParameterSectionID))
					{
						continue;
					}

					const FText Category = ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(ParameterSectionID) : FText::GetEmpty();
					const FText DisplayName = FText::FromName(Parameter.GetName());

					// Only add this action if it isn't already in the list.
					bool bUnique = true;
					for (int32 Index = 0; Index < OutAllActions.GetNumActions(); Index++)
					{
						const FGraphActionListBuilderBase::ActionGroup& ActionGroup = OutAllActions.GetAction(Index);
						for (const TSharedPtr<FEdGraphSchemaAction>& SchemaAction : ActionGroup.Actions)
						{
							if (SchemaAction->GetMenuDescription().EqualTo(DisplayName))
							{
								bUnique = false;
								break;
							}
						}

						if (!bUnique)
						{
							break;
						}
					}

					if (bUnique)
					{
						const FText Tooltip = FText::GetEmpty();
						TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
							Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
							FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu::AddParameterSelected, Parameter, false, ParameterSectionID)));

						OutAllActions.AddAction(Action);
					}
				}
			}
		}
	}
}

void SNiagaraAddParameterMenu::AddParameterGroup(
	FGraphActionListBuilderBase& OutActions,
	TArray<FNiagaraVariable>& Variables,
	const NiagaraParameterMapSectionID::Type InSection,
	const FText& Category,
	const FString& RootCategory,
	const bool bSort,
	const bool bCustomName,
	bool bForMakeNew)
{
	if (bSort)
	{
		Variables.Sort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });
	}

	for (const FNiagaraVariable& Variable : Variables)
	{
		const FText DisplayName = FText::FromName(Variable.GetName());
		FText Tooltip = FText::GetEmpty();

		if (const UStruct* VariableStruct = Variable.GetType().GetStruct())
		{
			Tooltip = VariableStruct->GetToolTipText(true);
		}

		TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(Category, DisplayName, Tooltip, 0, FText(),
			FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu::AddParameterSelected, Variable, bCustomName, InSection)));
		if (bForMakeNew == false)
		{
			Action->SetParamterVariable(Variable);
		}

		if (Variable.IsDataInterface())
		{
			if (const UClass* DataInterfaceClass = Variable.GetType().GetClass())
			{
				Action->IsExperimental = DataInterfaceClass->GetMetaData("DevelopmentStatus") == TEXT("Experimental");
			}
		}

		OutActions.AddAction(Action, RootCategory);
	}
}

void SNiagaraAddParameterMenu::CollectParameterCollectionsActions(FGraphActionListBuilderBase& OutActions)
{
	//Create sub menus for parameter collections.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> CollectionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);

	const FText Category = NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::PARAMETERCOLLECTION);
	for (FAssetData& CollectionAsset : CollectionAssets)
	{
		UNiagaraParameterCollection* Collection = CastChecked<UNiagaraParameterCollection>(CollectionAsset.GetAsset());
		if (Collection)
		{
			AddParameterGroup(OutActions, Collection->GetParameters(), NiagaraParameterMapSectionID::PARAMETERCOLLECTION, Category, FString(), true, false);
		}
	}
}

void SNiagaraAddParameterMenu::CollectMakeNew(FGraphActionListBuilderBase& OutActions, const NiagaraParameterMapSectionID::Type InSection)
{
	if (!AllowCreatingNew.Get())
	{
		return;
	}

	TArray<FNiagaraVariable> Variables;
	TArray<FNiagaraTypeDefinition> Types = FNiagaraTypeRegistry::GetRegisteredTypes();
	for (const FNiagaraTypeDefinition& RegisteredType : Types)
	{
		bool bAllowType = true;
		if (OnAllowMakeType.IsBound())
		{
			bAllowType = OnAllowMakeType.Execute(RegisteredType);
		}

		if (bAllowType)
		{
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetNameText().ToString()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);
			Variables.Add(Var);
		}
	}

	AddParameterGroup(OutActions, Variables, InSection,
		LOCTEXT("MakeNewCat", "Make New"), 
		ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(InSection).ToString() : FString(), 
		true, true, true);
}

void SNiagaraAddParameterMenu::AddParameterSelected(FNiagaraVariable NewVariable, const bool bCreateCustomName, const NiagaraParameterMapSectionID::Type InSection)
{
	if (bCreateCustomName)
	{
		FString TypeDisplayName;
		if (NewVariable.GetType().GetEnum() != nullptr)
		{
			TypeDisplayName = ((UField*)NewVariable.GetType().GetEnum())->GetDisplayNameText().ToString();
		}
		else if (NewVariable.GetType().GetStruct() != nullptr)
		{
			TypeDisplayName = NewVariable.GetType().GetStruct()->GetDisplayNameText().ToString();
		}
		else if (NewVariable.GetType().GetClass() != nullptr)
		{
			TypeDisplayName = NewVariable.GetType().GetClass()->GetDisplayNameText().ToString();
		}
		FString NewVariableDefaultName = TypeDisplayName.IsEmpty() 
			? TEXT("New Variable")
			: TEXT("New ") + TypeDisplayName;

		TArray<FString> NameParts;

		TArray<FName> SectionNamespaces;
		NiagaraParameterMapSectionID::OnGetSectionNamespaces(InSection, SectionNamespaces);
		for (FName SectionNamespace : SectionNamespaces)
		{
			NameParts.Add(SectionNamespace.ToString());
		}

		FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
		if (NamespaceMetadata.IsValid() && NamespaceMetadata.RequiredNamespaceModifier != NAME_None)
		{
			NameParts.Add(NamespaceMetadata.RequiredNamespaceModifier.ToString());
		}

		NameParts.Add(NewVariableDefaultName);
		const FString ResultName = FString::Join(NameParts, TEXT("."));
		NewVariable.SetName(FName(*ResultName));
	}

	OnAddParameter.ExecuteIfBound(NewVariable);
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


TSharedRef<SExpanderArrow> SNiagaraParameterMapView::CreateCustomActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraActionMenuExpander, ActionMenuData);
}

#undef LOCTEXT_NAMESPACE // "NiagaraParameterMapView"