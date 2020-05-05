// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectKey.h"
#include "UObject/Class.h"
#include "Engine/EngineTypes.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "BonePose.h"
#include "Logging/TokenizedMessage.h"
#include "Stats/StatsHierarchical.h"
#include "Animation/AnimTrace.h"
#include "UObject/FieldPath.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

#include "AnimNodeBase.generated.h"

#define DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Method) \
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#define ANIM_NODE_IDS_AVAILABLE	(ANIM_TRACE_ENABLED || WITH_EDITORONLY_DATA)

class IAnimClassInterface;
class UAnimBlueprint;
class UAnimInstance;
struct FAnimInstanceProxy;
struct FAnimNode_Base;
class UProperty;

/**
 * Utility container for tracking a stack of ancestor nodes by node type during graph traversal
 * This is not an exhaustive list of all visited ancestors. During Update nodes must call
 * FAnimationUpdateContext::TrackAncestor() to appear in the tracker.
 */
struct FAnimNodeTracker
{
	using FKey = FObjectKey;
	using FNodeStack = TArray<FAnimNode_Base*, TInlineAllocator<4>>;
	using FMap = TMap<FKey, FNodeStack, TInlineSetAllocator<4>>;

	FMap Map;

	template<typename NodeType>
	static FKey GetKey()
	{
		return FKey(NodeType::StaticStruct());
	}

	template<typename NodeType>
	FKey Push(NodeType* Node)
	{
		FKey Key(GetKey<NodeType>());
		FNodeStack& Stack = Map.FindOrAdd(Key);
		Stack.Push(Node);
		return Key;
	}

	template<typename NodeType>
	NodeType* Pop()
	{
		FNodeStack* Stack = Map.Find(GetKey<NodeType>());
		return Stack ? static_cast<NodeType*>(Stack->Pop()) : nullptr;
	}

	FAnimNode_Base* Pop(FKey Key)
	{
		FNodeStack* Stack = Map.Find(Key);
		return Stack ? Stack->Pop() : nullptr;
	}

	template<typename NodeType>
	NodeType* Top() const
	{
		const FNodeStack* Stack = Map.Find(GetKey<NodeType>());
		return (Stack && Stack->Num() != 0) ? static_cast<NodeType*>(Stack->Top()) : nullptr;
	}

	void CopyTopsOnly(const FAnimNodeTracker& Source)
	{
		Map.Reset();
		Map.Reserve(Source.Map.Num());
		for (const auto& Iter : Source.Map)
		{
			if (Iter.Value.Num() != 0)
			{
				FNodeStack& Stack = Map.Add(Iter.Key);
				Stack.Push(Iter.Value.Top());
			}
		}
	}
};


/** Helper RAII object to cleanup a node added to the node tracker */
class FScopedAnimNodeTracker
{
public:
	FScopedAnimNodeTracker() = default;

	FScopedAnimNodeTracker(FAnimNodeTracker* InTracker, FAnimNodeTracker::FKey InKey)
		: Tracker(InTracker)
		, TrackedKey(InKey)
	{}

	~FScopedAnimNodeTracker()
	{
		if (Tracker && TrackedKey != FAnimNodeTracker::FKey())
		{
			Tracker->Pop(TrackedKey);
		}
	}

private:
	FAnimNodeTracker* Tracker = nullptr;
	FAnimNodeTracker::FKey TrackedKey;
};


/** Persistent state shared during animation tree update  */
struct FAnimationUpdateSharedContext
{
	FAnimNodeTracker AncestorTracker;

	void CopyForCachedUpdate(const FAnimationUpdateSharedContext& Source)
	{
		AncestorTracker.CopyTopsOnly(Source.AncestorTracker);
	}
};

/** Base class for update/evaluate contexts */
struct FAnimationBaseContext
{
public:
	FAnimInstanceProxy* AnimInstanceProxy;

	FAnimationBaseContext();

protected:
	// DEPRECATED - Please use constructor that uses an FAnimInstanceProxy*
	ENGINE_API FAnimationBaseContext(UAnimInstance* InAnimInstance);

