// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorMode.h"
#include "ModelingToolsEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorViewportClient.h"

//#include "SingleClickTool.h"
//#include "MeshSurfacePointTool.h"
//#include "MeshVertexDragTool.h"
#include "DynamicMeshSculptTool.h"
#include "EditMeshPolygonsTool.h"
#include "DeformMeshPolygonsTool.h"
#include "ConvertToPolygonsTool.h"
#include "AddPrimitiveTool.h"
#include "AddPatchTool.h"
#include "SmoothMeshTool.h"
#include "RemeshMeshTool.h"
#include "SimplifyMeshTool.h"
#include "MeshInspectorTool.h"
#include "WeldMeshEdgesTool.h"
#include "DrawPolygonTool.h"
#include "DrawPolyPathTool.h"
#include "ShapeSprayTool.h"
#include "MergeMeshesTool.h"
#include "VoxelCSGMeshesTool.h"
#include "PlaneCutTool.h"
#include "PolygonOnMeshTool.h"
#include "DisplaceMeshTool.h"
#include "MeshSpaceDeformerTool.h"
#include "EditNormalsTool.h"
#include "RemoveOccludedTrianglesTool.h"
#include "AttributeEditorTool.h"
#include "TransformMeshesTool.h"
#include "MeshSelectionTool.h"
#include "UVProjectionTool.h"
#include "UVLayoutTool.h"
#include "EditMeshMaterialsTool.h"
#include "EditPivotTool.h"
#include "BakeTransformTool.h"
#include "CombineMeshesTool.h"
#include "AlignObjectsTool.h"
#include "EditUVIslandsTool.h"

#include "ParameterizeMeshTool.h"

#include "EditorModeManager.h"

// stylus support
#include "IStylusInputModule.h"

#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"

#include "ModelingToolsActions.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingModeAssetAPI.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorMode"


//#define ENABLE_DEBUG_PRINTING

const FEditorModeID FModelingToolsEditorMode::EM_ModelingToolsEditorModeId = TEXT("EM_ModelingToolsEditorMode");

FModelingToolsEditorMode::FModelingToolsEditorMode()
{
	ToolsContext = nullptr;

	UICommandList = MakeShareable(new FUICommandList);
}

FModelingToolsEditorMode::~FModelingToolsEditorMode()
{
	if (ToolsContext != nullptr)
	{
		ToolsContext->ShutdownContext();
		ToolsContext = nullptr;
	}
}


void FModelingToolsEditorMode::ActorSelectionChangeNotify()
{
}


bool FModelingToolsEditorMode::ProcessEditDelete()
{
	if (ToolsContext->ProcessEditDelete())
	{
		return true;
	}

	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if ( GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept() )
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotDeleteWarning", "Cannot delete objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}

	return false;
}


bool FModelingToolsEditorMode::ProcessEditCut()
{
	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if (GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept())
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotCutWarning", "Cannot cut objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}
	return false;
}



bool FModelingToolsEditorMode::CanAutoSave() const
{
	// prevent autosave if any tool is active
	return ToolsContext->ToolManager->HasAnyActiveTool() == false;
}

bool FModelingToolsEditorMode::ShouldDrawWidget() const
{ 
	// allow standard xform gizmo if we don't have an active tool
	if (ToolsContext != nullptr && ToolsContext->ToolManager->HasAnyActiveTool())
	{
		return false;
	}
	return FEdMode::ShouldDrawWidget(); 
}

bool FModelingToolsEditorMode::UsesTransformWidget() const
{ 
	return true; 
}


void FModelingToolsEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (ToolsContext != nullptr)
	{
		ToolsContext->Tick(ViewportClient, DeltaTime);
	}

	if (Toolkit.IsValid())
	{
		FModelingToolsEditorModeToolkit* ModelingToolkit = (FModelingToolsEditorModeToolkit*)Toolkit.Get();
		ModelingToolkit->EnableShowRealtimeWarning(ViewportClient->IsRealtime() == false);
	}
}




void FModelingToolsEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	// we do not use PDI hit testing in modeling tools, so skip these render passes
	if (PDI->IsHitTesting())
	{
		return;
	}

	if (ToolsContext != nullptr)
	{
		ToolsContext->Render(View, Viewport, PDI);
	}
}





bool FModelingToolsEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// try hotkeys
	if (Event != IE_Released)
	{
		if (ToolsContext->ShouldIgnoreHotkeys() == false)		// allow the context to capture keyboard input if necessary
		{
			if (UICommandList->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
			{
				return true;
			}
		}
	}

	bool bHandled = ToolsContext->InputKey(ViewportClient, Viewport, Key, Event);
	if (bHandled == false)
	{
		bHandled = FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	}
	return bHandled;

	//bool bHandled = FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	//bHandled |= ToolsContext->InputKey(ViewportClient, Viewport, Key, Event);
	//return bHandled;
}


bool FModelingToolsEditorMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	// mouse axes: EKeys::MouseX, EKeys::MouseY, EKeys::MouseWheelAxis
	return FEdMode::InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}





bool FModelingToolsEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{ 
	bool bHandled = FEdMode::StartTracking(InViewportClient, InViewport);
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("START TRACKING - base handled was %d"), (int)bHandled);
#endif

	bHandled |= ToolsContext->StartTracking(InViewportClient, InViewport);

	return bHandled;
}

bool FModelingToolsEditorMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	bool bHandled = ToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	return bHandled;
}


bool FModelingToolsEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bHandled = ToolsContext->EndTracking(InViewportClient, InViewport);
	return bHandled;
}








bool FModelingToolsEditorMode::ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("RECEIVED FOCUS"));
#endif

	return false;
}

bool FModelingToolsEditorMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("LOST FOCUS"));
#endif

	return false;
}



bool FModelingToolsEditorMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseEnter(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FModelingToolsEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseMove(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FModelingToolsEditorMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	bool bHandled = ToolsContext->MouseLeave(ViewportClient, Viewport);
	return bHandled;
}




//
// FStylusStateTracker registers itself as a listener for stylus events and implements
// the IToolStylusStateProviderAPI interface, which allows MeshSurfacePointTool implementations
// to query for the pen pressure. 
//
// This is kind of a hack. Unfortunately the current Stylus module is a Plugin so it
// cannot be used in the base ToolsFramework, and we need this in the Mode as a workaround.
//
class FStylusStateTracker : public IStylusMessageHandler, public IToolStylusStateProviderAPI
{
public:
	const IStylusInputDevice* ActiveDevice = nullptr;
	int32 ActiveDeviceIndex = -1;

	bool bPenDown = false;
	float ActivePressure = 1.0;

	FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->AddMessageHandler(*this);

		ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
		bPenDown = false;
	}

	virtual ~FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->RemoveMessageHandler(*this);
	}

	virtual void OnStylusStateChanged(const FStylusState& NewState, int32 StylusIndex) override
	{
		if (ActiveDevice == nullptr)
		{
			UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
			ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
			bPenDown = false;
		}
		if (ActiveDevice != nullptr && ActiveDeviceIndex == StylusIndex)
		{
			bPenDown = NewState.IsStylusDown();
			ActivePressure = NewState.GetPressure();
		}
	}


	bool HaveActiveStylusState() const
	{
		return ActiveDevice != nullptr && bPenDown;
	}

	static const IStylusInputDevice* FindFirstPenDevice(const UStylusInputSubsystem* StylusSubsystem, int32& ActiveDeviceOut)
	{
		int32 NumDevices = StylusSubsystem->NumInputDevices();
		for (int32 k = 0; k < NumDevices; ++k)
		{
			const IStylusInputDevice* Device = StylusSubsystem->GetInputDevice(k);
			const TArray<EStylusInputType>& Inputs = Device->GetSupportedInputs();
			for (EStylusInputType Input : Inputs)
			{
				if (Input == EStylusInputType::Pressure)
				{
					ActiveDeviceOut = k;
					return Device;
				}
			}
		}
		return nullptr;
	}



	// IToolStylusStateProviderAPI implementation
	virtual float GetCurrentPressure() const override
	{
		return (ActiveDevice != nullptr && bPenDown) ? ActivePressure : 1.0f;
	}

};





