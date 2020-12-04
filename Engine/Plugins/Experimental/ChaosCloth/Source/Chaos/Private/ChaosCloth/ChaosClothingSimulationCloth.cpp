// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulationCollider.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "Containers/ArrayView.h"
#include "HAL/IConsoleManager.h"
#include "ClothingSimulation.h"

using namespace Chaos;

namespace ChaosClothingSimulationClothConsoleVariables
{
	TAutoConsoleVariable<bool> CVarLegacyDisablesAccurateWind(TEXT("p.ChaosCloth.LegacyDisablesAccurateWind"), true, TEXT("Whether using the Legacy wind model switches off the accurate wind model, or adds up to it"));
}

FClothingSimulationCloth::FLODData::FLODData(int32 InNumParticles, const TConstArrayView<uint32>& InIndices, const TArray<TConstArrayView<float>>& InWeightMaps)
	: NumParticles(InNumParticles)
	, Indices(InIndices)
	, WeightMaps(InWeightMaps)
{
}

void FClothingSimulationCloth::FLODData::Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 InLODIndex)
{
	check(Solver);
	check(Cloth);

	if (!NumParticles)
	{
		return;
	}

	// Add a new solver data chunk
	check(!SolverData.Find(Solver));
	FSolverData& SolverDatum = SolverData.Add(Solver);
	int32& Offset = SolverDatum.Offset;
	TTriangleMesh<float>& TriangleMesh = SolverDatum.TriangleMesh;

	// Add particles
	Offset = Solver->AddParticles(NumParticles, Cloth->GroupId);  // TODO: Have a per solver map of offset

	// Update source mesh for this LOD, this is required prior to reset the start pose
	Cloth->Mesh->Update(Solver, INDEX_NONE, InLODIndex, 0, Offset);

	// Reset the particles start pose before setting up mass and constraints
	ResetStartPose(Solver);

	// Build a sim friendly triangle mesh including the solver particle's Offset
	const int32 NumElements = Indices.Num() / 3;
	TArray<TVector<int32, 3>> Elements;
	Elements.Reserve(NumElements);

	for (int32 i = 0; i < NumElements; ++i)
	{
		const int32 Index = 3 * i;
		Elements.Add(
			{ static_cast<int32>(Offset + Indices[Index]),
			 static_cast<int32>(Offset + Indices[Index + 1]),
			 static_cast<int32>(Offset + Indices[Index + 2]) });
	}

	TriangleMesh.Init(MoveTemp(Elements));
	TriangleMesh.GetPointToTriangleMap(); // Builds map for later use by GetPointNormals(), and the velocity fields

	// Initialize the normals, in case the sim data is queried before the simulation steps
	UpdateNormals(Solver);

	// Set the particle masses
	const TConstArrayView<float>& MaxDistances = WeightMaps[(int32)EChaosWeightMapTarget::MaxDistance];
	static const float KinematicDistanceThreshold = 0.1f;  // TODO: This is not the same value as set in the painting UI but we might want to expose this value as parameter
	auto KinematicPredicate =
		[MaxDistances](int32 Index)
		{
			return MaxDistances.IsValidIndex(Index) && MaxDistances[Index] < KinematicDistanceThreshold;
		};

	switch (Cloth->MassMode)
	{
	default:
		check(false);
	case EMassMode::UniformMass:
		Solver->SetParticleMassUniform(Offset, Cloth->MassValue, Cloth->MinPerParticleMass, TriangleMesh, KinematicPredicate);
		break;
	case EMassMode::TotalMass:
		Solver->SetParticleMassFromTotalMass(Offset, Cloth->MassValue, Cloth->MinPerParticleMass, TriangleMesh, KinematicPredicate);
		break;
	case EMassMode::Density:
		Solver->SetParticleMassFromDensity(Offset, Cloth->MassValue, Cloth->MinPerParticleMass, TriangleMesh, KinematicPredicate);
		break;
	}
	const TConstArrayView<float> InvMasses(Solver->GetParticleInvMasses(Offset), NumParticles);

	// Setup solver constraints
	FClothConstraints& ClothConstraints = Solver->GetClothConstraints(Offset);
	const TArray<TVector<int32, 3>>& SurfaceElements = TriangleMesh.GetSurfaceElements();

	// Self collisions
	if (Cloth->bUseSelfCollisions)
	{
		static const int32 DisabledCollisionElementsN = 5;  // TODO: Make this a parameter?
		TSet<TVector<int32, 2>> DisabledCollisionElements;  // TODO: Is this needed? Turn this into a bit array?
	
		const int32 Range = Offset + NumParticles;
		for (int32 Index = Offset; Index < Range; ++Index)
		{
			const TSet<int32> Neighbors = TriangleMesh.GetNRing(Index, DisabledCollisionElementsN);
			for (int32 Element : Neighbors)
			{
				check(Index != Element);
				DisabledCollisionElements.Emplace(TVector<int32, 2>(Index, Element));
				DisabledCollisionElements.Emplace(TVector<int32, 2>(Element, Index));
			}
		}
		ClothConstraints.SetSelfCollisionConstraints(SurfaceElements, MoveTemp(DisabledCollisionElements), Cloth->SelfCollisionThickness);
	}

	// Edge constraints
	if (Cloth->EdgeStiffness)
	{
		ClothConstraints.SetEdgeConstraints(SurfaceElements, Cloth->EdgeStiffness, Cloth->bUseXPBDConstraints);
	}

	// Bending constraints
	if (Cloth->BendingStiffness > 0.f)
	{
		if (Cloth->bUseBendingElements)
		{
			TArray<Chaos::TVector<int32, 4>> BendingElements = TriangleMesh.GetUniqueAdjacentElements();
			ClothConstraints.SetBendingConstraints(MoveTemp(BendingElements), Cloth->BendingStiffness);
		}
		else
		{
			TArray<Chaos::TVector<int32, 2>> Edges = TriangleMesh.GetUniqueAdjacentPoints();
			ClothConstraints.SetBendingConstraints(MoveTemp(Edges), Cloth->BendingStiffness, Cloth->bUseXPBDConstraints);
		}
	}

	// Area constraints
	if (Cloth->AreaStiffness)
	{
		TArray<Chaos::TVector<int32, 3>> SurfaceConstraints = SurfaceElements;
		ClothConstraints.SetAreaConstraints(MoveTemp(SurfaceConstraints), Cloth->AreaStiffness, Cloth->bUseXPBDConstraints);
	}

	// Volume constraints
	if (Cloth->VolumeStiffness)
	{
		if (Cloth->bUseThinShellVolumeConstraints)
		{
			TArray<Chaos::TVector<int32, 2>> BendingConstraints = TriangleMesh.GetUniqueAdjacentPoints();
			TArray<Chaos::TVector<int32, 2>> DoubleBendingConstraints;
			{
				TMap<int32, TArray<int32>> BendingHash;
				for (int32 i = 0; i < BendingConstraints.Num(); ++i)
				{
					BendingHash.FindOrAdd(BendingConstraints[i][0]).Add(BendingConstraints[i][1]);
					BendingHash.FindOrAdd(BendingConstraints[i][1]).Add(BendingConstraints[i][0]);
				}
				TSet<Chaos::TVector<int32, 2>> Visited;
				for (auto Elem : BendingHash)
				{
					for (int32 i = 0; i < Elem.Value.Num(); ++i)
					{
						for (int32 j = i + 1; j < Elem.Value.Num(); ++j)
						{
							if (Elem.Value[i] == Elem.Value[j])
								continue;
							auto NewElem = Chaos::TVector<int32, 2>(Elem.Value[i], Elem.Value[j]);
							if (!Visited.Contains(NewElem))
							{
								DoubleBendingConstraints.Add(NewElem);
								Visited.Add(NewElem);
								Visited.Add(Chaos::TVector<int32, 2>(Elem.Value[j], Elem.Value[i]));
							}
						}
					}
				}
			}

			ClothConstraints.SetVolumeConstraints(MoveTemp(DoubleBendingConstraints), Cloth->VolumeStiffness);
		}
		else
		{
			TArray<Chaos::TVector<int32, 3>> SurfaceConstraints = SurfaceElements;
			ClothConstraints.SetVolumeConstraints(MoveTemp(SurfaceConstraints), Cloth->VolumeStiffness);
		}
	}

	// Long range constraints
	if (Cloth->StrainLimitingStiffness)
	{
		check(TriangleMesh.GetNumElements() > 0);
		// PerFormance note: The Per constraint version of this function is quite a bit faster for smaller assets
		// There might be a cross-over point where the PerParticle version is faster: To be determined
		ClothConstraints.SetLongRangeConstraints(
			TriangleMesh.GetPointToNeighborsMap(),
			Cloth->StrainLimitingStiffness,
			Cloth->LimitScale,
			Cloth->TetherMode,
			Cloth->bUseXPBDConstraints);
	}

	// Max distances
	if (MaxDistances.Num())
	{
		ClothConstraints.SetMaximumDistanceConstraints(MaxDistances);
	}

	// Backstop Constraints
	const TConstArrayView<float>& BackstopDistances = WeightMaps[(int32)EChaosWeightMapTarget::BackstopDistance];
	const TConstArrayView<float>& BackstopRadiuses = WeightMaps[(int32)EChaosWeightMapTarget::BackstopRadius];
	if (BackstopRadiuses.Num() && BackstopDistances.Num())
	{
		ClothConstraints.SetBackstopConstraints(BackstopDistances, BackstopRadiuses, Cloth->bUseLegacyBackstop);
	}

	// Animation Drive Constraints
	const TConstArrayView<float>& AnimDriveMultipliers = WeightMaps[(int32)EChaosWeightMapTarget::AnimDriveMultiplier];
	if (AnimDriveMultipliers.Num())
	{
		ClothConstraints.SetAnimDriveConstraints(AnimDriveMultipliers);
	}

	// Shape target constraint
	if (Cloth->ShapeTargetStiffness)
	{
		ClothConstraints.SetShapeTargetConstraints(Cloth->ShapeTargetStiffness);
	}

	// Commit rules to solver
	ClothConstraints.CreateRules();

	// Disable constraints by default
	ClothConstraints.Enable(false);

	// Update LOD stats
	NumKinenamicParticles = 0;
	NumDynammicParticles = 0;
	for (int32 Index = 0; Index < NumParticles; ++Index)
	{
		if (InvMasses[Index] == 0.f)
		{
			++NumKinenamicParticles;
		}
		else
		{
			++NumDynammicParticles;
		}
	}
}

