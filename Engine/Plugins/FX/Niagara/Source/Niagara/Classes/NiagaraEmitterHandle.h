// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "NiagaraModule.h"
#include "NiagaraEmitterHandle.generated.h"

class UNiagaraSystem;
class UNiagaraEmitter;

/** 
 * Stores a references to a source emitter asset, and a copy of that emitter for editing within an System.  Also
 * stores whether or not this emitter is enabled, and it's name within the editor.
 */
USTRUCT()
struct NIAGARA_API FNiagaraEmitterHandle
{
	GENERATED_USTRUCT_BODY()
public:
	/** Creates a new invalid emitter handle. */
	FNiagaraEmitterHandle();

	/** Create a new emitter handle from an emitter, but does NOT make a copy, any changes made to the "Instance" will modify
		the original asset.  This version should only be used in the emitter toolkit. */
	FNiagaraEmitterHandle(UNiagaraEmitter& Emitter);

#if WITH_EDITORONLY_DATA
	/** Creates a new emitter handle from an emitter and an owning System. */
	FNiagaraEmitterHandle(UNiagaraEmitter& InSourceEmitter, FName InName, UNiagaraSystem& InOuterSystem);

	/** Creates a new emitter handle by duplicating an existing handle.  The new emitter handle will reference the same source emitter
		but will have it's own copy of the emitter made from the one in the supplied handle and will have it's own Id. */
	FNiagaraEmitterHandle(const FNiagaraEmitterHandle& InHandleToDuplicate, FName InDuplicateName, UNiagaraSystem& InDuplicateOwnerSystem);
#endif
	/** Whether or not this is a valid emitter handle. */
	bool IsValid() const;

	/** Gets the unique id for this handle. */
	FGuid GetId() const;

	// HACK!  Data sets used to use the emitter name, but this isn't guaranteed to be unique.  This is a temporary hack
	// to allow the data sets to continue work with using names, but that code needs to be refactored to use the id defined here.
	FName GetIdName() const;

	/** Gets the display name for this emitter in the System. */
	FName GetName() const;

	/** Sets the display name for this emitter in the System. The system is needed here in order to ensure uniqueness of the name. */
	void SetName(FName InName, UNiagaraSystem& InOwnerSystem);

	/** Gets whether or not this emitter is enabled within the System.  Disabled emitters aren't simulated. */
	bool GetIsEnabled() const;

	/** Gets whether or not this emitter is enabled within the System.  Disabled emitters aren't simulated. */
	void SetIsEnabled(bool bInIsEnabled);

#if WITH_EDITORONLY_DATA
	/** Gets the source emitter this emitter handle was built from. */
	const UNiagaraEmitter* GetSource() const;

	bool IsIsolated() const {	return bIsolated; }
	void SetIsolated(bool bInIsolated) { bIsolated = bInIsolated; }
#endif

	/** Gets the copied instance of the emitter this handle references. */
	UNiagaraEmitter* GetInstance() const;

	/** Gets a unique name for this emitter instance for use in scripts and parameter stores etc.*/
	FString GetUniqueInstanceName()const;

#if WITH_EDITORONLY_DATA
	/** Determine whether or not the Source and Instance refer to the same Emitter ChangeId.*/
	bool IsSynchronizedWithSource() const;

	/** Determine whether or not the Instance script is in synch with its graph.*/
	bool NeedsRecompile() const;

	/** Calls conditional post load on all sub-objects this handle references. */
	void ConditionalPostLoad();

	/** Merges in any changes from the source emitter into the instanced emitter. */
	INiagaraModule::FMergeEmitterResults MergeSourceChanges();

	/** Gets an object which contains the history of modifications to the emitter instance. */
	UObject* GetEmitterModificationHistory();

	/** Sets an object which will contain the history of modifications to the emitter instance. */
	void SetEmitterModificationHistory();

#endif
public:
	/** A static const invalid handle. */
	static const FNiagaraEmitterHandle InvalidHandle;

private:
	/** The id of this emitter handle. */
	UPROPERTY(VisibleAnywhere, Category="Emitter ID")
	FGuid Id;

	// HACK!  Data sets used to use the emitter name, but this isn't guaranteed to be unique.  This is a temporary hack
	// to allow the data sets to continue work with using names, but that code needs to be refactored to use the id defined here.
	UPROPERTY(VisibleAnywhere, Category = "Emitter ID")
	FName IdName;

	/** Whether or not this emitter is enabled within the System.  Disabled emitters aren't simulated. */
	UPROPERTY()
	bool bIsEnabled;
	
	/** The display name for this emitter in the System. */
	UPROPERTY()
	FName Name;

#if WITH_EDITORONLY_DATA
	/** The source emitter this emitter handle was built from. */
	UPROPERTY()
	UNiagaraEmitter* Source;

	/** An unmodified copy of the emitter this handle references for use when merging change from the source emitter. */
	UPROPERTY()
	UNiagaraEmitter* LastMergedSource;

	bool bIsolated;
#endif

	/** The copied instance of the emitter this handle references. */
	UPROPERTY()
	UNiagaraEmitter* Instance;
};
