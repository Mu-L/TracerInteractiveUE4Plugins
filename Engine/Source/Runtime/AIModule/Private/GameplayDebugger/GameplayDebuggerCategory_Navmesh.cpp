// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayDebugger/GameplayDebuggerCategory_Navmesh.h"

#if WITH_GAMEPLAY_DEBUGGER

#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"
#include "NavMesh/RecastNavMesh.h"

namespace
{
	int32 bDrawExcludedFlags = 0;
	FAutoConsoleVariableRef CVar(TEXT("ai.debug.nav.DrawExcludedFlags"), bDrawExcludedFlags, TEXT("If we want to mark \"forbidden\" nav polys while debug-drawing."), ECVF_Default);
}


FGameplayDebuggerCategory_Navmesh::FGameplayDebuggerCategory_Navmesh()
{
	bShowOnlyWithDebugActor = false;
	bShowCategoryName = false;
	bShowDataPackReplication = true;
	CollectDataInterval = 5.0f;
	SetDataPackReplication<FNavMeshSceneProxyData>(&NavmeshRenderData);
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_Navmesh::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_Navmesh());
}

void FGameplayDebuggerCategory_Navmesh::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
#if WITH_RECAST
	const ARecastNavMesh* NavData = nullptr;

	APawn* PlayerPawn = OwnerPC ? OwnerPC->GetPawnOrSpectator() : nullptr;
	APawn* DebugActorAsPawn = Cast<APawn>(DebugActor);
	APawn* DestPawn = DebugActorAsPawn ? DebugActorAsPawn : PlayerPawn;
	if (OwnerPC && DestPawn)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerPC->GetWorld());
		if (NavSys) 
		{
			const FNavAgentProperties& NavAgentProperties = DestPawn->GetNavAgentPropertiesRef();
			NavData = Cast<const ARecastNavMesh>(NavSys->GetNavDataForProps(NavAgentProperties));
		}
	}

	if (NavData)
	{
		// add 3x3 neighborhood of target
		const FVector TargetLocation = DestPawn->GetActorLocation();

		TArray<int32> TileSet;
		int32 TileX = 0;
		int32 TileY = 0;
		const int32 DeltaX[] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
		const int32 DeltaY[] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };

		int32 TargetTileX = 0;
		int32 TargetTileY = 0;
		NavData->GetNavMeshTileXY(TargetLocation, TargetTileX, TargetTileY);
		for (int32 Idx = 0; Idx < ARRAY_COUNT(DeltaX); Idx++)
		{
			const int32 NeiX = TargetTileX + DeltaX[Idx];
			const int32 NeiY = TargetTileY + DeltaY[Idx];
			NavData->GetNavMeshTilesAt(NeiX, NeiY, TileSet);
		}

		const int32 DetailFlags =
			(1 << static_cast<int32>(ENavMeshDetailFlags::PolyEdges)) |
			(1 << static_cast<int32>(ENavMeshDetailFlags::FilledPolys)) |
			(1 << static_cast<int32>(ENavMeshDetailFlags::NavLinks)) |
			(bDrawExcludedFlags ? (1 << static_cast<int32>(ENavMeshDetailFlags::MarkForbiddenPolys)) : 0);

		NavmeshRenderData.GatherData(NavData, DetailFlags, TileSet);
	}
#endif // WITH_RECAST
}

void FGameplayDebuggerCategory_Navmesh::OnDataPackReplicated(int32 DataPackId)
{
	MarkRenderStateDirty();
}

FDebugRenderSceneProxy* FGameplayDebuggerCategory_Navmesh::CreateDebugSceneProxy(const UPrimitiveComponent* InComponent, FDebugDrawDelegateHelper*& OutDelegateHelper)
{
	FNavMeshSceneProxy* NavMeshSceneProxy = new FNavMeshSceneProxy(InComponent, &NavmeshRenderData, true);

	auto* OutDelegateHelper2 = new FNavMeshDebugDrawDelegateHelper();
	OutDelegateHelper2->InitDelegateHelper(NavMeshSceneProxy);
	OutDelegateHelper = OutDelegateHelper2;

	return NavMeshSceneProxy;
}

#endif // WITH_GAMEPLAY_DEBUGGER
