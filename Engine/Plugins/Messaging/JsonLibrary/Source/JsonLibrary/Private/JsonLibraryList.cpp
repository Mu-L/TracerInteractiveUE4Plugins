// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryList.h"
#include "JsonLibraryObject.h"
#include "JsonLibraryHelpers.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

FJsonLibraryList::FJsonLibraryList( const TSharedPtr<FJsonValue>& Value )
{
	if ( Value.IsValid() && Value->Type == EJson::Array )
		JsonArray = StaticCastSharedPtr<FJsonValueArray>( Value );
}

FJsonLibraryList::FJsonLibraryList( const TSharedPtr<FJsonValueArray>& Value )
{
	JsonArray = Value;
}

FJsonLibraryList::FJsonLibraryList()
{
	JsonArray = MakeShareable( new FJsonValueArray( TArray<TSharedPtr<FJsonValue>>() ) );
}

FJsonLibraryList::FJsonLibraryList( const FJsonLibraryListNotify& Notify )
	: FJsonLibraryList()
{
	OnNotify = Notify;
}

FJsonLibraryList::FJsonLibraryList( const TArray<FJsonLibraryValue>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( Value[ i ].JsonValue );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<bool>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( MakeShareable( new FJsonValueBoolean( Value[ i ] ) ) );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<float>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( MakeShareable( new FJsonValueNumber( Value[ i ] ) ) );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<double>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( MakeShareable( new FJsonValueNumber( Value[ i ] ) ) );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<int32>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( MakeShareable( new FJsonValueNumber( (double)Value[ i ] ) ) );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<FString>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( MakeShareable( new FJsonValueString( Value[ i ] ) ) );
	}
}

FJsonLibraryList::FJsonLibraryList( const TArray<FJsonLibraryObject>& Value )
	: FJsonLibraryList()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( Json )
	{
		for ( int32 i = 0; i < Value.Num(); i++ )
			Json->Add( Value[ i ].JsonObject );
	}
}

bool FJsonLibraryList::Equals( const FJsonLibraryList& List ) const
{
	if ( !JsonArray.IsValid() || !List.JsonArray.IsValid() )
		return false;

	if ( JsonArray == List.JsonArray )
		return true;

	const TArray<TSharedPtr<FJsonValue>>* JsonA = GetJsonArray();
	const TArray<TSharedPtr<FJsonValue>>* JsonB = List.GetJsonArray();
	if ( JsonA && JsonB )
		return JsonA == JsonB;

	return false;
}

int32 FJsonLibraryList::Count() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return 0;

	return Json->Num();
}

void FJsonLibraryList::Clear()
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	NotifyCheck();
	Json->Empty();
	NotifyClear();
}

void FJsonLibraryList::Swap( int32 IndexA, int32 IndexB )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	if ( IndexA >= 0 && IndexA < Json->Num() && IndexB >= 0 && IndexB < Json->Num() )
	{
		if ( OnNotify.IsBound() )
		{
			const TSharedPtr<FJsonValue>& ValueA = ( *Json )[ IndexA ];
			const TSharedPtr<FJsonValue>& ValueB = ( *Json )[ IndexB ];

			NotifyCheck( IndexA );
			( *Json )[ IndexA ] = ValueB;
			NotifyChange( IndexA, FJsonLibraryValue( ValueB ) );

			NotifyCheck( IndexB );
			( *Json )[ IndexB ] = ValueA;
			NotifyChange( IndexB, FJsonLibraryValue( ValueA ) );
		}
		else
			Json->Swap( IndexA, IndexB );
	}
}

void FJsonLibraryList::Append( const FJsonLibraryList& List )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	const TArray<TSharedPtr<FJsonValue>>* ListJson = List.GetJsonArray();
	if ( !ListJson )
		return;

	if ( OnNotify.IsBound() )
	{
		for ( int32 i = 0; i < ListJson->Num(); i++ )
			AddValue( FJsonLibraryValue( ( *ListJson )[ i ] ) );
	}
	else
		Json->Append( *ListJson );
}

