// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformHttp.h"
#include "GenericPlatform/HttpRequestImpl.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"

/**
 * A generic http request
 */
class FGenericPlatformHttpRequest : public FHttpRequestImpl
{
public:
	// IHttpBase
	virtual FString GetURL() const override { return TEXT(""); }
	virtual FString GetURLParameter(const FString& ParameterName) const override { return TEXT(""); }
	virtual FString GetHeader(const FString& HeaderName) const override { return TEXT(""); }
	virtual TArray<FString> GetAllHeaders() const override { return TArray<FString>(); }
	virtual FString GetContentType() const override { return TEXT(""); }
	virtual int32 GetContentLength() const override { return 0; }
	virtual const TArray<uint8>& GetContent() const override { static TArray<uint8> Temp; return Temp; }

	// IHttpRequest
	virtual FString GetVerb() const override { return TEXT(""); }
	virtual void SetVerb(const FString& Verb) override {}
	virtual void SetURL(const FString& URL) override {}
	virtual void SetContent(const TArray<uint8>& ContentPayload) override {}
	virtual void SetContentAsString(const FString& ContentString) override {}
	virtual bool SetContentAsStreamedFile(const FString& Filename) override { return false; }
	virtual bool SetContentFromStream(TSharedRef<FArchive, ESPMode::ThreadSafe> Stream) override { return false; }
	virtual void SetHeader(const FString& HeaderName, const FString& HeaderValue) override {}
	virtual void AppendToHeader(const FString& HeaderName, const FString& AdditionalHeaderValue) override {}
	virtual bool ProcessRequest() override { return false; }
	virtual void CancelRequest() override {}
	virtual EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::NotStarted; }
	virtual const FHttpResponsePtr GetResponse() const override { return nullptr; }
	virtual void Tick(float DeltaSeconds) override {}
	virtual float GetElapsedTime() const override { return 0.0f; }
};

void FGenericPlatformHttp::Init()
{
}

void FGenericPlatformHttp::Shutdown()
{
}

IHttpRequest* FGenericPlatformHttp::ConstructRequest()
{
	return new FGenericPlatformHttpRequest();
}

bool FGenericPlatformHttp::UsesThreadedHttp()
{
	// Many platforms use libcurl.  Our libcurl implementation uses threading.
	// Platforms that don't use libcurl but have threading will need to override UsesThreadedHttp to return true for their platform.
	return WITH_LIBCURL;
}

static bool IsAllowedChar(UTF8CHAR LookupChar)
{
	static char AllowedChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
	static bool bTableFilled = false;
	static bool AllowedTable[256] = { false };

	if (!bTableFilled)
	{
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(AllowedChars) - 1; ++Idx)	// -1 to avoid trailing 0
		{
			uint8 AllowedCharIdx = static_cast<uint8>(AllowedChars[Idx]);
			check(AllowedCharIdx < UE_ARRAY_COUNT(AllowedTable));
			AllowedTable[AllowedCharIdx] = true;
		}

		bTableFilled = true;
	}

	return AllowedTable[LookupChar];
}

FString FGenericPlatformHttp::UrlEncode(const FString &UnencodedString)
{
	FTCHARToUTF8 Converter(*UnencodedString);	//url encoding must be encoded over each utf-8 byte
	const UTF8CHAR* UTF8Data = (UTF8CHAR*) Converter.Get();	//converter uses ANSI instead of UTF8CHAR - not sure why - but other code seems to just do this cast. In this case it really doesn't matter
	FString EncodedString = TEXT("");
	
	TCHAR Buffer[2] = { 0, 0 };

	for (int32 ByteIdx = 0, Length = Converter.Length(); ByteIdx < Length; ++ByteIdx)
	{
		UTF8CHAR ByteToEncode = UTF8Data[ByteIdx];
		
		if (IsAllowedChar(ByteToEncode))
		{
			Buffer[0] = ByteToEncode;
			FString TmpString = Buffer;
			EncodedString += TmpString;
		}
		else if (ByteToEncode != '\0')
		{
			EncodedString += TEXT("%");
			EncodedString += FString::Printf(TEXT("%.2X"), ByteToEncode);
		}
	}
	return EncodedString;
}

