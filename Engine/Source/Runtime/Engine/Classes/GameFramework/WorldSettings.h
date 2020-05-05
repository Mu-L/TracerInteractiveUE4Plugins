// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "GameFramework/Actor.h"
#include "Engine/MeshMerging.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Info.h"
#include "Sound/AudioVolume.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldSettings.generated.h"

class UAssetUserData;
class UNetConnection;
class UNavigationSystemConfig;

UENUM()
enum EVisibilityAggressiveness
{
	VIS_LeastAggressive UMETA(DisplayName = "Least Aggressive"),
	VIS_ModeratelyAggressive UMETA(DisplayName = "Moderately Aggressive"),
	VIS_MostAggressive UMETA(DisplayName = "Most Aggressive"),
	VIS_Max UMETA(Hidden),
};

UENUM()
enum EVolumeLightingMethod
{
	/** 
	 * Lighting samples are computed in an adaptive grid which covers the entire Lightmass Importance Volume.  Higher density grids are used near geometry.
	 * The Volumetric Lightmap is interpolated efficiently on the GPU per-pixel, allowing accurate indirect lighting for dynamic objects and volumetric fog.
	 * Positions outside of the Importance Volume reuse the border texels of the Volumetric Lightmap (clamp addressing).
	 * On mobile, interpolation is done on the CPU at the center of each object's bounds.
	 */
	VLM_VolumetricLightmap UMETA(DisplayName = "Volumetric Lightmap"),

	/** 
	 * Volume lighting samples are placed on top of static surfaces at medium density, and everywhere else in the Lightmass Importance Volume at low density.  Positions outside of the Importance Volume will have no indirect lighting.
	 * This method requires CPU interpolation so the Indirect Lighting Cache is used to interpolate results for each dynamic object, adding Rendering Thread overhead.  
	 * Volumetric Fog cannot be affected by precomputed lighting with this method.
	 */
	VLM_SparseVolumeLightingSamples UMETA(DisplayName = "Sparse Volume Lighting Samples"),
};

USTRUCT()
struct FLightmassWorldInfoSettings
{
	GENERATED_USTRUCT_BODY()

