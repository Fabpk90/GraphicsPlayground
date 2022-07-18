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
#include <array>
#include <memory>
#include <EASTL/unique_ptr.h>
#include "ImGuiImplWin32.hpp"
#include "Graphics/GraphicsTools/interface/DurationQueryHelper.hpp"
#include "Tracy.hpp"
#include "RayTracing.hpp"
#include "TextureUtilities.h"
#include "im3d/im3d.h"

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

#if defined(_DEBUG)
            EngineCI.EnableValidation = TRUE;
            EngineCI.ValidationFlags = Diligent::VALIDATION_FLAG_CHECK_SHADER_BUFFER_SIZE;
            EngineCI.D3D12ValidationFlags = Diligent::D3D12_VALIDATION_FLAG_ENABLE_GPU_BASED_VALIDATION;
#endif

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

            EngineCI.Features.IndependentBlend = Diligent::DEVICE_FEATURE_STATE_ENABLED;
            EngineCI.Features.DualSourceBlend = Diligent::DEVICE_FEATURE_STATE_ENABLED;

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
    createFullScreenResources();
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

    eastl::vector<PipelineState::VarStruct> vars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh} };

    m_pipelines[PSO_GBUFFER] = eastl::make_unique<PipelineState>(m_device, "Simple Mesh PSO", PIPELINE_TYPE_GRAPHICS, "gbuffer", vars, eastl::vector<PipelineState::VarStruct>()
                                                               , desc, layoutElements);

    m_camera.SetPos(float3(0, 0, -20));
    m_camera.SetProjAttribs(0.01f, 1000, (float)m_width / (float) m_height, 45.0f, Diligent::SURFACE_TRANSFORM_OPTIMAL, false);

    createLightingPipeline();
    createTransparencyPipeline();

    m_meshes.resize(3, nullptr);

    m_executor.silent_async([&](){
        m_meshes[0] = new Mesh(m_device, "mesh/Bunny/stanford-bunny.obj");
        m_meshes[0]->setTransparent(true);
    });

    m_executor.silent_async([&](){
        m_meshes[1] = new Mesh(m_device, "mesh/cerberus/Cerberus_LP.fbx", true);

        m_meshes[1]->getModel() *= float4x4::Translation(0, 0, 10);
        m_meshes[1]->addTexture("Textures/Cerberus_N.tga", 0);

        m_meshes[1]->setIsLoaded(true);
    });

    m_executor.silent_async([&](){
        //m_meshes[2] = new Mesh(m_device, "mesh/Sponza/Sponza.fbx");
    });
}

