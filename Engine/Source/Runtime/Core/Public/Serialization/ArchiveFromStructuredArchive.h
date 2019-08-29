// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Serialization/ArchiveProxy.h"
#include "Serialization/StructuredArchive.h"
#include "Misc/Optional.h"
#include "UObject/NameTypes.h"
#include "Containers/Map.h"
#include "Containers/BitArray.h"

class CORE_API FArchiveFromStructuredArchive : public FArchiveProxy
{
public:
	FArchiveFromStructuredArchive(FStructuredArchive::FSlot Slot);
	~FArchiveFromStructuredArchive();

	virtual void Flush() override;
	virtual bool Close() override;

	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual void Seek(int64 InPos) override;
	virtual bool AtEnd() override;

	virtual FArchive& operator<<(class FName& Value) override;
	virtual FArchive& operator<<(class UObject*& Value) override;
	virtual FArchive& operator<<(class FText& Value) override;

	virtual void Serialize(void* V, int64 Length) override;

	virtual FArchive* GetCacheableArchive() override
	{
		return IsTextFormat() ? nullptr : Record->GetUnderlyingArchive().GetCacheableArchive();
	}
	
private:
	static const int32 MaxBufferSize = 128;

	bool bPendingSerialize;

	TArray<uint8> Buffer;
	int32 Pos;

	TArray<FName> Names;
	TMap<FName, int32> NameToIndex;

	TArray<UObject*> Objects;
	TBitArray<> ObjectsValid;
	TMap<UObject*, int32> ObjectToIndex;

	TOptional<FStructuredArchive::FRecord> Record;

	void SerializeInternal();
};
