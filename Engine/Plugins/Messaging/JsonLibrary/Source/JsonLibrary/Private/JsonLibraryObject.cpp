// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryObject.h"
#include "JsonLibraryList.h"
#include "JsonLibraryHelpers.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

FJsonLibraryObject::FJsonLibraryObject( const TSharedPtr<FJsonValue>& Value )
{
	if ( Value.IsValid() && Value->Type == EJson::Object )
		JsonObject = StaticCastSharedPtr<FJsonValueObject>( Value );
}

FJsonLibraryObject::FJsonLibraryObject( const TSharedPtr<FJsonValueObject>& Value )
{
	JsonObject = Value;
}

FJsonLibraryObject::FJsonLibraryObject()
{
	JsonObject = MakeShareable( new FJsonValueObject( MakeShareable( new FJsonObject() ) ) );
}

FJsonLibraryObject::FJsonLibraryObject( const FJsonLibraryObjectNotify& Notify )
	: FJsonLibraryObject()
{
	OnNotify = Notify;
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FJsonLibraryValue>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FJsonLibraryValue>& Temp : Value )
			Json->SetField( Temp.Key, Temp.Value.JsonValue );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, bool>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, bool>& Temp : Value )
			Json->SetBoolField( Temp.Key, Temp.Value );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, float>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, float>& Temp : Value )
			Json->SetNumberField( Temp.Key, Temp.Value );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, double>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, double>& Temp : Value )
			Json->SetNumberField( Temp.Key, Temp.Value );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, int32>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, int32>& Temp : Value )
			Json->SetNumberField( Temp.Key, (double)Temp.Value );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FString>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FString>& Temp : Value )
			Json->SetStringField( Temp.Key, Temp.Value );
	}
}

bool FJsonLibraryObject::Equals( const FJsonLibraryObject& Object ) const
{
	if ( !JsonObject.IsValid() || !Object.JsonObject.IsValid() )
		return false;

	if ( JsonObject == Object.JsonObject )
		return true;
	
	const TSharedPtr<FJsonObject> JsonA = GetJsonObject();
	const TSharedPtr<FJsonObject> JsonB = Object.GetJsonObject();
	if ( JsonA.IsValid() && JsonB.IsValid() )
		return JsonA == JsonB;

	return false;
}

int32 FJsonLibraryObject::Count() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return 0;

	return Json->Values.Num();
}

void FJsonLibraryObject::Clear()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( !Json.IsValid() )
		return;

	NotifyCheck();
	Json->Values.Empty();
	NotifyClear();
}

bool FJsonLibraryObject::HasKey( const FString& Key ) const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return false;
	
	return Json->HasField( Key );
}

void FJsonLibraryObject::RemoveKey( const FString& Key )
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( !Json.IsValid() )
		return;
	
	NotifyCheck( Key );
	Json->RemoveField( Key );
	NotifyRemove( Key );
}

