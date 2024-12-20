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
#include "FirstPersonCamera.hpp"
#include "Common/interface/BasicMath.hpp"
#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "DeviceContext.h"
#include "ImGuiImplWin32.hpp"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "GBuffer.hpp"
#include "Mesh.h"
#include "RenderDocHook.hpp"
#include "Graphics/GraphicsTools/interface/ScopedQueryHelper.hpp"
#include "Graphics/GraphicsTools/interface/DurationQueryHelper.hpp"
#include "PipelineState.hpp"

using namespace Diligent;

struct Constants
{
    float4x4 m_cameraInvProj;
    float4x4 m_cameraInvView;
    float4 m_params;
    float4 m_lightPos;
    float4 m_camPos;
    float3 m_lightColor;
};

struct SPDConstants
{
    uint32_t    mips;
    uint32_t    numWorkGroups;
    int2        workGroupOffset;
    float2      invInputSize;
};

static eastl::vector<LayoutElement> layoutElementsPacked{
        {LayoutElement(0, 0, 2, Diligent::VT_UINT32, False),
         LayoutElement(1, 0, 2, Diligent::VT_UINT32, False),
         LayoutElement(2, 0, 1, Diligent::VT_UINT32, False),
         }
};

struct CSMProperties
{
    eastl::array<float4x4, FirstPersonCamera::getNbCascade()> VP;
    eastl::array<float, FirstPersonCamera::getNbCascade()> cascadeFarPlanes;
    eastl::array<uint3, FirstPersonCamera::getNbCascade()> padding;
};

struct spdConstants
{
    int mips;
    uint numWorkGroups;
    uint2 workGroupOffset;
};

