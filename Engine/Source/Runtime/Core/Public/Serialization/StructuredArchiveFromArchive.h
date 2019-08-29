// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/StructuredArchive.h"

class CORE_API FStructuredArchiveFromArchive
{
public:

	FStructuredArchiveFromArchive(FArchive& Ar)
		: Formatter(Ar)
		, StructuredArchive(Formatter)
		, Slot(StructuredArchive.Open())
	{
	}

	FStructuredArchive::FSlot GetSlot() { return Slot; }

private:

	FBinaryArchiveFormatter Formatter;
	FStructuredArchive StructuredArchive;
	FStructuredArchive::FSlot Slot;
};
