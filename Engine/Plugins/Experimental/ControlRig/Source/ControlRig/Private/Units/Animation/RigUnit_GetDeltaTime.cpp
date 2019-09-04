// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_GetDeltaTime.h"
#include "Units/RigUnitContext.h"

void FRigUnit_GetDeltaTime::Execute(const FRigUnitContext& Context)
{
	Result = Context.DeltaTime;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetDeltaTime)
{
	Context.DeltaTime = 0.2f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 0.2f), TEXT("unexpected delta time"));
	return true;
}
#endif