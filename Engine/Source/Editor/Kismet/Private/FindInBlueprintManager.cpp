// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FindInBlueprintManager.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Types/SlateEnums.h"
#include "Classes/EditorStyleSettings.h"
#include "Engine/Level.h"
#include "Components/ActorComponent.h"
#include "AssetData.h"
#include "EdGraph/EdGraphSchema.h"
#include "ISourceControlModule.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "FileHelpers.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "EditorStyleSet.h"
#include "BlueprintEditorSettings.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "ImaginaryBlueprintData.h"
#include "FiBSearchInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/HotReloadInterface.h"
#include "BlueprintAssetHandler.h"

#include "JsonObjectConverter.h"
#include "UObject/EditorObjectVersion.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FindInBlueprintManager"

FFindInBlueprintSearchManager* FFindInBlueprintSearchManager::Instance = NULL;

const FText FFindInBlueprintSearchTags::FiB_Properties = LOCTEXT("Properties", "Properties");

const FText FFindInBlueprintSearchTags::FiB_Components = LOCTEXT("Components", "Components");
const FText FFindInBlueprintSearchTags::FiB_IsSCSComponent = LOCTEXT("IsSCSComponent", "IsSCSComponent");

const FText FFindInBlueprintSearchTags::FiB_Nodes = LOCTEXT("Nodes", "Nodes");

const FText FFindInBlueprintSearchTags::FiB_SchemaName = LOCTEXT("SchemaName", "SchemaName");

const FText FFindInBlueprintSearchTags::FiB_UberGraphs = LOCTEXT("Uber", "Uber");
const FText FFindInBlueprintSearchTags::FiB_Functions = LOCTEXT("Functions", "Functions");
const FText FFindInBlueprintSearchTags::FiB_Macros = LOCTEXT("Macros", "Macros");
const FText FFindInBlueprintSearchTags::FiB_SubGraphs = LOCTEXT("Sub", "Sub");

const FText FFindInBlueprintSearchTags::FiB_Name = LOCTEXT("Name", "Name");
const FText FFindInBlueprintSearchTags::FiB_NativeName = LOCTEXT("NativeName", "Native Name");
const FText FFindInBlueprintSearchTags::FiB_ClassName = LOCTEXT("ClassName", "ClassName");
const FText FFindInBlueprintSearchTags::FiB_NodeGuid = LOCTEXT("NodeGuid", "NodeGuid");
const FText FFindInBlueprintSearchTags::FiB_Tooltip = LOCTEXT("Tooltip", "Tooltip");
const FText FFindInBlueprintSearchTags::FiB_DefaultValue = LOCTEXT("DefaultValue", "DefaultValue");
const FText FFindInBlueprintSearchTags::FiB_Description = LOCTEXT("Description", "Description");
const FText FFindInBlueprintSearchTags::FiB_Comment = LOCTEXT("Comment", "Comment");
const FText FFindInBlueprintSearchTags::FiB_Path = LOCTEXT("Path", "Path");
const FText FFindInBlueprintSearchTags::FiB_ParentClass = LOCTEXT("ParentClass", "ParentClass");
const FText FFindInBlueprintSearchTags::FiB_Interfaces = LOCTEXT("Interfaces", "Interfaces");

const FText FFindInBlueprintSearchTags::FiB_Pins = LOCTEXT("Pins", "Pins");
const FText FFindInBlueprintSearchTags::FiB_PinCategory = LOCTEXT("PinCategory", "PinCategory");
const FText FFindInBlueprintSearchTags::FiB_PinSubCategory = LOCTEXT("SubCategory", "SubCategory");
const FText FFindInBlueprintSearchTags::FiB_ObjectClass = LOCTEXT("ObjectClass", "ObjectClass");
const FText FFindInBlueprintSearchTags::FiB_IsArray = LOCTEXT("IsArray", "IsArray");
const FText FFindInBlueprintSearchTags::FiB_IsReference = LOCTEXT("IsReference", "IsReference");
const FText FFindInBlueprintSearchTags::FiB_Glyph = LOCTEXT("Glyph", "Glyph");
const FText FFindInBlueprintSearchTags::FiB_GlyphStyleSet = LOCTEXT("GlyphStyleSet", "GlyphStyleSet");
const FText FFindInBlueprintSearchTags::FiB_GlyphColor = LOCTEXT("GlyphColor", "GlyphColor");

const FText FFindInBlueprintSearchTags::FiBMetaDataTag = LOCTEXT("FiBMetaDataTag", "!!FiBMD");

const FString FFiBMD::FiBSearchableMD = TEXT("BlueprintSearchable");
const FString FFiBMD::FiBSearchableShallowMD = TEXT("BlueprintSearchableShallow");
const FString FFiBMD::FiBSearchableExplicitMD = TEXT("BlueprintSearchableExplicit");
const FString FFiBMD::FiBSearchableHiddenExplicitMD = TEXT("BlueprintSearchableHiddenExplicit");

/* Return the outer of the specified object that is a direct child of a package */
inline UObject* GetAssetObject(UObject* InObject)
{
	UObject* AssetObject = InObject;
	while (AssetObject && !AssetObject->GetOuter()->IsA<UPackage>())
	{
		AssetObject = AssetObject->GetOuter();
	}
	return AssetObject;
}

////////////////////////////////////
// FSearchDataVersionInfo
FSearchDataVersionInfo FSearchDataVersionInfo::Current =
{
	EFiBVersion::FIB_VER_LATEST,
	FEditorObjectVersion::LatestVersion
};

////////////////////////////////////
// FStreamSearch
FStreamSearch::FStreamSearch(const FString& InSearchValue)
	: SearchValue(InSearchValue)
	, bThreadCompleted(false)
	, StopTaskCounter(0)
	, MinimiumVersionRequirement(EFiBVersion::FIB_VER_LATEST)
	, BlueprintCountBelowVersion(0)
	, ImaginaryDataFilter(ESearchQueryFilter::AllFilter)
{
	// Add on a Guid to the thread name to ensure the thread is uniquely named.
	Thread = FRunnableThread::Create( this, *FString::Printf(TEXT("FStreamSearch%s"), *FGuid::NewGuid().ToString()), 0, TPri_BelowNormal );
}

FStreamSearch::FStreamSearch(const FString& InSearchValue, ESearchQueryFilter InImaginaryDataFilter, EFiBVersion InMinimiumVersionRequirement)
	: SearchValue(InSearchValue)
	, bThreadCompleted(false)
	, StopTaskCounter(0)
	, MinimiumVersionRequirement(InMinimiumVersionRequirement)
	, BlueprintCountBelowVersion(0)
	, ImaginaryDataFilter(InImaginaryDataFilter)
{
	// Add on a Guid to the thread name to ensure the thread is uniquely named.
	Thread = FRunnableThread::Create( this, *FString::Printf(TEXT("FStreamSearch%s"), *FGuid::NewGuid().ToString()), 0, TPri_BelowNormal );
}

bool FStreamSearch::Init()
{
	return true;
}

uint32 FStreamSearch::Run()
{
	FFindInBlueprintSearchManager::Get().BeginSearchQuery(this);

	TFunction<void(const FSearchResult&)> OnResultReady = [this](const FSearchResult& Result) {
		FScopeLock ScopeLock(&SearchCriticalSection);
		ItemsFound.Add(Result);
	};

	// Searching comes to an end if it is requested using the StopTaskCounter or continuing the search query yields no results
	FSearchData QueryResult;
	while (FFindInBlueprintSearchManager::Get().ContinueSearchQuery(this, QueryResult))
	{
		if (QueryResult.ImaginaryBlueprint.IsValid())
		{
			// If the Blueprint is below the version, add it to a list. The search will still proceed on this Blueprint
			if (QueryResult.VersionInfo.FiBDataVersion < MinimiumVersionRequirement)
			{
				++BlueprintCountBelowVersion;
			}

			TSharedPtr< FFiBSearchInstance > SearchInstance(new FFiBSearchInstance);
			FSearchResult SearchResult;
			if (ImaginaryDataFilter != ESearchQueryFilter::AllFilter)
			{
				SearchInstance->MakeSearchQuery(*SearchValue, QueryResult.ImaginaryBlueprint);
				SearchInstance->CreateFilteredResultsListFromTree(ImaginaryDataFilter, FilteredImaginaryResults);
				SearchResult = SearchInstance->GetSearchResults(QueryResult.ImaginaryBlueprint);
			}
			else
			{
				SearchResult = SearchInstance->StartSearchQuery(*SearchValue, QueryResult.ImaginaryBlueprint);
			}

			// If there are children, add the item to the search results
			if(SearchResult.IsValid() && SearchResult->Children.Num() != 0)
			{
				OnResultReady(SearchResult);
			}
		}

		if (StopTaskCounter.GetValue())
		{
			// Ensure that the FiB Manager knows that we are done searching
			FFindInBlueprintSearchManager::Get().EnsureSearchQueryEnds(this);
		}
	}

	bThreadCompleted = true;

	return 0;
}

void FStreamSearch::Stop()
{
	StopTaskCounter.Increment();
}

void FStreamSearch::Exit()
{

}

void FStreamSearch::EnsureCompletion()
{
	{
		FScopeLock CritSectionLock(&SearchCriticalSection);
		ItemsFound.Empty();
	}

	Stop();
	Thread->WaitForCompletion();
	delete Thread;
	Thread = NULL;
}

bool FStreamSearch::IsComplete() const
{
	return bThreadCompleted;
}

void FStreamSearch::GetFilteredItems(TArray<FSearchResult>& OutItemsFound)
{
	FScopeLock ScopeLock(&SearchCriticalSection);
	OutItemsFound.Append(ItemsFound);
	ItemsFound.Empty();
}

float FStreamSearch::GetPercentComplete() const
{
	return FFindInBlueprintSearchManager::Get().GetPercentComplete(this);
}

void FStreamSearch::GetFilteredImaginaryResults(TArray<FImaginaryFiBDataSharedPtr>& OutFilteredImaginaryResults)
{
	OutFilteredImaginaryResults = MoveTemp(FilteredImaginaryResults);
}

/** Temporarily forces all nodes and pins to use non-friendly names, forces all schema to have nodes clear their cached values so they will re-cache, and then reverts at the end */
struct FTemporarilyUseFriendlyNodeTitles
{
	FTemporarilyUseFriendlyNodeTitles()
	{
		UEditorStyleSettings* EditorSettings = GetMutableDefault<UEditorStyleSettings>();

		// Cache the value of bShowFriendlyNames, we will force it to true for gathering BP search data and then restore it
		bCacheShowFriendlyNames = EditorSettings->bShowFriendlyNames;

		EditorSettings->bShowFriendlyNames = true;
		ForceVisualizationCacheClear();
	}

	~FTemporarilyUseFriendlyNodeTitles()
	{
		UEditorStyleSettings* EditorSettings = GetMutableDefault<UEditorStyleSettings>();
		EditorSettings->bShowFriendlyNames = bCacheShowFriendlyNames;
		ForceVisualizationCacheClear();
	}

	/** Go through all Schemas and force a visualization cache clear, forcing nodes to refresh their titles */
	void ForceVisualizationCacheClear()
	{
		// Only do the purge if the state was changed
		if (!bCacheShowFriendlyNames)
		{
			// Find all Schemas and force a visualization cache clear
			for ( TObjectIterator<UEdGraphSchema> SchemaIt(RF_NoFlags); SchemaIt; ++SchemaIt)
			{
				SchemaIt->ForceVisualizationCacheClear();
			}
		}
	}

private:
	/** Cached state of ShowFriendlyNames in EditorSettings */
	bool bCacheShowFriendlyNames;
};

/** Helper functions for serialization of types to and from an FString */
namespace FiBSerializationHelpers
{
	/**
	* Helper function to handle properly encoding and serialization of a type into an FString
	*
	* @param InValue				Value to serialize
	* @param bInIncludeSize		If true, include the size of the type. This will place an int32
									before the value in the FString. This is needed for non-basic types
									because everything is stored in an FString and is impossible to distinguish
	*/
	template<class Type>
	const FString Serialize(Type& InValue, bool bInIncludeSize)
	{
		TArray<uint8> SerializedData;
		FMemoryWriter Ar(SerializedData);

		Ar << InValue;
		Ar.Close();
		FString Result = BytesToString(SerializedData.GetData(), SerializedData.Num());

		// If the size is included, prepend it onto the Result string.
		if(bInIncludeSize)
		{
			SerializedData.Empty();
			FMemoryWriter ArWithLength(SerializedData);
			int32 Length = Result.Len();
			ArWithLength << Length;

			Result = BytesToString(SerializedData.GetData(), SerializedData.Num()) + Result;
		}
		return Result;
	}

	/** Helper function to handle properly decoding of uint8 arrays so they can be deserialized as their respective types */
	void DecodeFromStream(FBufferReader& InStream, int32 InBytes, TArray<uint8>& OutDerivedData)
	{
		// Read, as a byte string, the number of characters composing the Lookup Table for the Json.
		FString SizeOfDataAsHex;
		SizeOfDataAsHex.GetCharArray().AddUninitialized(InBytes + 1);
		SizeOfDataAsHex.GetCharArray()[InBytes] = TEXT('\0');
		InStream.Serialize((char*)SizeOfDataAsHex.GetCharArray().GetData(), sizeof(TCHAR) * InBytes);

		// Convert the number (which is stored in 1 serialized byte per TChar) into an int32
		OutDerivedData.Empty();
		OutDerivedData.AddUninitialized(InBytes);
		StringToBytes(SizeOfDataAsHex, OutDerivedData.GetData(), InBytes);
	}

