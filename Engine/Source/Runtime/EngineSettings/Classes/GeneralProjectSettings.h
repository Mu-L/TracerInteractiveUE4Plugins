// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "GeneralProjectSettings.generated.h"


UCLASS(config=Game, defaultconfig)
class ENGINESETTINGS_API UGeneralProjectSettings
	: public UObject
{
	GENERATED_UCLASS_BODY()

	/** The name of the company (author, provider) that created the project. */
	UPROPERTY(config, EditAnywhere, Category=Publisher)
	FString CompanyName;

	/** The distinguished name of the company (author, provider) that created the project. */
	UPROPERTY(config, EditAnywhere, Category=Publisher)
	FString CompanyDistinguishedName;

	/** The project's copyright and/or trademark notices. */
	UPROPERTY(config, EditAnywhere, Category=Legal)
	FString CopyrightNotice;

	/** The project's description text. */
	UPROPERTY(config, EditAnywhere, Category=About)
	FString Description;

	/** The project's homepage URL. */
	UPROPERTY(config, EditAnywhere, Category=Publisher)
	FString Homepage;

	/** The project's licensing terms. */
	UPROPERTY(config, EditAnywhere, Category=Legal)
	FString LicensingTerms;

	/** The project's privacy policy. */
	UPROPERTY(config, EditAnywhere, Category=Legal)
	FString PrivacyPolicy;

	/** The project's unique identifier. */
	UPROPERTY(config, EditAnywhere, Category=About)
	FGuid ProjectID;

	/** The project's name. */
	UPROPERTY(config, EditAnywhere, Category=About)
	FString ProjectName;

	/** The project's version number. */
	UPROPERTY(config, EditAnywhere, Category=About)
	FString ProjectVersion;

	/** The project's support contact information. */
	UPROPERTY(config, EditAnywhere, Category=Publisher)
	FString SupportContact;

	/** The project's title as displayed on the window title bar (can include the tokens {GameName}, {PlatformArchitecture}, {BuildConfiguration} or {RHIName}, which will be replaced with the specified text) */
	UPROPERTY(config, EditAnywhere, Category=Displayed)
	FText ProjectDisplayedTitle;

	/** Additional data to be displayed on the window title bar in non-shipping configurations (can include the tokens {GameName}, {PlatformArchitecture}, {BuildConfiguration} or {RHIName}, which will be replaced with the specified text) */
	UPROPERTY(config, EditAnywhere, Category=Displayed)
	FText ProjectDebugTitleInfo;

	/** Should the game's window preserve its aspect ratio when resized by user. */
	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bShouldWindowPreserveAspectRatio;

	/** Should the game use a borderless Slate window instead of a window with system title bar and border */
	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bUseBorderlessWindow;

	/** Should the game attempt to start in VR, regardless of whether -vr was set on the commandline */
	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bStartInVR;
	
	/** This field is no longer used; @see FARSupportInterface ::StartARSession(); @see UARBlueprintLibrary::StartARSession() */
	UPROPERTY()
	bool bStartInAR_DEPRECATED;

    /** No longer used; AR support is determined by included plugins */
	UPROPERTY()
	bool bSupportAR_DEPRECATED;
	
	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bAllowWindowResize;

	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bAllowClose;

	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bAllowMaximize;

	UPROPERTY(config, EditAnywhere, Category = Settings)
	bool bAllowMinimize;
};
