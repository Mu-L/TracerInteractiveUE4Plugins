// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/Text/DisplayClusterConfigurationTextParser.h"
#include "Formats/Text/DisplayClusterConfigurationTextStrings.h"
#include "Formats/Text/DisplayClusterConfigurationTextTypes.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationStrings.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterConfigurationLog.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


UDisplayClusterConfigurationData* FDisplayClusterConfigurationTextParser::LoadData(const FString& FilePath, UObject* Owner)
{
	ConfigDataOwner = Owner;

	// Parse the file first
	if (!ParseTextFile(FilePath))
	{
		UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Couldn't parse the file: %s"), *FilePath);
		return nullptr;
	}

	ConfigFile = FilePath;

	// Convert text based data to generic container
	return ConvertDataToInternalTypes();
}

bool FDisplayClusterConfigurationTextParser::SaveData(const UDisplayClusterConfigurationData* ConfigData, const FString& FilePath)
{
	UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Export to text based format is not supported. Use json exporter."));
	return false;
}

UDisplayClusterConfigurationData* FDisplayClusterConfigurationTextParser::ConvertDataToInternalTypes()
{
	UDisplayClusterConfigurationData* Config = NewObject<UDisplayClusterConfigurationData>(ConfigDataOwner ? ConfigDataOwner : GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	check(Config && Config->Scene && Config->Input && Config->Cluster);

	// Fill metadata
	Config->Meta.DataSource = EDisplayClusterConfigurationDataSource::Text;
	Config->Meta.FilePath   = ConfigFile;

	Config->Info.Version     = CfgInfo.Version;
	Config->Info.Description = FString("nDisplay configuration");

	// Scene
	{
		// Scene nodes (Xforms)
		for (const FDisplayClusterConfigurationTextSceneNode& CfgComp : CfgSceneNodes)
		{
			UDisplayClusterConfigurationSceneComponentXform* Comp = NewObject<UDisplayClusterConfigurationSceneComponentXform>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.ParentId;
			Comp->Location       = CfgComp.Loc * 100;
			Comp->Rotation       = CfgComp.Rot;
			Comp->TrackerId      = CfgComp.TrackerId;
			Comp->TrackerChannel = CfgComp.TrackerCh;

			Config->Scene->Xforms.Emplace(CfgComp.Id, Comp);
		}

		// Screens
		for (const FDisplayClusterConfigurationTextScreen& CfgComp : CfgScreens)
		{
			UDisplayClusterConfigurationSceneComponentScreen* Comp = NewObject<UDisplayClusterConfigurationSceneComponentScreen>(Config);
			check(Comp);

			// General
			Comp->ParentId       = CfgComp.ParentId;
			Comp->Location       = CfgComp.Loc * 100;
			Comp->Rotation       = CfgComp.Rot;
			Comp->TrackerId      = CfgComp.TrackerId;
			Comp->TrackerChannel = CfgComp.TrackerCh;
			// Screen specific
			Comp->Size = CfgComp.Size;

			Config->Scene->Screens.Emplace(CfgComp.Id, Comp);
		}

		// Cameras
		for (const FDisplayClusterConfigurationTextCamera& CfgComp : CfgCameras)
		{
			UDisplayClusterConfigurationSceneComponentCamera* Comp = NewObject<UDisplayClusterConfigurationSceneComponentCamera>(Config);
			check(Comp);

			const EDisplayClusterConfigurationEyeStereoOffset EyeOffset = (CfgComp.ForceOffset == 0 ? EDisplayClusterConfigurationEyeStereoOffset::None :
				(CfgComp.ForceOffset < 0 ? EDisplayClusterConfigurationEyeStereoOffset::Left : EDisplayClusterConfigurationEyeStereoOffset::Right));

			// General
			Comp->ParentId       = CfgComp.ParentId;
			Comp->Location       = CfgComp.Loc * 100.f;
			Comp->Rotation       = CfgComp.Rot;
			Comp->TrackerId      = CfgComp.TrackerId;
			Comp->TrackerChannel = CfgComp.TrackerCh;
			// Camera specific
			Comp->InterpupillaryDistance = CfgComp.EyeDist;
			Comp->bSwapEyes              = CfgComp.EyeSwap;
			Comp->StereoOffset           = EyeOffset;

			Config->Scene->Cameras.Emplace(CfgComp.Id, Comp);
		}

		// Meshes
		// There is no meshes in the text version of config
	}

	// Cluster
	{
		// Sync
		{
			// Native input sync
			{
				switch (CfgGeneral.NativeInputSyncPolicy)
				{
				case 0:
					Config->Cluster->Sync.InputSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::input_sync::InputSyncPolicyNone;
					break;

				case 1:
					Config->Cluster->Sync.InputSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::input_sync::InputSyncPolicyReplicateMaster;
					break;

				default:
					Config->Cluster->Sync.InputSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::input_sync::InputSyncPolicyNone;
					break;
				}
			}

			// Render sync
			{
				switch (CfgGeneral.SwapSyncPolicy)
				{
				case 0:
					Config->Cluster->Sync.RenderSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::render_sync::None;
					break;

				case 1:
					Config->Cluster->Sync.RenderSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::render_sync::Ethernet;
					break;

				case 2:
				{
					Config->Cluster->Sync.RenderSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::render_sync::Nvidia;
					Config->Cluster->Sync.RenderSyncPolicy.Parameters.Add(DisplayClusterConfigurationStrings::config::cluster::render_sync::NvidiaSwapGroup,   DisplayClusterTypesConverter::template ToString(CfgNvidia.SyncGroup));
					Config->Cluster->Sync.RenderSyncPolicy.Parameters.Add(DisplayClusterConfigurationStrings::config::cluster::render_sync::NvidiaSwapBarrier, DisplayClusterTypesConverter::template ToString(CfgNvidia.SyncBarrier));
					break;
				}

				default:
					Config->Cluster->Sync.RenderSyncPolicy.Type = DisplayClusterConfigurationStrings::config::cluster::render_sync::Ethernet;
					break;
				}
			}
		}

		// Network
		{
			Config->Cluster->Network.ConnectRetriesAmount = CfgNetwork.ClientConnectTriesAmount;
			Config->Cluster->Network.ConnectRetryDelay = CfgNetwork.ClientConnectRetryDelay;
			Config->Cluster->Network.GameStartBarrierTimeout = CfgNetwork.BarrierGameStartWaitTimeout;
			Config->Cluster->Network.FrameStartBarrierTimeout = CfgNetwork.BarrierWaitTimeout;
			Config->Cluster->Network.FrameEndBarrierTimeout = CfgNetwork.BarrierWaitTimeout;
			Config->Cluster->Network.RenderSyncBarrierTimeout = CfgNetwork.BarrierWaitTimeout;
		}

		// Nodes
		for (const FDisplayClusterConfigurationTextClusterNode& CfgNode: CfgClusterNodes)
		{
			UDisplayClusterConfigurationClusterNode* Node = NewObject<UDisplayClusterConfigurationClusterNode>(Config);
			check(Node);

			// Base parameters
			Node->Host            = CfgNode.Addr;
			Node->bIsSoundEnabled = CfgNode.SoundEnabled;
			
			// Is master node?
			if (CfgNode.IsMaster)
			{
				Config->Cluster->MasterNode.Id = CfgNode.Id;

				// Ports
				Config->Cluster->MasterNode.Ports.ClusterSync         = CfgNode.Port_CS;
				Config->Cluster->MasterNode.Ports.RenderSync          = CfgNode.Port_SS;
				Config->Cluster->MasterNode.Ports.ClusterEventsJson   = CfgNode.Port_CE;
				Config->Cluster->MasterNode.Ports.ClusterEventsBinary = CfgNode.Port_CEB;
			}

			// Find the 'window' entity referenced in cluster_node
			const FDisplayClusterConfigurationTextWindow* const CfgWindow = CfgWindows.FindByPredicate([&CfgNode](const FDisplayClusterConfigurationTextWindow& Item)
			{
				return Item.Id.Equals(CfgNode.WindowId, ESearchCase::IgnoreCase);
			});

			// Initialize window related data
			if (CfgWindow)
			{
				Node->bIsFullscreen = CfgWindow->IsFullscreen;
				Node->WindowRect    = FDisplayClusterConfigurationRectangle(CfgWindow->WinX, CfgWindow->WinY, CfgWindow->ResX, CfgWindow->ResY);

				// Initialize viewports
				for (const FString& ViewportId : CfgWindow->ViewportIds)
				{
					const FDisplayClusterConfigurationTextViewport* const CfgViewport = CfgViewports.FindByPredicate([&ViewportId](const FDisplayClusterConfigurationTextViewport& Item)
					{
						return Item.Id.Equals(ViewportId, ESearchCase::IgnoreCase);
					});

					if (CfgViewport)
					{
						UDisplayClusterConfigurationViewport* Viewport = NewObject<UDisplayClusterConfigurationViewport>(Config);
						check(Viewport);

						Viewport->BufferRatio = CfgViewport->BufferRatio;
						Viewport->Camera      = CfgViewport->CameraId;
						Viewport->Region      = FDisplayClusterConfigurationRectangle(CfgViewport->Loc.X, CfgViewport->Loc.Y, CfgViewport->Size.X, CfgViewport->Size.Y);
						Viewport->GPUIndex    = CfgViewport->GPUIndex;
						Viewport->bIsShared   = CfgViewport->IsShared;
						Viewport->bAllowCrossGPUTransfer = CfgViewport->AllowCrossGPUTransfer;

						const FDisplayClusterConfigurationTextProjection* const CfgProjection = CfgProjections.FindByPredicate([&](const FDisplayClusterConfigurationTextProjection& Item)
						{
							return Item.Id.Equals(CfgViewport->ProjectionId);
						});

						if (CfgProjection)
						{
							FDisplayClusterConfigurationProjection Projection;

							Projection.Type = CfgProjection->Type;

							// We have to use explicit parsing for the 'manual' projection policy because it contains
							// some complex data that DisplayClusterHelpers::str::StrToMap can't properly parse
							if (Projection.Type.Equals(FString(DisplayClusterProjectionStrings::projection::Manual), ESearchCase::IgnoreCase))
							{
								auto Extractor = [](const FString& ParamsLine, const FString& Param, TMap<FString, FString>& OutMap)
								{
									FString Value;
									if (DisplayClusterHelpers::str::ExtractValue(ParamsLine, Param, Value))
									{
										OutMap.Add(FString(Param), Value);
									}
								};

								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::Rotation, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::Matrix, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::MatrixLeft, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::MatrixRight, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::Frustum, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::FrustumLeft, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::FrustumRight, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::AngleL, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::AngleR, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::AngleT, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::manual::AngleB, Projection.Parameters);
							}
							else
							if (Projection.Type.Equals(FString(DisplayClusterProjectionStrings::projection::VIOSO), ESearchCase::IgnoreCase))
							{
								auto Extractor = [](const FString& ParamsLine, const FString& Param, TMap<FString, FString>& OutMap)
								{
									FString Value;
									if (DisplayClusterHelpers::str::ExtractValue(ParamsLine, Param, Value))
									{
										OutMap.Add(FString(Param), Value);
									}
								};

								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::Origin, Projection.Parameters);

								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::INIFile,      Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::ChannelName,  Projection.Parameters);
								
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::File,         Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::CalibIndex,   Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::CalibAdapter, Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::Gamma,        Projection.Parameters);
								Extractor(CfgProjection->Params, DisplayClusterProjectionStrings::cfg::VIOSO::BaseMatrix,   Projection.Parameters);
							}
							else
							{
								DisplayClusterHelpers::str::StrToMap(CfgProjection->Params, Projection.Parameters);
							}

							Projection.Parameters.Remove(FString("id"));
							Projection.Parameters.Remove(FString("type"));

							Viewport->ProjectionPolicy = Projection;
							Node->Viewports.Emplace(ViewportId, Viewport);
						}
					}
				}

				// Initialize postprocess
				for (const FString& PostprocessId : CfgWindow->PostprocessIds)
				{
					const FDisplayClusterConfigurationTextPostprocess* const CfgPP = CfgPostprocess.FindByPredicate([&PostprocessId](const FDisplayClusterConfigurationTextPostprocess& Item)
					{
						return Item.Id.Equals(PostprocessId, ESearchCase::IgnoreCase);
					});

					if (CfgPP)
					{
						FDisplayClusterConfigurationPostprocess Postprocess;
						
						Postprocess.Type = CfgPP->Type;
						
						DisplayClusterHelpers::str::StrToMap(CfgPP->ConfigLine, Postprocess.Parameters);
						Postprocess.Parameters.Remove(FString("id"));
						Postprocess.Parameters.Remove(FString("type"));
						
						Node->Postprocess.Emplace(PostprocessId, Postprocess);
					}
				}

				// Store new cluster node
				Config->Cluster->Nodes.Emplace(CfgNode.Id, Node);
			}
		}
	}

	// Input devices
	for (const FDisplayClusterConfigurationTextInput& CfgInputDevice : CfgInputDevices)
	{
		// Common parameter - address
		FString Address;
		DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Address, Address);

		// Common parameter - channel mapping
		FString StrRemap;
		DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Remap, StrRemap);
		TMap<int32, int32> ChannelRemapping;
		DisplayClusterHelpers::str::StrToMap<int32, int32>(StrRemap, ChannelRemapping, DisplayClusterStrings::common::ArrayValSeparator, FString(":"));

		// Aarsing analog device
		if (CfgInputDevice.Type.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::DeviceAnalog, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceAnalog* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceAnalog>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			Config->Input->AnalogDevices.Emplace(CfgInputDevice.Id, InputDevice);
		}
		// Button device
		else if (CfgInputDevice.Type.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::DeviceButtons, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceButton* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceButton>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			Config->Input->ButtonDevices.Emplace(CfgInputDevice.Id, InputDevice);
		}
		// Keyboard device
		else if (CfgInputDevice.Type.Equals(FString(DisplayClusterConfigurationTextStrings::cfg::data::input::DeviceKeyboard), ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceKeyboard* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceKeyboard>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			FString StrReflectionType;
			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Reflect, StrReflectionType);

			if (StrReflectionType.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::ReflectNone))
			{
				InputDevice->ReflectionType = EDisplayClusterConfigurationKeyboardReflectionType::None;
			}
			else if (StrReflectionType.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::ReflectNdisplay))
			{
				InputDevice->ReflectionType = EDisplayClusterConfigurationKeyboardReflectionType::nDisplay;
			}
			else if (StrReflectionType.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::ReflectCore))
			{
				InputDevice->ReflectionType = EDisplayClusterConfigurationKeyboardReflectionType::Core;
			}
			else if (StrReflectionType.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::ReflectBoth))
			{
				InputDevice->ReflectionType = EDisplayClusterConfigurationKeyboardReflectionType::All;
			}

			Config->Input->KeyboardDevices.Emplace(CfgInputDevice.Id, InputDevice);
		}
		// Tracker device
		else if (CfgInputDevice.Type.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::DeviceTracker, ESearchCase::IgnoreCase))
		{
			UDisplayClusterConfigurationInputDeviceTracker* InputDevice = NewObject<UDisplayClusterConfigurationInputDeviceTracker>(Config);
			check(InputDevice);

			InputDevice->Address = Address;
			InputDevice->ChannelRemapping = ChannelRemapping;

			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::Loc, InputDevice->OriginLocation);
			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::Rot, InputDevice->OriginRotation);

			InputDevice->OriginLocation *= 100;

			auto ConvertMapping = [](const FString& StrMapping, EDisplayClusterConfigurationTrackerMapping& OutMapping)
			{
				if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapX, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::X;
				}
				else if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapNX, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::NX;
				}
				else if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapY, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::Y;
				}
				else if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapNY, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::NY;
				}
				else if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapZ, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::Z;
				}
				else if (StrMapping.Equals(DisplayClusterConfigurationTextStrings::cfg::data::input::MapNZ, ESearchCase::IgnoreCase))
				{
					OutMapping = EDisplayClusterConfigurationTrackerMapping::NZ;
				}
			};

			FString StrMappingFront;
			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Front, StrMappingFront);
			ConvertMapping(StrMappingFront, InputDevice->Front);

			FString StrMappingRight;
			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Right, StrMappingRight);
			ConvertMapping(StrMappingRight, InputDevice->Right);

			FString StrMappingUp;
			DisplayClusterHelpers::str::ExtractValue(CfgInputDevice.Params, DisplayClusterConfigurationTextStrings::cfg::data::input::Up, StrMappingUp);
			ConvertMapping(StrMappingUp, InputDevice->Up);

			Config->Input->TrackerDevices.Emplace(CfgInputDevice.Id, InputDevice);
		}
	}

	// Input bindings
	for (const FDisplayClusterConfigurationTextInputSetup& CfgInputBinding : CfgInputSetupRecords)
	{
		FDisplayClusterConfigurationInputBinding InputBinding;

		InputBinding.DeviceId = CfgInputBinding.Id;
		InputBinding.Channel  = CfgInputBinding.Channel;
		InputBinding.Key      = CfgInputBinding.Key;
		InputBinding.BindTo   = CfgInputBinding.BindName;

		Config->Input->InputBinding.Add(InputBinding);
	}

	// Custom parameters
	Config->CustomParameters = CfgCustom.Params;

	// Diagnostics
	Config->Diagnostics.bSimulateLag = CfgDebug.LagSimulateEnabled;
	Config->Diagnostics.MinLagTime = 0.f;
	Config->Diagnostics.MaxLagTime = CfgDebug.LagMaxTime;

	return Config;
}