	ENGINE_API FAnimationBaseContext(FAnimInstanceProxy* InAnimInstanceProxy);

public:
	// we define a copy constructor here simply to avoid deprecation warnings with clang
	ENGINE_API FAnimationBaseContext(const FAnimationBaseContext& InContext);

public:
	// Get the Blueprint IAnimClassInterface associated with this context, if there is one.
	// Note: This can return NULL, so check the result.
	ENGINE_API IAnimClassInterface* GetAnimClass() const;

#if WITH_EDITORONLY_DATA
	// Get the AnimBlueprint associated with this context, if there is one.
	// Note: This can return NULL, so check the result.
	ENGINE_API UAnimBlueprint* GetAnimBlueprint() const;
#endif //WITH_EDITORONLY_DATA

#if ANIM_NODE_IDS_AVAILABLE
	// Get the current node Id, set when we recurse into graph traversal functions from pose links
	ENGINE_API int32 GetCurrentNodeId() const { return CurrentNodeId; }

	// Get the previous node Id, set when we recurse into graph traversal functions from pose links
	ENGINE_API int32 GetPreviousNodeId() const { return PreviousNodeId; }

protected:
	// The current node ID, set when we recurse into graph traversal functions from pose links
	int32 CurrentNodeId;

	// The previous node ID, set when we recurse into graph traversal functions from pose links
	int32 PreviousNodeId;
#endif

protected:

	/** Interface for node contexts to register log messages with the proxy */
	ENGINE_API void LogMessageInternal(FName InLogType, EMessageSeverity::Type InSeverity, FText InMessage) const;
};


/** Initialization context passed around during animation tree initialization */
struct FAnimationInitializeContext : public FAnimationBaseContext
{
public:
	FAnimationInitializeContext(FAnimInstanceProxy* InAnimInstanceProxy)
		: FAnimationBaseContext(InAnimInstanceProxy)
	{
	}
};

/**
 * Context passed around when RequiredBones array changed and cached bones indices have to be refreshed.
 * (RequiredBones array changed because of an LOD switch for example)
 */
struct FAnimationCacheBonesContext : public FAnimationBaseContext
{
public:
	FAnimationCacheBonesContext(FAnimInstanceProxy* InAnimInstanceProxy)
		: FAnimationBaseContext(InAnimInstanceProxy)
	{
	}
};

/** Update context passed around during animation tree update */
struct FAnimationUpdateContext : public FAnimationBaseContext
{
private:
	FAnimationUpdateSharedContext* SharedContext;

	float CurrentWeight;
	float RootMotionWeightModifier;

	float DeltaTime;

public:
	FAnimationUpdateContext(FAnimInstanceProxy* InAnimInstanceProxy = nullptr)
		: FAnimationBaseContext(InAnimInstanceProxy)
		, SharedContext(nullptr)
		, CurrentWeight(1.0f)
		, RootMotionWeightModifier(1.0f)
		, DeltaTime(0.0f)
	{
	}

	FAnimationUpdateContext(FAnimInstanceProxy* InAnimInstanceProxy, float InDeltaTime, FAnimationUpdateSharedContext* InSharedContext = nullptr)
		: FAnimationUpdateContext(InAnimInstanceProxy)
	{
		SharedContext = InSharedContext;
		DeltaTime = InDeltaTime;
	}


	FAnimationUpdateContext(const FAnimationUpdateContext& Copy) = default;

	FAnimationUpdateContext(const FAnimationUpdateContext& Copy, FAnimInstanceProxy* InAnimInstanceProxy)
		: FAnimationBaseContext(InAnimInstanceProxy)
		, SharedContext(Copy.SharedContext)
		, CurrentWeight(Copy.CurrentWeight)
		, RootMotionWeightModifier(Copy.RootMotionWeightModifier)
		, DeltaTime(Copy.DeltaTime)
	{
#if ANIM_TRACE_ENABLED
		CurrentNodeId = Copy.CurrentNodeId;
		PreviousNodeId = Copy.PreviousNodeId;
#endif
	}

public:
	FAnimationUpdateContext WithOtherProxy(FAnimInstanceProxy* InAnimInstanceProxy) const
	{
		return FAnimationUpdateContext(*this, InAnimInstanceProxy);
	}

	FAnimationUpdateContext WithOtherSharedContext(FAnimationUpdateSharedContext* InSharedContext) const
	{
		FAnimationUpdateContext Result(*this);
		Result.SharedContext = InSharedContext;

#if ANIM_TRACE_ENABLED
		// This is currently only used in the case of cached poses, where we dont want to preserve the previous node, so clear it here
		Result.PreviousNodeId = INDEX_NONE;
#endif

		return Result;
	}

