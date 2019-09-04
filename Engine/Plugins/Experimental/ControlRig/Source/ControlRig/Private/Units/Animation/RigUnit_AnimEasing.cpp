// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_AnimEasing.h"
#include "Units/RigUnitContext.h"

void FRigUnit_AnimEasing::Execute(const FRigUnitContext& Context)
{
	if (FMath::IsNearlyEqual(SourceMinimum, SourceMaximum))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum are the same."));
	}

	Result = FMath::Clamp<float>((Value - SourceMinimum) / (SourceMaximum - SourceMinimum), 0.f, 1.f);
	Result = FControlRigMathLibrary::EaseFloat(Result, Type);
	Result = FMath::Lerp<float>(TargetMinimum, TargetMaximum, Result);
}

