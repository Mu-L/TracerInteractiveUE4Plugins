// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UCurveLinearColorAtlas.cpp
=============================================================================*/

#include "Curves/CurveLinearColorAtlas.h"
#include "Curves/CurveLinearColor.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"


UCurveLinearColorAtlas::UCurveLinearColorAtlas(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TextureSize = 256;
#if WITH_EDITORONLY_DATA
	bHasAnyDirtyTextures = false;
	bShowDebugColorsForNullGradients = false;
	SizeXY = { (float)TextureSize,  1.0f };
	MipGenSettings = TMGS_NoMipmaps;
#endif
	Filter = TextureFilter::TF_Bilinear;
	SRGB = false;
	AddressX = TA_Clamp;
	AddressY = TA_Clamp;
	CompressionSettings = TC_HDR;
}
#if WITH_EDITOR
void UCurveLinearColorAtlas::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Determine whether any property that requires recompression of the texture, or notification to Materials has changed.
	bool bRequiresNotifyMaterials = false;

	if (PropertyChangedEvent.Property != nullptr)
	{
		const FName PropertyName(PropertyChangedEvent.Property->GetFName());
		// if Resizing
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UCurveLinearColorAtlas, TextureSize))
		{
			if ((uint32)GradientCurves.Num() > TextureSize)
			{
				int32 OldCurveCount = GradientCurves.Num();
				GradientCurves.RemoveAt(TextureSize, OldCurveCount - TextureSize);
			}

			Source.Init(TextureSize, TextureSize, 1, 1, TSF_RGBA16F);

			SizeXY = { (float)TextureSize, 1.0f };
			UpdateTextures();
			bRequiresNotifyMaterials = true;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UCurveLinearColorAtlas, GradientCurves))
		{
			if ((uint32)GradientCurves.Num() > TextureSize)
			{
				int32 OldCurveCount = GradientCurves.Num();
				GradientCurves.RemoveAt(TextureSize, OldCurveCount - TextureSize);
			}
			else
			{
				for (int32 i = 0; i < GradientCurves.Num(); ++i)
				{
					if (GradientCurves[i] != nullptr)
					{
						GradientCurves[i]->OnUpdateCurve.AddUObject(this, &UCurveLinearColorAtlas::OnCurveUpdated);
					}
				}
				UpdateTextures();
				bRequiresNotifyMaterials = true;
			}
		}	
	}

	// Notify any loaded material instances if changed our compression format
	if (bRequiresNotifyMaterials)
	{
		NotifyMaterials();
	}
}
#endif

void UCurveLinearColorAtlas::PostLoad()
{
#if WITH_EDITOR
	if (FApp::CanEverRender())
	{
		FinishCachePlatformData();
	}

	for (int32 i = 0; i < GradientCurves.Num(); ++i)
	{
		if (GradientCurves[i] != nullptr)
		{
			GradientCurves[i]->OnUpdateCurve.AddUObject(this, &UCurveLinearColorAtlas::OnCurveUpdated);
		}
	}
	Source.Init(TextureSize, TextureSize, 1, 1, TSF_RGBA16F);
	SizeXY = { (float)TextureSize, 1.0f };
	UpdateTextures();
#endif

	Super::PostLoad();
}

#if WITH_EDITOR
static void RenderGradient(TArray<FFloat16Color>& InSrcData, UObject* Gradient, int32 StartXY, FVector2D SizeXY)
{
	if (Gradient == nullptr)
	{
		int32 Start = StartXY;
		for (uint32 y = 0; y < SizeXY.Y; y++)
		{
			// Create base mip for the texture we created.
			for (uint32 x = 0; x < SizeXY.X; x++)
			{
				InSrcData[Start + x + y * SizeXY.X] = FLinearColor::White;
			}
		}
	}
	else if (Gradient->IsA(UCurveLinearColor::StaticClass()))
	{
		// Render a gradient
		UCurveLinearColor* GradientCurve = CastChecked<UCurveLinearColor>(Gradient);
		GradientCurve->PushToSourceData(InSrcData, StartXY, SizeXY);
	}
}

static void UpdateTexture(UCurveLinearColorAtlas& Atlas)
{
	const int32 TextureDataSize = Atlas.Source.CalcMipSize(0);

	FGuid MD5Guid;
	FMD5 MD5;
	MD5.Update(reinterpret_cast<const uint8*>(Atlas.SrcData.GetData()), TextureDataSize);
	MD5.Final(reinterpret_cast<uint8*>(&MD5Guid));

	uint32* TextureData = reinterpret_cast<uint32*>(Atlas.Source.LockMip(0));
	FMemory::Memcpy(TextureData, Atlas.SrcData.GetData(), TextureDataSize);
	Atlas.Source.UnlockMip(0);

	Atlas.Source.SetId(MD5Guid, /*bInGuidIsHash*/ true);
	Atlas.UpdateResource();
}

// Immediately render a new material to the specified slot index (SlotIndex must be within this section's range)
void UCurveLinearColorAtlas::OnCurveUpdated(UCurveBase* Curve, EPropertyChangeType::Type ChangeType)
{
	if (ChangeType != EPropertyChangeType::Interactive)
	{
		UCurveLinearColor* Gradient = CastChecked<UCurveLinearColor>(Curve);

		int32 SlotIndex = GradientCurves.Find(Gradient);

		if (SlotIndex != INDEX_NONE && (uint32)SlotIndex < MaxSlotsPerTexture())
		{
			// Determine the position of the gradient
			int32 StartXY = SlotIndex * TextureSize;

			// Render the single gradient to the render target
			RenderGradient(SrcData, Gradient, StartXY, SizeXY);

			UpdateTexture(*this);
		}
	}	
}

// Render any textures
void UCurveLinearColorAtlas::UpdateTextures()
{
	// Save off the data needed to render each gradient.
	// Callback into the section owner to get the Gradients array
	const int32 TextureDataSize = Source.CalcMipSize(0);
	SrcData.Empty();
	SrcData.AddUninitialized(TextureDataSize);

	int32 NumSlotsToRender = FMath::Min(GradientCurves.Num(), (int32)MaxSlotsPerTexture());
	for (int32 i = 0; i < NumSlotsToRender; ++i)
	{
		if (GradientCurves[i] != nullptr)
		{
			int32 StartXY = i * TextureSize;
			RenderGradient(SrcData, GradientCurves[i], StartXY, SizeXY);
		}

	}

	for (uint32 y = 0; y < TextureSize; y++)
	{
		// Create base mip for the texture we created.
		for (uint32 x = GradientCurves.Num(); x < TextureSize; x++)
		{
			SrcData[x*TextureSize + y] = FLinearColor::White;
		}
	}

	UpdateTexture(*this);

	bIsDirty = false;
}

#endif

bool UCurveLinearColorAtlas::GetCurveIndex(UCurveLinearColor* InCurve, int32& Index)
{
	Index = GradientCurves.Find(InCurve);
	if (Index != INDEX_NONE)
	{
		return true;
	}
	return false;
}

bool UCurveLinearColorAtlas::GetCurvePosition(UCurveLinearColor* InCurve, float& Position)
{
	int32 Index = GradientCurves.Find(InCurve);
	Position = 0.0f;
	if (Index != INDEX_NONE)
	{
		Position = (float)Index;
		return true;
	}
	return false;
}
