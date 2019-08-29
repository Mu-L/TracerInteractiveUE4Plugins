// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericApplication.h"
#include "HTML5/HTML5Window.h"

THIRD_PARTY_INCLUDES_START
#include <emscripten/html5.h>
THIRD_PARTY_INCLUDES_END

/**
 * HTML5-specific application implementation.
 */
class FHTML5Application : public GenericApplication
{
public:

	static FHTML5Application* CreateHTML5Application();

	virtual ~FHTML5Application() {}

	void SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler );

	virtual void PollGameDeviceState( const float TimeDelta ) override;

	virtual FPlatformRect GetWorkArea( const FPlatformRect& CurrentWindow ) const override;

	TSharedRef< FGenericWindow > MakeWindow();

	EM_BOOL OnKeyEvent(int eventType, const EmscriptenKeyboardEvent *keyEvent);
	EM_BOOL OnMouseEvent(int eventType, const EmscriptenMouseEvent *mouseEvent);
	EM_BOOL OnWheelEvent(int eventType, const EmscriptenWheelEvent *wheelEvent);
	EM_BOOL OnFocusEvent(int eventType, const EmscriptenFocusEvent *focusEvent);
	EM_BOOL OnPointerLockChangeEvent(int eventType, const EmscriptenPointerlockChangeEvent *focusEvent);

private:

	FHTML5Application();

	TSharedPtr< class FHTML5InputInterface > InputInterface;
	TSharedRef< class FGenericWindow > ApplicationWindow;

	int32 WarmUpTicks;

	int32 WindowWidth;
	int32 WindowHeight;
};
