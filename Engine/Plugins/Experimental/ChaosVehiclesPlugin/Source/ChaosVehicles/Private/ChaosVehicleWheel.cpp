// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVehicleWheel.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Vehicles/TireType.h"
#include "GameFramework/PawnMovementComponent.h"
#include "ChaosVehicleManager.h"
#include "ChaosWheeledVehicleMovementComponent.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif


UChaosVehicleWheel::UChaosVehicleWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CollisionMeshObj(TEXT("/Engine/EngineMeshes/Cylinder"));
	CollisionMesh = CollisionMeshObj.Object;

	WheelRadius = 32.0f;
	WheelWidth = 20.0f;
	//bAutoAdjustCollisionSize = true;
	//WheelMass = 20.0f;
	LongitudinalFrictionForceMultiplier = 1.0f;
	LateralFrictionForceMultiplier = 2.0f;
	SideSlipModifier = 1.0f;

	bAffectedByBrake = true;
	bAffectedByHandbrake = false;
	MaxSteerAngle = 50.0f;
	MaxBrakeTorque = 1500.f;
	MaxHandBrakeTorque = 3000.f;

	SpringRate = 250.0f;
	SpringPreload = 50.f;
	SuspensionAxis = FVector(0.f, 0.f, -1.f);
	SuspensionForceOffset = FVector::ZeroVector;
	SuspensionMaxRaise = 10.0f;
	SuspensionMaxDrop = 10.0f;
	SuspensionDampingRatio = 0.5f;
	SuspensionSmoothing = 6;
	WheelLoadRatio = 0.5f;
	RollbarScaling = 0.15f;
	SweepType = ESweepType::SimpleSweep;
}


FChaosVehicleManager* UChaosVehicleWheel::GetVehicleManager() const
{
	UWorld* World = GEngine->GetWorldFromContextObject(VehicleSim, EGetWorldErrorMode::LogAndReturnNull);
	return World ? FChaosVehicleManager::GetVehicleManagerFromScene(World->GetPhysicsScene()) : nullptr;
}


float UChaosVehicleWheel::GetSteerAngle() const
{
	check(VehicleSim && VehicleSim->PhysicsVehicle());
	return VehicleSim->PhysicsVehicle()->Wheels[WheelIndex].GetSteeringAngle();
}

float UChaosVehicleWheel::GetRotationAngle() const
{
	check(VehicleSim && VehicleSim->PhysicsVehicle());
	float RotationAngle = -1.0f * FMath::RadiansToDegrees(VehicleSim->PhysicsVehicle()->Wheels[WheelIndex].GetAngularPosition());
	ensure(!FMath::IsNaN(RotationAngle));

	return RotationAngle;
}

float UChaosVehicleWheel::GetSuspensionOffset() const
{
	check(VehicleSim && VehicleSim->PhysicsVehicle());
	Chaos::FSimpleSuspensionSim& SimSuspension = VehicleSim->PhysicsVehicle()->Suspension[WheelIndex];
	return SimSuspension.GetSuspensionOffset();
}

bool UChaosVehicleWheel::IsInAir() const
{
	check(VehicleSim && VehicleSim->PhysicsVehicle());
	return !VehicleSim->PhysicsVehicle()->Wheels[WheelIndex].InContact();
}


void UChaosVehicleWheel::Init( UChaosWheeledVehicleMovementComponent* InVehicleSim, int32 InWheelIndex )
{
	check(InVehicleSim);
	check(InVehicleSim->Wheels.IsValidIndex(InWheelIndex));

	VehicleSim = InVehicleSim;
	WheelIndex = InWheelIndex;

//#if WITH_PHYSX_VEHICLES
//	WheelShape = NULL;
//
//	FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(VehicleSim->GetWorld()->GetPhysicsScene());
//	SCOPED_SCENE_READ_LOCK(VehicleManager->GetScene());
//
//	const int32 WheelShapeIdx = VehicleSim->PVehicle->mWheelsSimData.getWheelShapeMapping( WheelIndex );
//	check(WheelShapeIdx >= 0);
//
//	VehicleSim->PVehicle->getRigidDynamicActor()->getShapes( &WheelShape, 1, WheelShapeIdx );
//	check(WheelShape);
//#endif // WITH_PHYSX

	Location = GetPhysicsLocation();
	OldLocation = Location;
}

void UChaosVehicleWheel::Shutdown()
{
//	WheelShape = NULL;
}

FChaosWheelSetup& UChaosVehicleWheel::GetWheelSetup()
{
	return VehicleSim->WheelSetups[WheelIndex];
}

void UChaosVehicleWheel::Tick( float DeltaTime )
{
	OldLocation = Location;
	Location = GetPhysicsLocation();
	Velocity = ( Location - OldLocation ) / DeltaTime;
}

FVector UChaosVehicleWheel::GetPhysicsLocation()
{
	return Location;
}

#if WITH_EDITOR

void UChaosVehicleWheel::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	// Trigger a runtime rebuild of the Physics vehicle
	FChaosVehicleManager::VehicleSetupTag++;
}

#endif //WITH_EDITOR


UPhysicalMaterial* UChaosVehicleWheel::GetContactSurfaceMaterial()
{
	UPhysicalMaterial* PhysMaterial = NULL;

	if (HitResult.bBlockingHit && HitResult.PhysMaterial.IsValid())
	{
		PhysMaterial = HitResult.PhysMaterial.Get();
	}

	return PhysMaterial;
}


#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif



