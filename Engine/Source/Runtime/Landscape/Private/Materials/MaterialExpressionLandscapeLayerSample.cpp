// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "EngineGlobals.h"
#include "MaterialCompiler.h"
#include "Materials/Material.h"

#define LOCTEXT_NAMESPACE "Landscape"


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionLandscapeLayerSample
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionLandscapeLayerSample::UMaterialExpressionLandscapeLayerSample(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Landscape;
		FConstructorStatics()
			: NAME_Landscape(LOCTEXT("Landscape", "Landscape"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Landscape);
#endif
}


FGuid& UMaterialExpressionLandscapeLayerSample::GetParameterExpressionId()
{
	return ExpressionGUID;
}

#if WITH_EDITOR
int32 UMaterialExpressionLandscapeLayerSample::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const int32 WeightCode = Compiler->StaticTerrainLayerWeight(ParameterName, Compiler->Constant(PreviewWeight));
	if (WeightCode == INDEX_NONE)
	{
		// layer is not used in this component, sample value is 0.
		return Compiler->Constant(0.f);
	}
	else
	{
		return WeightCode;
	}
}
#endif // WITH_EDITOR

UObject* UMaterialExpressionLandscapeLayerSample::GetReferencedTexture() const
{
	return GEngine->WeightMapPlaceholderTexture;
}

#if WITH_EDITOR
void UMaterialExpressionLandscapeLayerSample::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("Sample '%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionLandscapeLayerSample::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	TArray<FString> Captions;
	GetCaption(Captions);
	for (const FString& Caption : Captions)
	{
		if (Caption.Contains(SearchQuery))
		{
			return true;
		}
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

#endif // WITH_EDITOR

void UMaterialExpressionLandscapeLayerSample::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);
	OutParameterInfo.AddUnique(NewParameter);

	if (CurrentSize != OutParameterInfo.Num())
	{
		OutParameterIds.Add(ExpressionGUID);
	}
}

#undef LOCTEXT_NAMESPACE
