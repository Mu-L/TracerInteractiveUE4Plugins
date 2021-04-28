// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "K2Node_JsonLibraryToStruct.h"
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

#define LOCTEXT_NAMESPACE "K2Node_JsonLibraryToStruct"

struct UK2Node_JsonLibraryToStructHelper
{
	static FName DataPinName;
	static FName FailedPinName;
};

FName UK2Node_JsonLibraryToStructHelper::FailedPinName( *LOCTEXT( "FailedPinName", "Failed" ).ToString() );
FName UK2Node_JsonLibraryToStructHelper::DataPinName( *LOCTEXT( "DataPinName", "Object" ).ToString() );

UK2Node_JsonLibraryToStruct::UK2Node_JsonLibraryToStruct( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	NodeTooltip = LOCTEXT( "NodeTooltip", "Attempts to parse a JSON object into a structure." );
}

void UK2Node_JsonLibraryToStruct::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	CreatePin( EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute );

	UEdGraphPin* SuccessPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then );
	SuccessPin->PinFriendlyName = LOCTEXT( "JsonLibraryToStruct Success Exec pin", "Success" );

	UEdGraphPin* FailedPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Exec, UK2Node_JsonLibraryToStructHelper::FailedPinName );
	FailedPin->PinFriendlyName = LOCTEXT( "JsonLibraryToStruct Failed Exec pin", "Failure" );

	UScriptStruct* JsonObjectStruct = TBaseStructure<FJsonLibraryObject>::Get();
	UEdGraphPin* DataPin = CreatePin( EGPD_Input, UEdGraphSchema_K2::PC_Struct, JsonObjectStruct, UK2Node_JsonLibraryToStructHelper::DataPinName );
	SetPinToolTip( *DataPin, LOCTEXT( "DataPinDescription", "The JSON object to convert." ) );

	UEdGraphPin* ResultPin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, UEdGraphSchema_K2::PN_ReturnValue );
  //ResultPin->bDisplayAsMutableRef = true;
	ResultPin->PinFriendlyName = LOCTEXT( "JsonLibraryToStruct Out Struct", "Structure" );
	SetPinToolTip( *ResultPin, LOCTEXT( "ResultPinDescription", "The returned structure, if converted." ) );

	Super::AllocateDefaultPins();
}

void UK2Node_JsonLibraryToStruct::SetPinToolTip( UEdGraphPin& MutatablePin, const FText& PinDescription ) const
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

void UK2Node_JsonLibraryToStruct::RefreshOutputPinType()
{
	UEdGraphPin* ResultPin = GetResultPin();
	const bool bFillTypeFromConnected = ResultPin && ( ResultPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard );

	UScriptStruct* OutputType = nullptr;
	if ( bFillTypeFromConnected )
	{
		FEdGraphPinType PinType = ResultPin->PinType;
		if ( ResultPin->LinkedTo.Num() > 0 )
			PinType = ResultPin->LinkedTo[ 0 ]->PinType;

		if ( PinType.PinCategory == UEdGraphSchema_K2::PC_Struct )
			OutputType = Cast<UScriptStruct>( PinType.PinSubCategoryObject.Get() );
	}

	SetReturnTypeForStruct( OutputType );
}

void UK2Node_JsonLibraryToStruct::SetReturnTypeForStruct( UScriptStruct* StructType )
{
	if ( StructType == GetReturnTypeForStruct() )
		return;

	UEdGraphPin* ResultPin = GetResultPin();
	if ( ResultPin->SubPins.Num() > 0 )
		GetSchema()->RecombinePin( ResultPin );
		
	ResultPin->PinType.PinSubCategoryObject = StructType;
	ResultPin->PinType.PinCategory = StructType ?
									 UEdGraphSchema_K2::PC_Struct :
									 UEdGraphSchema_K2::PC_Wildcard;

	CachedNodeTitle.Clear();
}

UScriptStruct* UK2Node_JsonLibraryToStruct::GetReturnTypeForStruct() const
{
	UScriptStruct* ReturnStructType = (UScriptStruct*)( GetResultPin()->PinType.PinSubCategoryObject.Get() );
	return ReturnStructType;
}

void UK2Node_JsonLibraryToStruct::GetMenuActions( FBlueprintActionDatabaseRegistrar& ActionRegistrar ) const
{
	UClass* ActionKey = GetClass();
	if ( ActionRegistrar.IsOpenForRegistration( ActionKey ) )
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create( GetClass() );
		check( NodeSpawner != nullptr );

		ActionRegistrar.AddBlueprintAction( ActionKey, NodeSpawner );
	}
}

FText UK2Node_JsonLibraryToStruct::GetMenuCategory() const
{
	return FText::FromString( TEXT( "JSON Library|Structure" ) );
}

