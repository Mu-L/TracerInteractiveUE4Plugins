// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "JsonLibraryEnums.generated.h"

UENUM(BlueprintType, meta = (DisplayName = "JSON Type"))
enum class EJsonLibraryType : uint8
{
	Invalid		UMETA(DisplayName="Invalid"),
	Null		UMETA(DisplayName="Null"),
	Object 		UMETA(DisplayName="Object"),
	Array		UMETA(DisplayName="Array"),
	Boolean		UMETA(DisplayName="Boolean"),
	Number		UMETA(DisplayName="Number"),
	String		UMETA(DisplayName="String")
};

UENUM(BlueprintType, meta = (DisplayName = "JSON Notify"))
enum class EJsonLibraryNotifyAction : uint8
{
	None	UMETA(DisplayName="None"),
	Added	UMETA(DisplayName="Added"),
	Removed	UMETA(DisplayName="Removed"),
	Changed	UMETA(DisplayName="Changed"),
	Reset	UMETA(DisplayName="Reset")
};
