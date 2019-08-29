/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef MODULE_CONVERSIONDESTRUCTIBLEACTORPARAM_0P22_0P23H_H
#define MODULE_CONVERSIONDESTRUCTIBLEACTORPARAM_0P22_0P23H_H

#include "NvParamConversionTemplate.h"
#include "DestructibleActorParam_0p22.h"
#include "DestructibleActorParam_0p23.h"

namespace nvidia {
namespace apex {
namespace legacy {


typedef NvParameterized::ParamConversionTemplate<nvidia::parameterized::DestructibleActorParam_0p22, 
						nvidia::parameterized::DestructibleActorParam_0p23, 
						nvidia::parameterized::DestructibleActorParam_0p22::ClassVersion, 
						nvidia::parameterized::DestructibleActorParam_0p23::ClassVersion>
						ConversionDestructibleActorParam_0p22_0p23Parent;

class ConversionDestructibleActorParam_0p22_0p23: public ConversionDestructibleActorParam_0p22_0p23Parent
{
public:
	static NvParameterized::Conversion* Create(NvParameterized::Traits* t)
	{
		void* buf = t->alloc(sizeof(ConversionDestructibleActorParam_0p22_0p23));
		return buf ? PX_PLACEMENT_NEW(buf, ConversionDestructibleActorParam_0p22_0p23)(t) : 0;
	}

protected:
	ConversionDestructibleActorParam_0p22_0p23(NvParameterized::Traits* t) : ConversionDestructibleActorParam_0p22_0p23Parent(t) {}

	const NvParameterized::PrefVer* getPreferredVersions() const
	{
		static NvParameterized::PrefVer prefVers[] =
		{
			//TODO:
			//	Add your preferred versions for included references here.
			//	Entry format is
			//		{ (const char*)longName, (uint32_t)preferredVersion }

			{ 0, 0 } // Terminator (do not remove!)
		};

		return prefVers;
	}

	bool convert()
	{
		//TODO:
		//	Write custom conversion code here using mNewData and mLegacyData members.
		//
		//	Note that
		//		- mNewData has already been initialized with default values
		//		- same-named/same-typed members have already been copied
		//			from mLegacyData to mNewData
		//		- included references were moved to mNewData
		//			(and updated to preferred versions according to getPreferredVersions)
		//
		//	For more info see the versioning wiki.

		return true;
	}
};


}
}
} //nvidia::apex::legacy

#endif
