// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialStats.h"
#include "MaterialStatsGrid.h"
#include "SMaterialEditorStatsWidget.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "MaterialEditorActions.h"
#include "Materials/MaterialInstance.h"
#include "IMaterialEditor.h"
#include "Preferences/MaterialStatsOptions.h"
#include "MaterialEditorSettings.h"

#include "Modules/ModuleManager.h"
#include "Developer/MessageLog/Public/MessageLogModule.h"

#define LOCTEXT_NAMESPACE "MaterialStats"

const FName FMaterialStats::StatsTabId(TEXT("MaterialStats_Grid"));
const FName FMaterialStats::OldStatsTabId(TEXT("OldMaterialStats_Grid"));
const FName FMaterialStats::HLSLCodeTabId(TEXT("MaterialStats_HLSLCode"));

/***********************************************************************************************************************/
/*begin FShaderPlatformSettings functions*/

FShaderPlatformSettings::FShaderPlatformSettings(
	const EPlatformCategoryType _PlatformType,
	const EShaderPlatform _ShaderPlatformID,
	const FName _Name,
	const bool _bAllowPresenceInGrid,
	const bool _bAllowCodeView,
	const FString& _Description)
	:
	PlatformType(_PlatformType),
	PlatformShaderID(_ShaderPlatformID),
	PlatformName(_Name),
	PlatformDescription(_Description),
	bAllowCodeView(_bAllowCodeView),
	bAllowPresenceInGrid(_bAllowPresenceInGrid)
{
	PlatformNameID = *FMaterialStatsUtils::ShaderPlatformTypeName(PlatformShaderID);
}

void FShaderPlatformSettings::ClearResources()
{
	// free material resources
	for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
	{
		if (PlatformData[i].MaterialResourcesStats != nullptr)
		{
			delete PlatformData[i].MaterialResourcesStats;
			PlatformData[i].MaterialResourcesStats = nullptr;
		}

		PlatformData[i].ArrShaderNames.Empty();

		PlatformData[i].bCompilingShaders = false;
		PlatformData[i].bNeedShaderRecompilation = true;
	}
}

FText FShaderPlatformSettings::GetSelectedShaderViewComboText(EMaterialQualityLevel::Type QualityLevel) const
{
	if (PlatformData[QualityLevel].ArrShaderNames.Num() == 0)
	{
		return FText::FromString(TEXT("-Compiling-Shaders-"));
	}

	return FText::FromName(PlatformData[QualityLevel].ComboBoxSelectedName);
}

void FShaderPlatformSettings::OnShaderViewComboSelectionChanged(TSharedPtr<FName> Item, EMaterialQualityLevel::Type QualityType)
{
	if (Item.IsValid())
	{
		PlatformData[QualityType].ComboBoxSelectedName = *Item.Get();
		PlatformData[QualityType].bUpdateShaderCode = true;
	}
}

FText FShaderPlatformSettings::GetShaderCode(const EMaterialQualityLevel::Type QualityType)
{
	// if there were no change to the material return the cached shader code
	if (!PlatformData[QualityType].bUpdateShaderCode)
	{
		return PlatformData[QualityType].ShaderCode;
	}

	PlatformData[QualityType].ShaderCode = LOCTEXT("ShaderCodeMsg", "Shader code compiling or not available!");

	FMaterialResource *Resource = PlatformData[QualityType].MaterialResourcesStats;
	const FMaterialShaderMap* MaterialShaderMap = Resource->GetGameThreadShaderMap();

	const bool bCompilationFinished = Resource->IsCompilationFinished() && (MaterialShaderMap != nullptr);

	// check if shader compilation is done and extract shader code
	if (bCompilationFinished)
	{
		TMap<FHashedName, TShaderRef<FShader>> ShaderMap;
		MaterialShaderMap->GetShaderList(ShaderMap);

		const auto Entry = ShaderMap.Find(PlatformData[QualityType].ComboBoxSelectedName);
		if (Entry != nullptr)
		{
			const TShaderRef<FShader>& Shader = *Entry;

			const FName ShaderFName = Shader.GetType()->GetFName();
			const FMemoryImageString* ShaderSource = MaterialShaderMap->GetShaderSource(ShaderFName);
			if (ShaderSource != nullptr)
			{
				PlatformData[QualityType].bUpdateShaderCode = false;
				PlatformData[QualityType].ShaderCode = FText::FromString(*ShaderSource);
			}
		}
	}

	return PlatformData[QualityType].ShaderCode;
}

void FShaderPlatformSettings::AllocateMaterialResources()
{
	ClearResources();

	const ERHIFeatureLevel::Type TargetFeatureLevel = GetMaxSupportedFeatureLevel(PlatformShaderID);

	for (int32 QualityLevelIndex = 0; QualityLevelIndex < EMaterialQualityLevel::Num; QualityLevelIndex++)
	{
		PlatformData[QualityLevelIndex].MaterialResourcesStats = new FMaterialResourceStats();
		PlatformData[QualityLevelIndex].MaterialResourcesStats->SetMaterial(Material, (EMaterialQualityLevel::Type)QualityLevelIndex, true, (ERHIFeatureLevel::Type)TargetFeatureLevel, MaterialInstance);
	}
}

