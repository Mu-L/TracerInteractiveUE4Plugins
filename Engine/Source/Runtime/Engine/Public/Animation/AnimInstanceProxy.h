// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimTypes.h"
#include "BoneContainer.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimBlueprint.h"
#include "BonePose.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/PoseSnapshot.h"
#include "Animation/AnimInstance.h"
#include "Engine/PoseWatch.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Logging/TokenizedMessage.h"
#include "Animation/AnimTrace.h"

#include "AnimInstanceProxy.generated.h"

class UBlendSpaceBase;
struct FAnimNode_AssetPlayerBase;
struct FAnimNode_Base;
struct FAnimNode_SaveCachedPose;
struct FAnimNode_StateMachine;
struct FAnimNode_LinkedInputPose;
struct FNodeDebugData;
struct FPoseContext;

// Disable debugging information for shipping and test builds.
#define ENABLE_ANIM_DRAW_DEBUG (1 && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

// Disable node logging for shipping and test builds
#define ENABLE_ANIM_LOGGING (1 && !NO_LOGGING && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

extern const FName NAME_AnimBlueprintLog;
extern const FName NAME_Evaluate;
extern const FName NAME_Update;
extern const FName NAME_AnimGraph;

UENUM()
namespace EDrawDebugItemType
{
	enum Type
	{
		DirectionalArrow,
		Sphere,
		Line,
		OnScreenMessage,
		CoordinateSystem,
	};
}

USTRUCT()
struct FQueuedDrawDebugItem 
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TEnumAsByte<EDrawDebugItemType::Type> ItemType;

	UPROPERTY(Transient)
	FVector StartLoc;

	UPROPERTY(Transient)
	FVector EndLoc;

	UPROPERTY(Transient)
	FVector Center;

	UPROPERTY(Transient)
	FRotator Rotation;

	UPROPERTY(Transient)
	float Radius;

	UPROPERTY(Transient)
	float Size;

	UPROPERTY(Transient)
	int32 Segments;

	UPROPERTY(Transient)
	FColor Color;

	UPROPERTY(Transient)
	bool bPersistentLines;

	UPROPERTY(Transient)
	float LifeTime;

	UPROPERTY(Transient)
	float Thickness;

	UPROPERTY(Transient)
	FString Message;

	UPROPERTY(Transient)
	FVector2D TextScale;
};

/** Proxy object passed around during animation tree update in lieu of a UAnimInstance */
USTRUCT(meta = (DisplayName = "Native Variables"))
struct ENGINE_API FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FAnimInstanceProxy()
		: AnimInstanceObject(nullptr)
		, AnimClassInterface(nullptr)
		, Skeleton(nullptr)
		, SkeletalMeshComponent(nullptr)
		, CurrentDeltaSeconds(0.0f)
		, CurrentTimeDilation(1.0f)
		, RootNode(nullptr)
		, DefaultLinkedInstanceInputNode(nullptr)
		, SyncGroupWriteIndex(0)
		, RootMotionMode(ERootMotionMode::NoRootMotionExtraction)
		, FrameCounterForUpdate(0)
		, FrameCounterForNodeUpdate(0)
		, CacheBonesRecursionCounter(0)
		, bUpdatingRoot(false)
		, bBoneCachesInvalidated(false)
		, bShouldExtractRootMotion(false)
		, bDeferRootNodeInitialization(false)
#if WITH_EDITORONLY_DATA
		, bIsBeingDebugged(false)
#endif
	{
	}

	FAnimInstanceProxy(UAnimInstance* Instance)
		: AnimInstanceObject(Instance)
		, AnimClassInterface(IAnimClassInterface::GetFromClass(Instance->GetClass()))
		, Skeleton(nullptr)
		, SkeletalMeshComponent(nullptr)
		, CurrentDeltaSeconds(0.0f)
		, CurrentTimeDilation(1.0f)
		, RootNode(nullptr)
		, DefaultLinkedInstanceInputNode(nullptr)
		, SyncGroupWriteIndex(0)
		, RootMotionMode(ERootMotionMode::NoRootMotionExtraction)
		, FrameCounterForUpdate(0)
		, FrameCounterForNodeUpdate(0)
		, CacheBonesRecursionCounter(0)
		, bUpdatingRoot(false)
		, bBoneCachesInvalidated(false)
		, bShouldExtractRootMotion(false)
		, bDeferRootNodeInitialization(false)
#if WITH_EDITORONLY_DATA
		, bIsBeingDebugged(false)
#endif
	{
	}

	virtual ~FAnimInstanceProxy() {}

	// Get the IAnimClassInterface associated with this context, if there is one.
	// Note: This can return NULL, so check the result.
	IAnimClassInterface* GetAnimClassInterface() const
	{
		return AnimClassInterface;
	}

	// Get the Blueprint Generated Class associated with this context, if there is one.
	// Note: This can return NULL, so check the result.
	UE_DEPRECATED(4.11, "GetAnimBlueprintClass() is deprecated, UAnimBlueprintGeneratedClass should not be directly used at runtime. Please use GetAnimClassInterface() instead.")
	UAnimBlueprintGeneratedClass* GetAnimBlueprintClass() const
	{
		return Cast<UAnimBlueprintGeneratedClass>(IAnimClassInterface::GetActualAnimClass(AnimClassInterface));
	}

	/** Get the last DeltaSeconds passed into PreUpdate() */
	float GetDeltaSeconds() const
	{
		return CurrentDeltaSeconds;
	}

	/** Get the last time dilation, gleaned from world settings */
	float GetTimeDilation() const
	{
		return CurrentTimeDilation;
	}

