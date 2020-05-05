// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraVariant.h"
#include "NiagaraDataInterface.h"

FNiagaraVariant::FNiagaraVariant()
{
	CurrentMode = ENiagaraVariantMode::None;
	Object = nullptr;
	DataInterface = nullptr;
}

FNiagaraVariant::FNiagaraVariant(const FNiagaraVariant& Other)
{
	CurrentMode = Other.CurrentMode;
	Object = Other.Object;
	DataInterface = Other.DataInterface;
	Bytes = Other.Bytes;
}

FNiagaraVariant::FNiagaraVariant(UObject* InObject)
{
	CurrentMode = ENiagaraVariantMode::Object;
	Object = InObject;
	DataInterface = nullptr;
}

FNiagaraVariant::FNiagaraVariant(UNiagaraDataInterface* InDataInterface)
{
	CurrentMode = ENiagaraVariantMode::DataInterface;
	DataInterface = InDataInterface;
	Object = nullptr;
}

FNiagaraVariant::FNiagaraVariant(const TArray<uint8>& InBytes)
{
	CurrentMode = ENiagaraVariantMode::Bytes;
	Bytes = InBytes;
	DataInterface = nullptr;
	Object = nullptr;
}

FNiagaraVariant::FNiagaraVariant(const void* InBytes, int32 Size)
{
	CurrentMode = ENiagaraVariantMode::Bytes;
	Bytes.Append((const uint8*) InBytes, Size);
	DataInterface = nullptr;
	Object = nullptr;
}

UObject* FNiagaraVariant::GetUObject() const 
{
	ensure(CurrentMode == ENiagaraVariantMode::Object);
	return Object;
}

void FNiagaraVariant::SetUObject(UObject* InObject)
{
	ensure(CurrentMode == ENiagaraVariantMode::None || CurrentMode == ENiagaraVariantMode::Object);

	CurrentMode = ENiagaraVariantMode::Object;
	Object = InObject;
}

UNiagaraDataInterface* FNiagaraVariant::GetDataInterface() const 
{
	ensure(CurrentMode == ENiagaraVariantMode::DataInterface);
	return DataInterface;
}

void FNiagaraVariant::SetDataInterface(UNiagaraDataInterface* InDataInterface)
{
	ensure(CurrentMode == ENiagaraVariantMode::None || CurrentMode == ENiagaraVariantMode::DataInterface);
		
	CurrentMode = ENiagaraVariantMode::DataInterface;
	DataInterface = InDataInterface;
}

void FNiagaraVariant::SetBytes(uint8* InBytes, int32 InCount)
{
	check(InCount > 0);
	ensure(CurrentMode == ENiagaraVariantMode::None || CurrentMode == ENiagaraVariantMode::Bytes);

	CurrentMode = ENiagaraVariantMode::Bytes;
	Bytes.Reset(InCount);
	Bytes.Append(InBytes, InCount);
}

uint8* FNiagaraVariant::GetBytes() const
{
	ensure(CurrentMode == ENiagaraVariantMode::Bytes);
	return (uint8*) Bytes.GetData();
}

bool FNiagaraVariant::operator==(const FNiagaraVariant& Other) const
{
	if (CurrentMode == Other.CurrentMode)
	{
		switch (CurrentMode)
		{
			case ENiagaraVariantMode::Bytes:
				return Bytes == Other.Bytes;

			case ENiagaraVariantMode::Object:
				return Object == Other.Object;

			case ENiagaraVariantMode::DataInterface:
				return GetDataInterface()->Equals(Other.GetDataInterface());

			case ENiagaraVariantMode::None:
				return true;
		}
	}

	return false;
}

bool FNiagaraVariant::operator!=(const FNiagaraVariant& Other) const
{
	return !(operator==(Other));
}
