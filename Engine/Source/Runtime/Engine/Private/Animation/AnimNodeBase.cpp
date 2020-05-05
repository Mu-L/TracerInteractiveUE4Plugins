// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"
#include "UObject/CoreObjectVersion.h"

void* FExposedValueCopyRecord::GetDestAddr(FAnimInstanceProxy* Proxy, const FProperty* NodeProperty) const
{
	UObject* Instance = Proxy->GetAnimInstanceObject();
	if (const FArrayProperty* DestArrayProperty = CastField<FArrayProperty>(DestProperty.Get()))
	{
		if (!bInstanceIsTarget)
		{
			FScriptArrayHelper PropArrayHelper(DestArrayProperty, DestProperty->ContainerPtrToValuePtr<uint8>( NodeProperty->ContainerPtrToValuePtr<uint8>(Instance) ));
			return PropArrayHelper.GetRawPtr(DestArrayIndex);
		}
		else
		{
			FScriptArrayHelper PropArrayHelper(DestArrayProperty, Instance);
			return PropArrayHelper.GetRawPtr(DestArrayIndex);
		}
	}
	else if(!bInstanceIsTarget)
	{
		return DestProperty->ContainerPtrToValuePtr<uint8>( NodeProperty->ContainerPtrToValuePtr<uint8>(Instance) );
	}
	else
	{
		return DestProperty->ContainerPtrToValuePtr<uint8>( Instance );
	}
}

const void* FExposedValueCopyRecord::GetSourceAddr(FAnimInstanceProxy* Proxy) const
{	
	const UObject* Instance = Proxy->GetAnimInstanceObject();
	if (const FArrayProperty* SourceArrayProperty = CastField<FArrayProperty>(CachedSourceProperty.Get()))
	{
		// the compiler should not be generating any code that calls down this path at the moment - it is untested
		check(false);
	}

	if(CachedSourceStructSubProperty.Get())
	{
		return CachedSourceStructSubProperty->ContainerPtrToValuePtr<uint8>(
			CachedSourceProperty->ContainerPtrToValuePtr<uint8>(Instance),
			SourceArrayIndex
		);
	}
	else
	{
		return CachedSourceProperty->ContainerPtrToValuePtr<uint8>(Instance, SourceArrayIndex);
	}
}

/////////////////////////////////////////////////////
// FAnimationBaseContext

FAnimationBaseContext::FAnimationBaseContext()
	: AnimInstanceProxy(nullptr)
#if ANIM_TRACE_ENABLED
	, CurrentNodeId(INDEX_NONE)
	, PreviousNodeId(INDEX_NONE)
#endif
{
}

FAnimationBaseContext::FAnimationBaseContext(FAnimInstanceProxy* InAnimInstanceProxy)
	: AnimInstanceProxy(InAnimInstanceProxy)
#if ANIM_TRACE_ENABLED
	, CurrentNodeId(INDEX_NONE)
	, PreviousNodeId(INDEX_NONE)
#endif
{
}

FAnimationBaseContext::FAnimationBaseContext(const FAnimationBaseContext& InContext)
	: AnimInstanceProxy(InContext.AnimInstanceProxy)
#if ANIM_TRACE_ENABLED
	, CurrentNodeId(InContext.CurrentNodeId)
	, PreviousNodeId(InContext.PreviousNodeId)
#endif
{
}

IAnimClassInterface* FAnimationBaseContext::GetAnimClass() const
{
	return AnimInstanceProxy ? AnimInstanceProxy->GetAnimClassInterface() : nullptr;
}

#if WITH_EDITORONLY_DATA
UAnimBlueprint* FAnimationBaseContext::GetAnimBlueprint() const
{
	return AnimInstanceProxy ? AnimInstanceProxy->GetAnimBlueprint() : nullptr;
}
#endif //WITH_EDITORONLY_DATA

void FAnimationBaseContext::LogMessageInternal(FName InLogType, EMessageSeverity::Type InSeverity, FText InMessage) const
{
	AnimInstanceProxy->LogMessage(InLogType, InSeverity, InMessage);
}
/////////////////////////////////////////////////////
// FPoseContext

