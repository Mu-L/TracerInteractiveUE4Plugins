// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TextureEditorToolkit.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Widgets/Layout/SBorder.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorStyleSet.h"
#include "EditorReimportHandler.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Engine/LightMapTexture2D.h"
#include "Engine/ShadowMapTexture2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Interfaces/ITextureEditorModule.h"
#include "TextureEditor.h"
#include "Slate/SceneViewport.h"
#include "PropertyEditorModule.h"
#include "TextureEditorConstants.h"
#include "Models/TextureEditorCommands.h"
#include "Widgets/STextureEditorViewport.h"
#include "ISettingsModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Curves/CurveLinearColorAtlas.h"

#define LOCTEXT_NAMESPACE "FTextureEditorToolkit"

DEFINE_LOG_CATEGORY_STATIC(LogTextureEditor, Log, All);

#define MIPLEVEL_MIN 0
#define MIPLEVEL_MAX 15
#define EXPOSURE_MIN -10
#define EXPOSURE_MAX 10


const FName FTextureEditorToolkit::ViewportTabId(TEXT("TextureEditor_Viewport"));
const FName FTextureEditorToolkit::PropertiesTabId(TEXT("TextureEditor_Properties"));

UNREALED_API void GetBestFitForNumberOfTiles(int32 InSize, int32& OutRatioX, int32& OutRatioY);


/* FTextureEditorToolkit structors
 *****************************************************************************/

FTextureEditorToolkit::FTextureEditorToolkit()
	: Texture(nullptr)
	, VolumeOpacity(1.f)
	, VolumeOrientation(90, 0, -90)
{
}

FTextureEditorToolkit::~FTextureEditorToolkit( )
{
	// Release the VT page table allocation used to display this texture
	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (Texture2D && Texture2D->IsCurrentlyVirtualTextured())
	{
		FVirtualTexture2DResource* Resource = (FVirtualTexture2DResource*)Texture2D->Resource;
		Resource->ReleaseAllocatedVT();
	}

	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);
	GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.RemoveAll(this);

	GEditor->UnregisterForUndo(this);
}


/* FAssetEditorToolkit interface
 *****************************************************************************/

FString FTextureEditorToolkit::GetDocumentationLink( ) const 
{
	return FString(TEXT("Engine/Content/Types/Textures/Properties/Interface"));
}


void FTextureEditorToolkit::RegisterTabSpawners( const TSharedRef<class FTabManager>& InTabManager )
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_TextureEditor", "Texture Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FTextureEditorToolkit::HandleTabSpawnerSpawnViewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FTextureEditorToolkit::HandleTabSpawnerSpawnProperties))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Details") )
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}


void FTextureEditorToolkit::UnregisterTabSpawners( const TSharedRef<class FTabManager>& InTabManager )
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(ViewportTabId);
	InTabManager->UnregisterTabSpawner(PropertiesTabId);
}


void FTextureEditorToolkit::InitTextureEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit )
{
	FReimportManager::Instance()->OnPreReimport().AddRaw(this, &FTextureEditorToolkit::HandleReimportManagerPreReimport);
	FReimportManager::Instance()->OnPostReimport().AddRaw(this, &FTextureEditorToolkit::HandleReimportManagerPostReimport);
	GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.AddRaw(this, &FTextureEditorToolkit::HandleAssetPostImport);

	Texture = CastChecked<UTexture>(ObjectToEdit);

	// Support undo/redo
	Texture->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	// initialize view options
	bIsRedChannel = true;
	bIsGreenChannel = true;
	bIsBlueChannel = true;
	bIsAlphaChannel = false;

	switch (Texture->CompressionSettings)
	{
	default:
		bIsAlphaChannel = !Texture->CompressionNoAlpha;
		break;
	case TC_Normalmap:
	case TC_Grayscale:
	case TC_Displacementmap:
	case TC_VectorDisplacementmap:
	case TC_DistanceFieldFont:
		bIsAlphaChannel = false;
		break;
	}

	bIsDesaturation = false;

	SpecifiedMipLevel = 0;
	bUseSpecifiedMipLevel = false;

	SpecifiedLayer = 0;

	SavedCompressionSetting = false;

	Zoom = 1.0f;

	// Register our commands. This will only register them if not previously registered
	FTextureEditorCommands::Register();

	BindCommands();
	CreateInternalWidgets();

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_TextureEditor_Layout_v3")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient(0.66f)
						->Split
						(
							FTabManager::NewStack()
								->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
								->SetHideTabWell(true)
								->SetSizeCoefficient(0.1f)
								
						)
						->Split
						(
							FTabManager::NewStack()
								->AddTab(ViewportTabId, ETabState::OpenedTab)
								->SetHideTabWell(true)
								->SetSizeCoefficient(0.9f)
						)
				)
				->Split
				(
					FTabManager::NewStack()
						->AddTab(PropertiesTabId, ETabState::OpenedTab)
						->SetSizeCoefficient(0.33f)
				)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TextureEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit);
	
	ITextureEditorModule* TextureEditorModule = &FModuleManager::LoadModuleChecked<ITextureEditorModule>("TextureEditor");
	AddMenuExtender(TextureEditorModule->GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	ExtendToolBar();

	RegenerateMenusAndToolbars();

	// @todo toolkit world centric editing
	/*if(IsWorldCentricAssetEditor())
	{
		SpawnToolkitTab(GetToolbarTabId(), FString(), EToolkitTabSpot::ToolBar);
		SpawnToolkitTab(ViewportTabId, FString(), EToolkitTabSpot::Viewport);
		SpawnToolkitTab(PropertiesTabId, FString(), EToolkitTabSpot::Details);
	}*/
}


