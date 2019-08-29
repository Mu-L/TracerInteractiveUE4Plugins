/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef USER_RENDER_RESOURCE_DESC_H
#define USER_RENDER_RESOURCE_DESC_H

/*!
\file
\brief class UserRenderResourceDesc
*/

#include "UserRenderResourceManager.h"
#include "foundation/PxAssert.h"

namespace nvidia
{
namespace apex
{

PX_PUSH_PACK_DEFAULT

class Renderable;
class UserOpaqueMesh;
class UserRenderVertexBuffer;
class UserRenderIndexBuffer;
class UserRenderBoneBuffer;
class UserRenderInstanceBuffer;
class UserRenderSpriteBuffer;

/**
\brief Describes all the data that makes up a renderable resource
*/
class UserRenderResourceDesc
{
public:
	UserRenderResourceDesc(void)
	{
		firstVertex      = 0;
		numVerts         = 0;

		indexBuffer      = 0;
		firstIndex       = 0;
		numIndices       = 0;

		boneBuffer       = 0;
		firstBone        = 0;
		numBones         = 0;

		instanceBuffer   = 0;
		firstInstance    = 0;
		numInstances     = 0;

		spriteBuffer     = 0;
		firstSprite      = 0;
		numSprites       = 0;
		visibleSpriteCount = 0;

		material         = 0;
		submeshIndex	 = 0;

		userRenderData   = 0;

		numVertexBuffers = 0;
		vertexBuffers     = NULL;

		cullMode         = RenderCullMode::CLOCKWISE;
		primitives       = RenderPrimitiveType::UNKNOWN;

		opaqueMesh		= NULL;
	}

	/**
	\brief Checks if the resource is valid
	*/
	bool isValid(void) const
	{
		uint32_t numFailed = 0;
		if (numVertexBuffers >= 255)
		{
			numFailed++;
		}
		if (numIndices   && !indexBuffer)
		{
			numFailed++;
		}
		if (numBones     && !boneBuffer)
		{
			numFailed++;
		}
		if (numInstances && !instanceBuffer)
		{
			numFailed++;
		}
		if (numSprites   && !spriteBuffer)
		{
			numFailed++;
		}
		PX_ASSERT(numFailed == 0);
		return numFailed == 0;
	}

public:
	UserOpaqueMesh* 			opaqueMesh;			//!< A user specified opaque mesh interface.
	UserRenderVertexBuffer**	vertexBuffers;		//!< vertex buffers used when rendering this resource.
	//!< there should be no overlap in semantics between any two VBs.
	uint32_t					numVertexBuffers;	//!< number of vertex buffers used when rendering this resource.

	uint32_t					firstVertex;		//!< First vertex to render
	uint32_t					numVerts;			//!< Number of vertices to render

	UserRenderIndexBuffer*		indexBuffer;		//!< optional index buffer used when rendering this resource.
	uint32_t					firstIndex;			//!< First index to render
	uint32_t					numIndices;			//!< Number of indices to render

	UserRenderBoneBuffer*		boneBuffer;			//!< optional bone buffer used for skinned meshes.
	uint32_t					firstBone;			//!< First bone to render
	uint32_t					numBones;			//!< Number of bones to render

	UserRenderInstanceBuffer*	instanceBuffer;	//!< optional instance buffer if rendering multiple instances of the same resource.
	uint32_t					firstInstance;		//!< First instance to render
	uint32_t					numInstances;		//!< Number of instances to render

	UserRenderSpriteBuffer*		spriteBuffer;		//!< optional sprite buffer if rendering sprites
	uint32_t					firstSprite;		//!< First sprite to render
	uint32_t					numSprites;			//!< Number of sprites to render
	uint32_t					visibleSpriteCount; //!< If the sprite buffer is using the view direction modifier; this will represent the number of sprites visible in front of the camera (Not necessarily in the frustum but in front of the camera)

	UserRenderSurfaceBuffer**	surfaceBuffers;		//!< optional surface buffer for transferring variable to texture	
	uint32_t					numSurfaceBuffers;	//!< Number of surface buffers to render
	uint32_t					widthSurfaceBuffers;//!< The surface buffer width
	uint32_t					heightSurfaceBuffers;//!< The surface buffer height

	void*						material;			//!< user defined material used when rendering this resource.
	uint32_t					submeshIndex;		//!< the index of the submesh that render resource belongs to

	//! user defined pointer originally passed in to Renderable::updateRenderResources(..)
	void*						userRenderData;

	RenderCullMode::Enum		cullMode;			//!< Triangle culling mode
	RenderPrimitiveType::Enum	primitives;			//!< Rendering primitive type (triangle, line strip, etc)
};

PX_POP_PACK

}
} // end namespace nvidia::apex

#endif
