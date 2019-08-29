/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef RENDER_MESH_H
#define RENDER_MESH_H

/*!
\file
\brief classes RenderSubmesh, VertexBuffer, and MaterialNamingConvention enums
*/

#include "ApexUsingNamespace.h"
#include "VertexFormat.h"

namespace nvidia
{
namespace apex
{

PX_PUSH_PACK_DEFAULT

class RenderMeshActor;
class Context;
struct VertexUV;


/**
\brief a vertex buffer that supports multiple formats
 */
class VertexBuffer
{
public:
	/**
	\brief Returns the number of vertices in the buffer
	*/
	virtual uint32_t				getVertexCount() const = 0;

	/**
	\brief Returns the data format.  See VertexFormat.
	*/
	virtual const VertexFormat&		getFormat() const = 0;

	/**
	\brief Returns the data format.  See VertexFormat. Can be changed.
	*/
	virtual VertexFormat&			getFormatWritable() = 0;

	/**
	\brief Accessor for the data buffer indexed by bufferIndex. To get the buffer format, use getFormat().getBufferFormat( index ).
	If the data channel doesn't exist then this function returns NULL.
	*/
	virtual const void*				getBuffer(uint32_t bufferIndex) const = 0;

	/**
	\brief Like getBuffer(), but also returns the buffer's format.
	*/
	virtual const void*				getBufferAndFormat(RenderDataFormat::Enum& format, uint32_t bufferIndex) const = 0;

	/**
	\brief Like getBuffer(), but also returns the buffer's format. Can be changed.
	*/
	virtual void*					getBufferAndFormatWritable(RenderDataFormat::Enum& format, uint32_t bufferIndex) = 0;

	/**
	\brief Accessor for data in a desired format from the buffer indexed by bufferIndex. If the channel does not exist, or if it is in
	a format for which there is not presently a converter to the the desired format dstBufferFormat, this function returns false.
	The dstBufferStride field must be at least the size of the dstBufferFormat data, or zero (in which case the stride is assumed to be
	the size of the dstBufferFormat data).  If neither of these conditions hold, this function returns false.
	Otherwise, dstBuffer is filled in with elementCount elements of the converted data, starting from startVertexIndex, withe the given stride.
	*/
	virtual bool					getBufferData(void* dstBuffer, nvidia::RenderDataFormat::Enum dstBufferFormat, uint32_t dstBufferStride, uint32_t bufferIndex,
												  uint32_t startVertexIndex, uint32_t elementCount) const = 0;

protected:
	/* Do not allow class to be created directly */
	VertexBuffer() {}
};


/**
\brief a mesh that has only one material (or render state, in general)
 */
class RenderSubmesh
{
public:
	virtual							~RenderSubmesh() {}

	/**
		Returns the number of vertices associated with the indexed part.
	*/
	virtual uint32_t				getVertexCount(uint32_t partIndex) const = 0;

	/**
		Returns the submesh's vertex buffer (contains all parts' vertices)
	*/
	virtual const VertexBuffer&		getVertexBuffer() const = 0;

	/**
		Returns the submesh's index buffer (contains all parts' vertices). Can be changed.
	*/
	virtual VertexBuffer&			getVertexBufferWritable() = 0;

	/**
		Vertices for a given part are contiguous within the vertex buffer.  This function
		returns the first vertex index for the indexed part.
	*/
	virtual uint32_t				getFirstVertexIndex(uint32_t partIndex) const = 0;

	/**
		Returns the number of indices in the part's index buffer.
	*/
	virtual uint32_t				getIndexCount(uint32_t partIndex) const = 0;

	/**
		Returns the index buffer associated with the indexed part.
	*/
	virtual const uint32_t*			getIndexBuffer(uint32_t partIndex) const = 0;

	/**
		Returns an array of smoothing groups for the given part, if one exists.  Otherwise, returns NULL.
		If not NULL, the size of the array is the number of triangles in the part.  Since only triangle
		lists are currently supported, the size of this array is getIndexCount(partIndex)/3.
	*/
	virtual const uint32_t*			getSmoothingGroups(uint32_t partIndex) const = 0;

protected:
	RenderSubmesh() {}
};

PX_POP_PACK

}
} // end namespace nvidia::apex

#endif // RENDER_MESH_H