	/** Helper function to deserialize from a Stream the sizeof the templated type */
	template<class Type>
	Type Deserialize(FBufferReader& InStream)
	{
		TArray<uint8> DerivedData;
		DecodeFromStream(InStream, sizeof(Type), DerivedData);

		FMemoryReader SizeOfDataAr(DerivedData);
		SizeOfDataAr.SetCustomVersions(InStream.GetCustomVersions());

		Type ReturnValue;
		SizeOfDataAr << ReturnValue;
		return ReturnValue;
	}

	/** Helper function to deserialize from a Stream a certain number of bytes */
	template<class Type>
	Type Deserialize(FBufferReader& InStream, int32 InBytes)
	{
		TArray<uint8> DerivedData;
		DecodeFromStream(InStream, InBytes, DerivedData);

		FMemoryReader SizeOfDataAr(DerivedData);
		SizeOfDataAr.SetCustomVersions(InStream.GetCustomVersions());

		Type ReturnValue;
		SizeOfDataAr << ReturnValue;
		return ReturnValue;
	}
}

namespace BlueprintSearchMetaDataHelpers
{
	/** Cache structure of searchable metadata and sub-properties relating to a Property */
	struct FSearchableProperty
	{
		UProperty* TargetProperty;
		bool bIsSearchableMD;
		bool bIsShallowSearchableMD;
		bool bIsMarkedNotSearchableMD;
		TArray<FSearchableProperty> ChildProperties;
	};

	/** Json Writer used for serializing FText's in the correct format for Find-in-Blueprints */
	template < class PrintPolicy = TPrettyJsonPrintPolicy<TCHAR> >
	class TJsonFindInBlueprintStringWriter : public TJsonStringWriter<PrintPolicy>
	{
	public:
		static TSharedRef< TJsonFindInBlueprintStringWriter > Create( FString* const InStream )
		{
			return MakeShareable( new TJsonFindInBlueprintStringWriter( InStream ) );
		}

		using TJsonStringWriter<PrintPolicy>::WriteObjectStart;

		void WriteObjectStart( const FText& Identifier )
		{
			check( this->Stack.Top() == EJson::Object );
			WriteIdentifier( Identifier );

			PrintPolicy::WriteLineTerminator(this->Stream);
			PrintPolicy::WriteTabs(this->Stream, this->IndentLevel);
			PrintPolicy::WriteChar(this->Stream, TCHAR('{'));
			++(this->IndentLevel);
			this->Stack.Push( EJson::Object );
			this->PreviousTokenWritten = EJsonToken::CurlyOpen;
		}

		void WriteArrayStart( const FText& Identifier )
		{
			check( this->Stack.Top() == EJson::Object );
			WriteIdentifier( Identifier );

			PrintPolicy::WriteSpace( this->Stream );
			PrintPolicy::WriteChar(this->Stream, TCHAR('['));
			++(this->IndentLevel);
			this->Stack.Push( EJson::Array );
			this->PreviousTokenWritten = EJsonToken::SquareOpen;
		}

		using TJsonStringWriter<PrintPolicy>::WriteValueOnly;

		EJsonToken WriteValueOnly(const FText& Value)
		{
			WriteTextValue(Value);
			return EJsonToken::String;
		}

		template <class FValue>
		void WriteValue( const FText& Identifier, FValue Value )
		{
			check( this->Stack.Top() == EJson::Object );
			WriteIdentifier( Identifier );

			PrintPolicy::WriteSpace(this->Stream);
			this->PreviousTokenWritten = this->WriteValueOnly( Value );
		}

		/** Converts the lookup table of ints (which are stored as identifiers and string values in the Json) and the FText's they represent to an FString. */
		FString GetSerializedLookupTable()
		{
			return FiBSerializationHelpers::Serialize< TMap< int32, FText > >(LookupTable, true);
		}

		struct FLookupTableItem
		{
			FText Text;

			FLookupTableItem(FText InText)
				: Text(InText)
			{

			}

			bool operator==(const FLookupTableItem& InObject) const
			{
				if (!Text.CompareTo(InObject.Text))
				{
					if (FTextInspector::GetNamespace(Text).Get(TEXT("DefaultNamespace")) == FTextInspector::GetNamespace(InObject.Text).Get(TEXT("DefaultNamespace")))
					{
						if (FTextInspector::GetKey(Text).Get(TEXT("DefaultKey")) == FTextInspector::GetKey(InObject.Text).Get(TEXT("DefaultKey")))
						{
							return true;
						}
					}
				}

				return false;
			}

			friend uint32 GetTypeHash(const FLookupTableItem& InObject)
			{
				FString Namespace = FTextInspector::GetNamespace(InObject.Text).Get(TEXT("DefaultNamespace"));
				FString Key = FTextInspector::GetKey(InObject.Text).Get(TEXT("DefaultKey"));
				uint32 Hash = HashCombine(GetTypeHash(InObject.Text.ToString()), HashCombine(GetTypeHash(Namespace), GetTypeHash(Key)));
				return Hash;
			}
		};

	protected:
		TJsonFindInBlueprintStringWriter( FString* const InOutString )
			: TJsonStringWriter<PrintPolicy>( InOutString, 0 )
		{
		}

		virtual void WriteStringValue( const FString& String ) override
		{
			// We just want to make sure all strings are converted into FText hex strings, used by the FiB system
			WriteTextValue(FText::FromString(String));
		}

		void WriteTextValue( const FText& Text )
		{
			// Check to see if the value has already been added.
			int32* TableLookupValuePtr = ReverseLookupTable.Find(FLookupTableItem(Text));
			if(TableLookupValuePtr)
			{
				TJsonStringWriter<PrintPolicy>::WriteStringValue(FString::FromInt(*TableLookupValuePtr));
			}
			else
			{
				// Add the FText to the table and write to the Json the ID to look the item up using
				int32 TableLookupValue = LookupTable.Num();
				{
					LookupTable.Add(TableLookupValue, Text);
					ReverseLookupTable.Add(FLookupTableItem(Text), TableLookupValue);
				}
				TJsonStringWriter<PrintPolicy>::WriteStringValue( FString::FromInt(TableLookupValue) );
			}
		}

		FORCEINLINE void WriteIdentifier( const FText& Identifier )
		{
			this->WriteCommaIfNeeded();
			PrintPolicy::WriteLineTerminator(this->Stream);

			PrintPolicy::WriteTabs(this->Stream, this->IndentLevel);

			WriteTextValue( Identifier );
			PrintPolicy::WriteChar(this->Stream, TCHAR(':'));
		}
		
		// This gets serialized
		TMap< int32, FText > LookupTable;

		// This is just locally needed for the write, to lookup the integer value by using the string of the FText
		TMap< FLookupTableItem, int32 > ReverseLookupTable;

	public:
		/** Cached mapping of all searchable properties that have been discovered while gathering searchable data for the current Blueprint */
		TMap<UStruct*, TArray<FSearchableProperty>> CachedPropertyMapping;
	};

	typedef TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>> SearchMetaDataWriterParentClass;
	typedef TJsonFindInBlueprintStringWriter<TCondensedJsonPrintPolicy<TCHAR>> SearchMetaDataWriter;

	/** Json Writer used for serializing FText's in the correct format for Find-in-Blueprints */
	template <class CharType = TCHAR>
	class TJsonFindInBlueprintStringReader : public TJsonReader<CharType>
	{
	public:
		static TSharedRef< TJsonFindInBlueprintStringReader< TCHAR > > Create( FArchive* const Stream, TMap< int32, FText >& InLookupTable  )
		{
			return MakeShareable( new TJsonFindInBlueprintStringReader( Stream, InLookupTable ) );
		}

		TJsonFindInBlueprintStringReader( FArchive* InStream,  TMap< int32, FText >& InLookupTable )
			: TJsonReader<CharType>(InStream)
			, LookupTable(MoveTemp(InLookupTable))
		{

		}

		FORCEINLINE virtual const FString& GetIdentifier() const override
		{
			return this->Identifier;
		}

		FORCEINLINE virtual  const FString& GetValueAsString() const override
		{ 
			check( this->CurrentToken == EJsonToken::String ); 
			// The string value from Json is a Hex value that must be looked up in the LookupTable to find the FText it represents
			return this->StringValue;
		}

		TMap< int32, FText > LookupTable;
	};

	typedef TJsonFindInBlueprintStringReader<TCHAR> SearchMetaDataReader;

	/**
	 * Checks if Json value is searchable, eliminating data that not considered useful to search for
	 *
	 * @param InJsonValue		The Json value object to examine for searchability
	 * @return					TRUE if the value should be searchable
	 */
	bool CheckIfJsonValueIsSearchable( TSharedPtr< FJsonValue > InJsonValue )
	{
		/** Check for interesting values
		 *  booleans are not interesting, there are a lot of them
		 *  strings are not interesting if they are empty
		 *  numbers are not interesting if they are 0
		 *  arrays are not interesting if they are empty or if they are filled with un-interesting types
		 *  objects may not have interesting values when dug into
		 */
		bool bValidPropetyValue = true;
		if(InJsonValue->Type == EJson::Boolean || InJsonValue->Type == EJson::None || InJsonValue->Type == EJson::Null)
		{
			bValidPropetyValue = false;
		}
		else if(InJsonValue->Type == EJson::String)
		{
			FString temp = InJsonValue->AsString();
			if(InJsonValue->AsString().IsEmpty())
			{
				bValidPropetyValue = false;
			}
		}
		else if(InJsonValue->Type == EJson::Number)
		{
			if(InJsonValue->AsNumber() == 0.0)
			{
				bValidPropetyValue = false;
			}
		}
		else if(InJsonValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& JsonArray = InJsonValue->AsArray();
			if(JsonArray.Num() > 0)
			{
				// Some types are never interesting and the contents of the array should be ignored. Other types can be interesting, the contents of the array should be stored (even if
				// the values may not be interesting, so that index values can be obtained)
				if(JsonArray[0]->Type != EJson::Array && JsonArray[0]->Type != EJson::String && JsonArray[0]->Type != EJson::Number && JsonArray[0]->Type != EJson::Object)
				{
					bValidPropetyValue = false;
				}
			}
		}
		else if(InJsonValue->Type == EJson::Object)
		{
			// Start it out as not being valid, if we find any sub-items that are searchable, it will be marked to TRUE
			bValidPropetyValue = false;

			// Go through all value/key pairs to see if any of them are searchable, remove the ones that are not
			const TSharedPtr<FJsonObject>& JsonObject = InJsonValue->AsObject();
			for(TMap<FString, TSharedPtr<FJsonValue>>::TIterator Iter = JsonObject->Values.CreateIterator(); Iter; ++Iter)
			{
				// Empty keys don't convert to JSON, so we also remove the entry in that case. Note: This means the entry is not going to be searchable.
				// @todo - Potentially use a placeholder string that uniquely identifies this as an empty key?
				const bool bHasEmptyKey = Iter->Key.IsEmpty();

				if(!CheckIfJsonValueIsSearchable(Iter->Value) || bHasEmptyKey)
				{
					// Note: It's safe to keep incrementing after this; the underlying logic maps to TSparseArray/TConstSetBitIterator::RemoveCurrent().
					Iter.RemoveCurrent();
				}
				else
				{
					bValidPropetyValue = true;
				}
			}
			
		}

		return bValidPropetyValue;
	}

	/**
	 * Saves a graph pin type to a Json object
	 *
	 * @param InWriter				Writer used for saving the Json
	 * @param InPinType				The pin type to save
	 */
	void SavePinTypeToJson(TSharedRef< SearchMetaDataWriter>& InWriter, const FEdGraphPinType& InPinType)
	{
		// Only save strings that are not empty

		if(!InPinType.PinCategory.IsNone())
		{
			InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_PinCategory, InPinType.PinCategory.ToString());
		}

