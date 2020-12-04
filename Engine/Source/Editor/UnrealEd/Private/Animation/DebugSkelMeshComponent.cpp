// Copyright Epic Games, Inc. All Rights Reserved.


#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "BonePose.h"
#include "Materials/Material.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "SceneManagement.h"
#include "EngineGlobals.h"
#include "GameFramework/WorldSettings.h"
#include "SkeletalRenderPublic.h"
#include "AnimPreviewInstance.h"
#include "Animation/AnimComposite.h"
#include "Animation/BlendSpaceBase.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"

#include "ClothingAsset.h"
#include "ClothingSimulation.h"
#include "Utils/ClothingMeshUtils.h"
#include "DynamicMeshBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"

//////////////////////////////////////////////////////////////////////////
// UDebugSkelMeshComponent

UDebugSkelMeshComponent::UDebugSkelMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bDrawMesh = true;
	PreviewInstance = NULL;
	bDisplayRawAnimation = false;
	bDisplayNonRetargetedPose = false;

	bMeshSocketsVisible = true;
	bSkeletonSocketsVisible = true;

	TurnTableSpeedScaling = 1.f;
	TurnTableMode = EPersonaTurnTableMode::Stopped;

	bPauseClothingSimulationWithAnim = false;
	bPerformSingleClothingTick = false;

	CachedClothBounds = FBoxSphereBounds(ForceInit);
}

FBoxSphereBounds UDebugSkelMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Override bounds with pre-skinned bounds if asking for them
	if (IsUsingPreSkinnedBounds())
	{
		FBoxSphereBounds PreSkinnedLocalBounds;
		GetPreSkinnedLocalBounds(PreSkinnedLocalBounds);
		return PreSkinnedLocalBounds;
	}

	FBoxSphereBounds Result = Super::CalcBounds(LocalToWorld);

	if (!IsUsingInGameBounds())
	{
		// extend bounds by required bones (respecting current LOD) but without root bone
		if (GetNumComponentSpaceTransforms())
		{
			FBox BoundingBox(ForceInit);
			const int32 NumRequiredBones = RequiredBones.Num();
			for (int32 BoneIndex = 1; BoneIndex < NumRequiredBones; ++BoneIndex)
			{
				FBoneIndexType RequiredBoneIndex = RequiredBones[BoneIndex];
				BoundingBox += GetBoneMatrix((int32)RequiredBoneIndex).GetOrigin();
			}

			if (BoundingBox.IsValid)
			{
				Result = Result + FBoxSphereBounds(BoundingBox);
			}			
		}

		if ( SkeletalMesh )
		{
			Result = Result + SkeletalMesh->GetBounds();
		}
	}

	if (!FMath::IsNearlyZero(CachedClothBounds.SphereRadius))
	{
		Result = Result + CachedClothBounds;
	}	

	return Result;
}

bool UDebugSkelMeshComponent::IsUsingInGameBounds() const
{
	return bIsUsingInGameBounds;
}

void UDebugSkelMeshComponent::UseInGameBounds(bool bUseInGameBounds)
{
	bIsUsingInGameBounds = bUseInGameBounds;
}

bool UDebugSkelMeshComponent::IsUsingPreSkinnedBounds() const
{
	return bIsUsingPreSkinnedBounds;
}

void UDebugSkelMeshComponent::UsePreSkinnedBounds(bool bUsePreSkinnedBounds)
{
	bIsUsingPreSkinnedBounds = bUsePreSkinnedBounds;
}

bool UDebugSkelMeshComponent::CheckIfBoundsAreCorrrect()
{
	if (GetPhysicsAsset())
	{
		bool bWasUsingInGameBounds = IsUsingInGameBounds();
		FTransform TempTransform = FTransform::Identity;
		UseInGameBounds(true);
		FBoxSphereBounds InGameBounds = CalcBounds(TempTransform);
		UseInGameBounds(false);
		FBoxSphereBounds PreviewBounds = CalcBounds(TempTransform);
		UseInGameBounds(bWasUsingInGameBounds);
		// calculate again to have bounds as requested
		CalcBounds(TempTransform);
		// if in-game bounds are of almost same size as preview bounds or bigger, it seems to be fine
		if (! InGameBounds.GetSphere().IsInside(PreviewBounds.GetSphere(), PreviewBounds.GetSphere().W * 0.1f) && // for spheres: A.IsInside(B) checks if A is inside of B
			! PreviewBounds.GetBox().IsInside(InGameBounds.GetBox().ExpandBy(PreviewBounds.GetSphere().W * 0.1f))) // for boxes: A.IsInside(B) checks if B is inside of A
		{
			return true;
		}
	}
	return false;
}

float WrapInRange(float StartVal, float MinVal, float MaxVal)
{
	float Size = MaxVal - MinVal;
	float EndVal = StartVal;
	while (EndVal < MinVal)
	{
		EndVal += Size;
	}

	while (EndVal > MaxVal)
	{
		EndVal -= Size;
	}
	return EndVal;
}