void FModelingToolsEditorMode::Enter()
{
	FEdMode::Enter();

	// initialize FEdMode ToolsContext adapter

	//ToolsContext = NewObject<UEdModeInteractiveToolsContext>(GetTransientPackage(), TEXT("ToolsContext"), RF_Transient);
	ToolsContext = NewObject<UEdModeInteractiveToolsContext>();
	ToolsContext->InitializeContextFromEdMode(this, new FModelingModeAssetAPI);

	ToolsContext->OnToolNotificationMessage.AddLambda([this](const FText& Message)
	{
		this->OnToolNotificationMessage.Broadcast(Message);
	});
	ToolsContext->OnToolWarningMessage.AddLambda([this](const FText& Message)
	{
		this->OnToolWarningMessage.Broadcast(Message);
	});

	// register stylus event handler
	StylusStateTracker = MakeUnique<FStylusStateTracker>();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FModelingToolsEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());


		const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
		const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

		CommandList->MapAction(
			ToolManagerCommands.AcceptActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Accept); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanAcceptActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

		CommandList->MapAction(
			ToolManagerCommands.CancelActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Cancel); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCancelActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

		CommandList->MapAction(
			ToolManagerCommands.CompleteActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Completed); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCompleteActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->CanCompleteActiveTool(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

	}

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	auto RegisterToolFunc = [this, &CommandList](TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
	{
		ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
		CommandList->MapAction( UICommand,
			FExecuteAction::CreateLambda([this, ToolIdentifier]() { ToolsContext->StartTool(ToolIdentifier); }),
			FCanExecuteAction::CreateLambda([this, ToolIdentifier]() { return ToolsContext->CanStartTool(ToolIdentifier); }));
	};


	// register tool set

	//
	// make shape tools
	//
	auto AddPrimitiveToolBuilder = NewObject<UAddPrimitiveToolBuilder>();
	AddPrimitiveToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginAddPrimitiveTool, TEXT("AddPrimitiveTool"), AddPrimitiveToolBuilder);

	auto AddPatchToolBuilder = NewObject<UAddPatchToolBuilder>();
	AddPatchToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginAddPatchTool, TEXT("AddPatchTool"), AddPatchToolBuilder);

	auto DrawPolygonToolBuilder = NewObject<UDrawPolygonToolBuilder>();
	DrawPolygonToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginDrawPolygonTool, TEXT("DrawPolygonTool"), DrawPolygonToolBuilder);

	auto DrawPolyPathToolBuilder = NewObject<UDrawPolyPathToolBuilder>();
	DrawPolyPathToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginDrawPolyPathTool, TEXT("DrawPolyPath"), DrawPolyPathToolBuilder);

	auto ShapeSprayToolBuilder = NewObject<UShapeSprayToolBuilder>();
	ShapeSprayToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginShapeSprayTool, TEXT("ShapeSprayTool"), ShapeSprayToolBuilder);


	//
	// vertex deform tools
	// 

	auto MoveVerticesToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	MoveVerticesToolBuilder->bEnableRemeshing = false;
	MoveVerticesToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterToolFunc(ToolManagerCommands.BeginSculptMeshTool, TEXT("MoveVerticesTool"), MoveVerticesToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginPolyEditTool, TEXT("EditMeshPolygonsTool"), NewObject<UEditMeshPolygonsToolBuilder>());
	UEditMeshPolygonsToolBuilder* TriEditBuilder = NewObject<UEditMeshPolygonsToolBuilder>();
	TriEditBuilder->bTriangleMode = true;
	RegisterToolFunc(ToolManagerCommands.BeginTriEditTool, TEXT("EditMeshTrianglesTool"), TriEditBuilder);
	RegisterToolFunc(ToolManagerCommands.BeginPolyDeformTool, TEXT("DeformMeshPolygonsTool"), NewObject<UDeformMeshPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSmoothMeshTool, TEXT("SmoothMeshTool"), NewObject<USmoothMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginDisplaceMeshTool, TEXT("DisplaceMeshTool"), NewObject<UDisplaceMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginMeshSpaceDeformerTool, TEXT("MeshSpaceDeformerTool"), NewObject<UMeshSpaceDeformerToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginTransformMeshesTool, TEXT("TransformMeshesTool"), NewObject<UTransformMeshesToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginEditPivotTool, TEXT("EditPivotTool"), NewObject<UEditPivotToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginAlignObjectsTool, TEXT("AlignObjects"), NewObject<UAlignObjectsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginBakeTransformTool, TEXT("BakeTransformTool"), NewObject<UBakeTransformToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginTransformUVIslandsTool, TEXT("EditUVIslands"), NewObject<UEditUVIslandsToolBuilder>());

	UCombineMeshesToolBuilder* CombineMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	CombineMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginCombineMeshesTool, TEXT("CombineMeshesTool"), CombineMeshesToolBuilder);

	UCombineMeshesToolBuilder* DuplicateMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	DuplicateMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	DuplicateMeshesToolBuilder->bIsDuplicateTool = true;
	RegisterToolFunc(ToolManagerCommands.BeginDuplicateMeshesTool, TEXT("DuplicateMeshesTool"), DuplicateMeshesToolBuilder);


	// edit tools


	auto DynaSculptToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	DynaSculptToolBuilder->bEnableRemeshing = true;
	DynaSculptToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterToolFunc(ToolManagerCommands.BeginRemeshSculptMeshTool, TEXT("DynaSculptTool"), DynaSculptToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginRemeshMeshTool, TEXT("RemeshMeshTool"), NewObject<URemeshMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSimplifyMeshTool, TEXT("SimplifyMeshTool"), NewObject<USimplifyMeshToolBuilder>());


	auto EditNormalsToolBuilder = NewObject<UEditNormalsToolBuilder>();
	EditNormalsToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginEditNormalsTool, TEXT("EditNormalsTool"), EditNormalsToolBuilder);

	auto RemoveOccludedTrianglesToolBuilder = NewObject<URemoveOccludedTrianglesToolBuilder>();
	RemoveOccludedTrianglesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginRemoveOccludedTrianglesTool, TEXT("RemoveOccludedTrianglesTool"), RemoveOccludedTrianglesToolBuilder);

	auto UVProjectionToolBuilder = NewObject<UUVProjectionToolBuilder>();
	UVProjectionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginUVProjectionTool, TEXT("UVProjectionTool"), UVProjectionToolBuilder);

	auto UVLayoutToolBuilder = NewObject<UUVLayoutToolBuilder>();
	UVLayoutToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginUVLayoutTool, TEXT("UVLayoutTool"), UVLayoutToolBuilder);

	auto MergeMeshesToolBuilder = NewObject<UMergeMeshesToolBuilder>();
	MergeMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelMergeTool, TEXT("MergeMeshesTool"), MergeMeshesToolBuilder);

	auto VoxelCSGMeshesToolBuilder = NewObject<UVoxelCSGMeshesToolBuilder>();
	VoxelCSGMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelBooleanTool, TEXT("VoxelCSGMeshesTool"), VoxelCSGMeshesToolBuilder);

	auto PlaneCutToolBuilder = NewObject<UPlaneCutToolBuilder>();
	PlaneCutToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginPlaneCutTool, TEXT("PlaneCutTool"), PlaneCutToolBuilder);

	auto PolygonCutToolBuilder = NewObject<UPolygonOnMeshToolBuilder>();
	RegisterToolFunc(ToolManagerCommands.BeginPolygonCutTool, TEXT("PolyCutTool"), PolygonCutToolBuilder);

	auto GlobalUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GlobalUVGenerateToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	GlobalUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = true;
	RegisterToolFunc(ToolManagerCommands.BeginGlobalUVGenerateTool, TEXT("GlobalParameterizeMeshTool"), GlobalUVGenerateToolBuilder);

	auto GroupUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GroupUVGenerateToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	GroupUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = false;
	RegisterToolFunc(ToolManagerCommands.BeginGroupUVGenerateTool, TEXT("GroupParameterizeMeshTool"), GroupUVGenerateToolBuilder);

	auto MeshSelectionToolBuilder = NewObject<UMeshSelectionToolBuilder>();
	MeshSelectionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginMeshSelectionTool, TEXT("MeshSelectionTool"), MeshSelectionToolBuilder);

	auto EditMeshMaterialsToolBuilder = NewObject<UEditMeshMaterialsToolBuilder>();
	EditMeshMaterialsToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginEditMeshMaterialsTool, TEXT("EditMaterialsTool"), EditMeshMaterialsToolBuilder);
	

	// analysis tools

	RegisterToolFunc(ToolManagerCommands.BeginMeshInspectorTool, TEXT("MeshInspectorTool"), NewObject<UMeshInspectorToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginWeldEdgesTool, TEXT("WeldMeshEdgesTool"), NewObject<UWeldMeshEdgesToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginPolyGroupsTool, TEXT("ConvertToPolygonsTool"), NewObject<UConvertToPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginAttributeEditorTool, TEXT("AttributeEditorTool"), NewObject<UAttributeEditorToolBuilder>());



	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("DynaSculptTool"));

	// register modeling mode hotkeys
	FModelingModeActionCommands::RegisterCommandBindings(UICommandList, [this](EModelingModeActionCommands Command) {
		ModelingModeShortcutRequested(Command);
	});

	// listen for Tool start/end events to bind/unbind any hotkeys relevant to that Tool
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, false);
	});
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, true);
	});

	// enable realtime viewport override
	ConfigureRealTimeViewportsOverride(true);
}



