/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef RECORDING_RENDER_RESOURCE_MANAGER_H
#define RECORDING_RENDER_RESOURCE_MANAGER_H

#include "UserRenderResourceManager.h"
#include "UserRenderer.h"

#include <string>

namespace nvidia
{
namespace apex
{
class RenderVertexBufferData;
//class RenderBoneBufferData; // not possible, d'oh
}
}

#include "UserRenderBoneBuffer.h"


class RecordingRenderResourceManager : public nvidia::apex::UserRenderResourceManager
{
public:

	class RecorderInterface
	{
	public:
		virtual ~RecorderInterface() {}

		virtual void createVertexBuffer(unsigned int id, const nvidia::apex::UserRenderVertexBufferDesc& desc) = 0;
		virtual void writeVertexBuffer(unsigned int id, const nvidia::apex::RenderVertexBufferData& data, unsigned int firstVertex, unsigned int numVertices) = 0;
		virtual void releaseVertexBuffer(unsigned int id) = 0;

		virtual void createIndexBuffer(unsigned int id, const nvidia::apex::UserRenderIndexBufferDesc& desc) = 0;
		virtual void writeIndexBuffer(unsigned int id, const void* srcData, uint32_t srcStride, unsigned int firstDestElement, unsigned int numElements, nvidia::apex::RenderDataFormat::Enum format) = 0;
		virtual void releaseIndexBuffer(unsigned int id) = 0;

		virtual void createBoneBuffer(unsigned int id, const nvidia::apex::UserRenderBoneBufferDesc& desc) = 0;
		virtual void writeBoneBuffer(unsigned int id, const nvidia::apex::RenderBoneBufferData& data, unsigned int firstBone, unsigned int numBones) = 0;
		virtual void releaseBoneBuffer(unsigned int id) = 0;

		virtual void createResource(unsigned int id, const nvidia::apex::UserRenderResourceDesc& desc) = 0;
		virtual void renderResource(unsigned int id, const nvidia::apex::UserRenderResourceDesc& desc) = 0;
		virtual void releaseResource(unsigned int id) = 0;

		virtual void setMaxBonesForMaterial(void* material, unsigned int maxBones) = 0;

	};

	RecordingRenderResourceManager(nvidia::apex::UserRenderResourceManager* child, bool ownsChild, RecorderInterface* recorder);
	~RecordingRenderResourceManager();


	virtual nvidia::apex::UserRenderVertexBuffer*   createVertexBuffer(const nvidia::apex::UserRenderVertexBufferDesc& desc);
	virtual void                                     releaseVertexBuffer(nvidia::apex::UserRenderVertexBuffer& buffer);

	virtual nvidia::apex::UserRenderIndexBuffer*    createIndexBuffer(const nvidia::apex::UserRenderIndexBufferDesc& desc);
	virtual void                                     releaseIndexBuffer(nvidia::apex::UserRenderIndexBuffer& buffer);

	virtual nvidia::apex::UserRenderBoneBuffer*     createBoneBuffer(const nvidia::apex::UserRenderBoneBufferDesc& desc);
	virtual void                                     releaseBoneBuffer(nvidia::apex::UserRenderBoneBuffer& buffer);

	virtual nvidia::apex::UserRenderInstanceBuffer* createInstanceBuffer(const nvidia::apex::UserRenderInstanceBufferDesc& desc);
	virtual void                                     releaseInstanceBuffer(nvidia::apex::UserRenderInstanceBuffer& buffer);

	virtual nvidia::apex::UserRenderSpriteBuffer*   createSpriteBuffer(const nvidia::apex::UserRenderSpriteBufferDesc& desc);
	virtual void                                     releaseSpriteBuffer(nvidia::apex::UserRenderSpriteBuffer& buffer);

	virtual nvidia::apex::UserRenderSurfaceBuffer*  createSurfaceBuffer(const nvidia::apex::UserRenderSurfaceBufferDesc& desc);
	virtual void									 releaseSurfaceBuffer(nvidia::apex::UserRenderSurfaceBuffer& buffer);

	virtual nvidia::apex::UserRenderResource*       createResource(const nvidia::apex::UserRenderResourceDesc& desc);

