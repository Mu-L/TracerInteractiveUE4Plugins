// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "IMessageContext.h"
#include "IMessageAttachment.h"
#include "UdpMessagingTestTypes.generated.h"

USTRUCT()
struct FUdpMockMessage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<uint8> Data;

	FUdpMockMessage()
	{
		Data.AddUninitialized(64);
	}

	FUdpMockMessage(int32 DataSize)
	{
		Data.AddUninitialized(DataSize);
	}
};


class FUdpMockMessageContext
	: public IMessageContext
{
public:

	FUdpMockMessageContext(FUdpMockMessage* InMessage, const FDateTime& InTimeSent)
		: Expiration(FDateTime::MaxValue())
		, Message(InMessage)
		, Scope(EMessageScope::Network)
		, SenderThread(ENamedThreads::AnyThread)
		, TimeSent(InTimeSent)
		, TypeInfo(FUdpMockMessage::StaticStruct())
	{
		FMessageAddress::Parse(TEXT("11111111-22222222-33333333-44444444"), Sender);
	}

	~FUdpMockMessageContext()
	{
		if (Message != nullptr)
		{
			if (TypeInfo.IsValid())
			{
				TypeInfo->DestroyStruct(Message);
			}

			FMemory::Free(Message);
		}
	}

public:

	//~ IMessageContext interface

	virtual const TMap<FName, FString>& GetAnnotations() const override { return Annotations; }
	virtual TSharedPtr<IMessageAttachment, ESPMode::ThreadSafe> GetAttachment() const override { return Attachment; }
	virtual const FDateTime& GetExpiration() const override { return Expiration; }
	virtual const void* GetMessage() const override { return Message; }
	virtual const TWeakObjectPtr<UScriptStruct>& GetMessageTypeInfo() const override { return TypeInfo; }
	virtual TSharedPtr<IMessageContext, ESPMode::ThreadSafe> GetOriginalContext() const override { return OriginalContext; }
	virtual const TArray<FMessageAddress>& GetRecipients() const override { return Recipients; }
	virtual EMessageScope GetScope() const override { return Scope; }
	virtual const FMessageAddress& GetSender() const override { return Sender; }
	virtual ENamedThreads::Type GetSenderThread() const override { return SenderThread; }
	virtual const FDateTime& GetTimeForwarded() const override { return TimeSent; }
	virtual const FDateTime& GetTimeSent() const override { return TimeSent; }

private:

	TMap<FName, FString> Annotations;
	TSharedPtr<IMessageAttachment, ESPMode::ThreadSafe> Attachment;
	FDateTime Expiration;
	void* Message;
	TSharedPtr<IMessageContext, ESPMode::ThreadSafe> OriginalContext;
	TArray<FMessageAddress> Recipients;
	EMessageScope Scope;
	FMessageAddress Sender;
	ENamedThreads::Type SenderThread;
	FDateTime TimeSent;
	TWeakObjectPtr<UScriptStruct> TypeInfo;
};
