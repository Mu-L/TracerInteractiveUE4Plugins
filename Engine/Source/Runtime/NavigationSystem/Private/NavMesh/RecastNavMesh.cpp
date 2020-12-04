// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMesh/RecastNavMesh.h"
#include "Misc/Paths.h"
#include "EngineGlobals.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineUtils.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastVersion.h"
#include "NavAreas/NavArea.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_LowHeight.h"
#include "NavLinkCustomInterface.h"
#include "NavMesh/RecastNavMeshDataChunk.h"
#include "NavMesh/RecastQueryFilter.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

#if WITH_RECAST
#include "Detour/DetourAlloc.h"
#endif // WITH_RECAST

#include "NavMesh/NavMeshRenderingComponent.h"

#if WITH_RECAST
/// Helper for accessing navigation query from different threads
#define INITIALIZE_NAVQUERY(NavQueryVariable, NumNodes)	\
	dtNavMeshQuery NavQueryVariable##Private;	\
	dtNavMeshQuery& NavQueryVariable = IsInGameThread() ? RecastNavMeshImpl->SharedNavQuery : NavQueryVariable##Private; \
	NavQueryVariable.init(RecastNavMeshImpl->DetourNavMesh, NumNodes);

#define INITIALIZE_NAVQUERY_WLINKFILTER(NavQueryVariable, NumNodes, LinkFilter)	\
	dtNavMeshQuery NavQueryVariable##Private;	\
	dtNavMeshQuery& NavQueryVariable = IsInGameThread() ? RecastNavMeshImpl->SharedNavQuery : NavQueryVariable##Private; \
	NavQueryVariable.init(RecastNavMeshImpl->DetourNavMesh, NumNodes, &LinkFilter);

#endif // WITH_RECAST

namespace 
{
	// Max tile size in voxels. Larger than this tiles will start to get slow to build.
	const int32 ArbitraryMaxTileSizeVoxels = 1024;
	// Min tile size on voxels. Smaller tiles than this waste computation during voxelization because the border are will be larger than usable area.
	const int32 ArbitraryMinTileSizeVoxels = 16;
	// Minimum tile size in multiples of agent radius.
	const int32 ArbitraryMinTileSizeAgentRadius = 4; 

	/** this helper function supplies a consistent way to keep TileSizeUU within defined bounds */
	float GetClampedTileSizeUU(const float InTileSizeUU, const float CellSize, const float AgentRadius)
	{
		const float MinTileSize = FMath::Max3(RECAST_MIN_TILE_SIZE, CellSize * ArbitraryMinTileSizeVoxels, AgentRadius * ArbitraryMinTileSizeAgentRadius);
		const float MaxTileSize = FMath::Max(RECAST_MIN_TILE_SIZE, CellSize * ArbitraryMaxTileSizeVoxels);
		
		return FMath::Clamp<float>(InTileSizeUU, MinTileSize, MaxTileSize);
	}
}

FNavMeshTileData::FNavData::~FNavData()
{
#if WITH_RECAST
	dtFree(RawNavData);
#else
	FMemory::Free(RawNavData);
#endif // WITH_RECAST
}

FNavMeshTileData::FNavMeshTileData(uint8* RawData, int32 RawDataSize, int32 LayerIdx, FBox LayerBounds)
	: LayerIndex(LayerIdx)
	, LayerBBox(LayerBounds)
	, DataSize(RawDataSize)
{
	INC_MEMORY_STAT_BY(STAT_Navigation_TileCacheMemory, DataSize);
	NavData = MakeShareable(new FNavData(RawData));
}

FNavMeshTileData::~FNavMeshTileData()
{
	if (NavData.IsUnique() && NavData->RawNavData)
	{
		DEC_MEMORY_STAT_BY(STAT_Navigation_TileCacheMemory, DataSize);
	}
}

uint8* FNavMeshTileData::Release()
{
	uint8* RawData = nullptr;

	if (NavData.IsValid() && NavData->RawNavData) 
	{ 
		RawData = NavData->RawNavData;
		NavData->RawNavData = nullptr;
		DEC_MEMORY_STAT_BY(STAT_Navigation_TileCacheMemory, DataSize);
	} 
 
	DataSize = 0; 
	LayerIndex = 0; 
	return RawData;
}

void FNavMeshTileData::MakeUnique()
{
	if (DataSize > 0 && !NavData.IsUnique())
	{
		INC_MEMORY_STAT_BY(STAT_Navigation_TileCacheMemory, DataSize);
#if WITH_RECAST
		uint8* UniqueRawData = (uint8*)dtAlloc(sizeof(uint8)*DataSize, DT_ALLOC_PERM);
#else
		uint8* UniqueRawData = (uint8*)FMemory::Malloc(sizeof(uint8)*DataSize);
#endif //WITH_RECAST
		FMemory::Memcpy(UniqueRawData, NavData->RawNavData, DataSize);
		NavData = MakeShareable(new FNavData(UniqueRawData));
	}
}

float ARecastNavMesh::DrawDistanceSq = 0.0f;
float ARecastNavMesh::MinimumSizeForChaosNavMeshInfluenceSq = 0.0f;
#if !WITH_RECAST

ARecastNavMesh::ARecastNavMesh(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void ARecastNavMesh::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);

	uint32 NavMeshVersion;
	Ar << NavMeshVersion;

	//@todo: How to handle loading nav meshes saved w/ recast when recast isn't present????

	// when writing, write a zero here for now.  will come back and fill it in later.
	uint32 RecastNavMeshSizeBytes = 0;
	int64 RecastNavMeshSizePos = Ar.Tell();
	Ar << RecastNavMeshSizeBytes;

	if (Ar.IsLoading())
	{
		// incompatible, just skip over this data.  navmesh needs rebuilt.
		Ar.Seek( RecastNavMeshSizePos + RecastNavMeshSizeBytes );

		// Mark self for delete
		CleanUpAndMarkPendingKill();
	}
}

#else // WITH_RECAST

#include "Detour/DetourNavMesh.h"
#include "Detour/DetourNavMeshQuery.h"
#include "NavMesh/PImplRecastNavMesh.h"
#include "NavMesh/RecastNavMeshGenerator.h"

//----------------------------------------------------------------------//
// FRecastDebugGeometry
//----------------------------------------------------------------------//
uint32 FRecastDebugGeometry::GetAllocatedSize() const
{
	uint32 Size = sizeof(*this) + MeshVerts.GetAllocatedSize()
		+ BuiltMeshIndices.GetAllocatedSize()
		+ PolyEdges.GetAllocatedSize()
		+ NavMeshEdges.GetAllocatedSize()
		+ OffMeshLinks.GetAllocatedSize()
#if WITH_NAVMESH_SEGMENT_LINKS
		+ OffMeshSegments.GetAllocatedSize()
#endif // WITH_NAVMESH_SEGMENT_LINKS
		;

	for (int i = 0; i < RECAST_MAX_AREAS; ++i)
	{
		Size += AreaIndices[i].GetAllocatedSize();
	}

#if WITH_NAVMESH_CLUSTER_LINKS
	Size += Clusters.GetAllocatedSize()	+ ClusterLinks.GetAllocatedSize();

	for (int i = 0; i < Clusters.Num(); ++i)
	{
		Size += Clusters[i].MeshIndices.GetAllocatedSize();
	}
#endif // WITH_NAVMESH_CLUSTER_LINKS

	return Size;
}

//----------------------------------------------------------------------//
// ARecastNavMesh
//----------------------------------------------------------------------//

namespace ERecastNamedFilter
{
	FRecastQueryFilter FilterOutNavLinksImpl;
	FRecastQueryFilter FilterOutAreasImpl;
	FRecastQueryFilter FilterOutNavLinksAndAreasImpl;
}

const FRecastQueryFilter* ARecastNavMesh::NamedFilters[] = {
	&ERecastNamedFilter::FilterOutNavLinksImpl
	, &ERecastNamedFilter::FilterOutAreasImpl
	, &ERecastNamedFilter::FilterOutNavLinksAndAreasImpl
};

namespace FNavMeshConfig
{
	ARecastNavMesh::FNavPolyFlags NavLinkFlag = ARecastNavMesh::FNavPolyFlags(0);

	FRecastNamedFiltersCreator::FRecastNamedFiltersCreator(bool bVirtualFilters)
	{
		// setting up the last bit available in dtPoly::flags
		NavLinkFlag = ARecastNavMesh::FNavPolyFlags(1 << (sizeof(((dtPoly*)0)->flags) * 8 - 1));

		ERecastNamedFilter::FilterOutNavLinksImpl.SetIsVirtual(bVirtualFilters);
		ERecastNamedFilter::FilterOutAreasImpl.SetIsVirtual(bVirtualFilters);
		ERecastNamedFilter::FilterOutNavLinksAndAreasImpl.SetIsVirtual(bVirtualFilters);

		ERecastNamedFilter::FilterOutNavLinksImpl.setExcludeFlags(NavLinkFlag);
		ERecastNamedFilter::FilterOutNavLinksAndAreasImpl.setExcludeFlags(NavLinkFlag);

		for (int32 AreaID = 0; AreaID < RECAST_MAX_AREAS; ++AreaID)
		{
			ERecastNamedFilter::FilterOutAreasImpl.setAreaCost(AreaID, RECAST_UNWALKABLE_POLY_COST);
			ERecastNamedFilter::FilterOutNavLinksAndAreasImpl.setAreaCost(AreaID, RECAST_UNWALKABLE_POLY_COST);
		}

		ERecastNamedFilter::FilterOutAreasImpl.setAreaCost(RECAST_DEFAULT_AREA, 1.f);
		ERecastNamedFilter::FilterOutNavLinksAndAreasImpl.setAreaCost(RECAST_DEFAULT_AREA, 1.f);
	}
}

FRecastNavMeshGenerationProperties::FRecastNavMeshGenerationProperties()
{
	TilePoolSize = 1024;
	TileSizeUU = 988.f;
	CellSize = 19;
	CellHeight = 10;
	AgentRadius = 34.f;
	AgentHeight = 144.f;
	AgentMaxSlope = 44.f;
	AgentMaxStepHeight = 35.f;
	MinRegionArea = 0.f;
	MergeRegionSize = 400.f;
	MaxSimplificationError = 1.3f;	// from RecastDemo
	TileNumberHardLimit = 1 << 20;
	RegionPartitioning = ERecastPartitioning::Watershed;
	LayerPartitioning = ERecastPartitioning::Watershed;
	RegionChunkSplits = 2;
	LayerChunkSplits = 2;
	bSortNavigationAreasByCost = false;
	bPerformVoxelFiltering = true;
	bMarkLowHeightAreas = false;
	bUseExtraTopCellWhenMarkingAreas = true;
	bFilterLowSpanSequences = false;
	bFilterLowSpanFromTileCache = false;
	bFixedTilePoolSize = false;
}

FRecastNavMeshGenerationProperties::FRecastNavMeshGenerationProperties(const ARecastNavMesh& RecastNavMesh)
{
	TilePoolSize = RecastNavMesh.TilePoolSize;
	TileSizeUU = RecastNavMesh.TileSizeUU;
	CellSize = RecastNavMesh.CellSize;
	CellHeight = RecastNavMesh.CellHeight;
	AgentRadius = RecastNavMesh.AgentRadius;
	AgentHeight = RecastNavMesh.AgentHeight;
	AgentMaxSlope = RecastNavMesh.AgentMaxSlope;
	AgentMaxStepHeight = RecastNavMesh.AgentMaxStepHeight;
	MinRegionArea = RecastNavMesh.MinRegionArea;
	MergeRegionSize = RecastNavMesh.MergeRegionSize;
	MaxSimplificationError = RecastNavMesh.MaxSimplificationError;
	TileNumberHardLimit = RecastNavMesh.TileNumberHardLimit;
	RegionPartitioning = RecastNavMesh.RegionPartitioning;
	LayerPartitioning = RecastNavMesh.LayerPartitioning;
	RegionChunkSplits = RecastNavMesh.RegionChunkSplits;
	LayerChunkSplits = RecastNavMesh.LayerChunkSplits;
	bSortNavigationAreasByCost = RecastNavMesh.bSortNavigationAreasByCost;
	bPerformVoxelFiltering = RecastNavMesh.bPerformVoxelFiltering;
	bMarkLowHeightAreas = RecastNavMesh.bMarkLowHeightAreas;
	bUseExtraTopCellWhenMarkingAreas = RecastNavMesh.bUseExtraTopCellWhenMarkingAreas;
	bFilterLowSpanSequences = RecastNavMesh.bFilterLowSpanSequences;
	bFilterLowSpanFromTileCache = RecastNavMesh.bFilterLowSpanFromTileCache;
	bFixedTilePoolSize = RecastNavMesh.bFixedTilePoolSize;
}

ARecastNavMesh::FNavPolyFlags ARecastNavMesh::NavLinkFlag = ARecastNavMesh::FNavPolyFlags(0);

ARecastNavMesh::ARecastNavMesh(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bDrawFilledPolys(true)
	, bDrawNavMeshEdges(true)
	, bDrawNavLinks(true)
	, bDrawOctreeDetails(true)
	, bDrawMarkedForbiddenPolys(false)
	, bDistinctlyDrawTilesBeingBuilt(true)
	, DrawOffset(10.f)
	, TilePoolSize(1024)
	, MaxSimplificationError(1.3f)	// from RecastDemo
	, DefaultMaxSearchNodes(RECAST_MAX_SEARCH_NODES)
	, DefaultMaxHierarchicalSearchNodes(RECAST_MAX_SEARCH_NODES)
	, bPerformVoxelFiltering(true)	
	, bMarkLowHeightAreas(false)
	, bUseExtraTopCellWhenMarkingAreas(true)
	, bFilterLowSpanSequences(false)
	, bFilterLowSpanFromTileCache(false)
	, bStoreEmptyTileLayers(false)
	, bUseVirtualFilters(true)
	, bAllowNavLinkAsPathEnd(false)
	, TileSetUpdateInterval(1.0f)
	, NavMeshVersion(NAVMESHVER_LATEST)	
	, RecastNavMeshImpl(NULL)
{
	HeuristicScale = 0.999f;
	RegionPartitioning = ERecastPartitioning::Watershed;
	LayerPartitioning = ERecastPartitioning::Watershed;
	RegionChunkSplits = 2;
	LayerChunkSplits = 2;
	MaxSimultaneousTileGenerationJobsCount = 1024;
	bDoFullyAsyncNavDataGathering = false;
	TileNumberHardLimit = 1 << 20;

#if RECAST_ASYNC_REBUILDING
	BatchQueryCounter = 0;
#endif // RECAST_ASYNC_REBUILDING


	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		INC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );

		FindPathImplementation = FindPath;
		FindHierarchicalPathImplementation = FindPath;

		TestPathImplementation = TestPath;
		TestHierarchicalPathImplementation = TestHierarchicalPath;

		RaycastImplementation = NavMeshRaycast;

		RecastNavMeshImpl = new FPImplRecastNavMesh(this);
	
		// add predefined areas up front
		SupportedAreas.Add(FSupportedAreaData(UNavArea_Null::StaticClass(), RECAST_NULL_AREA));
		SupportedAreas.Add(FSupportedAreaData(UNavArea_LowHeight::StaticClass(), RECAST_LOW_AREA));
		SupportedAreas.Add(FSupportedAreaData(UNavArea_Default::StaticClass(), RECAST_DEFAULT_AREA));
	}
}

