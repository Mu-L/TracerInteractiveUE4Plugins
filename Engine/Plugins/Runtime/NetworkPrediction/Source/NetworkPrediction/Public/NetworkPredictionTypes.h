// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "NetworkPredictionTypes.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
//	This file contains supporting types that are used by both the UE4 side (UNetworkPredictionComponent etc) and templated
//	network simulation model side (TNetworkedSimulationModel).
// -------------------------------------------------------------------------------------------------------------------------------

#define NETSIM_MODEL_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if NETSIM_MODEL_DEBUG
#define DO_NETSIM_MODEL_DEBUG(X) X
#else
#define DO_NETSIM_MODEL_DEBUG(X)
#endif

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetworkSim, Log, All);

UENUM()
enum class EReplicationProxyTarget: uint8
{
	ServerRPC,			// Client -> Server
	AutonomousProxy,	// Owning/Controlling client
	SimulatedProxy,		// Non owning client
	Replay,				// Replay net driver
	Debug,				// Replication target that is disabled in shipping
};

inline FString LexToString(EReplicationProxyTarget A)
{
	return *UEnum::GetValueAsString(TEXT("NetworkPrediction.EReplicationProxyTarget"), A);
}

// The parameters for NetSerialize that are passed around the system. Everything should use this, expecting to have to add more.
struct FNetSerializeParams
{
	FNetSerializeParams(FArchive& InAr) : Ar(InAr) { }
	FArchive& Ar;
};

struct FNetworkSimulationModelInitParameters
{
	int32 InputBufferSize=0;
	int32 SyncedBufferSize=0;
	int32 AuxBufferSize=0;
	int32 DebugBufferSize=0;
	int32 HistoricBufferSize=0;
};

struct FNetSimProcessedFrameDebugInfo
{
	int32 LocalGFrameNumber; // Local GFrame number
	float LocalDeltaTimeSeconds; // Local frame time
	TArray<int32> ProcessedKeyframes; // Which keyframes were processed this frame
	int32 LastProcessedKeyframe; // What LastProcessedKeyframe was at the end of the frame. Does NOT mean we processed it this frame!
	int32 HeadKeyframe; // Head keyframe of the inputbuffer when the frame ended

	float RemainingAllowedSimulationTimeSeconds;
	
	int32 LastSentInputKeyframe;
	int32 LastReceivedInputKeyframe;

	void NetSerialize(const FNetSerializeParams& P)
	{	
		P.Ar << LocalGFrameNumber;
		P.Ar << LocalDeltaTimeSeconds;
		P.Ar << LastProcessedKeyframe;
		P.Ar << HeadKeyframe;
		P.Ar << RemainingAllowedSimulationTimeSeconds;

		P.Ar << LastSentInputKeyframe;
		P.Ar << LastReceivedInputKeyframe;

		SafeNetSerializeTArray_Default<31>(P.Ar, ProcessedKeyframes);
	}
};

enum class EStandardLoggingContext: uint8
{
	HeaderOnly,		// Minimal logging
	Full			// "Logs everything"
};

struct FStandardLoggingParameters
{
	FStandardLoggingParameters(FOutputDevice* InAr, EStandardLoggingContext InContext, int32 InKeyframe)
		: Ar(InAr), Context(InContext), Keyframe(InKeyframe) { }
	FOutputDevice* Ar;
	EStandardLoggingContext Context;
	int32 Keyframe;
};

UENUM()
enum class EVisualLoggingContext: uint8
{
	// NOTE: Keep FVisualLoggingParameters::DebugColors in sync with this

	// (Contexts used in core network sim)
	LastPredicted,		// The last predicted state. Where the character "is now"
	OtherPredicted,		// "Middle" states between LastComfirmed and LastPredicted. Recommend to draw small crumbs (not full model/cylinder/etc) to avoid washing out the scene.
	LastConfirmed,		// The last confirmed state from the server
	FirstMispredicted,	// The first state that was mispredicted on the client. This correlates with LastConfirmed.
	OtherMispredicted,	// subsequent mispredicted states on the client. Recommend to draw small crumbs (not full model/cylinder/etc) to avoid washing out the scene.
	LastMispredicted,	// The latest predictive state when a misprediction was detected.
	CurrentServerPIE,	// The current server position *right now*. Only available in PIE