#if WITH_EDITORONLY_DATA
	/** Whether the UAnimInstance this context refers to is currently being debugged in the editor */
	bool IsBeingDebugged() const
	{
		return bIsBeingDebugged;
	}

	/** Record a visited node in the debugger */
	void RecordNodeVisit(int32 TargetNodeIndex, int32 SourceNodeIndex, float BlendWeight)
	{
		new (UpdatedNodesThisFrame) FAnimBlueprintDebugData::FNodeVisit(SourceNodeIndex, TargetNodeIndex, BlendWeight);
	}

	UAnimBlueprint* GetAnimBlueprint() const
	{
		UClass* ActualAnimClass = IAnimClassInterface::GetActualAnimClass(AnimClassInterface);
		return ActualAnimClass ? Cast<UAnimBlueprint>(ActualAnimClass->ClassGeneratedBy) : nullptr;
	}

	// Record pose for node of ID LinkID if it is currently being watched
	void RegisterWatchedPose(const FCompactPose& Pose, int32 LinkID);
	void RegisterWatchedPose(const FCSPose<FCompactPose>& Pose, int32 LinkID);
#endif

	// flip sync group read/write indices
	void TickSyncGroupWriteIndex()
	{ 
		SyncGroupWriteIndex = GetSyncGroupReadIndex();
	}

	/** Get the sync group we are currently reading from */
	const TArray<FAnimGroupInstance>& GetSyncGroupRead() const
	{ 
		return SyncGroupArrays[GetSyncGroupReadIndex()]; 
	}

	/** Get the ungrouped active player we are currently reading from */
	const TArray<FAnimTickRecord>& GetUngroupedActivePlayersRead() 
	{ 
		return UngroupedActivePlayerArrays[GetSyncGroupReadIndex()]; 
	}

	/** Tick active asset players. */
	void TickAssetPlayerInstances(float DeltaSeconds);

	/** Tick active asset players. This overload with no parameters uses CurrentDeltaSeconds */
	void TickAssetPlayerInstances();

	/** Queues an Anim Notify from the shared list on our generated class */
	void AddAnimNotifyFromGeneratedClass(int32 NotifyIndex);

	/** Trigger any anim notifies */
	void TriggerAnimNotifies(USkeletalMeshComponent* SkelMeshComp, float DeltaSeconds);

	/** Check whether the supplied skeleton is compatible with this instance's skeleton */
	bool IsSkeletonCompatible(USkeleton const* InSkeleton) const 
	{ 
		return InSkeleton->IsCompatible(Skeleton); 
	}

	/** Check whether we should extract root motion */
	bool ShouldExtractRootMotion() const 
	{ 
		return bShouldExtractRootMotion;
	}
	
	/** Save a pose snapshot to the internal snapshot cache */
	void SavePoseSnapshot(USkeletalMeshComponent* InSkeletalMeshComponent, FName SnapshotName);

	/** Get a cached pose snapshot by name */
	const FPoseSnapshot* GetPoseSnapshot(FName SnapshotName) const;

	/** Access various counters */
	const FGraphTraversalCounter& GetInitializationCounter() const { return InitializationCounter; }
	const FGraphTraversalCounter& GetCachedBonesCounter() const  { return CachedBonesCounter; }
	const FGraphTraversalCounter& GetUpdateCounter() const  { return UpdateCounter; }
	const FGraphTraversalCounter& GetEvaluationCounter() const { return EvaluationCounter; }
	const FGraphTraversalCounter& GetSlotNodeInitializationCounter() const { return SlotNodeInitializationCounter; }
	
	void ResetUpdateCounter() { UpdateCounter.Reset(); }

	/** Access root motion params */
	FRootMotionMovementParams& GetExtractedRootMotion() { return ExtractedRootMotion; }

	/** Access UObject base of UAnimInstance */
	UObject* GetAnimInstanceObject() { return AnimInstanceObject; }
	const UObject* GetAnimInstanceObject() const { return AnimInstanceObject; }

	/** Gets an unchecked (can return nullptr) node given an index into the node property array */
	FAnimNode_Base* GetNodeFromIndexUntyped(int32 NodeIdx, UScriptStruct* RequiredStructType);

	/** Gets a checked node given an index into the node property array */
	FAnimNode_Base* GetCheckedNodeFromIndexUntyped(int32 NodeIdx, UScriptStruct* RequiredStructType);

	/** Gets a checked node given an index into the node property array */
	template<class NodeType>
	NodeType* GetCheckedNodeFromIndex(int32 NodeIdx)
	{
		return (NodeType*)GetCheckedNodeFromIndexUntyped(NodeIdx, NodeType::StaticStruct());
	}

	/** Gets an unchecked (can return nullptr) node given an index into the node property array */
	template<class NodeType>
	NodeType* GetNodeFromIndex(int32 NodeIdx)
	{
		return (NodeType*)GetNodeFromIndexUntyped(NodeIdx, NodeType::StaticStruct());
	}

	/** const access to required bones array */
	const FBoneContainer& GetRequiredBones() const 
	{ 
		return RequiredBones; 
	}

	/** access to required bones array */
	FBoneContainer& GetRequiredBones() 
	{ 
		return RequiredBones;
	}

	/** Access to LODLevel */
	int32 GetLODLevel() const
	{
		return LODLevel;
	}

	// Cached SkeletalMeshComponent LocalToWorld Transform.
	const FTransform& GetSkelMeshCompLocalToWorld() const
	{
		return SkelMeshCompLocalToWorld;
	}

	// Cached SkeletalMeshComponent Owner Transform.
	const FTransform& GetSkelMeshCompOwnerTransform() const
	{
		return SkelMeshCompOwnerTransform;
	}

	/** Get the current skeleton we are using. Note that this will return nullptr outside of pre/post update */
	USkeleton* GetSkeleton() 
	{ 
		// Skeleton is only available during update/eval. If you're calling this function outside of it, it will return null. 
		// adding ensure here so that we can catch them earlier
		ensureAlways(Skeleton);
		return Skeleton; 
	}

	/** Get the current skeletal mesh component we are running on. Note that this will return nullptr outside of pre/post update */
	USkeletalMeshComponent* GetSkelMeshComponent() const
	{ 
		// Skeleton is only available during update/eval. If you're calling this function outside of it, it will return null. 
		// adding ensure here so that we can catch them earlier
		ensureAlways(SkeletalMeshComponent);
		return SkeletalMeshComponent; 
	}

	// Creates an uninitialized tick record in the list for the correct group or the ungrouped array.  If the group is valid, OutSyncGroupPtr will point to the group.
	FAnimTickRecord& CreateUninitializedTickRecord(int32 GroupIndex, FAnimGroupInstance*& OutSyncGroupPtr);

	/** Helper function: make a tick record for a sequence */
	void MakeSequenceTickRecord(FAnimTickRecord& TickRecord, UAnimSequenceBase* Sequence, bool bLooping, float PlayRate, float FinalBlendWeight, float& CurrentTime, FMarkerTickRecord& MarkerTickRecord) const;

	/** Helper function: make a tick record for a blend space */
	void MakeBlendSpaceTickRecord(FAnimTickRecord& TickRecord, UBlendSpaceBase* BlendSpace, const FVector& BlendInput, TArray<FBlendSampleData>& BlendSampleDataCache, FBlendFilter& BlendFilter, bool bLooping, float PlayRate, float FinalBlendWeight, float& CurrentTime, FMarkerTickRecord& MarkerTickRecord) const;

	/** Helper function: make a tick record for a pose asset*/
	void MakePoseAssetTickRecord(FAnimTickRecord& TickRecord, class UPoseAsset* PoseAsset, float FinalBlendWeight) const;
	/**
	 * Get Slot Node Weight : this returns new Slot Node Weight, Source Weight, Original TotalNodeWeight
	 *							this 3 values can't be derived from each other
	 *
	 * @param SlotNodeName : the name of the slot node you're querying
	 * @param out_SlotNodeWeight : The node weight for this slot node in the range of [0, 1]
	 * @param out_SourceWeight : The Source weight for this node. 
	 * @param out_TotalNodeWeight : Total weight of this node
	 */
	void GetSlotWeight(const FName& SlotNodeName, float& out_SlotNodeWeight, float& out_SourceWeight, float& out_TotalNodeWeight) const;

	/** Evaluate a pose for a named montage slot */
	void SlotEvaluatePose(const FName& SlotNodeName, const FCompactPose& SourcePose, const FBlendedCurve& SourceCurve, float InSourceWeight, FCompactPose& BlendedPose, FBlendedCurve& BlendedCurve, float InBlendWeight, float InTotalNodeWeight);
	
	// Allow slot nodes to store off their weight during ticking
	void UpdateSlotNodeWeight(const FName& SlotNodeName, float InLocalMontageWeight, float InNodeGlobalWeight);

	/** Register a named slot */
	void RegisterSlotNodeWithAnimInstance(const FName& SlotNodeName);

	/** Check whether we have a valid root node */
	bool HasRootNode() const
	{ 
		return RootNode != nullptr; 
	}

	/** @todo: remove after deprecation */
	FAnimNode_Base* GetRootNode() 
	{ 
		return RootNode;
	}

	/** Gather debug data from this instance proxy and the blend tree for display */
	void GatherDebugData(FNodeDebugData& DebugData);

	/** Gather debug data from this instance proxy and the specified blend tree root for display */
	void GatherDebugData_WithRoot(FNodeDebugData& DebugData, FAnimNode_Base* InRootNode, FName InLayerName);

