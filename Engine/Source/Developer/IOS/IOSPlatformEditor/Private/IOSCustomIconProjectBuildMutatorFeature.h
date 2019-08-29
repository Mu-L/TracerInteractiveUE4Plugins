// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProjectBuildMutatorFeature.h"

class FIOSCustomIconProjectBuildMutatorFeature : public FProjectBuildMutatorFeature
{
public:

	virtual bool RequiresProjectBuild(FName InPlatformInfoName) const override;

private:
};