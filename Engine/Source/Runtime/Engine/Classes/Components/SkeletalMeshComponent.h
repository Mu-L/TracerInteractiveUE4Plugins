// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Components/SceneComponent.h"
#include "EngineDefines.h"
#include "CollisionQueryParams.h"
#include "SkeletalMeshTypes.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimCurveTypes.h"
#include "Components/SkinnedMeshComponent.h"
#include "ClothSimData.h"
#include "SingleAnimationPlayData.h"
#include "Animation/PoseSnapshot.h"
#include "SkeletalMeshTypes.h"

#include "ClothingSystemRuntimeTypes.h"
#include "ClothingSimulationInterface.h"
#include "ClothingSimulationFactory.h"
#include "ClothCollisionPrim.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/CustomAttributesRuntime.h"

#include "SkeletalMeshComponent.generated.h"


class Error;
class FPrimitiveDrawInterface;
class FCanvas;
class FSceneView;
class UAnimInstance;
class UPhysicalMaterial;
class USkeletalMeshComponent;
struct FConstraintInstance;
struct FNavigableGeometryExport;
struct FCompactPose;

enum class EClothingTeleportMode : uint8;

DECLARE_MULTICAST_DELEGATE(FOnSkelMeshPhysicsCreatedMultiCast);
typedef FOnSkelMeshPhysicsCreatedMultiCast::FDelegate FOnSkelMeshPhysicsCreated;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAnimInitialized);

DECLARE_MULTICAST_DELEGATE(FOnSkelMeshTeleportedMultiCast);
typedef FOnSkelMeshTeleportedMultiCast::FDelegate FOnSkelMeshTeleported;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoneTransformsFinalized);

#if PHYSICS_INTERFACE_PHYSX
namespace physx
{
	class PxAggregate;
}
#endif

UENUM()
enum class EAnimCurveType : uint8 
{
	AttributeCurve,
	MaterialCurve, 
	MorphTargetCurve, 
	// make sure to update MaxCurve 
	MaxAnimCurveType UMETA(Hidden)
};

ENUM_RANGE_BY_COUNT(EAnimCurveType, EAnimCurveType::MaxAnimCurveType);

UENUM()
enum class EClothMassMode : uint8
{
	UniformMass,
	TotalMass,
	Density,
	MaxClothMassMode UMETA(Hidden)
};

/** Method used when retrieving a Custom Attribute value*/
UENUM()
enum class ECustomBoneAttributeLookup : uint8
{
	/** Only look for the attribute using the provided bone (name) */
	BoneOnly,
	/** Look for the attribute using the provided bone (name) and its direct parent bone */
	ImmediateParent,
	/** Look for the attribute using the provided bone (name) and its direct bone parent hierarchy up and until the root bone */
	ParentHierarchy
};

struct FAnimationEvaluationContext
{
	// The anim instance we are evaluating
	UAnimInstance* AnimInstance;

	// The post process instance we are evaluating
	UAnimInstance* PostProcessAnimInstance;

	// The SkeletalMesh we are evaluating for
	USkeletalMesh* SkeletalMesh;

	// Evaluation data, swapped in from the component when we are running parallel eval
	TArray<FTransform> ComponentSpaceTransforms;
	TArray<FTransform> BoneSpaceTransforms;
	TArray<FTransform> CachedComponentSpaceTransforms;
	TArray<FTransform> CachedBoneSpaceTransforms;
	FVector RootBoneTranslation;

	// Are we performing interpolation this tick
	bool bDoInterpolation;

	// Are we evaluating this tick
	bool bDoEvaluation;

	// Are we storing data in cache bones this tick
	bool bDuplicateToCacheBones;

	// duplicate the cache curves
	bool bDuplicateToCacheCurve;

	// duplicate the cached attributes
	bool bDuplicateToCachedAttributes;

	// Curve data, swapped in from the component when we are running parallel eval
	FBlendedHeapCurve	Curve;
	FBlendedHeapCurve	CachedCurve;

	// Custom attribute data, swapped in from the component when we are running parallel eval
	FHeapCustomAttributes CustomAttributes;
	FHeapCustomAttributes CachedCustomAttributes;

	FAnimationEvaluationContext()
	{
		Clear();
	}

	void Copy(const FAnimationEvaluationContext& Other)
	{
		AnimInstance = Other.AnimInstance;
		PostProcessAnimInstance = Other.PostProcessAnimInstance;
		SkeletalMesh = Other.SkeletalMesh;
		ComponentSpaceTransforms.Reset();
		ComponentSpaceTransforms.Append(Other.ComponentSpaceTransforms);
		BoneSpaceTransforms.Reset();
		BoneSpaceTransforms.Append(Other.BoneSpaceTransforms);
		CachedComponentSpaceTransforms.Reset();
		CachedComponentSpaceTransforms.Append(Other.CachedComponentSpaceTransforms);
		CachedBoneSpaceTransforms.Reset();
		CachedBoneSpaceTransforms.Append(Other.CachedBoneSpaceTransforms);
		RootBoneTranslation = Other.RootBoneTranslation;
		Curve.CopyFrom(Other.Curve);
		CachedCurve.CopyFrom(Other.CachedCurve);
		bDoInterpolation = Other.bDoInterpolation;
		bDoEvaluation = Other.bDoEvaluation;
		bDuplicateToCacheBones = Other.bDuplicateToCacheBones;
		bDuplicateToCacheCurve = Other.bDuplicateToCacheCurve;
		bDuplicateToCachedAttributes = Other.bDuplicateToCachedAttributes;

		CustomAttributes.CopyFrom(Other.CustomAttributes);
		CachedCustomAttributes.CopyFrom(Other.CachedCustomAttributes);
	}

	void Clear()
	{
		AnimInstance = nullptr;
		PostProcessAnimInstance = nullptr;
		SkeletalMesh = nullptr;
	}

};

/** This enum defines how you'd like to update bones to physics world.
	If bone is simulating, you don't have to waste time on updating bone transform from kinematic.
	But also sometimes you don't like fixed bones to be updated by animation data. */
UENUM()
namespace EKinematicBonesUpdateToPhysics
{
	enum Type
	{
		/** Update any bones that are not simulating. */
		SkipSimulatingBones,
		/** Skip physics update from kinematic changes. */
		SkipAllBones
	};
}

UENUM()
namespace EAnimationMode
{
	enum Type
	{
		AnimationBlueprint UMETA(DisplayName="Use Animation Blueprint"), 
		AnimationSingleNode UMETA(DisplayName="Use Animation Asset"), 
		// This is custom type, engine leaves AnimInstance as it is
		AnimationCustomMode UMETA(DisplayName = "Use Custom Mode"),
	};
}

UENUM()
namespace EPhysicsTransformUpdateMode
{
	enum Type
	{
		SimulationUpatesComponentTransform,
		ComponentTransformIsKinematic
	};
}

/** Enum for indicating whether kinematic updates can be deferred */
enum class EAllowKinematicDeferral
{
	AllowDeferral,
	DisallowDeferral
};

/**
* Tick function that does post physics work on skeletal mesh component. This executes in EndPhysics (after physics is done)
**/
USTRUCT()
struct FSkeletalMeshComponentEndPhysicsTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	 USkeletalMeshComponent*	Target;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
	/** Function used to describe this tick for active tick reporting. **/
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FSkeletalMeshComponentEndPhysicsTickFunction> : public TStructOpsTypeTraitsBase2<FSkeletalMeshComponentEndPhysicsTickFunction>
{
	enum
	{
		WithCopy = false
	};
};


/**
* Tick function that prepares and simulates cloth
**/
USTRUCT()
struct FSkeletalMeshComponentClothTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	USkeletalMeshComponent*	Target;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
	/** Function used to describe this tick for active tick reporting. **/
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FSkeletalMeshComponentClothTickFunction> : public TStructOpsTypeTraitsBase2<FSkeletalMeshComponentClothTickFunction>
{
	enum
	{
		WithCopy = false
	};
};


struct ENGINE_API FClosestPointOnPhysicsAsset
{
	/** The closest point in world space */
	FVector ClosestWorldPosition;

	/** The normal associated with the surface of the closest body */
	FVector Normal;

	/** The name of the bone associated with the closest body */
	FName BoneName;

	/** The distance of the closest point and the original world position. 0 Indicates world position is inside the closest body. */
	float Distance;

	FClosestPointOnPhysicsAsset()
		: ClosestWorldPosition(FVector::ZeroVector)
		, Normal(FVector::ZeroVector)
		, BoneName(NAME_None)
		, Distance(-1.f)
	{
	}
};

/**
 * SkeletalMeshComponent is used to create an instance of an animated SkeletalMesh asset.
 *
 * @see https://docs.unrealengine.com/latest/INT/Engine/Content/Types/SkeletalMeshes/
 * @see USkeletalMesh
 */
UCLASS(Blueprintable, ClassGroup=(Rendering, Common), hidecategories=Object, config=Engine, editinlinenew, meta=(BlueprintSpawnableComponent))
class ENGINE_API USkeletalMeshComponent : public USkinnedMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_UCLASS_BODY()

	friend class FSkinnedMeshComponentRecreateRenderStateContext;
	friend class FParallelAnimationCompletionTask;
	friend class USkeletalMesh; 
	friend class UAnimInstance;
	friend struct FAnimNode_LinkedAnimGraph;
	friend struct FAnimNode_LinkedAnimLayer;
	friend struct FLinkedInstancesAdapter;

	/**
	 * Animation 
	 */
	
	/** @Todo anim: Matinee related data start - this needs to be replaced to new system. */
	
	/** @Todo anim: Matinee related data end - this needs to be replaced to new system. */

public:
#if WITH_EDITORONLY_DATA
	/** The blueprint for creating an AnimationScript. */
	UPROPERTY()
	class UAnimBlueprint* AnimationBlueprint_DEPRECATED;
#endif

	UE_DEPRECATED(4.11, "This property is deprecated. Please use AnimClass instead")
	UPROPERTY(BlueprintReadOnly, Category = Animation, meta = (DeprecationMessage = "This property is deprecated. Please use AnimClass instead"))
	class UAnimBlueprintGeneratedClass* AnimBlueprintGeneratedClass;

	/* The AnimBlueprint class to use. Use 'SetAnimInstanceClass' to change at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Animation)
	class TSubclassOf<UAnimInstance> AnimClass;

	/** The active animation graph program instance. */
	UPROPERTY(transient, NonTransactional)
	UAnimInstance* AnimScriptInstance;

#if WITH_EDITORONLY_DATA
	/** Any running linked anim instances */
	UE_DEPRECATED(4.24, "Direct access to this property is deprecated and the array is no longer used. Storage is now in LinkedInstances. Please use GetLinkedAnimInstances() instead.")
	UPROPERTY(transient)
	TArray<UAnimInstance*> SubInstances;
#endif

	/** An instance created from the PostPhysicsBlueprint property of the skeletal mesh we're using,
	 *  Runs after (and receives pose from) the main anim instance.
	 */
	UPROPERTY(transient)
	UAnimInstance* PostProcessAnimInstance;

public:

	/** Toggles whether the post process blueprint will run for this component */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ToggleDisablePostProcessBlueprint();

	/** Gets whether the post process blueprint is currently disabled for this component */
	UFUNCTION(BlueprintGetter)
	bool GetDisablePostProcessBlueprint() const;

	/** Sets whether the post process blueprint is currently running for this component.
	 *  If it is not currently running, and is set to run, the instance will be reinitialized
	 */
	UFUNCTION(BlueprintSetter)
	void SetDisablePostProcessBlueprint(bool bInDisablePostProcess);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Animation, meta=(ShowOnlyInnerProperties))
	struct FSingleAnimationPlayData AnimationData;

	// this is explicit copy because this buffer is reused during evaluation
	// we want to have reference and emptied during evaluation
	TArray<FTransform> GetBoneSpaceTransforms();

	/** 
	 * Temporary array of local-space (relative to parent bone) rotation/translation for each bone. 
	 * This property is not safe to access during evaluation, so we created wrapper.
	 */
	UE_DEPRECATED(4.23, "Direct access to this property is deprecated, please use GetBoneSpaceTransforms instead. We will move to private in the future.")
	TArray<FTransform> BoneSpaceTransforms;

public:
	/** Offset of the root bone from the reference pose. Used to offset bounding box. */
	UPROPERTY(transient)
	FVector RootBoneTranslation;

	/** If bEnableLineCheckWithBounds is true, scale the bounds by this value before doing line check. */
	UPROPERTY()
	FVector LineCheckBoundsScale;

	/** Temporary storage for curves */
	FBlendedHeapCurve AnimCurves;

	/** Access cached component space transforms */
	const TArray<FTransform>& GetCachedComponentSpaceTransforms() const;

