// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NavMesh/RecastHelpers.h"

FVector Unreal2RecastPoint(const FVector& UnrealPoint)
{
	return FVector(-UnrealPoint[0], UnrealPoint[2], -UnrealPoint[1]);
}

FVector Unreal2RecastPoint(const float* UnrealPoint)
{
	return FVector(-UnrealPoint[0], UnrealPoint[2], -UnrealPoint[1]);
}

FBox Unreal2RecastBox(const FBox& UnrealBox)
{
	FVector Points[2] = { Unreal2RecastPoint(UnrealBox.Min), Unreal2RecastPoint(UnrealBox.Max) };
	return FBox(Points, 2);
}

FMatrix Unreal2RecastMatrix()
{
	static FMatrix TM(FVector(-1.f,0,0),FVector(0,0,-1.f),FVector(0,1.f,0),FVector::ZeroVector);
	return TM;
}

FVector Recast2UnrealPoint(const float* RecastPoint)
{
	return FVector(-RecastPoint[0], -RecastPoint[2], RecastPoint[1]);
}

FVector Recast2UnrealPoint(const FVector& RecastPoint)
{
	return FVector(-RecastPoint.X, -RecastPoint.Z, RecastPoint.Y);
}

FBox Recast2UnrealBox(const float* RecastMin, const float* RecastMax)
{
	FVector Points[2] = { Recast2UnrealPoint(RecastMin), Recast2UnrealPoint(RecastMax) };
	return FBox(Points, 2);
}

FBox Recast2UnrealBox(const FBox& RecastBox)
{
	FVector Points[2] = { Recast2UnrealPoint(RecastBox.Min), Recast2UnrealPoint(RecastBox.Max) };
	return FBox(Points, 2);
}

FMatrix Recast2UnrealMatrix()
{
	static FMatrix TM(FVector(-1.f, 0, 0), FVector(0, 0, -1.f), FVector(0, 1.f, 0), FVector::ZeroVector);
	return TM;
}