ARecastNavMesh::~ARecastNavMesh()
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		DEC_DWORD_STAT_BY( STAT_NavigationMemory, sizeof(*this) );
		DestroyRecastPImpl();
	}
}

void ARecastNavMesh::DestroyRecastPImpl()
{
	if (RecastNavMeshImpl != NULL)
	{
		delete RecastNavMeshImpl;
		RecastNavMeshImpl = NULL;
	}
}

UPrimitiveComponent* ARecastNavMesh::ConstructRenderingComponent() 
{
	return NewObject<UNavMeshRenderingComponent>(this, TEXT("NavRenderingComp"), RF_Transient);
}

void ARecastNavMesh::UpdateNavMeshDrawing()
{
#if !UE_BUILD_SHIPPING
	UNavMeshRenderingComponent* NavMeshRenderComp = Cast<UNavMeshRenderingComponent>(RenderingComp);
	if (NavMeshRenderComp != nullptr && NavMeshRenderComp->GetVisibleFlag() && (NavMeshRenderComp->IsForcingUpdate() || UNavMeshRenderingComponent::IsNavigationShowFlagSet(GetWorld())))
	{
		RenderingComp->MarkRenderStateDirty();
	}
#endif // UE_BUILD_SHIPPING
}

void ARecastNavMesh::CleanUp()
{
	Super::CleanUp();
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}
	DestroyRecastPImpl();
}

void ARecastNavMesh::PostLoad()
{
	Super::PostLoad();

	UE_CLOG(TileSizeUU < CellSize, LogNavigation, Error, TEXT("%s: TileSizeUU (%f) being less than CellSize (%f) is an invalid case and will cause navmesh generation issues.")
		, *GetName(), TileSizeUU, CellSize);

	RecreateDefaultFilter();
	UpdatePolyRefBitsPreview();
}

void ARecastNavMesh::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (GetActorLocation().IsNearlyZero() == false)
	{
		ApplyWorldOffset(GetActorLocation(), /*unused*/false);
	}
}

void ARecastNavMesh::PostInitProperties()
{
	if (HasAnyFlags(RF_ClassDefaultObject) == true)
	{
		SetDrawDistance(DefaultDrawDistance);

		static const FNavMeshConfig::FRecastNamedFiltersCreator RecastNamedFiltersCreator(bUseVirtualFilters);
		NavLinkFlag = FNavMeshConfig::NavLinkFlag;
	}

	UWorld* MyWorld = GetWorld();
	if (MyWorld != nullptr 
		&& HasAnyFlags(RF_NeedLoad) //  was loaded
		&& FNavigationSystem::ShouldDiscardSubLevelNavData(*this))
	{
		// get rid of instances saved within levels that are streamed-in
		if ((GEngine->IsSettingUpPlayWorld() == false) // this is a @HACK
			&&	(MyWorld->GetOutermost() != GetOutermost())
			// If we are cooking, then let them all pass.
			// They will be handled at load-time when running.
			&&	(IsRunningCommandlet() == false))
		{
			UE_LOG(LogNavigation, Log, TEXT("Discarding %s due to it not being part of PersistentLevel")
				, *GetNameSafe(this));
			
			// marking self for deletion 
			CleanUpAndMarkPendingKill();
		}
	}
	
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		RecreateDefaultFilter();
	}

	// voxel cache requires the same rasterization setup for all navmeshes, as it's stored in octree
	if (IsVoxelCacheEnabled() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ARecastNavMesh* DefOb = (ARecastNavMesh*)ARecastNavMesh::StaticClass()->GetDefaultObject();

		if (TileSizeUU != DefOb->TileSizeUU)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s param: TileSizeUU(%f) differs from config settings, forcing value %f so it can be used with voxel cache!"),
				*GetNameSafe(this), TileSizeUU, DefOb->TileSizeUU);
			
			TileSizeUU = DefOb->TileSizeUU;
		}

		if (CellSize != DefOb->CellSize)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s param: CellSize(%f) differs from config settings, forcing value %f so it can be used with voxel cache!"),
				*GetNameSafe(this), CellSize, DefOb->CellSize);

			CellSize = DefOb->CellSize;
		}

		if (CellHeight != DefOb->CellHeight)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s param: CellHeight(%f) differs from config settings, forcing value %f so it can be used with voxel cache!"),
				*GetNameSafe(this), CellHeight, DefOb->CellHeight);

			CellHeight = DefOb->CellHeight;
		}

		if (AgentMaxSlope != DefOb->AgentMaxSlope)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s param: AgentMaxSlope(%f) differs from config settings, forcing value %f so it can be used with voxel cache!"),
				*GetNameSafe(this), AgentMaxSlope, DefOb->AgentMaxSlope);

			AgentMaxSlope = DefOb->AgentMaxSlope;
		}

		if (AgentMaxStepHeight != DefOb->AgentMaxStepHeight)
		{
			UE_LOG(LogNavigation, Warning, TEXT("%s param: AgentMaxStepHeight(%f) differs from config settings, forcing value %f so it can be used with voxel cache!"),
				*GetNameSafe(this), AgentMaxStepHeight, DefOb->AgentMaxStepHeight);

			AgentMaxStepHeight = DefOb->AgentMaxStepHeight;
		}
	}

	UpdatePolyRefBitsPreview();
}

void ARecastNavMesh::RecreateDefaultFilter()
{
	DefaultQueryFilter->SetFilterType<FRecastQueryFilter>();
	DefaultQueryFilter->SetMaxSearchNodes(DefaultMaxSearchNodes);

	FRecastQueryFilter* DetourFilter = static_cast<FRecastQueryFilter*>(DefaultQueryFilter->GetImplementation());
	DetourFilter->SetIsVirtual(bUseVirtualFilters);
	DetourFilter->setHeuristicScale(HeuristicScale);
	// clearing out the 'navlink flag' from included flags since it would make 
	// dtQueryFilter::passInlineFilter pass navlinks of area classes with
	// AreaFlags == 0 (like NavArea_Null), which should mean 'unwalkable'
	DetourFilter->setIncludeFlags(DetourFilter->getIncludeFlags() & (~ARecastNavMesh::GetNavLinkFlag()));

	for (int32 Idx = 0; Idx < SupportedAreas.Num(); Idx++)
	{
		const FSupportedAreaData& AreaData = SupportedAreas[Idx];
		
		UNavArea* DefArea = nullptr;
		if (AreaData.AreaClass)
		{
			DefArea = ((UClass*)AreaData.AreaClass)->GetDefaultObject<UNavArea>();
		}

		if (DefArea)
		{
			DetourFilter->SetAreaCost(AreaData.AreaID, DefArea->DefaultCost);
			DetourFilter->SetFixedAreaEnteringCost(AreaData.AreaID, DefArea->GetFixedAreaEnteringCost());
		}
	}
}

void ARecastNavMesh::UpdatePolyRefBitsPreview()
{
	static const int32 TotalBits = (sizeof(dtPolyRef) * 8);

	FRecastNavMeshGenerator::CalcPolyRefBits(this, PolyRefTileBits, PolyRefNavPolyBits);
	PolyRefSaltBits = TotalBits - PolyRefTileBits - PolyRefNavPolyBits;
}

void ARecastNavMesh::OnNavAreaAdded(const UClass* NavAreaClass, int32 AgentIndex)
{
	Super::OnNavAreaAdded(NavAreaClass, AgentIndex);

	// update navmesh query filter with area costs
	const int32 AreaID = GetAreaID(NavAreaClass);
	if (AreaID != INDEX_NONE)
	{
		UNavArea* DefArea = ((UClass*)NavAreaClass)->GetDefaultObject<UNavArea>();

		DefaultQueryFilter->SetAreaCost(AreaID, DefArea->DefaultCost);
		DefaultQueryFilter->SetFixedAreaEnteringCost(AreaID, DefArea->GetFixedAreaEnteringCost());
	}

	// update generator's cached data
	FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
	if (MyGenerator)
	{
		MyGenerator->OnAreaAdded(NavAreaClass, AreaID);
	}
}

void ARecastNavMesh::OnNavAreaChanged()
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->OnAreaCostChanged();
	}
}

int32 ARecastNavMesh::GetNewAreaID(const UClass* AreaClass) const
{
	if (AreaClass == FNavigationSystem::GetDefaultWalkableArea())
	{
		return RECAST_DEFAULT_AREA;
	}

	if (AreaClass == UNavArea_Null::StaticClass())
	{
		return RECAST_NULL_AREA;
	}

	if (AreaClass == UNavArea_LowHeight::StaticClass())
	{
		return RECAST_LOW_AREA;
	}

	int32 FreeAreaID = Super::GetNewAreaID(AreaClass);
	while (FreeAreaID == RECAST_NULL_AREA || FreeAreaID == RECAST_DEFAULT_AREA || FreeAreaID == RECAST_LOW_AREA)
	{
		FreeAreaID++;
	}

	check(FreeAreaID < GetMaxSupportedAreas());
	return FreeAreaID;
}

FColor ARecastNavMesh::GetAreaIDColor(uint8 AreaID) const
{
	const UClass* AreaClass = GetAreaClass(AreaID);
	const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
	return DefArea ? DefArea->DrawColor : FColor::Red;
}

void ARecastNavMesh::SortAreasForGenerator(TArray<FRecastAreaNavModifierElement>& Modifiers) const
{
	// initialize costs for sorting
	float AreaCosts[RECAST_MAX_AREAS];
	float AreaFixedCosts[RECAST_MAX_AREAS];
	DefaultQueryFilter->GetAllAreaCosts(AreaCosts, AreaFixedCosts, RECAST_MAX_AREAS);

	for (auto& Element : Modifiers)
	{
		if (Element.Areas.Num())
		{
			FAreaNavModifier& AreaMod = Element.Areas[0];
			const int32 AreaId = GetAreaID(AreaMod.GetAreaClass());
			if (AreaId >= 0 && AreaId < RECAST_MAX_AREAS)
			{
				AreaMod.Cost = AreaCosts[AreaId];
				AreaMod.FixedCost = AreaFixedCosts[AreaId];
			}
		}
	}

	struct FNavAreaSortPredicate
	{
		FORCEINLINE bool operator()(const FRecastAreaNavModifierElement& ElA, const FRecastAreaNavModifierElement& ElB) const
		{
			if (ElA.Areas.Num() == 0 || ElB.Areas.Num() == 0)
			{
				return ElA.Areas.Num() <= ElB.Areas.Num();
			}

			// assuming composite modifiers has same area type
			const FAreaNavModifier& A = ElA.Areas[0];
			const FAreaNavModifier& B = ElB.Areas[0];
			
			const bool bIsAReplacing = (A.GetAreaClassToReplace() != NULL);
			const bool bIsBReplacing = (B.GetAreaClassToReplace() != NULL);
			if (bIsAReplacing != bIsBReplacing)
			{
				return bIsAReplacing;
			}

			return A.Cost != B.Cost ? A.Cost < B.Cost : A.FixedCost < B.FixedCost;
		}
	};

	Modifiers.Sort(FNavAreaSortPredicate());
}

TArray<FIntPoint>& ARecastNavMesh::GetActiveTiles()
{
	FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
	check(MyGenerator);
	return MyGenerator->ActiveTiles;
}

void ARecastNavMesh::RestrictBuildingToActiveTiles(bool InRestrictBuildingToActiveTiles)
{
	FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
	if (MyGenerator)
	{
		MyGenerator->RestrictBuildingToActiveTiles(InRestrictBuildingToActiveTiles);
	}
}

void ARecastNavMesh::SerializeRecastNavMesh(FArchive& Ar, FPImplRecastNavMesh*& NavMesh, int32 InNavMeshVersion)
{
	if (!Ar.IsLoading()	&& NavMesh == NULL)
	{
		return;
	}

	if (Ar.IsLoading())
	{
		// allocate if necessary
		if (RecastNavMeshImpl == NULL)
		{
			RecastNavMeshImpl = new FPImplRecastNavMesh(this);
		}
	}
	
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->Serialize(Ar, InNavMeshVersion);
	}	
}

void ARecastNavMesh::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);

	Ar << NavMeshVersion;

	//@todo: How to handle loading nav meshes saved w/ recast when recast isn't present????
	
	// when writing, write a zero here for now.  will come back and fill it in later.
	uint32 RecastNavMeshSizeBytes = 0;
	int64 RecastNavMeshSizePos = Ar.Tell();
	{
#if WITH_EDITOR
		FArchive::FScopeSetDebugSerializationFlags S(Ar, DSF_IgnoreDiff);
#endif
		Ar << RecastNavMeshSizeBytes;
	}
	if (Ar.IsLoading())
	{
		if (NavMeshVersion < NAVMESHVER_MIN_COMPATIBLE)
		{
			// incompatible, just skip over this data.  navmesh needs rebuilt.
			Ar.Seek( RecastNavMeshSizePos + RecastNavMeshSizeBytes );

			// Mark self for delete
			CleanUpAndMarkPendingKill();
		}
		else if (RecastNavMeshSizeBytes > 4)
		{
			SerializeRecastNavMesh(Ar, RecastNavMeshImpl, NavMeshVersion);
#if !(UE_BUILD_SHIPPING)
			RequestDrawingUpdate();
#endif //!(UE_BUILD_SHIPPING)
		}
		else
		{
			// empty, just skip over this data
			Ar.Seek( RecastNavMeshSizePos + RecastNavMeshSizeBytes );
			// if it's not getting filled it's better to just remove it
			if (RecastNavMeshImpl)
			{
				RecastNavMeshImpl->ReleaseDetourNavMesh();
			}
		}
	}
	else
	{
		SerializeRecastNavMesh(Ar, RecastNavMeshImpl, NavMeshVersion);

		if (Ar.IsSaving())
		{
			int64 CurPos = Ar.Tell();
			RecastNavMeshSizeBytes = CurPos - RecastNavMeshSizePos;
			Ar.Seek(RecastNavMeshSizePos);
			Ar << RecastNavMeshSizeBytes;
			Ar.Seek(CurPos);
		}
	}
}

