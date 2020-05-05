// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkBasicRole.h"

FLiveLinkRoleProjectSetting::FLiveLinkRoleProjectSetting()
	: SettingClass(ULiveLinkSubjectSettings::StaticClass())
{}


ULiveLinkSettings::ULiveLinkSettings()
	: ClockOffsetCorrectionStep(100e-6)
	, DefaultMessageBusSourceMode(ELiveLinkSourceMode::EngineTime)
	, MessageBusPingRequestFrequency(1.0)
	, MessageBusHeartbeatFrequency(1.0)
	, MessageBusHeartbeatTimeout(2.0)
	, MessageBusTimeBeforeRemovingInactiveSource(30.0)
	, TimeWithoutFrameToBeConsiderAsInvalid(0.5)
	, ValidColor(FLinearColor::Green)
	, InvalidColor(FLinearColor::Yellow)
	, TextSizeSource(16)
	, TextSizeSubject(12)
{
}

FLiveLinkRoleProjectSetting ULiveLinkSettings::GetDefaultSettingForRole(TSubclassOf<ULiveLinkRole> Role) const
{
	int32 IndexOf = DefaultRoleSettings.IndexOfByPredicate([Role](const FLiveLinkRoleProjectSetting& Other) {return Other.Role == Role; });
	if (IndexOf != INDEX_NONE)
	{
		return DefaultRoleSettings[IndexOf];
	}
	FLiveLinkRoleProjectSetting Result;
	Result.Role = Role;
	return Result;
}
