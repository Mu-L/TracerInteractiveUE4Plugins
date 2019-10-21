// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
InstancedFoliage.cpp: Instanced foliage implementation.
=============================================================================*/

#include "InstancedFoliage.h"
#include "Templates/SubclassOf.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/DamageType.h"
#include "Engine/EngineTypes.h"
#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "FoliageType.h"
#include "UObject/UObjectIterator.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageType_Actor.h"
#include "InstancedFoliageActor.h"
#include "Serialization/CustomVersion.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Brush.h"
#include "Engine/Engine.h"
#include "Components/BrushComponent.h"
#include "Components/ModelComponent.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "ProceduralFoliageComponent.h"
#include "ProceduralFoliageBlockingVolume.h"
#include "ProceduralFoliageVolume.h"
#include "EngineUtils.h"
#include "EngineGlobals.h"
#include "Engine/StaticMesh.h"
#include "DrawDebugHelpers.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "PreviewScene.h"
#include "FoliageActor.h"

#define LOCTEXT_NAMESPACE "InstancedFoliage"

#define DO_FOLIAGE_CHECK			0			// whether to validate foliage data during editing.
#define FOLIAGE_CHECK_TRANSFORM		0			// whether to compare transforms between render and painting data.

DEFINE_LOG_CATEGORY(LogInstancedFoliage);

DECLARE_CYCLE_STAT(TEXT("FoliageActor_Trace"), STAT_FoliageTrace, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("FoliageMeshInfo_AddInstance"), STAT_FoliageAddInstance, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("FoliageMeshInfo_RemoveInstance"), STAT_FoliageRemoveInstance, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("FoliageMeshInfo_CreateComponent"), STAT_FoliageCreateComponent, STATGROUP_Foliage);

extern FName FoliageActorTag;

static TAutoConsoleVariable<int32> CVarFoliageDiscardDataOnLoad(
	TEXT("foliage.DiscardDataOnLoad"),
	0,
	TEXT("1: Discard scalable foliage data on load (disables all scalable foliage types); 0: Keep scalable foliage data (requires reloading level)"),
	ECVF_Scalability);

const FGuid FFoliageCustomVersion::GUID(0x430C4D19, 0x71544970, 0x87699B69, 0xDF90B0E5);
// Register the custom version with core
FCustomVersionRegistration GRegisterFoliageCustomVersion(FFoliageCustomVersion::GUID, FFoliageCustomVersion::LatestVersion, TEXT("FoliageVer"));

///
/// FFoliageStaticMesh
///
struct FFoliageStaticMesh : public FFoliageImpl
{
	FFoliageStaticMesh(UHierarchicalInstancedStaticMeshComponent* InComponent)
		: FFoliageImpl()
		, Component(InComponent)
#if WITH_EDITOR
		, UpdateDepth(0)
		, bPreviousValue(false)
		, bInvalidateLightingCache(false)
#endif
	{

	}

	UHierarchicalInstancedStaticMeshComponent* Component;
#if WITH_EDITOR
	int32 UpdateDepth;
	bool bPreviousValue;

	bool bInvalidateLightingCache;
#endif

	virtual void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual bool IsInitialized() const override { return Component != nullptr; }
	virtual void Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType) override;
	virtual void Uninitialize() override;
	virtual int32 GetInstanceCount() const override;
	virtual void PreAddInstances(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, int32 Count) override;
	virtual void AddInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& NewInstance) override;
	virtual void RemoveInstance(int32 InstanceIndex) override;
	virtual void SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport) override;
	virtual FTransform GetInstanceWorldTransform(int32 InstanceIndex) const override;
	virtual void PostUpdateInstances() override;
	virtual bool IsOwnedComponent(const UPrimitiveComponent* HitComponent) const override;

	virtual void SelectInstances(bool bSelect, int32 InstanceIndex, int32 Count) override;
	virtual void ApplySelection(bool bApply, const TSet<int32>& SelectedIndices) override;
	virtual void ClearSelection(const TSet<int32>& SelectedIndices) override;

	virtual void BeginUpdate() override;
	virtual void EndUpdate() override;
	virtual void Refresh(AInstancedFoliageActor* IFA, const TArray<FFoliageInstance>& Instances, bool Async, bool Force) override;
	virtual void OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews) override;
	virtual void PreEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType) override;
	virtual void PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices) override;
	virtual void NotifyFoliageTypeWillChange(AInstancedFoliageActor* IFA, UFoliageType* FoliageType) override;
	virtual void NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices, bool bSourceChanged) override;
	virtual void EnterEditMode() override;
	virtual void ExitEditMode() override;

	void HandleComponentMeshBoundsChanged(const FBoxSphereBounds& NewBounds);
#endif

	virtual int32 GetOverlappingSphereCount(const FSphere& Sphere) const override;
	virtual int32 GetOverlappingBoxCount(const FBox& Box) const override;
	virtual void GetOverlappingBoxTransforms(const FBox& Box, TArray<FTransform>& OutTransforms) const override;
	virtual void GetOverlappingMeshCount(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const override;

	void UpdateComponentSettings(const UFoliageType_InstancedStaticMesh* InSettings);
	// Recreate the component if the FoliageType's ComponentClass doesn't match the Component's class
	void CheckComponentClass(AInstancedFoliageActor* InIFA, const UFoliageType_InstancedStaticMesh* InSettings, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices);
	void ReapplyInstancesToComponent(const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices);
	void CreateNewComponent(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings);
};

// Legacy (< FFoliageCustomVersion::CrossLevelBase) serializer
FArchive& operator<<(FArchive& Ar, FFoliageInstance_Deprecated& Instance)
{
	Ar << Instance.Base;
	Ar << Instance.Location;
	Ar << Instance.Rotation;
	Ar << Instance.DrawScale3D;

	if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageUsingHierarchicalISMC)
	{
		int32 OldClusterIndex;
		Ar << OldClusterIndex;
		Ar << Instance.PreAlignRotation;
		Ar << Instance.Flags;

		if (OldClusterIndex == INDEX_NONE)
		{
			// When converting, we need to skip over any instance that was previously deleted but still in the Instances array.
			Instance.Flags |= FOLIAGE_InstanceDeleted;
		}
	}
	else
	{
		Ar << Instance.PreAlignRotation;
		Ar << Instance.Flags;
	}

	Ar << Instance.ZOffset;

#if WITH_EDITORONLY_DATA
	if (!Ar.ArIsFilterEditorOnly && Ar.CustomVer(FFoliageCustomVersion::GUID) >= FFoliageCustomVersion::ProceduralGuid)
	{
		Ar << Instance.ProceduralGuid;
	}
#endif

	return Ar;
}

//
// Serializers for struct data
//
FArchive& operator<<(FArchive& Ar, FFoliageInstance& Instance)
{
	Ar << Instance.Location;
	Ar << Instance.Rotation;
	Ar << Instance.DrawScale3D;
	Ar << Instance.PreAlignRotation;
	Ar << Instance.ProceduralGuid;
	Ar << Instance.Flags;
	Ar << Instance.ZOffset;
	Ar << Instance.BaseId;

	return Ar;
}

static void ConvertDeprecatedFoliageMeshes(
	AInstancedFoliageActor* IFA,
	const TMap<UFoliageType*, TUniqueObj<FFoliageMeshInfo_Deprecated>>& FoliageMeshesDeprecated,
	TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos)
{
#if WITH_EDITORONLY_DATA	
	for (auto Pair : FoliageMeshesDeprecated)
	{
		auto& FoliageMesh = FoliageInfos.Add(Pair.Key);
		const auto& FoliageMeshDeprecated = Pair.Value;

		// Old Foliage mesh is always static mesh (no actors)
		FoliageMesh->Type = EFoliageImplType::StaticMesh;
		FoliageMesh->Implementation.Reset(new FFoliageStaticMesh(FoliageMeshDeprecated->Component));
		FoliageMesh->FoliageTypeUpdateGuid = FoliageMeshDeprecated->FoliageTypeUpdateGuid;

		FoliageMesh->Instances.Reserve(FoliageMeshDeprecated->Instances.Num());

		for (const FFoliageInstance_Deprecated& DeprecatedInstance : FoliageMeshDeprecated->Instances)
		{
			FFoliageInstance Instance;
			static_cast<FFoliageInstancePlacementInfo&>(Instance) = DeprecatedInstance;
			Instance.BaseId = IFA->InstanceBaseCache.AddInstanceBaseId(DeprecatedInstance.Base);
			Instance.ProceduralGuid = DeprecatedInstance.ProceduralGuid;

			FoliageMesh->Instances.Add(Instance);
		}
	}

	// there were no cross-level references before
	check(IFA->InstanceBaseCache.InstanceBaseLevelMap.Num() <= 1);
	// populate WorldAsset->BasePtr map
	IFA->InstanceBaseCache.InstanceBaseLevelMap.Empty();
	auto& BaseList = IFA->InstanceBaseCache.InstanceBaseLevelMap.Add(TSoftObjectPtr<UWorld>(Cast<UWorld>(IFA->GetLevel()->GetOuter())));
	for (auto& BaseInfoPair : IFA->InstanceBaseCache.InstanceBaseMap)
	{
		BaseList.Add(BaseInfoPair.Value.BasePtr);
	}
#endif//WITH_EDITORONLY_DATA	
}

static void ConvertDeprecated2FoliageMeshes(
	AInstancedFoliageActor* IFA,
	const TMap<UFoliageType*, TUniqueObj<FFoliageMeshInfo_Deprecated2>>& FoliageMeshesDeprecated,
	TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos)
{
#if WITH_EDITORONLY_DATA	
	for (auto Pair : FoliageMeshesDeprecated)
	{
		auto& FoliageMesh = FoliageInfos.Add(Pair.Key);
		const auto& FoliageMeshDeprecated = Pair.Value;

		// Old Foliage mesh is always static mesh (no actors)
		FoliageMesh->Type = EFoliageImplType::StaticMesh;
		FoliageMesh->Implementation.Reset(new FFoliageStaticMesh(FoliageMeshDeprecated->Component));
		FoliageMesh->FoliageTypeUpdateGuid = FoliageMeshDeprecated->FoliageTypeUpdateGuid;

		FoliageMesh->Instances.Reserve(FoliageMeshDeprecated->Instances.Num());

		for (const FFoliageInstance& Instance : FoliageMeshDeprecated->Instances)
		{
			FoliageMesh->Instances.Add(Instance);
		}
	}
#endif//WITH_EDITORONLY_DATA	
}

/**
*	FFoliageInstanceCluster_Deprecated
*/
struct FFoliageInstanceCluster_Deprecated
{
	UInstancedStaticMeshComponent* ClusterComponent;
	FBoxSphereBounds Bounds;

#if WITH_EDITORONLY_DATA
	TArray<int32> InstanceIndices;	// index into editor editor Instances array
#endif

	friend FArchive& operator<<(FArchive& Ar, FFoliageInstanceCluster_Deprecated& OldCluster)
	{
		check(Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageUsingHierarchicalISMC);

		Ar << OldCluster.Bounds;
		Ar << OldCluster.ClusterComponent;

#if WITH_EDITORONLY_DATA
		if (!Ar.ArIsFilterEditorOnly ||
			Ar.UE4Ver() < VER_UE4_FOLIAGE_SETTINGS_TYPE)
		{
			Ar << OldCluster.InstanceIndices;
		}
#endif

		return Ar;
	}
};

FArchive& operator<<(FArchive& Ar, FFoliageMeshInfo_Deprecated& MeshInfo)
{
	if (Ar.CustomVer(FFoliageCustomVersion::GUID) >= FFoliageCustomVersion::FoliageUsingHierarchicalISMC)
	{
		Ar << MeshInfo.Component;
	}
	else
	{
		TArray<FFoliageInstanceCluster_Deprecated> OldInstanceClusters;
		Ar << OldInstanceClusters;
	}

#if WITH_EDITORONLY_DATA
	if ((!Ar.ArIsFilterEditorOnly || Ar.UE4Ver() < VER_UE4_FOLIAGE_SETTINGS_TYPE) &&
		(!(Ar.GetPortFlags() & PPF_DuplicateForPIE)))
	{
		Ar << MeshInfo.Instances;
	}

	if (!Ar.ArIsFilterEditorOnly && Ar.CustomVer(FFoliageCustomVersion::GUID) >= FFoliageCustomVersion::AddedFoliageTypeUpdateGuid)
	{
		Ar << MeshInfo.FoliageTypeUpdateGuid;
	}
#endif

	return Ar;
}

FFoliageMeshInfo_Deprecated2::FFoliageMeshInfo_Deprecated2()
	: Component(nullptr)
{ }

FArchive& operator<<(FArchive& Ar, FFoliageMeshInfo_Deprecated2& MeshInfo)
{
	Ar << MeshInfo.Component;

#if WITH_EDITORONLY_DATA
	Ar << MeshInfo.Instances;
	Ar << MeshInfo.FoliageTypeUpdateGuid;
#endif

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FFoliageInfo& Info)
{
	Ar << Info.Type;
	if (Ar.IsLoading() || (Ar.IsTransacting() && !Info.Implementation.IsValid()))
	{
		Info.CreateImplementation(Info.Type);
	}
	
	if (Info.Implementation)
	{
		Info.Implementation->Serialize(Ar);
	}

#if WITH_EDITORONLY_DATA
	if (!Ar.ArIsFilterEditorOnly && !(Ar.GetPortFlags() & PPF_DuplicateForPIE))
	{
		if (Ar.IsTransacting())
		{
			Info.Instances.BulkSerialize(Ar);
		}
		else
		{
			Ar << Info.Instances;
		}
	}

	if (!Ar.ArIsFilterEditorOnly)
	{
		Ar << Info.FoliageTypeUpdateGuid;
	}

	// Serialize the transient data for undo.
	if (Ar.IsTransacting())
	{
		Ar << Info.ComponentHash;
		Ar << Info.SelectedIndices;
	}
#endif

	return Ar;
}

//
// UFoliageType
//

UFoliageType::UFoliageType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Density = 100.0f;
	Radius = 0.0f;
	AlignToNormal = true;
	RandomYaw = true;
	Scaling = EFoliageScaling::Uniform;
	ScaleX.Min = 1.0f;
	ScaleY.Min = 1.0f;
	ScaleZ.Min = 1.0f;
	ScaleX.Max = 1.0f;
	ScaleY.Max = 1.0f;
	ScaleZ.Max = 1.0f;
	AlignMaxAngle = 0.0f;
	RandomPitchAngle = 0.0f;
	GroundSlopeAngle.Min = 0.0f;
	GroundSlopeAngle.Max = 45.0f;
	Height.Min = -262144.0f;
	Height.Max = 262144.0f;
	ZOffset.Min = 0.0f;
	ZOffset.Max = 0.0f;
	CullDistance.Min = 0;
	CullDistance.Max = 0;
	bEnableStaticLighting_DEPRECATED = true;
	MinimumLayerWeight = 0.5f;
#if WITH_EDITORONLY_DATA
	IsSelected = false;
#endif
	DensityAdjustmentFactor = 1.0f;
	CollisionWithWorld = false;
	CollisionScale = FVector(0.9f, 0.9f, 0.9f);

	Mobility = EComponentMobility::Static;
	CastShadow = true;
	bCastDynamicShadow = true;
	bCastStaticShadow = true;
	bAffectDynamicIndirectLighting = false;
	// Most of the high instance count foliage like grass causes performance problems with distance field lighting
	bAffectDistanceFieldLighting = false;
	bCastShadowAsTwoSided = false;
	bReceivesDecals = false;

	TranslucencySortPriority = 0;

	bOverrideLightMapRes = false;
	OverriddenLightMapRes = 8;
	bUseAsOccluder = false;

	BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	/** Ecosystem settings*/
	AverageSpreadDistance = 50;
	SpreadVariance = 150;
	bCanGrowInShade = false;
	bSpawnsInShade = false;
	SeedsPerStep = 3;
	OverlapPriority = 0.f;
	NumSteps = 3;
	ProceduralScale = FFloatInterval(1.f, 3.f);
	ChangeCount = 0;
	InitialSeedDensity = 1.f;
	CollisionRadius = 100.f;
	ShadeRadius = 100.f;
	MaxInitialAge = 0.f;
	MaxAge = 10.f;

	FRichCurve* Curve = ScaleCurve.GetRichCurve();
	Curve->AddKey(0.f, 0.f);
	Curve->AddKey(1.f, 1.f);

	UpdateGuid = FGuid::NewGuid();
#if WITH_EDITORONLY_DATA
	HiddenEditorViews = 0;
#endif
	bEnableDensityScaling = false;

#if WITH_EDITORONLY_DATA
	// Deprecated since FFoliageCustomVersion::FoliageTypeCustomization
	ScaleMinX_DEPRECATED = 1.0f;
	ScaleMinY_DEPRECATED = 1.0f;
	ScaleMinZ_DEPRECATED = 1.0f;
	ScaleMaxX_DEPRECATED = 1.0f;
	ScaleMaxY_DEPRECATED = 1.0f;
	ScaleMaxZ_DEPRECATED = 1.0f;
	HeightMin_DEPRECATED = -262144.0f;
	HeightMax_DEPRECATED = 262144.0f;
	ZOffsetMin_DEPRECATED = 0.0f;
	ZOffsetMax_DEPRECATED = 0.0f;
	UniformScale_DEPRECATED = true;
	GroundSlope_DEPRECATED = 45.0f;

	// Deprecated since FFoliageCustomVersion::FoliageTypeProceduralScaleAndShade
	MinScale_DEPRECATED = 1.f;
	MaxScale_DEPRECATED = 3.f;

