// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterModule.h"

#include "USDMemory.h"

class FUsdExporterModule : public IUsdExporterModule
{
};

IMPLEMENT_MODULE_USD( FUsdExporterModule, USDExporter );
