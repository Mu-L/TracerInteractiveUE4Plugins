// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultControlRigManipulationLayer.h"
#include "Manipulatable/IControlRigManipulatable.h"
#include "ControlRig.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRigGizmoLibrary.h"
#include "IControlRigObjectBinding.h"

#if WITH_EDITOR
#include "ControlRigBlueprint.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigDefaultManipulationLayer"

UDefaultControlRigManipulationLayer::UDefaultControlRigManipulationLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InteractionTransaction(nullptr)
{
}

void UDefaultControlRigManipulationLayer::CreateLayer()
{
}

void UDefaultControlRigManipulationLayer::DestroyLayer()
{
	IControlRigManipulationLayer::DestroyLayer();
}

void UDefaultControlRigManipulationLayer::AddManipulatableObject(IControlRigManipulatable* InObject)
{
	if (InObject)
	{
		IControlRigManipulationLayer::AddManipulatableObject(InObject);
		if (UControlRig* ControlRig = Cast<UControlRig>(InObject))
		{
			OnControlRigAdded(ControlRig);
		}
	}
}

void UDefaultControlRigManipulationLayer::RemoveManipulatableObject(IControlRigManipulatable* InObject)
{
	if (InObject)
	{
		if (UControlRig* ControlRig = Cast<UControlRig>(InObject))
		{
			OnControlRigRemoved(ControlRig);
		}
		IControlRigManipulationLayer::RemoveManipulatableObject(InObject);
	}
}

void UDefaultControlRigManipulationLayer::OnControlRigAdded(UControlRig* InControlRig)
{
	// bind execution delegate

	ControlModifiedDelegateHandles.Add(InControlRig->ControlModified().AddUObject(this, &UDefaultControlRigManipulationLayer::OnControlModified));

	// ensure the size matches
	ensure(ControlModifiedDelegateHandles.Num() == ManipulatableObjects.Num());

	// object binding. This overwrites if there were multi
	SetObjectBinding(InControlRig->GetObjectBinding());

	// currently all the manipulatable mesh component is supposed to be same
	// if that changes, this code has to change
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	if (MeshComponent)
	{
		MeshComponent->OnBoneTransformsFinalized.AddDynamic(this, &UDefaultControlRigManipulationLayer::PostPoseUpdate);
		MeshComponent->OnAnimInitialized.AddDynamic(this, &UDefaultControlRigManipulationLayer::OnPoseInitialized);
	}
}

void UDefaultControlRigManipulationLayer::OnControlRigRemoved(UControlRig* InControlRig)
{
	// unbind execution delegate
	int32 Found = ManipulatableObjects.Find(InControlRig);
	if (Found != INDEX_NONE)
	{
		// last one 
		// currently all the manipulatable mesh component is supposed to be same
		// if that changes, this code has to change
		if (ManipulatableObjects.Num() == 1)
		{
			USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
			if (MeshComponent)
			{
				MeshComponent->OnBoneTransformsFinalized.RemoveDynamic(this, &UDefaultControlRigManipulationLayer::PostPoseUpdate);
				MeshComponent->OnAnimInitialized.RemoveDynamic(this, &UDefaultControlRigManipulationLayer::OnPoseInitialized);
			}
		}

		if (ControlModifiedDelegateHandles.IsValidIndex(Found) && ControlModifiedDelegateHandles[Found].IsValid())
		{
			InControlRig->ControlModified().Remove(ControlModifiedDelegateHandles[Found]);
		}
	}
}

// gizmo related 
void UDefaultControlRigManipulationLayer::SetGizmoTransform(AControlRigGizmoActor* GizmoActor, const FTransform& InTransform)
{
	const FControlData* Manip = GetControlDataFromGizmo(GizmoActor);
	if (Manip)
	{
		Manip->ManipObject->SetControlGlobalTransform(Manip->ControlName, InTransform);
	}
}

void UDefaultControlRigManipulationLayer::GetGizmoTransform(AControlRigGizmoActor* GizmoActor, FTransform& OutTransform) const
{
	const FControlData* Manip = GetControlDataFromGizmo(GizmoActor);
	if (Manip)
	{
		OutTransform = Manip->ManipObject->GetControlGlobalTransform(Manip->ControlName);
	}
}

