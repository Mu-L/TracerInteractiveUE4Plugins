// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "USDMemory.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

class SWidget;

namespace UsdUtils
{
	enum class EBrowseFileMode
	{
		Open,
		Save
	};

	/** Inserts the SubLayerFile path into ParentLayer as a sublayer */
	USDUTILITIES_API bool InsertSubLayer( const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* SubLayerFile );

	/** Opens a file dialog to open or save a USD file */
	USDUTILITIES_API TOptional< FString > BrowseUsdFile( EBrowseFileMode Mode, TSharedRef< const SWidget > OriginatingWidget );

	/** Creates a new layer with a default prim */
	USDUTILITIES_API TUsdStore< pxr::SdfLayerRefPtr > CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TUsdStore<pxr::SdfLayerRefPtr>& ParentLayer, const TCHAR* LayerFilePath );
}

#endif // #if USE_USD_SDK
