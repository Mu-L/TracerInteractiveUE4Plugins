// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryObject.h"
#include "JsonLibraryConverter.h"
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

FJsonLibraryObject::FJsonLibraryObject( const UStruct* StructType, const void* StructPtr )
	: FJsonLibraryObject()
{
	if ( StructType && StructPtr )
	{
		TSharedPtr<FJsonObject> Json = SetJsonObject();
		if ( Json.IsValid() )
		{
			if ( !FJsonLibraryConverter::UStructToJsonObject( StructType, StructPtr, Json.ToSharedRef() ) )
				JsonObject.Reset();
		}
	}
	else
		JsonObject.Reset();
}

FJsonLibraryObject::FJsonLibraryObject( const TSharedPtr<FStructOnScope>& StructData )
	: FJsonLibraryObject()
{
	if ( StructData.IsValid() )
	{
		TSharedPtr<FJsonObject> Json = SetJsonObject();
		if ( Json.IsValid() )
		{
			if ( !FJsonLibraryConverter::UStructToJsonObject( StructData->GetStruct(), StructData->GetStructMemory(), Json.ToSharedRef() ) )
				JsonObject.Reset();
		}
	}
	else
		JsonObject.Reset();
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

FJsonLibraryObject::FJsonLibraryObject( const FLinearColor& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		Json->SetNumberField( "r", Value.R );
		Json->SetNumberField( "g", Value.G );
		Json->SetNumberField( "b", Value.B );
		if ( Value.A != 1.0f )
			Json->SetNumberField( "a", Value.A );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const FRotator& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		Json->SetNumberField( "pitch", Value.Pitch );
		Json->SetNumberField( "yaw",   Value.Yaw );
		Json->SetNumberField( "roll",  Value.Roll );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const FTransform& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		Json->SetField( "rotation", FJsonLibraryObject( Value.GetRotation().Rotator() ).JsonObject );
		Json->SetField( "translation", FJsonLibraryObject( Value.GetTranslation() ).JsonObject );

		const FVector Scale = Value.GetScale3D();
		if ( Scale != FVector::OneVector )
		{
			if ( Scale.IsUniform() )
				Json->SetNumberField( "scale", Scale.X );
			else
				Json->SetField( "scale", FJsonLibraryObject( Scale ).JsonObject );
		}
	}
}

FJsonLibraryObject::FJsonLibraryObject( const FVector& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		Json->SetNumberField( "x", Value.X );
		Json->SetNumberField( "y", Value.Y );
		Json->SetNumberField( "z", Value.Z );
	}
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

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FDateTime>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FDateTime>& Temp : Value )
			Json->SetStringField( Temp.Key, Temp.Value.ToIso8601() );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FGuid>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FGuid>& Temp : Value )
			Json->SetStringField( Temp.Key, Temp.Value.ToString( EGuidFormats::DigitsWithHyphens ) );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FColor>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FColor>& Temp : Value )
			Json->SetStringField( Temp.Key, "#" + Temp.Value.ToHex() );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FLinearColor>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FLinearColor>& Temp : Value )
			Json->SetField( Temp.Key, FJsonLibraryObject( Temp.Value ).JsonObject );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FRotator>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FRotator>& Temp : Value )
			Json->SetField( Temp.Key, FJsonLibraryObject( Temp.Value ).JsonObject );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FTransform>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FTransform>& Temp : Value )
			Json->SetField( Temp.Key, FJsonLibraryObject( Temp.Value ).JsonObject );
	}
}