	/** 
	 * Warning: Setting this to less than 1 will greatly increase build times!
	 * Scale of the level relative to real world scale (1 Unreal Unit = 1 cm). 
	 * All scale-dependent Lightmass setting defaults have been tweaked to work well with real world scale, 
	 * Any levels with a different scale should use this scale to compensate. 
	 * For large levels it can drastically reduce build times to set this to 2 or 4.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, AdvancedDisplay, meta=(UIMin = "1.0", UIMax = "4.0"))
	float StaticLightingLevelScale;

	/** 
	 * Number of light bounces to simulate for point / spot / directional lights, starting from the light source. 
	 * 0 is direct lighting only, 1 is one bounce, etc. 
	 * Bounce 1 takes the most time to calculate and contributes the most to visual quality, followed by bounce 2.
	 * Successive bounces don't really affect build times, but have a much lower visual impact, unless the material diffuse colors are close to 1.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, meta=(UIMin = "1.0", UIMax = "10.0"))
	int32 NumIndirectLightingBounces;

	/** 
	 * Number of skylight and emissive bounces to simulate.  
	 * Lightmass uses a non-distributable radiosity method for skylight bounces whose cost is proportional to the number of bounces.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, meta=(UIMin = "1.0", UIMax = "10.0"))
	int32 NumSkyLightingBounces;

	/** 
	 * Warning: Setting this higher than 1 will greatly increase build times!
	 * Can be used to increase the GI solver sample counts in order to get higher quality for levels that need it.
	 * It can be useful to reduce IndirectLightingSmoothness somewhat (~.75) when increasing quality to get defined indirect shadows.
	 * Note that this can't affect compression artifacts, UV seams or other texture based artifacts.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, AdvancedDisplay, meta=(UIMin = "1.0", UIMax = "4.0"))
	float IndirectLightingQuality;

	/** 
	 * Smoothness factor to apply to indirect lighting.  This is useful in some lighting conditions when Lightmass cannot resolve accurate indirect lighting.
	 * 1 is default smoothness tweaked for a variety of lighting situations.
	 * Higher values like 3 smooth out the indirect lighting more, but at the cost of indirect shadows losing detail.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, AdvancedDisplay, meta=(UIMin = "0.5", UIMax = "6.0"))
	float IndirectLightingSmoothness;

	/** 
	 * Represents a constant color light surrounding the upper hemisphere of the level, like a sky.
	 * This light source currently does not get bounced as indirect lighting and causes reflection capture brightness to be incorrect.  Prefer using a Static Skylight instead.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral)
	FColor EnvironmentColor;

	/** Scales EnvironmentColor to allow independent color and brightness controls. */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, meta=(UIMin = "0", UIMax = "10"))
	float EnvironmentIntensity;

	/** Scales the emissive contribution of all materials in the scene.  Currently disabled and should be removed with mesh area lights. */
	UPROPERTY()
	float EmissiveBoost;

	/** Scales the diffuse contribution of all materials in the scene. */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, meta=(UIMin = "0.1", UIMax = "6.0"))
	float DiffuseBoost;

	/** Technique to use for providing precomputed lighting at all positions inside the Lightmass Importance Volume */
	UPROPERTY(EditAnywhere, Category=LightmassVolumeLighting)
	TEnumAsByte<enum EVolumeLightingMethod> VolumeLightingMethod;

	/** If true, AmbientOcclusion will be enabled. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion)
	uint8 bUseAmbientOcclusion:1;

	/** 
	 * Whether to generate textures storing the AO computed by Lightmass.
	 * These can be accessed through the PrecomputedAOMask material node, 
	 * Which is useful for blending between material layers on environment assets.
	 * Be sure to set DirectIlluminationOcclusionFraction and IndirectIlluminationOcclusionFraction to 0 if you only want the PrecomputedAOMask!
	 */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion)
	uint8 bGenerateAmbientOcclusionMaterialMask:1;

	/** If true, override normal direct and indirect lighting with just the exported diffuse term. */
	UPROPERTY(EditAnywhere, Category=LightmassDebug, AdvancedDisplay)
	uint8 bVisualizeMaterialDiffuse:1;

	/** If true, override normal direct and indirect lighting with just the AO term. */
	UPROPERTY(EditAnywhere, Category=LightmassDebug, AdvancedDisplay)
	uint8 bVisualizeAmbientOcclusion:1;

	/** 
	 * Whether to compress lightmap textures.  Disabling lightmap texture compression will reduce artifacts but increase memory and disk size by 4x.
	 * Use caution when disabling this.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassGeneral, AdvancedDisplay)
	uint8 bCompressLightmaps:1;

	/** 
	 * Size of an Volumetric Lightmap voxel at the highest density (used around geometry), in world space units. 
	 * This setting has a large impact on build times and memory, use with caution.  
	 * Halving the DetailCellSize can increase memory by up to a factor of 8x.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassVolumeLighting, meta=(UIMin = "50", UIMax = "1000"))
	float VolumetricLightmapDetailCellSize;

	/** 
	 * Maximum amount of memory to spend on Volumetric Lightmap Brick data.  High density bricks will be discarded until this limit is met, with bricks furthest from geometry discarded first.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassVolumeLighting, meta=(UIMin = "1", UIMax = "500"))
	float VolumetricLightmapMaximumBrickMemoryMb;

	/** 
	 * Controls how much smoothing should be done to Volumetric Lightmap samples during Spherical Harmonic de-ringing.  
	 * Whenever highly directional lighting is stored in a Spherical Harmonic, a ringing artifact occurs which manifests as unexpected black areas on the opposite side.
	 * Smoothing can reduce this artifact.  Smoothing is only applied when the ringing artifact is present.
	 * 0 = no smoothing, 1 = strong smooth (little directionality in lighting).
	 */
	UPROPERTY(EditAnywhere, Category=LightmassVolumeLighting, meta=(UIMin = "0", UIMax = "1"))
	float VolumetricLightmapSphericalHarmonicSmoothing;

	/** 
	 * Scales the distances at which volume lighting samples are placed.  Volume lighting samples are computed by Lightmass and are used for GI on movable components.
	 * Using larger scales results in less sample memory usage and reduces Indirect Lighting Cache update times, but less accurate transitions between lighting areas.
	 */
	UPROPERTY(EditAnywhere, Category=LightmassVolumeLighting, AdvancedDisplay, meta=(UIMin = "0.1", UIMax = "100.0"))
	float VolumeLightSamplePlacementScale;

	/** How much of the AO to apply to direct lighting. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion, meta=(UIMin = "0", UIMax = "1"))
	float DirectIlluminationOcclusionFraction;

	/** How much of the AO to apply to indirect lighting. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion, meta=(UIMin = "0", UIMax = "1"))
	float IndirectIlluminationOcclusionFraction;

	/** Higher exponents increase contrast. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion, meta=(UIMin = ".5", UIMax = "8"))
	float OcclusionExponent;

	/** Fraction of samples taken that must be occluded in order to reach full occlusion. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion, meta=(UIMin = "0", UIMax = "1"))
	float FullyOccludedSamplesFraction;

	/** Maximum distance for an object to cause occlusion on another object. */
	UPROPERTY(EditAnywhere, Category=LightmassOcclusion)
	float MaxOcclusionDistance;

	FLightmassWorldInfoSettings()
		: StaticLightingLevelScale(1)
		, NumIndirectLightingBounces(3)
		, NumSkyLightingBounces(1)
		, IndirectLightingQuality(1)
		, IndirectLightingSmoothness(1)
		, EnvironmentColor(ForceInit)
		, EnvironmentIntensity(1.0f)
		, EmissiveBoost(1.0f)
		, DiffuseBoost(1.0f)
		, VolumeLightingMethod(VLM_VolumetricLightmap)
		, bUseAmbientOcclusion(false)
		, bGenerateAmbientOcclusionMaterialMask(false)
		, bVisualizeMaterialDiffuse(false)
		, bVisualizeAmbientOcclusion(false)
		, bCompressLightmaps(true)
		, VolumetricLightmapDetailCellSize(200)
		, VolumetricLightmapMaximumBrickMemoryMb(30)
		, VolumetricLightmapSphericalHarmonicSmoothing(.02f)
		, VolumeLightSamplePlacementScale(1)
		, DirectIlluminationOcclusionFraction(0.5f)
		, IndirectIlluminationOcclusionFraction(1.0f)
		, OcclusionExponent(1.0f)
		, FullyOccludedSamplesFraction(1.0f)
		, MaxOcclusionDistance(200.0f)
	{
	}
};

