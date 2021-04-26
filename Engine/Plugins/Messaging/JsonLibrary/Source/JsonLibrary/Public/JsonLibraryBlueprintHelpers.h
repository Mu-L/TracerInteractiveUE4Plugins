// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JsonLibraryObject.h"
#include "JsonLibraryBlueprintHelpers.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FStructBase
{
	GENERATED_USTRUCT_BODY()

	FStructBase()
	{
	}

	virtual ~FStructBase()
	{
	}
};

UCLASS()
class JSONLIBRARY_API UJsonLibraryBlueprintHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "JSON Library", meta=(CustomStructureParam = "OutStruct", BlueprintInternalUseOnly="true"))
    static bool StructFromJson( const UScriptStruct* StructType, const FJsonLibraryObject& Object, FStructBase& OutStruct );
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "JSON Library", meta=(CustomStructureParam = "Struct", BlueprintInternalUseOnly="true"))
    static FJsonLibraryObject StructToJson( const UScriptStruct* StructType, const FStructBase& Struct );

	UFUNCTION(BlueprintCallable, Category = "JSON Library", meta=(BlueprintInternalUseOnly="true"))
	static FJsonLibraryObject ConstructInvalidObject();
	UFUNCTION(BlueprintPure, Category = "JSON Library", meta=(BlueprintInternalUseOnly="true"))
	static bool IsValidObject( const FJsonLibraryObject& Object );
    
	static bool Generic_StructFromJson( const UScriptStruct* StructType, const FJsonLibraryObject& Object, void* OutStructPtr );
    DECLARE_FUNCTION( execStructFromJson )
    {
        P_GET_OBJECT( UScriptStruct, StructType );
        P_GET_STRUCT( FJsonLibraryObject, Object );
        
		Stack.StepCompiledIn<FStructProperty>( NULL );
		void* OutStructPtr = Stack.MostRecentPropertyAddress;

		P_FINISH;
		bool bSuccess = false;
		
		P_NATIVE_BEGIN;
		bSuccess = Generic_StructFromJson( StructType, Object, OutStructPtr );
		P_NATIVE_END;

		*(bool*)RESULT_PARAM = bSuccess;
    }
    
	static bool Generic_StructToJson( const UScriptStruct* StructType, void* StructPtr, FJsonLibraryObject& OutObject );
    DECLARE_FUNCTION( execStructToJson )
    {
        P_GET_OBJECT( UScriptStruct, StructType );

		Stack.StepCompiledIn<FStructProperty>( NULL );
		void* StructPtr = Stack.MostRecentPropertyAddress;

		P_FINISH;
		bool bSuccess = false;
		FJsonLibraryObject OutObject = ConstructInvalidObject();
		
		P_NATIVE_BEGIN;
		bSuccess = Generic_StructToJson( StructType, StructPtr, OutObject );
		P_NATIVE_END;

		*(FJsonLibraryObject*)RESULT_PARAM = bSuccess ? OutObject : ConstructInvalidObject();
    }

	static bool InitializeStructData( const FJsonLibraryObject& Object, const UScriptStruct* StructType, FStructOnScope& StructData );
};