#if WITH_EDITOR
bool ARecastNavMesh::CanEditChange(const FProperty* InProperty) const
{
#if !WITH_NAVMESH_CLUSTER_LINKS
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ARecastNavMesh, bDrawClusters))
		{
			return false;
		}
	}
#endif // WITH_NAVMESH_CLUSTER_LINKS

	return Super::CanEditChange(InProperty);
}
#endif // WITH_EDITOR

void ARecastNavMesh::SetConfig(const FNavDataConfig& Src) 
{ 
	NavDataConfig = Src; 
	AgentHeight = Src.AgentHeight;
	AgentRadius = Src.AgentRadius;

	if (Src.HasStepHeightOverride())
	{
		AgentMaxStepHeight = Src.AgentStepHeight;
	}
}

void ARecastNavMesh::FillConfig(FNavDataConfig& Dest)
{
	Dest = NavDataConfig;
	Dest.AgentHeight = AgentHeight;
	Dest.AgentRadius = AgentRadius;
	Dest.AgentStepHeight = AgentMaxStepHeight;
}

void ARecastNavMesh::BeginBatchQuery() const
{
#if RECAST_ASYNC_REBUILDING
	// lock critical section when no other batch queries are active
	if (BatchQueryCounter <= 0)
	{
		BatchQueryCounter = 0;
	}

	BatchQueryCounter++;
#endif // RECAST_ASYNC_REBUILDING
}

void ARecastNavMesh::FinishBatchQuery() const
{
#if RECAST_ASYNC_REBUILDING
	BatchQueryCounter--;
#endif // RECAST_ASYNC_REBUILDING
}

FBox ARecastNavMesh::GetNavMeshBounds() const
{
	FBox Bounds;
	if (RecastNavMeshImpl)
	{
		Bounds = RecastNavMeshImpl->GetNavMeshBounds();
	}

	return Bounds;
}

FBox ARecastNavMesh::GetNavMeshTileBounds(int32 TileIndex) const
{
	FBox Bounds;
	if (RecastNavMeshImpl)
	{
		Bounds = RecastNavMeshImpl->GetNavMeshTileBounds(TileIndex);
	}

	return Bounds;
}

bool ARecastNavMesh::GetNavMeshTileXY(int32 TileIndex, int32& OutX, int32& OutY, int32& OutLayer) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetNavMeshTileXY(TileIndex, OutX, OutY, OutLayer);
}

bool ARecastNavMesh::GetNavMeshTileXY(const FVector& Point, int32& OutX, int32& OutY) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetNavMeshTileXY(Point, OutX, OutY);
}

void ARecastNavMesh::GetNavMeshTilesAt(int32 TileX, int32 TileY, TArray<int32>& Indices) const
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->GetNavMeshTilesAt(TileX, TileY, Indices);
	}
}

bool ARecastNavMesh::GetPolysInTile(int32 TileIndex, TArray<FNavPoly>& Polys) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolysInTile(TileIndex, Polys);
}

bool ARecastNavMesh::GetNavLinksInTile(const int32 TileIndex, TArray<FNavPoly>& Polys, const bool bIncludeLinksFromNeighborTiles) const
{
	if (RecastNavMeshImpl == nullptr || RecastNavMeshImpl->DetourNavMesh == nullptr
		|| TileIndex < 0 || TileIndex >= RecastNavMeshImpl->DetourNavMesh->getMaxTiles())
	{
		return false;
	}

	const dtNavMesh* DetourNavMesh = RecastNavMeshImpl->DetourNavMesh;
	const int32 InitialLinkCount = Polys.Num();

	const dtMeshTile* Tile = DetourNavMesh->getTile(TileIndex);
	if (Tile && Tile->header)
	{
		const int32 LinkCount = Tile->header->offMeshConCount;

		if (LinkCount > 0)
		{
			const int32 BaseIdx = Polys.Num();
			Polys.AddZeroed(LinkCount);

			const dtPoly* Poly = Tile->polys;
			for (int32 LinkIndex = 0; LinkIndex < LinkCount; ++LinkIndex, ++Poly)
			{
				FNavPoly& OutPoly = Polys[BaseIdx + LinkIndex];
				const int32 PolyIndex = Tile->header->offMeshBase + LinkIndex;
				OutPoly.Ref = DetourNavMesh->encodePolyId(Tile->salt, TileIndex, PolyIndex);
				OutPoly.Center = (Recast2UnrealPoint(&Tile->verts[Poly->verts[0] * 3]) + Recast2UnrealPoint(&Tile->verts[Poly->verts[1] * 3])) / 2;
			}
		}

		if (bIncludeLinksFromNeighborTiles)
		{
			TArray<const dtMeshTile*> NeighborTiles;
			NeighborTiles.Reserve(32);
			for (int32 SideIndex = 0; SideIndex < 8; ++SideIndex)
			{
				const int32 StartIndex = NeighborTiles.Num();
				const int32 NeighborCount = DetourNavMesh->getNeighbourTilesCountAt(Tile->header->x, Tile->header->y, SideIndex);
				if (NeighborCount > 0)
				{
					const unsigned char oppositeSide = (unsigned char)dtOppositeTile(SideIndex);

					NeighborTiles.AddZeroed(NeighborCount);
					int32 NeighborX = Tile->header->x;
					int32 NeighborY = Tile->header->y;

					if (DetourNavMesh->getNeighbourCoords(Tile->header->x, Tile->header->y, SideIndex, NeighborX, NeighborY))
					{
						DetourNavMesh->getTilesAt(NeighborX, NeighborY, NeighborTiles.GetData() + StartIndex, NeighborCount);
					}

					for (const dtMeshTile* NeighborTile : NeighborTiles)
					{
						if (NeighborTile && NeighborTile->header && NeighborTile->offMeshCons)
						{
							const dtTileRef NeighborTileId = DetourNavMesh->getTileRef(NeighborTile);

							for (int32 LinkIndex = 0; LinkIndex < NeighborTile->header->offMeshConCount; ++LinkIndex)
							{
								dtOffMeshConnection* targetCon = &NeighborTile->offMeshCons[LinkIndex];
								if (targetCon->side != oppositeSide)
								{
									continue;
								}

								const unsigned char biDirFlag = targetCon->getBiDirectional() ? DT_LINK_FLAG_OFFMESH_CON_BIDIR : 0;

								const dtPoly* targetPoly = &NeighborTile->polys[targetCon->poly];
								// Skip off-mesh connections which start location could not be connected at all.
								if (targetPoly->firstLink == DT_NULL_LINK)
								{
									continue;
								}

								FNavPoly& OutPoly = Polys[Polys.AddZeroed()];
								OutPoly.Ref = NeighborTileId | targetCon->poly;
								OutPoly.Center = (Recast2UnrealPoint(&targetCon->pos[0]) + Recast2UnrealPoint(&targetCon->pos[3])) / 2;
							}
						}
					}

					NeighborTiles.Reset();
				}
			}
		}
	}

	return (Polys.Num() - InitialLinkCount > 0);
}

int32 ARecastNavMesh::GetNavMeshTilesCount() const
{
	int32 NumTiles = 0;
	if (RecastNavMeshImpl)
	{
		NumTiles = RecastNavMeshImpl->GetNavMeshTilesCount();
	}

	return NumTiles;
}

void ARecastNavMesh::RemoveTileCacheLayers(int32 TileX, int32 TileY)
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->RemoveTileCacheLayers(TileX, TileY);
	}
}
	
void ARecastNavMesh::AddTileCacheLayers(int32 TileX, int32 TileY, const TArray<FNavMeshTileData>& InLayers)
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->AddTileCacheLayers(TileX, TileY, InLayers);
	}
}

#if RECAST_INTERNAL_DEBUG_DATA
void ARecastNavMesh::RemoveTileDebugData(int32 TileX, int32 TileY)
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->DebugDataMap.Remove(FIntPoint(TileX, TileY));
	}
}

void ARecastNavMesh::AddTileDebugData(int32 TileX, int32 TileY, const FRecastInternalDebugData& InTileDebugData)
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->DebugDataMap.Add(FIntPoint(TileX, TileY), InTileDebugData);
	}
}
#endif //RECAST_INTERNAL_DEBUG_DATA

void ARecastNavMesh::MarkEmptyTileCacheLayers(int32 TileX, int32 TileY)
{
	if (RecastNavMeshImpl && bStoreEmptyTileLayers)
	{
		RecastNavMeshImpl->MarkEmptyTileCacheLayers(TileX, TileY);
	}
}
	
TArray<FNavMeshTileData> ARecastNavMesh::GetTileCacheLayers(int32 TileX, int32 TileY) const
{
	if (RecastNavMeshImpl)
	{
		return RecastNavMeshImpl->GetTileCacheLayers(TileX, TileY);
	}
	
	return TArray<FNavMeshTileData>();
}

#if !UE_BUILD_SHIPPING
int32 ARecastNavMesh::GetCompressedTileCacheSize()
{
	return RecastNavMeshImpl ? RecastNavMeshImpl->GetCompressedTileCacheSize() : 0;
}
#endif

bool ARecastNavMesh::IsResizable() const
{
	return !bFixedTilePoolSize;
}

void ARecastNavMesh::GetEdgesForPathCorridor(const TArray<NavNodeRef>* PathCorridor, TArray<FNavigationPortalEdge>* PathCorridorEdges) const
{
	check(PathCorridor != NULL && PathCorridorEdges != NULL);

	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->GetEdgesForPathCorridor(PathCorridor, PathCorridorEdges);
	}
}

FNavLocation ARecastNavMesh::GetRandomPoint(FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	FNavLocation RandomPt;
	if (RecastNavMeshImpl)
	{
		RandomPt = RecastNavMeshImpl->GetRandomPoint(GetRightFilterRef(Filter), QueryOwner);
	}

	return RandomPt;
}

bool ARecastNavMesh::GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	if (RecastNavMeshImpl == nullptr || RecastNavMeshImpl->DetourNavMesh == nullptr || Radius <= 0.f)
	{
		return false;
	}

	const FNavigationQueryFilter& FilterInstance = GetRightFilterRef(Filter);

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()), QueryOwner);
	INITIALIZE_NAVQUERY_WLINKFILTER(NavQuery, FilterInstance.GetMaxSearchNodes(), LinkFilter);

	// inits to "pass all"
	const dtQueryFilter* QueryFilter = (static_cast<const FRecastQueryFilter*>(FilterInstance.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		// find starting poly
		const FVector ProjectionExtent(NavDataConfig.DefaultQueryExtent.X, NavDataConfig.DefaultQueryExtent.Y, BIG_NUMBER);
		const FVector RcExtent = Unreal2RecastPoint(ProjectionExtent).GetAbs();
		// convert start/end pos to Recast coords
		const FVector RecastOrigin = Unreal2RecastPoint(Origin);
		NavNodeRef OriginPolyID = INVALID_NAVNODEREF;
		NavQuery.findNearestPoly(&RecastOrigin.X, &RcExtent.X, QueryFilter, &OriginPolyID, nullptr);

		if (OriginPolyID != INVALID_NAVNODEREF)
		{
			dtPolyRef Poly;
			float RandPt[3];
			dtStatus Status = NavQuery.findRandomPointAroundCircle(OriginPolyID, &RecastOrigin.X, Radius
				, QueryFilter, FMath::FRand, &Poly, RandPt);

			if (dtStatusSucceed(Status))
			{
				OutResult = FNavLocation(Recast2UnrealPoint(RandPt), Poly);
				return true;
			}
		}

		OutResult = FNavLocation(Origin, OriginPolyID);
	}

	return false;
}

bool ARecastNavMesh::GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	const FVector ProjectionExtent(NavDataConfig.DefaultQueryExtent.X, NavDataConfig.DefaultQueryExtent.Y, BIG_NUMBER);
	OutResult = FNavLocation(FNavigationSystem::InvalidLocation);

	const float RandomAngle = 2.f * PI * FMath::FRand();
	const float U = FMath::FRand() + FMath::FRand();
	const float RandomRadius = Radius * (U > 1 ? 2.f - U : U);
	const FVector RandomOffset(FMath::Cos(RandomAngle) * RandomRadius, FMath::Sin(RandomAngle) * RandomRadius, 0);
	FVector RandomLocationInRadius = Origin + RandomOffset;

	// naive implementation 
	ProjectPoint(RandomLocationInRadius, OutResult, ProjectionExtent, Filter);

	// if failed get a list of all nav polys in the area and do it the hard way
	if (OutResult.HasNodeRef() == false && RecastNavMeshImpl)
	{
		const float RadiusSq = FMath::Square(Radius);
		TArray<FNavPoly> Polys;
		const FVector FallbackExtent(Radius, Radius, HALF_WORLD_MAX); //Using HALF_WORLD_MAX instead of BIG_NUMBER, else the box size will be NaN.
		const FVector BoxOrigin(Origin.X, Origin.Y, 0.f);
		const FBox Box(BoxOrigin - FallbackExtent, BoxOrigin + FallbackExtent);
		GetPolysInBox(Box, Polys, Filter, Querier);
	
		// @todo extremely naive implementation, barely random. To be improved
		while (Polys.Num() > 0)
		{
			const int32 RandomIndex = FMath::RandHelper(Polys.Num());
			const FNavPoly& Poly = Polys[RandomIndex];

			FVector PointOnPoly(0);
			if (RecastNavMeshImpl->GetClosestPointOnPoly(Poly.Ref, Origin, PointOnPoly)
				&& FVector::DistSquared(PointOnPoly, Origin) < RadiusSq)
			{
				OutResult = FNavLocation(PointOnPoly, Poly.Ref);
				break;
			}

			Polys.RemoveAtSwap(RandomIndex, 1, /*bAllowShrinking=*/false);
		}
	}

	return OutResult.HasNodeRef() == true;
}

