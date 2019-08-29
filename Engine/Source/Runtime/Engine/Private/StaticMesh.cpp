// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StaticMesh.cpp: Static mesh class implementation.
=============================================================================*/

#include "Engine/StaticMesh.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectAnnotation.h"
#include "RenderingThread.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "RawIndexBuffer.h"
#include "Engine/TextureStreamingTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Serialization/MemoryReader.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/Package.h"
#include "EngineUtils.h"
#include "Engine/AssetUserData.h"
#include "StaticMeshResources.h"
#include "StaticMeshVertexData.h"
#include "Interfaces/ITargetPlatform.h"
#include "SpeedTreeWind.h"
#include "DistanceFieldAtlas.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "HAL/LowLevelMemTracker.h"
#include "DynamicMeshBuilder.h"
#include "Model.h"
#include "SplineMeshSceneProxy.h"
#include "Templates/UniquePtr.h"

#if WITH_EDITOR
#include "RawMesh.h"
#include "Settings/EditorExperimentalSettings.h"
#include "MeshBuilder.h"
#include "MeshUtilities.h"
#include "DerivedDataCacheInterface.h"
#include "PlatformInfo.h"
#include "ScopedTransaction.h"
#include "IMeshBuilderModule.h"
#include "MeshDescriptionOperations.h"
#endif // #if WITH_EDITOR

#include "MeshDescription.h"
#include "MeshAttributes.h"

#include "Engine/StaticMeshSocket.h"
#include "EditorFramework/AssetImportData.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemHelpers.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Streaming/UVChannelDensity.h"

#define LOCTEXT_NAMESPACE "StaticMesh"
DEFINE_LOG_CATEGORY(LogStaticMesh);	

DECLARE_MEMORY_STAT( TEXT( "StaticMesh Total Memory" ), STAT_StaticMeshTotalMemory2, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Vertex Memory" ), STAT_StaticMeshVertexMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh VxColor Resource Mem" ), STAT_ResourceVertexColorMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Index Memory" ), STAT_StaticMeshIndexMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Distance Field Memory" ), STAT_StaticMeshDistanceFieldMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Occluder Memory" ), STAT_StaticMeshOccluderMemory, STATGROUP_MemoryStaticMesh );

DECLARE_MEMORY_STAT( TEXT( "StaticMesh Total Memory" ), STAT_StaticMeshTotalMemory, STATGROUP_Memory );

/** Package name, that if set will cause only static meshes in that package to be rebuilt based on SM version. */
ENGINE_API FName GStaticMeshPackageNameToRebuild = NAME_None;

#if WITH_EDITORONLY_DATA
int32 GUpdateMeshLODGroupSettingsAtLoad = 0;
static FAutoConsoleVariableRef CVarStaticMeshUpdateMeshLODGroupSettingsAtLoad(
	TEXT("r.StaticMesh.UpdateMeshLODGroupSettingsAtLoad"),
	GUpdateMeshLODGroupSettingsAtLoad,
	TEXT("If set, LODGroup settings for static meshes will be applied at load time."));
#endif

static TAutoConsoleVariable<int32> CVarStripMinLodDataDuringCooking(
	TEXT("r.StaticMesh.StripMinLodDataDuringCooking"),
	0,
	TEXT("If non-zero, data for Static Mesh LOD levels below MinLOD will be discarded at cook time"));

int32 GForceStripMeshAdjacencyDataDuringCooking = 0;
static FAutoConsoleVariableRef CVarForceStripMeshAdjacencyDataDuringCooking(
	TEXT("r.ForceStripAdjacencyDataDuringCooking"),
	GForceStripMeshAdjacencyDataDuringCooking,
	TEXT("If set, adjacency data will be stripped for all static and skeletal meshes during cooking (acting like the target platform did not support tessellation)."));

static TAutoConsoleVariable<int32> CVarSupportDepthOnlyIndexBuffers(
	TEXT("r.SupportDepthOnlyIndexBuffers"),
	1,
	TEXT("Enables depth-only index buffers. Saves a little time at the expense of doubling the size of index buffers."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSupportReversedIndexBuffers(
	TEXT("r.SupportReversedIndexBuffers"),
	1,
	TEXT("Enables reversed index buffers. Saves a little time at the expense of doubling the size of index buffers."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

#if ENABLE_COOK_STATS
namespace StaticMeshCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("StaticMesh.Usage"), TEXT(""));
	});
}
#endif


#if WITH_EDITOR
static void FillMaterialName(const TArray<FStaticMaterial>& StaticMaterials, TMap<int32, FName>& OutMaterialMap)
{
	OutMaterialMap.Empty(StaticMaterials.Num());

	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		FName MaterialName = StaticMaterials[MaterialIndex].ImportedMaterialSlotName;
		if (MaterialName == NAME_None)
		{
			MaterialName = *(TEXT("MaterialSlot_") + FString::FromInt(MaterialIndex));
		}
		OutMaterialMap.Add(MaterialIndex, MaterialName);
	}
}
#endif


/*-----------------------------------------------------------------------------
	FStaticMeshLODResources
-----------------------------------------------------------------------------*/

FArchive& operator<<(FArchive& Ar, FStaticMeshSection& Section)
{
	Ar << Section.MaterialIndex;
	Ar << Section.FirstIndex;
	Ar << Section.NumTriangles;
	Ar << Section.MinVertexIndex;
	Ar << Section.MaxVertexIndex;
	Ar << Section.bEnableCollision;
	Ar << Section.bCastShadow;

#if WITH_EDITORONLY_DATA
	if((!Ar.IsCooking() && !Ar.IsFilterEditorOnly()) || (Ar.IsCooking() && Ar.CookingTarget()->HasEditorOnlyData()))
	{
		for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; ++UVIndex)
		{
			Ar << Section.UVDensities[UVIndex];
			Ar << Section.Weights[UVIndex];
		}
	}
#endif

	return Ar;
}

void FStaticMeshLODResources::Serialize(FArchive& Ar, UObject* Owner, int32 Index)
{
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("FStaticMeshLODResources::Serialize"), STAT_StaticMeshLODResources_Serialize, STATGROUP_LoadTime );

	bool bEnableDepthOnlyIndexBuffer = (CVarSupportDepthOnlyIndexBuffers.GetValueOnAnyThread() == 1);
	bool bEnableReversedIndexBuffer = (CVarSupportReversedIndexBuffers.GetValueOnAnyThread() == 1);

	// See if the mesh wants to keep resources CPU accessible
	UStaticMesh* OwnerStaticMesh = Cast<UStaticMesh>(Owner);
	bool bMeshCPUAcces = OwnerStaticMesh ? OwnerStaticMesh->bAllowCPUAccess : false;

	// Note: this is all derived data, native versioning is not needed, but be sure to bump STATICMESH_DERIVEDDATA_VER when modifying!

	// On cooked platforms we never need the resource data.
	// TODO: Not needed in uncooked games either after PostLoad!
	bool bNeedsCPUAccess = !FPlatformProperties::RequiresCookedData() || bMeshCPUAcces;

	bHasAdjacencyInfo = false;
	bHasDepthOnlyIndices = false;
	bHasReversedIndices = false;
	bHasReversedDepthOnlyIndices = false;
	DepthOnlyNumTriangles = 0;

	// Defined class flags for possible stripping
	const uint8 AdjacencyDataStripFlag = 1;
	const uint8 MinLodDataStripFlag = 2;
	const uint8 ReversedIndexBufferStripFlag = 4;

	// Actual flags used during serialization
	uint8 ClassDataStripFlags = 0;

#if WITH_EDITOR
	const bool bWantToStripTessellation = Ar.IsCooking() && ((GForceStripMeshAdjacencyDataDuringCooking != 0) || !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::Tessellation));
	const bool bWantToStripLOD = Ar.IsCooking() && (CVarStripMinLodDataDuringCooking.GetValueOnAnyThread() != 0) && OwnerStaticMesh && OwnerStaticMesh->MinLOD.GetValueForPlatformGroup(Ar.CookingTarget()->GetPlatformInfo().PlatformGroupName) > Index;

	ClassDataStripFlags |=	(bWantToStripTessellation ? AdjacencyDataStripFlag : 0)	|
							(bWantToStripLOD ? MinLodDataStripFlag : 0);
#endif

	FStripDataFlags StripFlags( Ar, ClassDataStripFlags );

	Ar << Sections;
	Ar << MaxDeviation;

	if( !StripFlags.IsDataStrippedForServer() && !StripFlags.IsClassDataStripped(MinLodDataStripFlag) )
	{
		VertexBuffers.PositionVertexBuffer.Serialize( Ar, bNeedsCPUAccess );
		VertexBuffers.StaticMeshVertexBuffer.Serialize( Ar, bNeedsCPUAccess );
		VertexBuffers.ColorVertexBuffer.Serialize( Ar, bNeedsCPUAccess );
		IndexBuffer.Serialize(Ar, bNeedsCPUAccess);

		const bool bSerailizeReversedIndexBuffer = !StripFlags.IsClassDataStripped(ReversedIndexBufferStripFlag);
		if (bSerailizeReversedIndexBuffer)
		{
			ReversedIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
			if (!bEnableReversedIndexBuffer)
			{
				ReversedIndexBuffer.Discard();
			}
		}
		DepthOnlyIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		if (!bEnableDepthOnlyIndexBuffer)
		{
			DepthOnlyIndexBuffer.Discard();
		}
		if (bSerailizeReversedIndexBuffer)
		{
			ReversedDepthOnlyIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
			if (!bEnableReversedIndexBuffer)
			{
				ReversedDepthOnlyIndexBuffer.Discard();
			}
		}

		if( !StripFlags.IsEditorDataStripped() )
		{
			WireframeIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		}

		if ( !StripFlags.IsClassDataStripped( AdjacencyDataStripFlag ) )
		{
			AdjacencyIndexBuffer.Serialize( Ar, bNeedsCPUAccess );
			bHasAdjacencyInfo = AdjacencyIndexBuffer.GetNumIndices() != 0;
		}

		// Needs to be done now because on cooked platform, indices are discarded after RHIInit.
		bHasDepthOnlyIndices = DepthOnlyIndexBuffer.GetNumIndices() != 0;
		bHasReversedIndices = bSerailizeReversedIndexBuffer && ReversedIndexBuffer.GetNumIndices() != 0;
		bHasReversedDepthOnlyIndices = bSerailizeReversedIndexBuffer && ReversedDepthOnlyIndexBuffer.GetNumIndices() != 0;
		DepthOnlyNumTriangles = DepthOnlyIndexBuffer.GetNumIndices() / 3;

		AreaWeightedSectionSamplers.SetNum(Sections.Num());
		for (FStaticMeshSectionAreaWeightedTriangleSampler& Sampler : AreaWeightedSectionSamplers)
		{
			Sampler.Serialize(Ar);
		}
		AreaWeightedSampler.Serialize(Ar);
	}
}

int32 FStaticMeshLODResources::GetNumTriangles() const
{
	int32 NumTriangles = 0;
	for(int32 SectionIndex = 0;SectionIndex < Sections.Num();SectionIndex++)
	{
		NumTriangles += Sections[SectionIndex].NumTriangles;
	}
	return NumTriangles;
}

int32 FStaticMeshLODResources::GetNumVertices() const
{
	return VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
}

int32 FStaticMeshLODResources::GetNumTexCoords() const
{
	return VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
}

void FStaticMeshVertexFactories::InitVertexFactory(
	const FStaticMeshLODResources& LodResources,
	FLocalVertexFactory& InOutVertexFactory,
	const UStaticMesh* InParentMesh,
	bool bInOverrideColorVertexBuffer
	)
{
	check( InParentMesh != NULL );

	struct InitStaticMeshVertexFactoryParams
	{
		FLocalVertexFactory* VertexFactory;
		const FStaticMeshLODResources* LODResources;
		bool bOverrideColorVertexBuffer;
		uint32 LightMapCoordinateIndex;
	} Params;

	uint32 LightMapCoordinateIndex = (uint32)InParentMesh->LightMapCoordinateIndex;
	LightMapCoordinateIndex = LightMapCoordinateIndex < LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() ? LightMapCoordinateIndex : LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() - 1;

	Params.VertexFactory = &InOutVertexFactory;
	Params.LODResources = &LodResources;
	Params.bOverrideColorVertexBuffer = bInOverrideColorVertexBuffer;
	Params.LightMapCoordinateIndex = LightMapCoordinateIndex;

	// Initialize the static mesh's vertex factory.
	ENQUEUE_RENDER_COMMAND(InitStaticMeshVertexFactory)(
		[Params](FRHICommandListImmediate& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;

			Params.LODResources->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(Params.VertexFactory, Data, Params.LightMapCoordinateIndex);

			// bOverrideColorVertexBuffer means we intend to override the color later.  We must construct the vertexfactory such that it believes a proper stride (not 0) is set for
			// the color stream so that the real stream works later.
			if(Params.bOverrideColorVertexBuffer)
			{ 
				FColorVertexBuffer::BindDefaultColorVertexBuffer(Params.VertexFactory, Data, FColorVertexBuffer::NullBindStride::FColorSizeForComponentOverride);
			}
			//otherwise just bind the incoming buffer directly.
			else
			{
				Params.LODResources->VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(Params.VertexFactory, Data);
			}

			Params.VertexFactory->SetData(Data);
			Params.VertexFactory->InitResource();
		});
}

void FStaticMeshVertexFactories::InitResources(const FStaticMeshLODResources& LodResources, const UStaticMesh* Parent)
{
	InitVertexFactory(LodResources, VertexFactory, Parent, false);
	BeginInitResource(&VertexFactory);

	InitVertexFactory(LodResources, VertexFactoryOverrideColorVertexBuffer, Parent, true);
	BeginInitResource(&VertexFactoryOverrideColorVertexBuffer);
}

void FStaticMeshVertexFactories::ReleaseResources()
{
	// Release the vertex factories.
	BeginReleaseResource(&VertexFactory);
	BeginReleaseResource(&VertexFactoryOverrideColorVertexBuffer);

	if (SplineVertexFactory)
	{
		BeginReleaseResource(SplineVertexFactory);		
	}
	if (SplineVertexFactoryOverrideColorVertexBuffer)
	{
		BeginReleaseResource(SplineVertexFactoryOverrideColorVertexBuffer);		
	}
}

FStaticMeshVertexFactories::~FStaticMeshVertexFactories()
{
	delete SplineVertexFactory;
	delete SplineVertexFactoryOverrideColorVertexBuffer;
}

FStaticMeshSectionAreaWeightedTriangleSampler::FStaticMeshSectionAreaWeightedTriangleSampler()
	: Owner(nullptr)
	, SectionIdx(INDEX_NONE)
{
}

void FStaticMeshSectionAreaWeightedTriangleSampler::Init(FStaticMeshLODResources* InOwner, int32 InSectionIdx)
{
	Owner = InOwner;
	SectionIdx = InSectionIdx;
	Initialize();
}

float FStaticMeshSectionAreaWeightedTriangleSampler::GetWeights(TArray<float>& OutWeights)
{
	//If these hit, you're trying to get weights on a sampler that's not been initialized.
	check(Owner);
	check(SectionIdx != INDEX_NONE);
	check(Owner->Sections.IsValidIndex(SectionIdx));
	FIndexArrayView Indicies = Owner->IndexBuffer.GetArrayView();
	FStaticMeshSection& Section = Owner->Sections[SectionIdx];

	int32 First = Section.FirstIndex;
	int32 Last = First + Section.NumTriangles * 3;
	float Total = 0.0f;
	OutWeights.Empty(Indicies.Num() / 3);
	for (int32 i = First; i < Last; i+=3)
	{
		FVector V0 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i]);
		FVector V1 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i + 1]);
		FVector V2 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i + 2]);

		float Area = ((V1 - V0) ^ (V2 - V0)).Size() * 0.5f;
		OutWeights.Add(Area);
		Total += Area;
	}
	return Total;
}

FStaticMeshAreaWeightedSectionSampler::FStaticMeshAreaWeightedSectionSampler()
	: Owner(nullptr)
{
}

void FStaticMeshAreaWeightedSectionSampler::Init(FStaticMeshLODResources* InOwner)
{
	Owner = InOwner;
	Initialize();
}

float FStaticMeshAreaWeightedSectionSampler::GetWeights(TArray<float>& OutWeights)
{
	//If this hits, you're trying to get weights on a sampler that's not been initialized.
	check(Owner);
	float Total = 0.0f;
	OutWeights.Empty(Owner->Sections.Num());
	for (int32 i = 0; i < Owner->Sections.Num(); ++i)
	{
		float T = Owner->AreaWeightedSectionSamplers[i].GetTotalWeight();
		OutWeights.Add(T);
		Total += T;
	}
	return Total;
}

static inline void InitOrUpdateResource(FRenderResource* Resource)
{
	if (!Resource->IsInitialized())
	{
		Resource->InitResource();
	}
	else
	{
		Resource->UpdateRHI();
	}
}

void FStaticMeshVertexBuffers::InitModelBuffers(TArray<FModelVertex>& Vertices)
{
	if (Vertices.Num())
	{
		PositionVertexBuffer.Init(Vertices.Num());
		StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		StaticMeshVertexBuffer.Init(Vertices.Num(), 2);

		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			const FModelVertex& Vertex = Vertices[i];

			PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX, Vertex.GetTangentY(), Vertex.TangentZ);
			StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TexCoord);
			StaticMeshVertexBuffer.SetVertexUV(i, 1, Vertex.ShadowTexCoord);
		}
	}
	else
	{
		PositionVertexBuffer.Init(1);
		StaticMeshVertexBuffer.Init(1, 2);

		PositionVertexBuffer.VertexPosition(0) = FVector(0, 0, 0);
		StaticMeshVertexBuffer.SetVertexTangents(0, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1));
		StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2D(0, 0));
		StaticMeshVertexBuffer.SetVertexUV(0, 1, FVector2D(0, 0));
	}
}

void FStaticMeshVertexBuffers::InitModelVF(FLocalVertexFactory* VertexFactory)
{
	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyBspInit)(
		[VertexFactory, Self](FRHICommandListImmediate& RHICmdList)
	{
		check(Self->PositionVertexBuffer.IsInitialized());
		check(Self->StaticMeshVertexBuffer.IsInitialized());

		FLocalVertexFactory::FDataType Data;
		Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, 1);
		FColorVertexBuffer::BindDefaultColorVertexBuffer(VertexFactory, Data, FColorVertexBuffer::NullBindStride::ZeroForDefaultBufferBind);
		VertexFactory->SetData(Data);

		InitOrUpdateResource(VertexFactory);
	});
}

void FStaticMeshVertexBuffers::InitWithDummyData(FLocalVertexFactory* VertexFactory, uint32 NumVerticies, uint32 NumTexCoords, uint32 LightMapIndex)
{
	check(NumVerticies);
	check(NumTexCoords < MAX_STATIC_TEXCOORDS && NumTexCoords > 0);
	check(LightMapIndex < NumTexCoords);

	PositionVertexBuffer.Init(NumVerticies);
	StaticMeshVertexBuffer.Init(NumVerticies, NumTexCoords);
	ColorVertexBuffer.Init(NumVerticies);

	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[VertexFactory, Self, LightMapIndex](FRHICommandListImmediate& RHICmdList)
	{
		InitOrUpdateResource(&Self->PositionVertexBuffer);
		InitOrUpdateResource(&Self->StaticMeshVertexBuffer);
		InitOrUpdateResource(&Self->ColorVertexBuffer);

		FLocalVertexFactory::FDataType Data;
		Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, LightMapIndex);
		Self->ColorVertexBuffer.BindColorVertexBuffer(VertexFactory, Data);
		VertexFactory->SetData(Data);

		InitOrUpdateResource(VertexFactory);
	});
}