#endif// WITH_EDITORONLY_DATA
}

void UFoliageType::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFoliageCustomVersion::GUID);

	// we now have mask configurations for every color channel
	if (Ar.IsLoading() && Ar.IsPersistent() && !Ar.HasAnyPortFlags(PPF_Duplicate | PPF_DuplicateForPIE) && VertexColorMask_DEPRECATED != FOLIAGEVERTEXCOLORMASK_Disabled)
	{
		FFoliageVertexColorChannelMask* Mask = nullptr;
		switch (VertexColorMask_DEPRECATED)
		{
		case FOLIAGEVERTEXCOLORMASK_Red:
			Mask = &VertexColorMaskByChannel[(uint8)EVertexColorMaskChannel::Red];
			break;

		case FOLIAGEVERTEXCOLORMASK_Green:
			Mask = &VertexColorMaskByChannel[(uint8)EVertexColorMaskChannel::Green];
			break;

		case FOLIAGEVERTEXCOLORMASK_Blue:
			Mask = &VertexColorMaskByChannel[(uint8)EVertexColorMaskChannel::Blue];
			break;

		case FOLIAGEVERTEXCOLORMASK_Alpha:
			Mask = &VertexColorMaskByChannel[(uint8)EVertexColorMaskChannel::Alpha];
			break;
		}

		if (Mask != nullptr)
		{
			Mask->UseMask = true;
			Mask->MaskThreshold = VertexColorMaskThreshold_DEPRECATED;
			Mask->InvertMask = VertexColorMaskInvert_DEPRECATED;

			VertexColorMask_DEPRECATED = FOLIAGEVERTEXCOLORMASK_Disabled;
		}
	}

	if (LandscapeLayer_DEPRECATED != NAME_None && LandscapeLayers.Num() == 0)	//we now store an array of names so initialize the array with the old name
	{
		LandscapeLayers.Add(LandscapeLayer_DEPRECATED);
		LandscapeLayer_DEPRECATED = NAME_None;
	}

	if (Ar.IsLoading() && GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::AddedMobility)
	{
		Mobility = bEnableStaticLighting_DEPRECATED ? EComponentMobility::Static : EComponentMobility::Movable;
	}

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageTypeCustomization)
		{
			ScaleX.Min = ScaleMinX_DEPRECATED;
			ScaleX.Max = ScaleMaxX_DEPRECATED;

			ScaleY.Min = ScaleMinY_DEPRECATED;
			ScaleY.Max = ScaleMaxY_DEPRECATED;

			ScaleZ.Min = ScaleMinZ_DEPRECATED;
			ScaleZ.Max = ScaleMaxZ_DEPRECATED;

			Height.Min = HeightMin_DEPRECATED;
			Height.Max = HeightMax_DEPRECATED;

			ZOffset.Min = ZOffsetMin_DEPRECATED;
			ZOffset.Max = ZOffsetMax_DEPRECATED;

			CullDistance.Min = StartCullDistance_DEPRECATED;
			CullDistance.Max = EndCullDistance_DEPRECATED;
		}

		if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageTypeCustomizationScaling)
		{
			Scaling = UniformScale_DEPRECATED ? EFoliageScaling::Uniform : EFoliageScaling::Free;

			GroundSlopeAngle.Min = MinGroundSlope_DEPRECATED;
			GroundSlopeAngle.Max = GroundSlope_DEPRECATED;
		}

		if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageTypeProceduralScaleAndShade)
		{
			bCanGrowInShade = bSpawnsInShade;

			ProceduralScale.Min = MinScale_DEPRECATED;
			ProceduralScale.Max = MaxScale_DEPRECATED;
		}
	}
#endif// WITH_EDITORONLY_DATA
}

void UFoliageType::PostLoad()
{
	Super::PostLoad();

	if (!IsTemplate())
	{
		BodyInstance.FixupData(this);
	}
}

bool UFoliageType::IsNotAssetOrBlueprint() const
{
	return IsAsset() == false && Cast<UBlueprint>(GetClass()->ClassGeneratedBy) == nullptr;
}

FVector UFoliageType::GetRandomScale() const
{
	FVector Result(1.0f);
	float LockRand = 0.0f;

	switch (Scaling)
	{
	case EFoliageScaling::Uniform:
		Result.X = ScaleX.Interpolate(FMath::FRand());
		Result.Y = Result.X;
		Result.Z = Result.X;
		break;

	case EFoliageScaling::Free:
		Result.X = ScaleX.Interpolate(FMath::FRand());
		Result.Y = ScaleY.Interpolate(FMath::FRand());
		Result.Z = ScaleZ.Interpolate(FMath::FRand());
		break;

	case EFoliageScaling::LockXY:
		LockRand = FMath::FRand();
		Result.X = ScaleX.Interpolate(LockRand);
		Result.Y = ScaleY.Interpolate(LockRand);
		Result.Z = ScaleZ.Interpolate(FMath::FRand());
		break;

	case EFoliageScaling::LockXZ:
		LockRand = FMath::FRand();
		Result.X = ScaleX.Interpolate(LockRand);
		Result.Y = ScaleY.Interpolate(FMath::FRand());
		Result.Z = ScaleZ.Interpolate(LockRand);

	case EFoliageScaling::LockYZ:
		LockRand = FMath::FRand();
		Result.X = ScaleX.Interpolate(FMath::FRand());
		Result.Y = ScaleY.Interpolate(LockRand);
		Result.Z = ScaleZ.Interpolate(LockRand);
	}

	return Result;
}

UFoliageType_InstancedStaticMesh::UFoliageType_InstancedStaticMesh(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Mesh = nullptr;
	ComponentClass = UFoliageInstancedStaticMeshComponent::StaticClass();
	CustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;
}

UObject* UFoliageType_InstancedStaticMesh::GetSource() const
{
	return Cast<UObject>(GetStaticMesh());
}

#if WITH_EDITOR
void UFoliageType_InstancedStaticMesh::SetSource(UObject* InSource)
{
	UStaticMesh* InMesh = Cast<UStaticMesh>(InSource);
	check(InSource == nullptr || InMesh != nullptr);
	SetStaticMesh(InMesh);
}

void UFoliageType_InstancedStaticMesh::UpdateBounds()
{
	if (Mesh == nullptr)
	{
		return;
	}

	MeshBounds = Mesh->GetBounds();

	// Make bottom only bound
	FBox LowBound = MeshBounds.GetBox();
	LowBound.Max.Z = LowBound.Min.Z + (LowBound.Max.Z - LowBound.Min.Z) * 0.1f;

	float MinX = FLT_MAX, MaxX = FLT_MIN, MinY = FLT_MAX, MaxY = FLT_MIN;
	LowBoundOriginRadius = FVector::ZeroVector;

	if (Mesh->RenderData)
	{
		FPositionVertexBuffer& PositionVertexBuffer = Mesh->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
		for (uint32 Index = 0; Index < PositionVertexBuffer.GetNumVertices(); ++Index)
		{
			const FVector& Pos = PositionVertexBuffer.VertexPosition(Index);
			if (Pos.Z < LowBound.Max.Z)
			{
				MinX = FMath::Min(MinX, Pos.X);
				MinY = FMath::Min(MinY, Pos.Y);
				MaxX = FMath::Max(MaxX, Pos.X);
				MaxY = FMath::Max(MaxY, Pos.Y);
			}
		}
	}

	LowBoundOriginRadius = FVector((MinX + MaxX), (MinY + MaxY), FMath::Sqrt(FMath::Square(MaxX - MinX) + FMath::Square(MaxY - MinY))) * 0.5f;
}
#endif

UFoliageType_Actor::UFoliageType_Actor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Density = 10;
	Radius = 500;
}

#if WITH_EDITOR
void UFoliageType_Actor::UpdateBounds()
{
	if (ActorClass == nullptr)
	{
		return;
	}

	FPreviewScene PreviewScene;
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnInfo.bNoFail = true;
	SpawnInfo.ObjectFlags = RF_Transient;
	AActor* PreviewActor = PreviewScene.GetWorld()->SpawnActor<AActor>(ActorClass, SpawnInfo);
	if (PreviewActor == nullptr)
	{
		return;
	}
	
	PreviewActor->SetActorEnableCollision(false);
	MeshBounds = FBoxSphereBounds(ForceInitToZero);

	// Put this in method...
	if (PreviewActor != nullptr && PreviewActor->GetRootComponent())
	{
		TArray<USceneComponent*> PreviewComponents;
		PreviewActor->GetRootComponent()->GetChildrenComponents(true, PreviewComponents);
		PreviewComponents.Add(PreviewActor->GetRootComponent());

		for (USceneComponent* PreviewComponent : PreviewComponents)
		{
			MeshBounds = MeshBounds + PreviewComponent->Bounds;
		}
	}

	FBox LowBound = MeshBounds.GetBox();
	LowBound.Max.Z = LowBound.Min.Z + (LowBound.Max.Z - LowBound.Min.Z) * 0.1f;

	float MinX = LowBound.Min.X, MaxX = LowBound.Max.X, MinY = LowBound.Min.Y, MaxY = LowBound.Max.Y;
	LowBoundOriginRadius = FVector::ZeroVector;

	// TODO: Get more precise lower bound from multiple possible meshes in Actor

	LowBoundOriginRadius = FVector((MinX + MaxX), (MinY + MaxY), FMath::Sqrt(FMath::Square(MaxX - MinX) + FMath::Square(MaxY - MinY))) * 0.5f;

	PreviewActor->Destroy();
}
#endif

float UFoliageType::GetMaxRadius() const
{
	return FMath::Max(CollisionRadius, ShadeRadius);
}

float UFoliageType::GetScaleForAge(const float Age) const
{
	const FRichCurve* Curve = ScaleCurve.GetRichCurveConst();
	const float Time = FMath::Clamp(MaxAge == 0 ? 1.f : Age / MaxAge, 0.f, 1.f);
	const float Scale = Curve->Eval(Time);
	return ProceduralScale.Min + ProceduralScale.Size() * Scale;
}

float UFoliageType::GetInitAge(FRandomStream& RandomStream) const
{
	return RandomStream.FRandRange(0, MaxInitialAge);
}

float UFoliageType::GetNextAge(const float CurrentAge, const int32 InNumSteps) const
{
	float NewAge = CurrentAge;
	for (int32 Count = 0; Count < InNumSteps; ++Count)
	{
		const float GrowAge = NewAge + 1;
		if (GrowAge <= MaxAge)
		{
			NewAge = GrowAge;
		}
		else
		{
			break;
		}
	}

	return NewAge;
}

bool UFoliageType::GetSpawnsInShade() const
{
	return bCanGrowInShade && bSpawnsInShade;
}

#if WITH_EDITOR
void UFoliageType::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ensure that OverriddenLightMapRes is a factor of 4
	OverriddenLightMapRes = OverriddenLightMapRes > 4 ? OverriddenLightMapRes + 3 & ~3 : 4;
	++ChangeCount;

	UpdateGuid = FGuid::NewGuid();

	const bool bSourceChanged = IsSourcePropertyChange(PropertyChangedEvent.Property);
	if (bSourceChanged)
	{
		UpdateBounds();
	}

	// Notify any currently-loaded InstancedFoliageActors
	if (IsFoliageReallocationRequiredForPropertyChange(PropertyChangedEvent.Property))
	{
		for (TObjectIterator<AInstancedFoliageActor> It(RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFalgs */ EInternalObjectFlags::PendingKill); It; ++It)
		{
			if (It->GetWorld() != nullptr)
			{
				It->NotifyFoliageTypeChanged(this, bSourceChanged);
			}
		}
	}
}

void UFoliageType::PreEditChange(UProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	if (IsSourcePropertyChange(PropertyAboutToChange))
	{
		for (TObjectIterator<AInstancedFoliageActor> It(RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFalgs */ EInternalObjectFlags::PendingKill); It; ++It)
		{
			It->NotifyFoliageTypeWillChange(this);
		}
	}
}

void UFoliageType::OnHiddenEditorViewMaskChanged(UWorld* InWorld)
{
	for (TActorIterator<AInstancedFoliageActor> It(InWorld); It; ++It)
	{
		FFoliageInfo* Info = It->FindInfo(this);
		if (Info != nullptr)
		{
			Info->OnHiddenEditorViewMaskChanged(HiddenEditorViews);
		}
	}
}

FName UFoliageType::GetDisplayFName() const
{
	FName DisplayFName;

	if (IsAsset())
	{
		DisplayFName = GetFName();
	}
	else if (UBlueprint* FoliageTypeBP = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
	{
		DisplayFName = FoliageTypeBP->GetFName();
	}
	else if (UObject* Source = GetSource())
	{
		DisplayFName = Source->GetFName();
	}

	return DisplayFName;
}

#endif

//
// FFoliageStaticMesh
//
void FFoliageStaticMesh::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	if (Component != nullptr)
	{
		Collector.AddReferencedObject(Component, InThis);
	}
}

void FFoliageStaticMesh::Serialize(FArchive& Ar)
{
	Ar << Component;
}


int32 FFoliageStaticMesh::GetOverlappingSphereCount(const FSphere& Sphere) const
{
	if (Component && Component->IsTreeFullyBuilt())
	{
		return Component->GetOverlappingSphereCount(Sphere);
	}
	return 0;
}

int32 FFoliageStaticMesh::GetOverlappingBoxCount(const FBox& Box) const
{
	if (Component && Component->IsTreeFullyBuilt())
	{
		return Component->GetOverlappingBoxCount(Box);
	}
	return 0;
}
void FFoliageStaticMesh::GetOverlappingBoxTransforms(const FBox& Box, TArray<FTransform>& OutTransforms) const
{
	if (Component && Component->IsTreeFullyBuilt())
	{
		Component->GetOverlappingBoxTransforms(Box, OutTransforms);
	}
}
void FFoliageStaticMesh::GetOverlappingMeshCount(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const
{
	int32 Count = GetOverlappingSphereCount(Sphere);
	if (Count > 0)
	{
		UStaticMesh* const Mesh = Component->GetStaticMesh();
		int32& StoredCount = OutCounts.FindOrAdd(Mesh);
		StoredCount += Count;
	}
}

#if WITH_EDITOR
void FFoliageStaticMesh::Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType)
{
	CreateNewComponent(IFA, FoliageType);
}

void FFoliageStaticMesh::Uninitialize()
{
	if (Component != nullptr)
	{
		if (Component->GetStaticMesh() != nullptr)
		{
			Component->GetStaticMesh()->GetOnExtendedBoundsChanged().RemoveAll(this);
		}

		Component->ClearInstances();
		Component->SetFlags(RF_Transactional);
		Component->Modify();
		Component->DestroyComponent();
		Component = nullptr;
	}
}

int32 FFoliageStaticMesh::GetInstanceCount() const
{
	if (Component != nullptr)
	{
		return Component->GetInstanceCount();
	}

	return 0;
}

void FFoliageStaticMesh::PreAddInstances(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, int32 Count)
{
	if (!IsInitialized())
	{
		Initialize(IFA, FoliageType);
		check(IsInitialized());
	}
	else
	{
		Component->InitPerInstanceRenderData(false);
		Component->InvalidateLightingCache();
	}

	if (Count)
	{
		Component->PreAllocateInstancesMemory(Count);
	}
}

void FFoliageStaticMesh::AddInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& NewInstance)
{
	check(Component);
	Component->AddInstanceWorldSpace(NewInstance.GetInstanceWorldTransform());
}

void FFoliageStaticMesh::RemoveInstance(int32 InstanceIndex)
{
	check(Component);
	Component->RemoveInstance(InstanceIndex);

	if (UpdateDepth > 0)
	{
		bInvalidateLightingCache = true;
	}
	else
	{
		Component->InvalidateLightingCache();
	}
}

void FFoliageStaticMesh::SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport)
{
	check(Component);
	Component->UpdateInstanceTransform(InstanceIndex, Transform, true, true, bTeleport);
}

FTransform FFoliageStaticMesh::GetInstanceWorldTransform(int32 InstanceIndex) const
{
	return FTransform(Component->PerInstanceSMData[InstanceIndex].Transform) * Component->GetComponentToWorld();
}

void FFoliageStaticMesh::PostUpdateInstances()
{
	check(Component);
	Component->InvalidateLightingCache();
	Component->MarkRenderStateDirty();
}

bool FFoliageStaticMesh::IsOwnedComponent(const UPrimitiveComponent* HitComponent) const
{
	return Component == HitComponent;
}

void FFoliageStaticMesh::SelectInstances(bool bSelect, int32 InstanceIndex, int32 Count)
{
	check(Component);
	Component->SelectInstance(bSelect, InstanceIndex, Count);
	Component->MarkRenderStateDirty();
}

void FFoliageStaticMesh::ApplySelection(bool bApply, const TSet<int32>& SelectedIndices)
{
	if (Component && (bApply || Component->SelectedInstances.Num() > 0))
	{
		Component->ClearInstanceSelection();

		if (bApply)
		{
			for (int32 i : SelectedIndices)
			{
				Component->SelectInstance(true, i, 1);
			}
		}

		Component->MarkRenderStateDirty();
	}
}

