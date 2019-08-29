// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextureInstanceView.cpp: Implementation of content streaming classes.
=============================================================================*/

#include "Streaming/TextureInstanceView.h"
#include "Streaming/TextureInstanceView.inl"
#include "Engine/TextureStreamingTypes.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "Components/PrimitiveComponent.h"
#include "ContentStreaming.h"

void FTextureInstanceView::FBounds4::Set(int32 Index, const FBoxSphereBounds& Bounds, uint32 InPackedRelativeBox, float InLastRenderTime, const FVector& RangeOrigin, float InMinDistance, float InMinRange, float InMaxRange)
{
	check(Index >= 0 && Index < 4);

	OriginX.Component(Index) = Bounds.Origin.X;
	OriginY.Component(Index) = Bounds.Origin.Y;
	OriginZ.Component(Index) = Bounds.Origin.Z;
	RangeOriginX.Component(Index) = RangeOrigin.X;
	RangeOriginY.Component(Index) = RangeOrigin.Y;
	RangeOriginZ.Component(Index) = RangeOrigin.Z;
	ExtentX.Component(Index) = Bounds.BoxExtent.X;
	ExtentY.Component(Index) = Bounds.BoxExtent.Y;
	ExtentZ.Component(Index) = Bounds.BoxExtent.Z;
	Radius.Component(Index) = Bounds.SphereRadius;
	PackedRelativeBox[Index] = InPackedRelativeBox;
	MinDistanceSq.Component(Index) = InMinDistance * InMinDistance;
	MinRangeSq.Component(Index) = InMinRange * InMinRange;
	MaxRangeSq.Component(Index) = InMaxRange != FLT_MAX ? (InMaxRange * InMaxRange) : FLT_MAX;
	LastRenderTime.Component(Index) = InLastRenderTime;
}

void FTextureInstanceView::FBounds4::UnpackBounds(int32 Index, const UPrimitiveComponent* Component)
{
	check(Component);
	check(Index >= 0 && Index < 4);

	if (PackedRelativeBox[Index])
	{
		FBoxSphereBounds SubBounds;
		UnpackRelativeBox(Component->Bounds, PackedRelativeBox[Index], SubBounds);

		// Update the visibility range once we have the bounds.
		float MinDistance = 0, MinRange = 0, MaxRange = FLT_MAX;
		FTextureInstanceView::GetDistanceAndRange(Component, SubBounds, MinDistance, MinRange, MaxRange);

		OriginX.Component(Index) = SubBounds.Origin.X;
		OriginY.Component(Index) = SubBounds.Origin.Y;
		OriginZ.Component(Index) = SubBounds.Origin.Z;
		RangeOriginX.Component(Index) = Component->Bounds.Origin.X;
		RangeOriginY.Component(Index) = Component->Bounds.Origin.Y;
		RangeOriginZ.Component(Index) = Component->Bounds.Origin.Z;
		ExtentX.Component(Index) = SubBounds.BoxExtent.X;
		ExtentY.Component(Index) = SubBounds.BoxExtent.Y;
		ExtentZ.Component(Index) = SubBounds.BoxExtent.Z;
		Radius.Component(Index) = SubBounds.SphereRadius;
		PackedRelativeBox[Index] = PackedRelativeBox_Identity;
		MinDistanceSq.Component(Index) = MinDistance * MinDistance;
		MinRangeSq.Component(Index) = MinRange * MinRange;
		MaxRangeSq.Component(Index) = MaxRange != FLT_MAX ? (MaxRange * MaxRange) : FLT_MAX;
	}
}

/** Dynamic Path, this needs to reset all members since the dynamic data is rebuilt from scratch every update (the previous data is given to the async task) */
void FTextureInstanceView::FBounds4::FullUpdate(int32 Index, const FBoxSphereBounds& Bounds, float InLastRenderTime)
{
	check(Index >= 0 && Index < 4);

	OriginX.Component(Index) = Bounds.Origin.X;
	OriginY.Component(Index) = Bounds.Origin.Y;
	OriginZ.Component(Index) = Bounds.Origin.Z;
	RangeOriginX.Component(Index) = Bounds.Origin.X;
	RangeOriginY.Component(Index) = Bounds.Origin.Y;
	RangeOriginZ.Component(Index) = Bounds.Origin.Z;
	ExtentX.Component(Index) = Bounds.BoxExtent.X;
	ExtentY.Component(Index) = Bounds.BoxExtent.Y;
	ExtentZ.Component(Index) = Bounds.BoxExtent.Z;
	Radius.Component(Index) = Bounds.SphereRadius;
	PackedRelativeBox[Index] = PackedRelativeBox_Identity;
	MinDistanceSq.Component(Index) = 0;
	MinRangeSq.Component(Index) = 0;
	MaxRangeSq.Component(Index) = FLT_MAX;
	LastRenderTime.Component(Index) = InLastRenderTime;
}