private:
	/** Any running linked anim instances */
	UPROPERTY(transient)
	TArray<UAnimInstance*> LinkedInstances;

	// Update Rate

	/** Cached BoneSpaceTransforms for Update Rate optimization. */
	UPROPERTY(Transient)
	TArray<FTransform> CachedBoneSpaceTransforms;

	/** Cached SpaceBases for Update Rate optimization. */
	UPROPERTY(Transient)
	TArray<FTransform> CachedComponentSpaceTransforms;

	/** Cached Curve result for Update Rate optimization */
	FBlendedHeapCurve CachedCurve;

	/** Current and cached custom attribute evaluation data, used for Update Rate optimization */
	FHeapCustomAttributes CachedAttributes;
	FHeapCustomAttributes CustomAttributes;
public:
	/** 
	 * Get float type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue (reference) Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category=CustomAttributes)
	bool GetFloatAttribute_Ref(const FName& BoneName, const FName& AttributeName, UPARAM(ref) float& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);

	/** 
	 * Get integer type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue (reference) Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category = CustomAttributes)
	bool GetIntegerAttribute_Ref(const FName& BoneName, const FName& AttributeName, UPARAM(ref) int32& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);

	/** 
	 * Get string type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue (reference) Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category = CustomAttributes)
	bool GetStringAttribute_Ref(const FName& BoneName, const FName& AttributeName, UPARAM(ref) FString& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);


	/** 
	 * Get float type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category = CustomAttributes)
	bool GetFloatAttribute(const FName& BoneName, const FName& AttributeName, float DefaultValue, float& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);

	/** 
	 * Get integer type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category = CustomAttributes)
	bool GetIntegerAttribute(const FName& BoneName, const FName& AttributeName, int32 DefaultValue, int32& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);

	/** 
	 * Get string type custom attribute value.

	 * @param BoneName Name of the bone to retrieve try and retrieve the attribute from
	 * @param AttributeName Name of the attribute to retrieve
	 * @param DefaultValue In case the attribute could not be found
     * @param OutValue Retrieved attribute value if found, otherwise is set to DefaultValue
	 * @param LookupType Determines how the attribute is retrieved from the specified BoneName (see ECustomBoneAttributeLookup)
	 * @return Whether or not the atttribute was successfully retrieved
	*/
	UFUNCTION(BlueprintCallable, Category = CustomAttributes)
	bool GetStringAttribute(const FName& BoneName, const FName& AttributeName, FString DefaultValue, FString& OutValue, ECustomBoneAttributeLookup LookupType = ECustomBoneAttributeLookup::BoneOnly);

protected:
	/** Templated version to try and retrieve a typed bone attribute's value */
	template<typename DataType>
	bool GetBoneAttribute(const FName& BoneName, const FName& AttributeName, DataType DefaultValue, DataType& OutValue, ECustomBoneAttributeLookup LookupType);	

public:
	/** Used to scale speed of all animations on this skeletal mesh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Animation)
	float GlobalAnimRateScale;
	
	/** If we are running physics, should we update non-simulated bones based on the animation bone positions. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	TEnumAsByte<EKinematicBonesUpdateToPhysics::Type> KinematicBonesUpdateType;

	/** Whether physics simulation updates component transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Physics)
	TEnumAsByte<EPhysicsTransformUpdateMode::Type> PhysicsTransformUpdateMode;

	/** whether we need to teleport cloth. */
	EClothingTeleportMode ClothTeleportMode;

protected:
	/** Whether to use Animation Blueprint or play Single Animation Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Animation)
	TEnumAsByte<EAnimationMode::Type>	AnimationMode;
	
private:
	/** Teleport type to use on the next update */
	ETeleportType PendingTeleportType;

	/** Controls whether or not this component will evaluate its post process instance. The post-process
	 *  Instance is dictated by the skeletal mesh so this is used for per-instance control.
	 */
	UPROPERTY(EditAnywhere, BlueprintGetter=GetDisablePostProcessBlueprint, BlueprintSetter=SetDisablePostProcessBlueprint, Category = Animation)
	uint8 bDisablePostProcessBlueprint:1;

public:
	/** Indicates that simulation (if it's enabled) is entirely responsible for children transforms. This is only ok if you are not animating attachment points relative to the simulation */
	uint8 bSimulationUpdatesChildTransforms:1;

	/** Controls whether blending in physics bones will refresh overlaps on this component, defaults to true but can be disabled in cases where we know anim->physics blending doesn't meaningfully change overlaps */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Physics)
	uint8 bUpdateOverlapsOnAnimationFinalize:1;

	/** Temporary fix for local space kinematics. This only works for bodies that have no constraints and is needed by vehicles. Proper support will remove this flag */
	uint8 bLocalSpaceKinematics:1;

	/** If true, there is at least one body in the current PhysicsAsset with a valid bone in the current SkeletalMesh */
	UPROPERTY(transient)
	uint8 bHasValidBodies:1;


	/** Enables blending in of physics bodies whether Simulate or not*/
	UPROPERTY(transient)
	uint8 bBlendPhysics:1;

	/**
	 *  If true, simulate physics for this component on a dedicated server.
	 *  This should be set if simulating physics and replicating with a dedicated server.
	 *	Note: This property cannot be changed at runtime.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = SkeletalMesh)
	uint8 bEnablePhysicsOnDedicatedServer:1;

	/**
	 *	If we should pass joint position to joints each frame, so that they can be used by motorized joints to drive the
	 *	ragdoll based on the animation.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	uint8 bUpdateJointsFromAnimation:1;

	/** Disable cloth simulation and play original animation without simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint8 bDisableClothSimulation:1;

	/** Indicates that this SkeletalMeshComponent has deferred kinematic bone updates until next physics sim if not INDEX_NONE. */
	int32 DeferredKinematicUpdateIndex;
#if PHYSICS_INTERFACE_PHYSX
	/** Indicates that this SkeletalMeshComponent has deferred kinematic bone updates until next physics sim.  */
	uint8 bDeferredKinematicUpdate:1;
#endif

private:
	/** Disable rigid body animation nodes and play original animation without simulation */
	UPROPERTY(EditAnywhere, Category=Clothing)
	uint8 bDisableRigidBodyAnimNode:1;

	/** Disable animation curves for this component. If this is set true, no curves will be processed */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = SkeletalMesh)
	uint8 bAllowAnimCurveEvaluation : 1;

	/** DEPRECATED. Use bAllowAnimCurveEvaluation instead */
	UE_DEPRECATED(4.18, "This property is deprecated. Please use bAllowAnimCurveEvaluatiuon instead. Note that the meaning is reversed.")	
	UPROPERTY()
	uint8 bDisableAnimCurves_DEPRECATED : 1;

	/** Whether or not we're taking cloth sim information from our master component */
	uint8 bBindClothToMasterComponent:1;

	/** Flag denoting whether or not the clothing transform needs to update */
	uint8 bPendingClothTransformUpdate:1;

	/** Flag denoting whether or not the clothing collision needs to update from its physics asset */
	uint8 bPendingClothCollisionUpdate:1;

public:
	/** can't collide with part of environment if total collision volumes exceed 16 capsules or 32 planes per convex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint8 bCollideWithEnvironment:1;
	/** can't collide with part of attached children if total collision volumes exceed 16 capsules or 32 planes per convex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint8 bCollideWithAttachedChildren:1;
	/**
	 * It's worth trying this option when you feel that the current cloth simulation is unstable.
	 * The scale of the actor is maintained during the simulation. 
	 * It is possible to add the inertia effects to the simulation, through the inertiaScale parameter of the clothing material. 
	 * So with an inertiaScale of 1.0 there should be no visible difference between local space and global space simulation. 
	 * Known issues: - Currently there's simulation issues when this feature is used in 3.x (DE4076) So if localSpaceSim is enabled there's no inertia effect when the global pose of the clothing actor changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Clothing)
	uint8 bLocalSpaceSimulation : 1;

	/** reset the clothing after moving the clothing position (called teleport) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Clothing)
	uint8 bResetAfterTeleport:1;

	/** To save previous state */
	uint8 bPrevDisableClothSimulation : 1;

	/** 
	 * Optimization
	 */
	
	 /** Whether animation and world transform updates are deferred. If this is on, the kinematic bodies (scene query data) will not update until the next time the physics simulation is run */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = SkeletalMesh)
	uint8 bDeferKinematicBoneUpdate : 1;

	/** Skips Ticking and Bone Refresh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=SkeletalMesh)
	uint8 bNoSkeletonUpdate:1;

	/** pauses this component's animations (doesn't tick them, but still refreshes bones) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category=Animation)
	uint8 bPauseAnims:1;

	/** On InitAnim should we set to ref pose (if false use first tick of animation data)*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Animation)
	uint8 bUseRefPoseOnInitAnim:1;

	/**
	* Uses skinned data for collision data.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=SkeletalMesh)
	uint8 bEnablePerPolyCollision:1;

	/**
	 * Misc 
	 */
	
	/** If true, force the mesh into the reference pose - is an optimization. */
	UPROPERTY()
	uint8 bForceRefpose:1;
	
	/** If true TickPose() will not be called from the Component's TickComponent function.
	* It will instead be called from Autonomous networking updates. See ACharacter. */
	UPROPERTY(Transient)
	uint8 bOnlyAllowAutonomousTickPose : 1;

	/** True if calling TickPose() from Autonomous networking updates. See ACharacter. */
	UPROPERTY(Transient)
	uint8 bIsAutonomousTickPose : 1;

	/** If bForceRefPose was set last tick. */
	UPROPERTY()
	uint8 bOldForceRefPose:1;

	/** Bool that enables debug drawing of the skeleton before it is passed to the physics. Useful for debugging animation-driven physics. */
	UPROPERTY()
	uint8 bShowPrePhysBones:1;

	/** If false, indicates that on the next call to UpdateSkelPose the RequiredBones array should be recalculated. */
	UPROPERTY(transient)
	uint8 bRequiredBonesUpToDate:1;

	/** If true, AnimTree has been initialised. */
	UPROPERTY(transient)
	uint8 bAnimTreeInitialised:1;

	/** If true, the Location of this Component will be included into its bounds calculation
	* (this can be useful when using SMU_OnlyTickPoseWhenRendered on a character that moves away from the root and no bones are left near the origin of the component) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = SkeletalMesh)
	uint8 bIncludeComponentLocationIntoBounds : 1;

	/** If true, line checks will test against the bounding box of this skeletal mesh component and return a hit if there is a collision. */
	UPROPERTY()
	uint8 bEnableLineCheckWithBounds:1;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	uint8 bUseBendingElements_DEPRECATED :1;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	uint8 bUseTetrahedralConstraints_DEPRECATED :1;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	uint8 bUseThinShellVolumeConstraints_DEPRECATED :1;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	uint8 bUseSelfCollisions_DEPRECATED :1;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	uint8 bUseContinuousCollisionDetection_DEPRECATED :1;

	/** If true, propagates calls to ApplyAnimationCurvesToComponent for slave components, only needed if slave components do not tick themselves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MasterPoseComponent)
	uint8 bPropagateCurvesToSlaves : 1;

	/** Whether to skip UpdateKinematicBonesToAnim() when interpolating. Kinematic bones are updated to the target interpolation pose only on ticks when they are evaluated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Optimization)
	uint8 bSkipKinematicUpdateWhenInterpolating:1;

	/** Whether to skip bounds update when interpolating. Bounds are updated to the target interpolation pose only on ticks when they are evaluated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Optimization)
	uint8 bSkipBoundsUpdateWhenInterpolating:1;

protected:

	/** Whether the clothing simulation is suspended (not the same as disabled, we no longer run the sim but keep the last valid sim data around) */
	uint8 bClothingSimulationSuspended:1;

#if WITH_EDITORONLY_DATA
	/** If true, this will Tick until disabled */
	UPROPERTY(AdvancedDisplay, EditInstanceOnly, transient, Category = SkeletalMesh)
	uint8 bUpdateAnimationInEditor : 1;
	/** If true, will play cloth in editor */
	UPROPERTY(AdvancedDisplay, EditInstanceOnly, transient, Category = SkeletalMesh)
	uint8 bUpdateClothInEditor : 1;
#endif

	/** If true, OnSyncComponentToRBPhysics() notify will be called */
	uint8 bNotifySyncComponentToRBPhysics : 1;

private:

	UPROPERTY(Transient)
	uint8 bNeedsQueuedAnimEventsDispatched:1;

	uint8 bPostEvaluatingAnimation:1;

