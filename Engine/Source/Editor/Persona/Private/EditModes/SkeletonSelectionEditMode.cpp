// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "EditModes/SkeletonSelectionEditMode.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimationEditorViewportClient.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "AnimPreviewInstance.h"
#include "ISkeletonTree.h"
#include "AssetEditorModeManager.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"

#include "IPersonaToolkit.h"
#include "IEditableSkeleton.h"

#define LOCTEXT_NAMESPACE "SkeletonSelectionEditMode"

namespace SkeletonSelectionModeConstants
{
	/** Distance to trace for physics bodies */
	static const float BodyTraceDistance = 10000.0f;
}

FSkeletonSelectionEditMode::FSkeletonSelectionEditMode()
	: bManipulating(false)
	, bInTransaction(false)
{
	// Disable grid drawing for this mode as the viewport handles this
	bDrawGrid = false;
}

bool FSkeletonSelectionEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	bool bHandled = false;

	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (GetAnimPreviewScene().GetSelectedBoneIndex() != INDEX_NONE)
	{
		const int32 FocusBoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
		if (FocusBoneIndex != INDEX_NONE)
		{
			const FName BoneName = PreviewMeshComponent->SkeletalMesh->RefSkeleton.GetBoneName(FocusBoneIndex);
			OutTarget.Center = PreviewMeshComponent->GetBoneLocation(BoneName);
			OutTarget.W = 30.0f;
			bHandled = true;
		}
	}

	if (!bHandled && GetAnimPreviewScene().GetSelectedSocket().IsValid())
	{
		USkeletalMeshSocket * Socket = GetAnimPreviewScene().GetSelectedSocket().Socket;
		if (Socket)
		{
			OutTarget.Center = Socket->GetSocketLocation(PreviewMeshComponent);
			OutTarget.W = 30.0f;
			bHandled = true;
		}
	}

	return bHandled;
}

IPersonaPreviewScene& FSkeletonSelectionEditMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FSkeletonSelectionEditMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{

}

FSelectedSocketInfo FSkeletonSelectionEditMode::DuplicateAndSelectSocket(const FSelectedSocketInfo& SocketInfoToDuplicate)
{
	USkeletalMesh* SkeletalMesh = GetAnimPreviewScene().GetPreviewMeshComponent()->SkeletalMesh;
	USkeletalMeshSocket* NewSocket = GetAnimPreviewScene().GetPersonaToolkit()->GetEditableSkeleton()->DuplicateSocket(SocketInfoToDuplicate, SocketInfoToDuplicate.Socket->BoneName, SkeletalMesh);

	FSelectedSocketInfo NewSocketInfo(NewSocket, SocketInfoToDuplicate.bSocketIsOnSkeleton);
	GetAnimPreviewScene().DeselectAll();
	GetAnimPreviewScene().SetSelectedSocket(NewSocketInfo);

	return NewSocketInfo;
}