FTextureInstanceView::FTextureLinkConstIterator::FTextureLinkConstIterator(const FTextureInstanceView& InState, const UTexture2D* InTexture) 
:	State(InState)
,	CurrElementIndex(INDEX_NONE)
{
	const FTextureDesc* TextureDesc = State.TextureMap.Find(InTexture);
	if (TextureDesc)
	{
		CurrElementIndex = TextureDesc->HeadLink;
	}
}

FBoxSphereBounds FTextureInstanceView::FTextureLinkConstIterator::GetBounds() const
{ 
	FBoxSphereBounds Bounds(ForceInitToZero);

	int32 BoundsIndex = State.Elements[CurrElementIndex].BoundsIndex; 
	if (State.Bounds4.IsValidIndex(BoundsIndex / 4))
	{
		const FBounds4& TheBounds4 = State.Bounds4[BoundsIndex / 4];
		int32 Index = BoundsIndex % 4;

		Bounds.Origin.X = TheBounds4.OriginX[Index];
		Bounds.Origin.Y = TheBounds4.OriginY[Index];
		Bounds.Origin.Z = TheBounds4.OriginZ[Index];

		Bounds.BoxExtent.X = TheBounds4.ExtentX[Index];
		Bounds.BoxExtent.Y = TheBounds4.ExtentY[Index];
		Bounds.BoxExtent.Z = TheBounds4.ExtentZ[Index];

		Bounds.SphereRadius = TheBounds4.Radius[Index];
	}
	return Bounds;
}

void FTextureInstanceView::FTextureLinkConstIterator::OutputToLog(float MaxNormalizedSize, float MaxNormalizedSize_VisibleOnly, const TCHAR* Prefix) const
{
#if !UE_BUILD_SHIPPING
	const UPrimitiveComponent* Component = GetComponent();
	FBoxSphereBounds Bounds = GetBounds();
	float TexelFactor = GetTexelFactor();
	bool bForceLoad = GetForceLoad();

	// Log the component reference.
	if (Component)
	{
		UE_LOG(LogContentStreaming, Log, TEXT("  %sReference= %s"), Prefix, *Component->GetFullName());
	}
	else
	{
		UE_LOG(LogContentStreaming, Log, TEXT("  %sReference"), Prefix);
	}
	
	// Log the wanted mips.
	if (TexelFactor == FLT_MAX || bForceLoad)
	{
		UE_LOG(LogContentStreaming, Log, TEXT("    Forced FullyLoad"));
	}
	else if (TexelFactor >= 0)
	{
		if (GIsEditor) // In editor, visibility information is unreliable and we only consider the max.
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    Size=%f, BoundIndex=%d"), TexelFactor * FMath::Max(MaxNormalizedSize, MaxNormalizedSize_VisibleOnly), GetBoundsIndex());
		}
		else if (MaxNormalizedSize_VisibleOnly > 0)
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    OnScreenSize=%f, BoundIndex=%d"), TexelFactor * MaxNormalizedSize_VisibleOnly, GetBoundsIndex());
		}
		else
		{
			const int32 BoundIndex = GetBoundsIndex();
			if (State.Bounds4.IsValidIndex(BoundIndex / 4))
			{
				float LastRenderTime = State.Bounds4[BoundIndex / 4].LastRenderTime[BoundIndex % 4];
				UE_LOG(LogContentStreaming, Log, TEXT("    OffScreenSize=%f, LastRenderTime= %.3f, BoundIndex=%d"), TexelFactor * MaxNormalizedSize, LastRenderTime, BoundIndex);
			}
			else
			{
				UE_LOG(LogContentStreaming, Log, TEXT("    OffScreenSize=%f, BoundIndex=Invalid"), TexelFactor * MaxNormalizedSize);
			}
		}
	}
	else // Negative texel factors relate to forced specific resolution.
	{
		UE_LOG(LogContentStreaming, Log, TEXT("    ForcedSize=%f"), -TexelFactor);
	}

	// Log the bounds
	if (CVarStreamingUseNewMetrics.GetValueOnGameThread() != 0) // New metrics uses AABB while previous metrics used spheres.
	{
		if (TexelFactor >= 0 && TexelFactor < FLT_MAX)
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    Origin=(%s), BoxExtent=(%s), TexelSize=%f"), *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString(), TexelFactor);
		}
		else
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    Origin=(%s), BoxExtent=(%s)"), *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString());
		}
	}
	else
	{
		if (TexelFactor >= 0 && TexelFactor < FLT_MAX)
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    Origin=(%s), SphereRadius=%f, TexelSize=%f"),  *Bounds.Origin.ToString(), Bounds.SphereRadius, TexelFactor);
		}
		else
		{
			UE_LOG(LogContentStreaming, Log, TEXT("    Origin=(%s), SphereRadius=%f"),  *Bounds.Origin.ToString(), Bounds.SphereRadius);
		}
	}
