// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/DisplayClusterConfigManager.h"

#include "Cluster/IPDisplayClusterClusterManager.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Config/Parser/DisplayClusterConfigParserText.h"
#include "Config/Parser/DisplayClusterConfigParserXml.h"
#include "Config/Parser/DisplayClusterConfigParserDebugAuto.h"

#include "DisplayClusterBuildConfig.h"
#include "Misc/Paths.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"

#include "HAL/FileManager.h"


FDisplayClusterConfigManager::FDisplayClusterConfigManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
}

FDisplayClusterConfigManager::~FDisplayClusterConfigManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterConfigManager::Init(EDisplayClusterOperationMode OperationMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	
	return true;
}

void FDisplayClusterConfigManager::Release()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
}

bool FDisplayClusterConfigManager::StartSession(const FString& configPath, const FString& nodeId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	
	ConfigPath = configPath;
	ClusterNodeId = nodeId;

	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Starting session with config: %s"), *ConfigPath);

	// Load data
	return LoadConfig(ConfigPath);
}

void FDisplayClusterConfigManager::EndSession()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);

	ConfigPath.Empty();
	ClusterNodeId.Empty();

	ResetConfigData();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterConfigManager
//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster nodes
TArray<FDisplayClusterConfigClusterNode> FDisplayClusterConfigManager::GetClusterNodes() const
{
	return CfgClusterNodes;
}

int32 FDisplayClusterConfigManager::GetClusterNodesAmount() const
{
	return CfgClusterNodes.Num();
}

bool FDisplayClusterConfigManager::GetClusterNode(const FString& id, FDisplayClusterConfigClusterNode& node) const
{
	return GetItem(CfgClusterNodes, id, node, FString("GetNode"));
}

bool FDisplayClusterConfigManager::GetMasterClusterNode(FDisplayClusterConfigClusterNode& node) const
{
	const FDisplayClusterConfigClusterNode* const pFound = CfgClusterNodes.FindByPredicate([](const FDisplayClusterConfigClusterNode& item)
	{
		return item.IsMaster == true;
	});

	if (!pFound)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("Master node configuration not found"));
		return false;
	}

	node = *pFound;
	return true;
}

// Windows
int32 FDisplayClusterConfigManager::GetWindowsAmount() const
{
	return CfgWindows.Num();
}

TArray<FDisplayClusterConfigWindow> FDisplayClusterConfigManager::GetWindows() const
{
	return CfgWindows;
}

bool FDisplayClusterConfigManager::GetWindow(const FString& ID, FDisplayClusterConfigWindow& Window) const
{
	return GetItem(CfgWindows, ID, Window, FString("GetWindow"));
}

bool FDisplayClusterConfigManager::GetMasterWindow(FDisplayClusterConfigWindow& Window) const
{
	if (!GDisplayCluster)
	{
		return false;
	}

	IPDisplayClusterConfigManager* ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
	if (!ConfigMgr)
	{
		return false;
	}

	FDisplayClusterConfigClusterNode MasterClusterNode;
	if (!ConfigMgr->GetMasterClusterNode(MasterClusterNode))
	{
		return false;
	}

	return ConfigMgr->GetWindow(MasterClusterNode.Id, Window);
}

// Screens
TArray<FDisplayClusterConfigScreen> FDisplayClusterConfigManager::GetScreens() const
{
	return CfgScreens;
}

int32 FDisplayClusterConfigManager::GetScreensAmount() const
{
	return CfgScreens.Num();
}

bool FDisplayClusterConfigManager::GetScreen(const FString& id, FDisplayClusterConfigScreen& screen) const
{
	return GetItem(CfgScreens, id, screen, FString("GetScreen"));
}


// Cameras
TArray<FDisplayClusterConfigCamera> FDisplayClusterConfigManager::GetCameras() const
{
	return CfgCameras;
}

int32 FDisplayClusterConfigManager::GetCamerasAmount() const
{
	return CfgCameras.Num();
}

bool FDisplayClusterConfigManager::GetCamera(const FString& id, FDisplayClusterConfigCamera& camera) const
{
	return GetItem(CfgCameras, id, camera, FString("GetCamera"));
}


