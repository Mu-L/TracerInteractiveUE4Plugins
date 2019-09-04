// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CurveTable.h"
#include "CompositeCurveTable.generated.h"

/**
 * Curve table composed of a stack of other curve tables.
 */
UCLASS(MinimalAPI)
class UCompositeCurveTable
	: public UCurveTable
{
	GENERATED_UCLASS_BODY()

	// Parent tables
	// Tables with higher indices override data in tables with lower indices
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Tables)
	TArray<UCurveTable*> ParentTables;

	//~ Begin UObject Interface.
	ENGINE_API virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
	ENGINE_API virtual void Serialize(FArchive& Ar) override;
	ENGINE_API virtual void PostLoad() override;
	//~ End UObject Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void PostEditUndo() override;
#endif // WITH_EDITOR

	ENGINE_API virtual void EmptyTable() override;

protected:

	// Searches the parent tables to see if there are any loops.
	// Returns a pointer to the first table found that depends on itself if a loop exists. Returns nullptr if no loops are found.
	const UCompositeCurveTable* FindLoops(TArray<const UCompositeCurveTable*> AlreadySeenTables) const;

	void UpdateCachedRowMap(bool bWarnOnInvalidChildren = true);

	void OnParentTablesUpdated(EPropertyChangeType::Type ChangeType = EPropertyChangeType::Unspecified);

	// true if this asset is currently being loaded; false otherwise
	uint8 bIsLoading : 1;

	// temporary copy used to detect changes so we can update delegates correctly on removal
	UPROPERTY(transient)
	TArray<UCurveTable*> OldParentTables;
};
