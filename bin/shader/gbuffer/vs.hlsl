#include "common/common.hlsl"

cbuffer Constants
{
    float4x4 g_WorldViewProj;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV  : TEX_COORD;
};

void main(in  VSInput VSIn,
          out PSInput PSIn)
{
    PSIn.Pos = mul( getPosition(VSIn), g_WorldViewProj);
    PSIn.UV  = getUV(VSIn);
    PSIn.Normal = getNormal(VSIn);
}