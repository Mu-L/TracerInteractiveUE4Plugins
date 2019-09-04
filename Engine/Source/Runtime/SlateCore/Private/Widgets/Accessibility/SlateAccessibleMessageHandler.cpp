// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#include "Widgets/Accessibility/SlateAccessibleWidgetCache.h"
#include "Application/SlateApplicationBase.h"
#include "Application/SlateWindowHelper.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"
#include "HAL/IConsoleManager.h"

DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Tick"), STAT_AccessibilitySlateTick, STATGROUP_Accessibility);
DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Event Raised"), STAT_AccessibilitySlateEventRaised, STATGROUP_Accessibility);

FSlateAccessibleMessageHandler::FSlateAccessibleMessageHandler()
	: FGenericAccessibleMessageHandler()
	, bDirty(false)
{
	bApplicationIsAccessible = true;
}

void FSlateAccessibleMessageHandler::OnActivate()
{
	bDirty = true;
}

void FSlateAccessibleMessageHandler::OnDeactivate()
{
	FSlateAccessibleWidgetCache::ClearAll();
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleMessageHandler::GetAccessibleWindow(const TSharedRef<FGenericWindow>& InWindow) const
{
	if (IsActive())
	{
		TSharedPtr<SWindow> SlateWindow = FSlateWindowHelper::FindWindowByPlatformWindow(FSlateApplicationBase::Get().GetTopLevelWindows(), InWindow);
		return FSlateAccessibleWidgetCache::GetAccessibleWidgetChecked(SlateWindow);
	}
	return nullptr;
}

AccessibleWidgetId FSlateAccessibleMessageHandler::GetAccessibleWindowId(const TSharedRef<FGenericWindow>& InWindow) const
{
	TSharedPtr<IAccessibleWidget> AccessibleWindow = GetAccessibleWindow(InWindow);
	if (AccessibleWindow.IsValid())
	{
		return AccessibleWindow->GetId();
	}
	return IAccessibleWidget::InvalidAccessibleWidgetId;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleMessageHandler::GetAccessibleWidgetFromId(AccessibleWidgetId Id) const
{
	return FSlateAccessibleWidgetCache::GetAccessibleWidgetFromId(Id);
}

void FSlateAccessibleMessageHandler::OnWidgetRemoved(SWidget* Widget)
{
	if (IsActive())
	{
		TSharedPtr<FSlateAccessibleWidget> RemovedWidget = FSlateAccessibleWidgetCache::RemoveWidget(Widget);
		if (RemovedWidget.IsValid())
		{
			RaiseEvent(RemovedWidget.ToSharedRef(), EAccessibleEvent::WidgetRemoved);
			// If this ensure fails, bDirty = true must be called to ensure the tree is kept up to date.
			ensureMsgf(!Widget->GetParentWidget().IsValid(), TEXT("A widget was unexpectedly deleted before detaching from its parent."));
		}
	}
}

void FSlateAccessibleMessageHandler::OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateEventRaised);
		// todo: not sure what to do for a case like focus changed to not-accessible widget. maybe pass through a nullptr?
		if (Widget->IsAccessible())
		{
			FSlateAccessibleMessageHandler::RaiseEvent(FSlateAccessibleWidgetCache::GetAccessibleWidget(Widget), Event, OldValue, NewValue);
		}
	}
}

int32 GAccessibleWidgetsProcessedPerTick = 100;
FAutoConsoleVariableRef AccessibleWidgetsProcessedPerTickRef(
	TEXT("Slate.AccessibleWidgetsProcessedPerTick"),
	GAccessibleWidgetsProcessedPerTick,
	TEXT("To reduce performance spikes, generating the accessible widget tree is limited to this many widgets per tick to update.")
);

void FSlateAccessibleMessageHandler::Tick()
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateTick);
		if (bDirty && ToProcess.Num() == 0)
		{
			bDirty = false;
			// Process ALL windows, not just the top level ones. Otherwise we miss things like combo boxes.
			TArray<TSharedRef<SWindow>> SlateWindows = FSlateApplicationBase::Get().GetTopLevelWindows();
			while (SlateWindows.Num() > 0)
			{
				const TSharedRef<SWindow> CurrentWindow = SlateWindows.Pop(false);
				ToProcess.Emplace(CurrentWindow, FSlateAccessibleWidgetCache::GetAccessibleWidget(CurrentWindow));
				SlateWindows.Append(CurrentWindow->GetChildWindows());
			}
		}

		if (ToProcess.Num() > 0)
		{
			for (int32 Counter = 0; ToProcess.Num() > 0 && Counter < GAccessibleWidgetsProcessedPerTick; ++Counter)
			{
				FWidgetAndParent WidgetAndParent = ToProcess.Pop(false);
				if (WidgetAndParent.Widget.IsValid())
				{
					TSharedPtr<SWidget> SharedWidget = WidgetAndParent.Widget.Pin();
					if (SharedWidget->CanChildrenBeAccessible())
					{
						FChildren* SharedChildren = SharedWidget->GetChildren();
						for (int32 i = 0; i < SharedChildren->Num(); ++i)
						{
							TSharedRef<SWidget> Child = SharedChildren->GetChildAt(i);
							if (Child->GetAccessibleBehavior() != EAccessibleBehavior::NotAccessible)
							{
								TSharedRef<FSlateAccessibleWidget> AccessibleChild = FSlateAccessibleWidgetCache::GetAccessibleWidget(Child);
								AccessibleChild->SiblingIndex = WidgetAndParent.Parent->ChildrenBuffer.Num();
								AccessibleChild->UpdateParent(WidgetAndParent.Parent);
								// A separate children buffer is filled instead of the children array itself
								// so that accessibility queries still work (using the old data) while updating
								// accessible widget data.
								WidgetAndParent.Parent->ChildrenBuffer.Add(AccessibleChild);
								ToProcess.Emplace(Child, AccessibleChild);
							}
							else
							{
								// Keep a reference to the last-known accessible parent
								ToProcess.Emplace(Child, WidgetAndParent.Parent);
							}
						}
					}
				}
			}

			// Once processing is finished, update each widget's children array with its children buffer
			if (ToProcess.Num() == 0)
			{
				for (auto WidgetIterator = FSlateAccessibleWidgetCache::GetAllWidgets(); WidgetIterator; ++WidgetIterator)
				{
					TSharedRef<FSlateAccessibleWidget> AccessibleWidget = WidgetIterator.Value();
					AccessibleWidget->Children = MoveTemp(AccessibleWidget->ChildrenBuffer);
				}
			}
		}
	}
}

#endif