bool FSkeletonSelectionEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	FWidget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();

	int32 BoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
	USkeletalMeshSocket* SelectedSocket = GetAnimPreviewScene().GetSelectedSocket().Socket;
	AActor* SelectedActor = GetAnimPreviewScene().GetSelectedActor();

	if (BoneIndex >= 0 || SelectedSocket != nullptr || SelectedActor != nullptr)
	{
		bool bValidAxis = false;
		FVector WorldAxisDir;

		if ((CurrentAxis & EAxisList::XYZ) != 0)
		{
			UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
			FSelectedSocketInfo SelectedSocketInfo = GetAnimPreviewScene().GetSelectedSocket();
			if (SelectedSocketInfo.IsValid())
			{
				const bool bAltDown = InViewportClient->IsAltPressed();

				if (bAltDown)
				{
					// Rather than moving/rotating the selected socket, copy it and move the copy instead
					SelectedSocketInfo = DuplicateAndSelectSocket(SelectedSocketInfo);
				}

				// Socket movement is transactional - we want undo/redo and saving of it
				USkeletalMeshSocket* Socket = SelectedSocketInfo.Socket;

				if (Socket && bInTransaction == false)
				{
					if (WidgetMode == FWidget::WM_Rotate)
					{
						GEditor->BeginTransaction(LOCTEXT("AnimationEditorViewport_RotateSocket", "Rotate Socket"));
					}
					else
					{
						GEditor->BeginTransaction(LOCTEXT("AnimationEditorViewport_TranslateSocket", "Translate Socket"));
					}

					Socket->SetFlags(RF_Transactional);	// Undo doesn't work without this!
					Socket->Modify();
					bInTransaction = true;
				}
			}
			else if (BoneIndex >= 0)
			{
				if (bInTransaction == false)
				{
					// we also allow undo/redo of bone manipulations
					if (WidgetMode == FWidget::WM_Rotate)
					{
						GEditor->BeginTransaction(LOCTEXT("AnimationEditorViewport_RotateBone", "Rotate Bone"));
					}
					else
					{
						GEditor->BeginTransaction(LOCTEXT("AnimationEditorViewport_TranslateBone", "Translate Bone"));
					}

					PreviewMeshComponent->PreviewInstance->SetFlags(RF_Transactional);	// Undo doesn't work without this!
					PreviewMeshComponent->PreviewInstance->Modify();
					bInTransaction = true;

					// now modify the bone array
					const FName BoneName = PreviewMeshComponent->SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex);
					PreviewMeshComponent->PreviewInstance->ModifyBone(BoneName);
				}
			}
		}

		bManipulating = true;
		return true;
	}

	return false;
}

bool FSkeletonSelectionEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (bManipulating)
	{
		// Socket movement is transactional - we want undo/redo and saving of it
		if (bInTransaction)
		{
			GEditor->EndTransaction();
			bInTransaction = false;
		}

		bManipulating = false;
		return true;
	}

	return false;
}

