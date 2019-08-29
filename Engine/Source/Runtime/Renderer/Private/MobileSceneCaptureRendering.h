// Copyright 1998-2018 Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRenderTarget;
class FRHICommandListImmediate;
class FSceneRenderer;
class FTexture;
struct FResolveParams;

void UpdateSceneCaptureContentMobile_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& OwnerName,
	const FResolveParams& ResolveParams);
