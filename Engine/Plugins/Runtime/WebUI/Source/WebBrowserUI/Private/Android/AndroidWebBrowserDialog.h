// Engine/Source/Runtime/WebBrowser/Private/Android/AndroidWebBrowserDialog.h

#pragma once

#include "CoreMinimal.h"

#if USE_ANDROID_JNI

#include "IWebInterfaceBrowserDialog.h"

#include <jni.h>

class SAndroidWebBrowserWidget;

class FAndroidWebBrowserDialog
	: public IWebInterfaceBrowserDialog
{
public:
	virtual ~FAndroidWebBrowserDialog()
	{}

	// IWebBrowserDialog interface:

	virtual EWebInterfaceBrowserDialogType GetType() override
	{
		return Type;
	}

	virtual const FText& GetMessageText() override
	{
		return MessageText;
	}

	virtual const FText& GetDefaultPrompt() override
	{
		return DefaultPrompt;
	}

	virtual bool IsReload() override
	{
		check(Type == EWebInterfaceBrowserDialogType::Unload);
		return false; // The android webkit browser does not provide this infomation
	}

	virtual void Continue(bool Success = true, const FText& UserResponse = FText::GetEmpty()) override;

private:

	EWebInterfaceBrowserDialogType Type;
	FText MessageText;
	FText DefaultPrompt;

	jobject Callback; // Either a reference to a JsResult or a JsPromptResult object depending on Type

	// Create a dialog from OnJSPrompt arguments
	FAndroidWebBrowserDialog(jstring InMessageText, jstring InDefaultPrompt, jobject InCallback);

	// Create a dialog from OnJSAlert|Confirm|BeforeUnload arguments
	FAndroidWebBrowserDialog(EWebInterfaceBrowserDialogType InDialogType, jstring InMessageText, jobject InCallback);

	friend class FAndroidWebBrowserWindow;
	friend class SAndroidWebBrowserWidget;
};

typedef FAndroidWebBrowserDialog FWebInterfaceBrowserDialog;

#endif // USE_ANDROID_JNI