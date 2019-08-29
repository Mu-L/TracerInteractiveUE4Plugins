// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MobileJSStructDeserializerBackend.h"

#if	PLATFORM_ANDROID || PLATFORM_IOS

#include "MobileJSScripting.h"
#include "UObject/UnrealType.h"
#include "Templates/Casts.h"

namespace
{
	// @todo: this function is copied from CEFJSStructDeserializerBackend.cpp. Move shared utility code to a common header file
	/**
	 * Sets the value of the given property.
	 *
	 * @param Property The property to set.
	 * @param Outer The property that contains the property to be set, if any.
	 * @param Data A pointer to the memory holding the property's data.
	 * @param ArrayIndex The index of the element to set (if the property is an array).
	 * @return true on success, false otherwise.
	 * @see ClearPropertyValue
	 */
	template<typename UPropertyType, typename PropertyType>
	bool SetPropertyValue( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, const PropertyType& Value )
	{
		PropertyType* ValuePtr = nullptr;
		UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Outer);

		if (ArrayProperty != nullptr)
		{
			if (ArrayProperty->Inner != Property)
			{
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->template ContainerPtrToValuePtr<void>(Data));
			int32 Index = ArrayHelper.AddValue();

			ValuePtr = (PropertyType*)ArrayHelper.GetRawPtr(Index);
		}
		else
		{
			UPropertyType* TypedProperty = Cast<UPropertyType>(Property);

			if (TypedProperty == nullptr || ArrayIndex >= TypedProperty->ArrayDim)
			{
				return false;
			}

			ValuePtr = TypedProperty->template ContainerPtrToValuePtr<PropertyType>(Data, ArrayIndex);
		}

		if (ValuePtr == nullptr)
		{
			return false;
		}

		*ValuePtr = Value;

		return true;
	}
}

bool FMobileJSStructDeserializerBackend::ReadProperty( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex )
{
	switch (GetLastNotation())
	{
		case EJsonNotation::String:
		{
			if (Property->IsA<UStructProperty>())
			{
				UStructProperty* StructProperty = Cast<UStructProperty>(Property);

				if ( StructProperty->Struct == FWebJSFunction::StaticStruct())
				{

					FGuid CallbackID;
					if (!FGuid::Parse(GetReader()->GetValueAsString(), CallbackID))
					{
						return false;
					}

					FWebJSFunction CallbackObject(Scripting, CallbackID);
					return SetPropertyValue<UStructProperty, FWebJSFunction>(Property, Outer, Data, ArrayIndex, CallbackObject);
				}
			}
		}
		break;
	}

	// If we reach this, default to parent class behavior
	return FJsonStructDeserializerBackend::ReadProperty(Property, Outer, Data, ArrayIndex);
}

FMobileJSStructDeserializerBackend::FMobileJSStructDeserializerBackend(FMobileJSScriptingRef InScripting, const FString& JsonString)
	: FJsonStructDeserializerBackend(Reader)
	, Scripting(InScripting)
	, JsonData()
	, Reader(JsonData)
{
	auto Convert = StringCast<UCS2CHAR>(*JsonString);
	JsonData.Append((uint8*)Convert.Get(), JsonString.Len() * sizeof(UCS2CHAR));
}

#endif // PLATFORM_ANDROID || PLATFORM_IOS