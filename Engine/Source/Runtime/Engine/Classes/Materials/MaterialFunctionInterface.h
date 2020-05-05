// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/Casts.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "StaticParameterSet.h"
#include "MaterialFunctionInterface.generated.h"

class UMaterial;
class UTexture;
struct FPropertyChangedEvent;

/** Usage set on a material function determines feature compatibility and validation. */
UENUM()
enum class EMaterialFunctionUsage : uint8
{
	Default,
	MaterialLayer,
	MaterialLayerBlend
};

/**
 * A Material Function is a collection of material expressions that can be reused in different materials
 */
UCLASS(hidecategories=object, MinimalAPI)
class UMaterialFunctionInterface : public UObject
{
	GENERATED_UCLASS_BODY()

	//~ Begin UObject Interface.
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	//~ End UObject Interface.

	/** Used by materials using this function to know when to recompile. */
	UPROPERTY(duplicatetransient)
	FGuid StateId;

protected:
	/** The intended usage of this function, required for material layers. */
	UPROPERTY(AssetRegistrySearchable)
	EMaterialFunctionUsage MaterialFunctionUsage;

public:
	virtual EMaterialFunctionUsage GetMaterialFunctionUsage()
		PURE_VIRTUAL(UMaterialFunctionInterface::GetMaterialFunctionUsage,return EMaterialFunctionUsage::Default;);

	virtual void UpdateFromFunctionResource()
		PURE_VIRTUAL(UMaterialFunctionInterface::UpdateFromFunctionResource,);

	virtual void GetInputsAndOutputs(TArray<struct FFunctionExpressionInput>& OutInputs, TArray<struct FFunctionExpressionOutput>& OutOutputs) const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetInputsAndOutputs,);

	virtual bool ValidateFunctionUsage(class FMaterialCompiler* Compiler, const FFunctionExpressionOutput& Output)
		PURE_VIRTUAL(UMaterialFunctionInterface::ValidateFunctionUsage,return false;);

#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, const struct FFunctionExpressionOutput& Output)
		PURE_VIRTUAL(UMaterialFunctionInterface::Compile,return INDEX_NONE;);

	virtual void LinkIntoCaller(const TArray<FFunctionExpressionInput>& CallerInputs)
		PURE_VIRTUAL(UMaterialFunctionInterface::LinkIntoCaller,);

	virtual void UnlinkFromCaller()
		PURE_VIRTUAL(UMaterialFunctionInterface::UnlinkFromCaller,);
#endif

	/** @return true if this function is dependent on the passed in function, directly or indirectly. */
	ENGINE_API virtual bool IsDependent(UMaterialFunctionInterface* OtherFunction)
		PURE_VIRTUAL(UMaterialFunctionInterface::IsDependent,return false;);

#if WITH_EDITORONLY_DATA
	/**
	 * Iterates all functions that this function is dependent on, directly or indrectly.
	 *
	 * @param Predicate a visitor predicate returning true to continue iteration, false to break
	 *
	 * @return true if all dependent functions were visited, false if the Predicate did break iteration
	 */
	ENGINE_API virtual bool IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
		PURE_VIRTUAL(UMaterialFunctionInterface::IterateDependentFunctions,return false;);

	/** Returns an array of the functions that this function is dependent on, directly or indirectly. */
	ENGINE_API virtual void GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetDependentFunctions,);
#endif

#if WITH_EDITOR
	ENGINE_API virtual UMaterialInterface* GetPreviewMaterial()
		PURE_VIRTUAL(UMaterialFunctionInterface::GetPreviewMaterial,return nullptr;);

	virtual void UpdateInputOutputTypes()
		PURE_VIRTUAL(UMaterialFunctionInterface::UpdateInputOutputTypes,);

	/** Checks whether a Material Function is arranged in the old style, with inputs flowing from right to left */
	virtual bool HasFlippedCoordinates() const
		PURE_VIRTUAL(UMaterialFunctionInterface::HasFlippedCoordinates,return false;);
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable)
	uint32 CombinedInputTypes;

	UPROPERTY(AssetRegistrySearchable)
	uint32 CombinedOutputTypes;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, Category = Thumbnail)
	class UThumbnailInfo* ThumbnailInfo;
