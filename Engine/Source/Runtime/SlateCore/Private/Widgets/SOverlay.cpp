// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

 #include "Widgets/SOverlay.h"
 #include "Types/PaintArgs.h"
 #include "Layout/ArrangedChildren.h"
 #include "Layout/LayoutUtils.h"


SOverlay::SOverlay()
	: Children(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
}

void SOverlay::Construct( const SOverlay::FArguments& InArgs )
{
	const int32 NumSlots = InArgs.Slots.Num();
	for ( int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex )
	{
		Children.Add( InArgs.Slots[SlotIndex] );
	}
}

void SOverlay::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	for ( int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex )
	{
		const FOverlaySlot& CurChild = Children[ChildIndex];
		const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
		if ( ArrangedChildren.Accepts(ChildVisibility) )
		{
			const FMargin SlotPadding(LayoutPaddingWithFlow(GSlateFlowDirection, CurChild.SlotPadding.Get()));
			AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(GSlateFlowDirection, AllottedGeometry.GetLocalSize().X, CurChild, SlotPadding);
			AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AllottedGeometry.GetLocalSize().Y, CurChild, SlotPadding);

			ArrangedChildren.AddWidget( ChildVisibility, AllottedGeometry.MakeChild(
				CurChild.GetWidget(),
				FVector2D(XResult.Offset, YResult.Offset),
				FVector2D(XResult.Size, YResult.Size)
			) );
		}
	}
}

FVector2D SOverlay::ComputeDesiredSize( float ) const
{
	FVector2D MaxSize(0,0);
	for ( int32 ChildIndex=0; ChildIndex < Children.Num(); ++ChildIndex )
	{
		const FOverlaySlot& CurSlot = Children[ChildIndex];
		const EVisibility ChildVisibilty = CurSlot.GetWidget()->GetVisibility();
		if ( ChildVisibilty != EVisibility::Collapsed )
		{
			FVector2D ChildDesiredSize = CurSlot.GetWidget()->GetDesiredSize() + CurSlot.SlotPadding.Get().GetDesiredSize();
			MaxSize.X = FMath::Max( MaxSize.X, ChildDesiredSize.X );
			MaxSize.Y = FMath::Max( MaxSize.Y, ChildDesiredSize.Y );
		}
	}

	return MaxSize;
}

FChildren* SOverlay::GetChildren()
{
	return &Children;
}

int32 SOverlay::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	{
		// The box panel has no visualization of its own; it just visualizes its children.
		ArrangeChildren(AllottedGeometry, ArrangedChildren);
	}

	// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents
	// wants to an overlay for all of its contents.
	int32 MaxLayerId = LayerId;

	const FPaintArgs NewArgs = Args.WithNewParent(this);
	const bool bChildrenEnabled = ShouldBeEnabled(bParentEnabled);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		// We don't increment the first layer.
		if (ChildIndex > 0)
		{
			MaxLayerId++;
		}

		FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

		const int32 CurWidgetsMaxLayerId =
			CurWidget.Widget->Paint(
				NewArgs,
				CurWidget.Geometry,
				MyCullingRect,
				OutDrawElements,
				MaxLayerId,
				InWidgetStyle,
				bChildrenEnabled);

		MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
	}

	return MaxLayerId;
}

SOverlay::FOverlaySlot& SOverlay::AddSlot( int32 ZOrder )
{
	FOverlaySlot& NewSlot = *new FOverlaySlot();
	if ( ZOrder == INDEX_NONE )
	{
		// No ZOrder was specified; just add to the end of the list.
		// Use a ZOrder index one after the last elements.
		ZOrder = (Children.Num() == 0)
			? 0
			: ( Children[ Children.Num()-1 ].ZOrder + 1 );

		this->Children.Add( &NewSlot );
	}
	else
	{
		// Figure out where to add the widget based on ZOrder
		bool bFoundSlot = false;
		int32 CurSlotIndex = 0;
		for( ; CurSlotIndex < Children.Num(); ++CurSlotIndex )
		{
			const FOverlaySlot& CurSlot = Children[ CurSlotIndex ];
			if( ZOrder < CurSlot.ZOrder )
			{
				// Insert before
				bFoundSlot = true;
				break;
			}
		}

		// Add a slot at the desired location
		this->Children.Insert( &NewSlot, CurSlotIndex );
	}

	Invalidate(EInvalidateWidget::Layout);

	NewSlot.ZOrder = ZOrder;
	return NewSlot;
}

void SOverlay::RemoveSlot( int32 ZOrder )
{
	if (ZOrder != INDEX_NONE)
	{
		for( int32 ChildIndex=0; ChildIndex < Children.Num(); ++ChildIndex )
		{
			if ( Children[ChildIndex].ZOrder == ZOrder )
			{
				Children.RemoveAt( ChildIndex );
				Invalidate(EInvalidateWidget::Layout);
				return;
			}
		}

		ensureMsgf(false, TEXT("Could not remove slot. There are no children with ZOrder %d."));
	}
	else if (Children.Num() > 0)
	{
		Children.RemoveAt( Children.Num() - 1 );
		Invalidate(EInvalidateWidget::Layout);
	}
	else
	{
		ensureMsgf(false, TEXT("Could not remove slot. There are no slots left."));
	}
}

void SOverlay::ClearChildren()
{
	Children.Empty();
	Invalidate(EInvalidateWidget::Layout);
}

int32 SOverlay::GetNumWidgets() const
{
	return Children.Num();
}

bool SOverlay::RemoveSlot( TSharedRef< SWidget > Widget )
{
	// Search and remove
	for( int32 CurSlotIndex = 0; CurSlotIndex < Children.Num(); ++CurSlotIndex )
	{
		const FOverlaySlot& CurSlot = Children[ CurSlotIndex ];
		if( CurSlot.GetWidget() == Widget )
		{
			Children.RemoveAt( CurSlotIndex );
			Invalidate(EInvalidateWidget::Layout);
			return true;
		}
	}

	return false;
}
