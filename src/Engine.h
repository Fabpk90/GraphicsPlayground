//
// Created by FanyMontpell on 03/09/2021.
//

#ifndef GRAPHICSPLAYGROUND_ENGINE_H
#define GRAPHICSPLAYGROUND_ENGINE_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EASTL/allocator.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>

#include "Mesh.h"
#include "FirstPersonCamera.hpp"
#include "Common/interface/BasicMath.hpp"
#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "DeviceContext.h"
#include "ImGuiImplWin32.hpp"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "GBuffer.hpp"
#include "RenderDocHook.hpp"
#include "Graphics/GraphicsTools/interface/ScopedQueryHelper.hpp"
#include "Graphics/GraphicsTools/interface/DurationQueryHelper.hpp"
#include "PipelineState.hpp"

using namespace Diligent;

struct Constants
{
    float4 m_params;
    float4 m_lightPos;
    float4 m_camPos;
};

class RayTracing;

class Engine {

public:
    Engine()
    {
        instance = this;
    };
    ~Engine();

    Engine(Engine&&) = delete;
    Engine(Engine&) = delete;

    static Engine* instance;

public:
    static RENDER_DEVICE_TYPE constexpr getRenderType() { return Diligent::RENDER_DEVICE_TYPE_VULKAN;}
    void windowResize(int _width, int _height)
    {
        //todo: this whole method is an ugly hax, change this
        m_isMinimized = true;
        if(_width == 0 || _height == 0) // minimizing the window
            return;
        m_width = _width; m_height = _height;
        if(m_immediateContext)
            m_immediateContext->SetViewports(1, nullptr, m_width, m_height);

        if(m_gbuffer)
        {
            m_gbuffer->Resize(float2(m_width, m_height));
        }

        m_camera.SetProjAttribs(0.01f, 1000, (float)m_width / (float) m_height, 45.0f, Diligent::SURFACE_TRANSFORM_OPTIMAL, false);

        //todo: this is ugly, fix that
        if(m_swapChain)
        {
            m_swapChain->Resize(m_width, m_height);
        }

        auto psoLighting = m_pipelines.find("lighting");
        if(psoLighting != m_pipelines.end())
        {
            auto& srb = psoLighting->second->getSRB();
            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_color")
                    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_normal")
                    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_depth")
                    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            //todo: change this when resizing (maybe a callback system for resize event ?)
            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_output")
                    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Output)->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }

        m_isMinimized = false;
    }
    bool initializeDiligentEngine(HWND hwnd);
    void createResources();

    void createLightingPipeline();
    void renderLighting();

    //TODO: make collecting stats a class
    void startCollectingStats();
    void endCollectingStats();
    void initStatsResources();
    void showStats();

    void render();
    void present();

    RefCntAutoPtr<IRenderDevice>& getDevice() {return m_device;}
    RefCntAutoPtr<IShaderSourceInputStreamFactory> getShaderStreamFactory() { return m_ShaderSourceFactory; }

public:
    InputControllerWin32 m_inputController;
    ImGuiImplWin32* m_imguiRenderer;

private:
    int m_width, m_height;
    bool m_isMinimized = false;

    IEngineFactory*               m_engineFactory  = nullptr;
    RefCntAutoPtr<IShaderSourceInputStreamFactory> m_ShaderSourceFactory;

    RefCntAutoPtr<IRenderDevice>  m_device;
    RefCntAutoPtr<IDeviceContext> m_immediateContext;
    RefCntAutoPtr<ISwapChain>     m_swapChain;

    static constexpr const char* PSO_GBUFFER = "gbuffer";
    static constexpr const char* PSO_LIGHTING = "lighting";
    static constexpr const char* PSO_TRANSPARENCY = "transparency";

    eastl::unordered_map<eastl::string, eastl::unique_ptr<PipelineState>> m_pipelines;

    GBuffer* m_gbuffer = nullptr;

    RefCntAutoPtr<IBuffer> m_bufferMatrixMesh;

    eastl::vector<Mesh*> m_meshes;
    FirstPersonCamera m_camera;

    RefCntAutoPtr<IBuffer> m_bufferLighting;

    float4 m_lightPos = float4(0);

    RenderDocHook* m_renderdoc = nullptr;

    eastl::unique_ptr<ScopedQueryHelper> m_pPipelineStatsQuery;
    eastl::unique_ptr<ScopedQueryHelper> m_pOcclusionQuery;
    eastl::unique_ptr<ScopedQueryHelper> m_pDurationQuery;
    eastl::unique_ptr<DurationQueryHelper> m_pDurationFromTimestamps;

    QueryDataPipelineStatistics m_PipelineStatsData;
    QueryDataOcclusion          m_OcclusionData;
    QueryDataDuration           m_DurationData;
    double                      m_DurationFromTimestamps = 0;

    RayTracing* m_raytracing;

    //TODO: we *could* hash the string to have a faster search, but since it's a few elements it shouldn't matter
    eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> m_defaultTextures;

    void createDefaultTextures();
    void uiPass();

    void createTransparencyPipeline();
};


#endif //GRAPHICSPLAYGROUND_ENGINE_H