	FAnimationUpdateContext FractionalWeight(float WeightMultiplier) const
	{
		FAnimationUpdateContext Result(*this);
		Result.CurrentWeight = CurrentWeight * WeightMultiplier;

		return Result;
	}

	FAnimationUpdateContext FractionalWeightAndRootMotion(float WeightMultiplier, float RootMotionMultiplier) const
	{
		FAnimationUpdateContext Result(*this);
		Result.CurrentWeight = CurrentWeight * WeightMultiplier;
		Result.RootMotionWeightModifier = RootMotionWeightModifier * RootMotionMultiplier;

		return Result;
	}

	FAnimationUpdateContext FractionalWeightAndTime(float WeightMultiplier, float TimeMultiplier) const
	{
		FAnimationUpdateContext Result(*this);
		Result.DeltaTime = DeltaTime * TimeMultiplier;
		Result.CurrentWeight = CurrentWeight * WeightMultiplier;
		return Result;
	}

	FAnimationUpdateContext FractionalWeightTimeAndRootMotion(float WeightMultiplier, float TimeMultiplier, float RootMotionMultiplier) const
	{
		FAnimationUpdateContext Result(*this);
		Result.DeltaTime = DeltaTime * TimeMultiplier;
		Result.CurrentWeight = CurrentWeight * WeightMultiplier;
		Result.RootMotionWeightModifier = RootMotionWeightModifier * RootMotionMultiplier;

		return Result;
	}

#if ANIM_NODE_IDS_AVAILABLE
	FAnimationUpdateContext WithNodeId(int32 InNodeId) const
	{ 
		FAnimationUpdateContext Result(*this);
		Result.PreviousNodeId = CurrentNodeId;
		Result.CurrentNodeId = InNodeId;
		return Result; 
	}
#endif

	// Add a node to the list of tracked ancestors
	template<typename NodeType>
	FScopedAnimNodeTracker TrackAncestor(NodeType* Node) const
	{
		if (ensure(SharedContext != nullptr))
		{
			FAnimNodeTracker::FKey Key = SharedContext->AncestorTracker.Push<NodeType>(Node);
			return FScopedAnimNodeTracker(&SharedContext->AncestorTracker, Key);
		}

		return FScopedAnimNodeTracker();
	}

	// Returns the nearest ancestor node of a particular type
	template<typename NodeType>
	NodeType* GetAncestor() const
	{
		if (ensure(SharedContext != nullptr))
		{
			FAnimNode_Base* Node = SharedContext->AncestorTracker.Top<NodeType>();
			return static_cast<NodeType*>(Node);
		}
		
		return nullptr;
	}

	// Returns persistent state that is tracked through animation tree update
	FAnimationUpdateSharedContext* GetSharedContext() const
	{
		return SharedContext;
	}

	// Returns the final blend weight contribution for this stage
	float GetFinalBlendWeight() const { return CurrentWeight; }

	// Returns the weight modifier for root motion (as root motion weight wont always match blend weight)
	float GetRootMotionWeightModifier() const { return RootMotionWeightModifier; }

	// Returns the delta time for this update, in seconds
	float GetDeltaTime() const { return DeltaTime; }

	// Log update message
	void LogMessage(EMessageSeverity::Type InSeverity, FText InMessage) const { LogMessageInternal("Update", InSeverity, InMessage); }
};


/** Evaluation context passed around during animation tree evaluation */
struct FPoseContext : public FAnimationBaseContext
{
public:
	/* These Pose/Curve is stack allocator. You should not use it outside of stack. */
	FCompactPose	Pose;
	FBlendedCurve	Curve;

public:
	// This constructor allocates a new uninitialized pose for the specified anim instance
	FPoseContext(FAnimInstanceProxy* InAnimInstanceProxy, bool bInExpectsAdditivePose = false)
		: FAnimationBaseContext(InAnimInstanceProxy)
		, bExpectsAdditivePose(bInExpectsAdditivePose)
	{
		Initialize(InAnimInstanceProxy);
	}

