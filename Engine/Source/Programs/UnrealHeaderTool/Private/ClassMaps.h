// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Stack.h"
#include "UnderlyingEnumType.h"

#include "UnrealSourceFile.h"

class UField;
class UClass;
class FProperty;
class UPackage;
class UEnum;
class FClassDeclarationMetaData;
class FArchive;
struct FManifestModule;
class FUnrealSourceFile;
class FUnrealTypeDefinitionInfo;

enum class ESerializerArchiveType
{
	None,
	Archive,
	StructuredArchiveRecord
};

struct FArchiveTypeDefinePair
{
	ESerializerArchiveType ArchiveType;
	FString EnclosingDefine;
};

extern TMap<FString, TSharedRef<FUnrealSourceFile> > GUnrealSourceFilesMap;
extern TMap<UField*, TSharedRef<FUnrealTypeDefinitionInfo> > GTypeDefinitionInfoMap;
extern TMap<const UPackage*, TArray<UField*>> GPackageSingletons;
extern FCriticalSection GPackageSingletonsCriticalSection;
extern TSet<FUnrealSourceFile*> GPublicSourceFileSet;
extern TMap<FProperty*, FString> GArrayDimensions;
extern TMap<UPackage*,  const FManifestModule*> GPackageToManifestModuleMap;
extern TMap<void*, uint32> GGeneratedCodeHashes;
extern FRWLock GGeneratedCodeHashesLock;
extern TMap<UEnum*, EUnderlyingEnumType> GEnumUnderlyingTypes;
extern TMap<FName, TSharedRef<FClassDeclarationMetaData> > GClassDeclarations;
extern TSet<FProperty*> GUnsizedProperties;
extern TSet<UField*> GEditorOnlyDataTypes;
extern TMap<UStruct*, TTuple<TSharedRef<FUnrealSourceFile>, int32>> GStructToSourceLine;
extern TMap<UClass*, FArchiveTypeDefinePair> GClassSerializerMap;
extern TSet<FProperty*> GPropertyUsesMemoryImageAllocator;

/** Types access specifiers. */
enum EAccessSpecifier
{
	ACCESS_NotAnAccessSpecifier = 0,
	ACCESS_Public,
	ACCESS_Private,
	ACCESS_Protected,
	ACCESS_Num,
};

inline FArchive& operator<<(FArchive& Ar, EAccessSpecifier& ObjectType)
{
	if (Ar.IsLoading())
	{
		int32 Value;
		Ar << Value;
		ObjectType = EAccessSpecifier(Value);
	}
	else if (Ar.IsSaving())
	{
		int32 Value = (int32)ObjectType;
		Ar << Value;
	}

	return Ar;
}
