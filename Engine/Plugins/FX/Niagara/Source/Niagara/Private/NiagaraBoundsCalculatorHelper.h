// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "NiagaraBoundsCalculator.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"

enum class ENiagaraBoundsMeshOffsetTransform
{
	None,
	WorldToLocal,
	LocalToWorld
};

template<bool bUsedWithSprites, bool bUsedWithMeshes, bool bUsedWithRibbons>
class FNiagaraBoundsCalculatorHelper : public FNiagaraBoundsCalculator
{
public:

	FNiagaraBoundsCalculatorHelper() = default;
	FNiagaraBoundsCalculatorHelper(const FVector& InMeshExtents, const FVector& InMeshOffset, ENiagaraBoundsMeshOffsetTransform InMeshOffsetTransform)
		: MeshExtents(InMeshExtents)
		, MeshOffset(InMeshOffset)
		, MeshOffsetTransform(InMeshOffsetTransform)
	{}

	virtual void InitAccessors(const FNiagaraDataSetCompiledData* CompiledData) override final
	{
		static const FName PositionName(TEXT("Position"));
		static const FName SpriteSizeName(TEXT("SpriteSize"));
		static const FName ScaleName(TEXT("Scale"));
		static const FName RibbonWidthName(TEXT("RibbonWidth"));

		PositionAccessor.Init(CompiledData, PositionName);
		if (bUsedWithSprites)
		{
			SpriteSizeAccessor.Init(CompiledData, SpriteSizeName);
		}
		if (bUsedWithMeshes)
		{
			ScaleAccessor.Init(CompiledData, ScaleName);
		}
		if (bUsedWithRibbons)
		{
			RibbonWidthAccessor.Init(CompiledData, RibbonWidthName);
		}
	}

	virtual FBox CalculateBounds(const FTransform& SystemTransform, const FNiagaraDataSet& DataSet, const int32 NumInstances) const override final
	{
		if (!NumInstances || !PositionAccessor.IsValid())
		{
			return FBox(ForceInit);
		}

		constexpr float kDefaultSize = 50.0f;

		FBox Bounds(ForceInitToZero);
		PositionAccessor.GetReader(DataSet).GetMinMax(Bounds.Min, Bounds.Max);

		float MaxSize = KINDA_SMALL_NUMBER;
		if (bUsedWithMeshes)
		{
			FVector MaxScale(1.0f, 1.0f, 1.0f);
			if (ScaleAccessor.IsValid())
			{
				MaxScale = ScaleAccessor.GetReader(DataSet).GetMax();
			}

			// NOTE: Since we're not taking particle rotation into account we have to treat the extents like a sphere,
			// which is a little bit more conservative, but saves us having to rotate the extents per particle
			const FVector ScaledExtents = MeshExtents * MaxScale;
			MaxSize = FMath::Max(MaxSize, ScaledExtents.Size());
			
			// Apply a potentially transformed MeshOffset
			FVector TransformedOffset;
			switch (MeshOffsetTransform)
			{
			case ENiagaraBoundsMeshOffsetTransform::LocalToWorld:
				TransformedOffset = SystemTransform.TransformVector(MeshOffset);
				break;

			case ENiagaraBoundsMeshOffsetTransform::WorldToLocal:
				TransformedOffset = SystemTransform.InverseTransformVector(MeshOffset);
				break;

			default:
				TransformedOffset = MeshOffset;
				break;
			}

			if (!bUsedWithSprites && !bUsedWithRibbons)
			{
				// If it's only used with meshes, we can simply shift the min/max
				Bounds = Bounds.ShiftBy(TransformedOffset);
			}
			else
			{
				// We have to extend the min/max by the offset
				Bounds.Max = Bounds.Max.ComponentMax(Bounds.Max + TransformedOffset);
				Bounds.Min = Bounds.Min.ComponentMin(Bounds.Min + TransformedOffset);
			}
		}

		if (bUsedWithSprites)
		{
			float MaxSpriteSize = kDefaultSize;

			if (SpriteSizeAccessor.IsValid())
			{
				const FVector2D MaxSpriteSize2D = SpriteSizeAccessor.GetReader(DataSet).GetMax();
				MaxSpriteSize = FMath::Max(MaxSpriteSize2D.X, MaxSpriteSize2D.Y);
			}
			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxSpriteSize) ? 1.0f : MaxSpriteSize);
		}

		if (bUsedWithRibbons)
		{
			float MaxRibbonWidth = kDefaultSize;
			if (RibbonWidthAccessor.IsValid())
			{
				MaxRibbonWidth = RibbonWidthAccessor.GetReader(DataSet).GetMax();
			}

			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxRibbonWidth) ? 1.0f : MaxRibbonWidth);
		}

		return Bounds.ExpandBy(MaxSize);
	}

	FNiagaraDataSetAccessor<FVector> PositionAccessor;
	FNiagaraDataSetAccessor<FVector2D> SpriteSizeAccessor;
	FNiagaraDataSetAccessor<FVector> ScaleAccessor;
	FNiagaraDataSetAccessor<float> RibbonWidthAccessor;

	const FVector MeshExtents = FVector::OneVector;
	const FVector MeshOffset = FVector::ZeroVector;
	const ENiagaraBoundsMeshOffsetTransform MeshOffsetTransform = ENiagaraBoundsMeshOffsetTransform::None;
};
