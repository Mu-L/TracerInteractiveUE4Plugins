// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/NetSerialization.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "WorldCollision.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/OutputDevice.h"
#include "Misc/CoreDelegates.h"

#include "NetworkSimulationModel.h"
#include "NetworkPredictionComponent.h"

#include "MockNetworkSimulation.generated.h"

class IMockNetworkSimulationDriver;
class IMockDriver;

// -------------------------------------------------------------------------------------------------------------------------------
//	Mock Network Simulation
//
//	This provides a minimal "mock" example of using the Network Prediction system. The simulation being run by these classes
//	is a simple accumulator that takes random numbers (FMockInputCmd::InputValue) as input. There is no movement related functionality
//	in this example. This is just meant to show the bare minimum hook ups into the system to make it easier to understand.
//
//	Highlights:
//		FMockNetworkModel::Update			The "core update" function of the simulation.
//		UMockNetworkSimulationComponent			The UE4 Actor Component that anchors the system to an actor.
//
//	Usage:
//		You can just add a UMockNetworkSimulationComponent to any ROLE_AutonomousProxy actor yourself (Default Subobject, through blueprints, manually, etc)
//		The console command "mns.Spawn" can be used to dynamically spawn the component on every pawn. Must be run on the server or in single process PIE.
//
//	Once spawned, there are some useful console commands that can be used. These bind to number keys by default (toggleable via mns.BindAutomatically).
//		[Five] "mns.DoLocalInput 1"			can be used to submit random local input into the accumulator. This is how you advance the simulation.
//		[Six]  "mns.RequestMispredict 1"	can be used to force a mispredict (random value added to accumulator server side). Useful for tracing through the correction/resimulate code path.
//		[Nine] "nms.Debug.LocallyControlledPawn"	toggle debug hud for the locally controlled player.
//		[Zero] "nms.Debug.ToggleContinous"			Toggles continuous vs snapshotted display of the debug hud
//
//	Notes:
//		Everything is crammed into MockNetworkSimulation.h/.cpp. It may make sense to break the simulation and component code into
//		separate files for more complex simulations.
//
// -------------------------------------------------------------------------------------------------------------------------------

// State the client generates
struct FMockInputCmd
{
	float InputValue=0;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << InputValue;
	}

	void Log(FStandardLoggingParameters& P) const
	{
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("InputValue: %4f"), InputValue);
		}
		else
		{
			P.Ar->Logf(TEXT("%2f"), InputValue);
		}
	}
};

// State we are evolving frame to frame and keeping in sync
struct FMockSyncState
{
	float Total=0;
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Total;
	}

	// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
	bool ShouldReconcile(const FMockSyncState& AuthorityState)
	{
		return FMath::Abs<float>(Total - AuthorityState.Total) > SMALL_NUMBER;
	}

	void Log(FStandardLoggingParameters& P) const
	{
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("Total: %4f"), Total);
		}
		else
		{
			P.Ar->Logf(TEXT("%2f"), Total);
		}
	}

	void VisualLog(const FVisualLoggingParameters& Parameters, IMockDriver* Driver, IMockDriver* LogDriver) const;

	static void Interpolate(const FMockSyncState& From, const FMockSyncState& To, const float PCT, FMockSyncState& OutDest)
	{
		OutDest.Total = From.Total + ((To.Total - From.Total) * PCT);
	}
};

// Auxiliary state that is input into the simulation. Doesn't change during the simulation tick.
// (It can change and even be predicted but doing so will trigger more bookeeping, etc. Changes will happen "next tick")
// NOTE: Currently incomplete!
struct FMockAuxState
{
	float Multiplier=1;
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Multiplier;
	}
};

using TMockNetworkSimulationBufferTypes = TNetworkSimBufferTypes<FMockInputCmd, FMockSyncState, FMockAuxState>;

static FName MockSimulationGroupName("Mock");

// Interface between the simulation and owning component driving it. Functions added here are available in ::Update and must be implemented by UMockNetworkSimulationComponent.
class IMockDriver : public TNetworkSimDriverInterfaceBase<TMockNetworkSimulationBufferTypes>
{
public:

	virtual UWorld* GetDriverWorld() const = 0;
	virtual FTransform GetDebugWorldTransform() const = 0;
};

class FMockNetworkSimulation
{
public:

	/** Main update function */
	static void Update(IMockDriver* Driver, const float DeltaTimeSeconds, const FMockInputCmd& InputCmd, const FMockSyncState& InputState, FMockSyncState& OutputState, const FMockAuxState& AuxState);

	/** Tick group the simulation maps to */
	static const FName GroupName;
};

// Actual definition of our network simulation.
using FMockNetworkModel = TNetworkedSimulationModel<FMockNetworkSimulation, IMockDriver, TMockNetworkSimulationBufferTypes>;

// Needed to trick UHT into letting UMockNetworkSimulationComponent implement. UHT cannot parse the ::
// Also needed for forward declaring. Can't just be a typedef/using =
class IMockNetworkSimulationDriver : public IMockDriver { };

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running a MockNetworkSimulation
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UMockNetworkSimulationComponent : public UNetworkPredictionComponent, public IMockNetworkSimulationDriver
{
	GENERATED_BODY()

public:

	UMockNetworkSimulationComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	
	virtual INetworkSimulationModel* InstantiateNetworkSimulation() override;
	
public:

	FString GetDebugName() const override;
	virtual UObject* GetVLogOwner() const override final;

	void InitSyncState(FMockSyncState& OutSyncState) const override;
	void FinalizeFrame(const FMockSyncState& SyncState) override;
	void ProduceInput(const FNetworkSimTime SimTime, FMockInputCmd& Cmd);
	virtual UWorld* GetDriverWorld() const override final { return GetWorld(); }
	virtual FTransform GetDebugWorldTransform() const override final;

	// Mock representation of "syncing' to the sync state in the network sim.
	float MockValue = 1000.f;
};