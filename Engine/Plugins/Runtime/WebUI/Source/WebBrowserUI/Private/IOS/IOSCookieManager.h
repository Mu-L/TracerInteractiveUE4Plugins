// Engine/Source/Runtime/WebBrowser/Private/IOS/IOSCookieManager.h

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_IOS
#include "IWebInterfaceBrowserCookieManager.h"

/**
 * Implementation of interface for dealing with a Web Browser cookies for iOS.
 */
class FIOSCookieManager
	: public IWebInterfaceBrowserCookieManager
	, public TSharedFromThis<FIOSCookieManager>
{
public:

	// IWebBrowserCookieManager interface

	virtual void SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed = nullptr) override;
	virtual void DeleteCookies(const FString& URL = TEXT(""), const FString& CookieName = TEXT(""), TFunction<void(int)> Completed = nullptr) override;

	// FIOSCookieManager

	FIOSCookieManager();
	virtual ~FIOSCookieManager();
};
#endif
