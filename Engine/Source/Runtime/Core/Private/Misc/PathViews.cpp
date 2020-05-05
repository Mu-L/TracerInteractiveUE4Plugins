// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PathViews.h"

#include "Algo/FindLast.h"
#include "Containers/StringView.h"
#include "Misc/StringBuilder.h"

namespace UE4PathViews_Private
{
	static bool IsSlashOrBackslash(TCHAR C) { return C == TEXT('/') || C == TEXT('\\'); }
	static bool IsNotSlashOrBackslash(TCHAR C) { return C != TEXT('/') && C != TEXT('\\'); }
}

FStringView FPathViews::GetCleanFilename(const FStringView& InPath)
{
	if (const TCHAR* StartPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsSlashOrBackslash))
	{
		return InPath.RightChop(UE_PTRDIFF_TO_INT32(StartPos - InPath.GetData() + 1));
	}
	return InPath;
}

FStringView FPathViews::GetBaseFilename(const FStringView& InPath)
{
	const FStringView CleanPath = GetCleanFilename(InPath);
	return CleanPath.LeftChop(GetExtension(CleanPath, /*bIncludeDot*/ true).Len());
}

FStringView FPathViews::GetBaseFilenameWithPath(const FStringView& InPath)
{
	return InPath.LeftChop(GetExtension(InPath, /*bIncludeDot*/ true).Len());
}

FStringView FPathViews::GetBaseFilename(const FStringView& InPath, bool bRemovePath)
{
	return bRemovePath ? GetBaseFilename(InPath) : GetBaseFilenameWithPath(InPath);
}

FStringView FPathViews::GetPath(const FStringView& InPath)
{
	if (const TCHAR* EndPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsSlashOrBackslash))
	{
		return InPath.Left((FStringView::SizeType)(EndPos - InPath.GetData()));
	}
	return FStringView();
}

FStringView FPathViews::GetExtension(const FStringView& InPath, bool bIncludeDot)
{
	if (const TCHAR* Dot = Algo::FindLast(GetCleanFilename(InPath), TEXT('.')))
	{
		const TCHAR* Extension = bIncludeDot ? Dot : Dot + 1;
		return FStringView(Extension, (FStringView::SizeType)(InPath.GetData() + InPath.Len() - Extension));
	}
	return FStringView();
}

FStringView FPathViews::GetPathLeaf(const FStringView& InPath)
{
	if (const TCHAR* EndPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsNotSlashOrBackslash))
	{
		++EndPos;
		return GetCleanFilename(InPath.Left((FStringView::SizeType)(EndPos - InPath.GetData())));
	}
	return FStringView();
}

void FPathViews::Split(const FStringView& InPath, FStringView& OutPath, FStringView& OutName, FStringView& OutExt)
{
	const FStringView CleanName = GetCleanFilename(InPath);
	const TCHAR* DotPos = Algo::FindLast(CleanName, TEXT('.'));
	const FStringView::SizeType NameLen = DotPos ? (FStringView::SizeType)(DotPos - CleanName.GetData()) : CleanName.Len();
	OutPath = InPath.LeftChop(CleanName.Len() + 1);
	OutName = CleanName.Left(NameLen);
	OutExt = CleanName.RightChop(NameLen + 1);
}

void FPathViews::Append(FStringBuilderBase& Builder, const FStringView& Suffix)
{
	if (Builder.Len() > 0 && !UE4PathViews_Private::IsSlashOrBackslash(Builder.LastChar()))
	{
		Builder.Append(TEXT('/'));
	}
	Builder.Append(Suffix);
}