public:

	/** Cache AnimCurveUidVersion from Skeleton and this will be used to identify if it needs to be updated */
	UPROPERTY(transient)
	uint16 CachedAnimCurveUidVersion;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	EClothMassMode MassMode_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float UniformMass_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float TotalMass_DEPRECATED;
	
	/**
	 * Water: 1.0
	 * Cotton: 0.155
	 * Wool: 0.13
	 * Silk: 0.133
	 */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float Density_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float MinPerParticleMass_DEPRECATED;


	/**
	 * weight to blend between simulated results and key-framed positions
	 * if weight is 1.0, shows only cloth simulation results and 0.0 will show only skinned results
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = Clothing)
	float ClothBlendWeight;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float EdgeStiffness_DEPRECATED;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float BendingStiffness_DEPRECATED;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float AreaStiffness_DEPRECATED;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float VolumeStiffness_DEPRECATED;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float StrainLimitingStiffness_DEPRECATED;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "This property is deprecated, please set it on the Clothing Asset / ClothConfig instead."))
	float ShapeTargetStiffness_DEPRECATED;

	/** Whether we should stall the Cloth tick task until the cloth simulation is complete. This is required if we want up-to-date
	 * cloth data on the game thread, for example if we want to generate particles at cloth vertices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Clothing)
	bool bWaitForParallelClothTask;

private:

	/** You can choose to disable certain curves if you prefer. 
	 * This is transient curves that will be ignored by animation system if you choose this */
	UPROPERTY(transient)
	TArray<FName> DisallowedAnimCurves;

public:

	/**
	* Used for per poly collision. In 99% of cases you will be better off using a Physics Asset.
	* This BodySetup is per instance because all modification of vertices is done in place */
	UPROPERTY(transient)
	class UBodySetup * BodySetup;

	void CreateBodySetup();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	virtual void SendRenderDebugPhysics(FPrimitiveSceneProxy* OverrideSceneProxy = nullptr) override;
#endif

public:

	/** Threshold for physics asset bodies above which we use an aggregate for broadphase collisions */
	int32 RagdollAggregateThreshold;

	float ClothMaxDistanceScale;

	/** Notification when constraint is broken. */
	UPROPERTY(BlueprintAssignable)
	FConstraintBrokenSignature OnConstraintBroken;

	/** Class of the object responsible for  */
	UPROPERTY(EditAnywhere, Category = ClothingSimulation)
	TSubclassOf<class UClothingSimulationFactory> ClothingSimulationFactory;

	struct FPendingRadialForces
	{
		enum EType
		{
			AddImpulse,
			AddForce
		};

		FVector Origin;
		float Radius;
		float Strength;
		ERadialImpulseFalloff Falloff;
		bool bIgnoreMass;
		EType Type;
		int32 FrameNum;

		FPendingRadialForces(FVector InOrigin, float InRadius, float InStrength, ERadialImpulseFalloff InFalloff, bool InIgnoreMass, EType InType)
			: Origin(InOrigin)
			, Radius(InRadius)
			, Strength(InStrength)
			, Falloff(InFalloff)
			, bIgnoreMass(InIgnoreMass)
			, Type(InType)
			, FrameNum(GFrameNumber)
		{
		}
	};

	const TArray<FPendingRadialForces>& GetPendingRadialForces() const
	{
		return PendingRadialForces;
	}
	/** Array of physical interactions for the frame. This is a temporary solution for a more permanent force system and should not be used directly*/
	TArray<FPendingRadialForces> PendingRadialForces;

	UE_DEPRECATED(4.23, "This function is deprecated. Please use SetAnimClass instead. ")
	virtual void K2_SetAnimInstanceClass(class UClass* NewClass);

	/** Set the anim instance class. Clears and re-initializes the anim instance with the new class and sets animation mode to 'AnimationBlueprint' */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "AnimBlueprint", DisplayName = "Set Anim Instance Class"))
	virtual void SetAnimClass(class UClass* NewClass);

	/** Get the anim instance class via getter callable by sequencer.  */
	UFUNCTION(BlueprintInternalUseOnly)
	class UClass*  GetAnimClass();

	/** Set the anim instance class. Clears and re-initializes the anim instance with the new class and sets animation mode to 'AnimationBlueprint' */
	void SetAnimInstanceClass(class UClass* NewClass);

	/** 
	 * Returns the animation instance that is driving the class (if available). This is typically an instance of
	 * the class set as AnimBlueprintGeneratedClass (generated by an animation blueprint)
	 * Since this instance is transient, it is not safe to be used during construction script
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(Keywords = "AnimBlueprint", UnsafeDuringActorConstruction = "true"))
	class UAnimInstance * GetAnimInstance() const;
	
	/**
	 * Returns the active post process instance is one is available. This is set on the mesh that this
	 * component is using, and is evaluated immediately after the main instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "AnimBlueprint"))
	UAnimInstance* GetPostProcessInstance() const;

	/** Get the anim instances linked to the main AnimScriptInstance */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TArray<UAnimInstance*>& GetLinkedAnimInstances() const { return LinkedInstances; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

private:
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TArray<UAnimInstance*>& GetLinkedAnimInstances() { return LinkedInstances; }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** Clear the linked anim instances and mark them pending kill */
	void ResetLinkedAnimInstances();

public:
	UE_DEPRECATED(4.23, "This function is deprecated. Please use GetLinkedAnimGraphInstanceByTag")
	UAnimInstance* GetSubInstanceByName(FName InTag) const { return GetLinkedAnimGraphInstanceByTag(InTag); }

	UE_DEPRECATED(4.24, "This function is deprecated. Please use GetLinkedAnimGraphInstanceByTag")
	UAnimInstance* GetSubInstanceByTag(FName InTag) const { return GetLinkedAnimGraphInstanceByTag(InTag); }

	/**
	 * Returns the a tagged linked instance node. If no linked instances are found or none are tagged with the
	 * supplied name, this will return NULL.
	 */
	UFUNCTION(BlueprintPure, Category = "Components|SkeletalMesh|Animation Blueprint Linking", meta = (Keywords = "AnimBlueprint"))
	UAnimInstance* GetLinkedAnimGraphInstanceByTag(FName InTag) const;

	UE_DEPRECATED(4.24, "Function renamed, please use GetLinkedAnimGraphInstancesByTag")
	void GetSubInstancesByTag(FName InTag, TArray<UAnimInstance*>& OutSubInstances) const { GetLinkedAnimGraphInstancesByTag(InTag, OutSubInstances); }

	/**
	 * Returns all tagged linked instance nodes that match the tag.
	 */
	UFUNCTION(BlueprintPure, Category = "Components|SkeletalMesh|Animation Blueprint Linking", meta = (Keywords = "AnimBlueprint"))
	void GetLinkedAnimGraphInstancesByTag(FName InTag, TArray<UAnimInstance*>& OutLinkedInstances) const;

	UE_DEPRECATED(4.24, "Function renamed, please use LinkAnimGraphByTag")
	void SetSubInstanceClassByTag(FName InTag, TSubclassOf<UAnimInstance> InClass) { LinkAnimGraphByTag(InTag, InClass); }

	/** Runs through all nodes, attempting to find linked instance by name/tag, then sets the class of each node if the tag matches */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh|Animation Blueprint Linking", meta = (Keywords = "AnimBlueprint"))
	void LinkAnimGraphByTag(FName InTag, TSubclassOf<UAnimInstance> InClass);

	UE_DEPRECATED(4.24, "Function renamed, please use LinkAnimClassLayers")
	void SetLayerOverlay(TSubclassOf<UAnimInstance> InClass) { LinkAnimClassLayers(InClass); }

	/** 
	 * Runs through all layer nodes, attempting to find layer nodes that are implemented by the specified class, then sets up a linked instance of the class for each.
	 * Allocates one linked instance to run each of the groups specified in the class, so state is shared. If a layer is not grouped (ie. NAME_None), then state is not shared
	 * and a separate linked instance is allocated for each layer node.
	 * If InClass is null, then all layers are reset to their defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh|Animation Blueprint Linking")
	void LinkAnimClassLayers(TSubclassOf<UAnimInstance> InClass);

	UE_DEPRECATED(4.24, "Function renamed, please use UnlinkAnimClassLayers")
	void ClearLayerOverlay(TSubclassOf<UAnimInstance> InClass) { UnlinkAnimClassLayers(InClass); }

	/** 
	 * Runs through all layer nodes, attempting to find layer nodes that are currently running the specified class, then resets each to its default value.
	 * State sharing rules are as with SetLayerOverlay.
	 * If InClass is null, does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh|Animation Blueprint Linking")
	void UnlinkAnimClassLayers(TSubclassOf<UAnimInstance> InClass);

	UE_DEPRECATED(4.24, "Function renamed, please use GetLinkedLayerInstanceByGroup")
	UAnimInstance* GetLayerSubInstanceByGroup(FName InGroup) const { return GetLinkedAnimLayerInstanceByGroup(InGroup); }

	/** Gets the layer linked instance corresponding to the specified group */
	UFUNCTION(BlueprintPure, Category = "Components|SkeletalMesh|Animation Blueprint Linking")
	UAnimInstance* GetLinkedAnimLayerInstanceByGroup(FName InGroup) const;

	UE_DEPRECATED(4.24, "Function renamed, please use GetLinkedAnimLayerInstanceByClass")
	UAnimInstance* GetLayerSubInstanceByClass(TSubclassOf<UAnimInstance> InClass) const { return GetLinkedAnimLayerInstanceByClass(InClass);  }

	/** Gets the first layer linked instance corresponding to the specified class */
	UFUNCTION(BlueprintPure, Category = "Components|SkeletalMesh|Animation Blueprint Linking")
	UAnimInstance* GetLinkedAnimLayerInstanceByClass(TSubclassOf<UAnimInstance> InClass) const;

	/** Calls a function on each of the anim instances that this mesh component hosts, including linked and post-process instances */
	void ForEachAnimInstance(TFunctionRef<void(UAnimInstance*)> InFunction);

	/** 
	 * Returns whether there are any valid instances to run, currently this means whether we have
	 * have an animation instance or a post process instance available to process.
	 */
	UFUNCTION(BlueprintPure, Category = "Components|SkeletalMesh", meta = (Keywords = "AnimBlueprint"))
	bool HasValidAnimationInstance() const;

	/**
	 * Informs any active anim instances (main instance, linked instances, post instance) that a dynamics reset is required
	 * for example if a teleport occurs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "Dynamics,Physics", UnsafeDuringActorConstruction = "true"))
	void ResetAnimInstanceDynamics(ETeleportType InTeleportType = ETeleportType::ResetPhysics);

	/** Below are the interface to control animation when animation mode, not blueprint mode **/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation"))
	void SetAnimationMode(EAnimationMode::Type InAnimationMode);
	
	UFUNCTION(BlueprintPure, Category = "Components|Animation", meta = (Keywords = "Animation"))
	EAnimationMode::Type GetAnimationMode() const;

	/* Animation play functions
	 *
	 * These changes status of animation instance, which is transient data, which means it won't serialize with this component
	 * Because of that reason, it is not safe to be used during construction script
	 * Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized 
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void PlayAnimation(class UAnimationAsset* NewAnimToPlay, bool bLooping);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetAnimation(class UAnimationAsset* NewAnimToPlay);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void Play(bool bLooping);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void Stop();

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	bool IsPlaying() const;

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetPosition(float InPos, bool bFireNotifies = true);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	float GetPosition() const;

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	void SetPlayRate(float Rate);

	/* Animation play functions
	*
	* These changes status of animation instance, which is transient data, which means it won't serialize with this component
	* Because of that reason, it is not safe to be used during construction script
	* Please use OverrideAnimationDatat for construction script. That will override AnimationData to be serialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation", UnsafeDuringActorConstruction = "true"))
	float GetPlayRate() const;

	/**
	 * This overrides current AnimationData parameter in the SkeletalMeshComponent. This will serialize when the component serialize
	 * so it can be used during construction script. However note that this will override current existing data
	 * This can be useful if you'd like to make a blueprint with custom default animation per component
	 * This sets single player mode, which means you can't use AnimBlueprint with it
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|Animation", meta = (Keywords = "Animation"))
	void OverrideAnimationData(UAnimationAsset* InAnimToPlay, bool bIsLooping = true, bool bIsPlaying = true, float Position = 0.f, float PlayRate = 1.f);

	/**
	 * Set Morph Target with Name and Value(0-1)
	 *
	 * @param bRemoveZeroWeight : Used by editor code when it should stay in the active list with zero weight
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void SetMorphTarget(FName MorphTargetName, float Value, bool bRemoveZeroWeight=true);

	/**
	 * Clear all Morph Target that are set to this mesh
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ClearMorphTargets();

	/**
	 * Get Morph target with given name
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	float GetMorphTarget(FName MorphTargetName) const;

	/**
	 * Takes a snapshot of this skeletal mesh component's pose and saves it to the specified snapshot.
	 * The snapshot is taken at the current LOD, so if for example you took the snapshot at LOD1 
	 * and then used it at LOD0 any bones not in LOD1 will use the reference pose 
	 */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SnapshotPose(UPARAM(ref) FPoseSnapshot& Snapshot);

	/**
	 * Get/Set the max distance scale of clothing mesh vertices
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	float GetClothMaxDistanceScale() const;
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetClothMaxDistanceScale(float Scale);

	/** 
	 * Used to indicate we should force 'teleport' during the next call to UpdateClothState, 
	 * This will transform positions and velocities and thus keep the simulation state, just translate it to a new pose.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ForceClothNextUpdateTeleport();
	/** 
	 * Used to indicate we should force 'teleport and reset' during the next call to UpdateClothState.
	 * This can be used to reset it from a bad state or by a teleport where the old state is not important anymore.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ForceClothNextUpdateTeleportAndReset();

	/** Stops simulating clothing, but does not show clothing ref pose. Keeps the last known simulation state */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction))
	void SuspendClothingSimulation();

	/** Resumes a previously suspended clothing simulation, teleporting the clothing on the next tick */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction))
	void ResumeClothingSimulation();

	/** Gets whether or not the clothing simulation is currently suspended */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	bool IsClothingSimulationSuspended() const;

	/**
	 * Reset the teleport mode of a next update to 'Continuous'
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ResetClothTeleportMode();

	/** 
	 * If this component has a valid MasterPoseComponent then this function makes cloth items on the slave component
	 * take the transforms of the cloth items on the master component instead of simulating separately.
	 * @Note This will FORCE any cloth actor on the master component to simulate in local space. Also
	 * The meshes used in the components must be identical for the cloth to bind correctly
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void BindClothToMasterPoseComponent();

	/** 
	 * If this component has a valid MasterPoseComponent and has previously had its cloth bound to the
	 * MCP, this function will unbind the cloth and resume simulation.
	 * @param bRestoreSimulationSpace if true and the master pose cloth was originally simulating in world
	 * space, we will restore this setting. This will cause the master component to reset which may be
	 * undesirable.
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(UnsafeDuringActorConstruction="true"))
	void UnbindClothFromMasterPoseComponent(bool bRestoreSimulationSpace = true);

	/**
	 * Sets whether or not to allow rigid body animation nodes for this component
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllowRigidBodyAnimNode(bool bInAllow, bool bReinitAnim = true);

	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	bool GetAllowRigidBodyAnimNode() const { return !bDisableRigidBodyAnimNode; }

	/**
	* Sets whether or not to force tick component in order to update animation and refresh transform for this component
	* This is supported only in the editor
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (DevelopmentOnly, UnsafeDuringActorConstruction = "true"))
	void SetUpdateAnimationInEditor(const bool NewUpdateState);

	/**
	* Sets whether or not to animate cloth in the editor. Requires Update Animation In Editor to also be true.
	* This is supported only in the editor
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (DevelopmentOnly, UnsafeDuringActorConstruction = "true"))
	void SetUpdateClothInEditor(const bool NewUpdateState);

#if WITH_EDITOR
	/**
	 * return true if currently updating in editor is true
	 * this is non BP because this is only used for slave component to detect master component ticking state
	 */
	bool GetUpdateAnimationInEditor() const 
	{		
		return bUpdateAnimationInEditor;	
	}

	bool GetUpdateClothInEditor() const
	{
		return bUpdateClothInEditor;
	}