	// (Contexts used by interpolation)
	InterpolationBufferHead,// Head end of sync buffer while interpolating
	InterpolationBufferTail,// Tail end of sync buffer while interpolating
	InterpolationFrom,		// Immediate "from" interpolation keyframe
	InterpolationTo,		// Immediate "to" interpolation keyframe
	
	InterpolationLatest,	// Latest interpolation position in normal circumstances
	InterpolationWaiting,	// Latest interpolation while waiting (overrun)
	InterpolationSpeedUp,	// Latest interpolation while speeding up (underrun)

	MAX
};

inline FString LexToString(EVisualLoggingContext A)
{
	return *UEnum::GetValueAsString(TEXT("NetworkPrediction.EVisualLoggingContext"), A);
}

enum class EVisualLoggingDrawType : uint8
{
	Full,				// Draw "the full thing" (maybe a collision capsule for example)
	Crumb,				// Draw a small/minimal representation (E.g, a point or small axis)
};

enum class EVisualLoggingLifetime : uint8
{
	Transient,			// This logging is transient and will (probably) be done every frame. Don't persist (in contexts where that would matter).
	Persistent,			// This is more of a persistent/one-off event that should be drawn for some longer amount of time (probably configurable at whatever level is doing the logging)
};

struct FVisualLoggingParameters
{
	FVisualLoggingParameters(EVisualLoggingContext InContext, int32 InKeyframe, EVisualLoggingLifetime InLifetime) :
		 Context(InContext), Keyframe(InKeyframe), Lifetime(InLifetime) { }

	EVisualLoggingContext Context;
	int32 Keyframe;
	EVisualLoggingLifetime Lifetime;

	FColor GetDebugColor() const { return DebugColors[(int32)Context]; }
	static NETWORKPREDICTION_API FColor DebugColors[(int32)EVisualLoggingContext::MAX];
};

UENUM()
enum class ESimulatedUpdateMode : uint8
{
	Interpolate,		// Update from previous to current known states from the server. This puts the simulation further "behind" due to having to buffer the known state (but is never "wrong" and doesn't require a simulation update)
	Extrapolate,		// Extrapolate the simulation once per local frame, by synthesizing (guessing) input commands
	ForwardPredict		// Predict the simulation ahead. For a simulated proxy to do this, it must be tied to an autonomous proxy
};

inline FString LexToString(ESimulatedUpdateMode A)
{
	return *UEnum::GetValueAsString(TEXT("NetworkPrediction.ESimulatedUpdateMode"), A);
}

// -------------------------------------------------------------------------------------------------------------------------------
// Ticking parameters use to drive the simulation
// -------------------------------------------------------------------------------------------------------------------------------

struct NETWORKPREDICTION_API FNetSimTickParameters
{
	FNetSimTickParameters(float InLocalDeltaTimeSeconds) : LocalDeltaTimeSeconds(InLocalDeltaTimeSeconds) { }
	FNetSimTickParameters(float InLocalDeltaTimeSeconds, AActor* Actor);

	/** Initializes Role and bGenerateLocalInputCmds from an Actor's state. */
	void InitFromActor(AActor* Actor);

	// Owner's role. Necessary to know which proxy we should be forwarding functions in tick to
	ENetRole Role = ROLE_None;

	// Are we creating input cmds locally. Note this is distinct from Role/Authority:
	//		-[On Server] Autonomous Proxy client = false
	//		-[On Server] Non player controlled actor = true
	//		-[On Client] Simulated proxies (everyone but client) = true, if you want extrapolation. Note clients can just not tick the netsim to disable extrapolation as well.
	bool bGenerateLocalInputCmds = false;

	float LocalDeltaTimeSeconds = 0.f;
};

// -------------------------------------------------------------------------------------------------------------------------------
// Interface that the proxy talks to. This is what will implement the replication.
// -------------------------------------------------------------------------------------------------------------------------------
class IReplicationProxy
{
public:

	virtual ~IReplicationProxy() { }
	
	virtual void NetSerializeProxy(EReplicationProxyTarget Target, const FNetSerializeParams& Params) = 0;
	virtual int32 GetProxyDirtyCount(EReplicationProxyTarget Target) = 0;
};

class INetworkSimulationModel : public IReplicationProxy
{
public:

	virtual FName GetSimulationGroupName() const = 0;