/* ITextureEditorToolkit interface
 *****************************************************************************/

void FTextureEditorToolkit::CalculateTextureDimensions( uint32& Width, uint32& Height ) const
{
	uint32 ImportedWidth = Texture->Source.GetSizeX();
	uint32 ImportedHeight = Texture->Source.GetSizeY();

	// if Original Width and Height are 0, use the saved current width and height
	if ((ImportedWidth == 0) && (ImportedHeight == 0))
	{
		ImportedWidth = Texture->GetSurfaceWidth();
		ImportedHeight = Texture->GetSurfaceHeight();
	}

	Width = ImportedWidth;
	Height = ImportedHeight;


	// catch if the Width and Height are still zero for some reason
	if ((Width == 0) || (Height == 0))
	{
		Width = 0;
		Height= 0;

		return;
	}

	// See if we need to uniformly scale it to fit in viewport
	// Cap the size to effective dimensions
	uint32 ViewportW = TextureViewport->GetViewport()->GetSizeXY().X;
	uint32 ViewportH = TextureViewport->GetViewport()->GetSizeXY().Y;
	uint32 MaxWidth; 
	uint32 MaxHeight;

	const bool bFitToViewport = GetFitToViewport();
	if (bFitToViewport)
	{
		const UVolumeTexture* VolumeTexture = Cast<UVolumeTexture>(Texture);

		// Subtract off the viewport space devoted to padding (2 * PreviewPadding)
		// so that the texture is padded on all sides
		MaxWidth = ViewportW;
		MaxHeight = ViewportH;

		if (IsCubeTexture())
		{
			// Cubes are displayed 2:1. 2x width if the source exists and is not an unwrapped image.
			const bool bMultipleSourceImages = Texture->Source.GetNumSlices() > 1;
			const bool bNoSourceImage = Texture->Source.GetNumSlices() == 0;
			Width *= (bNoSourceImage || bMultipleSourceImages) ? 2 : 1;
		}
		else if (VolumeTexture)
		{
			UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
			if (Settings.VolumeViewMode == ETextureEditorVolumeViewMode::TextureEditorVolumeViewMode_VolumeTrace)
			{
				Width  = Height;
			}
			else
			{
				Width = (uint32)((float)Height * (float)PreviewEffectiveTextureWidth / (float)PreviewEffectiveTextureHeight);
			}
		}

		// First, scale up based on the size of the viewport
		if (MaxWidth > MaxHeight)
		{
			Height = Height * MaxWidth / Width;
			Width = MaxWidth;
		}
		else
		{
			Width = Width * MaxHeight / Height;
			Height = MaxHeight;
		}

		// then, scale again if our width and height is impacted by the scaling
		if (Width > MaxWidth)
		{
			Height = Height * MaxWidth / Width;
			Width = MaxWidth;
		}
		if (Height > MaxHeight)
		{
			Width = Width * MaxHeight / Height;
			Height = MaxHeight;
		}
	}
	else
	{
		Width = PreviewEffectiveTextureWidth * Zoom;
		Height = PreviewEffectiveTextureHeight * Zoom;
	}
}


ESimpleElementBlendMode FTextureEditorToolkit::GetColourChannelBlendMode( ) const
{
	if (Texture && (Texture->CompressionSettings == TC_Grayscale || Texture->CompressionSettings == TC_Alpha)) 
	{
		return SE_BLEND_Opaque;
	}

	// Add the red, green, blue, alpha and desaturation flags to the enum to identify the chosen filters
	uint32 Result = (uint32)SE_BLEND_RGBA_MASK_START;
	Result += bIsRedChannel ? (1 << 0) : 0;
	Result += bIsGreenChannel ? (1 << 1) : 0;
	Result += bIsBlueChannel ? (1 << 2) : 0;
	Result += bIsAlphaChannel ? (1 << 3) : 0;
	
	// If we only have one color channel active, enable color desaturation by default
	const int32 NumColorChannelsActive = (bIsRedChannel ? 1 : 0) + (bIsGreenChannel ? 1 : 0) + (bIsBlueChannel ? 1 : 0);
	const bool bIsDesaturationLocal = bIsDesaturation ? true : (NumColorChannelsActive==1);
	Result += bIsDesaturationLocal ? (1 << 4) : 0;

	return (ESimpleElementBlendMode)Result;
}


bool FTextureEditorToolkit::GetFitToViewport( ) const
{
	const UTextureEditorSettings& Settings = *GetDefault<UTextureEditorSettings>();
	return Settings.FitToViewport;
}


int32 FTextureEditorToolkit::GetMipLevel( ) const
{
	return GetUseSpecifiedMip() ? SpecifiedMipLevel : -1;
}

int32 FTextureEditorToolkit::GetLayer() const
{
	return SpecifiedLayer;
}

UTexture* FTextureEditorToolkit::GetTexture( ) const
{
	return Texture;
}


