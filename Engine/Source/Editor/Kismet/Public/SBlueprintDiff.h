// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "GraphEditor.h"
#include "DiffUtils.h"
#include "DiffResults.h"
#include "SKismetInspector.h"
#include "Developer/AssetTools/Public/IAssetTypeActions.h"

class FSpawnTabArgs;
class FTabManager;
class IDiffControl;
class SMyBlueprint;
class UEdGraph;
struct FGraphToDiff;
enum class EAssetEditorCloseReason : uint8;

/** Individual Diff item shown in the list of diffs */
struct FDiffResultItem : public TSharedFromThis<FDiffResultItem>
{
	FDiffResultItem(FDiffSingleResult InResult) : Result(InResult){}

	FDiffSingleResult Result;

	TSharedRef<SWidget> KISMET_API GenerateWidget() const;
};

DECLARE_DELEGATE_OneParam(FOnMyBlueprintActionSelected, UObject*);

namespace DiffWidgetUtils
{
	KISMET_API void SelectNextRow(SListView< TSharedPtr< struct FDiffSingleResult> >& ListView, const TArray< TSharedPtr< struct FDiffSingleResult > >& ListViewSource );
	KISMET_API void SelectPrevRow(SListView< TSharedPtr< struct FDiffSingleResult> >& ListView, const TArray< TSharedPtr< struct FDiffSingleResult > >& ListViewSource );
	KISMET_API bool HasNextDifference(SListView< TSharedPtr< struct FDiffSingleResult> >& ListView, const TArray< TSharedPtr< struct FDiffSingleResult > >& ListViewSource);
	KISMET_API bool HasPrevDifference(SListView< TSharedPtr< struct FDiffSingleResult> >& ListView, const TArray< TSharedPtr< struct FDiffSingleResult > >& ListViewSource);
}

/** Panel used to display the blueprint */
struct KISMET_API FDiffPanel
{
	FDiffPanel();

	/** Initializes the panel, can be moved into constructor if diff and merge clients are made more uniform: */
	void InitializeDiffPanel();

	/** Generate this panel based on the specified graph */
	void GeneratePanel(UEdGraph* Graph, UEdGraph* GraphToDiff);

	/** Generate the 'MyBlueprint' widget, which is private to this module */
	TSharedRef<class SWidget> GenerateMyBlueprintWidget();

	/** Called when user hits keyboard shortcut to copy nodes */
	void CopySelectedNodes();

	/** Gets whatever nodes are selected in the Graph Editor */
	FGraphPanelSelectionSet GetSelectedNodes() const;

	/** Can user copy any of the selected nodes? */
	bool CanCopyNodes() const;

	/** Functions used to focus/find a particular change in a diff result */
	void FocusDiff(UEdGraphPin& Pin);
	void FocusDiff(UEdGraphNode& Node);

	/** The blueprint that owns the graph we are showing */
	const UBlueprint*				Blueprint;

	/** The box around the graph editor, used to change the content when new graphs are set */
	TSharedPtr<SBox>				GraphEditorBox;

	/** The actual my blueprint panel, used to regenerate the panel when the new graphs are set */
	TSharedPtr<class SMyBlueprint>	MyBlueprint;

	/** The details view associated with the graph editor */
	TSharedPtr<class SKismetInspector>	DetailsView;

	/** The graph editor which does the work of displaying the graph */
	TWeakPtr<class SGraphEditor>	GraphEditor;

	/** Revision information for this blueprint */
	FRevisionInfo					RevisionInfo;

	/** True if we should show a name identifying which asset this panel is displaying */
	bool							bShowAssetName;

	/** The panel stores the last pin that was focused on by the user, so that it can clear the visual style when selection changes */
	UEdGraphPin*					LastFocusedPin;
private:
	/** Command list for this diff panel */
	TSharedPtr<FUICommandList> GraphEditorCommands;
};

/* Visual Diff between two Blueprints*/
class  KISMET_API SBlueprintDiff: public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams( FOpenInDefaults, const class UBlueprint* , const class UBlueprint* );

	SLATE_BEGIN_ARGS( SBlueprintDiff ){}
			SLATE_ARGUMENT( const class UBlueprint*, BlueprintOld )
			SLATE_ARGUMENT( const class UBlueprint*, BlueprintNew )
			SLATE_ARGUMENT( struct FRevisionInfo, OldRevision )
			SLATE_ARGUMENT( struct FRevisionInfo, NewRevision )
			SLATE_ARGUMENT( bool, ShowAssetNames )
			SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBlueprintDiff();

	/** Called when a new Graph is clicked on by user */
	void OnGraphChanged(FGraphToDiff* Diff);

	/** Called when blueprint is modified */
	void OnBlueprintChanged(UBlueprint* InBlueprint);

	/** Called when user clicks on a new graph list item */
	void OnGraphSelectionChanged(TSharedPtr<FGraphToDiff> Item, ESelectInfo::Type SelectionType);

	/** Called when user clicks on an entry in the listview of differences */
	void OnDiffListSelectionChanged(TSharedPtr<struct FDiffResultItem> TheDiff);

	/** Helper function for generating an empty widget */
	static TSharedRef<SWidget> DefaultEmptyPanel();

	/** Helper function to create a window that holds a diff widget */
	static TSharedPtr<SWindow> CreateDiffWindow(FText WindowTitle, UBlueprint* OldBlueprint, UBlueprint* NewBlueprint, const struct FRevisionInfo& OldRevision, const struct FRevisionInfo& NewRevision);

protected:
	/** Called when user clicks button to go to next difference */
	void NextDiff();

	/** Called when user clicks button to go to prev difference */
	void PrevDiff();

	/** Called to determine whether we have a list of differences to cycle through */
	bool HasNextDiff() const;
	bool HasPrevDiff() const;

	/** Find the FGraphToDiff that displays the graph with GraphPath relative path */
	FGraphToDiff* FindGraphToDiffEntry(const FString& GraphPath);

	/** Bring these revisions of graph into focus on main display*/
	void FocusOnGraphRevisions(FGraphToDiff* Diff);

	/** Create a list item entry graph that exists in at least one of the blueprints */
	void CreateGraphEntry(class UEdGraph* GraphOld, class UEdGraph* GraphNew);
		
	/** Disable the focus on a particular pin */
	void DisablePinDiffFocus();

	/** User toggles the option to lock the views between the two blueprints */
	void OnToggleLockView();

	/** Reset the graph editor, called when user switches graphs to display*/
	void ResetGraphEditors();

	/** Get the image to show for the toggle lock option*/
	FSlateIcon GetLockViewImage() const;

	/** List of graphs to diff, are added to panel last */
	TArray<TSharedPtr<FGraphToDiff>> Graphs;

	/** Get Graph editor associated with this Graph */
	FDiffPanel& GetDiffPanelForNode(UEdGraphNode& Node);

	/** Event handler that updates the graph view when user selects a new graph */
	void HandleGraphChanged( const FString& GraphPath );
	
	/** Function used to generate the list of differences and the widgets needed to calculate that list */
	void GenerateDifferencesList();

	/** Checks if a graph is valid for diffing */
	bool IsGraphDiffNeeded(class UEdGraph* InGraph) const;

	/** Called when editor may need to be closed */
	void OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason);

	struct FDiffControl
	{
		FDiffControl()
		: Widget()
		, DiffControl(nullptr)
		{
		}

		TSharedPtr<SWidget> Widget;
		TSharedPtr< class IDiffControl > DiffControl;
	};

	FDiffControl GenerateBlueprintTypePanel();
	FDiffControl GenerateMyBlueprintPanel();
	FDiffControl GenerateGraphPanel();
	FDiffControl GenerateDefaultsPanel();
	FDiffControl GenerateClassSettingsPanel();
	FDiffControl GenerateComponentsPanel();

	/** Accessor and event handler for toggling between diff view modes (defaults, components, graph view, interface, macro): */
	void SetCurrentMode(FName NewMode);
	FName GetCurrentMode() const { return CurrentMode; }

	FName CurrentMode;

	/*The two panels used to show the old & new revision*/ 
	FDiffPanel				PanelOld, PanelNew;
	
	/** If the two views should be locked */
	bool	bLockViews;

	/** Contents widget that we swap when mode changes (defaults, components, etc) */
	TSharedPtr<SBox> ModeContents;

	friend struct FListItemGraphToDiff;

	/** We can't use the global tab manager because we need to instance the diff control, so we have our own tab manager: */
	TSharedPtr<FTabManager> TabManager;

	/** Tree of differences collected across all panels: */
	TArray< TSharedPtr<class FBlueprintDifferenceTreeEntry> > MasterDifferencesList;

	/** List of all differences, cached so that we can iterate only the differences and not labels, etc: */
	TArray< TSharedPtr<class FBlueprintDifferenceTreeEntry> > RealDifferences;

	/** Tree view that displays the differences, cached for the buttons that iterate the differences: */
	TSharedPtr< STreeView< TSharedPtr< FBlueprintDifferenceTreeEntry > > > DifferencesTreeView;

	/** Stored references to widgets used to display various parts of a blueprint, from the mode name */
	TMap<FName, FDiffControl> ModePanels;

	/** A pointer to the window holding this */
	TWeakPtr<SWindow> WeakParentWindow;

	FDelegateHandle AssetEditorCloseDelegate;
};


