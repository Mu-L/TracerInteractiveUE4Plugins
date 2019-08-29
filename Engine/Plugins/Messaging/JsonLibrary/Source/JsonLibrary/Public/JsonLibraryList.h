// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "JsonLibraryEnums.h"
#include "JsonLibraryValue.h"
#include "JsonLibraryList.generated.h"

typedef struct FJsonLibraryObject FJsonLibraryObject;

DECLARE_DYNAMIC_DELEGATE_FourParams( FJsonLibraryListNotify, const FJsonLibraryValue&, List, EJsonLibraryNotifyAction, Action, int32, Index, const FJsonLibraryValue&, Value );

USTRUCT(BlueprintType, meta = (DisplayName = "JSON List"))
struct JSONLIBRARY_API FJsonLibraryList
{
	friend struct FJsonLibraryObject;
	friend struct FJsonLibraryValue;

	GENERATED_USTRUCT_BODY()

protected:

	FJsonLibraryList( const TSharedPtr<FJsonValue>& Value );
	FJsonLibraryList( const TSharedPtr<FJsonValueArray>& Value );

public:

	FJsonLibraryList();
	FJsonLibraryList( const FJsonLibraryListNotify& Notify );

	FJsonLibraryList( const TArray<FJsonLibraryValue>& Value );
	FJsonLibraryList( const TArray<bool>& Value );
	FJsonLibraryList( const TArray<float>& Value );
	FJsonLibraryList( const TArray<double>& Value );
	FJsonLibraryList( const TArray<int32>& Value );
	FJsonLibraryList( const TArray<FString>& Value );
	FJsonLibraryList( const TArray<FJsonLibraryObject>& Value );

	// Check if this list equals another JSON array.
	bool Equals( const FJsonLibraryList& List ) const;

	// Get the number of items in this list.
	int32 Count() const;
	// Clear the items in this list.
	void Clear();
	// Swap two items in this list.
	void Swap( int32 IndexA, int32 IndexB );

	// Append a JSON array to this list.
	void Append( const FJsonLibraryList& List );

	// Append an array of booleans to this list.
	void AppendBooleanArray( const TArray<bool>& Array );
	// Append an array of floats to this list.
	void AppendFloatArray( const TArray<float>& Array );
	// Append an array of integers to this list.
	void AppendIntegerArray( const TArray<int32>& Array );
	// Append an array of numbers to this list.
	void AppendNumberArray( const TArray<double>& Array );
	// Append an array of strings to this list.
	void AppendStringArray( const TArray<FString>& Array );
	// Append an array of JSON objects to this list.
	void AppendObjectArray( const TArray<FJsonLibraryObject>& Array );

	// Inject the items of a JSON array into this list.
	void Inject( int32 Index, const FJsonLibraryList& List );

	// Inject the items of a boolean array into this list.
	void InjectBooleanArray( int32 Index, const TArray<bool>& Array );
	// Inject the items of a float array into this list.
	void InjectFloatArray( int32 Index, const TArray<float>& Array );
	// Inject the items of an integer array into this list.
	void InjectIntegerArray( int32 Index, const TArray<int32>& Array );
	// Inject the items of a number array into this list.
	void InjectNumberArray( int32 Index, const TArray<double>& Array );
	// Inject the items of a string array into this list.
	void InjectStringArray( int32 Index, const TArray<FString>& Array );
	// Inject the items of an array of JSON objects into this list.
	void InjectObjectArray( int32 Index, const TArray<FJsonLibraryObject>& Array );

	// Add a boolean to this list.
	void AddBoolean( bool Value );
	// Add a float to this list.
	void AddFloat( float Value );
	// Add an integer to this list.
	void AddInteger( int32 Value );
	// Add a number to this list.
	void AddNumber( double Value );
	// Add a string to this list.
	void AddString( const FString& Value );

	// Add a JSON value to this list.
	void AddValue( const FJsonLibraryValue& Value );
	// Add a JSON object to this list.
	void AddObject( const FJsonLibraryObject& Value );
	// Add a JSON array to this list.
	void AddList( const FJsonLibraryList& Value );

	// Add an array of JSON values to this list.
	void AddArray( const TArray<FJsonLibraryValue>& Value );
	// Add a map of JSON values to this list.
	void AddMap( const TMap<FString, FJsonLibraryValue>& Value );

	// Insert a boolean into this list.
	void InsertBoolean( int32 Index, bool Value );
	// Insert a float into this list.
	void InsertFloat( int32 Index, float Value );
	// Insert an integer into this list.
	void InsertInteger( int32 Index, int32 Value );
	// Insert a number into this list.
	void InsertNumber( int32 Index, double Value );
	// Insert a string into this list.
	void InsertString( int32 Index, const FString& Value );

	// Insert a JSON value into this list.
	void InsertValue( int32 Index, const FJsonLibraryValue& Value );
	// Insert a JSON object into this list.
	void InsertObject( int32 Index, const FJsonLibraryObject& Value );
	// Insert a JSON array into this list.
	void InsertList( int32 Index, const FJsonLibraryList& Value );

	// Insert an array of JSON values into this list.
	void InsertArray( int32 Index, const TArray<FJsonLibraryValue>& Value );
	// Insert a map of JSON values into this list.
	void InsertMap( int32 Index, const TMap<FString, FJsonLibraryValue>& Value );

