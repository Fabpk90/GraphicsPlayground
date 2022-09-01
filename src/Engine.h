//
// Created by FanyMontpell on 03/09/2021.
//

#ifndef GRAPHICSPLAYGROUND_ENGINE_H
#define GRAPHICSPLAYGROUND_ENGINE_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EASTL/allocator.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector_set.h>
#include <EASTL/unordered_map.h>

#include <taskflow/taskflow.hpp>

#include "imgui.h"
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

struct Group;

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
    static RENDER_DEVICE_TYPE constexpr getRenderType() { return Diligent::RENDER_DEVICE_TYPE_D3D12;}
    void windowResize(int _width, int _height);
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

    void addDebugTexture(ITexture* _tex) { m_registeredTexturesForDebug.push_back(_tex);}
    void removeDebugTexture(ITexture* _tex){
        auto it = eastl::find(m_registeredTexturesForDebug.begin(), m_registeredTexturesForDebug.end(), _tex);

        if (it != m_registeredTexturesForDebug.end())
            m_registeredTexturesForDebug.erase(it);
    }

public:
    InputControllerWin32 m_inputController;
    ImGuiImplWin32* m_imguiRenderer;

private:
    int m_width, m_height;
    bool m_isMinimized = false;
    std::chrono::time_point<std::chrono::steady_clock> m_tickLastFrame;
    float m_deltaTime;


    IEngineFactory*               m_engineFactory  = nullptr;
    RefCntAutoPtr<IShaderSourceInputStreamFactory> m_ShaderSourceFactory;

    RefCntAutoPtr<IRenderDevice>  m_device;
    RefCntAutoPtr<IDeviceContext> m_immediateContext;
    RefCntAutoPtr<ISwapChain>     m_swapChain;

    //todo replace this by a hash table/map

    static constexpr const char* PSO_GBUFFER = "gbuffer";
    static constexpr const char* PSO_LIGHTING = "lighting";
    static constexpr const char* PSO_TRANSPARENCY = "transparency";
    static constexpr const char* PSO_TRANSPARENCY_COMPOSE = "transparency_compose";
    static constexpr const char* PSO_ZPREPASS = "zprepass";

    eastl::unordered_map<eastl::string, eastl::unique_ptr<PipelineState>> m_pipelines;

    static constexpr const char* TEX_ACCUM_COLOR = "accumColor";
    static constexpr const char* TEX_REVEAL = "revealTerm";

    eastl::unordered_map<eastl::string, eastl::unique_ptr<ITexture>> m_textures;

    RefCntAutoPtr<ITexture> m_accumColorTexture;
    RefCntAutoPtr<ITexture> m_revealTermTexture;

    RefCntAutoPtr<IBuffer> m_fullScreenTriangleBuffer;

    GBuffer* m_gbuffer = nullptr;

    RefCntAutoPtr<IBuffer> m_bufferMatrixMesh;

    eastl::vector<Mesh*> m_meshes; // Convenient vector to list all meshes
    eastl::vector_set<Mesh*> m_meshOpaque;
    eastl::vector_set<Mesh*> m_meshTransparent;

    FirstPersonCamera m_camera;

    RefCntAutoPtr<IBuffer> m_bufferLighting;

    float4 m_lightPos = float4(1);

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

    tf::Executor m_executor;
    tf::Taskflow m_taskflow;

    Mesh* m_clickedMesh = nullptr;

    //TODO: we should go to all resources counted system (measure perf maybe ?)
    eastl::vector<ITexture*> m_registeredTexturesForDebug;
    ImGuiTextFilter m_imguiFilter;

    void createDefaultTextures();
    void uiPass();

    void debugTextures();

    void createTransparencyPipeline();

    void showFrameTimeGraph();

    void renderTransparency();

    void createFullScreenResources();

    void showGizmos();

    void copyToSwapChain();

    void createZprepassPipeline();

    void Im3dNewFrame();

    void AddMesh(Mesh* _mesh);
    void SortMeshes();

    void renderGBuffer();

    void renderZPrepass();

    void createGBufferPipeline();
};


#endif //GRAPHICSPLAYGROUND_ENGINE_H