void FShaderPlatformSettings::SetMaterial(UMaterial* InMaterial)
{
	// if this is a different material, clear away the old one's resources and compile new shaders
	if (Material != InMaterial)
	{
		Material = InMaterial;
		MaterialInstance = nullptr;

		AllocateMaterialResources();
	}
}

void FShaderPlatformSettings::SetMaterial(UMaterialInstance* InMaterialInstance)
{
	if (MaterialInstance != InMaterialInstance)
	{
		MaterialInstance = InMaterialInstance;
		Material = MaterialInstance->GetMaterial();

		AllocateMaterialResources();
	}
}

bool FShaderPlatformSettings::CheckShaders()
{
	bool bRetValue = false;

	if (Material != nullptr)
	{
		// check and triggers shader recompilation if needed
		for (int32 QualityLevelIndex = 0; QualityLevelIndex < EMaterialQualityLevel::Num; QualityLevelIndex++)
		{
			auto &Data = PlatformData[QualityLevelIndex];
			const bool bNeedsShaders = (bPresentInGrid && Data.bExtractStats) || Data.bExtractCode;
			if (Data.bNeedShaderRecompilation && bNeedsShaders)
			{
				Data.MaterialResourcesStats->CancelCompilation();

				Material->UpdateCachedExpressionData();

				if (MaterialInstance != nullptr)
				{
					MaterialInstance->UpdateCachedLayerParameters();
				}

				Data.MaterialResourcesStats->CacheShaders(PlatformShaderID);

				Data.bCompilingShaders = true;
				Data.bUpdateShaderCode = true;
				Data.bNeedShaderRecompilation = false;

				Data.ShaderStatsInfo.Reset();

				bRetValue = true;
			}
		}
	}

	return bRetValue;
}

bool FShaderPlatformSettings::Update()
{
	bool bRetValue = CheckShaders();

	// if a shader compilation has been requested check if completed and extract shader names needed by code viewer combo-box
	for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
	{
		auto& QualityItem = PlatformData[i];
		if (QualityItem.bCompilingShaders)
		{
			FMaterialResource* Resource = QualityItem.MaterialResourcesStats;

			// if compilation is complete extract the list of compiled shader names
			const bool bCompilationFinished = Resource->IsCompilationFinished();
			if (bCompilationFinished)
			{
				QualityItem.bCompilingShaders = false;
				QualityItem.bUpdateShaderCode = true;

				const FMaterialShaderMap* MaterialShaderMap = Resource->GetGameThreadShaderMap();
				if (MaterialShaderMap != nullptr)
				{
					TMap<FShaderId, TShaderRef<FShader>> ShaderMap;
					MaterialShaderMap->GetShaderList(ShaderMap);

					QualityItem.ArrShaderNames.Empty();
					for (const auto Entry : ShaderMap)
					{
						QualityItem.ArrShaderNames.Add(MakeShareable(new FName(Entry.Value.GetType()->GetFName())));
					}

					if (QualityItem.ArrShaderNames.Num() > 0)
					{
						QualityItem.ComboBoxSelectedName = *QualityItem.ArrShaderNames[0];
					}
				}

				FMaterialStatsUtils::ExtractMatertialStatsInfo(QualityItem.ShaderStatsInfo, Resource);

				bRetValue = true;
			}
		}
	}

	return bRetValue;
}

/*end FShaderPlatformSettings functions*/
/***********************************************************************************************************************/

/***********************************************************************************************************************/
/*begin FMaterialStats functions*/

FMaterialStats::~FMaterialStats()
{
	SaveSettings();
}

void FMaterialStats::Initialize(IMaterialEditor* InMaterialEditor)
{
	MaterialEditor = InMaterialEditor;

	StatsGrid = MakeShareable(new FMaterialStatsGrid(AsShared()));

	BuildShaderPlatformDB();

	LoadSettings();

	StatsGrid->BuildGrid();

	GridStatsWidget = SNew(SMaterialEditorStatsWidget)
		.MaterialStatsWPtr(SharedThis(this));

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions LogOptions;
	// Show Pages so that user is never allowed to clear log messages
	LogOptions.bShowPages = false;
	LogOptions.bShowFilters = false; //TODO - Provide custom filters? E.g. "Critical Errors" vs "Errors" needed for materials?
	LogOptions.bAllowClear = false;
	LogOptions.MaxPageCount = 1;
	OldStatsListing = MessageLogModule.CreateLogListing("MaterialEditorStats", LogOptions);
	OldStatsWidget = MessageLogModule.CreateLogListingWidget(OldStatsListing.ToSharedRef());

	auto ToolkitCommands = MaterialEditor->GetToolkitCommands();
	const FMaterialEditorCommands& Commands = FMaterialEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.TogglePlatformStats,
		FExecuteAction::CreateSP(this, &FMaterialStats::ToggleStats),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FMaterialStats::IsShowingStats));

	ToolkitCommands->MapAction(
		Commands.ToggleMaterialStats,
		FExecuteAction::CreateSP(this, &FMaterialStats::ToggleOldStats),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FMaterialStats::IsShowingOldStats));
}

