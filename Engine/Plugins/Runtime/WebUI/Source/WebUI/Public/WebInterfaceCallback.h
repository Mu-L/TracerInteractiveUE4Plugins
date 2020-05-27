// Copyright 2020 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "JsonLibrary.h"
#include "WebInterfaceCallback.generated.h"

class UWebInterface;

USTRUCT(BlueprintType)
struct WEBUI_API FWebInterfaceCallback
{
	friend class UWebInterface;
	friend class UWebInterfaceObject;

	GENERATED_USTRUCT_BODY()

	FWebInterfaceCallback();

protected:
	
	FWebInterfaceCallback( TWeakObjectPtr<UWebInterface> Interface, const FString& Callback );

public:
	
	bool IsValid() const;
	void Call( const FJsonLibraryValue& Data );

private:

	FString MyCallback;
	TWeakObjectPtr<UWebInterface> MyInterface;
};
