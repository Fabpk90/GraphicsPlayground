cbuffer Constants
{
    float4 m_params; //x width y height z near w far
    float4 m_lightpos;
    float4 m_camPos;
}

Texture2D<float4> g_color;
Texture2D<float4> g_normal;
Texture2D<float> g_depth;

RWTexture2D<float4> g_output;

[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float2 uv = id.xy;
    float3 N = g_normal[uv];
    float3 worldPos = abs(1 - g_depth[uv]) * m_params.w;
    float3 halfV = normalize(normalize((m_camPos - worldPos)) + N);

    float diffuse = max(0.005f, dot(N, m_lightpos));
    float4 specular = pow(dot(halfV, m_lightpos), 300) * float4(1, 1, 1, 0);

    g_output[uv] = g_color[uv] * (specular + diffuse);
}