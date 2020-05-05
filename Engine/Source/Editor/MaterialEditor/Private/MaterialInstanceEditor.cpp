// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialInstanceEditor.h"
#include "Widgets/Text/STextBlock.h"
#include "EngineGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Views/SListView.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "MaterialEditor/DEditorTextureParameterValue.h"
#include "MaterialEditor/DEditorRuntimeVirtualTextureParameterValue.h"
#include "Materials/Material.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "ThumbnailRendering/SceneThumbnailInfoWithPrimitive.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "MaterialEditorModule.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"

#include "MaterialEditor.h"
#include "MaterialEditorActions.h"
#include "MaterialEditorUtilities.h"

#include "PropertyEditorModule.h"
#include "MaterialEditorInstanceDetailCustomization.h"
#include "SMaterialLayersFunctionsTree.h"

#include "EditorViewportCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "CanvasTypes.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "AdvancedPreviewSceneModule.h"
#include "Misc/MessageDialog.h"
#include "Framework/Commands/UICommandInfo.h"
#include "EditorStyleSet.h"
#include "MaterialStats.h"
#include "MaterialEditingLibrary.h"
#include "Widgets/Layout/SScrollBox.h"
#include "DebugViewModeHelpers.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Widgets/Input/SButton.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "MaterialInstanceEditor"

DEFINE_LOG_CATEGORY_STATIC(LogMaterialInstanceEditor, Log, All);

const FName FMaterialInstanceEditor::PreviewTabId( TEXT( "MaterialInstanceEditor_Preview" ) );
const FName FMaterialInstanceEditor::PropertiesTabId( TEXT( "MaterialInstanceEditor_MaterialProperties" ) );
const FName FMaterialInstanceEditor::LayerPropertiesTabId(TEXT("MaterialInstanceEditor_MaterialLayerProperties"));
const FName FMaterialInstanceEditor::PreviewSettingsTabId(TEXT("MaterialInstanceEditor_PreviewSettings"));

//////////////////////////////////////////////////////////////////////////
// SMaterialTreeWidgetItem
class SMaterialTreeWidgetItem : public SMultiColumnTableRow< TWeakObjectPtr<UMaterialInterface> >
{
public:
	SLATE_BEGIN_ARGS(SMaterialTreeWidgetItem)
		: _ParentIndex( -1 )
		, _WidgetInfoToVisualize()
		{}
		SLATE_ARGUMENT( int32, ParentIndex )
		SLATE_ARGUMENT( TWeakObjectPtr<UMaterialInterface>, WidgetInfoToVisualize )
	SLATE_END_ARGS()

	/**
	 * Construct child widgets that comprise this widget.
	 *
	 * @param InArgs  Declaration from which to construct this widget
	 */
	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView )
	{
		this->WidgetInfo = InArgs._WidgetInfoToVisualize;
		this->ParentIndex = InArgs._ParentIndex;

		SMultiColumnTableRow< TWeakObjectPtr<UMaterialInterface> >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
	}

	/** @return Widget based on the column name */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override
	{
		FText Entry;
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 9);
		if ( ColumnName == "Parent" )
		{
			if ( ParentIndex == 0 )
			{
				Entry = NSLOCTEXT("UnrealEd", "Material", "Material");
			}
			else if ( ParentIndex != -1 )
			{
				FFormatNamedArguments Args;
				Args.Add( TEXT("Index"), ParentIndex );
				Entry = FText::Format( FText::FromString("Parent {Index}"), Args );
			}
			else
			{
				Entry = NSLOCTEXT("UnrealEd", "Current", "Current");
				FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);
			}
		}
		else
		{
			Entry = FText::FromString( WidgetInfo.Get()->GetName() );
			if ( ParentIndex == -1 )
			{
				FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);
			}
		}
		
		return
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew( STextBlock )
				.Text( Entry )
				.Font( FontInfo )
			];
	}

protected:
	/** The info about the widget that we are visualizing */
	TAttribute< TWeakObjectPtr<UMaterialInterface> > WidgetInfo;

	/** The index this material has in our parents array */
	int32 ParentIndex;
};

//////////////////////////////////////////////////////////////////////////
// SFunctionTreeWidgetItem
class SFunctionTreeWidgetItem : public SMultiColumnTableRow< TWeakObjectPtr<UMaterialFunctionInterface> >
{
public:
	SLATE_BEGIN_ARGS(SFunctionTreeWidgetItem)
		: _ParentIndex( -1 )
		, _WidgetInfoToVisualize()
		{}
		SLATE_ARGUMENT( int32, ParentIndex )
		SLATE_ARGUMENT( TWeakObjectPtr<UMaterialFunctionInterface>, WidgetInfoToVisualize )
	SLATE_END_ARGS()

	/**
	 * Construct child widgets that comprise this widget.
	 *
	 * @param InArgs  Declaration from which to construct this widget
	 */
	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView )
	{
		this->WidgetInfo = InArgs._WidgetInfoToVisualize;
		this->ParentIndex = InArgs._ParentIndex;

		SMultiColumnTableRow< TWeakObjectPtr<UMaterialFunctionInterface> >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
	}

	/** @return Widget based on the column name */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override
	{
		FText Entry;
		FSlateFontInfo FontInfo = FSlateFontInfo( FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), 9 );
		if ( ColumnName == "Parent" )
		{
			if ( ParentIndex == 0 )
			{
				Entry = NSLOCTEXT("UnrealEd", "Function", "Function");
			}
			else if ( ParentIndex != -1 )
			{
				FFormatNamedArguments Args;
				Args.Add( TEXT("Index"), ParentIndex );
				Entry = FText::Format( FText::FromString("Parent {Index}"), Args );
			}
			else
			{
				Entry = NSLOCTEXT("UnrealEd", "Current", "Current");
				FontInfo = FSlateFontInfo( FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 9 );
			}
		}
		else
		{
			Entry = FText::FromString( WidgetInfo.Get()->GetName() );
			if ( ParentIndex == -1 )
			{
				FontInfo = FSlateFontInfo( FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 9 );
			}
		}
		
		return
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew( STextBlock )
				.Text( Entry )
				.Font( FontInfo )
			];
	}

protected:
	/** The info about the widget that we are visualizing */
	TAttribute< TWeakObjectPtr<UMaterialFunctionInterface> > WidgetInfo;

	/** The index this material has in our parents array */
	int32 ParentIndex;
};

//////////////////////////////////////////////////////////////////////////
// FMaterialInstanceEditor

void FMaterialInstanceEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MaterialInstanceEditor", "Material Instance Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	
	InTabManager->RegisterTabSpawner( PreviewTabId, FOnSpawnTab::CreateSP( this, &FMaterialInstanceEditor::SpawnTab_Preview ) )
		.SetDisplayName( LOCTEXT( "ViewportTab", "Viewport" ) )
		.SetGroup( WorkspaceMenuCategoryRef )
		.SetIcon( FSlateIcon( FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports" ) );
	
	InTabManager->RegisterTabSpawner( PropertiesTabId, FOnSpawnTab::CreateSP( this, &FMaterialInstanceEditor::SpawnTab_Properties ) )
		.SetDisplayName( LOCTEXT( "PropertiesTab", "Details" ) )
		.SetGroup( WorkspaceMenuCategoryRef )
		.SetIcon( FSlateIcon( FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details" ) );

	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	if (MaterialEditorModule->MaterialLayersEnabled() && !bIsFunctionPreviewMaterial)
	{
		InTabManager->RegisterTabSpawner(LayerPropertiesTabId, FOnSpawnTab::CreateSP(this, &FMaterialInstanceEditor::SpawnTab_LayerProperties))
			.SetDisplayName(LOCTEXT("LayerPropertiesTab", "Layer Parameters"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Layers"));
	}
	
	InTabManager->RegisterTabSpawner(PreviewSettingsTabId, FOnSpawnTab::CreateSP(this, &FMaterialInstanceEditor::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	MaterialStatsManager->RegisterTabs();

	OnRegisterTabSpawners().Broadcast(InTabManager);
}

void FMaterialInstanceEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner( PreviewTabId );		
	InTabManager->UnregisterTabSpawner( PropertiesTabId );	
	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	if (MaterialEditorModule->MaterialLayersEnabled() && !bIsFunctionPreviewMaterial)
	{
		InTabManager->UnregisterTabSpawner(LayerPropertiesTabId);
	}
	InTabManager->UnregisterTabSpawner( PreviewSettingsTabId );

	MaterialStatsManager->UnregisterTabs();

	OnUnregisterTabSpawners().Broadcast(InTabManager);
}

void FMaterialInstanceEditor::InitEditorForMaterial(UMaterialInstance* InMaterial)
{
	check(InMaterial);
	MaterialFunctionOriginal = nullptr;
	MaterialFunctionInstance = nullptr;
	FunctionMaterialProxy = nullptr;
	FunctionInstanceProxy = nullptr;
}

void FMaterialInstanceEditor::InitEditorForMaterialFunction(UMaterialFunctionInstance* InMaterialFunction)
{
	check(InMaterialFunction);
	MaterialFunctionOriginal = InMaterialFunction;

	// Working version of the function instance
	MaterialFunctionInstance = (UMaterialFunctionInstance*)StaticDuplicateObject(InMaterialFunction, GetTransientPackage(), NAME_None, ~RF_Standalone, UMaterialFunctionInstance::StaticClass()); 
	MaterialFunctionInstance->Parent = InMaterialFunction;
	
	// Preview material for function expressions
	FunctionMaterialProxy = NewObject<UMaterial>(); 
	{
		FArchiveUObject DummyArchive;
		FunctionMaterialProxy->Serialize(DummyArchive);
	}

	FunctionMaterialProxy->SetShadingModel(MSM_Unlit);
	FunctionMaterialProxy->SetFlags(RF_Transactional);
	FunctionMaterialProxy->bIsFunctionPreviewMaterial = true;

	UMaterialFunctionInterface* BaseFunction = MaterialFunctionInstance;
	while (UMaterialFunctionInstance* Instance = Cast<UMaterialFunctionInstance>(BaseFunction))
	{
		BaseFunction = Instance->GetBaseFunction();
	}
	const TArray<UMaterialExpression*>* FunctionExpressions = BaseFunction->GetFunctionExpressions();
	FunctionMaterialProxy->Expressions = FunctionExpressions ? *FunctionExpressions : TArray<UMaterialExpression*>();

	// Set expressions to be used with preview material
	bool bSetPreviewExpression = false;
	UMaterialExpressionFunctionOutput* FirstOutput = NULL;
	for (int32 ExpressionIndex = FunctionMaterialProxy->Expressions.Num() - 1; ExpressionIndex >= 0; --ExpressionIndex)
	{
		UMaterialExpression* Expression = FunctionMaterialProxy->Expressions[ExpressionIndex];

		if (!Expression)
		{
			FunctionMaterialProxy->Expressions.RemoveAt(ExpressionIndex);
			continue;
		}

		Expression->Function = NULL;
		Expression->Material = FunctionMaterialProxy;

		if (UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Expression))
		{
			FirstOutput = FunctionOutput;
			if (FunctionOutput->bLastPreviewed)
			{
				bSetPreviewExpression = true;
				FunctionOutput->ConnectToPreviewMaterial(FunctionMaterialProxy, 0);
			}
		}
	}

	if (!bSetPreviewExpression && FirstOutput)
	{
		FirstOutput->ConnectToPreviewMaterial(FunctionMaterialProxy, 0);
	}

	FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
	UpdateContext.AddMaterial(FunctionMaterialProxy);
	FunctionMaterialProxy->PreEditChange(NULL);
	FunctionMaterialProxy->PostEditChange();

	// Preview instance for function expressions
	FunctionInstanceProxy = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transactional);
	FunctionInstanceProxy->SetParentEditorOnly(FunctionMaterialProxy);

	MaterialFunctionInstance->OverrideMaterialInstanceParameterValues(FunctionInstanceProxy);
	FunctionInstanceProxy->PreEditChange(NULL);
	FunctionInstanceProxy->PostEditChange();
}

void FMaterialInstanceEditor::InitMaterialInstanceEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit )
{
	GEditor->RegisterForUndo( this );

	check( ObjectToEdit );
	bIsFunctionPreviewMaterial = !!(MaterialFunctionInstance);
	UMaterialInstanceConstant* InstanceConstant = bIsFunctionPreviewMaterial ? FunctionInstanceProxy : Cast<UMaterialInstanceConstant>(ObjectToEdit);

	bShowAllMaterialParameters = false;

	// Construct a temp holder for our instance parameters.
	MaterialEditorInstance = NewObject<UMaterialEditorInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transactional);

	bool bTempUseOldStyleMICEditorGroups = true;
	GConfig->GetBool(TEXT("/Script/UnrealEd.EditorEngine"), TEXT("UseOldStyleMICEditorGroups"), bTempUseOldStyleMICEditorGroups, GEngineIni);
	MaterialEditorInstance->bUseOldStyleMICEditorGroups = bTempUseOldStyleMICEditorGroups;
	MaterialEditorInstance->SetSourceInstance(InstanceConstant);
	MaterialEditorInstance->SetSourceFunction(MaterialFunctionOriginal);

	MaterialStatsManager = FMaterialStatsUtils::CreateMaterialStats(this);
	MaterialStatsManager->SetMaterialDisplayName(MaterialEditorInstance->SourceInstance->GetName());

	// Register our commands. This will only register them if not previously registered
	FMaterialEditorCommands::Register();

	CreateInternalWidgets();

	BindCommands();

	UpdatePreviewViewportsVisibility();
	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MaterialInstanceEditor_Layout_v5")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()->SetSizeCoefficient(0.1f)->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewStack()->SetSizeCoefficient(0.70f)->SetHideTabWell(true)
					->AddTab(PreviewTabId, ETabState::OpenedTab)
					->AddTab(PreviewSettingsTabId, ETabState::ClosedTab)
				)
				->Split
				(
					FTabManager::NewStack()->SetSizeCoefficient(0.30f)
					->AddTab(PropertiesTabId, ETabState::OpenedTab)
				)
			)
		);

	if (MaterialEditorModule->MaterialLayersEnabled() && !bIsFunctionPreviewMaterial)
	{
		StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MaterialInstanceEditor_Layout_v7")
			->AddArea
			(
				FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewStack()->SetSizeCoefficient(0.1f)->SetHideTabWell(true)
					->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
					->Split
					(
						FTabManager::NewStack()->SetSizeCoefficient(0.70f)->SetHideTabWell(true)
						->AddTab(PreviewTabId, ETabState::OpenedTab)
						->AddTab(PreviewSettingsTabId, ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()->SetSizeCoefficient(0.30f)
						->AddTab(PropertiesTabId, ETabState::OpenedTab)
						->AddTab(LayerPropertiesTabId, ETabState::OpenedTab)
						->SetForegroundTab(PropertiesTabId)
					)
				)
			);
		}

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, MaterialInstanceEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit );

	AddMenuExtender(MaterialEditorModule->GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	ExtendToolbar();
	RegenerateMenusAndToolbars();

	// @todo toolkit world centric editing
	/*if( IsWorldCentricAssetEditor() )
	{
		SpawnToolkitTab(GetToolbarTabId(), FString(), EToolkitTabSpot::ToolBar);
		SpawnToolkitTab(PreviewTabId, FString(), EToolkitTabSpot::Viewport);		
		SpawnToolkitTab(PropertiesTabId, FString(), EToolkitTabSpot::Details);
	}*/

	// Load editor settings.
	LoadSettings();
	
	// Set the preview mesh for the material.  This call must occur after the toolbar is initialized.
	
	if ( !SetPreviewAssetByName( *InstanceConstant->PreviewMesh.ToString() ) )
	{
		// If the preview mesh could not be found for this instance, attempt to use the preview mesh for the parent material if one exists,
		//	or use a default instead if the parent's preview mesh cannot be used

		if ( InstanceConstant->Parent == nullptr || !SetPreviewAssetByName( *InstanceConstant->Parent->PreviewMesh.ToString() ) )
		{
			USceneThumbnailInfoWithPrimitive* ThumbnailInfoWithPrim = Cast<USceneThumbnailInfoWithPrimitive>( InstanceConstant->ThumbnailInfo );

			if ( ThumbnailInfoWithPrim != nullptr )
			{
				SetPreviewAssetByName( *ThumbnailInfoWithPrim->PreviewMesh.ToString() );
			}
		}
	}

	Refresh();
}

