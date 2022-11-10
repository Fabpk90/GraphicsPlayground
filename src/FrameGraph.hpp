//
// Created by fab on 04/09/2022.
//

#ifndef GRAPHICSPLAYGROUND_FRAMEGRAPH_HPP
#define GRAPHICSPLAYGROUND_FRAMEGRAPH_HPP


#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <vector>
#include <EASTL/hash_map.h>
#include "RenderPass.hpp"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Texture.h"

class RenderPassResources
{
public:
    struct Resource
    {
        eastl::string m_name;
    };

    void reset()
    {
        m_ids.clear();
        m_globalIDs = 0;
    }

private:
    uint32_t m_globalIDs;
    eastl::hash_map<uint32_t, Resource> m_ids;
};

class FrameGraphBuilder
{
public:
    enum class EReadFlag
    {
        READ,
        WRITE,
        CREATE
    };

    RenderPassResources createTexture(Diligent::TextureDesc& _desc)
    {
        return {};
    }
};

class FrameGraph
{
public:
    template<class T>
    [[maybe_unused]] RenderPassImpl<T>* addPass(eastl::string _name, eastl::function<void(FrameGraphBuilder&, T&)> _setupFunc
                                 , eastl::function<void(const T &, const RenderPassResources &)> _executeFunc)
    {
        auto* renderPass = new RenderPassImpl<T>(std::move(_name), T(), _setupFunc, _executeFunc, m_graphBuilder, m_resources);
        m_renderpasses.emplace_back(renderPass);

        return renderPass;
    };

    void startSetup();
    void startCompiling();

    ~FrameGraph()
    {
        for(auto* pass : m_renderpasses)
        {
            delete pass;
        }
    }
private:
    eastl::vector<RenderPass*> m_renderpasses;
    RenderPassResources m_resources;
    FrameGraphBuilder m_graphBuilder;
};






#endif //GRAPHICSPLAYGROUND_FRAMEGRAPH_HPP
