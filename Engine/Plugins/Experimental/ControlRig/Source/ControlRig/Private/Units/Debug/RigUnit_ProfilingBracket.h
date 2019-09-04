// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "RigUnit_ProfilingBracket.generated.h"

USTRUCT(meta=(DisplayName="Start Profiling Timer", Keywords="Measure,BeginProfiling,Profile"))
struct FRigUnit_StartProfilingTimer : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_StartProfilingTimer()
	{
	}

	virtual void Execute(const FRigUnitContext& Context) override;
};

USTRUCT(meta = (DisplayName = "End Profiling Timer", Keywords = "Measure,StopProfiling,Meter,Profile"))
struct FRigUnit_EndProfilingTimer : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_EndProfilingTimer()
	{
		NumberOfMeasurements = 1;
		AccumulatedTime = 0.f;
		MeasurementsLeft = 0;
		Prefix = TEXT("Timer");
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input, Constant))
	int32 NumberOfMeasurements;

	UPROPERTY(meta = (Input, Constant))
	FString Prefix;

	UPROPERTY()
	float AccumulatedTime;

	UPROPERTY()
	int32 MeasurementsLeft;
};