#endif 

	UE_DEPRECATED(4.18, "This function is deprecated. Please use SetAllowAnimCurveEvaluation instead. Note that the meaning is reversed.")
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetDisableAnimCurves(bool bInDisableAnimCurves);

	UE_DEPRECATED(4.18, "This function is deprecated. Please use GetAllowedAnimCurveEvaluate instead. Note that the meaning is reversed.")
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	bool GetDisableAnimCurves() const { return !bAllowAnimCurveEvaluation; }

	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllowAnimCurveEvaluation(bool bInAllow);

	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	bool GetAllowedAnimCurveEvaluate() const { return bAllowAnimCurveEvaluation; }

	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void AllowAnimCurveEvaluation(FName NameOfCurve, bool bAllow);

	/** By reset, it will allow all the curves to be evaluated */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void ResetAllowedAnimCurveEvaluation();

	/** resets, and then only allow the following list to be allowed/disallowed */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllowedAnimCurvesEvaluation(const TArray<FName>& List, bool bAllow);

	const TArray<FName>& GetDisallowedAnimCurvesEvaluation() const { return DisallowedAnimCurves; }

	/** We detach the Component once we are done playing it.
	 *
	 * @param	ParticleSystemComponent that finished
	 */
	virtual void SkelMeshCompOnParticleSystemFinished( class UParticleSystemComponent* PSC );

	class UAnimSingleNodeInstance * GetSingleNodeInstance() const;
	bool InitializeAnimScriptInstance(bool bForceReinit = true, bool bInDeferRootNodeInitialization = false);

	/** @return true if wind is enabled */
	virtual bool IsWindEnabled() const;

#if WITH_EDITOR
	/**
	* Subclasses such as DebugSkelMeshComponent keep track of errors in the anim notifies so they can be displayed to the user. This function adds an error.
	* Errors are added uniquely and only removed when they're cleared by ClearAnimNotifyError.
	*/
	virtual void ReportAnimNotifyError(const FText& Error, UObject* InSourceNotify){}
	
	/**
	* Clears currently stored errors. Call before triggering anim notifies for a particular mesh.
	*/
	virtual void ClearAnimNotifyErrors(UObject* InSourceNotify){}
#endif

public:
	/** 
	 *	Index of the 'Root Body', or top body in the asset hierarchy. 
	 *	Filled in by InitInstance, so we don't need to save it.
	 */
	/** To save root body index/bone index consistently **/
	struct 
	{
		int32 BodyIndex;
		FTransform TransformToRoot;
	} RootBodyData;

	/** Set Root Body Index */
	void SetRootBodyIndex(int32 InBodyIndex);
	/** Reset Root Body Index */
	void ResetRootBodyIndex();

	/** Temporary array of bone indices required this frame. Filled in by UpdateSkelPose. */
	TArray<FBoneIndexType> RequiredBones;

	/** Temporary array of bone indices required to populate component space transforms */
	TArray<FBoneIndexType> FillComponentSpaceTransformsRequiredBones;

	/** Array of FBodyInstance objects, storing per-instance state about about each body. */
	TArray<struct FBodyInstance*> Bodies;

	/** Array of FConstraintInstance structs, storing per-instance state about each constraint. */
	TArray<struct FConstraintInstance*> Constraints;

	/** Physics-engine representation of aggregate which contains a physics asset instance with more than numbers of bodies. */
	FPhysicsAggregateHandle Aggregate;

	FSkeletalMeshComponentClothTickFunction ClothTickFunction;

	/**
	* Gets the teleportation rotation threshold.
	* 
	* @return Threshold in degrees.
	*/
	UFUNCTION(BlueprintGetter, Category=Clothing)
	float GetTeleportRotationThreshold() const;

	/**
	* Sets the teleportation rotation threshold.
	* 
	* @param threshold Threshold in degrees.
	*/
	UFUNCTION(BlueprintSetter, Category=Clothing)
	void SetTeleportRotationThreshold(float Threshold);

	/**
	* Gets the teleportation distance threshold.
	* 
	* @return Threshold value.
	*/
	UFUNCTION(BlueprintGetter, Category=Clothing)
	float GetTeleportDistanceThreshold() const;

	/**
	* Sets the teleportation distance threshold.
	* 
	* @param threshold Threshold value.
	*/
	UFUNCTION(BlueprintSetter, Category=Clothing)
	void SetTeleportDistanceThreshold(float Threshold);

private:
	/**
	* Conduct teleportation if the character's movement is greater than this threshold in 1 frame.
	* Zero or negative values will skip the check.
	* You can also do force teleport manually using ForceNextUpdateTeleport() / ForceNextUpdateTeleportAndReset().
	*/
	UPROPERTY(EditAnywhere, BlueprintGetter=GetTeleportDistanceThreshold, BlueprintSetter=SetTeleportDistanceThreshold, Category=Clothing)
	float TeleportDistanceThreshold;

	/**
	* Rotation threshold in degrees, ranging from 0 to 180.
	* Conduct teleportation if the character's rotation is greater than this threshold in 1 frame.
	* Zero or negative values will skip the check.
	*/
	UPROPERTY(EditAnywhere, BlueprintGetter=GetTeleportRotationThreshold, BlueprintSetter=SetTeleportRotationThreshold, Category=Clothing)
	float TeleportRotationThreshold;

	/** Used for pre-computation using TeleportRotationThreshold property */
	float ClothTeleportCosineThresholdInRad;
	/** Used for pre-computation using tTeleportDistanceThreshold property */
	float ClothTeleportDistThresholdSquared;

	void ComputeTeleportRotationThresholdInRadians();
	void ComputeTeleportDistanceThresholdInRadians();

	// Can't rely on time value, because those may be affected by dilation and whether or not
	// the game is paused.
	// Also can't just rely on a flag as other components (like CharacterMovementComponent) may tick
	// the pose and we can't guarantee tick order.
	UPROPERTY(Transient)
	uint32 LastPoseTickFrame;

public:

	/** Checked whether we have already ticked the pose this frame */
	bool PoseTickedThisFrame() const;

	bool IsClothBoundToMasterComponent() const { return bBindClothToMasterComponent; }

	/** Get the current clothing simulation (read only) */
	const IClothingSimulation* GetClothingSimulation() const;

	/** Get the current clothing simulation context (read only) */
	const IClothingSimulationContext* GetClothingSimulationContext() const;

	/** Get the current interactor for a clothing simulation, if the current simulation supports runtime interaction. */
	UFUNCTION(BlueprintCallable, Category=ClothingSimulation)
	UClothingSimulationInteractor* GetClothingSimulationInteractor() const;

	/** Callback when the parallel clothing task finishes, copies needed data back to component for gamethread */
	void CompleteParallelClothSimulation();

	/** Get the current simulation data map for the clothing on this component. For use on the game thread and only valid if bWaitForParallelClothTask is true. */
	const TMap<int32, FClothSimulData>& GetCurrentClothingData_GameThread() const;

	/** Get the current simulation data map for the clothing on this component. This will stall until the cloth simulation is complete. */
	const TMap<int32, FClothSimulData>& GetCurrentClothingData_AnyThread() const;

private:

	/** Let the cloth tick and completion tasks have access to private clothing data */
	friend FSkeletalMeshComponentClothTickFunction;
	friend class FParallelClothCompletionTask;

	/** Debug mesh component should be able to access for visualisation */
	friend class UDebugSkelMeshComponent;

	/** Copies the data from the external cloth simulation context. We copy instead of flipping because the API has to return the full struct to make backwards compat easy*/
	void UpdateClothSimulationContext(float InDeltaTime);

	/** previous root bone matrix to compare the difference and decide to do clothing teleport  */
	FMatrix	PrevRootBoneMatrix;

	/** 
	 * Clothing simulation objects.
	 * ClothingSimulation is responsible for maintaining and siulating clothing actors
	 * ClothingSimulationContext is a datastore for simulation data sent to the clothing thread
	 */
	IClothingSimulation* ClothingSimulation;
	IClothingSimulationContext* ClothingSimulationContext;

	/** 
	 * Object responsible for interacting with the clothing simulation.
	 * Blueprints and code can call/set data on this from the game thread and the next time
	 * it is safe to do so the interactor will sync to the simulation context
	 */
	UPROPERTY(Transient)
	UClothingSimulationInteractor* ClothingInteractor;

	/** Helper struct used to store info about a cloth collision source */
	struct FClothCollisionSource
	{
		FClothCollisionSource(USkeletalMeshComponent* InSourceComponent, UPhysicsAsset* InSourcePhysicsAsset)
			: SourceComponent(InSourceComponent)
			, SourcePhysicsAsset(InSourcePhysicsAsset)
			, bCached(false)
		{}

		/** Component that collision data will be copied from */
		TWeakObjectPtr<USkeletalMeshComponent> SourceComponent;

		/** Physics asset to use to generate collision against the source component */
		TWeakObjectPtr<UPhysicsAsset> SourcePhysicsAsset;

		/** Cached skeletal mesh used to invalidate the cache if the skeletal mesh has changed */
		TWeakObjectPtr<USkeletalMesh> CachedSkeletalMesh;

		/** Cached spheres from physics asset */
		TArray<FClothCollisionPrim_Sphere> CachedSpheres;

		/** Cached sphere connections from physics asset */
		TArray<FClothCollisionPrim_SphereConnection> CachedSphereConnections;

		/** Flag whether the cache is valid */
		bool bCached;
	};

	/** Array of sources for cloth collision */
	TArray<FClothCollisionSource> ClothCollisionSources;

	/** Ref for the clothing parallel task, so we can detect whether or not a sim is running */
	FGraphEventRef ParallelClothTask;

	/** Whether we should stall the Cloth tick task until the cloth simulation is complete. This is required if we want up-to-date
	 * cloth data on the game thread, for example if we want to generate particles at cloth vertices. When the data is not required
	 * except for rendering, we can set this to false to eliminate a potential game thread stall while we wait for the cloth sim 
	 */
	bool ShouldWaitForClothInTickFunction() const;

	/** Stalls on any currently running clothing simulations, needed when changing core sim state, or to access the clothing data */
	void HandleExistingParallelClothSimulation();

	/** Called by the clothing completion event to perform a writeback of the simulation data 
	 * to the game thread, the task is friended to gain access to this and not allow any
	 * external callers to trigger writebacks
	 */
	void WritebackClothingSimulationData();

	/** Gets the factory responsible for building the clothing simulation and simulation contexts */
	UClothingSimulationFactory* GetClothingSimFactory() const;

