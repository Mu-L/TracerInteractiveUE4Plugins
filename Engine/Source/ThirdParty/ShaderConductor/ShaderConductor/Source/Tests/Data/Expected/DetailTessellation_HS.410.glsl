#version 410
layout(vertices = 3) out;

struct VS_OUTPUT_HS_INPUT
{
    vec3 worldPos;
    vec3 normal;
    vec2 texCoord;
    vec3 lightTS;
};

layout(std140) uniform type_cbMain
{
    vec4 tessellationFactor;
} cbMain;

layout(location = 0) in vec3 in_var_WORLDPOS[];
layout(location = 1) in vec3 in_var_NORMAL[];
layout(location = 2) in vec2 in_var_TEXCOORD0[];
layout(location = 3) in vec3 in_var_LIGHTVECTORTS[];
layout(location = 3) out vec3 out_var_WORLDPOS[3];
layout(location = 1) out vec3 out_var_NORMAL[3];
layout(location = 2) out vec2 out_var_TEXCOORD0[3];
layout(location = 0) out vec3 out_var_LIGHTVECTORTS[3];

void main()
{
    VS_OUTPUT_HS_INPUT param_var_inputPatch[3] = VS_OUTPUT_HS_INPUT[](VS_OUTPUT_HS_INPUT(in_var_WORLDPOS[0], in_var_NORMAL[0], in_var_TEXCOORD0[0], in_var_LIGHTVECTORTS[0]), VS_OUTPUT_HS_INPUT(in_var_WORLDPOS[1], in_var_NORMAL[1], in_var_TEXCOORD0[1], in_var_LIGHTVECTORTS[1]), VS_OUTPUT_HS_INPUT(in_var_WORLDPOS[2], in_var_NORMAL[2], in_var_TEXCOORD0[2], in_var_LIGHTVECTORTS[2]));
    out_var_WORLDPOS[gl_InvocationID] = param_var_inputPatch[gl_InvocationID].worldPos;
    out_var_NORMAL[gl_InvocationID] = param_var_inputPatch[gl_InvocationID].normal;
    out_var_TEXCOORD0[gl_InvocationID] = param_var_inputPatch[gl_InvocationID].texCoord;
    out_var_LIGHTVECTORTS[gl_InvocationID] = param_var_inputPatch[gl_InvocationID].lightTS;
    barrier();
    if (gl_InvocationID == 0u)
    {
        gl_TessLevelOuter[0u] = cbMain.tessellationFactor.x;
        gl_TessLevelOuter[1u] = cbMain.tessellationFactor.y;
        gl_TessLevelOuter[2u] = cbMain.tessellationFactor.z;
        gl_TessLevelInner[0u] = cbMain.tessellationFactor.w;
    }
}

