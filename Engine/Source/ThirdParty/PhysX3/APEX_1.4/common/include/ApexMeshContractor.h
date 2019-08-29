/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef APEX_MESH_CONTRACTOR_H
#define APEX_MESH_CONTRACTOR_H

#include "ApexDefs.h"
#include "ApexUsingNamespace.h"
#include "PsArray.h"
#include "PxVec3.h"
#include "PsUserAllocated.h"

namespace nvidia
{
namespace apex
{

class IProgressListener;

class ApexMeshContractor : public UserAllocated
{
public:
	ApexMeshContractor();

	void registerVertex(const PxVec3& pos);
	void registerTriangle(uint32_t v0, uint32_t v1, uint32_t v2);
	bool endRegistration(uint32_t subdivision, IProgressListener* progress);

	uint32_t contract(int32_t steps, float abortionRatio, float& volumeRatio, IProgressListener* progress);
	void expandBorder();

	uint32_t getNumVertices()
	{
		return mVertices.size();
	}
	uint32_t getNumIndices()
	{
		return mIndices.size();
	}
	const PxVec3* getVertices()
	{
		return mVertices.begin();
	}
	const uint32_t* getIndices()
	{
		return mIndices.begin();
	}
private:

	void computeNeighbours();
	void computeSignedDistanceField();
	void contractionStep();
	void computeAreaAndVolume(float& area, float& volume);

	void addTriangle(const PxVec3& v0, const PxVec3& v1, const PxVec3& v2);
	bool updateDistance(uint32_t xi, uint32_t yi, uint32_t zi);
	void setInsideOutside();
	void interpolateGradientAt(const PxVec3& pos, PxVec3& grad);
	void subdivide(float spacing);
	void collapse(float spacing);

	void getButterfly(uint32_t triNr, uint32_t v0, uint32_t v1, int32_t& adj, int32_t& t0, int32_t& t1, int32_t& t2, int32_t& t3) const;
	int32_t getOppositeVertex(int32_t t, uint32_t v0, uint32_t v1) const;
	void replaceVertex(int32_t t, uint32_t vOld, uint32_t vNew);
	void replaceNeighbor(int32_t t, int32_t nOld, uint32_t nNew);
	bool triangleContains(int32_t t, uint32_t v) const;
	bool legalCollapse(int32_t triNr, uint32_t v0, uint32_t v1) const;
	void advanceAdjTriangle(uint32_t v, int32_t& t, int32_t& prev) const;
	bool areNeighbors(int32_t t0, int32_t t1) const;
	float findMin(const PxVec3& p, const PxVec3& maxDisp) const;
	float interpolateDistanceAt(const PxVec3& pos) const;
	void collectNeighborhood(int32_t triNr, float radius, uint32_t newMark, physx::Array<int32_t> &tris, physx::Array<float> &dists, uint32_t* triMarks) const;
	void getTriangleCenter(int32_t triNr, PxVec3& center) const;
	float curvatureAt(int triNr, int v);

	struct ContractorCell
	{
		ContractorCell() : inside(0), distance(PX_MAX_F32), marked(false)
		{
			numCuts[0] = numCuts[1] = numCuts[2] = 0;
		}
		/*
		void init() {
			distance = PX_MAX_F32;
			inside = 0;
			marked = false;
			numCuts[0] = 0;
			numCuts[1] = 0;
			numCuts[2] = 0;
		}
		*/
		uint32_t inside;
		float distance;
		uint8_t numCuts[3];
		bool marked;
	};
	inline ContractorCell& cellAt(int32_t xi, int32_t yi, int32_t zi)
	{
		return mGrid[(((uint32_t)xi * mNumY) + (uint32_t)yi) * mNumZ + (uint32_t)zi];
	}

	inline const ContractorCell& constCellAt(int32_t xi, int32_t yi, int32_t zi) const
	{
		return mGrid[(((uint32_t)xi * mNumY) + (uint32_t)yi) * mNumZ + (uint32_t)zi];
	}
	float mCellSize;
	PxVec3 mOrigin;

	uint32_t mNumX, mNumY, mNumZ;

	physx::Array<PxVec3> mVertices;
	physx::Array<uint32_t> mIndices;
	physx::Array<int32_t> mNeighbours;

	physx::Array<ContractorCell> mGrid;
	physx::Array<float> mVertexCurvatures;

	float mInitialVolume;
	float mCurrentVolume;
};

}
} // end namespace nvidia::apex

#endif // APEX_MESH_CONTRACTOR_H
