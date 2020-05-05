// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EditorLevelUtils.h: Editor-specific level management routines
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "EditorLevelUtils.generated.h"

class AActor;
class ULevel;
class ULevelStreaming;

DECLARE_LOG_CATEGORY_EXTERN(LogLevelTools, Warning, All);

UENUM(BlueprintType)
enum class ELevelVisibilityDirtyMode : uint8
{
	// Use when the user is causing the visibility change.  Will update transaction state and mark the package dirty.
	ModifyOnChange,
	// Use when code is causing the visibility change.
	DontModify
};

UCLASS(transient)
class UEditorLevelUtils : public UObject
{
	GENERATED_BODY()
public:
	/**
	 * Creates a new streaming level in the current world
	 *
	 * @param	LevelStreamingClass					The streaming class type instead to use for the level.
	 * @param	NewLevelPath						Optional path to the level package path format ("e.g /Game/MyLevel").  If empty, the user will be prompted during the save process.
	 * @param	bMoveSelectedActorsIntoNewLevel		If true, move any selected actors into the new level.
	 *
	 * @return	Returns the newly created level, or NULL on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Creation")
	static UNREALED_API ULevelStreaming* CreateNewStreamingLevel(TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& NewLevelPath = TEXT(""), bool bMoveSelectedActorsIntoNewLevel = false);

	/**
	 * Makes the specified streaming level the current level for editing.
	 * The current level is where actors are spawned to when calling SpawnActor
	 *
	 * @return	true	If a level was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Creation")
	static UNREALED_API void MakeLevelCurrent(ULevelStreaming* InStreamingLevel);


	/**
	 * Moves the specified list of actors to the specified streaming level. The new actors will be selected
	 *
	 * @param	ActorsToMove			List of actors to move
	 * @param	DestStreamingLevel		The destination streaming level of the current world to move the actors to
	 * @param	bWarnAboutReferences	Whether or not to show a modal warning about referenced actors that may no longer function after being moved
	 * @return							The number of actors that were successfully moved to the new level
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Creation")
	static UNREALED_API int32 MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevelStreaming* DestStreamingLevel, bool bWarnAboutReferences = true, bool bWarnAboutRenaming = true);

	/**
	 * Moves the currently selected actors to the specified streaming level. The new actors will be selected
	 *
	 * @param	DestStreamingLevel		The destination streaming level of the current world to move the actors to
	 * @param	bWarnAboutReferences	Whether or not to show a modal warning about referenced actors that may no longer function after being moved
	 * @return							The number of actors that were successfully moved to the new level
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Creation")
	static UNREALED_API int32 MoveSelectedActorsToLevel(ULevelStreaming* DestLevel, bool bWarnAboutReferences = true);

	
	/**
	 * Makes the specified level the current level for editing.
	 * The current level is where actors are spawned to when calling SpawnActor
	 * @param InLevel			The level to make current
	 * @param bForceOperation	True if the operation should succeed even if the level is locked.  In certian circumstances (like removing the current level, we must be able to set a new current level even if the only one left is locked)
	 *
	 * @return	true	If a level was removed.
	 */
	static UNREALED_API void MakeLevelCurrent(ULevel* InLevel, bool bEvenIfLocked = false);

	
	static UNREALED_API int32 MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevel* DestLevel, bool bWarnAboutReferences = true, bool bWarnAboutRenaming = true);

	static UNREALED_API int32 MoveSelectedActorsToLevel(ULevel* DestLevel, bool bWarnAboutReferences = true);

	/**
	 * Creates a new streaming level and adds it to a world
	 *
	 * @param	InWorld								The world to add the streaming level to
	 * @param	LevelStreamingClass					The streaming class type instead to use for the level.
	 * @param	DefaultFilename						Optional file name for level.  If empty, the user will be prompted during the save process.
	 * @param	bMoveSelectedActorsIntoNewLevel		If true, move any selected actors into the new level.
	 * @param	InTemplateWorld						If valid, the new level will be a copy of the template world.
	 *
	 * @return	Returns the newly created level, or NULL on failure
	 */
	static UNREALED_API ULevelStreaming* CreateNewStreamingLevelForWorld(UWorld& World, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& DefaultFilename = TEXT(""), bool bMoveSelectedActorsIntoNewLevel = false, UWorld* InTemplateWorld = nullptr);


	/**
	 * Adds the named level packages to the world.  Does nothing if all the levels already exist in the world.
	 *
	 * @param	InWorld				World in which to add the level
	 * @param	LevelPackageName	The base filename of the level package to add.
	 * @param	LevelStreamingClass	The streaming class type instead to use for the level.
	 *
	 * @return								The new level, or NULL if the level couldn't added.
	 */
	static UNREALED_API ULevel* AddLevelsToWorld(UWorld* InWorld, TArray<FString> LevelPackageNames, TSubclassOf<ULevelStreaming> LevelStreamingClass);


	/**
	 * Adds the named level package to the world.  Does nothing if the level already exists in the world.
	 *
	 * @param	InWorld				World in which to add the level
	 * @param	LevelPackageName	The base filename of the level package to add.
	 * @param	LevelStreamingClass	The streaming class type instead to use for the level.
	 *
	 * @return								The new level, or NULL if the level couldn't added.
	 */
	static UNREALED_API ULevelStreaming* AddLevelToWorld(UWorld* InWorld, const TCHAR* LevelPackageName, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FTransform& LevelTransform = FTransform::Identity);

private:

	static UNREALED_API ULevelStreaming* AddLevelToWorld_Internal(UWorld* InWorld, const TCHAR* LevelPackageName, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FTransform& LevelTransform = FTransform::Identity);

