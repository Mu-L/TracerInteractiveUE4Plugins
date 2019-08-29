#ifndef COMMON_BUFFERS_HLSL
#define COMMON_BUFFERS_HLSL

cbuffer Camera : register(b0)
{
	row_major matrix viewProjection;
	row_major matrix projectionInv;
	float3 viewPos;
};

cbuffer World : register(b1)
{
	float3 ambientColor;
	float3 pointLightPos;
	float3 pointLightColor;
	float3 dirLightDir;
	float specularPower;
	float3 dirLightColor;
	float specularIntensity;
};

cbuffer Object : register(b2)
{
	row_major matrix model;
	float4 defaultColor;
};

#endif