void FPoseContext::Initialize(FAnimInstanceProxy* InAnimInstanceProxy)
{
	checkSlow(AnimInstanceProxy && AnimInstanceProxy->GetRequiredBones().IsValid());
	const FBoneContainer& RequiredBone = AnimInstanceProxy->GetRequiredBones();
	Pose.SetBoneContainer(&RequiredBone);
	Curve.InitFrom(RequiredBone);
}

/////////////////////////////////////////////////////
// FComponentSpacePoseContext

void FComponentSpacePoseContext::ResetToRefPose()
{
	checkSlow(AnimInstanceProxy && AnimInstanceProxy->GetRequiredBones().IsValid());
	const FBoneContainer& RequiredBone = AnimInstanceProxy->GetRequiredBones();
	Pose.InitPose(&RequiredBone);
	Curve.InitFrom(RequiredBone);
}

/////////////////////////////////////////////////////
// FAnimNode_Base

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FAnimNode_Base::Initialize(const FAnimationInitializeContext& Context)
{
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FAnimNode_Base::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Initialize(Context);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAnimNode_Base::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	CacheBones(Context);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAnimNode_Base::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Update(Context);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAnimNode_Base::Evaluate_AnyThread(FPoseContext& Output)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Evaluate(Output);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAnimNode_Base::EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	EvaluateComponentSpace(Output);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FAnimNode_Base::IsLODEnabled(FAnimInstanceProxy* AnimInstanceProxy)
{
	const int32 NodeLODThreshold = GetLODThreshold();
	return ((NodeLODThreshold == INDEX_NONE) || (AnimInstanceProxy->GetLODLevel() <= NodeLODThreshold));
}

void FAnimNode_Base::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	RootInitialize(InProxy);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAnimNode_Base::ResetDynamics(ETeleportType InTeleportType)
{
	// Call legacy implementation for backwards compatibility
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ResetDynamics();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}


/////////////////////////////////////////////////////
// FPoseLinkBase

void FPoseLinkBase::AttemptRelink(const FAnimationBaseContext& Context)
{
	// Do the linkage
	if ((LinkedNode == NULL) && (LinkID != INDEX_NONE))
	{
		IAnimClassInterface* AnimBlueprintClass = Context.GetAnimClass();
		check(AnimBlueprintClass);

		// adding ensure. We had a crash on here
		const TArray<FStructProperty*>& AnimNodeProperties = AnimBlueprintClass->GetAnimNodeProperties();
		if ( ensure(AnimNodeProperties.IsValidIndex(LinkID)) )
		{
			FStructProperty* LinkedProperty = AnimNodeProperties[LinkID];
			void* LinkedNodePtr = LinkedProperty->ContainerPtrToValuePtr<void>(Context.AnimInstanceProxy->GetAnimInstanceObject());
			LinkedNode = (FAnimNode_Base*)LinkedNodePtr;
		}
	}
}

void FPoseLinkBase::Initialize(const FAnimationInitializeContext& Context)
{
#if DO_CHECK
	checkf(!bProcessed, TEXT("Initialize already in progress, circular link for AnimInstance [%s] Blueprint [%s]"), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

	AttemptRelink(Context);

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	InitializationCounter.SynchronizeWith(Context.AnimInstanceProxy->GetInitializationCounter());

	// Initialization will require update to be called before an evaluate.
	UpdateCounter.Reset();
#endif

	// Do standard initialization
	if (LinkedNode != NULL)
	{
		LinkedNode->Initialize_AnyThread(Context);
	}
}

void FPoseLinkBase::SetLinkNode(struct FAnimNode_Base* NewLinkNode)
{
	// this is custom interface, only should be used by native handlers
	LinkedNode = NewLinkNode;
}

void FPoseLinkBase::SetDynamicLinkNode(struct FPoseLinkBase* InPoseLink)
{
	if(InPoseLink)
	{
		LinkedNode = InPoseLink->LinkedNode;
#if WITH_EDITORONLY_DATA
		SourceLinkID = InPoseLink->SourceLinkID;
#endif
		LinkID = InPoseLink->LinkID;
	}
	else
	{
		LinkedNode = nullptr;
#if WITH_EDITORONLY_DATA
		SourceLinkID = INDEX_NONE;
#endif
		LinkID = INDEX_NONE;
	}
}

FAnimNode_Base* FPoseLinkBase::GetLinkNode()
{
	return LinkedNode;
}

const FExposedValueHandler& FAnimNode_Base::GetEvaluateGraphExposedInputs()
{
	// Inverting control (entering via the immutable data rather than the mutable data) would allow
	// us to remove this static local. Would also allow us to remove the vtable from FAnimNode_Base.
	static const FExposedValueHandler Default;
	if(ExposedValueHandler)
	{
		return *ExposedValueHandler;
	}
	else
	{
		return Default;
	}
}

void FPoseLinkBase::CacheBones(const FAnimationCacheBonesContext& Context) 
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "CacheBones already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	CachedBonesCounter.SynchronizeWith(Context.AnimInstanceProxy->GetCachedBonesCounter());
#endif

	if (LinkedNode != NULL)
	{
		LinkedNode->CacheBones_AnyThread(Context);
	}
}

void FPoseLinkBase::Update(const FAnimationUpdateContext& Context)
{
#if ENABLE_VERBOSE_ANIM_PERF_TRACKING
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FPoseLinkBase_Update);
#endif // ENABLE_VERBOSE_ANIM_PERF_TRACKING

#if DO_CHECK
	checkf( !bProcessed, TEXT( "Update already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if WITH_EDITOR
	if (GIsEditor)
	{
		if (LinkedNode == NULL)
		{
			//@TODO: Should only do this when playing back
			AttemptRelink(Context);
		}

		// Record the node line activation
		if (LinkedNode != NULL)
		{
			if (Context.AnimInstanceProxy->IsBeingDebugged())
			{
				Context.AnimInstanceProxy->RecordNodeVisit(LinkID, SourceLinkID, Context.GetFinalBlendWeight());
			}
		}
	}
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronized_Counter(Context.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling Update without initialization!"));
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());
#endif

	if (LinkedNode != NULL)
	{
#if ANIM_TRACE_ENABLED
		{
			FAnimationUpdateContext LinkContext(Context.WithNodeId(LinkID));
			TRACE_SCOPED_ANIM_NODE(LinkContext);
			LinkedNode->Update_AnyThread(LinkContext);
		}
#else
		LinkedNode->Update_AnyThread(Context);
#endif
	}
}

void FPoseLinkBase::GatherDebugData(FNodeDebugData& DebugData)
{
	if(LinkedNode != NULL)
	{
		LinkedNode->GatherDebugData(DebugData);
	}
}

/////////////////////////////////////////////////////
// FPoseLink

void FPoseLink::Evaluate(FPoseContext& Output)
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "Evaluate already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Output.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Output.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if WITH_EDITOR
	if ((LinkedNode == NULL) && GIsEditor)
	{
		//@TODO: Should only do this when playing back
		AttemptRelink(Output);
	}
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling Evaluate without initialization!"));
	checkf(UpdateCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetUpdateCounter()), TEXT("Calling Evaluate without Update for this node!"));
	checkf(CachedBonesCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetCachedBonesCounter()), TEXT("Calling Evaluate without CachedBones!"));
	EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());
