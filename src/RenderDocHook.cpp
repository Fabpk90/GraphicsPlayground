//
// Created by fab on 14/05/2022.
//

#include "RenderDocHook.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

RenderDocHook::RenderDocHook(RENDERDOC_DevicePointer devicePointer, RENDERDOC_WindowHandle windowHandle)
{
    if(HMODULE mod = LoadLibraryA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&m_api);

        m_api->SetActiveWindow(devicePointer, windowHandle);
        m_api->SetCaptureFilePathTemplate("captures/GraphicsPlayground");

        RENDERDOC_InputButton keys[] = {RENDERDOC_InputButton::eRENDERDOC_Key_F4};

        m_api->SetCaptureKeys(keys, 1);

    }
}

void RenderDocHook::startCapture()
{
    if(m_api) m_api->StartFrameCapture(nullptr, nullptr);
}

void RenderDocHook::endCapture()
{
    if(m_api) m_api->EndFrameCapture(nullptr, nullptr);
}
