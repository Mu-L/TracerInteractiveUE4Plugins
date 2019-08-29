// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2017 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#pragma once

#define NOMINMAX
#pragma warning(disable : 4668 4917 4365 4061 4005)
#if NV_XBOXONE
#include <d3d11_x.h>
#else
#include <d3d11.h>
#endif

namespace nv
{
namespace cloth
{
// implemented in DxFactory.cpp
void checkSuccessImpl(long, const char*, const int);
}

// safe dx calls
#define checkSuccess(err) cloth::checkSuccessImpl(err, __FILE__, __LINE__)
}

/*
#define NOMINMAX
#include "d3dcommon.h"

template<int length>
inline void setDebugObjectName(ID3D11DeviceChild* resource, const char (&name)[length])
{
#if PX_DEBUG
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, length - 1, name);
#endif
}
*/