	// This constructor allocates a new uninitialized pose, copying non-pose state from the source context
	FPoseContext(const FPoseContext& SourceContext, bool bInOverrideExpectsAdditivePose = false)
		: FAnimationBaseContext(SourceContext.AnimInstanceProxy)
		, bExpectsAdditivePose(SourceContext.bExpectsAdditivePose || bInOverrideExpectsAdditivePose)
	{
		Initialize(SourceContext.AnimInstanceProxy);

#if ANIM_NODE_IDS_AVAILABLE
		CurrentNodeId = SourceContext.CurrentNodeId;
		PreviousNodeId = SourceContext.PreviousNodeId;
#endif
	}

#if ANIM_NODE_IDS_AVAILABLE
	void SetNodeId(int32 InNodeId)
	{ 
		PreviousNodeId = CurrentNodeId;
		CurrentNodeId = InNodeId;
	}

	void SetNodeIds(const FAnimationBaseContext& InContext)
	{ 
		CurrentNodeId = InContext.GetCurrentNodeId();
		PreviousNodeId = InContext.GetPreviousNodeId();
	}
#endif

	ENGINE_API void Initialize(FAnimInstanceProxy* InAnimInstanceProxy);

	// Log evaluation message
	void LogMessage(EMessageSeverity::Type InSeverity, FText InMessage) const { LogMessageInternal("Evaluate", InSeverity, InMessage); }

	void ResetToRefPose()
	{
		if (bExpectsAdditivePose)
		{
			Pose.ResetToAdditiveIdentity();
		}
		else
		{
			Pose.ResetToRefPose();
		}
	}

	void ResetToAdditiveIdentity()
	{
		Pose.ResetToAdditiveIdentity();
	}

	bool ContainsNaN() const
	{
		return Pose.ContainsNaN();
	}

	bool IsNormalized() const
	{
		return Pose.IsNormalized();
	}

	FPoseContext& operator=(const FPoseContext& Other)
	{
		if (AnimInstanceProxy != Other.AnimInstanceProxy)
		{
			Initialize(AnimInstanceProxy);
		}

		Pose = Other.Pose;
		Curve = Other.Curve;
		bExpectsAdditivePose = Other.bExpectsAdditivePose;
		return *this;
	}

	// Is this pose expected to be additive
	bool ExpectsAdditivePose() const { return bExpectsAdditivePose; }

private:

	// Is this pose expected to be an additive pose
	bool bExpectsAdditivePose;
};


/** Evaluation context passed around during animation tree evaluation */
struct FComponentSpacePoseContext : public FAnimationBaseContext
{
public:
	FCSPose<FCompactPose>	Pose;
	FBlendedCurve			Curve;

public:
	// This constructor allocates a new uninitialized pose for the specified anim instance
	FComponentSpacePoseContext(FAnimInstanceProxy* InAnimInstanceProxy)
		: FAnimationBaseContext(InAnimInstanceProxy)
	{
		// No need to initialize, done through FA2CSPose::AllocateLocalPoses
	}

	// This constructor allocates a new uninitialized pose, copying non-pose state from the source context
	FComponentSpacePoseContext(const FComponentSpacePoseContext& SourceContext)
		: FAnimationBaseContext(SourceContext.AnimInstanceProxy)
	{
		// No need to initialize, done through FA2CSPose::AllocateLocalPoses

#if ANIM_NODE_IDS_AVAILABLE
		CurrentNodeId = SourceContext.CurrentNodeId;
		PreviousNodeId = SourceContext.PreviousNodeId;
#endif
	}

#if ANIM_NODE_IDS_AVAILABLE
	void SetNodeId(int32 InNodeId)
	{ 
		PreviousNodeId = CurrentNodeId;
		CurrentNodeId = InNodeId;
	}

	void SetNodeIds(const FAnimationBaseContext& InContext)
	{ 
		CurrentNodeId = InContext.GetCurrentNodeId();
		PreviousNodeId = InContext.GetPreviousNodeId();
	}
#endif

	ENGINE_API void ResetToRefPose();

	ENGINE_API bool ContainsNaN() const;
	ENGINE_API bool IsNormalized() const;
};

/**
 * We pass array items by reference, which is scary as TArray can move items around in memory.
 * So we make sure to allocate enough here so it doesn't happen and crash on us.
 */
#define ANIM_NODE_DEBUG_MAX_CHAIN 50
#define ANIM_NODE_DEBUG_MAX_CHILDREN 12
#define ANIM_NODE_DEBUG_MAX_CACHEPOSE 20

struct ENGINE_API FNodeDebugData
{
private:
	struct DebugItem
	{
		DebugItem(FString Data, bool bInPoseSource) : DebugData(Data), bPoseSource(bInPoseSource) {}

		/** This node item's debug text to display. */
		FString DebugData;

