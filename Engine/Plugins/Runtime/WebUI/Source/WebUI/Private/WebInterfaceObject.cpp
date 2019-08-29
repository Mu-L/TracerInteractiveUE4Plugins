// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceObject.h"
#include "WebInterface.h"

void UWebInterfaceObject::Broadcast( const FString& Name, const FString& Data )
{
	if ( MyInterface.IsValid() )
		MyInterface->OnInterfaceEvent.Broadcast( FName( *Name ), FJsonLibraryValue::Parse( Data ) );
}
