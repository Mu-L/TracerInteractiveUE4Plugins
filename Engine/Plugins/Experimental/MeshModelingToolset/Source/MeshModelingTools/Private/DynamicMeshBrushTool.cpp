// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshBrushTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

// localization namespace
#define LOCTEXT_NAMESPACE "UDynamicMeshBrushTool"


/*
 * Tool
 */

UDynamicMeshBrushTool::UDynamicMeshBrushTool()
{
}


void UDynamicMeshBrushTool::Setup()
{
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = true;
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	// initialize from LOD-0 MeshDescription
	PreviewMesh->InitializeMesh(ComponentTarget->GetMesh());
	OnBaseMeshComponentChangedHandle = PreviewMesh->GetOnMeshChanged().Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDynamicMeshBrushTool::OnBaseMeshComponentChanged));

	// call this here so that base tool can estimate target dimension
	InputMeshBoundsLocal = PreviewMesh->GetPreviewDynamicMesh()->GetBounds();
	double ScaledDim = ComponentTarget->GetWorldTransform().TransformVector(FVector::OneVector).Size();
	this->WorldToLocalScale = FMathd::Sqrt3 / FMathd::Max(FMathf::ZeroTolerance, ScaledDim);
	UBaseBrushTool::Setup();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);
}



double UDynamicMeshBrushTool::EstimateMaximumTargetDimension()
{
	return InputMeshBoundsLocal.MaxDim();
}


void UDynamicMeshBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	UBaseBrushTool::Shutdown(ShutdownType);

	ComponentTarget->SetOwnerVisibility(true);

	if (PreviewMesh != nullptr)
	{
		PreviewMesh->GetOnMeshChanged().Remove(OnBaseMeshComponentChangedHandle);

		OnShutdown(ShutdownType);

		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

}




bool UDynamicMeshBrushTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	return PreviewMesh->FindRayIntersection(FRay3d(Ray), OutHit);
}






#undef LOCTEXT_NAMESPACE