	// Get an item as a boolean.
	bool GetBoolean( int32 Index ) const;
	// Get an item as a float.
	float GetFloat( int32 Index ) const;
	// Get an item as an integer.
	int32 GetInteger( int32 Index ) const;
	// Get an item as a number.
	double GetNumber( int32 Index ) const;
	// Get an item as a string.
	FString GetString( int32 Index ) const;

	// Get an item as a JSON value.
	FJsonLibraryValue GetValue( int32 Index ) const;
	// Get an item as a JSON object.
	FJsonLibraryObject GetObject( int32 Index ) const;
	// Get an item as a JSON array.
	FJsonLibraryList GetList( int32 Index ) const;

	// Copy an item to an array of JSON values.
	TArray<FJsonLibraryValue> GetArray( int32 Index ) const;
	// Copy an item to a map of JSON values.
	TMap<FString, FJsonLibraryValue> GetMap( int32 Index ) const;

	// Set an item as a boolean.
	void SetBoolean( int32 Index, bool Value );
	// Set an item as a float.
	void SetFloat( int32 Index, float Value );
	// Set an item as an integer.
	void SetInteger( int32 Index, int32 Value );
	// Set an item as a number.
	void SetNumber( int32 Index, double Value );
	// Set an item as a string.
	void SetString( int32 Index, const FString& Value );

	// Set an item as a JSON value.
	void SetValue( int32 Index, const FJsonLibraryValue& Value );
	// Set an item as a JSON object.
	void SetObject( int32 Index, const FJsonLibraryObject& Value );
	// Set an item as a JSON array.
	void SetList( int32 Index, const FJsonLibraryList& Value );
	
	// Set an item as an array of JSON values.
	void SetArray( int32 Index, const TArray<FJsonLibraryValue>& Value );
	// Set an item as a map of JSON values.
	void SetMap( int32 Index, const TMap<FString, FJsonLibraryValue>& Value );

	// Remove an item from this list.
	void Remove( int32 Index );

	// Remove a boolean from this list.
	void RemoveBoolean( bool Value );
	// Remove a float from this list.
	void RemoveFloat( float Value );
	// Remove an integer from this list.
	void RemoveInteger( int32 Value );
	// Remove a number from this list.
	void RemoveNumber( double Value );
	// Remove a string from this list.
	void RemoveString( const FString& Value );

	// Remove a JSON value from this list.
	void RemoveValue( const FJsonLibraryValue& Value );
	// Remove a JSON object from this list.
	void RemoveObject( const FJsonLibraryObject& Value );
	// Remove a JSON array from this list.
	void RemoveList( const FJsonLibraryList& Value );

	// Find a boolean in this list.
	int32 FindBoolean( bool Value, int32 Index = 0 ) const;
	// Find a float in this list.
	int32 FindFloat( float Value, int32 Index = 0 ) const;
	// Find an integer in this list.
	int32 FindInteger( int32 Value, int32 Index = 0 ) const;
	// Find a number in this list.
	int32 FindNumber( double Value, int32 Index = 0 ) const;
	// Find a string in this list.
	int32 FindString( const FString& Value, int32 Index = 0 ) const;

	// Find a JSON value in this list.
	int32 FindValue( const FJsonLibraryValue& Value, int32 Index = 0 ) const;
	// Find a JSON object in this list.
	int32 FindObject( const FJsonLibraryObject& Value, int32 Index = 0 ) const;
	// Find a JSON array in this list.
	int32 FindList( const FJsonLibraryList& Value, int32 Index = 0 ) const;

protected:
	
	TSharedPtr<FJsonValueArray> JsonArray;

	const TArray<TSharedPtr<FJsonValue>>* GetJsonArray() const;
	TArray<TSharedPtr<FJsonValue>>* SetJsonArray();

	bool TryParse( const FString& Text );
	bool TryStringify( FString& Text, bool bCondensed = true ) const;

private:

	FJsonLibraryListNotify OnNotify;

	bool bNotifyHasIndex;
	TSharedPtr<FJsonValue> NotifyValue;

	void NotifyAdd( int32 Index, const FJsonLibraryValue& Value );
	void NotifyChange( int32 Index, const FJsonLibraryValue& Value );
	bool NotifyCheck();
	bool NotifyCheck( int32 Index );
	void NotifyClear();
	void NotifyParse();
	void NotifyRemove( int32 Index );

public:

	// Check if this list is valid.
	bool IsValid() const;
	// Check if this list is empty.
	bool IsEmpty() const;

	// Parse a JSON string.
	static FJsonLibraryList Parse( const FString& Text );
	// Parse a JSON string.
	static FJsonLibraryList Parse( const FString& Text, const FJsonLibraryListNotify& Notify );

	// Stringify this list as a JSON string.
	FString Stringify() const;

	// Copy this list to an array of JSON values.
	TArray<FJsonLibraryValue> ToArray() const;

	// Copy this list to an array of booleans.
	TArray<bool> ToBooleanArray() const;
	// Copy this list to an array of floats.
	TArray<float> ToFloatArray() const;
	// Copy this list to an array of integers.
	TArray<int32> ToIntegerArray() const;
	// Copy this list to an array of numbers.
	TArray<double> ToNumberArray() const;
	// Copy this list to an array of strings.
	TArray<FString> ToStringArray() const;

	// Copy this list to an array of JSON objects.
	TArray<FJsonLibraryObject> ToObjectArray() const;

	bool operator==( const FJsonLibraryList& List ) const;
	bool operator!=( const FJsonLibraryList& List ) const;

	bool operator==( const FJsonLibraryValue& Value ) const;
	bool operator!=( const FJsonLibraryValue& Value ) const;
};
