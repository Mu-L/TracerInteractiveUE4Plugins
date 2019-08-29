// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Curves/RichCurve.h"
#include "Curves/CurveBase.h"
#include "CurveLinearColor.generated.h"

class UCurveLinearColor;
// Delegate called right before rendering each slot. This allows the gradient to be modified dynamically per slot.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUpdateGradient, UCurveLinearColor* /*GradientAtlas*/);

USTRUCT()
struct ENGINE_API FRuntimeCurveLinearColor
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY()
	FRichCurve ColorCurves[4];

	UPROPERTY(EditAnywhere, Category = RuntimeFloatCurve)
	class UCurveLinearColor* ExternalCurve;

	FLinearColor GetLinearColorValue(float InTime) const;
};

UCLASS(BlueprintType, collapsecategories, hidecategories = (FilePath))
class ENGINE_API UCurveLinearColor : public UCurveBase
{
	GENERATED_UCLASS_BODY()

	/** Keyframe data, one curve for red, green, blue, and alpha */
	UPROPERTY()
	FRichCurve FloatCurves[4];

	// Begin FCurveOwnerInterface
	virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
	virtual TArray<FRichCurveEditInfo> GetCurves() override;
	virtual bool IsLinearColorCurve() const override { return true; }

	UFUNCTION(BlueprintCallable, Category="Math|Curves")
	virtual FLinearColor GetLinearColorValue(float InTime) const override;

	FLinearColor GetUnadjustedLinearColorValue(float InTime) const;

	bool HasAnyAlphaKeys() const override { return FloatCurves[3].GetNumKeys() > 0; }

	virtual bool IsValidCurve( FRichCurveEditInfo CurveInfo ) override;
	// End FCurveOwnerInterface

	/** Determine if Curve is the same */
	bool operator == (const UCurveLinearColor& Curve) const;

public:
#if WITH_EDITOR

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	void DrawThumbnail(class FCanvas* Canvas, FVector2D StartXY, FVector2D SizeXY);

	void PushToSourceData(TArray<FColor> &SrcData, int32 StartXY, FVector2D SizeXY);

	virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
#endif
public:
	// Properties for adjusting the color of the gradient
	UPROPERTY(EditAnywhere, Category="Color", meta = (ClampMin = "0.0", ClampMax = "359.0"))
	float AdjustHue;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustSaturation;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustBrightness;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustBrightnessCurve;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustVibrance;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustMinAlpha;

	UPROPERTY(EditAnywhere, Category = "Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustMaxAlpha;

#if WITH_EDITORONLY_DATA
	FOnUpdateGradient OnUpdateGradient;
#endif
protected:
	void WritePixel(uint8* Pixel, const FLinearColor& Color);
};

