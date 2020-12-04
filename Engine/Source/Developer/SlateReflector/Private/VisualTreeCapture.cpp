// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualTreeCapture.h"
#include "Debugging/SlateDebugging.h"
#include "Rendering/SlateRenderTransform.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Types/InvisibleToWidgetReflectorMetaData.h"

static float VectorSign(const FVector2D& Vec, const FVector2D& A, const FVector2D& B)
{
	return FMath::Sign((B.X - A.X) * (Vec.Y - A.Y) - (B.Y - A.Y) * (Vec.X - A.X));
}

// Returns true when the point is inside the triangle
// Should not return true when the point is on one of the edges
static bool IsPointInTriangle(const FVector2D& TestPoint, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	float BA = VectorSign(B, A, TestPoint);
	float CB = VectorSign(C, B, TestPoint);
	float AC = VectorSign(A, C, TestPoint);

	// point is in the same direction of all 3 tri edge lines
	// must be inside, regardless of tri winding
	return BA == CB && CB == AC;
}

FVisualEntry::FVisualEntry(int32 InElementIndex)
	: ElementIndex(InElementIndex)
{
}

void FVisualEntry::Resolve(const FSlateWindowElementList& ElementList)
{
	const FSlateDrawElement& Element = ElementList.GetUncachedDrawElements()[ElementIndex];
	const FSlateRenderTransform& Transform = Element.GetRenderTransform();
	const FVector2D& LocalSize = Element.GetLocalSize();

	TopLeft = Transform.TransformPoint(FVector2D(0, 0));
	TopRight = Transform.TransformPoint(FVector2D(LocalSize.X, 0));
	BottomLeft = Transform.TransformPoint(FVector2D(0, LocalSize.Y));
	BottomRight = Transform.TransformPoint(LocalSize);

	LayerId = Element.GetLayer();
	ClippingIndex = Element.GetPrecachedClippingIndex();
}

bool FVisualEntry::IsPointInside(const FVector2D& Point) const
{
	if (IsPointInTriangle(Point, TopLeft, TopRight, BottomLeft) || IsPointInTriangle(Point, BottomLeft, TopRight, BottomRight))
	{
		return true;
	}

	return false;
}

TSharedPtr<const SWidget> FVisualTreeSnapshot::Pick(FVector2D Point)
{
	for (int Index = Entries.Num() - 1; Index >= 0; Index--)
	{
		const FVisualEntry& Entry = Entries[Index];
		if (Entry.ClippingIndex != -1)
		{
			// TODO: This isn't always a valid index, but should be.
			if (ensure(ClippingStates.IsValidIndex(Entry.ClippingIndex)))
			{
				if (!ClippingStates[Entry.ClippingIndex].IsPointInside(Point))
				{
					continue;
				}
			}
		}

		if (!Entry.IsPointInside(Point))
		{
			continue;
		}

		TSharedPtr<const SWidget> Widget = Entry.Widget.Pin();

		//if (Widget->GetVisibility() == EVisibility::HitTestInvisible)
		//{
		//	continue;
		//}

		// Build List.
		return Widget;
	}

	return TSharedPtr<const SWidget>();
}

FVisualTreeCapture::FVisualTreeCapture()
	: bIsEnabled(false)
{
}

FVisualTreeCapture::~FVisualTreeCapture()
{
	Disable();
}

void FVisualTreeCapture::Enable()
{
#if WITH_SLATE_DEBUGGING
	if (ensure(bIsEnabled == false))
	{
		FSlateApplication::Get().OnWindowBeingDestroyed().AddRaw(this, &FVisualTreeCapture::OnWindowBeingDestroyed);
		FSlateDebugging::BeginWindow.AddRaw(this, &FVisualTreeCapture::BeginWindow);
		FSlateDebugging::EndWindow.AddRaw(this, &FVisualTreeCapture::EndWindow);
		FSlateDebugging::BeginWidgetPaint.AddRaw(this, &FVisualTreeCapture::BeginWidgetPaint);
		FSlateDebugging::EndWidgetPaint.AddRaw(this, &FVisualTreeCapture::EndWidgetPaint);
		FSlateDebugging::ElementAdded.AddRaw(this, &FVisualTreeCapture::ElementAdded);
		bIsEnabled = true;
	}
#endif
}

