// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreDelegates.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Texture.h"
#include "HAL/ThreadSafeBool.h"

class FGoogleARCoreDeviceCameraBlitter
{
public:

	FGoogleARCoreDeviceCameraBlitter();
	~FGoogleARCoreDeviceCameraBlitter();

	void DoBlit(uint32_t TextureId, FIntPoint ImageSize);
	UTexture2D* GetLastCameraImageTexture();

private:
	void LateInit(FIntPoint ImageSize);
	
#if PLATFORM_ANDROID
	void CopyTextureToVulkan(UTexture2D* TargetTexture);
	void DeleteOpenGLTextures();
#endif
	
	uint32 CurrentCameraCopy;
	uint32 BlitShaderProgram;
	uint32 BlitShaderProgram_Uniform_CameraTexture;
	uint32 BlitShaderProgram_Attribute_InPos;
	uint32 FrameBufferObject;
	uint32 VertexBufferObject;
	TArray<UTexture2D*> CameraCopies;
    TArray<uint32*> CameraCopyIds;
	
	FIntPoint CameraCopySize = FIntPoint::ZeroValue;
};
