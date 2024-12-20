//
// Created by FanyMontpell on 03/09/2021.
//

#define PLATFORM_WIN32 1
#ifndef ENGINE_DLL
#    define ENGINE_DLL 1
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <Windows.h>
#include <iostream>


#include "Engine.h"
#include "EngineFactoryVk.h"
#include "EngineFactoryD3D12.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include <array>
#include <memory>
#include <EASTL/unique_ptr.h>
#include <stb_image.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "ImGuiImplWin32.hpp"
#include "Graphics/GraphicsTools/interface/DurationQueryHelper.hpp"
#include "RayTracing.hpp"
#include "TextureUtilities.h"
#include "im3d/im3d.h"
#include "im3d/im3d_math.h"
#include "assimp/DefaultLogger.hpp"
#include "FrameGraph.hpp"
#include "tracy/Tracy.hpp"
#include "GPUMarkerScoped.hpp"
#include "Mesh.h"
#include "debug/DebugShape.hpp"
#include "util/UIProgressingBars.hpp"
#include "FirstPersonCamera.hpp"
#include "../external/QuikMafs/Matrix4x4.hpp"

#define FFX_CPU
#include "FidelityFX/gpu/spd/ffx_spd_resources.h"
#include "FidelityFX/host/ffx_core.h"
#include "FidelityFX/gpu/spd/ffx_spd.h"

Engine *Engine::instance = nullptr;

bool Engine::initializeDiligentEngine(HWND hWnd)
{
    SwapChainDesc SCDesc;
    SCDesc.Usage = Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET;

    void *devicePointer = nullptr;

    switch (getRenderType())
    {
        case RENDER_DEVICE_TYPE_D3D12:
        {
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D12() function
            auto GetEngineFactoryD3D12 = LoadGraphicsEngineD3D12();
#    endif
            EngineD3D12CreateInfo EngineCI;

#if defined(_DEBUG)
            EngineCI.EnableValidation = TRUE;
            EngineCI.SetValidationLevel(Diligent::VALIDATION_LEVEL_1);
            EngineCI.D3D12ValidationFlags |= Diligent::D3D12_VALIDATION_FLAG_BREAK_ON_ERROR;
            EngineCI.Features =  DeviceFeatures{DEVICE_FEATURE_STATE_OPTIONAL};

#endif

            auto *pFactoryD3D12 = GetEngineFactoryD3D12();
            pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_device, &m_immediateContext);

            m_engineFactory = pFactoryD3D12;

            Win32NativeWindow Window{hWnd};
            pFactoryD3D12->CreateSwapChainD3D12(m_device, m_immediateContext, SCDesc, FullScreenModeDesc{}, Window,
                                                &m_swapChain);
        }
            break;

        case RENDER_DEVICE_TYPE_VULKAN:
        {
#    if EXPLICITLY_LOAD_ENGINE_VK_DLL
            // Load the dll and import GetEngineFactoryVk() function
            auto GetEngineFactoryVk = LoadGraphicsEngineVk();
#    endif
            EngineVkCreateInfo EngineCI;

            EngineCI.Features = DeviceFeatures{DEVICE_FEATURE_STATE_OPTIONAL};

            auto *pFactoryVk = GetEngineFactoryVk();
            pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_device, &m_immediateContext);

            m_engineFactory = pFactoryVk;

            if (!m_swapChain && hWnd != nullptr)
            {
                Win32NativeWindow Window{hWnd};
                pFactoryVk->CreateSwapChainVk(m_device, m_immediateContext, SCDesc, Window, &m_swapChain);

                //devicePointer = reinterpret_cast<IRenderDeviceD3D12*>(m_device.RawPtr())->GetD3D12Device();
            }
        }
            break;

        default:
            std::cerr << "Unknown/unsupported device type";
            return false;
            break;
    }

    ImGuiDiligentCreateInfo info;
    info.BackBufferFmt = m_swapChain->GetDesc().ColorBufferFormat;
    info.DepthBufferFmt = m_swapChain->GetDesc().DepthBufferFormat;
    info.pDevice = m_device;
    info.ColorConversion = IMGUI_COLOR_CONVERSION_MODE_AUTO;

    m_imguiRenderer = new ImGuiImplWin32(info, hWnd);
    auto &io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";

    //if(getRenderType() == Diligent::RENDER_DEVICE_TYPE_VULKAN)
    {
        //m_renderdoc = new RenderDocHook(devicePointer, hWnd);
    }

    auto proj = float4x4::Projection(m_camera.GetProjAttribs().FOV, m_camera.GetProjAttribs().AspectRatio, m_camera.GetProjAttribs().NearClipPlane, m_camera.GetProjAttribs().FarClipPlane, true);
    auto myProj = GraphicsPlayground::mat4::proj(m_camera.GetProjAttribs().FOV, m_camera.GetProjAttribs().AspectRatio, m_camera.GetProjAttribs().NearClipPlane, m_camera.GetProjAttribs().FarClipPlane);

    std::cout << proj[0][0] << " and " << myProj.vals.t[0][0] << std::endl;

    return true;
}


void Engine::createResources()
{
    initStatsResources();
    createDefaultTextures();
    createFullScreenResources();
    m_gbuffer = new GBuffer(float2(m_width, m_height));

    m_debugShape = new DebugShape();

    m_engineFactory->CreateDefaultShaderSourceStreamFactory("shader/", &m_ShaderSourceFactory);

    m_raytracing = new RayTracing(m_device, m_immediateContext);

    BufferDesc cbDesc;
    cbDesc.Usage = Diligent::USAGE_DYNAMIC;
    cbDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cbDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cbDesc.Size = sizeof(float4x4);
    cbDesc.Name = "UBO";
    m_device->CreateBuffer(cbDesc, nullptr, &m_bufferMatrixMesh);

    m_camera.SetPos(float3(0, 0, -20));

    createZprepassPipeline();
    createCSMPipeline();
    createGBufferPipeline();
    createLightingPipeline();
    createTransparencyPipeline();
    createSkydomeTexturePipeline();
    createCubeMapPipeline();
    createDepthMinMaxPipeline();
    createPrecomputeIrradiancePipeline();

    m_debugShape->createPipeline();

    Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
    Assimp::DefaultLogger::create();
    Assimp::DefaultLogger::get()->setLogSeverity(severity);

    for (int i = 0; i < 1; ++i)
    {
        m_executor.silent_async([&]()
                                {
                                    Mesh *m = new Mesh(m_device, "mesh/Bunny/stanford-bunny.obj");
                                    m->setTransparent(true);
                                    m->addTexture("redTransparent16x16.png", 0);
                                    AddMesh(m);
                                });
        m_executor.silent_async([&]()
                                {
                                    auto *m = new Mesh(m_device, "mesh/Bunny/stanford-bunny.obj");
                                    m->setTransparent(true);
                                    m->addTexture("blueTransparent16x16.png", 0);

                                    AddMesh(m);
                                });

        m_executor.silent_async([&]()
                                {
                                    auto *m = new Mesh(m_device, "mesh/Bunny/stanford-bunny.obj");
                                    m->setTransparent(true);
                                    m->addTexture("greenTransparent16x16.png", 0);

                                    AddMesh(m);
                                });

        m_executor.silent_async([&]()
                                {
                                    auto *m = new Mesh(m_device, "mesh/cerberus/Cerberus_LP.fbx", true, float3(0, 0, 0),
                                                       1.0f);

                                    m->addTexture("textures/Cerberus_N.tga", 0);
                                    m->setIsLoaded(true);

                                    AddMesh(m);
                                    m_clickedMesh = m;
                                });
    }

    m_executor.silent_async([&](){
         auto* m = new Mesh(m_device, "mesh/Sponza/Sponza.gltf");
         AddMesh(m);
         m_raytracing->addMeshToRayTrace(m);
       });

   m_executor.silent_async([&](){
        auto* m = new Mesh(m_device, "mesh/Floor/floor.obj");
        AddMesh(m);
    });

    struct Data
    {
        int m_test = 1;
        RenderPassResources m_tex;
    };

    m_frameGraph = new FrameGraph();

    m_frameGraph->addPass<Data>("Test Name", [&](FrameGraphBuilder &_frameBuilder, Data &_data)
    {
        TextureDesc desc;
        desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
        desc.Width = 1280;
        desc.Height = 720;
        desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;

        //TODO fsantoro add here if we read, write or write read the resource
        _data.m_tex = _frameBuilder.createTexture(desc);

    }, [=](const Data &_data, const RenderPassResources &_renderResources)
                                {

                                });

    m_frameGraph->startSetup();
    m_frameGraph->startCompiling();

    renderCubeMapInTextures();

}

