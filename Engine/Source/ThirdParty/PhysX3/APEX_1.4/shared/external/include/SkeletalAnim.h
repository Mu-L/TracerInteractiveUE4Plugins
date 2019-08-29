/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef SKELETAL_ANIM
#define SKELETAL_ANIM

#include <PsFastXml.h>
#include <vector>
#include "TriangleMesh.h"

#include "ApexDefs.h"

namespace MESHIMPORT
{
};

namespace nvidia
{
namespace apex
{
class RenderDebugInterface;
class RenderMeshAssetAuthoring;
}
}

namespace mimp
{
	class MeshSystemContainer;
};

namespace Samples
{

class TriangleMesh;

// ---------------------------------------------------------------------------
struct SkeletalBone
{
	void clear();

	std::string name;
	int id;
	physx::PxTransform pose;
	physx::PxVec3 scale;
	int parent;
	int firstChild;
	int numChildren;
	int firstVertex;

	physx::PxMat44 bindWorldPose;
	physx::PxMat44 invBindWorldPose;
	physx::PxMat44 currentWorldPose;
	int boneOption;
	float inflateConvex;
	float minimalBoneWeight;
	int numShapes;
	bool selected;
	bool isRoot; // this is used for localspace sim
	bool isRootLock; // this is used to lock the translation of the rootbone
	bool allowPrimitives;
	bool dirtyParams;
	bool manualShapes;
};

struct BoneKeyFrame
{
	void clear();
	physx::PxTransform relPose;
	float time;
	physx::PxVec3 scale;
};

struct BoneTrack
{
	void clear();
	int firstFrame;
	int numFrames;
};

struct SkeletalAnimation
{
	void clear();
	std::string name;
	std::vector<BoneTrack> mBoneTracks;
	float minTime;
	float maxTime;
};

// ---------------------------------------------------------------------------
class SkeletalAnim : public physx::shdfnd::FastXml::Callback
{
public:
	SkeletalAnim();
	virtual ~SkeletalAnim();

	void clear();
	void copyFrom(const SkeletalAnim& anim);

	bool loadFromXML(const std::string& xmlFile, std::string& error);
	bool saveToXML(const std::string& xmlFile) const;
	bool loadFromParent(const SkeletalAnim* parent);
	bool loadFromMeshImport(mimp::MeshSystemContainer* msc, std::string& error, bool onlyAddAnimation);
	bool saveToMeshImport(mimp::MeshSystemContainer* msc);
	bool initFrom(nvidia::apex::RenderMeshAssetAuthoring& rma);

	void setBindPose();
	void setAnimPose(int animNr, float time, bool lockRootbone = false);
	const std::vector<SkeletalBone> &getBones() const
	{
		return mBones;
	}
	void setBoneCollision(uint32_t boneNr, int option);
	void setBoneSelected(uint32_t boneNr, bool selected)
	{
		mBones[boneNr].selected = selected;
	}
	void setBoneRoot(uint32_t boneNr, bool isRoot)
	{
		mBones[boneNr].isRoot = isRoot;
	}
	void setBoneAllowPrimitives(uint32_t boneNr, bool on)
	{
		mBones[boneNr].dirtyParams |= mBones[boneNr].allowPrimitives != on;
		mBones[boneNr].allowPrimitives = on;
	}
	void setBoneInflation(uint32_t boneNr, float value)
	{
		mBones[boneNr].inflateConvex = value;
	}
	void setBoneMinimalWeight(uint32_t boneNr, float value)
	{
		mBones[boneNr].dirtyParams |= mBones[boneNr].minimalBoneWeight != value;
		mBones[boneNr].minimalBoneWeight = value;
	}
	void setBoneDirty(uint32_t boneNr, bool on)
	{
		mBones[boneNr].dirtyParams = on;
	}
	void setBoneManualShapes(uint32_t boneNr, bool on)
	{
		mBones[boneNr].manualShapes = on;
	}
	const std::vector<int> &getChildren() const
	{
		return mChildren;
	}
	const std::vector<physx::PxMat44>& getSkinningMatrices() const
	{
		return mSkinningMatrices;
	}
	const std::vector<physx::PxMat44>& getSkinningMatricesWorld() const
	{
		return mSkinningMatricesWorld;
	}
	const std::vector<SkeletalAnimation*> &getAnimations() const
	{
		if (mParent != NULL)
		{
			return mParent->getAnimations();
		}
		return mAnimations;
	}

	void draw(nvidia::RenderDebugInterface* batcher);

	void clearShapeCount(int boneIndex = -1);
	void incShapeCount(int boneIndex);
	void decShapeCount(int boneIndex);

	void setRagdoll(bool on);

	virtual bool processElement(const char* elementName, const char* elementData, const physx::shdfnd::FastXml::AttributePairs& attr, int lineno);
	virtual bool processComment(const char* comment) // encountered a comment in the XML
	{
		PX_UNUSED(comment);
		return true;
	}

	virtual bool processClose(const char* element, uint32_t depth, bool& isError)	 // process the 'close' indicator for a previously encountered element
	{
		PX_UNUSED(element);
		PX_UNUSED(depth);
		isError = false;
		return true;
	}

	virtual void*   fastxml_malloc(uint32_t size)
	{
		return ::malloc(size);
	}
	virtual void	fastxml_free(void* mem)
	{
		::free(mem);
	}

private:
	void init(bool firstTime);
	void initBindPoses(int boneNr, const physx::PxVec3& scale);
	void setAnimPoseRec(int animNr, int boneNr, float time, bool lockBoneTranslation);

	void interpolateBonePose(int animNr, int boneNr, float time, physx::PxTransform& pose, physx::PxVec3& scale);
	int  findBone(const std::string& name);
	void setupConnectivity();

	// skeleton
	std::vector<SkeletalBone> mBones;
	std::vector<physx::PxMat44> mSkinningMatrices;
	std::vector<physx::PxMat44> mSkinningMatricesWorld;
	std::vector<int> mChildren;

	// animation
	std::vector<SkeletalAnimation*> mAnimations;
	std::vector<BoneKeyFrame> mKeyFrames;

	const SkeletalAnim* mParent;

	bool ragdollMode;
};

} // namespace Samples

#endif
