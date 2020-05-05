// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialLayersFunctions.h"
#include "MaterialExpressionMaterialAttributeLayers.generated.h"

class FMaterialCompiler;
class UMaterialFunctionInterface;
class UMaterialExpressionMaterialFunctionCall;
struct FMaterialParameterInfo;

UCLASS(hidecategories=Object, MinimalAPI)
class UMaterialExpressionMaterialAttributeLayers : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** name to be referenced when we want to find and set this parameter */
	UPROPERTY(EditAnywhere, Category=LayersParameter)
	FName ParameterName;

	/** GUID that should be unique within the material, this is used for parameter renaming. */
	UPROPERTY()
	FGuid ExpressionGUID;

	UPROPERTY()
	FMaterialAttributesInput Input;

	UPROPERTY(EditAnywhere, Category=Layers)
	FMaterialLayersFunctions DefaultLayers;

	const TArray<UMaterialFunctionInterface*>& GetLayers() const
	{
		return ParamLayers ? ParamLayers->Layers : DefaultLayers.Layers;
	}

	const TArray<UMaterialFunctionInterface*>& GetBlends() const
	{
		return ParamLayers ? ParamLayers->Blends : DefaultLayers.Blends;
	}

#if WITH_EDITOR
	const TArray<FText>& GetLayerNames() const
	{
		return ParamLayers ? ParamLayers->LayerNames : DefaultLayers.LayerNames;
	}

	const TArray<bool>& GetShouldFilterLayers() const
	{
		return ParamLayers ? ParamLayers->RestrictToLayerRelatives : DefaultLayers.RestrictToLayerRelatives;
	}

	const TArray<bool>& GetShouldFilterBlends() const
	{
		return ParamLayers ? ParamLayers->RestrictToBlendRelatives : DefaultLayers.RestrictToBlendRelatives;
	}

	const TArray<FGuid>& GetLayerGuids() const
	{
		return ParamLayers ? ParamLayers->LayerGuids : DefaultLayers.LayerGuids;
	}

	const TArray<FGuid>& GetParentLayerGuids() const
	{
		return ParamLayers ? ParamLayers->ParentLayerGuids : DefaultLayers.ParentLayerGuids;
	}
#endif // WITH_EDITOR

	const TArray<bool>& GetLayerStates() const
	{
		return ParamLayers ? ParamLayers->LayerStates : DefaultLayers.LayerStates;
	}

	UPROPERTY(Transient)
	TArray<UMaterialExpressionMaterialFunctionCall*> LayerCallers;

	UPROPERTY(Transient)
	int32 NumActiveLayerCallers;

	UPROPERTY(Transient)
	TArray<UMaterialExpressionMaterialFunctionCall*> BlendCallers;

	UPROPERTY(Transient)
	int32 NumActiveBlendCallers;

	UPROPERTY(Transient)
	bool bIsLayerGraphBuilt;

	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ Begin UObject Interface

#if WITH_EDITORONLY_DATA
	ENGINE_API void RebuildLayerGraph(bool bReportErrors);
	ENGINE_API void OverrideLayerGraph(const FMaterialLayersFunctions* OverrideLayers);
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	bool ValidateLayerConfiguration(FMaterialCompiler* Compiler, bool bReportErrors);
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	bool IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const;
	void GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const;
#endif

	UMaterialFunctionInterface* GetParameterAssociatedFunction(const FHashedMaterialParameterInfo& ParameterInfo) const;
#if 0
	void GetParameterAssociatedFunctions(const FMaterialParameterInfo& ParameterInfo, TArray<UMaterialFunctionInterface*>& AssociatedFunctions) const;
#endif

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
	virtual const TArray<FExpressionInput*> GetInputs()override;
	virtual FExpressionInput* GetInput(int32 InputIndex)override;
	virtual FName GetInputName(int32 InputIndex) const override;
	virtual bool IsInputConnectionRequired(int32 InputIndex) const override {return false;}
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultMaterialAttributes(int32 OutputIndex) override {return true;}

	virtual bool MatchesSearchQuery( const TCHAR* SearchQuery ) override;
	virtual bool CanRenameNode() const override { return true; }
	virtual FString GetEditableName() const override;
	virtual void SetEditableName(const FString& NewName) override;

	virtual bool HasAParameterName() const override { return true; }
	virtual FName GetParameterName() const override { return ParameterName; }
	virtual void SetParameterName(const FName& Name) override { ParameterName = Name; }
#endif
	//~ End UMaterialExpression Interface
	
	/** Return whether this is the named parameter, and fill in its value */
	bool IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, FMaterialLayersFunctions& OutLayers, FGuid& OutExpressionGuid) const;
	
	ENGINE_API virtual FGuid& GetParameterExpressionId() override
	{
		return ExpressionGUID;
	}

	void GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const;

private:
	/** Internal pointer to parameter-driven layer graph */
	const FMaterialLayersFunctions* ParamLayers;
};