void FVisualTreeCapture::Disable()
{
#if WITH_SLATE_DEBUGGING
	if (bIsEnabled)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnWindowBeingDestroyed().RemoveAll(this);
		}
		FSlateDebugging::BeginWindow.RemoveAll(this);
		FSlateDebugging::EndWindow.RemoveAll(this);
		FSlateDebugging::BeginWidgetPaint.RemoveAll(this);
		FSlateDebugging::EndWidgetPaint.RemoveAll(this);
		FSlateDebugging::ElementAdded.RemoveAll(this);
		bIsEnabled = false;
	}
#endif
}

void FVisualTreeCapture::Reset()
{
	VisualTrees.Reset();
}

TSharedPtr<FVisualTreeSnapshot> FVisualTreeCapture::GetVisualTreeForWindow(SWindow* InWindow)
{
	return VisualTrees.FindRef(InWindow);
}

void FVisualTreeCapture::BeginWindow(const FSlateWindowElementList& ElementList)
{
	TSharedPtr<FVisualTreeSnapshot> Tree = VisualTrees.FindRef(ElementList.GetPaintWindow());
	if (!Tree.IsValid())
	{
		Tree = MakeShared<FVisualTreeSnapshot>();
		VisualTrees.Add(ElementList.GetPaintWindow(), Tree);
	}

	Tree->Entries.Reset();
	Tree->ClippingStates.Reset();
}

void FVisualTreeCapture::EndWindow(const FSlateWindowElementList& ElementList)
{
	TSharedPtr<FVisualTreeSnapshot> Tree = VisualTrees.FindRef(ElementList.GetPaintWindow());
	if (Tree.IsValid())
	{
		for (FVisualEntry& Entry : Tree->Entries)
		{
			Entry.Resolve(ElementList);
		}

		Tree->ClippingStates = ElementList.GetClippingManager().GetClippingStates();
		Tree->Entries.Sort([](const FVisualEntry& A, const FVisualEntry& B) {
			return A.LayerId < B.LayerId;
		});
	}
}

void FVisualTreeCapture::BeginWidgetPaint(const SWidget* Widget, const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, const FSlateWindowElementList& ElementList, int32 LayerId)
{
	TSharedPtr<FVisualTreeSnapshot> Tree = VisualTrees.FindRef(ElementList.GetPaintWindow());
	if (Tree.IsValid())
	{
		Tree->WidgetStack.Push(Widget->AsShared());
	}
}

void FVisualTreeCapture::EndWidgetPaint(const SWidget* Widget, const FSlateWindowElementList& ElementList, int32 LayerId)
{
	TSharedPtr<FVisualTreeSnapshot> Tree = VisualTrees.FindRef(ElementList.GetPaintWindow());
	if (Tree.IsValid())
	{
		Tree->WidgetStack.Pop();
	}
}

void FVisualTreeCapture::ElementAdded(const FSlateWindowElementList& ElementList, int32 InElementIndex)
{
	TSharedPtr<FVisualTreeSnapshot> Tree = VisualTrees.FindRef(ElementList.GetPaintWindow());
	if (Tree.IsValid())
	{
		if (Tree->WidgetStack.Num() > 0)
		{
			// Ignore any element added from a widget that's invisible to the widget reflector.
			if (Tree->WidgetStack.Top().Pin()->GetMetaData<FInvisibleToWidgetReflectorMetaData>())
			{
				return;
			}

			FVisualEntry Entry(InElementIndex);
			Entry.Widget = Tree->WidgetStack.Top();
			Tree->Entries.Add(Entry);
		}
	}
}

void FVisualTreeCapture::OnWindowBeingDestroyed(const SWindow& WindowBeingDestoyed)
{
	VisualTrees.Remove(&WindowBeingDestoyed);
}
