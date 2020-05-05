// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IPlatformTextField.h"
#include "Internationalization/Text.h"
#include "IOSView.h"

#import <UIKit/UIKit.h>

class IVirtualKeyboardEntry;

@class SlateTextField;

class FIOSPlatformTextField : public IPlatformTextField
{
public:
	FIOSPlatformTextField();
	virtual ~FIOSPlatformTextField();

	virtual void ShowVirtualKeyboard(bool bShow, int32 UserIndex, TSharedPtr<IVirtualKeyboardEntry> TextEntryWidget) override;
	virtual bool AllowMoveCursor() override { return true; };

private:
	SlateTextField* TextField;
};

typedef FIOSPlatformTextField FPlatformTextField;

#if !PLATFORM_TVOS
@interface SlateTextField : NSObject<UIAlertViewDelegate>
{
	TWeakPtr<IVirtualKeyboardEntry> TextWidget;
	FText TextEntry;
    
#ifdef __IPHONE_8_0
    UIAlertController* AlertController;
#endif
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_9_0
    UIAlertView* AlertView;
#endif
}

-(void)show:(TSharedPtr<IVirtualKeyboardEntry>)InTextWidget text:(NSString*)TextContents placeholder:(NSString*)PlaceholderContents keyboardConfig:(FKeyboardConfig)KeyboardConfig;
-(void)hide;
-(bool)hasTextWidget;

@end
#endif
