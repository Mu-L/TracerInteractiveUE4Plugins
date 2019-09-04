// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class UDataTable;
enum class EDataTableExportFlags : uint8;

template <class CharType> struct TPrettyJsonPrintPolicy;

// forward declare JSON writer
template <class CharType>
struct TPrettyJsonPrintPolicy;
template <class CharType, class PrintPolicy>
class TJsonWriter;

namespace DataTableJSONUtils
{
	/** Returns what string is used as the key/name field for a data table */
	FString GetKeyFieldName(const UDataTable& InDataTable);
}

#if WITH_EDITOR

class FDataTableExporterJSON
{
public:
	typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FDataTableJsonWriter;

	FDataTableExporterJSON(const EDataTableExportFlags InDTExportFlags, FString& OutExportText);

	FDataTableExporterJSON(const EDataTableExportFlags InDTExportFlags, TSharedRef<FDataTableJsonWriter> InJsonWriter);

	~FDataTableExporterJSON();

	/** Writes the data table out as an array of objects */
	bool WriteTable(const UDataTable& InDataTable);

	/** Writes the data table out as a named object with each row being a sub value on that object */
	bool WriteTableAsObject(const UDataTable& InDataTable);

	/** Writes out a single row */
	bool WriteRow(const UScriptStruct* InRowStruct, const void* InRowData, const FString* FieldToSkip = nullptr);

	/** Writes the contents of a single row */
	bool WriteStruct(const UScriptStruct* InStruct, const void* InStructData, const FString* FieldToSkip = nullptr);

private:
	bool WriteStructEntry(const void* InRowData, const UProperty* InProperty, const void* InPropertyData);

	bool WriteContainerEntry(const UProperty* InProperty, const void* InPropertyData, const FString* InIdentifier = nullptr);

	EDataTableExportFlags DTExportFlags;
	TSharedRef<FDataTableJsonWriter> JsonWriter;
	bool bJsonWriterNeedsClose;
};

#endif // WITH_EDITOR

class FDataTableImporterJSON
{
public:
	FDataTableImporterJSON(UDataTable& InDataTable, const FString& InJSONData, TArray<FString>& OutProblems);

	~FDataTableImporterJSON();

	bool ReadTable();

private:
	bool ReadRow(const TSharedRef<FJsonObject>& InParsedTableRowObject, const int32 InRowIdx);

	bool ReadStruct(const TSharedRef<FJsonObject>& InParsedObject, UScriptStruct* InStruct, const FName InRowName, void* InStructData);

	bool ReadStructEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const void* InRowData, UProperty* InProperty, void* InPropertyData);

	bool ReadContainerEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const int32 InArrayEntryIndex, UProperty* InProperty, void* InPropertyData);

	UDataTable* DataTable;
	const FString& JSONData;
	TArray<FString>& ImportProblems;
};