#if WITH_NAVMESH_CLUSTER_LINKS
bool ARecastNavMesh::GetRandomPointInCluster(NavNodeRef ClusterRef, FNavLocation& OutLocation) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetRandomPointInCluster(ClusterRef, OutLocation);
}

NavNodeRef ARecastNavMesh::GetClusterRef(NavNodeRef PolyRef) const
{
	NavNodeRef ClusterRef = 0;
	if (RecastNavMeshImpl)
	{
		ClusterRef = RecastNavMeshImpl->GetClusterRefFromPolyRef(PolyRef);
	}

	return ClusterRef;
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

bool ARecastNavMesh::FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	bool bSuccess = false;
	if (RecastNavMeshImpl)
	{
		bSuccess = RecastNavMeshImpl->FindMoveAlongSurface(StartLocation, TargetPosition, OutLocation, GetRightFilterRef(Filter), QueryOwner);
	}

	return bSuccess;
}

bool ARecastNavMesh::ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& Extent, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	bool bSuccess = false;
	if (RecastNavMeshImpl)
	{
		bSuccess = RecastNavMeshImpl->ProjectPointToNavMesh(Point, OutLocation, Extent, GetRightFilterRef(Filter), QueryOwner);
	}

	return bSuccess;
}

bool ARecastNavMesh::IsNodeRefValid(NavNodeRef NodeRef) const
{
	if (NodeRef == INVALID_NAVNODEREF)
	{
		return false;
	}
	const dtNavMesh* NavMesh = RecastNavMeshImpl ? RecastNavMeshImpl->GetRecastMesh() : nullptr;
	if (!NavMesh)
	{
		return false;
	}
	dtPoly const* Poly = 0;
	dtMeshTile const* Tile = 0;
	const dtStatus Status = NavMesh->getTileAndPolyByRef(NodeRef, &Tile, &Poly);
	const bool bSuccess = dtStatusSucceed(Status);
	return bSuccess;
}

void ARecastNavMesh::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter Filter, const UObject* Querier) const 
{
	if (Workload.Num() == 0 || RecastNavMeshImpl == NULL || RecastNavMeshImpl->DetourNavMesh == NULL)
	{
		return;
	}
	
	const FNavigationQueryFilter& FilterToUse = GetRightFilterRef(Filter);

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()), Querier);
	INITIALIZE_NAVQUERY_WLINKFILTER(NavQuery, FilterToUse.GetMaxSearchNodes(), LinkFilter);
	const dtQueryFilter* QueryFilter = static_cast<const FRecastQueryFilter*>(FilterToUse.GetImplementation())->GetAsDetourQueryFilter();
	
	if (ensure(QueryFilter))
	{
		const FVector ModifiedExtent = GetModifiedQueryExtent(Extent);
		FVector RcExtent = Unreal2RecastPoint(ModifiedExtent).GetAbs();
		float ClosestPoint[3];
		dtPolyRef PolyRef;

		for (int32 Idx = 0; Idx < Workload.Num(); Idx++)
		{
			FVector RcPoint = Unreal2RecastPoint(Workload[Idx].Point);
			if (Workload[Idx].bHintProjection2D)
			{
				NavQuery.findNearestPoly2D(&RcPoint.X, &RcExtent.X, QueryFilter, &PolyRef, ClosestPoint);
			}
			else
			{
				NavQuery.findNearestPoly(&RcPoint.X, &RcExtent.X, QueryFilter, &PolyRef, ClosestPoint);
			}

			// one last step required due to recast's BVTree imprecision
			if (PolyRef > 0)
			{
				const FVector& UnrealClosestPoint = Recast2UnrealPoint(ClosestPoint);
				if (FVector::DistSquared(UnrealClosestPoint, Workload[Idx].Point) <= ModifiedExtent.SizeSquared())
				{
					Workload[Idx].OutLocation = FNavLocation(UnrealClosestPoint, PolyRef);
					Workload[Idx].bResult = true;
				}
			}
		}
	}
}

void ARecastNavMesh::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	if (Workload.Num() == 0 || RecastNavMeshImpl == NULL || RecastNavMeshImpl->DetourNavMesh == NULL)
	{
		return;
	}

	const FNavigationQueryFilter& FilterToUse = GetRightFilterRef(Filter);

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()), Querier);
	INITIALIZE_NAVQUERY_WLINKFILTER(NavQuery, FilterToUse.GetMaxSearchNodes(), LinkFilter);
	const dtQueryFilter* QueryFilter = static_cast<const FRecastQueryFilter*>(FilterToUse.GetImplementation())->GetAsDetourQueryFilter();

	if (ensure(QueryFilter))
	{
		float ClosestPoint[3];
		dtPolyRef PolyRef;

		for (FNavigationProjectionWork& Work : Workload)
		{
			ensure(Work.ProjectionLimit.IsValid);
			const FVector RcReferencePoint = Unreal2RecastPoint(Work.Point);
			const FVector ModifiedExtent = GetModifiedQueryExtent(Work.ProjectionLimit.GetExtent());
			const FVector RcExtent = Unreal2RecastPoint(ModifiedExtent).GetAbs();
			const FVector RcBoxCenter = Unreal2RecastPoint(Work.ProjectionLimit.GetCenter());

			if (Work.bHintProjection2D)
			{
				NavQuery.findNearestPoly2D(&RcBoxCenter.X, &RcExtent.X, QueryFilter, &PolyRef, ClosestPoint, &RcReferencePoint.X);
			}
			else
			{
				NavQuery.findNearestPoly(&RcBoxCenter.X, &RcExtent.X, QueryFilter, &PolyRef, ClosestPoint, &RcReferencePoint.X);
			}

			// one last step required due to recast's BVTree imprecision
			if (PolyRef > 0)
			{
				const FVector& UnrealClosestPoint = Recast2UnrealPoint(ClosestPoint);
				if (FVector::DistSquared(UnrealClosestPoint, Work.Point) <= ModifiedExtent.SizeSquared())
				{
					Work.OutLocation = FNavLocation(UnrealClosestPoint, PolyRef);
					Work.bResult = true;
				}
			}
		}
	}
}

bool ARecastNavMesh::GetPolysInBox(const FBox& Box, TArray<FNavPoly>& Polys, FSharedConstNavQueryFilter Filter, const UObject* InOwner) const
{
	// sanity check
	if (RecastNavMeshImpl->GetRecastMesh() == NULL)
	{
		return false;
	}

	bool bSuccess = false;

	const FNavigationQueryFilter& FilterToUse = GetRightFilterRef(Filter);
	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()), InOwner);
	INITIALIZE_NAVQUERY_WLINKFILTER(NavQuery, FilterToUse.GetMaxSearchNodes(), LinkFilter);

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(FilterToUse.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);
	if (QueryFilter)
	{
		const FVector ModifiedExtent = GetModifiedQueryExtent(Box.GetExtent());

		const FVector RcPoint = Unreal2RecastPoint( Box.GetCenter() );
		const FVector RcExtent = Unreal2RecastPoint( ModifiedExtent ).GetAbs();

		const int32 MaxHitPolys = 256;
		dtPolyRef HitPolys[MaxHitPolys];
		int32 NumHitPolys = 0;

		dtStatus status = NavQuery.queryPolygons(&RcPoint.X, &RcExtent.X, QueryFilter, HitPolys, &NumHitPolys, MaxHitPolys);
		if (dtStatusSucceed(status))
		{
			// only ground type polys
			int32 BaseIdx = Polys.Num();
			Polys.AddZeroed(NumHitPolys);

			for (int32 i = 0; i < NumHitPolys; i++)
			{
				dtPoly const* Poly;
				dtMeshTile const* Tile;
				dtStatus Status = RecastNavMeshImpl->GetRecastMesh()->getTileAndPolyByRef(HitPolys[i], &Tile, &Poly);
				if (dtStatusSucceed(Status))
				{
					FVector PolyCenter(0);
					for (int k = 0; k < Poly->vertCount; ++k)
					{
						PolyCenter += Recast2UnrealPoint(&Tile->verts[Poly->verts[k]*3]);
					}
					PolyCenter /= Poly->vertCount;

					FNavPoly& OutPoly = Polys[BaseIdx + i];
					OutPoly.Ref = HitPolys[i];
					OutPoly.Center = PolyCenter;
				}
			}

			bSuccess = true;
		}
	}

	return bSuccess;
}

bool ARecastNavMesh::ProjectPointMulti(const FVector& Point, TArray<FNavLocation>& OutLocations, const FVector& Extent,
	float MinZ, float MaxZ, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->ProjectPointMulti(Point, OutLocations, Extent, MinZ, MaxZ, GetRightFilterRef(Filter), QueryOwner);
}

ENavigationQueryResult::Type ARecastNavMesh::CalcPathCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter, const UObject* QueryOwner) const
{
	float TmpPathLength = 0.f;
	ENavigationQueryResult::Type Result = CalcPathLengthAndCost(PathStart, PathEnd, TmpPathLength, OutPathCost, QueryFilter, QueryOwner);
	return Result;
}

ENavigationQueryResult::Type ARecastNavMesh::CalcPathLength(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, FSharedConstNavQueryFilter QueryFilter, const UObject* QueryOwner) const
{
	float TmpPathCost = 0.f;
	ENavigationQueryResult::Type Result = CalcPathLengthAndCost(PathStart, PathEnd, OutPathLength, TmpPathCost, QueryFilter, QueryOwner);
	return Result;
}

ENavigationQueryResult::Type ARecastNavMesh::CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter, const UObject* QueryOwner) const
{
	ENavigationQueryResult::Type Result = ENavigationQueryResult::Invalid;

	if (RecastNavMeshImpl)
	{
		if ((PathStart - PathEnd).IsNearlyZero() == true)
		{
			OutPathLength = 0.f;
			Result = ENavigationQueryResult::Success;
		}
		else
		{
			TSharedRef<FNavMeshPath> Path = MakeShareable(new FNavMeshPath());
			Path->SetWantsStringPulling(false);
			Path->SetWantsPathCorridor(true);
			
			const float CostLimit = FLT_MAX;
			Result = RecastNavMeshImpl->FindPath(PathStart, PathEnd, CostLimit, Path.Get(), GetRightFilterRef(QueryFilter), QueryOwner);

			if (Result == ENavigationQueryResult::Success || (Result == ENavigationQueryResult::Fail && Path->IsPartial()))
			{
				OutPathLength = Path->GetTotalPathLength();
				OutPathCost = Path->GetCost();
			}
		}
	}

	return Result;
}

bool ARecastNavMesh::DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const
{
	bool bResult = false;
	if (RecastNavMeshImpl != nullptr && RecastNavMeshImpl->GetRecastMesh() != nullptr)
	{
		dtNavMeshQuery NavQuery;
		NavQuery.init(RecastNavMeshImpl->GetRecastMesh(), 0);

		const FVector RcLocation = Unreal2RecastPoint(WorldSpaceLocation);
		if (dtStatusFailed(NavQuery.isPointInsidePoly(NodeRef, &RcLocation.X, bResult)))
		{
			bResult = false;
		}
	}

	return bResult; 
}

NavNodeRef ARecastNavMesh::FindNearestPoly(FVector const& Loc, FVector const& Extent, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	NavNodeRef PolyRef = 0;
	if (RecastNavMeshImpl)
	{
		PolyRef = RecastNavMeshImpl->FindNearestPoly(Loc, Extent, GetRightFilterRef(Filter), QueryOwner);
	}

	return PolyRef;
}

float ARecastNavMesh::FindDistanceToWall(const FVector& StartLoc, FSharedConstNavQueryFilter Filter, float MaxDistance, FVector* OutClosestPointOnWall) const
{
	if (HasValidNavmesh() == false)
	{
		return 0.f;
	}

	const FNavigationQueryFilter& FilterToUse = GetRightFilterRef(Filter);

	INITIALIZE_NAVQUERY(NavQuery, FilterToUse.GetMaxSearchNodes());
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(FilterToUse.GetImplementation()))->GetAsDetourQueryFilter();

	if (QueryFilter == nullptr)
	{
		UE_VLOG(this, LogNavigation, Warning, TEXT("ARecastNavMesh::FindDistanceToWall failing due to QueryFilter == NULL"));
		return 0.f;
	}

	const FVector NavExtent = GetModifiedQueryExtent(GetDefaultQueryExtent());
	const float Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	const FVector RecastStart = Unreal2RecastPoint(StartLoc);

	NavNodeRef StartNode = INVALID_NAVNODEREF;
	NavQuery.findNearestPoly(&RecastStart.X, Extent, QueryFilter, &StartNode, NULL);

	if (StartNode != INVALID_NAVNODEREF)
	{
		float TmpHitPos[3], TmpHitNormal[3];
		float DistanceToWall = 0.f;
		const dtStatus RaycastStatus = NavQuery.findDistanceToWall(StartNode, &RecastStart.X, MaxDistance, QueryFilter
			, &DistanceToWall, TmpHitPos, TmpHitNormal);

		if (dtStatusSucceed(RaycastStatus))
		{
			if (OutClosestPointOnWall)
			{
				*OutClosestPointOnWall = Recast2UnrealPoint(TmpHitPos);
			}
			return DistanceToWall;
		}
	}

	return 0.f;
}

void ARecastNavMesh::UpdateCustomLink(const INavLinkCustomInterface* CustomLink)
{
	TSubclassOf<UNavArea> AreaClass = CustomLink->GetLinkAreaClass();
	const int32 UserId = CustomLink->GetLinkId();
	const int32 AreaId = GetAreaID(AreaClass);
	if (AreaId >= 0 && RecastNavMeshImpl)
	{
		UNavArea* DefArea = (UNavArea*)(AreaClass->GetDefaultObject());
		const uint16 PolyFlags = DefArea->GetAreaFlags() | ARecastNavMesh::GetNavLinkFlag();

		RecastNavMeshImpl->UpdateNavigationLinkArea(UserId, AreaId, PolyFlags);
#if WITH_NAVMESH_SEGMENT_LINKS
		RecastNavMeshImpl->UpdateSegmentLinkArea(UserId, AreaId, PolyFlags);
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if !UE_BUILD_SHIPPING
		RequestDrawingUpdate(false);
#endif
	}
}

