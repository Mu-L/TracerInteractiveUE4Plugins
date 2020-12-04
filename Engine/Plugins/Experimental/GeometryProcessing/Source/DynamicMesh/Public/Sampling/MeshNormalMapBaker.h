// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"
#include "MeshTangents.h"


class DYNAMICMESH_API FMeshNormalMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshNormalMapBaker() {}

	//
	// Required input data
	//

	const TMeshTangents<double>* BaseMeshTangents;


	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult() const { return NormalsBuilder; }
	
	TUniquePtr<TImageBuilder<FVector3f>> TakeResult() { return MoveTemp(NormalsBuilder); }

protected:
	TUniquePtr<TImageBuilder<FVector3f>> NormalsBuilder;

};