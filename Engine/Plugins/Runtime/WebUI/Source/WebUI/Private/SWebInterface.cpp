// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "SWebInterface.h"
#if !UE_SERVER
#include "SWebBrowser.h"
#include "WebBrowserModule.h"
#include "IWebBrowserPopupFeatures.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserWindow.h"
#include "RenderUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Widgets/Layout/SBorder.h"

SWebInterface::SWebInterface()
{
	bMouseTransparency      = false;
	TransparencyThreadshold = 0.333f;

	LastMousePixel = FLinearColor::White;
	LastMouseTime  = 0.0f;
}

SWebInterface::~SWebInterface()
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	for ( TPair<TWeakPtr<IWebBrowserWindow>, TWeakPtr<SWindow>> Temp : BrowserWindowWidgets )
	{
		if ( Temp.Key.IsValid() )
		{
			TSharedPtr<IWebBrowserWindow> WebBrowserWindow = Temp.Key.Pin();
			if ( WebBrowserWindow.IsValid() )
				WebBrowserWindow->CloseBrowser( false );
		}

		if ( Temp.Value.IsValid() )
		{
			TSharedPtr<SWindow> Window = Temp.Value.Pin();
			if ( Window.IsValid() )
				Window->RequestDestroyWindow();
		}
	}
#endif
}

void SWebInterface::Construct( const FArguments& InArgs )
{
	OnLoadCompleted     = InArgs._OnLoadCompleted;
	OnLoadError         = InArgs._OnLoadError;
	OnLoadStarted       = InArgs._OnLoadStarted;
	OnTitleChanged      = InArgs._OnTitleChanged;
	OnUrlChanged        = InArgs._OnUrlChanged;
	OnBeforeNavigation  = InArgs._OnBeforeNavigation;
	OnLoadUrl           = InArgs._OnLoadUrl;
	OnShowDialog        = InArgs._OnShowDialog;
	OnDismissAllDialogs = InArgs._OnDismissAllDialogs;
	OnBeforePopup       = InArgs._OnBeforePopup;
	OnCreateWindow      = InArgs._OnCreateWindow;
	OnCloseWindow       = InArgs._OnCloseWindow;

	bMouseTransparency      = InArgs._EnableMouseTransparency;
	TransparencyDelay       = FMath::Max( 0.0f, InArgs._MouseTransparencyDelay );
	TransparencyThreadshold = FMath::Clamp( InArgs._MouseTransparencyThreshold, 0.0f, 1.0f );
	
	FCreateBrowserWindowSettings Settings;
	Settings.BrowserFrameRate  = FMath::Clamp( InArgs._FrameRate, 1, 60 );
	Settings.bUseTransparency  = true;
	Settings.BackgroundColor   = InArgs._BackgroundColor;
	Settings.InitialURL        = InArgs._InitialURL;
	Settings.ContentsToLoad    = InArgs._ContentsToLoad;
	Settings.bShowErrorMessage = UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG;
	Settings.bThumbMouseButtonNavigation = false;

	IWebBrowserSingleton* Singleton = IWebBrowserModule::Get().GetSingleton();
	if ( Singleton )
	{
		Singleton->SetDevToolsShortcutEnabled( Settings.bShowErrorMessage );
		BrowserWindow = Singleton->CreateBrowserWindow( Settings );
	}

	ChildSlot
	[
		SAssignNew( BrowserView, SWebBrowserView, BrowserWindow )
		.ParentWindow( InArgs._ParentWindow )
		.InitialURL( InArgs._InitialURL )
		.ContentsToLoad( InArgs._ContentsToLoad )
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
		.ShowErrorMessage( true )
#else
		.ShowErrorMessage( false )
#endif
		.SupportsTransparency( true )
		.SupportsThumbMouseButtonNavigation( false )
		.BackgroundColor( InArgs._BackgroundColor )
		.PopupMenuMethod( InArgs._PopupMenuMethod )
		.ViewportSize( InArgs._ViewportSize )
		.OnLoadCompleted( OnLoadCompleted )
		.OnLoadError( OnLoadError )
		.OnLoadStarted( OnLoadStarted )
		.OnTitleChanged( OnTitleChanged )
		.OnUrlChanged( OnUrlChanged )
		.OnBeforePopup( this, &SWebInterface::HandleBeforePopup )
		.OnCreateWindow( this, &SWebInterface::HandleCreateWindow )
		.OnCloseWindow( this, &SWebInterface::HandleCloseWindow )
		.OnBeforeNavigation( OnBeforeNavigation )
		.OnLoadUrl( OnLoadUrl )
		.OnShowDialog( OnShowDialog )
		.OnDismissAllDialogs( OnDismissAllDialogs )
		.Visibility( this, &SWebInterface::GetViewportVisibility )
		.OnSuppressContextMenu( this, &SWebInterface::HandleSuppressContextMenu )
	];
}

