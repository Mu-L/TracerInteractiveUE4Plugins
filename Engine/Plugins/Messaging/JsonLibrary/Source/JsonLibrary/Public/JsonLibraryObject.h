// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "UObject/StructOnScope.h"
#include "JsonLibraryEnums.h"
#include "JsonLibraryValue.h"
#include "JsonLibraryObject.generated.h"

typedef struct FJsonLibraryList FJsonLibraryList;

DECLARE_DYNAMIC_DELEGATE_FourParams( FJsonLibraryObjectNotify, const FJsonLibraryValue&, Object, EJsonLibraryNotifyAction, Action, const FString&, Key, const FJsonLibraryValue&, Value );

USTRUCT(BlueprintType, meta = (DisplayName = "JSON Object"))
struct JSONLIBRARY_API FJsonLibraryObject
{
	friend struct FJsonLibraryList;
	friend struct FJsonLibraryValue;

	friend class UJsonLibraryBlueprintHelpers;

	GENERATED_USTRUCT_BODY()

protected:

	FJsonLibraryObject( const TSharedPtr<FJsonValue>& Value );
	FJsonLibraryObject( const TSharedPtr<FJsonValueObject>& Value );

	FJsonLibraryObject( const UStruct* StructType, const void* StructPtr );

public:

	FJsonLibraryObject( const TSharedPtr<FStructOnScope>& StructData );
	
	template<typename StructType>
	FJsonLibraryObject( const StructType& Struct )
		: FJsonLibraryObject( StructType::StaticStruct(), &Struct )
	{
	}

	FJsonLibraryObject();
	FJsonLibraryObject( const FJsonLibraryObjectNotify& Notify );

	FJsonLibraryObject( const FLinearColor& Value );

	FJsonLibraryObject( const FRotator& Value );
	FJsonLibraryObject( const FTransform& Value );
	FJsonLibraryObject( const FVector& Value );

	FJsonLibraryObject( const TMap<FString, FJsonLibraryValue>& Value );
	FJsonLibraryObject( const TMap<FString, bool>& Value );
	FJsonLibraryObject( const TMap<FString, float>& Value );
	FJsonLibraryObject( const TMap<FString, double>& Value );
	FJsonLibraryObject( const TMap<FString, int32>& Value );
	FJsonLibraryObject( const TMap<FString, FString>& Value );

	FJsonLibraryObject( const TMap<FString, FDateTime>& Value );
	FJsonLibraryObject( const TMap<FString, FGuid>& Value );

	FJsonLibraryObject( const TMap<FString, FColor>& Value );
	FJsonLibraryObject( const TMap<FString, FLinearColor>& Value );

	FJsonLibraryObject( const TMap<FString, FRotator>& Value );
	FJsonLibraryObject( const TMap<FString, FTransform>& Value );
	FJsonLibraryObject( const TMap<FString, FVector>& Value );

	// Check if this object equals another JSON object.
	bool Equals( const FJsonLibraryObject& Object ) const;

	// Get the number of properties in this object.
	int32 Count() const;
	// Clear the properties in this object.
	void Clear();
	
	// Check if this object has a property.
	bool HasKey( const FString& Key ) const;
	// Remove a property from this object.
	void RemoveKey( const FString& Key );
	
	// Add a JSON object to this object.
	void Add( const FJsonLibraryObject& Object );

	// Add a map of booleans to this object.
	void AddBooleanMap( const TMap<FString, bool>& Map );
	// Add a map of floats to this object.
	void AddFloatMap( const TMap<FString, float>& Map );
	// Add a map of integers to this object.
	void AddIntegerMap( const TMap<FString, int32>& Map );
	// Add a map of numbers to this object.
	void AddNumberMap( const TMap<FString, double>& Map );
	// Add a map of strings to this object.
	void AddStringMap( const TMap<FString, FString>& Map );

	// Add a map of date/times to this object.
	void AddDateTimeMap( const TMap<FString, FDateTime>& Map );
	// Add a map of GUIDs to this object.
	void AddGuidMap( const TMap<FString, FGuid>& Map );

