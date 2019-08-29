// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "K2Node_HttpLibraryPostRequest.h"
#include "HttpLibraryPostRequestCallbackProxy.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_HttpLibraryPostRequest::UK2Node_HttpLibraryPostRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED( UHttpLibraryPostRequestCallbackProxy, CreateProxyObjectForPost );
	ProxyFactoryClass = UHttpLibraryPostRequestCallbackProxy::StaticClass();

	ProxyClass = UHttpLibraryPostRequestCallbackProxy::StaticClass();
}

FText UK2Node_HttpLibraryPostRequest::GetTooltipText() const
{
	return LOCTEXT( "K2Node_HttpLibraryPostRequest_Tooltip", "Send an HTTP POST request" );
}
FText UK2Node_HttpLibraryPostRequest::GetNodeTitle( ENodeTitleType::Type TitleType ) const
{
	return LOCTEXT( "HttpLibraryPostRequest", "HTTP POST Request" );
}
void UK2Node_HttpLibraryPostRequest::GetPinHoverText( const UEdGraphPin& Pin, FString& HoverTextOut ) const
{
	Super::GetPinHoverText( Pin, HoverTextOut );

	static FName NAME_OnSuccess = FName( TEXT( "OnSuccess" ) );
	static FName NAME_OnFailure = FName( TEXT( "OnFailure" ) );

	if ( Pin.PinName == NAME_OnSuccess )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryPostRequest_OnSuccess_Tooltip", "Event called when the HTTP request has successfully completed." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
	else if ( Pin.PinName == NAME_OnFailure )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryPostRequest_OnFailure_Tooltip", "Event called when the HTTP request has failed with an error code." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
}

FText UK2Node_HttpLibraryPostRequest::GetMenuCategory() const
{
	return LOCTEXT( "HttpLibraryPostRequestCategory", "HTTP Library" );
}

#undef LOCTEXT_NAMESPACE
