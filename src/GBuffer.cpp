//
// Created by fab on 10/05/2022.
//

#include "GBuffer.hpp"
#include "Engine.h"

GBuffer::GBuffer(float2 _size) : m_size(_size)
{
    createTextures();
}

void GBuffer::createTextures()
{
    int i = 0;
    for (auto& tex: m_textures)
    {
	    const auto type = static_cast<EGBufferType>(i++);
        const TEXTURE_FORMAT format = GetTextureFormat(type);

        BIND_FLAGS flags = getBindFlags(type);

        TextureDesc desc{};
        desc.Name = getName(type);
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.BindFlags = flags;
        desc.Format = format;
        desc.Width = m_size.x;
        desc.Height = m_size.y;

        ITexture* texture = nullptr;
        Engine::instance->getDevice()->CreateTexture(desc, nullptr, &texture);
        Engine::instance->addDebugTexture(texture);
        tex = { texture, type };
    }
}

void GBuffer::resize(float2 _size)
{
    m_size = _size;
    auto device = Engine::instance->getDevice();

    if(!device) return;

    device->IdleGPU();

    for (auto& tex : m_textures)
    {
        tex.m_tex->Release();
    }

    createTextures();
}

BIND_FLAGS GBuffer::getBindFlags(const GBuffer::EGBufferType type)
{
    switch (type)
    {

        case EGBufferType::Albedo:
        case EGBufferType::Normal:
            return BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        case EGBufferType::Depth:
            return BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;
            break;
        case EGBufferType::Output:
            return BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE; // added BIND_SHADER_RESOURCE for debug
            break;
        case EGBufferType::Max:
            break;
    }

    return BIND_NONE;
}
