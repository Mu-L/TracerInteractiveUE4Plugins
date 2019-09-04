// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRig.h"
#include "ControlRig.h"
#include "ControlRigComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "GameFramework/Actor.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"
#include "ControlRigVariables.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

FAnimNode_ControlRig::FAnimNode_ControlRig()
	: ControlRig(nullptr)
{
}

void FAnimNode_ControlRig::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	if (ControlRigClass)
	{
		ControlRig = NewObject<UControlRig>(InAnimInstance->GetOwningComponent(), ControlRigClass);
	}

	FAnimNode_ControlRigBase::OnInitializeAnimInstance(InProxy, InAnimInstance);

#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().AddRaw(this, &FAnimNode_ControlRig::OnObjectsReplaced);
	}
#endif // WITH_EDITOR

	InitializeProperties(InAnimInstance, GetTargetClass());
}

FAnimNode_ControlRig::~FAnimNode_ControlRig()
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
#endif // WITH_EDITOR
}
void FAnimNode_ControlRig::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(%s)"), *GetNameSafe(ControlRigClass.Get()));
	DebugData.AddDebugItem(DebugLine);
	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_ControlRig::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	FAnimNode_ControlRigBase::Update_AnyThread(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	PropagateInputProperties(Context.AnimInstanceProxy->GetAnimInstanceObject());
	Source.Update(Context);
}

void FAnimNode_ControlRig::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_ControlRigBase::Initialize_AnyThread(Context);

	Source.Initialize(Context);
}

void FAnimNode_ControlRig::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_ControlRigBase::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);

	FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	CurveMappingUIDs.Reset();
	TArray<FName> const& UIDToNameLookUpTable = RequiredBones.GetUIDToNameLookupTable();

	auto CacheCurveMappingUIDs = [&](const TMap<FName, FName>& Mapping, TArray<FName> const& InUIDToNameLookUpTable, 
		const FAnimationCacheBonesContext& InContext)
	{
		for (auto Iter = Mapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			const FName CurveName = Iter.Value();

			if (SourcePath != NAME_None && CurveName != NAME_None)
			{
				int32 Found = InUIDToNameLookUpTable.Find(CurveName);
				if (Found != INDEX_NONE)
				{
					// set value - sound should be UID
					CurveMappingUIDs.Add(Iter.Value()) = Found;
				}
				else
				{
					UE_LOG(LogAnimation, Warning, TEXT("Curve %s Not Found from the Skeleton %s"), 
						*CurveName.ToString(), *GetNameSafe(InContext.AnimInstanceProxy->GetSkeleton()));
				}
			}

			// @todo: should we clear the item if not found?
		}
	};

	CacheCurveMappingUIDs(InputMapping, UIDToNameLookUpTable, Context);
	CacheCurveMappingUIDs(OutputMapping, UIDToNameLookUpTable, Context);
}

void FAnimNode_ControlRig::Evaluate_AnyThread(FPoseContext & Output)
{
	// If not playing a montage, just pass through
	Source.Evaluate(Output);

	// evaluate 
	FAnimNode_ControlRigBase::Evaluate_AnyThread(Output);
}

void FAnimNode_ControlRig::PostSerialize(const FArchive& Ar)
{
	// after compile, we have to reinitialize
	// because it needs new execution code
	// since memory has changed
	if (Ar.IsObjectReferenceCollector())
	{
		if (ControlRig)
		{
			ControlRig->Initialize();
		}
	}
}

void FAnimNode_ControlRig::UpdateInput(UControlRig* InControlRig, const FPoseContext& InOutput)
{
	FAnimNode_ControlRigBase::UpdateInput(InControlRig, InOutput);
	// now go through variable mapping table and see if anything is mapping through input
	if (InputMapping.Num() > 0 && InControlRig)
	{
		for (auto Iter = InputMapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			if (SourcePath != NAME_None)
			{
				const FName CurveName = Iter.Value();

				SmartName::UID_Type UID = *CurveMappingUIDs.Find(CurveName);
				if (UID != SmartName::MaxUID)
				{
					const float Value = InOutput.Curve.Get(UID);
	
					// helper function to set input value for ControlRig
					// This converts to the proper destination type, and sets the float type Value
					if (!FControlRigIOHelper::SetInputValue(InControlRig, SourcePath, FControlRigIOTypes::GetTypeString<float>(), Value))
					{
						UE_LOG(LogAnimation, Warning, TEXT("[%s] Missing Input Property [%s]"), *GetNameSafe(InControlRig->GetClass()), *SourcePath.ToString());
					}
				}
			}
		} 
	}
}

void FAnimNode_ControlRig::UpdateOutput(UControlRig* InControlRig, FPoseContext& InOutput)
{
	FAnimNode_ControlRigBase::UpdateOutput(InControlRig, InOutput);

	// update output curves
	if (OutputMapping.Num() > 0 && InControlRig)
	{
		for (auto Iter = OutputMapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			const FName CurveName = Iter.Value();

			if (SourcePath != NAME_None)
			{
				// find Segment is right value
				float Value;
				// helper function to get output value and convert to float 
				if (FControlRigIOHelper::GetOutputValue(InControlRig, SourcePath, FControlRigIOTypes::GetTypeString<float>(), Value))
				{
					SmartName::UID_Type* UID = CurveMappingUIDs.Find(Iter.Value());
					if (UID)
					{
						InOutput.Curve.Set(*UID, Value);
					}
				}
				else
				{
					UE_LOG(LogAnimation, Warning, TEXT("[%s] Missing Output Property [%s]"), *GetNameSafe(ControlRig->GetClass()), *SourcePath.ToString());
				}
			}
		}
	}
}

void FAnimNode_ControlRig::SetIOMapping(bool bInput, const FName& SourceProperty, const FName& TargetCurve)
{
	UClass* TargetClass = GetTargetClass();
	if (TargetClass)
	{
		UControlRig* CDO = TargetClass->GetDefaultObject<UControlRig>();
		if (CDO)
		{
			TMap<FName, FName>& MappingData = (bInput) ? InputMapping : OutputMapping;

			// if it's valid as of now, we add it
			if (CDO->IsValidIOVariables(bInput, SourceProperty))
			{
				if (TargetCurve == NAME_None)
				{
					MappingData.Remove(SourceProperty);
				}
				else
				{
					MappingData.FindOrAdd(SourceProperty) = TargetCurve;
				}
			}
		}
	}
}

FName FAnimNode_ControlRig::GetIOMapping(bool bInput, const FName& SourceProperty) const
{
	const TMap<FName, FName>& MappingData = (bInput) ? InputMapping : OutputMapping;
	if (const FName* NameFound = MappingData.Find(SourceProperty))
	{
		return *NameFound;
	}

	return NAME_None;
}

#if WITH_EDITOR
void FAnimNode_ControlRig::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if (ControlRig)
	{
		UObject* const* NewFound = OldToNewInstanceMap.Find(ControlRig);

		if (NewFound)
		{
			// recache the properties
			bReinitializeProperties = true;
		}
	}
}
#endif	// #if WITH_EDITOR