/** stores information on a viewer that actors need to be checked against for relevancy */
USTRUCT()
struct ENGINE_API FNetViewer
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UNetConnection* Connection;

	/** The "controlling net object" associated with this view (typically player controller) */
	UPROPERTY()
	class AActor* InViewer;

	/** The actor that is being directly viewed, usually a pawn.  Could also be the net actor of consequence */
	UPROPERTY()
	class AActor* ViewTarget;

	/** Where the viewer is looking from */
	UPROPERTY()
	FVector ViewLocation;

	/** Direction the viewer is looking */
	UPROPERTY()
	FVector ViewDir;

	FNetViewer()
		: Connection(NULL)
		, InViewer(NULL)
		, ViewTarget(NULL)
		, ViewLocation(ForceInit)
		, ViewDir(ForceInit)
	{
	}

	FNetViewer(UNetConnection* InConnection, float DeltaSeconds);
};

USTRUCT()
struct ENGINE_API FHierarchicalSimplification
{
	GENERATED_USTRUCT_BODY()

	/** The screen radius an mesh object should reach before swapping to the LOD actor, once one of parent displays, it won't draw any of children. */
	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, meta = (UIMin = "0.00001", ClampMin = "0.000001", UIMax = "1.0", ClampMax = "1.0"))
	float TransitionScreenSize;

	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, AdvancedDisplay, meta = (UIMin = "1.0", ClampMin = "1.0", UIMax = "50000.0", editcondition="bUseOverrideDrawDistance"))
	float OverrideDrawDistance;

	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, AdvancedDisplay, meta = (InlineEditConditionToggle))
	uint8 bUseOverrideDrawDistance:1;

	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, AdvancedDisplay)
	uint8 bAllowSpecificExclusion : 1;

	/** If this is true, it will simplify mesh but it is slower.
	* If false, it will just merge actors but not simplify using the lower LOD if exists.
	* For example if you build LOD 1, it will use LOD 1 of the mesh to merge actors if exists.
	* If you merge material, it will reduce drawcalls.
	*/
	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere)
	uint8 bSimplifyMesh:1;

	/** Only generate clusters for HLOD volumes */
	UPROPERTY(EditAnywhere, Category = FHierarchicalSimplification, AdvancedDisplay, meta = (editcondition = "!bReusePreviousLevelClusters", DisplayAfter="MinNumberOfActorsToBuild"))
	uint8 bOnlyGenerateClustersForVolumes:1;

	/** Will reuse the clusters generated for the previous (lower) HLOD level */
	UPROPERTY(EditAnywhere, Category = FHierarchicalSimplification, AdvancedDisplay, meta=(DisplayAfter="bOnlyGenerateClustersForVolumes"))
	uint8 bReusePreviousLevelClusters:1;

	/** Simplification Setting if bSimplifyMesh is true */
	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, AdvancedDisplay)
	FMeshProxySettings ProxySetting;

	/** Merge Mesh Setting if bSimplifyMesh is false */
	UPROPERTY(Category = FHierarchicalSimplification, EditAnywhere, AdvancedDisplay)
	FMeshMergingSettings MergeSetting;

	/** Desired Bounding Radius for clustering - this is not guaranteed but used to calculate filling factor for auto clustering */
	UPROPERTY(EditAnywhere, Category=FHierarchicalSimplification, AdvancedDisplay, meta=(UIMin=10.f, ClampMin=10.f, editcondition = "!bReusePreviousLevelClusters"))
	float DesiredBoundRadius;

	/** Desired Filling Percentage for clustering - this is not guaranteed but used to calculate filling factor  for auto clustering */
	UPROPERTY(EditAnywhere, Category=FHierarchicalSimplification, AdvancedDisplay, meta=(ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "100", editcondition = "!bReusePreviousLevelClusters"))
	float DesiredFillingPercentage;

	/** Min number of actors to build LODActor */
	UPROPERTY(EditAnywhere, Category=FHierarchicalSimplification, AdvancedDisplay, meta=(ClampMin = "1", UIMin = "1", editcondition = "!bReusePreviousLevelClusters"))
	int32 MinNumberOfActorsToBuild;	

	FHierarchicalSimplification()
		: TransitionScreenSize(0.315f)
		, OverrideDrawDistance(10000)
		, bUseOverrideDrawDistance(false)
		, bAllowSpecificExclusion(false)
		, bSimplifyMesh(false)
		, bOnlyGenerateClustersForVolumes(false)
		, bReusePreviousLevelClusters(false)
		, DesiredBoundRadius(2000)
		, DesiredFillingPercentage(50)
		, MinNumberOfActorsToBuild(2)
	{
		MergeSetting.bMergeMaterials = true;
		MergeSetting.bGenerateLightMapUV = true;
		ProxySetting.MaterialSettings.MaterialMergeType = EMaterialMergeType::MaterialMergeType_Simplygon;
		ProxySetting.bCreateCollision = false;
	}
};