	virtual void                                     releaseResource(nvidia::apex::UserRenderResource& resource);

	virtual uint32_t                             getMaxBonesForMaterial(void* material);

	/** \brief Get the sprite layout data */
	virtual bool getSpriteLayoutData(uint32_t spriteCount, uint32_t spriteSemanticsBitmap, nvidia::apex::UserRenderSpriteBufferDesc* textureDescArray)
	{
		PX_ALWAYS_ASSERT(); // TODO TODO TODO : This needs to be implemented.
		PX_UNUSED(spriteCount);
		PX_UNUSED(spriteSemanticsBitmap);
		PX_UNUSED(textureDescArray);
		return false;
	}

	/** \brief Get the instance layout data */
	virtual bool getInstanceLayoutData(uint32_t spriteCount, uint32_t spriteSemanticsBitmap, nvidia::apex::UserRenderInstanceBufferDesc* instanceDescArray)
	{
		PX_ALWAYS_ASSERT(); // TODO TODO TODO : This needs to be implemented.
		PX_UNUSED(spriteCount);
		PX_UNUSED(spriteSemanticsBitmap);
		PX_UNUSED(instanceDescArray);
		return false;
	}
protected:

	nvidia::apex::UserRenderResourceManager* mChild;
	bool mOwnsChild;

	RecorderInterface* mRecorder;
};


class RecordingRenderer : public nvidia::apex::UserRenderer
{
public:
	RecordingRenderer(nvidia::apex::UserRenderer* child, RecordingRenderResourceManager::RecorderInterface* recorder);
	virtual ~RecordingRenderer();

	virtual void renderResource(const nvidia::apex::RenderContext& context);

protected:
	nvidia::apex::UserRenderer* mChild;
	RecordingRenderResourceManager::RecorderInterface* mRecorder;
};


class FileRecorder : public RecordingRenderResourceManager::RecorderInterface
{
public:
	FileRecorder(const char* filename);
	~FileRecorder();

	virtual void createVertexBuffer(unsigned int id, const nvidia::apex::UserRenderVertexBufferDesc& desc);
	virtual void writeVertexBuffer(unsigned int id, const nvidia::apex::RenderVertexBufferData& data, unsigned int firstVertex, unsigned int numVertices);
	virtual void releaseVertexBuffer(unsigned int id);

	virtual void createIndexBuffer(unsigned int id, const nvidia::apex::UserRenderIndexBufferDesc& desc);
	virtual void writeIndexBuffer(unsigned int id, const void* srcData, uint32_t srcStride, unsigned int firstDestElement, unsigned int numElements, nvidia::apex::RenderDataFormat::Enum format);
	virtual void releaseIndexBuffer(unsigned int id);

	virtual void createBoneBuffer(unsigned int id, const nvidia::apex::UserRenderBoneBufferDesc& desc);
	virtual void writeBoneBuffer(unsigned int id, const nvidia::apex::RenderBoneBufferData& data, unsigned int firstBone, unsigned int numBones);
	virtual void releaseBoneBuffer(unsigned int id);

	virtual void createResource(unsigned int id, const nvidia::apex::UserRenderResourceDesc& desc);
	virtual void renderResource(unsigned int id, const nvidia::apex::UserRenderResourceDesc& desc);
	virtual void releaseResource(unsigned int id);

	virtual void setMaxBonesForMaterial(void* material, unsigned int maxBones);

protected:
	void writeElem(const char* name, unsigned int value);

	void writeBufferData(const void* data, unsigned int stride, unsigned int numElements, nvidia::apex::RenderDataFormat::Enum format);
	void writeBufferDataFloat(const void* data, unsigned int stride, unsigned int numElements, unsigned int numFloatsPerDataSet);
	void writeBufferDataShort(const void* data, unsigned int stride, unsigned int numElements, unsigned int numFloatsPerDataSet);
	void writeBufferDataLong(const void* data, unsigned int stride, unsigned int numElements, unsigned int numFloatsPerDataSet);

	FILE* mOutputFile;
};


#endif // RECORDING_RENDER_RESOURCE_MANAGER_H
