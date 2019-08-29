// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"
#include "Misc/TextFilter.h"

class IPlugin;

typedef TTextFilter< const IPlugin* > FPluginTextFilter;
