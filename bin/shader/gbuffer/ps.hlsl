#define USE_NORMAL_MAP
#define USE_ROUGHNESS_MAP

Texture2D<float4>    g_TextureAlbedo;
SamplerState g_TextureAlbedo_sampler;
#if defined(USE_NORMAL_MAP)
    Texture2D<float4>    g_TextureNormal;
#endif
#if defined(USE_ROUGHNESS_MAP)
    Texture2D<float>    g_TextureRoughness;
#endif

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

struct PSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float Roughness: SV_TARGET2;
};

[earlydepthstencil]
void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = pow(g_TextureAlbedo.Sample(g_TextureAlbedo_sampler, PSIn.UV), 2.2);
    float3 normal;
#if defined(USE_NORMAL_MAP)
    normal = g_TextureNormal.Sample(g_TextureAlbedo_sampler, PSIn.UV).xyz;
    #else
    // todo mul by model
        normal = float4(mul(g_model, PSIn.Normal), 0.0);
    #endif
    // get the tbn and transform the normal 
    normal = (normal * 2.0 - 1.0);
    normal = normalize(mul( normal, PSIn.TBN ));
   //normal = float3(0, 1, 0);

    PSOut.Normal = float4(normal, 0);

#if defined(USE_ROUGHNESS_MAP)
        PSOut.Roughness = g_TextureRoughness.Sample(g_TextureAlbedo_sampler, PSIn.UV);
    #else
        PSOut.Roughness = 0.25f;
    #endif
}