void FMaterialInstanceEditor::ReInitMaterialFunctionProxies()
{
	if (bIsFunctionPreviewMaterial)
	{
		// Temporarily store unsaved parameters
		TArray<FScalarParameterValue> ScalarParameterValues = FunctionInstanceProxy->ScalarParameterValues;
		TArray<FVectorParameterValue> VectorParameterValues = FunctionInstanceProxy->VectorParameterValues;
		TArray<FTextureParameterValue> TextureParameterValues = FunctionInstanceProxy->TextureParameterValues;
		TArray<FRuntimeVirtualTextureParameterValue> RuntimeVirtualTextureParameterValues = FunctionInstanceProxy->RuntimeVirtualTextureParameterValues;
		TArray<FFontParameterValue> FontParameterValues = FunctionInstanceProxy->FontParameterValues;

		const FStaticParameterSet& OldStaticParameters = FunctionInstanceProxy->GetStaticParameters();
		TArray<FStaticSwitchParameter> StaticSwitchParameters = OldStaticParameters.StaticSwitchParameters;
		TArray<FStaticComponentMaskParameter> StaticComponentMaskParameters = OldStaticParameters.StaticComponentMaskParameters;

		// Regenerate proxies
		InitEditorForMaterialFunction(MaterialFunctionOriginal);
		MaterialEditorInstance->SetSourceInstance(FunctionInstanceProxy);
		MaterialEditorInstance->SetSourceFunction(MaterialFunctionOriginal);
		
		// Restore dynamic parameters, filtering those that no-longer exist
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;

		FunctionInstanceProxy->GetAllScalarParameterInfo(OutParameterInfo, Guids);
		FunctionInstanceProxy->ScalarParameterValues.Empty();
		for (FScalarParameterValue& ScalarParameter : ScalarParameterValues)
		{
			int32 Index = Guids.Find(ScalarParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FunctionInstanceProxy->ScalarParameterValues.Add(ScalarParameter);
				FunctionInstanceProxy->ScalarParameterValues.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->GetAllVectorParameterInfo(OutParameterInfo, Guids);
		FunctionInstanceProxy->VectorParameterValues.Empty();
		for (FVectorParameterValue& VectorParameter : VectorParameterValues)
		{
			int32 Index = Guids.Find(VectorParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FunctionInstanceProxy->VectorParameterValues.Add(VectorParameter);
				FunctionInstanceProxy->VectorParameterValues.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->GetAllTextureParameterInfo(OutParameterInfo, Guids);
		FunctionInstanceProxy->TextureParameterValues.Empty();
		for (FTextureParameterValue& TextureParameter : TextureParameterValues)
		{
			int32 Index = Guids.Find(TextureParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FunctionInstanceProxy->TextureParameterValues.Add(TextureParameter);
				FunctionInstanceProxy->TextureParameterValues.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->GetAllRuntimeVirtualTextureParameterInfo(OutParameterInfo, Guids);
		FunctionInstanceProxy->RuntimeVirtualTextureParameterValues.Empty();
		for (FRuntimeVirtualTextureParameterValue& RuntimeVirtualTextureParameter : RuntimeVirtualTextureParameterValues)
		{
			int32 Index = Guids.Find(RuntimeVirtualTextureParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FunctionInstanceProxy->RuntimeVirtualTextureParameterValues.Add(RuntimeVirtualTextureParameter);
				FunctionInstanceProxy->RuntimeVirtualTextureParameterValues.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->GetAllFontParameterInfo(OutParameterInfo, Guids);
		FunctionInstanceProxy->FontParameterValues.Empty();
		for (FFontParameterValue& FontParameter : FontParameterValues)
		{
			int32 Index = Guids.Find(FontParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FunctionInstanceProxy->FontParameterValues.Add(FontParameter);
				FunctionInstanceProxy->FontParameterValues.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		// Restore static parameters, filtering those that no-longer exist
		FStaticParameterSet StaticParametersOverride = FunctionInstanceProxy->GetStaticParameters();

		FunctionInstanceProxy->GetAllStaticSwitchParameterInfo(OutParameterInfo, Guids);
		StaticParametersOverride.StaticSwitchParameters.Empty();
		for (FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameters)
		{
			int32 Index = Guids.Find(StaticSwitchParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				StaticParametersOverride.StaticSwitchParameters.Add(StaticSwitchParameter);
				StaticParametersOverride.StaticSwitchParameters.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->GetAllStaticComponentMaskParameterInfo(OutParameterInfo, Guids);
		StaticParametersOverride.StaticComponentMaskParameters.Empty();
		for (FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameters)
		{
			int32 Index = Guids.Find(StaticComponentMaskParameter.ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				StaticParametersOverride.StaticComponentMaskParameters.Add(StaticComponentMaskParameter);
				StaticParametersOverride.StaticComponentMaskParameters.Last().ParameterInfo = OutParameterInfo[Index];
			}
		}

		FunctionInstanceProxy->UpdateStaticPermutation(StaticParametersOverride);

		// Refresh and apply to preview
		FunctionInstanceProxy->PreEditChange(NULL);
		FunctionInstanceProxy->PostEditChange();
		SetPreviewMaterial(FunctionInstanceProxy);
	}
}

FMaterialInstanceEditor::FMaterialInstanceEditor()
: MaterialEditorInstance(nullptr)
, bIsFunctionPreviewMaterial(false)
, MenuExtensibilityManager(new FExtensibilityManager)
, ToolBarExtensibilityManager(new FExtensibilityManager)
, MaterialFunctionOriginal(nullptr)
, MaterialFunctionInstance(nullptr)
, FunctionMaterialProxy(nullptr)
, FunctionInstanceProxy(nullptr)
{
	UPackage::PreSavePackageEvent.AddRaw(this, &FMaterialInstanceEditor::PreSavePackage);
}

FMaterialInstanceEditor::~FMaterialInstanceEditor()
{
	// Broadcast that this editor is going down to all listeners
	OnMaterialEditorClosed().Broadcast();

	GEditor->UnregisterForUndo( this );

	UPackage::PreSavePackageEvent.RemoveAll(this);

	// The streaming data will be null if there were any edits
	if (MaterialEditorInstance && MaterialEditorInstance->SourceInstance && !MaterialEditorInstance->SourceInstance->HasTextureStreamingData())
	{
		UPackage* Package = MaterialEditorInstance->SourceInstance->GetOutermost();
		if (Package && Package->IsDirty() && Package != GetTransientPackage())
		{
			ClearDebugViewMaterials(MaterialEditorInstance->SourceInstance);
			FMaterialEditorUtilities::BuildTextureStreamingData(MaterialEditorInstance->SourceInstance);
		}
	}

	if (MaterialEditorInstance)
	{
		MaterialEditorInstance->SourceInstance = nullptr;
		MaterialEditorInstance->SourceFunction = nullptr;
		MaterialEditorInstance->MarkPendingKill();
		MaterialEditorInstance = nullptr;
	}

	MaterialParentList.Empty();
	FunctionParentList.Empty();

	SaveSettings();

	MaterialInstanceDetails.Reset();
}

void FMaterialInstanceEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Serialize our custom object instance
	Collector.AddReferencedObject(MaterialEditorInstance);
}

void FMaterialInstanceEditor::BindCommands()
{
	const FMaterialEditorCommands& Commands = FMaterialEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.Apply,
		FExecuteAction::CreateSP( this, &FMaterialInstanceEditor::OnApply ),
		FCanExecuteAction::CreateSP( this, &FMaterialInstanceEditor::OnApplyEnabled ),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FMaterialInstanceEditor::OnApplyVisible));

	ToolkitCommands->MapAction(
		Commands.ShowAllMaterialParameters,
		FExecuteAction::CreateSP( this, &FMaterialInstanceEditor::ToggleShowAllMaterialParameters ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( this, &FMaterialInstanceEditor::IsShowAllMaterialParametersChecked ) );

	ToolkitCommands->MapAction(
		FEditorViewportCommands::Get().ToggleRealTime,
		FExecuteAction::CreateSP( PreviewVC.ToSharedRef(), &SMaterialEditor3DPreviewViewport::OnToggleRealtime ),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP( PreviewVC.ToSharedRef(), &SMaterialEditor3DPreviewViewport::IsRealtime ) );
}

void FMaterialInstanceEditor::OnApply()
{
	if (bIsFunctionPreviewMaterial && MaterialEditorInstance)
	{
		UE_LOG(LogMaterialInstanceEditor, Log, TEXT("Applying instance %s"), *GetEditingObjects()[0]->GetName());
		MaterialEditorInstance->bIsFunctionInstanceDirty = true;
		MaterialEditorInstance->ApplySourceFunctionChanges();
	}
}

bool FMaterialInstanceEditor::OnApplyEnabled() const
{
	return MaterialEditorInstance && MaterialEditorInstance->bIsFunctionInstanceDirty == true;
}

bool FMaterialInstanceEditor::OnApplyVisible() const
{
	return MaterialEditorInstance && MaterialEditorInstance->bIsFunctionPreviewMaterial == true;
}

bool FMaterialInstanceEditor::OnRequestClose()
{
	if (MaterialEditorInstance->bIsFunctionInstanceDirty)
	{
		// Find out the user wants to do with this dirty function instance
		EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
			FText::Format(
				NSLOCTEXT("UnrealEd", "Prompt_MaterialInstanceEditorClose", "Would you like to apply changes to this instance to the original asset?\n{0}\n(No will lose all changes!)"),
				FText::FromString(MaterialFunctionOriginal->GetPathName())));

		switch (YesNoCancelReply)
		{
		case EAppReturnType::Yes:
			// Update instance and exit
			MaterialEditorInstance->ApplySourceFunctionChanges();
			break;
				
		case EAppReturnType::No:
			// Exit
			break;
				
		case EAppReturnType::Cancel:
			// Don't exit
			return false;
		}
	}

	return true;
}

void FMaterialInstanceEditor::ToggleShowAllMaterialParameters()
{
	bShowAllMaterialParameters = !bShowAllMaterialParameters;
	UpdatePropertyWindow();
}

bool FMaterialInstanceEditor::IsShowAllMaterialParametersChecked() const
{
	return bShowAllMaterialParameters;
}

void FMaterialInstanceEditor::CreateInternalWidgets()
{
	PreviewVC = SNew(SMaterialEditor3DPreviewViewport)
		.MaterialEditor(SharedThis(this));

	PreviewUIViewport = SNew(SMaterialEditorUIPreviewViewport, GetMaterialInterface());

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
	FDetailsViewArgs DetailsViewArgs( false, false, true, FDetailsViewArgs::HideNameArea, true, this );
	DetailsViewArgs.bShowModifiedPropertiesOption = false;
	DetailsViewArgs.bShowCustomFilterOption = true;
	MaterialInstanceDetails = PropertyEditorModule.CreateDetailView( DetailsViewArgs );
	FOnGetDetailCustomizationInstance LayoutMICDetails = FOnGetDetailCustomizationInstance::CreateStatic( 
		&FMaterialInstanceParameterDetails::MakeInstance, MaterialEditorInstance, FGetShowHiddenParameters::CreateSP(this, &FMaterialInstanceEditor::GetShowHiddenParameters) );
	MaterialInstanceDetails->RegisterInstancedCustomPropertyLayout( UMaterialEditorInstanceConstant::StaticClass(), LayoutMICDetails );
	MaterialInstanceDetails->SetCustomFilterLabel(LOCTEXT("ShowOverriddenOnly", "Show Only Overridden Parameters"));
	MaterialInstanceDetails->SetCustomFilterDelegate(FSimpleDelegate::CreateSP(this, &FMaterialInstanceEditor::FilterOverriddenProperties));
	MaterialEditorInstance->DetailsView = MaterialInstanceDetails;

	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	if (MaterialEditorModule->MaterialLayersEnabled() && !bIsFunctionPreviewMaterial)
	{
		MaterialLayersFunctionsInstance = SNew(SMaterialLayersFunctionsInstanceWrapper)
			.InMaterialEditorInstance(MaterialEditorInstance)
			.InShowHiddenDelegate(FGetShowHiddenParameters::CreateSP(this, &FMaterialInstanceEditor::GetShowHiddenParameters));
	}
}

void FMaterialInstanceEditor::FilterOverriddenProperties()
{
	MaterialEditorInstance->bShowOnlyOverrides = !MaterialEditorInstance->bShowOnlyOverrides;
	MaterialInstanceDetails->ForceRefresh();
}

void FMaterialInstanceEditor::UpdatePreviewViewportsVisibility()
{
	UMaterial* PreviewMaterial = GetMaterialInterface()->GetBaseMaterial();
	if( PreviewMaterial->IsUIMaterial() )
	{
		PreviewVC->SetVisibility(EVisibility::Collapsed);
		PreviewUIViewport->SetVisibility(EVisibility::Visible);
	}
	else
	{
		PreviewVC->SetVisibility(EVisibility::Visible);
		PreviewUIViewport->SetVisibility(EVisibility::Collapsed);
	}
}

void FMaterialInstanceEditor::RegisterToolBar()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* ToolBar = ToolMenus->ExtendMenu(GetToolMenuToolbarName());

	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& Section = ToolBar->AddSection("Apply", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMaterialEditorCommands::Get().Apply));
	}

	{
		FToolMenuSection& Section = ToolBar->AddSection("Command", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMaterialEditorCommands::Get().ShowAllMaterialParameters));
		// TODO: support in material instance editor.
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMaterialEditorCommands::Get().TogglePlatformStats));
	}
	
	{
		FToolMenuSection& Section = ToolBar->AddSection("Parent", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"Hierarchy",
			FToolUIActionChoice(),
			FNewToolMenuDelegate::CreateSP(this, &FMaterialInstanceEditor::GenerateInheritanceMenu),
			LOCTEXT("Hierarchy", "Hierarchy"),
			FText::GetEmpty(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "BTEditor.SwitchToBehaviorTreeMode"),
			false
		));
	}

}

void FMaterialInstanceEditor::ExtendToolbar()
{
	RegisterToolBar();

	AddToolbarExtender(GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
	AddToolbarExtender(MaterialEditorModule->GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FMaterialInstanceEditor::GenerateInheritanceMenu(UToolMenu* Menu)
{
	struct Local
	{
		static void AddMenuEntry(FToolMenuSection& Section, FAssetData AssetData, bool bIsFunctionPreviewMaterial)
		{
			FExecuteAction OpenAction;
			FExecuteAction FindInContentBrowserAction;
			if (bIsFunctionPreviewMaterial)
			{
				OpenAction.BindStatic(&FMaterialEditorUtilities::OnOpenFunction, AssetData);
				FindInContentBrowserAction.BindStatic(&FMaterialEditorUtilities::OnShowFunctionInContentBrowser, AssetData);
			}
			else
			{
				OpenAction.BindStatic(&FMaterialEditorUtilities::OnOpenMaterial, AssetData);
				FindInContentBrowserAction.BindStatic(&FMaterialEditorUtilities::OnShowMaterialInContentBrowser, AssetData);
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("ParentName"), FText::FromName(AssetData.AssetName));
			FText Label = FText::Format(LOCTEXT("InstanceParentName", "{ParentName}"), Args);

			FSlateIcon OpenIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.OpenInExternalEditor");
			FSlateIcon FindInContentBrowserIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser");

			TSharedRef<SWidget> EntryWidget =
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("OpenInEditor", "Open In Editor"))

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(2, 0, 2, 0))
				[
					SNew( SBox )
					.WidthOverride( MultiBoxConstants::MenuIconSize + 2 )
					.HeightOverride( MultiBoxConstants::MenuIconSize )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew( SBox )
						.WidthOverride( MultiBoxConstants::MenuIconSize )
						.HeightOverride( MultiBoxConstants::MenuIconSize )
						[
							SNew(SImage)
							.Image(OpenIcon.GetIcon())
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth( 1.0f )
				.Padding(FMargin(2, 0, 6, 0))
				.VAlign( VAlign_Center )
				[
					SNew(STextBlock)
					.Text(Label)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign( VAlign_Center )
				.HAlign( HAlign_Right )
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("FindInContentBrowser", "Find In Content Browser"))
					.OnClicked_Lambda([FindInContentBrowserAction]() { FindInContentBrowserAction.ExecuteIfBound(); return FReply::Handled(); })
					[
						SNew( SBox )
						.WidthOverride( MultiBoxConstants::MenuIconSize + 2 )
						.HeightOverride( MultiBoxConstants::MenuIconSize )
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew( SBox )
							.WidthOverride( MultiBoxConstants::MenuIconSize )
							.HeightOverride( MultiBoxConstants::MenuIconSize )
							[
								SNew(SImage)
								.Image(FindInContentBrowserIcon.GetIcon())
							]
						]
					]
				];

			Section.AddEntry(FToolMenuEntry::InitMenuEntry(
				NAME_None,
				FUIAction(OpenAction),
				EntryWidget
			));
		}
	};

	RebuildInheritanceList();
	Menu->bShouldCloseWindowAfterMenuSelection = true;
	Menu->bSearchable = true;
	Menu->SetMaxHeight(500);
	const FName ParentName = TEXT("ParentChain");
	FToolMenuSection& Section = Menu->AddSection(ParentName, LOCTEXT("ParentChain", "Parent Chain"));
	if (bIsFunctionPreviewMaterial)
	{
		if (FunctionParentList.Num() == 0)
		{
			const FText NoParentText = LOCTEXT("NoParentFound", "No Parent Found");
			TSharedRef<SWidget> NoParentWidget = SNew(STextBlock)
				.Text(NoParentText);
			Section.AddEntry(FToolMenuEntry::InitWidget("NoParentEntry", NoParentWidget, FText::GetEmpty()));
		}
		for (FAssetData FunctionParent : FunctionParentList)
		{
			Local::AddMenuEntry(Section, FunctionParent, bIsFunctionPreviewMaterial);
		}
	}
	else
	{
		if (MaterialParentList.Num() == 0)
		{
			const FText NoParentText = LOCTEXT("NoParentFound", "No Parent Found");
			TSharedRef<SWidget> NoParentWidget = SNew(STextBlock)
				.Text(NoParentText);
			Section.AddEntry(FToolMenuEntry::InitWidget("NoParentEntry", NoParentWidget, FText::GetEmpty()));
		}
		for (FAssetData MaterialParent : MaterialParentList)
		{
			Local::AddMenuEntry(Section, MaterialParent, bIsFunctionPreviewMaterial);
		}
	}

	if (!bIsFunctionPreviewMaterial)
	{
		const FName MaterialInstances = TEXT("MaterialInstances");
		FToolMenuSection& MaterialInstancesSection = Menu->AddSection(MaterialInstances, LOCTEXT("MaterialInstances", "Material Instances"));
		for (FAssetData MaterialChild : MaterialChildList)
		{
			Local::AddMenuEntry(MaterialInstancesSection, MaterialChild, bIsFunctionPreviewMaterial);
		}
	}
}


TSharedRef<SDockTab> FMaterialInstanceEditor::SpawnTab_Preview( const FSpawnTabArgs& Args )
{	
	check( Args.GetTabId().TabType == PreviewTabId );

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("ViewportTabTitle", "Viewport"))
		[
			SNew( SOverlay )
			+ SOverlay::Slot()
			[
				PreviewVC.ToSharedRef()
			]
			+ SOverlay::Slot()
			[
				PreviewUIViewport.ToSharedRef()
			]
		];

	PreviewVC->OnAddedToTab( SpawnedTab );

	AddToSpawnedToolPanels( Args.GetTabId().TabType, SpawnedTab );
	return SpawnedTab;
}


TSharedRef<SDockTab> FMaterialInstanceEditor::SpawnTab_Properties( const FSpawnTabArgs& Args )
{	
	check( Args.GetTabId().TabType == PropertiesTabId );

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon( FEditorStyle::GetBrush("MaterialInstanceEditor.Tabs.Properties") )
		.Label(LOCTEXT("MaterialPropertiesTitle", "Details"))
		[
			SNew(SBorder)
			.Padding(4)
			[
				MaterialInstanceDetails.ToSharedRef()
			]
		];

	UpdatePropertyWindow();

	AddToSpawnedToolPanels( Args.GetTabId().TabType, SpawnedTab );
	return SpawnedTab;
}

TSharedRef<SDockTab> FMaterialInstanceEditor::SpawnTab_LayerProperties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == LayerPropertiesTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("MaterialInstanceEditor.Tabs.Properties"))
		.Label(LOCTEXT("MaterialLayerPropertiesTitle", "Layer Parameters"))
		[
			SNew(SBorder)
			.Padding(4)
		[
			MaterialLayersFunctionsInstance.ToSharedRef()
		]
		];

	AddToSpawnedToolPanels(Args.GetTabId().TabType, SpawnedTab);
	return SpawnedTab;
}

