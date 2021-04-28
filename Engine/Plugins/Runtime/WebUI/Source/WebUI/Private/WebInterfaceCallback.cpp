// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceCallback.h"
#include "WebInterface.h"

FWebInterfaceCallback::FWebInterfaceCallback()
{
	//
}

FWebInterfaceCallback::FWebInterfaceCallback( TWeakObjectPtr<UWebInterface> Interface, const FString& Callback )
{
	MyInterface = Interface;
	MyCallback  = Callback;
}

bool FWebInterfaceCallback::IsValid() const
{
	if ( MyInterface.IsValid() )
		return !MyCallback.IsEmpty();

	return false;
}

void FWebInterfaceCallback::Call( const FJsonLibraryValue& Data )
{
	if ( !MyInterface.IsValid() || MyCallback.IsEmpty() )
		return;

	if ( Data.GetType() != EJsonLibraryType::Invalid )
		MyInterface->Execute( FString::Printf( TEXT( "ue.interface[%s](%s)" ),
			*FJsonLibraryValue( MyCallback ).Stringify(),
			*Data.Stringify() ) );
	else
		MyInterface->Execute( FString::Printf( TEXT( "ue.interface[%s]()" ),
			*FJsonLibraryValue( MyCallback ).Stringify() ) );
}
