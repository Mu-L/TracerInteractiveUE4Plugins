/*
* Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "ScaledScene.h"
#include "Scene/SceneController.h"
#include <NvClothExt/ClothFabricCooker.h>
#include "ClothMeshGenerator.h"
#include <NvCloth/Fabric.h>
#include <NvCloth/Solver.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Factory.h>
#include "Renderer.h"
#include "renderer/RenderUtils.h"
 
DECLARE_SCENE_NAME(ScaledScene,"Scaled Scene")

void ScaledScene::onInitialize()
{
	///////////////////////////////////////////////////////////////////////
	ClothMeshData clothMesh;

	physx::PxMat44 transform = PxTransform(PxVec3(-2.f, 13.f, 0.f)*0.0, PxQuat(PxPi / 6.f, PxVec3(1.f, 0.f, 0.f)));
	clothMesh.GeneratePlaneCloth(600.f, 700.f, 49, 59, false, transform);
	clothMesh.AttachClothPlaneByAngles(49, 59);

	mClothActor = new ClothActor;
	nv::cloth::ClothMeshDesc meshDesc = clothMesh.GetClothMeshDesc();
	{
		mClothActor->mClothRenderMesh = new ClothRenderMesh(meshDesc);
		mClothActor->mClothRenderable = getSceneController()->getRenderer().createRenderable(*(static_cast<IRenderMesh*>(mClothActor->mClothRenderMesh)), *getSceneController()->getDefaultMaterial());
		mClothActor->mClothRenderable->setColor(getRandomPastelColor());
		mClothActor->mClothRenderable->setScale(physx::PxVec3(0.01, 0.01, 0.01));
		mClothActor->mClothRenderable->setTransform(PxTransform(PxVec3(-2.f, 13.f, 0.f),physx::PxQuat(physx::PxIdentity)));
	}

	nv::cloth::Vector<int32_t>::Type phaseTypeInfo;
	mFabric = NvClothCookFabricFromMesh(getSceneController()->getFactory(), meshDesc, physx::PxVec3(0.0f, -9.8f, 0.0f), &phaseTypeInfo, false);
	trackFabric(mFabric);

	// Initialize start positions and masses for the actual cloth instance
	// (note: the particle/vertex positions do not have to match the mesh description here. Set the positions to the initial shape of this cloth instance)
	std::vector<physx::PxVec4> particlesCopy;
	particlesCopy.resize(clothMesh.mVertices.size());

	physx::PxVec3 center = transform.transform(physx::PxVec3(0.0f, 0.0f, 0.0f));
	for (int i = 0; i < (int)clothMesh.mVertices.size(); i++)
	{
		// To put attachment point closer to each other
		if(clothMesh.mInvMasses[i] < 1e-6)
			clothMesh.mVertices[i] = (clothMesh.mVertices[i] - center) * 0.85f + center;

		particlesCopy[i] = physx::PxVec4(clothMesh.mVertices[i], clothMesh.mInvMasses[i]); // w component is 1/mass, or 0.0f for anchored/fixed particles
	}

	// Create the cloth from the initial positions/masses and the fabric
	mClothActor->mCloth = getSceneController()->getFactory()->createCloth(nv::cloth::Range<physx::PxVec4>(&particlesCopy[0], &particlesCopy[0] + particlesCopy.size()), *mFabric);
	particlesCopy.clear(); particlesCopy.shrink_to_fit();

	mClothActor->mCloth->setGravity(physx::PxVec3(0.0f, -980.0f, 0.0f));

	// Setup phase configs
	std::vector<nv::cloth::PhaseConfig> phases(mFabric->getNumPhases());
	for (int i = 0; i < (int)phases.size(); i++)
	{
		phases[i].mPhaseIndex = i;
		phases[i].mStiffness = 1.0f;
		phases[i].mStiffnessMultiplier = 1.0f;
		phases[i].mCompressionLimit = 1.0f;
		phases[i].mStretchLimit = 1.0f;
	}
	mClothActor->mCloth->setPhaseConfig(nv::cloth::Range<nv::cloth::PhaseConfig>(&phases.front(), &phases.back()));
	mClothActor->mCloth->setDragCoefficient(0.1f);
	mClothActor->mCloth->setLiftCoefficient(0.1f);
	//mClothActor->mCloth->setWindVelocity(physx::PxVec3(50, 0.0, 50.0));
	mClothActor->mCloth->setFluidDensity(1.0f / powf(100, 3));

	mSolver = getSceneController()->getFactory()->createSolver();
	trackSolver(mSolver);
	trackClothActor(mClothActor);

	// Add the cloth to the solver for simulation
	addClothToSolver(mClothActor, mSolver);
	
	{
		IRenderMesh* mesh = getSceneController()->getRenderer().getPrimitiveRenderMesh(PrimitiveRenderMeshType::Plane);
		Renderable* plane = getSceneController()->getRenderer().createRenderable(*mesh, *getSceneController()->getDefaultPlaneMaterial());
		plane->setTransform(PxTransform(PxVec3(0.f, 0.f, 0.f), PxQuat(PxPiDivTwo, PxVec3(0.f, 0.f, 1.f))));
		plane->setScale(PxVec3(1000.f));
		trackRenderable(plane);
	}
}
