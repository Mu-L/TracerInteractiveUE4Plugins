// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_ModifyBoneTransforms.h"
#include "Units/RigUnitContext.h"

FRigUnit_ModifyBoneTransforms_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	TArray<int32>& CachedBoneIndices = WorkData.CachedBoneIndices;

	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBoneIndices.Reset();
				for (const FRigUnit_ModifyBoneTransforms_PerBone& Entry : BoneToModify)
				{
					CachedBoneIndices.Add(Hierarchy->GetIndex(Entry.Bone));
				}
				return;
			}
			case EControlRigState::Update:
			{
				float Minimum = FMath::Min<float>(WeightMinimum, WeightMaximum);
				float Maximum = FMath::Max<float>(WeightMinimum, WeightMaximum);

				if (Weight <= Minimum + SMALL_NUMBER || FMath::IsNearlyEqual(Minimum, Maximum))
				{
					return;
				}

				if (CachedBoneIndices.Num() == BoneToModify.Num())
				{
					float T = FMath::Clamp<float>((Weight - Minimum) / (Maximum - Minimum), 0.f, 1.f);
					bool bNeedsBlend = T < 1.f - SMALL_NUMBER;

					int32 EntryIndex = 0;
					for (const FRigUnit_ModifyBoneTransforms_PerBone& Entry : BoneToModify)
					{
						int32 BoneIndex = CachedBoneIndices[EntryIndex];
						if (BoneIndex == INDEX_NONE)
						{
							continue;
						}

						FTransform Transform = Entry.Transform;

						switch (Mode)
						{
							case EControlRigModifyBoneMode::OverrideLocal:
							{
								if (bNeedsBlend)
								{
									Transform = FControlRigMathLibrary::LerpTransform(Hierarchy->GetLocalTransform(BoneIndex), Transform, T);
								}
								Hierarchy->SetLocalTransform(BoneIndex, Transform, true);
								break;
							}
							case EControlRigModifyBoneMode::OverrideGlobal:
							{
								if (bNeedsBlend)
								{
									Transform = FControlRigMathLibrary::LerpTransform(Hierarchy->GetGlobalTransform(BoneIndex), Transform, T);
								}
								Hierarchy->SetGlobalTransform(BoneIndex, Transform, true);
								break;
							}
							case EControlRigModifyBoneMode::AdditiveLocal:
							{
								if (bNeedsBlend)
								{
									Transform = FControlRigMathLibrary::LerpTransform(FTransform::Identity, Transform, T);
								}
								Transform = Transform * Hierarchy->GetLocalTransform(BoneIndex);
								Hierarchy->SetLocalTransform(BoneIndex, Transform, true);
								break;
							}
							case EControlRigModifyBoneMode::AdditiveGlobal:
							{
								if (bNeedsBlend)
								{
									Transform = FControlRigMathLibrary::LerpTransform(FTransform::Identity, Transform, T);
								}
								Transform = Hierarchy->GetGlobalTransform(BoneIndex) * Transform;
								Hierarchy->SetGlobalTransform(BoneIndex, Transform, true);
								break;
							}
							default:
							{
								break;
							}
						}
						EntryIndex++;
					}
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

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_ModifyBoneTransforms)
{
	BoneHierarchy.Add(TEXT("Root"), NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	BoneHierarchy.Add(TEXT("BoneA"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(1.f, 2.f, 3.f)));
	BoneHierarchy.Add(TEXT("BoneB"), TEXT("Root"), ERigBoneType::User, FTransform(FVector(5.f, 6.f, 7.f)));
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;

	Unit.BoneToModify.SetNumZeroed(2);
	Unit.BoneToModify[0].Bone = TEXT("BoneA");
	Unit.BoneToModify[1].Bone = TEXT("BoneB");
	Unit.BoneToModify[0].Transform = Unit.BoneToModify[1].Transform = FTransform(FVector(10.f, 11.f, 12.f));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveLocal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 13.f, 15.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(15.f, 17.f, 19.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveGlobal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 13.f, 15.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(15.f, 17.f, 19.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::OverrideLocal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(11.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(11.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::OverrideGlobal;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(10.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(10.f, 11.f, 12.f)).IsNearlyZero(), TEXT("unexpected transform"));

	BoneHierarchy.ResetTransforms();
	Unit.Mode = EControlRigModifyBoneMode::AdditiveLocal;
	Unit.Weight = 0.5f;
	InitAndExecute();
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(0).GetTranslation() - FVector(1.f, 0.f, 0.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(1).GetTranslation() - FVector(6.f, 7.5f, 9.f)).IsNearlyZero(), TEXT("unexpected transform"));
	AddErrorIfFalse((BoneHierarchy.GetGlobalTransform(2).GetTranslation() - FVector(10.f, 11.5f, 13.f)).IsNearlyZero(), TEXT("unexpected transform"));


	return true;
}
#endif