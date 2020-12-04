// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "UdpMessagingSettings.generated.h"

/** Defines the UDP message format available (how the message data is encoded). */
UENUM()
enum class EUdpMessageFormat : uint8
{
	/** No format specified. Legacy - Not exposed to user.*/
	None = 0 UMETA(Hidden),

	/** JSON format specified. Legacy - Not exposed to user.*/
	Json UMETA(Hidden),

	/** Tagged property format specified. Legacy - Not exposed to user.*/
	TaggedProperty UMETA(Hidden),

	/**
	 * UDP messages are encoded in CBOR, using the platform endianness. This is the fastest and preferred option, but the CBOR data will not be readable by an external standard-compliant CBOR parser
	 * if generated from a little endian platform. If the data needs to be consumed outside the Unreal Engine, consider using CborStandardEndianness format instead.
	 */
	CborPlatformEndianness UMETA(DisplayName="CBOR (Platform Endianness)"),

	/**
	 * UDP messages are encoded in CBOR, using the CBOR standard-complinant endianness (big endian). It will perform slower on a little-endian platform, but the data will be readable by standard CBOR parsers.
	 * Useful if the UDP messages needs to be analyzed/consumed outside the Unreal Engine.
	 */
	CborStandardEndianness UMETA(DisplayName="CBOR (Standard Endianness)"),
};

UCLASS(config=Engine)
class UUdpMessagingSettings
	: public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/**
	 * Whether UDP messaging is enabled by default. If false -messaging will need to be added
	 * to the commandline when running non-editor builds.
	 *
	 * (Note - in Shipping builds ALLOW_UDP_MESSAGING_SHIPPING=1 must also be defined in TargetRules
	 * for messaging to be available with or without this setting)
	 */
	UPROPERTY(config, EditAnywhere, Category = Availability)
	bool EnabledByDefault = false;

	/**
	 * Whether the UDP transport channel is enabled.
	 * Can be specified on the command line with `-UDPMESSAGING_TRANSPORT_ENABLE=`
	 */
	UPROPERTY(config, EditAnywhere, Category=Transport)
	bool EnableTransport;

	/** Whether the UDP transport channel should try to auto repair when in error. */
	UPROPERTY(config, EditAnywhere, Category=Transport, AdvancedDisplay)
	bool bAutoRepair = true;

	/** Whether to stop the transport service when the application deactivates, and restart it when the application is reactivated */
	UPROPERTY(config)
	bool bStopServiceWhenAppDeactivates = true;

	/**
	 * The IP endpoint to listen to and send packets from.
	 *
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 * 0.0.0.0:0 will bind to the default network adapter on Windows,
	 * and all available network adapters on other operating systems.
	 * Specifying an interface IP here, will use that interface for multicasting and static endpoint *might* also reach this client through <unicast ip:multicast port>
	 * Specifying both the IP and Port will allow usage of static endpoint to reach this client
	 * Can be specified on the command line with `-UDPMESSAGING_TRANSPORT_UNICAST=`
	 */
	UPROPERTY(config, EditAnywhere, Category=Transport)
	FString UnicastEndpoint;

	/**
	 * The IP endpoint to send multicast packets to.
	 *
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 * The multicast IP address must be in the range 224.0.0.0 to 239.255.255.255.
	 * Can be specified on the command line with `-UDPMESSAGING_TRANSPORT_MULTICAST=`
	 */
	UPROPERTY(config, EditAnywhere, Category=Transport)
	FString MulticastEndpoint;

	/** The format used to serialize the UDP message payload. */
	UPROPERTY(config, EditAnywhere, Category=Transport)
	EUdpMessageFormat MessageFormat = EUdpMessageFormat::CborPlatformEndianness;

	/** The time-to-live (TTL) for sent multicast packets. */
	UPROPERTY(config, EditAnywhere, Category=Transport, AdvancedDisplay)
	uint8 MulticastTimeToLive;

	/**
	 * The IP endpoints of static devices.
	 *
	 * Use this setting to reach devices on other subnets, such as mobile phones on a WiFi network.
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 */
	UPROPERTY(config, EditAnywhere, Category=Transport, AdvancedDisplay)
	TArray<FString> StaticEndpoints;

public:

	/** Whether the UDP tunnel is enabled. */
	UPROPERTY(config, EditAnywhere, Category=Tunnel)
	bool EnableTunnel;

	/**
	 * The local IP endpoint to listen to and send packets from.
	 *
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 */
	UPROPERTY(config, EditAnywhere, Category=Tunnel)
	FString TunnelUnicastEndpoint;

	/**
	 * The IP endpoint to send multicast packets to.
	 *
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 * The multicast IP address must be in the range 224.0.0.0 to 239.255.255.255.
	 */
	UPROPERTY(config, EditAnywhere, Category=Tunnel)
	FString TunnelMulticastEndpoint;

	/**
	 * The IP endpoints of remote tunnel nodes.
	 *
	 * Use this setting to connect to remote tunnel services.
	 * The format is IP_ADDRESS:PORT_NUMBER.
	 */
	UPROPERTY(config, EditAnywhere, Category=Tunnel, AdvancedDisplay)
	TArray<FString> RemoteTunnelEndpoints;
};