#endif
	
	virtual UMaterialFunctionInterface* GetBaseFunction()
		PURE_VIRTUAL(UMaterialFunctionInterface::GetBaseFunction,return nullptr;);

	virtual const UMaterialFunctionInterface* GetBaseFunction() const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetBaseFunction,return nullptr;);

#if WITH_EDITORONLY_DATA
	virtual const TArray<UMaterialExpression*>* GetFunctionExpressions() const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetFunctionExpressions,return nullptr;);
#endif // WITH_EDITORONLY_DATA

	virtual const FString* GetDescription() const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetDescription,return nullptr;);

	virtual bool GetReentrantFlag() const
		PURE_VIRTUAL(UMaterialFunctionInterface::GetReentrantFlag,return false;);

	virtual void SetReentrantFlag(const bool bIsReentrant)
		PURE_VIRTUAL(UMaterialFunctionInterface::SetReentrantFlag,);

public:
#if WITH_EDITORONLY_DATA
	/** Finds the names of all matching type parameters */
	template<typename ExpressionType>
	void GetAllParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
	{
		if (const UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			const UClass* TargetClass = UMaterialExpressionMaterialFunctionCall::StaticClass();
			for (UMaterialExpression* Expression : *ParameterFunction->GetFunctionExpressions())
			{
				if (const UMaterialExpressionMaterialFunctionCall* FunctionExpression = (Expression && Expression->IsA(TargetClass)) ? (const UMaterialExpressionMaterialFunctionCall*)Expression : nullptr)
				{
					if (FunctionExpression->MaterialFunction)
					{
						FunctionExpression->MaterialFunction->GetAllParameterInfo<const ExpressionType>(OutParameterInfo, OutParameterIds, InBaseParameterInfo);
					}
				}
				else if (const ExpressionType* ParameterExpression = Cast<const ExpressionType>(Expression))
				{
					ParameterExpression->GetAllParameterInfo(OutParameterInfo, OutParameterIds, InBaseParameterInfo);
				}
			}

			check(OutParameterInfo.Num() == OutParameterIds.Num());
		}
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/** Finds the first matching parameter by name and type */
	template<typename ExpressionType>
	bool GetNamedParameterOfType(const FHashedMaterialParameterInfo& ParameterInfo, ExpressionType*& Parameter, UMaterialFunctionInterface** OwningFunction = nullptr)
	{
		Parameter = nullptr;

		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			const UClass* TargetClass = ExpressionType::StaticClass();

			auto GetExpressionParameterByNamePredicate = 
				[&ParameterInfo, &Parameter, &OwningFunction, TargetClass](UMaterialFunctionInterface* Function) -> bool
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (ExpressionType* ExpressionParameter = (FunctionExpression && FunctionExpression->IsA(TargetClass)) ? (ExpressionType *)FunctionExpression : nullptr)
					{
						if (ExpressionParameter->ParameterName == ParameterInfo.Name)
						{
							Parameter = ExpressionParameter;

							if (OwningFunction)
							{
								(*OwningFunction) = Function;
							}

							return false; // found, stop iterating
						}
					}
				}

				return true; // not found, continue iterating
			};
			
			if (!ParameterFunction->IterateDependentFunctions(GetExpressionParameterByNamePredicate))
			{
				return true;
			}
			return !GetExpressionParameterByNamePredicate(ParameterFunction);
		}

		return false;
	}

	/** Finds the first matching parameter's group name */
	bool GetParameterGroupName(const FHashedMaterialParameterInfo& ParameterInfo, FName& OutGroup)
	{
		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (const UMaterialExpressionParameter* Parameter = Cast<const UMaterialExpressionParameter>(FunctionExpression))
					{
						if (Parameter->ParameterName == ParameterInfo.Name)
						{
							OutGroup = Parameter->Group;
							return true;
						}
					}
					else if (const UMaterialExpressionTextureSampleParameter* TexParameter = Cast<const UMaterialExpressionTextureSampleParameter>(FunctionExpression))
					{
						if (TexParameter->ParameterName == ParameterInfo.Name)
						{
							OutGroup = TexParameter->Group;
							return true;
						}
					}
					else if (const UMaterialExpressionFontSampleParameter* FontParameter = Cast<const UMaterialExpressionFontSampleParameter>(FunctionExpression))
					{
						if (FontParameter->ParameterName == ParameterInfo.Name)
						{
							OutGroup = FontParameter->Group;
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	/** Finds the first matching parameter's group name */
	bool GetParameterSortPriority(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutSortPriority)
	{
		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (const UMaterialExpressionParameter* Parameter = Cast<const UMaterialExpressionParameter>(FunctionExpression))
					{
						if (Parameter->ParameterName == ParameterInfo.Name)
						{
							OutSortPriority = Parameter->SortPriority;
							return true;
						}
					}
					else if (const UMaterialExpressionTextureSampleParameter* TexParameter = Cast<const UMaterialExpressionTextureSampleParameter>(FunctionExpression))
					{
						if (TexParameter->ParameterName == ParameterInfo.Name)
						{
							OutSortPriority = TexParameter->SortPriority;
							return true;
						}
					}
					else if (const UMaterialExpressionFontSampleParameter* FontParameter = Cast<const UMaterialExpressionFontSampleParameter>(FunctionExpression))
					{
						if (FontParameter->ParameterName == ParameterInfo.Name)
						{
							OutSortPriority = FontParameter->SortPriority;
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	/** Finds the first matching parameter's description */
	bool GetParameterDesc(const FHashedMaterialParameterInfo& ParameterInfo, FString& OutDesc)
	{
		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (const UMaterialExpressionParameter* Parameter = Cast<const UMaterialExpressionParameter>(FunctionExpression))
					{
						if (Parameter->ParameterName == ParameterInfo.Name)
						{
							OutDesc = Parameter->Desc;
							return true;
						}
					}
					else if (const UMaterialExpressionTextureSampleParameter* TexParameter = Cast<const UMaterialExpressionTextureSampleParameter>(FunctionExpression))
					{
						if (TexParameter->ParameterName == ParameterInfo.Name)
						{
							OutDesc = TexParameter->Desc;
							return true;
						}
					}
					else if (const UMaterialExpressionFontSampleParameter* FontParameter = Cast<const UMaterialExpressionFontSampleParameter>(FunctionExpression))
					{
						if (FontParameter->ParameterName == ParameterInfo.Name)
						{
							OutDesc = FontParameter->Desc;
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	/** Returns if any of the matching parameters have changed */
	template <typename ParameterType, typename ExpressionType>
	bool UpdateParameterSet(ParameterType& Parameter)
	{
		bool bChanged = false;

		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (ExpressionType* ParameterExpression = Cast<ExpressionType>(FunctionExpression))
					{
						if (ParameterExpression->ParameterName == Parameter.ParameterInfo.Name)
						{
							Parameter.ExpressionGUID = ParameterExpression->ExpressionGUID;
							bChanged = true;
							break;
						}
					}
				}
			}
		}

		return bChanged;
	}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/** Get all expressions of the requested type, recursing through any function expressions in the function */
	template<typename ExpressionType>
	bool HasAnyExpressionsOfType()
	{
		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (ExpressionType* FunctionExpressionOfType = Cast<ExpressionType>(FunctionExpression))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	/** Get all expressions of the requested type, recursing through any function expressions in the function */
	template<typename ExpressionType>
	void GetAllExpressionsOfType(TArray<ExpressionType*>& OutExpressions, const bool bRecursive = true)
	{
		if (UMaterialFunctionInterface* ParameterFunction = GetBaseFunction())
		{
			TArray<UMaterialFunctionInterface*> Functions;
			if (bRecursive)
			{
				ParameterFunction->GetDependentFunctions(Functions);
			}
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (ExpressionType* FunctionExpressionOfType = Cast<ExpressionType>(FunctionExpression))
					{
						OutExpressions.Add(FunctionExpressionOfType);
					}
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

	virtual bool OverrideNamedScalarParameter(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue)
	{
		return false;
	}

	virtual bool OverrideNamedVectorParameter(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue)
	{
		return false;
	}

	virtual bool OverrideNamedTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, class UTexture*& OutValue)
	{
		return false;
	}
	
	virtual bool OverrideNamedRuntimeVirtualTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture*& OutValue)
	{
		return false;
	}

	virtual bool OverrideNamedFontParameter(const FHashedMaterialParameterInfo& ParameterInfo, class UFont*& OutFontValue, int32& OutFontPage)
	{
		return false;
	}

	virtual bool OverrideNamedStaticSwitchParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid)
	{
		return false;
	}

	virtual bool OverrideNamedStaticComponentMaskParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid)
	{
		return false;
	}
};
