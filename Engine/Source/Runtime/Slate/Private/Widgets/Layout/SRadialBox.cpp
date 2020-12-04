// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Layout/SRadialBox.h"
#include "Layout/LayoutUtils.h"

SRadialBox::SRadialBox()
: Slots(this)
{
}

SRadialBox::FSlot& SRadialBox::Slot()
{
	return *( new SRadialBox::FSlot() );
}

SRadialBox::FSlot& SRadialBox::AddSlot()
{
	SRadialBox::FSlot* NewSlot = new SRadialBox::FSlot();
	Slots.Add(NewSlot);
	return *NewSlot;
}

int32 SRadialBox::RemoveSlot( const TSharedRef<SWidget>& SlotWidget )
{
	for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
	{
		if ( SlotWidget == Slots[SlotIdx].GetWidget() )
		{
			Slots.RemoveAt(SlotIdx);
			return SlotIdx;
		}
	}

	return -1;
}

void SRadialBox::Construct( const FArguments& InArgs )
{
	PreferredWidth = InArgs._PreferredWidth;
	bUseAllottedWidth = InArgs._UseAllottedWidth;
	StartingAngle = InArgs._StartingAngle;
	bDistributeItemsEvenly = InArgs._bDistributeItemsEvenly;
	AngleBetweenItems = InArgs._AngleBetweenItems;

	for ( int32 ChildIndex=0; ChildIndex < InArgs.Slots.Num(); ++ChildIndex )
	{
		Slots.Add( InArgs.Slots[ChildIndex] );
	}
}

void SRadialBox::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bUseAllottedWidth)
	{
		PreferredWidth = AllottedGeometry.GetLocalSize().X;
	}
}

/*
 * Simple class for handling the circular arrangement of elements
 */
class SRadialBox::FChildArranger
{
public:
	struct FArrangementData
	{
		FVector2D SlotOffset;
		FVector2D SlotSize;
	};

	typedef TFunctionRef<void(const FSlot& Slot, const FArrangementData& ArrangementData)> FOnSlotArranged;

	static void Arrange(const SRadialBox& RadialBox, const FOnSlotArranged& OnSlotArranged);

private:
	FChildArranger(const SRadialBox& RadialBox, const FOnSlotArranged& OnSlotArranged);
	void Arrange();

	const SRadialBox& RadialBox;
	const FOnSlotArranged& OnSlotArranged;
	TMap<int32, FArrangementData> OngoingArrangementDataMap;
};


SRadialBox::FChildArranger::FChildArranger(const SRadialBox& InRadialBox, const FOnSlotArranged& InOnSlotArranged)
	: RadialBox(InRadialBox)
	, OnSlotArranged(InOnSlotArranged)
{
	OngoingArrangementDataMap.Reserve(RadialBox.Slots.Num());
}

void SRadialBox::FChildArranger::Arrange()
{
	const int32 NumItems = RadialBox.Slots.Num();
	const float Radius = RadialBox.PreferredWidth.Get() / 2.f;
	const float DegreeIncrements = RadialBox.bDistributeItemsEvenly? 360.f / NumItems : RadialBox.AngleBetweenItems;
	float DegreeOffset = RadialBox.StartingAngle * (-1.f) ;

	//Offset to create the elements based on the middle of the widget as starting point
	const float MiddlePointOffset = RadialBox.PreferredWidth.Get() / 2.f;

	for (int32 ChildIndex = 0; ChildIndex < NumItems; ++ChildIndex)
	{
		const FSlot& Slot = RadialBox.Slots[ChildIndex];
		const TSharedRef<SWidget>& Widget = Slot.GetWidget();

		// Skip collapsed widgets.
		if (Widget->GetVisibility() == EVisibility::Collapsed)
		{
			continue;
		}

		FArrangementData& ArrangementData = OngoingArrangementDataMap.Add(ChildIndex, FArrangementData());

		const FVector2D DesiredSizeOfSlot = Widget->GetDesiredSize();

		ArrangementData.SlotOffset.X = (Radius - DesiredSizeOfSlot.X / 2.f) * FMath::Cos(FMath::DegreesToRadians(DegreeOffset)) + MiddlePointOffset - DesiredSizeOfSlot.X / 2.f;
		ArrangementData.SlotOffset.Y = (Radius - DesiredSizeOfSlot.Y / 2.f )* FMath::Sin(FMath::DegreesToRadians(DegreeOffset)) + MiddlePointOffset - DesiredSizeOfSlot.Y / 2.f;
		ArrangementData.SlotSize.X = DesiredSizeOfSlot.X;
		ArrangementData.SlotSize.Y = DesiredSizeOfSlot.Y;

		DegreeOffset -= DegreeIncrements;

		OnSlotArranged(Slot, ArrangementData);
	}
}

void SRadialBox::FChildArranger::Arrange(const SRadialBox& RadialBox, const FOnSlotArranged& OnSlotArranged)
{
	FChildArranger(RadialBox, OnSlotArranged).Arrange();
}

void SRadialBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	FChildArranger::Arrange(*this, [&](const FSlot& Slot, const FChildArranger::FArrangementData& ArrangementData)
	{
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Slot.GetWidget(), ArrangementData.SlotOffset, ArrangementData.SlotSize));
	});
}

void SRadialBox::ClearChildren()
{
	Slots.Empty();
}

FVector2D SRadialBox::ComputeDesiredSize( float ) const
{
	FVector2D MyDesiredSize = FVector2D::ZeroVector;

	FChildArranger::Arrange(*this, [&](const FSlot& Slot, const FChildArranger::FArrangementData& ArrangementData)
	{
		// Increase desired size to the maximum X and Y positions of any child widget.
		MyDesiredSize.X = FMath::Max(MyDesiredSize.X, ArrangementData.SlotOffset.X + ArrangementData.SlotSize.X);
		MyDesiredSize.Y = FMath::Max(MyDesiredSize.Y, ArrangementData.SlotOffset.Y + ArrangementData.SlotSize.Y);
	});

	return MyDesiredSize;
}

FChildren* SRadialBox::GetChildren()
{
	return &Slots;	
}

void SRadialBox::SetUseAllottedWidth(bool bInUseAllottedWidth)
{
	bUseAllottedWidth = bInUseAllottedWidth;
}
