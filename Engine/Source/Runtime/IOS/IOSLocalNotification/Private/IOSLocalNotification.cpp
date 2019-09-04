// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 	IOSLocalNotification.cpp: Unreal IOSLocalNotification service interface object.
 =============================================================================*/

/*------------------------------------------------------------------------------------
	Includes
 ------------------------------------------------------------------------------------*/

#include "IOSLocalNotification.h"

#include "IOS/IOSApplication.h"
#include "IOS/IOSAppDelegate.h"

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

#import <UserNotifications/UserNotifications.h>

DEFINE_LOG_CATEGORY(LogIOSLocalNotification);

class FIOSLocalNotificationModule : public ILocalNotificationModule
{
public:

	/** Creates a new instance of the audio device implemented by the module. */
	virtual ILocalNotificationService* GetLocalNotificationService() override
	{
		static ILocalNotificationService*	oneTrueLocalNotificationService = nullptr;
		
		if(oneTrueLocalNotificationService == nullptr)
		{
			oneTrueLocalNotificationService = new FIOSLocalNotificationService;
		}
		
		return oneTrueLocalNotificationService;
	}

#if !PLATFORM_TVOS
	static UNMutableNotificationContent* CreateNotificationContent(const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent, uint32 BadgeNumber)
	{
		UNMutableNotificationContent* Content = [UNMutableNotificationContent new];
		if(Content != nil)
		{
			if(!Title.IsEmpty())
			{
				NSString* NotificationTitle = [NSString stringWithFString:Title.ToString()];
				if(NotificationTitle != nil)
				{
					Content.title = NotificationTitle;
				}
			}
			
			if(!Body.IsEmpty())
			{
				NSString* NotificationBody = [NSString stringWithFString:Body.ToString()];
				if(NotificationBody != nil)
				{
					Content.body = NotificationBody;
				}
			}
			
			NSNumber* BadgeNSNumber = [NSNumber numberWithInt:BadgeNumber];
			Content.badge = BadgeNSNumber;
			Content.sound = [UNNotificationSound defaultSound];
			
			if(!ActivationEvent.IsEmpty())
			{
				NSString* ActivationEventString = [NSString stringWithFString:ActivationEvent];
				NSString* LocalString = [NSString stringWithFString:FString(TEXT("Local"))];
				if (ActivationEventString != nil && LocalString != nil)
				{
					NSDictionary* Dict = [NSDictionary dictionaryWithObjectsAndKeys: ActivationEventString, @"ActivationEvent", LocalString, @"NotificationType", nil];
					if (Dict != nil)
					{
						Content.userInfo = Dict;
					}
				}
			}
		}
		
		return Content;
	}
	static UNCalendarNotificationTrigger* CreateCalendarNotificationTrigger(const FDateTime& FireDateTime)
	{
		NSCalendar *calendar = [NSCalendar autoupdatingCurrentCalendar];
		NSDateComponents *dateComps = [[NSDateComponents alloc] init];
		[dateComps setDay : FireDateTime.GetDay()];
		[dateComps setMonth : FireDateTime.GetMonth()];
		[dateComps setYear : FireDateTime.GetYear()];
		[dateComps setHour : FireDateTime.GetHour()];
		[dateComps setMinute : FireDateTime.GetMinute()];
		[dateComps setSecond : FireDateTime.GetSecond()];
		
		UNCalendarNotificationTrigger *trigger = [UNCalendarNotificationTrigger triggerWithDateMatchingComponents:dateComps repeats:NO];
		
		return trigger;
	}
#endif
};

IMPLEMENT_MODULE(FIOSLocalNotificationModule, IOSLocalNotification);

/*------------------------------------------------------------------------------------
	FIOSLocalNotification
 ------------------------------------------------------------------------------------*/
FIOSLocalNotificationService::FIOSLocalNotificationService()
{
	AppLaunchedWithNotification = false;
	LaunchNotificationFireDate = 0;
}

void FIOSLocalNotificationService::ClearAllLocalNotifications()
{
#if !PLATFORM_TVOS
	UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
	
	[Center removeAllPendingNotificationRequests];
#endif
}