UCLASS(Blueprintable)
class ENGINE_API UHierarchicalLODSetup : public UObject
{
	GENERATED_BODY()
public:
	UHierarchicalLODSetup()
	{
		HierarchicalLODSetup.AddDefaulted();
		OverrideBaseMaterial = nullptr;
	}

	/** Hierarchical LOD Setup */
	UPROPERTY(EditAnywhere, Category = HLODSystem)
	TArray<struct FHierarchicalSimplification> HierarchicalLODSetup;

	UPROPERTY(EditAnywhere, Category = HLODSystem)
	TSoftObjectPtr<UMaterialInterface> OverrideBaseMaterial;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

/** Settings pertaining to which PhysX broadphase to use, and settings for MBP if that is the chosen broadphase type */
USTRUCT()
struct FBroadphaseSettings
{
	GENERATED_BODY();

	FBroadphaseSettings()
		: bUseMBPOnClient(false)
		, bUseMBPOnServer(false)
		, bUseMBPOuterBounds(false)
		, MBPBounds(EForceInit::ForceInitToZero)
		, MBPOuterBounds(EForceInit::ForceInitToZero)
		, MBPNumSubdivs(2)
	{

	}

	/** Whether to use MBP (Multi Broadphase Pruning */
	UPROPERTY(EditAnywhere, Category = Broadphase)
	bool bUseMBPOnClient;

	UPROPERTY(EditAnywhere, Category = Broadphase)
	bool bUseMBPOnServer;

	/** Whether to have MBP grid over concentrated inner bounds with loose outer bounds */
	UPROPERTY(EditAnywhere, Category = Broadphase)
	bool bUseMBPOuterBounds;

	/** Total bounds for MBP, must cover the game world or collisions are disabled for out of bounds actors */
	UPROPERTY(EditAnywhere, Category = Broadphase, meta = (EditCondition = "bUseMBPOnClient || bUseMBPOnServer"))
	FBox MBPBounds;

	/** Total bounds for MBP, should cover absolute maximum bounds of the game world where physics is required */
	UPROPERTY(EditAnywhere, Category = Broadphase, meta = (EditCondition = "bUseMBPOnClient || bUseMBPOnServer"))
	FBox MBPOuterBounds;

	/** Number of times to subdivide the MBP bounds, final number of regions is MBPNumSubdivs^2 */
	UPROPERTY(EditAnywhere, Category = Broadphase, meta = (EditCondition = "bUseMBPOnClient || bUseMBPOnServer", ClampMin=1, ClampMax=16))
	uint32 MBPNumSubdivs;
};

/**
 * Actor containing all script accessible world properties.
 */
UCLASS(config=game, hidecategories=(Actor, Advanced, Display, Events, Object, Attachment, Info, Input, Blueprint, Layers, Tags, Replication), showcategories=(Rendering, "Input|MouseInput", "Input|TouchInput"), notplaceable)
class ENGINE_API AWorldSettings : public AInfo, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const override;

	/** PRECOMPUTED VISIBILITY SETTINGS **/

	/** 
	 * World space size of precomputed visibility cells in x and y.
	 * Smaller sizes produce more effective occlusion culling at the cost of increased runtime memory usage and lighting build times.
	 */
	UPROPERTY(EditAnywhere, Category=PrecomputedVisibility, AdvancedDisplay, meta=(DisplayAfter="bPlaceCellsOnlyAlongCameraTracks"))
	int32 VisibilityCellSize;

	/** 
	 * Determines how aggressive precomputed visibility should be.
	 * More aggressive settings cull more objects but also cause more visibility errors like popping.
	 */
	UPROPERTY(EditAnywhere, Category=PrecomputedVisibility, AdvancedDisplay, meta=(DisplayAfter="bPlaceCellsOnlyAlongCameraTracks"))
	TEnumAsByte<enum EVisibilityAggressiveness> VisibilityAggressiveness;

	/** 
	 * Whether to place visibility cells inside Precomputed Visibility Volumes and along camera tracks in this level. 
	 * Precomputing visibility reduces rendering thread time at the cost of some runtime memory and somewhat increased lighting build times.
	 */
	UPROPERTY(EditAnywhere, Category=PrecomputedVisibility)
	uint8 bPrecomputeVisibility:1;

