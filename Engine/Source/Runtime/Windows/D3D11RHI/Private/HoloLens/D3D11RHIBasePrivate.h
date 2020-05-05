// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11RHIBasePrivate.h: Private D3D RHI definitions for HoloLens.
=============================================================================*/

#pragma once

// Assume D3DX is available
#ifndef WITH_D3DX_LIBS
#define WITH_D3DX_LIBS	1
#endif

// Disable macro redefinition warning for compatibility with Windows SDK 8+
#pragma warning(push)
#if _MSC_VER >= 1700
#pragma warning(disable : 4005)	// macro redefinition
#endif

// D3D headers.
#if PLATFORM_64BITS
#pragma pack(push,16)
#else
#pragma pack(push,8)
#endif
#define D3D_OVERLOADS 1
#include "AllowWindowsPlatformTypes.h"
#include <D3D11_2.h>
#include "HideWindowsPlatformTypes.h"

#undef DrawText

#pragma pack(pop)
#pragma warning(pop)

typedef ID3D11DeviceContext FD3D11DeviceContext;
typedef ID3D11Device FD3D11Device;