void UDefaultControlRigManipulationLayer::MoveGizmo(AControlRigGizmoActor* GizmoActor, const bool bTranslation, FVector& InDrag,
	const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform)
{
	const FControlData* Manip = GetControlDataFromGizmo(GizmoActor);
	if (Manip)
	{
		FTransform CurrentTransform = Manip->ManipObject->GetControlGlobalTransform(Manip->ControlName) * ToWorldTransform;

		bool bTransformChanged = false;
		if (bRotation)
		{
			FQuat CurrentRotation = CurrentTransform.GetRotation();
			CurrentRotation = (InRot.Quaternion() * CurrentRotation);
			CurrentTransform.SetRotation(CurrentRotation);
			bTransformChanged = true;
		}

		if (bTranslation)
		{
			FVector CurrentLocation = CurrentTransform.GetLocation();
			CurrentLocation = CurrentLocation + InDrag;
			CurrentTransform.SetLocation(CurrentLocation);
			bTransformChanged = true;
		}

		if (bScale)
		{
			FVector CurrentScale = CurrentTransform.GetScale3D();
			CurrentScale = CurrentScale + InScale;
			CurrentTransform.SetScale3D(CurrentScale);
			bTransformChanged = true;
		}

		if (bTransformChanged)
		{
			FTransform NewTransform = CurrentTransform.GetRelativeTransform(ToWorldTransform);
			Manip->ManipObject->SetControlGlobalTransform(Manip->ControlName, NewTransform);
			// assumes it's attached to actor
			GizmoActor->SetGlobalTransform(CurrentTransform);

#if WITH_EDITOR
			if (UControlRig* ControlRig = Cast<UControlRig>(Manip->ManipObject))
			{
				if(UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(ControlRig->GetClass()->ClassGeneratedBy))
				{
					Blueprint->PropagatePoseFromInstanceToBP(ControlRig);
				}
			}
#endif
		}
	}
}

// temporarily we just support following types of gizmo
bool IsSupportedControlType(const ERigControlType ControlType)
{
	switch (ControlType)
	{
		case ERigControlType::Float:
		case ERigControlType::Vector2D:
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		{
			return true;
		}
		default:
		{
			break;
		}
	}

	return false;
}

void UDefaultControlRigManipulationLayer::GetGizmoCreationParams(TArray<FGizmoActorCreationParam>& OutCreationParams)
{
	OutCreationParams.Reset();

	// for now we only support FTransform
	for (TWeakObjectPtr<UObject> ManipObject : ManipulatableObjects)
	{
		if (!ManipObject.IsValid())
		{
			continue;
		}

		IControlRigManipulatable* Manipulatable = Cast<IControlRigManipulatable>(ManipObject.Get());
		if (Manipulatable == nullptr)
		{
			checkNoEntry();
			continue;
		}

		const TArray<FRigControl>& Controls = Manipulatable->AvailableControls();
		UControlRigGizmoLibrary* GizmoLibrary = Manipulatable->GetGizmoLibrary();

		for (const FRigControl& Control : Controls)
		{
			if(!Control.bGizmoEnabled)
			{
				continue;
			}
			if (IsSupportedControlType(Control.ControlType))
			{
				FGizmoActorCreationParam Param;
				Param.ManipObj = Manipulatable;
				Param.ControlName = Control.Name;
				Param.SpawnTransform = Manipulatable->GetControlGlobalTransform(Control.Name);
				Param.GizmoTransform = Control.GizmoTransform;

				if (GizmoLibrary)
				{
					if (const FControlRigGizmoDefinition* Gizmo = GizmoLibrary->GetGizmoByName(Control.GizmoName, true /* use default */))
					{
						Param.MeshTransform = Gizmo->Transform;
						Param.StaticMesh = Gizmo->StaticMesh;
						Param.Material = GizmoLibrary->DefaultMaterial;
						Param.ColorParameterName = GizmoLibrary->MaterialColorParameter;
					}
				}

				Param.Color = Control.GizmoColor;
				OutCreationParams.Add(Param);
			}
		}
	}
}