bool FDisplayClusterConfigurationTextParser::ParseTextFile(const FString& FilePath)
{
	// Normalize the file path
	FString ConfigPath(FilePath);
	FPaths::NormalizeFilename(ConfigPath);

	// Load data
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Parsing config file: %s"), *ConfigPath);
	if (!FPaths::FileExists(ConfigPath))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("File not found: %s"), *ConfigPath);
		return false;
	}

	TArray<FString> Data;
	if (!FFileHelper::LoadANSITextFileToStrings(*ConfigPath, nullptr, Data) == true)
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Couldn't load config data: %s"), *ConfigPath);
		return false;
	}

	// Parse each line from config
	for (FString& StrLine : Data)
	{
		StrLine.TrimStartAndEndInline();
		ParseTextLine(StrLine);
	}

	return true;
}

void FDisplayClusterConfigurationTextParser::ParseTextLine(const FString& line)
{
	if (line.IsEmpty() || line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::comment::Header)))
	{
		// Skip this line
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::info::Header)))
	{
		AddInfo(impl_parse<FDisplayClusterConfigurationTextInfo>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::Header)))
	{
		AddClusterNode(impl_parse<FDisplayClusterConfigurationTextClusterNode>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::window::Header)))
	{
		AddWindow(impl_parse<FDisplayClusterConfigurationTextWindow>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::screen::Header)))
	{
		AddScreen(impl_parse<FDisplayClusterConfigurationTextScreen>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::Header)))
	{
		AddViewport(impl_parse<FDisplayClusterConfigurationTextViewport>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::postprocess::Header)))
	{
		AddPostprocess(impl_parse<FDisplayClusterConfigurationTextPostprocess>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::camera::Header)))
	{
		AddCamera(impl_parse<FDisplayClusterConfigurationTextCamera>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::scene::Header)))
	{
		AddSceneNode(impl_parse<FDisplayClusterConfigurationTextSceneNode>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::general::Header)))
	{
		AddGeneral(impl_parse<FDisplayClusterConfigurationTextGeneral>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::nvidia::Header)))
	{
		AddNvidia(impl_parse<FDisplayClusterConfigurationTextNvidia>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::network::Header)))
	{
		AddNetwork(impl_parse<FDisplayClusterConfigurationTextNetwork>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::debug::Header)))
	{
		AddDebug(impl_parse<FDisplayClusterConfigurationTextDebug>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::input::Header)))
	{
		AddInput(impl_parse<FDisplayClusterConfigurationTextInput>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::inputsetup::Header)))
	{
		AddInputSetup(impl_parse<FDisplayClusterConfigurationTextInputSetup>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::custom::Header)))
	{
		AddCustom(impl_parse<FDisplayClusterConfigurationTextCustom>(line));
	}
	else if (line.StartsWith(FString(DisplayClusterConfigurationTextStrings::cfg::data::projection::Header)))
	{
		AddProjection(impl_parse<FDisplayClusterConfigurationTextProjection>(line));
	}
	else
	{
		UE_LOG(LogDisplayClusterConfiguration, Warning, TEXT("Unknown config token [%s]"), *line);
	}
}

