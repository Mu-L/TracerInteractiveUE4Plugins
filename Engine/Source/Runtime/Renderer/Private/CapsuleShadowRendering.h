// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CapsuleShadowRendering.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

extern int32 GCapsuleShadows;
extern int32 GCapsuleDirectShadows;
extern int32 GCapsuleIndirectShadows;

inline bool DoesPlatformSupportCapsuleShadows(EShaderPlatform Platform)
{
	// Hasn't been tested elsewhere yet
	return Platform == SP_PCD3D_SM5 || Platform == SP_PS4 || Platform == SP_XBOXONE_D3D12
		|| IsMetalSM5Platform(Platform)
		|| IsVulkanSM5Platform(Platform)
		|| Platform == SP_SWITCH
		|| FDataDrivenShaderPlatformInfo::GetInfo(Platform).bSupportsCapsuleShadows;
}

inline bool SupportsCapsuleShadows(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GCapsuleShadows
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportCapsuleShadows(ShaderPlatform);
}

inline bool SupportsCapsuleDirectShadows(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GCapsuleDirectShadows && SupportsCapsuleShadows(FeatureLevel, ShaderPlatform);
}

inline bool SupportsCapsuleIndirectShadows(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GCapsuleIndirectShadows && SupportsCapsuleShadows(FeatureLevel, ShaderPlatform);
}