void FFoliageStaticMesh::ClearSelection(const TSet<int32>& SelectedIndices)
{
	check(Component);
	Component->ClearInstanceSelection();
	Component->MarkRenderStateDirty();
}

void FFoliageStaticMesh::BeginUpdate()
{
	if (UpdateDepth == 0)
	{
		bPreviousValue = Component->bAutoRebuildTreeOnInstanceChanges;
		Component->bAutoRebuildTreeOnInstanceChanges = false;
	}
	++UpdateDepth;
}

void FFoliageStaticMesh::EndUpdate()
{
	check(UpdateDepth > 0);
	--UpdateDepth;

	if (UpdateDepth == 0)
	{
		Component->bAutoRebuildTreeOnInstanceChanges = bPreviousValue;

		if (bInvalidateLightingCache)
		{
			Component->InvalidateLightingCache();
			bInvalidateLightingCache = false;
		}
	}
}

void FFoliageStaticMesh::Refresh(AInstancedFoliageActor* IFA, const TArray<FFoliageInstance>& Instances, bool Async, bool Force)
{
	if (Component != nullptr)
	{
		Component->BuildTreeIfOutdated(Async, Force);
	}
}

void FFoliageStaticMesh::OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews)
{
	UFoliageInstancedStaticMeshComponent* FoliageComponent = Cast<UFoliageInstancedStaticMeshComponent>(Component);

	if (FoliageComponent && FoliageComponent->FoliageHiddenEditorViews != InHiddenEditorViews)
	{
		FoliageComponent->FoliageHiddenEditorViews = InHiddenEditorViews;
		FoliageComponent->MarkRenderStateDirty();
	}
}

void FFoliageStaticMesh::PreEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType)
{
	if (UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType))
	{
		if (FoliageType_InstancedStaticMesh->GetStaticMesh() != nullptr)
		{
			FoliageType_InstancedStaticMesh->GetStaticMesh()->GetOnExtendedBoundsChanged().RemoveAll(this);
		}
	}
}

void FFoliageStaticMesh::PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices)
{
	if (UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType))
	{
		if (Component != nullptr && FoliageType_InstancedStaticMesh->GetStaticMesh() != nullptr)
		{
			FoliageType_InstancedStaticMesh->GetStaticMesh()->GetOnExtendedBoundsChanged().AddRaw(this, &FFoliageStaticMesh::HandleComponentMeshBoundsChanged);
		}

		CheckComponentClass(IFA, FoliageType_InstancedStaticMesh, Instances, SelectedIndices);
		ReapplyInstancesToComponent(Instances, SelectedIndices);
	}
}

void FFoliageStaticMesh::NotifyFoliageTypeWillChange(AInstancedFoliageActor* IFA, UFoliageType* FoliageType)
{
	if (Component != nullptr)
	{
		if (UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType))
		{
			if (FoliageType_InstancedStaticMesh->GetStaticMesh() != nullptr)
			{
				FoliageType_InstancedStaticMesh->GetStaticMesh()->GetOnExtendedBoundsChanged().RemoveAll(this);
			}
		}
	}
}

void FFoliageStaticMesh::NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices, bool bSourceChanged)
{
	UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
	check(FoliageType_InstancedStaticMesh);
	CheckComponentClass(IFA, FoliageType_InstancedStaticMesh, Instances, SelectedIndices);
	UpdateComponentSettings(FoliageType_InstancedStaticMesh);
	
	if (bSourceChanged && Component != nullptr && Component->GetStaticMesh() != nullptr)
	{
		// Change bounds delegate bindings
		if (FoliageType_InstancedStaticMesh->GetStaticMesh() != nullptr)
		{
			Component->GetStaticMesh()->GetOnExtendedBoundsChanged().AddRaw(this, &FFoliageStaticMesh::HandleComponentMeshBoundsChanged);

			// Mesh changed, so we must update the occlusion tree
			Component->BuildTreeIfOutdated(true, false);
		}
	}
}

void FFoliageStaticMesh::EnterEditMode()
{
	if (Component == nullptr)
	{
		return;
	}

	if (Component->GetStaticMesh() != nullptr)
	{
		Component->GetStaticMesh()->GetOnExtendedBoundsChanged().AddRaw(this, &FFoliageStaticMesh::HandleComponentMeshBoundsChanged);

		Component->BuildTreeIfOutdated(true, false);
	}

	Component->bCanEnableDensityScaling = false;
	Component->UpdateDensityScaling();
}

void FFoliageStaticMesh::ExitEditMode()
{
	if (Component == nullptr)
	{
		return;
	}

	if (Component->GetStaticMesh() != nullptr)
	{
		Component->GetStaticMesh()->GetOnExtendedBoundsChanged().RemoveAll(this);
	}

	Component->bCanEnableDensityScaling = true;
	Component->UpdateDensityScaling();
}

void FFoliageStaticMesh::CreateNewComponent(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageCreateComponent);

	check(!Component);
	const UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(InSettings);

	UClass* ComponentClass = FoliageType_InstancedStaticMesh->GetComponentClass();
	if (ComponentClass == nullptr)
	{
		ComponentClass = UFoliageInstancedStaticMeshComponent::StaticClass();
	}

	UFoliageInstancedStaticMeshComponent* FoliageComponent = NewObject<UFoliageInstancedStaticMeshComponent>(InIFA, ComponentClass, NAME_None, RF_Transactional);
	
	check(FoliageType_InstancedStaticMesh);

	Component = FoliageComponent;
	Component->SetStaticMesh(FoliageType_InstancedStaticMesh->GetStaticMesh());
	Component->bSelectable = true;
	Component->bHasPerInstanceHitProxies = true;

	if (Component->GetStaticMesh() != nullptr)
	{
		Component->GetStaticMesh()->GetOnExtendedBoundsChanged().AddRaw(this, &FFoliageStaticMesh::HandleComponentMeshBoundsChanged);
	}

	FoliageComponent->FoliageHiddenEditorViews = InSettings->HiddenEditorViews;

	UpdateComponentSettings(FoliageType_InstancedStaticMesh);

	Component->SetupAttachment(InIFA->GetRootComponent());

	if (InIFA->GetRootComponent()->IsRegistered())
	{
		Component->RegisterComponent();
	}

	// Use only instance translation as a component transform
	Component->SetWorldTransform(InIFA->GetRootComponent()->GetComponentTransform());

	// Add the new component to the transaction buffer so it will get destroyed on undo
	Component->Modify();
	// We don't want to track changes to instances later so we mark it as non-transactional
	Component->ClearFlags(RF_Transactional);
}

void FFoliageStaticMesh::HandleComponentMeshBoundsChanged(const FBoxSphereBounds& NewBounds)
{
	if (Component != nullptr)
	{
		Component->BuildTreeIfOutdated(true, false);
	}
}

void FFoliageStaticMesh::CheckComponentClass(AInstancedFoliageActor* InIFA, const UFoliageType_InstancedStaticMesh* InSettings, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices)
{
	if (Component)
	{
		UClass* ComponentClass = InSettings->GetComponentClass();
		if (ComponentClass == nullptr)
		{
			ComponentClass = UFoliageInstancedStaticMeshComponent::StaticClass();
		}

		if (ComponentClass != Component->GetClass())
		{
			InIFA->Modify();

			// prepare to destroy the old component
			Uninitialize();

			// create a new component
			Initialize(InIFA, InSettings);

			// apply the instances to it
			ReapplyInstancesToComponent(Instances, SelectedIndices);
		}
	}
}

void FFoliageStaticMesh::UpdateComponentSettings(const UFoliageType_InstancedStaticMesh* InSettings)
{
	if (Component)
	{
		bool bNeedsMarkRenderStateDirty = false;
		bool bNeedsInvalidateLightingCache = false;

		const UFoliageType_InstancedStaticMesh* FoliageType = InSettings;
		if (InSettings->GetClass()->ClassGeneratedBy)
		{
			// If we're updating settings for a BP foliage type, use the CDO
			FoliageType = InSettings->GetClass()->GetDefaultObject<UFoliageType_InstancedStaticMesh>();
		}

		if (Component->GetStaticMesh() != FoliageType->GetStaticMesh())
		{
			Component->SetStaticMesh(FoliageType->GetStaticMesh());

			bNeedsInvalidateLightingCache = true;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->Mobility != FoliageType->Mobility)
		{
			Component->SetMobility(FoliageType->Mobility);
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->InstanceStartCullDistance != FoliageType->CullDistance.Min)
		{
			Component->InstanceStartCullDistance = FoliageType->CullDistance.Min;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->InstanceEndCullDistance != FoliageType->CullDistance.Max)
		{
			Component->InstanceEndCullDistance = FoliageType->CullDistance.Max;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->CastShadow != FoliageType->CastShadow)
		{
			Component->CastShadow = FoliageType->CastShadow;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastDynamicShadow != FoliageType->bCastDynamicShadow)
		{
			Component->bCastDynamicShadow = FoliageType->bCastDynamicShadow;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastStaticShadow != FoliageType->bCastStaticShadow)
		{
			Component->bCastStaticShadow = FoliageType->bCastStaticShadow;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->RuntimeVirtualTextures != FoliageType->RuntimeVirtualTextures)
		{
			Component->RuntimeVirtualTextures = FoliageType->RuntimeVirtualTextures;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->VirtualTextureRenderPassType != FoliageType->VirtualTextureRenderPassType)
		{
			Component->VirtualTextureRenderPassType = FoliageType->VirtualTextureRenderPassType;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->VirtualTextureCullMips != FoliageType->VirtualTextureCullMips)
		{
			Component->VirtualTextureCullMips = FoliageType->VirtualTextureCullMips;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->TranslucencySortPriority != FoliageType->TranslucencySortPriority)
		{
			Component->TranslucencySortPriority = FoliageType->TranslucencySortPriority;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->bAffectDynamicIndirectLighting != FoliageType->bAffectDynamicIndirectLighting)
		{
			Component->bAffectDynamicIndirectLighting = FoliageType->bAffectDynamicIndirectLighting;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bAffectDistanceFieldLighting != FoliageType->bAffectDistanceFieldLighting)
		{
			Component->bAffectDistanceFieldLighting = FoliageType->bAffectDistanceFieldLighting;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastShadowAsTwoSided != FoliageType->bCastShadowAsTwoSided)
		{
			Component->bCastShadowAsTwoSided = FoliageType->bCastShadowAsTwoSided;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bReceivesDecals != FoliageType->bReceivesDecals)
		{
			Component->bReceivesDecals = FoliageType->bReceivesDecals;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bOverrideLightMapRes != FoliageType->bOverrideLightMapRes)
		{
			Component->bOverrideLightMapRes = FoliageType->bOverrideLightMapRes;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->OverriddenLightMapRes != FoliageType->OverriddenLightMapRes)
		{
			Component->OverriddenLightMapRes = FoliageType->OverriddenLightMapRes;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->LightmapType != FoliageType->LightmapType)
		{
			Component->LightmapType = FoliageType->LightmapType;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bUseAsOccluder != FoliageType->bUseAsOccluder)
		{
			Component->bUseAsOccluder = FoliageType->bUseAsOccluder;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->bEnableDensityScaling != FoliageType->bEnableDensityScaling)
		{
			Component->bEnableDensityScaling = FoliageType->bEnableDensityScaling;

			Component->UpdateDensityScaling();

			bNeedsMarkRenderStateDirty = true;
		}

		if (GetLightingChannelMaskForStruct(Component->LightingChannels) != GetLightingChannelMaskForStruct(FoliageType->LightingChannels))
		{
			Component->LightingChannels = FoliageType->LightingChannels;
			bNeedsMarkRenderStateDirty = true;
		}

		UFoliageInstancedStaticMeshComponent* FoliageComponent = Cast<UFoliageInstancedStaticMeshComponent>(Component);

		if (FoliageComponent && FoliageComponent->FoliageHiddenEditorViews != InSettings->HiddenEditorViews)
		{
			FoliageComponent->FoliageHiddenEditorViews = InSettings->HiddenEditorViews;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->bRenderCustomDepth != FoliageType->bRenderCustomDepth)
		{
			Component->bRenderCustomDepth = FoliageType->bRenderCustomDepth;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->CustomDepthStencilValue != FoliageType->CustomDepthStencilValue)
		{
			Component->CustomDepthStencilValue = FoliageType->CustomDepthStencilValue;
			bNeedsMarkRenderStateDirty = true;
		}

		const UFoliageType_InstancedStaticMesh* FoliageType_ISM = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		if (FoliageType_ISM)
		{
			// Check override materials
			if (Component->OverrideMaterials.Num() != FoliageType_ISM->OverrideMaterials.Num())
			{
				Component->OverrideMaterials = FoliageType_ISM->OverrideMaterials;
				bNeedsMarkRenderStateDirty = true;
				bNeedsInvalidateLightingCache = true;
			}
			else
			{
				for (int32 Index = 0; Index < FoliageType_ISM->OverrideMaterials.Num(); Index++)
				{
					if (Component->OverrideMaterials[Index] != FoliageType_ISM->OverrideMaterials[Index])
					{
						Component->OverrideMaterials = FoliageType_ISM->OverrideMaterials;
						bNeedsMarkRenderStateDirty = true;
						bNeedsInvalidateLightingCache = true;
						break;
					}
				}
			}
		}

		Component->BodyInstance.CopyBodyInstancePropertiesFrom(&FoliageType->BodyInstance);

		Component->SetCustomNavigableGeometry(FoliageType->CustomNavigableGeometry);

		if (bNeedsInvalidateLightingCache)
		{
			Component->InvalidateLightingCache();
		}

		if (bNeedsMarkRenderStateDirty)
		{
			Component->MarkRenderStateDirty();
		}
	}
}

void FFoliageStaticMesh::ReapplyInstancesToComponent(const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices)
{
	if (Component)
	{
		// clear the transactional flag if it was set prior to deleting the actor
		Component->ClearFlags(RF_Transactional);

		const bool bWasRegistered = Component->IsRegistered();
		Component->UnregisterComponent();
		Component->ClearInstances();
		Component->InitPerInstanceRenderData(false);

		Component->bAutoRebuildTreeOnInstanceChanges = false;

		for (auto& Instance : Instances)
		{
			Component->AddInstanceWorldSpace(Instance.GetInstanceWorldTransform());
		}

		Component->bAutoRebuildTreeOnInstanceChanges = true;
		Component->BuildTreeIfOutdated(true, true);

		Component->ClearInstanceSelection();

		if (SelectedIndices.Num())
		{
			for (int32 i : SelectedIndices)
			{
				Component->SelectInstance(true, i, 1);
			}
		}

		if (bWasRegistered)
		{
			Component->RegisterComponent();
		}
	}
}

#endif // WITH_EDITOR

//
// FFoliageInfo
//
FFoliageInfo::FFoliageInfo()
	: Type(EFoliageImplType::StaticMesh)
#if WITH_EDITOR
	, InstanceHash(GIsEditor ? new FFoliageInstanceHash() : nullptr)
#endif
{ }

FFoliageInfo::~FFoliageInfo()
{ }

UHierarchicalInstancedStaticMeshComponent* FFoliageInfo::GetComponent() const
{
	if (Type == EFoliageImplType::StaticMesh && Implementation.IsValid())
	{
		FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Implementation.Get());
		return FoliageStaticMesh->Component;
	}

	return nullptr;
}

void FFoliageInfo::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	if (Implementation.IsValid())
	{
		Implementation->AddReferencedObjects(InThis, Collector);
	}
}

void FFoliageInfo::CreateImplementation(EFoliageImplType InType)
{
	check(InType != EFoliageImplType::Unknown);
	check(!Implementation.IsValid());
	// Change Impl based on InType param
	Type = InType;
	
	if (Type == EFoliageImplType::StaticMesh)
	{
		Implementation.Reset(new FFoliageStaticMesh(nullptr));
	}
	else if (Type == EFoliageImplType::Actor)
	{
		Implementation.Reset(new FFoliageActor());
	}
}

int32 FFoliageInfo::GetOverlappingSphereCount(const FSphere& Sphere) const
{
	if (Implementation.IsValid())
	{
		return Implementation->GetOverlappingSphereCount(Sphere);
	}

	return 0;
}

int32 FFoliageInfo::GetOverlappingBoxCount(const FBox& Box) const
{
	if (Implementation.IsValid())
	{
		return Implementation->GetOverlappingBoxCount(Box);
	}

	return 0;
}

void FFoliageInfo::GetOverlappingBoxTransforms(const FBox& Box, TArray<FTransform>& OutTransforms) const
{
	if (Implementation.IsValid())
	{
		Implementation->GetOverlappingBoxTransforms(Box, OutTransforms);
	}
}

void FFoliageInfo::GetOverlappingMeshCount(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const
{
	if (Implementation.IsValid())
	{
		Implementation->GetOverlappingMeshCount(Sphere, OutCounts);
	}
}

#if WITH_EDITOR
void FFoliageInfo::CreateImplementation(const UFoliageType* FoliageType)
{
	check(!Implementation.IsValid());
	// Change Impl based on FoliageType param
	Type = EFoliageImplType::Unknown;
	if (FoliageType->IsA<UFoliageType_InstancedStaticMesh>())
	{
		Type = EFoliageImplType::StaticMesh;
		Implementation.Reset(new FFoliageStaticMesh(nullptr));
	}
	else if (FoliageType->IsA<UFoliageType_Actor>())
	{
		Type = EFoliageImplType::Actor;
		Implementation.Reset(new FFoliageActor());
	}
	check(Type != EFoliageImplType::Unknown);
}

void FFoliageInfo::Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType)
{
	check(!IsInitialized());
	check(Implementation.IsValid());
	Implementation->Initialize(IFA, FoliageType);
}

