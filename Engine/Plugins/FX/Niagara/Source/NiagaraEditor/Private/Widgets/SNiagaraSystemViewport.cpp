// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraSystemViewport.h"
#include "Widgets/Layout/SBox.h"
#include "Editor/UnrealEdEngine.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UnrealEdGlobals.h"
#include "NiagaraComponent.h"
#include "ComponentReregisterContext.h"
#include "NiagaraEditorModule.h"
#include "Slate/SceneViewport.h"
#include "NiagaraSystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Engine/TextureCube.h"
#include "SNiagaraSystemViewportToolBar.h"
#include "NiagaraEditorCommands.h"
#include "EditorViewportCommands.h"
#include "AdvancedPreviewScene.h"
#include "ImageUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "DrawDebugHelpers.h"

#define LOCTEXT_NAMESPACE "SNiagaraSystemViewport"

/** Viewport Client for the preview viewport */
class FNiagaraSystemViewportClient : public FEditorViewportClient
{
public:
	DECLARE_DELEGATE_OneParam(FOnScreenShotCaptured, UTexture2D*);

public:
	FNiagaraSystemViewportClient(FAdvancedPreviewScene& InPreviewScene, const TSharedRef<SNiagaraSystemViewport>& InNiagaraEditorViewport, FOnScreenShotCaptured InOnScreenShotCaptured);
	
	// FEditorViewportClient interface
	virtual FLinearColor GetBackgroundColor() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(FViewport* Viewport,FCanvas* Canvas) override;
	virtual bool ShouldOrbitCamera() const override;
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const EStereoscopicPass StereoPass = eSSP_FULL) override;
	virtual bool CanSetWidgetMode(FWidget::EWidgetMode NewMode) const override { return false; }
	virtual bool CanCycleWidgetMode() const override { return false; }


	virtual void SetIsSimulateInEditorViewport(bool bInIsSimulateInEditorViewport)override;

	void DrawInstructionCounts(UNiagaraSystem* ParticleSystem, FCanvas* Canvas, float& CurrentX, float& CurrentY, UFont* Font, const float FontHeight);
	void DrawParticleCounts(UNiagaraComponent* Component, FCanvas* Canvas, float& CurrentX, float& CurrentY, UFont* Font, const float FontHeight);

	TWeakPtr<SNiagaraSystemViewport> NiagaraViewportPtr;
	bool bCaptureScreenShot;
	TWeakObjectPtr<UObject> ScreenShotOwner;

	FOnScreenShotCaptured OnScreenShotCaptured;
};

FNiagaraSystemViewportClient::FNiagaraSystemViewportClient(FAdvancedPreviewScene& InPreviewScene, const TSharedRef<SNiagaraSystemViewport>& InNiagaraEditorViewport, FOnScreenShotCaptured InOnScreenShotCaptured)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InNiagaraEditorViewport))
	, OnScreenShotCaptured(InOnScreenShotCaptured)
{
	NiagaraViewportPtr = InNiagaraEditorViewport;

	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.GridColorAxis = FColor(80,80,80);
	DrawHelper.GridColorMajor = FColor(72,72,72);
	DrawHelper.GridColorMinor = FColor(64,64,64);
	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;
	ShowWidget(false);

	SetViewMode(VMI_Lit);
	
	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.SetSnap(0);
	
	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;
	bCaptureScreenShot = false;

	//This seems to be needed to get the correct world time in the preview.
	SetIsSimulateInEditorViewport(true);
}


void FNiagaraSystemViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	
	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FNiagaraSystemViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	TSharedPtr<SNiagaraSystemViewport> NiagaraViewport = NiagaraViewportPtr.Pin();
	UNiagaraSystem* ParticleSystem = NiagaraViewport.IsValid() ? NiagaraViewport->GetPreviewComponent()->GetAsset() : nullptr;
	UNiagaraComponent* Component = NiagaraViewport.IsValid() ? NiagaraViewport->GetPreviewComponent() : nullptr;

	if (NiagaraViewport.IsValid() && NiagaraViewport->GetDrawElement(SNiagaraSystemViewport::EDrawElements::Bounds))
	{
		EngineShowFlags.SetBounds(true);
		EngineShowFlags.Game = 1;
	}
	else
	{
		EngineShowFlags.SetBounds(false);
		EngineShowFlags.Game = 0;
	}

	FEditorViewportClient::Draw(InViewport, Canvas);

	if (NiagaraViewport.IsValid() )
	{
		float CurrentX = 10.0f;
		float CurrentY = 50.0f;
		UFont* Font = GEngine->GetSmallFont();
		const float FontHeight = Font->GetMaxCharHeight() * 1.1f;

		if (NiagaraViewport->GetDrawElement(SNiagaraSystemViewport::EDrawElements::InstructionCounts) && ParticleSystem)
		{
			DrawInstructionCounts(ParticleSystem, Canvas, CurrentX, CurrentY, Font, FontHeight);
			CurrentY += FontHeight;
		}
		if (NiagaraViewport->GetDrawElement(SNiagaraSystemViewport::EDrawElements::ParticleCounts) && Component)
		{
			DrawParticleCounts(Component, Canvas, CurrentX, CurrentY, Font, FontHeight);
		}
	}

	if (bCaptureScreenShot && ScreenShotOwner.IsValid() && OnScreenShotCaptured.IsBound())
	{
		
		int32 SrcWidth = InViewport->GetSizeXY().X;
		int32 SrcHeight = InViewport->GetSizeXY().Y;
		// Read the contents of the viewport into an array.
		TArray<FColor> OrigBitmap;
		if (InViewport->ReadPixels(OrigBitmap))
		{
			check(OrigBitmap.Num() == SrcWidth * SrcHeight);

			// Resize image to enforce max size.
			TArray<FColor> ScaledBitmap;
			int32 ScaledWidth = 512;
			int32 ScaledHeight = 512;
			FImageUtils::ImageResize(SrcWidth, SrcHeight, OrigBitmap, ScaledWidth, ScaledHeight, ScaledBitmap, true);

			// Compress.
			FCreateTexture2DParameters Params;
			Params.bDeferCompression = true;
			
			UTexture2D* ThumbnailImage = FImageUtils::CreateTexture2D(ScaledWidth, ScaledHeight, ScaledBitmap, ScreenShotOwner.Get(), TEXT("ThumbnailTexture"), RF_NoFlags, Params);

			OnScreenShotCaptured.Execute(ThumbnailImage);
		}

		bCaptureScreenShot = false;
		ScreenShotOwner.Reset();
	}
}

void FNiagaraSystemViewportClient::DrawInstructionCounts(UNiagaraSystem* ParticleSystem, FCanvas* Canvas, float& CurrentX, float& CurrentY, UFont* Font, const float FontHeight)
{
	Canvas->DrawShadowedString(CurrentX, CurrentY, TEXT("Instruction Counts"), Font, FLinearColor::White);
	CurrentY += FontHeight;

	for (const FNiagaraEmitterHandle& EmitterHandle : ParticleSystem->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
		if (Emitter == nullptr)
		{
			continue;
		}

		Canvas->DrawShadowedString(CurrentX + 10.0f, CurrentY, *FString::Printf(TEXT("Emitter %s"), *EmitterHandle.GetName().ToString()), Font, FLinearColor::White);
		CurrentY += FontHeight;

		TArray<UNiagaraScript*> EmitterScripts;
		Emitter->GetScripts(EmitterScripts);

		for (UNiagaraScript* Script : EmitterScripts)
		{
			uint32 NumInstructions = 0;
			if (Script->GetUsage() == ENiagaraScriptUsage::ParticleGPUComputeScript)
			{
				FNiagaraShaderRef Shader = Script->GetRenderThreadScript()->GetShaderGameThread();
				if (Shader.IsValid())
				{
					NumInstructions = Shader->GetNumInstructions();
				}
			}
			else
			{
				NumInstructions = Script->GetVMExecutableData().LastOpCount;
			}

			if (NumInstructions > 0)
			{
				Canvas->DrawShadowedString(CurrentX + 20.0f, CurrentY, *FString::Printf(TEXT("%s = %u"), *Script->GetName(), NumInstructions), Font, FLinearColor::White);
				CurrentY += FontHeight;
			}
		}
	}
}

