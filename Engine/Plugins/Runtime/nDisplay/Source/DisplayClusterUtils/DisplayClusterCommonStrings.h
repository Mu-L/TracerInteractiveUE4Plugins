// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace DisplayClusterStrings
{
	// Common strings
	static constexpr auto strPairSeparator     = TEXT(" ");
	static constexpr auto strKeyValSeparator   = TEXT("=");
	static constexpr auto strArrayValSeparator = TEXT(",");

	namespace cfg
	{
		// Config special constants
		namespace spec
		{
			static constexpr auto Comment          = TEXT("#");
			static constexpr auto KeyValSeparator  = TEXT("=");
			static constexpr auto ValTrue          = TEXT("true");
			static constexpr auto ValFalse         = TEXT("false");
			static constexpr auto MappingDelimiter = TEXT(",");
		}

		// Config file extensions
		namespace file
		{
			static constexpr auto FileExtCfg1 = TEXT("cfg");
			static constexpr auto FileExtCfg2 = TEXT("conf");
			static constexpr auto FileExtCfg3 = TEXT("config");
			static constexpr auto FileExtTxt  = TEXT("txt");
			static constexpr auto FileExtXml  = TEXT("xml");
		}

		// Config data tokens
		namespace data
		{
			static constexpr auto Id       = TEXT("id");
			static constexpr auto ParentId = TEXT("parent");
			static constexpr auto Loc      = TEXT("loc");
			static constexpr auto Rot      = TEXT("rot");

			// Config info
			namespace info
			{
				static constexpr auto Header  = TEXT("[info]");
				static constexpr auto Version = TEXT("version");
			}

			// Cluster tokens
			namespace cluster
			{
				static constexpr auto Header = TEXT("[cluster_node]");
				static constexpr auto Addr   = TEXT("addr");
				static constexpr auto Window = TEXT("window");
				static constexpr auto PortCS = TEXT("port_cs");
				static constexpr auto PortSS = TEXT("port_ss");
				static constexpr auto PortCE = TEXT("port_ce");
				static constexpr auto Master = TEXT("master");
				static constexpr auto Sound  = TEXT("sound");
				// + Id
			}

			// Window tokens
			namespace window
			{
				static constexpr auto Header       = TEXT("[window]");
				static constexpr auto Viewports    = TEXT("viewports");
				static constexpr auto Postprocess  = TEXT("postprocess");
				static constexpr auto Fullscreen   = TEXT("fullscreen");
				static constexpr auto WinX         = TEXT("WinX");
				static constexpr auto WinY         = TEXT("WinY");
				static constexpr auto ResX         = TEXT("ResX");
				static constexpr auto ResY         = TEXT("ResY");
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
				static constexpr auto Header     = TEXT("[viewport]");
				static constexpr auto Projection = TEXT("projection");
				static constexpr auto Camera     = TEXT("camera");
				static constexpr auto PosX       = TEXT("x");
				static constexpr auto PosY       = TEXT("y");
				static constexpr auto Width      = TEXT("width");
				static constexpr auto Height     = TEXT("height");
				static constexpr auto RTT        = TEXT("rtt");
				// + Id
			}

			// Posprocess tokens
			namespace postprocess
			{
				static constexpr auto Header        = TEXT("[postprocess]");
				static constexpr auto PostprocessId = TEXT("type");
				// + Id
			}

			// Camera tokens
			namespace camera
			{
				static constexpr auto Header      = TEXT("[camera]");
				static constexpr auto EyeDist     = TEXT("eye_dist");
				static constexpr auto EyeSwap     = TEXT("eye_swap");
				static constexpr auto ForceOffset = TEXT("force_offset");

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
				static constexpr auto Header  = TEXT("[input]");
				static constexpr auto Type    = TEXT("type");
				static constexpr auto Address = TEXT("addr");
				static constexpr auto Remap   = TEXT("remap");

				static constexpr auto Right   = TEXT("right");
				static constexpr auto Front   = TEXT("front");
				static constexpr auto Up      = TEXT("up");

				static constexpr auto MapX    = TEXT("x");
				static constexpr auto MapNX   = TEXT("-x");
				static constexpr auto MapY    = TEXT("y");
				static constexpr auto MapNY   = TEXT("-y");
				static constexpr auto MapZ    = TEXT("z");
				static constexpr auto MapNZ   = TEXT("-z");

				static constexpr auto DeviceTracker  = TEXT("tracker");
				static constexpr auto DeviceAnalog   = TEXT("analog");
				static constexpr auto DeviceButtons  = TEXT("buttons");
				static constexpr auto DeviceKeyboard = TEXT("keyboard");

				// + Id
			}

			// Input Setup tokens
			namespace inputsetup
			{
				static constexpr auto Header  = TEXT("[input_setup]");
				static constexpr auto Channel = TEXT("ch");
				static constexpr auto Key     = TEXT("key");
				static constexpr auto Bind    = TEXT("bind");

				// + Id
			}

			// General settings tokens
			namespace general
			{
				static constexpr auto Header         = TEXT("[general]");
				static constexpr auto SwapSyncPolicy = TEXT("swap_sync_policy");
			}

			// Stereo tokens
			namespace stereo
			{
				static constexpr auto Header = TEXT("[stereo]");
			}

			// Render tokens
			namespace render
			{
				static constexpr auto Header = TEXT("[render]");
			}

			// Network tokens
			namespace network
			{
				static constexpr auto Header = TEXT("[network]");
				static constexpr auto ClientConnectTriesAmount    = TEXT("cln_conn_tries_amount");
				static constexpr auto ClientConnectRetryDelay     = TEXT("cln_conn_retry_delay");
				static constexpr auto BarrierGameStartWaitTimeout = TEXT("game_start_timeout");
				static constexpr auto BarrierWaitTimeout          = TEXT("barrier_wait_timeout");
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
				static constexpr auto Header = TEXT("[custom]");
			}

			// Projection data
			namespace projection
			{
				static constexpr auto Header = TEXT("[projection]");
				static constexpr auto Type   = TEXT("type");

				// + Id
			}
		}
	};

	namespace rhi
	{
		static constexpr auto OpenGL = TEXT("OpenGL");
		static constexpr auto D3D11  = TEXT("D3D11");
		static constexpr auto D3D12  = TEXT("D3D12");
	}

	namespace platform
	{
		static constexpr auto Windows = TEXT("Windows");
	}
};
