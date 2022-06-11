//
// Created by FanyMontpell on 03/09/2021.
//

#define PLATFORM_WIN32 1
#ifndef ENGINE_DLL
#    define ENGINE_DLL 1
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>


#include "Engine.h"
#include "EngineFactoryVk.h"
#include "EngineFactoryD3D12.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "imgui.h"
#include <array>
#include <memory>
#include <EASTL/unique_ptr.h>
#include "ImGuiImplWin32.hpp"
#include "Graphics/GraphicsTools/interface/DurationQueryHelper.hpp"
#include "Tracy.hpp"
#include "RayTracing.hpp"
#include "TextureUtilities.h"

Engine* Engine::instance = nullptr;

bool Engine::initializeDiligentEngine(HWND hWnd)
{
    SwapChainDesc SCDesc;
    SCDesc.Usage = Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET;

    void* devicePointer = nullptr;

    switch (getRenderType())
    {
        case RENDER_DEVICE_TYPE_D3D12:
        {
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D12() function
            auto GetEngineFactoryD3D12 = LoadGraphicsEngineD3D12();
#    endif
            EngineD3D12CreateInfo EngineCI;

            auto* pFactoryD3D12 = GetEngineFactoryD3D12();
            pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_device, &m_immediateContext);

            m_engineFactory = pFactoryD3D12;

            Win32NativeWindow Window{hWnd};
            pFactoryD3D12->CreateSwapChainD3D12(m_device, m_immediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_swapChain);
        }
        break;

        case RENDER_DEVICE_TYPE_VULKAN:
        {
#    if EXPLICITLY_LOAD_ENGINE_VK_DLL
            // Load the dll and import GetEngineFactoryVk() function
            auto GetEngineFactoryVk = LoadGraphicsEngineVk();
#    endif
            EngineVkCreateInfo EngineCI;

            auto* pFactoryVk = GetEngineFactoryVk();
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

    m_imguiRenderer = new ImGuiImplWin32(hWnd, m_device, m_swapChain->GetDesc().ColorBufferFormat, m_swapChain->GetDesc().DepthBufferFormat);
    auto& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";

    //if(getRenderType() == Diligent::RENDER_DEVICE_TYPE_VULKAN)
    {
        //m_renderdoc = new RenderDocHook(devicePointer, hWnd);
    }

    m_raytracing = new RayTracing(m_device);

    return true;
}

void Engine::createResources()
{
    initStatsResources();
    createDefaultTextures();
    m_gbuffer = new GBuffer(float2(m_width, m_height));

    m_engineFactory->CreateDefaultShaderSourceStreamFactory("shader/", &m_ShaderSourceFactory);

    GraphicsPipelineDesc desc;
    desc.NumRenderTargets             = 2;
    desc.RTVFormats[0]                = m_swapChain->GetDesc().ColorBufferFormat;
    desc.RTVFormats[1]                = TEX_FORMAT_RGBA8_UNORM;
    desc.DSVFormat                    = m_swapChain->GetDesc().DepthBufferFormat;
    desc.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode      = Diligent::CULL_MODE_BACK;

    BufferDesc cbDesc;
    cbDesc.Usage = Diligent::USAGE_DYNAMIC;
    cbDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cbDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cbDesc.Size = sizeof(float4x4);
    cbDesc.Name = "UBO";
    m_device->CreateBuffer(cbDesc, nullptr, &m_bufferMatrixMesh);

    eastl::vector<LayoutElement> layoutElements = {
            LayoutElement(0, 0, 3, VT_FLOAT32, False),
            LayoutElement (1, 0, 3, VT_FLOAT32, True),
            LayoutElement (2, 0, 2, VT_FLOAT32, False)
    };

    eastl::vector<PipelineState::StaticVarsStruct> vars = { {SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh} };

    m_pipelines[PSO_GBUFFER] = eastl::make_unique<PipelineState>(m_device, "Simple Mesh PSO", PIPELINE_TYPE_GRAPHICS, "gbuffer", vars
                                                               , desc, layoutElements);

    m_camera.SetPos(float3(0, 0, -20));
    m_camera.SetProjAttribs(0.01f, 1000, (float)m_width / (float) m_height, 45.0f, Diligent::SURFACE_TRANSFORM_OPTIMAL, false);

    createLightingPipeline();
    createTransparencyPipeline();

    m_meshes.push_back(new Mesh(m_device, "mesh/Bunny/stanford-bunny.obj"));
    m_meshes.push_back(new Mesh(m_device, "mesh/cerberus/Cerberus_LP.fbx"));
    //  m_meshes.push_back(new Mesh(m_device, "mesh/Sponza/Sponza.fbx"));
    m_meshes[1]->getModel() *= float4x4::Translation(0, 0, 10);
    m_meshes[1]->addTexture("Textures/Cerberus_N.tga", 0);
}

void Engine::render()
{
    if(m_isMinimized) return;

    if(m_inputController.IsKeyDown(Diligent::InputKeys::R))
    {
        ZoneScopedN("Reload pipeline");
        m_pipelines[PSO_LIGHTING]->reload();
    }

    ZoneScopedN("Render");

    m_camera.Update(m_inputController, 0.16f);

    startCollectingStats();

    // Set render targets before issuing any draw command.
    // Note that Present() unbinds the back buffer if it is set as render target.
    ITextureView* pRTV[] = {m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)
                            , m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    auto pDSV = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();
    m_immediateContext->SetRenderTargets(2, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    auto& psoFinal = m_pipelines[PSO_GBUFFER];

    m_immediateContext->SetPipelineState(psoFinal->getPipeline());

    const float ClearColor[] = {0.0f, 181.f / 255.f, 221.f / 255.f, 1.0f};
    const float ClearColorNormal[] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Let the engine perform required state transitions
    m_immediateContext->ClearRenderTarget(pRTV[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearRenderTarget(pRTV[1], ClearColorNormal, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearDepthStencil(pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    for(Mesh* m : m_meshes)
    {
        m->draw(m_immediateContext, &psoFinal->getSRB(), m_camera, m_bufferMatrixMesh, m_defaultTextures);
    }

    renderLighting();

    CopyTextureAttribs attribs;
    attribs.pDstTexture = m_swapChain->GetCurrentBackBufferRTV()->GetTexture();
    attribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.pSrcTexture = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Output);
    attribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_immediateContext->CopyTexture(attribs);

    ITextureView* view[] = {m_swapChain->GetCurrentBackBufferRTV()};

    m_immediateContext->SetRenderTargets(1, view,
                                         m_swapChain->GetDepthBufferDSV()
                                         , RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    endCollectingStats();
    uiPass();
}

void Engine::uiPass()
{
    m_imguiRenderer->NewFrame(m_width, m_height, SURFACE_TRANSFORM_IDENTITY);
    ImGui::Begin("Inspector");
    ImGui::Text("%f %f %f", m_camera.GetPos().x, m_camera.GetPos().y, m_camera.GetPos().z);
    ImGui::SliderFloat4("Pos light: ", m_lightPos.Data(), -5, 5);

    for(Mesh* m : m_meshes)
    {
        m->drawInspector();
    }
    ImGui::End();

    ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
        auto &desc = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDesc();
        ImGui::Text("%s", desc.Name);
        ImGui::Image(
                m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                ImVec2(desc.Width / 4, desc.Height / 4));
    }
    {
        auto &desc = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDesc();
        ImGui::Text("%s", desc.Name);
        ImGui::Image(
                m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                ImVec2(desc.Width / 4, desc.Height / 4));
    }
    ImGui::End();

    showStats();
    m_imguiRenderer->Render(m_immediateContext);
}

void Engine::present()
{
    if(!m_isMinimized)
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

    eastl::vector<PipelineState::StaticVarsStruct> vars = { {SHADER_TYPE_COMPUTE, "Constants", m_bufferLighting} };

    m_pipelines[PSO_LIGHTING] = eastl::make_unique<PipelineState>(m_device, "Compute Lighting PSO", PIPELINE_TYPE_COMPUTE, "lighting", vars);
    auto& psoLighting = m_pipelines[PSO_LIGHTING];
    auto& srb = psoLighting->getSRB();

    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_color")
            ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));

    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_normal")
    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));

    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_depth")
            ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));

    //todo: change this when resizing (maybe a callback system for resize event ?)
    srb.GetVariableByName(SHADER_TYPE_COMPUTE, "g_output")
    ->Set(m_gbuffer->GetTextureType(GBuffer::EGBufferType::Output)->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS));
}