void UDebugSkelMeshComponent::ConsumeRootMotion(const FVector& FloorMin, const FVector& FloorMax)
{
	//Extract root motion regardless of where we use it so that we don't hit
	//problems with it building up in the instance

	FRootMotionMovementParams ExtractedRootMotion = ConsumeRootMotion_Internal(1.0f);

	if (bPreviewRootMotion)
	{
		if (ExtractedRootMotion.bHasRootMotion)
		{
			AddLocalTransform(ExtractedRootMotion.GetRootMotionTransform());

			//Handle moving component so that it stays within the editor floor
			FTransform CurrentTransform = GetRelativeTransform();
			FVector Trans = CurrentTransform.GetTranslation();
			Trans.X = WrapInRange(Trans.X, FloorMin.X, FloorMax.X);
			Trans.Y = WrapInRange(Trans.Y, FloorMin.Y, FloorMax.Y);
			CurrentTransform.SetTranslation(Trans);
			SetRelativeTransform(CurrentTransform);
		}
	}
}

bool UDebugSkelMeshComponent::GetPreviewRootMotion() const
{
	return bPreviewRootMotion;
}

void UDebugSkelMeshComponent::SetPreviewRootMotion(bool bInPreviewRootMotion)
{
	bPreviewRootMotion = bInPreviewRootMotion;
	if (!bPreviewRootMotion)
	{
		if (TurnTableMode == EPersonaTurnTableMode::Stopped)
		{
			SetWorldTransform(FTransform());
		}
		else
		{
			SetRelativeLocation(FVector::ZeroVector);
		}
	}
}

FPrimitiveSceneProxy* UDebugSkelMeshComponent::CreateSceneProxy()
{
	FDebugSkelMeshSceneProxy* Result = NULL;
	ERHIFeatureLevel::Type SceneFeatureLevel = GetWorld()->FeatureLevel;
	FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalMesh ? SkeletalMesh->GetResourceForRendering() : NULL;

	// only create a scene proxy for rendering if
	// properly initialized
	if(SkelMeshRenderData &&
		SkelMeshRenderData->LODRenderData.IsValidIndex(PredictedLODLevel) &&
		!bHideSkin &&
		MeshObject)
	{
		const FColor WireframeMeshOverlayColor(102,205,170,255);
		Result = ::new FDebugSkelMeshSceneProxy(this, SkelMeshRenderData, WireframeMeshOverlayColor);
	}

	return Result;
}

bool UDebugSkelMeshComponent::ShouldRenderSelected() const
{
	return bDisplayBound || bDisplayVertexColors;
}

bool UDebugSkelMeshComponent::IsPreviewOn() const
{
	return (PreviewInstance != NULL) && (PreviewInstance == AnimScriptInstance);
}

FString UDebugSkelMeshComponent::GetPreviewText() const
{
#define LOCTEXT_NAMESPACE "SkelMeshComponent"

	if (IsPreviewOn())
	{
		UAnimationAsset* CurrentAsset = PreviewInstance->GetCurrentAsset();
		if (USkeletalMeshComponent* SkeletalMeshComponent = PreviewInstance->GetDebugSkeletalMeshComponent())
		{
			FText Label = SkeletalMeshComponent->GetOwner() ? FText::FromString(SkeletalMeshComponent->GetOwner()->GetActorLabel()) : LOCTEXT("NoActor", "None");
			return FText::Format(LOCTEXT("ExternalComponent", "External Instance on {0}"), Label).ToString();
		}
		else if (UBlendSpaceBase* BlendSpace = Cast<UBlendSpaceBase>(CurrentAsset))
		{
			return FText::Format( LOCTEXT("BlendSpace", "Blend Space {0}"), FText::FromString(BlendSpace->GetName()) ).ToString();
		}
		else if (UAnimMontage* Montage = Cast<UAnimMontage>(CurrentAsset))
		{
			return FText::Format( LOCTEXT("Montage", "Montage {0}"), FText::FromString(Montage->GetName()) ).ToString();
		}
		else if(UAnimComposite* Composite = Cast<UAnimComposite>(CurrentAsset))
		{
			return FText::Format(LOCTEXT("Composite", "Composite {0}"), FText::FromString(Composite->GetName())).ToString();
		}
		else if (UAnimSequence* Sequence = Cast<UAnimSequence>(CurrentAsset))
		{
			return FText::Format( LOCTEXT("Animation", "Animation {0}"), FText::FromString(Sequence->GetName()) ).ToString();
		}
	}

	return LOCTEXT("ReferencePose", "Reference Pose").ToString();

#undef LOCTEXT_NAMESPACE
}

void UDebugSkelMeshComponent::InitAnim(bool bForceReinit)
{
	// If we already have PreviewInstnace and its asset's Skeleton does not match with mesh's Skeleton
	// then we need to clear it up to avoid an issue
	if ( PreviewInstance && PreviewInstance->GetCurrentAsset() && SkeletalMesh )
	{
		if ( PreviewInstance->GetCurrentAsset()->GetSkeleton() != SkeletalMesh->Skeleton )
		{
			// if it doesn't match, just clear it
			PreviewInstance->SetAnimationAsset(NULL);
		}
	}

	if (PreviewInstance != nullptr && AnimScriptInstance == PreviewInstance && bForceReinit)
	{
		// Reset current animation data
		AnimationData.PopulateFrom(PreviewInstance);
		AnimationData.Initialize(PreviewInstance);
	}

	Super::InitAnim(bForceReinit);

	// if PreviewInstance is NULL, create here once
	if (PreviewInstance == NULL)
	{
		PreviewInstance = NewObject<UAnimPreviewInstance>(this);
		check(PreviewInstance);

		//Set transactional flag in order to restore slider position when undo operation is performed
		PreviewInstance->SetFlags(RF_Transactional);
	}

	// if anim script instance is null because it's not playing a blueprint, set to PreviewInstnace by default
	// that way if user would like to modify bones or do extra stuff, it will work
	if (AnimScriptInstance == NULL)
	{
		AnimScriptInstance = PreviewInstance;
		AnimScriptInstance->InitializeAnimation();
	}
	else
	{
		// Make sure we initialize the preview instance here, as we want the required bones to be up to date
		// even if we arent using the instance right now.
		PreviewInstance->InitializeAnimation();
	}

	if(PostProcessAnimInstance)
	{
		// Add the same settings as the preview instance in this case.
		PostProcessAnimInstance->RootMotionMode = ERootMotionMode::RootMotionFromEverything;
		PostProcessAnimInstance->bUseMultiThreadedAnimationUpdate = false;
	}
}

