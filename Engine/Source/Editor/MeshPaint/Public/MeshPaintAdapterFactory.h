// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshPaintModule.h"

class UMeshComponent;
class IMeshPaintGeometryAdapterFactory;
class FReferenceCollector;

class MESHPAINT_API FMeshPaintAdapterFactory
{
public:
	static TArray<TSharedPtr<IMeshPaintGeometryAdapterFactory>> FactoryList;

public:
	static TSharedPtr<class IMeshPaintGeometryAdapter> CreateAdapterForMesh(UMeshComponent* InComponent, int32 InPaintingMeshLODIndex);
	static void InitializeAdapterGlobals();
	static void AddReferencedObjectsGlobals(FReferenceCollector& Collector);
	static void CleanupGlobals();
};