bool UDefaultControlRigManipulationLayer::ModeSupportedByGizmoActor(const AControlRigGizmoActor* GizmoActor, FWidget::EWidgetMode InMode) const
{
	const FControlData* Control = GetControlDataFromGizmo(GizmoActor);
	if (Control)
	{
		const FRigControl* RigControl = Control->ManipObject->FindControl(Control->ControlName);
		if (RigControl)
		{
			if (IsSupportedControlType(RigControl->ControlType))
			{
				switch (InMode)
				{
					case FWidget::WM_Rotate:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Rotator:
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_Translate:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Float:
							case ERigControlType::Vector2D:
							case ERigControlType::Position:
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_Scale:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Scale:
							case ERigControlType::Transform:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
					case FWidget::WM_TranslateRotateZ:
					{
						switch (RigControl->ControlType)
						{
							case ERigControlType::Transform:
							case ERigControlType::TransformNoScale:
							{
								return true;
							}
							default:
							{
								break;
							}
						}
						break;
					}
				}
			}
		}
	}
	return false;
}

void UDefaultControlRigManipulationLayer::TickGizmo(AControlRigGizmoActor* GizmoActor, const FTransform& ComponentTransform)
{
	if (GizmoActor)
	{
		const FControlData* DataPtr = GetControlDataFromGizmo(GizmoActor);
		if (DataPtr)
		{
			FTransform Transform = DataPtr->ManipObject->GetControlGlobalTransform(DataPtr->ControlName);
			GizmoActor->SetActorTransform(Transform * ComponentTransform);

			if (FRigControl* Control = DataPtr->ManipObject->FindControl(DataPtr->ControlName))
			{
				GizmoActor->SetGizmoColor(Control->GizmoColor);
			}
		}
	}
}

const FControlData* UDefaultControlRigManipulationLayer::GetControlDataFromGizmo(const AControlRigGizmoActor* GizmoActor) const
{
	const FControlID* Found = GizmoToControlMap.Find(GizmoActor);

	// for now we only support one to one
	if (Found)
	{
		return &ControlData[*Found];
	}

	return nullptr;
}

AControlRigGizmoActor* UDefaultControlRigManipulationLayer::GetGizmoFromControlName(const FName& ControlName) const
{
	for (auto Iter=GizmoToControlMap.CreateConstIterator(); Iter; ++Iter)
	{
		const FControlID ID= Iter.Value();

		if (ControlData[ID].ControlName == ControlName)
		{
			return Iter.Key();
		}
	}

	return nullptr;
}

void UDefaultControlRigManipulationLayer::TickManipulatableObjects(float DeltaTime)
{
	// tick skeletalmeshcomponent, that's how they update their transform from rig change
	USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent();
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->RefreshBoneTransforms();
		SkeletalMeshComponent->RefreshSlaveComponents();
		SkeletalMeshComponent->UpdateComponentToWorld();
		SkeletalMeshComponent->FinalizeBoneTransform();
		SkeletalMeshComponent->MarkRenderTransformDirty();
		SkeletalMeshComponent->MarkRenderDynamicDataDirty();
	}

	PostPoseUpdate();
}

bool UDefaultControlRigManipulationLayer::GetGlobalTransform(AControlRigGizmoActor* GizmoActor, const FName& ControlName, FTransform& OutTransform) const
{
	const FControlData* Data = GetControlDataFromGizmo(GizmoActor);
	if (Data)
	{
		OutTransform = Data->ManipObject->GetControlGlobalTransform(ControlName);
		return true;
	}

	return false;
}

void UDefaultControlRigManipulationLayer::AddToControlData(AControlRigGizmoActor* GizmoActor, IControlRigManipulatable* InManipulatableObject, const FName& InControlName)
{
	FControlID NewID = ControlData.AddUninitialized();
	FControlData& NewManipulatable = ControlData[NewID];
	NewManipulatable.ManipObject = InManipulatableObject;
	NewManipulatable.ControlName = InControlName;

	GizmoToControlMap.Add(GizmoActor, NewID);
}

void UDefaultControlRigManipulationLayer::ResetControlData()
{
	GizmoToControlMap.Reset();
	ControlData.Reset();
}

