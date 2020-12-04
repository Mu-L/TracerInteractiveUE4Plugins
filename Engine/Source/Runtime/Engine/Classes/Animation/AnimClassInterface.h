// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Templates/Casts.h"
#include "UObject/Interface.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimStateMachineTypes.h"
#include "UObject/FieldPath.h"

#include "AnimClassInterface.generated.h"

class USkeleton;
struct FExposedValueHandler;
struct FPropertyAccessLibrary;

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
	FStructProperty* OutputPoseNodeProperty;

	/** The properties of the input nodes, patched up during link */
	TArray< FStructProperty* > InputPoseNodeProperties;

	/** The input properties themselves */
	TArray< FProperty* > InputProperties;

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

/** Contains indices for any Asset Player nodes found for a specific Name Anim Graph (only and specifically harvested for Anim Graph Layers and Implemented Anim Layer Graphs) */
USTRUCT()
struct FGraphAssetPlayerInformation
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<int32> PlayerNodeIndices;
};

/** Blending options for animation graphs in Linked Animation Blueprints. */
USTRUCT()
struct FAnimGraphBlendOptions
{
	GENERATED_USTRUCT_BODY()

	/**
	* Time to blend this graph in using Inertialization. Specify -1.0 to defer to the BlendOutTime of the previous graph.
	* To blend this graph in you must place an Inertialization node after the Linked Anim Graph node or Linked Anim Layer node that uses this graph.
	*/
	UPROPERTY(EditAnywhere, Category = GraphBlending)
	float BlendInTime;       

	/**
	* Time to blend this graph out using Inertialization. Specify -1.0 to defer to the BlendInTime of the next graph.
	* To blend this graph out you must place an Inertialization node after the Linked Anim Graph node or Linked Anim Layer node that uses this graph.
	*/
	UPROPERTY(EditAnywhere, Category = GraphBlending)
	float BlendOutTime;

	FAnimGraphBlendOptions()
		: BlendInTime(-1.0f)
		, BlendOutTime(-1.0f)
	{}
};


UINTERFACE()
class ENGINE_API UAnimClassInterface : public UInterface
{
	GENERATED_BODY()
};

typedef TFieldPath<FStructProperty> FStructPropertyPath;

class ENGINE_API IAnimClassInterface
{
	GENERATED_BODY()
public:
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const = 0;
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies() const = 0;
	virtual const TArray<FStructProperty*>& GetAnimNodeProperties() const = 0;
	UE_DEPRECATED(4.24, "Function has been renamed, please use GetLinkedAnimGraphNodeProperties")
	virtual const TArray<FStructProperty*>& GetSubInstanceNodeProperties() const { return GetLinkedAnimGraphNodeProperties(); }
	virtual const TArray<FStructProperty*>& GetLinkedAnimGraphNodeProperties() const = 0;
	UE_DEPRECATED(4.24, "Function has been renamed, please use GetLinkedLayerNodeProperties")
	virtual const TArray<FStructProperty*>& GetLayerNodeProperties() const { return GetLinkedAnimLayerNodeProperties(); }
	virtual const TArray<FStructProperty*>& GetLinkedAnimLayerNodeProperties() const = 0;
	virtual const TArray<FStructProperty*>& GetPreUpdateNodeProperties() const = 0;
	virtual const TArray<FStructProperty*>& GetDynamicResetNodeProperties() const = 0;
	virtual const TArray<FStructProperty*>& GetStateMachineNodeProperties() const = 0;
	virtual const TArray<FStructProperty*>& GetInitializationNodeProperties() const = 0;
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const = 0;
	virtual const TArray<FName>& GetSyncGroupNames() const = 0;
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap() const = 0;
	virtual const TArray<FAnimBlueprintFunction>& GetAnimBlueprintFunctions() const = 0;
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const = 0;
	virtual const TMap<FName, FAnimGraphBlendOptions>& GetGraphBlendOptions() const = 0;
	virtual USkeleton* GetTargetSkeleton() const = 0;
	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const = 0;
	virtual const FPropertyAccessLibrary& GetPropertyAccessLibrary() const = 0;
	
protected:
	friend class UAnimBlueprintGeneratedClass;

	// These direct accessors are here to allow internal access that doesnt redirect to the root class
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines_Direct() const = 0;
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies_Direct() const = 0;
	virtual const TArray<FName>& GetSyncGroupNames_Direct() const = 0;
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap_Direct() const = 0;
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation_Direct() const = 0;
	virtual const TMap<FName, FAnimGraphBlendOptions>& GetGraphBlendOptions_Direct() const = 0;
	virtual const FPropertyAccessLibrary& GetPropertyAccessLibrary_Direct() const = 0;

public:

	// Get the root anim class interface (i.e. if this is a derived class).
	// Some properties that are derived from the compiled anim graph are routed to the 'Root' class
	// as child classes don't get fully compiled. Instead they just override various asset players leaving the
	// full compilation up to the base class. 
	const IAnimClassInterface* GetRootClass() const
	{
		auto GetSuperClassInterface = [](const IAnimClassInterface* InClass) -> const IAnimClassInterface*
		{
			if(const UClass* ActualClass = GetActualAnimClass(InClass))
			{
				return GetFromClass(ActualClass->GetSuperClass());
			}

			return nullptr;
		};

		const IAnimClassInterface* RootClass = this;
		while(const IAnimClassInterface* NextClass = GetSuperClassInterface(RootClass))
		{
			RootClass = NextClass;
		}

		return RootClass;
	}

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

	static const IAnimClassInterface* GetFromClass(const UClass* InClass)
	{
		if (auto AnimClassInterface = Cast<const IAnimClassInterface>(InClass))
		{
			return AnimClassInterface;
		}
		if (auto DynamicClass = Cast<const UDynamicClass>(InClass))
		{
			DynamicClass->GetDefaultObject(true);
			return CastChecked<const IAnimClassInterface>(DynamicClass->AnimClassImplementation, ECastCheckedType::NullAllowed);
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

	static const UClass* GetActualAnimClass(const IAnimClassInterface* AnimClassInterface)
	{
		if (const UClass* ActualAnimClass = Cast<const UClass>(AnimClassInterface))
		{
			return ActualAnimClass;
		}
		if (const UObject* AsObject = Cast<const UObject>(AnimClassInterface))
		{
			return Cast<const UClass>(AsObject->GetOuter());
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
	virtual FStructProperty* GetRootAnimNodeProperty() const { return nullptr; }
};