void UDebugSkelMeshComponent::SetAnimClass(class UClass* NewClass)
{
	// Override this to do nothing and warn the user
	UE_LOG(LogAnimation, Warning, TEXT("Attempting to destroy an animation preview actor, skipping."));
}

void UDebugSkelMeshComponent::EnablePreview(bool bEnable, UAnimationAsset* PreviewAsset)
{
	if (PreviewInstance)
	{
		if (bEnable)
		{
			// back up current AnimInstance if not currently previewing anything
			if (!IsPreviewOn())
			{
				SavedAnimScriptInstance = AnimScriptInstance;
			}

			AnimScriptInstance = PreviewInstance;
		    // restore previous state
		    bDisableClothSimulation = bPrevDisableClothSimulation;
    
			PreviewInstance->SetAnimationAsset(PreviewAsset);
		}
		else if (IsPreviewOn())
		{
			if (PreviewInstance->GetCurrentAsset() == PreviewAsset || PreviewAsset == NULL)
			{
				// now recover to saved AnimScriptInstance;
				AnimScriptInstance = SavedAnimScriptInstance;
				PreviewInstance->SetAnimationAsset(nullptr);
			}
		}

		ClothTeleportMode = EClothingTeleportMode::TeleportAndReset;
	}
}

bool UDebugSkelMeshComponent::ShouldCPUSkin()
{
	return 	GetCPUSkinningEnabled() || bDrawBoneInfluences || bDrawNormals || bDrawTangents || bDrawBinormals || bDrawMorphTargetVerts;
}


void UDebugSkelMeshComponent::PostInitMeshObject(FSkeletalMeshObject* InMeshObject)
{
	Super::PostInitMeshObject( InMeshObject );

	if (InMeshObject)
	{
		if(bDrawBoneInfluences)
		{
			InMeshObject->EnableOverlayRendering(true, &BonesOfInterest, nullptr);
		}
		else if (bDrawMorphTargetVerts)
		{
			InMeshObject->EnableOverlayRendering(true, nullptr, &MorphTargetOfInterests);
		}
	}
}

void UDebugSkelMeshComponent::SetShowBoneWeight(bool bNewShowBoneWeight)
{
	// Check we are actually changing it!
	if(bNewShowBoneWeight == bDrawBoneInfluences)
	{
		return;
	}

	if (bDrawMorphTargetVerts)
	{
		SetShowMorphTargetVerts(false);
	}

	// if turning on this mode
	EnableOverlayMaterial(bNewShowBoneWeight);

	bDrawBoneInfluences = bNewShowBoneWeight;
}

void UDebugSkelMeshComponent::EnableOverlayMaterial(bool bEnable)
{
	if (bEnable)
	{
		SkelMaterials.Empty();
		int32 NumMaterials = GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; i++)
		{
			// Back up old material
			SkelMaterials.Add(GetMaterial(i));
			// Set special bone weight material
			SetMaterial(i, GEngine->BoneWeightMaterial);
		}
	}
	// if turning it off
	else
	{
		int32 NumMaterials = GetNumMaterials();
		check(NumMaterials == SkelMaterials.Num());
		for (int32 i = 0; i < NumMaterials; i++)
		{
			// restore original material
			SetMaterial(i, SkelMaterials[i]);
		}
	}
}

bool UDebugSkelMeshComponent::ShouldRunClothTick() const
{
	const bool bBaseShouldTick = Super::ShouldRunClothTick();
	const bool bBaseCouldTick = CanSimulateClothing();

	// If we could tick, but our simulation is suspended - only tick if we've attempted to step the animation
	if(bBaseCouldTick && bClothingSimulationSuspended && bPerformSingleClothingTick)
	{
		return true;
	}

	return bBaseShouldTick;
}

void UDebugSkelMeshComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();

	if (SceneProxy)
	{
		FDebugSkelMeshDynamicData* NewDynamicData = new FDebugSkelMeshDynamicData(this);

		FDebugSkelMeshSceneProxy* TargetProxy = (FDebugSkelMeshSceneProxy*)SceneProxy;

		ENQUEUE_RENDER_COMMAND(DebugSkelMeshObjectUpdateDataCommand)(
			[TargetProxy, NewDynamicData](FRHICommandListImmediate& RHICommandList)
		{
			if (TargetProxy->DynamicData)
			{
				delete TargetProxy->DynamicData;
			}

			TargetProxy->DynamicData = NewDynamicData;
		}
		);
	}
}

