// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Backends/CborStructSerializerBackend.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/PropertyPortFlags.h"

FCborStructSerializerBackend::FCborStructSerializerBackend(FArchive& InArchive)
	: CborWriter(&InArchive)
	, Flags(EStructSerializerBackendFlags::Legacy)
{}

FCborStructSerializerBackend::FCborStructSerializerBackend(FArchive& InArchive, const EStructSerializerBackendFlags InFlags)
	: CborWriter(&InArchive)
	, Flags(InFlags)
{}

FCborStructSerializerBackend::~FCborStructSerializerBackend() = default;

void FCborStructSerializerBackend::BeginArray(const FStructSerializerState& State)
{
	UObject* Outer = State.ValueProperty->GetOuter();

	// Array nested in Array/Set
	if ((Outer != nullptr) && (Outer->GetClass() == UArrayProperty::StaticClass() || Outer->GetClass() == USetProperty::StaticClass()))
	{
		CborWriter.WriteContainerStart(ECborCode::Array, -1);
	}
	// Array nested in Map
	else if (State.KeyProperty != nullptr)
	{
		FString KeyString;
		State.KeyProperty->ExportTextItem(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
		CborWriter.WriteValue(KeyString);
		CborWriter.WriteContainerStart(ECborCode::Array, -1/*Indefinite*/);
	}
	// Array nested in Object
	else
	{
		CborWriter.WriteValue(State.ValueProperty->GetName());
		CborWriter.WriteContainerStart(ECborCode::Array, -1/*Indefinite*/);
	}
}

void FCborStructSerializerBackend::BeginStructure(const FStructSerializerState& State)
{
	if (State.ValueProperty != nullptr)
	{
		UObject* Outer = State.ValueProperty->GetOuter();

		// Object nested in Array/Set
		if ((Outer != nullptr) && (Outer->GetClass() == UArrayProperty::StaticClass() || Outer->GetClass() == USetProperty::StaticClass()))
		{
			CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);
		}
		// Object nested in Map
		else if (State.KeyProperty != nullptr)
		{
			FString KeyString;
			State.KeyProperty->ExportTextItem(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
			CborWriter.WriteValue(KeyString);
			CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);
		}
		// Object nested in Object
		else
		{
			CborWriter.WriteValue(State.ValueProperty->GetName());
			CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);
		}
	}
	// Root Object
	else
	{
		CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);
	}
}

void FCborStructSerializerBackend::EndArray(const FStructSerializerState& State)
{
	CborWriter.WriteContainerEnd();
}

void FCborStructSerializerBackend::EndStructure(const FStructSerializerState& State)
{
	CborWriter.WriteContainerEnd();
}

void FCborStructSerializerBackend::WriteComment(const FString& Comment)
{
	// Binary format do not support comment
}

