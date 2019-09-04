// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "InteractiveToolsContext.h"
#include "EdModeInteractiveToolsContext.generated.h"

/**
 * EdModeInteractiveToolsContext is an extension/adapter of an InteractiveToolsContext which 
 * allows it to be easily embedded inside an FEdMode. A set of functions are provided which can be
 * called from the FEdMode functions of the same name. These will handle the data type
 * conversions and forwarding calls necessary to operate the ToolsContext
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEdModeInteractiveToolsContext : public UInteractiveToolsContext
{
	GENERATED_BODY()

public:
	UEdModeInteractiveToolsContext();


	virtual void InitializeContextFromEdMode(FEdMode* EditorMode);
	virtual void ShutdownContext();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnPIEStart();

	// default behavior is to accept active tool
	virtual void TerminateActiveToolsOnSaveWorld();

	IToolsContextQueriesAPI* GetQueriesAPI() const { return QueriesAPI; }
	IToolsContextTransactionsAPI* GetTransactionAPI() const { return TransactionAPI; }
	IToolsContextAssetAPI* GetAssetAPI() const { return AssetAPI; }
	IComponentSourceFactory* GetComponentSourceFactory() const { return SourceFactory; }

	virtual void PostInvalidation();


	// call these from your FEdMode functions of the same name

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime);
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI);

	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event);

	virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);
	virtual bool MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y);

	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY);
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);


	//
	// Utility functions useful for hooking up to UICommand/etc
	//

	virtual bool CanStartTool(const FString& ToolTypeIdentifier) const;
	virtual bool ActiveToolHasAccept() const;
	virtual bool CanAcceptActiveTool() const;
	virtual bool CanCancelActiveTool() const;
	virtual bool CanCompleteActiveTool() const;
	virtual void StartTool(const FString& ToolTypeIdentifier);
	virtual void EndTool(EToolShutdownType ShutdownType);


protected:
	// we hide these 
	virtual void Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI) override;
	virtual void Shutdown() override;


public:
	UPROPERTY()
	UMaterialInterface* StandardVertexColorMaterial;

protected:
	FEdMode* EditorMode;

	FDelegateHandle BeginPIEDelegateHandle;
	FDelegateHandle PreSaveWorldDelegateHandle;

	IToolsContextQueriesAPI* QueriesAPI;
	IToolsContextTransactionsAPI* TransactionAPI;
	IToolsContextAssetAPI* AssetAPI;
	IComponentSourceFactory* SourceFactory;

	bool bInvalidationPending;

	/** Input event instance used to keep track of various button states, etc, that we cannot directly query on-demand */
	FInputDeviceState CurrentMouseState;

	static FRay GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY);

};
