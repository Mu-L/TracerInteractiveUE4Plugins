// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ScopeLock.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace NotificationManagerConstants
{
	// Offsets from the bottom-left corner of the work area
	const FVector2D NotificationOffset( 15.0f, 15.0f );
}

FSlateNotificationManager::FRegionalNotificationList::FRegionalNotificationList(const FSlateRect& InRectangle)
	: Region(InRectangle)
{

}

void FSlateNotificationManager::FRegionalNotificationList::RemoveDeadNotifications()
{
	// Iterate backwards and remove anything that has finished
	for (int32 ListIndex = Notifications.Num() - 1; ListIndex >= 0; --ListIndex)
	{
		if (Notifications[ListIndex]->bDone)
		{
			TSharedPtr<SWindow> PinnedWindow = Notifications[ListIndex]->ParentWindowPtr.Pin();
			if( PinnedWindow.IsValid() )
			{
				PinnedWindow->RequestDestroyWindow();
			}

			Notifications.RemoveAt(ListIndex);
		}
	}
}

void FSlateNotificationManager::FRegionalNotificationList::Arrange()
{
	FVector2D AnchorPoint(
		Region.Right - NotificationManagerConstants::NotificationOffset.X,
		Region.Bottom - NotificationManagerConstants::NotificationOffset.Y );

	for (int32 ListIndex = Notifications.Num() - 1; ListIndex >= 0; --ListIndex)
	{
		TSharedPtr<SWindow> PinnedWindow = Notifications[ListIndex]->ParentWindowPtr.Pin();
		if( PinnedWindow.IsValid() )
		{
			const FVector2D DesiredSize = PinnedWindow->GetDesiredSize();
			const FVector2D NewPosition(AnchorPoint.X - DesiredSize.X, AnchorPoint.Y - DesiredSize.Y);
			if( NewPosition != PinnedWindow->GetPositionInScreen() && DesiredSize != PinnedWindow->GetSizeInScreen() )
			{
				PinnedWindow->ReshapeWindow( NewPosition, DesiredSize );
			}
			else if( NewPosition != PinnedWindow->GetPositionInScreen() )
			{
				float StackOffset = NotificationManagerConstants::NotificationOffset.Y * ((Notifications.Num()-1) - ListIndex);
				PinnedWindow->MoveWindowTo(NewPosition - FVector2D(0, StackOffset));
			}
			AnchorPoint.Y -= DesiredSize.Y;
		}
	}
}

FSlateNotificationManager& FSlateNotificationManager::Get()
{
	static FSlateNotificationManager Instance;
	return Instance;
}

FSlateNotificationManager::FSlateNotificationManager()
{
	FCoreDelegates::OnPreExit.AddRaw(this, &FSlateNotificationManager::ShutdownOnPreExit);
}

void FSlateNotificationManager::ShutdownOnPreExit()
{
	FCoreDelegates::OnPreExit.RemoveAll(this);

	RegionalLists.Empty();
}

void FSlateNotificationManager::SetRootWindow( const TSharedRef<SWindow> InRootWindow )
{
	RootWindowPtr = InRootWindow;
}

TSharedRef<SNotificationList> FSlateNotificationManager::CreateStackForArea(const FSlateRect& InRectangle)
{
	TSharedRef<SNotificationList> NotificationList = SNew(SNotificationList);
	TSharedRef<SWindow> NotificationWindow = SWindow::MakeNotificationWindow();
	NotificationWindow->SetContent(NotificationList);
	NotificationList->ParentWindowPtr = NotificationWindow;

	if( RootWindowPtr.IsValid() )
	{
		FSlateApplication::Get().AddWindowAsNativeChild( NotificationWindow, RootWindowPtr.Pin().ToSharedRef() );
	}
	else
	{
		FSlateApplication::Get().AddWindow( NotificationWindow );
	}

	if( !FSlateApplication::Get().GetActiveModalWindow().IsValid() )
	{
		if ( NotificationWindow->IsActive() || NotificationWindow->HasActiveParent() )
		{
			NotificationWindow->BringToFront();
		}
	}

	bool bFound = false;
	for (FRegionalNotificationList& List : RegionalLists)
	{
		if (FSlateRect::IsRectangleContained(List.Region, InRectangle))
		{
			List.Notifications.Add(NotificationList);
			bFound = true;
		}
	}

	if (!bFound)
	{
		FRegionalNotificationList NewList(FSlateApplication::Get().GetWorkArea(InRectangle));
		NewList.Notifications.Add(NotificationList);
		RegionalLists.Add(NewList);
	}

	return NotificationList;
}