void SWebInterface::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SWidget::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );
	
	if ( bMouseTransparency && FSlateApplication::IsInitialized() )
	{
		LastMousePixel = FLinearColor::Transparent;
		LastMouseTime  = LastMouseTime + InDeltaTime;

		TSharedPtr<ICursor> Mouse = FSlateApplication::Get().GetPlatformCursor();
		if ( Mouse.IsValid() && Mouse->GetType() != EMouseCursor::None )
		{
			FVector2D MousePosition = Mouse->GetPosition();
			if ( !MousePosition.ContainsNaN() )
			{
				FVector2D LocalMouse = AllottedGeometry.AbsoluteToLocal( MousePosition );
				FVector2D LocalSize  = AllottedGeometry.GetLocalSize();

				FVector2D LocalUV = LocalSize.X > 0.0f && LocalSize.Y > 0.0f ?
									FVector2D( LocalMouse.X / LocalSize.X, LocalMouse.Y / LocalSize.Y ) :
									FVector2D();

				if ( LocalUV.X >= 0.0f && LocalUV.X <= 1.0f && LocalUV.Y >= 0.0f && LocalUV.Y <= 1.0f )
				{
					int32 X = FMath::FloorToInt( LocalUV.X * GetTextureWidth() );
					int32 Y = FMath::FloorToInt( LocalUV.Y * GetTextureHeight() );
	
					FLinearColor Pixel = ReadTexturePixel( X, Y );
					if ( ( Pixel.A <  TransparencyThreadshold && LastMousePixel.A >= TransparencyThreadshold )
					  || ( Pixel.A >= TransparencyThreadshold && LastMousePixel.A <  TransparencyThreadshold ) )
						LastMouseTime = 0.0f;

					LastMousePixel = Pixel;
				}
				else
					LastMousePixel = FLinearColor::White;
			}
		}
	}
	else
		LastMousePixel = FLinearColor::White;
}

EVisibility SWebInterface::GetViewportVisibility() const
{
	if ( !BrowserView.IsValid() || !BrowserView->IsInitialized() )
		return EVisibility::Hidden;

	if ( bMouseTransparency && LastMousePixel.A < TransparencyThreadshold && LastMouseTime >= TransparencyDelay )
		return EVisibility::HitTestInvisible;

	return EVisibility::Visible;
}

bool SWebInterface::HandleBeforePopup( FString URL, FString Frame )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( URL.StartsWith( "chrome-devtools://" ) )
		return false;
#endif

	if ( OnBeforePopup.IsBound() )
		OnBeforePopup.Execute( URL, Frame );

	return true;
}

bool SWebInterface::HandleSuppressContextMenu()
{
	return true;
}