bool UK2Node_JsonLibraryToStruct::IsConnectionDisallowed( const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason ) const
{
	if ( MyPin == GetResultPin() && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard )
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

FText UK2Node_JsonLibraryToStruct::GetTooltipText() const
{
	return NodeTooltip;
}

UEdGraphPin* UK2Node_JsonLibraryToStruct::GetThenPin()const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked( UEdGraphSchema_K2::PN_Then );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryToStruct::GetDataPin() const
{
	UEdGraphPin* Pin = FindPinChecked( UK2Node_JsonLibraryToStructHelper::DataPinName );
	check( Pin->Direction == EGPD_Input );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryToStruct::GetFailedPin() const
{
	UEdGraphPin* Pin = FindPinChecked( UK2Node_JsonLibraryToStructHelper::FailedPinName );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

UEdGraphPin* UK2Node_JsonLibraryToStruct::GetResultPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	check( Pin->Direction == EGPD_Output );
	return Pin;
}

FText UK2Node_JsonLibraryToStruct::GetNodeTitle( ENodeTitleType::Type TitleType ) const
{
	if ( TitleType == ENodeTitleType::MenuTitle )
		return LOCTEXT( "ListViewTitle", "JSON to Structure" );
	
	if ( UEdGraphPin* ResultPin = GetResultPin() )
	{
		UScriptStruct* StructType = GetReturnTypeForStruct();
		if ( !StructType || ResultPin->LinkedTo.Num() == 0 )
			return NSLOCTEXT( "K2Node", "JsonToStruct_Title_None", "JSON to Structure" );

		if ( CachedNodeTitle.IsOutOfDate( this ) )
		{
			FFormatNamedArguments Args;
			Args.Add( TEXT( "StructName" ), FText::FromName( StructType->GetFName() ) );
			
			FText LocFormat = NSLOCTEXT( "K2Node", "JsonToStruct", "JSON to {StructName}" );
			CachedNodeTitle.SetCachedText( FText::Format( LocFormat, Args ), this );
		}
	}
	else
		return NSLOCTEXT( "K2Node", "JsonToStruct_Title_None", "JSON to Structure" );
	
	return CachedNodeTitle;
}

void UK2Node_JsonLibraryToStruct::ExpandNode( FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph )
{
	Super::ExpandNode( CompilerContext, SourceGraph );
	
	const FName StructFromJsonFunctionName = GET_FUNCTION_NAME_CHECKED( UJsonLibraryBlueprintHelpers, StructFromJson );
	UK2Node_CallFunction* CallStructFromJsonFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>( this, SourceGraph );

	CallStructFromJsonFunction->FunctionReference.SetExternalMember( StructFromJsonFunctionName, UJsonLibraryBlueprintHelpers::StaticClass() );
	CallStructFromJsonFunction->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate( *GetExecPin(), *( CallStructFromJsonFunction->GetExecPin() ) );

	UScriptStruct* StructType = GetReturnTypeForStruct();
	UUserDefinedStruct* UserStructType = Cast<UUserDefinedStruct>( StructType );

	UEdGraphPin* StructTypePin = CallStructFromJsonFunction->FindPinChecked( TEXT( "StructType" ) );
	if ( UserStructType && UserStructType->PrimaryStruct.IsValid() )
		StructTypePin->DefaultObject = UserStructType->PrimaryStruct.Get();
	else
		StructTypePin->DefaultObject = StructType;

	UEdGraphPin* DataInPin = CallStructFromJsonFunction->FindPinChecked( TEXT( "Object" ) );
	CompilerContext.MovePinLinksToIntermediate( *GetDataPin(), *DataInPin );

	UEdGraphPin* OriginalReturnPin    = FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	UEdGraphPin* FunctionOutStructPin = CallStructFromJsonFunction->FindPinChecked( TEXT( "OutStruct" ) );
	UEdGraphPin* FunctionReturnPin    = CallStructFromJsonFunction->FindPinChecked( UEdGraphSchema_K2::PN_ReturnValue );
	UEdGraphPin* FunctionThenPin      = CallStructFromJsonFunction->GetThenPin();
	
	FunctionOutStructPin->PinType                      = OriginalReturnPin->PinType;
	FunctionOutStructPin->PinType.PinSubCategoryObject = OriginalReturnPin->PinType.PinSubCategoryObject;

	UK2Node_IfThenElse* BranchNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>( this, SourceGraph );
	BranchNode->AllocateDefaultPins();

	FunctionThenPin->MakeLinkTo( BranchNode->GetExecPin() );
	FunctionReturnPin->MakeLinkTo( BranchNode->GetConditionPin() );
	
	CompilerContext.MovePinLinksToIntermediate( *GetThenPin(), *( BranchNode->GetThenPin() ) );
	CompilerContext.MovePinLinksToIntermediate( *GetFailedPin(), *( BranchNode->GetElsePin() ) );
	CompilerContext.MovePinLinksToIntermediate( *OriginalReturnPin, *FunctionOutStructPin );

	BreakAllNodeLinks();
}

FSlateIcon UK2Node_JsonLibraryToStruct::GetIconAndTint( FLinearColor& OutColor ) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon( "EditorStyle", "Kismet.AllClasses.FunctionIcon" );
	return Icon;
}

void UK2Node_JsonLibraryToStruct::PostReconstructNode()
{
	Super::PostReconstructNode();
	RefreshOutputPinType();
}

void UK2Node_JsonLibraryToStruct::EarlyValidation( FCompilerResultsLog& MessageLog ) const
{
	Super::EarlyValidation( MessageLog );
	if ( UEdGraphPin* ResultPin = GetResultPin() )
	{
		if ( ResultPin->LinkedTo.Num() == 0 )
		{
			MessageLog.Error( *LOCTEXT( "MissingPins", "Missing pins in @@" ).ToString(), this );
			return;
		}
	}
}

void UK2Node_JsonLibraryToStruct::NotifyPinConnectionListChanged( UEdGraphPin* Pin )
{
	Super::NotifyPinConnectionListChanged( Pin );
	if ( Pin == GetResultPin() )
		RefreshOutputPinType();
}

#undef LOCTEXT_NAMESPACE