#endif // !UE_BUILD_SHIPPING
}

TRefCountPtr<const FTextureInstanceView> FTextureInstanceView::CreateView(const FTextureInstanceView* RefView)
{
	TRefCountPtr<FTextureInstanceView> NewView(new FTextureInstanceView());
	
	NewView->Bounds4 = RefView->Bounds4;
	NewView->Elements = RefView->Elements;
	NewView->TextureMap = RefView->TextureMap;
	NewView->MaxTexelFactor = RefView->MaxTexelFactor;
	// NewView->CompiledTextureMap = RefView->CompiledTextureMap;

	return TRefCountPtr<const FTextureInstanceView>(NewView.GetReference());
}

TRefCountPtr<FTextureInstanceView> FTextureInstanceView::CreateViewWithUninitializedBounds(const FTextureInstanceView* RefView)
{
	TRefCountPtr<FTextureInstanceView> NewView(new FTextureInstanceView());
	
	NewView->Bounds4.AddUninitialized(RefView->Bounds4.Num());
	NewView->Elements = RefView->Elements;
	NewView->TextureMap = RefView->TextureMap;
	NewView->MaxTexelFactor = RefView->MaxTexelFactor;
	// NewView->CompiledTextureMap = RefView->RefView;

	return NewView;
}

void FTextureInstanceView::GetDistanceAndRange(const UPrimitiveComponent* Component, const FBoxSphereBounds& TextureInstanceBounds, float& MinDistance, float& MinRange, float& MaxRange)
{
	check(Component && Component->IsRegistered());

	// In the engine, the MinDistance is computed from the component bound center to the viewpoint.
	// The streaming computes the distance as the distance from viewpoint to the edge of the texture bound box.
	// The implementation also handles MinDistance by bounding the distance to it so that if the viewpoint gets closer the screen size will stop increasing at some point.
	// The fact that the primitive will disappear is not so relevant as this will be handled by the visibility logic, normally streaming one less mip than requested.
	// The important mater is to control the requested mip by limiting the distance, since at close up, the distance becomes very small and all mips are streamer (even after the 1 mip bias).

	const UPrimitiveComponent* LODParent = Component->GetLODParentPrimitive();

	MinDistance = FMath::Max<float>(0, Component->MinDrawDistance - (TextureInstanceBounds.Origin - Component->Bounds.Origin).Size() - TextureInstanceBounds.SphereRadius);
	MinRange = FMath::Max<float>(0, Component->MinDrawDistance);
	// Max distance when HLOD becomes visible.
	MaxRange = LODParent ? (LODParent->MinDrawDistance + (Component->Bounds.Origin - LODParent->Bounds.Origin).Size()) : FLT_MAX;
	if (LODParent)
	{
		MaxRange = LODParent->MinDrawDistance + (Component->Bounds.Origin - LODParent->Bounds.Origin).Size();
	}
}

