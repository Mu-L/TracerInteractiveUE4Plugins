// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRigBase.h"
#include "ControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"

FAnimNode_ControlRigBase::FAnimNode_ControlRigBase()
	: FAnimNode_CustomProperty()
	, InputSettings(FControlRigIOSettings())
	, OutputSettings(FControlRigIOSettings())
	, bExecute(true)
	, InternalBlendAlpha (1.f)
{

}

void FAnimNode_ControlRigBase::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::OnInitializeAnimInstance(InProxy, InAnimInstance);

	USkeletalMeshComponent* Component = InAnimInstance->GetOwningComponent();
	UControlRig* ControlRig = GetControlRig();
	if (Component && Component->SkeletalMesh && ControlRig)
	{
		UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(ControlRig->GetClass());
		if (BlueprintClass)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy);
			// node mapping container will be saved on the initialization part
			NodeMappingContainer = Component->SkeletalMesh->GetNodeMappingContainer(Blueprint);
		}
		// register skeletalmesh component for now
		ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, InAnimInstance->GetOwningComponent());
	}
}

void FAnimNode_ControlRigBase::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::Initialize_AnyThread(Context);
	Source.Initialize(Context);

	if (UControlRig* ControlRig = GetControlRig())
	{
		//Don't Inititialize the Control Rig here it may have the wrong VM on the CDO
		SetTargetInstance(ControlRig);
		ControlRig->RequestInit();
	}
}

void FAnimNode_ControlRigBase::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_ControlRigBase::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::Update_AnyThread(Context);
	Source.Update(Context);

	if (bExecute)
	{
		if (UControlRig* ControlRig = GetControlRig())
		{
			// @TODO: fix this to be thread-safe
			// Pre-update doesn't work for custom anim instances
			// FAnimNode_ControlRigExternalSource needs this to be called to reset to ref pose
			ControlRig->SetDeltaTime(Context.GetDeltaTime());
		}
	}
}

void FAnimNode_ControlRigBase::UpdateInput(UControlRig* ControlRig, const FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InputSettings.bUpdatePose)
	{
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		// get component pose from control rig
		FCSPose<FCompactPose> MeshPoses;
		// first I need to convert to local pose
		MeshPoses.InitPose(InOutput.Pose);

		// @re-think - now control rig contains init pose from their default hierarchy and current pose from this instance.
		// we may need this init pose somewhere (instance refpose)
		for (auto Iter = ControlRigBoneMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			FTransform ComponentTransform = MeshPoses.GetComponentSpaceTransform(FCompactPoseBoneIndex(Index));
			if (NodeMappingContainer.IsValid())
			{
				ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name).GetRelativeTransformReverse(ComponentTransform);
			}

			ControlRig->SetGlobalTransform(Name, ComponentTransform, false);
		}
	}

	if (InputSettings.bUpdateCurves)
	{
		// we just do name mapping 
		for (auto Iter = ControlRigCurveMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			ControlRig->SetCurveValue(Name, InOutput.Curve.Get(Index));
		}
	}
}

void FAnimNode_ControlRigBase::UpdateOutput(UControlRig* ControlRig, FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (OutputSettings.bUpdatePose)
	{
		// copy output of the rig
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		// get component pose from control rig
		FCSPose<FCompactPose> MeshPoses;
		MeshPoses.InitPose(InOutput.Pose);

		for (auto Iter = ControlRigBoneMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			FCompactPoseBoneIndex CompactPoseIndex(Index);
			FTransform ComponentTransform = ControlRig->GetGlobalTransform(Name);
			if (NodeMappingContainer.IsValid())
			{
				ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name) * ComponentTransform;
			}

			MeshPoses.SetComponentSpaceTransform(CompactPoseIndex, ComponentTransform);
		}

		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MeshPoses, InOutput.Pose);
	}

	if (OutputSettings.bUpdateCurves)
	{
		// update curve
		for (auto Iter = ControlRigCurveMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			const float Value = ControlRig->GetCurveValue(Name);
			InOutput.Curve.Set(Index, Value);
		}
	}
}

