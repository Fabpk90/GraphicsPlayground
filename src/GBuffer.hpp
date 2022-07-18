//
// Created by fab on 10/05/2022.
//

#ifndef GRAPHICSPLAYGROUND_GBUFFER_HPP
#define GRAPHICSPLAYGROUND_GBUFFER_HPP

#include <EASTL/allocator.h>

#include <EASTL/array.h>
#include "Common/interface/BasicMath.hpp"
#include "Texture.h"

using namespace Diligent;

class GBuffer
{
public:
    enum class EGBufferType{
        Albedo = 0,
        Normal,
        Depth,
        Output,
        Max
    };

    GBuffer(float2 _size);
    ~GBuffer()
    {
	    for (const auto & texture : m_textures)
	    {
             texture.m_tex->Release();
	    }
    }

    static constexpr char* getName(EGBufferType _type)
    {
        switch (_type)
        {

            case EGBufferType::Albedo:
                return "GBuffer Albedo";
                break;
            case EGBufferType::Normal:
                return "GBuffer Normal";
                break;
            case EGBufferType::Depth:
                return "GBuffer Depth";
                break;
            case EGBufferType::Output:
                return "GBuffer Output";
            case EGBufferType::Max:
                return "";
                break;
        }

        return "";
    }

    static TEXTURE_FORMAT GetTextureFormat(EGBufferType _type)
    {
	    switch (_type) {
        case EGBufferType::Albedo:
            return TEX_FORMAT_RGBA8_UNORM_SRGB;
        case EGBufferType::Output:
            return  TEX_FORMAT_RGBA8_UNORM;
        case EGBufferType::Normal: return TEX_FORMAT_RGBA8_UNORM; //todo: compress octahedron
        case EGBufferType::Depth: return  TEX_FORMAT_D32_FLOAT;
	    default: return TEX_FORMAT_BGRA8_UNORM;
	    }
    }

    ITexture* GetTextureType(EGBufferType _type)
    {
        return m_textures[static_cast<uint>(_type)].m_tex;
    }

    void resize(float2 _size);

private:

    struct GTexture{
        ITexture* m_tex;
        EGBufferType m_type;
    };

    float2 m_size;

    eastl::array<GTexture, static_cast<size_t >(EGBufferType::Max)> m_textures;

    void createTextures();

    BIND_FLAGS getBindFlags(const EGBufferType type);
};


#endif //GRAPHICSPLAYGROUND_GBUFFER_HPP