void FClothingSimulationCloth::FLODData::Remove(FClothingSimulationSolver* Solver)
{
	SolverData.Remove(Solver);
}

void FClothingSimulationCloth::FLODData::Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth)
{
	check(Solver);
	const int32 Offset = SolverData.FindChecked(Solver).Offset;
	check(Offset != INDEX_NONE);

	// Update the animatable constraint parameters
	FClothConstraints& ClothConstraints = Solver->GetClothConstraints(Offset);
	ClothConstraints.SetMaxDistancesMultiplier(Cloth->MaxDistancesMultiplier);
	ClothConstraints.SetAnimDriveSpringStiffness(Cloth->AnimDriveSpringStiffness);
}

void FClothingSimulationCloth::FLODData::Enable(FClothingSimulationSolver* Solver, bool bEnable) const
{
	check(Solver);
	const int32 Offset = SolverData.FindChecked(Solver).Offset;
	check(Offset != INDEX_NONE);
	
	// Enable particles (and related constraints)
	Solver->EnableParticles(Offset, bEnable);
}

void FClothingSimulationCloth::FLODData::ResetStartPose(FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 Offset = SolverData.FindChecked(Solver).Offset;
	check(Offset != INDEX_NONE);

	TVector<float, 3>* const Ps = Solver->GetParticlePs(Offset);
	TVector<float, 3>* const Xs = Solver->GetParticleXs(Offset);
	TVector<float, 3>* const Vs = Solver->GetParticleVs(Offset);
	const TVector<float, 3>* const AnimationPositions = Solver->GetAnimationPositions(Offset);
	TVector<float, 3>* const OldAnimationPositions = Solver->GetOldAnimationPositions(Offset);

	for (int32 Index = 0; Index < NumParticles; ++Index)
	{
		Ps[Index] = Xs[Index] = OldAnimationPositions[Index] = AnimationPositions[Index];
		Vs[Index] = TVector<float, 3>(0.f);
	}
}