TSharedRef<SDockTab> FMaterialInstanceEditor::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PreviewSettingsTabId);

	TSharedRef<SWidget> InWidget = SNullWidget::NullWidget;
	if (PreviewVC.IsValid())
	{
		FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
		InWidget = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(PreviewVC->GetPreviewScene());
	}

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			SNew(SBox)
			[
				InWidget
			]
		];

	return SpawnedTab;
}

void FMaterialInstanceEditor::AddToSpawnedToolPanels(const FName& TabIdentifier, const TSharedRef<SDockTab>& SpawnedTab)
{
	TWeakPtr<SDockTab>* TabSpot = SpawnedToolPanels.Find(TabIdentifier);
	if (!TabSpot)
	{
		SpawnedToolPanels.Add(TabIdentifier, SpawnedTab);
	}
	else
	{
		check(!TabSpot->IsValid());
		*TabSpot = SpawnedTab;
	}
}

FName FMaterialInstanceEditor::GetToolkitFName() const
{
	return FName("MaterialInstanceEditor");
}

FText FMaterialInstanceEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Material Instance Editor");
}

FString FMaterialInstanceEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Material Instance ").ToString();
}

FLinearColor FMaterialInstanceEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.3f, 0.2f, 0.5f, 0.5f );
}

UMaterialInterface* FMaterialInstanceEditor::GetMaterialInterface() const
{
	return MaterialEditorInstance->SourceInstance;
}

