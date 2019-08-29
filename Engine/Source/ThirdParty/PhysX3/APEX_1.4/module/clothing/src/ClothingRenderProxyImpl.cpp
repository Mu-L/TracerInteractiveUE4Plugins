/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#include "ApexDefs.h"

#include "ClothingRenderProxyImpl.h"

#include "ClothingAssetImpl.h"
#include "ClothingActorParam.h"
#include "RenderMeshActorDesc.h"
#include "RenderMeshAssetIntl.h"
#include "ClothingScene.h"

namespace nvidia
{
namespace clothing
{

ClothingRenderProxyImpl::ClothingRenderProxyImpl(RenderMeshAssetIntl* rma, bool useFallbackSkinning, bool useCustomVertexBuffer, const HashMap<uint32_t, ApexSimpleString>& overrideMaterials, const PxVec3* morphTargetNewPositions, const uint32_t* morphTargetVertexOffsets, ClothingScene* scene) : 
renderingDataPosition(NULL),
renderingDataNormal(NULL),
renderingDataTangent(NULL),
mBounds(),
mPose(PxMat44(PxIdentity)),
mRenderMeshActor(NULL),
mRenderMeshAsset(rma),
mScene(scene),
mUseFallbackSkinning(useFallbackSkinning),
mMorphTargetNewPositions(morphTargetNewPositions),
mTimeInPool(0)
{
	// create renderMeshActor
	RenderMeshActorDesc desc;
	desc.keepVisibleBonesPacked = false;
	desc.forceFallbackSkinning = mUseFallbackSkinning;

	// prepare material names array and copy the map with override names
	const uint32_t numSubmeshes = rma->getSubmeshCount();
	Array<const char*> overrideMaterialNames;
	for (uint32_t si = 0; si < numSubmeshes; ++si)
	{
		const Pair<const uint32_t, ApexSimpleString>* overrideMat = overrideMaterials.find(si);
		if (overrideMat != NULL)
		{
			overrideMaterialNames.pushBack(overrideMat->second.c_str());
			mOverrideMaterials[si] = overrideMat->second;
		}
		else
		{
			overrideMaterialNames.pushBack(rma->getMaterialName(si));
		}
	}

	desc.overrideMaterialCount = numSubmeshes;
	desc.overrideMaterials = &overrideMaterialNames[0];
	mRenderMeshActor = DYNAMIC_CAST(RenderMeshActorIntl*)(mRenderMeshAsset->createActor(desc));

	// Necessary for clothing
	mRenderMeshActor->setSkinningMode(RenderMeshActorSkinningMode::AllBonesPerPart);

	if (useCustomVertexBuffer)
	{
		// get num verts and check if we need tangents
		ClothingGraphicalMeshAssetWrapper meshAsset(rma);
		uint32_t numRenderVertices = meshAsset.getNumTotalVertices();
		bool renderTangents = meshAsset.hasChannel(NULL, RenderVertexSemantic::TANGENT);

		// allocate aligned buffers and init to 0
		const uint32_t alignedNumRenderVertices = (numRenderVertices + 15) & 0xfffffff0;
		const uint32_t renderingDataSize = sizeof(PxVec3) * alignedNumRenderVertices * 2 + sizeof(PxVec4) * alignedNumRenderVertices * (renderTangents ? 1 : 0);
		renderingDataPosition = (PxVec3*)PX_ALLOC(renderingDataSize, PX_DEBUG_EXP("SimulationAbstract::renderingDataPositions"));
		renderingDataNormal = renderingDataPosition + alignedNumRenderVertices;
		if (renderTangents)
		{
			renderingDataTangent = reinterpret_cast<PxVec4*>(renderingDataNormal + alignedNumRenderVertices);
			PX_ASSERT(((size_t)renderingDataTangent & 0xf) == 0);
		}
		memset(renderingDataPosition, 0, renderingDataSize);

		// update rma to use the custom buffers
		uint32_t submeshOffset = 0;
		for (uint32_t i = 0; i < meshAsset.getSubmeshCount(); i++)
		{
			PxVec3* position =  renderingDataPosition	+ (renderingDataPosition != NULL ? submeshOffset : 0);
			PxVec3* normal =    renderingDataNormal		+ (renderingDataNormal != NULL	? submeshOffset : 0);
			PxVec4* tangent =   renderingDataTangent	+ (renderingDataTangent != NULL	? submeshOffset : 0);
			mRenderMeshActor->addVertexBuffer(i, true, position, normal, tangent);

			// morph targets
			if (mMorphTargetNewPositions != NULL)
			{
				const PxVec3* staticPosition = mMorphTargetNewPositions + morphTargetVertexOffsets[i];
				mRenderMeshActor->setStaticPositionReplacement(i, staticPosition);
			}

			submeshOffset += meshAsset.getNumVertices(i);
		}
	}
}



ClothingRenderProxyImpl::~ClothingRenderProxyImpl()
{
	mRMALock.lock();
	if (mRenderMeshActor != NULL)
	{
		mRenderMeshActor->release();
		mRenderMeshActor = NULL;
	}
	mRMALock.unlock();

	if (renderingDataPosition != NULL)
	{
		PX_FREE(renderingDataPosition);
		renderingDataPosition = NULL;
		renderingDataNormal = NULL;
		renderingDataTangent = NULL;
	}
}



// from ApexInterface
void ClothingRenderProxyImpl::release()
{
	WRITE_ZONE();
	setTimeInPool(1);
	if(mScene == NULL || mRenderMeshActor == NULL)
	{
		PX_DELETE(this);
	}
}



// from Renderable
void ClothingRenderProxyImpl::dispatchRenderResources(UserRenderer& api)
{
	mRMALock.lock();
	if (mRenderMeshActor != NULL)
	{
		mRenderMeshActor->dispatchRenderResources(api, mPose);
	}
	mRMALock.unlock();
}



// from RenderDataProvider.h
void ClothingRenderProxyImpl::updateRenderResources(bool rewriteBuffers, void* userRenderData)
{
	URR_SCOPE;
	
	mRMALock.lock();
	if (mRenderMeshActor != NULL)
	{
		mRenderMeshActor->updateRenderResources(renderingDataPosition == NULL, rewriteBuffers, userRenderData);
	}
	mRMALock.unlock();
}


void ClothingRenderProxyImpl::lockRenderResources()
{
	// no need to lock anything, as soon as the user can access the proxy, we don't write it anymore
	// until he calls release
}


void ClothingRenderProxyImpl::unlockRenderResources()
{
}



bool ClothingRenderProxyImpl::hasSimulatedData() const
{
	READ_ZONE();
	return renderingDataPosition != NULL;
}



RenderMeshActorIntl* ClothingRenderProxyImpl::getRenderMeshActor()
{
	return mRenderMeshActor;
}



RenderMeshAssetIntl* ClothingRenderProxyImpl::getRenderMeshAsset()
{
	return mRenderMeshAsset;
}



void ClothingRenderProxyImpl::setOverrideMaterial(uint32_t submeshIndex, const char* overrideMaterialName)
{
	mOverrideMaterials[submeshIndex] = overrideMaterialName;
	mRMALock.lock();
	if (mRenderMeshActor != NULL)
	{
		mRenderMeshActor->setOverrideMaterial(submeshIndex, overrideMaterialName);
	}
	mRMALock.unlock();
}



bool ClothingRenderProxyImpl::overrideMaterialsEqual(const HashMap<uint32_t, ApexSimpleString>& overrideMaterials)
{
	uint32_t numEntries = mOverrideMaterials.size();
	if (overrideMaterials.size() != numEntries)
		return false;

	for(HashMap<uint32_t, ApexSimpleString>::Iterator iter = mOverrideMaterials.getIterator(); !iter.done(); ++iter)
	{
		uint32_t submeshIndex = iter->first;
		const Pair<const uint32_t, ApexSimpleString>* overrideMat = overrideMaterials.find(submeshIndex);

		// submeshIndex not found
		if (overrideMat == NULL)
			return false;

		// name is different
		if (overrideMat->second != iter->second)
			return false;
	}

	return true;
}


uint32_t ClothingRenderProxyImpl::getTimeInPool() const
{
	return mTimeInPool;
}


void ClothingRenderProxyImpl::setTimeInPool(uint32_t time)
{
	mTimeInPool = time;
}


void ClothingRenderProxyImpl::notifyAssetRelease()
{
	mRMALock.lock();
	if (mRenderMeshActor != NULL)
	{
		mRenderMeshActor->release();
		mRenderMeshActor = NULL;
	}
	mRenderMeshAsset = NULL;
	mRMALock.unlock();
}

}
} // namespace nvidia