void Engine::render()
{
    SortMeshes();

    const auto now = std::chrono::high_resolution_clock::now();
    m_deltaTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_tickLastFrame).count() * 0.01; //0.16 format
    m_tickLastFrame = now;

    if (m_isMinimized) return;

    if (m_inputController.IsKeyDown(Diligent::InputKeys::R))
    {
        for (auto &pso: m_pipelines)
        {
            pso.second->reload();
        }
    }

    {
        if (m_inputController.GetMouseState().ButtonFlags & Diligent::MouseState::BUTTON_FLAG_LEFT
            && !ImGui::GetIO().WantCaptureMouse)
        {
            ZoneScopedN("RayCast Selection");

            float3 rayNDC(2.0f * (m_inputController.GetMouseState().PosX / (float) m_width),
                                2.0f * (m_inputController.GetMouseState().PosY / (float) m_height), 1);

            rayNDC.x -= 1.0f;
            rayNDC.y -= 1.0f;

            float4 rayClip = float4(rayNDC.x, rayNDC.y, 1, 0); // ray pointing forward
            rayClip.y *= -1.0f;

            float4 rayEye = m_camera.GetProjMatrix().Inverse() * rayClip; // clip space
            rayEye /= rayEye.w;
            rayEye.z = 1.0f;
            rayEye.w = 0.0f; // it's a direction not a point

            float3 rayWorldDir = m_camera.GetViewMatrix().Inverse() * rayEye;

            float enterDist = 0;
            float exitDist = 0;

            const float4x4& mvp = m_camera.GetWorldMatrix();

            for (auto& mesh: m_meshes)
            {
                if (mesh && mesh->isClicked(mvp, m_camera.GetPos(), rayWorldDir, enterDist, exitDist))
                {
                    m_clickedMesh = mesh;
                    std::cout << m_clickedMesh->getName() << "found ! " << std::endl;
                    break;
                }
            }
        }
    }

    {
        ZoneScopedN("Sort&Cull");
        std::scoped_lock mut(m_mutexAddMesh);
        frustrumCulling();
    }


    ZoneScopedN("Render");

    m_camera.Update(m_inputController, m_deltaTime);

    startCollectingStats();

    {
        // todo fsantoro separate world part of this buffer to only update it (make sure to profile, maybe this is useless)
        // As this is a dynamic buffer, it cleared each frame so we need to map it here in case debushape uses it (since it is mapped only if a mesh is loaded)
        MapHelper<float4x4> mappedMem(m_immediateContext, m_bufferMatrixMesh, Diligent::MAP_WRITE,
                                      Diligent::MAP_FLAG_DISCARD);
        //*mappedMem = m_camera.GetProjMatrix() * m_camera.GetViewMatrix() * m_camera.GetWorldMatrix();
    }

    //z prepass
    renderZPrepass();
    //TODO depth min max
    //renderDepthMinMax();
    renderCSM();

    //gbuffer
    renderGBuffer();



    {
        ZoneScopedN("Raytracing");
        {
            ZoneScopedN("Raytracing - Update");
            m_raytracing->createBlasIfNeeded();
        }
        {
            ZoneScopedN("Raytracing - Draw");
            //todo fsantoro: ugly change that, cache the pointer
            auto size =float2(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output)->GetDesc().Height, m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output)->GetDesc().Width);
            m_raytracing->render(m_immediateContext, size.x, size.y);
        }
    }

    renderLighting();
    renderTransparency();
    renderCubeMap();
    renderPrecomputeIrradiance();

    for (const auto mesh: m_meshes)
    {
        if (mesh)
        {
            DebugShape::ShapeParams params;
            params.m_position = mesh->getTranslation();
            const auto aabb = mesh->getBoundingBox();
            params.m_size = aabb.Max * mesh->getScale();

            m_debugShape->addCubeAt(params);
        }
    }
    DebugShape::ShapeParams params;
    params.m_position = m_lightPos;
    params.m_size = float3(0.15f);

    m_debugShape->addCubeAt(params);

    m_debugShape->render(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output),
                         m_camera.GetViewMatrix() * m_camera.GetProjMatrix());
    copyToSwapChain();


    endCollectingStats();
    uiPass();

    FrameMark;
}

