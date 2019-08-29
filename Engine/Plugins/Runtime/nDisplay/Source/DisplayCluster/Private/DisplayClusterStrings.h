// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterBuildConfig.h"


namespace DisplayClusterStrings
{
	// Common strings
	static constexpr auto strKeyValSeparator = TEXT("=");

	// Command line arguments
	namespace args
	{
		static constexpr auto Cluster    = TEXT("dc_cluster");
		static constexpr auto Standalone = TEXT("dc_standalone");
		static constexpr auto Node       = TEXT("dc_node");
		static constexpr auto Config     = TEXT("dc_cfg");
		static constexpr auto Camera     = TEXT("dc_camera");

		// Stereo device types (command line values)
		namespace dev
		{
			static constexpr auto Debug = TEXT("dc_dev_debug");
			static constexpr auto QBS   = TEXT("quad_buffer_stereo");
			static constexpr auto TB    = TEXT("dc_dev_top_bottom");
			static constexpr auto SbS   = TEXT("dc_dev_side_by_side");
			static constexpr auto Mono  = TEXT("dc_dev_mono");
		}
	}

	namespace cfg
	{
		// Config file extensions
		namespace file
		{
			static constexpr auto FileExtCfg1 = TEXT("cfg");
			static constexpr auto FileExtCfg2 = TEXT("conf");
			static constexpr auto FileExtCfg3 = TEXT("config");
			static constexpr auto FileExtTxt  = TEXT("txt");
			static constexpr auto FileExtXml  = TEXT("xml");
		}

		// Config special constants
		namespace spec
		{
			static constexpr auto Comment          = TEXT("#");
			static constexpr auto KeyValSeparator  = TEXT("=");
			static constexpr auto ValTrue          = TEXT("true");
			static constexpr auto ValFalse         = TEXT("false");
			static constexpr auto MappingDelimiter = TEXT(",");
		}

		// Config data tokens
		namespace data
		{
			static constexpr auto Id       = TEXT("id");
			static constexpr auto ParentId = TEXT("parent");
			static constexpr auto Loc      = TEXT("loc");
			static constexpr auto Rot      = TEXT("rot");

			// Cluster tokens
			namespace cluster
			{
				static constexpr auto Header   = TEXT("[cluster_node]");
				static constexpr auto Addr     = TEXT("addr");
				static constexpr auto Screen   = TEXT("screen");
				static constexpr auto Viewport = TEXT("viewport");
				static constexpr auto PortCS   = TEXT("port_cs");
				static constexpr auto PortSS   = TEXT("port_ss");
				static constexpr auto Master   = TEXT("master");
				static constexpr auto Sound    = TEXT("sound");
				// + Id
			}

			// Screen tokens
			namespace screen
			{
				static constexpr auto Header = TEXT("[screen]");
				static constexpr auto Size   = TEXT("size");
				// + Id, Parent, Loc, Rot
			}

			// Viewport tokens
			namespace viewport
			{
				static constexpr auto Header = TEXT("[viewport]");
				static constexpr auto PosX   = TEXT("x");
				static constexpr auto PosY   = TEXT("y");
				static constexpr auto Width  = TEXT("width");
				static constexpr auto Height = TEXT("height");
				static constexpr auto FlipH  = TEXT("flip_h");
				static constexpr auto FlipV  = TEXT("flip_v");
				// + Id
			}

			// Camera tokens
			namespace camera
			{
				static constexpr auto Header = TEXT("[camera]");
				// + Id, Loc, Rot, Parent
			}

			// Scene node (transforms)
			namespace scene
			{
				static constexpr auto Header    = TEXT("[scene_node]");
				static constexpr auto TrackerId = TEXT("tracker_id");
				static constexpr auto TrackerCh = TEXT("tracker_ch");
				// + Id, Loc, Rot, Parent
			}

			// Input tokens
			namespace input
			{
				static constexpr auto Header   = TEXT("[input]");
				static constexpr auto Type     = TEXT("type");
				static constexpr auto Address  = TEXT("addr");
				static constexpr auto Remap    = TEXT("remap");
				// + Id

				static constexpr auto Right = TEXT("right");
				static constexpr auto Front = TEXT("front");
				static constexpr auto Up    = TEXT("up");

				static constexpr auto MapX  = TEXT("x");
				static constexpr auto MapNX = TEXT("-x");
				static constexpr auto MapY  = TEXT("y");
				static constexpr auto MapNY = TEXT("-y");
				static constexpr auto MapZ  = TEXT("z");
				static constexpr auto MapNZ = TEXT("-z");

				static constexpr auto DeviceTracker = TEXT("tracker");
				static constexpr auto DeviceAnalog  = TEXT("analog");
				static constexpr auto DeviceButtons = TEXT("buttons");
			}

			// General settings tokens
			namespace general
			{
				static constexpr auto Header            = TEXT("[general]");
				static constexpr auto SwapSyncPolicy    = TEXT("swap_sync_policy");
			}

			// Stereo tokens
			namespace stereo
			{
				static constexpr auto Header  = TEXT("[stereo]");
				static constexpr auto EyeSwap = TEXT("eye_swap");
				static constexpr auto EyeDist = TEXT("eye_dist");
			}

			// Render tokens
			namespace render
			{
				static constexpr auto Header          = TEXT("[render]");
			}

			// Debug tokens
			namespace debug
			{
				static constexpr auto Header    = TEXT("[debug]");
				static constexpr auto LagSim    = TEXT("lag_simulation");
				static constexpr auto LagTime   = TEXT("lag_max_time");
				static constexpr auto DrawStats = TEXT("draw_stats");
			}

			// Custom arguments
			namespace custom
			{
				static constexpr auto Header     = TEXT("[custom]");
				static constexpr auto SwapInt    = TEXT("swap_int");
			}
		}
	};

	namespace rhi
	{
		static constexpr auto OpenGL = TEXT("OpenGL");
		static constexpr auto D3D11  = TEXT("D3D11");
		static constexpr auto D3D12  = TEXT("D3D12");
	}

	namespace misc
	{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		static constexpr auto DbgStubConfig = TEXT("?");
		static constexpr auto DbgStubNodeId    = TEXT("node_stub");
#endif
	}
};
