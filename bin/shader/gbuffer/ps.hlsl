Texture2D    g_TextureAlbedo;
SamplerState g_TextureAlbedo_sampler;

Texture2D    g_TextureNormal;
SamplerState g_TextureNormal_sampler;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV  : TEX_COORD;
};

struct PSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = g_TextureAlbedo.Sample(g_TextureAlbedo_sampler, PSIn.UV);
    PSOut.Normal = g_TextureNormal.Sample(g_TextureNormal_sampler, PSIn.UV);
}