		/** Whether we are supplying a pose instead of modifying one (e.g. an playing animation). */
		bool bPoseSource;

		/** Nodes that we are connected to. */
		TArray<FNodeDebugData> ChildNodeChain;
	};

	/** This nodes final contribution weight (based on its own weight and the weight of its parents). */
	float AbsoluteWeight;

	/** Nodes that we are dependent on. */
	TArray<DebugItem> NodeChain;

	/** Additional info provided, used in GetNodeName. States machines can provide the state names for the Root Nodes to use for example. */
	FString NodeDescription;

	/** Pointer to RootNode */
	FNodeDebugData* RootNodePtr;

	/** SaveCachePose Nodes */
	TArray<FNodeDebugData> SaveCachePoseNodes;

public:
	struct FFlattenedDebugData
	{
		FFlattenedDebugData(FString Line, float AbsWeight, int32 InIndent, int32 InChainID, bool bInPoseSource) : DebugLine(Line), AbsoluteWeight(AbsWeight), Indent(InIndent), ChainID(InChainID), bPoseSource(bInPoseSource){}
		FString DebugLine;
		float AbsoluteWeight;
		int32 Indent;
		int32 ChainID;
		bool bPoseSource;

		bool IsOnActiveBranch() { return FAnimWeight::IsRelevant(AbsoluteWeight); }
	};

	FNodeDebugData(const class UAnimInstance* InAnimInstance) 
		: AbsoluteWeight(1.f), RootNodePtr(this), AnimInstance(InAnimInstance)
	{
		SaveCachePoseNodes.Reserve(ANIM_NODE_DEBUG_MAX_CACHEPOSE);
	}
	
	FNodeDebugData(const class UAnimInstance* InAnimInstance, const float AbsWeight, FString InNodeDescription, FNodeDebugData* InRootNodePtr)
		: AbsoluteWeight(AbsWeight)
		, NodeDescription(InNodeDescription)
		, RootNodePtr(InRootNodePtr)
		, AnimInstance(InAnimInstance) 
	{}

	void AddDebugItem(FString DebugData, bool bPoseSource = false);
	FNodeDebugData& BranchFlow(float BranchWeight, FString InNodeDescription = FString());
	FNodeDebugData* GetCachePoseDebugData(float GlobalWeight);

	template<class Type>
	FString GetNodeName(Type* Node)
	{
		FString FinalString = FString::Printf(TEXT("%s<W:%.1f%%> %s"), *Node->StaticStruct()->GetName(), AbsoluteWeight*100.f, *NodeDescription);
		NodeDescription.Empty();
		return FinalString;
	}

	void GetFlattenedDebugData(TArray<FFlattenedDebugData>& FlattenedDebugData, int32 Indent, int32& ChainID);

	TArray<FFlattenedDebugData> GetFlattenedDebugData()
	{
		TArray<FFlattenedDebugData> Data;
		int32 ChainID = 0;
		GetFlattenedDebugData(Data, 0, ChainID);
		return Data;
	}

	// Anim instance that we are generating debug data for
	const UAnimInstance* AnimInstance;
};

/** The display mode of editable values on an animation node. */
UENUM()
namespace EPinHidingMode
{
	enum Type
	{
		/** Never show this property as a pin, it is only editable in the details panel (default for everything but FPoseLink properties). */
		NeverAsPin,

		/** Hide this property by default, but allow the user to expose it as a pin via the details panel. */
		PinHiddenByDefault,

		/** Show this property as a pin by default, but allow the user to hide it via the details panel. */
		PinShownByDefault,

		/** Always show this property as a pin; it never makes sense to edit it in the details panel (default for FPoseLink properties). */
		AlwaysAsPin
	};
}

#define ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG 0