		if(!InPinType.PinSubCategory.IsNone())
		{
			InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_PinSubCategory, InPinType.PinSubCategory.ToString());
		}

		if(InPinType.PinSubCategoryObject.IsValid())
		{
			InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_ObjectClass, FText::FromString(InPinType.PinSubCategoryObject->GetName()));
		}
		InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_IsArray, InPinType.IsArray());
		InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_IsReference, InPinType.bIsReference);
	}

	/**
	 * Helper function to save a variable description to Json
	 *
	 * @param InWriter					Json writer object
	 * @param InBlueprint				Blueprint the property for the variable can be found in, if any
	 * @param InVariableDescription		The variable description being serialized to Json
	 */
	void SaveVariableDescriptionToJson(TSharedRef< SearchMetaDataWriter>& InWriter, const UBlueprint* InBlueprint, const FBPVariableDescription& InVariableDescription)
	{
		FEdGraphPinType VariableType = InVariableDescription.VarType;

		InWriter->WriteObjectStart();

		InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_Name, InVariableDescription.FriendlyName);

		// Find the variable's tooltip
		FString TooltipResult;
		
		if(InVariableDescription.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			TooltipResult = InVariableDescription.GetMetaData(FBlueprintMetadata::MD_Tooltip);
		}
		InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_Tooltip, TooltipResult);

		// Save the variable's pin type
		SavePinTypeToJson(InWriter, VariableType);

		// Find the UProperty and convert it into a Json value.
		UProperty* VariableProperty = FindField<UProperty>(InBlueprint->GeneratedClass, InVariableDescription.VarName);
		if(VariableProperty)
		{
			const uint8* PropData = VariableProperty->ContainerPtrToValuePtr<uint8>(InBlueprint->GeneratedClass->GetDefaultObject());
			auto JsonValue = FJsonObjectConverter::UPropertyToJsonValue(VariableProperty, PropData, 0, 0);

			// Only use the value if it is searchable
			if(BlueprintSearchMetaDataHelpers::CheckIfJsonValueIsSearchable(JsonValue))
			{
				TSharedRef< FJsonValue > JsonValueAsSharedRef = JsonValue.ToSharedRef();
				FJsonSerializer::Serialize(JsonValue, FFindInBlueprintSearchTags::FiB_DefaultValue.ToString(), StaticCastSharedRef<SearchMetaDataWriterParentClass>(InWriter), false );
			}
		}

		InWriter->WriteObjectEnd();
	}

	/** Helper enum to gather searchable UProperties */
	enum EGatherSearchableType
	{
		SEARCHABLE_AS_DESIRED = 0,
		SEARCHABLE_FULL,
		SEARCHABLE_SHALLOW,
	};

	/**
	 * Gathers all searchable properties in a UObject and writes them out to Json
	 *
	 * @param InWriter				Json writer
	 * @param InValue				Value of the Object to serialize
	 * @param InStruct				Struct or class that represent the UObject's layout
	 * @param InSearchableType		Informs the system how it should examine the properties to determine if they are searchable. All sub-properties of searchable properties are automatically gathered unless marked as not being searchable
	 */
	void GatherSearchableProperties(TSharedRef< SearchMetaDataWriter>& InWriter, const void* InValue, UStruct* InStruct, EGatherSearchableType InSearchableType = SEARCHABLE_AS_DESIRED);

	/**
	 * Examines a searchable property and digs in deeper if it is a UObject, UStruct, or an array, or serializes it straight out to Json
	 *
	 * @param InWriter				Json writer
	 * @param InProperty			Property to examine
	 * @param InValue				Value to find the property in the UStruct
	 * @param InStruct				Struct or class that represent the UObject's layout
	 */
	void GatherSearchablesFromProperty(TSharedRef< SearchMetaDataWriter>& InWriter, UProperty* InProperty, const void* InValue, UStruct* InStruct)
	{
		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(InProperty))
		{
			FScriptArrayHelper Helper(ArrayProperty, InValue);
			InWriter->WriteArrayStart(FText::FromString(InProperty->GetName()));
			for (int32 i=0, n=Helper.Num(); i<n; ++i)
			{
				GatherSearchablesFromProperty(InWriter, ArrayProperty->Inner, Helper.GetRawPtr(i), InStruct);
			}
			InWriter->WriteArrayEnd();
		}
		else if (UStructProperty* StructProperty = Cast<UStructProperty>(InProperty))
		{
			if (!InProperty->HasMetaData(*FFiBMD::FiBSearchableMD) || InProperty->GetBoolMetaData(*FFiBMD::FiBSearchableMD))
			{
				GatherSearchableProperties(InWriter, InValue, StructProperty->Struct, SEARCHABLE_FULL);
			}
		}
		else if (UObjectProperty* ObjectProperty = Cast<UObjectProperty>(InProperty))
		{
			UObject* SubObject = ObjectProperty->GetObjectPropertyValue(InValue);
			if (SubObject)
			{
				// Objects default to shallow unless they are marked as searchable
				EGatherSearchableType searchType = SEARCHABLE_SHALLOW;

				// Check if there is any Searchable metadata
				if (InProperty->HasMetaData(*FFiBMD::FiBSearchableMD))
				{
					// Check if that metadata informs us that the property should not be searchable
					bool bSearchable = InProperty->GetBoolMetaData(*FFiBMD::FiBSearchableMD);
					if (bSearchable)
					{
						GatherSearchableProperties(InWriter, SubObject, SubObject->GetClass(), SEARCHABLE_FULL);
					}
				}
				else
				{
					// Shallow conversion of property to string
					TSharedPtr<FJsonValue> JsonValue;
					JsonValue = FJsonObjectConverter::UPropertyToJsonValue(InProperty, InValue, 0, 0);
					FJsonSerializer::Serialize(JsonValue, InProperty->GetName(), StaticCastSharedRef<SearchMetaDataWriterParentClass>(InWriter), false);
				}
			}
		}
		else
		{
			TSharedPtr<FJsonValue> JsonValue;
			JsonValue = FJsonObjectConverter::UPropertyToJsonValue(InProperty, InValue, 0, 0);
			FJsonSerializer::Serialize(JsonValue, InProperty->GetName(), StaticCastSharedRef<SearchMetaDataWriterParentClass>(InWriter), false);
		}
	}

	void GatherSearchableProperties(TSharedRef<SearchMetaDataWriter>& InWriter, const void* InValue, UStruct* InStruct, EGatherSearchableType InSearchableType)
	{
		if (InValue)
		{
			TArray<FSearchableProperty>* SearchablePropertyData = InWriter->CachedPropertyMapping.Find(InStruct);
			check(SearchablePropertyData);

			for (FSearchableProperty& SearchableProperty : *SearchablePropertyData)
			{
				UProperty* Property = SearchableProperty.TargetProperty;
				bool bIsSearchableMD = SearchableProperty.bIsSearchableMD;
				bool bIsShallowSearchableMD = SearchableProperty.bIsShallowSearchableMD;
				// It only is truly marked as not searchable if it has the metadata set to false, if the metadata is missing then we assume the searchable type that is passed in unless SEARCHABLE_AS_DESIRED
				bool bIsMarkedNotSearchableMD = SearchableProperty.bIsMarkedNotSearchableMD;

				if ( (InSearchableType != SEARCHABLE_AS_DESIRED && !bIsMarkedNotSearchableMD) 
					|| bIsShallowSearchableMD || bIsSearchableMD)
				{
					const void* Value = Property->ContainerPtrToValuePtr<uint8>(InValue);

					// Need to store the metadata on the property in a sub-object
					InWriter->WriteObjectStart(FText::FromString(Property->GetName()));
					{
						InWriter->WriteObjectStart(FFindInBlueprintSearchTags::FiBMetaDataTag);
						{
							if (Property->GetBoolMetaData(*FFiBMD::FiBSearchableHiddenExplicitMD))
							{
								InWriter->WriteValue(FText::FromString(FFiBMD::FiBSearchableHiddenExplicitMD), true);
							}
							else if (Property->GetBoolMetaData(*FFiBMD::FiBSearchableExplicitMD))
							{
								InWriter->WriteValue(FText::FromString(FFiBMD::FiBSearchableExplicitMD), true);
							}
						}
						InWriter->WriteObjectEnd();

						if (Property->ArrayDim == 1)
						{
							GatherSearchablesFromProperty(InWriter, Property, Value, InStruct);
						}
						else
						{
							TArray< TSharedPtr<FJsonValue> > Array;
							for (int Index = 0; Index != Property->ArrayDim; ++Index)
							{
								GatherSearchablesFromProperty(InWriter, Property, (char*)Value + Index * Property->ElementSize, InStruct);
							}
						}
					}
					InWriter->WriteObjectEnd();
				}
			}
		}
	}

	/**
	 * Caches all properties that have searchability metadata
	 *
	 * @param InOutCachePropertyMapping		Mapping of all the searchable properties that we are building
	 * @param InValue						Value of the Object to serialize
	 * @param InStruct						Struct or class that represent the UObject's layout
	 * @param InSearchableType				Informs the system how it should examine the properties to determine if they are searchable. All sub-properties of searchable properties are automatically gathered unless marked as not being searchable
	 */
	void CacheSearchableProperties(TMap<UStruct*, TArray<FSearchableProperty>>& InOutCachePropertyMapping, const void* InValue, UStruct* InStruct, EGatherSearchableType InSearchableType = SEARCHABLE_AS_DESIRED);

	/**
	 * Digs into a property for any sub-properties that might exist so it can recurse and cache them
	 *
	 * @param InOutCachePropertyMapping		Mapping of all the searchable properties that we are building
	 * @param InProperty					Property currently being cached
	 * @param InValue						Value of the Object to serialize
	 * @param InStruct						Struct or class that represent the UObject's layout
	 */
	void CacheSubPropertySearchables(TMap<UStruct*, TArray<FSearchableProperty>>& InOutCachePropertyMapping, UProperty* InProperty, const void* InValue, UStruct* InStruct)
	{
		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(InProperty))
		{
			FScriptArrayHelper Helper(ArrayProperty, InValue);
			for (int32 i = 0, n = Helper.Num(); i < n; ++i)
			{
				CacheSubPropertySearchables(InOutCachePropertyMapping, ArrayProperty->Inner, Helper.GetRawPtr(i), InStruct);
			}
		}
		else if (UStructProperty* StructProperty = Cast<UStructProperty>(InProperty))
		{
			if (!InOutCachePropertyMapping.Find(StructProperty->Struct))
			{
				if (!InProperty->HasMetaData(*FFiBMD::FiBSearchableMD) || InProperty->GetBoolMetaData(*FFiBMD::FiBSearchableMD))
				{
					CacheSearchableProperties(InOutCachePropertyMapping, InValue, StructProperty->Struct, SEARCHABLE_FULL);
				}
			}
		}
		else if (UObjectProperty* ObjectProperty = Cast<UObjectProperty>(InProperty))
		{
			UObject* SubObject = ObjectProperty->GetObjectPropertyValue(InValue);
			if (SubObject)
			{
				// Objects default to shallow unless they are marked as searchable
				EGatherSearchableType SearchType = SEARCHABLE_SHALLOW;

				// Check if there is any Searchable metadata
				if (InProperty->HasMetaData(*FFiBMD::FiBSearchableMD))
				{
					if (!InOutCachePropertyMapping.Find(SubObject->GetClass()))
					{
						// Check if that metadata informs us that the property should not be searchable
						bool bSearchable = InProperty->GetBoolMetaData(*FFiBMD::FiBSearchableMD);
						if (bSearchable)
						{
							CacheSearchableProperties(InOutCachePropertyMapping, SubObject, SubObject->GetClass(), SEARCHABLE_FULL);
						}
					}
				}
			}
		}
	}

	void CacheSearchableProperties(TMap<UStruct*, TArray<FSearchableProperty>>& InOutCachePropertyMapping, const void* InValue, UStruct* InStruct, EGatherSearchableType InSearchableType)
	{
		if (InValue)
		{
			TArray<FSearchableProperty> SearchableProperties;

			for (TFieldIterator<UProperty> PropIt(InStruct); PropIt; ++PropIt)
			{
				UProperty* Property = *PropIt;
				bool bIsSearchableMD = Property->GetBoolMetaData(*FFiBMD::FiBSearchableMD);
				bool bIsShallowSearchableMD = Property->GetBoolMetaData(*FFiBMD::FiBSearchableShallowMD);
				// It only is truly marked as not searchable if it has the metadata set to false, if the metadata is missing then we assume the searchable type that is passed in unless SEARCHABLE_AS_DESIRED
				bool bIsMarkedNotSearchableMD = Property->HasMetaData(*FFiBMD::FiBSearchableMD) && !bIsSearchableMD;

				if ((InSearchableType != SEARCHABLE_AS_DESIRED && !bIsMarkedNotSearchableMD)
					|| bIsShallowSearchableMD || bIsSearchableMD)
				{
					const void* Value = Property->ContainerPtrToValuePtr<uint8>(InValue);

					FSearchableProperty SearchableProperty;
					SearchableProperty.TargetProperty = Property;
					SearchableProperty.bIsSearchableMD = bIsSearchableMD;
					SearchableProperty.bIsShallowSearchableMD = bIsShallowSearchableMD;
					SearchableProperty.bIsMarkedNotSearchableMD = bIsMarkedNotSearchableMD;

					if (Property->ArrayDim == 1)
					{
						CacheSubPropertySearchables(InOutCachePropertyMapping, Property, Value, InStruct);
					}
					else
					{
						TArray< TSharedPtr<FJsonValue> > Array;
						for (int Index = 0; Index != Property->ArrayDim; ++Index)
						{
							CacheSubPropertySearchables(InOutCachePropertyMapping, Property, (char*)Value + Index * Property->ElementSize, InStruct);
						}
					}
					SearchableProperties.Add(MoveTemp(SearchableProperty));
				}
				InOutCachePropertyMapping.Add(InStruct, SearchableProperties);
			}
		}
	}

	/**
	 * Gathers all nodes from a specified graph and serializes their searchable data to Json
	 *
	 * @param InWriter		The Json writer to use for serialization
	 * @param InGraph		The graph to search through
	 */
	void GatherNodesFromGraph(TSharedRef< SearchMetaDataWriter>& InWriter, const UEdGraph* InGraph)
	{
		// Collect all macro graphs
		InWriter->WriteArrayStart(FFindInBlueprintSearchTags::FiB_Nodes);
		{
			for(auto* Node : InGraph->Nodes)
			{
				if(Node)
				{
					{
						// Make sure we don't collect search data for nodes that are going away soon
						if (Node->GetOuter()->IsPendingKill())
						{
							continue;
						}

						InWriter->WriteObjectStart();

						// Retrieve the search metadata from the node, some node types may have extra metadata to be searchable.
						TArray<struct FSearchTagDataPair> Tags;
						Node->AddSearchMetaDataInfo(Tags);

						// Go through the node metadata tags and put them into the Json object.
						for (const FSearchTagDataPair& SearchData : Tags)
						{
							InWriter->WriteValue(SearchData.Key, SearchData.Value);
						}
					}

					{
						// Find all the pins and extract their metadata
						InWriter->WriteArrayStart(FFindInBlueprintSearchTags::FiB_Pins);
						for (UEdGraphPin* Pin : Node->Pins)
						{
							// Hidden pins are not searchable
							if (Pin->bHidden == false)
							{
								InWriter->WriteObjectStart();
								{
									InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_Name, Pin->GetSchema()->GetPinDisplayName(Pin));
									InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_DefaultValue, Pin->GetDefaultAsText());
								}
								SavePinTypeToJson(InWriter, Pin->PinType);
								InWriter->WriteObjectEnd();
							}
						}
						InWriter->WriteArrayEnd();

						if (!InWriter->CachedPropertyMapping.Find(Node->GetClass()))
						{
							CacheSearchableProperties(InWriter->CachedPropertyMapping, Node, Node->GetClass());
						}
						// Only support this for nodes for now, will gather all searchable properties
						GatherSearchableProperties(InWriter, Node, Node->GetClass());

						InWriter->WriteObjectEnd();
					}
				}
				
			}
		}
		InWriter->WriteArrayEnd();
	}

	/** 
	 * Gathers all graph's search data (and subojects) and serializes them to Json
	 *
	 * @param InWriter			The Json writer to use for serialization
	 * @param InGraphArray		All the graphs to process
	 * @param InTitle			The array title to place these graphs into
	 * @param InOutSubGraphs	All the subgraphs that need to be processed later
	 */
	void GatherGraphSearchData(TSharedRef< SearchMetaDataWriter>& InWriter, const UBlueprint* InBlueprint, const TArray< UEdGraph* >& InGraphArray, FText InTitle, TArray< UEdGraph* >* InOutSubGraphs)
	{
		if(InGraphArray.Num() > 0)
		{
			// Collect all graphs
			InWriter->WriteArrayStart(InTitle);
			{
				for(const UEdGraph* Graph : InGraphArray)
				{
					// This is non-critical but should not happen and needs to be resolved
					if (!ensure(Graph != nullptr))
					{
						continue;
					}
					InWriter->WriteObjectStart();

					FGraphDisplayInfo DisplayInfo;
					if (auto GraphSchema = Graph->GetSchema())
					{
						GraphSchema->GetGraphDisplayInformation(*Graph, DisplayInfo);
					}
					InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_Name, DisplayInfo.PlainName);

					FText GraphDescription = FBlueprintEditorUtils::GetGraphDescription(Graph);
					if(!GraphDescription.IsEmpty())
					{
						InWriter->WriteValue(FFindInBlueprintSearchTags::FiB_Description, GraphDescription);
					}
					// All nodes will appear as children to the graph in search results
					GatherNodesFromGraph(InWriter, Graph);

					// Collect local variables
					TArray<UK2Node_FunctionEntry*> FunctionEntryNodes;
					Graph->GetNodesOfClass<UK2Node_FunctionEntry>(FunctionEntryNodes);

					InWriter->WriteArrayStart(FFindInBlueprintSearchTags::FiB_Properties);
					{
						// Search in all FunctionEntry nodes for their local variables and add them to the list
						FString ActionCategory;
						for (UK2Node_FunctionEntry* const FunctionEntry : FunctionEntryNodes)
						{
							for( const FBPVariableDescription& Variable : FunctionEntry->LocalVariables )
							{
								SaveVariableDescriptionToJson(InWriter, InBlueprint, Variable);
							}
						}
					}
					InWriter->WriteArrayEnd(); // Properties

					InWriter->WriteObjectEnd();

					// Only if asked to do it
					if(InOutSubGraphs)
					{
						Graph->GetAllChildrenGraphs(*InOutSubGraphs);
					}
				}
			}
			InWriter->WriteArrayEnd();
		}
	}
}

