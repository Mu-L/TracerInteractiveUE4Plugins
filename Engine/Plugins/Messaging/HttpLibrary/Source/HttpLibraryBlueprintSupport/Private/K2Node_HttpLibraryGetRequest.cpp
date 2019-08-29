// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "K2Node_HttpLibraryGetRequest.h"
#include "HttpLibraryGetRequestCallbackProxy.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_HttpLibraryGetRequest::UK2Node_HttpLibraryGetRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED( UHttpLibraryGetRequestCallbackProxy, CreateProxyObjectForGet );
	ProxyFactoryClass = UHttpLibraryGetRequestCallbackProxy::StaticClass();

	ProxyClass = UHttpLibraryGetRequestCallbackProxy::StaticClass();
}

FText UK2Node_HttpLibraryGetRequest::GetTooltipText() const
{
	return LOCTEXT( "K2Node_HttpLibraryGetRequest_Tooltip", "Send an HTTP GET request" );
}
FText UK2Node_HttpLibraryGetRequest::GetNodeTitle( ENodeTitleType::Type TitleType ) const
{
	return LOCTEXT( "HttpLibraryGetRequest", "HTTP GET Request" );
}
void UK2Node_HttpLibraryGetRequest::GetPinHoverText( const UEdGraphPin& Pin, FString& HoverTextOut ) const
{
	Super::GetPinHoverText( Pin, HoverTextOut );

	static FName NAME_OnSuccess = FName( TEXT( "OnSuccess" ) );
	static FName NAME_OnFailure = FName( TEXT( "OnFailure" ) );

	if ( Pin.PinName == NAME_OnSuccess )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryGetRequest_OnSuccess_Tooltip", "Event called when the HTTP request has successfully completed." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
	else if ( Pin.PinName == NAME_OnFailure )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryGetRequest_OnFailure_Tooltip", "Event called when the HTTP request has failed with an error code." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
}

FText UK2Node_HttpLibraryGetRequest::GetMenuCategory() const
{
	return LOCTEXT( "HttpLibraryGetRequestCategory", "HTTP Library" );
}

#undef LOCTEXT_NAMESPACE
