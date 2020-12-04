// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Set.h"
#include "Containers/UnrealString.h"

struct FSoftObjectPath;
class UMaterial;

class DATASMITHTRANSLATOR_API FDatasmithMasterMaterial
{
public:
	FDatasmithMasterMaterial();
	explicit FDatasmithMasterMaterial( UMaterial* InMaterial );

	void FromMaterial( UMaterial* InMaterial );
	void FromSoftObjectPath( const FSoftObjectPath& InObjectPath );

	UMaterial* GetMaterial() const { return Material; }

	bool IsValid() const { return Material != nullptr; }

	TSet< FString > VectorParams;
	TSet< FString > ScalarParams;
	TSet< FString > TextureParams;
#if WITH_EDITORONLY_DATA
	TSet< FString > BoolParams;
#endif	

private:
	UMaterial* Material;

};
