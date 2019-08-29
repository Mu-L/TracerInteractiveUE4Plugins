// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_HttpLibraryPostRequest.generated.h"

UCLASS()
class HTTPLIBRARYBLUEPRINTSUPPORT_API UK2Node_HttpLibraryPostRequest : public UK2Node_BaseAsyncTask
{
	GENERATED_UCLASS_BODY()

	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle( ENodeTitleType::Type TitleType ) const override;
	virtual void  GetPinHoverText( const UEdGraphPin& Pin, FString& HoverTextOut ) const override;

	virtual FText GetMenuCategory() const override;
};