bool FSkeletonSelectionEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	const FWidget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
	const ECoordSystem CoordSystem = InViewportClient->GetWidgetCoordSystemSpace();

	// Get some useful info about buttons being held down
	const bool bCtrlDown = InViewport->KeyState(EKeys::LeftControl) || InViewport->KeyState(EKeys::RightControl);
	const bool bShiftDown = InViewport->KeyState(EKeys::LeftShift) || InViewport->KeyState(EKeys::RightShift);
	const bool bMouseButtonDown = InViewport->KeyState( EKeys::LeftMouseButton ) || InViewport->KeyState( EKeys::MiddleMouseButton ) || InViewport->KeyState( EKeys::RightMouseButton );

	bool bHandled = false;

	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	if ( bManipulating && CurrentAxis != EAxisList::None )
	{
		bHandled = true;

		int32 BoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
		USkeletalMeshSocket* SelectedSocket = GetAnimPreviewScene().GetSelectedSocket().Socket;
		AActor* SelectedActor = GetAnimPreviewScene().GetSelectedActor();
		FAnimNode_ModifyBone* SkelControl = nullptr;

		if ( BoneIndex >= 0 )
		{
			//Get the skeleton control manipulating this bone
			const FName BoneName = PreviewMeshComponent->SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex);
			SkelControl = &(PreviewMeshComponent->PreviewInstance->ModifyBone(BoneName));
		}

		if ( SkelControl || SelectedSocket )
		{
			FTransform CurrentSkelControlTM(
				SelectedSocket ? SelectedSocket->RelativeRotation : SkelControl->Rotation,
				SelectedSocket ? SelectedSocket->RelativeLocation : SkelControl->Translation,
				SelectedSocket ? SelectedSocket->RelativeScale : SkelControl->Scale);

			FTransform BaseTM;

			if ( SelectedSocket )
			{
				BaseTM = SelectedSocket->GetSocketTransform( PreviewMeshComponent );
			}
			else
			{
				BaseTM = PreviewMeshComponent->GetBoneTransform( BoneIndex );
			}

			// Remove SkelControl's orientation from BoneMatrix, as we need to translate/rotate in the non-SkelControlled space
			BaseTM = BaseTM.GetRelativeTransformReverse(CurrentSkelControlTM);

			const bool bDoRotation    = WidgetMode == FWidget::WM_Rotate    || WidgetMode == FWidget::WM_TranslateRotateZ;
			const bool bDoTranslation = WidgetMode == FWidget::WM_Translate || WidgetMode == FWidget::WM_TranslateRotateZ;
			const bool bDoScale = WidgetMode == FWidget::WM_Scale;

			if (bDoRotation)
			{
				FVector RotAxis;
				float RotAngle;
				InRot.Quaternion().ToAxisAndAngle( RotAxis, RotAngle );

				FVector4 BoneSpaceAxis = BaseTM.TransformVectorNoScale( RotAxis );

				//Calculate the new delta rotation
				FQuat DeltaQuat( BoneSpaceAxis, RotAngle );
				DeltaQuat.Normalize();

				FRotator NewRotation = ( CurrentSkelControlTM * FTransform( DeltaQuat )).Rotator();

				if ( SelectedSocket )
				{
					SelectedSocket->RelativeRotation = NewRotation;
				}
				else
				{
					SkelControl->Rotation = NewRotation;
				}
			}

			if (bDoTranslation)
			{
				FVector4 BoneSpaceOffset = BaseTM.TransformVector(InDrag);
				if (SelectedSocket)
				{
					SelectedSocket->RelativeLocation += BoneSpaceOffset;
				}
				else
				{
					SkelControl->Translation += BoneSpaceOffset;
				}
			}
			if(bDoScale)
			{
				FVector4 BoneSpaceScaleOffset;

				if (CoordSystem == COORD_World)
				{
					BoneSpaceScaleOffset = BaseTM.TransformVector(InScale);
				}
				else
				{
					BoneSpaceScaleOffset = InScale;
				}

				if(SelectedSocket)
				{
					SelectedSocket->RelativeScale += BoneSpaceScaleOffset;
				}
				else
				{
					SkelControl->Scale += BoneSpaceScaleOffset;
				}
			}

		}
		else if( SelectedActor != nullptr )
		{
			if (WidgetMode == FWidget::WM_Rotate)
			{
				FTransform Transform = SelectedActor->GetTransform();
				FRotator NewRotation = (Transform * FTransform( InRot ) ).Rotator();

				SelectedActor->SetActorRotation( NewRotation );
			}
			else
			{
				FVector Location = SelectedActor->GetActorLocation();
				Location += InDrag;
				SelectedActor->SetActorLocation(Location);
			}
		}

		InViewport->Invalidate();
	}

	return bHandled;
}

void FSkeletonSelectionEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	// If we have a socket of interest, draw the widget
	if (GetAnimPreviewScene().GetSelectedSocket().IsValid())
	{
		TArray<USkeletalMeshSocket*> SocketAsArray;
		SocketAsArray.Add(GetAnimPreviewScene().GetSelectedSocket().Socket);
		FAnimationViewportClient::DrawSockets(GetAnimPreviewScene().GetPreviewMeshComponent(), SocketAsArray, GetAnimPreviewScene().GetSelectedSocket(), PDI, false);
	}
}

void FSkeletonSelectionEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	// Draw name of selected bone
	if (IsSelectedBoneRequired())
	{
		const FIntPoint ViewPortSize = Viewport->GetSizeXY();
		const int32 HalfX = ViewPortSize.X / 2;
		const int32 HalfY = ViewPortSize.Y / 2;

		int32 BoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
		const FName BoneName = PreviewMeshComponent->SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex);

		FMatrix BoneMatrix = PreviewMeshComponent->GetBoneMatrix(BoneIndex);
		const FPlane Proj = View->Project(BoneMatrix.GetOrigin());
		if (Proj.W > 0.f)
		{
			const int32 XPos = HalfX + (HalfX * Proj.X);
			const int32 YPos = HalfY + (HalfY * (Proj.Y * -1));

			FQuat BoneQuat = PreviewMeshComponent->GetBoneQuaternion(BoneName);
			FVector Loc = PreviewMeshComponent->GetBoneLocation(BoneName);
			FCanvasTextItem TextItem(FVector2D(XPos, YPos), FText::FromString(BoneName.ToString()), GEngine->GetSmallFont(), FLinearColor::White);
			Canvas->DrawItem(TextItem);
		}
	}

	// Draw name of selected socket
	if (GetAnimPreviewScene().GetSelectedSocket().IsValid())
	{
		USkeletalMeshSocket* Socket = GetAnimPreviewScene().GetSelectedSocket().Socket;

		FMatrix SocketMatrix;
		Socket->GetSocketMatrix(SocketMatrix, PreviewMeshComponent);
		const FVector SocketPos = SocketMatrix.GetOrigin();

		const FPlane Proj = View->Project(SocketPos);
		if (Proj.W > 0.f)
		{
			const FIntPoint ViewPortSize = Viewport->GetSizeXY();
			const int32 HalfX = ViewPortSize.X / 2;
			const int32 HalfY = ViewPortSize.Y / 2;

			const int32 XPos = HalfX + (HalfX * Proj.X);
			const int32 YPos = HalfY + (HalfY * (Proj.Y * -1));
			FCanvasTextItem TextItem(FVector2D(XPos, YPos), FText::FromString(Socket->SocketName.ToString()), GEngine->GetSmallFont(), FLinearColor::White);
			Canvas->DrawItem(TextItem);
		}
	}
}

bool FSkeletonSelectionEditMode::AllowWidgetMove()
{
	return ShouldDrawWidget();
}

bool FSkeletonSelectionEditMode::IsSelectedBoneRequired() const
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
	int32 SelectedBoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
	if (SelectedBoneIndex != INDEX_NONE && PreviewMeshComponent->GetSkeletalMeshRenderData())
	{
		//Get current LOD
		FSkeletalMeshRenderData* SkelMeshRenderData = PreviewMeshComponent->GetSkeletalMeshRenderData();
		const int32 LODIndex = FMath::Clamp(PreviewMeshComponent->PredictedLODLevel, 0, SkelMeshRenderData->LODRenderData.Num() - 1);
		FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

		//Check whether the bone is vertex weighted
		return LODData.RequiredBones.Find(SelectedBoneIndex) != INDEX_NONE;
	}

	return false;
}

bool FSkeletonSelectionEditMode::ShouldDrawWidget() const
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
	if (!PreviewMeshComponent->IsAnimBlueprintInstanced())
	{
		return IsSelectedBoneRequired() || GetAnimPreviewScene().GetSelectedSocket().IsValid() || GetAnimPreviewScene().GetSelectedActor() != nullptr;
	}

	return false;
}

bool FSkeletonSelectionEditMode::UsesTransformWidget() const
{
	return true;
}

bool FSkeletonSelectionEditMode::UsesTransformWidget(FWidget::EWidgetMode CheckMode) const
{
	return ShouldDrawWidget() && (CheckMode == FWidget::WM_Scale || CheckMode == FWidget::WM_Translate || CheckMode == FWidget::WM_Rotate);
}

bool FSkeletonSelectionEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	int32 BoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
	if (BoneIndex != INDEX_NONE)
	{
		FTransform BoneMatrix = PreviewMeshComponent->GetBoneTransform(BoneIndex);

		InMatrix = BoneMatrix.ToMatrixNoScale().RemoveTranslation();
		return true;
	}
	else if (GetAnimPreviewScene().GetSelectedSocket().IsValid())
	{
		USkeletalMeshSocket* Socket = GetAnimPreviewScene().GetSelectedSocket().Socket;

		FTransform SocketMatrix = Socket->GetSocketTransform(PreviewMeshComponent);

		InMatrix = SocketMatrix.ToMatrixNoScale().RemoveTranslation();
		return true;
	}
	else if (AActor* SelectedActor = GetAnimPreviewScene().GetSelectedActor())
	{
		InMatrix = SelectedActor->GetTransform().ToMatrixNoScale().RemoveTranslation();
		return true;
	}

	return false;
}

bool FSkeletonSelectionEditMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

FVector FSkeletonSelectionEditMode::GetWidgetLocation() const
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	int32 BoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
	if (BoneIndex != INDEX_NONE)
	{
		const FName BoneName = PreviewMeshComponent->SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex);

		FMatrix BoneMatrix = PreviewMeshComponent->GetBoneMatrix(BoneIndex);

		return BoneMatrix.GetOrigin();
	}
	else if (GetAnimPreviewScene().GetSelectedSocket().IsValid())
	{
		USkeletalMeshSocket* Socket = GetAnimPreviewScene().GetSelectedSocket().Socket;

		FMatrix SocketMatrix;
		Socket->GetSocketMatrix(SocketMatrix, PreviewMeshComponent);

		return SocketMatrix.GetOrigin();
	}
	else if (AActor* SelectedActor = GetAnimPreviewScene().GetSelectedActor())
	{
		return SelectedActor->GetActorLocation();
	}

	return FVector::ZeroVector;
}

bool FSkeletonSelectionEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	bool bHandled = false;
	const bool bSelectingSections = GetAnimPreviewScene().AllowMeshHitProxies();

	USkeletalMeshComponent* MeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	if ( HitProxy )
	{
		if (!HitProxy->IsA(HActor::StaticGetType()) && MeshComponent)
		{
			MeshComponent->SetSelectedEditorSection(INDEX_NONE);
		}

		if ( HitProxy->IsA( HPersonaSocketProxy::StaticGetType() ) )
		{
			// Tell the preview scene that the socket has been selected - this will sort out the skeleton tree, etc.
			GetAnimPreviewScene().DeselectAll();
			GetAnimPreviewScene().SetSelectedSocket(static_cast<HPersonaSocketProxy*>(HitProxy)->SocketInfo);
			bHandled = true;
		}
		else if ( HitProxy->IsA( HPersonaBoneProxy::StaticGetType() ) )
		{			
			// Tell the preview scene that the bone has been selected - this will sort out the skeleton tree, etc.
			GetAnimPreviewScene().DeselectAll();
			GetAnimPreviewScene().SetSelectedBone(static_cast<HPersonaBoneProxy*>(HitProxy)->BoneName);
			bHandled = true;
		}
		else if ( HitProxy->IsA( HActor::StaticGetType() ) && bSelectingSections)
		{
			HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
			GetAnimPreviewScene().BroadcastMeshClick(ActorHitProxy, Click); // This can pop up menu which redraws viewport and invalidates HitProxy!
			bHandled = true;
		}
	}
	else
	{
		// Deselect mesh sections
		if (MeshComponent)
		{
			MeshComponent->SetSelectedEditorSection(INDEX_NONE);
		}
	}
	
	if ( !bHandled && !bSelectingSections )
	{
		// Cast for phys bodies if we didn't get any hit proxies
		FHitResult Result(1.0f);
		UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
		bool bHit = PreviewMeshComponent->LineTraceComponent(Result, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * SkeletonSelectionModeConstants::BodyTraceDistance, FCollisionQueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(),true));
		
		if(bHit)
		{
			GetAnimPreviewScene().DeselectAll();
			GetAnimPreviewScene().SetSelectedBone(Result.BoneName);
			bHandled = true;
		}
		else
		{
			// We didn't hit a proxy or a physics object, so deselect all objects
			GetAnimPreviewScene().DeselectAll();
		}
	}

	return bHandled;
}

bool FSkeletonSelectionEditMode::CanCycleWidgetMode() const
{
	int32 SelectedBoneIndex = GetAnimPreviewScene().GetSelectedBoneIndex();
	USkeletalMeshSocket* SelectedSocket = GetAnimPreviewScene().GetSelectedSocket().Socket;
	AActor* SelectedActor = GetAnimPreviewScene().GetSelectedActor();

	return (SelectedBoneIndex >= 0 || SelectedSocket || SelectedActor != nullptr);
}

#undef LOCTEXT_NAMESPACE
