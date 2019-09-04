// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoftObjectPathCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/GameViewportClient.h"
#include "AssetData.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"

void FSoftObjectPathCustomization::CustomizeHeader( TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
	StructPropertyHandle = InStructPropertyHandle;

	const FString& AllowedClassFilterString = StructPropertyHandle->GetMetaData("AllowedClasses");
	if (!AllowedClassFilterString.IsEmpty())
	{
		TArray<FString> AllowedClassFilterNames;
		AllowedClassFilterString.ParseIntoArray(AllowedClassFilterNames, TEXT(","), true);

		for(auto It = AllowedClassFilterNames.CreateConstIterator(); It; ++It)
		{
			const FString& ClassName = *It;

			UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName);
			if (!Class)
			{
				Class = LoadObject<UClass>(nullptr, *ClassName);
			}
			if (Class)
			{
				AllowedClassFilters.Add(Class);
			}
		}
	}

	const FString& DisallowedClassFilterString = StructPropertyHandle->GetMetaData("DisallowedClasses");
	if (!DisallowedClassFilterString.IsEmpty())
	{
		TArray<FString> DisallowedClassFilterNames;
		DisallowedClassFilterString.ParseIntoArray(DisallowedClassFilterNames, TEXT(","), true);

		for (auto It = DisallowedClassFilterNames.CreateConstIterator(); It; ++It)
		{
			const FString& ClassName = *It;

			UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName);
			if (!Class)
			{
				Class = LoadObject<UClass>(nullptr, *ClassName);
			}
			if (Class)
			{
				DisallowedClassFilters.Add(Class);
			}
		}
	}

	bExactClass = StructPropertyHandle->GetBoolMetaData("ExactClass");

	FOnShouldFilterAsset AssetFilter;
	UClass* ClassFilter = UObject::StaticClass();
	if (AllowedClassFilters.Num() == 1 && DisallowedClassFilters.Num() <= 0 && !bExactClass)
	{
		// If we only have one class to filter on, set it as the class type filter rather than use a filter callback
		// We can only do this if we don't need an exact match, as the class filter also allows derived types
		// The class filter is much faster than the callback as we're not performing two different sets of type tests
		// (one against UObject, one against the actual type)
		ClassFilter = AllowedClassFilters[0];
	}
	else if (AllowedClassFilters.Num() > 0 || DisallowedClassFilters.Num() > 0)
	{
		// Only bind the filter if we have classes that need filtering
		AssetFilter.BindSP(this, &FSoftObjectPathCustomization::OnShouldFilterAsset);
	}

	// Can the field be cleared
	const bool bAllowClear = !(StructPropertyHandle->GetMetaDataProperty()->PropertyFlags & CPF_NoClear);

	HeaderRow
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(0.0f)
	[
		// Add an object entry box.  Even though this isn't an object entry, we will simulate one
		SNew( SObjectPropertyEntryBox )
		.PropertyHandle( InStructPropertyHandle )
		.ThumbnailPool( StructCustomizationUtils.GetThumbnailPool() )
		.AllowedClass( ClassFilter )
		.OnShouldFilterAsset( AssetFilter )
		.AllowClear( bAllowClear )
	];

	// This avoids making duplicate reset boxes
	StructPropertyHandle->MarkResetToDefaultCustomized();
}

void FSoftObjectPathCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

bool FSoftObjectPathCustomization::OnShouldFilterAsset( const FAssetData& InAssetData ) const
{
	// Only bound if we have classes to filter on, so we don't need to test for an empty array here
	UClass* const AssetClass = InAssetData.GetClass();

	for (auto It = DisallowedClassFilters.CreateConstIterator(); It; ++It)
	{
		UClass* const DisallowClass = *It;
		if (AssetClass->IsChildOf(DisallowClass))
		{
			return true;
		}
	}

	for(auto It = AllowedClassFilters.CreateConstIterator(); It; ++It)
	{
		UClass* const FilterClass = *It;
		const bool bMatchesFilter = (bExactClass) ? AssetClass == FilterClass : AssetClass->IsChildOf(FilterClass);
		if (bMatchesFilter)
		{
			return false;
		}
	}

	return true;
}
