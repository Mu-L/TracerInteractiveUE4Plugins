// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_ANDROID_JNI

#include "CoreMinimal.h"
#include "AndroidJSScripting.h"
#include "Backends/JsonStructSerializerBackend.h"

class UObject;

/**
 * Implements a writer for UStruct serialization using JavaScript.
 *
 * Based on FJsonStructSerializerBackend, it adds support for certain object types not representable in pure JSON
 *
 */
class FAndroidJSStructSerializerBackend
	: public FJsonStructSerializerBackend
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InScripting An instance of a web browser scripting obnject.
	 */
	FAndroidJSStructSerializerBackend(FAndroidJSScriptingRef InScripting);

public:
	virtual void WriteProperty(const FStructSerializerState& State, int32 ArrayIndex = 0) override;

	FString ToString();

private:
	void WriteUObject(const FStructSerializerState& State, UObject* Value);

	FAndroidJSScriptingRef Scripting;
	TArray<uint8> ReturnBuffer;
	FMemoryWriter Writer;
};

#endif // USE_ANDROID_JNI