class FCacheAllBlueprintsTickableObject
{
public:
	DECLARE_DELEGATE_OneParam(FOnAssetCached, FName);

	struct FCacheParams
	{
		/** Control flags */
		EFiBCacheOpFlags OpFlags;

		/** Callback for when assets are cached */
		FOnAssetCached OnCached;

		/** Callback for when caching is finished */
		FSimpleDelegate OnFinished;

		FCacheParams()
			:OpFlags(EFiBCacheOpFlags::None)
		{
		}
	};

	FCacheAllBlueprintsTickableObject(TSet<FName> InAssets, const FCacheParams& InParams)
		: TickCacheIndex(0)
		, UncachedAssets(InAssets.Array())
		, bIsStarted(false)
		, bIsCancelled(false)
		, CacheParams(InParams)
	{
		if (EnumHasAnyFlags(CacheParams.OpFlags, EFiBCacheOpFlags::ShowProgress)
			&& !EnumHasAnyFlags(CacheParams.OpFlags, EFiBCacheOpFlags::HideNotifications))
		{
			// Start the Blueprint indexing 'progress' notification
			FNotificationInfo Info(LOCTEXT("BlueprintIndexMessage", "Indexing Blueprints..."));
			Info.bFireAndForget = false;
			if (EnumHasAnyFlags(CacheParams.OpFlags, EFiBCacheOpFlags::AllowUserCancel))
			{
				Info.ButtonDetails.Add(FNotificationButtonInfo(
					LOCTEXT("BlueprintIndexCancel", "Cancel"),
					LOCTEXT("BlueprintIndexCancelToolTip", "Cancels indexing Blueprints."), FSimpleDelegate::CreateRaw(this, &FCacheAllBlueprintsTickableObject::OnCancelCaching, false)));
			}

			ProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
			if (ProgressNotification.IsValid())
			{
				ProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}

	~FCacheAllBlueprintsTickableObject()
	{

	}

	/** Returns the current cache index of the object */
	int32 GetCurrentCacheIndex() const
	{
		return TickCacheIndex + 1;
	}

	/** Returns the name of the current Blueprint being cached */
	FName GetCurrentCacheBlueprintName() const
	{
		if(UncachedAssets.Num() && TickCacheIndex >= 0)
		{
			return UncachedAssets[TickCacheIndex];
		}
		return NAME_None;
	}

	/** Returns the progress as a percent */
	float GetCacheProgress() const
	{
		return UncachedAssets.Num() > 0 ? (float)TickCacheIndex / (float)UncachedAssets.Num() : 1.0f;
	}

	/** Returns the number of uncached assets */
	int32 GetUncachedAssetCount()
	{
		return UncachedAssets.Num();
	}

	/** Returns the entire list of uncached assets that this object will attempt to cache */
	const TArray<FName>& GetUncachedAssetList() const
	{
		return UncachedAssets;
	}

	/** True if there is a callback when done caching, this will prevent a re-query from occuring */
	bool HasPostCacheWork() const
	{
		return CacheParams.OnFinished.IsBound();
	}

	/** Cancels caching and destroys this object */
	void OnCancelCaching(bool bIsImmediate)
	{
		if (!bIsCancelled)
		{
			if (ProgressNotification.IsValid())
			{
				ProgressNotification.Pin()->SetText(LOCTEXT("BlueprintIndexCancelled", "Cancelled Indexing Blueprints!"));

				ProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
				ProgressNotification.Pin()->ExpireAndFadeout();
			}

			// Sometimes we can't wait another tick to shutdown, so make the callback immediately.
			if (bIsImmediate)
			{
				// Note: This will effectively delete this instance. It should not be used after this!
				FFindInBlueprintSearchManager::Get().FinishedCachingBlueprints(TickCacheIndex, FailedToCacheList);
			}
			else
			{
				bIsCancelled = true;
			}
		}
	}

	/** Enables the caching process */
	void Start()
	{
		if (!bIsStarted)
		{
			bIsStarted = true;
			FFindInBlueprintSearchManager::Get().StartedCachingBlueprints(CacheParams.OpFlags);
		}
	}

	void Tick(float InDeltaTime)
	{
		if (!bIsStarted)
		{
			return;
		}

		if (UncachedAssets.Num() == 0)
		{
			// Immediately finish if we have no assets to index. This will delete this instance!
			Finish();

			return;
		}

		if (bIsCancelled || GWarn->ReceivedUserCancel())
		{
			// Note: This will effectively delete this instance. It should not be used after this!
			FFindInBlueprintSearchManager::Get().FinishedCachingBlueprints(TickCacheIndex, FailedToCacheList);
		}
		else
		{
			FAssetRegistryModule* AssetRegistryModule = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FAssetData AssetData = AssetRegistryModule->Get().GetAssetByObjectPath(UncachedAssets[TickCacheIndex]);
			if (AssetData.IsValid())
			{
				const bool bIsWorldAsset = AssetData.AssetClass == UWorld::StaticClass()->GetFName();

				// Construct a full package filename with path so we can query the read only status and save to disk
				FString FinalPackageFilename = FPackageName::LongPackageNameToFilename(AssetData.PackageName.ToString());
				if (FinalPackageFilename.Len() > 0 && FPaths::GetExtension(FinalPackageFilename).Len() == 0)
				{
					FinalPackageFilename += bIsWorldAsset ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
				}
				FText ErrorMessage;
				bool bValidFilename = FFileHelper::IsFilenameValidForSaving(FinalPackageFilename, ErrorMessage);
				if (bValidFilename)
				{
					bValidFilename = bIsWorldAsset ? FEditorFileUtils::IsValidMapFilename(FinalPackageFilename, ErrorMessage) : FPackageName::IsValidLongPackageName(FinalPackageFilename, false, &ErrorMessage);
				}

				const bool bCheckOutAndSave = EnumHasAnyFlags(CacheParams.OpFlags, EFiBCacheOpFlags::CheckOutAndSave);

				bool bIsAssetReadOnlyOnDisk = IFileManager::Get().IsReadOnly(*FinalPackageFilename);
				bool bFailedToCache = bCheckOutAndSave;

				if (!bIsAssetReadOnlyOnDisk || !bCheckOutAndSave)
				{
					if (!FFindInBlueprintSearchManager::Get().IsUnindexedCacheInProgress())
					{
						// Re-index and update the cached value for loaded Blueprint assets only
						if (AssetData.IsAssetLoaded())
						{
							if (UBlueprint* LoadedBlueprintAsset = Cast<UBlueprint>(AssetData.GetAsset()))
							{
								FFindInBlueprintSearchManager::Get().AddOrUpdateBlueprintSearchMetadata(LoadedBlueprintAsset, true);
							}
						}
					}
					else
					{
						UObject* Asset = AssetData.GetAsset();
						if (Asset && bCheckOutAndSave)
						{
							if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset))
							{
								if (BlueprintAsset->SkeletonGeneratedClass == nullptr)
								{
									// There is no skeleton class, something was wrong with the Blueprint during compile on load. This asset will be marked as failing to cache.
									bFailedToCache = false;
								}
							}

							// Still good to attempt to save
							if (bFailedToCache)
							{
								// Assume the package was correctly checked out from SCC
								bool bOutPackageLocallyWritable = true;

								UPackage* Package = AssetData.GetPackage();

								ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
								// Trusting the SCC status in the package file cache to minimize network activity during save.
								const FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(Package, EStateCacheUsage::Use);
								// If the package is in the depot, and not recognized as editable by source control, and not read-only, then we know the user has made the package locally writable!
								const bool bSCCCanEdit = !SourceControlState.IsValid() || SourceControlState->CanCheckIn() || SourceControlState->IsIgnored() || SourceControlState->IsUnknown();
								const bool bSCCIsCheckedOut = SourceControlState.IsValid() && SourceControlState->IsCheckedOut();
								const bool bInDepot = SourceControlState.IsValid() && SourceControlState->IsSourceControlled();
								if (!bSCCCanEdit && bInDepot && !bIsAssetReadOnlyOnDisk && SourceControlProvider.UsesLocalReadOnlyState() && !bSCCIsCheckedOut)
								{
									bOutPackageLocallyWritable = false;
								}

								// Save the package if the file is writable
								if (bOutPackageLocallyWritable)
								{
									UWorld* WorldAsset = Cast<UWorld>(Asset);

									// Save the package
									EObjectFlags ObjectFlags = (WorldAsset == nullptr) ? RF_Standalone : RF_NoFlags;

									if (GEditor->SavePackage(Package, WorldAsset, ObjectFlags, *FinalPackageFilename, GError, nullptr, false, true, SAVE_NoError))
									{
										bFailedToCache = false;
									}
								}
							}
						}
					}
				}

				if (bFailedToCache)
				{
					FailedToCacheList.Add(UncachedAssets[TickCacheIndex]);
				}
				else
				{
					CacheParams.OnCached.ExecuteIfBound(UncachedAssets[TickCacheIndex]);
				}
			}
			else
			{
				FailedToCacheList.Add(UncachedAssets[TickCacheIndex]);
			}

			++TickCacheIndex;

			// Check if done caching Blueprints
			if(TickCacheIndex == UncachedAssets.Num())
			{
				// Note: This will effectively delete this instance, do not use after this!
				Finish();
			}
			else if(ProgressNotification.IsValid())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("Percent"), FText::AsPercent(GetCacheProgress()));
				ProgressNotification.Pin()->SetText(FText::Format(LOCTEXT("BlueprintIndexProgress", "Indexing Blueprints... ({Percent})"), Args));
			}
		}
	}

protected:
	/** Completes a successful caching process */
	void Finish()
	{
		if (ProgressNotification.IsValid())
		{
			ProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Success);
			ProgressNotification.Pin()->ExpireAndFadeout();

			ProgressNotification.Pin()->SetText(LOCTEXT("BlueprintIndexComplete", "Finished indexing Blueprints!"));
		}

		// We have actually finished, use the OnFinished callback.
		CacheParams.OnFinished.ExecuteIfBound();

		// Note: This will effectively delete this instance. It should not be used after this!
		FFindInBlueprintSearchManager::Get().FinishedCachingBlueprints(TickCacheIndex, FailedToCacheList);
	}

private:

	/** The current index, increases at a rate of once per tick */
	int32 TickCacheIndex;

	/** The list of assets that are in the process of being cached */
	TArray<FName> UncachedAssets;

	/** Notification that appears and details progress */
	TWeakPtr<SNotificationItem> ProgressNotification;

	/** Set of Blueprints that failed to be saved */
	TSet<FName> FailedToCacheList;

	/** TRUE if the caching process is started */
	bool bIsStarted;

	/** TRUE if the user has requested to cancel the caching process */
	bool bIsCancelled;

	/** Parameters for task configuration */
	FCacheParams CacheParams;
};

