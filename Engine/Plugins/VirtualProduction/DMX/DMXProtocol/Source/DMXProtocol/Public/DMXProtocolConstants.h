// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum 
{
  DMX_UNIVERSE_SIZE = 512,
  DMX_MAX_CHANNEL_VALUE = 255,
  RDM_UID_WIDTH = 6
};

enum
{
	DMX_UNLIMITED_REFRESH_RATE	= 0,
	DMX_SLOW_REFRESH_RATE		= 32,
	DMX_MEDIUM_REFRESH_RATE		= 37,
	DMX_FAST_REFRESH_RATE		= 40,
	DMX_MAX_REFRESH_RATE		= 44
};

enum EDMXPortCapability
{
	DMX_PORT_CAPABILITY_NONE,
	DMX_PORT_CAPABILITY_STATIC,
	DMX_PORT_CAPABILITY_FULL,
};

enum EDMUniverseDirection
{
	DMX_UNIVERSE_UNKNOWN,
	DMX_UNIVERSE_OUTPUT,
	DMX_UNIVERSE_INPUT
};

#define DMX_MAX_NETPIN 137
#define DMX_MAX_SUBNETPIN 15
#define DMX_MAX_UNIVERSE 63999
#define DMX_MAX_UNIVERSEARTNET 15
#define DMX_MAX_FINALUNIVERSE 32768
#define DMX_MAX_ADDRESS 512
#define DMX_MAX_VALUE 255
#define DMX_MAX_PACKET_SIZE 2048u
#define DMX_MAX_FUNCTION_SIZE ((int32)sizeof(int32))

#define DMX_K2_CATEGORY_NAME "DMX"

namespace DMXJsonFieldNames
{
	const FString DMXPortID(TEXT("PortID"));
	const FString DMXUniverseID(TEXT("UniverseID"));
	const FString DMXEthernetPort(TEXT("EthernetPort"));
	const FString DMXIpAddresses(TEXT("IpAddresses"));
}