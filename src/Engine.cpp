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
#include <diligent/include/Platforms/Win32/interface/Win32NativeWindow.h>
#include <diligent/include/Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h>
#include <diligent/include/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <diligent/include/Graphics/GraphicsTools/interface/MapHelper.hpp>

#include "Engine.h"

Engine* Engine::instance = nullptr;

bool Engine::initializeDiligentEngine(HWND hWnd)
{
    SwapChainDesc SCDesc;
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
            }
        }
        break;

        default:
            std::cerr << "Unknown/unsupported device type";
            return false;
            break;
    }

        return true;
}

void Engine::createResources()
{
    // Pipeline state object encompasses configuration of all GPU stages

    GraphicsPipelineStateCreateInfo PSOCreateInfo;

    // Pipeline state name is used by the engine to report issues.
    // It is always a good idea to give objects descriptive names.
    PSOCreateInfo.PSODesc.Name = "Simple Mesh PSO";

    // This is a graphics pipeline
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    // clang-format off
    // This tutorial will render to a single render target
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 2;
    // Set render target format which is the format of the swap chain's color buffer
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_swapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[1]                = TEX_FORMAT_RGBA8_UNORM;
    // Use the depth buffer format from the swap chain
    PSOCreateInfo.GraphicsPipeline.DSVFormat                    = m_swapChain->GetDesc().DepthBufferFormat;
    // Primitive topology defines what kind of primitives will be rendered by this pipeline state
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // No back face culling for this tutorial
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_BACK;
    // Disable depth testing
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    // clang-format on

    LayoutElement layoutElements[] = {
            LayoutElement(0, 0, 3, VT_FLOAT32),
            LayoutElement (1, 0, 3, VT_FLOAT32),
            LayoutElement (2, 0, 2, VT_FLOAT32)
    };
    m_mesh = new Mesh("mesh/sponza/sponza.obj");
    //m_mesh->addTexture("textures/Cerberus_M.tga", 0);
    //m_mesh->addTexture("Textures/Cerberus_N.tga", 0);
    //m_mesh->addTexture("Textures/Cerberus_R.tga", 0);

    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);


    ShaderResourceVariableDesc Vars[] =
    {
            {SHADER_TYPE_PIXEL, "g_TextureAlbedo", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables    = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    SamplerDesc SamLinearClampDesc
    {
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
    };
    ImmutableSamplerDesc ImtblSamplers[] =
    {
            {SHADER_TYPE_PIXEL, "g_TextureAlbedo", SamLinearClampDesc},
            {SHADER_TYPE_PIXEL, "g_TextureNormal", SamLinearClampDesc}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

    ShaderCreateInfo ShaderCI;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_engineFactory->CreateDefaultShaderSourceStreamFactory("shader/", &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    // Tell the system that the shader source code is in HLSL.
    // For OpenGL, the engine will convert this into GLSL under the hood
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    // OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
    ShaderCI.UseCombinedTextureSamplers = true;
    // Create a vertex shader
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Mesh vertex shader";
        ShaderCI.FilePath        = "gbuffer/vs.hlsl";
        m_device->CreateShader(ShaderCI, &pVS);
    }

    // Create a pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Mesh pixel shader";
        ShaderCI.FilePath        = "gbuffer/ps.hlsl";
        m_device->CreateShader(ShaderCI, &pPS);
    }

    // Finally, create the pipeline state
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    m_device->CreateGraphicsPipelineState(PSOCreateInfo, &m_PSOFinal);

    m_PSOFinal->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_mesh->getCB());

    m_PSOFinal->CreateShaderResourceBinding(&m_SRB, true);

    TextureDesc desc;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.BindFlags = Diligent::BIND_RENDER_TARGET;

    m_device->CreateTexture(desc, nullptr, &m_gbufferNormal);

    //ugly hack, this version of diligent doesn't include this strange, look into it later
    m_imguiRenderer = new ImGuiImplDiligent(m_device, m_swapChain->GetDesc().ColorBufferFormat, m_swapChain->GetDesc().DepthBufferFormat);
    m_imguiRenderer->CreateDeviceObjects();
    m_imguiRenderer->UpdateFontsTexture();

    m_camera.SetPos(float3(0, 0, -20));
    m_camera.SetProjAttribs(0.01f, 1000, (float)m_width / (float) m_height, 45.0f, Diligent::SURFACE_TRANSFORM_OPTIMAL, false);
}

void Engine::render()
{
    m_camera.Update(m_inputController, 0.16f);

    //m_imguiRenderer->NewFrame(m_width, m_height, Diligent::SURFACE_TRANSFORM_OPTIMAL);
       // ImGui::Text("%f %f %f", m_camera.GetPos().x, m_camera.GetPos().y, m_camera.GetPos().z);
   // m_imguiRenderer->EndFrame();

    // Set render targets before issuing any draw command.
    // Note that Present() unbinds the back buffer if it is set as render target.
    ITextureView* pRTV[] = {m_swapChain->GetCurrentBackBufferRTV(), m_gbufferNormal->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    auto* pDSV = m_swapChain->GetDepthBufferDSV();
    m_immediateContext->SetRenderTargets(2, pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_immediateContext->SetPipelineState(m_PSOFinal);

    const float ClearColor[] = {0.0f, 181.f / 255.f, 221.f / 255.f, 1.0f};
    // Let the engine perform required state transitions
    m_immediateContext->ClearRenderTarget(pRTV[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_mesh->draw(m_immediateContext, m_SRB, m_camera);

   // m_imguiRenderer->Render(m_immediateContext);

}

void Engine::present()
{
    m_swapChain->Present();
}