	// Add a map of colors to this object.
	void AddColorMap( const TMap<FString, FColor>& Map );
	// Add a map of linear colors to this object.
	void AddLinearColorMap( const TMap<FString, FLinearColor>& Map );

	// Add a map of rotators to this object.
	void AddRotatorMap( const TMap<FString, FRotator>& Map );
	// Add a map of transforms to this object.
	void AddTransformMap( const TMap<FString, FTransform>& Map );
	// Add a map of vectors to this object.
	void AddVectorMap( const TMap<FString, FVector>& Map );

	// Get the keys of this object as an array of strings.
	TArray<FString> GetKeys() const;
	// Get the values of this object as an array of JSON values.
	TArray<FJsonLibraryValue> GetValues() const;

	// Get a property as a boolean.
	bool GetBoolean( const FString& Key ) const;
	// Get a property as a float.
	float GetFloat( const FString& Key ) const;
	// Get a property as an integer.
	int32 GetInteger( const FString& Key ) const;
	// Get a property as a number.
	double GetNumber( const FString& Key ) const;
	// Get a property as a string.
	FString GetString( const FString& Key ) const;

	// Get a property as a date/time.
	FDateTime GetDateTime( const FString& Key ) const;
	// Get a property as a GUID.
	FGuid GetGuid( const FString& Key ) const;

	// Get a property as a color.
	FColor GetColor( const FString& Key ) const;
	// Get a property as a linear color.
	FLinearColor GetLinearColor( const FString& Key ) const;

	// Get a property as a rotator.
	FRotator GetRotator( const FString& Key ) const;
	// Get a property as a transform.
	FTransform GetTransform( const FString& Key ) const;
	// Get a property as a vector.
	FVector GetVector( const FString& Key ) const;
	
	// Get a property as a JSON value.
	FJsonLibraryValue GetValue( const FString& Key ) const;
	// Get a property as a JSON object.
	FJsonLibraryObject GetObject( const FString& Key ) const;
	// Get a property as a JSON array.
	FJsonLibraryList GetList( const FString& Key ) const;

	// Get a property as an array of JSON values.
	TArray<FJsonLibraryValue> GetArray( const FString& Key ) const;
	// Get a property as a map of JSON values.
	TMap<FString, FJsonLibraryValue> GetMap( const FString& Key ) const;
	
	// Set a property as a boolean.
	void SetBoolean( const FString& Key, bool Value );
	// Set a property as a float.
	void SetFloat( const FString& Key, float Value );
	// Set a property as an integer.
	void SetInteger( const FString& Key, int32 Value );
	// Set a property as a number.
	void SetNumber( const FString& Key, double Value );
	// Set a property as a string.
	void SetString( const FString& Key, const FString& Value );

	// Set a property as a date/time.
	void SetDateTime( const FString& Key, const FDateTime& Value );
	// Set a property as a GUID.
	void SetGuid( const FString& Key, const FGuid& Value );

	// Set a property as a color.
	void SetColor( const FString& Key, const FColor& Value );
	// Set a property as a linear color.
	void SetLinearColor( const FString& Key, const FLinearColor& Value );

	// Set a property as a rotator.
	void SetRotator( const FString& Key, const FRotator& Value );
	// Set a property as a transform.
	void SetTransform( const FString& Key, const FTransform& Value );
	// Set a property as a vector.
	void SetVector( const FString& Key, const FVector& Value );

	// Set a property as a JSON value.
	void SetValue( const FString& Key, const FJsonLibraryValue& Value );
	// Set a property as a JSON object.
	void SetObject( const FString& Key, const FJsonLibraryObject& Value );
	// Set a property as a JSON array.
	void SetList( const FString& Key, const FJsonLibraryList& Value );

	// Set a property as an array of JSON values.
	void SetArray( const FString& Key, const TArray<FJsonLibraryValue>& Value );
	// Set a property as a map of JSON values.
	void SetMap( const FString& Key, const TMap<FString, FJsonLibraryValue>& Value );

protected:
	
	TSharedPtr<FJsonValueObject> JsonObject;

