// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnalyticsConversion.h"

struct FJsonNull
{
};

struct FJsonFragment
{
	explicit FJsonFragment(FString&& StringRef) : FragmentString(MoveTemp(StringRef)) {}
	FString FragmentString;
};

/**
 * Struct to hold key/value pairs that will be sent as attributes along with analytics events.
 * All values are actually strings, but we provide a convenient constructor that relies on ToStringForAnalytics() to 
 * convert common types. 
 */
struct FAnalyticsEventAttribute
{
	const FString AttrName;

	const FString AttrValueString;
	const double AttrValueNumber;
	const bool AttrValueBool;

	enum class AttrTypeEnum
	{
		String,
		Number,
		Boolean,
		Null,
		JsonFragment
	};
	const AttrTypeEnum AttrType;

	/** Default ctor since we declare a custom ctor. */
	FAnalyticsEventAttribute()
		: AttrName()
		, AttrValueString()
		, AttrValueNumber(0)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::String)
	{}

	/** Reinstate the default copy ctor because that one still works fine. */
	FAnalyticsEventAttribute(const FAnalyticsEventAttribute& RHS) = default;

	/** Hack to allow copy ctor using an rvalue-ref. This class only "sort of" acts like an immutable class because the const members prevents assignment, which was not intended when this code was changed. */
	FAnalyticsEventAttribute(FAnalyticsEventAttribute&& RHS)
		: AttrName(MoveTemp(const_cast<FString&>(RHS.AttrName)))
		, AttrValueString(MoveTemp(const_cast<FString&>(RHS.AttrValueString)))
		// no need to use MoveTemp on intrinsic types.
		, AttrValueNumber(RHS.AttrValueNumber)
		, AttrValueBool(RHS.AttrValueBool)
		, AttrType(RHS.AttrType)
	{
	}

	/** Hack to allow assignment. This class only "sort of" acts like an immutable class because the const members prevents assignment, which was not intended when this code was changed. */
	FAnalyticsEventAttribute& operator=(const FAnalyticsEventAttribute& RHS)
	{
		if (&RHS == this)
		{
			return *this;
		}

		const_cast<FString&>(AttrName) = RHS.AttrName;
		const_cast<FString&>(AttrValueString) = RHS.AttrValueString;
		const_cast<double&>(AttrValueNumber) = RHS.AttrValueNumber;
		const_cast<bool&>(AttrValueBool) = RHS.AttrValueBool;
		const_cast<AttrTypeEnum&>(AttrType) = RHS.AttrType;
		return *this;
	}

	/** Hack to allow assignment. This class only "sort of" acts like an immutable class because the const members prevents assignment, which was not intended when this code was changed. */
	FAnalyticsEventAttribute& operator=(FAnalyticsEventAttribute&& RHS)
	{
		if (&RHS == this)
		{
			return *this;
		}

		const_cast<FString&>(AttrName) = MoveTemp(const_cast<FString&>(RHS.AttrName));
		const_cast<FString&>(AttrValueString) = MoveTemp(const_cast<FString&>(RHS.AttrValueString));
		// no need to use MoveTemp on intrinsic types.
		const_cast<double&>(AttrValueNumber) = RHS.AttrValueNumber;
		const_cast<bool&>(AttrValueBool) = RHS.AttrValueBool;
		const_cast<AttrTypeEnum&>(AttrType) = RHS.AttrType;
		return *this;
	}

	/** If you need the old AttrValue behavior (i.e. stringify everything), call this function instead. */
	FString ToString() const
	{
		switch (AttrType)
		{
		case AttrTypeEnum::String:
		case AttrTypeEnum::JsonFragment:
			return AttrValueString;
		case AttrTypeEnum::Number:
			// From CL #3669417 : Integer numbers are formatted as "1" and not "1.00"
			if (AttrValueNumber - FMath::FloorToDouble(AttrValueNumber) == 0.0)
				return LexToSanitizedString((int64)AttrValueNumber);
			return LexToSanitizedString(AttrValueNumber);
		case AttrTypeEnum::Boolean:
			return LexToString(AttrValueBool);
		case AttrTypeEnum::Null:
			return TEXT("null");
		default:
			ensure(false);
			return FString();
		}
	}

public: // null
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, FJsonNull)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(0)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::Null)
	{
	}

