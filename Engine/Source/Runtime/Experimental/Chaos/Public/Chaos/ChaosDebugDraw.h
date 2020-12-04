// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	namespace DebugDraw
	{
#if CHAOS_DEBUG_DRAW

		struct CHAOS_API FChaosDebugDrawSettings
		{
		public:
			FChaosDebugDrawSettings(
				float InArrowSize,
				float InBodyAxisLen,
				float InContactLen,
				float InContactWidth,
				float InContactPhiWidth,
				float InContactOwnerWidth,
				float InConstraintAxisLen,
				float InJointComSize,
				float InLineThickness,
				float InDrawScale,
				float InFontHeight,
				float InFontScale,
				float InShapeThicknesScale,
				float InPointSize,
				float InVelScale,
				float InAngVelScale,
				float InImpulseScale,
				int InDrawPriority
			)
				: ArrowSize(InArrowSize)
				, BodyAxisLen(InBodyAxisLen)
				, ContactLen(InContactLen)
				, ContactWidth(InContactWidth)
				, ContactPhiWidth(InContactPhiWidth)
				, ContactOwnerWidth(InContactOwnerWidth)
				, ConstraintAxisLen(InConstraintAxisLen)
				, JointComSize(InJointComSize)
				, LineThickness(InLineThickness)
				, DrawScale(InDrawScale)
				, FontHeight(InFontHeight)
				, FontScale(InFontScale)
				, ShapeThicknesScale(InShapeThicknesScale)
				, PointSize(InPointSize)
				, VelScale(InVelScale)
				, AngVelScale(InAngVelScale)
				, ImpulseScale(InImpulseScale)
				, DrawPriority(InDrawPriority)
			{}

			float ArrowSize;
			float BodyAxisLen;
			float ContactLen;
			float ContactWidth;
			float ContactPhiWidth;
			float ContactOwnerWidth;
			float ConstraintAxisLen;
			float JointComSize;
			float LineThickness;
			float DrawScale;
			float FontHeight;
			float FontScale;
			float ShapeThicknesScale;
			float PointSize;
			float VelScale;
			float AngVelScale;
			float ImpulseScale;
			int DrawPriority;
		};


		enum class EDebugDrawJointFeature
		{
			None = 0,
			CoMConnector = 1 << 0,		// 1
			ActorConnector = 1 << 1,	// 2
			Stretch = 1 << 2,			// 4
			Axes = 1 << 3,				// 8
			Level = 1 << 4,				// 16
			Index = 1 << 5,				// 32
			Color = 1 << 6,				// 64
			Batch = 1 << 7,				// 128
			Island = 1 << 8,			// 256

			Default = ActorConnector | Stretch
		};

		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TGeometryParticle<float, 3>* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleCollisions(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const FPBDCollisionConstraints& Collisions, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawCollisions(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, float ColorScale, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawCollisions(const FRigidTransform3& SpaceTransform, const TArray<FPBDCollisionConstraintHandle*>& ConstraintHandles, float ColorScale, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, float ColorScale, uint32 FeatureMask = (uint32)EDebugDrawJointFeature::Default, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, float ColorScale, uint32 FeatureMask = (uint32)EDebugDrawJointFeature::Default, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawSimulationSpace(const FSimulationSpace& SimSpace, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawShape(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawConstraintGraph(const FRigidTransform3& ShapeTransform, const FPBDConstraintColor& Graph, const FChaosDebugDrawSettings* Settings = nullptr);
#endif
	}
}