/** A pose link to another node */
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FPoseLinkBase
{
	GENERATED_USTRUCT_BODY()

	/** Serialized link ID, used to build the non-serialized pointer map. */
	UPROPERTY()
	int32 LinkID;

#if WITH_EDITORONLY_DATA
	/** The source link ID, used for debug visualization. */
	UPROPERTY()
	int32 SourceLinkID;
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	FGraphTraversalCounter InitializationCounter;
	FGraphTraversalCounter CachedBonesCounter;
	FGraphTraversalCounter UpdateCounter;
	FGraphTraversalCounter EvaluationCounter;
#endif

protected:
	/** Flag to prevent reentry when dealing with circular trees. */
	bool bProcessed;

	/** The non serialized node pointer. */
	struct FAnimNode_Base* LinkedNode;

public:
	FPoseLinkBase()
		: LinkID(INDEX_NONE)
#if WITH_EDITORONLY_DATA
		, SourceLinkID(INDEX_NONE)
#endif
		, bProcessed(false)
		, LinkedNode(NULL)
	{
	}

	// Interface

	void Initialize(const FAnimationInitializeContext& Context);
	void CacheBones(const FAnimationCacheBonesContext& Context) ;
	void Update(const FAnimationUpdateContext& Context);
	void GatherDebugData(FNodeDebugData& DebugData);

	/** Try to re-establish the linked node pointer. */
	void AttemptRelink(const FAnimationBaseContext& Context);
	/** This only used by custom handlers, and it is advanced feature. */
	void SetLinkNode(struct FAnimNode_Base* NewLinkNode);
	/** This only used when dynamic linking other graphs to this one. */
	void SetDynamicLinkNode(struct FPoseLinkBase* InPoseLink);
	/** This only used by custom handlers, and it is advanced feature. */
	FAnimNode_Base* GetLinkNode();
};

#define ENABLE_ANIMNODE_POSE_DEBUG 0

/** A local-space pose link to another node */
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FPoseLink : public FPoseLinkBase
{
	GENERATED_USTRUCT_BODY()

public:
	// Interface
	void Evaluate(FPoseContext& Output);

#if ENABLE_ANIMNODE_POSE_DEBUG
private:
	// forwarded pose data from the wired node which current node's skeletal control is not applied yet
	FCompactHeapPose CurrentPose;
#endif //#if ENABLE_ANIMNODE_POSE_DEBUG
};

/** A component-space pose link to another node */
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FComponentSpacePoseLink : public FPoseLinkBase
{
	GENERATED_USTRUCT_BODY()

public:
	// Interface
	void EvaluateComponentSpace(FComponentSpacePoseContext& Output);
};

UENUM()
enum class EPostCopyOperation : uint8
{
	None,

	LogicalNegateBool,
};

UENUM()
enum class ECopyType : uint8
{
	// For plain old data types, we do a simple memcpy.
	PlainProperty,

	// Read and write properties using bool property helpers, as source/dest could be bitfield or boolean
	BoolProperty,
	
	// Use struct copy operation, as this needs to correctly handle CPP struct ops
	StructProperty,

	// Read and write properties using object property helpers, as source/dest could be regular/weak/lazy etc.
	ObjectProperty,

	// FName needs special case because its size changes between editor/compiler and runtime.
	NameProperty,
};


USTRUCT()
struct FExposedValueCopyRecord
{
	GENERATED_USTRUCT_BODY()

	FExposedValueCopyRecord()
		:
#if WITH_EDITORONLY_DATA
		  SourceProperty_DEPRECATED(nullptr), 
#endif
		  SourcePropertyName(NAME_None)
		, SourceSubPropertyName(NAME_None)
		, SourceArrayIndex(0)
		, bInstanceIsTarget(false)
		, PostCopyOperation(EPostCopyOperation::None)
		, CopyType(ECopyType::PlainProperty)
		, DestProperty(nullptr)
		, DestArrayIndex(0)
		, Size(0)
		, CachedSourceProperty(nullptr)
		, CachedSourceStructSubProperty(nullptr)
	{}

	void* GetDestAddr(FAnimInstanceProxy* Proxy, const FProperty* NodeProperty) const;
	const void* GetSourceAddr(FAnimInstanceProxy* Proxy) const;

#if WITH_EDITORONLY_DATA
	void PostSerialize(const FArchive& Ar);

	UPROPERTY()
	UProperty* SourceProperty_DEPRECATED;
#endif

	UPROPERTY()
	FName SourcePropertyName;

	UPROPERTY()
	FName SourceSubPropertyName;

	UPROPERTY()
	int32 SourceArrayIndex;

	// Whether or not the anim instance object is the target for the copy instead of a node.
	UPROPERTY()
	bool bInstanceIsTarget;

	UPROPERTY()
	EPostCopyOperation PostCopyOperation;

	UPROPERTY(Transient)
	ECopyType CopyType;

	UPROPERTY()
	TFieldPath<FProperty> DestProperty;

	UPROPERTY()
	int32 DestArrayIndex;

	UPROPERTY()
	int32 Size;