void FAnimNode_ControlRigBase::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FPoseContext SourcePose(Output);

	if (Source.GetLinkNode())
	{
		Source.Evaluate(SourcePose);
	}
	else
	{
		// apply refpose
		SourcePose.ResetToRefPose();
	}

	if (FAnimWeight::IsRelevant(InternalBlendAlpha))
	{
		if (FAnimWeight::IsFullWeight(InternalBlendAlpha))
		{
			ExecuteControlRig(SourcePose);
			Output = SourcePose;
		}
		else 
		{
			// this blends additively - by weight
			FPoseContext ControlRigPose(SourcePose);
			ControlRigPose = SourcePose;
			ExecuteControlRig(ControlRigPose);

			FPoseContext AdditivePose(ControlRigPose);
			AdditivePose = ControlRigPose;
			FAnimationRuntime::ConvertPoseToAdditive(AdditivePose.Pose, SourcePose.Pose);
			AdditivePose.Curve.ConvertToAdditive(SourcePose.Curve);
			Output = SourcePose;
			FAnimationRuntime::AccumulateAdditivePose(Output.Pose, AdditivePose.Pose, Output.Curve, AdditivePose.Curve, InternalBlendAlpha, AAT_LocalSpaceBase);
		}
	}
	else // if not relevant, skip to run control rig
		// this may cause issue if we have simulation node in the control rig that accumulates time
	{
		Output = SourcePose;
	}
}

void FAnimNode_ControlRigBase::ExecuteControlRig(FPoseContext& InOutput)
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		// first update input to the system
		UpdateInput(ControlRig, InOutput);

		if (bExecute)
		{
			// first evaluate control rig
			ControlRig->Evaluate_AnyThread();
		}

		// now update output
		UpdateOutput(ControlRig, InOutput);
	}
}

void FAnimNode_ControlRigBase::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);

	if (UControlRig* ControlRig = GetControlRig())
	{
		// fill up node names
		FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
		const TArray<FBoneIndexType>& RequiredBonesArray = RequiredBones.GetBoneIndicesArray();
		const int32 NumBones = RequiredBonesArray.Num();
		ControlRigBoneMapping.Reset();
		ControlRigCurveMapping.Reset();

		const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();

		// @todo: thread-safe? probably not in editor, but it may not be a big issue in editor
		if (NodeMappingContainer.IsValid())
		{
			// get target to source mapping table - this is reversed mapping table
			TMap<FName, FName> TargetToSourceMappingTable;
			NodeMappingContainer->GetTargetToSourceMappingTable(TargetToSourceMappingTable);

			// now fill up node name
			for (uint16 Index = 0; Index < NumBones; ++Index)
			{
				// get bone name, and find reverse mapping
				FName TargetNodeName = RefSkeleton.GetBoneName(RequiredBonesArray[Index]);
				FName* SourceName = TargetToSourceMappingTable.Find(TargetNodeName);
				if (SourceName)
				{
					ControlRigBoneMapping.Add(*SourceName, Index);
				}
			}
		}
		else
		{
			TArray<FName> NodeNames;
			TArray<FNodeItem> NodeItems;
			ControlRig->GetMappableNodeData(NodeNames, NodeItems);

			// even if not mapped, we map only node that exists in the controlrig
			for (uint16 Index = 0; Index < NumBones; ++Index)
			{
				const FName& BoneName = RefSkeleton.GetBoneName(RequiredBonesArray[Index]);
				if (NodeNames.Contains(BoneName))
				{
					ControlRigBoneMapping.Add(BoneName, Index);
				}
			}
		}

		// we just support curves by name only
		TArray<FName> const& CurveNames = RequiredBones.GetUIDToNameLookupTable();
		const FRigCurveContainer& RigCurveContainer = ControlRig->GetCurveContainer();
		for (uint16 Index = 0; Index < CurveNames.Num(); ++Index)
		{
			// see if the curve name exists in the control rig
			if (RigCurveContainer.GetIndex(CurveNames[Index]) != INDEX_NONE)
			{
				ControlRigCurveMapping.Add(CurveNames[Index], Index);
			}
		}
	}
}

UClass* FAnimNode_ControlRigBase::GetTargetClass() const
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		return ControlRig->GetClass();
	}

	return nullptr;
}

