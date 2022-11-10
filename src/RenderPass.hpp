//
// Created by fab on 04/09/2022.
//

#ifndef GRAPHICSPLAYGROUND_RENDERPASS_HPP
#define GRAPHICSPLAYGROUND_RENDERPASS_HPP

#include <EASTL/string.h>

#include <utility>

class FrameGraphBuilder;
class RenderPassResources;

class RenderPass
{
public:
    virtual ~RenderPass() = default;
    virtual void setup() = 0;
    virtual void execute() = 0;
};

template<typename T>
class RenderPassImpl : public RenderPass
{
    typedef eastl::function<void(FrameGraphBuilder&, T&)> setupFunc;
    typedef eastl::function<void(const T &, const RenderPassResources &)> executeFunc;
public:
    RenderPassImpl(eastl::string _name, T _data, setupFunc, executeFunc, FrameGraphBuilder& _graphBuilder, RenderPassResources& _passResources);

    void execute() override
    {
        m_execute(m_data, m_renderPassResources);
    }

    void setup() override
    {
        m_setup(m_frameBuilder, m_data);
    }

private:
    eastl::string m_name;
    T m_data;
    setupFunc m_setup;
    executeFunc m_execute;
    FrameGraphBuilder& m_frameBuilder;
    RenderPassResources& m_renderPassResources;
};

template<typename T>
RenderPassImpl<T>::RenderPassImpl(eastl::string _name, T _data, RenderPassImpl::setupFunc _setupFunc, RenderPassImpl::executeFunc _executeFunc
, FrameGraphBuilder& _graphBuilder, RenderPassResources& _passResources)
: m_name(std::move(_name)), m_data(_data), m_setup(_setupFunc), m_execute(_executeFunc), m_frameBuilder(_graphBuilder), m_renderPassResources(_passResources)
{}

#endif //GRAPHICSPLAYGROUND_RENDERPASS_HPP