// Viewports
TArray<FDisplayClusterConfigViewport> FDisplayClusterConfigManager::GetViewports() const
{
	return CfgViewports;
}

int32 FDisplayClusterConfigManager::GetViewportsAmount() const
{
	return CfgViewports.Num();
}

bool FDisplayClusterConfigManager::GetViewport(const FString& id, FDisplayClusterConfigViewport& viewport) const
{
	return GetItem(CfgViewports, id, viewport, FString("GetViewport"));
}


// Postprocess
TArray<FDisplayClusterConfigPostprocess> FDisplayClusterConfigManager::GetPostprocess() const
{
	return CfgPostprocess;
}

int32 FDisplayClusterConfigManager::GetPostprocessAmount() const
{
	return CfgPostprocess.Num();
}

bool FDisplayClusterConfigManager::GetPostprocess(const FString& id, FDisplayClusterConfigPostprocess& postprocess) const
{
	return GetItem(CfgPostprocess, id, postprocess, FString("GetPostprocess"));
}


// Scene nodes
TArray<FDisplayClusterConfigSceneNode> FDisplayClusterConfigManager::GetSceneNodes() const
{
	return CfgSceneNodes;
}

int32 FDisplayClusterConfigManager::GetSceneNodesAmount() const
{
	return CfgSceneNodes.Num();
}

bool FDisplayClusterConfigManager::GetSceneNode(const FString& id, FDisplayClusterConfigSceneNode& actor) const
{
	return GetItem(CfgSceneNodes, id, actor, FString("GetActor"));
}


// Input devices
TArray<FDisplayClusterConfigInput> FDisplayClusterConfigManager::GetInputDevices() const
{
	return CfgInputDevices;
}

int32 FDisplayClusterConfigManager::GetInputDevicesAmount() const
{
	return CfgInputDevices.Num();
}

bool FDisplayClusterConfigManager::GetInputDevice(const FString& id, FDisplayClusterConfigInput& input) const
{
	return GetItem(CfgInputDevices, id, input, FString("GetInputDevice"));
}

TArray<FDisplayClusterConfigInputSetup> FDisplayClusterConfigManager::GetInputSetupRecords() const
{
	return CfgInputSetupRecords;
}

bool FDisplayClusterConfigManager::GetInputSetupRecord(const FString& id, FDisplayClusterConfigInputSetup& input) const
{
	return GetItem(CfgInputSetupRecords, id, input, FString("GetInputSetupRecord"));
}

TArray<FDisplayClusterConfigProjection> FDisplayClusterConfigManager::GetProjections() const
{
	return CfgProjections;
}

bool FDisplayClusterConfigManager::GetProjection(const FString& id, FDisplayClusterConfigProjection& projection) const
{
	return GetItem(CfgProjections, id, projection, FString("GetProjection"));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterConfigParserListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfigManager::AddInfo(const FDisplayClusterConfigInfo& InCfgInfo)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found info node: %s"), *InCfgInfo.ToString());
	CfgInfo = InCfgInfo;
}

void FDisplayClusterConfigManager::AddClusterNode(const FDisplayClusterConfigClusterNode& InCfgCNode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found cluster node: %s"), *InCfgCNode.ToString());
	CfgClusterNodes.Add(InCfgCNode);
}

void FDisplayClusterConfigManager::AddWindow(const FDisplayClusterConfigWindow& InCfgWindow)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found window: %s"), *InCfgWindow.ToString());
	CfgWindows.Add(InCfgWindow);
}

void FDisplayClusterConfigManager::AddScreen(const FDisplayClusterConfigScreen& InCfgScreen)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found screen: %s"), *InCfgScreen.ToString());
	CfgScreens.Add(InCfgScreen);
}

void FDisplayClusterConfigManager::AddViewport(const FDisplayClusterConfigViewport& InCfgViewport)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found viewport: %s"), *InCfgViewport.ToString());
	CfgViewports.Add(InCfgViewport);
}

void FDisplayClusterConfigManager::AddPostprocess(const FDisplayClusterConfigPostprocess& InCfgPostprocess)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found postprocess: %s"), *InCfgPostprocess.ToString());
	CfgPostprocess.Add(InCfgPostprocess);
}

