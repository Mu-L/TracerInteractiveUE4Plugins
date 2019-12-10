// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBevelLinear;
class FContour;
struct FPart;

class FIntersection
{
public:
	static const int MinContourSizeForIntersectionFar = 4;


	FIntersection(FBevelLinear* const BevelIn, FContour* const ContourIn);
	virtual ~FIntersection();

	float GetValue() const;

	virtual void BevelTillThis() = 0;

protected:
	FBevelLinear* const Bevel;
	FContour& Contour;


	FPart* GetVertex() const;
	/**
	 * Check if intersection value for point is less then stored one and store this if it is.
	 * @param Point - Point that is checked.
	 * @param Expand - Point's expand value.
	 * @return Is it less then stored one?
	 */
	bool ContourHasCloserIntersectionAt(FPart* const Point, const float Expand);

private:
	/** Point whose normal intersects. */
	FPart* Vertex;

	/** Offset in surface of front cap till point of intersection. */
	float Value;
};

/** Intersection of point's normal with next point's normal. */
class FIntersectionNear final : public FIntersection
{
public:
	FIntersectionNear(FBevelLinear* const Bevel, FContour* const ContourIn);

	void BevelTillThis() override;
};

/** Intersection of point's normal with edge. */
class FIntersectionFar final : public FIntersection
{
public:
	FIntersectionFar(FBevelLinear* const Bevel, FContour* const ContourIn);

	void BevelTillThis() override;

private:
	/** Edge with which point's normal intersects. */
	FPart* SplitEdge;


	/**
	 * Update list of expands for contour (after beveling till IntersectionFar contour is split to 2 contours).
	 * @param UpdatedContour - Updated part.
	 * @param OtherContour - Not updated part.
	 * @param Curr - Point that splits contours.
	 * @param SplitEdgePart - Part of split edge that belongs to UpdatedContour.
	 * @param PrevDelta - Distance that Curr->Prev passed during FIntersectionFar::BevelTillThis.
	 * @param NextDelta - Distance that Curr->Next passed during FIntersectionFar::BevelTillThis.
	 */
	void UpdateExpandsFar(FContour* const UpdatedContour, const FContour& OtherContour, FPart* const Curr, const FPart* const SplitEdgePart, const float PrevDelta, const float NextDelta);
};
