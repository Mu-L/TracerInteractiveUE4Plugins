// Copyright 2020 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JsonLibraryValue.h"
#include "JsonLibraryObject.h"
#include "JsonLibraryList.h"
#include "JsonLibraryHelpers.generated.h"

UCLASS()
class JSONLIBRARY_API UJsonLibraryHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

#if UE_EDITOR
protected:
#else
public:
#endif

	// Parse a JSON string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Parse"), Category = "JSON Library")
	static FJsonLibraryValue Parse( const FString& Text );
	
	// Parse a JSON object string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Parse Object", AutoCreateRefTerm = "Notify", AdvancedDisplay = "Notify"), Category = "JSON Library|Object")
	static FJsonLibraryObject ParseObject( const FString& Text, const FJsonLibraryObjectNotify& Notify );
	// Parse a JSON array string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Parse List", AutoCreateRefTerm = "Notify", AdvancedDisplay = "Notify"), Category = "JSON Library|List")
	static FJsonLibraryList ParseList( const FString& Text, const FJsonLibraryListNotify& Notify );

	// Construct a JSON null.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Construct null", CompactNodeTitle = "null"), Category = "JSON Library")
	static FJsonLibraryValue ConstructNull();

	// Construct a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct Object", CompactNodeTitle = "JSON", AutoCreateRefTerm = "Notify"), Category = "JSON Library")
	static FJsonLibraryObject ConstructObject( const FJsonLibraryObjectNotify& Notify );
	// Construct a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct List", CompactNodeTitle = "JSON", AutoCreateRefTerm = "Notify"), Category = "JSON Library")
	static FJsonLibraryList ConstructList( const FJsonLibraryListNotify& Notify );

	// Construct an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct Array", CompactNodeTitle = "ARRAY"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryValue> ConstructArray();
	// Construct a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct Map", CompactNodeTitle = "MAP"), Category = "JSON Library|Map")
	static TMap<FString, FJsonLibraryValue> ConstructMap();

	// Convert a boolean to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Boolean", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromBoolean( bool Value );
	// Convert a float to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Float", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromFloat( float Value );
	// Convert an integer to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Integer", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromInteger( int32 Value );
	// Convert a string to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From String", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromString( const FString& Value );
	
	// Convert a rotator to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Rotator", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryValue FromRotator( const FRotator& Value );
	// Convert a transform to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Transform", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryValue FromTransform( const FTransform& Value );
	// Convert a vector to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Vector", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryValue FromVector( const FVector& Value );

	// Convert a JSON object to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From Object", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromObject( UPARAM(ref) const FJsonLibraryObject& Value );
	// Convert a JSON array to a JSON value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert From List", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryValue FromList( UPARAM(ref) const FJsonLibraryList& Value );

	// Copy an array of JSON values to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromArray( const TArray<FJsonLibraryValue>& Value );
	// Copy a map of JSON values to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Map"), Category = "JSON Library|Map")
	static FJsonLibraryValue FromMap( const TMap<FString, FJsonLibraryValue>& Value );

	// Copy an array of booleans to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Boolean Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromBooleanArray( const TArray<bool>& Value );
	// Copy an array of floats to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Float Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromFloatArray( const TArray<float>& Value );
	// Copy an array of integers to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Integer Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromIntegerArray( const TArray<int32>& Value );
	// Copy an array of strings to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From String Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromStringArray( const TArray<FString>& Value );

	// Copy an array of rotators to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Rotator Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryValue FromRotatorArray( const TArray<FRotator>& Value );
	// Copy an array of transforms to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Transform Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryValue FromTransformArray( const TArray<FTransform>& Value );
	// Copy an array of vectors to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Vector Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryValue FromVectorArray( const TArray<FVector>& Value );

	// Copy an array of JSON objects to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Object Array"), Category = "JSON Library|Array")
	static FJsonLibraryValue FromObjectArray( const TArray<FJsonLibraryObject>& Value );

	// Copy a map of booleans to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Boolean Map"), Category = "JSON Library|Map")
	static FJsonLibraryValue FromBooleanMap( const TMap<FString, bool>& Value );
	// Copy a map of floats to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Float Map"), Category = "JSON Library|Map")
	static FJsonLibraryValue FromFloatMap( const TMap<FString, float>& Value );
	// Copy a map of integers to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Integer Map"), Category = "JSON Library|Map")
	static FJsonLibraryValue FromIntegerMap( const TMap<FString, int32>& Value );
	// Copy a map of strings to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From String Map"), Category = "JSON Library|Map")
	static FJsonLibraryValue FromStringMap( const TMap<FString, FString>& Value );

	// Copy a map of rotators to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Rotator Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryValue FromRotatorMap( const TMap<FString, FRotator>& Value );
	// Copy a map of transforms to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Transform Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryValue FromTransformMap( const TMap<FString, FTransform>& Value );
	// Copy a map of vectors to a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy From Vector Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryValue FromVectorMap( const TMap<FString, FVector>& Value );

	// Convert a JSON value to a boolean.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Boolean", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static bool ToBoolean( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to a float.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Float", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static float ToFloat( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to an integer.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Integer", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static int32 ToInteger( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to a string.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To String", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FString ToString( UPARAM(ref) const FJsonLibraryValue& Value );

	// Convert a JSON value to a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Rotator", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FRotator ToRotator( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Transform", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FTransform ToTransform( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Vector", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FVector ToVector( UPARAM(ref) const FJsonLibraryValue& Value );

	// Convert a JSON value to a JSON object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To Object", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryObject ToObject( UPARAM(ref) const FJsonLibraryValue& Value );
	// Convert a JSON value to a JSON array.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert To List", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library")
	static FJsonLibraryList ToList( UPARAM(ref) const FJsonLibraryValue& Value );

	// Copy this value to an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Array"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryValue> ToArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Map"), Category = "JSON Library|Map")
	static TMap<FString, FJsonLibraryValue> ToMap( UPARAM(ref) const FJsonLibraryValue& Target );

	// Copy this value to an array of booleans.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Boolean Array"), Category = "JSON Library|Array")
	static TArray<bool> ToBooleanArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to an array of floats.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Float Array"), Category = "JSON Library|Array")
	static TArray<float> ToFloatArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to an array of integers.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Integer Array"), Category = "JSON Library|Array")
	static TArray<int32> ToIntegerArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to an array of strings.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To String Array"), Category = "JSON Library|Array")
	static TArray<FString> ToStringArray( UPARAM(ref) const FJsonLibraryValue& Target );

	// Copy this value to an array of rotators.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Rotator Array"), Category = "JSON Library|Array|Engine")
	static TArray<FRotator> ToRotatorArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to an array of transforms.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Transform Array"), Category = "JSON Library|Array|Engine")
	static TArray<FTransform> ToTransformArray( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to an array of vectors.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Vector Array"), Category = "JSON Library|Array|Engine")
	static TArray<FVector> ToVectorArray( UPARAM(ref) const FJsonLibraryValue& Target );

	// Copy this value to an array of JSON objects.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Object Array"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryObject> ToObjectArray( UPARAM(ref) const FJsonLibraryValue& Target );

	// Copy this value to a map of booleans.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Boolean Map"), Category = "JSON Library|Map")
	static TMap<FString, bool> ToBooleanMap( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of floats.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Float Map"), Category = "JSON Library|Map")
	static TMap<FString, float> ToFloatMap( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of integers.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Integer Map"), Category = "JSON Library|Map")
	static TMap<FString, int32> ToIntegerMap( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of strings.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To String Map"), Category = "JSON Library|Map")
	static TMap<FString, FString> ToStringMap( UPARAM(ref) const FJsonLibraryValue& Target );

	// Copy this value to a map of rotators.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Rotator Map"), Category = "JSON Library|Map|Engine")
	static TMap<FString, FRotator> ToRotatorMap( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of transforms.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Transform Map"), Category = "JSON Library|Map|Engine")
	static TMap<FString, FTransform> ToTransformMap( UPARAM(ref) const FJsonLibraryValue& Target );
	// Copy this value to a map of vectors.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy To Vector Map"), Category = "JSON Library|Map|Engine")
	static TMap<FString, FVector> ToVectorMap( UPARAM(ref) const FJsonLibraryValue& Target );

	// Convert a rotator to a JSON object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Rotator To Object", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryObject ConvertRotatorToObject( const FRotator& Value );
	// Convert a transform to a JSON object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Transform To Object", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryObject ConvertTransformToObject( const FTransform& Value );
	// Convert a vector to a JSON object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Vector To Object", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FJsonLibraryObject ConvertVectorToObject( const FVector& Value );

	// Convert a JSON object to a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Object To Rotator", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FRotator ConvertObjectToRotator( UPARAM(ref) const FJsonLibraryObject& Object );
	// Convert a JSON object to a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Object To Transform", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FTransform ConvertObjectToTransform( UPARAM(ref) const FJsonLibraryObject& Object );
	// Convert a JSON object to a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Convert Object To Vector", CompactNodeTitle = "->", BlueprintAutocast), Category = "JSON Library|Engine")
	static FVector ConvertObjectToVector( UPARAM(ref) const FJsonLibraryObject& Object );

	// Copy a map of JSON values to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Map To Object"), Category = "JSON Library|Map")
	static FJsonLibraryObject ConvertMapToObject( const TMap<FString, FJsonLibraryValue>& Value );
	// Copy a JSON object to a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Object To Map"), Category = "JSON Library|Map")
	static TMap<FString, FJsonLibraryValue> ConvertObjectToMap( UPARAM(ref) const FJsonLibraryObject& Object );

	// Copy a map of booleans to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Boolean Map To Object"), Category = "JSON Library|Map")
	static FJsonLibraryObject ConvertBooleanMapToObject( const TMap<FString, bool>& Value );
	// Copy a map of floats to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Float Map To Object"), Category = "JSON Library|Map")
	static FJsonLibraryObject ConvertFloatMapToObject( const TMap<FString, float>& Value );
	// Copy a map of integers to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Integer Map To Object"), Category = "JSON Library|Map")
	static FJsonLibraryObject ConvertIntegerMapToObject( const TMap<FString, int32>& Value );
	// Copy a map of strings to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy String Map To Object"), Category = "JSON Library|Map")
	static FJsonLibraryObject ConvertStringMapToObject( const TMap<FString, FString>& Value );

	// Copy a map of rotators to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Rotator Map To Object"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject ConvertRotatorMapToObject( const TMap<FString, FRotator>& Value );
	// Copy a map of transforms to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Transform Map To Object"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject ConvertTransformMapToObject( const TMap<FString, FTransform>& Value );
	// Copy a map of vectors to a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Vector Map To Object"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject ConvertVectorMapToObject( const TMap<FString, FVector>& Value );

	// Copy an array of JSON values to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertArrayToList( const TArray<FJsonLibraryValue>& Value );
	// Copy a JSON array to an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy List To Array"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryValue> ConvertListToArray( UPARAM(ref) const FJsonLibraryList& List );

	// Copy an array of booleans to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Boolean Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertBooleanArrayToList( const TArray<bool>& Value );
	// Copy an array of floats to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Float Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertFloatArrayToList( const TArray<float>& Value );
	// Copy an array of integers to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Integer Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertIntegerArrayToList( const TArray<int32>& Value );
	// Copy an array of strings to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy String Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertStringArrayToList( const TArray<FString>& Value );

	// Copy an array of rotators to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Rotator Array To List"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList ConvertRotatorArrayToList( const TArray<FRotator>& Value );
	// Copy an array of transforms to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Transform Array To List"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList ConvertTransformArrayToList( const TArray<FTransform>& Value );
	// Copy an array of vectors to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Vector Array To List"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList ConvertVectorArrayToList( const TArray<FVector>& Value );

	// Copy an array of JSON objects to a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Object Array To List"), Category = "JSON Library|Array")
	static FJsonLibraryList ConvertObjectArrayToList( const TArray<FJsonLibraryObject>& Value );


	// Get the JSON type of this value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Type"), Category = "JSON Library|Value")
	static EJsonLibraryType JsonValue_GetType( UPARAM(ref) const FJsonLibraryValue& Target );
	// Check if this value equals another value.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equals"), Category = "JSON Library|Value")
	static bool JsonValue_Equals( UPARAM(ref) const FJsonLibraryValue& Target, const FJsonLibraryValue& Value );
	// Check if this value is valid.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Valid"), Category = "JSON Library|Value")
	static bool JsonValue_IsValid( UPARAM(ref) const FJsonLibraryValue& Target );

	// Check if this value is a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Rotator"), Category = "JSON Library|Value|Engine")
	static bool JsonValue_IsRotator( UPARAM(ref) const FJsonLibraryValue& Target );
	// Check if this value is a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Transform"), Category = "JSON Library|Value|Engine")
	static bool JsonValue_IsTransform( UPARAM(ref) const FJsonLibraryValue& Target );
	// Check if this value is a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Vector"), Category = "JSON Library|Value|Engine")
	static bool JsonValue_IsVector( UPARAM(ref) const FJsonLibraryValue& Target );

	// Stringify this value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Stringify"), Category = "JSON Library|Value")
	static FString JsonValue_Stringify( UPARAM(ref) const FJsonLibraryValue& Target );


	// Check if this object equals another object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equals"), Category = "JSON Library|Object")
	static bool JsonObject_Equals( UPARAM(ref) const FJsonLibraryObject& Target, const FJsonLibraryObject& Object );

	// Get the number of properties in this object.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Count"), Category = "JSON Library|Object")
	static int32 JsonObject_Count( UPARAM(ref) const FJsonLibraryObject& Target );
	// Clear the properties in this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Clear"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_Clear( UPARAM(ref) FJsonLibraryObject& Target );

	// Check if this object has a property.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Has Property"), Category = "JSON Library|Object")
	static bool JsonObject_HasKey( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Remove a property from this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Property"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_RemoveKey( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key );

	// Add a JSON object to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_Add( UPARAM(ref) FJsonLibraryObject& Target, const FJsonLibraryObject& Object );

	// Add a map of booleans to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Boolean Map"), Category = "JSON Library|Map")
	static FJsonLibraryObject& JsonObject_AddBooleanMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, bool>& Map );
	// Add a map of floats to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Float Map"), Category = "JSON Library|Map")
	static FJsonLibraryObject& JsonObject_AddFloatMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, float>& Map );
	// Add a map of integers to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Integer Map"), Category = "JSON Library|Map")
	static FJsonLibraryObject& JsonObject_AddIntegerMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, int32>& Map );
	// Add a map of strings to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add String Map"), Category = "JSON Library|Map")
	static FJsonLibraryObject& JsonObject_AddStringMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, FString>& Map );

	// Add a map of rotators to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Rotator Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject& JsonObject_AddRotatorMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, FRotator>& Map );
	// Add a map of transforms to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Transform Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject& JsonObject_AddTransformMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, FTransform>& Map );
	// Add a map of vectors to this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Vector Map"), Category = "JSON Library|Map|Engine")
	static FJsonLibraryObject& JsonObject_AddVectorMap( UPARAM(ref) FJsonLibraryObject& Target, const TMap<FString, FVector>& Map );

	// Get the keys of this object as an array of strings.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Keys"), Category = "JSON Library|Object")
	static TArray<FString> JsonObject_GetKeys( UPARAM(ref) const FJsonLibraryObject& Target );
	// Get the values of this object as an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Values"), Category = "JSON Library|Object")
	static TArray<FJsonLibraryValue> JsonObject_GetValues( UPARAM(ref) const FJsonLibraryObject& Target );

	// Get a property as a boolean.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Boolean"), Category = "JSON Library|Object")
	static bool JsonObject_GetBoolean( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a number.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Float"), Category = "JSON Library|Object")
	static float JsonObject_GetFloat( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as an integer.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Integer"), Category = "JSON Library|Object")
	static int32 JsonObject_GetInteger( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a string.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get String"), Category = "JSON Library|Object")
	static FString JsonObject_GetString( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );

	// Get a property as a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Rotator"), Category = "JSON Library|Object|Engine")
	static FRotator JsonObject_GetRotator( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Transform"), Category = "JSON Library|Object|Engine")
	static FTransform JsonObject_GetTransform( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Vector"), Category = "JSON Library|Object|Engine")
	static FVector JsonObject_GetVector( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );

	// Get a property as a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Value"), Category = "JSON Library|Object")
	static FJsonLibraryValue JsonObject_GetValue( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Object"), Category = "JSON Library|Object")
	static FJsonLibraryObject JsonObject_GetObject( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get List"), Category = "JSON Library|Object")
	static FJsonLibraryList JsonObject_GetList( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );

	// Get a property as an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Property To Array"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryValue> JsonObject_GetArray( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );
	// Get a property as a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Property To Map"), Category = "JSON Library|Map")
	static TMap<FString, FJsonLibraryValue> JsonObject_GetMap( UPARAM(ref) const FJsonLibraryObject& Target, const FString& Key );

	// Set a property as a boolean.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Boolean"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetBoolean( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, bool Value );
	// Set a property as a float.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Float"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetFloat( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, float Value );
	// Set a property as an integer.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Integer"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetInteger( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, int32 Value );
	// Set a property as a string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set String"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetString( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FString& Value );

	// Set a property as a rotator.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Rotator"), Category = "JSON Library|Object|Engine")
	static FJsonLibraryObject& JsonObject_SetRotator( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FRotator& Value );
	// Set a property as a transform.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Transform"), Category = "JSON Library|Object|Engine")
	static FJsonLibraryObject& JsonObject_SetTransform( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FTransform& Value );
	// Set a property as a vector.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Vector"), Category = "JSON Library|Object|Engine")
	static FJsonLibraryObject& JsonObject_SetVector( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FVector& Value );

	// Set a property as a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Value"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetValue( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryValue& Value );
	// Set a property as a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Object"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetObject( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryObject& Value );
	// Set a property as a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set List"), Category = "JSON Library|Object")
	static FJsonLibraryObject& JsonObject_SetList( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryList& Value );

	// Set a property as an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Property From Array"), Category = "JSON Library|Array")
	static FJsonLibraryObject& JsonObject_SetArray( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const TArray<FJsonLibraryValue>& Value );
	// Set a property as a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Property From Map"), Category = "JSON Library|Map")
	static FJsonLibraryObject& JsonObject_SetMap( UPARAM(ref) FJsonLibraryObject& Target, const FString& Key, const TMap<FString, FJsonLibraryValue>& Value );
	
	// Check if this object is valid.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Valid"), Category = "JSON Library|Object")
	static bool JsonObject_IsValid( UPARAM(ref) const FJsonLibraryObject& Target );
	// Check if this object is empty.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Empty"), Category = "JSON Library|Object")
	static bool JsonObject_IsEmpty( UPARAM(ref) const FJsonLibraryObject& Target );

	// Check if this object is a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Rotator"), Category = "JSON Library|Object|Engine")
	static bool JsonObject_IsRotator( UPARAM(ref) const FJsonLibraryObject& Target );
	// Check if this object is a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Transform"), Category = "JSON Library|Object|Engine")
	static bool JsonObject_IsTransform( UPARAM(ref) const FJsonLibraryObject& Target );
	// Check if this object is a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Vector"), Category = "JSON Library|Object|Engine")
	static bool JsonObject_IsVector( UPARAM(ref) const FJsonLibraryObject& Target );

	// Stringify this object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Stringify"), Category = "JSON Library|Object")
	static FString JsonObject_Stringify( UPARAM(ref) const FJsonLibraryObject& Target );


	// Check if this list equals another list.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equals"), Category = "JSON Library|List")
	static bool JsonList_Equals( UPARAM(ref) const FJsonLibraryList& Target, const FJsonLibraryList& List );

	// Get the number of items in this list.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Count"), Category = "JSON Library|List")
	static int32 JsonList_Count( UPARAM(ref) const FJsonLibraryList& Target );
	// Clear the items in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Clear"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_Clear( UPARAM(ref) FJsonLibraryList& Target );
	// Swap two items in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Swap"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_Swap( UPARAM(ref) FJsonLibraryList& Target, int32 IndexA, int32 IndexB );

	// Append a JSON array to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_Append( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryList& List );

	// Append an array of booleans to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Boolean Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AppendBooleanArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<bool>& Array );
	// Append an array of floats to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Float Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AppendFloatArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<float>& Array );
	// Append an array of integers to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Integer Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AppendIntegerArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<int32>& Array );
	// Append an array of strings to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append String Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AppendStringArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FString>& Array );

	// Append an array of rotators to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Rotator Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_AppendRotatorArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FRotator>& Array );
	// Append an array of transforms to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Transform Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_AppendTransformArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FTransform>& Array );
	// Append an array of vectors to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Vector Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_AppendVectorArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FVector>& Array );

	// Append an array of JSON objects to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Append Object Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AppendObjectArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FJsonLibraryObject>& Array );

	// Inject the items of a JSON array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_Inject( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& List );

	// Inject the items of a boolean array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Boolean Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InjectBooleanArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<bool>& Array );
	// Inject the items of a float array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Float Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InjectFloatArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<float>& Array );
	// Inject the items of an integer array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Integer Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InjectIntegerArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<int32>& Array );
	// Inject the items of a string array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject String Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InjectStringArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FString>& Array );

	// Inject the items of a rotator array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Rotator Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_InjectRotatorArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FRotator>& Array );
	// Inject the items of a transform array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Transform Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_InjectTransformArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FTransform>& Array );
	// Inject the items of a vector array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Vector Array"), Category = "JSON Library|Array|Engine")
	static FJsonLibraryList& JsonList_InjectVectorArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FVector>& Array );

	// Inject the items of an array of JSON objects into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Inject Object Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InjectObjectArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryObject>& Array );
	
	// Add a boolean to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Boolean"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddBoolean( UPARAM(ref) FJsonLibraryList& Target, bool Value );
	// Add a number to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Float"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddFloat( UPARAM(ref) FJsonLibraryList& Target, float Value );
	// Add an integer to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Integer"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddInteger( UPARAM(ref) FJsonLibraryList& Target, int32 Value );
	// Add a string to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add String"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddString( UPARAM(ref) FJsonLibraryList& Target, const FString& Value );
	
	// Add a rotator to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Rotator"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_AddRotator( UPARAM(ref) FJsonLibraryList& Target, const FRotator& Value );
	// Add a transform to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Transform"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_AddTransform( UPARAM(ref) FJsonLibraryList& Target, const FTransform& Value );
	// Add a vector to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Vector"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_AddVector( UPARAM(ref) FJsonLibraryList& Target, const FVector& Value );

	// Add a JSON value to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Value"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddValue( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryValue& Value );
	// Add a JSON object to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Object"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddObject( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryObject& Value );
	// Add a JSON array to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add List"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_AddList( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryList& Value );

	// Add an array of JSON values to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Item From Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_AddArray( UPARAM(ref) FJsonLibraryList& Target, const TArray<FJsonLibraryValue>& Value );
	// Add a map of JSON values to this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Item From Map"), Category = "JSON Library|Map")
	static FJsonLibraryList& JsonList_AddMap( UPARAM(ref) FJsonLibraryList& Target, const TMap<FString, FJsonLibraryValue>& Value );

	// Insert a boolean into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Boolean"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertBoolean( UPARAM(ref) FJsonLibraryList& Target, int32 Index, bool Value );
	// Insert a number into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Float"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertFloat( UPARAM(ref) FJsonLibraryList& Target, int32 Index, float Value );
	// Insert an integer into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Integer"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertInteger( UPARAM(ref) FJsonLibraryList& Target, int32 Index, int32 Value );
	// Insert a string into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert String"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertString( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FString& Value );

	// Insert a rotator into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Rotator"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_InsertRotator( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FRotator& Value );
	// Insert a transform into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Transform"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_InsertTransform( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FTransform& Value );
	// Insert a vector into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Vector"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_InsertVector( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FVector& Value );

	// Insert a JSON value into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Value"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertValue( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryValue& Value );
	// Insert a JSON object into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Object"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertObject( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryObject& Value );
	// Insert a JSON array into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert List"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_InsertList( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& Value );

	// Insert an array of JSON values into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Item From Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_InsertArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryValue>& Value );
	// Insert a map of JSON values into this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Insert Item From Map"), Category = "JSON Library|Map")
	static FJsonLibraryList& JsonList_InsertMap( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TMap<FString, FJsonLibraryValue>& Value );

	// Get an item as a boolean.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Boolean"), Category = "JSON Library|List")
	static bool JsonList_GetBoolean( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a number.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Float"), Category = "JSON Library|List")
	static float JsonList_GetFloat( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as an integer.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Integer"), Category = "JSON Library|List")
	static int32 JsonList_GetInteger( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a string.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get String"), Category = "JSON Library|List")
	static FString JsonList_GetString( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );

	// Get an item as a rotator.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Rotator"), Category = "JSON Library|List|Engine")
	static FRotator JsonList_GetRotator( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a transform.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Transform"), Category = "JSON Library|List|Engine")
	static FTransform JsonList_GetTransform( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a vector.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Vector"), Category = "JSON Library|List|Engine")
	static FVector JsonList_GetVector( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );

	// Get an item as a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Value"), Category = "JSON Library|List")
	static FJsonLibraryValue JsonList_GetValue( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Object"), Category = "JSON Library|List")
	static FJsonLibraryObject JsonList_GetObject( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Get an item as a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get List"), Category = "JSON Library|List")
	static FJsonLibraryList JsonList_GetList( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );

	// Copy an item to an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Item To Array"), Category = "JSON Library|Array")
	static TArray<FJsonLibraryValue> JsonList_GetArray( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );
	// Copy an item to a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Copy Item To Map"), Category = "JSON Library|Map")
	static TMap<FString, FJsonLibraryValue> JsonList_GetMap( UPARAM(ref) const FJsonLibraryList& Target, int32 Index );

	// Set an item as a boolean.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Boolean"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetBoolean( UPARAM(ref) FJsonLibraryList& Target, int32 Index, bool Value );
	// Set an item as a number.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Float"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetFloat( UPARAM(ref) FJsonLibraryList& Target, int32 Index, float Value );
	// Set an item as an integer.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Integer"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetInteger( UPARAM(ref) FJsonLibraryList& Target, int32 Index, int32 Value );
	// Set an item as a string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set String"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetString( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FString& Value );

	// Set an item as a rotator.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Rotator"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_SetRotator( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FRotator& Value );
	// Set an item as a transform.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Transform"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_SetTransform( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FTransform& Value );
	// Set an item as a vector.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Vector"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_SetVector( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FVector& Value );

	// Set an item as a JSON value.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Value"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetValue( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryValue& Value );
	// Set an item as a JSON object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Object"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetObject( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryObject& Value );
	// Set an item as a JSON array.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set List"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_SetList( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& Value );
	
	// Set an item as an array of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Item From Array"), Category = "JSON Library|Array")
	static FJsonLibraryList& JsonList_SetArray( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryValue>& Value );
	// Set an item as a map of JSON values.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Item From Map"), Category = "JSON Library|Map")
	static FJsonLibraryList& JsonList_SetMap( UPARAM(ref) FJsonLibraryList& Target, int32 Index, const TMap<FString, FJsonLibraryValue>& Value );

	// Remove an item from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_Remove( UPARAM(ref) FJsonLibraryList& Target, int32 Index );

	// Remove a boolean from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Boolean"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveBoolean( UPARAM(ref) FJsonLibraryList& Target, bool Value );
	// Remove a float from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Float"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveFloat( UPARAM(ref) FJsonLibraryList& Target, float Value );
	// Remove an integer from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Integer"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveInteger( UPARAM(ref) FJsonLibraryList& Target, int32 Value );
	// Remove a string from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove String"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveString( UPARAM(ref) FJsonLibraryList& Target, const FString& Value );

	// Remove a rotator from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Rotator"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_RemoveRotator( UPARAM(ref) FJsonLibraryList& Target, const FRotator& Value );
	// Remove a transform from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Transform"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_RemoveTransform( UPARAM(ref) FJsonLibraryList& Target, const FTransform& Value );
	// Remove a vector from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Vector"), Category = "JSON Library|List|Engine")
	static FJsonLibraryList& JsonList_RemoveVector( UPARAM(ref) FJsonLibraryList& Target, const FVector& Value );

	// Remove a JSON value from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Value"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveValue( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryValue& Value );
	// Remove a JSON object from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove Object"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveObject( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryObject& Value );
	// Remove a JSON array from this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove List"), Category = "JSON Library|List")
	static FJsonLibraryList& JsonList_RemoveList( UPARAM(ref) FJsonLibraryList& Target, const FJsonLibraryList& Value );

	// Find a boolean in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Boolean"), Category = "JSON Library|List")
	static int32 JsonList_FindBoolean( UPARAM(ref) const FJsonLibraryList& Target, bool Value, int32 Index = 0 );
	// Find a float in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Float"), Category = "JSON Library|List")
	static int32 JsonList_FindFloat( UPARAM(ref) const FJsonLibraryList& Target, float Value, int32 Index = 0 );
	// Find an integer in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Integer"), Category = "JSON Library|List")
	static int32 JsonList_FindInteger( UPARAM(ref) const FJsonLibraryList& Target, int32 Value, int32 Index = 0 );
	// Find a string in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find String"), Category = "JSON Library|List")
	static int32 JsonList_FindString( UPARAM(ref) const FJsonLibraryList& Target, const FString& Value, int32 Index = 0 );

	// Find a rotator in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Rotator"), Category = "JSON Library|List|Engine")
	static int32 JsonList_FindRotator( UPARAM(ref) const FJsonLibraryList& Target, const FRotator& Value, int32 Index = 0 );
	// Find a transform in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Transform"), Category = "JSON Library|List|Engine")
	static int32 JsonList_FindTransform( UPARAM(ref) const FJsonLibraryList& Target, const FTransform& Value, int32 Index = 0 );
	// Find a vector in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Vector"), Category = "JSON Library|List|Engine")
	static int32 JsonList_FindVector( UPARAM(ref) const FJsonLibraryList& Target, const FVector& Value, int32 Index = 0 );

	// Find a JSON value in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Value"), Category = "JSON Library|List")
	static int32 JsonList_FindValue( UPARAM(ref) const FJsonLibraryList& Target, const FJsonLibraryValue& Value, int32 Index = 0 );
	// Find a JSON object in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find Object"), Category = "JSON Library|List")
	static int32 JsonList_FindObject( UPARAM(ref) const FJsonLibraryList& Target, const FJsonLibraryObject& Value, int32 Index = 0 );
	// Find a JSON array in this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Find List"), Category = "JSON Library|List")
	static int32 JsonList_FindList( UPARAM(ref) const FJsonLibraryList& Target, const FJsonLibraryList& Value, int32 Index = 0 );

	// Check if this list is valid.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Valid"), Category = "JSON Library|List")
	static bool JsonList_IsValid( UPARAM(ref) const FJsonLibraryList& Target );
	// Check if this list is empty.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Empty"), Category = "JSON Library|List")
	static bool JsonList_IsEmpty( UPARAM(ref) const FJsonLibraryList& Target );
	
	// Stringify this list.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Stringify"), Category = "JSON Library|List")
	static FString JsonList_Stringify( UPARAM(ref) const FJsonLibraryList& Target );
};