void FTextureInstanceView::SwapData(FTextureInstanceView* Lfs, FTextureInstanceView* Rhs)
{
	// Things must be compatible somehow or derived classes will be in incoherent state.
	check(Lfs->Bounds4.Num() == Rhs->Bounds4.Num());
	check(Lfs->Elements.Num() == Rhs->Elements.Num());
	check(Lfs->TextureMap.Num() == Rhs->TextureMap.Num());
	check(Lfs->CompiledTextureMap.Num() == 0 && Rhs->CompiledTextureMap.Num() == 0);

	FMemory::Memswap(&Lfs->Bounds4 , &Rhs->Bounds4, sizeof(Lfs->Bounds4));
	FMemory::Memswap(&Lfs->Elements , &Rhs->Elements, sizeof(Lfs->Elements));
	FMemory::Memswap(&Lfs->TextureMap , &Rhs->TextureMap, sizeof(Lfs->TextureMap));
	FMemory::Memswap(&Lfs->MaxTexelFactor , &Rhs->MaxTexelFactor, sizeof(Lfs->MaxTexelFactor));
}

struct FStreamingViewInfoExtra
{
	// The screen size factor including the view boost.
	float ScreenSizeFloat;

	// The extra view boost for visible primitive (if ViewInfo.BoostFactor > "r.Streaming.MaxHiddenPrimitiveViewBoost").
	float ExtraBoostForVisiblePrimitiveFloat;
};

