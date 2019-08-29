/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#include "RTdef.h"
#if RT_COMPILE
#include "PolygonTriangulatorBase.h"
#include <PxAssert.h>

namespace nvidia
{
namespace fracture
{
namespace base
{

using namespace nvidia;

// ------ singleton pattern -----------------------------------------------------------

//static PolygonTriangulator *gPolygonTriangulator = NULL;
//
//PolygonTriangulator* PolygonTriangulator::getInstance() 
//{
//	if (gPolygonTriangulator == NULL) {
//		gPolygonTriangulator = PX_NEW(PolygonTriangulator)();
//	}
//	return gPolygonTriangulator;
//}
//
//void PolygonTriangulator::destroyInstance() 
//{
//	if (gPolygonTriangulator != NULL) {
//		PX_DELETE(gPolygonTriangulator);
//	}
//	gPolygonTriangulator = NULL;
//}

// -------------------------------------------------------------------------------------
PolygonTriangulator::PolygonTriangulator(SimScene* scene):
	mScene(scene)
{
}

// -------------------------------------------------------------------------------------
PolygonTriangulator::~PolygonTriangulator() 
{
}

// -------------------------------------------------------------------------------------
float PolygonTriangulator::cross(const PxVec3 &p0, const PxVec3 &p1)
{
	return p0.x * p1.y - p0.y * p1.x;
}

// -------------------------------------------------------------------------------------
bool PolygonTriangulator::inTriangle(const PxVec3 &p, const PxVec3 &p0, const PxVec3 &p1, const PxVec3 &p2)
{
	float d0 = cross(p1 - p0, p - p0);
	float d1 = cross(p2 - p1, p - p1);
	float d2 = cross(p0 - p2, p - p2);
	return (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) ||
		(d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
}

// -------------------------------------------------------------------------------------
void PolygonTriangulator::triangulate(const PxVec3 *points, int numPoints, const int *indices, PxVec3 *planeNormal)
{
	mIndices.clear();

	if (numPoints < 3)
		return;
	if (numPoints == 3) {
		if (indices == NULL) {
			mIndices.pushBack(0);
			mIndices.pushBack(1);
			mIndices.pushBack(2);
		}
		else {
			mIndices.pushBack(indices[0]);
			mIndices.pushBack(indices[1]);
			mIndices.pushBack(indices[2]);
		}
		return;
	}

	bool isConvex;
	importPoints(points, numPoints, indices, planeNormal, isConvex);

	if (isConvex) {		// fast path
		mIndices.resize(3*((uint32_t)numPoints-2));
		for (int i = 0; i < numPoints-2; i++) {
			mIndices[3*(uint32_t)i]   = 0;
			mIndices[3*(uint32_t)i+1] = i+1;
			mIndices[3*(uint32_t)i+2] = i+2;
		}
	}
	else
		clipEars();

	if (indices != NULL) {
		for (uint32_t i = 0; i < mIndices.size(); i++) {
			mIndices[i] = indices[(uint32_t)mIndices[i]];
		}
	}
}

// -------------------------------------------------------------------------------------
void PolygonTriangulator::importPoints(const PxVec3 *points, int numPoints, const int *indices, PxVec3 *planeNormal, bool &isConvex)
{
	// find projection 3d -> 2d;
	PxVec3 n;

	isConvex = true;

	if (planeNormal) 
		n = *planeNormal;
	else {
		PX_ASSERT(numPoints >= 3);
		n = PxVec3(0.0f, 0.0f, 0.0f);

		for (int i = 1; i < numPoints-1; i++) {
			int i0 = 0;
			int i1 = i;
			int i2 = i+1;
			if (indices) {
				i0 = indices[i0];
				i1 = indices[i1];
				i2 = indices[i2];
			}
			const PxVec3 &p0 = points[i0];
			const PxVec3 &p1 = points[i1];
			const PxVec3 &p2 = points[i2];
			PxVec3 ni = (p1-p0).cross(p2-p0);
			if (i > 1 && ni.dot(n) < 0.0f)
				isConvex = false;
			n += ni;
		}
	}

	n.normalize();
	PxVec3 t0,t1;

	if (fabs(n.x) < fabs(n.y) && fabs(n.x) < fabs(n.z))
		t0 = PxVec3(1.0f, 0.0f, 0.0f);
	else if (fabs(n.y) < fabs(n.z))
		t0 = PxVec3(0.0f, 1.0f, 0.0f);
	else
		t0 = PxVec3(0.0f, 0.0f, 1.0f);
	t1 = n.cross(t0);
	t1.normalize();
	t0 = t1.cross(n);
	
	mPoints.resize((uint32_t)numPoints);
	if (indices == NULL) {
		for (uint32_t i = 0; i < (uint32_t)numPoints; i++) 
			mPoints[i] = PxVec3(points[i].dot(t0), points[i].dot(t1), 0.0f);
	}
	else {
		for (uint32_t i = 0; i < (uint32_t)numPoints; i++) {
			const PxVec3 &p = points[(uint32_t)indices[i]];
			mPoints[i] = PxVec3(p.dot(t0), p.dot(t1), 0.0f);
		}
	}
}

// -------------------------------------------------------------------------------------
void PolygonTriangulator::clipEars()
{
	// init
	int32_t num = (int32_t)mPoints.size();

	mCorners.resize((uint32_t)num);

	for (int i = 0; i < num; i++) {
		Corner &c = mCorners[(uint32_t)i];
		c.prev = (i > 0) ? i-1 : num-1;
		c.next = (i < num-1) ? i + 1 : 0;
		c.isEar = false;
		c.angle = 0.0f;
		PxVec3 &p0 = mPoints[(uint32_t)c.prev];
		PxVec3 &p1 = mPoints[(uint32_t)i];
		PxVec3 &p2 = mPoints[(uint32_t)c.next];
		PxVec3 n1 = p1-p0;
		PxVec3 n2 = p2-p1;
		if (cross(n1, n2) > 0.0f) {
			n1.normalize();
			n2.normalize();
			c.angle = n1.dot(n2);

			c.isEar = true;
			int nr = (i+2) % num;
			for (int j = 0; j < num-3; j++) {
				if (inTriangle(mPoints[(uint32_t)nr], p0,p1,p2)) {
					c.isEar = false;
					break;
				}
				nr = (nr+1) % num;
			}
		}
	}

	int firstCorner = 0;
	int numCorners = num;

	while (numCorners > 3) {

		// find best ear
		float minAng = FLT_MAX;
		int minNr = -1;

		int nr = firstCorner;
		for (int i = 0; i < numCorners; i++) {
			Corner &c = mCorners[(uint32_t)nr];
			if (c.isEar && c.angle < minAng) {
				minAng = c.angle;
				minNr = nr;
			}
			nr = c.next;
		}

		// cut ear
//		assert(minNr >= 0);
if (minNr < 0)
break;
		Corner &cmin = mCorners[(uint32_t)minNr];
		mIndices.pushBack(cmin.prev);
		mIndices.pushBack(minNr);
		mIndices.pushBack(cmin.next);
		mCorners[(uint32_t)cmin.prev].next = cmin.next;
		mCorners[(uint32_t)cmin.next].prev = cmin.prev;

		if (firstCorner == minNr)
			firstCorner = cmin.next;
		numCorners--;
		if (numCorners == 3)
			break;

		// new ears?
		for (int i = 0; i < 2; i++) {
			uint32_t i1 = uint32_t((i == 0) ? cmin.prev : cmin.next);
			uint32_t i0 = (uint32_t)mCorners[i1].prev;
			uint32_t i2 = (uint32_t)mCorners[i1].next;

			PxVec3 &p0 = mPoints[i0];
			PxVec3 &p1 = mPoints[i1];
			PxVec3 &p2 = mPoints[i2];
			PxVec3 n1 = p1-p0;
			PxVec3 n2 = p2-p1;
			if (cross(n1, n2) > 0.0f) {
				n1.normalize();
				n2.normalize();
				mCorners[i1].angle = n1.dot(n2);
				
				mCorners[i1].isEar = true;
				int nr = mCorners[i2].next;
				for (int j = 0; j < numCorners-3; j++) {
					if (inTriangle(mPoints[(uint32_t)nr], p0,p1,p2)) {
						mCorners[i1].isEar = false;
						break;
					}
					nr = mCorners[(uint32_t)nr].next;
				}
			}
		}
	}
	int id = firstCorner;
	mIndices.pushBack(id);
	id = mCorners[(uint32_t)id].next;
	mIndices.pushBack(id);
	id = mCorners[(uint32_t)id].next;
	mIndices.pushBack(id);
}

}
}
}
#endif