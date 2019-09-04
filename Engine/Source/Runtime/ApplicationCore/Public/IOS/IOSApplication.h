// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericApplication.h"

class FIOSWindow;

class FIOSApplication : public GenericApplication
{
public:

	static FIOSApplication* CreateIOSApplication();


public:

	virtual ~FIOSApplication() {}

	void SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler );
#if WITH_ACCESSIBILITY
	virtual void SetAccessibleMessageHandler(const TSharedRef<FGenericAccessibleMessageHandler>& InAccessibleMessageHandler) override;
#endif

	virtual void PollGameDeviceState( const float TimeDelta ) override;

	virtual FPlatformRect GetWorkArea( const FPlatformRect& CurrentWindow ) const override;

	virtual TSharedRef< FGenericWindow > MakeWindow() override;

	virtual void AddExternalInputDevice(TSharedPtr<class IInputDevice> InputDevice);

#if !PLATFORM_TVOS
	static void OrientationChanged(UIInterfaceOrientation orientation);
#endif

	virtual IInputInterface* GetInputInterface() override { return (IInputInterface*)InputInterface.Get(); }

	virtual bool IsGamepadAttached() const override;

	TSharedRef<FIOSWindow> FindWindowByAppDelegateView();

protected:
	virtual void InitializeWindow( const TSharedRef< FGenericWindow >& Window, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FGenericWindow >& InParent, const bool bShowImmediately ) override;

private:

	FIOSApplication();
#if WITH_ACCESSIBILITY
	void OnAccessibleEventRaised(TSharedRef<IAccessibleWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue);
#endif

private:

	TSharedPtr< class FIOSInputInterface > InputInterface;

	/** List of input devices implemented in external modules. */
	TArray< TSharedPtr<class IInputDevice> > ExternalInputDevices;
	bool bHasLoadedInputPlugins;

	TArray< TSharedRef< FIOSWindow > > Windows;

	static FCriticalSection CriticalSection;
	static bool bOrientationChanged;

	void CacheDisplayMetrics();
};
