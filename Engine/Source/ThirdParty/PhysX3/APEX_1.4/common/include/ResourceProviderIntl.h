/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef RESOURCE_PROVIDER_INTL_H
#define RESOURCE_PROVIDER_INTL_H

#include "ResourceProvider.h"
#include "ApexString.h"
#include "PsUserAllocated.h"

/**
Framework/Module interface to named resource provider
*/

namespace nvidia
{
namespace apex
{

typedef uint32_t ResID;

const ResID INVALID_RESOURCE_ID = ResID(-1);

/* Used for storing asset name/resID pairs */
class AssetNameIDMapping : public UserAllocated
{
public:
	AssetNameIDMapping()
	{}

	AssetNameIDMapping(const char* inAssetName, const char* inIosAssetTypeName, ResID inResID):
		assetName(inAssetName),
		iosAssetTypeName(inIosAssetTypeName),
		resID(inResID),
		isOpaqueMesh(false)
	{}

	AssetNameIDMapping(const char* inAssetName, const char* inIosAssetTypeName):
		assetName(inAssetName),
		iosAssetTypeName(inIosAssetTypeName),
		resID(INVALID_RESOURCE_ID),
		isOpaqueMesh(false)
	{}

	AssetNameIDMapping(const char* inAssetName, ResID inResID):
		assetName(inAssetName),
		iosAssetTypeName(""),
		resID(inResID),
		isOpaqueMesh(false)
	{}

	AssetNameIDMapping(const char* inAssetName, bool _isOpaqueMesh):
		assetName(inAssetName),
		iosAssetTypeName(""),
		resID(INVALID_RESOURCE_ID),
		isOpaqueMesh(_isOpaqueMesh)
	{}

	void setIsOpaqueMesh(bool state)
	{
		isOpaqueMesh = state;
	}

	ApexSimpleString		assetName;
	ApexSimpleString		iosAssetTypeName;
	ResID					resID;
	bool					isOpaqueMesh;
};


class ResourceProviderIntl : public ResourceProvider
{
public:
	/**
	The Apex framework and modules can create name spaces in which unique names can be stored.
	The user setResource() interface can also implicitly create new name spaces if they set a
	resource in a new name space.  This function will return the existing ID in this case.
	The calling code must store this handle and call releaseResource() when appropriate.
	releaseAtExit determines whether the NRP will call releaseResource() on items in this
	namespace when the ApexSDK exits.
	*/
	virtual ResID createNameSpace(const char* nameSpace, bool releaseAtExit = true) = 0;

	/**
	The Apex Authorable Object needs a way to tell the resource provider that the resource
	value is no longer set and the app's callback should be used once again if the name
	is queried
	*/
	virtual void setResource(const char* nameSpace, const char* name, void* resource, bool valueIsSet, bool incRefCount = false) = 0;

	/**
	The Apex framework and modules should use this function to release their reference to a named
	resource when they no longer use it.  If the named resource's reference count reaches zero,
	ResourceCallback::releaseResource() will be called.
	*/
	virtual void releaseResource(ResID id) = 0;

	/**
	Create a named resource inside a specific name space.  Returns a resource ID which must be
	stored by the calling module or framework code.
	*/
	virtual ResID createResource(ResID nameSpace, const char* name, bool refCount = true) = 0;

	/**
	Returns true if named resource has a specified pointer
	*/
	virtual bool checkResource(ResID nameSpace, const char* name) = 0;

	/**
	Returns true if named resource has a specified pointer
	*/
	virtual bool checkResource(ResID id) = 0;

	/**
	Modifies name parameter such that it is unique in its namespace
	*/
	virtual void generateUniqueName(ResID nameSpace, ApexSimpleString& name) = 0;

	/**
	Retrieve the named resource pointer, which has been provided by the user interface.  If the
	named resource has never been set by the user API and the request callback has been specified,
	the callback will be called to provide the void*.  The user callback will be called only once
	per named resource.
	*/
	virtual void* getResource(ResID id) = 0;

	/**
	Retrieve the named resource name.
	*/
	virtual const char* getResourceName(ResID id) = 0;

	/**
	Given a namespace name, this method will fill in all of the resource IDs registered
	in the namespace.  'inCount' contains the total space allocated for the 'outResIDs' list.
	'outCount' will contain how many IDs are in the 'outResIDs' list.  If 'inCount' is not
	large enough, the method will return false
	*/
	virtual bool getResourceIDs(const char* nameSpace, ResID* outResIDs, uint32_t& outCount, uint32_t inCount) = 0;

	/**
	\brief Returns if the resource provider is operating in a case sensitive mode.

	\note By default the resource provider is NOT case sensitive
	*/
	virtual bool isCaseSensitive() = 0;

	/**
	Retrieve the named resource name space.
	*/
	virtual const char* getResourceNameSpace(ResID id) = 0;

};

} // namespace apex
} // namespace nvidia

#endif // RESOURCE_PROVIDER_INTL_H
