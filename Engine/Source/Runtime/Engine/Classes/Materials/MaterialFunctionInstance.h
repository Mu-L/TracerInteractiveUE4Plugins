// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "StaticParameterSet.h"
#include "MaterialFunctionInstance.generated.h"

/**
 * A material function instance defines parameter overrides for a parent material function.
 */
UCLASS(hidecategories=object, MinimalAPI)
class UMaterialFunctionInstance : public UMaterialFunctionInterface
{
	GENERATED_UCLASS_BODY()

	void SetParent(UMaterialFunctionInterface* NewParent)
	{
		Parent = NewParent;
		MaterialFunctionUsage = NewParent->GetMaterialFunctionUsage();
		Base = GetBaseFunction();
	}

	virtual EMaterialFunctionUsage GetMaterialFunctionUsage() override
	{
		UMaterialFunctionInterface* BaseFunction = GetBaseFunction();
		return BaseFunction ? BaseFunction->GetMaterialFunctionUsage() : EMaterialFunctionUsage::Default;
	}

	/** Parent function. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialFunctionInstance, AssetRegistrySearchable)
	UMaterialFunctionInterface* Parent;

	/** Base function. */
	UPROPERTY(AssetRegistrySearchable)
	UMaterialFunctionInterface* Base;

	/** Scalar parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FScalarParameterValue> ScalarParameterValues;

	/** Vector parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FVectorParameterValue> VectorParameterValues;

	/** Texture parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FTextureParameterValue> TextureParameterValues;

	/** Font parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FFontParameterValue> FontParameterValues;

	/** Static switch parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FStaticSwitchParameter> StaticSwitchParameterValues;

	/** Static component mask parameters. */
	UPROPERTY(EditAnywhere, Category=MaterialFunctionInstance)
	TArray<struct FStaticComponentMaskParameter> StaticComponentMaskParameterValues;

	/** Runtime virtual texture parameters. */
	UPROPERTY(EditAnywhere, Category = MaterialFunctionInstance)
	TArray<struct FRuntimeVirtualTextureParameterValue> RuntimeVirtualTextureParameterValues;

#if WITH_EDITOR
	ENGINE_API void UpdateParameterSet();
	ENGINE_API void OverrideMaterialInstanceParameterValues(class UMaterialInstance* Instance);
#endif // WITH_EDITOR

	//~ Begin UMaterialFunctionInterface interface
	virtual void UpdateFromFunctionResource() override;
	virtual void GetInputsAndOutputs(TArray<struct FFunctionExpressionInput>& OutInputs, TArray<struct FFunctionExpressionOutput>& OutOutputs) const override;
	virtual bool ValidateFunctionUsage(class FMaterialCompiler* Compiler, const FFunctionExpressionOutput& Output) override;

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, const struct FFunctionExpressionOutput& Output) override;
	virtual void LinkIntoCaller(const TArray<FFunctionExpressionInput>& CallerInputs) override;
	virtual void UnlinkFromCaller() override;
#endif

	virtual bool IsDependent(UMaterialFunctionInterface* OtherFunction) override;

#if WITH_EDITORONLY_DATA
	ENGINE_API virtual bool IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const override;
	ENGINE_API virtual void GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const override;
#endif

#if WITH_EDITOR
	virtual UMaterialInterface* GetPreviewMaterial() override;
	virtual void UpdateInputOutputTypes() override;
	virtual bool HasFlippedCoordinates() const override;
#endif

	virtual UMaterialFunctionInterface* GetBaseFunction() override
	{
		UMaterialFunctionInterface* BasePtr = nullptr;
		UMaterialFunctionInterface* BaseParent = Parent;

		while (true)
		{
			BasePtr = BaseParent;
			if (UMaterialFunctionInstance* BaseInstance = Cast<UMaterialFunctionInstance>(BasePtr))
			{
				BaseParent = BaseInstance->Parent;
			}
			else
			{
				break;
			}
		}

		return BasePtr;
	}

	virtual const UMaterialFunctionInterface* GetBaseFunction() const override
	{
		UMaterialFunctionInterface* BasePtr = nullptr;
		UMaterialFunctionInterface* BaseParent = Parent;

		while (true)
		{
			BasePtr = BaseParent;
			if (UMaterialFunctionInstance* BaseInstance = Cast<UMaterialFunctionInstance>(BasePtr))
			{
				BaseParent = BaseInstance->Parent;
			}
			else
			{
				break;
			}
		}

		return BasePtr;
	}

#if WITH_EDITORONLY_DATA
	virtual const TArray<UMaterialExpression*>* GetFunctionExpressions() const override
	{
		const UMaterialFunctionInterface* BaseFunction = GetBaseFunction();
		return BaseFunction ? BaseFunction->GetFunctionExpressions() : nullptr;
	}
#endif // WITH_EDITORONLY_DATA

	virtual const FString* GetDescription() const override
	{
		const UMaterialFunctionInterface* BaseFunction = GetBaseFunction();
		return BaseFunction ? BaseFunction->GetDescription() : nullptr;
	}

#if WITH_EDITOR
	virtual bool GetReentrantFlag() const override
	{
		const UMaterialFunctionInterface* BaseFunction = GetBaseFunction();
		return BaseFunction ? BaseFunction->GetReentrantFlag() : false;
	}

	virtual void SetReentrantFlag(const bool bIsReentrant) override
	{
		if (UMaterialFunctionInterface* BaseFunction = GetBaseFunction())
		{
			BaseFunction->SetReentrantFlag(bIsReentrant);
		}
	}
#endif // WITH_EDITOR

public:
	virtual bool OverrideNamedScalarParameter(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue) override;
	virtual bool OverrideNamedVectorParameter(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) override;
	virtual bool OverrideNamedTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, class UTexture*& OutValue) override;
	virtual bool OverrideNamedRuntimeVirtualTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture*& OutValue) override;
	virtual bool OverrideNamedFontParameter(const FHashedMaterialParameterInfo& ParameterInfo, class UFont*& OutFontValue, int32& OutFontPage) override;
	virtual bool OverrideNamedStaticSwitchParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid) override;
	virtual bool OverrideNamedStaticComponentMaskParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid) override;
	//~ End UMaterialFunctionInterface interface

protected:
#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	class UMaterialInstanceConstant* PreviewMaterial;
#endif // WITH_EDITORONLY_DATA
};