void Engine::uiPass()
{
    GPUScopedMarker("UI");
    ITextureView *view[] = {m_swapChain->GetCurrentBackBufferRTV()};

    m_immediateContext->SetRenderTargets(1, view,
                                         m_swapChain->GetDepthBufferDSV(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_imguiRenderer->NewFrame(m_width, m_height, SURFACE_TRANSFORM_IDENTITY);
    ImGui::Begin("Inspector");
    ImGui::Text("%f %f %f", m_camera.GetPos().x, m_camera.GetPos().y, m_camera.GetPos().z);
    ImGui::DragFloat3("Pos light: ", m_lightPos.Data(), 1.0f);
    ImGui::ColorEdit3("Light color: ", m_lightColor.Data());

    for (Mesh *m: m_meshes)
    {
        if (m && m->isLoaded())
        {
            m->drawInspector();
        }
    }
    ImGui::End();

    debugTextures();
    showStats();
    showFrameTimeGraph();
    showGizmos();
    showProgressIndicators();

    m_imguiRenderer->Render(m_immediateContext);
}

void Engine::present()
{
    if (!m_isMinimized)
        m_swapChain->Present();
}

void Engine::createLightingPipeline()
{
    //Lighting buffer
    BufferDesc cbDesc;
    cbDesc.Usage = USAGE_DYNAMIC;
    cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
    cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    cbDesc.ElementByteStride = sizeof(Constants);
    cbDesc.Size = sizeof(Constants);
    m_device->CreateBuffer(cbDesc, nullptr, &m_bufferLighting);

    BufferDesc csmBufferDesc;
    csmBufferDesc.Usage = USAGE_DYNAMIC;
    csmBufferDesc.BindFlags = BIND_UNIFORM_BUFFER;
    csmBufferDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    csmBufferDesc.ElementByteStride = sizeof(CSMProperties);
    csmBufferDesc.Size = sizeof(CSMProperties);
    m_device->CreateBuffer(csmBufferDesc, nullptr, &m_bufferCSMProperties);

    eastl::vector<PipelineState::VarStruct> staticVars = {
            {SHADER_TYPE_COMPUTE, "Constants",     m_bufferLighting},
            {SHADER_TYPE_COMPUTE, "CSMProperties", m_bufferCSMProperties}
    };
    eastl::vector<PipelineState::VarStruct> dynamicVars =
            {
                    {SHADER_TYPE_COMPUTE, "g_color", m_gbuffer->getTextureOfType(
                            GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
                    {SHADER_TYPE_COMPUTE, "g_normal", m_gbuffer->getTextureOfType(
                            GBuffer::EGBufferType::Normal)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
                    {SHADER_TYPE_COMPUTE, "g_roughness", m_gbuffer->getTextureOfType(
                            GBuffer::EGBufferType::Roughness)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
                    {SHADER_TYPE_COMPUTE, "g_depth", m_gbuffer->getTextureOfType(
                            GBuffer::EGBufferType::Depth)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
                    {SHADER_TYPE_COMPUTE, "g_output", m_gbuffer->getTextureOfType(
                            GBuffer::EGBufferType::Output)->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS)}
            };

    eastl::vector<eastl::pair<eastl::string, eastl::string >> shaderDefines;
    auto csmNb = eastl::string(std::to_string(FirstPersonCamera::getNbCascade()).c_str());

    shaderDefines.push_back(eastl::pair(eastl::string("CASCADE_NB"), csmNb));

    m_pipelines[PSO_LIGHTING] = eastl::make_unique<PipelineState>(m_device, "Compute Lighting PSO",
                                                                  PIPELINE_TYPE_COMPUTE, "lighting", shaderDefines,
                                                                  staticVars, dynamicVars);
}

void Engine::renderLighting()
{
    GPUScopedMarker("Lighting");
    ZoneScopedN("Render/Lighting - CPU");
    DispatchComputeAttribs dispatchComputeAttribs;
    dispatchComputeAttribs.ThreadGroupCountX = (m_width) / 8;
    dispatchComputeAttribs.ThreadGroupCountY = (m_height) / 8;

    auto &pipelineLighting = m_pipelines[PSO_LIGHTING];

    //todo make this possible while creating the pipeline
    IDeviceObject *views[] = {m_cascadeTextures[0]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE),
                              m_cascadeTextures[1]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE),
                              m_cascadeTextures[2]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)};

    if(auto var = pipelineLighting->getSRB().GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "g_csmSlices"))
    {
        var->SetArray(views, 0, 3);
    }


    m_immediateContext->SetPipelineState(pipelineLighting->getPipeline());
    m_immediateContext->CommitShaderResources(&pipelineLighting->getSRB(),
                                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    auto &camParams = m_camera.GetProjAttribs();

    {
        MapHelper<Constants> mapBuffer(m_immediateContext, m_bufferLighting, Diligent::MAP_WRITE,
                                       Diligent::MAP_FLAG_DISCARD);
        mapBuffer->m_cameraInvProj = (m_camera.GetProjMatrix()).Inverse().Transpose();
        const glm::vec3 pos(m_camera.GetPos().x, m_camera.GetPos().y, m_camera.GetPos().z);
        glm::vec3 center(m_camera.GetWorldAhead().x, m_camera.GetWorldAhead().y, m_camera.GetWorldAhead().z);
        center += pos;
        glm::mat4 view = glm::lookAt(pos, center, glm::vec3(0, 1, 0));
        view = glm::transpose(glm::inverse(view));
        float4x4 viewDilig;
        memcpy_s(&viewDilig, sizeof(viewDilig), &view, sizeof(view));
        mapBuffer->m_cameraInvView = viewDilig;
        mapBuffer->m_lightPos = m_lightPos;
        mapBuffer->m_lightColor = m_lightColor;
        mapBuffer->m_camPos = m_camera.GetPos();
        mapBuffer->m_params = float4(m_width, m_height, camParams.NearClipPlane, camParams.FarClipPlane);
    }

    {

        CSMProperties props;
        props.VP = m_camera.getSliceViewProjMatrix(normalize(m_lightPos));
        props.cascadeFarPlanes = Diligent::FirstPersonCamera::getCascadeFarPlane();

        MapHelper<CSMProperties> mapBuffer(m_immediateContext, m_bufferCSMProperties, Diligent::MAP_WRITE,
                                           Diligent::MAP_FLAG_DISCARD);
        *mapBuffer = props;
    }

    m_immediateContext->DispatchCompute(dispatchComputeAttribs);
}

void Engine::showStats()
{
    if (ImGui::Begin("Query data", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (m_pPipelineStatsQuery || m_pOcclusionQuery || m_pDurationQuery || m_pDurationFromTimestamps)
        {
            std::stringstream params_ss, values_ss;
            if (m_pPipelineStatsQuery)
            {
                params_ss << "Input vertices" << std::endl
                          << "Input primitives" << std::endl
                          << "VS Invocations" << std::endl
                          << "Clipping Invocations" << std::endl
                          << "Rasterized Primitives" << std::endl
                          << "PS Invocations" << std::endl;

                values_ss << m_PipelineStatsData.InputVertices << std::endl
                          << m_PipelineStatsData.InputPrimitives << std::endl
                          << m_PipelineStatsData.VSInvocations << std::endl
                          << m_PipelineStatsData.ClippingInvocations << std::endl
                          << m_PipelineStatsData.ClippingPrimitives << std::endl
                          << m_PipelineStatsData.PSInvocations << std::endl;
            }

            if (m_pOcclusionQuery)
            {
                params_ss << "Samples rendered" << std::endl;
                values_ss << m_OcclusionData.NumSamples << std::endl;
            }

            if (m_pDurationQuery)
            {
                if (m_DurationData.Frequency > 0)
                {
                    params_ss << "Duration (ms)" << std::endl;
                    values_ss << std::fixed << std::setprecision(2)
                              << static_cast<float>(m_DurationData.Duration) /
                                 static_cast<float>(m_DurationData.Frequency) * 1000.f << std::endl;
                }
                else
                {
                    params_ss << "Duration unavailable" << std::endl;
                }
            }

            if (m_pDurationFromTimestamps)
            {
                params_ss << "Duration from TS (ms)" << std::endl;
                values_ss << std::fixed << std::setprecision(2)
                          << (m_DurationFromTimestamps * 1000) << std::endl;
            }

            ImGui::TextDisabled("%s", params_ss.str().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", values_ss.str().c_str());
        }
        else
        {
            ImGui::TextDisabled("Queries are not supported by this device");
        }
    }
    ImGui::End();
}

void Engine::renderDepthMinMax()
{
    //copy depth into the imgdst
    //launch dispatch

    CopyTextureAttribs attribs;
    attribs.pSrcTexture = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth);
    attribs.pDstTexture = m_SPDTextureOutputArray;
    attribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_immediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_immediateContext->CopyTexture(attribs);

    const auto& pso = m_pipelines[PSO_DEPTH_MAX];
    m_immediateContext->SetPipelineState(pso->getPipeline());
    auto& srb = pso->getSRB();

    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "imgDst6")->Set(m_SPDTexture6->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));
    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "spdGlobalAtomic")->Set(m_SPDGlobalAtomicBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));

    m_immediateContext->CommitShaderResources(&srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    uint2 dispatchThreadGroupCountXY = uint2(0, 0);
    {
        uint2 workGroupOffset = uint2(0, 0);
        uint2 numWorkGroupsAndMips = uint2(0, 0);
        uint4 rectInfo = uint4(0, 0, m_width, m_height);

        ffxSpdSetup(dispatchThreadGroupCountXY.Data(), workGroupOffset.Data(), numWorkGroupsAndMips.Data(), rectInfo.Data());
        MapHelper<spdConstants> data = MapHelper<spdConstants>(m_immediateContext, m_SPDConstantBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
        data->workGroupOffset = workGroupOffset; // mips
        data->numWorkGroups = numWorkGroupsAndMips.x; // numworkgroups
        data->mips = numWorkGroupsAndMips.y; // workgroupoffset X
    }

    DispatchComputeAttribs dispatchAttrib;
    dispatchAttrib.ThreadGroupCountX = dispatchThreadGroupCountXY.x;
    dispatchAttrib.ThreadGroupCountY = dispatchThreadGroupCountXY.y;

    m_immediateContext->DispatchCompute(dispatchAttrib);

}

void Engine::initStatsResources()
{
    // Check query support
    const auto &Features = m_device->GetDeviceInfo().Features;
    if (Features.PipelineStatisticsQueries)
    {
        QueryDesc queryDesc;
        queryDesc.Name = "Pipeline statistics query";
        queryDesc.Type = QUERY_TYPE_PIPELINE_STATISTICS;
        m_pPipelineStatsQuery = eastl::make_unique<ScopedQueryHelper>(m_device, queryDesc, 2);
    }

    if (Features.OcclusionQueries)
    {
        QueryDesc queryDesc;
        queryDesc.Name = "Occlusion query";
        queryDesc.Type = QUERY_TYPE_OCCLUSION;
        m_pOcclusionQuery = eastl::make_unique<ScopedQueryHelper>(m_device, queryDesc, 2);
    }

    if (Features.DurationQueries)
    {
        QueryDesc queryDesc;
        queryDesc.Name = "Duration query";
        queryDesc.Type = QUERY_TYPE_DURATION;
        m_pDurationQuery = eastl::make_unique<ScopedQueryHelper>(m_device, queryDesc, 2);
    }

    if (Features.TimestampQueries)
    {
        m_pDurationFromTimestamps = eastl::make_unique<DurationQueryHelper>(m_device, 2);
    }
}

void Engine::startCollectingStats()
{
    if (m_pPipelineStatsQuery)
    {
        m_pPipelineStatsQuery->Begin(m_immediateContext);
    }
    if (m_pOcclusionQuery)
    {
        m_pOcclusionQuery->Begin(m_immediateContext);
    }
    if (m_pDurationQuery)
    {
        m_pDurationQuery->Begin(m_immediateContext);
    }

    if (m_pDurationFromTimestamps)
    {
        m_pDurationFromTimestamps->Begin(m_immediateContext);
    }
}

void Engine::endCollectingStats()
{
    if (m_pDurationQuery)
    {
        m_pDurationQuery->End(m_immediateContext, &m_DurationData, sizeof(m_DurationData));
    }

    if (m_pOcclusionQuery)
    {
        m_pOcclusionQuery->End(m_immediateContext, &m_OcclusionData, sizeof(m_OcclusionData));
    }

    if (m_pPipelineStatsQuery)
    {
        m_pPipelineStatsQuery->End(m_immediateContext, &m_PipelineStatsData, sizeof(m_PipelineStatsData));
    }

    if (m_pDurationFromTimestamps)
    {
        m_pDurationFromTimestamps->End(m_immediateContext, m_DurationFromTimestamps);
    }
}

Engine::~Engine()
{
    m_immediateContext->Flush();

    delete m_gbuffer;
    delete m_renderdoc;
    delete m_raytracing;
    delete m_imguiRenderer;
    delete m_frameGraph;
    delete m_debugShape;
}

void Engine::createDefaultTextures()
{
    std::cout << "Init default textures" << std::endl;

    TextureLoadInfo info;
    info.Name = "Default Albedo";
    info.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    info.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    info.IsSRGB = True;
    info.GenerateMips = False;

    RefCntAutoPtr<ITexture> texAlbedo;
    CreateTextureFromFile("textures/defaults/white16x16.png", info, m_device, &texAlbedo);
    m_defaultTextures["albedo"] = texAlbedo;

    info.Name = "Default Normal";
    info.IsSRGB = False;
    info.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;

    RefCntAutoPtr<ITexture> texNormal;
    CreateTextureFromFile("textures/defaults/normal16x16.png", info, m_device, &texNormal);
    m_defaultTextures["normal"] = texNormal;

    info.Name = "Default Red";

    RefCntAutoPtr<ITexture> texRedTransparent;
    CreateTextureFromFile("textures/defaults/redTransparent16x16.png", info, m_device, &texRedTransparent);
    m_defaultTextures["redTransparent"] = texRedTransparent;

    info.Name = "Default Roughness";

    RefCntAutoPtr<ITexture> texRoughness;
    CreateTextureFromFile("textures/defaults/roughness16x16.png", info, m_device, &texRoughness);
    m_defaultTextures["roughness"] = texRoughness;

    TextureDesc desc;
    desc.Format = Diligent::TEX_FORMAT_RGB32_FLOAT;
    desc.Name = "Default HDRI";
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;

    RefCntAutoPtr<ITexture> texHdri;
    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    float *data = stbi_loadf("textures/hdrTestTex.hdr", &width, &height, &nrComponents, 0);
    stbi_set_flip_vertically_on_load(false);
    if (data)
    {

        desc.Width = width;
        desc.Height = height;

        TextureSubResData subRes;
        subRes.pData = data;
        subRes.Stride = nrComponents * sizeof(float) * width;

        TextureData texData;
        texData.NumSubresources = 1;
        texData.pSubResources = &subRes;
        m_device->CreateTexture(desc, &texData, &texHdri);
        stbi_image_free(data);
    }

    m_registeredTexturesForDebug.push_back(texHdri);

    m_defaultTextures["hdr"] = texHdri;
}

void Engine::createTransparencyPipeline()
{
    //todo: make this half res
    TextureDesc texDesc;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
    texDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    //texDesc.ClearValue
    texDesc.Format = Diligent::TEX_FORMAT_R16_FLOAT;
    texDesc.Name = "Reveal Term";

    m_device->CreateTexture(texDesc, nullptr, &m_revealTermTexture);
    addDebugTexture(m_revealTermTexture.RawPtr());

    texDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    texDesc.Name = "Accum Color";
    m_device->CreateTexture(texDesc, nullptr, &m_accumColorTexture);
    addDebugTexture(m_accumColorTexture.RawPtr());

    {
        GraphicsPipelineDesc desc;
        desc.NumRenderTargets = 2;
        desc.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
        desc.RTVFormats[1] = Diligent::TEX_FORMAT_R16_FLOAT;
        desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
        desc.DepthStencilDesc.DepthEnable = True;
        desc.DepthStencilDesc.DepthWriteEnable = False;
        desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;

        desc.BlendDesc.IndependentBlendEnable = True; // THIS IS VERY IMPORTANT ! Without this only the first blend desc is used

        desc.BlendDesc.RenderTargets[0].BlendEnable = True;
        desc.BlendDesc.RenderTargets[0].BlendOp = Diligent::BLEND_OPERATION_ADD;

        desc.BlendDesc.RenderTargets[0].SrcBlend = Diligent::BLEND_FACTOR_ONE;
        desc.BlendDesc.RenderTargets[0].DestBlend = Diligent::BLEND_FACTOR_ONE;
        // desc.BlendDesc.RenderTargets[0].DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        // desc.BlendDesc.RenderTargets[0].SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;

        desc.BlendDesc.RenderTargets[1].BlendEnable = True;
        desc.BlendDesc.RenderTargets[1].BlendOp = Diligent::BLEND_OPERATION_ADD;

        desc.BlendDesc.RenderTargets[1].SrcBlend = Diligent::BLEND_FACTOR_ZERO;
        desc.BlendDesc.RenderTargets[1].DestBlend = Diligent::BLEND_FACTOR_INV_SRC_COLOR;
        //desc.BlendDesc.RenderTargets[1].DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC1_ALPHA;
        //desc.BlendDesc.RenderTargets[1].SrcBlendAlpha = Diligent::BLEND_FACTOR_ZERO;

        eastl::vector<LayoutElement> layoutElementsVertexPacked = layoutElementsPacked;

        eastl::vector<PipelineState::VarStruct> staticVars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

        eastl::vector<PipelineState::VarStruct> dynamicVars =
                {{SHADER_TYPE_PIXEL, "g_TextureAlbedo",
                  m_defaultTextures["redTransparent"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)}};

        m_pipelines[PSO_TRANSPARENCY] = eastl::make_unique<PipelineState>(m_device, "Transparency PSO",
                                                                          PIPELINE_TYPE_GRAPHICS, "transparency",
                                                                          eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                          staticVars,
                                                                          dynamicVars, desc,
                                                                          layoutElementsVertexPacked);
    }

    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = m_swapChain->GetDesc().ColorBufferFormat;
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    desc.DepthStencilDesc.DepthWriteEnable = False;
    desc.DepthStencilDesc.DepthEnable = False;

    desc.BlendDesc.RenderTargets[0].BlendEnable = True;
    desc.BlendDesc.RenderTargets[0].SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    desc.BlendDesc.RenderTargets[0].DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;

    eastl::vector<PipelineState::VarStruct> dynamicVars =
            {
                    {
                            SHADER_TYPE_PIXEL, "g_Accum",
                            m_accumColorTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)
                    },
                    {
                            SHADER_TYPE_PIXEL, "g_RevealTerm",
                            m_revealTermTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)
                    },
            };

    m_pipelines[PSO_TRANSPARENCY_COMPOSE] = eastl::make_unique<PipelineState>(m_device, "Transparency Compose PSO",
                                                                              PIPELINE_TYPE_GRAPHICS,
                                                                              "transparency_compose",
                                                                              eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                              eastl::vector<PipelineState::VarStruct>(),
                                                                              dynamicVars, desc);

}

