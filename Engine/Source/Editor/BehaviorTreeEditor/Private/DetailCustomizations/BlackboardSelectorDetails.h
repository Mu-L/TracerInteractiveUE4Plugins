// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "UnrealClient.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class UBlackboardData;

class FBlackboardSelectorDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader( TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;
	virtual void CustomizeChildren( TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;

private:

	void CacheBlackboardData();
	void FindBlackboardAsset(const UObject* InObj, const UObject*& OutBlackboardOwner, UBlackboardData*& OutBlackboardAsset) const;

	void OnBlackboardDataChanged(UBlackboardData* Asset);
	void OnBlackboardOwnerChanged(UObject* Owner, UBlackboardData* Asset);

	void InitKeyFromProperty();
	void OnKeyComboChange(int32 Index);
	TSharedRef<SWidget> OnGetKeyContent() const;
	FText GetCurrentKeyDesc() const;
	bool IsEditingEnabled() const;

	FDelegateHandle	OnBlackboardDataChangedHandle;
	FDelegateHandle	OnBlackboardOwnerChangedHandle;

	TSharedPtr<IPropertyHandle> MyStructProperty;
	TSharedPtr<IPropertyHandle> MyKeyNameProperty;
	TSharedPtr<IPropertyHandle> MyKeyIDProperty;
	TSharedPtr<IPropertyHandle> MyKeyClassProperty;

	/** cached names of keys */	
	TArray<FName> KeyValues;

	bool bNoneIsAllowedValue;

	/** cached blackboard asset */
	TWeakObjectPtr<class UBlackboardData> CachedBlackboardAsset;
	TWeakObjectPtr<const class UObject> CachedBlackboardAssetOwner;

	/** property utils */
	class IPropertyUtilities* PropUtils;
};
