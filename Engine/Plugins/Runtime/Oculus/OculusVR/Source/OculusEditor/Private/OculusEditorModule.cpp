// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "OculusEditorModule.h"
#include "OculusHMDRuntimeSettings.h"
#include "Modules/ModuleManager.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/Class.h"
#include "OculusAssetDirectory.h"

// Settings
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "OculusEditor"

//////////////////////////////////////////////////////////////////////////
// FOculusEditor

class FOculusEditor : public IOculusEditorModule
{
public:
	virtual void StartupModule() override
	{				
		RegisterSettings();
		FOculusAssetDirectory::LoadForCook();
	}

	virtual void ShutdownModule() override
	{
		FOculusAssetDirectory::ReleaseAll();

		if (UObjectInitialized())
		{
			UnregisterSettings();  
		}		
	}
private:
	
	void RegisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "OculusVR",
				LOCTEXT("RuntimeSettingsName", "OculusVR"),
				LOCTEXT("RuntimeSettingsDescription", "Configure the OculusVR plugin"),
				GetMutableDefault<UOculusHMDRuntimeSettings>()
			);
		}
	}

	void UnregisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "OculusVR");
		}
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FOculusEditor, OculusEditor);

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE