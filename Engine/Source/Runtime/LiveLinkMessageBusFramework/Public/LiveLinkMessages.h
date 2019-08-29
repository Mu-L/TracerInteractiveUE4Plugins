﻿// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkTypes.h"
#include "LiveLinkMessages.generated.h"

USTRUCT()
struct FLiveLinkSubjectDataMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FLiveLinkRefSkeleton RefSkeleton;

	UPROPERTY()
	FName SubjectName;
};

USTRUCT()
struct FLiveLinkSubjectFrameMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName SubjectName;

	// Bone Transform data for this frame
	UPROPERTY()
	TArray<FTransform> Transforms;

	// Curve data for this frame
	UPROPERTY()
	TArray<FLiveLinkCurveElement> Curves;

	// Subject MetaData for this frame
	UPROPERTY()
	FLiveLinkMetaData MetaData;

	// Incrementing time for interpolation
	UPROPERTY()
	double Time;
};

USTRUCT()
struct FLiveLinkPingMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FGuid PollRequest;

	// default constructor for the receiver
	FLiveLinkPingMessage() = default;

	FLiveLinkPingMessage(const FGuid& InPollRequest) : PollRequest(InPollRequest) {}
};

USTRUCT()
struct FLiveLinkPongMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString ProviderName;

	UPROPERTY()
	FString MachineName;

	UPROPERTY()
	FGuid PollRequest;

	// default constructor for the receiver
	FLiveLinkPongMessage() = default;

	FLiveLinkPongMessage(const FString& InProviderName, const FString& InMachineName, const FGuid& InPollRequest) : ProviderName(InProviderName), MachineName(InMachineName), PollRequest(InPollRequest) {}
};

USTRUCT()
struct FLiveLinkConnectMessage
{
	GENERATED_USTRUCT_BODY()
};

USTRUCT()
struct FLiveLinkHeartbeatMessage
{
	GENERATED_USTRUCT_BODY()
};

USTRUCT()
struct FLiveLinkClearSubject
{
	GENERATED_USTRUCT_BODY()

	// Name of the subject to clear
	UPROPERTY()
	FName SubjectName;

	FLiveLinkClearSubject() {}
	FLiveLinkClearSubject(const FName& InSubjectName) : SubjectName(InSubjectName) {}
};
