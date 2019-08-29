// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryHelpers.h"

FJsonLibraryValue UJsonLibraryHelpers::Parse( const FString& Text )
{
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

FString UJsonLibraryHelpers::JsonValue_Stringify( const FJsonLibraryValue& Target )
{
	return Target.Stringify();
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

FString UJsonLibraryHelpers::JsonObject_Stringify( const FJsonLibraryObject& Target )
{
	return Target.Stringify();
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

FString UJsonLibraryHelpers::JsonList_Stringify( const FJsonLibraryList& Target )
{
	return Target.Stringify();
}