	/** 
	 * Whether to place visibility cells only along camera tracks or only above shadow casting surfaces.
	 */
	UPROPERTY(EditAnywhere, Category=PrecomputedVisibility, AdvancedDisplay)
	uint8 bPlaceCellsOnlyAlongCameraTracks:1;

	/** DEFAULT BASIC PHYSICS SETTINGS **/

	/** If true, enables CheckStillInWorld checks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, AdvancedDisplay)
	uint8 bEnableWorldBoundsChecks:1;

protected:
	/** if set to false navigation system will not get created (and all 
	 *	navigation functionality won't be accessible).
	 *	This flag is now deprecated. Use NavigationSystemConfig property to 
	 *	determine navigation system's properties or existence */
	UE_DEPRECATED(4.22, "This member will be removed. Please use NavigationSystemConfig instead.")
	UPROPERTY(BlueprintReadOnly, config, Category = World, meta=(DisplayName = "DEPRECATED_bEnableNavigationSystem"))
	uint8 bEnableNavigationSystem:1;

public:
	/** if set to false AI system will not get created. Use it to disable all AI-related activity on a map */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category=AI, AdvancedDisplay)
	uint8 bEnableAISystem:1;

	/** 
	 * Enables tools for composing a tiled world. 
	 * Level has to be saved and all sub-levels removed before enabling this option.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World)
	uint8 bEnableWorldComposition:1;
		
	/**
	 * Enables client-side streaming volumes instead of server-side.
	 * Expected usage scenario: server has all streaming levels always loaded, clients independently stream levels in/out based on streaming volumes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = World)
	uint8 bUseClientSideLevelStreamingVolumes:1;

	/** World origin will shift to a camera position when camera goes far away from current origin */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, AdvancedDisplay, meta=(editcondition = "bEnableWorldComposition"))
	uint8 bEnableWorldOriginRebasing:1;
		
	/** if set to true, when we call GetGravityZ we assume WorldGravityZ has already been initialized and skip the lookup of DefaultGravityZ and GlobalGravityZ */
	UPROPERTY(transient)
	uint8 bWorldGravitySet:1;

	/** If set to true we will use GlobalGravityZ instead of project setting DefaultGravityZ */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayName = "Override World Gravity"), Category = Physics)
	uint8 bGlobalGravitySet:1;

	/** 
	 * Causes the BSP build to generate as few sections as possible.
	 * This is useful when you need to reduce draw calls but can reduce texture streaming efficiency and effective lightmap resolution.
	 * Note - changes require a rebuild to propagate.  Also, be sure to select all surfaces and make sure they all have the same flags to minimize section count.
	 */
	UPROPERTY(EditAnywhere, Category=World, AdvancedDisplay)
	uint8 bMinimizeBSPSections:1;

	/** 
	 * Whether to force lightmaps and other precomputed lighting to not be created even when the engine thinks they are needed.
	 * This is useful for improving iteration in levels with fully dynamic lighting and shadowing.
	 * Note that any lighting and shadowing interactions that are usually precomputed will be lost if this is enabled.
	 */
	UPROPERTY(EditAnywhere, Category=Lightmass, AdvancedDisplay)
	uint8 bForceNoPrecomputedLighting:1;

	/** when this flag is set, more time is allocated to background loading (replicated) */
	UPROPERTY(replicated)
	uint8 bHighPriorityLoading:1;

	/** copy of bHighPriorityLoading that is not replicated, for clientside-only loading operations */
	UPROPERTY()
	uint8 bHighPriorityLoadingLocal:1;

	UPROPERTY(config, EditAnywhere, Category = Broadphase)
	uint8 bOverrideDefaultBroadphaseSettings:1;

protected:
	/** Holds parameters for NavigationSystem's creation. Set to Null will result
	 *	in NavigationSystem instance not being created for this world. Note that
	 *	if set NavigationSystemConfigOverride will be used instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, AdvancedDisplay, Instanced, NoClear, meta=(NoResetToDefault))
	UNavigationSystemConfig* NavigationSystemConfig;

	/** Overrides NavigationSystemConfig. */
	UPROPERTY(Transient)
	UNavigationSystemConfig* NavigationSystemConfigOverride;