void ARecastNavMesh::UpdateNavigationLinkArea(int32 UserId, TSubclassOf<UNavArea> AreaClass) const
{
	int32 AreaId = GetAreaID(AreaClass);
	if (AreaId >= 0 && RecastNavMeshImpl)
	{
		UNavArea* DefArea = (UNavArea*)(AreaClass->GetDefaultObject());
		const uint16 PolyFlags = DefArea->GetAreaFlags() | ARecastNavMesh::GetNavLinkFlag();

		RecastNavMeshImpl->UpdateNavigationLinkArea(UserId, AreaId, PolyFlags);
	}
}

#if WITH_NAVMESH_SEGMENT_LINKS
void ARecastNavMesh::UpdateSegmentLinkArea(int32 UserId, TSubclassOf<UNavArea> AreaClass) const
{
	int32 AreaId = GetAreaID(AreaClass);
	if (AreaId >= 0 && RecastNavMeshImpl)
	{
		UNavArea* DefArea = (UNavArea*)(AreaClass->GetDefaultObject());
		const uint16 PolyFlags = DefArea->GetAreaFlags() | ARecastNavMesh::GetNavLinkFlag();

		RecastNavMeshImpl->UpdateSegmentLinkArea(UserId, AreaId, PolyFlags);
	}
}
#endif // WITH_NAVMESH_SEGMENT_LINKS

bool ARecastNavMesh::GetPolyCenter(NavNodeRef PolyID, FVector& OutCenter) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolyCenter(PolyID, OutCenter);
}

bool ARecastNavMesh::GetPolyVerts(NavNodeRef PolyID, TArray<FVector>& OutVerts) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolyVerts(PolyID, OutVerts);
}

uint32 ARecastNavMesh::GetPolyAreaID(NavNodeRef PolyID) const
{
	uint32 AreaID = RECAST_DEFAULT_AREA;
	if (RecastNavMeshImpl)
	{
		AreaID = RecastNavMeshImpl->GetPolyAreaID(PolyID);
	}

	return AreaID;
}

bool ARecastNavMesh::SetPolyArea(NavNodeRef PolyID, TSubclassOf<UNavArea> AreaClass)
{
	bool bSuccess = false;
	if (AreaClass && RecastNavMeshImpl)
	{
		dtNavMesh* NavMesh = RecastNavMeshImpl->GetRecastMesh();
		const int32 AreaId = GetAreaID(AreaClass);
		const uint16 AreaFlags = AreaClass->GetDefaultObject<UNavArea>()->GetAreaFlags();
		
		if (AreaId != INDEX_NONE && NavMesh)
		{
			// @todo implement a single detour function that would do both
			bSuccess = dtStatusSucceed(NavMesh->setPolyArea(PolyID, AreaId));
			bSuccess = (bSuccess && dtStatusSucceed(NavMesh->setPolyFlags(PolyID, AreaFlags)));
		}
	}
	return bSuccess;
}

void ARecastNavMesh::SetPolyArrayArea(const TArray<FNavPoly>& Polys, TSubclassOf<UNavArea> AreaClass)
{
	if (AreaClass && RecastNavMeshImpl)
	{
		dtNavMesh* NavMesh = RecastNavMeshImpl->GetRecastMesh();
		const int32 AreaId = GetAreaID(AreaClass);
		const uint16 AreaFlags = AreaClass->GetDefaultObject<UNavArea>()->GetAreaFlags();

		if (AreaId != INDEX_NONE && NavMesh)
		{
			for (int32 Idx = 0; Idx < Polys.Num(); Idx++)
			{
				NavMesh->setPolyArea(Polys[Idx].Ref, AreaId);
				NavMesh->setPolyFlags(Polys[Idx].Ref, AreaFlags);
			}
		}
	}
}

int32 ARecastNavMesh::ReplaceAreaInTileBounds(const FBox& Bounds, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool ReplaceLinks, TArray<NavNodeRef>* OutTouchedNodes)
{
	int32 PolysTouched = 0;

	if (RecastNavMeshImpl && RecastNavMeshImpl->GetRecastMesh())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMesh_ReplaceAreaInTiles);

		const int32 OldAreaID = GetAreaID(OldArea);
		ensure(OldAreaID != INDEX_NONE);
		const int32 NewAreaID = GetAreaID(NewArea);
		ensure(NewAreaID != INDEX_NONE);
		ensure(NewAreaID != OldAreaID);

		// workaround for privacy issue in the recast API
		dtNavMesh* DetourNavMesh = RecastNavMeshImpl->GetRecastMesh();
		dtNavMesh const* const ConstDetourNavMesh = RecastNavMeshImpl->GetRecastMesh();

		const FVector RcNavMeshOrigin = Unreal2RecastPoint(NavMeshOriginOffset);
		const float RcTileSize = FMath::TruncToInt(TileSizeUU / CellSize);
		const float TileSizeInWorldUnits = RcTileSize * CellSize;
		const FRcTileBox TileBox(Bounds, RcNavMeshOrigin, TileSizeInWorldUnits);

		for (int32 TileY = TileBox.YMin; TileY <= TileBox.YMax; ++TileY)
		{
			for (int32 TileX = TileBox.XMin; TileX <= TileBox.XMax; ++TileX)
			{
				const int32 MaxTiles = ConstDetourNavMesh->getTileCountAt(TileX, TileY);
				if (MaxTiles == 0)
				{
					continue;
				}

				TArray<const dtMeshTile*> Tiles;
				Tiles.AddZeroed(MaxTiles);
				const int32 NumTiles = ConstDetourNavMesh->getTilesAt(TileX, TileY, Tiles.GetData(), MaxTiles);
				for (int32 i = 0; i < NumTiles; i++)
				{
					dtTileRef TileRef = ConstDetourNavMesh->getTileRef(Tiles[i]);
					if (TileRef)
					{
						const int32 TileIndex = (int32)ConstDetourNavMesh->decodePolyIdTile(TileRef);
						const dtMeshTile* Tile = ((const dtNavMesh*)DetourNavMesh)->getTile(TileIndex);
						//const int32 MaxPolys = Tile && Tile->header ? Tile->header->offMeshBase : 0;
						const int32 MaxPolys = Tile && Tile->header
							? (ReplaceLinks ? Tile->header->polyCount : Tile->header->offMeshBase)
							: 0;
						if (MaxPolys > 0)
						{
							dtPoly* Poly = Tile->polys;
							for (int32 PolyIndex = 0; PolyIndex < MaxPolys; PolyIndex++, Poly++)
							{
								if (Poly->getArea() == OldAreaID)
								{
									Poly->setArea(NewAreaID);
									++PolysTouched;
								}
							}
						}
					}
				}
			}
		}
	}
	return PolysTouched;
}

bool ARecastNavMesh::GetPolyFlags(NavNodeRef PolyID, uint16& PolyFlags, uint16& AreaFlags) const
{
	bool bFound = false;
	if (RecastNavMeshImpl)
	{
		uint8 AreaType = RECAST_DEFAULT_AREA;
		bFound = RecastNavMeshImpl->GetPolyData(PolyID, PolyFlags, AreaType);
		if (bFound)
		{
			const UClass* AreaClass = GetAreaClass(AreaType);
			const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
			AreaFlags = DefArea ? DefArea->GetAreaFlags() : 0;
		}
	}

	return bFound;
}

bool ARecastNavMesh::GetPolyFlags(NavNodeRef PolyID, FNavMeshNodeFlags& Flags) const
{
	bool bFound = false;
	if (RecastNavMeshImpl)
	{
		uint16 PolyFlags = 0;

		bFound = RecastNavMeshImpl->GetPolyData(PolyID, PolyFlags, Flags.Area);
		if (bFound)
		{
			const UClass* AreaClass = GetAreaClass(Flags.Area);
			const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
			Flags.AreaFlags = DefArea ? DefArea->GetAreaFlags() : 0;
			// @todo what is this literal?
			Flags.PathFlags = (PolyFlags & GetNavLinkFlag()) ? 4 : 0;
		}
	}

	return bFound;
}

bool ARecastNavMesh::GetPolyNeighbors(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Neighbors) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolyNeighbors(PolyID, Neighbors);
}

bool ARecastNavMesh::GetPolyNeighbors(NavNodeRef PolyID, TArray<NavNodeRef>& Neighbors) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolyNeighbors(PolyID, Neighbors);
}

bool ARecastNavMesh::GetPolyEdges(NavNodeRef PolyID, TArray<FNavigationPortalEdge>& Neighbors) const
{
	bool bFound = false;
	if (RecastNavMeshImpl)
	{
		bFound = RecastNavMeshImpl->GetPolyEdges(PolyID, Neighbors);
	}

	return bFound;
}

bool ARecastNavMesh::GetClosestPointOnPoly(NavNodeRef PolyID, const FVector& TestPt, FVector& PointOnPoly) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetClosestPointOnPoly(PolyID, TestPt, PointOnPoly);
}

bool ARecastNavMesh::GetPolyTileIndex(NavNodeRef PolyID, uint32& PolyIndex, uint32& TileIndex) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolyTileIndex(PolyID, PolyIndex, TileIndex);
}

bool ARecastNavMesh::GetLinkEndPoints(NavNodeRef LinkPolyID, FVector& PointA, FVector& PointB) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetLinkEndPoints(LinkPolyID, PointA, PointB);
}

bool ARecastNavMesh::IsCustomLink(NavNodeRef LinkPolyID) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->IsCustomLink(LinkPolyID);
}

#if WITH_NAVMESH_CLUSTER_LINKS
bool ARecastNavMesh::GetClusterBounds(NavNodeRef ClusterRef, FBox& OutBounds) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetClusterBounds(ClusterRef, OutBounds);
}
#endif // WITH_NAVMESH_CLUSTER_LINKS

bool ARecastNavMesh::GetPolysWithinPathingDistance(FVector const& StartLoc, const float PathingDistance, TArray<NavNodeRef>& FoundPolys,
	FSharedConstNavQueryFilter Filter, const UObject* QueryOwner, FRecastDebugPathfindingData* DebugData) const
{
	return RecastNavMeshImpl && RecastNavMeshImpl->GetPolysWithinPathingDistance(StartLoc, PathingDistance, GetRightFilterRef(Filter), QueryOwner, FoundPolys, DebugData);
}

void ARecastNavMesh::GetDebugGeometry(FRecastDebugGeometry& OutGeometry, int32 TileIndex) const
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->GetDebugGeometry(OutGeometry, TileIndex);
	}
}

void ARecastNavMesh::RequestDrawingUpdate(bool bForce)
{
#if !UE_BUILD_SHIPPING
	if (bForce || UNavMeshRenderingComponent::IsNavigationShowFlagSet(GetWorld()))
	{
		if (bForce)
		{
			UNavMeshRenderingComponent* NavRenderingComp = Cast<UNavMeshRenderingComponent>(RenderingComp);
			if (NavRenderingComp)
			{
				NavRenderingComp->ForceUpdate();
			}
		}

		DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.Requesting navmesh redraw"),
		STAT_FSimpleDelegateGraphTask_RequestingNavmeshRedraw,
			STATGROUP_TaskGraphTasks);

		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &ARecastNavMesh::UpdateDrawing),
			GET_STATID(STAT_FSimpleDelegateGraphTask_RequestingNavmeshRedraw), NULL, ENamedThreads::GameThread);
	}
#endif // !UE_BUILD_SHIPPING
}

void ARecastNavMesh::UpdateDrawing()
{
	UpdateNavMeshDrawing();
}

void ARecastNavMesh::DrawDebugPathCorridor(NavNodeRef const* PathPolys, int32 NumPathPolys, bool bPersistent) const
{
#if ENABLE_DRAW_DEBUG
	static const FColor PathLineColor(255, 128, 0);
	UWorld* World = GetWorld();

	// draw poly outlines
	TArray<FVector> PolyVerts;
	for (int32 PolyIdx=0; PolyIdx < NumPathPolys; ++PolyIdx)
	{
		if ( GetPolyVerts(PathPolys[PolyIdx], PolyVerts) )
		{
			for (int32 VertIdx=0; VertIdx < PolyVerts.Num()-1; ++VertIdx)
			{
				DrawDebugLine(World, PolyVerts[VertIdx], PolyVerts[VertIdx+1], PathLineColor, bPersistent);
			}
			DrawDebugLine(World, PolyVerts[PolyVerts.Num()-1], PolyVerts[0], PathLineColor, bPersistent);
		}
	}

	// draw ordered poly links
	if (NumPathPolys > 0)
	{
		FVector PolyCenter;
		FVector NextPolyCenter;
		if ( GetPolyCenter(PathPolys[0], NextPolyCenter) )			// prime the pump
		{
			for (int32 PolyIdx=0; PolyIdx < NumPathPolys-1; ++PolyIdx)
			{
				PolyCenter = NextPolyCenter;
				if ( GetPolyCenter(PathPolys[PolyIdx+1], NextPolyCenter) )
				{
					DrawDebugLine(World, PolyCenter, NextPolyCenter, PathLineColor, bPersistent);
					DrawDebugBox(World, PolyCenter, FVector(5.f), PathLineColor, bPersistent);
				}
			}
		}
	}
#endif // ENABLE_DRAW_DEBUG
}

void ARecastNavMesh::OnNavMeshTilesUpdated(const TArray<uint32>& ChangedTiles)
{
	InvalidateAffectedPaths(ChangedTiles);
}

void ARecastNavMesh::InvalidateAffectedPaths(const TArray<uint32>& ChangedTiles)
{
	const int32 PathsCount = ActivePaths.Num();
	const int32 ChangedTilesCount = ChangedTiles.Num();
	
	if (ChangedTilesCount == 0 || PathsCount == 0)
	{
		return;
	}

	// Paths can be registered from async pathfinding thread.
	// Theoretically paths are invalidated synchronously by the navigation system 
	// before starting async queries task but protecting ActivePaths will make
	// the system safer in case of future timing changes.
	{
		FScopeLock PathLock(&ActivePathsLock);

		FNavPathWeakPtr* WeakPathPtr = (ActivePaths.GetData() + PathsCount - 1);
		for (int32 PathIndex = PathsCount - 1; PathIndex >= 0; --PathIndex, --WeakPathPtr)
		{
			FNavPathSharedPtr SharedPath = WeakPathPtr->Pin();
			if (WeakPathPtr->IsValid() == false)
			{
				ActivePaths.RemoveAtSwap(PathIndex, 1, /*bAllowShrinking=*/false);
			}
			else
			{
				// iterate through all tile refs in FreshTilesCopy and 
				const FNavMeshPath* Path = (const FNavMeshPath*)(SharedPath.Get());
				if (Path->IsReady() == false ||
					Path->GetIgnoreInvalidation() == true)
				{
					// path not filled yet or doesn't care about invalidation
					continue;
				}

				const int32 PathLenght = Path->PathCorridor.Num();
				const NavNodeRef* PathPoly = Path->PathCorridor.GetData();
				for (int32 NodeIndex = 0; NodeIndex < PathLenght; ++NodeIndex, ++PathPoly)
				{
					const uint32 NodeTileIdx = RecastNavMeshImpl->GetTileIndexFromPolyRef(*PathPoly);
					if (ChangedTiles.Contains(NodeTileIdx))
					{
						SharedPath->Invalidate();
						ActivePaths.RemoveAtSwap(PathIndex, 1, /*bAllowShrinking=*/false);
						break;
					}
				}
			}
		}
	}
}

