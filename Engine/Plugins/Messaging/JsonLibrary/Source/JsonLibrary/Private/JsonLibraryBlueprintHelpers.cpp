// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryBlueprintHelpers.h"
#include "Engine/UserDefinedStruct.h"

bool UJsonLibraryBlueprintHelpers::StructFromJson( const UScriptStruct* StructType, const FJsonLibraryObject& Object, FStructBase& OutStruct )
{
	check( 0 );
	return false;
}

FJsonLibraryObject UJsonLibraryBlueprintHelpers::StructToJson( const UScriptStruct* StructType, const FStructBase& Struct )
{
	check( 0 );
	return FJsonLibraryObject();
}

bool UJsonLibraryBlueprintHelpers::Generic_StructFromJson( const UScriptStruct* StructType, const FJsonLibraryObject& Object, void* OutStructPtr )
{
	if ( !StructType || !OutStructPtr )
		return false;
	if ( !Object.IsValid() )
		return false;
	
	return Object.ToStruct( StructType, OutStructPtr );
}

bool UJsonLibraryBlueprintHelpers::Generic_StructToJson( const UScriptStruct* StructType, void* StructPtr, FJsonLibraryObject& OutObject )
{
	if ( !StructType || !StructPtr )
		return false;

	OutObject = FJsonLibraryObject( StructType, StructPtr );
	return OutObject.IsValid();
}

FJsonLibraryObject UJsonLibraryBlueprintHelpers::ConstructInvalidObject()
{
	return FJsonLibraryObject( TSharedPtr<FJsonValueObject>() );
}

bool UJsonLibraryBlueprintHelpers::IsValidObject( const FJsonLibraryObject& Object )
{
	return Object.IsValid();
}

bool UJsonLibraryBlueprintHelpers::InitializeStructData( const FJsonLibraryObject& Object, const UScriptStruct* StructType, FStructOnScope& StructData )
{
	if ( !StructType )
		return false;

	StructData.Initialize( StructType );

	void* StructPtr = StructData.GetStructMemory();
	if ( !StructPtr )
		return false;
	
	return Object.ToStruct( StructType, StructPtr );
}
