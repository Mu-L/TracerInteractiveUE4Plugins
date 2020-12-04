// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_PixelMappingBaseComponent.h"
#include "K2Node_PixelMappingOutputDMXComponent.generated.h"

/**
 * Node for getting Output DMX Component from PixelMapping object and Output DMX FName
 */
UCLASS()
class DMXPIXELMAPPINGBLUEPRINTGRAPH_API UK2Node_PixelMappingOutputDMXComponent
	: public UK2Node_PixelMappingBaseComponent
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	//~ End UEdGraphNode Interface

	//~ Begin UK2Node Interface
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	//~ End UK2Node Interface

	//~ Begin UK2Node_PixelMappingBaseComponent Interface
	virtual void OnPixelMappingChanged(UDMXPixelMapping* InDMXPixelMapping) override;
	//~ End UK2Node_PixelMappingBaseComponent Interface

public:
	/** 
	 * Pointer to the input Output DMX group pin.
	 * The pin holds the FName of the non Public UObject component.
	 * Since it not possible to save NonPublic UObject references outside uasset it should be used as FName
	 */
	UEdGraphPin* GetInOutputDMXComponentPin() const;

	/** 
	 * Pointer to the output Output DMX group pin.
	 * It dynamically returns a pointer to Output DMX Component by input FName of the component.  
	 */
	UEdGraphPin* GetOutOutputDMXComponentPin() const;

public:
	/** Input Output DMX Component pin name. It holds a FName of the component. */
	static const FName InOutputDMXComponentPinName;

	/** Output Output DMX Component pin name. It holds a pointer to the component. */
	static const FName OutOutputDMXComponentPinName;
};