void FMaterialStats::LoadSettings()
{
	Options = NewObject<UMaterialStatsOptions>();

	for (const auto PlatformEntry : ShaderPlatformStatsDB)
	{
		const EShaderPlatform PlatformID = PlatformEntry.Key;
		const bool bPresentInGrid = !!Options->bPlatformUsed[PlatformID];

		auto Platform = PlatformEntry.Value;
		Platform->SetPresentInGrid(bPresentInGrid);
	}

	for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
	{
		const bool bUsed = !!Options->bMaterialQualityUsed[i];
		bArrStatsQualitySelector[(EMaterialQualityLevel::Type)i] = bUsed;

		for (const auto PlatformEntry : ShaderPlatformStatsDB)
		{
			TSharedPtr<FShaderPlatformSettings> SomePlatform = PlatformEntry.Value;
			if (SomePlatform.IsValid())
			{
				SomePlatform->SetExtractStatsQualityLevel((EMaterialQualityLevel::Type)i, bUsed);
			}
		}
	}
}

void FMaterialStats::SaveSettings()
{
	for (const auto PlatformEntry : ShaderPlatformStatsDB)
	{
		const EShaderPlatform PlatformID = PlatformEntry.Key;
		const auto Platform = PlatformEntry.Value;

		const bool bPresentInGrid = Platform->IsPresentInGrid();
		Options->bPlatformUsed[PlatformID] = bPresentInGrid ? 1 : 0;
	}

	for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
	{
		const bool bQualityPresent = GetStatsQualityFlag((EMaterialQualityLevel::Type)i);

		Options->bMaterialQualityUsed[i] = bQualityPresent ? 1 : 0;
	}

	Options->SaveConfig();
}

void FMaterialStats::SetShowStats(const bool bValue)
{
	bShowStats = bValue;

	// open/close stats tab
	DisplayStatsGrid(bShowStats);

	GetGridStatsWidget()->RequestRefresh();
}

void FMaterialStats::SetShowOldStats(const bool bValue)
{
	bShowOldStats = bValue;

	// open/close stats tab
	DisplayOldStats(bShowOldStats);
}

void FMaterialStats::ToggleStats()
{
	// Toggle the showing of material stats each time the user presses the show stats button
	SetShowStats(!bShowStats);
}

void FMaterialStats::ToggleOldStats()
{
	// Toggle the showing of material stats each time the user presses the show stats button
	SetShowOldStats(!bShowOldStats);
}

void FMaterialStats::DisplayOldStats(const bool bShow)
{
	if (bShow)
	{
		MaterialEditor->GetTabManager()->InvokeTab(OldStatsTabId);
	}
	else if (!bShowOldStats && OldStatsTab.IsValid())
	{
		OldStatsTab.Pin()->RequestCloseTab();
	}
}

void FMaterialStats::DisplayStatsGrid(const bool bShow)
{
	if (bShow)
	{
		MaterialEditor->GetTabManager()->InvokeTab(StatsTabId);
	}
	else if (!bShowStats && StatsTab.IsValid())
	{
		StatsTab.Pin()->RequestCloseTab();
	}
}

void FMaterialStats::RefreshStatsGrid()
{
	GetGridStatsWidget()->RequestRefresh();
}