void UDebugSkelMeshComponent::SetShowMorphTargetVerts(bool bNewShowMorphTargetVerts)
{
	// Check we are actually changing it!
	if (bNewShowMorphTargetVerts == bDrawMorphTargetVerts)
	{
		return;
	}

	if (bDrawBoneInfluences)
	{
		SetShowBoneWeight(false);
	}

	// if turning on this mode
	EnableOverlayMaterial(bNewShowMorphTargetVerts);

	bDrawMorphTargetVerts = bNewShowMorphTargetVerts;
}

void UDebugSkelMeshComponent::GenSpaceBases(TArray<FTransform>& OutSpaceBases)
{
	TArray<FTransform> TempBoneSpaceTransforms;
	TempBoneSpaceTransforms.AddUninitialized(OutSpaceBases.Num());
	FVector TempRootBoneTranslation;
	FBlendedHeapCurve TempCurve;
	FHeapCustomAttributes TempAtttributes;
	AnimScriptInstance->PreEvaluateAnimation();
	PerformAnimationEvaluation(SkeletalMesh, AnimScriptInstance, OutSpaceBases, TempBoneSpaceTransforms, TempRootBoneTranslation, TempCurve, TempAtttributes);
	AnimScriptInstance->PostEvaluateAnimation();
}

void UDebugSkelMeshComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	// Run regular update first so we get RequiredBones up to date.
	Super::RefreshBoneTransforms(NULL); // Pass NULL so we force non threaded work

	// none of these code works if we don't have anim instance, so no reason to check it for every if
	if (AnimScriptInstance && AnimScriptInstance->GetRequiredBones().IsValid())
	{
		const bool bIsPreviewInstance = (PreviewInstance && PreviewInstance == AnimScriptInstance);	
		FBoneContainer& BoneContainer = AnimScriptInstance->GetRequiredBones();

		BakedAnimationPoses.Reset();
		if(bDisplayBakedAnimation && bIsPreviewInstance)
		{
			if(UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewInstance->GetCurrentAsset()))
			{
				BakedAnimationPoses.AddUninitialized(BoneContainer.GetNumBones());
				bool bSavedUseSourceData = BoneContainer.ShouldUseSourceData();
				BoneContainer.SetUseRAWData(true);
				BoneContainer.SetUseSourceData(false);
				PreviewInstance->EnableControllers(false);
				GenSpaceBases(BakedAnimationPoses);
				BoneContainer.SetUseRAWData(false);
				BoneContainer.SetUseSourceData(bSavedUseSourceData);
				PreviewInstance->EnableControllers(true);
			}
		}

		SourceAnimationPoses.Reset();
		if(bDisplaySourceAnimation && bIsPreviewInstance)
		{
			if(UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewInstance->GetCurrentAsset()))
			{
				SourceAnimationPoses.AddUninitialized(BoneContainer.GetNumBones());
				bool bSavedUseSourceData = BoneContainer.ShouldUseSourceData();
				BoneContainer.SetUseSourceData(true);
				PreviewInstance->EnableControllers(false);
				GenSpaceBases(SourceAnimationPoses);
				BoneContainer.SetUseSourceData(bSavedUseSourceData);
				PreviewInstance->EnableControllers(true);
			}
		}

		UncompressedSpaceBases.Reset();
		if ( bDisplayRawAnimation )
		{
			UncompressedSpaceBases.AddUninitialized(BoneContainer.GetNumBones());

			const bool bUseSource = BoneContainer.ShouldUseSourceData();
			const bool bUseRaw = BoneContainer.ShouldUseRawData();

			BoneContainer.SetUseSourceData(false);
			BoneContainer.SetUseRAWData(true);

			GenSpaceBases(UncompressedSpaceBases);

			BoneContainer.SetUseRAWData(bUseRaw);
			BoneContainer.SetUseSourceData(bUseSource);
		}

		// Non retargeted pose.
		NonRetargetedSpaceBases.Reset();
		if( bDisplayNonRetargetedPose )
		{
			NonRetargetedSpaceBases.AddUninitialized(BoneContainer.GetNumBones());
			BoneContainer.SetDisableRetargeting(true);
			GenSpaceBases(NonRetargetedSpaceBases);
			BoneContainer.SetDisableRetargeting(false);
		}

		// Only works in PreviewInstance, and not for anim blueprint. This is intended.
		AdditiveBasePoses.Reset();
		if( bDisplayAdditiveBasePose && bIsPreviewInstance )
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewInstance->GetCurrentAsset())) 
			{ 
				if (Sequence->IsValidAdditive()) 
				{ 
					FCSPose<FCompactPose> CSAdditiveBasePose;
					{
						FCompactPose AdditiveBasePose;
						FBlendedCurve AdditiveCurve;
						FStackCustomAttributes AdditiveAttributes;
						AdditiveCurve.InitFrom(BoneContainer);
						AdditiveBasePose.SetBoneContainer(&BoneContainer);
						
						FAnimationPoseData AnimationPoseData(AdditiveBasePose, AdditiveCurve, AdditiveAttributes);
						Sequence->GetAdditiveBasePose(AnimationPoseData, FAnimExtractContext(PreviewInstance->GetCurrentTime()));
						CSAdditiveBasePose.InitPose(AnimationPoseData.GetPose());
					}

					const int32 NumSkeletonBones = BoneContainer.GetNumBones();

					AdditiveBasePoses.AddUninitialized(NumSkeletonBones);

					for (int32 i = 0; i < AdditiveBasePoses.Num(); ++i)
					{
						FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(i));

						// AdditiveBasePoses has one entry for every bone in the asset ref skeleton - if we're on a LOD
						// we need to check this is actually valid for the current pose.
						if(CSAdditiveBasePose.GetPose().IsValidIndex(CompactIndex))
						{
							AdditiveBasePoses[i] = CSAdditiveBasePose.GetComponentSpaceTransform(CompactIndex);
						}
						else
						{
							AdditiveBasePoses[i] = FTransform::Identity;
						}
					}
				}
			}
		}
	}
}

