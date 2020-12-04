// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingTransformHandle.h"
#include "Views/SDMXPixelMappingDesignerView.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "DMXPixelMappingComponentReference.h"

#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SDMXPixelMappingTransformHandle"

void SDMXPixelMappingTransformHandle::Construct(const FArguments& InArgs, TSharedPtr<SDMXPixelMappingDesignerView> InDesignerView, EDMXPixelMappingTransformDirection InTransformDirection, TAttribute<FVector2D> InOffset)
{
	TransformDirection = InTransformDirection;
	DesignerViewWeakPtr = InDesignerView;
	Offset = InOffset;

	Action = EDMXPixelMappingTransformAction::None;
	ScopedTransaction = nullptr;

	DragDirection = ComputeDragDirection(InTransformDirection);
	DragOrigin = ComputeOrigin(InTransformDirection);

	ChildSlot
	[
		SNew(SImage)
		.Visibility(this, &SDMXPixelMappingTransformHandle::GetHandleVisibility)
		.Image(FEditorStyle::Get().GetBrush("UMGEditor.TransformHandle"))
	];
}

EVisibility SDMXPixelMappingTransformHandle::GetHandleVisibility() const
{
	return EVisibility::Visible;
}

FReply SDMXPixelMappingTransformHandle::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		Action = ComputeActionAtLocation(MyGeometry, MouseEvent);

		check(DesignerViewWeakPtr.Pin());

		const FDMXPixelMappingComponentReference& ComponentReference = DesignerViewWeakPtr.Pin()->GetSelectedComponent();
		UDMXPixelMappingBaseComponent* Preview = ComponentReference.GetComponent();
		UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent();

		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Preview))
		{
			FMargin Offsets;
			FVector2D Size = OutputComponent->GetSize();
			Offsets.Right = Size.X;
			Offsets.Bottom = Size.Y;
			StartingOffsets = Offsets;
		}

		MouseDownPosition = MouseEvent.GetScreenSpacePosition();

		ScopedTransaction = new FScopedTransaction(LOCTEXT("ResizeWidget", "Resize Widget"));
		Component->Modify();

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SDMXPixelMappingTransformHandle::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		if ( ScopedTransaction )
		{
			delete ScopedTransaction;
			ScopedTransaction = nullptr;
		}

		Action = EDMXPixelMappingTransformAction::None;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SDMXPixelMappingTransformHandle::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( Action != EDMXPixelMappingTransformAction::None )
	{
		check(DesignerViewWeakPtr.Pin());

		const FDMXPixelMappingComponentReference& ComponentReference = DesignerViewWeakPtr.Pin()->GetSelectedComponent();
		UDMXPixelMappingBaseComponent* Preview = ComponentReference.GetComponent();
		UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent();

		const FVector2D Delta = MouseEvent.GetScreenSpacePosition() - MouseDownPosition;
		const FVector2D TranslateAmount = Delta * (1.0f / (DesignerViewWeakPtr.Pin()->GetPreviewScale() * MyGeometry.Scale));

		Resize(Component, DragDirection, TranslateAmount);
	}

	return FReply::Unhandled();
}

void SDMXPixelMappingTransformHandle::Resize(UDMXPixelMappingBaseComponent* BaseComponent, const FVector2D& Direction, const FVector2D& Amount)
{

	if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(BaseComponent))
	{
		FVector2D ComponentSize = OutputComponent->GetSize();

		FMargin Offsets = StartingOffsets;

		FVector2D Movement = Amount * Direction;
		FVector2D PositionMovement = Movement * (FVector2D(1.0f, 1.0f));
		FVector2D SizeMovement = Movement;

		if (Direction.X < 0)
		{
			Offsets.Left -= PositionMovement.X;
			Offsets.Right += SizeMovement.X;
		}

		if (Direction.Y < 0)
		{
			Offsets.Top -= PositionMovement.Y;
			Offsets.Bottom += SizeMovement.Y;
		}

		if (Direction.X > 0)
		{
			Offsets.Left += (Movement).X;
			Offsets.Right += Amount.X * Direction.X;
		}

		if (Direction.Y > 0)
		{
			Offsets.Top += (Movement).Y;
			Offsets.Bottom += Amount.Y * Direction.Y;
		}


		OutputComponent->SetSize(FVector2D(Offsets.Right, Offsets.Bottom));
	}
}

FCursorReply SDMXPixelMappingTransformHandle::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	EDMXPixelMappingTransformAction CurrentAction = Action;
	if ( CurrentAction == EDMXPixelMappingTransformAction::None )
	{
		CurrentAction = ComputeActionAtLocation(MyGeometry, MouseEvent);
	}

	switch ( TransformDirection )
	{
		case EDMXPixelMappingTransformDirection::BottomRight:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		case EDMXPixelMappingTransformDirection::BottomLeft:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
		case EDMXPixelMappingTransformDirection::BottomCenter:
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		case EDMXPixelMappingTransformDirection::CenterRight:
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	return FCursorReply::Unhandled();
}

FVector2D SDMXPixelMappingTransformHandle::ComputeDragDirection(EDMXPixelMappingTransformDirection InTransformDirection) const
{
	switch ( InTransformDirection )
	{
	case EDMXPixelMappingTransformDirection::CenterRight:
		return FVector2D(1, 0);

	case EDMXPixelMappingTransformDirection::BottomLeft:
		return FVector2D(-1, 1);
	case EDMXPixelMappingTransformDirection::BottomCenter:
		return FVector2D(0, 1);
	case EDMXPixelMappingTransformDirection::BottomRight:
		return FVector2D(1, 1);
	}

	return FVector2D(0, 0);
}

FVector2D SDMXPixelMappingTransformHandle::ComputeOrigin(EDMXPixelMappingTransformDirection InTransformDirection) const
{
	FVector2D Size(10, 10);

	switch ( InTransformDirection )
	{
	case EDMXPixelMappingTransformDirection::CenterRight:
		return Size * FVector2D(0, 0.5);

	case EDMXPixelMappingTransformDirection::BottomLeft:
		return Size * FVector2D(1, 0);
	case EDMXPixelMappingTransformDirection::BottomCenter:
		return Size * FVector2D(0.5, 0);
	case EDMXPixelMappingTransformDirection::BottomRight:
		return Size * FVector2D(0, 0);
	}

	return FVector2D(0, 0);
}

EDMXPixelMappingTransformAction SDMXPixelMappingTransformHandle::ComputeActionAtLocation(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	FVector2D GrabOriginOffset = LocalPosition - DragOrigin;
	if ( GrabOriginOffset.SizeSquared() < 36.f )
	{
		return EDMXPixelMappingTransformAction::Primary;
	}
	else
	{
		return EDMXPixelMappingTransformAction::Secondary;
	}
}

#undef LOCTEXT_NAMESPACE
