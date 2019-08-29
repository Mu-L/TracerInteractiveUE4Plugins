// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2017 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#pragma once

#include "NvCloth/Factory.h"
#include "DxClothData.h"
#include "DxBatchedVector.h"
#include "../IndexPair.h"
#include <foundation/PxVec4.h>
#include <foundation/PxVec3.h>

#if _MSC_VER >= 1700
#pragma warning(disable : 4471)
#endif

struct ID3D11Device;
struct ID3D11Buffer;
enum D3D11_MAP;

namespace nv
{

namespace cloth
{
class DxFabric;
class DxCloth;
template <typename>
class ClothImpl;
class DxContextManagerCallback;

class DxFactory : public Factory
{
protected:
	DxFactory(const DxFactory&); // not implemented	
	DxFactory& operator = (const DxFactory&); // not implemented

  public:
	typedef DxFabric FabricType;
	typedef DxCloth ClothType;

	explicit DxFactory(DxContextManagerCallback*);
	virtual ~DxFactory();

	virtual Platform getPlatform() const { return Platform::DX11; }

	virtual Fabric* createFabric(uint32_t numParticles, Range<const uint32_t> phaseIndices, Range<const uint32_t> sets,
	                             Range<const float> restvalues, Range<const float> stiffnessValues, Range<const uint32_t> indices,
	                             Range<const uint32_t> anchors, Range<const float> tetherLengths,
	                             Range<const uint32_t> triangles);

	virtual Cloth* createCloth(Range<const physx::PxVec4> particles, Fabric& fabric);

	virtual Solver* createSolver();

	virtual Cloth* clone(const Cloth& cloth);

	virtual void extractFabricData(const Fabric& fabric, Range<uint32_t> phaseIndices, Range<uint32_t> sets,
	                               Range<float> restvalues, Range<float> stiffnessValues, Range<uint32_t> indices, Range<uint32_t> anchors,
	                               Range<float> tetherLengths, Range<uint32_t> triangles) const;

	virtual void extractCollisionData(const Cloth& cloth, Range<physx::PxVec4> spheres, Range<uint32_t> capsules,
	                                  Range<physx::PxVec4> planes, Range<uint32_t> convexes, Range<physx::PxVec3> triangles) const;

	virtual void extractMotionConstraints(const Cloth& cloth, Range<physx::PxVec4> destConstraints) const;

	virtual void extractSeparationConstraints(const Cloth& cloth, Range<physx::PxVec4> destConstraints) const;

	virtual void extractParticleAccelerations(const Cloth& cloth, Range<physx::PxVec4> destAccelerations) const;

	virtual void extractVirtualParticles(const Cloth& cloth, Range<uint32_t[4]> destIndices,
	                                     Range<physx::PxVec3> destWeights) const;

	virtual void extractSelfCollisionIndices(const Cloth& cloth, Range<uint32_t> destIndices) const;

	virtual void extractRestPositions(const Cloth& cloth, Range<physx::PxVec4> destRestPositions) const;

  public:
	void copyToHost(void* dst, ID3D11Buffer* buffer, uint32_t offset, uint32_t size) const; //size and offset in bytes (or in pixels when buffer is a texture?)
	void CompileComputeShaders(); // this is called once to setup the shaders

	void reserveStagingBuffer(uint32_t size);
	void* mapStagingBuffer(D3D11_MAP) const;
	void unmapStagingBuffer() const;

	Vector<DxFabric*>::Type mFabrics;

	DxContextManagerCallback* mContextManager;
	ID3D11Buffer* mStagingBuffer;

	ID3D11ComputeShader* mSolverKernelComputeShader;

	uint32_t mNumThreadsPerBlock;

	const uint32_t mMaxThreadsPerBlock;

	DxBatchedStorage<DxConstraint> mConstraints;
	DxBatchedStorage<DxConstraint> mConstraintsHostCopy;
	DxBatchedStorage<float> mStiffnessValues;
	DxBatchedStorage<DxTether> mTethers;
	DxBatchedStorage<physx::PxVec4> mParticles;
	DxBatchedStorage<physx::PxVec4> mParticlesHostCopy;
	DxBatchedStorage<DxPhaseConfig> mPhaseConfigs;

	DxBatchedStorage<physx::PxVec4> mParticleAccelerations;
	DxBatchedStorage<physx::PxVec4> mParticleAccelerationsHostCopy;

	DxBatchedStorage<IndexPair> mCapsuleIndices;
	DxBuffer<IndexPair> mCapsuleIndicesDeviceCopy;

	DxBatchedStorage<physx::PxVec4> mCollisionSpheres;
	DxBuffer<physx::PxVec4> mCollisionSpheresDeviceCopy;

	DxBatchedStorage<uint32_t> mConvexMasks;
	DxBuffer<uint32_t> mConvexMasksDeviceCopy;

	DxBatchedStorage<physx::PxVec4> mCollisionPlanes;
	DxBuffer<physx::PxVec4> mCollisionPlanesDeviceCopy;

	DxBatchedStorage<physx::PxVec3> mCollisionTriangles;
	DxBuffer<physx::PxVec3> mCollisionTrianglesDeviceCopy;

	DxBatchedStorage<physx::PxVec4> mMotionConstraints;
	DxBatchedStorage<physx::PxVec4> mSeparationConstraints;

	DxBatchedStorage<physx::PxVec4> mRestPositions;
	DxBuffer<physx::PxVec4> mRestPositionsDeviceCopy;

	DxBatchedStorage<uint32_t> mSelfCollisionIndices;
	DxBatchedStorage<physx::PxVec4> mSelfCollisionParticles;
	DxBatchedStorage<uint32_t> mSelfCollisionData;

	DxBatchedStorage<uint32_t> mTriangles;
};
}
}
