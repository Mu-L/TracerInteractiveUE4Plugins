// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Templates/Casts.h"
#include "UObject/Interface.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimStateMachineTypes.h"

#include "AnimClassInterface.generated.h"

class USkeleton;
struct FExposedValueHandler;

/** Describes the input and output of an anim blueprint 'function' */
USTRUCT()
struct FAnimBlueprintFunction
{
	GENERATED_BODY()

	FAnimBlueprintFunction()
		: Name(NAME_None)
		, Group(NAME_None)
		, OutputPoseNodeIndex(INDEX_NONE)
		, OutputPoseNodeProperty(nullptr)
		, bImplemented(false)
	{}

	FAnimBlueprintFunction(const FName& InName)
		: Name(InName)
		, Group(NAME_None)
		, OutputPoseNodeIndex(INDEX_NONE)
		, OutputPoseNodeProperty(nullptr)
		, bImplemented(false)
	{}

	bool operator==(const FAnimBlueprintFunction& InFunction) const
	{
		return Name == InFunction.Name;
	}

	/** The name of the function */
	UPROPERTY()
	FName Name;

	/** The group of the function */
	UPROPERTY()
	FName Group;

	/** Index of the output node */
	UPROPERTY()
	int32 OutputPoseNodeIndex;

	/** The names of the input poses */
	UPROPERTY()
	TArray<FName> InputPoseNames;

	/** Indices of the input nodes */
	UPROPERTY()
	TArray<int32> InputPoseNodeIndices;

	/** The property of the output node, patched up during link */
	UPROPERTY(transient)
	UStructProperty* OutputPoseNodeProperty;

	/** The properties of the input nodes, patched up during link */
	UPROPERTY(transient)
	TArray<UStructProperty*> InputPoseNodeProperties;

	/** The input properties themselves */
	UPROPERTY(transient)
	TArray<UProperty*> InputProperties;

	/** Whether this function is actually implemented by this class - it could just be a stub */
	UPROPERTY(transient)
	bool bImplemented;
};

/** Wrapper struct as we dont support nested containers */
USTRUCT()
struct ENGINE_API FCachedPoseIndices
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> OrderedSavedPoseNodeIndices;

	bool operator==(const FCachedPoseIndices& InOther) const
	{
		return OrderedSavedPoseNodeIndices == InOther.OrderedSavedPoseNodeIndices;
	}
};

UINTERFACE()
class ENGINE_API UAnimClassInterface : public UInterface
{
	GENERATED_BODY()
};

class ENGINE_API IAnimClassInterface
{
	GENERATED_BODY()
public:
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const = 0;
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies() const = 0;
	virtual const TArray<UStructProperty*>& GetAnimNodeProperties() const = 0;
	virtual const TArray<UStructProperty*>& GetSubInstanceNodeProperties() const = 0;
	virtual const TArray<UStructProperty*>& GetLayerNodeProperties() const = 0;
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const = 0;
	virtual const TArray<FName>& GetSyncGroupNames() const = 0;
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap() const = 0;
	virtual const TArray<FAnimBlueprintFunction>& GetAnimBlueprintFunctions() const = 0;

	virtual USkeleton* GetTargetSkeleton() const = 0;

	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const = 0;

	static IAnimClassInterface* GetFromClass(UClass* InClass)
	{
		if (auto AnimClassInterface = Cast<IAnimClassInterface>(InClass))
		{
			return AnimClassInterface;
		}
		if (auto DynamicClass = Cast<UDynamicClass>(InClass))
		{
			DynamicClass->GetDefaultObject(true);
			return CastChecked<IAnimClassInterface>(DynamicClass->AnimClassImplementation, ECastCheckedType::NullAllowed);
		}
		return nullptr;
	}

	static UClass* GetActualAnimClass(IAnimClassInterface* AnimClassInterface)
	{
		if (UClass* ActualAnimClass = Cast<UClass>(AnimClassInterface))
		{
			return ActualAnimClass;
		}
		if (UObject* AsObject = Cast<UObject>(AnimClassInterface))
		{
			return Cast<UClass>(AsObject->GetOuter());
		}
		return nullptr;
	}

	static const FAnimBlueprintFunction* FindAnimBlueprintFunction(IAnimClassInterface* AnimClassInterface, const FName& InFunctionName)
	{
		for(const FAnimBlueprintFunction& Function : AnimClassInterface->GetAnimBlueprintFunctions())
		{
			if(Function.Name == InFunctionName)
			{
				return &Function;
			}
		}

		return nullptr;
	}

	/**
	 * Check if a function is an anim function on this class
	 * @param	InAnimClassInterface	The interface to check
	 * @param	InFunction				The function to check
	 * @return true if the supplied function is an anim function on the specified class
	 */
	static bool IsAnimBlueprintFunction(IAnimClassInterface* InAnimClassInterface, const UFunction* InFunction)
	{
		if(InFunction->GetOuterUClass() == GetActualAnimClass(InAnimClassInterface))
		{
			for(const FAnimBlueprintFunction& Function : InAnimClassInterface->GetAnimBlueprintFunctions())
			{
				if(Function.Name == InFunction->GetFName())
				{
					return true;
				}
			}
		}
		return false;
	}

	UE_DEPRECATED(4.23, "Please use GetAnimBlueprintFunctions()")
	virtual int32 GetRootAnimNodeIndex() const { return INDEX_NONE; }

	UE_DEPRECATED(4.23, "Please use GetAnimBlueprintFunctions()")
	virtual UStructProperty* GetRootAnimNodeProperty() const { return nullptr; }
};