protected:

	/** Simulation data written back to the component after the simulation has taken place. If this data is required
	 * by any system other than rendering, bWaitForParallelClothTask must be true. If bWaitForParallelClothTask is false,
	 * this data cannot be read on the game thread, and there must be a call to HandleExistingParallelClothSimulation() prior
	 * to accessing it. 
	*/
	TMap<int32, FClothSimulData> CurrentSimulationData;

private:

	/** Wrapper that calls our constraint broken delegate */
	void OnConstraintBrokenWrapper(int32 ConstraintIndex);

	/** 
	 * Morph Target Curves. This will override AnimInstance MorphTargetCurves
	 * if same curve is found
	 **/
	TMap<FName, float>	MorphTargetCurves;

	/** 
	 * Temporary storage for Curve UIDList of evaluating Animation 
	 */
	TArray<uint16> CachedCurveUIDList;

public:
	const TMap<FName, float>& GetMorphTargetCurves() const { return MorphTargetCurves;  }
	// 
	// Animation
	//
	virtual void InitAnim(bool bForceReinit);
 
	// Broadcast when the components anim instance is initialized
	UPROPERTY(BlueprintAssignable, Category = Animation)
	FOnAnimInitialized OnAnimInitialized;

	/**
		If VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered
		Should we tick Montages only?
	*/
	bool ShouldOnlyTickMontages(const float DeltaTime) const;

	/** @return whether we should tick animation (we may want to skip it due to URO) */
	bool ShouldTickAnimation() const;

	/** Tick Animation system */
	void TickAnimation(float DeltaTime, bool bNeedsValidRootMotion);

	/** Tick Clothing Animation , basically this is called inside TickComponent */
	void TickClothing(float DeltaTime, FTickFunction& ThisTickFunction);

	/** Store cloth simulation data into OutClothSimData */
	void GetUpdateClothSimulationData(TMap<int32, FClothSimulData>& OutClothSimData, USkeletalMeshComponent* OverrideLocalRootComponent = nullptr);

	/** Remove clothing actors from their simulation */
	void RemoveAllClothingActors();

	/** Remove all clothing actors from their simulation and clear any other necessary clothing data to leave the simulations in a clean state */
	void ReleaseAllClothingResources();

	/**
	 * Draw the currently clothing state, using the editor extender interface
	 * @param PDI The draw interface to use
	 */
	void DebugDrawClothing(FPrimitiveDrawInterface* PDI);

	/**
	 * Draw the currently clothing state, using the editor extender interface
	 * @param Canvas The canvas to draw the text on
	 * @param SceneView The view to project the text with
	 */
	void DebugDrawClothingTexts(FCanvas* Canvas, const FSceneView* SceneView);

	/** Changes the value of bNotifyRigidBodyCollision
	* @param bNewNotifyRigidBodyCollision - The value to assign to bNotifyRigidBodyCollision
	*/
	virtual void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision) override;

	/** Changes the value of bNotifyRigidBodyCollision for a given body
	* @param bNewNotifyRigidBodyCollision	The value to assign to bNotifyRigidBodyCollision
	* @param BoneName						Name of the body to turn hit notifies on/off. None implies root body
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void SetBodyNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision, FName BoneName = NAME_None);

	/** Changes the value of bNotifyRigidBodyCollision on all bodies below a given bone
	* @param bNewNotifyRigidBodyCollision	The value to assign to bNotifyRigidBodyCollision
	* @param BoneName						Name of the body to turn hit notifies on (and below)
	* @param bIncludeSelf					Whether to modify the given body (useful for roots with multiple children)
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void SetNotifyRigidBodyCollisionBelow(bool bNewNotifyRigidBodyCollision, FName BoneName = NAME_None, bool bIncludeSelf = true);

	/** 
	 * Recalculates the RequiredBones array in this SkeletalMeshComponent based on current SkeletalMesh, LOD and PhysicsAsset.
	 * Is called when bRequiredBonesUpToDate = false
	 *
	 * @param	LODIndex	Index of LOD [0-(MaxLOD-1)]
	 */
	void RecalcRequiredBones(int32 LODIndex);

	/** Computes the required bones in this SkeletalMeshComponent based on current SkeletalMesh, LOD and PhysicsAsset
	  * @param	LODIndex	Index of LOD [0-(MaxLOD-1)]
	*/
	void ComputeRequiredBones(TArray<FBoneIndexType>& OutRequiredBones, TArray<FBoneIndexType>& OutFillComponentSpaceTransformsRequiredBones, int32 LODIndex, bool bIgnorePhysicsAsset) const;

	/**
	* Recalculates the AnimCurveUids array in RequiredBone of this SkeletalMeshComponent based on current required bone set
	* Is called when Skeleton->IsRequiredCurvesUpToDate() = false
	*/
	void RecalcRequiredCurves();

public:
	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnSkeletalMeshPropertyChangedMulticaster)
	FOnSkeletalMeshPropertyChangedMulticaster OnSkeletalMeshPropertyChanged;
	typedef FOnSkeletalMeshPropertyChangedMulticaster::FDelegate FOnSkeletalMeshPropertyChanged;

	/** Register / Unregister delegates called when the skeletal mesh property is changed */
	FDelegateHandle RegisterOnSkeletalMeshPropertyChanged(const FOnSkeletalMeshPropertyChanged& Delegate);
	void UnregisterOnSkeletalMeshPropertyChanged(FDelegateHandle Handle);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Validates the animation asset or blueprint, making sure it is compatible with the current skeleton */
	void ValidateAnimation();

	virtual void LoadedFromAnotherClass(const FName& OldClassName) override;
	virtual void UpdateCollisionProfile() override;
#endif // WITH_EDITOR
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~ End UObject Interface.

	//~ Begin UActorComponent Interface.
protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
public:
	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	//Handle registering our end physics tick function
	virtual void RegisterEndPhysicsTick(bool bRegister);

	virtual bool RequiresPreEndOfFrameSync() const override;
	virtual void OnPreEndOfFrameSync() override;

	//Handle registering our pre cloth tick function
	void RegisterClothTick(bool bRegister);

	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool IsAnySimulatingPhysics() const override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual bool UpdateOverlapsImpl(const TOverlapArrayView* PendingOverlaps=NULL, bool bDoNotifies=true, const TOverlapArrayView* OverlapsAtEndLocation=NULL) override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface.
protected:
	/**
	 *  Test the collision of the supplied component at the supplied location/rotation, and determine the set of components that it overlaps
	 *  @param  OutOverlaps     Array of overlaps found between this component in specified pose and the world
	 *  @param  World			World to use for overlap test
	 *  @param  Pos             Location of the component's geometry for the test against the world
	 *  @param  Rot             Rotation of the component's geometry for the test against the world
	 *  @param  TestChannel		The 'channel' that this ray is in, used to determine which components to hit
	 *	@param	ObjectQueryParams	List of object types it's looking for. When this enters, we do object query with component shape
	 *  @return TRUE if OutOverlaps contains any blocking results
	 */
	virtual bool ComponentOverlapMultiImpl(TArray<struct FOverlapResult>& OutOverlaps, const class UWorld* InWorld, const FVector& Pos, const FQuat& Rot, ECollisionChannel TestChannel, const struct FComponentQueryParams& Params, const struct FCollisionObjectQueryParams& ObjectQueryParams = FCollisionObjectQueryParams::DefaultObjectQueryParam) const override;
	
	virtual bool ComponentOverlapComponentImpl(class UPrimitiveComponent* PrimComp, const FVector Pos, const FQuat& Quat, const FCollisionQueryParams& Params) override;

	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;

