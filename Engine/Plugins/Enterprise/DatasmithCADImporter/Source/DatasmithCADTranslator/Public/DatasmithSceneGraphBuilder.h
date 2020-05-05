// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef CAD_INTERFACE
#include "CoreMinimal.h"

#include "CADData.h"
#include "CADSceneGraph.h"
#include "CoreTechHelper.h"
#include "CoreTechTypes.h"
#include "CTSession.h"
#include "DatasmithImportOptions.h"

#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Misc/Paths.h"

class FDatasmithSceneSource;
class IDatasmithActorElement;
class IDatasmithMeshElement;
class IDatasmithScene;
class IDatasmithUEPbrMaterialElement;

class ActorData  //#ueent_CAD
{
public:
	ActorData(const TCHAR* NodeUuid, const ActorData& ParentData)
		: Uuid(NodeUuid)
		, Material(ParentData.Material)
		, MaterialUuid(ParentData.MaterialUuid)
		, Color(ParentData.Color)
		, ColorUuid(ParentData.ColorUuid)
	{
	}

	ActorData(const TCHAR* NodeUuid)
		: Uuid(NodeUuid)
		, MaterialUuid(0)
		, ColorUuid(0)
	{
	}

	const TCHAR* Uuid;

	CADLibrary::FCADMaterial Material;
	uint32 MaterialUuid;

	FColor Color;
	uint32 ColorUuid;
};




class DATASMITHCADTRANSLATOR_API FDatasmithSceneGraphBuilder
{
public:
	FDatasmithSceneGraphBuilder(
		TMap<uint32, FString>& InCADFileToUE4FileMap, 
		const FString& InCachePath, 
		TSharedRef<IDatasmithScene> InScene, 
		const FDatasmithSceneSource& InSource, 
		const CADLibrary::FImportParameters& InImportParameters);

	bool Build();

	void LoadSceneGraphDescriptionFiles();

	void FillAnchorActor(const TSharedRef< IDatasmithActorElement >& ActorElement, const FString& CleanFilenameOfCADFile);
private:

	TSharedPtr< IDatasmithActorElement > BuildInstance(int32 InstanceIndex, const ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildComponent(CADLibrary::FArchiveComponent& Component, const ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildBody(int32 BodyIndex, const ActorData& ParentData);

	void AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap);
	void AddChildren(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FArchiveComponent& Component, const ActorData& ParentData);
	bool DoesActorHaveChildrenOrIsAStaticMesh(const TSharedPtr< IDatasmithActorElement >& ActorElement);

	TSharedPtr< IDatasmithUEPbrMaterialElement > GetDefaultMaterial();
	TSharedPtr<IDatasmithMaterialIDElement> FindOrAddMaterial(uint32 MaterialUuid);

	TSharedPtr< IDatasmithActorElement > CreateActor(const TCHAR* ActorUUID, const TCHAR* ActorLabel);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(CADLibrary::FArchiveBody& Body, FString& InLabel);

	void GetNodeUUIDAndName(TMap<FString, FString>& InInstanceNodeMetaDataMap, TMap<FString, FString>& InReferenceNodeMetaDataMap, int32 InComponentIndex, const TCHAR* InParentUEUUID, FString& OutUEUUID, FString& OutName);

protected:
	TMap<uint32, FString>& CADFileToSceneGraphDescriptionFile;
	const FString& CachePath;
	TSharedRef<IDatasmithScene> DatasmithScene;
	const CADLibrary::FImportParameters& ImportParameters;
	const uint32 ImportParametersHash;

	CADLibrary::FFileDescription rootFileDescription;

	TArray<CADLibrary::FArchiveSceneGraph> ArchiveMockUps;
	TMap<uint32, CADLibrary::FArchiveSceneGraph*> CADFileToSceneGraphArchive;

	TMap< CADUUID, TSharedPtr< IDatasmithMeshElement > > BodyUuidToMeshElement;

	TMap< CADUUID, TSharedPtr< IDatasmithUEPbrMaterialElement > > MaterialUuidMap;
	TSharedPtr<IDatasmithUEPbrMaterialElement > DefaultMaterial;

	TMap<CADUUID, CADLibrary::FArchiveColor> ColorNameToColorArchive; 
	TMap<CADUUID, CADLibrary::FArchiveMaterial> MaterialNameToMaterialArchive; 

	TArray<uint32> AncestorSceneGraphHash;

	CADLibrary::FArchiveSceneGraph* SceneGraph;

	bool bPreferMaterial;
	bool bMaterialPropagationIsTopDown;
};

#endif // CAD_INTERFACE
