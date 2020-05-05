// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRigDefines.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigUnit.generated.h"

struct FRigUnitContext;

/** Base class for all rig units */
USTRUCT(BlueprintType, meta=(Abstract, NodeColor = "0.1 0.1 0.1"))
struct CONTROLRIG_API FRigUnit : public FRigVMStruct
{
	GENERATED_BODY()

	FRigUnit()
	{}

	/** Virtual destructor */
	virtual ~FRigUnit() {}

	/** Returns the label of this unit */
	virtual FString GetUnitLabel() const { return FString(); }

	/** Execute logic for this rig unit */
	virtual void Execute(const FRigUnitContext& Context) {}
};

/** Base class for all rig units that can change data */
USTRUCT(BlueprintType, meta = (Abstract))
struct CONTROLRIG_API FRigUnitMutable : public FRigUnit
{
	GENERATED_BODY()

	FRigUnitMutable()
	: FRigUnit()
	{}

	/*
	 * This property is used to chain multiple mutable units together
	 */
	UPROPERTY(DisplayName = "Execute", Transient, meta = (Input, Output))
	FControlRigExecuteContext ExecuteContext;
};