FString FGenericPlatformHttp::UrlDecode(const FString &EncodedString)
{
	FTCHARToUTF8 Converter(*EncodedString);	
	const UTF8CHAR* UTF8Data = (UTF8CHAR*)Converter.Get();	
	
	TArray<ANSICHAR> Data;
	Data.Reserve(EncodedString.Len());

	for (int32 CharIdx = 0; CharIdx < Converter.Length();)
	{
		if (UTF8Data[CharIdx] == '%')
		{
			int32 Value = 0;
			if (UTF8Data[CharIdx + 1] == 'u')
			{
				if (CharIdx + 6 <= Converter.Length())
				{
					// Treat all %uXXXX as code point
					Value = FParse::HexDigit(UTF8Data[CharIdx + 2]) << 12;
					Value += FParse::HexDigit(UTF8Data[CharIdx + 3]) << 8;
					Value += FParse::HexDigit(UTF8Data[CharIdx + 4]) << 4;
					Value += FParse::HexDigit(UTF8Data[CharIdx + 5]);
					CharIdx += 6;

					ANSICHAR Buffer[8] = { 0 };
					ANSICHAR* BufferPtr = Buffer;
					const int32 Len = UE_ARRAY_COUNT(Buffer);
					const int32 WrittenChars = FTCHARToUTF8_Convert::Utf8FromCodepoint(Value, BufferPtr, Len);

					Data.Append(Buffer, WrittenChars);
				}
				else
				{
					// Not enough in the buffer for valid decoding, skip it
					CharIdx++;
					continue;
				}
			}
			else if(CharIdx + 3 <= Converter.Length())
			{
				// Treat all %XX as straight byte
				Value = FParse::HexDigit(UTF8Data[CharIdx + 1]) << 4;
				Value += FParse::HexDigit(UTF8Data[CharIdx + 2]);
				CharIdx += 3;
				Data.Add((ANSICHAR)(Value));
			}
			else
			{
				// Not enough in the buffer for valid decoding, skip it
				CharIdx++;
				continue;
			}
		}
		else
		{
			// Non escaped characters
			Data.Add(UTF8Data[CharIdx]);
			CharIdx++;
		}
	}

	Data.Add('\0');
	return FString(UTF8_TO_TCHAR(Data.GetData()));
}

FString FGenericPlatformHttp::HtmlEncode(const FString& UnencodedString)
{
	FString EncodedString;
	EncodedString.Reserve(UnencodedString.Len());

	for ( int32 Index = 0; Index != UnencodedString.Len(); ++Index )
	{
		switch ( UnencodedString[Index] )
		{
			case '&':  EncodedString.Append(TEXT("&amp;"));					break;
			case '\"': EncodedString.Append(TEXT("&quot;"));				break;
			case '\'': EncodedString.Append(TEXT("&apos;"));				break;
			case '<':  EncodedString.Append(TEXT("&lt;"));					break;
			case '>':  EncodedString.Append(TEXT("&gt;"));					break;
			default:   EncodedString.AppendChar(UnencodedString[Index]);	break;
		}
	}

	return EncodedString;
}

FString FGenericPlatformHttp::GetUrlDomain(const FString& Url)
{
	FString Protocol;
	FString Domain = Url;
	// split the http protocol portion from domain
	Url.Split(TEXT("://"), &Protocol, &Domain);
	// strip off everything but the domain portion
	const int32 Idx = Domain.GetCharArray().IndexOfByPredicate([](TCHAR Character) { return Character == TEXT('/') || Character == TEXT('?') || Character == TEXT(':'); });
	if (Idx != INDEX_NONE)
	{
		Domain.LeftInline(Idx, false);
	}
	return Domain;
}