FFindInBlueprintSearchManager& FFindInBlueprintSearchManager::Get()
{
	if (Instance == NULL)
	{
		Instance = new FFindInBlueprintSearchManager();
		Instance->Initialize();
	}

	return *Instance;
}

FFindInBlueprintSearchManager::FFindInBlueprintSearchManager()
	: bEnableGatheringData(true)
	, bDisableDeferredIndexing(false)
	, bIsPausing(false)
	, AssetRegistryModule(nullptr)
	, CachingObject(nullptr)
{
	for (int32 TabIdx = 0; TabIdx < ARRAY_COUNT(GlobalFindResultsTabIDs); TabIdx++)
	{
		const FName TabID = FName(*FString::Printf(TEXT("GlobalFindResults_%02d"), TabIdx + 1));
		GlobalFindResultsTabIDs[TabIdx] = TabID;
	}
}

FFindInBlueprintSearchManager::~FFindInBlueprintSearchManager()
{
	if (AssetRegistryModule)
	{
		AssetRegistryModule->Get().OnAssetAdded().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRenamed().RemoveAll(this);
	}
	FKismetEditorUtilities::OnBlueprintUnloaded.RemoveAll(this);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);

	if(FModuleManager::Get().IsModuleLoaded("HotReload"))
	{
		IHotReloadInterface& HotReloadSupport = FModuleManager::GetModuleChecked<IHotReloadInterface>("HotReload");
		HotReloadSupport.OnHotReload().RemoveAll(this);
	}

	// Shut down the global find results tab feature.
	EnableGlobalFindResults(false);
}

void FFindInBlueprintSearchManager::Initialize()
{
	// Init configuration
	GConfig->GetBool(TEXT("BlueprintSearchSettings"), TEXT("bDisableDeferredIndexing"), bDisableDeferredIndexing, GEditorIni);

	// Must ensure we do not attempt to load the AssetRegistry Module while saving a package, however, if it is loaded already we can safely obtain it
	if (!GIsSavingPackage || (GIsSavingPackage && FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry"))))
	{
		AssetRegistryModule = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule->Get().OnAssetAdded().AddRaw(this, &FFindInBlueprintSearchManager::OnAssetAdded);
		AssetRegistryModule->Get().OnAssetRemoved().AddRaw(this, &FFindInBlueprintSearchManager::OnAssetRemoved);
		AssetRegistryModule->Get().OnAssetRenamed().AddRaw(this, &FFindInBlueprintSearchManager::OnAssetRenamed);
	}
	else
	{
		// Log a warning to inform the Asset Registry could not be initialized when FiB initialized due to saving package
		// The Asset Registry should be initialized before Find-in-Blueprints, or FiB should be explicitly initialized during a safe time
		// This message will not appear in commandlets because most commandlets do not care. If a search query is made, further warnings will be produced even in commandlets.
		if (!IsRunningCommandlet())
		{
			UE_LOG(LogBlueprint, Warning, TEXT("Find-in-Blueprints could not pre-cache all unloaded Blueprints due to the Asset Registry module being unable to initialize because a package is currently being saved. Pre-cache will not be reattempted!"));
		}
	}

	FKismetEditorUtilities::OnBlueprintUnloaded.AddRaw(this, &FFindInBlueprintSearchManager::OnBlueprintUnloaded);

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FFindInBlueprintSearchManager::PauseFindInBlueprintSearch);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FFindInBlueprintSearchManager::UnpauseFindInBlueprintSearch);
	FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FFindInBlueprintSearchManager::OnAssetLoaded);
	
	// Register to be notified of hot reloads
	IHotReloadInterface& HotReloadSupport = FModuleManager::LoadModuleChecked<IHotReloadInterface>("HotReload");
	HotReloadSupport.OnHotReload().AddRaw(this, &FFindInBlueprintSearchManager::OnHotReload);

	if(!GIsSavingPackage && AssetRegistryModule)
	{
		// Do an immediate load of the cache to catch any Blueprints that were discovered by the asset registry before we initialized.
		BuildCache();
	}

	// Register global find results tabs if the feature is enabled.
	if (GetDefault<UBlueprintEditorSettings>()->bHostFindInBlueprintsInGlobalTab)
	{
		EnableGlobalFindResults(true);
	}
}

void FFindInBlueprintSearchManager::OnAssetAdded(const FAssetData& InAssetData)
{
	const UClass* AssetClass = nullptr;
	{
		const UClass** FoundClass = CachedAssetClasses.Find(InAssetData.AssetClass);
		if (FoundClass)
		{
			AssetClass = *FoundClass;
		}
		else
		{
			AssetClass = InAssetData.GetClass();
			if (AssetClass)
			{
				CachedAssetClasses.Add(InAssetData.AssetClass, AssetClass);
			}
		}
	}

	const IBlueprintAssetHandler* Handler = AssetClass ? FBlueprintAssetHandler::Get().FindHandler(AssetClass) : nullptr;

	// No handler means we can't process this asset
	if (!Handler)
	{
		return;
	}

	if (InAssetData.IsAssetLoaded())
	{
		UObject*    AssetObject = InAssetData.GetAsset();
		UBlueprint* Blueprint   = Handler->RetrieveBlueprint(AssetObject);
		if (Blueprint)
		{
			AddOrUpdateBlueprintSearchMetadata(Blueprint);
		}
	}
	else if (Handler->AssetContainsBlueprint(InAssetData))
	{
		// Check first for versioned FiB data (latest codepath)
		FAssetDataTagMapSharedView::FFindTagResult Result = InAssetData.TagsAndValues.FindTag(FBlueprintTags::FindInBlueprintsData);
		if(Result.IsSet())
		{
			const FString& FiBVersionedSearchData = Result.GetValue();
			if (FiBVersionedSearchData.Len() == 0)
			{
				UnindexedAssets.Add(InAssetData.ObjectPath);
			}
			else
			{
				ExtractUnloadedFiBData(InAssetData, FiBVersionedSearchData, true);
			}
		}
		else
		{
			// Check for legacy (unversioned) FiB data
			FAssetDataTagMapSharedView::FFindTagResult ResultLegacy = InAssetData.TagsAndValues.FindTag("FiB");
			if (ResultLegacy.IsSet())
			{
				ExtractUnloadedFiBData(InAssetData, ResultLegacy.GetValue(), false);
			}
			// The asset has no FiB data, keep track of it so we can inform the user
			else
			{
				UnindexedAssets.Add(InAssetData.ObjectPath);
			}

		}
	}
}

void FFindInBlueprintSearchManager::ExtractUnloadedFiBData(const FAssetData& InAssetData, const FString& InFiBData, bool bIsVersioned)
{
	if (SearchMap.Contains(InAssetData.ObjectPath))
	{
		return;
	}

	FSearchData NewSearchData;
	NewSearchData.AssetPath = InAssetData.ObjectPath;
	InAssetData.GetTagValue(FBlueprintTags::ParentClassPath, NewSearchData.ParentClass);

	const FString ImplementedInterfaces = InAssetData.GetTagValueRef<FString>(FBlueprintTags::ImplementedInterfaces);
	if(!ImplementedInterfaces.IsEmpty())
	{
		// ImplementedInterfaces is an array of structs (FBPInterfaceDescription). When exported to an AR tag value, each entry will be formatted as:
		//
		//	Entry := (Interface=Type'Package.Class') OR
		//  Entry := (Interface=Type'Package.Class',Graphs=(Type'Package.Blueprint:Graph1',Type'Package.Blueprint:Graph2',...Type'Package.Blueprint:GraphN'))
		//
		// The full tag value (array of exported struct values) will then be formatted as follows:
		//
		//  Value := (Entry1,Entry2,...EntryN)
		//
		// Here we parse out the 'Interface' values, and add only the 'Name' part of the full interface path substrings into the new search data as keywords.

		auto FindSubStringPosLambda = [&ImplementedInterfaces](const FString& InSubString, int32 StartPosition) -> int32
		{
			return ImplementedInterfaces.Find(InSubString, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartPosition);
		};

		static const FString InterfaceFieldName = GET_MEMBER_NAME_STRING_CHECKED(FBPInterfaceDescription, Interface);

		int32 CurPos = FindSubStringPosLambda(InterfaceFieldName, 0);
		while (CurPos != INDEX_NONE)
		{
			CurPos = FindSubStringPosLambda(TEXT("="), CurPos);
			if (CurPos != INDEX_NONE)
			{
				CurPos = FindSubStringPosLambda(TEXT("."), CurPos);
				if (CurPos != INDEX_NONE)
				{
					const int32 StartPos = CurPos + 1;
					CurPos = FindSubStringPosLambda(TEXT("\'"), StartPos);
					if (CurPos != INDEX_NONE)
					{
						const FString InterfaceName = ImplementedInterfaces.Mid(StartPos, CurPos - StartPos);
						if (!InterfaceName.IsEmpty())
						{
							NewSearchData.Interfaces.Add(InterfaceName.TrimQuotes());
						}

						CurPos = FindSubStringPosLambda(InterfaceFieldName, CurPos + 1);
					}
				}
			}
		}
	}

	NewSearchData.bMarkedForDeletion = false;
	NewSearchData.Value = *InFiBData;

	// Deserialize the version if available
	if (bIsVersioned)
	{
		checkf(NewSearchData.Value.Len(), TEXT("Versioned search data was zero length!"));
		FBufferReader ReaderStream((void*)*NewSearchData.Value, NewSearchData.Value.Len() * sizeof(TCHAR), false);
		NewSearchData.VersionInfo.FiBDataVersion = FiBSerializationHelpers::Deserialize<int32>(ReaderStream);
	}

	// Determine the editor object version that the asset package was last serialized with
	FString PackageFilename;
	if (ensureMsgf(FPackageName::DoesPackageExist(InAssetData.PackageName.ToString(), nullptr, &PackageFilename), TEXT("FiB: Failed to map package to filename.")))
	{
		// Open a new file archive for reading
		FArchive* PackageFile = IFileManager::Get().CreateFileReader(*PackageFilename);
		if (ensureMsgf(PackageFile != nullptr, TEXT("FiB: Unable to open package to read file summary.")))
		{
			// Read the package file summary
			FPackageFileSummary PackageFileSummary;
			*PackageFile << PackageFileSummary;

			// Close the file
			delete PackageFile;

			// If an editor object version exists in the package file summary, record it
			if (const FCustomVersion* const EditorObjectVersion = PackageFileSummary.GetCustomVersionContainer().GetVersion(FEditorObjectVersion::GUID))
			{
				NewSearchData.VersionInfo.EditorObjectVersion = EditorObjectVersion->Version;
			}
		}
	}

	// Since the asset was not loaded, pull out the searchable data stored in the asset
	AddSearchDataToDatabase(MoveTemp(NewSearchData));
}

int32 FFindInBlueprintSearchManager::AddSearchDataToDatabase(FSearchData InSearchData)
{
	FName AssetPath = InSearchData.AssetPath; // Copy before we move the data into the array

	int32 ArrayIndex = SearchArray.Add(MoveTemp(InSearchData));

	// Add the asset file path to the map along with the index into the array
	SearchMap.Add(AssetPath, ArrayIndex);

	return ArrayIndex;
}

void FFindInBlueprintSearchManager::RemoveBlueprintByPath(FName InPath)
{
	int32* SearchIdx = SearchMap.Find(InPath);

	if(SearchIdx)
	{
		SearchArray[*SearchIdx].bMarkedForDeletion = true;
	}
}
void FFindInBlueprintSearchManager::OnAssetRemoved(const struct FAssetData& InAssetData)
{
	if(InAssetData.IsAssetLoaded())
	{
		RemoveBlueprintByPath(InAssetData.ObjectPath);
	}
}

void FFindInBlueprintSearchManager::OnAssetRenamed(const struct FAssetData& InAssetData, const FString& InOldName)
{
	// Renaming removes the item from the manager, it will be re-added in the OnAssetAdded event under the new name.
	if(InAssetData.IsAssetLoaded())
	{
		RemoveBlueprintByPath(FName(*InOldName));
	}
}

void FFindInBlueprintSearchManager::OnAssetLoaded(UObject* InAsset)
{
	const IBlueprintAssetHandler* Handler = FBlueprintAssetHandler::Get().FindHandler(InAsset->GetClass());
	UBlueprint* BlueprintObject = Handler ? Handler->RetrieveBlueprint(InAsset) : nullptr;

	if (BlueprintObject)
	{
		FName AssetPath = *InAsset->GetPathName();

		// Find and update the item in the search array. Searches may currently be active, this will do no harm to them

		// Confirm that the Blueprint has not been added already, this can occur during duplication of Blueprints.
		int32* IndexPtr = SearchMap.Find(AssetPath);

		// The asset registry might not have informed us of this asset yet.
		if(IndexPtr)
		{
			// That index should never have a Blueprint already, but if it does, it should be the same Blueprint!
			ensureMsgf(!SearchArray[*IndexPtr].Blueprint.IsValid() || SearchArray[*IndexPtr].Blueprint == BlueprintObject, TEXT("Blueprint in database has path %s and is being stomped by %s"), *(SearchArray[*IndexPtr].AssetPath.ToString()), *AssetPath.ToString());
			ensureMsgf(!SearchArray[*IndexPtr].Blueprint.IsValid() || SearchArray[*IndexPtr].AssetPath == AssetPath, TEXT("Blueprint in database has path %s and is being stomped by %s"), *(SearchArray[*IndexPtr].AssetPath.ToString()), *AssetPath.ToString());
			SearchArray[*IndexPtr].Blueprint = BlueprintObject;
		}

		UnindexedAssets.Remove(AssetPath);
	}
}

void FFindInBlueprintSearchManager::OnBlueprintUnloaded(UBlueprint* InBlueprint)
{
	RemoveBlueprintByPath(*InBlueprint->GetPathName());
}

