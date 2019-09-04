// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Bus/MessageBus.h"
#include "HAL/RunnableThread.h"
#include "Bus/MessageRouter.h"
#include "Bus/MessageContext.h"
#include "Bus/MessageSubscription.h"
#include "IMessageSender.h"


/* FMessageBus structors
 *****************************************************************************/

FMessageBus::FMessageBus(const TSharedPtr<IAuthorizeMessageRecipients>& InRecipientAuthorizer)
	: RecipientAuthorizer(InRecipientAuthorizer)
{
	Router = new FMessageRouter();
	RouterThread = FRunnableThread::Create(Router, TEXT("FMessageBus.Router"), 128 * 1024, TPri_Normal, FPlatformAffinity::GetPoolThreadMask());

	check(Router != nullptr);
}


FMessageBus::~FMessageBus()
{
	Shutdown();

	delete Router;
}


/* IMessageBus interface
 *****************************************************************************/

void FMessageBus::Forward(
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context,
	const TArray<FMessageAddress>& Recipients,
	const FTimespan& Delay,
	const TSharedRef<IMessageSender, ESPMode::ThreadSafe>& Forwarder
)
{
	Router->RouteMessage(MakeShareable(new FMessageContext(
		Context,
		Forwarder->GetSenderAddress(),
		Recipients,
		EMessageScope::Process,
		FDateTime::UtcNow() + Delay,
		FTaskGraphInterface::Get().GetCurrentThreadIfKnown()
	)));
}


TSharedRef<IMessageTracer, ESPMode::ThreadSafe> FMessageBus::GetTracer()
{
	return Router->GetTracer();
}


void FMessageBus::Intercept(const TSharedRef<IMessageInterceptor, ESPMode::ThreadSafe>& Interceptor, const FName& MessageType)
{
	if (MessageType == NAME_None)
	{
		return;
	}

	if (!RecipientAuthorizer.IsValid() || RecipientAuthorizer->AuthorizeInterceptor(Interceptor, MessageType))
	{
		Router->AddInterceptor(Interceptor, MessageType);
	}			
}


FOnMessageBusShutdown& FMessageBus::OnShutdown()
{
	return ShutdownDelegate;
}


void FMessageBus::Publish(
	void* Message,
	UScriptStruct* TypeInfo,
	EMessageScope Scope,
	const TMap<FName, FString>& Annotations,
	const FTimespan& Delay,
	const FDateTime& Expiration,
	const TSharedRef<IMessageSender, ESPMode::ThreadSafe>& Publisher
)
{
	Router->RouteMessage(MakeShared<FMessageContext, ESPMode::ThreadSafe>(
		Message,
		TypeInfo,
		Annotations,
		nullptr,
		Publisher->GetSenderAddress(),
		TArray<FMessageAddress>(),
		Scope,
		EMessageFlags::None,
		FDateTime::UtcNow() + Delay,
		Expiration,
		FTaskGraphInterface::Get().GetCurrentThreadIfKnown()
	));
}


void FMessageBus::Register(const FMessageAddress& Address, const TSharedRef<IMessageReceiver, ESPMode::ThreadSafe>& Recipient)
{
	Router->AddRecipient(Address, Recipient);
}


void FMessageBus::Send(
	void* Message,
	UScriptStruct* TypeInfo,
	EMessageFlags Flags,
	const TMap<FName, FString>& Annotations,
	const TSharedPtr<IMessageAttachment, ESPMode::ThreadSafe>& Attachment,
	const TArray<FMessageAddress>& Recipients,
	const FTimespan& Delay,
	const FDateTime& Expiration,
	const TSharedRef<IMessageSender, ESPMode::ThreadSafe>& Sender
)
{
	Router->RouteMessage(MakeShared<FMessageContext, ESPMode::ThreadSafe>(
		Message,
		TypeInfo,
		Annotations,
		Attachment,
		Sender->GetSenderAddress(),
		Recipients,
		EMessageScope::Network,
		Flags,
		FDateTime::UtcNow() + Delay,
		Expiration,
		FTaskGraphInterface::Get().GetCurrentThreadIfKnown()
	));
}


void FMessageBus::Shutdown()
{
	if (RouterThread != nullptr)
	{
		ShutdownDelegate.Broadcast();

		RouterThread->Kill(true);
		delete RouterThread;
		RouterThread = nullptr;
	}
}


TSharedPtr<IMessageSubscription, ESPMode::ThreadSafe> FMessageBus::Subscribe(
	const TSharedRef<IMessageReceiver, ESPMode::ThreadSafe>& Subscriber,
	const FName& MessageType,
	const FMessageScopeRange& ScopeRange
)
{
	if (MessageType != NAME_None)
	{
		if (!RecipientAuthorizer.IsValid() || RecipientAuthorizer->AuthorizeSubscription(Subscriber, MessageType))
		{
			TSharedRef<IMessageSubscription, ESPMode::ThreadSafe> Subscription = MakeShareable(new FMessageSubscription(Subscriber, MessageType, ScopeRange));
			Router->AddSubscription(Subscription);

			return Subscription;
		}
	}

	return nullptr;
}


void FMessageBus::Unintercept(const TSharedRef<IMessageInterceptor, ESPMode::ThreadSafe>& Interceptor, const FName& MessageType)
{
	if (MessageType != NAME_None)
	{
		Router->RemoveInterceptor(Interceptor, MessageType);
	}
}


void FMessageBus::Unregister(const FMessageAddress& Address)
{
	if (!RecipientAuthorizer.IsValid() || RecipientAuthorizer->AuthorizeUnregistration(Address))
	{
		Router->RemoveRecipient(Address);
	}
}


void FMessageBus::Unsubscribe(const TSharedRef<IMessageReceiver, ESPMode::ThreadSafe>& Subscriber, const FName& MessageType)
{
	if (MessageType != NAME_None)
	{
		if (!RecipientAuthorizer.IsValid() || RecipientAuthorizer->AuthorizeUnsubscription(Subscriber, MessageType))
		{
			Router->RemoveSubscription(Subscriber, MessageType);
		}
	}
}

void FMessageBus::AddNotificationListener(const TSharedRef<IBusListener, ESPMode::ThreadSafe>& Listener)
{
	Router->AddNotificationListener(Listener);
}

void FMessageBus::RemoveNotificationListener(const TSharedRef<IBusListener, ESPMode::ThreadSafe>& Listener)
{
	Router->RemoveNotificationListener(Listener);
}
