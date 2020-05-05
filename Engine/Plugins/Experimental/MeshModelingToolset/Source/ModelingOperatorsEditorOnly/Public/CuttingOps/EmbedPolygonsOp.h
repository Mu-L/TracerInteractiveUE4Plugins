// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "ProxyLODVolume.h"

#include "Polygon2.h"

#include "EmbedPolygonsOp.generated.h"


UENUM()
enum class EEmbeddedPolygonOpMethod : uint8
{
	CutAndFill,
	CutThrough
	//, Extrude  // TODO: extrude(/intrude?) would also be easy/natural to support here
};


class MODELINGOPERATORSEDITORONLY_API FEmbedPolygonsOp : public FDynamicMeshOperator
{
public:
	virtual ~FEmbedPolygonsOp() {}

	
	// inputs
	FFrame3d PolygonFrame;
	FPolygon2d EmbedPolygon;

	// TODO: stop hardcoding the polygon shape, switch to FGeneralPolygon2d
	FPolygon2d GetPolygon()
	{
		return EmbedPolygon;
	}

	bool bDiscardAttributes;

	EEmbeddedPolygonOpMethod Operation;

	//float ExtrudeDistance; // TODO if we support extrude

	TSharedPtr<FDynamicMesh3> OriginalMesh;

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


