// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshEditorCommands.h"

#include "UnifyNormals.generated.h"

UCLASS()
class POLYGONMODELING_API UUnifyNormalsCommand : public UMeshEditorInstantCommand
{
	GENERATED_BODY()

public:

	// Overrides
	virtual EEditableMeshElementType GetElementType() const override { return EEditableMeshElementType::Polygon; }

	virtual void RegisterUICommand(class FBindingContext* BindingContext) override;
	virtual void Execute(class IMeshEditorModeEditingContract& MeshEditorMode) override;
};