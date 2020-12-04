// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "SolverEventFilters.generated.h"




	USTRUCT(Blueprintable)
	struct CHAOS_API FSolverTrailingFilterSettings
	{
		GENERATED_USTRUCT_BODY()

		FSolverTrailingFilterSettings() 
			: FilterEnabled(false)
			, MinMass(0.0f)
			, MinSpeed(0.f)
			, MinVolume(0.f)
		{}

		/** Filter is enabled. */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|TrailingData Generation")
		bool FilterEnabled;

		/** The minimum mass threshold for the results (compared with min of particle 1 mass and particle 2 mass). */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|TrailingData Generation", meta = (DisplayName = "Min Mass Threshold"))
		float MinMass;

		/** */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|TrailingData Generation", meta = (DisplayName = "Min Speed Threshold"))
		float MinSpeed;

		/** */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|TrailingData Generation", meta = (DisplayName = "Min Volume Threshold"))
		float MinVolume;
	};

	USTRUCT(BlueprintType)
	struct CHAOS_API FSolverCollisionFilterSettings
	{
		GENERATED_USTRUCT_BODY()

		FSolverCollisionFilterSettings()
			: FilterEnabled(false)
			, MinMass(0.0f)
			, MinSpeed(0.0f)
			, MinImpulse(0.0f)
		{}

		/** Filter is enabled. */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|CollisionData Generation")
		bool FilterEnabled;

		/** The minimum mass threshold for the results (compared with min of particle 1 mass and particle 2 mass). */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|CollisionData Generation", meta = (DisplayName = "Min Mass Threshold"))
		float MinMass;

		/** The min velocity threshold for the results (compared with min of particle 1 speed and particle 2 speed). */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|CollisionData Generation", meta = (DisplayName = "Min Speed Threshold"))
		float MinSpeed;

		/** The minimum impulse threshold for the results. */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|CollisionData Generation", meta = (DisplayName = "Min Impulse Threshold"))
		float MinImpulse;

	};

	USTRUCT(BlueprintType)
	struct CHAOS_API FSolverBreakingFilterSettings
	{
		GENERATED_USTRUCT_BODY()

			FSolverBreakingFilterSettings()
			: FilterEnabled(false)
			, MinMass(0.0f)
			, MinSpeed(0.0f)
			, MinVolume(0.0f)
		{}

		/** Filter is enabled. */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|BreakingData Generation")
		bool FilterEnabled;

		/** The minimum mass threshold for the results (compared with min of particle 1 mass and particle 2 mass). */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|BreakingData Generation", meta = (DisplayName = "Min Mass Threshold"))
		float MinMass;

		/** The min velocity threshold for the results (compared with min of particle 1 speed and particle 2 speed). */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|BreakingData Generation", meta = (DisplayName = "Min Speed Threshold"))
		float MinSpeed;

		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|BreakingData Generation", meta = (DisplayName = "Min Volume Threshold"))
		float MinVolume;

	};


namespace Chaos
{

	template<class T, int d>
	struct TCollisionData;

	template<class T, int d>
	struct TTrailingData;

	template<class T, int d>
	struct TBreakingData;



	class CHAOS_API FSolverCollisionEventFilter
	{
	public:
		FSolverCollisionEventFilter() {}
		FSolverCollisionEventFilter(const FSolverCollisionFilterSettings& InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TCollisionData<float, 3>& InData) const;
		bool Enabled() const { return Settings.FilterEnabled; }
		void UpdateFilterSettings(const FSolverCollisionFilterSettings& InSettings) { Settings = InSettings; }

		FSolverCollisionFilterSettings Settings;
	};

	class CHAOS_API FSolverTrailingEventFilter
	{
	public:
		FSolverTrailingEventFilter() {}
		FSolverTrailingEventFilter(const FSolverTrailingFilterSettings &InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TTrailingData<float, 3>& InData) const;
		bool Enabled() const { return Settings.FilterEnabled; }
		void UpdateFilterSettings(const FSolverTrailingFilterSettings& InSettings) { Settings = InSettings; }

		FSolverTrailingFilterSettings Settings;
	};

	class CHAOS_API FSolverBreakingEventFilter
	{
	public:
		FSolverBreakingEventFilter() {}
		FSolverBreakingEventFilter(const FSolverBreakingFilterSettings& InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TBreakingData<float, 3>& InData) const;
		bool Enabled() const { return Settings.FilterEnabled; }
		void UpdateFilterSettings(const FSolverBreakingFilterSettings& InSettings) { Settings = InSettings; }

		FSolverBreakingFilterSettings Settings;
	};


	/**
	 * Container for the Solver Event Filters that have settings exposed through the Solver Actor
	 */
	class FSolverEventFilters
	{
	public:
		FSolverEventFilters()
			: CollisionFilter(new FSolverCollisionEventFilter())
			, BreakingFilter(new FSolverBreakingEventFilter())
			, TrailingFilter(new FSolverTrailingEventFilter())
			, CollisionEventsEnabled(false)
			, BreakingEventsEnabled(false)
			, TrailingEventsEnabled(false)
		{}

		void SetGenerateCollisionEvents(bool bDoGenerate) { CollisionEventsEnabled = bDoGenerate; }
		void SetGenerateBreakingEvents(bool bDoGenerate) { BreakingEventsEnabled = bDoGenerate; }
		void SetGenerateTrailingEvents(bool bDoGenerate) { TrailingEventsEnabled = bDoGenerate; }

		/* Const access */
		FSolverCollisionEventFilter* GetCollisionFilter() const { return CollisionFilter.Get(); }
		FSolverBreakingEventFilter* GetBreakingFilter() const { return BreakingFilter.Get(); }
		FSolverTrailingEventFilter* GetTrailingFilter() const { return TrailingFilter.Get(); }

		/* non-const access */
		FSolverCollisionEventFilter* GetCollisionFilter() { return CollisionFilter.Get(); }
		FSolverBreakingEventFilter* GetBreakingFilter() { return BreakingFilter.Get(); }
		FSolverTrailingEventFilter* GetTrailingFilter() { return TrailingFilter.Get(); }

		bool IsCollisionEventEnabled() const { return CollisionEventsEnabled; }
		bool IsBreakingEventEnabled() const { return BreakingEventsEnabled; }
		bool IsTrailingEventEnabled() const { return TrailingEventsEnabled; }

	private:

		TUniquePtr<FSolverCollisionEventFilter> CollisionFilter;
		TUniquePtr<FSolverBreakingEventFilter> BreakingFilter;
		TUniquePtr<FSolverTrailingEventFilter> TrailingFilter;

		bool CollisionEventsEnabled;
		bool BreakingEventsEnabled;
		bool TrailingEventsEnabled;
	};


} // namespace Chaos