void FDisplayClusterConfigManager::AddCamera(const FDisplayClusterConfigCamera& InCfgCamera)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found camera: %s"), *InCfgCamera.ToString());
	CfgCameras.Add(InCfgCamera);
}

void FDisplayClusterConfigManager::AddSceneNode(const FDisplayClusterConfigSceneNode& InCfgSNode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found scene node: %s"), *InCfgSNode.ToString());
	CfgSceneNodes.Add(InCfgSNode);
}

void FDisplayClusterConfigManager::AddInput(const FDisplayClusterConfigInput& InCfgInput)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found input device: %s"), *InCfgInput.ToString());
	CfgInputDevices.Add(InCfgInput);
}

void FDisplayClusterConfigManager::AddInputSetup(const FDisplayClusterConfigInputSetup& InCfgInputSetup)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found input setup record: %s"), *InCfgInputSetup.ToString());
	CfgInputSetupRecords.Add(InCfgInputSetup);
}

void FDisplayClusterConfigManager::AddGeneral(const FDisplayClusterConfigGeneral& InCfgGeneral)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found general: %s"), *InCfgGeneral.ToString());
	CfgGeneral = InCfgGeneral;
}

void FDisplayClusterConfigManager::AddRender(const FDisplayClusterConfigRender& InCfgRender)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found render: %s"), *InCfgRender.ToString());
	CfgRender = InCfgRender;
}

void FDisplayClusterConfigManager::AddNvidia(const FDisplayClusterConfigNvidia& InCfgNvidia)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found NVIDIA: %s"), *InCfgNvidia.ToString());
	CfgNvidia = InCfgNvidia;
}

void FDisplayClusterConfigManager::AddStereo(const FDisplayClusterConfigStereo& InCfgStereo)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found stereo: %s"), *InCfgStereo.ToString());
	CfgStereo = InCfgStereo;
}

void FDisplayClusterConfigManager::AddNetwork(const FDisplayClusterConfigNetwork& InCfgNetwork)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found network: %s"), *InCfgNetwork.ToString());
	CfgNetwork = InCfgNetwork;
}

void FDisplayClusterConfigManager::AddDebug(const FDisplayClusterConfigDebug& InCfgDebug)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found debug: %s"), *InCfgDebug.ToString());
	CfgDebug = InCfgDebug;
}

void FDisplayClusterConfigManager::AddCustom(const FDisplayClusterConfigCustom& InCfgCustom)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found custom: %s"), *InCfgCustom.ToString());
	CfgCustom = InCfgCustom;
}

void FDisplayClusterConfigManager::AddProjection(const FDisplayClusterConfigProjection& InCfgProjection)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found projeciton: %s"), *InCfgProjection.ToString());
	CfgProjections.Add(InCfgProjection);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigManager
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterConfigManager::EConfigFileType FDisplayClusterConfigManager::GetConfigFileType(const FString& cfgPath) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	if (cfgPath == DisplayClusterStrings::misc::DbgStubConfig)
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("Debug auto config requested"));
		return EConfigFileType::DebugAuto;
	}
#endif

	const FString ext = FPaths::GetExtension(cfgPath).ToLower();
	if (ext == FString(DisplayClusterStrings::cfg::file::FileExtXml).ToLower())
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("XML config: %s"), *cfgPath);
		return EConfigFileType::Xml;
	}
	else if (
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg1).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg2).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg3).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtTxt).ToLower())
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("TXT config: %s"), *cfgPath);
		return EConfigFileType::Text;
	}

	UE_LOG(LogDisplayClusterConfig, Warning, TEXT("Unknown file extension: %s"), *ext);
	return EConfigFileType::Unknown;
}

