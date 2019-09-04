// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectTemplates/DatasmithSceneComponentTemplate.h"

#include "DatasmithStaticMeshComponentTemplate.generated.h"

UCLASS()
class DATASMITHCONTENT_API UDatasmithStaticMeshComponentTemplate : public UDatasmithObjectTemplate
{
	GENERATED_BODY()

public:

	UPROPERTY()
	class UStaticMesh* StaticMesh;

	UPROPERTY()
	TArray< class UMaterialInterface* > OverrideMaterials;

	virtual UObject* UpdateObject( UObject* Destination, bool bForce = false ) override;
	virtual void Load( const UObject* Source ) override;
	virtual bool Equals( const UDatasmithObjectTemplate* Other ) const override;
};