void FMaterialInstanceEditor::NotifyPreChange(FProperty* PropertyThatChanged)
{
}

void FMaterialInstanceEditor::NotifyPostChange( const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	// If they changed the parent, regenerate the parent list.
	if(PropertyThatChanged->GetName()==TEXT("Parent"))
	{
		bool bSetEmptyParent = false;

		// Check to make sure they didnt set the parent to themselves.
		if(MaterialEditorInstance->Parent==MaterialEditorInstance->SourceInstance)
		{
			bSetEmptyParent = true;
		}

		if (bSetEmptyParent)
		{
			FMaterialUpdateContext UpdateContext;
			MaterialEditorInstance->Parent = NULL;

			if(MaterialEditorInstance->SourceInstance)
			{
				MaterialEditorInstance->SourceInstance->SetParentEditorOnly(NULL);
				MaterialEditorInstance->SourceInstance->PostEditChange();
			}
			UpdateContext.AddMaterialInstance(MaterialEditorInstance->SourceInstance);
		}

		RebuildInheritanceList();

		UpdatePropertyWindow();
	}
	else if(PropertyThatChanged->GetName() == TEXT("PreviewMesh"))
	{
		RefreshPreviewAsset();
	}

	// Rebuild the property window to account for the possibility that  
	// the item changed was a static switch or function call parameter
	UObject* PropertyClass = PropertyThatChanged->GetOwner<UObject>();
	if(PropertyClass && (PropertyClass->GetName() == TEXT("DEditorStaticSwitchParameterValue") || PropertyClass->GetName() == TEXT("EditorParameterGroup"))//DEditorMaterialLayerParameters"))
		&& MaterialEditorInstance->Parent && MaterialEditorInstance->SourceInstance )
	{
		// TODO: We need to hit this on MaterialLayerParam updates but only get notifications for their array elements changing, hence the overly generic test above
		MaterialEditorInstance->VisibleExpressions.Empty();
		FMaterialEditorUtilities::GetVisibleMaterialParameters(MaterialEditorInstance->Parent->GetMaterial(), MaterialEditorInstance->SourceInstance, MaterialEditorInstance->VisibleExpressions);

		UpdatePropertyWindow();
	}

	// something was changed in the material so we need to reflect this in the stats
	MaterialStatsManager->SignalMaterialChanged();

	// Update the preview window when the user changes a property.
	PreviewVC->RefreshViewport();
}