#if WITH_EDITOR
void UDebugSkelMeshComponent::ReportAnimNotifyError(const FText& Error, UObject* InSourceNotify)
{
	for (FAnimNotifyErrors& Errors : AnimNotifyErrors)
	{
		if (Errors.SourceNotify == InSourceNotify)
		{
			Errors.Errors.Add(Error.ToString());
			return;
		}
	}

	int32 i = AnimNotifyErrors.Num();
	AnimNotifyErrors.Add(FAnimNotifyErrors(InSourceNotify));
	AnimNotifyErrors[i].Errors.Add(Error.ToString());
}

void UDebugSkelMeshComponent::ClearAnimNotifyErrors(UObject* InSourceNotify)
{
	for (FAnimNotifyErrors& Errors : AnimNotifyErrors)
	{
		if (Errors.SourceNotify == InSourceNotify)
		{
			Errors.Errors.Empty();
		}
	}
}

FDelegateHandle UDebugSkelMeshComponent::RegisterExtendedViewportTextDelegate(const FGetExtendedViewportText& InDelegate)
{
	ExtendedViewportTextDelegates.Add(InDelegate);
	return ExtendedViewportTextDelegates.Last().GetHandle();
}

void UDebugSkelMeshComponent::UnregisterExtendedViewportTextDelegate(const FDelegateHandle& InDelegateHandle)
{
	ExtendedViewportTextDelegates.RemoveAll([&InDelegateHandle](const FGetExtendedViewportText& InDelegate)
	{
		return InDelegate.GetHandle() == InDelegateHandle;
	});
}

#endif

void UDebugSkelMeshComponent::ToggleClothSectionsVisibility(bool bShowOnlyClothSections)
{
	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();
	if (SkelMeshRenderData)
	{
		for (int32 LODIndex = 0; LODIndex < SkelMeshRenderData->LODRenderData.Num(); LODIndex++)
		{
			FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

			for (int32 SecIdx = 0; SecIdx < LODData.RenderSections.Num(); SecIdx++)
			{
				FSkelMeshRenderSection& Section = LODData.RenderSections[SecIdx];

				if(Section.HasClothingData())
				{
					ShowMaterialSection(Section.MaterialIndex, SecIdx, bShowOnlyClothSections, LODIndex);
				}
				else
				{
					ShowMaterialSection(Section.MaterialIndex, SecIdx, !bShowOnlyClothSections, LODIndex);
				}
			}
		}
	}
}

void UDebugSkelMeshComponent::RestoreClothSectionsVisibility()
{
	// if this skeletal mesh doesn't have any clothing assets, just return
	if (!SkeletalMesh || SkeletalMesh->MeshClothingAssets.Num() == 0)
	{
		return;
	}

	for(int32 LODIndex = 0; LODIndex < GetNumLODs(); LODIndex++)
	{
		ShowAllMaterialSections(LODIndex);
	}
}

void UDebugSkelMeshComponent::SetMeshSectionVisibilityForCloth(FGuid InClothGuid, bool bVisibility)
{
	if(!InClothGuid.IsValid())
	{
		// Nothing to toggle.
		return;
	}

	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();
	if(SkelMeshRenderData)
	{
		for(int32 LODIndex = 0; LODIndex < SkelMeshRenderData->LODRenderData.Num(); LODIndex++)
		{
			FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

			for(int32 SecIdx = 0; SecIdx < LODData.RenderSections.Num(); SecIdx++)
			{
				FSkelMeshRenderSection& Section = LODData.RenderSections[SecIdx];

				// disables cloth section and also corresponding original section for matching cloth asset
				if(Section.HasClothingData() && Section.ClothingData.AssetGuid == InClothGuid)
				{
					ShowMaterialSection(Section.MaterialIndex, SecIdx, bVisibility, LODIndex);
				}
			}
		}
	}
}

void UDebugSkelMeshComponent::ResetMeshSectionVisibility()
{
	for (int32 LODIndex = 0; LODIndex < GetNumLODs(); LODIndex++)
	{
		ShowAllMaterialSections(LODIndex);
	}
}