bool FTextureEditorToolkit::HasValidTextureResource( ) const
{
	return Texture != nullptr && Texture->Resource != nullptr;
}


bool FTextureEditorToolkit::GetUseSpecifiedMip( ) const
{
	if (GetMaxMipLevel().Get(MIPLEVEL_MAX) > 0)
	{
		if (HandleMipLevelCheckBoxIsEnabled())
		{
			return bUseSpecifiedMipLevel;
		}

		// by default this is on
		return true; 
	}

	// disable the widgets if we have no mip maps
	return false;
}


double FTextureEditorToolkit::GetZoom( ) const
{
	return Zoom;
}


void FTextureEditorToolkit::PopulateQuickInfo( )
{
	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	UTextureRenderTarget2D* Texture2DRT = Cast<UTextureRenderTarget2D>(Texture);
	UTextureRenderTargetCube* TextureCubeRT = Cast<UTextureRenderTargetCube>(Texture);
	UTextureCube* TextureCube = Cast<UTextureCube>(Texture);
	UTexture2DDynamic* Texture2DDynamic = Cast<UTexture2DDynamic>(Texture);
	UVolumeTexture* VolumeTexture = Cast<UVolumeTexture>(Texture);

	const uint32 SurfaceWidth = (uint32)Texture->GetSurfaceWidth();
	const uint32 SurfaceHeight = (uint32)Texture->GetSurfaceHeight();
	const uint32 SurfaceDepth =  VolumeTexture ? (uint32)VolumeTexture->GetSizeZ() : 1;

	const uint32 ImportedWidth = FMath::Max<uint32>(SurfaceWidth, Texture->Source.GetSizeX());
	const uint32 ImportedHeight =  FMath::Max<uint32>(SurfaceHeight, Texture->Source.GetSizeY());
	const uint32 ImportedDepth =  FMath::Max<uint32>(SurfaceDepth, VolumeTexture ? Texture->Source.GetNumSlices() : 1);

	const int32 ActualMipBias = Texture2D ? (Texture2D->GetNumMips() - Texture2D->GetNumResidentMips())	: Texture->GetCachedLODBias();
	const uint32 ActualWidth = FMath::Max<uint32>(SurfaceWidth >> ActualMipBias, 1);
	const uint32 ActualHeight = FMath::Max<uint32>(SurfaceHeight >> ActualMipBias, 1);
	const uint32 ActualDepth =  FMath::Max<uint32>(SurfaceDepth >> ActualMipBias, 1);

	// Editor dimensions (takes user specified mip setting into account)
	const int32 MipLevel = FMath::Max(GetMipLevel(), 0);
	PreviewEffectiveTextureWidth = FMath::Max<uint32>(ActualWidth >> MipLevel, 1);
	PreviewEffectiveTextureHeight = FMath::Max<uint32>(ActualHeight >> MipLevel, 1);;
	uint32 PreviewEffectiveTextureDepth = FMath::Max<uint32>(ActualDepth >> MipLevel, 1);

	// In game max bias and dimensions
	const int32 MaxResMipBias = Texture2D ? (Texture2D->GetNumMips() - Texture2D->GetNumMipsAllowed(true)) : Texture->GetCachedLODBias();
	const uint32 MaxInGameWidth = FMath::Max<uint32>(SurfaceWidth >> MaxResMipBias, 1);
	const uint32 MaxInGameHeight = FMath::Max<uint32>(SurfaceHeight >> MaxResMipBias, 1);
	const uint32 MaxInGameDepth = FMath::Max<uint32>(SurfaceDepth >> MaxResMipBias, 1);

	// Texture asset size
	const uint32 Size = (Texture->GetResourceSizeBytes(EResourceSizeMode::Exclusive) + 512) / 1024;

	FNumberFormattingOptions SizeOptions;
	SizeOptions.UseGrouping = false;
	SizeOptions.MaximumFractionalDigits = 0;

	// Cubes are previewed as unwrapped 2D textures.
	// These have 2x the width of a cube face.
	PreviewEffectiveTextureWidth *= IsCubeTexture() ? 2 : 1;

	FNumberFormattingOptions Options;
	Options.UseGrouping = false;


	if (VolumeTexture)
	{
		ImportedText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_Imported_3x", "Imported: {0}x{1}x{2}"), FText::AsNumber(ImportedWidth, &Options), FText::AsNumber(ImportedHeight, &Options), FText::AsNumber(ImportedDepth, &Options)));
		CurrentText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_Displayed_3x", "Displayed: {0}x{1}x{2}"), FText::AsNumber(PreviewEffectiveTextureWidth, &Options ), FText::AsNumber(PreviewEffectiveTextureHeight, &Options), FText::AsNumber(PreviewEffectiveTextureDepth, &Options)));
		MaxInGameText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_MaxInGame_3x", "Max In-Game: {0}x{1}x{2}"), FText::AsNumber(MaxInGameWidth, &Options), FText::AsNumber(MaxInGameHeight, &Options), FText::AsNumber(MaxInGameDepth, &Options)));

		UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
		if (Settings.VolumeViewMode == ETextureEditorVolumeViewMode::TextureEditorVolumeViewMode_VolumeTrace)
		{
			PreviewEffectiveTextureWidth = PreviewEffectiveTextureHeight = FMath::Max(PreviewEffectiveTextureWidth, PreviewEffectiveTextureHeight);
		}
		else
		{
			int32 NumTilesX = 0;
			int32 NumTilesY = 0;
			GetBestFitForNumberOfTiles(PreviewEffectiveTextureDepth, NumTilesX, NumTilesY);
			PreviewEffectiveTextureWidth *= (uint32)NumTilesX;
			PreviewEffectiveTextureHeight *= (uint32)NumTilesY;
		}
	}
	else
	{
	    FText CubemapAdd;
	    if(TextureCube)
	    {
		    CubemapAdd = NSLOCTEXT("TextureEditor", "QuickInfo_PerCubeSide", "x6 (CubeMap)");
	    }
    
	    ImportedText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_Imported_2x", "Imported: {0}x{1}"), FText::AsNumber(ImportedWidth, &Options), FText::AsNumber(ImportedHeight, &Options)));
		CurrentText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_Displayed_2x", "Displayed: {0}x{1}{2}"), FText::AsNumber(PreviewEffectiveTextureWidth, &Options ), FText::AsNumber(PreviewEffectiveTextureHeight, &Options), CubemapAdd));
		MaxInGameText->SetText(FText::Format( NSLOCTEXT("TextureEditor", "QuickInfo_MaxInGame_2x", "Max In-Game: {0}x{1}{2}"), FText::AsNumber(MaxInGameWidth, &Options), FText::AsNumber(MaxInGameHeight, &Options), CubemapAdd));
	}

	SizeText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_ResourceSize", "Resource Size: {0} Kb"), FText::AsNumber(Size, &SizeOptions)));

	FText Method = Texture->VirtualTextureStreaming ? NSLOCTEXT("TextureEditor", "QuickInfo_MethodVirtualStreamed", "Virtual Streamed")
													: (!Texture->bIsStreamable ? NSLOCTEXT("TextureEditor", "QuickInfo_MethodNotStreamed", "Not Streamed") 
																			: NSLOCTEXT("TextureEditor", "QuickInfo_MethodStreamed", "Streamed") );

	MethodText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_Method", "Method: {0}"), Method));
	LODBiasText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_LODBias", "Combined LOD Bias: {0}"), FText::AsNumber(Texture->GetCachedLODBias())));

	int32 TextureFormatIndex = PF_MAX;
	
	if (Texture2D)
	{
		TextureFormatIndex = Texture2D->GetPixelFormat(SpecifiedLayer);
	}
	else if (TextureCube)
	{
		TextureFormatIndex = TextureCube->GetPixelFormat();
	}
	else if (Texture2DRT)
	{
		TextureFormatIndex = Texture2DRT->GetFormat();
	}
	else if (Texture2DDynamic)
	{
		TextureFormatIndex = Texture2DDynamic->Format;
	}
	else if (VolumeTexture)
	{
		TextureFormatIndex = VolumeTexture->GetPixelFormat();
	}

	if (TextureFormatIndex != PF_MAX)
	{
		FormatText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_Format", "Format: {0}"), FText::FromString(GPixelFormats[TextureFormatIndex].Name)));
	}

	int32 NumMips = 1;
	if (Texture2D)
	{
		NumMips = Texture2D->GetNumMips();
	}
	else if (TextureCube)
	{
		NumMips = TextureCube->GetNumMips();
	}
	else if (Texture2DRT)
	{
		NumMips = Texture2DRT->GetNumMips();
	}
	else if (Texture2DDynamic)
	{
		NumMips = Texture2DDynamic->NumMips;
	}
	else if (VolumeTexture)
	{
		NumMips = VolumeTexture->GetNumMips();
	}

	NumMipsText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_NumMips", "Number of Mips: {0}"), FText::AsNumber(NumMips)));

	if (Texture2D)
	{
		HasAlphaChannelText->SetText(FText::Format(NSLOCTEXT("TextureEditor", "QuickInfo_HasAlphaChannel", "Has Alpha Channel: {0}"),
			Texture2D->HasAlphaChannel() ? NSLOCTEXT("TextureEditor", "True", "True") : NSLOCTEXT("TextureEditor", "False", "False")));
	}

	HasAlphaChannelText->SetVisibility(Texture2D ? EVisibility::Visible : EVisibility::Collapsed);
}


