// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "UObject/ObjectMacros.h"
#include "Textures/SlateIcon.h"
#include "K2Node.h"
#include "K2Node_JsonLibraryFromStruct.generated.h"

class FBlueprintActionDatabaseRegistrar;
class UEdGraph;

UCLASS()
class JSONLIBRARYBLUEPRINTSUPPORT_API UK2Node_JsonLibraryFromStruct : public UK2Node
{
	GENERATED_UCLASS_BODY()

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle( ENodeTitleType::Type TitleType ) const override;
	virtual FText GetTooltipText() const override;
	virtual void ExpandNode( class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph ) override;
	virtual FSlateIcon GetIconAndTint( FLinearColor& OutColor ) const override;
	virtual void PostReconstructNode() override;

	virtual bool IsNodeSafeToIgnore() const override { return true; }
	virtual void GetMenuActions( FBlueprintActionDatabaseRegistrar& ActionRegistrar ) const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsConnectionDisallowed( const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason ) const override;
	virtual void EarlyValidation( class FCompilerResultsLog& MessageLog ) const override;
	virtual void NotifyPinConnectionListChanged( UEdGraphPin* Pin ) override;

	UScriptStruct* GetPropertyTypeForStruct() const;
	
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetDataPin() const;
	UEdGraphPin* GetFailedPin() const;
	UEdGraphPin* GetResultPin() const;

private:
	
	void SetPinToolTip( UEdGraphPin& MutatablePin, const FText& PinDescription ) const;

	void SetPropertyTypeForStruct( UScriptStruct* InClass );
	void RefreshInputPinType();

	FText NodeTooltip;
	FNodeTextCache CachedNodeTitle;
};
