// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"

namespace ImmediatePhysics_PhysX
{
	struct FSimulation;

	/** handle associated with a physics joint. This is the proper way to read/write to the physics simulation */
	struct ENGINE_API FJointHandle
	{
	private:
		FSimulation& OwningSimulation;
		int32 JointDataIndex;

		friend FSimulation;
		FJointHandle(FSimulation& InOwningSimulation, int32 InJointDataIndex)
			: OwningSimulation(InOwningSimulation)
			, JointDataIndex(InJointDataIndex)
		{
		}

		~FJointHandle()
		{
		}

		FJointHandle(const FJointHandle&);	//Ensure no copying of handles

	};

}

#endif // WITH_PHYSX
