// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSearchBox.h"
#include "GraphEditor.h"
#include "AssetData.h"
#include "HistoryManager.h"
#include "CollectionManagerTypes.h"
#include "AssetManagerEditorModule.h"

class UEdGraph;
class UEdGraph_ReferenceViewer;

/**
 * 
 */
class SReferenceViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SReferenceViewer ){}

	SLATE_END_ARGS()

	~SReferenceViewer();

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs );

	/** Sets a new root package name */
	void SetGraphRootIdentifiers(const TArray<FAssetIdentifier>& NewGraphRootIdentifiers);

	/** Gets graph editor */
	TSharedPtr<SGraphEditor> GetGraphEditor() const { return GraphEditorPtr; }

	/** Called when the current registry source changes */
	void SetCurrentRegistrySource(const FAssetManagerEditorRegistrySource* RegistrySource);

private:

	/** Call after a structural change is made that causes the graph to be recreated */
	void RebuildGraph();

	/** Called to create context menu when right-clicking on graph */
	FActionMenuContent OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	/** Called when a node is double clicked */
	void OnNodeDoubleClicked(class UEdGraphNode* Node);

	/** True if the user may use the history back button */
	bool IsBackEnabled() const;

	/** True if the user may use the history forward button */
	bool IsForwardEnabled() const;

	/** Handler for clicking the history back button */
	FReply BackClicked();

	/** Handler for clicking the history forward button */
	FReply ForwardClicked();

	/** Handler for when the graph panel tells us to go back in history (like using the mouse thumb button) */
	void GraphNavigateHistoryBack();

	/** Handler for when the graph panel tells us to go forward in history (like using the mouse thumb button) */
	void GraphNavigateHistoryForward();

	/** Gets the tool tip text for the history back button */
	FText GetHistoryBackTooltip() const;

	/** Gets the tool tip text for the history forward button */
	FText GetHistoryForwardTooltip() const;

	/** Gets the text to be displayed in the address bar */
	FText GetAddressBarText() const;

	/** Called when the path is being edited */
	void OnAddressBarTextChanged(const FText& NewText);

	/** Sets the new path for the viewer */
	void OnAddressBarTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo);

	void OnApplyHistoryData(const FReferenceViewerHistoryData& History);

	void OnUpdateHistoryData(FReferenceViewerHistoryData& HistoryData) const;
	
	void OnSearchDepthEnabledChanged( ECheckBoxState NewState );
	ECheckBoxState IsSearchDepthEnabledChecked() const;
	int32 GetSearchDepthCount() const;
	void OnSearchDepthCommitted(int32 NewValue);

	void OnSearchBreadthEnabledChanged( ECheckBoxState NewState );
	ECheckBoxState IsSearchBreadthEnabledChecked() const;

	void OnEnableCollectionFilterChanged(ECheckBoxState NewState);
	ECheckBoxState IsEnableCollectionFilterChecked() const;
	TSharedRef<SWidget> GenerateCollectionFilterItem(TSharedPtr<FName> InItem);
	void HandleCollectionFilterChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo);
	FText GetCollectionFilterText() const;

	void OnShowSoftReferencesChanged( ECheckBoxState NewState );
	ECheckBoxState IsShowSoftReferencesChecked() const;
	void OnShowHardReferencesChanged(ECheckBoxState NewState);
	ECheckBoxState IsShowHardReferencesChecked() const;

	EVisibility GetManagementReferencesVisibility() const;
	void OnShowManagementReferencesChanged(ECheckBoxState NewState);
	ECheckBoxState IsShowManagementReferencesChecked() const;

	void OnShowSearchableNamesChanged(ECheckBoxState NewState);
	ECheckBoxState IsShowSearchableNamesChecked() const;
	void OnShowNativePackagesChanged(ECheckBoxState NewState);
	ECheckBoxState IsShowNativePackagesChecked() const;

	int32 GetSearchBreadthCount() const;
	void OnSearchBreadthCommitted(int32 NewValue);

	void RegisterActions();
	void ShowSelectionInContentBrowser();
	void OpenSelectedInAssetEditor();
	void ReCenterGraph();
	void CopyReferencedObjects();
	void CopyReferencingObjects();
	void ShowReferencedObjects();
	void ShowReferencingObjects();
	void MakeCollectionWithReferencersOrDependencies(ECollectionShareType::Type ShareType, bool bReferencers);
	void ShowReferenceTree();
	void ViewSizeMap();
	void ViewAssetAudit();
	void ZoomToFit();
	bool CanZoomToFit() const;
	void OnFind();

	/** Handlers for searching */
	void HandleOnSearchTextChanged(const FText& SearchText);
	void HandleOnSearchTextCommitted(const FText& SearchText, ETextCommit::Type CommitType);

	void ReCenterGraphOnNodes(const TSet<UObject*>& Nodes);

	FString GetReferencedObjectsList() const;
	FString GetReferencingObjectsList() const;

	UObject* GetObjectFromSingleSelectedNode() const;
	void GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const;
	bool HasExactlyOneNodeSelected() const;
	bool HasExactlyOnePackageNodeSelected() const;
	bool HasAtLeastOnePackageNodeSelected() const;
	bool HasAtLeastOneRealNodeSelected() const;

	void OnInitialAssetRegistrySearchComplete();
	EActiveTimerReturnType TriggerZoomToFit(double InCurrentTime, float InDeltaTime);
private:

	/** The manager that keeps track of history data for this browser */
	FReferenceViewerHistoryManager HistoryManager;

	TSharedPtr<SGraphEditor> GraphEditorPtr;

	TSharedPtr<FUICommandList> ReferenceViewerActions;
	TSharedPtr<SSearchBox> SearchBox;

	UEdGraph_ReferenceViewer* GraphObj;

	/** The temporary copy of the path text when it is actively being edited. */
	FText TemporaryPathBeingEdited;

	/** List of collection filter options */
	TArray<TSharedPtr<FName>> CollectionsComboList;
};