void FClothingSimulationCloth::FLODData::UpdateNormals(FClothingSimulationSolver* Solver) const
{
	check(Solver);

	const FSolverData& SolverDatum = SolverData.FindChecked(Solver);
	const int32 Offset = SolverDatum.Offset;
	const TTriangleMesh<float>& TriangleMesh = SolverDatum.TriangleMesh;

	check(Offset != INDEX_NONE);

	TConstArrayView<TVector<float, 3>> Points(Solver->GetParticleXs(Offset) - Offset, Offset + NumParticles);  // TODO: TriangleMesh still uses global array
	TArray<TVector<float, 3>> FaceNormals;
	TriangleMesh.GetFaceNormals(FaceNormals, Points, /*ReturnEmptyOnError =*/ false);

	TArrayView<TVector<float, 3>> Normals(Solver->GetNormals(Offset), NumParticles);
	TriangleMesh.GetPointNormals(Normals, FaceNormals, /*bUseGlobalArray =*/ false);
}

FClothingSimulationCloth::FClothingSimulationCloth(
	FClothingSimulationMesh* InMesh,
	TArray<FClothingSimulationCollider*>&& InColliders,
	uint32 InGroupId,
	EMassMode InMassMode,
	float InMassValue,
	float InMinPerParticleMass,
	float InEdgeStiffness,
	float InBendingStiffness,
	bool bInUseBendingElements,
	float InAreaStiffness,
	float InVolumeStiffness,
	bool bInUseThinShellVolumeConstraints,
	float InStrainLimitingStiffness,
	float InLimitScale,
	ETetherMode InTetherMode,
	float InMaxDistancesMultiplier,
	float InAnimDriveSpringStiffness,
	float InShapeTargetStiffness,
	bool bInUseXPBDConstraints,
	float InGravityScale,
	bool bInIsGravityOverridden,
	const TVector<float, 3>& InGravityOverride,
	const TVector<float, 3>& InLinearVelocityScale,
	float InAngularVelocityScale,
	float InDragCoefficient,
	float InLiftCoefficient,
	bool bInUseLegacyWind,
	float InDampingCoefficient,
	float InCollisionThickness,
	float InFrictionCoefficient,
	bool bInUseSelfCollisions,
	float InSelfCollisionThickness,
	bool bInUseLegacyBackstop,
	bool bInUseLODIndexOverride, 
	int32 InLODIndexOverride)
	: Mesh(nullptr)
	, Colliders()
	, GroupId(InGroupId)
	, MassMode(InMassMode)
	, MassValue(InMassValue)
	, MinPerParticleMass(InMinPerParticleMass)
	, EdgeStiffness(InEdgeStiffness)
	, BendingStiffness(InBendingStiffness)
	, bUseBendingElements(bInUseBendingElements)
	, AreaStiffness(InAreaStiffness)
	, VolumeStiffness(InVolumeStiffness)
	, bUseThinShellVolumeConstraints(bInUseThinShellVolumeConstraints)
	, StrainLimitingStiffness(InStrainLimitingStiffness)
	, LimitScale(InLimitScale)
	, TetherMode(InTetherMode)
	, MaxDistancesMultiplier(InMaxDistancesMultiplier)
	, AnimDriveSpringStiffness(InAnimDriveSpringStiffness)
	, ShapeTargetStiffness(InShapeTargetStiffness)
	, bUseXPBDConstraints(bInUseXPBDConstraints)
	, GravityScale(InGravityScale)
	, bIsGravityOverridden(bInIsGravityOverridden)
	, GravityOverride(InGravityOverride)
	, LinearVelocityScale(InLinearVelocityScale)
	, AngularVelocityScale(InAngularVelocityScale)
	, DragCoefficient(InDragCoefficient)
	, LiftCoefficient(InLiftCoefficient)
	, bUseLegacyWind(bInUseLegacyWind)
	, DampingCoefficient(InDampingCoefficient)
	, CollisionThickness(InCollisionThickness)
	, FrictionCoefficient(InFrictionCoefficient)
	, bUseSelfCollisions(bInUseSelfCollisions)
	, SelfCollisionThickness(InSelfCollisionThickness)
	, bUseLegacyBackstop(bInUseLegacyBackstop)
	, bUseLODIndexOverride(bInUseLODIndexOverride)
	, LODIndexOverride(InLODIndexOverride)
	, bNeedsReset(false)
	, bNeedsTeleport(false)
	, NumActiveKinematicParticles(0)
	, NumActiveDynamicParticles(0)
{
	SetMesh(InMesh);
	SetColliders(MoveTemp(InColliders));
}