void FStaticMeshVertexBuffers::InitFromDynamicVertex(FLocalVertexFactory* VertexFactory, TArray<FDynamicMeshVertex>& Vertices, uint32 NumTexCoords, uint32 LightMapIndex)
{
	check(NumTexCoords < MAX_STATIC_TEXCOORDS && NumTexCoords > 0);
	check(LightMapIndex < NumTexCoords);

	if (Vertices.Num())
	{
		PositionVertexBuffer.Init(Vertices.Num());
		StaticMeshVertexBuffer.Init(Vertices.Num(), NumTexCoords);
		ColorVertexBuffer.Init(Vertices.Num());

		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			const FDynamicMeshVertex& Vertex = Vertices[i];

			PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
			for (uint32 j = 0; j < NumTexCoords; j++)
			{
				StaticMeshVertexBuffer.SetVertexUV(i, j, Vertex.TextureCoordinate[j]);
			}
			ColorVertexBuffer.VertexColor(i) = Vertex.Color;
		}
	}
	else
	{
		PositionVertexBuffer.Init(1);
		StaticMeshVertexBuffer.Init(1, 1);
		ColorVertexBuffer.Init(1);

		PositionVertexBuffer.VertexPosition(0) = FVector(0, 0, 0);
		StaticMeshVertexBuffer.SetVertexTangents(0, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1));
		StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2D(0, 0));
		ColorVertexBuffer.VertexColor(0) = FColor(1,1,1,1);
		NumTexCoords = 1;
		LightMapIndex = 0;
	}

	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[VertexFactory, Self, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			InitOrUpdateResource(&Self->PositionVertexBuffer);
			InitOrUpdateResource(&Self->StaticMeshVertexBuffer);
			InitOrUpdateResource(&Self->ColorVertexBuffer);

			FLocalVertexFactory::FDataType Data;
			Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, LightMapIndex);
			Self->ColorVertexBuffer.BindColorVertexBuffer(VertexFactory, Data);
			VertexFactory->SetData(Data);

			InitOrUpdateResource(VertexFactory);
		});
};

FStaticMeshLODResources::FStaticMeshLODResources()
	: DistanceFieldData(NULL)
	, MaxDeviation(0.0f)
	, bHasAdjacencyInfo(false)
	, bHasDepthOnlyIndices(false)
	, bHasReversedIndices(false)
	, bHasReversedDepthOnlyIndices(false)
	, DepthOnlyNumTriangles(0)
#if STATS
	, StaticMeshIndexMemory(0)
#endif
{
}

FStaticMeshLODResources::~FStaticMeshLODResources()
{
	delete DistanceFieldData;
}

void FStaticMeshLODResources::InitResources(UStaticMesh* Parent)
{
	const auto MaxShaderPlatform = GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel];

	// Initialize the vertex and index buffers.
	// All platforms supporting Metal also support 32-bit indices.
	if (IsES2Platform(MaxShaderPlatform) && !IsMetalPlatform(MaxShaderPlatform))
	{
		if (IndexBuffer.Is32Bit())
		{
			//TODO: Show this as an error in the static mesh editor when doing a Mobile preview so gets fixed in content
			TArray<uint32> Indices;
			IndexBuffer.GetCopy(Indices);
			IndexBuffer.SetIndices(Indices, EIndexBufferStride::Force16Bit);
			UE_LOG(LogStaticMesh, Warning, TEXT("[%s] Mesh has more that 65535 vertices, incompatible with mobile; forcing 16-bit (will probably cause rendering issues)." ), *Parent->GetName());
		}
	}

#if STATS
	uint32 iMem = IndexBuffer.GetAllocatedSize();
	uint32 wiMem = WireframeIndexBuffer.GetAllocatedSize();
	uint32 riMem = ReversedIndexBuffer.GetAllocatedSize();
	uint32 doiMem = DepthOnlyIndexBuffer.GetAllocatedSize();
	uint32 rdoiMem = ReversedDepthOnlyIndexBuffer.GetAllocatedSize();
	uint32 aiMem = AdjacencyIndexBuffer.GetAllocatedSize();
	StaticMeshIndexMemory = iMem + wiMem + riMem + doiMem + rdoiMem + aiMem;
	INC_DWORD_STAT_BY(STAT_StaticMeshIndexMemory, StaticMeshIndexMemory);
#endif

	BeginInitResource(&IndexBuffer);
	if( WireframeIndexBuffer.GetNumIndices() > 0 )
	{
		BeginInitResource(&WireframeIndexBuffer);
	}	
	BeginInitResource(&VertexBuffers.StaticMeshVertexBuffer);
	BeginInitResource(&VertexBuffers.PositionVertexBuffer);
	if( VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0 )
	{
		BeginInitResource(&VertexBuffers.ColorVertexBuffer);
	}

	if (ReversedIndexBuffer.GetNumIndices() > 0)
	{
		BeginInitResource(&ReversedIndexBuffer);
	}

	if (DepthOnlyIndexBuffer.GetNumIndices() > 0)
	{
		BeginInitResource(&DepthOnlyIndexBuffer);
	}

	if (ReversedDepthOnlyIndexBuffer.GetNumIndices() > 0)
	{
		BeginInitResource(&ReversedDepthOnlyIndexBuffer);
	}

	if (RHISupportsTessellation(MaxShaderPlatform))
	{
		BeginInitResource(&AdjacencyIndexBuffer);
	}

	if (DistanceFieldData)
	{
		DistanceFieldData->VolumeTexture.Initialize(Parent);
		INC_DWORD_STAT_BY( STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes() );
	}

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		UpdateMemoryStats,
		FStaticMeshLODResources*, This, this,
		{		
			const uint32 StaticMeshVertexMemory =
			This->VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
			This->VertexBuffers.PositionVertexBuffer.GetStride() * This->VertexBuffers.PositionVertexBuffer.GetNumVertices();
			const uint32 ResourceVertexColorMemory = This->VertexBuffers.ColorVertexBuffer.GetStride() * This->VertexBuffers.ColorVertexBuffer.GetNumVertices();

			INC_DWORD_STAT_BY( STAT_StaticMeshVertexMemory, StaticMeshVertexMemory );
			INC_DWORD_STAT_BY( STAT_ResourceVertexColorMemory, ResourceVertexColorMemory );
		});
}

void FStaticMeshLODResources::ReleaseResources()
{
	const uint32 StaticMeshVertexMemory = 
		VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
		VertexBuffers.PositionVertexBuffer.GetStride() * VertexBuffers.PositionVertexBuffer.GetNumVertices();
	const uint32 ResourceVertexColorMemory = VertexBuffers.ColorVertexBuffer.GetStride() * VertexBuffers.ColorVertexBuffer.GetNumVertices();

	DEC_DWORD_STAT_BY( STAT_StaticMeshVertexMemory, StaticMeshVertexMemory );
	DEC_DWORD_STAT_BY( STAT_ResourceVertexColorMemory, ResourceVertexColorMemory );
	DEC_DWORD_STAT_BY( STAT_StaticMeshIndexMemory, StaticMeshIndexMemory );

	// Release the vertex and index buffers.
	
	// AdjacencyIndexBuffer may not be initialized at this time, but it is safe to release it anyway.
	// The bInitialized flag will be safely checked in the render thread.
	// This avoids a race condition regarding releasing this resource.
	BeginReleaseResource(&AdjacencyIndexBuffer);

	BeginReleaseResource(&IndexBuffer);
	BeginReleaseResource(&WireframeIndexBuffer);
	BeginReleaseResource(&VertexBuffers.StaticMeshVertexBuffer);
	BeginReleaseResource(&VertexBuffers.PositionVertexBuffer);
	BeginReleaseResource(&VertexBuffers.ColorVertexBuffer);
	BeginReleaseResource(&ReversedIndexBuffer);
	BeginReleaseResource(&DepthOnlyIndexBuffer);
	BeginReleaseResource(&ReversedDepthOnlyIndexBuffer);

	if (DistanceFieldData)
	{
		DEC_DWORD_STAT_BY( STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes() );
		DistanceFieldData->VolumeTexture.Release();
	}
}

/*------------------------------------------------------------------------------
	FStaticMeshRenderData
------------------------------------------------------------------------------*/

FStaticMeshRenderData::FStaticMeshRenderData()
	: bLODsShareStaticLighting(false)
{
	for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
	{
		ScreenSize[LODIndex] = 0.0f;
	}
}

void FStaticMeshRenderData::Serialize(FArchive& Ar, UStaticMesh* Owner, bool bCooked)
{
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("FStaticMeshRenderData::Serialize"), STAT_StaticMeshRenderData_Serialize, STATGROUP_LoadTime );

	// Note: this is all derived data, native versioning is not needed, but be sure to bump STATICMESH_DERIVEDDATA_VER when modifying!
#if WITH_EDITOR
	const bool bHasEditorData = !Owner->GetOutermost()->bIsCookedForEditor;
	if (Ar.IsSaving() && bHasEditorData)
	{
		ResolveSectionInfo(Owner);
	}
#endif
#if WITH_EDITORONLY_DATA
	if (!bCooked)
	{
		Ar << WedgeMap;
		Ar << MaterialIndexToImportIndex;
	}

#endif // #if WITH_EDITORONLY_DATA

	LODResources.Serialize(Ar, Owner);
	if (Ar.IsLoading())
	{
		LODVertexFactories.Empty(LODResources.Num());
		for (int i = 0; i < LODResources.Num(); i++)
		{
			new(LODVertexFactories) FStaticMeshVertexFactories(ERHIFeatureLevel::Num);
		}
	}

	// Inline the distance field derived data for cooked builds
	if (bCooked)
	{
		FStripDataFlags StripFlags( Ar );
		if ( !StripFlags.IsDataStrippedForServer() )
		{
			if (Ar.IsSaving())
			{
				GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Owner, false);
			}

			for (int32 ResourceIndex = 0; ResourceIndex < LODResources.Num(); ResourceIndex++)
			{
				FStaticMeshLODResources& LOD = LODResources[ResourceIndex];
				
				bool bStripDistanceFields = false;
				if (Ar.IsCooking())
				{
					bStripDistanceFields = !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DeferredRendering);
				}
				
				bool bValid = (LOD.DistanceFieldData != NULL) && !bStripDistanceFields;

				Ar << bValid;

				if (bValid)
				{
					if (!LOD.DistanceFieldData)
					{
						LOD.DistanceFieldData = new FDistanceFieldVolumeData();
					}

					Ar << *(LOD.DistanceFieldData);
				}
			}
		}
	}

	Ar << Bounds;
	Ar << bLODsShareStaticLighting;

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		float DummyFactor;
		for (int32 TexCoordIndex = 0; TexCoordIndex < MAX_STATIC_TEXCOORDS; ++TexCoordIndex)
		{
			Ar << DummyFactor; // StreamingTextureFactors[TexCoordIndex];
		}
		Ar << DummyFactor; // MaxStreamingTextureFactor;
	}

	if (bCooked)
	{
		for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			Ar << ScreenSize[LODIndex];
		}
	}
}

void FStaticMeshRenderData::InitResources(ERHIFeatureLevel::Type InFeatureLevel, UStaticMesh* Owner)
{
#if WITH_EDITOR
	ResolveSectionInfo(Owner);
#endif // #if WITH_EDITOR

	for (int32 LODIndex = 0; LODIndex < LODResources.Num(); ++LODIndex)
	{
		// Skip LODs that have their render data stripped
		if (LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0)
		{
			LODResources[LODIndex].InitResources(Owner);
			LODVertexFactories[LODIndex].InitResources(LODResources[LODIndex], Owner);
		}
	}
}

void FStaticMeshRenderData::ReleaseResources()
{
	for (int32 LODIndex = 0; LODIndex < LODResources.Num(); ++LODIndex)
	{
		if (LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0)
		{
			LODResources[LODIndex].ReleaseResources();
			LODVertexFactories[LODIndex].ReleaseResources();
		}
	}
}

void FStaticMeshRenderData::AllocateLODResources(int32 NumLODs)
{
	check(LODResources.Num() == 0);
	while (LODResources.Num() < NumLODs)
	{
		new(LODResources) FStaticMeshLODResources;
		new(LODVertexFactories) FStaticMeshVertexFactories(ERHIFeatureLevel::Num);
	}
}

FStaticMeshOccluderData::FStaticMeshOccluderData()
{
	VerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
	IndicesSP = MakeShared<FOccluderIndexArray, ESPMode::ThreadSafe>();
}

SIZE_T FStaticMeshOccluderData::GetResourceSizeBytes() const
{
	return VerticesSP->GetAllocatedSize() + IndicesSP->GetAllocatedSize();
}

TUniquePtr<FStaticMeshOccluderData> FStaticMeshOccluderData::Build(UStaticMesh* Owner)
{
	TUniquePtr<FStaticMeshOccluderData> Result;
#if WITH_EDITOR		
	if (Owner->LODForOccluderMesh >= 0)
	{
		// TODO: Custom geometry for occluder mesh?
		int32 LODIndex = FMath::Min(Owner->LODForOccluderMesh, Owner->RenderData->LODResources.Num()-1);
		const FStaticMeshLODResources& LODModel = Owner->RenderData->LODResources[LODIndex];
			
		const FRawStaticIndexBuffer& IndexBuffer = LODModel.DepthOnlyIndexBuffer.GetNumIndices() > 0 ? LODModel.DepthOnlyIndexBuffer : LODModel.IndexBuffer;
		int32 NumVtx = LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		int32 NumIndices = IndexBuffer.GetNumIndices();
		
		if (NumVtx > 0 && NumIndices > 0 && !IndexBuffer.Is32Bit())
		{
			Result = MakeUnique<FStaticMeshOccluderData>();
		
			Result->VerticesSP->SetNumUninitialized(NumVtx);
			Result->IndicesSP->SetNumUninitialized(NumIndices);

			const FVector* V0 = &LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(0);
			const uint16* Indices = IndexBuffer.AccessStream16();

			FMemory::Memcpy(Result->VerticesSP->GetData(), V0, NumVtx*sizeof(FVector));
			FMemory::Memcpy(Result->IndicesSP->GetData(), Indices, NumIndices*sizeof(uint16));
		}
	}
#endif // WITH_EDITOR
	return Result;
}

void FStaticMeshOccluderData::SerializeCooked(FArchive& Ar, UStaticMesh* Owner)
{
#if WITH_EDITOR	
	if (Ar.IsSaving())
	{
		bool bHasOccluderData = false;
		if (Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::SoftwareOcclusion) && Owner->OccluderData.IsValid())
		{
			bHasOccluderData = true;
		}
		
		Ar << bHasOccluderData;
		
		if (bHasOccluderData)
		{
			Owner->OccluderData->VerticesSP->BulkSerialize(Ar);
			Owner->OccluderData->IndicesSP->BulkSerialize(Ar);
		}
	}
	else
#endif // WITH_EDITOR
	{
		bool bHasOccluderData;
		Ar << bHasOccluderData;
		if (bHasOccluderData)
		{
			Owner->OccluderData = MakeUnique<FStaticMeshOccluderData>();
			Owner->OccluderData->VerticesSP->BulkSerialize(Ar);
			Owner->OccluderData->IndicesSP->BulkSerialize(Ar);
		}
	}
}


#if WITH_EDITOR
/**
 * Calculates the view distance that a mesh should be displayed at.
 * @param MaxDeviation - The maximum surface-deviation between the reduced geometry and the original. This value should be acquired from Simplygon
 * @returns The calculated view distance	 
 */
static float CalculateViewDistance(float MaxDeviation, float AllowedPixelError)
{
	// We want to solve for the depth in world space given the screen space distance between two pixels
	//
	// Assumptions:
	//   1. There is no scaling in the view matrix.
	//   2. The horizontal FOV is 90 degrees.
	//   3. The backbuffer is 1920x1080.
	//
	// If we project two points at (X,Y,Z) and (X',Y,Z) from view space, we get their screen
	// space positions: (X/Z, Y'/Z) and (X'/Z, Y'/Z) where Y' = Y * AspectRatio.
	//
	// The distance in screen space is then sqrt( (X'-X)^2/Z^2 + (Y'-Y')^2/Z^2 )
	// or (X'-X)/Z. This is in clip space, so PixelDist = 1280 * 0.5 * (X'-X)/Z.
	//
	// Solving for Z: ViewDist = (X'-X * 640) / PixelDist

	const float ViewDistance = (MaxDeviation * 960.0f) / FMath::Max(AllowedPixelError, UStaticMesh::MinimumAutoLODPixelError);
	return ViewDistance;
}

void FStaticMeshRenderData::ResolveSectionInfo(UStaticMesh* Owner)
{
	int32 LODIndex = 0;
	int32 MaxLODs = LODResources.Num();
	check(MaxLODs <= MAX_STATIC_MESH_LODS);
	for (; LODIndex < MaxLODs; ++LODIndex)
	{
		FStaticMeshLODResources& LOD = LODResources[LODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			FMeshSectionInfo Info = Owner->SectionInfoMap.Get(LODIndex,SectionIndex);
			FStaticMeshSection& Section = LOD.Sections[SectionIndex];
			Section.MaterialIndex = Info.MaterialIndex;
			Section.bEnableCollision = Info.bEnableCollision;
			Section.bCastShadow = Info.bCastShadow;
		}

		// Arbitrary constant used as a base in Pow(K, LODIndex) that achieves much the same progression as a
		// conversion of the old 1 / (MaxLODs * LODIndex) passed through the newer bounds computation.
		// i.e. this achieves much the same results, but is still fairly arbitrary.
		const float AutoComputeLODPowerBase = 0.75f;

		if (Owner->bAutoComputeLODScreenSize)
		{
			if (LODIndex == 0)
			{
				ScreenSize[LODIndex].Default = 1.0f;
			}
			else if(LOD.MaxDeviation <= 0.0f)
			{
				ScreenSize[LODIndex].Default = FMath::Pow(AutoComputeLODPowerBase, LODIndex);
			}
			else
			{
				const float PixelError = Owner->SourceModels.IsValidIndex(LODIndex) ? Owner->SourceModels[LODIndex].ReductionSettings.PixelError : UStaticMesh::MinimumAutoLODPixelError;
				const float ViewDistance = CalculateViewDistance(LOD.MaxDeviation, PixelError);

				// Generate a projection matrix.
				// ComputeBoundsScreenSize only uses (0, 0) and (1, 1) of this matrix.
				const float HalfFOV = PI * 0.25f;
				const float ScreenWidth = 1920.0f;
				const float ScreenHeight = 1080.0f;
				const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);

				// Note we offset ViewDistance by SphereRadius here because the MaxDeviation is known to be somewhere in the bounds of the mesh. 
				// It won't necessarily be at the origin. Before adding this factor for very high poly meshes it would calculate a very small deviation 
				// for LOD1 which translates to a very small ViewDistance and a large (larger than 1) ScreenSize. This meant you could clip the camera 
				// into the mesh but unless you were near its origin it wouldn't switch to LOD0. Adding SphereRadius to ViewDistance makes it so that 
				// the distance is to the bounds which corrects the problem.
				ScreenSize[LODIndex].Default = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ViewDistance + Bounds.SphereRadius), ProjMatrix);
			}
			
			//We must enforce screen size coherence between LOD when we autocompute the LOD screensize
			//This case can happen if we mix auto generate LOD with custom LOD
			if (LODIndex > 0 && ScreenSize[LODIndex].Default > ScreenSize[LODIndex - 1].Default)
			{
				ScreenSize[LODIndex].Default = ScreenSize[LODIndex - 1].Default / 2.0f;
			}
		}
		else if (Owner->SourceModels.IsValidIndex(LODIndex))
		{
			ScreenSize[LODIndex] = Owner->SourceModels[LODIndex].ScreenSize;
		}
		else
		{
			check(LODIndex > 0);

			// No valid source model and we're not auto-generating. Auto-generate in this case
			// because we have nothing else to go on.
			const float Tolerance = 0.01f;
			float AutoDisplayFactor = FMath::Pow(AutoComputeLODPowerBase, LODIndex);

			// Make sure this fits in with the previous LOD
			ScreenSize[LODIndex].Default = FMath::Clamp(AutoDisplayFactor, 0.0f, ScreenSize[LODIndex-1].Default - Tolerance);
		}
	}
	for (; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
	{
		ScreenSize[LODIndex].Default = 0.0f;
	}
}

void FStaticMeshRenderData::SyncUVChannelData(const TArray<FStaticMaterial>& ObjectData)
{
	TUniquePtr< TArray<FMeshUVChannelInfo> > UpdateData = MakeUnique< TArray<FMeshUVChannelInfo> >();
	UpdateData->Empty(ObjectData.Num());

	for (const FStaticMaterial& StaticMaterial : ObjectData)
	{
		UpdateData->Add(StaticMaterial.UVChannelData);
	}

	ENQUEUE_RENDER_COMMAND(SyncUVChannelData)([this, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList)
	{
		FMemory::Memswap(&UVChannelDataPerMaterial, UpdateData.Get(), sizeof(TArray<FMeshUVChannelInfo>));
	});
}

/*------------------------------------------------------------------------------
	FStaticMeshLODSettings
------------------------------------------------------------------------------*/