#endif

	if (LinkedNode != NULL)
	{
#if ENABLE_ANIMNODE_POSE_DEBUG
		CurrentPose.ResetToAdditiveIdentity();
#endif

		{
#if ANIM_TRACE_ENABLED
			Output.SetNodeId(LinkID);
			TRACE_SCOPED_ANIM_NODE(Output);
#endif
			LinkedNode->Evaluate_AnyThread(Output);
		}

#if ENABLE_ANIMNODE_POSE_DEBUG
		CurrentPose.CopyBonesFrom(Output.Pose);
#endif

#if WITH_EDITOR
		Output.AnimInstanceProxy->RegisterWatchedPose(Output.Pose, LinkID);
#endif
	}
	else
	{
		//@TODO: Warning here?
		Output.ResetToRefPose();
	}

	// Detect non valid output
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (Output.ContainsNaN())
	{
		// Show bone transform with some useful debug info
		for (const FTransform& Bone : Output.Pose.GetBones())
		{
			if (Bone.ContainsNaN())
			{
				ensureMsgf(!Bone.ContainsNaN(), TEXT("Bone transform contains NaN from AnimInstance:[%s] Node:[%s] Value:[%s]")
					, *Output.AnimInstanceProxy->GetAnimInstanceName(), LinkedNode ? *LinkedNode->StaticStruct()->GetName() : TEXT("NULL"), *Bone.ToString());
			}
		}
	}

	if (!Output.IsNormalized())
	{
		// Show bone transform with some useful debug info
		for (const FTransform& Bone : Output.Pose.GetBones())
		{
			if (!Bone.IsRotationNormalized())
			{
				ensureMsgf(Bone.IsRotationNormalized(), TEXT("Bone Rotation not normalized from AnimInstance:[%s] Node:[%s] Rotation:[%s]")
					, *Output.AnimInstanceProxy->GetAnimInstanceName(), LinkedNode ? *LinkedNode->StaticStruct()->GetName() : TEXT("NULL"), *Bone.GetRotation().ToString());
			}
		}
	}
