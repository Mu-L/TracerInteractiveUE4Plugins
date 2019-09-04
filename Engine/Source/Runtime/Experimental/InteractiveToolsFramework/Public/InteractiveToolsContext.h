// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "InteractiveToolsContext.generated.h"

/**
 * InteractiveToolsContext owns a ToolManager and an InputRouter. This is just a top-level 
 * UObject container, however implementations like UEdModeInteractiveToolsContext extend
 * this class to make it easier to connect external systems (like an FEdMode) to the ToolsFramework.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolsContext : public UObject
{
	GENERATED_BODY()
	
public:
	UInteractiveToolsContext();

	/** 
	 * Initialize the Context. This creates the InputRouter and ToolManager 
	 * @param QueriesAPI client-provided implementation of the API for querying the higher-evel scene state
	 * @param TransactionsAPI client-provided implementation of the API for publishing events and transactions
	 */
	virtual void Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI);

	/** Shutdown Context by destroying InputRouter and ToolManager */
	virtual void Shutdown();

public:
	/** current UInputRouter for this Context */
	UPROPERTY()
	UInputRouter* InputRouter;	

	/** current UInteractiveToolManager for this Context */
	UPROPERTY()
	UInteractiveToolManager* ToolManager;	

	/** current UInteractiveGizmoManager for this Context */
	UPROPERTY()
	UInteractiveGizmoManager* GizmoManager;
};