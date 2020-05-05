// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizeMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "FaceGroupUtil.h"

#include "SimpleDynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "ParameterizationOps/ParameterizeMeshOp.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UParameterizeMeshTool"


DEFINE_LOG_CATEGORY_STATIC(LogParameterizeMeshTool, Log, All);

/*
 * ToolBuilder
 */

bool UParameterizeMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UParameterizeMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UParameterizeMeshTool* NewTool = NewObject<UParameterizeMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	NewTool->SetUseAutoGlobalParameterizationMode(bDoAutomaticGlobalUnwrap);

	return NewTool;
}





void UParameterizeMeshToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UParameterizeMeshToolProperties* PropertyCache = GetPropertyCache<UParameterizeMeshToolProperties>();
	PropertyCache->ChartStretch = this->ChartStretch;
	//PropertyCache->IslandMode = this->IslandMode;
	PropertyCache->UnwrapType = this->UnwrapType;
	PropertyCache->UVScaleMode = this->UVScaleMode;
	PropertyCache->UVScale = this->UVScale;
}

void UParameterizeMeshToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UParameterizeMeshToolProperties* PropertyCache = GetPropertyCache<UParameterizeMeshToolProperties>();
	this->ChartStretch = PropertyCache->ChartStretch;
	//this->IslandMode = PropertyCache->IslandMode;
	this->UnwrapType = PropertyCache->UnwrapType;
	this->UVScaleMode = PropertyCache->UVScaleMode;
	this->UVScale = PropertyCache->UVScale;
}




/*
 * Tool
 */

UParameterizeMeshTool::UParameterizeMeshTool()
{
}

void UParameterizeMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UParameterizeMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

void UParameterizeMeshTool::SetUseAutoGlobalParameterizationMode(bool bEnable)
{
	bDoAutomaticGlobalUnwrap = bEnable;
}


void UParameterizeMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// Deep copy of input mesh to be shared with the UV generation tool.
	InputMesh = MakeShared<FMeshDescription>(*ComponentTarget->GetMesh());

	// Copy existing material if there is one	
	DefaultMaterial = ComponentTarget->GetMaterial(0);
	if (DefaultMaterial == nullptr)
	{
		DefaultMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));  
	}

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// Construct the preview object and set the material on it
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	// Initialize the preview mesh with a copy of the source mesh.
	bool bHasGroups = false;
	{
		FDynamicMesh3 Mesh;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(InputMesh.Get(), Mesh);
		bHasGroups = FaceGroupUtil::HasMultipleGroups(Mesh);

		FComponentMaterialSet MaterialSet;
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		Preview->PreviewMesh->UpdatePreview(&Mesh);
		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	}

	if (bDoAutomaticGlobalUnwrap == false && bHasGroups == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("NoGroupsWarning", "This mesh has no PolyGroups!"),
			EToolMessageLevel::UserWarning);
		//bDoAutomaticGlobalUnwrap = true;
	}

	// initialize our properties
	Settings = NewObject<UParameterizeMeshToolProperties>(this);
	Settings->RestoreProperties(this);
	Settings->bIsGlobalMode = bDoAutomaticGlobalUnwrap;
	AddToolPropertySource(Settings);


	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->RestoreProperties(this);
	AddToolPropertySource(MaterialSettings);
	// force update
	MaterialSettings->UpdateMaterials();
	Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();


	Preview->SetVisibility(true);
	Preview->InvalidateResult();    // start compute
}

void UParameterizeMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == MaterialSettings)
	{
		MaterialSettings->UpdateMaterials();
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}
	if (PropertySet == Settings)
	{
		// One of the UV generation properties must have changed.  Dirty the result to force a recompute
		Preview->InvalidateResult();
	}
}


void UParameterizeMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	MaterialSettings->SaveProperties(this);
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
		check(DynamicMeshResult != nullptr);
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ParameterizeMesh", "Parameterize Mesh"));

		ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
		});
		GetToolManager()->EndUndoTransaction();
	}

	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

}

void UParameterizeMeshTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void UParameterizeMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

bool UParameterizeMeshTool::HasAccept() const
{
	return true;
}

bool UParameterizeMeshTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

TUniquePtr<FDynamicMeshOperator> UParameterizeMeshTool::MakeNewOperator()
{
	FAxisAlignedBox3d MeshBounds = Preview->PreviewMesh->GetMesh()->GetBounds();
	TUniquePtr<FParameterizeMeshOp> ParamertizeMeshOp = MakeUnique<FParameterizeMeshOp>();
	ParamertizeMeshOp->Stretch   = Settings->ChartStretch;
	ParamertizeMeshOp->NumCharts = 0;
	ParamertizeMeshOp->InputMesh = InputMesh;
	
	if (bDoAutomaticGlobalUnwrap)
	{
		ParamertizeMeshOp->IslandMode = EParamOpIslandMode::Auto;
		ParamertizeMeshOp->UnwrapType = EParamOpUnwrapType::MinStretch;
	}
	else
	{
		ParamertizeMeshOp->IslandMode = EParamOpIslandMode::PolyGroups;		// (EParamOpIslandMode)(int)Settings->IslandMode;
		ParamertizeMeshOp->UnwrapType = (EParamOpUnwrapType)(int)Settings->UnwrapType;
	}

	switch (Settings->UVScaleMode)
	{
		case EParameterizeMeshToolUVScaleMode::NoScaling:
			ParamertizeMeshOp->bNormalizeAreas = false;
			ParamertizeMeshOp->AreaScaling = 1.0;
			break;
		case EParameterizeMeshToolUVScaleMode::NormalizeToBounds:
			ParamertizeMeshOp->bNormalizeAreas = true;
			ParamertizeMeshOp->AreaScaling = Settings->UVScale / MeshBounds.MaxDim();
			break;
		case EParameterizeMeshToolUVScaleMode::NormalizeToWorld:
			ParamertizeMeshOp->bNormalizeAreas = true;
			ParamertizeMeshOp->AreaScaling = Settings->UVScale;
			break;
	}

	const FTransform XForm = ComponentTarget->GetWorldTransform();
	FTransform3d XForm3d(XForm);
	ParamertizeMeshOp->SetTransform(XForm3d);

	return ParamertizeMeshOp;
}



#undef LOCTEXT_NAMESPACE