	// cached source property
	UPROPERTY()
	TFieldPath<FProperty> CachedSourceProperty;

	UPROPERTY()
	TFieldPath<FProperty> CachedSourceStructSubProperty;
};

#if WITH_EDITORONLY_DATA
template<>
struct TStructOpsTypeTraits< FExposedValueCopyRecord > : public TStructOpsTypeTraitsBase2< FExposedValueCopyRecord >
{
	enum
	{
		WithPostSerialize = true,
	};
};
#endif

// An exposed value updater
USTRUCT()
struct ENGINE_API FExposedValueHandler
{
	GENERATED_USTRUCT_BODY()

	FExposedValueHandler()
		: BoundFunction(NAME_None)
		, Function(nullptr)
		, ValueHandlerNodeProperty(nullptr)
		, bInitialized(false)
	{
	}

	// The function to call to update associated properties (can be NULL)
	UPROPERTY()
	FName BoundFunction;

	// Direct data access to property in anim instance
	UPROPERTY()
	TArray<FExposedValueCopyRecord> CopyRecords;

	// function pointer if BoundFunction != NAME_None
	UPROPERTY()
	UFunction* Function;

	// Node property that this value handler is associated with, when the node
	// is instantiated from this property the node's ExposedValueHandler will 
	// point back to this FExposedValueHandler:
	UPROPERTY()
	TFieldPath<FStructProperty> ValueHandlerNodeProperty;

	// Prevent multiple initialization
	bool bInitialized;

	// Helper function to bind an array of handlers:
	static void Initialize(TArray<FExposedValueHandler>& Handlers, UObject* ClassDefaultObject );

	// Bind copy records and cache UFunction if necessary
	void Initialize(UObject* AnimInstanceObject, int32 NodeOffset);

	// Execute the function and copy records
	void Execute(const FAnimationBaseContext& Context) const;
};

/**
 * This is the base of all runtime animation nodes
 *
 * To create a new animation node:
 *   Create a struct derived from FAnimNode_Base - this is your runtime node
 *   Create a class derived from UAnimGraphNode_Base, containing an instance of your runtime node as a member - this is your visual/editor-only node
 */