void FTextureInstanceAsyncView::UpdateBoundSizes_Async(const TArray<FStreamingViewInfo>& ViewInfos, float LastUpdateTime, const FTextureStreamingSettings& Settings)
{
	if (!View.IsValid())  return;

	const int32 NumViews = ViewInfos.Num();
	const int32 NumBounds4 = View->NumBounds4();

	const VectorRegister LastUpdateTime4 = VectorSet(LastUpdateTime, LastUpdateTime, LastUpdateTime, LastUpdateTime);
	const float OneOverMaxHiddenPrimitiveViewBoost = 1.f / Settings.MaxHiddenPrimitiveViewBoost;
	
	TArray<FStreamingViewInfoExtra, TInlineAllocator<4> > ViewInfoExtras;
	ViewInfoExtras.AddZeroed(ViewInfos.Num());
	
	for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
	{
		const FStreamingViewInfo& ViewInfo = ViewInfos[ViewIndex];
		FStreamingViewInfoExtra& ViewInfoExtra = ViewInfoExtras[ViewIndex];

		const float EffectiveScreenSize = (Settings.MaxEffectiveScreenSize > 0.0f) ? FMath::Min(Settings.MaxEffectiveScreenSize, ViewInfo.ScreenSize) : ViewInfo.ScreenSize;
		ViewInfoExtra.ScreenSizeFloat = EffectiveScreenSize * .5f; // Multiply by half since the ratio factors map to half the screen only
		ViewInfoExtra.ExtraBoostForVisiblePrimitiveFloat = 1.f;

		if (ViewInfo.BoostFactor > Settings.MaxHiddenPrimitiveViewBoost)
		{
			ViewInfoExtra.ScreenSizeFloat *= Settings.MaxHiddenPrimitiveViewBoost;
			ViewInfoExtra.ExtraBoostForVisiblePrimitiveFloat = ViewInfo.BoostFactor * OneOverMaxHiddenPrimitiveViewBoost;
		}
		else
		{
			ViewInfoExtra.ScreenSizeFloat *= ViewInfo.BoostFactor;
		}
	}

	BoundsViewInfo.Empty(NumBounds4 * 4);
	BoundsViewInfo.AddUninitialized(NumBounds4 * 4);

	// Max normalized size from all elements
	VectorRegister ViewMaxNormalizedSize = VectorZero();

	for (int32 Bounds4Index = 0; Bounds4Index < NumBounds4; ++Bounds4Index)
	{
		const FTextureInstanceView::FBounds4& CurrentBounds4 = View->GetBounds4(Bounds4Index);

		// Calculate distance of viewer to bounding sphere.
		const VectorRegister OriginX = VectorLoadAligned( &CurrentBounds4.OriginX );
		const VectorRegister OriginY = VectorLoadAligned( &CurrentBounds4.OriginY );
		const VectorRegister OriginZ = VectorLoadAligned( &CurrentBounds4.OriginZ );
		const VectorRegister RangeOriginX = VectorLoadAligned( &CurrentBounds4.RangeOriginX );
		const VectorRegister RangeOriginY = VectorLoadAligned( &CurrentBounds4.RangeOriginY );
		const VectorRegister RangeOriginZ = VectorLoadAligned( &CurrentBounds4.RangeOriginZ );
		const VectorRegister ExtentX = VectorLoadAligned( &CurrentBounds4.ExtentX );
		const VectorRegister ExtentY = VectorLoadAligned( &CurrentBounds4.ExtentY );
		const VectorRegister ExtentZ = VectorLoadAligned( &CurrentBounds4.ExtentZ );
		const VectorRegister Radius = VectorLoadAligned( &CurrentBounds4.Radius );
		const VectorRegister PackedRelativeBox = VectorLoadAligned( reinterpret_cast<const FVector4*>(&CurrentBounds4.PackedRelativeBox) );
		const VectorRegister MinDistanceSq = VectorLoadAligned( &CurrentBounds4.MinDistanceSq );
		const VectorRegister MinRangeSq = VectorLoadAligned( &CurrentBounds4.MinRangeSq );
		const VectorRegister MaxRangeSq = VectorLoadAligned(&CurrentBounds4.MaxRangeSq);
		const VectorRegister LastRenderTime = VectorLoadAligned(&CurrentBounds4.LastRenderTime);

		VectorRegister MaxNormalizedSize = VectorZero();
		VectorRegister MaxNormalizedSize_VisibleOnly = VectorZero();

		for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
		{
			const FStreamingViewInfo& ViewInfo = ViewInfos[ViewIndex];
			const FStreamingViewInfoExtra& ViewInfoExtra = ViewInfoExtras[ViewIndex];

			const VectorRegister ScreenSize = VectorLoadFloat1( &ViewInfoExtra.ScreenSizeFloat );
			const VectorRegister ExtraBoostForVisiblePrimitive = VectorLoadFloat1( &ViewInfoExtra.ExtraBoostForVisiblePrimitiveFloat );
			const VectorRegister ViewOriginX = VectorLoadFloat1( &ViewInfo.ViewOrigin.X );
			const VectorRegister ViewOriginY = VectorLoadFloat1( &ViewInfo.ViewOrigin.Y );
			const VectorRegister ViewOriginZ = VectorLoadFloat1( &ViewInfo.ViewOrigin.Z );

			VectorRegister DistSqMinusRadiusSq = VectorZero();
			if (Settings.bUseNewMetrics)
			{
				// In this case DistSqMinusRadiusSq will contain the distance to the box^2
				VectorRegister Temp = VectorSubtract( ViewOriginX, OriginX );
				Temp = VectorAbs( Temp );
				VectorRegister BoxRef = VectorMin( Temp, ExtentX );
				Temp = VectorSubtract( Temp, BoxRef );
				DistSqMinusRadiusSq = VectorMultiply( Temp, Temp );

				Temp = VectorSubtract( ViewOriginY, OriginY );
				Temp = VectorAbs( Temp );
				BoxRef = VectorMin( Temp, ExtentY );
				Temp = VectorSubtract( Temp, BoxRef );
				DistSqMinusRadiusSq = VectorMultiplyAdd( Temp, Temp, DistSqMinusRadiusSq );

				Temp = VectorSubtract( ViewOriginZ, OriginZ );
				Temp = VectorAbs( Temp );
				BoxRef = VectorMin( Temp, ExtentZ );
				Temp = VectorSubtract( Temp, BoxRef );
				DistSqMinusRadiusSq = VectorMultiplyAdd( Temp, Temp, DistSqMinusRadiusSq );
			}
			else
			{
				VectorRegister Temp = VectorSubtract( ViewOriginX, OriginX );
				VectorRegister DistSq = VectorMultiply( Temp, Temp );
				Temp = VectorSubtract( ViewOriginY, OriginY );
				DistSq = VectorMultiplyAdd( Temp, Temp, DistSq );
				Temp = VectorSubtract( ViewOriginZ, OriginZ );
				DistSq = VectorMultiplyAdd( Temp, Temp, DistSq );

				DistSqMinusRadiusSq = VectorMultiply( Radius, Radius );
				DistSqMinusRadiusSq = VectorSubtract( DistSq, DistSqMinusRadiusSq );
				// This can be negative here!!!
			}

			// If the bound is not visible up close, limit the distance to it's minimal possible range.
			VectorRegister ClampedDistSq = VectorMax( MinDistanceSq, DistSqMinusRadiusSq );

			// Compute in range  Squared distance between range.
			VectorRegister InRangeMask;
			{
				VectorRegister Temp = VectorSubtract( ViewOriginX, RangeOriginX );
				VectorRegister RangeDistSq = VectorMultiply( Temp, Temp );
				Temp = VectorSubtract( ViewOriginY, RangeOriginY );
				RangeDistSq = VectorMultiplyAdd( Temp, Temp, RangeDistSq );
				Temp = VectorSubtract( ViewOriginZ, RangeOriginZ );
				RangeDistSq = VectorMultiplyAdd( Temp, Temp, RangeDistSq );

				VectorRegister ClampedRangeDistSq = VectorMax( MinRangeSq, RangeDistSq );
				ClampedRangeDistSq = VectorMin( MaxRangeSq, ClampedRangeDistSq );
				InRangeMask = VectorCompareEQ( RangeDistSq, ClampedRangeDistSq); // If the clamp dist is equal, then it was in range.
			}

			ClampedDistSq = VectorMax(ClampedDistSq, VectorOne()); // Prevents / 0
			VectorRegister ScreenSizeOverDistance = VectorReciprocalSqrt(ClampedDistSq);
			ScreenSizeOverDistance = VectorMultiply(ScreenSizeOverDistance, ScreenSize);

			MaxNormalizedSize = VectorMax(ScreenSizeOverDistance, MaxNormalizedSize);

			// Accumulate the view max amongst all. When PackedRelativeBox == 0, the entry is not valid and must not affet the max.
			const VectorRegister CulledMaxNormalizedSize = VectorSelect(VectorCompareNE(PackedRelativeBox, VectorZero()), MaxNormalizedSize, VectorZero());
			ViewMaxNormalizedSize = VectorMax(ViewMaxNormalizedSize, CulledMaxNormalizedSize);

			// Now mask to zero if not in range, or not seen recently.
			ScreenSizeOverDistance = VectorMultiply(ScreenSizeOverDistance, ExtraBoostForVisiblePrimitive);
			ScreenSizeOverDistance = VectorSelect(InRangeMask, ScreenSizeOverDistance, VectorZero());
			ScreenSizeOverDistance = VectorSelect(VectorCompareGT(LastRenderTime, LastUpdateTime4), ScreenSizeOverDistance, VectorZero());

			MaxNormalizedSize_VisibleOnly = VectorMax(ScreenSizeOverDistance, MaxNormalizedSize_VisibleOnly);
		}

		// Store results
		FBoundsViewInfo* BoundsVieWInfo = &BoundsViewInfo[Bounds4Index * 4];
		for (int32 SubIndex = 0; SubIndex < 4; ++SubIndex)
		{
			BoundsVieWInfo[SubIndex].MaxNormalizedSize = VectorGetComponent(MaxNormalizedSize, SubIndex);
			BoundsVieWInfo[SubIndex].MaxNormalizedSize_VisibleOnly = VectorGetComponent(MaxNormalizedSize_VisibleOnly, SubIndex);
		}
	}

	if (Settings.MinLevelTextureScreenSize > 0)
	{
		float ViewMaxNormalizedSizeResult = VectorGetComponent(ViewMaxNormalizedSize, 0);
		for (int32 SubIndex = 1; SubIndex < 4; ++SubIndex)
		{
			ViewMaxNormalizedSizeResult = FMath::Max(ViewMaxNormalizedSizeResult, VectorGetComponent(ViewMaxNormalizedSize, SubIndex));
		}
		MaxLevelTextureScreenSize = View->GetMaxTexelFactor() * ViewMaxNormalizedSizeResult;
	}
}