void FNiagaraSystemViewportClient::DrawParticleCounts(UNiagaraComponent* Component, FCanvas* Canvas, float& CurrentX, float& CurrentY, UFont* Font, const float FontHeight)
{
	FCanvasTextItem TextItem(FVector2D(CurrentX, CurrentY), FText::FromString(TEXT("Particle Counts")), Font, FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	TextItem.Draw(Canvas);
	CurrentY += FontHeight;

	FNiagaraSystemInstance* SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		return;
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> EmitterInstance : SystemInstance->GetEmitters())
	{
		FName EmitterName = EmitterInstance->GetEmitterHandle().GetName();
		int32 CurrentCount = EmitterInstance->GetNumParticles();
		int32 MaxCount = EmitterInstance->GetEmitterHandle().GetInstance()->GetMaxParticleCountEstimate();
		TextItem.Text = FText::FromString(FString::Printf(TEXT("%i Current, %i Max (est.) - [%s]"), CurrentCount, MaxCount, *EmitterName.ToString()));
		TextItem.Position = FVector2D(CurrentX, CurrentY);
		TextItem.Draw(Canvas);
		CurrentY += FontHeight;
	}
}

bool FNiagaraSystemViewportClient::ShouldOrbitCamera() const
{
	return bUsingOrbitCamera;
}


FLinearColor FNiagaraSystemViewportClient::GetBackgroundColor() const
{
	FLinearColor BackgroundColor = FLinearColor::Black;
	return BackgroundColor;
}

FSceneView* FNiagaraSystemViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily, const EStereoscopicPass StereoPass)
{
	FSceneView* SceneView = FEditorViewportClient::CalcSceneView(ViewFamily);
	FFinalPostProcessSettings::FCubemapEntry& CubemapEntry = *new(SceneView->FinalPostProcessSettings.ContributingCubemaps) FFinalPostProcessSettings::FCubemapEntry;
	CubemapEntry.AmbientCubemap = GUnrealEd->GetThumbnailManager()->AmbientCubemap;
	CubemapEntry.AmbientCubemapTintMulScaleValue = FLinearColor::White;
	return SceneView;
}

void FNiagaraSystemViewportClient::SetIsSimulateInEditorViewport(bool bInIsSimulateInEditorViewport)
{
	bIsSimulateInEditorViewport = bInIsSimulateInEditorViewport;

// 	if (bInIsSimulateInEditorViewport)
// 	{
// 		TSharedRef<FPhysicsManipulationEdModeFactory> Factory = MakeShareable(new FPhysicsManipulationEdModeFactory);
// 		FEditorModeRegistry::Get().RegisterMode(FBuiltinEditorModes::EM_Physics, Factory);
// 	}
// 	else
// 	{
// 		FEditorModeRegistry::Get().UnregisterMode(FBuiltinEditorModes::EM_Physics);
// 	}
}

//////////////////////////////////////////////////////////////////////////

void SNiagaraSystemViewport::Construct(const FArguments& InArgs)
{
	DrawFlags = 0;
	bShowBackground = false;
	PreviewComponent = nullptr;
	AdvancedPreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));
	AdvancedPreviewScene->SetFloorVisibility(false);
	OnThumbnailCaptured = InArgs._OnThumbnailCaptured;

	SEditorViewport::Construct( SEditorViewport::FArguments() );
}

SNiagaraSystemViewport::~SNiagaraSystemViewport()
{
	if (SystemViewportClient.IsValid())
	{
		SystemViewportClient->Viewport = NULL;
	}
}

void SNiagaraSystemViewport::CreateThumbnail(UObject* InScreenShotOwner)
{
	if (SystemViewportClient.IsValid() && PreviewComponent != nullptr)
	{
		SystemViewportClient->bCaptureScreenShot = true;
		SystemViewportClient->ScreenShotOwner = InScreenShotOwner;
	}
}


