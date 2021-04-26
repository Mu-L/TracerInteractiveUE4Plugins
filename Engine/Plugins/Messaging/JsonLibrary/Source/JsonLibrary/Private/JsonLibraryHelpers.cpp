// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryHelpers.h"

FJsonLibraryValue UJsonLibraryHelpers::Parse( const FString& Text, bool bComments /*= false*/, bool bTrailingCommas /*= false*/ )
{
	if ( bComments || bTrailingCommas )
		return FJsonLibraryValue::ParseRelaxed( Text, bComments, bTrailingCommas );

	return FJsonLibraryValue::Parse( Text );
}

FJsonLibraryObject UJsonLibraryHelpers::ParseObject( const FString& Text, const FJsonLibraryObjectNotify& Notify )
{
	return FJsonLibraryObject::Parse( Text, Notify );
}

FJsonLibraryList UJsonLibraryHelpers::ParseList( const FString& Text, const FJsonLibraryListNotify& Notify )
{
	return FJsonLibraryList::Parse( Text, Notify );
}

FJsonLibraryValue UJsonLibraryHelpers::ConstructNull()
{
	return FJsonLibraryValue();
}

FJsonLibraryObject UJsonLibraryHelpers::ConstructObject( const FJsonLibraryObjectNotify& Notify )
{
	return FJsonLibraryObject( Notify );
}