void FJsonLibraryObject::Add( const FJsonLibraryObject& Object )
{
	const TSharedPtr<FJsonObject> ObjectJson = Object.GetJsonObject();
	if ( !ObjectJson.IsValid() )
		return;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : ObjectJson->Values )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddBooleanMap( const TMap<FString, bool>& Map )
{
	for ( const TPair<FString, bool>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddFloatMap( const TMap<FString, float>& Map )
{
	for ( const TPair<FString, float>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddIntegerMap( const TMap<FString, int32>& Map )
{
	for ( const TPair<FString, int32>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddNumberMap( const TMap<FString, double>& Map )
{
	for ( const TPair<FString, double>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddStringMap( const TMap<FString, FString>& Map )
{
	for ( const TPair<FString, FString>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

TArray<FString> FJsonLibraryObject::GetKeys() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TArray<FString> Keys;
	if ( !Json.IsValid() )
		return Keys;

	Json->Values.GetKeys( Keys );
	return Keys;
}

TArray<FJsonLibraryValue> FJsonLibraryObject::GetValues() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TArray<FJsonLibraryValue> Values;
	if ( !Json.IsValid() )
		return Values;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Values.Add( FJsonLibraryValue( Temp.Value ) );

	return Values;
}

bool FJsonLibraryObject::GetBoolean( const FString& Key ) const
{
	return GetValue( Key ).GetBoolean();
}

float FJsonLibraryObject::GetFloat( const FString& Key ) const
{
	return GetValue( Key ).GetFloat();
}

int32 FJsonLibraryObject::GetInteger( const FString& Key ) const
{
	return GetValue( Key ).GetInteger();
}

double FJsonLibraryObject::GetNumber( const FString& Key ) const
{
	return GetValue( Key ).GetNumber();
}

FString FJsonLibraryObject::GetString( const FString& Key ) const
{
	return GetValue( Key ).GetString();
}

FJsonLibraryValue FJsonLibraryObject::GetValue( const FString& Key ) const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return FJsonLibraryValue( TSharedPtr<FJsonValue>() );
	
	return FJsonLibraryValue( Json->TryGetField( Key ) );
}

FJsonLibraryObject FJsonLibraryObject::GetObject( const FString& Key ) const
{
	return GetValue( Key ).GetObject();
}

FJsonLibraryList FJsonLibraryObject::GetList( const FString& Key ) const
{
	return GetValue( Key ).GetList();
}

TArray<FJsonLibraryValue> FJsonLibraryObject::GetArray( const FString& Key ) const
{
	return GetValue( Key ).ToArray();
}

TMap<FString, FJsonLibraryValue> FJsonLibraryObject::GetMap( const FString& Key ) const
{
	return GetValue( Key ).ToMap();
}

void FJsonLibraryObject::SetBoolean( const FString& Key, bool Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetFloat( const FString& Key, float Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetInteger( const FString& Key, int32 Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetNumber( const FString& Key, double Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetString( const FString& Key, const FString& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetValue( const FString& Key, const FJsonLibraryValue& Value )
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( !Json.IsValid() )
		return;

	NotifyCheck( Key );
	Json->SetField( Key, Value.JsonValue );
	NotifyAddOrChange( Key, Value );
}

void FJsonLibraryObject::SetObject( const FString& Key, const FJsonLibraryObject& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetList( const FString& Key, const FJsonLibraryList& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetArray( const FString& Key, const TArray<FJsonLibraryValue>& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetMap( const FString& Key, const TMap<FString, FJsonLibraryValue>& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

const TSharedPtr<FJsonObject> FJsonLibraryObject::GetJsonObject() const
{
	if ( JsonObject.IsValid() && JsonObject->Type == EJson::Object )
	{
		const TSharedPtr<FJsonObject>* Object;
		if ( JsonObject->TryGetObject( Object ) && Object )
			return *Object;
	}

	return TSharedPtr<FJsonObject>();
}

TSharedPtr<FJsonObject> FJsonLibraryObject::SetJsonObject()
{
	return GetJsonObject();
}

bool FJsonLibraryObject::TryParse( const FString& Text )
{
	if ( Text.IsEmpty() )
		return false;

	FString TrimmedText = Text;
	TrimmedText.TrimStartInline();
	TrimmedText.TrimEndInline();

	if ( !TrimmedText.StartsWith( "{" ) || !TrimmedText.EndsWith( "}" ) )
		return false;

	TSharedPtr<FJsonObject> Object;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create( TrimmedText );
	if ( !FJsonSerializer::Deserialize( Reader, Object ) || !Object.IsValid() )
		return false;

	JsonObject = MakeShareable( new FJsonValueObject( Object ) );

	NotifyParse();
	return true;
}

bool FJsonLibraryObject::TryStringify( FString& Text, bool bCondensed /*= true*/ ) const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return false;

	if ( bCondensed )
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
		if ( !FJsonSerializer::Serialize( Json.ToSharedRef(), Writer ) )
			return false;
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( &Text );
		if ( !FJsonSerializer::Serialize( Json.ToSharedRef(), Writer ) )
			return false;
	}

	Text.TrimStartInline();
	Text.TrimEndInline();

	if ( !Text.StartsWith( "{" ) || !Text.EndsWith( "}" ) )
		return false;

	return true;
}

void FJsonLibraryObject::NotifyAddOrChange( const FString& Key, const FJsonLibraryValue& Value )
{
	if ( !OnNotify.IsBound() )
		return;

	if ( bNotifyHasKey )
	{
		if ( !Value.Equals( FJsonLibraryValue( NotifyValue ), true ) )
			OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Changed, Key, Value );
		else
			OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, Key, Value );
	}
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Added, Key, Value );

	bNotifyHasKey = false;
	NotifyValue.Reset();
}

bool FJsonLibraryObject::NotifyCheck()
{
	if ( !OnNotify.IsBound() )
		return false;

	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( Json.IsValid() )
		bNotifyHasKey = Json->Values.Num() > 0;
	else
		bNotifyHasKey = false;

	NotifyValue.Reset();
	return bNotifyHasKey;
}

bool FJsonLibraryObject::NotifyCheck( const FString& Key )
{
	if ( !OnNotify.IsBound() )
		return false;

	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( Json.IsValid() )
		bNotifyHasKey = Json->HasField( Key );
	else
		bNotifyHasKey = false;

	NotifyValue.Reset();
	if ( bNotifyHasKey )
		NotifyValue = Json->TryGetField( Key );

	return bNotifyHasKey;
}

void FJsonLibraryObject::NotifyClear()
{
	if ( !OnNotify.IsBound() )
		return;

	NotifyValue.Reset();
	if ( bNotifyHasKey )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Reset, FString(), FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, FString(), FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );

	bNotifyHasKey = false;
}

void FJsonLibraryObject::NotifyParse()
{
	if ( !OnNotify.IsBound() )
		return;

	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return;

	NotifyValue.Reset();
	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Added, Temp.Key, FJsonLibraryValue( Temp.Value ) );

	bNotifyHasKey = false;
}

void FJsonLibraryObject::NotifyRemove( const FString& Key )
{
	if ( !OnNotify.IsBound() )
		return;

	if ( bNotifyHasKey )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Removed, Key, FJsonLibraryValue( NotifyValue ) );
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, Key, FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );

	bNotifyHasKey = false;
	NotifyValue.Reset();
}

bool FJsonLibraryObject::IsValid() const
{
	return GetJsonObject().IsValid();
}

bool FJsonLibraryObject::IsEmpty() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return false;

	return Json->Values.Num() == 0;
}

FJsonLibraryObject FJsonLibraryObject::Parse( const FString& Text )
{
	FJsonLibraryObject Object = TSharedPtr<FJsonValueObject>();
	if ( !Object.TryParse( Text ) )
		Object.JsonObject.Reset();
	
	return Object;
}

FJsonLibraryObject FJsonLibraryObject::Parse( const FString& Text, const FJsonLibraryObjectNotify& Notify )
{
	FJsonLibraryObject Object = Parse( Text );
	Object.OnNotify = Notify;

	return Object;
}

FString FJsonLibraryObject::Stringify() const
{
	FString Text;
	if ( TryStringify( Text ) )
		return Text;

	return FString();
}

TMap<FString, FJsonLibraryValue> FJsonLibraryObject::ToMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FJsonLibraryValue> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ) );
	
	return Map;
}

TMap<FString, bool> FJsonLibraryObject::ToBooleanMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, bool> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetBoolean() );

	return Map;
}

TMap<FString, float> FJsonLibraryObject::ToFloatMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, float> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetFloat() );

	return Map;
}

TMap<FString, int32> FJsonLibraryObject::ToIntegerMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, int32> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetInteger() );

	return Map;
}

TMap<FString, double> FJsonLibraryObject::ToNumberMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, double> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetNumber() );

	return Map;
}

TMap<FString, FString> FJsonLibraryObject::ToStringMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FString> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetString() );

	return Map;
}

bool FJsonLibraryObject::operator==( const FJsonLibraryObject& Object ) const
{
	return Equals( Object );
}

bool FJsonLibraryObject::operator!=( const FJsonLibraryObject& Object ) const
{
	return !Equals( Object );
}

bool FJsonLibraryObject::operator==( const FJsonLibraryValue& Value ) const
{
	return Value.Equals( FJsonLibraryValue( *this ) );
}

bool FJsonLibraryObject::operator!=( const FJsonLibraryValue& Value ) const
{
	return !Value.Equals( FJsonLibraryValue( *this ) );
}