#endif
}

/////////////////////////////////////////////////////
// FComponentSpacePoseLink

void FComponentSpacePoseLink::EvaluateComponentSpace(FComponentSpacePoseContext& Output)
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "EvaluateComponentSpace already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Output.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Output.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling EvaluateComponentSpace without initialization!"));
	checkf(CachedBonesCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetCachedBonesCounter()), TEXT("Calling EvaluateComponentSpace without CachedBones!"));
	checkf(UpdateCounter.IsSynchronized_Counter(Output.AnimInstanceProxy->GetUpdateCounter()), TEXT("Calling EvaluateComponentSpace without Update for this node!"));
	EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());
#endif

	if (LinkedNode != NULL)
	{
		{
#if ANIM_TRACE_ENABLED
			Output.SetNodeId(LinkID);
			TRACE_SCOPED_ANIM_NODE(Output);
#endif
			LinkedNode->EvaluateComponentSpace_AnyThread(Output);
		}

#if WITH_EDITOR
		Output.AnimInstanceProxy->RegisterWatchedPose(Output.Pose, LinkID);
#endif
	}
	else
	{
		//@TODO: Warning here?
		Output.ResetToRefPose();
	}

	// Detect non valid output
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (Output.ContainsNaN())
	{
		// Show bone transform with some useful debug info
		for (const FTransform& Bone : Output.Pose.GetPose().GetBones())
		{
			if (Bone.ContainsNaN())
			{
				ensureMsgf(!Bone.ContainsNaN(), TEXT("Bone transform contains NaN from AnimInstance:[%s] Node:[%s] Value:[%s]")
					, *Output.AnimInstanceProxy->GetAnimInstanceName(), LinkedNode ? *LinkedNode->StaticStruct()->GetName() : TEXT("NULL"), *Bone.ToString());
			}
		}
	}

	if (!Output.IsNormalized())
	{
		// Show bone transform with some useful debug info
		for (const FTransform& Bone : Output.Pose.GetPose().GetBones())
		{
			if (!Bone.IsRotationNormalized())
			{
				ensureMsgf(Bone.IsRotationNormalized(), TEXT("Bone Rotation not normalized from AnimInstance:[%s] Node:[%s] Value:[%s]")
					, *Output.AnimInstanceProxy->GetAnimInstanceName(), LinkedNode ? *LinkedNode->StaticStruct()->GetName() : TEXT("NULL"), *Bone.ToString());
			}
		}
	}