#if ENABLE_ANIM_DRAW_DEBUG
	TArray<FQueuedDrawDebugItem> QueuedDrawDebugItems;

	void AnimDrawDebugOnScreenMessage(const FString& DebugMessage, const FColor& Color, const FVector2D& TextScale = FVector2D::UnitVector);
	void AnimDrawDebugLine(const FVector& StartLoc, const FVector& EndLoc, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f);
	void AnimDrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f);
	void AnimDrawDebugSphere(const FVector& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f);
	void AnimDrawDebugCoordinateSystem(FVector const& AxisLoc, FRotator const& AxisRot, float Scale = 1.f, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f);
	void AnimDrawDebugPlane(const FTransform& BaseTransform, float Radii, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f);
#else
	void AnimDrawDebugOnScreenMessage(const FString& DebugMessage, const FColor& Color, const FVector2D& TextScale = FVector2D::UnitVector) {}
	void AnimDrawDebugLine(const FVector& StartLoc, const FVector& EndLoc, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f) {}
	void AnimDrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f) {}
	void AnimDrawDebugSphere(const FVector& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f) {}
	void AnimDrawDebugCoordinateSystem(FVector const& AxisLoc, FRotator const& AxisRot, float Scale = 1.f, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f) {}
	void AnimDrawDebugPlane(const FTransform& BaseTransform, float Radii, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, float Thickness = 0.f) {}
