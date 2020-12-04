// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Crc.h"
#include "Misc/CString.h"
#include "Templates/AndOrNot.h"
#include "Templates/Decay.h"
#include "Templates/EnableIf.h"
#include "Templates/IsArray.h"
#include "Templates/RemoveCV.h"
#include "Templates/UnrealTemplate.h"
#include "Traits/IsContiguousContainer.h"

namespace StringViewPrivate
{
	/** Wrapper to allow GetData to resolve from within a scope with another overload. */
	template <typename... ArgTypes>
	constexpr inline auto WrapGetData(ArgTypes&&... Args) -> decltype(GetData(Forward<ArgTypes>(Args)...))
	{
		return GetData(Forward<ArgTypes>(Args)...);
	}
}

/**
 * String View
 *
 * A string view is implicitly constructible from const char* style strings and
 * from compatible character ranges such as FString and TStringBuilderBase.
 *
 * A string view does not own any data nor does it attempt to control any lifetimes, it
 * merely points at a subrange of characters in some other string. It's up to the user
 * to ensure the underlying string stays valid for the lifetime of the string view.
 *
 * A string view is cheap to copy and is intended to be passed by value.
 *
 * A string view does not represent a NUL terminated string and therefore you should
 * never pass in the pointer returned by GetData() into a C-string API accepting only a
 * pointer. You must either use a string builder to make a properly terminated string,
 * or use an API that accepts a length argument in addition to the C-string.
 *
 * String views are a good fit for arguments to functions which don't wish to care
 * which style of string construction is used by the caller. If you accept strings via
 * string views then the caller is free to use FString, FStringBuilder, raw C strings,
 * or any other type which can be converted into a string view.
 *
 * @code
 *	void DoFoo(FStringView InString);
 *
 *	// DoFoo may be called as:
 *	void MultiFoo()
 *	{
 *		FString MyFoo(TEXT("Zoo"));
 *		const TCHAR* MyFooStr = *MyFoo;
 *
 *		TStringBuilder<64> BuiltFoo;
 *		BuiltFoo.Append(TEXT("ABC"));
 *
 *		DoFoo(MyFoo);
 *		DoFoo(MyFooStr);
 *		DoFoo(TEXT("ABC"));
 *		DoFoo(BuiltFoo);
 *	}
 * @endcode
 */
template <typename CharType>
class TStringView
{
public:
	using ElementType = CharType;
	using SizeType = int32;
	using ViewType = TStringView<CharType>;

private:
	/** Trait testing whether a type is a contiguous range of CharType, and not CharType[]. */
	template <typename CharRangeType>
	using TIsCharRange = TAnd<
		TIsContiguousContainer<CharRangeType>,
		TNot<TIsArray<typename TRemoveReference<CharRangeType>::Type>>,
		TIsSame<ElementType, typename TRemoveCV<typename TRemovePointer<decltype(StringViewPrivate::WrapGetData(DeclVal<CharRangeType&>()))>::Type>::Type>>;

public:
	/** Construct an empty view. */
	constexpr TStringView() = default;

	/**
	 * Construct a view of the null-terminated string pointed to by InData.
	 *
	 * The caller is responsible for ensuring that the provided character range remains valid for the lifetime of the view.
	 */
	constexpr inline TStringView(const CharType* InData)
		: DataPtr(InData)
		, Size(InData ? TCString<CharType>::Strlen(InData) : 0)
	{
	}

	/**
	 * Construct a view of InSize characters beginning at InData.
	 *
	 * The caller is responsible for ensuring that the provided character range remains valid for the lifetime of the view.
	 */
	constexpr inline TStringView(const CharType* InData, SizeType InSize)
		: DataPtr(InData)
		, Size(InSize)
	{
	}