bool SWebInterface::HandleCreateWindow( const TWeakPtr<IWebBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebBrowserPopupFeatures>& PopupFeatures )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( !PopupFeatures.IsValid() )
		return false;

	TSharedPtr<IWebBrowserPopupFeatures> PopupFeaturesSP = PopupFeatures.Pin();
	if ( !PopupFeaturesSP.IsValid() )
		return false;
	
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow( SharedThis( this ) );
	if ( !ParentWindow.IsValid() )
		return false;

	const int PosX = PopupFeaturesSP->IsXSet() ? PopupFeaturesSP->GetX() : 100;
	const int PosY = PopupFeaturesSP->IsYSet() ? PopupFeaturesSP->GetY() : 100;
	const FVector2D BrowserWindowPosition( PosX, PosY );

	const int Width  = PopupFeaturesSP->IsWidthSet()  ? PopupFeaturesSP->GetWidth()  : 800;
	const int Height = PopupFeaturesSP->IsHeightSet() ? PopupFeaturesSP->GetHeight() : 600;
	const FVector2D BrowserWindowSize( Width, Height );

	const ESizingRule SizingRule = PopupFeaturesSP->IsResizable() ?
								   ESizingRule::UserSized :
								   ESizingRule::FixedSize;

	TSharedPtr<IWebBrowserWindow> NewBrowserWindowSP = NewBrowserWindow.Pin();
	if ( !NewBrowserWindowSP.IsValid() )
		return false;

	TSharedRef<SWindow> NewWindow =
		SNew( SWindow )
		.Title( FText::GetEmpty() )
		.ClientSize( BrowserWindowSize )
		.ScreenPosition( BrowserWindowPosition )
		.AutoCenter( EAutoCenter::None )
		.SizingRule( SizingRule )
		.SupportsMaximize( SizingRule != ESizingRule::FixedSize )
		.SupportsMinimize( SizingRule != ESizingRule::FixedSize )
		.HasCloseButton( true )
		.CreateTitleBar( true )
		.IsInitiallyMaximized( PopupFeaturesSP->IsFullscreen() )
		.LayoutBorder( FMargin( 0 ) );

	TSharedPtr<SWebBrowser> WebBrowser;
	NewWindow->SetContent(
		SNew( SBorder )
		.VAlign( VAlign_Fill )
		.HAlign( HAlign_Fill )
		.Padding( 0 )
		[
			SAssignNew( WebBrowser, SWebBrowser, NewBrowserWindowSP )
				.ShowControls( false )
				.ShowAddressBar( false )
				.OnCreateWindow( this, &SWebInterface::HandleCreateWindow )
				.OnCloseWindow( this, &SWebInterface::HandleCloseWindow )
		] );
	
	{
		struct FLocal
		{
			static void RequestDestroyWindowOverride( const TSharedRef<SWindow>& Window, TWeakPtr<IWebBrowserWindow> BrowserWindowPtr )
			{
				TSharedPtr<IWebBrowserWindow> BrowserWindow = BrowserWindowPtr.Pin();
				if ( BrowserWindow.IsValid() )
				{
					if ( BrowserWindow->IsClosing() )
						FSlateApplicationBase::Get().RequestDestroyWindow( Window );
					else
						BrowserWindow->CloseBrowser( false );
				}
			}
		};

		NewWindow->SetRequestDestroyWindowOverride( FRequestDestroyWindowOverride::CreateStatic( &FLocal::RequestDestroyWindowOverride, TWeakPtr<IWebBrowserWindow>( NewBrowserWindow ) ) );
	}

	FSlateApplication::Get().AddWindow( NewWindow );
	NewWindow->BringToFront();
	FSlateApplication::Get().SetKeyboardFocus( WebBrowser, EFocusCause::SetDirectly );

	BrowserWindowWidgets.Add( NewBrowserWindow, NewWindow );
	return true;
#else
	return false;
#endif
}

bool SWebInterface::HandleCloseWindow( const TWeakPtr<IWebBrowserWindow>& BrowserWindowPtr )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( !BrowserWindowPtr.IsValid() )
		return false;

	TSharedPtr<IWebBrowserWindow> WebBrowserWindow = BrowserWindowPtr.Pin();
	if ( !WebBrowserWindow.IsValid() )
		return false;

	if ( WebBrowserWindow->IsClosing() )
	{
		const TWeakPtr<SWindow>* FoundWebBrowserWindow = BrowserWindowWidgets.Find( WebBrowserWindow );
		if ( FoundWebBrowserWindow != nullptr )
		{
			TSharedPtr<SWindow> FoundWindow = FoundWebBrowserWindow->Pin();
			if ( FoundWindow.IsValid() )
				FoundWindow->RequestDestroyWindow();

			BrowserWindowWidgets.Remove( WebBrowserWindow );
			return true;
		}
	}
	else
		WebBrowserWindow->CloseBrowser( false );

	return false;
