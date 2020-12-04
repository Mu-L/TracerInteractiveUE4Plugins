// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlaneCutTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshTriangleAttribute.h"
#include "DynamicMeshEditor.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Drawing/MeshDebugDrawing.h"
#include "AssetGenerationUtil.h"

#include "Changes/ToolCommandChangeSequence.h"

#include "CuttingOps/PlaneCutOp.h"

#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "UPlaneCutTool"


/*
 * ToolBuilder
 */


bool UPlaneCutToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UPlaneCutToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPlaneCutTool* NewTool = NewObject<UPlaneCutTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */

UPlaneCutToolProperties::UPlaneCutToolProperties() :
	bKeepBothHalves(false),
	SpacingBetweenHalves(1),
	bFillCutHole(true),
	bShowPreview(true),
	bFillSpans(false)
{
}

UPlaneCutTool::UPlaneCutTool()
{
	CutPlaneOrigin = FVector::ZeroVector;
	CutPlaneOrientation = FQuat::Identity;
}

void UPlaneCutTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UPlaneCutTool::Setup()
{
	UInteractiveTool::Setup();

	// add modifier button for snapping
	UKeyAsModifierInputBehavior* SnapToggleBehavior = NewObject<UKeyAsModifierInputBehavior>();
	SnapToggleBehavior->ModifierCheckFunc = FInputDeviceState::IsShiftKeyDown;
	SnapToggleBehavior->Initialize(this, IgnoreSnappingModifier, EKeys::AnyKey);
	AddInputBehavior(SnapToggleBehavior);

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	bool bAnyHasSameSource = false;
	for (int32 ComponentIdx = 0; !bAnyHasSameSource && ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		FPrimitiveComponentTarget* ComponentTarget = ComponentTargets[ComponentIdx].Get();
		for (int32 VsIdx = ComponentIdx + 1; VsIdx < ComponentTargets.Num(); VsIdx++)
		{
			if (ComponentTarget->HasSameSourceData(*ComponentTargets[VsIdx]))
			{
				bAnyHasSameSource = true;
				break;
			}
		}
	}

	if (bAnyHasSameSource)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("PlaneCutMultipleAssetWithSameSource", "WARNING: Multiple meshes in your selection use the same source asset!  Plane cuts apply to the source asset, and this tool will not duplicate assets for you, so the tool typically cannot give a correct result in this case.  Please consider exiting the tool and duplicating the source assets."),
			EToolMessageLevel::UserWarning);
	}

	// Convert input mesh descriptions to dynamic mesh
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[Idx];
		FDynamicMesh3* OriginalDynamicMesh = new FDynamicMesh3;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
		OriginalDynamicMesh->EnableAttributes();
		TDynamicMeshScalarTriangleAttribute<int>* SubObjectIDs = new TDynamicMeshScalarTriangleAttribute<int>(OriginalDynamicMesh);
		SubObjectIDs->Initialize(0);
		int AttribIndex = OriginalDynamicMesh->Attributes()->AttachAttribute(SubObjectIDs);

		/// fill in the MeshesToCut array
		UDynamicMeshReplacementChangeTarget* Target = MeshesToCut.Add_GetRef(NewObject<UDynamicMeshReplacementChangeTarget>());
		MeshSubObjectAttribIndices.Add(AttribIndex);
		check(MeshSubObjectAttribIndices.Num() == MeshesToCut.Num());
		// store a UV scale based on the original mesh bounds (we don't want to recompute this between cuts b/c we want consistent UV scale)
		MeshUVScaleFactor.Add(1.0 / OriginalDynamicMesh->GetBounds().MaxDim());

		// Set callbacks so previews are invalidated on undo/redo changing the meshes
		Target->SetMesh(TSharedPtr<const FDynamicMesh3>(OriginalDynamicMesh));
		Target->OnMeshChanged.AddLambda([this, Idx]() { Previews[Idx]->InvalidateResult(); });
	}

	// click to set plane behavior
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = this->TargetWorld;

	// Include the original components even though we made them invisible, since we want
	// to be able to reposition the plane onto the original mesh.
	for (const TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		SetPlaneAction->InvisibleComponentsToHitTest.Add(Target->GetOwnerComponent());
	}

	SetPlaneAction->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		SetCutPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal, false);
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->InvalidateResult();
		}
	};
	SetPointInWorldConnector = SetPlaneAction;

	USingleClickInputBehavior* ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector);
	AddInputBehavior(ClickToSetPlaneBehavior);

	// create proxy and gizmo (but don't attach yet)
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::StandardTranslateRotate, this);

	// initialize our properties
	BasicProperties = NewObject<UPlaneCutToolProperties>(this, TEXT("Plane Cut Settings"));
	BasicProperties->RestoreProperties(this);
	AddToolPropertySource(BasicProperties);

	AcceptProperties = NewObject<UAcceptOutputProperties>(this, TEXT("Tool Accept Output Settings"));
	AcceptProperties->RestoreProperties(this);
	AddToolPropertySource(AcceptProperties);

	ToolPropertyObjects.Add(this);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreviews();

	// set initial cut plane (also attaches gizmo/proxy)
	FBox CombinedBounds; CombinedBounds.Init();
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		FVector ComponentOrigin, ComponentExtents;
		ComponentTarget->GetOwnerActor()->GetActorBounds(false, ComponentOrigin, ComponentExtents);
		CombinedBounds += FBox::BuildAABB(ComponentOrigin, ComponentExtents);
	}
	SetCutPlaneFromWorldPos(CombinedBounds.GetCenter(), FVector::UpVector, true);
	// hook up callback so further changes trigger recut
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UPlaneCutTool::TransformChanged);

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartPlaneCutTool", "Press 'A' or use the Cut button to cut the mesh without leaving the tool.  When grid snapping is enabled, you can toggle snapping with the shift key."),
		EToolMessageLevel::UserNotification);
}




void UPlaneCutTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 101,
		TEXT("Do Plane Cut"), 
		LOCTEXT("DoPlaneCut", "Do Plane Cut"),
		LOCTEXT("DoPlaneCutTooltip", "Cut the mesh with the current cutting plane, without exiting the tool"),
		EModifierKey::None, EKeys::A,
		[this]() { this->Cut(); } );
}




void UPlaneCutTool::SetupPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 NumSourceMeshes = MeshesToCut.Num();
	int32 TargetNumPreview = NumSourceMeshes;
	for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
	{
		UPlaneCutOperatorFactory *CutSide = NewObject<UPlaneCutOperatorFactory>();
		CutSide->CutTool = this;
		CutSide->ComponentIndex = PreviewIdx;
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(CutSide, "Preview"));
		Preview->Setup(this->TargetWorld, CutSide);
		Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

		FComponentMaterialSet MaterialSet;
		ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		// set initial preview to un-processed mesh, so stuff doesn't just disappear if the first cut takes a while
		Preview->PreviewMesh->UpdatePreview(MeshesToCut[PreviewIdx]->GetMesh().Get());
		Preview->PreviewMesh->SetTransform(ComponentTargets[PreviewIdx]->GetWorldTransform());
		Preview->SetVisibility(BasicProperties->bShowPreview);
	}
}



void UPlaneCutTool::Cut()
{
	if (!CanAccept())
	{
		return;
	}

	

	TUniquePtr<FToolCommandChangeSequence> ChangeSeq = MakeUnique<FToolCommandChangeSequence>();

	TArray<FDynamicMeshOpResult> Results;
	for (int Idx = 0, N = MeshesToCut.Num(); Idx < N; Idx++)
	{
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews[Idx];
		TUniquePtr<FDynamicMesh3> ResultMesh = Preview->PreviewMesh->ExtractPreviewMesh();
		ChangeSeq->AppendChange(MeshesToCut[Idx], MeshesToCut[Idx]->ReplaceMesh(
			TSharedPtr<const FDynamicMesh3>(ResultMesh.Release()))
		);
	}

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshPlaneCut", "Cut Mesh with Plane"));
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}



void UPlaneCutTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);
	AcceptProperties->SaveProperties(this);

	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}

	if (SetPointInWorldConnector != nullptr)
	{
		delete SetPointInWorldConnector;
	}
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}

void UPlaneCutTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UPlaneCutOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FPlaneCutOp> CutOp = MakeUnique<FPlaneCutOp>();
	CutOp->bFillCutHole = CutTool->BasicProperties->bFillCutHole;
	CutOp->bFillSpans = CutTool->BasicProperties->bFillSpans;

	FTransform LocalToWorld = CutTool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	CutOp->SetTransform(LocalToWorld);
	// for all plane computation, change LocalToWorld to not have any zero scale dims
	FVector LocalToWorldScale = LocalToWorld.GetScale3D();
	for (int i = 0; i < 3; i++)
	{
		float DimScale = FMathf::Abs(LocalToWorldScale[i]);
		float Tolerance = KINDA_SMALL_NUMBER;
		if (DimScale < Tolerance)
		{
			LocalToWorldScale[i] = Tolerance * FMathf::SignNonZero(LocalToWorldScale[i]);
		}
	}
	LocalToWorld.SetScale3D(LocalToWorldScale);
	FTransform WorldToLocal = LocalToWorld.Inverse();
	FVector LocalOrigin = WorldToLocal.TransformPosition(CutTool->CutPlaneOrigin);
	FVector WorldNormal = CutTool->CutPlaneOrientation.GetAxisZ();
	FTransform3d W2LForNormal(WorldToLocal);
	FVector LocalNormal = (FVector)W2LForNormal.TransformNormal(WorldNormal);
	FVector BackTransformed = LocalToWorld.TransformVector(LocalNormal);
	float NormalScaleFactor = FVector::DotProduct(BackTransformed, WorldNormal);
	if (NormalScaleFactor >= FLT_MIN)
	{
		NormalScaleFactor = 1.0 / NormalScaleFactor;
	}
	CutOp->LocalPlaneOrigin = LocalOrigin;
	CutOp->LocalPlaneNormal = LocalNormal;
	CutOp->OriginalMesh = CutTool->MeshesToCut[ComponentIndex]->GetMesh();
	CutOp->bKeepBothHalves = CutTool->BasicProperties->bKeepBothHalves;
	CutOp->CutPlaneLocalThickness = CutTool->BasicProperties->SpacingBetweenHalves * NormalScaleFactor;
	CutOp->UVScaleFactor = CutTool->MeshUVScaleFactor[ComponentIndex];
	CutOp->SubObjectsAttribIndex = CutTool->MeshSubObjectAttribIndices[ComponentIndex];
	

	return CutOp;
}



void UPlaneCutTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FViewCameraState RenderCameraState = RenderAPI->GetCameraState();
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FColor GridColor(128, 128, 128, 32);
	float GridThickness = 0.5f*RenderCameraState.GetPDIScalingFactor();
	int NumGridLines = 10;
	
	FFrame3f DrawFrame(CutPlaneOrigin, CutPlaneOrientation);
	MeshDebugDraw::DrawSimpleFixedScreenAreaGrid(RenderCameraState, DrawFrame, NumGridLines, 45.0, GridThickness, GridColor, false, PDI, FTransform::Identity);
}

void UPlaneCutTool::OnTick(float DeltaTime)
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->bSnapToWorldGrid =
			BasicProperties->bSnapToWorldGrid && bIgnoreSnappingToggle == false;
	}

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}


#if WITH_EDITOR
void UPlaneCutTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UPlaneCutTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPlaneCutToolProperties, bShowPreview)))
	{
		for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
		{
			ComponentTarget->SetOwnerVisibility(!BasicProperties->bShowPreview);
		}
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->SetVisibility(BasicProperties->bShowPreview);
		}
	}

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == IgnoreSnappingModifier)
	{
		bIgnoreSnappingToggle = bIsOn;
	}
}


void UPlaneCutTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	CutPlaneOrientation = Transform.GetRotation();
	CutPlaneOrigin = (FVector)Transform.GetTranslation();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::SetCutPlaneFromWorldPos(const FVector& Position, const FVector& Normal, bool bIsInitializing)
{
	CutPlaneOrigin = Position;

	FFrame3f CutPlane(Position, Normal);
	CutPlaneOrientation = (FQuat)CutPlane.Rotation;

	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
	if (bIsInitializing)
	{
		PlaneTransformGizmo->ReinitializeGizmoTransform(CutPlane.ToFTransform());
	}
	else
	{
		PlaneTransformGizmo->SetNewGizmoTransform(CutPlane.ToFTransform());
	}
}