void FMaterialStats::BuildShaderPlatformDB()
{
#if PLATFORM_WINDOWS
	// DirectX
	AddShaderPlatform(EPlatformCategoryType::Desktop, SP_PCD3D_SM5, TEXT("DirectX SM5"), true, true, TEXT("Desktop, DirectX, Shader Model 5"));
#endif

	// Vulkan
	AddShaderPlatform(EPlatformCategoryType::Desktop, SP_VULKAN_SM5, TEXT("Vulkan SM5"), false, true, TEXT("Desktop, Vulkan, Shader Model 5"));

	// OpenGL
	AddShaderPlatform(EPlatformCategoryType::Desktop, SP_OPENGL_SM5, TEXT("OpenGL SM5"), false, true, TEXT("Desktop, OpenGL, Shader Model 5"));

	// Android
	AddShaderPlatform(EPlatformCategoryType::Android, SP_OPENGL_ES3_1_ANDROID, TEXT("Android GLES 3.1"), true, true, TEXT("Android, OpenGLES 3.1"));
	AddShaderPlatform(EPlatformCategoryType::Android, SP_VULKAN_ES3_1_ANDROID, TEXT("Android Vulkan"), true, true, TEXT("Android, Vulkan"));
	AddShaderPlatform(EPlatformCategoryType::Android, SP_VULKAN_SM5_ANDROID, TEXT("Android Vulkan SM5"), true, true, TEXT("Android, Vulkan SM5"));

	// Apple
	AddShaderPlatform(EPlatformCategoryType::Desktop, SP_METAL_SM5, TEXT("Metal SM5"), false, true, TEXT("macOS, Metal, Shader Model 5"));
	AddShaderPlatform(EPlatformCategoryType::IOS, SP_METAL, TEXT("Metal"), false, true, TEXT("iOS, Metal, Mobile"));
	AddShaderPlatform(EPlatformCategoryType::IOS, SP_METAL_MRT, TEXT("Metal MRT"), false, true, TEXT("iOS, Metal, Shader Model 5"));

	// Look to see what console platforms we have
	static const EShaderPlatform ConsolePlatforms[] =
	{
		SP_PS4,
		SP_XBOXONE_D3D12,
		SP_SWITCH,
		SP_SWITCH_FORWARD,
	};

	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (EShaderPlatform ShaderPlatform : ConsolePlatforms)
	{
		const FName ShaderFormat = LegacyShaderPlatformToShaderFormat(ShaderPlatform);
		if (TPM.FindShaderFormat(ShaderFormat) != nullptr )
		{
			FString PlatformName = FMaterialStatsUtils::ShaderPlatformTypeName(ShaderPlatform);
			AddShaderPlatform(EPlatformCategoryType::Console, ShaderPlatform, FName(*PlatformName), true, true, PlatformName);
		}
	}
}

TSharedPtr<FShaderPlatformSettings> FMaterialStats::AddShaderPlatform(const EPlatformCategoryType PlatformType, const EShaderPlatform PlatformID, const FName PlatformName,
	const bool bAllowPresenceInGrid, const bool bAllowCodeView, const FString& Description)
{
	TSharedPtr<FShaderPlatformSettings> PlatformPtr = MakeShareable(new FShaderPlatformSettings(PlatformType, PlatformID, PlatformName, bAllowPresenceInGrid, bAllowCodeView, Description));
	ShaderPlatformStatsDB.Add(PlatformID, PlatformPtr);

	auto& ArrayPlatforms = PlatformTypeDB.FindOrAdd(PlatformType);
	ArrayPlatforms.Add(PlatformPtr);

	for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
	{
		PlatformPtr->SetExtractStatsFlag((EMaterialQualityLevel::Type)i, bArrStatsQualitySelector[i]);
	}

	return PlatformPtr;
}

void FMaterialStats::SignalMaterialChanged()
{
	ExtractHLSLCode();

	for (auto Entry : ShaderPlatformStatsDB)
	{
		for (int32 i = 0; i < EMaterialQualityLevel::Num; ++i)
			Entry.Value->SetNeedShaderCompilation((EMaterialQualityLevel::Type)i, true);
	}
}

bool FMaterialStats::SwitchShaderPlatformUseStats(const EShaderPlatform PlatformID)
{
	bool bRetValue = false;

	auto* Item = ShaderPlatformStatsDB.Find(PlatformID);
	if (Item != nullptr)
	{
		bRetValue = (*Item)->FlipPresentInGrid();

		GetStatsGrid()->OnAddOrRemovePlatform(*Item);
		SaveSettings();
	}

	return bRetValue;
}

void FMaterialStats::SetStatusQualityFlag(const EMaterialQualityLevel::Type QualityLevel, const bool bValue)
{
	check(QualityLevel < EMaterialQualityLevel::Num);

	bArrStatsQualitySelector[QualityLevel] = bValue;

	for (const auto PlatformEntry : ShaderPlatformStatsDB)
	{
		TSharedPtr<FShaderPlatformSettings> SomePlatform = PlatformEntry.Value;
		if (SomePlatform.IsValid())
		{
			SomePlatform->SetExtractStatsQualityLevel(QualityLevel, bValue);
		}
	}

	SaveSettings();
}

bool FMaterialStats::SwitchStatsQualityFlag(EMaterialQualityLevel::Type Quality)
{
	check(Quality < EMaterialQualityLevel::Num);

	const bool bValue = !bArrStatsQualitySelector[Quality];
	SetStatusQualityFlag(Quality, bValue);

	return bValue;
}

void FMaterialStats::SetShaderPlatformUseCodeView(const EShaderPlatform PlatformID, const EMaterialQualityLevel::Type Quality, const bool bValue)
{
	auto* Item = ShaderPlatformStatsDB.Find(PlatformID);
	if (Item != nullptr)
	{
		(*Item)->SetCodeViewNeeded(Quality, bValue);
	}
}

FName FMaterialStats::GetPlatformName(const EShaderPlatform InEnumValue) const
{
	FName PlatformName = NAME_None;

	auto* Entry = ShaderPlatformStatsDB.Find(InEnumValue);
	if (Entry != nullptr && Entry->IsValid())
	{
		PlatformName = (*Entry)->GetPlatformName();
	}

	return PlatformName;
}

