/*
* Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef RENDERER_H
#define RENDERER_H

#include "RenderMaterial.h"
#include <DirectXMath.h>
#include "XInput.h"
#include "DXUTMisc.h"
#include "DXUTCamera.h"
#include "SampleManager.h"
#include "Utils.h"
#include "ResourceManager.h"
#include "PrimitiveRenderMesh.h"
#include "RendererShadow.h"
#include "RendererHBAO.h"
#include <unordered_set>

class CFirstPersonCamera;
class PhysXPrimitive;
class RenderDebugImpl;

namespace physx
{
class PxRenderBuffer;
};

/**
3D World Renderer
- use createRenderable() to add objects to render.
- use queueRenderBuffer() every frame to render debug primitives.
- contains ResourceManager to search for file and load resources.
- contains RendererShadow and RendererHBAO, use them through getters to control shadows.
*/
class Renderer : public ISampleController
{
	friend class Renderable;

  public:
	//////// ctor ////////

	Renderer();
	~Renderer();


	//////// public API ////////

	void reloadShaders();

	bool getWireframeMode()
	{
		return m_wireframeMode;
	}

	void setWireframeMode(bool enabled)
	{
		if(m_wireframeMode != enabled)
		{
			m_wireframeMode = enabled;
			initializeDefaultRSState();
		}
	}

	IRenderMesh* getPrimitiveRenderMesh(PrimitiveRenderMeshType::Enum type);

	Renderable* createRenderable(IRenderMesh& mesh, RenderMaterial& material);
	void removeRenderable(Renderable* r);

	void drawUI();


	//////// public getters ////////

	float getScreenWidth() const
	{
		return m_screenWidth;
	}

	float getScreenHeight() const
	{
		return m_screenHeight;
	}

	void queueRenderBuffer(const PxRenderBuffer* buffer)
	{
		m_queuedRenderBuffers.push_back(buffer);
	}

	ResourceManager& getResourceManager() 
	{ 
		return m_resourceManager; 
	}

	uint32_t getVisibleOpaqueRenderablesCount()
	{
		return m_visibleOpaqueRenderablesCount;
	}

	uint32_t getVisibleTransparentRenderablesCount()
	{
		return m_visibleTransparentRenderablesCount;
	}

	CFirstPersonCamera& getCamera()
	{
		return m_camera;
	}


	//////// public 'internal' methods ////////

	// for internal usage (used by RenderShadows)
	void renderDepthOnly(DirectX::XMMATRIX* viewProjectionSubstitute);

  protected:

	//////// controller callbacks ////////

	virtual HRESULT DeviceCreated(ID3D11Device* pDevice);
	virtual void DeviceDestroyed();
	virtual LRESULT MsgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	virtual void Animate(double fElapsedTimeSeconds);
	virtual void onInitialize();
	virtual void onTerminate();
	virtual void BackBufferResized(ID3D11Device* pDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc);
	virtual void Render(ID3D11Device* /*device*/, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* pRTV,
	                    ID3D11DepthStencilView* pDSV);

  private:

	//////// internal methods ////////

	struct RenderDebugVertex
	{
		PxVec3 mPos;
		uint32_t mColor;
	};

	void render(const PxRenderBuffer* renderBuffer);
	void render(Renderable* renderable);
	void renderDebugPrimitive(const RenderDebugVertex *vertices, uint32_t verticesCount, D3D11_PRIMITIVE_TOPOLOGY topology);
	void initializeDefaultRSState();
	void setAllConstantBuffers(ID3D11DeviceContext* ctx);
	void toggleCameraSpeed(bool overspeed);


	//////// constant buffers ////////

	struct CBCamera
	{
		DirectX::XMMATRIX viewProjection;
		DirectX::XMMATRIX projectionInv;
		DirectX::XMFLOAT3 viewPos;
		float unusedPad;
	};
	struct CBWorld
	{
		DirectX::XMFLOAT3 ambientColor;
		float unusedPad1;
		DirectX::XMFLOAT3 pointLightPos;
		float unusedPad2;
		DirectX::XMFLOAT3 pointLightColor;
		float unusedPad3;
		DirectX::XMFLOAT3 dirLightDir;
		float specularPower;
		DirectX::XMFLOAT3 dirLightColor;
		float specularIntensity; // TODO: actually it's per object property
	};
	struct CBObject
	{
		DirectX::XMMATRIX world;
		DirectX::XMFLOAT4 color;
	};


	//////// internal data ////////

	// camera
	CFirstPersonCamera	               m_camera;
	float							   m_screenWidth;
	float							   m_screenHeight;

	// resources
	ResourceManager                    m_resourceManager;

	// additional render modules(libs)
	RendererShadow                     m_shadow;
	bool                               m_shadowEnabled;
	RendererHBAO                       m_HBAO;
	bool                               m_HBAOEnabled;

	// DX11 common
	ID3D11Device*                      m_device;
	ID3D11DeviceContext*               m_context;
	D3D11_VIEWPORT                     m_viewport;

	// DX11 states
	ID3D11RasterizerState*             m_RSState;
	ID3D11DepthStencilState*           m_opaqueRenderDSState;
	ID3D11DepthStencilState*           m_transparencyRenderDSState;

	// DX11 samplers
	ID3D11SamplerState*                m_pointSampler;
	ID3D11SamplerState*                m_linearSampler;

	// Depth Buffer
	ID3D11Texture2D*                   m_DSTexture;
	ID3D11DepthStencilView*	           m_DSView;
	ID3D11ShaderResourceView*          m_DSTextureSRV;

	// Constant Buffers
	ID3D11Buffer*                      m_cameraCB;
	ID3D11Buffer*                      m_worldCB;
	CBWorld                            m_worldCBData;
	ID3D11Buffer*                      m_objectCB;

	// toggles (options)
	bool                               m_wireframeMode;

	// renderables
	std::unordered_set<Renderable*>    m_renderables;

	// primitive meshes cache
	IRenderMesh*                       m_primitiveRenderMeshes[PrimitiveRenderMeshType::Count];

	// stats
	uint32_t						  m_visibleOpaqueRenderablesCount;
	uint32_t						  m_visibleTransparentRenderablesCount;

	// Debug Render
	RenderMaterial*                    m_debugPrimitiveRenderMaterial;
	RenderMaterial::InstancePtr        m_debugPrimitiveRenderMaterialInstance;
	ID3D11Buffer*                      m_debugPrimitiveVB;
	uint32_t                           m_debugPrimitiveVBVerticesCount;
	std::vector<const PxRenderBuffer*> m_queuedRenderBuffers;
};


#endif