void UDebugSkelMeshComponent::RebuildClothingSectionsFixedVerts(bool bInvalidateDerivedDataCache)
{
	FSkeletalMeshModel* Resource = SkeletalMesh->GetImportedModel();
	FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(SkeletalMesh);

	const int32 NumLods = Resource->LODModels.Num();
	for (FSkeletalMeshLODModel& LodModel : Resource->LODModels)
	{
		SkeletalMesh->PreEditChange(NULL);

		for(FSkelMeshSection& Section : LodModel.Sections)
		{
			if(Section.ClothMappingData.Num() > 0)
			{
				UClothingAssetBase* BaseAsset = SkeletalMesh->GetClothingAsset(Section.ClothingData.AssetGuid);

				if(BaseAsset)
				{
					UClothingAssetCommon* ConcreteAsset = Cast<UClothingAssetCommon>(BaseAsset);
					const FClothLODDataCommon& LodData = ConcreteAsset->LodData[Section.ClothingData.AssetLodIndex];
					const FPointWeightMap* const MaxDistances = LodData.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);

					if (MaxDistances && MaxDistances->Num())
					{
						for (FMeshToMeshVertData& VertData : Section.ClothMappingData)
						{
							VertData.SourceMeshVertIndices[3] = MaxDistances->AreAllBelowThreshold(
								VertData.SourceMeshVertIndices[0],
								VertData.SourceMeshVertIndices[1],
								VertData.SourceMeshVertIndices[2]) ? 0xFFFF : 0;
						}
					}
					else
					{
						for (FMeshToMeshVertData& VertData : Section.ClothMappingData)
						{
							VertData.SourceMeshVertIndices[3] = 0;
						}
					}
					if (bInvalidateDerivedDataCache)
					{
						// We must always dirty the DDC key unless previewing
						SkeletalMesh->InvalidateDeriveDataCacheGUID();
					}
				}
			}
		}
	}

	ReregisterComponent();
}

void UDebugSkelMeshComponent::CheckClothTeleport()
{
	// do nothing to avoid clothing reset while modifying properties
	// modifying values can cause frame delay and clothes will be reset by a large delta time (low fps)
	// doesn't need cloth teleport while previewing
}

void UDebugSkelMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (TurnTableMode == EPersonaTurnTableMode::Playing)
	{
		FRotator Rotation = GetRelativeTransform().Rotator();
		// Take into account time dilation, so it doesn't affect turn table turn rate.
		float CurrentTimeDilation = 1.0f;
		if (UWorld* MyWorld = GetWorld())
		{
			CurrentTimeDilation = MyWorld->GetWorldSettings()->GetEffectiveTimeDilation();
		}
		Rotation.Yaw += 36.f * TurnTableSpeedScaling * DeltaTime / FMath::Max(CurrentTimeDilation, KINDA_SMALL_NUMBER);
		SetRelativeRotation(Rotation);
	}

    // Brute force approach to ensure that when materials are changed the names are cached parameter names are updated 
	bCachedMaterialParameterIndicesAreDirty = true;
	
	// Force retargeting data to be re-cached to take into account skeleton edits.
	if (bRequiredBonesUpToDateDuringTick)
	{
		bRequiredBonesUpToDate = false;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The tick from our super will call ShouldRunClothTick on us which will 'consume' this flag.
	// flip this flag here to only allow a single tick.
	bPerformSingleClothingTick = false;

	// If we have clothing selected we need to skin the asset for the editor tools
	RefreshSelectedClothingSkinnedPositions();
	return;
}

void UDebugSkelMeshComponent::RefreshSelectedClothingSkinnedPositions()
{
	if(SkeletalMesh && SelectedClothingGuidForPainting.IsValid())
	{
		UClothingAssetBase** Asset = SkeletalMesh->MeshClothingAssets.FindByPredicate([&](UClothingAssetBase* Item)
		{
			return Item && SelectedClothingGuidForPainting == Item->GetAssetGuid();
		});

		if(Asset)
		{
			UClothingAssetCommon* ConcreteAsset = Cast<UClothingAssetCommon>(*Asset);

			if(ConcreteAsset->LodData.IsValidIndex(SelectedClothingLodForPainting))
			{
				SkinnedSelectedClothingPositions.Reset();
				SkinnedSelectedClothingNormals.Reset();

				TArray<FMatrix> RefToLocals;
				// Pass LOD0 to collect all bones
				GetCurrentRefToLocalMatrices(RefToLocals, 0);

				const FClothLODDataCommon& LodData = ConcreteAsset->LodData[SelectedClothingLodForPainting];

				ClothingMeshUtils::SkinPhysicsMesh(ConcreteAsset->UsedBoneIndices, LodData.PhysicalMeshData, FTransform::Identity, RefToLocals.GetData(), RefToLocals.Num(), SkinnedSelectedClothingPositions, SkinnedSelectedClothingNormals);
				RebuildCachedClothBounds();
			}
		}
	}
	else
	{
		SkinnedSelectedClothingNormals.Reset();
		SkinnedSelectedClothingPositions.Reset();
	}
}

void UDebugSkelMeshComponent::GetUsedMaterials(TArray<UMaterialInterface *>& OutMaterials, bool bGetDebugMaterials /*= false*/) const
{
	USkeletalMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

	if (bGetDebugMaterials)
	{
		OutMaterials.Add(GEngine->ClothPaintMaterialInstance);
		OutMaterials.Add(GEngine->ClothPaintMaterialWireframeInstance);
	}
}

IClothingSimulation* UDebugSkelMeshComponent::GetMutableClothingSimulation()
{
	return ClothingSimulation;
}