TSharedPtr<SNotificationItem> FSlateNotificationManager::AddNotification(const FNotificationInfo& Info)
{
	check(IsInGameThread() || !"FSlateNotificationManager::AddNotification must be called on game thread. Use QueueNotification if necessary.");

	// Early calls of this function can happen before Slate is initialized.
	if( FSlateApplication::IsInitialized() )
	{
		FSlateRect PreferredWorkArea;
		// Retrieve the main editor window to display the notication in, otherwise use the preferred work area
		if (RootWindowPtr.IsValid())
		{
			PreferredWorkArea = FSlateApplication::Get().GetWorkArea(RootWindowPtr.Pin()->GetRectInScreen());
		}
		else
		{
			PreferredWorkArea = FSlateApplication::Get().GetPreferredWorkArea();
		}

		TSharedRef<SNotificationList> List = CreateStackForArea(PreferredWorkArea);

		return List->AddNotification(Info);
	}

	return nullptr;
}

void FSlateNotificationManager::QueueNotification(FNotificationInfo* Info)
{
	PendingNotifications.Push(Info);
}

void FSlateNotificationManager::GetWindows(TArray< TSharedRef<SWindow> >& OutWindows) const
{
	for (const FRegionalNotificationList& RegionList : RegionalLists)
	{
		for (const auto& NotificationList : RegionList.Notifications)
		{
			TSharedPtr<SWindow> PinnedWindow = NotificationList->ParentWindowPtr.Pin();
			if( PinnedWindow.IsValid() )
			{
				OutWindows.Add(PinnedWindow.ToSharedRef());
			}
		}
	}
}

void FSlateNotificationManager::Tick()
{
	// Ensure that the region rectangles still match the screen work areas.
	// This is necessary if the desktop configuration has changed
	for (auto& RegionList : RegionalLists)
	{
		RegionList.Region = FSlateApplication::Get().GetWorkArea(RegionList.Region);
	}

	for (;;)
	{
		FNotificationInfo* Notification = PendingNotifications.Pop();
		if (!Notification)
		{
			break;
		}

		AddNotification(*Notification);
		delete Notification;
	}

	// Check notifications to see if any have timed out & need to be removed.
	// We need to do this here as we cant remove their windows in the normal
	// window-tick callstack (as the SlateWindows array gets corrupted)
	
	// We don't need to worry about duplicates here as there is always a unique list per-region
	for (int32 RegionIndex = RegionalLists.Num() - 1; RegionIndex >= 0; --RegionIndex)
	{
		RegionalLists[RegionIndex].RemoveDeadNotifications();

		if (RegionalLists[RegionIndex].Notifications.Num() == 0)
		{
			// It's empty, so remove it
			RegionalLists.RemoveAt(RegionIndex);
		}
		else
		{
			// Arrange the notifications in the list
			RegionalLists[RegionIndex].Arrange();
		}
	}
}

void FSlateNotificationManager::ForceNotificationsInFront( const TSharedRef<SWindow> InWindow )
{
	// check to see if this is a re-entrant call from one of our windows
	for (const auto& RegionList : RegionalLists)
	{
		for (auto& Notification : RegionList.Notifications)
		{
			TSharedPtr<SWindow> PinnedWindow = Notification->ParentWindowPtr.Pin();
			if( PinnedWindow.IsValid() && InWindow == PinnedWindow )
			{
				return;
			}
		}
	}

	const bool IsActiveModelWindowNotValid = !FSlateApplication::Get().GetActiveModalWindow().IsValid();
	// now bring all of our windows back to the front
	for (const auto& RegionList : RegionalLists)
	{
		for (auto& Notification : RegionList.Notifications)
		{
			TSharedPtr<SWindow> PinnedWindow = Notification->ParentWindowPtr.Pin();
			if( PinnedWindow.IsValid() && IsActiveModelWindowNotValid )
			{
				PinnedWindow->BringToFront();
			}
		}
	}
}
