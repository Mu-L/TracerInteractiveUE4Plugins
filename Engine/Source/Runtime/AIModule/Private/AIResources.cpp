// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AIResources.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
FString UAIResource_Movement::GenerateDebugDescription() const
{
	return TEXT("Move");
}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