#endif // ENABLE_ANIM_DRAW_DEBUG

#if ENABLE_ANIM_LOGGING
	const FString& GetActorName() const 
	{
		return ActorName; 
	}
#endif

	const FString& GetAnimInstanceName() const 
	{ 
		return AnimInstanceName;
	}

	/** Gets the runtime instance of the specified state machine by Name */
	FAnimNode_StateMachine* GetStateMachineInstanceFromName(FName MachineName);

	/** Get the machine description for the specified instance. Does not rely on PRIVATE_MachineDescription being initialized */
	static const FBakedAnimationStateMachine* GetMachineDescription(IAnimClassInterface* AnimBlueprintClass, FAnimNode_StateMachine* MachineInstance);

	/** 
	 * Get the index of the specified instance asset player. Useful to pass to GetInstanceAssetPlayerLength (etc.).
	 * Passing NAME_None to InstanceName will return the first (assumed only) player instance index found.
	 */
	int32 GetInstanceAssetPlayerIndex(FName MachineName, FName StateName, FName InstanceName = NAME_None);

	float GetRecordedMachineWeight(const int32 InMachineClassIndex) const;
	void RecordMachineWeight(const int32 InMachineClassIndex, const float InMachineWeight);

	float GetRecordedStateWeight(const int32 InMachineClassIndex, const int32 InStateIndex) const;
	void RecordStateWeight(const int32 InMachineClassIndex, const int32 InStateIndex, const float InStateWeight, const float InElapsedTime);

	bool IsSlotNodeRelevantForNotifies(const FName& SlotNodeName) const;
	/** Reset any dynamics running simulation-style updates (e.g. on teleport, time skip etc.) */
	void ResetDynamics(ETeleportType InTeleportType);

	/** Returns all Animation Nodes of FAnimNode_AssetPlayerBase class within the specified (named) Animation Graph */
	TArray<FAnimNode_AssetPlayerBase*> GetInstanceAssetPlayers(const FName& GraphName);

	UE_DEPRECATED(4.20, "Please use ResetDynamics with a ETeleportType argument")
	void ResetDynamics();

	/** Get the relative transform of the component we are running on */
	const FTransform& GetComponentRelativeTransform() const { return ComponentRelativeTransform; }

	/** Get the component to world transform of the component we are running on */
	const FTransform& GetComponentTransform() const { return ComponentTransform; }

	/** Get the transform of the actor we are running on */
	const FTransform& GetActorTransform() const { return ActorTransform; }

#if ANIM_TRACE_ENABLED
	// Trace montage debug data for the specified slot
	void TraceMontageEvaluationData(const FAnimationUpdateContext& InContext, const FName& InSlotName);
#endif

	/** Get the debug data for this instance's anim bp */
	FAnimBlueprintDebugData* GetAnimBlueprintDebugData() const;

	/** Only restricted classes can access the protected interface */
	friend class UAnimInstance;
	friend class UAnimSingleNodeInstance;
	friend class USkeletalMeshComponent;
	friend struct FAnimNode_LinkedAnimGraph;
	friend struct FAnimationBaseContext;
	friend struct FAnimTrace;