void FStaticMeshLODSettings::Initialize(const FConfigFile& IniFile)
{
	// Ensure there is a default LOD group.
	Groups.FindOrAdd(NAME_None);

	// Read individual entries from a config file.
	const TCHAR* IniSection = TEXT("StaticMeshLODSettings");
	const FConfigSection* Section = IniFile.Find(IniSection);
	if (Section)
	{
		for (TMultiMap<FName,FConfigValue>::TConstIterator It(*Section); It; ++It)
		{
			FName GroupName = It.Key();
			FStaticMeshLODGroup& Group = Groups.FindOrAdd(GroupName);
			ReadEntry(Group, It.Value().GetValue());
		};
	}

	// Do some per-group initialization.
	for (TMap<FName,FStaticMeshLODGroup>::TIterator It(Groups); It; ++It)
	{
		FStaticMeshLODGroup& Group = It.Value();
		float PercentTrianglesPerLOD = Group.DefaultSettings[1].PercentTriangles;
		for (int32 LODIndex = 1; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			float PercentTriangles = Group.DefaultSettings[LODIndex-1].PercentTriangles;
			Group.DefaultSettings[LODIndex] = Group.DefaultSettings[LODIndex - 1];
			Group.DefaultSettings[LODIndex].PercentTriangles = PercentTriangles * PercentTrianglesPerLOD;
		}
	}
}

void FStaticMeshLODSettings::ReadEntry(FStaticMeshLODGroup& Group, FString Entry)
{
	FMeshReductionSettings& Settings = Group.DefaultSettings[0];
	FMeshReductionSettings& Bias = Group.SettingsBias;
	int32 Importance = EMeshFeatureImportance::Normal;

	// Trim whitespace at the beginning.
	Entry.TrimStartInline();

	FParse::Value(*Entry, TEXT("Name="), Group.DisplayName, TEXT("StaticMeshLODSettings"));

	// Remove brackets.
	Entry = Entry.Replace( TEXT("("), TEXT("") );
	Entry = Entry.Replace( TEXT(")"), TEXT("") );
		
	if (FParse::Value(*Entry, TEXT("NumLODs="), Group.DefaultNumLODs))
	{
		Group.DefaultNumLODs = FMath::Clamp<int32>(Group.DefaultNumLODs, 1, MAX_STATIC_MESH_LODS);
	}

	if (FParse::Value(*Entry, TEXT("LightMapResolution="), Group.DefaultLightMapResolution))
	{
		Group.DefaultLightMapResolution = FMath::Max<int32>(Group.DefaultLightMapResolution, 0);
		Group.DefaultLightMapResolution = (Group.DefaultLightMapResolution + 3) & (~3);
	}

	float BasePercentTriangles = 100.0f;
	if (FParse::Value(*Entry, TEXT("BasePercentTriangles="), BasePercentTriangles))
	{
		BasePercentTriangles = FMath::Clamp<float>(BasePercentTriangles, 0.0f, 100.0f);
	}
	Group.DefaultSettings[0].PercentTriangles = BasePercentTriangles * 0.01f;

	float LODPercentTriangles = 100.0f;
	if (FParse::Value(*Entry, TEXT("LODPercentTriangles="), LODPercentTriangles))
	{
		LODPercentTriangles = FMath::Clamp<float>(LODPercentTriangles, 0.0f, 100.0f);
	}
	Group.DefaultSettings[1].PercentTriangles = LODPercentTriangles * 0.01f;

	if (FParse::Value(*Entry, TEXT("MaxDeviation="), Settings.MaxDeviation))
	{
		Settings.MaxDeviation = FMath::Clamp<float>(Settings.MaxDeviation, 0.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("PixelError="), Settings.PixelError))
	{
		Settings.PixelError = FMath::Clamp<float>(Settings.PixelError, 1.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("WeldingThreshold="), Settings.WeldingThreshold))
	{
		Settings.WeldingThreshold = FMath::Clamp<float>(Settings.WeldingThreshold, 0.0f, 10.0f);
	}

	if (FParse::Value(*Entry, TEXT("HardAngleThreshold="), Settings.HardAngleThreshold))
	{
		Settings.HardAngleThreshold = FMath::Clamp<float>(Settings.HardAngleThreshold, 0.0f, 180.0f);
	}

	if (FParse::Value(*Entry, TEXT("SilhouetteImportance="), Importance))
	{
		Settings.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("TextureImportance="), Importance))
	{
		Settings.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("ShadingImportance="), Importance))
	{
		Settings.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	float BasePercentTrianglesMult = 100.0f;
	if (FParse::Value(*Entry, TEXT("BasePercentTrianglesMult="), BasePercentTrianglesMult))
	{
		BasePercentTrianglesMult = FMath::Clamp<float>(BasePercentTrianglesMult, 0.0f, 100.0f);
	}
	Group.BasePercentTrianglesMult = BasePercentTrianglesMult * 0.01f;

	float LODPercentTrianglesMult = 100.0f;
	if (FParse::Value(*Entry, TEXT("LODPercentTrianglesMult="), LODPercentTrianglesMult))
	{
		LODPercentTrianglesMult = FMath::Clamp<float>(LODPercentTrianglesMult, 0.0f, 100.0f);
	}
	Bias.PercentTriangles = LODPercentTrianglesMult * 0.01f;

	if (FParse::Value(*Entry, TEXT("MaxDeviationBias="), Bias.MaxDeviation))
	{
		Bias.MaxDeviation = FMath::Clamp<float>(Bias.MaxDeviation, -1000.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("PixelErrorBias="), Bias.PixelError))
	{
		Bias.PixelError = FMath::Clamp<float>(Bias.PixelError, 1.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("WeldingThresholdBias="), Bias.WeldingThreshold))
	{
		Bias.WeldingThreshold = FMath::Clamp<float>(Bias.WeldingThreshold, -10.0f, 10.0f);
	}

	if (FParse::Value(*Entry, TEXT("HardAngleThresholdBias="), Bias.HardAngleThreshold))
	{
		Bias.HardAngleThreshold = FMath::Clamp<float>(Bias.HardAngleThreshold, -180.0f, 180.0f);
	}

	if (FParse::Value(*Entry, TEXT("SilhouetteImportanceBias="), Importance))
	{
		Bias.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("TextureImportanceBias="), Importance))
	{
		Bias.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("ShadingImportanceBias="), Importance))
	{
		Bias.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}
}

void FStaticMeshLODSettings::GetLODGroupNames(TArray<FName>& OutNames) const
{
	for (TMap<FName,FStaticMeshLODGroup>::TConstIterator It(Groups); It; ++It)
	{
		OutNames.Add(It.Key());
	}
}

void FStaticMeshLODSettings::GetLODGroupDisplayNames(TArray<FText>& OutDisplayNames) const
{
	for (TMap<FName,FStaticMeshLODGroup>::TConstIterator It(Groups); It; ++It)
	{
		OutDisplayNames.Add( It.Value().DisplayName );
	}
}

FMeshReductionSettings FStaticMeshLODGroup::GetSettings(const FMeshReductionSettings& InSettings, int32 LODIndex) const
{
	check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);

	FMeshReductionSettings FinalSettings = InSettings;

	// PercentTriangles is actually a multiplier.
	float PercentTrianglesMult = (LODIndex == 0) ? BasePercentTrianglesMult : SettingsBias.PercentTriangles;
	FinalSettings.PercentTriangles = FMath::Clamp(InSettings.PercentTriangles * PercentTrianglesMult, 0.0f, 1.0f);

	// Bias the remaining settings.
	FinalSettings.MaxDeviation = FMath::Max(InSettings.MaxDeviation + SettingsBias.MaxDeviation, 0.0f);
	FinalSettings.PixelError = FMath::Max(InSettings.PixelError + SettingsBias.PixelError, 1.0f);
	FinalSettings.WeldingThreshold = FMath::Max(InSettings.WeldingThreshold + SettingsBias.WeldingThreshold, 0.0f);
	FinalSettings.HardAngleThreshold = FMath::Clamp(InSettings.HardAngleThreshold + SettingsBias.HardAngleThreshold, 0.0f, 180.0f);
	FinalSettings.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.SilhouetteImportance + SettingsBias.SilhouetteImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	FinalSettings.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.TextureImportance + SettingsBias.TextureImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	FinalSettings.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.ShadingImportance + SettingsBias.ShadingImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	return FinalSettings;
}

void UStaticMesh::GetLODGroups(TArray<FName>& OutLODGroups)
{
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);
	RunningPlatform->GetStaticMeshLODSettings().GetLODGroupNames(OutLODGroups);
}

void UStaticMesh::GetLODGroupsDisplayNames(TArray<FText>& OutLODGroupsDisplayNames)
{
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);
	RunningPlatform->GetStaticMeshLODSettings().GetLODGroupDisplayNames(OutLODGroupsDisplayNames);
}


void UStaticMesh::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		SetLightingGuid();
	}
}

/*------------------------------------------------------------------------------
	FStaticMeshRenderData
------------------------------------------------------------------------------*/

FArchive& operator<<(FArchive& Ar, FMeshReductionSettings& ReductionSettings)
{
	Ar << ReductionSettings.PercentTriangles;
	Ar << ReductionSettings.MaxDeviation;
	Ar << ReductionSettings.PixelError;
	Ar << ReductionSettings.WeldingThreshold;
	Ar << ReductionSettings.HardAngleThreshold;
	Ar << ReductionSettings.SilhouetteImportance;
	Ar << ReductionSettings.TextureImportance;
	Ar << ReductionSettings.ShadingImportance;
	Ar << ReductionSettings.bRecalculateNormals;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FMeshBuildSettings& BuildSettings)
{
	// Note: this serializer is currently only used to build the mesh DDC key, no versioning is required
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeNormals);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeTangents);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseMikkTSpace);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRemoveDegenerates);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bBuildAdjacencyBuffer);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bBuildReversedIndexBuffer);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseHighPrecisionTangentBasis);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseFullPrecisionUVs);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bGenerateLightmapUVs);

	Ar << BuildSettings.MinLightmapResolution;
	Ar << BuildSettings.SrcLightmapIndex;
	Ar << BuildSettings.DstLightmapIndex;

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_BUILD_SCALE_VECTOR)
	{
		float BuildScale(1.0f);
		Ar << BuildScale;
		BuildSettings.BuildScale3D = FVector( BuildScale );
	}
	else
	{
		Ar << BuildSettings.BuildScale3D;
	}
	
	Ar << BuildSettings.DistanceFieldResolutionScale;
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bGenerateDistanceFieldAsIfTwoSided);

	FString ReplacementMeshName = BuildSettings.DistanceFieldReplacementMesh->GetPathName();
	Ar << ReplacementMeshName;

	return Ar;
}

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.                                       
#define STATICMESH_DERIVEDDATA_VER TEXT("3713973CA1B84F41BA1EB2E56FCE9211")

static const FString& GetStaticMeshDerivedDataVersion()
{
	static FString CachedVersionString;
	if (CachedVersionString.IsEmpty())
	{
		// Static mesh versioning is controlled by the version reported by the mesh utilities module.
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		CachedVersionString = FString::Printf(TEXT("%s_%s"),
			STATICMESH_DERIVEDDATA_VER,
			*MeshUtilities.GetVersionString()
			);
	}
	return CachedVersionString;
}

class FStaticMeshStatusMessageContext : public FScopedSlowTask
{
public:
	explicit FStaticMeshStatusMessageContext(const FText& InMessage)
		: FScopedSlowTask(0, InMessage)
	{
		UE_LOG(LogStaticMesh,Log,TEXT("%s"),*InMessage.ToString());
		MakeDialog();
	}
};

namespace StaticMeshDerivedDataTimings
{
	int64 GetCycles = 0;
	int64 BuildCycles = 0;
	int64 ConvertCycles = 0;

	static void DumpTimings()
	{
		UE_LOG(LogStaticMesh,Log,TEXT("Derived Data Times: Get=%.3fs Build=%.3fs ConvertLegacy=%.3fs"),
			FPlatformTime::ToSeconds(GetCycles),
			FPlatformTime::ToSeconds(BuildCycles),
			FPlatformTime::ToSeconds(ConvertCycles)
			);
	}

	static FAutoConsoleCommand DumpTimingsCmd(
		TEXT("sm.DerivedDataTimings"),
		TEXT("Dumps derived data timings to the log."),
		FConsoleCommandDelegate::CreateStatic(DumpTimings)
		);
}

static FString BuildStaticMeshDerivedDataKey(UStaticMesh* Mesh, const FStaticMeshLODGroup& LODGroup)
{
	FString KeySuffix(TEXT(""));
	TArray<uint8> TempBytes;
	TempBytes.Reserve(64);

	// Add LightmapUVVersion to key going forward
	if ( (ELightmapUVVersion)Mesh->LightmapUVVersion > ELightmapUVVersion::BitByBit )
	{
		KeySuffix += LexToString(Mesh->LightmapUVVersion);
	}
#if WITH_EDITOR
	if (GIsAutomationTesting && Mesh->BuildCacheAutomationTestGuid.IsValid())
	{
		//If we are in automation testing and the BuildCacheAutomationTestGuid was set
		KeySuffix += Mesh->BuildCacheAutomationTestGuid.ToString(EGuidFormats::Digits);
	}
#endif

	int32 NumLODs = Mesh->SourceModels.Num();
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		FStaticMeshSourceModel& SrcModel = Mesh->SourceModels[LODIndex];
		KeySuffix += SrcModel.RawMeshBulkData->GetIdString();

		// Serialize the build and reduction settings into a temporary array. The archive
		// is flagged as persistent so that machines of different endianness produce
		// identical binary results.
		TempBytes.Reset();
		FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);
		Ar << SrcModel.BuildSettings;

		ANSICHAR Flag[2] = { (SrcModel.BuildSettings.bUseFullPrecisionUVs || !GVertexElementTypeSupport.IsSupported(VET_Half2)) ? '1' : '0', '\0' };
		Ar.Serialize(Flag, 1);

		FMeshReductionSettings FinalReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LODIndex);
		Ar << FinalReductionSettings;

		// Now convert the raw bytes to a string.
		const uint8* SettingsAsBytes = TempBytes.GetData();
		KeySuffix.Reserve(KeySuffix.Len() + TempBytes.Num() + 1);
		for (int32 ByteIndex = 0; ByteIndex < TempBytes.Num(); ++ByteIndex)
		{
			ByteToHex(SettingsAsBytes[ByteIndex], KeySuffix);
		}
	}

	KeySuffix.AppendChar(Mesh->bSupportUniformlyDistributedSampling ? TEXT('1') : TEXT('0'));

	// Value of this CVar affects index buffer <-> painted vertex color correspondence (see UE-51421).
	static const TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TriangleOrderOptimization"));

	// depending on module loading order this might be called too early on Linux (possibly other platforms too?)
	if (CVar == nullptr)
	{
		FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TriangleOrderOptimization"));
	}

	if (CVar)
	{
		switch (CVar->GetValueOnAnyThread())
		{
			case 2:
				KeySuffix += TEXT("_NoTOO");
				break;
			case 0:
				KeySuffix += TEXT("_NVTS");
				break;
			case 1:
				// intentional - default value will not influence DDC to avoid unnecessary invalidation
				break;
			default:
				KeySuffix += FString::Printf(TEXT("_TOO%d"), CVar->GetValueOnAnyThread());	//	 allow unknown values transparently
				break;
		}
	}

	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("STATICMESH"),
		*GetStaticMeshDerivedDataVersion(),
		*KeySuffix
		);
}

void FStaticMeshRenderData::ComputeUVDensities()
{
#if WITH_EDITORONLY_DATA
	for (FStaticMeshLODResources& LODModel : LODResources)
	{
		const int32 NumTexCoords = FMath::Min<int32>(LODModel.GetNumTexCoords(), MAX_STATIC_TEXCOORDS);

		for (FStaticMeshSection& SectionInfo : LODModel.Sections)
		{
			FMemory::Memzero(SectionInfo.UVDensities);
			FMemory::Memzero(SectionInfo.Weights);

			FUVDensityAccumulator UVDensityAccs[MAX_STATIC_TEXCOORDS];
			for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
			{
				UVDensityAccs[UVIndex].Reserve(SectionInfo.NumTriangles);
			}

			FIndexArrayView IndexBuffer = LODModel.IndexBuffer.GetArrayView();

			for (uint32 TriangleIndex = 0; TriangleIndex < SectionInfo.NumTriangles; ++TriangleIndex)
			{
				const int32 Index0 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 0];
				const int32 Index1 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 1];
				const int32 Index2 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 2];

				const float Aera = FUVDensityAccumulator::GetTriangleAera(
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index0), 
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index1), 
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index2));

				if (Aera > SMALL_NUMBER)
				{
					for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
					{
						const float UVAera = FUVDensityAccumulator::GetUVChannelAera(
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVIndex), 
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVIndex), 
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVIndex));

						UVDensityAccs[UVIndex].PushTriangle(Aera, UVAera);
					}
				}
			}

			for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
			{
				float WeightedUVDensity = 0;
				float Weight = 0;
				UVDensityAccs[UVIndex].AccumulateDensity(WeightedUVDensity, Weight);

				if (Weight > SMALL_NUMBER)
				{
					SectionInfo.UVDensities[UVIndex] = WeightedUVDensity / Weight;
					SectionInfo.Weights[UVIndex] = Weight;
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void FStaticMeshRenderData::BuildAreaWeighedSamplingData()
{
	for (FStaticMeshLODResources& LODModel : LODResources)
	{
		for (FStaticMeshSection& SectionInfo : LODModel.Sections)
		{
			LODModel.AreaWeightedSectionSamplers.SetNum(LODModel.Sections.Num());
			for (int32 i = 0; i < LODModel.Sections.Num(); ++i)
			{
				LODModel.AreaWeightedSectionSamplers[i].Init(&LODModel, i);
			}
			LODModel.AreaWeightedSampler.Init(&LODModel);
		}
	}
}

void FStaticMeshRenderData::Cache(UStaticMesh* Owner, const FStaticMeshLODSettings& LODSettings)
{
	if (Owner->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		// Don't cache for cooked packages
		return;
	}


	{
		COOK_STAT(auto Timer = StaticMeshCookStats::UsageStats.TimeSyncWork());
		int32 T0 = FPlatformTime::Cycles();
		int32 NumLODs = Owner->SourceModels.Num();
		const FStaticMeshLODGroup& LODGroup = LODSettings.GetLODGroup(Owner->LODGroup);
		DerivedDataKey = BuildStaticMeshDerivedDataKey(Owner, LODGroup);

		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData))
		{
			COOK_STAT(Timer.AddHit(DerivedData.Num()));
			FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);
			Serialize(Ar, Owner, /*bCooked=*/ false);

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogStaticMesh,Verbose,TEXT("Static mesh found in DDC [%fms] %s"),
				FPlatformTime::ToMilliseconds(T1-T0),
				*Owner->GetPathName()
				);
			FPlatformAtomics::InterlockedAdd(&StaticMeshDerivedDataTimings::GetCycles, T1 - T0);
		}
		else
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("StaticMeshName"), FText::FromString( Owner->GetName() ) );
			FStaticMeshStatusMessageContext StatusContext( FText::Format( NSLOCTEXT("Engine", "BuildingStaticMeshStatus", "Building static mesh {StaticMeshName}..."), Args ) );

			bool bUseMeshDescription = false;
			if (Owner->GetOriginalMeshDescription(0) != nullptr)
			{
				bUseMeshDescription = true;
			}

			if (bUseMeshDescription)
			{
				IMeshBuilderModule& MeshBuilderModule = FModuleManager::Get().LoadModuleChecked<IMeshBuilderModule>(TEXT("MeshBuilder"));
				if (!MeshBuilderModule.BuildMesh(*this, Owner, LODGroup))
				{
					UE_LOG(LogStaticMesh, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
					return;
				}
			}
			else
			{
				IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
				if (!MeshUtilities.BuildStaticMesh(*this, Owner, LODGroup))
				{
					UE_LOG(LogStaticMesh, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
					return;
				}
			}

			ComputeUVDensities();
			if(Owner->bSupportUniformlyDistributedSampling)
			{
				BuildAreaWeighedSamplingData();
			}
			bLODsShareStaticLighting = Owner->CanLODsShareStaticLighting();
			FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
			Serialize(Ar, Owner, /*bCooked=*/ false);
			GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData);

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogStaticMesh,Log,TEXT("Built static mesh [%.2fs] %s"),
				FPlatformTime::ToMilliseconds(T1-T0) / 1000.0f,
				*Owner->GetPathName()
				);
			FPlatformAtomics::InterlockedAdd(&StaticMeshDerivedDataTimings::BuildCycles, T1 - T0);
			COOK_STAT(Timer.AddMiss(DerivedData.Num()));
		}
	}

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));

	if (CVar->GetValueOnGameThread() != 0 || Owner->bGenerateMeshDistanceField)
	{
		FString DistanceFieldKey = BuildDistanceFieldDerivedDataKey(DerivedDataKey);
		if (LODResources.IsValidIndex(0))
		{
			if (!LODResources[0].DistanceFieldData)
			{
				LODResources[0].DistanceFieldData = new FDistanceFieldVolumeData();
			}

			const FMeshBuildSettings& BuildSettings = Owner->SourceModels[0].BuildSettings;
			UStaticMesh* MeshToGenerateFrom = BuildSettings.DistanceFieldReplacementMesh ? BuildSettings.DistanceFieldReplacementMesh : Owner;

			if (BuildSettings.DistanceFieldReplacementMesh)
			{
				// Make sure dependency is postloaded
				BuildSettings.DistanceFieldReplacementMesh->ConditionalPostLoad();
			}

			LODResources[0].DistanceFieldData->CacheDerivedData(DistanceFieldKey, Owner, MeshToGenerateFrom, BuildSettings.DistanceFieldResolutionScale, BuildSettings.bGenerateDistanceFieldAsIfTwoSided);
		}
		else
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Failed to generate distance field data for %s due to missing LODResource for LOD 0."), *Owner->GetPathName());
		}
	}
}
#endif // #if WITH_EDITOR

