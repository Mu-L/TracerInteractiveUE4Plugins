// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Interfaces/ToolMode.h"

namespace BuildPatchTool
{
	class FAutomationToolModeFactory
	{
	public:
		static IToolModeRef Create(IBuildPatchServicesModule& BpsInterface);
	};
}

#endif // !UE_BUILD_SHIPPING
