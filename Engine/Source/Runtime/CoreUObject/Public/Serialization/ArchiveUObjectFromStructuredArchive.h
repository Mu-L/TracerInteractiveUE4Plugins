// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/ArchiveFromStructuredArchive.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/ObjectResource.h"

#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

class COREUOBJECT_API FArchiveUObjectFromStructuredArchive : public FArchiveFromStructuredArchive
{
public:

	FArchiveUObjectFromStructuredArchive(FStructuredArchive::FSlot Slot);
	virtual ~FArchiveUObjectFromStructuredArchive();

	using FArchive::operator<<; // For visibility of the overloads we don't override

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(FLazyObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override;
	//~ End FArchive Interface

private:

	bool bPendingSerialize;

	TArray<FLazyObjectPtr> LazyObjectPtrs;
	TArray<FWeakObjectPtr> WeakObjectPtrs;
	TArray<FSoftObjectPtr> SoftObjectPtrs;
	TArray<FSoftObjectPath> SoftObjectPaths;

	TMap<FLazyObjectPtr, int32> LazyObjectPtrToIndex;
	TMap<FWeakObjectPtr, int32> WeakObjectPtrToIndex;
	TMap<FSoftObjectPtr, int32> SoftObjectPtrToIndex;
	TMap<FSoftObjectPath, int32> SoftObjectPathToIndex;

	virtual void SerializeInternal(FStructuredArchive::FRecord Record) override;
};

#else

class COREUOBJECT_API FArchiveUObjectFromStructuredArchive : public FArchiveFromStructuredArchive
{
public:

	FArchiveUObjectFromStructuredArchive(FStructuredArchive::FSlot InSlot)
		: FArchiveFromStructuredArchive(InSlot)
	{

	}
};

#endif