bool FDisplayClusterConfigManager::LoadConfig(const FString& cfgPath)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);

	FString ConfigFile = cfgPath;
	ConfigFile.TrimStartAndEndInline();

	if (FPaths::IsRelative(ConfigFile))
	{
		const FString ProjectDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
		ConfigFile = FPaths::ConvertRelativePathToFull(ProjectDir, ConfigFile);
	}

	// Actually the data is reset on EndFrame. This one is a safety call.
	ResetConfigData();

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	if (ConfigFile.Compare(FString(DisplayClusterStrings::misc::DbgStubConfig), ESearchCase::IgnoreCase) != 0 &&
		FPaths::FileExists(ConfigFile) == false)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("File not found: %s"), *ConfigFile);
		return false;
	}
#else
	if (FPaths::FileExists(ConfigFile) == false)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("File not found: %s"), *ConfigFile);
		return false;
	}
#endif

	// Instantiate appropriate parser
	TUniquePtr<FDisplayClusterConfigParser> parser;
	switch (GetConfigFileType(ConfigFile))
	{
	case EConfigFileType::Text:
		parser.Reset(new FDisplayClusterConfigParserText(this));
		break;

	case EConfigFileType::Xml:
		parser.Reset(new FDisplayClusterConfigParserXml(this));
		break;

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	case EConfigFileType::DebugAuto:
		bIsDebugAuto = true;
		parser.Reset(new FDisplayClusterConfigParserDebugAuto(this));
		break;
#endif

	default:
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("Unknown config type"));
		return false;
	}

	return parser->ParseFile(ConfigFile);
}

void FDisplayClusterConfigManager::ResetConfigData()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterConfig);

	CfgClusterNodes.Reset();
	CfgWindows.Reset();
	CfgScreens.Reset();
	CfgViewports.Reset();
	CfgPostprocess.Reset();
	CfgCameras.Reset();
	CfgSceneNodes.Reset();
	CfgInputDevices.Reset();
	CfgInputSetupRecords.Reset();
	CfgProjections.Reset();

	CfgInfo    = FDisplayClusterConfigInfo();
	CfgGeneral = FDisplayClusterConfigGeneral();
	CfgStereo  = FDisplayClusterConfigStereo();
	CfgRender  = FDisplayClusterConfigRender();
	CfgNvidia  = FDisplayClusterConfigNvidia();
	CfgNetwork = FDisplayClusterConfigNetwork();
	CfgDebug   = FDisplayClusterConfigDebug();
	CfgCustom  = FDisplayClusterConfigCustom();
}

template <typename DataType>
bool FDisplayClusterConfigManager::GetItem(const TArray<DataType>& container, const FString& id, DataType& item, const FString& logHeader) const
{
	auto pFound = container.FindByPredicate([id](const DataType& _item)
	{
		return _item.Id == id;
	});

	if (!pFound)
	{
		UE_LOG(LogDisplayClusterConfig, Warning, TEXT("%s: ID not found <%s>"), *logHeader, *id);
		return false;
	}

	item = *pFound;
	return true;
}

FString FDisplayClusterConfigManager::GetFullPathToFile(const FString& FileName) const
{
	if (!FPaths::FileExists(FileName))
	{
		TArray<FString> OrderedBaseDirs;

		//Add ordered search base dirs
		OrderedBaseDirs.Add(FPaths::GetPath(ConfigPath));
		OrderedBaseDirs.Add(FPaths::RootDir());

		// Process base dirs in order:
		for (auto It : OrderedBaseDirs)
		{
			FString FullPath = FPaths::ConvertRelativePathToFull(It, FileName);
			if (FPaths::FileExists(FullPath))
			{
				return FullPath;
			}
		}

		//@Handle error, file not found
		UE_LOG(LogDisplayClusterConfig, Warning, TEXT("File '%s' not found. In case of relative path do not forget to put './' at the beginning"), *FileName);
	}

	return FileName;
}

FString FDisplayClusterConfigManager::GetFullPathToNewFile(const FString& FileName) const
{
	TArray<FString> OrderedBaseDirs;

	//Add ordered search base dirs
	OrderedBaseDirs.Add(FPaths::GetPath(ConfigPath));
	OrderedBaseDirs.Add(FPaths::RootDir());

	// Process base dirs in order:
	for (auto It : OrderedBaseDirs)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(It, FileName);
		
		if (FPaths::DirectoryExists(FPaths::GetPath(FullPath)))
		{
			return FullPath;
		}
	}

	return FileName;
}