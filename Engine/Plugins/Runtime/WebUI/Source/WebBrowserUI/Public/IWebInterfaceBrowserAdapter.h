// Engine/Source/Runtime/WebBrowser/Public/IWebBrowserAdapter.h
#pragma once

#include "CoreMinimal.h"
#include "IWebInterfaceBrowserWindow.h"

class IWebInterfaceBrowserAdapter
{
public:

	virtual FString GetName() const = 0;

	virtual bool IsPermanent() const = 0;

	virtual void ConnectTo(const TSharedRef<IWebInterfaceBrowserWindow>& BrowserWindow) = 0;

	virtual void DisconnectFrom(const TSharedRef<IWebInterfaceBrowserWindow>& BrowserWindow) = 0;

};

class WEBBROWSERUI_API FWebInterfaceBrowserAdapterFactory
{ 
public: 

	static TSharedRef<IWebInterfaceBrowserAdapter> Create(const FString& Name, UObject* JSBridge, bool IsPermanent); 

	static TSharedRef<IWebInterfaceBrowserAdapter> Create(const FString& Name, UObject* JSBridge, bool IsPermanent, const FString& ConnectScriptText, const FString& DisconnectScriptText);
}; 
