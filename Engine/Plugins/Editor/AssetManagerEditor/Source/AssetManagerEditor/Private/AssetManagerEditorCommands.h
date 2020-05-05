// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"

// Actions that can be invoked in the reference viewer
class FAssetManagerEditorCommands : public TCommands<FAssetManagerEditorCommands>
{
public:
	FAssetManagerEditorCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;
	// End of TCommands<> interface

	// Shows the reference viewer for the selected assets
	TSharedPtr<FUICommandInfo> ViewReferences;

	// Shows a size map for the selected assets
	TSharedPtr<FUICommandInfo> ViewSizeMap;

	// Shows shader cook statistics
	TSharedPtr<FUICommandInfo> ViewShaderCookStatistics;

	// Adds assets to asset audit window
	TSharedPtr<FUICommandInfo> ViewAssetAudit;

	// Opens the selected asset in the asset editor
	TSharedPtr<FUICommandInfo> OpenSelectedInAssetEditor;

	// Re-constructs the graph with the selected asset as the center
	TSharedPtr<FUICommandInfo> ReCenterGraph;

	// Copies the list of objects that the selected asset references
	TSharedPtr<FUICommandInfo> CopyReferencedObjects;

	// Copies the list of objects that reference the selected asset
	TSharedPtr<FUICommandInfo> CopyReferencingObjects;

	// Shows a list of objects that the selected asset references
	TSharedPtr<FUICommandInfo> ShowReferencedObjects;

	// Shows a list of objects that reference the selected asset
	TSharedPtr<FUICommandInfo> ShowReferencingObjects;

	// Shows a reference tree for the selected asset
	TSharedPtr<FUICommandInfo> ShowReferenceTree;

	// Adds all referenced objects to asset audit window
	TSharedPtr<FUICommandInfo> AuditReferencedObjects;

	/** Zoom in to fit the selected objects in the window */
	TSharedPtr<FUICommandInfo> ZoomToFit;
	
	/** Start finding objects */
	TSharedPtr<FUICommandInfo> Find;

	// Creates a new collection with the list of assets that this asset references, user selects which ECollectionShareType to use.
	TSharedPtr<FUICommandInfo> MakeLocalCollectionWithReferencers;
	TSharedPtr<FUICommandInfo> MakePrivateCollectionWithReferencers;
	TSharedPtr<FUICommandInfo> MakeSharedCollectionWithReferencers;
	TSharedPtr<FUICommandInfo> MakeLocalCollectionWithDependencies;
	TSharedPtr<FUICommandInfo> MakePrivateCollectionWithDependencies;
	TSharedPtr<FUICommandInfo> MakeSharedCollectionWithDependencies;
};
