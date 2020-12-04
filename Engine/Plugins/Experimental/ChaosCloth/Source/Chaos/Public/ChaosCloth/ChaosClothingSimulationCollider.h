// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothCollisionData.h"
#include "Containers/ContainersFwd.h"
#include "Chaos/Rotation.h"
#include "Chaos/ImplicitObject.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;
class FClothingSimulationContextCommon;

namespace Chaos
{
	class FClothingSimulationSolver;
	class FClothingSimulationCloth;

	// Collider simulation node
	class FClothingSimulationCollider final
	{
	public:
		enum class ECollisionDataType : int32
		{
			LODless = 0,  // Global LODless collision slot filled with physics collisions
			External,  // External collision slot added/removed at every frame
			LODs,  // LODIndex based start slot for LODs collisions
		};

		FClothingSimulationCollider(
			const UClothingAssetCommon* InAsset,  // Cloth asset for collision data, can be nullptr
			const USkeletalMeshComponent* InSkeletalMeshComponent,  // For asset LODs management, can be nullptr
			bool bInUseLODIndexOverride,
			int32 InLODIndexOverride);
		~FClothingSimulationCollider();

		int32 GetNumGeometries() const { int32 NumGeometries = 0; for (const FLODData& LODDatum : LODData) { NumGeometries += LODDatum.NumGeometries; } return NumGeometries; }

		// Return source (untransformed) collision data for LODless, external and active LODs.
		FClothCollisionData GetCollisionData(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth) const;

		// ---- Animatable property setters ----
		// Set external collision data, will only get updated when used as a Solver Collider TODO: Subclass collider?
		void SetCollisionData(const FClothCollisionData* InCollisionData) { CollisionData = InCollisionData; }
		// ---- End of the animatable property setters ----

		// ---- Cloth interface ----
		void Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);
		void Remove(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

		void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);
		void ResetStartPose(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);
		// ---- End of the Cloth interface ----

		// ---- Debugging and visualization functions ----
		// Return currently LOD active collision particles translations, not thread safe, to use after solver update.
		TConstArrayView<TVector<float, 3>> GetCollisionTranslations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;

		// Return currently LOD active collision particles rotations, not thread safe, to use after solver update.
		TConstArrayView<TRotation<float, 3>> GetCollisionRotations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;

		// Return currently LOD active collision geometries, not thread safe, to use after solver update.
		TConstArrayView<TUniquePtr<FImplicitObject>> GetCollisionGeometries(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;
		// ---- End of the debugging and visualization functions ----

	private:
		void ExtractPhysicsAssetCollision(FClothCollisionData& ClothCollisionData, TArray<int32>& UsedBoneIndices);

		int32 GetNumGeometries(int32 InLODIndex) const;
		int32 GetOffset(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, int32 InLODIndex) const;

	private:
		typedef TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*> FSolverClothPair;

		struct FLODData
		{
			FClothCollisionData ClothCollisionData;
			int32 NumGeometries;  // Number of collision bodies
			TMap<FSolverClothPair, int32> Offsets;  // Solver particle offset

			FLODData() : NumGeometries(0) {}

			void Add(
				FClothingSimulationSolver* Solver,
				FClothingSimulationCloth* Cloth,
				const FClothCollisionData& InClothCollisionData,
				const float InScale = 1.f,
				const TArray<int32>& UsedBoneIndices = TArray<int32>());
			void Remove(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

			void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, const FClothingSimulationContextCommon* Context);

			void Enable(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, bool bEnable);

			void ResetStartPose(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

			FORCEINLINE static int32 GetMappedBoneIndex(const TArray<int32>& UsedBoneIndices, int32 BoneIndex)
			{
				return UsedBoneIndices.IsValidIndex(BoneIndex) ? UsedBoneIndices[BoneIndex] : INDEX_NONE;
			}
		};

		const UClothingAssetCommon* Asset;
		const USkeletalMeshComponent* SkeletalMeshComponent;
		const FClothCollisionData* CollisionData;
		bool bUseLODIndexOverride;
		int32 LODIndexOverride;

		// Collision primitives
		TArray<FLODData> LODData;  // Actual LODs start at LODStart
		TMap<FSolverClothPair, int32> LODIndices;

		// Initial scale
		float Scale;
	};
} // namespace Chaos