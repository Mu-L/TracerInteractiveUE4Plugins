// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryValue.h"
#include "JsonLibraryObject.h"
#include "JsonLibraryList.h"
#include "JsonLibraryHelpers.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

FJsonLibraryValue::FJsonLibraryValue( const TSharedPtr<FJsonValue>& Value )
{
	JsonValue = Value;
}

FJsonLibraryValue::FJsonLibraryValue()
{
	JsonValue = MakeShareable( new FJsonValueNull() );
}

FJsonLibraryValue::FJsonLibraryValue( bool Value )
{
	JsonValue = MakeShareable( new FJsonValueBoolean( Value ) );
}

FJsonLibraryValue::FJsonLibraryValue( float Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( double Value )
{
	JsonValue = MakeShareable( new FJsonValueNumber( Value ) );
}

FJsonLibraryValue::FJsonLibraryValue( int8 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( uint8 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( int16 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( uint16 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( int32 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( uint32 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( int64 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( uint64 Value )
	: FJsonLibraryValue( (double)Value )
{
	//
}

FJsonLibraryValue::FJsonLibraryValue( const FString& Value )
{
	JsonValue = MakeShareable( new FJsonValueString( Value ) );
}

FJsonLibraryValue::FJsonLibraryValue( const FJsonLibraryObject& Value )
{
	JsonValue = Value.JsonObject;
}

FJsonLibraryValue::FJsonLibraryValue( const FJsonLibraryList& Value )
{
	JsonValue = Value.JsonArray;
}

FJsonLibraryValue::FJsonLibraryValue( const TArray<FJsonLibraryValue>& Value )
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for ( int32 i = 0; i < Value.Num(); i++ )
		Array.Add( Value[ i ].JsonValue );

	JsonValue = MakeShareable( new FJsonValueArray( Array ) );
}

FJsonLibraryValue::FJsonLibraryValue( const TMap<FString, FJsonLibraryValue>& Value )
{
	TSharedPtr<FJsonObject> Object = MakeShareable( new FJsonObject() );
	for ( const TPair<FString, FJsonLibraryValue>& Temp : Value )
		Object->SetField( Temp.Key, Temp.Value.JsonValue );

	JsonValue = MakeShareable( new FJsonValueObject( Object ) );
}

EJsonLibraryType FJsonLibraryValue::GetType() const
{
	if ( !JsonValue.IsValid() )
		return EJsonLibraryType::Invalid;

	switch ( JsonValue->Type )
	{
		case EJson::Null:    return EJsonLibraryType::Null;
		case EJson::Boolean: return EJsonLibraryType::Boolean;
		case EJson::Number:  return EJsonLibraryType::Number;
		case EJson::String:  return EJsonLibraryType::String;
		case EJson::Object:  return EJsonLibraryType::Object;
		case EJson::Array:   return EJsonLibraryType::Array;
	}

	return EJsonLibraryType::Invalid;
}

bool FJsonLibraryValue::Equals( const FJsonLibraryValue& Value, bool bStrict /*= false*/ ) const
{
	if ( !JsonValue.IsValid() )
	{
		if ( !Value.JsonValue.IsValid() )
			return true;

		EJson Type = Value.JsonValue->Type;
		if ( Type == EJson::None )
			return true;
		if ( !bStrict && Type == EJson::Null )
			return true;

		return false;
	}
	else if ( !Value.JsonValue.IsValid() )
	{
		EJson Type = JsonValue->Type;
		if ( Type == EJson::None )
			return true;
		if ( !bStrict && Type == EJson::Null )
			return true;

		return false;
	}
	
	if ( JsonValue == Value.JsonValue )
		return true;

	if ( JsonValue->Type == Value.JsonValue->Type )
	{
		switch ( JsonValue->Type )
		{
			case EJson::None:    return true;
			case EJson::Null:    return true;
			case EJson::Boolean: return JsonValue->AsBool()   == Value.JsonValue->AsBool();
			case EJson::Number:  return JsonValue->AsNumber() == Value.JsonValue->AsNumber();
			case EJson::String:  return JsonValue->AsString() == Value.JsonValue->AsString();
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject>& JsonA = JsonValue->AsObject();
				const TSharedPtr<FJsonObject>& JsonB = Value.JsonValue->AsObject();
				if ( JsonA.IsValid() && JsonB.IsValid() )
					return JsonA == JsonB;
			}
			case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>* JsonA;
				const TArray<TSharedPtr<FJsonValue>>* JsonB;
				if ( JsonValue->TryGetArray( JsonA ) && Value.JsonValue->TryGetArray( JsonB ) )
					return JsonA == JsonB;
			}
		}

		return false;
	}

	if ( bStrict )
		return false;
	
	EJson TypeA = JsonValue->Type;
	EJson TypeB = Value.JsonValue->Type;

	if ( TypeA == EJson::None || TypeA == EJson::Null )
	{
		if ( TypeB == EJson::None || TypeB == EJson::Null )
			return true;
	}

	if ( TypeA == EJson::Number && TypeB == EJson::String )
		return JsonValue->AsNumber() == Value.GetNumber();
	if ( TypeA == EJson::String && TypeB == EJson::Number )
		return GetNumber() == Value.JsonValue->AsNumber();

	if ( TypeA == EJson::Boolean )
	{
		if ( TypeB == EJson::Number )
			return GetNumber() == Value.JsonValue->AsNumber();
		if ( TypeB == EJson::String )
			return GetNumber() == Value.GetNumber();
	}

	if ( TypeB == EJson::Boolean )
	{
		if ( TypeA == EJson::Number )
			return JsonValue->AsNumber() == Value.GetNumber();
		if ( TypeA == EJson::String )
			return GetNumber() == Value.GetNumber();
	}
	
	return false;
}

bool FJsonLibraryValue::GetBoolean() const
{
	if ( !JsonValue.IsValid() )
		return false;

	switch ( JsonValue->Type )
	{
		case EJson::Boolean: return JsonValue->AsBool();
		case EJson::Number:  return JsonValue->AsNumber() != 0.0;
		case EJson::String:  return JsonValue->AsString().ToBool();
	}

	return false;
}

float FJsonLibraryValue::GetFloat() const
{
	return (float)GetNumber();
}

double FJsonLibraryValue::GetNumber() const
{
	if ( !JsonValue.IsValid() )
		return 0.0;

	switch ( JsonValue->Type )
	{
		case EJson::Boolean: return JsonValue->AsBool() ? 1.0 : 0.0;
		case EJson::Number:  return JsonValue->AsNumber();
		case EJson::String:
		{
			const FString& Value = JsonValue->AsString();
			return Value.IsNumeric() ? FCString::Atod( *Value ) : 0.0;
		}
	}

	return 0.0;
}

int32 FJsonLibraryValue::GetInteger() const
{
	return (int32)GetNumber();
}

FString FJsonLibraryValue::GetString() const
{
	if ( !JsonValue.IsValid() )
		return FString();
	
	switch ( JsonValue->Type )
	{
		case EJson::Boolean: return JsonValue->AsBool() ? TEXT( "true" ) : TEXT( "false" );
		case EJson::Number:  return FString::SanitizeFloat( JsonValue->AsNumber(), 0 );
		case EJson::String:  return JsonValue->AsString();
	}

	return FString();
}

FJsonLibraryObject FJsonLibraryValue::GetObject() const
{
	return FJsonLibraryObject( JsonValue );
}

FJsonLibraryList FJsonLibraryValue::GetList() const
{
	return FJsonLibraryList( JsonValue );
}

int8 FJsonLibraryValue::GetInt8() const
{
	return (int16)GetNumber();
}

uint8 FJsonLibraryValue::GetUInt8() const
{
	return (uint16)GetNumber();
}

int16 FJsonLibraryValue::GetInt16() const
{
	return (int16)GetNumber();
}

uint16 FJsonLibraryValue::GetUInt16() const
{
	return (uint16)GetNumber();
}

int32 FJsonLibraryValue::GetInt32() const
{
	return (int32)GetNumber();
}

uint32 FJsonLibraryValue::GetUInt32() const
{
	return (uint32)GetNumber();
}

int64 FJsonLibraryValue::GetInt64() const
{
	return (int64)GetNumber();
}

uint64 FJsonLibraryValue::GetUInt64() const
{
	return (uint64)GetNumber();
}

bool FJsonLibraryValue::TryParse( const FString& Text )
{
	if ( Text.IsEmpty() )
		return false;

	FString TrimmedText = Text;
	TrimmedText.TrimStartInline();
	TrimmedText.TrimEndInline();

	if ( ( TrimmedText.StartsWith( "{" ) && TrimmedText.EndsWith( "}" ) )
	  || ( TrimmedText.StartsWith( "[" ) && TrimmedText.EndsWith( "]" ) ) )
	{
		// deserialize object or array
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create( TrimmedText );
		if ( !FJsonSerializer::Deserialize( Reader, JsonValue ) || !JsonValue.IsValid() )
			return false;

		// check type
		if ( JsonValue->Type != EJson::Object && JsonValue->Type != EJson::Array )
		{
			JsonValue.Reset();
			return false;
		}
	}
	else
	{
		// wrap value with array
		TrimmedText = FString::Printf( TEXT( "[%s]" ), *TrimmedText );
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create( TrimmedText );

		TArray<TSharedPtr<FJsonValue>> JsonArray;
		if ( !FJsonSerializer::Deserialize( Reader, JsonArray ) || JsonArray.Num() != 1 )
			return false;

		// unwrap value
		JsonValue = JsonArray[ 0 ];
	}

	return JsonValue.IsValid();
}

bool FJsonLibraryValue::TryStringify( FString& Text, bool bCondensed /*= true*/ ) const
{
	if ( !JsonValue.IsValid() || JsonValue->Type == EJson::None )
		return false;
	
	if ( JsonValue->Type == EJson::Object )
	{
		if ( bCondensed )
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonValue->AsObject().ToSharedRef(), Writer ) )
				return false;
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonValue->AsObject().ToSharedRef(), Writer ) )
				return false;
		}

		Text.TrimStartInline();
		Text.TrimEndInline();

		if ( !Text.StartsWith( "{" ) || !Text.EndsWith( "}" ) )
			return false;
	}
	else if ( JsonValue->Type == EJson::Array )
	{
		if ( bCondensed )
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonValue->AsArray(), Writer ) )
				return false;
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonValue->AsArray(), Writer ) )
				return false;
		}

		Text.TrimStartInline();
		Text.TrimEndInline();

		if ( !Text.StartsWith( "[" ) || !Text.EndsWith( "]" ) )
			return false;
	}
	else
	{
		// wrap value with array
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		JsonArray.Add( JsonValue );
		
		if ( bCondensed )
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonArray, Writer ) )
				return false;
		}
		else
		{
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
			if ( !FJsonSerializer::Serialize( JsonArray, Writer ) )
				return false;
		}

		Text.TrimStartInline();
		Text.TrimEndInline();

		if ( !Text.StartsWith( "[" ) || !Text.EndsWith( "]" ) )
			return false;

		int32 Length = Text.Len();
		if ( Length > 2 )
		{
			// trim array brackets
			Text = Text.Mid( 1, Length - 2 );
			Text.TrimStartInline();
			Text.TrimEndInline();
		}
		else
			Text = FString();
	}

	return true;
}