void FFoliageInfo::Uninitialize()
{
	check(IsInitialized());
	Implementation->Uninitialize();
}

bool FFoliageInfo::IsInitialized() const
{
	return Implementation.IsValid() && Implementation->IsInitialized();
}

void FFoliageInfo::NotifyFoliageTypeWillChange(AInstancedFoliageActor* IFA, UFoliageType* FoliageType)
{
	Implementation->NotifyFoliageTypeWillChange(IFA, FoliageType);
}

void FFoliageInfo::NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, bool bSourceChanged)
{
	Implementation->NotifyFoliageTypeChanged(IFA, FoliageType, Instances, SelectedIndices, bSourceChanged);
}

void FFoliageInfo::CheckValid()
{
#if DO_FOLIAGE_CHECK
	int32 ClusterTotal = 0;
	int32 ComponentTotal = 0;

	check(Instances.Num() == Implementation->GetInstanceCount());
		
	InstanceHash->CheckInstanceCount(Instances.Num());

	int32 ComponentHashTotal = 0;
	for (const auto& Pair : ComponentHash)
	{
		ComponentHashTotal += Pair.Value.Num();
	}
	check(ComponentHashTotal == Instances.Num());

#if FOLIAGE_CHECK_TRANSFORM
	// Check transforms match up with editor data
	int32 MismatchCount = 0;
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		FTransform InstanceToWorldEd = Instances[i].GetInstanceWorldTransform();
		FTransform InstanceToWorldImpl = Implementation->GetInstanceWorldTransform(i);

		if (!InstanceToWorldEd.Equals(InstanceToWorldImpl))
		{
			MismatchCount++;
		}
	}
		
	if (MismatchCount != 0)
	{
		UE_LOG(LogInstancedFoliage, Log, TEXT("transform mismatch: %d"), MismatchCount);
	}
#endif

#endif
}

/*
*/
void FFoliageInfo::ClearSelection()
{
	if (Instances.Num() > 0)
	{
		Implementation->ClearSelection(SelectedIndices);
		SelectedIndices.Empty();
	}
}

void FFoliageInfo::SetRandomSeed(int32 seed)
{
	if (Type == EFoliageImplType::StaticMesh)
	{
		FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Implementation.Get());
		FoliageStaticMesh->Component->InstancingRandomSeed = 1;
	}
}

void FFoliageInfo::SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport)
{
	Implementation->SetInstanceWorldTransform(InstanceIndex, Transform, bTeleport);
}

void FFoliageInfo::AddInstanceImpl(AInstancedFoliageActor* InIFA, const FFoliageInstance& InNewInstance)
{
	// Add the instance taking either a free slot or adding a new item.
	int32 InstanceIndex = Instances.Add(InNewInstance);
	FFoliageInstance& AddedInstance = Instances[InstanceIndex];

	AddedInstance.BaseId = InIFA->InstanceBaseCache.AddInstanceBaseId(InNewInstance.BaseComponent);

	// Add the instance to the hash
	AddToBaseHash(InstanceIndex);
	InstanceHash->InsertInstance(AddedInstance.Location, InstanceIndex);

	// Add the instance to the component
	Implementation->AddInstance(InIFA, AddedInstance);
}

void FFoliageInfo::AddInstances(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const TSet<const FFoliageInstance*>& InNewInstances)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageAddInstance);

	InIFA->Modify();

	Implementation->PreAddInstances(InIFA, InSettings, InNewInstances.Num());

	Implementation->BeginUpdate();

	Instances.Reserve(Instances.Num() + InNewInstances.Num());
			
	for (const FFoliageInstance* Instance : InNewInstances)
	{
		AddInstanceImpl(InIFA, *Instance);
	}

	CheckValid();

	Implementation->EndUpdate();
}

void FFoliageInfo::AddInstance(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const FFoliageInstance& InNewInstance, UActorComponent* InBaseComponent)
{
	FFoliageInstance Instance = InNewInstance;
	Instance.BaseId = InIFA->InstanceBaseCache.AddInstanceBaseId(InBaseComponent);
	AddInstance(InIFA, InSettings, Instance);
}

void FFoliageInfo::AddInstance(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const FFoliageInstance& InNewInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageAddInstance);

	InIFA->Modify();

	Implementation->PreAddInstances(InIFA, InSettings, 1);

	Implementation->BeginUpdate();

	AddInstanceImpl(InIFA, InNewInstance);

	CheckValid();

	Implementation->EndUpdate();
}

void FFoliageInfo::RemoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesToRemove, bool RebuildFoliageTree)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageRemoveInstance);

	if (InInstancesToRemove.Num() <= 0)
	{
		return;
	}

	check(IsInitialized());
	InIFA->Modify();

	Implementation->BeginUpdate();

	TSet<int32> InstancesToRemove;
	InstancesToRemove.Append(InInstancesToRemove);

	while (InstancesToRemove.Num() > 0)
	{
		// Get an item from the set for processing
		auto It = InstancesToRemove.CreateConstIterator();
		int32 InstanceIndex = *It;
		int32 InstanceIndexToRemove = InstanceIndex;

		FFoliageInstance& Instance = Instances[InstanceIndex];

		// remove from hash
		RemoveFromBaseHash(InstanceIndex);
		InstanceHash->RemoveInstance(Instance.Location, InstanceIndex);

		// remove from the component
		Implementation->RemoveInstance(InstanceIndex);

		// Remove it from the selection.
		SelectedIndices.Remove(InstanceIndex);

		// remove from instances array
		Instances.RemoveAtSwap(InstanceIndex, 1, false);

		// update hashes for swapped instance
		if (InstanceIndex != Instances.Num() && Instances.Num() > 0)
		{
			// Instance hash
			FFoliageInstance& SwappedInstance = Instances[InstanceIndex];
			InstanceHash->RemoveInstance(SwappedInstance.Location, Instances.Num());
			InstanceHash->InsertInstance(SwappedInstance.Location, InstanceIndex);

			// Component hash
			auto* InstanceSet = ComponentHash.Find(SwappedInstance.BaseId);
			if (InstanceSet)
			{
				InstanceSet->Remove(Instances.Num());
				InstanceSet->Add(InstanceIndex);
			}

			// Selection
			if (SelectedIndices.Contains(Instances.Num()))
			{
				SelectedIndices.Remove(Instances.Num());
				SelectedIndices.Add(InstanceIndex);
			}

			// Removal list
			if (InstancesToRemove.Contains(Instances.Num()))
			{
				// The item from the end of the array that we swapped in to InstanceIndex is also on the list to remove.
				// Remove the item at the end of the array and leave InstanceIndex in the removal list.
				InstanceIndexToRemove = Instances.Num();
			}
		}

		// Remove the removed item from the removal list
		InstancesToRemove.Remove(InstanceIndexToRemove);
	}

	Instances.Shrink();
		
	Implementation->EndUpdate();
		
	if (RebuildFoliageTree)
	{
		Refresh(InIFA, true, true);
	}

	CheckValid();
}

void FFoliageInfo::PreMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesToMove)
{
	// Remove instances from the hash
	for (TArray<int32>::TConstIterator It(InInstancesToMove); It; ++It)
	{
		int32 InstanceIndex = *It;
		const FFoliageInstance& Instance = Instances[InstanceIndex];
		InstanceHash->RemoveInstance(Instance.Location, InstanceIndex);
	}
}


void FFoliageInfo::PostUpdateInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesUpdated, bool bReAddToHash, bool InUpdateSelection)
{
	if (InInstancesUpdated.Num())
	{
		for (TArray<int32>::TConstIterator It(InInstancesUpdated); It; ++It)
		{
			int32 InstanceIndex = *It;
			const FFoliageInstance& Instance = Instances[InstanceIndex];

			FTransform InstanceToWorld = Instance.GetInstanceWorldTransform();

			Implementation->SetInstanceWorldTransform(InstanceIndex, InstanceToWorld, true);

			// Re-add instance to the hash if requested
			if (bReAddToHash)
			{
				InstanceHash->InsertInstance(Instance.Location, InstanceIndex);
			}

			// Reselect the instance to update the render update to include selection as by default it gets removed
			if (InUpdateSelection)
			{
				Implementation->SelectInstances(true, InstanceIndex, 1);
			}
		}

		Implementation->PostUpdateInstances();
	}
}

void FFoliageInfo::PostMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved)
{
	PostUpdateInstances(InIFA, InInstancesMoved, true, true);
}

void FFoliageInfo::DuplicateInstances(AInstancedFoliageActor* InIFA, UFoliageType* InSettings, const TArray<int32>& InInstancesToDuplicate)
{
	Implementation->BeginUpdate();

	for (int32 InstanceIndex : InInstancesToDuplicate)
	{
		const FFoliageInstance TempInstance = Instances[InstanceIndex];
		AddInstance(InIFA, InSettings, TempInstance);
	}

	Implementation->EndUpdate();
	Refresh(InIFA, true, true);
}

/* Get the number of placed instances */
int32 FFoliageInfo::GetPlacedInstanceCount() const
{
	int32 PlacedInstanceCount = 0;

	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		if (!Instances[i].ProceduralGuid.IsValid())
		{
			++PlacedInstanceCount;
		}
	}

	return PlacedInstanceCount;
}

void FFoliageInfo::AddToBaseHash(int32 InstanceIndex)
{
	FFoliageInstance& Instance = Instances[InstanceIndex];
	ComponentHash.FindOrAdd(Instance.BaseId).Add(InstanceIndex);
}

void FFoliageInfo::RemoveFromBaseHash(int32 InstanceIndex)
{
	FFoliageInstance& Instance = Instances[InstanceIndex];

	// Remove current base link
	auto* InstanceSet = ComponentHash.Find(Instance.BaseId);
	if (InstanceSet)
	{
		InstanceSet->Remove(InstanceIndex);
		if (InstanceSet->Num() == 0)
		{
			// Remove the component from the component hash if this is the last instance.
			ComponentHash.Remove(Instance.BaseId);
		}
	}
}

// Destroy existing clusters and reassign all instances to new clusters
void FFoliageInfo::ReallocateClusters(AInstancedFoliageActor* InIFA, UFoliageType* InSettings)
{
	// In case Foliage Type Changed recreate implementation
	Implementation.Reset();
	CreateImplementation(InSettings);
	
	// Remove everything
	TArray<FFoliageInstance> OldInstances;
	Exchange(Instances, OldInstances);
	InstanceHash->Empty();
	ComponentHash.Empty();
	SelectedIndices.Empty();

	// Copy the UpdateGuid from the foliage type
	FoliageTypeUpdateGuid = InSettings->UpdateGuid;

	// Re-add
	for (FFoliageInstance& Instance : OldInstances)
	{
		if ((Instance.Flags & FOLIAGE_InstanceDeleted) == 0)
		{
			AddInstance(InIFA, InSettings, Instance);
		}
	}

	
	Refresh(InIFA, true, true);
}

void FFoliageInfo::GetInstancesInsideSphere(const FSphere& Sphere, TArray<int32>& OutInstances)
{
	auto TempInstances = InstanceHash->GetInstancesOverlappingBox(FBox::BuildAABB(Sphere.Center, FVector(Sphere.W)));
	for (int32 Idx : TempInstances)
	{
		if (FSphere(Instances[Idx].Location, 0.f).IsInside(Sphere))
		{
			OutInstances.Add(Idx);
		}
	}
}

void FFoliageInfo::GetInstanceAtLocation(const FVector& Location, int32& OutInstance, bool& bOutSucess)
{
	auto TempInstances = InstanceHash->GetInstancesOverlappingBox(FBox::BuildAABB(Location, FVector(KINDA_SMALL_NUMBER)));

	float ShortestDistance = MAX_FLT;
	OutInstance = -1;

	for (int32 Idx : TempInstances)
	{
		FVector InstanceLocation = Instances[Idx].Location;
		float DistanceSquared = FVector::DistSquared(InstanceLocation, Location);
		if (DistanceSquared < ShortestDistance)
		{
			ShortestDistance = DistanceSquared;
			OutInstance = Idx;
		}
	}

	bOutSucess = OutInstance != -1;
}

// Returns whether or not there is are any instances overlapping the sphere specified
bool FFoliageInfo::CheckForOverlappingSphere(const FSphere& Sphere)
{
	auto TempInstances = InstanceHash->GetInstancesOverlappingBox(FBox::BuildAABB(Sphere.Center, FVector(Sphere.W)));
	for (int32 Idx : TempInstances)
	{
		if (FSphere(Instances[Idx].Location, 0.f).IsInside(Sphere))
		{
			return true;
		}
	}
	return false;
}

// Returns whether or not there is are any instances overlapping the instance specified, excluding the set of instances provided
bool FFoliageInfo::CheckForOverlappingInstanceExcluding(int32 TestInstanceIdx, float Radius, TSet<int32>& ExcludeInstances)
{
	FSphere Sphere(Instances[TestInstanceIdx].Location, Radius);

	auto TempInstances = InstanceHash->GetInstancesOverlappingBox(FBox::BuildAABB(Sphere.Center, FVector(Sphere.W)));
	for (int32 Idx : TempInstances)
	{
		if (Idx != TestInstanceIdx && !ExcludeInstances.Contains(Idx) && FSphere(Instances[Idx].Location, 0.f).IsInside(Sphere))
		{
			return true;
		}
	}
	return false;
}

void FFoliageInfo::SelectInstances(AInstancedFoliageActor* InIFA, bool bSelect)
{
	if (Implementation->IsInitialized())
	{
		InIFA->Modify();

		if (bSelect)
		{
			SelectedIndices.Reserve(Instances.Num());

			for (int32 i = 0; i < Instances.Num(); ++i)
			{
				SelectedIndices.Add(i);
			}

			Implementation->SelectInstances(true, 0, SelectedIndices.Num());
		}
		else
		{
			Implementation->ClearSelection(SelectedIndices);
			SelectedIndices.Empty();
		}
	}
}

void FFoliageInfo::SelectInstances(AInstancedFoliageActor* InIFA, bool bSelect, TArray<int32>& InInstances)
{
	if (InInstances.Num())
	{
		check(Implementation->IsInitialized());
		if (bSelect)
		{
			InIFA->Modify();

			SelectedIndices.Reserve(InInstances.Num());

			for (int32 i : InInstances)
			{
				SelectedIndices.Add(i);
				Implementation->SelectInstances(true, i, 1);
			}
		}
		else
		{
			InIFA->Modify();

			for (int32 i : InInstances)
			{
				SelectedIndices.Remove(i);
			}

			for (int32 i : InInstances)
			{
				Implementation->SelectInstances(false, i, 1);
			}
		}
	}
}

void FFoliageInfo::Refresh(AInstancedFoliageActor* IFA, bool Async, bool Force)
{
	check(Implementation.IsValid())
	Implementation->Refresh(IFA, this->Instances, Async, Force);
}

void FFoliageInfo::OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews)
{
	Implementation->OnHiddenEditorViewMaskChanged(InHiddenEditorViews);
}

void FFoliageInfo::PreEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType)
{
	Implementation->PreEditUndo(IFA, FoliageType);
}

void FFoliageInfo::PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType)
{
	Implementation->PostEditUndo(IFA, FoliageType, Instances, SelectedIndices);
		
	// Regenerate instance hash
	// We regenerate it here instead of saving to transaction buffer to speed up modify operations
	InstanceHash->Empty();
	for (int32 InstanceIdx = 0; InstanceIdx < Instances.Num(); InstanceIdx++)
	{
		InstanceHash->InsertInstance(Instances[InstanceIdx].Location, InstanceIdx);
	}
}

void FFoliageInfo::EnterEditMode()
{
	Implementation->EnterEditMode();
}

void FFoliageInfo::ExitEditMode()
{
	Implementation->ExitEditMode();
}

TArray<int32> FFoliageInfo::GetInstancesOverlappingBox(const FBox& Box) const
{
	return InstanceHash->GetInstancesOverlappingBox(Box);
}
#endif	//WITH_EDITOR

//
// AInstancedFoliageActor
//
AInstancedFoliageActor::AInstancedFoliageActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

	SetActorEnableCollision(true);
#if WITH_EDITORONLY_DATA
	bListedInSceneOutliner = false;
#endif // WITH_EDITORONLY_DATA
	PrimaryActorTick.bCanEverTick = false;
}

bool AInstancedFoliageActor::IsOwnedByFoliage(const AActor* InActor)
{
	return InActor != nullptr && InActor->ActorHasTag(FoliageActorTag);
}

AInstancedFoliageActor* AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(UWorld* InWorld, bool bCreateIfNone)
{
	return GetInstancedFoliageActorForLevel(InWorld->GetCurrentLevel(), bCreateIfNone);
}