void Engine::renderTransparency()
{
    GPUMarkerScoped marker("Transparency - OIT");
    {
        ITextureView *pRTV[] = {m_accumColorTexture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET),
                                m_revealTermTexture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        auto pDSV = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();

        m_immediateContext->SetRenderTargets(2, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL),
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const float4 clearValue[] = {{1, 0, 0, 0},
                                     {0, 0, 0, 0}};
        m_immediateContext->ClearRenderTarget(pRTV[1], clearValue[0].Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_immediateContext->ClearRenderTarget(pRTV[0], clearValue[1].Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->SetPipelineState(m_pipelines[PSO_TRANSPARENCY]->getPipeline());

        auto &psoTransparency = m_pipelines[PSO_TRANSPARENCY];

        GPUScopedMarker("Draw");

        ViewFrustum viewFrustum;
        ExtractViewFrustumPlanesFromMatrix(m_camera.GetViewMatrix() * m_camera.GetProjMatrix(), viewFrustum, false);

        std::scoped_lock mut(m_mutexAddMesh);
        for (auto& mesh: m_meshes)
        {
            if (mesh->isTransparent())
            {
                auto &groups = mesh->getGroups();
                const auto &model = mesh->getModel();
                {
                    // Map the buffer and write current world-view-projection matrix
                    MapHelper<float4x4> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE,
                                                    MAP_FLAG_DISCARD);
                    *CBConstants = (model * m_camera.GetViewMatrix() * m_camera.GetProjMatrix()).Transpose();
                }

                for (Mesh::Group &grp: groups)
                {
                    if (GetBoxVisibility(viewFrustum, grp.m_aabb.Transform(model)) == Diligent::BoxVisibility::Invisible)
                    {
                        continue;
                    }

                    if (grp.m_textures.empty())
                    {
                        psoTransparency->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                                Set(m_defaultTextures["redTransparent"]->GetDefaultView(
                                Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
                    }
                    else
                    {
                        psoTransparency->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                                Set(grp.m_textures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
                    }

                    Uint64 offset = 0;
                    IBuffer *pBuffs[] = {grp.m_meshVertexBuffer};
                    m_immediateContext->SetVertexBuffers(0, 1, pBuffs, &offset,
                                                         RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                         SET_VERTEX_BUFFERS_FLAG_RESET);
                    m_immediateContext->SetIndexBuffer(grp.m_meshIndexBuffer, 0,
                                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    m_immediateContext->CommitShaderResources(&psoTransparency->getSRB(),
                                                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                    DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
                    DrawAttrs.IndexType = VT_UINT16; // Index type
                    DrawAttrs.NumIndices = grp.m_indices.size();
                    // Verify the state of vertex and index buffers as well as consistence of
                    // render targets and correctness of draw command arguments
                    DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
                    m_immediateContext->DrawIndexed(DrawAttrs);
                }
            }
        }
    }

    //Compose phase
    {
        GPUScopedMarker("Compose");
        DrawAttribs attribs;
        attribs.FirstInstanceLocation = 0;
        attribs.NumInstances = 1;
        attribs.NumVertices = 3;
        attribs.StartVertexLocation = 0;
        attribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

        ITextureView *pRTV[] = {m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output)->GetDefaultView(
                Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        auto pDSV = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();

        m_immediateContext->SetRenderTargets(1, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL),
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->SetPipelineState(m_pipelines[PSO_TRANSPARENCY_COMPOSE]->getPipeline());
        m_immediateContext->CommitShaderResources(&m_pipelines[PSO_TRANSPARENCY_COMPOSE]->getSRB(),
                                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->Draw(attribs);
    }
}

void Engine::showFrameTimeGraph()
{
    /*const float width = ImGui::GetWindowWidth();
    const size_t frameCount = m_FrameTimeHistory.GetCount();
    if(width > 0.f && frameCount > 0)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 basePos = ImGui::GetCursorScreenPos();
        constexpr float minHeight = 2.f;
        constexpr float maxHeight = 64.f;
        float endX = width;
        constexpr float dtMin = 1.f / 120.f;
        constexpr float dtMax = 1.f / 15.f;
        const float dtMin_Log2 = log2(dtMin);
        const float dtMax_Log2 = log2(dtMax);
        drawList->AddRectFilled(basePos, ImVec2(basePos.x + width, basePos.y + maxHeight), 0xFF404040);
        for(size_t frameIndex = 0; frameIndex < frameCount && endX > 0.f; ++frameIndex)
        {
            const FrameTimeHistory::Entry dt = m_FrameTimeHistory.Get(frameIndex);
            const float frameWidth = dt.m_DT / dtMin;
            const float frameHeightFactor = (dt.m_DT_Log2 - dtMin_Log2) / (dtMax_Log2 - dtMin_Log2);
            const float frameHeightFactor_Nrm = std::min(std::max(0.f, frameHeightFactor), 1.f);
            const float frameHeight = lerp(minHeight, maxHeight, frameHeightFactor_Nrm);
            const float begX = endX - frameWidth;
            const uint32_t color = glm::packUnorm4x8(DeltaTimeToColor(dt.m_DT));
            drawList->AddRectFilled(
                    ImVec2(basePos.x + std::max(0.f, floor(begX)), basePos.y + maxHeight - frameHeight),
                    ImVec2(basePos.x + ceil(endX), basePos.y + maxHeight),
                    color);
            endX = begX;
        }
        ImGui::Dummy(ImVec2(width, maxHeight));
    }*/
}

void Engine::createFullScreenResources()
{
    //creates a buffer of 3 vertices to do a full screen pass

    //the positions are 0 cause we will generate the real positions in the vertex shader
    const float3 positions[] = {float3(0), float3(0), float3(0)};
    BufferData data;
    data.pData = positions;
    data.DataSize = sizeof(positions);

    BufferDesc desc;
    desc.Usage = Diligent::USAGE_IMMUTABLE;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    desc.Size = sizeof(positions);
    desc.ElementByteStride = sizeof(float3);
    desc.Name = "Full Screen Triangle Buffer";

    m_device->CreateBuffer(desc, &data, &m_fullScreenTriangleBuffer);
}

void Engine::showGizmos()
{
    if (!m_clickedMesh) return;

    Im3dNewFrame();

    Im3d::Vec3 translation = Im3d::Vec3(m_clickedMesh->getTranslation().x, m_clickedMesh->getTranslation().y,
                                        m_clickedMesh->getTranslation().z);
    Im3d::Mat3 rotation(1.0f);
    Im3d::Vec3 scale(m_clickedMesh->getScale());

    Im3d::PushMatrix(Im3d::Mat4(translation, rotation, scale));

    if (Im3d::GizmoTranslation("UnifiedGizmo", translation))
    {
        // transform was modified, do something with the matrix
    }

    Im3d::PopMatrix();

    Im3d::EndFrame();

    for(int i = 0; i < Im3d::GetDrawListCount(); ++i)
    {
        const Im3d::DrawList& drawList = Im3d::GetDrawLists()[i];

        // todo fsantoro: add pso for each type (line etc) shaders, encapsulate all this in a class ! import shaders from github
    }
}

void Engine::debugTextures()
{
    ImGui::Begin("Texture Debugger", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    m_imguiFilter.Draw();

    const auto texSize = ImVec2(128, 128);
    if (m_imguiFilter.IsActive())
    {
        for (auto *tex: m_registeredTexturesForDebug)
        {
            const auto &desc = tex->GetDesc();
            const eastl::string texName = desc.Name;

            if (m_imguiFilter.PassFilter(texName.data(), texName.data() + texName.size()))
            {
                ImGui::Text("%s", texName.c_str());
                ImGui::Image(tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), texSize);
            }

        }
    }
    else
    {
        for (auto *tex: m_registeredTexturesForDebug)
        {
            const auto &desc = tex->GetDesc();
            ImGui::Text("%s", desc.Name);
            ImGui::Image(tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), texSize);
        }
    }


    ImGui::End();

}

void Engine::windowResize(int _width, int _height)
{
    {
        //todo: this whole method is an ugly hax, change this
        m_isMinimized = true;
        if (_width == 0 || _height == 0) // minimizing the window
            return;
        m_width = _width;
        m_height = _height;
        if (m_immediateContext)
            m_immediateContext->SetViewports(1, nullptr, m_width, m_height);

        //todo: this is ugly, fix that
        if (m_swapChain)
        {
            m_swapChain->Resize(m_width, m_height);
        }

        if (m_gbuffer)
        {
            m_gbuffer->resize(float2(m_width, m_height));
        }

        m_camera.SetProjAttribs(0.01f, 1000, (float) m_width / (float) m_height, 45.0f,
                                Diligent::SURFACE_TRANSFORM_OPTIMAL, false);

        auto psoLighting = m_pipelines.find("lighting");
        if (psoLighting != m_pipelines.end())
        {
            auto &srb = psoLighting->second->getSRB();
            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_color")
                    ->Set(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Albedo)->GetDefaultView(
                            Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_normal")
                    ->Set(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Normal)->GetDefaultView(
                            Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_depth")
                    ->Set(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth)->GetDefaultView(
                            Diligent::TEXTURE_VIEW_SHADER_RESOURCE), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

            //todo: change this when resizing (maybe a callback system for resize event ?)
            srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_output")
                    ->Set(m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output)->GetDefaultView(
                            Diligent::TEXTURE_VIEW_UNORDERED_ACCESS), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }

        if (m_accumColorTexture)
        {
            removeDebugTexture(m_accumColorTexture.RawPtr());
            removeDebugTexture(m_revealTermTexture.RawPtr());

            //todo: make this half res
            TextureDesc texDesc;
            texDesc.Width = m_width;
            texDesc.Height = m_height;
            texDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
            texDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            //texDesc.ClearValue
            texDesc.Format = Diligent::TEX_FORMAT_R16_FLOAT;
            texDesc.Name = "Reveal Term";

            RefCntAutoPtr<ITexture> revealTermTexture;

            m_device->CreateTexture(texDesc, nullptr, &revealTermTexture);
            m_revealTermTexture.swap(revealTermTexture);
            addDebugTexture(m_revealTermTexture.RawPtr());

            texDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            texDesc.Name = "Accum Color";

            RefCntAutoPtr<ITexture> accumColorTexture;
            m_device->CreateTexture(texDesc, nullptr, &accumColorTexture);

            m_accumColorTexture.swap(accumColorTexture);
            addDebugTexture(m_accumColorTexture.RawPtr());
        }
        m_isMinimized = false;
    }
}

void Engine::copyToSwapChain()
{

    m_immediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

    CopyTextureAttribs attribs;
    attribs.pDstTexture = m_swapChain->GetCurrentBackBufferRTV()->GetTexture();
    attribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.pSrcTexture = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Output);
    attribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_immediateContext->CopyTexture(attribs);
}

void Engine::createZprepassPipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 0;
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.DepthStencilDesc.DepthEnable = True;
    desc.DepthStencilDesc.DepthWriteEnable = True;
    desc.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;

    //m_registeredTexturesForDebug.emplace_back(m_swapChain->GetDepthBufferDSV()->GetTexture());
    eastl::vector<LayoutElement> layoutElements;
    if (m_isVertexPacked)
    {
        layoutElements = layoutElementsPacked;
    }
    else
    {
        layoutElements = {
                LayoutElement(0, 0, 3, VT_FLOAT32, False),
                LayoutElement(1, 0, 3, VT_FLOAT32, True),
                LayoutElement(2, 0, 2, VT_FLOAT32, False)
        };
    }

    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    m_pipelines[PSO_ZPREPASS] = eastl::make_unique<PipelineState>(m_device, "Z Prepass", PIPELINE_TYPE_GRAPHICS,
                                                                  "zprepass",
                                                                  eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                  vars,
                                                                  eastl::vector<PipelineState::VarStruct>(), desc,
                                                                  layoutElements);

}

void Engine::Im3dNewFrame()
{
    Im3d::AppData &ad = Im3d::GetAppData();

    ad.m_deltaTime = m_deltaTime;
    ad.m_viewportSize = Im3d::Vec2(m_width, m_height);
    ad.m_viewOrigin = Im3d::Vec3(m_camera.GetPos().x, m_camera.GetPos().y,
                                 m_camera.GetPos().z); // for VR use the head position
    ad.m_viewDirection = Im3d::Vec3(m_camera.GetWorldAhead().x, m_camera.GetWorldAhead().y, m_camera.GetWorldAhead().z);
    ad.m_worldUp = Im3d::Vec3(0.0f, 1.0f, 0.0f); // used internally for generating orthonormal bases
    //ad.m_projOrtho     = false;

    // m_projScaleY controls how gizmos are scaled in world space to maintain a constant screen height
    ad.m_projScaleY = tanf(m_camera.GetProjAttribs().FOV * 0.5f) * 2.0f // or vertical fov for a perspective projection
            ;

    // World space cursor ray from mouse position; for VR this might be the position/orientation of the HMD or a tracked controller.
    Im3d::Vec2 cursorPos = Im3d::Vec2(m_inputController.GetMouseState().PosX, m_inputController.GetMouseState().PosY);
    cursorPos = (cursorPos / ad.m_viewportSize) * 2.0f - Im3d::Vec2(1.0f);
    cursorPos.y = -cursorPos.y; // window origin is top-left, ndc is bottom-left
    Im3d::Vec3 rayOrigin, rayDirection;
    rayOrigin = ad.m_viewOrigin;
    rayDirection.x = cursorPos.x / m_camera.GetProjMatrix().m00;
    rayDirection.y = cursorPos.y / m_camera.GetProjMatrix().m11;
    rayDirection.z = 1.0f;

    Im3d::Mat4 camMatIm;
    memcpy_s(camMatIm, sizeof(float) * 16, m_camera.GetWorldMatrix().Data(), sizeof(float) * 16);
    rayDirection = camMatIm * Im3d::Vec4(Normalize(rayDirection), 0.0f);

    ad.m_cursorRayOrigin = rayOrigin;
    ad.m_cursorRayDirection = rayDirection;

    // Set cull frustum planes. This is only required if IM3D_CULL_GIZMOS or IM3D_CULL_PRIMTIIVES is enable via
    // im3d_config.h, or if any of the IsVisible() functions are called.
    const auto mat = m_camera.GetViewMatrix() * m_camera.GetProjMatrix();
    Im3d::Mat4 matIm;
    memcpy_s(matIm, sizeof(float) * 16, mat.Data(), sizeof(float) * 16);
    ad.setCullFrustum(matIm, true);

    // Fill the key state array; using GetAsyncKeyState here but this could equally well be done via the window proc.
    // All key states have an equivalent (and more descriptive) 'Action_' enum.
    ad.m_keyDown[Im3d::Mouse_Left/*Im3d::Action_Select*/] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // The following key states control which gizmo to use for the generic Gizmo() function. Here using the left ctrl
    // key as an additional predicate.
    bool ctrlDown = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    ad.m_keyDown[Im3d::Key_L/*Action_GizmoLocal*/] = ctrlDown && (GetAsyncKeyState(0x4c) & 0x8000) != 0;
    ad.m_keyDown[Im3d::Key_T/*Action_GizmoTranslation*/] = ctrlDown && (GetAsyncKeyState(0x54) & 0x8000) != 0;
    ad.m_keyDown[Im3d::Key_R/*Action_GizmoRotation*/] = ctrlDown && (GetAsyncKeyState(0x52) & 0x8000) != 0;
    ad.m_keyDown[Im3d::Key_S/*Action_GizmoScale*/] = ctrlDown && (GetAsyncKeyState(0x53) & 0x8000) != 0;

    // Enable gizmo snapping by setting the translation/rotation/scale increments to be > 0
    ad.m_snapTranslation = ctrlDown ? 0.1f : 0.0f;
    ad.m_snapRotation = ctrlDown ? Im3d::Radians(30.0f) : 0.0f;
    ad.m_snapScale = ctrlDown ? 0.5f : 0.0f;

    Im3d::NewFrame();
}

void Engine::AddMesh(Mesh *_mesh)
{
    {
        std::scoped_lock mut(m_mutexAddMesh);

        std::cout << "Adding mesh" << _mesh->getName() << std::endl;

        m_meshToAdd.emplace_back(_mesh);
    }
}

void Engine::createGBufferPipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 3;
    desc.RTVFormats[0] = m_swapChain->GetDesc().ColorBufferFormat;
    desc.RTVFormats[1] = GBuffer::getTextureFormat(GBuffer::EGBufferType::Normal);
    desc.RTVFormats[2] = GBuffer::getTextureFormat(GBuffer::EGBufferType::Roughness);
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
    desc.DepthStencilDesc.DepthEnable = True;
    desc.DepthStencilDesc.DepthWriteEnable = False;
    desc.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_EQUAL;

    eastl::vector<LayoutElement> layoutElements;
    if (m_isVertexPacked)
    {
        layoutElements = layoutElementsPacked;
    }
    else
    {
        layoutElements = {
                LayoutElement(0, 0, 3, VT_FLOAT32, False),
                LayoutElement(1, 0, 3, VT_FLOAT32, True),
                LayoutElement(2, 0, 2, VT_FLOAT32, False)
        };
    }

    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    m_pipelines[PSO_GBUFFER] = eastl::make_unique<PipelineState>(m_device, "Simple Mesh PSO", PIPELINE_TYPE_GRAPHICS,
                                                                 "gbuffer",
                                                                 eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                 vars, eastl::vector<PipelineState::VarStruct>(), desc,
                                                                 layoutElements);
}

void Engine::renderGBuffer()
{
    GPUScopedMarker("GBuffer");
    const auto &psoGBuffer = m_pipelines[PSO_GBUFFER];
    m_immediateContext->SetPipelineState(psoGBuffer->getPipeline());

    ITextureView *pRTV[] = {m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Albedo)->GetDefaultView(
            Diligent::TEXTURE_VIEW_RENDER_TARGET),
                            m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Normal)->GetDefaultView(
                                    Diligent::TEXTURE_VIEW_RENDER_TARGET),
                            m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Roughness)->GetDefaultView(
                                    Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    auto pDSV = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();
    m_immediateContext->SetRenderTargets(3, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL),
                                         RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float ClearColor[] = {0.0f, 181.f / 255.f, 221.f / 255.f, 1.0f};
    const float ClearColorNormal[] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Let the engine perform required state transitions
    m_immediateContext->ClearRenderTarget(pRTV[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearRenderTarget(pRTV[1], ClearColorNormal, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearRenderTarget(pRTV[2], ClearColorNormal, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    std::scoped_lock mut(m_mutexAddMesh);
    for (Mesh *m: m_meshOpaque)
    {
        if(m->isLoaded())
        {
            auto &groups = m->getGroups();
            const auto &model = m->getModel();

            {
                struct ConstantsGBuffer {
                    float4x4 g_WorldViewProj;
                    float4x4 g_model;
                };

                // Map the buffer and write current world-view-projection matrix
                MapHelper<ConstantsGBuffer> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
                CBConstants->g_WorldViewProj = (model * m_camera.GetViewMatrix() * m_camera.GetProjMatrix()).Transpose();
                CBConstants->g_model = (model).Transpose();
            }

            for (Mesh::Group &grp: groups)
            {
                if (grp.m_textures.empty())
                {
                    psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                            Set(m_defaultTextures["albedo"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));

                    if (auto *pVar = psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal"))
                    {
                        pVar->Set(m_defaultTextures["normal"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
                    }

                    if (auto *pVar = psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness"))
                    {
                        pVar->Set(m_defaultTextures["roughness"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
                    }
                }
                else
                {
                    psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                            Set(grp.m_textures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

                    if (grp.m_textures.size() > 1)
                    {
                        if(auto * normal = psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal"))
                        {
                            normal->Set(grp.m_textures[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
                        }
                        if(auto * roughness = psoGBuffer->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness"))
                        {
                            roughness->Set(m_defaultTextures["roughness"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
                            //roughness->Set(grp.m_textures[2]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
                        }
                    }
                }

                Uint64 offset = 0;
                IBuffer *pBuffs[] = {grp.m_meshVertexBuffer};
                m_immediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                     SET_VERTEX_BUFFERS_FLAG_RESET);
                m_immediateContext->SetIndexBuffer(grp.m_meshIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                m_immediateContext->CommitShaderResources(&psoGBuffer->getSRB(),
                                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
                DrawAttrs.IndexType = VT_UINT16; // Index type
                DrawAttrs.NumIndices = grp.m_indices.size();
                // Verify the state of vertex and index buffers as well as consistence of
                // render targets and correctness of draw command arguments
                DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
                m_immediateContext->DrawIndexed(DrawAttrs);
            }
        }
    }
}

void Engine::renderZPrepass()
{
    GPUScopedMarker("Z prepass");
    const auto &psoZPrepass = m_pipelines[PSO_ZPREPASS];
    m_immediateContext->SetPipelineState(psoZPrepass->getPipeline());

    auto pDSV = m_gbuffer->getTextureOfType(GBuffer::EGBufferType::Depth);// m_swapChain-
    m_immediateContext->SetRenderTargets(0, nullptr, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL),
                                         RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearDepthStencil(pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL),
                                          Diligent::CLEAR_DEPTH_FLAG, 1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    ViewFrustum viewFrustum;
    ExtractViewFrustumPlanesFromMatrix(m_camera.GetViewMatrix() * m_camera.GetProjMatrix(),
                                       viewFrustum, false);

    std::scoped_lock mut(m_mutexAddMesh);
    for (Mesh *m: m_meshOpaque)
    {
        auto &groups = m->getGroups();
        const auto &model = m->getModel();

        {
            // Map the buffer and write current world-view-projection matrix
            MapHelper<float4x4> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
            *CBConstants = (model * m_camera.GetViewMatrix() * m_camera.GetProjMatrix()).Transpose();
        }

        for (Mesh::Group &grp: groups)
        {
            if (GetBoxVisibility(viewFrustum, grp.m_aabb.Transform(model)) == Diligent::BoxVisibility::Invisible)
            {
                continue;
            }

            Uint64 offset = 0;
            IBuffer *pBuffs[] = {grp.m_meshVertexBuffer};
            m_immediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                 SET_VERTEX_BUFFERS_FLAG_RESET);
            m_immediateContext->SetIndexBuffer(grp.m_meshIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_immediateContext->CommitShaderResources(&psoZPrepass->getSRB(),
                                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
            DrawAttrs.IndexType = VT_UINT16; // Index type
            DrawAttrs.NumIndices = grp.m_indices.size();
            // Verify the state of vertex and index buffers as well as consistence of
            // render targets and correctness of draw command arguments
            //DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
            m_immediateContext->DrawIndexed(DrawAttrs);
        }
    }
}

void Engine::renderCSM()
{
    const auto &psoCsm = m_pipelines[PSO_CSM];
    m_immediateContext->SetPipelineState(psoCsm->getPipeline());

    //These are not transposed for GPU consumption
    eastl::array<float4x4, 3> slicesVP = m_camera.getSliceViewProjMatrix(normalize(m_lightPos));

    GPUScopedMarker("CSM");

    ViewFrustum viewFrustum;
    ExtractViewFrustumPlanesFromMatrix(m_camera.GetViewMatrix() * m_camera.GetProjMatrix(),
                                       viewFrustum, false);

    for (int i = 0; i < Diligent::FirstPersonCamera::getNbCascade(); ++i)
    {
        eastl::string cascadeName = eastl::string("Cascade ");
        cascadeName.append(std::to_string(i).c_str());
        GPUScopedMarker(cascadeName);

        auto *cascadeView = m_cascadeTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        m_immediateContext->SetRenderTargets(0, nullptr, cascadeView, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_immediateContext->ClearDepthStencil(cascadeView, Diligent::CLEAR_DEPTH_FLAG, 1, 0,
                                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        std::scoped_lock mut(m_mutexAddMesh);

        m_immediateContext->CommitShaderResources(&psoCsm->getSRB(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        for (Mesh *m: m_meshOpaque)
        {
            auto &groups = m->getGroups();
            const auto &model = m->getModel();

            {
                // Map the buffer and write current world-view-projection matrix
                MapHelper<float4x4> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
                *CBConstants = (model * slicesVP[i]).Transpose();
            }

            for (Mesh::Group &grp: groups)
            {
                /*if (GetBoxVisibility(viewFrustum, grp.m_aabb.Transform(model)) == Diligent::BoxVisibility::Invisible)
                {
                   continue;
                }*/
                Uint64 offset = 0;
                IBuffer *pBuffs[] = {grp.m_meshVertexBuffer};
                m_immediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                     SET_VERTEX_BUFFERS_FLAG_RESET);
                m_immediateContext->SetIndexBuffer(grp.m_meshIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
                DrawAttrs.IndexType = VT_UINT16; // Index type
                DrawAttrs.NumIndices = grp.m_indices.size();
                // Verify the state of vertex and index buffers as well as consistence of
                // render targets and correctness of draw command arguments
                DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
                m_immediateContext->DrawIndexed(DrawAttrs);
            }
        }
    }
}

void Engine::createCSMPipeline()
{
    for (uint i = 0; i < Diligent::FirstPersonCamera::getNbCascade(); ++i)
    {
        TextureDesc texDesc;
        texDesc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
        texDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_DEPTH_STENCIL;
        texDesc.Height = 4096;
        texDesc.Width = 4096;
        texDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        eastl::string cascadeName("Cascade ");
        cascadeName += std::to_string(i).c_str();
        texDesc.Name = cascadeName.c_str();
        texDesc.Usage = Diligent::USAGE_DEFAULT;

        RefCntAutoPtr<ITexture> tex;
        m_device->CreateTexture(texDesc, nullptr, &tex);
        m_cascadeTextures.emplace_back(tex);

        m_registeredTexturesForDebug.push_back(tex);
    }
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 0;
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.DepthStencilDesc.DepthEnable = True;
    desc.DepthStencilDesc.DepthWriteEnable = True;
    desc.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_FRONT;

    eastl::vector<LayoutElement> layoutElements;
    if (m_isVertexPacked)
    {
        layoutElements = layoutElementsPacked;
    }
    else
    {
        layoutElements = {
                LayoutElement(0, 0, 3, VT_FLOAT32, False),
                LayoutElement(1, 0, 3, VT_FLOAT32, True),
                LayoutElement(2, 0, 2, VT_FLOAT32, False)
        };
    }

    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    m_pipelines[PSO_CSM] = eastl::make_unique<PipelineState>(m_device, "CSM", PIPELINE_TYPE_GRAPHICS,
                                                             "csm",
                                                             eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                             vars,
                                                             eastl::vector<PipelineState::VarStruct>(), desc,
                                                             layoutElements);
}

uint32_t Engine::addImportProgress(const char *_name)
{
    std::scoped_lock lock(m_mutexImportProgress);
    m_importProgressMap[m_idImporterProgress].first = _name;
    m_importProgressMap[m_idImporterProgress].second = 0;
    return m_idImporterProgress++;
}

void Engine::updateImportProgress(uint32_t _id, float _percentage)
{
    m_importProgressMap[_id].second = _percentage;
}

void Engine::removeImportProgress(uint32_t _id)
{
    m_importProgressMap.erase(m_importProgressMap.find_as(_id));
}

void Engine::showProgressIndicators()
{
    ImGui::Begin("Progress Indicators");

    const ImU32 col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);
    for (auto &nameAndProgress: m_importProgressMap)
    {
        ImGui::Text("%s %d", nameAndProgress.second.first.c_str(), nameAndProgress.first);
        ImGui::Spinner("##spinner", 15, 6, col);
        ImGui::SameLine();
        ImGui::BufferingBar("##buffer_bar", nameAndProgress.second.second, ImVec2(400, 6), bg, col);
    }

    ImGui::End();
}

void Engine::frustrumCulling()
{
    m_meshesSortedAndCulled.clear();

    ViewFrustum viewFrustum;
    ExtractViewFrustumPlanesFromMatrix(m_camera.GetViewMatrix() * m_camera.GetProjMatrix(),
                                       viewFrustum, false);

    for(Mesh* m : m_meshes)
    {
        if(m && m->isLoaded())
        {
            auto &groups = m->getGroups();
            const auto &model = m->getModel();
            for(Mesh::Group& grp : groups)
            {
                if (GetBoxVisibility(viewFrustum, grp.m_aabb.Transform(model)) == Diligent::BoxVisibility::Invisible)
                {
                    continue;
                }
            }
        }

    }
}

void Engine::createSkydomeTexturePipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    desc.DepthStencilDesc.DepthEnable = True;
    desc.DepthStencilDesc.DepthWriteEnable = True;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.FrontCounterClockwise = True;
    desc.RasterizerDesc.CullMode = CULL_MODE_BACK;

    eastl::vector<LayoutElement> layoutElements;
    layoutElements = {
            LayoutElement(0, 0, 3, VT_FLOAT32, False)
    };


    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    m_pipelines[PSO_SKYDOME_CREATE] = eastl::make_unique<PipelineState>(m_device, "Skydome_create", PIPELINE_TYPE_GRAPHICS,
                                                                        "skydome_create",
                                                                        eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                        vars,
                                                                        eastl::vector<PipelineState::VarStruct>(), desc,
                                                                        layoutElements);

    uint32_t indices[] =
            {
                    0, 1, 2,    // side 1
                    2, 1, 3,
                    4, 0, 6,    // side 2
                    6, 0, 2,
                    7, 5, 6,    // side 3
                    6, 5, 4,
                    3, 1, 7,    // side 4
                    7, 1, 5,
                    4, 5, 0,    // side 5
                    0, 5, 1,
                    3, 7, 2,    // side 6
                    2, 7, 6,
            };


    const float vertices[] {
             -1.0f, 1.0f, -1.0f,
             1.0f, 1.0f, -1.0f,
             -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             -1.0f, 1.0f, 1.0f,
             1.0f, 1.0f, 1.0f,
             -1.0f, -1.0f, 1.0f,
             1.0f, -1.0f, 1.0f,
    };

    BufferDesc bufferDesc;
    bufferDesc.Usage = Diligent::USAGE_IMMUTABLE;
    bufferDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    bufferDesc.ElementByteStride = sizeof(float3);
    bufferDesc.Size = sizeof(vertices);
    bufferDesc.Name = "Vertex Buffer Cube Debug";

    BufferData bufferData;
    bufferData.DataSize = sizeof(vertices);
    bufferData.pData = vertices;

    Engine::instance->getDevice()->CreateBuffer(bufferDesc, &bufferData, &m_bufferVerticesSkyDome);


    bufferDesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    bufferDesc.ElementByteStride = sizeof(uint32_t);
    bufferDesc.Size = sizeof(indices);
    bufferDesc.Name = "Index Buffer Cube Debug";

    bufferData.pData = indices;
    bufferData.DataSize = sizeof(indices);

    Engine::instance->getDevice()->CreateBuffer(bufferDesc, &bufferData, &m_bufferIndicesSkyDome);

    const auto &pso = m_pipelines[PSO_SKYDOME_CREATE];
    pso->getSRB().GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_TextureAlbedo")->Set(m_defaultTextures["hdr"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));

    {
        TextureDesc texDesc;
        texDesc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
        texDesc.Width = texDesc.Height = 512;
        texDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        texDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;

        glm::mat4 captureViews[] =
        {
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        memcpy_s(m_skyBoxViewMatrices.data(), sizeof(float4x4) * m_skyBoxViewMatrices.size(), captureViews, sizeof(captureViews));

        for (int i = 0; i < 6; ++i)
        {
            eastl::string name;
            name = (std::to_string(i) + " CubeMap ").c_str();
            texDesc.Name = name.c_str();
            m_device->CreateTexture(texDesc, nullptr, &m_skyBoxCubeMap[i]);

            m_registeredTexturesForDebug.push_back(m_skyBoxCubeMap[i]);
        }

    }
}

void Engine::renderCubeMapInTextures()
{
    GPUScopedMarker("HDR");
    GPUScopedMarker("CreateCubeMap");
    const auto &pso = m_pipelines[PSO_SKYDOME_CREATE];

    float4x4 projCubeMap = float4x4::Projection(PI_F / 2.0f, 1.0f, 0.01f, 10.0f, false);

    m_immediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Viewport viewport;
    viewport.Width = viewport.Height = 512.0f;

    m_immediateContext->SetViewports(1, &viewport, 512, 512);
    m_immediateContext->SetPipelineState(pso->getPipeline());
    m_immediateContext->CommitShaderResources(&pso->getSRB(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);


    IBuffer* buffer = {m_bufferVerticesSkyDome};
    const uint64_t* offset = {nullptr};

    m_immediateContext->SetVertexBuffers(0, 1, &buffer, offset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
    m_immediateContext->SetIndexBuffer(m_bufferIndicesSkyDome, 0,  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );

    DrawIndexedAttribs attribs;
    attribs.IndexType = Diligent::VT_UINT32;
    attribs.BaseVertex = 0;
    attribs.NumIndices = m_bufferIndicesSkyDome->GetDesc().Size / sizeof(uint32_t);
    attribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
    for (int i = 0; i < m_skyBoxViewMatrices.size(); ++i)
    {
        ITextureView *pRTV[] = {m_skyBoxCubeMap[i]->GetDefaultView(

                Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        m_immediateContext->SetRenderTargets(1, pRTV, nullptr,
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        /*float clearColor[4] = {0};
        m_immediateContext->ClearRenderTarget(*pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);*/

        {
            struct ConstantsSkyDome {
                float4x4 g_WorldViewProj;
            };

            // Map the buffer and write current world-view-projection matrix
            MapHelper<ConstantsSkyDome> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants->g_WorldViewProj = (m_skyBoxViewMatrices[i] * projCubeMap).Transpose();
        }
        m_immediateContext->DrawIndexed(attribs);
    }

    CopyTextureAttribs copyTexAttribs;
    copyTexAttribs.pDstTexture = m_skyBoxCubeTexture;
    copyTexAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copyTexAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;


    viewport.Width = 1280;
    viewport.Height = 720;

    m_immediateContext->SetViewports(1, &viewport, 1280, 720);

    m_immediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    for (int i = 0; i < 6; ++i)
    {
        copyTexAttribs.pSrcTexture = m_skyBoxCubeMap[i];
        copyTexAttribs.DstSlice = i;

        m_immediateContext->CopyTexture(copyTexAttribs);
    }


}

void Engine::createCubeMapPipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = m_swapChain->GetDesc().ColorBufferFormat;
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.DepthStencilDesc.DepthEnable = True;
    desc.DepthStencilDesc.DepthWriteEnable = True;
    desc.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.FrontCounterClockwise = True;
    desc.RasterizerDesc.CullMode = CULL_MODE_BACK;

    eastl::vector<LayoutElement> layoutElements = {
        LayoutElement(0, 0, 3, VT_FLOAT32, False)
    };


    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    m_pipelines[PSO_CUBEMAP] = eastl::make_unique<PipelineState>(m_device, "cubemap", PIPELINE_TYPE_GRAPHICS,
                                                                        "cubemap",
                                                                        eastl::vector<eastl::pair<eastl::string, eastl::string>>(),
                                                                        vars,
                                                                        eastl::vector<PipelineState::VarStruct>(), desc,
                                                                        layoutElements);

    TextureDesc texDesc;
    texDesc.Format = TEX_FORMAT_RGBA16_FLOAT;
    texDesc.ArraySize = 6;
    texDesc.Type = RESOURCE_DIM_TEX_CUBE;
    texDesc.Width = texDesc.Height = 512;
    texDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;

    m_device->CreateTexture(texDesc, nullptr, &m_skyBoxCubeTexture);

    auto& pso = m_pipelines[PSO_CUBEMAP];
    auto resource = pso->getSRB().GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureCube");
    resource->Set(m_skyBoxCubeTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

}

void Engine::createDepthMinMaxPipeline()
{

    {
        BufferDesc desc;
        desc.Mode = BUFFER_MODE_STRUCTURED;
        desc.ElementByteStride = sizeof(uint) * 6;
        desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        desc.Size = sizeof(uint) * 6;
        desc.Name = "spdGlobalAtomic";

        BufferData data;
        uint dataSet[6] = { 0 };
        data.pData = dataSet;
        data.DataSize = sizeof(dataSet);

        m_device->CreateBuffer(desc, &data, &m_SPDGlobalAtomicBuffer);
    }

    {
        BufferDesc desc;
        desc.Size = sizeof(uint4);
        desc.ElementByteStride = sizeof(uint4);
        desc.Name = "spdConstants";
        desc.Usage = USAGE_DYNAMIC;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;

        m_device->CreateBuffer(desc, nullptr, &m_SPDConstantBuffer);
    }

    {
        TextureDesc desc;
        desc.Format = m_swapChain->GetDesc().DepthBufferFormat;
        desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        desc.Height = m_height;
        desc.Width = m_width;

        m_device->CreateTexture(desc, nullptr, &m_SPDTextureOutputArray);
    }

    {
        TextureDesc desc;
        desc.Format = m_swapChain->GetDesc().DepthBufferFormat;
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Height = m_height;
        desc.Width = m_width;

        m_device->CreateTexture(desc,nullptr, &m_SPDTexture6);
    }


    eastl::vector<PipelineState::VarStruct> staticVars = {
        {
            {SHADER_TYPE_COMPUTE, "spdConstants", m_SPDConstantBuffer}
        }
    };
    eastl::vector<PipelineState::VarStruct> dynamicVars =
            {
                    {SHADER_TYPE_COMPUTE, "imgDst", m_SPDTextureOutputArray->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS)},
                    {SHADER_TYPE_COMPUTE, "imgDst6", m_SPDTexture6->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS)},
                        {SHADER_TYPE_COMPUTE, "spdGlobalAtomic", m_SPDGlobalAtomicBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS)}
            };

    eastl::vector<eastl::pair<eastl::string, eastl::string >> shaderDefines;
    shaderDefines.push_back({"SPD_MIN", "1"});
    m_pipelines[PSO_DEPTH_MIN] = eastl::make_unique<PipelineState>(m_device, "Compute Depth Min Max PSO - Min",
                                                                  PIPELINE_TYPE_COMPUTE, "depth_min_max", shaderDefines,
                                                                  staticVars, dynamicVars);
    shaderDefines.clear();
    shaderDefines.push_back({"SPD_MAX", "1"});


    m_pipelines[PSO_DEPTH_MAX] = eastl::make_unique<PipelineState>(m_device, "Compute Depth Min Max PSO - Max",
                                                                  PIPELINE_TYPE_COMPUTE, "depth_min_max", shaderDefines,
                                                                  staticVars, dynamicVars);
}

void Engine::createPrecomputeIrradiancePipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    desc.DepthStencilDesc.DepthEnable = False;
    desc.DepthStencilDesc.DepthWriteEnable = False;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.FrontCounterClockwise = True;
    desc.RasterizerDesc.CullMode = CULL_MODE_BACK;

    eastl::vector<LayoutElement> layoutElements = {
        LayoutElement(0, 0, 3, VT_FLOAT32, False)
    };

    eastl::vector<PipelineState::VarStruct> staticVars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

    eastl::vector<PipelineState::VarStruct> dynamicVars = {{SHADER_TYPE_PIXEL, "g_TextureCube", m_skyBoxCubeTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)}};

    eastl::vector<eastl::pair<eastl::string, eastl::string >> shaderDefines;
    m_pipelines[PSO_PRECOMPUTE_IRRADIANCE] = eastl::make_unique<PipelineState>(m_device, "Precompute Irradiance",
                                                                  PIPELINE_TYPE_GRAPHICS, "precompute_irradiance", shaderDefines,
                                                                  staticVars, dynamicVars, desc, layoutElements);

    TextureDesc texDesc;
    texDesc.Format = TEX_FORMAT_RGBA16_FLOAT;
    texDesc.Type = RESOURCE_DIM_TEX_2D;
    texDesc.Width = texDesc.Height = 32;
    texDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;

    for (int i = 0; i < 6; ++i)
    {
        eastl::string name;
        name = (std::to_string(i) + " PrecomputeIrradianceMap ").c_str();
        texDesc.Name = name.c_str();
        m_device->CreateTexture(texDesc, nullptr, &m_irradiancePrecomputed[i]);

        m_registeredTexturesForDebug.push_back(m_irradiancePrecomputed[i]);
    }
}

void Engine::renderPrecomputeIrradiance()
{
    GPUScopedMarker("PreCompute Irradiance");

    //TODO @fsantoro bind the texture to output the irradiance to

    const auto &pso = m_pipelines[PSO_PRECOMPUTE_IRRADIANCE];

    m_immediateContext->SetPipelineState(pso->getPipeline());
    m_immediateContext->CommitShaderResources(&pso->getSRB(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    IBuffer* buffer = {m_bufferVerticesSkyDome};
    const uint64_t* offset = {nullptr};

    m_immediateContext->SetVertexBuffers(0, 1, &buffer, offset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
    m_immediateContext->SetIndexBuffer(m_bufferIndicesSkyDome, 0,  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );

    const float4x4 projCubeMap = float4x4::Projection(PI_F / 2.0f, 1.0f, 0.01f, 10.0f, false);

    DrawIndexedAttribs attribs;
    attribs.IndexType = Diligent::VT_UINT32;
    attribs.BaseVertex = 0;
    attribs.NumIndices = m_bufferIndicesSkyDome->GetDesc().Size / sizeof(uint32_t);
    attribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

    m_immediateContext->SetViewports(1, nullptr, 32, 32);

    for (int i = 0; i < m_skyBoxViewMatrices.size(); ++i)
    {
        ITextureView *pRTV[] = {m_irradiancePrecomputed[i]->GetDefaultView(

                Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        m_immediateContext->SetRenderTargets(1, pRTV, nullptr,
                                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        {
            struct ConstantsSkyDome {
                float4x4 g_WorldViewProj;
            };

            // Map the buffer and write current world-view-projection matrix
            MapHelper<ConstantsSkyDome> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants->g_WorldViewProj = (m_skyBoxViewMatrices[i] * projCubeMap).Transpose();
        }
        m_immediateContext->DrawIndexed(attribs);
    }


    m_immediateContext->SetViewports(1, nullptr, m_width, m_height);
}


void Engine::renderCubeMap()
{
    GPUScopedMarker("DrawCubeMap");
    const auto &pso = m_pipelines[PSO_CUBEMAP];

    m_immediateContext->SetPipelineState(pso->getPipeline());
    m_immediateContext->CommitShaderResources(&pso->getSRB(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    IBuffer* buffer = {m_bufferVerticesSkyDome};
    const uint64_t* offset = {nullptr};

    m_immediateContext->SetVertexBuffers(0, 1, &buffer, offset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
    m_immediateContext->SetIndexBuffer(m_bufferIndicesSkyDome, 0,  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );

    DrawIndexedAttribs attribs;
    attribs.IndexType = Diligent::VT_UINT32;
    attribs.BaseVertex = 0;
    attribs.NumIndices = m_bufferIndicesSkyDome->GetDesc().Size / sizeof(uint32_t);
    attribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

    {
        struct ConstantsSkyDome {
            float4x4 projection;
            float4x4 view;
        };

        MapHelper<ConstantsSkyDome> CBConstants(m_immediateContext, m_bufferMatrixMesh, MAP_WRITE, MAP_FLAG_DISCARD);
        CBConstants->projection = m_camera.GetProjMatrix().Inverse().Transpose();
        CBConstants->view = m_camera.GetViewMatrix().Transpose();
    }
    m_immediateContext->DrawIndexed(attribs);

}

void Engine::SortMeshes()
{
    {
        std::scoped_lock lock (m_mutexAddMesh);

        for(auto& mesh : m_meshToAdd)
        {
            m_meshes.emplace_back(mesh);

            if (!mesh->isTransparent())
            {
                m_meshOpaque.insert(mesh);
            }
            else
            {
                m_meshTransparent.insert(mesh);
            }

            auto &groups = mesh->getGroups();
            for (auto &group: groups)
            {
                //m_raytracing->addMesh(m_device, m_immediateContext, group, mesh->getName());
                m_heapTextures.insert(m_heapTextures.end(), group.m_textures.begin(), group.m_textures.end());
            }
        }

       m_meshToAdd.clear();
    }

}