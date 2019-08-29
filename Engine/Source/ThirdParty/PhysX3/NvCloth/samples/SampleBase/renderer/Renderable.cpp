/*
* Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Renderable.h"
#include "Renderer.h"
#include "RenderUtils.h"

const DirectX::XMFLOAT4 DEFAULT_COLOR(0.5f, 0.5f, 0.5f, 1.0f);

Renderable::Renderable(IRenderMesh& mesh, RenderMaterial& material) : m_mesh(mesh), m_scale(1, 1, 1), m_color(DEFAULT_COLOR), m_hidden(false), m_transform(PxIdentity)
{
	setMaterial(material);
}

void Renderable::setMaterial(RenderMaterial& material)
{
	m_materialInstance = material.getMaterialInstance(&m_mesh);
}

void Renderable::render(Renderer& renderer, bool depthStencilOnly) const
{
	if (!m_materialInstance->isValid())
	{
		PX_ALWAYS_ASSERT();
		return;
	}

	m_materialInstance->bind(*renderer.m_context, 0, depthStencilOnly);

	// setup object CB
	{
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		renderer.m_context->Map(renderer.m_objectCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		Renderer::CBObject* objectBuffer = (Renderer::CBObject*)mappedResource.pData;
		objectBuffer->world = PxMat44ToXMMATRIX(getModelMatrix());
		objectBuffer->color = getColor();
		renderer.m_context->Unmap(renderer.m_objectCB, 0);
	}

	m_mesh.render(*renderer.m_context);
}
