// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"



class MODELINGOPERATORSEDITORONLY_API FUVLayoutOp : public FDynamicMeshOperator
{
public:
	virtual ~FUVLayoutOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	bool bSeparateUVIslands;
	int TextureResolution;
	float UVScaleFactor;


	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