static int32 NotificationNumber = 0;
int32 FIOSLocalNotificationService::ScheduleLocalNotificationAtTime(const FDateTime& FireDateTime, bool LocalTime, const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent)
{
#if !PLATFORM_TVOS
    //Create local copies of these for the block to capture
    FDateTime FireDateTimeCopy = FireDateTime;
    FText TitleCopy = Title;
    FText BodyCopy = Body;
    FText ActionCopy = Action;
    FString ActivationEventCopy = ActivationEvent;
	int32 CurrentNotificationId = NotificationNumber++;
	
	//have to schedule notification on main thread queue
	dispatch_async(dispatch_get_main_queue(), ^{
        UNMutableNotificationContent* Content = FIOSLocalNotificationModule::CreateNotificationContent(TitleCopy, BodyCopy, ActionCopy, ActivationEventCopy, 1);
        UNCalendarNotificationTrigger* Trigger = FIOSLocalNotificationModule::CreateCalendarNotificationTrigger(FireDateTimeCopy);
        
        UNNotificationRequest *Request = [UNNotificationRequest requestWithIdentifier:@(CurrentNotificationId).stringValue content:Content trigger:Trigger];

		UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
        [Center addNotificationRequest : Request withCompletionHandler : ^ (NSError * _Nullable error) {
            if (error != nil) {
                UE_LOG(LogIOSLocalNotification, Warning, TEXT("Error scheduling notification: %d"), CurrentNotificationId);
            }
        }];
    });
	return CurrentNotificationId;
#else
	return -1;
#endif
}

int32 FIOSLocalNotificationService::ScheduleLocalNotificationBadgeAtTime(const FDateTime& FireDateTime, bool LocalTime, const FString& ActivationEvent)
{
#if !PLATFORM_TVOS
	FDateTime FireDateTimeCopy = FireDateTime;
	FString ActivationEventCopy = ActivationEvent;
	int32 CurrentNotificationId = NotificationNumber++;
	
	//have to schedule notification on main thread queue
	dispatch_async(dispatch_get_main_queue(), ^{
		UNMutableNotificationContent* Content = FIOSLocalNotificationModule::CreateNotificationContent(FText(), FText(), FText(), ActivationEventCopy, 1);
		UNCalendarNotificationTrigger* Trigger = FIOSLocalNotificationModule::CreateCalendarNotificationTrigger(FireDateTime);
		
		UNNotificationRequest *Request = [UNNotificationRequest requestWithIdentifier:@(CurrentNotificationId).stringValue content:Content trigger:Trigger];

		UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
		[Center addNotificationRequest:Request withCompletionHandler:^(NSError * _Nullable error) {
			if (error != nil) {
				UE_LOG(LogIOSLocalNotification, Warning, TEXT("Error scheduling notification: %d"), CurrentNotificationId);
			}
		}];
	});
	
	return CurrentNotificationId;
#else
	return -1;
#endif
}

void FIOSLocalNotificationService::CancelLocalNotification(const FString& ActivationEvent)
{
	// TODO
}

void FIOSLocalNotificationService::CancelLocalNotification(int32 NotificationId)
{
#if !PLATFORM_TVOS
	UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
	[Center removePendingNotificationRequestsWithIdentifiers:@[@(NotificationId).stringValue]];
#endif
}

void FIOSLocalNotificationService::GetLaunchNotification(bool& NotificationLaunchedApp, FString& ActivationEvent, int32& FireDate)
{
	NotificationLaunchedApp = AppLaunchedWithNotification;
	ActivationEvent = LaunchNotificationActivationEvent;
	FireDate = LaunchNotificationFireDate;
}

void FIOSLocalNotificationService::SetLaunchNotification(FString const& ActivationEvent, int32 FireDate)
{
	AppLaunchedWithNotification = true;
	LaunchNotificationActivationEvent = ActivationEvent;
	LaunchNotificationFireDate = FireDate;
}


static FIOSLocalNotificationService::FAllowedNotifications NotificationsAllowedDelegate;
void FIOSLocalNotificationService::CheckAllowedNotifications(const FAllowedNotifications& AllowedNotificationsDelegate)
{
	NotificationsAllowedDelegate = AllowedNotificationsDelegate;
	
#if !PLATFORM_TVOS
	UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
	[Center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings * _Nonnull settings) {
		bool NotificationsAllowed = settings.authorizationStatus == UNAuthorizationStatusAuthorized;
		NotificationsAllowedDelegate.ExecuteIfBound(NotificationsAllowed);
	}];
#else
	NotificationsAllowedDelegate.ExecuteIfBound(false);
#endif
}
