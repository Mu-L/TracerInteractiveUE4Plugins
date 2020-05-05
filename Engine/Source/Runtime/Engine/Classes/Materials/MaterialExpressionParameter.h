// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionParameter.generated.h"

struct FMaterialParameterInfo;

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionParameter : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** The name of the parameter */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionParameter)
	FName ParameterName;

	/** GUID that should be unique within the material, this is used for parameter renaming. */
	UPROPERTY()
	FGuid ExpressionGUID;

#if WITH_EDITORONLY_DATA
	/** The name of the parameter Group to display in MaterialInstance Editor. Default is None group */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionParameter)
	FName Group;

	/** Controls where the this parameter is displayed in a material instance parameter list.  The lower the number the higher up in the parameter list. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionParameter)
	int32 SortPriority;
#endif
	static FName ParameterDefaultName;


#if WITH_EDITOR

	//~ Begin UMaterialExpression Interface
	virtual bool MatchesSearchQuery( const TCHAR* SearchQuery ) override;
	virtual bool CanRenameNode() const override { return true; }
	virtual FString GetEditableName() const override;
	virtual void SetEditableName(const FString& NewName) override;

	virtual bool HasAParameterName() const override { return true; }
	virtual FName GetParameterName() const override { return ParameterName; }
	virtual void SetParameterName(const FName& Name) override { ParameterName = Name; }
	virtual void ValidateParameterName(const bool bAllowDuplicateName) override;
#endif
	//~ End UMaterialExpression Interface

	ENGINE_API virtual FGuid& GetParameterExpressionId() override
	{
		return ExpressionGUID;
	}

	/**
	 * Get list of parameter names for static parameter sets
	 */
	virtual void GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const;

};