void FFindInBlueprintSearchManager::OnHotReload(bool bWasTriggeredAutomatically)
{
	CachedAssetClasses.Reset();
}

FString FFindInBlueprintSearchManager::GatherBlueprintSearchMetadata(const UBlueprint* Blueprint)
{	
	FTemporarilyUseFriendlyNodeTitles TemporarilyUseFriendlyNodeTitles;

	FString SearchMetaData;

	// The search registry tags for a Blueprint are all in Json
	TSharedRef< BlueprintSearchMetaDataHelpers::TJsonFindInBlueprintStringWriter<TCondensedJsonPrintPolicy<TCHAR>> > Writer = BlueprintSearchMetaDataHelpers::TJsonFindInBlueprintStringWriter<TCondensedJsonPrintPolicy<TCHAR>>::Create( &SearchMetaData );

	TMap<FString, TMap<FString,int>> AllPaths;
	Writer->WriteObjectStart();

	// Only pull properties if the Blueprint has been compiled
	if(Blueprint->SkeletonGeneratedClass)
	{
		Writer->WriteArrayStart(FFindInBlueprintSearchTags::FiB_Properties);
		{
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				BlueprintSearchMetaDataHelpers::SaveVariableDescriptionToJson(Writer, Blueprint, Variable);
			}
		}
		Writer->WriteArrayEnd(); // Properties
	}

	// Gather all graph searchable data
	TArray< UEdGraph* > SubGraphs;

	// Gather normal event graphs
	BlueprintSearchMetaDataHelpers::GatherGraphSearchData(Writer, Blueprint, Blueprint->UbergraphPages, FFindInBlueprintSearchTags::FiB_UberGraphs, &SubGraphs);
	
	// We have interface graphs and function graphs to put into the Functions category. We cannot do them separately, so we must compile the full list
	{
		TArray<UEdGraph*> CompleteGraphList;
		CompleteGraphList.Append(Blueprint->FunctionGraphs);
		// Gather all interface graphs as functions
		for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			CompleteGraphList.Append(InterfaceDesc.Graphs);
		}
		BlueprintSearchMetaDataHelpers::GatherGraphSearchData(Writer, Blueprint, CompleteGraphList, FFindInBlueprintSearchTags::FiB_Functions, &SubGraphs);
	}

	// Gather Macros
	BlueprintSearchMetaDataHelpers::GatherGraphSearchData(Writer, Blueprint, Blueprint->MacroGraphs, FFindInBlueprintSearchTags::FiB_Macros, &SubGraphs);

	// Sub graphs are processed separately so that they do not become children in the TreeView, cluttering things up if the tree is deep
	BlueprintSearchMetaDataHelpers::GatherGraphSearchData(Writer, Blueprint, SubGraphs, FFindInBlueprintSearchTags::FiB_SubGraphs, NULL);

	// Gather all SCS components
	// If we have an SCS but don't support it, then we remove it
	if(Blueprint->SimpleConstructionScript)
	{
		// Remove any SCS variable nodes
		const TArray<USCS_Node*>& AllSCSNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		Writer->WriteArrayStart(FFindInBlueprintSearchTags::FiB_Components);
		for (TFieldIterator<UProperty> PropertyIt(Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			UProperty* Property = *PropertyIt;
			UObjectPropertyBase* Obj = Cast<UObjectPropertyBase>(Property);
			const bool bComponentProperty = Obj && Obj->PropertyClass ? Obj->PropertyClass->IsChildOf<UActorComponent>() : false;
			FName PropName = Property->GetFName();
			if(bComponentProperty && FBlueprintEditorUtils::FindSCS_Node(Blueprint, PropName) != INDEX_NONE)
			{
				FEdGraphPinType PropertyPinType;
				if(UEdGraphSchema_K2::StaticClass()->GetDefaultObject<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PropertyPinType))
				{
					Writer->WriteObjectStart();
					{
						Writer->WriteValue(FFindInBlueprintSearchTags::FiB_Name, FText::FromName(PropName));
						Writer->WriteValue(FFindInBlueprintSearchTags::FiB_IsSCSComponent, true);
						SavePinTypeToJson(Writer,  PropertyPinType);
					}
					Writer->WriteObjectEnd();
				}
			}
		}
		Writer->WriteArrayEnd(); // Components
	}

	Writer->WriteObjectEnd();
	Writer->Close();

	// Build the search metadata string for the asset tag (version + LUT + JSON)
	SearchMetaData = FiBSerializationHelpers::Serialize(FSearchDataVersionInfo::Current.FiBDataVersion, false)
		+ Writer->GetSerializedLookupTable()
		+ SearchMetaData;

	return SearchMetaData;
}

void FFindInBlueprintSearchManager::AddOrUpdateBlueprintSearchMetadata(UBlueprint* InBlueprint, bool bInForceReCache/* = false*/)
{
	// No need to update the cache in the following cases:
	//	a) Indexing is disabled.
	//	b) The Blueprint is not yet fully loaded. This ensures that we don't make attempts to re-index before load completion.
	//	c) The Blueprint was loaded for diffing. It makes search all very strange and allows you to fully open those Blueprints.
	//	d) The Blueprint was loaded/copied for PIE. These assets are temporarily created for a session and don't need to be re-indexed.
	if (!bEnableGatheringData
		|| InBlueprint->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad)
		|| InBlueprint->GetOutermost()->HasAnyPackageFlags(PKG_ForDiffing | PKG_PlayInEditor))
	{
		return;
	}

	UObject* AssetObject = GetAssetObject(InBlueprint);

	check(InBlueprint && AssetObject);

	// Allow only one thread modify the search data at a time
	FScopeLock ScopeLock(&SafeModifyCacheCriticalSection);

	FName AssetPath = *AssetObject->GetPathName();

	int32* IndexPtr = SearchMap.Find(AssetPath);
	int32 Index = 0;
	if(!IndexPtr)
	{
		FSearchData SearchData;
		SearchData.Blueprint = InBlueprint;
		SearchData.AssetPath = AssetPath;
		Index = AddSearchDataToDatabase(MoveTemp(SearchData));
	}
	else
	{
		Index = *IndexPtr;
		SearchArray[Index].Blueprint = InBlueprint; // Blueprint instance may change due to reloading
		SearchArray[Index].bMarkedForDeletion = false;
	}

	// Build the search data
	if (UProperty* ParentClassProp = InBlueprint->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBlueprint, ParentClass)))
	{
		ParentClassProp->ExportTextItem(SearchArray[Index].ParentClass, ParentClassProp->ContainerPtrToValuePtr<uint8>(InBlueprint), nullptr, InBlueprint, 0);
	}

	// During unindexed/out-of-date caching we will arrive here as a result of loading the asset, so don't remove the IsCacheInProgress() check!
	if (bInForceReCache || IsCacheInProgress() || bDisableDeferredIndexing)
	{
		// Cannot successfully gather most searchable data if there is no SkeletonGeneratedClass, so don't try, leave it as whatever it was last set to
		if (InBlueprint->SkeletonGeneratedClass != nullptr)
		{
			// Update search metadata string content
			SearchArray[Index].Value = GatherBlueprintSearchMetadata(InBlueprint);

			// Update version info stored in database to latest
			SearchArray[Index].VersionInfo = FSearchDataVersionInfo::Current;
		}

		// Remove it from the list of pending assets (if it exists)
		PendingAssets.Remove(AssetPath);
	}
	else
	{
		// Add it to the list of assets to be indexed (deferred until the next global search)
		PendingAssets.Add(AssetPath);
	}
}

void FFindInBlueprintSearchManager::BeginSearchQuery(const FStreamSearch* InSearchOriginator)
{
	if (AssetRegistryModule == nullptr)
	{
		UE_LOG(LogBlueprint, Warning, TEXT("Find-in-Blueprints was not fully initialized, possibly due to problems being initialized while saving a package. Please explicitly initialize earlier!"));
	}

	// Cannot begin a search thread while saving
	FScopeLock ScopeLock(&PauseThreadsCriticalSection);
	FScopeLock ScopeLock2(&SafeQueryModifyCriticalSection);

	ActiveSearchCounter.Increment();
	ActiveSearchQueries.FindOrAdd(InSearchOriginator) = 0;
}

bool FFindInBlueprintSearchManager::ContinueSearchQuery(const FStreamSearch* InSearchOriginator, FSearchData& OutSearchData)
{
	// Check if the thread has been told to pause, this occurs for the Garbage Collector and for saving to disk
	if(bIsPausing == true)
	{
		// Pause all searching, the GC is running and we will also be saving the database
		ActiveSearchCounter.Decrement();
		FScopeLock ScopeLock(&PauseThreadsCriticalSection);
		ActiveSearchCounter.Increment();
	}

	// Must lock this behind a critical section to ensure that no other thread is accessing it at the same time
	FScopeLock ScopeLock(&SafeQueryModifyCriticalSection);

	if(int32* SearchIdxPtr = ActiveSearchQueries.Find(InSearchOriginator))
	{
		int32& SearchIdx = *SearchIdxPtr;
		while(SearchIdx < SearchArray.Num())
		{
			// If the Blueprint is not marked for deletion, and the asset is valid, we will check to see if we want to refresh the searchable data.
			if( SearchArray[SearchIdx].bMarkedForDeletion || (SearchArray[SearchIdx].Blueprint.IsValid() && SearchArray[SearchIdx].Blueprint->IsPendingKill()) )
			{
				// Mark it for deletion, it will be removed on next save
				SearchArray[SearchIdx].bMarkedForDeletion = true;
			}
			else
			{
				// If there is FiB data, parse it into an ImaginaryBlueprint
				if (SearchArray[SearchIdx].Value.Len() > 0)
				{
					SearchArray[SearchIdx].ImaginaryBlueprint = MakeShareable(new FImaginaryBlueprint(FPaths::GetBaseFilename(SearchArray[SearchIdx].AssetPath.ToString()), SearchArray[SearchIdx].AssetPath.ToString(), SearchArray[SearchIdx].ParentClass, SearchArray[SearchIdx].Interfaces, SearchArray[SearchIdx].Value, SearchArray[SearchIdx].VersionInfo));
					SearchArray[SearchIdx].Value.Empty();
				}

 				OutSearchData = SearchArray[SearchIdx++];
				return true;
			}

			++SearchIdx;
		}
	}

	ActiveSearchQueries.Remove(InSearchOriginator);
	ActiveSearchCounter.Decrement();

	return false;
}

void FFindInBlueprintSearchManager::EnsureSearchQueryEnds(const class FStreamSearch* InSearchOriginator)
{
	// Must lock this behind a critical section to ensure that no other thread is accessing it at the same time
	FScopeLock ScopeLock(&SafeQueryModifyCriticalSection);
	int32* SearchIdxPtr = ActiveSearchQueries.Find(InSearchOriginator);

	// If the search thread is still considered active, remove it
	if(SearchIdxPtr)
	{
		ActiveSearchQueries.Remove(InSearchOriginator);
		ActiveSearchCounter.Decrement();
	}
}

float FFindInBlueprintSearchManager::GetPercentComplete(const FStreamSearch* InSearchOriginator) const
{
	FScopeLock ScopeLock(&SafeQueryModifyCriticalSection);
	const int32* SearchIdxPtr = ActiveSearchQueries.Find(InSearchOriginator);

	float ReturnPercent = 0.0f;

	if(SearchIdxPtr)
	{
		ReturnPercent = (float)*SearchIdxPtr / (float)SearchArray.Num();
	}

	return ReturnPercent;
}

const FSearchData* FFindInBlueprintSearchManager::QuerySingleBlueprint(UBlueprint* InBlueprint, bool bInRebuildSearchData)
{
	// AddOrUpdateBlueprintSearchMetadata would fail to cache any data for a Blueprint loaded specifically for diffing, but the bigger question
	// here in this function is how you are doing a search specifically for data within this Blueprint. This function is limited to be called
	// only when querying within the specific Blueprint (somehow opened a diff Blueprint) and when gathering the Blueprint's tags (usually for saving)
	const bool bIsDiffingBlueprint = InBlueprint->GetOutermost()->HasAnyPackageFlags(PKG_ForDiffing);
	if (!bIsDiffingBlueprint)
	{
		if (bInRebuildSearchData)
		{
			// Update the Blueprint, make sure it is fully up-to-date
			AddOrUpdateBlueprintSearchMetadata(InBlueprint, true);
		}

		UObject* AssetObject = GetAssetObject(InBlueprint);
		check(AssetObject);
		FName Key = *AssetObject->GetPathName();

		int32* ArrayIdx = SearchMap.Find(Key);
		if (ArrayIdx)
		{
			checkf(*ArrayIdx < SearchArray.Num(),
				TEXT("ArrayIdx:%d, SearchArray.Num():%d"),
				*ArrayIdx,
				SearchArray.Num());

			return &SearchArray[*ArrayIdx];
		}
		else if(bInRebuildSearchData)
		{
			// Warn here, since we make sure to refresh the search data for this Blueprint when doing the search, and we expect that it should have
			// been indexed. Note that there are some situations in which we never index a Blueprint asset (@see AddOrUpdateBlueprintSearchMetadata).
			UE_LOG(LogBlueprint, Warning, TEXT("Attempted to query a Blueprint (%s) that was not re-indexed even after rebuilding. No results can be returned."), *InBlueprint->GetPathName());
		}
	}
	else
	{
		// Also warn here as we do not index diff-only packages.
		UE_LOG(LogBlueprint, Warning, TEXT("Attempted to query an old Blueprint package opened for diffing!"));
	}
	return nullptr;
}