AInstancedFoliageActor* AInstancedFoliageActor::GetInstancedFoliageActorForLevel(ULevel* InLevel, bool bCreateIfNone /* = false */)
{
	AInstancedFoliageActor* IFA = nullptr;
	if (InLevel)
	{
		IFA = InLevel->InstancedFoliageActor.Get();

		if (!IFA && bCreateIfNone)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = InLevel;
			IFA = InLevel->GetWorld()->SpawnActor<AInstancedFoliageActor>(SpawnParams);
			InLevel->InstancedFoliageActor = IFA;
		}
	}

	return IFA;
}


int32 AInstancedFoliageActor::GetOverlappingSphereCount(const UFoliageType* FoliageType, const FSphere& Sphere) const
{
	if (const FFoliageInfo* Info = FindInfo(FoliageType))
	{
		return Info->GetOverlappingSphereCount(Sphere);
	}

	return 0;
}


int32 AInstancedFoliageActor::GetOverlappingBoxCount(const UFoliageType* FoliageType, const FBox& Box) const
{
	if (const FFoliageInfo* Info = FindInfo(FoliageType))
	{
		return Info->GetOverlappingBoxCount(Box);
	}

	return 0;
}


void AInstancedFoliageActor::GetOverlappingBoxTransforms(const UFoliageType* FoliageType, const FBox& Box, TArray<FTransform>& OutTransforms) const
{
	if (const FFoliageInfo* Info = FindInfo(FoliageType))
	{
		Info->GetOverlappingBoxTransforms(Box, OutTransforms);
	}
}

void AInstancedFoliageActor::GetOverlappingMeshCounts(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const
{
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo const* Info = &*Pair.Value;

		if (Info)
		{
			Info->GetOverlappingMeshCount(Sphere, OutCounts);
		}
	}
}



UFoliageType* AInstancedFoliageActor::GetLocalFoliageTypeForSource(const UObject* InSource, FFoliageInfo** OutMeshInfo)
{
	UFoliageType* ReturnType = nullptr;
	FFoliageInfo* Info = nullptr;

	for (auto& Pair : FoliageInfos)
	{
		UFoliageType* FoliageType = Pair.Key;
		// Check that the type is neither an asset nor blueprint instance
		if (FoliageType && FoliageType->GetSource() == InSource && !FoliageType->IsAsset() && !FoliageType->GetClass()->ClassGeneratedBy)
		{
			ReturnType = FoliageType;
			Info = &*Pair.Value;
			break;
		}
	}

	if (OutMeshInfo)
	{
		*OutMeshInfo = Info;
	}

	return ReturnType;
}

void AInstancedFoliageActor::GetAllFoliageTypesForSource(const UObject* InSource, TArray<const UFoliageType*>& OutFoliageTypes)
{
	for (auto& Pair : FoliageInfos)
	{
		UFoliageType* FoliageType = Pair.Key;
		if (FoliageType && FoliageType->GetSource() == InSource)
		{
			OutFoliageTypes.Add(FoliageType);
		}
	}
}


FFoliageInfo* AInstancedFoliageActor::FindFoliageTypeOfClass(TSubclassOf<UFoliageType_InstancedStaticMesh> Class)
{
	FFoliageInfo* Info = nullptr;

	for (auto& Pair : FoliageInfos)
	{
		UFoliageType* FoliageType = Pair.Key;
		if (FoliageType && FoliageType->GetClass() == Class)
		{
			Info = &Pair.Value.Get();
			break;
		}
	}

	return Info;
}

FFoliageInfo* AInstancedFoliageActor::FindInfo(const UFoliageType* InType)
{
	TUniqueObj<FFoliageInfo>* InfoEntry = FoliageInfos.Find(InType);
	FFoliageInfo* Info = InfoEntry ? &InfoEntry->Get() : nullptr;
	return Info;
}

const FFoliageInfo* AInstancedFoliageActor::FindInfo(const UFoliageType* InType) const
{
	const TUniqueObj<FFoliageInfo>* InfoEntry = FoliageInfos.Find(InType);
	const FFoliageInfo* Info = InfoEntry ? &InfoEntry->Get() : nullptr;
	return Info;
}


#if WITH_EDITOR
void AInstancedFoliageActor::MoveInstancesForMovedComponent(UActorComponent* InComponent)
{
	const auto BaseId = InstanceBaseCache.GetInstanceBaseId(InComponent);
	if (BaseId == FFoliageInstanceBaseCache::InvalidBaseId)
	{
		return;
	}

	const auto CurrentBaseInfo = InstanceBaseCache.GetInstanceBaseInfo(BaseId);

	// Found an invalid base so don't try to move instances
	if (!CurrentBaseInfo.BasePtr.IsValid())
	{
		return;
	}

	bool bFirst = true;
	const auto NewBaseInfo = InstanceBaseCache.UpdateInstanceBaseInfoTransform(InComponent);

	FMatrix DeltaTransfrom =
		FTranslationMatrix(-CurrentBaseInfo.CachedLocation) *
		FInverseRotationMatrix(CurrentBaseInfo.CachedRotation) *
		FScaleMatrix(NewBaseInfo.CachedDrawScale / CurrentBaseInfo.CachedDrawScale) *
		FRotationMatrix(NewBaseInfo.CachedRotation) *
		FTranslationMatrix(NewBaseInfo.CachedLocation);

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		const auto* InstanceSet = Info.ComponentHash.Find(BaseId);
		if (InstanceSet && InstanceSet->Num())
		{
			if (bFirst)
			{
				bFirst = false;
				Modify();
			}

			Info.Implementation->BeginUpdate();

			for (int32 InstanceIndex : *InstanceSet)
			{
				FFoliageInstance& Instance = Info.Instances[InstanceIndex];

				Info.InstanceHash->RemoveInstance(Instance.Location, InstanceIndex);

				// Apply change
				FMatrix NewTransform =
					FRotationMatrix(Instance.Rotation) *
					FTranslationMatrix(Instance.Location) *
					DeltaTransfrom;

				// Extract rotation and position
				Instance.Location = NewTransform.GetOrigin();
				Instance.Rotation = NewTransform.Rotator();

				// Apply render data
				Info.Implementation->SetInstanceWorldTransform(InstanceIndex, Instance.GetInstanceWorldTransform(), true);

				// Re-add the new instance location to the hash
				Info.InstanceHash->InsertInstance(Instance.Location, InstanceIndex);
			}

			Info.Implementation->EndUpdate();
			Info.Refresh(this, true, false);
		}
	}
}

void AInstancedFoliageActor::DeleteInstancesForComponent(UActorComponent* InComponent)
{
	const auto BaseId = InstanceBaseCache.GetInstanceBaseId(InComponent);
	// Instances with empty base has BaseId==InvalidBaseId, we should not delete these
	if (BaseId == FFoliageInstanceBaseCache::InvalidBaseId)
	{
		return;
	}

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		const auto* InstanceSet = Info.ComponentHash.Find(BaseId);
		if (InstanceSet)
		{
			Info.RemoveInstances(this, InstanceSet->Array(), true);
		}
	}
}

void AInstancedFoliageActor::DeleteInstancesForComponent(UActorComponent* InComponent, const UFoliageType* FoliageType)
{
	const auto BaseId = InstanceBaseCache.GetInstanceBaseId(InComponent);
	// Instances with empty base has BaseId==InvalidBaseId, we should not delete these
	if (BaseId == FFoliageInstanceBaseCache::InvalidBaseId)
	{
		return;
	}

	FFoliageInfo* Info = FindInfo(FoliageType);
	if (Info)
	{
		const auto* InstanceSet = Info->ComponentHash.Find(BaseId);
		if (InstanceSet)
		{
			Info->RemoveInstances(this, InstanceSet->Array(), true);
		}
	}
}

void AInstancedFoliageActor::DeleteInstancesForComponent(UWorld* InWorld, UActorComponent* InComponent)
{
	for (TActorIterator<AInstancedFoliageActor> It(InWorld); It; ++It)
	{
		AInstancedFoliageActor* IFA = (*It);
		IFA->Modify();
		IFA->DeleteInstancesForComponent(InComponent);
	}
}

void AInstancedFoliageActor::DeleteInstancesForProceduralFoliageComponent(const UProceduralFoliageComponent* ProceduralFoliageComponent, bool InRebuildTree)
{
	const FGuid& ProceduralGuid = ProceduralFoliageComponent->GetProceduralGuid();
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		TArray<int32> InstancesToRemove;
		for (int32 InstanceIdx = 0; InstanceIdx < Info.Instances.Num(); InstanceIdx++)
		{
			if (Info.Instances[InstanceIdx].ProceduralGuid == ProceduralGuid)
			{
				InstancesToRemove.Add(InstanceIdx);
			}
		}

		if (InstancesToRemove.Num())
		{
			Info.RemoveInstances(this, InstancesToRemove, InRebuildTree);
		}
	}

	// Clean up dead cross-level references
	FFoliageInstanceBaseCache::CompactInstanceBaseCache(this);
}

bool AInstancedFoliageActor::ContainsInstancesFromProceduralFoliageComponent(const UProceduralFoliageComponent* ProceduralFoliageComponent)
{
	const FGuid& ProceduralGuid = ProceduralFoliageComponent->GetProceduralGuid();
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		TArray<int32> InstancesToRemove;
		for (int32 InstanceIdx = 0; InstanceIdx < Info.Instances.Num(); InstanceIdx++)
		{
			if (Info.Instances[InstanceIdx].ProceduralGuid == ProceduralGuid)
			{
				// The procedural component is responsible for an instance
				return true;
			}
		}
	}

	return false;
}

void AInstancedFoliageActor::MoveInstancesForComponentToCurrentLevel(UActorComponent* InComponent)
{
	if (!HasFoliageAttached(InComponent))
	{
		// Quit early if there are no foliage instances painted on this component
		return;
	}

	UWorld* InWorld = InComponent->GetWorld();
	AInstancedFoliageActor* NewIFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(InWorld, true);
	NewIFA->Modify();

	for (TActorIterator<AInstancedFoliageActor> It(InWorld); It; ++It)
	{
		AInstancedFoliageActor* IFA = (*It);

		const auto SourceBaseId = IFA->InstanceBaseCache.GetInstanceBaseId(InComponent);
		if (SourceBaseId != FFoliageInstanceBaseCache::InvalidBaseId && IFA != NewIFA)
		{
			IFA->Modify();

			for (auto& Pair : IFA->FoliageInfos)
			{
				FFoliageInfo& Info = *Pair.Value;
				UFoliageType* FoliageType = Pair.Key;

				const auto* InstanceSet = Info.ComponentHash.Find(SourceBaseId);
				if (InstanceSet)
				{
					// Duplicate the foliage type if it's not shared
					FFoliageInfo* TargetMeshInfo = nullptr;
					UFoliageType* TargetFoliageType = NewIFA->AddFoliageType(FoliageType, &TargetMeshInfo);

					// Add the foliage to the new level
					for (int32 InstanceIndex : *InstanceSet)
					{
						TargetMeshInfo->AddInstance(NewIFA, TargetFoliageType, Info.Instances[InstanceIndex], InComponent);
					}

					TargetMeshInfo->Refresh(NewIFA, true, true);

					// Remove from old level
					Info.RemoveInstances(IFA, InstanceSet->Array(), true);
				}
			}
		}
	}
}

void AInstancedFoliageActor::MoveInstancesToNewComponent(UPrimitiveComponent* InOldComponent, const FBox& InBoxWithInstancesToMove, UPrimitiveComponent* InNewComponent)
{	
	const auto OldBaseId = InstanceBaseCache.GetInstanceBaseId(InOldComponent);
	if (OldBaseId == FFoliageInstanceBaseCache::InvalidBaseId)
	{
		// This foliage actor has no instances with specified base
		return;
	}

	AInstancedFoliageActor* TargetIFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(InNewComponent->GetTypedOuter<ULevel>(), true);
	TArray<int32> InstancesToMove;

	for (auto& Pair : FoliageInfos)
	{
		InstancesToMove.Reset();

		FFoliageInfo& Info = *Pair.Value;
		
		InstancesToMove = Info.GetInstancesOverlappingBox(InBoxWithInstancesToMove);
		
		FFoliageInfo* TargetMeshInfo = nullptr;
		UFoliageType* TargetFoliageType = TargetIFA->AddFoliageType(Pair.Key, &TargetMeshInfo);

		// Add the foliage to the new level
		for (int32 InstanceIndex : InstancesToMove)
		{
			if (Info.Instances.IsValidIndex(InstanceIndex))
			{
				FFoliageInstance NewInstance = Info.Instances[InstanceIndex];
				TargetMeshInfo->AddInstance(TargetIFA, TargetFoliageType, NewInstance, InNewComponent);
			}
		}

		TargetMeshInfo->Refresh(TargetIFA, true, true);
		
		// Remove from old level
		Info.RemoveInstances(this, InstancesToMove, true);
	}
}

void AInstancedFoliageActor::MoveInstancesToNewComponent(UPrimitiveComponent* InOldComponent, UPrimitiveComponent* InNewComponent)
{
	AInstancedFoliageActor* TargetIFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(InNewComponent->GetTypedOuter<ULevel>(), true);

	const auto OldBaseId = this->InstanceBaseCache.GetInstanceBaseId(InOldComponent);
	if (OldBaseId == FFoliageInstanceBaseCache::InvalidBaseId)
	{
		// This foliage actor has no any instances with specified base
		return;
	}

	const auto NewBaseId = TargetIFA->InstanceBaseCache.AddInstanceBaseId(InNewComponent);

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;

		TSet<int32> InstanceSet;
		if (Info.ComponentHash.RemoveAndCopyValue(OldBaseId, InstanceSet) && InstanceSet.Num())
		{
			// For same FoliageActor can just remap the instances, otherwise we have to do a more complex move
			if (TargetIFA == this)
			{
				// Update the instances
				for (int32 InstanceIndex : InstanceSet)
				{
					Info.Instances[InstanceIndex].BaseId = NewBaseId;
				}

				// Update the hash
				Info.ComponentHash.Add(NewBaseId, MoveTemp(InstanceSet));
			}
			else
			{
				FFoliageInfo* TargetMeshInfo = nullptr;
				UFoliageType* TargetFoliageType = TargetIFA->AddFoliageType(Pair.Key, &TargetMeshInfo);

				// Add the foliage to the new level
				for (int32 InstanceIndex : InstanceSet)
				{
					FFoliageInstance NewInstance = Info.Instances[InstanceIndex];
					NewInstance.BaseId = NewBaseId;
					TargetMeshInfo->AddInstance(TargetIFA, TargetFoliageType, NewInstance);
				}

				TargetMeshInfo->Refresh(TargetIFA, true, true);

				// Remove from old level
				Info.RemoveInstances(this, InstanceSet.Array(), true);
			}
		}
	}
}

void AInstancedFoliageActor::MoveInstancesToNewComponent(UWorld* InWorld, UPrimitiveComponent* InOldComponent, UPrimitiveComponent* InNewComponent)
{
	for (TActorIterator<AInstancedFoliageActor> It(InWorld); It; ++It)
	{
		AInstancedFoliageActor* IFA = (*It);
		IFA->MoveInstancesToNewComponent(InOldComponent, InNewComponent);
	}
}

void AInstancedFoliageActor::MoveInstancesToNewComponent(UWorld* InWorld, UPrimitiveComponent* InOldComponent, const FBox& InBoxWithInstancesToMove, UPrimitiveComponent* InNewComponent)
{
	for (TActorIterator<AInstancedFoliageActor> It(InWorld); It; ++It)
	{
		AInstancedFoliageActor* IFA = (*It);
		IFA->MoveInstancesToNewComponent(InOldComponent, InBoxWithInstancesToMove, InNewComponent);
	}
}

void AInstancedFoliageActor::MoveInstancesToLevel(ULevel* InTargetLevel, TSet<int32>& InInstanceList, FFoliageInfo* InCurrentMeshInfo, UFoliageType* InFoliageType)
{
	if (InTargetLevel == GetLevel())
	{
		return;
	}

	AInstancedFoliageActor* TargetIFA = GetInstancedFoliageActorForLevel(InTargetLevel, /*bCreateIfNone*/ true);

	Modify();
	TargetIFA->Modify();

	// Do move
	FFoliageInfo* TargetMeshInfo = nullptr;
	UFoliageType* TargetFoliageType = TargetIFA->AddFoliageType(InFoliageType, &TargetMeshInfo);

	// Add selected instances to the target actor
	for (int32 InstanceIndex : InInstanceList)
	{
		FFoliageInstance& Instance = InCurrentMeshInfo->Instances[InstanceIndex];
		TargetMeshInfo->AddInstance(TargetIFA, TargetFoliageType, Instance, InstanceBaseCache.GetInstanceBasePtr(Instance.BaseId).Get());
	}

	TargetMeshInfo->Refresh(TargetIFA, true, true);

	// Remove selected instances from this actor
	InCurrentMeshInfo->RemoveInstances(this, InInstanceList.Array(), true);
}

void AInstancedFoliageActor::MoveSelectedInstancesToLevel(ULevel* InTargetLevel)
{
	if (InTargetLevel == GetLevel() || !HasSelectedInstances())
	{
		return;
	}

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		UFoliageType* FoliageType = Pair.Key;

		MoveInstancesToLevel(InTargetLevel, Info.SelectedIndices, &Info, FoliageType);
	}
}