bool FJsonLibraryValue::IsValid() const
{
	return GetType() != EJsonLibraryType::Invalid;
}

FJsonLibraryValue FJsonLibraryValue::Parse( const FString& Text )
{
	FJsonLibraryValue Value = TSharedPtr<FJsonValue>();
	if ( !Value.TryParse( Text ) )
		Value.JsonValue.Reset();
	
	return Value;
}

FString FJsonLibraryValue::Stringify() const
{
	FString Text;
	if ( TryStringify( Text ) )
		return Text;

	return FString();
}

TArray<FJsonLibraryValue> FJsonLibraryValue::ToArray() const
{
	return FJsonLibraryList( JsonValue ).ToArray();
}

TMap<FString, FJsonLibraryValue> FJsonLibraryValue::ToMap() const
{
	return FJsonLibraryObject( JsonValue ).ToMap();
}

bool FJsonLibraryValue::operator==( const FJsonLibraryValue& Value ) const
{
	return Equals( Value );
}

bool FJsonLibraryValue::operator!=( const FJsonLibraryValue& Value ) const
{
	return !Equals( Value );
}

bool FJsonLibraryValue::operator==( const FJsonLibraryObject& Object ) const
{
	return Equals( FJsonLibraryValue( Object ) );
}

bool FJsonLibraryValue::operator!=( const FJsonLibraryObject& Object ) const
{
	return !Equals( FJsonLibraryValue( Object ) );
}

bool FJsonLibraryValue::operator==( const FJsonLibraryList& List ) const
{
	return Equals( FJsonLibraryValue( List ) );
}

bool FJsonLibraryValue::operator!=( const FJsonLibraryList& List ) const
{
	return !Equals( FJsonLibraryValue( List ) );
}
