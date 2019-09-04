// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/Throbber.h"
#include "SlateFwd.h"
#include "Slate/SlateBrushAsset.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UThrobber

static FSlateBrush* DefaultThrobberBrush = nullptr;

UThrobber::UThrobber(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumberOfPieces = 3;

	bAnimateVertically = true;
	bAnimateHorizontally = true;
	bAnimateOpacity = true;

	if (DefaultThrobberBrush == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultThrobberBrush = new FSlateBrush(*FCoreStyle::Get().GetBrush("Throbber.Chunk"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultThrobberBrush->UnlinkColors();
	}

	Image = *DefaultThrobberBrush;
}

void UThrobber::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyThrobber.Reset();
}

TSharedRef<SWidget> UThrobber::RebuildWidget()
{
	MyThrobber = SNew(SThrobber)
		.PieceImage(&Image)
		.NumPieces(FMath::Clamp(NumberOfPieces, 1, 25))
		.Animate(GetAnimation());

	return MyThrobber.ToSharedRef();
}

void UThrobber::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyThrobber->SetNumPieces(FMath::Clamp(NumberOfPieces, 1, 25));
	MyThrobber->SetAnimate(GetAnimation());
}

SThrobber::EAnimation UThrobber::GetAnimation() const
{
	const int32 AnimationParams = (bAnimateVertically ? SThrobber::Vertical : 0) |
		(bAnimateHorizontally ? SThrobber::Horizontal : 0) |
		(bAnimateOpacity ? SThrobber::Opacity : 0);

	return static_cast<SThrobber::EAnimation>(AnimationParams);
}

void UThrobber::SetNumberOfPieces(int32 InNumberOfPieces)
{
	NumberOfPieces = InNumberOfPieces;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetNumPieces(FMath::Clamp(NumberOfPieces, 1, 25));
	}
}

void UThrobber::SetAnimateHorizontally(bool bInAnimateHorizontally)
{
	bAnimateHorizontally = bInAnimateHorizontally;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber::SetAnimateVertically(bool bInAnimateVertically)
{
	bAnimateVertically = bInAnimateVertically;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber:: SetAnimateOpacity(bool bInAnimateOpacity)
{
	bAnimateOpacity = bInAnimateOpacity;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber::PostLoad()
{
	Super::PostLoad();

	if ( GetLinkerUE4Version() < VER_UE4_DEPRECATE_UMG_STYLE_ASSETS )
	{
		if ( PieceImage_DEPRECATED != nullptr )
		{
			Image = PieceImage_DEPRECATED->Brush;
			PieceImage_DEPRECATED = nullptr;
		}
	}
}

#if WITH_EDITOR

const FText UThrobber::GetPaletteCategory()
{
	return LOCTEXT("Primitive", "Primitive");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