void FFindInBlueprintSearchManager::PauseFindInBlueprintSearch()
{
	// Lock the critical section and flag that threads need to pause, they will pause when they can
	PauseThreadsCriticalSection.Lock();
	bIsPausing = true;

	// It is UNSAFE to lock any other critical section here, threads need them to finish a cycle of searching. Next cycle they will pause

	// Wait until all threads have come to a stop, it won't take long
	while(ActiveSearchCounter.GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.1f);
	}
}

void FFindInBlueprintSearchManager::UnpauseFindInBlueprintSearch()
{
	// Before unpausing, we clean the cache of any excess data to keep it from bloating in size
	CleanCache();
	bIsPausing = false;

	// Release the threads to continue searching.
	PauseThreadsCriticalSection.Unlock();
}

void FFindInBlueprintSearchManager::CleanCache()
{
	// *NOTE* SaveCache is a thread safe operation by design, all searching threads are paused during the operation so there is no critical section locking

	// We need to cache where the active queries are so that we can put them back in a safe and expected position
	TMap< const FStreamSearch*, FName > CacheQueries;
	for( auto It = ActiveSearchQueries.CreateIterator() ; It ; ++It )
	{
	 	const FStreamSearch* ActiveSearch = It.Key();
	 	check(ActiveSearch);
	 	{
			FSearchData SearchData;
	 		ContinueSearchQuery(ActiveSearch, SearchData);

			FName CachePath = SearchData.AssetPath;
	 		CacheQueries.Add(ActiveSearch, CachePath);
	 	}
	}

	TMap<FName, int32> NewSearchMap;
	TArray<FSearchData> NewSearchArray;

	for(auto& SearchValuePair : SearchMap)
	{
		// Here it builds the new map/array, clean of deleted content.

		// If the database item is not marked for deletion and not pending kill (if loaded), keep it in the database
		if( !SearchArray[SearchValuePair.Value].bMarkedForDeletion && !(SearchArray[SearchValuePair.Value].Blueprint.IsValid() && SearchArray[SearchValuePair.Value].Blueprint->IsPendingKill()) )
		{
			// Build the new map/array
			NewSearchMap.Add(SearchValuePair.Key, NewSearchArray.Add(MoveTemp(SearchArray[SearchValuePair.Value])) );
		}
		else
		{
			// Remove it from the list of loaded assets that require indexing
			PendingAssets.Remove(SearchArray[SearchValuePair.Value].AssetPath);

			// Level Blueprints are destroyed when you open a new level, we need to re-add it as an unloaded asset so long as they were not marked for deletion
			if(!SearchArray[SearchValuePair.Value].bMarkedForDeletion && FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
			{
				SearchArray[SearchValuePair.Value].Blueprint = nullptr;

				AssetRegistryModule = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

				// The asset was not user deleted, so this should usually find the asset. New levels can be deleted if they were not saved
				FAssetData AssetData = AssetRegistryModule->Get().GetAssetByObjectPath(SearchArray[SearchValuePair.Value].AssetPath);
				if(AssetData.IsValid())
				{
					FAssetDataTagMapSharedView::FFindTagResult ResultLegacy = AssetData.TagsAndValues.FindTag("FiB");
					if (ResultLegacy.IsSet())
					{
						SearchArray[SearchValuePair.Value].Value = ResultLegacy.GetValue();
					}
					// Build the new map/array
					NewSearchMap.Add(SearchValuePair.Key, NewSearchArray.Add(SearchArray[SearchValuePair.Value]) );
				}
			}
		}
	}

	SearchMap = MoveTemp( NewSearchMap );
	SearchArray = MoveTemp( NewSearchArray );

	// After the search, we have to place the active search queries where they belong
	for( auto& CacheQuery : CacheQueries )
	{
	 	int32 NewMappedIndex = 0;
	 	// Is the CachePath is valid? Otherwise we are at the end and there are no more search results, leave the query there so it can handle shutdown on it's own
	 	if(!CacheQuery.Value.IsNone())
	 	{
	 		int32* NewMappedIndexPtr = SearchMap.Find(CacheQuery.Value);
	 		check(NewMappedIndexPtr);
	 
	 		NewMappedIndex = *NewMappedIndexPtr;
	 	}
	 	else
	 	{
	 		NewMappedIndex = SearchArray.Num();
	 	}
	 
		// Update the active search to the new index of where it is at in the search
	 	*(ActiveSearchQueries.Find(CacheQuery.Key)) = NewMappedIndex;
	}
}

void FFindInBlueprintSearchManager::BuildCache()
{
	AssetRegistryModule = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray< FAssetData > BlueprintAssets;
	FARFilter ClassFilter;
	ClassFilter.bRecursiveClasses = true;

	for (FName ClassName : FBlueprintAssetHandler::Get().GetRegisteredClassNames())
	{
		ClassFilter.ClassNames.Add(ClassName);
	}

	AssetRegistryModule->Get().GetAssets(ClassFilter, BlueprintAssets);
	
	for( FAssetData& Asset : BlueprintAssets )
	{
		OnAssetAdded(Asset);
	}
}

FText FFindInBlueprintSearchManager::ConvertHexStringToFText(FString InHexString)
{
	TArray<uint8> SerializedData;
	SerializedData.AddZeroed(InHexString.Len());

	HexToBytes(InHexString, SerializedData.GetData());

	FText ResultText;

	FMemoryReader Ar(SerializedData);
	Ar << ResultText;
	Ar.Close();

	return ResultText;
}

FString FFindInBlueprintSearchManager::ConvertFTextToHexString(FText InValue)
{
	TArray<uint8> SerializedData;
	FMemoryWriter Ar(SerializedData);

	Ar << InValue;
	Ar.Close();

	return BytesToHex(SerializedData.GetData(), SerializedData.Num());
}

void FFindInBlueprintSearchManager::OnCacheAllUnindexedAssets(bool bInSourceControlActive, bool bInCheckoutAndSave)
{
	// Multiple threads can be adding to this at the same time
	FScopeLock ScopeLock(&SafeModifyCacheCriticalSection);

	// We need to check validity first in case the user has closed the initiating FiB tab before responding to the source control login dialog (which is modeless).
	if (CachingObject)
	{
		if(bInSourceControlActive && bInCheckoutAndSave)
		{
			TArray<FString> UncachedAssetStrings;
			const TArray<FName>& TotalUncachedAssets = CachingObject->GetUncachedAssetList();
		
			UncachedAssetStrings.Reserve(TotalUncachedAssets.Num());
			for (const FName& UncachedAsset : TotalUncachedAssets)
			{
				UncachedAssetStrings.Add(UncachedAsset.ToString());
			}
			FEditorFileUtils::CheckoutPackages(UncachedAssetStrings);
		}

		// Start the cache process.
		CachingObject->Start();
	}
}

void FFindInBlueprintSearchManager::CacheAllAssets(TWeakPtr< SFindInBlueprints > InSourceWidget, const FFindInBlueprintCachingOptions& InOptions)
{
	// Do not start another caching process if one is in progress
	if(!IsCacheInProgress())
	{
		CurrentCacheOpType = InOptions.OpType;

		if (CurrentCacheOpType == EFiBCacheOpType::CachePendingAssets)
		{
			FCacheAllBlueprintsTickableObject::FCacheParams CacheParams;
			CacheParams.OnFinished = InOptions.OnFinished;
			CacheParams.OnCached.BindLambda([this](FName InAssetName)
			{
				// Ensure removal in case it did not get re-indexed for some reason (e.g. not loaded, not a Blueprint asset, etc.)
				PendingAssets.Remove(InAssetName);
			});

			// Determine if PIE is active - in that case we're potentially streaming assets in at random intervals, so just hide the progress UI while re-indexing those assets
			const bool bIsPIESimulating = (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld);

			// Display progress during a re-indexing operation only if we have multiple assets to process (e.g. avoid showing after compiling a single asset) and we're not in PIE
			if ((PendingAssets.Num() > 1) && !bIsPIESimulating)
			{
				CacheParams.OpFlags |= EFiBCacheOpFlags::ShowProgress;
			}

			// Keep popup notifications hidden during this operation
			CacheParams.OpFlags |= EFiBCacheOpFlags::HideNotifications;

			// Keep track of which global FiB context started the operation (if any)
			SourceCachingWidget = InSourceWidget;

			CachingObject = new FCacheAllBlueprintsTickableObject(PendingAssets, CacheParams);

			// Immediately start the operation (non-interactive)
			check(CachingObject);
			CachingObject->Start();
		}
		else
		{
			TArray<FName> BlueprintsToUpdate;
			// Add any out-of-date Blueprints to the list
			for (FSearchData SearchData : SearchArray)
			{
				if ((SearchData.Value.Len() != 0 || SearchData.ImaginaryBlueprint.IsValid()) && SearchData.VersionInfo.FiBDataVersion < InOptions.MinimiumVersionRequirement)
				{
					BlueprintsToUpdate.Add(SearchData.AssetPath);
				}
			}

			FText DialogTitle = LOCTEXT("ConfirmIndexAll_Title", "Indexing All");
			FFormatNamedArguments Args;
			Args.Add(TEXT("PackageCount"), UnindexedAssets.Num() + BlueprintsToUpdate.Num());

			FText DialogDisplayText;

			if (UnindexedAssets.Num() && BlueprintsToUpdate.Num())
			{
				Args.Add(TEXT("PackageCount"), UnindexedAssets.Num() + BlueprintsToUpdate.Num());
				Args.Add(TEXT("UnindexedCount"), UnindexedAssets.Num());
				Args.Add(TEXT("OutOfDateCount"), BlueprintsToUpdate.Num());
				DialogDisplayText = FText::Format(LOCTEXT("CacheAllConfirmationMessage_UnindexedAndOutOfDate", "This process can take a long time and the editor may become unresponsive; there are {PackageCount} ({UnindexedCount} Unindexed/{OutOfDateCount} Out-of-Date) Blueprints to load. \
																					\n\nWould you like to checkout, load, and save all Blueprints to make this indexing permanent? Otherwise, all Blueprints will still be loaded but you will be required to re-index the next time you start the editor!"), Args);
			}
			else if (UnindexedAssets.Num() && BlueprintsToUpdate.Num() == 0)
			{
				DialogDisplayText = FText::Format(LOCTEXT("CacheAllConfirmationMessage_UnindexedOnly", "This process can take a long time and the editor may become unresponsive; there are {PackageCount} unindexed Blueprints to load. \
																					 \n\nWould you like to checkout, load, and save all Blueprints to make this indexing permanent? Otherwise, all Blueprints will still be loaded but you will be required to re-index the next time you start the editor!"), Args);
			}
			else if (UnindexedAssets.Num() == 0 && BlueprintsToUpdate.Num())
			{
				DialogDisplayText = FText::Format(LOCTEXT("CacheAllConfirmationMessage_OutOfDateOnly", "This process can take a long time and the editor may become unresponsive; there are {PackageCount} out-of-date Blueprints to load. \
																					 \n\nWould you like to checkout, load, and save all Blueprints to make this indexing permanent? Otherwise, all Blueprints will still be loaded but you will be required to re-index the next time you start the editor!"), Args);
			}

			const EAppReturnType::Type ReturnValue = FMessageDialog::Open(EAppMsgType::YesNoCancel, DialogDisplayText, &DialogTitle);

			// If Yes is chosen, checkout and save all Blueprints, if No is chosen, only load all Blueprints
			if (ReturnValue != EAppReturnType::Cancel)
			{
				FailedToCachePaths.Empty();

				TSet<FName> TempUncachedAssets;
				TempUncachedAssets.Append(UnindexedAssets);
				TempUncachedAssets.Append(BlueprintsToUpdate);

				const bool bCheckOutAndSave = (ReturnValue == EAppReturnType::Yes);
				FCacheAllBlueprintsTickableObject::FCacheParams CacheParams;
				CacheParams.OpFlags = EFiBCacheOpFlags::ShowProgress | EFiBCacheOpFlags::AllowUserCancel;
				if (bCheckOutAndSave)
				{
					CacheParams.OpFlags |= EFiBCacheOpFlags::CheckOutAndSave;
				}
				CacheParams.OnFinished = InOptions.OnFinished;
				CachingObject = new FCacheAllBlueprintsTickableObject(MoveTemp(TempUncachedAssets), CacheParams);

				const bool bIsSourceControlEnabled = ISourceControlModule::Get().IsEnabled();
				if (!bIsSourceControlEnabled && bCheckOutAndSave)
				{
					// Offer to start up Source Control
					ISourceControlModule::Get().ShowLoginDialog(FSourceControlLoginClosed::CreateRaw(this, &FFindInBlueprintSearchManager::OnCacheAllUnindexedAssets, bCheckOutAndSave), ELoginWindowMode::Modeless, EOnLoginWindowStartup::PreserveProvider);
				}
				else
				{
					OnCacheAllUnindexedAssets(bIsSourceControlEnabled, bCheckOutAndSave);
				}

				SourceCachingWidget = InSourceWidget;
			}
		}
	}
}

void FFindInBlueprintSearchManager::CancelCacheAll(SFindInBlueprints* InFindInBlueprintWidget)
{
	if(IsCacheInProgress() && ((SourceCachingWidget.IsValid() && SourceCachingWidget.Pin().Get() == InFindInBlueprintWidget) || !SourceCachingWidget.IsValid()))
	{
		CachingObject->OnCancelCaching(!SourceCachingWidget.IsValid());
		SourceCachingWidget.Reset();
	}
}

int32 FFindInBlueprintSearchManager::GetCurrentCacheIndex() const
{
	int32 CachingIndex = 0;
	if(CachingObject)
	{
		CachingIndex = CachingObject->GetCurrentCacheIndex();
	}

	return CachingIndex;
}

FName FFindInBlueprintSearchManager::GetCurrentCacheBlueprintName() const
{
	FName CachingBPName;
	if(CachingObject)
	{
		CachingBPName = CachingObject->GetCurrentCacheBlueprintName();
	}

	return CachingBPName;
}

float FFindInBlueprintSearchManager::GetCacheProgress() const
{
	float ReturnCacheValue = 1.0f;

	if(CachingObject)
	{
		ReturnCacheValue = CachingObject->GetCacheProgress();
	}

	return ReturnCacheValue;
}

int32 FFindInBlueprintSearchManager::GetNumberPendingAssets() const
{
	return PendingAssets.Num();
}

int32 FFindInBlueprintSearchManager::GetNumberUnindexedAssets() const
{
	return UnindexedAssets.Num();
}

int32 FFindInBlueprintSearchManager::GetNumberUncachedAssets() const
{
	if (CachingObject)
	{
		return CachingObject->GetUncachedAssetCount();
	}
	
	return 0;
}

void FFindInBlueprintSearchManager::StartedCachingBlueprints(EFiBCacheOpFlags InCacheOpFlags)
{
	// Invoke the callback on any open global widgets
	for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
	{
		if (FindResultsPtr.IsValid())
		{
			FindResultsPtr.Pin()->OnCacheStarted(CurrentCacheOpType, InCacheOpFlags);
		}
	}
}

void FFindInBlueprintSearchManager::FinishedCachingBlueprints(int32 InNumberCached, TSet<FName>& InFailedToCacheList)
{
	// Multiple threads could be adding to this at the same time
	FScopeLock ScopeLock(&SafeModifyCacheCriticalSection);

	// Update the list of cache failures
	FailedToCachePaths = InFailedToCacheList;

	// Remove any failed attempts from the pending list
	if (InFailedToCacheList.Num() > 0)
	{
		PendingAssets = PendingAssets.Difference(InFailedToCacheList);
	}

	// Invoke the completion callback on any open widgets that are not the initiating one (e.g. to hide progress bar)
	for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
	{
		if (FindResultsPtr.IsValid() && FindResultsPtr != SourceCachingWidget)
		{
			FindResultsPtr.Pin()->OnCacheComplete(CurrentCacheOpType);
		}
	}

	// Invoke the completion callback on the initiating widget only if necessary (i.e. only if it doesn't already delegate it)
	if(SourceCachingWidget.IsValid() && !CachingObject->HasPostCacheWork())
	{
		SourceCachingWidget.Pin()->OnCacheComplete(CurrentCacheOpType);
	}

	// Reset the reference to the widget that initiated the caching operation
	SourceCachingWidget.Reset();

	// Delete the object and NULL it out so we can do it again in the future if needed (if it was canceled)
	delete CachingObject;
	CachingObject = nullptr;
}

bool FFindInBlueprintSearchManager::IsCacheInProgress() const
{
	return CachingObject != nullptr;
}

bool FFindInBlueprintSearchManager::IsUnindexedCacheInProgress() const
{
	return IsCacheInProgress() && CurrentCacheOpType == EFiBCacheOpType::CacheUnindexedAssets;
}

TSharedPtr< FJsonObject > FFindInBlueprintSearchManager::ConvertJsonStringToObject(FSearchDataVersionInfo InVersionInfo, FString InJsonString, TMap<int32, FText>& OutFTextLookupTable)
{
	/** The searchable data is more complicated than a Json string, the Json being the main searchable body that is parsed. Below is a diagram of the full data:
	 *  | int32 "Version" | int32 "Size" | TMap "Lookup Table" | Json String |
	 *
	 * Version: Version of the FiB data, which may impact searching
	 * Size: The size of the TMap in bytes
	 * Lookup Table: The Json's identifiers and string values are in Hex strings and stored in a TMap, the Json stores these values as ints and uses them as the Key into the TMap
	 * Json String: The Json string to be deserialized in full
	 */
	TArray<uint8> DerivedData;

	// SearchData is currently the full string
	// We want to first extract the size of the TMap we will be serializing
	int32 SizeOfData;
	FBufferReader ReaderStream((void*)*InJsonString, InJsonString.Len() * sizeof(TCHAR), false);

	// If the stream is versioned, read past the version info
	if (InVersionInfo.FiBDataVersion > EFiBVersion::FIB_VER_BASE)
	{
		// Read the FiB search data version
		const int32 Version = FiBSerializationHelpers::Deserialize<int32>(ReaderStream);

		// Check that the deserialized version matches up with what's recorded in the search database
		ensureMsgf(Version == InVersionInfo.FiBDataVersion, TEXT("FiB: JSON stream data does not match search data version from database. This is unexpected."));
	}

	// Configure the JSON stream with the proper object version for FText serialization when reading the LUT
	ReaderStream.SetCustomVersion(FEditorObjectVersion::GUID, InVersionInfo.EditorObjectVersion, TEXT("Dev-Editor"));

 	// Read, as a byte string, the number of characters composing the Lookup Table for the Json.
	SizeOfData = FiBSerializationHelpers::Deserialize<int32>(ReaderStream);

 	// With the size of the TMap in hand, let's serialize JUST that (as a byte string)
	TMap<int32, FText> LookupTable;
	OutFTextLookupTable = LookupTable = FiBSerializationHelpers::Deserialize< TMap<int32, FText> >(ReaderStream, SizeOfData);

	// The original BufferReader should be positioned at the Json
	TSharedPtr< FJsonObject > JsonObject = NULL;
	TSharedRef< TJsonReader<> > Reader = BlueprintSearchMetaDataHelpers::SearchMetaDataReader::Create( &ReaderStream, LookupTable );
	FJsonSerializer::Deserialize( Reader, JsonObject );

	return JsonObject;
}

void FFindInBlueprintSearchManager::GlobalFindResultsClosed(const TSharedRef<SFindInBlueprints>& FindResults)
{
	for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
	{
		if (FindResultsPtr.Pin() == FindResults)
		{
			GlobalFindResults.Remove(FindResultsPtr);

			if (CachingObject && SourceCachingWidget == FindResultsPtr)
			{
				SourceCachingWidget.Reset();
			}
			break;
		}
	}
}

FText FFindInBlueprintSearchManager::GetGlobalFindResultsTabLabel(int32 TabIdx)
{
	int32 NumOpenGlobalFindResultsTabs = 0;
	for (int32 i = GlobalFindResults.Num() - 1; i >= 0; --i)
	{
		if (GlobalFindResults[i].IsValid())
		{
			++NumOpenGlobalFindResultsTabs;
		}
		else
		{
			GlobalFindResults.RemoveAt(i);
		}
	}

	if (NumOpenGlobalFindResultsTabs > 1 || TabIdx > 0)
	{
		return FText::Format(LOCTEXT("GlobalFindResultsTabNameWithIndex", "Find in Blueprints {0}"), FText::AsNumber(TabIdx + 1));
	}
	else
	{
		return LOCTEXT("GlobalFindResultsTabName", "Find in Blueprints");
	}
}

TSharedRef<SDockTab> FFindInBlueprintSearchManager::SpawnGlobalFindResultsTab(const FSpawnTabArgs& SpawnTabArgs, int32 TabIdx)
{
	TAttribute<FText> Label = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FFindInBlueprintSearchManager::GetGlobalFindResultsTabLabel, TabIdx));

	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(Label)
		.ToolTipText(LOCTEXT("GlobalFindResultsTabTooltip", "Search for a string in all Blueprint assets."));

	TSharedRef<SFindInBlueprints> FindResults = SNew(SFindInBlueprints)
		.bIsSearchWindow(false)
		.ContainingTab(NewTab);

	GlobalFindResults.Add(FindResults);

	NewTab->SetContent(FindResults);

	return NewTab;
}

