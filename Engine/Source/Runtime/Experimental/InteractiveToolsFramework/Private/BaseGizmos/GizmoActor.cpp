// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoActor.h"

#include "BaseGizmos/GizmoArrowComponent.h"
#include "BaseGizmos/GizmoRectangleComponent.h"
#include "BaseGizmos/GizmoCircleComponent.h"
#include "BaseGizmos/GizmoBoxComponent.h"
#include "BaseGizmos/GizmoLineHandleComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"


#define LOCTEXT_NAMESPACE "UTransformGizmo"


AGizmoActor::AGizmoActor()
{
	// generally gizmo actor creation/destruction should not be transacted
	ClearFlags(RF_Transactional);

#if WITH_EDITORONLY_DATA
	// hide this actor in the scene outliner
	bListedInSceneOutliner = false;
#endif
}


UGizmoArrowComponent* AGizmoActor::AddDefaultArrowComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& LocalDirection, const float Length)
{
	UGizmoArrowComponent* NewArrow = NewObject<UGizmoArrowComponent>(Actor);
	Actor->AddInstanceComponent(NewArrow);
	NewArrow->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewArrow->Direction = LocalDirection;
	NewArrow->Color = Color;
	NewArrow->Length = Length;
	NewArrow->RegisterComponent();
	return NewArrow;
}



UGizmoRectangleComponent* AGizmoActor::AddDefaultRectangleComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& PlaneAxis1, const FVector& PlaneAxisx2)
{
	UGizmoRectangleComponent* NewRectangle = NewObject<UGizmoRectangleComponent>(Actor);
	Actor->AddInstanceComponent(NewRectangle);
	NewRectangle->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewRectangle->DirectionX = PlaneAxis1;
	NewRectangle->DirectionY = PlaneAxisx2;
	NewRectangle->Color = Color;
	NewRectangle->LengthX = NewRectangle->LengthY = 30.0f;
	NewRectangle->SegmentFlags = 0x2 | 0x4;
	NewRectangle->RegisterComponent();
	return NewRectangle;
}


UGizmoCircleComponent* AGizmoActor::AddDefaultCircleComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& PlaneNormal, float Radius)
{
	UGizmoCircleComponent* NewCircle = NewObject<UGizmoCircleComponent>(Actor);
	Actor->AddInstanceComponent(NewCircle);
	NewCircle->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewCircle->Normal = PlaneNormal;
	NewCircle->Color = Color;
	NewCircle->Radius = Radius;
	NewCircle->RegisterComponent();
	return NewCircle;
}



UGizmoBoxComponent* AGizmoActor::AddDefaultBoxComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& Origin,
	const FVector& Dimensions
)
{
	UGizmoBoxComponent* NewBox = NewObject<UGizmoBoxComponent>(Actor);
	Actor->AddInstanceComponent(NewBox);
	NewBox->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewBox->Origin = Origin;
	NewBox->Color = Color;
	NewBox->Dimensions = Dimensions;
	NewBox->RegisterComponent();
	return NewBox;
}


UGizmoLineHandleComponent* AGizmoActor::AddDefaultLineHandleComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& HandleNormal, const FVector& LocalDirection,
    const float Length, const bool bImageScale)
{
	UGizmoLineHandleComponent* LineHandle = NewObject<UGizmoLineHandleComponent>(Actor);
	Actor->AddInstanceComponent(LineHandle);
	LineHandle->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	LineHandle->Normal = HandleNormal;
	LineHandle->Direction = LocalDirection;
	LineHandle->Length = Length;
	LineHandle->bImageScale = bImageScale;
	LineHandle->Color = Color;
	LineHandle->RegisterComponent();
	return LineHandle;
}

#undef LOCTEXT_NAMESPACE