void FMaterialInstanceEditor::RefreshPreviewAsset()
{
	UObject* PreviewAsset = MaterialEditorInstance->SourceInstance->PreviewMesh.TryLoad();
	if (!PreviewAsset)
	{
		// Attempt to use the parent material's preview mesh if the instance's preview mesh is invalid, and use a default
		//	sphere instead if the parent's mesh is also invalid
		UMaterialInterface* ParentMaterial = MaterialEditorInstance->SourceInstance->Parent;

		UObject* ParentPreview = ParentMaterial != nullptr ? ParentMaterial->PreviewMesh.TryLoad() : nullptr;
		PreviewAsset = ParentPreview != nullptr ? ParentPreview : GUnrealEd->GetThumbnailManager()->EditorSphere;

		USceneThumbnailInfoWithPrimitive* ThumbnailInfo = Cast<USceneThumbnailInfoWithPrimitive>(MaterialEditorInstance->SourceInstance->ThumbnailInfo);
		if (ThumbnailInfo)
		{
			ThumbnailInfo->PreviewMesh.Reset();
		}

	}
	PreviewVC->SetPreviewAsset(PreviewAsset);
}

void FMaterialInstanceEditor::PreSavePackage(UPackage* Package)
{
	// The streaming data will be null if there were any edits
	if (MaterialEditorInstance && 
		MaterialEditorInstance->SourceInstance && 
		MaterialEditorInstance->SourceInstance->GetOutermost() == Package &&
		!MaterialEditorInstance->SourceInstance->HasTextureStreamingData())
	{
		FMaterialEditorUtilities::BuildTextureStreamingData(MaterialEditorInstance->SourceInstance);
	}
}

void FMaterialInstanceEditor::RebuildInheritanceList()
{
	if (bIsFunctionPreviewMaterial)
	{
		FunctionParentList.Empty();

		// Append function instance parent chain
		UMaterialFunctionInstance* Current = MaterialFunctionOriginal;
		UMaterialFunctionInterface* Parent = Current->Parent;
		while (Parent)
		{
			FunctionParentList.Insert(Parent, 0);

			Current = Cast<UMaterialFunctionInstance>(Parent);
			Parent = Current ? Current->Parent : nullptr;
		}
	}
	else
	{
		MaterialChildList.Empty();
		MaterialParentList.Empty();

		// Travel up the parent chain for this material instance until we reach the root material.
		UMaterialInstance* InstanceConstant = MaterialEditorInstance->SourceInstance;

		if(InstanceConstant)
		{
			UMaterialEditingLibrary::GetChildInstances(InstanceConstant, MaterialChildList);

			// Add all parents
			UMaterialInterface* Parent = InstanceConstant->Parent;
			while(Parent && Parent != InstanceConstant)
			{
				MaterialParentList.Insert(Parent,0);

				// If the parent is a material then break.
				InstanceConstant = Cast<UMaterialInstance>(Parent);

				if(InstanceConstant)
				{
					Parent = InstanceConstant->Parent;
				}
				else
				{
					break;
				}
			}
		}
	}
}

