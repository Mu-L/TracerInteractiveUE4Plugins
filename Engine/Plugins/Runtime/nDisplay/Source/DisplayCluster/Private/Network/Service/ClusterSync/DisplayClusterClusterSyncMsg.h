// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Cluster synchronization messages
 */
//@todo: encapsulate strings below in message classes
namespace FDisplayClusterClusterSyncMsg
{
	constexpr static auto ProtocolName = "ClusterSync";
	
	constexpr static auto TypeRequest  = "request";
	constexpr static auto TypeResponse = "response";

	namespace WaitForGameStart
	{
		constexpr static auto name = "WaitForGameStart";
	};

	namespace WaitForFrameStart
	{
		constexpr static auto name = "WaitForFrameStart";
	};

	namespace WaitForFrameEnd
	{
		constexpr static auto name = "WaitForFrameEnd";
	};

	namespace WaitForTickEnd
	{
		constexpr static auto name = "WaitForTickEnd";
	};

	namespace GetDeltaTime
	{
		constexpr static auto name         = "GetDeltaTime";
		constexpr static auto argDeltaTime = "DeltaTime";
	};

	namespace GetTimecode
	{
		constexpr static auto name         = "GetTimecode";
		constexpr static auto argTimecode  = "Timecode";
		constexpr static auto argFrameRate = "FrameRate";
	}

	namespace GetSyncData
	{
		constexpr static auto name = "GetSyncData";
	};

	namespace GetInputData
	{
		constexpr static auto name = "GetInputData";
	}

	namespace GetEventsData
	{
		constexpr static auto name = "GetEventsData";
	}
};