void FDisplayClusterConfigurationTextParser::AddInfo(const FDisplayClusterConfigurationTextInfo& InCfgInfo)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found info node: %s"), *InCfgInfo.ToString());
	CfgInfo = InCfgInfo;
}

void FDisplayClusterConfigurationTextParser::AddClusterNode(const FDisplayClusterConfigurationTextClusterNode& InCfgCNode)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found cluster node: %s"), *InCfgCNode.ToString());
	CfgClusterNodes.Add(InCfgCNode);
}

void FDisplayClusterConfigurationTextParser::AddWindow(const FDisplayClusterConfigurationTextWindow& InCfgWindow)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found window: %s"), *InCfgWindow.ToString());
	CfgWindows.Add(InCfgWindow);
}

void FDisplayClusterConfigurationTextParser::AddScreen(const FDisplayClusterConfigurationTextScreen& InCfgScreen)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found screen: %s"), *InCfgScreen.ToString());
	CfgScreens.Add(InCfgScreen);
}

void FDisplayClusterConfigurationTextParser::AddViewport(const FDisplayClusterConfigurationTextViewport& InCfgViewport)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found viewport: %s"), *InCfgViewport.ToString());
	CfgViewports.Add(InCfgViewport);
}

void FDisplayClusterConfigurationTextParser::AddProjection(const FDisplayClusterConfigurationTextProjection& InCfgProjection)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found projection: %s"), *InCfgProjection.ToString());
	CfgProjections.Add(InCfgProjection);
}