FJsonLibraryList UJsonLibraryHelpers::ConstructList( const FJsonLibraryListNotify& Notify )
{
	return FJsonLibraryList( Notify );
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::ConstructArray()
{
	return TArray<FJsonLibraryValue>();
}

TMap<FString, FJsonLibraryValue> UJsonLibraryHelpers::ConstructMap()
{
	return TMap<FString, FJsonLibraryValue>();
}

FJsonLibraryValue UJsonLibraryHelpers::FromBoolean( bool Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromFloat( float Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromInteger( int32 Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromString( const FString& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromDateTime( const FDateTime& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromGuid( const FGuid& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromColor( const FColor& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromLinearColor( const FLinearColor& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromRotator( const FRotator& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromTransform( const FTransform& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromVector( const FVector& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromObject( const FJsonLibraryObject& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromList( const FJsonLibraryList& Value )
{
	return FJsonLibraryValue( Value );
}

FJsonLibraryValue UJsonLibraryHelpers::FromArray( const TArray<FJsonLibraryValue>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromMap( const TMap<FString, FJsonLibraryValue>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromBooleanArray( const TArray<bool>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromFloatArray( const TArray<float>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromIntegerArray( const TArray<int32>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromStringArray( const TArray<FString>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromDateTimeArray( const TArray<FDateTime>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromGuidArray( const TArray<FGuid>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromColorArray( const TArray<FColor>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromLinearColorArray( const TArray<FLinearColor>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromRotatorArray( const TArray<FRotator>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromTransformArray( const TArray<FTransform>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromVectorArray( const TArray<FVector>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromObjectArray( const TArray<FJsonLibraryObject>& Value )
{
	return FromList( FJsonLibraryList( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromBooleanMap( const TMap<FString, bool>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromFloatMap( const TMap<FString, float>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromIntegerMap( const TMap<FString, int32>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromStringMap( const TMap<FString, FString>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromDateTimeMap( const TMap<FString, FDateTime>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromGuidMap( const TMap<FString, FGuid>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromColorMap( const TMap<FString, FColor>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromLinearColorMap( const TMap<FString, FLinearColor>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromRotatorMap( const TMap<FString, FRotator>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromTransformMap( const TMap<FString, FTransform>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

FJsonLibraryValue UJsonLibraryHelpers::FromVectorMap( const TMap<FString, FVector>& Value )
{
	return FromObject( FJsonLibraryObject( Value ) );
}

bool UJsonLibraryHelpers::ToBoolean( const FJsonLibraryValue& Value )
{
	return Value.GetBoolean();
}

float UJsonLibraryHelpers::ToFloat( const FJsonLibraryValue& Value )
{
	return Value.GetFloat();
}

int32 UJsonLibraryHelpers::ToInteger( const FJsonLibraryValue& Value )
{
	return Value.GetInteger();
}

FString UJsonLibraryHelpers::ToString( const FJsonLibraryValue& Value )
{
	return Value.GetString();
}

FDateTime UJsonLibraryHelpers::ToDateTime( const FJsonLibraryValue& Value )
{
	return Value.GetDateTime();
}

FGuid UJsonLibraryHelpers::ToGuid( const FJsonLibraryValue& Value )
{
	return Value.GetGuid();
}

FColor UJsonLibraryHelpers::ToColor( const FJsonLibraryValue& Value )
{
	return Value.GetColor();
}

FLinearColor UJsonLibraryHelpers::ToLinearColor( const FJsonLibraryValue& Value )
{
	return Value.GetLinearColor();
}

FRotator UJsonLibraryHelpers::ToRotator( const FJsonLibraryValue& Value )
{
	return Value.GetRotator();
}

FTransform UJsonLibraryHelpers::ToTransform( const FJsonLibraryValue& Value )
{
	return Value.GetTransform();
}

FVector UJsonLibraryHelpers::ToVector( const FJsonLibraryValue& Value )
{
	return Value.GetVector();
}

FJsonLibraryObject UJsonLibraryHelpers::ToObject( const FJsonLibraryValue& Value )
{
	return Value.GetObject();
}

FJsonLibraryList UJsonLibraryHelpers::ToList( const FJsonLibraryValue& Value )
{
	return Value.GetList();
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::ToArray( const FJsonLibraryValue& Target )
{
	return Target.ToArray();
}

TMap<FString, FJsonLibraryValue> UJsonLibraryHelpers::ToMap( const FJsonLibraryValue& Target )
{
	return Target.ToMap();
}

TArray<bool> UJsonLibraryHelpers::ToBooleanArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToBooleanArray();
}

TArray<float> UJsonLibraryHelpers::ToFloatArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToFloatArray();
}

TArray<int32> UJsonLibraryHelpers::ToIntegerArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToIntegerArray();
}

TArray<FString> UJsonLibraryHelpers::ToStringArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToStringArray();
}

TArray<FDateTime> UJsonLibraryHelpers::ToDateTimeArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToDateTimeArray();
}

TArray<FGuid> UJsonLibraryHelpers::ToGuidArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToGuidArray();
}

TArray<FColor> UJsonLibraryHelpers::ToColorArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToColorArray();
}

TArray<FLinearColor> UJsonLibraryHelpers::ToLinearColorArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToLinearColorArray();
}

TArray<FRotator> UJsonLibraryHelpers::ToRotatorArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToRotatorArray();
}

TArray<FTransform> UJsonLibraryHelpers::ToTransformArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToTransformArray();
}

TArray<FVector> UJsonLibraryHelpers::ToVectorArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToVectorArray();
}

TArray<FJsonLibraryObject> UJsonLibraryHelpers::ToObjectArray( const FJsonLibraryValue& Target )
{
	return Target.GetList().ToObjectArray();
}

TMap<FString, bool> UJsonLibraryHelpers::ToBooleanMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToBooleanMap();
}

TMap<FString, float> UJsonLibraryHelpers::ToFloatMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToFloatMap();
}

TMap<FString, int32> UJsonLibraryHelpers::ToIntegerMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToIntegerMap();
}

TMap<FString, FString> UJsonLibraryHelpers::ToStringMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToStringMap();
}

TMap<FString, FDateTime> UJsonLibraryHelpers::ToDateTimeMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToDateTimeMap();
}

TMap<FString, FGuid> UJsonLibraryHelpers::ToGuidMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToGuidMap();
}

TMap<FString, FColor> UJsonLibraryHelpers::ToColorMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToColorMap();
}

TMap<FString, FLinearColor> UJsonLibraryHelpers::ToLinearColorMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToLinearColorMap();
}

TMap<FString, FRotator> UJsonLibraryHelpers::ToRotatorMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToRotatorMap();
}

TMap<FString, FTransform> UJsonLibraryHelpers::ToTransformMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToTransformMap();
}

TMap<FString, FVector> UJsonLibraryHelpers::ToVectorMap( const FJsonLibraryValue& Target )
{
	return Target.GetObject().ToVectorMap();
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertLinearColorToObject( const FLinearColor& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertRotatorToObject( const FRotator& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertTransformToObject( const FTransform& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertVectorToObject( const FVector& Value )
{
	return FJsonLibraryObject( Value );
}

FLinearColor UJsonLibraryHelpers::ConvertObjectToLinearColor( const FJsonLibraryObject& Object )
{
	return Object.ToLinearColor();
}

FRotator UJsonLibraryHelpers::ConvertObjectToRotator( const FJsonLibraryObject& Object )
{
	return Object.ToRotator();
}

FTransform UJsonLibraryHelpers::ConvertObjectToTransform( const FJsonLibraryObject& Object )
{
	return Object.ToTransform();
}

FVector UJsonLibraryHelpers::ConvertObjectToVector( const FJsonLibraryObject& Object )
{
	return Object.ToVector();
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertMapToObject( const TMap<FString, FJsonLibraryValue>& Value )
{
	return FJsonLibraryObject( Value );
}

TMap<FString, FJsonLibraryValue> UJsonLibraryHelpers::ConvertObjectToMap( const FJsonLibraryObject& Object )
{
	return Object.ToMap();
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertBooleanMapToObject( const TMap<FString, bool>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertFloatMapToObject( const TMap<FString, float>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertIntegerMapToObject( const TMap<FString, int32>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertStringMapToObject( const TMap<FString, FString>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertDateTimeMapToObject( const TMap<FString, FDateTime>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertGuidMapToObject( const TMap<FString, FGuid>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertColorMapToObject( const TMap<FString, FColor>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertLinearColorMapToObject( const TMap<FString, FLinearColor>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertRotatorMapToObject( const TMap<FString, FRotator>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertTransformMapToObject( const TMap<FString, FTransform>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryObject UJsonLibraryHelpers::ConvertVectorMapToObject( const TMap<FString, FVector>& Value )
{
	return FJsonLibraryObject( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertArrayToList( const TArray<FJsonLibraryValue>& Value )
{
	return FJsonLibraryList( Value );
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::ConvertListToArray( const FJsonLibraryList& List )
{
	return List.ToArray();
}

FJsonLibraryList UJsonLibraryHelpers::ConvertBooleanArrayToList( const TArray<bool>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertFloatArrayToList( const TArray<float>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertIntegerArrayToList( const TArray<int32>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertStringArrayToList( const TArray<FString>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertDateTimeArrayToList( const TArray<FDateTime>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertGuidArrayToList( const TArray<FGuid>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertColorArrayToList( const TArray<FColor>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertLinearColorArrayToList( const TArray<FLinearColor>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertRotatorArrayToList( const TArray<FRotator>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertTransformArrayToList( const TArray<FTransform>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertVectorArrayToList( const TArray<FVector>& Value )
{
	return FJsonLibraryList( Value );
}

FJsonLibraryList UJsonLibraryHelpers::ConvertObjectArrayToList( const TArray<FJsonLibraryObject>& Value )
{
	return FJsonLibraryList( Value );
}


EJsonLibraryType UJsonLibraryHelpers::JsonValue_GetType( const FJsonLibraryValue& Target )
{
	return Target.GetType();
}

bool UJsonLibraryHelpers::JsonValue_Equals( const FJsonLibraryValue& Target, const FJsonLibraryValue& Value )
{
	return Target.Equals( Value );
}

bool UJsonLibraryHelpers::JsonValue_IsValid( const FJsonLibraryValue& Target )
{
	return Target.IsValid();
}

bool UJsonLibraryHelpers::JsonValue_IsGuid( const FJsonLibraryValue& Target )
{
	return Target.IsGuid();
}

bool UJsonLibraryHelpers::JsonValue_IsRotator( const FJsonLibraryValue& Target )
{
	return Target.IsRotator();
}

bool UJsonLibraryHelpers::JsonValue_IsTransform( const FJsonLibraryValue& Target )
{
	return Target.IsTransform();
}

bool UJsonLibraryHelpers::JsonValue_IsVector( const FJsonLibraryValue& Target )
{
	return Target.IsVector();
}

FString UJsonLibraryHelpers::JsonValue_Stringify( const FJsonLibraryValue& Target, bool bCondensed /*= true*/ )
{
	return Target.Stringify( bCondensed );
}


bool UJsonLibraryHelpers::JsonObject_Equals( const FJsonLibraryObject& Target, const FJsonLibraryObject& Object )
{
	return Target.Equals( Object );
}

int32 UJsonLibraryHelpers::JsonObject_Count( const FJsonLibraryObject& Target )
{
	return Target.Count();
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_Clear( FJsonLibraryObject& Target )
{
	Target.Clear();
	return Target;
}

bool UJsonLibraryHelpers::JsonObject_HasKey( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.HasKey( Key );
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_RemoveKey( FJsonLibraryObject& Target, const FString& Key )
{
	Target.RemoveKey( Key );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_Add( FJsonLibraryObject& Target, const FJsonLibraryObject& Object )
{
	Target.Add( Object );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddBooleanMap( FJsonLibraryObject& Target, const TMap<FString, bool>& Map )
{
	Target.AddBooleanMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddFloatMap( FJsonLibraryObject& Target, const TMap<FString, float>& Map )
{
	Target.AddFloatMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddIntegerMap( FJsonLibraryObject& Target, const TMap<FString, int32>& Map )
{
	Target.AddIntegerMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddStringMap( FJsonLibraryObject& Target, const TMap<FString, FString>& Map )
{
	Target.AddStringMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddDateTimeMap( FJsonLibraryObject& Target, const TMap<FString, FDateTime>& Map )
{
	Target.AddDateTimeMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddGuidMap( FJsonLibraryObject& Target, const TMap<FString, FGuid>& Map )
{
	Target.AddGuidMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddColorMap( FJsonLibraryObject& Target, const TMap<FString, FColor>& Map )
{
	Target.AddColorMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddLinearColorMap( FJsonLibraryObject& Target, const TMap<FString, FLinearColor>& Map )
{
	Target.AddLinearColorMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddRotatorMap( FJsonLibraryObject& Target, const TMap<FString, FRotator>& Map )
{
	Target.AddRotatorMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddTransformMap( FJsonLibraryObject& Target, const TMap<FString, FTransform>& Map )
{
	Target.AddTransformMap( Map );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_AddVectorMap( FJsonLibraryObject& Target, const TMap<FString, FVector>& Map )
{
	Target.AddVectorMap( Map );
	return Target;
}

TArray<FString> UJsonLibraryHelpers::JsonObject_GetKeys( const FJsonLibraryObject& Target )
{
	return Target.GetKeys();
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::JsonObject_GetValues( const FJsonLibraryObject& Target )
{
	return Target.GetValues();
}

bool UJsonLibraryHelpers::JsonObject_GetBoolean( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetBoolean( Key );
}

float UJsonLibraryHelpers::JsonObject_GetFloat( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetFloat( Key );
}

int32 UJsonLibraryHelpers::JsonObject_GetInteger( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetInteger( Key );
}

FString UJsonLibraryHelpers::JsonObject_GetString( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetString( Key );
}

FDateTime UJsonLibraryHelpers::JsonObject_GetDateTime( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetDateTime( Key );
}

FGuid UJsonLibraryHelpers::JsonObject_GetGuid( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetGuid( Key );
}

FColor UJsonLibraryHelpers::JsonObject_GetColor( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetColor( Key );
}

FLinearColor UJsonLibraryHelpers::JsonObject_GetLinearColor( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetLinearColor( Key );
}

FRotator UJsonLibraryHelpers::JsonObject_GetRotator( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetRotator( Key );
}

FTransform UJsonLibraryHelpers::JsonObject_GetTransform( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetTransform( Key );
}

FVector UJsonLibraryHelpers::JsonObject_GetVector( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetVector( Key );
}

FJsonLibraryValue UJsonLibraryHelpers::JsonObject_GetValue( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetValue( Key );
}

FJsonLibraryObject UJsonLibraryHelpers::JsonObject_GetObject( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetObject( Key );
}

FJsonLibraryList UJsonLibraryHelpers::JsonObject_GetList( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetList( Key );
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::JsonObject_GetArray( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetArray( Key );
}

TMap<FString, FJsonLibraryValue> UJsonLibraryHelpers::JsonObject_GetMap( const FJsonLibraryObject& Target, const FString& Key )
{
	return Target.GetMap( Key );
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetBoolean( FJsonLibraryObject& Target, const FString& Key, bool Value )
{
	Target.SetBoolean( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetFloat( FJsonLibraryObject& Target, const FString& Key, float Value )
{
	Target.SetFloat( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetInteger( FJsonLibraryObject& Target, const FString& Key, int32 Value )
{
	Target.SetInteger( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetString( FJsonLibraryObject& Target, const FString& Key, const FString& Value )
{
	Target.SetString( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetDateTime( FJsonLibraryObject& Target, const FString& Key, const FDateTime& Value )
{
	Target.SetDateTime( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetGuid( FJsonLibraryObject& Target, const FString& Key, const FGuid& Value )
{
	Target.SetGuid( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetColor( FJsonLibraryObject& Target, const FString& Key, const FColor& Value )
{
	Target.SetColor( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetLinearColor( FJsonLibraryObject& Target, const FString& Key, const FLinearColor& Value )
{
	Target.SetLinearColor( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetRotator( FJsonLibraryObject& Target, const FString& Key, const FRotator& Value )
{
	Target.SetRotator( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetTransform( FJsonLibraryObject& Target, const FString& Key, const FTransform& Value )
{
	Target.SetTransform( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetVector( FJsonLibraryObject& Target, const FString& Key, const FVector& Value )
{
	Target.SetVector( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetValue( FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryValue& Value )
{
	Target.SetValue( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetObject( FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryObject& Value )
{
	Target.SetObject( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetList( FJsonLibraryObject& Target, const FString& Key, const FJsonLibraryList& Value )
{
	Target.SetList( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetArray( FJsonLibraryObject& Target, const FString& Key, const TArray<FJsonLibraryValue>& Value )
{
	Target.SetArray( Key, Value );
	return Target;
}

FJsonLibraryObject& UJsonLibraryHelpers::JsonObject_SetMap( FJsonLibraryObject& Target, const FString& Key, const TMap<FString, FJsonLibraryValue>& Value )
{
	Target.SetMap( Key, Value );
	return Target;
}

bool UJsonLibraryHelpers::JsonObject_IsValid( const FJsonLibraryObject& Target )
{
	return Target.IsValid();
}

bool UJsonLibraryHelpers::JsonObject_IsEmpty( const FJsonLibraryObject& Target )
{
	return Target.IsEmpty();
}

bool UJsonLibraryHelpers::JsonObject_IsRotator( const FJsonLibraryObject& Target )
{
	return Target.IsRotator();
}

bool UJsonLibraryHelpers::JsonObject_IsTransform( const FJsonLibraryObject& Target )
{
	return Target.IsTransform();
}

bool UJsonLibraryHelpers::JsonObject_IsVector( const FJsonLibraryObject& Target )
{
	return Target.IsVector();
}

FString UJsonLibraryHelpers::JsonObject_Stringify( const FJsonLibraryObject& Target, bool bCondensed /*= true*/ )
{
	return Target.Stringify( bCondensed );
}


bool UJsonLibraryHelpers::JsonList_Equals( const FJsonLibraryList& Target, const FJsonLibraryList& List )
{
	return Target.Equals( List );
}

int32 UJsonLibraryHelpers::JsonList_Count( const FJsonLibraryList& Target )
{
	return Target.Count();
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_Clear( FJsonLibraryList& Target )
{
	Target.Clear();
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_Swap( FJsonLibraryList& Target, int32 IndexA, int32 IndexB )
{
	Target.Swap( IndexA, IndexB );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_Append( FJsonLibraryList& Target, const FJsonLibraryList& List )
{
	Target.Append( List );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendBooleanArray( FJsonLibraryList& Target, const TArray<bool>& Array )
{
	Target.AppendBooleanArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendFloatArray( FJsonLibraryList& Target, const TArray<float>& Array )
{
	Target.AppendFloatArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendIntegerArray( FJsonLibraryList& Target, const TArray<int32>& Array )
{
	Target.AppendIntegerArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendStringArray( FJsonLibraryList& Target, const TArray<FString>& Array )
{
	Target.AppendStringArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendDateTimeArray( FJsonLibraryList& Target, const TArray<FDateTime>& Array )
{
	Target.AppendDateTimeArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendGuidArray( FJsonLibraryList& Target, const TArray<FGuid>& Array )
{
	Target.AppendGuidArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendColorArray( FJsonLibraryList& Target, const TArray<FColor>& Array )
{
	Target.AppendColorArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendLinearColorArray( FJsonLibraryList& Target, const TArray<FLinearColor>& Array )
{
	Target.AppendLinearColorArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendRotatorArray( FJsonLibraryList& Target, const TArray<FRotator>& Array )
{
	Target.AppendRotatorArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendTransformArray( FJsonLibraryList& Target, const TArray<FTransform>& Array )
{
	Target.AppendTransformArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendVectorArray( FJsonLibraryList& Target, const TArray<FVector>& Array )
{
	Target.AppendVectorArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AppendObjectArray( FJsonLibraryList& Target, const TArray<FJsonLibraryObject>& Array )
{
	Target.AppendObjectArray( Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_Inject( FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& List )
{
	Target.Inject( Index, List );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectBooleanArray( FJsonLibraryList& Target, int32 Index, const TArray<bool>& Array )
{
	Target.InjectBooleanArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectFloatArray( FJsonLibraryList& Target, int32 Index, const TArray<float>& Array )
{
	Target.InjectFloatArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectIntegerArray( FJsonLibraryList& Target, int32 Index, const TArray<int32>& Array )
{
	Target.InjectIntegerArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectStringArray( FJsonLibraryList& Target, int32 Index, const TArray<FString>& Array )
{
	Target.InjectStringArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectDateTimeArray( FJsonLibraryList& Target, int32 Index, const TArray<FDateTime>& Array )
{
	Target.InjectDateTimeArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectGuidArray( FJsonLibraryList& Target, int32 Index, const TArray<FGuid>& Array )
{
	Target.InjectGuidArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectColorArray( FJsonLibraryList& Target, int32 Index, const TArray<FColor>& Array )
{
	Target.InjectColorArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectLinearColorArray( FJsonLibraryList& Target, int32 Index, const TArray<FLinearColor>& Array )
{
	Target.InjectLinearColorArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectRotatorArray( FJsonLibraryList& Target, int32 Index, const TArray<FRotator>& Array )
{
	Target.InjectRotatorArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectTransformArray( FJsonLibraryList& Target, int32 Index, const TArray<FTransform>& Array )
{
	Target.InjectTransformArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectVectorArray( FJsonLibraryList& Target, int32 Index, const TArray<FVector>& Array )
{
	Target.InjectVectorArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InjectObjectArray( FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryObject>& Array )
{
	Target.InjectObjectArray( Index, Array );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddBoolean( FJsonLibraryList& Target, bool Value )
{
	Target.AddBoolean( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddFloat( FJsonLibraryList& Target, float Value )
{
	Target.AddFloat( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddInteger( FJsonLibraryList& Target, int32 Value )
{
	Target.AddInteger( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddString( FJsonLibraryList& Target, const FString& Value )
{
	Target.AddString( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddDateTime( FJsonLibraryList& Target, const FDateTime& Value )
{
	Target.AddDateTime( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddGuid( FJsonLibraryList& Target, const FGuid& Value )
{
	Target.AddGuid( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddColor( FJsonLibraryList& Target, const FColor& Value )
{
	Target.AddColor( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddLinearColor( FJsonLibraryList& Target, const FLinearColor& Value )
{
	Target.AddLinearColor( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddRotator( FJsonLibraryList& Target, const FRotator& Value )
{
	Target.AddRotator( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddTransform( FJsonLibraryList& Target, const FTransform& Value )
{
	Target.AddTransform( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddVector( FJsonLibraryList& Target, const FVector& Value )
{
	Target.AddVector( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddValue( FJsonLibraryList& Target, const FJsonLibraryValue& Value )
{
	Target.AddValue( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddObject( FJsonLibraryList& Target, const FJsonLibraryObject& Value )
{
	Target.AddObject( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddList( FJsonLibraryList& Target, const FJsonLibraryList& Value )
{
	Target.AddList( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddArray( FJsonLibraryList& Target, const TArray<FJsonLibraryValue>& Value )
{
	Target.AddArray( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_AddMap( FJsonLibraryList& Target, const TMap<FString, FJsonLibraryValue>& Value )
{
	Target.AddMap( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertBoolean( FJsonLibraryList& Target, int32 Index, bool Value )
{
	Target.InsertBoolean( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertFloat( FJsonLibraryList& Target, int32 Index, float Value )
{
	Target.InsertFloat( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertInteger( FJsonLibraryList& Target, int32 Index, int32 Value )
{
	Target.InsertInteger( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertString( FJsonLibraryList& Target, int32 Index, const FString& Value )
{
	Target.InsertString( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertDateTime( FJsonLibraryList& Target, int32 Index, const FDateTime& Value )
{
	Target.InsertDateTime( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertGuid( FJsonLibraryList& Target, int32 Index, const FGuid& Value )
{
	Target.InsertGuid( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertColor( FJsonLibraryList& Target, int32 Index, const FColor& Value )
{
	Target.InsertColor( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertLinearColor( FJsonLibraryList& Target, int32 Index, const FLinearColor& Value )
{
	Target.InsertLinearColor( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertRotator( FJsonLibraryList& Target, int32 Index, const FRotator& Value )
{
	Target.InsertRotator( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertTransform( FJsonLibraryList& Target, int32 Index, const FTransform& Value )
{
	Target.InsertTransform( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertVector( FJsonLibraryList& Target, int32 Index, const FVector& Value )
{
	Target.InsertVector( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertValue( FJsonLibraryList& Target, int32 Index, const FJsonLibraryValue& Value )
{
	Target.InsertValue( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertObject( FJsonLibraryList& Target, int32 Index, const FJsonLibraryObject& Value )
{
	Target.InsertObject( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertList( FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& Value )
{
	Target.InsertList( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertArray( FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryValue>& Value )
{
	Target.InsertArray( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_InsertMap( FJsonLibraryList& Target, int32 Index, const TMap<FString, FJsonLibraryValue>& Value )
{
	Target.InsertMap( Index, Value );
	return Target;
}

bool UJsonLibraryHelpers::JsonList_GetBoolean( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetBoolean( Index );
}

float UJsonLibraryHelpers::JsonList_GetFloat( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetFloat( Index );
}

int32 UJsonLibraryHelpers::JsonList_GetInteger( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetInteger( Index );
}

FString UJsonLibraryHelpers::JsonList_GetString( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetString( Index );
}

FDateTime UJsonLibraryHelpers::JsonList_GetDateTime( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetDateTime( Index );
}

FGuid UJsonLibraryHelpers::JsonList_GetGuid( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetGuid( Index );
}

FColor UJsonLibraryHelpers::JsonList_GetColor( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetColor( Index );
}

FLinearColor UJsonLibraryHelpers::JsonList_GetLinearColor( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetLinearColor( Index );
}

FRotator UJsonLibraryHelpers::JsonList_GetRotator( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetRotator( Index );
}

FTransform UJsonLibraryHelpers::JsonList_GetTransform( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetTransform( Index );
}

FVector UJsonLibraryHelpers::JsonList_GetVector( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetVector( Index );
}

FJsonLibraryValue UJsonLibraryHelpers::JsonList_GetValue( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetValue( Index );
}

FJsonLibraryObject UJsonLibraryHelpers::JsonList_GetObject( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetObject( Index );
}

FJsonLibraryList UJsonLibraryHelpers::JsonList_GetList( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetList( Index );
}

TArray<FJsonLibraryValue> UJsonLibraryHelpers::JsonList_GetArray( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetArray( Index );
}

TMap<FString, FJsonLibraryValue> UJsonLibraryHelpers::JsonList_GetMap( const FJsonLibraryList& Target, int32 Index )
{
	return Target.GetMap( Index );
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetBoolean( FJsonLibraryList& Target, int32 Index, bool Value )
{
	Target.SetBoolean( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetFloat( FJsonLibraryList& Target, int32 Index, float Value )
{
	Target.SetFloat( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetInteger( FJsonLibraryList& Target, int32 Index, int32 Value )
{
	Target.SetInteger( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetString( FJsonLibraryList& Target, int32 Index, const FString& Value )
{
	Target.SetString( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetDateTime( FJsonLibraryList& Target, int32 Index, const FDateTime& Value )
{
	Target.SetDateTime( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetGuid( FJsonLibraryList& Target, int32 Index, const FGuid& Value )
{
	Target.SetGuid( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetColor( FJsonLibraryList& Target, int32 Index, const FColor& Value )
{
	Target.SetColor( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetLinearColor( FJsonLibraryList& Target, int32 Index, const FLinearColor& Value )
{
	Target.SetLinearColor( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetRotator( FJsonLibraryList& Target, int32 Index, const FRotator& Value )
{
	Target.SetRotator( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetTransform( FJsonLibraryList& Target, int32 Index, const FTransform& Value )
{
	Target.SetTransform( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetVector( FJsonLibraryList& Target, int32 Index, const FVector& Value )
{
	Target.SetVector( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetValue( FJsonLibraryList& Target, int32 Index, const FJsonLibraryValue& Value )
{
	Target.SetValue( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetObject( FJsonLibraryList& Target, int32 Index, const FJsonLibraryObject& Value )
{
	Target.SetObject( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetList( FJsonLibraryList& Target, int32 Index, const FJsonLibraryList& Value )
{
	Target.SetList( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetArray( FJsonLibraryList& Target, int32 Index, const TArray<FJsonLibraryValue>& Value )
{
	Target.SetArray( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_SetMap( FJsonLibraryList& Target, int32 Index, const TMap<FString, FJsonLibraryValue>& Value )
{
	Target.SetMap( Index, Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_Remove( FJsonLibraryList& Target, int32 Index )
{
	Target.Remove( Index );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveBoolean( FJsonLibraryList& Target, bool Value )
{
	Target.RemoveBoolean( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveFloat( FJsonLibraryList& Target, float Value )
{
	Target.RemoveFloat( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveInteger( FJsonLibraryList& Target, int32 Value )
{
	Target.RemoveInteger( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveString( FJsonLibraryList& Target, const FString& Value )
{
	Target.RemoveString( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveDateTime( FJsonLibraryList& Target, const FDateTime& Value )
{
	Target.RemoveDateTime( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveGuid( FJsonLibraryList& Target, const FGuid& Value )
{
	Target.RemoveGuid( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveColor( FJsonLibraryList& Target, const FColor& Value )
{
	Target.RemoveColor( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveLinearColor( FJsonLibraryList& Target, const FLinearColor& Value )
{
	Target.RemoveLinearColor( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveRotator( FJsonLibraryList& Target, const FRotator& Value )
{
	Target.RemoveRotator( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveTransform( FJsonLibraryList& Target, const FTransform& Value )
{
	Target.RemoveTransform( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveVector( FJsonLibraryList& Target, const FVector& Value )
{
	Target.RemoveVector( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveValue( FJsonLibraryList& Target, const FJsonLibraryValue& Value )
{
	Target.RemoveValue( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveObject( FJsonLibraryList& Target, const FJsonLibraryObject& Value )
{
	Target.RemoveObject( Value );
	return Target;
}

FJsonLibraryList& UJsonLibraryHelpers::JsonList_RemoveList( FJsonLibraryList& Target, const FJsonLibraryList& Value )
{
	Target.RemoveList( Value );
	return Target;
}

int32 UJsonLibraryHelpers::JsonList_FindBoolean( const FJsonLibraryList& Target, bool Value, int32 Index /*= 0*/ )
{
	return Target.FindBoolean( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindFloat( const FJsonLibraryList& Target, float Value, int32 Index /*= 0*/ )
{
	return Target.FindFloat( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindInteger( const FJsonLibraryList& Target, int32 Value, int32 Index /*= 0*/ )
{
	return Target.FindInteger( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindString( const FJsonLibraryList& Target, const FString& Value, int32 Index /*= 0*/ )
{
	return Target.FindString( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindDateTime( const FJsonLibraryList& Target, const FDateTime& Value, int32 Index /*= 0*/ )
{
	return Target.FindDateTime( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindGuid( const FJsonLibraryList& Target, const FGuid& Value, int32 Index /*= 0*/ )
{
	return Target.FindGuid( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindColor( const FJsonLibraryList& Target, const FColor& Value, int32 Index /*= 0*/ )
{
	return Target.FindColor( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindLinearColor( const FJsonLibraryList& Target, const FLinearColor& Value, int32 Index /*= 0*/ )
{
	return Target.FindLinearColor( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindRotator( const FJsonLibraryList& Target, const FRotator& Value, int32 Index /*= 0*/ )
{
	return Target.FindRotator( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindTransform( const FJsonLibraryList& Target, const FTransform& Value, int32 Index /*= 0*/ )
{
	return Target.FindTransform( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindVector( const FJsonLibraryList& Target, const FVector& Value, int32 Index /*= 0*/ )
{
	return Target.FindVector( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindValue( const FJsonLibraryList& Target, const FJsonLibraryValue& Value, int32 Index /*= 0*/ )
{
	return Target.FindValue( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindObject( const FJsonLibraryList& Target, const FJsonLibraryObject& Value, int32 Index /*= 0*/ )
{
	return Target.FindObject( Value, Index );
}

int32 UJsonLibraryHelpers::JsonList_FindList( const FJsonLibraryList& Target, const FJsonLibraryList& Value, int32 Index /*= 0*/ )
{
	return Target.FindList( Value, Index );
}

bool UJsonLibraryHelpers::JsonList_IsValid( const FJsonLibraryList& Target )
{
	return Target.IsValid();
}

bool UJsonLibraryHelpers::JsonList_IsEmpty( const FJsonLibraryList& Target )
{
	return Target.IsEmpty();
}

FString UJsonLibraryHelpers::JsonList_Stringify( const FJsonLibraryList& Target, bool bCondensed /*= true*/ )
{
	return Target.Stringify( bCondensed );
}

FString UJsonLibraryHelpers::StripCommentsOrCommas( const FString& Text, bool bComments /*= true*/, bool bTrailingCommas /*= true*/ )
{
	if ( !bComments && !bTrailingCommas )
		return Text;

	int32 BlockComment  = -1;
	int32 LineComment   = -1;
	int32 TrailingComma = -1;

	bool bStringLiteral   = false;
	bool bEscapeCharacter = false;

	int32 Length = Text.Len();
	FString StrippedText = Text;
	for ( int32 Index = 0; Index < Length; Index++ )
	{
		auto StrippedCharacter = StrippedText[ Index ];
		if ( BlockComment >= 0 )
		{
			if ( StrippedCharacter == '*' && Index + 1 < Length && StrippedText[ Index + 1 ] == '/' )
			{
				if ( bComments )
				{
					StrippedText = Index + 2 < Length ?
								   StrippedText.Left( BlockComment ) + StrippedText.RightChop( Index + 2 ) :
								   StrippedText.Left( BlockComment );

					int32 CommentLength = Index + 2 - BlockComment;
					Length -= CommentLength;
					Index  -= CommentLength - 1;
				}

				BlockComment = -1;
			}

			continue;
		}
		else if ( LineComment >= 0 )
		{
			if ( StrippedCharacter == '\r' || StrippedCharacter == '\n' )
			{
				if ( bComments )
				{
					StrippedText = StrippedText.Left( LineComment ) + StrippedText.RightChop( Index );
				
					int32 CommentLength = Index - LineComment;
					Length -= CommentLength;
					Index  -= CommentLength;
				}

				LineComment = -1;
			}

			continue;
		}
		else if ( !bStringLiteral && StrippedCharacter == '/' && Index + 1 < Length )
		{
			if ( StrippedText[ Index + 1 ] == '*' )
			{
				BlockComment = Index;
				++Index;
			}
			else if ( StrippedText[ Index + 1 ] == '/' )
			{
				LineComment = Index;
				++Index;
			}
			else
				TrailingComma = -1;
			
			bEscapeCharacter = false;
			continue;
		}
		else if ( !bStringLiteral && TrailingComma >= 0 && ( StrippedCharacter == '}'
														  || StrippedCharacter == ']' ) )
		{
			if ( bTrailingCommas )
			{
				StrippedText = StrippedText.Left( TrailingComma ) + StrippedText.RightChop( TrailingComma + 1 );
				
				Length -= 1;
				Index  -= 1;
			}

			TrailingComma    = -1;
			bEscapeCharacter = false;
			continue;
		}

		if ( bStringLiteral )
			TrailingComma = -1;
		else if ( StrippedCharacter == ',' )
			TrailingComma = Index;
		else if ( !FChar::IsWhitespace( StrippedCharacter ) && StrippedCharacter != '\r'
															&& StrippedCharacter != '\n')
			TrailingComma = -1;
		
		if ( !bEscapeCharacter )
		{
			if ( StrippedCharacter == '"' )
				bStringLiteral = !bStringLiteral;
			else if ( StrippedCharacter == '\\' )
				bEscapeCharacter = true;
		}
		else
			bEscapeCharacter = false;
	}

	if ( BlockComment >= 0 )
		StrippedText = StrippedText.Left( BlockComment );
	else if ( LineComment >= 0 )
		StrippedText = StrippedText.Left( LineComment );
	
	return StrippedText;
}
