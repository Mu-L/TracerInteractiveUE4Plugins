// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "EngineDefines.h"
#include "PhysXIncludes.h"
#include "Stats/Stats.h"
#include "Physics/PhysDerivedDataPublic.h"

#if WITH_PHYSX && WITH_EDITOR
#include "DerivedDataPluginInterface.h"
#include "Physics/IPhysXCooking.h"

class UBodySetup;
struct FBodyInstance;
struct FBodySetupUVInfo;
struct FTriMeshCollisionData;

//////////////////////////////////////////////////////////////////////////
// PhysX Cooker
class FDerivedDataPhysXCooker : public FDerivedDataPluginInterface
{
private:

	UBodySetup* BodySetup;
	UObject* CollisionDataProvider;
	FName Format;
	bool bGenerateNormalMesh;
	bool bGenerateMirroredMesh;
	bool bGenerateUVInfo;
	int32 BodyComplexity;
	EPhysXMeshCookFlags RuntimeCookFlags;
	const class IPhysXCooking* Cooker;
	FGuid DataGuid;
	FString MeshId;
	bool bIsRuntime;
	bool bVerifyDDC;

public:
	FDerivedDataPhysXCooker(FName InFormat, EPhysXMeshCookFlags InRuntimeCookFlags, UBodySetup* InBodySetup, bool InIsRuntime);

	virtual const TCHAR* GetPluginName() const override
	{
		return TEXT("PhysX");
	}

	virtual const TCHAR* GetVersionString() const override
	{
		// This is a version string that mimics the old versioning scheme. If you
		// want to bump this version, generate a new guid using VS->Tools->Create GUID and
		// return it here. Ex.
		return PHYSX_DDC;	
	}

	virtual FString GetPluginSpecificCacheKeySuffix() const override
	{
		//  1 - base version
		//  2 - cook out small area trimesh triangles from BSP (see UPhysicsSettings::TriangleMeshTriangleMinAreaThreshold)
		//  3 - increase default small area threshold and force recook.
		enum { UE_PHYSX_DERIVEDDATA_VER = 3 };

		const uint16 PhysXVersion = ((PX_PHYSICS_VERSION_MAJOR  & 0xF) << 12) |
				((PX_PHYSICS_VERSION_MINOR  & 0xF) << 8) |
				((PX_PHYSICS_VERSION_BUGFIX & 0xF) << 4) |
				((UE_PHYSX_DERIVEDDATA_VER	& 0xF));

		return FString::Printf( TEXT("%s_%s_%s_%d_%d_%d_%d_%d_%hu_%hu"),
			*Format.ToString(),
			*DataGuid.ToString(),
			*MeshId,
			(int32)bGenerateNormalMesh,
			(int32)bGenerateMirroredMesh,
			(int32)bGenerateUVInfo,
			(int32)RuntimeCookFlags,
			BodyComplexity,
			PhysXVersion,
			Cooker ? Cooker->GetVersion( Format ) : 0xffff
			);
	}


	virtual bool IsBuildThreadsafe() const override
	{
		return false;
	}

	virtual bool IsDeterministic() const override
	{
		return true;
	}

	virtual FString GetDebugContextString() const override;

	virtual bool Build( TArray<uint8>& OutData ) override;

	/** Return true if we can build **/
	bool CanBuild()
	{
		return !!Cooker;
	}
private:

	void InitCooker();
	bool BuildConvex( TArray<uint8>& OutData, bool bDeformableMesh, bool InMirrored, const TArray<TArray<FVector>>& Elements, EPhysXMeshCookFlags CookFlags, int32& NumConvexCooked);
	bool BuildTriMesh( TArray<uint8>& OutData, const FTriMeshCollisionData& TriangleMeshDesc, EPhysXMeshCookFlags CookFlags, FBodySetupUVInfo* UVInfo, int32& NumTriMeshCooked);
	bool ShouldGenerateTriMeshData(bool InUseAllTriData);
};

#endif	//WITH_PHYSX && WITH_EDITOR
