// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Distance/DistPoint3Triangle3.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "BoxTypes.h"
#include "IndexTypes.h"

template <class TriangleMeshType>
class TMeshQueries
{
public:
	TMeshQueries() = delete;

	/**
	 * construct a DistPoint3Triangle3 object for a Mesh triangle
	 */
	static FDistPoint3Triangle3d TriangleDistance(const TriangleMeshType& Mesh, int TriIdx, FVector3d Point)
	{
		check(Mesh.IsTriangle(TriIdx));
		FTriangle3d tri;
		Mesh.GetTriVertices(TriIdx, tri.V[0], tri.V[1], tri.V[2]);
		FDistPoint3Triangle3d q(Point, tri);
		q.GetSquared();
		return q;
	}

	/**
	 * convenience function to construct a IntrRay3Triangle3 object for a Mesh triangle
	 */
	static FIntrRay3Triangle3d TriangleIntersection(const TriangleMeshType& Mesh, int TriIdx, const FRay3d& Ray)
	{
		check(Mesh.IsTriangle(TriIdx));
		FTriangle3d tri;
		Mesh.GetTriVertices(TriIdx, tri.V[0], tri.V[1], tri.V[2]);
		FIntrRay3Triangle3d q(Ray, tri);
		q.Find();
		return q;
	}

	/**
	 * Compute triangle centroid
	 * @param Mesh Mesh with triangle
	 * @param TriIdx Index of triangle
	 * @return Computed centroid
	 */
	static FVector3d GetTriCentroid(const TriangleMeshType& Mesh, int TriIdx)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		return Triangle.Centroid();
	}

	/**
	 * Compute the normal, area, and centroid of a triangle all together
	 * @param Mesh Mesh w/ triangle
	 * @param TriIdx Index of triangle
	 * @param Normal Computed normal (returned by reference)
	 * @param Area Computed area (returned by reference)
	 * @param Centroid Computed centroid (returned by reference)
	 */
	static void GetTriNormalAreaCentroid(const TriangleMeshType& Mesh, int TriIdx, FVector3d& Normal, double& Area, FVector3d& Centroid)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		Centroid = Triangle.Centroid();
		Normal = VectorUtil::FastNormalArea(Triangle.V[0], Triangle.V[1], Triangle.V[2], Area);
	}

	static FAxisAlignedBox3d GetTriBounds(const TriangleMeshType& Mesh, int TID)
	{
		FIndex3i TriInds = Mesh.GetTriangle(TID);
		FVector3d MinV, MaxV, V = Mesh.GetVertex(TriInds.A);
		MinV = MaxV = V;
		for (int i = 1; i < 3; ++i)
		{
			V = Mesh.GetVertex(TriInds[i]);
			if (V.X < MinV.X)				MinV.X = V.X;
			else if (V.X > MaxV.X)			MaxV.X = V.X;
			if (V.Y < MinV.Y)				MinV.Y = V.Y;
			else if (V.Y > MaxV.Y)			MaxV.Y = V.Y;
			if (V.Z < MinV.Z)				MinV.Z = V.Z;
			else if (V.Z > MaxV.Z)			MaxV.Z = V.Z;
		}
		return FAxisAlignedBox3d(MinV, MaxV);
	}

	// brute force search for nearest triangle to Point
	static int FindNearestTriangle_LinearSearch(const TriangleMeshType& Mesh, const FVector3d& P)
	{
		int tNearest = IndexConstants::InvalidID;
		double fNearestSqr = TNumericLimits<double>::Max();
		for (int TriIdx : Mesh.TriangleIndicesItr())
		{
			double distSqr = TriDistanceSqr(Mesh, TriIdx, P);
			if (distSqr < fNearestSqr)
			{
				fNearestSqr = distSqr;
				tNearest = TriIdx;
			}
		}
		return tNearest;
	}

	/**
	 * Compute distance from Point to triangle in Mesh, with minimal extra objects/etc
	 */
	static double TriDistanceSqr(const TriangleMeshType& Mesh, int TriIdx, const FVector3d& Point)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		FDistPoint3Triangle3d Distance(Point, Triangle);
		return Distance.GetSquared();
	}

	// brute force search for nearest triangle intersection
	static int FindHitTriangle_LinearSearch(const TriangleMeshType& Mesh, const FRay3d& Ray)
	{
		int tNearestID = IndexConstants::InvalidID;
		double fNearestT = TNumericLimits<double>::Max();
		FTriangle3d Triangle;

		for (int TriIdx : Mesh.TriangleIndicesItr())
		{
			Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(Ray, Triangle);
			if (Query.Find())
			{
				if (Query.RayParameter < fNearestT)
				{
					fNearestT = Query.RayParameter;
					tNearestID = TriIdx;
				}
			}
		}

		return tNearestID;
	}

	/**
	 * convenience function to construct a IntrRay3Triangle3 object for a Mesh triangle
	 */
	static FIntrRay3Triangle3d RayTriangleIntersection(const TriangleMeshType& Mesh, int TriIdx, const FRay3d& Ray)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		FIntrRay3Triangle3d Query(Ray, Triangle);
		Query.Find();
		return Query;
	}
};