void SNiagaraSystemViewport::AddReferencedObjects( FReferenceCollector& Collector )
{
	if (PreviewComponent != nullptr)
	{
		Collector.AddReferencedObject(PreviewComponent);
	}
}


bool SNiagaraSystemViewport::GetDrawElement(EDrawElements Element) const
{
	return (DrawFlags & Element) != 0;
}

void SNiagaraSystemViewport::ToggleDrawElement(EDrawElements Element)
{
	DrawFlags = DrawFlags ^ Element;
}

bool SNiagaraSystemViewport::IsToggleOrbitChecked() const
{
	return SystemViewportClient->bUsingOrbitCamera;
}

void SNiagaraSystemViewport::ToggleOrbit()
{
	SystemViewportClient->ToggleOrbitCamera(!SystemViewportClient->bUsingOrbitCamera);
}

void SNiagaraSystemViewport::RefreshViewport()
{
	//reregister the preview components, so if the preview material changed it will be propagated to the render thread
	PreviewComponent->MarkRenderStateDirty();
	SceneViewport->InvalidateDisplay();
}

void SNiagaraSystemViewport::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SEditorViewport::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );
}

void SNiagaraSystemViewport::SetPreviewComponent(UNiagaraComponent* NiagaraComponent)
{
	if (PreviewComponent != nullptr)
	{
		AdvancedPreviewScene->RemoveComponent(PreviewComponent);
	}
	PreviewComponent = NiagaraComponent;

	if (PreviewComponent != nullptr)
	{
		AdvancedPreviewScene->AddComponent(PreviewComponent, PreviewComponent->GetRelativeTransform());
	}
}


void SNiagaraSystemViewport::ToggleRealtime()
{
	SystemViewportClient->ToggleRealtime();
}

/*
TSharedRef<FUICommandList> SNiagaraSystemViewport::GetSystemEditorCommands() const
{
	check(SystemEditorPtr.IsValid());
	return SystemEditorPtr.GetToolkitCommands();
}
*/

void SNiagaraSystemViewport::OnAddedToTab( const TSharedRef<SDockTab>& OwnerTab )
{
	ParentTab = OwnerTab;
}

bool SNiagaraSystemViewport::IsVisible() const
{
	return ViewportWidget.IsValid() && (!ParentTab.IsValid() || ParentTab.Pin()->IsForeground()) && SEditorViewport::IsVisible() ;
}

void SNiagaraSystemViewport::OnScreenShotCaptured(UTexture2D* ScreenShot)
{
	OnThumbnailCaptured.ExecuteIfBound(ScreenShot);
}

