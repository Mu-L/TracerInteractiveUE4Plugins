// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"

#if !UE_SERVER
#include "SWebBrowserView.h"

class SWebBrowserView;
class IWebBrowserDialog;
class IWebBrowserWindow;
struct FWebNavigationRequest;
enum class EWebBrowserDialogEventResponse;

class WEBUI_API SWebInterface : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams( bool, FOnBeforeBrowse, const FString& /*Url*/, const FWebNavigationRequest& /*Request*/ );
	DECLARE_DELEGATE_RetVal_ThreeParams( bool, FOnLoadUrl, const FString& /*Method*/, const FString& /*Url*/, FString& /* Response */ );
	DECLARE_DELEGATE_RetVal_OneParam( EWebBrowserDialogEventResponse, FOnShowDialog, const TWeakPtr<IWebBrowserDialog>& );

	DECLARE_DELEGATE_RetVal( bool, FOnSuppressContextMenu );

	SLATE_BEGIN_ARGS( SWebInterface )
		: _FrameRate( 60 )
		, _InitialURL( TEXT( "http://tracerinteractive.com" ) )
		, _BackgroundColor( 255, 255, 255, 255 )
		, _EnableMouseTransparency( false )
		, _MouseTransparencyDelay( 0.1f )
		, _MouseTransparencyThreshold( 0.333f )
		, _ViewportSize( FVector2D::ZeroVector )
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}
		SLATE_ARGUMENT( TSharedPtr<SWindow>, ParentWindow )
		SLATE_ARGUMENT( int32, FrameRate )
		SLATE_ARGUMENT( FString, InitialURL )
		SLATE_ARGUMENT( TOptional<FString>, ContentsToLoad )
		SLATE_ARGUMENT( FColor, BackgroundColor )
		SLATE_ARGUMENT( bool, EnableMouseTransparency )
		SLATE_ARGUMENT( float, MouseTransparencyDelay )
		SLATE_ARGUMENT( float, MouseTransparencyThreshold )
		SLATE_ARGUMENT( TOptional<EPopupMethod>, PopupMenuMethod )

		SLATE_ATTRIBUTE( FVector2D, ViewportSize );

		SLATE_EVENT( FSimpleDelegate, OnLoadCompleted )
		SLATE_EVENT( FSimpleDelegate, OnLoadError )
		SLATE_EVENT( FSimpleDelegate, OnLoadStarted )
		SLATE_EVENT( FOnTextChanged, OnTitleChanged )
		SLATE_EVENT( FOnTextChanged, OnUrlChanged )
		SLATE_EVENT( FOnBeforePopupDelegate, OnBeforePopup )
		SLATE_EVENT( FOnCreateWindowDelegate, OnCreateWindow )
		SLATE_EVENT( FOnCloseWindowDelegate, OnCloseWindow )
		SLATE_EVENT( FOnBeforeBrowse, OnBeforeNavigation )
		SLATE_EVENT( FOnLoadUrl, OnLoadUrl )
		SLATE_EVENT( FOnShowDialog, OnShowDialog )
		SLATE_EVENT( FSimpleDelegate, OnDismissAllDialogs )
		SLATE_EVENT( FOnSuppressContextMenu, OnSuppressContextMenu );
	SLATE_END_ARGS()

	SWebInterface();
	~SWebInterface();

	void Construct( const FArguments& InArgs );

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:

	FLinearColor LastMousePixel;
	float        LastMouseTime;

	EVisibility GetViewportVisibility() const;

	bool HandleBeforePopup( FString URL, FString Frame );
	bool HandleSuppressContextMenu();

	bool HandleCreateWindow( const TWeakPtr<IWebBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebBrowserPopupFeatures>& PopupFeatures );
	bool HandleCloseWindow( const TWeakPtr<IWebBrowserWindow>& BrowserWindowPtr );

protected:

	TSharedPtr<SWebBrowserView>   BrowserView;
	TSharedPtr<IWebBrowserWindow> BrowserWindow;

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	TMap<TWeakPtr<IWebBrowserWindow>, TWeakPtr<SWindow>> BrowserWindowWidgets;
#endif

	bool  bMouseTransparency;
	float TransparencyDelay;
	float TransparencyThreadshold;


	FSimpleDelegate OnLoadCompleted;
	FSimpleDelegate OnLoadError;
	FSimpleDelegate OnLoadStarted;

	FOnTextChanged OnTitleChanged;
	FOnTextChanged OnUrlChanged;

	FOnBeforePopupDelegate  OnBeforePopup;
	FOnCreateWindowDelegate OnCreateWindow;
	FOnCloseWindowDelegate  OnCloseWindow;

	FOnBeforeBrowse OnBeforeNavigation;
	FOnLoadUrl      OnLoadUrl;

	FOnShowDialog   OnShowDialog;
	FSimpleDelegate OnDismissAllDialogs;

public:

	int32 GetTextureWidth() const;
	int32 GetTextureHeight() const;

	FColor ReadTexturePixel( int32 X, int32 Y ) const;
	TArray<FColor> ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height ) const;
	
	void LoadURL( FString NewURL );
	void LoadString( FString Contents, FString DummyURL );

	void Reload();
	void StopLoad();

	FString GetUrl() const;

	bool IsLoaded() const;
	bool IsLoading() const;

	void ExecuteJavascript( const FString& ScriptText );

	void BindUObject( const FString& Name, UObject* Object, bool bIsPermanent = true );
	void UnbindUObject( const FString& Name, UObject* Object, bool bIsPermanent = true );
};
#endif
