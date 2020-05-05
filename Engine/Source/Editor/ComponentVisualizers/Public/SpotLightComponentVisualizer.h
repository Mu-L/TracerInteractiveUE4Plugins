// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"
#include "PointLightComponentVisualizer.h"

class FPrimitiveDrawInterface;
class FSceneView;

class COMPONENTVISUALIZERS_API FSpotLightComponentVisualizer : public FComponentVisualizer
{
public:
	//~ Begin FComponentVisualizer Interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	//~ End FComponentVisualizer Interface

private:
	FTextureLightProfileVisualizer LightProfileVisualizer;
};