void FTextureEditorToolkit::SetFitToViewport( const bool bFitToViewport )
{
	UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
	Settings.FitToViewport = bFitToViewport;
	Settings.PostEditChange();
}


void FTextureEditorToolkit::SetZoom( double ZoomValue )
{
	Zoom = FMath::Clamp(ZoomValue, MinZoom, MaxZoom);
	SetFitToViewport(false);
}


void FTextureEditorToolkit::ZoomIn( )
{
	SetZoom(Zoom + ZoomStep);
}


void FTextureEditorToolkit::ZoomOut( )
{
	SetZoom(Zoom - ZoomStep);
}

float FTextureEditorToolkit::GetVolumeOpacity() const
{
	return VolumeOpacity;
}

void FTextureEditorToolkit::SetVolumeOpacity(float ZoomValue)
{
	VolumeOpacity = FMath::Clamp(ZoomValue, 0.f, 1.f);
}

const FRotator& FTextureEditorToolkit::GetVolumeOrientation() const
{
	return VolumeOrientation;
}

void FTextureEditorToolkit::SetVolumeOrientation(const FRotator& InOrientation)
{
	VolumeOrientation = InOrientation;
}

/* IToolkit interface
 *****************************************************************************/

FText FTextureEditorToolkit::GetBaseToolkitName( ) const
{
	return LOCTEXT("AppLabel", "Texture Editor");
}