void SNiagaraSystemViewport::BindCommands()
{
	SEditorViewport::BindCommands();

	// Unbind the CycleTransformGizmos since niagara currently doesn't use the gizmos and it prevents resetting the system with
	// spacebar when the viewport is focused.
	CommandList->UnmapAction(FEditorViewportCommands::Get().CycleTransformGizmos);

	const FNiagaraEditorCommands& Commands = FNiagaraEditorCommands::Get();
	
	// Add the commands to the toolkit command list so that the toolbar buttons can find them
	
	CommandList->MapAction(
		Commands.TogglePreviewGrid,
		FExecuteAction::CreateSP( this, &SNiagaraSystemViewport::TogglePreviewGrid ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SNiagaraSystemViewport::IsTogglePreviewGridChecked )
	);

	CommandList->MapAction(
		Commands.ToggleInstructionCounts,
		FExecuteAction::CreateLambda([Viewport=this]() { Viewport->ToggleDrawElement(EDrawElements::InstructionCounts); Viewport->RefreshViewport(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([Viewport=this]() -> bool { return Viewport->GetDrawElement(EDrawElements::InstructionCounts); })
	);

	CommandList->MapAction(
		Commands.ToggleParticleCounts,
		FExecuteAction::CreateLambda([Viewport = this]() { Viewport->ToggleDrawElement(EDrawElements::ParticleCounts); Viewport->RefreshViewport(); }),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([Viewport = this]() -> bool { return Viewport->GetDrawElement(EDrawElements::ParticleCounts); })
	);

	CommandList->MapAction(
		Commands.TogglePreviewBackground,
		FExecuteAction::CreateSP( this, &SNiagaraSystemViewport::TogglePreviewBackground ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &SNiagaraSystemViewport::IsTogglePreviewBackgroundChecked )
	);

	CommandList->MapAction(
		Commands.ToggleOrbit,
		FExecuteAction::CreateSP(this, &SNiagaraSystemViewport::ToggleOrbit),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SNiagaraSystemViewport::IsToggleOrbitChecked));
								  
}

void SNiagaraSystemViewport::OnFocusViewportToSelection()
{
	if( PreviewComponent )
	{
		// FocusViewportOnBox disables orbit, so remember our state
		bool bIsOrbit = SystemViewportClient->ShouldOrbitCamera();

		SystemViewportClient->FocusViewportOnBox(PreviewComponent->Bounds.GetBox());

		SystemViewportClient->ToggleOrbitCamera(bIsOrbit);
	}
}

void SNiagaraSystemViewport::TogglePreviewGrid()
{
	SystemViewportClient->SetShowGrid();
	RefreshViewport();
}

bool SNiagaraSystemViewport::IsTogglePreviewGridChecked() const
{
	return SystemViewportClient->IsSetShowGridChecked();
}

void SNiagaraSystemViewport::TogglePreviewBackground()
{
	bShowBackground = !bShowBackground;
	// @todo DB: Set the background mesh for the preview viewport.
	RefreshViewport();
}

bool SNiagaraSystemViewport::IsTogglePreviewBackgroundChecked() const
{
	return bShowBackground;
}

TSharedRef<FEditorViewportClient> SNiagaraSystemViewport::MakeEditorViewportClient() 
{
	SystemViewportClient = MakeShareable( new FNiagaraSystemViewportClient(*AdvancedPreviewScene.Get(), SharedThis(this), 
		FNiagaraSystemViewportClient::FOnScreenShotCaptured::CreateSP(this, &SNiagaraSystemViewport::OnScreenShotCaptured) ) );
	
	SystemViewportClient->SetViewLocation( FVector::ZeroVector );
	SystemViewportClient->SetViewRotation( FRotator::ZeroRotator );
	SystemViewportClient->SetViewLocationForOrbiting( FVector::ZeroVector );
	SystemViewportClient->bSetListenerPosition = false;

	SystemViewportClient->SetRealtime( true );
	SystemViewportClient->VisibilityDelegate.BindSP( this, &SNiagaraSystemViewport::IsVisible );
	
	return SystemViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SNiagaraSystemViewport::MakeViewportToolbar()
{
	//return SNew(SNiagaraSystemViewportToolBar)
	//.Viewport(SharedThis(this));
	return SNew(SBox);
}

EVisibility SNiagaraSystemViewport::OnGetViewportContentVisibility() const
{
	EVisibility BaseVisibility = SEditorViewport::OnGetViewportContentVisibility();
	if (BaseVisibility != EVisibility::Visible)
	{
		return BaseVisibility;
	}
	return IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiagaraSystemViewport::OnGetViewportCompileTextVisibility() const
{
	if (PreviewComponent && PreviewComponent->GetAsset() && PreviewComponent->GetAsset()->HasOutstandingCompilationRequests())
	{
		return EVisibility::Visible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

void SNiagaraSystemViewport::PopulateViewportOverlays(TSharedRef<class SOverlay> Overlay)
{
	Overlay->AddSlot()
		.VAlign(VAlign_Top)
		[
			SNew(SNiagaraSystemViewportToolBar, SharedThis(this))
		];
	Overlay->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SAssignNew(CompileText, STextBlock)
			.Visibility_Raw(this, &SNiagaraSystemViewport::OnGetViewportCompileTextVisibility)
		];

	CompileText->SetText(LOCTEXT("Compiling","Compiling"));

}


TSharedRef<class SEditorViewport> SNiagaraSystemViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SNiagaraSystemViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SNiagaraSystemViewport::OnFloatingButtonClicked()
{
}


#undef LOCTEXT_NAMESPACE