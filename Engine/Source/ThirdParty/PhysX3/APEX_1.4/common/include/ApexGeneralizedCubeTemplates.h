/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef __APEX_GENERALIZED_CUBE_TEMPLATES_H__
#define __APEX_GENERALIZED_CUBE_TEMPLATES_H__

#include "ApexUsingNamespace.h"
#include "PsUserAllocated.h"
#include "PsArray.h"

#include "PxVec3.h"

namespace nvidia
{
namespace apex
{

class ApexGeneralizedCubeTemplates : public UserAllocated
{
public:
	ApexGeneralizedCubeTemplates();

	void getTriangles(const int groups[8], physx::Array<int32_t> &indices);

private:

	enum AllConsts
	{
		GEN_NUM_SUB_CELLS	= 6,
		SUB_GRID_LEN		= GEN_NUM_SUB_CELLS,
		SUB_GRID_LEN_2		= GEN_NUM_SUB_CELLS * GEN_NUM_SUB_CELLS,
		NUM_SUB_CELLS		= GEN_NUM_SUB_CELLS * GEN_NUM_SUB_CELLS * GEN_NUM_SUB_CELLS,
		NUM_CUBE_VERTS		= 19,
		NUM_CASES_3			= 6561, // 2^8
	};
	struct GenSubCell
	{
		inline void init()
		{
			group = -1;
			marked = false;
		}
		int32_t group;
		bool marked;
	};

	struct GenCoord
	{
		void init(int32_t xi, int32_t yi, int32_t zi)
		{
			this->xi = xi;
			this->yi = yi;
			this->zi = zi;
		}
		bool operator == (const GenCoord& c) const
		{
			return xi == c.xi && yi == c.yi && zi == c.zi;
		}

		int32_t xi, yi, zi;
	};


	void createLookupTable3();
	void setCellGroups(const int32_t groups[8]);
	void splitDisconnectedGroups();
	void findVertices();
	void createTriangles(physx::Array<int32_t>& currentIndices);
	bool isEdge(const GenCoord& c, int32_t dim, int32_t group0, int32_t group1);


	inline uint32_t cellNr(uint32_t x, uint32_t y, uint32_t z)
	{
		return x * SUB_GRID_LEN_2 + y * SUB_GRID_LEN + z;
	}

	inline int32_t groupAt(int32_t x, int32_t y, int32_t z)
	{
		if (x < 0 || x >= SUB_GRID_LEN || y < 0 || y >= SUB_GRID_LEN || z < 0 || z >= SUB_GRID_LEN)
		{
			return -1;
		}
		return mSubGrid[x * SUB_GRID_LEN_2 + y * SUB_GRID_LEN + z].group;
	}

	inline bool vertexMarked(const GenCoord& c)
	{
		if (c.xi < 0 || c.xi > SUB_GRID_LEN || c.yi < 0 || c.yi > SUB_GRID_LEN || c.zi < 0 || c.zi > SUB_GRID_LEN)
		{
			return false;
		}
		return mVertexMarked[c.xi][c.yi][c.zi];
	}

	inline void markVertex(const GenCoord& c)
	{
		if (c.xi < 0 || c.xi > SUB_GRID_LEN || c.yi < 0 || c.yi > SUB_GRID_LEN || c.zi < 0 || c.zi > SUB_GRID_LEN)
		{
			return;
		}
		mVertexMarked[c.xi][c.yi][c.zi] = true;
	}




	float mBasis[NUM_SUB_CELLS][8];
	PxVec3 mVertPos[NUM_CUBE_VERTS];
	int mVertexAt[SUB_GRID_LEN + 1][SUB_GRID_LEN + 1][SUB_GRID_LEN + 1];
	bool mVertexMarked[SUB_GRID_LEN + 1][SUB_GRID_LEN + 1][SUB_GRID_LEN + 1];

	GenSubCell mSubGrid[NUM_SUB_CELLS];
	int32_t mFirst3[NUM_CASES_3];	// 3^8

	physx::Array<int32_t> mLookupIndices3;

	int32_t mFirstPairVertex[8][8];
	GenCoord mFirstPairCoord[8][8];

};

}
} // end namespace nvidia::apex

#endif
