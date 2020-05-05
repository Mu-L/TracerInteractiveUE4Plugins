// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "DMXProtocolTypes.h"
#include "Widgets/SNameListPicker.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Templates/SubclassOf.h"

class FDMXEditor;
class UDMXLibrary;
class UDMXEntity;
class UDMXEntityFixtureType;
class UDMXEntityFixturePatch;
class UDMXComponent;
class UDMXEntity;

class SEditableTextBox;
struct EVisibility;

class FDMXCustomization
	: public IDetailCustomization
{
public:
	/** Constructor */
	FDMXCustomization(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder & DetailLayout) override;

protected:
	FText OnGetEntityName() const;
	void OnEntityNameChanged(const FText& InNewText);
	void OnEntityNameCommitted(const FText& InNewText, ETextCommit::Type InCommitType);

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;

	/** Custom Name text field to validate input name */
	TSharedPtr<SEditableTextBox> NameEditableTextBox;
	/** Handle to the Name property for getting and setting it */
	TSharedPtr<IPropertyHandle> NamePropertyHandle;
};

/** Details customization for Controllers */
class FDMXControllersDetails
	: public FDMXCustomization
{
public:
	/** Constructor */
	FDMXControllersDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: FDMXCustomization(InDMXEditorPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};

/** Base class for Fixture Types' Modes, Functions and Sub Functions customizations */
class FDMXFixtureTypeFunctionsDetails
	: public IPropertyTypeCustomization
{
public:
	/** Constructor */
	FDMXFixtureTypeFunctionsDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;

protected:
	/** Called to get new Name field settings. */
	virtual void GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError) = 0;

	/** Find the existing names for the function type being edited within the Fixture Type. */
	virtual TArray<FString> GetExistingNames() const = 0;
	
	/** Allows customization of how properties are added */
	virtual void AddProperty(IDetailChildrenBuilder& InStructBuilder, const FName& PropertyName, TSharedRef<IPropertyHandle> PropertyHandle);

	void OnFunctionNameChanged(const FText& InNewText);
	void OnFunctionNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);
	FText OnGetFunctionName() const;
	/** Changes the function name on the fixture properties */
	void SetFunctionName(const FString& NewName);

protected:
	TArray<UDMXEntityFixtureType*> SelectedFixtures;
	TSharedPtr<IPropertyHandle> NamePropertyHandle;
	FName NamePropertyName;

	TWeakPtr<FDMXEditor> DMXEditorPtr;

private:
	TSharedPtr<SEditableTextBox> NameEditableTextBox;
	FText ExistingNameError;
};

/** Details customization for Fixture Modes */
class FDMXFixtureModeDetails
	: public FDMXFixtureTypeFunctionsDetails
{
public:
	/** Constructor */
	FDMXFixtureModeDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: FDMXFixtureTypeFunctionsDetails(InDMXEditorPtr)
	{}

	void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;

protected:
	//~ FDMXFixtureTypeFunctionsDetails interface
	virtual void GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError) override;
	virtual TArray<FString> GetExistingNames() const override;

private:
	TSharedPtr<IPropertyHandle> AutoChannelSpanHandle;
};

/** Details customization for Fixture Mode Functions */
class FDMXFixtureFunctionDetails
	: public FDMXFixtureTypeFunctionsDetails
{
public:
	/** Constructor */
	FDMXFixtureFunctionDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: FDMXFixtureTypeFunctionsDetails(InDMXEditorPtr)
	{}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;

protected:
	//~ FDMXFixtureTypeFunctionsDetails interface
	virtual void AddProperty(IDetailChildrenBuilder& InStructBuilder, const FName& PropertyName, TSharedRef<IPropertyHandle> PropertyHandle) override;
	virtual void GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError) override;
	virtual TArray<FString> GetExistingNames() const override;

private:
	EVisibility GetSubFunctionsVisibility() const;
	
	void AddChannelInputFields(IDetailChildrenBuilder& InStructBuilder);
	TSharedRef<SWidget> CreateChannelField(uint8 Channel, const FLinearColor& LabelColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	TOptional<uint8> GetChannelValue(uint8 Channel) const;
	EVisibility GetChannelInputVisibility(uint8 Channel) const;

	void HandleChannelValueChanged(uint8 NewValue, uint8 Channel);
	void HandleChannelValueCommitted(uint8 NewValue, ETextCommit::Type CommitType);

private:
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<IPropertyHandle> DataTypeHandle;
	TSharedPtr<IPropertyHandle> DefaultValueHandle;
	TSharedPtr<IPropertyHandle> UseLSBHandle;
};

/**
 * Details customization for Fixture Function's Sub Functions.
 * The GetExistingNames implementation is very similar to FDMXFixtureFunctionDetails's.
 * So we only need to override GetCustomNameFieldSettings and the rest just works. :)
 */
class FDMXFixtureSubFunctionDetails
	: public FDMXFixtureFunctionDetails
{
public:
	/** Constructor */
	FDMXFixtureSubFunctionDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: FDMXFixtureFunctionDetails(InDMXEditorPtr)
	{}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;

protected:
	//~ FDMXFixtureTypeFunctionsDetails interface
	virtual void GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError) override;
};