bool UDefaultControlRigManipulationLayer::CreateGizmoActors(UWorld* World, TArray<AControlRigGizmoActor*>& OutGizmoActors)
{
	DestroyGizmosActors();

	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.bTemporaryEditorActor = true;

	TArray<FGizmoActorCreationParam> Params;
	GetGizmoCreationParams(Params);

	OutGizmoActors.Reset();

	for (const FGizmoActorCreationParam& Param : Params)
	{
		AControlRigGizmoActor* GizmoActor = FControlRigGizmoHelper::CreateDefaultGizmoActor(World, Param);
		if (GizmoActor)
		{
			AddToControlData(GizmoActor, Param.ManipObj, Param.ControlName);
			OutGizmoActors.Add(GizmoActor);
		}
	}

	WorldPtr = World;
	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UDefaultControlRigManipulationLayer::OnWorldCleanup);
	return (OutGizmoActors.Num() > 0);
}

void UDefaultControlRigManipulationLayer::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// if world gets cleaned up first, we destroy gizmo actors
	if (WorldPtr == World)
	{
		DestroyGizmosActors();
	}
}

void UDefaultControlRigManipulationLayer::DestroyGizmosActors()
{
	// clear previous gizmo actors
	// since this is creating, we also have to destroy
	for (auto Iter = GizmoToControlMap.CreateIterator(); Iter; ++Iter)
	{
		AControlRigGizmoActor* GizmoActor = Iter.Key();
		UWorld* World = GizmoActor->GetWorld();
		if (World)
		{
			World->DestroyActor(GizmoActor);
		}
	}

	ResetControlData();

	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
}

void UDefaultControlRigManipulationLayer::OnControlModified(IControlRigManipulatable* InManipulatable, const FRigControl& InControl, EControlRigSetKey InSetKey)
{
	if (UControlRig* ControlRig = static_cast<UControlRig*>(InManipulatable))
	{
		FTransform ComponentTransform = GetSkeletalMeshComponentTransform();
		if (AControlRigGizmoActor* const* Actor = GizmoToControlMap.FindKey(InControl.Index))
		{
			TickGizmo(*Actor, ComponentTransform);
		}
	}
}

TSharedPtr<IControlRigObjectBinding> UDefaultControlRigManipulationLayer::GetObjectBinding() const
{
	for (TWeakObjectPtr<UObject> Manip : ManipulatableObjects)
	{
		if(UControlRig* ControlRig = Cast<UControlRig>(Manip))
		{
			return ControlRig->GetObjectBinding();
		}
	}

	return TSharedPtr<IControlRigObjectBinding>();
}

void UDefaultControlRigManipulationLayer::SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding)
{
	for (TWeakObjectPtr<UObject> Manip : ManipulatableObjects)
	{
		if (UControlRig* ControlRig = Cast<UControlRig>(Manip))
		{
			ControlRig->SetObjectBinding(InObjectBinding);
		}
	}
}

USkeletalMeshComponent* UDefaultControlRigManipulationLayer::GetSkeletalMeshComponent() const
{
	TSharedPtr<IControlRigObjectBinding> ObjectBinding = GetObjectBinding();
	if (ObjectBinding.IsValid())
	{
		return Cast<USkeletalMeshComponent>(ObjectBinding->GetBoundObject());
	}

	return nullptr;
}

FTransform	UDefaultControlRigManipulationLayer::GetSkeletalMeshComponentTransform() const
{
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	return MeshComponent ? MeshComponent->GetComponentTransform() : FTransform::Identity;
}

void UDefaultControlRigManipulationLayer::PostPoseUpdate()
{
	FTransform ComponentTransform = GetSkeletalMeshComponentTransform();
	// after executing rig, update gizmo
	for (auto Iter = GizmoToControlMap.CreateIterator(); Iter; ++Iter)
	{
		TickGizmo(Iter.Key(), ComponentTransform);
	}
}

void UDefaultControlRigManipulationLayer::OnPoseInitialized()
{
	// broadcast delegates
	OnAnimSystemInitialized.Broadcast();
}

#undef LOCTEXT_NAMESPACE
