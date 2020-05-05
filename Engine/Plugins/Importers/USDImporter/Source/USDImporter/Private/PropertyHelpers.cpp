// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyHelpers.h"
#include "UObject/UnrealType.h"
#include "CoreMinimal.h"
#include "UObject/Class.h"

namespace PropertyHelpers
{
	FPropertyAndIndex FindPropertyAndArrayIndex(UStruct* InStruct, const FString& PropertyName)
	{
		FPropertyAndIndex PropertyAndIndex;

		// Calculate the array index if possible
		int32 ArrayIndex = -1;
		if (PropertyName.Len() > 0 && PropertyName.GetCharArray()[PropertyName.Len() - 1] == ']')
		{
			int32 OpenIndex = 0;
			if (PropertyName.FindLastChar('[', OpenIndex))
			{
				FString TruncatedPropertyName(OpenIndex, *PropertyName);
				PropertyAndIndex.Property = FindFProperty<FProperty>(InStruct, *TruncatedPropertyName);
				if (PropertyAndIndex.Property)
				{
					const int32 NumberLength = PropertyName.Len() - OpenIndex - 2;
					if (NumberLength > 0 && NumberLength <= 10)
					{
						TCHAR NumberBuffer[11];
						FMemory::Memcpy(NumberBuffer, &PropertyName[OpenIndex + 1], sizeof(TCHAR) * NumberLength);
						LexFromString(PropertyAndIndex.ArrayIndex, NumberBuffer);
					}
				}

				return PropertyAndIndex;
			}
		}

		PropertyAndIndex.Property = FindFProperty<FProperty>(InStruct, *PropertyName);
		return PropertyAndIndex;
	}

	FPropertyAddress FindPropertyRecursive(void* BasePointer, UStruct* InStruct, TArray<FString>& InPropertyNames, uint32 Index, TArray<FProperty*>& InOutPropertyChain, bool bAllowArrayResize)
	{
		FPropertyAndIndex PropertyAndIndex = FindPropertyAndArrayIndex(InStruct, *InPropertyNames[Index]);

		FPropertyAddress NewAddress;

		if (PropertyAndIndex.ArrayIndex != INDEX_NONE)
		{
			FArrayProperty* ArrayProp = CastFieldChecked<FArrayProperty>(PropertyAndIndex.Property);

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(BasePointer));
			if (bAllowArrayResize)
			{
				ArrayHelper.ExpandForIndex(PropertyAndIndex.ArrayIndex);
			}

			if (ArrayHelper.IsValidIndex(PropertyAndIndex.ArrayIndex))
			{
				FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
				if (InnerStructProp && InPropertyNames.IsValidIndex(Index + 1))
				{
					return FindPropertyRecursive(ArrayHelper.GetRawPtr(PropertyAndIndex.ArrayIndex), InnerStructProp->Struct, InPropertyNames, Index + 1, InOutPropertyChain, bAllowArrayResize);
				}
				else
				{
					NewAddress.Property = ArrayProp->Inner;
					NewAddress.Address = ArrayHelper.GetRawPtr(PropertyAndIndex.ArrayIndex);

					InOutPropertyChain.Add(NewAddress.Property);
				}
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(PropertyAndIndex.Property))
		{
			NewAddress.Property = StructProp;
			NewAddress.Address = BasePointer;

			InOutPropertyChain.Add(NewAddress.Property);
			if (InPropertyNames.IsValidIndex(Index + 1))
			{
				void* StructContainer = StructProp->ContainerPtrToValuePtr<void>(BasePointer);
				return FindPropertyRecursive(StructContainer, StructProp->Struct, InPropertyNames, Index + 1, InOutPropertyChain, bAllowArrayResize);
			}
			else
			{
				check(StructProp->GetName() == InPropertyNames[Index]);
			}
		}
		else if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(PropertyAndIndex.Property))
		{
			NewAddress.Property = ObjectProp;
			NewAddress.Address = BasePointer;

			InOutPropertyChain.Add(NewAddress.Property);
			if (InPropertyNames.IsValidIndex(Index + 1))
			{
				void* ObjectContainer = ObjectProp->ContainerPtrToValuePtr<void>(BasePointer);
				UObject* Object = ObjectProp->GetObjectPropertyValue(ObjectContainer);
				if (Object)
				{
					return FindPropertyRecursive(Object, Object->GetClass(), InPropertyNames, Index + 1, InOutPropertyChain, bAllowArrayResize);
				}
			}
			else
			{
				check(ObjectProp->GetName() == InPropertyNames[Index]);
			}

		}
		else if (PropertyAndIndex.Property)
		{
			NewAddress.Property = PropertyAndIndex.Property;
			NewAddress.Address = BasePointer;

			InOutPropertyChain.Add(NewAddress.Property);
		}

		return NewAddress;
	}

	FPropertyAddress FindProperty(void* BasePointer, UStruct* InStruct, const FString& InPropertyPath, TArray<FProperty*>& InOutPropertyChain, bool bAllowArrayResize)
	{
		TArray<FString> PropertyNames;

		InPropertyPath.ParseIntoArray(PropertyNames, TEXT("."), true);

		if (PropertyNames.Num() > 0)
		{
			return FindPropertyRecursive(BasePointer, InStruct, PropertyNames, 0, InOutPropertyChain, bAllowArrayResize);
		}
		else
		{
			return FPropertyAddress();
		}
	}

}
