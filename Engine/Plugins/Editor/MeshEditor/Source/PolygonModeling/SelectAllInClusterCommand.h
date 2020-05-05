// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "MeshEditorCommands.h"
#include "EditableMesh.h"
#include "GeometryCollectionCommandCommon.h"
#include "SelectAllInClusterCommand.generated.h"

/** Select all nodes in cluster */
UCLASS()
class USelectAllInClusterCommand : public UMeshEditorInstantCommand, public FGeometryCollectionCommandCommon
{
public:
	GENERATED_BODY()

protected:

	// Overrides
	virtual EEditableMeshElementType GetElementType() const override
	{
		return EEditableMeshElementType::Fracture;
	}
	virtual void RegisterUICommand(class FBindingContext* BindingContext) override;
	virtual void Execute(class IMeshEditorModeEditingContract& MeshEditorMode) override;

private:
	void SelectAllInCluster(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors);
};
