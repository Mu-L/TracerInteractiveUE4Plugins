// Copyright 2020 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceHelpers.h"

bool UWebInterfaceHelpers::WebInterfaceCallback_IsValid( const FWebInterfaceCallback& Target )
{
	return Target.IsValid();
}

void UWebInterfaceHelpers::WebInterfaceCallback_Call( FWebInterfaceCallback& Target, const FJsonLibraryValue& Data )
{
	Target.Call( Data );
}
