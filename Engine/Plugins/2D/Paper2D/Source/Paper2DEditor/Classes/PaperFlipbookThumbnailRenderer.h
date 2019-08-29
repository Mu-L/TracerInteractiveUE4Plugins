// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PaperSpriteThumbnailRenderer.h"
#include "PaperFlipbookThumbnailRenderer.generated.h"

class FCanvas;
class FRenderTarget;

UCLASS()
class UPaperFlipbookThumbnailRenderer : public UPaperSpriteThumbnailRenderer
{
	GENERATED_UCLASS_BODY()

	// UThumbnailRenderer interface
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas) override;
	// End of UThumbnailRenderer interface
};