void Engine::render()
{
    if(m_isMinimized) return;

    if(m_inputController.IsKeyDown(Diligent::InputKeys::R))
    {
        for (auto& pso : m_pipelines)
        {
            pso.second->reload();
        }
    }

    {
        if(m_inputController.GetMouseState().ButtonFlags & Diligent::MouseState::BUTTON_FLAG_LEFT)
        {
            ZoneScopedN("RayCast Selection")

            const float4 mousePos(m_inputController.GetMouseState().PosX / (float)m_width, m_inputController.GetMouseState().PosY / (float)m_height, 0, 1);
            float3 rayOrigin = mousePos * (m_camera.GetProjMatrix() * m_camera.GetViewMatrix()).Inverse();
            rayOrigin.z = m_camera.GetPos().z;

            float3 testRay = mousePos * (m_camera.GetViewMatrix() * m_camera.GetProjMatrix() ).Inverse();

            float enterDist = 0;
            float exitDist = 0;

            const float4x4 mvp = m_camera.GetWorldMatrix();

            for (auto* mesh : m_meshes)
            {
                if(mesh && mesh->isClicked(mvp, rayOrigin, normalize(rayOrigin - m_camera.GetPos()), enterDist, exitDist))
                {
                    m_clickedMesh = mesh;
                    //std::cout << "found ! " << std::endl;
                    break;
                }
            }
        }
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

    auto& psoGBuffer = m_pipelines[PSO_GBUFFER];

    m_immediateContext->SetPipelineState(psoGBuffer->getPipeline());

    const float ClearColor[] = {0.0f, 181.f / 255.f, 221.f / 255.f, 1.0f};
    const float ClearColorNormal[] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Let the engine perform required state transitions
    m_immediateContext->ClearRenderTarget(pRTV[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearRenderTarget(pRTV[1], ClearColorNormal, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearDepthStencil(pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    for(Mesh* m : m_meshes)
    {
        if(m && m->isLoaded() && !m->isTransparent())
        {
            m->draw(m_immediateContext, &psoGBuffer->getSRB(), m_camera, m_bufferMatrixMesh, m_defaultTextures);
        }
    }

    renderLighting();
    renderTransparency();

    endCollectingStats();
    uiPass();

    FrameMark;
}

void Engine::uiPass()
{
    ITextureView* view[] = {m_swapChain->GetCurrentBackBufferRTV()};

    m_immediateContext->SetRenderTargets(1, view,
                                         m_swapChain->GetDepthBufferDSV()
            , RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_imguiRenderer->NewFrame(m_width, m_height, SURFACE_TRANSFORM_IDENTITY);
    ImGui::Begin("Inspector");
    ImGui::Text("%f %f %f", m_camera.GetPos().x, m_camera.GetPos().y, m_camera.GetPos().z);
    ImGui::SliderFloat4("Pos light: ", m_lightPos.Data(), -5, 5);

    for(Mesh* m : m_meshes)
    {
        if(m && m->isLoaded())
        {
            m->drawInspector();
        }
    }
    ImGui::End();

    debugTextures();
    showStats();
    showFrameTimeGraph();
    showGizmos();

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

    eastl::vector<PipelineState::VarStruct> staticVars = {{SHADER_TYPE_COMPUTE, "Constants", m_bufferLighting}};
    eastl::vector<PipelineState::VarStruct> dynamicVars =
    {
        {SHADER_TYPE_COMPUTE, "g_color", m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
        {SHADER_TYPE_COMPUTE, "g_normal",m_gbuffer->GetTextureType(GBuffer::EGBufferType::Normal)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
        {SHADER_TYPE_COMPUTE, "g_depth", m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth)->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)},
        {SHADER_TYPE_COMPUTE, "g_output", m_gbuffer->GetTextureType(GBuffer::EGBufferType::Output)->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS)}
    };

    m_pipelines[PSO_LIGHTING] = eastl::make_unique<PipelineState>(m_device, "Compute Lighting PSO", PIPELINE_TYPE_COMPUTE, "lighting", staticVars, dynamicVars);
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

    CopyTextureAttribs attribs;
    attribs.pDstTexture = m_swapChain->GetCurrentBackBufferRTV()->GetTexture();
    attribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.pSrcTexture = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Output);
    attribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_immediateContext->CopyTexture(attribs);
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
    for(auto* mesh : m_meshes)
    {
        delete mesh;
    }

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

    RefCntAutoPtr<ITexture> texRedTransparent;
    CreateTextureFromFile("textures/defaults/redTransparent16x16.png", info, m_device, &texRedTransparent);
    m_defaultTextures["redTransparent"] = texRedTransparent;
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
    texDesc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    texDesc.Name = "Reveal Term";

    m_device->CreateTexture(texDesc, nullptr, &m_revealTermTexture);

    texDesc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    texDesc.Name = "Accum Color";
    m_device->CreateTexture(texDesc, nullptr, &m_accumColorTexture);

    {
        GraphicsPipelineDesc desc;
        desc.NumRenderTargets = 2;
        desc.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        desc.RTVFormats[1] = Diligent::TEX_FORMAT_R32_FLOAT;
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
        desc.BlendDesc.RenderTargets[1].RenderTargetWriteMask = Diligent::COLOR_MASK_RED;
       // desc.BlendDesc.RenderTargets[1].DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC1_ALPHA;
       // desc.BlendDesc.RenderTargets[1].SrcBlendAlpha = Diligent::BLEND_FACTOR_ZERO;

        eastl::vector<LayoutElement> layoutElements = {
                LayoutElement(0, 0, 3, VT_FLOAT32, False),
                LayoutElement (1, 0, 3, VT_FLOAT32, True),
                LayoutElement (2, 0, 2, VT_FLOAT32, False)
        };

       eastl::vector<PipelineState::VarStruct> staticVars = {{SHADER_TYPE_VERTEX, "Constants", m_bufferMatrixMesh}};

       eastl::vector<PipelineState::VarStruct> dynamicVars =
                {{SHADER_TYPE_PIXEL, "g_TextureAlbedo",
                  m_defaultTextures["redTransparent"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)}};

       m_pipelines[PSO_TRANSPARENCY] = eastl::make_unique<PipelineState>(m_device, "Transparency PSO",
                                                                          PIPELINE_TYPE_GRAPHICS, "transparency",
                                                                          staticVars,
                                                                          dynamicVars, desc, layoutElements);
    }

    GraphicsPipelineDesc desc;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = m_swapChain->GetDesc().ColorBufferFormat;
    desc.DSVFormat = m_swapChain->GetDesc().DepthBufferFormat;
    desc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    desc.DepthStencilDesc.DepthWriteEnable = False;
    desc.DepthStencilDesc.DepthEnable = False;

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

    m_pipelines[PSO_TRANSPARENCY_COMPOSE] = eastl::make_unique<PipelineState>(m_device, "Transparency Compose PSO", PIPELINE_TYPE_GRAPHICS,
                                                                              "transparency_compose",
                                                                              eastl::vector<PipelineState::VarStruct>(),
                                                                              dynamicVars, desc);

}

void Engine::renderTransparency()
{
    {
        ITextureView* pRTV[] = {m_accumColorTexture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)
                , m_revealTermTexture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        auto pDSV = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();

        m_immediateContext->SetRenderTargets(2, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL)
                , RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const float4 clearValue[] = {{1, 0, 0, 0}, {0, 0, 0, 0}};
        m_immediateContext->ClearRenderTarget(pRTV[1], clearValue[0].Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_immediateContext->ClearRenderTarget(pRTV[0], clearValue[1].Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->SetPipelineState(m_pipelines[PSO_TRANSPARENCY]->getPipeline());

        for (auto* mesh: m_meshes)
        {
            if(mesh && mesh->isLoaded() && mesh->isTransparent())
            {
                mesh->draw(m_immediateContext, &m_pipelines[PSO_TRANSPARENCY]->getSRB(), m_camera, m_bufferMatrixMesh, m_defaultTextures);
            }
        }
    }


    //Compose phase
    {
        DrawAttribs attribs;
        attribs.FirstInstanceLocation = 0;
        attribs.NumInstances = 1;
        attribs.NumVertices = 3;
        attribs.StartVertexLocation = 0;
        attribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

        ITextureView* pRTV[] = {m_gbuffer->GetTextureType(GBuffer::EGBufferType::Albedo)->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
        auto pDSV = m_gbuffer->GetTextureType(GBuffer::EGBufferType::Depth);// m_swapChain->GetDepthBufferDSV();

        m_immediateContext->SetRenderTargets(1, pRTV, pDSV->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL)
                , RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->SetPipelineState(m_pipelines[PSO_TRANSPARENCY_COMPOSE]->getPipeline());
        m_immediateContext->CommitShaderResources(&m_pipelines[PSO_TRANSPARENCY_COMPOSE]->getSRB(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_immediateContext->Draw(attribs);
    }


}

void Engine::showFrameTimeGraph()
{
  /*  const float width = ImGui::GetWindowWidth();
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
            const float frameHeight = glm::mix(minHeight, maxHeight, frameHeightFactor_Nrm);
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
    const float3 positions[] = { float3(0), float3(0), float3(0) };
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
    if(!m_clickedMesh) return;

    Im3d::NewFrame();

    Im3d::Vec3 translation = Im3d::Vec3(m_clickedMesh->getTranslation().x, m_clickedMesh->getTranslation().y, m_clickedMesh->getTranslation().z);
    Im3d::Mat3 rotation(1.0f);
    Im3d::Vec3 scale(m_clickedMesh->getScale().x, m_clickedMesh->getTranslation().y, m_clickedMesh->getTranslation().z);

    Im3d::PushMatrix(Im3d::Mat4(translation, rotation, scale));

    if (Im3d::GizmoTranslation("UnifiedGizmo", m_clickedMesh->getTranslation().Data())) {
        // transform was modified, do something with the matrix
    }

    Im3d::PopMatrix();

    Im3d::EndFrame();
}

void Engine::debugTextures()
{
    ImGui::Begin("Texture Debugger", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    m_imguiFilter.Draw();

    const auto texSize = ImVec2(128, 128);
    if(m_imguiFilter.IsActive())
    {
        for(auto* tex : m_registeredTexturesForDebug)
        {
            const auto &desc = tex->GetDesc();
            const eastl::string texName = desc.Name;

            if(m_imguiFilter.PassFilter(texName.data(), texName.data() + texName.size()))
            {
                ImGui::Text("%s", texName.c_str());
                ImGui::Image(tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), texSize);
            }

        }
    }
    else
    {
        for(auto* tex : m_registeredTexturesForDebug)
        {
            const auto &desc = tex->GetDesc();
            ImGui::Text("%s", desc.Name);
            ImGui::Image(tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), texSize);
        }
    }


    ImGui::End();

}
