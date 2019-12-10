// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "HttpLibraryEnums.generated.h"

UENUM(BlueprintType, meta = (DisplayName = "Request Method"))
enum class EHttpLibraryRequestMethod : uint8
{
	GET		UMETA(DisplayName="GET"),
	POST	UMETA(DisplayName="POST"),
	PUT		UMETA(DisplayName="PUT"),
	PATCH	UMETA(DisplayName="PATCH"),
	DELETE	UMETA(DisplayName="DELETE"),
	HEAD	UMETA(DisplayName="HEAD"),
	CONNECT	UMETA(DisplayName="CONNECT"),
	OPTIONS	UMETA(DisplayName="OPTIONS"),
	TRACE	UMETA(DisplayName="TRACE")
};

UENUM(BlueprintType, meta = (DisplayName = "Content Type"))
enum class EHttpLibraryContentType : uint8
{
	Default	UMETA(DisplayName="*"),
	TXT		UMETA(DisplayName="text/plain"),
	HTML	UMETA(DisplayName="text/html"),
	CSS		UMETA(DisplayName="text/css"),
	CSV		UMETA(DisplayName="text/csv"),
	JSON	UMETA(DisplayName="application/json"),
	JS		UMETA(DisplayName="application/javascript"),
	RTF		UMETA(DisplayName="application/rtf"),
	XML		UMETA(DisplayName="application/xml"),
	XHTML	UMETA(DisplayName="application/xhtml+xml"),
	BIN		UMETA(DisplayName="application/octet-stream")
};
