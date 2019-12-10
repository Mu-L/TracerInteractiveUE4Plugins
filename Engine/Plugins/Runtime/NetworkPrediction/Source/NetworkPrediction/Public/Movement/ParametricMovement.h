// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved

#pragma once

#include "NetworkSimulationModel.h"
#include "BaseMovementComponent.h"

#include "ParametricMovement.generated.h"

class IParametricMovementDriver;

namespace ParametricMovement
{
	class IMovementDriver;

	// State the client generates
	struct FInputCmd
	{
		// Input Playrate. This being set can be thought of "telling the simulation what its new playrate should be"
		TOptional<float> PlayRate;

		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << PlayRate;
		}

		void Log(FStandardLoggingParameters& P) const
		{
			if (P.Context == EStandardLoggingContext::HeaderOnly)
			{
				P.Ar->Logf(TEXT(" %d "), P.Keyframe);
			}
			else if (P.Context == EStandardLoggingContext::Full)
			{
				if (PlayRate.IsSet())
				{
					P.Ar->Logf(TEXT("PlayRate: %.2f"), PlayRate.GetValue());
				}
			}
		}
	};

	// State we are evolving frame to frame and keeping in sync
	struct FMoveState
	{
		float Position;
		float PlayRate;

		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << Position;
			P.Ar << PlayRate;
		}

		// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
		bool ShouldReconcile(const FMoveState& AuthorityState)
		{
			const float ErrorTolerance = 0.01f;
			return (FMath::Abs<float>(AuthorityState.Position - Position) > ErrorTolerance) || (FMath::Abs<float>(AuthorityState.PlayRate - PlayRate) > ErrorTolerance);
		}

		void Log(FStandardLoggingParameters& Params) const
		{
			if (Params.Context == EStandardLoggingContext::HeaderOnly)
			{
				Params.Ar->Logf(TEXT(" %d "), Params.Keyframe);
			}
			else if (Params.Context == EStandardLoggingContext::Full)
			{
				Params.Ar->Logf(TEXT("Frame: %d"), Params.Keyframe);
				Params.Ar->Logf(TEXT("Pos: %.2f"), Position);
				Params.Ar->Logf(TEXT("Rate: %.2f"), PlayRate);
			}
		}

		void VisualLog(const FVisualLoggingParameters& Parameters, IMovementDriver* Driver, IMovementDriver* LogDriver) const;

		static void Interpolate(const FMoveState& From, const FMoveState& To, const float PCT, FMoveState& OutDest)
		{
			OutDest.Position = From.Position + ((To.Position - From.Position) * PCT);
			OutDest.PlayRate = From.PlayRate + ((To.PlayRate - From.PlayRate) * PCT);
		}
	};

	// Auxiliary state that is input into the simulation. Doesn't change during the simulation tick.
	// (It can change and even be predicted but doing so will trigger more bookeeping, etc. Changes will happen "next tick")
	struct FAuxState
	{
		float Multiplier=1;
		void NetSerialize(const FNetSerializeParams& P)
		{
			P.Ar << Multiplier;
		}
	};

	using TMovementBufferTypes = TNetworkSimBufferTypes<FInputCmd, FMoveState, FAuxState>;

	// Interface between the simulation and owning component driving it. Functions added here are available in ::Update
	class IMovementDriver : public TNetworkSimDriverInterfaceBase<TMovementBufferTypes>
	{
	public:

		// BaseMovement driver API (functions for moving around a primitive component)
		virtual IBaseMovementDriver& GetBaseMovementDriver() = 0;

		// Advance parametric time. This is meant to do simple things like looping/reversing etc.
		// Note how this should be STATIC and not rely on state outside of what is passed in (such thing would need to be done inside the simulation, not through the driver!)
		virtual void AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const = 0;

		// Actually turn the given position into a transform. Again, should be static and not conditional on changing state outside of the network sim
		virtual void MapTimeToTransform(const float InPosition, FTransform& OutTransform) const = 0;
	};
	
	class FMovementSimulation
	{
	public:
		static void Update(IMovementDriver* Driver, const float DeltaSeconds, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState);
		static const FName GroupName;
	};


	// Actual definition of our network simulation.
	template<int32 FixedStepMS=0>
	using FMovementSystem = TNetworkedSimulationModel<FMovementSimulation, IMovementDriver, TMovementBufferTypes, TNetworkSimTickSettings<FixedStepMS>>;

	//using TSimTime = FMovementSystem::TSimTime;

} // End namespace

// Needed to trick UHT into letting UMockNetworkSimulationComponent implement. UHT cannot parse the ::
// Also needed for forward declaring. Can't just be a typedef/using =
class IParametricMovementDriver : public ParametricMovement::IMovementDriver { };

// -------------------------------------------------------------------------------------------------------------------------------
//	ActorComponent for running basic Parametric movement. 
//	Parametric movement could be anything that takes a Time and returns an FTransform.
//	
//	Initially, we will support pushing (ie, we sweep as we update the mover's position).
//	But we will not allow a parametric mover from being blocked. 
//
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UParametricMovementComponent : public UBaseMovementComponent, public IParametricMovementDriver
{
	GENERATED_BODY()

	UParametricMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Base TNetworkModelSimulation driver
	FString GetDebugName() const override;
	const UObject* GetVLogOwner() const override;
	void InitSyncState(ParametricMovement::FMoveState& OutSyncState) const override;
	void FinalizeFrame(const ParametricMovement::FMoveState& SyncState) override;
	void ProduceInput(const FNetworkSimTime SimTime, ParametricMovement::FInputCmd& Cmd);

	// Base Movement Driver
	IBaseMovementDriver& GetBaseMovementDriver() override final { return *static_cast<IBaseMovementDriver*>(this); }

	// Parametric Movement Driver
	virtual void AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const override;
	virtual void MapTimeToTransform(const float InPosition, FTransform& OutTransform) const override;

protected:

	virtual INetworkSimulationModel* InstantiateNetworkSimulation() override;
	FNetworkSimulationModelInitParameters GetSimulationInitParameters(ENetRole Role) override;

	// ------------------------------------------------------------------------
	// Temp Parametric movement example
	//	The essence of this movement simulation is to map some Time value to a transform. That is it.
	//	(It could be mapped via a spline, a curve, a simple blueprint function, etc).
	//	What is below is just a simple C++ implementation to stand things up. Most likely we would 
	//	do additional subclasses to vary the way this is implemented)
	// ------------------------------------------------------------------------
	
	/** Disables starting the simulation. For development/testing ease of use */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	bool bDisableParametricMovementSimulation = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	FVector ParametricDelta = FVector(0.f, 0.f, 500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MinTime = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovement)
	float MaxTime = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableDependentSimulation = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableInterpolation = true;

	/** Calls ForceNetUpdate every frame. Has slightly different behavior than a very high NetUpdateFrequency */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	bool bEnableForceNetUpdate = false;

	/** Sets NetUpdateFrequency on parent. This is editable on the component and really just meant for use during development/test maps */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ParametricMovementNetworking)
	float ParentNetUpdateFrequency = 0.f;

	FTransform CachedStartingTransform;

	TOptional<float> PendingPlayRate = 1.f;
};

