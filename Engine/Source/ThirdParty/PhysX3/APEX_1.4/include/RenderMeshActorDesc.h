/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef RENDER_MESH_ACTOR_DESC_H
#define RENDER_MESH_ACTOR_DESC_H

/*!
\file
\brief class RenderMeshActorDesc
*/

#include "ApexDesc.h"
#include "UserRenderResourceManager.h"

namespace nvidia
{
namespace apex
{

PX_PUSH_PACK_DEFAULT

/**
\brief Descriptor for creating a rendering mesh (collection of parts and submesh extra data)
*/
class RenderMeshActorDesc : public ApexDesc
{
public:

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE RenderMeshActorDesc()
	{
		setToDefault();
	}

	/**
	\brief (re)sets the structure to the default.
	*/
	PX_INLINE void setToDefault()
	{
		visible = true;
		bufferVisibility = false;
		keepVisibleBonesPacked = false;
		renderWithoutSkinning = false;
		forceFallbackSkinning = false;
		maxInstanceCount = 0;
		indexBufferHint = RenderBufferHint::STATIC;
		overrideMaterials = NULL;
		overrideMaterialCount = 0;
		keepPreviousFrameBoneBuffer = false;
		forceBoneIndexChannel = false;
	}

	/**
	\brief Returns true if the descriptor is valid.
	\return true if the current settings are valid.
	*/
	PX_INLINE bool isValid() const
	{
		if (overrideMaterialCount != 0 && overrideMaterials == NULL)
		{
			return false;
		}
		return ApexDesc::isValid();
	}

	/**
	\brief Initial visibility of all parts.
	*/
	bool						visible;

	/**
	\brief If this set to true, render visibility will not be updated until the
	user calls syncVisibility().
	*/
	bool						bufferVisibility;

	/**
	\brief Pack visible bones

	If set, bone transform order will be maitained in an order that keeps visible bones
	contiguous.  This is more efficient for cases where there are large number of
	nonvisible parts (it reduces the number of  bone transforms that need to be updated
	in writeBuffer).  This only works when vertices are single-weighted, and
	the number of bones equals the number of parts.
	*/
	bool						keepVisibleBonesPacked;

	/**
	\brief Render without skinning

	If set, all vertices will be transformed by one transform, set using
	RenderMeshActor::setTM with boneIndex = 0 (the default).
	*/
	bool						renderWithoutSkinning;

	/**
	\brief Enforce the use of fallback skinning

	This will not create render resources with bone buffers since all the skinning will be done on the CPU already.
	Does not work if keepVisibleBones is set. These two features are mutually exclusive.
	*/
	bool						forceFallbackSkinning;

	/**
	\brief If maxInstanceCount = 0, mesh will be renedered without instancing.
	Otherwise, instance buffers (below) will be used.
	*/
	uint32_t					maxInstanceCount;

	/**
	\brief Hint passed along to the user describing whether the index buffer of
	this render mesh can be modified.
	*/
	RenderBufferHint::Enum		indexBufferHint;

	/**
	\brief Per-actor material names, to override those in the asset.
	The number of override material names is given by overrideMaterialCount.
	*/
	const char**				overrideMaterials;

	/**
	\brief Number of override material names in the overrideMaterials array.
	If this number is less than the number of materials in the asset, only
	the first overrideMaterialCount names will be overridden.  If this number
	is greater than the number of materials in the asset, the extra override
	material names will be ignored.
	*/
	uint32_t					overrideMaterialCount;

	/**
	\brief If true, the previous frame's bone buffer is kept and delivered
	during updateRenderResources as a second bone buffer.
	*/
	bool						keepPreviousFrameBoneBuffer;

	/**
	\brief If true, a static runtime bone index channel will be created.
	*/
	bool						forceBoneIndexChannel;
};

PX_POP_PACK

}
} // end namespace nvidia::apex

#endif // RENDER_MESH_ACTOR_DESC_H