public:

	/** scale of 1uu to 1m in real world measurements, for HMD and other physically tracked devices (e.g. 1uu = 1cm would be 100.0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=VR)
	float WorldToMeters;

	// any actor falling below this level gets destroyed
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, meta=(editcondition = "bEnableWorldBoundsChecks"))
	float KillZ;

	// The type of damage inflicted when a actor falls below KillZ
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, AdvancedDisplay, meta=(editcondition = "bEnableWorldBoundsChecks"))
	TSubclassOf<UDamageType> KillZDamageType;

	// current gravity actually being used
	UPROPERTY(transient, ReplicatedUsing=OnRep_WorldGravityZ)
	float WorldGravityZ;

	UFUNCTION()
	virtual void OnRep_WorldGravityZ();

	// optional level specific gravity override set by level designer
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Physics, meta=(editcondition = "bGlobalGravitySet"))
	float GlobalGravityZ;

	// level specific default physics volume
	UPROPERTY(EditAnywhere, noclear, BlueprintReadOnly, Category=Physics, AdvancedDisplay)
	TSubclassOf<class ADefaultPhysicsVolume> DefaultPhysicsVolumeClass;

	// optional level specific collision handler
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Physics, AdvancedDisplay)
	TSubclassOf<class UPhysicsCollisionHandler>	PhysicsCollisionHandlerClass;

	/************************************/
	
	/** GAMEMODE SETTINGS **/
	
	/** The default GameMode to use when starting this map in the game. If this value is NULL, the INI setting for default game type is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=GameMode, meta=(DisplayName="GameMode Override"))
	TSubclassOf<class AGameModeBase> DefaultGameMode;

	/** Class of GameNetworkManager to spawn for network games */
	UPROPERTY()
	TSubclassOf<class AGameNetworkManager> GameNetworkManagerClass;

	/************************************/
	
	/** RENDERING SETTINGS **/
	/** Maximum size of textures for packed light and shadow maps */
	UPROPERTY(EditAnywhere, Category=Lightmass, AdvancedDisplay)
	int32 PackedLightAndShadowMapTextureSize;

	/** Default color scale for the level */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=World, AdvancedDisplay)
	FVector DefaultColorScale;

	/** Max occlusion distance used by mesh distance fields, overridden if there is a movable skylight. */
	UPROPERTY(EditAnywhere, Category=Rendering, meta=(UIMin = "500", UIMax = "5000", DisplayName = "Default Max DistanceField Occlusion Distance"))
	float DefaultMaxDistanceFieldOcclusionDistance;

	/** Distance from the camera that the global distance field should cover. */
	UPROPERTY(EditAnywhere, Category=Rendering, meta=(UIMin = "10000", UIMax = "100000", DisplayName = "Global DistanceField View Distance"))
	float GlobalDistanceFieldViewDistance;

	/** 
	 * Controls the intensity of self-shadowing from capsule indirect shadows. 
	 * These types of shadows use approximate occluder representations, so reducing self-shadowing intensity can hide those artifacts.
	 */
	UPROPERTY(EditAnywhere, config, Category=Rendering, meta=(UIMin = "0", UIMax = "1"))
	float DynamicIndirectShadowsSelfShadowingIntensity;

#if WITH_EDITORONLY_DATA
	/************************************/
	
	/** LIGHTMASS RELATED SETTINGS **/
	
	UPROPERTY(EditAnywhere, Category=Lightmass)
	struct FLightmassWorldInfoSettings LightmassSettings;
#endif

	/************************************/
	/** AUDIO SETTINGS **/
	/** Default reverb settings used by audio volumes.													*/
	UPROPERTY(EditAnywhere, config, Category=Audio)
	FReverbSettings DefaultReverbSettings;

	/** Default interior settings used by audio volumes.												*/
	UPROPERTY(EditAnywhere, config, Category=Audio)
	FInteriorSettings DefaultAmbientZoneSettings;

	/** Distance from the player after which content will be rendered in mono if monoscopic far field rendering is activated */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=VR)
	float MonoCullingDistance;

	/** Default Base SoundMix.																			*/
	UPROPERTY(EditAnywhere, Category=Audio)
	class USoundMix* DefaultBaseSoundMix;

#if WITH_EDITORONLY_DATA
	/** if set to true, hierarchical LODs will be built, which will create hierarchical LODActors*/
	UPROPERTY(EditAnywhere, config, Category=LODSystem)
	uint32 bEnableHierarchicalLODSystem:1;

	/** If set overrides the level settings and global project settings */
	UPROPERTY(EditAnywhere, config, Category = LODSystem)
	TSoftClassPtr<class UHierarchicalLODSetup> HLODSetupAsset;

	/** If set overrides the project-wide base material used for Proxy Materials*/
	UPROPERTY(EditAnywhere, config, Category = LODSystem, meta=(editcondition = "bEnableHierarchicalLODSystem && HLODSetupAsset == nullptr"))
	TSoftObjectPtr<class UMaterialInterface> OverrideBaseMaterial;	

protected:
	/** Hierarchical LOD Setup */
	UPROPERTY(EditAnywhere, Category=LODSystem, config, meta=(editcondition = "bEnableHierarchicalLODSystem && HLODSetupAsset == nullptr"))
	TArray<struct FHierarchicalSimplification>	HierarchicalLODSetup;

