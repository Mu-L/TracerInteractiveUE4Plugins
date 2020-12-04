// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Hierarchy/RigUnit_SetCurveValue.h"
#include "Units/RigUnitContext.h"

FRigUnit_SetCurveValue_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();
	if (CurveContainer)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedCurveIndex.Reset();
				// fall through to update
			}
			case EControlRigState::Update:
			{
				if (CachedCurveIndex.UpdateCache(Curve, CurveContainer))
				{
					CurveContainer->SetValue(CachedCurveIndex, Value);
				}
			}
			default:
			{
				break;
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_SetCurveValue)
{
	CurveContainer.Add(TEXT("CurveA"));
	CurveContainer.Add(TEXT("CurveB"));
	CurveContainer.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;
	
	CurveContainer.ResetValues();
	Unit.Curve = TEXT("CurveA");
	Unit.Value = 3.0f;
	InitAndExecute();

	AddErrorIfFalse(CurveContainer.GetValue(FName(TEXT("CurveA"))) == 3.f, TEXT("unexpected value"));

	CurveContainer.ResetValues();
	Unit.Curve = TEXT("CurveB");
	Unit.Value = 13.0f;
	InitAndExecute();

	AddErrorIfFalse(CurveContainer.GetValue(FName(TEXT("CurveB"))) == 13.f, TEXT("unexpected value"));

	return true;
}
#endif