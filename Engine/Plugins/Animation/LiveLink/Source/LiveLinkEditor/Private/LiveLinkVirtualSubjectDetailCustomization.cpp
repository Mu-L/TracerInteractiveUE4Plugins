// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkVirtualSubjectDetailCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "ILiveLinkClient.h"
#include "GuidStructCustomization.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "LiveLinkVirtualSubjectDetailsCustomization"


void FLiveLinkVirtualSubjectDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	MyDetailsBuilder = &DetailBuilder;

	for (const TWeakObjectPtr<UObject> SelectedObject : MyDetailsBuilder->GetSelectedObjects())
	{
		if (ULiveLinkVirtualSubject* Selection = Cast<ULiveLinkVirtualSubject>(SelectedObject.Get()))
		{
			SubjectPtr = Selection;
			break;
		}
	}

	ULiveLinkVirtualSubject* Subject = SubjectPtr.Get();
	if (Subject == nullptr)
	{
		return;
	}

	Client = Subject->GetClient();

	SubjectsPropertyHandle = DetailBuilder.GetProperty(TEXT("Subjects"));

	{
		UArrayProperty* ArrayProperty = Cast<UArrayProperty>(SubjectsPropertyHandle->GetProperty());
		check(ArrayProperty);
		UStructProperty* StructProperty = Cast<UStructProperty>(ArrayProperty->Inner);
		check(StructProperty);
		check(StructProperty->Struct == FLiveLinkSubjectName::StaticStruct());
	}

	DetailBuilder.HideProperty(SubjectsPropertyHandle);

	SubjectsListItems.Reset();

	TArray<FLiveLinkSubjectKey> SubjectKeys = Client->GetSubjectsSupportingRole(Subject->GetRole(), false, false);
	for (const FLiveLinkSubjectKey& SubjectKey : SubjectKeys)
	{
		SubjectsListItems.AddUnique(MakeShared<FName>(SubjectKey.SubjectName.Name));
	}

	IDetailCategoryBuilder& SubjectPropertyGroup = DetailBuilder.EditCategory(*SubjectsPropertyHandle->GetMetaData(TEXT("Category")));
	SubjectPropertyGroup.AddCustomRow(LOCTEXT("SubjectsTitleLabel", "Subjects"))
	.NameContent()
	[
		SubjectsPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(SubjectsListView, SListView<FSubjectEntryPtr>)
		.ListItemsSource(&SubjectsListItems)
		.OnGenerateRow(this, &FLiveLinkVirtualSubjectDetailCustomization::OnGenerateWidgetForSubjectItem)
	];
}

int32 GetArrayPropertyIndex(TSharedPtr<IPropertyHandleArray> ArrayProperty, FName ItemToSearchFor, uint32 NumItems)
{
	for (uint32 Index = 0; Index < NumItems; ++Index)
	{
		TArray<void*> RawData;
		ArrayProperty->GetElement(Index)->AccessRawData(RawData);
		FLiveLinkSubjectName* SubjectNamePtr = reinterpret_cast<FLiveLinkSubjectName*>(RawData[0]);
		if (SubjectNamePtr && SubjectNamePtr->Name == ItemToSearchFor)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

TSharedRef<ITableRow> FLiveLinkVirtualSubjectDetailCustomization::OnGenerateWidgetForSubjectItem(FSubjectEntryPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FLiveLinkVirtualSubjectDetailCustomization* CaptureThis = this;

	return SNew(STableRow<FSubjectEntryPtr>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([CaptureThis, InItem]()
				{
					const TSharedPtr<IPropertyHandleArray> SubjectsArrayPropertyHandle = CaptureThis->SubjectsPropertyHandle->AsArray();
					uint32 NumItems;
					SubjectsArrayPropertyHandle->GetNumElements(NumItems);

					int32 RemoveIndex = GetArrayPropertyIndex(SubjectsArrayPropertyHandle, *InItem, NumItems);

					return RemoveIndex != INDEX_NONE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([CaptureThis, InItem](const ECheckBoxState NewState)
				{
					const TSharedPtr<IPropertyHandleArray> SubjectsArrayPropertyHandle = CaptureThis->SubjectsPropertyHandle->AsArray();
					uint32 NumItems;
					SubjectsArrayPropertyHandle->GetNumElements(NumItems);

					if (NewState == ECheckBoxState::Checked)
					{
						FFormatNamedArguments Arguments;
						Arguments.Add(TEXT("SubjectName"), FText::FromName(*InItem));
						FScopedTransaction Transaction(FText::Format(LOCTEXT("AddSourceToVirtualSubject", "Add {SubjectName} to virtual subject"), Arguments));

						FString TextValue;
						FLiveLinkSubjectName NewSubjectName = *InItem;
						FLiveLinkSubjectName::StaticStruct()->ExportText(TextValue, &NewSubjectName, &NewSubjectName, nullptr, EPropertyPortFlags::PPF_None, nullptr);

						FPropertyAccess::Result Result = SubjectsArrayPropertyHandle->AddItem();
						check(Result == FPropertyAccess::Success);
						Result = SubjectsArrayPropertyHandle->GetElement(NumItems)->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::NotTransactable);
						check(Result == FPropertyAccess::Success);
					}
					else
					{
						int32 RemoveIndex = GetArrayPropertyIndex(SubjectsArrayPropertyHandle, *InItem, NumItems);
						if (RemoveIndex != INDEX_NONE)
						{
							SubjectsArrayPropertyHandle->DeleteItem(RemoveIndex);
						}
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromName(*InItem))
			]
		];
}
#undef LOCTEXT_NAMESPACE
