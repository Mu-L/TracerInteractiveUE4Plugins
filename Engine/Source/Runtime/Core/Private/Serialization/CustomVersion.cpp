// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CustomVersion.cpp: Unreal custom versioning system.
=============================================================================*/

#include "Serialization/CustomVersion.h"
#include "Serialization/StructuredArchiveFromArchive.h"

namespace
{
	FCustomVersion UnusedCustomVersion(FGuid(0, 0, 0, 0xF99D40C1), 0, TEXT("Unused custom version"));

	struct FEnumCustomVersion_DEPRECATED
	{
		uint32 Tag;
		int32  Version;

		FCustomVersion ToCustomVersion() const
		{
			// We'll invent a GUID from three zeroes and the original tag
			return FCustomVersion(FGuid(0, 0, 0, Tag), Version, *FString::Printf(TEXT("EnumTag%u"), Tag));
		}
	};

	void operator<<(FStructuredArchive::FSlot Slot, FEnumCustomVersion_DEPRECATED& Version)
	{
		// Serialize keys
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		Record << NAMED_ITEM("Tag", Version.Tag);
		Record << NAMED_ITEM("Version", Version.Version);
	}

	FArchive& operator<<(FArchive& Ar, FEnumCustomVersion_DEPRECATED& Version)
	{
		FStructuredArchiveFromArchive(Ar).GetSlot() << Version;
		return Ar;
	}

	struct FGuidCustomVersion_DEPRECATED
	{
		FGuid Key;
		int32 Version;
		FString FriendlyName;

		FCustomVersion ToCustomVersion() const
		{
			// We'll invent a GUID from three zeroes and the original tag
			return FCustomVersion(Key, Version, *FriendlyName);
		}
	};

	void operator<<(FStructuredArchive::FSlot Slot, FGuidCustomVersion_DEPRECATED& Version)
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		Record << NAMED_ITEM("Key", Version.Key);
		Record << NAMED_ITEM("Version", Version.Version);
		Record << NAMED_ITEM("FriendlyName", Version.FriendlyName);
	}

	FArchive& operator<<(FArchive& Ar, FGuidCustomVersion_DEPRECATED& Version)
	{
		FStructuredArchiveFromArchive(Ar).GetSlot() << Version;
		return Ar;
	}
}

const FName FCustomVersion::GetFriendlyName() const
{
	if (FriendlyName == NAME_None)
	{
		FriendlyName = FCustomVersionContainer::GetRegistered().GetFriendlyName(Key);
	}
	return FriendlyName;
}

const FCustomVersionContainer& FCustomVersionContainer::GetRegistered()
{
	return GetInstance();
}

void FCustomVersionContainer::Empty()
{
	Versions.Empty();
}

FString FCustomVersionContainer::ToString(const FString& Indent) const
{
	FString VersionsAsString;
	for (const FCustomVersion& SomeVersion : Versions)
	{
		VersionsAsString += Indent;
		VersionsAsString += FString::Printf(TEXT("Key=%s  Version=%d  Friendly Name=%s \n"), *SomeVersion.Key.ToString(), SomeVersion.Version, *SomeVersion.GetFriendlyName().ToString() );
	}

	return VersionsAsString;
}

FCustomVersionContainer& FCustomVersionContainer::GetInstance()
{
	static FCustomVersionContainer Singleton;

	return Singleton;
}

FArchive& operator<<(FArchive& Ar, FCustomVersion& Version)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << Version;
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FCustomVersion& Version)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << NAMED_ITEM("Key", Version.Key);
	Record << NAMED_ITEM("Version", Version.Version);
}

void FCustomVersionContainer::Serialize(FArchive& Ar, ECustomVersionSerializationFormat::Type Format)
{
	Serialize(FStructuredArchiveFromArchive(Ar).GetSlot(), Format);
}