FName FTextureEditorToolkit::GetToolkitFName( ) const
{
	return FName("TextureEditor");
}


FLinearColor FTextureEditorToolkit::GetWorldCentricTabColorScale( ) const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}


FString FTextureEditorToolkit::GetWorldCentricTabPrefix( ) const
{
	return LOCTEXT("WorldCentricTabPrefix", "Texture ").ToString();
}


/* FGCObject interface
 *****************************************************************************/

void FTextureEditorToolkit::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObject(Texture);
	TextureViewport->AddReferencedObjects(Collector);
}


/* FEditorUndoClient interface
 *****************************************************************************/

void FTextureEditorToolkit::PostUndo( bool bSuccess )
{
}


void FTextureEditorToolkit::PostRedo( bool bSuccess )
{
	PostUndo(bSuccess);
}


/* FTextureEditorToolkit implementation
 *****************************************************************************/

void FTextureEditorToolkit::BindCommands( )
{
	const FTextureEditorCommands& Commands = FTextureEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.RedChannel,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleRedChannelActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleRedChannelActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.GreenChannel,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleGreenChannelActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleGreenChannelActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.BlueChannel,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleBlueChannelActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleBlueChannelActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.AlphaChannel,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleAlphaChannelActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleAlphaChannelActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.Desaturation,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleDesaturationChannelActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleDesaturationChannelActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.FitToViewport,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleFitToViewportActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleFitToViewportActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.CheckeredBackground,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionExecute, TextureEditorBackground_Checkered),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionIsChecked, TextureEditorBackground_Checkered));

	ToolkitCommands->MapAction(
		Commands.CheckeredBackgroundFill,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionExecute, TextureEditorBackground_CheckeredFill),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionIsChecked, TextureEditorBackground_CheckeredFill));

	ToolkitCommands->MapAction(
		Commands.SolidBackground,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionExecute, TextureEditorBackground_SolidColor),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleCheckeredBackgroundActionIsChecked, TextureEditorBackground_SolidColor));

	// Begin - Volume Texture Specifics
	ToolkitCommands->MapAction(
		Commands.DepthSlices,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleVolumeViewModeActionExecute, TextureEditorVolumeViewMode_DepthSlices),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleVolumeViewModeActionIsChecked, TextureEditorVolumeViewMode_DepthSlices));

	ToolkitCommands->MapAction(
		Commands.TraceIntoVolume,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleVolumeViewModeActionExecute, TextureEditorVolumeViewMode_VolumeTrace),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleVolumeViewModeActionIsChecked, TextureEditorVolumeViewMode_VolumeTrace));
	// End - Volume Texture Specifics

	ToolkitCommands->MapAction(
		Commands.TextureBorder,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleTextureBorderActionExecute),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTextureEditorToolkit::HandleTextureBorderActionIsChecked));

	ToolkitCommands->MapAction(
		Commands.CompressNow,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleCompressNowActionExecute),
		FCanExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleCompressNowActionCanExecute));

	ToolkitCommands->MapAction(
		Commands.Reimport,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleReimportActionExecute),
		FCanExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleReimportActionCanExecute));

	ToolkitCommands->MapAction(
		Commands.Settings,
		FExecuteAction::CreateSP(this, &FTextureEditorToolkit::HandleSettingsActionExecute));
}


TSharedRef<SWidget> FTextureEditorToolkit::BuildTexturePropertiesWidget( )
{
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TexturePropertiesWidget = PropertyModule.CreateDetailView(Args);
	TexturePropertiesWidget->SetObject(Texture);

	return TexturePropertiesWidget.ToSharedRef();
}