public: // numeric types
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, double InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(InValue)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::Number)
	{
	}
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, float InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(InValue)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::Number)
	{
	}
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, int32 InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(InValue)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::Number)
	{
	}
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, uint32 InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(InValue)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::Number)
	{
	}

public: // boolean
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, bool InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString()
		, AttrValueNumber(0)
		, AttrValueBool(InValue)
		, AttrType(AttrTypeEnum::Boolean)
	{
	}

public: // json fragment
	template <typename NameType>
	FAnalyticsEventAttribute(NameType&& InName, FJsonFragment&& Fragment)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString(MoveTemp(Fragment.FragmentString))
		, AttrValueNumber(0)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::JsonFragment)
	{
	}

public: // string (catch-all)
	/**
	 * Helper constructor to make an attribute from a name/value pair by forwarding through LexToString and AnalyticsConversionToString.
	 * 
	 * @param InName Name of the attribute. Will be converted to a string via forwarding to LexToString
	 * @param InValue Value of the attribute. Will be converted to a string via forwarding to AnalyticsConversionToString (same as Lex but with basic support for arrays and maps)
	 */
	template <typename NameType, typename ValueType>
	FAnalyticsEventAttribute(NameType&& InName, ValueType&& InValue)
		: AttrName(LexToString(Forward<NameType>(InName)))
		, AttrValueString(AnalyticsConversionToString(Forward<ValueType>(InValue)))
		, AttrValueNumber(0)
		, AttrValueBool(false)
		, AttrType(AttrTypeEnum::String)
	{}
};

/** Helper functions for MakeAnalyticsEventAttributeArray. */
namespace ImplMakeAnalyticsEventAttributeArray
{
	/** Recursion terminator. Empty list. */
	template <typename Allocator>
	inline void MakeArray(TArray<FAnalyticsEventAttribute, Allocator>& Attrs)
	{
	}

	/** Recursion terminator. Convert the key/value pair to analytics strings. */
	template <typename Allocator, typename KeyType, typename ValueType>
	inline void MakeArray(TArray<FAnalyticsEventAttribute, Allocator>& Attrs, KeyType&& Key, ValueType&& Value)
	{
		Attrs.Emplace(Forward<KeyType>(Key), Forward<ValueType>(Value));
	}

	/** recursively add the arguments to the array. */
	template <typename Allocator, typename KeyType, typename ValueType, typename...ArgTypes>
	inline void MakeArray(TArray<FAnalyticsEventAttribute, Allocator>& Attrs, KeyType&& Key, ValueType&& Value, ArgTypes&&...Args)
	{
		// pop off the top two args and recursively apply the rest.
		Attrs.Emplace(Forward<KeyType>(Key), Forward<ValueType>(Value));
		MakeArray(Attrs, Forward<ArgTypes>(Args)...);
	}
}

/** Helper to create an array of attributes using a single expression. Reserves the necessary space in advance. There must be an even number of arguments, one for each key/value pair. */
template <typename Allocator = FDefaultAllocator, typename...ArgTypes>
inline TArray<FAnalyticsEventAttribute, Allocator> MakeAnalyticsEventAttributeArray(ArgTypes&&...Args)
{
	static_assert(sizeof...(Args) % 2 == 0, "Must pass an even number of arguments.");
	TArray<FAnalyticsEventAttribute, Allocator> Attrs;
	Attrs.Empty(sizeof...(Args) / 2);
	ImplMakeAnalyticsEventAttributeArray::MakeArray(Attrs, Forward<ArgTypes>(Args)...);
	return Attrs;
}

/** Helper to append to an array of attributes using a single expression. Reserves the necessary space in advance. There must be an even number of arguments, one for each key/value pair. */
template <typename Allocator = FDefaultAllocator, typename...ArgTypes>
inline TArray<FAnalyticsEventAttribute, Allocator>& AppendAnalyticsEventAttributeArray(TArray<FAnalyticsEventAttribute, Allocator>& Attrs, ArgTypes&&...Args)
{
	static_assert(sizeof...(Args) % 2 == 0, "Must pass an even number of arguments.");
	Attrs.Reserve(Attrs.Num() + (sizeof...(Args) / 2));
	ImplMakeAnalyticsEventAttributeArray::MakeArray(Attrs, Forward<ArgTypes>(Args)...);
	return Attrs;
}
