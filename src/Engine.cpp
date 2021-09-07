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
#include "Mesh.h"

static const char* VSSource = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos : ATTRIB0;
    float2 UV : ATTRIB1;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = g_Texture.Sample(g_Texture_sampler, PSIn.UV);
}
)";

// Pixel shader simply outputs interpolated vertex color
static const char* PSSource = R"(
cbuffer Constants
{
    float4x4 g_WorldViewProj;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV  : ATTRIB2;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV  : TEX_COORD;
};

void main(in  VSInput VSIn,
          out PSInput PSIn)
{
    PSIn.Pos = mul( float4(VSIn.Pos,1.0), g_WorldViewProj);
    PSIn.UV  = VSIn.UV;
}
)";

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
    PSOCreateInfo.PSODesc.Name = "Simple triangle PSO";

    // This is a graphics pipeline
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    // clang-format off
    // This tutorial will render to a single render target
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    // Set render target format which is the format of the swap chain's color buffer
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_swapChain->GetDesc().ColorBufferFormat;
    // Use the depth buffer format from the swap chain
    PSOCreateInfo.GraphicsPipeline.DSVFormat                    = m_swapChain->GetDesc().DepthBufferFormat;
    // Primitive topology defines what kind of primitives will be rendered by this pipeline state
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // No back face culling for this tutorial
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = Diligent::CULL_MODE_BACK;
    // Disable depth testing
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    // clang-format on

    BufferDesc cbDesc;
    cbDesc.Usage = Diligent::USAGE_DYNAMIC;
    cbDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cbDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cbDesc.uiSizeInBytes = sizeof(float4x4);
    m_device->CreateBuffer(cbDesc, nullptr, &m_cbBuffer);

    LayoutElement layoutElements[] = {
            LayoutElement(0, 0, 3, VT_FLOAT32),
            LayoutElement (1, 0, 3, VT_FLOAT32),
            LayoutElement (2, 0, 2, VT_FLOAT32)
    };
    m_mesh = new Mesh("mesh/Cerberus/Cerberus_LP.fbx");

    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);

    BufferDesc VertBuffDesc;
    VertBuffDesc.Name          = "Cube vertex buffer";
    VertBuffDesc.Usage         = USAGE_IMMUTABLE;
    VertBuffDesc.BindFlags     = BIND_VERTEX_BUFFER;
    VertBuffDesc.uiSizeInBytes          = sizeof(Vertex);
    BufferData VBData;
    VBData.pData    = m_mesh->getGroup().m_vertices.data();
    VBData.DataSize = m_mesh->getGroup().m_vertices.size();// AVOID THIS PLEEEASE

    BufferDesc IndexBuffDesc;
    IndexBuffDesc.Name          = "Cube vertex buffer";
    IndexBuffDesc.Usage         = USAGE_IMMUTABLE;
    IndexBuffDesc.BindFlags     = BIND_INDEX_BUFFER;
    IndexBuffDesc.uiSizeInBytes          = sizeof(uint);
    BufferData IBData;
    IBData.pData    = m_mesh->getGroup().m_indices.data();
    IBData.DataSize = m_mesh->getGroup().m_indices.size();// AVOID THIS PLEEEASE

    m_device->CreateBuffer(IndexBuffDesc, &IBData, &m_meshIndexBuffer);


    ShaderResourceVariableDesc Vars[] =
    {
            {SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
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
            {SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

    ShaderCreateInfo ShaderCI;
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
        ShaderCI.Source          = VSSource;
        m_device->CreateShader(ShaderCI, &pVS);
    }

    // Create a pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Mesh pixel shader";
        ShaderCI.Source          = PSSource;
        m_device->CreateShader(ShaderCI, &pPS);
    }

    // Finally, create the pipeline state
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    m_device->CreateGraphicsPipelineState(PSOCreateInfo, &m_PSO);

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->
    Set(dynamic_cast<ITexture*>(m_mesh->getGroup().m_textures[0]->GetUserData())->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

void Engine::render()
{
    {
        // Map the buffer and write current world-view-projection matrix
        MapHelper<float4x4> CBConstants(m_immediateContext, m_cbBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = (m_worldview);
    }


    // Set render targets before issuing any draw command.
    // Note that Present() unbinds the back buffer if it is set as render target.
    auto* pRTV = m_swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain->GetDepthBufferDSV();
    m_immediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Clear the back buffer
    const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
    // Let the engine perform required state transitions
    m_immediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_immediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set the pipeline state in the immediate context
    m_immediateContext->SetPipelineState(m_PSO);

    Uint32   offset   = 0;
    IBuffer* pBuffs[] = {m_meshVertexBuffer};
    m_immediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);
    m_immediateContext->SetIndexBuffer(m_meshIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_immediateContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);


    // Typically we should now call CommitShaderResources(), however shaders in this example don't
    // use any resources.

    DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
    DrawAttrs.IndexType  = VT_UINT32; // Index type
    DrawAttrs.NumIndices = m_mesh->getGroup().m_indices.size();
    // Verify the state of vertex and index buffers as well as consistence of
    // render targets and correctness of draw command arguments
    DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
    m_immediateContext->DrawIndexed(DrawAttrs);
}

void Engine::present()
{
    m_swapChain->Present();
}