FArchive& operator<<(FArchive& Ar, FStaticMaterial& Elem)
{
	Ar << Elem.MaterialInterface;

	Ar << Elem.MaterialSlotName;
#if WITH_EDITORONLY_DATA
	if((!Ar.IsCooking() && !Ar.IsFilterEditorOnly()) || (Ar.IsCooking() && Ar.CookingTarget()->HasEditorOnlyData()))
	{
		Ar << Elem.ImportedMaterialSlotName;
	}
#endif //#if WITH_EDITORONLY_DATA

	if (!Ar.IsLoading() || Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		Ar << Elem.UVChannelData;
	}
	
	return Ar;
}

bool operator== (const FStaticMaterial& LHS, const FStaticMaterial& RHS)
{
	return (LHS.MaterialInterface == RHS.MaterialInterface &&
		LHS.MaterialSlotName == RHS.MaterialSlotName
#if WITH_EDITORONLY_DATA
		&& LHS.ImportedMaterialSlotName == RHS.ImportedMaterialSlotName
#endif
		);
}

bool operator== (const FStaticMaterial& LHS, const UMaterialInterface& RHS)
{
	return (LHS.MaterialInterface == &RHS);
}

bool operator== (const UMaterialInterface& LHS, const FStaticMaterial& RHS)
{
	return (RHS.MaterialInterface == &LHS);
}

/*-----------------------------------------------------------------------------
UStaticMesh
-----------------------------------------------------------------------------*/

#if WITH_EDITORONLY_DATA
const float UStaticMesh::MinimumAutoLODPixelError = SMALL_NUMBER;
#endif	//#if WITH_EDITORONLY_DATA

UStaticMesh::UStaticMesh(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer)
{
	ElementToIgnoreForTexFactor = -1;
	bHasNavigationData=true;
#if WITH_EDITORONLY_DATA
	bAutoComputeLODScreenSize=true;
	ImportVersion = EImportStaticMeshVersion::BeforeImportStaticMeshVersionWasAdded;
	LODForOccluderMesh = -1;
#endif // #if WITH_EDITORONLY_DATA
	LightMapResolution = 4;
	LpvBiasMultiplier = 1.0f;
	MinLOD.Default = 0;

	bSupportUniformlyDistributedSampling = false;
	bRenderingResourcesInitialized = false;
#if WITH_EDITOR
	BuildCacheAutomationTestGuid.Invalidate();
#endif
}

void UStaticMesh::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
	Super::PostInitProperties();
}

/**
 * Initializes the static mesh's render resources.
 */
void UStaticMesh::InitResources()
{
	bRenderingResourcesInitialized = true;

	UpdateUVChannelData(false);

	if (RenderData)
	{
		RenderData->InitResources(GetWorld() ? GetWorld()->FeatureLevel : ERHIFeatureLevel::Num, this);
	}

	if (OccluderData)
	{
		INC_DWORD_STAT_BY( STAT_StaticMeshOccluderMemory, OccluderData->GetResourceSizeBytes() );
	}
	
#if	STATS
	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		UpdateMemoryStats,
		UStaticMesh*, This, this,
		{
			const uint32 StaticMeshResourceSize = This->GetResourceSizeBytes( EResourceSizeMode::Exclusive );
			INC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory, StaticMeshResourceSize );
			INC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory2, StaticMeshResourceSize );
		} );
#endif // STATS
}

void UStaticMesh::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (RenderData)
	{
		RenderData->GetResourceSizeEx(CumulativeResourceSize);
	}

	if (OccluderData)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(OccluderData->GetResourceSizeBytes());
	}
}

void FStaticMeshRenderData::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));

	// Count dynamic arrays.
	CumulativeResourceSize.AddUnknownMemoryBytes(LODResources.GetAllocatedSize());

	for(int32 LODIndex = 0;LODIndex < LODResources.Num();LODIndex++)
	{
		const FStaticMeshLODResources& LODRenderData = LODResources[LODIndex];

		const int32 VBSize = LODRenderData.VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
			LODRenderData.VertexBuffers.PositionVertexBuffer.GetStride()			* LODRenderData.VertexBuffers.PositionVertexBuffer.GetNumVertices() +
			LODRenderData.VertexBuffers.ColorVertexBuffer.GetStride()				* LODRenderData.VertexBuffers.ColorVertexBuffer.GetNumVertices();
		const int32 IBSize = LODRenderData.IndexBuffer.GetAllocatedSize()
			+ LODRenderData.WireframeIndexBuffer.GetAllocatedSize()
			+ (RHISupportsTessellation(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]) ? LODRenderData.AdjacencyIndexBuffer.GetAllocatedSize() : 0);

		CumulativeResourceSize.AddUnknownMemoryBytes(VBSize + IBSize);
		CumulativeResourceSize.AddUnknownMemoryBytes(LODRenderData.Sections.GetAllocatedSize());

		if (LODRenderData.DistanceFieldData)
		{
			LODRenderData.DistanceFieldData->GetResourceSizeEx(CumulativeResourceSize);
		}
	}

#if WITH_EDITORONLY_DATA
	// If render data for multiple platforms is loaded, count it all.
	if (NextCachedRenderData)
	{
		NextCachedRenderData->GetResourceSizeEx(CumulativeResourceSize);
	}
#endif // #if WITH_EDITORONLY_DATA
}

int32 UStaticMesh::GetNumVertices(int32 LODIndex) const
{
	int32 NumVertices = 0;
	if (RenderData && RenderData->LODResources.IsValidIndex(LODIndex))
	{
		NumVertices = RenderData->LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
	}
	return NumVertices;
}

int32 UStaticMesh::GetNumLODs() const
{
	int32 NumLODs = 0;
	if (RenderData)
	{
		NumLODs = RenderData->LODResources.Num();
	}
	return NumLODs;
}

// pass false for bCheckLODForVerts for any runtime code that can handle empty LODs, for example due to them being stripped
//  as a result of minimum LOD setup on the static mesh; in cooked builds, those verts are stripped, but systems still need to
//  be able to handle these cases; to check specifically for an LOD, pass true (default arg), and an LOD index (default arg implies MinLOD)
//
bool UStaticMesh::HasValidRenderData(bool bCheckLODForVerts, int32 LODIndex) const
{
	if (RenderData != nullptr
		&& RenderData->LODResources.Num() > 0
		&& RenderData->LODResources.GetData() != nullptr)
	{
		if (bCheckLODForVerts)
		{
		    if (LODIndex == INDEX_NONE)
		    {
			    LODIndex = FMath::Clamp<int32>(MinLOD.GetValueForFeatureLevel(GMaxRHIFeatureLevel), 0, RenderData->LODResources.Num() - 1);
		    }
			return (RenderData->LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0);
		}
		else
		{
			return true;
		}
	}
	return false;
}

FBoxSphereBounds UStaticMesh::GetBounds() const
{
	return ExtendedBounds;
}

FBox UStaticMesh::GetBoundingBox() const
{
	return ExtendedBounds.GetBox();
}

int32 UStaticMesh::GetNumSections(int32 InLOD) const
{
	int32 NumSections = 0;
	if (RenderData != NULL && RenderData->LODResources.IsValidIndex(InLOD))
	{
		const FStaticMeshLODResources& LOD = RenderData->LODResources[InLOD];
		NumSections = LOD.Sections.Num();
	}
	return NumSections;
}

#if WITH_EDITORONLY_DATA
static float GetUVDensity(const TIndirectArray<FStaticMeshLODResources>& LODResources, int32 UVIndex)
{
	float WeightedUVDensity = 0;
	float WeightSum = 0;

	if (UVIndex < MAX_STATIC_TEXCOORDS)
	{
		// Parse all LOD-SECTION using this material index.
		for (const FStaticMeshLODResources& LODModel : LODResources)
		{
			if (UVIndex < LODModel.GetNumTexCoords())
			{
				for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
				{
					WeightedUVDensity += SectionInfo.UVDensities[UVIndex] * SectionInfo.Weights[UVIndex];
					WeightSum += SectionInfo.Weights[UVIndex];
				}
			}
		}
	}

	return (WeightSum > SMALL_NUMBER) ? (WeightedUVDensity / WeightSum) : 0;
}
#endif

void UStaticMesh::UpdateUVChannelData(bool bRebuildAll)
{
#if WITH_EDITORONLY_DATA
	// Once cooked, the data required to compute the scales will not be CPU accessible.
	if (FPlatformProperties::HasEditorOnlyData() && RenderData)
	{
		bool bDensityChanged = false;

		for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
		{
			FMeshUVChannelInfo& UVChannelData = StaticMaterials[MaterialIndex].UVChannelData;

			// Skip it if we want to keep it.
			if (UVChannelData.bInitialized && (!bRebuildAll || UVChannelData.bOverrideDensities))
			{
				continue;
			}

			float WeightedUVDensities[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};
			float Weights[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};

			// Parse all LOD-SECTION using this material index.
			for (const FStaticMeshLODResources& LODModel : RenderData->LODResources)
			{
				const int32 NumTexCoords = FMath::Min<int32>(LODModel.GetNumTexCoords(), TEXSTREAM_MAX_NUM_UVCHANNELS);
				for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
				{
					if (SectionInfo.MaterialIndex == MaterialIndex)
					{
						for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
						{
							WeightedUVDensities[UVIndex] += SectionInfo.UVDensities[UVIndex] * SectionInfo.Weights[UVIndex];
							Weights[UVIndex] += SectionInfo.Weights[UVIndex];
						}

						// If anything needs to be updated, also update the lightmap densities.
						bDensityChanged = true;
					}
				}
			}

			UVChannelData.bInitialized = true;
			UVChannelData.bOverrideDensities = false;
			for (int32 UVIndex = 0; UVIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++UVIndex)
			{
				UVChannelData.LocalUVDensities[UVIndex] = (Weights[UVIndex] > SMALL_NUMBER) ? (WeightedUVDensities[UVIndex] / Weights[UVIndex]) : 0;
			}
		}

		if (bDensityChanged || bRebuildAll)
		{
			LightmapUVDensity = GetUVDensity(RenderData->LODResources, LightMapCoordinateIndex);

			if (GEngine)
			{
				GEngine->TriggerStreamingDataRebuild();
			}
		}

		// Update the data for the renderthread debug viewmodes
		RenderData->SyncUVChannelData(StaticMaterials);
	}
#endif
}

#if WITH_EDITORONLY_DATA
static void AccumulateBounds(FBox& Bounds, const FStaticMeshLODResources& LODModel, const FStaticMeshSection& SectionInfo, const FTransform& Transform)
{
	const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;
	FIndexArrayView IndexBuffer = LODModel.IndexBuffer.GetArrayView();

	FBox TransformedBox(ForceInit);
	for (uint32 TriangleIndex = 0; TriangleIndex < SectionInfo.NumTriangles; ++TriangleIndex)
	{
		const int32 Index0 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 0];
		const int32 Index1 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 1];
		const int32 Index2 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 2];

		FVector Pos1 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index1));
		FVector Pos2 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index2));
		FVector Pos0 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index0));

		Bounds += Pos0;
		Bounds += Pos1;
		Bounds += Pos2;
	}
}
#endif

FBox UStaticMesh::GetMaterialBox(int32 MaterialIndex, const FTransform& Transform) const
{
#if WITH_EDITORONLY_DATA
	// Once cooked, the data requires to compute the scales will not be CPU accessible.
	if (FPlatformProperties::HasEditorOnlyData() && RenderData)
	{
		FBox MaterialBounds(ForceInit);
		for (const FStaticMeshLODResources& LODModel : RenderData->LODResources)
		{
			for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
			{
				if (SectionInfo.MaterialIndex != MaterialIndex)
					continue;

				AccumulateBounds(MaterialBounds, LODModel, SectionInfo, Transform);
			}
		}
		return MaterialBounds;
	}
#endif
	// Fallback back using the full bounds.
	return GetBoundingBox().TransformBy(Transform);
}

const FMeshUVChannelInfo* UStaticMesh::GetUVChannelData(int32 MaterialIndex) const
{
	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		ensure(StaticMaterials[MaterialIndex].UVChannelData.bInitialized);
		return &StaticMaterials[MaterialIndex].UVChannelData;
	}

	return nullptr;
}

/**
 * Releases the static mesh's render resources.
 */
void UStaticMesh::ReleaseResources()
{
#if STATS
	uint32 StaticMeshResourceSize = GetResourceSizeBytes(EResourceSizeMode::Exclusive);
	DEC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory, StaticMeshResourceSize );
	DEC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory2, StaticMeshResourceSize );
#endif

	if (RenderData)
	{
		RenderData->ReleaseResources();
	}

	if (OccluderData)
	{
		DEC_DWORD_STAT_BY( STAT_StaticMeshOccluderMemory, OccluderData->GetResourceSizeBytes() );
	}
	
	// insert a fence to signal when these commands completed
	ReleaseResourcesFence.BeginFence();

	bRenderingResourcesInitialized = false;
}

#if WITH_EDITOR
void UStaticMesh::PreEditChange(UProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	// Release the static mesh's resources.
	ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the edit change doesn't occur while a resource is still
	// allocated, and potentially accessing the UStaticMesh.
	ReleaseResourcesFence.Wait();
}

void UStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LODGroup))
	{
		// Force an update of LOD group settings

		// Dont rebuild inside here.  We're doing that below.
		bool bRebuild = false;
		SetLODGroup(LODGroup, bRebuild);
	}
	LightMapResolution = FMath::Max(LightMapResolution, 0);

	if (PropertyChangedEvent.MemberProperty 
		&& ((PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStaticMesh, PositiveBoundsExtension)) 
			|| (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStaticMesh, NegativeBoundsExtension))))
	{
		// Update the extended bounds
		CalculateExtendedBounds();
	}

	if (!bAutoComputeLODScreenSize
		&& RenderData
		&& PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, bAutoComputeLODScreenSize))
	{
		for (int32 LODIndex = 1; LODIndex < SourceModels.Num(); ++LODIndex)
		{
			SourceModels[LODIndex].ScreenSize = RenderData->ScreenSize[LODIndex];
		}
	}

	EnforceLightmapRestrictions();

	// Following an undo or other operation which can change the SourceModels, ensure it is in sync with the MeshDescriptions
	LoadMeshDescriptions();
	for (int32 Index = 0; Index < SourceModels.Num(); ++Index)
	{
		SourceModels[Index].OriginalMeshDescription = MeshDescriptions->Get(Index);
		SourceModels[Index].StaticMeshOwner = this;
	}

	Build(/*bSilent=*/ true);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, bHasNavigationData)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, BodySetup))
	{
		// Build called above will result in creation, update or destruction 
		// of NavCollision. We need to let related StaticMeshComponents know
		BroadcastNavCollisionChange();
	}

	// Only unbuild lighting for properties which affect static lighting
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapResolution)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapCoordinateIndex))
	{
		FStaticMeshComponentRecreateRenderStateContext Context(this, true);		
		SetLightingGuid();
	}
	
	UpdateUVChannelData(true);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnMeshChanged.Broadcast();
}

void UStaticMesh::PostEditUndo()
{
	// Following an undo/redo, ensure it is in sync with the MeshDescriptions
	LoadMeshDescriptions();
	for (int32 Index = 0; Index < SourceModels.Num(); ++Index)
	{
		SourceModels[Index].OriginalMeshDescription = MeshDescriptions->Get(Index);
		SourceModels[Index].StaticMeshOwner = this;
	}

	// The super will cause a Build() via PostEditChangeProperty().
	Super::PostEditUndo();
}

void UStaticMesh::SetLODGroup(FName NewGroup, bool bRebuildImmediately)
{
#if WITH_EDITORONLY_DATA
	const bool bBeforeDerivedDataCached = (RenderData == nullptr);
	if (!bBeforeDerivedDataCached)
	{
		Modify();
	}
	LODGroup = NewGroup;
	if (NewGroup != NAME_None)
	{
		const ITargetPlatform* Platform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		check(Platform);
		const FStaticMeshLODGroup& GroupSettings = Platform->GetStaticMeshLODSettings().GetLODGroup(NewGroup);

		// Set the number of LODs to at least the default. If there are already LODs they will be preserved, with default settings of the new LOD group.
		int32 DefaultLODCount = GroupSettings.GetDefaultNumLODs();

		SetNumSourceModels(DefaultLODCount);

		// Set reduction settings to the defaults.
		for (int32 LODIndex = 0; LODIndex < DefaultLODCount; ++LODIndex)
		{
			SourceModels[LODIndex].ReductionSettings = GroupSettings.GetDefaultSettings(LODIndex);
		}
		LightMapResolution = GroupSettings.GetDefaultLightMapResolution();

		if (!bBeforeDerivedDataCached)
		{
			bAutoComputeLODScreenSize = true;
		}
	}
	if (bRebuildImmediately && !bBeforeDerivedDataCached)
	{
		PostEditChange();
	}
#endif
}

void UStaticMesh::BroadcastNavCollisionChange()
{
	if (FNavigationSystem::WantsComponentChangeNotifies())
	{
		for (FObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(*Iter);
			UWorld* MyWorld = StaticMeshComponent->GetWorld();
			if (StaticMeshComponent->GetStaticMesh() == this)
			{
				StaticMeshComponent->bNavigationRelevant = StaticMeshComponent->IsNavigationRelevant();
				FNavigationSystem::UpdateComponentData(*StaticMeshComponent);
			}
		}
	}
}

FStaticMeshSourceModel& UStaticMesh::AddSourceModel()
{
	LoadMeshDescriptions();
	check(MeshDescriptions->Num() == SourceModels.Num());
	int32 LodModelIndex = SourceModels.AddDefaulted();
	MeshDescriptions->SetNum(SourceModels.Num());
	SourceModels[LodModelIndex].StaticMeshOwner = this;
	return SourceModels[LodModelIndex];
}

void UStaticMesh::SetNumSourceModels(const int32 Num)
{
	LoadMeshDescriptions();
	check(MeshDescriptions->Num() == SourceModels.Num());
	const int32 OldNum = SourceModels.Num();
	SourceModels.SetNum(Num);
	MeshDescriptions->SetNum(Num);

	for (int32 Index = OldNum; Index < Num; ++Index)
	{
		SourceModels[Index].StaticMeshOwner = this;
	}
}

void UStaticMesh::RemoveSourceModel(const int32 Index)
{
	LoadMeshDescriptions();
	check(MeshDescriptions->Num() == SourceModels.Num());
	check(SourceModels.IsValidIndex(Index));
	SourceModels.RemoveAt(Index);
	MeshDescriptions->RemoveAt(Index);
}


#endif // WITH_EDITOR

void UStaticMesh::BeginDestroy()
{
	Super::BeginDestroy();

	if (FApp::CanEverRender() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ReleaseResources();
	}
}

