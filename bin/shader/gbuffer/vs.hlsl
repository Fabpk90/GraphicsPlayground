#include "common/common.hlsl"

cbuffer Constants
{
    float4x4 g_WorldViewProj;
    float4x4 g_model;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV  : TEX_COORD;
    nointerpolation float3x3 TBN : TANGENT0;
};


void main(in  VSInput VSIn,
          out PSInput PSIn)
{
    PSIn.Pos = mul( getPosition(VSIn), g_WorldViewProj);
    PSIn.UV  = getUV(VSIn);
    PSIn.Normal = getNormal(VSIn);
    const float3 tangent = getTangent(VSIn);

    float3 T = normalize(float3(mul(g_model, float4(tangent, 0.0f)).xyz ));
    float3 N = normalize(float3(mul(g_model, float4(PSIn.Normal, 0.0f)).xyz ));
    // re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);

    float3 B = cross(T, N);

    PSIn.TBN = float3x3(T, B, N);
}