// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/AnimNode_RigidBody.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"

#include "AnimNode_RigidBody_Chaos.generated.h"

struct FBodyInstance;
struct FConstraintInstance;

extern ANIMGRAPHRUNTIME_API TAutoConsoleVariable<int32> CVarEnableChaosRigidBodyNode;

///** Determines in what space the simulation should run */
//UENUM()
//enum class ESimulationSpace : uint8
//{
//	/** Simulate in component space. Moving the entire skeletal mesh will have no affect on velocities */
//	ComponentSpace,
//	/** Simulate in world space. Moving the skeletal mesh will generate velocity changes */
//	WorldSpace,
//	/** Simulate in another bone space. Moving the entire skeletal mesh and individually modifying the base bone will have no affect on velocities */
//	BaseBoneSpace,
//};

/**
 *	Controller that simulates physics based on the physics asset of the skeletal mesh component
 */
USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_RigidBody_Chaos : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

public:
	FAnimNode_RigidBody_Chaos();
	~FAnimNode_RigidBody_Chaos();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual bool NeedsOnInitializeAnimInstance() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual bool NeedsDynamicReset() const override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual int32 GetLODThreshold() const override;
	// End of FAnimNode_SkeletalControlBase interface

	void PostSerialize(const FArchive& Ar);

	/** Physics asset to use. If empty use the skeletal mesh's default physics asset */
	UPROPERTY(EditAnywhere, Category = Settings)
	UPhysicsAsset* OverridePhysicsAsset;

	/** Override gravity*/
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, editcondition = "bOverrideWorldGravity"))
	FVector OverrideWorldGravity;

	/** Applies a uniform external force in world space. This allows for easily faking inertia of movement while still simulating in component space for example */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	FVector ExternalForce;

	/** When using non-world-space sim, this controls how much of the components world-space acceleration is passed on to the local-space simulation. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FVector ComponentLinearAccScale;

	/** When using non-world-space sim, this applies a 'drag' to the bodies in the local space simulation, based on the components world-space velocity. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FVector ComponentLinearVelScale;

	/** When using non-world-space sim, this is an overall clamp on acceleration derived from ComponentLinearAccScale and ComponentLinearVelScale, to ensure it is not too large. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector	ComponentAppliedLinearAccClamp;

	/**
	 * Scale of cached bounds (vs. actual bounds).
	 * Increasing this may improve performance, but overlaps may not work as well.
	 * (A value of 1.0 effectively disables cached bounds).
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin="1.0", ClampMax="2.0"))
	float CachedBoundsScale;

	/** Matters if SimulationSpace is BaseBone */
	UPROPERTY(EditAnywhere, Category = Settings)
	FBoneReference BaseBoneRef;

	/** The channel we use to find static geometry to collide with */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (editcondition = "bEnableWorldGeometry"))
	TEnumAsByte<ECollisionChannel> OverlapChannel;

	/** What space to simulate the bodies in. This affects how velocities are generated */
	UPROPERTY(EditAnywhere, Category = Settings)
	ESimulationSpace SimulationSpace;

	/** Whether to allow collisions between two bodies joined by a constraint  */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bForceDisableCollisionBetweenConstraintBodies;

	// @todo(ccaulfield): Inline toggles don't seem to be working in our branch
	//UPROPERTY(EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bEnableWorldGeometry : 1;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	uint8 bOverrideWorldGravity : 1;

	/** 
		When simulation starts, transfer previous bone velocities (from animation)
		to make transition into simulation seamless.
	*/
	UPROPERTY(EditAnywhere, Category = Settings, meta=(PinHiddenByDefault))
	uint8 bTransferBoneVelocities : 1;

	/**
		When simulation starts, freeze incoming pose.
		This is useful for ragdolls, when we want the simulation to take over.
		It prevents non simulated bones from animating.
	*/
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bFreezeIncomingPoseOnStart : 1;

	/**
		Correct for linear tearing on bodies with all axes Locked.
		This only works if all axes linear translation are locked
	*/
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bClampLinearTranslationLimitToRefPose : 1;

	/**
	 * Solver iteration settings overrides (defaults are set in the Physics Asset).
	 * These can be varied in the runtime and set through blueprint (e.g., to increase
	 * iterations during difficult movements).
	 * Set to -1 to leave an individual iteration value at its Physics Asset value.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
		FSolverIterations OverrideSolverIterations;

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bComponentSpaceSimulation_DEPRECATED;	//use SimulationSpace
#endif

	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void InitPhysics(const UAnimInstance* InAnimInstance);
	void UpdateWorldGeometry(const UWorld& World, const USkeletalMeshComponent& SKC);
	void UpdateWorldForces(const FTransform& ComponentToWorld, const FTransform& RootBoneTM);

	void InitializeNewBodyTransformsDuringSimulation(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, const FTransform& BaseBoneTM);

	ETeleportType ResetSimulatedTeleportType;

	uint8 bEnabled : 1;
	uint8 bSimulationStarted : 1;
	uint8 bCheckForBodyTransformInit : 1;

	FTransform PreviousCompWorldSpaceTM;
	FTransform CurrentTransform;
	FTransform PreviousTransform;


	float AccumulatedDeltaTime;
	float AnimPhysicsMinDeltaTime;
	bool bSimulateAnimPhysicsAfterReset;

	/** This should only be used for removing the delegate during termination. Do NOT use this for any per frame work */
	TWeakObjectPtr<USkeletalMeshComponent> SkelMeshCompWeakPtr;

	ImmediatePhysics_Chaos::FSimulation* PhysicsSimulation;
	FSolverIterations SolverIterations;

	struct FOutputBoneData
	{
		FOutputBoneData()
			: CompactPoseBoneIndex(INDEX_NONE)
		{}

		TArray<FCompactPoseBoneIndex> BoneIndicesToParentBody;
		FCompactPoseBoneIndex CompactPoseBoneIndex;
		int32 BodyIndex;
		int32 ParentBodyIndex;
	};

	struct FBodyAnimData
	{
		FBodyAnimData()
			: TransferedBoneAngularVelocity(ForceInit)
			, TransferedBoneLinearVelocity(ForceInitToZero)
			, LinearXMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearYMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearZMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearLimit(0.0f)
			, RefPoseLength (0.f)
			, bIsSimulated(false)
			, bBodyTransformInitialized(false)
		{}

		FQuat TransferedBoneAngularVelocity;
		FVector TransferedBoneLinearVelocity;

		ELinearConstraintMotion LinearXMotion;
		ELinearConstraintMotion LinearYMotion;
		ELinearConstraintMotion LinearZMotion;
		float LinearLimit;
		// we don't use linear limit but use default length to limit the bodies
		// linear limits are defined per constraint - it can be any two joints that can limit
		// this is just default length of the local space from parent, and we use that info to limit
		// the translation
		float RefPoseLength;

		bool bIsSimulated : 1;
		bool bBodyTransformInitialized : 1;
	};

	TArray<FOutputBoneData> OutputBoneData;
	TArray<ImmediatePhysics_Chaos::FActorHandle*> Bodies;
	TArray<int32> SkeletonBoneIndexToBodyIndex;
	TArray<FBodyAnimData> BodyAnimData;

	TArray<FPhysicsConstraintHandle*> Constraints;
	TArray<USkeletalMeshComponent::FPendingRadialForces> PendingRadialForces;

	TSet<UPrimitiveComponent*> ComponentsInSim;

	FVector WorldSpaceGravity;

	FSphere Bounds;

	float TotalMass;

	FSphere CachedBounds;

	FCollisionQueryParams QueryParams;

	FPhysScene* PhysScene;

	// Evaluation counter, to detect when we haven't be evaluated in a while.
	FGraphTraversalCounter EvalCounter;

	// Typically, World should never be accessed off the Game Thread.
	// However, since we're just doing overlaps this should be OK.
	const UWorld* UnsafeWorld;

	FBoneContainer CapturedBoneVelocityBoneContainer;
	FCSPose<FCompactHeapPose> CapturedBoneVelocityPose;
	FCSPose<FCompactHeapPose> CapturedFrozenPose;
	FBlendedHeapCurve CapturedFrozenCurves;

	FVector PreviousComponentLinearVelocity;
};

#if WITH_EDITORONLY_DATA
template<>
struct TStructOpsTypeTraits<FAnimNode_RigidBody_Chaos> : public TStructOpsTypeTraitsBase2<FAnimNode_RigidBody_Chaos>
{
	enum
	{
		WithPostSerialize = true
	};
};
#endif
