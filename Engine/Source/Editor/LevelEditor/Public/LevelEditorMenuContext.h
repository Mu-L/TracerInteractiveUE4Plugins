// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LevelEditorMenuContext.generated.h"

class SLevelEditor;
class UActorComponent;
class SLevelViewport;
class SLevelViewportToolBar;
class FLevelEditorViewportClient;

UCLASS()
class LEVELEDITOR_API ULevelEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<SLevelEditor> LevelEditor;
};

/** Enum to describe what a level editor context menu should be built for */
enum class ELevelEditorMenuContext
{
	/** This context menu is applicable to a viewport */
	Viewport,
	/** This context menu is applicable to the Scene Outliner (disables click-position-based menu items) */
	SceneOutliner,
};

UCLASS()
class LEVELEDITOR_API ULevelEditorContextMenuContext : public UObject
{
	GENERATED_BODY()
public:

	TWeakPtr<SLevelEditor> LevelEditor;
	TArray<UActorComponent*> SelectedComponents;
	ELevelEditorMenuContext ContextType;
};

UCLASS()
class LEVELEDITOR_API ULevelViewportToolBarContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<SLevelViewportToolBar> LevelViewportToolBarWidget;
	TWeakPtr<const SLevelViewportToolBar> LevelViewportToolBarWidgetConst;

	FLevelEditorViewportClient* GetLevelViewportClient();
};
