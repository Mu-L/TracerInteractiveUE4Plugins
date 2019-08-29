/*
* Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef PRIMITIVE_RENDER_MESH_H
#define PRIMITIVE_RENDER_MESH_H

#include "Utils.h"
#include <DirectXMath.h>

#include <vector>
#include "Renderable.h"
#include "CustomRenderMesh.h"


class PrimitiveRenderMesh : public CustomRenderMesh
{
protected:
	PrimitiveRenderMesh(const float v[], UINT numVertices);
};

class BoxRenderMesh : public PrimitiveRenderMesh
{
public:
	BoxRenderMesh();
};


class PlaneRenderMesh : public CustomRenderMesh
{
public:
	PlaneRenderMesh();
};


class SphereRenderMesh : public CustomRenderMesh
{
public:
	SphereRenderMesh();
	virtual ~SphereRenderMesh();
};


struct PrimitiveRenderMeshType
{
	enum Enum
	{
		Box,
		Plane,
		Sphere,
		Count
	};
};

#endif //PRIMITIVE_RENDER_MESH_H