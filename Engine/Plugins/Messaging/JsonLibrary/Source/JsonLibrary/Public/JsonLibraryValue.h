// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "JsonLibraryEnums.h"
#include "JsonLibraryValue.generated.h"

typedef struct FJsonLibraryObject FJsonLibraryObject;
typedef struct FJsonLibraryList FJsonLibraryList;

USTRUCT(BlueprintType, meta = (DisplayName = "JSON Value"))
struct JSONLIBRARY_API FJsonLibraryValue
{
	friend struct FJsonLibraryList;
	friend struct FJsonLibraryObject;

	GENERATED_USTRUCT_BODY()

protected:

	FJsonLibraryValue( const TSharedPtr<FJsonValue>& Value );

public:

	FJsonLibraryValue();

	FJsonLibraryValue( bool Value );
	FJsonLibraryValue( float Value );
	FJsonLibraryValue( double Value );
	FJsonLibraryValue( int8 Value );
	FJsonLibraryValue( uint8 Value );
	FJsonLibraryValue( int16 Value );
	FJsonLibraryValue( uint16 Value );
	FJsonLibraryValue( int32 Value );
	FJsonLibraryValue( uint32 Value );
	FJsonLibraryValue( int64 Value );
	FJsonLibraryValue( uint64 Value );
	FJsonLibraryValue( const FString& Value );

	FJsonLibraryValue( const FJsonLibraryObject& Value );
	FJsonLibraryValue( const FJsonLibraryList& Value );

	FJsonLibraryValue( const TArray<FJsonLibraryValue>& Value );
	FJsonLibraryValue( const TMap<FString, FJsonLibraryValue>& Value );

	// Get the JSON type of this value.
	EJsonLibraryType GetType() const;

	// Check if this value equals another JSON value.
	bool Equals( const FJsonLibraryValue& Value, bool bStrict = false ) const;

	// Convert this value to a boolean.
	bool GetBoolean() const;
	// Convert this value to a float.
	float GetFloat() const;
	// Convert this value to an integer.
	int32 GetInteger() const;
	// Convert this value to a number.
	double GetNumber() const;
	// Convert this value to a string.
	FString GetString() const;

	// Convert this value to a JSON object.
	FJsonLibraryObject GetObject() const;
	// Convert this value to a JSON array.
	FJsonLibraryList GetList() const;

	// Convert this value to a 8-bit signed integer.
	int8 GetInt8() const;
	// Convert this value to a 16-bit signed integer.
	int16 GetInt16() const;
	// Convert this value to a 32-bit signed integer.
	int32 GetInt32() const;
	// Convert this value to a 64-bit signed integer.
	int64 GetInt64() const;
	// Convert this value to a 8-bit unsigned integer.
	uint8 GetUInt8() const;
	// Convert this value to a 16-bit unsigned integer.
	uint16 GetUInt16() const;
	// Convert this value to a 32-bit unsigned integer.
	uint32 GetUInt32() const;
	// Convert this value to a 64-bit unsigned integer.
	uint64 GetUInt64() const;

protected:
	
	TSharedPtr<FJsonValue> JsonValue;

	bool TryParse( const FString& Text );
	bool TryStringify( FString& Text, bool bCondensed = true ) const;

public:

	// Check if this value is valid.
	bool IsValid() const;

	// Parse a JSON string.
	static FJsonLibraryValue Parse( const FString& Text );

	// Stringify this value as a JSON string.
	FString Stringify() const;

	// Copy this value to an array of JSON values.
	TArray<FJsonLibraryValue> ToArray() const;
	// Copy this value to a map of JSON values.
	TMap<FString, FJsonLibraryValue> ToMap() const;

	bool operator==( const FJsonLibraryValue& Value ) const;
	bool operator!=( const FJsonLibraryValue& Value ) const;

	bool operator==( const FJsonLibraryObject& Object ) const;
	bool operator!=( const FJsonLibraryObject& Object ) const;

	bool operator==( const FJsonLibraryList& List ) const;
	bool operator!=( const FJsonLibraryList& List ) const;
};