namespace CborStructSerializerBackend
{
	// Writes a property value to the serialization output.
	template<typename ValueType>
	void WritePropertyValue(FCborWriter& CborWriter, const FStructSerializerState& State, const ValueType& Value)
	{
		// Value nested in Array/Set or as root
		if ((State.ValueProperty == nullptr) ||
			(State.ValueProperty->ArrayDim > 1) ||
			(State.ValueProperty->GetOuter()->GetClass() == UArrayProperty::StaticClass() || State.ValueProperty->GetOuter()->GetClass() == USetProperty::StaticClass()))
		{
			CborWriter.WriteValue(Value);
		}
		// Value nested in Map
		else if (State.KeyProperty != nullptr)
		{
			FString KeyString;
			State.KeyProperty->ExportTextItem(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
			CborWriter.WriteValue(KeyString);
			CborWriter.WriteValue(Value);
		}
		// Value nested in Object
		else
		{
			CborWriter.WriteValue(State.ValueProperty->GetName());
			CborWriter.WriteValue(Value);
		}
	}

	// Writes a null value to the serialization output.
	void WriteNull(FCborWriter& CborWriter, const FStructSerializerState& State)
	{
		if ((State.ValueProperty == nullptr) ||
			(State.ValueProperty->ArrayDim > 1) ||
			(State.ValueProperty->GetOuter()->GetClass() == UArrayProperty::StaticClass() || State.ValueProperty->GetOuter()->GetClass() == USetProperty::StaticClass()))
		{
			CborWriter.WriteNull();
		}
		else if (State.KeyProperty != nullptr)
		{
			FString KeyString;
			State.KeyProperty->ExportTextItem(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
			CborWriter.WriteValue(KeyString);
			CborWriter.WriteNull();
		}
		else
		{
			CborWriter.WriteValue(State.ValueProperty->GetName());
			CborWriter.WriteNull();
		}
	}
}

void FCborStructSerializerBackend::WriteProperty(const FStructSerializerState& State, int32 ArrayIndex)
{
	using namespace CborStructSerializerBackend;

	// Bool
	if (State.ValueType == UBoolProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, CastChecked<UBoolProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}

	// Unsigned Bytes & Enums
	else if (State.ValueType == UEnumProperty::StaticClass())
	{
		UEnumProperty* EnumProperty = CastChecked<UEnumProperty>(State.ValueProperty);

		WritePropertyValue(CborWriter, State, EnumProperty->GetEnum()->GetNameStringByValue(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(State.ValueData, ArrayIndex))));
	}
	else if (State.ValueType == UByteProperty::StaticClass())
	{
		UByteProperty* ByteProperty = CastChecked<UByteProperty>(State.ValueProperty);

		if (ByteProperty->IsEnum())
		{
			WritePropertyValue(CborWriter, State, ByteProperty->Enum->GetNameStringByValue(ByteProperty->GetPropertyValue_InContainer(State.ValueData, ArrayIndex)));
		}
		else
		{
			WritePropertyValue(CborWriter, State, (int64)ByteProperty->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
		}
	}

	// Double & Float
	else if (State.ValueType == UDoubleProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, CastChecked<UDoubleProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UFloatProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, CastChecked<UFloatProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}

	// Signed Integers
	else if (State.ValueType == UIntProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UIntProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UInt8Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UInt8Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UInt16Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UInt16Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UInt64Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UInt64Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}

	// Unsigned Integers
	else if (State.ValueType == UUInt16Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UUInt16Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UUInt32Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UUInt32Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UUInt64Property::StaticClass())
	{
		WritePropertyValue(CborWriter, State, (int64)CastChecked<UUInt64Property>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}

	// FNames, Strings & Text
	else if (State.ValueType == UNameProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, CastChecked<UNameProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex).ToString());
	}
	else if (State.ValueType == UStrProperty::StaticClass())
	{
		WritePropertyValue(CborWriter, State, CastChecked<UStrProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	else if (State.ValueType == UTextProperty::StaticClass())
	{
		const FText& TextValue = CastChecked<UTextProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		if (EnumHasAnyFlags(Flags, EStructSerializerBackendFlags::WriteTextAsComplexString))
		{
			FString TextValueString;
			FTextStringHelper::WriteToBuffer(TextValueString, TextValue);
			WritePropertyValue(CborWriter, State, TextValueString);
		}
		else
		{
			WritePropertyValue(CborWriter, State, TextValue.ToString());
		}
	}

	// Classes & Objects
	else if (State.ValueType == UClassProperty::StaticClass())
	{
		UObject* const& Value = CastChecked<UClassProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		WritePropertyValue(CborWriter, State, Value ? Value->GetPathName() : FString());
	}
	else if (State.ValueType == USoftClassProperty::StaticClass())
	{
		FSoftObjectPtr const& Value = CastChecked<USoftClassProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		WritePropertyValue(CborWriter, State, Value.IsValid() ? Value->GetPathName() : FString());
	}
	else if (State.ValueType == UObjectProperty::StaticClass())
	{
		UObject* const& Value = CastChecked<UObjectProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		WritePropertyValue(CborWriter, State, Value ? Value->GetPathName() : FString());
	}
	else if (State.ValueType == UWeakObjectProperty::StaticClass())
	{
		FWeakObjectPtr const& Value = CastChecked<UWeakObjectProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		WritePropertyValue(CborWriter, State, Value.IsValid() ? Value.Get()->GetPathName() : FString());
	}
	else if (State.ValueType == USoftObjectProperty::StaticClass())
	{
		FSoftObjectPtr const& Value = CastChecked<USoftObjectProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex);
		WritePropertyValue(CborWriter, State, Value.ToString());
	}

	// Unsupported
	else
	{
		UE_LOG(LogSerialization, Verbose, TEXT("FCborStructSerializerBackend: Property %s cannot be serialized, because its type (%s) is not supported"), *State.ValueProperty->GetFName().ToString(), *State.ValueType->GetFName().ToString());
	}

}