	const TSharedPtr<FJsonObject> GetJsonObject() const;
	TSharedPtr<FJsonObject> SetJsonObject();

	bool TryParse( const FString& Text, bool bStripComments = false, bool bStripTrailingCommas = false );
	bool TryStringify( FString& Text, bool bCondensed = true ) const;

private:

	FJsonLibraryObjectNotify OnNotify;

	bool bNotifyHasKey;
	TSharedPtr<FJsonValue> NotifyValue;

	void NotifyAddOrChange( const FString& Key, const FJsonLibraryValue& Value );
	bool NotifyCheck();
	bool NotifyCheck( const FString& Key );
	void NotifyClear();
	void NotifyParse();
	void NotifyRemove( const FString& Key );

public:

	// Check if this object is valid.
	bool IsValid() const;
	// Check if this object is empty.
	bool IsEmpty() const;

	// Check if this object is a linear color.
	bool IsLinearColor() const;

	// Check if this object is a rotator.
	bool IsRotator() const;
	// Check if this object is a transform.
	bool IsTransform() const;
	// Check if this object is a vector.
	bool IsVector() const;
	
	// Parse a JSON string.
	static FJsonLibraryObject Parse( const FString& Text );
	// Parse a JSON string.
	static FJsonLibraryObject Parse( const FString& Text, const FJsonLibraryObjectNotify& Notify );
	
	// Parse a relaxed JSON string.
	static FJsonLibraryObject ParseRelaxed( const FString& Text, bool bStripComments = true, bool bStripTrailingCommas = true );

	// Stringify this object as a JSON string.
	FString Stringify( bool bCondensed = true ) const;

protected:
	
	bool ToStruct( const UStruct* StructType, void* StructPtr ) const;

public:

	// Convert this object to structure memory.
	TSharedPtr<FStructOnScope> ToStruct( const UStruct* StructType ) const;

	// Convert this object to a structure.
	template<typename StructType>
	StructType ToStruct() const
	{
		StructType Struct;
		if ( ToStruct( StructType::StaticStruct(), &Struct ) )
			return Struct;
		
		return StructType();
	}

	// Convert this object to a linear color.
	FLinearColor ToLinearColor() const;

	// Convert this object to a rotator.
	FRotator ToRotator() const;
	// Convert this object to a transform.
	FTransform ToTransform() const;
	// Convert this object to a vector.
	FVector ToVector() const;

	// Copy a JSON object to a map of JSON values.
	TMap<FString, FJsonLibraryValue> ToMap() const;

	// Copy a JSON object to a map of booleans.
	TMap<FString, bool> ToBooleanMap() const;
	// Copy a JSON object to a map of floats.
	TMap<FString, float> ToFloatMap() const;
	// Copy a JSON object to a map of integers.
	TMap<FString, int32> ToIntegerMap() const;
	// Copy a JSON object to a map of numbers.
	TMap<FString, double> ToNumberMap() const;
	// Copy a JSON object to a map of strings.
	TMap<FString, FString> ToStringMap() const;

	// Copy a JSON object to a map of date/times.
	TMap<FString, FDateTime> ToDateTimeMap() const;
	// Copy a JSON object to a map of GUIDs.
	TMap<FString, FGuid> ToGuidMap() const;

	// Copy a JSON object to a map of colors.
	TMap<FString, FColor> ToColorMap() const;
	// Copy a JSON object to a map of linear colors.
	TMap<FString, FLinearColor> ToLinearColorMap() const;

	// Copy a JSON object to a map of rotators.
	TMap<FString, FRotator> ToRotatorMap() const;
	// Copy a JSON object to a map of transforms.
	TMap<FString, FTransform> ToTransformMap() const;
	// Copy a JSON object to a map of vectors.
	TMap<FString, FVector> ToVectorMap() const;

	bool operator==( const FJsonLibraryObject& Object ) const;
	bool operator!=( const FJsonLibraryObject& Object ) const;

	bool operator==( const FJsonLibraryValue& Value ) const;
	bool operator!=( const FJsonLibraryValue& Value ) const;
};