public:
	UPROPERTY()
	int32 NumHLODLevels;

	/** if set to true, all eligible actors in this level will be added to a single cluster representing the entire level (used for small sublevels)*/
	UPROPERTY(EditAnywhere, config, Category = LODSystem, AdvancedDisplay)
	uint32 bGenerateSingleClusterForLevel : 1;

	/************************************/
	/** EDITOR ONLY SETTINGS **/

	/** Level Bookmarks: 10 should be MAX_BOOKMARK_NUMBER @fixmeconst */
	UE_DEPRECATED(4.21, "This member will be removed. Please use the Bookmark accessor functions instead.")
	UPROPERTY()
	class UBookMark* BookMarks[10];
#endif

	/************************************/

	/** 
	 * Normally 1 - scales real time passage.
	 * Warning - most use cases should use GetEffectiveTimeDilation() instead of reading from this directly
	 */
	UPROPERTY(transient, replicated)
	float TimeDilation;

	// Additional time dilation used by Matinee (or Sequencer) slomo track.  Transient because this is often 
	// temporarily modified by the editor when previewing slow motion effects, yet we don't want it saved or loaded from level packages.
	UPROPERTY(transient, replicated)
	float MatineeTimeDilation;

	// Additional TimeDilation used to control demo playback speed
	UPROPERTY(transient)
	float DemoPlayTimeDilation;

	/** Lowest acceptable global time dilation. */
	UPROPERTY(config, EditAnywhere, Category = Tick, AdvancedDisplay, meta = (UIMin = "0", ClampMin = "0"))
	float MinGlobalTimeDilation;
	
	/** Highest acceptable global time dilation. */
	UPROPERTY(config, EditAnywhere, Category = Tick, AdvancedDisplay, meta = (UIMin = "0", ClampMin = "0"))
	float MaxGlobalTimeDilation;

	/** Smallest possible frametime, not considering dilation. Equiv to 1/FastestFPS. */
	UPROPERTY(config, EditAnywhere, Category = Tick, AdvancedDisplay, meta = (UIMin = "0", ClampMin = "0"))
	float MinUndilatedFrameTime;

	/** Largest possible frametime, not considering dilation. Equiv to 1/SlowestFPS. */
	UPROPERTY(config, EditAnywhere, Category = Tick, AdvancedDisplay, meta = (UIMin = "0", ClampMin = "0"))
	float MaxUndilatedFrameTime;

	UPROPERTY(config, EditAnywhere, Category = Broadphase)
	FBroadphaseSettings BroadphaseSettings;

	// If paused, FName of person pausing the game.
	UE_DEPRECATED(4.23, "This property is deprecated. Please use Get/SetPauserPlayerState().")
	UPROPERTY(transient)
	class APlayerState* Pauser;

	/** valid only during replication - information about the player(s) being replicated to
	 * (there could be more than one in the case of a splitscreen client)
	 */
	UPROPERTY()
	TArray<struct FNetViewer> ReplicationViewers;

	// ************************************

	/** Maximum number of bookmarks	*/
	UE_DEPRECATED(4.21, "Please use GetMaxNumberOfBookmarks or NumMappedBookmarks instead.")
	static const int32 MAX_BOOKMARK_NUMBER = 10;

	protected:

	/** Array of user data stored with the asset */
	UPROPERTY()
	TArray<UAssetUserData*> AssetUserData;

	// If paused, PlayerState of person pausing the game.
	UPROPERTY(transient, replicated)
	class APlayerState* PauserPlayerState;

public:
	//~ Begin UObject Interface.
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.


	//~ Begin AActor Interface.
#if WITH_EDITOR
	virtual void CheckForErrors() override;
	virtual bool IsSelectable() const override { return false; }
#endif // WITH_EDITOR
	virtual void PostInitProperties() override;
	virtual void PreInitializeComponents() override;
	virtual void PostRegisterAllComponents() override;
	//~ End AActor Interface.

	/**
	 * Returns the Z component of the current world gravity and initializes it to the default
	 * gravity if called for the first time.
	 *
	 * @return Z component of current world gravity.
	 */
	virtual float GetGravityZ() const;

	virtual float GetEffectiveTimeDilation() const
	{
		return TimeDilation * MatineeTimeDilation * DemoPlayTimeDilation;
	}

	/**
	 * Returns the delta time to be used by the tick. Can be overridden if game specific logic is needed.
	 */
	virtual float FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds);

	/** Sets the global time dilation value (subject to clamping). Returns the final value that was set. */
	virtual float SetTimeDilation(float NewTimeDilation);

	/** @return configuration for NavigationSystem's creation. Null means 
	 *	no navigation system will be created*/
	UNavigationSystemConfig* const GetNavigationSystemConfig() const { return NavigationSystemConfigOverride ? NavigationSystemConfigOverride : NavigationSystemConfig; }

	void SetNavigationSystemConfigOverride(UNavigationSystemConfig* NewConfig);

	/** @return whether given world is configured to host any NavigationSystem */
	bool IsNavigationSystemEnabled() const;

	/**
	 * Called from GameStateBase, calls BeginPlay on all actors
	 */
	virtual void NotifyBeginPlay();

	/** 
	 * Called from GameStateBase, used to notify native classes of match startup (such as level scripting)
	 */	
	virtual void NotifyMatchStarted();

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	//~ End IInterface_AssetUserData Interface

