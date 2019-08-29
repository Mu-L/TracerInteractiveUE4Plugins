// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in Dev-Core stream
struct CORE_API FReleaseObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Static Mesh extended bounds radius fix
		StaticMeshExtendedBoundsFix,

		//Physics asset bodies are either in the sync scene or the async scene, but not both
		NoSyncAsyncPhysAsset,

		// ULevel was using TTransArray incorrectly (serializing the entire array in addition to individual mutations).
		// converted to a TArray:
		LevelTransArrayConvertedToTArray,

		// Add Component node templates now use their own unique naming scheme to ensure more reliable archetype lookups.
		AddComponentNodeTemplateUniqueNames,

		// Fix a serialization issue with static mesh FMeshSectionInfoMap UProperty
		UPropertryForMeshSectionSerialize,

		// Existing HLOD settings screen size to screen area conversion
		ConvertHLODScreenSize,

		// Adding mesh section info data for existing billboard LOD models
		SpeedTreeBillboardSectionInfoFixup,

		// Change FMovieSceneEventParameters::StructType to be a string asset reference from a TWeakObjectPtr<UScriptStruct>
		EventSectionParameterStringAssetRef,

		// Remove serialized irradiance map data from skylight.
		SkyLightRemoveMobileIrradianceMap,

		// rename bNoTwist to bAllowTwist
		RenameNoTwistToAllowTwistInTwoBoneIK,

		// Material layers serialization refactor
		MaterialLayersParameterSerializationRefactor,

		// Added disable flag to skeletal mesh data
		AddSkeletalMeshSectionDisable,

		// Removed objects that were serialized as part of this material feature
		RemovedMaterialSharedInputCollection,

		// HISMC Cluster Tree migration to add new data
		HISMCClusterTreeMigration,

		// Default values on pins in blueprints could be saved incoherently
		PinDefaultValuesVerified,

		// During copy and paste transition getters could end up with broken state machine references
		FixBrokenStateMachineReferencesInTransitionGetters,

		// Change to MeshDescription serialization
		MeshDescriptionNewSerialization,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FReleaseObjectVersion() {}
};