bool UPlaneCutTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return Super::CanAccept();
}


void UPlaneCutTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	if (Results.Num() == 0)
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlaneCutToolTransactionName", "Plane Cut Tool"));
	

	// currently in-place replaces the first half, and adds a new actor for the second half (if it was generated)
	// TODO: options to support other choices re what should be a new actor

	ensure(Results.Num() > 0);
	int32 NumSourceMeshes = MeshesToCut.Num();
	TArray<TArray<FDynamicMesh3>> AllSplitMeshes; AllSplitMeshes.SetNum(NumSourceMeshes);

	// build a selection change starting w/ the original selection (used if objects are added below)
	FSelectedOjectsChangeList NewSelection;
	NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[OrigMeshIdx];
		NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());
	}

	// check if we entirely cut away any meshes
	bool bWantDestroy = false;
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		bWantDestroy = bWantDestroy || (Results[OrigMeshIdx].Mesh->TriangleCount() == 0);
	}
	// if so ask user what to do
	if (bWantDestroy)
	{
		FText Title = LOCTEXT("PlaneCutDestroyTitle", "Delete mesh components?");
		EAppReturnType::Type Ret = FMessageDialog::Open(EAppMsgType::YesNo, 
			LOCTEXT("PlaneCutDestroyQuestion", "Plane cuts have entirely cut away some meshes.  Actually destroy these mesh components?"), &Title);
		if (Ret == EAppReturnType::No)
		{
			bWantDestroy = false; // quell destructive urge
		}
	}
	
	bool bNeedToAdd = false; // will be set to true if any mesh will be partly split out into a new generated asset
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		FDynamicMesh3* UseMesh = Results[OrigMeshIdx].Mesh.Get();
		check(UseMesh != nullptr);

		if (UseMesh->TriangleCount() == 0)
		{
			if (bWantDestroy)
			{
				ComponentTargets[OrigMeshIdx]->GetOwnerComponent()->DestroyComponent();
			}
			continue;
		}

		if (AcceptProperties->bExportSeparatedPiecesAsNewMeshAssets)
		{
			TDynamicMeshScalarTriangleAttribute<int>* SubMeshIDs =
				static_cast<TDynamicMeshScalarTriangleAttribute<int>*>(UseMesh->Attributes()->GetAttachedAttribute(
					MeshSubObjectAttribIndices[OrigMeshIdx]));
			TArray<FDynamicMesh3>& SplitMeshes = AllSplitMeshes[OrigMeshIdx];
			bool bWasSplit = FDynamicMeshEditor::SplitMesh(UseMesh, SplitMeshes, [SubMeshIDs](int TID)
			{
				return SubMeshIDs->GetValue(TID);
			}
			);
			if (bWasSplit)
			{
				// split mesh did something but has no meshes in the output array??
				if (!ensure(SplitMeshes.Num() > 0))
				{
					continue;
				}
				bNeedToAdd = bNeedToAdd || (SplitMeshes.Num() > 1);
				UseMesh = &SplitMeshes[0];
			}
		}

		ComponentTargets[OrigMeshIdx]->CommitMesh([&UseMesh](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(UseMesh, *CommitParams.MeshDescription);
		});
	}

	if (bNeedToAdd)
	{
		for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
		{
			TArray<FDynamicMesh3>& SplitMeshes = AllSplitMeshes[OrigMeshIdx];
			if (SplitMeshes.Num() < 2)
			{
				continue;
			}

			// build array of materials from the original
			TArray<UMaterialInterface*> Materials;
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[OrigMeshIdx];
			for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
			{
				Materials.Add(ComponentTarget->GetMaterial(MaterialIdx));
			}

			// add all the additional meshes
			for (int AddMeshIdx = 1; AddMeshIdx < SplitMeshes.Num(); AddMeshIdx++)
			{
				AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
					AssetAPI, TargetWorld,
					&SplitMeshes[AddMeshIdx], Results[OrigMeshIdx].Transform, TEXT("PlaneCutOtherPart"), Materials);
				if (NewActor != nullptr)
				{
					NewSelection.Actors.Add(NewActor);
				}
			}
		}

		if (NewSelection.Actors.Num() > 0)
		{
			GetToolManager()->RequestSelectionChange(NewSelection);
		}
	}


	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