EShaderPlatform FMaterialStats::GetShaderPlatformID(const FName InName) const
{
	for (auto Entry : ShaderPlatformStatsDB)
	{
		if (Entry.Value.Get()->GetPlatformName() == InName)
		{
			return Entry.Key;
		}
	}

	return SP_NumPlatforms;
}

TSharedPtr<FShaderPlatformSettings> FMaterialStats::GetPlatformSettings(const EShaderPlatform PlatformID)
{
	auto* Entry = ShaderPlatformStatsDB.Find(PlatformID);
	if (Entry == nullptr)
	{
		return TSharedPtr<FShaderPlatformSettings>(nullptr);
	}

	return *Entry;
}

TSharedPtr<FShaderPlatformSettings> FMaterialStats::GetPlatformSettings(const FName PlatformName)
{
	const EShaderPlatform PlatformID = GetShaderPlatformID(PlatformName);

	return GetPlatformSettings(PlatformID);
}

FText FMaterialStats::GetShaderCode(const EShaderPlatform PlatformID, const EMaterialQualityLevel::Type QualityType)
{
	auto* Entry = ShaderPlatformStatsDB.Find(PlatformID);
	if (Entry == nullptr)
	{
		return FText::FromString(TEXT("Shader code compiling or not available!"));
	}

	return (*Entry)->GetShaderCode(QualityType);
}

void FMaterialStats::Update()
{
	const bool bNeedsUpdate = IsShowingStats() || IsCodeViewWindowActive();
	if (!bNeedsUpdate)
	{
		return;
	}

	bool bInfoChanged = false;

	for (const auto Entry : ShaderPlatformStatsDB)
	{
		auto PlatformStats = Entry.Value;
		bInfoChanged |= PlatformStats->Update();
	}

	if (bInfoChanged)
	{
		GetStatsGrid()->OnShaderChanged();
	}

	ComputeGridWarnings();
}

TSharedRef<class SDockTab> FMaterialStats::SpawnTab_HLSLCode(const class FSpawnTabArgs& Args)
{
	auto CodeViewUtility =
		SNew(SVerticalBox)
		// Copy Button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.Text(LOCTEXT("CopyHLSLButton", "Copy"))
				.ToolTipText(LOCTEXT("CopyHLSLButtonToolTip", "Copies all HLSL code to the clipboard."))
				.ContentPadding(3)
				.OnClicked_Lambda
				(
					[Code = &HLSLCode]()
					{
						FPlatformApplicationMisc::ClipboardCopy(**Code);
						return FReply::Handled();
					}
				)
			]
		]
		// Separator
		+ SVerticalBox::Slot()
		.FillHeight(1)
		[
			SNew(SSeparator)
		];

	auto CodeView =
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(5)
		[
			SNew(STextBlock)
			.Text_Lambda([Code = &HLSLCode]() { return FText::FromString(*Code); })
		];


	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("HLSLCodeTitle", "HLSL Code"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CodeViewUtility
			]
			+ SVerticalBox::Slot()
			.FillHeight(1)
			[
				CodeView
			]
		];

	HLSLTab = SpawnedTab;

	ExtractHLSLCode();

	return SpawnedTab;
}

TSharedRef<SDockTab> FMaterialStats::SpawnTab_ShaderCode(const FSpawnTabArgs& Args, const EShaderPlatform PlatformID, const EMaterialQualityLevel::Type QualityLevel)
{
	SetShaderPlatformUseCodeView(PlatformID, QualityLevel, true);

	const FString PlatformName = GetPlatformName(PlatformID).ToString();
	const FString FullPlatformName = PlatformName + TEXT(" -- ") + FMaterialStatsUtils::MaterialQualityToString(QualityLevel);

	TSharedPtr<FShaderPlatformSettings> PlatformPtr = GetPlatformSettings(PlatformID);
	check(PlatformPtr.IsValid());

	TSharedRef<SComboBox<TSharedPtr<FName>>> ShaderBox = SNew(SComboBox<TSharedPtr<FName>>)
		.OptionsSource(PlatformPtr->GetShaderNames(QualityLevel))
		.OnGenerateWidget_Lambda([](TSharedPtr<FName> Value) { return SNew(STextBlock).Text(FText::FromName(*Value.Get())); })
		.OnSelectionChanged_Lambda([PlatformPtr, QualityLevel](TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo) { PlatformPtr->OnShaderViewComboSelectionChanged(Item, QualityLevel); })
		[
			SNew(STextBlock)
			.Text_Lambda([PlatformPtr, QualityLevel]() { return PlatformPtr->GetSelectedShaderViewComboText(QualityLevel); })
		];

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				// Copy Button
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.0f, 0.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SButton)
						.Text(LOCTEXT("CopyShaderCodeButton", "Copy"))
						.ToolTipText(LOCTEXT("CopyShaderCodeButtonToolTip", "Copies all shader code to the clipboard."))
						.ContentPadding(3)
						.OnClicked_Lambda
						(
							[MaterialStats = TWeakPtr<FMaterialStats>(SharedThis(this)), PlatformID, QualityLevel]()
							{
								auto StatsPtr = MaterialStats.Pin();
								if (StatsPtr.IsValid())
								{
									FText ShaderCode = StatsPtr->GetShaderCode(PlatformID, QualityLevel);
									FPlatformApplicationMisc::ClipboardCopy(*ShaderCode.ToString());
									return FReply::Handled();
								}

								return FReply::Unhandled();
							}
						)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.0f, 2.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FullPlatformName))
					]
				]
				// Separator
				+ SVerticalBox::Slot()
				.FillHeight(1)
				[
					SNew(SSeparator)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ShaderBox
			]
			+ SVerticalBox::Slot()
			.FillHeight(1)
			[
				PlatformPtr->GetShaderViewerScrollBox(QualityLevel).ToSharedRef()
			]
		];

	PlatformPtr->SetCodeViewerTab(QualityLevel, SpawnedTab);

	SpawnedTab->SetLabel(FText::FromString(PlatformName));

	return SpawnedTab;
}

