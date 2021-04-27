// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "K2Node_JsonLibraryFromStruct.h"
#include "Engine/UserDefinedStruct.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "JsonLibraryObject.h"
#include "JsonLibraryBlueprintHelpers.h"

#define LOCTEXT_NAMESPACE "K2Node_JsonLibraryFromStruct"

struct UK2Node_JsonLibraryFromStructHelper
{
	static FName DataPinName;
	static FName FailedPinName;
};

FName UK2Node_JsonLibraryFromStructHelper::FailedPinName( *LOCTEXT( "FailedPinName", "Failed" ).ToString() );
FName UK2Node_JsonLibraryFromStructHelper::DataPinName( *LOCTEXT( "DataPinName", "Structure" ).ToString() );

UK2Node_JsonLibraryFromStruct::UK2Node_JsonLibraryFromStruct( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	NodeTooltip = LOCTEXT( "NodeTooltip", "Attempts to parse a JSON object into a structure." );
}

void UK2Node_JsonLibraryFromStruct::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	CreatePin( EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute );

	UEdGraphPin* SuccessPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then );
	SuccessPin->PinFriendlyName = LOCTEXT( "JsonLibraryFromStruct Success Exec pin", "Success" );

	UEdGraphPin* FailedPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Exec, UK2Node_JsonLibraryFromStructHelper::FailedPinName );
	FailedPin->PinFriendlyName = LOCTEXT( "JsonLibraryFromStruct Failed Exec pin", "Failure" );

	UEdGraphPin* DataPin = CreatePin( EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, UK2Node_JsonLibraryFromStructHelper::DataPinName );
	DataPin->bDisplayAsMutableRef = true;
	SetPinToolTip( *DataPin, LOCTEXT( "DataPinDescription", "The structure to convert." ) );

	UScriptStruct* JsonObjectStruct = TBaseStructure<FJsonLibraryObject>::Get();
	UEdGraphPin* ResultPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Struct, JsonObjectStruct, UEdGraphSchema_K2::PN_ReturnValue );
	ResultPin->PinFriendlyName = LOCTEXT( "JsonLibraryFromStruct Out Json", "Object" );
	SetPinToolTip( *ResultPin, LOCTEXT( "ResultPinDescription", "The returned JSON object, if converted." ) );

	Super::AllocateDefaultPins();
}

void UK2Node_JsonLibraryFromStruct::SetPinToolTip( UEdGraphPin& MutatablePin, const FText& PinDescription ) const
{
	MutatablePin.PinToolTip = UEdGraphSchema_K2::TypeToText( MutatablePin.PinType ).ToString();

	UEdGraphSchema_K2 const* const K2Schema = Cast<const UEdGraphSchema_K2>( GetSchema() );
	if ( K2Schema )
	{
		MutatablePin.PinToolTip += TEXT( " " );
		MutatablePin.PinToolTip += K2Schema->GetPinDisplayName( &MutatablePin ).ToString();
	}

	MutatablePin.PinToolTip += FString( TEXT( "\n" ) ) + PinDescription.ToString();
}

void UK2Node_JsonLibraryFromStruct::RefreshInputPinType()
{
	UEdGraphPin* DataPin = GetDataPin();
	const bool bFillTypeFromConnected = DataPin && ( DataPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard );

	UScriptStruct* InputType = nullptr;
	if ( bFillTypeFromConnected )
	{
		FEdGraphPinType PinType = DataPin->PinType;
		if ( DataPin->LinkedTo.Num() > 0 )
			PinType = DataPin->LinkedTo[ 0 ]->PinType;

		if ( PinType.PinCategory == UEdGraphSchema_K2::PC_Struct )
			InputType = Cast<UScriptStruct>( PinType.PinSubCategoryObject.Get() );
	}

	SetPropertyTypeForStruct( InputType );
}

void UK2Node_JsonLibraryFromStruct::SetPropertyTypeForStruct( UScriptStruct* StructType )
{
	if ( StructType == GetPropertyTypeForStruct() )
		return;

	UEdGraphPin* DataPin = GetDataPin();
	if ( DataPin->SubPins.Num() > 0 )
		GetSchema()->RecombinePin( DataPin );
		
	DataPin->PinType.PinSubCategoryObject = StructType;
	DataPin->PinType.PinCategory = StructType ?
								   UEdGraphSchema_K2::PC_Struct :
								   UEdGraphSchema_K2::PC_Wildcard;

	CachedNodeTitle.Clear();
}

