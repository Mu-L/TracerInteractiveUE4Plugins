// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "ObjectEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"

#if WITH_EDITOR
#include "EditorCategoryUtils.h"

namespace FObjectEditorUtils
{

	FText GetCategoryText( const UProperty* InProperty )
	{
		static const FName NAME_Category(TEXT("Category"));
		if (InProperty && InProperty->HasMetaData(NAME_Category))
		{
			return InProperty->GetMetaDataText(NAME_Category, TEXT("UObjectCategory"), InProperty->GetFullGroupName(false));
		}
		else
		{
			return FText::GetEmpty();
		}
	}

	FString GetCategory( const UProperty* InProperty )
	{
		return GetCategoryText(InProperty).ToString();
	}


	FName GetCategoryFName( const UProperty* InProperty )
	{
		FName OutCategoryName( NAME_None );

		static const FName CategoryKey( TEXT("Category") );
		if( InProperty && InProperty->HasMetaData( CategoryKey ) )
		{
			OutCategoryName = FName( *InProperty->GetMetaData( CategoryKey ) );
		}

		return OutCategoryName;
	}

	bool IsFunctionHiddenFromClass( const UFunction* InFunction,const UClass* Class )
	{
		bool bResult = false;
		if( InFunction )
		{
			bResult = Class->IsFunctionHidden( *InFunction->GetName() );

			static const FName FunctionCategory(TEXT("Category")); // FBlueprintMetadata::MD_FunctionCategory
			if( !bResult && InFunction->HasMetaData( FunctionCategory ) )
			{
				FString const& FuncCategory = InFunction->GetMetaData(FunctionCategory);
				bResult = FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, FuncCategory);
			}
		}
		return bResult;
	}

	bool IsVariableCategoryHiddenFromClass( const UProperty* InVariable,const UClass* Class )
	{
		bool bResult = false;
		if( InVariable && Class )
		{
			bResult = FEditorCategoryUtils::IsCategoryHiddenFromClass(Class, GetCategory(InVariable));
		}
		return bResult;
	}

	void GetClassDevelopmentStatus(UClass* Class, bool& bIsExperimental, bool& bIsEarlyAccess)
	{
		static const FName DevelopmentStatusKey(TEXT("DevelopmentStatus"));
		static const FString EarlyAccessValue(TEXT("EarlyAccess"));
		static const FString ExperimentalValue(TEXT("Experimental"));

		bIsExperimental = bIsEarlyAccess = false;

		FString DevelopmentStatus;
		if ( Class->GetStringMetaDataHierarchical(DevelopmentStatusKey, /*out*/ &DevelopmentStatus) )
		{
			bIsExperimental = DevelopmentStatus == ExperimentalValue;
			bIsEarlyAccess = DevelopmentStatus == EarlyAccessValue;
		}
	}

	static void CopySinglePropertyRecursive(UObject* SourceObject, const void* const InSourcePtr, UProperty* InSourceProperty, void* const InTargetPtr, UObject* InDestinationObject, UProperty* InDestinationProperty)
	{
		bool bNeedsShallowCopy = true;
		bool bNeedsStringCopy = false;

		if (UStructProperty* const DestStructProperty = Cast<UStructProperty>(InDestinationProperty))
		{
			UStructProperty* const SrcStructProperty = Cast<UStructProperty>(InSourceProperty);

			// Ensure that the target struct is initialized before copying fields from the source.
			DestStructProperty->InitializeValue_InContainer(InTargetPtr);

			const int32 PropertyArrayDim = DestStructProperty->ArrayDim;
			for (int32 ArrayIndex = 0; ArrayIndex < PropertyArrayDim; ArrayIndex++)
			{
				const void* const SourcePtr = SrcStructProperty->ContainerPtrToValuePtr<void>(InSourcePtr, ArrayIndex);
				void* const TargetPtr = DestStructProperty->ContainerPtrToValuePtr<void>(InTargetPtr, ArrayIndex);

				for (TFieldIterator<UProperty> It(SrcStructProperty->Struct); It; ++It)
				{
					UProperty* const InnerProperty = *It;
					CopySinglePropertyRecursive(SourceObject, SourcePtr, InnerProperty, TargetPtr, InDestinationObject, InnerProperty);
				}
			}

			bNeedsShallowCopy = false;
		}
		else if (UArrayProperty* const DestArrayProperty = Cast<UArrayProperty>(InDestinationProperty))
		{
			UArrayProperty* const SrcArrayProperty = Cast<UArrayProperty>(InSourceProperty);

			check(InDestinationProperty->ArrayDim == 1);
			FScriptArrayHelper SourceArrayHelper(SrcArrayProperty, SrcArrayProperty->ContainerPtrToValuePtr<void>(InSourcePtr));
			FScriptArrayHelper TargetArrayHelper(DestArrayProperty, DestArrayProperty->ContainerPtrToValuePtr<void>(InTargetPtr));

			int32 Num = SourceArrayHelper.Num();

			TargetArrayHelper.EmptyAndAddValues(Num);

			for (int32 Index = 0; Index < Num; Index++)
			{
				CopySinglePropertyRecursive(SourceObject, SourceArrayHelper.GetRawPtr(Index), SrcArrayProperty->Inner, TargetArrayHelper.GetRawPtr(Index), InDestinationObject, DestArrayProperty->Inner);
			}

			bNeedsShallowCopy = false;
		}
		else if ( UMapProperty* const DestMapProperty = Cast<UMapProperty>(InDestinationProperty) )
		{
			UMapProperty* const SrcMapProperty = Cast<UMapProperty>(InSourceProperty);

			check(InDestinationProperty->ArrayDim == 1);
			FScriptMapHelper SourceMapHelper(SrcMapProperty, SrcMapProperty->ContainerPtrToValuePtr<void>(InSourcePtr));
			FScriptMapHelper TargetMapHelper(DestMapProperty, DestMapProperty->ContainerPtrToValuePtr<void>(InTargetPtr));

			TargetMapHelper.EmptyValues();

			int32 Num = SourceMapHelper.Num();
			for ( int32 Index = 0; Num; Index++ )
			{
				if ( SourceMapHelper.IsValidIndex(Index) )
				{
					uint8* SrcPairPtr = SourceMapHelper.GetPairPtr(Index);

					int32 NewIndex = TargetMapHelper.AddDefaultValue_Invalid_NeedsRehash();
					TargetMapHelper.Rehash();

					uint8* PairPtr = TargetMapHelper.GetPairPtr(NewIndex);

					CopySinglePropertyRecursive(SourceObject, SrcPairPtr, SrcMapProperty->KeyProp, PairPtr, InDestinationObject, DestMapProperty->KeyProp);
					CopySinglePropertyRecursive(SourceObject, SrcPairPtr, SrcMapProperty->ValueProp, PairPtr, InDestinationObject, DestMapProperty->ValueProp);

					TargetMapHelper.Rehash();

					--Num;
				}
			}

			bNeedsShallowCopy = false;
			bNeedsStringCopy = false;
		}
		else if ( USetProperty* const DestSetProperty = Cast<USetProperty>(InDestinationProperty) )
		{
			//USetProperty* const SrcSetProperty = Cast<USetProperty>(InSourceProperty);

			//check(InDestinationProperty->ArrayDim == 1);
			//FScriptSetHelper SourceSetHelper(SrcSetProperty, SrcSetProperty->ContainerPtrToValuePtr<void>(InSourcePtr));
			//FScriptSetHelper TargetSetHelper(DestSetProperty, DestSetProperty->ContainerPtrToValuePtr<void>(InTargetPtr));

			//TargetSetHelper.EmptyElements();

			bNeedsShallowCopy = false;
			bNeedsStringCopy = true;
		}
		else if ( UObjectPropertyBase* SourceObjectProperty = Cast<UObjectPropertyBase>(InSourceProperty) )
		{
			if ( SourceObjectProperty->HasAllPropertyFlags(CPF_InstancedReference) )
			{
				UObject* Value = SourceObjectProperty->GetObjectPropertyValue_InContainer(InSourcePtr);
				if (Value)
				{
					// If the outer of the value is the source object, then we need to translate that same relationship
					// onto the destination object by deep copying the value and outering it to the destination object.
					if (Value->GetOuter() == SourceObject)
					{
						bNeedsShallowCopy = false;

						UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), InDestinationObject, *Value->GetFName().ToString());
						if (ExistingObject)
						{
							ExistingObject->Rename(nullptr, GetTransientPackage());
						}

						UObject* DuplicateValue = StaticDuplicateObject(Value, InDestinationObject, Value->GetFName(), RF_AllFlags, nullptr, EDuplicateMode::Normal, EInternalObjectFlags::AllFlags);

						UObjectPropertyBase* DestObjectProperty = CastChecked<UObjectPropertyBase>(InDestinationProperty);
						DestObjectProperty->SetObjectPropertyValue_InContainer(InTargetPtr, DuplicateValue);
					}

					// If the outers match, we should look for a corresponding object already in existance
					// with the same name inside the destination object's outer.
					if (Value->GetOuter() == SourceObject->GetOuter())
					{
						bNeedsShallowCopy = false;

						UObject* DesintationValue = FindObjectFast<UObject>(InDestinationObject->GetOuter(), Value->GetFName());

						UObjectPropertyBase* DestObjectProperty = CastChecked<UObjectPropertyBase>(InDestinationProperty);
						DestObjectProperty->SetObjectPropertyValue_InContainer(InTargetPtr, DesintationValue);
					}
				}
			}
		}

		check(!( bNeedsShallowCopy && bNeedsStringCopy ));

		if ( bNeedsShallowCopy )
		{
			const uint8* SourceAddr = InSourceProperty->ContainerPtrToValuePtr<uint8>(InSourcePtr);
			uint8* DestinationAddr = InDestinationProperty->ContainerPtrToValuePtr<uint8>(InTargetPtr);

			InSourceProperty->CopyCompleteValue(DestinationAddr, SourceAddr);
		}
		else if ( bNeedsStringCopy )
		{
			FString ExportedTextString;
			if ( InSourceProperty->ExportText_InContainer(0, ExportedTextString, InSourcePtr, InSourcePtr, SourceObject, PPF_Copy, SourceObject) )
			{
				InDestinationProperty->ImportText(*ExportedTextString, InDestinationProperty->ContainerPtrToValuePtr<void>(InTargetPtr), 0, InDestinationObject);
			}
		}
	}

	bool MigratePropertyValue(UObject* SourceObject, UProperty* SourceProperty, UObject* DestinationObject, UProperty* DestinationProperty)
	{
		if (SourceObject == nullptr || DestinationObject == nullptr)
		{
			return false;
		}

		// Get the property addresses for the source and destination objects.
		uint8* SourceAddr = SourceProperty->ContainerPtrToValuePtr<uint8>(SourceObject);
		uint8* DestionationAddr = DestinationProperty->ContainerPtrToValuePtr<uint8>(DestinationObject);

		if (SourceAddr == nullptr || DestionationAddr == nullptr)
		{
			return false;
		}

		if (!DestinationObject->HasAnyFlags(RF_ClassDefaultObject))
		{
			FEditPropertyChain PropertyChain;
			PropertyChain.AddHead(DestinationProperty);
			DestinationObject->PreEditChange(PropertyChain);
		}

		CopySinglePropertyRecursive(SourceObject, SourceObject, SourceProperty, DestinationObject, DestinationObject, DestinationProperty);

		if (!DestinationObject->HasAnyFlags(RF_ClassDefaultObject))
		{
			FPropertyChangedEvent PropertyEvent(DestinationProperty);
			DestinationObject->PostEditChangeProperty(PropertyEvent);
		}

		return true;
	}
}

#endif // WITH_EDITOR