FName FMaterialStats::MakeTabName(const EPlatformCategoryType PlatformType, const EShaderPlatform ShaderPlatformType, const EMaterialQualityLevel::Type QualityLevel)
{
	const FString TabName = FMaterialStatsUtils::GetPlatformTypeName(PlatformType) + FMaterialStatsUtils::ShaderPlatformTypeName(ShaderPlatformType) + FMaterialStatsUtils::MaterialQualityToString(QualityLevel);

	return FName(*TabName);
}

void FMaterialStats::BuildViewShaderCodeMenus()
{
	auto TabManager = MaterialEditor->GetTabManager();
	auto ParentCategoryRef = MaterialEditor->GetWorkspaceMenuCategory();

	TSharedPtr<FWorkspaceItem> PlatformGroupMenuItem = ParentCategoryRef->AddGroup(LOCTEXT("ViewShaderCodePlatformsGroupMenu", "Shader Code"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.Tabs.HLSLCode"));

	// add hlsl code viewer tab
	TabManager->RegisterTabSpawner( HLSLCodeTabId, FOnSpawnTab::CreateSP(this, &FMaterialStats::SpawnTab_HLSLCode))
		.SetDisplayName( LOCTEXT("HLSLCodeTab", "HLSL Code") )
		.SetGroup( PlatformGroupMenuItem.ToSharedRef() )
		.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.Tabs.HLSLCode") );

	for (auto MapEntry : PlatformTypeDB)
	{
		const EPlatformCategoryType PlatformType = MapEntry.Key;

		const FString PlatformName = FMaterialStatsUtils::GetPlatformTypeName(PlatformType);
		TSharedPtr<FWorkspaceItem> PlatformMenuItem = PlatformGroupMenuItem->AddGroup(FText::FromString(PlatformName), FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.Tabs.HLSLCode"));

		TArray<TSharedPtr<FShaderPlatformSettings>>& ArrShaderPlatforms = MapEntry.Value;

		for (int32 i = 0; i < ArrShaderPlatforms.Num(); ++i)
		{
			TSharedPtr<FShaderPlatformSettings> PlatformPtr = ArrShaderPlatforms[i];

			if (!PlatformPtr.IsValid())
			{
				continue;
			}

			const EShaderPlatform PlatformID = PlatformPtr->GetPlatformShaderType();

			if (PlatformID == SP_NumPlatforms || !PlatformPtr->IsCodeViewAllowed())
			{
				continue;
			}

			const FString ShaderPlatformName = PlatformPtr->GetPlatformName().ToString();
			TSharedPtr<FWorkspaceItem> ShaderPlatformMenuItem = PlatformMenuItem->AddGroup(FText::FromString(ShaderPlatformName), FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.Tabs.HLSLCode"));

			for (int32 q = 0; q < EMaterialQualityLevel::Num; ++q)
			{
				const EMaterialQualityLevel::Type QualityLevel = (EMaterialQualityLevel::Type)q;

				const FString MaterialQualityName = FMaterialStatsUtils::MaterialQualityToString(QualityLevel);
				const FName TabName = MakeTabName(PlatformType, PlatformID, QualityLevel);

				TabManager->RegisterTabSpawner(TabName, FOnSpawnTab::CreateSP<FMaterialStats, EShaderPlatform, EMaterialQualityLevel::Type>(this, &FMaterialStats::SpawnTab_ShaderCode, PlatformID, QualityLevel))
					.SetGroup(ShaderPlatformMenuItem.ToSharedRef())
					.SetDisplayName(FText::FromString(MaterialQualityName));

				auto CodeScrollBox = SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(5)
					[
						SNew(STextBlock)
						.Text_Lambda([MaterialStats = TWeakPtr<FMaterialStats>(SharedThis(this)), PlatformID, QualityLevel]()
						{
							auto StatsPtr = MaterialStats.Pin();
							if (StatsPtr.IsValid())
							{
								return StatsPtr->GetShaderCode(PlatformID, QualityLevel);
							}

							return FText::FromString(TEXT("Error reading shader code!"));
						})
					];

				PlatformPtr->GetPlatformData(QualityLevel).CodeScrollBox = CodeScrollBox;
			}
		}
	}
}

bool FMaterialStats::IsCodeViewWindowActive() const
{
	for (const auto MapEntry : ShaderPlatformStatsDB)
	{
		TSharedPtr<FShaderPlatformSettings> PlatformPtr = MapEntry.Value;

		for (int q = 0; q < EMaterialQualityLevel::Num; ++q)
		{
			if (PlatformPtr->GetCodeViewerTab((EMaterialQualityLevel::Type)q).IsValid())
			{
				return true;
			}
		}
	}

	return false;
}

TSharedRef<SDockTab> FMaterialStats::SpawnTab_Stats(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == StatsTabId);

	FString TabName = FString::Printf(TEXT(""), *GetMaterialName().ToString());

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Kismet.Tabs.CompilerResults"))
		.Label(LOCTEXT("Platform Stats", "Platform Stats"))
		.OnTabClosed_Lambda([&bShowStats = bShowStats](TSharedRef<SDockTab>) { bShowStats = false; })
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("MaterialStats")))
			[
				GetGridStatsWidget().ToSharedRef()
			]
		];

	StatsTab = SpawnedTab;

	// like so because the material editor will automatically restore this tab if it was still opened when the editor shuts down
	bShowStats = true;

	return SpawnedTab;
}