void UDebugSkelMeshComponent::RebuildCachedClothBounds()
{
	FBox ClothBBox(ForceInit);
	
	for ( int32 Index = 0; Index < SkinnedSelectedClothingPositions.Num(); ++Index )
	{
		ClothBBox += SkinnedSelectedClothingPositions[Index];
	}

	CachedClothBounds = FBoxSphereBounds(ClothBBox);
}

void UDebugSkelMeshComponent::ShowReferencePose(bool bRefPose)
{
	if (bRefPose)
	{
		EnablePreview(true, nullptr);
	}
}

bool UDebugSkelMeshComponent::IsReferencePoseShown() const
{
	return (IsPreviewOn() && PreviewInstance->GetCurrentAsset() == nullptr);
}

/***************************************************
 * FDebugSkelMeshSceneProxy 
 ***************************************************/
FDebugSkelMeshSceneProxy::FDebugSkelMeshSceneProxy(const UDebugSkelMeshComponent* InComponent, FSkeletalMeshRenderData* InSkelMeshRenderData, const FColor& InWireframeOverlayColor /*= FColor::White*/) :
	FSkeletalMeshSceneProxy(InComponent, InSkelMeshRenderData)
{
	DynamicData = nullptr;
	SetWireframeColor(FLinearColor(InWireframeOverlayColor));

	if(GEngine->ClothPaintMaterial)
	{
		MaterialRelevance |= GEngine->ClothPaintMaterial->GetRelevance_Concurrent(GetScene().GetFeatureLevel());
	}
}

SIZE_T FDebugSkelMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FDebugSkelMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if(!DynamicData || DynamicData->bDrawMesh)
	{
		GetMeshElementsConditionallySelectable(Views, ViewFamily, /*bSelectable=*/true, VisibilityMap, Collector);
	}

	if(MeshObject && DynamicData && (DynamicData->bDrawNormals || DynamicData->bDrawTangents || DynamicData->bDrawBinormals))
	{
		for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if(VisibilityMap & (1 << ViewIndex))
			{
				MeshObject->DrawVertexElements(Collector.GetPDI(ViewIndex), GetLocalToWorld(), DynamicData->bDrawNormals, DynamicData->bDrawTangents, DynamicData->bDrawBinormals);
			}
		}
	}

	if(DynamicData && DynamicData->ClothingSimDataIndexWhenPainting != INDEX_NONE && DynamicData->bDrawClothPaintPreview)
	{
		if(DynamicData->SkinnedPositions.Num() > 0 && DynamicData->ClothingVisiblePropertyValues.Num() > 0)
		{
			if (Views.Num())
			{
				FDynamicMeshBuilder MeshBuilderSurface(Views[0]->GetFeatureLevel());
				FDynamicMeshBuilder MeshBuilderWireframe(Views[0]->GetFeatureLevel());

				const TArray<uint32>& Indices = DynamicData->ClothingSimIndices;
				const TArray<FVector>& Vertices = DynamicData->SkinnedPositions;
				const TArray<FVector>& Normals = DynamicData->SkinnedNormals;

				float* ValueArray = DynamicData->ClothingVisiblePropertyValues.GetData();

				const int32 NumVerts = Vertices.Num();

				const FLinearColor Magenta = FLinearColor(1.0f, 0.0f, 1.0f);
				for (int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
				{
					FDynamicMeshVertex Vert;

					Vert.Position = Vertices[VertIndex];
					Vert.TextureCoordinate[0] = { 1.0f, 1.0f };
					Vert.TangentZ = DynamicData->bFlipNormal ? -Normals[VertIndex] : Normals[VertIndex];

					float CurrValue = ValueArray[VertIndex];
					float Range = DynamicData->PropertyViewMax - DynamicData->PropertyViewMin;
					float ClampedViewValue = FMath::Clamp(CurrValue, DynamicData->PropertyViewMin, DynamicData->PropertyViewMax);
					const FLinearColor Color = CurrValue == 0.0f ? Magenta : (FLinearColor::White * ((ClampedViewValue - DynamicData->PropertyViewMin) / Range));
					Vert.Color = Color.ToFColor(true);

					MeshBuilderSurface.AddVertex(Vert);
					MeshBuilderWireframe.AddVertex(Vert);
				}

				const int32 NumIndices = Indices.Num();
				for (int32 TriBaseIndex = 0; TriBaseIndex < NumIndices; TriBaseIndex += 3)
				{
					if (DynamicData->bFlipNormal)
					{
						MeshBuilderSurface.AddTriangle(Indices[TriBaseIndex], Indices[TriBaseIndex + 2], Indices[TriBaseIndex + 1]);
						MeshBuilderWireframe.AddTriangle(Indices[TriBaseIndex], Indices[TriBaseIndex + 2], Indices[TriBaseIndex + 1]);
					}
					else
					{
						MeshBuilderSurface.AddTriangle(Indices[TriBaseIndex], Indices[TriBaseIndex + 1], Indices[TriBaseIndex + 2]);
						MeshBuilderWireframe.AddTriangle(Indices[TriBaseIndex], Indices[TriBaseIndex + 1], Indices[TriBaseIndex + 2]);
					}
				}

				// Set material params
				UMaterialInstanceDynamic* SurfaceMID = GEngine->ClothPaintMaterialInstance;
				check(SurfaceMID);
				UMaterialInstanceDynamic* WireMID = GEngine->ClothPaintMaterialWireframeInstance;
				check(WireMID);

				SurfaceMID->SetScalarParameterValue(FName("ClothOpacity"), DynamicData->ClothMeshOpacity);
				WireMID->SetScalarParameterValue(FName("ClothOpacity"), DynamicData->ClothMeshOpacity);

				SurfaceMID->SetScalarParameterValue(FName("BackfaceCull"), DynamicData->bCullBackface ? 1.0f : 0.0f);
				WireMID->SetScalarParameterValue(FName("BackfaceCull"), true);

				FMaterialRenderProxy* MatProxySurface = SurfaceMID->GetRenderProxy();
				FMaterialRenderProxy* MatProxyWireframe = WireMID->GetRenderProxy();

				if (MatProxySurface && MatProxyWireframe)
				{
					const int32 NumViews = Views.Num();
					for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
					{
						const FSceneView* View = Views[ViewIndex];
						MeshBuilderSurface.GetMesh(GetLocalToWorld(), MatProxySurface, SDPG_Foreground, false, false, ViewIndex, Collector);
						MeshBuilderWireframe.GetMesh(GetLocalToWorld(), MatProxyWireframe, SDPG_Foreground, false, false, ViewIndex, Collector);
					}
				}
			}
		}
	}
}