bool UStaticMesh::IsReadyForFinishDestroy()
{
	return ReleaseResourcesFence.IsFenceComplete();
}

int32 UStaticMesh::GetNumSectionsWithCollision() const
{
#if WITH_EDITORONLY_DATA
	int32 NumSectionsWithCollision = 0;

	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		// Find how many sections have collision enabled
		const int32 UseLODIndex = FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);
		const FStaticMeshLODResources& CollisionLOD = RenderData->LODResources[UseLODIndex];
		for (int32 SectionIndex = 0; SectionIndex < CollisionLOD.Sections.Num(); ++SectionIndex)
		{
			if (SectionInfoMap.Get(UseLODIndex, SectionIndex).bEnableCollision)
			{
				NumSectionsWithCollision++;
			}
		}
	}

	return NumSectionsWithCollision;
#else
	return 0;
#endif
}

void UStaticMesh::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	int32 NumTriangles = 0;
	int32 NumVertices = 0;
	int32 NumUVChannels = 0;
	int32 NumLODs = 0;

	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
		NumTriangles = LOD.IndexBuffer.GetNumIndices() / 3;
		NumVertices = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
		NumUVChannels = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		NumLODs = RenderData->LODResources.Num();
	}

	int32 NumSectionsWithCollision = GetNumSectionsWithCollision();

	int32 NumCollisionPrims = 0;
	if ( BodySetup != NULL )
	{
		NumCollisionPrims = BodySetup->AggGeom.GetElementCount();
	}

	FBoxSphereBounds Bounds(ForceInit);
	if (RenderData)
	{
		Bounds = RenderData->Bounds;
	}
	const FString ApproxSizeStr = FString::Printf(TEXT("%dx%dx%d"), FMath::RoundToInt(Bounds.BoxExtent.X * 2.0f), FMath::RoundToInt(Bounds.BoxExtent.Y * 2.0f), FMath::RoundToInt(Bounds.BoxExtent.Z * 2.0f));

	// Get name of default collision profile
	FName DefaultCollisionName = NAME_None;
	if(BodySetup != nullptr)
	{
		DefaultCollisionName = BodySetup->DefaultInstance.GetCollisionProfileName();
	}

	FString ComplexityString;
	if (BodySetup != nullptr)
	{
		ComplexityString = LexToString((ECollisionTraceFlag)BodySetup->GetCollisionTraceFlag());
	}

	OutTags.Add( FAssetRegistryTag("Triangles", FString::FromInt(NumTriangles), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("Vertices", FString::FromInt(NumVertices), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("UVChannels", FString::FromInt(NumUVChannels), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("Materials", FString::FromInt(StaticMaterials.Num()), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("ApproxSize", ApproxSizeStr, FAssetRegistryTag::TT_Dimensional) );
	OutTags.Add( FAssetRegistryTag("CollisionPrims", FString::FromInt(NumCollisionPrims), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("LODs", FString::FromInt(NumLODs), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("SectionsWithCollision", FString::FromInt(NumSectionsWithCollision), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("DefaultCollision", DefaultCollisionName.ToString(), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add( FAssetRegistryTag("CollisionComplexity", ComplexityString, FAssetRegistryTag::TT_Alphabetical));

#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}
#endif

	Super::GetAssetRegistryTags(OutTags);
}

#if WITH_EDITOR
void UStaticMesh::GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const
{
	Super::GetAssetRegistryTagMetadata(OutMetadata);

	OutMetadata.Add("CollisionPrims",
		FAssetRegistryTagMetadata()
			.SetTooltip(NSLOCTEXT("UStaticMesh", "CollisionPrimsTooltip", "The number of collision primitives in the static mesh"))
			.SetImportantValue(TEXT("0"))
		);
}
#endif


/*------------------------------------------------------------------------------
	FStaticMeshSourceModel
------------------------------------------------------------------------------*/

FStaticMeshSourceModel::FStaticMeshSourceModel()
{
	LODDistance_DEPRECATED = 0.0f;
#if WITH_EDITOR
	RawMeshBulkData = new FRawMeshBulkData();
	ScreenSize.Default = 0.0f;
	OriginalMeshDescription = nullptr;
	StaticMeshOwner = nullptr;
#endif // #if WITH_EDITOR
	SourceImportFilename = FString();
#if WITH_EDITORONLY_DATA
	bImportWithBaseMesh = false;
#endif
}

FStaticMeshSourceModel::~FStaticMeshSourceModel()
{
#if WITH_EDITOR
	if (RawMeshBulkData)
	{
		delete RawMeshBulkData;
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR
bool FStaticMeshSourceModel::IsRawMeshEmpty() const
{
	return (RawMeshBulkData == nullptr || (RawMeshBulkData->IsEmpty() && OriginalMeshDescription == nullptr));
}

void FStaticMeshSourceModel::LoadRawMesh(FRawMesh& OutRawMesh) const
{
	if (RawMeshBulkData->IsEmpty() && OriginalMeshDescription != nullptr)
	{
		TMap<FName, int32> MaterialMap;
		check(StaticMeshOwner != nullptr);
		for (int32 MaterialIndex = 0; MaterialIndex < StaticMeshOwner->StaticMaterials.Num(); ++MaterialIndex)
		{
			MaterialMap.Add(StaticMeshOwner->StaticMaterials[MaterialIndex].ImportedMaterialSlotName, MaterialIndex);
		}
		FMeshDescriptionOperations::ConvertToRawMesh(*OriginalMeshDescription, OutRawMesh, MaterialMap);
	}
	else
	{
		RawMeshBulkData->LoadRawMesh(OutRawMesh);
	}
}

void FStaticMeshSourceModel::SaveRawMesh(FRawMesh& InRawMesh, bool bConvertToMeshdescription /*= true*/)
{
	if (!InRawMesh.IsValid())
	{
		return;
	}
	//Save both format
	RawMeshBulkData->SaveRawMesh(InRawMesh);
	if (bConvertToMeshdescription && OriginalMeshDescription != nullptr)
	{
		TMap<int32, FName> MaterialMap;
		check(StaticMeshOwner != nullptr);
		FillMaterialName(StaticMeshOwner->StaticMaterials, MaterialMap);
		FMeshDescriptionOperations::ConvertFromRawMesh(InRawMesh, *OriginalMeshDescription, MaterialMap);
	}
}

void FStaticMeshSourceModel::SerializeBulkData(FArchive& Ar, UObject* Owner)
{
	check(RawMeshBulkData != NULL);
	RawMeshBulkData->Serialize(Ar, Owner);
}
#endif // #if WITH_EDITOR

/*------------------------------------------------------------------------------
	FMeshSectionInfoMap
------------------------------------------------------------------------------*/

#if WITH_EDITORONLY_DATA
	
bool operator==(const FMeshSectionInfo& A, const FMeshSectionInfo& B)
{
	return A.MaterialIndex == B.MaterialIndex
		&& A.bCastShadow == B.bCastShadow
		&& A.bEnableCollision == B.bEnableCollision;
}

bool operator!=(const FMeshSectionInfo& A, const FMeshSectionInfo& B)
{
	return !(A == B);
}
	
static uint32 GetMeshMaterialKey(int32 LODIndex, int32 SectionIndex)
{
	return ((LODIndex & 0xffff) << 16) | (SectionIndex & 0xffff);
}

void FMeshSectionInfoMap::Clear()
{
	Map.Empty();
}

int32 FMeshSectionInfoMap::GetSectionNumber(int32 LODIndex) const
{
	int32 SectionCount = 0;
	for (auto kvp : Map)
	{
		if (((kvp.Key & 0xffff0000) >> 16) == LODIndex)
		{
			SectionCount++;
		}
	}
	return SectionCount;
}

bool FMeshSectionInfoMap::IsValidSection(int32 LODIndex, int32 SectionIndex) const
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	return (Map.Find(Key) != nullptr);
}

FMeshSectionInfo FMeshSectionInfoMap::Get(int32 LODIndex, int32 SectionIndex) const
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	const FMeshSectionInfo* InfoPtr = Map.Find(Key);
	if (InfoPtr == NULL)
	{
		Key = GetMeshMaterialKey(0, SectionIndex);
		InfoPtr = Map.Find(Key);
	}
	if (InfoPtr != NULL)
	{
		return *InfoPtr;
	}
	return FMeshSectionInfo(SectionIndex);
}

void FMeshSectionInfoMap::Set(int32 LODIndex, int32 SectionIndex, FMeshSectionInfo Info)
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	Map.Add(Key, Info);
}

void FMeshSectionInfoMap::Remove(int32 LODIndex, int32 SectionIndex)
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	Map.Remove(Key);
}

void FMeshSectionInfoMap::CopyFrom(const FMeshSectionInfoMap& Other)
{
	for (TMap<uint32,FMeshSectionInfo>::TConstIterator It(Other.Map); It; ++It)
	{
		Map.Add(It.Key(), It.Value());
	}
}

bool FMeshSectionInfoMap::AnySectionHasCollision(int32 LodIndex) const
{
	for (TMap<uint32,FMeshSectionInfo>::TConstIterator It(Map); It; ++It)
	{
		uint32 Key = It.Key();
		int32 KeyLODIndex = (int32)(Key >> 16);
		if (KeyLODIndex == LodIndex && It.Value().bEnableCollision)
		{
			return true;
		}
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FMeshSectionInfo& Info)
{
	Ar << Info.MaterialIndex;
	Ar << Info.bEnableCollision;
	Ar << Info.bCastShadow;
	return Ar;
}

void FMeshSectionInfoMap::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if ( Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::UPropertryForMeshSectionSerialize // Release-4.15 change
		&& Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSectionSerialize) // Dev-Editor change
	{
		Ar << Map;
	}
}

#endif // #if WITH_EDITORONLY_DATA

/**
 * Registers the mesh attributes required by the mesh description for a static mesh.
 */
void UStaticMesh::RegisterMeshAttributes( FMeshDescription& MeshDescription )
{
	// Add basic vertex attributes
	MeshDescription.VertexAttributes().RegisterAttribute<FVector>( MeshAttribute::Vertex::Position, 1, FVector::ZeroVector, EMeshAttributeFlags::Lerpable );
	MeshDescription.VertexAttributes().RegisterAttribute<float>( MeshAttribute::Vertex::CornerSharpness, 1, 0.0f, EMeshAttributeFlags::Lerpable );

	// Add basic vertex instance attributes
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector2D>( MeshAttribute::VertexInstance::TextureCoordinate, 1, FVector2D::ZeroVector, EMeshAttributeFlags::Lerpable );
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector>( MeshAttribute::VertexInstance::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector>( MeshAttribute::VertexInstance::Tangent, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<float>( MeshAttribute::VertexInstance::BinormalSign, 1, 0.0f, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector4>( MeshAttribute::VertexInstance::Color, 1, FVector4( 1.0f ), EMeshAttributeFlags::Lerpable );

	// Add basic edge attributes
	MeshDescription.EdgeAttributes().RegisterAttribute<bool>( MeshAttribute::Edge::IsHard, 1, false );
	MeshDescription.EdgeAttributes().RegisterAttribute<bool>( MeshAttribute::Edge::IsUVSeam, 1, false );
	MeshDescription.EdgeAttributes().RegisterAttribute<float>( MeshAttribute::Edge::CreaseSharpness, 1, 0.0f, EMeshAttributeFlags::Lerpable );

	// Add basic polygon attributes
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Tangent, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Binormal, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>( MeshAttribute::Polygon::Center, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated );

	// Add basic polygon group attributes
	MeshDescription.PolygonGroupAttributes().RegisterAttribute<FName>( MeshAttribute::PolygonGroup::ImportedMaterialSlotName ); //The unique key to match the mesh material slot
	MeshDescription.PolygonGroupAttributes().RegisterAttribute<bool>( MeshAttribute::PolygonGroup::EnableCollision ); //Deprecated
	MeshDescription.PolygonGroupAttributes().RegisterAttribute<bool>( MeshAttribute::PolygonGroup::CastShadow ); //Deprecated
}


UStaticMeshDescriptions::UStaticMeshDescriptions(const FObjectInitializer& ObjectInitializer)	:
	Super(ObjectInitializer)
{
}

UStaticMeshDescriptions::UStaticMeshDescriptions(FVTableHelper& Helper) :
	Super(Helper)
{
}

UStaticMeshDescriptions::~UStaticMeshDescriptions() = default;

void UStaticMeshDescriptions::Serialize(FArchive& Ar)
{
	int32 MeshDescriptionCount = MeshDescriptions.Num();
	Ar << MeshDescriptionCount;

	if (Ar.IsLoading())
	{
		MeshDescriptions.Reset(MeshDescriptionCount);
		MeshDescriptions.SetNum(MeshDescriptionCount);
	}

	for (int32 Index = 0; Index < MeshDescriptionCount; ++Index)
	{
		bool bIsValid = MeshDescriptions[Index].IsValid();
		Ar << bIsValid;

		if (bIsValid)
		{
			if (Ar.IsLoading())
			{
				MeshDescriptions[Index] = MakeUnique<FMeshDescription>();
			}

			Ar << (*MeshDescriptions[Index]);
		}
	}
}

void UStaticMeshDescriptions::Empty()
{
	MeshDescriptions.Reset();
}

int32 UStaticMeshDescriptions::Num() const
{
	return MeshDescriptions.Num();
}

void UStaticMeshDescriptions::SetNum(const int32 Num)
{
	MeshDescriptions.SetNum(Num);
}

FMeshDescription* UStaticMeshDescriptions::Get(int32 Index) const
{
	return MeshDescriptions[Index].Get();
}

FMeshDescription* UStaticMeshDescriptions::Create(int32 Index)
{
	return (MeshDescriptions[Index] = MakeUnique<FMeshDescription>()).Get();
}

void UStaticMeshDescriptions::Reset(int32 Index)
{
	MeshDescriptions[Index].Reset();
}

void UStaticMeshDescriptions::InsertAt(int32 Index, int32 Count)
{
	MeshDescriptions.InsertDefaulted(Index, Count);
}

void UStaticMeshDescriptions::RemoveAt(int32 Index, int32 Count)
{
	MeshDescriptions.RemoveAt(Index, Count);
}


#if WITH_EDITOR
static FStaticMeshRenderData& GetPlatformStaticMeshRenderData(UStaticMesh* Mesh, const ITargetPlatform* Platform)
{
	check(Mesh && Mesh->RenderData);
	const FStaticMeshLODSettings& PlatformLODSettings = Platform->GetStaticMeshLODSettings();
	FString PlatformDerivedDataKey = BuildStaticMeshDerivedDataKey(Mesh, PlatformLODSettings.GetLODGroup(Mesh->LODGroup));
	FStaticMeshRenderData* PlatformRenderData = Mesh->RenderData.Get();

	if (Mesh->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		check(PlatformRenderData);
		return *PlatformRenderData;
	}

	while (PlatformRenderData && PlatformRenderData->DerivedDataKey != PlatformDerivedDataKey)
	{
		PlatformRenderData = PlatformRenderData->NextCachedRenderData.Get();
	}
	if (PlatformRenderData == NULL)
	{
		// Cache render data for this platform and insert it in to the linked list.
		PlatformRenderData = new FStaticMeshRenderData();
		PlatformRenderData->Cache(Mesh, PlatformLODSettings);
		check(PlatformRenderData->DerivedDataKey == PlatformDerivedDataKey);
		Swap(PlatformRenderData->NextCachedRenderData, Mesh->RenderData->NextCachedRenderData);
		Mesh->RenderData->NextCachedRenderData = TUniquePtr<FStaticMeshRenderData>(PlatformRenderData);
	}
	check(PlatformRenderData);
	return *PlatformRenderData;
}


#if WITH_EDITORONLY_DATA

void UStaticMesh::LoadMeshDescriptions()
{
	if (MeshDescriptions)
	{
		//Sync the already loaded MeshDescription
		MeshDescriptions->SetNum(SourceModels.Num());
		for (int32 LodIndex = 0; LodIndex < MeshDescriptions->Num(); ++LodIndex)
		{
			//Get the missing MeshDescription to create them from the FRawMesh
			if (MeshDescriptions->Get(LodIndex) == nullptr && !SourceModels[LodIndex].IsRawMeshEmpty())
			{
				// If the MeshDescriptions are out of sync with the SourceModels RawMesh, perform a conversion here.
				// @todo: once all tools are ported, we can replace this with a check() instead.
				FMeshDescription* MeshDescription = MeshDescriptions->Create(LodIndex);
				SourceModels[LodIndex].OriginalMeshDescription = MeshDescription;
				RegisterMeshAttributes(*MeshDescription);

				FRawMesh LodRawMesh;
				SourceModels[LodIndex].LoadRawMesh(LodRawMesh);
				TMap<int32, FName> MaterialMap;
				FillMaterialName(StaticMaterials, MaterialMap);
				FMeshDescriptionOperations::ConvertFromRawMesh(LodRawMesh, *MeshDescription, MaterialMap);
			}
		}
	}
	else
	{
		MeshDescriptions = NewObject<UStaticMeshDescriptions>(GetTransientPackage());

		// For the moment, this comes from the DDC. Eventually it will load the UObject from the same package as the static mesh
		// from a soft object path.
		FString MeshDataKey;
		if (GetMeshDataKey(MeshDataKey))
		{
			TArray<uint8> DerivedData;
			if (GetDerivedDataCacheRef().GetSynchronous(*MeshDataKey, DerivedData))
			{
				// Load from the DDC
				const bool bIsPersistent = true;
				FMemoryReader Ar(DerivedData, bIsPersistent);
				MeshDescriptions->Serialize(Ar);
			}
			else
			{
				// Nothing cached in the DDC; create a blank one
				MeshDescriptions->SetNum(SourceModels.Num());
			}
		}
		else
		{
			// If we get here, it's because there are no SourceModels.
			// At this point we just have an empty UStaticMeshDescriptions object.
		}
		// Assign the pointer in the individual FStaticMeshSourceModels
		check(MeshDescriptions->Num() == SourceModels.Num());
		for (int32 Index = 0; Index < SourceModels.Num(); ++Index)
		{
			SourceModels[Index].OriginalMeshDescription = MeshDescriptions->Get(Index);
		}
	}
}


void UStaticMesh::UnloadMeshDescriptions()
{
	// @note: Do we really need this method? In theory, once loaded, this UObject should stick around until its package is unloaded

	// Do nothing if already unloaded
	if (!MeshDescriptions)
	{
		return;
	}

	check(MeshDescriptions->Num() == SourceModels.Num());
	for (int32 Index = 0; Index < SourceModels.Num(); ++Index)
	{
		SourceModels[Index].OriginalMeshDescription = nullptr;
	}

	MeshDescriptions->Empty();
	MeshDescriptions->MarkPendingKill();
	MeshDescriptions = nullptr;
}

FMeshDescription* UStaticMesh::GetOriginalMeshDescription(int32 LodIndex)
{
	LoadMeshDescriptions();

	if (SourceModels.IsValidIndex(LodIndex))
	{
		check(MeshDescriptions->Num() == SourceModels.Num());
		check(MeshDescriptions->Get(LodIndex) == SourceModels[LodIndex].OriginalMeshDescription);

#if 0
		// There should be no way for this to yield a null pointer if the RawMesh is non-empty, as LoadMeshDescriptions should already
		// have converted the RawMesh to a valid MeshDescription.
		// @note: Doesn't seem to the be the case yet; Alembic still imports directly to RawMesh.
		check(MeshDescriptions->Get(LodIndex) || SourceModels[LodIndex].IsRawMeshEmpty());
#else
		if (MeshDescriptions->Get(LodIndex) == nullptr && !SourceModels[LodIndex].IsRawMeshEmpty())
		{
			// If the MeshDescriptions are out of sync with the SourceModels RawMesh, perform a conversion here.
			// @todo: once all tools are ported, we can replace this with the disabled check() above.
			FMeshDescription* MeshDescription = MeshDescriptions->Create(LodIndex);
			SourceModels[LodIndex].OriginalMeshDescription = MeshDescription;
			RegisterMeshAttributes(*MeshDescription);

			FRawMesh LodRawMesh;
			SourceModels[LodIndex].LoadRawMesh(LodRawMesh);
			TMap<int32, FName> MaterialMap;
			FillMaterialName(StaticMaterials, MaterialMap);
			FMeshDescriptionOperations::ConvertFromRawMesh(LodRawMesh, *MeshDescription, MaterialMap);
		}
#endif

		return MeshDescriptions->Get(LodIndex);
	}

	return nullptr;

}

FMeshDescription* UStaticMesh::CreateOriginalMeshDescription(int32 LodIndex)
{
	LoadMeshDescriptions();

	if (SourceModels.IsValidIndex(LodIndex))
	{
		// @todo: mark package dirty once MeshDescriptions is packaged with the static mesh

		check(MeshDescriptions->Num() == SourceModels.Num());
		FMeshDescription* MeshDescription = MeshDescriptions->Create(LodIndex);
		SourceModels[LodIndex].OriginalMeshDescription = MeshDescription;
		return MeshDescription;
	}

	return nullptr;
}

void UStaticMesh::CommitOriginalMeshDescription(int32 LodIndex)
{
	// The source model must be created before calling this function
	check(SourceModels.IsValidIndex(LodIndex));
	check(MeshDescriptions->Num() == SourceModels.Num());
	check(MeshDescriptions->Get(LodIndex) == SourceModels[LodIndex].OriginalMeshDescription);

	const FMeshDescription* MeshDescription = MeshDescriptions->Get(LodIndex);
	if (MeshDescription != nullptr)
	{
		// Convert MeshDescription to RawMesh
		FRawMesh TempRawMesh;
		TMap<FName, int32> MaterialMap;
		for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
		{
			MaterialMap.Add(StaticMaterials[MaterialIndex].ImportedMaterialSlotName, MaterialIndex);
		}
		FMeshDescriptionOperations::ConvertToRawMesh(*MeshDescription, TempRawMesh, MaterialMap);
		SourceModels[LodIndex].RawMeshBulkData->SaveRawMesh(TempRawMesh);
	}
	else
	{
		// Mesh description is null, remove the rawmesh data
		SourceModels[LodIndex].RawMeshBulkData->Empty();
	}

	// @todo: mark package dirty once MeshDescriptions is packaged with the static mesh
}

void UStaticMesh::ClearOriginalMeshDescription(int32 LodIndex)
{
	LoadMeshDescriptions();

	if (SourceModels.IsValidIndex(LodIndex))
	{
		// @todo: mark package dirty once MeshDescriptions is packaged with the static mesh

		check(MeshDescriptions->Num() == SourceModels.Num());
		MeshDescriptions->Reset(LodIndex);
		SourceModels[LodIndex].OriginalMeshDescription = nullptr;
	}
}

void UStaticMesh::FixupMaterialSlotName()
{
	TArray<FName> UniqueMaterialSlotName;
	//Make sure we have non empty imported material slot names
	for (FStaticMaterial& Material : StaticMaterials)
	{
		if (Material.ImportedMaterialSlotName == NAME_None)
		{
			if (Material.MaterialSlotName != NAME_None)
			{
				Material.ImportedMaterialSlotName = Material.MaterialSlotName;
			}
			else if (Material.MaterialInterface != nullptr)
			{
				Material.ImportedMaterialSlotName = Material.MaterialInterface->GetFName();
			}
			else
			{
				Material.ImportedMaterialSlotName = FName(TEXT("MaterialSlot"));
			}
		}

		FString UniqueName = Material.ImportedMaterialSlotName.ToString();
		int32 UniqueIndex = 1;
		while (UniqueMaterialSlotName.Contains(FName(*UniqueName)))
		{
			UniqueName = FString::Printf(TEXT("%s_%d"), *UniqueName, UniqueIndex);
			UniqueIndex++;
		}
		Material.ImportedMaterialSlotName = FName(*UniqueName);
		UniqueMaterialSlotName.Add(Material.ImportedMaterialSlotName);
		if (Material.MaterialSlotName == NAME_None)
		{
			Material.MaterialSlotName = Material.ImportedMaterialSlotName;
		}
	}
}

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.                                       
#define MESHDATAKEY_STATICMESH_DERIVEDDATA_VER TEXT("A3E9E442F5784050BCAF878E4E80EE44")

static const FString& GetMeshDataKeyStaticMeshDerivedDataVersion()
{
	static FString CachedVersionString;
	if (CachedVersionString.IsEmpty())
	{
		// Static mesh versioning is controlled by the version reported by the mesh utilities module.
		CachedVersionString = FString::Printf(TEXT("%s%s"),
			*FMeshDescription::GetMeshDescriptionVersion(),
			MESHDATAKEY_STATICMESH_DERIVEDDATA_VER);
	}
	return CachedVersionString;
}

bool UStaticMesh::GetMeshDataKey(FString& OutKey)
{
	OutKey.Empty();
	if (SourceModels.Num() < 1)
	{
		return false;
	}
	FSHA1 Sha;
	for (int32 LodIndex = 0; LodIndex < SourceModels.Num(); ++LodIndex)
	{
		FString LodIndexString = FString::Printf(TEXT("%d_"), LodIndex);
		FStaticMeshSourceModel& SourceModel = SourceModels[LodIndex];
		if(!SourceModel.RawMeshBulkData->IsEmpty())
		{
			LodIndexString += SourceModel.RawMeshBulkData->GetIdString();
		}
		else
		{
			LodIndexString += TEXT("REDUCELOD");
		}
		const TArray<TCHAR>& LodIndexArray = LodIndexString.GetCharArray();
		Sha.Update((uint8*)LodIndexArray.GetData(), LodIndexArray.Num() * LodIndexArray.GetTypeSize());
	}
	Sha.Final();
	// Retrieve the hash and use it to construct a pseudo-GUID.
	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	FString MeshLodData = Guid.ToString(EGuidFormats::Digits);

	OutKey = FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("MESHDATAKEY_STATICMESH"),
		*GetMeshDataKeyStaticMeshDerivedDataVersion(),
		*MeshLodData
	);
	return true;
}


void UStaticMesh::CacheMeshData()
{
	FString MeshDataKey;
	if (GetMeshDataKey(MeshDataKey))
	{
		// If the DDC key doesn't exist, convert the data and save it to DDC
		if (!GetDerivedDataCacheRef().CachedDataProbablyExists(*MeshDataKey))
		{
			UStaticMeshDescriptions* StaticMeshDescriptions = NewObject<UStaticMeshDescriptions>(GetTransientPackage());
			StaticMeshDescriptions->SetNum(SourceModels.Num());

			for (int32 LodIndex = 0; LodIndex < SourceModels.Num(); ++LodIndex)
			{
				FStaticMeshSourceModel& SourceModel = SourceModels[LodIndex];
				if (!SourceModel.RawMeshBulkData->IsEmpty())
				{
					// Get the RawMesh for this LOD
					FRawMesh TempRawMesh;
					SourceModel.RawMeshBulkData->LoadRawMesh(TempRawMesh);

					// Create a new MeshDescription
					FMeshDescription* MeshDescription = StaticMeshDescriptions->Create(LodIndex);
					RegisterMeshAttributes(*MeshDescription);

					// Convert the RawMesh to MeshDescription
					TMap<int32, FName> MaterialMap;
					FillMaterialName(StaticMaterials, MaterialMap);
					FMeshDescriptionOperations::ConvertFromRawMesh(TempRawMesh, *MeshDescription, MaterialMap);
				}
			}

			// Write the DDC cache
			TArray<uint8> DerivedData;
			const bool bIsPersistent = true;
			FMemoryWriter Ar(DerivedData, bIsPersistent);

			StaticMeshDescriptions->Serialize(Ar);
			GetDerivedDataCacheRef().Put(*MeshDataKey, DerivedData);

			// Kill the StaticMeshDescriptions object; if it's required in the future, it'll be loaded on demand.
			StaticMeshDescriptions->MarkPendingKill();
		}
	}
}

#endif

void UStaticMesh::CacheDerivedData()
{
#if WITH_EDITORONLY_DATA
	CacheMeshData();
#endif
	// Cache derived data for the running platform.
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);
	const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

	if (RenderData)
	{
		// Finish any previous async builds before modifying RenderData
		// This can happen during import as the mesh is rebuilt redundantly
		GDistanceFieldAsyncQueue->BlockUntilBuildComplete(this, true);

		for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
		{
			FDistanceFieldVolumeData* DistanceFieldData = RenderData->LODResources[LODIndex].DistanceFieldData;

			if (DistanceFieldData)
			{
				// Release before destroying RenderData
				DistanceFieldData->VolumeTexture.Release();
			}
		}
	}

	RenderData = MakeUnique<FStaticMeshRenderData>();
	RenderData->Cache(this, LODSettings);

	// Conditionally create occluder data
	OccluderData = FStaticMeshOccluderData::Build(this);

	// Additionally cache derived data for any other platforms we care about.
	const TArray<ITargetPlatform*>& TargetPlatforms = TargetPlatformManager.GetActiveTargetPlatforms();
	for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
	{
		ITargetPlatform* Platform = TargetPlatforms[PlatformIndex];
		if (Platform != RunningPlatform)
		{
			GetPlatformStaticMeshRenderData(this, Platform);
		}
	}
}

#endif // #if WITH_EDITORONLY_DATA

void UStaticMesh::CalculateExtendedBounds()
{
	FBoxSphereBounds Bounds(ForceInit);
	if (RenderData)
	{
		Bounds = RenderData->Bounds;
	}

	// Only apply bound extension if necessary, as it will result in a larger bounding sphere radius than retrieved from the render data
	if (!NegativeBoundsExtension.IsZero() || !PositiveBoundsExtension.IsZero())
	{
		// Convert to Min and Max
		FVector Min = Bounds.Origin - Bounds.BoxExtent;
		FVector Max = Bounds.Origin + Bounds.BoxExtent;
		// Apply bound extensions
		Min -= NegativeBoundsExtension;
		Max += PositiveBoundsExtension;
		// Convert back to Origin, Extent and update SphereRadius
		Bounds.Origin = (Min + Max) / 2;
		Bounds.BoxExtent = (Max - Min) / 2;
		Bounds.SphereRadius = Bounds.BoxExtent.Size();
	}

	ExtendedBounds = Bounds;

#if WITH_EDITOR
	OnExtendedBoundsChanged.Broadcast(Bounds);
#endif
}


#if WITH_EDITORONLY_DATA
FUObjectAnnotationSparseBool GStaticMeshesThatNeedMaterialFixup;
#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITOR
COREUOBJECT_API extern bool GOutputCookingWarnings;
#endif


/**
 *	UStaticMesh::Serialize
 */
void UStaticMesh::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::StaticMesh);

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("UStaticMesh::Serialize"), STAT_StaticMesh_Serialize, STATGROUP_LoadTime );

	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	FStripDataFlags StripFlags( Ar );

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_REMOVE_ZERO_TRIANGLE_SECTIONS)
	{
		GStaticMeshesThatNeedMaterialFixup.Set(this);
	}
#endif // #if WITH_EDITORONLY_DATA

	Ar << BodySetup;

	if (Ar.UE4Ver() >= VER_UE4_STATIC_MESH_STORE_NAV_COLLISION)
	{
		Ar << NavCollision;
#if WITH_EDITOR
		if ((BodySetup != nullptr) && 
			bHasNavigationData && 
			(NavCollision == nullptr))
		{
			if (Ar.IsPersistent() && Ar.IsLoading() && (Ar.GetDebugSerializationFlags() & DSF_EnableCookerWarnings))
			{
				UE_LOG(LogStaticMesh, Warning, TEXT("Serialized NavCollision but it was null (%s) NavCollision will be created dynamicaly at cook time.  Please resave package %s."), *GetName(), *GetOutermost()->GetPathName());
			}
		}
#endif
	}
#if WITH_EDITOR
	else if (bHasNavigationData && BodySetup && (Ar.GetDebugSerializationFlags() & DSF_EnableCookerWarnings))
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("This StaticMeshes (%s) NavCollision will be created dynamicaly at cook time.  Please resave %s."), *GetName(), *GetOutermost()->GetPathName())
	}
#endif

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if(Ar.IsLoading() && Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::UseBodySetupCollisionProfile && BodySetup)
	{
		BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}

#if WITH_EDITORONLY_DATA
	if( !StripFlags.IsEditorDataStripped() )
	{
		if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_DEPRECATED_STATIC_MESH_THUMBNAIL_PROPERTIES_REMOVED )
		{
			FRotator DummyThumbnailAngle;
			float DummyThumbnailDistance;
			Ar << DummyThumbnailAngle;
			Ar << DummyThumbnailDistance;
		}
	}

	if( !StripFlags.IsEditorDataStripped() )
	{
		Ar << HighResSourceMeshName;
		Ar << HighResSourceMeshCRC;
	}
#endif // #if WITH_EDITORONLY_DATA

	if( Ar.IsCountingMemory() )
	{
		// Include collision as part of memory used
		if ( BodySetup )
		{
			BodySetup->Serialize( Ar );
		}

		if ( NavCollision )
		{
			NavCollision->Serialize( Ar );
		}

		//TODO: Count these members when calculating memory used
		//Ar << ReleaseResourcesFence;
	}

	Ar << LightingGuid;
	Ar << Sockets;

#if WITH_EDITOR
	if (!StripFlags.IsEditorDataStripped())
	{
		for (int32 i = 0; i < SourceModels.Num(); ++i)
		{
			FStaticMeshSourceModel& SrcModel = SourceModels[i];
			SrcModel.SerializeBulkData(Ar, this);
		}

		if (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSection)
		{
			SectionInfoMap.Serialize(Ar);
		}

		// Need to set a flag rather than do conversion in place as RenderData is not
		// created until postload and it is needed for bounding information
		bRequiresLODDistanceConversion = Ar.UE4Ver() < VER_UE4_STATIC_MESH_SCREEN_SIZE_LODS;
		bRequiresLODScreenSizeConversion = Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::LODsUseResolutionIndependentScreenSize;
	}
#endif // #if WITH_EDITOR

	// Inline the derived data for cooked builds. Never include render data when
	// counting memory as it is included by GetResourceSize.
	if (bCooked && !IsTemplate() && !Ar.IsCountingMemory())
	{	
		if (Ar.IsLoading())
		{
			RenderData = MakeUnique<FStaticMeshRenderData>();
			RenderData->Serialize(Ar, this, bCooked);
			
			FStaticMeshOccluderData::SerializeCooked(Ar, this);
		}

#if WITH_EDITOR
		else if (Ar.IsSaving())
		{
			FStaticMeshRenderData& PlatformRenderData = GetPlatformStaticMeshRenderData(this, Ar.CookingTarget());
			PlatformRenderData.Serialize(Ar, this, bCooked);
			
			FStaticMeshOccluderData::SerializeCooked(Ar, this);
		}
#endif
	}

	if (Ar.UE4Ver() >= VER_UE4_SPEEDTREE_STATICMESH)
	{
		bool bHasSpeedTreeWind = SpeedTreeWind.IsValid();
		Ar << bHasSpeedTreeWind;

		if (bHasSpeedTreeWind)
		{
			if (!SpeedTreeWind.IsValid())
			{
				SpeedTreeWind = TSharedPtr<FSpeedTreeWind>(new FSpeedTreeWind);
			}

			Ar << *SpeedTreeWind;
		}
	}

#if WITH_EDITORONLY_DATA
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	
	// SourceFilePath and SourceFileTimestamp were moved into a subobject
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ADDED_FBX_ASSET_IMPORT_DATA && AssetImportData )
	{
		// AssetImportData should always have been set up in the constructor where this is relevant
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
		
		SourceFilePath_DEPRECATED = TEXT("");
		SourceFileTimestamp_DEPRECATED = TEXT("");
	}

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::DistanceFieldSelfShadowBias)
	{
		DistanceFieldSelfShadowBias = SourceModels[0].BuildSettings.DistanceFieldBias_DEPRECATED * 10.0f;
	}
