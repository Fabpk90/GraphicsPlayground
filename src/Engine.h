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
    RefCntAutoPtr<IPipelineState> m_PSOGBuffer;
    RefCntAutoPtr<IPipelineState> m_PSOShowGBuffer;
    RefCntAutoPtr<IPipelineState> m_PSOFinal;

    ImGuiImplDiligent* m_imguiRenderer;

    RefCntAutoPtr<ITexture> m_gbufferNormal;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    Mesh* m_mesh;
    FirstPersonCamera m_camera;

};


#endif //GRAPHICSPLAYGROUND_ENGINE_H