FClothingSimulationCloth::~FClothingSimulationCloth()
{
}

void FClothingSimulationCloth::SetMesh(FClothingSimulationMesh* InMesh)
{
	Mesh = InMesh;

	// Reset LODs
	const int32 NumLODs = Mesh ? Mesh->GetNumLODs() : 0;
	LODData.Reset(NumLODs);
	for (int32 Index = 0; Index < NumLODs; ++Index)
	{
		LODData.Emplace(Mesh->GetNumPoints(Index), Mesh->GetIndices(Index), Mesh->GetWeightMaps(Index));
	}

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Refresh this cloth to recreate particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::SetColliders(TArray<FClothingSimulationCollider*>&& InColliders)
{
	// Empty the collider list, but keep the pointers around for the removal operation below
	const TArray<FClothingSimulationCollider*> TempColliders = MoveTemp(Colliders);

	// Replace with the new colliders
	Colliders = InColliders;

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		for (FClothingSimulationCollider* const Collider : TempColliders)
		{
			Collider->Remove(Solver, this);
		}

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::AddCollider(FClothingSimulationCollider* InCollider)
{
	check(InCollider);

	if (Colliders.Find(InCollider) != INDEX_NONE)
	{
		return;
	}

	// Add the collider to the solver update array
	Colliders.Emplace(InCollider);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::RemoveCollider(FClothingSimulationCollider* InCollider)
{
	if (Colliders.Find(InCollider) == INDEX_NONE)
	{
		return;
	}

	// Remove collider from array
	Colliders.RemoveSwap(InCollider);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		InCollider->Remove(Solver, this);

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::RemoveColliders()
{
	// Empty the collider list, but keep the pointers around for the removal operation below
	const TArray<FClothingSimulationCollider*> TempColliders = MoveTemp(Colliders);

	// Iterate all known solvers
	TArray<FClothingSimulationSolver*> Solvers;
	LODIndices.GetKeys(Solvers);
	for (FClothingSimulationSolver* const Solver : Solvers)
	{
		// Remove any held collider data related to this cloth simulation
		for (FClothingSimulationCollider* const Collider : TempColliders)
		{
			Collider->Remove(Solver, this);
		}

		// Refresh this cloth to recreate collision particles
		Solver->RefreshCloth(this);
	}
}

void FClothingSimulationCloth::Add(FClothingSimulationSolver* Solver)
{
	check(Solver);

	// Can't add a cloth twice to the same solver
	check(!LODIndices.Find(Solver));

	// Initialize LODIndex
	int32& LODIndex = LODIndices.Add(Solver);
	LODIndex = INDEX_NONE;

	// Add LODs
	for (int32 Index = 0; Index < LODData.Num(); ++Index)
	{
		LODData[Index].Add(Solver, this, Index);
	}

	// Add Colliders
	for (FClothingSimulationCollider* Collider : Colliders)
	{
		Collider->Add(Solver, this);
	}
}

void FClothingSimulationCloth::Remove(FClothingSimulationSolver* Solver)
{
	// Remove Colliders
	for (FClothingSimulationCollider* Collider : Colliders)
	{
		Collider->Remove(Solver, this);
	}

	// Remove solver from maps
	LODIndices.Remove(Solver);
	for (FLODData& LODDatum: LODData)
	{
		LODDatum.Remove(Solver);
	}
}

int32 FClothingSimulationCloth::GetNumParticles(int32 InLODIndex) const
{
	return LODData.IsValidIndex(InLODIndex) ? LODData[InLODIndex].NumParticles : 0;
}

int32 FClothingSimulationCloth::GetOffset(const FClothingSimulationSolver* Solver, int32 InLODIndex) const
{
	return LODData.IsValidIndex(InLODIndex) ? LODData[InLODIndex].SolverData.FindChecked(Solver).Offset : 0;
}

TVector<float, 3> FClothingSimulationCloth::GetGravity(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	return Solver->IsClothGravityOverrideEnabled() && bIsGravityOverridden ? GravityOverride : Solver->GetGravity() * GravityScale;
}

TAABB<float, 3> FClothingSimulationCloth::CalculateBoundingBox(const FClothingSimulationSolver* Solver) const
{
	check(Solver);

	// Calculate local space bounding box
	TAABB<float, 3> BoundingBox = TAABB<float, 3>::EmptyAABB();

	const TConstArrayView<TVector<float, 3>> ParticlePositions = GetParticlePositions(Solver);
	for (const TVector<float, 3>& ParticlePosition : ParticlePositions)
	{
		BoundingBox.GrowToInclude(ParticlePosition);
	}

	// Return world space bounding box
	return BoundingBox.TransformedAABB(TRigidTransform<float, 3>(Solver->GetLocalSpaceLocation(), TRotation<float, 3>::Identity));
}

int32 FClothingSimulationCloth::GetOffset(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	return LODData.IsValidIndex(LODIndex) ? GetOffset(Solver, LODIndex) : INDEX_NONE;
}

const TTriangleMesh<float>& FClothingSimulationCloth::GetTriangleMesh(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	static const TTriangleMesh<float> EmptyTriangleMesh;
	return LODData.IsValidIndex(LODIndex) ? LODData[LODIndex].SolverData.FindChecked(Solver).TriangleMesh : EmptyTriangleMesh;
}

const TArray<TConstArrayView<float>>& FClothingSimulationCloth::GetWeightMaps(const FClothingSimulationSolver* Solver) const
{
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	static const TArray<TConstArrayView<float>> EmptyWeightMaps;
	return LODData.IsValidIndex(LODIndex) ? LODData[LODIndex].WeightMaps : EmptyWeightMaps;
}

int32 FClothingSimulationCloth::GetReferenceBoneIndex() const
{
	return Mesh ? Mesh->GetReferenceBoneIndex() : INDEX_NONE;
}

void FClothingSimulationCloth::Update(FClothingSimulationSolver* Solver)
{
	check(Solver);

	// Exit if the input mesh is missing
	if (!Mesh)
	{
		return;
	}

	// Retrieve LOD Index, either from the override, or from the mesh input
	int32& LODIndex = LODIndices.FindChecked(Solver);  // Must be added to solver first

	const int32 PrevLODIndex = LODIndex;
	LODIndex = bUseLODIndexOverride && LODData.IsValidIndex(LODIndexOverride) ? LODIndexOverride : Mesh->GetLODIndex();

	// Update reference space transform from the mesh's reference bone transform  TODO: Add override in the style of LODIndexOverride
	const TRigidTransform<float, 3> OldReferenceSpaceTransform = ReferenceSpaceTransform;
	ReferenceSpaceTransform = Mesh->GetReferenceBoneTransform();
	ReferenceSpaceTransform.SetScale3D(TVector<float, 3>(1.f));

	// Update Cloth Colliders
	{
		SCOPE_CYCLE_COUNTER(STAT_ClothUpdateCollisions);

		for (FClothingSimulationCollider* Collider : Colliders)
		{
			Collider->Update(Solver, this);
		}
	}

	// Update the source mesh skinned positions
	const int32 PrevOffset = GetOffset(Solver, PrevLODIndex);
	const int32 Offset = GetOffset(Solver, LODIndex);
	check(PrevOffset != INDEX_NONE && Offset != INDEX_NONE);

	Mesh->Update(Solver, PrevLODIndex, LODIndex, PrevOffset, Offset);

	// LOD Switching
	if (LODIndex != PrevLODIndex)
	{
		if (PrevLODIndex != INDEX_NONE)
		{
			// Disable previous LOD's particles
			LODData[PrevLODIndex].Enable(Solver, false);
		}
		if (LODIndex != INDEX_NONE)
		{
			// Enable new LOD's particles
			LODData[LODIndex].Enable(Solver, true);
			NumActiveKinematicParticles = LODData[LODIndex].NumKinenamicParticles;
			NumActiveDynamicParticles = LODData[LODIndex].NumDynammicParticles;

			// Wrap new LOD based on previous LOD if possible (can only do 1 level LOD at a time, and if previous LOD exists)
			bNeedsReset = bNeedsReset || !Mesh->WrapDeformLOD(
				PrevLODIndex,
				LODIndex,
				Solver->GetNormals(PrevOffset),
				Solver->GetParticlePs(PrevOffset),
				Solver->GetParticleVs(PrevOffset),
				Solver->GetParticlePs(Offset),
				Solver->GetParticleXs(Offset),
				Solver->GetParticleVs(Offset));
		}
		else
		{
			NumActiveKinematicParticles = 0;
			NumActiveDynamicParticles = 0;
		}
	}

	if (LODIndex != INDEX_NONE)
	{
		// Update Cloth group parameters  TODO: Cloth groups should exist as their own node object so that they can be used by several cloth objects
		LODData[LODIndex].Update(Solver, this);

		// Update gravity
		// This code relies on the solver gravity property being already set.
		// In order to use a cloth gravity override, it must first be enabled by the solver so that an override at solver level can still take precedence if needed.
		// In all cases apart from when the cloth override is used, the gravity scale must be combined to the solver gravity value.
		Solver->SetGravity(GroupId, GetGravity(Solver));

		// Update wind
		Solver->SetLegacyWind(GroupId, bUseLegacyWind);

		if (bUseLegacyWind && ChaosClothingSimulationClothConsoleVariables::CVarLegacyDisablesAccurateWind.GetValueOnAnyThread())
		{
			Solver->SetWindVelocityField(GroupId, 0.f, 0.f, &GetTriangleMesh(Solver));
		}
		else
		{
			Solver->SetWindVelocityField(GroupId, DragCoefficient, LiftCoefficient, &GetTriangleMesh(Solver));
		}

		// Update general solver properties
		Solver->SetProperties(GroupId, DampingCoefficient, CollisionThickness, FrictionCoefficient);

		// TODO: Move all groupID updates out of the cloth update to allow to use of the same GroupId with different cloths

		// Set the reference input velocity and deal with teleport & reset
		TVector<float, 3> OutLinearVelocityScale;
		float OutAngularVelocityScale;

		if (bNeedsReset)
		{
			// Make sure not to do any pre-sim transform just after a reset
			OutLinearVelocityScale = TVector<float, 3>(1.f);
			OutAngularVelocityScale = 1.f;

			// Reset to start pose
			LODData[LODIndex].ResetStartPose(Solver);
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Cloth in group Id %d Needs reset."), GroupId);
		}
		else if (bNeedsTeleport)
		{
			// Remove all impulse velocity from the last frame
			OutLinearVelocityScale = TVector<float, 3>(0.f);
			OutAngularVelocityScale = 0.f;
			UE_LOG(LogChaosCloth, VeryVerbose, TEXT("Cloth in group Id %d Needs teleport."), GroupId);
		}
		else
		{
			// Use the cloth group's parameters
			OutLinearVelocityScale = LinearVelocityScale;
			OutAngularVelocityScale = AngularVelocityScale;
		}

		Solver->SetReferenceVelocityScale(
			GroupId,
			OldReferenceSpaceTransform,
			ReferenceSpaceTransform,
			OutLinearVelocityScale,
			OutAngularVelocityScale);
	}

	// Reset trigger flags
	bNeedsTeleport = false;
	bNeedsReset = false;
}

void FClothingSimulationCloth::PostUpdate(FClothingSimulationSolver* Solver)
{
	check(Solver);

	const int32 LODIndex = LODIndices.FindChecked(Solver);
	if (LODIndex != INDEX_NONE)
	{
		// Update normals
		LODData[LODIndex].UpdateNormals(Solver);
	}
}

TConstArrayView<TVector<float, 3>> FClothingSimulationCloth::GetAnimationPositions(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetOffset(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<TVector<float, 3>>(Solver->GetAnimationPositions(GetOffset(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<TVector<float, 3>> FClothingSimulationCloth::GetAnimationNormals(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetOffset(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<TVector<float, 3>>(Solver->GetAnimationNormals(GetOffset(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<TVector<float, 3>> FClothingSimulationCloth::GetParticlePositions(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetOffset(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<TVector<float, 3>>(Solver->GetParticleXs(GetOffset(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<TVector<float, 3>> FClothingSimulationCloth::GetParticleNormals(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetOffset(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<TVector<float, 3>>(Solver->GetNormals(GetOffset(Solver, LODIndex)), GetNumParticles(LODIndex));
}

TConstArrayView<float> FClothingSimulationCloth::GetParticleInvMasses(const FClothingSimulationSolver* Solver) const
{
	check(Solver);
	const int32 LODIndex = LODIndices.FindChecked(Solver);
	check(GetOffset(Solver, LODIndex) != INDEX_NONE);
	return TConstArrayView<float>(Solver->GetParticleInvMasses(GetOffset(Solver, LODIndex)), GetNumParticles(LODIndex));
}