#endif // WITH_EDITORONLY_DATA

	if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		Ar << StaticMaterials;
	}
	else if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> Unique_Materials_DEPRECATED;
		TArray<FName> MaterialSlotNames;
		for (UMaterialInterface *MaterialInterface : Materials_DEPRECATED)
		{
			FName MaterialSlotName = MaterialInterface != nullptr ? MaterialInterface->GetFName() : NAME_None;
			int32 NameCounter = 1;
			if (MaterialInterface)
			{
				while (MaterialSlotName != NAME_None && MaterialSlotNames.Find(MaterialSlotName) != INDEX_NONE)
				{
					FString MaterialSlotNameStr = MaterialInterface->GetName() + TEXT("_") + FString::FromInt(NameCounter);
					MaterialSlotName = FName(*MaterialSlotNameStr);
					NameCounter++;
				}
			}
			MaterialSlotNames.Add(MaterialSlotName);
			StaticMaterials.Add(FStaticMaterial(MaterialInterface, MaterialSlotName));
			int32 UniqueIndex = Unique_Materials_DEPRECATED.AddUnique(MaterialInterface);
#if WITH_EDITOR
			//We must cleanup the material list since we have a new way to build static mesh
			CleanUpRedondantMaterialPostLoad = StaticMaterials.Num() > 1;
#endif
		}
		Materials_DEPRECATED.Empty();

	}


#if WITH_EDITOR
	bool bHasSpeedTreeWind = SpeedTreeWind.IsValid();
	if (Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::SpeedTreeBillboardSectionInfoFixup && bHasSpeedTreeWind)
	{
		// Ensure we have multiple tree LODs
		if (SourceModels.Num() > 1)
		{
			// Look a the last LOD model and check its vertices
			const int32 LODIndex = SourceModels.Num() - 1;
			FStaticMeshSourceModel& SourceModel = SourceModels[LODIndex];

			FRawMesh RawMesh;
			SourceModel.LoadRawMesh(RawMesh);

			// Billboard LOD is made up out of quads so check for this
			bool bQuadVertices = ((RawMesh.VertexPositions.Num() % 4) == 0);

			// If there is no section info for the billboard LOD make sure we add it
			uint32 Key = GetMeshMaterialKey(LODIndex, 0);
			bool bSectionInfoExists = SectionInfoMap.Map.Contains(Key);
			if (!bSectionInfoExists && bQuadVertices)
			{
				FMeshSectionInfo Info;
				// Assuming billboard material is added last
				Info.MaterialIndex = StaticMaterials.Num() - 1;
				SectionInfoMap.Set(LODIndex, 0, Info);
				OriginalSectionInfoMap.Set(LODIndex, 0, Info);
			}
		}
	}
#endif // WITH_EDITOR
}

bool UStaticMesh::IsPostLoadThreadSafe() const
{
	return false;
}