/** Details customization for FixturePatches */
class FDMXFixturePatchesDetails
	: public FDMXCustomization
{
public:
	/** Constructor */
	FDMXFixturePatchesDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: FDMXCustomization(InDMXEditorPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	/** Fill the ActiveModeOptions array with the modes for the selected patches */
	void GenerateActiveModeOptions();
	
	TWeakObjectPtr<UDMXEntityFixtureType> GetParentFixtureTemplate() const;
	void OnParentTemplateSelected(UDMXEntity* NewTemplate) const;
	bool GetParentFixtureTypeIsMultipleValues() const;

	bool GetActiveModeEditable() const;
	TSharedRef<SWidget> GenerateActiveModeOptionWidget(const TSharedPtr<uint32> InMode) const;
	void OnActiveModeChanged(const TSharedPtr<uint32> InSelectedMode, ESelectInfo::Type SelectInfo);
	FText GetCurrentActiveModeLabel() const;

private:
	TArray< TSharedPtr<uint32> > ActiveModeOptions;

	TSharedPtr<IPropertyHandle> ParentFixtureTypeHandle;
	TSharedPtr<IPropertyHandle> ActiveModeHandle;
};

/**  Customization for any property that should be displayed as a dropdown of options from a FName array */
template<typename TStructType>
class FNameListCustomization
	: public IPropertyTypeCustomization
{
private:
	using TNameListType = FNameListCustomization<TStructType>;

public:
	/** Construction requires a delegate that returns the source list of possible names */
	FNameListCustomization(TAttribute<TArray<FName>> InOptionsList, FSimpleMulticastDelegate* InUpdateOptionsDelegate)
		: ListDelegate(InOptionsList)
		, UpdateOptionsDelegate(InUpdateOptionsDelegate)
		, StructPropertyHandle(nullptr)
	{}

	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		StructPropertyHandle = InPropertyHandle;
		TSharedPtr<IPropertyUtilities> PropertyUtils = CustomizationUtils.GetPropertyUtilities();

		check(CastFieldChecked<FStructProperty>(StructPropertyHandle->GetProperty())->Struct == TStructType::StaticStruct());

		InHeaderRow
			.NameContent()
			[
				StructPropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SNameListPicker)
				.Font(CustomizationUtils.GetRegularFont())
				.HasMultipleValues(this, &TNameListType::HasMultipleValues)
				.OptionsSource(ListDelegate)
				.UpdateOptionsDelegate(UpdateOptionsDelegate)
				.Value(this, &TNameListType::GetValue)
				.OnValueChanged(this, &TNameListType::SetValue)
			]
		.IsEnabled(MakeAttributeLambda([=] { return !InPropertyHandle->IsEditConst() && PropertyUtils->IsPropertyEditingEnabled(); }));
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {};
	//~ IPropertyTypeCustomization interface end

private:
	FName GetValue() const
	{
		TArray<const void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);

		for (const void* RawPtr : RawData)
		{
			if (RawPtr != nullptr)
			{
				return reinterpret_cast<const TStructType*>(RawPtr)->Name;
			}
		}

		return FName();
	}

	void SetValue(FName NewValue)
	{
		FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(StructPropertyHandle->GetProperty());

		TArray<void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);
		TStructType* PreviousValue = reinterpret_cast<TStructType*>(RawData[0]);
		TStructType NewProtocolName(NewValue);

		// Export new value to text format that can be imported later
		FString TextValue;
		StructProperty->Struct->ExportText(TextValue, &NewProtocolName, PreviousValue, nullptr, EPropertyPortFlags::PPF_None, nullptr);

		// Set values on edited property handle from exported text
		ensure(StructPropertyHandle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Result::Success);
	}

	bool HasMultipleValues() const
	{
		TArray<const void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);

		TOptional<TStructType> CompareAgainst;
		for (const void* RawPtr : RawData)
		{
			if (RawPtr == nullptr)
			{
				if (CompareAgainst.IsSet())
				{
					return false;
				}
			}
			else
			{
				const TStructType* ThisValue = reinterpret_cast<const TStructType*>(RawPtr);

				if (!CompareAgainst.IsSet())
				{
					CompareAgainst = *ThisValue;
				}
				else if (!(*ThisValue == CompareAgainst.GetValue()))
				{
					return true;
				}
			}
		}

		return false;
	}

private:
	TAttribute< TArray<FName> > ListDelegate;
	FSimpleMulticastDelegate* UpdateOptionsDelegate;
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
};

class FDMXEntityReferenceCustomization
	: public IPropertyTypeCustomization
{
public:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end

private:
	bool GetDisplayLibrary() const;

	TSharedRef<SWidget> CreateEntityPickerWidget(TSharedPtr<IPropertyHandle> InPropertyHandle) const;
	FText GetPickerPropertyLabel() const;
	bool GetPickerEnabled() const;

	TWeakObjectPtr<UDMXEntity> GetCurrentEntity() const;
	bool GetEntityIsMultipleValues() const;
	void OnEntitySelected(UDMXEntity* NewEntity) const;
	TSubclassOf<UDMXEntity> GetEntityType() const;
	TWeakObjectPtr<UDMXLibrary> GetDMXLibrary() const;

private:
	static const FName NAME_DMXLibrary;

	TSharedPtr<IPropertyHandle> StructHandle;
};

struct FDMXCustomizationFactory
{
	template<typename TDetailCustomizationType, typename TReturnType>
	static TSharedRef<TReturnType> MakeInstance(TWeakPtr<FDMXEditor> InEditor)
	{
		return MakeShared<TDetailCustomizationType>(InEditor);
	}

	template<typename TDetailCustomizationType, typename TReturnType>
	static TSharedRef<TReturnType> MakeInstance()
	{
		return MakeShared<TDetailCustomizationType>();
	}

	template<typename TDetailCustomizationType>
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(TAttribute<TArray<FName>> InListOptions, FSimpleMulticastDelegate* UpdateListDelegate)
	{
		return MakeShared<TDetailCustomizationType>(InListOptions, UpdateListDelegate);
	}
};
