// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_hand_meshing.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandMeshingCreateClient)
#define MLHandMeshingCreateClient ::LUMIN_MLSDK_API::MLHandMeshingCreateClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandMeshingDestroyClient)
#define MLHandMeshingDestroyClient ::LUMIN_MLSDK_API::MLHandMeshingDestroyClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandMeshingRequestMesh)
#define MLHandMeshingRequestMesh ::LUMIN_MLSDK_API::MLHandMeshingRequestMeshShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandMeshingGetResult)
#define MLHandMeshingGetResult ::LUMIN_MLSDK_API::MLHandMeshingGetResultShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandMeshingFreeResource)
#define MLHandMeshingFreeResource ::LUMIN_MLSDK_API::MLHandMeshingFreeResourceShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