#if WITH_EDITOR
	const TArray<struct FHierarchicalSimplification>& GetHierarchicalLODSetup() const;
	TArray<struct FHierarchicalSimplification>& GetHierarchicalLODSetup();
	int32 GetNumHierarchicalLODLevels() const;
	UMaterialInterface* GetHierarchicalLODBaseMaterial() const;
#endif // WITH EDITOR

	FORCEINLINE class APlayerState* GetPauserPlayerState() const { return PauserPlayerState; }
	FORCEINLINE virtual void SetPauserPlayerState(class APlayerState* PlayerState) { PauserPlayerState = PlayerState; }

	virtual void RewindForReplay() override;

private:

	// Hidden functions that don't make sense to use on this class.
	HIDE_ACTOR_TRANSFORM_FUNCTIONS();

	virtual void Serialize( FArchive& Ar ) override;

private:
	void InternalPostPropertyChanged(FName PropertyName);

	void AdjustNumberOfBookmarks();
	void UpdateNumberOfBookmarks();

	void SanitizeBookmarkClasses();
	void UpdateBookmarkClass();

	/**
	 * Maximum number of bookmarks allowed.
	 * Changing this will change the allocation of the bookmarks array, and when shrinking
	 * may cause some bookmarks to become eligible for GC.
	 */
	UPROPERTY(Config, Meta=(ClampMin=0))
	int32 MaxNumberOfBookmarks;

	/**
	 * Class that will be used when creating new bookmarks.
	 * Old bookmarks may be recreated with the new class where possible.
	 */
	UPROPERTY(Config, Meta=(AllowAbstract="false", ExactClass="false", AllowedClasses="BookmarkBase"))
	TSubclassOf<class UBookmarkBase> DefaultBookmarkClass;

	UPROPERTY()
	TArray<class UBookmarkBase*> BookmarkArray;

	// Tracked so we can detect changes from Config
	UPROPERTY()
	TSubclassOf<class UBookmarkBase> LastBookmarkClass;

public:
	virtual FSoftClassPath GetAISystemClassName() const;

	/**
	 * The number of bookmarks that will have mapped keyboard shortcuts by default.
	 */
	static const uint32 NumMappedBookmarks = 10;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBookmarkClassChanged, AWorldSettings*);
	static FOnBookmarkClassChanged OnBookmarkClassChanged;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNumberOfBookmarksChanged, AWorldSettings*);
	static FOnNumberOfBookmarksChanged OnNumberOfBoomarksChanged;
#endif

	const int32 GetMaxNumberOfBookmarks() const
	{
		return MaxNumberOfBookmarks;
	}

	TSubclassOf<class UBookmarkBase> GetDefaultBookmarkClass() const
	{
		return DefaultBookmarkClass;
	}

	/**
	 * Gets the array of bookmarks.
	 * It's common for entries to be null as this is treated more like a sparse array.
	 */
	const TArray<class UBookmarkBase*>& GetBookmarks() const
	{
		return BookmarkArray;
	}

	/**
	 * Attempts to move bookmarks such that all bookmarks are adjacent in memory.
	 *
	 * Note, this will not rearrange any valid Bookmarks inside the mapped range, but
	 * may move bookmarks outside that range to fill up mapped bookmarks.
	 */
	void CompactBookmarks();

	/**
	 * Gets the bookmark at the specified index, creating it if a bookmark doesn't exist.
	 *
	 * This will fail if the specified index is greater than MaxNumberOfBookmarks.
	 *
	 * For "plain" access that doesn't cause reallocation, use GetBookmarks
	 *
	 * @param bRecreateOnClassMismatch	Whether or not we should recreate an existing bookmark if it's class
	 *									doesn't match the default bookmark class.
	 */
	class UBookmarkBase* GetOrAddBookmark(const uint32 BookmarkIndex, const bool bRecreateOnClassMismatch);

	/**
	 * Creates and adds a new bookmark of a different class.
	 *
	 * When the bookmark's class is not of the same class as the default bookmark class, the bookmark
	 * will be removed on the next update.
	 * This will fail if we've overrun MaxNumberOfBookmarks.
	 *
	 * @param	BookmarkClass	Class of the new bookmark.
	 * @param	bExpandIfNecessary	Will increase MaxNumberOfBookmarks if there's not enough add another.
	 *
	 * @return	The bookmark that was created.
	 *			Will be nullptr on failure.
	 */
	class UBookmarkBase* AddBookmark(const TSubclassOf<UBookmarkBase> BookmarkClass, const bool bExpandIfNecessary);

	/**
	 * Clears the reference to the bookmark from the specified index.
	 */
	void ClearBookmark(const uint32 BookmarkIndex);

	/**
	 * Clears all references to current bookmarks.
	 */
	void ClearAllBookmarks();
};