FDebugSkelMeshDynamicData::FDebugSkelMeshDynamicData(UDebugSkelMeshComponent* InComponent)
	: bDrawMesh(InComponent->bDrawMesh)
	, bDrawNormals(InComponent->bDrawNormals)
	, bDrawTangents(InComponent->bDrawTangents)
	, bDrawBinormals(InComponent->bDrawBinormals)
	, bDrawClothPaintPreview(InComponent->bShowClothData)
	, bFlipNormal(InComponent->bClothFlipNormal)
	, bCullBackface(InComponent->bClothCullBackface)
	, ClothingSimDataIndexWhenPainting(INDEX_NONE)
	, PropertyViewMin(InComponent->MinClothPropertyView)
	, PropertyViewMax(InComponent->MaxClothPropertyView)
	, ClothMeshOpacity(InComponent->ClothMeshOpacity)
{
	if(InComponent->SelectedClothingGuidForPainting.IsValid())
	{
		SkinnedPositions = InComponent->SkinnedSelectedClothingPositions;
		SkinnedNormals = InComponent->SkinnedSelectedClothingNormals;

		if(USkeletalMesh* Mesh = InComponent->SkeletalMesh)
		{
			const int32 NumClothingAssets = Mesh->MeshClothingAssets.Num();
			for(int32 ClothingAssetIndex = 0; ClothingAssetIndex < NumClothingAssets; ++ClothingAssetIndex)
			{
				UClothingAssetBase* BaseAsset = Mesh->MeshClothingAssets[ClothingAssetIndex];
				if(BaseAsset && BaseAsset->GetAssetGuid() == InComponent->SelectedClothingGuidForPainting)
				{
					ClothingSimDataIndexWhenPainting = ClothingAssetIndex;

					if(UClothingAssetCommon* ConcreteAsset = Cast<UClothingAssetCommon>(BaseAsset))
					{
						if(ConcreteAsset->LodData.IsValidIndex(InComponent->SelectedClothingLodForPainting))
						{
							const FClothLODDataCommon& LodData = ConcreteAsset->LodData[InComponent->SelectedClothingLodForPainting];

							ClothingSimIndices = LodData.PhysicalMeshData.Indices;

							if(LodData.PointWeightMaps.IsValidIndex(InComponent->SelectedClothingLodMaskForPainting))
							{
								const FPointWeightMap& Mask = LodData.PointWeightMaps[InComponent->SelectedClothingLodMaskForPainting];

								ClothingVisiblePropertyValues = Mask.Values;
							}
						}
					}

					break;
				}
			}
		}
	}
}

FScopedSuspendAlternateSkinWeightPreview::FScopedSuspendAlternateSkinWeightPreview(USkeletalMesh* SkeletalMesh)
{
	SuspendedComponentArray.Empty(2);
	if (SkeletalMesh != nullptr)
	{
		// Now iterate over all skeletal mesh components and unregister them from the world, we will reregister them in the destructor
		for (TObjectIterator<UDebugSkelMeshComponent> It; It; ++It)
		{
			UDebugSkelMeshComponent* DebugSKComp = *It;
			if (DebugSKComp->SkeletalMesh == SkeletalMesh)
			{
				const FName ProfileName = DebugSKComp->GetCurrentSkinWeightProfileName();
				if (ProfileName != NAME_None)
				{
					DebugSKComp->ClearSkinWeightProfile();
					TTuple<UDebugSkelMeshComponent*, FName> ComponentTupple;
					ComponentTupple.Key = DebugSKComp;
					ComponentTupple.Value = ProfileName;
					SuspendedComponentArray.Add(ComponentTupple);
				}
			}
		}
	}
}

FScopedSuspendAlternateSkinWeightPreview::~FScopedSuspendAlternateSkinWeightPreview()
{
	//Put back the skin weight profile for all editor debug component
	for (const TTuple<UDebugSkelMeshComponent*, FName>& ComponentTupple : SuspendedComponentArray)
	{
		ComponentTupple.Key->SetSkinWeightProfile(ComponentTupple.Value);
	}
	SuspendedComponentArray.Empty();
}
