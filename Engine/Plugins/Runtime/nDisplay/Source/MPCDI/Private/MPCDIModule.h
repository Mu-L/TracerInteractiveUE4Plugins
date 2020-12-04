// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"

#include "Components/SceneComponent.h"

class FMPCDIData;


class FMPCDIModule : public IMPCDI
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IMPCDI
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Load(const FString& MPCDIFile) override;
	virtual bool IsLoaded(const FString& MPCDIFile) override;
	virtual bool GetRegionLocator(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) override;
	virtual bool ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, float WorldScale, float ZNear, float ZFar, IMPCDI::FFrustum& InOutFrustum) override;
	virtual bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData) override;
	virtual TSharedPtr<FMPCDIData> GetMPCDIData(IMPCDI::FShaderInputData& ShaderInputData) override;

	virtual bool CreateCustomRegion(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) override;
	virtual bool SetMPCDIProfileType(const IMPCDI::FRegionLocator& InRegionLocator, const EMPCDIProfileType ProfileType) override;

	virtual bool SetStaticMeshWarp(const IMPCDI::FRegionLocator& InRegionLocator, UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent) override;

	virtual bool LoadPFM(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PFMFile, const float PFMScale, bool bIsUnrealGameSpace = false) override;
	virtual bool LoadPFMGeometry(const IMPCDI::FRegionLocator& InRegionLocator, const TArray<FVector>& PFMPoints, int DimW, int DimH, const float WorldScale, bool bIsUnrealGameSpace = false) override;

	virtual bool LoadAlphaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PNGFile, float GammaValue) override;
	virtual bool LoadBetaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& PNGFile) override;

	virtual bool LoadConfig(const TMap<FString, FString>& InConfigParameters, ConfigParser& OutCfgData) override;
	virtual bool Load(const ConfigParser& CfgData, IMPCDI::FRegionLocator& OutRegionLocator) override;

	virtual void ReloadAll() override;
	virtual void ReloadAll_RenderThread()override;

	virtual bool GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData) override;

private:
	void ReleaseMPCDIData();

private:
	mutable FCriticalSection DataGuard;
	TArray<TSharedPtr<FMPCDIData>> MPCDIData;
};