protected:
	/** Called when our anim instance is being initialized */
	virtual void Initialize(UAnimInstance* InAnimInstance);

	/** Called when our anim instance is being uninitialized */
	virtual void Uninitialize(UAnimInstance* InAnimInstance);

	/** Called before update so we can copy any data we need */
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds);

	/** Called during PreUpdate, if SkelMesh LOD has changed since last update */
	void OnPreUpdateLODChanged(const int32 PreviousLODIndex, const int32 NewLODIndex);

	/** Update override point */
	virtual void Update(float DeltaSeconds) {}

	UE_DEPRECATED(4.24, "Please use the overload that takes an FAnimationUpdateContext")
	virtual void UpdateAnimationNode(float DeltaSeconds)
	{
		FAnimationUpdateContext Context(this, DeltaSeconds);
		UpdateAnimationNode(Context);
	}

	/** Updates the anim graph */
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext);

	UE_DEPRECATED(4.24, "Please use the overload that takes an FAnimationUpdateContext")
	virtual void UpdateAnimationNode_WithRoot(float DeltaSeconds, FAnimNode_Base* InRootNode, FName InLayerName) 
	{
		FAnimationUpdateContext Context(this, DeltaSeconds);
		UpdateAnimationNode_WithRoot(Context, InRootNode, InLayerName);
	}

	/** Updates the anim graph using a specified root node */
	virtual void UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InLayerName);

	/** Called on the game thread pre-evaluate. */
	virtual void PreEvaluateAnimation(UAnimInstance* InAnimInstance);

	/** Called when the anim instance is being initialized. If we are not using a blueprint instance, this root node can be provided*/
	virtual FAnimNode_Base* GetCustomRootNode()
	{
		return nullptr;
	}

	/** Called when the anim instance is being initialized. If we are not using a blueprint instance, these nodes can be provided */
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
	{
	}
	
	/** 
	 * Cache bones override point. You should call CacheBones on any nodes that need it here.
	 * bBoneCachesInvalidated is used to only perform this when needed (e.g. when a LOD changes), 
	 * as it is usually an expensive operation.
	 */
	virtual void CacheBones();

	/** 
	 * Cache bones override point. You should call CacheBones on any nodes that need it here.
	 * bBoneCachesInvalidated is used to only perform this when needed (e.g. when a LOD changes), 
	 * as it is usually an expensive operation.
	 */
	virtual void CacheBones_WithRoot(FAnimNode_Base* InRootNode);

	/** 
	 * Evaluate override point 
	 * @return true if this function is implemented, false otherwise.
	 * Note: the node graph will not be evaluated if this function returns true
	 */
	virtual bool Evaluate(FPoseContext& Output) { return false; }

	/** 
	 * Evaluate override point with root node override.
	 * @return true if this function is implemented, false otherwise.
	 * Note: the node graph will not be evaluated if this function returns true
	 */
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) { return Evaluate(Output); }

	/** Called after update so we can copy any data we need */
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const;

	/** Called after evaluate so we can do any game thread work we need to */
	virtual void PostEvaluate(UAnimInstance* InAnimInstance);

	/** Copy any UObjects we might be using. Called Pre-update and pre-evaluate. */
	virtual void InitializeObjects(UAnimInstance* InAnimInstance);

	/** 
	 * Clear any UObjects we might be using. Called at the end of the post-evaluate phase.
	 * This is to ensure that objects are not used by anything apart from animation nodes.
	 * Please make sure to call the base implementation if this is overridden.
	 */
	virtual void ClearObjects();

	/** Calls Update(), updates the anim graph, ticks asset players */
	void UpdateAnimation();

	UE_DEPRECATED(4.24, "Please use the overload that takes an FAnimationUpdateContext")
	void UpdateAnimation_WithRoot(FAnimNode_Base* InRootNode, FName InLayerName)
	{
		FAnimationUpdateContext Context(this, CurrentDeltaSeconds);
		UpdateAnimation_WithRoot(Context, InRootNode, InLayerName);
	}

	/** Calls Update(), updates the anim graph from the specified root, ticks asset players */
	void UpdateAnimation_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InLayerName);

	/** Evaluates the anim graph if Evaluate() returns false */
	void EvaluateAnimation(FPoseContext& Output);

	/** Evaluates the anim graph given the specified root if Evaluate() returns false */
	void EvaluateAnimation_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode);

	/** Evaluates the anim graph */
	void EvaluateAnimationNode(FPoseContext& Output);

	/** Evaluates the anim graph given the specified root */
	void EvaluateAnimationNode_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode);

	// @todo document
	void SequenceAdvanceImmediate(UAnimSequenceBase* Sequence, bool bLooping, float PlayRate, float DeltaSeconds, /*inout*/ float& CurrentTime, FMarkerTickRecord& MarkerTickRecord);

	// @todo document
	void BlendSpaceAdvanceImmediate(UBlendSpaceBase* BlendSpace, const FVector& BlendInput, TArray<FBlendSampleData> & BlendSampleDataCache, FBlendFilter & BlendFilter, bool bLooping, float PlayRate, float DeltaSeconds, /*inout*/ float& CurrentTime, FMarkerTickRecord& MarkerTickRecord);

	// Gets the sync group we should be reading from
	int32 GetSyncGroupReadIndex() const 
	{ 
		return 1 - SyncGroupWriteIndex; 
	}

	// Gets the sync group we should be writing to
	int32 GetSyncGroupWriteIndex() const 
	{ 
		return SyncGroupWriteIndex; 
	}

	/** Add anim notifier **/
	void AddAnimNotifies(const TArray<FAnimNotifyEventReference>& NewNotifies, const float InstanceWeight);

	/** Returns the baked sync group index from the compile step */
	int32 GetSyncGroupIndexFromName(FName SyncGroupName) const;

	bool GetTimeToClosestMarker(FName SyncGroup, FName MarkerName, float& OutMarkerTime) const;

	bool HasMarkerBeenHitThisFrame(FName SyncGroup, FName MarkerName) const;

	bool IsSyncGroupBetweenMarkers(FName InSyncGroupName, FName PreviousMarker, FName NextMarker, bool bRespectMarkerOrder = true) const;

	FMarkerSyncAnimPosition GetSyncGroupPosition(FName InSyncGroupName) const;

	// slot node run-time functions
	void ReinitializeSlotNodes();

	// if it doesn't tick, it will keep old weight, so we'll have to clear it in the beginning of tick
	void ClearSlotNodeWeights();

	/** Get global weight in AnimGraph for this slot node. 
	 * Note: this is the weight of the node, not the weight of any potential montage it is playing. */
	float GetSlotNodeGlobalWeight(const FName& SlotNodeName) const;

	/** Get Global weight of any montages this slot node is playing. 
	 * If this slot is not currently playing a montage, it will return 0. */
	float GetSlotMontageGlobalWeight(const FName& SlotNodeName) const;

	/** Get local weight of any montages this slot node is playing.
	* If this slot is not currently playing a montage, it will return 0. 
	* This is double buffered, will return last frame data if called from Update or Evaluate. */
	float GetSlotMontageLocalWeight(const FName& SlotNodeName) const;

	/** Get local weight of any montages this slot is playing.
	* If this slot is not current playing a montage, it will return 0.
	* This will return up to date data if called during Update or Evaluate. */
	float CalcSlotMontageLocalWeight(const FName& SlotNodeName) const;

	/** 
	 * Recalculate Required Bones [RequiredBones]
	 * Is called when bRequiredBonesUpToDate = false
	 */
	void RecalcRequiredBones(USkeletalMeshComponent* Component, UObject* Asset);

	/** 
	 * Recalculate required curve list for animation - if you call RecalcRequiredBones, this should already happen
	 */
	void RecalcRequiredCurves(const FCurveEvaluationOption& CurveEvalOption);

	/** Update the material parameters of the supplied component from this instance */
	void UpdateCurvesToComponents(USkeletalMeshComponent* Component);

	/** Get Currently active montage evaluation state.
		Note that there might be multiple Active at the same time. This will only return the first active one it finds. **/
	const FMontageEvaluationState* GetActiveMontageEvaluationState() const;

	/** Access montage array data */
	TArray<FMontageEvaluationState>& GetMontageEvaluationData() { return MontageEvaluationData; }

	/** Access montage array data */
	const TArray<FMontageEvaluationState>& GetMontageEvaluationData() const { return MontageEvaluationData; }

	/** Check whether we have active morph target curves */
	/** Gets the most relevant asset player in a specified state */
	FAnimNode_AssetPlayerBase* GetRelevantAssetPlayerFromState(int32 MachineIndex, int32 StateIndex);

	/** Gets the runtime instance of the specified state machine */
	FAnimNode_StateMachine* GetStateMachineInstance(int32 MachineIndex);

	/** Gets an unchecked (can return nullptr) node given a property of the anim instance */
	template<class NodeType>
	NodeType* GetNodeFromProperty(FProperty* Property)
	{
		return (NodeType*)Property->ContainerPtrToValuePtr<NodeType>(AnimInstanceObject);
	}

	/** Gets the length in seconds of the asset referenced in an asset player node */
	float GetInstanceAssetPlayerLength(int32 AssetPlayerIndex);

	/** Get the current accumulated time in seconds for an asset player node */
	float GetInstanceAssetPlayerTime(int32 AssetPlayerIndex);

	/** Get the current accumulated time as a fraction for an asset player node */
	float GetInstanceAssetPlayerTimeFraction(int32 AssetPlayerIndex);

	/** Get the time in seconds from the end of an animation in an asset player node */
	float GetInstanceAssetPlayerTimeFromEnd(int32 AssetPlayerIndex);

	/** Get the time as a fraction of the asset length of an animation in an asset player node */
	float GetInstanceAssetPlayerTimeFromEndFraction(int32 AssetPlayerIndex);

	/** Get the blend weight of a specified state */
	float GetInstanceMachineWeight(int32 MachineIndex);

	/** Get the blend weight of a specified state */
	float GetInstanceStateWeight(int32 MachineIndex, int32 StateIndex);

	/** Get the current elapsed time of a state within the specified state machine */
	float GetInstanceCurrentStateElapsedTime(int32 MachineIndex);

	/** Get the crossfade duration of a specified transition */
	float GetInstanceTransitionCrossfadeDuration(int32 MachineIndex, int32 TransitionIndex);

	/** Get the elapsed time in seconds of a specified transition */
	float GetInstanceTransitionTimeElapsed(int32 MachineIndex, int32 TransitionIndex);

	/** Get the elapsed time as a fraction of the crossfade duration of a specified transition */
	float GetInstanceTransitionTimeElapsedFraction(int32 MachineIndex, int32 TransitionIndex);

	/** Get the time remaining in seconds for the most relevant animation in the source state */
	float GetRelevantAnimTimeRemaining(int32 MachineIndex, int32 StateIndex);

	/** Get the time remaining as a fraction of the duration for the most relevant animation in the source state */
	float GetRelevantAnimTimeRemainingFraction(int32 MachineIndex, int32 StateIndex);

	/** Get the length in seconds of the most relevant animation in the source state */
	float GetRelevantAnimLength(int32 MachineIndex, int32 StateIndex);

	/** Get the current accumulated time in seconds for the most relevant animation in the source state */
	float GetRelevantAnimTime(int32 MachineIndex, int32 StateIndex);

	/** Get the current accumulated time as a fraction of the length of the most relevant animation in the source state */
	float GetRelevantAnimTimeFraction(int32 MachineIndex, int32 StateIndex);

	// Sets up a native transition delegate between states with PrevStateName and NextStateName, in the state machine with name MachineName.
	// Note that a transition already has to exist for this to succeed
	void AddNativeTransitionBinding(const FName& MachineName, const FName& PrevStateName, const FName& NextStateName, const FCanTakeTransition& NativeTransitionDelegate, const FName& TransitionName = NAME_None);

	// Check for whether a native rule is bound to the specified transition
	bool HasNativeTransitionBinding(const FName& MachineName, const FName& PrevStateName, const FName& NextStateName, FName& OutBindingName);

	// Sets up a native state entry delegate from state with StateName, in the state machine with name MachineName.
	void AddNativeStateEntryBinding(const FName& MachineName, const FName& StateName, const FOnGraphStateChanged& NativeEnteredDelegate, const FName& BindingName = NAME_None);
	
	// Check for whether a native entry delegate is bound to the specified state
	bool HasNativeStateEntryBinding(const FName& MachineName, const FName& StateName, FName& OutBindingName);

	// Sets up a native state exit delegate from state with StateName, in the state machine with name MachineName.
	void AddNativeStateExitBinding(const FName& MachineName, const FName& StateName, const FOnGraphStateChanged& NativeExitedDelegate, const FName& BindingName = NAME_None);

	// Check for whether a native exit delegate is bound to the specified state
	bool HasNativeStateExitBinding(const FName& MachineName, const FName& StateName, FName& OutBindingName);

	/** Bind any native delegates that we have set up */
	void BindNativeDelegates();

	/** Gets the runtime instance desc of the state machine specified by name */
	const FBakedAnimationStateMachine* GetStateMachineInstanceDesc(FName MachineName);

	/** Gets the index of the state machine matching MachineName */
	int32 GetStateMachineIndex(FName MachineName);

	void GetStateMachineIndexAndDescription(FName InMachineName, int32& OutMachineIndex, const FBakedAnimationStateMachine** OutMachineDescription);

	/** Initialize the root node - split into a separate function for backwards compatibility (initialization order) reasons */
	void InitializeRootNode(bool bInDeferRootNodeInitialization = false);

	/** Initialize the specified root node */
	void InitializeRootNode_WithRoot(FAnimNode_Base* InRootNode);

	/** Manually add object references to GC */
	virtual void AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector);

	/** Allow nodes to register log messages to be processed on the game thread */
	void LogMessage(FName InLogType, EMessageSeverity::Type InSeverity, const FText& InMessage) const;

	/** Get the current value of all animation curves **/
	TMap<FName, float>& GetAnimationCurves(EAnimCurveType InCurveType) { return AnimationCurves[(uint8)InCurveType]; }
	const TMap<FName, float>& GetAnimationCurves(EAnimCurveType InCurveType) const { return AnimationCurves[(uint8)InCurveType]; }

	/** Reset Animation Curves */
	void ResetAnimationCurves();

	/** Pushes blended heap curve to output curves in the proxy using required bones cached data */
	void UpdateCurvesToEvaluationContext(const FAnimationEvaluationContext& InContext);

	/** Update curves once evaluation has taken place. Mostly pushes curves to materials/morphs */
	void UpdateCurvesPostEvaluation(USkeletalMeshComponent* SkelMeshComp);

	/** Check whether we have any active curves */
	bool HasActiveCurves() const;

	/** Add a curve value */
	void AddCurveValue(const FSmartNameMapping& Mapping, const FName& CurveName, float Value);

	/** Custom proxy Init/Cache/Update/Evaluate functions */
	static void InitializeInputProxy(FAnimInstanceProxy* InputProxy, UAnimInstance* InAnimInstance);
	static void GatherInputProxyDebugData(FAnimInstanceProxy* InputProxy, FNodeDebugData& DebugData);
	static void CacheBonesInputProxy(FAnimInstanceProxy* InputProxy);
	static void UpdateInputProxy(FAnimInstanceProxy* InputProxy, const FAnimationUpdateContext& Context);
	static void EvaluateInputProxy(FAnimInstanceProxy* InputProxy, FPoseContext& Output);

