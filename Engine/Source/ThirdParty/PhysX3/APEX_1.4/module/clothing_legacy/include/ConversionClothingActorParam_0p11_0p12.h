/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef MODULE_CONVERSIONCLOTHINGACTORPARAM_0P11_0P12H_H
#define MODULE_CONVERSIONCLOTHINGACTORPARAM_0P11_0P12H_H

#include "NvParamConversionTemplate.h"
#include "ClothingActorParam_0p11.h"
#include "ClothingActorParam_0p12.h"

namespace nvidia {
namespace apex {
namespace legacy {


typedef NvParameterized::ParamConversionTemplate<nvidia::parameterized::ClothingActorParam_0p11, 
						nvidia::parameterized::ClothingActorParam_0p12, 
						nvidia::parameterized::ClothingActorParam_0p11::ClassVersion, 
						nvidia::parameterized::ClothingActorParam_0p12::ClassVersion>
						ConversionClothingActorParam_0p11_0p12Parent;

class ConversionClothingActorParam_0p11_0p12: public ConversionClothingActorParam_0p11_0p12Parent
{
public:
	static NvParameterized::Conversion* Create(NvParameterized::Traits* t)
	{
		void* buf = t->alloc(sizeof(ConversionClothingActorParam_0p11_0p12));
		return buf ? PX_PLACEMENT_NEW(buf, ConversionClothingActorParam_0p11_0p12)(t) : 0;
	}

protected:
	ConversionClothingActorParam_0p11_0p12(NvParameterized::Traits* t) : ConversionClothingActorParam_0p11_0p12Parent(t) {}

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