void FTextureEditorToolkit::CreateInternalWidgets( )
{
	TextureViewport = SNew(STextureEditorViewport, SharedThis(this));

	TextureProperties = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(2.0f)
	[
		SNew(SBorder)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(ImportedText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(CurrentText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(MaxInGameText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(SizeText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(HasAlphaChannelText, STextBlock)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(MethodText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(FormatText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(LODBiasText, STextBlock)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(4.0f)
				[
					SAssignNew(NumMipsText, STextBlock)
				]
			]
		]
	]

	+ SVerticalBox::Slot()
	.FillHeight(1.0f)
	.Padding(2.0f)
	[
		SNew(SBorder)
		.Padding(4.0f)
		[
			BuildTexturePropertiesWidget()
		]
	];
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FTextureEditorToolkit::ExtendToolBar( )
{
	TSharedRef<SWidget> LODControl = SNew(SBox)
		.WidthOverride(240.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.MaxWidth(240.0f)
				.Padding(0.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					// Mip and exposure controls
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
						.Padding(4.0f, 0.0f, 4.0f, 0.0f)
						.AutoWidth()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								.AutoWidth()
								[
									SNew(SCheckBox)
										.IsChecked(this, &FTextureEditorToolkit::HandleMipLevelCheckBoxIsChecked)
										.IsEnabled(this, &FTextureEditorToolkit::HandleMipLevelCheckBoxIsEnabled)
										.OnCheckStateChanged(this, &FTextureEditorToolkit::HandleMipLevelCheckBoxCheckedStateChanged)
								]
						]

					+ SHorizontalBox::Slot()
						.Padding(4.0f, 0.0f, 4.0f, 0.0f)
						.FillWidth(1)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.Padding(0.0f, 0.0, 4.0, 0.0)
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(NSLOCTEXT("TextureEditor", "MipLevel", "Mip Level: "))
								]

							+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								.FillWidth(1.0f)
								[
									SNew(SNumericEntryBox<int32>)
										.AllowSpin(true)
										.MinSliderValue(MIPLEVEL_MIN)
										.MaxSliderValue(this, &FTextureEditorToolkit::GetMaxMipLevel)
										.Value(this, &FTextureEditorToolkit::HandleMipLevelEntryBoxValue)
										.OnValueChanged(this, &FTextureEditorToolkit::HandleMipLevelEntryBoxChanged)
										.IsEnabled(this, &FTextureEditorToolkit::GetUseSpecifiedMip)
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(2.0f)
								[
									SNew(SButton)
										.Text(NSLOCTEXT("TextureEditor", "MipMinus", "-"))
										.OnClicked(this, &FTextureEditorToolkit::HandleMipMapMinusButtonClicked)
										.IsEnabled(this, &FTextureEditorToolkit::GetUseSpecifiedMip)
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(2.0f)
								[
									SNew(SButton)
										.Text(NSLOCTEXT("TextureEditor", "MipPlus", "+"))
										.OnClicked(this, &FTextureEditorToolkit::HandleMipMapPlusButtonClicked)
										.IsEnabled(this, &FTextureEditorToolkit::GetUseSpecifiedMip)
								]
						]
				]
			];

	TSharedRef<SWidget> LayerControl = SNew(SBox)
		.WidthOverride(200.0f)
		[
			SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.MaxWidth(200.0f)
					.Padding(0.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						// Layer controls
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
							.Padding(0.0f, 0.0, 4.0, 0.0)
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(NSLOCTEXT("TextureEditor", "Layer", "Layer: "))
							]

						+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							.FillWidth(1.0f)
							[
								SNew(SNumericEntryBox<int32>)
									.AllowSpin(true)
									.MinSliderValue(MIPLEVEL_MIN)
									.MaxSliderValue(this, &FTextureEditorToolkit::GetMaxLayer)
									.Value(this, &FTextureEditorToolkit::HandleLayerEntryBoxValue)
									.OnValueChanged(this, &FTextureEditorToolkit::HandleLayerEntryBoxChanged)
							]

						+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2.0f)
							[
								SNew(SButton)
									.Text(NSLOCTEXT("TextureEditor", "LayerMinus", "-"))
									.OnClicked(this, &FTextureEditorToolkit::HandleLayerMinusButtonClicked)
							]

						+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(2.0f)
							[
								SNew(SButton)
									.Text(NSLOCTEXT("TextureEditor", "LayerPlus", "+"))
									.OnClicked(this, &FTextureEditorToolkit::HandleLayerPlusButtonClicked)
							]
					]
		];

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FTextureEditorToolkit::FillToolbar, GetToolkitCommands(), LODControl, LayerControl)
	);

	AddToolbarExtender(ToolbarExtender);

	ITextureEditorModule* TextureEditorModule = &FModuleManager::LoadModuleChecked<ITextureEditorModule>("TextureEditor");
	AddToolbarExtender(TextureEditorModule->GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FTextureEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef< FUICommandList > InToolkitCommands, TSharedRef<SWidget> LODControl, TSharedRef<SWidget> LayerControl)
{
	UCurveLinearColorAtlas* Atlas = Cast<UCurveLinearColorAtlas>(GetTexture());
	if (!Atlas)
	{
		ToolbarBuilder.BeginSection("TextureMisc");
		{
			ToolbarBuilder.AddToolBarButton(FTextureEditorCommands::Get().CompressNow);
			ToolbarBuilder.AddToolBarButton(FTextureEditorCommands::Get().Reimport);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("TextureMipAndExposure");
		{
			ToolbarBuilder.AddWidget(LODControl);
		}
		ToolbarBuilder.EndSection();

		if (HasLayers())
		{
			ToolbarBuilder.BeginSection("Layers");
			{
				ToolbarBuilder.AddWidget(LayerControl);
			}
			ToolbarBuilder.EndSection();
		}
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


TOptional<int32> FTextureEditorToolkit::GetMaxMipLevel( ) const
{
	const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	const UTextureCube* TextureCube = Cast<UTextureCube>(Texture);
	const UTextureRenderTargetCube* RTTextureCube = Cast<UTextureRenderTargetCube>(Texture);
	const UTextureRenderTarget2D* RTTexture2D = Cast<UTextureRenderTarget2D>(Texture);
	const UVolumeTexture* VolumeTexture = Cast<UVolumeTexture>(Texture);

	if (Texture2D)
	{
		return Texture2D->GetNumMips() - 1;
	}

	if (TextureCube)
	{
		return TextureCube->GetNumMips() - 1;
	}

	if (RTTextureCube)
	{
		return RTTextureCube->GetNumMips() - 1;
	}

	if (RTTexture2D)
	{
		return RTTexture2D->GetNumMips() - 1;
	}

	if (VolumeTexture)
	{
		return VolumeTexture->GetNumMips() - 1;
	}

	return MIPLEVEL_MAX;
}

TOptional<int32> FTextureEditorToolkit::GetMaxLayer() const
{
	return FMath::Max(Texture->Source.GetNumLayers() - 1, 1);
}

bool FTextureEditorToolkit::IsCubeTexture( ) const
{
	return (Texture->IsA(UTextureCube::StaticClass()) || Texture->IsA(UTextureRenderTargetCube::StaticClass()));
}


/* FTextureEditorToolkit callbacks
 *****************************************************************************/

bool FTextureEditorToolkit::HandleAlphaChannelActionCanExecute( ) const
{
	const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);

	if (Texture2D == NULL)
	{
		return false;
	}

	return Texture2D->HasAlphaChannel();
}


void FTextureEditorToolkit::HandleAlphaChannelActionExecute( )
{
	bIsAlphaChannel = !bIsAlphaChannel;
}


bool FTextureEditorToolkit::HandleAlphaChannelActionIsChecked( ) const
{
	return bIsAlphaChannel;
}


void FTextureEditorToolkit::HandleBlueChannelActionExecute( )
{
	 bIsBlueChannel = !bIsBlueChannel;
}


bool FTextureEditorToolkit::HandleBlueChannelActionIsChecked( ) const
{
	return bIsBlueChannel;
}


void FTextureEditorToolkit::HandleCheckeredBackgroundActionExecute( ETextureEditorBackgrounds Background )
{
	UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
	Settings.Background = Background;
	Settings.PostEditChange();
}


bool FTextureEditorToolkit::HandleCheckeredBackgroundActionIsChecked( ETextureEditorBackgrounds Background )
{
	const UTextureEditorSettings& Settings = *GetDefault<UTextureEditorSettings>();

	return (Background == Settings.Background);
}

// Callback for toggling the volume display action.
void FTextureEditorToolkit::HandleVolumeViewModeActionExecute(ETextureEditorVolumeViewMode InViewMode)
{
	UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
	Settings.VolumeViewMode = InViewMode;
	Settings.PostEditChange();
}

// Callback for getting the checked state of the volume display action.
bool FTextureEditorToolkit::HandleVolumeViewModeActionIsChecked(ETextureEditorVolumeViewMode InViewMode)
{
	const UTextureEditorSettings& Settings = *GetDefault<UTextureEditorSettings>();

	return (InViewMode == Settings.VolumeViewMode);
}


void FTextureEditorToolkit::HandleCompressNowActionExecute( )
{
	GWarn->BeginSlowTask(NSLOCTEXT("TextureEditor", "CompressNow", "Compressing 1 Textures that have Defer Compression set"), true);

	if (Texture->DeferCompression)
	{
		// turn off deferred compression and compress the texture
		Texture->DeferCompression = false;
		Texture->Source.Compress();
		Texture->PostEditChange();

		PopulateQuickInfo();
	}

	GWarn->EndSlowTask();
}


bool FTextureEditorToolkit::HandleCompressNowActionCanExecute( ) const
{
	return (Texture->DeferCompression != 0);
}


void FTextureEditorToolkit::HandleFitToViewportActionExecute( )
{
	ToggleFitToViewport();
}


bool FTextureEditorToolkit::HandleFitToViewportActionIsChecked( ) const
{
	return GetFitToViewport();
}


void FTextureEditorToolkit::HandleGreenChannelActionExecute( )
{
	 bIsGreenChannel = !bIsGreenChannel;
}


bool FTextureEditorToolkit::HandleGreenChannelActionIsChecked( ) const
{
	return bIsGreenChannel;
}


void FTextureEditorToolkit::HandleMipLevelCheckBoxCheckedStateChanged( ECheckBoxState InNewState )
{
	bUseSpecifiedMipLevel = InNewState == ECheckBoxState::Checked;
}


ECheckBoxState FTextureEditorToolkit::HandleMipLevelCheckBoxIsChecked( ) const
{
	return GetUseSpecifiedMip() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}


bool FTextureEditorToolkit::HandleMipLevelCheckBoxIsEnabled( ) const
{
	UTextureCube* TextureCube = Cast<UTextureCube>(Texture);

	if (GetMaxMipLevel().Get(MIPLEVEL_MAX) <= 0 || TextureCube)
	{
		return false;
	}

	return true;
}


void FTextureEditorToolkit::HandleMipLevelEntryBoxChanged( int32 NewMipLevel )
{
	SpecifiedMipLevel = FMath::Clamp<int32>(NewMipLevel, MIPLEVEL_MIN, GetMaxMipLevel().Get(MIPLEVEL_MAX));
}


TOptional<int32> FTextureEditorToolkit::HandleMipLevelEntryBoxValue( ) const
{
	return SpecifiedMipLevel;
}


FReply FTextureEditorToolkit::HandleMipMapMinusButtonClicked( )
{
	if (SpecifiedMipLevel > MIPLEVEL_MIN)
	{
		--SpecifiedMipLevel;
	}

	return FReply::Handled();
}


FReply FTextureEditorToolkit::HandleMipMapPlusButtonClicked( )
{
	if (SpecifiedMipLevel < GetMaxMipLevel().Get(MIPLEVEL_MAX))
	{
		++SpecifiedMipLevel;
	}

	return FReply::Handled();
}

void FTextureEditorToolkit::HandleLayerEntryBoxChanged(int32 NewLayer)
{
	SpecifiedLayer = FMath::Clamp<int32>(NewLayer, 0, Texture->Source.GetNumLayers() - 1);
	PopulateQuickInfo();
}

FReply FTextureEditorToolkit::HandleLayerMinusButtonClicked()
{
	if (SpecifiedLayer > 0)
	{
		--SpecifiedLayer;
		PopulateQuickInfo();
	}

	return FReply::Handled();
}


FReply FTextureEditorToolkit::HandleLayerPlusButtonClicked()
{
	if ((SpecifiedLayer + 1) < Texture->Source.GetNumLayers())
	{
		++SpecifiedLayer;
		PopulateQuickInfo();
	}

	return FReply::Handled();
}

TOptional<int32> FTextureEditorToolkit::HandleLayerEntryBoxValue() const
{
	return SpecifiedLayer;
}

bool FTextureEditorToolkit::HasLayers() const
{
	return Texture->Source.GetNumLayers() > 1;
}

void FTextureEditorToolkit::HandleRedChannelActionExecute( )
{
	bIsRedChannel = !bIsRedChannel;
}


bool FTextureEditorToolkit::HandleRedChannelActionIsChecked( ) const
{
	return bIsRedChannel;
}


bool FTextureEditorToolkit::HandleReimportActionCanExecute( ) const
{
	if (Texture->IsA<ULightMapTexture2D>() || Texture->IsA<UShadowMapTexture2D>() || Texture->IsA<UTexture2DDynamic>() || Texture->IsA<UTextureRenderTarget>() || Texture->IsA<UCurveLinearColorAtlas>())
	{
		return false;
	}

	return true;
}


void FTextureEditorToolkit::HandleReimportActionExecute( )
{
	FReimportManager::Instance()->Reimport(Texture, /*bAskForNewFileIfMissing=*/true);
}


void FTextureEditorToolkit::HandleReimportManagerPostReimport( UObject* InObject, bool bSuccess )
{
	// Ignore if this is regarding a different object
	if (InObject != Texture)
	{
		return;
	}

	if (!bSuccess)
	{
		// Failed, restore the compression flag
		Texture->DeferCompression = SavedCompressionSetting;
	}

	// Re-enable viewport rendering now that the texture should be in a known state again
	TextureViewport->EnableRendering();
}


void FTextureEditorToolkit::HandleReimportManagerPreReimport( UObject* InObject )
{
	// Ignore if this is regarding a different object
	if (InObject != Texture)
	{
		return;
	}

	// Prevent the texture from being compressed immediately, so the user can see the results
	SavedCompressionSetting = Texture->DeferCompression;
	Texture->DeferCompression = true;

	// Disable viewport rendering until the texture has finished re-importing
	TextureViewport->DisableRendering();
}

void FTextureEditorToolkit::HandleAssetPostImport(UFactory* InFactory, UObject* InObject)
{
	if (Cast<UTexture>(InObject) != nullptr && InObject == Texture)
	{
		// Refresh this object within the details panel
		TexturePropertiesWidget->SetObject(InObject);
	}
}

void FTextureEditorToolkit::HandleDesaturationChannelActionExecute( )
{
	bIsDesaturation = !bIsDesaturation;
}


bool FTextureEditorToolkit::HandleDesaturationChannelActionIsChecked( ) const
{
	return bIsDesaturation;
}


void FTextureEditorToolkit::HandleSettingsActionExecute( )
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "ContentEditors", "TextureEditor");
}


TSharedRef<SDockTab> FTextureEditorToolkit::HandleTabSpawnerSpawnProperties( const FSpawnTabArgs& Args )
{
	check(Args.GetTabId() == PropertiesTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("TextureEditor.Tabs.Properties"))
		.Label(LOCTEXT("TexturePropertiesTitle", "Details"))
		[
			TextureProperties.ToSharedRef()
		];

	PopulateQuickInfo();

	return SpawnedTab;
}


TSharedRef<SDockTab> FTextureEditorToolkit::HandleTabSpawnerSpawnViewport( const FSpawnTabArgs& Args )
{
	check(Args.GetTabId() == ViewportTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("TextureViewportTitle", "Viewport"))
		[
			TextureViewport.ToSharedRef()
		];
}


void FTextureEditorToolkit::HandleTextureBorderActionExecute( )
{
	UTextureEditorSettings& Settings = *GetMutableDefault<UTextureEditorSettings>();
	Settings.TextureBorderEnabled = !Settings.TextureBorderEnabled;
	Settings.PostEditChange();
}


bool FTextureEditorToolkit::HandleTextureBorderActionIsChecked( ) const
{
	const UTextureEditorSettings& Settings = *GetDefault<UTextureEditorSettings>();

	return Settings.TextureBorderEnabled;
}


#undef LOCTEXT_NAMESPACE