//
//	UStaticMesh::PostLoad
//
void UStaticMesh::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR

	if (SourceModels.Num() > 0)
	{
		UStaticMesh* DistanceFieldReplacementMesh = SourceModels[0].BuildSettings.DistanceFieldReplacementMesh;
 
		if (DistanceFieldReplacementMesh)
		{
			DistanceFieldReplacementMesh->ConditionalPostLoad();
		}
		
		//TODO remove this code when FRawMesh will be removed
		//Fill the static mesh owner
		int32 NumLODs = SourceModels.Num();
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			FStaticMeshSourceModel& SrcModel = SourceModels[LODIndex];
			SrcModel.StaticMeshOwner = this;
		}
	}

	if (!GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		// Needs to happen before 'CacheDerivedData'
		if (GetLinkerUE4Version() < VER_UE4_BUILD_SCALE_VECTOR)
		{
			int32 NumLODs = SourceModels.Num();
			for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
			{
				FStaticMeshSourceModel& SrcModel = SourceModels[LODIndex];
				SrcModel.BuildSettings.BuildScale3D = FVector(SrcModel.BuildSettings.BuildScale_DEPRECATED);
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_LIGHTMAP_MESH_BUILD_SETTINGS)
		{
			for (int32 i = 0; i < SourceModels.Num(); i++)
			{
				SourceModels[i].BuildSettings.bGenerateLightmapUVs = false;
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_MIKKTSPACE_IS_DEFAULT)
		{
			for (int32 i = 0; i < SourceModels.Num(); ++i)
			{
				SourceModels[i].BuildSettings.bUseMikkTSpace = true;
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_BUILD_MESH_ADJ_BUFFER_FLAG_EXPOSED)
		{
			FRawMesh TempRawMesh;
			uint32 TotalIndexCount = 0;

			for (int32 i = 0; i < SourceModels.Num(); ++i)
			{
				if (!SourceModels[i].IsRawMeshEmpty())
				{
					SourceModels[i].LoadRawMesh(TempRawMesh);
					TotalIndexCount += TempRawMesh.WedgeIndices.Num();
				}
			}

			for (int32 i = 0; i < SourceModels.Num(); ++i)
			{
				SourceModels[i].BuildSettings.bBuildAdjacencyBuffer = (TotalIndexCount < 50000);
			}
		}

		// The LODGroup update on load must happen before CacheDerivedData so we don't have to rebuild it after
		if (GUpdateMeshLODGroupSettingsAtLoad && LODGroup != NAME_None)
		{
			SetLODGroup(LODGroup);
		}

		FixupMaterialSlotName();

		CacheDerivedData();

		//Fix up the material to remove redundant material, this is needed since the material refactor where we do not have anymore copy of the materials
		//in the materials list
		if (RenderData && CleanUpRedondantMaterialPostLoad)
		{
			bool bMaterialChange = false;
			TArray<FStaticMaterial> CompactedMaterial;
			for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
			{
				if (RenderData->LODResources.IsValidIndex(LODIndex))
				{
					FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
					const int32 NumSections = LOD.Sections.Num();
					for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
					{
						const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
						if (StaticMaterials.IsValidIndex(MaterialIndex))
						{
							if (LODIndex == 0)
							{
								//We do not compact LOD 0 material
								CompactedMaterial.Add(StaticMaterials[MaterialIndex]);
							}
							else
							{
								FMeshSectionInfo MeshSectionInfo = SectionInfoMap.Get(LODIndex, SectionIndex);
								int32 CompactedIndex = INDEX_NONE;
								if (StaticMaterials.IsValidIndex(MeshSectionInfo.MaterialIndex))
								{
									for (int32 CompactedMaterialIndex = 0; CompactedMaterialIndex < CompactedMaterial.Num(); ++CompactedMaterialIndex)
									{
										const FStaticMaterial& StaticMaterial = CompactedMaterial[CompactedMaterialIndex];
										if (StaticMaterials[MeshSectionInfo.MaterialIndex].MaterialInterface == StaticMaterial.MaterialInterface)
										{
											CompactedIndex = CompactedMaterialIndex;
											break;
										}
									}
								}

								if (CompactedIndex == INDEX_NONE)
								{
									CompactedIndex = CompactedMaterial.Add(StaticMaterials[MaterialIndex]);
								}
								if (MeshSectionInfo.MaterialIndex != CompactedIndex)
								{
									MeshSectionInfo.MaterialIndex = CompactedIndex;
									SectionInfoMap.Set(LODIndex, SectionIndex, MeshSectionInfo);
									bMaterialChange = true;
								}
							}
						}
					}
				}
			}
			//If we change some section material index or there is unused material, we must use the new compacted material list.
			if (bMaterialChange || CompactedMaterial.Num() < StaticMaterials.Num())
			{
				StaticMaterials.Empty(CompactedMaterial.Num());
				for (const FStaticMaterial &Material : CompactedMaterial)
				{
					StaticMaterials.Add(Material);
				}
				//Make sure the physic data is recompute
				if (BodySetup)
				{
					BodySetup->InvalidatePhysicsData();
				}
			}
			CleanUpRedondantMaterialPostLoad = false;
		}

		if (RenderData && GStaticMeshesThatNeedMaterialFixup.Get(this))
		{
			FixupZeroTriangleSections();
		}
	}
#endif // #if WITH_EDITOR

#if WITH_EDITORONLY_DATA
	if (GetLinkerCustomVersion(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::FixedMeshUVDensity)
	{
		UpdateUVChannelData(true);
	}
#endif

	EnforceLightmapRestrictions();

	if( FApp::CanEverRender() && !HasAnyFlags(RF_ClassDefaultObject) )
	{
		InitResources();
	}
	else
	{
		// Update any missing data when cooking.
		UpdateUVChannelData(false);
#if WITH_EDITOR
		if (RenderData)
		{
			RenderData->ResolveSectionInfo(this);
		}
#endif
	}

#if WITH_EDITOR
	// Fix extended bounds if needed
	const int32 CustomVersion = GetLinkerCustomVersion(FReleaseObjectVersion::GUID);
	if (GetLinkerUE4Version() < VER_UE4_STATIC_MESH_EXTENDED_BOUNDS || CustomVersion < FReleaseObjectVersion::StaticMeshExtendedBoundsFix)
	{
		CalculateExtendedBounds();
	}
	//Conversion of LOD distance need valid bounds it must be call after the extended Bounds fixup
	// Only required in an editor build as other builds process this in a different place
	if (bRequiresLODDistanceConversion)
	{
		// Convert distances to Display Factors
		ConvertLegacyLODDistance();
	}

	if (bRequiresLODScreenSizeConversion)
	{
		// Convert screen area to screen size
		ConvertLegacyLODScreenArea();
	}

	//Always redo the whole SectionInfoMap to be sure it contain only valid data
	//This will reuse everything valid from the just serialize SectionInfoMap.
	FMeshSectionInfoMap TempOldSectionInfoMap = SectionInfoMap;
	SectionInfoMap.Clear();
	for (int32 LODResourceIndex = 0; LODResourceIndex < RenderData->LODResources.Num(); ++LODResourceIndex)
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[LODResourceIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			if (TempOldSectionInfoMap.IsValidSection(LODResourceIndex, SectionIndex))
			{
				FMeshSectionInfo Info = TempOldSectionInfoMap.Get(LODResourceIndex, SectionIndex);
				if (StaticMaterials.IsValidIndex(Info.MaterialIndex))
				{
					//Reuse the valid data that come from the serialize
					SectionInfoMap.Set(LODResourceIndex, SectionIndex, Info);
				}
				else
				{
					//Use the render data material index, but keep the flags (collision, shadow...)
					const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
					if (StaticMaterials.IsValidIndex(MaterialIndex))
					{
						Info.MaterialIndex = MaterialIndex;
						SectionInfoMap.Set(LODResourceIndex, SectionIndex, Info);
					}
				}
			}
			else
			{
				//Create a new SectionInfoMap from the render data
				const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
				if (StaticMaterials.IsValidIndex(MaterialIndex))
				{
					SectionInfoMap.Set(LODResourceIndex, SectionIndex, FMeshSectionInfo(MaterialIndex));
				}
			}
			//Make sure the OriginalSectionInfoMap has some information, the post load only add missing slot, this data should be set when importing/re-importing the asset
			if (!OriginalSectionInfoMap.IsValidSection(LODResourceIndex, SectionIndex))
			{
				OriginalSectionInfoMap.Set(LODResourceIndex, SectionIndex, SectionInfoMap.Get(LODResourceIndex, SectionIndex));
			}
		}
	}
#endif // #if WITH_EDITOR

	// We want to always have a BodySetup, its used for per-poly collision as well
	if(BodySetup == NULL)
	{
		CreateBodySetup();
	}


	CreateNavCollision();
}

bool UStaticMesh::CanBeClusterRoot() const
{
	return false;
}

//
//	UStaticMesh::GetDesc
//

/** 
 * Returns a one line description of an object for viewing in the thumbnail view of the generic browser
 */
FString UStaticMesh::GetDesc()
{
	int32 NumTris = 0;
	int32 NumVerts = 0;
	int32 NumLODs = RenderData ? RenderData->LODResources.Num() : 0;
	if (NumLODs > 0)
	{
		NumTris = RenderData->LODResources[0].GetNumTriangles();
		NumVerts = RenderData->LODResources[0].GetNumVertices();
	}
	return FString::Printf(
		TEXT("%d LODs, %d Tris, %d Verts"),
		NumLODs,
		NumTris,
		NumVerts
		);
}


static int32 GetCollisionVertIndexForMeshVertIndex(int32 MeshVertIndex, TMap<int32, int32>& MeshToCollisionVertMap, TArray<FVector>& OutPositions, TArray< TArray<FVector2D> >& OutUVs, FPositionVertexBuffer& InPosVertBuffer, FStaticMeshVertexBuffer& InVertBuffer)
{
	int32* CollisionIndexPtr = MeshToCollisionVertMap.Find(MeshVertIndex);
	if (CollisionIndexPtr != nullptr)
	{
		return *CollisionIndexPtr;
	}
	else
	{
		// Copy UVs for vert if desired
		for (int32 ChannelIdx = 0; ChannelIdx < OutUVs.Num(); ChannelIdx++)
		{
			check(OutPositions.Num() == OutUVs[ChannelIdx].Num());
			OutUVs[ChannelIdx].Add(InVertBuffer.GetVertexUV(MeshVertIndex, ChannelIdx));
		}

		// Copy position
		int32 CollisionVertIndex = OutPositions.Add(InPosVertBuffer.VertexPosition(MeshVertIndex));

		// Add indices to map
		MeshToCollisionVertMap.Add(MeshVertIndex, CollisionVertIndex);

		return CollisionVertIndex;
	}
}

bool UStaticMesh::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool bInUseAllTriData)
{
#if WITH_EDITORONLY_DATA
	check(HasValidRenderData());

	// Get the LOD level to use for collision
	// Always use 0 if asking for 'all tri data'
	const int32 UseLODIndex = bInUseAllTriData ? 0 : FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num()-1);

	FStaticMeshLODResources& LOD = RenderData->LODResources[UseLODIndex];
	FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();

	TMap<int32, int32> MeshToCollisionVertMap; // map of static mesh verts to collision verts

	bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults; // See if we should copy UVs

	// If copying UVs, allocate array for storing them
	if (bCopyUVs)
	{
		CollisionData->UVs.AddZeroed(LOD.GetNumTexCoords());
	}

	for(int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& Section = LOD.Sections[SectionIndex];

		if (bInUseAllTriData || SectionInfoMap.Get(UseLODIndex,SectionIndex).bEnableCollision)
		{
			const uint32 OnePastLastIndex  = Section.FirstIndex + Section.NumTriangles*3;

			for (uint32 TriIdx = Section.FirstIndex; TriIdx < OnePastLastIndex; TriIdx += 3)
			{
				FTriIndices TriIndex;
				TriIndex.v0 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +0], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);
				TriIndex.v1 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +1], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);
				TriIndex.v2 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +2], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);

				CollisionData->Indices.Add(TriIndex);
				CollisionData->MaterialIndices.Add(Section.MaterialIndex);
			}
		}
	}
	CollisionData->bFlipNormals = true;
	
	// We only have a valid TriMesh if the CollisionData has vertices AND indices. For meshes with disabled section collision, it
	// can happen that the indices will be empty, in which case we do not want to consider that as valid trimesh data
	return CollisionData->Vertices.Num() > 0 && CollisionData->Indices.Num() > 0;
#else // #if WITH_EDITORONLY_DATA
	return false;
#endif // #if WITH_EDITORONLY_DATA
}

bool UStaticMesh::ContainsPhysicsTriMeshData(bool bInUseAllTriData) const 
{
#if WITH_EDITORONLY_DATA
	if(RenderData == nullptr || RenderData->LODResources.Num() == 0)
	{
		return false;
	}

	// Get the LOD level to use for collision
	// Always use 0 if asking for 'all tri data'
	const int32 UseLODIndex = bInUseAllTriData ? 0 : FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);

	if (RenderData->LODResources[UseLODIndex].VertexBuffers.PositionVertexBuffer.GetNumVertices() > 0)
	{
		// Get the LOD level to use for collision
		FStaticMeshLODResources& LOD = RenderData->LODResources[UseLODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& Section = LOD.Sections[SectionIndex];
			if ((bInUseAllTriData || SectionInfoMap.Get(UseLODIndex, SectionIndex).bEnableCollision) && Section.NumTriangles > 0)
			{
				return true;
			}
		}
	}
	return false; 
#else // #if WITH_EDITORONLY_DATA
	return false;
#endif // #if WITH_EDITORONLY_DATA
}

void UStaticMesh::GetMeshId(FString& OutMeshId)
{
#if WITH_EDITORONLY_DATA
	if (RenderData)
	{
		OutMeshId = RenderData->DerivedDataKey;
	}
#endif
}

void UStaticMesh::AddAssetUserData(UAssetUserData* InUserData)
{
	if(InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if(ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UStaticMesh::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UStaticMesh::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UStaticMesh::GetAssetUserDataArray() const 
{
	return &AssetUserData;
}

/**
 * Create BodySetup for this staticmesh 
 */
void UStaticMesh::CreateBodySetup()
{
	if (BodySetup==NULL)
	{
		BodySetup = NewObject<UBodySetup>(this);
		BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
}

void UStaticMesh::CreateNavCollision(const bool bIsUpdate)
{
	if (bHasNavigationData && BodySetup != nullptr)
	{
		if (NavCollision == nullptr)
		{
			NavCollision = UNavCollisionBase::ConstructNew(*this);
		}

		if (NavCollision)
		{
#if WITH_EDITOR
			if (bIsUpdate)
			{
				NavCollision->InvalidateCollision();
			}
#endif // WITH_EDITOR
			NavCollision->Setup(BodySetup);
		}
	}
	else
	{
		NavCollision = nullptr;
	}
}

void UStaticMesh::MarkAsNotHavingNavigationData()
{
	bHasNavigationData = false;
	NavCollision = nullptr;
}

/**
 * Returns vertex color data by position.
 * For matching to reimported meshes that may have changed or copying vertex paint data from mesh to mesh.
 *
 *	@param	VertexColorData		(out)A map of vertex position data and its color. The method fills this map.
 */
void UStaticMesh::GetVertexColorData(TMap<FVector, FColor>& VertexColorData)
{
	VertexColorData.Empty();
#if WITH_EDITOR
	// What LOD to get vertex colors from.  
	// Currently mesh painting only allows for painting on the first lod.
	const uint32 PaintingMeshLODIndex = 0;
	if (SourceModels.IsValidIndex(PaintingMeshLODIndex))
	{
		if (SourceModels[PaintingMeshLODIndex].IsRawMeshEmpty() == false)
		{
			// Extract the raw mesh.
			FRawMesh Mesh;
			SourceModels[PaintingMeshLODIndex].LoadRawMesh(Mesh);
			// Nothing to copy if there are no colors stored.
			if (Mesh.WedgeColors.Num() != 0 && Mesh.WedgeColors.Num() == Mesh.WedgeIndices.Num())
			{
				// Build a mapping of vertex positions to vertex colors.
				for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
				{
					FVector Position = Mesh.VertexPositions[Mesh.WedgeIndices[WedgeIndex]];
					FColor Color = Mesh.WedgeColors[WedgeIndex];
					if (!VertexColorData.Contains(Position))
					{
						VertexColorData.Add(Position, Color);
					}
				}
			}
		}
	}
#endif // #if WITH_EDITORONLY_DATA
}

/**
 * Sets vertex color data by position.
 * Map of vertex color data by position is matched to the vertex position in the mesh
 * and nearest matching vertex color is used.
 *
 *	@param	VertexColorData		A map of vertex position data and color.
 */
void UStaticMesh::SetVertexColorData(const TMap<FVector, FColor>& VertexColorData)
{
#if WITH_EDITOR
	// What LOD to get vertex colors from.  
	// Currently mesh painting only allows for painting on the first lod.
	const uint32 PaintingMeshLODIndex = 0;
	if (SourceModels.IsValidIndex(PaintingMeshLODIndex))
	{
		if (SourceModels[PaintingMeshLODIndex].IsRawMeshEmpty() == false)
		{
			// Extract the raw mesh.
			FRawMesh Mesh;
			SourceModels[PaintingMeshLODIndex].LoadRawMesh(Mesh);

			// Reserve space for the new vertex colors.
			if (Mesh.WedgeColors.Num() == 0 || Mesh.WedgeColors.Num() != Mesh.WedgeIndices.Num())
			{
				Mesh.WedgeColors.Empty(Mesh.WedgeIndices.Num());
				Mesh.WedgeColors.AddUninitialized(Mesh.WedgeIndices.Num());
			}

			// Build a mapping of vertex positions to vertex colors.
			for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
			{
				FVector Position = Mesh.VertexPositions[Mesh.WedgeIndices[WedgeIndex]];
				const FColor* Color = VertexColorData.Find(Position);
				if (Color)
				{
					Mesh.WedgeColors[WedgeIndex] = *Color;
				}
				else
				{
					Mesh.WedgeColors[WedgeIndex] = FColor(255, 255, 255, 255);
				}
			}

			// Save the new raw mesh.
			SourceModels[PaintingMeshLODIndex].SaveRawMesh(Mesh);
		}
	}
	// TODO_STATICMESH: Build?
#endif // #if WITH_EDITOR
}

ENGINE_API void UStaticMesh::RemoveVertexColors()
{
#if WITH_EDITOR
	bool bRemovedVertexColors = false;

	for (FStaticMeshSourceModel& SourceModel : SourceModels)
	{
		if (!SourceModel.IsRawMeshEmpty())
		{
			FRawMesh RawMesh;
			SourceModel.LoadRawMesh(RawMesh);

			if (RawMesh.WedgeColors.Num() > 0)
			{
				RawMesh.WedgeColors.Empty();

				SourceModel.SaveRawMesh(RawMesh);

				bRemovedVertexColors = true;
			}
		}
	}

	if (bRemovedVertexColors)
	{
		Build();
		MarkPackageDirty();
	}
#endif
}

void UStaticMesh::EnforceLightmapRestrictions()
{
	// Legacy content may contain a lightmap resolution of 0, which was valid when vertex lightmaps were supported, but not anymore with only texture lightmaps
	LightMapResolution = FMath::Max(LightMapResolution, 4);

	int32 NumUVs = 16;

	if (RenderData)
	{
		for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
		{
			NumUVs = FMath::Min(RenderData->LODResources[LODIndex].GetNumTexCoords(),NumUVs);
		}
	}
	else
	{
		NumUVs = 1;
	}

	// Clamp LightMapCoordinateIndex to be valid for all lightmap uvs
	LightMapCoordinateIndex = FMath::Clamp(LightMapCoordinateIndex, 0, NumUVs - 1);
}

/**
 * Static: Processes the specified static mesh for light map UV problems
 *
 * @param	InStaticMesh					Static mesh to process
 * @param	InOutAssetsWithMissingUVSets	Array of assets that we found with missing UV sets
 * @param	InOutAssetsWithBadUVSets		Array of assets that we found with bad UV sets
 * @param	InOutAssetsWithValidUVSets		Array of assets that we found with valid UV sets
 * @param	bInVerbose						If true, log the items as they are found
 */
void UStaticMesh::CheckLightMapUVs( UStaticMesh* InStaticMesh, TArray< FString >& InOutAssetsWithMissingUVSets, TArray< FString >& InOutAssetsWithBadUVSets, TArray< FString >& InOutAssetsWithValidUVSets, bool bInVerbose )
{
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);
	if (!bAllowStaticLighting)
	{
		// We do not need to check for lightmap UV problems when we do not allow static lighting
		return;
	}

	struct FLocal
	{
		/**
		 * Checks to see if a point overlaps a triangle
		 *
		 * @param	P	Point
		 * @param	A	First triangle vertex
		 * @param	B	Second triangle vertex
		 * @param	C	Third triangle vertex
		 *
		 * @return	true if the point overlaps the triangle
		 */
		bool IsPointInTriangle( const FVector& P, const FVector& A, const FVector& B, const FVector& C, const float Epsilon )
		{
			struct
			{
				bool SameSide( const FVector& P1, const FVector& P2, const FVector& InA, const FVector& InB, const float InEpsilon )
				{
					const FVector Cross1((InB - InA) ^ (P1 - InA));
					const FVector Cross2((InB - InA) ^ (P2 - InA));
					return (Cross1 | Cross2) >= -InEpsilon;
				}
			} Local;

			return ( Local.SameSide( P, A, B, C, Epsilon ) &&
					 Local.SameSide( P, B, A, C, Epsilon ) &&
					 Local.SameSide( P, C, A, B, Epsilon ) );
		}
		
		/**
		 * Checks to see if a point overlaps a triangle
		 *
		 * @param	P	Point
		 * @param	Triangle	triangle vertices
		 *
		 * @return	true if the point overlaps the triangle
		 */
		bool IsPointInTriangle(const FVector2D & P, const FVector2D (&Triangle)[3])
		{
			// Bias toward non-overlapping so sliver triangles won't overlap their adjoined neighbors
			const float TestEpsilon = -0.001f;
			// Test for overlap
			if( IsPointInTriangle(
				FVector( P, 0.0f ),
				FVector( Triangle[0], 0.0f ),
				FVector( Triangle[1], 0.0f ),
				FVector( Triangle[2], 0.0f ),
				TestEpsilon ) )
			{
				return true;
			}
			return false;
		}

		/**
		 * Checks for UVs outside of a 0.0 to 1.0 range.
		 *
		 * @param	TriangleUVs	a referenced array of 3 UV coordinates.
		 *
		 * @return	true if UVs are <0.0 or >1.0
		 */
		bool AreUVsOutOfRange(const FVector2D (&TriangleUVs)[3])
		{
			// Test for UVs outside of the 0.0 to 1.0 range (wrapped/clamped)
			for(int32 UVIndex = 0; UVIndex < 3; UVIndex++)
			{
				const FVector2D& CurVertUV = TriangleUVs[UVIndex];
				const float TestEpsilon = 0.001f;
				for( int32 CurDimIndex = 0; CurDimIndex < 2; ++CurDimIndex )
				{
					if( CurVertUV[ CurDimIndex ] < ( 0.0f - TestEpsilon ) || CurVertUV[ CurDimIndex ] > ( 1.0f + TestEpsilon ) )
					{
						return true;
					}
				}
			}
			return false;
		}

		/**
		 * Fills an array with 3 UV coordinates for a specified triangle from a FStaticMeshLODResources object.
		 *
		 * @param	MeshLOD	Source mesh.
		 * @param	TriangleIndex	triangle to get UV data from
		 * @param	UVChannel UV channel to extract
		 * @param	TriangleUVsOUT an array which is filled with the UV data
		 */
		void GetTriangleUVs( const FStaticMeshLODResources& MeshLOD, const int32 TriangleIndex, const int32 UVChannel, FVector2D (&TriangleUVsOUT)[3])
		{
			check( TriangleIndex < MeshLOD.GetNumTriangles());
			
			FIndexArrayView Indices = MeshLOD.IndexBuffer.GetArrayView();
			const int32 StartIndex = TriangleIndex*3;			
			const uint32 VertexIndices[] = {Indices[StartIndex + 0], Indices[StartIndex + 1], Indices[StartIndex + 2]};
			for(int i = 0; i<3;i++)
			{
				TriangleUVsOUT[i] = MeshLOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndices[i], UVChannel);		
			}
		}

		enum UVCheckResult { UVCheck_Missing, UVCheck_Bad, UVCheck_OK, UVCheck_NoTriangles};
		/**
		 * Performs a UV check on a specific LOD from a UStaticMesh.
		 *
		 * @param	MeshLOD	a referenced array of 3 UV coordinates.
		 * @param	LightMapCoordinateIndex The UV channel containing the light map UVs.
		 * @param	OverlappingLightMapUVTriangleCountOUT Filled with the number of triangles that overlap one another.
		 * @param	OutOfBoundsTriangleCountOUT Filled with the number of triangles whose UVs are out of 0..1 range.
		 * @return	UVCheckResult UVCheck_Missing: light map UV channel does not exist in the data. UVCheck_Bad: one or more triangles break UV mapping rules. UVCheck_NoTriangle: The specified mesh has no triangles. UVCheck_OK: no problems were found.
		 */
		UVCheckResult CheckLODLightMapUVs( const FStaticMeshLODResources& MeshLOD, const int32 InLightMapCoordinateIndex, int32& OverlappingLightMapUVTriangleCountOUT, int32& OutOfBoundsTriangleCountOUT)
		{
			const int32 TriangleCount = MeshLOD.GetNumTriangles();
			if(TriangleCount==0)
			{
				return UVCheck_NoTriangles;
			}
			OverlappingLightMapUVTriangleCountOUT = 0;
			OutOfBoundsTriangleCountOUT = 0;

			TArray< int32 > TriangleOverlapCounts;
			TriangleOverlapCounts.AddZeroed( TriangleCount );

			if (InLightMapCoordinateIndex >= MeshLOD.GetNumTexCoords())
			{
				return UVCheck_Missing;
			}

			for(int32 CurTri = 0; CurTri<TriangleCount;CurTri++)
			{
				FVector2D CurTriangleUVs[3];
				GetTriangleUVs(MeshLOD, CurTri, InLightMapCoordinateIndex, CurTriangleUVs);
				FVector2D CurTriangleUVCentroid = ( CurTriangleUVs[0] + CurTriangleUVs[1] + CurTriangleUVs[2] ) / 3.0f;
		
				if( AreUVsOutOfRange(CurTriangleUVs) )
				{
					++OutOfBoundsTriangleCountOUT;
				}

				if(TriangleOverlapCounts[CurTri] != 0)
				{
					continue;
				}
				for(int32 OtherTri = CurTri+1; OtherTri<TriangleCount;OtherTri++)
				{
					if(TriangleOverlapCounts[OtherTri] != 0)
					{
						continue;
					}

					FVector2D OtherTriangleUVs[3];
					GetTriangleUVs(MeshLOD, OtherTri, InLightMapCoordinateIndex, OtherTriangleUVs);
					FVector2D OtherTriangleUVCentroid = ( OtherTriangleUVs[0] + OtherTriangleUVs[1] + OtherTriangleUVs[2] ) / 3.0f;

					bool result1 = IsPointInTriangle(CurTriangleUVCentroid, OtherTriangleUVs );
					bool result2 = IsPointInTriangle(OtherTriangleUVCentroid, CurTriangleUVs );

					if( result1 || result2)
					{
						++OverlappingLightMapUVTriangleCountOUT;
						++TriangleOverlapCounts[ CurTri ];
						++OverlappingLightMapUVTriangleCountOUT;
						++TriangleOverlapCounts[ OtherTri ];
					}
				}
			}

			return (OutOfBoundsTriangleCountOUT != 0 || OverlappingLightMapUVTriangleCountOUT !=0 ) ? UVCheck_Bad : UVCheck_OK;
		}
	} Local;

	check( InStaticMesh != NULL );

	TArray< int32 > TriangleOverlapCounts;

	const int32 NumLods = InStaticMesh->GetNumLODs();
	for( int32 CurLODModelIndex = 0; CurLODModelIndex < NumLods; ++CurLODModelIndex )
	{
		const FStaticMeshLODResources& RenderData = InStaticMesh->RenderData->LODResources[CurLODModelIndex];
		int32 LightMapTextureCoordinateIndex = InStaticMesh->LightMapCoordinateIndex;

		// We expect the light map texture coordinate to be greater than zero, as the first UV set
		// should never really be used for light maps, unless this mesh was exported as a light mapped uv set.
		if( LightMapTextureCoordinateIndex <= 0 && RenderData.GetNumTexCoords() > 1 )
		{	
			LightMapTextureCoordinateIndex = 1;
		}

		int32 OverlappingLightMapUVTriangleCount = 0;
		int32 OutOfBoundsTriangleCount = 0;

		const FLocal::UVCheckResult result = Local.CheckLODLightMapUVs( RenderData, LightMapTextureCoordinateIndex, OverlappingLightMapUVTriangleCount, OutOfBoundsTriangleCount);
		switch(result)
		{
			case FLocal::UVCheck_OK:
				InOutAssetsWithValidUVSets.Add( InStaticMesh->GetFullName() );
			break;
			case FLocal::UVCheck_Bad:
				InOutAssetsWithBadUVSets.Add( InStaticMesh->GetFullName() );
			break;
			case FLocal::UVCheck_Missing:
				InOutAssetsWithMissingUVSets.Add( InStaticMesh->GetFullName() );
			break;
			default:
			break;
		}

		if(bInVerbose == true)
		{
			switch(result)
			{
				case FLocal::UVCheck_OK:
					UE_LOG(LogStaticMesh, Log, TEXT( "[%s, LOD %i] light map UVs OK" ), *InStaticMesh->GetName(), CurLODModelIndex );
					break;
				case FLocal::UVCheck_Bad:
					if( OverlappingLightMapUVTriangleCount > 0 )
					{
						UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] %i triangles with overlapping UVs (of %i) (UV set %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, OverlappingLightMapUVTriangleCount, RenderData.GetNumTriangles(), LightMapTextureCoordinateIndex );
					}
					if( OutOfBoundsTriangleCount > 0 )
					{
						UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] %i triangles with out-of-bound UVs (of %i) (UV set %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, OutOfBoundsTriangleCount, RenderData.GetNumTriangles(), LightMapTextureCoordinateIndex );
					}
					break;
				case FLocal::UVCheck_Missing:
					UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] missing light map UVs (Res %i, CoordIndex %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, InStaticMesh->LightMapResolution, InStaticMesh->LightMapCoordinateIndex );
					break;
				case FLocal::UVCheck_NoTriangles:
					UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] doesn't have any triangles" ), *InStaticMesh->GetName(), CurLODModelIndex );
					break;
				default:
					break;
			}
		}
	}
}

