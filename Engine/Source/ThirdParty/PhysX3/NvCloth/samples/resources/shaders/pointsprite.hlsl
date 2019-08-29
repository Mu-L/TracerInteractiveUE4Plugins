#include "common_buffers.hlsl"
#include "lighting.hlsl"

SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);
Texture2D diffuseTexture : register(t0);
Texture2D<float> depthTexture   : register(t1);

static const float POINT_SIZE = 1.00f;
static const float FADE_DISTANCE = 1.0f;

struct VS_INPUT
{
	float3 position : POSITION0;
	float4 color : COLOR0;
	float2 scale : TANGENT;
};

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR0;
	float2 uv : TEXCOORD0;
	float2 screenPos : TEXCOORD1;
	float2 depth : TEXCOORD2;
	float2  pointSize  : PSIZE;
};

VS_OUTPUT VS(VS_INPUT iV)
{
	VS_OUTPUT oV;

	float4 worldSpacePos = mul(float4(iV.position, 1.0f), model);
	oV.position = mul(worldSpacePos, viewProjection);

	oV.color = iV.color;
	oV.uv = float2(0, 0);
	
	// uncomment to use scale
	//oV.pointSize = iV.scale * POINT_SIZE;
	oV.pointSize = float2(1, 1) * POINT_SIZE;

	return oV;
}

static const float4 SPRITE_VERTEX_POSITIONS[4] = 
{
    float4(  0.5, -0.5, 0, 0),
    float4(  0.5,  0.5, 0, 0),
    float4( -0.5, -0.5, 0, 0),
    float4( -0.5,  0.5, 0, 0),
};

static const float2 SPRITE_VERTEX_TEXCOORDS[4] = 
{ 
	float2(1, 0), 
	float2(1, 1),
	float2(0, 0),
	float2(0, 1),
};

[maxvertexcount(4)]
void GS( point VS_OUTPUT sprite[1], inout TriangleStream<VS_OUTPUT> triStream )
{
    VS_OUTPUT v;

	v.color = sprite[0].color;
	v.pointSize = sprite[0].pointSize;                      
    
	float4 aspectFactor = float4((projection[0][0] / projection[1][1]) * v.pointSize.x, 1.0 * v.pointSize.y, 0, 0);
	
	[unroll] for(int i = 0; i < 4; ++i)
	{
		v.position = sprite[0].position + SPRITE_VERTEX_POSITIONS[i] * aspectFactor;
		v.screenPos = v.position.xy / v.position.w;
		v.depth = v.position.zw;
		v.uv = SPRITE_VERTEX_TEXCOORDS[i];
		triStream.Append(v);
	}
	
    triStream.RestartStrip();	
}

float4 PS(VS_OUTPUT input) : SV_Target0
{
	// soft particles fade:
    float2 screenPos = 0.5*( (input.screenPos) + float2(1,1));
    screenPos.y = 1 - screenPos.y;
	
    float particleDepth = input.depth.x / input.depth.y;

	float depthSample = depthTexture.Sample(pointSampler, screenPos);
        
	float4 depthViewSample = mul(float4(input.screenPos, depthSample, 1), projectionInv );
	float4 depthViewParticle = mul(float4(input.screenPos, particleDepth, 1), projectionInv);

	float depthDiff = depthViewSample.z / depthViewSample.w - depthViewParticle.z / depthViewParticle.w;
	if( depthDiff < 0 )
		discard;
            
    float depthFade = saturate( depthDiff / FADE_DISTANCE );
	
	float4 textureColor = diffuseTexture.Sample(linearSampler, input.uv) * input.color;
	textureColor.a *= depthFade;
	return textureColor;
}