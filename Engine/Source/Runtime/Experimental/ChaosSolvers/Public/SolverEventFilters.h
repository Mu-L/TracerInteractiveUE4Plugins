// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Chaos/PBDCollisionTypes.h"
#include "SolverEventFilters.generated.h"

	USTRUCT(Blueprintable)
	struct CHAOSSOLVERS_API FSolverTrailingFilterSettings
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
	struct CHAOSSOLVERS_API FSolverCollisionFilterSettings
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
	struct CHAOSSOLVERS_API FSolverBreakingFilterSettings
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

	class CHAOSSOLVERS_API FSolverCollisionEventFilter
	{
	public:
		FSolverCollisionEventFilter(const FSolverCollisionFilterSettings& InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TCollisionData<float, 3>& InData);
		bool Enabled() { return Settings.FilterEnabled; }

		const FSolverCollisionFilterSettings& Settings;
	};

	class CHAOSSOLVERS_API FSolverTrailingEventFilter
	{
	public:
		FSolverTrailingEventFilter(const FSolverTrailingFilterSettings &InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TTrailingData<float, 3>& InData);
		bool Enabled() { return Settings.FilterEnabled; }

		const FSolverTrailingFilterSettings& Settings;
	};

	class CHAOSSOLVERS_API FSolverBreakingEventFilter
	{
	public:
		FSolverBreakingEventFilter(const FSolverBreakingFilterSettings& InSettings) : Settings(InSettings) {}

		bool Pass(const Chaos::TBreakingData<float, 3>& InData);
		bool Enabled() { return Settings.FilterEnabled; }

		const FSolverBreakingFilterSettings& Settings;
	};


} // namespace Chaos
