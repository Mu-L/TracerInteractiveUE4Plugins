// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraModifier_CameraShake.h"
#include "EngineGlobals.h"
#include "Camera/CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"

//////////////////////////////////////////////////////////////////////////
// UCameraModifier_CameraShake

DECLARE_CYCLE_STAT(TEXT("AddCameraShake"), STAT_AddCameraShake, STATGROUP_Game);

UCameraModifier_CameraShake::UCameraModifier_CameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SplitScreenShakeScale = 0.5f;
}


bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// Call super where modifier may be disabled
	Super::ModifyCamera(DeltaTime, InOutPOV);

	// If no alpha, exit early
	if( Alpha <= 0.f )
	{
		return false;
	}

	// Update and apply active shakes
	if( ActiveShakes.Num() > 0 )
	{
		for (UCameraShake* ShakeInst : ActiveShakes)
		{
			ShakeInst->UpdateAndApplyCameraShake(DeltaTime, Alpha, InOutPOV);
		}

		// Delete any obsolete shakes
		for (int32 i=ActiveShakes.Num()-1; i>=0; i--)
		{
			UCameraShake* const ShakeInst = ActiveShakes[i];
			if ((ShakeInst == nullptr) || ShakeInst->IsFinished())
			{
				ActiveShakes.RemoveAt(i, 1);

				SaveShakeInExpiredPool(ShakeInst);
			}
		}
	}

	// If ModifyCamera returns true, exit loop
	// Allows high priority things to dictate if they are
	// the last modifier to be applied
	// Returning true causes to stop adding another modifier! 
	// Returning false is the right behavior since this is not high priority modifier.
	return false;
}

UCameraShake* UCameraModifier_CameraShake::AddCameraShake(TSubclassOf<class UCameraShake> ShakeClass, float Scale, ECameraAnimPlaySpace::Type PlaySpace, FRotator UserPlaySpaceRot)
{
	SCOPE_CYCLE_COUNTER(STAT_AddCameraShake);

	if (ShakeClass != nullptr)
	{
		// adjust for splitscreen
		if (GEngine->IsSplitScreen(CameraOwner->GetWorld()))
		{
			Scale *= SplitScreenShakeScale;
		}

		UCameraShake const* const ShakeCDO = GetDefault<UCameraShake>(ShakeClass);
		if (ShakeCDO && ShakeCDO->bSingleInstance)
		{
			// look for existing instance of same class
			for (UCameraShake* ShakeInst : ActiveShakes)
			{
				if (ShakeInst && (ShakeClass == ShakeInst->GetClass()))
				{
					// just restart the existing shake
					ShakeInst->PlayShake(CameraOwner, Scale, PlaySpace, UserPlaySpaceRot);
					return ShakeInst;
				}
			}
		}

		// Try to find a shake in the expired pool
		UCameraShake* NewInst = ReclaimShakeFromExpiredPool(ShakeClass);

		// No old shakes, create a new one
		if (NewInst == nullptr)
		{
			NewInst = NewObject<UCameraShake>(this, ShakeClass);
		}

		if (NewInst)
		{
			// Initialize new shake and add it to the list of active shakes
			NewInst->PlayShake(CameraOwner, Scale, PlaySpace, UserPlaySpaceRot);

			// look for nulls in the array to replace first -- keeps the array compact
			bool bReplacedNull = false;
			for (int32 Idx = 0; Idx < ActiveShakes.Num(); ++Idx)
			{
				if (ActiveShakes[Idx] == nullptr)
				{
					ActiveShakes[Idx] = NewInst;
					bReplacedNull = true;
				}
			}

			// no holes, extend the array
			if (bReplacedNull == false)
			{
				ActiveShakes.Emplace(NewInst);
			}
		}

		return NewInst;
	}

	return nullptr;
}

void UCameraModifier_CameraShake::SaveShakeInExpiredPool(UCameraShake* ShakeInst)
{
	FPooledCameraShakes& PooledCameraShakes = ExpiredPooledShakesMap.FindOrAdd(ShakeInst->GetClass());
	if (PooledCameraShakes.PooledShakes.Num() < 5)
	{
		PooledCameraShakes.PooledShakes.Emplace(ShakeInst);
	}
}

UCameraShake* UCameraModifier_CameraShake::ReclaimShakeFromExpiredPool(TSubclassOf<UCameraShake> CameraShakeClass)
{
	if (FPooledCameraShakes* PooledCameraShakes = ExpiredPooledShakesMap.Find(CameraShakeClass))
	{
		if (PooledCameraShakes->PooledShakes.Num() > 0)
		{
			UCameraShake* OldShake = PooledCameraShakes->PooledShakes.Pop();
			// Calling new object with the exact same name will re-initialize the uobject in place
			OldShake = NewObject<UCameraShake>(this, CameraShakeClass, OldShake->GetFName());
			return OldShake;
		}
	}
	return nullptr;
}

void UCameraModifier_CameraShake::RemoveCameraShake(UCameraShake* ShakeInst, bool bImmediately)
{
	for (int32 i = 0; i < ActiveShakes.Num(); ++i)
	{
		if (ActiveShakes[i] == ShakeInst)
		{
			ShakeInst->StopShake(bImmediately);

			if (bImmediately)
			{
				ActiveShakes.RemoveAt(i, 1);
				SaveShakeInExpiredPool(ShakeInst);
			}
			break;
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesOfClass(TSubclassOf<class UCameraShake> ShakeClass, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num()-1; i >= 0; --i)
	{
		if ( ActiveShakes[i] && (ActiveShakes[i]->GetClass()->IsChildOf(ShakeClass)) )
		{
			ActiveShakes[i]->StopShake(bImmediately);
			if (bImmediately)
			{
				SaveShakeInExpiredPool(ActiveShakes[i]);
				ActiveShakes.RemoveAt(i, 1);
			}
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakes(bool bImmediately)
{
	// clean up any active camera shake anims
	for (UCameraShake* Inst : ActiveShakes)
	{
		Inst->StopShake(bImmediately);
	}

	if (bImmediately)
	{
		for (UCameraShake* Inst : ActiveShakes)
		{
			SaveShakeInExpiredPool(Inst);
		}

		// clear ActiveShakes array
		ActiveShakes.Empty();
	}
}