UScriptStruct* UK2Node_JsonLibraryFromStruct::GetPropertyTypeForStruct() const
{
	UScriptStruct* DataStructType = (UScriptStruct*)( GetDataPin()->PinType.PinSubCategoryObject.Get() );
	return DataStructType;
}

void UK2Node_JsonLibraryFromStruct::GetMenuActions( FBlueprintActionDatabaseRegistrar& ActionRegistrar ) const
{
	UClass* ActionKey = GetClass();
	if ( ActionRegistrar.IsOpenForRegistration( ActionKey ) )
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create( GetClass() );
		check( NodeSpawner != nullptr );

		ActionRegistrar.AddBlueprintAction( ActionKey, NodeSpawner );
	}
}

FText UK2Node_JsonLibraryFromStruct::GetMenuCategory() const
{
	return FText::FromString( TEXT( "JSON Library|Structure" ) );
}

bool UK2Node_JsonLibraryFromStruct::IsConnectionDisallowed( const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason ) const
{
	if ( MyPin == GetDataPin() && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard )
	{
		bool bDisallowed = true;
		if ( OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct )
		{
			if ( UScriptStruct* ConnectionType = Cast<UScriptStruct>( OtherPin->PinType.PinSubCategoryObject.Get() ) )
				bDisallowed = false;
		}
		else if ( OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard )
			bDisallowed = false;

		if ( bDisallowed )
			OutReason = TEXT( "Must be a structure." );

		return bDisallowed;
	}

	return false;
}

FText UK2Node_JsonLibraryFromStruct::GetTooltipText() const
{
	return NodeTooltip;
}