	/**
	 * Construct a view from a contiguous range of characters.
	 *
	 * The caller is responsible for ensuring that the provided character range remains valid for the lifetime of the view.
	 */
	template <typename CharRangeType,
		typename TEnableIf<TAnd<
			TNot<TIsSame<ViewType, typename TDecay<CharRangeType>::Type>>,
			TIsCharRange<CharRangeType>
		>::Value>::Type* = nullptr>
	constexpr inline TStringView(CharRangeType&& InRange)
		: DataPtr(StringViewPrivate::WrapGetData(InRange))
		, Size(static_cast<SizeType>(GetNum(InRange)))
	{
	}

	/** Access the character at the given index in the view. */
	inline const CharType& operator[](SizeType Index) const;

	/** Returns a pointer to the start of the view. This is NOT guaranteed to be null-terminated! */
	constexpr inline const CharType* GetData() const { return DataPtr; }

	/** Returns a pointer to the start of the view. This is NOT guaranteed to be null-terminated! */
	UE_DEPRECATED(4.25, "'Data' is deprecated. Please use 'GetData' instead!")
	constexpr inline const CharType* Data() const { return DataPtr; }

	// Capacity

	/** Returns the length of the string view. */
	constexpr inline SizeType Len() const { return Size; }

	/** Returns whether the string view is empty. */
	constexpr inline bool IsEmpty() const { return Size == 0; }

	// Modifiers

	/** Modifies the view to remove the given number of characters from the start. */
	inline void		RemovePrefix(SizeType CharCount)	{ DataPtr += CharCount; Size -= CharCount; }
	/** Modifies the view to remove the given number of characters from the end. */
	inline void		RemoveSuffix(SizeType CharCount)	{ Size -= CharCount; }
	/** Resets to an empty view */
	inline void		Reset()								{ DataPtr = nullptr; Size = 0; }

	// Operations

	/**
	 * Copy characters from the view into a destination buffer without null termination.
	 *
	 * @param Dest Buffer to write into. Must have space for at least CharCount characters.
	 * @param CharCount The maximum number of characters to copy.
	 * @param Position The offset into the view from which to start copying.
	 *
	 * @return The number of characters written to the destination buffer.
	 */
	inline SizeType CopyString(CharType* Dest, SizeType CharCount, SizeType Position = 0) const;

	/** Alias for Mid. */
	inline ViewType SubStr(SizeType Position, SizeType CharCount) const { return Mid(Position, CharCount); }

	/** Returns the left-most part of the view by taking the given number of characters from the left. */
	inline ViewType Left(SizeType CharCount) const;
	/** Returns the left-most part of the view by chopping the given number of characters from the right. */
	inline ViewType LeftChop(SizeType CharCount) const;
	/** Returns the right-most part of the view by taking the given number of characters from the right. */
	inline ViewType Right(SizeType CharCount) const;
	/** Returns the right-most part of the view by chopping the given number of characters from the left. */
	inline ViewType RightChop(SizeType CharCount) const;
	/** Returns the middle part of the view by taking up to the given number of characters from the given position. */
	inline ViewType Mid(SizeType Position, SizeType CharCount = TNumericLimits<SizeType>::Max()) const;
	/** Returns the middle part of the view between any whitespace at the start and end. */
	inline ViewType TrimStartAndEnd() const;
	/** Returns the right part of the view after any whitespace at the start. */
	CORE_API ViewType TrimStart() const;
	/** Returns the left part of the view before any whitespace at the end. */
	CORE_API ViewType TrimEnd() const;