void FTextureInstanceAsyncView::ProcessElement(const FBoundsViewInfo& BoundsVieWInfo, float TexelFactor, bool bForceLoad, float& MaxSize, float& MaxSize_VisibleOnly) const
{
	if (TexelFactor == FLT_MAX) // If this is a forced load component.
	{
		MaxSize = BoundsVieWInfo.MaxNormalizedSize > 0 ? FLT_MAX : MaxSize;
		MaxSize_VisibleOnly = BoundsVieWInfo.MaxNormalizedSize_VisibleOnly > 0 ? FLT_MAX : MaxSize_VisibleOnly;
	}
	else if (TexelFactor >= 0)
	{
		MaxSize = FMath::Max(MaxSize, TexelFactor * BoundsVieWInfo.MaxNormalizedSize);
		MaxSize_VisibleOnly = FMath::Max(MaxSize_VisibleOnly, TexelFactor * BoundsVieWInfo.MaxNormalizedSize_VisibleOnly);

		// Force load will load the immediately visible part, and later the full texture.
		if (bForceLoad && (BoundsVieWInfo.MaxNormalizedSize > 0 || BoundsVieWInfo.MaxNormalizedSize_VisibleOnly > 0))
		{
			MaxSize = FLT_MAX;
		}
	}
	else // Negative texel factors map to fixed resolution. Currently used for lanscape.
	{
		MaxSize = FMath::Max(MaxSize, -TexelFactor);
		MaxSize_VisibleOnly = FMath::Max(MaxSize_VisibleOnly, -TexelFactor);

		// Force load will load the immediatly visible part, and later the full texture.
		if (bForceLoad && (BoundsVieWInfo.MaxNormalizedSize > 0 || BoundsVieWInfo.MaxNormalizedSize_VisibleOnly > 0))
		{
			MaxSize = FLT_MAX;
			MaxSize_VisibleOnly = FLT_MAX;
		}
	}
}