TSharedRef<SDockTab> FMaterialStats::SpawnTab_OldStats(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == OldStatsTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Kismet.Tabs.CompilerResults"))
		.Label(LOCTEXT("Stats", "Stats"))
		.OnTabClosed_Lambda([&bShowStats = bShowOldStats](TSharedRef<SDockTab>) { bShowStats = false; })
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("MaterialStats")))
			[
				OldStatsWidget.ToSharedRef()
			]
		];

	OldStatsTab = SpawnedTab;

	// like so because the material editor will automatically restore this tab if it was still opened when the editor shuts down
	bShowOldStats = true;

	return SpawnedTab;
}

void FMaterialStats::BuildStatsTab()
{
	const auto ParentCategoryRef = MaterialEditor->GetWorkspaceMenuCategory();
	auto TabManager = MaterialEditor->GetTabManager();

	TabManager->RegisterTabSpawner(StatsTabId, FOnSpawnTab::CreateSP(this, &FMaterialStats::SpawnTab_Stats))
		.SetDisplayName(LOCTEXT("StatsTab", "Platform Stats"))
		.SetGroup(ParentCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
}

void FMaterialStats::BuildOldStatsTab()
{
	const auto ParentCategoryRef = MaterialEditor->GetWorkspaceMenuCategory();
	auto TabManager = MaterialEditor->GetTabManager();

	TabManager->RegisterTabSpawner(OldStatsTabId, FOnSpawnTab::CreateSP(this, &FMaterialStats::SpawnTab_OldStats))
		.SetDisplayName(LOCTEXT("OldStatsTab", "Stats"))
		.SetGroup(ParentCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
}

void FMaterialStats::RegisterTabs()
{
	BuildStatsTab();
	BuildOldStatsTab();
	BuildViewShaderCodeMenus();
}

void FMaterialStats::UnregisterTabs()
{
	auto TabManager = MaterialEditor->GetTabManager();

	for (auto MapEntry : PlatformTypeDB)
	{
		const EPlatformCategoryType PlatformType = MapEntry.Key;
		TArray<TSharedPtr<FShaderPlatformSettings>>& ArrShaderPlatforms = MapEntry.Value;

		for (int32 i = 0; i < ArrShaderPlatforms.Num(); ++i)
		{
			TSharedPtr<FShaderPlatformSettings> PlatformPtr = ArrShaderPlatforms[i];

			if (!PlatformPtr.IsValid())
			{
				continue;
			}

			const EShaderPlatform ShaderPlatformID = PlatformPtr->GetPlatformShaderType();

			for (int32 q = 0; q < EMaterialQualityLevel::Num; ++q)
			{
				const FName TabName = MakeTabName(PlatformType, ShaderPlatformID, (EMaterialQualityLevel::Type)q);

				TabManager->UnregisterTabSpawner(TabName);
			}
		}
	}

	TabManager->UnregisterTabSpawner(StatsTabId);
	TabManager->UnregisterTabSpawner(OldStatsTabId);
	TabManager->UnregisterTabSpawner(HLSLCodeTabId);
}

void FMaterialStats::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Options);
}

void FMaterialStats::ComputeGridWarnings()
{
	auto GridPtr = GetGridStatsWidget();
	if (!GridPtr.IsValid())
	{
		return;
	}

	int32 Warnings = 0;
	TArray<EShaderPlatform> CompilerWarnings;
	TArray<FString> WarningMessages;

	bool bAnyQualityPresent = false;
	for (int32 q = 0; q < EMaterialQualityLevel::Num; ++q)
	{
		auto Quality = static_cast<EMaterialQualityLevel::Type>(q);

		bAnyQualityPresent |= GetStatsQualityFlag(Quality);
	}

	if (!bAnyQualityPresent)
	{
		Warnings |= WarningNoQuality;
		WarningMessages.Emplace("No material quality selected. Please use the 'Settings' button and choose the desired quality level to be analyzed.");
	}

	bool bAnyPlatformPresent = false;
	bool bPlatformsNeedOfflineCompiler = false;

	const auto& PlatformList = GetPlatformsDB();
	for (auto Pair : PlatformList)
	{
		auto& PlatformPtr = Pair.Value;

		if (PlatformPtr->IsPresentInGrid())
		{
			bAnyPlatformPresent = true;

			auto ShaderPlatformType = PlatformPtr->GetPlatformShaderType();
			bool bNeedsOfflineCompiler = FMaterialStatsUtils::PlatformNeedsOfflineCompiler(ShaderPlatformType);
			if (bNeedsOfflineCompiler)
			{
				bool bCompilerAvailable = FMaterialStatsUtils::IsPlatformOfflineCompilerAvailable(ShaderPlatformType);

				if (!bCompilerAvailable)
				{
					CompilerWarnings.Add(ShaderPlatformType);
					auto WarningString = FString::Printf(TEXT("Platform %s needs an offline shader compiler to extract instruction count. Please check 'Editor Preferences' -> 'Content Editors' -> 'Material Editor' for additional settings."), *PlatformPtr->GetPlatformName().ToString());
					WarningMessages.Add(MoveTemp(WarningString));
				}
			}
		}
	}

	if (!bAnyPlatformPresent)
	{
		Warnings |= WarningNoPlatform;
		WarningMessages.Emplace("No platform selected. Please use the 'Settings' button and choose desired platform to be analyzed.");
	}

	bool bRefreshWarnings = (Warnings != LastGenericWarning) || (CompilerWarnings.Num() != LastMissingCompilerWarnings.Num());
	if (!bRefreshWarnings)
	{
		for (int32 i = 0; i < CompilerWarnings.Num(); ++i)
		{
			if (CompilerWarnings[i] != LastMissingCompilerWarnings[i])
			{
				bRefreshWarnings = true;
				break;
			}
		}
	}

	if (bRefreshWarnings)
	{
		LastGenericWarning = Warnings;
		LastMissingCompilerWarnings = CompilerWarnings;

		GridPtr->ClearWarningMessages();

		for (int32 i = 0; i < WarningMessages.Num(); ++i)
		{
			GridPtr->AddWarningMessage(WarningMessages[i]);
		}
	}
}

void FMaterialStats::ExtractHLSLCode()
{
#define MARKTAG TEXT("/*MARK_")
#define MARKTAGLEN 7

	HLSLCode = TEXT("");

 	if (!HLSLTab.IsValid())
	{
		return;
	}

	FString MarkupSource;
	if (MaterialInterface != nullptr && MaterialInterface->GetMaterialResource(GMaxRHIFeatureLevel)->GetMaterialExpressionSource(MarkupSource))
	{
		// Remove line-feeds and leave just CRs so the character counts match the selection ranges.
		MarkupSource.ReplaceInline(TEXT("\r"), TEXT(""));

		// Improve formatting: Convert tab to 4 spaces since STextBlock (currently) doesn't show tab characters
		MarkupSource.ReplaceInline(TEXT("\t"), TEXT("    "));

		// Extract highlight ranges from markup tags

		// Make a copy so we can insert null terminators.
		TCHAR* MarkupSourceCopy = new TCHAR[MarkupSource.Len() + 1];
		FCString::Strcpy(MarkupSourceCopy, MarkupSource.Len() + 1, *MarkupSource);

		TCHAR* Ptr = MarkupSourceCopy;
		while (Ptr && *Ptr != '\0')
		{
			TCHAR* NextTag = FCString::Strstr(Ptr, MARKTAG);
			if (!NextTag)
			{
				// No more tags, so we're done!
				HLSLCode += Ptr;
				break;
			}

			// Copy the text up to the tag.
			*NextTag = '\0';
			HLSLCode += Ptr;

			// Advance past the markup tag to see what type it is (beginning or end)
			NextTag += MARKTAGLEN;
			int32 TagNumber = FCString::Atoi(NextTag + 1);
			Ptr = FCString::Strstr(NextTag, TEXT("*/")) + 2;
		}

		delete[] MarkupSourceCopy;
	}
}

#undef LOCTEXT_NAMESPACE

/*end FMaterialStats functions*/
/***********************************************************************************************************************/