void AInstancedFoliageActor::MoveAllInstancesToLevel(ULevel* InTargetLevel)
{
	if (InTargetLevel == GetLevel())
	{
		return;
	}

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		UFoliageType* FoliageType = Pair.Key;

		TSet<int32> instancesList;

		for (int32 i = 0; i < Info.Instances.Num(); ++i)
		{
			instancesList.Add(i);
		}

		MoveInstancesToLevel(InTargetLevel, instancesList, &Info, FoliageType);
	}
}

TMap<UFoliageType*, TArray<const FFoliageInstancePlacementInfo*>> AInstancedFoliageActor::GetInstancesForComponent(UActorComponent* InComponent)
{
	TMap<UFoliageType*, TArray<const FFoliageInstancePlacementInfo*>> Result;
	const auto BaseId = InstanceBaseCache.GetInstanceBaseId(InComponent);

	if (BaseId != FFoliageInstanceBaseCache::InvalidBaseId)
	{
		for (auto& Pair : FoliageInfos)
		{
			const FFoliageInfo& Info = *Pair.Value;
			const auto* InstanceSet = Info.ComponentHash.Find(BaseId);
			if (InstanceSet)
			{
				TArray<const FFoliageInstancePlacementInfo*>& Array = Result.Add(Pair.Key, TArray<const FFoliageInstancePlacementInfo*>());
				Array.Empty(InstanceSet->Num());

				for (int32 InstanceIndex : *InstanceSet)
				{
					const FFoliageInstancePlacementInfo* Instance = &Info.Instances[InstanceIndex];
					Array.Add(Instance);
				}
			}
		}
	}

	return Result;
}

FFoliageInfo* AInstancedFoliageActor::FindOrAddMesh(UFoliageType* InType)
{
	TUniqueObj<FFoliageInfo>* MeshInfoEntry = FoliageInfos.Find(InType);
	FFoliageInfo* Info = MeshInfoEntry ? &MeshInfoEntry->Get() : AddMesh(InType);
	return Info;
}

UFoliageType* AInstancedFoliageActor::AddFoliageType(const UFoliageType* InType, FFoliageInfo** OutInfo)
{
	FFoliageInfo* Info = nullptr;
	UFoliageType* FoliageType = const_cast<UFoliageType*>(InType);

	if (FoliageType->GetOuter() == this || FoliageType->IsAsset())
	{
		auto ExistingMeshInfo = FoliageInfos.Find(FoliageType);
		if (!ExistingMeshInfo)
		{
			Modify();
			Info = &FoliageInfos.Add(FoliageType).Get();
		}
		else
		{
			Info = &ExistingMeshInfo->Get();
		}
	}
	else if (FoliageType->GetClass()->ClassGeneratedBy)
	{
		// Foliage type blueprint
		FFoliageInfo* ExistingMeshInfo = FindFoliageTypeOfClass(FoliageType->GetClass());
		if (!ExistingMeshInfo)
		{
			Modify();
			FoliageType = DuplicateObject<UFoliageType>(InType, this);
			Info = &FoliageInfos.Add(FoliageType).Get();
		}
		else
		{
			Info = ExistingMeshInfo;
		}
	}
	else
	{
		// Unique meshes only
		// Multiple entries for same static mesh can be added using FoliageType as an asset
		FoliageType = GetLocalFoliageTypeForSource(FoliageType->GetSource(), &Info);
		if (FoliageType == nullptr)
		{
			Modify();
			FoliageType = DuplicateObject<UFoliageType>(InType, this);
			Info = &FoliageInfos.Add(FoliageType).Get();
		}
	}

	if (Info && !Info->Implementation.IsValid())
	{
		Info->CreateImplementation(FoliageType);
		check(Info->Implementation.IsValid());
	}

	if (OutInfo)
	{
		*OutInfo = Info;
	}

	return FoliageType;
}

FFoliageInfo* AInstancedFoliageActor::AddMesh(UStaticMesh* InMesh, UFoliageType** OutSettings, const UFoliageType_InstancedStaticMesh* DefaultSettings)
{
	check(GetLocalFoliageTypeForSource(InMesh) == nullptr);

	MarkPackageDirty();

	UFoliageType_InstancedStaticMesh* Settings = nullptr;
#if WITH_EDITORONLY_DATA
	if (DefaultSettings)
	{
		// TODO: Can't we just use this directly?
		FObjectDuplicationParameters DuplicationParameters(const_cast<UFoliageType_InstancedStaticMesh*>(DefaultSettings), this);
		DuplicationParameters.ApplyFlags = RF_Transactional;
		Settings = CastChecked<UFoliageType_InstancedStaticMesh>(StaticDuplicateObjectEx(DuplicationParameters));
	}
	else
#endif
	{
		Settings = NewObject<UFoliageType_InstancedStaticMesh>(this, NAME_None, RF_Transactional);
	}
	Settings->SetStaticMesh(InMesh);
	FFoliageInfo* Info = AddMesh(Settings);
	
	if (OutSettings)
	{
		*OutSettings = Settings;
	}

	return Info;
}

FFoliageInfo* AInstancedFoliageActor::AddMesh(UFoliageType* InType)
{
	check(FoliageInfos.Find(InType) == nullptr);

	Modify();

	FFoliageInfo* Info = &*FoliageInfos.Add(InType);
	if (!Info->Implementation.IsValid())
	{
		Info->CreateImplementation(InType);
	}
	Info->FoliageTypeUpdateGuid = InType->UpdateGuid;
	InType->IsSelected = true;

	return Info;
}

void AInstancedFoliageActor::RemoveFoliageType(UFoliageType** InFoliageTypes, int32 Num)
{
	Modify();
	UnregisterAllComponents();

	// Remove all components for this mesh from the Components array.
	for (int32 FoliageTypeIdx = 0; FoliageTypeIdx < Num; ++FoliageTypeIdx)
	{
		const UFoliageType* FoliageType = InFoliageTypes[FoliageTypeIdx];
		FFoliageInfo* Info = FindInfo(FoliageType);
		if (Info)
		{
			if (Info->IsInitialized())
			{
				Info->Uninitialize();
			}

			FoliageInfos.Remove(FoliageType);
		}
	}

	RegisterAllComponents();
}

void AInstancedFoliageActor::ClearSelection()
{
	UWorld* World = GetWorld();
	const int32 NumLevels = World->GetNumLevels();
	for (int32 LevelIdx = 0; LevelIdx < NumLevels; ++LevelIdx)
	{
		if (ULevel* Level = World->GetLevel(LevelIdx))
		{
			if (AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level))
			{
				for (auto& Pair : IFA->FoliageInfos)
				{
					FFoliageInfo& Info = *Pair.Value;
					Info.ClearSelection();
				}
			}
		}
	}
}

void AInstancedFoliageActor::SelectInstance(UInstancedStaticMeshComponent* InComponent, int32 InInstanceIndex, bool bToggle)
{
	Modify();

	// If we're not toggling, we need to first deselect everything else
	if (!bToggle)
	{
		ClearSelection();
	}

	if (InComponent)
	{
		UFoliageType* Type = nullptr;
		FFoliageInfo* Info = nullptr;

		for (auto& Pair : FoliageInfos)
		{
			if (Pair.Value->Type == EFoliageImplType::StaticMesh)
			{
				FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Pair.Value->Implementation.Get());
				if (FoliageStaticMesh->Component == InComponent)
				{
					Type = Pair.Key;
					Info = &Pair.Value.Get();
					break;
				}
			}
		}

		if (Info)
		{
			bool bIsSelected = Info->SelectedIndices.Contains(InInstanceIndex);

			// Deselect if it's already selected.
			if (InInstanceIndex < InComponent->SelectedInstances.Num())
			{
				InComponent->SelectInstance(false, InInstanceIndex, 1);
				InComponent->MarkRenderStateDirty();
			}

			if (bIsSelected)
			{
				Info->SelectedIndices.Remove(InInstanceIndex);
			}

			if (!bToggle || !bIsSelected)
			{
				// Add the selection
				InComponent->SelectInstance(true, InInstanceIndex, 1);
				InComponent->MarkRenderStateDirty();

				Info->SelectedIndices.Add(InInstanceIndex);
			}
		}
	}
}

void AInstancedFoliageActor::SelectInstance(AActor* InActor, bool bToggle)
{
	Modify();

	// If we're not toggling, we need to first deselect everything else
	if (!bToggle)
	{
		ClearSelection();
	}

	if (InActor)
	{
		UFoliageType* Type = nullptr;
		FFoliageInfo* Info = nullptr;
		FFoliageActor* FoliageActor = nullptr;
		int32 index = INDEX_NONE;

		for (auto& Pair : FoliageInfos)
		{
			if (Pair.Value->Type == EFoliageImplType::Actor)
			{
				FFoliageActor* CurrentFoliageActor = StaticCast<FFoliageActor*>(Pair.Value->Implementation.Get());
				index = CurrentFoliageActor->FindIndex(InActor);
				if (index != INDEX_NONE)
				{
					Info = &Pair.Value.Get();
					FoliageActor = CurrentFoliageActor;
					break;
				}
			}
		}

		if (Info)
		{
			bool bIsSelected = Info->SelectedIndices.Contains(index);

			FoliageActor->SelectInstances(false, index, 1);
			
			if (bIsSelected)
			{
				Info->SelectedIndices.Remove(index);
			}

			if (!bToggle || !bIsSelected)
			{
				// Add the selection
				FoliageActor->SelectInstances(true, index, 1);

				Info->SelectedIndices.Add(index);
			}
		}
	}
}

bool AInstancedFoliageActor::HasSelectedInstances() const
{
	for (const auto& Pair : FoliageInfos)
	{
		if (Pair.Value->SelectedIndices.Num() > 0)
		{
			return true;
		}
	}

	return false;
}

TMap<UFoliageType*, FFoliageInfo*> AInstancedFoliageActor::GetAllInstancesFoliageType()
{
	TMap<UFoliageType*, FFoliageInfo*> InstanceFoliageTypes;

	for (auto& Pair : FoliageInfos)
	{
		InstanceFoliageTypes.Add(Pair.Key, &Pair.Value.Get());
	}

	return InstanceFoliageTypes;
}

TMap<UFoliageType*, FFoliageInfo*> AInstancedFoliageActor::GetSelectedInstancesFoliageType()
{
	TMap<UFoliageType*, FFoliageInfo*> SelectedInstanceFoliageTypes;

	for (auto& Pair : FoliageInfos)
	{
		if (Pair.Value->SelectedIndices.Num() > 0)
		{
			SelectedInstanceFoliageTypes.Add(Pair.Key, &Pair.Value.Get());
		}
	}

	return SelectedInstanceFoliageTypes;
}

void AInstancedFoliageActor::Destroyed()
{
	if (GIsEditor && !GetWorld()->IsGameWorld())
	{
		for (auto& Pair : FoliageInfos)
		{
			if (Pair.Value->Type == EFoliageImplType::StaticMesh)
			{
				FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Pair.Value->Implementation.Get());
				UHierarchicalInstancedStaticMeshComponent* Component = FoliageStaticMesh->Component;

				if (Component)
				{
					Component->ClearInstances();
					// Save the component's PendingKill flag to restore the component if the delete is undone.
					Component->SetFlags(RF_Transactional);
					Component->Modify();
				}
			}
			else if (Pair.Value->Type == EFoliageImplType::Actor)
			{
				FFoliageActor* FoliageActor = StaticCast<FFoliageActor*>(Pair.Value->Implementation.Get());
				FoliageActor->DestroyActors(false);
			}
		}
		FoliageInfos.Empty();
	}

	Super::Destroyed();
}

void AInstancedFoliageActor::PreEditUndo()
{
	Super::PreEditUndo();

	// Remove all delegate as we dont know what the Undo will affect and we will simply readd those still valid afterward
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;

		Info.PreEditUndo(this, Pair.Key);
	}
}

void AInstancedFoliageActor::PostEditUndo()
{
	Super::PostEditUndo();

	FlushRenderingCommands();

	InstanceBaseCache.UpdateInstanceBaseCachedTransforms();

	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;

		Info.PostEditUndo(this, Pair.Key);
	}
}

bool AInstancedFoliageActor::ShouldExport()
{
	// We don't support exporting/importing InstancedFoliageActor itself
	// Instead foliage instances exported/imported together with components it's painted on
	return false;
}

bool AInstancedFoliageActor::ShouldImport(FString* ActorPropString, bool IsMovingLevel)
{
	return false;
}

void AInstancedFoliageActor::ApplySelection(bool bApply)
{
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
				
		Info.Implementation->ApplySelection(bApply, Info.SelectedIndices);
	}
}

bool AInstancedFoliageActor::GetSelectionLocation(FVector& OutLocation) const
{
	for (const auto& Pair : FoliageInfos)
	{
		const FFoliageInfo& Info = Pair.Value.Get();
		if (Info.SelectedIndices.Num())
		{
			const int32 InstanceIdx = (*Info.SelectedIndices.CreateConstIterator());
			OutLocation = Info.Instances[InstanceIdx].Location;
			return true;
		}
	}
	return false;
}

bool AInstancedFoliageActor::HasFoliageAttached(UActorComponent* InComponent)
{
	for (TActorIterator<AInstancedFoliageActor> It(InComponent->GetWorld()); It; ++It)
	{
		AInstancedFoliageActor* IFA = (*It);
		if (IFA->InstanceBaseCache.GetInstanceBaseId(InComponent) != FFoliageInstanceBaseCache::InvalidBaseId)
		{
			return true;
		}
	}

	return false;
}


void AInstancedFoliageActor::MapRebuild()
{
	// Map rebuild may have modified the BSP's ModelComponents and thrown the previous ones away.
	// Most BSP-painted foliage is attached to a Brush's UModelComponent which persist across rebuilds,
	// but any foliage attached directly to the level BSP's ModelComponents will need to try to find a new base.

	TMap<UFoliageType*, TArray<FFoliageInstance>> NewInstances;
	TArray<UModelComponent*> RemovedModelComponents;
	UWorld* World = GetWorld();
	check(World);

	// For each foliage brush, represented by the mesh/info pair
	for (auto& Pair : FoliageInfos)
	{
		// each target component has some foliage instances
		FFoliageInfo const& Info = *Pair.Value;
		UFoliageType* Settings = Pair.Key;
		check(Settings);

		for (auto& ComponentFoliagePair : Info.ComponentHash)
		{
			// BSP components are UModelComponents - they are the only ones we need to change
			auto BaseComponentPtr = InstanceBaseCache.GetInstanceBasePtr(ComponentFoliagePair.Key);
			UModelComponent* TargetComponent = Cast<UModelComponent>(BaseComponentPtr.Get());

			// Check if it's part of a brush. We only need to fix up model components that are part of the level BSP.
			if (TargetComponent && Cast<ABrush>(TargetComponent->GetOuter()) == nullptr)
			{
				// Delete its instances later
				RemovedModelComponents.Add(TargetComponent);

				// We have to test each instance to see if we can migrate it across
				for (int32 InstanceIdx : ComponentFoliagePair.Value)
				{
					// Use a line test against the world. This is not very reliable as we don't know the original trace direction.
					check(Info.Instances.IsValidIndex(InstanceIdx));
					FFoliageInstance const& Instance = Info.Instances[InstanceIdx];

					FFoliageInstance NewInstance = Instance;

					FTransform InstanceToWorld = Instance.GetInstanceWorldTransform();
					FVector Down(-FVector::UpVector);
					FVector Start(InstanceToWorld.TransformPosition(FVector::UpVector));
					FVector End(InstanceToWorld.TransformPosition(Down));

					FHitResult Result;
					bool bHit = World->LineTraceSingleByObjectType(Result, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(), true));

					if (bHit && Result.Component.IsValid() && Result.Component->IsA(UModelComponent::StaticClass()))
					{
						NewInstance.BaseId = InstanceBaseCache.AddInstanceBaseId(Result.Component.Get());
						NewInstances.FindOrAdd(Settings).Add(NewInstance);
					}
				}
			}
		}
	}

	// Remove all existing & broken instances & component references.
	for (UModelComponent* Component : RemovedModelComponents)
	{
		DeleteInstancesForComponent(Component);
	}

	// And then finally add our new instances to the correct target components.
	for (auto& NewInstancePair : NewInstances)
	{
		UFoliageType* Settings = NewInstancePair.Key;
		check(Settings);
		FFoliageInfo& Info = *FindOrAddMesh(Settings);
		for (FFoliageInstance& Instance : NewInstancePair.Value)
		{
			Info.AddInstance(this, Settings, Instance);
		}

		Info.Refresh(this, true, true);
	}
}

#endif // WITH_EDITOR

struct FFoliageMeshInfo_Old
{
	TArray<FFoliageInstanceCluster_Deprecated> InstanceClusters;
	TArray<FFoliageInstance_Deprecated> Instances;
	UFoliageType_InstancedStaticMesh* Settings; // Type remapped via +ActiveClassRedirects
};
FArchive& operator<<(FArchive& Ar, FFoliageMeshInfo_Old& MeshInfo)
{
	Ar << MeshInfo.InstanceClusters;
	Ar << MeshInfo.Instances;
	Ar << MeshInfo.Settings;

	return Ar;
}

void AInstancedFoliageActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFoliageCustomVersion::GUID);

#if WITH_EDITORONLY_DATA
	if (!Ar.ArIsFilterEditorOnly && Ar.CustomVer(FFoliageCustomVersion::GUID) >= FFoliageCustomVersion::CrossLevelBase)
	{
		Ar << InstanceBaseCache;
	}