class RayTracing;
class FrameGraph;

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

    void createDepthMinMaxPipeline();

    void createPrecomputeIrradiancePipeline();
    void renderPrecomputeIrradiance();

    void createResources();

    void createLightingPipeline();
    void renderLighting();

    //TODO: make collecting stats a class
    void startCollectingStats();
    void endCollectingStats();
    void initStatsResources();
    void showStats();

    void renderDepthMinMax();

    void render();
    void present();

    RefCntAutoPtr<IRenderDevice>& getDevice() {return m_device;}
    RefCntAutoPtr<IDeviceContext>& getContext() {return m_immediateContext;}
    RefCntAutoPtr<IShaderSourceInputStreamFactory> getShaderStreamFactory() { return m_ShaderSourceFactory; }

    void addDebugTexture(ITexture* _tex) { m_registeredTexturesForDebug.push_back(_tex);}
    void removeDebugTexture(ITexture* _tex){
        auto it = eastl::find(m_registeredTexturesForDebug.begin(), m_registeredTexturesForDebug.end(), _tex);

        if (it != m_registeredTexturesForDebug.end())
            m_registeredTexturesForDebug.erase(it);
    }

    bool areVerticesPacked() { return m_isVertexPacked;}

    GBuffer& getGBuffer() const { return *m_gbuffer;}
    FirstPersonCamera& getCamera() {return m_camera;}

    static constexpr uint HEAP_MAX_TEXTURES = 1024;
    static constexpr uint HEAP_MAX_BUFFERS = 1024;

    //todo fsantoro: make a debug class or something for this, it's clutter to the rendering
    uint32_t addImportProgress(const char* _name);
    void updateImportProgress(uint32_t _id, float _percentage);
    void removeImportProgress(uint32_t _id);

    typedef eastl::pair<eastl::string, float> ProgressPair;
    std::mutex m_mutexImportProgress;
    eastl::hash_map<uint32_t, ProgressPair> m_importProgressMap; // this is public for convenience, should be encapsulated when a class is designed for the progresion
    uint32_t m_idImporterProgress = 0;

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
    static constexpr const char* PSO_CSM = "csm";
    static constexpr const char* PSO_SKYDOME_CREATE = "skydome";
    static constexpr const char* PSO_CUBEMAP = "cubemap";
    static constexpr const char* PSO_DEPTH_MIN = "depth_min";
    static constexpr const char* PSO_DEPTH_MAX = "depth_max";
    static constexpr const char* PSO_PRECOMPUTE_IRRADIANCE = "precompute_irradiance";

    eastl::unordered_map<eastl::string, eastl::unique_ptr<PipelineState>> m_pipelines;

    static constexpr const char* TEX_ACCUM_COLOR = "accumColor";
    static constexpr const char* TEX_REVEAL = "revealTerm";

    eastl::unordered_map<eastl::string, eastl::unique_ptr<ITexture>> m_textures;

    RefCntAutoPtr<ITexture> m_accumColorTexture;
    RefCntAutoPtr<ITexture> m_revealTermTexture;

    RefCntAutoPtr<IBuffer> m_fullScreenTriangleBuffer;

    bool m_isVertexPacked = true;

    GBuffer* m_gbuffer = nullptr;

    RefCntAutoPtr<IBuffer> m_bufferMatrixMesh;

    std::mutex m_mutexAddMesh;
    eastl::vector<Mesh*> m_meshToAdd; // will be added in the correct vector at the start of the next frame
    eastl::vector< Mesh*> m_meshes; // Convenient vector to list all meshes
    eastl::vector_set<Mesh*> m_meshOpaque;
    eastl::vector_set<Mesh*> m_meshTransparent;

    eastl::vector<Mesh*> m_meshesSortedAndCulled;

    FirstPersonCamera m_camera;

    RefCntAutoPtr<IBuffer> m_bufferLighting;
    RefCntAutoPtr<IBuffer> m_bufferCSMProperties;

    eastl::vector<RefCntAutoPtr<ITexture>> m_heapTextures;
    eastl::vector<RefCntAutoPtr<IBuffer>> m_heapBuffers;

    eastl::vector<RefCntAutoPtr<ITexture>> m_cascadeTextures;

    float3 m_lightPos = float3(1, 1, 0);
    float3 m_lightColor = float3(0.5, 0.2, 0.5);

    RenderDocHook* m_renderdoc = nullptr;

    eastl::unique_ptr<ScopedQueryHelper> m_pPipelineStatsQuery;
    eastl::unique_ptr<ScopedQueryHelper> m_pOcclusionQuery;
    eastl::unique_ptr<ScopedQueryHelper> m_pDurationQuery;
    eastl::unique_ptr<DurationQueryHelper> m_pDurationFromTimestamps;

    class DebugShape* m_debugShape;

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

    FrameGraph* m_frameGraph;

    RefCntAutoPtr<IBuffer> m_bufferVerticesSkyDome;
    RefCntAutoPtr<IBuffer> m_bufferIndicesSkyDome;

    eastl::array<RefCntAutoPtr<ITexture>, 6> m_skyBoxCubeMap;
    eastl::array<float4x4, 6> m_skyBoxViewMatrices;

    RefCntAutoPtr<ITexture> m_skyBoxCubeTexture;
    eastl::array<RefCntAutoPtr<ITexture>, 6>  m_irradiancePrecomputed; // This will contain the cube map convolution

    RefCntAutoPtr<IBuffer> m_SPDConstantBuffer;
    RefCntAutoPtr<ITexture> m_SPDTextureOutputArray;
    RefCntAutoPtr<ITexture> m_SPDTexture6;
    RefCntAutoPtr<IBuffer> m_SPDGlobalAtomicBuffer;

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

    void renderGBuffer();

    void renderZPrepass();

    void createGBufferPipeline();

    void renderCSM();

    void createCSMPipeline();

    void showProgressIndicators();

    void frustrumCulling();

    void createSkydomeTexturePipeline();

    void renderCubeMapInTextures();

    void createCubeMapPipeline();
    void renderCubeMap();

    void SortMeshes();
};


#endif //GRAPHICSPLAYGROUND_ENGINE_H