UMaterialInterface* UStaticMesh::GetMaterial(int32 MaterialIndex) const
{
	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		return StaticMaterials[MaterialIndex].MaterialInterface;
	}

	return NULL;
}

int32 UStaticMesh::GetMaterialIndex(FName MaterialSlotName) const
{
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		const FStaticMaterial &StaticMaterial = StaticMaterials[MaterialIndex];
		if (StaticMaterial.MaterialSlotName == MaterialSlotName)
		{
			return MaterialIndex;
		}
	}
	return -1;
}

#if WITH_EDITOR
void UStaticMesh::SetMaterial(int32 MaterialIndex, UMaterialInterface* NewMaterial)
{
	static FName NAME_StaticMaterials = GET_MEMBER_NAME_CHECKED(UStaticMesh, StaticMaterials);

	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		FScopedTransaction ScopeTransaction(LOCTEXT("StaticMeshMaterialChanged", "StaticMesh: Material changed"));

		// flag the property (Materials) we're modifying so that not all of the object is rebuilt.
		UProperty* ChangedProperty = FindField<UProperty>(UStaticMesh::StaticClass(), NAME_StaticMaterials);
		check(ChangedProperty);
		PreEditChange(ChangedProperty);

		StaticMaterials[MaterialIndex].MaterialInterface = NewMaterial;
		if (NewMaterial != nullptr)
		{
			//Set the Material slot name to a good default one
			if (StaticMaterials[MaterialIndex].MaterialSlotName == NAME_None)
			{
				StaticMaterials[MaterialIndex].MaterialSlotName = NewMaterial->GetFName();
			}
			
			//Set the original fbx material name so we can re-import correctly, ensure the name is unique
			if (StaticMaterials[MaterialIndex].ImportedMaterialSlotName == NAME_None)
			{
				auto IsMaterialNameUnique = [this, MaterialIndex](const FName TestName)
				{
					for (int32 MatIndex = 0; MatIndex < StaticMaterials.Num(); ++MatIndex)
					{
						if (MatIndex == MaterialIndex)
						{
							continue;
						}
						if (StaticMaterials[MatIndex].ImportedMaterialSlotName == TestName)
						{
							return false;
						}
					}
					return true;
				};

				int32 MatchNameCounter = 0;
				//Make sure the name is unique for imported material slot name
				bool bUniqueName = false;
				FString MaterialSlotName = NewMaterial->GetName();
				while (!bUniqueName)
				{
					bUniqueName = true;
					if (!IsMaterialNameUnique(FName(*MaterialSlotName)))
					{
						bUniqueName = false;
						MatchNameCounter++;
						MaterialSlotName = NewMaterial->GetName() + TEXT("_") + FString::FromInt(MatchNameCounter);
					}
				}
				StaticMaterials[MaterialIndex].ImportedMaterialSlotName = FName(*MaterialSlotName);
			}
		}

		if (ChangedProperty)
		{
			FPropertyChangedEvent PropertyUpdateStruct(ChangedProperty);
			PostEditChangeProperty(PropertyUpdateStruct);
		}
		else
		{
			Modify();
			PostEditChange();
		}
		if (BodySetup)
		{
			BodySetup->CreatePhysicsMeshes();
		}
	}
}
#endif //WITH_EDITOR

int32 UStaticMesh::GetMaterialIndexFromImportedMaterialSlotName(FName ImportedMaterialSlotName) const
{
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		const FStaticMaterial &StaticMaterial = StaticMaterials[MaterialIndex];
		if (StaticMaterial.ImportedMaterialSlotName == ImportedMaterialSlotName)
		{
			return MaterialIndex;
		}
	}
	return INDEX_NONE;
}

/**
 * Returns the render data to use for exporting the specified LOD. This method should always
 * be called when exporting a static mesh.
 */
const FStaticMeshLODResources& UStaticMesh::GetLODForExport(int32 LODIndex) const
{
	check(RenderData);
	LODIndex = FMath::Clamp<int32>( LODIndex, 0, RenderData->LODResources.Num()-1 );
	// TODO_STATICMESH: Don't allow exporting simplified meshes?
	return RenderData->LODResources[LODIndex];
}

#if WITH_EDITOR
bool UStaticMesh::CanLODsShareStaticLighting() const
{
	bool bCanShareData = true;
	for (int32 LODIndex = 1; bCanShareData && LODIndex < SourceModels.Num(); ++LODIndex)
	{
		bCanShareData = bCanShareData && SourceModels[LODIndex].RawMeshBulkData->IsEmpty();
	}

	if (SpeedTreeWind.IsValid())
	{
		// SpeedTrees are set up for lighting to share between LODs
		bCanShareData = true;
	}

	return bCanShareData;
}

void UStaticMesh::ConvertLegacyLODDistance()
{
	check(SourceModels.Num() > 0);
	check(SourceModels.Num() <= MAX_STATIC_MESH_LODS);

	if(SourceModels.Num() == 1)
	{
		// Only one model, 
		SourceModels[0].ScreenSize.Default = 1.0f;
	}
	else
	{
		// Multiple models, we should have LOD distance data.
		// Assuming an FOV of 90 and a screen size of 1920x1080 to estimate an appropriate display factor.
		const float HalfFOV = PI / 4.0f;
		const float ScreenWidth = 1920.0f;
		const float ScreenHeight = 1080.0f;

		for(int32 ModelIndex = 0 ; ModelIndex < SourceModels.Num() ; ++ModelIndex)
		{
			FStaticMeshSourceModel& SrcModel = SourceModels[ModelIndex];

			if(SrcModel.LODDistance_DEPRECATED == 0.0f)
			{
				SrcModel.ScreenSize.Default = 1.0f;
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
			else
			{
				// Create a screen position from the LOD distance
				const FVector4 PointToTest(0.0f, 0.0f, SrcModel.LODDistance_DEPRECATED, 1.0f);
				FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
				FVector4 ScreenPosition = ProjMatrix.TransformFVector4(PointToTest);
				// Convert to a percentage of the screen
				const float ScreenMultiple = ScreenWidth / 2.0f * ProjMatrix.M[0][0];
				const float ScreenRadius = ScreenMultiple * GetBounds().SphereRadius / FMath::Max(ScreenPosition.W, 1.0f);
				const float ScreenArea = ScreenWidth * ScreenHeight;
				const float BoundsArea = PI * ScreenRadius * ScreenRadius;
				SrcModel.ScreenSize.Default = FMath::Clamp(BoundsArea / ScreenArea, 0.0f, 1.0f);
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
		}
	}
}

void UStaticMesh::ConvertLegacyLODScreenArea()
{
	check(SourceModels.Num() > 0);
	check(SourceModels.Num() <= MAX_STATIC_MESH_LODS);

	if (SourceModels.Num() == 1)
	{
		// Only one model, 
		SourceModels[0].ScreenSize.Default = 1.0f;
	}
	else
	{
		// Use 1080p, 90 degree FOV as a default, as this should not cause runtime regressions in the common case.
		const float HalfFOV = PI * 0.25f;
		const float ScreenWidth = 1920.0f;
		const float ScreenHeight = 1080.0f;
		const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
		FBoxSphereBounds Bounds = GetBounds();

		// Multiple models, we should have LOD screen area data.
		for (int32 ModelIndex = 0; ModelIndex < SourceModels.Num(); ++ModelIndex)
		{
			FStaticMeshSourceModel& SrcModel = SourceModels[ModelIndex];

			if (SrcModel.ScreenSize.Default == 0.0f)
			{
				SrcModel.ScreenSize.Default = 1.0f;
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
			else
			{
				// legacy transition screen size was previously a screen AREA fraction using resolution-scaled values, so we need to convert to distance first to correctly calculate the threshold
				const float ScreenArea = SrcModel.ScreenSize.Default * (ScreenWidth * ScreenHeight);
				const float ScreenRadius = FMath::Sqrt(ScreenArea / PI);
				const float ScreenDistance = FMath::Max(ScreenWidth / 2.0f * ProjMatrix.M[0][0], ScreenHeight / 2.0f * ProjMatrix.M[1][1]) * Bounds.SphereRadius / ScreenRadius;

				// Now convert using the query function
				SrcModel.ScreenSize.Default = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ScreenDistance), ProjMatrix);
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
		}
	}
}

void UStaticMesh::GenerateLodsInPackage()
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("StaticMeshName"), FText::FromString(GetName()));
	FStaticMeshStatusMessageContext StatusContext(FText::Format(NSLOCTEXT("Engine", "SavingStaticMeshLODsStatus", "Saving generated LODs for static mesh {StaticMeshName}..."), Args));

	// Get LODGroup info
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);
	const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

	// Generate the reduced models
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
	if (MeshUtilities.GenerateStaticMeshLODs(this, LODSettings.GetLODGroup(LODGroup)))
	{
		// Clear LOD settings
		LODGroup = NAME_None;
		const auto& NewGroup = LODSettings.GetLODGroup(LODGroup);
		for (int32 Index = 0; Index < SourceModels.Num(); ++Index)
		{
			SourceModels[Index].ReductionSettings = NewGroup.GetDefaultSettings(0);
		}

		Build(true);

		// Raw mesh is now dirty, so the package has to be resaved
		MarkPackageDirty();
	}
}

#endif // #if WITH_EDITOR

UStaticMeshSocket* UStaticMesh::FindSocket(FName InSocketName)
{
	if(InSocketName == NAME_None)
	{
		return NULL;
	}

	for(int32 i=0; i<Sockets.Num(); i++)
	{
		UStaticMeshSocket* Socket = Sockets[i];
		if(Socket && Socket->SocketName == InSocketName)
		{
			return Socket;
		}
	}
	return NULL;
}

/*-----------------------------------------------------------------------------
UStaticMeshSocket
-----------------------------------------------------------------------------*/

UStaticMeshSocket::UStaticMeshSocket(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RelativeScale = FVector(1.0f, 1.0f, 1.0f);
#if WITH_EDITORONLY_DATA
	bSocketCreatedAtImport = false;
#endif
}

/** Utility that returns the current matrix for this socket. */
bool UStaticMeshSocket::GetSocketMatrix(FMatrix& OutMatrix, UStaticMeshComponent const* MeshComp) const
{
	check( MeshComp );
	OutMatrix = FScaleRotationTranslationMatrix( RelativeScale, RelativeRotation, RelativeLocation ) * MeshComp->GetComponentTransform().ToMatrixWithScale();
	return true;
}

bool UStaticMeshSocket::GetSocketTransform(FTransform& OutTransform, class UStaticMeshComponent const* MeshComp) const
{
	check( MeshComp );
	OutTransform = FTransform(RelativeRotation, RelativeLocation, RelativeScale) * MeshComp->GetComponentTransform();
	return true;
}

bool UStaticMeshSocket::AttachActor(AActor* Actor,  UStaticMeshComponent* MeshComp) const
{
	bool bAttached = false;

	// Don't support attaching to own socket
	if (Actor != MeshComp->GetOwner() && Actor->GetRootComponent())
	{
		FMatrix SocketTM;
		if( GetSocketMatrix( SocketTM, MeshComp ) )
		{
			Actor->Modify();

			Actor->SetActorLocation(SocketTM.GetOrigin(), false);
			Actor->SetActorRotation(SocketTM.Rotator());
			Actor->GetRootComponent()->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

#if WITH_EDITOR
			if (GIsEditor)
			{
				Actor->PreEditChange(NULL);
				Actor->PostEditChange();
			}
#endif // WITH_EDITOR

			bAttached = true;
		}
	}
	return bAttached;
}

#if WITH_EDITOR
void UStaticMeshSocket::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	if( PropertyChangedEvent.Property )
	{
		ChangedEvent.Broadcast( this, PropertyChangedEvent.MemberProperty );
	}
}
#endif

void UStaticMeshSocket::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if(Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::MeshSocketScaleUtilization)
	{
		// Set the relative scale to 1.0. As it was not used before this should allow existing data
		// to work as expected.
		RelativeScale = FVector(1.0f, 1.0f, 1.0f);
	}
}

#undef LOCTEXT_NAMESPACE
