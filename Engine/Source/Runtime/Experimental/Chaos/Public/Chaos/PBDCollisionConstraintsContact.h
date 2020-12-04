// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"

namespace Chaos
{
	class FCollisionContext;

	namespace Collisions
	{
		struct FContactParticleParameters {
			FReal CullDistance;
			FReal RestitutionVelocityThreshold;
			bool bCanDisableContacts;
			TArrayCollectionArray<bool>* Collided;
		};

		struct FContactIterationParameters {
			const FReal Dt;
			const int32 Iteration;
			const int32 NumIterations;
			const int32 NumPairIterations;
			const ECollisionApplyType ApplyType;	// @todo(chaos): a better way to customize the collision solver
			bool* NeedsAnotherIteration;
		};

		// Regenerate (one-shot or incremental) the manifold plane and points
		extern void UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance);

		// Update the constraint (re-runs collision detection for this contact)
		extern void Update(FRigidBodyPointContactConstraint& Constraint, const FReal CullDistance, const FReal Dt);

		// Update the constraint (select best point from the manifold)
		extern void Update(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance, const FReal Dt);

		extern void Apply(FCollisionConstraintBase& Constraint, const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters);
		extern void ApplySinglePoint(FRigidBodyPointContactConstraint& Constraint, const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters);
		extern void ApplyMultiPoint(FRigidBodyMultiPointContactConstraint& Constraint, const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters);

		extern void ApplyPushOut(FCollisionConstraintBase& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters, const FVec3& GravityDir = FVec3::DownVector);
		extern void ApplyPushOutSinglePoint(FRigidBodyPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters, const FVec3& GravityDir = FVec3::DownVector);
		extern void ApplyPushOutMultiPoint(FRigidBodyMultiPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters, const FVec3& GravityDir = FVec3::DownVector);


	}

}
