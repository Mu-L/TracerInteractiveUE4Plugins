// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/ObjectResource.h"
#include "UObject/Linker.h"
#include "UObject/UObjectThreadContext.h"
#include "Templates/RefCounting.h"

struct FUntypedBulkData;
class FPackageHeaderSaver;

/*----------------------------------------------------------------------------
	FLinkerSave.
----------------------------------------------------------------------------*/

/**
 * Handles saving Unreal package files.
 */
class FLinkerSave : public FLinker, public FArchiveUObject
{
public:

	FORCEINLINE static ELinkerType::Type StaticType()
	{
		return ELinkerType::Save;
	}

	virtual ~FLinkerSave();

	// Variables.
	/** The archive that actually writes the data to disk. */
	FArchive* Saver;

	FPackageIndex CurrentlySavingExport;
	TArray<FPackageIndex> DepListForErrorChecking;

	/** Index array - location of the resource for a UObject is stored in the ObjectIndices array using the UObject's Index */
	TMap<UObject *,FPackageIndex> ObjectIndicesMap;

	/** List of Searchable Names, by object containing them. This gets turned into package indices later */
	TMap<const UObject *, TArray<FName> > SearchableNamesObjectMap;

	FPackageHeaderSaver& HeaderSaver;

	/** Save context associated with this linker */
	TRefCountPtr<FUObjectSerializeContext> SaveContext;

	/** List of bulkdata that needs to be stored at the end of the file */
	struct FBulkDataStorageInfo
	{
		/** Offset to the location where the payload offset is stored */
		int64 BulkDataOffsetInFilePos;
		/** Offset to the location where the payload size is stored */
		int64 BulkDataSizeOnDiskPos;
		/** Offset to the location where the bulk data flags are stored */
		int64 BulkDataFlagsPos;
		/** Bulk data flags at the time of serialization */
		uint32 BulkDataFlags;
		/** The bulkdata */
		FUntypedBulkData* BulkData;
	};
	TArray<FBulkDataStorageInfo> BulkDataToAppend;

	/** A mapping of package name to generated script SHA keys */
	COREUOBJECT_API static TMap<FString, TArray<uint8> > PackagesToScriptSHAMap;

	/** Constructor for file writer */
	FLinkerSave(FPackageHeaderSaver& InHeaderSaver, UPackage* InParent, const TCHAR* InFilename, bool bForceByteSwapping, bool bInSaveUnversioned = false );
	/** Constructor for memory writer */
	FLinkerSave(FPackageHeaderSaver& InHeaderSaver, UPackage* InParent, bool bForceByteSwapping, bool bInSaveUnversioned = false );
	/** Constructor for custom savers. The linker assumes ownership of the custom saver. */
	FLinkerSave(FPackageHeaderSaver& InHeaderSaver, UPackage* InParent, FArchive *InSaver, bool bForceByteSwapping, bool bInSaveUnversioned = false);

	/** Returns the appropriate package index for the source object, or default value if not found in ObjectIndicesMap */
	FPackageIndex MapObject(const UObject* Object) const;

	// FArchive interface.
	using FArchiveUObject::operator<<; // For visibility of the overloads we don't override
	FArchive& operator<<( FName& InName );
	FArchive& operator<<( UObject*& Obj );
	FArchive& operator<<( FLazyObjectPtr& LazyObjectPtr );
	virtual void SetSerializeContext(FUObjectSerializeContext* InLoadContext) override;
	FUObjectSerializeContext* GetSerializeContext() override;
	virtual void UsingCustomVersion(const struct FGuid& Guid) override;

#if WITH_EDITOR
	// proxy for debugdata
	virtual void PushDebugDataString(const FName& DebugData) override { Saver->PushDebugDataString(DebugData); }
	virtual void PopDebugDataString() override { Saver->PopDebugDataString(); }
#endif

	virtual FString GetArchiveName() const override;

	/**
	 * If this archive is a FLinkerLoad or FLinkerSave, returns a pointer to the FLinker portion.
	 */
	virtual FLinker* GetLinker() { return this; }

	void Seek( int64 InPos );
	int64 Tell();
	// this fixes the warning : 'FLinkerSave::Serialize' hides overloaded virtual function
	using FLinker::Serialize;
	void Serialize( void* V, int64 Length );

	/**
	 * Closes and deletes the Saver (file, memory or custom writer) which will close any associated file handle.
	 * Returns false if the owned saver contains errors after closing it, true otherwise.
	 */
	bool CloseAndDestroySaver();

	/**
	 * Sets a flag indicating that this archive contains data required to be gathered for localization.
	 */
	void ThisRequiresLocalizationGather();
};
