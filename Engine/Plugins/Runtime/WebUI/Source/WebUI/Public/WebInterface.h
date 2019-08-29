// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Components/Widget.h"
#include "Engine/EngineBaseTypes.h"
#include "JsonLibrary.h"
#include "WebInterface.generated.h"

class UMaterial;

UENUM(BlueprintType, meta = (DisplayName = "UI Directory"))
enum class EWebInterfaceDirectory : uint8
{
	UI		UMETA(DisplayName="/UI"),
	Content	UMETA(DisplayName="/Content")
};

UCLASS()
class WEBUI_API UWebInterface : public UWidget
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FOnUrlChangedEvent, const FText&, URL );
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FOnInterfaceEvent, const FName, Name, FJsonLibraryValue, Data );

	// Load HTML in the browser.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void LoadHTML( const FString& HTML );
	// Load a URL in the browser.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void LoadURL( const FString& URL );
	// Load a file in the browser.
	UFUNCTION(BlueprintCallable, Category = "Web UI", meta = (AdvancedDisplay = "Directory"))
	void LoadFile( const FString& File, EWebInterfaceDirectory Directory = EWebInterfaceDirectory::UI );
	// Load content into the browser.
	UFUNCTION(BlueprintCallable, Category = "Web UI", meta = (AdvancedDisplay = "bScript"))
	bool LoadContent( const FString& File, bool bScript = false );

	// Get the current URL of the browser.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	FString GetURL() const;

	// Execute JavaScript in the browser context.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void Execute( const FString& Script );
	// Call ue.interface.function(data) in the browser context.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void Call( const FString& Function, const FJsonLibraryValue& Data );
	
	// Bind an object to ue.name in the browser context.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void Bind( const FString& Name, UObject* Object );
	// Unbind an object from ue.name in the browser context.
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void Unbind( const FString& Name, UObject* Object );

	// Set focus to the browser.
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MouseLockMode"), Category = "Web UI|Helpers")
	void Focus( EMouseLockMode MouseLockMode = EMouseLockMode::LockOnCapture );
	// Set focus to the game.
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MouseCaptureMode"), Category = "Web UI|Helpers")
	void Unfocus( EMouseCaptureMode MouseCaptureMode = EMouseCaptureMode::CapturePermanently );
	// Reset cursor to center of the viewport.
	UFUNCTION(BlueprintCallable, Category = "Web UI|Helpers")
	void ResetMousePosition();

	// Get the width of the browser texture.
	UFUNCTION(BlueprintPure, Category = "Web UI|Textures")
	int32 GetTextureWidth() const;
	// Get the height of the browser texture.
	UFUNCTION(BlueprintPure, Category = "Web UI|Textures")
	int32 GetTextureHeight() const;

	// Read a pixel from the browser texture.
	UFUNCTION(BlueprintCallable, Category = "Web UI|Textures")
	FColor ReadTexturePixel( int32 X, int32 Y );
	// Read an area of pixels from the browser texture.
	UFUNCTION(BlueprintCallable, Category = "Web UI|Textures")
	TArray<FColor> ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height );

	// Called when the URL has changed.
	UPROPERTY(BlueprintAssignable, Category = "Web UI|Events")
	FOnUrlChangedEvent OnUrlChangedEvent;
	
	// Called with ue.interface.broadcast(name, data) in the browser context.
	UPROPERTY(BlueprintAssignable, Category = "Web UI|Events")
	FOnInterfaceEvent OnInterfaceEvent;

	virtual void ReleaseSlateResources( bool bReleaseChildren ) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	UMaterial* GetDefaultMaterial() const;

private:

	UPROPERTY()
	class UWebInterfaceObject* MyObject;
	void HandleUrlChanged( const FText& URL );

protected:
	
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (UIMin = 1, UIMax = 60))
	int32 FrameRate;
	UPROPERTY(EditAnywhere, Category = "Behavior")
	FString InitialURL;

	UPROPERTY(EditAnywhere, meta = (DisplayName = "Enable Transparency"), Category = "Behavior|Mouse")
	bool bEnableMouseTransparency;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Transparency Threshold", UIMin = 0, UIMax = 1), Category = "Behavior|Mouse")
	float MouseTransparencyThreshold;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Transparency Delay", UIMin = 0, UIMax = 1), Category = "Behavior|Mouse")
	float MouseTransparencyDelay;
	
#if !UE_SERVER
	TSharedPtr<class SWebInterface> WebInterfaceWidget;
#endif
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:

	UMaterial* DefaultMaterial;
};