USTRUCT()
struct ENGINE_API FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** 
	 * Called when the node first runs. If the node is inside a state machine or cached pose branch then this can be called multiple times. 
	 * This can be called on any thread.
	 * @param	Context		Context structure providing access to relevant data
	 */
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context);

	/** 
	 * Called to cache any bones that this node needs to track (e.g. in a FBoneReference). 
	 * This is usually called at startup when LOD switches occur.
	 * This can be called on any thread.
	 * @param	Context		Context structure providing access to relevant data
	 */
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context);

	/** 
	 * Called to update the state of the graph relative to this node.
	 * Generally this should configure any weights (etc.) that could affect the poses that
	 * will need to be evaluated. This function is what usually executes EvaluateGraphExposedInputs.
	 * This can be called on any thread.
	 * @param	Context		Context structure providing access to relevant data
	 */
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context);

	/** 
	 * Called to evaluate local-space bones transforms according to the weights set up in Update().
	 * You should implement either Evaluate or EvaluateComponentSpace, but not both of these.
	 * This can be called on any thread.
	 * @param	Output		Output structure to write pose or curve data to. Also provides access to relevant data as a context.
	 */
	virtual void Evaluate_AnyThread(FPoseContext& Output);

	/** 
	 * Called to evaluate component-space bone transforms according to the weights set up in Update().
	 * You should implement either Evaluate or EvaluateComponentSpace, but not both of these.
	 * This can be called on any thread.
	 * @param	Output		Output structure to write pose or curve data to. Also provides access to relevant data as a context.
	 */	
	virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output);
	/** 
	 * If a derived anim node should respond to asset overrides, OverrideAsset should be defined to handle changing the asset 
	 * This is called during anim blueprint compilation to handle child anim blueprints.
	 * @param	NewAsset	The new asset that is being set
	 */
	virtual void OverrideAsset(class UAnimationAsset* NewAsset) {}

	/**
	 * Called to gather on-screen debug data. 
	 * This is called on the game thread.
	 * @param	DebugData	Debug data structure used to output any relevant data
	 */
	virtual void GatherDebugData(FNodeDebugData& DebugData)
	{ 
		DebugData.AddDebugItem(FString::Printf(TEXT("Non Overriden GatherDebugData! (%s)"), *DebugData.GetNodeName(this)));
	}

	/**
	 * Whether this node can run its Update() call on a worker thread.
	 * This is called on the game thread.
	 * If any node in a graph returns false from this function, then ALL nodes will update on the game thread.
	 */
	virtual bool CanUpdateInWorkerThread() const { return true; }

	/**
	 * Override this to indicate that PreUpdate() should be called on the game thread (usually to 
	 * gather non-thread safe data) before Update() is called.
	 * Note that this is called at load on the UAnimInstance CDO to avoid needing to call this at runtime.
	 * This is called on the game thread.
	 */
	virtual bool HasPreUpdate() const { return false; }

	/** Override this to perform game-thread work prior to non-game thread Update() being called */
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) {}

	/**
	 * For nodes that implement some kind of simulation, return true here so ResetDynamics() gets called
	 * when things like teleports, time skips etc. occur that might require special handling.
	 * Note that this is called at load on the UAnimInstance CDO to avoid needing to call this at runtime.
	 * This is called on the game thread.
	 */
	virtual bool NeedsDynamicReset() const { return false; }

	/** Called to help dynamics-based updates to recover correctly from large movements/teleports */
	virtual void ResetDynamics(ETeleportType InTeleportType);

	/**
	 * Override this if your node uses ancestor tracking and wants to be informed of Update() calls
	 * that were skipped due to pose caching.
	 */
	virtual bool WantsSkippedUpdates() const { return false; }
	
	/**
	 * Called on a tracked ancestor node when there are Update() calls that were skipped due to pose 
	 * caching. Your node must implement WantsSkippedUpdates to receive this callback.
	 */
	virtual void OnUpdatesSkipped(TArrayView<const FAnimationUpdateContext*> SkippedUpdateContexts) {}

	/** Called after compilation */
	virtual void PostCompile(const class USkeleton* InSkeleton) {}

	/** 
	 * For nodes that need some kind of initialization that is not dependent on node relevancy 
	 * (i.e. it is insufficent or inefficent to use Initialize_AnyThread), return true here.
	 * Note that this is called at load on the UAnimInstance CDO to avoid needing to call this at runtime.
	 */
	virtual bool NeedsOnInitializeAnimInstance() const { return false; }

	virtual ~FAnimNode_Base() {}

	/** Deprecated functions */
	UE_DEPRECATED(4.17, "Please use Initialize_AnyThread instead")
	virtual void Initialize(const FAnimationInitializeContext& Context);
	UE_DEPRECATED(4.17, "Please use CacheBones_AnyThread instead")
	virtual void CacheBones(const FAnimationCacheBonesContext& Context) {}
	UE_DEPRECATED(4.17, "Please use Update_AnyThread instead")
	virtual void Update(const FAnimationUpdateContext& Context) {}
	UE_DEPRECATED(4.17, "Please use Evaluate_AnyThread instead")
	virtual void Evaluate(FPoseContext& Output) { check(false); }
	UE_DEPRECATED(4.17, "Please use EvaluateComponentSpace_AnyThread instead")
	virtual void EvaluateComponentSpace(FComponentSpacePoseContext& Output) { check(false); }
	UE_DEPRECATED(4.20, "Please use ResetDynamics with an ETeleportPhysics flag instead")
	virtual void ResetDynamics() {}

	// The default handler for graph-exposed inputs:
	const FExposedValueHandler& GetEvaluateGraphExposedInputs();

	// Initialization function for the default handler for graph-exposed inputs, used only by instancing code:
	void SetExposedValueHandler(const FExposedValueHandler* Handler) 
	{ 
		ExposedValueHandler = Handler; 
	}

protected:
	/** return true if enabled, otherwise, return false. This is utility function that can be used per node level */
	bool IsLODEnabled(FAnimInstanceProxy* AnimInstanceProxy);
	virtual int32 GetLODThreshold() const { return INDEX_NONE; }

	/** Deprecated function */
	UE_DEPRECATED(4.17, "Please use OnInitializeAnimInstance instead")
	virtual void RootInitialize(const FAnimInstanceProxy* InProxy) {}

	/** Called once, from game thread as the parent anim instance is created */
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance);

	friend struct FAnimInstanceProxy;

private:		
	// Reference to the exposed value handler used by this node. Allocated on the class, rather than per instance:
	const FExposedValueHandler* ExposedValueHandler = nullptr;
};


#include "UObject/DefineUPropertyMacros.h"