void FMaterialInstanceEditor::RebuildMaterialInstanceEditor()
{
	if( MaterialEditorInstance )
	{
		ReInitMaterialFunctionProxies();
		MaterialEditorInstance->CopyBasePropertiesFromParent();
		MaterialEditorInstance->RegenerateArrays();
		RebuildInheritanceList(); // Required b/c recompiled parent materials result in invalid weak object pointers
		UpdatePropertyWindow();
	}
}

void FMaterialInstanceEditor::DrawMessages( FViewport* Viewport, FCanvas* Canvas )
{
	Canvas->PushAbsoluteTransform(FMatrix::Identity);
	if ( MaterialEditorInstance->Parent && MaterialEditorInstance->SourceInstance )
	{
		const FMaterialResource* MaterialResource = MaterialEditorInstance->SourceInstance->GetMaterialResource(GMaxRHIFeatureLevel);
		UMaterial* BaseMaterial = MaterialEditorInstance->SourceInstance->GetMaterial();
		int32 DrawPositionY = 50;
		if ( BaseMaterial && MaterialResource )
		{
			const bool bGeneratedNewShaders = MaterialEditorInstance->SourceInstance->bHasStaticPermutationResource;
			const bool bAllowOldMaterialStats = true;
			FMaterialEditor::DrawMaterialInfoStrings( Canvas, BaseMaterial, MaterialResource, MaterialResource->GetCompileErrors(), DrawPositionY, bAllowOldMaterialStats, bGeneratedNewShaders );
		}

		DrawSamplerWarningStrings( Canvas, DrawPositionY );
	}
	Canvas->PopTransform();
}

/**
 * Draws sampler/texture mismatch warning strings.
 * @param Canvas - The canvas on which to draw.
 * @param DrawPositionY - The Y position at which to draw. Upon return contains the Y value following the last line of text drawn.
 */
void FMaterialInstanceEditor::DrawSamplerWarningStrings(FCanvas* Canvas, int32& DrawPositionY)
{
	if ( MaterialEditorInstance->SourceInstance )
	{
		UMaterial* BaseMaterial = MaterialEditorInstance->SourceInstance->GetMaterial();
		if ( BaseMaterial )
		{
			UFont* FontToUse = GEngine->GetTinyFont();
			const int32 SpacingBetweenLines = 13;
			UEnum* SamplerTypeEnum = StaticEnum<EMaterialSamplerType>();
			check( SamplerTypeEnum );
			UEnum* MaterialTypeEnum = StaticEnum<ERuntimeVirtualTextureMaterialType>();
			check(MaterialTypeEnum);

			const int32 GroupCount = MaterialEditorInstance->ParameterGroups.Num();
			for ( int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex )
			{
				const FEditorParameterGroup& Group = MaterialEditorInstance->ParameterGroups[ GroupIndex ];
				const int32 ParameterCount = Group.Parameters.Num();
				for ( int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex )
				{
					UDEditorTextureParameterValue* TextureParameterValue = Cast<UDEditorTextureParameterValue>( Group.Parameters[ ParameterIndex ] );
					if ( TextureParameterValue && TextureParameterValue->ExpressionId.IsValid() )
					{
						UTexture* Texture = NULL;
						MaterialEditorInstance->SourceInstance->GetTextureParameterValue( TextureParameterValue->ParameterInfo, Texture );
						if ( Texture )
						{
							EMaterialSamplerType SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture );
							UMaterialExpressionTextureSampleParameter* Expression = BaseMaterial->FindExpressionByGUID<UMaterialExpressionTextureSampleParameter>( TextureParameterValue->ExpressionId );

							FString ErrorMessage;
							if (Expression && !Expression->TextureIsValid(Texture, ErrorMessage))
							{
								Canvas->DrawShadowedString(
									5,
									DrawPositionY,
									*FString::Printf(TEXT("Error: %s has invalid texture %s: %s."),
										*TextureParameterValue->ParameterInfo.Name.ToString(),
										*Texture->GetPathName(),
										*ErrorMessage),
									FontToUse,
									FLinearColor(1, 0, 0));
								DrawPositionY += SpacingBetweenLines;
							}
							else
							{
								if (Expression && Expression->SamplerType != SamplerType)
								{
									FString SamplerTypeDisplayName = SamplerTypeEnum->GetDisplayNameTextByValue(Expression->SamplerType).ToString();

									Canvas->DrawShadowedString(
										5,
										DrawPositionY,
										*FString::Printf(TEXT("Warning: %s samples %s as %s."),
											*TextureParameterValue->ParameterInfo.Name.ToString(),
											*Texture->GetPathName(),
											*SamplerTypeDisplayName),
										FontToUse,
										FLinearColor(1, 1, 0));
									DrawPositionY += SpacingBetweenLines;
								}
								if (Expression && ((Expression->SamplerType == (EMaterialSamplerType)TC_Normalmap || Expression->SamplerType == (EMaterialSamplerType)TC_Masks) && Texture->SRGB))
								{
									FString SamplerTypeDisplayName = SamplerTypeEnum->GetDisplayNameTextByValue(Expression->SamplerType).ToString();

									Canvas->DrawShadowedString(
										5,
										DrawPositionY,
										*FString::Printf(TEXT("Warning: %s samples texture as '%s'. SRGB should be disabled for '%s'."),
											*TextureParameterValue->ParameterInfo.Name.ToString(),
											*SamplerTypeDisplayName,
											*Texture->GetPathName()),
										FontToUse,
										FLinearColor(1, 1, 0));
									DrawPositionY += SpacingBetweenLines;
								}
							}
						}
					}

					UDEditorRuntimeVirtualTextureParameterValue* RuntimeVirtualTextureParameterValue = Cast<UDEditorRuntimeVirtualTextureParameterValue>(Group.Parameters[ParameterIndex]);
					if (RuntimeVirtualTextureParameterValue && RuntimeVirtualTextureParameterValue->ExpressionId.IsValid())
					{
						URuntimeVirtualTexture* RuntimeVirtualTexture = NULL;
						MaterialEditorInstance->SourceInstance->GetRuntimeVirtualTextureParameterValue(RuntimeVirtualTextureParameterValue->ParameterInfo, RuntimeVirtualTexture);
						if (RuntimeVirtualTexture)
						{
							UMaterialExpressionRuntimeVirtualTextureSampleParameter* Expression = BaseMaterial->FindExpressionByGUID<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(RuntimeVirtualTextureParameterValue->ExpressionId);
							if (Expression->MaterialType != RuntimeVirtualTexture->GetMaterialType())
							{
								FString BaseMaterialTypeDisplayName = MaterialTypeEnum->GetDisplayNameTextByValue((int64)(Expression->MaterialType)).ToString();
								FString OverrideMaterialTypeDisplayName = MaterialTypeEnum->GetDisplayNameTextByValue((int64)(RuntimeVirtualTexture->GetMaterialType())).ToString();

								Canvas->DrawShadowedString(
									5, DrawPositionY,
									*FString::Printf(TEXT("Warning: '%s' interprets the virtual texture as '%s' not '%s'"),
										*RuntimeVirtualTextureParameterValue->ParameterInfo.Name.ToString(),
										*BaseMaterialTypeDisplayName,
										*OverrideMaterialTypeDisplayName,
										*RuntimeVirtualTexture->GetPathName()),
									FontToUse,
									FLinearColor(1, 1, 0));

								DrawPositionY += SpacingBetweenLines;
							}
							if (Expression->bSinglePhysicalSpace != RuntimeVirtualTexture->GetSinglePhysicalSpace())
							{
								Canvas->DrawShadowedString(
									5, DrawPositionY,
									*FString::Printf(TEXT("Warning: '%s' interprets the virtual texture page table packing as '%d' not '%d'"),
										*RuntimeVirtualTextureParameterValue->ParameterInfo.Name.ToString(),
										RuntimeVirtualTexture->GetSinglePhysicalSpace() ? 1 : 0,
										Expression->bSinglePhysicalSpace ? 1 : 0,
										*RuntimeVirtualTexture->GetPathName()),
									FontToUse,
									FLinearColor(1, 1, 0));

								DrawPositionY += SpacingBetweenLines;
							}
						}
					}
				}
			}
		}
	}
}