UEdGraphPin* UK2Node_JsonLibraryFromStruct::GetThenPin()const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked( UEdGraphSchema_K2::PN_Then );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryFromStruct::GetDataPin() const
{
	UEdGraphPin* Pin = FindPinChecked( UK2Node_JsonLibraryFromStructHelper::DataPinName );
	check( Pin->Direction == EGPD_Input );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryFromStruct::GetFailedPin() const
{
	UEdGraphPin* Pin = FindPinChecked( UK2Node_JsonLibraryFromStructHelper::FailedPinName );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryFromStruct::GetResultPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

FText UK2Node_JsonLibraryFromStruct::GetNodeTitle( ENodeTitleType::Type TitleType ) const
{
	if ( TitleType == ENodeTitleType::MenuTitle )
		return LOCTEXT( "ListViewTitle", "Structure to JSON" );
	
	if ( UEdGraphPin* DataPin = GetDataPin() )
	{
		UScriptStruct* StructType = GetPropertyTypeForStruct();
		if ( !StructType || DataPin->LinkedTo.Num() == 0 )
			return NSLOCTEXT( "K2Node", "JsonFromStruct_Title_None", "Structure to JSON" );

		if ( CachedNodeTitle.IsOutOfDate( this ) )
		{
			FFormatNamedArguments Args;
			Args.Add( TEXT( "StructName" ), FText::FromName( StructType->GetFName() ) );
			
			FText LocFormat = NSLOCTEXT( "K2Node", "JsonFromStruct", "{StructName} to JSON" );
			CachedNodeTitle.SetCachedText( FText::Format( LocFormat, Args ), this );
		}
	}
	else
		return NSLOCTEXT( "K2Node", "JsonFromStruct_Title_None", "Structure to JSON" );
	
	return CachedNodeTitle;
}

void UK2Node_JsonLibraryFromStruct::ExpandNode( FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph )
{
	Super::ExpandNode( CompilerContext, SourceGraph );
	
	const FName StructToJsonFunctionName = GET_FUNCTION_NAME_CHECKED( UJsonLibraryBlueprintHelpers, StructToJson );
	UK2Node_CallFunction* CallStructToJsonFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>( this, SourceGraph );

	CallStructToJsonFunction->FunctionReference.SetExternalMember( StructToJsonFunctionName, UJsonLibraryBlueprintHelpers::StaticClass() );
	CallStructToJsonFunction->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate( *GetExecPin(), *( CallStructToJsonFunction->GetExecPin() ) );
	
	UScriptStruct* StructType = GetPropertyTypeForStruct();
	UUserDefinedStruct* UserStructType = Cast<UUserDefinedStruct>( StructType );
	
	UEdGraphPin* StructTypePin = CallStructToJsonFunction->FindPinChecked( TEXT( "StructType" ) );
	if ( UserStructType && UserStructType->PrimaryStruct.IsValid() )
		StructTypePin->DefaultObject = UserStructType->PrimaryStruct.Get();
	else
		StructTypePin->DefaultObject = StructType;

	UEdGraphPin* OriginalDataPin = GetDataPin();
	UEdGraphPin* StructInPin     = CallStructToJsonFunction->FindPinChecked( TEXT( "Struct" ) );

	StructInPin->PinType                      = OriginalDataPin->PinType;
	StructInPin->PinType.PinSubCategoryObject = OriginalDataPin->PinType.PinSubCategoryObject;

	CompilerContext.MovePinLinksToIntermediate( *OriginalDataPin, *StructInPin );

	UEdGraphPin* OriginalReturnPin = FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	UEdGraphPin* FunctionReturnPin = CallStructToJsonFunction->FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	UEdGraphPin* FunctionThenPin   = CallStructToJsonFunction->GetThenPin();

	const FName IsValidObjectFunctionName = GET_FUNCTION_NAME_CHECKED( UJsonLibraryBlueprintHelpers, IsValidObject );
	UK2Node_CallFunction* CallIsValidObjectFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>( this, SourceGraph );

	CallIsValidObjectFunction->FunctionReference.SetExternalMember( IsValidObjectFunctionName, UJsonLibraryBlueprintHelpers::StaticClass() );
	CallIsValidObjectFunction->bIsPureFunc = true;
	CallIsValidObjectFunction->AllocateDefaultPins();

	UEdGraphPin* ObjectInPin   = CallIsValidObjectFunction->FindPinChecked( TEXT( "Object" ) );
	UEdGraphPin* CallReturnPin = CallIsValidObjectFunction->FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );

	FunctionReturnPin->MakeLinkTo( ObjectInPin );

	UK2Node_IfThenElse* BranchNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>( this, SourceGraph );
	BranchNode->AllocateDefaultPins();

	FunctionThenPin->MakeLinkTo( BranchNode->GetExecPin() );
	CallReturnPin->MakeLinkTo( BranchNode->GetConditionPin() );

	CompilerContext.MovePinLinksToIntermediate( *GetThenPin(), *( BranchNode->GetThenPin() ) );
	CompilerContext.MovePinLinksToIntermediate( *GetFailedPin(), *( BranchNode->GetElsePin() ) );
	CompilerContext.MovePinLinksToIntermediate( *OriginalReturnPin, *FunctionReturnPin );

	BreakAllNodeLinks();
}

FSlateIcon UK2Node_JsonLibraryFromStruct::GetIconAndTint( FLinearColor& OutColor ) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon( "EditorStyle", "Kismet.AllClasses.FunctionIcon" );
	return Icon;
}

void UK2Node_JsonLibraryFromStruct::PostReconstructNode()
{
	Super::PostReconstructNode();
	RefreshInputPinType();
}

void UK2Node_JsonLibraryFromStruct::EarlyValidation( FCompilerResultsLog& MessageLog ) const
{
	Super::EarlyValidation( MessageLog );
	if ( UEdGraphPin* DataPin = GetDataPin() )
	{
		if ( DataPin->LinkedTo.Num() == 0 )
		{
			MessageLog.Error( *LOCTEXT( "MissingPins", "Missing pins in @@" ).ToString(), this );
			return;
		}
	}
}

void UK2Node_JsonLibraryFromStruct::NotifyPinConnectionListChanged( UEdGraphPin* Pin )
{
	Super::NotifyPinConnectionListChanged( Pin );
	if ( Pin == GetDataPin() )
		RefreshInputPinType();
}

#undef LOCTEXT_NAMESPACE
