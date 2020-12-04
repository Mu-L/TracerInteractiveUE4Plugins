// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/UnrealMemory.h"
#include "Traits/IntType.h"

template <typename CharType>
inline const CharType& TStringView<CharType>::operator[](SizeType Index) const
{
	checkf(Index >= 0 && Index < Size, TEXT("Index out of bounds on StringView: index %i on a view with a length of %i"), Index, Size);
	return DataPtr[Index];
}

template <typename CharType>
inline typename TStringView<CharType>::SizeType TStringView<CharType>::CopyString(CharType* Dest, SizeType CharCount, SizeType Position) const
{
	const  SizeType CopyCount = FMath::Min(Size - Position, CharCount);
	FMemory::Memcpy(Dest, DataPtr + Position, CopyCount * sizeof(CharType));
	return CopyCount;
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::Left(SizeType CharCount) const
{
	return ViewType(DataPtr, FMath::Clamp(CharCount, 0, Size));
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::LeftChop(SizeType CharCount) const
{
	return ViewType(DataPtr, FMath::Clamp(Size - CharCount, 0, Size));
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::Right(SizeType CharCount) const
{
	const SizeType OutLen = FMath::Clamp(CharCount, 0, Size);
	return ViewType(DataPtr + Size - OutLen, OutLen);
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::RightChop(SizeType CharCount) const
{
	const SizeType OutLen = FMath::Clamp(Size - CharCount, 0, Size);
	return ViewType(DataPtr + Size - OutLen, OutLen);
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::Mid(SizeType Position, SizeType CharCount) const
{
	using USizeType = TUnsignedIntType_T<sizeof(SizeType)>;
	Position = FMath::Clamp<USizeType>(Position, 0, Size);
	CharCount = FMath::Clamp<USizeType>(CharCount, 0, Size - Position);
	return ViewType(DataPtr + Position, CharCount);
}

template <typename CharType>
inline TStringView<CharType> TStringView<CharType>::TrimStartAndEnd() const
{
	return TrimStart().TrimEnd();
}

template <typename CharType>
inline bool TStringView<CharType>::Equals(ViewType Other, ESearchCase::Type SearchCase) const
{
	return Size == Other.Size && Compare(Other, SearchCase) == 0;
}

template <typename CharType>
inline bool TStringView<CharType>::StartsWith(ViewType Prefix, ESearchCase::Type SearchCase) const
{
	return Prefix.Equals(Left(Prefix.Len()), SearchCase);
}

template <typename CharType>
inline bool TStringView<CharType>::EndsWith(ViewType Suffix, ESearchCase::Type SearchCase) const
{
	return Suffix.Equals(Right(Suffix.Len()), SearchCase);
}

// Case-insensitive string view comparison operators

template <typename CharType>
inline bool operator==(TStringView<CharType> Lhs, TStringView<CharType> Rhs)
{
	return Lhs.Equals(Rhs, ESearchCase::IgnoreCase);
}

template <typename CharType>
inline bool operator!=(TStringView<CharType> Lhs, TStringView<CharType> Rhs)
{
	return !(Lhs == Rhs);
}

// Case-insensitive character range comparison operators

template <typename CharType, typename CharRangeType>
inline auto operator==(TStringView<CharType> Lhs, CharRangeType&& Rhs)
	-> decltype(Lhs.Equals(ImplicitConv<TStringView<CharType>>(Forward<CharRangeType>(Rhs)), ESearchCase::IgnoreCase))
{
	return Lhs.Equals(ImplicitConv<TStringView<CharType>>(Forward<CharRangeType>(Rhs)), ESearchCase::IgnoreCase);
}

template <typename CharType, typename CharRangeType>
inline auto operator==(CharRangeType&& Lhs, TStringView<CharType> Rhs)
	-> decltype(ImplicitConv<TStringView<CharType>>(Forward<CharRangeType>(Lhs)).Equals(Rhs, ESearchCase::IgnoreCase))
{
	return ImplicitConv<TStringView<CharType>>(Forward<CharRangeType>(Lhs)).Equals(Rhs, ESearchCase::IgnoreCase);
}

template <typename CharType, typename CharRangeType>
inline auto operator!=(TStringView<CharType> Lhs, CharRangeType&& Rhs) -> decltype(!(Lhs == Forward<CharRangeType>(Rhs)))
{
	return !(Lhs == Forward<CharRangeType>(Rhs));
}

template <typename CharType, typename CharRangeType>
inline auto operator!=(CharRangeType&& Lhs, TStringView<CharType> Rhs) -> decltype(!(Rhs == Forward<CharRangeType>(Lhs)))
{
	return !(Rhs == Forward<CharRangeType>(Lhs));
}

// Case-insensitive C-string comparison operators

template <typename CharType>
inline bool operator==(TStringView<CharType> Lhs, const CharType* Rhs)
{
	return TCString<CharType>::Strnicmp(Lhs.GetData(), Rhs, Lhs.Len()) == 0 && !Rhs[Lhs.Len()];
}

template <typename CharType>
inline bool operator==(const CharType* Lhs, TStringView<CharType> Rhs)
{
	return Rhs == Lhs;
}

template <typename CharType>
inline bool operator!=(TStringView<CharType> Lhs, const CharType* Rhs)
{
	return !(Lhs == Rhs);
}

template <typename CharType>
inline bool operator!=(const CharType* Lhs, TStringView<CharType> Rhs)
{
	return !(Lhs == Rhs);
}
