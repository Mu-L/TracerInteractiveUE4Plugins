// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("Chaos"), STATGROUP_Chaos, STATCAT_Advanced);
DECLARE_STATS_GROUP_VERBOSE(TEXT("ChaosWide"), STATGROUP_ChaosWide, STATCAT_Advanced);
DECLARE_STATS_GROUP(TEXT("ChaosThread"), STATGROUP_ChaosThread, STATCAT_Advanced);
DECLARE_STATS_GROUP(TEXT("ChaosDedicated"), STATGROUP_ChaosDedicated, STATCAT_Advanced);
DECLARE_STATS_GROUP(TEXT("ChaosEngine"), STATGROUP_ChaosEngine, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Physics Advance"), STAT_PhysicsAdvance, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Solver Advance"), STAT_SolverAdvance, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Handle Solver Commands"), STAT_HandleSolverCommands, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Integrate Solver"), STAT_IntegrateSolver, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Sync Physics Proxies"), STAT_SyncProxies, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Handle Physics Commands"), STAT_PhysCommands, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Handle Task Commands"), STAT_TaskCommands, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Kinematic Particle Update"), STAT_KinematicUpdate, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Begin Frame"), STAT_BeginFrame, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("End Frame"), STAT_EndFrame, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Update Reverse Mapping"), STAT_UpdateReverseMapping, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Collision Data Generation"), STAT_CollisionContactsCallback, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Breaking Data Generation"), STAT_BreakingCallback, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Trailing Data Generation"), STAT_TrailingCallback, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Raycast"), STAT_GCRaycast, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Overlap"), STAT_GCOverlap, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Sweep"), STAT_GCSweep, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Component UpdateBounds"), STAT_GCCUpdateBounds, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Component CalculateGlobalMatrices"), STAT_GCCUGlobalMatrices, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection InitDynamicData"), STAT_GCInitDynamicData, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Physics Lock Waits"), STAT_LockWaits, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Geometry Collection Begin Frame"), STAT_GeomBeginFrame, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Skeletal Mesh Update Anim"), STAT_SkelMeshUpdateAnim, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Dispatch Event Notifies"), STAT_DispatchEventNotifies, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Dispatch Collision Events"), STAT_DispatchCollisionEvents, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Dispatch Break Events"), STAT_DispatchBreakEvents, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Cache Results"), STAT_CacheResults, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Flip Results"), STAT_FlipResults, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[CacheResults] - Geometry Collection"), STAT_CacheResultGeomCollection, STATGROUP_ChaosWide, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[CacheResults] - StaticMesh"), STAT_CacheResultStaticMesh, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Capture Disabled State"), STAT_CaptureDisabledState, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Calc Global Matrices"), STAT_CalcGlobalGCMatrices, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Calc Global Bounds"), STAT_CalcGlobalGCBounds, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Calc ParticleToWorld"), STAT_CalcParticleToWorld, STATGROUP_Chaos, CHAOS_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Create bodies"), STAT_CreateBodies, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Parameter Update"), STAT_UpdateParams, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Disable collisions"), STAT_DisableCollisions, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Evolution/Kinematic update and forces"), STAT_EvolutionAndKinematicUpdate, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("AdvanceOneTimestep Event Waits"), STAT_AdvanceEventWaits, STATGROUP_Chaos, CHAOS_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Reset Collision Rule"), STAT_ResetCollisionRule, STATGROUP_Chaos, CHAOS_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Object Parameter Update"), STAT_ParamUpdateObject, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Field Parameter Update"), STAT_ParamUpdateField, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Sync Events - Game Thread"), STAT_SyncEvents_GameThread, STATGROUP_Chaos, CHAOS_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Physics Thread Stat Update"), STAT_PhysicsStatUpdate, STATGROUP_ChaosThread, CHAOS_API);
DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT("Physics Thread Time Actual (ms)"), STAT_PhysicsThreadTime, STATGROUP_ChaosThread, CHAOS_API);
DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT("Physics Thread Time Effective (ms)"), STAT_PhysicsThreadTimeEff, STATGROUP_ChaosThread, CHAOS_API);
DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT("Physics Thread FPS Actual"), STAT_PhysicsThreadFps, STATGROUP_ChaosThread, CHAOS_API);
DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT("Physics Thread FPS Effective"), STAT_PhysicsThreadFpsEff, STATGROUP_ChaosThread, CHAOS_API);

// Interface / scene stats
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Scene] - StartFrame"), STAT_Scene_StartFrame, STATGROUP_ChaosEngine, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Scene] - EndFrame"), STAT_Scene_EndFrame, STATGROUP_ChaosEngine, CHAOS_API);

// Field update stats
DECLARE_CYCLE_STAT_EXTERN(TEXT("Field System Object Parameter Update"), STAT_ParamUpdateField_Object, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] DynamicState"), STAT_ParamUpdateField_DynamicState, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] ExternalClusterStrain"), STAT_ParamUpdateField_ExternalClusterStrain, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] Kill"), STAT_ParamUpdateField_Kill, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] LinearVelocity"), STAT_ParamUpdateField_LinearVelocity, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] AngularVelocity"), STAT_ParamUpdateField_AngularVelocity, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] SleepingThreshold"), STAT_ParamUpdateField_SleepingThreshold, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] DisableThreshold"), STAT_ParamUpdateField_DisableThreshold, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] InternalClusterStrain"), STAT_ParamUpdateField_InternalClusterStrain, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] PositionStatic"), STAT_ParamUpdateField_PositionStatic, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] PositionTarget"), STAT_ParamUpdateField_PositionTarget, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] PositionAnimated"), STAT_ParamUpdateField_PositionAnimated, STATGROUP_Chaos, CHAOS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("[Field Update] DynamicConstraint"), STAT_ParamUpdateField_DynamicConstraint, STATGROUP_Chaos, CHAOS_API);