public:

	virtual class UBodySetup* GetBodySetup() override;
	virtual bool CanEditSimulatePhysics() override;
	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None, bool bGetWelded = true) const override;
	virtual void UpdatePhysicsToRBChannels() override;
	virtual void SetAllPhysicsAngularVelocityInRadians(FVector const& NewVel, bool bAddToCurrent = false) override;
	virtual void SetAllPhysicsPosition(FVector NewPos) override;
	virtual void SetAllPhysicsRotation(FRotator NewRot) override;
	virtual void SetAllPhysicsRotation(const FQuat& NewRot) override;
	virtual void WakeAllRigidBodies() override;
	virtual void PutAllRigidBodiesToSleep() override;
	virtual bool IsAnyRigidBodyAwake() override;
	virtual void SetEnableGravity(bool bGravityEnabled);
	virtual bool IsGravityEnabled() const override;
	virtual void OnComponentCollisionSettingsChanged(bool bUpdateOverlaps=true) override;
	virtual void SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial) override;
	virtual bool GetSquaredDistanceToCollision(const FVector& Point, float& OutSquaredDistance, FVector& OutClosestPointOnCollision) const override;

	/**
	 *	Enables or disables gravity for the given bone.
	 *	NAME_None indicates the root body will be edited.
	 *	If the bone name given is otherwise invalid, nothing happens.
	 *
	 *	@param bEnableGravity	Whether gravity should be enabled or disabled.
	 *	@param BoneName			The name of the bone to modify.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics|Components")
	void SetEnableBodyGravity(bool bEnableGravity, FName BoneName);

	/**
	 *	Checks whether or not gravity is enabled on the given bone.
	 *	NAME_None indicates the root body should be queried.
	 *	If the bone name given is otherwise invalid, false is returned.
	 *
	 *	@param BoneName	The name of the bone to check.
	 *	@return True if gravity is enabled on the bone.
	 */
	UFUNCTION(BlueprintCallable, Category="Physics|Components")
	bool IsBodyGravityEnabled(FName BoneName);

	/**
	*	Enables or disables gravity to all bodies below the given bone.
	*   NAME_None indicates all bodies will be edited.
	*	In that case, consider using UPrimitiveComponent::EnableGravity.
	*
	*	@param bEnableGravity	Whether gravity should be enabled or disabled.
	*	@param BoneName			The name of the top most bone.
	*	@param bIncludeSelf		Whether the bone specified should be edited.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components")
	void SetEnableGravityOnAllBodiesBelow(bool bEnableGravity, FName BoneName, bool bIncludeSelf=true);

	/** 
	 *	Given a world position, find the closest point on the physics asset. Note that this is independent of collision and welding. This is based purely on animation position
	 *  @param	WorldPosition				The point we want the closest point to (i.e. for all bodies in the physics asset, find the one that has a point closest to WorldPosition)
	 *  @param	ClosestPointOnPhysicsAsset	The data associated with the closest point (position, normal, etc...)
	 *  @param	bApproximate				The closest body is found using bone transform distance instead of body distance. This approximation means the final point is the closest point on a potentially not closest body. This approximation gets worse as the size of Bodies gets bigger.
	 *  @return	true if we found a closest point
	 */
	bool GetClosestPointOnPhysicsAsset(const FVector& WorldPosition, FClosestPointOnPhysicsAsset& ClosestPointOnPhysicsAsset, bool bApproximate) const;

	/** 
	 *	Given a world position, find the closest point on the physics asset. Note that this is independent of collision and welding. This is based purely on animation position
	 *  @param	WorldPosition				The point we want the closest point to (i.e. for all bodies in the physics asset, find the one that has a point closest to WorldPosition)
	 *  @param	ClosestPointOnPhysicsAsset	The data associated with the closest point (position, normal, etc...)
	 *  @return	true if we found a closest point
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh", meta=(DisplayName="GetClosestPointOnPhysicsAsset", ScriptName="GetClosestPointOnPhysicsAsset", Keywords="closest point"))
	bool K2_GetClosestPointOnPhysicsAsset(const FVector& WorldPosition, FVector& ClosestWorldPosition, FVector& Normal, FName& BoneName, float& Distance) const;

	virtual bool LineTraceComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params ) override;
	
	/** 
	 *  Trace a shape against just this component. Will trace against each body, returning as soon as any collision is found. Note that this collision may not be the closest.
	 *  @param  OutHit          	Information about hit against this component, if true is returned
	 *  @param  Start           	Start location of the trace
	 *  @param  End             	End location of the trace
	 *  @param  ShapeWorldRotation  The rotation applied to the collision shape in world space
	 *  @param  CollisionShape  	Collision Shape
	 *	@param	bTraceComplex	Whether or not to trace complex
	 *  @return true if a hit is found
	 */
	 virtual bool SweepComponent( FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapRotation, const FCollisionShape& CollisionShape, bool bTraceComplex=false) override;
	
	virtual bool OverlapComponent(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape) override;
	virtual void SetSimulatePhysics(bool bEnabled) override;
	virtual void AddRadialImpulse(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bVelChange=false) override;
	virtual void AddRadialForce(FVector Origin, float Radius, float Strength, ERadialImpulseFalloff Falloff, bool bAccelChange=false) override;
	virtual void SetAllPhysicsLinearVelocity(FVector NewVel,bool bAddToCurrent = false) override;
	virtual void SetAllMassScale(float InMassScale = 1.f) override;
	virtual float GetMass() const override;
	virtual void SetAllUseCCD(bool InUseCCD) override;

	/**
	*	Returns the mass (in kg) of the given bone
	*
	*	@param BoneName		Name of the body to return. 'None' indicates root body.
	*	@param bScaleMass	If true, the mass is scaled by the bone's MassScale.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	float GetBoneMass(FName BoneName = NAME_None, bool bScaleMass = true) const;

	/**
	*	Returns the center of mass of the skeletal mesh, instead of the root body's location
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	FVector GetSkeletalCenterOfMass() const;


	virtual float CalculateMass(FName BoneName = NAME_None) override;
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;

	/**
	*	Add a force to all rigid bodies below.
	*   This is like a 'thruster'. Good for adding a burst over some (non zero) time. Should be called every frame for the duration of the force.
	*
	*	@param	Force		 Force vector to apply. Magnitude indicates strength of force.
	*	@param	BoneName	 If a SkeletalMeshComponent, name of body to apply force to. 'None' indicates root body.
	*   @param  bAccelChange If true, Force is taken as a change in acceleration instead of a physical force (i.e. mass will have no effect).
	*   @param  bIncludeSelf If false, Force is only applied to bodies below but not given bone name.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void AddForceToAllBodiesBelow(FVector Force, FName BoneName = NAME_None, bool bAccelChange = false, bool bIncludeSelf = true);

	/**
	*	Add impulse to all single rigid bodies below. Good for one time instant burst.
	*
	*	@param	Impulse		Magnitude and direction of impulse to apply.
	*	@param	BoneName	If a SkeletalMeshComponent, name of body to apply impulse to. 'None' indicates root body.
	*	@param	bVelChange	If true, the Strength is taken as a change in velocity instead of an impulse (ie. mass will have no effect).
	*	@param bIncludeSelf If false, Force is only applied to bodies below but not given bone name.
	*/
	UFUNCTION(BlueprintCallable, Category = "Physics")
	virtual void AddImpulseToAllBodiesBelow(FVector Impulse, FName BoneName = NAME_None, bool bVelChange = false, bool bIncludeSelf = true);

#if WITH_EDITOR
	virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
#endif
protected:
	virtual FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI) override;
	//~ End UPrimitiveComponent Interface.

public:
	//~ Begin USkinnedMeshComponent Interface
	virtual bool UpdateLODStatus() override;
	virtual void UpdateVisualizeLODString(FString& DebugString) override;
	virtual void RefreshBoneTransforms( FActorComponentTickFunction* TickFunction = NULL ) override;
protected:
	virtual void DispatchParallelTickPose( FActorComponentTickFunction* TickFunction ) override;
public:
	virtual void TickPose(float DeltaTime, bool bNeedsValidRootMotion) override;
	virtual void UpdateSlaveComponent() override;
	virtual bool ShouldUpdateTransform(bool bLODHasChanged) const override;
	virtual bool ShouldTickPose() const override;
	virtual bool AllocateTransformData() override;
	virtual void DeallocateTransformData() override;
	virtual void HideBone( int32 BoneIndex, EPhysBodyOp PhysBodyOption ) override;
	virtual void UnHideBone( int32 BoneIndex ) override;
	virtual void SetPhysicsAsset(class UPhysicsAsset* NewPhysicsAsset,bool bForceReInit = false) override;
	virtual void SetSkeletalMesh(class USkeletalMesh* NewMesh, bool bReinitPose = true) override;

	static FVector GetSkinnedVertexPosition(USkeletalMeshComponent* Component, int32 VertexIndex, const FSkeletalMeshLODRenderData& Model, const FSkinWeightVertexBuffer& SkinWeightBuffer);
	static FVector GetSkinnedVertexPosition(USkeletalMeshComponent* Component, int32 VertexIndex, const FSkeletalMeshLODRenderData& Model, const FSkinWeightVertexBuffer& SkinWeightBuffer, TArray<FMatrix>& CachedRefToLocals);
	static void ComputeSkinnedPositions(USkeletalMeshComponent* Component, TArray<FVector> & OutPositions, TArray<FMatrix>& CachedRefToLocals, const FSkeletalMeshLODRenderData& Model, const FSkinWeightVertexBuffer& SkinWeightBuffer);

	static void GetSkinnedTangentBasis(USkeletalMeshComponent* Component, int32 VertexIndex, const FSkeletalMeshLODRenderData& Model, const FSkinWeightVertexBuffer& SkinWeightBuffer, TArray<FMatrix>& CachedRefToLocals, FVector& OutTangentX, FVector& OutTangentZ);
	static void ComputeSkinnedTangentBasis(USkeletalMeshComponent* Component, TArray<FVector>& OutTangenXZ, TArray<FMatrix>& CachedRefToLocals, const FSkeletalMeshLODRenderData& Model, const FSkinWeightVertexBuffer& SkinWeightBuffer);

	void SetSkeletalMeshWithoutResettingAnimation(class USkeletalMesh* NewMesh);

	virtual bool IsPlayingRootMotion() const override;
	virtual bool IsPlayingNetworkedRootMotionMontage() const override;
	virtual bool IsPlayingRootMotionFromEverything() const override;
	virtual void FinalizeBoneTransform() override;
	virtual void SetRefPoseOverride(const TArray<FTransform>& NewRefPoseTransforms) override;
	virtual void ClearRefPoseOverride() override;
	//~ End USkinnedMeshComponent Interface

	FOnBoneTransformsFinalized OnBoneTransformsFinalized;

	void GetCurrentRefToLocalMatrices(TArray<FMatrix>& OutRefToLocals, int32 InLodIdx) const;

	// Conditions used to gate when post process events happen
	bool ShouldUpdatePostProcessInstance() const;
	bool ShouldPostUpdatePostProcessInstance() const;
	bool ShouldEvaluatePostProcessInstance() const;

	/** 
	 *	Iterate over each joint in the physics for this mesh, setting its AngularPositionTarget based on the animation information.
	 */
	void UpdateRBJointMotors();

	/**
	* Runs the animation evaluation for the current pose into the supplied variables
	* PerformAnimationProcessing runs evaluation based on bInDoEvaluation. PerformAnimationEvaluation 
	* always runs evaluation (and exists for backward compatibility)
	*
	* @param	InSkeletalMesh			The skeletal mesh we are animating
	* @param	InAnimInstance			The anim instance we are evaluating
	* @param	bInDoEvaluation			Whether to perform evaluation (we may just want to update)
	* @param	OutSpaceBases			Component space bone transforms
	* @param	OutBoneSpaceTransforms	Local space bone transforms
	* @param	OutRootBoneTranslation	Calculated root bone translation
	* @param	OutCurves				Blended Curve
	*/
#if WITH_EDITOR
	void PerformAnimationEvaluation(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, TArray<FTransform>& OutSpaceBases, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve, FHeapCustomAttributes& OutAttributes);

	UE_DEPRECATED(4.26, "Please use PerformAnimationEvaluation with different signature")
	void PerformAnimationEvaluation(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, TArray<FTransform>& OutSpaceBases, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve);
#endif
	void PerformAnimationProcessing(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, bool bInDoEvaluation, TArray<FTransform>& OutSpaceBases, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve, FHeapCustomAttributes& OutAttributes);

	UE_DEPRECATED(4.26, "Please use PerformAnimationEvaluation with different signature")
	void PerformAnimationProcessing(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, bool bInDoEvaluation, TArray<FTransform>& OutSpaceBases, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve);

	/**
	 * Evaluates the post process instance from the skeletal mesh this component is using.
	 */
	void EvaluatePostProcessMeshInstance(TArray<FTransform>& OutBoneSpaceTransforms, FCompactPose& InOutPose, FBlendedHeapCurve& OutCurve, const USkeletalMesh* InSkeletalMesh, FVector& OutRootBoneTranslation, FHeapCustomAttributes& OutAttributes) const;

	UE_DEPRECATED(4.26, "Please use EvaluatePostProcessMeshInstance with different signature")
	void EvaluatePostProcessMeshInstance(TArray<FTransform>& OutBoneSpaceTransforms, FCompactPose& InOutPose, FBlendedHeapCurve& OutCurve, const USkeletalMesh* InSkeletalMesh, FVector& OutRootBoneTranslation) const;

	void PostAnimEvaluation(FAnimationEvaluationContext& EvaluationContext);

	/** */
	void InitCollisionRelationships();

	/** */
	void TermCollisionRelationships();

	/**
	 * Blend of Physics Bones with PhysicsWeight and Animated Bones with (1-PhysicsWeight)
	 *
	 * @param	RequiredBones	List of bones to be blend
	 */
	UE_DEPRECATED(4.26, "This function is deprecated and should not be called directly. Please use the mechanism provided in USkeletalMeshComponent::EndPhysicsTickComponent")
	void BlendPhysicsBones( TArray<FBoneIndexType>& Bones )
	{
		PerformBlendPhysicsBones(Bones, AnimEvaluationContext.BoneSpaceTransforms, AnimEvaluationContext.BoneSpaceTransforms);
	}


	/** Take the results of the physics and blend them with the animation state (based on the PhysicsWeight parameter), and update the SpaceBases array. */
	UE_DEPRECATED(4.26, "Public access to this function is deprecated. Please use the mechanism provided in USkeletalMeshComponent::EndPhysicsTickComponent")
	void BlendInPhysics(FTickFunction& ThisTickFunction) { BlendInPhysicsInternal(ThisTickFunction); }

	/** 
	 * Initialize PhysicsAssetInstance for the physicsAsset 
	 * 
	 * @param	PhysScene	Physics Scene
	 */
	void InitArticulated(FPhysScene* PhysScene);

	/** Instantiates bodies given a physics asset. Typically you should call InitArticulated unless you are planning to do something special with the bodies. The Created bodies and constraints are owned by the calling code and must be freed when necessary.*/
	void InstantiatePhysicsAsset(const UPhysicsAsset& PhysAsset, const FVector& Scale3D, TArray<FBodyInstance*>& OutBodies, TArray<FConstraintInstance*>& OutConstraints, FPhysScene* PhysScene = nullptr, USkeletalMeshComponent* OwningComponent = nullptr, int32 UseRootBodyIndex = INDEX_NONE, const FPhysicsAggregateHandle& UseAggregate = FPhysicsAggregateHandle()) const;

	/** Instantiates bodies given a physics asset like InstantiatePhysicsAsset but instead of reading the current component state, this reads the ref-pose from the reference skeleton of the mesh. Useful if trying to create bodies to be used during any evaluation work */
	void InstantiatePhysicsAssetRefPose(const UPhysicsAsset& PhysAsset, const FVector& Scale3D, TArray<FBodyInstance*>& OutBodies, TArray<FConstraintInstance*>& OutConstraints, FPhysScene* PhysScene = nullptr, USkeletalMeshComponent* OwningComponent = nullptr, int32 UseRootBodyIndex = INDEX_NONE, const FPhysicsAggregateHandle& UseAggregate = FPhysicsAggregateHandle(), bool bCreateBodiesInRefPose = false) const;

	/** Turn off all physics and remove the instance. */
	void TermArticulated();

	/** Find the root body index*/
	int32 FindRootBodyIndex() const;


	/** Terminate physics on all bodies below the named bone, effectively disabling collision forever. If you terminate, you won't be able to re-init later. */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void TermBodiesBelow(FName ParentBoneName);

	/** Find instance of the constraint that matches the name supplied. */
	FConstraintInstance* FindConstraintInstance(FName ConName);

	/** Utility which returns total mass of all bones below the supplied one in the hierarchy (including this one). */
	float GetTotalMassBelowBone(FName InBoneName);

	/** Set the collision object type on the skeletal mesh */
	virtual void SetCollisionObjectType(ECollisionChannel Channel) override;

	/** Set the movement channel of all bodies */
	void SetAllBodiesCollisionObjectType(ECollisionChannel NewChannel);

	/** Set the rigid body notification state for all bodies. */
	void SetAllBodiesNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision);

	/** Set bSimulatePhysics to true for all bone bodies. Does not change the component bSimulatePhysics flag. */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesSimulatePhysics(bool bNewSimulate);

	/** This is global set up for setting physics blend weight
	 * This does multiple things automatically
	 * If PhysicsBlendWeight == 1.f, it will enable Simulation, and if PhysicsBlendWeight == 0.f, it will disable Simulation. 
	 * Also it will respect each body's setup, so if the body is fixed, it won't simulate. Vice versa
	 * So if you'd like all bodies to change manually, do not use this function, but SetAllBodiesPhysicsBlendWeight
	 */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetPhysicsBlendWeight(float PhysicsBlendWeight);

	/** Disable physics blending of bones **/
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetEnablePhysicsBlending(bool bNewBlendPhysics);

	/** Set all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesBelowSimulatePhysics(const FName& InBoneName, bool bNewSimulate, bool bIncludeSelf = true );

	/** Allows you to reset bodies Simulate state based on where bUsePhysics is set to true in the BodySetup. */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void ResetAllBodiesSimulatePhysics();

	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesPhysicsBlendWeight(float PhysicsBlendWeight, bool bSkipCustomPhysicsType = false );

	/** Set all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void SetAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float PhysicsBlendWeight, bool bSkipCustomPhysicsType = false, bool bIncludeSelf = true );

	/** Accumulate AddPhysicsBlendWeight to physics blendweight for all of the bones below passed in bone to be simulated */
	UFUNCTION(BlueprintCallable, Category="Components|SkeletalMesh")
	void AccumulateAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float AddPhysicsBlendWeight, bool bSkipCustomPhysicsType = false );

	/** Enable or Disable AngularPositionDrive. If motor is in SLERP mode it will be turned on if either EnableSwingDrive OR EnableTwistDrive are enabled. In Twist and Swing mode the twist and the swing can be controlled individually.*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularPositionDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType = false);

	/** Enable or Disable AngularVelocityDrive. If motor is in SLERP mode it will be turned on if either EnableSwingDrive OR EnableTwistDrive are enabled. In Twist and Swing mode the twist and the swing can be controlled individually.*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularVelocityDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType = false);

	/** Set Angular Drive motors params for all constraint instances */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void SetAllMotorsAngularDriveParams(float InSpring, float InDamping, float InForceLimit, bool bSkipCustomPhysicsType = false);

	/** Sets the constraint profile properties (limits, motors, etc...) to match the constraint profile as defined in the physics asset. If profile name is not found the joint is set to use the default constraint profile.*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	void SetConstraintProfile(FName JointName, FName ProfileName, bool bDefaultIfNotFound = false);

	/** Sets the constraint profile properties (limits, motors, etc...) to match the constraint profile as defined in the physics asset for all constraints. If profile name is not found the joint is set to use the default constraint profile.*/
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	void SetConstraintProfileForAll(FName ProfileName, bool bDefaultIfNotFound = false);

	/** Enable or Disable AngularPositionDrive based on a list of bone names */
	void SetNamedMotorsAngularPositionDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, const TArray<FName>& BoneNames, bool bSetOtherBodiesToComplement = false);

	/** Enable or Disable AngularVelocityDrive based on a list of bone names */
	void SetNamedMotorsAngularVelocityDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, const TArray<FName>& BoneNames, bool bSetOtherBodiesToComplement = false);

	void GetWeldedBodies(TArray<FBodyInstance*> & OutWeldedBodies, TArray<FName> & OutChildrenLabels, bool bIncludingAutoWeld = false) override;

	/** Iterates over all bodies below and executes Func. Returns number of bodies found */
	int32 ForEachBodyBelow(FName BoneName, bool bIncludeSelf, bool bSkipCustomType, TFunctionRef<void(FBodyInstance*)> Func);

	/** 
	 * Change whether to force mesh into ref pose (and use cheaper vertex shader) 
	 *
	 * @param	bNewForceRefPose	true if it would like to force ref pose
	 */
	void SetForceRefPose(bool bNewForceRefPose);
	
	/** Update bHasValidBodies flag */
	void UpdateHasValidBodies();

	/** Update the bone mapping on each body instance. This is useful when changing skeletal mesh without recreating bodies */
	void UpdateBoneBodyMapping();
	
	/** 
	 * Initialize SkelControls
	 */
	void InitSkelControls();
	
	/**
	 * Find Constraint Index from the name
	 * 
	 * @param	ConstraintName	Joint Name of constraint to look for
	 * @return	Constraint Index
	 */
	int32	FindConstraintIndex(FName ConstraintName);
	
	/**
	 * Find Constraint Name from index
	 * 
	 * @param	ConstraintIndex	Index of constraint to look for
	 * @return	Constraint Joint Name
	 */
	UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
	FName	FindConstraintBoneName(int32 ConstraintIndex);

	/** 
	 *	Iterate over each physics body in the physics for this mesh, and for each 'kinematic' (ie fixed or default if owner isn't simulating) one, update its
	 *	transform based on the animated transform.
	 *	@param	InComponentSpaceTransforms	Array of bone transforms in component space
	 *	@param	Teleport					Whether movement is a 'teleport' (ie infers no physics velocity, but moves simulating bodies) or not
	 *	@param	bNeedsSkinning				Whether we may need  to send new triangle data for per-poly skeletal mesh collision
	 *	@param	AllowDeferral				Whether we can defer actual update of bodies (if 'physics only' collision)
	 */
	void UpdateKinematicBonesToAnim(const TArray<FTransform>& InComponentSpaceTransforms, ETeleportType Teleport, bool bNeedsSkinning, EAllowKinematicDeferral DeferralAllowed = EAllowKinematicDeferral::AllowDeferral);

	/**
	 * Look up all bodies for broken constraints.
	 * Makes sure child bodies of a broken constraints are not fixed and using bone springs, and child joints not motorized.
	 */
	void UpdateMeshForBrokenConstraints();
	
	/**
	 * Notifier when look at control goes beyond of limit - candidate for delegate
	 */
	virtual void NotifySkelControlBeyondLimit(class USkelControlLookAt* LookAt);

	/** 
	 * Break a constraint off a Gore mesh. 
	 * 
	 * @param	Impulse	vector of impulse
	 * @param	HitLocation	location of the hit
	 * @param	InBoneName	Name of bone to break constraint for
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh", meta = (Keywords = "Constraint"))
	void BreakConstraint(FVector Impulse, FVector HitLocation, FName InBoneName);

	/** Sets the Angular Motion Ranges for a named bone
	*  @param InBoneName  Name of bone to adjust constraint ranges for
	*  @param Swing1LimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*  @param TwistLimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*  @param Swing2LimitAngle	 Size of limit in degrees, 0 means locked, 180 means free
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void  SetAngularLimits(FName InBoneName,float Swing1LimitAngle, float TwistLimitAngle, float Swing2LimitAngle);

	/** Gets the current Angular state for a named bone constraint 
	*  @param InBoneName  Name of bone to get constraint ranges for
	*  @param Swing1Angle current angular state of the constraint
	*  @param TwistAngle  current angular state of the constraint
	*  @param Swing2Angle current angular state of the constraint
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|SkeletalMesh")
	void GetCurrentJointAngles(FName InBoneName,float& Swing1Angle, float& TwistAngle, float& Swing2Angle) ;


	/** iterates through all bodies in our PhysicsAsset and returns the location of the closest bone associated
	 * with a body that has collision enabled.
	 * @param TestLocation - location to check against
	 * @return location of closest colliding rigidbody, or TestLocation if there were no bodies to test
	 */
	FVector GetClosestCollidingRigidBodyLocation(const FVector& TestLocation) const;
	
	/** Set physics transforms for all bodies */
	void ApplyDeltaToAllPhysicsTransforms(const FVector& DeltaLocation, const FQuat& DeltaRotation);

	/** Destroys and recreates the clothing actors in the current simulation */
	void RecreateClothingActors();

	/** Given bounds InOutBounds, expand them to also enclose the clothing simulation mesh */
	void AddClothingBounds(FBoxSphereBounds& InOutBounds, const FTransform& LocalToWorld) const;

	/** Check linear and angular thresholds for clothing teleport */
	virtual void CheckClothTeleport();

	/** Update the clothing simulation state and trigger the simulation task */
	void UpdateClothStateAndSimulate(float DeltaTime, FTickFunction& ThisTickFunction);
	
	/** 
	 * Updates cloth collision outside the cloth asset (environment collision, child collision, etc...)
	 * Should be called when scene changes or world position changes
	 */
	void UpdateClothTransform(ETeleportType TeleportType);

	/**
	 * Updates cloth collision inside the cloth asset (from a physics asset).
	 * Should be called when the physics asset changes and the effects are needed straight away.
	 */
	void UpdateClothCollision();

	/** if the vertex index is valid for simulated vertices, returns the position in world space */
	bool GetClothSimulatedPosition_GameThread(const FGuid& AssetGuid, int32 VertexIndex, FVector& OutSimulPos) const;

#if WITH_CLOTH_COLLISION_DETECTION

	/**
	 * Add a collision source for the cloth on this component.
	 * Each cloth tick, the collision defined by the physics asset, transformed by the bones in the source
	 * component, will be applied to cloth.
	 * @param	InSourceComponent		The component to extract collision transforms from
	 * @param	InSourcePhysicsAsset	The physics asset that defines the collision primitives (that will be transformed by InSourceComponent's bones)
	 */
	void AddClothCollisionSource(USkeletalMeshComponent* InSourceComponent, UPhysicsAsset* InSourcePhysicsAsset);

	/** Remove a cloth collision source defined by a component */
	void RemoveClothCollisionSource(USkeletalMeshComponent* InSourceComponent);

	/** Remove a cloth collision source defined by both a component and a physics asset */
	void RemoveClothCollisionSource(USkeletalMeshComponent* InSourceComponent, UPhysicsAsset* InSourcePhysicsAsset);

protected:
	/** copy cloth collision sources to this, where parent means components above it in the hierarchy */
	void CopyClothCollisionSources();

	void ProcessClothCollisionWithEnvironment();

	/** copy parent's cloth collisions to attached children, where parent means this component */
	void CopyClothCollisionsToChildren();

	/** copy children's cloth collisions to parent, where parent means this component */
	void CopyChildrenClothCollisionsToParent();

	/** find if this component has collisions for clothing and return the results calculated by bone transforms */
	void FindClothCollisions(FClothCollisionData& OutCollisions);

#else

public:
	/** Stub out these public functions if cloth collision is disabled */
	void AddClothCollisionSource(USkeletalMeshComponent* InSourceComponent, UPhysicsAsset* InSourcePhysicsAsset) {}
	void RemoveClothCollisionSource(USkeletalMeshComponent* InSourceComponent) {}
	void RemoveClothCollisionSource(USkeletalMeshComponent* InSourceComponent, UPhysicsAsset* InSourcePhysicsAsset) {}

#endif

public:
	bool IsAnimBlueprintInstanced() const;
	void ClearAnimScriptInstance();

	/** Clear cached animation data generated for URO during evaluation */
	void ClearCachedAnimProperties();