URecastNavMeshDataChunk* ARecastNavMesh::GetNavigationDataChunk(ULevel* InLevel) const
{
	FName ThisName = GetFName();
	int32 ChunkIndex = InLevel->NavDataChunks.IndexOfByPredicate([&](UNavigationDataChunk* Chunk) 
	{
		return Chunk->NavigationDataName == ThisName;
	});
	
	URecastNavMeshDataChunk* RcNavDataChunk = nullptr;
	if (ChunkIndex != INDEX_NONE)
	{
		RcNavDataChunk = Cast<URecastNavMeshDataChunk>(InLevel->NavDataChunks[ChunkIndex]);
	}
		
	return RcNavDataChunk;
}

void ARecastNavMesh::EnsureBuildCompletion()
{
	Super::EnsureBuildCompletion();

	// Doing this as a safety net solution due to UE-20646, which was basically a result of random 
	// over-releasing of default filter's shared pointer (it seemed). We might have time to get 
	// back to this time some time in next 3 years :D
	RecreateDefaultFilter();
}

void ARecastNavMesh::OnNavMeshGenerationFinished()
{
	UWorld* World = GetWorld();

	if (World != nullptr && World->IsPendingKill() == false)
	{
#if WITH_EDITOR	
		// For navmeshes that support streaming create navigation data holders in each streaming level
		// so parts of navmesh can be streamed in/out with those levels
		if (!World->IsGameWorld())
		{
			const auto& Levels = World->GetLevels();
			for (auto Level : Levels)
			{
				if (Level->IsPersistentLevel())
				{
					continue;
				}

				URecastNavMeshDataChunk* NavDataChunk = GetNavigationDataChunk(Level);

				if (SupportsStreaming())
				{
					// We use navigation volumes that belongs to this streaming level to find tiles we want to save
					TArray<int32> LevelTiles;
					TArray<FBox> LevelNavBounds = GetNavigableBoundsInLevel(Level);
					RecastNavMeshImpl->GetNavMeshTilesIn(LevelNavBounds, LevelTiles);

					if (LevelTiles.Num())
					{
						// Create new chunk only if we have something to save in it			
						if (NavDataChunk == nullptr)
						{
							NavDataChunk = NewObject<URecastNavMeshDataChunk>(Level);
							NavDataChunk->NavigationDataName = GetFName();
							Level->NavDataChunks.Add(NavDataChunk);
						}

						const EGatherTilesCopyMode CopyMode = RecastNavMeshImpl->NavMeshOwner->SupportsRuntimeGeneration() ? EGatherTilesCopyMode::CopyDataAndCacheData  : EGatherTilesCopyMode::CopyData;
						NavDataChunk->GetTiles(RecastNavMeshImpl, LevelTiles, CopyMode);
						NavDataChunk->MarkPackageDirty();
						continue;
					}
				}

				// stale data that is left in the level
				if (NavDataChunk)
				{
					// clear it
					NavDataChunk->ReleaseTiles();
					NavDataChunk->MarkPackageDirty();
					Level->NavDataChunks.Remove(NavDataChunk);
				}
			}
		}

		// force navmesh drawing update
		RequestDrawingUpdate(/*bForce=*/true);		
#endif// WITH_EDITOR

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys)
		{
			NavSys->OnNavigationGenerationFinished(*this);
		}
	}
}

#if !UE_BUILD_SHIPPING
uint32 ARecastNavMesh::LogMemUsed() const 
{
	const uint32 SuperMemUsed = Super::LogMemUsed();
	const int32 headerSize = dtAlign4(sizeof(dtMeshHeader));

	uint32 MemUsed = 0;

	if (RecastNavMeshImpl && RecastNavMeshImpl->DetourNavMesh)
	{
		const dtNavMesh* const ConstNavMesh = RecastNavMeshImpl->DetourNavMesh;

		for (int TileIndex = 0; TileIndex < RecastNavMeshImpl->DetourNavMesh->getMaxTiles(); ++TileIndex)
		{
			const dtMeshTile* Tile = ConstNavMesh->getTile(TileIndex);
			if (Tile && Tile->header)
			{
				dtMeshHeader* const H = (dtMeshHeader*)(Tile->header);
				const int32 vertsSize = dtAlign4(sizeof(float) * 3 * H->vertCount);
				const int32 polysSize = dtAlign4(sizeof(dtPoly) * H->polyCount);
				const int32 linksSize = dtAlign4(sizeof(dtLink) * H->maxLinkCount);
				const int32 detailMeshesSize = dtAlign4(sizeof(dtPolyDetail) * H->detailMeshCount);
				const int32 detailVertsSize = dtAlign4(sizeof(float) * 3 * H->detailVertCount);
				const int32 detailTrisSize = dtAlign4(sizeof(unsigned char) * 4 * H->detailTriCount);
				const int32 bvTreeSize = dtAlign4(sizeof(dtBVNode) * H->bvNodeCount);
				const int32 offMeshConsSize = dtAlign4(sizeof(dtOffMeshConnection) * H->offMeshConCount);

#if WITH_NAVMESH_SEGMENT_LINKS
				const int32 offMeshSegsSize = dtAlign4(sizeof(dtOffMeshSegmentConnection) * H->offMeshSegConCount);
#else
				const int32 offMeshSegsSize = 0;
#endif // WITH_NAVMESH_SEGMENT_LINKS

#if WITH_NAVMESH_CLUSTER_LINKS
				const int32 clusterSize = dtAlign4(sizeof(dtCluster) * H->clusterCount);
				const int32 polyClustersSize = dtAlign4(sizeof(unsigned short) * H->detailMeshCount);
#else
				const int32 clusterSize = 0;
				const int32 polyClustersSize = 0;
#endif // WITH_NAVMESH_CLUSTER_LINKS

				const int32 TileDataSize = headerSize + vertsSize + polysSize + linksSize +
					detailMeshesSize + detailVertsSize + detailTrisSize +
					bvTreeSize + offMeshConsSize + offMeshSegsSize +
					clusterSize + polyClustersSize;

				MemUsed += TileDataSize;
			}
		}
	}

	UE_LOG(LogNavigation, Warning, TEXT("%s: ARecastNavMesh: %u\n    self: %d"), *GetName(), MemUsed, sizeof(ARecastNavMesh));	

	return MemUsed + SuperMemUsed;
}

#endif // !UE_BUILD_SHIPPING

uint16 ARecastNavMesh::GetDefaultForbiddenFlags() const
{
	return FPImplRecastNavMesh::GetFilterForbiddenFlags((const FRecastQueryFilter*)DefaultQueryFilter->GetImplementation());
}

void ARecastNavMesh::SetDefaultForbiddenFlags(uint16 ForbiddenAreaFlags)
{
	FPImplRecastNavMesh::SetFilterForbiddenFlags((FRecastQueryFilter*)DefaultQueryFilter->GetImplementation(), ForbiddenAreaFlags);
}

void ARecastNavMesh::SetMaxSimultaneousTileGenerationJobsCount(int32 NewJobsCountLimit) 
{
	const int32 NewCount = NewJobsCountLimit > 0 ? NewJobsCountLimit : 1;
	if (MaxSimultaneousTileGenerationJobsCount != NewCount)
	{
		MaxSimultaneousTileGenerationJobsCount = NewCount;
		if (GetGenerator() != nullptr)
		{
			FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
			MyGenerator->SetMaxTileGeneratorTasks(NewCount);
		}
	}
}

bool ARecastNavMesh::FilterPolys(TArray<NavNodeRef>& PolyRefs, const FRecastQueryFilter* Filter, const UObject* QueryOwner) const
{
	bool bSuccess = false;
	if (RecastNavMeshImpl)
	{
		bSuccess = RecastNavMeshImpl->FilterPolys(PolyRefs, Filter, QueryOwner);
	}

	return bSuccess;
}

void ARecastNavMesh::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	if (RecastNavMeshImpl)
	{
		RecastNavMeshImpl->ApplyWorldOffset(InOffset, bWorldShift);
	}

	Super::ApplyWorldOffset(InOffset, bWorldShift);
	RequestDrawingUpdate();
}

void ARecastNavMesh::OnStreamingLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMesh_OnStreamingLevelAdded);
	
	if (SupportsStreaming() && RecastNavMeshImpl)
	{
		URecastNavMeshDataChunk* NavDataChunk = GetNavigationDataChunk(InLevel);
		if (NavDataChunk)
		{
			AttachNavMeshDataChunk(*NavDataChunk);
		}
	}
}

void ARecastNavMesh::AttachNavMeshDataChunk(URecastNavMeshDataChunk& NavDataChunk)
{
	TArray<uint32> AttachedIndices = NavDataChunk.AttachTiles(*RecastNavMeshImpl);
	if (AttachedIndices.Num() > 0)
	{
		InvalidateAffectedPaths(AttachedIndices);
		RequestDrawingUpdate();
	}
}

void ARecastNavMesh::OnStreamingLevelRemoved(ULevel* InLevel, UWorld* InWorld)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMesh_OnStreamingLevelRemoved);
	
	if (SupportsStreaming() && RecastNavMeshImpl)
	{
		URecastNavMeshDataChunk* NavDataChunk = GetNavigationDataChunk(InLevel);
		if (NavDataChunk)
		{
			DetachNavMeshDataChunk(*NavDataChunk);
		}
	}
}

void ARecastNavMesh::DetachNavMeshDataChunk(URecastNavMeshDataChunk& NavDataChunk)
{
	TArray<uint32> DetachedIndices = NavDataChunk.DetachTiles(*RecastNavMeshImpl);
	if (DetachedIndices.Num() > 0)
	{
		InvalidateAffectedPaths(DetachedIndices);
		RequestDrawingUpdate();
	}
}

bool ARecastNavMesh::AdjustLocationWithFilter(const FVector& StartLoc, FVector& OutAdjustedLocation, const FNavigationQueryFilter& Filter, const UObject* QueryOwner) const
{
	INITIALIZE_NAVQUERY(NavQuery, Filter.GetMaxSearchNodes());

	const FVector NavExtent = GetModifiedQueryExtent(GetDefaultQueryExtent());
	const float Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(Filter.GetImplementation()))->GetAsDetourQueryFilter();
	ensure(QueryFilter);

	FVector RecastStart = Unreal2RecastPoint(StartLoc);
	FVector RecastAdjustedPoint = Unreal2RecastPoint(StartLoc);
	NavNodeRef StartPolyID = INVALID_NAVNODEREF;
	NavQuery.findNearestPoly(&RecastStart.X, Extent, QueryFilter, &StartPolyID, &RecastAdjustedPoint.X);

	if (FVector::DistSquared(RecastStart, RecastAdjustedPoint) < KINDA_SMALL_NUMBER)
	{
		OutAdjustedLocation = StartLoc;
		return false;
	}
	else
	{
		OutAdjustedLocation = Recast2UnrealPoint(RecastAdjustedPoint);
		// move it just a bit further - otherwise recast can still pick "wrong" poly when 
		// later projecting StartLoc (meaning a poly we want to filter out with 
		// QueryFilter here)
		OutAdjustedLocation += (OutAdjustedLocation - StartLoc).GetSafeNormal() * 0.1f;
		return true;
	}
}

FPathFindingResult ARecastNavMesh::FindPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_RecastPathfinding);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Pathfinding);

	const ANavigationData* Self = Query.NavData.Get();
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == NULL || RecastNavMesh->RecastNavMeshImpl == NULL)
	{
		return ENavigationQueryResult::Error;
	}
		
	FPathFindingResult Result(ENavigationQueryResult::Error);

	FNavigationPath* NavPath = Query.PathInstanceToFill.Get();
	FNavMeshPath* NavMeshPath = NavPath ? NavPath->CastPath<FNavMeshPath>() : nullptr;

	if (NavMeshPath)
	{
		Result.Path = Query.PathInstanceToFill;
		NavMeshPath->ResetForRepath();
	}
	else
	{
		Result.Path = Self->CreatePathInstance<FNavMeshPath>(Query);
		NavPath = Result.Path.Get();
		NavMeshPath = NavPath ? NavPath->CastPath<FNavMeshPath>() : nullptr;
	}

	const FNavigationQueryFilter* NavFilter = Query.QueryFilter.Get();
	if (NavMeshPath && NavFilter)
	{
		NavMeshPath->ApplyFlags(Query.NavDataFlags);

		const FVector AdjustedEndLocation = NavFilter->GetAdjustedEndLocation(Query.EndLocation);
		if ((Query.StartLocation - AdjustedEndLocation).IsNearlyZero() == true)
		{
			Result.Path->GetPathPoints().Reset();
			Result.Path->GetPathPoints().Add(FNavPathPoint(AdjustedEndLocation));
			Result.Result = ENavigationQueryResult::Success;
		}
		else
		{
			Result.Result = RecastNavMesh->RecastNavMeshImpl->FindPath(Query.StartLocation, AdjustedEndLocation, Query.CostLimit, *NavMeshPath, *NavFilter, Query.Owner.Get());

			const bool bPartialPath = Result.IsPartial();
			if (bPartialPath)
			{
				Result.Result = Query.bAllowPartialPaths ? ENavigationQueryResult::Success : ENavigationQueryResult::Fail;
			}
		}
	}

	return Result;
}