void FDisplayClusterConfigurationTextParser::AddPostprocess(const FDisplayClusterConfigurationTextPostprocess& InCfgPostprocess)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found postprocess: %s"), *InCfgPostprocess.ToString());
	CfgPostprocess.Add(InCfgPostprocess);
}

void FDisplayClusterConfigurationTextParser::AddCamera(const FDisplayClusterConfigurationTextCamera& InCfgCamera)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found camera: %s"), *InCfgCamera.ToString());
	CfgCameras.Add(InCfgCamera);
}

void FDisplayClusterConfigurationTextParser::AddSceneNode(const FDisplayClusterConfigurationTextSceneNode& InCfgSNode)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found scene node: %s"), *InCfgSNode.ToString());
	CfgSceneNodes.Add(InCfgSNode);
}

void FDisplayClusterConfigurationTextParser::AddGeneral(const FDisplayClusterConfigurationTextGeneral& InCfgGeneral)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found general: %s"), *InCfgGeneral.ToString());
	CfgGeneral = InCfgGeneral;
}

void FDisplayClusterConfigurationTextParser::AddNvidia(const FDisplayClusterConfigurationTextNvidia& InCfgNvidia)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found NVIDIA: %s"), *InCfgNvidia.ToString());
	CfgNvidia = InCfgNvidia;
}

void FDisplayClusterConfigurationTextParser::AddNetwork(const FDisplayClusterConfigurationTextNetwork& InCfgNetwork)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found network: %s"), *InCfgNetwork.ToString());
	CfgNetwork = InCfgNetwork;
}

void FDisplayClusterConfigurationTextParser::AddDebug(const FDisplayClusterConfigurationTextDebug& InCfgDebug)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found debug: %s"), *InCfgDebug.ToString());
	CfgDebug = InCfgDebug;
}

void FDisplayClusterConfigurationTextParser::AddInput(const FDisplayClusterConfigurationTextInput& InCfgInput)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found input device: %s"), *InCfgInput.ToString());
	CfgInputDevices.Add(InCfgInput);
}

void FDisplayClusterConfigurationTextParser::AddInputSetup(const FDisplayClusterConfigurationTextInputSetup& InCfgInputSetup)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found input setup record: %s"), *InCfgInputSetup.ToString());
	CfgInputSetupRecords.Add(InCfgInputSetup);
}

void FDisplayClusterConfigurationTextParser::AddCustom(const FDisplayClusterConfigurationTextCustom& InCfgCustom)
{
	UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Found custom: %s"), *InCfgCustom.ToString());
	CfgCustom = InCfgCustom;
}