#endif

	if (Ar.UE4Ver() < VER_UE4_FOLIAGE_SETTINGS_TYPE)
	{
#if WITH_EDITORONLY_DATA
		TMap<UFoliageType*, TUniqueObj<FFoliageMeshInfo_Deprecated>> FoliageMeshesDeprecated;
		TMap<UStaticMesh*, FFoliageMeshInfo_Old> OldFoliageMeshes;
		Ar << OldFoliageMeshes;
		for (auto& OldMeshInfo : OldFoliageMeshes)
		{
			FFoliageMeshInfo_Deprecated NewMeshInfo;

			NewMeshInfo.Instances = MoveTemp(OldMeshInfo.Value.Instances);

			UFoliageType_InstancedStaticMesh* FoliageType = OldMeshInfo.Value.Settings;
			if (FoliageType == nullptr)
			{
				// If the Settings object was null, eg the user forgot to save their settings asset, create a new one.
				FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(this);
			}

			if (FoliageType->Mesh == nullptr)
			{
				FoliageType->Modify();
				FoliageType->Mesh = OldMeshInfo.Key;
			}
			else if (FoliageType->Mesh != OldMeshInfo.Key)
			{
				// If mesh doesn't match (two meshes sharing the same settings object?) then we need to duplicate as that is no longer supported
				FoliageType = (UFoliageType_InstancedStaticMesh*)StaticDuplicateObject(FoliageType, this, NAME_None, RF_AllFlags & ~(RF_Standalone | RF_Public));
				FoliageType->Mesh = OldMeshInfo.Key;
			}
			NewMeshInfo.FoliageTypeUpdateGuid = FoliageType->UpdateGuid;
			FoliageMeshes_Deprecated.Add(FoliageType, TUniqueObj<FFoliageMeshInfo_Deprecated>(MoveTemp(NewMeshInfo)));
		}
#endif//WITH_EDITORONLY_DATA
	}
	else
	{
		if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::CrossLevelBase)
		{
#if WITH_EDITORONLY_DATA
			Ar << FoliageMeshes_Deprecated;
#endif
		}
		else if(Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageActorSupport)
		{
#if WITH_EDITORONLY_DATA
			Ar << FoliageMeshes_Deprecated2;
#endif
		}
		else
		{
			Ar << FoliageInfos;
		}
	}

	// Clean up any old cluster components and convert to hierarchical instanced foliage.
	if (Ar.CustomVer(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageUsingHierarchicalISMC)
	{
		for (UActorComponent* Component : GetComponents())
		{
			if (Cast<UInstancedStaticMeshComponent>(Component))
			{
				Component->bAutoRegister = false;
			}
		}
	}
}

#if WITH_EDITOR
AInstancedFoliageActor::FOnSelectionChanged AInstancedFoliageActor::SelectionChanged;

void AInstancedFoliageActor::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsTemplate())
	{
		GEngine->OnActorMoved().Remove(OnLevelActorMovedDelegateHandle);
		OnLevelActorMovedDelegateHandle = GEngine->OnActorMoved().AddUObject(this, &AInstancedFoliageActor::OnLevelActorMoved);

		GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedDelegateHandle);
		OnLevelActorDeletedDelegateHandle = GEngine->OnLevelActorDeleted().AddUObject(this, &AInstancedFoliageActor::OnLevelActorDeleted);

		if (GetLevel())
		{
			OnApplyLevelTransformDelegateHandle = GetLevel()->OnApplyLevelTransform.AddUObject(this, &AInstancedFoliageActor::OnApplyLevelTransform);
		}

		FWorldDelegates::PostApplyLevelOffset.Remove(OnPostApplyLevelOffsetDelegateHandle);
		OnPostApplyLevelOffsetDelegateHandle = FWorldDelegates::PostApplyLevelOffset.AddUObject(this, &AInstancedFoliageActor::OnPostApplyLevelOffset);
	}
}

void AInstancedFoliageActor::BeginDestroy()
{
	Super::BeginDestroy();

	if (!IsTemplate())
	{
		GEngine->OnActorMoved().Remove(OnLevelActorMovedDelegateHandle);
		GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedDelegateHandle);
		
		if (GetLevel())
		{
			GetLevel()->OnApplyLevelTransform.Remove(OnApplyLevelTransformDelegateHandle);
		}

		FWorldDelegates::PostApplyLevelOffset.Remove(OnPostApplyLevelOffsetDelegateHandle);
	}
}
#endif

void AInstancedFoliageActor::PostLoad()
{
	Super::PostLoad();

	ULevel* OwningLevel = GetLevel();
	if (OwningLevel)
	{
		if (!OwningLevel->InstancedFoliageActor.IsValid())
		{
			OwningLevel->InstancedFoliageActor = this;
		}
		else
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Level"), FText::FromString(*OwningLevel->GetOutermost()->GetName()));
			FMessageLog("MapCheck").Warning()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_DuplicateInstancedFoliageActor", "Level {Level} has an unexpected duplicate Instanced Foliage Actor."), Arguments)))
#if WITH_EDITOR
				->AddToken(FActionToken::Create(LOCTEXT("MapCheck_FixDuplicateInstancedFoliageActor", "Fix"),
					LOCTEXT("MapCheck_FixDuplicateInstancedFoliageActor_Desc", "Click to consolidate foliage into the main foliage actor."),
					FOnActionTokenExecuted::CreateUObject(OwningLevel->InstancedFoliageActor.Get(), &AInstancedFoliageActor::RepairDuplicateIFA, this), true))
#endif// WITH_EDITOR
				;
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::CrossLevelBase)
		{
			ConvertDeprecatedFoliageMeshes(this, FoliageMeshes_Deprecated, FoliageInfos);
			FoliageMeshes_Deprecated.Empty();
		}
		else if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageActorSupport)
		{
			ConvertDeprecated2FoliageMeshes(this, FoliageMeshes_Deprecated2, FoliageInfos);
			FoliageMeshes_Deprecated2.Empty();
		}
			   
		{
			bool bContainsNull = FoliageInfos.Remove(nullptr) > 0;
			if (bContainsNull)
			{
				FMessageLog("MapCheck").Warning()
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_FoliageMissingStaticMesh", "Foliage instances for a missing static mesh have been removed.")))
					->AddToken(FMapErrorToken::Create(FMapErrors::FoliageMissingStaticMesh));
				while (bContainsNull)
				{
					bContainsNull = FoliageInfos.Remove(nullptr) > 0;
				}
			}
		}

		TArray<UFoliageType*> FoliageTypeToRemove;

		for (auto& Pair : FoliageInfos)
		{
			// Find the per-mesh info matching the mesh.
			FFoliageInfo& Info = *Pair.Value;
			UFoliageType* FoliageType = Pair.Key;

			// Make sure the source data has been PostLoaded as if not it can be considered invalid resulting in a bad HISMC tree
			UObject* Source = FoliageType->GetSource();
			if (Source)
			{
				Source->ConditionalPostLoad();
			}

			if (Info.Instances.Num() && !Info.IsInitialized())
			{
				FFormatNamedArguments Arguments;
				if (Source)
				{
					Arguments.Add(TEXT("MeshName"), FText::FromString(Source->GetName()));
				}
				else
				{
					Arguments.Add(TEXT("MeshName"), FText::FromString(TEXT("None")));
				}

				FMessageLog("MapCheck").Warning()
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_FoliageMissingComponent", "Foliage in this map is missing a component for static mesh {MeshName}. This has been repaired."), Arguments)))
					->AddToken(FMapErrorToken::Create(FMapErrors::FoliageMissingClusterComponent));

				Info.ReallocateClusters(this, Pair.Key);
			}

			// Update the hash.
			Info.ComponentHash.Empty();
			Info.InstanceHash->Empty();
			for (int32 InstanceIdx = 0; InstanceIdx < Info.Instances.Num(); InstanceIdx++)
			{
				Info.AddToBaseHash(InstanceIdx);
				Info.InstanceHash->InsertInstance(Info.Instances[InstanceIdx].Location, InstanceIdx);
			}

			// Convert to Hierarchical foliage
			if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageUsingHierarchicalISMC)
			{
				Info.ReallocateClusters(this, Pair.Key);
			}

			if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::HierarchicalISMCNonTransactional)
			{
				check(Info.Type == EFoliageImplType::StaticMesh);
				if (Info.Type == EFoliageImplType::StaticMesh)
				{
					FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Info.Implementation.Get());
					if (FoliageStaticMesh->Component)
					{
						FoliageStaticMesh->Component->ClearFlags(RF_Transactional);
					}
				}
			}

			// Clean up case where embeded instances had their static mesh deleted
			if (FoliageType->IsNotAssetOrBlueprint() && FoliageType->GetSource() == nullptr)
			{
				// We can't remove them here as we are within the loop itself so clean up after
				FoliageTypeToRemove.Add(FoliageType);
				
				continue;
			}

			// Upgrade foliage component
			if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageUsingFoliageISMC)
			{
				check(Info.Type == EFoliageImplType::StaticMesh);
				if (Info.Type == EFoliageImplType::StaticMesh)
				{
					FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Info.Implementation.Get());
					UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
					FoliageStaticMesh->CheckComponentClass(this, FoliageType_InstancedStaticMesh, Info.Instances, Info.SelectedIndices);
				}
			}

			if (GetLinkerCustomVersion(FFoliageCustomVersion::GUID) < FFoliageCustomVersion::FoliageActorSupportNoWeakPtr)
			{
				if (Info.Type == EFoliageImplType::Actor)
				{
					FFoliageActor* FoliageActor = StaticCast<FFoliageActor*>(Info.Implementation.Get());
					for (const TWeakObjectPtr<AActor>& ActorPtr : FoliageActor->ActorInstances_Deprecated)
					{
						FoliageActor->ActorInstances.Add(ActorPtr.Get());
					}
					FoliageActor->ActorInstances_Deprecated.Empty();
				}
			}

			// Update foliage component settings if the foliage settings object was changed while the level was not loaded.
			if (Info.FoliageTypeUpdateGuid != FoliageType->UpdateGuid)
			{
				if (Info.FoliageTypeUpdateGuid.IsValid())
				{
					if (Info.Type == EFoliageImplType::StaticMesh)
					{
						FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Info.Implementation.Get());
						UFoliageType_InstancedStaticMesh* FoliageType_InstancedStaticMesh = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
						FoliageStaticMesh->CheckComponentClass(this, FoliageType_InstancedStaticMesh, Info.Instances, Info.SelectedIndices);
						FoliageStaticMesh->UpdateComponentSettings(FoliageType_InstancedStaticMesh);
					}
					else if (Info.Type == EFoliageImplType::Actor)
					{
						FFoliageActor* FoliageActor = StaticCast<FFoliageActor*>(Info.Implementation.Get());
						const bool bPostLoad = true;
						FoliageActor->Reapply(this, FoliageType, Info.Instances, bPostLoad);
					}
				}
				Info.FoliageTypeUpdateGuid = FoliageType->UpdateGuid;
			}
		}

#if WITH_EDITORONLY_DATA
		if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::FoliageLazyObjPtrToSoftObjPtr)
		{
			for (auto Iter = InstanceBaseCache.InstanceBaseMap.CreateIterator(); Iter; ++Iter)
			{
				TPair<FFoliageInstanceBaseId, FFoliageInstanceBaseInfo>& Pair = *Iter;
				FFoliageInstanceBaseInfo& BaseInfo = Pair.Value;
				UActorComponent* Component = BaseInfo.BasePtr_DEPRECATED.Get();
				BaseInfo.BasePtr_DEPRECATED.Reset();

				if (Component != nullptr)
				{
					BaseInfo.BasePtr = Component;

					if (!InstanceBaseCache.InstanceBaseInvMap.Contains(BaseInfo.BasePtr))
					{
						InstanceBaseCache.InstanceBaseInvMap.Add(BaseInfo.BasePtr, Pair.Key);
					}
				}
				else
				{
					Iter.RemoveCurrent();

					const FFoliageInstanceBasePtr* BaseInfoPtr = InstanceBaseCache.InstanceBaseInvMap.FindKey(Pair.Key);

					if (BaseInfoPtr != nullptr && BaseInfoPtr->Get() == nullptr)
					{
						InstanceBaseCache.InstanceBaseInvMap.Remove(*BaseInfoPtr);
					}
				}
			}

			InstanceBaseCache.InstanceBaseMap.Compact();
			InstanceBaseCache.InstanceBaseInvMap.Compact();

			for (auto& Pair : InstanceBaseCache.InstanceBaseLevelMap_DEPRECATED)
			{
				TArray<FFoliageInstanceBasePtr_DEPRECATED>& BaseInfo_DEPRECATED = Pair.Value;
				TArray<FFoliageInstanceBasePtr> BaseInfo;

				for (FFoliageInstanceBasePtr_DEPRECATED& BasePtr_DEPRECATED : BaseInfo_DEPRECATED)
				{
					UActorComponent* Component = BasePtr_DEPRECATED.Get();
					BasePtr_DEPRECATED.Reset();

					if (Component != nullptr)
					{
						BaseInfo.Add(Component);
					}
				}

				InstanceBaseCache.InstanceBaseLevelMap.Add(Pair.Key, BaseInfo);
			}

			InstanceBaseCache.InstanceBaseLevelMap_DEPRECATED.Empty();
		}

		// Clean up dead cross-level references
		FFoliageInstanceBaseCache::CompactInstanceBaseCache(this);
#endif

		// Clean up invalid foliage type
		for (UFoliageType* FoliageType : FoliageTypeToRemove)
		{
			OnFoliageTypeMeshChangedEvent.Broadcast(FoliageType);
			RemoveFoliageType(&FoliageType, 1);
		}
	}

#endif// WITH_EDITOR

	if (!GIsEditor && CVarFoliageDiscardDataOnLoad.GetValueOnGameThread())
	{
		for (auto& Pair : FoliageInfos)
		{
			if (!Pair.Key || Pair.Key->bEnableDensityScaling)
			{
				if (Pair.Value->Type == EFoliageImplType::StaticMesh)
				{
					FFoliageStaticMesh* FoliageStaticMesh = StaticCast<FFoliageStaticMesh*>(Pair.Value->Implementation.Get());

					if (FoliageStaticMesh->Component != nullptr)
					{
						FoliageStaticMesh->Component->ConditionalPostLoad();
						FoliageStaticMesh->Component->DestroyComponent();
					}
				}
				else if (Pair.Value->Type == EFoliageImplType::Actor)
				{
					FFoliageActor* FoliageActor = StaticCast<FFoliageActor*>(Pair.Value->Implementation.Get());
					FoliageActor->DestroyActors(true);
				}
			}
				
			Pair.Value = FFoliageInfo();
		}
	}
}

#if WITH_EDITOR

void AInstancedFoliageActor::RepairDuplicateIFA(AInstancedFoliageActor* DuplicateIFA)
{
	for (auto& Pair : DuplicateIFA->FoliageInfos)
	{
		UFoliageType* DupeFoliageType = Pair.Key;
		FFoliageInfo& DupeMeshInfo = *Pair.Value;

		// Get foliage type compatible with target IFA
		FFoliageInfo* TargetMeshInfo = nullptr;
		UFoliageType* TargetFoliageType = AddFoliageType(DupeFoliageType, &TargetMeshInfo);

		// Copy the instances
		for (FFoliageInstance& Instance : DupeMeshInfo.Instances)
		{
			if ((Instance.Flags & FOLIAGE_InstanceDeleted) == 0)
			{
				TargetMeshInfo->AddInstance(this, TargetFoliageType, Instance);
			}
		}

		TargetMeshInfo->Refresh(this, true, true);
	}

	GetWorld()->DestroyActor(DuplicateIFA);
}

void AInstancedFoliageActor::NotifyFoliageTypeChanged(UFoliageType* FoliageType, bool bSourceChanged)
{
	FFoliageInfo* TypeInfo = FindInfo(FoliageType);

	if (TypeInfo)
	{
		TypeInfo->NotifyFoliageTypeChanged(this, FoliageType, bSourceChanged);	

		if (bSourceChanged)
		{
			// If the type's mesh has changed, the UI needs to be notified so it can update thumbnails accordingly
			OnFoliageTypeMeshChangedEvent.Broadcast(FoliageType);		

			if (FoliageType->IsNotAssetOrBlueprint() && FoliageType->GetSource() == nullptr) //If the source data has been deleted and we're a per foliage actor instance we must remove all instances 
			{
				RemoveFoliageType(&FoliageType, 1);
			}
		}
	}
}

void AInstancedFoliageActor::NotifyFoliageTypeWillChange(UFoliageType* FoliageType)
{
	FFoliageInfo* TypeInfo = FindInfo(FoliageType);

	// Change bounds delegate bindings
	if (TypeInfo)
	{
		TypeInfo->NotifyFoliageTypeWillChange(this, FoliageType);
	}
}

void AInstancedFoliageActor::OnLevelActorMoved(AActor* InActor)
{
	UWorld* InWorld = InActor->GetWorld();

	if (!InWorld || !InWorld->IsGameWorld())
	{
		for (UActorComponent* Component : InActor->GetComponents())
		{
			if (Component)
			{
				MoveInstancesForMovedComponent(Component);
			}
		}
	}
}