protected:
	bool NeedToSpawnAnimScriptInstance() const;
	bool NeedToSpawnPostPhysicsInstance(bool bForceReinit) const;

	bool ShouldBlendPhysicsBones() const;

	/** Extract collisions for cloth from this component (given a component we want to apply the data to) */
	static void ExtractCollisionsForCloth(USkeletalMeshComponent* SourceComponent,  UPhysicsAsset* PhysicsAsset, USkeletalMeshComponent* DestClothComponent, FClothCollisionData& OutCollisions, FClothCollisionSource& ClothCollisionSource);

	/** Notify called just before syncing physics update, called only if bNotifySyncComponentToRBPhysics flag is set */
	virtual void OnSyncComponentToRBPhysics() { }

	FSkeletalMeshComponentEndPhysicsTickFunction EndPhysicsTickFunction;

private:

	friend struct FSkeletalMeshComponentEndPhysicsTickFunction;

	/** Update systems after physics sim is done */
	void EndPhysicsTickComponent(FSkeletalMeshComponentEndPhysicsTickFunction& ThisTickFunction);

	/** Evaluate Anim System **/
	void EvaluateAnimation(const USkeletalMesh* InSkeletalMesh, UAnimInstance* InAnimInstance, FVector& OutRootBoneTranslation, FBlendedHeapCurve& OutCurve, FCompactPose& OutPose, FHeapCustomAttributes& OutAttributes) const;

	/** Queues up tasks for parallel update/evaluation, as well as the chained game thread completion task */
	void DispatchParallelEvaluationTasks(FActorComponentTickFunction* TickFunction);

	/** Performs parallel eval/update work, but on the game thread */
	void DoParallelEvaluationTasks_OnGameThread();

	/** Swaps buffers into the evaluation context before and after task dispatch */
	void SwapEvaluationContextBuffers();

	/** Duplicates cached transforms/curves and performs interpolation */
	void ParallelDuplicateAndInterpolate(FAnimationEvaluationContext& InAnimEvaluationContext);

	/**
	* Take the BoneSpaceTransforms array (translation vector, rotation quaternion and scale vector) and update the array of component-space bone transformation matrices (ComponentSpaceTransforms).
	* It will work down hierarchy multiplying the component-space transform of the parent by the relative transform of the child.
	* This code also applies any per-bone rotators etc. as part of the composition process
	*/
	void FillComponentSpaceTransforms(const USkeletalMesh* InSkeletalMesh, const TArray<FTransform>& InBoneSpaceTransforms, TArray<FTransform>& OutComponentSpaceTransforms) const;

	bool DoAnyPhysicsBodiesHaveWeight() const;

	virtual void RefreshMorphTargets() override;

	void GetWindForCloth_GameThread(FVector& WindVector, float& WindAdaption) const;

	void InstantiatePhysicsAsset_Internal(const UPhysicsAsset& PhysAsset, const FVector& Scale3D, TArray<FBodyInstance*>& OutBodies, TArray<FConstraintInstance*>& OutConstraints, TFunctionRef<FTransform(int32)> BoneTransformGetter, FPhysScene* PhysScene = nullptr, USkeletalMeshComponent* OwningComponent = nullptr, int32 UseRootBodyIndex = INDEX_NONE, const FPhysicsAggregateHandle& UseAggregate = FPhysicsAggregateHandle()) const;

	// Reference to our current parallel animation evaluation task (if there is one)
	FGraphEventRef				ParallelAnimationEvaluationTask;

	// Reference to our current blend physics task (if there is one)
	FGraphEventRef				ParallelBlendPhysicsCompletionTask;

	//Data for parallel evaluation of animation
	FAnimationEvaluationContext AnimEvaluationContext;

public:
	// Parallel evaluation wrappers
	void ParallelAnimationEvaluation();
	virtual void CompleteParallelAnimationEvaluation(bool bDoPostAnimEvaluation);


	// Returns whether we are currently trying to run a parallel animation evaluation task
	bool IsRunningParallelEvaluation() const { return IsValidRef(ParallelAnimationEvaluationTask); }

	// Management function for if we want to do an evaluation but may already be running one
	// bBlockOnTask - if true and we are currently performing parallel eval we wait for it to finish
	// bPerformPostAnimEvaluation - if true and we are currently performing parallel eval we call PostAnimEvaluation too
	// return true if parallel task was running.
	bool HandleExistingParallelEvaluationTask(bool bBlockOnTask, bool bPerformPostAnimEvaluation);

	friend class FSkeletalMeshComponentDetails;

	/** Apply animation curves to this component */
	void ApplyAnimationCurvesToComponent(const TMap<FName, float>* InMaterialParameterCurves, const TMap<FName, float>* InAnimationMorphCurves);
	
	// Returns whether we're able to run a simulation (ignoring the suspend flag)
	bool CanSimulateClothing() const;

protected:

	// Returns whether we need to run the Cloth Tick or not
	virtual bool ShouldRunClothTick() const;

private:
	/** Override USkinnedMeshComponent */
	virtual void AddSlavePoseComponent(USkinnedMeshComponent* SkinnedMeshComponent) override;
	virtual void RemoveSlavePoseComponent(USkinnedMeshComponent* SkinnedMeshComponent) override;

	// Returns whether we need to run the Pre Cloth Tick or not
	bool ShouldRunEndPhysicsTick() const;

	// Handles registering/unregistering the pre cloth tick as it is needed
	void UpdateEndPhysicsTickRegisteredState();

	// Handles registering/unregistering the cloth tick as it is needed
	void UpdateClothTickRegisteredState();

	// Handles registering/unregistering the 'during animation' tick as it is needed
	void UpdateDuringAnimationTickRegisteredState();

	// Finalizes pose to OutBoneSpaceTransforms
	void FinalizePoseEvaluationResult(const USkeletalMesh* InMesh, TArray<FTransform>& OutBoneSpaceTransforms, FVector& OutRootBoneTranslation, FCompactPose& InFinalPose) const;

	friend class FParallelBlendPhysicsTask;
	
	void BlendInPhysicsInternal(FTickFunction& ThisTickFunction);

	//wrapper for parallel blend physics
	void ParallelBlendPhysics() { PerformBlendPhysicsBones(RequiredBones, AnimEvaluationContext.ComponentSpaceTransforms, AnimEvaluationContext.BoneSpaceTransforms); }

	void PerformBlendPhysicsBones(const TArray<FBoneIndexType>& InRequiredBones, TArray<FTransform>& InOutComponentSpaceTransforms, TArray<FTransform>& InOutBoneSpaceTransforms);

	friend class FParallelClothTask;
	// This is the parallel function that updates the cloth data and runs the simulation. This is safe to call from worker threads.
	//static void ParallelEvaluateCloth(float DeltaTime, const FClothingActor& ClothingActor, const FClothSimulationContext& ClothSimulationContext);

	friend class FParallelBlendPhysicsCompletionTask;
	void CompleteParallelBlendPhysics();
	void FinalizeAnimationUpdate();

	/** See UpdateClothTransform for documentation. */
	void UpdateClothTransformImp();

	friend class FClothingSimulationContextCommon;

	friend class FTickClothingTask;

#if WITH_EDITORONLY_DATA
	// these are deprecated variables from removing SingleAnimSkeletalComponent
	// remove if this version goes away : VER_UE4_REMOVE_SINGLENODEINSTANCE
	// deprecated variable to be re-save
	UPROPERTY()
	class UAnimSequence* SequenceToPlay_DEPRECATED;

	// The default sequence to play on this skeletal mesh
	UPROPERTY()
	class UAnimationAsset* AnimToPlay_DEPRECATED;

	// Default setting for looping for SequenceToPlay. This is not current state of looping.
	UPROPERTY()
	uint32 bDefaultLooping_DEPRECATED:1;

	// Default setting for playing for SequenceToPlay. This is not current state of playing.
	UPROPERTY()
	uint32 bDefaultPlaying_DEPRECATED:1;

	// Default setting for position of SequenceToPlay to play. 
	UPROPERTY()
	float DefaultPosition_DEPRECATED;

	// Default setting for playrate of SequenceToPlay to play. 
	UPROPERTY()
	float DefaultPlayRate_DEPRECATED;
#endif

	/*
	 * Update MorphTargetCurves from mesh - these are not animation curves, but SetMorphTarget and similar functions that can set to this mesh component
	 */
	void UpdateMorphTargetOverrideCurves();

	/*
	 * Reset MorphTarget Curves - Reset all morphtarget curves
	 */
	void ResetMorphTargetCurves();

public:
	/** Take extracted RootMotion and convert it from local space to world space. */
	FTransform ConvertLocalRootMotionToWorld(const FTransform& InTransform);

	/** Consume and return pending root motion from our internal anim instances (main, sub and post) */
	FRootMotionMovementParams ConsumeRootMotion();

protected:

	/** Consume and return pending root motion from our internal anim instances (main, sub and post) */
	FRootMotionMovementParams ConsumeRootMotion_Internal(float InAlpha);

private:

#if WITH_EDITOR
	/** This is required for recording animations, so save for editor only */
	/** Temporary array of curve arrays that are active on this component - keeps same buffer index as SpaceBases - Please check SkinnedMeshComponent*/
	FBlendedHeapCurve	CurvesArray[2];
public: 
	/** Access Curve Array for reading */
	const FBlendedHeapCurve& GetAnimationCurves() const { return CurvesArray[CurrentReadComponentTransforms]; }

	/** Get Access to the current editable Curve Array - uses same buffer as space bases*/
	FBlendedHeapCurve& GetEditableAnimationCurves() { return CurvesArray[CurrentEditableComponentTransforms]; }
	const FBlendedHeapCurve& GetEditableAnimationCurves() const { return CurvesArray[CurrentEditableComponentTransforms]; }
#endif 


private:
	/** Temporary array of custom attributes that are active on this component - keeps same buffer index as SpaceBases - Please check SkinnedMeshComponent*/
	FHeapCustomAttributes AttributesArray[2];

	FHeapCustomAttributes& GetEditableCustomAttributes() { return AttributesArray[CurrentEditableComponentTransforms]; }

public:
	const FHeapCustomAttributes& GetCustomAttributes() const { return AttributesArray[CurrentReadComponentTransforms]; }
public:
	/** Skeletal mesh component should not be able to have its mobility set to static */
	virtual const bool CanHaveStaticMobility() const override { return false; }

public:
	/** Register/Unregister for physics state creation callback */
	FDelegateHandle RegisterOnPhysicsCreatedDelegate(const FOnSkelMeshPhysicsCreated& Delegate);
	void UnregisterOnPhysicsCreatedDelegate(const FDelegateHandle& DelegateHandle);

	/** Register/Unregister for teleport callback */
	FDelegateHandle RegisterOnTeleportDelegate(const FOnSkelMeshTeleported& Delegate);
	void UnregisterOnTeleportDelegate(const FDelegateHandle& DelegateHandle);

private:

	/** Multicaster fired when this component creates physics state (in case external objects rely on physics state)*/
	FOnSkelMeshPhysicsCreatedMultiCast OnSkelMeshPhysicsCreated;

	/** Multicaster fired when this component teleports */
	FOnSkelMeshTeleportedMultiCast OnSkelMeshPhysicsTeleported;

	/** Mark current anim UID version to up-to-date. Called when it's recalcualted */
	void MarkRequiredCurveUpToDate();
	
	/* This will check if the required curves are up-to-date by checking version number with skeleton. 
	 * Skeleton's curve list changes whenever newer is added or deleted. 
	 * This still has to happen in editor as well as in game as 
	 * There is no guarantee of Skeleton having all curves as we've seen over and over again. 
	 * Cooking does not guarantee skeleton containing all names
	 */
	bool AreRequiredCurvesUpToDate() const;

public:
	void ConditionallyDispatchQueuedAnimEvents();

	// Are we currently within PostAnimEvaluation
	bool IsPostEvaluatingAnimation() const { return bPostEvaluatingAnimation; }
};

struct FLinkedInstancesAdapter
{
	static void AddLinkedInstance(USkeletalMeshComponent* InComponent, UAnimInstance* InAnimInstance)
	{
		if (InComponent && InAnimInstance)
		{
			InComponent->LinkedInstances.Add(InAnimInstance);
		}
	}

	static void RemoveLinkedInstance(USkeletalMeshComponent* InComponent, UAnimInstance* InAnimInstance)
	{
		if (InComponent && InAnimInstance)
		{
			InComponent->LinkedInstances.Remove(InAnimInstance);
		}
	}

	static void ResetLinkedInstance(USkeletalMeshComponent* InComponent)
	{
		if (InComponent)
		{
			InComponent->LinkedInstances.Reset();
		}
	}
};