	/** Modifies the view to be the given number of characters from the left. */
	inline void LeftInline(SizeType CharCount) { *this = Left(CharCount); }
	/** Modifies the view by chopping the given number of characters from the right. */
	inline void LeftChopInline(SizeType CharCount) { *this = LeftChop(CharCount); }
	/** Modifies the view to be the given number of characters from the right. */
	inline void RightInline(SizeType CharCount) { *this = Right(CharCount); }
	/** Modifies the view by chopping the given number of characters from the left. */
	inline void RightChopInline(SizeType CharCount) { *this = RightChop(CharCount); }
	/** Modifies the view to be the middle part by taking up to the given number of characters from the given position. */
	inline void MidInline(SizeType Position, SizeType CharCount = TNumericLimits<SizeType>::Max()) { *this = Mid(Position, CharCount); }
	/** Modifies the view to be the middle part between any whitespace at the start and end. */
	inline void TrimStartAndEndInline() { *this = TrimStartAndEnd(); }
	/** Modifies the view to be the right part after any whitespace at the start. */
	inline void TrimStartInline() { *this = TrimStart(); }
	/** Modifies the view to be the left part before any whitespace at the end. */
	inline void TrimEndInline() { *this = TrimEnd(); }

	// Comparison

	/**
	 * Check whether this view is lexicographically equivalent to another view.
	 *
	 * @param SearchCase Whether the comparison should ignore case.
	 */
	inline bool Equals(ViewType Other, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive) const;

	/**
	 * Compare this view lexicographically with another view.
	 *
	 * @param SearchCase Whether the comparison should ignore case.
	 *
	 * @return 0 is equal, negative if this view is less, positive if this view is greater.
	 */
	CORE_API int32 Compare(ViewType Other, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive) const;

	/** Returns whether this view starts with the prefix character compared case-sensitively. */
	inline bool StartsWith(CharType Prefix) const { return Size >= 1 && DataPtr[0] == Prefix; }
	/** Returns whether this view starts with the prefix with optional case sensitivity. */
	inline bool StartsWith(ViewType Prefix, ESearchCase::Type SearchCase = ESearchCase::IgnoreCase) const;

	/** Returns whether this view ends with the suffix character compared case-sensitively. */
	inline bool EndsWith(CharType Suffix) const { return Size >= 1 && DataPtr[Size-1] == Suffix; }
	/** Returns whether this view ends with the suffix with optional case sensitivity. */
	inline bool EndsWith(ViewType Suffix, ESearchCase::Type SearchCase = ESearchCase::IgnoreCase) const;

	// Searching/Finding

	/**
	 * Search the view for the first occurrence of a character.
	 *
	 * @param InChar The character to search for. Comparison is lexicographic.
	 * @param OutIndex [out] The position at which the character was found, or INDEX_NONE if not found.
	 *
	 * @return Whether the character was found in the view.
	 */
	CORE_API bool FindChar(CharType InChar, SizeType& OutIndex) const;

	/**
	 * Search the view for the last occurrence of a character.
	 *
	 * @param InChar The character to search for. Comparison is lexicographic.
	 * @param OutIndex [out] The position at which the character was found, or INDEX_NONE if not found.
	 *
	 * @return Whether the character was found in the view.
	 */
	CORE_API bool FindLastChar(CharType InChar, SizeType& OutIndex) const;

public:
	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	constexpr inline const CharType* begin() const { return DataPtr; }
	constexpr inline const CharType* end() const { return DataPtr + Size; }

protected:
	const CharType* DataPtr = nullptr;
	SizeType Size = 0;
};

template <typename CharType>
constexpr inline auto GetNum(TStringView<CharType> String)
{
	return String.Len();
}

//////////////////////////////////////////////////////////////////////////

constexpr inline FStringView operator "" _SV(const TCHAR* String, size_t Size) { return FStringView(String, Size); }
constexpr inline FAnsiStringView operator "" _ASV(const ANSICHAR* String, size_t Size) { return FAnsiStringView(String, Size); }
constexpr inline FWideStringView operator "" _WSV(const WIDECHAR* String, size_t Size) { return FWideStringView(String, Size); }

//////////////////////////////////////////////////////////////////////////

/** Case insensitive string hash function. */
template <typename CharType>
FORCEINLINE uint32 GetTypeHash(TStringView<CharType> View)
{
	// This must match the GetTypeHash behavior of FString
	return FCrc::Strihash_DEPRECATED(View.Len(), View.GetData());
}

#include "StringView.inl"
