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

//todo fsantoro pack RT
class GBuffer
{
public:
    enum class EGBufferType{
        Albedo = 0,
        Normal,
        Roughness,
        Depth,
        Output,
        Max
    };

    GBuffer(float2 _size);

    static constexpr const char* getName(EGBufferType _type)
    {
        switch (_type)
        {

            case EGBufferType::Albedo:
                return "GBuffer Albedo";
            case EGBufferType::Normal:
                return "GBuffer Normal";
            case EGBufferType::Depth:
                return "GBuffer Depth";
            case EGBufferType::Roughness:
                return "GBuffer Roughness";
            case EGBufferType::Output:
                return "GBuffer Output";
            case EGBufferType::Max:
                return "";
        }

        return "";
    }

    static TEXTURE_FORMAT getTextureFormat(EGBufferType _type)
    {
	    switch (_type) {
        case EGBufferType::Albedo:
            return TEX_FORMAT_RGBA8_UNORM_SRGB;
        case EGBufferType::Output:
            return TEX_FORMAT_RGBA8_UNORM_SRGB;
        case EGBufferType::Normal: return Diligent::TEX_FORMAT_RGBA8_SNORM; //todo: compress octahedron
        case EGBufferType::Roughness: return TEX_FORMAT_R8_UNORM; //todo: compress octahedron
        case EGBufferType::Depth: return  TEX_FORMAT_D32_FLOAT;
	    default: return TEX_FORMAT_BGRA8_UNORM;
	    }
    }

    [[nodiscard]] ITexture* getTextureOfType(EGBufferType _type) const
    {
        return m_textures[static_cast<uint>(_type)].m_tex;
    }

    void resize(float2 _size);

private:

    struct GTexture{
        RefCntAutoPtr<ITexture> m_tex;
        EGBufferType m_type;
    };

    float2 m_size;

    eastl::array<GTexture, static_cast<size_t >(EGBufferType::Max)> m_textures;

    void createTextures();

    static BIND_FLAGS getBindFlags(EGBufferType type);
};


#endif //GRAPHICSPLAYGROUND_GBUFFER_HPP