void Engine::renderLighting()
{
    ZoneScopedN("Render/Lighting - CPU");
    DispatchComputeAttribs dispatchComputeAttribs;
    dispatchComputeAttribs.ThreadGroupCountX = (m_width) / 8;
    dispatchComputeAttribs.ThreadGroupCountY = (m_height) / 8;

    auto& pipelineLighting = m_pipelines[PSO_LIGHTING];

    m_immediateContext->SetPipelineState(pipelineLighting->getPipeline());
    m_immediateContext->CommitShaderResources(&pipelineLighting->getSRB(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Constants cst;
    auto& camParams = m_camera.GetProjAttribs();
    cst.m_params = float4(m_width, m_height, camParams.NearClipPlane, camParams.FarClipPlane);
    cst.m_lightPos = m_lightPos == float4(0) ? float4(1) : normalize(m_lightPos);
    cst.m_camPos = m_camera.GetPos();

    {
        MapHelper<Constants> mapBuffer(m_immediateContext, m_bufferLighting, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        *mapBuffer = cst;
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
                              << static_cast<float>(m_DurationData.Duration) / static_cast<float>(m_DurationData.Frequency) * 1000.f << std::endl;
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
                          <<(m_DurationFromTimestamps * 1000) << std::endl;
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

void Engine::initStatsResources()
{
    // Check query support
    const auto& Features = m_device->GetDeviceInfo().Features;
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
    if(m_pPipelineStatsQuery)
    {
        m_pPipelineStatsQuery->Begin(m_immediateContext);
    }
    if(m_pOcclusionQuery)
    {
        m_pOcclusionQuery->Begin(m_immediateContext);
    }
     if(m_pDurationQuery)
     {
         m_pDurationQuery->Begin(m_immediateContext);
     }

     if(m_pDurationFromTimestamps)
     {
         m_pDurationFromTimestamps->Begin(m_immediateContext);
     }
}

void Engine::endCollectingStats()
{
    if(m_pDurationQuery)
    {
        m_pDurationQuery->End(m_immediateContext, &m_DurationData, sizeof(m_DurationData));
    }

    if(m_pOcclusionQuery)
    {
        m_pOcclusionQuery->End(m_immediateContext, &m_OcclusionData, sizeof(m_OcclusionData));
    }

    if(m_pPipelineStatsQuery)
    {
        m_pPipelineStatsQuery->End(m_immediateContext, &m_PipelineStatsData, sizeof(m_PipelineStatsData));
    }

    if(m_pDurationFromTimestamps)
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

    info.IsSRGB = False;
    info.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;

    RefCntAutoPtr<ITexture> texNormal;
    CreateTextureFromFile("textures/defaults/normal16x16.png", info, m_device, &texNormal);
    m_defaultTextures["normal"] = texNormal;
}

void Engine::createTransparencyPipeline()
{
    GraphicsPipelineDesc desc;
    desc.NumRenderTargets             = 1;
    desc.RTVFormats[0]                = m_swapChain->GetDesc().ColorBufferFormat;
    desc.DSVFormat                    = m_swapChain->GetDesc().DepthBufferFormat;
    desc.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode      = Diligent::CULL_MODE_BACK;

    desc.BlendDesc.RenderTargets[0].BlendEnable = True;
    desc.BlendDesc.RenderTargets[0].BlendOp = Diligent::BLEND_OPERATION_ADD;

    desc.BlendDesc.RenderTargets[0].SrcBlend = Diligent::BLEND_FACTOR_SRC1_ALPHA;
    //desc.BlendDesc.RenderTargets[0]. = Diligent::BLEND_FACTOR_SRC1_ALPHA;

    eastl::vector<LayoutElement> layoutElements = {
            LayoutElement(0, 0, 4, Diligent::VT_FLOAT32),
            LayoutElement(1, 0, 2, Diligent::VT_FLOAT32, True)
    };
    eastl::vector<PipelineState::StaticVarsStruct> vars = { {SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh} };

    m_pipelines[PSO_TRANSPARENCY] = eastl::make_unique<PipelineState>(m_device, "Transparency PSO", PIPELINE_TYPE_GRAPHICS, "transparency", vars
            , desc, layoutElements);
}