void AInstancedFoliageActor::OnLevelActorDeleted(AActor* InActor)
{
	UWorld* InWorld = InActor->GetWorld();

	if (!InWorld || !InWorld->IsGameWorld())
	{
		for (UActorComponent* Component : InActor->GetComponents())
		{
			if (Component)
			{
				DeleteInstancesForComponent(Component);
			}
		}
	}
}

void AInstancedFoliageActor::OnApplyLevelTransform(const FTransform& InTransform)
{
	for (auto& Pair : FoliageInfos)
	{
		FFoliageInfo& Info = *Pair.Value;
		if (Info.Implementation)
		{
			Info.Implementation->PostApplyLevelTransform(InTransform, Info.Instances);
		}
	}
}

void AInstancedFoliageActor::OnPostApplyLevelOffset(ULevel* InLevel, UWorld* InWorld, const FVector& InOffset, bool bWorldShift)
{
	ULevel* OwningLevel = GetLevel();
	if (InLevel != OwningLevel) // TODO: cross-level foliage 
	{
		return;
	}

	if (GIsEditor && InWorld && !InWorld->IsGameWorld())
	{
		for (auto& Pair : FoliageInfos)
		{
			FFoliageInfo& Info = *Pair.Value;

			InstanceBaseCache.UpdateInstanceBaseCachedTransforms();

			Info.InstanceHash->Empty();
			for (int32 InstanceIdx = 0; InstanceIdx < Info.Instances.Num(); InstanceIdx++)
			{
				FFoliageInstance& Instance = Info.Instances[InstanceIdx];
				Instance.Location += InOffset;
				// Rehash instance location
				Info.InstanceHash->InsertInstance(Instance.Location, InstanceIdx);
			}
		}
	}
}


void AInstancedFoliageActor::CleanupDeletedFoliageType()
{
	for (auto& Pair : FoliageInfos)
	{
		if (Pair.Key == nullptr)
		{
			FFoliageInfo& Info = *Pair.Value;
			TArray<int32> InstancesToRemove;
			for (int32 InstanceIdx = 0; InstanceIdx < Info.Instances.Num(); InstanceIdx++)
			{
				InstancesToRemove.Add(InstanceIdx);
			}

			if (InstancesToRemove.Num())
			{
				Info.RemoveInstances(this, InstancesToRemove, true);
			}
		}

	}

	while (FoliageInfos.Remove(nullptr)) {}	//remove entries from the map

}


#endif
//
// Serialize all our UObjects for RTGC 
//
void AInstancedFoliageActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AInstancedFoliageActor* This = CastChecked<AInstancedFoliageActor>(InThis);

	for (auto& Pair : This->FoliageInfos)
	{
		Collector.AddReferencedObject(Pair.Key, This);
		FFoliageInfo& Info = *Pair.Value;

		Info.AddReferencedObjects(This, Collector);
	}

	Super::AddReferencedObjects(This, Collector);
}

#if WITH_EDITOR
bool AInstancedFoliageActor::FoliageTrace(const UWorld* InWorld, FHitResult& OutHit, const FDesiredFoliageInstance& DesiredInstance, FName InTraceTag, bool InbReturnFaceIndex, const FFoliageTraceFilterFunc& FilterFunc)
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageTrace);

	FCollisionQueryParams QueryParams(InTraceTag, SCENE_QUERY_STAT_ONLY(IFA_FoliageTrace), true);
	QueryParams.bReturnFaceIndex = InbReturnFaceIndex;

	//It's possible that with the radius of the shape we will end up with an initial overlap which would place the instance at the top of the procedural volume.
	//Moving the start trace back a bit will fix this, but it introduces the potential for spawning instances a bit above the volume. This second issue is already somewhat broken because of how sweeps work so it's not too bad, also this is a less common case.
	//The proper fix would be to do something like EncroachmentCheck where we first do a sweep, then we fix it up if it's overlapping, then check the filters. This is more expensive and error prone so for now we just move the trace up a bit.
	const FVector Dir = (DesiredInstance.EndTrace - DesiredInstance.StartTrace).GetSafeNormal();
	const FVector StartTrace = DesiredInstance.StartTrace - (Dir * DesiredInstance.TraceRadius);

	TArray<FHitResult> Hits;
	FCollisionShape SphereShape;
	SphereShape.SetSphere(DesiredInstance.TraceRadius);
	InWorld->SweepMultiByObjectType(Hits, StartTrace, DesiredInstance.EndTrace, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), SphereShape, QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		const AActor* HitActor = Hit.GetActor();
				
		// don't place procedural foliage inside an AProceduralFoliageBlockingVolume
		// this test is first because two of the tests below would otherwise cause the trace to ignore AProceduralFoliageBlockingVolume
		if (DesiredInstance.PlacementMode == EFoliagePlacementMode::Procedural)
		{
			if (const AProceduralFoliageBlockingVolume* ProceduralFoliageBlockingVolume = Cast<AProceduralFoliageBlockingVolume>(HitActor))
			{
				const AProceduralFoliageVolume* ProceduralFoliageVolume = ProceduralFoliageBlockingVolume->ProceduralFoliageVolume;
				if (ProceduralFoliageVolume == nullptr || ProceduralFoliageVolume->ProceduralComponent == nullptr || ProceduralFoliageVolume->ProceduralComponent->GetProceduralGuid() == DesiredInstance.ProceduralGuid)
				{
					return false;
				}
			}
			else if (HitActor && HitActor->IsA<AProceduralFoliageVolume>()) //we never want to collide with our spawning volume
			{
				continue;
			}
		}

		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		check(HitComponent);

		// In the editor traces can hit "No Collision" type actors, so ugh. (ignore these)
		if (!HitComponent->IsQueryCollisionEnabled() || HitComponent->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block)
		{
			continue;
		}

		// Don't place foliage on invisible walls / triggers / volumes
		if (HitComponent->IsA<UBrushComponent>())
		{
			continue;
		}

		// Don't place foliage on itself
		const AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(HitActor);
		if (!FoliageActor && HitActor && AInstancedFoliageActor::IsOwnedByFoliage(HitActor))
		{
			FoliageActor = HitActor->GetLevel()->InstancedFoliageActor.Get();
			if (FoliageActor == nullptr)
			{
				continue;
			}

			if (const FFoliageInfo* FoundMeshInfo = FoliageActor->FindInfo(DesiredInstance.FoliageType))
			{
				if (FoundMeshInfo->Implementation->IsOwnedComponent(HitComponent))
				{
					continue;
				}
			}
		}

		if (FilterFunc && FilterFunc(HitComponent) == false)
		{
			// supplied filter does not like this component, so keep iterating
			continue;
		}

		bool bInsideProceduralVolumeOrArentUsingOne = true;
		if (DesiredInstance.PlacementMode == EFoliagePlacementMode::Procedural && DesiredInstance.ProceduralVolumeBodyInstance)
		{
			// We have a procedural volume, so lets make sure we are inside it.
			bInsideProceduralVolumeOrArentUsingOne = DesiredInstance.ProceduralVolumeBodyInstance->OverlapTest(Hit.ImpactPoint, FQuat::Identity, FCollisionShape::MakeSphere(1.f));	//make sphere of 1cm radius to test if we're in the procedural volume
		}

		OutHit = Hit;
			
		// When placing foliage on other foliage, we need to return the base component of the other foliage, not the foliage component, so that it moves correctly
		if (FoliageActor)
		{
			for (auto& Pair : FoliageActor->FoliageInfos)
			{
				const FFoliageInfo& Info = *Pair.Value;

				if (Hit.Item != INDEX_NONE && Info.Implementation->IsOwnedComponent(HitComponent))
				{
					OutHit.Component = CastChecked<UPrimitiveComponent>(FoliageActor->InstanceBaseCache.GetInstanceBasePtr(Info.Instances[Hit.Item].BaseId).Get(), ECastCheckedType::NullAllowed);
					break;
				}
				else
				{
					int32 InstanceIndex = Info.Implementation->FindIndex(HitComponent);
					if (InstanceIndex != INDEX_NONE)
					{
						OutHit.Component = CastChecked<UPrimitiveComponent>(FoliageActor->InstanceBaseCache.GetInstanceBasePtr(Info.Instances[InstanceIndex].BaseId).Get(), ECastCheckedType::NullAllowed);
						break;
					}
				}				
			}

			// The foliage we are snapping on doesn't have a valid base
			if (!OutHit.Component.IsValid())
			{
				continue; 
			}
		}

		return bInsideProceduralVolumeOrArentUsingOne;
	}

	return false;
}

bool AInstancedFoliageActor::CheckCollisionWithWorld(const UWorld* InWorld, const UFoliageType* Settings, const FFoliageInstance& Inst, const FVector& HitNormal, const FVector& HitLocation, UPrimitiveComponent* HitComponent)
{
	if (!Settings->CollisionWithWorld)
	{
		return true;
	}

	FTransform OriginalTransform = Inst.GetInstanceWorldTransform();
	OriginalTransform.SetRotation(FQuat::Identity);

	FMatrix InstTransformNoRotation = OriginalTransform.ToMatrixWithScale();
	OriginalTransform = Inst.GetInstanceWorldTransform();

	// Check for overhanging ledge
	const int32 SamplePositionCount = 4;
	{
		FVector LocalSamplePos[SamplePositionCount] = {
			FVector(Settings->LowBoundOriginRadius.Z, 0, 0),
			FVector(-Settings->LowBoundOriginRadius.Z, 0, 0),
			FVector(0, Settings->LowBoundOriginRadius.Z, 0),
			FVector(0, -Settings->LowBoundOriginRadius.Z, 0)
		};

		for (uint32 i = 0; i < SamplePositionCount; ++i)
		{
			FVector SamplePos = InstTransformNoRotation.TransformPosition(Settings->LowBoundOriginRadius + LocalSamplePos[i]);
			float WorldRadius = (Settings->LowBoundOriginRadius.Z + Settings->LowBoundOriginRadius.Z)*FMath::Max(Inst.DrawScale3D.X, Inst.DrawScale3D.Y);
			FVector NormalVector = Settings->AlignToNormal ? HitNormal : OriginalTransform.GetRotation().GetUpVector();

			//::DrawDebugSphere(InWorld, SamplePos, 10, 6, FColor::Red, true, 30.0f);
			//::DrawDebugSphere(InWorld, SamplePos - NormalVector*WorldRadius, 10, 6, FColor::Orange, true, 30.0f);
			//::DrawDebugDirectionalArrow(InWorld, SamplePos, SamplePos - NormalVector*WorldRadius, 10.0f, FColor::Red, true, 30.0f);

			FHitResult Hit;
			if (AInstancedFoliageActor::FoliageTrace(InWorld, Hit, FDesiredFoliageInstance(SamplePos, SamplePos - NormalVector*WorldRadius)))
			{
				FVector LocalHit = OriginalTransform.InverseTransformPosition(Hit.Location);
				
				if (LocalHit.Z - Inst.ZOffset < Settings->LowBoundOriginRadius.Z && Hit.Component.Get() == HitComponent)
				{
					//::DrawDebugSphere(InWorld, Hit.Location, 6, 6, FColor::Green, true, 30.0f);
					continue;
				}
			}

			//::DrawDebugSphere(InWorld, SamplePos, 6, 6, FColor::Cyan, true, 30.0f);

			return false;
		}
	}

	FBoxSphereBounds LocalBound(Settings->MeshBounds.GetBox());
	FBoxSphereBounds WorldBound = LocalBound.TransformBy(OriginalTransform);

	static FName NAME_FoliageCollisionWithWorld = FName(TEXT("FoliageCollisionWithWorld"));
	if (InWorld->OverlapBlockingTestByChannel(WorldBound.Origin, FQuat(Inst.Rotation), ECC_WorldStatic, FCollisionShape::MakeBox(LocalBound.BoxExtent * Inst.DrawScale3D * Settings->CollisionScale), FCollisionQueryParams(NAME_FoliageCollisionWithWorld, false, HitComponent != nullptr ? HitComponent->GetOwner() : nullptr)))
	{
		return false;
	}

	//::DrawDebugBox(InWorld, WorldBound.Origin, LocalBound.BoxExtent * Inst.DrawScale3D * Settings->CollisionScale, FQuat(Inst.Rotation), FColor::Red, true, 30.f);

	return true;
}

FPotentialInstance::FPotentialInstance(FVector InHitLocation, FVector InHitNormal, UPrimitiveComponent* InHitComponent, float InHitWeight, const FDesiredFoliageInstance& InDesiredInstance)
	: HitLocation(InHitLocation)
	, HitNormal(InHitNormal)
	, HitComponent(InHitComponent)
	, HitWeight(InHitWeight)
	, DesiredInstance(InDesiredInstance)
{
}

bool FPotentialInstance::PlaceInstance(const UWorld* InWorld, const UFoliageType* Settings, FFoliageInstance& Inst, bool bSkipCollision)
{
	if (DesiredInstance.PlacementMode != EFoliagePlacementMode::Procedural)
	{
		Inst.DrawScale3D = Settings->GetRandomScale();
	}
	else
	{
		//Procedural foliage uses age to get the scale
		Inst.DrawScale3D = FVector(Settings->GetScaleForAge(DesiredInstance.Age));
	}

	Inst.ZOffset = Settings->ZOffset.Interpolate(FMath::FRand());

	Inst.Location = HitLocation;

	if (DesiredInstance.PlacementMode != EFoliagePlacementMode::Procedural)
	{
		// Random yaw and optional random pitch up to the maximum
		Inst.Rotation = FRotator(FMath::FRand() * Settings->RandomPitchAngle, 0.f, 0.f);

		if (Settings->RandomYaw)
		{
			Inst.Rotation.Yaw = FMath::FRand() * 360.f;
		}
		else
		{
			Inst.Flags |= FOLIAGE_NoRandomYaw;
		}
	}
	else
	{
		Inst.Rotation = DesiredInstance.Rotation.Rotator();
		Inst.Flags |= FOLIAGE_NoRandomYaw;
	}


	if (Settings->AlignToNormal)
	{
		Inst.AlignToNormal(HitNormal, Settings->AlignMaxAngle);
	}

	// Apply the Z offset in local space
	if (FMath::Abs(Inst.ZOffset) > KINDA_SMALL_NUMBER)
	{
		Inst.Location = Inst.GetInstanceWorldTransform().TransformPosition(FVector(0, 0, Inst.ZOffset));
	}

	UModelComponent* ModelComponent = Cast<UModelComponent>(HitComponent);
	if (ModelComponent)
	{
		ABrush* BrushActor = ModelComponent->GetModel()->FindBrush(HitLocation);
		if (BrushActor)
		{
			HitComponent = BrushActor->GetBrushComponent();
		}
	}

	return bSkipCollision || AInstancedFoliageActor::CheckCollisionWithWorld(InWorld, Settings, Inst, HitNormal, HitLocation, HitComponent);
}
#endif

float AInstancedFoliageActor::InternalTakeRadialDamage(float Damage, struct FRadialDamageEvent const& RadialDamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	// Radial damage scaling needs to be applied per instance so we don't do anything here
	return Damage;
}

UFoliageInstancedStaticMeshComponent::UFoliageInstancedStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bEnableAutoLODGeneration = false;
#endif
}

void UFoliageInstancedStaticMeshComponent::ReceiveComponentDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (DamageAmount != 0.f)
	{
		UDamageType const* const DamageTypeCDO = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
		if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			// Point damage event, hit a single instance.
			FPointDamageEvent* const PointDamageEvent = (FPointDamageEvent*)&DamageEvent;
			if (PerInstanceSMData.IsValidIndex(PointDamageEvent->HitInfo.Item))
			{
				OnInstanceTakePointDamage.Broadcast(PointDamageEvent->HitInfo.Item, DamageAmount, EventInstigator, PointDamageEvent->HitInfo.ImpactPoint, PointDamageEvent->ShotDirection, DamageTypeCDO, DamageCauser);
			}
		}
		else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			// Radial damage event, find which instances it hit and notify
			FRadialDamageEvent* const RadialDamageEvent = (FRadialDamageEvent*)&DamageEvent;

			float MaxRadius = RadialDamageEvent->Params.GetMaxRadius();
			TArray<int32> Instances = GetInstancesOverlappingSphere(RadialDamageEvent->Origin, MaxRadius, true);

			if (Instances.Num())
			{
				FVector LocalOrigin = GetComponentToWorld().Inverse().TransformPosition(RadialDamageEvent->Origin);
				float Scale = GetComponentScale().X; // assume component (not instances) is uniformly scaled

				TArray<float> Damages;
				Damages.Empty(Instances.Num());

				for (int32 InstanceIndex : Instances)
				{
					// Find distance in local space and then scale; quicker than transforming each instance to world space.
					float DistanceFromOrigin = (PerInstanceSMData[InstanceIndex].Transform.GetOrigin() - LocalOrigin).Size() * Scale;
					Damages.Add(RadialDamageEvent->Params.GetDamageScale(DistanceFromOrigin));
				}

				OnInstanceTakeRadialDamage.Broadcast(Instances, Damages, EventInstigator, RadialDamageEvent->Origin, MaxRadius, DamageTypeCDO, DamageCauser);
			}
		}
	}
}

#if WITH_EDITOR

uint64 UFoliageInstancedStaticMeshComponent::GetHiddenEditorViews() const
{
	return FoliageHiddenEditorViews;
}

#endif// WITH_EDITOR

#undef LOCTEXT_NAMESPACE