bool ARecastNavMesh::TestPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_RecastTestPath);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Pathfinding);

	const ANavigationData* Self = Query.NavData.Get();
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == NULL || RecastNavMesh->RecastNavMeshImpl == NULL)
	{
		return false;
	}

	bool bPathExists = true;

	const FNavigationQueryFilter* NavFilter = Query.QueryFilter.Get();
	if (NavFilter)
	{
		const FVector AdjustedEndLocation = NavFilter->GetAdjustedEndLocation(Query.EndLocation);
		if ((Query.StartLocation - AdjustedEndLocation).IsNearlyZero() == false)
		{
			ENavigationQueryResult::Type Result = RecastNavMesh->RecastNavMeshImpl->TestPath(Query.StartLocation, AdjustedEndLocation, *NavFilter, Query.Owner.Get(), NumVisitedNodes);
			bPathExists = (Result == ENavigationQueryResult::Success);
		}
	}

	return bPathExists;
}

bool ARecastNavMesh::TestHierarchicalPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes)
{
	const ANavigationData* Self = Query.NavData.Get();
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == nullptr || RecastNavMesh->RecastNavMeshImpl == nullptr || RecastNavMesh->RecastNavMeshImpl->DetourNavMesh == nullptr)
	{
		return false;
	}

	const bool bCanUseHierachicalPath = (Query.QueryFilter == RecastNavMesh->GetDefaultQueryFilter());
	bool bPathExists = true;

	const FNavigationQueryFilter* NavFilter = Query.QueryFilter.Get();
	if (NavFilter)
	{
		const FVector AdjustedEndLocation = NavFilter->GetAdjustedEndLocation(Query.EndLocation);
		if ((Query.StartLocation - AdjustedEndLocation).IsNearlyZero() == false)
		{
			bool bUseFallbackSearch = false;
			if (bCanUseHierachicalPath)
			{
#if WITH_NAVMESH_CLUSTER_LINKS
				ENavigationQueryResult::Type Result = RecastNavMesh->RecastNavMeshImpl->TestClusterPath(Query.StartLocation, AdjustedEndLocation, NumVisitedNodes);
#else
				UE_LOG(LogNavigation, Error, TEXT("Navmesh requires generation of clusters for hierarchical path. Set WITH_NAVMESH_CLUSTER_LINKS to 1 to generate them."));
				ENavigationQueryResult::Type Result = ENavigationQueryResult::Invalid;
#endif // WITH_NAVMESH_CLUSTER_LINKS
				bPathExists = (Result == ENavigationQueryResult::Success);

				if (Result == ENavigationQueryResult::Error)
				{
					bUseFallbackSearch = true;
				}
			}
			else
			{
				UE_LOG(LogNavigation, Log, TEXT("Hierarchical path finding test failed: filter doesn't match!"));
				bUseFallbackSearch = true;
			}

			if (bUseFallbackSearch)
			{
				ENavigationQueryResult::Type Result = RecastNavMesh->RecastNavMeshImpl->TestPath(Query.StartLocation, AdjustedEndLocation, *NavFilter, Query.Owner.Get(), NumVisitedNodes);
				bPathExists = (Result == ENavigationQueryResult::Success);
			}
		}
	}

	return bPathExists;
}

bool ARecastNavMesh::NavMeshRaycast(const ANavigationData* Self, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter,const UObject* QueryOwner, FRaycastResult& Result)
{
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == NULL || RecastNavMesh->RecastNavMeshImpl == NULL)
	{
		HitLocation = RayStart;
		return true;
	}

	RecastNavMesh->RecastNavMeshImpl->Raycast(RayStart, RayEnd, RecastNavMesh->GetRightFilterRef(QueryFilter), QueryOwner, Result);
	HitLocation = Result.HasHit() ? (RayStart + (RayEnd - RayStart) * Result.HitTime) : RayEnd;

	return Result.HasHit();
}

bool ARecastNavMesh::NavMeshRaycast(const ANavigationData* Self, NavNodeRef RayStartNode, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter, const UObject* QueryOwner)
{
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == NULL || RecastNavMesh->RecastNavMeshImpl == NULL)
	{
		HitLocation = RayStart;
		return true;
	}

	FRaycastResult Result;
	RecastNavMesh->RecastNavMeshImpl->Raycast(RayStart, RayEnd, RecastNavMesh->GetRightFilterRef(QueryFilter), QueryOwner, Result, RayStartNode);

	HitLocation = Result.HasHit() ? (RayStart + (RayEnd - RayStart) * Result.HitTime) : RayEnd;
	return Result.HasHit();
}

void ARecastNavMesh::BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	if (RecastNavMeshImpl == NULL || Workload.Num() == 0 || RecastNavMeshImpl->DetourNavMesh == NULL)
	{
		return;
	}

	const FNavigationQueryFilter& FilterToUse = GetRightFilterRef(Filter);

	FRecastSpeciaLinkFilter LinkFilter(FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()), Querier);
	INITIALIZE_NAVQUERY_WLINKFILTER(NavQuery, FilterToUse.GetMaxSearchNodes(), LinkFilter);
	const dtQueryFilter* QueryFilter = ((const FRecastQueryFilter*)(FilterToUse.GetImplementation()))->GetAsDetourQueryFilter();
	
	if (QueryFilter == NULL)
	{
		UE_VLOG(this, LogNavigation, Warning, TEXT("FPImplRecastNavMesh::FindPath failing due to QueryFilter == NULL"));
		return;
	}
	
	const FVector NavExtent = GetModifiedQueryExtent(GetDefaultQueryExtent());
	const float Extent[3] = { NavExtent.X, NavExtent.Z, NavExtent.Y };

	for (FNavigationRaycastWork& WorkItem : Workload)
	{
		ARecastNavMesh::FRaycastResult RaycastResult;

		const FVector RecastStart = Unreal2RecastPoint(WorkItem.RayStart);
		const FVector RecastEnd = Unreal2RecastPoint(WorkItem.RayEnd);

		NavNodeRef StartNode = INVALID_NAVNODEREF;
		NavQuery.findNearestContainingPoly(&RecastStart.X, Extent, QueryFilter, &StartNode, NULL);

		if (StartNode != INVALID_NAVNODEREF)
		{
			float RecastHitNormal[3];

			const dtStatus RaycastStatus = NavQuery.raycast(StartNode, &RecastStart.X, &RecastEnd.X
				, QueryFilter, &RaycastResult.HitTime, RecastHitNormal
				, RaycastResult.CorridorPolys, &RaycastResult.CorridorPolysCount, RaycastResult.GetMaxCorridorSize());

			if (dtStatusSucceed(RaycastStatus) && RaycastResult.HasHit())
			{
				WorkItem.bDidHit = true;
				WorkItem.HitLocation = FNavLocation(WorkItem.RayStart + (WorkItem.RayEnd - WorkItem.RayStart) * RaycastResult.HitTime, RaycastResult.GetLastNodeRef());
			}
		}
	}
}

bool ARecastNavMesh::IsSegmentOnNavmesh(const FVector& SegmentStart, const FVector& SegmentEnd, FSharedConstNavQueryFilter Filter, const UObject* QueryOwner) const
{
	if (RecastNavMeshImpl == NULL)
	{
		return false;
	}
	
	FRaycastResult Result;
	RecastNavMeshImpl->Raycast(SegmentStart, SegmentEnd, GetRightFilterRef(Filter), QueryOwner, Result);

	return Result.bIsRaycastEndInCorridor && !Result.HasHit();
}

bool ARecastNavMesh::FindStraightPath(const FVector& StartLoc, const FVector& EndLoc, const TArray<NavNodeRef>& PathCorridor, TArray<FNavPathPoint>& PathPoints, TArray<uint32>* CustomLinks) const
{
	bool bResult = false;
	if (RecastNavMeshImpl)
	{
		bResult = RecastNavMeshImpl->FindStraightPath(StartLoc, EndLoc, PathCorridor, PathPoints, CustomLinks);
	}

	return bResult;
}

int32 ARecastNavMesh::DebugPathfinding(const FPathFindingQuery& Query, TArray<FRecastDebugPathfindingData>& Steps)
{
	int32 NumSteps = 0;

	const ANavigationData* Self = Query.NavData.Get();
	check(Cast<const ARecastNavMesh>(Self));

	const ARecastNavMesh* RecastNavMesh = (const ARecastNavMesh*)Self;
	if (Self == NULL || RecastNavMesh->RecastNavMeshImpl == NULL)
	{
		return false;
	}

	if ((Query.StartLocation - Query.EndLocation).IsNearlyZero() == false)
	{
		NumSteps = RecastNavMesh->RecastNavMeshImpl->DebugPathfinding(Query.StartLocation, Query.EndLocation, Query.CostLimit, *(Query.QueryFilter.Get()), Query.Owner.Get(), Steps);
	}

	return NumSteps;
}

void ARecastNavMesh::UpdateNavVersion() 
{ 
	NavMeshVersion = NAVMESHVER_LATEST; 
}

#if WITH_EDITOR

void ARecastNavMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_Generation = FName(TEXT("Generation"));
	static const FName NAME_Display = FName(TEXT("Display"));
	static const FName NAME_RuntimeGeneration = FName(TEXT("RuntimeGeneration"));
	static const FName NAME_TileNumberHardLimit = GET_MEMBER_NAME_CHECKED(ARecastNavMesh, TileNumberHardLimit);
	static const FName NAME_Query = FName(TEXT("Query"));

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != NULL)
	{
		const FName CategoryName = FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property);
		if (CategoryName == NAME_Generation)
		{
			const FName PropName = PropertyChangedEvent.Property->GetFName();
			
			if (PropName == GET_MEMBER_NAME_CHECKED(ARecastNavMesh, AgentRadius))
			{
				// changing AgentRadius is no longer affecting TileSizeUU since 
				// that's not how we use it. It's actually not really supported to 
				// modify AgentRadius directly on navmesh instance, since such
				// a navmesh will get discarded during navmesh registration with
				// the navigation system. 
				// @todo consider hiding it (we might already have a ticket for that).
				UE_LOG(LogNavigation, Warning, TEXT("Changing AgentRadius directly on RecastNavMesh instance is unsupported. Please use Project Settings > NavigationSystem > SupportedAgents to change AgentRadius"));
			}
			else if (PropName == GET_MEMBER_NAME_CHECKED(ARecastNavMesh, TileSizeUU))
			{
				TileSizeUU = GetClampedTileSizeUU(TileSizeUU, CellSize, AgentRadius);
				
				// trying to make cell size match TileSizeUU an integer number of times
				const float AdjustedCellSize = TileSizeUU / FMath::TruncToInt(TileSizeUU / CellSize);
				CellSize = FMath::Clamp(AdjustedCellSize, TileSizeUU / ArbitraryMinTileSizeVoxels, TileSizeUU / ArbitraryMaxTileSizeVoxels);

				// update config
				FillConfig(NavDataConfig);
			}
			else if (PropName == GET_MEMBER_NAME_CHECKED(ARecastNavMesh, CellSize))
			{
				const float AdjustedTileSizeUU = CellSize * FMath::TruncToInt(TileSizeUU / CellSize);
				TileSizeUU = GetClampedTileSizeUU(AdjustedTileSizeUU, CellSize, AgentRadius);

				// update config
				FillConfig(NavDataConfig);
			}
			else if (PropName == NAME_TileNumberHardLimit)
			{
				TileNumberHardLimit = 1 << (FMath::CeilToInt(FMath::Log2(TileNumberHardLimit)));
				UpdatePolyRefBitsPreview();
			}

			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			if (!HasAnyFlags(RF_ClassDefaultObject)
				&& NavSys->GetIsAutoUpdateEnabled()
				&& PropName != GET_MEMBER_NAME_CHECKED(ARecastNavMesh, MaxSimultaneousTileGenerationJobsCount))
			{
				RebuildAll();
			}
		}
		else if (CategoryName == NAME_Display)
		{
			RequestDrawingUpdate();
		}
		else if (PropertyChangedEvent.Property->GetFName() == NAME_RuntimeGeneration)
		{
			// @todo this contraption is required to clear RuntimeGeneration value in DefaultEngine.ini
			// if it gets set to its default value (UE-23762). This is hopefully a temporary solution
			// since it's an Core-level issue (UE-23873).
			if (RuntimeGeneration == ERuntimeGenerationType::Static)
			{
				const FString EngineIniFilename = FPaths::ConvertRelativePathToFull(GetDefault<UEngine>()->GetDefaultConfigFilename());
				GConfig->SetString(TEXT("/Script/NavigationSystem.RecastNavMesh"), *NAME_RuntimeGeneration.ToString(), TEXT("Static"), *EngineIniFilename);
				GConfig->Flush(false);
			}
		}
		else if (CategoryName == NAME_Query)
		{
			RecreateDefaultFilter();
		}
	}
}

#endif // WITH_EDITOR

bool ARecastNavMesh::NeedsRebuild() const
{
	bool bLooksLikeNeeded = !RecastNavMeshImpl || RecastNavMeshImpl->GetRecastMesh() == 0;
	if (NavDataGenerator.IsValid())
	{
		return bLooksLikeNeeded || NavDataGenerator->GetNumRemaningBuildTasks() > 0;
	}

	return bLooksLikeNeeded;
}

bool ARecastNavMesh::SupportsRuntimeGeneration() const
{
	// Generator should be disabled for Static navmesh
	return (RuntimeGeneration != ERuntimeGenerationType::Static);
}

bool ARecastNavMesh::SupportsStreaming() const
{
	// Actually nothing prevents us to support streaming with dynamic generation
	// Right now streaming in sub-level causes navmesh to build itself, so no point to stream tiles in
	return (RuntimeGeneration != ERuntimeGenerationType::Dynamic);
}

FRecastNavMeshGenerator* ARecastNavMesh::CreateGeneratorInstance()
{
	return new FRecastNavMeshGenerator(*this);
}

void ARecastNavMesh::ConditionalConstructGenerator()
{	
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}

	UWorld* World = GetWorld();
	check(World);
	const bool bRequiresGenerator = SupportsRuntimeGeneration() || !World->IsGameWorld();
	if (bRequiresGenerator)
	{
		FRecastNavMeshGenerator* Generator = CreateGeneratorInstance();
		if (Generator)
		{
			NavDataGenerator = MakeShareable((FNavDataGenerator*)Generator);
			Generator->Init();
		}

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys)
		{
			RestrictBuildingToActiveTiles(NavSys->IsActiveTilesGenerationEnabled());
		}
	}
}