private:
	/** The component to world transform of the component we are running on */
	FTransform ComponentTransform;

	/** The relative transform of the component we are running on */
	FTransform ComponentRelativeTransform;

	/** The transform of the actor we are running on */
	FTransform ActorTransform;

	/** Object ptr to our UAnimInstance */
	mutable UObject* AnimInstanceObject;

	/** Our anim blueprint generated class */
	IAnimClassInterface* AnimClassInterface;

	/** Skeleton we are using, only used for comparison purposes. Note that this will be nullptr outside of pre/post update */
	USkeleton* Skeleton;

	/** Skeletal mesh component we are attached to. Note that this will be nullptr outside of pre/post update */
	USkeletalMeshComponent* SkeletalMeshComponent;

	/** The last time passed into PreUpdate() */
	float CurrentDeltaSeconds;

	/** The last dime dilation (gleaned from world settings) */
	float CurrentTimeDilation;

#if WITH_EDITORONLY_DATA
	/** Array of visited nodes this frame */
	TArray<FAnimBlueprintDebugData::FNodeVisit> UpdatedNodesThisFrame;

	/** Array of nodes to watch this frame */
	TArray<FAnimNodePoseWatch> PoseWatchEntriesForThisFrame;
#endif

#if ENABLE_ANIM_LOGGING
	/** Actor name for debug logging purposes */
	FString ActorName;