#endif
}

/////////////////////////////////////////////////////
// FComponentSpacePoseContext

bool FComponentSpacePoseContext::ContainsNaN() const
{
	return Pose.GetPose().ContainsNaN();
}

bool FComponentSpacePoseContext::IsNormalized() const
{
	return Pose.GetPose().IsNormalized();
}

/////////////////////////////////////////////////////
// FNodeDebugData

void FNodeDebugData::AddDebugItem(FString DebugData, bool bPoseSource)
{
	check(NodeChain.Num() == 0 || NodeChain.Last().ChildNodeChain.Num() == 0); //Cannot add to this chain once we have branched

	NodeChain.Add( DebugItem(DebugData, bPoseSource) );
	NodeChain.Last().ChildNodeChain.Reserve(ANIM_NODE_DEBUG_MAX_CHILDREN);
}

FNodeDebugData& FNodeDebugData::BranchFlow(float BranchWeight, FString InNodeDescription)
{
	NodeChain.Last().ChildNodeChain.Add(FNodeDebugData(AnimInstance, BranchWeight*AbsoluteWeight, InNodeDescription, RootNodePtr));
	NodeChain.Last().ChildNodeChain.Last().NodeChain.Reserve(ANIM_NODE_DEBUG_MAX_CHAIN);
	return NodeChain.Last().ChildNodeChain.Last();
}

FNodeDebugData* FNodeDebugData::GetCachePoseDebugData(float GlobalWeight)
{
	check(RootNodePtr);

	RootNodePtr->SaveCachePoseNodes.Add( FNodeDebugData(AnimInstance, GlobalWeight, FString(), RootNodePtr) );
	RootNodePtr->SaveCachePoseNodes.Last().NodeChain.Reserve(ANIM_NODE_DEBUG_MAX_CHAIN);
	return &RootNodePtr->SaveCachePoseNodes.Last();
}

void FNodeDebugData::GetFlattenedDebugData(TArray<FFlattenedDebugData>& FlattenedDebugData, int32 Indent, int32& ChainID)
{
	int32 CurrChainID = ChainID;
	for(DebugItem& Item : NodeChain)
	{
		FlattenedDebugData.Add( FFlattenedDebugData(Item.DebugData, AbsoluteWeight, Indent, CurrChainID, Item.bPoseSource) );
		bool bMultiBranch = Item.ChildNodeChain.Num() > 1;
		int32 ChildIndent = bMultiBranch ? Indent + 1 : Indent;
		for(FNodeDebugData& Child : Item.ChildNodeChain)
		{
			if(bMultiBranch)
			{
				// If we only have one branch we treat it as the same really
				// as we may have only changed active status
				++ChainID;
			}
			Child.GetFlattenedDebugData(FlattenedDebugData, ChildIndent, ChainID);
		}
	}

	// Do CachePose nodes only from the root.
	if (RootNodePtr == this)
	{
		for (FNodeDebugData& CachePoseData : SaveCachePoseNodes)
		{
			++ChainID;
			CachePoseData.GetFlattenedDebugData(FlattenedDebugData, 0, ChainID);
		}
	}
}

#if WITH_EDITORONLY_DATA
void FExposedValueCopyRecord::PostSerialize(const FArchive& Ar)
{
	// backwards compatibility: check value of deprecated source property and patch up property name
	if(SourceProperty_DEPRECATED && SourcePropertyName == NAME_None)
	{
		SourcePropertyName = ((UObject*)SourceProperty_DEPRECATED)->GetFName();
	}
}
#endif

void FExposedValueHandler::Initialize(TArray<FExposedValueHandler>& Handlers, UObject* ClassDefaultObject )
{
	for(FExposedValueHandler& Handler : Handlers)
	{
		FAnimNode_Base* AnimNode = Handler.ValueHandlerNodeProperty->ContainerPtrToValuePtr<FAnimNode_Base>(ClassDefaultObject);
		check(AnimNode);
		AnimNode->SetExposedValueHandler(&Handler);
		Handler.Initialize(ClassDefaultObject, Handler.ValueHandlerNodeProperty->GetOffset_ForInternal());
	}
}