FJsonLibraryObject::FJsonLibraryObject( const TMap<FString, FVector>& Value )
	: FJsonLibraryObject()
{
	TSharedPtr<FJsonObject> Json = SetJsonObject();
	if ( Json.IsValid() )
	{
		for ( const TPair<FString, FVector>& Temp : Value )
			Json->SetField( Temp.Key, FJsonLibraryObject( Temp.Value ).JsonObject );
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

void FJsonLibraryObject::AddDateTimeMap( const TMap<FString, FDateTime>& Map )
{
	for ( const TPair<FString, FDateTime>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddGuidMap( const TMap<FString, FGuid>& Map )
{
	for ( const TPair<FString, FGuid>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddColorMap( const TMap<FString, FColor>& Map )
{
	for ( const TPair<FString, FColor>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddLinearColorMap( const TMap<FString, FLinearColor>& Map )
{
	for ( const TPair<FString, FLinearColor>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddRotatorMap( const TMap<FString, FRotator>& Map )
{
	for ( const TPair<FString, FRotator>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddTransformMap( const TMap<FString, FTransform>& Map )
{
	for ( const TPair<FString, FTransform>& Temp : Map )
		SetValue( Temp.Key, FJsonLibraryValue( Temp.Value ) );
}

void FJsonLibraryObject::AddVectorMap( const TMap<FString, FVector>& Map )
{
	for ( const TPair<FString, FVector>& Temp : Map )
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

FDateTime FJsonLibraryObject::GetDateTime( const FString& Key ) const
{
	return GetValue( Key ).GetDateTime();
}

FGuid FJsonLibraryObject::GetGuid( const FString& Key ) const
{
	return GetValue( Key ).GetGuid();
}

FColor FJsonLibraryObject::GetColor( const FString& Key ) const
{
	return GetValue( Key ).GetColor();
}

FLinearColor FJsonLibraryObject::GetLinearColor( const FString& Key ) const
{
	return GetValue( Key ).GetLinearColor();
}

FRotator FJsonLibraryObject::GetRotator( const FString& Key ) const
{
	return GetValue( Key ).GetRotator();
}

FTransform FJsonLibraryObject::GetTransform( const FString& Key ) const
{
	return GetValue( Key ).GetTransform();
}

FVector FJsonLibraryObject::GetVector( const FString& Key ) const
{
	return GetValue( Key ).GetVector();
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

void FJsonLibraryObject::SetDateTime( const FString& Key, const FDateTime& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetGuid( const FString& Key, const FGuid& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetColor( const FString& Key, const FColor& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetLinearColor( const FString& Key, const FLinearColor& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetRotator( const FString& Key, const FRotator& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetTransform( const FString& Key, const FTransform& Value )
{
	SetValue( Key, FJsonLibraryValue( Value ) );
}

void FJsonLibraryObject::SetVector( const FString& Key, const FVector& Value )
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

bool FJsonLibraryObject::TryParse( const FString& Text, bool bStripComments /*= false*/, bool bStripTrailingCommas /*= false*/ )
{
	if ( Text.IsEmpty() )
		return false;

	FString TrimmedText = Text;
	TrimmedText.TrimStartInline();
	TrimmedText.TrimEndInline();

	if ( bStripComments || bStripTrailingCommas )
		TrimmedText = UJsonLibraryHelpers::StripCommentsOrCommas( TrimmedText, bStripComments, bStripTrailingCommas );

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

bool FJsonLibraryObject::IsLinearColor() const
{
	int32 Keys = Count();
	if ( Keys < 3 || Keys > 4 )
		return false;

	if ( !HasKey( "r" )
	  || !HasKey( "g" )
	  || !HasKey( "b" ) )
		return false;

	if ( Keys == 3 )
		return true;
	if ( Keys == 4 && HasKey( "a" ) )
		return true;
	
	return false;
}

bool FJsonLibraryObject::IsRotator() const
{
	if ( Count() != 3 )
		return false;
	
	if ( HasKey( "pitch" )
	  && HasKey( "yaw" )
	  && HasKey( "roll" ) )
		return true;

	return false;
}

bool FJsonLibraryObject::IsTransform() const
{
	int32 Keys = Count();
	if ( Keys == 0 || Keys > 3 )
		return false;

	if ( !HasKey( "rotation" )
	  || !HasKey( "translation" ) )
		return false;

	if ( !GetValue( "rotation" ).IsRotator() )
		return false;
	if ( !GetValue( "translation" ).IsVector() )
		return false;
	
	if ( Keys == 2 )
		return true;

	if ( Keys == 3 && HasKey( "scale" ) )
	{
		FJsonLibraryValue Scale = GetValue( "scale" );
		switch ( Scale.GetType() )
		{
			case EJsonLibraryType::Number:
			case EJsonLibraryType::String:
				return true;

			case EJsonLibraryType::Object:
				if ( Scale.IsVector() )
					return true;
		}
	}

	return false;
}

bool FJsonLibraryObject::IsVector() const
{
	if ( Count() != 3 )
		return false;
	
	if ( HasKey( "x" )
	  && HasKey( "y" )
	  && HasKey( "z" ) )
		return true;

	return false;
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

FJsonLibraryObject FJsonLibraryObject::ParseRelaxed( const FString& Text, bool bStripComments /*= true*/, bool bStripTrailingCommas /*= true*/ )
{
	FJsonLibraryObject Object = TSharedPtr<FJsonValueObject>();
	if ( !Object.TryParse( Text, bStripComments, bStripTrailingCommas ) )
		Object.JsonObject.Reset();
	
	return Object;
}

FString FJsonLibraryObject::Stringify( bool bCondensed /*= true*/ ) const
{
	FString Text;
	if ( TryStringify( Text, bCondensed ) )
		return Text;

	return FString();
}


bool FJsonLibraryObject::ToStruct( const UStruct* StructType, void* StructPtr ) const
{
	if ( !StructType || !StructPtr )
		return false;

	const TSharedPtr<FJsonObject> Json = GetJsonObject();
	if ( !Json.IsValid() )
		return false;
	
	return FJsonLibraryConverter::JsonObjectToUStruct( Json.ToSharedRef(), StructType, StructPtr );
}

TSharedPtr<FStructOnScope> FJsonLibraryObject::ToStruct( const UStruct* StructType ) const
{
	if ( StructType )
	{
		TSharedPtr<FStructOnScope> StructData = MakeShareable( new FStructOnScope( StructType ) );
		if ( ToStruct( StructType, StructData->GetStructMemory() ) )
			return StructData;
	}

	return TSharedPtr<FStructOnScope>();
}

FLinearColor FJsonLibraryObject::ToLinearColor() const
{
	if ( !IsLinearColor() )
		return FLinearColor();
	
	return FLinearColor( GetFloat( "r" ),
					     GetFloat( "g" ),
					     GetFloat( "b" ),
					     HasKey( "a" ) ? GetFloat( "a" ) : 1.0f );
}

FRotator FJsonLibraryObject::ToRotator() const
{
	if ( !IsRotator() )
		return FRotator::ZeroRotator;
	
	return FRotator( GetFloat( "pitch" ),
					 GetFloat( "yaw" ),
					 GetFloat( "roll" ) );
}

FTransform FJsonLibraryObject::ToTransform() const
{
	if ( !IsTransform() )
		return FTransform::Identity;
	
	const FRotator Rotation    = GetRotator( "rotation" );
	const FVector  Translation = GetVector( "translation" );
	if ( !HasKey( "scale" ) )
		return FTransform( Rotation, Translation );

	FJsonLibraryValue Scale = GetValue( "scale" );
	switch ( Scale.GetType() )
	{
		case EJsonLibraryType::Number:
		case EJsonLibraryType::String:
			return FTransform( Rotation, Translation, FVector( Scale.GetNumber() ) );

		case EJsonLibraryType::Object:
			if ( Scale.IsVector() )
				return FTransform( Rotation, Translation, Scale.GetVector() );
	}
	
	return FTransform( Rotation, Translation );
}

FVector FJsonLibraryObject::ToVector() const
{
	if ( !IsVector() )
		return FVector::ZeroVector;

	return FVector( GetFloat( "x" ),
					GetFloat( "y" ),
					GetFloat( "z" ) );
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

TMap<FString, FDateTime> FJsonLibraryObject::ToDateTimeMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FDateTime> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetDateTime() );

	return Map;
}

TMap<FString, FGuid> FJsonLibraryObject::ToGuidMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FGuid> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetGuid() );

	return Map;
}

TMap<FString, FColor> FJsonLibraryObject::ToColorMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FColor> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetColor() );

	return Map;
}

TMap<FString, FLinearColor> FJsonLibraryObject::ToLinearColorMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FLinearColor> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetLinearColor() );

	return Map;
}

TMap<FString, FRotator> FJsonLibraryObject::ToRotatorMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FRotator> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetRotator() );

	return Map;
}

TMap<FString, FTransform> FJsonLibraryObject::ToTransformMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FTransform> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetTransform() );

	return Map;
}

TMap<FString, FVector> FJsonLibraryObject::ToVectorMap() const
{
	const TSharedPtr<FJsonObject> Json = GetJsonObject();

	TMap<FString, FVector> Map;
	if ( !Json.IsValid() )
		return Map;

	for ( const TPair<FString, TSharedPtr<FJsonValue>>& Temp : Json->Values )
		Map.Add( Temp.Key, FJsonLibraryValue( Temp.Value ).GetVector() );

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