TSharedPtr<SFindInBlueprints> FFindInBlueprintSearchManager::OpenGlobalFindResultsTab()
{
	TSet<FName> OpenGlobalTabIDs;

	for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
	{
		TSharedPtr<SFindInBlueprints> FindResults = FindResultsPtr.Pin();
		if (FindResults.IsValid())
		{
			OpenGlobalTabIDs.Add(FindResults->GetHostTabId());
		}
	}

	for (int32 Idx = 0; Idx < ARRAY_COUNT(GlobalFindResultsTabIDs); ++Idx)
	{
		const FName GlobalTabId = GlobalFindResultsTabIDs[Idx];
		if (!OpenGlobalTabIDs.Contains(GlobalTabId))
		{
			TSharedRef<SDockTab> NewTab = FGlobalTabmanager::Get()->InvokeTab(GlobalTabId);
			return StaticCastSharedRef<SFindInBlueprints>(NewTab->GetContent());
		}
	}

	return TSharedPtr<SFindInBlueprints>();
}

TSharedPtr<SFindInBlueprints> FFindInBlueprintSearchManager::GetGlobalFindResults()
{
	TSharedPtr<SFindInBlueprints> FindResultsToUse;

	for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
	{
		TSharedPtr<SFindInBlueprints> FindResults = FindResultsPtr.Pin();
		if (FindResults.IsValid() && !FindResults->IsLocked())
		{
			FindResultsToUse = FindResults;
			break;
		}
	}

	if (FindResultsToUse.IsValid())
	{
		FGlobalTabmanager::Get()->InvokeTab(FindResultsToUse->GetHostTabId());
	}
	else
	{
		FindResultsToUse = OpenGlobalFindResultsTab();
	}

	return FindResultsToUse;
}

void FFindInBlueprintSearchManager::EnableGlobalFindResults(bool bEnable)
{
	const TSharedRef<FGlobalTabmanager>& GlobalTabManager = FGlobalTabmanager::Get();

	if (bEnable)
	{
		// Register the spawners for all global Find Results tabs
		const FSlateIcon GlobalFindResultsIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.FindResults");
		GlobalFindResultsMenuItem = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
			LOCTEXT("WorkspaceMenu_GlobalFindResultsCategory", "Find in Blueprints"),
			LOCTEXT("GlobalFindResultsMenuTooltipText", "Find references to functions, events and variables in all Blueprints."),
			GlobalFindResultsIcon,
			true);

		for (int32 TabIdx = 0; TabIdx < ARRAY_COUNT(GlobalFindResultsTabIDs); TabIdx++)
		{
			const FName TabID = GlobalFindResultsTabIDs[TabIdx];
			if (!GlobalTabManager->HasTabSpawner(TabID))
			{
				const FText DisplayName = FText::Format(LOCTEXT("GlobalFindResultsDisplayName", "Find in Blueprints {0}"), FText::AsNumber(TabIdx + 1));

				GlobalTabManager->RegisterNomadTabSpawner(TabID, FOnSpawnTab::CreateRaw(this, &FFindInBlueprintSearchManager::SpawnGlobalFindResultsTab, TabIdx))
					.SetDisplayName(DisplayName)
					.SetIcon(GlobalFindResultsIcon)
					.SetGroup(GlobalFindResultsMenuItem.ToSharedRef());
			}
		}
	}
	else
	{
		// Close all Global Find Results tabs when turning the feature off, since these may not get closed along with the Blueprint Editor contexts above.
		TSet<TSharedPtr<SFindInBlueprints>> FindResultsToClose;

		for (TWeakPtr<SFindInBlueprints> FindResultsPtr : GlobalFindResults)
		{
			TSharedPtr<SFindInBlueprints> FindResults = FindResultsPtr.Pin();
			if (FindResults.IsValid())
			{
				FindResultsToClose.Add(FindResults);
			}
		}

		for (TSharedPtr<SFindInBlueprints> FindResults : FindResultsToClose)
		{
			FindResults->CloseHostTab();
		}

		GlobalFindResults.Empty();

		for (int32 TabIdx = 0; TabIdx < ARRAY_COUNT(GlobalFindResultsTabIDs); TabIdx++)
		{
			const FName TabID = GlobalFindResultsTabIDs[TabIdx];
			if (GlobalTabManager->HasTabSpawner(TabID))
			{
				GlobalTabManager->UnregisterNomadTabSpawner(TabID);
			}
		}

		if (GlobalFindResultsMenuItem.IsValid())
		{
			WorkspaceMenu::GetMenuStructure().GetToolsCategory()->RemoveItem(GlobalFindResultsMenuItem.ToSharedRef());
			GlobalFindResultsMenuItem.Reset();
		}
	}
}

void FFindInBlueprintSearchManager::CloseOrphanedGlobalFindResultsTabs(TSharedPtr<class FTabManager> TabManager)
{
	if (TabManager.IsValid())
	{
		for (int32 TabIdx = 0; TabIdx < ARRAY_COUNT(GlobalFindResultsTabIDs); TabIdx++)
		{
			const FName TabID = GlobalFindResultsTabIDs[TabIdx];
			if (!FGlobalTabmanager::Get()->HasTabSpawner(TabID))
			{
				TSharedPtr<SDockTab> OrphanedTab = TabManager->FindExistingLiveTab(FTabId(TabID));
				if (OrphanedTab.IsValid())
				{
					OrphanedTab->RequestCloseTab();
				}
			}
		}
	}
}

void FFindInBlueprintSearchManager::Tick(float DeltaTime)
{
	if(IsCacheInProgress())
	{
		check(CachingObject);
		CachingObject->Tick(DeltaTime);
	}
	else if (PendingAssets.Num() > 0)
	{
		// Kick off a re-indexing operation to update the cache
		FFindInBlueprintCachingOptions CachingOptions;
		CachingOptions.OpType = EFiBCacheOpType::CachePendingAssets;
		CacheAllAssets(nullptr, CachingOptions);
	}
}

bool FFindInBlueprintSearchManager::IsTickable() const
{
	// Tick only if we have an active caching operation or if we have pending assets and an open FiB context
	return IsCacheInProgress() || (PendingAssets.Num() > 0 && GlobalFindResults.Num() > 0);
}

TStatId FFindInBlueprintSearchManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FFindInBlueprintSearchManager, STATGROUP_Tickables);
}

#undef LOCTEXT_NAMESPACE
