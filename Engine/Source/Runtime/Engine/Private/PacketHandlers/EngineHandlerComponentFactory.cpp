// Copyright Epic Games, Inc. All Rights Reserved.

#include "PacketHandlers/EngineHandlerComponentFactory.h"
#include "PacketHandlers/StatelessConnectHandlerComponent.h"


/**
 * UEngineHandlerComponentFactor
 */
UEngineHandlerComponentFactory::UEngineHandlerComponentFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedPtr<HandlerComponent> UEngineHandlerComponentFactory::CreateComponentInstance(FString& Options)
{
	if (Options == TEXT("StatelessConnectHandlerComponent"))
	{
		return MakeShareable(new StatelessConnectHandlerComponent);
	}

	return nullptr;
}