void FExposedValueHandler::Initialize(UObject* AnimInstanceObject, int32 NodeOffset)
{
	// bInitialized may no longer be necessary, but leaving alone for now:
	if (bInitialized)
	{
		return;
	}

	if (BoundFunction != NAME_None)
	{
		// This cached function is NULL when the CDO is initially serialized, or (in editor) when the class has been
		// recompiled and any instances have been re-instanced. When new instances are spawned, this function is
		// duplicated (it is a FProperty) onto those instances so we dont pay the cost of the FindFunction() call
#if !WITH_EDITOR
		if (Function == nullptr)
#endif
		{
			// we cant call FindFunction on anything but the game thread as it accesses a shared map in the object's class
			check(IsInGameThread());
			Function = AnimInstanceObject->FindFunction(BoundFunction);
			check(Function);
		}
	}
	else
	{
		Function = NULL;
	}

	// initialize copy records
	for (FExposedValueCopyRecord& CopyRecord : CopyRecords)
	{
		// We do a similar thing to the above function caching process for properties here too
#if !WITH_EDITOR
		if (CopyRecord.CachedSourceProperty == nullptr)
#endif
		{
			CopyRecord.CachedSourceProperty = AnimInstanceObject->GetClass()->FindPropertyByName(CopyRecord.SourcePropertyName);
		}
		check(CopyRecord.CachedSourceProperty.Get());

		if (FArrayProperty* SourceArrayProperty = CastField<FArrayProperty>(CopyRecord.CachedSourceProperty.Get()))
		{
			// the compiler should not be generating any code that calls down this path at the moment - it is untested
			check(false);
			//	FScriptArrayHelper ArrayHelper(SourceArrayProperty, CopyRecord.CachedSourceProperty->ContainerPtrToValuePtr<uint8>(AnimInstanceObject));
			//	check(ArrayHelper.IsValidIndex(CopyRecord.SourceArrayIndex));
			//	CopyRecord.Source = ArrayHelper.GetRawPtr(CopyRecord.SourceArrayIndex);
			//	CopyRecord.Size = ArrayHelper.Num() * SourceArrayProperty->Inner->GetSize();
		}
		else
		{
			if (CopyRecord.SourceSubPropertyName != NAME_None)
			{
				FStructProperty* SourceStructProperty = CastFieldChecked<FStructProperty>(CopyRecord.CachedSourceProperty.Get());
#if !WITH_EDITOR
				if (CopyRecord.CachedSourceStructSubProperty == nullptr)
#endif
				{
					CopyRecord.CachedSourceStructSubProperty = SourceStructProperty->Struct->FindPropertyByName(CopyRecord.SourceSubPropertyName);
				}
				check(CopyRecord.CachedSourceStructSubProperty.Get());
				CopyRecord.Size = CopyRecord.CachedSourceStructSubProperty->GetSize();
			}
			else
			{
				CopyRecord.Size = CopyRecord.CachedSourceProperty->GetSize();
			}
		}

		if (FArrayProperty* DestArrayProperty = CastField<FArrayProperty>(CopyRecord.DestProperty.Get()))
		{
		}
		else if (CopyRecord.DestProperty != nullptr)
		{
			if (CopyRecord.bInstanceIsTarget)
			{
				// Re-find our dest property as it (or its class outer) may have changed
				if (CopyRecord.DestProperty->GetOwner<UObject>() != AnimInstanceObject->GetClass())
				{
					CopyRecord.DestProperty = AnimInstanceObject->GetClass()->FindPropertyByName(CopyRecord.DestProperty->GetFName());
				}
				check(CopyRecord.DestProperty.Get());
			}

			if (CastField<FNameProperty>(CopyRecord.DestProperty.Get()))
			{
				CopyRecord.CopyType = ECopyType::NameProperty;
			}
			else if (CastField<FBoolProperty>(CopyRecord.DestProperty.Get()))
			{
				CopyRecord.CopyType = ECopyType::BoolProperty;
			}
			else if (CastField<FStructProperty>(CopyRecord.DestProperty.Get()))
			{
				CopyRecord.CopyType = ECopyType::StructProperty;
			}
			else if (CastField<FObjectPropertyBase>(CopyRecord.DestProperty.Get()))
			{
				CopyRecord.CopyType = ECopyType::ObjectProperty;
			}
			else
			{
				check(CopyRecord.DestProperty->PropertyFlags & CPF_IsPlainOldData);
				CopyRecord.CopyType = ECopyType::PlainProperty;
			}
		}
	}

	bInitialized = true;
}

void FExposedValueHandler::Execute(const FAnimationBaseContext& Context) const
{
	if (Function != nullptr)
	{
		Context.AnimInstanceProxy->GetAnimInstanceObject()->ProcessEvent(Function, NULL);
	}

	for(const FExposedValueCopyRecord& CopyRecord : CopyRecords)
	{
		// if any of these checks fail then it's likely that Initialize has not been called.
		// has new anim node type been added that doesnt call the base class Initialize()?
		checkSlow(CopyRecord.Size != 0);
		
		FProperty* SourceProperty = CopyRecord.CachedSourceStructSubProperty != nullptr ? CopyRecord.CachedSourceStructSubProperty.Get() : CopyRecord.CachedSourceProperty.Get();

		void* Dest = CopyRecord.GetDestAddr(Context.AnimInstanceProxy, *ValueHandlerNodeProperty);
		const void* Src = CopyRecord.GetSourceAddr(Context.AnimInstanceProxy);;

		switch(CopyRecord.PostCopyOperation)
		{
		case EPostCopyOperation::None:
			{
				switch(CopyRecord.CopyType)
				{
				default:
				case ECopyType::PlainProperty:
					FMemory::Memcpy(Dest, Src, CopyRecord.Size);
					break;
				case ECopyType::BoolProperty:
					{
						bool bValue = static_cast<FBoolProperty*>(SourceProperty)->GetPropertyValue(Src);
						static_cast<FBoolProperty*>(CopyRecord.DestProperty.Get())->SetPropertyValue(Dest, bValue);
					}
					break;
				case ECopyType::StructProperty:
					static_cast<FStructProperty*>(CopyRecord.DestProperty.Get())->Struct->CopyScriptStruct(Dest, Src);
					break;
				case ECopyType::ObjectProperty:
					{
						UObject* Value = static_cast<FObjectPropertyBase*>(SourceProperty)->GetObjectPropertyValue(Src);
						static_cast<FObjectPropertyBase*>(CopyRecord.DestProperty.Get())->SetObjectPropertyValue(Dest, Value);
					}
					break;
				case ECopyType::NameProperty:
					{
						const FName& NameValue = static_cast<FNameProperty*>(SourceProperty)->GetPropertyValue(Src);
						static_cast<FNameProperty*>(CopyRecord.DestProperty.Get())->SetPropertyValue(Dest, NameValue);
					}
					break;
				}
			}
			break;
		case EPostCopyOperation::LogicalNegateBool:
			{
				check(SourceProperty != nullptr && CopyRecord.DestProperty != nullptr);

				bool bValue = static_cast<FBoolProperty*>(SourceProperty)->GetPropertyValue(Src);
				if (CopyRecord.CopyType == ECopyType::BoolProperty)
				{
					static_cast<FBoolProperty*>(CopyRecord.DestProperty.Get())->SetPropertyValue(Dest, !bValue);
				}
				else if (FArrayProperty* DestArrayProperty = CastField<FArrayProperty>(CopyRecord.DestProperty.Get()))
				{
					static_cast<FBoolProperty*>(DestArrayProperty->Inner)->SetPropertyValue(Dest, !bValue); // added to support arrays
				}
			}
			break;
		}
	}
}