void FCustomVersionContainer::Serialize(FStructuredArchive::FSlot Slot, ECustomVersionSerializationFormat::Type Format)
{
	switch (Format)
	{
	default: check(false);

	case ECustomVersionSerializationFormat::Enums:
	{
		// We should only ever be loading enums.  They should never be saved - they only exist for backward compatibility.
		check(Slot.GetUnderlyingArchive().IsLoading());

		TArray<FEnumCustomVersion_DEPRECATED> OldTags;
		Slot << OldTags;

		Versions.Empty(OldTags.Num());
		for (auto It = OldTags.CreateConstIterator(); It; ++It)
		{
			Versions.Add(It->ToCustomVersion());
		}
	}
	break;

	case ECustomVersionSerializationFormat::Guids:
	{
		// We should only ever be loading old versions.  They should never be saved - they only exist for backward compatibility.
		check(Slot.GetUnderlyingArchive().IsLoading());

		TArray<FGuidCustomVersion_DEPRECATED> VersionArray;
		Slot << VersionArray;
		Versions.Empty(VersionArray.Num());
		for (FGuidCustomVersion_DEPRECATED& OldVer : VersionArray)
		{
			Versions.Add(OldVer.ToCustomVersion());
		}
	}
	break;

	case ECustomVersionSerializationFormat::Optimized:
	{
		Slot << Versions;
	}
	break;
	}
}

const FCustomVersion* FCustomVersionContainer::GetVersion(FGuid Key) const
{
	// A testing tag was written out to a few archives during testing so we need to
	// handle the existence of it to ensure that those archives can still be loaded.
	if (Key == UnusedCustomVersion.Key)
	{
		return &UnusedCustomVersion;
	}

	return Versions.FindByKey(Key);
}

const FName FCustomVersionContainer::GetFriendlyName(FGuid Key) const
{
	FName FriendlyName = NAME_Name;
	const FCustomVersion* CustomVersion = GetVersion(Key);
	if (CustomVersion)
	{
		FriendlyName = CustomVersion->FriendlyName;
	}
	return FriendlyName;
}

void FCustomVersionContainer::SetVersion(FGuid CustomKey, int32 Version, FName FriendlyName)
{
	if (CustomKey == UnusedCustomVersion.Key)
	{
		return;
	}

	if (FCustomVersion* Found = Versions.FindByKey(CustomKey))
	{
		Found->Version      = Version;
		Found->FriendlyName = FriendlyName;
	}
	else
	{
		Versions.Add(FCustomVersion(CustomKey, Version, FriendlyName));
	}
}

FCustomVersionRegistration::FCustomVersionRegistration(FGuid InKey, int32 InVersion, FName InFriendlyName)
{
	FCustomVersionArray& Versions = FCustomVersionContainer::GetInstance().Versions;

	// Check if this tag hasn't already been registered
	if (FCustomVersion* ExistingRegistration = Versions.FindByKey(InKey))
	{
		// We don't allow the registration details to change across registrations - this code path only exists to support hotreload

		// If you hit this then you've probably either:
		// * Changed registration details during hotreload.
		// * Accidentally copy-and-pasted an FCustomVersionRegistration object.
		ensureMsgf(
			ExistingRegistration->Version == InVersion && ExistingRegistration->GetFriendlyName() == InFriendlyName,
			TEXT("Custom version registrations cannot change between hotreloads - \"%s\" version %d is being reregistered as \"%s\" version %d"),
			*ExistingRegistration->GetFriendlyName().ToString(),
			ExistingRegistration->Version,
			*InFriendlyName.ToString(),
			InVersion
		);

		++ExistingRegistration->ReferenceCount;
	}
	else
	{
		Versions.Add(FCustomVersion(InKey, InVersion, InFriendlyName));
	}

	Key = InKey;
}

FCustomVersionRegistration::~FCustomVersionRegistration()
{
	FCustomVersionArray& Versions = FCustomVersionContainer::GetInstance().Versions;

	const int32 KeyIndex = Versions.IndexOfByKey(Key);

	// Ensure this tag has been registered
	check(KeyIndex != INDEX_NONE);

	FCustomVersion* FoundKey = &Versions[KeyIndex];

	--FoundKey->ReferenceCount;
	if (FoundKey->ReferenceCount == 0)
	{
		Versions.RemoveAtSwap(KeyIndex);
	}
}