FString FGenericPlatformHttp::GetMimeType(const FString& FilePath)
{
	const FString FileExtension = FPaths::GetExtension(FilePath, true);

	static TMap<FString, FString> ExtensionMimeTypeMap;
	if ( ExtensionMimeTypeMap.Num() == 0 )
	{
		// Web
		ExtensionMimeTypeMap.Add(TEXT(".html"), TEXT("text/html"));
		ExtensionMimeTypeMap.Add(TEXT(".css"), TEXT("text/css"));
		ExtensionMimeTypeMap.Add(TEXT(".js"), TEXT("application/x-javascript"));

		// Video
		ExtensionMimeTypeMap.Add(TEXT(".avi"), TEXT("video/msvideo, video/avi, video/x-msvideo"));
		ExtensionMimeTypeMap.Add(TEXT(".mpeg"), TEXT("video/mpeg"));

		// Image
		ExtensionMimeTypeMap.Add(TEXT(".bmp"), TEXT("image/bmp"));
		ExtensionMimeTypeMap.Add(TEXT(".gif"), TEXT("image/gif"));
		ExtensionMimeTypeMap.Add(TEXT(".jpg"), TEXT("image/jpeg"));
		ExtensionMimeTypeMap.Add(TEXT(".jpeg"), TEXT("image/jpeg"));
		ExtensionMimeTypeMap.Add(TEXT(".png"), TEXT("image/png"));
		ExtensionMimeTypeMap.Add(TEXT(".svg"), TEXT("image/svg+xml"));
		ExtensionMimeTypeMap.Add(TEXT(".tiff"), TEXT("image/tiff"));

		// Audio
		ExtensionMimeTypeMap.Add(TEXT(".midi"), TEXT("audio/x-midi"));
		ExtensionMimeTypeMap.Add(TEXT(".mp3"), TEXT("audio/mpeg"));
		ExtensionMimeTypeMap.Add(TEXT(".ogg"), TEXT("audio/vorbis, application/ogg"));
		ExtensionMimeTypeMap.Add(TEXT(".wav"), TEXT("audio/wav, audio/x-wav"));

		// Documents
		ExtensionMimeTypeMap.Add(TEXT(".xml"), TEXT("application/xml"));
		ExtensionMimeTypeMap.Add(TEXT(".txt"), TEXT("text/plain"));
		ExtensionMimeTypeMap.Add(TEXT(".tsv"), TEXT("text/tab-separated-values"));
		ExtensionMimeTypeMap.Add(TEXT(".csv"), TEXT("text/csv"));
		ExtensionMimeTypeMap.Add(TEXT(".json"), TEXT("application/json"));
		
		// Compressed
		ExtensionMimeTypeMap.Add(TEXT(".zip"), TEXT("application/zip, application/x-compressed-zip"));
	}

	if ( FString* FoundMimeType = ExtensionMimeTypeMap.Find(FileExtension) )
	{
		return *FoundMimeType;
	}

	return TEXT("application/unknown");
}

FString FGenericPlatformHttp::GetDefaultUserAgent()
{
	//** strip/escape slashes and whitespace from components
	static FString CachedUserAgent = FString::Printf(TEXT("%s/%s %s/%s"),
		*EscapeUserAgentString(FApp::GetProjectName()),
		*EscapeUserAgentString(FApp::GetBuildVersion()),
		*EscapeUserAgentString(FString(FPlatformProperties::IniPlatformName())),
		*EscapeUserAgentString(FPlatformMisc::GetOSVersion()));
	return CachedUserAgent;
}

FString FGenericPlatformHttp::EscapeUserAgentString(const FString& UnescapedString)
{
	if (UnescapedString.Contains(" ") || UnescapedString.Contains("/"))
	{
		FString EscapedString;
		EscapedString = UnescapedString.Replace(TEXT(" "), TEXT(""));
		EscapedString = UnescapedString.Replace(TEXT("/"), TEXT("+"));
		return EscapedString;
	}
	else
	{
		return UnescapedString;
	}
}

TOptional<FString> FGenericPlatformHttp::GetOperatingSystemProxyAddress()
{
	return TOptional<FString>();
}

bool FGenericPlatformHttp::IsOperatingSystemProxyInformationSupported()
{
	return false;
}
