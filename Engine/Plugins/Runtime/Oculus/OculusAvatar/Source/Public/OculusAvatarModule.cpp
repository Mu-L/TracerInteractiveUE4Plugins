// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusAvatarModule.h"
#include "Features/IModularFeatures.h"
#include "OvrAvatarManager.h"

void OculusAvatarModule::StartupModule()
{
	UOvrAvatarManager::Get().InitializeSDK();
}
 
void OculusAvatarModule::ShutdownModule()
{
	UOvrAvatarManager::Destroy();
}

IMPLEMENT_MODULE(OculusAvatarModule, OculusAvatar)
