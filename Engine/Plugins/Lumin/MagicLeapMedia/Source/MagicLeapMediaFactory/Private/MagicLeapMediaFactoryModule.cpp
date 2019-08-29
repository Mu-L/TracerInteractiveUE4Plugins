// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IMediaModule.h"
#include "IMagicLeapMediaModule.h"
#include "MagicLeapMediaFactoryPrivate.h"
#include "IMediaPlayerFactory.h"

DEFINE_LOG_CATEGORY(LogMagicLeapMediaFactory);

#define LOCTEXT_NAMESPACE "FMagicLeapMediaFactoryModule"

/**
 * Implements the MagicLeapMediaFactory module.
 */
class FMagicLeapMediaFactoryModule : public IMediaPlayerFactory, public IModuleInterface
{
public:

	/** IMediaPlayerFactory interface */
	virtual bool CanPlayUrl(const FString& Url, const IMediaOptions* /*Options*/, TArray<FText>* /*OutWarnings*/, TArray<FText>* OutErrors) const override
	{
		FString Scheme;
		FString Location;

		// check scheme
		if (!Url.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(LOCTEXT("NoSchemeFound", "No URI scheme found"));
			}

			return false;
		}

		if (!SupportedUriSchemes.Contains(Scheme))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(FText::Format(LOCTEXT("SchemeNotSupported", "The URI scheme '{0}' is not supported"), FText::FromString(Scheme)));
			}

			return false;
		}

		// check file extension
		if (Scheme == TEXT("file"))
		{
			const FString Extension = FPaths::GetExtension(Location, false);

			if (!SupportedFileExtensions.Contains(Extension))
			{
				if (OutErrors != nullptr)
				{
					OutErrors->Add(FText::Format(LOCTEXT("ExtensionNotSupported", "The file extension '{0}' is not supported"), FText::FromString(Extension)));
				}

				return false;
			}
		}

		return true;
	}

	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		auto MagicLeapMediaModule = FModuleManager::LoadModulePtr<IMagicLeapMediaModule>("MagicLeapMedia");
		return (MagicLeapMediaModule != nullptr) ? MagicLeapMediaModule->CreatePlayer(EventSink) : nullptr;
	}

	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("MediaPlayerDisplayName", "MagicLeap Media");
	}

	virtual FName GetPlayerName() const override
	{
		static FName PlayerName(TEXT("MagicLeapMedia"));
		return PlayerName;
	}

	virtual const TArray<FString>& GetSupportedPlatforms() const override
	{
		return SupportedPlatforms;
	}

	virtual bool SupportsFeature(EMediaFeature Feature) const
	{
		return ((Feature == EMediaFeature::AudioTracks) ||
			(Feature == EMediaFeature::VideoSamples) ||
			(Feature == EMediaFeature::VideoTracks));
	}

public:

	/** IModuleInterface interface */
	
	virtual void StartupModule() override
	{
		// supported file extensions
		SupportedFileExtensions.Add(TEXT("mp4"));
		SupportedFileExtensions.Add(TEXT("3gpp"));
		SupportedFileExtensions.Add(TEXT("aac"));
		SupportedFileExtensions.Add(TEXT("m3u8"));

    	// supported platforms
    	SupportedPlatforms.Add(TEXT("Lumin"));

		// supported schemes
		SupportedUriSchemes.Add(TEXT("file"));
		SupportedUriSchemes.Add(TEXT("http"));
		SupportedUriSchemes.Add(TEXT("https"));
		SupportedUriSchemes.Add(TEXT("rtsp"));
		// Not supporting streaming right now.
		// SupportedUriSchemes.Add(TEXT("httpd"));
		// SupportedUriSchemes.Add(TEXT("mms"));
		// SupportedUriSchemes.Add(TEXT("rtspt"));
		// SupportedUriSchemes.Add(TEXT("rtspu"));

		// register media player info
		auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->RegisterPlayerFactory(*this);
		}
	}

	virtual void ShutdownModule() override
	{
		// unregister player factory
		auto MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");
		
		if (MediaModule != nullptr)
		{
			MediaModule->UnregisterPlayerFactory(*this);
		}
	}

private:

	/** List of supported media file types. */
	TArray<FString> SupportedFileExtensions;
	
	/** List of platforms that the media player support. */
	TArray<FString> SupportedPlatforms;
	
	/** List of supported URI schemes. */
	TArray<FString> SupportedUriSchemes;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMagicLeapMediaFactoryModule, MagicLeapMediaFactory);
