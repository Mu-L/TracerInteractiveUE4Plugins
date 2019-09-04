// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Templates/SharedPointer.h"

namespace Trace
{

class FAnalysisSessionLock;
class FStringStore;

struct FBookmarkSpec
{
	const TCHAR* File;
	const TCHAR* FormatString;
	int32 Line;
};

struct FBookmarkInternal
{
	double Time;
	const TCHAR* Text;
};

class FBookmarkProvider
	: public IBookmarkProvider
{
public:
	static const FName ProviderName;

	FBookmarkProvider(IAnalysisSession& Session);

	FBookmarkSpec& GetSpec(uint64 BookmarkPoint);
	virtual uint64 GetBookmarkCount() const override { return Bookmarks.Num(); }
	void AppendBookmark(double Time, uint64 BookmarkPoint, const uint8* FormatArgs);
	virtual void EnumerateBookmarks(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FBookmark&)> Callback) const override;

private:
	enum
	{
		FormatBufferSize = 65536
	};

	IAnalysisSession& Session;
	TMap<uint64, TSharedPtr<FBookmarkSpec>> SpecMap;
	TArray<TSharedRef<FBookmarkInternal>> Bookmarks;
	TCHAR FormatBuffer[FormatBufferSize];
};

}