	virtual void Reconcile(const ENetRole Role) = 0;
	virtual void Tick(const FNetSimTickParameters&) = 0;
	virtual void InitializeForNetworkRole(const ENetRole Role, const FNetworkSimulationModelInitParameters& Parameters) = 0;

	virtual bool ShouldSendServerRPC(float DeltaSeconds) = 0;
	virtual void SetDesiredServerRPCSendFrequency(float DesiredHz) = 0;

	// ----------------------------------------------------------------------
	// Functions for depedent simulation (forward predicting a simulated proxy sim along with an auto proxy sim)
	// ----------------------------------------------------------------------
	
	// Main function to call on simulated proxy sim
	virtual void SetParentSimulation(INetworkSimulationModel* Simulation) = 0;
	virtual INetworkSimulationModel* GetParentSimulation() const = 0;
	
	virtual void AddDepdentSimulation(INetworkSimulationModel* Simulation) = 0;
	virtual void RemoveDependentSimulation(INetworkSimulationModel* Simulation) = 0;
	
	// Tell parent sim that a dependent sim needs to reconcile (parent sim drives this)
	virtual void NotifyDependentSimNeedsReconcile() = 0;
	
	// Called by parent sim on the dependent sim as it reconciles
	virtual void BeginRollback(const struct FNetworkSimTime& RollbackDeltaTime, const int32 ParentKeyframe) = 0;
	virtual void StepRollback(const struct FNetworkSimTime& Step, const int32 ParentKeyframe, const bool FinalStep) = 0;
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FReplicationProxy
//	Replicated USTRUCT that point to the IReplicationProxy to do the replication.
// -------------------------------------------------------------------------------------------------------------------------------
USTRUCT()
struct FReplicationProxy
{
	GENERATED_BODY()

	void Init(IReplicationProxy* InNetworkSimModel, EReplicationProxyTarget InReplicationTarget);
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
	void OnPreReplication();	
	bool Identical(const FReplicationProxy* Other, uint32 PortFlags) const;
	
private:

	IReplicationProxy* NetworkSimModel;
	EReplicationProxyTarget ReplicationTarget;
	int32 CachedDirtyCount = 0;
};

template<>
struct TStructOpsTypeTraits<FReplicationProxy> : public TStructOpsTypeTraitsBase2<FReplicationProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FServerRPCProxyParameter
//	Used for the client->Server RPC. Since this is instantiated on the stack by the replication system prior to net serializing,
//	we have no opportunity to point the RPC parameter to the member variables we want. So we serialize into a generic temp byte buffer
//	and move them into the real buffers on the component in the RPC body (via ::NetSerialzeToProxy).
// -------------------------------------------------------------------------------------------------------------------------------
USTRUCT()
struct FServerReplicationRPCParameter
{
	GENERATED_BODY()

	// Receive flow: ctor() -> NetSerializetoProxy
	FServerReplicationRPCParameter() : Proxy(nullptr)	{ }
	void NetSerializeToProxy(FReplicationProxy& InProxy);

	// Send flow: ctor(Proxy) -> NetSerialize
	FServerReplicationRPCParameter(FReplicationProxy& InProxy) : Proxy(&InProxy) { }
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

private:

	static TArray<uint8> TempStorage;

	FReplicationProxy* Proxy;
	int64 CachedNumBits = -1;
	class UPackageMap* CachedPackageMap = nullptr;
};

template<>
struct TStructOpsTypeTraits<FServerReplicationRPCParameter> : public TStructOpsTypeTraitsBase2<FServerReplicationRPCParameter>
{
	enum
	{
		WithNetSerializer = true
	};
};

// Helper struct to bypass the bandwidth limit imposed by the engine's NetDriver (QueuedBits, NetSpeed, etc).
// This is really a temp measure to make the system easier to drop in/try in a project without messing with your engine settings.
// (bandwidth optimizations have not been done yet and the system in general hasn't been stressed with packetloss / gaps in command streams)
// So, you are free to use this in your own code but it may be removed one day. Hopefully in general bandwidth limiting will also become more robust.
struct FScopedBandwidthLimitBypass
{
	FScopedBandwidthLimitBypass(AActor* OwnerActor);
	~FScopedBandwidthLimitBypass();
private:

	int64 RestoreBits = 0;
	class UNetConnection* CachedNetConnection = nullptr;
};

