// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldAtlas.cpp
=============================================================================*/

#include "DistanceFieldAtlas.h"
#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "Misc/App.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshResources.h"
#include "ProfilingDebugging/CookStats.h"
#include "Templates/UniquePtr.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Async/ParallelFor.h"
#include "DistanceFieldDownsampling.h"
#include "GlobalShader.h"
#include "RenderGraph.h"

#if WITH_EDITOR
#include "DerivedDataCacheInterface.h"
#include "MeshUtilities.h"
#endif

CSV_DEFINE_CATEGORY(DistanceField, false);

#if ENABLE_COOK_STATS
namespace DistanceFieldCookStats
{
	FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("DistanceField.Usage"), TEXT(""));
	});
}
#endif

static TAutoConsoleVariable<int32> CVarDistField(
	TEXT("r.GenerateMeshDistanceFields"),
	0,	
	TEXT("Whether to build distance fields of static meshes, needed for distance field AO, which is used to implement Movable SkyLight shadows.\n")
	TEXT("Enabling will increase mesh build times and memory usage.  Changing this value will cause a rebuild of all static meshes."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarCompressDistField(
	TEXT("r.DistanceFieldBuild.Compress"),
	0,	
	TEXT("Whether to store mesh distance fields compressed in memory, which reduces how much memory they take, but also causes serious hitches when making new levels visible.  Only enable if your project does not stream levels in-game.\n")
	TEXT("Changing this regenerates all mesh distance fields."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarEightBitDistField(
	TEXT("r.DistanceFieldBuild.EightBit"),
	0,	
	TEXT("Whether to store mesh distance fields in an 8 bit fixed point format instead of 16 bit floating point.  \n")
	TEXT("8 bit uses half the memory, but introduces artifacts for large meshes or thin meshes."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarUseEmbreeForMeshDistanceFieldGeneration(
	TEXT("r.DistanceFieldBuild.UseEmbree"),
	1,
	TEXT("Whether to use embree ray tracer for mesh distance field generation."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarDistFieldRes(
	TEXT("r.DistanceFields.MaxPerMeshResolution"),
	128,	
	TEXT("Highest resolution (in one dimension) allowed for a single static mesh asset, used to cap the memory usage of meshes with a large scale.\n")
	TEXT("Changing this will cause all distance fields to be rebuilt.  Large values such as 512 can consume memory very quickly! (128Mb for one asset at 512)"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<float> CVarDistFieldResScale(
	TEXT("r.DistanceFields.DefaultVoxelDensity"),
	.1f,	
	TEXT("Determines how the default scale of a mesh converts into distance field voxel dimensions.\n")
	TEXT("Changing this will cause all distance fields to be rebuilt.  Large values can consume memory very quickly!"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarDistFieldAtlasResXY(
	TEXT("r.DistanceFields.AtlasSizeXY"),
	512,	
	TEXT("Max size of the global mesh distance field atlas volume texture in X and Y."));

static TAutoConsoleVariable<int32> CVarDistFieldAtlasResZ(
	TEXT("r.DistanceFields.AtlasSizeZ"),
	1024,	
	TEXT("Max size of the global mesh distance field atlas volume texture in Z."));

int32 GDistanceFieldForceAtlasRealloc = 0;

FAutoConsoleVariableRef CVarDistFieldForceAtlasRealloc(
	TEXT("r.DistanceFields.ForceAtlasRealloc"),
	GDistanceFieldForceAtlasRealloc,
	TEXT("Force a full realloc."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDistFieldDiscardCPUData(
	TEXT("r.DistanceFields.DiscardCPUData"),
	0,
	TEXT("Discard Mesh DF CPU data once it has been ULed to Atlas. WIP - This cant be used if atlas gets reallocated and mesh DF needs to be ULed again to new atlas"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDistFieldThrottleCopyToAtlasInBytes(
	TEXT("r.DistanceFields.ThrottleCopyToAtlasInBytes"),
	0,
	TEXT("When enabled (higher than 0), throttle mesh distance field copy to global mesh distance field atlas volume (in bytes uncompressed)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarDistFieldRuntimeDownsampling(
	TEXT("r.DistanceFields.RuntimeDownsamplingFactor"),
	0,
	TEXT("When enabled (higher than 0 and lower than 1), mesh distance field will be downsampled by factor value on GPU and uploaded to the atlas."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLandscapeGI(
	TEXT("r.GenerateLandscapeGIData"),
	0,
	TEXT("Whether to generate a low-resolution base color texture for landscapes for rendering real-time global illumination.\n")
	TEXT("This feature requires GenerateMeshDistanceFields is also enabled, and will increase mesh build times and memory usage.\n"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDistFieldForceMaxAtlasSize(
	TEXT("r.DistanceFields.ForceMaxAtlasSize"),
	0,
	TEXT("When enabled, we'll always allocate the largest possible volume texture for the distance field atlas regardless of how many blocks we need.  This is an optimization to avoid re-packing the texture, for projects that are expected to always require the largest amount of space."),
	ECVF_Default);

static int32 GDistanceFieldParallelAtlasUpdate = 1;
static FAutoConsoleVariableRef CVarDistanceFieldParallelAtlasUpdate(
	TEXT("r.DistanceFields.ParallelAtlasUpdate"),
	GDistanceFieldParallelAtlasUpdate,
	TEXT("Whether to parallelize distance field data decompression and copying to upload buffer"),
	ECVF_RenderThreadSafe);

static int32 GHeightFieldAtlasTileSize = 64;
static FAutoConsoleVariableRef CVarHeightFieldAtlasTileSize(
	TEXT("r.HeightFields.AtlasTileSize"),
	GHeightFieldAtlasTileSize,
	TEXT("Suballocation granularity"),
	ECVF_RenderThreadSafe);

static int32 GHeightFieldAtlasDimInTiles = 16;
static FAutoConsoleVariableRef CVarHeightFieldAtlasDimInTiles(
	TEXT("r.HeightFields.AtlasDimInTiles"),
	GHeightFieldAtlasDimInTiles,
	TEXT("Number of tiles the atlas has in one dimension"),
	ECVF_RenderThreadSafe);

static int32 GHeightFieldAtlasDownSampleLevel = 2;
static FAutoConsoleVariableRef CVarHeightFieldAtlasDownSampleLevel(
	TEXT("r.HeightFields.AtlasDownSampleLevel"),
	GHeightFieldAtlasDownSampleLevel,
	TEXT("Max number of times a suballocation can be down-sampled"),
	ECVF_RenderThreadSafe);

static int32 GHFVisibilityAtlasTileSize = 64;
static FAutoConsoleVariableRef CVarHFVisibilityAtlasTileSize(
	TEXT("r.HeightFields.VisibilityAtlasTileSize"),
	GHFVisibilityAtlasTileSize,
	TEXT("Suballocation granularity"),
	ECVF_RenderThreadSafe);

static int32 GHFVisibilityAtlasDimInTiles = 8;
static FAutoConsoleVariableRef CVarHFVisibilityAtlasDimInTiles(
	TEXT("r.HeightFields.VisibilityAtlasDimInTiles"),
	GHFVisibilityAtlasDimInTiles,
	TEXT("Number of tiles the atlas has in one dimension"),
	ECVF_RenderThreadSafe);

static int32 GHFVisibilityAtlasDownSampleLevel = 2;
static FAutoConsoleVariableRef CVarHFVisibilityAtlasDownSampleLevel(
	TEXT("r.HeightFields.VisibilityAtlasDownSampleLevel"),
	GHFVisibilityAtlasDownSampleLevel,
	TEXT("Max number of times a suballocation can be down-sampled"),
	ECVF_RenderThreadSafe);

TGlobalResource<FDistanceFieldVolumeTextureAtlas> GDistanceFieldVolumeTextureAtlas = TGlobalResource<FDistanceFieldVolumeTextureAtlas>();
TGlobalResource<FLandscapeTextureAtlas> GHeightFieldTextureAtlas(FLandscapeTextureAtlas::SAT_Height);
TGlobalResource<FLandscapeTextureAtlas> GHFVisibilityTextureAtlas(FLandscapeTextureAtlas::SAT_Visibility);

FDistanceFieldVolumeTextureAtlas::FDistanceFieldVolumeTextureAtlas() :
	BlockAllocator(0, 0, 0, 0, 0, 0, false, false),
	bInitialized(false),
	AllocatedPixels(0),
	FailedAllocatedPixels(0),
	MaxUsedAtlasX(0),
	MaxUsedAtlasY(0),
	MaxUsedAtlasZ(0),
	AllocatedCPUDataInBytes(0)
{
	// Warning: can't access cvars here, this is called during global init
	Generation = 0;
}

void FDistanceFieldVolumeTextureAtlas::InitializeIfNeeded()
{
	if (!bInitialized)
	{
		bInitialized = true;

		static const auto CVarEightBit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.EightBit"));
		const bool bEightBitFixedPoint = CVarEightBit->GetValueOnAnyThread() != 0;

		Format = bEightBitFixedPoint ? PF_G8 : PF_R16F;

		static const auto CVarXY = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.AtlasSizeXY"));
		const int32 AtlasXY = CVarXY->GetValueOnAnyThread();

		static const auto CVarZ = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.AtlasSizeZ"));
		const int32 AtlasZ = CVarZ->GetValueOnAnyThread();

		BlockAllocator = FTextureLayout3d(0, 0, 0, AtlasXY, AtlasXY, AtlasZ, false, false);

		MaxUsedAtlasX = 0;
		MaxUsedAtlasY = 0;
		MaxUsedAtlasZ = 0;
	}
}

FString FDistanceFieldVolumeTextureAtlas::GetSizeString() const
{
	if (VolumeTextureRHI)
	{
		const int32 FormatSize = GPixelFormats[Format].BlockBytes;

		size_t BackingDataBytes = 0;

		for (int32 AllocationIndex = 0; AllocationIndex < CurrentAllocations.Num(); AllocationIndex++)
		{
			FDistanceFieldVolumeTexture* Texture = CurrentAllocations[AllocationIndex];
			BackingDataBytes += Texture->VolumeData.CompressedDistanceFieldVolume.Num() * Texture->VolumeData.CompressedDistanceFieldVolume.GetTypeSize();
		}

		for (int32 AllocationIndex = 0; AllocationIndex < PendingAllocations.Num(); AllocationIndex++)
		{
			FDistanceFieldVolumeTexture* Texture = PendingAllocations[AllocationIndex];
			BackingDataBytes += Texture->VolumeData.CompressedDistanceFieldVolume.Num() * Texture->VolumeData.CompressedDistanceFieldVolume.GetTypeSize();
		}

		float AtlasMemorySize = VolumeTextureRHI->GetSizeX() * VolumeTextureRHI->GetSizeY() * VolumeTextureRHI->GetSizeZ() * FormatSize / 1024.0f / 1024.0f;
		return FString::Printf(TEXT("Allocated %ux%ux%u distance field atlas = %.1fMb, with %u objects containing %.1fMb backing data"), 
			VolumeTextureRHI->GetSizeX(), 
			VolumeTextureRHI->GetSizeY(), 
			VolumeTextureRHI->GetSizeZ(), 
			AtlasMemorySize,
			CurrentAllocations.Num() + PendingAllocations.Num(),
			BackingDataBytes / 1024.0f / 1024.0f);
	}
	else
	{
		return TEXT("");
	}
}

struct FMeshDistanceFieldStats
{
	size_t MemoryBytes;
	float ResolutionScale;
	UStaticMesh* Mesh;
};

void FDistanceFieldVolumeTextureAtlas::ListMeshDistanceFields() const
{
	TArray<FMeshDistanceFieldStats> GatheredStats;

	const int32 FormatSize = GPixelFormats[Format].BlockBytes;

	for (int32 AllocationIndex = 0; AllocationIndex < CurrentAllocations.Num(); AllocationIndex++)
	{
		FDistanceFieldVolumeTexture* Texture = CurrentAllocations[AllocationIndex];
		FMeshDistanceFieldStats Stats;
		size_t AtlasMemory = Texture->VolumeData.Size.X * Texture->VolumeData.Size.Y * Texture->VolumeData.Size.Z * FormatSize;
		size_t BackingMemory = Texture->VolumeData.CompressedDistanceFieldVolume.Num() * Texture->VolumeData.CompressedDistanceFieldVolume.GetTypeSize();
		Stats.MemoryBytes = AtlasMemory + BackingMemory;
		Stats.Mesh = Texture->GetStaticMesh();
#if WITH_EDITORONLY_DATA
		Stats.ResolutionScale = Stats.Mesh->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale;
#else
		Stats.ResolutionScale = -1;
#endif
		GatheredStats.Add(Stats);
	}

	struct FMeshDistanceFieldStatsSorter
	{
		bool operator()( const FMeshDistanceFieldStats& A, const FMeshDistanceFieldStats& B ) const
		{
			return A.MemoryBytes > B.MemoryBytes;
		}
	};

	GatheredStats.Sort(FMeshDistanceFieldStatsSorter());

	size_t TotalMemory = 0;

	for (int32 EntryIndex = 0; EntryIndex < GatheredStats.Num(); EntryIndex++)
	{
		const FMeshDistanceFieldStats& MeshStats = GatheredStats[EntryIndex];
		TotalMemory += MeshStats.MemoryBytes;
	}

	UE_LOG(LogStaticMesh, Log, TEXT("Dumping mesh distance fields for %u meshes, total %.1fMb"), GatheredStats.Num(), TotalMemory / 1024.0f / 1024.0f);
	UE_LOG(LogStaticMesh, Log, TEXT("   Memory Mb, Scale, Name, Path"));

	for (int32 EntryIndex = 0; EntryIndex < GatheredStats.Num(); EntryIndex++)
	{
		const FMeshDistanceFieldStats& MeshStats = GatheredStats[EntryIndex];
		UE_LOG(LogStaticMesh, Log, TEXT("   %.2f, %.1f, %s, %s"), MeshStats.MemoryBytes / 1024.0f / 1024.0f, MeshStats.ResolutionScale, *MeshStats.Mesh->GetName(), *MeshStats.Mesh->GetPathName());
	}
}

void FDistanceFieldVolumeTextureAtlas::AddAllocation(FDistanceFieldVolumeTexture* Texture)
{
	InitializeIfNeeded();

	if (!PendingAllocations.Contains(Texture))
	{
		AllocatedCPUDataInBytes += Texture->VolumeData.CompressedDistanceFieldVolume.GetAllocatedSize();
	}

	PendingAllocations.AddUnique(Texture);
	const int32 ThrottleSize = CVarDistFieldThrottleCopyToAtlasInBytes.GetValueOnAnyThread();
	if (ThrottleSize >= 1024)
	{
		Texture->bThrottled = true;
	}
	const FIntVector Size = Texture->VolumeData.Size;
}

void FDistanceFieldVolumeTextureAtlas::RemoveAllocation(FDistanceFieldVolumeTexture* Texture)
{
	InitializeIfNeeded();
	PendingAllocations.Remove(Texture);
	AllocatedCPUDataInBytes -= Texture->VolumeData.CompressedDistanceFieldVolume.GetAllocatedSize();

	if (FailedAllocations.Remove(Texture) > 0)
	{
		FailedAllocatedPixels -= Texture->VolumeData.Size.X * Texture->VolumeData.Size.Y * Texture->VolumeData.Size.Z;;
	}

	if (!CurrentAllocations.Contains(Texture))
	{
		return;
	}
	
	FIntVector Size = Texture->SizeInAtlas;
	int PixelAreaSize = Size.X * Size.Y * Size.Z;

	const FIntVector Min = Texture->GetAllocationMin();
	verify(BlockAllocator.RemoveElement(Min.X, Min.Y, Min.Z, Size.X, Size.Y, Size.Z));
	CurrentAllocations.Remove(Texture);
	AllocatedPixels -= PixelAreaSize;

	FIntVector RemainingSize = Size;
	
	// Check if there is room for a previous failed allocation
	for (int32 Index = 0; Index < FailedAllocations.Num(); Index++)
	{
		FDistanceFieldVolumeTexture* PreviouslyFailedAllocatedTexture = FailedAllocations[Index];
		Size = PreviouslyFailedAllocatedTexture->VolumeData.Size;
		if (Size.X > RemainingSize.X || Size.Y > RemainingSize.Y || Size.Z > RemainingSize.Z)
		{
			continue;
		}
		// Room available. Add texture to pending list
		PendingAllocations.Add(PreviouslyFailedAllocatedTexture);
		FailedAllocations.RemoveAt(Index);
		Index--;
		FailedAllocatedPixels -= PixelAreaSize;

		RemainingSize.X -= Size.X;
		RemainingSize.Y -= Size.Y;
		RemainingSize.Z -= Size.Z;
		
		// Continue iterating if remaining size can support another mesh DF
		if (RemainingSize.X < 4 || RemainingSize.Y < 4 || RemainingSize.Z < 4)
		{
			break;
		}
	}
}

struct FCompareVolumeAllocation
{
	FORCEINLINE bool operator()(const FDistanceFieldVolumeTexture& A, const FDistanceFieldVolumeTexture& B) const
	{
		return A.GetAllocationVolume() > B.GetAllocationVolume();
	}
};

static void CopyToUpdateTextureData
(
	const FIntVector& SrcSize,
	int32 FormatSize,
	const TArray<uint8>& SrcData,
	const FUpdateTexture3DData& UpdateTextureData,
	const FIntVector& DstOffset
)
{
	// Is there any padding? If not, straight memcpy
	if ((UpdateTextureData.DepthPitch * SrcSize.Z) == SrcData.Num())
	{
		FPlatformMemory::Memcpy(UpdateTextureData.Data, SrcData.GetData(), SrcData.Num());
	}
	else
	{

		const int32 SourcePitch = SrcSize.X * FormatSize;
		check(SourcePitch <= (int32)UpdateTextureData.RowPitch);

		for (int32 ZIndex = 0; ZIndex < SrcSize.Z; ZIndex++)
		{
			const int32 DestZIndex = (DstOffset.Z + ZIndex) * UpdateTextureData.DepthPitch + DstOffset.X * FormatSize;
			const int32 SourceZIndex = ZIndex * SrcSize.Y * SourcePitch;

			for (int32 YIndex = 0; YIndex < SrcSize.Y; YIndex++)
			{
				const int32 DestIndex = DestZIndex + (DstOffset.Y + YIndex) * UpdateTextureData.RowPitch;
				const int32 SourceIndex = SourceZIndex + YIndex * SourcePitch;
				check(uint32(DestIndex) + SourcePitch <= UpdateTextureData.DataSizeBytes);
				FMemory::Memcpy((uint8*)&UpdateTextureData.Data[DestIndex], (const uint8*)&(SrcData[SourceIndex]), SourcePitch);
			}
		}
	}
}

void FDistanceFieldVolumeTextureAtlas::UpdateAllocations(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type InFeatureLevel)
{
	SCOPED_NAMED_EVENT(FDistanceFieldVolumeTextureAtlas_UpdateAllocations, FColor::Emerald);

	{
		uint32 TotalSurface = BlockAllocator.GetMaxSizeX() * BlockAllocator.GetMaxSizeY() * BlockAllocator.GetMaxSizeZ();
		CSV_CUSTOM_STAT(DistanceField, DFAtlasPercentageUsage, float((float(AllocatedPixels) / float(TotalSurface))*100.0f), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(DistanceField, DFAtlasMaxX, float(MaxUsedAtlasX), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(DistanceField, DFAtlasMaxY, float(MaxUsedAtlasY), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(DistanceField, DFAtlasMaxZ, float(MaxUsedAtlasZ), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(DistanceField, DFAtlasFailedAllocatedMagaPixels, (float(FailedAllocatedPixels)/1024)/1024, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(DistanceField, DFPersistentCPUMemory, float(AllocatedCPUDataInBytes) / 1024, ECsvCustomStatOp::Set);
	}

	static const auto CVarXY = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.AtlasSizeXY"));
	const int32 AtlasXY = CVarXY->GetValueOnAnyThread();

	static const auto CVarZ = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.AtlasSizeZ"));
	const int32 AtlasZ = CVarZ->GetValueOnAnyThread();
	
	const bool bDiscardCPUData = (CVarDistFieldDiscardCPUData.GetValueOnAnyThread() != 0);

	if (bInitialized && (BlockAllocator.GetMaxSizeX() != AtlasXY || BlockAllocator.GetMaxSizeZ() != AtlasZ))
	{
		// Atlas size has changed (most likely because of a hotfix). Reallocate everything
		GDistanceFieldForceAtlasRealloc = 1;
	}

	if (PendingAllocations.Num() > 0 || GDistanceFieldForceAtlasRealloc != 0)
	{
		const double StartTime = FPlatformTime::Seconds();

		const int32 FormatSize = GPixelFormats[Format].BlockBytes;

		// Sort largest to smallest for best packing
		PendingAllocations.Sort(FCompareVolumeAllocation());
		
		TArray<FDistanceFieldVolumeTexture*> ThrottledAllocations;
		TArray<FDistanceFieldVolumeTexture*>* LocalPendingAllocations = &PendingAllocations;
		int ThrottledCopyCount = 0;
		int PendingCopyCount = PendingAllocations.Num();

		const float RuntimeDownsamplingFactor = CVarDistFieldRuntimeDownsampling->GetFloat();
		const bool bRuntimeDownsampling = FDistanceFieldDownsampling::CanDownsample() && (RuntimeDownsamplingFactor > 0 && RuntimeDownsamplingFactor < 1);

		auto AllocateBlocks = [&]()
		{
			int32 FailedAllocationCount = FailedAllocations.Num();
			for (int32 AllocationIndex = 0; AllocationIndex < LocalPendingAllocations->Num(); AllocationIndex++)
			{
				FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[AllocationIndex];

				if (bDiscardCPUData && Texture->VolumeData.CompressedDistanceFieldVolume.Num() == 0)
				{
					// CPU data has been discarded. Do not UL to the atlas
					LocalPendingAllocations->RemoveAt(AllocationIndex);
					AllocationIndex--;
					continue;
				}

				FIntVector Size = Texture->VolumeData.Size;
				
				if (bRuntimeDownsampling)
				{
					FDistanceFieldDownsampling::GetDownsampledSize(Size, RuntimeDownsamplingFactor, Size);
				}
				
				Texture->SizeInAtlas = Size;
				Texture->bThrottled = false;

				if (!BlockAllocator.AddElement((uint32&)Texture->AtlasAllocationMin.X, (uint32&)Texture->AtlasAllocationMin.Y, (uint32&)Texture->AtlasAllocationMin.Z, Size.X, Size.Y, Size.Z))
				{
					UE_LOG(LogStaticMesh, Warning, TEXT("Failed to allocate %ux%ux%u in distance field atlas. Moved mesh distance field to FailedAllocations list"), Size.X, Size.Y, Size.Z);
					LocalPendingAllocations->RemoveAt(AllocationIndex);
					FailedAllocations.Add(Texture);
					FailedAllocatedPixels += Size.X * Size.Y * Size.Z;
					AllocationIndex--;
				}
				else
				{
					MaxUsedAtlasX = FMath::Max<uint32>(MaxUsedAtlasX, Texture->AtlasAllocationMin.X + Size.X);
					MaxUsedAtlasY = FMath::Max<uint32>(MaxUsedAtlasY, Texture->AtlasAllocationMin.Y + Size.Y);
					MaxUsedAtlasZ = FMath::Max<uint32>(MaxUsedAtlasZ, Texture->AtlasAllocationMin.Z + Size.Z);
					AllocatedPixels += Size.X * Size.Y * Size.Z;
				}
			}

			if (FailedAllocations.Num() > FailedAllocationCount)
			{
				// Sort largest to smallest
				FailedAllocations.Sort(FCompareVolumeAllocation());
			}
		};

		const int32 ThrottleSize = CVarDistFieldThrottleCopyToAtlasInBytes.GetValueOnAnyThread();
		const bool bThrottleUpdateAllocation = ThrottleSize >= 1024;

		if (bThrottleUpdateAllocation)
		{
			int32 CurrentSize = 0;

			for (int32 AllocationIndex = 0; AllocationIndex < PendingAllocations.Num() && CurrentSize < ThrottleSize; ++AllocationIndex)
			{
				FDistanceFieldVolumeTexture* Texture = PendingAllocations[AllocationIndex];
				const FIntVector Size = Texture->VolumeData.Size;
				CurrentSize += Size.X * Size.Y * Size.Z * FormatSize;
				ThrottledAllocations.Add(Texture);
				PendingAllocations.RemoveAt(AllocationIndex);
				AllocationIndex--;
			}

			LocalPendingAllocations = &ThrottledAllocations;
			ThrottledCopyCount = ThrottledAllocations.Num();
			PendingCopyCount = PendingAllocations.Num();
		}

		AllocateBlocks();
			
		static const auto CVarCompress = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.Compress"));
		const bool bDataIsCompressed = CVarCompress->GetValueOnAnyThread() != 0;

		TArray<FDistanceFieldDownsamplingDataTask> DownsamplingTasks;
		TArray<FUpdateTexture3DData> UpdateDataArray;

		if (!VolumeTextureRHI
			|| BlockAllocator.GetSizeX() > VolumeTextureRHI->GetSizeX()
			|| BlockAllocator.GetSizeY() > VolumeTextureRHI->GetSizeY()
			|| BlockAllocator.GetSizeZ() > VolumeTextureRHI->GetSizeZ()
			|| GDistanceFieldForceAtlasRealloc)
		{
			if (CurrentAllocations.Num() > 0)
			{
				// Remove all allocations from the layout so we have a clean slate
				BlockAllocator = FTextureLayout3d(0, 0, 0, AtlasXY, AtlasXY, AtlasZ, false, false);
				
				Generation++;

				MaxUsedAtlasX = 0;
				MaxUsedAtlasY = 0;
				MaxUsedAtlasZ = 0;

				// Re-upload all textures since we had to reallocate
				PendingAllocations.Append(CurrentAllocations);
				if (bThrottleUpdateAllocation)
				{
					PendingAllocations.Append(ThrottledAllocations);
					ThrottledAllocations.Empty();
				}
				CurrentAllocations.Empty();

				// Sort largest to smallest for best packing
				PendingAllocations.Sort(FCompareVolumeAllocation());

				if (bThrottleUpdateAllocation)
				{
					// Throttling during a full realloc when not using the max size of volume texture will make the same blocks being reused over and over
					// allocate everything pending to avoid this
					LocalPendingAllocations = &PendingAllocations;
					ThrottledCopyCount = ThrottledAllocations.Num();
					PendingCopyCount = PendingAllocations.Num();
				}

				// Add all allocations back to the layout
				AllocateBlocks();
			}

			// Fully free the previous atlas memory before allocating a new one
			{
				// Remove last ref, add to deferred delete list
				VolumeTextureRHI = nullptr;
				VolumeTextureUAVRHI = nullptr;

				// Flush commandlist, flush RHI thread, delete deferred resources (GNM Memblock defers further)
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

				// Flush GPU, flush GNM Memblock free
				RHIFlushResources();
			}

			FRHIResourceCreateInfo CreateInfo;

			FIntVector VolumeTextureSize( BlockAllocator.GetSizeX(), BlockAllocator.GetSizeY(), BlockAllocator.GetSizeZ() );
			if( CVarDistFieldForceMaxAtlasSize->GetInt() != 0 )
			{
				VolumeTextureSize = FIntVector( BlockAllocator.GetMaxSizeX(), BlockAllocator.GetMaxSizeY(), BlockAllocator.GetMaxSizeZ() );
			}

			VolumeTextureRHI = RHICreateTexture3D(
				VolumeTextureSize.X, 
				VolumeTextureSize.Y, 
				VolumeTextureSize.Z, 
				Format,
				1,
				TexCreate_ShaderResource | TexCreate_UAV,
				ERHIAccess::SRVMask,
				CreateInfo);
			VolumeTextureUAVRHI = RHICreateUnorderedAccessView(VolumeTextureRHI, 0);

			UE_LOG(LogStaticMesh,Log,TEXT("%s"),*GetSizeString());

			// Full update, coalesce the thousands of small allocations into a single array for RHIUpdateTexture3D
			// D3D12 has a huge alignment requirement which results in 6Gb of staging textures being needed to update a 112Mb atlas in small chunks otherwise (FD3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
			{
				const int32 Pitch = BlockAllocator.GetSizeX() * FormatSize;
				const int32 DepthPitch = BlockAllocator.GetSizeX() * BlockAllocator.GetSizeY() * FormatSize;

				const FUpdateTextureRegion3D UpdateRegion(FIntVector::ZeroValue, FIntVector::ZeroValue, BlockAllocator.GetSize());
				// FUpdateTexture3DData default constructor is private. it might not be used if copy is done on GPU
				// Not sure we want to make it public. let's be conservative, and allocate on stack an array of its size
				uint8 TextureUpdateDataStackMem[sizeof(FUpdateTexture3DData)];
				FUpdateTexture3DData* AtlasUpdateDataPtr = reinterpret_cast<FUpdateTexture3DData*>(&TextureUpdateDataStackMem);

				if (!bRuntimeDownsampling)
				{
					*AtlasUpdateDataPtr = RHIBeginUpdateTexture3D(VolumeTextureRHI, 0, UpdateRegion);
					// This fills in any holes in the update region so we don't upload garbage data to the GPU.
					FMemory::Memzero(AtlasUpdateDataPtr->Data, AtlasUpdateDataPtr->DataSizeBytes);
				}
				else
				{
					UpdateDataArray.Empty(LocalPendingAllocations->Num());
					UpdateDataArray.AddUninitialized(LocalPendingAllocations->Num());
					DownsamplingTasks.AddDefaulted(LocalPendingAllocations->Num());

					for (int32 Idx = 0; Idx < LocalPendingAllocations->Num(); ++Idx)
					{
						FDistanceFieldDownsamplingDataTask& DownsamplingTask = DownsamplingTasks[Idx];
						FUpdateTexture3DData& UpdateData = UpdateDataArray[Idx];
						const FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[Idx];
						FDistanceFieldDownsampling::FillDownsamplingTask(Texture->VolumeData.Size, Texture->SizeInAtlas, Texture->GetAllocationMin(), Format, DownsamplingTask, UpdateData);
					}
				}

				// @todo arne @todo beni: can you verify i properly merged the optimizations here and in Main?
				ParallelFor(LocalPendingAllocations->Num(), [FormatSize, this, bDataIsCompressed, bRuntimeDownsampling, bDiscardCPUData, LocalPendingAllocations, AtlasUpdateDataPtr, &UpdateDataArray](int32 AllocationIndex)
				{
					TArray<uint8> UncompressedData;
					FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[AllocationIndex];
					FIntVector Size = Texture->VolumeData.Size;
					
					const TArray<uint8>* SourceDataPtr = NULL;
					if (bDataIsCompressed)
					{
						const int32 UncompressedSize = Size.X * Size.Y * Size.Z * FormatSize;
						UncompressedData.Reset(UncompressedSize);
						UncompressedData.AddUninitialized(UncompressedSize);

						verify(FCompression::UncompressMemory(NAME_LZ4, UncompressedData.GetData(), UncompressedSize, Texture->VolumeData.CompressedDistanceFieldVolume.GetData(), Texture->VolumeData.CompressedDistanceFieldVolume.Num()));

						SourceDataPtr = &UncompressedData;
					}
					else
					{
						// Update the volume texture atlas
						check(Texture->VolumeData.CompressedDistanceFieldVolume.Num() == Size.X * Size.Y * Size.Z * FormatSize);
						SourceDataPtr = &Texture->VolumeData.CompressedDistanceFieldVolume;
					}

					FIntVector DstOffset;
					FUpdateTexture3DData* TextureUpdateDataPtr;
					
					if (bRuntimeDownsampling)
					{
						DstOffset = FIntVector::ZeroValue;
						TextureUpdateDataPtr = &UpdateDataArray[AllocationIndex];
					}
					else
					{
						DstOffset = Texture->GetAllocationMin();
						TextureUpdateDataPtr = AtlasUpdateDataPtr;
					}
					
					CopyToUpdateTextureData(Size, FormatSize, *SourceDataPtr, *TextureUpdateDataPtr, DstOffset);

					if (bDiscardCPUData)
					{
						AllocatedCPUDataInBytes -= Texture->VolumeData.CompressedDistanceFieldVolume.GetAllocatedSize(); 
						Texture->DiscardCPUData();
					}

				}, !GDistanceFieldParallelAtlasUpdate, false);

				if (!bRuntimeDownsampling)
				{
					RHIEndUpdateTexture3D(*AtlasUpdateDataPtr);
				}
			}
		}
		else
		{
			const int32 NumUpdates = LocalPendingAllocations->Num();
			UpdateDataArray.Empty(NumUpdates);
			UpdateDataArray.AddUninitialized(NumUpdates);
			
			// Allocate upload buffers
			if (!bRuntimeDownsampling)
			{
				for (int32 Idx = 0; Idx < NumUpdates; ++Idx)
				{
					FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[Idx];

					const FUpdateTextureRegion3D UpdateRegion(Texture->AtlasAllocationMin, FIntVector::ZeroValue, Texture->SizeInAtlas);

					UpdateDataArray[Idx] = RHIBeginUpdateTexture3D(VolumeTextureRHI, 0, UpdateRegion);

					check(!!UpdateDataArray[Idx].Data);
					check(static_cast<int32>(UpdateDataArray[Idx].RowPitch) >= Texture->SizeInAtlas.X * FormatSize);
					check(static_cast<int32>(UpdateDataArray[Idx].DepthPitch) >= Texture->SizeInAtlas.X * Texture->SizeInAtlas.Y * FormatSize);
				}
			}
			else
			{
				DownsamplingTasks.Empty(NumUpdates);
				DownsamplingTasks.AddDefaulted(NumUpdates);

				for (int32 Idx = 0; Idx < NumUpdates; ++Idx)
				{
					FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[Idx];
					FDistanceFieldDownsampling::FillDownsamplingTask(Texture->VolumeData.Size, Texture->SizeInAtlas, Texture->GetAllocationMin(), Format, DownsamplingTasks[Idx], UpdateDataArray[Idx]);
				}
			}

			// Copy data to upload buffers and decompress source data if necessary
			ParallelFor(
				NumUpdates,
				[this, FormatSize, bDataIsCompressed, bRuntimeDownsampling, bDiscardCPUData, &UpdateDataArray, LocalPendingAllocations](int32 Idx)
				{
					FUpdateTexture3DData& UpdateData = UpdateDataArray[Idx];
					FDistanceFieldVolumeTexture* Texture = (*LocalPendingAllocations)[Idx];
					const FIntVector& Size = Texture->VolumeData.Size;

					if (!bDataIsCompressed)
					{
						CopyToUpdateTextureData(Size, FormatSize, Texture->VolumeData.CompressedDistanceFieldVolume, UpdateDataArray[Idx], FIntVector::ZeroValue);
					}
					else
					{
						const int32 UncompressedSize = Size.X * Size.Y * Size.Z * FormatSize;
						TArray<uint8> UncompressedData;
						UncompressedData.Empty(UncompressedSize);
						UncompressedData.AddUninitialized(UncompressedSize);
						verify(FCompression::UncompressMemory(NAME_LZ4, UncompressedData.GetData(), UncompressedSize, Texture->VolumeData.CompressedDistanceFieldVolume.GetData(), Texture->VolumeData.CompressedDistanceFieldVolume.Num()));
						
						CopyToUpdateTextureData(Size, FormatSize, UncompressedData, UpdateDataArray[Idx], FIntVector::ZeroValue);
					}

					if (bDiscardCPUData)
					{
						AllocatedCPUDataInBytes -= Texture->VolumeData.CompressedDistanceFieldVolume.GetAllocatedSize();
						Texture->DiscardCPUData();
					}

				}, !GDistanceFieldParallelAtlasUpdate, false);

			if (!bRuntimeDownsampling)
			{
				// For some RHIs, this has the advantage of reducing transition barriers
				RHIEndMultiUpdateTexture3D(UpdateDataArray);
			}
		}

		CurrentAllocations.Append(*LocalPendingAllocations);
		LocalPendingAllocations->Empty();

		if (DownsamplingTasks.Num() > 0)
		{
			FDistanceFieldDownsampling::DispatchDownsampleTasks(RHICmdList, VolumeTextureUAVRHI, InFeatureLevel, DownsamplingTasks, UpdateDataArray);
		}

		const double EndTime = FPlatformTime::Seconds();
		const float UpdateDurationMs = (float)(EndTime - StartTime) * 1000.0f;

		if (UpdateDurationMs > 10.0f)
		{
			UE_LOG(LogStaticMesh,Verbose,TEXT("FDistanceFieldVolumeTextureAtlas::UpdateAllocations took %.1fms"), UpdateDurationMs);
		}
		GDistanceFieldForceAtlasRealloc = 0;
	}
}

FDistanceFieldVolumeTexture::~FDistanceFieldVolumeTexture()
{
	if (FApp::CanEverRender())
	{
		// Make sure we have been properly removed from the atlas before deleting
		check(!bReferencedByAtlas);
	}
}

void FDistanceFieldVolumeTexture::Initialize(UStaticMesh* InStaticMesh)
{
	if (IsValidDistanceFieldVolume())
	{
		StaticMesh = InStaticMesh;

		bReferencedByAtlas = true;

		FDistanceFieldVolumeTexture* DistanceFieldVolumeTexture = this;
		ENQUEUE_RENDER_COMMAND(AddAllocation)(
			[DistanceFieldVolumeTexture](FRHICommandList& RHICmdList)
			{
				GDistanceFieldVolumeTextureAtlas.AddAllocation(DistanceFieldVolumeTexture);
			});
	}
}

void FDistanceFieldVolumeTexture::Release()
{
	if (bReferencedByAtlas)
	{
		StaticMesh = NULL;

		bReferencedByAtlas = false;

		FDistanceFieldVolumeTexture* DistanceFieldVolumeTexture = this;
		ENQUEUE_RENDER_COMMAND(ReleaseAllocation)(
			[DistanceFieldVolumeTexture](FRHICommandList& RHICmdList)
			{
				GDistanceFieldVolumeTextureAtlas.RemoveAllocation(DistanceFieldVolumeTexture);
			});
	}
}

void FDistanceFieldVolumeTexture::DiscardCPUData()
{
	VolumeData.CompressedDistanceFieldVolume.Empty();
}

FIntVector FDistanceFieldVolumeTexture::GetAllocationSize() const
{
	return VolumeData.Size;
}

bool FDistanceFieldVolumeTexture::IsValidDistanceFieldVolume() const
{
	return VolumeData.Size.GetMax() > 0;
}

FDistanceFieldAsyncQueue* GDistanceFieldAsyncQueue = NULL;

#if WITH_EDITOR

// DDC key for distance field data, must be changed when modifying the generation code or data format
#define DISTANCEFIELD_DERIVEDDATA_VER TEXT("6CBBF5D788CA4699B140BAEC2A3B6B67")

FString BuildDistanceFieldDerivedDataKey(const FString& InMeshKey)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.MaxPerMeshResolution"));
	const int32 PerMeshMax = CVar->GetValueOnAnyThread();
	const FString PerMeshMaxString = PerMeshMax == 128 ? TEXT("") : FString::Printf(TEXT("_%u"), PerMeshMax);

	static const auto CVarDensity = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.DistanceFields.DefaultVoxelDensity"));
	const float VoxelDensity = CVarDensity->GetValueOnAnyThread();
	const FString VoxelDensityString = VoxelDensity == .1f ? TEXT("") : FString::Printf(TEXT("_%.3f"), VoxelDensity);

	static const auto CVarCompress = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.Compress"));
	const bool bCompress = CVarCompress->GetValueOnAnyThread() != 0;
	const FString CompressString = bCompress ? TEXT("") : TEXT("_uc");

	static const auto CVarEightBit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.EightBit"));
	const bool bEightBitFixedPoint = CVarEightBit->GetValueOnAnyThread() != 0;
	const FString FormatString = bEightBitFixedPoint ? TEXT("_8u") : TEXT("");

	static const auto CVarEmbree = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.UseEmbree"));
	const bool bUseEmbree = CVarEmbree->GetValueOnAnyThread() != 0;
	const FString EmbreeString = bUseEmbree ? TEXT("_e") : TEXT("");

	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("DIST"),
		*FString::Printf(TEXT("%s_%s%s%s%s%s%s"), *InMeshKey, DISTANCEFIELD_DERIVEDDATA_VER, *PerMeshMaxString, *VoxelDensityString, *CompressString, *FormatString, *EmbreeString),
		TEXT(""));
}

#endif

#if WITH_EDITORONLY_DATA

void FDistanceFieldVolumeData::CacheDerivedData(const FString& InDDCKey, UStaticMesh* Mesh, UStaticMesh* GenerateSource, float DistanceFieldResolutionScale, bool bGenerateDistanceFieldAsIfTwoSided)
{
	TArray<uint8> DerivedData;

	COOK_STAT(auto Timer = DistanceFieldCookStats::UsageStats.TimeSyncWork());
	if (GetDerivedDataCacheRef().GetSynchronous(*InDDCKey, DerivedData, Mesh->GetPathName()))
	{
		COOK_STAT(Timer.AddHit(DerivedData.Num()));
		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);
		Ar << *this;
	}
	else
	{
		// We don't actually build the resource until later, so only track the cycles used here.
		COOK_STAT(Timer.TrackCyclesOnly());
		FAsyncDistanceFieldTask* NewTask = new FAsyncDistanceFieldTask;
		NewTask->DDCKey = InDDCKey;
		check(Mesh && GenerateSource);
		NewTask->StaticMesh = Mesh;
		NewTask->GenerateSource = GenerateSource;
		NewTask->DistanceFieldResolutionScale = DistanceFieldResolutionScale;
		NewTask->bGenerateDistanceFieldAsIfTwoSided = bGenerateDistanceFieldAsIfTwoSided;
		NewTask->GeneratedVolumeData = new FDistanceFieldVolumeData();

		for (int32 MaterialIndex = 0; MaterialIndex < Mesh->StaticMaterials.Num(); MaterialIndex++)
		{
			// Default material blend mode
			EBlendMode BlendMode = BLEND_Opaque;

			if (Mesh->StaticMaterials[MaterialIndex].MaterialInterface)
			{
				BlendMode = Mesh->StaticMaterials[MaterialIndex].MaterialInterface->GetBlendMode();
			}

			NewTask->MaterialBlendModes.Add(BlendMode);
		}

		GDistanceFieldAsyncQueue->AddTask(NewTask);
	}
}

#endif

int32 GUseAsyncDistanceFieldBuildQueue = 1;
static FAutoConsoleVariableRef CVarAOAsyncBuildQueue(
	TEXT("r.AOAsyncBuildQueue"),
	GUseAsyncDistanceFieldBuildQueue,
	TEXT("Whether to asynchronously build distance field volume data from meshes."),
	ECVF_Default | ECVF_ReadOnly
	);

class FBuildDistanceFieldThreadRunnable : public FRunnable
{
public:
	/** Initialization constructor. */
	FBuildDistanceFieldThreadRunnable(
		FDistanceFieldAsyncQueue* InAsyncQueue
		)
		: NextThreadIndex(0)
		, AsyncQueue(*InAsyncQueue)
		, Thread(nullptr)
		, bIsRunning(false)
		, bForceFinish(false)
	{}

	virtual ~FBuildDistanceFieldThreadRunnable()
	{
		check(!bIsRunning);
	}

	// FRunnable interface.
	virtual bool Init() { return true; }
	virtual void Exit() { bIsRunning = false; }
	virtual void Stop() { bForceFinish = true; }
	virtual uint32 Run();

	void Launch()
	{
		check(!bIsRunning);

		// Calling Reset will call Kill which in turn will call Stop and set bForceFinish to true.
		Thread.Reset();

		// Now we can set bForceFinish to false without being overwritten by the old thread shutting down.
		bForceFinish = false;
		Thread.Reset(FRunnableThread::Create(this, *FString::Printf(TEXT("BuildDistanceFieldThread%u"), NextThreadIndex), 0, TPri_Normal, FPlatformAffinity::GetPoolThreadMask()));

		// Set this now before exiting so that IsRunning() returns true without having to wait on the thread to be completely started.
		bIsRunning = true;
		NextThreadIndex++;
	}

	inline bool IsRunning() { return bIsRunning; }

private:

	int32 NextThreadIndex;

	FDistanceFieldAsyncQueue& AsyncQueue;

	/** The runnable thread */
	TUniquePtr<FRunnableThread> Thread;

	TUniquePtr<FQueuedThreadPool> WorkerThreadPool;

	volatile bool bIsRunning;
	volatile bool bForceFinish;
};

static FQueuedThreadPool* CreateWorkerThreadPool()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateWorkerThreadPool)
	const int32 NumThreads = FMath::Max<int32>(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2, 1);
	FQueuedThreadPool* WorkerThreadPool = FQueuedThreadPool::Allocate();
	WorkerThreadPool->Create(NumThreads, 32 * 1024, TPri_BelowNormal);
	return WorkerThreadPool;
}

uint32 FBuildDistanceFieldThreadRunnable::Run()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildDistanceFieldThreadRunnable::Run)

	bool bHasWork = true;

	// Do not exit right away if no work to do as it often leads to stop and go problems
	// when tasks are being queued at a slower rate than the processor capability to process them.
	const uint64 ExitAfterIdleCycle = static_cast<uint64>(10.0 / FPlatformTime::GetSecondsPerCycle64()); // 10s

	uint64 LastWorkCycle = FPlatformTime::Cycles64();
	while (!bForceFinish &&  (bHasWork || (FPlatformTime::Cycles64() - LastWorkCycle) < ExitAfterIdleCycle))
	{
		// LIFO build order, since meshes actually visible in a map are typically loaded last
		FAsyncDistanceFieldTask* Task = AsyncQueue.TaskQueue.Pop();

		FQueuedThreadPool* ThreadPool = nullptr;
		
#if WITH_EDITOR
		ThreadPool = GLargeThreadPool;
#endif

		if (Task)
		{
			if (!ThreadPool)
			{
				if (!WorkerThreadPool)
				{
					WorkerThreadPool.Reset(CreateWorkerThreadPool());
				}

				ThreadPool = WorkerThreadPool.Get();
			}

			AsyncQueue.Build(Task, *ThreadPool);
			LastWorkCycle = FPlatformTime::Cycles64();

			bHasWork = true;
		}
		else
		{
			bHasWork = false;
			FPlatformProcess::Sleep(.01f);
		}
	}

	WorkerThreadPool = nullptr;

	return 0;
}

FAsyncDistanceFieldTask::FAsyncDistanceFieldTask()
	: StaticMesh(nullptr)
	, GenerateSource(nullptr)
	, DistanceFieldResolutionScale(0.0f)
	, bGenerateDistanceFieldAsIfTwoSided(false)
	, GeneratedVolumeData(nullptr)
{
}


FDistanceFieldAsyncQueue::FDistanceFieldAsyncQueue() 
{
#if WITH_EDITOR
	MeshUtilities = NULL;
#endif

	ThreadRunnable = MakeUnique<FBuildDistanceFieldThreadRunnable>(this);
}

FDistanceFieldAsyncQueue::~FDistanceFieldAsyncQueue()
{
}

void FDistanceFieldAsyncQueue::AddTask(FAsyncDistanceFieldTask* Task)
{
#if WITH_EDITOR
	if (!MeshUtilities)
	{
		MeshUtilities = &FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
	}
	
	{
		// Array protection when called from multiple threads
		FScopeLock Lock(&CriticalSection);
		ReferencedTasks.Add(Task);
	}

	// If we're already in worker threads, we have to use async tasks
	// to avoid crashing in the Build function.
	// Also protects from creating too many thread pools when already parallel.
	if (GUseAsyncDistanceFieldBuildQueue || !IsInGameThread())
	{
		TaskQueue.Push(Task);

		// Logic protection when called from multiple threads
		FScopeLock Lock(&CriticalSection);
		if (!ThreadRunnable->IsRunning())
		{
			ThreadRunnable->Launch();
		}
	}
	else
	{
		TUniquePtr<FQueuedThreadPool> WorkerThreadPool(CreateWorkerThreadPool());
		Build(Task, *WorkerThreadPool);
	}
#else
	UE_LOG(LogStaticMesh,Fatal,TEXT("Tried to build a distance field without editor support (this should have been done during cooking)"));
#endif
}

void FDistanceFieldAsyncQueue::BlockUntilBuildComplete(UStaticMesh* StaticMesh, bool bWarnIfBlocked)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldAsyncQueue::BlockUntilBuildComplete)

	// We will track the wait time here, but only the cycles used.
	// This function is called whether or not an async task is pending, 
	// so we have to look elsewhere to properly count how many resources have actually finished building.
	COOK_STAT(auto Timer = DistanceFieldCookStats::UsageStats.TimeAsyncWait());
	COOK_STAT(Timer.TrackCyclesOnly());
	bool bReferenced = false;
	bool bHadToBlock = false;
	double StartTime = 0;

	do 
	{
		ProcessAsyncTasks();

		bReferenced = false;

		for (int TaskIndex = 0; TaskIndex < ReferencedTasks.Num(); TaskIndex++)
		{
			bReferenced = bReferenced || ReferencedTasks[TaskIndex]->StaticMesh == StaticMesh;
			bReferenced = bReferenced || ReferencedTasks[TaskIndex]->GenerateSource == StaticMesh;
		}

		if (bReferenced)
		{
			if (!bHadToBlock)
			{
				StartTime = FPlatformTime::Seconds();
			}

			bHadToBlock = true;
			FPlatformProcess::Sleep(.01f);
		}
	} 
	while (bReferenced);

	if (bHadToBlock &&
		bWarnIfBlocked
#if WITH_EDITOR
		&& !FAutomationTestFramework::Get().GetCurrentTest() // HACK - Don't output this warning during automation test
#endif
		)
	{
		UE_LOG(LogStaticMesh, Display, TEXT("Main thread blocked for %.3fs for async distance field build of %s to complete!  This can happen if the mesh is rebuilt excessively."),
			(float)(FPlatformTime::Seconds() - StartTime), 
			*StaticMesh->GetName());
	}
}

void FDistanceFieldAsyncQueue::BlockUntilAllBuildsComplete()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldAsyncQueue::BlockUntilAllBuildsComplete)
	do 
	{
		ProcessAsyncTasks();
		FPlatformProcess::Sleep(.01f);
	} 
	while (GetNumOutstandingTasks() > 0);
}

void FDistanceFieldAsyncQueue::Build(FAsyncDistanceFieldTask* Task, FQueuedThreadPool& ThreadPool)
{
#if WITH_EDITOR
	// Editor 'force delete' can null any UObject pointers which are seen by reference collecting (eg FProperty or serialized)
	if (Task->StaticMesh && Task->GenerateSource)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldAsyncQueue::Build)

		const FStaticMeshLODResources& LODModel = Task->GenerateSource->RenderData->LODResources[0];

		MeshUtilities->GenerateSignedDistanceFieldVolumeData(
			Task->StaticMesh->GetName(),
			LODModel,
			ThreadPool,
			Task->MaterialBlendModes,
			Task->GenerateSource->RenderData->Bounds,
			Task->DistanceFieldResolutionScale,
			Task->bGenerateDistanceFieldAsIfTwoSided,
			*Task->GeneratedVolumeData);
	}

    CompletedTasks.Push(Task);

#endif
}

void FDistanceFieldAsyncQueue::AddReferencedObjects(FReferenceCollector& Collector)
{	
	for (int TaskIndex = 0; TaskIndex < ReferencedTasks.Num(); TaskIndex++)
	{
		// Make sure none of the UObjects referenced by the async tasks are GC'ed during the task
		Collector.AddReferencedObject(ReferencedTasks[TaskIndex]->StaticMesh);
		Collector.AddReferencedObject(ReferencedTasks[TaskIndex]->GenerateSource);
	}
}

FString FDistanceFieldAsyncQueue::GetReferencerName() const
{
	return TEXT("FDistanceFieldAsyncQueue");
}

void FDistanceFieldAsyncQueue::ProcessAsyncTasks()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldAsyncQueue::ProcessAsyncTasks)
#if WITH_EDITOR
	TArray<FAsyncDistanceFieldTask*> LocalCompletedTasks;
	CompletedTasks.PopAll(LocalCompletedTasks);

	for (int TaskIndex = 0; TaskIndex < LocalCompletedTasks.Num(); TaskIndex++)
	{
		// We want to count each resource built from a DDC miss, so count each iteration of the loop separately.
		COOK_STAT(auto Timer = DistanceFieldCookStats::UsageStats.TimeSyncWork());
		FAsyncDistanceFieldTask* Task = LocalCompletedTasks[TaskIndex];

		ReferencedTasks.Remove(Task);

		// Editor 'force delete' can null any UObject pointers which are seen by reference collecting (eg FProperty or serialized)
		if (Task->StaticMesh)
		{
			Task->GeneratedVolumeData->VolumeTexture.Initialize(Task->StaticMesh);
			FDistanceFieldVolumeData* OldVolumeData = Task->StaticMesh->RenderData->LODResources[0].DistanceFieldData;

			// Renderstates are not initialized between UStaticMesh::PreEditChange() and UStaticMesh::PostEditChange()
			if (Task->StaticMesh->RenderData->IsInitialized())
			{
				// Cause all components using this static mesh to get re-registered, which will recreate their proxies and primitive uniform buffers
				FStaticMeshComponentRecreateRenderStateContext RecreateRenderStateContext(Task->StaticMesh, false);

				// Assign the new volume data
				Task->StaticMesh->RenderData->LODResources[0].DistanceFieldData = Task->GeneratedVolumeData;
			}
			else
			{
				// Assign the new volume data
				Task->StaticMesh->RenderData->LODResources[0].DistanceFieldData = Task->GeneratedVolumeData;
			}

			OldVolumeData->VolumeTexture.Release();

			// Rendering thread may still be referencing the old one, use the deferred cleanup interface to delete it next frame when it is safe
			BeginCleanup(OldVolumeData);

			{
				TArray<uint8> DerivedData;
				// Save built distance field volume to DDC
				FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
				Ar << *(Task->StaticMesh->RenderData->LODResources[0].DistanceFieldData);
				GetDerivedDataCacheRef().Put(*Task->DDCKey, DerivedData, Task->StaticMesh->GetPathName());
				COOK_STAT(Timer.AddMiss(DerivedData.Num()));
			}
		}

		delete Task;
	}

	if (ReferencedTasks.Num() > 0 && !ThreadRunnable->IsRunning())
	{
		ThreadRunnable->Launch();
	}
#endif
}

void FDistanceFieldAsyncQueue::Shutdown()
{
	ThreadRunnable->Stop();
	bool bLogged = false;

	while (ThreadRunnable->IsRunning())
	{
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogStaticMesh,Log,TEXT("Abandoning remaining async distance field tasks for shutdown"));
		}
		FPlatformProcess::Sleep(0.01f);
	}
}

FLandscapeTextureAtlas::FLandscapeTextureAtlas(ESubAllocType InSubAllocType)
	: MaxDownSampleLevel(0)
	, Generation(0)
	, SubAllocType(InSubAllocType)
{}

void FLandscapeTextureAtlas::InitializeIfNeeded()
{
	const bool bHeight = SubAllocType == SAT_Height;
	const uint32 LocalTileSize = bHeight ? GHeightFieldAtlasTileSize : GHFVisibilityAtlasTileSize;
	const uint32 LocalDimInTiles = bHeight ? GHeightFieldAtlasDimInTiles : GHFVisibilityAtlasDimInTiles;
	const uint32 LocalDownSampleLevel = bHeight ? GHeightFieldAtlasDownSampleLevel : GHFVisibilityAtlasDownSampleLevel;

	if (!AtlasTextureRHI
		|| AddrSpaceAllocator.TileSize != LocalTileSize
		|| AddrSpaceAllocator.DimInTiles != LocalDimInTiles
		|| MaxDownSampleLevel != LocalDownSampleLevel)
	{
		AddrSpaceAllocator.Init(LocalTileSize, 1, LocalDimInTiles);

		for (int32 Idx = 0; Idx < PendingStreamingTextures.Num(); ++Idx)
		{
			UTexture2D* Texture = PendingStreamingTextures[Idx];
			Texture->bForceMiplevelsToBeResident = false;
		}
		PendingStreamingTextures.Empty();

		for (TSet<FAllocation>::TConstIterator It(CurrentAllocations); It; ++It)
		{
			check(!PendingAllocations.Contains(*It));
			PendingAllocations.Add(*It);
		}

		CurrentAllocations.Reset();

		const uint32 SizeX = AddrSpaceAllocator.DimInTexels;
		const uint32 SizeY = AddrSpaceAllocator.DimInTexels;
		const ETextureCreateFlags Flags = TexCreate_ShaderResource | TexCreate_UAV;
		const EPixelFormat Format = bHeight ? PF_R8G8 : PF_G8;
		FRHIResourceCreateInfo CreateInfo(bHeight ? TEXT("HeightFieldAtlas") : TEXT("VisibilityAtlas"));

		AtlasTextureRHI = RHICreateTexture2D(SizeX, SizeY, Format, 1, 1, Flags, CreateInfo);
		AtlasUAVRHI = RHICreateUnorderedAccessView(AtlasTextureRHI, 0);

		MaxDownSampleLevel = LocalDownSampleLevel;
		++Generation;
	}
}

void FLandscapeTextureAtlas::AddAllocation(UTexture2D* Texture, uint32 VisibilityChannel)
{
	check(Texture);

	FAllocation* Found = CurrentAllocations.Find(Texture);
	if (Found)
	{
		++Found->RefCount;
		return;
	}

	Found = FailedAllocations.Find(Texture);
	if (Found)
	{
		++Found->RefCount;
		return;
	}

	Found = PendingAllocations.Find(Texture);
	if (Found)
	{
		++Found->RefCount;
	}
	else
	{
		PendingAllocations.Add(FAllocation(Texture, VisibilityChannel));
	}
}

void FLandscapeTextureAtlas::RemoveAllocation(UTexture2D* Texture)
{
	FSetElementId Id = PendingAllocations.FindId(Texture);
	if (Id.IsValidId())
	{
		check(PendingAllocations[Id].RefCount);
		if (!--PendingAllocations[Id].RefCount)
		{
			check(!PendingStreamingTextures.Contains(Texture));
			PendingAllocations.Remove(Id);
		}
		return;
	}

	Id = FailedAllocations.FindId(Texture);
	if (Id.IsValidId())
	{
		check(FailedAllocations[Id].RefCount);
		if (!--FailedAllocations[Id].RefCount)
		{
			check(!PendingStreamingTextures.Contains(Texture));
			FailedAllocations.Remove(Id);
		}
		return;
	}

	Id = CurrentAllocations.FindId(Texture);
	if (Id.IsValidId())
	{
		FAllocation& Allocation = CurrentAllocations[Id];
		check(Allocation.RefCount && Allocation.Handle != INDEX_NONE);
		if (!--Allocation.RefCount)
		{
			AddrSpaceAllocator.Free(Allocation.Handle);
			PendingStreamingTextures.Remove(Texture);
			CurrentAllocations.Remove(Id);
		}
	}
}

class FUploadLandscapeTextureToAtlasCS : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FSharedParameters, )
		SHADER_PARAMETER(FUintVector4, UpdateRegionOffsetAndSize)
		SHADER_PARAMETER(FVector4, SourceScaleBias)
		SHADER_PARAMETER(uint32, SourceMipBias)
		SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	FUploadLandscapeTextureToAtlasCS() = default;

	FUploadLandscapeTextureToAtlasCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
	}

	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
};

class FUploadHeightFieldToAtlasCS : public FUploadLandscapeTextureToAtlasCS
{
	DECLARE_GLOBAL_SHADER(FUploadHeightFieldToAtlasCS);
	SHADER_USE_PARAMETER_STRUCT(FUploadHeightFieldToAtlasCS, FUploadLandscapeTextureToAtlasCS);

	using FPermutationDomain = FShaderPermutationNone;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_STRUCT_INCLUDE(FUploadLandscapeTextureToAtlasCS::FSharedParameters, SharedParams)
		SHADER_PARAMETER_UAV(RWTexture2D<float2>, RWHeightFieldAtlas)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FUploadHeightFieldToAtlasCS, "/Engine/Private/HeightFieldAtlasManagement.usf", "UploadHeightFieldToAtlasCS", SF_Compute);

class FUploadVisibilityToAtlasCS : public FUploadLandscapeTextureToAtlasCS
{
	DECLARE_GLOBAL_SHADER(FUploadVisibilityToAtlasCS);
	SHADER_USE_PARAMETER_STRUCT(FUploadVisibilityToAtlasCS, FUploadLandscapeTextureToAtlasCS);

	using FPermutationDomain = FShaderPermutationNone;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FUploadLandscapeTextureToAtlasCS::FSharedParameters, SharedParams)
		SHADER_PARAMETER(FVector4, VisibilityChannelMask)
		SHADER_PARAMETER_UAV(RWTexture2D<float>, RWVisibilityAtlas)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FUploadVisibilityToAtlasCS, "/Engine/Private/HeightFieldAtlasManagement.usf", "UploadVisibilityToAtlasCS", SF_Compute);

uint32 FLandscapeTextureAtlas::CalculateDownSampleLevel(uint32 SizeX, uint32 SizeY) const
{
	const uint32 TileSize = AddrSpaceAllocator.TileSize;

	for (uint32 CurLevel = 0; CurLevel <= MaxDownSampleLevel; ++CurLevel)
	{
		const uint32 DownSampledSizeX = SizeX >> CurLevel;
		const uint32 DownSampledSizeY = SizeY >> CurLevel;

		if (DownSampledSizeX <= TileSize && DownSampledSizeY <= TileSize)
		{
			return CurLevel;
		}
	}

	return MaxDownSampleLevel;
}

void FLandscapeTextureAtlas::UpdateAllocations(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type InFeatureLevel)
{
	InitializeIfNeeded();

	TArray<FPendingUpload, TInlineAllocator<8>> PendingUploads;

	auto AllocSortPred = [](const FAllocation& A, const FAllocation& B)
	{
		const int32 SizeA = FMath::Max(A.SourceTexture->GetSizeX(), A.SourceTexture->GetSizeY());
		const int32 SizeB = FMath::Max(B.SourceTexture->GetSizeX(), B.SourceTexture->GetSizeY());
		return SizeA < SizeB;
	};

	for (int32 Idx = 0; Idx < PendingStreamingTextures.Num(); ++Idx)
	{
		UTexture2D* SourceTexture = PendingStreamingTextures[Idx];
		const uint32 SizeX = SourceTexture->GetSizeX();
		const uint32 SizeY = SourceTexture->GetSizeY();
		const uint32 DownSampleLevel = CalculateDownSampleLevel(SizeX, SizeY);
		const uint32 NumMissingMips = SourceTexture->GetNumMips() - SourceTexture->GetNumResidentMips();

		if (NumMissingMips <= DownSampleLevel)
		{
			SourceTexture->bForceMiplevelsToBeResident = false;
			const uint32 SourceMipBias = DownSampleLevel - NumMissingMips;
			const FAllocation* Allocation = CurrentAllocations.Find(SourceTexture);
			check(Allocation && Allocation->Handle != INDEX_NONE);
			const uint32 VisibilityChannel = Allocation->VisibilityChannel;
			PendingUploads.Emplace(SourceTexture, SizeX >> DownSampleLevel, SizeY >> DownSampleLevel, SourceMipBias, Allocation->Handle, VisibilityChannel);
			PendingStreamingTextures.RemoveAtSwap(Idx--);
		}
	}

	if (PendingAllocations.Num())
	{
		PendingAllocations.Sort(AllocSortPred);
		bool bAllocFailed = false;

		for (TSet<FAllocation>::TConstIterator It(PendingAllocations); It; ++It)
		{
			FAllocation TmpAllocation = *It;

			if (!bAllocFailed)
			{
				UTexture2D* SourceTexture = TmpAllocation.SourceTexture;
				const uint32 SizeX = SourceTexture->GetSizeX();
				const uint32 SizeY = SourceTexture->GetSizeY();
				const uint32 DownSampleLevel = CalculateDownSampleLevel(SizeX, SizeY);
				const uint32 DownSampledSizeX = SizeX >> DownSampleLevel;
				const uint32 DownSampledSizeY = SizeY >> DownSampleLevel;
				const uint32 Handle = AddrSpaceAllocator.Alloc(DownSampledSizeX, DownSampledSizeY);
				const uint32 VisibilityChannel = TmpAllocation.VisibilityChannel;

				if (Handle == INDEX_NONE)
				{
					FailedAllocations.Add(TmpAllocation);
					bAllocFailed = true;
					continue;
				}

				const uint32 NumMissingMips = SourceTexture->GetNumMips() - SourceTexture->GetNumResidentMips();
				const uint32 SourceMipBias = NumMissingMips > DownSampleLevel ? 0 : DownSampleLevel - NumMissingMips;

				if (NumMissingMips > DownSampleLevel)
				{
					SourceTexture->bForceMiplevelsToBeResident = true;
					check(!PendingStreamingTextures.Contains(SourceTexture));
					PendingStreamingTextures.Add(SourceTexture);
				}

				TmpAllocation.Handle = Handle;
				CurrentAllocations.Add(TmpAllocation);
				PendingUploads.Emplace(SourceTexture, DownSampledSizeX, DownSampledSizeY, SourceMipBias, Handle, VisibilityChannel);
			}
			else
			{
				FailedAllocations.Add(TmpAllocation);
			}
		}

		if (bAllocFailed)
		{
			FailedAllocations.Sort(AllocSortPred);
		}
		PendingAllocations.Empty();
	}

	if (FailedAllocations.Num())
	{
		for (TSet<FAllocation>::TIterator It(FailedAllocations); It; ++It)
		{
			FAllocation TmpAllocation = *It;
			UTexture2D* SourceTexture = TmpAllocation.SourceTexture;
			const uint32 SizeX = SourceTexture->GetSizeX();
			const uint32 SizeY = SourceTexture->GetSizeY();
			const uint32 DownSampleLevel = CalculateDownSampleLevel(SizeX, SizeY);
			const uint32 DownSampledSizeX = SizeX >> DownSampleLevel;
			const uint32 DownSampledSizeY = SizeY >> DownSampleLevel;
			const uint32 Handle = AddrSpaceAllocator.Alloc(DownSampledSizeX, DownSampledSizeY);
			const uint32 VisibilityChannel = TmpAllocation.VisibilityChannel;

			if (Handle == INDEX_NONE)
			{
				break;
			}

			const uint32 NumMissingMips = SourceTexture->GetNumMips() - SourceTexture->GetNumResidentMips();
			const uint32 SourceMipBias = NumMissingMips > DownSampleLevel ? 0 : DownSampleLevel - NumMissingMips;

			if (NumMissingMips > DownSampleLevel)
			{
				SourceTexture->bForceMiplevelsToBeResident = true;
				check(!PendingStreamingTextures.Contains(SourceTexture));
				PendingStreamingTextures.Add(SourceTexture);
			}

			TmpAllocation.Handle = Handle;
			CurrentAllocations.Add(TmpAllocation);
			PendingUploads.Emplace(SourceTexture, DownSampledSizeX, DownSampledSizeY, SourceMipBias, Handle, VisibilityChannel);
			It.RemoveCurrent();
		}
	}

	if (PendingUploads.Num())
	{
		FRDGBuilder GraphBuilder(RHICmdList);

		RHICmdList.Transition(FRHITransitionInfo(AtlasUAVRHI, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));

		if (SubAllocType == SAT_Height)
		{
			TShaderMapRef<FUploadHeightFieldToAtlasCS> ComputeShader(GetGlobalShaderMap(InFeatureLevel));
			for (int32 Idx = 0; Idx < PendingUploads.Num(); ++Idx)
			{
				typename FUploadHeightFieldToAtlasCS::FParameters* Parameters = GraphBuilder.AllocParameters<typename FUploadHeightFieldToAtlasCS::FParameters>();
				const FIntPoint UpdateRegion = PendingUploads[Idx].SetShaderParameters(Parameters, *this);
				const bool bNeedBarrier = Idx > 0;
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UploadHeightFieldToAtlas"),
					Parameters,
					ERDGPassFlags::Compute,
					[Parameters, ComputeShader, UpdateRegion, bNeedBarrier](FRHICommandList& CmdList)
				{
					if (bNeedBarrier)
					{
						CmdList.Transition(FRHITransitionInfo(Parameters->RWHeightFieldAtlas, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));
					}
					FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *Parameters, FIntVector(UpdateRegion.X, UpdateRegion.Y, 1));
				});
			}
		}
		else
		{
			TShaderMapRef<FUploadVisibilityToAtlasCS> ComputeShader(GetGlobalShaderMap(InFeatureLevel));
			for (int32 Idx = 0; Idx < PendingUploads.Num(); ++Idx)
			{
				typename FUploadVisibilityToAtlasCS::FParameters* Parameters = GraphBuilder.AllocParameters<typename FUploadVisibilityToAtlasCS::FParameters>();
				const FIntPoint UpdateRegion = PendingUploads[Idx].SetShaderParameters(Parameters, *this);
				const bool bNeedBarrier = Idx > 0;
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UploadVisibilityToAtlas"),
					Parameters,
					ERDGPassFlags::Compute,
					[Parameters, ComputeShader, UpdateRegion, bNeedBarrier](FRHICommandList& CmdList)
				{
					if (bNeedBarrier)
					{
						CmdList.Transition(FRHITransitionInfo(Parameters->RWVisibilityAtlas, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));
					}
					FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *Parameters, FIntVector(UpdateRegion.X, UpdateRegion.Y, 1));
				});
			}
		}

		GraphBuilder.Execute();
		RHICmdList.Transition(FRHITransitionInfo(AtlasUAVRHI, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	}
}

uint32 FLandscapeTextureAtlas::GetAllocationHandle(UTexture2D* Texture) const
{
	const FAllocation* Allocation = CurrentAllocations.Find(Texture);
	return Allocation ? Allocation->Handle : INDEX_NONE;
}

FVector4 FLandscapeTextureAtlas::GetAllocationScaleBias(uint32 Handle) const
{
	return AddrSpaceAllocator.GetScaleBias(Handle);
}

void FLandscapeTextureAtlas::FSubAllocator::Init(uint32 InTileSize, uint32 InBorderSize, uint32 InDimInTiles)
{
	check(InDimInTiles && !(InDimInTiles & (InDimInTiles - 1)));

	TileSize = InTileSize;
	BorderSize = InBorderSize;
	TileSizeWithBorder = InTileSize + 2 * InBorderSize;
	DimInTiles = InDimInTiles;
	DimInTilesShift = FMath::CountBits(InDimInTiles - 1);
	DimInTilesMask = InDimInTiles - 1;
	DimInTexels = InDimInTiles * TileSizeWithBorder;
	MaxNumTiles = InDimInTiles * InDimInTiles;
	
	TexelSize = 1.f / DimInTexels;
	TileScale = TileSize * TexelSize;

	LevelOffsets.Empty();
	MarkerQuadTree.Empty();
	SubAllocInfos.Empty();

	uint32 NumBits = 0;
	for (uint32 Level = 1; Level <= DimInTiles; Level <<= 1)
	{
		const uint32 NumQuadsInLevel = Level * Level;
		LevelOffsets.Add(NumBits);
		NumBits += NumQuadsInLevel;
	}
	MarkerQuadTree.Add(false, NumBits);
}

uint32 FLandscapeTextureAtlas::FSubAllocator::Alloc(uint32 SizeX, uint32 SizeY)
{
	const uint32 NumTiles1D = FMath::DivideAndRoundUp(FMath::Max(SizeX, SizeY), TileSize);
	check(NumTiles1D <= DimInTiles);
	const uint32 NumLevels = LevelOffsets.Num();
	const uint32 Level = NumLevels - FMath::CeilLogTwo(NumTiles1D) - 1;
	const uint32 LevelOffset = LevelOffsets[Level];
	const uint32 QuadsInLevel1D = 1u << Level;
	const uint32 SearchEnd = LevelOffset + QuadsInLevel1D * QuadsInLevel1D;

	uint32 QuadIdx = LevelOffset;
	for (; QuadIdx < SearchEnd; ++QuadIdx)
	{
		if (!MarkerQuadTree[QuadIdx])
		{
			break;
		}
	}

	if (QuadIdx != SearchEnd)
	{
		const uint32 QuadIdxInLevel = QuadIdx - LevelOffset;
		
		uint32 ParentLevel = Level;
		uint32 ParentQuadIdxInLevel = QuadIdxInLevel;
		for (; ParentLevel != (uint32)-1; --ParentLevel)
		{
			const uint32 ParentLevelOffset = LevelOffsets[ParentLevel];
			const uint32 ParentQuadIdx = ParentLevelOffset + ParentQuadIdxInLevel;
			FBitReference Marker = MarkerQuadTree[ParentQuadIdx];
			if (Marker)
			{
				break;
			}
			Marker = true;
			ParentQuadIdxInLevel >>= 2;
		}

		uint32 ChildLevel = Level + 1;
		uint32 ChildQuadIdxInLevel = QuadIdxInLevel << 2;
		uint32 NumChildren = 4;
		for (; ChildLevel < NumLevels; ++ChildLevel)
		{
			const uint32 ChildQuadIdx = ChildQuadIdxInLevel + LevelOffsets[ChildLevel];
			for (uint32 Idx = 0; Idx < NumChildren; ++Idx)
			{
				check(!MarkerQuadTree[ChildQuadIdx + Idx]);
				MarkerQuadTree[ChildQuadIdx + Idx] = true;
			}
			ChildQuadIdxInLevel <<= 2;
			NumChildren <<= 2;
		}
		
		const uint32 QuadX = FMath::ReverseMortonCode2(QuadIdxInLevel);
		const uint32 QuadY = FMath::ReverseMortonCode2(QuadIdxInLevel >> 1);
		const uint32 QuadSizeInTiles1D = DimInTiles >> Level;
		const uint32 TileX = QuadX * QuadSizeInTiles1D;
		const uint32 TileY = QuadY * QuadSizeInTiles1D;

		FSubAllocInfo SubAllocInfo;
		SubAllocInfo.Level = Level;
		SubAllocInfo.QuadIdx = QuadIdx;
		SubAllocInfo.UVScaleBias.X = SizeX * TexelSize;
		SubAllocInfo.UVScaleBias.Y = SizeY * TexelSize;
		SubAllocInfo.UVScaleBias.Z = TileX / (float)DimInTiles + BorderSize * TexelSize;
		SubAllocInfo.UVScaleBias.W = TileY / (float)DimInTiles + BorderSize * TexelSize;

		return SubAllocInfos.Add(SubAllocInfo);
	}

	return INDEX_NONE;
}

void FLandscapeTextureAtlas::FSubAllocator::Free(uint32 Handle)
{
	check(SubAllocInfos.IsValidIndex(Handle));

	const FSubAllocInfo SubAllocInfo = SubAllocInfos[Handle];
	SubAllocInfos.RemoveAt(Handle);

	const uint32 Level = SubAllocInfo.Level;
	const uint32 QuadIdx = SubAllocInfo.QuadIdx;
	
	uint32 ChildLevel = Level;
	uint32 ChildIdxInLevel = QuadIdx - LevelOffsets[Level];
	uint32 NumChildren = 1;
	const uint32 NumLevels = LevelOffsets.Num();
	for (; ChildLevel < NumLevels; ++ChildLevel)
	{
		const uint32 ChildIdx = ChildIdxInLevel + LevelOffsets[ChildLevel];
		for (uint32 Idx = 0; Idx < NumChildren; ++Idx)
		{
			check(MarkerQuadTree[ChildIdx + Idx]);
			MarkerQuadTree[ChildIdx + Idx] = false;
		}
		ChildIdxInLevel <<= 2;
		NumChildren <<= 2;
	}

	uint32 TestIdxInLevel = (QuadIdx - LevelOffsets[Level]) & ~3u;
	uint32 ParentLevel = Level - 1;
	for (; ParentLevel != (uint32)-1; --ParentLevel)
	{
		const uint32 TestIdx = TestIdxInLevel + LevelOffsets[ParentLevel + 1];
		const bool bParentFree = !MarkerQuadTree[TestIdx] && !MarkerQuadTree[TestIdx + 1] && !MarkerQuadTree[TestIdx + 2] && !MarkerQuadTree[TestIdx + 3];
		if (!bParentFree)
		{
			break;
		}
		const uint32 ParentIdxInLevel = TestIdxInLevel >> 2;
		const uint32 ParentIdx = ParentIdxInLevel + LevelOffsets[ParentLevel];
		MarkerQuadTree[ParentIdx] = false;
		TestIdxInLevel = ParentIdxInLevel & ~3u;
	}
}

FVector4 FLandscapeTextureAtlas::FSubAllocator::GetScaleBias(uint32 Handle) const
{
	check(SubAllocInfos.IsValidIndex(Handle));
	return SubAllocInfos[Handle].UVScaleBias;
}

FIntPoint FLandscapeTextureAtlas::FSubAllocator::GetStartOffset(uint32 Handle) const
{
	check(SubAllocInfos.IsValidIndex(Handle));
	const FSubAllocInfo& Info = SubAllocInfos[Handle];
	const uint32 QuadIdxInLevel = Info.QuadIdx - LevelOffsets[Info.Level];
	const uint32 QuadX = FMath::ReverseMortonCode2(QuadIdxInLevel);
	const uint32 QuadY = FMath::ReverseMortonCode2(QuadIdxInLevel >> 1);
	const uint32 QuadSizeInTexels1D = (DimInTiles >> Info.Level) * TileSizeWithBorder;	
	return FIntPoint(QuadX * QuadSizeInTexels1D, QuadY * QuadSizeInTexels1D);
}

FLandscapeTextureAtlas::FAllocation::FAllocation()
	: SourceTexture(nullptr)
	, Handle(INDEX_NONE)
	, VisibilityChannel(0)
	, RefCount(0)
{}

FLandscapeTextureAtlas::FAllocation::FAllocation(UTexture2D* InTexture, uint32 InVisibilityChannel)
	: SourceTexture(InTexture)
	, Handle(INDEX_NONE)
	, VisibilityChannel(InVisibilityChannel)
	, RefCount(1)
{}

FLandscapeTextureAtlas::FPendingUpload::FPendingUpload(UTexture2D* Texture, uint32 SizeX, uint32 SizeY, uint32 MipBias, uint32 InHandle, uint32 Channel)
	: SourceTexture(Texture->Resource->TextureRHI)
	, SizesAndMipBias(FIntVector(SizeX, SizeY, MipBias))
	, VisibilityChannel(Channel)
	, Handle(InHandle)
{}

FIntPoint FLandscapeTextureAtlas::FPendingUpload::SetShaderParameters(void* ParamsPtr, const FLandscapeTextureAtlas& Atlas) const
{
	if (Atlas.SubAllocType == SAT_Height)
	{
		typename FUploadHeightFieldToAtlasCS::FParameters* Params = (typename FUploadHeightFieldToAtlasCS::FParameters*)ParamsPtr;
		Params->RWHeightFieldAtlas = Atlas.AtlasUAVRHI;
		return SetCommonShaderParameters(&Params->SharedParams, Atlas);
	}
	else
	{
		typename FUploadVisibilityToAtlasCS::FParameters* Params = (typename FUploadVisibilityToAtlasCS::FParameters*)ParamsPtr;
		FVector4 ChannelMask(ForceInitToZero);
		ChannelMask[VisibilityChannel] = 1.f;
		Params->VisibilityChannelMask = ChannelMask;
		Params->RWVisibilityAtlas = Atlas.AtlasUAVRHI;
		return SetCommonShaderParameters(&Params->SharedParams, Atlas);
	}
}

FIntPoint FLandscapeTextureAtlas::FPendingUpload::SetCommonShaderParameters(void* ParamsPtr, const FLandscapeTextureAtlas& Atlas) const
{
	const uint32 DownSampledSizeX = SizesAndMipBias.X;
	const uint32 DownSampledSizeY = SizesAndMipBias.Y;
	const uint32 SourceMipBias = SizesAndMipBias.Z;
	const float InvDownSampledSizeX = 1.f / DownSampledSizeX;
	const float InvDownSampledSizeY = 1.f / DownSampledSizeY;
	const uint32 BorderSize = Atlas.AddrSpaceAllocator.BorderSize;
	const uint32 UpdateRegionSizeX = DownSampledSizeX + 2 * BorderSize;
	const uint32 UpdateRegionSizeY = DownSampledSizeY + 2 * BorderSize;
	const FIntPoint StartOffset = Atlas.AddrSpaceAllocator.GetStartOffset(Handle);

	typename FUploadLandscapeTextureToAtlasCS::FSharedParameters* CommonParams = (typename FUploadLandscapeTextureToAtlasCS::FSharedParameters*)ParamsPtr;
	CommonParams->UpdateRegionOffsetAndSize = FUintVector4(StartOffset.X, StartOffset.Y, UpdateRegionSizeX, UpdateRegionSizeY);
	CommonParams->SourceScaleBias = FVector4(InvDownSampledSizeX, InvDownSampledSizeY, (.5f - BorderSize) * InvDownSampledSizeX, (.5f - BorderSize) * InvDownSampledSizeY);
	CommonParams->SourceMipBias = SourceMipBias;
	CommonParams->SourceTexture = SourceTexture;
	CommonParams->SourceTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	const uint32 NumGroupsX = FMath::DivideAndRoundUp(UpdateRegionSizeX, FUploadLandscapeTextureToAtlasCS::ThreadGroupSizeX);
	const uint32 NumGroupsY = FMath::DivideAndRoundUp(UpdateRegionSizeY, FUploadLandscapeTextureToAtlasCS::ThreadGroupSizeY);
	return FIntPoint(NumGroupsX, NumGroupsY);
}
