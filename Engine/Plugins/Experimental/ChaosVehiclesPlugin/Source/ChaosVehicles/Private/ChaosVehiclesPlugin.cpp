// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosVehiclesPlugin.h"

//#include "AssetToolsModule.h"
#include "CoreMinimal.h"
#include "ChaosVehicles.h"
#include "HAL/ConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "ChaosVehicleManager.h"

PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE( IChaosVehiclesPlugin, ChaosVehicles )


void IChaosVehiclesPlugin::PhysSceneInit(FPhysScene* PhysScene)
{
#if WITH_CHAOS
	new FChaosVehicleManager(PhysScene);
#endif
}

void IChaosVehiclesPlugin::PhysSceneTerm(FPhysScene* PhysScene)
{
#if WITH_CHAOS
	FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene);
	if (VehicleManager != nullptr)
	{
		VehicleManager->DetachFromPhysScene(PhysScene);
		delete VehicleManager;
		VehicleManager = nullptr;
	}
#endif // WITH_CHAOS
}




void IChaosVehiclesPlugin::StartupModule()
{
	OnPhysSceneInitHandle = FPhysicsDelegates::OnPhysSceneInit.AddRaw(this, &IChaosVehiclesPlugin::PhysSceneInit);
	OnPhysSceneTermHandle = FPhysicsDelegates::OnPhysSceneTerm.AddRaw(this, &IChaosVehiclesPlugin::PhysSceneTerm);
}


void IChaosVehiclesPlugin::ShutdownModule()
{
	FPhysicsDelegates::OnPhysSceneInit.Remove(OnPhysSceneInitHandle);
	FPhysicsDelegates::OnPhysSceneTerm.Remove(OnPhysSceneTermHandle);
}

PRAGMA_ENABLE_OPTIMIZATION