void FModelingToolsEditorMode::Exit()
{
	OnToolNotificationMessage.Clear();
	OnToolWarningMessage.Clear();


	StylusStateTracker = nullptr;

	ToolsContext->ShutdownContext();
	ToolsContext = nullptr;

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	// clear realtime viewport override
	ConfigureRealTimeViewportsOverride(false);

	// Call base Exit method to ensure proper cleanup
	FEdMode::Exit();
}

bool FModelingToolsEditorMode::UsesToolkits() const
{
	return true;
}


void FModelingToolsEditorMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ToolsContext);
}


void FModelingToolsEditorMode::ModelingModeShortcutRequested(EModelingModeActionCommands Command)
{
	if (Command == EModelingModeActionCommands::FocusViewToCursor)
	{
		FocusCameraAtCursorHotkey();
	}
}


void FModelingToolsEditorMode::FocusCameraAtCursorHotkey()
{
	FRay Ray = ToolsContext->GetLastWorldRay();

	FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.bTraceComplex = true;
	FHitResult HitResult;
	bool bHitWorld = GetWorld()->LineTraceSingleByObjectType(HitResult, Ray.Origin, Ray.PointAt(HALF_WORLD_MAX), ObjectQueryParams, QueryParams);
	if (bHitWorld)
	{
		FVector HitPoint = HitResult.ImpactPoint;
		if (GCurrentLevelEditingViewportClient)
		{
			GCurrentLevelEditingViewportClient->CenterViewportAtPoint(HitPoint, false);
		}
	}
}


bool FModelingToolsEditorMode::GetPivotForOrbit(FVector& OutPivot) const
{
	if (GCurrentLevelEditingViewportClient)
	{
		OutPivot = GCurrentLevelEditingViewportClient->GetViewTransform().GetLookAt();
		return true;
	}
	return false;
}



void FModelingToolsEditorMode::ConfigureRealTimeViewportsOverride(bool bEnable)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<IAssetViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<IAssetViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetAssetViewportClient();
				if (bEnable)
				{
					Viewport.SetRealtimeOverride(bEnable, LOCTEXT("RealtimeOverrideMessage_ModelingMode", "Modeling Mode"));
				}
				else
				{
					Viewport.RemoveRealtimeOverride();
				}
			}
		}
	}
}



#undef LOCTEXT_NAMESPACE