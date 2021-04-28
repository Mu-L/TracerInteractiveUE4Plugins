// Engine/Source/Runtime/WebBrowser/Private/Android/AndroidCookieManager.h

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID
#include "IWebInterfaceBrowserCookieManager.h"

/**
 * Implementation of interface for dealing with a Web Browser cookies for iOS.
 */
class FAndroidCookieManager
	: public IWebInterfaceBrowserCookieManager
	, public TSharedFromThis<FAndroidCookieManager>
{
public:

	// IWebBrowserCookieManager interface

	virtual void SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed = nullptr) override;
	virtual void DeleteCookies(const FString& URL = TEXT(""), const FString& CookieName = TEXT(""), TFunction<void(int)> Completed = nullptr) override;

	// FAndroidCookieManager

	FAndroidCookieManager();
	virtual ~FAndroidCookieManager();
};
#endif
