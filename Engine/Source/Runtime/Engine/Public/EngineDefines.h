// Copyright Epic Games, Inc. All Rights Reserved.
// 
// Public defines form the Engine

#pragma once

#include "CoreMinimal.h"

/*-----------------------------------------------------------------------------
	Configuration defines
-----------------------------------------------------------------------------*/

#ifndef ENABLE_VISUAL_LOG
	#define ENABLE_VISUAL_LOG (PLATFORM_DESKTOP && !NO_LOGGING && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))
#endif

// Whether lightmass generates FSHVector2 or FSHVector3. Linked with VER_UE4_INDIRECT_LIGHTING_SH3
#define NUM_INDIRECT_LIGHTING_SH_COEFFICIENTS 9

// The number of lights to consider for sky/atmospheric light scattering
#define NUM_ATMOSPHERE_LIGHTS 2

/*-----------------------------------------------------------------------------
	Size of the world.
-----------------------------------------------------------------------------*/

#define WORLD_MAX					2097152.0				/* Maximum size of the world */
#define HALF_WORLD_MAX				(WORLD_MAX * 0.5)		/* Half the maximum size of the world */
#define HALF_WORLD_MAX1				(HALF_WORLD_MAX - 1.0)	/* Half the maximum size of the world minus one */

#define DEFAULT_ORTHOZOOM			10000.0					/* Default 2D viewport zoom */