void FJsonLibraryList::AppendBooleanArray( const TArray<bool>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AppendFloatArray( const TArray<float>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AppendIntegerArray( const TArray<int32>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AppendNumberArray( const TArray<double>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AppendStringArray( const TArray<FString>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AppendObjectArray( const TArray<FJsonLibraryObject>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		AddValue( FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::Inject( int32 Index, const FJsonLibraryList& List )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	const TArray<TSharedPtr<FJsonValue>>* ListJson = List.GetJsonArray();
	if ( !ListJson )
		return;

	if ( OnNotify.IsBound() )
	{
		for ( int32 i = 0; i < ListJson->Num(); i++ )
			InsertValue( Index + i, FJsonLibraryValue( ( *ListJson )[ i ] ) );
	}
	else
		Json->Insert( *ListJson, Index );
}

void FJsonLibraryList::InjectBooleanArray( int32 Index, const TArray<bool>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::InjectFloatArray( int32 Index, const TArray<float>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::InjectIntegerArray( int32 Index, const TArray<int32>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::InjectNumberArray( int32 Index, const TArray<double>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::InjectStringArray( int32 Index, const TArray<FString>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::InjectObjectArray( int32 Index, const TArray<FJsonLibraryObject>& Array )
{
	for ( int32 i = 0; i < Array.Num(); i++ )
		InsertValue( Index + i, FJsonLibraryValue( Array[ i ] ) );
}

void FJsonLibraryList::AddBoolean( bool Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddFloat( float Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddInteger( int32 Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddNumber( double Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddString( const FString& Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddValue( const FJsonLibraryValue& Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	int32 Index = Json->Num();
	Json->Add( Value.JsonValue );
	NotifyAdd( Index, Value );
}

void FJsonLibraryList::AddObject( const FJsonLibraryObject& Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddList( const FJsonLibraryList& Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddArray( const TArray<FJsonLibraryValue>& Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::AddMap( const TMap<FString, FJsonLibraryValue>& Value )
{
	AddValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertBoolean( int32 Index, bool Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertFloat( int32 Index, float Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertInteger( int32 Index, int32 Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertNumber( int32 Index, double Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertString( int32 Index, const FString& Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertValue( int32 Index, const FJsonLibraryValue& Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	Json->Insert( Value.JsonValue, Index );
	NotifyAdd( Index, Value );
}

void FJsonLibraryList::InsertObject( int32 Index, const FJsonLibraryObject& Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertList( int32 Index, const FJsonLibraryList& Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertArray( int32 Index, const TArray<FJsonLibraryValue>& Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::InsertMap( int32 Index, const TMap<FString, FJsonLibraryValue>& Value )
{
	InsertValue( Index, FJsonLibraryValue( Value ) );
}

bool FJsonLibraryList::GetBoolean( int32 Index ) const
{
	return GetValue( Index ).GetBoolean();
}

float FJsonLibraryList::GetFloat( int32 Index ) const
{
	return GetValue( Index ).GetFloat();
}

int32 FJsonLibraryList::GetInteger( int32 Index ) const
{
	return GetValue( Index ).GetInteger();
}

double FJsonLibraryList::GetNumber( int32 Index ) const
{
	return GetValue( Index ).GetNumber();
}

FString FJsonLibraryList::GetString( int32 Index ) const
{
	return GetValue( Index ).GetString();
}

FJsonLibraryValue FJsonLibraryList::GetValue( int32 Index ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json || Index < 0 || Index >= Json->Num() )
		return FJsonLibraryValue( TSharedPtr<FJsonValue>() );
	
	return FJsonLibraryValue( ( *Json )[ Index ] );
}

FJsonLibraryObject FJsonLibraryList::GetObject( int32 Index ) const
{
	return GetValue( Index ).GetObject();
}

FJsonLibraryList FJsonLibraryList::GetList( int32 Index ) const
{
	return GetValue( Index ).GetList();
}

TArray<FJsonLibraryValue> FJsonLibraryList::GetArray( int32 Index ) const
{
	return GetValue( Index ).ToArray();
}

TMap<FString, FJsonLibraryValue> FJsonLibraryList::GetMap( int32 Index ) const
{
	return GetValue( Index ).ToMap();
}

void FJsonLibraryList::SetBoolean( int32 Index, bool Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetFloat( int32 Index, float Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetInteger( int32 Index, int32 Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetNumber( int32 Index, double Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetString( int32 Index, const FString& Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetValue( int32 Index, const FJsonLibraryValue& Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json || Index < 0 || Index >= Json->Num() )
		return;

	NotifyCheck( Index );
	( *Json )[ Index ] = Value.JsonValue;
	NotifyChange( Index, Value );
}

void FJsonLibraryList::SetObject( int32 Index, const FJsonLibraryObject& Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetList( int32 Index, const FJsonLibraryList& Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetArray( int32 Index, const TArray<FJsonLibraryValue>& Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::SetMap( int32 Index, const TMap<FString, FJsonLibraryValue>& Value )
{
	SetValue( Index, FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::Remove( int32 Index )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json || Index < 0 || Index >= Json->Num() )
		return;

	NotifyCheck( Index );
	Json->RemoveAt( Index );
	NotifyRemove( Index );
}

void FJsonLibraryList::RemoveBoolean( bool Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	for ( int32 i = Json->Num() - 1; i >= 0; i-- )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::Boolean && Item->AsBool() == Value )
		{
			NotifyCheck( i );
			Json->RemoveAt( i );
			NotifyRemove( i );
		}
	}
}

void FJsonLibraryList::RemoveFloat( float Value )
{
	RemoveNumber( Value );
}

void FJsonLibraryList::RemoveInteger( int32 Value )
{
	RemoveNumber( (double)Value );
}

void FJsonLibraryList::RemoveNumber( double Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	for ( int32 i = Json->Num() - 1; i >= 0; i-- )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::Number && Item->AsNumber() == Value )
		{
			NotifyCheck( i );
			Json->RemoveAt( i );
			NotifyRemove( i );
		}
	}
}

void FJsonLibraryList::RemoveString( const FString& Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	for ( int32 i = Json->Num() - 1; i >= 0; i-- )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::String && Item->AsString() == Value )
		{
			NotifyCheck( i );
			Json->RemoveAt( i );
			NotifyRemove( i );
		}
	}
}

void FJsonLibraryList::RemoveValue( const FJsonLibraryValue& Value )
{
	TArray<TSharedPtr<FJsonValue>>* Json = SetJsonArray();
	if ( !Json )
		return;

	for ( int32 i = Json->Num() - 1; i >= 0; i-- )
	{
		if ( Value.Equals( FJsonLibraryValue( ( *Json )[ i ] ) ) )
		{
			NotifyCheck( i );
			Json->RemoveAt( i );
			NotifyRemove( i );
		}
	}
}

void FJsonLibraryList::RemoveObject( const FJsonLibraryObject& Value )
{
	RemoveValue( FJsonLibraryValue( Value ) );
}

void FJsonLibraryList::RemoveList( const FJsonLibraryList& Value )
{
	RemoveValue( FJsonLibraryValue( Value ) );
}

int32 FJsonLibraryList::FindBoolean( bool Value, int32 Index ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return -1;

	for ( int32 i = Index; i < Json->Num(); i++ )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::Boolean && Item->AsBool() == Value )
			return i;
	}

	return -1;
}

int32 FJsonLibraryList::FindFloat( float Value, int32 Index ) const
{
	return FindNumber( Value, Index );
}

int32 FJsonLibraryList::FindInteger( int32 Value, int32 Index ) const
{
	return FindNumber( (double)Value, Index );
}

int32 FJsonLibraryList::FindNumber( double Value, int32 Index ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return -1;

	for ( int32 i = Index; i < Json->Num(); i++ )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::Number && Item->AsNumber() == Value )
			return i;
	}

	return -1;
}

int32 FJsonLibraryList::FindString( const FString& Value, int32 Index ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return -1;

	for ( int32 i = Index; i < Json->Num(); i++ )
	{
		const TSharedPtr<FJsonValue>& Item = ( *Json )[ i ];
		if ( Item.IsValid() && Item->Type == EJson::String && Item->AsString() == Value )
			return i;
	}

	return -1;
}

int32 FJsonLibraryList::FindValue( const FJsonLibraryValue& Value, int32 Index ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return -1;

	for ( int32 i = Index; i < Json->Num(); i++ )
		if ( Value.Equals( FJsonLibraryValue( ( *Json )[ i ] ) ) )
			return i;

	return -1;
}

int32 FJsonLibraryList::FindObject( const FJsonLibraryObject& Value, int32 Index ) const
{
	return FindValue( FJsonLibraryValue( Value ), Index );
}

int32 FJsonLibraryList::FindList( const FJsonLibraryList& Value, int32 Index ) const
{
	return FindValue( FJsonLibraryValue( Value ), Index );
}

const TArray<TSharedPtr<FJsonValue>>* FJsonLibraryList::GetJsonArray() const
{
	if ( JsonArray.IsValid() && JsonArray->Type == EJson::Array )
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if ( JsonArray->TryGetArray( Array ) )
			return Array;
	}
	
	return nullptr;
}

TArray<TSharedPtr<FJsonValue>>* FJsonLibraryList::SetJsonArray()
{
	return const_cast<TArray<TSharedPtr<FJsonValue>>*>( GetJsonArray() );
}

bool FJsonLibraryList::TryParse( const FString& Text )
{
	if ( Text.IsEmpty() )
		return false;

	FString TrimmedText = Text;
	TrimmedText.TrimStartInline();
	TrimmedText.TrimEndInline();
	
	if ( !TrimmedText.StartsWith( "[" ) || !TrimmedText.EndsWith( "]" ) )
		return false;

	TArray<TSharedPtr<FJsonValue>> Array;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create( TrimmedText );
	if ( !FJsonSerializer::Deserialize( Reader, Array ) )
		return false;

	JsonArray = MakeShareable( new FJsonValueArray( Array ) );

	NotifyParse();
	return true;
}

bool FJsonLibraryList::TryStringify( FString& Text, bool bCondensed /*= true*/ ) const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return false;

	if ( Json->Num() <= 0 )
	{
		if ( bCondensed )
			Text = FString( "[]" );
		else
			Text = FString::Printf( TEXT( "[%s]" ), LINE_TERMINATOR );

		return true;
	}

	if ( bCondensed )
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create( &Text );
		if ( !FJsonSerializer::Serialize( *Json, Writer ) )
			return false;
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create( &Text );
		if ( !FJsonSerializer::Serialize( *Json, Writer ) )
			return false;
	}

	Text.TrimStartInline();
	Text.TrimEndInline();

	if ( !Text.StartsWith( "[" ) || !Text.EndsWith( "]" ) )
		return false;

	return true;
}

void FJsonLibraryList::NotifyAdd( int32 Index, const FJsonLibraryValue& Value )
{
	if ( !OnNotify.IsBound() )
		return;
	
	OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Added, Index, Value );

	bNotifyHasIndex = false;
	NotifyValue.Reset();
}

void FJsonLibraryList::NotifyChange( int32 Index, const FJsonLibraryValue& Value )
{
	if ( !OnNotify.IsBound() )
		return;

	if ( !Value.Equals( FJsonLibraryValue( NotifyValue ), true ) )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Changed, Index, Value );
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, Index, Value );

	bNotifyHasIndex = false;
	NotifyValue.Reset();
}

bool FJsonLibraryList::NotifyCheck()
{
	if ( !OnNotify.IsBound() )
		return false;

	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( Json )
		bNotifyHasIndex = Json->Num() > 0;
	else
		bNotifyHasIndex = false;

	NotifyValue.Reset();
	return bNotifyHasIndex;
}

bool FJsonLibraryList::NotifyCheck( int32 Index )
{
	if ( !OnNotify.IsBound() )
		return false;

	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( Json )
		bNotifyHasIndex = Index >= 0 && Index < Json->Num();
	else
		bNotifyHasIndex = false;

	NotifyValue.Reset();
	if ( bNotifyHasIndex )
		NotifyValue = ( *Json )[ Index ];

	return bNotifyHasIndex;
}

void FJsonLibraryList::NotifyClear()
{
	if ( !OnNotify.IsBound() )
		return;

	NotifyValue.Reset();
	if ( bNotifyHasIndex )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Reset, -1, FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, -1, FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );

	bNotifyHasIndex = false;
}

void FJsonLibraryList::NotifyParse()
{
	if ( !OnNotify.IsBound() )
		return;

	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return;

	NotifyValue.Reset();
	for ( int32 i = 0; i < Json->Num(); i++ )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Added, i, FJsonLibraryValue( ( *Json )[ i ] ) );

	bNotifyHasIndex = false;
}

void FJsonLibraryList::NotifyRemove( int32 Index )
{
	if ( !OnNotify.IsBound() )
		return;

	if ( bNotifyHasIndex )
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::Removed, Index, FJsonLibraryValue( NotifyValue ) );
	else
		OnNotify.Execute( FJsonLibraryValue( *this ), EJsonLibraryNotifyAction::None, Index, FJsonLibraryValue( TSharedPtr<FJsonValue>() ) );

	bNotifyHasIndex = false;
	NotifyValue.Reset();
}

bool FJsonLibraryList::IsValid() const
{
	if ( GetJsonArray() )
		return true;

	return false;
}

bool FJsonLibraryList::IsEmpty() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	if ( !Json )
		return false;

	return Json->Num() == 0;
}

FJsonLibraryList FJsonLibraryList::Parse( const FString& Text )
{
	FJsonLibraryList List = TSharedPtr<FJsonValueArray>();
	if ( !List.TryParse( Text ) )
		List.JsonArray.Reset();
	
	return List;
}

FJsonLibraryList FJsonLibraryList::Parse( const FString& Text, const FJsonLibraryListNotify& Notify )
{
	FJsonLibraryList List = Parse( Text );
	List.OnNotify = Notify;

	return List;
}

FString FJsonLibraryList::Stringify() const
{
	FString Text;
	if ( TryStringify( Text ) )
		return Text;

	return FString();
}

TArray<FJsonLibraryValue> FJsonLibraryList::ToArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();
	
	TArray<FJsonLibraryValue> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ) );
	
	return Array;
}

TArray<bool> FJsonLibraryList::ToBooleanArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<bool> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ).GetBoolean() );

	return Array;
}

TArray<float> FJsonLibraryList::ToFloatArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<float> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ).GetFloat() );

	return Array;
}

TArray<int32> FJsonLibraryList::ToIntegerArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<int32> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ).GetInteger() );

	return Array;
}

TArray<double> FJsonLibraryList::ToNumberArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<double> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ).GetNumber() );

	return Array;
}

TArray<FString> FJsonLibraryList::ToStringArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<FString> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryValue( ( *Json )[ i ] ).GetString() );

	return Array;
}

TArray<FJsonLibraryObject> FJsonLibraryList::ToObjectArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Json = GetJsonArray();

	TArray<FJsonLibraryObject> Array;
	if ( !Json )
		return Array;

	for ( int32 i = 0; i < Json->Num(); i++ )
		Array.Add( FJsonLibraryObject( ( *Json )[ i ] ) );

	return Array;
}

bool FJsonLibraryList::operator==( const FJsonLibraryList& List ) const
{
	return Equals( List );
}

bool FJsonLibraryList::operator!=( const FJsonLibraryList& List ) const
{
	return !Equals( List );
}

bool FJsonLibraryList::operator==( const FJsonLibraryValue& Value ) const
{
	return Value.Equals( FJsonLibraryValue( *this ) );
}

bool FJsonLibraryList::operator!=( const FJsonLibraryValue& Value ) const
{
	return !Value.Equals( FJsonLibraryValue( *this ) );
}
