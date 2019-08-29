// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshBuild.h: Contains commonly used functions and classes for building
	mesh data into engine usable form.

=============================================================================*/

#pragma once

#include "CoreMinimal.h"

struct FOverlappingThresholds
{
public:
	FOverlappingThresholds()
		: ThresholdPosition(THRESH_POINTS_ARE_SAME)
		, ThresholdTangentNormal(THRESH_NORMALS_ARE_SAME)
		, ThresholdUV(THRESH_UVS_ARE_SAME)
	{}

	/** Threshold use to decide if two vertex position are equal. */
	float ThresholdPosition;
	
	/** Threshold use to decide if two normal, tangents or bi-normals are equal. */
	float ThresholdTangentNormal;
	
	/** Threshold use to decide if two UVs are equal. */
	float ThresholdUV;
};


/**
 * Returns true if the specified points are about equal
 */
inline bool PointsEqual(const FVector& V1,const FVector& V2, bool bUseEpsilonCompare = true )
{
	const float Epsilon = bUseEpsilonCompare ? THRESH_POINTS_ARE_SAME : 0.0f;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon && FMath::Abs(V1.Z - V2.Z) <= Epsilon;
}

inline bool PointsEqual(const FVector& V1, const FVector& V2, const FOverlappingThresholds& OverlappingThreshold)
{
	const float Epsilon = OverlappingThreshold.ThresholdPosition;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon && FMath::Abs(V1.Z - V2.Z) <= Epsilon;
}


/**
 * Returns true if the specified normal vectors are about equal
 */
inline bool NormalsEqual(const FVector& V1,const FVector& V2)
{
	const float Epsilon = THRESH_NORMALS_ARE_SAME;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon && FMath::Abs(V1.Z - V2.Z) <= Epsilon;
}

inline bool UVsEqual(const FVector2D& V1, const FVector2D& V2)
{
	const float Epsilon = 1.0f / 1024.0f;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon;
}

inline bool NormalsEqual(const FVector& V1,const FVector& V2, const FOverlappingThresholds& OverlappingThreshold)
{
	const float Epsilon = OverlappingThreshold.ThresholdTangentNormal;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon && FMath::Abs(V1.Z - V2.Z) <= Epsilon;
}

inline bool UVsEqual(const FVector2D& V1, const FVector2D& V2, const FOverlappingThresholds& OverlappingThreshold)
{
	const float Epsilon = OverlappingThreshold.ThresholdUV;
	return FMath::Abs(V1.X - V2.X) <= Epsilon && FMath::Abs(V1.Y - V2.Y) <= Epsilon;
}