void ARecastNavMesh::UpdateGenerationProperties(const FRecastNavMeshGenerationProperties& GenerationProps)
{
	TilePoolSize = GenerationProps.TilePoolSize;
	TileSizeUU = GenerationProps.TileSizeUU;
	CellSize = GenerationProps.CellSize;
	CellHeight = GenerationProps.CellHeight;
	AgentRadius = GenerationProps.AgentRadius;
	AgentHeight = GenerationProps.AgentHeight;
	AgentMaxSlope = GenerationProps.AgentMaxSlope;
	AgentMaxStepHeight = GenerationProps.AgentMaxStepHeight;
	MinRegionArea = GenerationProps.MinRegionArea;
	MergeRegionSize = GenerationProps.MergeRegionSize;
	MaxSimplificationError = GenerationProps.MaxSimplificationError;
	TileNumberHardLimit = GenerationProps.TileNumberHardLimit;
	RegionPartitioning = GenerationProps.RegionPartitioning;
	LayerPartitioning = GenerationProps.LayerPartitioning;
	RegionChunkSplits = GenerationProps.RegionChunkSplits;
	LayerChunkSplits = GenerationProps.LayerChunkSplits;
	bSortNavigationAreasByCost = GenerationProps.bSortNavigationAreasByCost;
	bPerformVoxelFiltering = GenerationProps.bPerformVoxelFiltering;
	bMarkLowHeightAreas = GenerationProps.bMarkLowHeightAreas;
	bUseExtraTopCellWhenMarkingAreas = GenerationProps.bUseExtraTopCellWhenMarkingAreas;
	bFilterLowSpanSequences = GenerationProps.bFilterLowSpanSequences;
	bFilterLowSpanFromTileCache = GenerationProps.bFilterLowSpanFromTileCache;
	bFixedTilePoolSize = GenerationProps.bFixedTilePoolSize;
}

bool ARecastNavMesh::IsVoxelCacheEnabled()
{
#if RECAST_ASYNC_REBUILDING
	// voxel cache is using static buffers to minimize memory impact
	// therefore it can run only with synchronous navmesh rebuilds
	return false;
#endif

	ARecastNavMesh* DefOb = (ARecastNavMesh*)ARecastNavMesh::StaticClass()->GetDefaultObject();
	return DefOb && DefOb->bUseVoxelCache;
}

const FRecastQueryFilter* ARecastNavMesh::GetNamedFilter(ERecastNamedFilter::Type FilterType)
{
	check(FilterType < ERecastNamedFilter::NamedFiltersCount); 
	return NamedFilters[FilterType];
}

#undef INITIALIZE_NAVQUERY

void ARecastNavMesh::UpdateNavObject()
{
	OnNavMeshUpdate.Broadcast();
}
#endif	//WITH_RECAST

bool ARecastNavMesh::HasValidNavmesh() const
{
#if WITH_RECAST
	return (RecastNavMeshImpl && RecastNavMeshImpl->DetourNavMesh && RecastNavMeshImpl->DetourNavMesh->isEmpty() == false);
#else
	return false;
#endif // WITH_RECAST
}

#if WITH_RECAST
bool ARecastNavMesh::HasCompleteDataInRadius(const FVector& TestLocation, float TestRadius) const
{
	if (HasValidNavmesh() == false)
	{
		return false;
	}

	const dtNavMesh* NavMesh = RecastNavMeshImpl->DetourNavMesh;
	const dtNavMeshParams* NavParams = RecastNavMeshImpl->DetourNavMesh->getParams();
	const float NavTileSize = CellSize * FMath::TruncToInt(TileSizeUU / CellSize);
	const FVector RcNavOrigin(NavParams->orig[0], NavParams->orig[1], NavParams->orig[2]);

	const FBox RcBounds = Unreal2RecastBox(FBox::BuildAABB(TestLocation, FVector(TestRadius, TestRadius, 0)));
	const FVector RcTestLocation = Unreal2RecastPoint(TestLocation);

	const int32 MinTileX = FMath::FloorToInt((RcBounds.Min.X - RcNavOrigin.X) / NavTileSize);
	const int32 MaxTileX = FMath::CeilToInt((RcBounds.Max.X - RcNavOrigin.X) / NavTileSize);
	const int32 MinTileY = FMath::FloorToInt((RcBounds.Min.Z - RcNavOrigin.Z) / NavTileSize);
	const int32 MaxTileY = FMath::CeilToInt((RcBounds.Max.Z - RcNavOrigin.Z) / NavTileSize);
	const FVector RcTileExtent2D(NavTileSize * 0.5f, 0.f, NavTileSize * 0.5f);
	const float RadiusSq = FMath::Square(TestRadius);

	for (int32 TileX = MinTileX; TileX <= MaxTileX; TileX++)
	{
		for (int32 TileY = MinTileY; TileY <= MaxTileY; TileY++)
		{
			const FVector RcTileCenter(RcNavOrigin.X + ((TileX + 0.5f) * NavTileSize), RcTestLocation.Y, RcNavOrigin.Z + ((TileY + 0.5f) * NavTileSize));
			const bool bInside = FMath::SphereAABBIntersection(RcTestLocation, RadiusSq, FBox::BuildAABB(RcTileCenter, RcTileExtent2D));
			if (bInside)
			{
				const int32 NumTiles = NavMesh->getTileCountAt(TileX, TileY);
				if (NumTiles <= 0)
				{
					const bool bHasFailsafeData = bStoreEmptyTileLayers && RecastNavMeshImpl->HasTileCacheLayers(TileX, TileY);
					if (!bHasFailsafeData)
					{
						return false;
					}
				}
			}
		}
	}

	return true;
}

//----------------------------------------------------------------------//
// RecastNavMesh: Active Tiles 
//----------------------------------------------------------------------//
void ARecastNavMesh::UpdateActiveTiles(const TArray<FNavigationInvokerRaw>& InvokerLocations)
{
	if (HasValidNavmesh() == false)
	{
		return;
	}

	FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
	if (MyGenerator == nullptr)
	{
		return;
	}

	const dtNavMeshParams* NavParams = GetRecastNavMeshImpl()->DetourNavMesh->getParams();
	check(NavParams && MyGenerator);
	const FRecastBuildConfig& Config = MyGenerator->GetConfig();
	const FVector NavmeshOrigin = Recast2UnrealPoint(NavParams->orig);
	const float TileDim = Config.tileSize * Config.cs;
	const FVector TileCenterOffset(TileDim, TileDim, 0);

	TArray<FIntPoint>& ActiveTiles = GetActiveTiles();
	TArray<FIntPoint> OldActiveSet = ActiveTiles;
	TArray<FIntPoint> TilesInMinDistance;
	TArray<FIntPoint> TilesInMaxDistance;
	TilesInMinDistance.Reserve(ActiveTiles.Num());
	TilesInMaxDistance.Reserve(ActiveTiles.Num());
	ActiveTiles.Reset();

	//const int32 TileRadius = FMath::CeilToInt(Radius / TileDim);
	static const float SqareRootOf2 = FMath::Sqrt(2.f);

	for (const FNavigationInvokerRaw& Invoker : InvokerLocations)
	{
		const FVector InvokerRelativeLocation = (NavmeshOrigin - Invoker.Location);
		const float TileCenterDistanceToRemoveSq = FMath::Square(TileDim * SqareRootOf2 / 2 + Invoker.RadiusMax);
		const float TileCenterDistanceToAddSq = FMath::Square(TileDim * SqareRootOf2 / 2 + Invoker.RadiusMin);

		const int32 MinTileX = FMath::FloorToInt((InvokerRelativeLocation.X - Invoker.RadiusMax) / TileDim);
		const int32 MaxTileX = FMath::CeilToInt((InvokerRelativeLocation.X + Invoker.RadiusMax) / TileDim);
		const int32 MinTileY = FMath::FloorToInt((InvokerRelativeLocation.Y - Invoker.RadiusMax) / TileDim);
		const int32 MaxTileY = FMath::CeilToInt((InvokerRelativeLocation.Y + Invoker.RadiusMax) / TileDim);

		for (int32 X = MinTileX; X <= MaxTileX; ++X)
		{
			for (int32 Y = MinTileY; Y <= MaxTileY; ++Y)
			{
				const float DistanceSq = (InvokerRelativeLocation - FVector(X * TileDim + TileDim / 2, Y * TileDim + TileDim / 2, 0.f)).SizeSquared2D();
				if (DistanceSq < TileCenterDistanceToRemoveSq)
				{
					TilesInMaxDistance.AddUnique(FIntPoint(X, Y));

					if (DistanceSq < TileCenterDistanceToAddSq)
					{
						TilesInMinDistance.AddUnique(FIntPoint(X, Y));
					}
				}
			}
		}
	}

	ActiveTiles.Append(TilesInMinDistance);

	TArray<FIntPoint> TilesToRemove;
	TilesToRemove.Reserve(OldActiveSet.Num());
	for (int32 Index = OldActiveSet.Num() - 1; Index >= 0; --Index)
	{
		if (TilesInMaxDistance.Find(OldActiveSet[Index]) == INDEX_NONE)
		{
			TilesToRemove.Add(OldActiveSet[Index]);
			OldActiveSet.RemoveAtSwap(Index, 1, /*bAllowShrinking=*/false);
		}
		else
		{
			ActiveTiles.AddUnique(OldActiveSet[Index]);
		}
	}

	TArray<FIntPoint> TilesToUpdate;
	TilesToUpdate.Reserve(ActiveTiles.Num());
	for (int32 Index = TilesInMinDistance.Num() - 1; Index >= 0; --Index)
	{
		// check if it's a new tile
		if (OldActiveSet.Find(TilesInMinDistance[Index]) == INDEX_NONE)
		{
			TilesToUpdate.Add(TilesInMinDistance[Index]);
		}
	}

	RemoveTiles(TilesToRemove);
	RebuildTile(TilesToUpdate);

	if (TilesToRemove.Num() > 0 || TilesToUpdate.Num() > 0)
	{
		UpdateNavMeshDrawing();
	}
}

void ARecastNavMesh::RemoveTiles(const TArray<FIntPoint>& Tiles)
{
	if (Tiles.Num() > 0)
	{
		FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
		if (MyGenerator)
		{
			MyGenerator->RemoveTiles(Tiles);
		}
	}
}

void ARecastNavMesh::RebuildTile(const TArray<FIntPoint>& Tiles)
{
	if (Tiles.Num() > 0)
	{
		FRecastNavMeshGenerator* MyGenerator = static_cast<FRecastNavMeshGenerator*>(GetGenerator());
		if (MyGenerator)
		{
			MyGenerator->ReAddTiles(Tiles);
		}
	}
}

#if RECAST_INTERNAL_DEBUG_DATA
const TMap<FIntPoint, struct FRecastInternalDebugData>* ARecastNavMesh::GetDebugDataMap() const
{
	if (RecastNavMeshImpl)
	{
		return &RecastNavMeshImpl->DebugDataMap;
	}
	return nullptr;
}
#endif //RECAST_INTERNAL_DEBUG_DATA

//----------------------------------------------------------------------//
// FRecastNavMeshCachedData
//----------------------------------------------------------------------//

FRecastNavMeshCachedData FRecastNavMeshCachedData::Construct(const ARecastNavMesh* RecastNavMeshActor)
{
	check(RecastNavMeshActor);
	
	FRecastNavMeshCachedData CachedData;

	CachedData.ActorOwner = RecastNavMeshActor;
	// create copies from crucial ARecastNavMesh data
	CachedData.bUseSortFunction = RecastNavMeshActor->bSortNavigationAreasByCost;

	TArray<FSupportedAreaData> Areas;
	RecastNavMeshActor->GetSupportedAreas(Areas);
	FMemory::Memzero(CachedData.FlagsPerArea, sizeof(ARecastNavMesh::FNavPolyFlags) * RECAST_MAX_AREAS);

	for (int32 i = 0; i < Areas.Num(); i++)
	{
		const UClass* AreaClass = Areas[i].AreaClass;
		const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
		if (DefArea)
		{
			CachedData.AreaClassToIdMap.Add(AreaClass, Areas[i].AreaID);
			CachedData.FlagsPerArea[Areas[i].AreaID] = DefArea->GetAreaFlags();
		}
	}

	FMemory::Memcpy(CachedData.FlagsPerOffMeshLinkArea, CachedData.FlagsPerArea, sizeof(CachedData.FlagsPerArea));
	static const ARecastNavMesh::FNavPolyFlags NavLinkFlag = ARecastNavMesh::GetNavLinkFlag();
	if (NavLinkFlag != 0)
	{
		ARecastNavMesh::FNavPolyFlags* AreaFlag = CachedData.FlagsPerOffMeshLinkArea;
		for (int32 AreaIndex = 0; AreaIndex < RECAST_MAX_AREAS; ++AreaIndex, ++AreaFlag)
		{
			*AreaFlag |= NavLinkFlag;
		}
	}

	return CachedData;
}

void FRecastNavMeshCachedData::OnAreaAdded(const UClass* AreaClass, int32 AreaID)
{
	const UNavArea* DefArea = AreaClass ? ((UClass*)AreaClass)->GetDefaultObject<UNavArea>() : NULL;
	if (DefArea && AreaID >= 0)
	{
		AreaClassToIdMap.Add(AreaClass, AreaID);
		FlagsPerArea[AreaID] = DefArea->GetAreaFlags();

		static const ARecastNavMesh::FNavPolyFlags NavLinkFlag = ARecastNavMesh::GetNavLinkFlag();
		if (NavLinkFlag != 0)
		{
			FlagsPerOffMeshLinkArea[AreaID] = FlagsPerArea[AreaID] | NavLinkFlag;
		}
	}		
}

uint32 ARecastNavMesh::GetLinkUserId(NavNodeRef LinkPolyID) const
{
	return RecastNavMeshImpl ? RecastNavMeshImpl->GetLinkUserId(LinkPolyID) : 0;
}

dtNavMesh* ARecastNavMesh::GetRecastMesh()
{
	return RecastNavMeshImpl ? RecastNavMeshImpl->GetRecastMesh() : nullptr;
}

const dtNavMesh* ARecastNavMesh::GetRecastMesh() const
{
	return RecastNavMeshImpl ? RecastNavMeshImpl->GetRecastMesh() : nullptr;
}
#endif// WITH_RECAST

//----------------------------------------------------------------------//
// BP API
//----------------------------------------------------------------------//
bool ARecastNavMesh::K2_ReplaceAreaInTileBounds(FBox Bounds, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool ReplaceLinks)
{
	bool bReplaced = ReplaceAreaInTileBounds(Bounds, OldArea, NewArea, ReplaceLinks) > 0;
	if (bReplaced)
	{
		RequestDrawingUpdate();
	}
	return bReplaced;
}