#else
	return false;
#endif
}

int32 SWebInterface::GetTextureWidth() const
{
	if ( !BrowserWindow.IsValid() )
		return 0;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource )
		return 0;

	return Resource->GetWidth();
}

int32 SWebInterface::GetTextureHeight() const
{
	if ( !BrowserWindow.IsValid() )
		return 0;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource )
		return 0;

	return Resource->GetHeight();
}

FColor SWebInterface::ReadTexturePixel( int32 X, int32 Y ) const
{
	if ( X < 0 || X >= GetTextureWidth() )
		return FColor::Transparent;
	if ( Y < 0 || Y >= GetTextureHeight() )
		return FColor::Transparent;

	TArray<FColor> Pixels = ReadTexturePixels( X, Y, 1, 1 );
	if ( Pixels.Num() > 0 )
		return Pixels[ 0 ];

	return FColor::Transparent;
}

TArray<FColor> SWebInterface::ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height ) const
{
	TArray<FColor> OutPixels;
	if ( !BrowserWindow.IsValid() )
		return OutPixels;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource || Resource->GetType() != ESlateShaderResource::NativeTexture )
		return OutPixels;
	
	FTexture2DRHIRef TextureRHI;
	TextureRHI = ( ( TSlateTexture<FTexture2DRHIRef>* )Resource )->GetTypedResource();

	struct FReadSurfaceContext
	{
		FTexture2DRHIRef Texture;
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	int32 ResourceWidth  = (int32)Resource->GetWidth();
	int32 ResourceHeight = (int32)Resource->GetHeight();

	X = FMath::Clamp( X, 0, ResourceWidth  - 1 );
	Y = FMath::Clamp( Y, 0, ResourceHeight - 1 );

	Width  = FMath::Clamp( Width, 1, ResourceWidth );
	Width  = Width - FMath::Max( X + Width - ResourceWidth, 0 );

	Height = FMath::Clamp( Height, 1, ResourceHeight );
	Height = Height - FMath::Max( Y + Height - ResourceHeight, 0 );

	FReadSurfaceContext Context =
	{
		TextureRHI,
		&OutPixels,
		FIntRect( X, Y, X + Width, Y + Height ),
		FReadSurfaceDataFlags()
	};

	ENQUEUE_RENDER_COMMAND( ReadSurfaceCommand )(
		[ Context ]( FRHICommandListImmediate& RHICmdList )
		{
			RHICmdList.ReadSurfaceData(
				Context.Texture,
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		} );
	FlushRenderingCommands();
	
	return OutPixels;
}

void SWebInterface::LoadURL( FString NewURL )
{
	if ( BrowserView.IsValid() )
		BrowserView->LoadURL( NewURL );
}

void SWebInterface::LoadString( FString Contents, FString DummyURL )
{
	if ( BrowserView.IsValid() )
		BrowserView->LoadString( Contents, DummyURL );
}

void SWebInterface::Reload()
{
	if ( BrowserView.IsValid() )
		BrowserView->Reload();
}

void SWebInterface::StopLoad()
{
	if ( BrowserView.IsValid() )
		BrowserView->StopLoad();
}

FString SWebInterface::GetUrl() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->GetUrl();

	return FString();
}

bool SWebInterface::IsLoaded() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->IsLoaded();

	return false;
}

bool SWebInterface::IsLoading() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->IsLoading();

	return false;
}

void SWebInterface::ExecuteJavascript( const FString& ScriptText )
{
	if ( BrowserView.IsValid() )
		BrowserView->ExecuteJavascript( ScriptText );
}

void SWebInterface::BindUObject( const FString& Name, UObject* Object, bool bIsPermanent )
{
	if ( BrowserView.IsValid() )
		BrowserView->BindUObject( Name, Object, bIsPermanent );
}

void SWebInterface::UnbindUObject( const FString& Name, UObject* Object, bool bIsPermanent )
{
	if ( BrowserView.IsValid() )
		BrowserView->UnbindUObject( Name, Object, bIsPermanent );
}
#endif