#endif

	/** Anim instance name for debug purposes */
	FString AnimInstanceName;

	/** Anim graph */
	FAnimNode_Base* RootNode;

	/** Default linked instance input node if available */
	FAnimNode_LinkedInputPose* DefaultLinkedInstanceInputNode;

	/** Map of layer name to saved pose nodes to process after the graph has been updated */
	TMap<FName, TArray<FAnimNode_SaveCachedPose*>> SavedPoseQueueMap;

	/** The list of animation assets which are going to be evaluated this frame and need to be ticked (ungrouped) */
	TArray<FAnimTickRecord> UngroupedActivePlayerArrays[2];

	/** The set of tick groups for this anim instance */
	TArray<FAnimGroupInstance> SyncGroupArrays[2];

	/** Buffers containing read/write buffers for all current machine weights */
	TArray<float> MachineWeightArrays[2];

	/** Buffers containing read/write buffers for all current state weights */
	TArray<float> StateWeightArrays[2];

	/** Map that transforms state class indices to base offsets into the weight array */
	TMap<int32, int32> StateMachineClassIndexToWeightOffset;

	// Current sync group buffer index
	int32 SyncGroupWriteIndex;

	/** Animation Notifies that has been triggered in the latest tick **/
	FAnimNotifyQueue NotifyQueue;

	// Root motion mode duplicated from the anim instance
	ERootMotionMode::Type RootMotionMode;

	// Read/write buffers Tracker map for slot name->weights/relevancy
	TMap<FName, int32> SlotNameToTrackerIndex;
	TArray<FMontageActiveSlotTracker> SlotWeightTracker[2];

	/** Curves in an easily looked-up form **/
	TMap<FName, float> AnimationCurves[(uint8)EAnimCurveType::MaxAnimCurveType];

	/** Material parameters that we had been changing and now need to clear */
	TArray<FName> MaterialParametersToClear;

