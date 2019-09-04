// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "K2Node_HttpLibraryRequest.h"
#include "HttpLibraryRequestCallbackProxy.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_HttpLibraryRequest::UK2Node_HttpLibraryRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED( UHttpLibraryRequestCallbackProxy, CreateProxyObjectForRequest );
	ProxyFactoryClass = UHttpLibraryRequestCallbackProxy::StaticClass();

	ProxyClass = UHttpLibraryRequestCallbackProxy::StaticClass();
}

FText UK2Node_HttpLibraryRequest::GetTooltipText() const
{
	return LOCTEXT( "K2Node_HttpLibraryRequest_Tooltip", "Send an HTTP request" );
}
FText UK2Node_HttpLibraryRequest::GetNodeTitle( ENodeTitleType::Type TitleType ) const
{
	return LOCTEXT( "HttpLibraryRequest", "HTTP Request" );
}
void UK2Node_HttpLibraryRequest::GetPinHoverText( const UEdGraphPin& Pin, FString& HoverTextOut ) const
{
	Super::GetPinHoverText( Pin, HoverTextOut );

	static FName NAME_OnSuccess  = FName( TEXT( "OnSuccess" ) );
	static FName NAME_OnProgress = FName( TEXT( "OnProgress" ) );
	static FName NAME_OnFailure  = FName( TEXT( "OnFailure" ) );

	if ( Pin.PinName == NAME_OnSuccess )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryRequest_OnSuccess_Tooltip", "Event called when the HTTP request has successfully completed." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
	else if ( Pin.PinName == NAME_OnProgress )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryRequest_OnProgress_Tooltip", "Event called when the HTTP request has a progress update." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
	else if ( Pin.PinName == NAME_OnFailure )
	{
		FText ToolTipText = LOCTEXT( "K2Node_HttpLibraryRequest_OnFailure_Tooltip", "Event called when the HTTP request has failed with an error code." );
		HoverTextOut = FString::Printf( TEXT( "%s\n%s" ), *ToolTipText.ToString(), *HoverTextOut );
	}
}

FText UK2Node_HttpLibraryRequest::GetMenuCategory() const
{
	return LOCTEXT( "HttpLibraryRequestCategory", "HTTP Library" );
}

#undef LOCTEXT_NAMESPACE
