//
// Created by FanyMontpell on 03/09/2021.
//

#ifndef GRAPHICSPLAYGROUND_ENGINE_H
#define GRAPHICSPLAYGROUND_ENGINE_H

#include <windows.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/SwapChain.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/PipelineState.h>
#include <diligent/include/Common/interface/RefCntAutoPtr.hpp>
#include <diligent/include/Common/interface/BasicMath.hpp>
#include <diligent/include/Imgui/interface/ImGuiImplDiligent.hpp>
#include "Mesh.h"
#include "FirstPersonCamera.hpp"

using namespace Diligent;

struct Constants
{
    float4 m_params;
    float4 m_lightPos;
    float4 m_camPos;
};

class Engine {

public:
    Engine()
    {
        instance = this;
    };
    ~Engine()
    {
        m_immediateContext->Flush();
    }

    Engine(Engine&&) = delete;
    Engine(Engine&) = delete;

    static Engine* instance;

public:
    static RENDER_DEVICE_TYPE constexpr getRenderType() { return Diligent::RENDER_DEVICE_TYPE_VULKAN;}
    void windowResize(int _width, int _height)
    {
        m_width = _width; m_height = _height;
        if(m_immediateContext)
            m_immediateContext->SetViewports(1, nullptr, m_width, m_height);
    }
    bool initializeDiligentEngine(HWND hwnd);
    void createResources();

    void createLightingPipeline();
    void renderLighting();

    void render();
    void present();

    RefCntAutoPtr<IRenderDevice>& getDevice() {return m_device;}

public:
    InputControllerWin32 m_inputController;

private:
    int m_width, m_height;

    IEngineFactory*               m_engineFactory  = nullptr;

    RefCntAutoPtr<IRenderDevice>  m_device;
    RefCntAutoPtr<IDeviceContext> m_immediateContext;
    RefCntAutoPtr<ISwapChain>     m_swapChain;
    RefCntAutoPtr<IPipelineState> m_PSOLighting;
    RefCntAutoPtr<IShaderResourceBinding> m_lightingSRB;
    RefCntAutoPtr<IPipelineState> m_PSOShowGBuffer;
    RefCntAutoPtr<IPipelineState> m_PSOFinal;

    ImGuiImplDiligent* m_imguiRenderer;

    RefCntAutoPtr<ITexture> m_gbufferNormal;
    RefCntAutoPtr<ITexture> m_gbufferAlbedo;
    RefCntAutoPtr<ITexture> m_depthTexture;

    RefCntAutoPtr<ITexture> m_finalTexture;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    RefCntAutoPtr<IBuffer> m_bufferMatrixMesh;

    std::vector<Mesh*> m_meshes;
    FirstPersonCamera m_camera;

    RefCntAutoPtr<IBuffer> m_bufferLighting;
};


#endif //GRAPHICSPLAYGROUND_ENGINE_H