public:
	/** Sets the LevelStreamingClass for the specified Level 
	  * @param	InLevel				The level for which to change the streaming class
	  * @param	LevelStreamingClass	The desired streaming class
	  *
	  * @return	The new streaming level object
	  */
	static UNREALED_API ULevelStreaming* SetStreamingClassForLevel(ULevelStreaming* InLevel, TSubclassOf<ULevelStreaming> LevelStreamingClass);

	/**
	 * Removes the specified level from the world.  Refreshes.
	 *
	 * @return	true	If a level was removed.
	 */
	static UNREALED_API bool RemoveLevelFromWorld(ULevel* InLevel);

	/**
	 * Removes the specified LevelStreaming from the world, and Refreshes.
	 * Used to Clean up references of missing levels.
	 *
	 * @return	true	If a level was removed.
	 */
	static UNREALED_API bool RemoveInvalidLevelFromWorld(ULevelStreaming* InLevelStreaming);

	/** 
	 * Sets the actors within a level's visibility via bHiddenEdLevel.  
	 * Warning: modifies ULevel::bIsVisible and bHiddenEdLevel without marking packages dirty or supporting undo.  
	 * Calling code must restore to the original state before the user can interact with the levels.
	 */
	static UNREALED_API void SetLevelVisibilityTemporarily(ULevel* Level, bool bShouldBeVisible);

	/**
	 * Sets a level's visibility in the editor. Less efficient than SetLevelsVisibility when changing the visibility of multiple levels simultaneously.
	 *
	 * @param	Level					The level to modify.
	 * @param	bShouldBeVisible		The level's new visibility state.
	 * @param	bForceLayersVisible		If true and the level is visible, force the level's layers to be visible.
	 * @param	ModifyMode				ELevelVisibilityDirtyMode mode value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
	static UNREALED_API void SetLevelVisibility(ULevel* Level, const bool bShouldBeVisible, const bool bForceLayersVisible, const ELevelVisibilityDirtyMode ModifyMode = ELevelVisibilityDirtyMode::ModifyOnChange);

	/**
	 * Sets a level's visibility in the editor. More efficient than SetLevelsVisibility when changing the visibility of multiple levels simultaneously.
	 *
	 * @param	Levels					The levels to modify.
	 * @param	bShouldBeVisible		The level's new visibility state for each level.
	 * @param	bForceLayersVisible		If true and the level is visible, force the level's layers to be visible.
	 * @param	ModifyMode				ELevelVisibilityDirtyMode mode value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Level Utility")
	static UNREALED_API void SetLevelsVisibility(const TArray<ULevel*>& Levels, const TArray<bool>& bShouldBeVisible, const bool bForceLayersVisible, const ELevelVisibilityDirtyMode ModifyMode = ELevelVisibilityDirtyMode::ModifyOnChange);
	
	/** 
	 * Deselects all BSP surfaces in this level 
	 *
	 * @param InLevel		The level to deselect the surfaces of.
	 *
	 */	
	static UNREALED_API void DeselectAllSurfacesInLevel(ULevel* InLevel);

	/**
	 * Assembles the set of all referenced worlds.
	 *
	 * @param	InWorld				World containing streaming levels
	 * @param	Worlds				[out] The set of referenced worlds.
	 * @param	bIncludeInWorld		If true, include the InWorld in the output list.
	 * @param	bOnlyEditorVisible	If true, only sub-levels that should be visible in-editor are included
	 */
	static UNREALED_API void GetWorlds(UWorld* InWorld, TArray<UWorld*>& OutWorlds, bool bIncludeInWorld, bool bOnlyEditorVisible = false);

	UE_DEPRECATED(4.17, "The CreateNewLevel method to create streaming levels has been deprecated. Use CreateNewStreamingLevelForWorld instead")
	static UNREALED_API ULevel* CreateNewLevel(UWorld* InWorld, bool bMoveSelectedActorsIntoNewLevel, TSubclassOf<ULevelStreaming> LevelStreamingClass, const FString& DefaultFilename = TEXT(""));


	/**
	 * Moves the specified list of actors to the specified level
	 *
	 * @param	ActorsToMove		List of actors to move
	 * @param	DestLevelStreaming	The level streaming object associated with the destination level
	 * @param	OutNumMovedActors	The number of actors that were successfully moved to the new level
	 */
	UE_DEPRECATED(4.17, "The MovesActorsToLevel method has been deprecated. Use MoveActorsToLevel instead")
	static void MovesActorsToLevel(TArray< AActor* >& ActorsToMove, ULevelStreaming* DestLevelStreaming, int32& OutNumMovedActors);

private:
	/**
	 * Removes a level from the world.  Returns true if the level was removed successfully.
	 *
	 * @param	Level		The level to remove from the world.
	 * @return				true if the level was removed successfully, false otherwise.
	 */
	static bool PrivateRemoveLevelFromWorld(ULevel* Level);

	static bool PrivateRemoveInvalidLevelFromWorld(ULevelStreaming* InLevelStreaming);

	/**
	 * Completely removes the level from the world, unloads its package and forces garbage collection.
	 *
	 * @note: This function doesn't remove the associated streaming level.
	 *
	 * @param	InLevel			A non-NULL, non-Persistent Level that will be destroyed.
	 * @return					true if the level was removed.
	 */
	static bool EditorDestroyLevel(ULevel* InLevel);
private:

};

// For backwards compatibility
typedef UEditorLevelUtils EditorLevelUtils;