bool FMaterialInstanceEditor::SetPreviewAsset(UObject* InAsset)
{
	if (PreviewVC.IsValid())
	{
		return PreviewVC->SetPreviewAsset(InAsset);
	}
	return false;
}

bool FMaterialInstanceEditor::SetPreviewAssetByName(const TCHAR* InAssetName)
{
	if (PreviewVC.IsValid())
	{
		return PreviewVC->SetPreviewAssetByName(InAssetName);
	}
	return false;
}

void FMaterialInstanceEditor::SetPreviewMaterial(UMaterialInterface* InMaterialInterface)
{
	if (PreviewVC.IsValid())
	{
		PreviewVC->SetPreviewMaterial(InMaterialInterface);
	}
}

void FMaterialInstanceEditor::GetShowHiddenParameters(bool& bShowHiddenParameters)
{
	bShowHiddenParameters = bShowAllMaterialParameters;
}

void FMaterialInstanceEditor::Tick(float DeltaTime)
{
	MaterialStatsManager->SetMaterial(MaterialEditorInstance->SourceInstance);
	MaterialStatsManager->Update();
}

TStatId FMaterialInstanceEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FMaterialInstanceEditor, STATGROUP_Tickables);
}

void FMaterialInstanceEditor::SaveAsset_Execute()
{
	if (bIsFunctionPreviewMaterial && MaterialEditorInstance)
	{
		UE_LOG(LogMaterialInstanceEditor, Log, TEXT("Saving and applying instance %s"), *GetEditingObjects()[0]->GetName());
		MaterialEditorInstance->ApplySourceFunctionChanges();
	}

	IMaterialEditor::SaveAsset_Execute();
}

void FMaterialInstanceEditor::SaveAssetAs_Execute()
{
	if (bIsFunctionPreviewMaterial && MaterialEditorInstance)
	{
		UE_LOG(LogMaterialInstanceEditor, Log, TEXT("Saving and applying instance %s"), *GetEditingObjects()[0]->GetName());
		MaterialEditorInstance->ApplySourceFunctionChanges();
	}

	IMaterialEditor::SaveAssetAs_Execute();
}

void FMaterialInstanceEditor::SaveSettings()
{
	GConfig->SetBool(TEXT("MaterialInstanceEditor"), TEXT("bShowGrid"), PreviewVC->IsTogglePreviewGridChecked(), GEditorPerProjectIni);
	GConfig->SetBool(TEXT("MaterialInstanceEditor"), TEXT("bDrawGrid"), PreviewVC->IsRealtime(), GEditorPerProjectIni);
	GConfig->SetInt(TEXT("MaterialInstanceEditor"), TEXT("PrimType"), PreviewVC->PreviewPrimType, GEditorPerProjectIni);
}

void FMaterialInstanceEditor::LoadSettings()
{
	bool bRealtime=false;
	bool bShowGrid=false;
	int32 PrimType=static_cast<EThumbnailPrimType>( TPT_Sphere );
	GConfig->GetBool(TEXT("MaterialInstanceEditor"), TEXT("bShowGrid"), bShowGrid, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("MaterialInstanceEditor"), TEXT("bDrawGrid"), bRealtime, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("MaterialInstanceEditor"), TEXT("PrimType"), PrimType, GEditorPerProjectIni);

	if(PreviewVC.IsValid())
	{
		if ( bShowGrid )
		{
			PreviewVC->TogglePreviewGrid();
		}
		if ( bRealtime )
		{
			PreviewVC->OnToggleRealtime();
		}

		PreviewVC->OnSetPreviewPrimitive( static_cast<EThumbnailPrimType>(PrimType), true);
	}
}

void FMaterialInstanceEditor::OpenSelectedParentEditor(UMaterialInterface* InMaterialInterface)
{
	ensure(InMaterialInterface);

	// See if its a material or material instance constant.  Don't do anything if the user chose the current material instance.
	if(InMaterialInterface && MaterialEditorInstance->SourceInstance!=InMaterialInterface)
	{
		if(InMaterialInterface->IsA(UMaterial::StaticClass()))
		{
			// Show material editor
			UMaterial* Material = Cast<UMaterial>(InMaterialInterface);
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Material);
		}
		else if(InMaterialInterface->IsA(UMaterialInstance::StaticClass()))
		{
			// Show material instance editor
			UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(InMaterialInterface);
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(MaterialInstance);
		}
	}
}

void FMaterialInstanceEditor::OpenSelectedParentEditor(UMaterialFunctionInterface* InMaterialFunction)
{
	ensure(InMaterialFunction);

	// See if its a material or material instance constant.  Don't do anything if the user chose the current material instance.
	if(InMaterialFunction && MaterialFunctionOriginal != InMaterialFunction)
	{
		if(InMaterialFunction->IsA(UMaterialFunctionInstance::StaticClass()))
		{
			// Show function instance editor
			UMaterialFunctionInstance* FunctionInstance = Cast<UMaterialFunctionInstance>(InMaterialFunction);
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(FunctionInstance);
		}
		else
		{
			// Show function editor
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(InMaterialFunction);
		}
	}
}

void FMaterialInstanceEditor::UpdatePropertyWindow()
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Add( MaterialEditorInstance );
	MaterialInstanceDetails->SetObjects( SelectedObjects, true );
	if (MaterialLayersFunctionsInstance.IsValid())
	{
		MaterialLayersFunctionsInstance->SetEditorInstance(MaterialEditorInstance);
	}
}

UObject* FMaterialInstanceEditor::GetSyncObject()
{
	if (MaterialEditorInstance)
	{
		return MaterialEditorInstance->SourceInstance;
	}
	return NULL;
}

bool FMaterialInstanceEditor::ApproveSetPreviewAsset(UObject* InAsset)
{
	// Default impl is to always accept.
	return true;
}

void FMaterialInstanceEditor::Refresh()
{
	int32 TempIndex;
	const bool bParentChanged = !MaterialParentList.Find( MaterialEditorInstance->Parent, TempIndex );

	PreviewVC->RefreshViewport();

	if( bParentChanged )
	{
		RebuildInheritanceList();
	}
	
	UpdatePropertyWindow();
}

void FMaterialInstanceEditor::PostUndo( bool bSuccess )
{
	MaterialEditorInstance->CopyToSourceInstance();
	RefreshPreviewAsset();
	Refresh();
}

void FMaterialInstanceEditor::PostRedo( bool bSuccess )
{
	MaterialEditorInstance->CopyToSourceInstance();
	RefreshPreviewAsset();
	Refresh();
}

void FMaterialInstanceEditor::NotifyExternalMaterialChange()
{
	MaterialStatsManager->SignalMaterialChanged();
}

#undef LOCTEXT_NAMESPACE