protected:
	// Counters for synchronization
	FGraphTraversalCounter InitializationCounter;
	FGraphTraversalCounter CachedBonesCounter;
	FGraphTraversalCounter UpdateCounter;
	FGraphTraversalCounter EvaluationCounter;
	FGraphTraversalCounter SlotNodeInitializationCounter;

	// Sync counter
	uint64 FrameCounterForUpdate;
	uint64 FrameCounterForNodeUpdate;

private:
	// Root motion extracted from animation since the last time ConsumeExtractedRootMotion was called
	FRootMotionMovementParams ExtractedRootMotion;

	/** Temporary array of bone indices required this frame. Should be subset of Skeleton and Mesh's RequiredBones */
	FBoneContainer RequiredBones;

	/** LODLevel used by RequiredBones */
	int32 LODLevel = 0;

	/** Counter used to control CacheBones recursion behavior - makes sure we cache bones correctly when recursing into different subgraphs */
	int32 CacheBonesRecursionCounter;

	/** Cached SkeletalMeshComponent LocalToWorld transform. */
	FTransform SkelMeshCompLocalToWorld;

	/** Cached SkeletalMeshComponent Owner Transform */
	FTransform SkelMeshCompOwnerTransform;

	/** During animation update and eval, records the number of frames we will skip due to URO */
	int16 NumUroSkippedFrames_Update;
	int16 NumUroSkippedFrames_Eval;

private:
	/** Copy of UAnimInstance::MontageInstances data used for update & evaluation */
	TArray<FMontageEvaluationState> MontageEvaluationData;

	/** Delegate fired on the game thread before update occurs */
	TArray<FAnimNode_Base*> GameThreadPreUpdateNodes;

	/** When GameThreadPreUpdateNodes are disabled due to LOD, they are stored here. To be potentially restored later. */
	TArray<FAnimNode_Base*> LODDisabledGameThreadPreUpdateNodes;

	/** All nodes that need to be reset on DynamicReset() */
	TArray<FAnimNode_Base*> DynamicResetNodes;
	
	/** Native transition rules */
	TArray<FNativeTransitionBinding> NativeTransitionBindings;

	/** Native state entry bindings */
	TArray<FNativeStateBinding> NativeStateEntryBindings;

	/** Native state exit bindings */
	TArray<FNativeStateBinding> NativeStateExitBindings;

	/** Array of snapshots. Each entry contains a name for finding specific pose snapshots */
	TArray<FPoseSnapshot> PoseSnapshots;

#if ENABLE_ANIM_LOGGING
	/** Logged message queues. Allows nodes to report messages to MessageLog even though they may be running
	 *  on a worked thread
	 */
	typedef TPair<EMessageSeverity::Type, FText> FLogMessageEntry;
	mutable TMap<FName, TArray<FLogMessageEntry>> LoggedMessagesMap;

	/** Cache of guids generated from previously sent messages so we can stop spam*/
	mutable TArray<FGuid> PreviouslyLoggedMessages;
#endif

	/** Scope guard to prevent duplicate work on re-entracy */
	bool bUpdatingRoot;

protected:

	/** When RequiredBones mapping has changed, AnimNodes need to update their bones caches. */
	uint8 bBoneCachesInvalidated : 1;

private:

	// Diplicate of bool result of ShouldExtractRootMotion()
	uint8 bShouldExtractRootMotion : 1;

	/** We can defer initialization until first update */
	uint8 bDeferRootNodeInitialization : 1;

#if WITH_EDITORONLY_DATA
	/** Whether this UAnimInstance is currently being debugged in the editor */
	uint8 bIsBeingDebugged : 1;
#endif
};
