// Engine/Source/Runtime/WebBrowser/Private/WebBrowserModule.cpp

#include "WebInterfaceBrowserModule.h"
#include "WebInterfaceBrowserLog.h"
#include "WebInterfaceBrowserSingleton.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#if WITH_CEF3
#	include "CEF3Utils.h"
#endif

DEFINE_LOG_CATEGORY(LogWebInterfaceBrowser);

static FWebInterfaceBrowserSingleton* WebBrowserSingleton = nullptr;

FWebInterfaceBrowserInitSettings::FWebInterfaceBrowserInitSettings()
	: ProductVersion(FString::Printf(TEXT("%s/%s UnrealEngine/%s Chrome/59.0.3071.15"), FApp::GetProjectName(), FApp::GetBuildVersion(), *FEngineVersion::Current().ToString()))
{
}

class FWebInterfaceBrowserModule : public IWebInterfaceBrowserModule
{
private:
	// IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	virtual IWebInterfaceBrowserSingleton* GetSingleton() override;
	virtual bool CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings) override;
};

IMPLEMENT_MODULE( FWebInterfaceBrowserModule, WebBrowserUI );

void FWebInterfaceBrowserModule::StartupModule()
{
#if WITH_CEF3
	CEF3Utils::LoadCEF3Modules();
#endif
}

void FWebInterfaceBrowserModule::ShutdownModule()
{
	if (WebBrowserSingleton != nullptr)
	{
		delete WebBrowserSingleton;
		WebBrowserSingleton = nullptr;
	}

#if WITH_CEF3
	CEF3Utils::UnloadCEF3Modules();
#endif
}

bool FWebInterfaceBrowserModule::CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings)
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(WebBrowserInitSettings);
		return true;
	}
	return false;
}

IWebInterfaceBrowserSingleton* FWebInterfaceBrowserModule::GetSingleton()
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(FWebInterfaceBrowserInitSettings());
	}
	return WebBrowserSingleton;
}
