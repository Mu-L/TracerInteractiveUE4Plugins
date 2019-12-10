// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
#include "Templates/Function.h"

#include "USDListener.h"
#include "USDMemory.h"
#include "USDPrimTwin.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"

#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#include "USDStageActor.generated.h"

class ALevelSequenceActor;
class ULevelSequence;
class UMaterial;
class UUsdAsset;

UENUM()
enum class EUsdInitialLoadSet
{
	LoadAll,
	LoadNone
};

UCLASS( MinimalAPI )
class AUsdStageActor : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "USD", meta = (FilePathFilter = "usd files (*.usd; *.usda; *.usdc)|*.usd; *.usda; *.usdc"))
	FFilePath RootLayer;

	UPROPERTY(EditAnywhere, Category = "USD")
	EUsdInitialLoadSet InitialLoadSet;

private:
	UPROPERTY(Category = UsdStageActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	class USceneComponent* SceneComponent;

	/* TimeCode to evaluate the USD stage at */
	UPROPERTY(EditAnywhere, Category = "USD")
	float Time;

	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	void SetTime(float InTime);

	UPROPERTY(VisibleAnywhere, Category = "USD")
	float StartTimeCode;

	UPROPERTY(VisibleAnywhere, Category = "USD")
	float EndTimeCode;

	UPROPERTY(VisibleAnywhere, Category = "USD")
	float TimeCodesPerSecond;

	UPROPERTY(VisibleAnywhere, Category = "USD", Transient)
	ULevelSequence* LevelSequence;

public:
	DECLARE_EVENT_OneParam( AUsdStageActor, FOnActorLoaded, AUsdStageActor* );
	USDSTAGE_API static FOnActorLoaded OnActorLoaded;

	DECLARE_EVENT( AUsdStageActor, FOnStageChanged );
	FOnStageChanged OnStageChanged;

	DECLARE_EVENT_TwoParams( AUsdStageActor, FOnPrimChanged, const FString&, bool );
	FOnPrimChanged OnPrimChanged;

	DECLARE_MULTICAST_DELEGATE(FOnUsdStageTimeChanged);
	FOnUsdStageTimeChanged OnTimeChanged;

public:
	AUsdStageActor();
	virtual ~AUsdStageActor();

	void Refresh() const;

public:
	virtual void PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent ) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostLoad() override;

private:
	void Clear();
	void OpenUsdStage();
	void LoadUsdStage();

	void InitLevelSequence( float FramesPerSecond );
	void SetupLevelSequence();

	void OnUsdPrimTwinDestroyed( const FUsdPrimTwin& UsdPrimTwin );

	void OnPrimObjectPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent );

private:
	FUsdPrimTwin RootUsdTwin;

	TWeakObjectPtr< ALevelSequenceActor > LevelSequenceActor;

	TMultiMap< FString, FDelegateHandle > PrimDelegates;

	TArray< FString > PrimsToAnimate;

	TMap< UObject*, FString > ObjectsToWatch;

private:
	UPROPERTY( NonPIEDuplicateTransient )
	TMap< FString, UStaticMesh* > MeshCache;

	UPROPERTY( NonPIEDuplicateTransient )
	TMap< FString, UMaterial* > MaterialsCache;

#if USE_USD_SDK
public:
	USDSTAGE_API const pxr::UsdStageRefPtr& GetUsdStage();

	FUsdListener& GetUsdListener() { return UsdListener; }
	const FUsdListener& GetUsdListener() const { return UsdListener; }

	FUsdPrimTwin* SpawnPrim( const pxr::SdfPath& UsdPrimPath );
	FUsdPrimTwin* LoadPrim( const pxr::SdfPath& Path );
	FUsdPrimTwin* ExpandPrim( const pxr::UsdPrim& Prim );
	void UpdatePrim( const pxr::SdfPath& UsdPrimPath, bool bResync );
	bool LoadStaticMesh( const pxr::UsdGeomMesh& UsdMesh, UStaticMeshComponent& MeshComponent );
	bool ProcessSkeletonRoot( const pxr::UsdPrim& Prim, USkinnedMeshComponent& SkinnedMeshComponent );

private:
	TUsdStore< pxr::UsdStageRefPtr > UsdStageStore;
	FUsdListener UsdListener;

#endif // #if USE_USD_SDK

};