void FTextureInstanceAsyncView::GetTexelSize(const UTexture2D* InTexture, float& MaxSize, float& MaxSize_VisibleOnly, const TCHAR* LogPrefix) const
{
	// No need to iterate more if texture is already at maximum resolution.

	int32 CurrCount = 0;

	if (View.IsValid())
	{
		// Use the fast path if available, about twice as fast when there are a lot of elements.
		if (View->HasCompiledElements() && !LogPrefix)
		{
			const TArray<FTextureInstanceView::FCompiledElement>* CompiledElements = View->GetCompiledElements(InTexture);
			if (CompiledElements)
			{
				const int32 NumCompiledElements = CompiledElements->Num();
				const FTextureInstanceView::FCompiledElement* CompiledElementData = CompiledElements->GetData();

				int32 CompiledElementIndex = 0;
				while (CompiledElementIndex < NumCompiledElements && MaxSize_VisibleOnly < MAX_TEXTURE_SIZE)
				{
					const FTextureInstanceView::FCompiledElement& CompiledElement = CompiledElementData[CompiledElementIndex];
					if (ensure(BoundsViewInfo.IsValidIndex(CompiledElement.BoundsIndex)))
					{
						ProcessElement(BoundsViewInfo[CompiledElement.BoundsIndex], CompiledElement.TexelFactor, CompiledElement.bForceLoad, MaxSize, MaxSize_VisibleOnly);
					}
					++CompiledElementIndex;
				}

				if (MaxSize_VisibleOnly >= MAX_TEXTURE_SIZE && CompiledElementIndex > 1)
				{
					// This does not realloc anything but moves the closest element at head, making the next update find it immediately and early exit.
					FTextureInstanceView::FCompiledElement* SwapElementData = const_cast<FTextureInstanceView::FCompiledElement*>(CompiledElementData);
					Swap<FTextureInstanceView::FCompiledElement>(SwapElementData[0], SwapElementData[CompiledElementIndex - 1]);
				}
			}
		}
		else
		{
			for (auto It = View->GetElementIterator(InTexture); It && (MaxSize_VisibleOnly < MAX_TEXTURE_SIZE || LogPrefix); ++It)
			{
				// Only handle elements that are in bounds.
				if (ensure(BoundsViewInfo.IsValidIndex(It.GetBoundsIndex())))
				{
					const FBoundsViewInfo& BoundsVieWInfo = BoundsViewInfo[It.GetBoundsIndex()];
					ProcessElement(BoundsVieWInfo, It.GetTexelFactor(), It.GetForceLoad(), MaxSize, MaxSize_VisibleOnly);
					if (LogPrefix)
					{
						It.OutputToLog(BoundsVieWInfo.MaxNormalizedSize, BoundsVieWInfo.MaxNormalizedSize_VisibleOnly, LogPrefix);
					}
				}
			}
		}
	}
}

bool FTextureInstanceAsyncView::HasTextureReferences(const UTexture2D* InTexture) const
{
	return View.IsValid() && (bool)View->GetElementIterator(InTexture);
}
